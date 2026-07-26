// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
	bool ready;
	bool busy;
	uint16_t pm1;
	uint16_t pm25;
	uint16_t pm10;
	uint32_t completed_count;
	esp_err_t last_error;
} pms5003_status_t;

esp_err_t pms5003_start(int task_priority);
esp_err_t pms5003_trigger_once(void);
void pms5003_get_status(pms5003_status_t *status);
