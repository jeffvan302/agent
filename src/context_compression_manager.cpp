#include "context_compression_manager.h"

#include "prompt_dialog.h"
#include "util.h"

#include <commctrl.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr wchar_t kCompressionManagerClassName[] = L"AgentContextCompressionManagerWindow";

enum ControlId : int {
    kConfigList = 6301,
    kAddConfig = 6302,
    kDeleteConfig = 6303,
    kDuplicateConfig = 6304,
    kConfigNameLabel = 6305,
    kConfigNameEdit = 6306,
    kStrategyLabel = 6307,
    kStrategyCombo = 6308,
    kTruncateTopPanel = 6309,
    kTruncateTopLabel = 6310,
    kTruncateTopEdit = 6311,
    kHscPanel = 6312,
    kL1Enabled = 6313,
    kL1MaxPinsLabel = 6314,
    kL1MaxPinsEdit = 6315,
    kL1PinCode = 6316,
    kL1PinUrls = 6317,
    kL1PinNumbers = 6318,
    kL1PinFirst = 6319,
    kL1PinUserFlag = 6320,
    kL1PinInstructions = 6342,
    kL2Enabled = 6321,
    kL2ModelLabel = 6322,
    kL2ModelCombo = 6323,
    kL2MaxTokensLabel = 6324,
    kL2MaxTokensEdit = 6325,
    kL2TriggerLabel = 6326,
    kL2TriggerEdit = 6327,
    kL3Enabled = 6328,
    kL3ModelLabel = 6329,
    kL3ModelCombo = 6330,
    kL3MaxTokensLabel = 6331,
    kL3MaxTokensEdit = 6332,
    kL4Enabled = 6333,
    kL4MinRecentLabel = 6334,
    kL4MinRecentEdit = 6335,
    kFrequencyLabel = 6336,
    kFrequencyEdit = 6337,
    kContextTriggerLabel = 6340,
    kContextTriggerEdit = 6341,
    kProjectsPanel = 6338,
    kProjectsList = 6339,
    kSaveButton = IDOK,
    kCancelButton = IDCANCEL,
};

struct ProjectAssignment {
    std::string project_id;
    std::string project_name;
    std::string config_id;
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
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

    void OnCreate();
    void LayoutControls(int width, int height) const;
    void OnCommand(int control_id, int notification_code);
    void RefreshConfigList();
    void SelectConfig(int index);
    void AddConfig();
    void DeleteConfig();
    void DuplicateConfig();
    void LoadConfigToEditor(const ContextCompressionConfig& config);
    ContextCompressionConfig BuildConfigFromEditor() const;
    void SaveAllConfigs();
    void Relayout() const;

    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    ContextCompressionService* service_ = nullptr;
    AppStorage* storage_ = nullptr;
    std::function<std::vector<ProviderConfig>()> get_providers_;
    std::function<void()> on_changed_;
    std::vector<ContextCompressionConfig> configs_;
    std::vector<ProjectAssignment> project_assignments_;
    int selected_config_index_ = -1;

    HWND config_list_ = nullptr;
    HWND add_button_ = nullptr;
    HWND delete_button_ = nullptr;
    HWND duplicate_button_ = nullptr;
    HWND config_name_label_ = nullptr;
    HWND config_name_edit_ = nullptr;
    HWND strategy_label_ = nullptr;
    HWND strategy_combo_ = nullptr;
    HWND truncate_top_panel_ = nullptr;
    HWND truncate_top_label_ = nullptr;
    HWND truncate_top_edit_ = nullptr;
    HWND hsc_panel_ = nullptr;
    HWND l1_enabled_ = nullptr;
    HWND l1_max_pins_label_ = nullptr;
    HWND l1_max_pins_edit_ = nullptr;
    HWND l1_pin_code_ = nullptr;
    HWND l1_pin_urls_ = nullptr;
    HWND l1_pin_numbers_ = nullptr;
    HWND l1_pin_first_ = nullptr;
    HWND l1_pin_user_flag_ = nullptr;
    HWND l1_pin_instructions_ = nullptr;
    HWND l2_enabled_ = nullptr;
    HWND l2_model_label_ = nullptr;
    HWND l2_model_combo_ = nullptr;
    HWND l2_max_tokens_label_ = nullptr;
    HWND l2_max_tokens_edit_ = nullptr;
    HWND l2_trigger_label_ = nullptr;
    HWND l2_trigger_edit_ = nullptr;
    HWND l3_enabled_ = nullptr;
    HWND l3_model_label_ = nullptr;
    HWND l3_model_combo_ = nullptr;
    HWND l3_max_tokens_label_ = nullptr;
    HWND l3_max_tokens_edit_ = nullptr;
    HWND l4_enabled_ = nullptr;
    HWND l4_min_recent_label_ = nullptr;
    HWND l4_min_recent_edit_ = nullptr;
    HWND frequency_label_ = nullptr;
    HWND frequency_edit_ = nullptr;
    HWND context_trigger_label_ = nullptr;
    HWND context_trigger_edit_ = nullptr;
    HWND projects_list_ = nullptr;
    HWND save_button_ = nullptr;
    HWND cancel_button_ = nullptr;
};

HWND CompressionManagerWindow::Create(HINSTANCE instance) {
    RegisterWindowClass(instance);
    hwnd_ = CreateWindowExW(
        WS_EX_APPWINDOW,
        kCompressionManagerClassName,
        L"Context Window Editor",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1100,
        720,
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

void CompressionManagerWindow::OnCreate() {
    font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    config_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kConfigList), nullptr, nullptr);

    add_button_ = CreateWindowExW(0, L"BUTTON", L"Add", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kAddConfig), nullptr, nullptr);
    delete_button_ = CreateWindowExW(0, L"BUTTON", L"Delete", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kDeleteConfig), nullptr, nullptr);
    duplicate_button_ = CreateWindowExW(0, L"BUTTON", L"Duplicate", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kDuplicateConfig), nullptr, nullptr);

    config_name_label_ = CreateWindowExW(0, L"STATIC", L"Name:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kConfigNameLabel), nullptr, nullptr);
    config_name_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kConfigNameEdit), nullptr, nullptr);

    strategy_label_ = CreateWindowExW(0, L"STATIC", L"Strategy:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kStrategyLabel), nullptr, nullptr);
    strategy_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kStrategyCombo), nullptr, nullptr);

    // Truncate Top panel
    truncate_top_panel_ = CreateWindowExW(0, L"BUTTON", nullptr, WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kTruncateTopPanel), nullptr, nullptr);
    truncate_top_label_ = CreateWindowExW(0, L"STATIC", L"Keep recent messages:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kTruncateTopLabel), nullptr, nullptr);
    truncate_top_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"20", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kTruncateTopEdit), nullptr, nullptr);

    // HSC panel
    hsc_panel_ = CreateWindowExW(0, L"BUTTON", nullptr, WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kHscPanel), nullptr, nullptr);

    // Layer 1
    l1_enabled_ = CreateWindowExW(0, L"BUTTON", L"L1: Verbatim Pinning", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL1Enabled), nullptr, nullptr);
    l1_max_pins_label_ = CreateWindowExW(0, L"STATIC", L"Max pins:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL1MaxPinsLabel), nullptr, nullptr);
    l1_max_pins_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"10", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL1MaxPinsEdit), nullptr, nullptr);
    l1_pin_code_ = CreateWindowExW(0, L"BUTTON", L"Pin code", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL1PinCode), nullptr, nullptr);
    l1_pin_urls_ = CreateWindowExW(0, L"BUTTON", L"Pin URLs", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL1PinUrls), nullptr, nullptr);
    l1_pin_numbers_ = CreateWindowExW(0, L"BUTTON", L"Pin numbers", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL1PinNumbers), nullptr, nullptr);
    l1_pin_first_ = CreateWindowExW(0, L"BUTTON", L"Pin first msg", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL1PinFirst), nullptr, nullptr);
    l1_pin_user_flag_ = CreateWindowExW(0, L"BUTTON", L"Pin [PIN]", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL1PinUserFlag), nullptr, nullptr);
    l1_pin_instructions_ = CreateWindowExW(0, L"BUTTON", L"Pin instructions", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL1PinInstructions), nullptr, nullptr);

    // Layer 2
    l2_enabled_ = CreateWindowExW(0, L"BUTTON", L"L2: Summary", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL2Enabled), nullptr, nullptr);
    l2_model_label_ = CreateWindowExW(0, L"STATIC", L"Model:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL2ModelLabel), nullptr, nullptr);
    l2_model_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL2ModelCombo), nullptr, nullptr);
    l2_max_tokens_label_ = CreateWindowExW(0, L"STATIC", L"Max tokens:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL2MaxTokensLabel), nullptr, nullptr);
    l2_max_tokens_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"500", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL2MaxTokensEdit), nullptr, nullptr);
    l2_trigger_label_ = CreateWindowExW(0, L"STATIC", L"Trigger (turns):", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL2TriggerLabel), nullptr, nullptr);
    l2_trigger_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"8", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL2TriggerEdit), nullptr, nullptr);

    // Layer 3
    l3_enabled_ = CreateWindowExW(0, L"BUTTON", L"L3: State", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL3Enabled), nullptr, nullptr);
    l3_model_label_ = CreateWindowExW(0, L"STATIC", L"Model:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL3ModelLabel), nullptr, nullptr);
    l3_model_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL3ModelCombo), nullptr, nullptr);
    l3_max_tokens_label_ = CreateWindowExW(0, L"STATIC", L"Max tokens:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL3MaxTokensLabel), nullptr, nullptr);
    l3_max_tokens_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"800", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL3MaxTokensEdit), nullptr, nullptr);

    // Layer 4
    l4_enabled_ = CreateWindowExW(0, L"BUTTON", L"L4: Recency", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL4Enabled), nullptr, nullptr);
    l4_min_recent_label_ = CreateWindowExW(0, L"STATIC", L"Min recent:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL4MinRecentLabel), nullptr, nullptr);
    l4_min_recent_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"2", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kL4MinRecentEdit), nullptr, nullptr);

    // Frequency
    frequency_label_ = CreateWindowExW(0, L"STATIC", L"Frequency (every N prompts, 0=manual):", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kFrequencyLabel), nullptr, nullptr);
    frequency_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kFrequencyEdit), nullptr, nullptr);

    // Context window trigger
    context_trigger_label_ = CreateWindowExW(0, L"STATIC", L"Context trigger (%, 0=manual):", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kContextTriggerLabel), nullptr, nullptr);
    context_trigger_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"70", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kContextTriggerEdit), nullptr, nullptr);

    // Projects panel
    projects_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kProjectsList), nullptr, nullptr);

    save_button_ = CreateWindowExW(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kSaveButton), nullptr, nullptr);
    cancel_button_ = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kCancelButton), nullptr, nullptr);

    // Apply font to all controls
    HWND all_controls[] = {
        config_list_, add_button_, delete_button_, duplicate_button_,
        config_name_label_, config_name_edit_, strategy_label_, strategy_combo_,
        truncate_top_panel_, truncate_top_label_, truncate_top_edit_,
        hsc_panel_, l1_enabled_, l1_max_pins_label_, l1_max_pins_edit_,
        l1_pin_code_, l1_pin_urls_, l1_pin_numbers_, l1_pin_first_, l1_pin_user_flag_, l1_pin_instructions_,
        l2_enabled_, l2_model_label_, l2_model_combo_, l2_max_tokens_label_, l2_max_tokens_edit_,
        l2_trigger_label_, l2_trigger_edit_,
        l3_enabled_, l3_model_label_, l3_model_combo_, l3_max_tokens_label_, l3_max_tokens_edit_,
        l4_enabled_, l4_min_recent_label_, l4_min_recent_edit_,
        frequency_label_, frequency_edit_, context_trigger_label_, context_trigger_edit_,
        projects_list_,
        save_button_, cancel_button_
    };
    for (HWND ctrl : all_controls) {
        SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }

    // Populate strategy dropdown
    ComboBox_AddString(strategy_combo_, L"Truncate Top (Rolling Window)");
    ComboBox_AddString(strategy_combo_, L"Hierarchical Structured Compression");

    // Populate model dropdowns - store provider_index:model_index as LPARAM
    auto providers = get_providers_();
    int global_idx = 0;
    for (size_t pi = 0; pi < providers.size(); ++pi) {
        for (size_t mi = 0; mi < providers[pi].models.size(); ++mi) {
            const auto& model = providers[pi].models[mi];
            std::wstring label = Utf8ToWide(providers[pi].name) + L" / " + Utf8ToWide(model.id);
            int idx = ComboBox_AddString(l2_model_combo_, label.c_str());
            ComboBox_SetItemData(l2_model_combo_, idx, static_cast<LPARAM>(global_idx));
            idx = ComboBox_AddString(l3_model_combo_, label.c_str());
            ComboBox_SetItemData(l3_model_combo_, idx, static_cast<LPARAM>(global_idx));
            ++global_idx;
        }
    }

    // Set defaults
    Button_SetCheck(l1_enabled_, BST_CHECKED);
    Button_SetCheck(l1_pin_code_, BST_CHECKED);
    Button_SetCheck(l1_pin_urls_, BST_CHECKED);
    Button_SetCheck(l1_pin_numbers_, BST_CHECKED);
    Button_SetCheck(l1_pin_first_, BST_CHECKED);
    Button_SetCheck(l1_pin_instructions_, BST_CHECKED);
    Button_SetCheck(l1_pin_user_flag_, BST_CHECKED);
    Button_SetCheck(l2_enabled_, BST_CHECKED);
    Button_SetCheck(l3_enabled_, BST_CHECKED);
    Button_SetCheck(l4_enabled_, BST_CHECKED);
    ComboBox_SetCurSel(strategy_combo_, 0);

    configs_ = service_->LoadGlobalConfigs();
    RefreshConfigList();

    CenterWindowToOwner(hwnd_, owner_);
    LayoutControls(1100, 720);
}

void CompressionManagerWindow::LayoutControls(int width, int height) const {
    const int margin = Scale(hwnd_, 12);
    const int gutter = Scale(hwnd_, 8);
    const int label_height = Scale(hwnd_, 18);
    const int edit_height = Scale(hwnd_, 24);
    const int button_height = Scale(hwnd_, 28);
    const int button_width = Scale(hwnd_, 80);
    const int left_width = Scale(hwnd_, 260);
    const int right_x = left_width + margin + gutter;
    const int right_width = width - right_x - margin;

    int y = margin;

    // Left panel: config list and buttons
    MoveWindow(add_button_, margin, y, button_width, button_height, TRUE);
    MoveWindow(delete_button_, margin + button_width + gutter, y, button_width, button_height, TRUE);
    MoveWindow(duplicate_button_, margin + (button_width + gutter) * 2, y, button_width, button_height, TRUE);
    y += button_height + gutter;
    MoveWindow(config_list_, margin, y, left_width, height - margin - button_height - gutter * 3 - label_height - edit_height - margin, TRUE);

    // Right panel: config editor
    int editor_x = right_x;
    int editor_width = right_width;
    y = margin;

    MoveWindow(config_name_label_, editor_x, y, 60, label_height, TRUE);
    MoveWindow(config_name_edit_, editor_x + 60, y, editor_width - 60, edit_height, TRUE);
    y += edit_height + gutter;

    MoveWindow(strategy_label_, editor_x, y, 60, label_height, TRUE);
    MoveWindow(strategy_combo_, editor_x + 60, y, editor_width - 60, Scale(hwnd_, 120), TRUE);
    y += edit_height + gutter;

    // Determine current strategy
    int strategy_sel = ComboBox_GetCurSel(strategy_combo_);
    bool is_truncate = (strategy_sel == 0);
    const int truncate_display = is_truncate ? SW_SHOW : SW_HIDE;
    const int hsc_display = is_truncate ? SW_HIDE : SW_SHOW;

    HWND truncate_controls[] = {truncate_top_panel_, truncate_top_label_, truncate_top_edit_};
    for (HWND control : truncate_controls) {
        ShowWindow(control, truncate_display);
    }
    HWND hsc_controls[] = {
        hsc_panel_, l1_enabled_, l1_max_pins_label_, l1_max_pins_edit_,
        l1_pin_code_, l1_pin_urls_, l1_pin_numbers_, l1_pin_first_, l1_pin_user_flag_, l1_pin_instructions_,
        l2_enabled_, l2_model_label_, l2_model_combo_, l2_max_tokens_label_, l2_max_tokens_edit_, l2_trigger_label_, l2_trigger_edit_,
        l3_enabled_, l3_model_label_, l3_model_combo_, l3_max_tokens_label_, l3_max_tokens_edit_,
        l4_enabled_, l4_min_recent_label_, l4_min_recent_edit_,
    };
    for (HWND control : hsc_controls) {
        ShowWindow(control, hsc_display);
    }

    // Strategy-specific panel
    if (is_truncate) {
        // Truncate Top panel - only show this
        MoveWindow(truncate_top_panel_, editor_x, y, editor_width, Scale(hwnd_, 50), TRUE);
        MoveWindow(truncate_top_label_, editor_x + gutter, y + Scale(hwnd_, 16), Scale(hwnd_, 130), label_height, TRUE);
        MoveWindow(truncate_top_edit_, editor_x + gutter + Scale(hwnd_, 135), y + Scale(hwnd_, 14), Scale(hwnd_, 60), edit_height, TRUE);
        y += Scale(hwnd_, 55) + gutter;
    } else {
        // HSC panel - only show this
        MoveWindow(hsc_panel_, editor_x, y, editor_width, Scale(hwnd_, 375), TRUE);
        int hsc_y = y + Scale(hwnd_, 20);
        int col1_x = editor_x + gutter;

        // L1
        MoveWindow(l1_enabled_, col1_x, hsc_y, Scale(hwnd_, 160), Scale(hwnd_, 22), TRUE);
        MoveWindow(l1_max_pins_label_, col1_x, hsc_y + Scale(hwnd_, 24), Scale(hwnd_, 60), label_height, TRUE);
        MoveWindow(l1_max_pins_edit_, col1_x + Scale(hwnd_, 65), hsc_y + Scale(hwnd_, 22), Scale(hwnd_, 50), edit_height, TRUE);
        MoveWindow(l1_pin_code_, col1_x, hsc_y + Scale(hwnd_, 48), Scale(hwnd_, 90), Scale(hwnd_, 22), TRUE);
        MoveWindow(l1_pin_urls_, col1_x + Scale(hwnd_, 95), hsc_y + Scale(hwnd_, 48), Scale(hwnd_, 90), Scale(hwnd_, 22), TRUE);
        MoveWindow(l1_pin_numbers_, col1_x, hsc_y + Scale(hwnd_, 72), Scale(hwnd_, 90), Scale(hwnd_, 22), TRUE);
        MoveWindow(l1_pin_first_, col1_x + Scale(hwnd_, 95), hsc_y + Scale(hwnd_, 72), Scale(hwnd_, 90), Scale(hwnd_, 22), TRUE);
        MoveWindow(l1_pin_user_flag_, col1_x, hsc_y + Scale(hwnd_, 96), Scale(hwnd_, 90), Scale(hwnd_, 22), TRUE);
        MoveWindow(l1_pin_instructions_, col1_x + Scale(hwnd_, 95), hsc_y + Scale(hwnd_, 96), Scale(hwnd_, 130), Scale(hwnd_, 22), TRUE);

        // L2
        MoveWindow(l2_enabled_, col1_x, hsc_y + Scale(hwnd_, 125), Scale(hwnd_, 120), Scale(hwnd_, 22), TRUE);
        MoveWindow(l2_model_label_, col1_x, hsc_y + Scale(hwnd_, 150), 50, label_height, TRUE);
        MoveWindow(l2_model_combo_, col1_x + 55, hsc_y + Scale(hwnd_, 148), editor_width / 2 - gutter - 55, Scale(hwnd_, 120), TRUE);
        MoveWindow(l2_max_tokens_label_, col1_x, hsc_y + Scale(hwnd_, 175), 70, label_height, TRUE);
        MoveWindow(l2_max_tokens_edit_, col1_x + 75, hsc_y + Scale(hwnd_, 173), Scale(hwnd_, 60), edit_height, TRUE);
        MoveWindow(l2_trigger_label_, col1_x + Scale(hwnd_, 140), hsc_y + Scale(hwnd_, 175), 100, label_height, TRUE);
        MoveWindow(l2_trigger_edit_, col1_x + Scale(hwnd_, 245), hsc_y + Scale(hwnd_, 173), Scale(hwnd_, 50), edit_height, TRUE);

        // L3
        MoveWindow(l3_enabled_, col1_x, hsc_y + Scale(hwnd_, 205), Scale(hwnd_, 120), Scale(hwnd_, 22), TRUE);
        MoveWindow(l3_model_label_, col1_x, hsc_y + Scale(hwnd_, 230), 50, label_height, TRUE);
        MoveWindow(l3_model_combo_, col1_x + 55, hsc_y + Scale(hwnd_, 228), editor_width / 2 - gutter - 55, Scale(hwnd_, 120), TRUE);
        MoveWindow(l3_max_tokens_label_, col1_x, hsc_y + Scale(hwnd_, 255), 70, label_height, TRUE);
        MoveWindow(l3_max_tokens_edit_, col1_x + 75, hsc_y + Scale(hwnd_, 253), Scale(hwnd_, 60), edit_height, TRUE);

        // L4
        MoveWindow(l4_enabled_, col1_x, hsc_y + Scale(hwnd_, 285), Scale(hwnd_, 120), Scale(hwnd_, 22), TRUE);
        MoveWindow(l4_min_recent_label_, col1_x, hsc_y + Scale(hwnd_, 310), 80, label_height, TRUE);
        MoveWindow(l4_min_recent_edit_, col1_x + 85, hsc_y + Scale(hwnd_, 308), Scale(hwnd_, 50), edit_height, TRUE);

        y += Scale(hwnd_, 390) + gutter;
    }

    // Common settings (frequency and context trigger)
    MoveWindow(frequency_label_, editor_x, y, Scale(hwnd_, 220), label_height, TRUE);
    MoveWindow(frequency_edit_, editor_x + Scale(hwnd_, 225), y, Scale(hwnd_, 60), edit_height, TRUE);
    MoveWindow(context_trigger_label_, editor_x + Scale(hwnd_, 295), y, Scale(hwnd_, 200), label_height, TRUE);
    MoveWindow(context_trigger_edit_, editor_x + Scale(hwnd_, 500), y, Scale(hwnd_, 60), edit_height, TRUE);
    y += edit_height + gutter * 2;

    // Projects panel
    MoveWindow(projects_list_, editor_x, y, editor_width, height - y - margin - button_height - gutter, TRUE);

    // Bottom buttons
    const int buttons_y = height - margin - button_height;
    MoveWindow(cancel_button_, width - margin - button_width, buttons_y, button_width, button_height, TRUE);
    MoveWindow(save_button_, width - margin - button_width * 2 - gutter, buttons_y, button_width, button_height, TRUE);
}

void CompressionManagerWindow::Relayout() const {
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    LayoutControls(rect.right - rect.left, rect.bottom - rect.top);
}

void CompressionManagerWindow::OnCommand(int control_id, int notification_code) {
    switch (control_id) {
    case kConfigList:
        if (notification_code == LBN_SELCHANGE) {
            int sel = ListBox_GetCurSel(config_list_);
            SelectConfig(sel);
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
            Relayout();
        }
        break;
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

void CompressionManagerWindow::RefreshConfigList() {
    ListBox_ResetContent(config_list_);
    for (size_t i = 0; i < configs_.size(); ++i) {
        ListBox_AddString(config_list_, Utf8ToWide(configs_[i].name).c_str());
    }
    if (selected_config_index_ >= 0 && static_cast<size_t>(selected_config_index_) < configs_.size()) {
        ListBox_SetCurSel(config_list_, selected_config_index_);
    }
}

void CompressionManagerWindow::SelectConfig(int index) {
    if (index < 0 || static_cast<size_t>(index) >= configs_.size()) {
        return;
    }
    selected_config_index_ = index;
    LoadConfigToEditor(configs_[index]);
}

void CompressionManagerWindow::LoadConfigToEditor(const ContextCompressionConfig& config) {
    SetWindowTextW(config_name_edit_, Utf8ToWide(config.name).c_str());

    int strategy_idx = (config.strategy == ContextCompressionStrategy::TruncateTop) ? 0 : 1;
    ComboBox_SetCurSel(strategy_combo_, strategy_idx);

    bool is_truncate = (config.strategy == ContextCompressionStrategy::TruncateTop);
    ShowWindow(truncate_top_panel_, is_truncate ? SW_SHOW : SW_HIDE);
    ShowWindow(hsc_panel_, is_truncate ? SW_HIDE : SW_SHOW);

    SetWindowTextW(truncate_top_edit_, std::to_wstring(config.truncate_top_keep_messages).c_str());
    SetWindowTextW(frequency_edit_, std::to_wstring(config.frequency_every_n_prompts).c_str());
    SetWindowTextW(context_trigger_edit_, std::to_wstring(config.context_window_trigger_percent).c_str());

    // L1
    Button_SetCheck(l1_enabled_, config.layers.layer1.enabled ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextW(l1_max_pins_edit_, std::to_wstring(config.layers.layer1.max_pins).c_str());
    Button_SetCheck(l1_pin_code_, config.layers.layer1.pin_code_blocks ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(l1_pin_urls_, config.layers.layer1.pin_urls ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(l1_pin_numbers_, config.layers.layer1.pin_numbers ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(l1_pin_first_, config.layers.layer1.pin_first_message ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(l1_pin_instructions_, config.layers.layer1.pin_explicit_instructions ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(l1_pin_user_flag_, config.layers.layer1.pin_user_flagged ? BST_CHECKED : BST_UNCHECKED);

    // L2
    Button_SetCheck(l2_enabled_, config.layers.layer2.enabled ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextW(l2_max_tokens_edit_, std::to_wstring(config.layers.layer2.max_tokens).c_str());
    SetWindowTextW(l2_trigger_edit_, std::to_wstring(config.layers.layer2.trigger_threshold_turns).c_str());

    // Find and select the model in the combo using provider_id + model_id
    auto providers = get_providers_();
    int l2_sel = -1;
    int l3_sel = -1;
    int global_idx = 0;
    for (size_t pi = 0; pi < providers.size() && (l2_sel < 0 || l3_sel < 0); ++pi) {
        for (size_t mi = 0; mi < providers[pi].models.size(); ++mi) {
            if (l2_sel < 0 && providers[pi].models[mi].id == config.layers.layer2.model_id &&
                providers[pi].id == config.layers.layer2.model_provider_id) {
                l2_sel = global_idx;
            }
            if (l3_sel < 0 && providers[pi].models[mi].id == config.layers.layer3.model_id &&
                providers[pi].id == config.layers.layer3.model_provider_id) {
                l3_sel = global_idx;
            }
            ++global_idx;
        }
    }
    ComboBox_SetCurSel(l2_model_combo_, l2_sel >= 0 ? l2_sel : -1);

    // L3
    Button_SetCheck(l3_enabled_, config.layers.layer3.enabled ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextW(l3_max_tokens_edit_, std::to_wstring(config.layers.layer3.max_tokens).c_str());

    ComboBox_SetCurSel(l3_model_combo_, l3_sel >= 0 ? l3_sel : -1);

    // L4
    Button_SetCheck(l4_enabled_, config.layers.layer4.enabled ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextW(l4_min_recent_edit_, std::to_wstring(config.layers.layer4.min_recent_turns).c_str());
    Relayout();
}

ContextCompressionConfig CompressionManagerWindow::BuildConfigFromEditor() const {
    ContextCompressionConfig config;
    config.id = (selected_config_index_ >= 0 && static_cast<size_t>(selected_config_index_) < configs_.size())
        ? configs_[selected_config_index_].id : MakeId();
    config.name = WideToUtf8(TrimWide(GetWindowTextString(config_name_edit_)));
    if (config.name.empty()) config.name = "Unnamed Config";

    int strategy_idx = ComboBox_GetCurSel(strategy_combo_);
    config.strategy = (strategy_idx == 0) ? ContextCompressionStrategy::TruncateTop : ContextCompressionStrategy::HierarchicalStructured;

    config.truncate_top_keep_messages = ParseInt(GetWindowTextString(truncate_top_edit_)).value_or(20);
    config.frequency_every_n_prompts = ParseInt(GetWindowTextString(frequency_edit_)).value_or(0);
    config.context_window_trigger_percent = ParseInt(GetWindowTextString(context_trigger_edit_)).value_or(70);

    // L1
    config.layers.layer1.enabled = (Button_GetCheck(l1_enabled_) == BST_CHECKED);
    config.layers.layer1.max_pins = ParseInt(GetWindowTextString(l1_max_pins_edit_)).value_or(10);
    config.layers.layer1.pin_code_blocks = (Button_GetCheck(l1_pin_code_) == BST_CHECKED);
    config.layers.layer1.pin_urls = (Button_GetCheck(l1_pin_urls_) == BST_CHECKED);
    config.layers.layer1.pin_numbers = (Button_GetCheck(l1_pin_numbers_) == BST_CHECKED);
    config.layers.layer1.pin_first_message = (Button_GetCheck(l1_pin_first_) == BST_CHECKED);
    config.layers.layer1.pin_explicit_instructions = (Button_GetCheck(l1_pin_instructions_) == BST_CHECKED);
    config.layers.layer1.pin_user_flagged = (Button_GetCheck(l1_pin_user_flag_) == BST_CHECKED);

    // L2
    config.layers.layer2.enabled = (Button_GetCheck(l2_enabled_) == BST_CHECKED);
    config.layers.layer2.max_tokens = ParseInt(GetWindowTextString(l2_max_tokens_edit_)).value_or(500);
    config.layers.layer2.trigger_threshold_turns = ParseInt(GetWindowTextString(l2_trigger_edit_)).value_or(8);

    auto assign_model_from_combo_index = [&](int selected_idx, std::string& provider_id, std::string& model_id) {
        if (selected_idx < 0) {
            return;
        }
        auto providers = get_providers_();
        int global_idx = 0;
        for (size_t pi = 0; pi < providers.size(); ++pi) {
            for (size_t mi = 0; mi < providers[pi].models.size(); ++mi) {
                if (global_idx == selected_idx) {
                    model_id = providers[pi].models[mi].id;
                    provider_id = providers[pi].id;
                    return;
                }
                ++global_idx;
            }
        }
    };

    // Get provider/model from combo selection
    assign_model_from_combo_index(
        ComboBox_GetCurSel(l2_model_combo_),
        config.layers.layer2.model_provider_id,
        config.layers.layer2.model_id);

    // L3
    config.layers.layer3.enabled = (Button_GetCheck(l3_enabled_) == BST_CHECKED);
    config.layers.layer3.max_tokens = ParseInt(GetWindowTextString(l3_max_tokens_edit_)).value_or(800);

    assign_model_from_combo_index(
        ComboBox_GetCurSel(l3_model_combo_),
        config.layers.layer3.model_provider_id,
        config.layers.layer3.model_id);

    // L4
    config.layers.layer4.enabled = (Button_GetCheck(l4_enabled_) == BST_CHECKED);
    config.layers.layer4.min_recent_turns = ParseInt(GetWindowTextString(l4_min_recent_edit_)).value_or(2);

    return config;
}

void CompressionManagerWindow::AddConfig() {
    ContextCompressionConfig new_config;
    new_config.id = MakeId();
    new_config.name = "New Compression Config";
    new_config.strategy = ContextCompressionStrategy::TruncateTop;
    new_config.truncate_top_keep_messages = 20;
    new_config.frequency_every_n_prompts = 0;
    new_config.context_window_trigger_percent = 70;
    new_config.layers.layer1.enabled = true;
    new_config.layers.layer1.max_pins = 10;
    new_config.layers.layer1.pin_code_blocks = true;
    new_config.layers.layer1.pin_urls = true;
    new_config.layers.layer1.pin_numbers = true;
    new_config.layers.layer1.pin_first_message = true;
    new_config.layers.layer1.pin_explicit_instructions = true;
    new_config.layers.layer1.pin_user_flagged = true;
    new_config.layers.layer2.enabled = true;
    new_config.layers.layer2.max_tokens = 500;
    new_config.layers.layer2.trigger_threshold_turns = 8;
    new_config.layers.layer3.enabled = true;
    new_config.layers.layer3.max_tokens = 800;
    new_config.layers.layer4.enabled = true;
    new_config.layers.layer4.min_recent_turns = 2;

    configs_.push_back(new_config);
    selected_config_index_ = static_cast<int>(configs_.size()) - 1;
    RefreshConfigList();
    LoadConfigToEditor(new_config);
}

void CompressionManagerWindow::DeleteConfig() {
    if (selected_config_index_ < 0 || static_cast<size_t>(selected_config_index_) >= configs_.size()) {
        return;
    }
    configs_.erase(configs_.begin() + selected_config_index_);
    selected_config_index_ = -1;
    RefreshConfigList();
}

void CompressionManagerWindow::DuplicateConfig() {
    if (selected_config_index_ < 0 || static_cast<size_t>(selected_config_index_) >= configs_.size()) {
        return;
    }
    ContextCompressionConfig dup = configs_[selected_config_index_];
    dup.id = MakeId();
    dup.name = dup.name + " (Copy)";
    configs_.push_back(dup);
    selected_config_index_ = static_cast<int>(configs_.size()) - 1;
    RefreshConfigList();
    LoadConfigToEditor(dup);
}

void CompressionManagerWindow::SaveAllConfigs() {
    ContextCompressionConfig edited = BuildConfigFromEditor();
    if (selected_config_index_ >= 0 && static_cast<size_t>(selected_config_index_) < configs_.size()) {
        configs_[selected_config_index_] = edited;
    }
    service_->SaveGlobalConfigs(configs_);
}

}  // namespace

HWND CreateContextCompressionManagerWindow(
    HWND owner,
    ContextCompressionService* compression_service,
    AppStorage* storage,
    std::function<std::vector<ProviderConfig>()> get_providers,
    std::function<void()> on_changed) {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    auto* window = new CompressionManagerWindow(owner, compression_service, storage,
        std::move(get_providers), std::move(on_changed));
    return window->Create(instance);
}
