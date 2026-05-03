import codecs
import sys

# Important: keep CR LF intact; string literals use \r\n because source file has CRLF.
path = r'C:\Users\TheunisvanNiekerk\Code\agent\src\provider_manager.cpp'
with open(path, 'rb') as f:
    raw = f.read()
assert raw.startswith(codecs.BOM_UTF16_LE), 'Expected UTF-16 LE BOM'
text = raw.decode('utf-16-le')

# Edit 1
old1 = '    kTestConnection = 2009,\r\n    kStatusLabel = 2010,\r\n    kCloseButton = 2011,\r\n'
new1 = '    kTestConnection = 2009,\r\n    kTestQueue = 2010,\r\n    kStatusLabel = 2011,\r\n    kCloseButton = 2012,\r\n'
assert old1 in text, 'Edit 1: old string not found'
text = text.replace(old1, new1, 1)
print('Edit 1 succeeded')

# Edit 2a
old2a = '        close_button_ = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kCloseButton), nullptr, nullptr);\r\n'
new2a = '        test_queue_button_ = CreateWindowExW(0, L"BUTTON", L"Test Queue", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kTestQueue), nullptr, nullptr);\r\n        close_button_ = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kCloseButton), nullptr, nullptr);\r\n'
assert old2a in text, 'Edit 2a: old string not found'
text = text.replace(old2a, new2a, 1)
print('Edit 2a succeeded')

# Edit 2b
old2b = '        for (HWND control : {providers_list_, models_list_, add_provider_button_, edit_provider_button_, remove_provider_button_, add_model_button_, edit_model_button_, remove_model_button_, test_connection_button_, close_button_, status_label_}) {\r\n'
new2b = '        for (HWND control : {providers_list_, models_list_, add_provider_button_, edit_provider_button_, remove_provider_button_, add_model_button_, edit_model_button_, remove_model_button_, test_connection_button_, test_queue_button_, close_button_, status_label_}) {\r\n'
assert old2b in text, 'Edit 2b: old string not found'
text = text.replace(old2b, new2b, 1)
print('Edit 2b succeeded')

# Edit 3
old3 = '        case kTestConnection:\r\n            TestConnection();\r\n            break;\r\n        case kCloseButton:\r\n'
new3 = '        case kTestConnection:\r\n            TestConnection();\r\n            break;\r\n        case kTestQueue:\r\n            MessageBoxW(hwnd_, L"Queue test not yet implemented.", L"Test Queue", MB_OK);\r\n            break;\r\n        case kCloseButton:\r\n'
assert old3 in text, 'Edit 3: old string not found'
text = text.replace(old3, new3, 1)
print('Edit 3 succeeded')

# Edit 4
old4 = '    HWND test_connection_button_ = nullptr;\r\n    HWND close_button_ = nullptr;\r\n'
new4 = '    HWND test_connection_button_ = nullptr;\r\n    HWND test_queue_button_ = nullptr;\r\n    HWND close_button_ = nullptr;\r\n'
assert old4 in text, 'Edit 4: old string not found'
text = text.replace(old4, new4, 1)
print('Edit 4 succeeded')

with open(path, 'wb') as f:
    f.write(codecs.BOM_UTF16_LE + text.encode('utf-16-le'))
print('File written successfully')
