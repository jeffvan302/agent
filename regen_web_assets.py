#!/usr/bin/env python3
"""Regenerate src/web_assets_default.cpp from the www/ source files."""
import pathlib

root = pathlib.Path(r'C:\Users\TheunisvanNiekerk\Code\agent')
www_root = root / 'www'
src_path = root / 'src' / 'web_assets_default.cpp'

def file_content(rel_path):
    with open(www_root / rel_path, 'r', encoding='utf-8') as f:
        return f.read()

def emit_single(name, content):
    return f'const char k{name}[] =\nR"ASSET(\n{content}\n)ASSET";\n'

def emit_chunked(name, content, max_chunk=60000):
    parts = []
    while content:
        chunk = content[:max_chunk]
        content = content[max_chunk:]
        parts.append(f'R"ASSET(\n{chunk}\n)ASSET"')
    lines = []
    lines.append(f'const char k{name}[] = "";')
    lines.append(f'const char* const k{name}Parts[] = {{')
    for i, part in enumerate(parts):
        if i + 1 < len(parts):
            lines.append(part + ',')
        else:
            lines.append(part)
    lines.append('};')
    lines.append(f'const std::size_t k{name}PartCount = sizeof(k{name}Parts) / sizeof(k{name}Parts[0]);')
    return '\n'.join(lines) + '\n'

blocks = []

blocks.append('// AUTO-GENERATED — do not edit by hand.')
blocks.append('// Including the header first gives const variables external linkage.')
blocks.append('#include "web_assets_default.h"')
blocks.append('')
blocks.append('namespace DefaultWebAssets {')
blocks.append('')

blocks.append(emit_single('IndexHtml', file_content('index.html')))
blocks.append(emit_single('LoginHtml', file_content('login.html')))
blocks.append(emit_single('ChangePasswordHtml', file_content('change-password.html')))
blocks.append(emit_single('BaseCss', file_content('css/base.css')))
blocks.append(emit_chunked('AppJs', file_content('js/app.js')))
blocks.append(emit_single('AutomationJs', file_content('js/automation.js')))
blocks.append(emit_single('LoginJs', file_content('js/login.js')))
blocks.append(emit_single('ChangePasswordJs', file_content('js/change-password.js')))
blocks.append(emit_single('ThemeDefaultCss', file_content('themes/default/style.css')))
blocks.append(emit_single('ThemeDefaultJson', file_content('themes/default/theme.json')))
blocks.append(emit_single('ThemeDarkCss', file_content('themes/dark/style.css')))
blocks.append(emit_single('ThemeDarkJson', file_content('themes/dark/theme.json')))

blocks.append('} // namespace DefaultWebAssets')
blocks.append('')

with open(src_path, 'w', encoding='utf-8', newline='') as f:
    f.write('\n'.join(blocks))

print('Regenerated web_assets_default.cpp successfully.')
