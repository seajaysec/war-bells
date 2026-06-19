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

/*
 * War Bells — Signal Chain quick editor.
 *
 * Compact editor shown when War Bells is selected inside a Signal Chain. The full
 * paged menu (Effect / Time / Tone / Space / Hold / Looper / Config) is rendered
 * by the host from module.json's ui_hierarchy; this is just the macro view.
 *
 * Jog wheel selects a macro; any of knobs 1-4 adjusts the selected macro.
 * Uses globalThis.chain_ui (do NOT override globalThis.init / globalThis.tick).
 */

import {
    MoveMainKnob,
    MoveKnob1, MoveKnob2, MoveKnob3, MoveKnob4
} from '/data/UserData/schwung/shared/constants.mjs';

import { decodeDelta } from '/data/UserData/schwung/shared/input_filter.mjs';

import {
    drawMenuHeader as drawHeader,
    drawMenuFooter as drawFooter
} from '/data/UserData/schwung/shared/menu_layout.mjs';

const SCREEN_WIDTH = 128;

/* float macros shown here (effect/variation/etc. live in the full menu) */
const PARAMS = [
    { key: "activity", name: "Activity", min: 0, max: 1, step: 0.02 },
    { key: "repeats",  name: "Repeats",  min: 0, max: 1, step: 0.02 },
    { key: "shape",    name: "Shape",    min: 0, max: 1, step: 0.02 },
    { key: "mix",      name: "Mix",      min: 0, max: 1, step: 0.02 },
    { key: "space",    name: "Space",    min: 0, max: 1, step: 0.02 },
    { key: "filter",   name: "Filter",   min: 0, max: 1, step: 0.02 }
];

let selected = 0;
let values = PARAMS.map(() => 0);
let needsRedraw = true;

function fetchParams() {
    for (let i = 0; i < PARAMS.length; i++) {
        const v = host_module_get_param(PARAMS[i].key);
        if (v !== null && v !== undefined) values[i] = parseFloat(v) || 0;
    }
}

function adjust(i, delta) {
    const p = PARAMS[i];
    values[i] = Math.max(p.min, Math.min(p.max, values[i] + delta * p.step));
    host_module_set_param(p.key, values[i].toFixed(3));
}

function drawUI() {
    clear_screen();
    drawHeader("War Bells");
    const listY = 16, lh = 8;
    for (let i = 0; i < PARAMS.length; i++) {
        const y = listY + i * lh;
        const sel = i === selected;
        if (sel) fill_rect(0, y - 1, SCREEN_WIDTH, lh, 1);
        const color = sel ? 0 : 1;
        print(2, y, `${sel ? ">" : " "} ${PARAMS[i].name}`, color);
        const pct = `${Math.round(values[i] * 100)}%`;
        print(SCREEN_WIDTH - pct.length * 6 - 4, y, pct, color);
    }
    drawFooter({ left: "Jog: select", right: "Knob: adjust" });
    needsRedraw = false;
}

function init() { fetchParams(); needsRedraw = true; }
function tick() { if (needsRedraw) drawUI(); }

function onMidiMessageInternal(data) {
    const status = data[0], d1 = data[1], d2 = data[2];
    if ((status & 0xF0) !== 0xB0) return;
    if (d1 === MoveMainKnob) {
        const delta = decodeDelta(d2);
        if (delta !== 0) {
            selected = Math.max(0, Math.min(PARAMS.length - 1, selected + delta));
            needsRedraw = true;
        }
        return;
    }
    if (d1 === MoveKnob1 || d1 === MoveKnob2 || d1 === MoveKnob3 || d1 === MoveKnob4) {
        const delta = decodeDelta(d2);
        if (delta !== 0) { adjust(selected, delta); needsRedraw = true; }
    }
}

globalThis.chain_ui = { init, tick, onMidiMessageInternal };
