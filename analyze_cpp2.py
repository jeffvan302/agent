import re

with open(r'src\web_assets_default.cpp', 'r', encoding='utf-8') as fh:
    f = fh.read()

# Find all standalone R"ASSET( raw strings
for m in re.finditer(r'R"ASSET\((.*?)\)ASSET"', f, re.DOTALL):
    size = len(m.group(1))
    line = f[:m.start()].count('\n') + 1
    status = "WARNING >64KB" if size > 65535 else "OK"
    print(f'Line {line}: size {size} chars ({size/1024:.1f} KB) — {status}')
