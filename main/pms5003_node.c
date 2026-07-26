// SPDX-License-Identifier: Apache-2.0
#include "pms5003_node.h"

#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "humidifier_board.h"
#include "legacy_root_sender.h"

static const char *TAG = "pms5003";
static TaskHandle_t s_task;
static SemaphoreHandle_t s_lock;
static pms5003_status_t s_status;
static int s_task_priority = 5;

#define PMS_FRAME_TIMEOUT_MS 2000U
#define PMS_TOTAL_DEADLINE_MS 12000U

static void status_set_error(esp_err_t err)
{
	if (!s_lock) return;
	(void)xSemaphoreTake(s_lock, portMAX_DELAY);
	s_status.last_error = err;
	xSemaphoreGive(s_lock);
}

void pms5003_get_status(pms5003_status_t *status)
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

static esp_err_t pms_uart_init(void)
{
	uart_config_t config = {
		.baud_rate = HUMIDIFIER_PMS_BAUDRATE,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_DEFAULT,
	};
	esp_err_t err = uart_driver_install(HUMIDIFIER_PMS_UART, 4096, 0, 0, NULL, 0);
	if (err != ESP_OK) return err;
	err = uart_param_config(HUMIDIFIER_PMS_UART, &config);
	if (err == ESP_OK) {
		err = uart_set_pin(HUMIDIFIER_PMS_UART, HUMIDIFIER_PMS_TX_GPIO,
				   HUMIDIFIER_PMS_RX_GPIO, UART_PIN_NO_CHANGE,
				   UART_PIN_NO_CHANGE);
	}
	if (err == ESP_OK) err = uart_flush_input(HUMIDIFIER_PMS_UART);
	if (err != ESP_OK) {
		(void)uart_driver_delete(HUMIDIFIER_PMS_UART);
		return err;
	}
	ESP_LOGI(TAG, "UART ready TX=%d RX=%d", (int)HUMIDIFIER_PMS_TX_GPIO,
		 (int)HUMIDIFIER_PMS_RX_GPIO);
	return ESP_OK;
}

static esp_err_t pms_send_command(uint8_t command, uint16_t value)
{
	uint8_t frame[7] = {
		0x42, 0x4d, command, (uint8_t)(value >> 8), (uint8_t)value, 0, 0,
	};
	uint16_t sum = 0;
	for (size_t i = 0; i < 5; ++i) sum += frame[i];
	frame[5] = (uint8_t)(sum >> 8);
	frame[6] = (uint8_t)sum;
	int written = uart_write_bytes(HUMIDIFIER_PMS_UART, frame, sizeof(frame));
	if (written != sizeof(frame)) return ESP_FAIL;
	return uart_wait_tx_done(HUMIDIFIER_PMS_UART, pdMS_TO_TICKS(200));
}

static esp_err_t pms_read_frame(uint8_t *output, size_t output_size,
				uint16_t *length_field, TickType_t timeout)
{
	TickType_t started = xTaskGetTickCount();
	uint8_t byte;
	while ((xTaskGetTickCount() - started) < timeout) {
		if (uart_read_bytes(HUMIDIFIER_PMS_UART, &byte, 1,
				    pdMS_TO_TICKS(50)) != 1 || byte != 0x42) {
			continue;
		}
		if (uart_read_bytes(HUMIDIFIER_PMS_UART, &byte, 1,
				    pdMS_TO_TICKS(200)) != 1 || byte != 0x4d) {
			continue;
		}
		output[0] = 0x42;
		output[1] = 0x4d;
		if (uart_read_bytes(HUMIDIFIER_PMS_UART, output + 2, 2,
				    pdMS_TO_TICKS(200)) != 2) {
			return ESP_ERR_TIMEOUT;
		}
		uint16_t length = ((uint16_t)output[2] << 8) | output[3];
		*length_field = length;
		if (length < 2 || length > 60 || 4U + length > output_size) {
			return ESP_ERR_INVALID_SIZE;
		}
		size_t received = 0;
		while (received < length && (xTaskGetTickCount() - started) < timeout) {
			int count = uart_read_bytes(HUMIDIFIER_PMS_UART,
						    output + 4 + received,
						    length - received,
						    pdMS_TO_TICKS(100));
			if (count > 0) received += (size_t)count;
		}
		if (received != length) return ESP_ERR_TIMEOUT;

		size_t total = 4U + length;
		uint16_t sum = 0;
		for (size_t i = 0; i < total - 2; ++i) sum += output[i];
		uint16_t expected =
			((uint16_t)output[total - 2] << 8) | output[total - 1];
		return sum == expected ? ESP_OK : ESP_ERR_INVALID_CRC;
	}
	return ESP_ERR_TIMEOUT;
}

static void publish_values(uint16_t pm1, uint16_t pm25, uint16_t pm10)
{
	char text[32];
	snprintf(text, sizeof(text), "10%u", (unsigned)pm1);
	(void)legacy_send_to_root(text);
	snprintf(text, sizeof(text), "11%u", (unsigned)pm25);
	(void)legacy_send_to_root(text);
	snprintf(text, sizeof(text), "12%u", (unsigned)pm10);
	(void)legacy_send_to_root(text);
}

static void finish_measurement(esp_err_t err, bool values_valid, uint16_t pm1,
			       uint16_t pm25, uint16_t pm10)
{
	if (!s_lock) return;
	(void)xSemaphoreTake(s_lock, portMAX_DELAY);
	s_status.busy = false;
	s_status.last_error = err;
	if (values_valid) {
		s_status.pm1 = pm1;
		s_status.pm25 = pm25;
		s_status.pm10 = pm10;
		s_status.completed_count++;
	}
	xSemaphoreGive(s_lock);
}

static void pms_task(void *argument)
{
	(void)argument;
	uint8_t buffer[64];
	esp_err_t startup_err = pms_send_command(0xe1, 0x0000);
	if (startup_err == ESP_OK) startup_err = pms_send_command(0xe4, 0x0000);
	if (startup_err != ESP_OK) {
		status_set_error(startup_err);
		ESP_LOGW(TAG, "initial sleep command failed: %s",
			 esp_err_to_name(startup_err));
	}

	for (;;) {
		(void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		(void)legacy_send_to_root("pm155555555555555");
		esp_err_t err = pms_send_command(0xe4, 0x0001);
		if (err == ESP_OK) {
			vTaskDelay(pdMS_TO_TICKS(HUMIDIFIER_PMS_WAKE_WARMUP_MS));
			err = pms_send_command(0xe1, 0x0001);
		}
		if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(1200));

		uint16_t length = 0;
		TickType_t started = xTaskGetTickCount();
		while (err == ESP_OK &&
		       (xTaskGetTickCount() - started) <
			       pdMS_TO_TICKS(PMS_TOTAL_DEADLINE_MS)) {
			err = pms_read_frame(buffer, sizeof(buffer), &length,
					     pdMS_TO_TICKS(PMS_FRAME_TIMEOUT_MS));
			if (err == ESP_OK && length == 28) break;
			if (err == ESP_OK) continue;
			if (err != ESP_ERR_TIMEOUT && err != ESP_ERR_INVALID_CRC) break;
			err = ESP_OK;
		}
		if (err == ESP_OK && length != 28) err = ESP_ERR_TIMEOUT;

		uint16_t pm1 = 0;
		uint16_t pm25 = 0;
		uint16_t pm10 = 0;
		bool values_valid = err == ESP_OK;
		if (values_valid) {
			pm1 = ((uint16_t)buffer[10] << 8) | buffer[11];
			pm25 = ((uint16_t)buffer[12] << 8) | buffer[13];
			pm10 = ((uint16_t)buffer[14] << 8) | buffer[15];
			publish_values(pm1, pm25, pm10);
			ESP_LOGI(TAG, "PM1=%u PM2.5=%u PM10=%u",
				 (unsigned)pm1, (unsigned)pm25, (unsigned)pm10);
		} else {
			char text[32];
			snprintf(text, sizeof(text), "pm1_e%X", (unsigned)err);
			(void)legacy_send_to_root(text);
			(void)legacy_send_to_root("pm1_fail");
			ESP_LOGW(TAG, "measurement failed: %s", esp_err_to_name(err));
		}

		esp_err_t cleanup_err = pms_send_command(0xe1, 0x0000);
		if (cleanup_err == ESP_OK) cleanup_err = pms_send_command(0xe4, 0x0000);
		if (cleanup_err == ESP_OK) cleanup_err = uart_flush_input(HUMIDIFIER_PMS_UART);
		if (cleanup_err != ESP_OK) {
			ESP_LOGW(TAG, "sleep cleanup failed: %s",
				 esp_err_to_name(cleanup_err));
		}
		esp_err_t final_err = err != ESP_OK ? err : cleanup_err;
		finish_measurement(final_err, values_valid, pm1, pm25, pm10);
	}
}

esp_err_t pms5003_start(int task_priority)
{
	if (!s_lock) {
		s_lock = xSemaphoreCreateMutex();
		if (!s_lock) return ESP_ERR_NO_MEM;
	}
	if (s_task) return ESP_OK;
	if (task_priority > 0) s_task_priority = task_priority;

	esp_err_t err = pms_uart_init();
	if (err != ESP_OK) {
		status_set_error(err);
		return err;
	}
	if (xTaskCreate(pms_task, "pms5003", 5120, NULL, s_task_priority, &s_task) !=
	    pdPASS) {
		s_task = NULL;
		(void)uart_driver_delete(HUMIDIFIER_PMS_UART);
		(void)xSemaphoreTake(s_lock, portMAX_DELAY);
		s_status.ready = false;
		s_status.busy = false;
		s_status.last_error = ESP_ERR_NO_MEM;
		xSemaphoreGive(s_lock);
		return ESP_ERR_NO_MEM;
	}
	if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
		s_status.ready = true;
		s_status.last_error = ESP_OK;
		xSemaphoreGive(s_lock);
	}
	return ESP_OK;
}

esp_err_t pms5003_trigger_once(void)
{
	if (!s_task) {
		esp_err_t err = pms5003_start(s_task_priority);
		if (err != ESP_OK) return err;
	}
	if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
		return ESP_ERR_TIMEOUT;
	}
	if (s_status.busy) {
		xSemaphoreGive(s_lock);
		return ESP_ERR_INVALID_STATE;
	}
	s_status.busy = true;
	s_status.last_error = ESP_OK;
	xSemaphoreGive(s_lock);
	xTaskNotifyGive(s_task);
	return ESP_OK;
}
