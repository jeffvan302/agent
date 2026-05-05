
import re
with open('src/web_assets_default.cpp','r') as f:
    text = f.read()
pattern = re.compile(r'const char k(\w+)\[\] =\s*R\"ASSET\((.*?)\)ASSET\";', re.DOTALL)
for m in pattern.finditer(text):
    name = m.group(1)
    size = len(m.group(2))
    print('%s: %d chars (%.1f KB)' % (name, size, size/1024.0))
app = re.search(r'const char\* const kAppJsParts\[\] = \{([^}]+)\};', text, re.DOTALL)
if app:
    parts = re.findall(r'R\"ASSET\((.*?)\)ASSET\"', app.group(1), re.DOTALL)
    for i,p in enumerate(parts):
        size = len(p)
        print('AppJsPart%d: %d chars (%.1f KB)' % (i+1, size, size/1024.0))
