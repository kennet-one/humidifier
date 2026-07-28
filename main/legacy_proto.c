// SPDX-License-Identifier: Apache-2.0
#include "legacy_proto.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "humid_ctrl.h"
#include "legacy_root_sender.h"
#include "pms5003_node.h"
#include "water_led.h"

static const char *TAG = "legacy";

static void copy_result(char *result, size_t result_size, const char *text)
{
	if (!result || result_size == 0) return;
	snprintf(result, result_size, "%s", text ? text : "");
	result[result_size - 1] = '\0';
}

bool legacy_handle_command(const char *text, esp_err_t *command_error,
			   char *result, size_t result_size)
{
	if (command_error) *command_error = ESP_OK;
	if (!text || !text[0]) return false;

	if (water_led_handle_command(text, command_error, result, result_size)) {
		return true;
	}
	if (humid_ctrl_handle_command(text, command_error, result, result_size)) {
		return true;
	}
	if (strcmp(text, "pm1") == 0) {
		esp_err_t err = pms5003_trigger_once();
		if (command_error) *command_error = err;
		copy_result(result, result_size,
			    err == ESP_OK ? "pm1 accepted" :
			    err == ESP_ERR_INVALID_STATE ? "pm1 busy" :
			    esp_err_to_name(err));
		return true;
	}
	return false;
}

void legacy_handle_text(const char *text)
{
	esp_err_t err = ESP_OK;
	char result[64] = {0};
	if (!legacy_handle_command(text, &err, result, sizeof(result))) {
		ESP_LOGW(TAG, "unsupported command: \"%s\"", text ? text : "");
		return;
	}
	if (err != ESP_OK) {
		ESP_LOGW(TAG, "command \"%s\" failed: %s", text, esp_err_to_name(err));
		if (strcmp(result, "power_off") == 0) {
			(void)legacy_send_to_root("ERR:POWER_OFF:humidifier");
		} else {
			(void)legacy_send_to_root("ERR:REJECTED:humidifier");
		}
	}
}
