#include "water_led.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_err.h"

#include "led_strip.h"
#include "led_strip_rmt.h"
#include "led_strip_types.h"

#include "legacy_root_sender.h"

static const char *TAG = "water_led";

#define WATER_LED_GPIO	25
#define WATER_LED_NUM	1

// --- ВАЖЛИВО ---
// Якщо зараз “рандомить”, дуже часто причина: НЕ та модель таймінгів.
// 1) Спочатку лишай WS2811 (як у Arduino).
// 2) Якщо лишиться рандом — поміняй на WS2812 і протестуй.
#define WATER_LED_MODEL	LED_MODEL_WS2812

// У тебе було FastLED ... BRG. Якщо кольори будуть “переплутані”,
// міняй на LED_STRIP_COLOR_COMPONENT_FMT_GRB.
#define LED_STRIP_COLOR_COMPONENT_FMT_BRG (led_color_component_format_t){.format = {.r_pos = 1, .g_pos = 2, .b_pos = 0, .w_pos = 3, .reserved = 0, .num_components = 3}}
#define WATER_LED_FMT	LED_STRIP_COLOR_COMPONENT_FMT_BRG

typedef enum {
	MODE_OFF = 0,
	MODE_RED = 1,
	MODE_GREEN = 2,
	MODE_WHITE = 3
} water_mode_t;

static led_strip_handle_t s_strip = NULL;
static water_mode_t s_mode = MODE_OFF;
static uint8_t s_brightness = 51;

static void trim_inplace(char *s)
{
	if (!s) return;

	// left trim
	char *p = s;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
	if (p != s) memmove(s, p, strlen(p) + 1);

	// right trim
	size_t n = strlen(s);
	while (n > 0) {
		char c = s[n - 1];
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
			s[n - 1] = '\0';
			n--;
		} else {
			break;
		}
	}
}

static uint8_t scale_bri(uint8_t v)
{
	// з округленням, щоб на малих рівнях не було “дивних стрибків”
	uint16_t x = (uint16_t)v * (uint16_t)s_brightness + 127;
	return (uint8_t)(x / 255);
}

static void apply_mode(void)
{
	uint8_t r = 0, g = 0, b = 0;

	switch (s_mode) {
		case MODE_OFF:
			r = g = b = 0;
			break;
		case MODE_RED:
			r = scale_bri(255);
			g = 0;
			b = 0;
			break;
		case MODE_GREEN:
			r = 0;
			g = scale_bri(255);
			b = 0;
			break;
		case MODE_WHITE:
			r = scale_bri(255);
			g = scale_bri(255);
			b = scale_bri(255);
			break;
	}

	ESP_ERROR_CHECK(led_strip_set_pixel(s_strip, 0, r, g, b));
	ESP_ERROR_CHECK(led_strip_refresh(s_strip));

	ESP_LOGI(TAG, "apply mode=%d bri=%u -> rgb(%u,%u,%u)", (int)s_mode, (unsigned)s_brightness, r, g, b);
}

static void send_ledfeedback(void)
{
	switch (s_mode) {
		case MODE_OFF:	legacy_send_to_root("210"); break;
		case MODE_RED:	legacy_send_to_root("211"); break;
		case MODE_GREEN:	legacy_send_to_root("212"); break;
		case MODE_WHITE:	legacy_send_to_root("213"); break;
	}
}

static void send_bri_echo(void)
{
	char msg[16];
	snprintf(msg, sizeof(msg), "20%u", (unsigned)s_brightness);
	legacy_send_to_root(msg);
}

void water_led_echo_all(void)
{
	send_bri_echo();
	send_ledfeedback();
}

static void set_mode(water_mode_t m)
{
	s_mode = m;
	apply_mode();
	send_ledfeedback();
}

static void set_brightness(uint8_t b)
{
	s_brightness = b;
	apply_mode();
	send_bri_echo();
}

void water_led_init(void)
{
	led_strip_config_t strip_config = {
		.strip_gpio_num = WATER_LED_GPIO,
		.max_leds = WATER_LED_NUM,
		.led_model = WATER_LED_MODEL,
		.color_component_format = WATER_LED_FMT,
		.flags.invert_out = 0,
	};

	led_strip_rmt_config_t rmt_config = {
		.clk_src = RMT_CLK_SRC_DEFAULT,
		.resolution_hz = 0,		// хай компонент сам вибере дефолт (10MHz)
		.mem_block_symbols = 0,	// дефолт
		.flags.with_dma = 0,
	};

	ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip));

	s_mode = MODE_OFF;
	s_brightness = 51;

	ESP_ERROR_CHECK(led_strip_clear(s_strip));
	ESP_ERROR_CHECK(led_strip_refresh(s_strip));

	apply_mode();
	water_led_echo_all();

	ESP_LOGI(TAG, "init OK GPIO=%d model=%d", WATER_LED_GPIO, (int)WATER_LED_MODEL);
}

void water_led_handle_cmd(const char *txt)
{
	if (!txt) return;

	char s[32];
	snprintf(s, sizeof(s), "%s", txt);
	trim_inplace(s);

	ESP_LOGI(TAG, "cmd=\"%s\"", s);

	// 18x mode
	if (s[0] == '1' && s[1] == '8' && s[2] != '\0' && s[3] == '\0') {
		switch (s[2]) {
			case '0': set_mode(MODE_OFF); return;
			case '1': set_mode(MODE_RED); return;
			case '2': set_mode(MODE_GREEN); return;
			case '3': set_mode(MODE_WHITE); return;
			default: return;
		}
	}

	// 19x brightness
	if (s[0] == '1' && s[1] == '9' && s[2] != '\0' && s[3] == '\0') {
		static const uint8_t map[10] = { 0, 26, 51, 77, 102, 128, 153, 179, 204, 230 };

		if (s[2] >= '0' && s[2] <= '9') {
			set_brightness(map[s[2] - '0']);
			return;
		}
		if (s[2] == 'M') {
			set_brightness(255);
			return;
		}
	}
}
