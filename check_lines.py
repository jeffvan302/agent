text = open('src/openai_client.cpp','rb').read().decode('utf-16-le')
lines = text.splitlines()
for i in range(245, 265):
    print(f"{i}: {lines[i]}")
print('---')
for i in range(273, 288):
    print(f"{i}: {lines[i]}")
