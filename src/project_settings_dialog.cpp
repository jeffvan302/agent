#include "project_settings_dialog.h"

#include "built_in_tools.h"
#include "prompt_dialog.h"
#include "rag_tool_bridge.h"
#include "remote_worker_setup_dialog.h"
#include "util.h"
#include "variable_resolver.h"

#include <commdlg.h>
#include <commctrl.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr wchar_t kProjectSettingsClassName[] = L"AgentProjectSettingsWindow";
constexpr wchar_t kProjectSettingsScrollPanelClassName[] = L"AgentProjectSettingsScrollPanel";
constexpr wchar_t kProjectSettingsScrollContentClassName[] = L"AgentProjectSettingsScrollContent";
constexpr int kScrollPanelColorIndex = COLOR_WINDOW;

constexpr wchar_t kContextPreviewClassName[] = L"AgentContextPreviewWindow";
constexpr int kContextPreviewTextBoxId = 9001;

HBRUSH SettingsScrollBrush() {
    return GetSysColorBrush(kScrollPanelColorIndex);
}

enum ControlId : int {
    // Left panel - MCP servers
    kServerList = 6401,
    kAddServer = 6402,
    kRemoveServer = 6403,
    kServerDetailsPanel = 6404,
    kServerEnabled = 6405,
    kServerNameLabel = 6406,
    kServerScopeLabel = 6407,
    kVariablesHeader = 6408,

    // Right panel top - model selection
    kModelLabel = 6415,
    kModelCombo = 6416,
    kModelTimeoutLabel = 6417,
    kModelTimeoutEdit = 6418,

    // Right panel - context window
    kContextWindowLabel = 6410,
    kContextWindowCombo = 6411,
    kAddCompressionConfig = 6412,
    kDeleteCompressionConfig = 6413,
    kEditCompressionConfig = 6414,

    // Left panel bottom - Model tools
    kModelToolsHeader = 6446,
    kModelToolsList   = 6447,
    // Browse buttons for MCP variables: IDs kBrowseVarBase..kBrowseVarBase+99
    kBrowseVarBase    = 7000,

    // Right panel middle - RAG services
    kRagServicesHeader = 6420,
    kRagServicesList = 6421,
    kRagEnabledCheck = 6422,
    kRagReadCheck = 6423,
    kRagWriteCheck = 6424,
    kRagToolCheck = 6425,
    kRagDeleteCheck = 6426,
    kRagExportCheck = 6427,
    kRagDefaultIngestCheck = 6428,
    kRagPriorityLabel = 6429,
    kRagPriorityEdit = 6430,
    kRagMaxChunksLabel = 6431,
    kRagMaxChunksEdit = 6432,
    kRagMinConfidenceLabel = 6433,
    kRagMinConfidenceEdit = 6434,
    kRagMaxConfidenceLabel = 6435,
    kRagMaxConfidenceEdit = 6436,
    kRagExportPathLabel = 6437,
    kRagExportPathEdit = 6438,
    kRagRetrievalModeLabel = 6439,
    kRagRetrievalModeCombo = 6440,
    kInstructionsLabel = 6443,
    kInstructionsEdit = 6444,
    kImportInstructions = 6445,

    // Right panel - project variables
    kProjVarsHeader     = 6450,
    kProjVarsList       = 6451,
    kProjVarsAdd        = 6452,
    kProjVarsRemove     = 6453,
    kProjVarsNameLabel  = 6454,
    kProjVarsNameEdit   = 6455,
    kProjVarsValueLabel = 6456,
    kProjVarsValueEdit  = 6457,
    kProjVarsDescriptionLabel = 6458,
    kProjVarsDescriptionEdit = 6459,
    kProjVarsInjectCheck = 6460,

    // Agentic modes
    kAgenticModeLabel      = 6462,
    kAgenticModeCombo      = 6463,
    kAgenticModesListLabel = 6464,
    kAgenticModesList      = 6465,

    kChatLoggingCheck = 6466,
    kManualContextCompressionCheck = 6467,
    kWebDebuggingCheck = 6468,
    kAutomationCheck = 6495,
    kInlineWebLinksCheck = 6476,
    kInternalToolsHeader = 6469,
    kInternalToolsList = 6470,
    kInternalToolSettingsPanel = 6471,
    kInternalPowerShellEnabled = 6472,
    kInternalPowerShellWorkingDirLabel = 6473,
    kInternalPowerShellWorkingDirEdit = 6474,
    kInternalPowerShellRiskLabel = 6475,
    kInternalArtifactMemoryEnabled = 6477,
    kInternalArtifactMemoryNoteLabel = 6478,

    // User Questionnaire built-in tool settings
    kQuestionnaireEnabledCheck = 6479,
    kQuestionnaireMaxOptionsLabel = 6481,
    kQuestionnaireMaxOptionsEdit = 6482,
    kQuestionnaireRestrictModeCheck = 6483,
    kQuestionnaireModeLabel = 6484,
    kQuestionnaireModeCombo = 6485,
    kPlannerEnabledCheck = 6486,
    kPlannerStorageFolderLabel = 6487,
    kPlannerStorageFolderEdit = 6488,
    kPlannerNoteLabel = 6489,
    kCompletionDriverEnabledCheck = 6490,
    kCompletionDriverModesLabel = 6491,
    kCompletionDriverModesList = 6492,
    kCompletionDriverNoteLabel = 6493,

    // Filesystem built-in tool settings
    kFilesystemEnabledCheck = 6500,
    kFilesystemAutoArchiveCheck = 6501,
    kFilesystemWorkingDirLabel = 6502,
    kFilesystemWorkingDirEdit = 6503,
    kFilesystemNoteLabel = 6504,

    // Right panel scrollable host
    kSettingsScrollPanel = 6480,

    // Footer
    kCheckContextButton = 6461,
    kSaveButton = IDOK,
    kCancelButton = IDCANCEL,
};

struct ServerState {
    std::string server_id;
    bool selected = false;
    std::vector<ProjectMcpVariableValue> variables;
};

struct VariableControl {
    std::string name;
    McpVariableKind kind = McpVariableKind::None;
    bool global = false;
    HWND label = nullptr;
    HWND edit = nullptr;
    HWND browse = nullptr;
};

struct RagRow {
    std::string rag_id;
    std::string rag_name;
    bool enabled = false;
    bool can_read = false;
    bool can_write = false;
    bool expose_as_tool = false;
    bool can_delete = false;
    bool can_export = false;
    std::string export_path_template;
    bool default_ingest_target = false;
    int retrieval_priority = 10;
    int max_chunks = 8;
    double default_min_confidence = 0.0;
    double default_max_confidence = 1.0;
    RagRetrievalMode retrieval_mode = RagRetrievalMode::Both;
};

struct ContextPreviewWindowState {
    std::wstring text;
    HWND edit = nullptr;
    HWND close_button = nullptr;
    HFONT font = nullptr;
    HFONT mono_font = nullptr;
};

int Scale(HWND hwnd, int value) {
    return MulDiv(value, GetDpiForWindow(hwnd), 96);
}

std::string MakeId() {
    const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::string id = "cfg_";
    for (int i = 0; i < 8; ++i) {
        id += std::string(1, chars[rand() % (sizeof(chars) - 1)]);
    }
    return id;
}

std::optional<std::filesystem::path> PickMarkdownFile(HWND owner) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = path;
    ofn.nMaxFile = static_cast<DWORD>(std::size(path));
    ofn.lpstrFilter = L"Markdown and Text Files\0*.md;*.markdown;*.txt\0Markdown Files\0*.md;*.markdown\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = L"Import Project Instructions";

    if (!GetOpenFileNameW(&ofn)) {
        return std::nullopt;
    }
    return std::filesystem::path(path);
}

std::string ReadWholeFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Could not open the selected file.");
    }
    std::string content{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xef &&
        static_cast<unsigned char>(content[1]) == 0xbb &&
        static_cast<unsigned char>(content[2]) == 0xbf) {
        content.erase(0, 3);
    }
    return content;
}

std::string YesNo(bool value) {
    return value ? "yes" : "no";
}

std::string RagRetrievalModeLabel(RagRetrievalMode mode) {
    switch (mode) {
    case RagRetrievalMode::PassiveOnly:
        return "passive_only_inactive";
    case RagRetrievalMode::ActiveToolOnly:
        return "active_tool_only";
    case RagRetrievalMode::Disabled:
        return "disabled";
    case RagRetrievalMode::Both:
    default:
        return "tool_access_passive_later";
    }
}

std::string VariableKindLabel(McpVariableKind kind) {
    switch (kind) {
    case McpVariableKind::Folder:
        return "folder";
    case McpVariableKind::File:
        return "file";
    case McpVariableKind::None:
    default:
        return "text";
    }
}

std::string SanitizeModelToolName(const std::string& name) {
    std::string result;
    result.reserve(name.size());
    for (char ch : name) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        } else {
            result.push_back('_');
        }
    }

    std::string collapsed;
    collapsed.reserve(result.size());
    bool last_was_under = false;
    for (char ch : result) {
        if (ch == '_') {
            if (!last_was_under && !collapsed.empty()) {
                collapsed.push_back(ch);
            }
            last_was_under = true;
        } else {
            collapsed.push_back(ch);
            last_was_under = false;
        }
    }
    while (!collapsed.empty() && collapsed.back() == '_') {
        collapsed.pop_back();
    }
    return collapsed;
}

const RagLibraryConfig* FindRagLibrary(
    const std::vector<RagLibraryConfig>& libraries,
    const std::string& id) {
    const auto it = std::find_if(libraries.begin(), libraries.end(),
        [&](const RagLibraryConfig& library) {
            return library.id == id;
        });
    return it == libraries.end() ? nullptr : &*it;
}

void RegisterContextPreviewClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = [](HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) -> LRESULT {
        auto* state = reinterpret_cast<ContextPreviewWindowState*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
            state = reinterpret_cast<ContextPreviewWindowState*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        }
        if (!state) return DefWindowProcW(hwnd, message, w_param, l_param);

        switch (message) {
        case WM_CREATE: {
            state->font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            state->mono_font = CreateFontW(
                -MulDiv(10, GetDpiForWindow(hwnd), 72),
                0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
            state->edit = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                    ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                0, 0, 0, 0,
                hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kContextPreviewTextBoxId)),
                nullptr,
                nullptr);
            state->close_button = CreateWindowExW(
                0,
                L"BUTTON",
                L"Close",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                0, 0, 0, 0,
                hwnd,
                reinterpret_cast<HMENU>(IDOK),
                nullptr,
                nullptr);
            SendMessageW(state->edit, WM_SETFONT,
                         reinterpret_cast<WPARAM>(state->mono_font ? state->mono_font : state->font),
                         TRUE);
            SendMessageW(state->edit, EM_SETLIMITTEXT,
                         static_cast<WPARAM>(10 * 1024 * 1024), 0);
            SetWindowTextW(state->edit, state->text.c_str());
            SendMessageW(state->close_button, WM_SETFONT,
                         reinterpret_cast<WPARAM>(state->font), TRUE);
            return 0;
        }
        case WM_SIZE: {
            const int width = LOWORD(l_param);
            const int height = HIWORD(l_param);
            const int margin = Scale(hwnd, 12);
            const int gutter = Scale(hwnd, 8);
            const int button_width = Scale(hwnd, 90);
            const int button_height = Scale(hwnd, 28);
            const int buttons_y = height - margin - button_height;
            MoveWindow(state->edit,
                       margin,
                       margin,
                       std::max(1, width - margin * 2),
                       std::max(1, buttons_y - margin - gutter),
                       TRUE);
            MoveWindow(state->close_button,
                       width - margin - button_width,
                       buttons_y,
                       button_width,
                       button_height,
                       TRUE);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(w_param) == IDOK || LOWORD(w_param) == IDCANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd, message, w_param, l_param);
    };
    wc.lpszClassName = kContextPreviewClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
    registered = true;
}

void ShowContextPreviewWindow(HWND owner, const std::string& text) {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    RegisterContextPreviewClass(instance);

    ContextPreviewWindowState state;
    state.text = Utf8ToWide(text);

    const int width = Scale(owner, 980);
    const int height = Scale(owner, 720);
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (owner) {
        RECT owner_rect{};
        if (GetWindowRect(owner, &owner_rect)) {
            const int owner_width = static_cast<int>(owner_rect.right - owner_rect.left);
            const int owner_height = static_cast<int>(owner_rect.bottom - owner_rect.top);
            x = owner_rect.left + std::max(0, (owner_width - width) / 2);
            y = owner_rect.top + std::max(0, (owner_height - height) / 2);
        }
        EnableWindow(owner, FALSE);
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        kContextPreviewClassName,
        L"Check Context Window",
        WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE | WS_THICKFRAME,
        x, y, width, height,
        owner,
        nullptr,
        instance,
        &state);

    if (!hwnd) {
        if (owner) EnableWindow(owner, TRUE);
        return;
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

    if (state.mono_font) {
        DeleteObject(state.mono_font);
        state.mono_font = nullptr;
    }
    if (owner) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
    }
}

std::wstring FormatConfidence(double value) {
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(2) << std::clamp(value, 0.0, 1.0);
    return stream.str();
}

class ProjectSettingsDialog {
public:
    static std::optional<ProjectSettingsResult> Show(HWND owner, const ProjectSettingsOptions& options) {
        HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
        RegisterWindowClass(instance);
        RegisterScrollPanelClass(instance);
        RegisterScrollContentClass(instance);

        auto* dialog = new ProjectSettingsDialog(owner, options);
        const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE | WS_THICKFRAME;
        const DWORD ex_style = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;

        if (owner) {
            EnableWindow(owner, FALSE);
        }

        dialog->hwnd_ = CreateWindowExW(
            ex_style,
            kProjectSettingsClassName,
            options.title.empty() ? L"Project Settings" : options.title.c_str(),
            style,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1200,
            800,
            owner,
            nullptr,
            instance,
            dialog);

        if (!dialog->hwnd_) {
            if (owner) {
                EnableWindow(owner, TRUE);
            }
            delete dialog;
            return std::nullopt;
        }

        ShowWindow(dialog->hwnd_, SW_SHOW);
        UpdateWindow(dialog->hwnd_);

        MSG msg{};
        while (IsWindow(dialog->hwnd_) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(dialog->hwnd_, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        if (owner) {
            EnableWindow(owner, TRUE);
            SetActiveWindow(owner);
        }

        const auto result = dialog->result_;
        // dialog is not deleted in WM_DESTROY (to avoid dangling pointer),
        // so we delete it here after capturing the result
        delete dialog;
        return result;
    }

private:
    ProjectSettingsDialog(HWND owner, ProjectSettingsOptions options)
        : owner_(owner), options_(std::move(options)) {
        states_.reserve(options_.servers.size());
        for (const auto& server : options_.servers) {
            ServerState state;
            state.server_id = server.id;
            const auto binding_it = std::find_if(options_.initial_mcp_bindings.begin(), options_.initial_mcp_bindings.end(),
                [&](const ProjectMcpServerBinding& binding) { return binding.server_id == server.id; });
            if (binding_it != options_.initial_mcp_bindings.end()) {
                state.selected = true;
                state.variables = binding_it->variables;
            }
            states_.push_back(std::move(state));
        }

        // Populate global_values_ from the initial bindings.
        // Global variable values are stored inside each server binding's variable list;
        // the dialog's global_values_ is the single source of truth shown to the user.
        // If we don't seed it here, global variables (e.g. ProjectFolder) appear blank on open.
        for (const auto& state : states_) {
            for (const auto& var_val : state.variables) {
                const bool is_global = std::find_if(
                    options_.global_variables.begin(), options_.global_variables.end(),
                    [&](const McpServerVariable& v) { return v.name == var_val.name; }
                ) != options_.global_variables.end();
                if (!is_global) continue;
                // Only insert the first occurrence (all servers should agree on the value).
                auto existing = std::find_if(global_values_.begin(), global_values_.end(),
                    [&](const ProjectMcpVariableValue& v) { return v.name == var_val.name; });
                if (existing == global_values_.end()) {
                    global_values_.push_back(var_val);
                }
            }
        }

        // RAG bindings
        for (const auto& rag : options_.available_rags) {
            RagRow row;
            row.rag_id = rag.id;
            row.rag_name = rag.name;
            row.max_chunks = rag.default_max_chunks;
            const auto binding_it = std::find_if(options_.initial_rag_bindings.begin(), options_.initial_rag_bindings.end(),
                [&](const ProjectRagBinding& b) { return b.rag_id == rag.id; });
            if (binding_it != options_.initial_rag_bindings.end()) {
                row.enabled = binding_it->enabled;
                row.can_read = binding_it->can_read;
                row.can_write = binding_it->can_write;
                row.expose_as_tool = binding_it->expose_as_tool;
                row.can_delete = binding_it->can_delete;
                row.can_export = binding_it->can_export;
                row.export_path_template = binding_it->export_path_template;
                row.default_ingest_target = binding_it->default_ingest_target;
                row.retrieval_priority = binding_it->retrieval_priority;
                row.max_chunks = binding_it->max_chunks;
                row.default_min_confidence = binding_it->default_min_confidence;
                row.default_max_confidence = binding_it->default_max_confidence;
                row.retrieval_mode = binding_it->retrieval_mode;
            }
            rag_rows_.push_back(row);
        }

        // Selected compression config (loaded from global configs via options)
        selected_compression_config_id_ = options_.selected_compression_config_id;

        // Project-level variables
        project_variables_ = options_.initial_project_variables;
    }

    static int Scale(HWND hwnd, int value) {
        return MulDiv(value, GetDpiForWindow(hwnd), 96);
    }

    static void RegisterWindowClass(HINSTANCE instance) {
        static bool registered = false;
        if (registered) return;

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = &ProjectSettingsDialog::WindowProc;
        wc.lpszClassName = kProjectSettingsClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        RegisterClassExW(&wc);
        registered = true;
    }

    static void RegisterScrollPanelClass(HINSTANCE instance) {
        static bool registered = false;
        if (registered) return;

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = &ProjectSettingsDialog::ScrollPanelProc;
        wc.lpszClassName = kProjectSettingsScrollPanelClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = SettingsScrollBrush();
        wc.style = CS_HREDRAW | CS_VREDRAW;
        RegisterClassExW(&wc);
        registered = true;
    }

    static void RegisterScrollContentClass(HINSTANCE instance) {
        static bool registered = false;
        if (registered) return;

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = &ProjectSettingsDialog::ScrollContentProc;
        wc.lpszClassName = kProjectSettingsScrollContentClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = SettingsScrollBrush();
        wc.style = CS_HREDRAW | CS_VREDRAW;
        RegisterClassExW(&wc);
        registered = true;
    }

    static LRESULT CALLBACK ScrollContentProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
            auto* self = reinterpret_cast<ProjectSettingsDialog*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            return DefWindowProcW(hwnd, message, w_param, l_param);
        }
        auto* self = reinterpret_cast<ProjectSettingsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self) return DefWindowProcW(hwnd, message, w_param, l_param);

        switch (message) {
        case WM_COMMAND:
            self->OnCommand(LOWORD(w_param), HIWORD(w_param));
            return 0;
        case WM_KEYDOWN:
            if (w_param == VK_TAB) {
                self->AdvanceFocus(GetKeyState(VK_SHIFT) >= 0);
                return 0;
            }
            break;
        default:
            break;
        }
        return DefWindowProcW(hwnd, message, w_param, l_param);
    }

    void AdvanceFocus(bool forward) {
        static const std::vector<HWND*> kTabOrder = {
            &model_combo_,
            &model_timeout_edit_,
            &context_window_combo_,
            &manual_context_compression_check_,
            &chat_logging_check_,
            &web_debugging_check_,
            &inline_web_links_check_,
            &internal_tools_list_,
            &internal_powershell_enabled_check_,
            &internal_powershell_workdir_edit_,
            &internal_artifact_memory_enabled_check_,
            &planner_enabled_check_,
            &planner_storage_folder_edit_,
            &completion_driver_enabled_check_,
            &completion_driver_modes_list_,
            &questionnaire_enabled_check_,
            &questionnaire_max_options_edit_,
            &questionnaire_restrict_mode_check_,
            &questionnaire_mode_combo_,
            &filesystem_enabled_check_,
            &filesystem_auto_archive_check_,
            &filesystem_working_dir_edit_,
            &rag_services_list_,
            &rag_enabled_check_,
            &rag_read_check_,
            &rag_write_check_,
            &rag_tool_check_,
            &rag_export_check_,
            &rag_delete_check_,
            &rag_default_ingest_check_,
            &rag_export_path_edit_,
            &rag_priority_edit_,
            &rag_max_chunks_edit_,
            &rag_min_confidence_edit_,
            &rag_max_confidence_edit_,
            &rag_retrieval_mode_combo_,
            &proj_vars_list_,
            &proj_vars_add_btn_,
            &proj_vars_remove_btn_,
            &proj_vars_name_edit_,
            &proj_vars_value_edit_,
            &proj_vars_description_edit_,
            &proj_vars_inject_check_,
            &agentic_mode_combo_,
            &agentic_modes_list_,
            &instructions_edit_,
            &import_instructions_button_,
        };
        HWND focus = GetFocus();
        for (size_t i = 0; i < kTabOrder.size(); ++i) {
            if (*kTabOrder[i] == focus) {
                const size_t cnt = kTabOrder.size();
                for (size_t j = 1; j <= cnt; ++j) {
                    const size_t idx = forward ? (i + j) % cnt : (i + cnt - j) % cnt;
                    HWND target = *kTabOrder[idx];
                    if (target && IsWindowVisible(target) && IsWindowEnabled(target)) {
                        SetFocus(target);
                        return;
                    }
                }
                return;
            }
        }
    }

    static LRESULT CALLBACK ScrollPanelProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
        auto* self = reinterpret_cast<ProjectSettingsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
            self = reinterpret_cast<ProjectSettingsDialog*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, w_param, l_param);
        return self->HandleScrollPanelMessage(hwnd, message, w_param, l_param);
    }

    static LRESULT CALLBACK InternalToolsListProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
        auto* self = reinterpret_cast<ProjectSettingsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self) {
            switch (message) {
            case WM_LBUTTONDOWN: {
                const int x = GET_X_LPARAM(l_param);
                const int y = GET_Y_LPARAM(l_param);
                const LRESULT item_info = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(x, y));
                const int index = LOWORD(item_info);
                const bool outside = HIWORD(item_info) != 0;
                if (!outside && index >= 0 && index <= 5) {
                    SetFocus(hwnd);
                    if (x <= Scale(hwnd, 42)) {
                        self->ToggleInternalToolEnabled(index);
                        return 0;
                    }
                    self->SelectInternalTool(index);
                    return 0;
                }
                break;
            }
            case WM_KEYDOWN:
                if (w_param == VK_SPACE) {
                    const int index = ListBox_GetCurSel(hwnd);
                    if (index >= 0 && index <= 5) {
                        self->ToggleInternalToolEnabled(index);
                        return 0;
                    }
                }
                break;
            default:
                break;
            }
        }
        if (self && self->internal_tools_list_prev_proc_) {
            return CallWindowProcW(self->internal_tools_list_prev_proc_, hwnd, message, w_param, l_param);
        }
        return DefWindowProcW(hwnd, message, w_param, l_param);
    }

    static LRESULT CALLBACK AgenticModesListProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
        auto* self = reinterpret_cast<ProjectSettingsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self) {
            switch (message) {
            case WM_LBUTTONDOWN: {
                const int x = GET_X_LPARAM(l_param);
                const int y = GET_Y_LPARAM(l_param);
                const LRESULT item_info = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(x, y));
                const int index = LOWORD(item_info);
                const bool outside = HIWORD(item_info) != 0;
                if (!outside && index >= 0 && index < static_cast<int>(self->agentic_mode_enabled_.size())) {
                    SetFocus(hwnd);
                    ListBox_SetCurSel(hwnd, index);
                    if (x <= Scale(hwnd, 42)) {
                        self->ToggleAgenticModeEnabled(index);
                        return 0;
                    }
                    return 0;
                }
                break;
            }
            case WM_KEYDOWN:
                if (w_param == VK_SPACE) {
                    const int index = ListBox_GetCurSel(hwnd);
                    if (index >= 0 && index < static_cast<int>(self->agentic_mode_enabled_.size())) {
                        self->ToggleAgenticModeEnabled(index);
                        return 0;
                    }
                }
                break;
            default:
                break;
            }
        }
        if (self && self->agentic_modes_list_prev_proc_) {
            return CallWindowProcW(self->agentic_modes_list_prev_proc_, hwnd, message, w_param, l_param);
        }
        return DefWindowProcW(hwnd, message, w_param, l_param);
    }

    static LRESULT CALLBACK CompletionDriverModesListProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
        auto* self = reinterpret_cast<ProjectSettingsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self) {
            switch (message) {
            case WM_LBUTTONDOWN: {
                const int x = GET_X_LPARAM(l_param);
                const int y = GET_Y_LPARAM(l_param);
                const LRESULT item_info = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(x, y));
                const int index = LOWORD(item_info);
                const bool outside = HIWORD(item_info) != 0;
                if (!outside && index >= 0 && index < static_cast<int>(self->completion_driver_mode_allowed_.size())) {
                    SetFocus(hwnd);
                    ListBox_SetCurSel(hwnd, index);
                    if (x <= Scale(hwnd, 42)) {
                        self->ToggleCompletionDriverModeAllowed(index);
                    }
                    return 0;
                }
                break;
            }
            case WM_KEYDOWN:
                if (w_param == VK_SPACE) {
                    const int index = ListBox_GetCurSel(hwnd);
                    if (index >= 0 && index < static_cast<int>(self->completion_driver_mode_allowed_.size())) {
                        self->ToggleCompletionDriverModeAllowed(index);
                        return 0;
                    }
                }
                break;
            default:
                break;
            }
        }
        if (self && self->completion_driver_modes_list_prev_proc_) {
            return CallWindowProcW(self->completion_driver_modes_list_prev_proc_, hwnd, message, w_param, l_param);
        }
        return DefWindowProcW(hwnd, message, w_param, l_param);
    }

    LRESULT HandleScrollPanelMessage(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
        switch (message) {
        case WM_ERASEBKGND: {
            RECT rect{};
            GetClientRect(hwnd, &rect);
            FillRect(reinterpret_cast<HDC>(w_param), &rect, SettingsScrollBrush());
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            FillRect(dc, &paint.rcPaint, SettingsScrollBrush());
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

    void SetScrollOffset(int new_offset) {
        if (!scroll_panel_ || !scroll_content_host_) return;
        int max_offset = std::max(0, scroll_content_height_ - scroll_viewport_height_);
        new_offset = std::clamp(new_offset, 0, max_offset);
        if (new_offset == scroll_offset_) return;
        scroll_offset_ = new_offset;
        UpdateScrollContentHostBounds();
        UpdateScrollInfo();
    }

    void UpdateScrollInfo() {
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

    void UpdateScrollContentHostBounds() {
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

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
        auto* self = reinterpret_cast<ProjectSettingsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
            self = reinterpret_cast<ProjectSettingsDialog*>(create->lpCreateParams);
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
            self->LayoutVariableControls();
            return 0;
        case WM_COMMAND:
            self->OnCommand(LOWORD(w_param), HIWORD(w_param));
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            // Don't delete self here - we access dialog->result_ after the message loop
            // The dialog will be deleted by Show() after capturing the result
            // Don't call PostQuitMessage - it would quit the entire app's message loop
            return 0;
        default:
            return DefWindowProcW(hwnd, message, w_param, l_param);
        }
    }

    void OnCreate() {
        font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        // Left panel - server list
        server_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kServerList), nullptr, nullptr);
        add_server_button_ = CreateWindowExW(0, L"BUTTON", L"+", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kAddServer), nullptr, nullptr);
        remove_server_button_ = CreateWindowExW(0, L"BUTTON", L"-", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRemoveServer), nullptr, nullptr);

        // Server details panel
        server_details_panel_ = CreateWindowExW(0, L"BUTTON", nullptr, WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kServerDetailsPanel), nullptr, nullptr);
        server_enabled_check_ = CreateWindowExW(0, L"BUTTON", L"Use this MCP server", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kServerEnabled), nullptr, nullptr);
        server_name_label_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kServerNameLabel), nullptr, nullptr);
        server_scope_label_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kServerScopeLabel), nullptr, nullptr);
        variables_header_ = CreateWindowExW(0, L"STATIC", L"Variables:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kVariablesHeader), nullptr, nullptr);

        // Left panel – model tools section
        model_tools_header_ = CreateWindowExW(0, L"STATIC", L"Model Tools:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kModelToolsHeader), nullptr, nullptr);
        model_tools_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kModelToolsList), nullptr, nullptr);

        // Right panel scroll host
        scroll_panel_ = CreateWindowExW(
            WS_EX_CONTROLPARENT | WS_EX_COMPOSITED,
            kProjectSettingsScrollPanelClassName,
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_CLIPCHILDREN,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kSettingsScrollPanel), nullptr, this);
        scroll_backdrop_ = CreateWindowExW(
            0,
            L"STATIC",
            nullptr,
            WS_CHILD | WS_VISIBLE | SS_WHITERECT,
            0, 0, 0, 0, scroll_panel_, nullptr, nullptr, nullptr);
        scroll_content_host_ = CreateWindowExW(
            WS_EX_CONTROLPARENT,
            kProjectSettingsScrollContentClassName,
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0, 0, 0, 0, scroll_panel_, nullptr, nullptr, this);

    // Model selection section
    model_label_ = CreateWindowExW(0, L"STATIC", L"AI Model:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kModelLabel), nullptr, nullptr);
    model_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kModelCombo), nullptr, nullptr);

    model_timeout_label_ = CreateWindowExW(0, L"STATIC", L"Model timeout (seconds, 0 = infinite):", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kModelTimeoutLabel), nullptr, nullptr);
    model_timeout_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kModelTimeoutEdit), nullptr, nullptr);

        // Context window section
        context_window_label_ = CreateWindowExW(0, L"STATIC", L"Context Window Compression:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kContextWindowLabel), nullptr, nullptr);
        context_window_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kContextWindowCombo), nullptr, nullptr);
        manual_context_compression_check_ = CreateWindowExW(0, L"BUTTON", L"Allow manual context window compression",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kManualContextCompressionCheck), nullptr, nullptr);

        internal_tools_header_ = CreateWindowExW(0, L"STATIC", L"Built-in Tools:", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kInternalToolsHeader), nullptr, nullptr);
        internal_tools_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kInternalToolsList), nullptr, nullptr);
        SetWindowLongPtrW(internal_tools_list_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        internal_tools_list_prev_proc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            internal_tools_list_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ProjectSettingsDialog::InternalToolsListProc)));
        internal_tool_settings_panel_ = CreateWindowExW(0, L"BUTTON", L"Tool Settings", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kInternalToolSettingsPanel), nullptr, nullptr);
        internal_powershell_enabled_check_ = CreateWindowExW(0, L"BUTTON", L"Enable PowerShell command execution",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kInternalPowerShellEnabled), nullptr, nullptr);
        internal_powershell_workdir_label_ = CreateWindowExW(0, L"STATIC", L"Default folder:", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kInternalPowerShellWorkingDirLabel), nullptr, nullptr);
        internal_powershell_workdir_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"$ProjectFolder$",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kInternalPowerShellWorkingDirEdit), nullptr, nullptr);
        internal_powershell_risk_label_ = CreateWindowExW(0, L"STATIC",
            L"Risk: this allows the model to run local PowerShell commands for this project.",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kInternalPowerShellRiskLabel), nullptr, nullptr);
        internal_artifact_memory_enabled_check_ = CreateWindowExW(0, L"BUTTON", L"Enable Artifact/Code Memory tools",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kInternalArtifactMemoryEnabled), nullptr, nullptr);
        internal_artifact_memory_note_label_ = CreateWindowExW(0, L"STATIC",
            L"Layer 0 compression forces this on for artifact restore and code memory.",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kInternalArtifactMemoryNoteLabel), nullptr, nullptr);

        planner_enabled_check_ = CreateWindowExW(0, L"BUTTON", L"Enable Planner / Task Decomposition",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kPlannerEnabledCheck), nullptr, nullptr);
        planner_storage_folder_label_ = CreateWindowExW(0, L"STATIC", L"Storage folder:", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kPlannerStorageFolderLabel), nullptr, nullptr);
        planner_storage_folder_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"$ProjectFolder$\\.agent",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kPlannerStorageFolderEdit), nullptr, nullptr);
        planner_note_label_ = CreateWindowExW(0, L"STATIC",
            L"Stores planner.json here so the model can track nested goals, subgoals, steps, statuses, completion criteria, blockers, and tool hints across the complete interaction.",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kPlannerNoteLabel), nullptr, nullptr);

        completion_driver_enabled_check_ = CreateWindowExW(0, L"BUTTON", L"Enable Completion Driver",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kCompletionDriverEnabledCheck), nullptr, nullptr);
        completion_driver_modes_label_ = CreateWindowExW(0, L"STATIC", L"Allowed agentic modes:", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kCompletionDriverModesLabel), nullptr, nullptr);
        completion_driver_modes_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kCompletionDriverModesList), nullptr, nullptr);
        SetWindowLongPtrW(completion_driver_modes_list_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        completion_driver_modes_list_prev_proc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            completion_driver_modes_list_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ProjectSettingsDialog::CompletionDriverModesListProc)));
        completion_driver_note_label_ = CreateWindowExW(0, L"STATIC",
            L"Only checked modes can use completion_driver. The host continues those modes until the tool reports completed/done.",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kCompletionDriverNoteLabel), nullptr, nullptr);

        questionnaire_enabled_check_ = CreateWindowExW(0, L"BUTTON", L"Enable User Questionnaire",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kQuestionnaireEnabledCheck), nullptr, nullptr);
        questionnaire_max_options_label_ = CreateWindowExW(0, L"STATIC", L"Max options:",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, scroll_content_host_,
            reinterpret_cast<HMENU>(kQuestionnaireMaxOptionsLabel), nullptr, nullptr);
        questionnaire_max_options_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"8",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kQuestionnaireMaxOptionsEdit), nullptr, nullptr);
        questionnaire_restrict_mode_check_ = CreateWindowExW(0, L"BUTTON", L"Only when in this agentic mode",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kQuestionnaireRestrictModeCheck), nullptr, nullptr);
        questionnaire_mode_label_ = CreateWindowExW(0, L"STATIC", L"Mode:",
            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, scroll_content_host_,
            reinterpret_cast<HMENU>(kQuestionnaireModeLabel), nullptr, nullptr);
        questionnaire_mode_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kQuestionnaireModeCombo), nullptr, nullptr);

        // Filesystem tool section controls
        filesystem_enabled_check_ = CreateWindowExW(0, L"BUTTON", L"Enable Project Filesystem tool",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kFilesystemEnabledCheck), nullptr, nullptr);
        filesystem_auto_archive_check_ = CreateWindowExW(0, L"BUTTON", L"Auto-archive file reads/writes into Artifact/Code Memory",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kFilesystemAutoArchiveCheck), nullptr, nullptr);
        filesystem_working_dir_label_ = CreateWindowExW(0, L"STATIC", L"Working directory:", WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kFilesystemWorkingDirLabel), nullptr, nullptr);
        filesystem_working_dir_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"$ProjectFolder$",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kFilesystemWorkingDirEdit), nullptr, nullptr);
        filesystem_note_label_ = CreateWindowExW(0, L"STATIC",
            L"Allows the model to read, write, edit, list, and create files under the working directory.",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kFilesystemNoteLabel), nullptr, nullptr);

        // RAG services section
        rag_services_header_ = CreateWindowExW(0, L"STATIC", L"RAG Services:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kRagServicesHeader), nullptr, nullptr);
        rag_services_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kRagServicesList), nullptr, nullptr);
        rag_enabled_check_ = CreateWindowExW(0, L"BUTTON", L"Enabled", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kRagEnabledCheck), nullptr, nullptr);
        rag_read_check_ = CreateWindowExW(0, L"BUTTON", L"Read", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kRagReadCheck), nullptr, nullptr);
        rag_write_check_ = CreateWindowExW(0, L"BUTTON", L"Write", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kRagWriteCheck), nullptr, nullptr);
        rag_tool_check_ = CreateWindowExW(0, L"BUTTON", L"Tool", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kRagToolCheck), nullptr, nullptr);
        rag_delete_check_ = CreateWindowExW(0, L"BUTTON", L"Delete", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kRagDeleteCheck), nullptr, nullptr);
        rag_export_check_ = CreateWindowExW(0, L"BUTTON", L"Write file", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kRagExportCheck), nullptr, nullptr);
        rag_default_ingest_check_ = CreateWindowExW(0, L"BUTTON", L"Default ingest", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kRagDefaultIngestCheck), nullptr, nullptr);
        rag_priority_label_ = CreateWindowExW(0, L"STATIC", L"Priority:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kRagPriorityLabel), nullptr, nullptr);
        rag_priority_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kRagPriorityEdit), nullptr, nullptr);
        rag_max_chunks_label_ = CreateWindowExW(0, L"STATIC", L"Max chunks:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kRagMaxChunksLabel), nullptr, nullptr);
        rag_max_chunks_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kRagMaxChunksEdit), nullptr, nullptr);
        rag_min_confidence_label_ = CreateWindowExW(0, L"STATIC", L"Min confidence:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kRagMinConfidenceLabel), nullptr, nullptr);
        rag_min_confidence_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kRagMinConfidenceEdit), nullptr, nullptr);
        rag_max_confidence_label_ = CreateWindowExW(0, L"STATIC", L"Max confidence:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kRagMaxConfidenceLabel), nullptr, nullptr);
        rag_max_confidence_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kRagMaxConfidenceEdit), nullptr, nullptr);
        rag_export_path_label_ = CreateWindowExW(0, L"STATIC", L"Write file folder:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kRagExportPathLabel), nullptr, nullptr);
        rag_export_path_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kRagExportPathEdit), nullptr, nullptr);
        rag_retrieval_mode_label_ = CreateWindowExW(0, L"STATIC", L"Retrieval mode:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kRagRetrievalModeLabel), nullptr, nullptr);
        rag_retrieval_mode_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kRagRetrievalModeCombo), nullptr, nullptr);
        ComboBox_AddString(rag_retrieval_mode_combo_, L"Tool access (passive later)");
        ComboBox_AddString(rag_retrieval_mode_combo_, L"Passive only (inactive)");
        ComboBox_AddString(rag_retrieval_mode_combo_, L"Active tool only");
        ComboBox_AddString(rag_retrieval_mode_combo_, L"Disabled");
        // Project variables section
        proj_vars_header_ = CreateWindowExW(0, L"STATIC", L"Project Variables:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kProjVarsHeader), nullptr, nullptr);
        proj_vars_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kProjVarsList), nullptr, nullptr);
        proj_vars_add_btn_    = CreateWindowExW(0, L"BUTTON", L"+", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kProjVarsAdd),    nullptr, nullptr);
        proj_vars_remove_btn_ = CreateWindowExW(0, L"BUTTON", L"-", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kProjVarsRemove), nullptr, nullptr);
        proj_vars_name_label_  = CreateWindowExW(0, L"STATIC", L"Name:",  WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kProjVarsNameLabel),  nullptr, nullptr);
        proj_vars_name_edit_   = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kProjVarsNameEdit),   nullptr, nullptr);
        proj_vars_value_label_ = CreateWindowExW(0, L"STATIC", L"Value:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kProjVarsValueLabel), nullptr, nullptr);
        proj_vars_value_edit_  = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kProjVarsValueEdit),  nullptr, nullptr);
        proj_vars_description_label_ = CreateWindowExW(0, L"STATIC", L"Description:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kProjVarsDescriptionLabel), nullptr, nullptr);
        proj_vars_description_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kProjVarsDescriptionEdit), nullptr, nullptr);
        proj_vars_inject_check_ = CreateWindowExW(0, L"BUTTON", L"Inject this variable into the context window", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kProjVarsInjectCheck), nullptr, nullptr);

        agentic_mode_label_ = CreateWindowExW(0, L"STATIC", L"Default Agentic Mode:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kAgenticModeLabel), nullptr, nullptr);
        agentic_mode_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kAgenticModeCombo), nullptr, nullptr);
        agentic_modes_list_label_ = CreateWindowExW(0, L"STATIC", L"Enabled Agentic Modes:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kAgenticModesListLabel), nullptr, nullptr);
        agentic_modes_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kAgenticModesList), nullptr, nullptr);
        SetWindowLongPtrW(agentic_modes_list_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        agentic_modes_list_prev_proc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            agentic_modes_list_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ProjectSettingsDialog::AgenticModesListProc)));
        chat_logging_check_ = CreateWindowExW(0, L"BUTTON", L"Enable detailed chat logging",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kChatLoggingCheck), nullptr, nullptr);
        web_debugging_check_ = CreateWindowExW(0, L"BUTTON", L"Enable Web Debugging",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kWebDebuggingCheck), nullptr, nullptr);
        inline_web_links_check_ = CreateWindowExW(0, L"BUTTON", L"Serve /data and /rag web file links inline (risky)",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kInlineWebLinksCheck), nullptr, nullptr);
        automation_check_ = CreateWindowExW(0, L"BUTTON", L"Enable automation sequence recording",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kAutomationCheck), nullptr, nullptr);

        instructions_label_ = CreateWindowExW(0, L"STATIC", L"Project Instructions:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kInstructionsLabel), nullptr, nullptr);
        import_instructions_button_ = CreateWindowExW(0, L"BUTTON", L"Import Markdown", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, scroll_content_host_, reinterpret_cast<HMENU>(kImportInstructions), nullptr, nullptr);
        instructions_edit_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            Utf8ToWide(options_.project_instructions).c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            0,
            0,
            0,
            0,
            scroll_content_host_,
            reinterpret_cast<HMENU>(kInstructionsEdit),
            nullptr,
            nullptr);

        // Footer buttons
        check_context_button_ = CreateWindowExW(0, L"BUTTON", L"Check context window", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kCheckContextButton), nullptr, nullptr);
        save_button_ = CreateWindowExW(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kSaveButton), nullptr, nullptr);
        cancel_button_ = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kCancelButton), nullptr, nullptr);

        // Apply font to all controls
        HWND all_controls[] = {
            server_list_, add_server_button_, remove_server_button_,
            server_details_panel_, server_enabled_check_, server_name_label_, server_scope_label_,
            model_tools_header_, model_tools_list_,
            variables_header_,
            model_label_, model_combo_, model_timeout_label_, model_timeout_edit_,
            context_window_label_, context_window_combo_, manual_context_compression_check_,
            internal_tools_header_, internal_tools_list_, internal_tool_settings_panel_,
            internal_powershell_enabled_check_, internal_powershell_workdir_label_,
            internal_powershell_workdir_edit_, internal_powershell_risk_label_,
            internal_artifact_memory_enabled_check_, internal_artifact_memory_note_label_,
            planner_enabled_check_, planner_storage_folder_label_, planner_storage_folder_edit_, planner_note_label_,
            completion_driver_enabled_check_, completion_driver_modes_label_, completion_driver_modes_list_, completion_driver_note_label_,
            questionnaire_enabled_check_, questionnaire_max_options_label_, questionnaire_max_options_edit_,
            questionnaire_restrict_mode_check_, questionnaire_mode_label_, questionnaire_mode_combo_,
            filesystem_enabled_check_, filesystem_auto_archive_check_, filesystem_working_dir_label_,
            filesystem_working_dir_edit_, filesystem_note_label_,
            rag_services_header_, rag_services_list_, rag_enabled_check_, rag_read_check_, rag_write_check_, rag_tool_check_,
            rag_delete_check_, rag_export_check_, rag_default_ingest_check_,
            rag_priority_label_, rag_priority_edit_, rag_max_chunks_label_, rag_max_chunks_edit_,
            rag_min_confidence_label_, rag_min_confidence_edit_, rag_max_confidence_label_, rag_max_confidence_edit_,
            rag_export_path_label_, rag_export_path_edit_,
            rag_retrieval_mode_label_, rag_retrieval_mode_combo_,
            proj_vars_header_, proj_vars_list_, proj_vars_add_btn_, proj_vars_remove_btn_,
            proj_vars_name_label_, proj_vars_name_edit_, proj_vars_value_label_, proj_vars_value_edit_,
            proj_vars_description_label_, proj_vars_description_edit_, proj_vars_inject_check_,
            agentic_mode_label_, agentic_mode_combo_, agentic_modes_list_label_, agentic_modes_list_,
            chat_logging_check_, manual_context_compression_check_, web_debugging_check_, inline_web_links_check_, automation_check_,
            instructions_label_, import_instructions_button_, instructions_edit_,
            check_context_button_, save_button_, cancel_button_
        };
        for (HWND ctrl : all_controls) {
            SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }

        // Initial layout pass so the server details panel is positioned before SelectServer
        // tries to position variable controls relative to it.
        LayoutControls(1200, 800);

        // Populate server list
        RefreshServerList();
        SelectServer(0);

        // Populate model combo
        PopulateModelCombo();

        // Populate compression config dropdown
        RefreshCompressionCombo();

        // Populate RAG services list
        RefreshRagList();
        if (!rag_rows_.empty()) {
            ListBox_SetCurSel(rag_services_list_, 0);
            OnRagSelectionChanged();
        }

        // Populate project variables list
        RefreshProjVarsList();
        EnableWindow(proj_vars_name_edit_,   FALSE);
        EnableWindow(proj_vars_value_edit_,  FALSE);
        EnableWindow(proj_vars_description_edit_, FALSE);
        EnableWindow(proj_vars_inject_check_, FALSE);
        EnableWindow(proj_vars_remove_btn_,  FALSE);

        // Populate model tools checklist
        model_tool_enabled_.clear();
        for (const auto& mt : options_.model_tools) {
            bool enabled = std::find(options_.initial_model_tool_ids.begin(),
                options_.initial_model_tool_ids.end(), mt.id) != options_.initial_model_tool_ids.end();
            model_tool_enabled_.push_back(enabled);
            std::wstring label = (enabled ? L"[✓] " : L"[ ] ") +
                Utf8ToWide(mt.name.empty() ? "(unnamed)" : mt.name);
            ListBox_AddString(model_tools_list_, label.c_str());
        }

        // Populate agentic modes
        agentic_mode_enabled_.clear();
        completion_driver_mode_allowed_.clear();
        ComboBox_AddString(agentic_mode_combo_, L"(none)");
        for (const auto& mode : options_.agentic_modes) {
            const int idx = ComboBox_AddString(agentic_mode_combo_, Utf8ToWide(mode.name).c_str());
            bool enabled = std::find(options_.enabled_agentic_mode_ids.begin(),
                options_.enabled_agentic_mode_ids.end(), mode.id) != options_.enabled_agentic_mode_ids.end();
            if (mode.id == options_.selected_agentic_mode_id) {
                enabled = true;  // default must be checked
            }
            agentic_mode_enabled_.push_back(enabled);
            completion_driver_mode_allowed_.push_back(
                std::find(options_.completion_driver_allowed_mode_ids.begin(),
                          options_.completion_driver_allowed_mode_ids.end(),
                          mode.id) != options_.completion_driver_allowed_mode_ids.end());
            std::wstring label = (enabled ? L"[✓] " : L"[ ] ") + Utf8ToWide(mode.name);
            ListBox_AddString(agentic_modes_list_, label.c_str());
            if (mode.id == options_.selected_agentic_mode_id) {
                ComboBox_SetCurSel(agentic_mode_combo_, idx);
            }
        }
        if (options_.selected_agentic_mode_id.empty() || ComboBox_GetCurSel(agentic_mode_combo_) < 0) {
            ComboBox_SetCurSel(agentic_mode_combo_, 0);
        }
        if (options_.enable_chat_logging) {
            CheckDlgButton(scroll_content_host_, kChatLoggingCheck, BST_CHECKED);
        }
        if (options_.allow_manual_context_compression) {
            CheckDlgButton(scroll_content_host_, kManualContextCompressionCheck, BST_CHECKED);
        }
        if (options_.enable_web_debugging) {
            CheckDlgButton(scroll_content_host_, kWebDebuggingCheck, BST_CHECKED);
        }
        if (options_.serve_web_links_inline) {
            CheckDlgButton(scroll_content_host_, kInlineWebLinksCheck, BST_CHECKED);
        }
        if (options_.enable_automation) {
            CheckDlgButton(scroll_content_host_, kAutomationCheck, BST_CHECKED);
        }
        internal_powershell_enabled_ = options_.built_in_powershell_enabled;
        internal_artifact_memory_enabled_ = options_.built_in_artifact_memory_enabled;
        planner_enabled_ = options_.built_in_planner_enabled;
        completion_driver_enabled_ = options_.built_in_completion_driver_enabled;
        workdir_ = Trim(options_.built_in_powershell_working_directory).empty()
            ? std::string("$ProjectFolder$")
            : options_.built_in_powershell_working_directory;
        planner_storage_folder_ = Trim(options_.built_in_planner_storage_folder).empty()
            ? std::string("$ProjectFolder$\\.agent")
            : options_.built_in_planner_storage_folder;

        questionnaire_enabled_ = options_.built_in_questionnaire_enabled;
        filesystem_enabled_ = options_.built_in_filesystem_enabled;
        filesystem_auto_archive_ = options_.built_in_filesystem_auto_archive;
        filesystem_working_directory_ = Trim(options_.built_in_filesystem_working_directory).empty()
            ? std::string("$ProjectFolder$")
            : options_.built_in_filesystem_working_directory;
        if (completion_driver_enabled_) {
            CheckDlgButton(scroll_content_host_, kCompletionDriverEnabledCheck, BST_CHECKED);
        }
        RefreshCompletionDriverModesList();
        if (questionnaire_enabled_) {
            CheckDlgButton(scroll_content_host_, kQuestionnaireEnabledCheck, BST_CHECKED);
        }
        SetWindowTextW(questionnaire_max_options_edit_, std::to_wstring(options_.questionnaire_max_options).c_str());
        if (options_.questionnaire_restrict_by_mode) {
            CheckDlgButton(scroll_content_host_, kQuestionnaireRestrictModeCheck, BST_CHECKED);
        }
        PopulateQuestionnaireModeCombo(options_.questionnaire_allowed_mode_id);
        RefreshQuestionnaireControls();
        RefreshInternalToolsList(false);

        SetWindowTextW(model_timeout_edit_, std::to_wstring(options_.model_timeout_seconds).c_str());
    }

    void LayoutControls(int width, int height) {
        const int margin = Scale(hwnd_, 12);
        const int gutter = Scale(hwnd_, 8);
        const int label_height = Scale(hwnd_, 18);
        const int button_height = Scale(hwnd_, 28);
        const int button_width = Scale(hwnd_, 80);
        const int buttons_y = height - margin - button_height;

        // Left panel - MCP servers
        const int left_width = Scale(hwnd_, 300);
        const int server_btn_width = Scale(hwnd_, 30);

        int y = margin;
        // Server list with add/remove buttons
        MoveWindow(add_server_button_, margin, y, server_btn_width, button_height, TRUE);
        MoveWindow(remove_server_button_, margin + server_btn_width + gutter, y, server_btn_width, button_height, TRUE);
        y += button_height + gutter;
        MoveWindow(server_list_, margin, y, left_width, Scale(hwnd_, 160), TRUE);

        // Server details panel - fixed height to leave room for model tools below
        y += Scale(hwnd_, 165);
        const int details_height = Scale(hwnd_, 280);
        MoveWindow(server_details_panel_, margin, y, left_width, details_height, TRUE);
        const int detail_x = margin + gutter;
        const int detail_w = left_width - gutter * 2;
        MoveWindow(server_enabled_check_, detail_x, y + Scale(hwnd_, 18), detail_w, Scale(hwnd_, 22), TRUE);
        MoveWindow(server_name_label_, detail_x, y + Scale(hwnd_, 45), detail_w, label_height, TRUE);
        MoveWindow(server_scope_label_, detail_x, y + Scale(hwnd_, 65), detail_w, label_height, TRUE);
        MoveWindow(variables_header_, detail_x, y + Scale(hwnd_, 90), detail_w, label_height, TRUE);

        // Model tools section below server details
        y += details_height + gutter;
        MoveWindow(model_tools_header_, margin, y, left_width, label_height, TRUE);
        y += label_height + gutter;
        const int mt_list_height = std::max(Scale(hwnd_, 60), buttons_y - y - gutter);
        MoveWindow(model_tools_list_, margin, y, left_width, mt_list_height, TRUE);

        // Reposition any active variable controls
        LayoutVariableControls();

        const int right_x = margin + left_width + gutter * 2;
        const int right_width = width - right_x - margin;

        // Position the scrollable right panel
        const int scroll_top = margin;
        const int scroll_bottom = buttons_y - gutter;
        scroll_viewport_height_ = std::max(0, scroll_bottom - scroll_top);
        MoveWindow(scroll_panel_, right_x, scroll_top, right_width, scroll_viewport_height_, TRUE);
        MoveWindow(scroll_backdrop_, 0, 0, right_width, scroll_viewport_height_, TRUE);
        SetWindowPos(scroll_backdrop_, HWND_BOTTOM, 0, 0, right_width, scroll_viewport_height_,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);

        // Model selection section (top of right panel)
        y = margin;
        MoveWindow(model_label_, 0, y, right_width, label_height, TRUE);
        y += label_height + gutter;
        MoveWindow(model_combo_, 0, y, right_width, Scale(hwnd_, 250), TRUE);
        y += Scale(hwnd_, 28) + gutter;
        const int model_timeout_label_w = Scale(hwnd_, 260);
        MoveWindow(model_timeout_label_, 0, y, model_timeout_label_w, Scale(hwnd_, 20), TRUE);
        MoveWindow(model_timeout_edit_, model_timeout_label_w + gutter, y, Scale(hwnd_, 64), Scale(hwnd_, 22), TRUE);
        y += Scale(hwnd_, 24) + gutter * 2;

        // Context window section
        MoveWindow(context_window_label_, 0, y, right_width, label_height, TRUE);
        y += label_height + gutter;
        MoveWindow(context_window_combo_, 0, y, right_width, Scale(hwnd_, 200), TRUE);
        y += Scale(hwnd_, 28) + gutter;
        const int context_third = (right_width - gutter * 2) / 3;
        MoveWindow(manual_context_compression_check_, 0, y, context_third, Scale(hwnd_, 20), TRUE);
        MoveWindow(chat_logging_check_, context_third + gutter, y, context_third, Scale(hwnd_, 20), TRUE);
        MoveWindow(web_debugging_check_, (context_third + gutter) * 2, y, context_third, Scale(hwnd_, 20), TRUE);

        y += Scale(hwnd_, 24);
        MoveWindow(inline_web_links_check_, 0, y, right_width, Scale(hwnd_, 20), TRUE);
        y += Scale(hwnd_, 24);
        MoveWindow(automation_check_, 0, y, right_width, Scale(hwnd_, 20), TRUE);

        y += Scale(hwnd_, 26) + gutter;
        MoveWindow(internal_tools_header_, 0, y, right_width, label_height, TRUE);
        y += label_height + gutter;
        const int internal_available_w = std::max(0, right_width - GetSystemMetrics(SM_CXVSCROLL));
        const int internal_list_w = std::max(Scale(hwnd_, 180), (internal_available_w - gutter) / 2);
        const int internal_settings_x =  internal_list_w + gutter;
        const int internal_settings_w = internal_available_w - internal_list_w - gutter;
        const int internal_h = Scale(hwnd_, 200);
        MoveWindow(internal_tools_list_, 0, y, internal_list_w, internal_h, TRUE);
        MoveWindow(internal_tool_settings_panel_, internal_settings_x, y, internal_settings_w, internal_h, TRUE);
        const int panel_pad = Scale(hwnd_, 10);
        const int tool_y = y + Scale(hwnd_, 18);
        MoveWindow(internal_powershell_enabled_check_, internal_settings_x + panel_pad, tool_y, internal_settings_w - panel_pad * 2, Scale(hwnd_, 20), TRUE);
        MoveWindow(internal_powershell_workdir_label_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 25), Scale(hwnd_, 90), label_height, TRUE);
        MoveWindow(internal_powershell_workdir_edit_, internal_settings_x + panel_pad + Scale(hwnd_, 90), tool_y + Scale(hwnd_, 22), internal_settings_w - panel_pad * 2 - Scale(hwnd_, 90), Scale(hwnd_, 22), TRUE);
        MoveWindow(internal_powershell_risk_label_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 48), internal_settings_w - panel_pad * 2, label_height, TRUE);

        MoveWindow(internal_artifact_memory_enabled_check_, internal_settings_x + panel_pad, tool_y, internal_settings_w - panel_pad * 2, Scale(hwnd_, 20), TRUE);
        MoveWindow(internal_artifact_memory_note_label_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 25), internal_settings_w - panel_pad * 2, label_height, TRUE);

        MoveWindow(planner_enabled_check_, internal_settings_x + panel_pad, tool_y, internal_settings_w - panel_pad * 2, Scale(hwnd_, 20), TRUE);
        MoveWindow(planner_storage_folder_label_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 25), Scale(hwnd_, 92), label_height, TRUE);
        MoveWindow(planner_storage_folder_edit_, internal_settings_x + panel_pad + Scale(hwnd_, 94), tool_y + Scale(hwnd_, 22), internal_settings_w - panel_pad * 2 - Scale(hwnd_, 94), Scale(hwnd_, 22), TRUE);
        MoveWindow(planner_note_label_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 50), internal_settings_w - panel_pad * 2, Scale(hwnd_, 56), TRUE);

        MoveWindow(completion_driver_enabled_check_, internal_settings_x + panel_pad, tool_y, internal_settings_w - panel_pad * 2, Scale(hwnd_, 20), TRUE);
        MoveWindow(completion_driver_modes_label_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 25), internal_settings_w - panel_pad * 2, label_height, TRUE);
        MoveWindow(completion_driver_modes_list_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 45), internal_settings_w - panel_pad * 2, Scale(hwnd_, 72), TRUE);
        MoveWindow(completion_driver_note_label_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 122), internal_settings_w - panel_pad * 2, Scale(hwnd_, 44), TRUE);

        MoveWindow(questionnaire_enabled_check_, internal_settings_x + panel_pad, tool_y, internal_settings_w - panel_pad * 2, Scale(hwnd_, 20), TRUE);
        MoveWindow(questionnaire_max_options_label_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 25), Scale(hwnd_, 80), Scale(hwnd_, 20), TRUE);
        MoveWindow(questionnaire_max_options_edit_, internal_settings_x + panel_pad + Scale(hwnd_, 82), tool_y + Scale(hwnd_, 22), Scale(hwnd_, 50), Scale(hwnd_, 22), TRUE);
        MoveWindow(questionnaire_restrict_mode_check_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 50), internal_settings_w - panel_pad * 2, Scale(hwnd_, 20), TRUE);
        const int questionnaire_mode_label_w = Scale(hwnd_, 48);
        const int questionnaire_mode_combo_w = std::max(
            Scale(hwnd_, 120),
            internal_settings_w - panel_pad * 2 - questionnaire_mode_label_w - gutter);
        MoveWindow(questionnaire_mode_label_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 75), questionnaire_mode_label_w, label_height, TRUE);
        MoveWindow(questionnaire_mode_combo_, internal_settings_x + panel_pad + questionnaire_mode_label_w + gutter, tool_y + Scale(hwnd_, 72), questionnaire_mode_combo_w, Scale(hwnd_, 180), TRUE);
        SendMessageW(questionnaire_mode_combo_, CB_SETDROPPEDWIDTH, questionnaire_mode_combo_w, 0);

        // Filesystem tool controls
        MoveWindow(filesystem_enabled_check_, internal_settings_x + panel_pad, tool_y, internal_settings_w - panel_pad * 2, Scale(hwnd_, 20), TRUE);
        MoveWindow(filesystem_auto_archive_check_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 25), internal_settings_w - panel_pad * 2, Scale(hwnd_, 20), TRUE);
        MoveWindow(filesystem_working_dir_label_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 50), Scale(hwnd_, 100), label_height, TRUE);
        MoveWindow(filesystem_working_dir_edit_, internal_settings_x + panel_pad + Scale(hwnd_, 102), tool_y + Scale(hwnd_, 48), internal_settings_w - panel_pad * 2 - Scale(hwnd_, 102), Scale(hwnd_, 22), TRUE);
        MoveWindow(filesystem_note_label_, internal_settings_x + panel_pad, tool_y + Scale(hwnd_, 74), internal_settings_w - panel_pad * 2, Scale(hwnd_, 56), TRUE);

        // RAG services section
        y += internal_h + gutter * 2;
        MoveWindow(rag_services_header_, 0, y, right_width, label_height, TRUE);
        y += label_height + gutter;
        const int rag_list_height = Scale(hwnd_, 100);
        MoveWindow(rag_services_list_, 0, y, right_width, rag_list_height, TRUE);
        y += rag_list_height + gutter;
        // RAG permission checkboxes - positioned below the list
        const int checkbox_width = Scale(hwnd_, 70);
        const int checkbox_gap = Scale(hwnd_, 15);
        MoveWindow(rag_enabled_check_, 0, y, checkbox_width, Scale(hwnd_, 20), TRUE);
        MoveWindow(rag_read_check_, checkbox_width + checkbox_gap, y, checkbox_width, Scale(hwnd_, 20), TRUE);
        MoveWindow(rag_write_check_, (checkbox_width + checkbox_gap) * 2, y, checkbox_width, Scale(hwnd_, 20), TRUE);
        MoveWindow(rag_tool_check_, (checkbox_width + checkbox_gap) * 3, y, checkbox_width, Scale(hwnd_, 20), TRUE);
        MoveWindow(rag_export_check_, (checkbox_width + checkbox_gap) * 4, y, Scale(hwnd_, 95), Scale(hwnd_, 20), TRUE);
        y += Scale(hwnd_, 24);
        MoveWindow(rag_delete_check_, 0, y, checkbox_width, Scale(hwnd_, 20), TRUE);
        MoveWindow(rag_default_ingest_check_, checkbox_width + checkbox_gap, y, Scale(hwnd_, 130), Scale(hwnd_, 20), TRUE);
        y += Scale(hwnd_, 24) + gutter;

        const int path_label_width = Scale(hwnd_, 115);
        MoveWindow(rag_export_path_label_, 0, y + Scale(hwnd_, 3), path_label_width, label_height, TRUE);
        MoveWindow(rag_export_path_edit_, path_label_width, y, right_width - path_label_width, Scale(hwnd_, 22), TRUE);
        y += Scale(hwnd_, 24) + gutter;

        const int numeric_label_width = Scale(hwnd_, 105);
        const int numeric_edit_width = Scale(hwnd_, 70);
        const int numeric_row_height = Scale(hwnd_, 24);
        const int numeric_pair_width = numeric_label_width + numeric_edit_width + gutter * 2;
        MoveWindow(rag_priority_label_, 0, y + Scale(hwnd_, 3), numeric_label_width, label_height, TRUE);
        MoveWindow(rag_priority_edit_, numeric_label_width, y, numeric_edit_width, Scale(hwnd_, 22), TRUE);
        MoveWindow(rag_max_chunks_label_, numeric_pair_width, y + Scale(hwnd_, 3), numeric_label_width, label_height, TRUE);
        MoveWindow(rag_max_chunks_edit_, numeric_pair_width + numeric_label_width, y, numeric_edit_width, Scale(hwnd_, 22), TRUE);
        y += numeric_row_height + gutter;
        MoveWindow(rag_min_confidence_label_, 0, y + Scale(hwnd_, 3), numeric_label_width, label_height, TRUE);
        MoveWindow(rag_min_confidence_edit_, numeric_label_width, y, numeric_edit_width, Scale(hwnd_, 22), TRUE);
        MoveWindow(rag_max_confidence_label_, numeric_pair_width, y + Scale(hwnd_, 3), numeric_label_width, label_height, TRUE);
        MoveWindow(rag_max_confidence_edit_, numeric_pair_width + numeric_label_width, y, numeric_edit_width, Scale(hwnd_, 22), TRUE);
        y += numeric_row_height + gutter;
        MoveWindow(rag_retrieval_mode_label_, 0, y + Scale(hwnd_, 3), numeric_label_width, label_height, TRUE);
        MoveWindow(rag_retrieval_mode_combo_, numeric_label_width, y, Scale(hwnd_, 180), Scale(hwnd_, 200), TRUE);
        y += numeric_row_height + gutter;

        // Project variables section
        {
            const int vscroll_w     = GetSystemMetrics(SM_CXVSCROLL);
            const int pv_btn_w      = Scale(hwnd_, 30);
            const int pv_list_h     = Scale(hwnd_, 80);
            const int pv_lbl_w      = Scale(hwnd_, 82);
            const int pv_edit_h     = Scale(hwnd_, 24);
            const int pv_row_h      = pv_edit_h + Scale(hwnd_, 6);
            const int inner_right_w = right_width - vscroll_w - gutter;
            const int list_w        = inner_right_w - pv_btn_w - gutter;

            MoveWindow(proj_vars_header_, 0, y, inner_right_w, label_height, TRUE);
            y += label_height + gutter;

            MoveWindow(proj_vars_list_,       0,                y,                  list_w,         pv_list_h,  TRUE);
            MoveWindow(proj_vars_add_btn_,    list_w + gutter,  y,                                                  pv_btn_w,       button_height, TRUE);
            MoveWindow(proj_vars_remove_btn_, list_w + gutter,  y + button_height + gutter, pv_btn_w, button_height, TRUE);
            y += pv_list_h + gutter;

            MoveWindow(proj_vars_name_label_,  0,            y + Scale(hwnd_, 3), pv_lbl_w,              label_height, TRUE);
            MoveWindow(proj_vars_name_edit_,   pv_lbl_w, y,                  inner_right_w - pv_lbl_w, pv_edit_h,    TRUE);
            y += pv_row_h + gutter;

            MoveWindow(proj_vars_value_label_, 0,            y + Scale(hwnd_, 3), pv_lbl_w,              label_height, TRUE);
            MoveWindow(proj_vars_value_edit_,  pv_lbl_w, y,                  inner_right_w - pv_lbl_w, pv_edit_h,    TRUE);
            y += pv_row_h + gutter;

            MoveWindow(proj_vars_description_label_, 0, y + Scale(hwnd_, 3), pv_lbl_w, label_height, TRUE);
            MoveWindow(proj_vars_description_edit_, pv_lbl_w, y, inner_right_w - pv_lbl_w, pv_edit_h, TRUE);
            y += pv_row_h + gutter;

            MoveWindow(proj_vars_inject_check_, pv_lbl_w, y, inner_right_w - pv_lbl_w, Scale(hwnd_, 20), TRUE);
            y += Scale(hwnd_, 22) + gutter;
        }

        // Agentic modes section - two panels side by side
        const int am_half = (right_width - gutter) / 2;
        const int am_top = y;
        const int am_label_h = label_height;
        const int am_control_h = Scale(hwnd_, 130);

        MoveWindow(agentic_mode_label_,       0,              am_top,              am_half, am_label_h, TRUE);
        MoveWindow(agentic_mode_combo_,        0,              am_top + am_label_h + gutter,
                                              am_half,              Scale(hwnd_, 28), TRUE);
        MoveWindow(agentic_modes_list_label_, am_half + gutter, am_top,       am_half, am_label_h, TRUE);
        MoveWindow(agentic_modes_list_,       am_half + gutter, am_top + am_label_h + gutter,
                                              am_half,              am_control_h, TRUE);
        y = am_top + am_label_h + gutter + Scale(hwnd_, 28) + gutter;

        const int import_width = Scale(hwnd_, 130);
        MoveWindow(instructions_label_, 0, y + Scale(hwnd_, 5), right_width - import_width - gutter, label_height, TRUE);
        MoveWindow(import_instructions_button_, right_width - import_width, y, import_width, button_height, TRUE);
        y += button_height + gutter;
        MoveWindow(instructions_edit_, 0, y, right_width, std::max(Scale(hwnd_, 120), scroll_viewport_height_ - y - gutter), TRUE);

        // Set scroll content dimensions
        scroll_content_width_ = right_width;
        scroll_content_height_ = std::max(0, y + Scale(hwnd_, 120) + gutter);
        UpdateScrollContentHostBounds();
        UpdateScrollInfo();

        // Footer buttons (relative to dialog)
        const int check_width = Scale(hwnd_, 160);
        MoveWindow(cancel_button_, width - margin - button_width, buttons_y, button_width, button_height, TRUE);
        MoveWindow(save_button_, width - margin - button_width * 2 - gutter, buttons_y, button_width, button_height, TRUE);
        MoveWindow(check_context_button_, width - margin - button_width * 2 - gutter * 2 - check_width, buttons_y, check_width, button_height, TRUE);
    }

    // Reposition dynamically-created variable controls to track the server details panel.
    // Must be called after LayoutControls (or after SelectServer) so the panel has been sized.
    void LayoutVariableControls() {
        if (variable_controls_.empty()) return;
        RECT panel_rc;
        GetWindowRect(server_details_panel_, &panel_rc);
        POINT pt = {panel_rc.left, panel_rc.top};
        ScreenToClient(hwnd_, &pt);
        const int base_x = pt.x + Scale(hwnd_, 6);
        int base_y = pt.y + Scale(hwnd_, 115);
        const int panel_w = (panel_rc.right - panel_rc.left) - Scale(hwnd_, 12);
        for (const auto& vc : variable_controls_) {
            const bool has_browse = (vc.browse != nullptr);
            const int edit_w = panel_w - (has_browse ? Scale(hwnd_, 32) : 0);
            if (vc.label)  MoveWindow(vc.label,  base_x, base_y, panel_w, Scale(hwnd_, 18), TRUE);
            if (vc.edit)   MoveWindow(vc.edit,   base_x, base_y + Scale(hwnd_, 20), edit_w, Scale(hwnd_, 22), TRUE);
            if (vc.browse) MoveWindow(vc.browse, base_x + edit_w + Scale(hwnd_, 2), base_y + Scale(hwnd_, 20), Scale(hwnd_, 28), Scale(hwnd_, 22), TRUE);
            base_y += Scale(hwnd_, 48);
        }
    }

    void OnCommand(int control_id, int notification_code) {
        switch (control_id) {
        case kServerList:
            if (notification_code == LBN_SELCHANGE) {
                int sel = ListBox_GetCurSel(server_list_);
                SelectServer(sel);
            }
            break;
        case kAddServer:
            AddServer();
            break;
        case kRemoveServer:
            RemoveServer();
            break;
        case kServerEnabled:
            OnServerEnabledChanged();
            break;
        case kContextWindowCombo:
            if (notification_code == CBN_SELCHANGE) {
                OnCompressionConfigChanged();
            }
            break;
        case kAgenticModeCombo:
            if (notification_code == CBN_SELCHANGE &&
                IsDlgButtonChecked(scroll_content_host_, kQuestionnaireRestrictModeCheck) == BST_CHECKED &&
                CurrentQuestionnaireModeId().empty()) {
                PopulateQuestionnaireModeCombo(FallbackQuestionnaireModeId());
                RefreshQuestionnaireControls();
            }
            break;
        case kInternalToolsList:
            if (notification_code == LBN_SELCHANGE && !toggling_internal_tool_) {
                const int sel = ListBox_GetCurSel(internal_tools_list_);
                SelectInternalTool(sel);
            }
            break;
        case kInternalPowerShellEnabled:
            if (notification_code == BN_CLICKED && !toggling_internal_tool_) {
                internal_powershell_enabled_ = (IsDlgButtonChecked(scroll_content_host_, kInternalPowerShellEnabled) == BST_CHECKED);
                RefreshInternalToolsList();
            }
            break;
        case kInternalArtifactMemoryEnabled:
            if (notification_code == BN_CLICKED && !toggling_internal_tool_) {
                if (!ArtifactMemoryForcedByLayer0()) {
                    internal_artifact_memory_enabled_ =
                        (IsDlgButtonChecked(scroll_content_host_, kInternalArtifactMemoryEnabled) == BST_CHECKED);
                }
                RefreshInternalToolsList();
            }
            break;
        case kPlannerEnabledCheck:
            if (notification_code == BN_CLICKED && !toggling_internal_tool_) {
                planner_enabled_ = (IsDlgButtonChecked(scroll_content_host_, kPlannerEnabledCheck) == BST_CHECKED);
                RefreshInternalToolsList();
            }
            break;
        case kCompletionDriverEnabledCheck:
            if (notification_code == BN_CLICKED && !toggling_internal_tool_) {
                completion_driver_enabled_ = (IsDlgButtonChecked(scroll_content_host_, kCompletionDriverEnabledCheck) == BST_CHECKED);
                RefreshInternalToolsList();
            }
            break;
        case kFilesystemEnabledCheck:
            if (notification_code == BN_CLICKED && !toggling_internal_tool_) {
                filesystem_enabled_ = (IsDlgButtonChecked(scroll_content_host_, kFilesystemEnabledCheck) == BST_CHECKED);
                RefreshInternalToolsList();
            }
            break;
        case kCompletionDriverModesList:
            break;
        case kQuestionnaireEnabledCheck:
            if (notification_code == BN_CLICKED) {
                questionnaire_enabled_ = (IsDlgButtonChecked(scroll_content_host_, kQuestionnaireEnabledCheck) == BST_CHECKED);
                RefreshInternalToolsList();
            }
            break;
        case kQuestionnaireRestrictModeCheck:
            if (notification_code == BN_CLICKED) {
                if (IsDlgButtonChecked(scroll_content_host_, kQuestionnaireRestrictModeCheck) == BST_CHECKED) {
                    PopulateQuestionnaireModeCombo(FallbackQuestionnaireModeId());
                }
                RefreshQuestionnaireControls();
            }
            break;
        case kQuestionnaireModeCombo:
            break;
        case kQuestionnaireMaxOptionsEdit:
            break; // ignore EN_CHANGE
        case kRagServicesList:
            if (notification_code == LBN_SELCHANGE) {
                OnRagSelectionChanged();
            }
            break;
        case kRagEnabledCheck:
            OnRagEnabledChanged();
            break;
        case kRagReadCheck:
            OnRagReadChanged();
            break;
        case kRagWriteCheck:
            OnRagWriteChanged();
            break;
        case kRagToolCheck:
            OnRagToolChanged();
            break;
        case kRagDeleteCheck:
        case kRagExportCheck:
        case kRagDefaultIngestCheck:
            OnRagBindingControlsChanged();
            break;
        case kRagRetrievalModeCombo:
            if (notification_code == CBN_SELCHANGE) {
                OnRagBindingControlsChanged();
            }
            break;
        case kModelToolsList:
            if (notification_code == LBN_SELCHANGE && !toggling_model_tool_) {
                // Toggle enabled state for the selected model tool.
                const int sel = ListBox_GetCurSel(model_tools_list_);
                if (sel >= 0 && sel < static_cast<int>(model_tool_enabled_.size())) {
                    toggling_model_tool_ = true;
                    model_tool_enabled_[sel] = !model_tool_enabled_[sel];
                    // Update label prefix: delete + re-insert to update the string in place.
                    const auto& mt = options_.model_tools[sel];
                    std::wstring label = (model_tool_enabled_[sel] ? L"[✓] " : L"[ ] ") +
                        Utf8ToWide(mt.name.empty() ? "(unnamed)" : mt.name);
                    ListBox_DeleteString(model_tools_list_, sel);
                    ListBox_InsertString(model_tools_list_, sel, label.c_str());
                    ListBox_SetCurSel(model_tools_list_, sel);
                    toggling_model_tool_ = false;
                }
            }
            break;
        case kProjVarsList:
            if (notification_code == LBN_SELCHANGE) {
                OnProjVarsSelChange();
            }
            break;
        case kProjVarsAdd:
            OnProjVarsAdd();
            break;
        case kProjVarsRemove:
            OnProjVarsRemove();
            break;
        case kProjVarsNameEdit:
            if (notification_code == EN_CHANGE && !updating_proj_var_) {
                OnProjVarEditChanged();
            }
            break;
        case kProjVarsValueEdit:
            if (notification_code == EN_CHANGE && !updating_proj_var_) {
                OnProjVarEditChanged();
            }
            break;
        case kProjVarsDescriptionEdit:
            if (notification_code == EN_CHANGE && !updating_proj_var_) {
                OnProjVarEditChanged();
            }
            break;
        case kProjVarsInjectCheck:
            if (notification_code == BN_CLICKED && !updating_proj_var_) {
                OnProjVarEditChanged();
            }
            break;
        case kAgenticModesList:
            // Selection is handled by the listbox subclass. Only the prefix/Space toggles.
            break;
        case kImportInstructions:
            ImportInstructions();
            break;
        case kCheckContextButton:
            ShowContextPreview();
            break;
        case kSaveButton:
            SaveAndClose();
            break;
        case kCancelButton:
            DestroyWindow(hwnd_);
            break;
        default:
            // Handle browse buttons for MCP variable edits (IDs kBrowseVarBase..kBrowseVarBase+99)
            if (control_id >= kBrowseVarBase && control_id < kBrowseVarBase + 100) {
                OnBrowseVariable(control_id - kBrowseVarBase);
            }
            break;
        }
    }

    void RefreshServerList() {
        ListBox_ResetContent(server_list_);
        for (size_t i = 0; i < options_.servers.size(); ++i) {
            const auto& server = options_.servers[i];
            std::wstring name = Utf8ToWide(server.name);
            if (states_[i].selected) {
                name += L" [enabled]";
            }
            ListBox_AddString(server_list_, name.c_str());
        }
        if (!states_.empty()) {
            ListBox_SetCurSel(server_list_, 0);
        }
    }

    void SelectServer(int index) {
        // Save edits from the currently displayed server before switching.
        SaveCurrentVariableValues();

        selected_server_index_ = index;
        bool has_selection = (index >= 0 && index < static_cast<int>(states_.size()));

        ShowWindow(server_details_panel_, has_selection ? SW_SHOW : SW_HIDE);
        ShowWindow(server_enabled_check_, has_selection ? SW_SHOW : SW_HIDE);
        ShowWindow(server_name_label_, has_selection ? SW_SHOW : SW_HIDE);
        ShowWindow(server_scope_label_, has_selection ? SW_SHOW : SW_HIDE);
        ShowWindow(variables_header_, has_selection ? SW_SHOW : SW_HIDE);

        // Destroy existing dynamic variable controls.
        for (auto& vc : variable_controls_) {
            if (vc.label)  DestroyWindow(vc.label);
            if (vc.edit)   DestroyWindow(vc.edit);
            if (vc.browse) DestroyWindow(vc.browse);
        }
        variable_controls_.clear();

        if (!has_selection) {
            return;
        }

        const auto& server = options_.servers[index];
        const auto& state = states_[index];

        Button_SetCheck(server_enabled_check_, state.selected ? BST_CHECKED : BST_UNCHECKED);
        SetWindowTextW(server_name_label_, Utf8ToWide(server.name).c_str());
        std::wstring scope = (server.scope == McpServerScope::Shared) ? L"Scope: Shared process" : L"Scope: Per-project process";
        SetWindowTextW(server_scope_label_, scope.c_str());

        // Create variable controls.  Positions are set by LayoutVariableControls(); use
        // placeholder positions (0,0) here and let the layout pass fix them up.
        int var_index = 0;
        for (const auto& variable : server.variables) {
            bool is_global = std::find_if(options_.global_variables.begin(), options_.global_variables.end(),
                [&](const McpServerVariable& v) { return v.name == variable.name; }) != options_.global_variables.end();

            HWND lbl = CreateWindowExW(0, L"STATIC", Utf8ToWide(variable.name).c_str(),
                WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hwnd_, nullptr, nullptr, nullptr);
            SendMessageW(lbl, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);

            HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, hwnd_, nullptr, nullptr, nullptr);
            SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);

            // Load current value
            std::string current_value;
            if (is_global) {
                auto it = std::find_if(global_values_.begin(), global_values_.end(),
                    [&](const ProjectMcpVariableValue& v) { return v.name == variable.name; });
                if (it != global_values_.end()) current_value = it->value;
            } else {
                auto it = std::find_if(state.variables.begin(), state.variables.end(),
                    [&](const ProjectMcpVariableValue& v) { return v.name == variable.name; });
                if (it != state.variables.end()) current_value = it->value;
            }
            SetWindowTextW(edit, Utf8ToWide(current_value).c_str());

            // Browse button: assign a WM_COMMAND-routable ID (kBrowseVarBase + varIndex)
            HWND browse = nullptr;
            if (variable.kind != McpVariableKind::None) {
                const int btn_id = kBrowseVarBase + var_index;
                browse = CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(btn_id)), nullptr, nullptr);
                SendMessageW(browse, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
            }

            variable_controls_.push_back({variable.name, variable.kind, is_global, lbl, edit, browse});
            ++var_index;
        }

        // Position the new controls to match the current panel location.
        LayoutVariableControls();
    }

    void OnServerEnabledChanged() {
        if (selected_server_index_ < 0) return;
        bool enabled = (Button_GetCheck(server_enabled_check_) == BST_CHECKED);
        states_[selected_server_index_].selected = enabled;
        RefreshServerList();
        ListBox_SetCurSel(server_list_, selected_server_index_);
    }

    // Read the current variable edit controls back into states_[selected_server_index_].variables
    // so the values survive a server switch or a Save.
    void SaveCurrentVariableValues() {
        if (selected_server_index_ < 0 || selected_server_index_ >= static_cast<int>(states_.size())) return;
        auto& state = states_[selected_server_index_];
        for (const auto& vc : variable_controls_) {
            const std::string value = WideToUtf8(GetWindowTextString(vc.edit));
            if (vc.global) {
                // Also persist into global_values_ so other servers sharing the variable see the edit.
                auto it = std::find_if(global_values_.begin(), global_values_.end(),
                    [&](const ProjectMcpVariableValue& v) { return v.name == vc.name; });
                if (it != global_values_.end()) {
                    it->value = value;
                } else {
                    global_values_.push_back({vc.name, value});
                }
            } else {
                auto it = std::find_if(state.variables.begin(), state.variables.end(),
                    [&](const ProjectMcpVariableValue& v) { return v.name == vc.name; });
                if (it != state.variables.end()) {
                    it->value = value;
                } else {
                    state.variables.push_back({vc.name, value});
                }
            }
        }
    }

    // Called when a browse ("...") button for a variable is clicked.
    void OnBrowseVariable(int var_index) {
        if (var_index < 0 || var_index >= static_cast<int>(variable_controls_.size())) return;
        auto& vc = variable_controls_[var_index];

        if (vc.kind == McpVariableKind::Folder) {
            BROWSEINFOW bi{};
            bi.hwndOwner = hwnd_;
            bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            bi.lpszTitle = L"Select Folder";
            PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t path[MAX_PATH]{};
                if (SHGetPathFromIDListW(pidl, path)) {
                    SetWindowTextW(vc.edit, path);
                }
                CoTaskMemFree(pidl);
            }
        } else if (vc.kind == McpVariableKind::File) {
            wchar_t path[MAX_PATH]{};
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hwnd_;
            ofn.lpstrFile   = path;
            ofn.nMaxFile    = static_cast<DWORD>(std::size(path));
            ofn.lpstrFilter = L"All Files\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
            if (GetOpenFileNameW(&ofn)) {
                SetWindowTextW(vc.edit, path);
            }
        }
    }

    void AddServer() {
        // Not implemented for now - servers are managed via MCP Servers button
    }

    void RemoveServer() {
        // Not implemented for now
    }

    void PopulateModelCombo() {
        ComboBox_ResetContent(model_combo_);
        model_entries_.clear();

        // First entry: "(No preference)"
        ComboBox_AddString(model_combo_, L"(No preference — use first available)");
        model_entries_.push_back({"", ""});

        int preferred = 0;  // default to "No preference"
        for (const auto& provider : options_.providers) {
            for (const auto& model : provider.models) {
                ModelEntry entry;
                entry.provider_id = provider.id;
                entry.model_id = model.id;

                std::wstring label = Utf8ToWide(provider.name + " / " + model.display_name);
                if (model.context_window > 0) {
                    label += L"  " + std::to_wstring(model.context_window) + L" ctx";
                }
                if (model.supports_streaming) label += L"  [stream]";
                if (model.supports_tools)     label += L" [tools]";
                if (model.supports_vision)    label += L" [vision]";
                if (model.supports_embedding) label += L" [embed]";
                if (model.supports_thinking)  label += L" [think]";

                if (entry.provider_id == options_.preferred_provider_id &&
                    entry.model_id == options_.preferred_model_id) {
                    preferred = static_cast<int>(model_entries_.size());
                }
                model_entries_.push_back(entry);
                ComboBox_AddString(model_combo_, label.c_str());
            }
        }
        ComboBox_SetCurSel(model_combo_, preferred);
        EnableWindow(model_combo_, model_entries_.size() > 1);
    }

    void RefreshCompressionCombo() {
        ComboBox_ResetContent(context_window_combo_);
        ComboBox_AddString(context_window_combo_, L"None");
        for (const auto& cfg : options_.compression_configs) {
            ComboBox_AddString(context_window_combo_, Utf8ToWide(cfg.name).c_str());
        }
        // Select the currently selected config
        int sel_idx = selected_compression_config_id_.empty() ? 0 : -1;
        for (int i = 0; i < static_cast<int>(options_.compression_configs.size()); ++i) {
            if (options_.compression_configs[i].id == selected_compression_config_id_) {
                sel_idx = i + 1;
                break;
            }
        }
        ComboBox_SetCurSel(context_window_combo_, sel_idx >= 0 ? sel_idx : 0);
        if (sel_idx <= 0) {
            selected_compression_config_id_.clear();
        }
    }

    bool ArtifactMemoryForcedByLayer0() const {
        if (selected_compression_config_id_.empty()) return false;
        const auto it = std::find_if(options_.compression_configs.begin(), options_.compression_configs.end(),
            [&](const ContextCompressionConfig& cfg) { return cfg.id == selected_compression_config_id_; });
        return it != options_.compression_configs.end() &&
            it->strategy == ContextCompressionStrategy::HierarchicalStructured &&
            it->layers.layer0.enabled;
    }

    bool ArtifactMemoryEffectiveEnabled() const {
        return internal_artifact_memory_enabled_ || ArtifactMemoryForcedByLayer0();
    }

    std::string CurrentDefaultAgenticModeId() const {
        if (!agentic_mode_combo_) return {};
        const int sel = ComboBox_GetCurSel(agentic_mode_combo_);
        if (sel > 0 && static_cast<size_t>(sel - 1) < options_.agentic_modes.size()) {
            return options_.agentic_modes[static_cast<size_t>(sel - 1)].id;
        }
        return {};
    }

    std::string FallbackQuestionnaireModeId() const {
        std::string mode_id = CurrentDefaultAgenticModeId();
        if (!mode_id.empty()) return mode_id;
        if (!options_.agentic_modes.empty()) return options_.agentic_modes.front().id;
        return {};
    }

    void RefreshQuestionnaireControls() {
        questionnaire_enabled_ = (IsDlgButtonChecked(scroll_content_host_, kQuestionnaireEnabledCheck) == BST_CHECKED);
        const bool restrict_mode = (IsDlgButtonChecked(scroll_content_host_, kQuestionnaireRestrictModeCheck) == BST_CHECKED);
        if (questionnaire_enabled_ && restrict_mode && CurrentQuestionnaireModeId().empty()) {
            PopulateQuestionnaireModeCombo(FallbackQuestionnaireModeId());
        }
        const bool can_choose_mode = questionnaire_enabled_ && restrict_mode && !options_.agentic_modes.empty();
        EnableWindow(questionnaire_max_options_label_, questionnaire_enabled_ ? TRUE : FALSE);
        EnableWindow(questionnaire_max_options_edit_, questionnaire_enabled_ ? TRUE : FALSE);
        EnableWindow(questionnaire_restrict_mode_check_, questionnaire_enabled_ ? TRUE : FALSE);
        EnableWindow(questionnaire_mode_label_, can_choose_mode ? TRUE : FALSE);
        EnableWindow(questionnaire_mode_combo_, can_choose_mode ? TRUE : FALSE);
    }

    std::string CurrentQuestionnaireModeId() const {
        if (!questionnaire_mode_combo_) return {};
        const int sel = ComboBox_GetCurSel(questionnaire_mode_combo_);
        if (sel > 0 && static_cast<size_t>(sel) <= options_.agentic_modes.size()) {
            return options_.agentic_modes[static_cast<size_t>(sel - 1)].id;
        }
        return {};
    }

    void PopulateQuestionnaireModeCombo(const std::string& preferred_mode_id = {}) {
        if (!questionnaire_mode_combo_) return;
        std::string selected_id = !preferred_mode_id.empty()
            ? preferred_mode_id
            : CurrentQuestionnaireModeId();
        const bool restrict_mode = (IsDlgButtonChecked(scroll_content_host_, kQuestionnaireRestrictModeCheck) == BST_CHECKED);
        if (selected_id.empty() && restrict_mode) {
            selected_id = FallbackQuestionnaireModeId();
        }
        ComboBox_ResetContent(questionnaire_mode_combo_);
        ComboBox_AddString(questionnaire_mode_combo_, L"(none)");
        int selected_index = 0;
        for (const auto& mode : options_.agentic_modes) {
            const int idx = ComboBox_AddString(questionnaire_mode_combo_, Utf8ToWide(mode.name).c_str());
            if (mode.id == selected_id) {
                selected_index = idx;
            }
        }
        if (selected_index == 0 && restrict_mode && !options_.agentic_modes.empty()) {
            selected_index = 1;
        }
        ComboBox_SetCurSel(questionnaire_mode_combo_, selected_index);
    }

    void RedrawInternalToolSettingsPanel() {
        if (!scroll_content_host_) return;
        RedrawWindow(scroll_content_host_, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }

    void SaveCurrentInternalToolSettings() {
        if (selected_internal_tool_index_ == 0 && internal_powershell_workdir_edit_) {
            workdir_ = Trim(WideToUtf8(GetWindowTextString(internal_powershell_workdir_edit_)));
            if (workdir_.empty()) {
                workdir_ = "$ProjectFolder$";
            }
        }
        if (selected_internal_tool_index_ == 0 && internal_powershell_enabled_check_) {
            internal_powershell_enabled_ =
                (IsDlgButtonChecked(scroll_content_host_, kInternalPowerShellEnabled) == BST_CHECKED);
        }
        if (selected_internal_tool_index_ == 1 && internal_artifact_memory_enabled_check_ && !ArtifactMemoryForcedByLayer0()) {
            internal_artifact_memory_enabled_ =
                (IsDlgButtonChecked(scroll_content_host_, kInternalArtifactMemoryEnabled) == BST_CHECKED);
        }
        if (selected_internal_tool_index_ == 2 && planner_enabled_check_) {
            planner_enabled_ =
                (IsDlgButtonChecked(scroll_content_host_, kPlannerEnabledCheck) == BST_CHECKED);
        }
        if (selected_internal_tool_index_ == 2 && planner_storage_folder_edit_) {
            planner_storage_folder_ = Trim(WideToUtf8(GetWindowTextString(planner_storage_folder_edit_)));
            if (planner_storage_folder_.empty()) {
                planner_storage_folder_ = "$ProjectFolder$\\.agent";
            }
        }
        if (selected_internal_tool_index_ == 4 && completion_driver_enabled_check_) {
            completion_driver_enabled_ =
                (IsDlgButtonChecked(scroll_content_host_, kCompletionDriverEnabledCheck) == BST_CHECKED);
        }
        if (selected_internal_tool_index_ == 3 && questionnaire_enabled_check_) {
            questionnaire_enabled_ =
                (IsDlgButtonChecked(scroll_content_host_, kQuestionnaireEnabledCheck) == BST_CHECKED);
        }
    }

    void ToggleInternalToolEnabled(int index) {
        if (index < 0 || index > 4) return;
        SaveCurrentInternalToolSettings();
        selected_internal_tool_index_ = index;
        if (index == 0) {
            internal_powershell_enabled_ = !internal_powershell_enabled_;
        } else if (index == 1) {
            if (!ArtifactMemoryForcedByLayer0()) {
                internal_artifact_memory_enabled_ = !internal_artifact_memory_enabled_;
            }
        } else if (index == 2) {
            planner_enabled_ = !planner_enabled_;
        } else if (index == 3) {
            questionnaire_enabled_ = !questionnaire_enabled_;
        } else if (index == 4) {
            completion_driver_enabled_ = !completion_driver_enabled_;
        } else if (index == 5) {
            filesystem_enabled_ = !filesystem_enabled_;
        }
        RefreshInternalToolsList(false);
    }

    void SelectInternalTool(int index) {
        if (index < 0 || index > 5) return;
        SaveCurrentInternalToolSettings();
        selected_internal_tool_index_ = index;
        ListBox_SetCurSel(internal_tools_list_, index);
        ShowInternalToolSettings(index);
    }

    void ToggleAgenticModeEnabled(int index) {
        if (index < 0 || index >= static_cast<int>(agentic_mode_enabled_.size()) ||
            index >= static_cast<int>(options_.agentic_modes.size())) {
            return;
        }

        toggling_agentic_mode_ = true;
        agentic_mode_enabled_[index] = !agentic_mode_enabled_[index];
        const auto& mode = options_.agentic_modes[index];
        std::wstring label = (agentic_mode_enabled_[index] ? L"[✓] " : L"[ ] ") +
            Utf8ToWide(mode.name.empty() ? "(unnamed)" : mode.name);
        ListBox_DeleteString(agentic_modes_list_, index);
        ListBox_InsertString(agentic_modes_list_, index, label.c_str());
        ListBox_SetCurSel(agentic_modes_list_, index);
        toggling_agentic_mode_ = false;
    }

    void ToggleCompletionDriverModeAllowed(int index) {
        if (index < 0 || index >= static_cast<int>(completion_driver_mode_allowed_.size()) ||
            index >= static_cast<int>(options_.agentic_modes.size())) {
            return;
        }
        completion_driver_mode_allowed_[index] = !completion_driver_mode_allowed_[index];
        RefreshCompletionDriverModesList(index);
    }

    void RefreshCompletionDriverModesList(int select_index = -1) {
        if (!completion_driver_modes_list_) return;
        const int current = select_index >= 0 ? select_index : ListBox_GetCurSel(completion_driver_modes_list_);
        ListBox_ResetContent(completion_driver_modes_list_);
        for (size_t i = 0; i < options_.agentic_modes.size(); ++i) {
            const bool allowed = i < completion_driver_mode_allowed_.size() && completion_driver_mode_allowed_[i];
            std::wstring label = (allowed ? L"[✓] " : L"[ ] ") +
                Utf8ToWide(options_.agentic_modes[i].name.empty() ? "(unnamed)" : options_.agentic_modes[i].name);
            ListBox_AddString(completion_driver_modes_list_, label.c_str());
        }
        if (!options_.agentic_modes.empty()) {
            const int bounded = std::clamp(current, 0, static_cast<int>(options_.agentic_modes.size()) - 1);
            ListBox_SetCurSel(completion_driver_modes_list_, bounded);
        }
    }

    bool InternalToolListChecked(int index, bool fallback) const {
        if (!internal_tools_list_ || index < 0) return fallback;
        const LRESULT len = SendMessageW(internal_tools_list_, LB_GETTEXTLEN, index, 0);
        if (len == LB_ERR || len < 3) return fallback;
        std::vector<wchar_t> text(static_cast<size_t>(len) + 1, L'\0');
        if (SendMessageW(internal_tools_list_, LB_GETTEXT, index, reinterpret_cast<LPARAM>(text.data())) == LB_ERR) {
            return fallback;
        }
        return text[0] == L'[' && text[1] != L' ' && text[2] == L']';
    }

    void RefreshInternalToolsList(bool save_current = true) {
        if (!internal_tools_list_) return;
        if (save_current) {
            SaveCurrentInternalToolSettings();
        }
        toggling_internal_tool_ = true;
        ListBox_ResetContent(internal_tools_list_);

        ListBox_AddString(internal_tools_list_,
            (std::wstring(internal_powershell_enabled_ ? L"[✓] " : L"[ ] ") + L"PowerShell command execution").c_str());

        const bool memory_forced = ArtifactMemoryForcedByLayer0();
        const bool memory_enabled = ArtifactMemoryEffectiveEnabled();
        std::wstring memory_label = (memory_enabled ? L"[✓] " : L"[ ] ");
        memory_label += L"Artifact/Code Memory";
        if (memory_forced) {
            memory_label += L" (forced by L0)";
        }
        ListBox_AddString(internal_tools_list_, memory_label.c_str());

        ListBox_AddString(internal_tools_list_,
            (std::wstring(planner_enabled_ ? L"[✓] " : L"[ ] ") + L"Planner / Task Decomposition").c_str());

        ListBox_AddString(internal_tools_list_,
            (std::wstring(questionnaire_enabled_ ? L"[✓] " : L"[ ] ") + L"User Questionnaire").c_str());

        ListBox_AddString(internal_tools_list_,
            (std::wstring(completion_driver_enabled_ ? L"[✓] " : L"[ ] ") + L"Completion Driver").c_str());

        ListBox_AddString(internal_tools_list_,
            (std::wstring(filesystem_enabled_ ? L"[✓] " : L"[ ] ") + L"Project Filesystem").c_str());

        int sel = selected_internal_tool_index_;
        if (sel < 0 || sel > 5) sel = 0;
        ListBox_SetCurSel(internal_tools_list_, sel);
        toggling_internal_tool_ = false;
        ShowInternalToolSettings(sel);
    }

    void ShowInternalToolSettings(int index) {
        // Hide all tool-specific controls first
        ShowWindow(internal_powershell_enabled_check_, SW_HIDE);
        ShowWindow(internal_powershell_workdir_label_, SW_HIDE);
        ShowWindow(internal_powershell_workdir_edit_, SW_HIDE);
        ShowWindow(internal_powershell_risk_label_, SW_HIDE);
        ShowWindow(internal_artifact_memory_enabled_check_, SW_HIDE);
        ShowWindow(internal_artifact_memory_note_label_, SW_HIDE);
        ShowWindow(planner_enabled_check_, SW_HIDE);
        ShowWindow(planner_storage_folder_label_, SW_HIDE);
        ShowWindow(planner_storage_folder_edit_, SW_HIDE);
        ShowWindow(planner_note_label_, SW_HIDE);
        ShowWindow(completion_driver_enabled_check_, SW_HIDE);
        ShowWindow(completion_driver_modes_label_, SW_HIDE);
        ShowWindow(completion_driver_modes_list_, SW_HIDE);
        ShowWindow(completion_driver_note_label_, SW_HIDE);
        ShowWindow(filesystem_enabled_check_, SW_HIDE);
        ShowWindow(filesystem_auto_archive_check_, SW_HIDE);
        ShowWindow(filesystem_working_dir_label_, SW_HIDE);
        ShowWindow(filesystem_working_dir_edit_, SW_HIDE);
        ShowWindow(filesystem_note_label_, SW_HIDE);
        ShowWindow(questionnaire_enabled_check_, SW_HIDE);
        ShowWindow(questionnaire_max_options_label_, SW_HIDE);
        ShowWindow(questionnaire_max_options_edit_, SW_HIDE);
        ShowWindow(questionnaire_restrict_mode_check_, SW_HIDE);
        ShowWindow(questionnaire_mode_label_, SW_HIDE);
        ShowWindow(questionnaire_mode_combo_, SW_HIDE);

        bool panel_has_content = false;
        if (index == 0) {
            panel_has_content = true;
            ShowWindow(internal_powershell_enabled_check_, SW_SHOW);
            ShowWindow(internal_powershell_workdir_label_, SW_SHOW);
            ShowWindow(internal_powershell_workdir_edit_, SW_SHOW);
            ShowWindow(internal_powershell_risk_label_, SW_SHOW);
            CheckDlgButton(scroll_content_host_, kInternalPowerShellEnabled,
                internal_powershell_enabled_ ? BST_CHECKED : BST_UNCHECKED);
            SetWindowTextW(internal_powershell_workdir_edit_,
                Utf8ToWide(workdir_).c_str());
        }
        if (index == 1) {
            panel_has_content = true;
            ShowWindow(internal_artifact_memory_enabled_check_, SW_SHOW);
            ShowWindow(internal_artifact_memory_note_label_, SW_SHOW);
            CheckDlgButton(scroll_content_host_, kInternalArtifactMemoryEnabled,
                ArtifactMemoryEffectiveEnabled() ? BST_CHECKED : BST_UNCHECKED);
            EnableWindow(internal_artifact_memory_enabled_check_,
                ArtifactMemoryForcedByLayer0() ? FALSE : TRUE);
            SetWindowTextW(internal_artifact_memory_note_label_,
                ArtifactMemoryForcedByLayer0()
                    ? L"Forced on because the selected context compression has Layer 0 enabled."
                    : L"Stores versioned artifacts and code under project memory when enabled.");
        }
        if (index == 2) {
            panel_has_content = true;
            ShowWindow(planner_enabled_check_, SW_SHOW);
            ShowWindow(planner_storage_folder_label_, SW_SHOW);
            ShowWindow(planner_storage_folder_edit_, SW_SHOW);
            ShowWindow(planner_note_label_, SW_SHOW);
            CheckDlgButton(scroll_content_host_, kPlannerEnabledCheck,
                planner_enabled_ ? BST_CHECKED : BST_UNCHECKED);
            SetWindowTextW(planner_storage_folder_edit_, Utf8ToWide(planner_storage_folder_).c_str());
        }
        if (index == 3) {
            panel_has_content = true;
            PopulateQuestionnaireModeCombo();
            ShowWindow(questionnaire_enabled_check_, SW_SHOW);
            ShowWindow(questionnaire_max_options_label_, SW_SHOW);
            ShowWindow(questionnaire_max_options_edit_, SW_SHOW);
            ShowWindow(questionnaire_restrict_mode_check_, SW_SHOW);
            ShowWindow(questionnaire_mode_label_, SW_SHOW);
            ShowWindow(questionnaire_mode_combo_, SW_SHOW);
            CheckDlgButton(scroll_content_host_, kQuestionnaireEnabledCheck,
                questionnaire_enabled_ ? BST_CHECKED : BST_UNCHECKED);
            // sub-control enabling handled by RefreshQuestionnaireControls
            RefreshQuestionnaireControls();
        }
        if (index == 4) {
            panel_has_content = true;
            ShowWindow(completion_driver_enabled_check_, SW_SHOW);
            ShowWindow(completion_driver_modes_label_, SW_SHOW);
            ShowWindow(completion_driver_modes_list_, SW_SHOW);
            ShowWindow(completion_driver_note_label_, SW_SHOW);
            CheckDlgButton(scroll_content_host_, kCompletionDriverEnabledCheck,
                completion_driver_enabled_ ? BST_CHECKED : BST_UNCHECKED);
            RefreshCompletionDriverModesList();
            EnableWindow(completion_driver_modes_label_, completion_driver_enabled_ ? TRUE : FALSE);
            EnableWindow(completion_driver_modes_list_, completion_driver_enabled_ ? TRUE : FALSE);
        }
        if (index == 5) {
            panel_has_content = true;
            ShowWindow(filesystem_enabled_check_, SW_SHOW);
            ShowWindow(filesystem_auto_archive_check_, SW_SHOW);
            ShowWindow(filesystem_working_dir_label_, SW_SHOW);
            ShowWindow(filesystem_working_dir_edit_, SW_SHOW);
            ShowWindow(filesystem_note_label_, SW_SHOW);
            CheckDlgButton(scroll_content_host_, kFilesystemEnabledCheck,
                filesystem_enabled_ ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(scroll_content_host_, kFilesystemAutoArchiveCheck,
                filesystem_auto_archive_ ? BST_CHECKED : BST_UNCHECKED);
            SetWindowTextW(filesystem_working_dir_edit_, Utf8ToWide(filesystem_working_directory_).c_str());
            EnableWindow(filesystem_auto_archive_check_, filesystem_enabled_ ? TRUE : FALSE);
            EnableWindow(filesystem_working_dir_label_, filesystem_enabled_ ? TRUE : FALSE);
            EnableWindow(filesystem_working_dir_edit_, filesystem_enabled_ ? TRUE : FALSE);
        }
        if (panel_has_content) {
            std::wstring title;
            switch (index) {
            case 0: title = L"PowerShell Settings"; break;
            case 1: title = L"Artifact/Code Memory Settings"; break;
            case 2: title = L"Planner Settings"; break;
            case 3: title = L"Questionnaire Settings"; break;
            case 4: title = L"Completion Driver Settings"; break;
            case 5: title = L"Filesystem Settings"; break;
            default: title = L"Tool Settings"; break;
            }
            SetWindowTextW(internal_tool_settings_panel_, title.c_str());
        }
        RedrawInternalToolSettingsPanel();
    }

    void OnCompressionConfigChanged() {
        int sel = ComboBox_GetCurSel(context_window_combo_);
        if (sel <= 0) {
            selected_compression_config_id_.clear();
        } else if ((sel - 1) < static_cast<int>(options_.compression_configs.size())) {
            selected_compression_config_id_ = options_.compression_configs[sel - 1].id;
        }
        RefreshInternalToolsList();
    }

    void RefreshRagList() {
        ListBox_ResetContent(rag_services_list_);
        for (const auto& row : rag_rows_) {
            std::wstring text = Utf8ToWide(row.rag_name);
            text += L" [";
            if (row.enabled) text += L"E";
            if (row.can_read) text += L"R";
            if (row.can_write) text += L"W";
            if (row.expose_as_tool) text += L"T";
            if (row.can_delete) text += L"D";
            if (row.can_export) text += L"X";
            if (row.default_ingest_target) text += L"I";
            text += L"]";
            if (row.enabled) {
                text += L" P:";
                text += std::to_wstring(row.retrieval_priority);
                text += L" C:";
                text += std::to_wstring(row.max_chunks);
                text += L" ";
                text += FormatConfidence(row.default_min_confidence);
                text += L"-";
                text += FormatConfidence(row.default_max_confidence);
            }
            ListBox_AddString(rag_services_list_, text.c_str());
        }
    }

    void OnRagSelectionChanged() {
        const int sel = ListBox_GetCurSel(rag_services_list_);
        SaveSelectedRagControls();

        SaveCurrentInternalToolSettings();
        RefreshRagList();
        if (sel >= 0 && sel < static_cast<int>(rag_rows_.size())) {
            ListBox_SetCurSel(rag_services_list_, sel);
        }
        selected_rag_index_ = (sel >= 0 && sel < static_cast<int>(rag_rows_.size())) ? sel : -1;
        LoadRagControls(selected_rag_index_);
    }

    void OnRagEnabledChanged() {
        OnRagBindingControlsChanged();
    }

    void OnRagReadChanged() {
        OnRagBindingControlsChanged();
    }

    void OnRagWriteChanged() {
        OnRagBindingControlsChanged();
    }

    void OnRagToolChanged() {
        OnRagBindingControlsChanged();
    }

    void OnRagBindingControlsChanged() {
        SaveSelectedRagControls();
        const int sel = selected_rag_index_;
        RefreshRagList();
        if (sel >= 0 && sel < static_cast<int>(rag_rows_.size())) {
            ListBox_SetCurSel(rag_services_list_, sel);
        }
        LoadRagControls(sel);
    }

    void NormalizeRagPermissionDependencies(RagRow& row) {
        row.max_chunks = std::clamp(row.max_chunks, 1, 200);
        row.default_min_confidence = std::clamp(row.default_min_confidence, 0.0, 1.0);
        row.default_max_confidence = std::clamp(row.default_max_confidence, 0.0, 1.0);
        if (row.default_min_confidence > row.default_max_confidence) {
            std::swap(row.default_min_confidence, row.default_max_confidence);
        }

        if (row.can_delete || row.default_ingest_target) {
            row.enabled = true;
            row.can_read = true;
            row.can_write = true;
        }
        if (row.can_write || row.expose_as_tool || row.can_export) {
            row.enabled = true;
            row.can_read = true;
        }
        if (!row.enabled) {
            row.can_read = false;
            row.can_write = false;
            row.expose_as_tool = false;
            row.can_delete = false;
            row.can_export = false;
            row.default_ingest_target = false;
        }
        if (!row.can_read) {
            row.can_write = false;
            row.expose_as_tool = false;
            row.can_delete = false;
            row.can_export = false;
            row.default_ingest_target = false;
        }
        if (!row.can_write) {
            row.can_delete = false;
            row.default_ingest_target = false;
        }
    }

    void EnsureSingleDefaultIngestTarget(int selected_index) {
        if (selected_index < 0 || selected_index >= static_cast<int>(rag_rows_.size()) || !rag_rows_[selected_index].default_ingest_target) {
            return;
        }
        for (int i = 0; i < static_cast<int>(rag_rows_.size()); ++i) {
            if (i != selected_index) {
                rag_rows_[i].default_ingest_target = false;
            }
        }
    }

    static int RagRetrievalModeToComboIndex(RagRetrievalMode mode) {
        switch (mode) {
            case RagRetrievalMode::PassiveOnly:    return 1;
            case RagRetrievalMode::ActiveToolOnly: return 2;
            case RagRetrievalMode::Disabled:       return 3;
            case RagRetrievalMode::Both:           // fallthrough
            default:                               return 0;
        }
    }

    static RagRetrievalMode ComboIndexToRagRetrievalMode(int index) {
        switch (index) {
            case 1:  return RagRetrievalMode::PassiveOnly;
            case 2:  return RagRetrievalMode::ActiveToolOnly;
            case 3:  return RagRetrievalMode::Disabled;
            default: return RagRetrievalMode::Both;
        }
    }

    void SetRagControlsEnabled(bool enabled) {
        HWND controls[] = {
            rag_enabled_check_, rag_read_check_, rag_write_check_, rag_tool_check_, rag_delete_check_, rag_export_check_,
            rag_default_ingest_check_, rag_priority_edit_, rag_max_chunks_edit_, rag_min_confidence_edit_, rag_max_confidence_edit_,
            rag_export_path_edit_, rag_retrieval_mode_combo_
        };
        for (HWND control : controls) {
            EnableWindow(control, enabled ? TRUE : FALSE);
        }
    }

    void LoadRagControls(int index) {
        const bool has_selection = index >= 0 && index < static_cast<int>(rag_rows_.size());
        SetRagControlsEnabled(has_selection);
        if (!has_selection) {
            Button_SetCheck(rag_enabled_check_, BST_UNCHECKED);
            Button_SetCheck(rag_read_check_, BST_UNCHECKED);
            Button_SetCheck(rag_write_check_, BST_UNCHECKED);
            Button_SetCheck(rag_tool_check_, BST_UNCHECKED);
            Button_SetCheck(rag_delete_check_, BST_UNCHECKED);
            Button_SetCheck(rag_export_check_, BST_UNCHECKED);
            Button_SetCheck(rag_default_ingest_check_, BST_UNCHECKED);
            SetWindowTextW(rag_priority_edit_, L"");
            SetWindowTextW(rag_max_chunks_edit_, L"");
            SetWindowTextW(rag_min_confidence_edit_, L"");
            SetWindowTextW(rag_max_confidence_edit_, L"");
            SetWindowTextW(rag_export_path_edit_, L"");
            ComboBox_SetCurSel(rag_retrieval_mode_combo_, 0);
            return;
        }

        auto& row = rag_rows_[index];
        NormalizeRagPermissionDependencies(row);
        Button_SetCheck(rag_enabled_check_, row.enabled ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(rag_read_check_, row.can_read ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(rag_write_check_, row.can_write ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(rag_tool_check_, row.expose_as_tool ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(rag_delete_check_, row.can_delete ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(rag_export_check_, row.can_export ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(rag_default_ingest_check_, row.default_ingest_target ? BST_CHECKED : BST_UNCHECKED);
        EnableWindow(rag_export_path_edit_, row.can_export ? TRUE : FALSE);
        SetWindowTextW(rag_priority_edit_, std::to_wstring(row.retrieval_priority).c_str());
        SetWindowTextW(rag_max_chunks_edit_, std::to_wstring(row.max_chunks).c_str());
        SetWindowTextW(rag_min_confidence_edit_, FormatConfidence(row.default_min_confidence).c_str());
        SetWindowTextW(rag_max_confidence_edit_, FormatConfidence(row.default_max_confidence).c_str());
        SetWindowTextW(rag_export_path_edit_, Utf8ToWide(row.export_path_template).c_str());
        ComboBox_SetCurSel(rag_retrieval_mode_combo_, RagRetrievalModeToComboIndex(row.retrieval_mode));
    }

    void SaveSelectedRagControls() {
        if (selected_rag_index_ < 0 || selected_rag_index_ >= static_cast<int>(rag_rows_.size())) {
            return;
        }

        auto& row = rag_rows_[selected_rag_index_];
        row.enabled = Button_GetCheck(rag_enabled_check_) == BST_CHECKED;
        row.can_read = Button_GetCheck(rag_read_check_) == BST_CHECKED;
        row.can_write = Button_GetCheck(rag_write_check_) == BST_CHECKED;
        row.expose_as_tool = Button_GetCheck(rag_tool_check_) == BST_CHECKED;
        row.can_delete = Button_GetCheck(rag_delete_check_) == BST_CHECKED;
        row.can_export = Button_GetCheck(rag_export_check_) == BST_CHECKED;
        row.export_path_template = WideToUtf8(GetWindowTextString(rag_export_path_edit_));
        row.default_ingest_target = Button_GetCheck(rag_default_ingest_check_) == BST_CHECKED;

        if (const auto priority = ParseInt(GetWindowTextString(rag_priority_edit_))) {
            row.retrieval_priority = *priority;
        }
        if (const auto max_chunks = ParseInt(GetWindowTextString(rag_max_chunks_edit_))) {
            row.max_chunks = *max_chunks;
        }
        if (const auto min_confidence = ParseDouble(GetWindowTextString(rag_min_confidence_edit_))) {
            row.default_min_confidence = *min_confidence;
        }
        if (const auto max_confidence = ParseDouble(GetWindowTextString(rag_max_confidence_edit_))) {
            row.default_max_confidence = *max_confidence;
        }

        row.retrieval_mode = ComboIndexToRagRetrievalMode(ComboBox_GetCurSel(rag_retrieval_mode_combo_));

        NormalizeRagPermissionDependencies(row);
        EnsureSingleDefaultIngestTarget(selected_rag_index_);
    }

    void RefreshProjVarsList() {
        ListBox_ResetContent(proj_vars_list_);
        for (const auto& pv : project_variables_) {
            std::wstring text = Utf8ToWide(pv.name.empty() ? std::string("(unnamed)") : pv.name);
            text += L" = ";
            text += Utf8ToWide(pv.value);
            if (pv.inject_into_context) {
                text += L" [context]";
            }
            ListBox_AddString(proj_vars_list_, text.c_str());
        }
    }

    void SelectProjVar(int index) {
        selected_proj_var_index_ = index;
        const bool has_sel = (index >= 0 && index < static_cast<int>(project_variables_.size()));
        updating_proj_var_ = true;
        if (has_sel) {
            SetWindowTextW(proj_vars_name_edit_,  Utf8ToWide(project_variables_[index].name).c_str());
            SetWindowTextW(proj_vars_value_edit_, Utf8ToWide(project_variables_[index].value).c_str());
            SetWindowTextW(proj_vars_description_edit_, Utf8ToWide(project_variables_[index].description).c_str());
            Button_SetCheck(proj_vars_inject_check_, project_variables_[index].inject_into_context ? BST_CHECKED : BST_UNCHECKED);
        } else {
            SetWindowTextW(proj_vars_name_edit_,  L"");
            SetWindowTextW(proj_vars_value_edit_, L"");
            SetWindowTextW(proj_vars_description_edit_, L"");
            Button_SetCheck(proj_vars_inject_check_, BST_UNCHECKED);
        }
        updating_proj_var_ = false;
        EnableWindow(proj_vars_name_edit_,  has_sel ? TRUE : FALSE);
        EnableWindow(proj_vars_value_edit_, has_sel ? TRUE : FALSE);
        EnableWindow(proj_vars_description_edit_, has_sel ? TRUE : FALSE);
        EnableWindow(proj_vars_inject_check_, has_sel ? TRUE : FALSE);
        EnableWindow(proj_vars_remove_btn_, has_sel ? TRUE : FALSE);
    }

    // Flush current name/value edits back into project_variables_[selected_proj_var_index_].
    void SaveCurrentProjVar() {
        if (selected_proj_var_index_ < 0 ||
            selected_proj_var_index_ >= static_cast<int>(project_variables_.size())) return;
        auto& pv = project_variables_[selected_proj_var_index_];
        pv.name  = WideToUtf8(GetWindowTextString(proj_vars_name_edit_));
        pv.value = WideToUtf8(GetWindowTextString(proj_vars_value_edit_));
        pv.description = WideToUtf8(GetWindowTextString(proj_vars_description_edit_));
        pv.inject_into_context = Button_GetCheck(proj_vars_inject_check_) == BST_CHECKED;
    }

    void OnProjVarsSelChange() {
        // Capture the new selection BEFORE RefreshProjVarsList(), because
        // ListBox_ResetContent inside that function clears the selection and
        // a subsequent GetCurSel would return -1.
        const int sel = ListBox_GetCurSel(proj_vars_list_);
        SaveCurrentProjVar();
        RefreshProjVarsList();
        if (sel >= 0) ListBox_SetCurSel(proj_vars_list_, sel);
        SelectProjVar((sel >= 0 && sel < static_cast<int>(project_variables_.size())) ? sel : -1);
    }

    void OnProjVarsAdd() {
        SaveCurrentProjVar();
        ProjectMcpVariableValue pv;
        pv.name  = "";
        pv.value = "";
        project_variables_.push_back(pv);
        RefreshProjVarsList();
        const int new_idx = static_cast<int>(project_variables_.size()) - 1;
        ListBox_SetCurSel(proj_vars_list_, new_idx);
        SelectProjVar(new_idx);
        SetFocus(proj_vars_name_edit_);
    }

    void OnProjVarsRemove() {
        if (selected_proj_var_index_ < 0 ||
            selected_proj_var_index_ >= static_cast<int>(project_variables_.size())) return;
        project_variables_.erase(project_variables_.begin() + selected_proj_var_index_);
        RefreshProjVarsList();
        const int new_idx = std::min(selected_proj_var_index_, static_cast<int>(project_variables_.size()) - 1);
        ListBox_SetCurSel(proj_vars_list_, new_idx);
        SelectProjVar((new_idx >= 0 && new_idx < static_cast<int>(project_variables_.size())) ? new_idx : -1);
    }

    // Called when the name or value edit box changes — live-update the list entry label.
    void OnProjVarEditChanged() {
        if (selected_proj_var_index_ < 0 ||
            selected_proj_var_index_ >= static_cast<int>(project_variables_.size())) return;
        auto& pv = project_variables_[selected_proj_var_index_];
        pv.name  = WideToUtf8(GetWindowTextString(proj_vars_name_edit_));
        pv.value = WideToUtf8(GetWindowTextString(proj_vars_value_edit_));
        pv.description = WideToUtf8(GetWindowTextString(proj_vars_description_edit_));
        pv.inject_into_context = Button_GetCheck(proj_vars_inject_check_) == BST_CHECKED;
        // Refresh label in-place (delete+reinsert keeps selection).
        updating_proj_var_ = true;
        std::wstring text = Utf8ToWide(pv.name.empty() ? std::string("(unnamed)") : pv.name);
        text += L" = ";
        text += Utf8ToWide(pv.value);
        if (pv.inject_into_context) {
            text += L" [context]";
        }
        ListBox_DeleteString(proj_vars_list_, selected_proj_var_index_);
        ListBox_InsertString(proj_vars_list_, selected_proj_var_index_, text.c_str());
        ListBox_SetCurSel(proj_vars_list_, selected_proj_var_index_);
        updating_proj_var_ = false;
    }

    void ImportInstructions() {
        const auto path = PickMarkdownFile(hwnd_);
        if (!path) {
            return;
        }

        try {
            SetWindowTextW(instructions_edit_, Utf8ToWide(ReadWholeFile(*path)).c_str());
        } catch (const std::exception& ex) {
            MessageBoxW(hwnd_, Utf8ToWide(std::string("Could not import project instructions: ") + ex.what()).c_str(), L"Import Failed", MB_OK | MB_ICONERROR);
        }
    }

    ProjectSettingsResult CollectCurrentSettings() {
        // Flush any unsaved variable edits from the currently-visible server.
        SaveCurrentVariableValues();

        // Flush any in-progress project variable edits.
        SaveCurrentProjVar();

        SaveSelectedRagControls();

        SaveCurrentInternalToolSettings();

        // Ensure selected_compression_config_id is current from combo
        int sel = ComboBox_GetCurSel(context_window_combo_);
        if (sel <= 0) {
            selected_compression_config_id_.clear();
        } else if ((sel - 1) < static_cast<int>(options_.compression_configs.size())) {
            selected_compression_config_id_ = options_.compression_configs[sel - 1].id;
        }

        // Flush global variable values back into each server state.
        // During editing, global variable changes are kept in global_values_.
        // Server bindings store values per-variable in their own list, so we must
        // update (or insert) the global entries in each state before persisting.
        for (auto& state : states_) {
            // Find the server config so we know which variables it declares.
            const auto server_it = std::find_if(options_.servers.begin(), options_.servers.end(),
                [&](const McpServerConfig& s) { return s.id == state.server_id; });
            if (server_it == options_.servers.end()) continue;

            for (const auto& global_val : global_values_) {
                // Only inject if this server actually declares the variable.
                const bool server_uses = std::find_if(
                    server_it->variables.begin(), server_it->variables.end(),
                    [&](const McpServerVariable& v) { return v.name == global_val.name; }
                ) != server_it->variables.end();
                if (!server_uses) continue;

                auto it = std::find_if(state.variables.begin(), state.variables.end(),
                    [&](const ProjectMcpVariableValue& v) { return v.name == global_val.name; });
                if (it != state.variables.end()) {
                    it->value = global_val.value;
                } else {
                    state.variables.push_back(global_val);
                }
            }
        }

        ProjectSettingsResult result;

        // Build MCP bindings
        for (size_t i = 0; i < states_.size(); ++i) {
            if (states_[i].selected) {
                ProjectMcpServerBinding binding;
                binding.server_id = options_.servers[i].id;
                binding.variables = states_[i].variables;
                result.mcp_bindings.push_back(std::move(binding));
            }
        }

        // Build RAG bindings
        for (const auto& row : rag_rows_) {
            if (row.enabled) {
                ProjectRagBinding binding;
                binding.rag_id = row.rag_id;
                binding.enabled = row.enabled;
                binding.can_read = row.can_read;
                binding.can_write = row.can_write;
                binding.expose_as_tool = row.expose_as_tool;
                binding.can_delete = row.can_delete;
                binding.can_export = row.can_export;
                binding.export_path_template = row.export_path_template;
                binding.default_ingest_target = row.default_ingest_target;
                binding.retrieval_priority = row.retrieval_priority;
                binding.max_chunks = row.max_chunks;
                binding.default_min_confidence = row.default_min_confidence;
                binding.default_max_confidence = row.default_max_confidence;
                binding.retrieval_mode = row.retrieval_mode;
                result.rag_bindings.push_back(std::move(binding));
            }
        }

        // Collect selected model
        std::string preferred_provider_id;
        std::string preferred_model_id;
        {
            const int model_sel = ComboBox_GetCurSel(model_combo_);
            if (model_sel > 0 && static_cast<size_t>(model_sel) < model_entries_.size()) {
                preferred_provider_id = model_entries_[static_cast<size_t>(model_sel)].provider_id;
                preferred_model_id = model_entries_[static_cast<size_t>(model_sel)].model_id;
            }
            // sel == 0 means "No preference" — leave both empty
        }

        // Collect enabled model tool IDs
        std::vector<std::string> model_tool_ids;
        for (size_t i = 0; i < model_tool_enabled_.size(); ++i) {
            if (model_tool_enabled_[i] && i < options_.model_tools.size()) {
                model_tool_ids.push_back(options_.model_tools[i].id);
            }
        }

        // Collect agentic modes
        std::string selected_agentic_mode_id;
        std::vector<std::string> enabled_agentic_mode_ids;
        {
            selected_agentic_mode_id = CurrentDefaultAgenticModeId();
            for (size_t i = 0; i < agentic_mode_enabled_.size(); ++i) {
                if (agentic_mode_enabled_[i] && i < options_.agentic_modes.size()) {
                    enabled_agentic_mode_ids.push_back(options_.agentic_modes[i].id);
                }
            }
            if (!selected_agentic_mode_id.empty() &&
                std::find(enabled_agentic_mode_ids.begin(), enabled_agentic_mode_ids.end(),
                          selected_agentic_mode_id) == enabled_agentic_mode_ids.end()) {
                enabled_agentic_mode_ids.push_back(selected_agentic_mode_id);
            }
        }

        result.project_name = WideToUtf8(options_.project_name);
        result.project_instructions = WideToUtf8(GetWindowTextString(instructions_edit_));
        result.selected_compression_config_id = selected_compression_config_id_;
        result.preferred_provider_id = preferred_provider_id;
        result.preferred_model_id = preferred_model_id;
        result.model_tool_ids = std::move(model_tool_ids);
        result.project_variables = project_variables_;
        result.selected_agentic_mode_id = std::move(selected_agentic_mode_id);
        result.enabled_agentic_mode_ids = std::move(enabled_agentic_mode_ids);
        result.enable_chat_logging = (IsDlgButtonChecked(scroll_content_host_, kChatLoggingCheck) == BST_CHECKED);
        result.allow_manual_context_compression = (IsDlgButtonChecked(scroll_content_host_, kManualContextCompressionCheck) == BST_CHECKED);
        result.enable_web_debugging = (IsDlgButtonChecked(scroll_content_host_, kWebDebuggingCheck) == BST_CHECKED);
        result.serve_web_links_inline = (IsDlgButtonChecked(scroll_content_host_, kInlineWebLinksCheck) == BST_CHECKED);
        result.enable_automation = (IsDlgButtonChecked(scroll_content_host_, kAutomationCheck) == BST_CHECKED);
        result.built_in_powershell_enabled = InternalToolListChecked(0, internal_powershell_enabled_);
        result.built_in_powershell_working_directory = workdir_;
        if (result.built_in_powershell_working_directory.empty()) {
            result.built_in_powershell_working_directory = "$ProjectFolder$";
        }
        result.built_in_artifact_memory_enabled = internal_artifact_memory_enabled_;
        result.built_in_planner_enabled = InternalToolListChecked(2, planner_enabled_);
        result.built_in_planner_storage_folder = planner_storage_folder_;
        if (result.built_in_planner_storage_folder.empty()) {
            result.built_in_planner_storage_folder = "$ProjectFolder$\\.agent";
        }
        result.built_in_completion_driver_enabled = InternalToolListChecked(4, completion_driver_enabled_);
        for (size_t i = 0; i < completion_driver_mode_allowed_.size() && i < options_.agentic_modes.size(); ++i) {
            if (completion_driver_mode_allowed_[i]) {
                result.completion_driver_allowed_mode_ids.push_back(options_.agentic_modes[i].id);
            }
        }
        result.built_in_filesystem_enabled = InternalToolListChecked(5, filesystem_enabled_);
        result.built_in_filesystem_auto_archive = filesystem_auto_archive_;
        result.built_in_filesystem_working_directory = filesystem_working_directory_;
        if (result.built_in_filesystem_working_directory.empty()) {
            result.built_in_filesystem_working_directory = "$ProjectFolder$";
        }
        const std::wstring max_opts_text = TrimWide(GetWindowTextString(questionnaire_max_options_edit_));
        result.built_in_questionnaire_enabled = questionnaire_enabled_;
        if (!max_opts_text.empty()) {
            result.questionnaire_max_options = std::stoi(max_opts_text);
            if (result.questionnaire_max_options < 2) result.questionnaire_max_options = 2;
            if (result.questionnaire_max_options > 50) result.questionnaire_max_options = 50;
        } else {
            result.questionnaire_max_options = 8;
        }
        result.questionnaire_restrict_by_mode = (IsDlgButtonChecked(scroll_content_host_, kQuestionnaireRestrictModeCheck) == BST_CHECKED);
        {
            std::string q_mode_id = CurrentQuestionnaireModeId();
            if (result.questionnaire_restrict_by_mode && q_mode_id.empty()) {
                q_mode_id = FallbackQuestionnaireModeId();
            }
            result.questionnaire_allowed_mode_id = std::move(q_mode_id);
        }
        const std::wstring timeout_text = TrimWide(GetWindowTextString(model_timeout_edit_));
        if (!timeout_text.empty()) {
            result.model_timeout_seconds = std::stoi(timeout_text);
            if (result.model_timeout_seconds < 0) result.model_timeout_seconds = 0;
        }
        return result;
    }

    std::vector<ProjectMcpVariableValue> BuildPreviewRuntimeVariables(
        const ProjectSettingsResult& settings) const {
        std::vector<ProjectMcpVariableValue> values;
        variable_resolver::UpsertValue(values, "PROJECTNAME", settings.project_name);
        variable_resolver::UpsertValue(values, "CHATNAME", "My Chat");
        variable_resolver::UpsertValue(values, "CHATFULLNAME", "My Chat (admin)");
        variable_resolver::UpsertValue(values, "CHATID", "chat_1776423689503_8");
        variable_resolver::UpsertValue(values, "USERNAME", "admin");
        variable_resolver::UpsertValue(values, "USER", "The Admin");
        variable_resolver::UpsertValue(values, "USEREMAIL", "admin@theadmin.store");
        variable_resolver::UpsertValue(values, "UserName", "admin");
        return values;
    }

    std::vector<ProjectMcpVariableValue> BuildResolvedPreviewVariables(
        const ProjectSettingsResult& settings) const {
        std::vector<ProjectMcpVariableValue> values;
        for (const auto& binding : settings.mcp_bindings) {
            for (const auto& variable : binding.variables) {
                variable_resolver::UpsertValue(values, variable.name, variable.value);
            }
        }
        for (const auto& variable : settings.project_variables) {
            variable_resolver::UpsertValue(values, variable);
        }
        for (const auto& variable : BuildPreviewRuntimeVariables(settings)) {
            variable_resolver::UpsertValue(values, variable.name, variable.value);
        }
        return variable_resolver::ResolveValues(values);
    }

    const McpServerConfig* FindServerConfig(const std::string& server_id) const {
        const auto it = std::find_if(options_.servers.begin(), options_.servers.end(),
            [&](const McpServerConfig& server) {
                return server.id == server_id;
            });
        return it == options_.servers.end() ? nullptr : &*it;
    }

    const ModelToolConfig* FindModelToolConfig(const std::string& tool_id) const {
        const auto it = std::find_if(options_.model_tools.begin(), options_.model_tools.end(),
            [&](const ModelToolConfig& tool) {
                return tool.id == tool_id;
            });
        return it == options_.model_tools.end() ? nullptr : &*it;
    }

    std::pair<const ProviderConfig*, const ModelConfig*> ResolvePreviewModel(
        const ProjectSettingsResult& settings) const {
        if (!settings.preferred_provider_id.empty() && !settings.preferred_model_id.empty()) {
            for (const auto& provider : options_.providers) {
                if (provider.id != settings.preferred_provider_id) continue;
                for (const auto& model : provider.models) {
                    if (model.id == settings.preferred_model_id) {
                        return {&provider, &model};
                    }
                }
            }
        }
        if (!options_.providers.empty() && !options_.providers.front().models.empty()) {
            return {&options_.providers.front(), &options_.providers.front().models.front()};
        }
        return {nullptr, nullptr};
    }

    std::vector<rag_tools::RagToolLibrary> BuildPreviewRagToolLibraries(
        const ProjectSettingsResult& settings) const {
        std::vector<rag_tools::RagToolLibrary> libraries;
        for (const auto& binding : settings.rag_bindings) {
            if (!binding.enabled || !binding.expose_as_tool || !binding.can_read) {
                continue;
            }
            if (binding.retrieval_mode == RagRetrievalMode::PassiveOnly ||
                binding.retrieval_mode == RagRetrievalMode::Disabled) {
                continue;
            }
            const auto* library = FindRagLibrary(options_.available_rags, binding.rag_id);
            if (!library || !library->enabled) {
                continue;
            }
            libraries.push_back({*library, binding});
        }
        return libraries;
    }

    static void AppendPreviewSection(std::ostringstream& stream, const std::string& title) {
        stream << "\n============================================================\n";
        stream << title << "\n";
        stream << "============================================================\n";
    }

    std::string BuildMcpProjectContextPreview(
        const ProjectSettingsResult& settings,
        const std::vector<ProjectMcpVariableValue>& values) const {
        struct ContextVariable {
            std::string name;
            std::string value;
            std::string description;
        };

        std::vector<ContextVariable> context_values;
        std::vector<std::string> emitted_names;
        const auto add_variable = [&](const McpServerVariable& variable) {
            if (!variable.inject_into_context || variable.name.empty()) return;
            const std::string emitted_key = variable_resolver::ToLookupKey(variable.name);
            if (std::find(emitted_names.begin(), emitted_names.end(), emitted_key) != emitted_names.end()) {
                return;
            }
            const auto value = variable_resolver::FindValue(values, variable.name);

            ContextVariable context_variable;
            context_variable.name = variable.name;
            context_variable.value = value.value_or("");
            context_variable.description = Trim(variable.description);
            context_values.push_back(std::move(context_variable));
            emitted_names.push_back(emitted_key);
        };
        const auto add_project_variable = [&](const ProjectMcpVariableValue& variable) {
            if (!variable.inject_into_context || variable.name.empty()) return;
            const std::string emitted_key = variable_resolver::ToLookupKey(variable.name);
            if (std::find(emitted_names.begin(), emitted_names.end(), emitted_key) != emitted_names.end()) {
                return;
            }
            const auto value = variable_resolver::FindValue(values, variable.name);
            if (!value || Trim(*value).empty()) return;

            ContextVariable context_variable;
            context_variable.name = variable.name;
            context_variable.value = *value;
            context_variable.description = Trim(variable.description);
            context_values.push_back(std::move(context_variable));
            emitted_names.push_back(emitted_key);
        };

        for (const auto& variable : options_.global_variables) {
            add_variable(variable);
        }
        for (const auto& variable : settings.project_variables) {
            add_project_variable(variable);
        }
        for (const auto& binding : settings.mcp_bindings) {
            const auto* server = FindServerConfig(binding.server_id);
            if (!server) continue;
            for (const auto& variable : server->variables) {
                add_variable(variable);
            }
        }

        if (context_values.empty()) return {};

        std::ostringstream stream;
        stream << "MCP Project Context:\n";
        stream << "These project variable values are current for this chat. Use them when choosing paths, working directories, command locations, and MCP tool arguments.\n";
        for (const auto& variable : context_values) {
            stream << "- " << variable.name << ": " << variable.value;
            if (!variable.description.empty()) {
                stream << " (description: " << variable.description << ")";
            }
            stream << "\n";
        }
        return stream.str();
    }

    std::string BuildRagProjectContextPreview(
        const ProjectSettingsResult& settings) const {
        const auto libraries = BuildPreviewRagToolLibraries(settings);
        if (libraries.empty()) return {};

        std::ostringstream stream;
        stream << "Project RAG MCP Servers:\n";
        stream << "Each RAG library below is exposed as its own MCP-style server. "
                  "Use the server name and description to choose the correct RAG. "
                  "When listing MCP servers, use these library names as the server names; "
                  "do not call them RAG (Anonymous) or generic RAG Tools. "
                  "Passive RAG retrieval is inactive in this phase; use the active RAG tools explicitly. "
                  "RAG tool results may include download_url for server-hosted downloads; source_path, "
                  "original_source_uri, and original file paths are provenance only and may refer to another "
                  "computer, another system, or a file that no longer exists.\n";
        for (const auto& item : libraries) {
            const std::string library_name = rag_tools::RagLibraryDisplayName(item.library);
            stream << "- MCP server: " << library_name << "\n";
            stream << "  RAG library id: " << item.library.id << "\n";
            stream << "  RAG library name: " << library_name << "\n";
            stream << "  RAG library description: " << rag_tools::RagLibraryDescription(item.library) << "\n";
            stream << "  Available tools:";
            stream << " " << rag_tools::kSearchToolName << " (" << rag_tools::BuildRagToolAlias(item.library, rag_tools::kSearchToolName) << ")";
            stream << ", " << rag_tools::kGetDocumentToolName << " (" << rag_tools::BuildRagToolAlias(item.library, rag_tools::kGetDocumentToolName) << ")";
            stream << ", " << rag_tools::kListLibrariesToolName << " (" << rag_tools::BuildRagToolAlias(item.library, rag_tools::kListLibrariesToolName) << ")";
            if (item.binding.can_export && !Trim(item.binding.export_path_template).empty()) {
                stream << ", " << rag_tools::kWriteDocumentToDriveToolName << " ("
                       << rag_tools::BuildRagToolAlias(item.library, rag_tools::kWriteDocumentToDriveToolName) << ")";
            }
            if (item.binding.can_write) {
                stream << ", " << rag_tools::kIngestGeneratedDocumentToolName << " ("
                       << rag_tools::BuildRagToolAlias(item.library, rag_tools::kIngestGeneratedDocumentToolName) << ")";
            }
            stream << "\n";
        }
        return stream.str();
    }

    std::string BuildWebFormattingContextPreview() const {
        return
            "Web Chat Formatting Capabilities:\n"
            "This response is being rendered in the web chat. Mermaid and Vega-Lite libraries are available for visual output.\n"
            "- Use fenced code blocks tagged `mermaid` for flowcharts, sequence diagrams, state diagrams, class diagrams, timelines, and similar diagrams.\n"
            "- Use fenced code blocks tagged `vega-lite` with JSON for charts and data graphics. Keep Vega-Lite specs self-contained with inline data when possible.\n"
            "- Prefer these formats when a diagram, graph, or flow chart would make the answer clearer.\n"
            "\nWeb Download Links:\n"
            "- You may generate direct download URLs from raw link data by combining the server address with the route. Relative routes also work inside this web chat.\n"
            "- Project-accessible files can be linked as `server_address/data/{project_id}/{chat_id}/{file_or_relative_path}` or `/data/{project_id}/{chat_id}/{file_or_relative_path}`. The server only serves files inside configured folder variables for the current user and chat.\n"
            "- RAG originals can be linked as `server_address/rag/{project_id}/{rag_id}/{document_id}` or `/rag/{project_id}/{rag_id}/{document_id}` when a readable exposed RAG tool returns a document_id or download_url.\n"
            "- For RAG documents, source_path, original_source_uri, and original file names are provenance only. They may refer to another computer or a file that no longer exists. Use download_url or the /rag route for downloadable links.\n"
            "- Preview project_id uses this project; preview chat_id is chat_1776423689503_8.";
    }

    std::string BuildInjectedSystemContext(
        const ProjectSettingsResult& settings,
        const std::vector<ProjectMcpVariableValue>& resolved_values) const {
        std::string context;

        const std::string instructions =
            variable_resolver::ExpandTemplate(settings.project_instructions, resolved_values);
        if (!Trim(instructions).empty()) {
            context += "Project Instructions:\n";
            context += instructions;
        }

        // Preview selected agentic mode instructions
        if (!settings.selected_agentic_mode_id.empty()) {
            const auto it = std::find_if(options_.agentic_modes.begin(), options_.agentic_modes.end(),
                [&](const AgenticModeConfig& m) { return m.id == settings.selected_agentic_mode_id; });
            if (it != options_.agentic_modes.end()) {
                const std::string mode_instructions =
                    variable_resolver::ExpandTemplate(NormalizeNewlinesToLf(it->instructions), resolved_values);
                if (!Trim(mode_instructions).empty()) {
                    if (!context.empty()) context += "\n\n";
                    context += "Agentic Mode Instructions (" + it->name + "):\n";
                    context += mode_instructions;
                }
            }
        }

        ProjectSettings preview_settings;
        preview_settings.built_in_completion_driver_enabled = settings.built_in_completion_driver_enabled;
        preview_settings.completion_driver_allowed_mode_ids = settings.completion_driver_allowed_mode_ids;

        if (settings.built_in_powershell_enabled) {
            if (!context.empty()) context += "\n\n";
            context += built_in_tools::PowerShellSystemPrompt();
        }
        if (settings.built_in_planner_enabled) {
            if (!context.empty()) context += "\n\n";
            context += built_in_tools::PlannerSystemPrompt();
        }
        if (built_in_tools::IsCompletionDriverEnabled(preview_settings, settings.selected_agentic_mode_id)) {
            if (!context.empty()) context += "\n\n";
            context += built_in_tools::CompletionDriverSystemPrompt();
        }
        if (settings.built_in_questionnaire_enabled) {
            if (!context.empty()) context += "\n\n";
            context += built_in_tools::QuestionnaireSystemPrompt();
        }
        if (settings.built_in_filesystem_enabled) {
            if (!context.empty()) context += "\n\n";
            context += built_in_tools::FilesystemSystemPrompt();
        }

        const std::string mcp_context =
            BuildMcpProjectContextPreview(settings, resolved_values);
        if (!mcp_context.empty()) {
            if (!context.empty()) context += "\n\n";
            context += mcp_context;
        }

        const std::string rag_context = BuildRagProjectContextPreview(settings);
        if (!rag_context.empty()) {
            if (!context.empty()) context += "\n\n";
            context += rag_context;
        }

        const std::string web_formatting_context = BuildWebFormattingContextPreview();
        if (!web_formatting_context.empty()) {
            if (!context.empty()) context += "\n\n";
            context += web_formatting_context;
        }

        return context;
    }

    std::string BuildToolAndSetupPreview(
        const ProjectSettingsResult& settings,
        const std::vector<ProjectMcpVariableValue>& values) const {
        std::ostringstream stream;
        AppendPreviewSection(stream, "TOOL AND SETUP PREVIEW");

        const auto [provider, model] = ResolvePreviewModel(settings);
        if (provider && model) {
            stream << "Selected model: " << provider->name << " / " << model->display_name << "\n";
            stream << "Model supports tool calls: " << YesNo(model->supports_tools) << "\n";
        } else {
            stream << "Selected model: none configured\n";
            stream << "Model supports tool calls: no\n";
        }
        stream << "Tool definitions are sent to the model separately from the system context text above.\n\n";

        stream << "Selected MCP servers:\n";
        if (settings.mcp_bindings.empty()) {
            stream << "- none\n";
        } else {
            for (const auto& binding : settings.mcp_bindings) {
                const auto* server = FindServerConfig(binding.server_id);
                if (!server) {
                    stream << "- Missing server config for id " << binding.server_id << "\n";
                    continue;
                }
                stream << "- " << (server->name.empty() ? server->id : server->name) << "\n";
                stream << "  id: " << server->id << "\n";
                stream << "  scope: " << (server->scope == McpServerScope::Shared ? "shared" : "per_project") << "\n";
                stream << "  enabled: " << YesNo(server->enabled) << "\n";
                stream << "  auto_connect: " << YesNo(server->auto_connect) << "\n";
                stream << "  command: " << variable_resolver::ExpandTemplate(server->command, values) << "\n";
                stream << "  working_directory: " << variable_resolver::ExpandTemplate(server->working_directory, values) << "\n";
                if (!server->arguments.empty()) {
                    stream << "  arguments:\n";
                    for (const auto& argument : server->arguments) {
                        stream << "    - " << variable_resolver::ExpandTemplate(argument, values) << "\n";
                    }
                }
                if (!server->env_entries.empty()) {
                    stream << "  environment:\n";
                    for (const auto& entry : server->env_entries) {
                        stream << "    - " << variable_resolver::ExpandTemplate(entry, values) << "\n";
                    }
                }
                stream << "  declared variables:\n";
                std::vector<McpServerVariable> definitions = options_.global_variables;
                definitions = variable_resolver::MergeDefinitions(definitions, server->variables);
                for (const auto& variable : definitions) {
                    const auto value = variable_resolver::FindValue(values, variable.name);
                    if (!value) continue;
                    stream << "    - " << variable.name << " = " << *value
                           << " [" << VariableKindLabel(variable.kind)
                           << ", inject=" << YesNo(variable.inject_into_context) << "]\n";
                }
            }
        }

        stream << "\nRAG tool servers:\n";
        const auto libraries = BuildPreviewRagToolLibraries(settings);
        if (libraries.empty()) {
            stream << "- none exposed as active tools\n";
        } else {
            for (const auto& item : libraries) {
                const std::string library_name = rag_tools::RagLibraryDisplayName(item.library);
                stream << "- " << library_name << "\n";
                stream << "  description: " << rag_tools::RagLibraryDescription(item.library) << "\n";
                stream << "  retrieval_mode: " << RagRetrievalModeLabel(item.binding.retrieval_mode) << "\n";
                stream << "  tools:\n";
                stream << "    - " << rag_tools::BuildRagToolAlias(item.library, rag_tools::kListLibrariesToolName) << " -> " << rag_tools::kListLibrariesToolName << "\n";
                stream << "    - " << rag_tools::BuildRagToolAlias(item.library, rag_tools::kSearchToolName) << " -> " << rag_tools::kSearchToolName << "\n";
                stream << "    - " << rag_tools::BuildRagToolAlias(item.library, rag_tools::kGetDocumentToolName) << " -> " << rag_tools::kGetDocumentToolName << "\n";
                if (item.binding.can_export && !Trim(item.binding.export_path_template).empty()) {
                    stream << "    - " << rag_tools::BuildRagToolAlias(item.library, rag_tools::kWriteDocumentToDriveToolName) << " -> " << rag_tools::kWriteDocumentToDriveToolName << "\n";
                    stream << "      write_file_folder: " << variable_resolver::ExpandTemplate(item.binding.export_path_template, values) << "\n";
                }
                if (item.binding.can_write) {
                    stream << "    - " << rag_tools::BuildRagToolAlias(item.library, rag_tools::kIngestGeneratedDocumentToolName) << " -> " << rag_tools::kIngestGeneratedDocumentToolName << "\n";
                }
            }
        }

        stream << "\nEnabled model tools:\n";
        if (settings.model_tool_ids.empty()) {
            stream << "- none\n";
        } else {
            for (const auto& tool_id : settings.model_tool_ids) {
                const auto* tool = FindModelToolConfig(tool_id);
                if (!tool) {
                    stream << "- Missing model tool config for id " << tool_id << "\n";
                    continue;
                }
                stream << "- agent_" << SanitizeModelToolName(tool->name.empty() ? tool->id : tool->name)
                       << " (" << (tool->name.empty() ? tool->id : tool->name) << ")\n";
                stream << "  description: " << tool->description << "\n";
                const std::string expanded_instructions =
                    variable_resolver::ExpandTemplate(tool->instructions, values);
                if (!Trim(expanded_instructions).empty()) {
                    stream << "  expanded instructions preview:\n";
                    stream << expanded_instructions << "\n";
                }
            }
        }

        return stream.str();
    }

    std::string BuildContextPreviewText(const ProjectSettingsResult& settings) const {
        const auto resolved_values = BuildResolvedPreviewVariables(settings);
        const std::string injected_context =
            BuildInjectedSystemContext(settings, resolved_values);
        const nlohmann::json system_message = {
            {"role", "system"},
            {"content", injected_context},
        };
        std::ostringstream stream;
        stream << "Context Window Preview\n";
        stream << "Project: " << settings.project_name << "\n";
        stream << "\nPREVIEW NOTES - NOT SENT TO THE MODEL\n";
        stream << "- This preview excludes the user's actual prompt and message history.\n";
        stream << "- Tool definitions are sent beside the message list, not inside the system context text.\n";
        stream << "- The model receives the context as one system-message content string. The readable view below preserves line breaks so humans can inspect it; the one-line JSON view shows the escaped transport shape.\n";
        stream << "- Sample web chat identity for this preview:\n";
        stream << "  PROJECTNAME=" << settings.project_name << "\n";
        stream << "  CHATNAME=My Chat\n";
        stream << "  CHATFULLNAME=My Chat (admin)\n";
        stream << "  CHATID=chat_1776423689503_8\n";
        stream << "  USERNAME=admin\n";
        stream << "  USER=The Admin\n";
        stream << "  USEREMAIL=admin@theadmin.store\n";

        AppendPreviewSection(stream, "ACTUAL INJECTED SYSTEM CONTEXT - START");
        if (injected_context.empty()) {
            stream << "(empty)\n";
        } else {
            stream << injected_context << "\n";
        }
        AppendPreviewSection(stream, "ACTUAL INJECTED SYSTEM CONTEXT - END");

        AppendPreviewSection(stream, "SYSTEM MESSAGE JSON - ONE LINE");
        stream << system_message.dump() << "\n";

        AppendPreviewSection(stream, "SYSTEM MESSAGE JSON - READABLE DISPLAY");
        stream << system_message.dump(2) << "\n";

        AppendPreviewSection(stream, "READABLE DIAGNOSTICS - NOT SENT TO THE MODEL");
        stream << "Resolved variables:\n";
        if (resolved_values.empty()) {
            stream << "(none)\n";
        } else {
            for (const auto& variable : resolved_values) {
                stream << "- " << variable.name << ": " << variable.value;
                if (!variable.description.empty()) {
                    stream << " (description: " << variable.description << ")";
                }
                if (variable.inject_into_context) {
                    stream << " [context]";
                }
                stream << "\n";
            }
        }

        stream << BuildToolAndSetupPreview(settings, resolved_values);
        return stream.str();
    }

    void ShowContextPreview() {
        const auto settings = CollectCurrentSettings();
        ShowContextPreviewWindow(hwnd_, BuildContextPreviewText(settings));
    }

    void SaveAndClose() {
        result_ = CollectCurrentSettings();

        DestroyWindow(hwnd_);
    }

    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    ProjectSettingsOptions options_;
    std::optional<ProjectSettingsResult> result_;

    // Server state
    std::vector<ServerState> states_;
    int selected_server_index_ = -1;
    std::vector<ProjectMcpVariableValue> global_values_;
    std::vector<VariableControl> variable_controls_;

    // Controls
    HWND server_list_ = nullptr;
    HWND add_server_button_ = nullptr;
    HWND remove_server_button_ = nullptr;
    HWND server_details_panel_ = nullptr;
    HWND server_enabled_check_ = nullptr;
    HWND server_name_label_ = nullptr;
    HWND server_scope_label_ = nullptr;
    HWND variables_header_ = nullptr;

    // Model selection entries (mirrors provider list)
    struct ModelEntry {
        std::string provider_id;
        std::string model_id;
    };
    std::vector<ModelEntry> model_entries_;

    // Compression configs
    std::string selected_compression_config_id_;

    // RAG rows
    std::vector<RagRow> rag_rows_;
    int selected_rag_index_ = -1;

    HWND model_label_ = nullptr;
    HWND model_combo_ = nullptr;
    HWND model_timeout_label_ = nullptr;
    HWND model_timeout_edit_ = nullptr;
    HWND context_window_label_ = nullptr;
    HWND context_window_combo_ = nullptr;
    HWND rag_services_header_ = nullptr;
    HWND rag_services_list_ = nullptr;
    HWND rag_enabled_check_ = nullptr;
    HWND rag_read_check_ = nullptr;
    HWND rag_write_check_ = nullptr;
    HWND rag_tool_check_ = nullptr;
    HWND rag_delete_check_ = nullptr;
    HWND rag_export_check_ = nullptr;
    HWND rag_default_ingest_check_ = nullptr;
    HWND rag_priority_label_ = nullptr;
    HWND rag_priority_edit_ = nullptr;
    HWND rag_max_chunks_label_ = nullptr;
    HWND rag_max_chunks_edit_ = nullptr;
    HWND rag_min_confidence_label_ = nullptr;
    HWND rag_min_confidence_edit_ = nullptr;
    HWND rag_max_confidence_label_ = nullptr;
    HWND rag_max_confidence_edit_ = nullptr;
    HWND rag_export_path_label_ = nullptr;
    HWND rag_export_path_edit_ = nullptr;
    HWND rag_retrieval_mode_label_ = nullptr;
    HWND rag_retrieval_mode_combo_ = nullptr;
    HWND model_tools_header_ = nullptr;
    HWND model_tools_list_ = nullptr;
    std::vector<bool> model_tool_enabled_;
    bool toggling_model_tool_ = false;  // re-entrancy guard for checkbox toggle

    HWND agentic_mode_label_ = nullptr;
    HWND agentic_mode_combo_ = nullptr;
    HWND agentic_modes_list_label_ = nullptr;
    HWND agentic_modes_list_ = nullptr;
    WNDPROC agentic_modes_list_prev_proc_ = nullptr;
    HWND chat_logging_check_ = nullptr;
    HWND manual_context_compression_check_ = nullptr;
    HWND web_debugging_check_ = nullptr;
    HWND inline_web_links_check_ = nullptr;
    HWND automation_check_ = nullptr;
    HWND internal_tools_header_ = nullptr;
    HWND internal_tools_list_ = nullptr;
    WNDPROC internal_tools_list_prev_proc_ = nullptr;
    HWND internal_tool_settings_panel_ = nullptr;
    HWND internal_powershell_enabled_check_ = nullptr;
    HWND internal_powershell_workdir_label_ = nullptr;
    HWND internal_powershell_workdir_edit_ = nullptr;
    HWND internal_powershell_risk_label_ = nullptr;
    HWND internal_artifact_memory_enabled_check_ = nullptr;
    HWND internal_artifact_memory_note_label_ = nullptr;
    HWND planner_enabled_check_ = nullptr;
    HWND planner_storage_folder_label_ = nullptr;
    HWND planner_storage_folder_edit_ = nullptr;
    HWND planner_note_label_ = nullptr;
    HWND completion_driver_enabled_check_ = nullptr;
    HWND completion_driver_modes_label_ = nullptr;
    HWND completion_driver_modes_list_ = nullptr;
    HWND completion_driver_note_label_ = nullptr;
    WNDPROC completion_driver_modes_list_prev_proc_ = nullptr;
    HWND questionnaire_enabled_check_ = nullptr;
    HWND questionnaire_max_options_label_ = nullptr;
    HWND questionnaire_max_options_edit_ = nullptr;
    HWND questionnaire_restrict_mode_check_ = nullptr;
    HWND questionnaire_mode_label_ = nullptr;
    HWND questionnaire_mode_combo_ = nullptr;
    HWND filesystem_enabled_check_ = nullptr;
    HWND filesystem_auto_archive_check_ = nullptr;
    HWND filesystem_working_dir_label_ = nullptr;
    HWND filesystem_working_dir_edit_ = nullptr;
    HWND filesystem_note_label_ = nullptr;
    bool internal_powershell_enabled_ = false;
    bool internal_artifact_memory_enabled_ = false;
    bool planner_enabled_ = false;
    bool completion_driver_enabled_ = false;
    bool filesystem_enabled_ = false;
    bool filesystem_auto_archive_ = false;
    bool toggling_internal_tool_ = false;
    std::vector<bool> agentic_mode_enabled_;
    std::vector<bool> completion_driver_mode_allowed_;
    bool toggling_agentic_mode_ = false;

    bool questionnaire_enabled_ = false;
    int selected_internal_tool_index_ = 0;
    std::string workdir_ = "$ProjectFolder$";
    std::string planner_storage_folder_ = "$ProjectFolder$\\.agent";
    std::string filesystem_working_directory_ = "$ProjectFolder$";

    HWND scroll_panel_ = nullptr;
    HWND scroll_backdrop_ = nullptr;
    HWND scroll_content_host_ = nullptr;
    int scroll_offset_ = 0;
    int scroll_viewport_height_ = 0;
    int scroll_content_height_ = 0;
    int scroll_content_width_ = 0;

    HWND instructions_label_ = nullptr;
    HWND instructions_edit_ = nullptr;
    HWND import_instructions_button_ = nullptr;
    HWND check_context_button_ = nullptr;
    HWND save_button_ = nullptr;
    HWND cancel_button_ = nullptr;

    // Project variables
    std::vector<ProjectMcpVariableValue> project_variables_;
    int selected_proj_var_index_ = -1;
    bool updating_proj_var_ = false;  // re-entrancy guard for EN_CHANGE
    HWND proj_vars_header_     = nullptr;
    HWND proj_vars_list_       = nullptr;
    HWND proj_vars_add_btn_    = nullptr;
    HWND proj_vars_remove_btn_ = nullptr;
    HWND proj_vars_name_label_  = nullptr;
    HWND proj_vars_name_edit_   = nullptr;
    HWND proj_vars_value_label_ = nullptr;
    HWND proj_vars_value_edit_  = nullptr;
    HWND proj_vars_description_label_ = nullptr;
    HWND proj_vars_description_edit_ = nullptr;
    HWND proj_vars_inject_check_ = nullptr;
};

}  // namespace

std::optional<ProjectSettingsResult> ShowProjectSettingsDialog(HWND owner, const ProjectSettingsOptions& options) {
    return ProjectSettingsDialog::Show(owner, options);
}
