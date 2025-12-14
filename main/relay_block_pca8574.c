#include "relay_block_pca8574.h"

#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2c_master.h"

static const char *TAG = "relay_block";

#define I2C_PORT		I2C_NUM_0
#define I2C_SDA_GPIO	GPIO_NUM_21
#define I2C_SCL_GPIO	GPIO_NUM_22
#define I2C_HZ			100000

#define RELAY_ADDR		0x38

static i2c_master_bus_handle_t	s_bus = NULL;
static i2c_master_dev_handle_t	s_dev = NULL;

static uint8_t s_shadow = 0xFF;	// всі OFF (active-low)

uint8_t relay_block_get_shadow(void)
{
	return s_shadow;
}

esp_err_t relay_block_write(uint8_t v)
{
	uint8_t b = v;
	esp_err_t err = i2c_master_transmit(s_dev, &b, 1, -1);
	if (err == ESP_OK) {
		s_shadow = v;
	} else {
		ESP_LOGE(TAG, "i2c write failed: 0x%x (%s)", err, esp_err_to_name(err));
	}
	return err;
}

esp_err_t relay_block_init(void)
{
	i2c_master_bus_config_t bus_cfg = {
		.i2c_port = I2C_PORT,
		.sda_io_num = I2C_SDA_GPIO,
		.scl_io_num = I2C_SCL_GPIO,
		.clk_source = I2C_CLK_SRC_DEFAULT,
		.glitch_ignore_cnt = 7,
		.flags.enable_internal_pullup = true,
	};

	esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
	if (err != ESP_OK) return err;

	i2c_device_config_t dev_cfg = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = RELAY_ADDR,
		.scl_speed_hz = I2C_HZ,
	};

	err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
	if (err != ESP_OK) return err;

	ESP_LOGI(TAG, "PCA8574 init OK @0x%02X (SDA=%d SCL=%d)", RELAY_ADDR, (int)I2C_SDA_GPIO, (int)I2C_SCL_GPIO);

	return relay_block_write(0xFF);	// всі OFF
}

esp_err_t relay_block_set_on(uint8_t ch)
{
	if (ch > 7) return ESP_ERR_INVALID_ARG;
	uint8_t v = (uint8_t)(s_shadow & (uint8_t)~(1U << ch));
	return relay_block_write(v);
}

esp_err_t relay_block_set_off(uint8_t ch)
{
	if (ch > 7) return ESP_ERR_INVALID_ARG;
	uint8_t v = (uint8_t)(s_shadow | (uint8_t)(1U << ch));
	return relay_block_write(v);
}
