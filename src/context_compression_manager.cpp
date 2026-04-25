#include "context_compression_manager.h"

#include "util.h"

#include <commctrl.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace {
constexpr wchar_t kCompressionManagerClassName[] = L"AgentContextCompressionManagerWindow";
constexpr wchar_t kCompressionScrollPanelClassName[] = L"AgentContextCompressionScrollPanel";
constexpr wchar_t kCompressionScrollContentClassName[] = L"AgentContextCompressionScrollContent";
constexpr int kScrollPanelColorIndex = COLOR_WINDOW;

HBRUSH ScrollPanelBrush() {
    return GetSysColorBrush(kScrollPanelColorIndex);
}

enum ControlId : int {
    kConfigList = 6301,
    kAddConfig = 6302,
    kDeleteConfig = 6303,
    kDuplicateConfig = 6304,
    kConfigNameLabel = 6305,
    kConfigNameEdit = 6306,
    kStrategyLabel = 6307,
    kStrategyCombo = 6308,

    kScrollPanel = 6309,
    kFooterPanel = 6310,

    kTruncatePanel = 6311,
    kTruncateKeepLabel = 6312,
    kTruncateKeepEdit = 6313,

    kL0Panel = 6314,
    kL0Enabled = 6315,
    kL0CaptureModelLabel = 6316,
    kL0CaptureModelCombo = 6317,
    kL0CapturePromptLabel = 6318,
    kL0CapturePromptDefault = 6319,
    kL0CapturePromptEdit = 6320,
    kL0SelectionModelLabel = 6321,
    kL0SelectionModelCombo = 6322,
    kL0SelectionPromptLabel = 6323,
    kL0SelectionPromptDefault = 6324,
    kL0SelectionPromptEdit = 6325,
    kL0StorageLabel = 6326,
    kL0StorageEdit = 6327,
    kL0StorageBrowse = 6328,
    kL0MaxRowsLabel = 6329,
    kL0MaxRowsEdit = 6330,

    kL1Panel = 6331,
    kL1Enabled = 6332,
    kL1MaxPinsLabel = 6333,
    kL1MaxPinsEdit = 6334,
    kL1PinCode = 6335,
    kL1PinUrls = 6336,
    kL1PinNumbers = 6337,
    kL1PinFirst = 6338,
    kL1PinUserFlag = 6339,
    kL1PinInstructions = 6340,

    kL2Panel = 6341,
    kL2Enabled = 6342,
    kL2ModelLabel = 6343,
    kL2ModelCombo = 6344,
    kL2MaxTokensLabel = 6345,
    kL2MaxTokensEdit = 6346,
    kL2TriggerLabel = 6347,
    kL2TriggerEdit = 6348,
    kL2PromptLabel = 6349,
    kL2PromptDefault = 6350,
    kL2PromptEdit = 6351,

    kL3Panel = 6352,
    kL3Enabled = 6353,
    kL3ModelLabel = 6354,
    kL3ModelCombo = 6355,
    kL3MaxTokensLabel = 6356,
    kL3MaxTokensEdit = 6357,
    kL3PromptLabel = 6358,
    kL3PromptDefault = 6359,
    kL3PromptEdit = 6360,

    kL4Panel = 6361,
    kL4Enabled = 6362,
    kL4MinRecentLabel = 6363,
    kL4MinRecentEdit = 6364,

    kFrequencyLabel = 6365,
    kFrequencyEdit = 6366,
    kContextTriggerLabel = 6367,
    kContextTriggerEdit = 6368,

    kSaveButton = IDOK,
    kCancelButton = IDCANCEL,
};

int Scale(HWND hwnd, int value) {
    return MulDiv(value, GetDpiForWindow(hwnd), 96);
}

std::string MakeId() {
    const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::string id = "cc_";
    for (int i = 0; i < 8; ++i) {
        id += std::string(1, chars[rand() % (sizeof(chars) - 1)]);
    }
    return id;
}

std::wstring NormalizeMultilineForEdit(const std::string& value) {
    std::string normalized;
    normalized.reserve(value.size() * 2);
    for (char ch : value) {
        if (ch == '\r') continue;
        if (ch == '\n') {
            normalized += "\r\n";
        } else {
            normalized.push_back(ch);
        }
    }
    return Utf8ToWide(normalized);
}

std::string NormalizeMultilineFromEdit(const std::wstring& value) {
    const std::string utf8 = WideToUtf8(value);
    std::string normalized;
    normalized.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size(); ++i) {
        const char ch = utf8[i];
        if (ch == '\r') {
            if (i + 1 < utf8.size() && utf8[i + 1] == '\n') {
                continue;
            }
            normalized.push_back('\n');
        } else {
            normalized.push_back(ch);
        }
    }
    return normalized;
}

std::wstring BrowseForFolder(HWND owner) {
    BROWSEINFOW info{};
    info.hwndOwner = owner;
    info.lpszTitle = L"Select L0 Artifact Memory Folder";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&info);
    if (!pidl) return {};

    wchar_t path[MAX_PATH] = {0};
    std::wstring result;
    if (SHGetPathFromIDListW(pidl, path)) {
        result = path;
    }
    CoTaskMemFree(pidl);
    return result;
}

class CompressionManagerWindow {
public:
    CompressionManagerWindow(HWND owner, ContextCompressionService* service, AppStorage* storage,
        std::function<std::vector<ProviderConfig>()> get_providers,
        std::function<void()> on_changed)
        : owner_(owner), service_(service), storage_(storage),
          get_providers_(std::move(get_providers)), on_changed_(std::move(on_changed)) {}

    HWND Create(HINSTANCE instance);

private:
    static void RegisterWindowClass(HINSTANCE instance);
    static void RegisterScrollPanelClass(HINSTANCE instance);
    static void RegisterScrollContentClass(HINSTANCE instance);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);
    static LRESULT CALLBACK ScrollPanelProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

    void OnCreate();
    void LayoutControls(int width, int height);
    void LayoutScrollableContent(int width);
    void UpdateScrollInfo();
    void UpdateScrollContentHostBounds();
    void SetScrollOffset(int new_offset);
    void UpdateLayerEnabledStates() const;
    void OnCommand(int control_id, int notification_code);
    void RefreshConfigList();
    void SelectConfig(int index);
    void AddConfig();
    void DeleteConfig();
    void DuplicateConfig();
    void LoadConfigToEditor(const ContextCompressionConfig& config);
    ContextCompressionConfig BuildConfigFromEditor() const;
    void SaveAllConfigs();
    void Relayout();

    void PopulateModelCombo(HWND combo) const;
    void PopulateAllModelCombos() const;
    void SelectComboForModel(HWND combo, const std::string& provider_id, const std::string& model_id) const;
    void AssignModelFromCombo(HWND combo, std::string& provider_id, std::string& model_id) const;

    LRESULT HandleScrollPanelMessage(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    ContextCompressionService* service_ = nullptr;
    AppStorage* storage_ = nullptr;
    std::function<std::vector<ProviderConfig>()> get_providers_;
    std::function<void()> on_changed_;
    std::vector<ContextCompressionConfig> configs_;
    int selected_config_index_ = -1;
    int scroll_offset_ = 0;
    int scroll_content_height_ = 0;
    int scroll_content_width_ = 0;
    int scroll_viewport_height_ = 0;

    HWND config_list_ = nullptr;
    HWND add_button_ = nullptr;
    HWND delete_button_ = nullptr;
    HWND duplicate_button_ = nullptr;
    HWND config_name_label_ = nullptr;
    HWND config_name_edit_ = nullptr;
    HWND strategy_label_ = nullptr;
    HWND strategy_combo_ = nullptr;
    HWND scroll_panel_ = nullptr;
    HWND scroll_backdrop_ = nullptr;
    HWND scroll_content_host_ = nullptr;
    HWND footer_panel_ = nullptr;

    HWND truncate_panel_ = nullptr;
    HWND truncate_keep_label_ = nullptr;
    HWND truncate_keep_edit_ = nullptr;

    HWND l0_panel_ = nullptr;
    HWND l0_enabled_ = nullptr;
    HWND l0_capture_model_label_ = nullptr;
    HWND l0_capture_model_combo_ = nullptr;
    HWND l0_capture_prompt_label_ = nullptr;
    HWND l0_capture_prompt_default_button_ = nullptr;
    HWND l0_capture_prompt_edit_ = nullptr;
    HWND l0_selection_model_label_ = nullptr;
    HWND l0_selection_model_combo_ = nullptr;
    HWND l0_selection_prompt_label_ = nullptr;
    HWND l0_selection_prompt_default_button_ = nullptr;
    HWND l0_selection_prompt_edit_ = nullptr;
    HWND l0_storage_label_ = nullptr;
    HWND l0_storage_edit_ = nullptr;
    HWND l0_storage_browse_button_ = nullptr;
    HWND l0_max_rows_label_ = nullptr;
    HWND l0_max_rows_edit_ = nullptr;

    HWND l1_panel_ = nullptr;
    HWND l1_enabled_ = nullptr;
    HWND l1_max_pins_label_ = nullptr;
    HWND l1_max_pins_edit_ = nullptr;
    HWND l1_pin_code_ = nullptr;
    HWND l1_pin_urls_ = nullptr;
    HWND l1_pin_numbers_ = nullptr;
    HWND l1_pin_first_ = nullptr;
    HWND l1_pin_user_flag_ = nullptr;
    HWND l1_pin_instructions_ = nullptr;

    HWND l2_panel_ = nullptr;
    HWND l2_enabled_ = nullptr;
    HWND l2_model_label_ = nullptr;
    HWND l2_model_combo_ = nullptr;
    HWND l2_max_tokens_label_ = nullptr;
    HWND l2_max_tokens_edit_ = nullptr;
    HWND l2_trigger_label_ = nullptr;
    HWND l2_trigger_edit_ = nullptr;
    HWND l2_prompt_label_ = nullptr;
    HWND l2_prompt_default_button_ = nullptr;
    HWND l2_prompt_edit_ = nullptr;

    HWND l3_panel_ = nullptr;
    HWND l3_enabled_ = nullptr;
    HWND l3_model_label_ = nullptr;
    HWND l3_model_combo_ = nullptr;
    HWND l3_max_tokens_label_ = nullptr;
    HWND l3_max_tokens_edit_ = nullptr;
    HWND l3_prompt_label_ = nullptr;
    HWND l3_prompt_default_button_ = nullptr;
    HWND l3_prompt_edit_ = nullptr;

    HWND l4_panel_ = nullptr;
    HWND l4_enabled_ = nullptr;
    HWND l4_min_recent_label_ = nullptr;
    HWND l4_min_recent_edit_ = nullptr;

    HWND frequency_label_ = nullptr;
    HWND frequency_edit_ = nullptr;
    HWND context_trigger_label_ = nullptr;
    HWND context_trigger_edit_ = nullptr;
    HWND save_button_ = nullptr;
    HWND cancel_button_ = nullptr;
};

HWND CompressionManagerWindow::Create(HINSTANCE instance) {
    RegisterWindowClass(instance);
    RegisterScrollPanelClass(instance);
    RegisterScrollContentClass(instance);
    hwnd_ = CreateWindowExW(
        WS_EX_APPWINDOW,
        kCompressionManagerClassName,
        L"Context Window Editor",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1240,
        1080,
        owner_,
        nullptr,
        instance,
        this);
    return hwnd_;
}

void CompressionManagerWindow::RegisterWindowClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = &CompressionManagerWindow::WindowProc;
    wc.lpszClassName = kCompressionManagerClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
    registered = true;
}

void CompressionManagerWindow::RegisterScrollPanelClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = &CompressionManagerWindow::ScrollPanelProc;
    wc.lpszClassName = kCompressionScrollPanelClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = ScrollPanelBrush();
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
    registered = true;
}

void CompressionManagerWindow::RegisterScrollContentClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = kCompressionScrollContentClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = ScrollPanelBrush();
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
    registered = true;
}

LRESULT CALLBACK CompressionManagerWindow::WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* self = reinterpret_cast<CompressionManagerWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        self = reinterpret_cast<CompressionManagerWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
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
        self->OnCommand(LOWORD(w_param), HIWORD(w_param));
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

LRESULT CALLBACK CompressionManagerWindow::ScrollPanelProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* self = reinterpret_cast<CompressionManagerWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        self = reinterpret_cast<CompressionManagerWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, message, w_param, l_param);
    return self->HandleScrollPanelMessage(hwnd, message, w_param, l_param);
}

LRESULT CompressionManagerWindow::HandleScrollPanelMessage(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_ERASEBKGND: {
        RECT rect{};
        GetClientRect(hwnd, &rect);
        FillRect(reinterpret_cast<HDC>(w_param), &rect, ScrollPanelBrush());
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        FillRect(dc, &paint.rcPaint, ScrollPanelBrush());
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_VSCROLL: {
        SCROLLINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &info);

        int new_offset = scroll_offset_;
        switch (LOWORD(w_param)) {
        case SB_TOP: new_offset = info.nMin; break;
        case SB_BOTTOM: new_offset = info.nMax; break;
        case SB_LINEUP: new_offset -= Scale(hwnd_, 28); break;
        case SB_LINEDOWN: new_offset += Scale(hwnd_, 28); break;
        case SB_PAGEUP: new_offset -= static_cast<int>(info.nPage); break;
        case SB_PAGEDOWN: new_offset += static_cast<int>(info.nPage); break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK: new_offset = HIWORD(w_param); break;
        default: break;
        }
        SetScrollOffset(new_offset);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(w_param);
        SetScrollOffset(scroll_offset_ - MulDiv(delta, Scale(hwnd_, 36), WHEEL_DELTA));
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, message, w_param, l_param);
    }
}

void CompressionManagerWindow::OnCreate() {
    font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    config_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kConfigList), nullptr, nullptr);
    add_button_ = CreateWindowExW(0, L"BUTTON", L"Add", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kAddConfig), nullptr, nullptr);
    delete_button_ = CreateWindowExW(0, L"BUTTON", L"Delete", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kDeleteConfig), nullptr, nullptr);
    duplicate_button_ = CreateWindowExW(0, L"BUTTON", L"Duplicate", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kDuplicateConfig), nullptr, nullptr);

    config_name_label_ = CreateWindowExW(0, L"STATIC", L"Name:", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kConfigNameLabel), nullptr, nullptr);
    config_name_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kConfigNameEdit), nullptr, nullptr);

    strategy_label_ = CreateWindowExW(0, L"STATIC", L"Strategy:", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kStrategyLabel), nullptr, nullptr);
    strategy_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kStrategyCombo), nullptr, nullptr);

    scroll_panel_ = CreateWindowExW(
        WS_EX_CONTROLPARENT | WS_EX_COMPOSITED,
        kCompressionScrollPanelClassName,
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_CLIPCHILDREN,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kScrollPanel), nullptr, this);
    scroll_backdrop_ = CreateWindowExW(
        0,
        L"STATIC",
        nullptr,
        WS_CHILD | WS_VISIBLE | SS_WHITERECT,
        0, 0, 0, 0, scroll_panel_, nullptr, nullptr, nullptr);
    scroll_content_host_ = CreateWindowExW(
        WS_EX_CONTROLPARENT,
        kCompressionScrollContentClassName,
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 0, 0, scroll_panel_, nullptr, nullptr, nullptr);

    footer_panel_ = CreateWindowExW(0, L"BUTTON", L"Compression Triggers", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kFooterPanel), nullptr, nullptr);

    truncate_panel_ = CreateWindowExW(0, L"BUTTON", L"Truncate Top", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kTruncatePanel), nullptr, nullptr);
    truncate_keep_label_ = CreateWindowExW(0, L"STATIC", L"Keep recent messages:", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kTruncateKeepLabel), nullptr, nullptr);
    truncate_keep_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"20",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kTruncateKeepEdit), nullptr, nullptr);

    l0_panel_ = CreateWindowExW(0, L"BUTTON", L"Layer 0 - Artifact Memory", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL0Panel), nullptr, nullptr);
    l0_enabled_ = CreateWindowExW(0, L"BUTTON", L"L0 Memory Tool", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL0Enabled), nullptr, nullptr);
    l0_capture_model_label_ = CreateWindowExW(0, L"STATIC", L"Capture model:", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL0CaptureModelLabel), nullptr, nullptr);
    l0_capture_model_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL0CaptureModelCombo), nullptr, nullptr);
    l0_capture_prompt_label_ = CreateWindowExW(0, L"STATIC", L"Capture instructions:", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL0CapturePromptLabel), nullptr, nullptr);
    l0_capture_prompt_default_button_ = CreateWindowExW(0, L"BUTTON", L"Default", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL0CapturePromptDefault), nullptr, nullptr);
    l0_capture_prompt_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL0CapturePromptEdit), nullptr, nullptr);
    l0_selection_model_label_ = CreateWindowExW(0, L"STATIC", L"Selection model:", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL0SelectionModelLabel), nullptr, nullptr);
    l0_selection_model_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL0SelectionModelCombo), nullptr, nullptr);
    l0_selection_prompt_label_ = CreateWindowExW(0, L"STATIC", L"Selection instructions:", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL0SelectionPromptLabel), nullptr, nullptr);
    l0_selection_prompt_default_button_ = CreateWindowExW(0, L"BUTTON", L"Default", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL0SelectionPromptDefault), nullptr, nullptr);
    l0_selection_prompt_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL0SelectionPromptEdit), nullptr, nullptr);
    l0_storage_label_ = CreateWindowExW(0, L"STATIC", L"Storage folder:", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL0StorageLabel), nullptr, nullptr);
    l0_storage_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL0StorageEdit), nullptr, nullptr);
    l0_storage_browse_button_ = CreateWindowExW(0, L"BUTTON", L"Browse", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL0StorageBrowse), nullptr, nullptr);
    l0_max_rows_label_ = CreateWindowExW(0, L"STATIC", L"Max injected rows:", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL0MaxRowsLabel), nullptr, nullptr);
    l0_max_rows_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"12",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL0MaxRowsEdit), nullptr, nullptr);

    l1_panel_ = CreateWindowExW(0, L"BUTTON", L"Layer 1 - Verbatim Pinning", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL1Panel), nullptr, nullptr);
    l1_enabled_ = CreateWindowExW(0, L"BUTTON", L"Enable L1 pinning", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL1Enabled), nullptr, nullptr);
    l1_max_pins_label_ = CreateWindowExW(0, L"STATIC", L"Max pins:", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL1MaxPinsLabel), nullptr, nullptr);
    l1_max_pins_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"10",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL1MaxPinsEdit), nullptr, nullptr);
    l1_pin_code_ = CreateWindowExW(0, L"BUTTON", L"Pin code blocks", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL1PinCode), nullptr, nullptr);
    l1_pin_urls_ = CreateWindowExW(0, L"BUTTON", L"Pin URLs and paths", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL1PinUrls), nullptr, nullptr);
    l1_pin_numbers_ = CreateWindowExW(0, L"BUTTON", L"Pin numbers and versions", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL1PinNumbers), nullptr, nullptr);
    l1_pin_first_ = CreateWindowExW(0, L"BUTTON", L"Pin first user message", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL1PinFirst), nullptr, nullptr);
    l1_pin_user_flag_ = CreateWindowExW(0, L"BUTTON", L"Pin [PIN] markers", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL1PinUserFlag), nullptr, nullptr);
    l1_pin_instructions_ = CreateWindowExW(0, L"BUTTON", L"Pin explicit instructions", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL1PinInstructions), nullptr, nullptr);

    l2_panel_ = CreateWindowExW(0, L"BUTTON", L"Layer 2 - Summary", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL2Panel), nullptr, nullptr);
    l2_enabled_ = CreateWindowExW(0, L"BUTTON", L"Enable L2 summary", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL2Enabled), nullptr, nullptr);
    l2_model_label_ = CreateWindowExW(0, L"STATIC", L"Model:", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL2ModelLabel), nullptr, nullptr);
    l2_model_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL2ModelCombo), nullptr, nullptr);
    l2_max_tokens_label_ = CreateWindowExW(0, L"STATIC", L"Max tokens:", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL2MaxTokensLabel), nullptr, nullptr);
    l2_max_tokens_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"500",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL2MaxTokensEdit), nullptr, nullptr);
    l2_trigger_label_ = CreateWindowExW(0, L"STATIC", L"Trigger (turns):", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL2TriggerLabel), nullptr, nullptr);
    l2_trigger_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"8",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL2TriggerEdit), nullptr, nullptr);
    l2_prompt_label_ = CreateWindowExW(0, L"STATIC", L"Summary instructions:", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL2PromptLabel), nullptr, nullptr);
    l2_prompt_default_button_ = CreateWindowExW(0, L"BUTTON", L"Default", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL2PromptDefault), nullptr, nullptr);
    l2_prompt_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL2PromptEdit), nullptr, nullptr);

    l3_panel_ = CreateWindowExW(0, L"BUTTON", L"Layer 3 - Structured State", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL3Panel), nullptr, nullptr);
    l3_enabled_ = CreateWindowExW(0, L"BUTTON", L"Enable L3 state", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL3Enabled), nullptr, nullptr);
    l3_model_label_ = CreateWindowExW(0, L"STATIC", L"Model:", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL3ModelLabel), nullptr, nullptr);
    l3_model_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL3ModelCombo), nullptr, nullptr);
    l3_max_tokens_label_ = CreateWindowExW(0, L"STATIC", L"Max tokens:", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL3MaxTokensLabel), nullptr, nullptr);
    l3_max_tokens_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"800",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL3MaxTokensEdit), nullptr, nullptr);
    l3_prompt_label_ = CreateWindowExW(0, L"STATIC", L"State instructions:", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL3PromptLabel), nullptr, nullptr);
    l3_prompt_default_button_ = CreateWindowExW(0, L"BUTTON", L"Default", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL3PromptDefault), nullptr, nullptr);
    l3_prompt_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL3PromptEdit), nullptr, nullptr);

    l4_panel_ = CreateWindowExW(0, L"BUTTON", L"Layer 4 - Recency Window", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL4Panel), nullptr, nullptr);
    l4_enabled_ = CreateWindowExW(0, L"BUTTON", L"Enable L4 recency", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL4Enabled), nullptr, nullptr);
    l4_min_recent_label_ = CreateWindowExW(0, L"STATIC", L"Min recent turns:", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL4MinRecentLabel), nullptr, nullptr);
    l4_min_recent_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"2",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kL4MinRecentEdit), nullptr, nullptr);

    frequency_label_ = CreateWindowExW(0, L"STATIC", L"Frequency (every N prompts, 0 = manual):", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kFrequencyLabel), nullptr, nullptr);
    frequency_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kFrequencyEdit), nullptr, nullptr);
    context_trigger_label_ = CreateWindowExW(0, L"STATIC", L"Context trigger (% of context window):", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kContextTriggerLabel), nullptr, nullptr);
    context_trigger_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"70",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kContextTriggerEdit), nullptr, nullptr);
    save_button_ = CreateWindowExW(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kSaveButton), nullptr, nullptr);
    cancel_button_ = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kCancelButton), nullptr, nullptr);

    std::array<HWND, 49> controls = {
        config_list_, add_button_, delete_button_, duplicate_button_,
        config_name_label_, config_name_edit_, strategy_label_, strategy_combo_,
        scroll_panel_, scroll_content_host_, footer_panel_,
        truncate_panel_, truncate_keep_label_, truncate_keep_edit_,
        l0_panel_, l0_enabled_, l0_capture_model_label_, l0_capture_model_combo_, l0_capture_prompt_label_,
        l0_capture_prompt_default_button_, l0_capture_prompt_edit_, l0_selection_model_label_,
        l0_selection_model_combo_, l0_selection_prompt_label_, l0_selection_prompt_default_button_,
        l0_selection_prompt_edit_, l0_storage_label_, l0_storage_edit_, l0_storage_browse_button_,
        l0_max_rows_label_, l0_max_rows_edit_,
        l1_panel_, l1_enabled_, l1_max_pins_label_, l1_max_pins_edit_, l1_pin_code_, l1_pin_urls_,
        l1_pin_numbers_, l1_pin_first_, l1_pin_user_flag_, l1_pin_instructions_,
        l2_panel_, l2_enabled_, l2_model_label_, l2_model_combo_, l2_max_tokens_label_, l2_max_tokens_edit_,
        l2_trigger_label_, l2_trigger_edit_
    };
    for (HWND control : controls) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
    std::array<HWND, 15> controls_2 = {
        l2_prompt_label_, l2_prompt_default_button_, l2_prompt_edit_,
        l3_panel_, l3_enabled_, l3_model_label_, l3_model_combo_, l3_max_tokens_label_,
        l3_max_tokens_edit_, l3_prompt_label_, l3_prompt_default_button_, l3_prompt_edit_,
        l4_panel_, l4_enabled_, l4_min_recent_label_
    };
    for (HWND control : controls_2) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
    std::array<HWND, 7> controls_3 = {
        l4_min_recent_edit_, frequency_label_, frequency_edit_, context_trigger_label_,
        context_trigger_edit_, save_button_, cancel_button_
    };
    for (HWND control : controls_3) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }

    ComboBox_AddString(strategy_combo_, L"Truncate Top (Rolling Window)");
    ComboBox_AddString(strategy_combo_, L"Hierarchical Structured Compression");
    PopulateAllModelCombos();

    configs_ = service_->LoadGlobalConfigs();
    RefreshConfigList();
    if (!configs_.empty()) {
        SelectConfig(0);
    } else {
        AddConfig();
    }
}

void CompressionManagerWindow::PopulateModelCombo(HWND combo) const {
    ComboBox_ResetContent(combo);
    for (const auto& provider : get_providers_()) {
        for (const auto& model : provider.models) {
            const std::string display_name = model.display_name.empty() ? model.id : model.display_name;
            std::wstring label = Utf8ToWide(provider.name + " / " + display_name);
            ComboBox_AddString(combo, label.c_str());
        }
    }
}

void CompressionManagerWindow::PopulateAllModelCombos() const {
    PopulateModelCombo(l0_capture_model_combo_);
    PopulateModelCombo(l0_selection_model_combo_);
    PopulateModelCombo(l2_model_combo_);
    PopulateModelCombo(l3_model_combo_);
}

void CompressionManagerWindow::SelectComboForModel(HWND combo, const std::string& provider_id, const std::string& model_id) const {
    int global_index = 0;
    int selected_index = -1;
    for (const auto& provider : get_providers_()) {
        for (const auto& model : provider.models) {
            if (provider.id == provider_id && model.id == model_id) {
                selected_index = global_index;
                break;
            }
            ++global_index;
        }
        if (selected_index >= 0) break;
    }
    ComboBox_SetCurSel(combo, selected_index);
}

void CompressionManagerWindow::AssignModelFromCombo(HWND combo, std::string& provider_id, std::string& model_id) const {
    const int selected = ComboBox_GetCurSel(combo);
    if (selected < 0) return;

    int global_index = 0;
    for (const auto& provider : get_providers_()) {
        for (const auto& model : provider.models) {
            if (global_index == selected) {
                provider_id = provider.id;
                model_id = model.id;
                return;
            }
            ++global_index;
        }
    }
}

void CompressionManagerWindow::LayoutControls(int width, int height) {
    if (!hwnd_) return;

    const int margin = Scale(hwnd_, 16);
    const int gutter = Scale(hwnd_, 10);
    const int button_height = Scale(hwnd_, 28);
    const int button_width = Scale(hwnd_, 98);
    const int label_height = Scale(hwnd_, 18);
    const int edit_height = Scale(hwnd_, 24);
    const int sidebar_width = Scale(hwnd_, 260);
    const int top_area_height = Scale(hwnd_, 86);
    const int footer_height = Scale(hwnd_, 106);

    MoveWindow(config_list_, margin, margin, sidebar_width, height - margin * 2 - button_height - gutter, TRUE);
    const int list_bottom = height - margin - button_height;
    MoveWindow(add_button_, margin, list_bottom, Scale(hwnd_, 72), button_height, TRUE);
    MoveWindow(delete_button_, margin + Scale(hwnd_, 78), list_bottom, Scale(hwnd_, 72), button_height, TRUE);
    MoveWindow(duplicate_button_, margin + Scale(hwnd_, 156), list_bottom, Scale(hwnd_, 104), button_height, TRUE);

    const int right_x = margin + sidebar_width + gutter * 2;
    const int right_width = std::max(Scale(hwnd_, 620), width - right_x - margin);

    MoveWindow(config_name_label_, right_x, margin, Scale(hwnd_, 52), label_height, TRUE);
    MoveWindow(config_name_edit_, right_x + Scale(hwnd_, 58), margin - Scale(hwnd_, 2), right_width - Scale(hwnd_, 58), edit_height, TRUE);
    MoveWindow(strategy_label_, right_x, margin + Scale(hwnd_, 36), Scale(hwnd_, 60), label_height, TRUE);
    MoveWindow(strategy_combo_, right_x + Scale(hwnd_, 68), margin + Scale(hwnd_, 32), right_width - Scale(hwnd_, 68), Scale(hwnd_, 260), TRUE);

    const int footer_y = height - margin - footer_height;
    MoveWindow(footer_panel_, right_x, footer_y, right_width, footer_height, TRUE);
    MoveWindow(frequency_label_, right_x + gutter, footer_y + Scale(hwnd_, 24), Scale(hwnd_, 250), label_height, TRUE);
    MoveWindow(frequency_edit_, right_x + Scale(hwnd_, 258), footer_y + Scale(hwnd_, 20), Scale(hwnd_, 64), edit_height, TRUE);
    MoveWindow(context_trigger_label_, right_x + Scale(hwnd_, 340), footer_y + Scale(hwnd_, 24), Scale(hwnd_, 235), label_height, TRUE);
    MoveWindow(context_trigger_edit_, right_x + Scale(hwnd_, 582), footer_y + Scale(hwnd_, 20), Scale(hwnd_, 64), edit_height, TRUE);
    MoveWindow(cancel_button_, right_x + right_width - button_width, footer_y + footer_height - button_height - gutter, button_width, button_height, TRUE);
    MoveWindow(save_button_, right_x + right_width - button_width * 2 - gutter, footer_y + footer_height - button_height - gutter, button_width, button_height, TRUE);

    const int scroll_y = margin + top_area_height;
    const int scroll_height = std::max(Scale(hwnd_, 320), footer_y - scroll_y - gutter);
    scroll_viewport_height_ = scroll_height;
    MoveWindow(scroll_panel_, right_x, scroll_y, right_width, scroll_height, TRUE);
    MoveWindow(scroll_backdrop_, 0, 0, right_width, scroll_height, TRUE);
    SetWindowPos(scroll_backdrop_, HWND_BOTTOM, 0, 0, right_width, scroll_height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    LayoutScrollableContent(right_width);
    UpdateScrollInfo();
}

void CompressionManagerWindow::LayoutScrollableContent(int width) {
    const int gutter = Scale(hwnd_, 12);
    const int group_top_padding = Scale(hwnd_, 22);
    const int label_height = Scale(hwnd_, 18);
    const int edit_height = Scale(hwnd_, 24);
    const int combo_height = Scale(hwnd_, 260);
    const int prompt_button_width = Scale(hwnd_, 74);
    const int prompt_height = Scale(hwnd_, 126);
    const int storage_button_width = Scale(hwnd_, 74);
    const int panel_width = std::max(Scale(hwnd_, 540), width - Scale(hwnd_, 18));
    const int x = Scale(hwnd_, 6);
    const int left = x + gutter;
    const int inner_width = panel_width - gutter * 2;
    const int prompt_label_width = inner_width - prompt_button_width - gutter;

    int y = Scale(hwnd_, 6);
    const bool is_truncate = (ComboBox_GetCurSel(strategy_combo_) == 0);

    auto show = [](HWND control, bool visible) {
        ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
    };

    show(truncate_panel_, is_truncate);
    show(truncate_keep_label_, is_truncate);
    show(truncate_keep_edit_, is_truncate);

    const std::array<HWND, 33> hsc_controls = {
        l0_panel_, l0_enabled_, l0_capture_model_label_, l0_capture_model_combo_, l0_capture_prompt_label_,
        l0_capture_prompt_default_button_, l0_capture_prompt_edit_, l0_selection_model_label_,
        l0_selection_model_combo_, l0_selection_prompt_label_, l0_selection_prompt_default_button_,
        l0_selection_prompt_edit_, l0_storage_label_, l0_storage_edit_, l0_storage_browse_button_,
        l0_max_rows_label_, l0_max_rows_edit_,
        l1_panel_, l1_enabled_, l1_max_pins_label_, l1_max_pins_edit_, l1_pin_code_, l1_pin_urls_,
        l1_pin_numbers_, l1_pin_first_, l1_pin_user_flag_, l1_pin_instructions_,
        l2_panel_, l2_enabled_, l2_model_label_, l2_model_combo_, l2_max_tokens_label_, l2_max_tokens_edit_
    };
    for (HWND control : hsc_controls) show(control, !is_truncate);
    const std::array<HWND, 15> hsc_controls_2 = {
        l2_trigger_label_, l2_trigger_edit_, l2_prompt_label_, l2_prompt_default_button_, l2_prompt_edit_,
        l3_panel_, l3_enabled_, l3_model_label_, l3_model_combo_, l3_max_tokens_label_, l3_max_tokens_edit_,
        l3_prompt_label_, l3_prompt_default_button_, l3_prompt_edit_, l4_panel_
    };
    for (HWND control : hsc_controls_2) show(control, !is_truncate);
    const std::array<HWND, 3> hsc_controls_3 = {l4_enabled_, l4_min_recent_label_, l4_min_recent_edit_};
    for (HWND control : hsc_controls_3) show(control, !is_truncate);

    if (is_truncate) {
        const int panel_height = Scale(hwnd_, 74);
        MoveWindow(truncate_panel_, x, y, panel_width, panel_height, TRUE);
        MoveWindow(truncate_keep_label_, left, y + group_top_padding + Scale(hwnd_, 8), Scale(hwnd_, 140), label_height, TRUE);
        MoveWindow(truncate_keep_edit_, left + Scale(hwnd_, 150), y + group_top_padding + Scale(hwnd_, 4), Scale(hwnd_, 72), edit_height, TRUE);
        y += panel_height + gutter;
    } else {
        const int l0_panel_height = Scale(hwnd_, 470);
        MoveWindow(l0_panel_, x, y, panel_width, l0_panel_height, TRUE);
        MoveWindow(l0_enabled_, left, y + group_top_padding, Scale(hwnd_, 180), Scale(hwnd_, 22), TRUE);
        MoveWindow(l0_capture_model_label_, left, y + group_top_padding + Scale(hwnd_, 30), Scale(hwnd_, 92), label_height, TRUE);
        MoveWindow(l0_capture_model_combo_, left + Scale(hwnd_, 100), y + group_top_padding + Scale(hwnd_, 26), inner_width - Scale(hwnd_, 100), combo_height, TRUE);
        MoveWindow(l0_capture_prompt_label_, left, y + group_top_padding + Scale(hwnd_, 58), prompt_label_width, label_height, TRUE);
        MoveWindow(l0_capture_prompt_default_button_, left + inner_width - prompt_button_width, y + group_top_padding + Scale(hwnd_, 54), prompt_button_width, Scale(hwnd_, 24), TRUE);
        MoveWindow(l0_capture_prompt_edit_, left, y + group_top_padding + Scale(hwnd_, 80), inner_width, prompt_height, TRUE);
        MoveWindow(l0_selection_model_label_, left, y + group_top_padding + Scale(hwnd_, 214), Scale(hwnd_, 98), label_height, TRUE);
        MoveWindow(l0_selection_model_combo_, left + Scale(hwnd_, 100), y + group_top_padding + Scale(hwnd_, 210), inner_width - Scale(hwnd_, 100), combo_height, TRUE);
        MoveWindow(l0_selection_prompt_label_, left, y + group_top_padding + Scale(hwnd_, 242), prompt_label_width, label_height, TRUE);
        MoveWindow(l0_selection_prompt_default_button_, left + inner_width - prompt_button_width, y + group_top_padding + Scale(hwnd_, 238), prompt_button_width, Scale(hwnd_, 24), TRUE);
        MoveWindow(l0_selection_prompt_edit_, left, y + group_top_padding + Scale(hwnd_, 264), inner_width, prompt_height, TRUE);
        MoveWindow(l0_storage_label_, left, y + group_top_padding + Scale(hwnd_, 398), Scale(hwnd_, 94), label_height, TRUE);
        MoveWindow(l0_storage_edit_, left + Scale(hwnd_, 98), y + group_top_padding + Scale(hwnd_, 394), inner_width - Scale(hwnd_, 98) - storage_button_width - gutter, edit_height, TRUE);
        MoveWindow(l0_storage_browse_button_, left + inner_width - storage_button_width, y + group_top_padding + Scale(hwnd_, 392), storage_button_width, Scale(hwnd_, 26), TRUE);
        MoveWindow(l0_max_rows_label_, left, y + group_top_padding + Scale(hwnd_, 428), Scale(hwnd_, 108), label_height, TRUE);
        MoveWindow(l0_max_rows_edit_, left + Scale(hwnd_, 116), y + group_top_padding + Scale(hwnd_, 424), Scale(hwnd_, 64), edit_height, TRUE);
        y += l0_panel_height + gutter;

        const int l1_panel_height = Scale(hwnd_, 128);
        MoveWindow(l1_panel_, x, y, panel_width, l1_panel_height, TRUE);
        MoveWindow(l1_enabled_, left, y + group_top_padding, Scale(hwnd_, 160), Scale(hwnd_, 22), TRUE);
        MoveWindow(l1_max_pins_label_, left, y + group_top_padding + Scale(hwnd_, 28), Scale(hwnd_, 60), label_height, TRUE);
        MoveWindow(l1_max_pins_edit_, left + Scale(hwnd_, 70), y + group_top_padding + Scale(hwnd_, 24), Scale(hwnd_, 60), edit_height, TRUE);
        MoveWindow(l1_pin_code_, left, y + group_top_padding + Scale(hwnd_, 56), Scale(hwnd_, 140), Scale(hwnd_, 22), TRUE);
        MoveWindow(l1_pin_urls_, left + Scale(hwnd_, 152), y + group_top_padding + Scale(hwnd_, 56), Scale(hwnd_, 150), Scale(hwnd_, 22), TRUE);
        MoveWindow(l1_pin_numbers_, left, y + group_top_padding + Scale(hwnd_, 80), Scale(hwnd_, 160), Scale(hwnd_, 22), TRUE);
        MoveWindow(l1_pin_first_, left + Scale(hwnd_, 170), y + group_top_padding + Scale(hwnd_, 80), Scale(hwnd_, 160), Scale(hwnd_, 22), TRUE);
        MoveWindow(l1_pin_user_flag_, left + Scale(hwnd_, 340), y + group_top_padding + Scale(hwnd_, 56), Scale(hwnd_, 150), Scale(hwnd_, 22), TRUE);
        MoveWindow(l1_pin_instructions_, left + Scale(hwnd_, 340), y + group_top_padding + Scale(hwnd_, 80), Scale(hwnd_, 180), Scale(hwnd_, 22), TRUE);
        y += l1_panel_height + gutter;

        const int l2_panel_height = Scale(hwnd_, 252);
        MoveWindow(l2_panel_, x, y, panel_width, l2_panel_height, TRUE);
        MoveWindow(l2_enabled_, left, y + group_top_padding, Scale(hwnd_, 150), Scale(hwnd_, 22), TRUE);
        MoveWindow(l2_model_label_, left, y + group_top_padding + Scale(hwnd_, 28), Scale(hwnd_, 50), label_height, TRUE);
        MoveWindow(l2_model_combo_, left + Scale(hwnd_, 58), y + group_top_padding + Scale(hwnd_, 24), inner_width - Scale(hwnd_, 58), combo_height, TRUE);
        MoveWindow(l2_max_tokens_label_, left, y + group_top_padding + Scale(hwnd_, 56), Scale(hwnd_, 76), label_height, TRUE);
        MoveWindow(l2_max_tokens_edit_, left + Scale(hwnd_, 82), y + group_top_padding + Scale(hwnd_, 52), Scale(hwnd_, 64), edit_height, TRUE);
        MoveWindow(l2_trigger_label_, left + Scale(hwnd_, 160), y + group_top_padding + Scale(hwnd_, 56), Scale(hwnd_, 86), label_height, TRUE);
        MoveWindow(l2_trigger_edit_, left + Scale(hwnd_, 252), y + group_top_padding + Scale(hwnd_, 52), Scale(hwnd_, 64), edit_height, TRUE);
        MoveWindow(l2_prompt_label_, left, y + group_top_padding + Scale(hwnd_, 86), prompt_label_width, label_height, TRUE);
        MoveWindow(l2_prompt_default_button_, left + inner_width - prompt_button_width, y + group_top_padding + Scale(hwnd_, 82), prompt_button_width, Scale(hwnd_, 24), TRUE);
        MoveWindow(l2_prompt_edit_, left, y + group_top_padding + Scale(hwnd_, 108), inner_width, Scale(hwnd_, 118), TRUE);
        y += l2_panel_height + gutter;

        const int l3_panel_height = Scale(hwnd_, 228);
        MoveWindow(l3_panel_, x, y, panel_width, l3_panel_height, TRUE);
        MoveWindow(l3_enabled_, left, y + group_top_padding, Scale(hwnd_, 150), Scale(hwnd_, 22), TRUE);
        MoveWindow(l3_model_label_, left, y + group_top_padding + Scale(hwnd_, 28), Scale(hwnd_, 50), label_height, TRUE);
        MoveWindow(l3_model_combo_, left + Scale(hwnd_, 58), y + group_top_padding + Scale(hwnd_, 24), inner_width - Scale(hwnd_, 58), combo_height, TRUE);
        MoveWindow(l3_max_tokens_label_, left, y + group_top_padding + Scale(hwnd_, 56), Scale(hwnd_, 76), label_height, TRUE);
        MoveWindow(l3_max_tokens_edit_, left + Scale(hwnd_, 82), y + group_top_padding + Scale(hwnd_, 52), Scale(hwnd_, 64), edit_height, TRUE);
        MoveWindow(l3_prompt_label_, left, y + group_top_padding + Scale(hwnd_, 86), prompt_label_width, label_height, TRUE);
        MoveWindow(l3_prompt_default_button_, left + inner_width - prompt_button_width, y + group_top_padding + Scale(hwnd_, 82), prompt_button_width, Scale(hwnd_, 24), TRUE);
        MoveWindow(l3_prompt_edit_, left, y + group_top_padding + Scale(hwnd_, 108), inner_width, Scale(hwnd_, 94), TRUE);
        y += l3_panel_height + gutter;

        const int l4_panel_height = Scale(hwnd_, 88);
        MoveWindow(l4_panel_, x, y, panel_width, l4_panel_height, TRUE);
        MoveWindow(l4_enabled_, left, y + group_top_padding, Scale(hwnd_, 150), Scale(hwnd_, 22), TRUE);
        MoveWindow(l4_min_recent_label_, left, y + group_top_padding + Scale(hwnd_, 30), Scale(hwnd_, 106), label_height, TRUE);
        MoveWindow(l4_min_recent_edit_, left + Scale(hwnd_, 114), y + group_top_padding + Scale(hwnd_, 26), Scale(hwnd_, 64), edit_height, TRUE);
        y += l4_panel_height + gutter;
    }

    scroll_content_width_ = panel_width;
    scroll_content_height_ = std::max(0, y + Scale(hwnd_, 8));
    UpdateScrollContentHostBounds();
    UpdateLayerEnabledStates();
}

void CompressionManagerWindow::UpdateScrollInfo() {
    if (!scroll_panel_) return;
    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = std::max(0, scroll_content_height_ - 1);
    info.nPage = std::max(1, scroll_viewport_height_);
    info.nPos = scroll_offset_;
    SetScrollInfo(scroll_panel_, SB_VERT, &info, TRUE);
}

void CompressionManagerWindow::UpdateScrollContentHostBounds() {
    if (!scroll_content_host_) return;
    SetWindowPos(
        scroll_content_host_,
        nullptr,
        0,
        -scroll_offset_,
        scroll_content_width_,
        std::max(scroll_content_height_, scroll_viewport_height_),
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
}

void CompressionManagerWindow::SetScrollOffset(int new_offset) {
    const int max_offset = std::max(0, scroll_content_height_ - scroll_viewport_height_);
    new_offset = std::clamp(new_offset, 0, max_offset);
    if (new_offset == scroll_offset_) return;
    scroll_offset_ = new_offset;
    UpdateScrollInfo();
    UpdateScrollContentHostBounds();
    RedrawWindow(
        scroll_panel_,
        nullptr,
        nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    RedrawWindow(
        scroll_content_host_,
        nullptr,
        nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void CompressionManagerWindow::UpdateLayerEnabledStates() const {
    const bool is_truncate = (ComboBox_GetCurSel(strategy_combo_) == 0);
    const bool l0_enabled = !is_truncate && Button_GetCheck(l0_enabled_) == BST_CHECKED;
    const bool l1_enabled = !is_truncate && Button_GetCheck(l1_enabled_) == BST_CHECKED;
    const bool l2_enabled = !is_truncate && Button_GetCheck(l2_enabled_) == BST_CHECKED;
    const bool l3_enabled = !is_truncate && Button_GetCheck(l3_enabled_) == BST_CHECKED;
    const bool l4_enabled = !is_truncate && Button_GetCheck(l4_enabled_) == BST_CHECKED;

    const std::array<HWND, 14> l0_children = {
        l0_capture_model_label_, l0_capture_model_combo_, l0_capture_prompt_label_,
        l0_capture_prompt_default_button_, l0_capture_prompt_edit_, l0_selection_model_label_,
        l0_selection_model_combo_, l0_selection_prompt_label_, l0_selection_prompt_default_button_,
        l0_selection_prompt_edit_, l0_storage_label_, l0_storage_edit_,
        l0_storage_browse_button_, l0_max_rows_label_
    };
    for (HWND control : l0_children) EnableWindow(control, l0_enabled);
    EnableWindow(l0_max_rows_edit_, l0_enabled);

    const std::array<HWND, 7> l1_children = {
        l1_max_pins_label_, l1_max_pins_edit_, l1_pin_code_, l1_pin_urls_,
        l1_pin_numbers_, l1_pin_first_, l1_pin_user_flag_
    };
    for (HWND control : l1_children) EnableWindow(control, l1_enabled);
    EnableWindow(l1_pin_instructions_, l1_enabled);

    const std::array<HWND, 8> l2_children = {
        l2_model_label_, l2_model_combo_, l2_max_tokens_label_, l2_max_tokens_edit_,
        l2_trigger_label_, l2_trigger_edit_, l2_prompt_label_, l2_prompt_default_button_
    };
    for (HWND control : l2_children) EnableWindow(control, l2_enabled);
    EnableWindow(l2_prompt_edit_, l2_enabled);

    const std::array<HWND, 6> l3_children = {
        l3_model_label_, l3_model_combo_, l3_max_tokens_label_, l3_max_tokens_edit_,
        l3_prompt_label_, l3_prompt_default_button_
    };
    for (HWND control : l3_children) EnableWindow(control, l3_enabled);
    EnableWindow(l3_prompt_edit_, l3_enabled);

    EnableWindow(l4_min_recent_label_, l4_enabled);
    EnableWindow(l4_min_recent_edit_, l4_enabled);
}

void CompressionManagerWindow::Relayout() {
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    LayoutControls(rect.right - rect.left, rect.bottom - rect.top);
}

void CompressionManagerWindow::RefreshConfigList() {
    ListBox_ResetContent(config_list_);
    for (const auto& config : configs_) {
        ListBox_AddString(config_list_, Utf8ToWide(config.name).c_str());
    }
    if (selected_config_index_ >= 0 && static_cast<size_t>(selected_config_index_) < configs_.size()) {
        ListBox_SetCurSel(config_list_, selected_config_index_);
    }
}

void CompressionManagerWindow::SelectConfig(int index) {
    if (index < 0 || static_cast<size_t>(index) >= configs_.size()) return;
    selected_config_index_ = index;
    scroll_offset_ = 0;
    LoadConfigToEditor(configs_[index]);
}

void CompressionManagerWindow::LoadConfigToEditor(const ContextCompressionConfig& config) {
    SetWindowTextW(config_name_edit_, Utf8ToWide(config.name).c_str());
    ComboBox_SetCurSel(strategy_combo_,
        config.strategy == ContextCompressionStrategy::TruncateTop ? 0 : 1);

    SetWindowTextW(truncate_keep_edit_, std::to_wstring(config.truncate_top_keep_messages).c_str());

    Button_SetCheck(l0_enabled_, config.layers.layer0.enabled ? BST_CHECKED : BST_UNCHECKED);
    SelectComboForModel(l0_capture_model_combo_, config.layers.layer0.capture_model_provider_id, config.layers.layer0.capture_model_id);
    SelectComboForModel(l0_selection_model_combo_, config.layers.layer0.selection_model_provider_id, config.layers.layer0.selection_model_id);
    const std::string l0_capture_prompt = Trim(config.layers.layer0.capture_prompt_template).empty()
        ? ContextCompressionService::DefaultLayer0CapturePromptTemplate()
        : config.layers.layer0.capture_prompt_template;
    const std::string l0_selection_prompt = Trim(config.layers.layer0.selection_prompt_template).empty()
        ? ContextCompressionService::DefaultLayer0SelectionPromptTemplate()
        : config.layers.layer0.selection_prompt_template;
    SetWindowTextW(l0_capture_prompt_edit_, NormalizeMultilineForEdit(l0_capture_prompt).c_str());
    SetWindowTextW(l0_selection_prompt_edit_, NormalizeMultilineForEdit(l0_selection_prompt).c_str());
    SetWindowTextW(l0_storage_edit_, Utf8ToWide(config.layers.layer0.storage_folder_template).c_str());
    SetWindowTextW(l0_max_rows_edit_, std::to_wstring(config.layers.layer0.max_injected_rows).c_str());

    Button_SetCheck(l1_enabled_, config.layers.layer1.enabled ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextW(l1_max_pins_edit_, std::to_wstring(config.layers.layer1.max_pins).c_str());
    Button_SetCheck(l1_pin_code_, config.layers.layer1.pin_code_blocks ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(l1_pin_urls_, config.layers.layer1.pin_urls ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(l1_pin_numbers_, config.layers.layer1.pin_numbers ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(l1_pin_first_, config.layers.layer1.pin_first_message ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(l1_pin_user_flag_, config.layers.layer1.pin_user_flagged ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(l1_pin_instructions_, config.layers.layer1.pin_explicit_instructions ? BST_CHECKED : BST_UNCHECKED);

    Button_SetCheck(l2_enabled_, config.layers.layer2.enabled ? BST_CHECKED : BST_UNCHECKED);
    SelectComboForModel(l2_model_combo_, config.layers.layer2.model_provider_id, config.layers.layer2.model_id);
    SetWindowTextW(l2_max_tokens_edit_, std::to_wstring(config.layers.layer2.max_tokens).c_str());
    SetWindowTextW(l2_trigger_edit_, std::to_wstring(config.layers.layer2.trigger_threshold_turns).c_str());
    const std::string l2_prompt = Trim(config.layers.layer2.prompt_template).empty()
        ? ContextCompressionService::DefaultLayer2PromptTemplate()
        : config.layers.layer2.prompt_template;
    SetWindowTextW(l2_prompt_edit_, NormalizeMultilineForEdit(l2_prompt).c_str());

    Button_SetCheck(l3_enabled_, config.layers.layer3.enabled ? BST_CHECKED : BST_UNCHECKED);
    SelectComboForModel(l3_model_combo_, config.layers.layer3.model_provider_id, config.layers.layer3.model_id);
    SetWindowTextW(l3_max_tokens_edit_, std::to_wstring(config.layers.layer3.max_tokens).c_str());
    const std::string l3_prompt = Trim(config.layers.layer3.prompt_template).empty()
        ? ContextCompressionService::DefaultLayer3PromptTemplate()
        : config.layers.layer3.prompt_template;
    SetWindowTextW(l3_prompt_edit_, NormalizeMultilineForEdit(l3_prompt).c_str());

    Button_SetCheck(l4_enabled_, config.layers.layer4.enabled ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextW(l4_min_recent_edit_, std::to_wstring(config.layers.layer4.min_recent_turns).c_str());

    SetWindowTextW(frequency_edit_, std::to_wstring(config.frequency_every_n_prompts).c_str());
    SetWindowTextW(context_trigger_edit_, std::to_wstring(config.context_window_trigger_percent).c_str());

    UpdateLayerEnabledStates();
    Relayout();
}

ContextCompressionConfig CompressionManagerWindow::BuildConfigFromEditor() const {
    ContextCompressionConfig config =
        (selected_config_index_ >= 0 && static_cast<size_t>(selected_config_index_) < configs_.size())
            ? configs_[selected_config_index_]
            : ContextCompressionConfig{};

    if (config.id.empty()) {
        config.id = MakeId();
    }

    config.name = WideToUtf8(TrimWide(GetWindowTextString(config_name_edit_)));
    if (config.name.empty()) config.name = "Unnamed Config";

    config.strategy = ComboBox_GetCurSel(strategy_combo_) == 0
        ? ContextCompressionStrategy::TruncateTop
        : ContextCompressionStrategy::HierarchicalStructured;
    config.truncate_top_keep_messages = ParseInt(GetWindowTextString(truncate_keep_edit_)).value_or(20);

    config.layers.layer0.enabled = (Button_GetCheck(l0_enabled_) == BST_CHECKED);
    AssignModelFromCombo(l0_capture_model_combo_, config.layers.layer0.capture_model_provider_id, config.layers.layer0.capture_model_id);
    AssignModelFromCombo(l0_selection_model_combo_, config.layers.layer0.selection_model_provider_id, config.layers.layer0.selection_model_id);
    config.layers.layer0.capture_prompt_template = NormalizeMultilineFromEdit(GetWindowTextString(l0_capture_prompt_edit_));
    if (Trim(config.layers.layer0.capture_prompt_template).empty()) config.layers.layer0.capture_prompt_template.clear();
    config.layers.layer0.selection_prompt_template = NormalizeMultilineFromEdit(GetWindowTextString(l0_selection_prompt_edit_));
    if (Trim(config.layers.layer0.selection_prompt_template).empty()) config.layers.layer0.selection_prompt_template.clear();
    config.layers.layer0.storage_folder_template = WideToUtf8(GetWindowTextString(l0_storage_edit_));
    if (Trim(config.layers.layer0.storage_folder_template).empty()) {
        config.layers.layer0.storage_folder_template = "$ProjectFolder$\\.agent\\.memory\\$CHATID$";
    }
    config.layers.layer0.max_injected_rows = ParseInt(GetWindowTextString(l0_max_rows_edit_)).value_or(12);

    config.layers.layer1.enabled = (Button_GetCheck(l1_enabled_) == BST_CHECKED);
    config.layers.layer1.max_pins = ParseInt(GetWindowTextString(l1_max_pins_edit_)).value_or(10);
    config.layers.layer1.pin_code_blocks = (Button_GetCheck(l1_pin_code_) == BST_CHECKED);
    config.layers.layer1.pin_urls = (Button_GetCheck(l1_pin_urls_) == BST_CHECKED);
    config.layers.layer1.pin_numbers = (Button_GetCheck(l1_pin_numbers_) == BST_CHECKED);
    config.layers.layer1.pin_first_message = (Button_GetCheck(l1_pin_first_) == BST_CHECKED);
    config.layers.layer1.pin_user_flagged = (Button_GetCheck(l1_pin_user_flag_) == BST_CHECKED);
    config.layers.layer1.pin_explicit_instructions = (Button_GetCheck(l1_pin_instructions_) == BST_CHECKED);

    config.layers.layer2.enabled = (Button_GetCheck(l2_enabled_) == BST_CHECKED);
    AssignModelFromCombo(l2_model_combo_, config.layers.layer2.model_provider_id, config.layers.layer2.model_id);
    config.layers.layer2.max_tokens = ParseInt(GetWindowTextString(l2_max_tokens_edit_)).value_or(500);
    config.layers.layer2.trigger_threshold_turns = ParseInt(GetWindowTextString(l2_trigger_edit_)).value_or(8);
    config.layers.layer2.prompt_template = NormalizeMultilineFromEdit(GetWindowTextString(l2_prompt_edit_));
    if (Trim(config.layers.layer2.prompt_template).empty()) config.layers.layer2.prompt_template.clear();

    config.layers.layer3.enabled = (Button_GetCheck(l3_enabled_) == BST_CHECKED);
    AssignModelFromCombo(l3_model_combo_, config.layers.layer3.model_provider_id, config.layers.layer3.model_id);
    config.layers.layer3.max_tokens = ParseInt(GetWindowTextString(l3_max_tokens_edit_)).value_or(800);
    config.layers.layer3.prompt_template = NormalizeMultilineFromEdit(GetWindowTextString(l3_prompt_edit_));
    if (Trim(config.layers.layer3.prompt_template).empty()) config.layers.layer3.prompt_template.clear();

    config.layers.layer4.enabled = (Button_GetCheck(l4_enabled_) == BST_CHECKED);
    config.layers.layer4.min_recent_turns = ParseInt(GetWindowTextString(l4_min_recent_edit_)).value_or(2);

    config.frequency_every_n_prompts = ParseInt(GetWindowTextString(frequency_edit_)).value_or(0);
    config.context_window_trigger_percent = ParseInt(GetWindowTextString(context_trigger_edit_)).value_or(70);

    return config;
}

void CompressionManagerWindow::AddConfig() {
    ContextCompressionConfig config;
    config.id = MakeId();
    config.name = "New Compression Config";
    config.strategy = ContextCompressionStrategy::HierarchicalStructured;
    config.truncate_top_keep_messages = 20;
    config.frequency_every_n_prompts = 0;
    config.context_window_trigger_percent = 70;
    config.layers.layer0.enabled = false;
    config.layers.layer0.capture_prompt_template = ContextCompressionService::DefaultLayer0CapturePromptTemplate();
    config.layers.layer0.selection_prompt_template = ContextCompressionService::DefaultLayer0SelectionPromptTemplate();
    config.layers.layer0.storage_folder_template = "$ProjectFolder$\\.agent\\.memory\\$CHATID$";
    config.layers.layer0.max_injected_rows = 12;
    config.layers.layer1.enabled = true;
    config.layers.layer1.max_pins = 10;
    config.layers.layer1.pin_code_blocks = true;
    config.layers.layer1.pin_urls = true;
    config.layers.layer1.pin_numbers = true;
    config.layers.layer1.pin_first_message = true;
    config.layers.layer1.pin_user_flagged = true;
    config.layers.layer1.pin_explicit_instructions = true;
    config.layers.layer2.enabled = true;
    config.layers.layer2.max_tokens = 500;
    config.layers.layer2.trigger_threshold_turns = 8;
    config.layers.layer2.prompt_template = ContextCompressionService::DefaultLayer2PromptTemplate();
    config.layers.layer3.enabled = true;
    config.layers.layer3.max_tokens = 800;
    config.layers.layer3.prompt_template = ContextCompressionService::DefaultLayer3PromptTemplate();
    config.layers.layer4.enabled = true;
    config.layers.layer4.min_recent_turns = 2;

    configs_.push_back(config);
    selected_config_index_ = static_cast<int>(configs_.size()) - 1;
    RefreshConfigList();
    SelectConfig(selected_config_index_);
}

void CompressionManagerWindow::DeleteConfig() {
    if (selected_config_index_ < 0 || static_cast<size_t>(selected_config_index_) >= configs_.size()) return;
    configs_.erase(configs_.begin() + selected_config_index_);
    if (configs_.empty()) {
        selected_config_index_ = -1;
        AddConfig();
        return;
    }
    selected_config_index_ = std::clamp(selected_config_index_, 0, static_cast<int>(configs_.size()) - 1);
    RefreshConfigList();
    SelectConfig(selected_config_index_);
}

void CompressionManagerWindow::DuplicateConfig() {
    if (selected_config_index_ < 0 || static_cast<size_t>(selected_config_index_) >= configs_.size()) return;
    ContextCompressionConfig dup = BuildConfigFromEditor();
    dup.id = MakeId();
    dup.name += " (Copy)";
    configs_.push_back(dup);
    selected_config_index_ = static_cast<int>(configs_.size()) - 1;
    RefreshConfigList();
    SelectConfig(selected_config_index_);
}

void CompressionManagerWindow::SaveAllConfigs() {
    ContextCompressionConfig edited = BuildConfigFromEditor();
    if (selected_config_index_ >= 0 && static_cast<size_t>(selected_config_index_) < configs_.size()) {
        configs_[selected_config_index_] = edited;
    } else {
        configs_.push_back(edited);
        selected_config_index_ = static_cast<int>(configs_.size()) - 1;
    }
    service_->SaveGlobalConfigs(configs_);
}

void CompressionManagerWindow::OnCommand(int control_id, int notification_code) {
    switch (control_id) {
    case kConfigList:
        if (notification_code == LBN_SELCHANGE) {
            SelectConfig(ListBox_GetCurSel(config_list_));
        }
        break;
    case kAddConfig:
        AddConfig();
        break;
    case kDeleteConfig:
        DeleteConfig();
        break;
    case kDuplicateConfig:
        DuplicateConfig();
        break;
    case kStrategyCombo:
        if (notification_code == CBN_SELCHANGE) {
            scroll_offset_ = 0;
            Relayout();
        }
        break;
    case kL0Enabled:
    case kL1Enabled:
    case kL2Enabled:
    case kL3Enabled:
    case kL4Enabled:
        UpdateLayerEnabledStates();
        break;
    case kL0CapturePromptDefault:
        SetWindowTextW(l0_capture_prompt_edit_, NormalizeMultilineForEdit(ContextCompressionService::DefaultLayer0CapturePromptTemplate()).c_str());
        break;
    case kL0SelectionPromptDefault:
        SetWindowTextW(l0_selection_prompt_edit_, NormalizeMultilineForEdit(ContextCompressionService::DefaultLayer0SelectionPromptTemplate()).c_str());
        break;
    case kL2PromptDefault:
        SetWindowTextW(l2_prompt_edit_, NormalizeMultilineForEdit(ContextCompressionService::DefaultLayer2PromptTemplate()).c_str());
        break;
    case kL3PromptDefault:
        SetWindowTextW(l3_prompt_edit_, NormalizeMultilineForEdit(ContextCompressionService::DefaultLayer3PromptTemplate()).c_str());
        break;
    case kL0StorageBrowse: {
        const std::wstring selected = BrowseForFolder(hwnd_);
        if (!selected.empty()) {
            SetWindowTextW(l0_storage_edit_, selected.c_str());
        }
        break;
    }
    case kSaveButton:
        SaveAllConfigs();
        if (on_changed_) on_changed_();
        DestroyWindow(hwnd_);
        break;
    case kCancelButton:
        DestroyWindow(hwnd_);
        break;
    default:
        break;
    }
}

}  // namespace

HWND CreateContextCompressionManagerWindow(
    HWND owner,
    ContextCompressionService* compression_service,
    AppStorage* storage,
    std::function<std::vector<ProviderConfig>()> get_providers,
    std::function<void()> on_changed) {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    auto* window = new CompressionManagerWindow(
        owner,
        compression_service,
        storage,
        std::move(get_providers),
        std::move(on_changed));
    return window->Create(instance);
}
