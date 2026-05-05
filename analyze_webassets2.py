with open('src/web_assets_default.cpp', 'r', encoding='utf-8') as f:
    lines = f.readlines()

in_raw = False
raw_start_line = 0
raw_strings = []

for i, line in enumerate(lines, 1):
    line = line.rstrip('\n')
    if not in_raw:
        if 'R"ASSET(' in line:
            in_raw = True
            raw_start_line = i
            raw_content_lines = 0
            # Check if it also closes on the same line
            opener_idx = line.index('R"ASSET(')
            after_opener = line[opener_idx + 8:]  # len('R"ASSET(') = 8
            if ')ASSET"' in after_opener:
                in_raw = False
                raw_content_lines = 0
                raw_strings.append((raw_start_line, i, 0))
    else:
        # We are inside a raw string
        if ')ASSET"' in line:
            raw_strings.append((raw_start_line, i, raw_content_lines))
            in_raw = False
        else:
            raw_content_lines += 1

print(f"Found {len(raw_strings)} raw string literals")
for start, end, content_lines in raw_strings:
    size = sum(len(line.rstrip('\n')) for line in lines[start-1:end])
    status = "OK" if size <= 65535 else "WARNING >64KB"
    print(f"  Lines {start}-{end}: {size} chars, {content_lines} content lines, {end-start+1} total lines — {status}")
