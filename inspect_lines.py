import sys

with open('src/web_assets_default.cpp','rb') as f:
    raw = f.read()

# Count newlines to find byte offset for lines 835, 1424, 1623, 2104, 2613, 3145, 3991, 4457, 4798
def line_to_offset(raw_bytes, target_line):
    offset = 0
    line = 1
    for i, b in enumerate(raw_bytes):
        if line == target_line:
            return i
        if b == ord('\n'):
            line += 1
    return len(raw_bytes)

for ln in [835, 1424, 1623, 2104, 2613, 3145, 3991, 4457, 4798]:
    off = line_to_offset(raw, ln)
    context = raw[max(0,off-2):off+60]
    safe = context.replace(b'\r',b'\\r').replace(b'\n',b'\\n').replace(b'\t',b'\\t')
    print(f'Line {ln:4d} offset {off:6d}: {safe}', flush=True)
