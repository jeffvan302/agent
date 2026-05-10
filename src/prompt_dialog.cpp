#include "prompt_dialog.h"

#include "util.h"

#include <windowsx.h>

namespace {
constexpr wchar_t kPromptClassName[] = L"AgentPromptDialog";

enum ControlId : int {
    kPromptLabel = 1001,
    kPromptEdit = 1002,
    kPromptOk = IDOK,
    kPromptCancel = IDCANCEL,
};

struct PromptDialogState {
    PromptOptions options;
    HWND owner = nullptr;
    HWND edit = nullptr;
    HFONT font = nullptr;
    std::optional<std::wstring> result;
};

int DpiScale(HWND hwnd, int value) {
    return MulDiv(value, GetDpiForWindow(hwnd), 96);
}

void RegisterPromptClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) {
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = [](HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) -> LRESULT {
        auto* state = reinterpret_cast<PromptDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (message) {
        case WM_NCCREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
            auto* initial_state = reinterpret_cast<PromptDialogState*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(initial_state));
            return TRUE;
        }
        case WM_CREATE: {
            state = reinterpret_cast<PromptDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            state->font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

            const int margin = DpiScale(hwnd, 12);
            const int button_width = DpiScale(hwnd, 90);
            const int button_height = DpiScale(hwnd, 28);
            const int label_height = DpiScale(hwnd, 20);
            const int edit_height = state->options.multiline ? DpiScale(hwnd, 80) : DpiScale(hwnd, 24);
            const int width = DpiScale(hwnd, state->options.width);
            const int height = DpiScale(hwnd, state->options.height);

            SetWindowPos(hwnd, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);

            HWND label = CreateWindowExW(0, L"STATIC", state->options.label.c_str(), WS_CHILD | WS_VISIBLE, margin, margin, width - margin * 2, label_height, hwnd, reinterpret_cast<HMENU>(kPromptLabel), nullptr, nullptr);
            const DWORD edit_style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP | (state->options.multiline ? (ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL) : 0);
            state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->options.initial_text.c_str(), edit_style, margin, margin + label_height + DpiScale(hwnd, 6), width - margin * 2, edit_height, hwnd, reinterpret_cast<HMENU>(kPromptEdit), nullptr, nullptr);
            HWND ok = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, width - margin * 2 - button_width * 2 - DpiScale(hwnd, 8), height - margin - button_height, button_width, button_height, hwnd, reinterpret_cast<HMENU>(kPromptOk), nullptr, nullptr);
            HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, width - margin - button_width, height - margin - button_height, button_width, button_height, hwnd, reinterpret_cast<HMENU>(kPromptCancel), nullptr, nullptr);

            SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            SendMessageW(state->edit, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);

            CenterWindowToOwner(hwnd, state->owner);
            SetFocus(state->edit);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(w_param)) {
            case kPromptOk:
                state->result = TrimWide(GetWindowTextString(state->edit));
                DestroyWindow(hwnd);
                return 0;
            case kPromptCancel:
                DestroyWindow(hwnd);
                return 0;
            default:
                break;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        }

        return DefWindowProcW(hwnd, message, w_param, l_param);
    };
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kPromptClassName;
    wc.style = CS_HREDRAW | CS_VREDRAW;

    RegisterClassExW(&wc);
    registered = true;
}
}  // namespace

std::optional<std::wstring> ShowPromptDialog(HWND owner, const PromptOptions& options) {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    RegisterPromptClass(instance);

    PromptDialogState state;
    state.options = options;
    state.owner = owner;

    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE;
    const DWORD ex_style = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;

    if (owner) {
        EnableWindow(owner, FALSE);
    }

    HWND hwnd = CreateWindowExW(ex_style, kPromptClassName, options.title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, options.width, options.height, owner, nullptr, instance, &state);
    if (!hwnd) {
        if (owner) {
            EnableWindow(owner, TRUE);
        }
        return std::nullopt;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (owner) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
    }

    return state.result;
}
