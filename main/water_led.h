// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
	bool initialized;
	uint8_t mode;
	uint8_t brightness;
	esp_err_t last_error;
} water_led_status_t;

esp_err_t water_led_init(void);
bool water_led_handle_command(const char *text, esp_err_t *command_error,
			      char *result, size_t result_size);
esp_err_t water_led_echo_all(void);
void water_led_get_status(water_led_status_t *status);
