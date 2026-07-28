// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

typedef struct {
	uint32_t accepted;
	uint32_t delivered;
	uint32_t coalesced;
	uint32_t overflow;
	uint32_t send_errors;
	uint32_t pending;
	uint32_t high_watermark;
	int32_t last_send_err;
} keemash_mesh_event_outbox_stats_t;
