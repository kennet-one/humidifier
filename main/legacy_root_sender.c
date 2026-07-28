// SPDX-License-Identifier: Apache-2.0
#include "legacy_root_sender.h"

#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"

#include "keemash_mesh_hooks.h"
#include "keemash_mesh_event_outbox.h"
#include "keemash_mesh_node.h"
#include "mesh_proto.h"

static const char *TAG = "legacy_root_tx";
static portMUX_TYPE s_counter_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_counter;
static keemash_mesh_event_outbox_t *s_outbox;

static esp_err_t send_now(void *user, const char *text)
{
	(void)user;
	if (mesh_v2_node_lossless_negotiated()) {
		if (!mesh_v2_node_reliable_ready()) return ESP_ERR_INVALID_STATE;
		return mesh_v2_node_send_event(0, text);
	}

	mesh_packet_t packet = {0};
	packet.magic = MESH_PKT_MAGIC;
	packet.version = MESH_PKT_VERSION;
	packet.type = MESH_PKT_TYPE_TEXT;
	portENTER_CRITICAL(&s_counter_lock);
	packet.counter = ++s_counter;
	portEXIT_CRITICAL(&s_counter_lock);
	(void)esp_wifi_get_mac(WIFI_IF_STA, packet.src_mac);
	strncpy(packet.payload, text, sizeof(packet.payload) - 1);

	static const uint8_t root[6] = {0};
	return keemash_mesh_transport_send(root, &packet, sizeof(packet));
}

esp_err_t legacy_root_sender_start(UBaseType_t task_priority)
{
	if (s_outbox) return ESP_OK;
	keemash_mesh_event_outbox_config_t config = {
		.slots = 16,
		.text_size = LEGACY_ROOT_MSG_MAX_LEN,
		.retry_ms = 250,
		.task_stack_words = 3072,
		.task_priority = task_priority > 0 ? task_priority : 4,
		.task_name = "legacy_events",
		.send = send_now,
	};
	return keemash_mesh_event_outbox_init(&s_outbox, &config);
}

bool legacy_send_to_root(const char *text)
{
	if (!s_outbox || !text || !text[0]) return false;
	esp_err_t err = keemash_mesh_event_outbox_enqueue(s_outbox, NULL, text);
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "event enqueue failed: %s", esp_err_to_name(err));
		return false;
	}
	return true;
}

bool legacy_send_state_to_root(const char *key, const char *text)
{
	if (!s_outbox || !key || !key[0] || !text || !text[0]) return false;
	esp_err_t err = keemash_mesh_event_outbox_enqueue(s_outbox, key, text);
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "state event enqueue failed: %s", esp_err_to_name(err));
		return false;
	}
	return true;
}

bool legacy_send_group_to_root(const char *const *texts, size_t count)
{
	if (!s_outbox) return false;
	esp_err_t err = keemash_mesh_event_outbox_enqueue_group(s_outbox, texts, count);
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "event group enqueue failed: %s", esp_err_to_name(err));
		return false;
	}
	return true;
}

void legacy_root_sender_stats(keemash_mesh_event_outbox_stats_t *out)
{
	keemash_mesh_event_outbox_stats(s_outbox, out);
}
