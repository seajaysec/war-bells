# Third-Party Notices

War Bells (AGPL-3.0-or-later © Chris Farrell) includes / builds against the following
third-party work. These permissively-licensed files retain their own licenses; the rest of the
project is AGPLv3.

## Schwung host ABI headers

`src/host/audio_fx_api_v2.h` and `src/host/plugin_api_v1.h` are redistributed unmodified from
the Schwung project so the module can be built against the host ABI.

> MIT License — Copyright (c) 2025-2026 Charles Vestal
> https://github.com/charlesvestal/schwung

The full MIT terms above (see `LICENSE`) apply to these files; the copyright remains with
Charles Vestal.

## Freeverb (reverb)

`src/dsp/core/reverb.h` implements a Schroeder–Moorer reverb adapted from **Freeverb** by
Jezar at Dreampoint, which the author released into the **public domain**. The implementation
here is an independent rewrite of that well-known topology.
