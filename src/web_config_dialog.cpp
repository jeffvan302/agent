// ──────────────────────────────────────────────────────────────────────────────
// web_config_dialog.cpp — Win32 Web Config dialog implementation.
//
// Control ID range: 8000–8099 (leaves 8100+ for admin_config_dialog).
// ──────────────────────────────────────────────────────────────────────────────

#include "web_config_dialog.h"
#include "util.h"

#include <commctrl.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

// ── Control IDs ──────────────────────────────────────────────────────────────
namespace {

constexpr wchar_t kClassName[] = L"AgentWebConfigWindow";

enum ControlId : int {
    // Status bar
    kStatusLabel    = 8000,
    kStartStopBtn   = 8001,

    // Network
    kPortLabel      = 8010,
    kPortEdit       = 8011,
    kBindLabel      = 8012,
    kBindEdit       = 8013,
    kBaseUrlLabel   = 8014,
    kBaseUrlEdit    = 8015,

    // Web root
    kWebRootLabel   = 8020,
    kWebRootEdit    = 8021,
    kWebRootBrowse  = 8022,

    // Theme
    kThemeLabel     = 8030,
    kThemeCombo     = 8031,

    // Tuning
    kThreadsLabel   = 8040,
    kThreadsEdit    = 8041,
    kThreadsSpin    = 8042,
    kTimeoutLabel   = 8043,
    kTimeoutEdit    = 8044,
    kTimeoutSpin    = 8045,

    // Auto-start
    kAutoStartCheck = 8050,

    // Footer
    kOkBtn          = IDOK,
    kCancelBtn      = IDCANCEL,
};

// ── Dialog state ──────────────────────────────────────────────────────────────
struct DlgState {
    WebServer*           server;
    WebServerConfig*     cfg;
    std::filesystem::path app_root;

    // Resolved web_root at dialog open (used to scan themes/)
    std::filesystem::path web_root_resolved;

    bool result_ok = false;   // set to true on OK press
};

// ── Helpers ──────────────────────────────────────────────────────────────────

static std::wstring GetEditText(HWND dlg, int id)
{
    wchar_t buf[1024] = {};
    GetDlgItemTextW(dlg, id, buf, 1024);
    return buf;
}

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

// Open a folder-browser dialog; returns empty string on cancel.
static std::string BrowseForFolder(HWND owner, const std::string& initial)
{
    wchar_t buf[MAX_PATH] = {};

    BROWSEINFOW bi = {};
    bi.hwndOwner  = owner;
    bi.pszDisplayName = buf;
    bi.lpszTitle  = L"Select web root folder";
    bi.ulFlags    = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;

    std::wstring init_w = Utf8ToWide(initial);
    if (!init_w.empty()) {
        // Callback to set the initial selection
        bi.lParam = reinterpret_cast<LPARAM>(init_w.c_str());
        bi.lpfn   = [](HWND hw, UINT msg, LPARAM /*lp*/, LPARAM data) -> int {
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

// Scan <web_root>/themes/ for sub-directories → theme names.
static std::vector<std::string> ScanThemes(const std::filesystem::path& web_root)
{
    std::vector<std::string> themes;
    std::filesystem::path themes_dir = web_root / "themes";
    std::error_code ec;
    if (!std::filesystem::is_directory(themes_dir, ec)) return themes;
    for (auto& entry : std::filesystem::directory_iterator(themes_dir, ec)) {
        if (entry.is_directory(ec))
            themes.push_back(entry.path().filename().string());
    }
    std::sort(themes.begin(), themes.end());
    return themes;
}

// Rebuild the theme combo box and select the active theme.
static void PopulateThemeCombo(HWND dlg, DlgState* st)
{
    HWND combo = GetDlgItem(dlg, kThemeCombo);
    ComboBox_ResetContent(combo);

    // Resolve web_root from the current edit field
    std::string wr = WideToUtf8(GetEditText(dlg, kWebRootEdit));
    if (wr.empty()) wr = (st->app_root / "www").string();
    std::filesystem::path web_root_path(wr);

    auto themes = ScanThemes(web_root_path);
    if (themes.empty()) {
        // Always offer "default" even if directory doesn't exist yet
        themes.push_back("default");
    }

    int sel_idx = 0;
    for (int i = 0; i < (int)themes.size(); ++i) {
        std::wstring wt = Utf8ToWide(themes[i]);
        ComboBox_AddString(combo, wt.c_str());
        if (themes[i] == st->cfg->active_theme) sel_idx = i;
    }
    ComboBox_SetCurSel(combo, sel_idx);
}

// Update the status label and Start/Stop button caption.
static void UpdateStatus(HWND dlg, DlgState* st)
{
    if (!st->server) {
        SetDlgItemTextW(dlg, kStatusLabel, L"Status: server not initialized");
        EnableWindow(GetDlgItem(dlg, kStartStopBtn), FALSE);
        return;
    }

    bool running = st->server->IsRunning();
    int  sessions = running ? st->server->ActiveSessions() : 0;

    wchar_t buf[128];
    if (running) {
        _snwprintf_s(buf, _countof(buf), _TRUNCATE,
                     L"Status: Running on port %d  |  Active sessions: %d",
                     st->server->Port(), sessions);
    } else {
        wcscpy_s(buf, L"Status: Stopped");
    }
    SetDlgItemTextW(dlg, kStatusLabel, buf);
    SetDlgItemTextW(dlg, kStartStopBtn, running ? L"Stop" : L"Start");
}

// Read all fields from dialog controls into a WebServerConfig.
static WebServerConfig ReadFields(HWND dlg, DlgState* st)
{
    WebServerConfig c = *st->cfg;   // start from current

    // Port
    BOOL ok;
    int port = GetDlgItemInt(dlg, kPortEdit, &ok, FALSE);
    if (ok && port > 0 && port <= 65535) c.port = port;

    // Bind address
    c.bind_address = WideToUtf8(GetEditText(dlg, kBindEdit));

    // Base URL
    c.base_url = WideToUtf8(GetEditText(dlg, kBaseUrlEdit));

    // Web root
    c.web_root = WideToUtf8(GetEditText(dlg, kWebRootEdit));

    // Theme
    HWND combo = GetDlgItem(dlg, kThemeCombo);
    int idx = ComboBox_GetCurSel(combo);
    if (idx >= 0) {
        wchar_t tbuf[256] = {};
        ComboBox_GetLBText(combo, idx, tbuf);
        c.active_theme = WideToUtf8(tbuf);
    }

    // Thread pool
    int threads = GetDlgItemInt(dlg, kThreadsEdit, &ok, FALSE);
    if (ok && threads >= 1 && threads <= 64) c.thread_pool_size = threads;

    // Session timeout
    int timeout = GetDlgItemInt(dlg, kTimeoutEdit, &ok, FALSE);
    if (ok && timeout >= 1 && timeout <= 10080) c.session_timeout_minutes = timeout;

    // Auto-start
    c.auto_start = (IsDlgButtonChecked(dlg, kAutoStartCheck) == BST_CHECKED);

    return c;
}

// ── Layout constants ──────────────────────────────────────────────────────────
constexpr int kDlgW  = 480;
constexpr int kDlgH  = 440;
constexpr int kLblW  = 120;
constexpr int kEdtW  = 280;
constexpr int kBtnW  = 60;
constexpr int kBtnH  = 22;
constexpr int kRowH  = 28;
constexpr int kMarL  = 16;
constexpr int kMarT  = 14;

// Create a right-aligned static label + edit field row.
// Returns the HWND of the edit control.
static HWND AddRow(HWND dlg, int y, int id_lbl_unused, int id_edit,
                   const wchar_t* label, const std::string& value,
                   DWORD extra_style = 0)
{
    (void)id_lbl_unused;
    CreateWindowExW(0, L"STATIC", label,
                    WS_CHILD | WS_VISIBLE | SS_RIGHT,
                    kMarL, y + 3, kLblW, 16,
                    dlg, nullptr, nullptr, nullptr);
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                    Utf8ToWide(value).c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | extra_style,
                    kMarL + kLblW + 8, y, kEdtW, 20,
                    dlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id_edit)),
                    nullptr, nullptr);
    return edit;
}

// ── WndProc ───────────────────────────────────────────────────────────────────
static LRESULT CALLBACK WebConfigProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    DlgState* st = reinterpret_cast<DlgState*>(
        GetWindowLongPtrW(dlg, GWLP_USERDATA));

    switch (msg) {

    // ── WM_CREATE ────────────────────────────────────────────────────────────
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        st = reinterpret_cast<DlgState*>(cs->lpCreateParams);
        SetWindowLongPtrW(dlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

        HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        auto SetFont = [&](HWND hw) { SendMessageW(hw, WM_SETFONT, (WPARAM)hFont, TRUE); };
        (void)SetFont;

        int y = kMarT;

        // ── Status row ───────────────────────────────────────────────────────
        HWND lblStat = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE,
            kMarL, y + 3, kDlgW - kMarL * 2 - kBtnW - 8, 16,
            dlg, reinterpret_cast<HMENU>(kStatusLabel), nullptr, nullptr);
        SendMessageW(lblStat, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND btnSS = CreateWindowExW(0, L"BUTTON", L"Start",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            kDlgW - kMarL - kBtnW, y, kBtnW, kBtnH,
            dlg, reinterpret_cast<HMENU>(kStartStopBtn), nullptr, nullptr);
        SendMessageW(btnSS, WM_SETFONT, (WPARAM)hFont, TRUE);

        y += kRowH + 4;

        // ── Horizontal separator ─────────────────────────────────────────────
        CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
            kMarL, y, kDlgW - kMarL * 2, 2,
            dlg, nullptr, nullptr, nullptr);
        y += 10;

        // ── Section: Network ─────────────────────────────────────────────────
        {
            HWND lbl = CreateWindowExW(0, L"STATIC", L"Network",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                kMarL, y, 200, 16,
                dlg, nullptr, nullptr, nullptr);
            SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
            y += 20;
        }

        // Port
        {
            CreateWindowExW(0, L"STATIC", L"Port:",
                WS_CHILD | WS_VISIBLE | SS_RIGHT,
                kMarL, y + 3, kLblW, 16,
                dlg, reinterpret_cast<HMENU>(kPortLabel), nullptr, nullptr);
            wchar_t portBuf[16];
            _snwprintf_s(portBuf, _countof(portBuf), _TRUNCATE, L"%d", st->cfg->port);
            HWND editPort = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", portBuf,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
                kMarL + kLblW + 8, y, 60, 20,
                dlg, reinterpret_cast<HMENU>(kPortEdit), nullptr, nullptr);
            SendMessageW(editPort, WM_SETFONT, (WPARAM)hFont, TRUE);
            y += kRowH;
        }

        // Bind address
        {
            CreateWindowExW(0, L"STATIC", L"Bind address:",
                WS_CHILD | WS_VISIBLE | SS_RIGHT,
                kMarL, y + 3, kLblW, 16,
                dlg, reinterpret_cast<HMENU>(kBindLabel), nullptr, nullptr);
            HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                Utf8ToWide(st->cfg->bind_address).c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                kMarL + kLblW + 8, y, 200, 20,
                dlg, reinterpret_cast<HMENU>(kBindEdit), nullptr, nullptr);
            SendMessageW(e, WM_SETFONT, (WPARAM)hFont, TRUE);
            y += kRowH;
        }

        // Base URL
        {
            CreateWindowExW(0, L"STATIC", L"Base URL:",
                WS_CHILD | WS_VISIBLE | SS_RIGHT,
                kMarL, y + 3, kLblW, 16,
                dlg, reinterpret_cast<HMENU>(kBaseUrlLabel), nullptr, nullptr);
            HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                Utf8ToWide(st->cfg->base_url).c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                kMarL + kLblW + 8, y, kEdtW, 20,
                dlg, reinterpret_cast<HMENU>(kBaseUrlEdit), nullptr, nullptr);
            SendMessageW(e, WM_SETFONT, (WPARAM)hFont, TRUE);
            y += kRowH + 4;
        }

        // ── Section: Web root ─────────────────────────────────────────────────
        {
            HWND lbl = CreateWindowExW(0, L"STATIC", L"Web root",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                kMarL, y, 200, 16,
                dlg, nullptr, nullptr, nullptr);
            SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
            y += 20;
        }

        {
            CreateWindowExW(0, L"STATIC", L"Folder:",
                WS_CHILD | WS_VISIBLE | SS_RIGHT,
                kMarL, y + 3, kLblW, 16,
                dlg, reinterpret_cast<HMENU>(kWebRootLabel), nullptr, nullptr);
            HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                Utf8ToWide(st->cfg->web_root).c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                kMarL + kLblW + 8, y, kEdtW - kBtnW - 6, 20,
                dlg, reinterpret_cast<HMENU>(kWebRootEdit), nullptr, nullptr);
            SendMessageW(e, WM_SETFONT, (WPARAM)hFont, TRUE);
            HWND btn = CreateWindowExW(0, L"BUTTON", L"Browse…",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                kMarL + kLblW + 8 + kEdtW - kBtnW - 6 + 6, y, kBtnW + 10, kBtnH,
                dlg, reinterpret_cast<HMENU>(kWebRootBrowse), nullptr, nullptr);
            SendMessageW(btn, WM_SETFONT, (WPARAM)hFont, TRUE);
            y += kRowH + 4;
        }

        // ── Section: Appearance ───────────────────────────────────────────────
        {
            HWND lbl = CreateWindowExW(0, L"STATIC", L"Appearance",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                kMarL, y, 200, 16,
                dlg, nullptr, nullptr, nullptr);
            SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
            y += 20;
        }

        // Theme
        {
            CreateWindowExW(0, L"STATIC", L"Theme:",
                WS_CHILD | WS_VISIBLE | SS_RIGHT,
                kMarL, y + 3, kLblW, 16,
                dlg, reinterpret_cast<HMENU>(kThemeLabel), nullptr, nullptr);
            HWND combo = CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                kMarL + kLblW + 8, y, 200, 160,
                dlg, reinterpret_cast<HMENU>(kThemeCombo), nullptr, nullptr);
            SendMessageW(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
            y += kRowH + 4;
        }

        // ── Section: Performance ──────────────────────────────────────────────
        {
            HWND lbl = CreateWindowExW(0, L"STATIC", L"Performance",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                kMarL, y, 200, 16,
                dlg, nullptr, nullptr, nullptr);
            SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
            y += 20;
        }

        // Thread pool
        {
            CreateWindowExW(0, L"STATIC", L"Thread pool:",
                WS_CHILD | WS_VISIBLE | SS_RIGHT,
                kMarL, y + 3, kLblW, 16,
                dlg, reinterpret_cast<HMENU>(kThreadsLabel), nullptr, nullptr);
            wchar_t buf[16];
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%d", st->cfg->thread_pool_size);
            HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", buf,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
                kMarL + kLblW + 8, y, 50, 20,
                dlg, reinterpret_cast<HMENU>(kThreadsEdit), nullptr, nullptr);
            SendMessageW(e, WM_SETFONT, (WPARAM)hFont, TRUE);

            HWND spin = CreateWindowExW(0, UPDOWN_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS,
                0, 0, 0, 0,
                dlg, reinterpret_cast<HMENU>(kThreadsSpin), nullptr, nullptr);
            SendMessageW(spin, UDM_SETBUDDY, (WPARAM)e, 0);
            SendMessageW(spin, UDM_SETRANGE32, 1, 64);
            SendMessageW(spin, UDM_SETPOS32, 0, st->cfg->thread_pool_size);

            // helper label
            CreateWindowExW(0, L"STATIC", L"(1–64)",
                WS_CHILD | WS_VISIBLE,
                kMarL + kLblW + 8 + 58, y + 3, 50, 16,
                dlg, nullptr, nullptr, nullptr);
            y += kRowH;
        }

        // Session timeout
        {
            CreateWindowExW(0, L"STATIC", L"Session timeout:",
                WS_CHILD | WS_VISIBLE | SS_RIGHT,
                kMarL, y + 3, kLblW, 16,
                dlg, reinterpret_cast<HMENU>(kTimeoutLabel), nullptr, nullptr);
            wchar_t buf[16];
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%d", st->cfg->session_timeout_minutes);
            HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", buf,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
                kMarL + kLblW + 8, y, 50, 20,
                dlg, reinterpret_cast<HMENU>(kTimeoutEdit), nullptr, nullptr);
            SendMessageW(e, WM_SETFONT, (WPARAM)hFont, TRUE);

            HWND spin = CreateWindowExW(0, UPDOWN_CLASSW, L"",
                WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS,
                0, 0, 0, 0,
                dlg, reinterpret_cast<HMENU>(kTimeoutSpin), nullptr, nullptr);
            SendMessageW(spin, UDM_SETBUDDY, (WPARAM)e, 0);
            SendMessageW(spin, UDM_SETRANGE32, 1, 10080);
            SendMessageW(spin, UDM_SETPOS32, 0, st->cfg->session_timeout_minutes);

            CreateWindowExW(0, L"STATIC", L"minutes",
                WS_CHILD | WS_VISIBLE,
                kMarL + kLblW + 8 + 58, y + 3, 60, 16,
                dlg, nullptr, nullptr, nullptr);
            y += kRowH + 4;
        }

        // ── Auto-start checkbox ────────────────────────────────────────────────
        {
            HWND chk = CreateWindowExW(0, L"BUTTON", L"Start web server automatically on launch",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                kMarL + kLblW + 8, y, 280, 20,
                dlg, reinterpret_cast<HMENU>(kAutoStartCheck), nullptr, nullptr);
            SendMessageW(chk, WM_SETFONT, (WPARAM)hFont, TRUE);
            CheckDlgButton(dlg, kAutoStartCheck,
                st->cfg->auto_start ? BST_CHECKED : BST_UNCHECKED);
            y += kRowH + 4;
        }

        // ── Footer buttons ────────────────────────────────────────────────────
        {
            int fx = kDlgW - kMarL - kBtnW * 2 - 8;
            int fy = kDlgH - kBtnH - 12;
            HWND btnOk = CreateWindowExW(0, L"BUTTON", L"OK",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                fx, fy, kBtnW, kBtnH,
                dlg, reinterpret_cast<HMENU>(kOkBtn), nullptr, nullptr);
            SendMessageW(btnOk, WM_SETFONT, (WPARAM)hFont, TRUE);

            HWND btnCan = CreateWindowExW(0, L"BUTTON", L"Cancel",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                fx + kBtnW + 8, fy, kBtnW, kBtnH,
                dlg, reinterpret_cast<HMENU>(kCancelBtn), nullptr, nullptr);
            SendMessageW(btnCan, WM_SETFONT, (WPARAM)hFont, TRUE);
        }

        // Populate theme combo after all controls exist
        PopulateThemeCombo(dlg, st);
        UpdateStatus(dlg, st);
        return 0;
    }

    // ── WM_COMMAND ────────────────────────────────────────────────────────────
    case WM_COMMAND: {
        int id  = LOWORD(wp);
        int ntf = HIWORD(wp);

        if (id == kStartStopBtn) {
            if (st->server) {
                if (st->server->IsRunning()) {
                    st->server->Stop();
                } else {
                    // Apply current field values before starting
                    WebServerConfig newCfg = ReadFields(dlg, st);
                    st->server->Reconfigure(newCfg);
                    st->server->Start();
                }
            }
            UpdateStatus(dlg, st);
            return 0;
        }

        if (id == kWebRootBrowse) {
            std::string cur = WideToUtf8(GetEditText(dlg, kWebRootEdit));
            if (cur.empty()) cur = (st->app_root / "www").string();
            std::string chosen = BrowseForFolder(dlg, cur);
            if (!chosen.empty()) {
                SetDlgItemTextW(dlg, kWebRootEdit, Utf8ToWide(chosen).c_str());
                // Re-scan themes for the new folder
                PopulateThemeCombo(dlg, st);
            }
            return 0;
        }

        // Re-scan themes when web root text changes and user leaves the field
        if (id == kWebRootEdit && ntf == EN_KILLFOCUS) {
            PopulateThemeCombo(dlg, st);
            return 0;
        }

        if (id == kOkBtn) {
            *st->cfg = ReadFields(dlg, st);

            // If server is running, restart with new config
            if (st->server && st->server->IsRunning()) {
                st->server->Reconfigure(*st->cfg);
            }

            st->result_ok = true;
            DestroyWindow(dlg);
            return 0;
        }

        if (id == kCancelBtn || id == IDCANCEL) {
            st->result_ok = false;
            DestroyWindow(dlg);
            return 0;
        }

        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(dlg);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(dlg, msg, wp, lp);
}

} // namespace

// ── Public entry point ────────────────────────────────────────────────────────
bool ShowWebConfigDialog(HWND                        owner,
                         WebServer*                  server,
                         WebServerConfig*            config,
                         const std::filesystem::path app_root)
{
    // Ensure common controls are initialized (for UPDOWN_CLASS)
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_UPDOWN_CLASS };
    InitCommonControlsEx(&icex);

    // Register window class once per process
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WebConfigProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kClassName;
        wc.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
        RegisterClassExW(&wc);
        registered = true;
    }

    DlgState state;
    state.server    = server;
    state.cfg       = config;
    state.app_root  = app_root;

    // Centre on owner
    RECT ownerRect = {};
    if (owner) GetWindowRect(owner, &ownerRect);
    int ox = ownerRect.left + (ownerRect.right  - ownerRect.left  - kDlgW) / 2;
    int oy = ownerRect.top  + (ownerRect.bottom - ownerRect.top   - kDlgH) / 2;
    if (!owner) { ox = 200; oy = 150; }

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kClassName,
        L"Web Server Configuration",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        ox, oy, kDlgW, kDlgH,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        &state);

    if (!dlg) return false;

    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);

    // Modal message loop — runs until WM_DESTROY posts WM_QUIT
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    // state.result_ok was set to true iff the user pressed OK
    return state.result_ok;
}
