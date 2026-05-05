import pathlib, re

root = pathlib.Path(r'C:\Users\TheunisvanNiekerk\Code\agent')
www_root = root / 'www'

print("Checking www/ files for ')ASSET\"' substring...")
for f in www_root.rglob('*'):
    if f.is_file():
        text = f.read_text(encoding='utf-8')
        if ')ASSET"' in text:
            print(f"  FOUND in {f.relative_to(root)}")
            # Show context
            idx = text.find(')ASSET"')
            print(f"    Context: ...{repr(text[max(0,idx-20):idx+10])}...")
print("Done checking.")

print("\nChecking generated cpp for any raw string literal > 65535 chars...")
cpp = (root / 'src' / 'web_assets_default.cpp').read_text(encoding='utf-8')
matches = list(re.finditer(r'R"ASSET\((.*?)\)ASSET"', cpp, re.DOTALL))
for i, m in enumerate(matches):
    size = len(m.group(1))
    line = cpp[:m.start()].count('\n') + 1
    status = "OK" if size <= 65535 else "WARNING >64KB"
    print(f"  Literal #{i+1}: line {line}, size {size} chars ({size/1024:.1f} KB) — {status}")

print("\nChecking for extra R\"ASSET( openers not matched by closers...")
openers = [m.start() for m in re.finditer(r'R"ASSET\(', cpp)]
closers = [m.start() for m in re.finditer(r'\)ASSET"', cpp)]
print(f"  Openers: {len(openers)}, Closers: {len(closers)}")
if len(openers) != len(closers):
    print("  MISMATCH!")
if len(openers) == len(closers):
    for i, (o, c) in enumerate(zip(openers, closers)):
        size = c - o
        line_o = cpp[:o].count('\n') + 1
        line_c = cpp[:c].count('\n') + 1
        print(f"  Pair {i+1}: opener line {line_o}, closer line {line_c}, gap {size} chars")
