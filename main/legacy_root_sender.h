// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "keemash_mesh_event_outbox.h"

#define LEGACY_ROOT_MSG_MAX_LEN 96

esp_err_t legacy_root_sender_start(UBaseType_t task_priority);
bool legacy_send_to_root(const char *text);
bool legacy_send_state_to_root(const char *key, const char *text);
bool legacy_send_group_to_root(const char *const *texts, size_t count);
void legacy_root_sender_stats(keemash_mesh_event_outbox_stats_t *out);
