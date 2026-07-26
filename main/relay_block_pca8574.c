// SPDX-License-Identifier: Apache-2.0
#include "relay_block_pca8574.h"

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "humidifier_board.h"

static const char *TAG = "relay_block";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static SemaphoreHandle_t s_lock;
static uint8_t s_shadow = 0xff;

#define RELAY_I2C_TIMEOUT_MS 100

static esp_err_t write_locked(uint8_t value)
{
	if (!s_dev) return ESP_ERR_INVALID_STATE;
	esp_err_t err = i2c_master_transmit(s_dev, &value, 1, RELAY_I2C_TIMEOUT_MS);
	if (err == ESP_OK) {
		s_shadow = value;
	} else {
		ESP_LOGE(TAG, "I2C write failed: %s", esp_err_to_name(err));
	}
	return err;
}

bool relay_block_ready(void)
{
	return s_dev != NULL;
}

uint8_t relay_block_get_shadow(void)
{
	if (!s_lock) return 0xff;
	(void)xSemaphoreTake(s_lock, portMAX_DELAY);
	uint8_t value = s_shadow;
	xSemaphoreGive(s_lock);
	return value;
}

esp_err_t relay_block_write(uint8_t value)
{
	if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
		return ESP_ERR_TIMEOUT;
	}
	esp_err_t err = write_locked(value);
	xSemaphoreGive(s_lock);
	return err;
}

esp_err_t relay_block_init(void)
{
	if (s_dev) return relay_block_write(0xff);
	if (!s_lock) {
		s_lock = xSemaphoreCreateMutex();
		if (!s_lock) return ESP_ERR_NO_MEM;
	}

	i2c_master_bus_config_t bus_config = {
		.i2c_port = HUMIDIFIER_I2C_PORT,
		.sda_io_num = HUMIDIFIER_I2C_SDA_GPIO,
		.scl_io_num = HUMIDIFIER_I2C_SCL_GPIO,
		.clk_source = I2C_CLK_SRC_DEFAULT,
		.glitch_ignore_cnt = 7,
		.flags.enable_internal_pullup = true,
	};
	esp_err_t err = i2c_new_master_bus(&bus_config, &s_bus);
	if (err != ESP_OK) return err;

	i2c_device_config_t device_config = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = HUMIDIFIER_RELAY_ADDRESS,
		.scl_speed_hz = HUMIDIFIER_I2C_FREQ_HZ,
	};
	err = i2c_master_bus_add_device(s_bus, &device_config, &s_dev);
	if (err != ESP_OK) {
		(void)i2c_del_master_bus(s_bus);
		s_bus = NULL;
		return err;
	}

	err = relay_block_write(0xff);
	if (err != ESP_OK) {
		(void)i2c_master_bus_rm_device(s_dev);
		(void)i2c_del_master_bus(s_bus);
		s_dev = NULL;
		s_bus = NULL;
		return err;
	}
	ESP_LOGI(TAG, "PCA8574 ready at 0x%02x, all outputs OFF",
		 HUMIDIFIER_RELAY_ADDRESS);
	return ESP_OK;
}

static esp_err_t set_channel(uint8_t channel, bool on)
{
	if (channel > 7) return ESP_ERR_INVALID_ARG;
	if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
		return ESP_ERR_TIMEOUT;
	}
	uint8_t value = on ? (uint8_t)(s_shadow & (uint8_t)~(1U << channel))
			   : (uint8_t)(s_shadow | (uint8_t)(1U << channel));
	esp_err_t err = write_locked(value);
	xSemaphoreGive(s_lock);
	return err;
}

esp_err_t relay_block_set_on(uint8_t channel)
{
	return set_channel(channel, true);
}

esp_err_t relay_block_set_off(uint8_t channel)
{
	return set_channel(channel, false);
}
