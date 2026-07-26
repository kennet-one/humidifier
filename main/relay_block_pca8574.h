// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t relay_block_init(void);
esp_err_t relay_block_write(uint8_t value);
esp_err_t relay_block_set_on(uint8_t channel);
esp_err_t relay_block_set_off(uint8_t channel);
uint8_t relay_block_get_shadow(void);
bool relay_block_ready(void);
