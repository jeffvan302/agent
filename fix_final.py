import codecs, sys

def read(path):
    with open(path, 'rb') as f:
        raw = f.read()
    if raw[:2] == b'\xff\xfe':
        return raw.decode('utf-16-le'), '\r\n', True
    text = raw.decode('utf-8')
    return text, '\r\n' if '\r\n' in text else '\n', False

def write(path, text, eol, bom):
    if not text.endswith(eol):
        text += eol
    with open(path, 'wb') as f:
        f.write(text.encode('utf-16-le' if bom else 'utf-8'))

pm, eol, bom = read('src/provider_manager.cpp')

# 1. Add forward declaration of QueueTestDialog before ModelEditorDialog
if 'class QueueTestDialog;' not in pm:
    old_forward = 'struct ModelEditorResult {'
    new_forward = 'class QueueTestDialog;\n\nstruct ModelEditorResult {'
    if old_forward in pm:
        pm = pm.replace(old_forward, new_forward, 1)
        print('Added forward declaration for QueueTestDialog')
    else:
        print('WARN: could not find insertion point for forward declaration')
else:
    print('Forward declaration already present')

# 2. Restore ProviderManagerWindow for-loop that was accidentally removed
# Currently there is a stray:
#             SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
#         }
# We need to replace that with a proper for-loop.
old_stray = '''        status_label_ = CreateWindowExW(0, L"STATIC", L"Select a provider and model to test the connection.", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kStatusLabel), nullptr, nullptr);

            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }

        RefreshProviders();'''
new_stray = '''        status_label_ = CreateWindowExW(0, L"STATIC", L"Select a provider and model to test the connection.", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kStatusLabel), nullptr, nullptr);

        for (HWND control : {providers_list_, models_list_, add_provider_button_, edit_provider_button_, remove_provider_button_, add_model_button_, edit_model_button_, remove_model_button_, test_connection_button_, close_button_, status_label_}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }

        RefreshProviders();'''
if old_stray in pm:
    pm = pm.replace(old_stray, new_stray)
    print('Restored ProviderManagerWindow for-loop')
else:
    print('WARN: could not find stray SendMessageW block to restore')

write('src/provider_manager.cpp', pm, eol, bom)
print('Done')
