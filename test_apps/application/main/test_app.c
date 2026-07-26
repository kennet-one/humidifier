// SPDX-License-Identifier: Apache-2.0
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"

#include "humid_ctrl.h"
#include "legacy_proto.h"
#include "pms5003_node.h"
#include "relay_block_pca8574.h"
#include "water_led.h"

static uint8_t s_relay_shadow;
static esp_err_t s_relay_write_error;
static esp_err_t s_pms_trigger_error;
static char s_events[16][32];
static size_t s_event_count;

static void reset_mocks(void)
{
	s_relay_shadow = 0xff;
	s_relay_write_error = ESP_OK;
	s_pms_trigger_error = ESP_OK;
	s_event_count = 0;
	memset(s_events, 0, sizeof(s_events));
}

bool legacy_send_to_root(const char *text)
{
	if (!text || s_event_count >= 16) return false;
	snprintf(s_events[s_event_count], sizeof(s_events[s_event_count]), "%s", text);
	s_event_count++;
	return true;
}

esp_err_t relay_block_init(void)
{
	s_relay_shadow = 0xff;
	return ESP_OK;
}

bool relay_block_ready(void)
{
	return true;
}

uint8_t relay_block_get_shadow(void)
{
	return s_relay_shadow;
}

esp_err_t relay_block_write(uint8_t value)
{
	if (s_relay_write_error != ESP_OK) return s_relay_write_error;
	s_relay_shadow = value;
	return ESP_OK;
}

esp_err_t relay_block_set_on(uint8_t channel)
{
	if (channel > 7) return ESP_ERR_INVALID_ARG;
	return relay_block_write((uint8_t)(s_relay_shadow & ~(1U << channel)));
}

esp_err_t relay_block_set_off(uint8_t channel)
{
	if (channel > 7) return ESP_ERR_INVALID_ARG;
	return relay_block_write((uint8_t)(s_relay_shadow | (1U << channel)));
}

esp_err_t water_led_init(void)
{
	return ESP_OK;
}

bool water_led_handle_command(const char *text, esp_err_t *command_error,
			      char *result, size_t result_size)
{
	if (!text || strcmp(text, "180") != 0) return false;
	if (command_error) *command_error = ESP_OK;
	if (result && result_size) snprintf(result, result_size, "210");
	return true;
}

esp_err_t water_led_echo_all(void)
{
	(void)legacy_send_to_root("2051");
	(void)legacy_send_to_root("210");
	return ESP_OK;
}

void water_led_get_status(water_led_status_t *status)
{
	if (!status) return;
	memset(status, 0, sizeof(*status));
	status->initialized = true;
	status->brightness = 51;
}

esp_err_t pms5003_start(int task_priority)
{
	(void)task_priority;
	return ESP_OK;
}

esp_err_t pms5003_trigger_once(void)
{
	return s_pms_trigger_error;
}

void pms5003_get_status(pms5003_status_t *status)
{
	if (status) memset(status, 0, sizeof(*status));
}

static void init_controller(void)
{
	reset_mocks();
	TEST_ASSERT_EQUAL(ESP_OK, humid_ctrl_init());
}

static void test_safe_initial_state(void)
{
	init_controller();
	humid_ctrl_status_t state;
	humid_ctrl_get_status(&state);
	TEST_ASSERT_TRUE(state.initialized);
	TEST_ASSERT_FALSE(state.power_on);
	TEST_ASSERT_FALSE(state.pump_on);
	TEST_ASSERT_FALSE(state.flow_on);
	TEST_ASSERT_FALSE(state.ion_on);
	TEST_ASSERT_EQUAL_UINT8(0, state.turbo);
	TEST_ASSERT_EQUAL_HEX8(0xff, state.relay_shadow);
	TEST_ASSERT_EQUAL_HEX8(0xff, s_relay_shadow);
}

static void test_relay_state_changes_only_after_hardware_success(void)
{
	init_controller();
	esp_err_t err = ESP_OK;
	char result[32] = {0};
	TEST_ASSERT_TRUE(legacy_handle_command("pomp", &err, result, sizeof(result)));
	TEST_ASSERT_EQUAL(ESP_OK, err);
	TEST_ASSERT_EQUAL_STRING("131", result);

	humid_ctrl_status_t before;
	humid_ctrl_get_status(&before);
	TEST_ASSERT_TRUE(before.pump_on);
	TEST_ASSERT_EQUAL_HEX8(0xfe, before.relay_shadow);

	s_relay_write_error = ESP_FAIL;
	TEST_ASSERT_TRUE(legacy_handle_command("flow", &err, result, sizeof(result)));
	TEST_ASSERT_EQUAL(ESP_FAIL, err);

	humid_ctrl_status_t after;
	humid_ctrl_get_status(&after);
	TEST_ASSERT_FALSE(after.flow_on);
	TEST_ASSERT_EQUAL_HEX8(before.relay_shadow, after.relay_shadow);
	TEST_ASSERT_EQUAL_HEX8(before.relay_shadow, s_relay_shadow);
}

static void test_fan_and_power_transitions_use_complete_shadow(void)
{
	init_controller();
	esp_err_t err = ESP_OK;
	char result[32] = {0};

	TEST_ASSERT_TRUE(legacy_handle_command("141", &err, result, sizeof(result)));
	TEST_ASSERT_EQUAL(ESP_OK, err);
	TEST_ASSERT_BITS_HIGH((uint8_t)(1U << 4), s_relay_shadow);
	TEST_ASSERT_BITS_LOW((uint8_t)(1U << 5), s_relay_shadow);

	TEST_ASSERT_TRUE(legacy_handle_command("142", &err, result, sizeof(result)));
	TEST_ASSERT_EQUAL(ESP_OK, err);
	TEST_ASSERT_BITS_LOW((uint8_t)(1U << 4), s_relay_shadow);
	TEST_ASSERT_BITS_HIGH((uint8_t)(1U << 5), s_relay_shadow);

	TEST_ASSERT_TRUE(legacy_handle_command("huOn", &err, result, sizeof(result)));
	TEST_ASSERT_EQUAL_STRING("151000", result);
	humid_ctrl_status_t on;
	humid_ctrl_get_status(&on);
	TEST_ASSERT_TRUE(on.power_on);
	TEST_ASSERT_TRUE(on.pump_on);
	TEST_ASSERT_TRUE(on.flow_on);
	TEST_ASSERT_TRUE(on.ion_on);
	TEST_ASSERT_EQUAL_UINT8(1, on.turbo);

	TEST_ASSERT_TRUE(legacy_handle_command("huOn", &err, result, sizeof(result)));
	TEST_ASSERT_EQUAL_STRING("150111", result);
	humid_ctrl_status_t off;
	humid_ctrl_get_status(&off);
	TEST_ASSERT_FALSE(off.power_on);
	TEST_ASSERT_EQUAL_HEX8(0xff, off.relay_shadow);
}

static void test_dispatch_rejects_malformed_and_reports_pms_busy(void)
{
	init_controller();
	esp_err_t err = ESP_OK;
	char result[32] = {0};

	TEST_ASSERT_FALSE(legacy_handle_command("", &err, result, sizeof(result)));
	TEST_ASSERT_FALSE(legacy_handle_command("149", &err, result, sizeof(result)));
	TEST_ASSERT_FALSE(legacy_handle_command("18X", &err, result, sizeof(result)));
	TEST_ASSERT_FALSE(legacy_handle_command("19Z", &err, result, sizeof(result)));
	TEST_ASSERT_FALSE(legacy_handle_command("unknown", &err, result, sizeof(result)));

	TEST_ASSERT_TRUE(legacy_handle_command("180", &err, result, sizeof(result)));
	TEST_ASSERT_EQUAL(ESP_OK, err);
	TEST_ASSERT_EQUAL_STRING("210", result);

	s_pms_trigger_error = ESP_OK;
	TEST_ASSERT_TRUE(legacy_handle_command("pm1", &err, result, sizeof(result)));
	TEST_ASSERT_EQUAL(ESP_OK, err);
	TEST_ASSERT_EQUAL_STRING("pm1 accepted", result);

	s_pms_trigger_error = ESP_ERR_INVALID_STATE;
	TEST_ASSERT_TRUE(legacy_handle_command("pm1", &err, result, sizeof(result)));
	TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
	TEST_ASSERT_EQUAL_STRING("pm1 busy", result);
}

void app_main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_safe_initial_state);
	RUN_TEST(test_relay_state_changes_only_after_hardware_success);
	RUN_TEST(test_fan_and_power_transitions_use_complete_shadow);
	RUN_TEST(test_dispatch_rejects_malformed_and_reports_pms_busy);
	int failures = UNITY_END();
	printf("HUMIDIFIER_APPLICATION_TESTS:%s\n", failures == 0 ? "PASS" : "FAIL");
	vTaskDelay(portMAX_DELAY);
}
