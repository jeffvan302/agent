#include "ollama_cli_bridge.h"

#include "util.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {
std::wstring QuoteArgument(const std::wstring& value) {
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

bool HasLaunchOverrides(const OllamaCliLaunchOptions& options) {
    return options.num_threads > 0 ||
        options.no_gpu ||
        options.gpu_layers > 0 ||
        options.context_length > 0;
}

std::wstring ToWide(unsigned long value) {
    return std::to_wstring(static_cast<unsigned long long>(value));
}

bool WriteAllToHandle(HANDLE handle, const std::string& data) {
    const char* cursor = data.data();
    size_t remaining = data.size();
    while (remaining > 0) {
        DWORD written = 0;
        const DWORD chunk = remaining > static_cast<size_t>(DWORD(-1))
            ? DWORD(-1)
            : static_cast<DWORD>(remaining);
        if (!WriteFile(handle, cursor, chunk, &written, nullptr)) {
            return false;
        }
        remaining -= written;
        cursor += written;
    }
    return true;
}

std::string ReadHandleToString(HANDLE handle) {
    std::string output;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(handle, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        output.append(buffer, buffer + read);
    }
    return output;
}

std::map<std::wstring, std::wstring> LoadEnvironmentMap() {
    std::map<std::wstring, std::wstring> values;
    LPWCH raw = GetEnvironmentStringsW();
    if (!raw) {
        return values;
    }
    const wchar_t* current = raw;
    while (*current) {
        std::wstring entry = current;
        const size_t separator = entry.find(L'=');
        if (separator != std::wstring::npos && separator > 0) {
            values[entry.substr(0, separator)] = entry.substr(separator + 1);
        }
        current += entry.size() + 1;
    }
    FreeEnvironmentStringsW(raw);
    return values;
}
}  // namespace

void OllamaTerminalSanitizer::Append(const char* data, size_t length) {
    if (!data || length == 0) {
        return;
    }

    for (size_t i = 0; i < length; ++i) {
        const unsigned char ch = static_cast<unsigned char>(data[i]);
        if (in_escape_) {
            if (ch >= 0x40 && ch <= 0x7e) {
                in_escape_ = false;
            }
            continue;
        }
        if (ch == 0x1b) {
            in_escape_ = true;
            continue;
        }
        if (ch == '\r') {
            current_line_.clear();
            continue;
        }
        if (ch == '\n') {
            completed_lines_ += current_line_;
            completed_lines_.push_back('\n');
            current_line_.clear();
            continue;
        }
        if (ch == '\b') {
            if (!current_line_.empty()) {
                current_line_.pop_back();
            }
            continue;
        }
        if (ch < 0x20 && ch != '\t') {
            continue;
        }
        current_line_.push_back(static_cast<char>(ch));
    }
}

std::string OllamaTerminalSanitizer::VisibleText() const {
    return completed_lines_ + current_line_;
}

std::optional<std::wstring> FindOllamaCliPath() {
    wchar_t override_buffer[4096] = {};
    const DWORD override_length = GetEnvironmentVariableW(L"AGENT_OLLAMA_CLI_PATH", override_buffer, static_cast<DWORD>(std::size(override_buffer)));
    if (override_length > 0 && override_length < std::size(override_buffer)) {
        return std::wstring(override_buffer, override_length);
    }

    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD found = SearchPathW(nullptr, L"ollama.exe", nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (found > 0 && found < buffer.size()) {
        return std::wstring(buffer.data());
    }

    const std::vector<std::wstring> candidates = {
        ExpandEnvironmentPath(L"%LOCALAPPDATA%\\Programs\\Ollama\\ollama.exe"),
        ExpandEnvironmentPath(L"%ProgramFiles%\\Ollama\\ollama.exe"),
        ExpandEnvironmentPath(L"%ProgramFiles(x86)%\\Ollama\\ollama.exe"),
    };
    for (const auto& candidate : candidates) {
        if (candidate.empty()) {
            continue;
        }
        DWORD attributes = GetFileAttributesW(candidate.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            return candidate;
        }
    }

    return std::nullopt;
}

bool BuildOllamaCliLaunchSpec(const std::vector<std::wstring>& args,
                              const OllamaCliLaunchOptions& options,
                              OllamaCliLaunchSpec* spec,
                              std::string* error) {
    if (!spec) {
        if (error) {
            *error = "Launch spec output is missing.";
        }
        return false;
    }

    const auto path = FindOllamaCliPath();
    if (!path) {
        if (error) {
            *error = "Could not find ollama.exe. Install Ollama or set AGENT_OLLAMA_CLI_PATH.";
        }
        return false;
    }

    spec->executable_path = *path;
    spec->command_line = QuoteArgument(*path);
    for (const auto& arg : args) {
        spec->command_line.push_back(L' ');
        spec->command_line += QuoteArgument(arg);
    }

    spec->environment_block.clear();
    if (!HasLaunchOverrides(options)) {
        return true;
    }

    auto environment = LoadEnvironmentMap();
    if (options.num_threads > 0) {
        environment[L"OLLAMA_NUM_THREADS"] = std::to_wstring(options.num_threads);
    }
    if (options.no_gpu) {
        environment[L"OLLAMA_NO_GPU"] = L"1";
        environment[L"CUDA_VISIBLE_DEVICES"] = L"";
    }
    if (options.gpu_layers > 0) {
        environment[L"OLLAMA_GPU_LAYERS"] = std::to_wstring(options.gpu_layers);
    }
    if (options.context_length > 0) {
        environment[L"OLLAMA_CONTEXT_LENGTH"] = std::to_wstring(options.context_length);
    }

    for (const auto& [name, value] : environment) {
        spec->environment_block.insert(spec->environment_block.end(), name.begin(), name.end());
        spec->environment_block.push_back(L'=');
        spec->environment_block.insert(spec->environment_block.end(), value.begin(), value.end());
        spec->environment_block.push_back(L'\0');
    }
    spec->environment_block.push_back(L'\0');
    return true;
}

bool RunOllamaCliCommand(const std::vector<std::wstring>& args,
                         const std::string& stdin_text,
                         const OllamaCliLaunchOptions& options,
                         OllamaCliCommandOutput* output,
                         std::string* error) {
    if (!output) {
        if (error) {
            *error = "Command output destination is missing.";
        }
        return false;
    }

    OllamaCliLaunchSpec launch;
    if (!BuildOllamaCliLaunchSpec(args, options, &launch, error)) {
        return false;
    }

    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;

    HANDLE stdin_read = nullptr;
    HANDLE stdin_write = nullptr;
    HANDLE stdout_read = nullptr;
    HANDLE stdout_write = nullptr;
    HANDLE stderr_read = nullptr;
    HANDLE stderr_write = nullptr;

    auto close_handle = [](HANDLE& handle) {
        if (handle) {
            CloseHandle(handle);
            handle = nullptr;
        }
    };

    if (!CreatePipe(&stdin_read, &stdin_write, &security_attributes, 0) ||
        !SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0) ||
        !CreatePipe(&stdout_read, &stdout_write, &security_attributes, 0) ||
        !SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0) ||
        !CreatePipe(&stderr_read, &stderr_write, &security_attributes, 0) ||
        !SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0)) {
        if (error) {
            *error = "Could not create pipes for the Ollama CLI bridge.";
        }
        close_handle(stdin_read);
        close_handle(stdin_write);
        close_handle(stdout_read);
        close_handle(stdout_write);
        close_handle(stderr_read);
        close_handle(stderr_write);
        return false;
    }

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup_info.hStdInput = stdin_read;
    startup_info.hStdOutput = stdout_write;
    startup_info.hStdError = stderr_write;
    startup_info.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION process_info{};
    std::vector<wchar_t> mutable_command(launch.command_line.begin(), launch.command_line.end());
    mutable_command.push_back(L'\0');
    const LPVOID environment_block = launch.environment_block.empty()
        ? nullptr
        : static_cast<LPVOID>(launch.environment_block.data());
    const DWORD flags = launch.environment_block.empty()
        ? CREATE_NO_WINDOW
        : (CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT);

    if (!CreateProcessW(
            nullptr,
            mutable_command.data(),
            nullptr,
            nullptr,
            TRUE,
            flags,
            environment_block,
            nullptr,
            &startup_info,
            &process_info)) {
        if (error) {
            *error = "Could not launch the Ollama CLI (Windows error " + std::to_string(GetLastError()) + ").";
        }
        close_handle(stdin_read);
        close_handle(stdin_write);
        close_handle(stdout_read);
        close_handle(stdout_write);
        close_handle(stderr_read);
        close_handle(stderr_write);
        return false;
    }

    close_handle(stdin_read);
    close_handle(stdout_write);
    close_handle(stderr_write);
    CloseHandle(process_info.hThread);

    if (!stdin_text.empty() && !WriteAllToHandle(stdin_write, stdin_text)) {
        if (error) {
            *error = "Could not send stdin to the Ollama CLI process.";
        }
        close_handle(stdin_write);
        TerminateProcess(process_info.hProcess, 1);
        close_handle(stdout_read);
        close_handle(stderr_read);
        CloseHandle(process_info.hProcess);
        return false;
    }
    close_handle(stdin_write);

    std::string stderr_output;
    std::thread stderr_reader([&]() {
        stderr_output = ReadHandleToString(stderr_read);
    });
    const std::string stdout_output = ReadHandleToString(stdout_read);
    close_handle(stdout_read);

    WaitForSingleObject(process_info.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(process_info.hProcess, &exit_code);
    CloseHandle(process_info.hProcess);

    if (stderr_reader.joinable()) {
        stderr_reader.join();
    }
    close_handle(stderr_read);

    output->exit_code = exit_code;
    output->stdout_text = SanitizeOllamaTerminalOutput(stdout_output);
    output->stderr_text = SanitizeOllamaTerminalOutput(stderr_output);
    return true;
}

std::string SanitizeOllamaTerminalOutput(const std::string& raw_text) {
    OllamaTerminalSanitizer sanitizer;
    sanitizer.Append(raw_text.data(), raw_text.size());
    return sanitizer.VisibleText();
}
