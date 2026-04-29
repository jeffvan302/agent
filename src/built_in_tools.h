#pragma once

#include "mcp_manager.h"
#include "openai_client.h"
#include "types.h"
#include "util.h"
#include "variable_resolver.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <windows.h>

namespace built_in_tools {

inline constexpr const char* kPowerShellToolName = "powershell_execute";

inline bool IsBuiltInToolName(const std::string& name) {
    return name == kPowerShellToolName;
}

inline std::vector<ChatToolDefinition> BuildDefinitions(const ProjectSettings& settings) {
    std::vector<ChatToolDefinition> definitions;
    if (settings.built_in_powershell_enabled) {
        ChatToolDefinition tool;
        tool.name = kPowerShellToolName;
        tool.description =
            "Execute a PowerShell command line on the host machine. This is a high-risk built-in tool: "
            "use it only when the user explicitly wants local command execution. Provide concise commands, "
            "avoid destructive actions unless explicitly requested, and report stdout/stderr back to the user.";
        tool.parameters_json = R"({
  "type": "object",
  "properties": {
    "command": {"type": "string", "description": "PowerShell command to execute."},
    "timeout_seconds": {"type": "integer", "description": "Optional timeout from 1 to 120 seconds.", "minimum": 1, "maximum": 120}
  },
  "required": ["command"]
})";
        definitions.push_back(std::move(tool));
    }
    return definitions;
}

inline std::string Base64Encode(const unsigned char* data, size_t len) {
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        const unsigned int b0 = data[i];
        const unsigned int b1 = (i + 1 < len) ? data[i + 1] : 0;
        const unsigned int b2 = (i + 2 < len) ? data[i + 2] : 0;
        out.push_back(kTable[(b0 >> 2) & 0x3F]);
        out.push_back(kTable[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)]);
        out.push_back(i + 1 < len ? kTable[((b1 & 0x0F) << 2) | ((b2 >> 6) & 0x03)] : '=');
        out.push_back(i + 2 < len ? kTable[b2 & 0x3F] : '=');
    }
    return out;
}

inline McpToolCallResult ErrorResult(const std::string& message) {
    McpToolCallResult result;
    result.success = false;
    result.is_tool_error = true;
    result.content_text = message;
    result.raw_result_json = nlohmann::json{{"error", message}}.dump(2);
    return result;
}

inline std::string ReadAvailablePipe(HANDLE pipe, size_t max_bytes, bool* truncated) {
    std::string output;
    char buffer[4096];
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || available == 0) {
            break;
        }
        DWORD to_read = std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer)));
        DWORD read = 0;
        if (!ReadFile(pipe, buffer, to_read, &read, nullptr) || read == 0) {
            break;
        }
        if (output.size() + read > max_bytes) {
            const size_t remaining = max_bytes > output.size() ? max_bytes - output.size() : 0;
            output.append(buffer, buffer + remaining);
            if (truncated) *truncated = true;
        } else if (output.size() < max_bytes) {
            output.append(buffer, buffer + read);
        }
    }
    return output;
}

inline McpToolCallResult CallPowerShell(
    const std::string& arguments_json,
    const std::string& working_directory_template,
    const std::vector<ProjectMcpVariableValue>& variables) {
    nlohmann::json args;
    try {
        args = nlohmann::json::parse(arguments_json.empty() ? "{}" : arguments_json);
    } catch (const std::exception& ex) {
        return ErrorResult(std::string("Invalid PowerShell tool arguments: ") + ex.what());
    }
    const std::string command = Trim(args.value("command", ""));
    if (command.empty()) {
        return ErrorResult("PowerShell tool requires a non-empty command.");
    }
    int timeout_seconds = args.value("timeout_seconds", 30);
    timeout_seconds = std::clamp(timeout_seconds, 1, 120);

    std::string working_dir = variable_resolver::ExpandTemplate(
        working_directory_template.empty() ? "$ProjectFolder$" : working_directory_template,
        variables);
    working_dir = Trim(working_dir);

    const std::wstring command_w = Utf8ToWide(
        "[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)\n" + command);
    const std::string encoded = Base64Encode(
        reinterpret_cast<const unsigned char*>(command_w.data()),
        command_w.size() * sizeof(wchar_t));
    std::wstring command_line = L"powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -EncodedCommand " + Utf8ToWide(encoded);

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
        return ErrorResult("Failed to create process output pipe.");
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nullptr;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;

    PROCESS_INFORMATION process{};
    std::vector<wchar_t> cmd_buffer(command_line.begin(), command_line.end());
    cmd_buffer.push_back(L'\0');
    std::wstring working_dir_w = Utf8ToWide(working_dir);
    const wchar_t* cwd = working_dir_w.empty() ? nullptr : working_dir_w.c_str();

    const BOOL created = CreateProcessW(
        nullptr,
        cmd_buffer.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        cwd,
        &startup,
        &process);
    CloseHandle(write_pipe);
    if (!created) {
        const DWORD err = GetLastError();
        CloseHandle(read_pipe);
        return ErrorResult("Failed to start PowerShell. CreateProcess error: " + std::to_string(err));
    }

    const DWORD timeout_ms = static_cast<DWORD>(timeout_seconds) * 1000;
    const DWORD start = GetTickCount();
    bool timed_out = false;
    bool truncated = false;
    std::string output;
    constexpr size_t kMaxOutputBytes = 64 * 1024;
    for (;;) {
        output += ReadAvailablePipe(read_pipe, kMaxOutputBytes - std::min(output.size(), kMaxOutputBytes), &truncated);
        const DWORD wait = WaitForSingleObject(process.hProcess, 50);
        if (wait == WAIT_OBJECT_0) break;
        if (GetTickCount() - start >= timeout_ms) {
            timed_out = true;
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, 2000);
            break;
        }
    }
    output += ReadAvailablePipe(read_pipe, kMaxOutputBytes - std::min(output.size(), kMaxOutputBytes), &truncated);

    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(read_pipe);

    if (truncated) {
        output += "\n[output truncated at 64 KiB]";
    }

    McpToolCallResult result;
    result.success = !timed_out && exit_code == 0;
    result.is_tool_error = !result.success;
    nlohmann::json payload = {
        {"tool", kPowerShellToolName},
        {"success", result.success},
        {"exit_code", exit_code},
        {"timed_out", timed_out},
        {"working_directory", working_dir},
        {"output", output},
    };
    result.raw_result_json = payload.dump(2);
    result.content_text = "PowerShell exit code: " + std::to_string(exit_code);
    if (timed_out) result.content_text += " (timed out)";
    result.content_text += "\nWorking directory: " + (working_dir.empty() ? std::string("(default)") : working_dir);
    result.content_text += "\n\n" + (output.empty() ? std::string("(no output)") : output);
    return result;
}

inline McpToolCallResult CallTool(
    const std::string& name,
    const std::string& arguments_json,
    const ProjectSettings& settings,
    const std::vector<ProjectMcpVariableValue>& variables) {
    if (name == kPowerShellToolName && settings.built_in_powershell_enabled) {
        return CallPowerShell(arguments_json, settings.built_in_powershell_working_directory, variables);
    }
    return ErrorResult("Built-in tool is not enabled for this project: " + name);
}

}  // namespace built_in_tools
