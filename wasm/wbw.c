/* SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * War Bells — granular/glitch/delay/looper audio FX for Ableton Move
 * Copyright (C) 2026 Chris Farrell
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version. It is distributed WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU AGPL <https://www.gnu.org/licenses/> for more details.
 */

/* wbw.c — thin C entry points for the browser (WASM) build of War Bells.
 *
 * Drives the REAL audio_fx v2 plugin API — the exact same calls the Move host
 * makes — so the in-browser engine is the actual DSP, not a reimplementation.
 * War Bells is an effect, so wbw_render processes audio IN PLACE (the JS demo
 * feeds it a source). Host is NULL: wb_log() is NULL-guarded and get_bpm() is
 * only used in Sync clock mode, so this is safe. */
#include <stdint.h>
#include "host/plugin_api_v1.h"
#include "host/audio_fx_api_v2.h"

audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);

static audio_fx_api_v2_t *API;
static void *INST;

void wbw_init(void) {
    API  = move_audio_fx_init_v2(0);
    INST = API->create_instance(".", 0);
}
void wbw_set(const char *key, const char *val) { if (API && INST) API->set_param(INST, key, val); }
int  wbw_get(const char *key, char *buf, int n) { return (API && INST) ? API->get_param(INST, key, buf, n) : -1; }
void wbw_render(int16_t *buf, int frames)       { if (API && INST) API->process_block(INST, buf, frames); }  /* in-place FX */
void wbw_midi(int b0, int b1, int b2) {
    if (API && INST && API->on_midi) { uint8_t m[3] = {(uint8_t)b0,(uint8_t)b1,(uint8_t)b2}; API->on_midi(INST, m, 3, 0); }
}
