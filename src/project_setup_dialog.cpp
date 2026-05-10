#include "project_setup_dialog.h"

#include "util.h"

#include <commdlg.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace {
constexpr wchar_t kProjectSetupClassName[] = L"AgentProjectSetupDialog";

enum ControlId : int {
    kProjectNameLabel = 5001,
    kProjectNameEdit = 5002,
    kServersLabel = 5003,
    kServersList = 5004,
    kSelectedCheckbox = 5005,
    kScopeLabel = 5006,
    kDetailsLabel = 5007,
    kHintLabel = 5008,
    kNoVariablesLabel = 5009,
    kAcceptButton = IDOK,
    kCancelButton = IDCANCEL,
    kBrowseButtonBase = 5200,
};

std::wstring ScopeLabel(McpServerScope scope) {
    return scope == McpServerScope::Shared ? L"Shared process" : L"Per-project process";
}

std::wstring VariableKindLabel(McpVariableKind kind) {
    switch (kind) {
    case McpVariableKind::Folder:
        return L"Folder";
    case McpVariableKind::File:
        return L"File";
    case McpVariableKind::None:
    default:
        return L"Text";
    }
}

bool TextUsesVariable(const std::string& text, const std::string& name) {
    return text.find("$" + name + "$") != std::string::npos ||
           text.find("$<" + name + ">$") != std::string::npos;
}

bool ConfigUsesVariable(const McpServerConfig& config, const std::string& name) {
    if (TextUsesVariable(config.command, name) || TextUsesVariable(config.working_directory, name)) {
        return true;
    }
    for (const auto& argument : config.arguments) {
        if (TextUsesVariable(argument, name)) {
            return true;
        }
    }
    for (const auto& entry : config.env_entries) {
        if (TextUsesVariable(entry, name)) {
            return true;
        }
    }
    return false;
}

std::optional<std::wstring> BrowseForFolder(HWND owner, const std::wstring& title) {
    BROWSEINFOW info{};
    info.hwndOwner = owner;
    info.lpszTitle = title.c_str();
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;

    LPITEMIDLIST selection = SHBrowseForFolderW(&info);
    if (!selection) {
        return std::nullopt;
    }

    std::wstring result;
    wchar_t path[MAX_PATH] = {};
    if (SHGetPathFromIDListW(selection, path)) {
        result = path;
    }
    CoTaskMemFree(selection);

    if (result.empty()) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::wstring> BrowseForFile(HWND owner, const std::wstring& title) {
    wchar_t buffer[MAX_PATH] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrTitle = title.c_str();
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = static_cast<DWORD>(std::size(buffer));
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
    dialog.lpstrFilter = L"All Files\0*.*\0";

    if (!GetOpenFileNameW(&dialog)) {
        return std::nullopt;
    }
    return std::wstring(buffer);
}

class ProjectSetupDialog {
public:
    static std::optional<ProjectSetupDialogResult> Show(HWND owner, const ProjectSetupDialogOptions& options) {
        HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
        RegisterWindowClass(instance);

        auto* dialog = new ProjectSetupDialog(owner, options);
        const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE | WS_THICKFRAME;
        const DWORD ex_style = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;

        if (owner) {
            EnableWindow(owner, FALSE);
        }

        dialog->hwnd_ = CreateWindowExW(
            ex_style,
            kProjectSetupClassName,
            options.title.empty() ? L"Project Setup" : options.title.c_str(),
            style,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            980,
            720,
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
        delete dialog;
        return result;
    }

private:
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

    ProjectSetupDialog(HWND owner, ProjectSetupDialogOptions options) : owner_(owner), options_(std::move(options)) {
        states_.reserve(options_.servers.size());
        for (const auto& server : options_.servers) {
            ServerState state;
            state.server_id = server.id;
            const auto binding_it = std::find_if(options_.initial_bindings.begin(), options_.initial_bindings.end(), [&](const ProjectMcpServerBinding& binding) {
                return binding.server_id == server.id;
            });
            if (binding_it != options_.initial_bindings.end()) {
                state.selected = true;
                state.variables = binding_it->variables;
            }
            states_.push_back(std::move(state));
        }
        for (const auto& binding : options_.initial_bindings) {
            for (const auto& value : binding.variables) {
                const auto global_it = std::find_if(options_.global_variables.begin(), options_.global_variables.end(), [&](const McpServerVariable& variable) {
                    return variable.name == value.name;
                });
                if (global_it == options_.global_variables.end()) {
                    continue;
                }
                const auto existing = std::find_if(global_values_.begin(), global_values_.end(), [&](const ProjectMcpVariableValue& item) {
                    return item.name == value.name;
                });
                if (existing == global_values_.end()) {
                    global_values_.push_back(value);
                }
            }
        }
    }

    static int Scale(HWND hwnd, int value) {
        return MulDiv(value, GetDpiForWindow(hwnd), 96);
    }

    static void RegisterWindowClass(HINSTANCE instance) {
        static bool registered = false;
        if (registered) {
            return;
        }

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = &ProjectSetupDialog::WindowProc;
        wc.lpszClassName = kProjectSetupClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        RegisterClassExW(&wc);
        registered = true;
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
        auto* self = reinterpret_cast<ProjectSetupDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
            self = reinterpret_cast<ProjectSetupDialog*>(create->lpCreateParams);
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
        case WM_GETMINMAXINFO:
            self->OnGetMinMaxInfo(reinterpret_cast<MINMAXINFO*>(l_param));
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
        default:
            return DefWindowProcW(hwnd, message, w_param, l_param);
        }
    }

    void OnCreate() {
        font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        project_name_label_ = CreateWindowExW(0, L"STATIC", L"Project Name", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kProjectNameLabel), nullptr, nullptr);
        project_name_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", options_.project_name.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kProjectNameEdit), nullptr, nullptr);
        servers_label_ = CreateWindowExW(0, L"STATIC", L"MCP Servers", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kServersLabel), nullptr, nullptr);
        servers_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kServersList), nullptr, nullptr);
        details_label_ = CreateWindowExW(0, L"STATIC", L"Server Details", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kDetailsLabel), nullptr, nullptr);
        selected_checkbox_ = CreateWindowExW(0, L"BUTTON", L"Use this MCP server in the project", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kSelectedCheckbox), nullptr, nullptr);
        scope_label_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kScopeLabel), nullptr, nullptr);
        hint_label_ = CreateWindowExW(0, L"STATIC", L"Select a server on the left. Folder and file variables include a browse button.", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kHintLabel), nullptr, nullptr);
        no_variables_label_ = CreateWindowExW(0, L"STATIC", L"This server has no project variables.", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kNoVariablesLabel), nullptr, nullptr);
        accept_button_ = CreateWindowExW(0, L"BUTTON", options_.accept_label.empty() ? L"Save" : options_.accept_label.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kAcceptButton), nullptr, nullptr);
        cancel_button_ = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kCancelButton), nullptr, nullptr);

        for (HWND control : {project_name_label_, project_name_edit_, servers_label_, servers_list_, details_label_, selected_checkbox_, scope_label_, hint_label_, no_variables_label_, accept_button_, cancel_button_}) {
            if (control) {
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
            }
        }

        if (!options_.include_project_name) {
            ShowWindow(project_name_label_, SW_HIDE);
            ShowWindow(project_name_edit_, SW_HIDE);
        }

        RefreshServerList();
        if (!options_.servers.empty()) {
            SendMessageW(servers_list_, LB_SETCURSEL, 0, 0);
        }
        RebuildDetails();
        CenterWindowToOwner(hwnd_, owner_);
        SetFocus(options_.include_project_name ? project_name_edit_ : servers_list_);
    }

    void OnGetMinMaxInfo(MINMAXINFO* info) const {
        if (!info) {
            return;
        }
        info->ptMinTrackSize.x = Scale(hwnd_, 900);
        info->ptMinTrackSize.y = Scale(hwnd_, 640);
    }

    void LayoutControls(int width, int height) {
        const int margin = Scale(hwnd_, 12);
        const int gutter = Scale(hwnd_, 12);
        const int label_height = Scale(hwnd_, 18);
        const int edit_height = Scale(hwnd_, 24);
        const int checkbox_height = Scale(hwnd_, 22);
        const int button_height = Scale(hwnd_, 30);
        const int browse_width = Scale(hwnd_, 86);
        const int left_width = std::max(Scale(hwnd_, 300), width / 3);
        const int footer_y = height - margin - button_height;

        int top = margin;
        if (options_.include_project_name) {
            MoveWindow(project_name_label_, margin, top, width - margin * 2, label_height, TRUE);
            top += label_height + Scale(hwnd_, 4);
            MoveWindow(project_name_edit_, margin, top, width - margin * 2, edit_height, TRUE);
            top += edit_height + gutter;
        }

        MoveWindow(servers_label_, margin, top, left_width, label_height, TRUE);
        MoveWindow(details_label_, margin + left_width + gutter, top, width - margin * 2 - left_width - gutter, label_height, TRUE);
        top += label_height + Scale(hwnd_, 4);

        const int content_height = footer_y - top - gutter;
        MoveWindow(servers_list_, margin, top, left_width, content_height, TRUE);

        const int right_x = margin + left_width + gutter;
        const int right_width = width - margin - right_x;
        int right_y = top;

        MoveWindow(selected_checkbox_, right_x, right_y, right_width, checkbox_height, TRUE);
        right_y += checkbox_height + Scale(hwnd_, 6);
        MoveWindow(scope_label_, right_x, right_y, right_width, label_height, TRUE);
        right_y += label_height + Scale(hwnd_, 4);
        MoveWindow(hint_label_, right_x, right_y, right_width, label_height * 2, TRUE);
        right_y += label_height * 2 + gutter;

        if (!no_variables_label_) {
            return;
        }

        if (variable_controls_.empty()) {
            MoveWindow(no_variables_label_, right_x, right_y, right_width, label_height, TRUE);
        } else {
            MoveWindow(no_variables_label_, right_x, right_y, 0, 0, TRUE);
            const int row_spacing = Scale(hwnd_, 8);
            const int control_gap = Scale(hwnd_, 4);
            const int field_width = right_width - browse_width - gutter;
            int variable_y = right_y;
            for (auto& control : variable_controls_) {
                MoveWindow(control.label, right_x, variable_y, right_width, label_height, TRUE);
                variable_y += label_height + control_gap;
                if (control.browse) {
                    MoveWindow(control.edit, right_x, variable_y, field_width, edit_height, TRUE);
                    MoveWindow(control.browse, right_x + field_width + gutter, variable_y, browse_width, edit_height, TRUE);
                } else {
                    MoveWindow(control.edit, right_x, variable_y, right_width, edit_height, TRUE);
                }
                variable_y += edit_height + row_spacing;
            }
        }

        const int button_width = Scale(hwnd_, 100);
        MoveWindow(cancel_button_, width - margin - button_width, footer_y, button_width, button_height, TRUE);
        MoveWindow(accept_button_, width - margin - button_width * 2 - gutter, footer_y, button_width, button_height, TRUE);
    }

    void OnCommand(int control_id, int notification_code) {
        if (control_id >= kBrowseButtonBase && control_id < kBrowseButtonBase + 256) {
            OnBrowse(control_id - kBrowseButtonBase);
            return;
        }

        switch (control_id) {
        case kServersList:
            if (notification_code == LBN_SELCHANGE) {
                CaptureCurrentVariableValues();
                RebuildDetails();
            }
            break;
        case kSelectedCheckbox:
            UpdateSelectionFlag();
            break;
        case kAcceptButton:
            ValidateAndSave();
            break;
        case kCancelButton:
            DestroyWindow(hwnd_);
            break;
        default:
            break;
        }
    }

    const McpServerConfig* SelectedConfig() const {
        const int index = SelectedIndex();
        if (index < 0 || static_cast<size_t>(index) >= options_.servers.size()) {
            return nullptr;
        }
        return &options_.servers[static_cast<size_t>(index)];
    }

    ServerState* SelectedState() {
        const McpServerConfig* config = SelectedConfig();
        if (!config) {
            return nullptr;
        }
        return FindState(config->id);
    }

    int SelectedIndex() const {
        const LRESULT selection = SendMessageW(servers_list_, LB_GETCURSEL, 0, 0);
        return selection == LB_ERR ? -1 : static_cast<int>(selection);
    }

    ServerState* FindState(const std::string& server_id) {
        auto it = std::find_if(states_.begin(), states_.end(), [&](const ServerState& state) { return state.server_id == server_id; });
        return it == states_.end() ? nullptr : &(*it);
    }

    const ProjectMcpVariableValue* FindValue(const ServerState& state, const std::string& name) const {
        const auto it = std::find_if(state.variables.begin(), state.variables.end(), [&](const ProjectMcpVariableValue& value) { return value.name == name; });
        return it == state.variables.end() ? nullptr : &(*it);
    }

    void SetValue(ServerState& state, const std::string& name, const std::string& value) {
        auto it = std::find_if(state.variables.begin(), state.variables.end(), [&](const ProjectMcpVariableValue& item) { return item.name == name; });
        if (it == state.variables.end()) {
            ProjectMcpVariableValue item;
            item.name = name;
            item.value = value;
            state.variables.push_back(std::move(item));
        } else {
            it->value = value;
        }
    }

    const ProjectMcpVariableValue* FindGlobalValue(const std::string& name) const {
        const auto it = std::find_if(global_values_.begin(), global_values_.end(), [&](const ProjectMcpVariableValue& value) { return value.name == name; });
        return it == global_values_.end() ? nullptr : &(*it);
    }

    void SetGlobalValue(const std::string& name, const std::string& value) {
        auto it = std::find_if(global_values_.begin(), global_values_.end(), [&](const ProjectMcpVariableValue& item) { return item.name == name; });
        if (it == global_values_.end()) {
            ProjectMcpVariableValue item;
            item.name = name;
            item.value = value;
            global_values_.push_back(std::move(item));
        } else {
            it->value = value;
        }
    }

    std::vector<McpServerVariable> GlobalVariablesUsedBy(const McpServerConfig& config) const {
        std::vector<McpServerVariable> variables;
        for (const auto& variable : options_.global_variables) {
            if (!variable.name.empty() && ConfigUsesVariable(config, variable.name)) {
                variables.push_back(variable);
            }
        }
        return variables;
    }

    void RefreshServerList() {
        const int selected_index = SelectedIndex();
        SendMessageW(servers_list_, LB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < options_.servers.size(); ++i) {
            const auto& server = options_.servers[i];
            const auto& state = states_[i];
            std::wstring label = Utf8ToWide(server.name);
            label += L"  [";
            label += ScopeLabel(server.scope);
            label += L"]";
            label += state.selected ? L"  [selected]" : L"  [not selected]";
            SendMessageW(servers_list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }

        if (!options_.servers.empty()) {
            const int restore = selected_index >= 0 && static_cast<size_t>(selected_index) < options_.servers.size() ? selected_index : 0;
            SendMessageW(servers_list_, LB_SETCURSEL, restore, 0);
        }
    }

    void DestroyVariableControls() {
        for (auto& control : variable_controls_) {
            if (control.label) {
                DestroyWindow(control.label);
            }
            if (control.edit) {
                DestroyWindow(control.edit);
            }
            if (control.browse) {
                DestroyWindow(control.browse);
            }
        }
        variable_controls_.clear();
    }

    void RebuildDetails() {
        DestroyVariableControls();

        const McpServerConfig* config = SelectedConfig();
        ServerState* state = SelectedState();
        if (!config || !state) {
            SetWindowTextW(details_label_, L"Server Details");
            SetWindowTextW(scope_label_, L"");
            SetWindowTextW(hint_label_, L"Select a server on the left.");
            ShowWindow(selected_checkbox_, SW_HIDE);
            ShowWindow(no_variables_label_, SW_SHOW);
            SetWindowTextW(no_variables_label_, L"No MCP server selected.");
            LayoutNow();
            return;
        }

        ShowWindow(selected_checkbox_, SW_SHOW);
        SetWindowTextW(details_label_, Utf8ToWide(config->name).c_str());
        Button_SetCheck(selected_checkbox_, state->selected ? BST_CHECKED : BST_UNCHECKED);

        const auto global_variables = GlobalVariablesUsedBy(*config);
        std::wstring scope_text = L"Scope: " + ScopeLabel(config->scope);
        const size_t variable_count = config->variables.size() + global_variables.size();
        if (variable_count == 0) {
            scope_text += L"  |  No project variables";
        } else {
            scope_text += L"  |  " + std::to_wstring(variable_count) + L" project variable(s)";
        }
        SetWindowTextW(scope_label_, scope_text.c_str());
        SetWindowTextW(hint_label_, L"Shared variables are defined globally and only need one value per project. Placeholders use the $<Name>$ form.");

        if (variable_count == 0) {
            ShowWindow(no_variables_label_, SW_SHOW);
            SetWindowTextW(no_variables_label_, L"This server has no project variables. Turn it on if you want it available in this project.");
            LayoutNow();
            return;
        }

        ShowWindow(no_variables_label_, SW_HIDE);
        auto add_variable_control = [&](const McpServerVariable& variable, bool global) {
            VariableControl control;
            control.name = variable.name;
            control.kind = variable.kind;
            control.global = global;

            std::wstring label = Utf8ToWide(variable.name);
            label += L" [";
            label += VariableKindLabel(variable.kind);
            label += L"]";
            if (global) {
                label += L" [shared]";
            }
            if (variable.inject_into_context) {
                label += L" [context]";
            }
            if (!variable.description.empty()) {
                label += L" - " + Utf8ToWide(variable.description);
            }

            control.label = CreateWindowExW(0, L"STATIC", label.c_str(), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
            const std::wstring value = [&]() {
                const auto* current = global ? FindGlobalValue(variable.name) : FindValue(*state, variable.name);
                return current ? Utf8ToWide(current->value) : std::wstring();
            }();
            control.edit = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                value.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                0,
                0,
                0,
                0,
                hwnd_,
                nullptr,
                nullptr,
                nullptr);

            if (variable.kind == McpVariableKind::Folder || variable.kind == McpVariableKind::File) {
                control.browse = CreateWindowExW(
                    0,
                    L"BUTTON",
                    variable.kind == McpVariableKind::Folder ? L"Browse..." : L"Choose...",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    0,
                    0,
                    0,
                    0,
                    hwnd_,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBrowseButtonBase + static_cast<int>(variable_controls_.size()))),
                    nullptr,
                    nullptr);
            }

            for (HWND hwnd : {control.label, control.edit, control.browse}) {
                if (hwnd) {
                    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
                    EnableWindow(hwnd, state->selected ? TRUE : FALSE);
                }
            }
            variable_controls_.push_back(control);
        };

        for (const auto& variable : config->variables) {
            add_variable_control(variable, false);
        }
        for (const auto& variable : global_variables) {
            add_variable_control(variable, true);
        }
        LayoutNow();
    }

    void LayoutNow() const {
        RECT rect{};
        GetClientRect(hwnd_, &rect);
        const_cast<ProjectSetupDialog*>(this)->LayoutControls(rect.right - rect.left, rect.bottom - rect.top);
    }

    void CaptureCurrentVariableValues() {
        ServerState* state = SelectedState();
        if (!state) {
            return;
        }

        state->selected = Button_GetCheck(selected_checkbox_) == BST_CHECKED;
        for (const auto& control : variable_controls_) {
            const std::string value = WideToUtf8(TrimWide(GetWindowTextString(control.edit)));
            if (control.global) {
                SetGlobalValue(control.name, value);
            } else {
                SetValue(*state, control.name, value);
            }
        }
    }

    void UpdateSelectionFlag() {
        ServerState* state = SelectedState();
        if (!state) {
            return;
        }

        state->selected = Button_GetCheck(selected_checkbox_) == BST_CHECKED;
        for (const auto& control : variable_controls_) {
            if (control.edit) {
                EnableWindow(control.edit, state->selected ? TRUE : FALSE);
            }
            if (control.browse) {
                EnableWindow(control.browse, state->selected ? TRUE : FALSE);
            }
            if (control.label) {
                EnableWindow(control.label, state->selected ? TRUE : FALSE);
            }
        }
        RefreshServerList();
    }

    void OnBrowse(int variable_index) {
        if (variable_index < 0 || static_cast<size_t>(variable_index) >= variable_controls_.size()) {
            return;
        }
        auto* state = SelectedState();
        if (!state || !state->selected) {
            return;
        }

        auto& control = variable_controls_[static_cast<size_t>(variable_index)];
        std::optional<std::wstring> choice;
        if (control.kind == McpVariableKind::Folder) {
            choice = BrowseForFolder(hwnd_, L"Select Folder");
        } else if (control.kind == McpVariableKind::File) {
            choice = BrowseForFile(hwnd_, L"Select File");
        }
        if (!choice) {
            return;
        }

        SetWindowTextW(control.edit, choice->c_str());
        if (control.global) {
            SetGlobalValue(control.name, WideToUtf8(*choice));
        } else {
            SetValue(*state, control.name, WideToUtf8(*choice));
        }
    }

    bool ValidateAndSave() {
        CaptureCurrentVariableValues();

        if (options_.include_project_name) {
            const std::wstring project_name = TrimWide(GetWindowTextString(project_name_edit_));
            if (project_name.empty()) {
                MessageBoxW(hwnd_, L"Project name is required.", L"Missing Project Name", MB_OK | MB_ICONERROR);
                SetFocus(project_name_edit_);
                return false;
            }
        }

        ProjectSetupDialogResult result;
        if (options_.include_project_name) {
            result.project_name = WideToUtf8(TrimWide(GetWindowTextString(project_name_edit_)));
        }

        for (size_t index = 0; index < options_.servers.size(); ++index) {
            const auto& server = options_.servers[index];
            const auto& state = states_[index];
            if (!state.selected) {
                continue;
            }

            ProjectMcpServerBinding binding;
            binding.server_id = server.id;

            for (const auto& variable : server.variables) {
                const auto* value = FindValue(state, variable.name);
                const std::string trimmed = value ? Trim(value->value) : std::string();
                if (trimmed.empty()) {
                    const std::wstring message = L"Fill a value for \"" + Utf8ToWide(variable.name) + L"\" on server \"" + Utf8ToWide(server.name) + L"\".";
                    MessageBoxW(hwnd_, message.c_str(), L"Missing Variable Value", MB_OK | MB_ICONERROR);
                    SendMessageW(servers_list_, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
                    RebuildDetails();
                    if (!variable_controls_.empty()) {
                        const auto it = std::find_if(variable_controls_.begin(), variable_controls_.end(), [&](const VariableControl& control) { return control.name == variable.name; });
                        if (it != variable_controls_.end() && it->edit) {
                            SetFocus(it->edit);
                        }
                    }
                    return false;
                }

                std::error_code ec;
                const std::filesystem::path path(Utf8ToWide(trimmed));
                if (variable.kind == McpVariableKind::Folder) {
                    if (!std::filesystem::exists(path, ec) || !std::filesystem::is_directory(path, ec)) {
                        const std::wstring message = L"\"" + Utf8ToWide(variable.name) + L"\" must point to an existing folder.";
                        MessageBoxW(hwnd_, message.c_str(), L"Invalid Folder", MB_OK | MB_ICONERROR);
                        SendMessageW(servers_list_, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
                        RebuildDetails();
                        return false;
                    }
                } else if (variable.kind == McpVariableKind::File) {
                    if (!std::filesystem::exists(path, ec) || std::filesystem::is_directory(path, ec)) {
                        const std::wstring message = L"\"" + Utf8ToWide(variable.name) + L"\" must point to an existing file.";
                        MessageBoxW(hwnd_, message.c_str(), L"Invalid File", MB_OK | MB_ICONERROR);
                        SendMessageW(servers_list_, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
                        RebuildDetails();
                        return false;
                    }
                }

                ProjectMcpVariableValue variable_value;
                variable_value.name = variable.name;
                variable_value.value = trimmed;
                binding.variables.push_back(std::move(variable_value));
            }

            for (const auto& variable : GlobalVariablesUsedBy(server)) {
                const auto* value = FindGlobalValue(variable.name);
                const std::string trimmed = value ? Trim(value->value) : std::string();
                if (trimmed.empty()) {
                    const std::wstring message = L"Fill a shared value for \"" + Utf8ToWide(variable.name) + L"\" on server \"" + Utf8ToWide(server.name) + L"\".";
                    MessageBoxW(hwnd_, message.c_str(), L"Missing Shared Variable Value", MB_OK | MB_ICONERROR);
                    SendMessageW(servers_list_, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
                    RebuildDetails();
                    const auto it = std::find_if(variable_controls_.begin(), variable_controls_.end(), [&](const VariableControl& control) {
                        return control.global && control.name == variable.name;
                    });
                    if (it != variable_controls_.end() && it->edit) {
                        SetFocus(it->edit);
                    }
                    return false;
                }

                std::error_code ec;
                const std::filesystem::path path(Utf8ToWide(trimmed));
                if (variable.kind == McpVariableKind::Folder) {
                    if (!std::filesystem::exists(path, ec) || !std::filesystem::is_directory(path, ec)) {
                        const std::wstring message = L"Shared variable \"" + Utf8ToWide(variable.name) + L"\" must point to an existing folder.";
                        MessageBoxW(hwnd_, message.c_str(), L"Invalid Folder", MB_OK | MB_ICONERROR);
                        SendMessageW(servers_list_, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
                        RebuildDetails();
                        return false;
                    }
                } else if (variable.kind == McpVariableKind::File) {
                    if (!std::filesystem::exists(path, ec) || std::filesystem::is_directory(path, ec)) {
                        const std::wstring message = L"Shared variable \"" + Utf8ToWide(variable.name) + L"\" must point to an existing file.";
                        MessageBoxW(hwnd_, message.c_str(), L"Invalid File", MB_OK | MB_ICONERROR);
                        SendMessageW(servers_list_, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
                        RebuildDetails();
                        return false;
                    }
                }

                ProjectMcpVariableValue variable_value;
                variable_value.name = variable.name;
                variable_value.value = trimmed;
                binding.variables.push_back(std::move(variable_value));
            }

            result.bindings.push_back(std::move(binding));
        }

        result_ = std::move(result);
        DestroyWindow(hwnd_);
        return true;
    }

    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    ProjectSetupDialogOptions options_;
    std::vector<ServerState> states_;
    std::vector<ProjectMcpVariableValue> global_values_;
    std::vector<VariableControl> variable_controls_;
    std::optional<ProjectSetupDialogResult> result_;

    HWND project_name_label_ = nullptr;
    HWND project_name_edit_ = nullptr;
    HWND servers_label_ = nullptr;
    HWND servers_list_ = nullptr;
    HWND details_label_ = nullptr;
    HWND selected_checkbox_ = nullptr;
    HWND scope_label_ = nullptr;
    HWND hint_label_ = nullptr;
    HWND no_variables_label_ = nullptr;
    HWND accept_button_ = nullptr;
    HWND cancel_button_ = nullptr;
};
}  // namespace

std::optional<ProjectSetupDialogResult> ShowProjectSetupDialog(HWND owner, const ProjectSetupDialogOptions& options) {
    return ProjectSetupDialog::Show(owner, options);
}
