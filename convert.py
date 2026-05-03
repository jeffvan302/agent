import os

src = os.path.join(os.getcwd(), 'src/provider_manager.cpp')
dst = os.path.join(os.getcwd(), 'src/provider_manager.cpp.utf8')

with open(src, 'rb') as f:
    data = f.read()
# Assume UTF-16 LE with BOM
if data.startswith(b'\xff\xfe'):
    text = data.decode('utf-16-le')
else:
    text = data.decode('utf-16-le')

with open(dst, 'w', encoding='utf-8') as f:
    f.write(text)
