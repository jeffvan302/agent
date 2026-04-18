// ──────────────────────────────────────────────────────────────────────────────
// web_config_dialog.cpp — Win32 Web Config dialog implementation.
//
// Control ID range: 8000–8099 (leaves 8100+ for admin_config_dialog).
// ──────────────────────────────────────────────────────────────────────────────

#include "web_config_dialog.h"
#include "util.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <exception>
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

    // TLS / Security
    kTlsModeLabel       = 8060,
    kTlsModeCombo       = 8061,
    kTlsCertLabel       = 8062,
    kTlsCertEdit        = 8063,
    kTlsCertBrowse      = 8064,
    kTlsKeyLabel        = 8065,
    kTlsKeyEdit         = 8066,
    kTlsKeyBrowse       = 8067,
    kTlsPfxLabel        = 8068,
    kTlsPfxEdit         = 8069,
    kTlsPfxBrowse       = 8070,
    kTlsPfxPassLabel    = 8071,
    kTlsPfxPassEdit     = 8072,
    kTlsRedirLabel      = 8073,
    kTlsRedirEdit       = 8074,
    kTlsExpiryLabel     = 8075,
    kTlsExpiryValue     = 8076,
    kTlsGenCertBtn      = 8077,
    kSaveBtn            = 8078,

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

    bool result_ok = false;   // set to true once settings are saved/applied
};

static void RunModalLoop(HWND dlg, HWND owner)
{
    const bool owner_was_enabled =
        owner && IsWindow(owner) && IsWindowEnabled(owner);
    if (owner_was_enabled) EnableWindow(owner, FALSE);

    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);

    MSG msg = {};
    bool got_quit = false;
    while (IsWindow(dlg)) {
        const BOOL rc = GetMessageW(&msg, nullptr, 0, 0);
        if (rc <= 0) {
            got_quit = (rc == 0);
            break;
        }
        if (!IsWindow(dlg)) break;
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (owner_was_enabled && IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    if (got_quit) PostQuitMessage(static_cast<int>(msg.wParam));
}

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

// Open a file-picker dialog; returns empty string on cancel.
// filter_pairs: alternating description and pattern e.g. "PEM files\0*.crt;*.pem\0"
static std::string BrowseForFile(HWND owner, const std::string& initial,
                                  const wchar_t* filter_pairs)
{
    wchar_t buf[MAX_PATH] = {};
    std::wstring init_w = Utf8ToWide(initial);
    wcsncpy_s(buf, init_w.c_str(), _TRUNCATE);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFilter = filter_pairs;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!GetOpenFileNameW(&ofn)) return {};
    return WideToUtf8(buf);
}

// TLS mode combo box helpers: index → string id, string id → index.
static const wchar_t* kTlsModeLabels[] = {
    L"Off (HTTP)",
    L"Self-signed (auto-generate)",
    L"PEM files (cert + key)",
    L"PFX / PKCS#12 bundle",
};
static const char* kTlsModeIds[] = { "", "self_signed", "pem", "pfx" };
static constexpr int kTlsModeCount = 4;

static int TlsModeToIndex(const std::string& mode) {
    for (int i = 0; i < kTlsModeCount; ++i)
        if (kTlsModeIds[i] == mode) return i;
    return 0;
}

// Show/hide the TLS sub-controls based on the selected mode index.
// Visibility rules:
//   Off:          nothing extra
//   Self-signed:  expiry label, [Generate] button
//   PEM:          cert row, key row, redirect row, expiry
//   PFX:          pfx row, passphrase row, redirect row, expiry
static void UpdateTlsControlVisibility(HWND dlg, int mode_idx)
{
    auto show = [&](int id, bool vis) {
        ShowWindow(GetDlgItem(dlg, id), vis ? SW_SHOW : SW_HIDE);
    };
    bool is_self   = (mode_idx == 1);
    bool is_pem    = (mode_idx == 2);
    bool is_pfx    = (mode_idx == 3);
    bool has_tls   = (mode_idx > 0);

    show(kTlsCertLabel,    is_pem);
    show(kTlsCertEdit,     is_pem);
    show(kTlsCertBrowse,   is_pem);
    show(kTlsKeyLabel,     is_pem);
    show(kTlsKeyEdit,      is_pem);
    show(kTlsKeyBrowse,    is_pem);
    show(kTlsPfxLabel,     is_pfx);
    show(kTlsPfxEdit,      is_pfx);
    show(kTlsPfxBrowse,    is_pfx);
    show(kTlsPfxPassLabel, is_pfx);
    show(kTlsPfxPassEdit,  is_pfx);
    show(kTlsRedirLabel,   has_tls);
    show(kTlsRedirEdit,    has_tls);
    show(kTlsExpiryLabel,  has_tls);
    show(kTlsExpiryValue,  has_tls);
    show(kTlsGenCertBtn,   is_self);
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
    {
        HWND themeCombo = GetDlgItem(dlg, kThemeCombo);
        int themeIdx = ComboBox_GetCurSel(themeCombo);
        if (themeIdx >= 0) {
            wchar_t tbuf[256] = {};
            ComboBox_GetLBText(themeCombo, themeIdx, tbuf);
            c.active_theme = WideToUtf8(tbuf);
        }
    }

    // Thread pool
    int threads = GetDlgItemInt(dlg, kThreadsEdit, &ok, FALSE);
    if (ok && threads >= 1 && threads <= 64) c.thread_pool_size = threads;

    // Session timeout
    int timeout = GetDlgItemInt(dlg, kTimeoutEdit, &ok, FALSE);
    if (ok && timeout >= 1 && timeout <= 10080) c.session_timeout_minutes = timeout;

    // Auto-start
    c.auto_start = (IsDlgButtonChecked(dlg, kAutoStartCheck) == BST_CHECKED);

    // TLS mode
    {
        HWND tlsCombo = GetDlgItem(dlg, kTlsModeCombo);
        int tlsIdx = ComboBox_GetCurSel(tlsCombo);
        if (tlsIdx >= 0 && tlsIdx < kTlsModeCount) c.tls_mode = kTlsModeIds[tlsIdx];
    }
    c.tls_cert_file      = WideToUtf8(GetEditText(dlg, kTlsCertEdit));
    c.tls_key_file       = WideToUtf8(GetEditText(dlg, kTlsKeyEdit));
    c.tls_pfx_file       = WideToUtf8(GetEditText(dlg, kTlsPfxEdit));
    c.tls_pfx_passphrase = WideToUtf8(GetEditText(dlg, kTlsPfxPassEdit));
    {
        BOOL ok2;
        int rp = GetDlgItemInt(dlg, kTlsRedirEdit, &ok2, FALSE);
        c.http_redirect_port = (ok2 && rp >= 0 && rp <= 65535) ? rp : 0;
    }

    return c;
}

static bool SaveFields(HWND dlg, DlgState* st, bool notify)
{
    if (!st || !st->cfg) return false;

    WebServerConfig newCfg = ReadFields(dlg, st);
    const auto settings_path = st->app_root / "web_settings.json";

    try {
        newCfg.SaveToFile(settings_path);
    } catch (const std::exception& ex) {
        std::wstring msg = L"Could not save web server settings.\n\n";
        msg += Utf8ToWide(ex.what());
        MessageBoxW(dlg, msg.c_str(), L"Web Server Configuration",
                    MB_OK | MB_ICONERROR);
        return false;
    } catch (...) {
        MessageBoxW(dlg, L"Could not save web server settings.",
                    L"Web Server Configuration", MB_OK | MB_ICONERROR);
        return false;
    }

    *st->cfg = newCfg;
    if (st->server) {
        st->server->Reconfigure(newCfg);
    }
    st->result_ok = true;
    UpdateStatus(dlg, st);

    if (notify) {
        MessageBoxW(dlg, L"Web server settings saved.",
                    L"Web Server Configuration", MB_OK | MB_ICONINFORMATION);
    }
    return true;
}

// ── Layout constants ──────────────────────────────────────────────────────────
constexpr int kDlgW  = 520;   // desired client width
constexpr int kDlgH  = 740;   // desired client height; tall enough for TLS controls + footer
constexpr int kLblW  = 130;
constexpr int kEdtW  = 290;
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
                kMarL + kLblW + 8, y, 300, 20,
                dlg, reinterpret_cast<HMENU>(kAutoStartCheck), nullptr, nullptr);
            SendMessageW(chk, WM_SETFONT, (WPARAM)hFont, TRUE);
            CheckDlgButton(dlg, kAutoStartCheck,
                st->cfg->auto_start ? BST_CHECKED : BST_UNCHECKED);
            y += kRowH + 8;
        }

        // ── Separator ─────────────────────────────────────────────────────────
        CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
            kMarL, y, kDlgW - kMarL * 2, 2,
            dlg, nullptr, nullptr, nullptr);
        y += 10;

        // ── Section: Security (HTTPS / TLS) ──────────────────────────────────
        {
            HWND lbl = CreateWindowExW(0, L"STATIC", L"Security (HTTPS / TLS)",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                kMarL, y, 260, 16,
                dlg, nullptr, nullptr, nullptr);
            SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
            y += 20;
        }

        // TLS mode combo
        {
            HWND lbl = CreateWindowExW(0, L"STATIC", L"Mode:",
                WS_CHILD | WS_VISIBLE | SS_RIGHT,
                kMarL, y + 3, kLblW, 16,
                dlg, reinterpret_cast<HMENU>(kTlsModeLabel), nullptr, nullptr);
            SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);

            HWND combo = CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                kMarL + kLblW + 8, y, 240, 120,
                dlg, reinterpret_cast<HMENU>(kTlsModeCombo), nullptr, nullptr);
            SendMessageW(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
            for (int i = 0; i < kTlsModeCount; ++i)
                ComboBox_AddString(combo, kTlsModeLabels[i]);
            ComboBox_SetCurSel(combo, TlsModeToIndex(st->cfg->tls_mode));
            y += kRowH;
        }

        // PEM cert file
        {
            HWND lbl = CreateWindowExW(0, L"STATIC", L"Certificate (.crt/.pem):",
                WS_CHILD | SS_RIGHT,
                kMarL, y + 3, kLblW, 16,
                dlg, reinterpret_cast<HMENU>(kTlsCertLabel), nullptr, nullptr);
            SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
            HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                Utf8ToWide(st->cfg->tls_cert_file).c_str(),
                WS_CHILD | WS_TABSTOP,
                kMarL + kLblW + 8, y, kEdtW - kBtnW - 6, 20,
                dlg, reinterpret_cast<HMENU>(kTlsCertEdit), nullptr, nullptr);
            SendMessageW(e, WM_SETFONT, (WPARAM)hFont, TRUE);
            HWND btn = CreateWindowExW(0, L"BUTTON", L"Browse…",
                WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                kMarL + kLblW + 8 + kEdtW - kBtnW - 6 + 6, y, kBtnW + 10, kBtnH,
                dlg, reinterpret_cast<HMENU>(kTlsCertBrowse), nullptr, nullptr);
            SendMessageW(btn, WM_SETFONT, (WPARAM)hFont, TRUE);
            y += kRowH;
        }

        // PEM key file
        {
            HWND lbl = CreateWindowExW(0, L"STATIC", L"Private key (.key/.pem):",
                WS_CHILD | SS_RIGHT,
                kMarL, y + 3, kLblW, 16,
                dlg, reinterpret_cast<HMENU>(kTlsKeyLabel), nullptr, nullptr);
            SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
            HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                Utf8ToWide(st->cfg->tls_key_file).c_str(),
                WS_CHILD | WS_TABSTOP,
                kMarL + kLblW + 8, y, kEdtW - kBtnW - 6, 20,
                dlg, reinterpret_cast<HMENU>(kTlsKeyEdit), nullptr, nullptr);
            SendMessageW(e, WM_SETFONT, (WPARAM)hFont, TRUE);
            HWND btn = CreateWindowExW(0, L"BUTTON", L"Browse…",
                WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                kMarL + kLblW + 8 + kEdtW - kBtnW - 6 + 6, y, kBtnW + 10, kBtnH,
                dlg, reinterpret_cast<HMENU>(kTlsKeyBrowse), nullptr, nullptr);
            SendMessageW(btn, WM_SETFONT, (WPARAM)hFont, TRUE);
            y += kRowH;
        }

        // PFX file
        {
            HWND lbl = CreateWindowExW(0, L"STATIC", L"PFX bundle (.pfx):",
                WS_CHILD | SS_RIGHT,
                kMarL, y + 3, kLblW, 16,
                dlg, reinterpret_cast<HMENU>(kTlsPfxLabel), nullptr, nullptr);
            SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
            HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                Utf8ToWide(st->cfg->tls_pfx_file).c_str(),
                WS_CHILD | WS_TABSTOP,
                kMarL + kLblW + 8, y, kEdtW - kBtnW - 6, 20,
                dlg, reinterpret_cast<HMENU>(kTlsPfxEdit), nullptr, nullptr);
            SendMessageW(e, WM_SETFONT, (WPARAM)hFont, TRUE);
            HWND btn = CreateWindowExW(0, L"BUTTON", L"Browse…",
                WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                kMarL + kLblW + 8 + kEdtW - kBtnW - 6 + 6, y, kBtnW + 10, kBtnH,
                dlg, reinterpret_cast<HMENU>(kTlsPfxBrowse), nullptr, nullptr);
            SendMessageW(btn, WM_SETFONT, (WPARAM)hFont, TRUE);
            y += kRowH;
        }

        // PFX passphrase
        {
            HWND lbl = CreateWindowExW(0, L"STATIC", L"PFX passphrase:",
                WS_CHILD | SS_RIGHT,
                kMarL, y + 3, kLblW, 16,
                dlg, reinterpret_cast<HMENU>(kTlsPfxPassLabel), nullptr, nullptr);
            SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
            HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                Utf8ToWide(st->cfg->tls_pfx_passphrase).c_str(),
                WS_CHILD | WS_TABSTOP | ES_PASSWORD,
                kMarL + kLblW + 8, y, 200, 20,
                dlg, reinterpret_cast<HMENU>(kTlsPfxPassEdit), nullptr, nullptr);
            SendMessageW(e, WM_SETFONT, (WPARAM)hFont, TRUE);
            y += kRowH;
        }

        // HTTP redirect port
        {
            HWND lbl = CreateWindowExW(0, L"STATIC", L"HTTP redirect port:",
                WS_CHILD | SS_RIGHT,
                kMarL, y + 3, kLblW, 16,
                dlg, reinterpret_cast<HMENU>(kTlsRedirLabel), nullptr, nullptr);
            SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);
            wchar_t rbuf[16] = L"0";
            if (st->cfg->http_redirect_port > 0)
                _snwprintf_s(rbuf, _countof(rbuf), _TRUNCATE,
                             L"%d", st->cfg->http_redirect_port);
            HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", rbuf,
                WS_CHILD | WS_TABSTOP | ES_NUMBER,
                kMarL + kLblW + 8, y, 70, 20,
                dlg, reinterpret_cast<HMENU>(kTlsRedirEdit), nullptr, nullptr);
            SendMessageW(e, WM_SETFONT, (WPARAM)hFont, TRUE);
            CreateWindowExW(0, L"STATIC", L"(0 = disabled)",
                WS_CHILD,
                kMarL + kLblW + 8 + 78, y + 3, 100, 16,
                dlg, nullptr, nullptr, nullptr);
            y += kRowH;
        }

        // Certificate expiry
        {
            HWND lbl = CreateWindowExW(0, L"STATIC", L"Cert expires:",
                WS_CHILD | SS_RIGHT,
                kMarL, y + 3, kLblW, 16,
                dlg, reinterpret_cast<HMENU>(kTlsExpiryLabel), nullptr, nullptr);
            SendMessageW(lbl, WM_SETFONT, (WPARAM)hFont, TRUE);

            // Compute expiry text
            std::wstring expiry_text = L"—";
            if (st->server && !st->cfg->tls_mode.empty()) {
                int days = st->server->GetCertExpiryDays();
                if (days < 0) {
                    expiry_text = L"Unknown";
                } else if (days == 0) {
                    expiry_text = L"Expires today!";
                } else {
                    wchar_t ebuf[128];
                    if (days <= 30) {
                        _snwprintf_s(ebuf, _countof(ebuf), _TRUNCATE,
                            L"%d day%s  (expiring soon!)", days, days == 1 ? L"" : L"s");
                    } else {
                        _snwprintf_s(ebuf, _countof(ebuf), _TRUNCATE,
                            L"%d day%s", days, days == 1 ? L"" : L"s");
                    }
                    expiry_text = ebuf;
                }
            }
            HWND val = CreateWindowExW(0, L"STATIC", expiry_text.c_str(),
                WS_CHILD | SS_LEFT,
                kMarL + kLblW + 8, y + 3, 280, 16,
                dlg, reinterpret_cast<HMENU>(kTlsExpiryValue), nullptr, nullptr);
            SendMessageW(val, WM_SETFONT, (WPARAM)hFont, TRUE);
            y += kRowH;
        }

        // Generate Self-Signed Certificate button
        {
            HWND btn = CreateWindowExW(0, L"BUTTON", L"Generate Self-Signed Certificate",
                WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                kMarL + kLblW + 8, y, 220, kBtnH,
                dlg, reinterpret_cast<HMENU>(kTlsGenCertBtn), nullptr, nullptr);
            SendMessageW(btn, WM_SETFONT, (WPARAM)hFont, TRUE);
            y += kRowH + 4;
        }

        // Apply initial visibility based on configured TLS mode
        UpdateTlsControlVisibility(dlg, TlsModeToIndex(st->cfg->tls_mode));

        // ── Footer buttons ────────────────────────────────────────────────────
        {
            int fx = kDlgW - kMarL - kBtnW * 3 - 16;
            int fy = kDlgH - kBtnH - 12;   // fixed distance from window bottom
            HWND btnSave = CreateWindowExW(0, L"BUTTON", L"Save",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                fx, fy, kBtnW, kBtnH,
                dlg, reinterpret_cast<HMENU>(kSaveBtn), nullptr, nullptr);
            SendMessageW(btnSave, WM_SETFONT, (WPARAM)hFont, TRUE);

            HWND btnOk = CreateWindowExW(0, L"BUTTON", L"OK",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                fx + kBtnW + 8, fy, kBtnW, kBtnH,
                dlg, reinterpret_cast<HMENU>(kOkBtn), nullptr, nullptr);
            SendMessageW(btnOk, WM_SETFONT, (WPARAM)hFont, TRUE);

            HWND btnCan = CreateWindowExW(0, L"BUTTON", L"Cancel",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                fx + (kBtnW + 8) * 2, fy, kBtnW, kBtnH,
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

        // TLS mode changed — show/hide relevant sub-controls
        if (id == kTlsModeCombo && ntf == CBN_SELCHANGE) {
            int idx = ComboBox_GetCurSel(GetDlgItem(dlg, kTlsModeCombo));
            UpdateTlsControlVisibility(dlg, idx);
            return 0;
        }

        // Browse for PEM cert file
        if (id == kTlsCertBrowse) {
            std::string cur = WideToUtf8(GetEditText(dlg, kTlsCertEdit));
            std::string chosen = BrowseForFile(dlg, cur,
                L"Certificate files\0*.crt;*.pem;*.cer\0All files\0*.*\0");
            if (!chosen.empty())
                SetDlgItemTextW(dlg, kTlsCertEdit, Utf8ToWide(chosen).c_str());
            return 0;
        }

        // Browse for PEM key file
        if (id == kTlsKeyBrowse) {
            std::string cur = WideToUtf8(GetEditText(dlg, kTlsKeyEdit));
            std::string chosen = BrowseForFile(dlg, cur,
                L"Private key files\0*.key;*.pem\0All files\0*.*\0");
            if (!chosen.empty())
                SetDlgItemTextW(dlg, kTlsKeyEdit, Utf8ToWide(chosen).c_str());
            return 0;
        }

        // Browse for PFX file
        if (id == kTlsPfxBrowse) {
            std::string cur = WideToUtf8(GetEditText(dlg, kTlsPfxEdit));
            std::string chosen = BrowseForFile(dlg, cur,
                L"PFX / PKCS#12 files\0*.pfx;*.p12\0All files\0*.*\0");
            if (!chosen.empty())
                SetDlgItemTextW(dlg, kTlsPfxEdit, Utf8ToWide(chosen).c_str());
            return 0;
        }

        // Generate / regenerate self-signed certificate
        if (id == kTlsGenCertBtn) {
            if (!st->server) {
                MessageBoxW(dlg, L"Server is not initialized.",
                            L"Generate Certificate", MB_ICONERROR);
                return 0;
            }
            // Force regeneration by temporarily applying the current config,
            // then calling GetCertExpiryDays() which triggers cert generation.
            WebServerConfig tmp = ReadFields(dlg, st);
            // Swap into server so ResolveTlsCertAndKey uses the right paths
            *st->cfg = tmp;
            // Delete existing certs so they are regenerated
            const auto certs_dir = st->server->TlsCertsDir();
            std::error_code ec;
            std::filesystem::remove(certs_dir / "server.crt", ec);
            std::filesystem::remove(certs_dir / "server.key", ec);
            int days = st->server->GetCertExpiryDays();
            if (days < 0) {
                MessageBoxW(dlg,
                    L"Certificate generation failed.\n\n"
                    L"Make sure OpenSSL support is compiled in\n"
                    L"(run scripts\\download_openssl.ps1 and rebuild).",
                    L"Generate Certificate", MB_ICONERROR);
            } else {
                wchar_t popup[128], expiry[64];
                _snwprintf_s(expiry, _countof(expiry), _TRUNCATE,
                    L"%d day%s", days, days == 1 ? L"" : L"s");
                _snwprintf_s(popup, _countof(popup), _TRUNCATE,
                    L"Self-signed certificate generated.\nExpires in %s.", expiry);
                MessageBoxW(dlg, popup, L"Generate Certificate", MB_ICONINFORMATION);
                // Refresh expiry label
                SetDlgItemTextW(dlg, kTlsExpiryValue, expiry);
            }
            return 0;
        }

        if (id == kSaveBtn) {
            SaveFields(dlg, st, true);
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
            DestroyWindow(dlg);
            return 0;
        }

        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(dlg);
        return 0;

    case WM_DESTROY:
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

    constexpr DWORD kWindowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    constexpr DWORD kWindowExStyle = WS_EX_DLGMODALFRAME;
    RECT windowRect = { 0, 0, kDlgW, kDlgH };
    AdjustWindowRectEx(&windowRect, kWindowStyle, FALSE, kWindowExStyle);
    const int outerW = windowRect.right - windowRect.left;
    const int outerH = windowRect.bottom - windowRect.top;

    // Centre on owner
    RECT ownerRect = {};
    if (owner) GetWindowRect(owner, &ownerRect);
    int ox = ownerRect.left + (ownerRect.right  - ownerRect.left  - outerW) / 2;
    int oy = ownerRect.top  + (ownerRect.bottom - ownerRect.top   - outerH) / 2;
    if (!owner) { ox = 200; oy = 150; }

    HWND dlg = CreateWindowExW(
        kWindowExStyle,
        kClassName,
        L"Web Server Configuration",
        kWindowStyle,
        ox, oy, outerW, outerH,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        &state);

    if (!dlg) {
        Logger::Error("WebConfig", "Dialog creation failed!");
        return false;
    }

    Logger::Info("WebConfig", "About to run modal dialog loop");
    RunModalLoop(dlg, owner);
    Logger::Info("WebConfig", "Dialog closed");
    Logger::Flush();

    // state.result_ok is set once settings are saved or applied.
    return state.result_ok;
}
