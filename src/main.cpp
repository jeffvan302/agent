#include <windows.h>
#include <commctrl.h>
#include <richedit.h>

#include "mcp_manager.h"
#include "mcp_server_manager.h"
#include "openai_client.h"
#include "prompt_dialog.h"
#include "project_setup_dialog.h"
#include "project_settings_dialog.h"
#include "provider_manager.h"
#include "rag_service.h"
#include "rag_service_manager.h"
#include "context_compression.h"
#include "context_compression_manager.h"
#include "storage.h"
#include "util.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cwchar>
#include <cwctype>
#include <exception>
#include <fstream>
#include <filesystem>
#include <memory>
#include <optional>
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
    kContextWindow = 3017,
    kCompress = 3018,
    kContextMessages = 3019,
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

size_t EstimateToolTokens(const ChatToolDefinition& tool) {
    size_t tokens = 20;  // Function/tool declaration framing overhead.
    tokens += EstimateTokenCount(tool.name);
    tokens += EstimateTokenCount(tool.description);
    tokens += EstimateTokenCount(tool.parameters_json);
    return tokens;
}

size_t EstimateRequestInputTokens(const ChatRequestOptions& request, const std::vector<McpExposedTool>& exposed_tools, const std::vector<ChatToolDefinition>& extra_tools, bool include_tools) {
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
        for (const auto& tool : extra_tools) {
            tokens += EstimateToolTokens(tool);
        }
    }
    return tokens;
}

size_t EstimateRequestInputTokens(const ChatRequestOptions& request, const std::vector<McpExposedTool>& exposed_tools, bool include_tools) {
    return EstimateRequestInputTokens(request, exposed_tools, {}, include_tools);
}

bool CheckContextWindow(HWND owner, const ChatRequestOptions& request, const std::vector<McpExposedTool>& exposed_tools, const std::vector<ChatToolDefinition>& extra_tools, bool include_tools) {
    if (request.model.context_window <= 0) {
        return true;
    }

    const size_t estimated_input_tokens = EstimateRequestInputTokens(request, exposed_tools, extra_tools, include_tools);
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

bool CheckContextWindow(HWND owner, const ChatRequestOptions& request, const std::vector<McpExposedTool>& exposed_tools, bool include_tools) {
    return CheckContextWindow(owner, request, exposed_tools, {}, include_tools);
}

constexpr wchar_t kScrollableTextWindowClassName[] = L"AgentScrollableTextPreviewWindow";

struct ScrollableTextWindowState {
    HWND edit = nullptr;
    HFONT font = nullptr;
    std::wstring text;
};

int ScaleForWindow(HWND hwnd, int value) {
    return MulDiv(value, GetDpiForWindow(hwnd), 96);
}

LRESULT CALLBACK ScrollableTextWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* state = reinterpret_cast<ScrollableTextWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        state = reinterpret_cast<ScrollableTextWindowState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_CREATE: {
        state->font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        state->edit = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            MSFTEDIT_CLASS,
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
            0,
            0,
            0,
            0,
            hwnd,
            nullptr,
            nullptr,
            nullptr);
        SendMessageW(state->edit, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
        SendMessageW(state->edit, EM_SETBKGNDCOLOR, 0, GetSysColor(COLOR_WINDOW));
        SetWindowTextW(state->edit, state->text.c_str());
        return 0;
    }
    case WM_SIZE:
        if (state && state->edit) {
            const int margin = ScaleForWindow(hwnd, 10);
            MoveWindow(state->edit, margin, margin, LOWORD(l_param) - margin * 2, HIWORD(l_param) - margin * 2, TRUE);
        }
        return 0;
    case WM_DESTROY:
        delete state;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, w_param, l_param);
    }
}

void ShowScrollableTextWindow(HWND owner, const std::wstring& title, const std::wstring& text) {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = &ScrollableTextWindowProc;
        wc.lpszClassName = kScrollableTextWindowClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        RegisterClassExW(&wc);
        registered = true;
    }

    auto* state = new ScrollableTextWindowState;
    state->text = text;

    HWND window = CreateWindowExW(
        WS_EX_APPWINDOW,
        kScrollableTextWindowClassName,
        title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        900,
        650,
        owner,
        nullptr,
        instance,
        state);
    if (!window) {
        delete state;
        MessageBoxW(owner, text.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
}

constexpr wchar_t kContextMessagesWindowClassName[] = L"AgentContextMessagesDebugWindow";
constexpr int kContextMessagesListControlId = 3901;

struct ContextMessagesDebugItem {
    std::wstring label;
    std::wstring detail;
};

struct ContextMessagesWindowState {
    HWND list = nullptr;
    HWND detail = nullptr;
    HFONT font = nullptr;
    std::vector<ContextMessagesDebugItem> items;
};

std::string SquashForPreview(const std::string& text, size_t limit = 80) {
    std::string preview;
    preview.reserve(std::min(text.size(), limit));
    bool previous_space = false;
    for (char ch : text) {
        const bool is_space = ch == '\r' || ch == '\n' || ch == '\t' || ch == ' ';
        if (is_space) {
            if (!previous_space) {
                preview.push_back(' ');
            }
            previous_space = true;
        } else {
            preview.push_back(ch);
            previous_space = false;
        }
        if (preview.size() >= limit) {
            preview += "...";
            break;
        }
    }
    preview = Trim(preview);
    return preview.empty() ? "(empty)" : preview;
}

std::string PrettyJsonOrRaw(const std::string& value) {
    if (Trim(value).empty()) {
        return "(empty)";
    }
    try {
        return nlohmann::json::parse(value).dump(2);
    } catch (...) {
        return value;
    }
}

constexpr char kRagListLibrariesToolName[] = "rag_list_libraries";
constexpr char kRagSearchToolName[] = "rag_search";
constexpr char kRagGetDocumentToolName[] = "rag_get_document";
constexpr char kRagIngestGeneratedDocumentToolName[] = "rag_ingest_generated_document";
constexpr char kRagWriteDocumentToDriveToolName[] = "rag_write_document_to_drive";

struct RagToolLibrary {
    RagLibraryConfig library;
    ProjectRagBinding binding;
};

std::string RagStorageModeLabel(RagDocumentStorageMode mode) {
    switch (mode) {
    case RagDocumentStorageMode::CopyIntoRagStore:
        return "copy_into_rag_store";
    case RagDocumentStorageMode::ReferenceInPlace:
        return "reference_in_place";
    case RagDocumentStorageMode::CopyAndTrackOriginal:
    default:
        return "copy_and_track_original";
    }
}

nlohmann::json JsonObjectSchema(std::initializer_list<std::pair<const char*, nlohmann::json>> properties, std::vector<std::string> required = {}) {
    nlohmann::json props = nlohmann::json::object();
    for (const auto& property : properties) {
        props[property.first] = property.second;
    }
    nlohmann::json schema = {
        {"type", "object"},
        {"properties", props},
        {"additionalProperties", false},
    };
    if (!required.empty()) {
        schema["required"] = required;
    }
    return schema;
}

nlohmann::json ParseJsonOrRaw(const std::string& value) {
    if (Trim(value).empty()) {
        return nullptr;
    }
    try {
        return nlohmann::json::parse(value);
    } catch (...) {
        return nlohmann::json{{"raw", value}};
    }
}

std::string JsonStringOr(const nlohmann::json& value, const char* name, const std::string& fallback = {}) {
    if (!value.is_object() || !value.contains(name) || !value[name].is_string()) {
        return fallback;
    }
    return value[name].get<std::string>();
}

bool JsonBoolOr(const nlohmann::json& value, const char* name, bool fallback) {
    if (!value.is_object() || !value.contains(name)) {
        return fallback;
    }
    if (value[name].is_boolean()) {
        return value[name].get<bool>();
    }
    return fallback;
}

int JsonIntOr(const nlohmann::json& value, const char* name, int fallback) {
    if (!value.is_object() || !value.contains(name)) {
        return fallback;
    }
    if (value[name].is_number_integer()) {
        return value[name].get<int>();
    }
    if (value[name].is_number()) {
        return static_cast<int>(value[name].get<double>());
    }
    return fallback;
}

double JsonDoubleOr(const nlohmann::json& value, const char* name, double fallback) {
    if (!value.is_object() || !value.contains(name) || !value[name].is_number()) {
        return fallback;
    }
    return value[name].get<double>();
}

std::vector<std::string> JsonStringArrayOrEmpty(const nlohmann::json& value, const char* name) {
    std::vector<std::string> strings;
    if (!value.is_object() || !value.contains(name) || !value[name].is_array()) {
        return strings;
    }
    for (const auto& item : value[name]) {
        if (item.is_string()) {
            const std::string text = Trim(item.get<std::string>());
            if (!text.empty()) {
                strings.push_back(text);
            }
        }
    }
    return strings;
}

McpToolCallResult MakeJsonToolResult(nlohmann::json payload, bool success = true) {
    McpToolCallResult result;
    result.success = success;
    result.is_tool_error = !success;
    result.raw_result_json = payload.dump(2);
    result.content_text = result.raw_result_json;
    return result;
}

McpToolCallResult MakeRagToolError(const std::string& message, nlohmann::json details = nlohmann::json::object()) {
    nlohmann::json payload = {
        {"success", false},
        {"error", message},
    };
    if (!details.empty()) {
        payload["details"] = std::move(details);
    }
    return MakeJsonToolResult(std::move(payload), false);
}

std::vector<RagToolLibrary> GetProjectRagToolLibraries(RagService* rag_service, const std::string& project_id, bool require_write = false, bool require_export = false) {
    std::vector<RagToolLibrary> libraries;
    if (!rag_service || project_id.empty()) {
        return libraries;
    }

    const auto bindings = rag_service->LoadProjectBindings(project_id);
    for (const auto& binding : bindings) {
        if (!binding.enabled || !binding.expose_as_tool || !binding.can_read) {
            continue;
        }
        if (require_write && !binding.can_write) {
            continue;
        }
        if (require_export && (!binding.can_export || Trim(binding.export_path_template).empty())) {
            continue;
        }

        auto library = rag_service->GetLibrary(binding.rag_id);
        if (!library || !library->enabled) {
            continue;
        }

        RagToolLibrary item;
        item.library = *library;
        item.binding = binding;
        libraries.push_back(std::move(item));
    }

    std::sort(libraries.begin(), libraries.end(), [](const RagToolLibrary& left, const RagToolLibrary& right) {
        if (left.binding.retrieval_priority != right.binding.retrieval_priority) {
            return left.binding.retrieval_priority > right.binding.retrieval_priority;
        }
        return left.library.name < right.library.name;
    });
    return libraries;
}

const RagToolLibrary* FindRagToolLibrary(const std::vector<RagToolLibrary>& libraries, const std::string& rag_id) {
    const auto it = std::find_if(libraries.begin(), libraries.end(), [&](const RagToolLibrary& item) {
        return item.library.id == rag_id;
    });
    return it == libraries.end() ? nullptr : &*it;
}

void ReplaceAll(std::string& text, const std::string& needle, const std::string& replacement) {
    if (needle.empty()) {
        return;
    }
    size_t position = 0;
    while ((position = text.find(needle, position)) != std::string::npos) {
        text.replace(position, needle.size(), replacement);
        position += replacement.size();
    }
}

bool IsVariableNameCandidate(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    for (const unsigned char ch : name) {
        if (!std::isalnum(ch) && ch != '_' && ch != '-' && ch != '.') {
            return false;
        }
    }
    return true;
}

std::vector<std::string> FindVariablePlaceholders(const std::string& text) {
    std::vector<std::string> names;
    auto add_name = [&](std::string name) {
        name = Trim(name);
        if (IsVariableNameCandidate(name) && std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(std::move(name));
        }
    };

    for (size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '$') {
            continue;
        }
        if (index + 1 < text.size() && text[index + 1] == '<') {
            const size_t end = text.find(">$", index + 2);
            if (end != std::string::npos) {
                add_name(text.substr(index + 2, end - (index + 2)));
                index = end + 1;
            }
            continue;
        }

        const size_t end = text.find('$', index + 1);
        if (end != std::string::npos) {
            add_name(text.substr(index + 1, end - (index + 1)));
            index = end;
        }
    }
    return names;
}

std::unordered_map<std::string, std::string> CollectProjectVariableValues(const McpManager* mcp_manager, const std::string& project_id) {
    std::unordered_map<std::string, std::string> values;
    if (!mcp_manager || project_id.empty()) {
        return values;
    }
    for (const auto& binding : mcp_manager->GetProjectBindings(project_id)) {
        for (const auto& variable : binding.variables) {
            const std::string name = Trim(variable.name);
            if (!name.empty()) {
                values[name] = variable.value;
            }
        }
    }
    return values;
}

std::optional<std::string> ExpandProjectVariableTemplate(
    std::string text,
    const std::unordered_map<std::string, std::string>& values,
    std::string* error) {
    std::vector<std::string> missing;
    for (const auto& name : FindVariablePlaceholders(text)) {
        const auto value_it = values.find(name);
        if (value_it == values.end() || Trim(value_it->second).empty()) {
            missing.push_back("$<" + name + ">$");
            continue;
        }
        ReplaceAll(text, "$" + name + "$", value_it->second);
        ReplaceAll(text, "$<" + name + ">$", value_it->second);
    }

    if (!missing.empty()) {
        std::ostringstream stream;
        stream << "Missing project variable values for export path: ";
        for (size_t i = 0; i < missing.size(); ++i) {
            if (i > 0) {
                stream << ", ";
            }
            stream << missing[i];
        }
        if (error) {
            *error = stream.str();
        }
        return std::nullopt;
    }
    return text;
}

std::wstring LowercasePathString(std::filesystem::path path) {
    std::wstring text = path.lexically_normal().wstring();
    std::replace(text.begin(), text.end(), L'/', L'\\');
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return text;
}

bool PathIsAtOrInside(const std::filesystem::path& path, const std::filesystem::path& root) {
    std::wstring root_text = LowercasePathString(root);
    std::wstring path_text = LowercasePathString(path);
    while (root_text.size() > 1 && (root_text.back() == L'\\' || root_text.back() == L'/')) {
        root_text.pop_back();
    }
    if (path_text == root_text) {
        return true;
    }
    root_text += L"\\";
    return path_text.rfind(root_text, 0) == 0;
}

std::wstring SanitizePathComponent(std::wstring component) {
    static constexpr wchar_t kInvalidChars[] = L"<>:\"/\\|?*";
    for (wchar_t& ch : component) {
        if (ch < 32 || std::wcschr(kInvalidChars, ch)) {
            ch = L'_';
        }
    }
    while (!component.empty() && (component.back() == L'.' || component.back() == L' ')) {
        component.pop_back();
    }
    while (!component.empty() && component.front() == L' ') {
        component.erase(component.begin());
    }
    return component.empty() ? L"document" : component;
}

std::filesystem::path SafeRelativeExportPath(const std::string& requested_relative_path, const std::wstring& default_file_name, std::string* error) {
    std::filesystem::path raw = Trim(requested_relative_path).empty()
        ? std::filesystem::path(default_file_name)
        : std::filesystem::path(Utf8ToWide(Trim(requested_relative_path)));

    if (raw.is_absolute() || raw.has_root_name() || raw.has_root_directory()) {
        if (error) {
            *error = "target_relative_path must be relative to the configured write-file folder.";
        }
        return {};
    }

    std::filesystem::path safe;
    for (const auto& part : raw) {
        const std::wstring text = part.wstring();
        if (text.empty() || text == L".") {
            continue;
        }
        if (text == L"..") {
            if (error) {
                *error = "target_relative_path cannot contain '..'.";
            }
            return {};
        }
        safe /= SanitizePathComponent(text);
    }
    if (safe.empty()) {
        safe = SanitizePathComponent(default_file_name);
    }
    return safe;
}

std::filesystem::path SafeRelativeFolderPath(const std::string& requested_folder_path, std::string* error) {
    const std::string trimmed = Trim(requested_folder_path);
    if (trimmed.empty()) {
        return {};
    }

    const std::filesystem::path raw(Utf8ToWide(trimmed));
    if (raw.is_absolute() || raw.has_root_name() || raw.has_root_directory()) {
        if (error) {
            *error = "target_folder_relative_path must be relative to the configured write-file folder.";
        }
        return {};
    }

    std::filesystem::path safe;
    for (const auto& part : raw) {
        const std::wstring text = part.wstring();
        if (text.empty() || text == L".") {
            continue;
        }
        if (text == L"..") {
            if (error) {
                *error = "target_folder_relative_path cannot contain '..'.";
            }
            return {};
        }
        safe /= SanitizePathComponent(text);
    }
    return safe;
}

std::wstring DefaultExtractedExportFileName(const RagDocumentRecord& document) {
    std::filesystem::path name(Utf8ToWide(document.display_name.empty() ? document.id : document.display_name));
    std::wstring stem = name.stem().wstring();
    if (stem.empty()) {
        stem = name.filename().wstring();
    }
    if (stem.empty()) {
        stem = Utf8ToWide(document.id.empty() ? "document" : document.id);
    }
    return SanitizePathComponent(stem) + L".md";
}

std::wstring DefaultOriginalExportFileName(const RagDocumentRecord& document, const std::filesystem::path& source_path) {
    std::wstring file_name = source_path.filename().wstring();
    if (file_name.empty()) {
        file_name = Utf8ToWide(document.display_name.empty() ? document.id : document.display_name);
    }
    return SanitizePathComponent(file_name);
}

nlohmann::json RagToolLibraryToJson(const RagToolLibrary& item) {
    nlohmann::json actions = nlohmann::json::array();
    if (item.binding.can_read) {
        actions.push_back({
            {"name", kRagSearchToolName},
            {"purpose", "Search this RAG for relevant chunks. Use first when you need to discover relevant documents or references."},
            {"returns", "Ranked chunks with confidence, document_id, chunk_id, source path, metadata, and optional text."},
        });
        actions.push_back({
            {"name", kRagGetDocumentToolName},
            {"purpose", "Retrieve metadata and extracted document text after rag_search identifies a useful document_id."},
            {"returns", "Document metadata, managed original path information, and optional extracted text."},
        });
    }
    if (item.binding.can_export && !Trim(item.binding.export_path_template).empty()) {
        actions.push_back({
            {"name", kRagWriteDocumentToDriveToolName},
            {"purpose", "Write a RAG document to the configured drive folder without using any filesystem MCP tool."},
            {"versions", {"original", "extracted_markdown"}},
            {"path_options", {
                "target_relative_path for a complete relative file path",
                "target_folder_relative_path plus optional target_file_name for a folder-oriented write",
                "omit both to write a safe default filename at the configured folder root",
            }},
            {"creates_missing_directories", true},
            {"safety", "Absolute target paths and '..' traversal are rejected; writes stay inside the configured folder."},
        });
    }
    if (item.binding.can_write) {
        actions.push_back({
            {"name", kRagIngestGeneratedDocumentToolName},
            {"purpose", "Persist generated Markdown/text content into this RAG for future retrieval."},
            {"returns", "Ingestion status, generated source URI, errors, and document id when available."},
        });
    }

    return nlohmann::json{
        {"id", item.library.id},
        {"name", item.library.name},
        {"description", item.library.description},
        {"enabled", item.library.enabled},
        {"storage_path", item.library.storage_path},
        {"storage_mode", RagStorageModeLabel(item.library.storage_mode)},
        {"embedding_provider", item.library.embedding_provider},
        {"embedding_base_url", item.library.embedding_base_url},
        {"embedding_model", item.library.embedding_model},
        {"vector_backend", item.library.vector_backend},
        {"permissions", {
            {"can_read", item.binding.can_read},
            {"can_write", item.binding.can_write},
            {"can_delete", item.binding.can_delete},
            {"can_export", item.binding.can_export},
            {"can_write_to_drive", item.binding.can_export},
            {"default_ingest_target", item.binding.default_ingest_target},
            {"exposed_as_tool", item.binding.expose_as_tool},
        }},
        {"write_to_drive_path_template", item.binding.export_path_template},
        {"retrieval_priority", item.binding.retrieval_priority},
        {"default_max_chunks_for_project", item.binding.max_chunks},
        {"default_confidence_window", {
            {"min_confidence", item.binding.default_min_confidence},
            {"max_confidence", item.binding.default_max_confidence},
        }},
        {"available_tool_actions", actions},
        {"usage_guidance", {
            {"search_first", "Use rag_search before reading or exporting unless you already have a document_id."},
            {"read_more", "Use rag_get_document when a search result is relevant but the chunk text is not enough."},
            {"write_to_drive", item.binding.can_export ? "Use rag_write_document_to_drive to write original or extracted_markdown output to the configured folder. Missing folders are created automatically." : "Not available for this RAG binding."},
            {"write_to_rag", item.binding.can_write ? "Use rag_ingest_generated_document only for generated Markdown/text that should become persistent RAG content." : "Not available for this RAG binding."},
        }},
    };
}

std::vector<ChatToolDefinition> BuildRagToolDefinitions(RagService* rag_service, const std::string& project_id) {
    const auto read_libraries = GetProjectRagToolLibraries(rag_service, project_id);
    if (read_libraries.empty()) {
        return {};
    }

    std::vector<ChatToolDefinition> tools;
    tools.reserve(5);

    ChatToolDefinition list_tool;
    list_tool.name = kRagListLibrariesToolName;
    list_tool.description = "List every RAG library exposed to this project and the exact RAG actions allowed for each one. Call this first when you are unsure which RAG to use, whether a RAG can write generated content, or whether a RAG can write original/extracted documents to drive. The response includes descriptions, permissions, retrieval defaults, write-file folder templates, available functions, and usage guidance.";
    list_tool.parameters_json = JsonObjectSchema({}).dump();
    tools.push_back(std::move(list_tool));

    ChatToolDefinition search_tool;
    search_tool.name = kRagSearchToolName;
    search_tool.description = "Search one or more project-approved RAG libraries for relevant chunks. Use this to discover document_id/chunk_id references before calling rag_get_document or rag_write_document_to_drive. Results are sorted by confidence and include source, document, chunk, metadata, and optional excerpt text. If results are too narrow, retry with a lower min_confidence, a confidence band, a larger candidate_limit, or specific rag_ids from rag_list_libraries.";
    search_tool.parameters_json = JsonObjectSchema({
        {"query", {{"type", "string"}, {"description", "Natural-language information need, keywords, file title, concept, or question to search for."}}},
        {"rag_ids", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Optional exposed RAG library ids from rag_list_libraries. Omit to search all exposed readable RAGs."}}},
        {"max_results", {{"type", "integer"}, {"minimum", 1}, {"maximum", 50}, {"description", "Maximum final results after threshold filtering. Default: 8. Ask for 5-10 when browsing; increase for broader review."}}},
        {"candidate_limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}, {"description", "Candidate pool before confidence filtering. Increase this for broad searches, low-confidence exploration, or needle-in-a-haystack work. Default: max(50, max_results * 5)."}}},
        {"min_confidence", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}, {"description", "Minimum normalized confidence to return. If omitted, each RAG's project binding default is used. Lower this when no results appear or when exploring weak matches."}}},
        {"max_confidence", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}, {"description", "Maximum normalized confidence to return. Use with min_confidence to search a specific confidence band, such as 0.2-0.5, when top hits are not the target."}}},
        {"include_text", {{"type", "boolean"}, {"description", "Whether to include chunk excerpt text. Default: true. Set false for metadata-only scans before choosing documents."}}},
        {"retrieval_mode", {{"type", "string"}, {"enum", {"hybrid", "vector", "keyword", "reranked"}}, {"description", "Requested retrieval intent. The current backend executes hybrid/fallback retrieval and reports actual retrieval_method per result; vector/keyword/reranked are accepted as intent for future compatibility."}}},
    }, {"query"}).dump();
    tools.push_back(std::move(search_tool));

    ChatToolDefinition get_tool;
    get_tool.name = kRagGetDocumentToolName;
    get_tool.description = "Load metadata and extracted text for a document returned by rag_search. Use this when a relevant chunk needs more surrounding document context, when you need managed original path information, or before deciding whether to write the original/extracted document to drive. This does not write files and does not modify the RAG.";
    get_tool.parameters_json = JsonObjectSchema({
        {"rag_id", {{"type", "string"}, {"description", "Exposed readable RAG library id from rag_list_libraries or rag_search."}}},
        {"document_id", {{"type", "string"}, {"description", "Document id returned by rag_search."}}},
        {"include_text", {{"type", "boolean"}, {"description", "Whether to include extracted Markdown/text. Default: true. Set false when you only need metadata or source paths."}}},
        {"max_chars", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 200000}, {"description", "Maximum extracted text characters to return. Default: 20000. Increase when reviewing large documents; output may be truncated."}}},
    }, {"rag_id", "document_id"}).dump();
    tools.push_back(std::move(get_tool));

    const auto export_libraries = GetProjectRagToolLibraries(rag_service, project_id, false, true);
    if (!export_libraries.empty()) {
        ChatToolDefinition export_tool;
        export_tool.name = kRagWriteDocumentToDriveToolName;
        export_tool.description = "Write a RAG document directly to the project-configured write-file folder without using filesystem MCP tools. Use this after rag_search or rag_get_document when the user wants a RAG document materialized on disk. Choose version=original to copy the source file, such as PDF/DOCX/XLSX, or version=extracted_markdown to write the converted Markdown/text representation. The configured destination folder is controlled by Project Settings and may use project variables such as $<ProjectFolder>$. You may provide target_relative_path for a full relative file path, or target_folder_relative_path plus optional target_file_name. Missing directories are created automatically by this tool; do not call another tool to create folders first.";
        export_tool.parameters_json = JsonObjectSchema({
            {"rag_id", {{"type", "string"}, {"description", "Readable exposed RAG id with Write file enabled, from rag_list_libraries or rag_search."}}},
            {"document_id", {{"type", "string"}, {"description", "Document id returned by rag_search or rag_get_document."}}},
            {"version", {{"type", "string"}, {"enum", {"original", "extracted_markdown"}}, {"description", "Output version to write. original copies the managed/source file, preserving format such as PDF, DOCX, or XLSX. extracted_markdown writes the converted Markdown/text representation used for RAG indexing."}}},
            {"target_relative_path", {{"type", "string"}, {"description", "Optional full file path relative to the configured write-file folder. Subfolders are allowed and created automatically; absolute paths and '..' are rejected. If omitted, target_folder_relative_path plus target_file_name/default filename is used."}}},
            {"target_folder_relative_path", {{"type", "string"}, {"description", "Optional nested folder path relative to the configured write-file folder. Missing folders are created automatically by the tool. Use this when you only need to choose a subfolder and want the default or target_file_name filename."}}},
            {"target_file_name", {{"type", "string"}, {"description", "Optional file name to use with target_folder_relative_path. Path separators are sanitized. Omit to use a safe default filename from the RAG document, with .md for extracted_markdown."}}},
            {"overwrite", {{"type", "boolean"}, {"description", "Whether to overwrite an existing target file. Default: false. If false and the file exists, the tool returns a diagnostic error with the target path."}}},
        }, {"rag_id", "document_id", "version"}).dump();
        tools.push_back(std::move(export_tool));
    }

    const auto write_libraries = GetProjectRagToolLibraries(rag_service, project_id, true);
    if (!write_libraries.empty()) {
        ChatToolDefinition ingest_tool;
        ingest_tool.name = kRagIngestGeneratedDocumentToolName;
        ingest_tool.description = "Add or update generated Markdown/text content into a write-enabled RAG library attached to this project. Use this only when the user or project instructions indicate that generated findings, notes, summaries, or researched material should become persistent RAG knowledge. Do not use this to write an existing RAG document to disk; use rag_write_document_to_drive for that. Generated content is saved as a managed source and then indexed so rebuild can restore it.";
        ingest_tool.parameters_json = JsonObjectSchema({
            {"rag_id", {{"type", "string"}, {"description", "Write-enabled exposed RAG id. If omitted, the project's default ingest target is used when available, otherwise the only write-enabled RAG is used if exactly one exists."}}},
            {"title", {{"type", "string"}, {"description", "Human-readable document title for future search results."}}},
            {"content", {{"type", "string"}, {"description", "Markdown or plain text content to ingest. Must be non-empty and should include useful headings/provenance when available."}}},
            {"source_uri", {{"type", "string"}, {"description", "Optional stable source URI for idempotent updates, such as a URL, generated:// identifier, or source document reference."}}},
            {"metadata", {{"type", "object"}, {"description", "Optional provenance metadata such as author/model, source URLs, project/chat identifiers, tags, confidence, or reason for ingestion."}}},
        }, {"title", "content"}).dump();
        tools.push_back(std::move(ingest_tool));
    }

    return tools;
}

bool IsRagToolName(const std::string& name) {
    return name == kRagListLibrariesToolName ||
        name == kRagSearchToolName ||
        name == kRagGetDocumentToolName ||
        name == kRagIngestGeneratedDocumentToolName ||
        name == kRagWriteDocumentToDriveToolName;
}

McpToolCallResult CallRagTool(RagService* rag_service, const McpManager* mcp_manager, const std::string& project_id, const std::string& tool_name, const std::string& arguments_json) {
    if (!rag_service) {
        return MakeRagToolError("RAG service is not available.");
    }

    nlohmann::json arguments = nlohmann::json::object();
    try {
        if (!Trim(arguments_json).empty()) {
            arguments = nlohmann::json::parse(arguments_json);
        }
        if (!arguments.is_object()) {
            return MakeRagToolError("RAG tool arguments must be a JSON object.");
        }
    } catch (const std::exception& ex) {
        return MakeRagToolError(std::string("RAG tool arguments were not valid JSON: ") + ex.what());
    } catch (...) {
        return MakeRagToolError("RAG tool arguments were not valid JSON.");
    }

    const auto read_libraries = GetProjectRagToolLibraries(rag_service, project_id);
    if (read_libraries.empty()) {
        return MakeRagToolError("No readable RAG libraries are exposed as tools for this project.");
    }

    if (tool_name == kRagListLibrariesToolName) {
        nlohmann::json libraries = nlohmann::json::array();
        for (const auto& item : read_libraries) {
            libraries.push_back(RagToolLibraryToJson(item));
        }
        return MakeJsonToolResult({
            {"success", true},
            {"tool", tool_name},
            {"project_id", project_id},
            {"count", libraries.size()},
            {"libraries", libraries},
            {"available_functions", {
                {kRagListLibrariesToolName, "Discover exposed RAG libraries, permissions, write-file folders, retrieval defaults, and allowed actions."},
                {kRagSearchToolName, "Search for relevant chunks and document_id values. Use this before reading/exporting unless a document_id is already known."},
                {kRagGetDocumentToolName, "Read document metadata and extracted Markdown/text. Use this when search chunks are not enough context."},
                {kRagWriteDocumentToDriveToolName, "Write original or extracted_markdown document versions to the configured folder for RAG bindings with Write file enabled. Missing directories are created automatically."},
                {kRagIngestGeneratedDocumentToolName, "Persist generated Markdown/text into a write-enabled RAG. Use for generated knowledge, not for exporting existing documents."},
            }},
            {"recommended_workflow", {
                "Call rag_list_libraries when choosing a RAG or checking write/export permissions.",
                "Call rag_search with a natural query to get ranked chunks and document_id values.",
                "Call rag_get_document when a result needs more context or metadata.",
                "Call rag_write_document_to_drive when the user wants an existing RAG document written to disk as original or extracted_markdown.",
                "Call rag_ingest_generated_document only when generated content should be added back into a write-enabled RAG.",
            }},
            {"notes", "Only libraries selected for this project with Enable, Read, and Tool checked are listed. Per-library available_tool_actions explains exactly which RAG functions can be used for that library."},
        });
    }

    if (tool_name == kRagSearchToolName) {
        const std::string query = Trim(JsonStringOr(arguments, "query"));
        if (query.empty()) {
            return MakeRagToolError("rag_search requires a non-empty query.");
        }

        const int max_results = std::clamp(JsonIntOr(arguments, "max_results", 8), 1, 50);
        const int default_candidate_limit = std::max(50, max_results * 5);
        const int candidate_limit = std::clamp(JsonIntOr(arguments, "candidate_limit", default_candidate_limit), 1, 200);
        const bool has_min_confidence = arguments.contains("min_confidence") && arguments["min_confidence"].is_number();
        const bool has_max_confidence = arguments.contains("max_confidence") && arguments["max_confidence"].is_number();
        const double requested_min_confidence = std::clamp(JsonDoubleOr(arguments, "min_confidence", 0.0), 0.0, 1.0);
        const double requested_max_confidence = std::clamp(JsonDoubleOr(arguments, "max_confidence", 1.0), 0.0, 1.0);
        if (has_min_confidence && has_max_confidence && requested_min_confidence > requested_max_confidence) {
            return MakeRagToolError("min_confidence cannot be greater than max_confidence.");
        }
        const bool include_text = JsonBoolOr(arguments, "include_text", true);
        const std::string retrieval_mode = Trim(JsonStringOr(arguments, "retrieval_mode", "hybrid"));
        const auto requested_rag_ids = JsonStringArrayOrEmpty(arguments, "rag_ids");

        std::vector<RagToolLibrary> selected_libraries;
        nlohmann::json skipped = nlohmann::json::array();
        if (requested_rag_ids.empty()) {
            selected_libraries = read_libraries;
        } else {
            for (const auto& rag_id : requested_rag_ids) {
                if (const RagToolLibrary* library = FindRagToolLibrary(read_libraries, rag_id)) {
                    selected_libraries.push_back(*library);
                } else {
                    skipped.push_back({{"rag_id", rag_id}, {"reason", "RAG is not exposed as a readable tool for this project."}});
                }
            }
        }
        if (selected_libraries.empty()) {
            return MakeRagToolError("No requested RAG libraries are available to search.", {{"skipped_rag_ids", skipped}});
        }

        struct SearchHit {
            RagQueryResult result;
            double confidence = 0.0;
        };
        std::vector<SearchHit> hits;
        nlohmann::json searched = nlohmann::json::array();
        nlohmann::json thresholds_by_rag = nlohmann::json::array();
        for (const auto& item : selected_libraries) {
            searched.push_back(item.library.id);
            double min_confidence = has_min_confidence ? requested_min_confidence : item.binding.default_min_confidence;
            double max_confidence = has_max_confidence ? requested_max_confidence : item.binding.default_max_confidence;
            min_confidence = std::clamp(min_confidence, 0.0, 1.0);
            max_confidence = std::clamp(max_confidence, 0.0, 1.0);
            if (min_confidence > max_confidence) {
                std::swap(min_confidence, max_confidence);
            }
            thresholds_by_rag.push_back({
                {"rag_id", item.library.id},
                {"rag_name", item.library.name},
                {"min_confidence", min_confidence},
                {"max_confidence", max_confidence},
                {"min_source", has_min_confidence ? "request" : "project_binding_default"},
                {"max_source", has_max_confidence ? "request" : "project_binding_default"},
            });
            const int per_library_limit = std::max(candidate_limit, item.binding.max_chunks);
            for (auto& result : rag_service->QueryRag(item.library.id, query, per_library_limit)) {
                const double confidence = std::clamp(result.score, 0.0, 1.0);
                if (confidence < min_confidence || confidence > max_confidence) {
                    continue;
                }
                hits.push_back({std::move(result), confidence});
            }
        }

        std::sort(hits.begin(), hits.end(), [](const SearchHit& left, const SearchHit& right) {
            return left.result.score > right.result.score;
        });
        if (hits.size() > static_cast<size_t>(max_results)) {
            hits.resize(static_cast<size_t>(max_results));
        }

        nlohmann::json results = nlohmann::json::array();
        for (const auto& hit : hits) {
            nlohmann::json item = {
                {"result_id", hit.result.rag_id + ":" + hit.result.document_id + ":" + hit.result.chunk_id},
                {"confidence", hit.confidence},
                {"raw_score", hit.result.score},
                {"retrieval_method", hit.result.retrieval_method},
                {"rag_id", hit.result.rag_id},
                {"rag_name", hit.result.rag_name},
                {"document_id", hit.result.document_id},
                {"document_title", hit.result.document_title},
                {"source_path", hit.result.source_path},
                {"chunk_id", hit.result.chunk_id},
                {"last_indexed_at", hit.result.last_indexed_at},
                {"metadata", ParseJsonOrRaw(hit.result.metadata_json)},
            };
            if (include_text) {
                item["text"] = hit.result.text;
            }
            results.push_back(std::move(item));
        }

        nlohmann::json notes = nlohmann::json::array();
        if (!retrieval_mode.empty() && retrieval_mode != "hybrid") {
            notes.push_back("Requested retrieval_mode was accepted for intent, but the current local backend runs hybrid/fallback retrieval and reports actual retrieval_method per result.");
        }
        if (results.empty()) {
            notes.push_back("No results matched the requested confidence window. Try lowering min_confidence, increasing candidate_limit, or searching a different RAG id.");
        }

        return MakeJsonToolResult({
            {"success", true},
            {"tool", tool_name},
            {"query", query},
            {"requested", {
                {"max_results", max_results},
                {"candidate_limit", candidate_limit},
                {"min_confidence", has_min_confidence ? nlohmann::json(requested_min_confidence) : nlohmann::json(nullptr)},
                {"max_confidence", has_max_confidence ? nlohmann::json(requested_max_confidence) : nlohmann::json(nullptr)},
                {"confidence_defaults", "Omitted thresholds use each RAG project's binding defaults."},
                {"include_text", include_text},
                {"retrieval_mode", retrieval_mode.empty() ? "hybrid" : retrieval_mode},
            }},
            {"searched_rag_ids", searched},
            {"skipped_rag_ids", skipped},
            {"confidence_windows_by_rag", thresholds_by_rag},
            {"count", results.size()},
            {"results", results},
            {"notes", notes},
        });
    }

    if (tool_name == kRagGetDocumentToolName) {
        const std::string rag_id = Trim(JsonStringOr(arguments, "rag_id"));
        const std::string document_id = Trim(JsonStringOr(arguments, "document_id"));
        if (rag_id.empty() || document_id.empty()) {
            return MakeRagToolError("rag_get_document requires rag_id and document_id.");
        }
        const RagToolLibrary* library = FindRagToolLibrary(read_libraries, rag_id);
        if (!library) {
            return MakeRagToolError("The requested RAG library is not exposed as a readable tool for this project.");
        }

        auto document = rag_service->GetDocument(rag_id, document_id);
        if (!document) {
            return MakeRagToolError("Document not found in the requested RAG library.");
        }

        const bool include_text = JsonBoolOr(arguments, "include_text", true);
        const int max_chars = std::clamp(JsonIntOr(arguments, "max_chars", 20000), 1000, 200000);
        bool truncated = false;
        std::string load_error;
        std::string extracted_text;
        if (include_text) {
            extracted_text = rag_service->LoadDocumentText(rag_id, document_id, static_cast<size_t>(max_chars), &truncated, &load_error);
            if (!load_error.empty()) {
                return MakeRagToolError(load_error);
            }
        }

        std::string managed_original_path;
        if (!document->stored_relative_path.empty() && !library->library.storage_path.empty()) {
            const std::filesystem::path path = std::filesystem::path(Utf8ToWide(library->library.storage_path)) / std::filesystem::path(Utf8ToWide(document->stored_relative_path));
            managed_original_path = WideToUtf8(std::filesystem::absolute(path).wstring());
        }

        nlohmann::json payload = {
            {"success", true},
            {"tool", tool_name},
            {"rag_id", rag_id},
            {"rag_name", library->library.name},
            {"document", {
                {"id", document->id},
                {"display_name", document->display_name},
                {"original_source_uri", document->original_source_uri},
                {"original_source_type", document->original_source_type},
                {"stored_relative_path", document->stored_relative_path},
                {"managed_original_path", managed_original_path},
                {"extracted_relative_path", document->extracted_relative_path},
                {"mime_type", document->mime_type},
                {"file_size", document->file_size},
                {"imported_at", document->imported_at},
                {"last_indexed_at", document->last_indexed_at},
                {"metadata", ParseJsonOrRaw(document->metadata_json)},
            }},
            {"include_text", include_text},
            {"max_chars", max_chars},
            {"truncated", truncated},
        };
        if (include_text) {
            payload["extracted_text"] = extracted_text;
        }
        return MakeJsonToolResult(std::move(payload));
    }

    if (tool_name == kRagWriteDocumentToDriveToolName) {
        const auto export_libraries = GetProjectRagToolLibraries(rag_service, project_id, false, true);
        if (export_libraries.empty()) {
            return MakeRagToolError("No RAG libraries are exposed with Write file enabled and a configured write-file folder.");
        }

        const std::string rag_id = Trim(JsonStringOr(arguments, "rag_id"));
        const std::string document_id = Trim(JsonStringOr(arguments, "document_id"));
        std::string version = Trim(JsonStringOr(arguments, "version", "extracted_markdown"));
        if (version == "markdown" || version == "extracted") {
            version = "extracted_markdown";
        }
        if (rag_id.empty() || document_id.empty()) {
            return MakeRagToolError("rag_write_document_to_drive requires rag_id and document_id.");
        }
        if (version != "original" && version != "extracted_markdown") {
            return MakeRagToolError("version must be either 'original' or 'extracted_markdown'.");
        }

        const RagToolLibrary* library = FindRagToolLibrary(export_libraries, rag_id);
        if (!library) {
            nlohmann::json writable = nlohmann::json::array();
            for (const auto& item : export_libraries) {
                writable.push_back(RagToolLibraryToJson(item));
            }
            return MakeRagToolError("The requested RAG library is not exposed with Write file enabled for this project.", {{"write_file_enabled_libraries", writable}});
        }

        std::string expand_error;
        const auto expanded_folder = ExpandProjectVariableTemplate(
            library->binding.export_path_template,
            CollectProjectVariableValues(mcp_manager, project_id),
            &expand_error);
        if (!expanded_folder) {
            return MakeRagToolError(expand_error.empty() ? "Could not expand the configured write-file folder." : expand_error);
        }

        std::filesystem::path base_folder(Utf8ToWide(Trim(*expanded_folder)));
        if (base_folder.empty()) {
            return MakeRagToolError("The configured write-file folder is empty.");
        }
        if (!base_folder.is_absolute()) {
            return MakeRagToolError("The configured write-file folder must expand to an absolute path.");
        }

        std::error_code ec;
        const bool configured_folder_existed = std::filesystem::exists(base_folder, ec);
        ec.clear();
        std::filesystem::create_directories(base_folder, ec);
        if (ec) {
            return MakeRagToolError("Could not create the configured write-file folder: " + ec.message());
        }
        if (!std::filesystem::is_directory(base_folder, ec)) {
            return MakeRagToolError("The configured write-file folder is not a directory.");
        }

        std::filesystem::path base_root = std::filesystem::weakly_canonical(base_folder, ec);
        if (ec) {
            ec.clear();
            base_root = std::filesystem::absolute(base_folder, ec).lexically_normal();
            if (ec) {
                return MakeRagToolError("Could not resolve the configured write-file folder: " + ec.message());
            }
        }

        auto document = rag_service->GetDocument(rag_id, document_id);
        if (!document) {
            return MakeRagToolError("Document not found in the requested RAG library.");
        }

        const bool overwrite = JsonBoolOr(arguments, "overwrite", false);
        const std::string requested_relative_path = Trim(JsonStringOr(arguments, "target_relative_path"));
        const std::string requested_folder_path = Trim(JsonStringOr(arguments, "target_folder_relative_path"));
        const std::string requested_file_name = Trim(JsonStringOr(arguments, "target_file_name"));
        std::string path_error;
        std::filesystem::path default_name;
        std::filesystem::path original_source_path;
        std::string extracted_text;
        bool write_extracted_text = version == "extracted_markdown";

        if (write_extracted_text) {
            bool truncated = false;
            std::string load_error;
            extracted_text = rag_service->LoadDocumentText(rag_id, document_id, 0, &truncated, &load_error);
            if (!load_error.empty()) {
                return MakeRagToolError(load_error);
            }
            default_name = DefaultExtractedExportFileName(*document);
        } else {
            if (!document->stored_relative_path.empty()) {
                if (library->library.storage_path.empty()) {
                    return MakeRagToolError("The RAG library storage path is not configured, so the managed original cannot be located.");
                }
                original_source_path = std::filesystem::path(Utf8ToWide(library->library.storage_path)) /
                    std::filesystem::path(Utf8ToWide(document->stored_relative_path));
            } else if (document->original_source_type == "file" && !document->original_source_uri.empty()) {
                original_source_path = std::filesystem::path(Utf8ToWide(document->original_source_uri));
            }

            if (original_source_path.empty()) {
                return MakeRagToolError("Document does not have a managed or file-backed original to write.");
            }
            if (!std::filesystem::exists(original_source_path, ec) || !std::filesystem::is_regular_file(original_source_path, ec)) {
                return MakeRagToolError("Original file was not found: " + WideToUtf8(original_source_path.wstring()));
            }
            default_name = DefaultOriginalExportFileName(*document, original_source_path);
        }

        std::filesystem::path relative_target;
        if (!requested_relative_path.empty()) {
            relative_target = SafeRelativeExportPath(requested_relative_path, default_name.wstring(), &path_error);
        } else {
            const std::filesystem::path relative_folder = SafeRelativeFolderPath(requested_folder_path, &path_error);
            const std::wstring file_name = requested_file_name.empty()
                ? default_name.wstring()
                : SanitizePathComponent(Utf8ToWide(requested_file_name));
            relative_target = relative_folder / file_name;
        }
        if (!path_error.empty()) {
            return MakeRagToolError(path_error);
        }

        const std::filesystem::path target_path = (base_root / relative_target).lexically_normal();
        if (!PathIsAtOrInside(target_path, base_root)) {
            return MakeRagToolError("Resolved target path escapes the configured write-file folder.");
        }

        const bool target_folder_existed = std::filesystem::exists(target_path.parent_path(), ec);
        ec.clear();
        std::filesystem::create_directories(target_path.parent_path(), ec);
        if (ec) {
            return MakeRagToolError("Could not create target folder: " + ec.message());
        }
        if (std::filesystem::exists(target_path, ec) && !overwrite) {
            return MakeRagToolError("Target file already exists. Retry with overwrite=true or choose another target_relative_path.", {{"target_path", WideToUtf8(target_path.wstring())}});
        }

        if (write_extracted_text) {
            std::ofstream output(target_path, std::ios::binary | std::ios::trunc);
            if (!output.is_open()) {
                return MakeRagToolError("Could not open target file for writing: " + WideToUtf8(target_path.wstring()));
            }
            output.write(extracted_text.data(), static_cast<std::streamsize>(extracted_text.size()));
            if (!output.good()) {
                return MakeRagToolError("Failed while writing extracted Markdown/text to the target file.");
            }
        } else {
            std::filesystem::copy_file(
                original_source_path,
                target_path,
                overwrite ? std::filesystem::copy_options::overwrite_existing : std::filesystem::copy_options::none,
                ec);
            if (ec) {
                return MakeRagToolError("Could not copy original file: " + ec.message());
            }
        }

        ec.clear();
        const uintmax_t bytes_written = std::filesystem::file_size(target_path, ec);
        return MakeJsonToolResult({
            {"success", true},
            {"tool", tool_name},
            {"rag_id", rag_id},
            {"rag_name", library->library.name},
            {"document_id", document_id},
            {"document_title", document->display_name},
            {"version", version},
            {"target_path", WideToUtf8(target_path.wstring())},
            {"configured_folder_template", library->binding.export_path_template},
            {"resolved_folder", WideToUtf8(base_root.wstring())},
            {"relative_path", WideToUtf8(relative_target.generic_wstring())},
            {"target_folder_relative_path", requested_folder_path},
            {"target_file_name", requested_file_name.empty() ? WideToUtf8(default_name.wstring()) : requested_file_name},
            {"missing_directories_created", !configured_folder_existed || !target_folder_existed},
            {"creates_missing_directories", true},
            {"source_path", write_extracted_text ? document->extracted_relative_path : WideToUtf8(original_source_path.wstring())},
            {"overwrite", overwrite},
            {"bytes_written", ec ? 0 : bytes_written},
        });
    }

    if (tool_name == kRagIngestGeneratedDocumentToolName) {
        const auto write_libraries = GetProjectRagToolLibraries(rag_service, project_id, true);
        if (write_libraries.empty()) {
            return MakeRagToolError("No write-enabled RAG libraries are exposed as tools for this project.");
        }

        std::string rag_id = Trim(JsonStringOr(arguments, "rag_id"));
        if (rag_id.empty()) {
            const auto default_it = std::find_if(write_libraries.begin(), write_libraries.end(), [](const RagToolLibrary& item) {
                return item.binding.default_ingest_target;
            });
            if (default_it != write_libraries.end()) {
                rag_id = default_it->library.id;
            } else if (write_libraries.size() == 1) {
                rag_id = write_libraries.front().library.id;
            }
        }
        const RagToolLibrary* library = FindRagToolLibrary(write_libraries, rag_id);
        if (!library) {
            nlohmann::json writable = nlohmann::json::array();
            for (const auto& item : write_libraries) {
                writable.push_back(RagToolLibraryToJson(item));
            }
            return MakeRagToolError("rag_ingest_generated_document requires a write-enabled exposed rag_id when no default ingest target is available.", {{"write_enabled_libraries", writable}});
        }

        const std::string title = Trim(JsonStringOr(arguments, "title"));
        const std::string content = JsonStringOr(arguments, "content");
        if (title.empty() || Trim(content).empty()) {
            return MakeRagToolError("rag_ingest_generated_document requires non-empty title and content.");
        }

        std::string source_uri = Trim(JsonStringOr(arguments, "source_uri"));
        if (source_uri.empty()) {
            source_uri = "generated://rag-tool/" + MakeId("source");
        }

        std::string metadata_json;
        if (arguments.contains("metadata")) {
            if (arguments["metadata"].is_object()) {
                metadata_json = arguments["metadata"].dump();
            } else if (arguments["metadata"].is_string()) {
                metadata_json = arguments["metadata"].get<std::string>();
            }
        }

        RagIngestionResult ingestion = rag_service->IngestGeneratedDocument(rag_id, title, content, metadata_json, source_uri);
        nlohmann::json errors = nlohmann::json::array();
        for (const auto& error : ingestion.errors) {
            errors.push_back(error);
        }

        std::string document_id;
        if (ingestion.success) {
            for (const auto& summary : rag_service->ListDocuments(rag_id)) {
                if (summary.document.original_source_uri == source_uri) {
                    document_id = summary.document.id;
                    break;
                }
            }
        }

        return MakeJsonToolResult({
            {"success", ingestion.success},
            {"tool", tool_name},
            {"rag_id", rag_id},
            {"rag_name", library->library.name},
            {"document_id", document_id},
            {"title", title},
            {"source_uri", source_uri},
            {"files_ingested", ingestion.files_ingested},
            {"files_skipped", ingestion.files_skipped},
            {"chunks_added", ingestion.chunks_added},
            {"errors", errors},
        }, ingestion.success);
    }

    return MakeRagToolError("Unknown RAG tool: " + tool_name);
}

std::string MessageDebugLabel(const MessageRecord& message) {
    if (message.role == "user") {
        return "Prompt";
    }
    if (message.role == "assistant" && !message.tool_calls_json.empty()) {
        return message.content.empty() ? "Assistant tool request" : "Assistant reply + tools";
    }
    if (message.role == "assistant") {
        return "Assistant reply";
    }
    if (message.role == "tool") {
        return "Tool result";
    }
    return message.role.empty() ? "Message" : message.role;
}

void AppendDebugSection(std::ostringstream& out, const std::string& title) {
    out << "\n=== " << title << " ===\n";
}

void AppendMessageDebugBlock(std::ostringstream& out, const MessageRecord& message, size_t index) {
    out << "Message #" << (index + 1) << " [" << (message.role.empty() ? "unknown" : message.role) << "]\n";
    if (!message.created_at.empty()) {
        out << "Created: " << message.created_at << "\n";
    }
    if (!message.name.empty()) {
        out << "Name: " << message.name << "\n";
    }
    if (!message.tool_call_id.empty()) {
        out << "Tool call id: " << message.tool_call_id << "\n";
    }
    if (!message.tool_calls_json.empty()) {
        out << "Tool calls:\n" << PrettyJsonOrRaw(message.tool_calls_json) << "\n";
    }
    out << "Content:\n" << (message.content.empty() ? "(empty)" : message.content) << "\n";
}

const MessageRecord* FindLastUserMessage(const std::vector<MessageRecord>& messages) {
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->role == "user") {
            return &*it;
        }
    }
    return nullptr;
}

const ChatContextDebugEntry* FindContextDebugEntryForMessage(const std::vector<ChatContextDebugEntry>& entries, size_t message_index) {
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        if (it->user_message_index == message_index) {
            return &*it;
        }
    }
    return nullptr;
}

std::string BuildRequestDebugDetail(const ChatContextDebugEntry& entry, const MessageRecord* displayed_message) {
    std::ostringstream out;
    out << "PROMPT REQUEST DEBUG\n";
    if (!entry.created_at.empty()) {
        out << "Captured: " << entry.created_at << "\n";
    }
    out << "Request id: " << (entry.id.empty() ? "(none)" : entry.id) << "\n";
    out << "Provider: " << (entry.provider_id.empty() ? "(none)" : entry.provider_id) << "\n";
    out << "Model: " << (entry.model_id.empty() ? "(none)" : entry.model_id) << "\n";
    out << "Chat message index: " << (entry.user_message_index + 1) << "\n";

    AppendDebugSection(out, "Prompt Sent");
    if (displayed_message) {
        out << (displayed_message->content.empty() ? "(empty)" : displayed_message->content) << "\n";
    } else if (const MessageRecord* request_prompt = FindLastUserMessage(entry.request_messages)) {
        out << (request_prompt->content.empty() ? "(empty)" : request_prompt->content) << "\n";
    } else {
        out << "(No user prompt was captured in the request message list)\n";
    }

    AppendDebugSection(out, "Context Window / System Prompt Sent Below Prompt");
    out << (entry.system_prompt.empty() ? "(empty)" : entry.system_prompt) << "\n";

    AppendDebugSection(out, "Context Components");
    out << "[Compressed context]\n" << (entry.compressed_context.empty() ? "(empty)" : entry.compressed_context) << "\n\n";
    out << "[MCP project context]\n" << (entry.mcp_context.empty() ? "(empty)" : entry.mcp_context) << "\n\n";
    out << "[RAG context]\n" << (entry.rag_context.empty() ? "(empty)" : entry.rag_context) << "\n";

    AppendDebugSection(out, "Exact Request Messages Sent");
    if (entry.request_messages.empty()) {
        out << "(No request messages were captured)\n";
    } else {
        for (size_t i = 0; i < entry.request_messages.size(); ++i) {
            AppendMessageDebugBlock(out, entry.request_messages[i], i);
            out << "\n";
        }
    }
    return out.str();
}

std::string BuildMessageDebugDetail(
    const std::vector<MessageRecord>& messages,
    size_t index,
    const ChatContextDebugEntry* request_entry) {
    std::ostringstream out;
    if (index >= messages.size()) {
        return "(Message index is no longer available)";
    }

    const MessageRecord& message = messages[index];
    AppendDebugSection(out, "Selected Message");
    AppendMessageDebugBlock(out, message, index);

    if (message.role == "user") {
        if (request_entry) {
            AppendDebugSection(out, "Outbound Prompt Context");
            out << BuildRequestDebugDetail(*request_entry, &message);
        } else {
            AppendDebugSection(out, "Outbound Prompt Context");
            out << "(No persisted request context was found for this prompt. Older prompts sent before this debug feature was added will not have this log.)\n";
        }
        return out.str();
    }

    if (message.role == "assistant") {
        if (!message.tool_calls_json.empty()) {
            AppendDebugSection(out, "Tool Calls Requested By This Assistant Message");
            out << PrettyJsonOrRaw(message.tool_calls_json) << "\n";
        }

        size_t cursor = index + 1;
        bool found_tool_results = false;
        while (cursor < messages.size() && messages[cursor].role == "tool") {
            if (!found_tool_results) {
                AppendDebugSection(out, "Related Tool Results");
                found_tool_results = true;
            }
            AppendMessageDebugBlock(out, messages[cursor], cursor);
            out << "\n";
            ++cursor;
        }
        if (found_tool_results && cursor < messages.size() && messages[cursor].role == "assistant") {
            AppendDebugSection(out, "Follow-up Assistant Response After Tool Results");
            AppendMessageDebugBlock(out, messages[cursor], cursor);
        }
        if (message.tool_calls_json.empty()) {
            size_t first_related_tool = index;
            while (first_related_tool > 0 && messages[first_related_tool - 1].role == "tool") {
                --first_related_tool;
            }
            if (first_related_tool < index) {
                if (first_related_tool > 0 && messages[first_related_tool - 1].role == "assistant" && !messages[first_related_tool - 1].tool_calls_json.empty()) {
                    AppendDebugSection(out, "Preceding Assistant Tool Request");
                    AppendMessageDebugBlock(out, messages[first_related_tool - 1], first_related_tool - 1);
                }
                AppendDebugSection(out, "Tool Results That Led To This Response");
                for (size_t tool_index = first_related_tool; tool_index < index; ++tool_index) {
                    AppendMessageDebugBlock(out, messages[tool_index], tool_index);
                    out << "\n";
                }
            }
        }
        return out.str();
    }

    if (message.role == "tool") {
        for (size_t cursor = index; cursor > 0; --cursor) {
            const size_t previous_index = cursor - 1;
            if (messages[previous_index].role != "tool") {
                if (messages[previous_index].role == "assistant" && !messages[previous_index].tool_calls_json.empty()) {
                    AppendDebugSection(out, "Preceding Assistant Tool Request");
                    AppendMessageDebugBlock(out, messages[previous_index], previous_index);
                }
                break;
            }
        }
    }
    return out.str();
}

std::string BuildCompressionSnapshotDebugDetail(const ChatCompressionSnapshot& snapshot, size_t index) {
    std::ostringstream out;
    out << "CONTEXT COMPRESSION SNAPSHOT #" << (index + 1) << "\n";
    out << "Snapshot id: " << (snapshot.id.empty() ? "(none)" : snapshot.id) << "\n";
    out << "Created: " << (snapshot.created_at.empty() ? "(unknown)" : snapshot.created_at) << "\n";
    out << "Trigger: " << (snapshot.trigger_reason.empty() ? "(unknown)" : snapshot.trigger_reason) << "\n";
    out << "Config: " << (snapshot.config_name.empty() ? snapshot.config_id : snapshot.config_name) << "\n";
    out << "Strategy: " << snapshot.strategy << "\n";
    out << "Previous message index: " << snapshot.previous_message_index << "\n";
    out << "Compressed through message index: " << snapshot.compressed_through_message_index << "\n";

    AppendDebugSection(out, "Previous Compressed Context");
    out << (snapshot.previous_compressed_context.empty() ? "(empty)" : snapshot.previous_compressed_context) << "\n";

    AppendDebugSection(out, "New Compressed Context");
    out << (snapshot.compressed_context.empty() ? "(empty)" : snapshot.compressed_context) << "\n";

    AppendDebugSection(out, "Source Messages Used For This Rebuild");
    if (snapshot.source_messages.empty()) {
        out << "(No source messages captured)\n";
    } else {
        for (size_t i = 0; i < snapshot.source_messages.size(); ++i) {
            AppendMessageDebugBlock(out, snapshot.source_messages[i], i);
            out << "\n";
        }
    }
    return out.str();
}

std::vector<ContextMessagesDebugItem> BuildContextMessageDebugItems(
    const std::vector<MessageRecord>& messages,
    const std::vector<ChatContextDebugEntry>& request_entries,
    const std::vector<ChatCompressionSnapshot>& compression_history) {
    std::vector<ContextMessagesDebugItem> items;

    for (size_t i = 0; i < messages.size(); ++i) {
        const MessageRecord& message = messages[i];
        const ChatContextDebugEntry* request_entry = message.role == "user" ? FindContextDebugEntryForMessage(request_entries, i) : nullptr;

        std::ostringstream label;
        label << "#" << (i + 1) << " " << MessageDebugLabel(message);
        if (!message.name.empty()) {
            label << " (" << message.name << ")";
        }
        if (request_entry) {
            label << " [request context]";
        }
        label << ": " << SquashForPreview(message.content);

        ContextMessagesDebugItem item;
        item.label = Utf8ToWide(label.str());
        item.detail = Utf8ToWide(BuildMessageDebugDetail(messages, i, request_entry));
        items.push_back(std::move(item));
    }

    for (const auto& entry : request_entries) {
        if (entry.user_message_index < messages.size()) {
            continue;
        }
        std::ostringstream label;
        label << "Prompt request log";
        if (!entry.created_at.empty()) {
            label << " " << entry.created_at;
        }
        ContextMessagesDebugItem item;
        item.label = Utf8ToWide(label.str());
        item.detail = Utf8ToWide(BuildRequestDebugDetail(entry, nullptr));
        items.push_back(std::move(item));
    }

    for (size_t i = 0; i < compression_history.size(); ++i) {
        const auto& snapshot = compression_history[i];
        std::ostringstream label;
        label << "Context rebuild #" << (i + 1);
        if (!snapshot.created_at.empty()) {
            label << " " << snapshot.created_at;
        }
        ContextMessagesDebugItem item;
        item.label = Utf8ToWide(label.str());
        item.detail = Utf8ToWide(BuildCompressionSnapshotDebugDetail(snapshot, i));
        items.push_back(std::move(item));
    }

    if (items.empty()) {
        ContextMessagesDebugItem item;
        item.label = L"No context messages";
        item.detail = L"No messages or context debug entries have been saved for this chat yet.";
        items.push_back(std::move(item));
    }

    return items;
}

LRESULT CALLBACK ContextMessagesWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* state = reinterpret_cast<ContextMessagesWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        state = reinterpret_cast<ContextMessagesWindowState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_CREATE: {
        state->font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        state->list = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"LISTBOX",
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            0,
            0,
            0,
            0,
            hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kContextMessagesListControlId)),
            nullptr,
            nullptr);
        state->detail = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            MSFTEDIT_CLASS,
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
            0,
            0,
            0,
            0,
            hwnd,
            nullptr,
            nullptr,
            nullptr);
        SendMessageW(state->list, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
        SendMessageW(state->detail, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
        SendMessageW(state->detail, EM_SETBKGNDCOLOR, 0, GetSysColor(COLOR_WINDOW));

        for (const auto& item : state->items) {
            SendMessageW(state->list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.label.c_str()));
        }
        if (!state->items.empty()) {
            SendMessageW(state->list, LB_SETCURSEL, 0, 0);
            SetWindowTextW(state->detail, state->items.front().detail.c_str());
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w_param) == kContextMessagesListControlId && HIWORD(w_param) == LBN_SELCHANGE && state && state->list && state->detail) {
            const int selection = static_cast<int>(SendMessageW(state->list, LB_GETCURSEL, 0, 0));
            if (selection >= 0 && static_cast<size_t>(selection) < state->items.size()) {
                SetWindowTextW(state->detail, state->items[static_cast<size_t>(selection)].detail.c_str());
                SendMessageW(state->detail, EM_SETSEL, 0, 0);
                SendMessageW(state->detail, EM_SCROLLCARET, 0, 0);
            }
        }
        return 0;
    case WM_SIZE:
        if (state && state->list && state->detail) {
            const int margin = ScaleForWindow(hwnd, 10);
            const int gutter = ScaleForWindow(hwnd, 10);
            const int width = LOWORD(l_param);
            const int height = HIWORD(l_param);
            const int left_width = std::min(std::max(ScaleForWindow(hwnd, 280), width / 3), ScaleForWindow(hwnd, 430));
            MoveWindow(state->list, margin, margin, left_width, std::max(ScaleForWindow(hwnd, 80), height - margin * 2), TRUE);
            MoveWindow(
                state->detail,
                margin + left_width + gutter,
                margin,
                std::max(ScaleForWindow(hwnd, 120), width - left_width - gutter - margin * 2),
                std::max(ScaleForWindow(hwnd, 80), height - margin * 2),
                TRUE);
        }
        return 0;
    case WM_DESTROY:
        delete state;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, w_param, l_param);
    }
}

void ShowContextMessagesWindow(HWND owner, AppStorage* storage, const std::string& project_id, const std::string& chat_id) {
    if (!storage || project_id.empty() || chat_id.empty()) {
        MessageBoxW(owner, L"Select a chat first.", L"No Chat Selected", MB_OK | MB_ICONINFORMATION);
        return;
    }

    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetModuleHandleW(nullptr));
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = &ContextMessagesWindowProc;
        wc.lpszClassName = kContextMessagesWindowClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        RegisterClassExW(&wc);
        registered = true;
    }

    auto* state = new ContextMessagesWindowState;
    state->items = BuildContextMessageDebugItems(
        storage->LoadMessages(project_id, chat_id),
        storage->LoadChatContextDebugEntries(project_id, chat_id),
        storage->LoadChatCompressionHistory(project_id, chat_id));

    HWND window = CreateWindowExW(
        WS_EX_APPWINDOW,
        kContextMessagesWindowClassName,
        L"Context Messages Debug",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1120,
        720,
        owner,
        nullptr,
        instance,
        state);
    if (!window) {
        delete state;
        MessageBoxW(owner, L"Could not open the context messages debug window.", L"Context Messages", MB_OK | MB_ICONERROR);
        return;
    }

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
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
    void OpenCompressionManager();
    void EditProjectSettings();
    void EditChatSettings();
    void ReloadProjects(const std::string& preferred_project_id, const std::string& preferred_chat_id);
    void RefreshTree();
    void LoadActiveMessages();
    void RefreshModelCombo();
    void OnModelSelectionChanged();
    std::string BuildMcpProjectContext() const;
    void SendCurrentMessage();
    void CompressCurrentContext();
    void ShowCurrentContextMessages();
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
    HWND context_window_button_ = nullptr;
    HWND chat_settings_button_ = nullptr;
    HWND transcript_ = nullptr;
    HWND tool_trace_ = nullptr;
    HWND input_ = nullptr;
    HWND send_button_ = nullptr;
    HWND compress_button_ = nullptr;
    HWND context_messages_button_ = nullptr;
    HWND status_label_ = nullptr;
    HWND provider_window_ = nullptr;
    HWND mcp_server_window_ = nullptr;
    HWND rag_service_window_ = nullptr;
    HWND compression_manager_window_ = nullptr;
    HFONT font_ = nullptr;

    AppStorage storage_;
    McpManager mcp_manager_;
    RagService rag_service_;
    ContextCompressionService compression_service_;
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

MainWindow::MainWindow() : storage_(DetermineAppRoot()), mcp_manager_(&storage_), rag_service_(&storage_), compression_service_(&storage_) {}

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
    project_mcp_button_ = CreateWindowExW(0, L"BUTTON", L"Project Settings", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kProjectMcp), nullptr, nullptr);
    rag_service_button_ = CreateWindowExW(0, L"BUTTON", L"RAG Service", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagService), nullptr, nullptr);
    context_window_button_ = CreateWindowExW(0, L"BUTTON", L"Context Window", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kContextWindow), nullptr, nullptr);
    chat_settings_button_ = CreateWindowExW(0, L"BUTTON", L"Chat Settings", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kChatSettings), nullptr, nullptr);
    transcript_ = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kTranscript), nullptr, nullptr);
    tool_trace_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, nullptr, WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kToolTrace), nullptr, nullptr);
    input_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kInput), nullptr, nullptr);
    send_button_ = CreateWindowExW(0, L"BUTTON", L"Send", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kSend), nullptr, nullptr);
    compress_button_ = CreateWindowExW(0, L"BUTTON", L"Compress", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kCompress), nullptr, nullptr);
    context_messages_button_ = CreateWindowExW(0, L"BUTTON", L"Context Msgs", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kContextMessages), nullptr, nullptr);
    status_label_ = CreateWindowExW(0, L"STATIC", L"Initializing...", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kStatus), nullptr, nullptr);

    for (HWND control : {new_project_button_, new_chat_button_, rename_button_, delete_button_, tree_, model_combo_, providers_button_, mcp_servers_button_, project_mcp_button_, rag_service_button_, context_window_button_, chat_settings_button_, transcript_, tool_trace_, input_, send_button_, compress_button_, context_messages_button_, status_label_}) {
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
    OpenAIClient::SetProviderCache(providers_);
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
    const int input_height = Scale(hwnd_, 130);
    const int tool_trace_height = Scale(hwnd_, 150);

    const int left_button_width = (left_width - gutter) / 2;
    MoveWindow(new_project_button_, margin, margin, left_button_width, button_height, TRUE);
    MoveWindow(new_chat_button_, margin + left_button_width + gutter, margin, left_button_width, button_height, TRUE);
    MoveWindow(rename_button_, margin, margin + button_height + gutter, left_button_width, button_height, TRUE);
    MoveWindow(delete_button_, margin + left_button_width + gutter, margin + button_height + gutter, left_button_width, button_height, TRUE);
    const int rag_button_y = height - margin - button_height;
    const int tree_top = margin + (button_height + gutter) * 2;
    MoveWindow(tree_, margin, tree_top, left_width, std::max(Scale(hwnd_, 80), rag_button_y - gutter - tree_top), TRUE);
    const int half_width = (left_width - gutter) / 2;
    MoveWindow(rag_service_button_, margin, rag_button_y, half_width, button_height, TRUE);
    MoveWindow(context_window_button_, margin + half_width + gutter, rag_button_y, half_width, button_height, TRUE);

    const int right_x = margin + left_width + gutter * 2;
    const int right_width = width - right_x - margin;
    const int providers_width = Scale(hwnd_, 100);
    const int mcp_width = Scale(hwnd_, 80);
    const int project_mcp_width = Scale(hwnd_, 120);
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
    const int action_width = Scale(hwnd_, 115);
    const int action_height = Scale(hwnd_, 34);
    const int input_top = transcript_top + transcript_height + gutter + tool_trace_height + gutter;
    const int action_x = right_x + right_width - action_width;
    MoveWindow(input_, right_x, input_top, right_width - action_width - gutter, input_height, TRUE);
    MoveWindow(send_button_, action_x, input_top, action_width, action_height, TRUE);
    MoveWindow(compress_button_, action_x, input_top + action_height + gutter, action_width, action_height, TRUE);
    MoveWindow(context_messages_button_, action_x, input_top + (action_height + gutter) * 2, action_width, action_height, TRUE);
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
        EditProjectSettings();
        break;
    case kRagService:
        OpenRagServiceManager();
        break;
    case kContextWindow:
        OpenCompressionManager();
        break;
    case kChatSettings:
        EditChatSettings();
        break;
    case kSend:
        SendCurrentMessage();
        break;
    case kCompress:
        CompressCurrentContext();
        break;
    case kContextMessages:
        ShowCurrentContextMessages();
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
        OpenAIClient::SetProviderCache(this->providers_);
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

void MainWindow::OpenCompressionManager() {
    if (compression_manager_window_ && IsWindow(compression_manager_window_)) {
        SetForegroundWindow(compression_manager_window_);
        return;
    }

    compression_manager_window_ = CreateContextCompressionManagerWindow(hwnd_, &compression_service_, &storage_, [this]() {
        return this->providers_;
    }, [this]() {
        // Refresh project settings if open
    });
}

void MainWindow::EditProjectSettings() {
    if (active_project_id_.empty()) {
        MessageBoxW(hwnd_, L"Select a project first.", L"No Project Selected", MB_OK | MB_ICONINFORMATION);
        return;
    }

    ProjectRecord* project = FindProject(active_project_id_);
    if (!project) {
        MessageBoxW(hwnd_, L"The selected project could not be found.", L"Project Not Found", MB_OK | MB_ICONERROR);
        return;
    }

    ProjectSettingsOptions options;
    options.title = L"Project Settings";
    options.accept_label = L"Save";
    options.project_name = Utf8ToWide(project->info.name);
    options.servers = mcp_manager_.configs();
    options.global_variables = mcp_manager_.global_variables();
    options.initial_mcp_bindings = mcp_manager_.GetProjectBindings(active_project_id_);

    // Load per-project compression configs and selected config
    auto project_settings = storage_.LoadProjectSettings(active_project_id_);
    options.compression_configs = compression_service_.LoadGlobalConfigs();  // Global configs from Context Window Settings
    options.selected_compression_config_id = project_settings.selected_compression_config_id;
    options.project_instructions = project_settings.project_instructions;
    if (options.selected_compression_config_id.empty()) {
        auto legacy_compression = storage_.LoadProjectCompressionSettings(active_project_id_);
        options.selected_compression_config_id = legacy_compression.config_id;
    }

    // Load RAG services
    auto available_rags = rag_service_.ListLibraries();
    options.available_rags = available_rags;
    options.initial_rag_bindings = rag_service_.LoadProjectBindings(active_project_id_);
    if (options.initial_rag_bindings.empty()) {
        options.initial_rag_bindings = project_settings.rag_bindings;
    }

    const auto result = ShowProjectSettingsDialog(hwnd_, options);
    if (!result) {
        return;
    }

    // Save MCP bindings
    mcp_manager_.SaveProjectBindings(active_project_id_, result->mcp_bindings);
    mcp_manager_.ConnectAutoServers(active_project_id_);

    // Save project settings (including selected compression config - global configs are managed separately)
    ProjectSettings saved_settings;
    saved_settings.project_name = result->project_name;
    saved_settings.project_instructions = result->project_instructions;
    saved_settings.mcp_bindings = result->mcp_bindings;
    saved_settings.selected_compression_config_id = result->selected_compression_config_id;
    saved_settings.rag_bindings = result->rag_bindings;
    storage_.SaveProjectSettings(active_project_id_, saved_settings);
    storage_.SaveProjectCompressionSettings(active_project_id_, ProjectCompressionSettings{
        !result->selected_compression_config_id.empty(),
        result->selected_compression_config_id,
    });
    rag_service_.SaveProjectBindings(active_project_id_, result->rag_bindings);

    RenderToolTrace();
    UpdateStatus(L"Project settings saved.");
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

    std::vector<MessageRecord> full_messages = active_messages_;
    full_messages.push_back(user_message);

    std::vector<MessageRecord> request_history = active_messages_;
    std::string compressed_context;
    auto proj_settings = storage_.LoadProjectSettings(active_project_id_);
    std::optional<ContextCompressionConfig> selected_compression_config;
    if (!proj_settings.selected_compression_config_id.empty()) {
        selected_compression_config = compression_service_.GetGlobalConfig(proj_settings.selected_compression_config_id);
        if (selected_compression_config) {
            if (compression_service_.ShouldCompress(active_project_id_, active_chat_id_, active_messages_.size())) {
                auto model_caller = [&](const ChatRequestOptions& opts) -> std::optional<ChatCompletionResult> {
                    auto result = OpenAIClient::CreateSimpleCompletion(opts);
                    return result.success ? std::make_optional(result) : std::nullopt;
                };
                compressed_context = compression_service_.CompressConversation(
                    active_messages_, active_project_id_, active_chat_id_, proj_settings.selected_compression_config_id, model_caller);
            }

            auto compression_state = compression_service_.LoadChatState(active_project_id_, active_chat_id_);
            if (compressed_context.empty()) {
                compressed_context = compression_state.current_compressed_context;
            }
            if (!compressed_context.empty() && compression_state.last_compression_message_index > 0) {
                const size_t first_uncompressed = std::min(compression_state.last_compression_message_index, request_history.size());
                request_history.erase(request_history.begin(), request_history.begin() + first_uncompressed);
            }
        }
    }

    request.messages = std::move(request_history);
    request.messages.push_back(user_message);
    if (!compressed_context.empty()) {
        if (!request.system_prompt.empty()) {
            request.system_prompt = compressed_context + "\n\n" + request.system_prompt;
        } else {
            request.system_prompt = compressed_context;
        }
    }

    const std::string project_instructions = Trim(proj_settings.project_instructions);
    if (!project_instructions.empty()) {
        if (!request.system_prompt.empty()) {
            request.system_prompt += "\n\n";
        }
        request.system_prompt += "Project Instructions:\n";
        request.system_prompt += project_instructions;
    }

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

    if (selected_compression_config) {
        // Emergency trigger: if the selected model context is near the configured threshold,
        // compress before the following turn.
        size_t estimated = EstimateRequestInputTokens(request, {}, false);
        size_t context_window = static_cast<size_t>(request.model.context_window);
        int trigger_percent = selected_compression_config->context_window_trigger_percent;
        if (trigger_percent > 0 && context_window > 0 && estimated > (context_window * static_cast<size_t>(trigger_percent) / 100)) {
            compression_service_.MarkCompressionScheduled(active_project_id_, active_chat_id_);
        }
    }

    const std::string project_id = active_project_id_;
    const std::string chat_id = active_chat_id_;
    const auto exposed_tools = mcp_manager_.GetExposedToolsForProject(project_id);
    const auto rag_tool_definitions = BuildRagToolDefinitions(&rag_service_, project_id);
    const bool include_tools = request.model.supports_tools && (!exposed_tools.empty() || !rag_tool_definitions.empty());

    if (!CheckContextWindow(hwnd_, request, exposed_tools, rag_tool_definitions, include_tools)) {
        return;
    }

    active_messages_ = full_messages;
    storage_.SaveMessages(active_project_id_, active_chat_id_, active_messages_);

    ChatContextDebugEntry context_debug_entry;
    context_debug_entry.id = MakeId("ctxdbg");
    context_debug_entry.created_at = CurrentTimestampUtc();
    context_debug_entry.kind = "request";
    context_debug_entry.user_message_index = full_messages.empty() ? 0 : full_messages.size() - 1;
    context_debug_entry.provider_id = request.provider.id;
    context_debug_entry.model_id = request.model.id;
    context_debug_entry.system_prompt = request.system_prompt;
    context_debug_entry.request_messages = request.messages;
    context_debug_entry.compressed_context = compressed_context;
    context_debug_entry.mcp_context = mcp_project_context;
    context_debug_entry.rag_context = rag_context;
    storage_.AppendChatContextDebugEntry(project_id, chat_id, context_debug_entry);

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

    std::thread([hwnd = hwnd_, request, project_id, chat_id, existing_count, exposed_tools, rag_tool_definitions, mcp_manager = &mcp_manager_, rag_service = &rag_service_]() {
        constexpr int kMaxToolRounds = 8;

        std::vector<ChatToolDefinition> tool_definitions;
        tool_definitions.reserve(exposed_tools.size() + rag_tool_definitions.size());
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
        tool_definitions.insert(tool_definitions.end(), rag_tool_definitions.begin(), rag_tool_definitions.end());

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
                        if (IsRagToolName(tool_call.name)) {
                            result = CallRagTool(rag_service, mcp_manager, project_id, tool_call.name, tool_call.arguments_json);
                        } else {
                            result = mcp_manager->CallExposedTool(project_id, tool_call.name, tool_call.arguments_json);
                        }
                    }

                    auto* trace_payload = new ToolTracePayload;
                    trace_payload->project_id = project_id;
                    trace_payload->chat_id = chat_id;
                    const auto tool_it = tool_lookup.find(tool_call.name);
                    if (tool_it != tool_lookup.end()) {
                        trace_payload->entry.title = tool_it->second.server_name + " / " + tool_it->second.tool_name;
                    } else if (IsRagToolName(tool_call.name)) {
                        trace_payload->entry.title = "RAG / " + tool_call.name;
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

void MainWindow::CompressCurrentContext() {
    if (active_project_id_.empty() || active_chat_id_.empty()) {
        MessageBoxW(hwnd_, L"Select a chat first.", L"No Chat Selected", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // Get the selected compression config for this project
    auto proj_settings = storage_.LoadProjectSettings(active_project_id_);
    if (proj_settings.selected_compression_config_id.empty()) {
        MessageBoxW(hwnd_, L"No context window compression config selected for this project.\n\nGo to Project Settings and select a Context Window Compression config.", L"No Compression Config", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // Get the global compression config
    auto config = compression_service_.GetGlobalConfig(proj_settings.selected_compression_config_id);
    if (!config) {
        MessageBoxW(hwnd_, L"The selected compression config was not found.\n\nGo to Context Window Settings to create or select a valid config.", L"Config Not Found", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // Load current messages
    auto messages = storage_.LoadMessages(active_project_id_, active_chat_id_);
    auto state_before = compression_service_.LoadChatState(active_project_id_, active_chat_id_);
    const size_t compressed_through_before = std::min(state_before.last_compression_message_index, messages.size());
    if (!state_before.current_compressed_context.empty() && compressed_through_before >= messages.size()) {
        std::ostringstream preview;
        preview << "=== COMPRESSED CONTEXT PREVIEW ===\n\n";
        preview << "Project: " << active_project_id_ << "\n";
        preview << "Chat: " << active_chat_id_ << "\n";
        preview << "Config: " << config->name << "\n";
        preview << "Status: no new chat messages have been sent since the last compression.\n";
        preview << "Compressed through message index: " << state_before.last_compression_message_index << "\n\n";
        preview << "The existing compressed context is unchanged. The previous compressed context is not used as a new source message.\n\n";
        preview << "=== CURRENT COMPRESSED OUTPUT ===\n";
        preview << state_before.current_compressed_context << "\n";
        ShowScrollableTextWindow(hwnd_, L"Compression Preview", Utf8ToWide(preview.str()));
        UpdateStatus(L"Context already compressed; no new messages to fold in.");
        return;
    }

    // Run compression - this updates the chat state and returns the compressed block
    auto model_caller = [&](const ChatRequestOptions& opts) -> std::optional<ChatCompletionResult> {
        auto result = OpenAIClient::CreateSimpleCompletion(opts);
        return result.success ? std::make_optional(result) : std::nullopt;
    };

    std::string compressed = compression_service_.CompressConversation(
        messages, active_project_id_, active_chat_id_, proj_settings.selected_compression_config_id, model_caller, true, "manual");

    // Load the updated state to show in preview
    auto state = compression_service_.LoadChatState(active_project_id_, active_chat_id_);
    const auto history = storage_.LoadChatCompressionHistory(active_project_id_, active_chat_id_);

    if (compressed.empty()) {
        compressed = state.current_compressed_context;
    }

    // Build a scrollable preview that shows both the diagnostic layers and the exact
    // compressed context block that will be injected on the next send.
    std::ostringstream preview;
    preview << "=== COMPRESSED CONTEXT PREVIEW ===\n\n";
    preview << "Project: " << active_project_id_ << "\n";
    preview << "Chat: " << active_chat_id_ << "\n";
    preview << "Config: " << config->name << "\n";
    preview << "Compressed through message index: " << state.last_compression_message_index << "\n\n";
    if (!history.empty()) {
        const auto& snapshot = history.back();
        preview << "Snapshot ID: " << snapshot.id << "\n";
        preview << "Created at: " << snapshot.created_at << "\n";
        preview << "Trigger: " << snapshot.trigger_reason << "\n";
        preview << "Previous message index: " << snapshot.previous_message_index << "\n";
        preview << "Messages included in this rebuild: " << snapshot.source_messages.size() << "\n\n";
    }

    preview << "[Pinned Messages (Layer 1)]\n";
    if (!state.layer1_pinned_messages.empty()) {
        for (const auto& p : state.layer1_pinned_messages) {
            preview << "[" << p.role << "]: " << p.content << "\n\n";
        }
    } else {
        preview << "(No pinned messages)\n";
    }

    preview << "\n[Running Summary - Layer 2]\n";
    preview << (state.layer2_previous_summary.empty() ? "(No summary yet)" : state.layer2_previous_summary) << "\n\n";

    preview << "[Extracted State - Layer 3]\n";
    if (!state.layer3_previous_state_json.empty()) {
        preview << state.layer3_previous_state_json << "\n";
    } else {
        preview << "(No state extracted yet)\n";
    }

    preview << "\n[Recency Window - Layer 4]\n";
    preview << "Recent turns are included in the compressed block according to the selected strategy.\n";
    preview << "New prompts after this compression will send only messages after the compressed index, plus this stored compressed context.\n";

    preview << "\n=== COMPRESSED OUTPUT ===\n";
    if (!compressed.empty()) {
        preview << compressed << "\n";
    } else {
        preview << "(Compression produced no output - context may not need compression)\n";
    }

    ShowScrollableTextWindow(hwnd_, L"Compression Preview", Utf8ToWide(preview.str()));

    // Refresh the transcript to show updated state
    RenderTranscript();
    UpdateStatus(L"Context window compressed.");
}

void MainWindow::ShowCurrentContextMessages() {
    if (active_project_id_.empty() || active_chat_id_.empty()) {
        MessageBoxW(hwnd_, L"Select a chat first.", L"No Chat Selected", MB_OK | MB_ICONINFORMATION);
        return;
    }

    ShowContextMessagesWindow(hwnd_, &storage_, active_project_id_, active_chat_id_);
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
    EnableWindow(rag_service_button_, !busy);
    EnableWindow(context_window_button_, !busy);
    EnableWindow(compress_button_, !busy);
    EnableWindow(context_messages_button_, !busy);
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
