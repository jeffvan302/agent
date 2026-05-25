#include "context_compression_manager.h"

#include "util.h"

#include <commctrl.h>
#include <shellapi.h>
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
constexpr UINT_PTR kHelpHoverTimer = 1;

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
    kPrePassLabel = 6373,
    kPrePassCombo = 6374,

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

    kHelpPanel = 6369,
    kHelpTitle = 6370,
    kHelpText = 6371,
    kHelpLink = 6372,

    kSaveButton = IDOK,
    kCancelButton = IDCANCEL,
};

struct HelpTopic {
    const wchar_t* title = L"Context window editor";
    const wchar_t* body = L"Hover over a setting, or click into a value, to keep its explanation here while you edit.\r\n\r\n"
                          L"Use these configs from Project Settings to decide how old chat history is compressed before it is sent back to the model.";
    const wchar_t* url = L"";
};

HelpTopic HelpTopicForControlId(int control_id) {
    switch (control_id) {
    case kConfigList:
        return {L"Compression configs",
            L"Each row is a reusable context-window configuration. Projects pick one of these configs in Project Settings.\r\n\r\n"
            L"Duplicate an existing config before experimenting with a different strategy or token budget.", L""};
    case kAddConfig:
        return {L"Add config", L"Creates a new hierarchical compression config with practical defaults for L1-L4 and L0 disabled.", L""};
    case kDeleteConfig:
        return {L"Delete config", L"Removes the selected config from the global compression library. Check Project Settings first if a project still depends on it.", L""};
    case kDuplicateConfig:
        return {L"Duplicate config", L"Copies the currently edited config so you can tune thresholds, prompts, or models without losing a known-good setup.", L""};
    case kConfigNameLabel:
    case kConfigNameEdit:
        return {L"Config name", L"A human-readable name shown in this list and in Project Settings. Use names that describe intent, such as 'Code work - balanced' or 'Long research - aggressive'.", L""};
    case kStrategyLabel:
    case kStrategyCombo:
        return {L"Strategy",
            L"Truncate Top keeps only the newest messages and drops older context.\r\n\r\n"
            L"Hierarchical Structured Compression uses L0-L4 together: artifact memory, verbatim pins, summaries, structured state, and recent raw turns.",
            L"https://en.wikipedia.org/wiki/Automatic_summarization"};
    case kTruncatePanel:
    case kTruncateKeepLabel:
    case kTruncateKeepEdit:
        return {L"Keep recent messages",
            L"For Truncate Top, this is the number of newest model-visible messages kept exactly. Set it to 0 with Frequency set to 1 for stateless requests: an active prompt can complete its tool loop, but no prior prompt or tool result is sent with the next prompt.\r\n\r\n"
            L"For Rolling Summary, 0 removes the verbatim tail only; the rolling summary still carries prior information.",
            L""};
    case kL0Panel:
    case kL0Enabled:
        return {L"L0 artifact memory",
            L"Stores durable artifacts outside the prompt, such as generated HTML, code, SVG, or documents. Later turns inject only selected memory rows instead of entire artifacts.\r\n\r\n"
            L"Use this when chats create files or long code blocks that need to survive aggressive compression.", L""};
    case kL0CaptureModelLabel:
    case kL0CaptureModelCombo:
        return {L"L0 capture model", L"Optional model used to extract artifact-memory entries from chat messages. Leave unset if you want deterministic fallback capture to do most of the work.", L""};
    case kL0CapturePromptLabel:
    case kL0CapturePromptDefault:
    case kL0CapturePromptEdit:
        return {L"L0 capture instructions", L"Prompt template used by the capture model to decide which artifacts should be written into artifact memory and how they should be described.", L"https://en.wikipedia.org/wiki/Information_extraction"};
    case kL0SelectionModelLabel:
    case kL0SelectionModelCombo:
        return {L"L0 selection model", L"Optional model used to choose which saved artifact-memory rows are relevant for the next prompt. Use a cheap, reliable model if you enable this.", L""};
    case kL0SelectionPromptLabel:
    case kL0SelectionPromptDefault:
    case kL0SelectionPromptEdit:
        return {L"L0 selection instructions", L"Prompt template used to rank stored artifact-memory rows against the current chat context. Keep it strict so unrelated artifacts are not injected.", L""};
    case kL0StorageLabel:
    case kL0StorageEdit:
    case kL0StorageBrowse:
        return {L"L0 storage folder",
            L"Folder template where artifact memory files and indexes are written. Variables such as $ProjectFolder$, $CHATID$, $CHATNAME_$, and $USERNAME$ can be used.\r\n\r\n"
            L"For project-local memory, use a path under the project folder, for example $ProjectFolder$\\.agent\\memory\\$CHATID$.", L""};
    case kL0MaxRowsLabel:
    case kL0MaxRowsEdit:
        return {L"Max injected rows", L"Caps how many artifact-memory rows may be injected into the prompt. More rows can recover more context, but they also cost tokens and can add noise. Start around 8-16.", L""};
    case kL1Panel:
    case kL1Enabled:
        return {L"L1 verbatim pinning", L"Keeps selected snippets exactly as written before summarization. This is useful for facts that should not be paraphrased, such as code, paths, URLs, versions, and explicit instructions.", L""};
    case kL1MaxPinsLabel:
    case kL1MaxPinsEdit:
        return {L"Max pins", L"Maximum number of exact snippets L1 may preserve. Higher values protect more details but consume more of the compressed context budget.", L""};
    case kL1PinCode:
        return {L"Pin code blocks", L"Preserves fenced code blocks exactly. Enable this for development chats where losing punctuation or formatting can break generated code.", L""};
    case kL1PinUrls:
        return {L"Pin URLs and paths", L"Preserves links and file paths exactly so downloads, local paths, and project references survive compression.", L""};
    case kL1PinNumbers:
        return {L"Pin numbers and versions", L"Preserves numeric values, versions, ports, thresholds, and IDs that summaries often distort.", L""};
    case kL1PinFirst:
        return {L"Pin first user message", L"Keeps the initial task brief exact. This helps the model retain the original goal after many turns.", L""};
    case kL1PinUserFlag:
        return {L"Pin [PIN] markers", L"Lets users force exact preservation by marking important text with [PIN].", L""};
    case kL1PinInstructions:
        return {L"Pin explicit instructions", L"Preserves text that looks like a requirement, rule, constraint, or instruction. Useful when later summaries should not soften user requirements.", L""};
    case kL2Panel:
    case kL2Enabled:
        return {L"L2 summary", L"Creates a compact narrative summary of older chat turns. This is the main space-saving layer for long conversations.", L"https://en.wikipedia.org/wiki/Automatic_summarization"};
    case kL2ModelLabel:
    case kL2ModelCombo:
        return {L"L2 model", L"Model used to produce the L2 summary. Prefer a model that follows formatting instructions well; it can usually be cheaper than the main chat model.", L""};
    case kL2MaxTokensLabel:
    case kL2MaxTokensEdit:
        return {L"L2 max tokens", L"Approximate output budget for the narrative summary. Too low loses nuance; too high leaves less room for the live conversation.", L""};
    case kL2TriggerLabel:
    case kL2TriggerEdit:
        return {L"L2 trigger turns", L"Minimum number of turns before L2 starts summarizing. Larger values delay compression and preserve more raw context for short chats.", L""};
    case kL2PromptLabel:
    case kL2PromptDefault:
    case kL2PromptEdit:
        return {L"L2 summary instructions", L"Prompt template for writing the narrative summary. Tune this when summaries miss requirements, decisions, open tasks, or important constraints.", L"https://en.wikipedia.org/wiki/Prompt_engineering"};
    case kL3Panel:
    case kL3Enabled:
        return {L"L3 structured state", L"Extracts durable state as structured bullets: goals, decisions, files, entities, constraints, and open tasks. This complements the more narrative L2 summary.", L"https://en.wikipedia.org/wiki/State_(computer_science)"};
    case kL3ModelLabel:
    case kL3ModelCombo:
        return {L"L3 model", L"Model used to produce structured state. Choose a model that reliably follows schemas and avoids inventing state.", L""};
    case kL3MaxTokensLabel:
    case kL3MaxTokensEdit:
        return {L"L3 max tokens", L"Output budget for structured state. Increase this for projects with many files, decisions, or requirements; decrease it if injected context becomes noisy.", L""};
    case kL3PromptLabel:
    case kL3PromptDefault:
    case kL3PromptEdit:
        return {L"L3 state instructions", L"Prompt template for extracting structured project state. Tune this if state entries are too vague, too verbose, or omit important categories.", L""};
    case kL4Panel:
    case kL4Enabled:
        return {L"L4 recency window", L"Always keeps the newest raw turns even when older context is compressed. This protects local continuity and the user's immediate intent.", L"https://en.wikipedia.org/wiki/Recency_effect"};
    case kL4MinRecentLabel:
    case kL4MinRecentEdit:
        return {L"Min recent turns", L"Minimum number of latest user/assistant turns kept verbatim after compression. Increase this for fast-moving debugging or design work.", L""};
    case kFooterPanel:
        return {L"Compression triggers", L"Controls when automatic compression runs. Manual compression can still be triggered from the main chat window if the project allows it.", L""};
    case kFrequencyLabel:
    case kFrequencyEdit:
        return {L"Frequency", L"Runs automatic compression every N prompts. Set 0 for manual-only. Combine with the context trigger to avoid unnecessary compression on short chats.", L""};
    case kContextTriggerLabel:
    case kContextTriggerEdit:
        return {L"Context trigger percent", L"Runs automatic compression when the estimated request size reaches this percent of the selected model's context window. Lower values compress earlier; higher values preserve more raw context.", L"https://en.wikipedia.org/wiki/Large_language_model#Context_window"};
    case kSaveButton:
        return {L"Save", L"Writes the edited global compression configs and closes the editor.", L""};
    case kCancelButton:
        return {L"Cancel", L"Closes the editor without saving the current edits.", L""};
    default:
        return {};
    }
}

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

int StrategyToComboIndex(ContextCompressionStrategy strategy) {
    switch (strategy) {
    case ContextCompressionStrategy::TruncateTop:
        return 0;
    case ContextCompressionStrategy::RollingSummary:
        return 1;
    case ContextCompressionStrategy::ToolTraceDistillation:
        return 2;
    case ContextCompressionStrategy::HierarchicalStructured:
        return 3;
    case ContextCompressionStrategy::None:
    default:
        return 0;
    }
}

ContextCompressionStrategy StrategyFromComboIndex(int index) {
    switch (index) {
    case 0:
        return ContextCompressionStrategy::TruncateTop;
    case 1:
        return ContextCompressionStrategy::RollingSummary;
    case 2:
        return ContextCompressionStrategy::ToolTraceDistillation;
    case 3:
        return ContextCompressionStrategy::HierarchicalStructured;
    default:
        return ContextCompressionStrategy::TruncateTop;
    }
}

std::string DefaultLayer2PromptForStrategy(ContextCompressionStrategy strategy) {
    if (strategy == ContextCompressionStrategy::RollingSummary) {
        return ContextCompressionService::DefaultRollingSummaryPromptTemplate();
    }
    if (strategy == ContextCompressionStrategy::ToolTraceDistillation) {
        return ContextCompressionService::DefaultToolTraceDistillationPromptTemplate();
    }
    return ContextCompressionService::DefaultLayer2PromptTemplate();
}

bool IsKnownLayer2DefaultPrompt(const std::string& prompt) {
    const std::string normalized = Trim(prompt);
    return normalized == Trim(ContextCompressionService::DefaultLayer2PromptTemplate()) ||
           normalized == Trim(ContextCompressionService::DefaultRollingSummaryPromptTemplate()) ||
           normalized == Trim(ContextCompressionService::DefaultToolTraceDistillationPromptTemplate());
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
    void UpdateContextHelp(int control_id, bool focused);
    void UpdateHoverContextHelp();
    void OpenCurrentHelpLink() const;
    int HelpControlIdFromWindow(HWND control) const;
    bool IsHelpControlId(int control_id) const;
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
    void PopulatePrePassCombo(const std::string& selected_id);
    std::string SelectedPrePassConfigId() const;

    LRESULT HandleScrollPanelMessage(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    ContextCompressionService* service_ = nullptr;
    AppStorage* storage_ = nullptr;
    std::function<std::vector<ProviderConfig>()> get_providers_;
    std::function<void()> on_changed_;
    std::vector<ContextCompressionConfig> configs_;
    std::vector<std::string> pre_pass_combo_ids_;
    int selected_config_index_ = -1;
    int scroll_offset_ = 0;
    int scroll_content_height_ = 0;
    int scroll_content_width_ = 0;
    int scroll_viewport_height_ = 0;

    HWND config_list_ = nullptr;
    HWND add_button_ = nullptr;
    HWND delete_button_ = nullptr;
    HWND duplicate_button_ = nullptr;
    HWND help_panel_ = nullptr;
    HWND help_title_ = nullptr;
    HWND help_text_ = nullptr;
    HWND help_link_button_ = nullptr;
    HWND config_name_label_ = nullptr;
    HWND config_name_edit_ = nullptr;
    HWND strategy_label_ = nullptr;
    HWND strategy_combo_ = nullptr;
    HWND pre_pass_label_ = nullptr;
    HWND pre_pass_combo_ = nullptr;
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
    int focused_help_control_id_ = 0;
    int hovered_help_control_id_ = 0;
    int active_help_control_id_ = 0;
    std::wstring current_help_url_;
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
    case WM_TIMER:
        if (w_param == kHelpHoverTimer) {
            self->UpdateHoverContextHelp();
            return 0;
        }
        return DefWindowProcW(hwnd, message, w_param, l_param);
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kHelpHoverTimer);
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

    help_panel_ = CreateWindowExW(0, L"BUTTON", L"Context Help", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kHelpPanel), nullptr, nullptr);
    help_title_ = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kHelpTitle), nullptr, nullptr);
    help_text_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kHelpText), nullptr, nullptr);
    help_link_button_ = CreateWindowExW(0, L"BUTTON", L"More reading", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kHelpLink), nullptr, nullptr);

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

    pre_pass_label_ = CreateWindowExW(0, L"STATIC", L"Pre-pass:", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kPrePassLabel), nullptr, nullptr);
    pre_pass_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kPrePassCombo), nullptr, nullptr);

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

    std::array<HWND, 51> controls = {
        config_list_, add_button_, delete_button_, duplicate_button_,
        config_name_label_, config_name_edit_, strategy_label_, strategy_combo_,
        pre_pass_label_, pre_pass_combo_,
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
    std::array<HWND, 4> help_controls = {
        help_panel_, help_title_, help_text_, help_link_button_
    };
    for (HWND control : help_controls) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }

    UpdateContextHelp(0, false);
    SetTimer(hwnd_, kHelpHoverTimer, 150, nullptr);

    ComboBox_AddString(strategy_combo_, L"Truncate Top (Rolling Window)");
    ComboBox_AddString(strategy_combo_, L"Rolling Summary");
    ComboBox_AddString(strategy_combo_, L"Tool Trace Distillation");
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

void CompressionManagerWindow::PopulatePrePassCombo(const std::string& selected_id) {
    ComboBox_ResetContent(pre_pass_combo_);
    pre_pass_combo_ids_.clear();

    ComboBox_AddString(pre_pass_combo_, L"(None)");
    pre_pass_combo_ids_.push_back("");
    int selected_index = 0;

    const std::string current_id =
        (selected_config_index_ >= 0 && static_cast<size_t>(selected_config_index_) < configs_.size())
            ? configs_[selected_config_index_].id
            : std::string{};

    for (const auto& config : configs_) {
        if (!current_id.empty() && config.id == current_id) {
            continue;
        }
        ComboBox_AddString(pre_pass_combo_, Utf8ToWide(config.name).c_str());
        pre_pass_combo_ids_.push_back(config.id);
        if (!selected_id.empty() && config.id == selected_id) {
            selected_index = static_cast<int>(pre_pass_combo_ids_.size()) - 1;
        }
    }

    ComboBox_SetCurSel(pre_pass_combo_, selected_index);
    SendMessageW(pre_pass_combo_, CB_SETDROPPEDWIDTH, Scale(hwnd_, 360), 0);
}

std::string CompressionManagerWindow::SelectedPrePassConfigId() const {
    const int sel = ComboBox_GetCurSel(pre_pass_combo_);
    if (sel >= 0 && static_cast<size_t>(sel) < pre_pass_combo_ids_.size()) {
        return pre_pass_combo_ids_[static_cast<size_t>(sel)];
    }
    return {};
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
    const int help_target_height = Scale(hwnd_, 250);
    const int top_area_height = Scale(hwnd_, 118);
    const int footer_height = Scale(hwnd_, 106);

    const int list_height = std::max(
        Scale(hwnd_, 170),
        height - margin * 2 - button_height - gutter * 2 - help_target_height);
    MoveWindow(config_list_, margin, margin, sidebar_width, list_height, TRUE);

    const int buttons_y = margin + list_height + gutter;
    MoveWindow(add_button_, margin, buttons_y, Scale(hwnd_, 72), button_height, TRUE);
    MoveWindow(delete_button_, margin + Scale(hwnd_, 78), buttons_y, Scale(hwnd_, 72), button_height, TRUE);
    MoveWindow(duplicate_button_, margin + Scale(hwnd_, 156), buttons_y, Scale(hwnd_, 104), button_height, TRUE);

    const int help_y = buttons_y + button_height + gutter;
    const int help_height = std::max(Scale(hwnd_, 150), height - help_y - margin);
    MoveWindow(help_panel_, margin, help_y, sidebar_width, help_height, TRUE);
    MoveWindow(help_title_, margin + gutter, help_y + Scale(hwnd_, 24), sidebar_width - gutter * 2, label_height * 2, TRUE);
    MoveWindow(help_link_button_, margin + gutter, help_y + help_height - button_height - gutter, Scale(hwnd_, 112), button_height, TRUE);
    MoveWindow(help_text_, margin + gutter, help_y + Scale(hwnd_, 62), sidebar_width - gutter * 2,
        std::max(Scale(hwnd_, 64), help_height - Scale(hwnd_, 62) - button_height - gutter * 2), TRUE);

    const int right_x = margin + sidebar_width + gutter * 2;
    const int right_width = std::max(Scale(hwnd_, 620), width - right_x - margin);

    MoveWindow(config_name_label_, right_x, margin, Scale(hwnd_, 52), label_height, TRUE);
    MoveWindow(config_name_edit_, right_x + Scale(hwnd_, 58), margin - Scale(hwnd_, 2), right_width - Scale(hwnd_, 58), edit_height, TRUE);
    MoveWindow(strategy_label_, right_x, margin + Scale(hwnd_, 36), Scale(hwnd_, 60), label_height, TRUE);
    MoveWindow(strategy_combo_, right_x + Scale(hwnd_, 68), margin + Scale(hwnd_, 32), right_width - Scale(hwnd_, 68), Scale(hwnd_, 260), TRUE);
    MoveWindow(pre_pass_label_, right_x, margin + Scale(hwnd_, 68), Scale(hwnd_, 66), label_height, TRUE);
    MoveWindow(pre_pass_combo_, right_x + Scale(hwnd_, 68), margin + Scale(hwnd_, 64), right_width - Scale(hwnd_, 68), Scale(hwnd_, 260), TRUE);

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
    const ContextCompressionStrategy strategy = StrategyFromComboIndex(ComboBox_GetCurSel(strategy_combo_));
    const bool is_truncate = strategy == ContextCompressionStrategy::TruncateTop;
    const bool is_rolling = strategy == ContextCompressionStrategy::RollingSummary;
    const bool is_tool_trace = strategy == ContextCompressionStrategy::ToolTraceDistillation;
    const bool is_hsc = strategy == ContextCompressionStrategy::HierarchicalStructured;
    const bool show_keep_recent = is_truncate || is_rolling;
    const bool show_l2 = is_hsc || is_rolling || is_tool_trace;

    auto show = [](HWND control, bool visible) {
        ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
    };

    show(truncate_panel_, show_keep_recent);
    show(truncate_keep_label_, show_keep_recent);
    show(truncate_keep_edit_, show_keep_recent);

    const std::array<HWND, 27> hsc_only_controls = {
        l0_panel_, l0_enabled_, l0_capture_model_label_, l0_capture_model_combo_, l0_capture_prompt_label_,
        l0_capture_prompt_default_button_, l0_capture_prompt_edit_, l0_selection_model_label_,
        l0_selection_model_combo_, l0_selection_prompt_label_, l0_selection_prompt_default_button_,
        l0_selection_prompt_edit_, l0_storage_label_, l0_storage_edit_, l0_storage_browse_button_,
        l0_max_rows_label_, l0_max_rows_edit_,
        l1_panel_, l1_enabled_, l1_max_pins_label_, l1_max_pins_edit_, l1_pin_code_, l1_pin_urls_,
        l1_pin_numbers_, l1_pin_first_, l1_pin_user_flag_, l1_pin_instructions_
    };
    for (HWND control : hsc_only_controls) show(control, is_hsc);

    const std::array<HWND, 8> l2_controls = {
        l2_panel_, l2_enabled_, l2_model_label_, l2_model_combo_, l2_max_tokens_label_, l2_max_tokens_edit_,
        l2_trigger_label_, l2_trigger_edit_
    };
    for (HWND control : l2_controls) show(control, show_l2);
    const std::array<HWND, 3> l2_prompt_controls = {
        l2_prompt_label_, l2_prompt_default_button_, l2_prompt_edit_
    };
    for (HWND control : l2_prompt_controls) show(control, show_l2);

    const std::array<HWND, 13> hsc_tail_controls = {
        l3_panel_, l3_enabled_, l3_model_label_, l3_model_combo_, l3_max_tokens_label_, l3_max_tokens_edit_,
        l3_prompt_label_, l3_prompt_default_button_, l3_prompt_edit_, l4_panel_,
        l4_enabled_, l4_min_recent_label_, l4_min_recent_edit_
    };
    for (HWND control : hsc_tail_controls) show(control, is_hsc);

    if (show_keep_recent) {
        const int panel_height = Scale(hwnd_, 74);
        MoveWindow(truncate_panel_, x, y, panel_width, panel_height, TRUE);
        MoveWindow(truncate_keep_label_, left, y + group_top_padding + Scale(hwnd_, 8), Scale(hwnd_, 140), label_height, TRUE);
        MoveWindow(truncate_keep_edit_, left + Scale(hwnd_, 150), y + group_top_padding + Scale(hwnd_, 4), Scale(hwnd_, 72), edit_height, TRUE);
        y += panel_height + gutter;
    }

    if (is_hsc) {
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
    }

    if (show_l2) {
        SetWindowTextW(l2_panel_,
            is_tool_trace ? L"Tool Trace Distillation" :
            (is_rolling ? L"Rolling Summary" : L"Layer 2 - Summary"));
        SetWindowTextW(l2_enabled_,
            is_tool_trace ? L"Enable tool trace distillation" :
            (is_rolling ? L"Enable rolling summary" : L"Enable L2 summary"));
        const int l2_panel_height = Scale(hwnd_, 252);
        MoveWindow(l2_panel_, x, y, panel_width, l2_panel_height, TRUE);
        MoveWindow(l2_enabled_, left, y + group_top_padding, Scale(hwnd_, 230), Scale(hwnd_, 22), TRUE);
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
    }

    if (is_hsc) {
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
    const ContextCompressionStrategy strategy = StrategyFromComboIndex(ComboBox_GetCurSel(strategy_combo_));
    const bool is_hsc = strategy == ContextCompressionStrategy::HierarchicalStructured;
    const bool has_l2 = is_hsc ||
        strategy == ContextCompressionStrategy::RollingSummary ||
        strategy == ContextCompressionStrategy::ToolTraceDistillation;
    const bool l0_enabled = is_hsc && Button_GetCheck(l0_enabled_) == BST_CHECKED;
    const bool l1_enabled = is_hsc && Button_GetCheck(l1_enabled_) == BST_CHECKED;
    const bool l2_enabled = has_l2 && Button_GetCheck(l2_enabled_) == BST_CHECKED;
    const bool l3_enabled = is_hsc && Button_GetCheck(l3_enabled_) == BST_CHECKED;
    const bool l4_enabled = is_hsc && Button_GetCheck(l4_enabled_) == BST_CHECKED;

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

bool CompressionManagerWindow::IsHelpControlId(int control_id) const {
    return control_id == kHelpPanel ||
           control_id == kHelpTitle ||
           control_id == kHelpText ||
           control_id == kHelpLink;
}

int CompressionManagerWindow::HelpControlIdFromWindow(HWND control) const {
    while (control && control != hwnd_) {
        const int control_id = GetDlgCtrlID(control);
        if (control_id == kSaveButton ||
            control_id == kCancelButton ||
            (control_id >= kConfigList && control_id <= kHelpLink)) {
            return control_id;
        }
        control = GetParent(control);
    }
    return 0;
}

void CompressionManagerWindow::UpdateContextHelp(int control_id, bool focused) {
    if (IsHelpControlId(control_id)) return;

    const HelpTopic topic = HelpTopicForControlId(control_id);
    active_help_control_id_ = control_id;
    current_help_url_ = topic.url ? topic.url : L"";

    std::wstring title = topic.title ? topic.title : L"Context window editor";
    if (focused && control_id != 0) {
        title += L" (editing)";
    }
    SetWindowTextW(help_title_, title.c_str());
    SetWindowTextW(help_text_, topic.body ? topic.body : L"");
    EnableWindow(help_link_button_, !current_help_url_.empty());
}

void CompressionManagerWindow::UpdateHoverContextHelp() {
    if (!hwnd_ || focused_help_control_id_ != 0) return;

    POINT point{};
    if (!GetCursorPos(&point)) return;
    HWND hover_window = WindowFromPoint(point);
    if (!hover_window || (hover_window != hwnd_ && !IsChild(hwnd_, hover_window))) {
        if (hovered_help_control_id_ != 0) {
            hovered_help_control_id_ = 0;
            UpdateContextHelp(0, false);
        }
        return;
    }

    int control_id = HelpControlIdFromWindow(hover_window);
    if (IsHelpControlId(control_id)) {
        control_id = active_help_control_id_;
    }
    if (control_id != hovered_help_control_id_) {
        hovered_help_control_id_ = control_id;
        UpdateContextHelp(control_id, false);
    }
}

void CompressionManagerWindow::OpenCurrentHelpLink() const {
    if (current_help_url_.empty()) return;
    ShellExecuteW(hwnd_, L"open", current_help_url_.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
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
    ComboBox_SetCurSel(strategy_combo_, StrategyToComboIndex(config.strategy));
    PopulatePrePassCombo(config.pre_pass_config_id);

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
        ? DefaultLayer2PromptForStrategy(config.strategy)
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

    config.strategy = StrategyFromComboIndex(ComboBox_GetCurSel(strategy_combo_));
    config.pre_pass_config_id = SelectedPrePassConfigId();
    if (config.pre_pass_config_id == config.id) {
        config.pre_pass_config_id.clear();
    }
    config.truncate_top_keep_messages = std::max(
        0, ParseInt(GetWindowTextString(truncate_keep_edit_)).value_or(20));

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
    if (IsKnownLayer2DefaultPrompt(config.layers.layer2.prompt_template)) {
        config.layers.layer2.prompt_template = DefaultLayer2PromptForStrategy(config.strategy);
    }
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
    const std::string deleted_id = configs_[selected_config_index_].id;
    configs_.erase(configs_.begin() + selected_config_index_);
    for (auto& config : configs_) {
        if (config.pre_pass_config_id == deleted_id) {
            config.pre_pass_config_id.clear();
        }
    }
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
    const bool focus_notification =
        notification_code == EN_SETFOCUS ||
        notification_code == CBN_SETFOCUS ||
        notification_code == LBN_SETFOCUS ||
        notification_code == BN_SETFOCUS;
    const bool kill_focus_notification =
        notification_code == EN_KILLFOCUS ||
        notification_code == CBN_KILLFOCUS ||
        notification_code == LBN_KILLFOCUS ||
        notification_code == BN_KILLFOCUS;

    if (focus_notification && !IsHelpControlId(control_id)) {
        focused_help_control_id_ = control_id;
        UpdateContextHelp(control_id, true);
    } else if (kill_focus_notification && control_id == focused_help_control_id_) {
        focused_help_control_id_ = 0;
        UpdateHoverContextHelp();
    }

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
    case kHelpLink:
        OpenCurrentHelpLink();
        break;
    case kStrategyCombo:
        if (notification_code == CBN_SELCHANGE) {
            const std::string current_l2_prompt =
                NormalizeMultilineFromEdit(GetWindowTextString(l2_prompt_edit_));
            if (IsKnownLayer2DefaultPrompt(current_l2_prompt)) {
                SetWindowTextW(l2_prompt_edit_,
                    NormalizeMultilineForEdit(DefaultLayer2PromptForStrategy(
                        StrategyFromComboIndex(ComboBox_GetCurSel(strategy_combo_)))).c_str());
            }
            UpdateContextHelp(kStrategyCombo, focused_help_control_id_ == kStrategyCombo);
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
        SetWindowTextW(l2_prompt_edit_,
            NormalizeMultilineForEdit(DefaultLayer2PromptForStrategy(
                StrategyFromComboIndex(ComboBox_GetCurSel(strategy_combo_)))).c_str());
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
