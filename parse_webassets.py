import re

with open('src/web_assets_default.cpp', 'r', encoding='utf-8') as f:
    text = f.read()

# Find all R"ASSET( raw strings and measure them
matches = list(re.finditer(r'R"ASSET\((.*?)\)ASSET"', text, re.DOTALL))
print(f"Found {len(matches)} raw string literals")

for i, m in enumerate(matches):
    size = len(m.group(1))
    line_num = text[:m.start()].count('\n') + 1
    status = "WARNING >64KB" if size > 65535 else "OK"
    print(f"  Literal #{i+1}: line {line_num}, size {size} chars ({size/1024:.1f} KB) — {status}")

# Find all kXxx[] = declarations
decls = re.finditer(r'const char k(\w+)\[\]', text)
print("\nDeclarations found:")
for d in decls:
    line_num = text[:d.start()].count('\n') + 1
    print(f"  Line {line_num}: {d.group()}")
