// SPDX-License-Identifier: Apache-2.0
#include <inttypes.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_mesh.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "keemash_log_time_vprintf.h"
#include "keemash_mesh_node.h"
#include "keemash_mesh_ota_receiver.h"
#include "legacy_proto.h"
#include "legacy_root_sender.h"
#include "mesh_log_stream.h"
#include "mesh_proto.h"
#include "mesh_security.h"
#include "mesh_time_sync.h"
#include "mesh_v2_link.h"
#include "humid_ctrl.h"
#include "pms5003_node.h"
#include "water_led.h"

#define RX_SIZE 512
#define RECOVERY_CHECK_MS 5000U
#define ACK_FRESH_MS 30000U
#define RECOVERY_BURST_MS 5000U
#define RECOVERY_RECONNECT_MS 30000U
#define RECOVERY_RESTART_MS 75000U
#define RECOVERY_LOG_MS 15000U
#define RECOVERY_ACTION_BACKOFF_MS 20000U

#ifdef CONFIG_HUMIDIFIER_MESH_RELAY_ELIGIBLE
#define HUMIDIFIER_RELAY_ELIGIBLE true
#else
#define HUMIDIFIER_RELAY_ELIGIBLE false
#endif

typedef enum {
	RECOVERY_OK = 0,
	RECOVERY_WAIT_ACK = 1,
	RECOVERY_RECONNECT = 2,
	RECOVERY_MESH_RESTART = 3,
} recovery_phase_t;

static const char *TAG = "humidifier";
static const uint8_t MESH_ID[6] = {0x77, 0x77, 0x77, 0x77, 0x77, 0x77};

static _Atomic bool s_running = true;
static _Atomic bool s_parent_connected;
static _Atomic bool s_mesh_recovering;
static esp_netif_t *s_netif_sta;
static mesh_addr_t s_parent_addr;
static mesh_addr_t s_root_mesh_addr;
static portMUX_TYPE s_runtime_lock = portMUX_INITIALIZER_UNLOCKED;
static _Atomic int s_layer = -1;
static _Atomic uint32_t s_unhealthy_since_ms;
static _Atomic uint32_t s_last_burst_ms;
static _Atomic uint32_t s_last_recovery_log_ms;
static _Atomic uint32_t s_last_recovery_action_ms;
static _Atomic uint32_t s_last_soft_reconnect_ms;
static _Atomic uint32_t s_boot_seq;
static _Atomic uint16_t s_parent_disconnect_count;
static _Atomic uint16_t s_no_parent_count;
static _Atomic uint16_t s_rootless_count;
static _Atomic uint16_t s_soft_reconnect_count;
static _Atomic uint16_t s_mesh_restart_count;
static _Atomic uint16_t s_ack_stale_count;
static _Atomic uint16_t s_tx_without_ack_count;
static _Atomic uint8_t s_last_parent_disconnect_reason;
static _Atomic uint8_t s_last_recovery_reason;
static _Atomic recovery_phase_t s_recovery_phase = RECOVERY_OK;
static _Atomic bool s_rollback_pending;
static _Atomic bool s_telemetry_synced;
static bool s_application_healthy;
static _Atomic bool s_mesh_initialized;
static _Atomic bool s_mesh_event_registered;
static _Atomic bool s_mesh_started;
static TaskHandle_t s_mesh_rx_task;
static TaskHandle_t s_recovery_task;
static TaskHandle_t s_network_supervisor_task;

typedef enum {
	NETWORK_STAGE_NVS = 0,
	NETWORK_STAGE_NETIF,
	NETWORK_STAGE_EVENT_LOOP,
	NETWORK_STAGE_MESH_NETIFS,
	NETWORK_STAGE_WIFI,
	NETWORK_STAGE_WIFI_STORAGE,
	NETWORK_STAGE_WIFI_START,
	NETWORK_STAGE_LOG_TIME,
	NETWORK_STAGE_LINK,
	NETWORK_STAGE_OUTBOX,
	NETWORK_STAGE_OTA,
	NETWORK_STAGE_MESH,
	NETWORK_STAGE_READY,
} network_stage_t;

static network_stage_t s_network_stage;

static uint32_t tick_ms(void)
{
	return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static bool copy_config_value(uint8_t *destination, size_t destination_size,
			      const char *value, size_t *written)
{
	if (!destination || !value) return false;
	size_t length = strlen(value);
	if (length >= destination_size) return false;
	memset(destination, 0, destination_size);
	memcpy(destination, value, length);
	if (written) *written = length;
	return true;
}

static esp_err_t fill_mesh_config(mesh_cfg_t *config)
{
	if (!config) return ESP_ERR_INVALID_ARG;
	*config = (mesh_cfg_t)MESH_INIT_CONFIG_DEFAULT();
	memcpy(config->mesh_id.addr, MESH_ID, sizeof(MESH_ID));
	config->channel = CONFIG_MESH_CHANNEL;

	size_t ssid_length = 0;
	if (!copy_config_value(config->router.ssid, sizeof(config->router.ssid),
			       CONFIG_MESH_ROUTER_SSID, &ssid_length) ||
	    !copy_config_value(config->router.password, sizeof(config->router.password),
			       CONFIG_MESH_ROUTER_PASSWD, NULL) ||
	    !copy_config_value(config->mesh_ap.password, sizeof(config->mesh_ap.password),
			       CONFIG_MESH_AP_PASSWD, NULL)) {
		ESP_LOGE(TAG, "mesh credential length exceeds ESP-MESH limits");
		return ESP_ERR_INVALID_SIZE;
	}
	config->router.ssid_len = ssid_length;
	config->mesh_ap.max_connection = CONFIG_MESH_AP_CONNECTIONS;
	config->mesh_ap.nonmesh_max_connection = CONFIG_MESH_NON_MESH_AP_CONNECTIONS;
	return ESP_OK;
}

static esp_err_t configure_mesh(void)
{
	esp_err_t err = esp_mesh_fix_root(false);
	if (err != ESP_OK) return err;
	if ((err = esp_mesh_set_topology(CONFIG_MESH_TOPOLOGY)) != ESP_OK) return err;
	if ((err = esp_mesh_set_max_layer(CONFIG_MESH_MAX_LAYER)) != ESP_OK) return err;
	if ((err = esp_mesh_set_vote_percentage(1)) != ESP_OK) return err;
	if ((err = esp_mesh_set_xon_qsize(128)) != ESP_OK) return err;
	if ((err = esp_mesh_disable_ps()) != ESP_OK) return err;
	if ((err = esp_mesh_set_ap_assoc_expire(10)) != ESP_OK) return err;
	if ((err = esp_mesh_set_ap_authmode(CONFIG_MESH_AP_AUTHMODE)) != ESP_OK) return err;

	mesh_cfg_t config;
	if ((err = fill_mesh_config(&config)) != ESP_OK) return err;
	return esp_mesh_set_config(&config);
}

static void update_topology(void)
{
	uint8_t root_mac[6] = {0};
	uint8_t parent_mac[6] = {0};
	(void)mesh_v2_node_get_root_mac(root_mac);
	portENTER_CRITICAL(&s_runtime_lock);
	memcpy(parent_mac, s_parent_addr.addr, sizeof(parent_mac));
	portEXIT_CRITICAL(&s_runtime_lock);
	wifi_ap_record_t parent = {0};
	int8_t rssi = 0;
	if (esp_wifi_sta_get_ap_info(&parent) == ESP_OK) rssi = parent.rssi;
	int route_count = esp_mesh_get_routing_table_size();
	uint8_t descendants = route_count > 255 ? 255 :
			      route_count > 0 ? (uint8_t)route_count : 0;
	mesh_v2_node_update_topology(parent_mac, root_mac,
				     s_layer > 0 ? (uint16_t)s_layer : 0,
				     CONFIG_MESH_MAX_LAYER, rssi, descendants);
}

static void update_diagnostics(void)
{
	mesh_v2_node_diag_t diagnostics = {
		.boot_seq = s_boot_seq,
		.last_recovery_action_ms = s_last_recovery_action_ms,
		.last_mesh_send_err = mesh_log_stream_last_send_err(),
		.ack_stale_count = s_ack_stale_count,
		.tx_without_ack_count = s_tx_without_ack_count,
		.reset_reason = (uint16_t)esp_reset_reason(),
		.parent_disconnect_count = s_parent_disconnect_count,
		.no_parent_count = s_no_parent_count,
		.rootless_count = s_rootless_count,
		.soft_reconnect_count = s_soft_reconnect_count,
		.mesh_restart_count = s_mesh_restart_count,
		.last_parent_disconnect_reason = s_last_parent_disconnect_reason,
		.last_recovery_reason = s_last_recovery_reason,
	};
	mesh_v2_node_update_diagnostics(&diagnostics);
	mesh_v2_node_set_recovery_phase((uint8_t)s_recovery_phase);
}

static void request_root_resync(void)
{
	update_topology();
	update_diagnostics();
	(void)mesh_v2_node_force_hello(false);
}

static void send_telemetry_burst(void)
{
	update_topology();
	update_diagnostics();
	mesh_log_stream_kick_nodeinfo_burst();
	(void)mesh_log_stream_send_nodeinfo_now();
	(void)mesh_v2_node_send_topology();
	(void)mesh_v2_node_send_memory();
	(void)humid_ctrl_publish_status();
}

static esp_err_t full_mesh_restart(void)
{
	if (s_mesh_recovering) return ESP_ERR_INVALID_STATE;
	s_mesh_recovering = true;
	s_parent_connected = false;
	s_mesh_started = false;
	mesh_v2_link_parent_disconnected();
	mesh_v2_node_set_root_mac(NULL);

	esp_err_t err = esp_mesh_stop();
	if (err != ESP_OK && err != ESP_ERR_MESH_NOT_START) goto done;
	vTaskDelay(pdMS_TO_TICKS(250));
	err = esp_mesh_deinit();
	if (err != ESP_OK && err != ESP_ERR_MESH_NOT_INIT) goto done;
	err = esp_mesh_init();
	if (err != ESP_OK) goto done;
	err = configure_mesh();
	if (err != ESP_OK) goto done;
	err = esp_mesh_start();
	if (err == ESP_OK) s_mesh_started = true;

done:
	s_mesh_recovering = false;
	return err;
}

static void mark_rollback_valid_when_healthy(void)
{
	if (!s_rollback_pending || !s_application_healthy || !s_parent_connected ||
	    !mesh_v2_node_reliable_ready()) {
		return;
	}
	esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
	if (err == ESP_OK) {
		s_rollback_pending = false;
		ESP_LOGI(TAG, "OTA rollback: running app marked valid after reliable handshake");
	} else {
		ESP_LOGE(TAG, "OTA rollback mark-valid failed: %s", esp_err_to_name(err));
	}
}

static void recovery_task(void *arg)
{
	(void)arg;
	for (;;) {
		vTaskDelay(pdMS_TO_TICKS(RECOVERY_CHECK_MS));
		uint32_t now = tick_ms();
		mark_rollback_valid_when_healthy();
		update_diagnostics();

		if (!s_parent_connected) {
			if (s_unhealthy_since_ms == 0) s_unhealthy_since_ms = now;
			uint32_t down_ms = now - s_unhealthy_since_ms;
			if (down_ms >= RECOVERY_ACTION_BACKOFF_MS &&
			    (s_last_soft_reconnect_ms == 0 ||
			     now - s_last_soft_reconnect_ms >= RECOVERY_ACTION_BACKOFF_MS) &&
			    !keemash_mesh_ota_receiver_active()) {
				s_last_soft_reconnect_ms = now;
				s_soft_reconnect_count++;
				s_last_recovery_reason = MESH_V2_RECOVERY_REASON_SOFT_RECONNECT;
				s_last_recovery_action_ms = now;
				(void)esp_mesh_connect();
			}
			if (down_ms >= RECOVERY_RESTART_MS &&
			    !keemash_mesh_ota_receiver_active() && !s_mesh_recovering) {
				s_recovery_phase = RECOVERY_MESH_RESTART;
				s_mesh_restart_count++;
				s_last_recovery_reason = MESH_V2_RECOVERY_REASON_MESH_RESTART;
				s_last_recovery_action_ms = now;
				esp_err_t err = full_mesh_restart();
				ESP_LOGW(TAG, "mesh recovery restart: %s", esp_err_to_name(err));
				s_unhealthy_since_ms = tick_ms();
				s_last_soft_reconnect_ms = 0;
			}
			continue;
		}

		if (mesh_v2_node_ack_fresh(ACK_FRESH_MS)) {
			if (s_recovery_phase != RECOVERY_OK) {
				ESP_LOGI(TAG, "reliable root handshake restored");
			}
			s_recovery_phase = RECOVERY_OK;
			s_unhealthy_since_ms = 0;
			s_last_recovery_reason = MESH_V2_RECOVERY_REASON_NONE;
			s_last_soft_reconnect_ms = 0;
			if (!s_telemetry_synced) {
				s_telemetry_synced = true;
				send_telemetry_burst();
			}
			continue;
		}

		if (s_unhealthy_since_ms == 0) {
			s_unhealthy_since_ms = now;
			s_ack_stale_count++;
		}
		uint32_t down_ms = now - s_unhealthy_since_ms;
		s_recovery_phase = RECOVERY_WAIT_ACK;
		if (mesh_log_stream_tx_accepted_age_ms() < ACK_FRESH_MS) s_tx_without_ack_count++;

		if (s_last_burst_ms == 0 || now - s_last_burst_ms >= RECOVERY_BURST_MS) {
			s_last_burst_ms = now;
			request_root_resync();
		}
		if (down_ms >= RECOVERY_RECONNECT_MS &&
		    mesh_v2_node_protocol_recovery_exhausted() &&
		    (s_last_soft_reconnect_ms == 0 ||
		     now - s_last_soft_reconnect_ms >= RECOVERY_ACTION_BACKOFF_MS) &&
		    !keemash_mesh_ota_receiver_active()) {
			s_last_soft_reconnect_ms = now;
			s_recovery_phase = RECOVERY_RECONNECT;
			s_soft_reconnect_count++;
			s_last_recovery_reason = MESH_V2_RECOVERY_REASON_SOFT_RECONNECT;
			s_last_recovery_action_ms = now;
			(void)esp_mesh_disconnect();
			vTaskDelay(pdMS_TO_TICKS(250));
			(void)esp_mesh_connect();
		}
		if (s_last_recovery_log_ms == 0 || now - s_last_recovery_log_ms >= RECOVERY_LOG_MS) {
			s_last_recovery_log_ms = now;
			ESP_LOGW(TAG, "root recovery phase=%u down=%lu ack_age=%lu last_tx=%s",
				 (unsigned)s_recovery_phase, (unsigned long)down_ms,
				 (unsigned long)mesh_v2_node_ack_age_ms(),
				 esp_err_to_name(mesh_log_stream_last_send_err()));
		}
	}
}

static void mesh_rx_task(void *arg)
{
	(void)arg;
	uint8_t rx_buffer[RX_SIZE];
	mesh_data_t data = {.data = rx_buffer, .size = sizeof(rx_buffer)};
	mesh_addr_t from;
	int flags = 0;

	while (s_running) {
		data.size = sizeof(rx_buffer);
		esp_err_t err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flags, NULL, 0);
		if (err != ESP_OK) {
			if (!s_mesh_recovering) ESP_LOGW(TAG, "mesh RX failed: %s", esp_err_to_name(err));
			continue;
		}
		if (data.size < sizeof(mesh_pkt_hdr_t)) continue;

		const mesh_pkt_hdr_t *header = (const mesh_pkt_hdr_t *)rx_buffer;
		if (header->magic != MESH_PKT_MAGIC) continue;
		if (header->version == MESH_PKT_VERSION_V2) {
			(void)mesh_v2_node_handle_rx(from.addr, rx_buffer, data.size);
			continue;
		}
		if (header->version != MESH_PKT_VERSION) continue;
		uint8_t expected_root[6] = {0};
		bool have_protocol_root = mesh_v2_node_get_root_mac(expected_root);
		const uint8_t *root_identity = have_protocol_root
			? expected_root : s_root_mesh_addr.addr;
		bool have_root_identity = memcmp(root_identity,
						 (const uint8_t[6]){0}, 6) != 0;
		if (!have_root_identity ||
		    !humidifier_v1_origin_allowed(
			    from.addr, header->src_mac, root_identity,
			    mesh_v2_node_lossless_negotiated())) {
			ESP_LOGW(TAG, "rejected V1 type=%u from non-root " MACSTR,
				 (unsigned)header->type, MAC2STR(from.addr));
			continue;
		}

		switch (header->type) {
		case MESH_TIME_SYNC_TYPE_TIME:
			(void)mesh_time_sync_handle_rx(rx_buffer, data.size);
			break;
		case MESH_LOG_TYPE_CTRL:
			(void)mesh_log_stream_handle_rx(rx_buffer, data.size);
			break;
		case MESH_PKT_TYPE_TEXT:
			if (data.size >= sizeof(mesh_packet_t)) {
				const mesh_packet_t *packet = (const mesh_packet_t *)rx_buffer;
				char text[sizeof(packet->payload)];
				memcpy(text, packet->payload, sizeof(text));
				text[sizeof(text) - 1] = '\0';
				legacy_handle_text(text);
			}
			break;
		case MESH_OTA_TYPE_BEGIN:
		case MESH_OTA_TYPE_DATA:
		case MESH_OTA_TYPE_END:
		case MESH_OTA_TYPE_ABORT:
			(void)keemash_mesh_ota_receiver_handle_rx(rx_buffer, data.size);
			break;
		default:
			break;
		}
	}
	vTaskDelete(NULL);
}

static void on_parent_connected(const mesh_event_connected_t *connected)
{
	s_layer = connected->self_layer;
	portENTER_CRITICAL(&s_runtime_lock);
	memcpy(s_parent_addr.addr, connected->connected.bssid, sizeof(s_parent_addr.addr));
	portEXIT_CRITICAL(&s_runtime_lock);
	s_parent_connected = true;
	s_telemetry_synced = false;
	s_unhealthy_since_ms = tick_ms();
	mesh_v2_link_parent_connected();
	update_topology();
	update_diagnostics();
	ESP_LOGI(TAG, "parent connected layer=%d parent=" MACSTR,
		 s_layer, MAC2STR(s_parent_addr.addr));
}

static void mesh_event_handler(void *arg, esp_event_base_t base,
			       int32_t event_id, void *event_data)
{
	(void)arg;
	(void)base;
	switch (event_id) {
	case MESH_EVENT_STARTED:
		s_mesh_started = true;
		ESP_LOGI(TAG, "mesh started");
		break;
	case MESH_EVENT_STOPPED:
		s_mesh_started = false;
		s_parent_connected = false;
		s_telemetry_synced = false;
		mesh_v2_link_parent_disconnected();
		ESP_LOGW(TAG, "mesh stopped");
		break;
	case MESH_EVENT_CHILD_CONNECTED:
	case MESH_EVENT_CHILD_DISCONNECTED:
	case MESH_EVENT_ROUTING_TABLE_ADD:
	case MESH_EVENT_ROUTING_TABLE_REMOVE:
		update_topology();
		if (s_parent_connected) (void)mesh_v2_node_send_topology();
		break;
	case MESH_EVENT_PARENT_CONNECTED:
		on_parent_connected((const mesh_event_connected_t *)event_data);
		break;
	case MESH_EVENT_PARENT_DISCONNECTED: {
		const mesh_event_disconnected_t *event = event_data;
		s_parent_connected = false;
		s_parent_disconnect_count++;
		s_last_parent_disconnect_reason = (uint8_t)event->reason;
		s_last_recovery_reason = MESH_V2_RECOVERY_REASON_PARENT_DISC;
		s_unhealthy_since_ms = tick_ms();
		mesh_v2_link_parent_disconnected();
		mesh_log_stream_clear_tx_accepted();
		ESP_LOGW(TAG, "parent disconnected reason=%d", event->reason);
		break;
	}
	case MESH_EVENT_NO_PARENT_FOUND: {
		const mesh_event_no_parent_found_t *event = event_data;
		s_no_parent_count++;
		s_last_recovery_reason = MESH_V2_RECOVERY_REASON_NO_PARENT;
		if (s_unhealthy_since_ms == 0) s_unhealthy_since_ms = tick_ms();
		ESP_LOGW(TAG, "no parent found scans=%d", event->scan_times);
		break;
	}
	case MESH_EVENT_LAYER_CHANGE:
		s_layer = ((mesh_event_layer_change_t *)event_data)->new_layer;
		update_topology();
		(void)mesh_v2_node_send_topology();
		break;
	case MESH_EVENT_ROOT_ADDRESS: {
		const mesh_event_root_address_t *event = event_data;
		portENTER_CRITICAL(&s_runtime_lock);
		bool changed = memcmp(s_root_mesh_addr.addr, event->addr,
				      sizeof(s_root_mesh_addr.addr)) != 0;
		if (changed) {
			bool had_root = memcmp(s_root_mesh_addr.addr,
					      (const uint8_t[6]){0}, 6) != 0;
			memcpy(s_root_mesh_addr.addr, event->addr, sizeof(s_root_mesh_addr.addr));
			portEXIT_CRITICAL(&s_runtime_lock);
			if (had_root) mesh_v2_node_forget_root();
			else mesh_v2_node_set_root_mac(NULL);
		} else {
			portEXIT_CRITICAL(&s_runtime_lock);
		}
		ESP_LOGI(TAG, "root address=" MACSTR, MAC2STR(event->addr));
		if (s_parent_connected && (changed || !mesh_v2_node_reliable_ready())) {
			mesh_v2_node_kick_root();
		}
		break;
	}
	case MESH_EVENT_NETWORK_STATE: {
		const mesh_event_network_state_t *event = event_data;
		if (event->is_rootless) {
			s_rootless_count++;
			s_last_recovery_reason = MESH_V2_RECOVERY_REASON_ROOTLESS;
			s_unhealthy_since_ms = tick_ms();
			s_telemetry_synced = false;
			mesh_v2_node_set_root_mac(NULL);
			if (s_parent_connected) mesh_v2_node_kick_root();
		}
		break;
	}
	default:
		break;
	}
}

static esp_err_t init_nvs(void)
{
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_LOGW(TAG, "NVS requires recovery erase: %s", esp_err_to_name(err));
		err = nvs_flash_erase();
		if (err == ESP_OK) err = nvs_flash_init();
	}
	return err;
}

static esp_err_t application_init(void)
{
	esp_err_t relay_err = humid_ctrl_init();
	esp_err_t led_err = water_led_init();
	esp_err_t pms_err = pms5003_start(5);
	s_application_healthy =
		relay_err == ESP_OK && led_err == ESP_OK && pms_err == ESP_OK;
	if (!s_application_healthy) {
		ESP_LOGE(TAG, "application degraded relay=%s led=%s pms=%s",
			 esp_err_to_name(relay_err), esp_err_to_name(led_err),
			 esp_err_to_name(pms_err));
		return relay_err != ESP_OK ? relay_err :
		       led_err != ESP_OK ? led_err : pms_err;
	}
	ESP_LOGI(TAG, "application modules ready before mesh startup");
	return ESP_OK;
}

static esp_err_t ensure_mesh_tasks(void)
{
	if (!s_mesh_rx_task &&
	    xTaskCreate(mesh_rx_task, "mesh_rx", 6144, NULL, 6,
			&s_mesh_rx_task) != pdPASS) {
		return ESP_ERR_NO_MEM;
	}
	if (!s_recovery_task &&
	    xTaskCreate(recovery_task, "mesh_recovery", 4096, NULL, 5,
			&s_recovery_task) != pdPASS) {
		return ESP_ERR_NO_MEM;
	}
	return ESP_OK;
}

static esp_err_t try_start_mesh(void)
{
	esp_err_t err;
	if (!s_mesh_initialized) {
		err = esp_mesh_init();
		if (err != ESP_OK) return err;
		s_mesh_initialized = true;
	}
	if (!s_mesh_event_registered) {
		err = esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID,
						 mesh_event_handler, NULL);
		if (err != ESP_OK) return err;
		s_mesh_event_registered = true;
	}
	if (!s_mesh_started) {
		err = configure_mesh();
		if (err != ESP_OK) return err;
		err = esp_mesh_start();
		if (err != ESP_OK) return err;
		s_mesh_started = true;
	}
	return ensure_mesh_tasks();
}

static esp_err_t network_start_step(void)
{
	esp_err_t err = ESP_OK;
	switch (s_network_stage) {
	case NETWORK_STAGE_NVS:
		err = init_nvs();
		break;
	case NETWORK_STAGE_NETIF:
		err = esp_netif_init();
		if (err == ESP_ERR_INVALID_STATE) err = ESP_OK;
		break;
	case NETWORK_STAGE_EVENT_LOOP:
		err = esp_event_loop_create_default();
		if (err == ESP_ERR_INVALID_STATE) err = ESP_OK;
		break;
	case NETWORK_STAGE_MESH_NETIFS:
		err = esp_netif_create_default_wifi_mesh_netifs(&s_netif_sta, NULL);
		break;
	case NETWORK_STAGE_WIFI: {
		wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
		err = esp_wifi_init(&wifi_config);
		break;
	}
	case NETWORK_STAGE_WIFI_STORAGE:
		err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
		break;
	case NETWORK_STAGE_WIFI_START:
		err = esp_wifi_start();
		if (err == ESP_ERR_INVALID_STATE) err = ESP_OK;
		break;
	case NETWORK_STAGE_LOG_TIME:
		err = keemash_log_time_vprintf_start();
		if (err != ESP_OK) {
			ESP_LOGW(TAG, "timestamp log hook unavailable: %s",
				 esp_err_to_name(err));
		}
		mesh_time_sync_init();
		err = ESP_OK;
		break;
	case NETWORK_STAGE_LINK:
		err = mesh_v2_link_init(TAG, HUMIDIFIER_RELAY_ELIGIBLE);
		break;
	case NETWORK_STAGE_OUTBOX:
		err = legacy_root_sender_start(4);
		break;
	case NETWORK_STAGE_OTA:
		err = keemash_mesh_ota_receiver_start();
		break;
	case NETWORK_STAGE_MESH:
		err = try_start_mesh();
		break;
	case NETWORK_STAGE_READY:
		return ESP_OK;
	}
	if (err == ESP_OK) s_network_stage++;
	return err;
}

static void network_supervisor_task(void *argument)
{
	(void)argument;
	uint32_t failures = 0;
	while (s_network_stage != NETWORK_STAGE_READY) {
		esp_err_t err = network_start_step();
		if (err == ESP_OK) {
			failures = 0;
			continue;
		}
		failures++;
		uint32_t delay_ms = failures < 3 ? 1000U : 10000U;
		ESP_LOGW(TAG, "network stage %u retry %lu failed: %s",
			 (unsigned)s_network_stage, (unsigned long)failures,
			 esp_err_to_name(err));
		vTaskDelay(pdMS_TO_TICKS(delay_ms));
	}

	const esp_partition_t *running = esp_ota_get_running_partition();
	ESP_LOGI(TAG,
		 "ready idf=%s app=%s slot=%s flash=%s app_ok=%u relay=%u ps=%u",
		 esp_get_idf_version(), esp_app_get_description()->version,
		 running ? running->label : "?", CONFIG_ESPTOOLPY_FLASHSIZE,
		 s_application_healthy ? 1U : 0U,
		 HUMIDIFIER_RELAY_ELIGIBLE ? 1U : 0U,
		 esp_mesh_is_ps_enabled() ? 1U : 0U);
	s_network_supervisor_task = NULL;
	vTaskDelete(NULL);
}

static void detect_rollback_state(void)
{
	const esp_partition_t *running = esp_ota_get_running_partition();
	esp_ota_img_states_t state;
	if (running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
	    state == ESP_OTA_IMG_PENDING_VERIFY) {
		s_rollback_pending = true;
		ESP_LOGW(TAG, "OTA rollback pending until reliable root handshake");
	}
}

void app_main(void)
{
	esp_err_t application_error = application_init();
	if (application_error != ESP_OK) {
		ESP_LOGW(TAG, "continuing with network diagnostics in degraded mode");
	}
	detect_rollback_state();
	s_boot_seq = esp_random();
	if (xTaskCreate(network_supervisor_task, "network_supervisor", 4096, NULL, 4,
			&s_network_supervisor_task) != pdPASS) {
		ESP_LOGE(TAG, "failed to create network supervisor");
	}
}
