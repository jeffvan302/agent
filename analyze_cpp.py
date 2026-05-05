import re

f = open(r'src\web_assets_default.cpp', 'r', encoding='utf-8').read()

items = []
for m in re.finditer(r'const char k(\w+)\[\] =\s*R"ASSET\((.*?)\)ASSET";', f, re.DOTALL):
    name = m.group(1)
    size = len(m.group(2))
    items.append((name, size, m.start()))

# Check for chunked app.js separately
app_match = re.search(r'const char kAppJs\[\] = "";', f)
if app_match:
    # find parts
    parts_match = re.search(r'const char\* const kAppJsParts\[\] = \{([^}]+)\};', f, re.DOTALL)
    if parts_match:
        parts = re.findall(r'R"ASSET\((.*?)\)ASSET"', parts_match.group(1), re.DOTALL)
        for i, part in enumerate(parts):
            items.append((f'AppJsPart{i+1}', len(part), 0))

for name, size, pos in items:
    print(f'{name}: {size} chars ({size/1024:.1f} KB)')

# Also check if any single raw string > 65535
for m in re.finditer(r'R"ASSET\((.*?)\)ASSET', f, re.DOTALL):
    size = len(m.group(1))
    if size > 65535:
        print(f'WARNING: found raw string of {size} chars ({size/1024:.1f} KB) at position {m.start()}')
