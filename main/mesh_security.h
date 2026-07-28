// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static inline bool humidifier_root_sta_from_mesh_ap(
	const uint8_t mesh_ap[6],
	uint8_t root_sta[6])
{
	if (!mesh_ap || !root_sta) return false;
	memcpy(root_sta, mesh_ap, 6);
	for (int i = 5; i >= 0; i--) {
		if (root_sta[i] != 0) {
			root_sta[i]--;
			return true;
		}
		root_sta[i] = 0xff;
	}
	memset(root_sta, 0, 6);
	return false;
}

static inline bool humidifier_v1_origin_allowed(
	const uint8_t from[6],
	const uint8_t packet_src[6],
	const uint8_t root[6],
	bool lossless_negotiated)
{
	static const uint8_t zero[6] = {0};
	if (!from || !packet_src || !root || lossless_negotiated) return false;
	if (memcmp(root, zero, sizeof(zero)) == 0) return false;
	return memcmp(packet_src, from, 6) == 0 &&
	       memcmp(root, from, 6) == 0;
}
