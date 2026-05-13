#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <shellapi.h>
#include <winhttp.h>

#include "mcp_manager.h"
#include "mcp_server_manager.h"
#include "message_sanitizer.h"
#include "openai_client.h"
#include "questionnaire_dialog.h"
#include "prompt_dialog.h"
#include "project_setup_dialog.h"
#include "chat_request_logger.h"
#include "project_settings_dialog.h"
#include "provider_manager.h"
#include "rag_service.h"
#include "remote_worker_setup_dialog.h"
#include "remote_provider_worker.h"
#include "rag_tool_bridge.h"
#include "rag_service_manager.h"
#include "context_compression.h"
#include "context_compression_manager.h"
#include "model_tools_manager.h"
#include "agentic_modes_manager.h"
#include "web_server.h"
#include "web_user_store.h"
#include "web_config_dialog.h"
#include "admin_config_dialog.h"
#include "artifact_memory_tool_bridge.h"
#include "built_in_tools.h"
#include "storage.h"
#include "util.h"
#include "variable_resolver.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cwchar>
#include <cwctype>
#include <exception>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <windowsx.h>

namespace {
constexpr wchar_t kMainWindowClassName[] = L"AgentDesktopMainWindow";
constexpr UINT kChatDeltaMessage = WM_APP + 1;
constexpr UINT kChatFinishedMessage = WM_APP + 2;
constexpr UINT kToolTraceMessage = WM_APP + 3;
constexpr UINT kMcpChangedMessage = WM_APP + 4;
constexpr UINT kWebContentChangedMessage = WM_APP + 5;
constexpr UINT kStartupInitializeMessage = WM_APP + 6;
constexpr UINT kStartupServicesFinishedMessage = WM_APP + 7;
constexpr UINT kSetupSystemFinishedMessage = WM_APP + 8;
constexpr int kDefaultConfigZipResourceId = 101;
constexpr std::uintmax_t kDesktopTranscriptLoadLimitBytes = 8ull * 1024ull * 1024ull;

enum ControlId : int {
    kTree = 3001,
    kNewProject = 3002,
    kNewChat = 3003,
    kRename = 3004,
    kDelete = 3005,
    kProviders = 3007,
    kMcpServers = 3008,
    kSetupSystem = 3009,
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
    kModelTools = 3020,
    kWebConfig = 3021,
    kAdminConfig = 3022,
    kRemoteOllamaSetup = 3023,
    kAgenticModes = 3024,
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

struct ChatDeltaPayload {
    std::string project_id;
    std::string chat_id;
    std::string text;
};

struct ChatFinishedPayload {
    bool success = false;
    std::string error;
    std::string project_id;
    std::string chat_id;
    std::vector<MessageRecord> appended_messages;
    std::vector<RagWorkingSetEntry> rag_working_set_additions; // chunks retrieved via rag_search this turn
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

std::filesystem::path ResolveWebUsersPath() {
    const auto root = DetermineAppRoot();
    const auto config_root = root / ".config";
    const auto users_path = config_root / "users.json";
    const auto legacy_users_path = root / "users.json";

    std::error_code ec;
    std::filesystem::create_directories(config_root, ec);
    if (std::filesystem::is_regular_file(legacy_users_path, ec)) {
        const bool target_missing = !std::filesystem::is_regular_file(users_path, ec);
        bool legacy_is_newer = false;
        if (!target_missing) {
            const auto legacy_time = std::filesystem::last_write_time(legacy_users_path, ec);
            if (!ec) {
                const auto config_time = std::filesystem::last_write_time(users_path, ec);
                legacy_is_newer = !ec && legacy_time > config_time;
            }
        }
        if (target_missing || legacy_is_newer) {
            std::filesystem::copy_file(
                legacy_users_path,
                users_path,
                std::filesystem::copy_options::overwrite_existing,
                ec);
        }
    }
    return users_path;
}

std::filesystem::path ResolveWebSettingsPath(const RuntimePaths& runtime_paths) {
    return runtime_paths.config_root / "web_settings.json";
}

void MigrateLegacyWebSettings(const RuntimePaths& runtime_paths) {
    const auto settings_path = ResolveWebSettingsPath(runtime_paths);
    const auto legacy_path = runtime_paths.startup_root / "web_settings.json";

    std::error_code ec;
    std::filesystem::create_directories(settings_path.parent_path(), ec);
    if (ec) {
        Logger::Warn("WebSettings", "Could not create config directory for web_settings.json: " + ec.message());
        return;
    }

    if (!std::filesystem::is_regular_file(legacy_path, ec)) {
        return;
    }

    if (!std::filesystem::is_regular_file(settings_path, ec)) {
        std::filesystem::copy_file(
            legacy_path,
            settings_path,
            std::filesystem::copy_options::overwrite_existing,
            ec);
        if (ec) {
            Logger::Warn("WebSettings", "Could not migrate legacy web_settings.json into .config: " + ec.message());
            return;
        }
    }

    std::filesystem::remove(legacy_path, ec);
    if (ec) {
        Logger::Warn("WebSettings", "Could not remove legacy root web_settings.json: " + ec.message());
    } else {
        Logger::Info("WebSettings", "Removed legacy root web_settings.json; using .config/web_settings.json.");
    }
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

std::vector<MessageRecord> ModelVisibleMessages(const std::vector<MessageRecord>& messages) {
    return message_sanitizer::SanitizeModelVisibleMessages(messages);
}

std::wstring FormatFileUploadForTranscript(const std::string& content) {
    try {
        const auto data = nlohmann::json::parse(content);
        std::ostringstream out;
        out << data.value("filename", data.value("display_name", "Uploaded file"));
        const std::string status = data.value("status", "");
        if (!status.empty()) {
            out << " (" << status << ")";
        }
        if (data.contains("size") && data["size"].is_number_unsigned()) {
            out << "\nSize: " << data["size"].get<size_t>() << " bytes";
        }
        const std::string download = data.value("absolute_download_url", data.value("download_url", ""));
        if (!download.empty()) {
            out << "\nDownload: " << download;
        }
        const std::string warning = data.value("extraction_error", "");
        if (!warning.empty()) {
            out << "\nWarning: " << warning;
        }
        return Utf8ToWide(out.str());
    } catch (...) {
        return Utf8ToWide(content);
    }
}

enum class HeadlessCommandMode {
    None,
    OllamaSetup,
    OllamaRemote,
    RemoteWorkerSetup,
    RemoteWorkerRun,
};

struct HeadlessImageIngestSettings {
    std::string vision_model = "qwen2.5vl:7b";
    int ollama_instance_count = 1;
    int ollama_start_port = 11434;
};

struct HeadlessManagedOllamaProcess {
    HANDLE process = nullptr;
    DWORD process_id = 0;
    int port = 0;
};

HANDLE g_headless_stop_event = nullptr;

int ClampHeadlessOllamaInstanceCount(int value) {
    return std::clamp(value <= 0 ? 1 : value, 1, 32);
}

int ClampHeadlessOllamaStartPort(int value) {
    return std::clamp(value <= 0 ? 11434 : value, 1, 65535);
}

void EnsureHeadlessConsoleAttached() {
    static bool attempted = false;
    if (attempted) {
        return;
    }
    attempted = true;
    if (!AttachConsole(ATTACH_PARENT_PROCESS) && GetLastError() != ERROR_ACCESS_DENIED) {
        AllocConsole();
    }
}

void HeadlessWriteLine(const std::wstring& text) {
    EnsureHeadlessConsoleAttached();
    const std::wstring line = text + L"\r\n";
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output && output != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        if (WriteConsoleW(output, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr)) {
            return;
        }
        const std::string utf8 = WideToUtf8(line);
        WriteFile(output, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        return;
    }
    OutputDebugStringW(line.c_str());
}

std::wstring QuoteProcessArgument(const std::wstring& value) {
    std::wstring quoted = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'"') {
            quoted += L"\\\"";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted += L"\"";
    return quoted;
}

std::wstring ExpandEnvironmentPath(const std::wstring& value) {
    const DWORD needed = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (needed == 0) {
        return value;
    }
    std::wstring expanded(needed, L'\0');
    const DWORD written = ExpandEnvironmentStringsW(value.c_str(), expanded.data(), needed);
    if (written == 0 || written > needed) {
        return value;
    }
    expanded.resize(written > 0 ? written - 1 : 0);
    return expanded;
}

std::optional<std::filesystem::path> FindHeadlessExecutable(const std::wstring& executable_name) {
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD found = SearchPathW(nullptr, executable_name.c_str(), nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (found > 0 && found < buffer.size()) {
        return std::filesystem::path(buffer.data());
    }

    if (_wcsicmp(executable_name.c_str(), L"ollama.exe") == 0) {
        const std::vector<std::wstring> candidates = {
            ExpandEnvironmentPath(L"%LOCALAPPDATA%\\Programs\\Ollama\\ollama.exe"),
            ExpandEnvironmentPath(L"%ProgramFiles%\\Ollama\\ollama.exe"),
            ExpandEnvironmentPath(L"%ProgramFiles(x86)%\\Ollama\\ollama.exe"),
        };
        for (const auto& candidate : candidates) {
            std::error_code ec;
            if (!candidate.empty() && std::filesystem::exists(candidate, ec)) {
                return std::filesystem::path(candidate);
            }
        }
    }

    return std::nullopt;
}

bool RunHeadlessCommandLine(std::wstring command_line, DWORD timeout_ms, DWORD* exit_code) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    BOOL inherit_handles = FALSE;
    HANDLE std_output = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE std_error = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE std_input = GetStdHandle(STD_INPUT_HANDLE);
    if (std_output && std_output != INVALID_HANDLE_VALUE &&
        std_error && std_error != INVALID_HANDLE_VALUE) {
        startup.dwFlags |= STARTF_USESTDHANDLES;
        startup.hStdOutput = std_output;
        startup.hStdError = std_error;
        startup.hStdInput = (std_input && std_input != INVALID_HANDLE_VALUE) ? std_input : nullptr;
        inherit_handles = TRUE;
    }

    std::vector<wchar_t> buffer(command_line.begin(), command_line.end());
    buffer.push_back(L'\0');

    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, inherit_handles, 0, nullptr, nullptr, &startup, &process)) {
        HeadlessWriteLine(L"Failed to start command. CreateProcess error: " + std::to_wstring(GetLastError()));
        return false;
    }

    const DWORD wait_result = WaitForSingleObject(process.hProcess, timeout_ms);
    DWORD process_exit_code = 1;
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
        process_exit_code = 1;
    } else {
        GetExitCodeProcess(process.hProcess, &process_exit_code);
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (exit_code) {
        *exit_code = process_exit_code;
    }
    return wait_result != WAIT_TIMEOUT && process_exit_code == 0;
}

std::wstring PowerShellSingleQuoted(const std::wstring& value) {
    std::wstring quoted = L"'";
    for (wchar_t ch : value) {
        if (ch == L'\'') {
            quoted += L"''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted += L"'";
    return quoted;
}

bool RunHiddenCommandLine(std::wstring command_line, DWORD timeout_ms, DWORD* exit_code, std::wstring* error) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;

    std::vector<wchar_t> buffer(command_line.begin(), command_line.end());
    buffer.push_back(L'\0');

    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &startup, &process)) {
        if (error) {
            *error = L"CreateProcess failed with error " + std::to_wstring(GetLastError()) + L".";
        }
        return false;
    }

    const DWORD wait_result = WaitForSingleObject(process.hProcess, timeout_ms);
    DWORD process_exit_code = 1;
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
        process_exit_code = 1;
        if (error) {
            *error = L"Process timed out.";
        }
    } else {
        GetExitCodeProcess(process.hProcess, &process_exit_code);
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (exit_code) {
        *exit_code = process_exit_code;
    }
    if (wait_result != WAIT_TIMEOUT && process_exit_code != 0 && error) {
        *error = L"Process exited with code " + std::to_wstring(process_exit_code) + L".";
    }
    return wait_result != WAIT_TIMEOUT && process_exit_code == 0;
}

bool WriteEmbeddedResourceToFile(int resource_id, const std::filesystem::path& output_path, std::wstring* error) {
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resource_id), RT_RCDATA);
    if (!resource) {
        if (error) {
            *error = L"The embedded setup configuration resource was not found.";
        }
        return false;
    }

    const DWORD size = SizeofResource(module, resource);
    HGLOBAL loaded = LoadResource(module, resource);
    const void* data = loaded ? LockResource(loaded) : nullptr;
    if (size == 0 || !data) {
        if (error) {
            *error = L"The embedded setup configuration resource could not be loaded.";
        }
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(output_path.parent_path(), ec);
    if (ec) {
        if (error) {
            *error = L"Could not create temp directory: " + Utf8ToWide(ec.message());
        }
        return false;
    }

    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        if (error) {
            *error = L"Could not write temporary setup configuration zip.";
        }
        return false;
    }
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!out.good()) {
        if (error) {
            *error = L"Failed while writing temporary setup configuration zip.";
        }
        return false;
    }
    return true;
}

std::wstring ReadUtf8TextFileBestEffort(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return Utf8ToWide(text);
}

bool ExtractZipArchiveToDirectory(
    const std::filesystem::path& zip_path,
    const std::filesystem::path& destination,
    std::wstring* error) {
    std::error_code ec;
    std::filesystem::create_directories(destination, ec);
    if (ec) {
        if (error) {
            *error = L"Could not create destination directory: " + Utf8ToWide(ec.message());
        }
        return false;
    }

    const auto powershell = FindHeadlessExecutable(L"powershell.exe");
    if (!powershell) {
        if (error) {
            *error = L"powershell.exe was not found, so the embedded configuration zip could not be extracted.";
        }
        return false;
    }

    const std::filesystem::path log_path(zip_path.wstring() + L".extract.log");
    std::filesystem::remove(log_path, ec);

    const std::wstring script =
        L"$ErrorActionPreference='Stop'; "
        L"$ProgressPreference='SilentlyContinue'; "
        L"try { "
        L"Expand-Archive -LiteralPath " + PowerShellSingleQuoted(zip_path.wstring()) +
        L" -DestinationPath " + PowerShellSingleQuoted(destination.wstring()) + L" -Force"
        L" } catch { "
        L"$_ | Out-String | Set-Content -LiteralPath " + PowerShellSingleQuoted(log_path.wstring()) + L" -Encoding UTF8; "
        L"exit 1"
        L" }";
    const std::wstring command_line =
        QuoteProcessArgument(powershell->wstring()) +
        L" -NoLogo -NoProfile -ExecutionPolicy Bypass -Command " +
        QuoteProcessArgument(script);

    DWORD exit_code = 1;
    std::wstring run_error;
    if (!RunHiddenCommandLine(command_line, 5 * 60 * 1000, &exit_code, &run_error)) {
        const std::wstring details = ReadUtf8TextFileBestEffort(log_path);
        std::filesystem::remove(log_path, ec);
        if (error) {
            *error = L"PowerShell Expand-Archive failed: " + run_error;
            if (!details.empty()) {
                *error += L"\n\n" + details;
            }
        }
        return false;
    }
    std::filesystem::remove(log_path, ec);
    return true;
}

std::optional<HeadlessImageIngestSettings> LoadHeadlessImageIngestSettings(
    const std::filesystem::path& path,
    std::string* error) {
    try {
        if (path.empty() || !std::filesystem::exists(path)) {
            if (error) {
                *error = "Settings JSON file was not found.";
            }
            return std::nullopt;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            if (error) {
                *error = "Could not open settings JSON file.";
            }
            return std::nullopt;
        }
        const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        const auto data = nlohmann::json::parse(text);

        HeadlessImageIngestSettings settings;
        settings.vision_model = Trim(data.value("vision_model", "qwen2.5vl:7b"));
        if (settings.vision_model.empty()) {
            settings.vision_model = "qwen2.5vl:7b";
        }
        settings.ollama_instance_count = ClampHeadlessOllamaInstanceCount(data.value("ollama_instance_count", 1));
        settings.ollama_start_port = ClampHeadlessOllamaStartPort(data.value("ollama_start_port", 11434));
        if (settings.ollama_start_port + settings.ollama_instance_count - 1 > 65535) {
            if (error) {
                *error = "Ollama port range exceeds 65535.";
            }
            return std::nullopt;
        }
        return settings;
    } catch (const std::exception& ex) {
        if (error) {
            *error = ex.what();
        }
    } catch (...) {
        if (error) {
            *error = "Unexpected error loading settings JSON.";
        }
    }
    return std::nullopt;
}

bool EnsureHeadlessOllamaInstalled(std::filesystem::path* ollama_path) {
    if (auto existing = FindHeadlessExecutable(L"ollama.exe")) {
        if (ollama_path) {
            *ollama_path = *existing;
        }
        HeadlessWriteLine(L"Ollama is already installed: " + existing->wstring());
        return true;
    }

    if (!FindHeadlessExecutable(L"winget.exe")) {
        HeadlessWriteLine(L"Ollama is missing and winget.exe was not found. Install Ollama manually first.");
        return false;
    }

    HeadlessWriteLine(L"Ollama is not installed. Installing Ollama with winget...");
    DWORD exit_code = 1;
    const bool installed = RunHeadlessCommandLine(
        L"cmd.exe /c winget install --id Ollama.Ollama -e --accept-package-agreements --accept-source-agreements",
        INFINITE,
        &exit_code);
    if (!installed) {
        HeadlessWriteLine(L"Ollama installer failed with exit code " + std::to_wstring(exit_code) + L".");
        return false;
    }

    if (auto installed_path = FindHeadlessExecutable(L"ollama.exe")) {
        if (ollama_path) {
            *ollama_path = *installed_path;
        }
        HeadlessWriteLine(L"Ollama installed: " + installed_path->wstring());
        return true;
    }

    HeadlessWriteLine(L"Ollama installation finished, but ollama.exe is not visible to this process yet. Restart this shell or provide Ollama on PATH.");
    return false;
}

bool PullHeadlessOllamaModel(const std::filesystem::path& ollama_path, const std::string& model) {
    HeadlessWriteLine(L"Pulling Ollama vision model: " + Utf8ToWide(model));
    DWORD exit_code = 1;
    const std::wstring command =
        QuoteProcessArgument(ollama_path.wstring()) + L" pull " + QuoteProcessArgument(Utf8ToWide(model));
    const bool pulled = RunHeadlessCommandLine(command, INFINITE, &exit_code);
    if (!pulled) {
        HeadlessWriteLine(L"Model pull failed with exit code " + std::to_wstring(exit_code) + L".");
    }
    return pulled;
}

struct HeadlessWinHttpCloser {
    void operator()(void* handle) const {
        if (handle) {
            WinHttpCloseHandle(static_cast<HINTERNET>(handle));
        }
    }
};

using HeadlessWinHttpHandle = std::unique_ptr<void, HeadlessWinHttpCloser>;

bool IsHeadlessOllamaEndpointAvailable(int port) {
    HeadlessWinHttpHandle session(WinHttpOpen(
        L"AgentImageIngestRemote/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!session) {
        return false;
    }
    HeadlessWinHttpHandle connection(WinHttpConnect(
        static_cast<HINTERNET>(session.get()),
        L"127.0.0.1",
        static_cast<INTERNET_PORT>(port),
        0));
    if (!connection) {
        return false;
    }
    HeadlessWinHttpHandle request(WinHttpOpenRequest(
        static_cast<HINTERNET>(connection.get()),
        L"GET",
        L"/api/tags",
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0));
    if (!request) {
        return false;
    }
    WinHttpSetTimeouts(static_cast<HINTERNET>(request.get()), 1000, 1000, 1000, 1000);
    if (!WinHttpSendRequest(static_cast<HINTERNET>(request.get()), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        return false;
    }
    if (!WinHttpReceiveResponse(static_cast<HINTERNET>(request.get()), nullptr)) {
        return false;
    }
    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    if (!WinHttpQueryHeaders(static_cast<HINTERNET>(request.get()), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX)) {
        return false;
    }
    return status_code >= 200 && status_code < 500;
}

bool SetTemporaryEnvironmentVariable(
    const std::wstring& name,
    const std::wstring& value,
    std::wstring* previous_value,
    bool* had_previous_value) {
    DWORD size = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
    if (had_previous_value) {
        *had_previous_value = size > 0;
    }
    if (previous_value) {
        previous_value->clear();
        if (size > 0) {
            previous_value->resize(size, L'\0');
            const DWORD written = GetEnvironmentVariableW(name.c_str(), previous_value->data(), size);
            previous_value->resize(written);
        }
    }
    return SetEnvironmentVariableW(name.c_str(), value.c_str()) != FALSE;
}

void RestoreEnvironmentVariable(
    const std::wstring& name,
    const std::wstring& previous_value,
    bool had_previous_value) {
    if (had_previous_value) {
        SetEnvironmentVariableW(name.c_str(), previous_value.c_str());
    } else {
        SetEnvironmentVariableW(name.c_str(), nullptr);
    }
}

bool StartHeadlessOllamaServer(
    const std::filesystem::path& ollama_path,
    int port,
    HeadlessManagedOllamaProcess* managed_process) {
    const std::wstring host = L"0.0.0.0:" + std::to_wstring(port);
    std::wstring previous_host;
    bool had_previous_host = false;
    SetTemporaryEnvironmentVariable(L"OLLAMA_HOST", host, &previous_host, &had_previous_host);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    BOOL inherit_handles = FALSE;
    HANDLE std_output = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE std_error = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE std_input = GetStdHandle(STD_INPUT_HANDLE);
    if (std_output && std_output != INVALID_HANDLE_VALUE &&
        std_error && std_error != INVALID_HANDLE_VALUE) {
        startup.dwFlags |= STARTF_USESTDHANDLES;
        startup.hStdOutput = std_output;
        startup.hStdError = std_error;
        startup.hStdInput = (std_input && std_input != INVALID_HANDLE_VALUE) ? std_input : nullptr;
        inherit_handles = TRUE;
    }

    std::wstring command_line = QuoteProcessArgument(ollama_path.wstring()) + L" serve";
    std::vector<wchar_t> buffer(command_line.begin(), command_line.end());
    buffer.push_back(L'\0');
    PROCESS_INFORMATION process{};
    HeadlessWriteLine(L"Starting Ollama on 0.0.0.0:" + std::to_wstring(port) + L"...");
    const BOOL created = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, inherit_handles, 0, nullptr, nullptr, &startup, &process);

    RestoreEnvironmentVariable(L"OLLAMA_HOST", previous_host, had_previous_host);

    if (!created) {
        HeadlessWriteLine(L"Failed to start Ollama on port " + std::to_wstring(port) + L". CreateProcess error: " + std::to_wstring(GetLastError()));
        return false;
    }
    CloseHandle(process.hThread);

    for (int attempt = 0; attempt < 40; ++attempt) {
        Sleep(500);
        if (IsHeadlessOllamaEndpointAvailable(port)) {
            if (managed_process) {
                managed_process->process = process.hProcess;
                managed_process->process_id = process.dwProcessId;
                managed_process->port = port;
            } else {
                CloseHandle(process.hProcess);
            }
            HeadlessWriteLine(L"Ollama is responding on port " + std::to_wstring(port) + L".");
            return true;
        }
    }

    TerminateProcess(process.hProcess, 1);
    WaitForSingleObject(process.hProcess, 5000);
    CloseHandle(process.hProcess);
    HeadlessWriteLine(L"Ollama started but did not become ready on port " + std::to_wstring(port) + L".");
    return false;
}

BOOL WINAPI HeadlessConsoleControlHandler(DWORD control_type) {
    switch (control_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        if (g_headless_stop_event) {
            SetEvent(g_headless_stop_event);
            return TRUE;
        }
        break;
    default:
        break;
    }
    return FALSE;
}

void StopHeadlessManagedProcesses(std::vector<HeadlessManagedOllamaProcess>& processes) {
    for (auto& process : processes) {
        if (!process.process) {
            continue;
        }
        DWORD exit_code = 0;
        if (GetExitCodeProcess(process.process, &exit_code) && exit_code == STILL_ACTIVE) {
            HeadlessWriteLine(L"Stopping Ollama on port " + std::to_wstring(process.port) + L"...");
            TerminateProcess(process.process, 0);
            WaitForSingleObject(process.process, 5000);
        }
        CloseHandle(process.process);
        process.process = nullptr;
    }
}

int RunHeadlessOllamaSetup(const std::filesystem::path& settings_path) {
    std::string error;
    const auto settings = LoadHeadlessImageIngestSettings(settings_path, &error);
    if (!settings) {
        HeadlessWriteLine(L"Could not load image ingest settings: " + Utf8ToWide(error));
        return 2;
    }

    std::filesystem::path ollama_path;
    if (!EnsureHeadlessOllamaInstalled(&ollama_path)) {
        return 3;
    }
    if (!PullHeadlessOllamaModel(ollama_path, settings->vision_model)) {
        return 4;
    }

    HeadlessWriteLine(L"Setup complete.");
    HeadlessWriteLine(L"Run remote image ingestion services with:");
    HeadlessWriteLine(L"  agent --olama-remote " + QuoteProcessArgument(settings_path.wstring()));
    return 0;
}

int RunHeadlessOllamaRemote(const std::filesystem::path& settings_path) {
    std::string error;
    const auto settings = LoadHeadlessImageIngestSettings(settings_path, &error);
    if (!settings) {
        HeadlessWriteLine(L"Could not load image ingest settings: " + Utf8ToWide(error));
        return 2;
    }

    const auto ollama_path = FindHeadlessExecutable(L"ollama.exe");
    if (!ollama_path) {
        HeadlessWriteLine(L"ollama.exe was not found. Run --olama-setup first or install Ollama manually.");
        return 3;
    }

    g_headless_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SetConsoleCtrlHandler(HeadlessConsoleControlHandler, TRUE);

    std::vector<HeadlessManagedOllamaProcess> managed_processes;
    for (int i = 0; i < settings->ollama_instance_count; ++i) {
        const int port = settings->ollama_start_port + i;
        if (IsHeadlessOllamaEndpointAvailable(port)) {
            HeadlessWriteLine(L"Ollama is already responding on port " + std::to_wstring(port) + L"; leaving that external process alone.");
            continue;
        }

        HeadlessManagedOllamaProcess process;
        if (!StartHeadlessOllamaServer(*ollama_path, port, &process)) {
            StopHeadlessManagedProcesses(managed_processes);
            if (g_headless_stop_event) {
                CloseHandle(g_headless_stop_event);
                g_headless_stop_event = nullptr;
            }
            return 4;
        }
        managed_processes.push_back(process);
    }

    HeadlessWriteLine(L"Remote image ingestion Ollama endpoints are ready.");
    HeadlessWriteLine(L"Configure the desktop app's Vision host / base URL to this computer's address.");
    HeadlessWriteLine(L"Starting port: " + std::to_wstring(settings->ollama_start_port) + L"; instances: " + std::to_wstring(settings->ollama_instance_count) + L".");
    HeadlessWriteLine(L"Press Ctrl+C to stop app-managed Ollama processes.");

    int exit_code = 0;
    std::vector<HANDLE> wait_handles;
    wait_handles.push_back(g_headless_stop_event);
    for (const auto& process : managed_processes) {
        if (process.process) {
            wait_handles.push_back(process.process);
        }
    }

    for (;;) {
        const DWORD wait_result = WaitForMultipleObjects(static_cast<DWORD>(wait_handles.size()), wait_handles.data(), FALSE, 1000);
        if (wait_result == WAIT_OBJECT_0) {
            break;
        }
        if (wait_result > WAIT_OBJECT_0 && wait_result < WAIT_OBJECT_0 + wait_handles.size()) {
            const size_t process_index = static_cast<size_t>(wait_result - WAIT_OBJECT_0 - 1);
            const int port = process_index < managed_processes.size() ? managed_processes[process_index].port : 0;
            std::wstring message = L"Managed Ollama process exited unexpectedly";
            if (port > 0) {
                message += L" on port " + std::to_wstring(port);
            }
            message += L".";
            HeadlessWriteLine(message);
            exit_code = 5;
            break;
        }
    }

    StopHeadlessManagedProcesses(managed_processes);
    SetConsoleCtrlHandler(HeadlessConsoleControlHandler, FALSE);
    if (g_headless_stop_event) {
        CloseHandle(g_headless_stop_event);
        g_headless_stop_event = nullptr;
    }
    return exit_code;
}

int RunHeadlessOllamaCommand(HeadlessCommandMode mode, const std::filesystem::path& settings_path) {
    EnsureHeadlessConsoleAttached();
    if (mode == HeadlessCommandMode::RemoteWorkerSetup ||
        mode == HeadlessCommandMode::RemoteWorkerRun) {
        return RunRemoteProviderWorkerCommand(
            mode == HeadlessCommandMode::RemoteWorkerSetup
                ? RemoteProviderWorkerCommandMode::Setup
                : RemoteProviderWorkerCommandMode::Run,
            settings_path);
    }
    if (settings_path.empty()) {
        HeadlessWriteLine(L"Missing image ingest settings JSON path.");
        HeadlessWriteLine(L"Usage:");
        HeadlessWriteLine(L"  agent --olama-setup rag_image_ingest_settings.json");
        HeadlessWriteLine(L"  agent --olama-remote rag_image_ingest_settings.json");
        HeadlessWriteLine(L"  agent --remote-worker-setup worker.json");
        HeadlessWriteLine(L"  agent --remote-worker worker.json");
        return 2;
    }
    if (mode == HeadlessCommandMode::OllamaSetup) {
        return RunHeadlessOllamaSetup(settings_path);
    }
    if (mode == HeadlessCommandMode::OllamaRemote) {
        return RunHeadlessOllamaRemote(settings_path);
    }
    return 0;
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

// Build a "Previously Retrieved RAG Context" block from the per-chat working set.
// max_token_budget == 0 means no limit; otherwise chunks are included until the budget is consumed.
std::string BuildWorkingSetContextBlock(const std::vector<RagWorkingSetEntry>& entries, size_t max_token_budget = 0) {
    if (entries.empty()) return {};
    constexpr size_t kHeaderTokens = 30;
    constexpr size_t kChunkHeaderTokens = 20;
    const size_t budget = max_token_budget > 0 ? max_token_budget : SIZE_MAX;
    size_t tokens_used = kHeaderTokens;

    std::ostringstream stream;
    stream << "Previously Retrieved RAG Context:\n";
    stream << "The following excerpts were retrieved by earlier tool calls this session. They are provided for continuity; the assistant need not re-search for them.\n";

    bool any = false;
    for (const auto& entry : entries) {
        // Estimate tokens: ~4 chars per token
        const size_t chunk_tokens = kChunkHeaderTokens + entry.text.size() / 4 + 1;
        if (tokens_used + chunk_tokens > budget) break;
        tokens_used += chunk_tokens;
        any = true;
        stream << "\n[RAG: " << entry.rag_name
               << " | Source: " << entry.document_title
               << " | Chunk: " << entry.chunk_id
               << " | Score: " << entry.score
               << " | Query: \"" << entry.query << "\"]\n";
        stream << entry.text << "\n";
    }
    return any ? stream.str() : std::string{};
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
        // Skip bindings that are not configured for active tool exposure
        if (binding.retrieval_mode == RagRetrievalMode::PassiveOnly ||
            binding.retrieval_mode == RagRetrievalMode::Disabled) {
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
    std::vector<ProjectMcpVariableValue> variable_values;
    for (const auto& item : values) {
        variable_resolver::UpsertValue(variable_values, item.first, item.second);
    }
    if (error) error->clear();
    return variable_resolver::ExpandTemplate(text, variable_values);
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

// Model tool aliases follow the pattern "agent_<sanitized_name>"
bool IsModelToolAlias(const std::string& name) {
    return name.size() > 6 && name.substr(0, 6) == "agent_";
}

// Sanitize a tool name to a valid identifier (lowercase, underscores, alphanumeric only)
std::string SanitizeToolName(const std::string& name) {
    std::string result;
    result.reserve(name.size());
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            result += '_';
        }
    }
    // Collapse consecutive underscores
    std::string collapsed;
    collapsed.reserve(result.size());
    bool last_was_under = false;
    for (char c : result) {
        if (c == '_') {
            if (!last_was_under && !collapsed.empty()) {
                collapsed += c;
            }
            last_was_under = true;
        } else {
            collapsed += c;
            last_was_under = false;
        }
    }
    // Strip trailing underscore
    while (!collapsed.empty() && collapsed.back() == '_') collapsed.pop_back();
    return collapsed;
}

bool IsRagToolName(const std::string& name) {
    return name == kRagListLibrariesToolName ||
        name == kRagSearchToolName ||
        name == kRagGetDocumentToolName ||
        name == kRagIngestGeneratedDocumentToolName ||
        name == kRagWriteDocumentToDriveToolName;
}

McpToolCallResult CallRagTool(RagService* rag_service, const McpManager* mcp_manager, const std::string& project_id, const std::string& tool_name, const std::string& arguments_json, std::vector<RagWorkingSetEntry>* working_set_out = nullptr) {
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

        // Accumulate retrieved chunks into the per-chat working set
        if (working_set_out) {
            const std::string ts = [] {
                std::time_t t = std::time(nullptr);
                char buf[32]{};
                std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
                return std::string(buf);
            }();
            for (const auto& hit : hits) {
                if (!hit.result.text.empty()) {
                    const bool already = std::any_of(working_set_out->begin(), working_set_out->end(),
                        [&](const RagWorkingSetEntry& e) { return e.chunk_id == hit.result.chunk_id; });
                    if (!already) {
                        RagWorkingSetEntry ws;
                        ws.chunk_id = hit.result.chunk_id;
                        ws.rag_id = hit.result.rag_id;
                        ws.rag_name = hit.result.rag_name;
                        ws.document_id = hit.result.document_id;
                        ws.document_title = hit.result.document_title;
                        ws.text = hit.result.text;
                        ws.score = hit.result.score;
                        ws.query = query;
                        ws.retrieved_at = ts;
                        working_set_out->push_back(std::move(ws));
                    }
                }
            }
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

// Run a model tool sub-agent to completion and return its final reply as a tool result.
// Runs entirely on the calling thread (use from a worker thread).
McpToolCallResult CallModelToolAgent(
    const std::string& tool_alias,           // e.g. "agent_code_reviewer"
    const std::string& arguments_json,       // {"instructions": "..."} from main model
    const std::string& project_id,
    McpManager* mcp_manager,
    RagService* rag_service,
    const std::vector<ProviderConfig>& providers,
    const std::vector<ModelToolConfig>& model_tools,
    const std::vector<ProjectMcpVariableValue>& project_variables = {},
    const std::vector<ProjectMcpVariableValue>& runtime_variables = {})
{
    // Find the tool config matching this alias
    const ModelToolConfig* tool_config = nullptr;
    for (const auto& mt : model_tools) {
        if ("agent_" + SanitizeToolName(mt.name) == tool_alias) {
            tool_config = &mt;
            break;
        }
    }
    if (!tool_config) {
        McpToolCallResult err;
        err.success = false;
        err.is_tool_error = true;
        err.content_text = "Model tool '" + tool_alias + "' configuration not found.";
        return err;
    }

    // Parse arguments â€“ expect { "instructions": "..." }
    std::string task_instructions;
    try {
        if (!Trim(arguments_json).empty()) {
            const auto args = nlohmann::json::parse(arguments_json);
            if (args.is_object() && args.contains("instructions") && args["instructions"].is_string()) {
                task_instructions = args["instructions"].get<std::string>();
            }
        }
    } catch (...) {}

    if (task_instructions.empty()) {
        McpToolCallResult err;
        err.success = false;
        err.is_tool_error = true;
        err.content_text = "Model tool '" + tool_alias + "' requires a non-empty 'instructions' argument.";
        return err;
    }

    // Resolve which provider/model to use
    std::string use_provider_id = tool_config->preferred_provider_id;
    std::string use_model_id    = tool_config->preferred_model_id;
    if (use_provider_id.empty() || use_model_id.empty()) {
        // Fall back to first available model
        if (!providers.empty() && !providers.front().models.empty()) {
            use_provider_id = providers.front().id;
            use_model_id    = providers.front().models.front().id;
        }
    }

    // Find the ProviderConfig and ModelConfig
    ProviderConfig use_provider;
    ModelConfig use_model;
    bool found_model = false;
    for (const auto& prov : providers) {
        if (prov.id == use_provider_id) {
            for (const auto& model : prov.models) {
                if (model.id == use_model_id) {
                    use_provider = prov;
                    use_model    = model;
                    found_model  = true;
                    break;
                }
            }
        }
        if (found_model) break;
    }

    if (!found_model) {
        McpToolCallResult err;
        err.success = false;
        err.is_tool_error = true;
        err.content_text = "Model tool '" + tool_alias + "': configured model not found in providers.";
        return err;
    }

    // Get exposed MCP tools for this sub-agent (scoped to its mcp_bindings)
    // We reuse GetExposedToolsForProject filtered by the tool's server bindings
    std::vector<std::string> allowed_server_ids;
    for (const auto& b : tool_config->mcp_bindings) {
        allowed_server_ids.push_back(b.server_id);
    }

    for (const auto& config : mcp_manager->configs()) {
        if (config.enabled && config.auto_connect &&
            mcp_manager->IsServerSelectedForProject(project_id, config.id)) {
            std::string ignored;
            mcp_manager->ConnectServer(config.id, project_id, &ignored, runtime_variables);
        }
    }

    const auto all_exposed = mcp_manager->GetExposedToolsForProject(project_id, runtime_variables);
    std::vector<McpExposedTool> sub_exposed;
    for (const auto& t : all_exposed) {
        if (std::find(allowed_server_ids.begin(), allowed_server_ids.end(), t.server_id) != allowed_server_ids.end()) {
            sub_exposed.push_back(t);
        }
    }

    // RAGs are exposed as virtual MCP servers, one server per project-bound library.
    const auto sub_rag_tools = rag_tools::BuildRagToolSet(rag_service, project_id);

    // Build full tool definitions
    std::vector<ChatToolDefinition> tool_definitions;
    for (const auto& exposed : sub_exposed) {
        ChatToolDefinition def;
        def.name = exposed.alias;
        def.description = exposed.description.empty()
            ? ("MCP tool from server \"" + exposed.server_name + "\" named \"" + exposed.tool_name + "\".")
            : (exposed.description + " (MCP server: " + exposed.server_name + ", tool: " + exposed.tool_name + ")");
        def.parameters_json = exposed.input_schema_json;
        tool_definitions.push_back(std::move(def));
    }
    for (const auto& rd : sub_rag_tools.definitions) {
        tool_definitions.push_back(rd);
    }

    const bool include_tools = use_model.supports_tools && !tool_definitions.empty();

    // Build the initial request
    ChatRequestOptions sub_request;
    sub_request.provider      = use_provider;
    sub_request.model         = use_model;
    // Apply project variable substitution to the instructions: $<VarName> â†’ value
    {
        sub_request.system_prompt =
            variable_resolver::ExpandTemplate(tool_config->instructions, project_variables);
    }
    sub_request.temperature   = 0.2;
    sub_request.max_tokens    = 4096;

    // Passive/pre-searched RAG injection is intentionally inactive for now.
    // Project RAG access is available through the active virtual MCP tools above.

    // Append structured JSON reply format requirement to system prompt
    sub_request.system_prompt +=
        "\n\n## Required Response Format\n"
        "You MUST end your final response with a valid JSON object using this exact structure:\n"
        "```json\n"
        "{\n"
        "  \"status\": \"success\" | \"partial\" | \"error\",\n"
        "  \"summary\": \"<one-sentence description of what was accomplished>\",\n"
        "  \"result\": <primary output â€” string, object, or array>,\n"
        "  \"actions\": [\"<action 1>\", \"<action 2>\", ...]\n"
        "}\n"
        "```\n"
        "- status: \"success\" if fully completed, \"partial\" if only partially done, \"error\" if the task failed.\n"
        "- summary: concise human-readable description of what happened.\n"
        "- result: the main output data, text, or analysis.\n"
        "- actions: list of concrete steps taken (tool calls made, files modified, searches performed, etc.).";

    // Initial user message is the task instructions from the main model
    {
        MessageRecord user_msg;
        user_msg.role       = "user";
        user_msg.content    = task_instructions;
        user_msg.created_at = CurrentTimestampUtc();
        sub_request.messages.push_back(user_msg);
    }

    constexpr int kSubAgentMaxRounds = 6;
    std::vector<MessageRecord> working_messages = sub_request.messages;
    std::string last_reply;

    for (int round = 0; round < kSubAgentMaxRounds; ++round) {
        ChatRequestOptions loop_request = sub_request;
        loop_request.messages = working_messages;

        const auto completion = OpenAIClient::CreateToolAwareCompletion(
            loop_request, include_tools ? tool_definitions : std::vector<ChatToolDefinition>{});

        if (!completion.success) {
            McpToolCallResult err;
            err.success = false;
            err.is_tool_error = true;
            err.content_text = "Model tool '" + tool_alias + "' failed: " + completion.error;
            return err;
        }

        if (!completion.tool_calls.empty()) {
            // Record the assistant message with tool calls
            MessageRecord assistant_msg;
            assistant_msg.role       = "assistant";
            assistant_msg.content    = completion.assistant_text;
            assistant_msg.created_at = CurrentTimestampUtc();
            if (!completion.raw_message_json.empty()) {
                try {
                    const auto mj = nlohmann::json::parse(completion.raw_message_json);
                    if (mj.contains("tool_calls")) {
                        assistant_msg.tool_calls_json = mj["tool_calls"].dump();
                    }
                } catch (...) {}
            }
            if (assistant_msg.tool_calls_json.empty()) {
                nlohmann::json tca = nlohmann::json::array();
                for (const auto& tc : completion.tool_calls) {
                    tca.push_back({{"id", tc.id}, {"type", "function"}, {"function", {{"name", tc.name}, {"arguments", tc.arguments_json}}}});
                }
                assistant_msg.tool_calls_json = tca.dump();
            }
            working_messages.push_back(std::move(assistant_msg));

            // Execute each tool call
            for (const auto& tc : completion.tool_calls) {
                McpToolCallResult result;
                if (!tc.arguments_valid) {
                    result.success = false;
                    result.is_tool_error = true;
                    result.content_text = "Invalid arguments for tool '" + tc.name + "'.";
                } else if (rag_tools::IsRagToolName(tc.name, &sub_rag_tools.routes)) {
                    result = rag_tools::CallRagTool(
                        rag_service,
                        mcp_manager,
                        project_id,
                        tc.name,
                        tc.arguments_json,
                        &sub_rag_tools.routes,
                        nullptr,
                        project_variables);
                } else {
                    result = mcp_manager->CallExposedTool(
                        project_id, tc.name, tc.arguments_json, runtime_variables);
                }
                MessageRecord tool_msg;
                tool_msg.role        = "tool";
                tool_msg.name        = tc.name;
                tool_msg.tool_call_id= tc.id;
                tool_msg.content     = result.content_text;
                tool_msg.created_at  = CurrentTimestampUtc();
                working_messages.push_back(std::move(tool_msg));
            }
            continue;
        }

        // No tool calls â†’ final answer
        last_reply = completion.assistant_text;
        break;
    }

    McpToolCallResult result;
    result.success      = true;
    result.content_text = last_reply.empty() ? "(no response from model tool)" : last_reply;
    result.raw_result_json = nlohmann::json{{"tool", tool_alias}, {"result", result.content_text}}.dump(2);
    return result;
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
    ~MainWindow();
    HWND Create(HINSTANCE instance);
    void OpenWebConfig();  // public so command-line options can trigger it
    HWND GetAgenticModesWindow() const { return agentic_modes_window_; }

private:
    static int Scale(HWND hwnd, int value);
    static void RegisterWindowClass(HINSTANCE instance);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

    void OnCreate();
    void OnStartupInitialize();
    void StartBackgroundServices();
    void OnStartupServicesFinished();
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
    void OpenModelTools();
    void OpenAgenticModes();
    void EditProjectSettings();
    void RunSetupSystem();
    bool ApplyEmbeddedConfigPackage(std::wstring* error);
    void OnSetupSystemFinished();
    void RestartApplication();
    void EnsureWebServer();   // lazily creates web_server_ if not yet constructed
    void OpenAdminConfig();
    void SetupDefaultMcpServersIfEmpty();
    void ReloadProjects(const std::string& preferred_project_id, const std::string& preferred_chat_id);
    void RefreshTree();
    void LoadActiveMessages();
    std::string BuildMcpProjectContext() const;
    void SendCurrentMessage();
    void CompressCurrentContext();
    void ShowCurrentContextMessages();
    void OnChatDelta(ChatDeltaPayload* payload);
    void OnChatFinished(ChatFinishedPayload* payload);
    void OnToolTrace(ToolTracePayload* payload);
    void OnMcpStateChanged();
    void OnWebContentChanged();
    void RenderTranscript();
    void RenderToolTrace();
    void UpdateStatus(const std::wstring& text);
    void SetChatBusy(const std::string& chat_id, bool busy);
    void RefreshInputState();
    bool IsChatInFlight(const std::string& chat_id) const;
    int CountChatsInFlight() const;
    void UpdateChatTreeLabel(const std::string& chat_id);
    std::vector<ProjectMcpVariableValue> BuildRuntimeVariablesForChat(
        const std::string& project_id,
        const std::string& chat_id) const;
    std::vector<ProjectMcpVariableValue> BuildResolvedVariablesForChat(
        const std::string& project_id,
        const std::string& chat_id,
        const ProjectSettings& project_settings) const;
    ProjectRecord* FindProject(const std::string& project_id);
    ChatInfo* FindChat(const std::string& project_id, const std::string& chat_id);

    HWND hwnd_ = nullptr;
    HWND tree_ = nullptr;
    HWND new_project_button_ = nullptr;
    HWND new_chat_button_ = nullptr;
    HWND rename_button_ = nullptr;
    HWND delete_button_ = nullptr;
    HWND providers_button_ = nullptr;
    HWND mcp_servers_button_ = nullptr;
    HWND project_mcp_button_ = nullptr;
    HWND model_tools_button_ = nullptr;
    HWND agentic_modes_button_ = nullptr;
    HWND web_config_button_ = nullptr;
    HWND admin_config_button_ = nullptr;
    HWND remote_ollama_setup_button_ = nullptr;
    HWND rag_service_button_ = nullptr;
    HWND context_window_button_ = nullptr;
    HWND setup_system_button_ = nullptr;
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
    HWND model_tools_window_ = nullptr;
    HWND agentic_modes_window_ = nullptr;
    HFONT font_ = nullptr;

    AppStorage storage_;
    McpManager mcp_manager_;
    WebUserStore user_store_{ std::filesystem::path{} };  // path set in constructor
    std::unique_ptr<WebServer> web_server_;
    RagService rag_service_;
    ContextCompressionService compression_service_;
    std::vector<ProviderConfig> providers_;
    std::vector<ProjectRecord> projects_;
    std::vector<std::unique_ptr<TreeItemData>> tree_items_;
    std::unordered_map<std::string, std::vector<ToolTraceEntry>> tool_traces_by_chat_;

    std::string active_project_id_;
    std::string active_chat_id_;
    std::vector<MessageRecord> active_messages_;
    std::unordered_map<std::string, bool> chats_in_flight_;
    std::unordered_map<std::string, std::wstring> streaming_previews_by_chat_;
    std::unordered_map<std::string, HTREEITEM> chat_tree_items_;
    std::unordered_map<std::string, std::vector<RagWorkingSetEntry>> rag_working_sets_by_chat_;
    std::thread startup_worker_;
    bool active_messages_skipped_for_size_ = false;
    std::uintmax_t active_messages_file_size_ = 0;
    bool refreshing_tree_ = false;
};

MainWindow::MainWindow()
    : storage_(DetermineAppRoot())
    , mcp_manager_(&storage_)
    , rag_service_(&storage_)
    , compression_service_(&storage_)
    , user_store_(ResolveWebUsersPath())
{}

MainWindow::~MainWindow() {
    if (startup_worker_.joinable()) {
        startup_worker_.join();
    }
}

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
    case kWebContentChangedMessage:
        self->OnWebContentChanged();
        return 0;
    case kStartupInitializeMessage:
        self->OnStartupInitialize();
        return 0;
    case kStartupServicesFinishedMessage:
        self->OnStartupServicesFinished();
        return 0;
    case kSetupSystemFinishedMessage:
        self->OnSetupSystemFinished();
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
    Logger::Info("Main window WM_CREATE: creating controls");
    font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    new_project_button_ = CreateWindowExW(0, L"BUTTON", L"New Project", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kNewProject), nullptr, nullptr);
    new_chat_button_ = CreateWindowExW(0, L"BUTTON", L"New Chat", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kNewChat), nullptr, nullptr);
    rename_button_ = CreateWindowExW(0, L"BUTTON", L"Rename", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRename), nullptr, nullptr);
    delete_button_ = CreateWindowExW(0, L"BUTTON", L"Delete", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kDelete), nullptr, nullptr);

    tree_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kTree), nullptr, nullptr);
    providers_button_ = CreateWindowExW(0, L"BUTTON", L"Providers", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kProviders), nullptr, nullptr);
    mcp_servers_button_ = CreateWindowExW(0, L"BUTTON", L"MCP Servers", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kMcpServers), nullptr, nullptr);
    project_mcp_button_ = CreateWindowExW(0, L"BUTTON", L"Project Settings", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kProjectMcp), nullptr, nullptr);
    model_tools_button_ = CreateWindowExW(0, L"BUTTON", L"Model Tools", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kModelTools), nullptr, nullptr);
    agentic_modes_button_ = CreateWindowExW(0, L"BUTTON", L"Agentic Modes", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kAgenticModes), nullptr, nullptr);
    web_config_button_   = CreateWindowExW(0, L"BUTTON", L"Web Config",  WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kWebConfig),   nullptr, nullptr);
    admin_config_button_ = CreateWindowExW(0, L"BUTTON", L"Admin Config", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kAdminConfig), nullptr, nullptr);
    remote_ollama_setup_button_ = CreateWindowExW(0, L"BUTTON", L"Remote Model Config", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRemoteOllamaSetup), nullptr, nullptr);
    rag_service_button_ = CreateWindowExW(0, L"BUTTON", L"RAG Service", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kRagService), nullptr, nullptr);
    context_window_button_ = CreateWindowExW(0, L"BUTTON", L"Context Window", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kContextWindow), nullptr, nullptr);
    setup_system_button_ = CreateWindowExW(0, L"BUTTON", L"Setup System", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kSetupSystem), nullptr, nullptr);
    transcript_ = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kTranscript), nullptr, nullptr);
    tool_trace_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, nullptr, WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kToolTrace), nullptr, nullptr);
    input_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kInput), nullptr, nullptr);
    send_button_ = CreateWindowExW(0, L"BUTTON", L"Send", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kSend), nullptr, nullptr);
    compress_button_ = CreateWindowExW(0, L"BUTTON", L"Compress", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kCompress), nullptr, nullptr);
    context_messages_button_ = CreateWindowExW(0, L"BUTTON", L"Context Msgs", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kContextMessages), nullptr, nullptr);
    status_label_ = CreateWindowExW(0, L"STATIC", L"Initializing...", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kStatus), nullptr, nullptr);

    for (HWND control : {new_project_button_, new_chat_button_, rename_button_, delete_button_, tree_, providers_button_, mcp_servers_button_, project_mcp_button_, model_tools_button_, agentic_modes_button_, web_config_button_, admin_config_button_, remote_ollama_setup_button_, rag_service_button_, context_window_button_, setup_system_button_, transcript_, tool_trace_, input_, send_button_, compress_button_, context_messages_button_, status_label_}) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }

    SendMessageW(transcript_, EM_SETBKGNDCOLOR, 0, GetSysColor(COLOR_WINDOW));

    UpdateStatus(L"Starting...");
    Logger::Info("Main window WM_CREATE: controls created; deferring startup initialization");
    PostMessageW(hwnd_, kStartupInitializeMessage, 0, 0);
}

void MainWindow::OnStartupInitialize() {
    try {
        Logger::Info("Startup initialization: storage begin");
        storage_.EnsureInitialized();
        MigrateLegacyWebSettings(storage_.runtime_paths());
        user_store_.EnsureInitialized();

        Logger::Info("Startup initialization: MCP config load begin");
        mcp_manager_.Load();
        mcp_manager_.SetStateChangedCallback([hwnd = hwnd_]() {
            PostMessageW(hwnd, kMcpChangedMessage, 0, 0);
        });

        Logger::Info("Startup initialization: load UI state begin");
        LoadState();
        UpdateStatus(L"Ready. Starting background services...");

        // Auto-start web server if configured. WebServer::Start launches its
        // listener thread and returns, so this should not block first paint.
        Logger::Info("Startup initialization: web server autostart check begin");
        const auto settings_path = ResolveWebSettingsPath(storage_.runtime_paths());
        if (std::filesystem::exists(settings_path)) {
            auto cfg = WebServerConfig::LoadFromFile(settings_path);
            if (cfg.auto_start) {
                EnsureWebServer();
                if (web_server_->Start()) {
                    UpdateStatus((L"Web server auto-started on port "
                                 + std::to_wstring(cfg.port) + L".").c_str());
                }
            }
        }

        StartBackgroundServices();
        Logger::Info("Startup initialization: complete");
    } catch (const std::exception& ex) {
        Logger::Error(std::string("Startup initialization failed: ") + ex.what());
        UpdateStatus((L"Startup failed: " + Utf8ToWide(ex.what())).c_str());
        MessageBoxW(hwnd_, Utf8ToWide(ex.what()).c_str(), L"Agent Startup Error", MB_OK | MB_ICONERROR);
    } catch (...) {
        Logger::Error("Startup initialization failed with an unknown error");
        UpdateStatus(L"Startup failed.");
        MessageBoxW(hwnd_, L"An unknown startup error occurred.", L"Agent Startup Error", MB_OK | MB_ICONERROR);
    }
}

void MainWindow::StartBackgroundServices() {
    if (startup_worker_.joinable()) {
        return;
    }

    const std::string project_id = active_project_id_;
    startup_worker_ = std::thread([this, project_id]() {
        Logger::Info("Startup background services: RAG initialization begin");
        try {
            rag_service_.EnsureInitialized();
        } catch (const std::exception& ex) {
            Logger::Warn(std::string("Startup background services: RAG initialization failed: ") + ex.what());
        } catch (...) {
            Logger::Warn("Startup background services: RAG initialization failed with an unknown error");
        }

        Logger::Info("Startup background services: MCP auto-connect begin");
        try {
            mcp_manager_.ConnectAutoServers(project_id);
        } catch (const std::exception& ex) {
            Logger::Warn(std::string("Startup background services: MCP auto-connect failed: ") + ex.what());
        } catch (...) {
            Logger::Warn("Startup background services: MCP auto-connect failed with an unknown error");
        }

        Logger::Info("Startup background services: complete");
        if (hwnd_) {
            PostMessageW(hwnd_, kStartupServicesFinishedMessage, 0, 0);
        }
    });
}

void MainWindow::OnStartupServicesFinished() {
    UpdateStatus(L"Ready.");
    OnMcpStateChanged();
}

void MainWindow::LoadState() {
    Logger::Info("LoadState: providers begin");
    providers_ = storage_.LoadProviders();
    OpenAIClient::SetProviderCache(providers_);
    Logger::Info("LoadState: projects begin");
    projects_ = storage_.LoadProjects();
    Logger::Info("LoadState: selection begin");
    EnsureDefaultProjectAndChat();
    ChooseValidSelection();
    Logger::Info("LoadState: refresh tree begin");
    RefreshTree();
    Logger::Info("LoadState: active messages begin");
    LoadActiveMessages();
    RefreshInputState();
    Logger::Info("LoadState: render transcript begin");
    RenderTranscript();
    Logger::Info("LoadState: render tool trace begin");
    RenderToolTrace();
    Logger::Info("LoadState: complete");
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
    const int remote_button_y = rag_button_y - button_height - gutter;
    const int tree_top = margin + (button_height + gutter) * 2;
    MoveWindow(tree_, margin, tree_top, left_width, std::max(Scale(hwnd_, 80), remote_button_y - gutter - tree_top), TRUE);
    const int half_width = (left_width - gutter) / 2;
    MoveWindow(remote_ollama_setup_button_, margin, remote_button_y, left_width, button_height, TRUE);
    MoveWindow(rag_service_button_, margin, rag_button_y, half_width, button_height, TRUE);
    MoveWindow(context_window_button_, margin + half_width + gutter, rag_button_y, half_width, button_height, TRUE);

    const int right_x = margin + left_width + gutter * 2;
    const int right_width = width - right_x - margin;
    const int providers_width    = Scale(hwnd_, 100);
    const int mcp_width          = Scale(hwnd_, 80);
    const int project_mcp_width  = Scale(hwnd_, 120);
    const int model_tools_width  = Scale(hwnd_, 100);
    const int agentic_modes_width = Scale(hwnd_, 110);
    const int web_config_width   = Scale(hwnd_, 90);
    const int admin_config_width = Scale(hwnd_, 100);
    const int settings_width     = Scale(hwnd_, 110);

    int bx = right_x;
    MoveWindow(providers_button_,    bx, margin, providers_width,    top_row_height, TRUE); bx += providers_width + gutter;
    MoveWindow(mcp_servers_button_,  bx, margin, mcp_width,          top_row_height, TRUE); bx += mcp_width + gutter;
    MoveWindow(project_mcp_button_,  bx, margin, project_mcp_width,  top_row_height, TRUE); bx += project_mcp_width + gutter;
    MoveWindow(model_tools_button_,  bx, margin, model_tools_width,  top_row_height, TRUE); bx += model_tools_width + gutter;
    MoveWindow(agentic_modes_button_, bx, margin, agentic_modes_width, top_row_height, TRUE); bx += agentic_modes_width + gutter;
    MoveWindow(web_config_button_,   bx, margin, web_config_width,   top_row_height, TRUE); bx += web_config_width + gutter;
    MoveWindow(admin_config_button_, bx, margin, admin_config_width, top_row_height, TRUE); bx += admin_config_width + gutter;
    MoveWindow(setup_system_button_, bx, margin, settings_width,     top_row_height, TRUE);

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
    case kModelTools:
        OpenModelTools();
        break;
    case kAgenticModes:
        OpenAgenticModes();
        break;
    case kWebConfig:
        OpenWebConfig();
        break;
    case kAdminConfig:
        OpenAdminConfig();
        break;
    case kRemoteOllamaSetup:
        ShowRemoteOllamaSetupDialog(hwnd_, &storage_, &providers_);
        break;
    case kRagService:
        OpenRagServiceManager();
        break;
    case kContextWindow:
        OpenCompressionManager();
        break;
    case kSetupSystem:
        RunSetupSystem();
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
            RefreshInputState();
            RenderTranscript();
            RenderToolTrace();
        } else {
            active_chat_id_.clear();
            if (ProjectRecord* project = FindProject(item->project_id); project && !project->chats.empty()) {
                active_chat_id_ = project->chats.front().id;
                LoadActiveMessages();
                RefreshInputState();
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

    // Inherit the project's preferred model for the new chat
    auto proj_settings_new_chat = storage_.LoadProjectSettings(active_project_id_);
    const std::string provider_id = proj_settings_new_chat.preferred_provider_id;
    const std::string model_id = proj_settings_new_chat.preferred_model_id;

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

void MainWindow::OpenModelTools() {
    if (model_tools_window_ && IsWindow(model_tools_window_)) {
        SetForegroundWindow(model_tools_window_);
        return;
    }
    model_tools_window_ = OpenModelToolsManager(hwnd_, &storage_, providers_, &mcp_manager_, &rag_service_);
}

void MainWindow::OpenAgenticModes() {
    if (agentic_modes_window_ && IsWindow(agentic_modes_window_)) {
        SetForegroundWindow(agentic_modes_window_);
        return;
    }
    agentic_modes_window_ = OpenAgenticModesManager(hwnd_, &storage_);
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

    // Pass provider list and project's preferred model
    options.providers = providers_;
    options.preferred_provider_id = project_settings.preferred_provider_id;
    options.preferred_model_id = project_settings.preferred_model_id;

    // Model tools
    options.model_tools = storage_.LoadModelTools();
    options.initial_model_tool_ids = project_settings.model_tool_ids;

    // Agentic modes
    options.agentic_modes = storage_.LoadAgenticModes();
    options.selected_agentic_mode_id = project_settings.selected_agentic_mode_id;
    options.enabled_agentic_mode_ids = project_settings.enabled_agentic_mode_ids;

    // Project variables
    options.initial_project_variables = project_settings.project_variables;
    options.enable_chat_logging = project_settings.enable_chat_logging;
    options.allow_manual_context_compression = project_settings.allow_manual_context_compression;
    options.enable_web_debugging = project_settings.enable_web_debugging;
    options.serve_web_links_inline = project_settings.serve_web_links_inline;
    options.enable_automation = project_settings.enable_automation;
    options.built_in_powershell_enabled = project_settings.built_in_powershell_enabled;
    options.built_in_powershell_working_directory = project_settings.built_in_powershell_working_directory;
    options.built_in_artifact_memory_enabled = project_settings.built_in_artifact_memory_enabled;
    options.built_in_planner_enabled = project_settings.built_in_planner_enabled;
    options.built_in_planner_storage_folder = project_settings.built_in_planner_storage_folder;
    options.built_in_completion_driver_enabled = project_settings.built_in_completion_driver_enabled;
    options.completion_driver_allowed_mode_ids = project_settings.completion_driver_allowed_mode_ids;
    options.completion_driver_max_continuations = project_settings.completion_driver_max_continuations;
    options.built_in_questionnaire_enabled = project_settings.built_in_questionnaire_enabled;
    options.questionnaire_max_options = project_settings.questionnaire_max_options;
    options.questionnaire_restrict_by_mode = project_settings.questionnaire_restrict_by_mode;
    options.questionnaire_allowed_mode_id = project_settings.questionnaire_allowed_mode_id;
    options.built_in_filesystem_enabled = project_settings.built_in_filesystem_enabled;
    options.built_in_filesystem_auto_archive = project_settings.built_in_filesystem_auto_archive;
    options.built_in_filesystem_working_directory = project_settings.built_in_filesystem_working_directory;
    options.model_timeout_seconds = project_settings.model_timeout_seconds;

    // Pre-seed ProjectFolder from MCP global binding values if not already set.
    for (const auto& gv : mcp_manager_.global_variables()) {
        const bool already_set = std::find_if(
            options.initial_project_variables.begin(), options.initial_project_variables.end(),
            [&](const ProjectMcpVariableValue& pv) { return pv.name == gv.name; }
        ) != options.initial_project_variables.end();
        if (already_set) continue;
        // Find the value from MCP bindings stored in project_settings
        for (const auto& binding : project_settings.mcp_bindings) {
            auto it = std::find_if(binding.variables.begin(), binding.variables.end(),
                [&](const ProjectMcpVariableValue& v) { return v.name == gv.name; });
            if (it != binding.variables.end()) {
                options.initial_project_variables.push_back(*it);
                break;
            }
        }
    }

    const auto result = ShowProjectSettingsDialog(hwnd_, options);
    if (!result) {
        return;
    }

    // Save MCP bindings
    mcp_manager_.SaveProjectBindings(active_project_id_, result->mcp_bindings);

    // Save project settings (including selected compression config - global configs are managed separately)
    ProjectSettings saved_settings;
    saved_settings.project_name = result->project_name;
    saved_settings.project_instructions = result->project_instructions;
    saved_settings.mcp_bindings = result->mcp_bindings;
    saved_settings.selected_compression_config_id = result->selected_compression_config_id;
    saved_settings.rag_bindings = result->rag_bindings;
    saved_settings.preferred_provider_id = result->preferred_provider_id;
    saved_settings.preferred_model_id = result->preferred_model_id;
    saved_settings.model_tool_ids = result->model_tool_ids;
    saved_settings.project_variables = result->project_variables;
    saved_settings.selected_agentic_mode_id = result->selected_agentic_mode_id;
    saved_settings.enabled_agentic_mode_ids = result->enabled_agentic_mode_ids;
    saved_settings.enable_chat_logging = result->enable_chat_logging;
    saved_settings.allow_manual_context_compression = result->allow_manual_context_compression;
    saved_settings.enable_web_debugging = result->enable_web_debugging;
    saved_settings.serve_web_links_inline = result->serve_web_links_inline;
    saved_settings.enable_automation = result->enable_automation;
    saved_settings.built_in_powershell_enabled = result->built_in_powershell_enabled;
    saved_settings.built_in_powershell_working_directory = result->built_in_powershell_working_directory;
    saved_settings.built_in_artifact_memory_enabled = result->built_in_artifact_memory_enabled;
    saved_settings.built_in_planner_enabled = result->built_in_planner_enabled;
    saved_settings.built_in_planner_storage_folder = result->built_in_planner_storage_folder;
    saved_settings.built_in_completion_driver_enabled = result->built_in_completion_driver_enabled;
    saved_settings.completion_driver_allowed_mode_ids = result->completion_driver_allowed_mode_ids;
    saved_settings.completion_driver_max_continuations = result->completion_driver_max_continuations;
    saved_settings.built_in_questionnaire_enabled = result->built_in_questionnaire_enabled;
    saved_settings.questionnaire_max_options = result->questionnaire_max_options;
    saved_settings.questionnaire_restrict_by_mode = result->questionnaire_restrict_by_mode;
    saved_settings.questionnaire_allowed_mode_id = result->questionnaire_allowed_mode_id;
    saved_settings.built_in_filesystem_enabled = result->built_in_filesystem_enabled;
    saved_settings.built_in_filesystem_auto_archive = result->built_in_filesystem_auto_archive;
    saved_settings.built_in_filesystem_working_directory = result->built_in_filesystem_working_directory;
    saved_settings.model_timeout_seconds = result->model_timeout_seconds;
    storage_.SaveProjectSettings(active_project_id_, saved_settings);
    storage_.SaveProjectCompressionSettings(active_project_id_, ProjectCompressionSettings{
        !result->selected_compression_config_id.empty(),
        result->selected_compression_config_id,
    });
    rag_service_.SaveProjectBindings(active_project_id_, result->rag_bindings);
    mcp_manager_.ConnectAutoServers(active_project_id_);

    RenderToolTrace();
    UpdateStatus(L"Project settings saved.");
}

// Returns true if a dedicated NVIDIA or AMD GPU adapter is present.
static bool DetectDedicatedGpu() {
    DISPLAY_DEVICEW adapter{};
    adapter.cb = sizeof(adapter);
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &adapter, 0); ++i) {
        if (adapter.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) {
            continue;
        }
        std::wstring desc = adapter.DeviceString;
        for (auto& c : desc) {
            c = static_cast<wchar_t>(towupper(c));
        }
        if (desc.find(L"NVIDIA") != std::wstring::npos ||
            desc.find(L"RADEON") != std::wstring::npos ||
            (desc.find(L"AMD") != std::wstring::npos &&
             desc.find(L"INTEL") == std::wstring::npos)) {
            return true;
        }
    }
    return false;
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// EnsureWebServer â€” create the WebServer instance if not yet created.
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
void MainWindow::EnsureWebServer() {
    if (web_server_) return;
    const auto settings_path = ResolveWebSettingsPath(storage_.runtime_paths());
    if (!std::filesystem::exists(settings_path)) {
        std::error_code ec;
        std::filesystem::create_directories(settings_path.parent_path(), ec);
        WebServerConfig default_cfg;
        default_cfg.SaveToFile(settings_path);
    }
    auto cfg = WebServerConfig::LoadFromFile(settings_path);
    web_server_ = std::make_unique<WebServer>(
        &storage_, &user_store_, cfg, storage_.runtime_paths(), &mcp_manager_, &rag_service_);
    web_server_->SetAuditLogPath(storage_.log_root() / "web_audit.log");
    web_server_->SetContentChangedCallback([hwnd = hwnd_]() {
        PostMessageW(hwnd, kWebContentChangedMessage, 0, 0);
    });
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Web Config â€” full settings dialog + Start/Stop
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
void MainWindow::OpenWebConfig() {
    Logger::Info("WebConfig", "OpenWebConfig: starting");
    const auto settings_path = ResolveWebSettingsPath(storage_.runtime_paths());

    // Ensure settings file and WebServer instance exist
    Logger::Info("WebConfig", "OpenWebConfig: calling EnsureWebServer");
    EnsureWebServer();
    Logger::Info("WebConfig", "OpenWebConfig: EnsureWebServer returned");

    // Load current config from file (dialog will read it live too)
    auto cfg = WebServerConfig::LoadFromFile(settings_path);

    // Show the dialog â€” it modifies cfg in place and handles Start/Stop
    Logger::Info("WebConfig", "OpenWebConfig: calling ShowWebConfigDialog");
    const bool accepted = ShowWebConfigDialog(hwnd_, web_server_.get(), &cfg, storage_.runtime_paths());
    Logger::Info("WebConfig", "OpenWebConfig: ShowWebConfigDialog returned");

    if (accepted) {
        // Persist updated config
        cfg.SaveToFile(settings_path);

        // Update the live server's config (Reconfigure handles restart if running)
        web_server_->Reconfigure(cfg);

        // Update status bar
        if (web_server_->IsRunning()) {
            UpdateStatus((L"Web server running on port "
                         + std::to_wstring(web_server_->Port()) + L".").c_str());
        } else {
            UpdateStatus(L"Web server stopped.");
        }
    }
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Admin Config â€” tabbed Users / Groups / Bindings dialog
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
void MainWindow::OpenAdminConfig() {
    // Ensure the WebServer is created so ForceLogout works
    EnsureWebServer();
    ShowAdminConfigDialog(hwnd_, web_server_.get(), &user_store_, &storage_);
}

void MainWindow::SetupDefaultMcpServersIfEmpty() {
    // Only write defaults when the server list is completely empty.
    if (!storage_.LoadMcpServers().empty()) {
        return;
    }

    // Build the six default MCP servers that match the shipped mcp_servers.json.
    auto make_server = [](std::string name, std::string command,
                          std::vector<std::string> args, std::string working_dir,
                          McpServerScope scope) {
        McpServerConfig s;
        s.id = MakeId("mcp_server");
        s.name = std::move(name);
        s.command = std::move(command);
        s.arguments = std::move(args);
        s.working_directory = std::move(working_dir);
        s.scope = scope;
        s.enabled = true;
        s.auto_connect = true;
        return s;
    };

    std::vector<McpServerConfig> servers;
    servers.push_back(make_server(
        "duckduckgo", "uvx",
        {"duckduckgo-mcp-server"}, "",
        McpServerScope::Shared));
    servers.push_back(make_server(
        "file-system", "npx",
        {"-y", "@modelcontextprotocol/server-filesystem", "$ProjectFolder$"},
        "$ProjectFolder$",
        McpServerScope::PerProject));
    servers.push_back(make_server(
        "sequential-thinking", "npx",
        {"-y", "@modelcontextprotocol/server-sequential-thinking"}, "",
        McpServerScope::PerProject));
    servers.push_back(make_server(
        "server-time", "uvx",
        {"mcp-server-time"}, "",
        McpServerScope::Shared));
    servers.push_back(make_server(
        "server-git", "uvx",
        {"mcp-server-git"}, "",
        McpServerScope::PerProject));
    servers.push_back(make_server(
        "desktop-commander", "npx",
        {"-y", "@wonderwhy-er/desktop-commander@latest"},
        "$ProjectFolder$",
        McpServerScope::PerProject));

    // Global variable: $ProjectFolder$ resolved per project.
    McpServerVariable pf;
    pf.name = "ProjectFolder";
    pf.description = "Project folder where the project files are located.";
    pf.kind = McpVariableKind::Folder;
    pf.inject_into_context = true;

    storage_.SaveMcpConfiguration(servers, {pf});
}

bool MainWindow::ApplyEmbeddedConfigPackage(std::wstring* error) {
    wchar_t temp_dir[MAX_PATH]{};
    const DWORD temp_len = GetTempPathW(static_cast<DWORD>(std::size(temp_dir)), temp_dir);
    if (temp_len == 0 || temp_len >= std::size(temp_dir)) {
        if (error) {
            *error = L"Could not resolve the Windows temporary directory.";
        }
        return false;
    }

    wchar_t temp_file[MAX_PATH]{};
    if (GetTempFileNameW(temp_dir, L"agc", 0, temp_file) == 0) {
        if (error) {
            *error = L"Could not create a temporary filename for the embedded configuration zip.";
        }
        return false;
    }

    std::filesystem::path temp_seed(temp_file);
    std::filesystem::path temp_zip = temp_seed;
    temp_zip.replace_extension(L".zip");

    std::error_code cleanup_ec;
    std::filesystem::remove(temp_seed, cleanup_ec);
    std::filesystem::remove(temp_zip, cleanup_ec);

    auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove(temp_seed, ec);
        std::filesystem::remove(temp_zip, ec);
    };

    std::wstring write_error;
    if (!WriteEmbeddedResourceToFile(kDefaultConfigZipResourceId, temp_zip, &write_error)) {
        cleanup();
        if (error) {
            *error = write_error;
        }
        return false;
    }

    std::wstring extract_error;
    if (!ExtractZipArchiveToDirectory(temp_zip, storage_.config_root(), &extract_error)) {
        cleanup();
        if (error) {
            *error = extract_error;
        }
        return false;
    }

    cleanup();
    Logger::Info("Setup System restored embedded .config.zip into " + WideToUtf8(storage_.config_root().wstring()));
    return true;
}

void MainWindow::RunSetupSystem() {
    // --- Overwrite confirmation ---
    std::wstring confirm_msg =
        L"Setup System will OVERWRITE your current configuration:\n\n"
        L"- All existing projects and project chat data will be deleted.\n"
        L"- The embedded .config.zip package will be extracted into the app .config folder.\n"
        L"- Any config files inside that package will overwrite the current files.\n\n"
        L"Config files that are not included in .config.zip will be left in place.\n\n"
        L"Then it will install required system tools (Node.js, uv, Poppler,\n"
        L"Tesseract, Pandoc, LibreOffice, OpenSSL, Ollama) in a terminal window.\n\n"
        L"Are you sure you want to overwrite everything and proceed?";

    if (MessageBoxW(hwnd_, confirm_msg.c_str(), L"Setup System \u2014 Overwrite Warning",
                    MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }

    // Several default project templates and tool workflows assume C:\Temp exists.
    // Create it before touching configuration so setup fails cleanly if Windows
    // denies access or a non-directory already occupies that path.
    const std::filesystem::path temp_root = LR"(C:\Temp)";
    std::error_code temp_ec;
    if (std::filesystem::exists(temp_root, temp_ec) &&
        !std::filesystem::is_directory(temp_root, temp_ec)) {
        MessageBoxW(hwnd_,
            L"Setup System needs C:\\Temp, but that path already exists and is not a folder.",
            L"Setup System", MB_OK | MB_ICONERROR);
        return;
    }
    std::filesystem::create_directories(temp_root, temp_ec);
    if (temp_ec) {
        MessageBoxW(hwnd_,
            (L"Setup System could not create C:\\Temp.\n\n" + Utf8ToWide(temp_ec.message())).c_str(),
            L"Setup System", MB_OK | MB_ICONERROR);
        return;
    }
    Logger::Info("Setup System verified C:\\Temp exists.");

    // --- Step 1: Delete all existing projects ---
    const auto existing_projects = storage_.LoadProjects();
    for (const auto& project : existing_projects) {
        storage_.DeleteProject(project.info.id);
    }

    // --- Step 2: Restore config defaults embedded from .config.zip at build time ---
    std::wstring config_error;
    if (!ApplyEmbeddedConfigPackage(&config_error)) {
        MessageBoxW(hwnd_,
            (L"Setup System could not restore the embedded configuration package.\n\n" + config_error).c_str(),
            L"Setup System", MB_OK | MB_ICONERROR);
        return;
    }
    MigrateLegacyWebSettings(storage_.runtime_paths());

    // Reload the project list so the UI reflects the new state
    ReloadProjects("", "");

    const bool has_gpu = DetectDedicatedGpu();

    // --- Step 4: Build combined install script ---
    // All winget and ollama commands run sequentially in one terminal window.
    // Each step is prefixed with an echo so the user can track progress.
    std::wostringstream script;
    script << L"@echo off\r\n"
           << L"setlocal EnableExtensions EnableDelayedExpansion\r\n"
           << L"echo.\r\n"
           << L"echo ============================================================\r\n"
           << L"echo  Agent System Setup\r\n"
           << L"echo ============================================================\r\n"
           << L"echo.\r\n"
           // Node.js / npm / npx
           << L"echo [1/9] Checking Node.js, npm, and npx...\r\n"
           << L"set \"NEED_NODE=0\"\r\n"
           << L"where node >nul 2>nul || set \"NEED_NODE=1\"\r\n"
           << L"where npm >nul 2>nul || set \"NEED_NODE=1\"\r\n"
           << L"where npx >nul 2>nul || set \"NEED_NODE=1\"\r\n"
           << L"if \"%NEED_NODE%\"==\"1\" (\r\n"
           << L"  echo Installing Node.js LTS because node/npm/npx is missing...\r\n"
           << L"  winget install --id OpenJS.NodeJS.LTS -e "
              L"--accept-package-agreements --accept-source-agreements\r\n"
           << L") else (\r\n"
           << L"  echo Node.js, npm, and npx are already available.\r\n"
           << L")\r\n"
           << L"echo.\r\n"
           // uv / uvx
           << L"echo [2/9] Checking uv and uvx...\r\n"
           << L"set \"NEED_UV=0\"\r\n"
           << L"where uv >nul 2>nul || set \"NEED_UV=1\"\r\n"
           << L"where uvx >nul 2>nul || set \"NEED_UV=1\"\r\n"
           << L"if \"%NEED_UV%\"==\"1\" (\r\n"
           << L"  echo Installing uv because uv/uvx is missing...\r\n"
           << L"  winget install --id astral-sh.uv -e "
              L"--accept-package-agreements --accept-source-agreements\r\n"
           << L") else (\r\n"
           << L"  echo uv and uvx are already available.\r\n"
           << L")\r\n"
           << L"echo.\r\n"
           // Poppler
           << L"echo [3/9] Installing Poppler pdftotext (PDF extraction)...\r\n"
           << L"winget install --id oschwartz10612.Poppler -e "
              L"--accept-package-agreements --accept-source-agreements\r\n"
           << L"echo.\r\n"
           // Tesseract
           << L"echo [4/9] Installing Tesseract OCR...\r\n"
           << L"winget install --id tesseract-ocr.tesseract -e "
              L"--accept-package-agreements --accept-source-agreements\r\n"
           << L"echo.\r\n"
           // Pandoc
           << L"echo [5/9] Installing Pandoc...\r\n"
           << L"winget install --id JohnMacFarlane.Pandoc -e "
              L"--accept-package-agreements --accept-source-agreements\r\n"
           << L"echo.\r\n"
           // LibreOffice
           << L"echo [6/9] Installing LibreOffice...\r\n"
           << L"winget install --id TheDocumentFoundation.LibreOffice -e "
              L"--accept-package-agreements --accept-source-agreements\r\n"
           << L"echo.\r\n"
           // OpenSSL runtime used by HTTPS/TLS when the app is built against
           // dynamic OpenSSL import libraries. Install it before Ollama so a
           // post-setup restart can bring HTTPS up cleanly.
           << L"echo [7/9] Checking OpenSSL runtime...\r\n"
           << L"set \"NEED_OPENSSL=0\"\r\n"
           << L"set \"OPENSSL_VERSION=\"\r\n"
           << L"set \"OPENSSL_MAJOR=\"\r\n"
           << L"where openssl >nul 2>nul || set \"NEED_OPENSSL=1\"\r\n"
           << L"if \"%NEED_OPENSSL%\"==\"0\" (\r\n"
           << L"  for /f \"tokens=2\" %%V in ('openssl version 2^>nul') do set \"OPENSSL_VERSION=%%V\"\r\n"
           << L"  for /f \"tokens=1 delims=.\" %%A in (\"!OPENSSL_VERSION!\") do set \"OPENSSL_MAJOR=%%A\"\r\n"
           << L"  if not \"!OPENSSL_MAJOR!\"==\"4\" set \"NEED_OPENSSL=1\"\r\n"
           << L")\r\n"
           << L"if \"%NEED_OPENSSL%\"==\"1\" (\r\n"
           << L"  echo Installing or updating OpenSSL 4.0.0 for Windows...\r\n"
           << L"  winget upgrade --id ShiningLight.OpenSSL.Light -e --version 4.0.0 "
              L"--accept-package-agreements --accept-source-agreements\r\n"
           << L"  if errorlevel 1 (\r\n"
           << L"    echo OpenSSL Light 4.0.0 upgrade failed or was not applicable; trying install...\r\n"
           << L"    winget install --id ShiningLight.OpenSSL.Light -e --version 4.0.0 "
              L"--accept-package-agreements --accept-source-agreements\r\n"
           << L"  )\r\n"
           << L"  if errorlevel 1 (\r\n"
           << L"    echo OpenSSL Light 4.0.0 install failed; trying the current OpenSSL Light package...\r\n"
           << L"    winget install --id ShiningLight.OpenSSL.Light -e "
              L"--accept-package-agreements --accept-source-agreements\r\n"
           << L"  )\r\n"
           << L"  if errorlevel 1 (\r\n"
           << L"    echo OpenSSL Light install failed; trying the full OpenSSL developer package...\r\n"
           << L"    winget install --id ShiningLight.OpenSSL.Dev -e "
              L"--accept-package-agreements --accept-source-agreements\r\n"
           << L"  )\r\n"
           << L") else (\r\n"
           << L"  openssl version\r\n"
           << L")\r\n"
           << L"echo.\r\n"
           // Ollama
           << L"echo [8/9] Installing Ollama (local AI runtime)...\r\n"
           << L"winget install --id Ollama.Ollama -e "
              L"--accept-package-agreements --accept-source-agreements\r\n"
           << L"echo.\r\n"
           // Pull local models (Ollama must be running after install)
           << L"echo [9/9] Pulling required Ollama models...\r\n"
           << L"echo (Ollama may need a moment to start after installation.)\r\n"
           << L"timeout /t 8 /nobreak >nul\r\n"
           << L"for %%M in (qwen2.5vl:7b nomic-embed-text moondream:1.8b qwen3-embedding:0.6b qwen3-embedding:latest) do (\r\n"
           << L"  echo Pulling %%M...\r\n"
           << L"  ollama pull %%M\r\n"
           << L"  if errorlevel 1 echo WARNING: Failed to pull %%M. You can retry manually later.\r\n"
           << L")\r\n"
           << L"echo.\r\n";

    script << L"echo ============================================================\r\n"
           << L"echo  Setup complete.\r\n"
           << L"echo  You can now create a RAG library with Ollama as the\r\n"
           << L"echo  embedding provider and nomic-embed-text as the model.\r\n";
    if (has_gpu) {
        script
           << L"echo  Select 'vision_language_gpu' image ingest mode in the RAG\r\n"
           << L"echo  library settings to use local vision models.\r\n";
    }
    script << L"echo ============================================================\r\n"
           << L"echo.\r\n"
           << L"pause\r\n";

    // Write the script as a narrow-character .bat file (pure ASCII content).
    wchar_t temp_dir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp_dir);
    const std::wstring script_path = std::wstring(temp_dir) + L"agent_setup.bat";
    {
        const std::wstring wscript = script.str();
        const int narrow_len = WideCharToMultiByte(CP_ACP, 0, wscript.c_str(),
            static_cast<int>(wscript.size()), nullptr, 0, nullptr, nullptr);
        std::string narrow_script(static_cast<size_t>(narrow_len), '\0');
        WideCharToMultiByte(CP_ACP, 0, wscript.c_str(), static_cast<int>(wscript.size()),
            narrow_script.data(), narrow_len, nullptr, nullptr);
        std::ofstream f(script_path, std::ios::binary | std::ios::trunc);
        if (f.is_open()) {
            f.write(narrow_script.data(), static_cast<std::streamsize>(narrow_script.size()));
        }
    }

    const std::wstring cmd_args = L"/c \"" + script_path + L"\"";
    SHELLEXECUTEINFOW setup_exec{};
    setup_exec.cbSize = sizeof(setup_exec);
    setup_exec.fMask = SEE_MASK_NOCLOSEPROCESS;
    setup_exec.hwnd = hwnd_;
    setup_exec.lpVerb = L"open";
    setup_exec.lpFile = L"cmd.exe";
    setup_exec.lpParameters = cmd_args.c_str();
    setup_exec.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&setup_exec) || !setup_exec.hProcess) {
        MessageBoxW(hwnd_,
            L"Failed to open the setup terminal window.\n"
            L"Please run the following commands manually in a terminal:\n\n"
            L"  winget install --id OpenJS.NodeJS.LTS -e\n"
            L"  winget install --id astral-sh.uv -e\n"
            L"  winget install --id oschwartz10612.Poppler -e\n"
            L"  winget install --id tesseract-ocr.tesseract -e\n"
            L"  winget upgrade --id ShiningLight.OpenSSL.Light -e --version 4.0.0\n"
            L"  winget install --id ShiningLight.OpenSSL.Light -e --version 4.0.0\n"
            L"  winget install --id ShiningLight.OpenSSL.Dev -e\n"
            L"  winget install --id Ollama.Ollama -e\n"
            L"  ollama pull qwen2.5vl:7b\n"
            L"  ollama pull nomic-embed-text\n"
            L"  ollama pull moondream:1.8b\n"
            L"  ollama pull qwen3-embedding:0.6b\n"
            L"  ollama pull qwen3-embedding:latest",
            L"Setup System", MB_OK | MB_ICONWARNING);
        return;
    }

    HANDLE setup_process = setup_exec.hProcess;
    HWND setup_hwnd = hwnd_;
    std::thread([setup_process, setup_hwnd]() {
        WaitForSingleObject(setup_process, INFINITE);
        CloseHandle(setup_process);
        PostMessageW(setup_hwnd, kSetupSystemFinishedMessage, 0, 0);
    }).detach();

    std::wstring done_msg =
        L"Setup terminal launched.\n\n"
        L"Follow the progress in the terminal window.\n"
        L"\nThe embedded .config.zip package has been restored into the app .config folder.\n"
        L"The app will offer to restart after the setup terminal closes.\n";

    MessageBoxW(hwnd_, done_msg.c_str(), L"Setup System", MB_OK | MB_ICONINFORMATION);
    UpdateStatus(L"System setup launched. Press any key in the setup terminal when complete to restart the app.");
}

void MainWindow::OnSetupSystemFinished() {
    const int restart = MessageBoxW(
        hwnd_,
        L"Setup System has finished.\n\n"
        L"Restarting the application is required before the restored configuration and command runtime changes are reliably visible.\n\n"
        L"Restart the application now?",
        L"Setup System Complete",
        MB_YESNO | MB_ICONQUESTION);
    if (restart == IDYES) {
        RestartApplication();
    } else {
        UpdateStatus(L"System setup completed. Restart the app to load the restored configuration.");
    }
}

void MainWindow::RestartApplication() {
    wchar_t module_path[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, module_path, static_cast<DWORD>(std::size(module_path)))) {
        MessageBoxW(hwnd_, L"Could not locate the application executable to restart.", L"Restart Application", MB_OK | MB_ICONERROR);
        return;
    }

    const std::filesystem::path exe_path(module_path);
    const std::wstring working_dir = exe_path.parent_path().wstring();
    const HINSTANCE launched = ShellExecuteW(hwnd_, L"open", module_path, nullptr, working_dir.c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<intptr_t>(launched) <= 32) {
        MessageBoxW(hwnd_, L"Could not launch a new application instance. Please close and reopen the app manually.", L"Restart Application", MB_OK | MB_ICONWARNING);
        return;
    }
    DestroyWindow(hwnd_);
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
    RenderTranscript();
    RenderToolTrace();
}

void MainWindow::RefreshTree() {
    refreshing_tree_ = true;
    TreeView_DeleteAllItems(tree_);
    tree_items_.clear();
    chat_tree_items_.clear();

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
            // Append a streaming indicator if this chat is currently in flight.
            std::wstring chat_name = Utf8ToWide(chat.name);
            if (IsChatInFlight(chat.id)) {
                chat_name += L" \u25cf";  // â— U+25CF BLACK CIRCLE
            }
            child.item.pszText = chat_name.data();
            child.item.lParam = reinterpret_cast<LPARAM>(chat_data.get());
            HTREEITEM chat_item = TreeView_InsertItem(tree_, &child);
            chat_tree_items_[chat.id] = chat_item;
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
    active_messages_skipped_for_size_ = false;
    active_messages_file_size_ = 0;
    if (active_project_id_.empty() || active_chat_id_.empty()) {
        active_messages_.clear();
        return;
    }
    const auto messages_path = storage_.data_root() / "projects" / active_project_id_ /
        "chats" / active_chat_id_ / "messages.json";
    std::error_code ec;
    if (std::filesystem::exists(messages_path, ec)) {
        const auto size = std::filesystem::file_size(messages_path, ec);
        if (!ec) {
            active_messages_file_size_ = size;
            if (size > kDesktopTranscriptLoadLimitBytes) {
                active_messages_.clear();
                active_messages_skipped_for_size_ = true;
                Logger::Warn("Desktop transcript skipped large chat history: " +
                    messages_path.string() + " (" + std::to_string(size) + " bytes)");
                return;
            }
        }
    }
    active_messages_ = storage_.LoadMessages(active_project_id_, active_chat_id_);
}

std::string MainWindow::BuildMcpProjectContext() const {
    if (active_project_id_.empty()) {
        return {};
    }

    const auto bindings = mcp_manager_.GetProjectBindings(active_project_id_);
    std::vector<ProjectMcpVariableValue> values;
    const auto project_settings = storage_.LoadProjectSettings(active_project_id_);
    for (const auto& binding : bindings) {
        for (const auto& variable : binding.variables) {
            const std::string name = Trim(variable.name);
            if (!name.empty()) {
                variable_resolver::UpsertValue(values, name, variable.value);
            }
        }
    }
    for (const auto& variable : project_settings.project_variables) {
        const std::string name = Trim(variable.name);
        if (!name.empty()) {
            variable_resolver::UpsertValue(values, variable);
        }
    }
    for (const auto& variable : BuildRuntimeVariablesForChat(active_project_id_, active_chat_id_)) {
        variable_resolver::UpsertValue(values, variable.name, variable.value);
    }
    values = variable_resolver::ResolveValues(values);

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
        const std::string emitted_key = variable_resolver::ToLookupKey(variable.name);
        if (std::find(emitted_names.begin(), emitted_names.end(), emitted_key) != emitted_names.end()) {
            return;
        }

        const auto value_it = variable_resolver::FindValue(values, variable.name);

        ContextVariable context_variable;
        context_variable.name = variable.name;
        context_variable.value = value_it.value_or("");
        context_variable.description = Trim(variable.description);
        context_values.push_back(std::move(context_variable));
        emitted_names.push_back(emitted_key);
    };
    const auto add_project_variable = [&](const ProjectMcpVariableValue& variable) {
        if (!variable.inject_into_context || variable.name.empty()) {
            return;
        }
        const std::string emitted_key = variable_resolver::ToLookupKey(variable.name);
        if (std::find(emitted_names.begin(), emitted_names.end(), emitted_key) != emitted_names.end()) {
            return;
        }

        const auto value_it = variable_resolver::FindValue(values, variable.name);
        if (!value_it || Trim(*value_it).empty()) {
            return;
        }

        ContextVariable context_variable;
        context_variable.name = variable.name;
        context_variable.value = *value_it;
        context_variable.description = Trim(variable.description);
        context_values.push_back(std::move(context_variable));
        emitted_names.push_back(emitted_key);
    };

    for (const auto& variable : mcp_manager_.global_variables()) {
        add_variable(variable);
    }

    for (const auto& variable : project_settings.project_variables) {
        add_project_variable(variable);
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

static McpToolCallResult RunDesktopQuestionnaire(const std::string& arguments_json) {
    nlohmann::json args;
    try {
        args = nlohmann::json::parse(arguments_json.empty() ? "{}" : arguments_json);
    } catch (...) {
        return built_in_tools::ErrorResult("Invalid questionnaire arguments: not valid JSON.");
    }
    const std::string question = Trim(args.value("question", ""));
    const auto opts_j = args.value("options", nlohmann::json::array());
    const bool allow_multiple = args.value("allow_multiple", false);
    if (question.empty() || !opts_j.is_array() || opts_j.empty()) {
        return built_in_tools::ErrorResult("Questionnaire requires a non-empty question and at least one option.");
    }
    QuestionnaireOptions dlg;
    dlg.question = Utf8ToWide(question);
    for (const auto& item : opts_j) {
        if (item.is_string()) dlg.labels.push_back(Utf8ToWide(item.get<std::string>()));
    }
    dlg.allow_multiple = allow_multiple;
    const auto selected = ShowQuestionnaireDialog(nullptr, dlg);
    if (!selected || selected->empty()) {
        return built_in_tools::ErrorResult("User cancelled the questionnaire.");
    }
    nlohmann::json payload = {
        {"success", true},
        {"tool", "user_questionnaire"},
        {"question", question},
        {"selected_indices", nlohmann::json::array()},
        {"selected_labels", nlohmann::json::array()},
    };
    for (int index : *selected) {
        payload["selected_indices"].push_back(index);
        if (index >= 0 && static_cast<size_t>(index) < dlg.labels.size()) {
            payload["selected_labels"].push_back(WideToUtf8(dlg.labels[index]));
        }
    }
    McpToolCallResult result;
    result.success = true;
    result.raw_result_json = payload.dump(2);
    result.content_text = result.raw_result_json;
    return result;
}

void MainWindow::SendCurrentMessage() {
    if (IsChatInFlight(active_chat_id_)) {
        return;
    }

    constexpr int kMaxConcurrentChats = 8;
    if (CountChatsInFlight() >= kMaxConcurrentChats) {
        MessageBoxW(hwnd_,
            L"The maximum number of concurrent chats are already in flight.\r\n\r\nWait for one to finish before sending another message.",
            L"Too Many Active Chats", MB_OK | MB_ICONINFORMATION);
        return;
    }

    ChatInfo* chat = FindChat(active_project_id_, active_chat_id_);
    if (!chat) {
        MessageBoxW(hwnd_, L"Select a chat first.", L"No Chat Selected", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // Resolve provider + model from project settings, falling back to first available
    auto send_proj_settings = storage_.LoadProjectSettings(active_project_id_);
    const std::string& pref_provider = send_proj_settings.preferred_provider_id;
    const std::string& pref_model    = send_proj_settings.preferred_model_id;

    const ProviderConfig* selected_provider = nullptr;
    const ModelConfig*    selected_model    = nullptr;
    for (const auto& p : providers_) {
        for (const auto& m : p.models) {
            if ((pref_provider.empty() || p.id == pref_provider) &&
                (pref_model.empty()    || m.id == pref_model)) {
                selected_provider = &p;
                selected_model    = &m;
                break;
            }
        }
        if (selected_provider) break;
    }
    // Ultimate fallback: first model of first provider
    if (!selected_provider && !providers_.empty() && !providers_.front().models.empty()) {
        selected_provider = &providers_.front();
        selected_model    = &providers_.front().models.front();
    }
    if (!selected_provider || !selected_model) {
        MessageBoxW(hwnd_, L"Configure at least one provider and model first.\r\n\r\nUse Providers to add a model, then set the preferred model in Project Settings.", L"No Model Configured", MB_OK | MB_ICONINFORMATION);
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

    ChatRequestOptions request;
    request.provider = *selected_provider;
    request.model    = *selected_model;
    request.system_prompt = chat->system_prompt;
    request.temperature = chat->temperature;
    request.max_tokens = chat->max_tokens;
    request.model_timeout_seconds = send_proj_settings.model_timeout_seconds;

    std::vector<MessageRecord> full_messages = active_messages_;
    full_messages.push_back(user_message);

    std::vector<MessageRecord> request_history = ModelVisibleMessages(active_messages_);
    std::string compressed_context;
    auto proj_settings = storage_.LoadProjectSettings(active_project_id_);
    const auto runtime_variables = BuildRuntimeVariablesForChat(active_project_id_, active_chat_id_);
    const auto resolved_prompt_variables = BuildResolvedVariablesForChat(
        active_project_id_, active_chat_id_, proj_settings);
    std::optional<ContextCompressionConfig> selected_compression_config;
    if (!proj_settings.selected_compression_config_id.empty()) {
        selected_compression_config = compression_service_.GetGlobalConfig(proj_settings.selected_compression_config_id);
        if (selected_compression_config) {
            const auto compression_messages = ModelVisibleMessages(active_messages_);
            if (compression_service_.ShouldCompress(active_project_id_, active_chat_id_, compression_messages.size())) {
                auto model_caller = [&](const ChatRequestOptions& opts) -> std::optional<ChatCompletionResult> {
                    auto result = OpenAIClient::CreateSimpleCompletion(opts);
                    return result.success ? std::make_optional(result) : std::nullopt;
                };
                compressed_context = compression_service_.CompressConversation(
                    compression_messages,
                    active_project_id_,
                    active_chat_id_,
                    proj_settings.selected_compression_config_id,
                    model_caller,
                    false,
                    "automatic",
                    resolved_prompt_variables);
            }

            auto compression_state = compression_service_.LoadChatState(active_project_id_, active_chat_id_);
            if (compressed_context.empty()) {
                compressed_context = compression_state.current_compressed_context;
                bool needs_legacy_repair = compressed_context.size() >= 40000;
                if (needs_legacy_repair) {
                    needs_legacy_repair = false;
                    for (const auto& pinned : compression_state.layer1_pinned_messages) {
                        if (pinned.role == "tool") {
                            needs_legacy_repair = true;
                            break;
                        }
                    }
                }
                if (needs_legacy_repair) {
                    const std::string repaired_context =
                        compression_service_.RebuildCompressedContextFromExistingState(
                            compression_messages,
                            active_project_id_,
                            active_chat_id_,
                            proj_settings.selected_compression_config_id);
                    if (!repaired_context.empty()) {
                        compressed_context = repaired_context;
                        compression_state =
                            compression_service_.LoadChatState(active_project_id_, active_chat_id_);
                        Logger::Info("Compression",
                            "repaired legacy compressed context project=" + active_project_id_ +
                            " chat=" + active_chat_id_ +
                            " chars=" + std::to_string(repaired_context.size()));
                    }
                }
            }
            if (!compressed_context.empty() && compression_state.last_compression_message_index > 0) {
                const size_t first_uncompressed = std::min(compression_state.last_compression_message_index, request_history.size());
                request_history.erase(request_history.begin(), request_history.begin() + first_uncompressed);
                request_history = message_sanitizer::SanitizeModelVisibleMessages(request_history);
            }
        }
    }

    request.messages = std::move(request_history);
    request.messages.push_back(user_message);
    std::vector<std::pair<std::string, std::string>> system_prompt_sections;
    if (!compressed_context.empty()) {
        system_prompt_sections.push_back({"Compressed Context", compressed_context});
    }
    if (!Trim(chat->system_prompt).empty()) {
        system_prompt_sections.push_back({"Base Chat System Prompt", chat->system_prompt});
    }
    if (!compressed_context.empty()) {
        if (!request.system_prompt.empty()) {
            request.system_prompt = compressed_context + "\n\n" + request.system_prompt;
        } else {
            request.system_prompt = compressed_context;
        }
    }

    {
        std::string project_instructions = Trim(proj_settings.project_instructions);
        if (!project_instructions.empty()) {
            project_instructions =
                variable_resolver::ExpandTemplate(project_instructions, resolved_prompt_variables);
            if (!request.system_prompt.empty()) {
                request.system_prompt += "\n\n";
            }
            request.system_prompt += "Project Instructions:\n";
            request.system_prompt += project_instructions;
            system_prompt_sections.push_back({"Project Instructions", project_instructions});
        }
        // Inject selected agentic mode instructions after project instructions
        if (!proj_settings.selected_agentic_mode_id.empty()) {
            const auto all_modes = storage_.LoadAgenticModes();
            const auto it = std::find_if(all_modes.begin(), all_modes.end(),
                [&](const AgenticModeConfig& m) { return m.id == proj_settings.selected_agentic_mode_id; });
            if (it != all_modes.end()) {
                std::string mode_instructions = Trim(NormalizeNewlinesToLf(it->instructions));
                if (!mode_instructions.empty()) {
                    if (!request.system_prompt.empty()) {
                        request.system_prompt += "\n\n";
                    }
                    request.system_prompt += "Agentic Mode Instructions (" + it->name + "):\n";
                    request.system_prompt += mode_instructions;
                    system_prompt_sections.push_back({"Agentic Mode: " + it->name, mode_instructions});
                }
            }
        }
        // Inject built-in tool system prompts (not part of history / never compressed)
        if (proj_settings.built_in_powershell_enabled) {
            const std::string ps_context = built_in_tools::PowerShellSystemPrompt();
            if (!request.system_prompt.empty()) {
                request.system_prompt += "\n\n";
            }
            request.system_prompt += ps_context;
            system_prompt_sections.push_back({"PowerShell Execution", ps_context});
        }
        if (proj_settings.built_in_planner_enabled) {
            const std::string planner_context = built_in_tools::PlannerSystemPrompt();
            if (!request.system_prompt.empty()) {
                request.system_prompt += "\n\n";
            }
            request.system_prompt += planner_context;
            system_prompt_sections.push_back({"Planner / Task Decomposition", planner_context});
        }
        if (built_in_tools::IsCompletionDriverEnabled(proj_settings, proj_settings.selected_agentic_mode_id)) {
            const std::string driver_context = built_in_tools::CompletionDriverSystemPrompt(
                built_in_tools::NormalizedCompletionDriverMaxContinuations(proj_settings));
            if (!request.system_prompt.empty()) {
                request.system_prompt += "\n\n";
            }
            request.system_prompt += driver_context;
            system_prompt_sections.push_back({"Completion Driver", driver_context});
        }
        if (built_in_tools::IsQuestionnaireEnabled(proj_settings, proj_settings.selected_agentic_mode_id)) {
            const std::string q_context = built_in_tools::QuestionnaireSystemPrompt();
            if (!request.system_prompt.empty()) {
                request.system_prompt += "\n\n";
            }
            request.system_prompt += q_context;
            system_prompt_sections.push_back({"User Questionnaire", q_context});
        }
        if (proj_settings.built_in_filesystem_enabled) {
            const std::string fs_context = built_in_tools::FilesystemSystemPrompt();
            if (!request.system_prompt.empty()) {
                request.system_prompt += "\n\n";
            }
            request.system_prompt += fs_context;
            system_prompt_sections.push_back({"Project Filesystem", fs_context});
        }
    }

    const std::string mcp_project_context = BuildMcpProjectContext();
    if (!mcp_project_context.empty()) {
        if (!request.system_prompt.empty()) {
            request.system_prompt += "\n\n";
        }
        request.system_prompt += mcp_project_context;
        system_prompt_sections.push_back({"MCP Project Context", mcp_project_context});
    }

    const std::string rag_context = rag_tools::BuildRagProjectContext(&rag_service_, active_project_id_);
    if (!rag_context.empty()) {
        if (!request.system_prompt.empty()) {
            request.system_prompt += "\n\n";
        }
        request.system_prompt += rag_context;
        system_prompt_sections.push_back({"RAG Project Context", rag_context});
    }

    if (artifact_memory_tools::ShouldExposeArtifactMemoryTools(
            selected_compression_config,
            proj_settings.built_in_artifact_memory_enabled)) {
        const std::string artifact_context = artifact_memory_tools::BuildArtifactMemoryUsageContext();
        if (!request.system_prompt.empty()) {
            request.system_prompt += "\n\n";
        }
        request.system_prompt += artifact_context;
        system_prompt_sections.push_back({"Artifact Memory Restore Rules", artifact_context});
    }

    // Lazy-load per-chat working set from disk if not already in memory.
    auto ws_it = rag_working_sets_by_chat_.find(active_chat_id_);
    if (ws_it == rag_working_sets_by_chat_.end()) {
        rag_working_sets_by_chat_[active_chat_id_] = storage_.LoadChatRagWorkingSet(active_project_id_, active_chat_id_);
        ws_it = rag_working_sets_by_chat_.find(active_chat_id_);
    }
    const std::vector<RagWorkingSetEntry>& current_working_set = ws_it->second;

    // Compute the token budget for active RAG search results retained in the working set.
    // Passive RAG context injection is intentionally inactive in this phase.
    const int context_window = request.model.context_window;
    const int rag_working_budget  = context_window > 0 ? context_window * 10 / 100 : 0;

    // Include the working set from prior turns first (closest to the user message)
    const std::string working_set_context = BuildWorkingSetContextBlock(
        current_working_set, static_cast<size_t>(rag_working_budget));
    if (!working_set_context.empty()) {
        if (!request.system_prompt.empty()) {
            request.system_prompt += "\n\n";
        }
        request.system_prompt += working_set_context;
        system_prompt_sections.push_back({"RAG Working Set Context", working_set_context});
    }

    if (selected_compression_config) {
        // Emergency trigger: if the assembled request exceeds the configured threshold,
        // compress NOW (before the model call), not on the next turn.
        size_t estimated = EstimateRequestInputTokens(request, {}, false);
        size_t ctx_win = static_cast<size_t>(request.model.context_window);
        int trigger_percent = selected_compression_config->context_window_trigger_percent;
        if (trigger_percent > 0 && ctx_win > 0 && estimated > (ctx_win * static_cast<size_t>(trigger_percent) / 100)) {
            compression_service_.MarkCompressionScheduled(active_project_id_, active_chat_id_);
            const auto compression_messages = ModelVisibleMessages(active_messages_);
            auto model_caller = [&](const ChatRequestOptions& opts) -> std::optional<ChatCompletionResult> {
                auto result = OpenAIClient::CreateSimpleCompletion(opts);
                return result.success ? std::make_optional(result) : std::nullopt;
            };
            // Preserve old compressed context before CompressConversation updates the state.
            const std::string old_comp = compression_service_.LoadChatState(active_project_id_, active_chat_id_).current_compressed_context;
            const std::string fresh_compressed = compression_service_.CompressConversation(
                compression_messages,
                active_project_id_,
                active_chat_id_,
                proj_settings.selected_compression_config_id,
                model_caller,
                false,
                "emergency_threshold",
                resolved_prompt_variables);
            if (!fresh_compressed.empty()) {
                compressed_context = fresh_compressed;
                auto compression_state = compression_service_.LoadChatState(active_project_id_, active_chat_id_);
                request_history = ModelVisibleMessages(active_messages_);
                if (compression_state.last_compression_message_index > 0) {
                    const size_t first_uncompressed = std::min(compression_state.last_compression_message_index, request_history.size());
                    request_history.erase(request_history.begin(), request_history.begin() + first_uncompressed);
                    request_history = message_sanitizer::SanitizeModelVisibleMessages(request_history);
                }
                request.messages = std::move(request_history);
                request.messages.push_back(user_message);
                // Rebuild system prompt with the new compressed context.
                // request.system_prompt currently contains the FULL old system prompt including old compressed context.
                // We replace the old compressed part with the new one.
                if (!old_comp.empty() && !request.system_prompt.empty()) {
                    size_t pos = request.system_prompt.find(old_comp);
                    if (pos != std::string::npos) {
                        request.system_prompt.replace(pos, old_comp.size(), compressed_context);
                    } else {
                        // Old compressed context not found (edge case); prepend new one
                        request.system_prompt = compressed_context + "\n\n" + request.system_prompt;
                    }
                } else {
                    if (!request.system_prompt.empty()) {
                        request.system_prompt = compressed_context + "\n\n" + request.system_prompt;
                    } else {
                        request.system_prompt = compressed_context;
                    }
                }
            }
        }
    }

    const std::string project_id = active_project_id_;
    const std::string chat_id = active_chat_id_;

    // ── Chat logging setup (Desktop) ──────────────────────────────────
    ChatRequestLogger::MaybeInitialize(
        storage_.runtime_paths().data_root, project_id, proj_settings.enable_chat_logging);
    const bool logging = proj_settings.enable_chat_logging;
    const std::string ts = CurrentTimestampUtc();
    std::string log_header;
    if (logging) {
        log_header = ChatRequestLogger::FormatHeader(ts, project_id, chat_id);
        log_header += ChatRequestLogger::FormatProvider(*selected_provider, *selected_model);
        log_header += ChatRequestLogger::FormatBlock("System Prompt (Full)", request.system_prompt);
        log_header += ChatRequestLogger::FormatSections("System Prompt", system_prompt_sections);
        log_header += ChatRequestLogger::FormatBlock("Request Messages", ChatRequestLogger::FormatMessages(request.messages));
    }
    // ── End logging setup ───────────────────────────────────────────

    for (const auto& config : mcp_manager_.configs()) {
        if (config.enabled && config.auto_connect &&
            mcp_manager_.IsServerSelectedForProject(project_id, config.id)) {
            std::string ignored;
            mcp_manager_.ConnectServer(config.id, project_id, &ignored, runtime_variables);
        }
    }
    const auto exposed_tools =
        mcp_manager_.GetExposedToolsForProject(project_id, runtime_variables);
    const auto rag_tool_set = rag_tools::BuildRagToolSet(&rag_service_, project_id);
    const auto rag_tool_definitions = rag_tool_set.definitions;
    const auto rag_tool_routes = rag_tool_set.routes;
    const auto rag_exposed_tools = rag_tool_set.exposed_tools;
    const auto all_model_tools = storage_.LoadModelTools();
    // Filter model tools to only those enabled for this project.
    const auto project_settings_for_tools = storage_.LoadProjectSettings(project_id);
    const auto& enabled_tool_ids = project_settings_for_tools.model_tool_ids;
    const auto project_variables = resolved_prompt_variables;
    const auto artifact_tool_set = request.model.supports_tools
        ? artifact_memory_tools::BuildArtifactMemoryToolSet(
            selected_compression_config,
            project_id,
            chat_id,
            project_variables,
            project_settings_for_tools.built_in_artifact_memory_enabled)
        : artifact_memory_tools::ArtifactMemoryToolSet{};
    const auto artifact_tool_definitions = artifact_tool_set.definitions;
    std::vector<ModelToolConfig> model_tools;
    for (const auto& mt : all_model_tools) {
        // If the project has an explicit enabled list, only include listed tools.
        // An empty list means "legacy project" â€” include all tools until the user
        // explicitly saves Project Settings with the new checklist.
        if (!enabled_tool_ids.empty() &&
            std::find(enabled_tool_ids.begin(), enabled_tool_ids.end(), mt.id) == enabled_tool_ids.end()) {
            continue;
        }
        model_tools.push_back(mt);
    }

    // Build model tool definitions (agent_<name> tools)
    std::vector<ChatToolDefinition> model_tool_definitions;
    for (const auto& mt : model_tools) {
        if (mt.name.empty()) continue;
        ChatToolDefinition def;
        def.name = "agent_" + SanitizeToolName(mt.name);

        // Collect MCP server names for this tool's bindings
        std::vector<std::string> mcp_server_names;
        for (const auto& b : mt.mcp_bindings) {
            for (const auto& et : exposed_tools) {
                if (et.server_id == b.server_id) {
                    if (std::find(mcp_server_names.begin(), mcp_server_names.end(), et.server_name) == mcp_server_names.end()) {
                        mcp_server_names.push_back(et.server_name);
                    }
                    break;
                }
            }
        }

        // Collect RAG library names that are available as active tools.
        std::vector<std::string> rag_names;
        for (const auto& rb : mt.rag_bindings) {
            if (rb.enabled && rb.can_read && rb.expose_as_tool &&
                rb.retrieval_mode != RagRetrievalMode::PassiveOnly &&
                rb.retrieval_mode != RagRetrievalMode::Disabled) {
                if (const auto lib = rag_service_.GetLibrary(rb.rag_id)) {
                    rag_names.push_back(lib->name);
                }
            }
        }

        // Build enriched description
        std::string desc = mt.description.empty() ? ("Sub-agent: " + mt.name) : mt.description;
        if (!mcp_server_names.empty() || !rag_names.empty()) {
            desc += " This agent has access to:";
            if (!mcp_server_names.empty()) {
                desc += " MCP tools from servers: ";
                for (size_t i = 0; i < mcp_server_names.size(); ++i) {
                    if (i) desc += ", ";
                    desc += mcp_server_names[i];
                }
                desc += ".";
            }
            if (!rag_names.empty()) {
                desc += " RAG MCP servers: ";
                for (size_t i = 0; i < rag_names.size(); ++i) {
                    if (i) desc += ", ";
                    desc += rag_names[i];
                }
                desc += ".";
            }
        }
        def.description = std::move(desc);

        // Build richer instructions parameter description listing agent capabilities
        std::string instr_desc = "Detailed task instructions for this agent.";
        if (!mcp_server_names.empty() || !rag_names.empty()) {
            instr_desc += " The agent can access the following capabilities:";
            for (const auto& sn : mcp_server_names) {
                instr_desc += " [MCP server: " + sn + "]";
            }
            for (const auto& rag_name : rag_names) {
                instr_desc += " [RAG MCP server: " + rag_name + "]";
            }
            instr_desc += ". Provide sufficient context for the agent to complete the task using these resources.";
        }

        nlohmann::json params = {
            {"type", "object"},
            {"properties", {
                {"instructions", {
                    {"type", "string"},
                    {"description", instr_desc}
                }}
            }},
            {"required", nlohmann::json::array({"instructions"})}
        };
        def.parameters_json = params.dump();

        model_tool_definitions.push_back(std::move(def));
    }
    const auto built_in_tool_definitions = built_in_tools::BuildDefinitions(
        project_settings_for_tools, proj_settings.selected_agentic_mode_id);
    std::vector<ChatToolDefinition> extra_tool_definitions = rag_tool_definitions;
    extra_tool_definitions.insert(extra_tool_definitions.end(), artifact_tool_definitions.begin(), artifact_tool_definitions.end());
    extra_tool_definitions.insert(extra_tool_definitions.end(), model_tool_definitions.begin(), model_tool_definitions.end());
    extra_tool_definitions.insert(extra_tool_definitions.end(), built_in_tool_definitions.begin(), built_in_tool_definitions.end());

    const bool include_tools = request.model.supports_tools &&
        (!exposed_tools.empty() || !extra_tool_definitions.empty());

    if (!CheckContextWindow(hwnd_, request, exposed_tools, extra_tool_definitions, include_tools)) {
        return;
    }

    active_messages_ = full_messages;
    storage_.SaveMessages(active_project_id_, active_chat_id_, active_messages_);

    // Build working set diagnostic JSON for the debug entry
    nlohmann::json ws_diag = nlohmann::json::array();
    for (const auto& ws_entry : current_working_set) {
        ws_diag.push_back({
            {"chunk_id", ws_entry.chunk_id},
            {"rag_id", ws_entry.rag_id},
            {"rag_name", ws_entry.rag_name},
            {"document_id", ws_entry.document_id},
            {"document_title", ws_entry.document_title},
            {"score", ws_entry.score},
            {"query", ws_entry.query},
            {"retrieved_at", ws_entry.retrieved_at},
        });
    }

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
    context_debug_entry.rag_working_set_json = ws_diag.dump(2);
    storage_.AppendChatContextDebugEntry(project_id, chat_id, context_debug_entry);

    SetWindowTextW(input_, L"");
    streaming_previews_by_chat_[active_chat_id_].clear();
    SetChatBusy(active_chat_id_, true);
    RenderTranscript();
    UpdateStatus(L"Sending request...");

    const size_t existing_count = request.messages.size();

    if (!include_tools) {
        std::thread([hwnd = hwnd_, request, project_id, chat_id, log_header, logging]() {
            try {
                const auto result = OpenAIClient::StreamChat(request, [hwnd, project_id, chat_id](const std::string& piece) {
                    auto* payload = new ChatDeltaPayload;
                    payload->project_id = project_id;
                    payload->chat_id = chat_id;
                    payload->text = piece;
                    PostMessageW(hwnd, kChatDeltaMessage, 0, reinterpret_cast<LPARAM>(payload));
                });

                auto* final_payload = new ChatFinishedPayload;
                final_payload->success = result.success;
                final_payload->project_id = project_id;
                final_payload->chat_id = chat_id;
                final_payload->error = result.error;
                if (logging) {
                    if (!result.success) {
                        ChatRequestLogger::Log(project_id, logging,
                            log_header + ChatRequestLogger::FormatErrorResponse(result.error));
                    } else {
                        ChatRequestLogger::Log(project_id, logging,
                            log_header + ChatRequestLogger::FormatSuccessResponse(result.full_text));
                    }
                }
                if (result.success && !result.full_text.empty()) {
                    MessageRecord assistant_message;
                    assistant_message.role = "assistant";
                    assistant_message.content = result.full_text;
                    assistant_message.created_at = CurrentTimestampUtc();
                    final_payload->appended_messages.push_back(std::move(assistant_message));
                }
                PostMessageW(hwnd, kChatFinishedMessage, 0, reinterpret_cast<LPARAM>(final_payload));
            } catch (const std::exception& ex) {
                Logger::Error(std::string("Chat thread exception: ") + ex.what());
                auto* final_payload = new ChatFinishedPayload;
                final_payload->success = false;
                final_payload->project_id = project_id;
                final_payload->chat_id = chat_id;
                final_payload->error = std::string("Unhandled error in chat thread: ") + ex.what();
                {
                    MessageRecord err_msg;
                    err_msg.role = "assistant";
                    err_msg.content = std::string("[System Error] Chat thread crashed: ") + ex.what();
                    err_msg.created_at = CurrentTimestampUtc();
                    final_payload->appended_messages.push_back(std::move(err_msg));
                }
                PostMessageW(hwnd, kChatFinishedMessage, 0, reinterpret_cast<LPARAM>(final_payload));
            } catch (...) {
                Logger::Error("Chat thread exception: unknown error");
                auto* final_payload = new ChatFinishedPayload;
                final_payload->success = false;
                final_payload->project_id = project_id;
                final_payload->chat_id = chat_id;
                final_payload->error = "Unhandled unknown error in chat thread.";
                {
                    MessageRecord err_msg;
                    err_msg.role = "assistant";
                    err_msg.content = "[System Error] Chat thread crashed with an unknown error.";
                    err_msg.created_at = CurrentTimestampUtc();
                    final_payload->appended_messages.push_back(std::move(err_msg));
                }
                PostMessageW(hwnd, kChatFinishedMessage, 0, reinterpret_cast<LPARAM>(final_payload));
            }
        }).detach();
        return;
    }

    std::thread([hwnd = hwnd_, request, project_id, chat_id, log_header, logging, existing_count, exposed_tools, rag_exposed_tools, rag_tool_definitions, rag_tool_routes, artifact_tool_definitions, artifact_runtime = artifact_tool_set.runtime, model_tool_definitions, model_tools, built_in_tool_definitions, project_settings_for_tools, proj_settings, project_variables, runtime_variables, mcp_manager = &mcp_manager_, rag_service = &rag_service_, providers = providers_, selected_compression_config, compression_service = &compression_service_]() {
        try {
        auto working_set_additions = std::make_shared<std::vector<RagWorkingSetEntry>>();

        std::vector<ChatToolDefinition> tool_definitions;
        tool_definitions.reserve(exposed_tools.size() + rag_tool_definitions.size() + artifact_tool_definitions.size() + model_tool_definitions.size() + built_in_tool_definitions.size());
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
        for (const auto& exposed : rag_exposed_tools) {
            tool_lookup[exposed.alias] = exposed;
        }
        tool_definitions.insert(tool_definitions.end(), rag_tool_definitions.begin(), rag_tool_definitions.end());
        tool_definitions.insert(tool_definitions.end(), artifact_tool_definitions.begin(), artifact_tool_definitions.end());
        tool_definitions.insert(tool_definitions.end(), model_tool_definitions.begin(), model_tool_definitions.end());
        tool_definitions.insert(tool_definitions.end(), built_in_tool_definitions.begin(), built_in_tool_definitions.end());

        std::vector<MessageRecord> working_messages = request.messages;
        // Baseline snapshot before tool calls bloat context. Guard only triggers
        // if the *pre-loop* assembled request is already close to threshold,
        // not on natural in-loop assistant/tool-result growth.
        const std::vector<MessageRecord> pre_tool_loop_messages = working_messages;
        std::string error;
        const bool completion_driver_enabled = built_in_tools::IsCompletionDriverEnabled(
            project_settings_for_tools, proj_settings.selected_agentic_mode_id);
        const int completion_driver_max_continuations =
            built_in_tools::NormalizedCompletionDriverMaxContinuations(project_settings_for_tools);
        // Keep the limit scoped to this desktop send operation. A later send gets
        // a new counter, matching the web automation per-step behavior.
        int completion_driver_continuations = 0;
        bool completion_driver_done = !completion_driver_enabled;
        bool success = false;

        for (int round = 0; ; ++round) {
            // Tool-loop emergency: if pre-tool-loop request already exceeds threshold,
            // schedule compression for the *next* turn (preserves current tool chain).
            if (selected_compression_config && request.model.context_window > 0) {
                ChatRequestOptions check;
                check.system_prompt = request.system_prompt;
                check.model = request.model;
                check.messages = ModelVisibleMessages(pre_tool_loop_messages);
                const size_t est_tool = EstimateRequestInputTokens(check, {}, false);
                const size_t trigger_tool = request.model.context_window * selected_compression_config->context_window_trigger_percent / 100;
                if (est_tool > trigger_tool) {
                    compression_service->MarkCompressionScheduled(project_id, chat_id);
                }
            }
            ChatRequestOptions loop_request = request;
            loop_request.messages = ModelVisibleMessages(working_messages);

            const auto completion = OpenAIClient::StreamToolAwareCompletion(loop_request, tool_definitions, [hwnd, project_id, chat_id](const std::string& piece) {
                auto* payload = new ChatDeltaPayload;
                payload->project_id = project_id;
                payload->chat_id = chat_id;
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

                // Separate model tool calls from regular tool calls so we can
                // run model-tool sub-agents in parallel while regular tools run serially.
                struct PendingToolCall {
                    ChatToolCall tool_call;       // the raw call from completion
                    McpToolCallResult result;     // filled in after execution
                    std::string trace_arguments;  // for display
                };
                std::vector<PendingToolCall> pending;
                pending.reserve(completion.tool_calls.size());
                for (const auto& tc : completion.tool_calls) {
                    PendingToolCall p;
                    p.tool_call = tc;
                    p.trace_arguments = tc.arguments_json;
                    if (!tc.arguments_valid) {
                        p.trace_arguments = tc.original_arguments_json.empty() ? tc.arguments_json : tc.original_arguments_json;
                        if (p.trace_arguments.size() > 2000) p.trace_arguments = p.trace_arguments.substr(0, 2000) + "...";
                        std::ostringstream es;
                        es << "Tool call was not executed because the model returned invalid JSON arguments for \""
                           << tc.name << "\". Tool arguments must be a valid JSON object.";
                        if (!tc.arguments_error.empty()) es << "\nParser error: " << tc.arguments_error;
                        if (!p.trace_arguments.empty()) es << "\nOriginal arguments: " << p.trace_arguments;
                        es << "\nRetry the tool call with valid JSON object arguments.";
                        p.result.success = false;
                        p.result.is_tool_error = true;
                        p.result.content_text = es.str();
                        p.result.raw_result_json = nlohmann::json{{"error", p.result.content_text}, {"invalid_arguments", p.trace_arguments}}.dump(2);
                    }
                    pending.push_back(std::move(p));
                }

                // Identify model-tool calls (agent_*) among valid calls
                std::vector<size_t> model_tool_indices;
                for (size_t pi = 0; pi < pending.size(); ++pi) {
                    if (pending[pi].tool_call.arguments_valid && IsModelToolAlias(pending[pi].tool_call.name)) {
                        model_tool_indices.push_back(pi);
                    }
                }

                if (!model_tool_indices.empty()) {
                    // Run model tool calls in parallel, regular calls serially
                    std::vector<std::thread> sub_threads(model_tool_indices.size());
                    for (size_t ti = 0; ti < model_tool_indices.size(); ++ti) {
                        const size_t pi = model_tool_indices[ti];
                        sub_threads[ti] = std::thread([&pending, pi, &project_id, mcp_manager, rag_service, &providers, &model_tools, &project_variables, &runtime_variables]() {
                            try {
                                pending[pi].result = CallModelToolAgent(
                                    pending[pi].tool_call.name,
                                    pending[pi].tool_call.arguments_json,
                                    project_id,
                                    mcp_manager,
                                    rag_service,
                                    providers,
                                    model_tools,
                                    project_variables,
                                    runtime_variables);
                            } catch (const std::exception& ex) {
                                pending[pi].result.success = false;
                                pending[pi].result.is_tool_error = true;
                                pending[pi].result.content_text = std::string("Model tool agent crashed: ") + ex.what();
                            } catch (...) {
                                pending[pi].result.success = false;
                                pending[pi].result.is_tool_error = true;
                                pending[pi].result.content_text = "Model tool agent crashed with unknown error.";
                            }
                        });
                    }
                    // While model tools run, execute regular (non-model-tool) calls serially
                    for (size_t pi = 0; pi < pending.size(); ++pi) {
                        if (!pending[pi].tool_call.arguments_valid) continue;
                        if (IsModelToolAlias(pending[pi].tool_call.name)) continue;
                        if (artifact_memory_tools::IsArtifactMemoryToolName(pending[pi].tool_call.name)) {
                            pending[pi].result = artifact_memory_tools::CallArtifactMemoryTool(
                                artifact_runtime,
                                pending[pi].tool_call.name,
                                pending[pi].tool_call.arguments_json);
                        } else if (rag_tools::IsRagToolName(pending[pi].tool_call.name, &rag_tool_routes)) {
                            pending[pi].result = rag_tools::CallRagTool(
                                rag_service,
                                mcp_manager,
                                project_id,
                                pending[pi].tool_call.name,
                                pending[pi].tool_call.arguments_json,
                                &rag_tool_routes,
                                working_set_additions.get(),
                                project_variables);
                        } else if (built_in_tools::IsBuiltInToolName(pending[pi].tool_call.name)) {
                            if (pending[pi].tool_call.name == built_in_tools::kQuestionnaireToolName &&
                                built_in_tools::IsQuestionnaireEnabled(project_settings_for_tools, proj_settings.selected_agentic_mode_id)) {
                                pending[pi].result = RunDesktopQuestionnaire(
                                    pending[pi].tool_call.arguments_json);
                            } else {
                                pending[pi].result = built_in_tools::CallTool(
                                    pending[pi].tool_call.name,
                                    pending[pi].tool_call.arguments_json,
                                    project_settings_for_tools,
                                    project_variables,
                                    proj_settings.selected_agentic_mode_id);
                            }
                        } else {
                            pending[pi].result = mcp_manager->CallExposedTool(
                                project_id,
                                pending[pi].tool_call.name,
                                pending[pi].tool_call.arguments_json,
                                runtime_variables);
                        }
                    }
                    // Join all model-tool threads before continuing
                    for (auto& t : sub_threads) {
                        if (t.joinable()) t.join();
                    }
                } else {
                    // No model tools â€” run all calls serially
                    for (auto& p : pending) {
                        if (!p.tool_call.arguments_valid) continue;
                        if (artifact_memory_tools::IsArtifactMemoryToolName(p.tool_call.name)) {
                            p.result = artifact_memory_tools::CallArtifactMemoryTool(
                                artifact_runtime,
                                p.tool_call.name,
                                p.tool_call.arguments_json);
                        } else if (rag_tools::IsRagToolName(p.tool_call.name, &rag_tool_routes)) {
                            p.result = rag_tools::CallRagTool(
                                rag_service,
                                mcp_manager,
                                project_id,
                                p.tool_call.name,
                                p.tool_call.arguments_json,
                                &rag_tool_routes,
                                working_set_additions.get(),
                                project_variables);
                        } else if (built_in_tools::IsBuiltInToolName(p.tool_call.name)) {
                            p.result = built_in_tools::CallTool(
                                p.tool_call.name,
                                p.tool_call.arguments_json,
                                project_settings_for_tools,
                                project_variables,
                                proj_settings.selected_agentic_mode_id);
                        } else {
                            p.result = mcp_manager->CallExposedTool(
                                project_id,
                                p.tool_call.name,
                                p.tool_call.arguments_json,
                                runtime_variables);
                        }
                    }
                }

                for (const auto& p : pending) {
                    if (p.tool_call.name == built_in_tools::kCompletionDriverToolName &&
                        built_in_tools::IsCompletionDriverCompletedResult(p.result)) {
                        completion_driver_done = true;
                    }
                }

                // Post trace and build working_messages tool results
                for (const auto& p : pending) {
                    auto* trace_payload = new ToolTracePayload;
                    trace_payload->project_id = project_id;
                    trace_payload->chat_id = chat_id;
                    const auto tool_it = tool_lookup.find(p.tool_call.name);
                    if (tool_it != tool_lookup.end()) {
                        trace_payload->entry.title = tool_it->second.server_name + " / " + tool_it->second.tool_name;
                    } else if (rag_tools::IsRagToolName(p.tool_call.name, &rag_tool_routes)) {
                        trace_payload->entry.title = rag_tools::TraceTitleForRagTool(p.tool_call.name, rag_tool_routes);
                    } else if (artifact_memory_tools::IsArtifactMemoryToolName(p.tool_call.name)) {
                        trace_payload->entry.title = artifact_memory_tools::TraceTitleForArtifactMemoryTool(p.tool_call.name);
                    } else if (IsModelToolAlias(p.tool_call.name)) {
                        trace_payload->entry.title = "Agent / " + p.tool_call.name;
                    } else if (built_in_tools::IsBuiltInToolName(p.tool_call.name)) {
                        trace_payload->entry.title = built_in_tools::TraceTitleForBuiltInTool(p.tool_call.name);
                    } else {
                        trace_payload->entry.title = p.tool_call.name;
                    }
                    trace_payload->entry.arguments_json = p.trace_arguments;
                    trace_payload->entry.result_text = p.result.content_text;
                    trace_payload->entry.success = p.result.success && !p.result.is_tool_error;
                    PostMessageW(hwnd, kToolTraceMessage, 0, reinterpret_cast<LPARAM>(trace_payload));

                    MessageRecord tool_message;
                    tool_message.role = "tool";
                    tool_message.name = p.tool_call.name;
                    tool_message.tool_call_id = p.tool_call.id;
                    tool_message.content = p.result.content_text;
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
            if (completion_driver_enabled && !completion_driver_done) {
                if (completion_driver_max_continuations > 0 &&
                    completion_driver_continuations >= completion_driver_max_continuations) {
                    Logger::Warn("CompletionDriver",
                        "continuation limit reached project=" + project_id +
                        " chat=" + chat_id +
                        " limit=" + std::to_string(completion_driver_max_continuations));
                    success = true;
                    break;
                }
                ++completion_driver_continuations;
                working_messages.push_back(
                    built_in_tools::MakeCompletionDriverContinuationMessage(
                        completion_driver_continuations,
                        completion_driver_max_continuations));
                continue;
            }
            success = true;
            break;
        }

        if (!success && error.empty()) {
            ChatRequestOptions final_request = request;
            final_request.messages = ModelVisibleMessages(working_messages);
            if (!final_request.system_prompt.empty()) {
                final_request.system_prompt += "\n\n";
            }
            final_request.system_prompt +=
                "The tool loop stopped before a final assistant answer was produced. "
                "Do not call or request any more tools. Use the tool results already "
                "present in the conversation to write the final answer. If the requested "
                "work succeeded, say so and summarize the important output. If it did "
                "not fully succeed, explain the last observed state and what remains.";

            const auto final_result = OpenAIClient::StreamChat(
                final_request,
                [hwnd, project_id, chat_id](const std::string& piece) {
                    auto* payload = new ChatDeltaPayload;
                    payload->project_id = project_id;
                    payload->chat_id = chat_id;
                    payload->text = piece;
                    PostMessageW(hwnd, kChatDeltaMessage, 0, reinterpret_cast<LPARAM>(payload));
                });

            if (final_result.success && !final_result.full_text.empty()) {
                MessageRecord assistant_message;
                assistant_message.role = "assistant";
                assistant_message.content = final_result.full_text;
                assistant_message.created_at = CurrentTimestampUtc();
                working_messages.push_back(std::move(assistant_message));
                success = true;
            } else {
                error = final_result.error.empty()
                    ? "The tool loop stopped before producing a final answer."
                    : "The tool loop stopped before producing a final answer, and the final summary failed: " + final_result.error;
            }
        }

        if (logging) {
            if (!success || !error.empty()) {
                ChatRequestLogger::Log(project_id, logging,
                    log_header + ChatRequestLogger::FormatErrorResponse(error));
            } else if (!working_messages.empty()) {
                ChatRequestLogger::Log(project_id, logging,
                    log_header + ChatRequestLogger::FormatSuccessResponse(working_messages.back().content));
            }
        }

        auto* final_payload = new ChatFinishedPayload;
        final_payload->success = success;
        final_payload->project_id = project_id;
        final_payload->chat_id = chat_id;
        final_payload->error = error;
        for (size_t i = existing_count; i < working_messages.size(); ++i) {
            if (built_in_tools::IsCompletionDriverContinuationMessage(working_messages[i])) {
                continue;
            }
            final_payload->appended_messages.push_back(working_messages[i]);
        }
        final_payload->rag_working_set_additions = std::move(*working_set_additions);
        PostMessageW(hwnd, kChatFinishedMessage, 0, reinterpret_cast<LPARAM>(final_payload));
    } catch (const std::exception& ex) {
        Logger::Error(std::string("Tool-aware chat thread exception: ") + ex.what());
        auto* final_payload = new ChatFinishedPayload;
        final_payload->success = false;
        final_payload->project_id = project_id;
        final_payload->chat_id = chat_id;
        final_payload->error = std::string("Unhandled error in tool-aware chat thread: ") + ex.what();
        {
            MessageRecord err_msg;
            err_msg.role = "assistant";
            err_msg.content = std::string("[System Error] Tool-aware chat thread crashed: ") + ex.what();
            err_msg.created_at = CurrentTimestampUtc();
            final_payload->appended_messages.push_back(std::move(err_msg));
        }
        PostMessageW(hwnd, kChatFinishedMessage, 0, reinterpret_cast<LPARAM>(final_payload));
    } catch (...) {
        Logger::Error("Tool-aware chat thread exception: unknown error");
        auto* final_payload = new ChatFinishedPayload;
        final_payload->success = false;
        final_payload->project_id = project_id;
        final_payload->chat_id = chat_id;
        final_payload->error = "Unhandled unknown error in tool-aware chat thread.";
        {
            MessageRecord err_msg;
            err_msg.role = "assistant";
            err_msg.content = "[System Error] Tool-aware chat thread crashed with an unknown error.";
            err_msg.created_at = CurrentTimestampUtc();
            final_payload->appended_messages.push_back(std::move(err_msg));
        }
        PostMessageW(hwnd, kChatFinishedMessage, 0, reinterpret_cast<LPARAM>(final_payload));
    }
    }).detach();
}

void MainWindow::CompressCurrentContext() {
    if (active_project_id_.empty() || active_chat_id_.empty()) {
        MessageBoxW(hwnd_, L"Select a chat first.", L"No Chat Selected", MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (IsChatInFlight(active_chat_id_)) {
        MessageBoxW(hwnd_, L"Cannot compress context while a request is in progress for this chat.", L"Chat Busy", MB_OK | MB_ICONINFORMATION);
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
    auto messages = ModelVisibleMessages(storage_.LoadMessages(active_project_id_, active_chat_id_));
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
    const auto resolved_prompt_variables = BuildResolvedVariablesForChat(
        active_project_id_, active_chat_id_, proj_settings);

    std::string compressed = compression_service_.CompressConversation(
        messages,
        active_project_id_,
        active_chat_id_,
        proj_settings.selected_compression_config_id,
        model_caller,
        true,
        "manual",
        resolved_prompt_variables);

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
    streaming_previews_by_chat_[payload->chat_id] += Utf8ToWide(payload->text);
    if (payload->chat_id == active_chat_id_) {
        RenderTranscript();
    }
}

void MainWindow::OnChatFinished(ChatFinishedPayload* payload) {
    std::unique_ptr<ChatFinishedPayload> guard(payload);

    // Clear per-chat state regardless of which chat finished.
    SetChatBusy(payload->chat_id, false);
    streaming_previews_by_chat_.erase(payload->chat_id);

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

    // Merge RAG working set additions (dedup by chunk_id, cap at 60 entries per chat)
    if (!payload->rag_working_set_additions.empty()) {
        auto& ws = rag_working_sets_by_chat_[payload->chat_id];
        for (auto& entry : payload->rag_working_set_additions) {
            const bool already = std::any_of(ws.begin(), ws.end(),
                [&](const RagWorkingSetEntry& e) { return e.chunk_id == entry.chunk_id; });
            if (!already) {
                ws.push_back(std::move(entry));
            }
        }
        constexpr size_t kMaxWorkingSetSize = 60;
        if (ws.size() > kMaxWorkingSetSize) {
            ws.erase(ws.begin(), ws.begin() + static_cast<ptrdiff_t>(ws.size() - kMaxWorkingSetSize));
        }
        storage_.SaveChatRagWorkingSet(payload->project_id, payload->chat_id, ws);
    }

    if (payload->chat_id == active_chat_id_) {
        // The visible chat finished â€” update transcript and show status.
        RenderTranscript();
        RenderToolTrace();
        if (payload->success) {
            UpdateStatus(L"Response complete.");
        } else {
            UpdateStatus(Utf8ToWide("Request failed: " + payload->error));
            MessageBoxW(hwnd_, Utf8ToWide(payload->error).c_str(), L"Request Failed", MB_OK | MB_ICONERROR);
        }
    } else {
        // A background chat finished â€” tree label was already cleared by SetChatBusy.
        // Update status bar only if nothing else is running for the active chat.
        if (!IsChatInFlight(active_chat_id_)) {
            if (!payload->success) {
                UpdateStatus(Utf8ToWide("Background chat \"" + payload->chat_id + "\" failed: " + payload->error));
            } else {
                UpdateStatus(L"Background chat response complete.");
            }
        }
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
    if (!IsChatInFlight(active_chat_id_)) {
        if (tools.empty()) {
            UpdateStatus(L"No connected MCP tools available for this project.");
        } else {
            UpdateStatus(std::wstring(L"Connected MCP tools available: ") + std::to_wstring(tools.size()));
        }
    }
    RenderToolTrace();
}

void MainWindow::OnWebContentChanged() {
    ReloadProjects(active_project_id_, active_chat_id_);
}

std::vector<ProjectMcpVariableValue> MainWindow::BuildRuntimeVariablesForChat(
    const std::string& project_id,
    const std::string& chat_id) const {
    std::string project_name;
    std::string chat_name;

    for (const auto& project : projects_) {
        if (project.info.id != project_id) continue;
        project_name = project.info.name;
        for (const auto& chat : project.chats) {
            if (chat.id == chat_id) {
                chat_name = chat.name;
                break;
            }
        }
        break;
    }

    std::vector<ProjectMcpVariableValue> values;
    variable_resolver::UpsertValue(values, "PROJECTNAME", project_name);
    variable_resolver::UpsertValue(values, "CHATNAME", chat_name);
    variable_resolver::UpsertValue(values, "CHATFULLNAME", chat_name);
    variable_resolver::UpsertValue(values, "CHATID", chat_id);
    variable_resolver::UpsertValue(values, "USERNAME", "");
    variable_resolver::UpsertValue(values, "USER", "");
    variable_resolver::UpsertValue(values, "USEREMAIL", "");
    variable_resolver::UpsertValue(values, "UserName", "");
    return values;
}

std::vector<ProjectMcpVariableValue> MainWindow::BuildResolvedVariablesForChat(
    const std::string& project_id,
    const std::string& chat_id,
    const ProjectSettings& project_settings) const {
    std::vector<ProjectMcpVariableValue> resolved;
    for (const auto& binding : mcp_manager_.GetProjectBindings(project_id)) {
        for (const auto& variable : binding.variables) {
            variable_resolver::UpsertValue(resolved, variable.name, variable.value);
        }
    }
    for (const auto& variable : project_settings.project_variables) {
        variable_resolver::UpsertValue(resolved, variable);
    }
    for (const auto& variable : BuildRuntimeVariablesForChat(project_id, chat_id)) {
        variable_resolver::UpsertValue(resolved, variable.name, variable.value);
    }

    resolved = variable_resolver::ResolveValues(resolved);
    variable_resolver::EnsureFolderVariables(resolved, mcp_manager_.global_variables());
    return resolved;
}

void MainWindow::RenderTranscript() {
    std::wstring transcript;
    if (active_chat_id_.empty()) {
        transcript = L"Create or select a chat to begin.";
    } else if (active_messages_skipped_for_size_) {
        const double size_mb = static_cast<double>(active_messages_file_size_) / (1024.0 * 1024.0);
        std::wostringstream message;
        message.setf(std::ios::fixed);
        message.precision(1);
        message << L"This chat history is " << size_mb
                << L" MB, so the desktop preview skipped loading it to keep startup responsive.\r\n\r\n"
                << L"The web chat can still open the full record. Start a smaller chat or use the website for this large automation/debug transcript.";
        transcript = message.str();
    } else {
        for (const auto& message : active_messages_) {
            if (message.role == "tool") {
                continue;
            }
            if (message.role == "assistant" &&
                (message.content.empty() || !message.tool_calls_json.empty())) {
                continue;
            }

            if (message.role == "file") {
                transcript += L"File:\r\n";
                transcript += FormatFileUploadForTranscript(message.content);
                transcript += L"\r\n\r\n";
                continue;
            }

            transcript += message.role == "user" ? L"You" : L"Assistant";
            transcript += L":\r\n";
            transcript += Utf8ToWide(message.content);
            transcript += L"\r\n\r\n";
        }

        if (IsChatInFlight(active_chat_id_)) {
            transcript += L"Assistant:\r\n";
            auto preview_it = streaming_previews_by_chat_.find(active_chat_id_);
            if (preview_it != streaming_previews_by_chat_.end()) {
                transcript += preview_it->second;
            }
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

bool MainWindow::IsChatInFlight(const std::string& chat_id) const {
    if (chat_id.empty()) {
        return false;
    }
    auto it = chats_in_flight_.find(chat_id);
    return it != chats_in_flight_.end() && it->second;
}

int MainWindow::CountChatsInFlight() const {
    int count = 0;
    for (const auto& [id, busy] : chats_in_flight_) {
        if (busy) {
            ++count;
        }
    }
    return count;
}

void MainWindow::RefreshInputState() {
    // Called after switching active chat so controls reflect the new chat's status.
    const bool busy = IsChatInFlight(active_chat_id_);
    const bool large_preview = active_messages_skipped_for_size_;
    EnableWindow(send_button_, !busy && !large_preview);
    EnableWindow(input_, !busy && !large_preview);
    EnableWindow(compress_button_, !busy && !large_preview);
    EnableWindow(setup_system_button_, TRUE);  // Always enabled; does not require an active chat.
    // Tree, navigation, and manager buttons are NEVER locked by per-chat state.
}

void MainWindow::SetChatBusy(const std::string& chat_id, bool busy) {
    if (busy) {
        chats_in_flight_[chat_id] = true;
    } else {
        chats_in_flight_.erase(chat_id);
    }

    // Update input controls only when the affected chat is the one currently visible.
    if (chat_id == active_chat_id_) {
        RefreshInputState();
    }

    // Update the tree label to add or remove the streaming indicator (â—).
    UpdateChatTreeLabel(chat_id);
}

void MainWindow::UpdateChatTreeLabel(const std::string& chat_id) {
    auto tree_it = chat_tree_items_.find(chat_id);
    if (tree_it == chat_tree_items_.end() || !tree_it->second) {
        return;
    }

    // Find the chat name in the loaded project list.
    const ChatInfo* chat_info = nullptr;
    for (const auto& project : projects_) {
        for (const auto& chat : project.chats) {
            if (chat.id == chat_id) {
                chat_info = &chat;
                break;
            }
        }
        if (chat_info) {
            break;
        }
    }
    if (!chat_info) {
        return;
    }

    std::wstring label = Utf8ToWide(chat_info->name);
    if (IsChatInFlight(chat_id)) {
        label += L" \u25cf";  // â— U+25CF BLACK CIRCLE
    }

    TVITEMW item{};
    item.mask = TVIF_TEXT;
    item.hItem = tree_it->second;
    item.pszText = label.data();
    TreeView_SetItem(tree_, &item);
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

struct CommandLineArgs {
    bool web_config = false;
    HeadlessCommandMode headless_mode = HeadlessCommandMode::None;
    std::filesystem::path headless_settings_path;
    LogLevel log_level = LogLevel::Info;
    std::wstring log_level_str;
};

static CommandLineArgs ParseCommandLineArgs(PWSTR cmd_line) {
    (void)cmd_line;
    CommandLineArgs args;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        return args;
    }

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--web-config") {
            args.web_config = true;
        } else if (arg == L"--log-level") {
            if (i + 1 < argc) {
                args.log_level_str = argv[++i];
            }
            if (!args.log_level_str.empty()) {
                if (args.log_level_str == L"error") args.log_level = LogLevel::Error;
                else if (args.log_level_str == L"warn") args.log_level = LogLevel::Warn;
                else if (args.log_level_str == L"info") args.log_level = LogLevel::Info;
                else if (args.log_level_str == L"debug") args.log_level = LogLevel::Debug;
            }
        } else if (arg == L"--olama-setup" || arg == L"--ollama-setup") {
            args.headless_mode = HeadlessCommandMode::OllamaSetup;
            if (i + 1 < argc) {
                args.headless_settings_path = std::filesystem::path(argv[++i]);
            }
        } else if (arg.rfind(L"--olama-setup=", 0) == 0 || arg.rfind(L"--ollama-setup=", 0) == 0) {
            args.headless_mode = HeadlessCommandMode::OllamaSetup;
            const size_t equals = arg.find(L'=');
            args.headless_settings_path = std::filesystem::path(arg.substr(equals + 1));
        } else if (arg == L"--olama-remote" || arg == L"--ollama-remote") {
            args.headless_mode = HeadlessCommandMode::OllamaRemote;
            if (i + 1 < argc) {
                args.headless_settings_path = std::filesystem::path(argv[++i]);
            }
        } else if (arg.rfind(L"--olama-remote=", 0) == 0 || arg.rfind(L"--ollama-remote=", 0) == 0) {
            args.headless_mode = HeadlessCommandMode::OllamaRemote;
            const size_t equals = arg.find(L'=');
            args.headless_settings_path = std::filesystem::path(arg.substr(equals + 1));
        } else if (arg == L"--remote-worker" || arg == L"--remote-worker-setup") {
            args.headless_mode = (arg == L"--remote-worker-setup")
                ? HeadlessCommandMode::RemoteWorkerSetup
                : HeadlessCommandMode::RemoteWorkerRun;
            if (i + 1 < argc) {
                args.headless_settings_path = std::filesystem::path(argv[++i]);
            }
        } else if (arg.rfind(L"--remote-worker=", 0) == 0 || arg.rfind(L"--remote-worker-setup=", 0) == 0) {
            args.headless_mode = (arg.rfind(L"--remote-worker-setup=", 0) == 0)
                ? HeadlessCommandMode::RemoteWorkerSetup
                : HeadlessCommandMode::RemoteWorkerRun;
            const size_t equals = arg.find(L'=');
            args.headless_settings_path = std::filesystem::path(arg.substr(equals + 1));
        } else if (arg == L"--help" || arg == L"-h") {
            MessageBoxW(nullptr,
                L"Agent Desktop Command Line Options:\n\n"
                L"  --web-config     Open Web Config dialog directly\n"
                L"  --log-level N    Set log level: error, warn, info, debug\n"
                L"  --olama-setup FILE\n"
                L"                   Install Ollama if needed and pull the configured image vision model\n"
                L"  --olama-remote FILE\n"
                L"                   Run local Ollama image vision endpoints from image ingest settings\n"
                L"  --remote-worker-setup FILE\n"
                L"                   Generate shared secret and self-signed certificate for remote worker\n"
                L"  --remote-worker FILE\n"
                L"                   Run the HTTPS remote provider worker\n"
                L"  --ollama-setup / --ollama-remote are also accepted\n"
                L"  --help, -h       Show this help message\n\n"
                L"Example:\n"
                L"  agent.exe --web-config --log-level debug\n"
                L"  agent.exe --olama-remote rag_image_ingest_settings.json\n"
                L"  agent.exe --remote-worker worker.json",
                L"Agent Desktop Help", MB_OK | MB_ICONINFORMATION);
        }
    }

    LocalFree(argv);
    return args;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmd_line, int show_command) {
    CommandLineArgs args = ParseCommandLineArgs(cmd_line);

    Logger::Initialize(std::filesystem::current_path() / "logs", args.log_level);
    Logger::Info("Agent Desktop starting");
    Logger::Info("Command line: " + WideToUtf8(std::wstring(cmd_line)));

    if (args.headless_mode != HeadlessCommandMode::None) {
        const int exit_code = RunHeadlessOllamaCommand(args.headless_mode, args.headless_settings_path);
        Logger::Info("Agent Desktop headless command complete: " + std::to_string(exit_code));
        Logger::Shutdown();
        return exit_code;
    }

    Logger::Info("Startup: loading rich edit library");
    LoadLibraryW(L"Msftedit.dll");

    Logger::Info("Startup: initializing common controls");
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_TREEVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    int exit_code = 0;
    {
        Logger::Info("Startup: constructing main window object");
        MainWindow window;
        Logger::Info("Startup: creating main window");
        HWND hwnd = window.Create(instance);
        if (!hwnd) {
            Logger::Error("Failed to create main window");
            Logger::Shutdown();
            return 1;
        }
        Logger::Info("Startup: main window created");

        if (args.web_config) {
            Logger::Info("Opening Web Config from command line");
            window.OpenWebConfig();
        }

        ShowWindow(hwnd, show_command);
        UpdateWindow(hwnd);

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            const HWND agentic_hwnd = window.GetAgenticModesWindow();
            if (agentic_hwnd && IsWindow(agentic_hwnd) &&
                IsDialogMessageW(agentic_hwnd, &msg)) {
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        Logger::Info("Agent Desktop shutting down");
        exit_code = static_cast<int>(msg.wParam);
    }
    Logger::Info("Agent Desktop shutdown complete");
    Logger::Shutdown();

    return exit_code;
}
