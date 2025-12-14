#include "humid_ctrl.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"

#include "relay_block_pca8574.h"
#include "legacy_root_sender.h"

static const char *TAG = "humid_ctrl";

// Зберігаємо семантику як в Arduino (true/false там інвертовані під протокол)
static bool pomp_State = true;	// true -> “0”, false -> “1”
static bool flowSpin = true;
static bool ionic = true;

typedef enum {
	TURBOOFF = 0,
	TURBO1 = 1,
	TURBO2 = 2,
	TURBO3 = 3
} turbo_t;

static turbo_t tuurbo = TURBOOFF;
static turbo_t saved_turbo = TURBO1;

static bool tpower = true;		// true = “power off” як у тебе

static void send_eho(void)
{
	char msg[16];

	// "15" + turbo + y + u + i
	// y/u/i: (state==true) ? "1" : "0"  (як у твоєму Arduino eho())
	snprintf(msg, sizeof(msg), "15%d%d%d%d",
		(int)tuurbo,
		pomp_State ? 1 : 0,
		flowSpin ? 1 : 0,
		ionic ? 1 : 0);

	legacy_send_to_root(msg);
	ESP_LOGI(TAG, "EHO -> %s", msg);

	// Phase B: brightness + ledfeedback можна додати сюди пізніше
}

static void apply_fan(void)
{
	switch (tuurbo) {
		case TURBOOFF:
			relay_block_set_off(5);
			relay_block_set_off(4);
			relay_block_set_off(3);
			break;
		case TURBO1:
			relay_block_set_off(4);
			relay_block_set_off(3);
			relay_block_set_on(5);
			break;
		case TURBO2:
			relay_block_set_off(5);
			relay_block_set_off(3);
			relay_block_set_on(4);
			break;
		case TURBO3:
			relay_block_set_off(5);
			relay_block_set_off(4);
			relay_block_set_on(3);
			break;
	}
}

static void pimp(void)
{
	if (pomp_State) {
		relay_block_set_on(0);
	} else {
		relay_block_set_off(0);
	}
	pomp_State = !pomp_State;

	char msg[8];
	snprintf(msg, sizeof(msg), "13%d", pomp_State ? 0 : 1);
	legacy_send_to_root(msg);
	ESP_LOGI(TAG, "pomp -> %s", msg);
}

static void flo(void)
{
	if (flowSpin) {
		relay_block_set_on(1);
	} else {
		relay_block_set_off(1);
	}
	flowSpin = !flowSpin;

	char msg[8];
	snprintf(msg, sizeof(msg), "16%d", flowSpin ? 0 : 1);
	legacy_send_to_root(msg);
	ESP_LOGI(TAG, "flow -> %s", msg);
}

static void ionn(void)
{
	if (ionic) {
		relay_block_set_on(2);
	} else {
		relay_block_set_off(2);
	}
	ionic = !ionic;

	char msg[8];
	snprintf(msg, sizeof(msg), "17%d", ionic ? 0 : 1);
	legacy_send_to_root(msg);
	ESP_LOGI(TAG, "ion -> %s", msg);
}

static void set_turbo(turbo_t t)
{
	tuurbo = t;
	apply_fan();

	char msg[8];
	snprintf(msg, sizeof(msg), "14%d", (int)tuurbo);
	legacy_send_to_root(msg);
	ESP_LOGI(TAG, "turbo -> %s", msg);
}

static void power_toggle(void)
{
	if (tpower == false) {
		// turn OFF
		relay_block_set_off(2);
		ionic = true;

		relay_block_set_off(1);
		flowSpin = true;

		relay_block_set_off(0);
		pomp_State = true;

		saved_turbo = tuurbo;
		tuurbo = TURBOOFF;
		apply_fan();

		tpower = true;
		send_eho();
		ESP_LOGI(TAG, "power -> OFF");
	} else {
		// turn ON
		relay_block_set_on(2);
		ionic = false;

		relay_block_set_on(1);
		flowSpin = false;

		relay_block_set_on(0);
		pomp_State = false;

		tuurbo = saved_turbo;
		apply_fan();

		tpower = false;
		send_eho();
		ESP_LOGI(TAG, "power -> ON");
	}
}

void humid_ctrl_init(void)
{
	ESP_ERROR_CHECK(relay_block_init());

	// стартовий стан як у Arduino (всі OFF + turbo off)
	pomp_State = true;
	flowSpin = true;
	ionic = true;
	tuurbo = TURBOOFF;
	saved_turbo = TURBO1;
	tpower = true;

	apply_fan();
	send_eho();

	ESP_LOGI(TAG, "init done (shadow=0x%02X)", relay_block_get_shadow());
}

void humid_ctrl_handle_cmd(const char *txt)
{
	if (!txt) return;

	if (strcmp(txt, "pomp") == 0) {
		pimp();
		return;
	}
	if (strcmp(txt, "flow") == 0) {
		flo();
		return;
	}
	if (strcmp(txt, "ion") == 0) {
		ionn();
		return;
	}
	if (strcmp(txt, "echo_turb") == 0) {
		send_eho();
		return;
	}
	if (strcmp(txt, "huOn") == 0) {
		power_toggle();
		return;
	}

	// турбо команди як у тебе
	if (strcmp(txt, "140") == 0) { set_turbo(TURBOOFF); return; }
	if (strcmp(txt, "141") == 0) { set_turbo(TURBO1); return; }
	if (strcmp(txt, "142") == 0) { set_turbo(TURBO2); return; }
	if (strcmp(txt, "143") == 0) { set_turbo(TURBO3); return; }

	ESP_LOGI(TAG, "unknown cmd: \"%s\"", txt);
}
