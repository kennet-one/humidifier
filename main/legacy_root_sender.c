// SPDX-License-Identifier: Apache-2.0
#include "legacy_root_sender.h"

#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"

#include "keemash_mesh_hooks.h"
#include "keemash_mesh_node.h"
#include "mesh_proto.h"

static const char *TAG = "legacy_root_tx";
static portMUX_TYPE s_counter_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_counter;

void legacy_root_sender_start(UBaseType_t task_priority)
{
	(void)task_priority;
}

bool legacy_send_to_root(const char *text)
{
	if (!text || !text[0]) return false;
	if (mesh_v2_node_reliable_ready()) {
		esp_err_t event_err = mesh_v2_node_send_event(0, text);
		if (event_err != ESP_OK) {
			ESP_LOGW(TAG, "V2 event submit failed: %s",
				 esp_err_to_name(event_err));
			return false;
		}
		return true;
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
	esp_err_t err = keemash_mesh_transport_send(root, &packet, sizeof(packet));
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "V1 submit failed: %s", esp_err_to_name(err));
		return false;
	}
	return true;
}
