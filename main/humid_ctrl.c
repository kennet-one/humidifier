// SPDX-License-Identifier: Apache-2.0
#include "humid_ctrl.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "humidifier_board.h"
#include "legacy_root_sender.h"
#include "relay_block_pca8574.h"
#include "water_led.h"

static const char *TAG = "humid_ctrl";
static SemaphoreHandle_t s_lock;
static humid_ctrl_status_t s_status;

static void copy_result(char *result, size_t result_size, const char *text)
{
	if (!result || result_size == 0) return;
	snprintf(result, result_size, "%s", text ? text : "");
	result[result_size - 1] = '\0';
}

static uint8_t state_shadow(const humid_ctrl_status_t *state)
{
	uint8_t value = 0xff;
	if (state->pump_on) value &= (uint8_t)~(1U << HUMIDIFIER_RELAY_PUMP);
	if (state->flow_on) value &= (uint8_t)~(1U << HUMIDIFIER_RELAY_FLOW);
	if (state->ion_on) value &= (uint8_t)~(1U << HUMIDIFIER_RELAY_ION);
	switch (state->turbo) {
	case 1:
		value &= (uint8_t)~(1U << HUMIDIFIER_RELAY_TURBO1);
		break;
	case 2:
		value &= (uint8_t)~(1U << HUMIDIFIER_RELAY_TURBO2);
		break;
	case 3:
		value &= (uint8_t)~(1U << HUMIDIFIER_RELAY_TURBO3);
		break;
	default:
		break;
	}
	return value;
}

static esp_err_t commit_state_locked(const humid_ctrl_status_t *next)
{
	esp_err_t err = relay_block_write(state_shadow(next));
	if (err == ESP_OK) {
		s_status = *next;
		s_status.initialized = true;
		s_status.relay_shadow = relay_block_get_shadow();
		s_status.last_error = ESP_OK;
	} else {
		s_status.last_error = err;
	}
	return err;
}

void humid_ctrl_get_status(humid_ctrl_status_t *status)
{
	if (!status) return;
	if (!s_lock) {
		memset(status, 0, sizeof(*status));
		status->last_error = ESP_ERR_INVALID_STATE;
		return;
	}
	(void)xSemaphoreTake(s_lock, portMAX_DELAY);
	*status = s_status;
	xSemaphoreGive(s_lock);
}

static void format_echo(const humid_ctrl_status_t *state, char *reply, size_t reply_size)
{
	snprintf(reply, reply_size, "15%u%u%u%u", (unsigned)state->turbo,
		 state->pump_on ? 0U : 1U, state->flow_on ? 0U : 1U,
		 state->ion_on ? 0U : 1U);
}

esp_err_t humid_ctrl_publish_status(void)
{
	humid_ctrl_status_t state;
	humid_ctrl_get_status(&state);
	char reply[16];
	format_echo(&state, reply, sizeof(reply));
	if (!legacy_send_state_to_root("humid", reply)) return ESP_FAIL;
	esp_err_t led_err = water_led_echo_all();
	ESP_LOGI(TAG, "status -> %s", reply);
	return led_err;
}

esp_err_t humid_ctrl_init(void)
{
	if (!s_lock) {
		s_lock = xSemaphoreCreateMutex();
		if (!s_lock) return ESP_ERR_NO_MEM;
	}

	memset(&s_status, 0, sizeof(s_status));
	s_status.saved_turbo = 1;
	s_status.relay_shadow = 0xff;
	esp_err_t err = relay_block_init();
	s_status.initialized = err == ESP_OK;
	s_status.last_error = err;
	s_status.relay_shadow = relay_block_get_shadow();
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "relay initialization failed: %s", esp_err_to_name(err));
		return err;
	}
	ESP_LOGI(TAG, "controller ready in safe OFF state");
	return ESP_OK;
}

static bool toggle_single(const char *text, esp_err_t *command_error,
			  char *result, size_t result_size)
{
	const char *prefix = NULL;
	const char *event_key = NULL;
	uint8_t relay_kind = 0;
	if (strcmp(text, "pomp") == 0) {
		prefix = "13";
		event_key = "pump";
		relay_kind = 1;
	} else if (strcmp(text, "flow") == 0) {
		prefix = "16";
		event_key = "flow";
		relay_kind = 2;
	} else if (strcmp(text, "ion") == 0) {
		prefix = "17";
		event_key = "ion";
		relay_kind = 3;
	} else {
		return false;
	}

	if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
		if (command_error) *command_error = ESP_ERR_TIMEOUT;
		copy_result(result, result_size, "lock_timeout");
		return true;
	}
	humid_ctrl_status_t next = s_status;
	if (relay_kind == 1) next.pump_on = !next.pump_on;
	if (relay_kind == 2) next.flow_on = !next.flow_on;
	if (relay_kind == 3) next.ion_on = !next.ion_on;
	esp_err_t err = commit_state_locked(&next);
	xSemaphoreGive(s_lock);
	if (command_error) *command_error = err;
	if (err != ESP_OK) {
		copy_result(result, result_size, esp_err_to_name(err));
		return true;
	}

	bool on = strcmp(prefix, "13") == 0 ? next.pump_on
		  : strcmp(prefix, "16") == 0 ? next.flow_on : next.ion_on;
	char reply[8];
	snprintf(reply, sizeof(reply), "%s%u", prefix, on ? 1U : 0U);
	(void)legacy_send_state_to_root(event_key, reply);
	copy_result(result, result_size, reply);
	return true;
}

static bool set_turbo_command(const char *text, esp_err_t *command_error,
			      char *result, size_t result_size)
{
	if (strlen(text) != 3 || text[0] != '1' || text[1] != '4' ||
	    text[2] < '0' || text[2] > '3') {
		return false;
	}
	if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
		if (command_error) *command_error = ESP_ERR_TIMEOUT;
		copy_result(result, result_size, "lock_timeout");
		return true;
	}
	humid_ctrl_status_t next = s_status;
	if (!next.power_on) {
		xSemaphoreGive(s_lock);
		if (command_error) *command_error = ESP_ERR_INVALID_STATE;
		copy_result(result, result_size, "power_off");
		return true;
	}
	next.turbo = (uint8_t)(text[2] - '0');
	esp_err_t err = commit_state_locked(&next);
	xSemaphoreGive(s_lock);
	if (command_error) *command_error = err;
	if (err != ESP_OK) {
		copy_result(result, result_size, esp_err_to_name(err));
		return true;
	}
	char reply[8];
	snprintf(reply, sizeof(reply), "14%u", (unsigned)next.turbo);
	(void)legacy_send_state_to_root("turbo", reply);
	copy_result(result, result_size, reply);
	return true;
}

static bool power_command(const char *text, esp_err_t *command_error,
			  char *result, size_t result_size)
{
	if (strcmp(text, "huOn") != 0) return false;
	if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
		if (command_error) *command_error = ESP_ERR_TIMEOUT;
		copy_result(result, result_size, "lock_timeout");
		return true;
	}
	humid_ctrl_status_t next = s_status;
	if (next.power_on) {
		next.saved_turbo = next.turbo;
		next.power_on = false;
		next.pump_on = false;
		next.flow_on = false;
		next.ion_on = false;
		next.turbo = 0;
	} else {
		next.power_on = true;
		next.pump_on = true;
		next.flow_on = true;
		next.ion_on = true;
		next.turbo = next.saved_turbo;
	}

	esp_err_t err = commit_state_locked(&next);
	xSemaphoreGive(s_lock);
	if (command_error) *command_error = err;
	if (err != ESP_OK) {
		copy_result(result, result_size, esp_err_to_name(err));
		return true;
	}
	char reply[16];
	format_echo(&next, reply, sizeof(reply));
	(void)legacy_send_state_to_root("humid", reply);
	(void)water_led_echo_all();
	copy_result(result, result_size, reply);
	return true;
}

bool humid_ctrl_handle_command(const char *text, esp_err_t *command_error,
			       char *result, size_t result_size)
{
	if (command_error) *command_error = ESP_OK;
	if (!text || !text[0]) return false;

	if (strcmp(text, "echo_turb") == 0) {
		humid_ctrl_status_t state;
		humid_ctrl_get_status(&state);
		char reply[16];
		format_echo(&state, reply, sizeof(reply));
		(void)legacy_send_state_to_root("humid", reply);
		esp_err_t err = water_led_echo_all();
		if (command_error) *command_error = err;
		copy_result(result, result_size, reply);
		return true;
	}
	if (toggle_single(text, command_error, result, result_size)) return true;
	if (set_turbo_command(text, command_error, result, result_size)) return true;
	if (power_command(text, command_error, result, result_size)) return true;
	return false;
}
