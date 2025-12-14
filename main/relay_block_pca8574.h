#pragma once
#include "esp_err.h"
#include <stdint.h>

esp_err_t	relay_block_init(void);
esp_err_t	relay_block_write(uint8_t v);

esp_err_t	relay_block_set_on(uint8_t ch);	// ON = clear bit (active-low)
esp_err_t	relay_block_set_off(uint8_t ch);	// OFF = set bit

uint8_t	relay_block_get_shadow(void);
