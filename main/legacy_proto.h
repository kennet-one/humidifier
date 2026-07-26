// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

bool legacy_handle_command(const char *text, esp_err_t *command_error,
			   char *result, size_t result_size);
void legacy_handle_text(const char *text);
