// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h"

#define LEGACY_ROOT_MSG_MAX_LEN 96

void legacy_root_sender_start(UBaseType_t task_priority);
bool legacy_send_to_root(const char *text);
