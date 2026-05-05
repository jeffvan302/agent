with open('src/web_assets_default.cpp','rb') as f:
    raw = f.read()

# Check for NUL bytes
nuls = raw.count(b'\x00')
print(f'NUL bytes: {nuls}')

# Find line lengths around problematic area
lines = raw.decode('utf-8-sig').split('\n')
print(f'Total lines: {len(lines)}')

# Check for very long lines (>1000 chars) which might indicate a raw string that wasn't split
for i, line in enumerate(lines, 1):
    if len(line) > 2000:
        print(f'  Very long line {i}: {len(line)} chars')
        print(f'    First 80 chars: {repr(line[:80])}')
        print(f'    Last  80 chars: {repr(line[-80:])}')

print('--- Searching for )ASSET" and R"ASSET( on same line ---')
for i, line in enumerate(lines, 1):
    if ')ASSET"' in line and 'R"ASSET(' in line and i > 250:
        print(f'  Line {i}: {repr(line[:120])}')
