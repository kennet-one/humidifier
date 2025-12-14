#include "legacy_proto.h"
#include "esp_log.h"

static const char *TAG = "legacy";

void legacy_handle_text(const char *txt)
{
	ESP_LOGI(TAG, "legacy RX: \"%s\"", txt);

}
