#include "mcp_server_manager.h"

#include "project_setup_dialog.h"
#include "util.h"

#include <cctype>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <windowsx.h>

namespace {
constexpr wchar_t kMcpManagerClassName[] = L"AgentMcpServerManagerWindow";
constexpr wchar_t kMcpEditorClassName[] = L"AgentMcpServerEditorWindow";
constexpr wchar_t kVariableEditorClassName[] = L"AgentMcpVariableEditorWindow";
constexpr UINT kEditorTestResultMessage = WM_APP + 1;

enum ControlId : int {
    kServersList = 4001,
    kToolsList = 4002,
    kAddServer = 4003,
    kEditServer = 4004,
    kRemoveServer = 4005,
    kConnectServer = 4006,
    kDisconnectServer = 4007,
    kRefreshTools = 4008,
    kStatusLabel = 4009,
    kClose = 4010,
    kProjectServers = 4011,
    kGlobalVariablesList = 4012,
    kAddGlobalVariable = 4013,
    kEditGlobalVariable = 4014,
    kRemoveGlobalVariable = 4015,
};

enum EditorControlId : int {
    kEditorNameLabel = 4101,
    kEditorNameEdit = 4102,
    kEditorCommandLabel = 4103,
    kEditorCommandEdit = 4104,
    kEditorWorkingDirLabel = 4105,
    kEditorWorkingDirEdit = 4106,
    kEditorArgsLabel = 4107,
    kEditorArgsEdit = 4108,
    kEditorEnvLabel = 4109,
    kEditorEnvEdit = 4110,
    kEditorScopeLabel = 4111,
    kEditorScopeProject = 4112,
    kEditorScopeShared = 4113,
    kEditorVariablesLabel = 4114,
    kEditorVariablesList = 4115,
    kEditorAddVariable = 4116,
    kEditorEditVariable = 4117,
    kEditorRemoveVariable = 4118,
    kEditorEnabled = 4119,
    kEditorAutoConnect = 4120,
    kEditorDiagnosticsLabel = 4121,
    kEditorDiagnosticsEdit = 4122,
    kEditorTestButton = 4123,
    kEditorSaveButton = IDOK,
    kEditorCancelButton = IDCANCEL,
};

enum VariableEditorControlId : int {
    kVariableNameLabel = 4201,
    kVariableNameEdit = 4202,
    kVariableDescriptionLabel = 4203,
    kVariableDescriptionEdit = 4204,
    kVariableKindLabel = 4205,
    kVariableKindNone = 4206,
    kVariableKindFolder = 4207,
    kVariableKindFile = 4208,
    kVariableInjectContext = 4209,
    kVariableSaveButton = IDOK,
    kVariableCancelButton = IDCANCEL,
};

struct EditorTestPayload {
    McpServerTestResult result;
};

std::wstring StatusLabel(McpServerStatus status) {
    switch (status) {
    case McpServerStatus::Disconnected:
        return L"Disconnected";
    case McpServerStatus::Connecting:
        return L"Connecting";
    case McpServerStatus::Ready:
        return L"Ready";
    case McpServerStatus::Error:
        return L"Error";
    default:
        return L"Unknown";
    }
}

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

std::wstring JoinLines(const std::vector<std::string>& entries) {
    std::wstring text;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) {
            text += L"\r\n";
        }
        text += Utf8ToWide(entries[i]);
    }
    return text;
}

std::vector<std::string> SplitLines(const std::wstring& text) {
    std::vector<std::string> lines;
    std::wstringstream stream(text);
    std::wstring line;
    while (std::getline(stream, line)) {
        line = TrimWide(line);
        if (!line.empty()) {
            lines.push_back(WideToUtf8(line));
        }
    }
    return lines;
}

std::wstring JoinBulletLines(const std::vector<std::string>& items, const std::wstring& empty_text) {
    if (items.empty()) {
        return empty_text;
    }

    std::wstring text;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            text += L"\r\n";
        }
        text += L"- ";
        text += Utf8ToWide(items[i]);
    }
    return text;
}

std::optional<std::wstring> ValidateWorkingDirectory(const std::string& working_directory) {
    const std::wstring trimmed = TrimWide(Utf8ToWide(working_directory));
    if (trimmed.empty()) {
        return std::nullopt;
    }

    std::error_code ec;
    const std::filesystem::path path(trimmed);
    if (!std::filesystem::exists(path, ec)) {
        return L"Working directory does not exist.";
    }
    if (!std::filesystem::is_directory(path, ec)) {
        return L"Working directory must be a folder.";
    }
    return std::nullopt;
}

bool IsValidVariableName(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char ch) {
        const unsigned char value = static_cast<unsigned char>(ch);
        return std::isalnum(value) != 0 || ch == '_' || ch == '-';
    });
}

std::wstring VariableListLabel(const McpServerVariable& variable) {
    std::wstring label = Utf8ToWide(variable.name);
    label += L"  [";
    label += VariableKindLabel(variable.kind);
    label += L"]";
    if (variable.inject_into_context) {
        label += L"  [context]";
    }
    if (!variable.description.empty()) {
        label += L"  -  " + Utf8ToWide(variable.description);
    }
    return label;
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

bool ConfigUsesAnyGlobalVariable(const McpServerConfig& config, const std::vector<McpServerVariable>& global_variables) {
    return std::any_of(global_variables.begin(), global_variables.end(), [&](const McpServerVariable& variable) {
        return !variable.name.empty() && ConfigUsesVariable(config, variable.name);
    });
}

bool TextUsesAnyVariable(const std::string& text, const std::vector<McpServerVariable>& server_variables, const std::vector<McpServerVariable>& global_variables) {
    for (const auto& variable : server_variables) {
        if (!variable.name.empty() && TextUsesVariable(text, variable.name)) {
            return true;
        }
    }
    for (const auto& variable : global_variables) {
        if (!variable.name.empty() && TextUsesVariable(text, variable.name)) {
            return true;
        }
    }
    return false;
}

class McpVariableEditorDialog {
public:
    static std::optional<McpServerVariable> Show(HWND owner, const McpServerVariable& initial, bool editing);

private:
    McpVariableEditorDialog(HWND owner, McpServerVariable initial, bool editing);

    static int Scale(HWND hwnd, int value);
    static void RegisterWindowClass(HINSTANCE instance);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

    void OnCreate();
    void LayoutControls() const;
    void OnCommand(int control_id);
    bool ValidateAndSave();

    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    McpServerVariable variable_;
    bool editing_ = false;
    std::optional<McpServerVariable> result_;

    HWND name_label_ = nullptr;
    HWND name_edit_ = nullptr;
    HWND description_label_ = nullptr;
    HWND description_edit_ = nullptr;
    HWND kind_label_ = nullptr;
    HWND none_radio_ = nullptr;
    HWND folder_radio_ = nullptr;
    HWND file_radio_ = nullptr;
    HWND inject_context_checkbox_ = nullptr;
    HWND save_button_ = nullptr;
    HWND cancel_button_ = nullptr;
};

class McpServerEditorDialog {
public:
    static std::optional<McpServerConfig> Show(HWND owner, McpManager* manager, const McpServerConfig& initial, bool editing);

private:
    McpServerEditorDialog(HWND owner, McpManager* manager, McpServerConfig initial, bool editing);

    static int Scale(HWND hwnd, int value);
    static void RegisterWindowClass(HINSTANCE instance);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

    void OnCreate();
    void OnGetMinMaxInfo(MINMAXINFO* info) const;
    void LayoutControls(int width, int height) const;
    int SelectedVariableIndex() const;
    void RefreshVariablesList() const;
    void AddVariable();
    void EditVariable();
    void RemoveVariable();
    bool HasDuplicateVariableName(const std::string& name, int skip_index) const;
    McpServerConfig CollectConfig() const;
    bool ValidateConfig(McpServerConfig& config, bool show_message) const;
    bool ValidateAndSave();
    void RunTest();
    void OnTestResult(EditorTestPayload* payload);
    void OnCommand(int control_id, int notification_code);

    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    McpManager* manager_ = nullptr;
    McpServerConfig config_;
    bool editing_ = false;
    bool test_in_progress_ = false;
    std::vector<McpServerVariable> variables_;
    std::optional<McpServerConfig> result_;

    HWND name_label_ = nullptr;
    HWND name_edit_ = nullptr;
    HWND command_label_ = nullptr;
    HWND command_edit_ = nullptr;
    HWND working_dir_label_ = nullptr;
    HWND working_dir_edit_ = nullptr;
    HWND args_label_ = nullptr;
    HWND args_edit_ = nullptr;
    HWND env_label_ = nullptr;
    HWND env_edit_ = nullptr;
    HWND scope_label_ = nullptr;
    HWND scope_project_radio_ = nullptr;
    HWND scope_shared_radio_ = nullptr;
    HWND variables_label_ = nullptr;
    HWND variables_list_ = nullptr;
    HWND add_variable_button_ = nullptr;
    HWND edit_variable_button_ = nullptr;
    HWND remove_variable_button_ = nullptr;
    HWND enabled_checkbox_ = nullptr;
    HWND auto_connect_checkbox_ = nullptr;
    HWND diagnostics_label_ = nullptr;
    HWND diagnostics_edit_ = nullptr;
    HWND test_button_ = nullptr;
    HWND save_button_ = nullptr;
    HWND cancel_button_ = nullptr;
};

class McpServerManagerWindow {
public:
    McpServerManagerWindow(HWND owner, McpManager* manager, std::function<std::string()> active_project_id_provider);
    HWND Create(HINSTANCE instance);

private:
    static int Scale(HWND hwnd, int value);
    static void RegisterWindowClass(HINSTANCE instance);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

    void OnCreate();
    void LayoutControls(int width, int height) const;
    std::string ActiveProjectId() const;
    int SelectedIndex() const;
    int SelectedGlobalVariableIndex() const;
    void RefreshGlobalVariablesList();
    void ReloadSnapshots();
    void RefreshToolsList();
    void SaveConfigs(std::vector<McpServerConfig> configs);
    void SaveGlobalVariables(std::vector<McpServerVariable> variables);
    bool HasDuplicateGlobalVariableName(const std::string& name, int skip_index) const;
    void AddGlobalVariable();
    void EditGlobalVariable();
    void RemoveGlobalVariable();
    void AddServer();
    void EditServer();
    void RemoveServer();
    bool ConfigureProjectSelection(const McpServerConfig& config, const std::string& project_id);
    void EditProjectSelection();
    void ConnectSelected();
    void DisconnectSelected();
    void RefreshSelectedTools();
    void OnCommand(int control_id, int notification_code);

    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr;
    HWND global_variables_list_ = nullptr;
    HWND add_global_variable_button_ = nullptr;
    HWND edit_global_variable_button_ = nullptr;
    HWND remove_global_variable_button_ = nullptr;
    HWND servers_list_ = nullptr;
    HWND tools_list_ = nullptr;
    HWND add_button_ = nullptr;
    HWND edit_button_ = nullptr;
    HWND remove_button_ = nullptr;
    HWND connect_button_ = nullptr;
    HWND disconnect_button_ = nullptr;
    HWND refresh_button_ = nullptr;
    HWND project_servers_button_ = nullptr;
    HWND close_button_ = nullptr;
    HWND status_label_ = nullptr;
    McpManager* manager_ = nullptr;
    std::function<std::string()> active_project_id_provider_;
    std::vector<McpServerSnapshot> snapshots_;
};

std::optional<McpServerVariable> McpVariableEditorDialog::Show(HWND owner, const McpServerVariable& initial, bool editing) {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    RegisterWindowClass(instance);

    auto* dialog = new McpVariableEditorDialog(owner, initial, editing);
    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE;
    const DWORD ex_style = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;

    if (owner) {
        EnableWindow(owner, FALSE);
    }

    dialog->hwnd_ = CreateWindowExW(
        ex_style,
        kVariableEditorClassName,
        editing ? L"Edit MCP Variable" : L"Add MCP Variable",
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        520,
        320,
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

McpVariableEditorDialog::McpVariableEditorDialog(HWND owner, McpServerVariable initial, bool editing)
    : owner_(owner), variable_(std::move(initial)), editing_(editing) {}

int McpVariableEditorDialog::Scale(HWND hwnd, int value) {
    return MulDiv(value, GetDpiForWindow(hwnd), 96);
}

void McpVariableEditorDialog::RegisterWindowClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) {
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = &McpVariableEditorDialog::WindowProc;
    wc.lpszClassName = kVariableEditorClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
    registered = true;
}

LRESULT CALLBACK McpVariableEditorDialog::WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* self = reinterpret_cast<McpVariableEditorDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        self = reinterpret_cast<McpVariableEditorDialog*>(create->lpCreateParams);
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
    case WM_COMMAND:
        self->OnCommand(LOWORD(w_param));
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, w_param, l_param);
    }
}

void McpVariableEditorDialog::OnCreate() {
    font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    name_label_ = CreateWindowExW(0, L"STATIC", L"Variable Name", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kVariableNameLabel), nullptr, nullptr);
    name_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", Utf8ToWide(variable_.name).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kVariableNameEdit), nullptr, nullptr);
    description_label_ = CreateWindowExW(0, L"STATIC", L"Description", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kVariableDescriptionLabel), nullptr, nullptr);
    description_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", Utf8ToWide(variable_.description).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kVariableDescriptionEdit), nullptr, nullptr);
    kind_label_ = CreateWindowExW(0, L"STATIC", L"Value Type", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kVariableKindLabel), nullptr, nullptr);
    none_radio_ = CreateWindowExW(0, L"BUTTON", L"None", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kVariableKindNone), nullptr, nullptr);
    folder_radio_ = CreateWindowExW(0, L"BUTTON", L"Folder", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kVariableKindFolder), nullptr, nullptr);
    file_radio_ = CreateWindowExW(0, L"BUTTON", L"File", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kVariableKindFile), nullptr, nullptr);
    inject_context_checkbox_ = CreateWindowExW(0, L"BUTTON", L"Inject this variable value into the chat context", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kVariableInjectContext), nullptr, nullptr);
    save_button_ = CreateWindowExW(0, L"BUTTON", editing_ ? L"Save" : L"Add", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kVariableSaveButton), nullptr, nullptr);
    cancel_button_ = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kVariableCancelButton), nullptr, nullptr);

    for (HWND control : {name_label_, name_edit_, description_label_, description_edit_, kind_label_, none_radio_, folder_radio_, file_radio_, inject_context_checkbox_, save_button_, cancel_button_}) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }

    switch (variable_.kind) {
    case McpVariableKind::Folder:
        Button_SetCheck(folder_radio_, BST_CHECKED);
        break;
    case McpVariableKind::File:
        Button_SetCheck(file_radio_, BST_CHECKED);
        break;
    case McpVariableKind::None:
    default:
        Button_SetCheck(none_radio_, BST_CHECKED);
        break;
    }
    Button_SetCheck(inject_context_checkbox_, variable_.inject_into_context ? BST_CHECKED : BST_UNCHECKED);

    CenterWindowToOwner(hwnd_, owner_);
    LayoutControls();
    SetFocus(name_edit_);
}

void McpVariableEditorDialog::LayoutControls() const {
    const int margin = Scale(hwnd_, 12);
    const int label_height = Scale(hwnd_, 18);
    const int edit_height = Scale(hwnd_, 24);
    const int gutter = Scale(hwnd_, 8);
    const int button_width = Scale(hwnd_, 92);
    const int button_height = Scale(hwnd_, 30);
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;

    int y = margin;
    MoveWindow(name_label_, margin, y, width - margin * 2, label_height, TRUE);
    y += label_height + gutter;
    MoveWindow(name_edit_, margin, y, width - margin * 2, edit_height, TRUE);
    y += edit_height + gutter;

    MoveWindow(description_label_, margin, y, width - margin * 2, label_height, TRUE);
    y += label_height + gutter;
    MoveWindow(description_edit_, margin, y, width - margin * 2, edit_height, TRUE);
    y += edit_height + gutter;

    MoveWindow(kind_label_, margin, y, width - margin * 2, label_height, TRUE);
    y += label_height + gutter;
    MoveWindow(none_radio_, margin, y, Scale(hwnd_, 80), Scale(hwnd_, 22), TRUE);
    MoveWindow(folder_radio_, margin + Scale(hwnd_, 100), y, Scale(hwnd_, 90), Scale(hwnd_, 22), TRUE);
    MoveWindow(file_radio_, margin + Scale(hwnd_, 210), y, Scale(hwnd_, 70), Scale(hwnd_, 22), TRUE);
    y += Scale(hwnd_, 30);
    MoveWindow(inject_context_checkbox_, margin, y, width - margin * 2, Scale(hwnd_, 22), TRUE);

    const int buttons_y = height - margin - button_height;
    MoveWindow(cancel_button_, width - margin - button_width, buttons_y, button_width, button_height, TRUE);
    MoveWindow(save_button_, width - margin - button_width * 2 - gutter, buttons_y, button_width, button_height, TRUE);
}

void McpVariableEditorDialog::OnCommand(int control_id) {
    switch (control_id) {
    case kVariableSaveButton:
        ValidateAndSave();
        break;
    case kVariableCancelButton:
        DestroyWindow(hwnd_);
        break;
    default:
        break;
    }
}

bool McpVariableEditorDialog::ValidateAndSave() {
    McpServerVariable variable = variable_;
    variable.name = WideToUtf8(TrimWide(GetWindowTextString(name_edit_)));
    variable.description = WideToUtf8(TrimWide(GetWindowTextString(description_edit_)));
    if (Button_GetCheck(folder_radio_) == BST_CHECKED) {
        variable.kind = McpVariableKind::Folder;
    } else if (Button_GetCheck(file_radio_) == BST_CHECKED) {
        variable.kind = McpVariableKind::File;
    } else {
        variable.kind = McpVariableKind::None;
    }
    variable.inject_into_context = Button_GetCheck(inject_context_checkbox_) == BST_CHECKED;

    if (variable.name.empty()) {
        MessageBoxW(hwnd_, L"Variable name is required.", L"Missing Name", MB_OK | MB_ICONERROR);
        SetFocus(name_edit_);
        return false;
    }
    if (!IsValidVariableName(variable.name)) {
        MessageBoxW(hwnd_, L"Variable names can use letters, numbers, '_' and '-'.", L"Invalid Name", MB_OK | MB_ICONERROR);
        SetFocus(name_edit_);
        return false;
    }

    result_ = std::move(variable);
    DestroyWindow(hwnd_);
    return true;
}

std::optional<McpServerConfig> McpServerEditorDialog::Show(HWND owner, McpManager* manager, const McpServerConfig& initial, bool editing) {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    RegisterWindowClass(instance);

    auto* dialog = new McpServerEditorDialog(owner, manager, initial, editing);
    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE | WS_THICKFRAME;
    const DWORD ex_style = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;

    if (owner) {
        EnableWindow(owner, FALSE);
    }

    dialog->hwnd_ = CreateWindowExW(
        ex_style,
        kMcpEditorClassName,
        editing ? L"Edit MCP Server" : L"Add MCP Server",
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        860,
        920,
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

McpServerEditorDialog::McpServerEditorDialog(HWND owner, McpManager* manager, McpServerConfig initial, bool editing)
    : owner_(owner), manager_(manager), config_(std::move(initial)), editing_(editing), variables_(config_.variables) {}

int McpServerEditorDialog::Scale(HWND hwnd, int value) {
    return MulDiv(value, GetDpiForWindow(hwnd), 96);
}

void McpServerEditorDialog::RegisterWindowClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) {
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = &McpServerEditorDialog::WindowProc;
    wc.lpszClassName = kMcpEditorClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
    registered = true;
}

LRESULT CALLBACK McpServerEditorDialog::WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* self = reinterpret_cast<McpServerEditorDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        self = reinterpret_cast<McpServerEditorDialog*>(create->lpCreateParams);
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
    case kEditorTestResultMessage:
        self->OnTestResult(reinterpret_cast<EditorTestPayload*>(l_param));
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, w_param, l_param);
    }
}

void McpServerEditorDialog::OnCreate() {
    font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    name_label_ = CreateWindowExW(0, L"STATIC", L"Name", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorNameLabel), nullptr, nullptr);
    name_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", Utf8ToWide(config_.name).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorNameEdit), nullptr, nullptr);
    command_label_ = CreateWindowExW(0, L"STATIC", L"Command", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorCommandLabel), nullptr, nullptr);
    command_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", Utf8ToWide(config_.command).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorCommandEdit), nullptr, nullptr);
    working_dir_label_ = CreateWindowExW(0, L"STATIC", L"Working Directory", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorWorkingDirLabel), nullptr, nullptr);
    working_dir_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", Utf8ToWide(config_.working_directory).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorWorkingDirEdit), nullptr, nullptr);
    args_label_ = CreateWindowExW(0, L"STATIC", L"Arguments (one per line)", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorArgsLabel), nullptr, nullptr);
    args_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", JoinLines(config_.arguments).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorArgsEdit), nullptr, nullptr);
    env_label_ = CreateWindowExW(0, L"STATIC", L"Environment Variables (KEY=VALUE per line)", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorEnvLabel), nullptr, nullptr);
    env_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", JoinLines(config_.env_entries).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorEnvEdit), nullptr, nullptr);
    scope_label_ = CreateWindowExW(0, L"STATIC", L"Server Scope", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorScopeLabel), nullptr, nullptr);
    scope_project_radio_ = CreateWindowExW(0, L"BUTTON", L"Per project (default)", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorScopeProject), nullptr, nullptr);
    scope_shared_radio_ = CreateWindowExW(0, L"BUTTON", L"Shared per process", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorScopeShared), nullptr, nullptr);
    variables_label_ = CreateWindowExW(0, L"STATIC", L"Project Variables", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorVariablesLabel), nullptr, nullptr);
    variables_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | WS_VSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorVariablesList), nullptr, nullptr);
    add_variable_button_ = CreateWindowExW(0, L"BUTTON", L"Add Variable", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorAddVariable), nullptr, nullptr);
    edit_variable_button_ = CreateWindowExW(0, L"BUTTON", L"Edit Variable", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorEditVariable), nullptr, nullptr);
    remove_variable_button_ = CreateWindowExW(0, L"BUTTON", L"Remove Variable", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorRemoveVariable), nullptr, nullptr);
    enabled_checkbox_ = CreateWindowExW(0, L"BUTTON", L"Enabled", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorEnabled), nullptr, nullptr);
    auto_connect_checkbox_ = CreateWindowExW(0, L"BUTTON", L"Auto-connect when selected for a project", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorAutoConnect), nullptr, nullptr);
    diagnostics_label_ = CreateWindowExW(0, L"STATIC", L"Diagnostics", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorDiagnosticsLabel), nullptr, nullptr);
    diagnostics_edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"Use Test to validate the command and see launch output here.", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorDiagnosticsEdit), nullptr, nullptr);
    test_button_ = CreateWindowExW(0, L"BUTTON", L"Test", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorTestButton), nullptr, nullptr);
    save_button_ = CreateWindowExW(0, L"BUTTON", editing_ ? L"Save" : L"Add", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorSaveButton), nullptr, nullptr);
    cancel_button_ = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditorCancelButton), nullptr, nullptr);

    for (HWND control : {name_label_, name_edit_, command_label_, command_edit_, working_dir_label_, working_dir_edit_, args_label_, args_edit_, env_label_, env_edit_, scope_label_, scope_project_radio_, scope_shared_radio_, variables_label_, variables_list_, add_variable_button_, edit_variable_button_, remove_variable_button_, enabled_checkbox_, auto_connect_checkbox_, diagnostics_label_, diagnostics_edit_, test_button_, save_button_, cancel_button_}) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }

    Button_SetCheck(enabled_checkbox_, config_.enabled ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(auto_connect_checkbox_, config_.auto_connect ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(config_.scope == McpServerScope::Shared ? scope_shared_radio_ : scope_project_radio_, BST_CHECKED);

    RefreshVariablesList();
    CenterWindowToOwner(hwnd_, owner_);
    SetFocus(name_edit_);
}

void McpServerEditorDialog::OnGetMinMaxInfo(MINMAXINFO* info) const {
    if (!info) {
        return;
    }
    info->ptMinTrackSize.x = Scale(hwnd_, 820);
    info->ptMinTrackSize.y = Scale(hwnd_, 860);
}

void McpServerEditorDialog::LayoutControls(int width, int height) const {
    const int margin = Scale(hwnd_, 12);
    const int gutter = Scale(hwnd_, 8);
    const int label_height = Scale(hwnd_, 18);
    const int edit_height = Scale(hwnd_, 24);
    const int multi_height = Scale(hwnd_, 88);
    const int list_height = Scale(hwnd_, 124);
    const int checkbox_height = Scale(hwnd_, 22);
    const int button_height = Scale(hwnd_, 30);
    const int button_width = Scale(hwnd_, 96);
    const int variable_button_width = Scale(hwnd_, 108);

    int y = margin;
    MoveWindow(name_label_, margin, y, width - margin * 2, label_height, TRUE);
    y += label_height + gutter;
    MoveWindow(name_edit_, margin, y, width - margin * 2, edit_height, TRUE);
    y += edit_height + gutter;

    MoveWindow(command_label_, margin, y, width - margin * 2, label_height, TRUE);
    y += label_height + gutter;
    MoveWindow(command_edit_, margin, y, width - margin * 2, edit_height, TRUE);
    y += edit_height + gutter;

    MoveWindow(working_dir_label_, margin, y, width - margin * 2, label_height, TRUE);
    y += label_height + gutter;
    MoveWindow(working_dir_edit_, margin, y, width - margin * 2, edit_height, TRUE);
    y += edit_height + gutter;

    MoveWindow(args_label_, margin, y, width - margin * 2, label_height, TRUE);
    y += label_height + gutter;
    MoveWindow(args_edit_, margin, y, width - margin * 2, multi_height, TRUE);
    y += multi_height + gutter;

    MoveWindow(env_label_, margin, y, width - margin * 2, label_height, TRUE);
    y += label_height + gutter;
    MoveWindow(env_edit_, margin, y, width - margin * 2, multi_height, TRUE);
    y += multi_height + gutter;

    MoveWindow(scope_label_, margin, y, width - margin * 2, label_height, TRUE);
    y += label_height + gutter;
    MoveWindow(scope_project_radio_, margin, y, Scale(hwnd_, 180), checkbox_height, TRUE);
    MoveWindow(scope_shared_radio_, margin + Scale(hwnd_, 220), y, Scale(hwnd_, 180), checkbox_height, TRUE);
    y += checkbox_height + gutter;

    MoveWindow(variables_label_, margin, y, width - margin * 2, label_height, TRUE);
    y += label_height + gutter;
    const int variable_list_width = width - margin * 2 - variable_button_width - gutter;
    MoveWindow(variables_list_, margin, y, variable_list_width, list_height, TRUE);
    MoveWindow(add_variable_button_, margin + variable_list_width + gutter, y, variable_button_width, button_height, TRUE);
    MoveWindow(edit_variable_button_, margin + variable_list_width + gutter, y + button_height + gutter, variable_button_width, button_height, TRUE);
    MoveWindow(remove_variable_button_, margin + variable_list_width + gutter, y + (button_height + gutter) * 2, variable_button_width, button_height, TRUE);
    y += list_height + gutter;

    MoveWindow(enabled_checkbox_, margin, y, Scale(hwnd_, 110), checkbox_height, TRUE);
    MoveWindow(auto_connect_checkbox_, margin + Scale(hwnd_, 130), y, width - margin * 2 - Scale(hwnd_, 130), checkbox_height, TRUE);
    y += checkbox_height + gutter;

    MoveWindow(diagnostics_label_, margin, y, width - margin * 2, label_height, TRUE);
    y += label_height + gutter;

    const int footer_top = height - margin - button_height;
    MoveWindow(diagnostics_edit_, margin, y, width - margin * 2, std::max(Scale(hwnd_, 120), footer_top - y - gutter), TRUE);
    MoveWindow(cancel_button_, width - margin - button_width, footer_top, button_width, button_height, TRUE);
    MoveWindow(save_button_, width - margin - button_width * 2 - gutter, footer_top, button_width, button_height, TRUE);
    MoveWindow(test_button_, width - margin - button_width * 3 - gutter * 2, footer_top, button_width, button_height, TRUE);
}

int McpServerEditorDialog::SelectedVariableIndex() const {
    const LRESULT selection = SendMessageW(variables_list_, LB_GETCURSEL, 0, 0);
    return selection == LB_ERR ? -1 : static_cast<int>(selection);
}

void McpServerEditorDialog::RefreshVariablesList() const {
    SendMessageW(variables_list_, LB_RESETCONTENT, 0, 0);
    for (const auto& variable : variables_) {
        const std::wstring label = VariableListLabel(variable);
        SendMessageW(variables_list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (!variables_.empty()) {
        SendMessageW(variables_list_, LB_SETCURSEL, 0, 0);
    }
}

void McpServerEditorDialog::AddVariable() {
    const auto result = McpVariableEditorDialog::Show(hwnd_, McpServerVariable{}, false);
    if (!result) {
        return;
    }
    if (HasDuplicateVariableName(result->name, -1)) {
        MessageBoxW(hwnd_, L"A variable with that name already exists on this server.", L"Duplicate Variable", MB_OK | MB_ICONERROR);
        return;
    }
    variables_.push_back(*result);
    RefreshVariablesList();
}

void McpServerEditorDialog::EditVariable() {
    const int index = SelectedVariableIndex();
    if (index < 0 || static_cast<size_t>(index) >= variables_.size()) {
        return;
    }

    const auto result = McpVariableEditorDialog::Show(hwnd_, variables_[static_cast<size_t>(index)], true);
    if (!result) {
        return;
    }
    if (HasDuplicateVariableName(result->name, index)) {
        MessageBoxW(hwnd_, L"A variable with that name already exists on this server.", L"Duplicate Variable", MB_OK | MB_ICONERROR);
        return;
    }
    variables_[static_cast<size_t>(index)] = *result;
    RefreshVariablesList();
    SendMessageW(variables_list_, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
}

void McpServerEditorDialog::RemoveVariable() {
    const int index = SelectedVariableIndex();
    if (index < 0 || static_cast<size_t>(index) >= variables_.size()) {
        return;
    }

    const std::wstring prompt = L"Remove variable \"" + Utf8ToWide(variables_[static_cast<size_t>(index)].name) + L"\"?";
    if (MessageBoxW(hwnd_, prompt.c_str(), L"Remove Variable", MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }

    variables_.erase(variables_.begin() + index);
    RefreshVariablesList();
}

bool McpServerEditorDialog::HasDuplicateVariableName(const std::string& name, int skip_index) const {
    for (size_t i = 0; i < variables_.size(); ++i) {
        if (static_cast<int>(i) == skip_index) {
            continue;
        }
        if (variables_[i].name == name) {
            return true;
        }
    }
    return false;
}

McpServerConfig McpServerEditorDialog::CollectConfig() const {
    McpServerConfig config = config_;
    config.name = WideToUtf8(TrimWide(GetWindowTextString(name_edit_)));
    config.command = WideToUtf8(TrimWide(GetWindowTextString(command_edit_)));
    config.working_directory = WideToUtf8(TrimWide(GetWindowTextString(working_dir_edit_)));
    config.arguments = SplitLines(GetWindowTextString(args_edit_));
    config.env_entries = SplitLines(GetWindowTextString(env_edit_));
    config.scope = Button_GetCheck(scope_shared_radio_) == BST_CHECKED ? McpServerScope::Shared : McpServerScope::PerProject;
    config.variables = variables_;
    config.enabled = Button_GetCheck(enabled_checkbox_) == BST_CHECKED;
    config.auto_connect = Button_GetCheck(auto_connect_checkbox_) == BST_CHECKED;
    return config;
}

bool McpServerEditorDialog::ValidateConfig(McpServerConfig& config, bool show_message) const {
    if (config.name.empty()) {
        if (show_message) {
            MessageBoxW(hwnd_, L"Server name is required.", L"Missing Name", MB_OK | MB_ICONERROR);
            SetFocus(name_edit_);
        }
        return false;
    }
    if (config.command.empty()) {
        if (show_message) {
            MessageBoxW(hwnd_, L"Server command is required.", L"Missing Command", MB_OK | MB_ICONERROR);
            SetFocus(command_edit_);
        }
        return false;
    }
    const bool working_directory_uses_variable = TextUsesAnyVariable(config.working_directory, config.variables, manager_->global_variables());
    const auto working_directory_error = working_directory_uses_variable ? std::optional<std::wstring>{} : ValidateWorkingDirectory(config.working_directory);
    if (working_directory_error) {
        if (show_message) {
            MessageBoxW(hwnd_, working_directory_error->c_str(), L"Invalid Working Directory", MB_OK | MB_ICONERROR);
            SetFocus(working_dir_edit_);
        }
        return false;
    }
    if (config.scope == McpServerScope::Shared && (!config.variables.empty() || ConfigUsesAnyGlobalVariable(config, manager_->global_variables()))) {
        if (show_message) {
            MessageBoxW(hwnd_, L"Shared MCP servers cannot use project variables. Switch the server to per-project scope or remove the variables.", L"Invalid Scope", MB_OK | MB_ICONERROR);
        }
        return false;
    }
    for (const auto& variable : config.variables) {
        if (!IsValidVariableName(variable.name)) {
            if (show_message) {
                MessageBoxW(hwnd_, L"Every variable name must use letters, numbers, '_' or '-'.", L"Invalid Variable Name", MB_OK | MB_ICONERROR);
            }
            return false;
        }
    }
    return true;
}

bool McpServerEditorDialog::ValidateAndSave() {
    McpServerConfig config = CollectConfig();
    if (!ValidateConfig(config, true)) {
        return false;
    }

    result_ = std::move(config);
    DestroyWindow(hwnd_);
    return true;
}

void McpServerEditorDialog::RunTest() {
    if (test_in_progress_) {
        return;
    }

    McpServerConfig config = CollectConfig();
    if (!ValidateConfig(config, true)) {
        return;
    }

    test_in_progress_ = true;
    EnableWindow(test_button_, FALSE);
    EnableWindow(save_button_, FALSE);
    SetWindowTextW(diagnostics_edit_, L"Running launch and initialize test...");

    std::thread([hwnd = hwnd_, manager = manager_, config]() {
        auto* payload = new EditorTestPayload;
        payload->result = manager->TestServerConfig(config);
        PostMessageW(hwnd, kEditorTestResultMessage, 0, reinterpret_cast<LPARAM>(payload));
    }).detach();
}

void McpServerEditorDialog::OnTestResult(EditorTestPayload* payload) {
    std::unique_ptr<EditorTestPayload> guard(payload);
    test_in_progress_ = false;
    EnableWindow(test_button_, TRUE);
    EnableWindow(save_button_, TRUE);

    std::wstring diagnostics = L"Summary:\r\n" + Utf8ToWide(guard->result.summary);
    diagnostics += L"\r\n\r\nDetected Features:\r\n";
    diagnostics += JoinBulletLines(guard->result.detected_features, L"(no features reported)");
    diagnostics += L"\r\n\r\nDetected Tools:\r\n";
    diagnostics += JoinBulletLines(guard->result.detected_tools, L"(no tools reported)");
    diagnostics += L"\r\n\r\nStdin Sent:\r\n";
    diagnostics += guard->result.stdin_text.empty() ? L"(no stdin captured)" : Utf8ToWide(guard->result.stdin_text);
    diagnostics += L"\r\n\r\nStdout:\r\n";
    diagnostics += guard->result.stdout_text.empty() ? L"(no stdout captured)" : Utf8ToWide(guard->result.stdout_text);
    diagnostics += L"\r\n\r\nStderr:\r\n";
    diagnostics += guard->result.stderr_text.empty() ? L"(no stderr captured)" : Utf8ToWide(guard->result.stderr_text);
    SetWindowTextW(diagnostics_edit_, diagnostics.c_str());
    MessageBoxW(hwnd_, Utf8ToWide(guard->result.summary).c_str(), guard->result.success ? L"Test Passed" : L"Test Failed", MB_OK | (guard->result.success ? MB_ICONINFORMATION : MB_ICONWARNING));
}

void McpServerEditorDialog::OnCommand(int control_id, int notification_code) {
    switch (control_id) {
    case kEditorVariablesList:
        if (notification_code == LBN_DBLCLK) {
            EditVariable();
        }
        break;
    case kEditorAddVariable:
        AddVariable();
        break;
    case kEditorEditVariable:
        EditVariable();
        break;
    case kEditorRemoveVariable:
        RemoveVariable();
        break;
    case kEditorTestButton:
        RunTest();
        break;
    case kEditorSaveButton:
        ValidateAndSave();
        break;
    case kEditorCancelButton:
        DestroyWindow(hwnd_);
        break;
    default:
        break;
    }
}

McpServerManagerWindow::McpServerManagerWindow(HWND owner, McpManager* manager, std::function<std::string()> active_project_id_provider)
    : owner_(owner), manager_(manager), active_project_id_provider_(std::move(active_project_id_provider)) {}

HWND McpServerManagerWindow::Create(HINSTANCE instance) {
    RegisterWindowClass(instance);
    hwnd_ = CreateWindowExW(
        WS_EX_APPWINDOW,
        kMcpManagerClassName,
        L"MCP Server Manager",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        980,
        620,
        owner_,
        nullptr,
        instance,
        this);
    return hwnd_;
}

int McpServerManagerWindow::Scale(HWND hwnd, int value) {
    return MulDiv(value, GetDpiForWindow(hwnd), 96);
}

void McpServerManagerWindow::RegisterWindowClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) {
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = &McpServerManagerWindow::WindowProc;
    wc.lpszClassName = kMcpManagerClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
    registered = true;
}

LRESULT CALLBACK McpServerManagerWindow::WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* self = reinterpret_cast<McpServerManagerWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        self = reinterpret_cast<McpServerManagerWindow*>(create->lpCreateParams);
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
    case WM_ACTIVATE:
        if (LOWORD(w_param) != WA_INACTIVE) {
            self->ReloadSnapshots();
        }
        return 0;
    case WM_COMMAND:
        self->OnCommand(LOWORD(w_param), HIWORD(w_param));
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

void McpServerManagerWindow::OnCreate() {
    font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    global_variables_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kGlobalVariablesList), nullptr, nullptr);
    add_global_variable_button_ = CreateWindowExW(0, L"BUTTON", L"Add Variable", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kAddGlobalVariable), nullptr, nullptr);
    edit_global_variable_button_ = CreateWindowExW(0, L"BUTTON", L"Edit Variable", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditGlobalVariable), nullptr, nullptr);
    remove_global_variable_button_ = CreateWindowExW(0, L"BUTTON", L"Remove Variable", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRemoveGlobalVariable), nullptr, nullptr);
    servers_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kServersList), nullptr, nullptr);
    tools_list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kToolsList), nullptr, nullptr);
    add_button_ = CreateWindowExW(0, L"BUTTON", L"Add Server", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kAddServer), nullptr, nullptr);
    edit_button_ = CreateWindowExW(0, L"BUTTON", L"Edit Server", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kEditServer), nullptr, nullptr);
    remove_button_ = CreateWindowExW(0, L"BUTTON", L"Remove Server", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRemoveServer), nullptr, nullptr);
    connect_button_ = CreateWindowExW(0, L"BUTTON", L"Connect", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kConnectServer), nullptr, nullptr);
    disconnect_button_ = CreateWindowExW(0, L"BUTTON", L"Disconnect", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kDisconnectServer), nullptr, nullptr);
    refresh_button_ = CreateWindowExW(0, L"BUTTON", L"Refresh Tools", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRefreshTools), nullptr, nullptr);
    project_servers_button_ = CreateWindowExW(0, L"BUTTON", L"Project MCP", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kProjectServers), nullptr, nullptr);
    close_button_ = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kClose), nullptr, nullptr);
    status_label_ = CreateWindowExW(0, L"STATIC", L"Configure MCP servers, then select them for a project and connect them here.", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kStatusLabel), nullptr, nullptr);

    for (HWND control : {global_variables_list_, add_global_variable_button_, edit_global_variable_button_, remove_global_variable_button_, servers_list_, tools_list_, add_button_, edit_button_, remove_button_, connect_button_, disconnect_button_, refresh_button_, project_servers_button_, close_button_, status_label_}) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }

    RefreshGlobalVariablesList();
    ReloadSnapshots();
}

void McpServerManagerWindow::LayoutControls(int width, int height) const {
    const int margin = Scale(hwnd_, 12);
    const int gutter = Scale(hwnd_, 12);
    const int button_height = Scale(hwnd_, 28);
    const int variable_height = Scale(hwnd_, 108);
    const int variable_button_width = Scale(hwnd_, 120);
    const int variable_button_x = width - margin - variable_button_width;
    MoveWindow(global_variables_list_, margin, margin, width - margin * 2 - variable_button_width - gutter, variable_height, TRUE);
    MoveWindow(add_global_variable_button_, variable_button_x, margin, variable_button_width, button_height, TRUE);
    MoveWindow(edit_global_variable_button_, variable_button_x, margin + button_height + gutter, variable_button_width, button_height, TRUE);
    MoveWindow(remove_global_variable_button_, variable_button_x, margin + (button_height + gutter) * 2, variable_button_width, button_height, TRUE);

    const int lists_top = margin + variable_height + gutter;
    const int list_height = height - lists_top - margin - button_height * 2 - gutter * 2 - Scale(hwnd_, 24);
    const int list_width = (width - margin * 2 - gutter) / 2;
    const int button_width = (list_width - gutter * 2) / 3;

    MoveWindow(servers_list_, margin, lists_top, list_width, list_height, TRUE);
    MoveWindow(tools_list_, margin + list_width + gutter, lists_top, list_width, list_height, TRUE);

    const int buttons_top = lists_top + list_height + gutter;
    MoveWindow(add_button_, margin, buttons_top, button_width, button_height, TRUE);
    MoveWindow(edit_button_, margin + button_width + gutter, buttons_top, button_width, button_height, TRUE);
    MoveWindow(remove_button_, margin + (button_width + gutter) * 2, buttons_top, button_width, button_height, TRUE);

    MoveWindow(connect_button_, margin + list_width + gutter, buttons_top, button_width, button_height, TRUE);
    MoveWindow(disconnect_button_, margin + list_width + gutter + button_width + gutter, buttons_top, button_width, button_height, TRUE);
    MoveWindow(refresh_button_, margin + list_width + gutter + (button_width + gutter) * 2, buttons_top, button_width, button_height, TRUE);

    const int footer_top = buttons_top + button_height + gutter;
    const int footer_button_width = Scale(hwnd_, 100);
    MoveWindow(status_label_, margin, footer_top, width - margin * 2 - footer_button_width * 2 - gutter * 2, Scale(hwnd_, 22), TRUE);
    MoveWindow(project_servers_button_, width - margin - footer_button_width * 2 - gutter, footer_top - Scale(hwnd_, 2), footer_button_width, button_height, TRUE);
    MoveWindow(close_button_, width - margin - footer_button_width, footer_top - Scale(hwnd_, 2), footer_button_width, button_height, TRUE);
}

std::string McpServerManagerWindow::ActiveProjectId() const {
    return active_project_id_provider_ ? active_project_id_provider_() : std::string();
}

int McpServerManagerWindow::SelectedIndex() const {
    const LRESULT selection = SendMessageW(servers_list_, LB_GETCURSEL, 0, 0);
    return selection == LB_ERR ? -1 : static_cast<int>(selection);
}

int McpServerManagerWindow::SelectedGlobalVariableIndex() const {
    const LRESULT selection = SendMessageW(global_variables_list_, LB_GETCURSEL, 0, 0);
    return selection == LB_ERR ? -1 : static_cast<int>(selection);
}

void McpServerManagerWindow::RefreshGlobalVariablesList() {
    const int selected_index = SelectedGlobalVariableIndex();
    SendMessageW(global_variables_list_, LB_RESETCONTENT, 0, 0);
    for (const auto& variable : manager_->global_variables()) {
        std::wstring label = VariableListLabel(variable);
        label += L"  [global]";
        SendMessageW(global_variables_list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (!manager_->global_variables().empty()) {
        const int restore = selected_index >= 0 && static_cast<size_t>(selected_index) < manager_->global_variables().size() ? selected_index : 0;
        SendMessageW(global_variables_list_, LB_SETCURSEL, restore, 0);
    }
}

void McpServerManagerWindow::ReloadSnapshots() {
    const std::string project_id = ActiveProjectId();
    snapshots_ = manager_->GetServerSnapshots(project_id);

    const int selected_index = SelectedIndex();
    SendMessageW(servers_list_, LB_RESETCONTENT, 0, 0);
    for (const auto& snapshot : snapshots_) {
        std::wstring label = Utf8ToWide(snapshot.config.name);
        label += L"  [";
        label += StatusLabel(snapshot.status);
        label += L"]  [";
        label += ScopeLabel(snapshot.config.scope);
        label += L"]";
        label += snapshot.selected_for_project ? L"  [project-selected]" : L"  [not in project]";
        if (!snapshot.config.enabled) {
            label += L"  (disabled)";
        }
        SendMessageW(servers_list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }

    if (!snapshots_.empty()) {
        const int restore = selected_index >= 0 && static_cast<size_t>(selected_index) < snapshots_.size() ? selected_index : 0;
        SendMessageW(servers_list_, LB_SETCURSEL, restore, 0);
    }
    RefreshToolsList();
}

void McpServerManagerWindow::RefreshToolsList() {
    SendMessageW(tools_list_, LB_RESETCONTENT, 0, 0);
    const int index = SelectedIndex();
    if (index < 0 || static_cast<size_t>(index) >= snapshots_.size()) {
        SetWindowTextW(status_label_, L"Select an MCP server to inspect its tools.");
        return;
    }

    const auto& snapshot = snapshots_[static_cast<size_t>(index)];
    for (const auto& tool : snapshot.tools) {
        std::wstring label = Utf8ToWide(tool.title.empty() ? tool.name : (tool.title + " (" + tool.name + ")"));
        SendMessageW(tools_list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }

    std::wstring status = L"Status: " + StatusLabel(snapshot.status);
    status += L"  |  Scope: " + ScopeLabel(snapshot.config.scope);
    status += snapshot.selected_for_project ? L"  |  Active for this project" : L"  |  Not selected for this project";
    if (!snapshot.last_error.empty()) {
        status += L"  |  " + Utf8ToWide(snapshot.last_error);
    }
    SetWindowTextW(status_label_, status.c_str());
}

void McpServerManagerWindow::SaveConfigs(std::vector<McpServerConfig> configs) {
    manager_->SaveConfigs(configs);
    ReloadSnapshots();
}

void McpServerManagerWindow::SaveGlobalVariables(std::vector<McpServerVariable> variables) {
    manager_->SaveGlobalVariables(variables);
    RefreshGlobalVariablesList();
    ReloadSnapshots();
}

bool McpServerManagerWindow::HasDuplicateGlobalVariableName(const std::string& name, int skip_index) const {
    const auto& variables = manager_->global_variables();
    for (size_t i = 0; i < variables.size(); ++i) {
        if (static_cast<int>(i) == skip_index) {
            continue;
        }
        if (variables[i].name == name) {
            return true;
        }
    }
    return false;
}

void McpServerManagerWindow::AddGlobalVariable() {
    const auto result = McpVariableEditorDialog::Show(hwnd_, McpServerVariable{}, false);
    if (!result) {
        return;
    }
    if (HasDuplicateGlobalVariableName(result->name, -1)) {
        MessageBoxW(hwnd_, L"A global variable with that name already exists.", L"Duplicate Variable", MB_OK | MB_ICONERROR);
        return;
    }

    auto variables = manager_->global_variables();
    variables.push_back(*result);
    SaveGlobalVariables(std::move(variables));
}

void McpServerManagerWindow::EditGlobalVariable() {
    const int index = SelectedGlobalVariableIndex();
    const auto& current_variables = manager_->global_variables();
    if (index < 0 || static_cast<size_t>(index) >= current_variables.size()) {
        return;
    }

    const auto result = McpVariableEditorDialog::Show(hwnd_, current_variables[static_cast<size_t>(index)], true);
    if (!result) {
        return;
    }
    if (HasDuplicateGlobalVariableName(result->name, index)) {
        MessageBoxW(hwnd_, L"A global variable with that name already exists.", L"Duplicate Variable", MB_OK | MB_ICONERROR);
        return;
    }

    auto variables = manager_->global_variables();
    variables[static_cast<size_t>(index)] = *result;
    SaveGlobalVariables(std::move(variables));
}

void McpServerManagerWindow::RemoveGlobalVariable() {
    const int index = SelectedGlobalVariableIndex();
    const auto& current_variables = manager_->global_variables();
    if (index < 0 || static_cast<size_t>(index) >= current_variables.size()) {
        return;
    }

    const std::wstring prompt = L"Remove global variable \"" + Utf8ToWide(current_variables[static_cast<size_t>(index)].name) + L"\"?";
    if (MessageBoxW(hwnd_, prompt.c_str(), L"Remove Variable", MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }

    auto variables = manager_->global_variables();
    variables.erase(variables.begin() + index);
    SaveGlobalVariables(std::move(variables));
}

void McpServerManagerWindow::AddServer() {
    McpServerConfig config;
    config.id = MakeId("mcp_server");
    config.scope = McpServerScope::PerProject;
    const auto edited = McpServerEditorDialog::Show(hwnd_, manager_, config, false);
    if (!edited) {
        return;
    }

    auto configs = manager_->configs();
    configs.push_back(*edited);
    SaveConfigs(std::move(configs));
}

void McpServerManagerWindow::EditServer() {
    const int index = SelectedIndex();
    if (index < 0 || static_cast<size_t>(index) >= snapshots_.size()) {
        return;
    }

    const auto edited = McpServerEditorDialog::Show(hwnd_, manager_, snapshots_[static_cast<size_t>(index)].config, true);
    if (!edited) {
        return;
    }

    auto configs = manager_->configs();
    auto it = std::find_if(configs.begin(), configs.end(), [&](const McpServerConfig& item) { return item.id == edited->id; });
    if (it != configs.end()) {
        *it = *edited;
        SaveConfigs(std::move(configs));
    }
}

void McpServerManagerWindow::RemoveServer() {
    const int index = SelectedIndex();
    if (index < 0 || static_cast<size_t>(index) >= snapshots_.size()) {
        return;
    }

    const auto& snapshot = snapshots_[static_cast<size_t>(index)];
    const std::wstring prompt = L"Remove MCP server \"" + Utf8ToWide(snapshot.config.name) + L"\"?";
    if (MessageBoxW(hwnd_, prompt.c_str(), L"Confirm Removal", MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }

    auto configs = manager_->configs();
    configs.erase(std::remove_if(configs.begin(), configs.end(), [&](const McpServerConfig& item) { return item.id == snapshot.config.id; }), configs.end());
    SaveConfigs(std::move(configs));
}

bool McpServerManagerWindow::ConfigureProjectSelection(const McpServerConfig& config, const std::string& project_id) {
    if (project_id.empty()) {
        MessageBoxW(hwnd_, L"Select a project first so the server can be configured for that project.", L"No Active Project", MB_OK | MB_ICONINFORMATION);
        return false;
    }

    auto bindings = manager_->GetProjectBindings(project_id);
    ProjectSetupDialogOptions options;
    options.title = L"Project MCP Server";
    options.accept_label = L"Save";
    options.include_project_name = false;
    options.servers = {config};
    options.global_variables = manager_->global_variables();

    const auto existing = std::find_if(bindings.begin(), bindings.end(), [&](const ProjectMcpServerBinding& binding) { return binding.server_id == config.id; });
    if (existing != bindings.end()) {
        options.initial_bindings.push_back(*existing);
    }

    const auto result = ShowProjectSetupDialog(hwnd_, options);
    if (!result) {
        return false;
    }

    bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [&](const ProjectMcpServerBinding& binding) { return binding.server_id == config.id; }), bindings.end());
    for (const auto& binding : result->bindings) {
        bindings.push_back(binding);
    }
    manager_->SaveProjectBindings(project_id, bindings);
    return true;
}

void McpServerManagerWindow::EditProjectSelection() {
    const std::string project_id = ActiveProjectId();
    if (project_id.empty()) {
        MessageBoxW(hwnd_, L"Select a project first so its MCP servers can be edited.", L"No Active Project", MB_OK | MB_ICONINFORMATION);
        return;
    }

    ProjectSetupDialogOptions options;
    options.title = L"Project MCP Settings";
    options.accept_label = L"Save";
    options.include_project_name = false;
    options.servers = manager_->configs();
    options.global_variables = manager_->global_variables();
    options.initial_bindings = manager_->GetProjectBindings(project_id);

    const auto result = ShowProjectSetupDialog(hwnd_, options);
    if (!result) {
        return;
    }

    manager_->SaveProjectBindings(project_id, result->bindings);
    manager_->ConnectAutoServers(project_id);
    ReloadSnapshots();
}

void McpServerManagerWindow::ConnectSelected() {
    const int index = SelectedIndex();
    if (index < 0 || static_cast<size_t>(index) >= snapshots_.size()) {
        return;
    }

    const std::string project_id = ActiveProjectId();
    auto snapshot = snapshots_[static_cast<size_t>(index)];
    if (!snapshot.selected_for_project) {
        if (!ConfigureProjectSelection(snapshot.config, project_id)) {
            ReloadSnapshots();
            return;
        }
    }

    std::string error;
    if (!manager_->ConnectServer(snapshot.config.id, project_id, &error)) {
        if ((!snapshot.config.variables.empty() || ConfigUsesAnyGlobalVariable(snapshot.config, manager_->global_variables())) && !project_id.empty()) {
            const std::wstring prompt = Utf8ToWide(error) + L"\r\n\r\nOpen the project values for this server now?";
            if (MessageBoxW(hwnd_, prompt.c_str(), L"Connect Failed", MB_YESNO | MB_ICONWARNING) == IDYES) {
                if (ConfigureProjectSelection(snapshot.config, project_id)) {
                    error.clear();
                    if (!manager_->ConnectServer(snapshot.config.id, project_id, &error)) {
                        MessageBoxW(hwnd_, Utf8ToWide(error).c_str(), L"Connect Failed", MB_OK | MB_ICONERROR);
                    }
                }
            } else {
                MessageBoxW(hwnd_, Utf8ToWide(error).c_str(), L"Connect Failed", MB_OK | MB_ICONERROR);
            }
        } else {
            MessageBoxW(hwnd_, Utf8ToWide(error).c_str(), L"Connect Failed", MB_OK | MB_ICONERROR);
        }
    }
    ReloadSnapshots();
}

void McpServerManagerWindow::DisconnectSelected() {
    const int index = SelectedIndex();
    if (index < 0 || static_cast<size_t>(index) >= snapshots_.size()) {
        return;
    }

    const std::string project_id = ActiveProjectId();
    if (project_id.empty() && snapshots_[static_cast<size_t>(index)].config.scope == McpServerScope::PerProject) {
        MessageBoxW(hwnd_, L"Select a project first.", L"No Active Project", MB_OK | MB_ICONINFORMATION);
        return;
    }

    manager_->DisconnectServer(snapshots_[static_cast<size_t>(index)].config.id, project_id);
    ReloadSnapshots();
}

void McpServerManagerWindow::RefreshSelectedTools() {
    const int index = SelectedIndex();
    if (index < 0 || static_cast<size_t>(index) >= snapshots_.size()) {
        return;
    }

    const std::string project_id = ActiveProjectId();
    std::string error;
    if (!manager_->RefreshServerTools(snapshots_[static_cast<size_t>(index)].config.id, project_id, &error)) {
        MessageBoxW(hwnd_, Utf8ToWide(error).c_str(), L"Refresh Failed", MB_OK | MB_ICONERROR);
    }
    ReloadSnapshots();
}

void McpServerManagerWindow::OnCommand(int control_id, int notification_code) {
    switch (control_id) {
    case kGlobalVariablesList:
        if (notification_code == LBN_DBLCLK) {
            EditGlobalVariable();
        }
        break;
    case kAddGlobalVariable:
        AddGlobalVariable();
        break;
    case kEditGlobalVariable:
        EditGlobalVariable();
        break;
    case kRemoveGlobalVariable:
        RemoveGlobalVariable();
        break;
    case kServersList:
        if (notification_code == LBN_SELCHANGE) {
            RefreshToolsList();
        }
        break;
    case kAddServer:
        AddServer();
        break;
    case kEditServer:
        EditServer();
        break;
    case kRemoveServer:
        RemoveServer();
        break;
    case kConnectServer:
        ConnectSelected();
        break;
    case kDisconnectServer:
        DisconnectSelected();
        break;
    case kRefreshTools:
        RefreshSelectedTools();
        break;
    case kProjectServers:
        EditProjectSelection();
        break;
    case kClose:
        DestroyWindow(hwnd_);
        break;
    default:
        break;
    }
}
}  // namespace

HWND CreateMcpServerManagerWindow(HWND owner, McpManager* manager, std::function<std::string()> active_project_id_provider) {
    auto* window = new McpServerManagerWindow(owner, manager, std::move(active_project_id_provider));
    return window->Create(reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr)));
}
