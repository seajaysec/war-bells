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

/* ringbuf.h — rolling stereo capture of the live input that the granular voices
 * read from. Freezing the write head implements the Hold Sampler. Buffer memory is
 * owned by the engine instance (allocated once at create); this struct just tracks
 * the write head + helpers for interpolated reads. */
#ifndef WB_RINGBUF_H
#define WB_RINGBUF_H

#include <stdint.h>
#include "util.h"

typedef struct {
    float *l, *r;   /* engine-owned buffers, length = len */
    int len;
    int w;          /* write head */
    int frozen;     /* 1 = Hold engaged (stop advancing the write head) */
} wb_ring_t;

static inline void wb_ring_init(wb_ring_t *rb, float *l, float *r, int len) {
    rb->l = l; rb->r = r; rb->len = len; rb->w = 0; rb->frozen = 0;
}

static inline void wb_ring_write(wb_ring_t *rb, float l, float r) {
    if (rb->frozen) return;
    rb->l[rb->w] = l;
    rb->r[rb->w] = r;
    if (++rb->w >= rb->len) rb->w = 0;
}

/* linear-interpolated read at a fractional sample position (wrapped). */
static inline void wb_ring_read(const wb_ring_t *rb, float pos, float *ol, float *or_) {
    while (pos < 0.0f) pos += (float)rb->len;
    while (pos >= (float)rb->len) pos -= (float)rb->len;
    int i0 = (int)pos;
    int i1 = i0 + 1; if (i1 >= rb->len) i1 = 0;
    float f = pos - (float)i0;
    *ol = wb_lerpf(rb->l[i0], rb->l[i1], f);
    *or_ = wb_lerpf(rb->r[i0], rb->r[i1], f);
}

#endif /* WB_RINGBUF_H */
