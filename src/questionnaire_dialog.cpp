#include "questionnaire_dialog.h"

#include "util.h"

#include <windowsx.h>

#include <limits>

namespace {
constexpr wchar_t kQuestionnaireClassName[] = L"AgentQuestionnaireDialog";

constexpr int kOptionBaseId = 2000;
constexpr int kConfirmId = 2998;
constexpr int kCancelId = 2999;

struct QuestionnaireDialogState {
    QuestionnaireOptions options;
    HWND owner = nullptr;
    HFONT font = nullptr;
    std::optional<std::vector<int>> result;
    std::vector<HWND> option_hwnds;
    HWND confirm_btn = nullptr;
};

int DpiScale(HWND hwnd, int value) {
    return MulDiv(value, GetDpiForWindow(hwnd), 96);
}

void RegisterQuestionnaireClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) {
        return;
    }
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = [](HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) -> LRESULT {
        auto* state = reinterpret_cast<QuestionnaireDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (message) {
        case WM_NCCREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
            auto* initial_state = reinterpret_cast<QuestionnaireDialogState*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(initial_state));
            return TRUE;
        }
        case WM_CREATE: {
            state = reinterpret_cast<QuestionnaireDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            state->font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            const int margin = DpiScale(hwnd, 12);
            const int spacing = DpiScale(hwnd, 6);
            const int row_h = DpiScale(hwnd, 22);
            const int button_w = DpiScale(hwnd, 90);
            const int button_h = DpiScale(hwnd, 28);
            const int label_h = DpiScale(hwnd, 20);
            const int client_w = state->options.width;

            HWND question_label = CreateWindowExW(0, L"STATIC", state->options.question.c_str(),
                WS_CHILD | WS_VISIBLE, margin, margin, client_w - margin * 2, label_h, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(question_label, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);

            int y = margin + label_h + spacing;
            for (size_t i = 0; i < state->options.labels.size(); ++i) {
                const DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX;
                HWND btn = CreateWindowExW(0, L"BUTTON", state->options.labels[i].c_str(), style,
                    margin + DpiScale(hwnd, 4), y, client_w - margin * 2 - DpiScale(hwnd, 8), row_h,
                    hwnd, reinterpret_cast<HMENU>(kOptionBaseId + static_cast<int>(i)), nullptr, nullptr);
                SendMessageW(btn, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
                state->option_hwnds.push_back(btn);
                y += row_h + spacing;
            }

            int button_row_y = state->options.height - margin - button_h;
            if (state->options.allow_multiple) {
                state->confirm_btn = CreateWindowExW(0, L"BUTTON", L"Confirm",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    client_w - margin - button_w, button_row_y, button_w, button_h,
                    hwnd, reinterpret_cast<HMENU>(kConfirmId), nullptr, nullptr);
                SendMessageW(state->confirm_btn, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            }
            HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                client_w - margin - button_w * 2 - spacing, button_row_y, button_w, button_h,
                hwnd, reinterpret_cast<HMENU>(kCancelId), nullptr, nullptr);
            SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);

            CenterWindowToOwner(hwnd, state->owner);
            if (!state->option_hwnds.empty()) {
                SetFocus(state->option_hwnds.front());
            }
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(w_param);
            if (id == kConfirmId || id == kCancelId) {
                if (id == kConfirmId) {
                    std::vector<int> selected;
                    for (size_t i = 0; i < state->option_hwnds.size(); ++i) {
                        if (IsDlgButtonChecked(hwnd, kOptionBaseId + static_cast<int>(i)) == BST_CHECKED) {
                            selected.push_back(static_cast<int>(i));
                        }
                    }
                    if (!selected.empty()) {
                        state->result = std::move(selected);
                    }
                }
                DestroyWindow(hwnd);
                return 0;
            }
            if (id >= kOptionBaseId && !state->options.allow_multiple) {
                int clicked = id - kOptionBaseId;
                // Radio-like behavior: uncheck others
                for (size_t i = 0; i < state->option_hwnds.size(); ++i) {
                    if (static_cast<int>(i) != clicked) {
                        CheckDlgButton(hwnd, kOptionBaseId + static_cast<int>(i), BST_UNCHECKED);
                    } else {
                        CheckDlgButton(hwnd, kOptionBaseId + static_cast<int>(i), BST_CHECKED);
                    }
                }
                // Return result immediately for single-choice
                std::vector<int> selected;
                for (size_t i = 0; i < state->option_hwnds.size(); ++i) {
                    if (IsDlgButtonChecked(hwnd, kOptionBaseId + static_cast<int>(i)) == BST_CHECKED) {
                        selected.push_back(static_cast<int>(i));
                        break;
                    }
                }
                if (!selected.empty()) {
                    state->result = std::move(selected);
                    DestroyWindow(hwnd);
                }
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        }
        return DefWindowProcW(hwnd, message, w_param, l_param);
    };
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kQuestionnaireClassName;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
    registered = true;
}
}  // namespace

std::optional<std::vector<int>> ShowQuestionnaireDialog(HWND owner, const QuestionnaireOptions& options) {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    RegisterQuestionnaireClass(instance);

    QuestionnaireDialogState state;
    state.options = options;
    state.owner = owner;

    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE;
    const DWORD ex_style = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;

    if (owner) {
        EnableWindow(owner, FALSE);
    }

    HWND hwnd = CreateWindowExW(ex_style, kQuestionnaireClassName, options.title.c_str(), style,
        CW_USEDEFAULT, CW_USEDEFAULT, options.width, options.height,
        owner, nullptr, instance, &state);
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
