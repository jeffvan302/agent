#include <windows.h>
#include <commctrl.h>
#include <richedit.h>

#include "mcp_manager.h"
#include "mcp_server_manager.h"
#include "openai_client.h"
#include "prompt_dialog.h"
#include "project_setup_dialog.h"
#include "provider_manager.h"
#include "rag_service.h"
#include "rag_service_manager.h"
#include "storage.h"
#include "util.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <windowsx.h>

namespace {
constexpr wchar_t kMainWindowClassName[] = L"AgentDesktopMainWindow";
constexpr UINT kChatDeltaMessage = WM_APP + 1;
constexpr UINT kChatFinishedMessage = WM_APP + 2;
constexpr UINT kToolTraceMessage = WM_APP + 3;
constexpr UINT kMcpChangedMessage = WM_APP + 4;

enum ControlId : int {
    kTree = 3001,
    kNewProject = 3002,
    kNewChat = 3003,
    kRename = 3004,
    kDelete = 3005,
    kModelCombo = 3006,
    kProviders = 3007,
    kMcpServers = 3008,
    kChatSettings = 3009,
    kTranscript = 3010,
    kToolTrace = 3011,
    kInput = 3012,
    kSend = 3013,
    kStatus = 3014,
    kProjectMcp = 3015,
    kRagService = 3016,
};

struct TreeItemData {
    enum class Type {
        Project,
        Chat,
    };

    Type type = Type::Project;
    std::string project_id;
    std::string chat_id;
};

struct ModelSelectionEntry {
    size_t provider_index = 0;
    size_t model_index = 0;
    std::string provider_id;
    std::string model_id;
    std::wstring label;
};

struct ChatDeltaPayload {
    std::string text;
};

struct ChatFinishedPayload {
    bool success = false;
    std::string error;
    std::string project_id;
    std::string chat_id;
    std::vector<MessageRecord> appended_messages;
};

struct ToolTraceEntry {
    std::string title;
    std::string arguments_json;
    std::string result_text;
    bool success = false;
};

struct ToolTracePayload {
    std::string project_id;
    std::string chat_id;
    ToolTraceEntry entry;
};

std::filesystem::path DetermineAppRoot() {
    wchar_t module_path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, module_path, static_cast<DWORD>(std::size(module_path)));
    std::filesystem::path exe_path(module_path);
    std::filesystem::path root = exe_path.parent_path();
    if (root.filename() == L"build") {
        root = root.parent_path();
    }
    return root;
}

size_t EstimateTokenCount(const std::string& text) {
    if (text.empty()) {
        return 0;
    }

    // A conservative cross-provider estimate; exact tokenizers vary by model.
    return std::max<size_t>(1, (text.size() + 2) / 3);
}

size_t EstimateMessageTokens(const MessageRecord& message) {
    size_t tokens = 8;  // Chat message framing overhead.
    tokens += EstimateTokenCount(message.role);
    tokens += EstimateTokenCount(message.content);
    tokens += EstimateTokenCount(message.name);
    tokens += EstimateTokenCount(message.tool_call_id);
    tokens += EstimateTokenCount(message.tool_calls_json);
    return tokens;
}

size_t EstimateToolTokens(const McpExposedTool& tool) {
    size_t tokens = 20;  // Function/tool declaration framing overhead.
    tokens += EstimateTokenCount(tool.alias);
    tokens += EstimateTokenCount(tool.server_name);
    tokens += EstimateTokenCount(tool.tool_name);
    tokens += EstimateTokenCount(tool.title);
    tokens += EstimateTokenCount(tool.description);
    tokens += EstimateTokenCount(tool.input_schema_json);
    return tokens;
}

size_t EstimateRequestInputTokens(const ChatRequestOptions& request, const std::vector<McpExposedTool>& exposed_tools, bool include_tools) {
    size_t tokens = 3;  // Request-level framing overhead.
    if (!request.system_prompt.empty()) {
        MessageRecord system_message;
        system_message.role = "system";
        system_message.content = request.system_prompt;
        tokens += EstimateMessageTokens(system_message);
    }

    for (const auto& message : request.messages) {
        tokens += EstimateMessageTokens(message);
    }

    if (include_tools) {
        for (const auto& tool : exposed_tools) {
            tokens += EstimateToolTokens(tool);
        }
    }
    return tokens;
}

bool CheckContextWindow(HWND owner, const ChatRequestOptions& request, const std::vector<McpExposedTool>& exposed_tools, bool include_tools) {
    if (request.model.context_window <= 0) {
        return true;
    }

    const size_t estimated_input_tokens = EstimateRequestInputTokens(request, exposed_tools, include_tools);
    const size_t reserved_output_tokens = request.max_tokens > 0 ? static_cast<size_t>(request.max_tokens) : 0;
    const size_t estimated_total_tokens = estimated_input_tokens + reserved_output_tokens;
    const size_t context_window = static_cast<size_t>(request.model.context_window);
    if (estimated_total_tokens <= context_window) {
        return true;
    }

    std::wstring message = L"This request is likely too large for the selected model's configured context window.\r\n\r\n";
    message += L"Estimated input tokens: " + std::to_wstring(estimated_input_tokens) + L"\r\n";
    message += L"Reserved output tokens: " + std::to_wstring(reserved_output_tokens) + L"\r\n";
    message += L"Estimated total: " + std::to_wstring(estimated_total_tokens) + L"\r\n";
    message += L"Configured context window: " + std::to_wstring(context_window) + L"\r\n\r\n";
    message += L"Reduce the chat history, lower max tokens, choose a larger-context model, or set the model context window to 0 if it is unknown.";
    MessageBoxW(owner, message.c_str(), L"Context Window Exceeded", MB_OK | MB_ICONWARNING);
    return false;
}

class MainWindow {
public:
    MainWindow();
    HWND Create(HINSTANCE instance);

private:
    static int Scale(HWND hwnd, int value);
    static void RegisterWindowClass(HINSTANCE instance);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

    void OnCreate();
    void LoadState();
    void EnsureDefaultProjectAndChat();
    void ChooseValidSelection();
    void LayoutControls(int width, int height);
    void OnCommand(int control_id, int notification_code);
    void OnNotify(NMHDR* header);
    void CreateProject();
    void CreateChat();
    void RenameSelection();
    void DeleteSelection();
    void OpenProviderManager();
    void OpenRagServiceManager();
    void EditProjectMcpSettings();
    void EditChatSettings();
    void ReloadProjects(const std::string& preferred_project_id, const std::string& preferred_chat_id);
    void RefreshTree();
    void LoadActiveMessages();
    void RefreshModelCombo();
    void OnModelSelectionChanged();
    std::string BuildMcpProjectContext() const;
    void SendCurrentMessage();
    void OnChatDelta(ChatDeltaPayload* payload);
    void OnChatFinished(ChatFinishedPayload* payload);
    void OnToolTrace(ToolTracePayload* payload);
    void OnMcpStateChanged();
    void RenderTranscript();
    void RenderToolTrace();
    void UpdateStatus(const std::wstring& text);
    void SetRequestBusy(bool busy);
    ProjectRecord* FindProject(const std::string& project_id);
    ChatInfo* FindChat(const std::string& project_id, const std::string& chat_id);

    HWND hwnd_ = nullptr;
    HWND tree_ = nullptr;
    HWND new_project_button_ = nullptr;
    HWND new_chat_button_ = nullptr;
    HWND rename_button_ = nullptr;
    HWND delete_button_ = nullptr;
    HWND model_combo_ = nullptr;
    HWND providers_button_ = nullptr;
    HWND mcp_servers_button_ = nullptr;
    HWND project_mcp_button_ = nullptr;
    HWND rag_service_button_ = nullptr;
    HWND chat_settings_button_ = nullptr;
    HWND transcript_ = nullptr;
    HWND tool_trace_ = nullptr;
    HWND input_ = nullptr;
    HWND send_button_ = nullptr;
    HWND status_label_ = nullptr;
    HWND provider_window_ = nullptr;
    HWND mcp_server_window_ = nullptr;
    HWND rag_service_window_ = nullptr;
    HFONT font_ = nullptr;

    AppStorage storage_;
    McpManager mcp_manager_;
    RagService rag_service_;
    std::vector<ProviderConfig> providers_;
    std::vector<ProjectRecord> projects_;
    std::vector<std::unique_ptr<TreeItemData>> tree_items_;
    std::vector<ModelSelectionEntry> model_entries_;
    std::unordered_map<std::string, std::vector<ToolTraceEntry>> tool_traces_by_chat_;

    std::string active_project_id_;
    std::string active_chat_id_;
    std::vector<MessageRecord> active_messages_;
    bool request_in_flight_ = false;
    bool refreshing_tree_ = false;
    std::wstring streaming_preview_;
};

MainWindow::MainWindow() : storage_(DetermineAppRoot()), mcp_manager_(&storage_), rag_service_(&storage_) {}

int MainWindow::Scale(HWND hwnd, int value) {
    return MulDiv(value, GetDpiForWindow(hwnd), 96);
}

void MainWindow::RegisterWindowClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) {
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = &MainWindow::WindowProc;
    wc.lpszClassName = kMainWindowClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
    registered = true;
}

HWND MainWindow::Create(HINSTANCE instance) {
    RegisterWindowClass(instance);
    hwnd_ = CreateWindowExW(
        0,
        kMainWindowClassName,
        L"AI Agent Desktop - Phase 2",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1360,
        860,
        nullptr,
        nullptr,
        instance,
        this);
    return hwnd_;
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    MainWindow* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        self = reinterpret_cast<MainWindow*>(create->lpCreateParams);
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
    case WM_NOTIFY:
        self->OnNotify(reinterpret_cast<NMHDR*>(l_param));
        return 0;
    case kChatDeltaMessage:
        self->OnChatDelta(reinterpret_cast<ChatDeltaPayload*>(l_param));
        return 0;
    case kChatFinishedMessage:
        self->OnChatFinished(reinterpret_cast<ChatFinishedPayload*>(l_param));
        return 0;
    case kToolTraceMessage:
        self->OnToolTrace(reinterpret_cast<ToolTracePayload*>(l_param));
        return 0;
    case kMcpChangedMessage:
        self->OnMcpStateChanged();
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, w_param, l_param);
    }
}

void MainWindow::OnCreate() {
    font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    storage_.EnsureInitialized();
    mcp_manager_.Load();
    mcp_manager_.SetStateChangedCallback([hwnd = hwnd_]() {
        PostMessageW(hwnd, kMcpChangedMessage, 0, 0);
    });

    new_project_button_ = CreateWindowExW(0, L"BUTTON", L"New Project", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kNewProject), nullptr, nullptr);
    new_chat_button_ = CreateWindowExW(0, L"BUTTON", L"New Chat", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kNewChat), nullptr, nullptr);
    rename_button_ = CreateWindowExW(0, L"BUTTON", L"Rename", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRename), nullptr, nullptr);
    delete_button_ = CreateWindowExW(0, L"BUTTON", L"Delete", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kDelete), nullptr, nullptr);

    tree_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kTree), nullptr, nullptr);
    model_combo_ = CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP | CBS_HASSTRINGS, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kModelCombo), nullptr, nullptr);
    providers_button_ = CreateWindowExW(0, L"BUTTON", L"Providers", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kProviders), nullptr, nullptr);
    mcp_servers_button_ = CreateWindowExW(0, L"BUTTON", L"MCP Servers", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kMcpServers), nullptr, nullptr);
    project_mcp_button_ = CreateWindowExW(0, L"BUTTON", L"Project MCP", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kProjectMcp), nullptr, nullptr);
    rag_service_button_ = CreateWindowExW(0, L"BUTTON", L"RAG Service", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagService), nullptr, nullptr);
    chat_settings_button_ = CreateWindowExW(0, L"BUTTON", L"Chat Settings", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kChatSettings), nullptr, nullptr);
    transcript_ = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kTranscript), nullptr, nullptr);
    tool_trace_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, nullptr, WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kToolTrace), nullptr, nullptr);
    input_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kInput), nullptr, nullptr);
    send_button_ = CreateWindowExW(0, L"BUTTON", L"Send", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kSend), nullptr, nullptr);
    status_label_ = CreateWindowExW(0, L"STATIC", L"Initializing...", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kStatus), nullptr, nullptr);

    for (HWND control : {new_project_button_, new_chat_button_, rename_button_, delete_button_, tree_, model_combo_, providers_button_, mcp_servers_button_, project_mcp_button_, rag_service_button_, chat_settings_button_, transcript_, tool_trace_, input_, send_button_, status_label_}) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }

    SendMessageW(transcript_, EM_SETBKGNDCOLOR, 0, GetSysColor(COLOR_WINDOW));
    rag_service_.EnsureInitialized();
    LoadState();
    mcp_manager_.ConnectAutoServers(active_project_id_);
    UpdateStatus(L"Ready.");
}

void MainWindow::LoadState() {
    providers_ = storage_.LoadProviders();
    projects_ = storage_.LoadProjects();
    EnsureDefaultProjectAndChat();
    ChooseValidSelection();
    RefreshTree();
    LoadActiveMessages();
    RefreshModelCombo();
    RenderTranscript();
    RenderToolTrace();
}

void MainWindow::EnsureDefaultProjectAndChat() {
    if (!projects_.empty()) {
        return;
    }

    const ProjectInfo project = storage_.CreateProject("Default Project");
    storage_.CreateChat(project.id, "Chat 1", "", "");
    projects_ = storage_.LoadProjects();
}

void MainWindow::ChooseValidSelection() {
    if (FindChat(active_project_id_, active_chat_id_)) {
        return;
    }

    if (!projects_.empty()) {
        active_project_id_ = projects_.front().info.id;
        if (!projects_.front().chats.empty()) {
            active_chat_id_ = projects_.front().chats.front().id;
        } else {
            active_chat_id_.clear();
        }
    }
}

void MainWindow::LayoutControls(int width, int height) {
    const int margin = Scale(hwnd_, 10);
    const int gutter = Scale(hwnd_, 10);
    const int left_width = std::max(Scale(hwnd_, 250), width / 4);
    const int button_height = Scale(hwnd_, 28);
    const int top_row_height = button_height;
    const int status_height = Scale(hwnd_, 20);
    const int input_height = Scale(hwnd_, 110);
    const int tool_trace_height = Scale(hwnd_, 150);

    const int left_button_width = (left_width - gutter) / 2;
    MoveWindow(new_project_button_, margin, margin, left_button_width, button_height, TRUE);
    MoveWindow(new_chat_button_, margin + left_button_width + gutter, margin, left_button_width, button_height, TRUE);
    MoveWindow(rename_button_, margin, margin + button_height + gutter, left_button_width, button_height, TRUE);
    MoveWindow(delete_button_, margin + left_button_width + gutter, margin + button_height + gutter, left_button_width, button_height, TRUE);
    const int rag_button_y = height - margin - button_height;
    const int tree_top = margin + (button_height + gutter) * 2;
    MoveWindow(tree_, margin, tree_top, left_width, std::max(Scale(hwnd_, 80), rag_button_y - gutter - tree_top), TRUE);
    MoveWindow(rag_service_button_, margin, rag_button_y, left_width, button_height, TRUE);

    const int right_x = margin + left_width + gutter * 2;
    const int right_width = width - right_x - margin;
    const int providers_width = Scale(hwnd_, 100);
    const int mcp_width = Scale(hwnd_, 110);
    const int project_mcp_width = Scale(hwnd_, 110);
    const int settings_width = Scale(hwnd_, 120);
    const int fixed_width = providers_width + mcp_width + project_mcp_width + settings_width + gutter * 4;
    const int combo_width = std::max(Scale(hwnd_, 220), right_width - fixed_width);

    MoveWindow(model_combo_, right_x, margin, combo_width, top_row_height + Scale(hwnd_, 250), TRUE);
    MoveWindow(providers_button_, right_x + combo_width + gutter, margin, providers_width, top_row_height, TRUE);
    MoveWindow(mcp_servers_button_, right_x + combo_width + gutter + providers_width + gutter, margin, mcp_width, top_row_height, TRUE);
    MoveWindow(project_mcp_button_, right_x + combo_width + gutter + providers_width + gutter + mcp_width + gutter, margin, project_mcp_width, top_row_height, TRUE);
    MoveWindow(chat_settings_button_, right_x + combo_width + gutter + providers_width + gutter + mcp_width + gutter + project_mcp_width + gutter, margin, settings_width, top_row_height, TRUE);

    const int transcript_top = margin + top_row_height + gutter;
    const int transcript_height = height - transcript_top - tool_trace_height - input_height - status_height - margin * 2 - gutter * 3;
    MoveWindow(transcript_, right_x, transcript_top, right_width, transcript_height, TRUE);
    MoveWindow(tool_trace_, right_x, transcript_top + transcript_height + gutter, right_width, tool_trace_height, TRUE);
    MoveWindow(input_, right_x, transcript_top + transcript_height + gutter + tool_trace_height + gutter, right_width - Scale(hwnd_, 100) - gutter, input_height, TRUE);
    MoveWindow(send_button_, right_x + right_width - Scale(hwnd_, 100), transcript_top + transcript_height + gutter + tool_trace_height + gutter, Scale(hwnd_, 100), Scale(hwnd_, 34), TRUE);
    MoveWindow(status_label_, right_x, height - margin - status_height, right_width, status_height, TRUE);
}

void MainWindow::OnCommand(int control_id, int notification_code) {
    switch (control_id) {
    case kNewProject:
        CreateProject();
        break;
    case kNewChat:
        CreateChat();
        break;
    case kRename:
        RenameSelection();
        break;
    case kDelete:
        DeleteSelection();
        break;
    case kProviders:
        OpenProviderManager();
        break;
    case kMcpServers:
        if (mcp_server_window_ && IsWindow(mcp_server_window_)) {
            SetForegroundWindow(mcp_server_window_);
        } else {
            mcp_server_window_ = CreateMcpServerManagerWindow(hwnd_, &mcp_manager_, [this]() { return this->active_project_id_; });
        }
        break;
    case kProjectMcp:
        EditProjectMcpSettings();
        break;
    case kRagService:
        OpenRagServiceManager();
        break;
    case kChatSettings:
        EditChatSettings();
        break;
    case kSend:
        SendCurrentMessage();
        break;
    case kModelCombo:
        if (notification_code == CBN_SELCHANGE) {
            OnModelSelectionChanged();
        }
        break;
    default:
        break;
    }
}

void MainWindow::OnNotify(NMHDR* header) {
    if (!header || header->idFrom != kTree) {
        return;
    }
    if (refreshing_tree_) {
        return;
    }

    if (header->code == TVN_SELCHANGEDW) {
        auto* change = reinterpret_cast<NMTREEVIEWW*>(header);
        auto* item = reinterpret_cast<TreeItemData*>(change->itemNew.lParam);
        if (!item) {
            return;
        }

        active_project_id_ = item->project_id;
        mcp_manager_.ConnectAutoServers(active_project_id_);
        if (item->type == TreeItemData::Type::Chat) {
            active_chat_id_ = item->chat_id;
            LoadActiveMessages();
            RefreshModelCombo();
            RenderTranscript();
            RenderToolTrace();
        } else {
            active_chat_id_.clear();
            if (ProjectRecord* project = FindProject(item->project_id); project && !project->chats.empty()) {
                active_chat_id_ = project->chats.front().id;
                LoadActiveMessages();
                RefreshModelCombo();
                RenderTranscript();
                RenderToolTrace();
            }
        }
    }
}

void MainWindow::CreateProject() {
    ProjectSetupDialogOptions options;
    options.title = L"Create Project";
    options.accept_label = L"Create";
    options.project_name = L"New Project";
    options.include_project_name = true;
    options.servers = mcp_manager_.configs();
    options.global_variables = mcp_manager_.global_variables();

    const auto result = ShowProjectSetupDialog(hwnd_, options);
    if (!result || result->project_name.empty()) {
        return;
    }

    const ProjectInfo project = storage_.CreateProject(result->project_name);
    mcp_manager_.SaveProjectBindings(project.id, result->bindings);
    const ChatInfo chat = storage_.CreateChat(project.id, "Chat 1", "", "");
    ReloadProjects(project.id, chat.id);
}

void MainWindow::CreateChat() {
    if (active_project_id_.empty() && !projects_.empty()) {
        active_project_id_ = projects_.front().info.id;
    }
    if (active_project_id_.empty()) {
        return;
    }

    const auto name = ShowPromptDialog(hwnd_, PromptOptions{L"Create Chat", L"Chat name", L"New Chat"});
    if (!name || name->empty()) {
        return;
    }

    std::string provider_id;
    std::string model_id;
    const int selection = ComboBox_GetCurSel(model_combo_);
    if (selection >= 0 && static_cast<size_t>(selection) < model_entries_.size()) {
        provider_id = model_entries_[static_cast<size_t>(selection)].provider_id;
        model_id = model_entries_[static_cast<size_t>(selection)].model_id;
    }

    const ChatInfo chat = storage_.CreateChat(active_project_id_, WideToUtf8(*name), provider_id, model_id);
    ReloadProjects(active_project_id_, chat.id);
}

void MainWindow::RenameSelection() {
    HTREEITEM selected = TreeView_GetSelection(tree_);
    if (!selected) {
        return;
    }

    TVITEMW item{};
    item.mask = TVIF_PARAM;
    item.hItem = selected;
    TreeView_GetItem(tree_, &item);
    auto* data = reinterpret_cast<TreeItemData*>(item.lParam);
    if (!data) {
        return;
    }

    if (data->type == TreeItemData::Type::Project) {
        ProjectRecord* project = FindProject(data->project_id);
        if (!project) {
            return;
        }
        const auto name = ShowPromptDialog(hwnd_, PromptOptions{L"Rename Project", L"Project name", Utf8ToWide(project->info.name)});
        if (!name || name->empty()) {
            return;
        }
        storage_.RenameProject(data->project_id, WideToUtf8(*name));
        ReloadProjects(data->project_id, active_chat_id_);
    } else {
        ChatInfo* chat = FindChat(data->project_id, data->chat_id);
        if (!chat) {
            return;
        }
        const auto name = ShowPromptDialog(hwnd_, PromptOptions{L"Rename Chat", L"Chat name", Utf8ToWide(chat->name)});
        if (!name || name->empty()) {
            return;
        }
        storage_.RenameChat(data->project_id, data->chat_id, WideToUtf8(*name));
        ReloadProjects(data->project_id, data->chat_id);
    }
}

void MainWindow::DeleteSelection() {
    HTREEITEM selected = TreeView_GetSelection(tree_);
    if (!selected) {
        return;
    }

    TVITEMW item{};
    item.mask = TVIF_PARAM;
    item.hItem = selected;
    TreeView_GetItem(tree_, &item);
    auto* data = reinterpret_cast<TreeItemData*>(item.lParam);
    if (!data) {
        return;
    }

    if (data->type == TreeItemData::Type::Project) {
        ProjectRecord* project = FindProject(data->project_id);
        if (!project) {
            return;
        }
        const std::wstring message = L"Delete project \"" + Utf8ToWide(project->info.name) + L"\" and all chats?";
        if (MessageBoxW(hwnd_, message.c_str(), L"Confirm Delete", MB_YESNO | MB_ICONWARNING) != IDYES) {
            return;
        }
        try {
            mcp_manager_.SaveProjectBindings(data->project_id, {});
            storage_.DeleteProject(data->project_id);
        } catch (const std::exception& ex) {
            MessageBoxW(hwnd_, Utf8ToWide(std::string("Could not delete the project: ") + ex.what()).c_str(), L"Delete Failed", MB_OK | MB_ICONERROR);
            ReloadProjects(active_project_id_, active_chat_id_);
            return;
        }
        if (active_project_id_ == data->project_id) {
            active_project_id_.clear();
            active_chat_id_.clear();
            active_messages_.clear();
        }
        ReloadProjects("", "");
    } else {
        ChatInfo* chat = FindChat(data->project_id, data->chat_id);
        if (!chat) {
            return;
        }
        const std::wstring message = L"Delete chat \"" + Utf8ToWide(chat->name) + L"\"?";
        if (MessageBoxW(hwnd_, message.c_str(), L"Confirm Delete", MB_YESNO | MB_ICONWARNING) != IDYES) {
            return;
        }
        try {
            storage_.DeleteChat(data->project_id, data->chat_id);
        } catch (const std::exception& ex) {
            MessageBoxW(hwnd_, Utf8ToWide(std::string("Could not delete the chat: ") + ex.what()).c_str(), L"Delete Failed", MB_OK | MB_ICONERROR);
            ReloadProjects(data->project_id, data->chat_id);
            return;
        }
        ReloadProjects(data->project_id, "");
    }
}

void MainWindow::OpenProviderManager() {
    if (provider_window_ && IsWindow(provider_window_)) {
        SetForegroundWindow(provider_window_);
        return;
    }

    provider_window_ = CreateProviderManagerWindow(hwnd_, &storage_, &providers_, [this]() {
        this->providers_ = this->storage_.LoadProviders();
        this->RefreshModelCombo();
    });
}

void MainWindow::OpenRagServiceManager() {
    if (rag_service_window_ && IsWindow(rag_service_window_)) {
        SetForegroundWindow(rag_service_window_);
        return;
    }

    rag_service_window_ = CreateRagServiceManagerWindow(hwnd_, &rag_service_, [this]() {
        return active_project_id_;
    });
}

void MainWindow::EditProjectMcpSettings() {
    if (active_project_id_.empty()) {
        MessageBoxW(hwnd_, L"Select a project first.", L"No Project Selected", MB_OK | MB_ICONINFORMATION);
        return;
    }

    ProjectRecord* project = FindProject(active_project_id_);
    if (!project) {
        MessageBoxW(hwnd_, L"The selected project could not be found.", L"Project Not Found", MB_OK | MB_ICONERROR);
        return;
    }

    ProjectSetupDialogOptions options;
    options.title = L"Project MCP Settings";
    options.accept_label = L"Save";
    options.project_name = Utf8ToWide(project->info.name);
    options.include_project_name = false;
    options.servers = mcp_manager_.configs();
    options.global_variables = mcp_manager_.global_variables();
    options.initial_bindings = mcp_manager_.GetProjectBindings(active_project_id_);

    const auto result = ShowProjectSetupDialog(hwnd_, options);
    if (!result) {
        return;
    }

    mcp_manager_.SaveProjectBindings(active_project_id_, result->bindings);
    mcp_manager_.ConnectAutoServers(active_project_id_);
    RenderToolTrace();
    UpdateStatus(L"Project MCP settings saved.");
}

void MainWindow::EditChatSettings() {
    ChatInfo* chat = FindChat(active_project_id_, active_chat_id_);
    if (!chat) {
        return;
    }

    auto system_prompt = ShowPromptDialog(hwnd_, PromptOptions{
                                                     L"Chat System Prompt",
                                                     L"System prompt for this chat",
                                                     Utf8ToWide(chat->system_prompt),
                                                     true,
                                                     560,
                                                     250,
                                                 });
    if (!system_prompt) {
        return;
    }

    auto temperature = ShowPromptDialog(hwnd_, PromptOptions{
                                                 L"Temperature",
                                                 L"Temperature (for example 0.2)",
                                                 Utf8ToWide(std::to_string(chat->temperature)),
                                             });
    if (!temperature) {
        return;
    }

    auto max_tokens = ShowPromptDialog(hwnd_, PromptOptions{
                                               L"Max Tokens",
                                               L"Maximum output tokens",
                                               std::to_wstring(chat->max_tokens),
                                           });
    if (!max_tokens) {
        return;
    }

    const auto parsed_temperature = ParseDouble(*temperature);
    const auto parsed_max_tokens = ParseInt(*max_tokens);
    if (!parsed_temperature || !parsed_max_tokens) {
        MessageBoxW(hwnd_, L"Temperature must be a number and max tokens must be a whole number.", L"Invalid Settings", MB_OK | MB_ICONERROR);
        return;
    }

    chat->system_prompt = WideToUtf8(*system_prompt);
    chat->temperature = *parsed_temperature;
    chat->max_tokens = *parsed_max_tokens;
    storage_.SaveChat(active_project_id_, *chat);
    UpdateStatus(L"Chat settings saved.");
}

void MainWindow::ReloadProjects(const std::string& preferred_project_id, const std::string& preferred_chat_id) {
    projects_ = storage_.LoadProjects();
    EnsureDefaultProjectAndChat();
    if (!preferred_project_id.empty()) {
        active_project_id_ = preferred_project_id;
    }
    if (!preferred_chat_id.empty()) {
        active_chat_id_ = preferred_chat_id;
    }
    ChooseValidSelection();
    mcp_manager_.ConnectAutoServers(active_project_id_);
    RefreshTree();
    LoadActiveMessages();
    RefreshModelCombo();
    RenderTranscript();
    RenderToolTrace();
}

void MainWindow::RefreshTree() {
    refreshing_tree_ = true;
    TreeView_DeleteAllItems(tree_);
    tree_items_.clear();

    HTREEITEM selected_item = nullptr;
    for (const auto& project : projects_) {
        auto project_data = std::make_unique<TreeItemData>();
        project_data->type = TreeItemData::Type::Project;
        project_data->project_id = project.info.id;

        TVINSERTSTRUCTW insert{};
        insert.hParent = TVI_ROOT;
        insert.hInsertAfter = TVI_LAST;
        insert.item.mask = TVIF_TEXT | TVIF_PARAM;
        std::wstring project_name = Utf8ToWide(project.info.name);
        insert.item.pszText = project_name.data();
        insert.item.lParam = reinterpret_cast<LPARAM>(project_data.get());
        HTREEITEM project_item = TreeView_InsertItem(tree_, &insert);
        tree_items_.push_back(std::move(project_data));

        if (project.info.id == active_project_id_ && active_chat_id_.empty()) {
            selected_item = project_item;
        }

        for (const auto& chat : project.chats) {
            auto chat_data = std::make_unique<TreeItemData>();
            chat_data->type = TreeItemData::Type::Chat;
            chat_data->project_id = project.info.id;
            chat_data->chat_id = chat.id;

            TVINSERTSTRUCTW child{};
            child.hParent = project_item;
            child.hInsertAfter = TVI_LAST;
            child.item.mask = TVIF_TEXT | TVIF_PARAM;
            std::wstring chat_name = Utf8ToWide(chat.name);
            child.item.pszText = chat_name.data();
            child.item.lParam = reinterpret_cast<LPARAM>(chat_data.get());
            HTREEITEM chat_item = TreeView_InsertItem(tree_, &child);
            tree_items_.push_back(std::move(chat_data));

            if (project.info.id == active_project_id_ && chat.id == active_chat_id_) {
                selected_item = chat_item;
            }
        }

        TreeView_Expand(tree_, project_item, TVE_EXPAND);
    }

    if (selected_item) {
        TreeView_SelectItem(tree_, selected_item);
    }
    refreshing_tree_ = false;
}

void MainWindow::LoadActiveMessages() {
    if (active_project_id_.empty() || active_chat_id_.empty()) {
        active_messages_.clear();
        return;
    }
    active_messages_ = storage_.LoadMessages(active_project_id_, active_chat_id_);
}

void MainWindow::RefreshModelCombo() {
    ComboBox_ResetContent(model_combo_);
    model_entries_.clear();

    for (size_t provider_index = 0; provider_index < providers_.size(); ++provider_index) {
        const auto& provider = providers_[provider_index];
        for (size_t model_index = 0; model_index < provider.models.size(); ++model_index) {
            const auto& model = provider.models[model_index];
            ModelSelectionEntry entry;
            entry.provider_index = provider_index;
            entry.model_index = model_index;
            entry.provider_id = provider.id;
            entry.model_id = model.id;

            std::wstring label = Utf8ToWide(provider.name + " / " + model.display_name);
            if (model.context_window > 0) {
                label += L"  ";
                label += std::to_wstring(model.context_window);
                label += L" ctx";
            }
            if (model.supports_streaming) {
                label += L"  [stream]";
            }
            if (model.supports_tools) {
                label += L" [tools]";
            }
            if (model.supports_vision) {
                label += L" [vision]";
            }

            entry.label = label;
            model_entries_.push_back(entry);
            ComboBox_AddString(model_combo_, label.c_str());
        }
    }

    int preferred = -1;
    if (ChatInfo* chat = FindChat(active_project_id_, active_chat_id_)) {
        for (size_t i = 0; i < model_entries_.size(); ++i) {
            if (model_entries_[i].provider_id == chat->provider_id && model_entries_[i].model_id == chat->model_id) {
                preferred = static_cast<int>(i);
                break;
            }
        }

        if (preferred < 0 && chat->provider_id.empty() && chat->model_id.empty() && !model_entries_.empty()) {
            preferred = 0;
            chat->provider_id = model_entries_[0].provider_id;
            chat->model_id = model_entries_[0].model_id;
            storage_.SaveChat(active_project_id_, *chat);
        }
    }

    ComboBox_SetCurSel(model_combo_, preferred);
    EnableWindow(model_combo_, !model_entries_.empty());
    if (model_entries_.empty()) {
        UpdateStatus(L"Configure at least one provider and model to start chatting.");
    }
}

void MainWindow::OnModelSelectionChanged() {
    ChatInfo* chat = FindChat(active_project_id_, active_chat_id_);
    const int selection = ComboBox_GetCurSel(model_combo_);
    if (!chat || selection < 0 || static_cast<size_t>(selection) >= model_entries_.size()) {
        return;
    }

    chat->provider_id = model_entries_[static_cast<size_t>(selection)].provider_id;
    chat->model_id = model_entries_[static_cast<size_t>(selection)].model_id;
    storage_.SaveChat(active_project_id_, *chat);
    UpdateStatus(L"Active model updated for this chat.");
}

std::string MainWindow::BuildMcpProjectContext() const {
    if (active_project_id_.empty()) {
        return {};
    }

    const auto bindings = mcp_manager_.GetProjectBindings(active_project_id_);
    if (bindings.empty()) {
        return {};
    }

    std::unordered_map<std::string, std::string> values;
    for (const auto& binding : bindings) {
        for (const auto& variable : binding.variables) {
            const std::string name = Trim(variable.name);
            const std::string value = Trim(variable.value);
            if (!name.empty() && !value.empty()) {
                values[name] = value;
            }
        }
    }

    if (values.empty()) {
        return {};
    }

    struct ContextVariable {
        std::string name;
        std::string value;
        std::string description;
    };

    std::vector<ContextVariable> context_values;
    std::vector<std::string> emitted_names;
    const auto add_variable = [&](const McpServerVariable& variable) {
        if (!variable.inject_into_context || variable.name.empty()) {
            return;
        }
        if (std::find(emitted_names.begin(), emitted_names.end(), variable.name) != emitted_names.end()) {
            return;
        }

        const auto value_it = values.find(variable.name);
        if (value_it == values.end() || Trim(value_it->second).empty()) {
            return;
        }

        ContextVariable context_variable;
        context_variable.name = variable.name;
        context_variable.value = value_it->second;
        context_variable.description = Trim(variable.description);
        context_values.push_back(std::move(context_variable));
        emitted_names.push_back(variable.name);
    };

    for (const auto& variable : mcp_manager_.global_variables()) {
        add_variable(variable);
    }

    const auto& configs = mcp_manager_.configs();
    for (const auto& binding : bindings) {
        const auto config_it = std::find_if(configs.begin(), configs.end(), [&](const McpServerConfig& config) {
            return config.id == binding.server_id;
        });
        if (config_it == configs.end()) {
            continue;
        }
        for (const auto& variable : config_it->variables) {
            add_variable(variable);
        }
    }

    if (context_values.empty()) {
        return {};
    }

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

void MainWindow::SendCurrentMessage() {
    if (request_in_flight_) {
        return;
    }

    ChatInfo* chat = FindChat(active_project_id_, active_chat_id_);
    const int selection = ComboBox_GetCurSel(model_combo_);
    if (!chat) {
        MessageBoxW(hwnd_, L"Select a chat first.", L"No Chat Selected", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (selection < 0 || static_cast<size_t>(selection) >= model_entries_.size()) {
        MessageBoxW(hwnd_, L"Configure and select a model first.", L"No Model Selected", MB_OK | MB_ICONINFORMATION);
        return;
    }

    const std::wstring input_text = TrimWide(GetWindowTextString(input_));
    if (input_text.empty()) {
        return;
    }

    MessageRecord user_message;
    user_message.role = "user";
    user_message.content = WideToUtf8(input_text);
    user_message.created_at = CurrentTimestampUtc();

    const ModelSelectionEntry selected = model_entries_[static_cast<size_t>(selection)];
    ChatRequestOptions request;
    request.provider = providers_[selected.provider_index];
    request.model = providers_[selected.provider_index].models[selected.model_index];
    request.system_prompt = chat->system_prompt;
    request.temperature = chat->temperature;
    request.max_tokens = chat->max_tokens;
    request.messages = active_messages_;
    request.messages.push_back(user_message);
    const std::string mcp_project_context = BuildMcpProjectContext();
    if (!mcp_project_context.empty()) {
        if (!request.system_prompt.empty()) {
            request.system_prompt += "\n\n";
        }
        request.system_prompt += mcp_project_context;
    }
    const std::string rag_context = rag_service_.BuildContextBlock(active_project_id_, user_message.content, 12);
    if (!rag_context.empty()) {
        if (!request.system_prompt.empty()) {
            request.system_prompt += "\n\n";
        }
        request.system_prompt += rag_context;
    }
    const std::string project_id = active_project_id_;
    const std::string chat_id = active_chat_id_;
    const auto exposed_tools = mcp_manager_.GetExposedToolsForProject(project_id);
    const bool include_tools = !exposed_tools.empty() && request.model.supports_tools;

    if (!CheckContextWindow(hwnd_, request, exposed_tools, include_tools)) {
        return;
    }

    active_messages_ = request.messages;
    storage_.SaveMessages(active_project_id_, active_chat_id_, active_messages_);

    SetWindowTextW(input_, L"");
    streaming_preview_.clear();
    SetRequestBusy(true);
    RenderTranscript();
    UpdateStatus(L"Sending request...");

    const size_t existing_count = request.messages.size();

    if (!include_tools) {
        std::thread([hwnd = hwnd_, request, project_id, chat_id]() {
            const auto result = OpenAIClient::StreamChat(request, [hwnd](const std::string& piece) {
                auto* payload = new ChatDeltaPayload;
                payload->text = piece;
                PostMessageW(hwnd, kChatDeltaMessage, 0, reinterpret_cast<LPARAM>(payload));
            });

            auto* final_payload = new ChatFinishedPayload;
            final_payload->success = result.success;
            final_payload->project_id = project_id;
            final_payload->chat_id = chat_id;
            final_payload->error = result.error;
            if (result.success && !result.full_text.empty()) {
                MessageRecord assistant_message;
                assistant_message.role = "assistant";
                assistant_message.content = result.full_text;
                assistant_message.created_at = CurrentTimestampUtc();
                final_payload->appended_messages.push_back(std::move(assistant_message));
            }
            PostMessageW(hwnd, kChatFinishedMessage, 0, reinterpret_cast<LPARAM>(final_payload));
        }).detach();
        return;
    }

    std::thread([hwnd = hwnd_, request, project_id, chat_id, existing_count, exposed_tools, mcp_manager = &mcp_manager_]() {
        constexpr int kMaxToolRounds = 8;

        std::vector<ChatToolDefinition> tool_definitions;
        tool_definitions.reserve(exposed_tools.size());
        std::unordered_map<std::string, McpExposedTool> tool_lookup;
        for (const auto& exposed : exposed_tools) {
            ChatToolDefinition definition;
            definition.name = exposed.alias;
            definition.description = exposed.description.empty()
                ? ("MCP tool from server \"" + exposed.server_name + "\" named \"" + exposed.tool_name + "\".")
                : (exposed.description + " (MCP server: " + exposed.server_name + ", tool: " + exposed.tool_name + ")");
            definition.parameters_json = exposed.input_schema_json;
            tool_definitions.push_back(std::move(definition));
            tool_lookup[exposed.alias] = exposed;
        }

        std::vector<MessageRecord> working_messages = request.messages;
        std::string error;
        bool success = false;

        for (int round = 0; round < kMaxToolRounds; ++round) {
            ChatRequestOptions loop_request = request;
            loop_request.messages = working_messages;

            const auto completion = OpenAIClient::StreamToolAwareCompletion(loop_request, tool_definitions, [hwnd](const std::string& piece) {
                auto* payload = new ChatDeltaPayload;
                payload->text = piece;
                PostMessageW(hwnd, kChatDeltaMessage, 0, reinterpret_cast<LPARAM>(payload));
            });
            if (!completion.success) {
                error = completion.error;
                break;
            }

            if (!completion.tool_calls.empty()) {
                MessageRecord assistant_message;
                assistant_message.role = "assistant";
                assistant_message.content = completion.assistant_text;
                assistant_message.created_at = CurrentTimestampUtc();
                if (!completion.raw_message_json.empty()) {
                    try {
                        const auto message_json = nlohmann::json::parse(completion.raw_message_json);
                        if (message_json.contains("tool_calls")) {
                            assistant_message.tool_calls_json = message_json["tool_calls"].dump();
                        }
                    } catch (...) {
                    }
                }
                if (assistant_message.tool_calls_json.empty()) {
                    nlohmann::json tool_calls_json = nlohmann::json::array();
                    for (const auto& tool_call : completion.tool_calls) {
                        tool_calls_json.push_back({
                            {"id", tool_call.id},
                            {"type", "function"},
                            {"function", {
                                {"name", tool_call.name},
                                {"arguments", tool_call.arguments_json},
                            }},
                        });
                    }
                    assistant_message.tool_calls_json = tool_calls_json.dump();
                }
                working_messages.push_back(std::move(assistant_message));

                for (const auto& tool_call : completion.tool_calls) {
                    McpToolCallResult result;
                    std::string trace_arguments = tool_call.arguments_json;
                    if (!tool_call.arguments_valid) {
                        trace_arguments = tool_call.original_arguments_json.empty()
                            ? tool_call.arguments_json
                            : tool_call.original_arguments_json;
                        std::string shown_arguments = trace_arguments;
                        if (shown_arguments.size() > 2000) {
                            shown_arguments = shown_arguments.substr(0, 2000) + "...";
                        }

                        std::ostringstream error_stream;
                        error_stream << "Tool call was not executed because the model returned invalid JSON arguments for \""
                                     << tool_call.name
                                     << "\". Tool arguments must be a valid JSON object.";
                        if (!tool_call.arguments_error.empty()) {
                            error_stream << "\nParser error: " << tool_call.arguments_error;
                        }
                        if (!shown_arguments.empty()) {
                            error_stream << "\nOriginal arguments: " << shown_arguments;
                        }
                        error_stream << "\nRetry the tool call with valid JSON object arguments.";

                        result.success = false;
                        result.is_tool_error = true;
                        result.content_text = error_stream.str();
                        result.raw_result_json = nlohmann::json{
                            {"error", result.content_text},
                            {"invalid_arguments", shown_arguments},
                        }.dump(2);
                    } else {
                        result = mcp_manager->CallExposedTool(project_id, tool_call.name, tool_call.arguments_json);
                    }

                    auto* trace_payload = new ToolTracePayload;
                    trace_payload->project_id = project_id;
                    trace_payload->chat_id = chat_id;
                    const auto tool_it = tool_lookup.find(tool_call.name);
                    if (tool_it != tool_lookup.end()) {
                        trace_payload->entry.title = tool_it->second.server_name + " / " + tool_it->second.tool_name;
                    } else {
                        trace_payload->entry.title = tool_call.name;
                    }
                    trace_payload->entry.arguments_json = trace_arguments;
                    trace_payload->entry.result_text = result.content_text;
                    trace_payload->entry.success = result.success && !result.is_tool_error;
                    PostMessageW(hwnd, kToolTraceMessage, 0, reinterpret_cast<LPARAM>(trace_payload));

                    MessageRecord tool_message;
                    tool_message.role = "tool";
                    tool_message.name = tool_call.name;
                    tool_message.tool_call_id = tool_call.id;
                    tool_message.content = result.content_text;
                    tool_message.created_at = CurrentTimestampUtc();
                    working_messages.push_back(std::move(tool_message));
                }
                continue;
            }

            if (!completion.assistant_text.empty()) {
                MessageRecord assistant_message;
                assistant_message.role = "assistant";
                assistant_message.content = completion.assistant_text;
                assistant_message.created_at = CurrentTimestampUtc();
                working_messages.push_back(std::move(assistant_message));
            }
            success = true;
            break;
        }

        if (!success && error.empty()) {
            error = "The model exceeded the MCP tool-call loop limit.";
        }

        auto* final_payload = new ChatFinishedPayload;
        final_payload->success = success;
        final_payload->project_id = project_id;
        final_payload->chat_id = chat_id;
        final_payload->error = error;
        for (size_t i = existing_count; i < working_messages.size(); ++i) {
            final_payload->appended_messages.push_back(working_messages[i]);
        }
        PostMessageW(hwnd, kChatFinishedMessage, 0, reinterpret_cast<LPARAM>(final_payload));
    }).detach();
}

void MainWindow::OnChatDelta(ChatDeltaPayload* payload) {
    std::unique_ptr<ChatDeltaPayload> guard(payload);
    streaming_preview_ += Utf8ToWide(payload->text);
    RenderTranscript();
}

void MainWindow::OnChatFinished(ChatFinishedPayload* payload) {
    std::unique_ptr<ChatFinishedPayload> guard(payload);
    SetRequestBusy(false);

    if (!payload->appended_messages.empty()) {
        if (payload->project_id == active_project_id_ && payload->chat_id == active_chat_id_) {
            active_messages_.insert(active_messages_.end(), payload->appended_messages.begin(), payload->appended_messages.end());
            storage_.SaveMessages(active_project_id_, active_chat_id_, active_messages_);
        } else {
            auto messages = storage_.LoadMessages(payload->project_id, payload->chat_id);
            messages.insert(messages.end(), payload->appended_messages.begin(), payload->appended_messages.end());
            storage_.SaveMessages(payload->project_id, payload->chat_id, messages);
        }
    }

    streaming_preview_.clear();
    RenderTranscript();
    RenderToolTrace();

    if (payload->success) {
        UpdateStatus(L"Response complete.");
    } else {
        UpdateStatus(Utf8ToWide("Request failed: " + payload->error));
        MessageBoxW(hwnd_, Utf8ToWide(payload->error).c_str(), L"Request Failed", MB_OK | MB_ICONERROR);
    }
}

void MainWindow::OnToolTrace(ToolTracePayload* payload) {
    std::unique_ptr<ToolTracePayload> guard(payload);
    tool_traces_by_chat_[payload->chat_id].push_back(std::move(payload->entry));
    if (payload->project_id == active_project_id_ && payload->chat_id == active_chat_id_) {
        RenderToolTrace();
    }
}

void MainWindow::OnMcpStateChanged() {
    const auto tools = mcp_manager_.GetExposedToolsForProject(active_project_id_);
    if (!request_in_flight_) {
        if (tools.empty()) {
            UpdateStatus(L"No connected MCP tools available for this project.");
        } else {
            UpdateStatus(std::wstring(L"Connected MCP tools available: ") + std::to_wstring(tools.size()));
        }
    }
    RenderToolTrace();
}

void MainWindow::RenderTranscript() {
    std::wstring transcript;
    if (active_chat_id_.empty()) {
        transcript = L"Create or select a chat to begin.";
    } else {
        for (const auto& message : active_messages_) {
            if (message.role == "tool") {
                continue;
            }
            if (message.role == "assistant" && message.content.empty()) {
                continue;
            }

            transcript += message.role == "user" ? L"You" : L"Assistant";
            transcript += L":\r\n";
            transcript += Utf8ToWide(message.content);
            transcript += L"\r\n\r\n";
        }

        if (request_in_flight_) {
            transcript += L"Assistant:\r\n";
            transcript += streaming_preview_;
        }
    }

    SetWindowTextW(transcript_, transcript.c_str());
    SendMessageW(transcript_, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    SendMessageW(transcript_, EM_SCROLLCARET, 0, 0);
    SendMessageW(transcript_, WM_VSCROLL, SB_BOTTOM, 0);
}

void MainWindow::RenderToolTrace() {
    TreeView_DeleteAllItems(tool_trace_);

    const auto exposed_tools = mcp_manager_.GetExposedToolsForProject(active_project_id_);
    auto traces_it = tool_traces_by_chat_.find(active_chat_id_);

    TVINSERTSTRUCTW available_root{};
    available_root.hParent = TVI_ROOT;
    available_root.hInsertAfter = TVI_LAST;
    available_root.item.mask = TVIF_TEXT;
    std::wstring available_label = L"Available MCP Tools";
    available_root.item.pszText = available_label.data();
    HTREEITEM available_root_item = TreeView_InsertItem(tool_trace_, &available_root);

    if (exposed_tools.empty()) {
        TVINSERTSTRUCTW empty_tool{};
        empty_tool.hParent = available_root_item;
        empty_tool.hInsertAfter = TVI_LAST;
        empty_tool.item.mask = TVIF_TEXT;
        std::wstring label = L"No connected MCP tools selected for this project.";
        empty_tool.item.pszText = label.data();
        TreeView_InsertItem(tool_trace_, &empty_tool);
    } else {
        for (const auto& tool : exposed_tools) {
            TVINSERTSTRUCTW tool_item{};
            tool_item.hParent = available_root_item;
            tool_item.hInsertAfter = TVI_LAST;
            tool_item.item.mask = TVIF_TEXT;
            std::wstring label = Utf8ToWide(tool.server_name + " / " + (tool.title.empty() ? tool.tool_name : tool.title));
            tool_item.item.pszText = label.data();
            HTREEITEM tool_root = TreeView_InsertItem(tool_trace_, &tool_item);

            TVINSERTSTRUCTW alias_item{};
            alias_item.hParent = tool_root;
            alias_item.hInsertAfter = TVI_LAST;
            alias_item.item.mask = TVIF_TEXT;
            std::wstring alias_label = L"Alias: " + Utf8ToWide(tool.alias);
            alias_item.item.pszText = alias_label.data();
            TreeView_InsertItem(tool_trace_, &alias_item);

            if (!tool.description.empty()) {
                TVINSERTSTRUCTW desc_item{};
                desc_item.hParent = tool_root;
                desc_item.hInsertAfter = TVI_LAST;
                desc_item.item.mask = TVIF_TEXT;
                std::wstring desc_label = L"Description: " + Utf8ToWide(tool.description);
                desc_item.item.pszText = desc_label.data();
                TreeView_InsertItem(tool_trace_, &desc_item);
            }

            TreeView_Expand(tool_trace_, tool_root, TVE_EXPAND);
        }
    }
    TreeView_Expand(tool_trace_, available_root_item, TVE_EXPAND);

    TVINSERTSTRUCTW trace_root{};
    trace_root.hParent = TVI_ROOT;
    trace_root.hInsertAfter = TVI_LAST;
    trace_root.item.mask = TVIF_TEXT;
    std::wstring trace_label = L"Tool Call Trace";
    trace_root.item.pszText = trace_label.data();
    HTREEITEM trace_root_item = TreeView_InsertItem(tool_trace_, &trace_root);

    if (active_chat_id_.empty() || traces_it == tool_traces_by_chat_.end() || traces_it->second.empty()) {
        TVINSERTSTRUCTW empty_trace{};
        empty_trace.hParent = trace_root_item;
        empty_trace.hInsertAfter = TVI_LAST;
        empty_trace.item.mask = TVIF_TEXT;
        std::wstring label = L"No MCP tool calls yet in this chat.";
        empty_trace.item.pszText = label.data();
        TreeView_InsertItem(tool_trace_, &empty_trace);
        TreeView_Expand(tool_trace_, trace_root_item, TVE_EXPAND);
        return;
    }

    int index = 1;
    for (const auto& trace : traces_it->second) {
        TVINSERTSTRUCTW parent{};
        parent.hParent = trace_root_item;
        parent.hInsertAfter = TVI_LAST;
        parent.item.mask = TVIF_TEXT;
        std::wstring title = L"Tool " + std::to_wstring(index++) + L": " + Utf8ToWide(trace.title) + (trace.success ? L" [ok]" : L" [error]");
        parent.item.pszText = title.data();
        HTREEITEM parent_item = TreeView_InsertItem(tool_trace_, &parent);

        TVINSERTSTRUCTW args{};
        args.hParent = parent_item;
        args.hInsertAfter = TVI_LAST;
        args.item.mask = TVIF_TEXT;
        std::wstring args_text = L"Arguments: " + Utf8ToWide(trace.arguments_json);
        args.item.pszText = args_text.data();
        TreeView_InsertItem(tool_trace_, &args);

        TVINSERTSTRUCTW result{};
        result.hParent = parent_item;
        result.hInsertAfter = TVI_LAST;
        result.item.mask = TVIF_TEXT;
        std::wstring result_text = L"Result: " + Utf8ToWide(trace.result_text);
        result.item.pszText = result_text.data();
        TreeView_InsertItem(tool_trace_, &result);

        TreeView_Expand(tool_trace_, parent_item, TVE_EXPAND);
    }
    TreeView_Expand(tool_trace_, trace_root_item, TVE_EXPAND);
}

void MainWindow::UpdateStatus(const std::wstring& text) {
    SetWindowTextW(status_label_, text.c_str());
}

void MainWindow::SetRequestBusy(bool busy) {
    request_in_flight_ = busy;
    EnableWindow(send_button_, !busy);
    EnableWindow(input_, !busy);
    EnableWindow(tree_, !busy);
    EnableWindow(new_project_button_, !busy);
    EnableWindow(new_chat_button_, !busy);
    EnableWindow(rename_button_, !busy);
    EnableWindow(delete_button_, !busy);
    EnableWindow(model_combo_, !busy && !model_entries_.empty());
    EnableWindow(providers_button_, !busy);
    EnableWindow(mcp_servers_button_, !busy);
    EnableWindow(project_mcp_button_, !busy);
    EnableWindow(chat_settings_button_, !busy);
}

ProjectRecord* MainWindow::FindProject(const std::string& project_id) {
    for (auto& project : projects_) {
        if (project.info.id == project_id) {
            return &project;
        }
    }
    return nullptr;
}

ChatInfo* MainWindow::FindChat(const std::string& project_id, const std::string& chat_id) {
    if (project_id.empty() || chat_id.empty()) {
        return nullptr;
    }

    if (ProjectRecord* project = FindProject(project_id)) {
        for (auto& chat : project->chats) {
            if (chat.id == chat_id) {
                return &chat;
            }
        }
    }
    return nullptr;
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    LoadLibraryW(L"Msftedit.dll");

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_TREEVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    MainWindow window;
    HWND hwnd = window.Create(instance);
    if (!hwnd) {
        return 1;
    }

    ShowWindow(hwnd, show_command);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
