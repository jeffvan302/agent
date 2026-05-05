with open('src/web_assets_default.cpp','r',encoding='utf-8') as f:
    lines = f.readlines()

count = 0
for i, line in enumerate(lines,1):
    if ')ASSET"' in line:
        count += 1
        print(f'Line {i}: {repr(line.rstrip())}')
        if count > 5:
            print(f'... ({count-5} more found)')
            break

print(f"\nTotal ')ASSET\"' occurrences: {sum(1 for l in lines if ')ASSET\"' in l)}")

# Which one is the first that should close base.css?
# base.css starts at line 250, so its closing )ASSET" should be the first after that.
print("\nFirst occurrence after base.css start (line 250):")
for i, line in enumerate(lines[249:], 250):
    if ')ASSET"' in line:
        print(f'Line {i}: {repr(line.rstrip())}')
        break
