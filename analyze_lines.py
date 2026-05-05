with open('src/web_assets_default.cpp','r') as f:
    lines = f.readlines()
for i, line in enumerate(lines,1):
    if 'R"ASSET(' in line:
        print('Line %d: %s' % (i, line[:80].rstrip()))
