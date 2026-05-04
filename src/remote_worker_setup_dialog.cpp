#include "remote_worker_setup_dialog.h"

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

constexpr wchar_t kClassName[] = L"AgentRemoteWorkerSetupDialog";
constexpr int kDialogWidth = 900;
constexpr int kDialogHeight = 560;

enum ControlId : int {
    kWorkerNameLabel = 8622,
    kWorkerNameEdit = 8623,
    kLoadJson = 8601,
    kSaveJson = 8602,
    kSaveAsJson = 8603,
    kBindAddressLabel = 8604,
    kBindAddressEdit = 8605,
    kPortLabel = 8606,
    kPortEdit = 8607,
    kSecretLabel = 8608,
    kSecretEdit = 8609,
    kGenerateSecret = 8610,
    kCertLabel = 8611,
    kCertEdit = 8612,
    kGenerateCert = 8613,
    kProvidersLabel = 8614,
    kProvidersList = 8615,
    kModelsLabel = 8616,
    kModelsList = 8617,
    kSelectAll = 8618,
    kDeselectAll = 8619,
    kPreviewLabel = 8620,
    kPreviewEdit = 8621,
    kCloseButton = IDCANCEL,
};

class RemoteWorkerSetupDialog {
public:
    RemoteWorkerSetupDialog(HWND owner, AppStorage* storage,
                            std::vector<ProviderConfig>* providers)
        : owner_(owner), storage_(storage), providers_(providers) {}

    static void Open(HWND owner, AppStorage* storage,
                     std::vector<ProviderConfig>* providers) {
        auto* dialog = new RemoteWorkerSetupDialog(owner, storage, providers);
        dialog->Create();
    }

private:
    void Create() {
        HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
        RegisterWindowClass(instance);
        hwnd_ = CreateWindowExW(
            WS_EX_APPWINDOW,
            kClassName,
            L"Remote Model Config",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT,
            kDialogWidth, kDialogHeight,
            owner_,
            nullptr,
            instance,
            this);
    }

    static void RegisterWindowClass(HINSTANCE instance) {
        static bool registered = false;
        if (registered) return;
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = &RemoteWorkerSetupDialog::WindowProc;
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        RegisterClassExW(&wc);
        registered = true;
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message,
                                        WPARAM w_param, LPARAM l_param) {
        auto* self = reinterpret_cast<RemoteWorkerSetupDialog*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
            self = reinterpret_cast<RemoteWorkerSetupDialog*>(
                create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(self));
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
            if (LOWORD(w_param) == kModelsList &&
                HIWORD(w_param) == LBN_DBLCLK) {
                POINT pt{};
                GetCursorPos(&pt);
                ScreenToClient(self->models_list_, &pt);
                self->OnModelListClick(pt.x, pt.y);
            } else {
                self->OnCommand(LOWORD(w_param));
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            delete self;
            return 0;
        default:
            return DefWindowProcW(hwnd, message, w_param, l_param);
        }
    }

    int Scale(int value) const {
        return MulDiv(value, GetDpiForWindow(hwnd_), 96);
    }

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
        load_button_ = MakeButton(L"Load JSON", kLoadJson);
        save_button_ = MakeButton(L"Save JSON", kSaveJson);
        save_as_button_ = MakeButton(L"Save As", kSaveAsJson);

        worker_name_label_ = MakeLabel(L"Remote name:", kWorkerNameLabel);
        worker_name_edit_ = MakeEdit(L"Agent Remote Worker", kWorkerNameEdit);

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

        providers_label_ = MakeLabel(L"Providers:", kProvidersLabel);
        providers_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kProvidersList), nullptr, nullptr);

        models_label_ = MakeLabel(L"Models to export:", kModelsLabel);
        models_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY |
            LBS_HASSTRINGS | LBS_OWNERDRAWFIXED,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kModelsList), nullptr, nullptr);

        select_all_button_ = MakeButton(L"Select All", kSelectAll);
        deselect_all_button_ = MakeButton(L"Deselect All", kDeselectAll);

        preview_label_ = MakeLabel(L"Preview JSON:", kPreviewLabel);
        preview_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kPreviewEdit), nullptr, nullptr);

        close_button_ = MakeButton(L"Close", kCloseButton);

        for (HWND ctl : {load_button_, save_button_, save_as_button_,
                         worker_name_label_, worker_name_edit_,
                         bind_address_label_, bind_address_edit_, port_label_, port_edit_,
                         secret_label_, secret_edit_, generate_secret_button_,
                         cert_label_, cert_edit_, generate_cert_button_,
                         providers_label_, providers_list_, models_label_, models_list_,
                         select_all_button_, deselect_all_button_,
                         preview_label_, preview_edit_, close_button_}) {
            SendMessageW(ctl, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }

        // Populate providers list
        for (size_t i = 0; i < providers_->size(); ++i) {
            const auto& p = providers_->at(i);
            std::wstring label = Utf8ToWide(p.name + "  [" +
                NormalizeProviderType(p.provider_type) + "]");
            int idx = static_cast<int>(
                SendMessageW(providers_list_, LB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(label.c_str())));
            if (idx >= 0) {
                SendMessageW(providers_list_, LB_SETITEMDATA, idx,
                             static_cast<LPARAM>(i));
            }
        }
        ResizeModelChecked();
        if (!providers_->empty()) {
            SendMessageW(providers_list_, LB_SETCURSEL, 0, 0);
            RefreshModelsList();
        }

        OnGenerateSecret();
        UpdatePreview();
        LayoutControls(kDialogWidth, kDialogHeight);
        SetFocus(providers_list_);
    }

    void LayoutControls(int width, int height) const {
        const int margin = Scale(14);
        const int gutter = Scale(10);
        const int row_h = Scale(24);
        const int btn_h = Scale(26);
        const int mid = width / 2;

        int y = margin;
        MoveWindow(load_button_, margin, y, Scale(90), btn_h, TRUE);
        MoveWindow(save_button_, margin + Scale(100), y, Scale(90), btn_h, TRUE);
        MoveWindow(save_as_button_, margin + Scale(200), y, Scale(90), btn_h, TRUE);
        y += btn_h + gutter;

        MoveWindow(worker_name_label_, margin, y, Scale(90), row_h, TRUE);
        MoveWindow(worker_name_edit_, margin + Scale(100), y,
                   width - margin * 2 - Scale(100), row_h, TRUE);
        y += row_h + gutter;

        MoveWindow(bind_address_label_, margin, y, Scale(90), row_h, TRUE);
        MoveWindow(bind_address_edit_, margin + Scale(100), y,
                   mid - margin - Scale(100) - gutter, row_h, TRUE);
        MoveWindow(port_label_, mid + gutter, y, Scale(70), row_h, TRUE);
        MoveWindow(port_edit_, mid + gutter + Scale(80), y,
                   mid - margin - Scale(80) - gutter, row_h, TRUE);
        y += row_h + gutter;

        MoveWindow(secret_label_, margin, y, Scale(100), row_h, TRUE);
        MoveWindow(secret_edit_, margin + Scale(110), y,
                   mid - margin - Scale(110) - Scale(90) - gutter * 2, row_h, TRUE);
        MoveWindow(generate_secret_button_, mid - Scale(90), y, Scale(90), row_h, TRUE);
        y += row_h + gutter;

        MoveWindow(cert_label_, margin, y, Scale(160), row_h, TRUE);
        MoveWindow(cert_edit_, margin + Scale(170), y,
                   mid - margin - Scale(170) - Scale(130) - gutter * 2, row_h, TRUE);
        MoveWindow(generate_cert_button_, mid - Scale(130), y, Scale(130), row_h, TRUE);
        y += row_h + gutter * 2;

        int list_top = y;
        int list_h = height - y - margin - btn_h - gutter * 3 - Scale(120);
        MoveWindow(providers_label_, margin, y, mid - margin - gutter, row_h, TRUE);
        y += row_h + gutter;
        MoveWindow(providers_list_, margin, y, mid - margin - gutter, list_h, TRUE);

        MoveWindow(models_label_, mid + gutter, list_top,
                   mid - margin - gutter, row_h, TRUE);
        MoveWindow(models_list_, mid + gutter, list_top + row_h + gutter,
                   mid - margin - gutter, list_h, TRUE);
        MoveWindow(select_all_button_, mid + gutter,
                   list_top + row_h + gutter + list_h + gutter,
                   Scale(90), btn_h, TRUE);
        MoveWindow(deselect_all_button_, mid + gutter + Scale(100),
                   list_top + row_h + gutter + list_h + gutter,
                   Scale(90), btn_h, TRUE);

        MoveWindow(preview_label_, margin,
                   list_top + row_h + gutter + list_h + gutter, width - margin * 2, row_h, TRUE);
        MoveWindow(preview_edit_, margin,
                   list_top + row_h + gutter + list_h + gutter + row_h + gutter,
                   width - margin * 2, Scale(120), TRUE);

        int footer_y = height - margin - btn_h;
        MoveWindow(close_button_, width - margin - Scale(90), footer_y,
                   Scale(90), btn_h, TRUE);
    }

    void OnGenerateSecret() {
        SetWindowTextW(secret_edit_,
            Utf8ToWide(GenerateRemoteProviderWorkerSharedSecret()).c_str());
    }

    void OnGenerateCert() {
        RemoteProviderWorkerCertificateMaterial mat;
        std::string error;
        if (!GenerateRemoteProviderWorkerSelfSignedCertificateMaterial(
                &mat, &error)) {
            MessageBoxW(hwnd_, Utf8ToWide(error).c_str(),
                        L"Certificate Generation Failed",
                        MB_OK | MB_ICONERROR);
            return;
        }
        cert_pem_ = mat.certificate_pem;
        key_pem_ = mat.private_key_pem;
        SetWindowTextW(cert_edit_, Utf8ToWide(mat.fingerprint).c_str());
    }

    void RefreshModelsList() {
        SendMessageW(models_list_, LB_RESETCONTENT, 0, 0);
        model_items_.clear();
        int prov_sel = static_cast<int>(
            SendMessageW(providers_list_, LB_GETCURSEL, 0, 0));
        if (prov_sel < 0 || prov_sel >= static_cast<int>(providers_->size()))
            return;

        const auto& prov = providers_->at(prov_sel);
        for (size_t mi = 0; mi < prov.models.size(); ++mi) {
            const auto& m = prov.models[mi];
            std::wstring label = Utf8ToWide(m.display_name + "  (" + m.id + ")");
            int idx = static_cast<int>(
                SendMessageW(models_list_, LB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(label.c_str())));
            if (idx >= 0) {
                ModelItem item;
                item.provider_index = prov_sel;
                item.model_index = static_cast<int>(mi);
                item.checked = model_checked_[prov_sel][mi];
                model_items_.push_back(item);
            }
        }
    }

    void OnProviderSelChanged() {
        RefreshModelsList();
    }

    void ResizeModelChecked() {
        model_checked_.resize(providers_->size());
        for (size_t i = 0; i < providers_->size(); ++i) {
            model_checked_[i].resize(providers_->at(i).models.size(), false);
        }
    }

    json BuildPreviewJson() const {
        json providers = json::array();
        for (size_t pi = 0; pi < providers_->size(); ++pi) {
            bool any_checked = false;
            json models = json::array();
            for (size_t mi = 0; mi < providers_->at(pi).models.size(); ++mi) {
                if (model_checked_[pi][mi]) {
                    any_checked = true;
                    const auto& m = providers_->at(pi).models[mi];
                    json model_json = {
                        {"id", m.id},
                        {"display_name", m.display_name},
                        {"context_window", m.context_window},
                        {"max_output_tokens", m.max_output_tokens},
                        {"supports_streaming", m.supports_streaming},
                        {"supports_tools", m.supports_tools},
                        {"supports_vision", m.supports_vision},
                        {"supports_embedding", m.supports_embedding},
                        {"supports_thinking", m.supports_thinking},
                        {"max_active_requests", m.max_active_requests},
                        {"max_queue_size", m.max_queue_size},
                        {"self_managed_queue", m.self_managed_queue},
                        {"is_binding_model", m.is_binding_model},
                        {"binding_routing_mode",
                         m.binding_routing_mode == BindingRoutingMode::RoundRobin
                             ? "round_robin"
                             : "top_down_failover"}};
                    json targets = json::array();
                    for (const auto& t : m.binding_targets) {
                        targets.push_back({
                            {"provider_id", t.provider_id},
                            {"model_id", t.model_id},
                            {"enabled", t.enabled},
                            {"priority", t.priority},
                            {"busy_retry_interval_seconds",
                             t.busy_retry_interval_seconds},
                            {"busy_retry_budget_seconds",
                             t.busy_retry_budget_seconds},
                            {"busy_cooldown_seconds", t.busy_cooldown_seconds},
                            {"limit_cooldown_seconds",
                             t.limit_cooldown_seconds},
                            {"error_cooldown_seconds",
                             t.error_cooldown_seconds}});
                    }
                    model_json["binding_targets"] = std::move(targets);
                    models.push_back(std::move(model_json));
                }
            }
            // Auto-include binding targets on their target providers
            for (size_t mi = 0; mi < providers_->at(pi).models.size(); ++mi) {
                if (!providers_->at(pi).models[mi].is_binding_model) continue;
                if (!model_checked_[pi][mi]) continue;
                for (const auto& target :
                     providers_->at(pi).models[mi].binding_targets) {
                    // Find provider/model from global list
                    for (size_t tpi = 0; tpi < providers_->size(); ++tpi) {
                        for (size_t tmi = 0;
                             tmi < providers_->at(tpi).models.size(); ++tmi) {
                            if (providers_->at(tpi).models[tmi].id ==
                                    target.model_id &&
                                providers_->at(tpi).id == target.provider_id) {
                                bool already = false;
                                for (const auto& existing : models) {
                                    if (existing.value("id", "") ==
                                        target.model_id) {
                                        already = true;
                                        break;
                                    }
                                }
                                if (!already) {
                                    const auto& tm =
                                        providers_->at(tpi).models[tmi];
                                    models.push_back({
                                        {"id", tm.id},
                                        {"display_name", tm.display_name},
                                        {"context_window", tm.context_window},
                                        {"max_output_tokens",
                                         tm.max_output_tokens},
                                        {"supports_streaming",
                                         tm.supports_streaming},
                                        {"supports_tools", tm.supports_tools},
                                        {"supports_vision",
                                         tm.supports_vision},
                                        {"supports_embedding",
                                         tm.supports_embedding},
                                        {"supports_thinking",
                                         tm.supports_thinking},
                                        {"max_active_requests",
                                         tm.max_active_requests},
                                        {"max_queue_size",
                                         tm.max_queue_size},
                                        {"self_managed_queue",
                                         tm.self_managed_queue}});
                                }
                            }
                        }
                    }
                }
            }
            if (!any_checked) continue;
            json prov = {
                {"id", providers_->at(pi).id},
                {"name", providers_->at(pi).name},
                {"provider_type", providers_->at(pi).provider_type},
                {"base_url", providers_->at(pi).base_url},
                {"api_key", providers_->at(pi).api_key},
                {"tls_certificate_fingerprint",
                 providers_->at(pi).tls_certificate_fingerprint},
                {"auth_mode", providers_->at(pi).auth_mode},
                {"model_catalog_mode", providers_->at(pi).model_catalog_mode},
                {"models", models}};
            json entry = json{{"provider", prov}};
            if (NormalizeProviderType(providers_->at(pi).provider_type) ==
                "openai_codex_oauth") {
                if (!providers_->at(pi).oauth_credential_id.empty()) {
                    auto ar = storage_->LoadProviderAuthRecord(
                        providers_->at(pi).oauth_credential_id);
                    if (ar) {
                        entry["auth_record"] = json{
                            {"credential_id", ar->credential_id},
                            {"provider_id", ar->provider_id},
                            {"auth_mode", ar->auth_mode},
                            {"api_key", ar->api_key},
                            {"access_token", ar->access_token},
                            {"refresh_token", ar->refresh_token},
                            {"token_type", ar->token_type},
                            {"account_id", ar->account_id},
                            {"account_email", ar->account_email},
                            {"account_display_name",
                             ar->account_display_name},
                            {"scope", ar->scope},
                            {"expires_at", ar->expires_at}};
                    }
                }
            }
            providers.push_back(entry);
        }
        json root = {
            {"version", 2},
            {"worker_name",
             WideToUtf8(GetWindowTextString(worker_name_edit_))},
            {"agent_server",
             {{"bind_address",
               WideToUtf8(GetWindowTextString(bind_address_edit_))},
              {"https_port",
               ParseInt(GetWindowTextString(port_edit_)).value_or(8765)},
              {"shared_secret",
               WideToUtf8(GetWindowTextString(secret_edit_))},
              {"certificate_pem", cert_pem_},
              {"private_key_pem", key_pem_},
              {"certificate_fingerprint",
               WideToUtf8(GetWindowTextString(cert_edit_))}}},
            {"providers", providers}};
        return root;
    }

    void UpdatePreview() {
        SetWindowTextW(preview_edit_,
                       Utf8ToWide(BuildPreviewJson().dump(2)).c_str());
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
        case kProvidersList:
            OnProviderSelChanged();
            return;
        case kSelectAll:
            SelectAllModels(true);
            return;
        case kDeselectAll:
            SelectAllModels(false);
            return;
        case kLoadJson:
            LoadJson();
            return;
        case kSaveJson:
            SaveJson(false);
            return;
        case kSaveAsJson:
            SaveJson(true);
            return;
        case kCloseButton:
            DestroyWindow(hwnd_);
            return;
        }
    }

    void SelectAllModels(bool checked) {
        int prov_sel = static_cast<int>(
            SendMessageW(providers_list_, LB_GETCURSEL, 0, 0));
        if (prov_sel < 0 || prov_sel >= static_cast<int>(providers_->size()))
            return;
        for (size_t mi = 0; mi < model_checked_[prov_sel].size(); ++mi) {
            model_checked_[prov_sel][mi] = checked;
        }
        RefreshModelsList();
        UpdatePreview();
    }

    void OnModelListClick(int x, int y) {
        int idx = static_cast<int>(
            SendMessageW(models_list_, LB_ITEMFROMPOINT, 0,
                         MAKELPARAM(x, y)));
        if (idx >= 0 && idx < static_cast<int>(model_items_.size())) {
            RECT rc;
            SendMessageW(models_list_, LB_GETITEMRECT, idx,
                         reinterpret_cast<LPARAM>(&rc));
            if (x <= Scale(24)) {
                auto& item = model_items_[idx];
                item.checked = !item.checked;
                model_checked_[item.provider_index][item.model_index] =
                    item.checked;
                InvalidateRect(models_list_, &rc, TRUE);
                UpdatePreview();
            } else {
                SendMessageW(models_list_, LB_SETCURSEL, idx, 0);
            }
        }
    }

    void DrawItem(const DRAWITEMSTRUCT* dis) {
        if (dis->itemID == static_cast<UINT>(-1)) return;
        HDC dc = dis->hDC;
        RECT rc = dis->rcItem;
        bool checked = false;
        if (dis->itemID < model_items_.size())
            checked = model_items_[dis->itemID].checked;

        FillRect(dc, &rc,
                 reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        if (dis->itemState & ODS_SELECTED) {
            FillRect(dc, &rc,
                     reinterpret_cast<HBRUSH>(COLOR_HIGHLIGHT + 1));
            SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
        } else {
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        }

        int cb_size = Scale(14);
        RECT cb_rc = {
            rc.left + Scale(4),
            rc.top + (rc.bottom - rc.top - cb_size) / 2,
            rc.left + Scale(4) + cb_size,
            rc.top + (rc.bottom - rc.top - cb_size) / 2 + cb_size};
        DrawFrameControl(dc, &cb_rc, DFC_BUTTON,
                         DFCS_BUTTONCHECK |
                             (checked ? DFCS_CHECKED : 0));

        rc.left += Scale(24);
        wchar_t text[256] = {};
        SendMessageW(models_list_, LB_GETTEXT, dis->itemID,
                     reinterpret_cast<LPARAM>(text));
        DrawTextW(dc, text, -1, &rc,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }

    void OnMeasureItem(MEASUREITEMSTRUCT* mis) {
        mis->itemHeight = Scale(20);
    }

    std::optional<std::filesystem::path> PickLoadPath() const {
        wchar_t path[MAX_PATH] = {};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd_;
        ofn.lpstrFile = path;
        ofn.nMaxFile = static_cast<DWORD>(std::size(path));
        ofn.lpstrFilter = L"Remote Worker JSON\0*.json\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                    OFN_HIDEREADONLY;
        ofn.lpstrTitle = L"Load Remote Worker JSON";
        if (!GetOpenFileNameW(&ofn)) return std::nullopt;
        return std::filesystem::path(path);
    }

    std::optional<std::filesystem::path> PickSavePath() const {
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
        ofn.lpstrTitle = L"Save Remote Worker JSON";
        if (!GetSaveFileNameW(&ofn)) return std::nullopt;
        return std::filesystem::path(path);
    }

    void LoadJson() {
        auto path = PickLoadPath();
        if (!path) return;
        std::string error;
        auto config = LoadRemoteProviderWorkerConfig(*path, &error);
        if (!config) {
            MessageBoxW(hwnd_, Utf8ToWide(error).c_str(),
                        L"Load Failed", MB_OK | MB_ICONERROR);
            return;
        }
        SetWindowTextW(bind_address_edit_,
                          Utf8ToWide(config->bind_address).c_str());
        SetWindowTextW(port_edit_,
                          std::to_wstring(config->https_port).c_str());
        SetWindowTextW(secret_edit_,
                          Utf8ToWide(config->shared_secret).c_str());
        SetWindowTextW(worker_name_edit_,
                          Utf8ToWide(config->worker_name).c_str());
        cert_pem_ = config->certificate_pem;
        key_pem_ = config->private_key_pem;
        SetWindowTextW(cert_edit_,
                          Utf8ToWide(config->certificate_fingerprint).c_str());

        // Clear all checks
        for (auto& prov_checks : model_checked_)
            std::fill(prov_checks.begin(), prov_checks.end(), false);

        for (const auto& exp : config->exported_providers) {
            for (size_t pi = 0; pi < providers_->size(); ++pi) {
                if (providers_->at(pi).id != exp.provider.id) continue;
                for (const auto& m : exp.provider.models) {
                    for (size_t mi = 0;
                         mi < providers_->at(pi).models.size(); ++mi) {
                        if (providers_->at(pi).models[mi].id == m.id) {
                            model_checked_[pi][mi] = true;
                        }
                    }
                }
            }
        }
        RefreshModelsList();
        UpdatePreview();
    }

    void SaveJson(bool save_as) {
        if (save_as || current_json_path_.empty()) {
            auto path = PickSavePath();
            if (!path) return;
            current_json_path_ = *path;
        }
        RemoteProviderWorkerConfig config;
        config.source_path = current_json_path_;
        config.worker_name = WideToUtf8(GetWindowTextString(worker_name_edit_));
        config.bind_address =
            WideToUtf8(GetWindowTextString(bind_address_edit_));
        config.https_port =
            ParseInt(GetWindowTextString(port_edit_)).value_or(8765);
        config.shared_secret =
            WideToUtf8(GetWindowTextString(secret_edit_));
        config.certificate_pem = cert_pem_;
        config.private_key_pem = key_pem_;
        config.certificate_fingerprint =
            WideToUtf8(GetWindowTextString(cert_edit_));
        for (size_t pi = 0; pi < providers_->size(); ++pi) {
            bool any = false;
            for (bool c : model_checked_[pi])
                if (c) any = true;
            if (!any) continue;
            RemoteProviderWorkerExportedProvider exp;
            exp.provider = providers_->at(pi);
            exp.provider.models.clear();
            for (size_t mi = 0; mi < providers_->at(pi).models.size(); ++mi) {
                if (model_checked_[pi][mi])
                    exp.provider.models.push_back(
                        providers_->at(pi).models[mi]);
            }
            if (NormalizeProviderType(providers_->at(pi).provider_type) ==
                "openai_codex_oauth") {
                if (!providers_->at(pi).oauth_credential_id.empty()) {
                    auto ar = storage_->LoadProviderAuthRecord(
                        providers_->at(pi).oauth_credential_id);
                    if (ar) exp.auth_record = *ar;
                }
            }
            config.exported_providers.push_back(std::move(exp));
        }
        std::string error;
        if (!SaveRemoteProviderWorkerConfig(config, &error)) {
            MessageBoxW(hwnd_, Utf8ToWide(error).c_str(),
                        L"Save Failed", MB_OK | MB_ICONERROR);
            return;
        }
        UpdatePreview();
    }

    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    AppStorage* storage_ = nullptr;
    std::vector<ProviderConfig>* providers_ = nullptr;
    HFONT font_ = nullptr;

    HWND load_button_ = nullptr;
    HWND save_button_ = nullptr;
    HWND save_as_button_ = nullptr;
    HWND worker_name_label_ = nullptr;
    HWND worker_name_edit_ = nullptr;
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
    HWND models_label_ = nullptr;
    HWND models_list_ = nullptr;
    HWND select_all_button_ = nullptr;
    HWND deselect_all_button_ = nullptr;
    HWND preview_label_ = nullptr;
    HWND preview_edit_ = nullptr;
    HWND close_button_ = nullptr;

    struct ModelItem {
        int provider_index = -1;
        int model_index = -1;
        bool checked = false;
    };
    std::vector<ModelItem> model_items_;
    std::vector<std::vector<bool>> model_checked_;
    std::string cert_pem_;
    std::string key_pem_;
    std::filesystem::path current_json_path_;
};

} // namespace

void ShowRemoteOllamaSetupDialog(HWND owner, AppStorage* storage,
                                 std::vector<ProviderConfig>* providers) {
    RemoteWorkerSetupDialog::Open(owner, storage, providers);
}
