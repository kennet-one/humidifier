#include "legacy_proto.h"
#include "esp_log.h"
#include "humid_ctrl.h"
#include "water_led.h"
#include "pms5003_node.h"

static const char *TAG = "legacy";

void legacy_handle_text(const char *txt)
{
	ESP_LOGI(TAG, "legacy RX: \"%s\"", txt);
	water_led_handle_cmd(txt);
	humid_ctrl_handle_cmd(txt);

	if (strcmp(txt, "pm1") == 0) {
	pms5003_trigger_once();
	return;
}

}
