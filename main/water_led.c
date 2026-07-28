// SPDX-License-Identifier: Apache-2.0
#include "water_led.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "led_strip_types.h"

#include "humidifier_board.h"
#include "legacy_root_sender.h"

static const char *TAG = "water_led";
static led_strip_handle_t s_strip;
static SemaphoreHandle_t s_lock;
static water_led_status_t s_status = {
	.mode = 0,
	.brightness = 51,
};

#define WATER_LED_COUNT 1
#define WATER_LED_MODEL LED_MODEL_WS2812
#define WATER_LED_FORMAT \
	((led_color_component_format_t){ \
		.format = {.r_pos = 1, .g_pos = 2, .b_pos = 0, .w_pos = 3, \
			   .reserved = 0, .num_components = 3}})

static void copy_result(char *result, size_t result_size, const char *text)
{
	if (!result || result_size == 0) return;
	snprintf(result, result_size, "%s", text ? text : "");
	result[result_size - 1] = '\0';
}

static uint8_t scale(uint8_t value, uint8_t brightness)
{
	return (uint8_t)(((uint16_t)value * brightness + 127U) / 255U);
}

static esp_err_t apply(uint8_t mode, uint8_t brightness)
{
	if (!s_strip) return ESP_ERR_INVALID_STATE;
	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;
	switch (mode) {
	case 0:
		break;
	case 1:
		r = scale(255, brightness);
		break;
	case 2:
		g = scale(255, brightness);
		break;
	case 3:
		r = g = b = scale(255, brightness);
		break;
	default:
		return ESP_ERR_INVALID_ARG;
	}
	esp_err_t err = led_strip_set_pixel(s_strip, 0, r, g, b);
	if (err == ESP_OK) err = led_strip_refresh(s_strip);
	return err;
}

void water_led_get_status(water_led_status_t *status)
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

esp_err_t water_led_init(void)
{
	if (!s_lock) {
		s_lock = xSemaphoreCreateMutex();
		if (!s_lock) return ESP_ERR_NO_MEM;
	}
	if (!s_strip) {
		led_strip_config_t strip_config = {
			.strip_gpio_num = HUMIDIFIER_WATER_LED_GPIO,
			.max_leds = WATER_LED_COUNT,
			.led_model = WATER_LED_MODEL,
			.color_component_format = WATER_LED_FORMAT,
		};
		led_strip_rmt_config_t rmt_config = {
			.clk_src = RMT_CLK_SRC_DEFAULT,
		.resolution_hz = 0,
			.mem_block_symbols = 0,
			.flags.with_dma = 0,
		};
		esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config,
							&s_strip);
		if (err != ESP_OK) {
			s_status.last_error = err;
			return err;
		}
	}

	esp_err_t err = led_strip_clear(s_strip);
	if (err == ESP_OK) err = led_strip_refresh(s_strip);
	if (err == ESP_OK) err = apply(0, 51);
	s_status.initialized = err == ESP_OK;
	s_status.mode = 0;
	s_status.brightness = 51;
	s_status.last_error = err;
	if (err == ESP_OK) {
		ESP_LOGI(TAG, "ready on GPIO%d in safe OFF state",
			 (int)HUMIDIFIER_WATER_LED_GPIO);
	} else {
		ESP_LOGE(TAG, "initialization failed: %s", esp_err_to_name(err));
	}
	return err;
}

static esp_err_t set_mode(uint8_t mode)
{
	if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
		return ESP_ERR_TIMEOUT;
	}
	uint8_t brightness = s_status.brightness;
	esp_err_t err = apply(mode, brightness);
	if (err == ESP_OK) {
		s_status.mode = mode;
		s_status.brightness = brightness;
		s_status.initialized = true;
	}
	s_status.last_error = err;
	xSemaphoreGive(s_lock);
	return err;
}

static esp_err_t set_brightness(uint8_t brightness)
{
	if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
		return ESP_ERR_TIMEOUT;
	}
	uint8_t mode = s_status.mode;
	esp_err_t err = apply(mode, brightness);
	if (err == ESP_OK) {
		s_status.mode = mode;
		s_status.brightness = brightness;
		s_status.initialized = true;
	}
	s_status.last_error = err;
	xSemaphoreGive(s_lock);
	return err;
}

static void mode_reply(uint8_t mode, char reply[8])
{
	snprintf(reply, 8, "21%u", (unsigned)mode);
}

static void brightness_reply(uint8_t brightness, char reply[16])
{
	snprintf(reply, 16, "20%u", (unsigned)brightness);
}

esp_err_t water_led_echo_all(void)
{
	water_led_status_t status;
	water_led_get_status(&status);
	char reply[16];
	brightness_reply(status.brightness, reply);
	(void)legacy_send_state_to_root("led_level", reply);
	mode_reply(status.mode, reply);
	(void)legacy_send_state_to_root("led_mode", reply);
	return status.last_error;
}

bool water_led_handle_command(const char *text, esp_err_t *command_error,
			      char *result, size_t result_size)
{
	if (command_error) *command_error = ESP_OK;
	if (!text) return false;
	size_t length = strlen(text);
	if (length == 3 && text[0] == '1' && text[1] == '8' &&
	    text[2] >= '0' && text[2] <= '3') {
		uint8_t mode = (uint8_t)(text[2] - '0');
		esp_err_t err = set_mode(mode);
		if (command_error) *command_error = err;
		char reply[8];
		if (err == ESP_OK) {
			mode_reply(mode, reply);
			(void)legacy_send_state_to_root("led_mode", reply);
			copy_result(result, result_size, reply);
		} else {
			copy_result(result, result_size, esp_err_to_name(err));
		}
		return true;
	}

	if (length == 3 && text[0] == '1' && text[1] == '9') {
		static const uint8_t levels[10] =
			{0, 26, 51, 77, 102, 128, 153, 179, 204, 230};
		uint8_t brightness;
		if (text[2] >= '0' && text[2] <= '9') {
			brightness = levels[text[2] - '0'];
		} else if (text[2] == 'M') {
			brightness = 255;
		} else {
			return false;
		}
		esp_err_t err = set_brightness(brightness);
		if (command_error) *command_error = err;
		char reply[16];
		if (err == ESP_OK) {
			brightness_reply(brightness, reply);
			(void)legacy_send_state_to_root("led_level", reply);
			copy_result(result, result_size, reply);
		} else {
			copy_result(result, result_size, esp_err_to_name(err));
		}
		return true;
	}
	return false;
}
