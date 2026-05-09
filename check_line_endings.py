with open(r'C:\Temp\Creating_Applications\admin\Test_Face\README.md', 'rb') as f:
    content = f.read()
print('File size:', len(content))
print('Has CRLF:', b'\r\n' in content)
print('Has LF:', b'\n' in content)
print('CRLF count:', content.count(b'\r\n'))
print('LF count:', content.count(b'\n'))
