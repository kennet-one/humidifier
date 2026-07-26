// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
	bool initialized;
	bool power_on;
	bool pump_on;
	bool flow_on;
	bool ion_on;
	uint8_t turbo;
	uint8_t saved_turbo;
	uint8_t relay_shadow;
	esp_err_t last_error;
} humid_ctrl_status_t;

esp_err_t humid_ctrl_init(void);
bool humid_ctrl_handle_command(const char *text, esp_err_t *command_error,
			       char *result, size_t result_size);
void humid_ctrl_get_status(humid_ctrl_status_t *status);
esp_err_t humid_ctrl_publish_status(void);
