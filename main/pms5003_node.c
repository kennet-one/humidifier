#include "pms5003_node.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#include "legacy_root_sender.h"

static const char *TAG = "pms5003";

#define PMS_UART				UART_NUM_1
#define PMS_TX_GPIO				GPIO_NUM_17
#define PMS_RX_GPIO				GPIO_NUM_16
#define PMS_BAUDRATE				9600

// Для тесту можеш тимчасово зменшити до 5000, потім повернути 30000
#define PMS_WAKE_WARMUP_MS			30000

#define PMS_FRAME_TIMEOUT_MS			2000
#define PMS_TOTAL_DEADLINE_MS			12000

static TaskHandle_t s_task = NULL;

static void pms_uart_init(void)
{
	uart_config_t cfg = {
		.baud_rate = PMS_BAUDRATE,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_DEFAULT,
	};

	ESP_ERROR_CHECK(uart_driver_install(PMS_UART, 4096, 0, 0, NULL, 0));
	ESP_ERROR_CHECK(uart_param_config(PMS_UART, &cfg));
	ESP_ERROR_CHECK(uart_set_pin(PMS_UART, PMS_TX_GPIO, PMS_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
	ESP_ERROR_CHECK(uart_flush_input(PMS_UART));

	ESP_LOGI(TAG, "UART init OK (TX=%d RX=%d)", (int)PMS_TX_GPIO, (int)PMS_RX_GPIO);
}

static void pms_send_cmd(uint8_t cmd, uint16_t data)
{
	uint8_t f[7];

	f[0] = 0x42;
	f[1] = 0x4D;
	f[2] = cmd;
	f[3] = (uint8_t)((data >> 8) & 0xFF);
	f[4] = (uint8_t)(data & 0xFF);

	uint16_t sum = 0;
	for (int i = 0; i < 5; i++) {
		sum += f[i];
	}

	f[5] = (uint8_t)((sum >> 8) & 0xFF);
	f[6] = (uint8_t)(sum & 0xFF);

	uart_write_bytes(PMS_UART, (const char *)f, sizeof(f));
	uart_wait_tx_done(PMS_UART, pdMS_TO_TICKS(100));

	ESP_LOGI(TAG, "cmd 0x%02X data=0x%04X sum=0x%04X", (unsigned)cmd, (unsigned)data, (unsigned)sum);
}

static esp_err_t pms_read_one_frame(uint8_t *out, size_t out_sz, uint16_t *out_len_field, TickType_t timeout_ticks)
{
	TickType_t start = xTaskGetTickCount();
	uint8_t b = 0;

	while ((xTaskGetTickCount() - start) < timeout_ticks) {
		if (uart_read_bytes(PMS_UART, &b, 1, pdMS_TO_TICKS(50)) != 1) continue;
		if (b != 0x42) continue;

		if (uart_read_bytes(PMS_UART, &b, 1, pdMS_TO_TICKS(200)) != 1) continue;
		if (b != 0x4D) continue;

		out[0] = 0x42;
		out[1] = 0x4D;

		if (uart_read_bytes(PMS_UART, &out[2], 2, pdMS_TO_TICKS(200)) != 2) return ESP_ERR_TIMEOUT;

		uint16_t len = ((uint16_t)out[2] << 8) | out[3];
		*out_len_field = len;

		if (len < 2 || len > 60) return ESP_ERR_INVALID_SIZE;
		if ((4 + len) > out_sz) return ESP_ERR_INVALID_SIZE;

		int need = (int)len;
		int off = 4;

		while (need > 0 && (xTaskGetTickCount() - start) < timeout_ticks) {
			int n = uart_read_bytes(PMS_UART, out + off, need, pdMS_TO_TICKS(100));
			if (n > 0) {
				off += n;
				need -= n;
			}
		}
		if (need != 0) return ESP_ERR_TIMEOUT;

		int total = 4 + len;

		uint16_t sum = 0;
		for (int i = 0; i < total - 2; i++) {
			sum += out[i];
		}
		uint16_t chk = ((uint16_t)out[total - 2] << 8) | out[total - 1];

		if (sum != chk) return ESP_ERR_INVALID_CRC;

		return ESP_OK;
	}

	return ESP_ERR_TIMEOUT;
}

static void pms_publish(uint16_t pm1, uint16_t pm25, uint16_t pm10)
{
	char s[32];

	snprintf(s, sizeof(s), "10%u", (unsigned)pm1);
	legacy_send_to_root(s);

	snprintf(s, sizeof(s), "11%u", (unsigned)pm25);
	legacy_send_to_root(s);

	snprintf(s, sizeof(s), "12%u", (unsigned)pm10);
	legacy_send_to_root(s);

	ESP_LOGI(TAG, "publish PM1=%u PM2.5=%u PM10=%u", (unsigned)pm1, (unsigned)pm25, (unsigned)pm10);
}

static void pms_task(void *arg)
{
	uint8_t buf[64];

	pms_uart_init();

	// На старті: пасив + sleep
	pms_send_cmd(0xE1, 0x0000);
	pms_send_cmd(0xE4, 0x0000);

	while (1) {
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

		legacy_send_to_root("pm155555555555555");

		ESP_LOGI(TAG, "trigger -> wake");
		pms_send_cmd(0xE4, 0x0001); // wake
		vTaskDelay(pdMS_TO_TICKS(PMS_WAKE_WARMUP_MS));

		// Переходимо в ACTIVE щоб сенсор сам почав стрімити data-frame
		pms_send_cmd(0xE1, 0x0001); // ACTIVE
		vTaskDelay(pdMS_TO_TICKS(1200));

		uint16_t len = 0;
		esp_err_t err = ESP_FAIL;

		TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(PMS_TOTAL_DEADLINE_MS);

		while (xTaskGetTickCount() < deadline) {
			err = pms_read_one_frame(buf, sizeof(buf), &len, pdMS_TO_TICKS(PMS_FRAME_TIMEOUT_MS));
			if (err == ESP_OK) {
				if (len == 28) {
					break; // нормальний data-frame (32 bytes total)
				}
				ESP_LOGI(TAG, "skip frame len=%u", (unsigned)len); // ACK кадри теж сюди
			} else {
				ESP_LOGW(TAG, "frame err=0x%x (%s)", err, esp_err_to_name(err));
			}
		}

		size_t blen = 0;
		uart_get_buffered_data_len(PMS_UART, &blen);
		char dbg[32];
		snprintf(dbg, sizeof(dbg), "pm1_buf%u", (unsigned)blen);
		legacy_send_to_root(dbg);

		if (err == ESP_OK && len == 28) {
			// Atmospheric environment: Data4..Data6
			uint16_t pm1_atm  = ((uint16_t)buf[10] << 8) | buf[11];
			uint16_t pm25_atm = ((uint16_t)buf[12] << 8) | buf[13];
			uint16_t pm10_atm = ((uint16_t)buf[14] << 8) | buf[15];

			pms_publish(pm1_atm, pm25_atm, pm10_atm);
		} else {
			char e[32];
			snprintf(e, sizeof(e), "pm1_e%X", (unsigned)err);
			legacy_send_to_root(e);
			legacy_send_to_root("pm1_fail");
		}

		// Назад у пасив і sleep
		pms_send_cmd(0xE1, 0x0000); // PASSIVE
		pms_send_cmd(0xE4, 0x0000); // SLEEP
		ESP_LOGI(TAG, "sleep");
		uart_flush_input(PMS_UART);

	}
}

void pms5003_start(int task_prio)
{
	if (s_task) return;

	xTaskCreate(pms_task, "pms5003", 4096, NULL, task_prio, &s_task);
}

void pms5003_trigger_once(void)
{
	if (s_task) {
		xTaskNotifyGive(s_task);
	}
}
