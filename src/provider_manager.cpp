#include "provider_manager.h"

#include "openai_client.h"
#include "prompt_dialog.h"
#include "util.h"

#include <memory>
#include <thread>
#include <windowsx.h>

namespace {
constexpr wchar_t kProviderManagerClassName[] = L"AgentProviderManagerWindow";
constexpr UINT kProviderTestResultMessage = WM_APP + 100;

enum ControlId : int {
    kProvidersList = 2001,
    kModelsList = 2002,
    kAddProvider = 2003,
    kEditProvider = 2004,
    kRemoveProvider = 2005,
    kAddModel = 2006,
    kEditModel = 2007,
    kRemoveModel = 2008,
    kTestConnection = 2009,
    kStatusLabel = 2010,
    kCloseButton = 2011,
};

struct ProviderTestPayload {
    bool success = false;
    std::wstring message;
};

class ProviderManagerWindow {
public:
    ProviderManagerWindow(HWND owner, AppStorage* storage, std::vector<ProviderConfig>* providers, std::function<void()> on_changed)
        : owner_(owner), storage_(storage), providers_(providers), on_changed_(std::move(on_changed)) {}

    HWND Create(HINSTANCE instance) {
        RegisterWindowClass(instance);
        hwnd_ = CreateWindowExW(
            WS_EX_APPWINDOW,
            kProviderManagerClassName,
            L"Provider Manager",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            900,
            520,
            owner_,
            nullptr,
            instance,
            this);
        return hwnd_;
    }

    static void RegisterWindowClass(HINSTANCE instance) {
        static bool registered = false;
        if (registered) {
            return;
        }

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = &ProviderManagerWindow::WindowProc;
        wc.lpszClassName = kProviderManagerClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        RegisterClassExW(&wc);
        registered = true;
    }

private:
    static int Scale(HWND hwnd, int value) {
        return MulDiv(value, GetDpiForWindow(hwnd), 96);
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
        ProviderManagerWindow* self = reinterpret_cast<ProviderManagerWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
            self = reinterpret_cast<ProviderManagerWindow*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }

        if (!self) {
            return DefWindowProcW(hwnd, message, w_param, l_param);
        }

        switch (message) {
        case WM_CREATE:
            self->OnCreate();
            return 0;
        case WM_SIZE:
            self->LayoutControls(LOWORD(l_param), HIWORD(l_param));
            return 0;
        case WM_COMMAND:
            self->OnCommand(LOWORD(w_param), HIWORD(w_param));
            return 0;
        case kProviderTestResultMessage:
            self->OnTestCompleted(reinterpret_cast<ProviderTestPayload*>(l_param));
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

    void OnCreate() {
        font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        providers_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kProvidersList), nullptr, nullptr);
        models_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kModelsList), nullptr, nullptr);

        add_provider_button_ = CreateWindowExW(0, L"BUTTON", L"Add Provider", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kAddProvider), nullptr, nullptr);
        edit_provider_button_ = CreateWindowExW(0, L"BUTTON", L"Edit Provider", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditProvider), nullptr, nullptr);
        remove_provider_button_ = CreateWindowExW(0, L"BUTTON", L"Remove Provider", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRemoveProvider), nullptr, nullptr);
        add_model_button_ = CreateWindowExW(0, L"BUTTON", L"Add Model", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kAddModel), nullptr, nullptr);
        edit_model_button_ = CreateWindowExW(0, L"BUTTON", L"Edit Model", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditModel), nullptr, nullptr);
        remove_model_button_ = CreateWindowExW(0, L"BUTTON", L"Remove Model", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRemoveModel), nullptr, nullptr);
        test_connection_button_ = CreateWindowExW(0, L"BUTTON", L"Test Connection", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kTestConnection), nullptr, nullptr);
        close_button_ = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kCloseButton), nullptr, nullptr);
        status_label_ = CreateWindowExW(0, L"STATIC", L"Select a provider and model to test the connection.", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kStatusLabel), nullptr, nullptr);

        for (HWND control : {providers_list_, models_list_, add_provider_button_, edit_provider_button_, remove_provider_button_, add_model_button_, edit_model_button_, remove_model_button_, test_connection_button_, close_button_, status_label_}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }

        RefreshProviders();
    }

    void LayoutControls(int width, int height) {
        const int margin = Scale(hwnd_, 12);
        const int button_height = Scale(hwnd_, 28);
        const int status_height = Scale(hwnd_, 22);
        const int gutter = Scale(hwnd_, 12);
        const int list_top = margin;
        const int lists_height = height - margin * 2 - button_height * 2 - status_height - gutter * 3;
        const int list_width = (width - margin * 2 - gutter) / 2;

        MoveWindow(providers_list_, margin, list_top, list_width, lists_height, TRUE);
        MoveWindow(models_list_, margin + list_width + gutter, list_top, list_width, lists_height, TRUE);

        const int provider_buttons_top = list_top + lists_height + gutter;
        const int model_buttons_top = provider_buttons_top;
        const int small_button_width = (list_width - gutter * 2) / 3;

        MoveWindow(add_provider_button_, margin, provider_buttons_top, small_button_width, button_height, TRUE);
        MoveWindow(edit_provider_button_, margin + small_button_width + gutter, provider_buttons_top, small_button_width, button_height, TRUE);
        MoveWindow(remove_provider_button_, margin + (small_button_width + gutter) * 2, provider_buttons_top, small_button_width, button_height, TRUE);

        MoveWindow(add_model_button_, margin + list_width + gutter, model_buttons_top, small_button_width, button_height, TRUE);
        MoveWindow(edit_model_button_, margin + list_width + gutter + small_button_width + gutter, model_buttons_top, small_button_width, button_height, TRUE);
        MoveWindow(remove_model_button_, margin + list_width + gutter + (small_button_width + gutter) * 2, model_buttons_top, small_button_width, button_height, TRUE);

        const int footer_top = provider_buttons_top + button_height + gutter;
        MoveWindow(status_label_, margin, footer_top, width - margin * 2 - Scale(hwnd_, 220), status_height, TRUE);
        MoveWindow(test_connection_button_, width - margin - Scale(hwnd_, 210), footer_top - Scale(hwnd_, 4), Scale(hwnd_, 110), button_height, TRUE);
        MoveWindow(close_button_, width - margin - Scale(hwnd_, 90), footer_top - Scale(hwnd_, 4), Scale(hwnd_, 90), button_height, TRUE);
    }

    void RefreshProviders() {
        SendMessageW(providers_list_, LB_RESETCONTENT, 0, 0);
        for (const auto& provider : *providers_) {
            const std::wstring label = Utf8ToWide(provider.name + "  [" + provider.base_url + "]");
            SendMessageW(providers_list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        if (!providers_->empty()) {
            SendMessageW(providers_list_, LB_SETCURSEL, 0, 0);
        }
        RefreshModels();
    }

    void RefreshModels() {
        SendMessageW(models_list_, LB_RESETCONTENT, 0, 0);

        const int provider_index = SelectedProviderIndex();
        if (provider_index < 0) {
            return;
        }

        for (const auto& model : providers_->at(static_cast<size_t>(provider_index)).models) {
            std::wstring label = Utf8ToWide(model.display_name + " (" + model.id + ")");
            if (model.context_window > 0) {
                label += L"  ";
                label += std::to_wstring(model.context_window);
                label += L" ctx";
            }
            SendMessageW(models_list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }

        if (!providers_->at(static_cast<size_t>(provider_index)).models.empty()) {
            SendMessageW(models_list_, LB_SETCURSEL, 0, 0);
        }
    }

    int SelectedProviderIndex() const {
        const LRESULT selection = SendMessageW(providers_list_, LB_GETCURSEL, 0, 0);
        return selection == LB_ERR ? -1 : static_cast<int>(selection);
    }

    int SelectedModelIndex() const {
        const LRESULT selection = SendMessageW(models_list_, LB_GETCURSEL, 0, 0);
        return selection == LB_ERR ? -1 : static_cast<int>(selection);
    }

    void PersistAndNotify() {
        storage_->SaveProviders(*providers_);
        if (on_changed_) {
            on_changed_();
        }
    }

    static bool AskYesNo(HWND hwnd, const std::wstring& text, const std::wstring& title) {
        return MessageBoxW(hwnd, text.c_str(), title.c_str(), MB_YESNO | MB_ICONQUESTION) == IDYES;
    }

    bool PromptProviderFields(ProviderConfig& provider, bool editing) {
        auto name = ShowPromptDialog(hwnd_, PromptOptions{
                                             editing ? L"Edit Provider Name" : L"Add Provider",
                                             L"Provider name",
                                             Utf8ToWide(provider.name),
                                         });
        if (!name) {
            return false;
        }

        auto url = ShowPromptDialog(hwnd_, PromptOptions{
                                            editing ? L"Edit Provider URL" : L"Provider URL",
                                            L"Base URL (for example https://api.openai.com/v1)",
                                            Utf8ToWide(provider.base_url),
                                        });
        if (!url) {
            return false;
        }

        auto api_key = ShowPromptDialog(hwnd_, PromptOptions{
                                                editing ? L"Edit API Key" : L"Provider API Key",
                                                L"API key (leave blank for local endpoints)",
                                                Utf8ToWide(provider.api_key),
                                            });
        if (!api_key) {
            return false;
        }

        provider.name = WideToUtf8(*name);
        provider.base_url = WideToUtf8(*url);
        provider.api_key = WideToUtf8(*api_key);
        return true;
    }

    bool PromptModelFields(ModelConfig& model, bool editing) {
        auto model_id = ShowPromptDialog(hwnd_, PromptOptions{
                                                editing ? L"Edit Model ID" : L"Add Model",
                                                L"Model ID",
                                                Utf8ToWide(model.id),
                                            });
        if (!model_id) {
            return false;
        }

        auto display_name = ShowPromptDialog(hwnd_, PromptOptions{
                                                     editing ? L"Edit Display Name" : L"Model Display Name",
                                                     L"Display name",
                                                     Utf8ToWide(model.display_name),
                                                 });
        if (!display_name) {
            return false;
        }

        auto context_window = ShowPromptDialog(hwnd_, PromptOptions{
                                                       editing ? L"Edit Context Window" : L"Context Window",
                                                       L"Context window token limit (blank or 0 if unknown)",
                                                       model.context_window > 0 ? std::to_wstring(model.context_window) : L"",
                                                   });
        if (!context_window) {
            return false;
        }

        int parsed_context = 0;
        const std::wstring trimmed_context = TrimWide(*context_window);
        if (!trimmed_context.empty()) {
            const auto value = ParseInt(trimmed_context);
            if (!value || *value < 0) {
                MessageBoxW(hwnd_, L"Context window must be a positive whole number, 0, or blank if unknown.", L"Invalid Value", MB_OK | MB_ICONERROR);
                return false;
            }
            parsed_context = *value;
        }

        model.id = WideToUtf8(*model_id);
        model.display_name = WideToUtf8(*display_name);
        model.context_window = parsed_context;
        model.supports_streaming = AskYesNo(hwnd_, L"Should this model be marked as streaming-capable?", L"Streaming Support");
        model.supports_tools = AskYesNo(hwnd_, L"Should this model be marked as tool-capable?", L"Tool Support");
        model.supports_vision = AskYesNo(hwnd_, L"Should this model be marked as vision-capable?", L"Vision Support");
        return true;
    }

    void OnCommand(int control_id, int notification_code) {
        switch (control_id) {
        case kProvidersList:
            if (notification_code == LBN_SELCHANGE) {
                RefreshModels();
            }
            break;
        case kAddProvider:
            AddProvider();
            break;
        case kEditProvider:
            EditProvider();
            break;
        case kRemoveProvider:
            RemoveProvider();
            break;
        case kAddModel:
            AddModel();
            break;
        case kEditModel:
            EditModel();
            break;
        case kRemoveModel:
            RemoveModel();
            break;
        case kTestConnection:
            TestConnection();
            break;
        case kCloseButton:
            DestroyWindow(hwnd_);
            break;
        default:
            break;
        }
    }

    void AddProvider() {
        ProviderConfig provider;
        provider.id = MakeId("provider");
        if (!PromptProviderFields(provider, false)) {
            return;
        }

        providers_->push_back(provider);
        PersistAndNotify();
        RefreshProviders();
        SendMessageW(providers_list_, LB_SETCURSEL, static_cast<WPARAM>(providers_->size() - 1), 0);
        RefreshModels();
    }

    void EditProvider() {
        const int provider_index = SelectedProviderIndex();
        if (provider_index < 0) {
            return;
        }

        ProviderConfig provider = providers_->at(static_cast<size_t>(provider_index));
        if (!PromptProviderFields(provider, true)) {
            return;
        }

        providers_->at(static_cast<size_t>(provider_index)) = std::move(provider);
        PersistAndNotify();
        RefreshProviders();
        SendMessageW(providers_list_, LB_SETCURSEL, provider_index, 0);
        RefreshModels();
    }

    void RemoveProvider() {
        const int provider_index = SelectedProviderIndex();
        if (provider_index < 0) {
            return;
        }

        const std::wstring provider_name = Utf8ToWide(providers_->at(static_cast<size_t>(provider_index)).name);
        if (MessageBoxW(hwnd_, (L"Remove provider \"" + provider_name + L"\" and all of its models?").c_str(), L"Confirm Removal", MB_YESNO | MB_ICONWARNING) != IDYES) {
            return;
        }

        providers_->erase(providers_->begin() + provider_index);
        PersistAndNotify();
        RefreshProviders();
    }

    void AddModel() {
        const int provider_index = SelectedProviderIndex();
        if (provider_index < 0) {
            MessageBoxW(hwnd_, L"Select a provider before adding a model.", L"No Provider Selected", MB_OK | MB_ICONINFORMATION);
            return;
        }

        ModelConfig model;
        if (!PromptModelFields(model, false)) {
            return;
        }

        providers_->at(static_cast<size_t>(provider_index)).models.push_back(model);
        PersistAndNotify();
        RefreshModels();
    }

    void EditModel() {
        const int provider_index = SelectedProviderIndex();
        const int model_index = SelectedModelIndex();
        if (provider_index < 0 || model_index < 0) {
            return;
        }

        ModelConfig model = providers_->at(static_cast<size_t>(provider_index)).models.at(static_cast<size_t>(model_index));
        if (!PromptModelFields(model, true)) {
            return;
        }

        providers_->at(static_cast<size_t>(provider_index)).models.at(static_cast<size_t>(model_index)) = std::move(model);
        PersistAndNotify();
        RefreshModels();
        SendMessageW(models_list_, LB_SETCURSEL, model_index, 0);
    }

    void RemoveModel() {
        const int provider_index = SelectedProviderIndex();
        const int model_index = SelectedModelIndex();
        if (provider_index < 0 || model_index < 0) {
            return;
        }

        const auto& model = providers_->at(static_cast<size_t>(provider_index)).models.at(static_cast<size_t>(model_index));
        if (MessageBoxW(hwnd_, (L"Remove model \"" + Utf8ToWide(model.display_name) + L"\"?").c_str(), L"Confirm Removal", MB_YESNO | MB_ICONWARNING) != IDYES) {
            return;
        }

        auto& models = providers_->at(static_cast<size_t>(provider_index)).models;
        models.erase(models.begin() + model_index);
        PersistAndNotify();
        RefreshModels();
    }

    void TestConnection() {
        const int provider_index = SelectedProviderIndex();
        const int model_index = SelectedModelIndex();
        if (provider_index < 0 || model_index < 0) {
            MessageBoxW(hwnd_, L"Select both a provider and a model first.", L"Missing Selection", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const ProviderConfig provider = providers_->at(static_cast<size_t>(provider_index));
        const ModelConfig model = provider.models.at(static_cast<size_t>(model_index));

        EnableWindow(test_connection_button_, FALSE);
        SetWindowTextW(status_label_, L"Testing provider connection...");

        std::thread([hwnd = hwnd_, provider, model]() {
            const TestConnectionResult result = OpenAIClient::TestConnection(provider, model);
            auto* payload = new ProviderTestPayload;
            payload->success = result.success;
            payload->message = Utf8ToWide(result.message);
            PostMessageW(hwnd, kProviderTestResultMessage, 0, reinterpret_cast<LPARAM>(payload));
        }).detach();
    }

    void OnTestCompleted(ProviderTestPayload* payload) {
        std::unique_ptr<ProviderTestPayload> guard(payload);
        EnableWindow(test_connection_button_, TRUE);
        SetWindowTextW(status_label_, payload->message.c_str());
        MessageBoxW(hwnd_, payload->message.c_str(), payload->success ? L"Connection Successful" : L"Connection Failed", MB_OK | (payload->success ? MB_ICONINFORMATION : MB_ICONERROR));
    }

    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    AppStorage* storage_ = nullptr;
    std::vector<ProviderConfig>* providers_ = nullptr;
    std::function<void()> on_changed_;

    HFONT font_ = nullptr;
    HWND providers_list_ = nullptr;
    HWND models_list_ = nullptr;
    HWND add_provider_button_ = nullptr;
    HWND edit_provider_button_ = nullptr;
    HWND remove_provider_button_ = nullptr;
    HWND add_model_button_ = nullptr;
    HWND edit_model_button_ = nullptr;
    HWND remove_model_button_ = nullptr;
    HWND test_connection_button_ = nullptr;
    HWND close_button_ = nullptr;
    HWND status_label_ = nullptr;
};
}  // namespace

HWND CreateProviderManagerWindow(HWND owner, AppStorage* storage, std::vector<ProviderConfig>* providers, std::function<void()> on_changed) {
    auto* window = new ProviderManagerWindow(owner, storage, providers, std::move(on_changed));
    return window->Create(reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr)));
}
