#include "project_settings_dialog.h"

#include "prompt_dialog.h"
#include "util.h"

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

    // Footer
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
            1050,
            750,
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

        // Model selection section
        model_label_ = CreateWindowExW(0, L"STATIC", L"AI Model:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kModelLabel), nullptr, nullptr);
        model_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kModelCombo), nullptr, nullptr);

        // Context window section
        context_window_label_ = CreateWindowExW(0, L"STATIC", L"Context Window Compression:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kContextWindowLabel), nullptr, nullptr);
        context_window_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kContextWindowCombo), nullptr, nullptr);

        // RAG services section
        rag_services_header_ = CreateWindowExW(0, L"STATIC", L"RAG Services:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagServicesHeader), nullptr, nullptr);
        rag_services_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagServicesList), nullptr, nullptr);
        rag_enabled_check_ = CreateWindowExW(0, L"BUTTON", L"Enabled", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagEnabledCheck), nullptr, nullptr);
        rag_read_check_ = CreateWindowExW(0, L"BUTTON", L"Read", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagReadCheck), nullptr, nullptr);
        rag_write_check_ = CreateWindowExW(0, L"BUTTON", L"Write", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagWriteCheck), nullptr, nullptr);
        rag_tool_check_ = CreateWindowExW(0, L"BUTTON", L"Tool", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagToolCheck), nullptr, nullptr);
        rag_delete_check_ = CreateWindowExW(0, L"BUTTON", L"Delete", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagDeleteCheck), nullptr, nullptr);
        rag_export_check_ = CreateWindowExW(0, L"BUTTON", L"Write file", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagExportCheck), nullptr, nullptr);
        rag_default_ingest_check_ = CreateWindowExW(0, L"BUTTON", L"Default ingest", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagDefaultIngestCheck), nullptr, nullptr);
        rag_priority_label_ = CreateWindowExW(0, L"STATIC", L"Priority:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagPriorityLabel), nullptr, nullptr);
        rag_priority_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagPriorityEdit), nullptr, nullptr);
        rag_max_chunks_label_ = CreateWindowExW(0, L"STATIC", L"Max chunks:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagMaxChunksLabel), nullptr, nullptr);
        rag_max_chunks_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagMaxChunksEdit), nullptr, nullptr);
        rag_min_confidence_label_ = CreateWindowExW(0, L"STATIC", L"Min confidence:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagMinConfidenceLabel), nullptr, nullptr);
        rag_min_confidence_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagMinConfidenceEdit), nullptr, nullptr);
        rag_max_confidence_label_ = CreateWindowExW(0, L"STATIC", L"Max confidence:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagMaxConfidenceLabel), nullptr, nullptr);
        rag_max_confidence_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagMaxConfidenceEdit), nullptr, nullptr);
        rag_export_path_label_ = CreateWindowExW(0, L"STATIC", L"Write file folder:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagExportPathLabel), nullptr, nullptr);
        rag_export_path_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagExportPathEdit), nullptr, nullptr);
        rag_retrieval_mode_label_ = CreateWindowExW(0, L"STATIC", L"Retrieval mode:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagRetrievalModeLabel), nullptr, nullptr);
        rag_retrieval_mode_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagRetrievalModeCombo), nullptr, nullptr);
        ComboBox_AddString(rag_retrieval_mode_combo_, L"Both (passive + tool)");
        ComboBox_AddString(rag_retrieval_mode_combo_, L"Passive only");
        ComboBox_AddString(rag_retrieval_mode_combo_, L"Active tool only");
        ComboBox_AddString(rag_retrieval_mode_combo_, L"Disabled");
        // Project variables section
        proj_vars_header_ = CreateWindowExW(0, L"STATIC", L"Project Variables:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kProjVarsHeader), nullptr, nullptr);
        proj_vars_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kProjVarsList), nullptr, nullptr);
        proj_vars_add_btn_    = CreateWindowExW(0, L"BUTTON", L"+", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kProjVarsAdd),    nullptr, nullptr);
        proj_vars_remove_btn_ = CreateWindowExW(0, L"BUTTON", L"-", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kProjVarsRemove), nullptr, nullptr);
        proj_vars_name_label_  = CreateWindowExW(0, L"STATIC", L"Name:",  WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kProjVarsNameLabel),  nullptr, nullptr);
        proj_vars_name_edit_   = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kProjVarsNameEdit),   nullptr, nullptr);
        proj_vars_value_label_ = CreateWindowExW(0, L"STATIC", L"Value:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kProjVarsValueLabel), nullptr, nullptr);
        proj_vars_value_edit_  = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kProjVarsValueEdit),  nullptr, nullptr);

        instructions_label_ = CreateWindowExW(0, L"STATIC", L"Project Instructions:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kInstructionsLabel), nullptr, nullptr);
        import_instructions_button_ = CreateWindowExW(0, L"BUTTON", L"Import Markdown", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kImportInstructions), nullptr, nullptr);
        instructions_edit_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            Utf8ToWide(options_.project_instructions).c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            0,
            0,
            0,
            0,
            hwnd_,
            reinterpret_cast<HMENU>(kInstructionsEdit),
            nullptr,
            nullptr);

        // Footer buttons
        save_button_ = CreateWindowExW(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kSaveButton), nullptr, nullptr);
        cancel_button_ = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kCancelButton), nullptr, nullptr);

        // Apply font to all controls
        HWND all_controls[] = {
            server_list_, add_server_button_, remove_server_button_,
            server_details_panel_, server_enabled_check_, server_name_label_, server_scope_label_,
            model_tools_header_, model_tools_list_,
            variables_header_,
            model_label_, model_combo_,
            context_window_label_, context_window_combo_,
            rag_services_header_, rag_services_list_, rag_enabled_check_, rag_read_check_, rag_write_check_, rag_tool_check_,
            rag_delete_check_, rag_export_check_, rag_default_ingest_check_,
            rag_priority_label_, rag_priority_edit_, rag_max_chunks_label_, rag_max_chunks_edit_,
            rag_min_confidence_label_, rag_min_confidence_edit_, rag_max_confidence_label_, rag_max_confidence_edit_,
            rag_export_path_label_, rag_export_path_edit_,
            rag_retrieval_mode_label_, rag_retrieval_mode_combo_,
            proj_vars_header_, proj_vars_list_, proj_vars_add_btn_, proj_vars_remove_btn_,
            proj_vars_name_label_, proj_vars_name_edit_, proj_vars_value_label_, proj_vars_value_edit_,
            instructions_label_, import_instructions_button_, instructions_edit_,
            save_button_, cancel_button_
        };
        for (HWND ctrl : all_controls) {
            SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }

        // Initial layout pass so the server details panel is positioned before SelectServer
        // tries to position variable controls relative to it.
        LayoutControls(1050, 750);

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

        // Right panel
        const int right_x = margin + left_width + gutter * 2;
        const int right_width = width - right_x - margin;

        // Model selection section (top of right panel)
        y = margin;
        MoveWindow(model_label_, right_x, y, right_width, label_height, TRUE);
        y += label_height + gutter;
        MoveWindow(model_combo_, right_x, y, right_width, Scale(hwnd_, 250), TRUE);
        y += Scale(hwnd_, 28) + gutter * 2;

        // Context window section
        MoveWindow(context_window_label_, right_x, y, right_width, label_height, TRUE);
        y += label_height + gutter;
        MoveWindow(context_window_combo_, right_x, y, right_width, Scale(hwnd_, 200), TRUE);

        // RAG services section
        y += Scale(hwnd_, 28) + gutter * 2;
        MoveWindow(rag_services_header_, right_x, y, right_width, label_height, TRUE);
        y += label_height + gutter;
        const int rag_list_height = Scale(hwnd_, 100);
        MoveWindow(rag_services_list_, right_x, y, right_width, rag_list_height, TRUE);
        y += rag_list_height + gutter;
        // RAG permission checkboxes - positioned below the list
        const int checkbox_width = Scale(hwnd_, 70);
        const int checkbox_gap = Scale(hwnd_, 15);
        MoveWindow(rag_enabled_check_, right_x, y, checkbox_width, Scale(hwnd_, 20), TRUE);
        MoveWindow(rag_read_check_, right_x + checkbox_width + checkbox_gap, y, checkbox_width, Scale(hwnd_, 20), TRUE);
        MoveWindow(rag_write_check_, right_x + (checkbox_width + checkbox_gap) * 2, y, checkbox_width, Scale(hwnd_, 20), TRUE);
        MoveWindow(rag_tool_check_, right_x + (checkbox_width + checkbox_gap) * 3, y, checkbox_width, Scale(hwnd_, 20), TRUE);
        MoveWindow(rag_export_check_, right_x + (checkbox_width + checkbox_gap) * 4, y, Scale(hwnd_, 95), Scale(hwnd_, 20), TRUE);
        y += Scale(hwnd_, 24);
        MoveWindow(rag_delete_check_, right_x, y, checkbox_width, Scale(hwnd_, 20), TRUE);
        MoveWindow(rag_default_ingest_check_, right_x + checkbox_width + checkbox_gap, y, Scale(hwnd_, 130), Scale(hwnd_, 20), TRUE);
        y += Scale(hwnd_, 24) + gutter;

        const int path_label_width = Scale(hwnd_, 115);
        MoveWindow(rag_export_path_label_, right_x, y + Scale(hwnd_, 3), path_label_width, label_height, TRUE);
        MoveWindow(rag_export_path_edit_, right_x + path_label_width, y, right_width - path_label_width, Scale(hwnd_, 22), TRUE);
        y += Scale(hwnd_, 24) + gutter;

        const int numeric_label_width = Scale(hwnd_, 105);
        const int numeric_edit_width = Scale(hwnd_, 70);
        const int numeric_row_height = Scale(hwnd_, 24);
        const int numeric_pair_width = numeric_label_width + numeric_edit_width + gutter * 2;
        MoveWindow(rag_priority_label_, right_x, y + Scale(hwnd_, 3), numeric_label_width, label_height, TRUE);
        MoveWindow(rag_priority_edit_, right_x + numeric_label_width, y, numeric_edit_width, Scale(hwnd_, 22), TRUE);
        MoveWindow(rag_max_chunks_label_, right_x + numeric_pair_width, y + Scale(hwnd_, 3), numeric_label_width, label_height, TRUE);
        MoveWindow(rag_max_chunks_edit_, right_x + numeric_pair_width + numeric_label_width, y, numeric_edit_width, Scale(hwnd_, 22), TRUE);
        y += numeric_row_height + gutter;
        MoveWindow(rag_min_confidence_label_, right_x, y + Scale(hwnd_, 3), numeric_label_width, label_height, TRUE);
        MoveWindow(rag_min_confidence_edit_, right_x + numeric_label_width, y, numeric_edit_width, Scale(hwnd_, 22), TRUE);
        MoveWindow(rag_max_confidence_label_, right_x + numeric_pair_width, y + Scale(hwnd_, 3), numeric_label_width, label_height, TRUE);
        MoveWindow(rag_max_confidence_edit_, right_x + numeric_pair_width + numeric_label_width, y, numeric_edit_width, Scale(hwnd_, 22), TRUE);
        y += numeric_row_height + gutter;
        MoveWindow(rag_retrieval_mode_label_, right_x, y + Scale(hwnd_, 3), numeric_label_width, label_height, TRUE);
        MoveWindow(rag_retrieval_mode_combo_, right_x + numeric_label_width, y, Scale(hwnd_, 180), Scale(hwnd_, 200), TRUE);
        y += numeric_row_height + gutter;

        // Project variables section
        {
            const int pv_btn_w   = Scale(hwnd_, 30);
            const int pv_list_h  = Scale(hwnd_, 70);
            const int pv_lbl_w   = Scale(hwnd_, 50);
            const int pv_edit_h  = Scale(hwnd_, 22);
            const int pv_row_h   = pv_edit_h + Scale(hwnd_, 4);
            const int list_w     = right_width - pv_btn_w - gutter;

            MoveWindow(proj_vars_header_, right_x, y, right_width, label_height, TRUE);
            y += label_height + gutter;

            MoveWindow(proj_vars_list_,       right_x,                y,                  list_w,         pv_list_h,  TRUE);
            MoveWindow(proj_vars_add_btn_,    right_x + list_w + gutter, y,               pv_btn_w,       button_height, TRUE);
            MoveWindow(proj_vars_remove_btn_, right_x + list_w + gutter, y + button_height + gutter, pv_btn_w, button_height, TRUE);
            y += pv_list_h + gutter;

            MoveWindow(proj_vars_name_label_,  right_x,            y + Scale(hwnd_, 3), pv_lbl_w,              label_height, TRUE);
            MoveWindow(proj_vars_name_edit_,   right_x + pv_lbl_w, y,                  right_width - pv_lbl_w, pv_edit_h,    TRUE);
            y += pv_row_h + gutter;

            MoveWindow(proj_vars_value_label_, right_x,            y + Scale(hwnd_, 3), pv_lbl_w,              label_height, TRUE);
            MoveWindow(proj_vars_value_edit_,  right_x + pv_lbl_w, y,                  right_width - pv_lbl_w, pv_edit_h,    TRUE);
            y += pv_row_h + gutter;
        }

        const int import_width = Scale(hwnd_, 130);
        MoveWindow(instructions_label_, right_x, y + Scale(hwnd_, 5), right_width - import_width - gutter, label_height, TRUE);
        MoveWindow(import_instructions_button_, right_x + right_width - import_width, y, import_width, button_height, TRUE);
        y += button_height + gutter;
        MoveWindow(instructions_edit_, right_x, y, right_width, std::max(Scale(hwnd_, 120), buttons_y - y - gutter), TRUE);

        // Footer buttons
        MoveWindow(cancel_button_, width - margin - button_width, buttons_y, button_width, button_height, TRUE);
        MoveWindow(save_button_, width - margin - button_width * 2 - gutter, buttons_y, button_width, button_height, TRUE);
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
        case kImportInstructions:
            ImportInstructions();
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

    void OnCompressionConfigChanged() {
        int sel = ComboBox_GetCurSel(context_window_combo_);
        if (sel <= 0) {
            selected_compression_config_id_.clear();
        } else if ((sel - 1) < static_cast<int>(options_.compression_configs.size())) {
            selected_compression_config_id_ = options_.compression_configs[sel - 1].id;
        }
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
        } else {
            SetWindowTextW(proj_vars_name_edit_,  L"");
            SetWindowTextW(proj_vars_value_edit_, L"");
        }
        updating_proj_var_ = false;
        EnableWindow(proj_vars_name_edit_,  has_sel ? TRUE : FALSE);
        EnableWindow(proj_vars_value_edit_, has_sel ? TRUE : FALSE);
        EnableWindow(proj_vars_remove_btn_, has_sel ? TRUE : FALSE);
    }

    // Flush current name/value edits back into project_variables_[selected_proj_var_index_].
    void SaveCurrentProjVar() {
        if (selected_proj_var_index_ < 0 ||
            selected_proj_var_index_ >= static_cast<int>(project_variables_.size())) return;
        auto& pv = project_variables_[selected_proj_var_index_];
        pv.name  = WideToUtf8(GetWindowTextString(proj_vars_name_edit_));
        pv.value = WideToUtf8(GetWindowTextString(proj_vars_value_edit_));
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
        // Refresh label in-place (delete+reinsert keeps selection).
        updating_proj_var_ = true;
        std::wstring text = Utf8ToWide(pv.name.empty() ? std::string("(unnamed)") : pv.name);
        text += L" = ";
        text += Utf8ToWide(pv.value);
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

    void SaveAndClose() {
        // Flush any unsaved variable edits from the currently-visible server.
        SaveCurrentVariableValues();

        // Flush any in-progress project variable edits.
        SaveCurrentProjVar();

        SaveSelectedRagControls();

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

        // Build MCP bindings
        std::vector<ProjectMcpServerBinding> mcp_bindings;
        for (size_t i = 0; i < states_.size(); ++i) {
            if (states_[i].selected) {
                ProjectMcpServerBinding binding;
                binding.server_id = options_.servers[i].id;
                binding.variables = states_[i].variables;
                mcp_bindings.push_back(binding);
            }
        }

        // Build RAG bindings
        std::vector<ProjectRagBinding> rag_bindings;
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
                rag_bindings.push_back(binding);
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

        result_ = ProjectSettingsResult{};
        result_->project_name = WideToUtf8(options_.project_name);
        result_->project_instructions = WideToUtf8(GetWindowTextString(instructions_edit_));
        result_->mcp_bindings = std::move(mcp_bindings);
        result_->selected_compression_config_id = selected_compression_config_id_;
        result_->rag_bindings = std::move(rag_bindings);
        result_->preferred_provider_id = preferred_provider_id;
        result_->preferred_model_id = preferred_model_id;
        result_->model_tool_ids = std::move(model_tool_ids);
        result_->project_variables = project_variables_;

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
    HWND instructions_label_ = nullptr;
    HWND instructions_edit_ = nullptr;
    HWND import_instructions_button_ = nullptr;
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
};

}  // namespace

std::optional<ProjectSettingsResult> ShowProjectSettingsDialog(HWND owner, const ProjectSettingsOptions& options) {
    return ProjectSettingsDialog::Show(owner, options);
}
