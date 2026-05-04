import os

def read_file(path):
    with open(path, 'rb') as f:
        raw = f.read()
    if raw[:2] == b'\xff\xfe':
        text = raw.decode('utf-16-le')
    else:
        text = raw.decode('utf-8')
    return text

text = read_file('src/openai_client.cpp')
lines = text.splitlines()
for i in range(245, 260):
    print(f"{i}: {lines[i]}")
