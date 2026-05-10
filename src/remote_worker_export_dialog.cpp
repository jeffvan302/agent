#include "remote_worker_export_dialog.h"
#include "provider_profiles.h"
#include "remote_provider_worker.h"
#include "util.h"

#include <commdlg.h>
#include <commctrl.h>
#include <nlohmann/json.hpp>
#include <windowsx.h>

#include <algorithm>
#include <sstream>
#include <string>

using json = nlohmann::json;

namespace {

constexpr wchar_t kClassName[] = L"AgentRemoteWorkerExportDialog";
constexpr int kDialogWidth = 820;
constexpr int kDialogHeight = 540;

enum ControlId : int {
    kBindAddressLabel = 8401,
    kBindAddressEdit = 8402,
    kPortLabel = 8403,
    kPortEdit = 8404,
    kSecretLabel = 8405,
    kSecretEdit = 8406,
    kGenerateSecret = 8407,
    kCertLabel = 8408,
    kCertEdit = 8409,
    kGenerateCert = 8410,
    kProvidersLabel = 8411,
    kProvidersList = 8412,
    kSelectAll = 8413,
    kDeselectAll = 8414,
    kPreviewLabel = 8415,
    kPreviewEdit = 8416,
    kSaveButton = 8417,
    kCancelButton = IDCANCEL,
};

class ExportDialog {
public:
    ExportDialog(HWND owner, AppStorage* storage, const std::vector<ProviderConfig>& providers)
        : owner_(owner), storage_(storage), providers_(providers) {}

    std::optional<std::filesystem::path> Run() {
        HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
        RegisterWindowClass(instance);
        hwnd_ = CreateWindowExW(
            WS_EX_DLGMODALFRAME,
            kClassName,
            L"Export Remote Worker Configuration",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT,
            kDialogWidth, kDialogHeight,
            owner_,
            nullptr,
            instance,
            this);
        if (!hwnd_) return std::nullopt;
        CenterWindowToOwner(hwnd_, owner_);
        EnableWindow(owner_, FALSE);
        MSG msg{};
        while (IsWindow(hwnd_) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(hwnd_, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        EnableWindow(owner_, TRUE);
        SetForegroundWindow(owner_);
        return result_path_;
    }

private:
    static void RegisterWindowClass(HINSTANCE instance) {
        static bool registered = false;
        if (registered) return;
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = &ExportDialog::WindowProc;
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        RegisterClassExW(&wc);
        registered = true;
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
        auto* self = reinterpret_cast<ExportDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
            self = reinterpret_cast<ExportDialog*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }
        if (message == WM_DRAWITEM) {
            if (self) self->DrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(l_param));
            return TRUE;
        }
        if (message == WM_MEASUREITEM) {
            if (self) self->OnMeasureItem(reinterpret_cast<MEASUREITEMSTRUCT*>(l_param));
            return TRUE;
        }
        if (!self) return DefWindowProcW(hwnd, message, w_param, l_param);
        switch (message) {
        case WM_CREATE:
            self->OnCreate();
            return 0;
        case WM_SIZE:
            self->LayoutControls(LOWORD(l_param), HIWORD(l_param));
            return 0;
        case WM_COMMAND:
            if (LOWORD(w_param) == kProvidersList && HIWORD(w_param) == LBN_DBLCLK) {
                POINT pt{};
                GetCursorPos(&pt);
                ScreenToClient(self->providers_list_, &pt);
                self->OnListClick(pt.x, pt.y);
            } else {
                self->OnCommand(LOWORD(w_param));
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_NCDESTROY:
            delete self;
            return 0;
        default:
            return DefWindowProcW(hwnd, message, w_param, l_param);
        }
    }

    int Scale(int value) const { return MulDiv(value, GetDpiForWindow(hwnd_), 96); }

    HWND MakeLabel(const wchar_t* text, int id) {
        return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(id), nullptr, nullptr);
    }
    HWND MakeEdit(const wchar_t* text, int id, DWORD extra = ES_AUTOHSCROLL) {
        return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | extra,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(id), nullptr, nullptr);
    }
    HWND MakeButton(const wchar_t* text, int id) {
        return CreateWindowExW(0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(id), nullptr, nullptr);
    }

    void OnCreate() {
        font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        bind_address_label_ = MakeLabel(L"Bind address:", kBindAddressLabel);
        bind_address_edit_ = MakeEdit(L"0.0.0.0", kBindAddressEdit);
        port_label_ = MakeLabel(L"HTTPS port:", kPortLabel);
        port_edit_ = MakeEdit(L"8765", kPortEdit, ES_NUMBER);
        secret_label_ = MakeLabel(L"Shared secret:", kSecretLabel);
        secret_edit_ = MakeEdit(L"", kSecretEdit);
        generate_secret_button_ = MakeButton(L"Generate", kGenerateSecret);
        cert_label_ = MakeLabel(L"Certificate fingerprint:", kCertLabel);
        cert_edit_ = MakeEdit(L"", kCertEdit, ES_READONLY);
        generate_cert_button_ = MakeButton(L"Generate Certificate", kGenerateCert);

        providers_label_ = MakeLabel(L"Providers to export (check to include):", kProvidersLabel);
        providers_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS | LBS_OWNERDRAWFIXED,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kProvidersList), nullptr, nullptr);
        select_all_button_ = MakeButton(L"Select All", kSelectAll);
        deselect_all_button_ = MakeButton(L"Deselect All", kDeselectAll);

        preview_label_ = MakeLabel(L"Preview JSON:", kPreviewLabel);
        preview_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kPreviewEdit), nullptr, nullptr);

        save_button_ = MakeButton(L"Save JSON...", kSaveButton);
        cancel_button_ = MakeButton(L"Cancel", kCancelButton);

        for (HWND ctl : {bind_address_label_, bind_address_edit_, port_label_, port_edit_,
                           secret_label_, secret_edit_, generate_secret_button_,
                           cert_label_, cert_edit_, generate_cert_button_,
                           providers_label_, providers_list_, select_all_button_, deselect_all_button_,
                           preview_label_, preview_edit_, save_button_, cancel_button_}) {
            SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }

        // Fill provider list
        for (size_t i = 0; i < providers_.size(); ++i) {
            const auto& p = providers_[i];
            std::wstring label = Utf8ToWide(p.name + "  [" + NormalizeProviderType(p.provider_type) + "]");
            // Store index in item data
            int idx = static_cast<int>(SendMessageW(providers_list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())));
            if (idx >= 0) {
                SendMessageW(providers_list_, LB_SETITEMDATA, idx, static_cast<LPARAM>(i));
                // Default all checked
                checked_.push_back(true);
            }
        }

        OnGenerateSecret();
        UpdatePreview();
        LayoutControls(kDialogWidth, kDialogHeight);
        SetFocus(providers_list_);
    }

    void LayoutControls(int width, int height) {
        const int margin = Scale(14);
        const int gutter = Scale(10);
        const int row_h = Scale(24);
        const int btn_h = Scale(26);
        const int mid = width / 2;

        int y = margin;
        MoveWindow(bind_address_label_, margin, y, Scale(90), row_h, TRUE);
        MoveWindow(bind_address_edit_, margin + Scale(100), y, mid - margin - Scale(100) - gutter, row_h, TRUE);
        MoveWindow(port_label_, mid + gutter, y, Scale(70), row_h, TRUE);
        MoveWindow(port_edit_, mid + gutter + Scale(80), y, mid - margin - Scale(80) - gutter, row_h, TRUE);
        y += row_h + gutter;

        MoveWindow(secret_label_, margin, y, Scale(90), row_h, TRUE);
        MoveWindow(secret_edit_, margin + Scale(100), y, mid - margin - Scale(100) - Scale(90) - gutter * 2, row_h, TRUE);
        MoveWindow(generate_secret_button_, mid - Scale(80), y, Scale(80), row_h, TRUE);
        y += row_h + gutter;

        MoveWindow(cert_label_, margin, y, Scale(140), row_h, TRUE);
        MoveWindow(cert_edit_, margin + Scale(150), y, mid - margin - Scale(150) - Scale(120) - gutter * 2, row_h, TRUE);
        MoveWindow(generate_cert_button_, mid - Scale(110), y, Scale(110), row_h, TRUE);
        y += row_h + gutter * 2;

        MoveWindow(providers_label_, margin, y, width - margin * 2, row_h, TRUE);
        y += row_h + gutter;
        int list_h = height - y - margin - btn_h - gutter * 3 - Scale(120);
        MoveWindow(providers_list_, margin, y, mid - margin - gutter, list_h, TRUE);
        MoveWindow(select_all_button_, margin, y + list_h + gutter, Scale(90), btn_h, TRUE);
        MoveWindow(deselect_all_button_, margin + Scale(100), y + list_h + gutter, Scale(90), btn_h, TRUE);

        MoveWindow(preview_label_, mid + gutter, y, mid - margin - gutter, row_h, TRUE);
        MoveWindow(preview_edit_, mid + gutter, y + row_h + gutter, mid - margin - gutter, list_h + gutter + btn_h, TRUE);

        int footer_y = height - margin - btn_h;
        MoveWindow(save_button_, width - margin - Scale(180), footer_y, Scale(90), btn_h, TRUE);
        MoveWindow(cancel_button_, width - margin - Scale(80), footer_y, Scale(80), btn_h, TRUE);
    }

    void OnGenerateSecret() {
        SetWindowTextW(secret_edit_, Utf8ToWide(GenerateRemoteProviderWorkerSharedSecret()).c_str());
    }

    void OnGenerateCert() {
        RemoteProviderWorkerCertificateMaterial mat;
        std::string error;
        if (!GenerateRemoteProviderWorkerSelfSignedCertificateMaterial(&mat, &error)) {
            MessageBoxW(hwnd_, Utf8ToWide(error).c_str(), L"Certificate Generation Failed", MB_OK | MB_ICONERROR);
            return;
        }
        cert_pem_ = mat.certificate_pem;
        key_pem_ = mat.private_key_pem;
        SetWindowTextW(cert_edit_, Utf8ToWide(mat.fingerprint).c_str());
    }

    json BuildPreviewJson() const {
        json providers = json::array();
        for (size_t i = 0; i < providers_.size(); ++i) {
            if (i >= checked_.size() || !checked_[i]) continue;
            const auto& p = providers_[i];
            json prov = {
                {"id", p.id},
                {"name", p.name},
                {"provider_type", p.provider_type},
                {"base_url", p.base_url},
                {"model_count", static_cast<int>(p.models.size())}
            };
            providers.push_back(prov);
        }
        std::string fp = WideToUtf8(GetWindowTextString(cert_edit_));
        json root = {
            {"version", 2},
            {"worker_name", "Agent Remote Worker"},
            {"agent_server", {
                {"bind_address", WideToUtf8(GetWindowTextString(bind_address_edit_))},
                {"https_port", ParseInt(GetWindowTextString(port_edit_)).value_or(8765)},
                {"shared_secret", WideToUtf8(GetWindowTextString(secret_edit_))},
                {"certificate_fingerprint", fp},
                {"certificate_pem", cert_pem_.empty() ? "(will be generated)" : "(embedded)"},
                {"private_key_pem", key_pem_.empty() ? "(will be generated)" : "(embedded)"}
            }},
            {"providers", providers}
        };
        return root;
    }

    void UpdatePreview() {
        SetWindowTextW(preview_edit_, Utf8ToWide(BuildPreviewJson().dump(2)).c_str());
    }

    void OnCommand(int id) {
        switch (id) {
        case kGenerateSecret:
            OnGenerateSecret();
            UpdatePreview();
            return;
        case kGenerateCert:
            OnGenerateCert();
            UpdatePreview();
            return;
        case kSelectAll:
            std::fill(checked_.begin(), checked_.end(), true);
            InvalidateRect(providers_list_, nullptr, TRUE);
            UpdatePreview();
            return;
        case kDeselectAll:
            std::fill(checked_.begin(), checked_.end(), false);
            InvalidateRect(providers_list_, nullptr, TRUE);
            UpdatePreview();
            return;
        case kSaveButton: {
            auto path = PickSaveJsonPath();
            if (!path) return;
            RemoteProviderWorkerConfig config;
            config.source_path = *path;
            config.worker_name = "Agent Remote Worker";
            config.bind_address = WideToUtf8(GetWindowTextString(bind_address_edit_));
            config.https_port = ParseInt(GetWindowTextString(port_edit_)).value_or(8765);
            config.shared_secret = WideToUtf8(GetWindowTextString(secret_edit_));
            config.certificate_pem = cert_pem_;
            config.private_key_pem = key_pem_;
            config.certificate_fingerprint = WideToUtf8(GetWindowTextString(cert_edit_));
            for (size_t i = 0; i < providers_.size(); ++i) {
                if (i >= checked_.size() || !checked_[i]) continue;
                RemoteProviderWorkerExportedProvider exp;
                exp.provider = providers_[i];
                // Include OAuth auth record if applicable
                if (NormalizeProviderType(providers_[i].provider_type) == "openai_codex_oauth") {
                    if (!providers_[i].oauth_credential_id.empty()) {
                        auto ar = storage_->LoadProviderAuthRecord(providers_[i].oauth_credential_id);
                        if (ar) exp.auth_record = *ar;
                    }
                }
                config.exported_providers.push_back(std::move(exp));
            }
            std::string error;
            if (!SaveRemoteProviderWorkerConfig(config, &error)) {
                MessageBoxW(hwnd_, Utf8ToWide(error).c_str(), L"Save Failed", MB_OK | MB_ICONERROR);
                return;
            }
            result_path_ = *path;
            DestroyWindow(hwnd_);
            return;
        }
        case kCancelButton:
            DestroyWindow(hwnd_);
            return;
        }
    }

    std::optional<std::filesystem::path> PickSaveJsonPath() const {
        wchar_t path[MAX_PATH] = {};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd_;
        ofn.lpstrFile = path;
        ofn.nMaxFile = static_cast<DWORD>(std::size(path));
        ofn.lpstrFilter = L"Remote Worker JSON\0*.json\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
        ofn.lpstrDefExt = L"json";
        ofn.lpstrTitle = L"Save Remote Worker Configuration";
        if (!GetSaveFileNameW(&ofn)) return std::nullopt;
        return std::filesystem::path(path);
    }

    // Owner-draw checkboxes in listbox
    void DrawItem(const DRAWITEMSTRUCT* dis) {
        if (dis->itemID == static_cast<UINT>(-1)) return;
        HDC dc = dis->hDC;
        RECT rc = dis->rcItem;
        bool checked = (dis->itemID < checked_.size()) ? checked_[dis->itemID] : false;

        // Background
        FillRect(dc, &rc, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        if (dis->itemState & ODS_SELECTED) {
            FillRect(dc, &rc, reinterpret_cast<HBRUSH>(COLOR_HIGHLIGHT + 1));
            SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
        } else {
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        }

        // Checkbox
        int cb_size = Scale(14);
        RECT cb_rc = { rc.left + Scale(4), rc.top + (rc.bottom - rc.top - cb_size) / 2,
                       rc.left + Scale(4) + cb_size, rc.top + (rc.bottom - rc.top - cb_size) / 2 + cb_size };
        DrawFrameControl(dc, &cb_rc, DFC_BUTTON, DFCS_BUTTONCHECK | (checked ? DFCS_CHECKED : 0));

        // Text
        rc.left += Scale(24);
        wchar_t text[256] = {};
        SendMessageW(providers_list_, LB_GETTEXT, dis->itemID, reinterpret_cast<LPARAM>(text));
        DrawTextW(dc, text, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }

    void OnMeasureItem(MEASUREITEMSTRUCT* mis) {
        mis->itemHeight = Scale(20);
    }

    void OnListClick(int x, int y) {
        int idx = static_cast<int>(SendMessageW(providers_list_, LB_ITEMFROMPOINT, 0, MAKELPARAM(x, y)));
        if (idx >= 0 && idx < static_cast<int>(checked_.size())) {
            // Toggle only if clicking the checkbox region
            RECT rc;
            SendMessageW(providers_list_, LB_GETITEMRECT, idx, reinterpret_cast<LPARAM>(&rc));
            if (x <= Scale(24)) {
                checked_[idx] = !checked_[idx];
                InvalidateRect(providers_list_, &rc, TRUE);
                UpdatePreview();
            } else {
                SendMessageW(providers_list_, LB_SETCURSEL, idx, 0);
            }
        }
    }

    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    AppStorage* storage_ = nullptr;
    std::vector<ProviderConfig> providers_;
    HFONT font_ = nullptr;

    HWND bind_address_label_ = nullptr;
    HWND bind_address_edit_ = nullptr;
    HWND port_label_ = nullptr;
    HWND port_edit_ = nullptr;
    HWND secret_label_ = nullptr;
    HWND secret_edit_ = nullptr;
    HWND generate_secret_button_ = nullptr;
    HWND cert_label_ = nullptr;
    HWND cert_edit_ = nullptr;
    HWND generate_cert_button_ = nullptr;
    HWND providers_label_ = nullptr;
    HWND providers_list_ = nullptr;
    HWND select_all_button_ = nullptr;
    HWND deselect_all_button_ = nullptr;
    HWND preview_label_ = nullptr;
    HWND preview_edit_ = nullptr;
    HWND save_button_ = nullptr;
    HWND cancel_button_ = nullptr;

    std::vector<bool> checked_;
    std::string cert_pem_;
    std::string key_pem_;
    std::optional<std::filesystem::path> result_path_;
};

} // namespace

std::optional<std::filesystem::path> ShowRemoteWorkerExportDialog(
    HWND owner,
    AppStorage* storage,
    const std::vector<ProviderConfig>& providers) {
    ExportDialog dialog(owner, storage, providers);
    return dialog.Run();
}
