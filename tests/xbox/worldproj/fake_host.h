/* SPDX-License-Identifier: MIT */
/* The knobs of fake_host.c (SPDX: MIT, 2026 Patrick Carey). */
#ifndef WORLDPROJ_TEST_FAKE_HOST_H
#define WORLDPROJ_TEST_FAKE_HOST_H
#include <stdbool.h>
#include <stdint.h>
extern bool fake_view_projection;
extern int64_t fake_title_id;
extern int fake_requests, fake_forgets;
extern bool fake_mod_armed;
extern bool fake_view_armed;
extern char fake_mod_asked[64];
int fake_rows_outside(const uint32_t file[][4], const float copy[][4]);
#endif
