#!/usr/bin/env python3
"""Regenerate src/web_assets_default.cpp from the www/ source files.

Uses raw-string literals for small assets and hex arrays for large ones
(> 30 kB) to avoid MSVC C2026 (string too big).
"""
import pathlib

root = pathlib.Path(r'C:\Users\TheunisvanNiekerk\Code\agent')
www_root = root / 'www'
src_path = root / 'src' / 'web_assets_default.cpp'

LARGE_THRESHOLD = 30 * 1024  # switch to hex array above this

def file_content(rel_path):
    with open(www_root / rel_path, 'r', encoding='utf-8') as f:
        return f.read()

def emit_raw_string(name, content):
    return f'const char k{name}[] =\nR"ASSET(\n{content}\n)ASSET";\n'

def emit_hex_array(name, content):
    """Emit the data as a const char[] initialised from a brace-enclosed
    comma-separated list of hex bytes.  MSVC has no limit on this form."""
    data = content.encode('utf-8')
    lines = []
    lines.append(f'const char k{name}[] = {{')
    # 16 bytes per line to keep width sane
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_values = ', '.join(f'(char)0x{b:02x}' for b in chunk)
        if i + 16 >= len(data):
            lines.append('  ' + hex_values)
        else:
            lines.append('  ' + hex_values + ',')
    lines.append('};')
    return '\n'.join(lines) + '\n'

def emit_asset(name, content):
    if len(content.encode('utf-8')) > LARGE_THRESHOLD:
        return emit_hex_array(name, content)
    return emit_raw_string(name, content)

blocks = []

blocks.append('// AUTO-GENERATED — do not edit by hand.')
blocks.append('#pragma warning(disable : 4310)  // casts to char in hex arrays for UTF-8')
blocks.append('// Including the header first gives const variables external linkage.')
blocks.append('#include "web_assets_default.h"')
blocks.append('')
blocks.append('namespace DefaultWebAssets {')
blocks.append('')

blocks.append(emit_asset('IndexHtml',       file_content('index.html')))
blocks.append(emit_asset('LoginHtml',       file_content('login.html')))
blocks.append(emit_asset('ChangePasswordHtml', file_content('change-password.html')))
blocks.append(emit_asset('BaseCss',         file_content('css/base.css')))
blocks.append(emit_asset('AppJs',           file_content('js/app.js')))
blocks.append(emit_asset('AutomationJs',    file_content('js/automation.js')))
blocks.append(emit_asset('LoginJs',         file_content('js/login.js')))
blocks.append(emit_asset('ChangePasswordJs', file_content('js/change-password.js')))
blocks.append(emit_asset('ThemeDefaultCss', file_content('themes/default/style.css')))
blocks.append(emit_asset('ThemeDefaultJson', file_content('themes/default/theme.json')))
blocks.append(emit_asset('ThemeDarkCss',    file_content('themes/dark/style.css')))
blocks.append(emit_asset('ThemeDarkJson',   file_content('themes/dark/theme.json')))

blocks.append('} // namespace DefaultWebAssets')
blocks.append('')

with open(src_path, 'w', encoding='utf-8', newline='') as f:
    f.write('\n'.join(blocks))

print('Regenerated web_assets_default.cpp successfully.')
