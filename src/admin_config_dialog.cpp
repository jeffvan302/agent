// ──────────────────────────────────────────────────────────────────────────────
// admin_config_dialog.cpp — Win32 tabbed Admin Config dialog.
//
// Control ID range: 8100–8299
//
// Layout
//   Tab control  fills dialog minus header and footer.
//   Tab 0: Users     — ListView (username, display, email, enabled, last-login)
//                      Buttons: Add, Edit, Delete, Enable/Disable,
//                               Reset Password, Force Logout
//   Tab 1: Groups    — Left: Groups ListBox, Right: Members ListBox
//                      Buttons: New Group, Delete Group, Add Member, Remove Member
//   Tab 2: Bindings  — Left: Projects ListBox
//                      Right: Groups CheckListBox + folder-override section
// ──────────────────────────────────────────────────────────────────────────────

#include "admin_config_dialog.h"
#include "util.h"

#include <commctrl.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>
#include <optional>

// ── String helpers (local) ────────────────────────────────────────────────────
namespace {

static std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

static std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

static std::string GetEditText(HWND dlg, int id)
{
    wchar_t buf[1024] = {};
    GetDlgItemTextW(dlg, id, buf, 1024);
    return WideToUtf8(buf);
}

// ── Modal-dialog helper ───────────────────────────────────────────────────────
// Shared message-loop used by the admin dialogs. The owner is disabled while
// the child is open, and the loop exits when the dialog window is destroyed.
// Child dialogs must not use PostQuitMessage; only the main window owns the
// process-level quit signal.
static void RunModalLoop(HWND hw, HWND owner)
{
    const bool owner_was_enabled =
        owner && IsWindow(owner) && IsWindowEnabled(owner);
    if (owner_was_enabled) EnableWindow(owner, FALSE);

    ShowWindow(hw, SW_SHOW);
    UpdateWindow(hw);

    // Pre-focus the first edit control (ID 101 for two-field dialogs, 1 for
    // single-field ones).
    HWND firstEdit = GetDlgItem(hw, 101);
    if (!firstEdit) firstEdit = GetDlgItem(hw, 1);
    if (firstEdit) SetFocus(firstEdit);

    MSG msg = {};
    bool got_quit = false;
    while (IsWindow(hw)) {
        const BOOL rc = GetMessageW(&msg, nullptr, 0, 0);
        if (rc <= 0) {
            got_quit = (rc == 0);
            break;
        }
        if (!IsWindow(hw)) break;
        if (!IsDialogMessageW(hw, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!IsWindow(hw)) break;
    }

    if (owner_was_enabled && IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    if (got_quit) PostQuitMessage(static_cast<int>(msg.wParam));
}

// Simple input dialog (single text field).
// Returns std::nullopt on Cancel.
static std::optional<std::string> PromptInput(HWND owner,
                                               const wchar_t* title,
                                               const wchar_t* prompt,
                                               const std::string& initial = {})
{
    struct State {
        const wchar_t* prompt;
        std::string    initial;
        std::optional<std::string> result;
    };
    auto* s = new State{ prompt, initial, std::nullopt };

    static bool reg = false;
    static constexpr wchar_t kCls[] = L"AgentPromptInputDlg";
    if (!reg) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = [](HWND hw, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
            State* st = reinterpret_cast<State*>(GetWindowLongPtrW(hw, GWLP_USERDATA));
            if (msg == WM_CREATE) {
                auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
                st = reinterpret_cast<State*>(cs->lpCreateParams);
                SetWindowLongPtrW(hw, GWLP_USERDATA, (LONG_PTR)st);
                HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                HWND lbl = CreateWindowExW(0, L"STATIC", st->prompt,
                    WS_CHILD|WS_VISIBLE, 10, 10, 300, 16,
                    hw, nullptr, nullptr, nullptr);
                SendMessageW(lbl, WM_SETFONT, (WPARAM)f, TRUE);
                HWND edt = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                    Utf8ToWide(st->initial).c_str(),
                    WS_CHILD|WS_VISIBLE|WS_TABSTOP,
                    10, 30, 300, 22, hw,
                    reinterpret_cast<HMENU>(1), nullptr, nullptr);
                SendMessageW(edt, WM_SETFONT, (WPARAM)f, TRUE);
                HWND btnOk = CreateWindowExW(0, L"BUTTON", L"OK",
                    WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
                    120, 62, 80, 24, hw,
                    reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
                SendMessageW(btnOk, WM_SETFONT, (WPARAM)f, TRUE);
                HWND btnCan = CreateWindowExW(0, L"BUTTON", L"Cancel",
                    WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
                    210, 62, 80, 24, hw,
                    reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
                SendMessageW(btnCan, WM_SETFONT, (WPARAM)f, TRUE);
                return 0;
            }
            if (!st) return DefWindowProcW(hw, msg, wp, lp);
            if (msg == WM_COMMAND) {
                int id = LOWORD(wp);
                if (id == IDOK) {
                    wchar_t buf[512] = {};
                    GetDlgItemTextW(hw, 1, buf, 512);
                    st->result = WideToUtf8(buf);
                    DestroyWindow(hw);
                } else if (id == IDCANCEL) {
                    DestroyWindow(hw);
                }
                return 0;
            }
            if (msg == WM_CLOSE)   { DestroyWindow(hw); return 0; }
            if (msg == WM_DESTROY) { return 0; }
            return DefWindowProcW(hw, msg, wp, lp);
        };
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
        wc.lpszClassName = kCls;
        RegisterClassExW(&wc);
        reg = true;
    }

    RECT or_ = {};
    if (owner) GetWindowRect(owner, &or_);
    int ox = or_.left + (or_.right - or_.left - 330) / 2;
    int oy = or_.top  + (or_.bottom - or_.top  - 105) / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;

    HWND hw = CreateWindowExW(WS_EX_DLGMODALFRAME, kCls, title,
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
        ox, oy, 330, 105, owner, nullptr, GetModuleHandleW(nullptr), s);

    RunModalLoop(hw, owner);

    auto result = s->result;
    delete s;
    return result;
}

// Single password-field dialog — used for simple confirmation prompts.
static std::optional<std::string> PromptPassword(HWND owner,
                                                   const wchar_t* title,
                                                   const wchar_t* prompt)
{
    struct State {
        const wchar_t* prompt;
        std::optional<std::string> result;
    };
    auto* s = new State{ prompt, std::nullopt };

    static bool reg = false;
    static constexpr wchar_t kCls[] = L"AgentPasswordInputDlg";
    if (!reg) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = [](HWND hw, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
            State* st = (State*)GetWindowLongPtrW(hw, GWLP_USERDATA);
            if (msg == WM_CREATE) {
                auto* cs = (CREATESTRUCTW*)lp;
                st = (State*)cs->lpCreateParams;
                SetWindowLongPtrW(hw, GWLP_USERDATA, (LONG_PTR)st);
                HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                HWND lbl = CreateWindowExW(0, L"STATIC", st->prompt,
                    WS_CHILD|WS_VISIBLE, 10, 10, 300, 16,
                    hw, nullptr, nullptr, nullptr);
                SendMessageW(lbl, WM_SETFONT, (WPARAM)f, TRUE);
                HWND edt = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_PASSWORD,
                    10, 30, 300, 22, hw, (HMENU)1, nullptr, nullptr);
                SendMessageW(edt, WM_SETFONT, (WPARAM)f, TRUE);
                HWND btnOk = CreateWindowExW(0, L"BUTTON", L"OK",
                    WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
                    120, 62, 80, 24, hw, (HMENU)IDOK, nullptr, nullptr);
                SendMessageW(btnOk, WM_SETFONT, (WPARAM)f, TRUE);
                HWND btnCan = CreateWindowExW(0, L"BUTTON", L"Cancel",
                    WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
                    210, 62, 80, 24, hw, (HMENU)IDCANCEL, nullptr, nullptr);
                SendMessageW(btnCan, WM_SETFONT, (WPARAM)f, TRUE);
                return 0;
            }
            if (!st) return DefWindowProcW(hw, msg, wp, lp);
            if (msg == WM_COMMAND) {
                int id = LOWORD(wp);
                if (id == IDOK) {
                    wchar_t buf[512] = {};
                    GetDlgItemTextW(hw, 1, buf, 512);
                    st->result = WideToUtf8(buf);
                    SecureZeroMemory(buf, sizeof(buf));
                    DestroyWindow(hw);
                } else if (id == IDCANCEL) {
                    DestroyWindow(hw);
                }
                return 0;
            }
            if (msg == WM_CLOSE)   { DestroyWindow(hw); return 0; }
            if (msg == WM_DESTROY) { return 0; }
            return DefWindowProcW(hw, msg, wp, lp);
        };
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
        wc.lpszClassName = kCls;
        RegisterClassExW(&wc);
        reg = true;
    }

    RECT or_ = {};
    if (owner) GetWindowRect(owner, &or_);
    int ox = or_.left + (or_.right  - or_.left - 330) / 2;
    int oy = or_.top  + (or_.bottom - or_.top  - 105) / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;

    HWND hw = CreateWindowExW(WS_EX_DLGMODALFRAME, kCls, title,
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
        ox, oy, 330, 105, owner, nullptr, GetModuleHandleW(nullptr), s);

    RunModalLoop(hw, owner);

    auto result = s->result;
    delete s;
    return result;
}

// Two-field password-reset dialog: "New password" + "Confirm password".
// Validates minimum length (8) and that both fields match before accepting.
// Returns std::nullopt on Cancel.
static std::optional<std::string> PromptResetPassword(HWND owner)
{
    struct State {
        std::optional<std::string> result;
    };
    auto* s = new State{ std::nullopt };

    static bool reg = false;
    static constexpr wchar_t kCls[] = L"AgentResetPasswordDlg";
    if (!reg) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = [](HWND hw, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
            State* st = (State*)GetWindowLongPtrW(hw, GWLP_USERDATA);
            HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            if (msg == WM_CREATE) {
                auto* cs = (CREATESTRUCTW*)lp;
                st = (State*)cs->lpCreateParams;
                SetWindowLongPtrW(hw, GWLP_USERDATA, (LONG_PTR)st);

                auto AddRow = [&](int y, int id, const wchar_t* label) {
                    HWND lbl = CreateWindowExW(0, L"STATIC", label,
                        WS_CHILD|WS_VISIBLE|SS_RIGHT, 10, y + 3, 120, 16,
                        hw, nullptr, nullptr, nullptr);
                    SendMessageW(lbl, WM_SETFONT, (WPARAM)f, TRUE);
                    HWND edt = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                        WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_PASSWORD,
                        138, y, 168, 22, hw, (HMENU)(INT_PTR)id, nullptr, nullptr);
                    SendMessageW(edt, WM_SETFONT, (WPARAM)f, TRUE);
                };
                AddRow(14,  101, L"New password:");
                AddRow(46,  102, L"Confirm password:");

                // Policy hint
                HWND hint = CreateWindowExW(0, L"STATIC",
                    L"At least 8 characters. Both fields must match.",
                    WS_CHILD|WS_VISIBLE|SS_LEFT,
                    10, 80, 310, 16, hw, nullptr, nullptr, nullptr);
                SendMessageW(hint, WM_SETFONT, (WPARAM)f, TRUE);

                HWND btnOk = CreateWindowExW(0, L"BUTTON", L"OK",
                    WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
                    80, 106, 80, 26, hw, (HMENU)IDOK, nullptr, nullptr);
                SendMessageW(btnOk, WM_SETFONT, (WPARAM)f, TRUE);
                HWND btnCan = CreateWindowExW(0, L"BUTTON", L"Cancel",
                    WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
                    170, 106, 80, 26, hw, (HMENU)IDCANCEL, nullptr, nullptr);
                SendMessageW(btnCan, WM_SETFONT, (WPARAM)f, TRUE);
                return 0;
            }
            if (!st) return DefWindowProcW(hw, msg, wp, lp);
            if (msg == WM_COMMAND) {
                int id = LOWORD(wp);
                if (id == IDOK) {
                    wchar_t p1[256] = {}, p2[256] = {};
                    GetDlgItemTextW(hw, 101, p1, 256);
                    GetDlgItemTextW(hw, 102, p2, 256);
                    if (wcslen(p1) < 8) {
                        MessageBoxW(hw,
                            L"Password must be at least 8 characters.",
                            L"Validation", MB_OK|MB_ICONWARNING);
                        SetFocus(GetDlgItem(hw, 101));
                        return 0;
                    }
                    if (wcscmp(p1, p2) != 0) {
                        MessageBoxW(hw,
                            L"The passwords do not match. Please try again.",
                            L"Validation", MB_OK|MB_ICONWARNING);
                        SetDlgItemTextW(hw, 101, L"");
                        SetDlgItemTextW(hw, 102, L"");
                        SecureZeroMemory(p1, sizeof(p1));
                        SecureZeroMemory(p2, sizeof(p2));
                        SetFocus(GetDlgItem(hw, 101));
                        return 0;
                    }
                    st->result = WideToUtf8(p1);
                    SecureZeroMemory(p1, sizeof(p1));
                    SecureZeroMemory(p2, sizeof(p2));
                    DestroyWindow(hw);
                } else if (id == IDCANCEL) {
                    DestroyWindow(hw);
                }
                return 0;
            }
            if (msg == WM_CLOSE)   { DestroyWindow(hw); return 0; }
            if (msg == WM_DESTROY) { return 0; }
            return DefWindowProcW(hw, msg, wp, lp);
        };
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
        wc.lpszClassName = kCls;
        RegisterClassExW(&wc);
        reg = true;
    }

    RECT or_ = {};
    if (owner) GetWindowRect(owner, &or_);
    // Client area needed: buttons at y=106+26=132, + 14px bottom padding = 146.
    // Caption + borders ≈ 30px → total window height = 176.
    constexpr int kW = 340, kH = 176;
    int ox = or_.left + (or_.right  - or_.left - kW) / 2;
    int oy = or_.top  + (or_.bottom - or_.top  - kH) / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;

    HWND hw = CreateWindowExW(WS_EX_DLGMODALFRAME, kCls, L"Reset Password",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
        ox, oy, kW, kH, owner, nullptr, GetModuleHandleW(nullptr), s);

    RunModalLoop(hw, owner);

    auto result = s->result;
    delete s;
    return result;
}

// Folder browser
static std::string BrowseForFolder(HWND owner, const std::string& initial)
{
    wchar_t buf[MAX_PATH] = {};
    BROWSEINFOW bi = {};
    bi.hwndOwner      = owner;
    bi.pszDisplayName = buf;
    bi.lpszTitle      = L"Select base folder";
    bi.ulFlags        = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;

    std::wstring init_w = Utf8ToWide(initial);
    if (!init_w.empty()) {
        bi.lParam = (LPARAM)init_w.c_str();
        bi.lpfn   = [](HWND hw, UINT msg, LPARAM, LPARAM data) -> int {
            if (msg == BFFM_INITIALIZED && data)
                SendMessageW(hw, BFFM_SETSELECTIONW, TRUE, data);
            return 0;
        };
    }

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return {};
    wchar_t path[MAX_PATH] = {};
    SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    return WideToUtf8(path);
}

// ── Control IDs ───────────────────────────────────────────────────────────────
constexpr wchar_t kAdminClassName[] = L"AgentAdminConfigWindow";

enum ControlId : int {
    kTabCtrl          = 8100,

    // Users tab
    kUserList         = 8110,
    kUserAdd          = 8111,
    kUserEdit         = 8112,
    kUserDelete       = 8113,
    kUserToggle       = 8114,   // Enable / Disable
    kUserResetPwd     = 8115,
    kUserForceLogout  = 8116,

    // Groups tab
    kGroupList        = 8120,
    kGroupAdd         = 8121,
    kGroupEdit        = 8122,
    kGroupDelete      = 8123,
    kGroupNameEdit    = 8124,
    kGroupUserCheckList = 8125,

    // Bindings tab
    kProjectList      = 8130,
    kBindGroupList    = 8131,   // CheckListBox
    kFolderCheck      = 8132,
    kFolderEdit       = 8133,
    kFolderBrowse     = 8134,
    kFolderLabel      = 8135,

    // Footer
    kCloseBtn         = IDCANCEL,
};

// ── Dialog state ──────────────────────────────────────────────────────────────
struct AdminState {
    WebServer*    server;
    WebUserStore* user_store;
    AppStorage*   storage;

    bool changed = false;

    // Tab panels (child HWNDs we create manually under the tab area)
    HWND tab_ctrl    = nullptr;
    HWND panel_users = nullptr;
    HWND panel_groups= nullptr;
    HWND panel_binds = nullptr;

    // Cached data (refreshed each time a tab is shown)
    std::vector<WebUser>           users;
    std::vector<WebGroup>          groups;
    std::vector<WebProjectBinding> bindings;
    std::vector<ProjectRecord>     projects;

    std::string displayed_binding_project_id;
    int active_tab = 0;
};

// ── Forward declarations ──────────────────────────────────────────────────────
static void RefreshUserList(HWND dlg, AdminState* st);
static void RefreshGroupList(HWND dlg, AdminState* st);
static void RefreshGroupEditPanel(HWND dlg, AdminState* st);
static void RefreshProjectList(HWND dlg, AdminState* st);
static void RefreshBindGroups(HWND dlg, AdminState* st);
static void ShowTab(HWND dlg, AdminState* st, int tab);

// ── Layout helpers ────────────────────────────────────────────────────────────
constexpr int kDlgW  = 700;
constexpr int kDlgH  = 600;
constexpr int kTabT  = 40;   // y where tab control starts
constexpr int kTabH  = kDlgH - kTabT - 70; // tab control height
constexpr int kPanL  = 8;    // panel content left margin (relative to panel)
constexpr int kPanT  = 6;    // panel content top margin
constexpr int kBtnW  = 90;
constexpr int kBtnH  = 22;

// ── User list helpers ─────────────────────────────────────────────────────────

static void RefreshUserList(HWND dlg, AdminState* st)
{
    HWND lv = GetDlgItem(st->panel_users, kUserList);
    if (!lv) return;

    st->users = st->user_store->GetUsers();

    ListView_DeleteAllItems(lv);

    for (int i = 0; i < (int)st->users.size(); ++i) {
        const auto& u = st->users[i];
        LVITEMW lvi = {};
        lvi.mask    = LVIF_TEXT;
        lvi.iItem   = i;
        lvi.iSubItem = 0;
        std::wstring wun = Utf8ToWide(u.username);
        lvi.pszText = const_cast<LPWSTR>(wun.c_str());
        ListView_InsertItem(lv, &lvi);

        std::wstring wdn = Utf8ToWide(u.display_name);
        ListView_SetItemText(lv, i, 1, const_cast<LPWSTR>(wdn.c_str()));
        std::wstring wen = Utf8ToWide(u.email);
        ListView_SetItemText(lv, i, 2, const_cast<LPWSTR>(wen.c_str()));
        ListView_SetItemText(lv, i, 3, u.enabled ? (LPWSTR)L"Yes" : (LPWSTR)L"No");
        std::wstring wll = Utf8ToWide(u.last_login_at.empty() ? "Never" : u.last_login_at);
        ListView_SetItemText(lv, i, 4, const_cast<LPWSTR>(wll.c_str()));
    }
}

// Returns the selected user index in st->users, or -1.
static int GetSelectedUser(AdminState* st)
{
    HWND lv = GetDlgItem(st->panel_users, kUserList);
    if (!lv) return -1;
    return ListView_GetNextItem(lv, -1, LVNI_SELECTED);
}

// ── Group/member helpers ──────────────────────────────────────────────────────

static void RefreshGroupList(HWND dlg, AdminState* st)
{
    (void)dlg;
    HWND lb = GetDlgItem(st->panel_groups, kGroupList);
    if (!lb) return;

    st->groups = st->user_store->GetGroups();
    int sel = ListBox_GetCurSel(lb);

    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    for (auto& g : st->groups) {
        std::string label = g.name + " (" + std::to_string(g.user_ids.size()) + ")";
        std::wstring wn = Utf8ToWide(label);
        ListBox_AddString(lb, wn.c_str());
    }
    if (!st->groups.empty()) {
        if (sel < 0 || sel >= (int)st->groups.size()) sel = 0;
        ListBox_SetCurSel(lb, sel);
    }
    RefreshGroupEditPanel(dlg, st);
}

static void RefreshGroupEditPanel(HWND /*dlg*/, AdminState* st)
{
    HWND nameEdit = GetDlgItem(st->panel_groups, kGroupNameEdit);
    HWND clb = GetDlgItem(st->panel_groups, kGroupUserCheckList);
    HWND lb_grp = GetDlgItem(st->panel_groups, kGroupList);
    if (!nameEdit || !clb || !lb_grp) return;

    // Load fresh data
    st->users = st->user_store->GetUsers();
    st->groups = st->user_store->GetGroups();

    int gsel = ListBox_GetCurSel(lb_grp);

    // Set group name or clear
    if (gsel >= 0 && gsel < (int)st->groups.size()) {
        SetWindowTextW(nameEdit, Utf8ToWide(st->groups[gsel].name).c_str());
    } else {
        SetWindowTextW(nameEdit, L"");
    }

    // Populate user checklist
    SendMessageW(clb, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < st->users.size(); ++i) {
        const auto& u = st->users[i];
        std::wstring wn = Utf8ToWide(u.username);
        int idx = ListBox_AddString(clb, wn.c_str());

        // Check if user is a member of selected group
        bool is_member = false;
        if (gsel >= 0 && gsel < (int)st->groups.size()) {
            for (const auto& uid : st->groups[gsel].user_ids) {
                if (uid == u.id) { is_member = true; break; }
            }
        }
        SendMessageW(clb, LB_SETITEMDATA, idx, is_member ? 1 : 0);
    }
}

// ── Binding helpers ───────────────────────────────────────────────────────────

static void RefreshProjectList(HWND /*dlg*/, AdminState* st)
{
    HWND lb = GetDlgItem(st->panel_binds, kProjectList);
    if (!lb) return;

    st->projects = st->storage->LoadProjects();
    int sel = ListBox_GetCurSel(lb);

    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    for (auto& pr : st->projects) {
        std::wstring wn = Utf8ToWide(pr.info.name.empty() ? pr.info.id : pr.info.name);
        ListBox_AddString(lb, wn.c_str());
    }
    if (!st->projects.empty()) {
        if (sel < 0 || sel >= (int)st->projects.size()) sel = 0;
        ListBox_SetCurSel(lb, sel);
    }
    RefreshBindGroups(nullptr, st);
}

static void RefreshBindGroups(HWND /*dlg*/, AdminState* st)
{
    HWND lb_proj  = GetDlgItem(st->panel_binds, kProjectList);
    HWND clb      = GetDlgItem(st->panel_binds, kBindGroupList);
    HWND chk      = GetDlgItem(st->panel_binds, kFolderCheck);
    HWND edt      = GetDlgItem(st->panel_binds, kFolderEdit);
    if (!lb_proj || !clb) return;

    st->groups   = st->user_store->GetGroups();
    st->bindings = st->user_store->GetBindings();

    // Clear CheckListBox
    while (ListBox_GetCount(clb) > 0)
        SendMessageW(clb, LB_DELETESTRING, 0, 0);

    int psel = ListBox_GetCurSel(lb_proj);
    if (psel < 0 || psel >= (int)st->projects.size()) {
        st->displayed_binding_project_id.clear();
        if (chk) EnableWindow(chk, FALSE);
        if (edt) EnableWindow(edt, FALSE);
        return;
    }

    const std::string& pid = st->projects[psel].info.id;
    st->displayed_binding_project_id = pid;

    // Find binding
    const WebProjectBinding* pb = nullptr;
    for (auto& b : st->bindings)
        if (b.project_id == pid) { pb = &b; break; }

    // Populate CheckListBox with all groups; check those in binding
    for (int i = 0; i < (int)st->groups.size(); ++i) {
        std::wstring wn = Utf8ToWide(st->groups[i].name);
        SendMessageW(clb, LB_ADDSTRING, 0, (LPARAM)wn.c_str());

        bool checked = false;
        if (pb) {
            for (auto& gid : pb->group_ids)
                if (gid == st->groups[i].id) { checked = true; break; }
        }
        // Set check state via LB_SETITEMDATA trick; we use a CheckListBox via
        // CHECKLISTBOX style — but Win32 has no built-in checked listbox.
        // We use owner-draw workaround: store check state as item data.
        SendMessageW(clb, LB_SETITEMDATA, i, checked ? 1 : 0);
    }

    // Folder override
    bool folder_en = pb ? pb->user_project_folder_enabled : false;
    std::string folder_root = pb ? pb->user_project_folder_root : "";

    if (chk) {
        EnableWindow(chk, TRUE);
        CheckDlgButton(GetParent(chk), kFolderCheck, folder_en ? BST_CHECKED : BST_UNCHECKED);
        // The check is on the panel, not the main dlg
        Button_SetCheck(chk, folder_en ? BST_CHECKED : BST_UNCHECKED);
    }
    if (edt) {
        EnableWindow(edt, folder_en);
        SetWindowTextW(edt, Utf8ToWide(folder_root).c_str());
    }
    HWND btn = GetDlgItem(st->panel_binds, kFolderBrowse);
    if (btn) EnableWindow(btn, folder_en);
}

// Save group checks back to user_store for the project currently displayed.
static void SaveBindGroups(AdminState* st)
{
    HWND clb     = GetDlgItem(st->panel_binds, kBindGroupList);
    HWND chk     = GetDlgItem(st->panel_binds, kFolderCheck);
    HWND edt     = GetDlgItem(st->panel_binds, kFolderEdit);
    if (!clb || st->displayed_binding_project_id.empty()) return;

    WebProjectBinding b;
    b.project_id = st->displayed_binding_project_id;

    int cnt = ListBox_GetCount(clb);
    for (int i = 0; i < cnt && i < (int)st->groups.size(); ++i) {
        LRESULT checked = SendMessageW(clb, LB_GETITEMDATA, i, 0);
        if (checked) b.group_ids.push_back(st->groups[i].id);
    }

    if (chk) b.user_project_folder_enabled = (Button_GetCheck(chk) == BST_CHECKED);

    if (edt) {
        wchar_t buf[MAX_PATH] = {};
        GetWindowTextW(edt, buf, MAX_PATH);
        b.user_project_folder_root = WideToUtf8(buf);
    }

    st->user_store->SetBinding(b);
    st->changed = true;
}

// ── Tab panel creation ────────────────────────────────────────────────────────

static HWND CreateAdminPanel(HWND dlg, int left, int top, int w, int h)
{
    static bool reg = false;
    static constexpr wchar_t kCls[] = L"AgentAdminPanel";
    if (!reg) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = [](HWND hw, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
            HWND parent = reinterpret_cast<HWND>(GetWindowLongPtrW(hw, GWLP_USERDATA));
            if (msg == WM_COMMAND || msg == WM_NOTIFY ||
                msg == WM_DRAWITEM || msg == WM_MEASUREITEM) {
                if (parent) SendMessageW(parent, msg, wp, lp);
                return 0;
            }
            return DefWindowProcW(hw, msg, wp, lp);
        };
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kCls;
        RegisterClassExW(&wc);
        reg = true;
    }
    HWND hw = CreateWindowExW(0, kCls, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        left, top, w, h, dlg, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(hw, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dlg));
    return hw;
}

static HWND CreateUsersPanel(HWND parent, RECT area, AdminState* st)
{
    HWND panel = CreateAdminPanel(parent,
        area.left, area.top, area.right - area.left, area.bottom - area.top);

    HFONT hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    int pw = area.right - area.left;
    int ph = area.bottom - area.top;

    // ListView
    HWND lv = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        kPanL, kPanT, pw - kPanL*2 - kBtnW - 8, ph - kPanT*2,
        panel, reinterpret_cast<HMENU>(kUserList), nullptr, nullptr);
    SendMessageW(lv, WM_SETFONT, (WPARAM)hf, TRUE);
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    // Columns
    struct { const wchar_t* name; int w; } cols[] = {
        { L"Username",   120 },
        { L"Display",    100 },
        { L"Email",      130 },
        { L"Enabled",     55 },
        { L"Last Login",  130 },
    };
    for (int i = 0; i < 5; ++i) {
        LVCOLUMNW lvc = {};
        lvc.mask    = LVCF_TEXT | LVCF_WIDTH;
        lvc.cx      = cols[i].w;
        lvc.pszText = const_cast<LPWSTR>(cols[i].name);
        ListView_InsertColumn(lv, i, &lvc);
    }

    // Buttons (right side)
    int bx = pw - kBtnW - kPanL;
    int by = kPanT;
    auto AddBtn = [&](int id, const wchar_t* label) {
        HWND b = CreateWindowExW(0, L"BUTTON", label,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            bx, by, kBtnW, kBtnH, panel,
            reinterpret_cast<HMENU>(id), nullptr, nullptr);
        SendMessageW(b, WM_SETFONT, (WPARAM)hf, TRUE);
        by += kBtnH + 4;
    };
    AddBtn(kUserAdd,       L"Add…");
    AddBtn(kUserEdit,      L"Edit…");
    AddBtn(kUserDelete,    L"Delete");
    by += 8;
    AddBtn(kUserToggle,    L"Enable/Disable");
    by += 8;
    AddBtn(kUserResetPwd,  L"Reset Password…");
    AddBtn(kUserForceLogout, L"Force Logout");

    return panel;
}

static HWND CreateGroupsPanel(HWND parent, RECT area, AdminState* st)
{
    HWND panel = CreateAdminPanel(parent,
        area.left, area.top, area.right - area.left, area.bottom - area.top);

    HFONT hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    int pw = area.right - area.left;
    int ph = area.bottom - area.top;

    // Left side: Groups list
    HWND lblGrp = CreateWindowExW(0, L"STATIC", L"Groups:",
        WS_CHILD|WS_VISIBLE, kPanL, kPanT, 80, 16,
        panel, nullptr, nullptr, nullptr);
    SendMessageW(lblGrp, WM_SETFONT, (WPARAM)hf, TRUE);

    int listW = pw / 3;
    HWND lbG = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY,
        kPanL, kPanT+18, listW, ph - kPanT*2 - 50,
        panel, reinterpret_cast<HMENU>(kGroupList), nullptr, nullptr);
    SendMessageW(lbG, WM_SETFONT, (WPARAM)hf, TRUE);

    // Group buttons below list
    int btnX = kPanL;
    int btnY = kPanT + 18 + (ph - kPanT*2 - 50) + 8;
    auto AddBtnG = [&](int id, const wchar_t* lbl) {
        HWND b = CreateWindowExW(0, L"BUTTON", lbl,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
            btnX, btnY, kBtnW, kBtnH, panel, (HMENU)(INT_PTR)id, nullptr, nullptr);
        SendMessageW(b, WM_SETFONT, (WPARAM)hf, TRUE);
        btnX += kBtnW + 4;
    };
    AddBtnG(kGroupAdd,    L"New…");
    AddBtnG(kGroupEdit,   L"Edit");
    AddBtnG(kGroupDelete, L"Delete");

    // Right side: Group edit panel
    int rpX = kPanL + listW + 16;
    int rpW = pw - rpX - kPanL;

    // Group name field
    HWND lblName = CreateWindowExW(0, L"STATIC", L"Group name:",
        WS_CHILD|WS_VISIBLE, rpX, kPanT, 100, 16,
        panel, nullptr, nullptr, nullptr);
    SendMessageW(lblName, WM_SETFONT, (WPARAM)hf, TRUE);

    HWND nameEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER,
        rpX, kPanT+18, rpW, 22,
        panel, reinterpret_cast<HMENU>(kGroupNameEdit), nullptr, nullptr);
    SendMessageW(nameEdit, WM_SETFONT, (WPARAM)hf, TRUE);

    // User assignment checklist
    HWND lblUsers = CreateWindowExW(0, L"STATIC", L"Members assigned (check users):",
        WS_CHILD|WS_VISIBLE, rpX, kPanT+48, rpW, 16,
        panel, nullptr, nullptr, nullptr);
    SendMessageW(lblUsers, WM_SETFONT, (WPARAM)hf, TRUE);

    HWND clb = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|
        LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,
        rpX, kPanT+66, rpW, ph - (kPanT + 66) - kPanT,
        panel, reinterpret_cast<HMENU>(kGroupUserCheckList), nullptr, nullptr);
    SendMessageW(clb, WM_SETFONT, (WPARAM)hf, TRUE);
    SendMessageW(clb, LB_SETITEMHEIGHT, 0, 18);

    (void)st;
    return panel;
}

static HWND CreateBindingsPanel(HWND parent, RECT area, AdminState* st)
{
    HWND panel = CreateAdminPanel(parent,
        area.left, area.top, area.right - area.left, area.bottom - area.top);

    HFONT hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    int pw = area.right - area.left;
    int ph = area.bottom - area.top;

    // Projects
    HWND lbl1 = CreateWindowExW(0, L"STATIC", L"Projects:",
        WS_CHILD|WS_VISIBLE, kPanL, kPanT, 120, 16,
        panel, nullptr, nullptr, nullptr);
    SendMessageW(lbl1, WM_SETFONT, (WPARAM)hf, TRUE);

    int listW = pw / 3;
    HWND lbP = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY,
        kPanL, kPanT+18, listW, ph - kPanT*2 - 80,
        panel, reinterpret_cast<HMENU>(kProjectList), nullptr, nullptr);
    SendMessageW(lbP, WM_SETFONT, (WPARAM)hf, TRUE);

    // Groups (CheckListBox via LBS_OWNERDRAWFIXED + item data)
    int gx = kPanL + listW + 8;
    HWND lbl2 = CreateWindowExW(0, L"STATIC", L"Allowed groups (click to toggle):",
        WS_CHILD|WS_VISIBLE, gx, kPanT, 200, 16,
        panel, nullptr, nullptr, nullptr);
    SendMessageW(lbl2, WM_SETFONT, (WPARAM)hf, TRUE);

    // We simulate a check list box by handling DBLCLK / SPACE to toggle item data.
    // The actual drawing of checkboxes is done by owner-draw.  For simplicity we
    // use a plain LBS_OWNERDRAWFIXED ListBox with checkbox images drawn in WM_DRAWITEM.
    HWND clb = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|
        LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,
        gx, kPanT+18, pw - gx - kPanL, ph - kPanT*2 - 80,
        panel, reinterpret_cast<HMENU>(kBindGroupList), nullptr, nullptr);
    SendMessageW(clb, WM_SETFONT, (WPARAM)hf, TRUE);
    // Fixed item height
    SendMessageW(clb, LB_SETITEMHEIGHT, 0, 18);

    // Per-user folder section
    int fy = ph - kPanT - 70;
    HWND chk = CreateWindowExW(0, L"BUTTON",
        L"Enable per-user project folder",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,
        kPanL, fy, 240, 20,
        panel, reinterpret_cast<HMENU>(kFolderCheck), nullptr, nullptr);
    SendMessageW(chk, WM_SETFONT, (WPARAM)hf, TRUE);

    fy += 26;
    HWND lbl3 = CreateWindowExW(0, L"STATIC", L"Base folder:",
        WS_CHILD|WS_VISIBLE|SS_RIGHT,
        kPanL, fy+3, 90, 16,
        panel, reinterpret_cast<HMENU>(kFolderLabel), nullptr, nullptr);
    SendMessageW(lbl3, WM_SETFONT, (WPARAM)hf, TRUE);

    HWND edt = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP,
        kPanL+96, fy, pw - kPanL*2 - 96 - kBtnW - 6, 20,
        panel, reinterpret_cast<HMENU>(kFolderEdit), nullptr, nullptr);
    SendMessageW(edt, WM_SETFONT, (WPARAM)hf, TRUE);

    HWND btn = CreateWindowExW(0, L"BUTTON", L"Browse…",
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
        pw - kPanL - kBtnW, fy, kBtnW, kBtnH,
        panel, reinterpret_cast<HMENU>(kFolderBrowse), nullptr, nullptr);
    SendMessageW(btn, WM_SETFONT, (WPARAM)hf, TRUE);

    (void)st;
    return panel;
}

// ── ShowTab ───────────────────────────────────────────────────────────────────
static void ShowTab(HWND dlg, AdminState* st, int tab)
{
    (void)dlg;
    st->active_tab = tab;

    ShowWindow(st->panel_users,  tab == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(st->panel_groups, tab == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(st->panel_binds,  tab == 2 ? SW_SHOW : SW_HIDE);

    if (tab == 0) RefreshUserList(dlg, st);
    if (tab == 1) RefreshGroupList(dlg, st);
    if (tab == 2) RefreshProjectList(dlg, st);
}

// ── User add/edit sub-dialog ──────────────────────────────────────────────────
struct UserEditState {
    WebUser  user;            // in/out
    bool     is_new = false;
    bool     ok     = false;
    std::string new_password; // only set for new users
};

static bool ShowUserEditDialog(HWND owner, UserEditState& ues)
{
    static bool reg = false;
    static constexpr wchar_t kCls[] = L"AgentUserEditDlg";
    if (!reg) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = [](HWND hw, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
            UserEditState* st = (UserEditState*)GetWindowLongPtrW(hw, GWLP_USERDATA);
            HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            if (msg == WM_CREATE) {
                auto* cs = (CREATESTRUCTW*)lp;
                st = (UserEditState*)cs->lpCreateParams;
                SetWindowLongPtrW(hw, GWLP_USERDATA, (LONG_PTR)st);

                auto Row = [&](int y, int id, const wchar_t* label, const std::string& val, DWORD es = 0) {
                    HWND lbl = CreateWindowExW(0, L"STATIC", label,
                        WS_CHILD|WS_VISIBLE|SS_RIGHT, 10, y+3, 110, 16,
                        hw, nullptr, nullptr, nullptr);
                    SendMessageW(lbl, WM_SETFONT, (WPARAM)f, TRUE);
                    HWND edt = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                        Utf8ToWide(val).c_str(),
                        WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER|es,
                        126, y, 220, 22, hw, (HMENU)(INT_PTR)id, nullptr, nullptr);
                    SendMessageW(edt, WM_SETFONT, (WPARAM)f, TRUE);
                };

                int y = 10;
                Row(y,  101, L"Username:",     st->user.username);    y += 30;
                Row(y,  102, L"Display name:", st->user.display_name); y += 30;
                Row(y,  103, L"Email:",        st->user.email);        y += 30;
                if (st->is_new) {
                    Row(y, 104, L"Password:", {}, ES_PASSWORD);        y += 30;
                }
                // Enabled checkbox
                HWND chk = CreateWindowExW(0, L"BUTTON", L"Account enabled",
                    WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,
                    126, y, 200, 20, hw, (HMENU)105, nullptr, nullptr);
                SendMessageW(chk, WM_SETFONT, (WPARAM)f, TRUE);
                Button_SetCheck(chk, st->user.enabled ? BST_CHECKED : BST_UNCHECKED);
                y += 30;
                // Force password reset
                HWND chk2 = CreateWindowExW(0, L"BUTTON", L"Force password reset on next login",
                    WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,
                    126, y, 240, 20, hw, (HMENU)106, nullptr, nullptr);
                SendMessageW(chk2, WM_SETFONT, (WPARAM)f, TRUE);
                Button_SetCheck(chk2,
                    st->user.force_password_reset ? BST_CHECKED : BST_UNCHECKED);
                y += 36;
                // OK / Cancel
                HWND btnOk = CreateWindowExW(0, L"BUTTON", L"OK",
                    WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
                    126, y, 80, 24, hw, (HMENU)IDOK, nullptr, nullptr);
                SendMessageW(btnOk, WM_SETFONT, (WPARAM)f, TRUE);
                HWND btnC = CreateWindowExW(0, L"BUTTON", L"Cancel",
                    WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
                    216, y, 80, 24, hw, (HMENU)IDCANCEL, nullptr, nullptr);
                SendMessageW(btnC, WM_SETFONT, (WPARAM)f, TRUE);
                return 0;
            }
            if (!st) return DefWindowProcW(hw, msg, wp, lp);
            if (msg == WM_COMMAND) {
                int id = LOWORD(wp);
                if (id == IDOK) {
                    wchar_t buf[512];
                    auto getText = [&](int cid) -> std::string {
                        GetDlgItemTextW(hw, cid, buf, 512);
                        return WideToUtf8(buf);
                    };
                    st->user.username     = getText(101);
                    st->user.display_name = getText(102);
                    st->user.email        = getText(103);
                    if (st->is_new) st->new_password = getText(104);
                    st->user.enabled = (Button_GetCheck(GetDlgItem(hw, 105)) == BST_CHECKED);
                    st->user.force_password_reset =
                        (Button_GetCheck(GetDlgItem(hw, 106)) == BST_CHECKED);
                    st->ok = true;
                    DestroyWindow(hw);
                } else if (id == IDCANCEL) {
                    DestroyWindow(hw);
                }
                return 0;
            }
            if (msg == WM_CLOSE) { DestroyWindow(hw); return 0; }
            if (msg == WM_DESTROY) return 0;
            return DefWindowProcW(hw, msg, wp, lp);
        };
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
        wc.lpszClassName = kCls;
        RegisterClassExW(&wc);
        reg = true;
    }

    RECT or_ = {};
    if (owner) GetWindowRect(owner, &or_);
    int h = ues.is_new ? 320 : 290;
    int ox = or_.left + (or_.right  - or_.left - 380) / 2;
    int oy = or_.top  + (or_.bottom - or_.top  - h)   / 2;

    HWND hw = CreateWindowExW(WS_EX_DLGMODALFRAME, kCls,
        ues.is_new ? L"Add User" : L"Edit User",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
        ox, oy, 380, h,
        owner, nullptr, GetModuleHandleW(nullptr), &ues);

    RunModalLoop(hw, owner);
    return ues.ok;
}

// ── New Group sub-dialog ──────────────────────────────────────────────────────
// Dedicated modal dialog for entering a new group name.
// Uses the shared HWND-sentinel modal loop. WM_DESTROY must not post WM_QUIT;
// that message is reserved for shutting down the main application loop.
// Returns the trimmed group name on OK, or nullopt on Cancel / empty.

struct NewGroupState {
    std::wstring name;
    bool ok = false;
};

static std::optional<std::string> ShowNewGroupDialog(HWND owner)
{
    static bool reg = false;
    static constexpr wchar_t kCls[] = L"AgentNewGroupDlg";
    if (!reg) {
        WNDCLASSEXW wc   = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = [](HWND hw, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
            NewGroupState* st =
                reinterpret_cast<NewGroupState*>(GetWindowLongPtrW(hw, GWLP_USERDATA));
            HFONT hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

            if (msg == WM_CREATE) {
                auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
                st = reinterpret_cast<NewGroupState*>(cs->lpCreateParams);
                SetWindowLongPtrW(hw, GWLP_USERDATA, (LONG_PTR)st);

                // Label
                HWND lbl = CreateWindowExW(0, L"STATIC", L"Group name:",
                    WS_CHILD|WS_VISIBLE|SS_RIGHT,
                    10, 17, 88, 16, hw, nullptr, nullptr, nullptr);
                SendMessageW(lbl, WM_SETFONT, (WPARAM)hf, TRUE);

                // Edit control
                HWND edt = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,
                    104, 13, 194, 22, hw, (HMENU)(INT_PTR)101, nullptr, nullptr);
                SendMessageW(edt, WM_SETFONT, (WPARAM)hf, TRUE);
                SetFocus(edt);

                // OK button
                HWND btnOk = CreateWindowExW(0, L"BUTTON", L"OK",
                    WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
                    104, 48, 80, 24, hw, (HMENU)(INT_PTR)IDOK, nullptr, nullptr);
                SendMessageW(btnOk, WM_SETFONT, (WPARAM)hf, TRUE);

                // Cancel button
                HWND btnCancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
                    WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
                    194, 48, 80, 24, hw, (HMENU)(INT_PTR)IDCANCEL, nullptr, nullptr);
                SendMessageW(btnCancel, WM_SETFONT, (WPARAM)hf, TRUE);

                return 0;
            }

            if (!st) return DefWindowProcW(hw, msg, wp, lp);

            if (msg == WM_COMMAND) {
                int cid = LOWORD(wp);
                if (cid == IDOK) {
                    wchar_t buf[256] = {};
                    GetDlgItemTextW(hw, 101, buf, 256);
                    // Trim leading/trailing whitespace
                    std::wstring raw(buf);
                    size_t s = raw.find_first_not_of(L" \t");
                    size_t e = raw.find_last_not_of(L" \t");
                    if (s == std::wstring::npos) {
                        MessageBoxW(hw, L"Group name cannot be empty.",
                                    L"New Group", MB_OK|MB_ICONWARNING);
                        SetFocus(GetDlgItem(hw, 101));
                        return 0;
                    }
                    st->name = raw.substr(s, e - s + 1);
                    st->ok   = true;
                    DestroyWindow(hw);
                } else if (cid == IDCANCEL) {
                    DestroyWindow(hw);
                }
                return 0;
            }
            if (msg == WM_CLOSE)   { DestroyWindow(hw); return 0; }
            if (msg == WM_DESTROY) { return 0; }
            return DefWindowProcW(hw, msg, wp, lp);
        };
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = kCls;
        RegisterClassExW(&wc);
        reg = true;
    }

    static constexpr int kW = 320, kH = 120;
    RECT or_ = {};
    if (owner) GetWindowRect(owner, &or_);
    int ox = or_.left + (or_.right  - or_.left - kW) / 2;
    int oy = or_.top  + (or_.bottom - or_.top  - kH) / 2;

    NewGroupState state{};
    HWND hw = CreateWindowExW(WS_EX_DLGMODALFRAME, kCls, L"New Group",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
        ox, oy, kW, kH,
        owner, nullptr, GetModuleHandleW(nullptr), &state);

    RunModalLoop(hw, owner);

    if (state.ok && !state.name.empty())
        return WideToUtf8(state.name);
    return std::nullopt;
}

// ── WndProc for the main admin dialog ────────────────────────────────────────

// Panel WndProc for the owner-draw CheckListBox (kBindGroupList)
static void DrawCheckItem(DRAWITEMSTRUCT* dis)
{
    if (!dis) return;
    if (dis->itemID == static_cast<UINT>(-1)) return;
    HWND lb = dis->hwndItem;
    bool checked = (SendMessageW(lb, LB_GETITEMDATA, dis->itemID, 0) != 0);
    bool selected = (dis->itemState & ODS_SELECTED) != 0;

    // Background
    COLORREF bg = selected ? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_WINDOW);
    HBRUSH bg_brush = CreateSolidBrush(bg);
    FillRect(dis->hDC, &dis->rcItem, bg_brush);
    DeleteObject(bg_brush);

    // Checkbox (drawn as a small framed square with a checkmark)
    RECT cr = dis->rcItem;
    cr.right = cr.left + 16;
    cr.top   = cr.top + (dis->rcItem.bottom - cr.top - 12) / 2;
    cr.bottom = cr.top + 12;
    cr.left  += 2;
    cr.right  = cr.left + 12;
    DrawFrameControl(dis->hDC, &cr, DFC_BUTTON,
        DFCS_BUTTONCHECK | (checked ? DFCS_CHECKED : 0));

    // Text
    wchar_t buf[256] = {};
    SendMessageW(lb, LB_GETTEXT, dis->itemID, (LPARAM)buf);
    COLORREF fg = selected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_WINDOWTEXT);
    SetBkColor(dis->hDC, bg);
    SetTextColor(dis->hDC, fg);
    RECT tr = dis->rcItem;
    tr.left += 18;
    DrawTextW(dis->hDC, buf, -1, &tr, DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    if (dis->itemState & ODS_FOCUS)
        DrawFocusRect(dis->hDC, &dis->rcItem);
}

static LRESULT CALLBACK AdminConfigProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    AdminState* st = reinterpret_cast<AdminState*>(
        GetWindowLongPtrW(dlg, GWLP_USERDATA));

    switch (msg) {

    case WM_CREATE: {
        auto* cs = (CREATESTRUCTW*)lp;
        st = (AdminState*)cs->lpCreateParams;
        SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)st);

        HFONT hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        // Tab control
        HWND tab = CreateWindowExW(0, WC_TABCONTROLW, L"",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_CLIPSIBLINGS,
            8, kTabT - 4, kDlgW - 16, kTabH + 24,
            dlg, reinterpret_cast<HMENU>(kTabCtrl), nullptr, nullptr);
        SendMessageW(tab, WM_SETFONT, (WPARAM)hf, TRUE);
        st->tab_ctrl = tab;

        struct { const wchar_t* name; } tabs[] = {
            { L"Users" }, { L"Groups" }, { L"Bindings" }
        };
        for (int i = 0; i < 3; ++i) {
            TCITEMW tci = {};
            tci.mask    = TCIF_TEXT;
            tci.pszText = const_cast<LPWSTR>(tabs[i].name);
            TabCtrl_InsertItem(tab, i, &tci);
        }

        // Panel area (inside tab control)
        RECT panelArea;
        GetClientRect(tab, &panelArea);
        TabCtrl_AdjustRect(tab, FALSE, &panelArea);
        // Offset to screen coords relative to dlg
        POINT pt = { panelArea.left, panelArea.top };
        MapWindowPoints(tab, dlg, &pt, 1);
        panelArea.left   = pt.x + 8;
        panelArea.top    = pt.y + kTabT;
        panelArea.right  = pt.x + (panelArea.right - panelArea.left) + 8 + (kDlgW - 16) - 4;
        panelArea.bottom = pt.y + kTabT + kTabH - 4;
        // Simpler: just compute absolute
        panelArea.left   = 12;
        panelArea.top    = kTabT + 24;
        panelArea.right  = kDlgW - 12;
        panelArea.bottom = kTabT + 24 + kTabH - 2;

        st->panel_users  = CreateUsersPanel(dlg, panelArea, st);
        st->panel_groups = CreateGroupsPanel(dlg, panelArea, st);
        st->panel_binds  = CreateBindingsPanel(dlg, panelArea, st);

        // Header label
        HWND hdrLbl = CreateWindowExW(0, L"STATIC",
            L"Web Server Administration",
            WS_CHILD|WS_VISIBLE|SS_LEFT,
            12, 10, 400, 22, dlg, nullptr, nullptr, nullptr);
        SendMessageW(hdrLbl, WM_SETFONT, (WPARAM)hf, TRUE);

        // Close button
        HWND btnClose = CreateWindowExW(0, L"BUTTON", L"Close",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,
            kDlgW/2 - 40, kDlgH - 36, 80, 24,
            dlg, reinterpret_cast<HMENU>(kCloseBtn), nullptr, nullptr);
        SendMessageW(btnClose, WM_SETFONT, (WPARAM)hf, TRUE);

        ShowTab(dlg, st, 0);
        return 0;
    }

    // WM_DRAWITEM for CheckListBoxes on Bindings and Groups panels
    case WM_DRAWITEM: {
        auto* dis = (DRAWITEMSTRUCT*)lp;
        if (dis && (dis->CtlID == kBindGroupList || dis->CtlID == kGroupUserCheckList))
            DrawCheckItem(dis);
        return TRUE;
    }

    case WM_NOTIFY: {
        auto* hdr = (NMHDR*)lp;
        if (hdr->idFrom == kTabCtrl && hdr->code == TCN_SELCHANGE) {
            int tab = TabCtrl_GetCurSel(st->tab_ctrl);
            ShowTab(dlg, st, tab);
        }
        return 0;
    }

    case WM_COMMAND: {
        int id  = LOWORD(wp);
        int ntf = HIWORD(wp);

        // ── Users tab ─────────────────────────────────────────────────────────
        if (id == kUserAdd) {
            UserEditState ues;
            ues.is_new = true;
            ues.user.enabled = true;
            if (ShowUserEditDialog(dlg, ues)) {
                if (!ues.new_password.empty()) {
                    if (st->user_store->AddUser(ues.user, ues.new_password))
                        st->changed = true;
                } else {
                    MessageBoxW(dlg, L"Password is required for new users.",
                                L"Validation", MB_OK|MB_ICONWARNING);
                }
                RefreshUserList(dlg, st);
            }
            return 0;
        }

        if (id == kUserEdit) {
            int idx = GetSelectedUser(st);
            if (idx < 0) return 0;
            UserEditState ues;
            ues.user   = st->users[idx];
            ues.is_new = false;
            if (ShowUserEditDialog(dlg, ues)) {
                if (st->user_store->UpdateUser(ues.user))
                    st->changed = true;
                RefreshUserList(dlg, st);
            }
            return 0;
        }

        if (id == kUserDelete) {
            int idx = GetSelectedUser(st);
            if (idx < 0) return 0;
            std::wstring msg = L"Delete user \"" +
                Utf8ToWide(st->users[idx].username) + L"\"?";
            if (MessageBoxW(dlg, msg.c_str(), L"Confirm", MB_YESNO|MB_ICONQUESTION) == IDYES) {
                st->user_store->DeleteUser(st->users[idx].id);
                st->changed = true;
                RefreshUserList(dlg, st);
            }
            return 0;
        }

        if (id == kUserToggle) {
            int idx = GetSelectedUser(st);
            if (idx < 0) return 0;
            WebUser u = st->users[idx];
            u.enabled = !u.enabled;
            st->user_store->UpdateUser(u);
            st->changed = true;
            RefreshUserList(dlg, st);
            return 0;
        }

        if (id == kUserResetPwd) {
            int idx = GetSelectedUser(st);
            if (idx < 0) return 0;
            auto pwd = PromptResetPassword(dlg);
            if (pwd) {
                st->user_store->SetPassword(st->users[idx].id, *pwd);
                // Mark user for forced password change on next login
                auto u_opt = st->user_store->FindUserById(st->users[idx].id);
                if (u_opt) {
                    WebUser u = *u_opt;
                    u.force_password_reset = true;
                    st->user_store->UpdateUser(u);
                }
                st->changed = true;
                MessageBoxW(dlg, L"Password has been reset. The user will be\n"
                                 L"required to change it on next login.",
                            L"Password Reset", MB_OK|MB_ICONINFORMATION);
            }
            return 0;
        }

        if (id == kUserForceLogout) {
            int idx = GetSelectedUser(st);
            if (idx < 0) return 0;
            if (st->server)
                st->server->ForceLogoutUser(st->users[idx].id);
            MessageBoxW(dlg, L"User has been logged out.", L"Force Logout",
                        MB_OK|MB_ICONINFORMATION);
            return 0;
        }

        // ── Groups tab ────────────────────────────────────────────────────────
        if (id == kGroupAdd) {
            auto name = ShowNewGroupDialog(dlg);
            if (name && !name->empty()) {
                WebGroup g;
                g.name = *name;
                st->user_store->AddGroup(g);
                st->changed = true;
                RefreshGroupList(dlg, st);
            }
            return 0;
        }

        if (id == kGroupDelete) {
            HWND lb = GetDlgItem(st->panel_groups, kGroupList);
            int gsel = lb ? ListBox_GetCurSel(lb) : -1;
            if (gsel < 0 || gsel >= (int)st->groups.size()) return 0;
            std::wstring msg2 = L"Delete group \"" +
                Utf8ToWide(st->groups[gsel].name) + L"\"?";
            if (MessageBoxW(dlg, msg2.c_str(), L"Confirm",
                            MB_YESNO|MB_ICONQUESTION) == IDYES) {
                st->user_store->DeleteGroup(st->groups[gsel].id);
                st->changed = true;
                RefreshGroupList(dlg, st);
            }
            return 0;
        }

        if (id == kGroupEdit) {
            HWND lb_grp = GetDlgItem(st->panel_groups, kGroupList);
            int gsel = lb_grp ? ListBox_GetCurSel(lb_grp) : -1;
            if (gsel < 0 || gsel >= (int)st->groups.size()) return 0;
            HWND name_edit = GetDlgItem(st->panel_groups, kGroupNameEdit);
            if (name_edit) {
                SetFocus(name_edit);
                SendMessageW(name_edit, EM_SETSEL, 0, -1);
            }
            return 0;
        }

        if (id == kGroupList && ntf == LBN_SELCHANGE) {
            RefreshGroupEditPanel(dlg, st);
            return 0;
        }

        if (id == kGroupNameEdit && ntf == EN_KILLFOCUS) {
            HWND lb_grp = GetDlgItem(st->panel_groups, kGroupList);
            int gsel = lb_grp ? ListBox_GetCurSel(lb_grp) : -1;
            if (gsel >= 0 && gsel < (int)st->groups.size()) {
                wchar_t buf[256] = {};
                GetWindowTextW(GetDlgItem(st->panel_groups, kGroupNameEdit), buf, 256);
                std::string new_name = WideToUtf8(buf);
                if (!new_name.empty() && new_name != st->groups[gsel].name) {
                    WebGroup updated = st->groups[gsel];
                    updated.name = new_name;
                    st->user_store->UpdateGroup(updated);
                    st->changed = true;
                    RefreshGroupList(dlg, st);
                }
            }
            return 0;
        }

        if (id == kGroupUserCheckList && ntf == LBN_SELCHANGE) {
            HWND clb = GetDlgItem(st->panel_groups, kGroupUserCheckList);
            HWND lb_grp = GetDlgItem(st->panel_groups, kGroupList);
            int idx = ListBox_GetCurSel(clb);
            int gsel = lb_grp ? ListBox_GetCurSel(lb_grp) : -1;
            if (idx < 0 || gsel < 0 || gsel >= (int)st->groups.size()) return 0;
            if (idx >= (int)st->users.size()) return 0;

            WebGroup updated = st->groups[gsel];
            const std::string& uid = st->users[idx].id;
            auto it = std::find(updated.user_ids.begin(), updated.user_ids.end(), uid);
            if (it != updated.user_ids.end()) {
                updated.user_ids.erase(it);
            } else {
                updated.user_ids.push_back(uid);
            }
            st->user_store->UpdateGroup(updated);
            st->groups[gsel] = updated;
            st->changed = true;
            SendMessageW(clb, LB_SETITEMDATA, idx,
                (std::find(updated.user_ids.begin(), updated.user_ids.end(), uid) != updated.user_ids.end()) ? 1 : 0);
            InvalidateRect(clb, nullptr, FALSE);

            HWND lb = GetDlgItem(st->panel_groups, kGroupList);
            if (lb) {
                std::string label = updated.name + " (" + std::to_string(updated.user_ids.size()) + ")";
                std::wstring wlabel = Utf8ToWide(label);
                SendMessageW(lb, LB_DELETESTRING, gsel, 0);
                SendMessageW(lb, LB_INSERTSTRING, gsel, reinterpret_cast<LPARAM>(wlabel.c_str()));
                ListBox_SetCurSel(lb, gsel);
            }
            return 0;
        }

        // ── Bindings tab ──────────────────────────────────────────────────────
        if (id == kProjectList && ntf == LBN_SELCHANGE) {
            // Save current before switching
            SaveBindGroups(st);
            RefreshBindGroups(dlg, st);
            return 0;
        }

        if (id == kBindGroupList && ntf == LBN_SELCHANGE) {
            // Toggle check state on click
            HWND clb = GetDlgItem(st->panel_binds, kBindGroupList);
            if (clb) {
                int idx = ListBox_GetCurSel(clb);
                if (idx >= 0) {
                    LRESULT cur = SendMessageW(clb, LB_GETITEMDATA, idx, 0);
                    SendMessageW(clb, LB_SETITEMDATA, idx, cur ? 0 : 1);
                    InvalidateRect(clb, nullptr, TRUE);
                    SaveBindGroups(st);
                }
            }
            return 0;
        }

        if (id == kFolderCheck) {
            HWND chk = GetDlgItem(st->panel_binds, kFolderCheck);
            bool en = chk ? (Button_GetCheck(chk) == BST_CHECKED) : false;
            HWND edt = GetDlgItem(st->panel_binds, kFolderEdit);
            HWND btn = GetDlgItem(st->panel_binds, kFolderBrowse);
            if (edt) EnableWindow(edt, en);
            if (btn) EnableWindow(btn, en);
            SaveBindGroups(st);
            return 0;
        }

        if (id == kFolderEdit && (ntf == EN_KILLFOCUS || ntf == EN_CHANGE)) {
            SaveBindGroups(st);
            return 0;
        }

        if (id == kFolderBrowse) {
            HWND edt = GetDlgItem(st->panel_binds, kFolderEdit);
            std::string cur;
            if (edt) {
                wchar_t buf[MAX_PATH] = {};
                GetWindowTextW(edt, buf, MAX_PATH);
                cur = WideToUtf8(buf);
            }
            std::string chosen = BrowseForFolder(dlg, cur);
            if (!chosen.empty() && edt) {
                SetWindowTextW(edt, Utf8ToWide(chosen).c_str());
                SaveBindGroups(st);
            }
            return 0;
        }

        if (id == kCloseBtn || id == IDCANCEL) {
            // Save any pending binding changes before closing
            if (st->active_tab == 2) SaveBindGroups(st);
            DestroyWindow(dlg);
            return 0;
        }

        return 0;
    }

    case WM_CLOSE:
        if (st && st->active_tab == 2) SaveBindGroups(st);
        DestroyWindow(dlg);
        return 0;

    case WM_DESTROY:
        return 0;
    }

    return DefWindowProcW(dlg, msg, wp, lp);
}

} // namespace

// ── Public entry point ────────────────────────────────────────────────────────
bool ShowAdminConfigDialog(HWND          owner,
                           WebServer*    server,
                           WebUserStore* user_store,
                           AppStorage*   storage)
{
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = AdminConfigProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kAdminClassName;
        RegisterClassExW(&wc);
        reg = true;
    }

    AdminState state;
    state.server     = server;
    state.user_store = user_store;
    state.storage    = storage;

    constexpr DWORD kStyle = WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU;
    constexpr DWORD kExStyle = WS_EX_DLGMODALFRAME;
    RECT window_rect = {0, 0, kDlgW, kDlgH};
    AdjustWindowRectEx(&window_rect, kStyle, FALSE, kExStyle);
    const int win_w = window_rect.right - window_rect.left;
    const int win_h = window_rect.bottom - window_rect.top;

    RECT or_ = {};
    if (owner) GetWindowRect(owner, &or_);
    int ox = or_.left + (or_.right  - or_.left - win_w) / 2;
    int oy = or_.top  + (or_.bottom - or_.top  - win_h) / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;

    HWND hw = CreateWindowExW(kExStyle, kAdminClassName,
        L"Web Server Administration",
        kStyle,
        ox, oy, win_w, win_h,
        owner, nullptr, GetModuleHandleW(nullptr), &state);

    RunModalLoop(hw, owner);

    return state.changed;
}
