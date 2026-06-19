#!/usr/bin/env bash
# Assemble the SINGLE-FILE shippable web app: inline wbw.js (the SINGLE_FILE WASM engine) into
# web_ui.html -> docs/index.html. One file to ship to GitHub Pages AND to the Move; it detects its
# context at runtime (https/off-Move = demo+manual; http/served-from-Move = live control of War Bells
# in Schwung) and only instantiates the WASM lazily (never in live mode).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
python3 - <<'PY'
html = open('web_ui.html').read()
js   = open('wbw.js').read()
tag  = '<script src="wbw.js"></script>'
assert tag in html, "expected '<script src=\"wbw.js\"></script>' in web_ui.html"
html = html.replace(tag, '<script>/* wbw.js inlined — single-file ship */\n' + js + '\n</script>')
open('docs/index.html', 'w').write(html)
print('built docs/index.html (single file, %d KB)' % (len(html)//1024))
PY
rm -f docs/wbw.js   # no longer needed — inlined
