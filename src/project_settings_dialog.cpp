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

    // Right panel top - context window
    kContextWindowLabel = 6410,
    kContextWindowCombo = 6411,
    kAddCompressionConfig = 6412,
    kDeleteCompressionConfig = 6413,
    kEditCompressionConfig = 6414,

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
    kInstructionsLabel = 6440,
    kInstructionsEdit = 6441,
    kImportInstructions = 6442,

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
            }
            rag_rows_.push_back(row);
        }

        // Selected compression config (loaded from global configs via options)
        selected_compression_config_id_ = options_.selected_compression_config_id;
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
        add_server_button_ = CreateWindowExW(0, L"BUTTON", L"+", WS_CHILD | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kAddServer), nullptr, nullptr);
        remove_server_button_ = CreateWindowExW(0, L"BUTTON", L"-", WS_CHILD | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRemoveServer), nullptr, nullptr);

        // Server details panel
        server_details_panel_ = CreateWindowExW(0, L"BUTTON", nullptr, WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kServerDetailsPanel), nullptr, nullptr);
        server_enabled_check_ = CreateWindowExW(0, L"BUTTON", L"Use this MCP server", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kServerEnabled), nullptr, nullptr);
        server_name_label_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kServerNameLabel), nullptr, nullptr);
        server_scope_label_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kServerScopeLabel), nullptr, nullptr);
        variables_header_ = CreateWindowExW(0, L"STATIC", L"Variables:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kVariablesHeader), nullptr, nullptr);

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
            variables_header_,
            context_window_label_, context_window_combo_,
            rag_services_header_, rag_services_list_, rag_enabled_check_, rag_read_check_, rag_write_check_, rag_tool_check_,
            rag_delete_check_, rag_export_check_, rag_default_ingest_check_,
            rag_priority_label_, rag_priority_edit_, rag_max_chunks_label_, rag_max_chunks_edit_,
            rag_min_confidence_label_, rag_min_confidence_edit_, rag_max_confidence_label_, rag_max_confidence_edit_,
            rag_export_path_label_, rag_export_path_edit_,
            instructions_label_, import_instructions_button_, instructions_edit_,
            save_button_, cancel_button_
        };
        for (HWND ctrl : all_controls) {
            SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }

        // Populate server list
        RefreshServerList();
        SelectServer(0);

        // Populate compression config dropdown
        RefreshCompressionCombo();

        // Populate RAG services list
        RefreshRagList();
        if (!rag_rows_.empty()) {
            ListBox_SetCurSel(rag_services_list_, 0);
            OnRagSelectionChanged();
        }

        LayoutControls(1050, 750);
    }

    void LayoutControls(int width, int height) const {
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
        MoveWindow(server_list_, margin, y, left_width, Scale(hwnd_, 200), TRUE);

        // Server details panel
        y += Scale(hwnd_, 205);
        MoveWindow(server_details_panel_, margin, y, left_width, height - y - margin - button_height - gutter, TRUE);
        const int detail_x = margin + gutter;
        const int detail_w = left_width - gutter * 2;
        MoveWindow(server_enabled_check_, detail_x, y + Scale(hwnd_, 18), detail_w, Scale(hwnd_, 22), TRUE);
        MoveWindow(server_name_label_, detail_x, y + Scale(hwnd_, 45), detail_w, label_height, TRUE);
        MoveWindow(server_scope_label_, detail_x, y + Scale(hwnd_, 65), detail_w, label_height, TRUE);
        MoveWindow(variables_header_, detail_x, y + Scale(hwnd_, 90), detail_w, label_height, TRUE);

        // Right panel
        const int right_x = margin + left_width + gutter * 2;
        const int right_width = width - right_x - margin;

        // Context window section
        y = margin;
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
        y += numeric_row_height + gutter * 2;

        const int import_width = Scale(hwnd_, 130);
        MoveWindow(instructions_label_, right_x, y + Scale(hwnd_, 5), right_width - import_width - gutter, label_height, TRUE);
        MoveWindow(import_instructions_button_, right_x + right_width - import_width, y, import_width, button_height, TRUE);
        y += button_height + gutter;
        MoveWindow(instructions_edit_, right_x, y, right_width, std::max(Scale(hwnd_, 120), buttons_y - y - gutter), TRUE);

        // Footer buttons
        MoveWindow(cancel_button_, width - margin - button_width, buttons_y, button_width, button_height, TRUE);
        MoveWindow(save_button_, width - margin - button_width * 2 - gutter, buttons_y, button_width, button_height, TRUE);
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
        case kImportInstructions:
            ImportInstructions();
            break;
        case kSaveButton:
            SaveAndClose();
            break;
        case kCancelButton:
            DestroyWindow(hwnd_);
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
        selected_server_index_ = index;
        bool has_selection = (index >= 0 && index < static_cast<int>(states_.size()));

        ShowWindow(server_details_panel_, has_selection ? SW_SHOW : SW_HIDE);
        ShowWindow(server_enabled_check_, has_selection ? SW_SHOW : SW_HIDE);
        ShowWindow(server_name_label_, has_selection ? SW_SHOW : SW_HIDE);
        ShowWindow(server_scope_label_, has_selection ? SW_SHOW : SW_HIDE);
        ShowWindow(variables_header_, has_selection ? SW_SHOW : SW_HIDE);

        if (!has_selection) {
            return;
        }

        const auto& server = options_.servers[index];
        const auto& state = states_[index];

        Button_SetCheck(server_enabled_check_, state.selected ? BST_CHECKED : BST_UNCHECKED);
        SetWindowTextW(server_name_label_, Utf8ToWide(server.name).c_str());
        std::wstring scope = (server.scope == McpServerScope::Shared) ? L"Scope: Shared process" : L"Scope: Per-project process";
        SetWindowTextW(server_scope_label_, scope.c_str());

        // Clear existing variable controls
        for (auto& vc : variable_controls_) {
            if (vc.label) DestroyWindow(vc.label);
            if (vc.edit) DestroyWindow(vc.edit);
            if (vc.browse) DestroyWindow(vc.browse);
        }
        variable_controls_.clear();

        // Create variable controls
        RECT panel_rect;
        GetWindowRect(server_details_panel_, &panel_rect);
        int panel_x = Scale(hwnd_, 18);
        int panel_y = Scale(hwnd_, 115);
        int panel_w = Scale(hwnd_, 280) - Scale(hwnd_, 36);

        for (const auto& variable : server.variables) {
            bool is_global = std::find_if(options_.global_variables.begin(), options_.global_variables.end(),
                [&](const McpServerVariable& v) { return v.name == variable.name; }) != options_.global_variables.end();

            HWND lbl = CreateWindowExW(0, L"STATIC", Utf8ToWide(variable.name).c_str(),
                WS_CHILD | WS_VISIBLE, panel_x, panel_y, panel_w, Scale(hwnd_, 18), hwnd_, nullptr, nullptr, nullptr);
            SendMessageW(lbl, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);

            HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP, panel_x, panel_y + Scale(hwnd_, 20), panel_w - (variable.kind != McpVariableKind::None ? Scale(hwnd_, 30) : 0), Scale(hwnd_, 22), hwnd_, nullptr, nullptr, nullptr);
            SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);

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

            HWND browse = nullptr;
            if (variable.kind != McpVariableKind::None) {
                browse = CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    panel_x + panel_w - Scale(hwnd_, 30), panel_y + Scale(hwnd_, 20), Scale(hwnd_, 28), Scale(hwnd_, 22), hwnd_, nullptr, nullptr, nullptr);
                SendMessageW(browse, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
            }

            variable_controls_.push_back({variable.name, variable.kind, is_global, lbl, edit, browse});
            panel_y += Scale(hwnd_, 48);
        }
    }

    void OnServerEnabledChanged() {
        if (selected_server_index_ < 0) return;
        bool enabled = (Button_GetCheck(server_enabled_check_) == BST_CHECKED);
        states_[selected_server_index_].selected = enabled;
        RefreshServerList();
        ListBox_SetCurSel(server_list_, selected_server_index_);
    }

    void AddServer() {
        // Not implemented for now - servers are managed via MCP Servers button
    }

    void RemoveServer() {
        // Not implemented for now
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

    void SetRagControlsEnabled(bool enabled) {
        HWND controls[] = {
            rag_enabled_check_, rag_read_check_, rag_write_check_, rag_tool_check_, rag_delete_check_, rag_export_check_,
            rag_default_ingest_check_, rag_priority_edit_, rag_max_chunks_edit_, rag_min_confidence_edit_, rag_max_confidence_edit_,
            rag_export_path_edit_
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

        NormalizeRagPermissionDependencies(row);
        EnsureSingleDefaultIngestTarget(selected_rag_index_);
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
        SaveSelectedRagControls();

        // Ensure selected_compression_config_id is current from combo
        int sel = ComboBox_GetCurSel(context_window_combo_);
        if (sel <= 0) {
            selected_compression_config_id_.clear();
        } else if ((sel - 1) < static_cast<int>(options_.compression_configs.size())) {
            selected_compression_config_id_ = options_.compression_configs[sel - 1].id;
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
                rag_bindings.push_back(binding);
            }
        }

        result_ = ProjectSettingsResult{};
        result_->project_name = WideToUtf8(options_.project_name);
        result_->project_instructions = WideToUtf8(GetWindowTextString(instructions_edit_));
        result_->mcp_bindings = std::move(mcp_bindings);
        result_->selected_compression_config_id = selected_compression_config_id_;
        result_->rag_bindings = std::move(rag_bindings);

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

    // Compression configs
    std::string selected_compression_config_id_;

    // RAG rows
    std::vector<RagRow> rag_rows_;
    int selected_rag_index_ = -1;

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
    HWND instructions_label_ = nullptr;
    HWND instructions_edit_ = nullptr;
    HWND import_instructions_button_ = nullptr;
    HWND save_button_ = nullptr;
    HWND cancel_button_ = nullptr;
};

}  // namespace

std::optional<ProjectSettingsResult> ShowProjectSettingsDialog(HWND owner, const ProjectSettingsOptions& options) {
    return ProjectSettingsDialog::Show(owner, options);
}
