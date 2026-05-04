import codecs, sys

def read(path):
    with open(path, 'rb') as f:
        raw = f.read()
    if raw[:2] == b'\xff\xfe':
        return raw.decode('utf-16-le'), '\r\n', True
    text = raw.decode('utf-8')
    return text, '\r\n' if '\r\n' in text else '\n', False

pm, eol, bom = read('src/provider_manager.cpp')
lines = pm.splitlines()
with open('tmp_inspect2.txt','w',encoding='utf-8') as f:
    f.write('--- lines 1560-1580 ---\n')
    for i,l in enumerate(lines[1559:1580], start=1560):
        f.write(f'{i:04d}| {l}\n')
    f.write('--- lines 1975-1985 ---\n')
    for i,l in enumerate(lines[1974:1985], start=1975):
        f.write(f'{i:04d}| {l}\n')
print('Wrote tmp_inspect2.txt')
