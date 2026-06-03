#include "mcp_manager.h"

#include <windows.h>
#include <shellapi.h>

#include "util.h"
#include "variable_resolver.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

using json = nlohmann::json;

namespace {
constexpr auto kRequestTimeout = std::chrono::seconds(45);
constexpr wchar_t kProtocolVersion[] = L"2025-11-25";

std::wstring QuoteCommandPart(const std::wstring& value) {
    if (value.empty()) {
        return L"\"\"";
    }

    bool needs_quotes = value.find_first_of(L" \t\"") != std::wstring::npos;
    if (!needs_quotes) {
        return value;
    }

    std::wstring output = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
            output.push_back(ch);
            continue;
        }

        if (ch == L'"') {
            output.append(backslashes, L'\\');
            output.push_back(L'\\');
        }

        backslashes = 0;
        output.push_back(ch);
    }

    output.append(backslashes, L'\\');
    output.push_back(L'"');
    return output;
}

std::wstring BuildCommandLine(const McpServerConfig& config) {
    std::wstring command_line = QuoteCommandPart(Utf8ToWide(config.command));
    for (const auto& argument : config.arguments) {
        command_line += L" ";
        command_line += QuoteCommandPart(Utf8ToWide(argument));
    }
    return command_line;
}

std::wstring ToLowerWide(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(::towlower(ch)); });
    return value;
}

std::wstring GetMergedEnvValue(const std::vector<std::string>& env_entries, const std::wstring& key) {
    const std::wstring key_lower = ToLowerWide(key);
    for (auto it = env_entries.rbegin(); it != env_entries.rend(); ++it) {
        const std::wstring entry = Utf8ToWide(*it);
        const size_t equals = entry.find(L'=');
        if (equals == std::wstring::npos || equals == 0) {
            continue;
        }
        if (ToLowerWide(entry.substr(0, equals)) == key_lower) {
            return entry.substr(equals + 1);
        }
    }

    DWORD needed = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
    if (needed == 0) {
        return {};
    }
    std::wstring value(static_cast<size_t>(needed), L'\0');
    GetEnvironmentVariableW(key.c_str(), value.data(), needed);
    if (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return value;
}

std::vector<std::wstring> SplitSemicolonList(const std::wstring& value) {
    std::vector<std::wstring> items;
    std::wstringstream stream(value);
    std::wstring item;
    while (std::getline(stream, item, L';')) {
        item = TrimWide(item);
        if (!item.empty()) {
            items.push_back(item);
        }
    }
    return items;
}

std::vector<std::wstring> GetPathextList(const std::vector<std::string>& env_entries) {
    std::vector<std::wstring> extensions = SplitSemicolonList(GetMergedEnvValue(env_entries, L"PATHEXT"));
    if (extensions.empty()) {
        extensions = {L".COM", L".EXE", L".BAT", L".CMD", L".PS1"};
    }

    bool has_ps1 = false;
    for (auto& extension : extensions) {
        if (!extension.empty() && extension.front() != L'.') {
            extension.insert(extension.begin(), L'.');
        }
        extension = ToLowerWide(extension);
        if (extension == L".ps1") {
            has_ps1 = true;
        }
    }
    if (!has_ps1) {
        extensions.push_back(L".ps1");
    }
    return extensions;
}

std::pair<std::wstring, std::vector<std::wstring>> NormalizeCommandAndArguments(const McpServerConfig& config) {
    std::wstring command = TrimWide(Utf8ToWide(config.command));
    std::vector<std::wstring> arguments;
    for (const auto& argument : config.arguments) {
        arguments.push_back(Utf8ToWide(argument));
    }

    if (arguments.empty() && command.find_first_of(L" \t") != std::wstring::npos) {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(command.c_str(), &argc);
        if (argv && argc > 1) {
            command = argv[0];
            arguments.clear();
            for (int i = 1; i < argc; ++i) {
                arguments.push_back(argv[i]);
            }
        }
        if (argv) {
            LocalFree(argv);
        }
    }

    return {command, arguments};
}

std::optional<std::filesystem::path> FindExistingCommandPath(const std::wstring& command, const std::vector<std::string>& env_entries) {
    if (command.empty()) {
        return std::nullopt;
    }

    const std::filesystem::path command_path(command);
    const bool has_directory = command.find(L'\\') != std::wstring::npos || command.find(L'/') != std::wstring::npos || command.find(L':') != std::wstring::npos;
    const bool has_extension = command_path.has_extension();
    const auto extensions = GetPathextList(env_entries);

    auto try_candidate = [](const std::filesystem::path& candidate) -> std::optional<std::filesystem::path> {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec)) {
            return std::filesystem::absolute(candidate, ec);
        }
        return std::nullopt;
    };

    if (has_directory) {
        if (has_extension) {
            return try_candidate(command_path);
        }
        for (const auto& extension : extensions) {
            if (auto candidate = try_candidate(command_path.wstring() + extension)) {
                return candidate;
            }
        }
        return try_candidate(command_path);
    }

    const std::vector<std::wstring> directories = SplitSemicolonList(GetMergedEnvValue(env_entries, L"PATH"));
    for (const auto& directory : directories) {
        const std::filesystem::path base = std::filesystem::path(directory) / command_path;
        if (has_extension) {
            if (auto candidate = try_candidate(base)) {
                return candidate;
            }
        } else {
            for (const auto& extension : extensions) {
                if (auto candidate = try_candidate(base.wstring() + extension)) {
                    return candidate;
                }
            }
            if (auto candidate = try_candidate(base)) {
                return candidate;
            }
        }
    }

    return std::nullopt;
}

struct LaunchPlan {
    std::wstring application_name;
    std::wstring command_line;
    std::string description;
    std::wstring resolved_command;
};

std::optional<DWORD> TryGetExitCode(HANDLE process_handle) {
    if (!process_handle) {
        return std::nullopt;
    }

    DWORD exit_code = STILL_ACTIVE;
    if (!GetExitCodeProcess(process_handle, &exit_code) || exit_code == STILL_ACTIVE) {
        return std::nullopt;
    }
    return exit_code;
}

std::string DescribeProcessFailure(HANDLE process_handle, const std::string& base_message, const std::string& stderr_text) {
    std::string message = base_message;

    if (const auto exit_code = TryGetExitCode(process_handle)) {
        message += " Exit code: " + std::to_string(*exit_code) + ".";
    }

    const std::string stderr_trimmed = Trim(stderr_text);
    if (!stderr_trimmed.empty()) {
        message += " stderr: " + stderr_trimmed;
    }

    return message;
}

std::optional<std::string> ValidateConfigForLaunch(const McpServerConfig& config) {
    if (Trim(config.name).empty()) {
        return "Server name is required.";
    }
    if (Trim(config.command).empty()) {
        return "Server command is required.";
    }
    if (config.scope == McpServerScope::Shared && !config.variables.empty()) {
        return "Shared MCP servers cannot use project variables.";
    }

    const std::string working_directory = Trim(config.working_directory);
    if (!working_directory.empty()) {
        std::error_code ec;
        const std::filesystem::path path(Utf8ToWide(working_directory));
        if (!std::filesystem::exists(path, ec)) {
            return "Working directory does not exist: " + working_directory;
        }
        if (!std::filesystem::is_directory(path, ec)) {
            return "Working directory is not a folder: " + working_directory;
        }
    }

    return std::nullopt;
}

LaunchPlan BuildLaunchPlan(const McpServerConfig& config) {
    const auto [command, arguments] = NormalizeCommandAndArguments(config);
    const auto resolved = FindExistingCommandPath(command, config.env_entries);
    const std::wstring resolved_command = resolved ? resolved->wstring() : command;
    const std::wstring extension = ToLowerWide(std::filesystem::path(resolved_command).extension().wstring());

    LaunchPlan plan;
    plan.resolved_command = resolved_command;

    std::wstring inner_command = QuoteCommandPart(resolved_command);
    for (const auto& argument : arguments) {
        inner_command += L" ";
        inner_command += QuoteCommandPart(argument);
    }

    if (extension == L".cmd" || extension == L".bat") {
        std::wstring cmd_exe = GetMergedEnvValue(config.env_entries, L"ComSpec");
        if (cmd_exe.empty()) {
            cmd_exe = L"cmd.exe";
        }
        plan.application_name = cmd_exe;
        plan.command_line = QuoteCommandPart(cmd_exe) + L" /d /s /c \"" + inner_command + L"\"";
        plan.description = "Resolved through cmd.exe wrapper";
        return plan;
    }

    if (extension == L".ps1") {
        std::wstring powershell_exe = L"powershell.exe";
        plan.application_name = powershell_exe;
        plan.command_line = QuoteCommandPart(powershell_exe) + L" -NoLogo -NoProfile -ExecutionPolicy Bypass -File " + QuoteCommandPart(resolved_command);
        for (const auto& argument : arguments) {
            plan.command_line += L" ";
            plan.command_line += QuoteCommandPart(argument);
        }
        plan.description = "Resolved through PowerShell wrapper";
        return plan;
    }

    plan.application_name = resolved_command;
    plan.command_line = inner_command;
    plan.description = resolved ? "Resolved executable on PATH" : "Using configured command directly";
    return plan;
}

std::vector<wchar_t> BuildEnvironmentBlock(const std::vector<std::string>& env_entries) {
    if (env_entries.empty()) {
        return {};
    }

    std::map<std::wstring, std::wstring> environment;
    LPWCH current = GetEnvironmentStringsW();
    if (current) {
        for (LPWCH cursor = current; *cursor != L'\0'; cursor += wcslen(cursor) + 1) {
            std::wstring entry(cursor);
            const size_t equals = entry.find(L'=');
            if (equals == std::wstring::npos || equals == 0) {
                continue;
            }
            environment[entry.substr(0, equals)] = entry.substr(equals + 1);
        }
        FreeEnvironmentStringsW(current);
    }

    for (const auto& entry : env_entries) {
        const size_t equals = entry.find('=');
        if (equals == std::string::npos || equals == 0) {
            continue;
        }
        environment[Utf8ToWide(entry.substr(0, equals))] = Utf8ToWide(entry.substr(equals + 1));
    }

    std::vector<wchar_t> block;
    for (const auto& [key, value] : environment) {
        const std::wstring line = key + L"=" + value;
        block.insert(block.end(), line.begin(), line.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

std::string JsonIdToString(const json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<long long>());
    }
    if (value.is_number_unsigned()) {
        return std::to_string(value.get<unsigned long long>());
    }
    return value.dump();
}

std::string SanitizeIdentifier(const std::string& value) {
    std::string output;
    output.reserve(value.size());
    for (char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            output.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        } else {
            output.push_back('_');
        }
    }

    while (!output.empty() && output.front() == '_') {
        output.erase(output.begin());
    }
    while (!output.empty() && output.back() == '_') {
        output.pop_back();
    }
    if (output.empty()) {
        output = "tool";
    }
    return output;
}

std::string HashHex(const std::string& value) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : value) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }

    std::ostringstream stream;
    stream << std::hex << std::setw(8) << std::setfill('0') << static_cast<uint32_t>(hash & 0xffffffffu);
    return stream.str();
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool LooksLikeFilesystemServer(const McpServerConfig& config) {
    const std::string name = LowerAscii(config.name);
    const std::string command = LowerAscii(config.command);
    if (name.find("file-system") != std::string::npos ||
        name.find("filesystem") != std::string::npos ||
        command.find("server-filesystem") != std::string::npos) {
        return true;
    }

    for (const auto& argument : config.arguments) {
        const std::string lowered = LowerAscii(argument);
        if (lowered.find("server-filesystem") != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool LooksLikeWebResearchServer(const McpServerConfig& config) {
    const std::string name = LowerAscii(config.name);
    const std::string command = LowerAscii(config.command);
    if (name.find("duckduckgo") != std::string::npos ||
        name.find("duck duck go") != std::string::npos ||
        name.find("web-search") != std::string::npos ||
        name.find("web search") != std::string::npos ||
        command.find("duckduckgo") != std::string::npos) {
        return true;
    }

    for (const auto& argument : config.arguments) {
        const std::string lowered = LowerAscii(argument);
        if (lowered.find("duckduckgo") != std::string::npos ||
            lowered.find("web-search") != std::string::npos ||
            lowered.find("web_search") != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string WebResearchUsageContextText(bool browser_search_primary) {
    std::string text =
        "Web Research MCP Instructions:\n"
        "- A DuckDuckGo/web research MCP server is available in this chat. Use it when the answer depends on external web pages, current information, online documentation, release notes, command syntax, package behavior, or a user-provided URL/document.\n"
        "- Prefer a web/search tool before guessing about unfamiliar or version-sensitive commands, APIs, model/provider behavior, or documentation-backed details.\n"
        "- If the user provides a URL or asks about a specific online document/page, use the web retrieval/download/fetch-capable MCP tool when available to read the page contents before answering.\n"
        "- If no URL is provided, search first, choose the most relevant official or primary source, then fetch/download/read the result when the exact page content matters.\n"
        "- If DuckDuckGo returns repeated suspicious 'no results' responses with bot-detection or retry-later wording, treat that as temporary throttling. Stop retrying the same broad search, use a known URL/fetch tool or another source, and tell the user web search is temporarily limited.\n"
        "- Summarize findings in your own words and include source URLs when the answer relies on web research.\n"
        "- Do not use web research for purely local codebase facts that can be answered from project files or local tools.";
    if (browser_search_primary) {
        text +=
            "\n- This project marks the built-in browser_web_search tool as the primary web search path. Use DuckDuckGo/web MCP as a fallback, comparison source, or retrieval/download path for known URLs when it is the better fit.";
    }
    return text;
}

std::string AugmentToolDescription(const McpServerConfig& config, const McpToolDefinition& tool) {
    if (!LooksLikeFilesystemServer(config) && !LooksLikeWebResearchServer(config)) {
        return tool.description;
    }

    std::string description = tool.description;
    const std::string tool_name = LowerAscii(tool.name);

    if (LooksLikeFilesystemServer(config) && tool_name == "write_file") {
        if (!description.empty()) {
            description += " ";
        }
        description +=
            "Prefer this tool for small files or for creating an initial scaffold. "
            "For large files, avoid sending the entire final file as one huge content blob unless it is comfortably small. "
            "Instead, create a compact scaffold with stable section markers or placeholders, then use edit_file to fill or revise those sections in smaller passes.";
    } else if (LooksLikeFilesystemServer(config) && tool_name == "edit_file") {
        if (!description.empty()) {
            description += " ";
        }
        description +=
            "Prefer this tool for large-file updates, revisions, or filling sections of an existing scaffold. "
            "When a file is large, use write_file only for a minimal initial scaffold if needed, then use edit_file for targeted replacements so each tool call stays smaller and more reliable.";
    } else if (LooksLikeWebResearchServer(config)) {
        if (!description.empty()) {
            description += " ";
        }
        description +=
            "DuckDuckGo/web research guidance: use this tool when the task depends on external web pages, current facts, online documentation, release notes, package or command behavior, or a user-provided URL/document. ";
        if (tool_name.find("search") != std::string::npos) {
            description +=
                "Use search to find the relevant official or primary source; when exact details matter, follow up with a fetch/download/read-content tool if the server exposes one. If repeated simple searches return a no-results message mentioning bot detection or retrying in a few minutes, stop retrying that search and treat DuckDuckGo as temporarily throttled. ";
        }
        if (tool_name.find("fetch") != std::string::npos ||
            tool_name.find("download") != std::string::npos ||
            tool_name.find("content") != std::string::npos ||
            tool_name.find("read") != std::string::npos) {
            description +=
                "Use this to retrieve the contents of a known URL or search result before answering from that page. ";
        }
        description +=
            "Do not guess documentation-backed details when this tool can retrieve or verify the source.";
    }

    return description;
}

std::string BuildToolAlias(const std::string& server_id, const std::string& tool_name) {
    const std::string prefix = "mcp_" + SanitizeIdentifier(server_id).substr(0, 18) + "_" + SanitizeIdentifier(tool_name).substr(0, 18);
    return prefix + "_" + HashHex(server_id + "::" + tool_name);
}

std::string BuildConnectionKey(const McpServerConfig& config,
                               const std::string& project_id,
                               const std::vector<ProjectMcpVariableValue>& runtime_variables = {}) {
    if (config.scope == McpServerScope::Shared) {
        return "shared::" + config.id;
    }
    std::string key = "project::" + project_id + "::" + config.id;
    const std::string runtime_key = variable_resolver::BuildScopeKey(runtime_variables);
    if (!runtime_key.empty()) {
        key += "::runtime::" + runtime_key;
    }
    return key;
}

std::optional<std::string> FindBindingValue(const ProjectMcpServerBinding& binding, const std::string& name) {
    const auto it = std::find_if(binding.variables.begin(), binding.variables.end(), [&](const ProjectMcpVariableValue& variable) { return variable.name == name; });
    if (it == binding.variables.end()) {
        return std::nullopt;
    }
    return it->value;
}

std::optional<std::string> FindVariableValue(const std::vector<ProjectMcpVariableValue>& variables, const std::string& name) {
    const auto it = std::find_if(variables.begin(), variables.end(), [&](const ProjectMcpVariableValue& variable) { return variable.name == name; });
    if (it == variables.end()) {
        return std::nullopt;
    }
    return it->value;
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

void UpsertVariableValue(std::vector<ProjectMcpVariableValue>& values,
                         const std::string& name,
                         const std::string& value) {
    auto it = std::find_if(values.begin(), values.end(), [&](const ProjectMcpVariableValue& variable) { return variable.name == name; });
    if (it != values.end()) {
        it->value = value;
    } else {
        values.push_back({name, value});
    }
}

std::string ApplyVariableValues(std::string text, const std::vector<ProjectMcpVariableValue>& values) {
    for (const auto& variable : values) {
        if (variable.name.empty()) continue;
        ReplaceAll(text, "$<" + variable.name + ">$", variable.value);
        ReplaceAll(text, "$" + variable.name + "$", variable.value);
        ReplaceAll(text, "$<" + variable.name + ">", variable.value);
    }
    return text;
}

// Substitute $<VarName> tokens (no trailing '$') from project-level variables.
// Deliberately skips any match that is immediately followed by '$', which would
// indicate an MCP binding placeholder ($<Name>$) that was not resolved — we
// leave those in place rather than producing partially-replaced garbage.
std::string ApplyProjectVariables(std::string text, const std::vector<ProjectMcpVariableValue>& project_vars) {
    for (const auto& pv : project_vars) {
        if (pv.name.empty()) continue;
        const std::string ph = "$<" + pv.name + ">";
        std::string::size_type pos = 0;
        while ((pos = text.find(ph, pos)) != std::string::npos) {
            const size_t after = pos + ph.size();
            if (after < text.size() && text[after] == '$') {
                ++pos;  // skip — this is a $<Name>$ binding placeholder
                continue;
            }
            text.replace(pos, ph.size(), pv.value);
            pos += pv.value.size();
        }
    }
    return text;
}

bool TextUsesVariable(const std::string& text, const std::string& name) {
    return text.find("$" + name + "$") != std::string::npos ||
           text.find("$" + name + "_$") != std::string::npos ||
           text.find("$<" + name + ">$") != std::string::npos ||
           text.find("$<" + name + "_>$") != std::string::npos ||
           text.find("$<" + name + ">") != std::string::npos ||
           text.find("$<" + name + "_>") != std::string::npos;
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

std::vector<McpServerVariable> UsedGlobalVariables(const McpServerConfig& config, const std::vector<McpServerVariable>& global_variables) {
    std::vector<McpServerVariable> variables;
    for (const auto& variable : global_variables) {
        if (!variable.name.empty() && ConfigUsesVariable(config, variable.name)) {
            variables.push_back(variable);
        }
    }
    return variables;
}

bool ConfigUsesGlobalVariables(const McpServerConfig& config, const std::vector<McpServerVariable>& global_variables) {
    return !UsedGlobalVariables(config, global_variables).empty();
}

std::string JsonPretty(const json& value) {
    return value.dump(2);
}

std::string FormatContentItem(const json& item) {
    if (!item.is_object()) {
        return item.dump();
    }

    const std::string type = item.value("type", "");
    if (type == "text") {
        return item.value("text", "");
    }
    if (type == "image") {
        std::string text = "[image";
        if (item.contains("mimeType")) {
            text += " " + item.value("mimeType", "");
        }
        if (item.contains("data")) {
            text += " data=" + std::to_string(item.value("data", std::string{}).size()) + "b";
        }
        text += "]";
        return text;
    }
    if (type == "audio") {
        std::string text = "[audio";
        if (item.contains("mimeType")) {
            text += " " + item.value("mimeType", "");
        }
        text += "]";
        return text;
    }
    if (type == "resource_link" || type == "resource") {
        return "[resource] " + item.value("uri", item.value("name", std::string{}));
    }
    if (type == "embedded_resource") {
        if (item.contains("resource")) {
            return "[embedded resource]\n" + JsonPretty(item["resource"]);
        }
        return "[embedded resource]";
    }
    return item.dump(2);
}

std::string FormatToolResultText(const json& result) {
    std::vector<std::string> parts;

    if (result.contains("content") && result["content"].is_array()) {
        for (const auto& item : result["content"]) {
            const std::string text = FormatContentItem(item);
            if (!text.empty()) {
                parts.push_back(text);
            }
        }
    }

    if (result.contains("structuredContent")) {
        parts.push_back("Structured content:\n" + JsonPretty(result["structuredContent"]));
    }

    if (parts.empty()) {
        return result.dump(2);
    }

    std::ostringstream stream;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            stream << "\n\n";
        }
        stream << parts[i];
    }
    return stream.str();
}

bool ToolNameLooksLikeSearch(const std::string& tool_name) {
    const std::string lowered = LowerAscii(tool_name);
    return lowered.find("search") != std::string::npos ||
           lowered.find("duckduckgo") != std::string::npos ||
           lowered.find("duck_duck_go") != std::string::npos;
}

std::string NormalizeSearchQuery(std::string query) {
    query = Trim(std::move(query));
    std::string normalized;
    normalized.reserve(query.size());
    bool last_space = false;
    for (const unsigned char ch : query) {
        if (std::isspace(ch)) {
            if (!last_space) {
                normalized.push_back(' ');
                last_space = true;
            }
        } else {
            normalized.push_back(static_cast<char>(std::tolower(ch)));
            last_space = false;
        }
    }
    return Trim(std::move(normalized));
}

std::string ExtractSearchQueryFromArguments(const std::string& arguments_json) {
    if (Trim(arguments_json).empty()) {
        return "";
    }
    try {
        const json arguments = json::parse(arguments_json);
        if (!arguments.is_object()) {
            return "";
        }

        const std::array<std::string, 8> preferred_keys = {
            "query", "q", "keywords", "search", "search_query",
            "search_terms", "term", "text"
        };
        for (const auto& key : preferred_keys) {
            const auto it = arguments.find(key);
            if (it != arguments.end() && it->is_string()) {
                return NormalizeSearchQuery(it->get<std::string>());
            }
        }
    } catch (...) {
    }
    return "";
}

bool LooksLikeDuckDuckGoBlockedNoResults(const McpToolCallResult& result) {
    const std::string combined = LowerAscii(result.content_text + "\n" + result.raw_result_json);
    const bool no_results =
        combined.find("no results were found for your search query") != std::string::npos;
    const bool throttle_hint =
        combined.find("bot detection") != std::string::npos ||
        combined.find("try again in a few minutes") != std::string::npos ||
        combined.find("rate limit") != std::string::npos ||
        combined.find("too many requests") != std::string::npos;
    return no_results && throttle_hint;
}

std::string BuildWebResearchGuardMessage(const std::string& query, int consecutive_count) {
    std::ostringstream message;
    message << "DuckDuckGo/web search appears to be temporarily throttled or blocked. "
            << "The MCP server returned the same suspicious no-results/bot-detection response "
            << consecutive_count << " time(s)";
    if (!query.empty()) {
        message << " for query \"" << query << "\"";
    }
    message << ". Stop retrying this DuckDuckGo search for a few minutes. "
            << "Use a different source, use a known URL fetch/download tool, narrow to an official site URL, "
            << "or ask the user before continuing broad web research.";
    return message.str();
}

bool EquivalentVariables(const std::vector<McpServerVariable>& left, const std::vector<McpServerVariable>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (size_t i = 0; i < left.size(); ++i) {
        if (left[i].name != right[i].name ||
            left[i].description != right[i].description ||
            left[i].kind != right[i].kind ||
            left[i].inject_into_context != right[i].inject_into_context) {
            return false;
        }
    }
    return true;
}

bool EquivalentConfig(const McpServerConfig& left, const McpServerConfig& right) {
    return left.id == right.id &&
           left.name == right.name &&
           left.command == right.command &&
           left.arguments == right.arguments &&
           left.working_directory == right.working_directory &&
           left.env_entries == right.env_entries &&
           left.scope == right.scope &&
           EquivalentVariables(left.variables, right.variables) &&
           left.enabled == right.enabled &&
           left.auto_connect == right.auto_connect;
}

void CloseIfValid(HANDLE& handle) {
    if (handle) {
        CloseHandle(handle);
        handle = nullptr;
    }
}

std::string ReadAvailablePipeText(HANDLE pipe) {
    std::string output;
    if (!pipe) {
        return output;
    }

    while (true) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || available == 0) {
            break;
        }

        std::vector<char> buffer(static_cast<size_t>(available));
        DWORD read = 0;
        if (!ReadFile(pipe, buffer.data(), available, &read, nullptr) || read == 0) {
            break;
        }
        output.append(buffer.data(), buffer.data() + read);
    }

    return output;
}

std::optional<json> WaitForJsonResponse(
    HANDLE stdout_pipe,
    HANDLE stderr_pipe,
    const std::string& expected_id,
    std::string& stdout_capture,
    std::string& stderr_capture,
    std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        stdout_capture += ReadAvailablePipeText(stdout_pipe);
        stderr_capture += ReadAvailablePipeText(stderr_pipe);

        size_t newline = std::string::npos;
        while ((newline = stdout_capture.find('\n')) != std::string::npos) {
            std::string line = stdout_capture.substr(0, newline);
            stdout_capture.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (Trim(line).empty()) {
                continue;
            }

            try {
                json parsed = json::parse(line);
                if (parsed.contains("id") && JsonIdToString(parsed["id"]) == expected_id) {
                    return parsed;
                }
            } catch (...) {
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    stdout_capture += ReadAvailablePipeText(stdout_pipe);
    stderr_capture += ReadAvailablePipeText(stderr_pipe);
    return std::nullopt;
}
}  // namespace

class McpManager::Connection {
public:
    Connection(std::string server_id, std::string project_id, McpServerConfig config, std::function<void()> on_state_changed)
        : server_id_(std::move(server_id)),
          project_id_(std::move(project_id)),
          config_(std::move(config)),
          on_state_changed_(std::move(on_state_changed)) {}

    ~Connection() {
        Disconnect();
    }

    const std::string& server_id() const {
        return server_id_;
    }

    const std::string& project_id() const {
        return project_id_;
    }

    McpServerSnapshot Snapshot() const {
        std::scoped_lock lock(mutex_);
        McpServerSnapshot snapshot;
        snapshot.config = config_;
        snapshot.status = status_;
        snapshot.last_error = last_error_;
        snapshot.tools = tools_;
        return snapshot;
    }

    std::vector<McpToolDefinition> Tools() const {
        std::scoped_lock lock(mutex_);
        return tools_;
    }

    bool Connect(std::string* error);
    void Disconnect();
    bool RefreshTools(std::string* error);
    McpToolCallResult CallTool(const std::string& tool_name, const std::string& arguments_json) const;

private:
    struct PendingRequest {
        std::mutex mutex;
        std::condition_variable cv;
        bool ready = false;
        json response;
    };

    void SetStatus(McpServerStatus status, const std::string& error = std::string());
    void WriteJsonLine(const json& message) const;
    json SendRequest(const std::string& method, const json& params, std::chrono::milliseconds timeout) const;
    void SendNotification(const std::string& method, const json& params) const;
    void SendResponse(const json& id, const json& result) const;
    void SendError(const json& id, int code, const std::string& message) const;
    void ReadStdoutLoop();
    void ReadStderrLoop();
    void HandleIncomingMessage(const json& message);
    void HandleNotification(const json& message);
    void HandleRequest(const json& message);
    void FailPendingRequests(const std::string& message) const;

    std::string server_id_;
    std::string project_id_;
    McpServerConfig config_;
    mutable std::mutex mutex_;
    mutable std::mutex write_mutex_;
    mutable std::mutex pending_mutex_;
    mutable std::unordered_map<std::string, std::shared_ptr<PendingRequest>> pending_requests_;
    std::function<void()> on_state_changed_;

    HANDLE process_handle_ = nullptr;
    HANDLE stdin_write_ = nullptr;
    HANDLE stdout_read_ = nullptr;
    HANDLE stderr_read_ = nullptr;
    std::thread stdout_thread_;
    std::thread stderr_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> refresh_in_progress_{false};
    mutable std::atomic<unsigned long long> next_request_id_{1};
    McpServerStatus status_ = McpServerStatus::Disconnected;
    std::string last_error_;
    std::vector<McpToolDefinition> tools_;
};

void McpManager::Connection::SetStatus(McpServerStatus status, const std::string& error) {
    {
        std::scoped_lock lock(mutex_);
        status_ = status;
        if (!error.empty() || status != McpServerStatus::Ready) {
            last_error_ = error;
        }
    }
    if (on_state_changed_) {
        on_state_changed_();
    }
}

void McpManager::Connection::WriteJsonLine(const json& message) const {
    const std::string line = message.dump() + "\n";
    std::scoped_lock lock(write_mutex_);

    if (!stdin_write_) {
        throw std::runtime_error("MCP server stdin is not available.");
    }

    DWORD written = 0;
    if (!WriteFile(stdin_write_, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) || written != line.size()) {
        throw std::runtime_error("Failed to write to the MCP server process.");
    }
}

json McpManager::Connection::SendRequest(const std::string& method, const json& params, std::chrono::milliseconds timeout) const {
    const std::string request_id = std::to_string(next_request_id_.fetch_add(1));
    auto pending = std::make_shared<PendingRequest>();

    {
        std::scoped_lock lock(pending_mutex_);
        pending_requests_[request_id] = pending;
    }

    json request{
        {"jsonrpc", "2.0"},
        {"id", request_id},
        {"method", method},
        {"params", params},
    };

    try {
        WriteJsonLine(request);
    } catch (...) {
        std::scoped_lock lock(pending_mutex_);
        pending_requests_.erase(request_id);
        throw;
    }

    std::unique_lock lock(pending->mutex);
    if (!pending->cv.wait_for(lock, timeout, [&pending] { return pending->ready; })) {
        std::scoped_lock pending_lock(pending_mutex_);
        pending_requests_.erase(request_id);
        throw std::runtime_error("Timed out waiting for MCP response to " + method + ".");
    }

    {
        std::scoped_lock pending_lock(pending_mutex_);
        pending_requests_.erase(request_id);
    }

    if (pending->response.contains("error")) {
        const auto& error = pending->response["error"];
        throw std::runtime_error(error.value("message", "Unknown MCP error."));
    }

    return pending->response.value("result", json::object());
}

void McpManager::Connection::SendNotification(const std::string& method, const json& params) const {
    json notification{
        {"jsonrpc", "2.0"},
        {"method", method},
    };
    if (!params.is_null()) {
        notification["params"] = params;
    }
    WriteJsonLine(notification);
}

void McpManager::Connection::SendResponse(const json& id, const json& result) const {
    json response{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result},
    };
    WriteJsonLine(response);
}

void McpManager::Connection::SendError(const json& id, int code, const std::string& message) const {
    json response{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {
            {"code", code},
            {"message", message},
        }},
    };
    WriteJsonLine(response);
}

void McpManager::Connection::FailPendingRequests(const std::string& message) const {
    std::vector<std::shared_ptr<PendingRequest>> pending;
    {
        std::scoped_lock lock(pending_mutex_);
        for (const auto& [_, request] : pending_requests_) {
            pending.push_back(request);
        }
        pending_requests_.clear();
    }

    for (auto& request : pending) {
        std::scoped_lock request_lock(request->mutex);
        request->ready = true;
        request->response = json{
            {"jsonrpc", "2.0"},
            {"error", {
                {"code", -32000},
                {"message", message},
            }},
        };
        request->cv.notify_all();
    }
}

bool McpManager::Connection::Connect(std::string* error) {
    if (Snapshot().status == McpServerStatus::Ready) {
        return true;
    }

    if (const auto validation_error = ValidateConfigForLaunch(config_)) {
        if (error) {
            *error = *validation_error;
        }
        SetStatus(McpServerStatus::Error, *validation_error);
        return false;
    }

    Disconnect();
    SetStatus(McpServerStatus::Connecting);

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE stdin_read = nullptr;
    HANDLE stdout_write = nullptr;
    HANDLE stderr_write = nullptr;

    if (!CreatePipe(&stdin_read, &stdin_write_, &security, 0) ||
        !CreatePipe(&stdout_read_, &stdout_write, &security, 0) ||
        !CreatePipe(&stderr_read_, &stderr_write, &security, 0)) {
        if (error) {
            *error = "Failed to create stdio pipes for the MCP server.";
        }
        Disconnect();
        SetStatus(McpServerStatus::Error, error ? *error : "Failed to create stdio pipes.");
        return false;
    }

    SetHandleInformation(stdin_write_, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdout_read_, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderr_read_, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdin_read;
    startup.hStdOutput = stdout_write;
    startup.hStdError = stderr_write;

    PROCESS_INFORMATION process_info{};
    LaunchPlan launch_plan = BuildLaunchPlan(config_);
    std::wstring command_line = launch_plan.command_line;
    std::wstring working_directory = Utf8ToWide(config_.working_directory);
    std::vector<wchar_t> environment_block = BuildEnvironmentBlock(config_.env_entries);

    BOOL created = CreateProcessW(
        launch_plan.application_name.empty() ? nullptr : launch_plan.application_name.c_str(),
        command_line.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW | (environment_block.empty() ? 0 : CREATE_UNICODE_ENVIRONMENT),
        environment_block.empty() ? nullptr : environment_block.data(),
        working_directory.empty() ? nullptr : working_directory.c_str(),
        &startup,
        &process_info);

    CloseHandle(stdin_read);
    CloseHandle(stdout_write);
    CloseHandle(stderr_write);

    if (!created) {
        std::ostringstream failure;
        failure << "Failed to launch MCP server process";
        if (!config_.command.empty()) {
            failure << " for command \"" << config_.command << "\"";
        }
        failure << ".";
        if (error) {
            *error = failure.str();
        }
        Disconnect();
        SetStatus(McpServerStatus::Error, failure.str());
        return false;
    }

    process_handle_ = process_info.hProcess;
    CloseHandle(process_info.hThread);

    running_.store(true);
    stdout_thread_ = std::thread([this]() { ReadStdoutLoop(); });
    stderr_thread_ = std::thread([this]() { ReadStderrLoop(); });

    try {
        const json result = SendRequest(
            "initialize",
            json{
                {"protocolVersion", "2025-11-25"},
                {"capabilities", json::object()},
                {"clientInfo", {
                    {"name", "agent-desktop"},
                    {"title", "AI Agent Desktop"},
                    {"version", "0.2.0"},
                }},
            },
            std::chrono::duration_cast<std::chrono::milliseconds>(kRequestTimeout));

        const std::string negotiated = result.value("protocolVersion", "");
        if (!negotiated.empty() && negotiated != "2025-11-25") {
            throw std::runtime_error("Unsupported MCP protocol version negotiated by the server: " + negotiated);
        }

        SendNotification("notifications/initialized", json::object());

        std::string refresh_error;
        if (!RefreshTools(&refresh_error)) {
            throw std::runtime_error(refresh_error.empty() ? "Failed to load MCP tools." : refresh_error);
        }

        SetStatus(McpServerStatus::Ready);
        return true;
    } catch (const std::exception& ex) {
        if (error) {
            *error = ex.what();
        }
        Disconnect();
        SetStatus(McpServerStatus::Error, ex.what());
        return false;
    }
}

void McpManager::Connection::Disconnect() {
    running_.store(false);

    if (stdin_write_) {
        CloseHandle(stdin_write_);
        stdin_write_ = nullptr;
    }
    if (stdout_read_) {
        CloseHandle(stdout_read_);
        stdout_read_ = nullptr;
    }
    if (stderr_read_) {
        CloseHandle(stderr_read_);
        stderr_read_ = nullptr;
    }

    if (process_handle_) {
        if (WaitForSingleObject(process_handle_, 750) == WAIT_TIMEOUT) {
            TerminateProcess(process_handle_, 0);
            WaitForSingleObject(process_handle_, 1500);
        }
        CloseHandle(process_handle_);
        process_handle_ = nullptr;
    }

    if (stdout_thread_.joinable()) {
        stdout_thread_.join();
    }
    if (stderr_thread_.joinable()) {
        stderr_thread_.join();
    }

    FailPendingRequests("The MCP server disconnected.");

    {
        std::scoped_lock lock(mutex_);
        tools_.clear();
        if (status_ != McpServerStatus::Error) {
            status_ = McpServerStatus::Disconnected;
        }
    }
    if (on_state_changed_) {
        on_state_changed_();
    }
}

bool McpManager::Connection::RefreshTools(std::string* error) {
    try {
        std::vector<McpToolDefinition> tools;
        std::optional<std::string> cursor;

        do {
            json params = json::object();
            if (cursor && !cursor->empty()) {
                params["cursor"] = *cursor;
            }

            const json result = SendRequest("tools/list", params, std::chrono::duration_cast<std::chrono::milliseconds>(kRequestTimeout));
            if (result.contains("tools") && result["tools"].is_array()) {
                for (const auto& item : result["tools"]) {
                    McpToolDefinition tool;
                    tool.name = item.value("name", "");
                    tool.title = item.value("title", "");
                    tool.description = item.value("description", "");
                    if (item.contains("inputSchema")) {
                        tool.input_schema_json = item["inputSchema"].dump();
                    } else {
                        tool.input_schema_json = json{
                            {"type", "object"},
                            {"properties", json::object()},
                        }.dump();
                    }
                    tools.push_back(std::move(tool));
                }
            }

            if (result.contains("nextCursor") && result["nextCursor"].is_string()) {
                cursor = result["nextCursor"].get<std::string>();
            } else {
                cursor.reset();
            }
        } while (cursor.has_value());

        {
            std::scoped_lock lock(mutex_);
            tools_ = std::move(tools);
            if (status_ != McpServerStatus::Error) {
                status_ = McpServerStatus::Ready;
                last_error_.clear();
            }
        }
        if (on_state_changed_) {
            on_state_changed_();
        }
        return true;
    } catch (const std::exception& ex) {
        if (error) {
            *error = ex.what();
        }
        SetStatus(McpServerStatus::Error, ex.what());
        return false;
    }
}

McpToolCallResult McpManager::Connection::CallTool(const std::string& tool_name, const std::string& arguments_json) const {
    McpToolCallResult result;
    try {
        json arguments = json::object();
        if (!Trim(arguments_json).empty()) {
            arguments = json::parse(arguments_json);
        }

        const json call_result = SendRequest(
            "tools/call",
            json{
                {"name", tool_name},
                {"arguments", arguments},
            },
            std::chrono::duration_cast<std::chrono::milliseconds>(kRequestTimeout));

        result.success = !call_result.value("isError", false);
        result.is_tool_error = call_result.value("isError", false);
        result.content_text = FormatToolResultText(call_result);
        result.raw_result_json = call_result.dump(2);
    } catch (const std::exception& ex) {
        result.success = false;
        result.is_tool_error = true;
        result.content_text = ex.what();
        result.raw_result_json = json{{"error", ex.what()}}.dump(2);
    }

    return result;
}

void McpManager::Connection::ReadStdoutLoop() {
    std::string buffer;
    char chunk[4096];

    while (running_.load()) {
        DWORD read = 0;
        if (!ReadFile(stdout_read_, chunk, sizeof(chunk), &read, nullptr) || read == 0) {
            break;
        }

        buffer.append(chunk, chunk + read);

        size_t newline = std::string::npos;
        while ((newline = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, newline);
            buffer.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (Trim(line).empty()) {
                continue;
            }

            try {
                HandleIncomingMessage(json::parse(line));
            } catch (...) {
                SetStatus(McpServerStatus::Error, "Received invalid JSON from the MCP server.");
            }
        }
    }

    const bool was_running = running_.exchange(false);
    if (!was_running) {
        return;
    }

    std::string stderr_text;
    {
        std::scoped_lock lock(mutex_);
        stderr_text = last_error_;
    }
    const std::string message = DescribeProcessFailure(process_handle_, "The MCP server closed its output stream.", stderr_text);
    FailPendingRequests(message);
    SetStatus(McpServerStatus::Error, message);
}

void McpManager::Connection::ReadStderrLoop() {
    std::string buffer;
    char chunk[2048];

    while (running_.load()) {
        DWORD read = 0;
        if (!ReadFile(stderr_read_, chunk, sizeof(chunk), &read, nullptr) || read == 0) {
            break;
        }

        buffer.append(chunk, chunk + read);
        size_t newline = std::string::npos;
        while ((newline = buffer.find('\n')) != std::string::npos) {
            std::string line = Trim(buffer.substr(0, newline));
            buffer.erase(0, newline + 1);
            if (!line.empty()) {
                std::scoped_lock lock(mutex_);
                last_error_ = line;
            }
        }
    }
}

void McpManager::Connection::HandleIncomingMessage(const json& message) {
    if (message.contains("id") && (message.contains("result") || message.contains("error"))) {
        const std::string response_id = JsonIdToString(message["id"]);
        std::shared_ptr<PendingRequest> pending;
        {
            std::scoped_lock lock(pending_mutex_);
            const auto it = pending_requests_.find(response_id);
            if (it != pending_requests_.end()) {
                pending = it->second;
            }
        }

        if (pending) {
            std::scoped_lock lock(pending->mutex);
            pending->response = message;
            pending->ready = true;
            pending->cv.notify_all();
        }
        return;
    }

    if (message.contains("method") && message.contains("id")) {
        HandleRequest(message);
        return;
    }

    if (message.contains("method")) {
        HandleNotification(message);
    }
}

void McpManager::Connection::HandleNotification(const json& message) {
    const std::string method = message.value("method", "");
    if (method == "notifications/tools/list_changed") {
        if (!refresh_in_progress_.exchange(true)) {
            std::thread([this]() {
                std::string ignored;
                RefreshTools(&ignored);
                refresh_in_progress_.store(false);
            }).detach();
        }
    } else if (method == "notifications/message" && message.contains("params")) {
        std::string log_line;
        const auto& params = message["params"];
        if (params.contains("message")) {
            log_line = params.value("message", "");
        } else if (params.contains("data")) {
            log_line = params["data"].dump();
        }
        if (!log_line.empty()) {
            std::scoped_lock lock(mutex_);
            last_error_ = log_line;
        }
        if (on_state_changed_) {
            on_state_changed_();
        }
    }
}

void McpManager::Connection::HandleRequest(const json& message) {
    const std::string method = message.value("method", "");
    const json id = message["id"];

    try {
        if (method == "ping") {
            SendResponse(id, json::object());
        } else {
            SendError(id, -32601, "Method not supported by this Phase 2 client.");
        }
    } catch (...) {
    }
}

struct WebResearchGuardState {
    int consecutive_suspicious_empty_results = 0;
    std::chrono::steady_clock::time_point cooldown_until{};
};

struct McpManager::Impl {
    mutable std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<Connection>> connections;
    mutable std::unordered_map<std::string, std::vector<ProjectMcpServerBinding>> bindings_by_project;
    mutable std::unordered_map<std::string, WebResearchGuardState> web_research_guards;
};

McpManager::McpManager(AppStorage* storage) : storage_(storage), impl_(std::make_unique<Impl>()) {}

McpManager::~McpManager() {
    std::vector<std::shared_ptr<Connection>> connections;
    {
        std::scoped_lock lock(impl_->mutex);
        for (const auto& [_, connection] : impl_->connections) {
            connections.push_back(connection);
        }
        impl_->connections.clear();
    }

    for (auto& connection : connections) {
        connection->Disconnect();
    }
}

void McpManager::Load() {
    configs_ = storage_->LoadMcpServers();
    global_variables_ = storage_->LoadMcpGlobalVariables();
    bool changed = false;
    auto ensure_global_variable = [&](const char* name,
                                      const char* description,
                                      McpVariableKind kind,
                                      bool inject_into_context) {
        const auto exists = std::find_if(
            global_variables_.begin(),
            global_variables_.end(),
            [&](const McpServerVariable& variable) {
                return variable_resolver::ToLookupKey(variable.name) ==
                       variable_resolver::ToLookupKey(name);
            }) != global_variables_.end();
        if (exists) return;
        McpServerVariable variable;
        variable.name = name;
        variable.description = description;
        variable.kind = kind;
        variable.inject_into_context = inject_into_context;
        global_variables_.push_back(std::move(variable));
        changed = true;
    };
    ensure_global_variable(
        "ProjectFolder",
        "Project folder where the project files are located.",
        McpVariableKind::Folder,
        true);
    const auto before_remove = global_variables_.size();
    global_variables_.erase(
        std::remove_if(global_variables_.begin(), global_variables_.end(),
            [](const McpServerVariable& variable) {
                return variable_resolver::ToLookupKey(variable.name) == "userfolder" &&
                       variable.description.find("Per-chat user workspace folder. Defaults") != std::string::npos;
            }),
        global_variables_.end());
    if (global_variables_.size() != before_remove) {
        changed = true;
    }
    if (changed) {
        storage_->SaveMcpConfiguration(configs_, global_variables_);
    }
    NotifyStateChanged();
}

const std::vector<McpServerConfig>& McpManager::configs() const {
    return configs_;
}

const std::vector<McpServerVariable>& McpManager::global_variables() const {
    return global_variables_;
}

void McpManager::SaveConfigs(const std::vector<McpServerConfig>& configs) {
    std::vector<std::shared_ptr<Connection>> disconnect_list;
    {
        std::scoped_lock lock(impl_->mutex);
        std::vector<std::string> erase_keys;
        for (const auto& [connection_key, connection] : impl_->connections) {
            const auto it = std::find_if(configs.begin(), configs.end(), [&](const McpServerConfig& config) { return config.id == connection->server_id(); });
            if (it == configs.end()) {
                disconnect_list.push_back(connection);
                erase_keys.push_back(connection_key);
                continue;
            }

            const auto existing = std::find_if(configs_.begin(), configs_.end(), [&](const McpServerConfig& config) { return config.id == connection->server_id(); });
            if (existing == configs_.end() || !EquivalentConfig(*existing, *it)) {
                disconnect_list.push_back(connection);
                erase_keys.push_back(connection_key);
            }
        }

        for (const auto& key : erase_keys) {
            impl_->connections.erase(key);
        }
    }

    for (auto& connection : disconnect_list) {
        connection->Disconnect();
    }

    configs_ = configs;
    storage_->SaveMcpConfiguration(configs_, global_variables_);
    NotifyStateChanged();
}

void McpManager::SaveGlobalVariables(const std::vector<McpServerVariable>& variables) {
    std::vector<std::shared_ptr<Connection>> disconnect_list;
    {
        std::scoped_lock lock(impl_->mutex);
        for (auto it = impl_->connections.begin(); it != impl_->connections.end();) {
            const auto config_it = std::find_if(configs_.begin(), configs_.end(), [&](const McpServerConfig& config) { return config.id == it->second->server_id(); });
            if (config_it != configs_.end() && config_it->scope == McpServerScope::PerProject) {
                disconnect_list.push_back(it->second);
                it = impl_->connections.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (auto& connection : disconnect_list) {
        connection->Disconnect();
    }

    global_variables_ = variables;
    storage_->SaveMcpConfiguration(configs_, global_variables_);
    NotifyStateChanged();
}

void McpManager::SetStateChangedCallback(StateChangedCallback callback) {
    state_changed_callback_ = std::move(callback);
}

std::vector<ProjectMcpServerBinding> McpManager::LoadProjectBindings(const std::string& project_id) const {
    if (project_id.empty()) {
        return {};
    }

    std::scoped_lock lock(impl_->mutex);
    auto it = impl_->bindings_by_project.find(project_id);
    if (it == impl_->bindings_by_project.end()) {
        it = impl_->bindings_by_project.emplace(project_id, storage_->LoadProjectMcpBindings(project_id)).first;
    }
    return it->second;
}

std::vector<ProjectMcpServerBinding> McpManager::GetProjectBindings(const std::string& project_id) const {
    return LoadProjectBindings(project_id);
}

void McpManager::SaveProjectBindings(const std::string& project_id, const std::vector<ProjectMcpServerBinding>& bindings) {
    if (project_id.empty()) {
        return;
    }

    std::vector<std::shared_ptr<Connection>> disconnect_list;
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->bindings_by_project[project_id] = bindings;
        for (auto it = impl_->connections.begin(); it != impl_->connections.end();) {
            const auto config_it = std::find_if(configs_.begin(), configs_.end(), [&](const McpServerConfig& config) { return config.id == it->second->server_id(); });
            if (config_it != configs_.end() &&
                config_it->scope == McpServerScope::PerProject &&
                it->second->project_id() == project_id) {
                disconnect_list.push_back(it->second);
                it = impl_->connections.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (auto& connection : disconnect_list) {
        connection->Disconnect();
    }

    storage_->SaveProjectMcpBindings(project_id, bindings);
    NotifyStateChanged();
}

bool McpManager::IsServerSelectedForProject(const std::string& project_id, const std::string& server_id) const {
    if (project_id.empty()) {
        return false;
    }

    const auto bindings = LoadProjectBindings(project_id);
    return std::any_of(bindings.begin(), bindings.end(), [&](const ProjectMcpServerBinding& binding) { return binding.server_id == server_id; });
}

std::optional<ProjectMcpServerBinding> McpManager::FindProjectBinding(const std::string& project_id, const std::string& server_id) const {
    const auto bindings = LoadProjectBindings(project_id);
    const auto it = std::find_if(bindings.begin(), bindings.end(), [&](const ProjectMcpServerBinding& binding) { return binding.server_id == server_id; });
    if (it == bindings.end()) {
        return std::nullopt;
    }
    return *it;
}

std::optional<McpServerConfig> McpManager::ResolveConfigForProject(
    const McpServerConfig& config,
    const std::string& project_id,
    std::string* error,
    const std::vector<ProjectMcpVariableValue>& runtime_variables) const {
    McpServerConfig resolved = config;

    if (config.scope == McpServerScope::PerProject && project_id.empty()) {
        if (error) {
            *error = "This MCP server requires an active project.";
        }
        return std::nullopt;
    }

    if (config.scope == McpServerScope::Shared) {
        return resolved;
    }

    const auto binding = FindProjectBinding(project_id, config.id);
    if (!binding) {
        if (error) {
            *error = "This MCP server is not configured for the active project.";
        }
        return std::nullopt;
    }

    std::vector<ProjectMcpVariableValue> values;
    for (const auto& variable : binding->variables) {
        variable_resolver::UpsertValue(values, variable.name, variable.value);
    }

    ProjectSettings project_settings;
    if (storage_ && !project_id.empty()) {
        project_settings = storage_->LoadProjectSettings(project_id);
        for (const auto& variable : project_settings.project_variables) {
            variable_resolver::UpsertValue(values, variable);
        }
    }

    if (!project_settings.project_name.empty()) {
        variable_resolver::UpsertValue(values, "PROJECTNAME", project_settings.project_name);
    }
    for (const auto& variable : runtime_variables) {
        variable_resolver::UpsertValue(values, variable.name, variable.value);
    }
    const auto resolved_values = variable_resolver::ResolveValues(values);
    std::vector<McpServerVariable> folder_definitions =
        variable_resolver::MergeDefinitions(global_variables_, config.variables);
    variable_resolver::EnsureFolderVariables(resolved_values, folder_definitions);

    resolved.command = variable_resolver::ExpandTemplate(resolved.command, resolved_values);
    resolved.working_directory = variable_resolver::ExpandTemplate(resolved.working_directory, resolved_values);
    for (auto& argument : resolved.arguments) {
        argument = variable_resolver::ExpandTemplate(argument, resolved_values);
    }
    for (auto& entry : resolved.env_entries) {
        entry = variable_resolver::ExpandTemplate(entry, resolved_values);
    }

    // If the configured working directory is an expanded folder path, make sure
    // it exists before CreateProcessW validates it.
    if (!Trim(resolved.working_directory).empty()) {
        std::error_code ec;
        std::filesystem::create_directories(
            std::filesystem::path(Utf8ToWide(resolved.working_directory)), ec);
        if (ec && error) {
            *error = "Could not create MCP working directory: " + resolved.working_directory;
            return std::nullopt;
        }
        ec.clear();
        const std::filesystem::path working_dir(Utf8ToWide(resolved.working_directory));
        if (!std::filesystem::is_directory(working_dir, ec)) {
            if (error) {
                *error = "Working directory is not a folder: " + resolved.working_directory;
            }
            return std::nullopt;
        }
    }

    return resolved;
}

std::vector<McpServerSnapshot> McpManager::GetServerSnapshots(const std::string& project_id) const {
    std::vector<McpServerSnapshot> snapshots;
    std::unordered_map<std::string, std::shared_ptr<Connection>> connections;
    {
        std::scoped_lock lock(impl_->mutex);
        connections = impl_->connections;
    }

    for (const auto& config : configs_) {
        McpServerSnapshot snapshot;
        snapshot.config = config;
        snapshot.selected_for_project = IsServerSelectedForProject(project_id, config.id);

        const std::string connection_key = BuildConnectionKey(config, project_id);
        const auto it = connections.find(connection_key);
        if (it == connections.end()) {
            snapshots.push_back(std::move(snapshot));
        } else {
            snapshot.status = it->second->Snapshot().status;
            snapshot.last_error = it->second->Snapshot().last_error;
            snapshot.tools = it->second->Snapshot().tools;
            snapshots.push_back(std::move(snapshot));
        }
    }

    return snapshots;
}

bool McpManager::ConnectServer(const std::string& server_id,
                               const std::string& project_id,
                               std::string* error,
                               const std::vector<ProjectMcpVariableValue>& runtime_variables) {
    const auto config_it = std::find_if(configs_.begin(), configs_.end(), [&](const McpServerConfig& config) { return config.id == server_id; });
    if (config_it == configs_.end()) {
        if (error) {
            *error = "MCP server configuration not found.";
        }
        return false;
    }

    if (!project_id.empty() && !IsServerSelectedForProject(project_id, server_id)) {
        if (error) {
            *error = "This MCP server is not configured for the active project.";
        }
        return false;
    }

    std::string resolved_error;
    const auto resolved_config =
        ResolveConfigForProject(*config_it, project_id, &resolved_error, runtime_variables);
    if (!resolved_config) {
        if (error) {
            *error = resolved_error;
        }
        return false;
    }

    const std::string connection_key = BuildConnectionKey(*config_it, project_id, runtime_variables);
    std::shared_ptr<Connection> connection;
    {
        std::scoped_lock lock(impl_->mutex);
        const auto existing = impl_->connections.find(connection_key);
        if (existing != impl_->connections.end()) {
            connection = existing->second;
        } else {
            connection = std::make_shared<Connection>(config_it->id, config_it->scope == McpServerScope::Shared ? std::string() : project_id, *resolved_config, [this]() { NotifyStateChanged(); });
            impl_->connections[connection_key] = connection;
        }
    }

    std::string connect_error;
    if (!connection->Connect(&connect_error)) {
        if (error) {
            *error = connect_error;
        }
        return false;
    }

    NotifyStateChanged();
    return true;
}

bool McpManager::RefreshServerTools(const std::string& server_id, const std::string& project_id, std::string* error) {
    const auto config_it = std::find_if(configs_.begin(), configs_.end(), [&](const McpServerConfig& config) { return config.id == server_id; });
    if (config_it == configs_.end()) {
        if (error) {
            *error = "MCP server configuration not found.";
        }
        return false;
    }

    const std::string connection_key = BuildConnectionKey(*config_it, project_id);
    std::shared_ptr<Connection> connection;
    {
        std::scoped_lock lock(impl_->mutex);
        const auto it = impl_->connections.find(connection_key);
        if (it == impl_->connections.end()) {
            if (error) {
                *error = "The MCP server is not connected.";
            }
            return false;
        }
        connection = it->second;
    }

    const bool success = connection->RefreshTools(error);
    NotifyStateChanged();
    return success;
}

void McpManager::DisconnectServer(const std::string& server_id, const std::string& project_id) {
    const auto config_it = std::find_if(configs_.begin(), configs_.end(), [&](const McpServerConfig& config) { return config.id == server_id; });
    if (config_it == configs_.end()) {
        return;
    }

    const std::string connection_key = BuildConnectionKey(*config_it, project_id);
    std::shared_ptr<Connection> connection;
    {
        std::scoped_lock lock(impl_->mutex);
        const auto it = impl_->connections.find(connection_key);
        if (it == impl_->connections.end()) {
            return;
        }
        connection = it->second;
        impl_->connections.erase(it);
    }

    connection->Disconnect();
    NotifyStateChanged();
}

void McpManager::ConnectAutoServers(const std::string& project_id) {
    for (const auto& config : configs_) {
        if (config.enabled && config.auto_connect && IsServerSelectedForProject(project_id, config.id)) {
            std::string ignored;
            ConnectServer(config.id, project_id, &ignored);
        }
    }
}

std::vector<McpExposedTool> McpManager::GetExposedToolsForProject(
    const std::string& project_id,
    const std::vector<ProjectMcpVariableValue>& runtime_variables) const {
    std::vector<McpExposedTool> exposed_tools;
    std::unordered_map<std::string, std::shared_ptr<Connection>> connections;
    {
        std::scoped_lock lock(impl_->mutex);
        connections = impl_->connections;
    }

    for (const auto& config : configs_) {
        if (!config.enabled || !IsServerSelectedForProject(project_id, config.id)) {
            continue;
        }

        const auto it = connections.find(BuildConnectionKey(config, project_id, runtime_variables));
        if (it == connections.end()) {
            continue;
        }

        const McpServerSnapshot snapshot = it->second->Snapshot();
        if (snapshot.status != McpServerStatus::Ready) {
            continue;
        }

        for (const auto& tool : snapshot.tools) {
            McpExposedTool exposed;
            exposed.alias = BuildToolAlias(config.id, tool.name);
            exposed.server_id = config.id;
            exposed.server_name = config.name;
            exposed.tool_name = tool.name;
            exposed.title = tool.title;
            exposed.description = AugmentToolDescription(config, tool);
            exposed.input_schema_json = tool.input_schema_json;
            exposed_tools.push_back(std::move(exposed));
        }
    }

    std::sort(exposed_tools.begin(), exposed_tools.end(), [](const McpExposedTool& left, const McpExposedTool& right) {
        if (left.server_name == right.server_name) {
            return left.tool_name < right.tool_name;
        }
        return left.server_name < right.server_name;
    });

    return exposed_tools;
}

std::string McpManager::BuildWebResearchUsageContext(const std::string& project_id) const {
    bool browser_search_primary = false;
    if (storage_ && !project_id.empty()) {
        const auto settings = storage_->LoadProjectSettings(project_id);
        browser_search_primary =
            settings.built_in_browser_search_enabled &&
            settings.browser_search_primary;
    }
    for (const auto& config : configs_) {
        if (!config.enabled || !LooksLikeWebResearchServer(config)) {
            continue;
        }
        if (!project_id.empty() && !IsServerSelectedForProject(project_id, config.id)) {
            continue;
        }
        return WebResearchUsageContextText(browser_search_primary);
    }
    return {};
}

McpToolCallResult McpManager::CallExposedTool(
    const std::string& project_id,
    const std::string& alias,
    const std::string& arguments_json,
    const std::vector<ProjectMcpVariableValue>& runtime_variables) const {
    const auto exposed_tools = GetExposedToolsForProject(project_id, runtime_variables);
    const auto tool_it = std::find_if(exposed_tools.begin(), exposed_tools.end(), [&](const McpExposedTool& tool) { return tool.alias == alias; });
    if (tool_it == exposed_tools.end()) {
        McpToolCallResult result;
        result.success = false;
        result.is_tool_error = true;
        result.content_text = "The requested MCP tool is not available in this project.";
        result.raw_result_json = json{{"error", result.content_text}}.dump(2);
        return result;
    }

    std::shared_ptr<Connection> connection;
    McpServerConfig resolved_config;
    std::string connection_key;
    std::string web_guard_key;
    std::string search_query;
    bool use_web_research_guard = false;
    {
        std::scoped_lock lock(impl_->mutex);
        const auto config_it = std::find_if(configs_.begin(), configs_.end(), [&](const McpServerConfig& config) { return config.id == tool_it->server_id; });
        if (config_it == configs_.end()) {
            McpToolCallResult result;
            result.success = false;
            result.is_tool_error = true;
            result.content_text = "The MCP server configuration is no longer available.";
            result.raw_result_json = json{{"error", result.content_text}}.dump(2);
            return result;
        }
        resolved_config = *config_it;
        connection_key = BuildConnectionKey(resolved_config, project_id, runtime_variables);
        use_web_research_guard =
            LooksLikeWebResearchServer(resolved_config) &&
            ToolNameLooksLikeSearch(tool_it->tool_name);
        if (use_web_research_guard) {
            search_query = ExtractSearchQueryFromArguments(arguments_json);
            web_guard_key = connection_key + "::" + tool_it->tool_name + "::" + search_query;
            const auto guard_it = impl_->web_research_guards.find(web_guard_key);
            if (guard_it != impl_->web_research_guards.end() &&
                guard_it->second.cooldown_until > std::chrono::steady_clock::now()) {
                McpToolCallResult result;
                result.success = false;
                result.is_tool_error = true;
                result.content_text = BuildWebResearchGuardMessage(
                    search_query,
                    guard_it->second.consecutive_suspicious_empty_results);
                result.raw_result_json = json{
                    {"error", "web_research_search_guard_active"},
                    {"message", result.content_text},
                    {"query", search_query},
                    {"consecutive_suspicious_empty_results",
                        guard_it->second.consecutive_suspicious_empty_results},
                }.dump(2);
                return result;
            }
        }

        const auto connection_it = impl_->connections.find(connection_key);
        if (connection_it != impl_->connections.end()) {
            connection = connection_it->second;
        }
    }

    if (!connection) {
        McpToolCallResult result;
        result.success = false;
        result.is_tool_error = true;
        result.content_text = "The MCP server is not connected.";
        result.raw_result_json = json{{"error", result.content_text}}.dump(2);
        return result;
    }

    McpToolCallResult result = connection->CallTool(tool_it->tool_name, arguments_json);

    if (use_web_research_guard && !web_guard_key.empty()) {
        std::scoped_lock lock(impl_->mutex);
        auto& guard = impl_->web_research_guards[web_guard_key];
        if (LooksLikeDuckDuckGoBlockedNoResults(result)) {
            ++guard.consecutive_suspicious_empty_results;
            if (guard.consecutive_suspicious_empty_results >= 2) {
                guard.cooldown_until =
                    std::chrono::steady_clock::now() + std::chrono::minutes(5);
                const std::string original_raw_result = result.raw_result_json;
                result.success = false;
                result.is_tool_error = true;
                result.content_text = BuildWebResearchGuardMessage(
                    search_query,
                    guard.consecutive_suspicious_empty_results);
                result.raw_result_json = json{
                    {"error", "web_research_suspected_duckduckgo_throttle"},
                    {"message", result.content_text},
                    {"query", search_query},
                    {"consecutive_suspicious_empty_results",
                        guard.consecutive_suspicious_empty_results},
                    {"original_result", original_raw_result},
                }.dump(2);
            }
        } else if (result.success) {
            guard.consecutive_suspicious_empty_results = 0;
            guard.cooldown_until = {};
        }
    }

    return result;
}

McpServerTestResult McpManager::TestServerConfig(const McpServerConfig& config) const {
    McpServerTestResult result;

    HANDLE stdin_read = nullptr;
    HANDLE stdin_write = nullptr;
    HANDLE stdout_read = nullptr;
    HANDLE stdout_write = nullptr;
    HANDLE stderr_read = nullptr;
    HANDLE stderr_write = nullptr;
    HANDLE process_handle = nullptr;
    HANDLE thread_handle = nullptr;

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    auto cleanup = [&]() {
        CloseIfValid(stdin_read);
        CloseIfValid(stdin_write);
        CloseIfValid(stdout_read);
        CloseIfValid(stdout_write);
        CloseIfValid(stderr_read);
        CloseIfValid(stderr_write);
        CloseIfValid(thread_handle);
        if (process_handle) {
            if (WaitForSingleObject(process_handle, 500) == WAIT_TIMEOUT) {
                TerminateProcess(process_handle, 0);
                WaitForSingleObject(process_handle, 1500);
            }
            CloseHandle(process_handle);
            process_handle = nullptr;
        }
    };

    try {
        if (!config.variables.empty() || ConfigUsesGlobalVariables(config, global_variables_)) {
            result.summary = "This MCP server uses project variables. Test it after assigning project values.";
            return result;
        }
        if (const auto validation_error = ValidateConfigForLaunch(config)) {
            result.summary = *validation_error;
            return result;
        }

        if (!CreatePipe(&stdin_read, &stdin_write, &security, 0) ||
            !CreatePipe(&stdout_read, &stdout_write, &security, 0) ||
            !CreatePipe(&stderr_read, &stderr_write, &security, 0)) {
            result.summary = "Failed to create pipes for the test process.";
            cleanup();
            return result;
        }

        SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = stdin_read;
        startup.hStdOutput = stdout_write;
        startup.hStdError = stderr_write;

        PROCESS_INFORMATION process_info{};
        const LaunchPlan launch_plan = BuildLaunchPlan(config);
        std::wstring command_line = launch_plan.command_line;
        std::wstring working_directory = Utf8ToWide(config.working_directory);
        std::vector<wchar_t> environment_block = BuildEnvironmentBlock(config.env_entries);

        result.stdin_text += "[launch]\n";
        result.stdin_text += "Configured command: " + config.command + "\n";
        result.stdin_text += "Resolved command: " + WideToUtf8(launch_plan.resolved_command) + "\n";
        result.stdin_text += "Launch mode: " + launch_plan.description + "\n";
        result.stdin_text += "Command line: " + WideToUtf8(launch_plan.command_line) + "\n\n";

        const BOOL created = CreateProcessW(
            launch_plan.application_name.empty() ? nullptr : launch_plan.application_name.c_str(),
            command_line.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW | (environment_block.empty() ? 0 : CREATE_UNICODE_ENVIRONMENT),
            environment_block.empty() ? nullptr : environment_block.data(),
            working_directory.empty() ? nullptr : working_directory.c_str(),
            &startup,
            &process_info);

        CloseIfValid(stdin_read);
        CloseIfValid(stdout_write);
        CloseIfValid(stderr_write);

        if (!created) {
            result.summary = "Failed to launch the test process.";
            cleanup();
            return result;
        }

        process_handle = process_info.hProcess;
        thread_handle = process_info.hThread;

        const std::string initialize_id = "test-initialize";
        const std::string initialize_line = json{
            {"jsonrpc", "2.0"},
            {"id", initialize_id},
            {"method", "initialize"},
            {"params", {
                {"protocolVersion", "2025-11-25"},
                {"capabilities", json::object()},
                {"clientInfo", {
                    {"name", "agent-desktop-test"},
                    {"title", "AI Agent Desktop Test"},
                    {"version", "0.2.0"},
                }},
            }},
        }.dump() + "\n";
        result.stdin_text += "[initialize request]\n" + initialize_line;

        DWORD written = 0;
        if (!WriteFile(stdin_write, initialize_line.data(), static_cast<DWORD>(initialize_line.size()), &written, nullptr) || written != initialize_line.size()) {
            result.summary = "The process launched, but the initialize request could not be sent.";
            cleanup();
            return result;
        }

        std::string stdout_buffer;
        std::string stderr_buffer;
        const auto initialize_response = WaitForJsonResponse(stdout_read, stderr_read, initialize_id, stdout_buffer, stderr_buffer, std::chrono::seconds(5));
        result.stdout_text = stdout_buffer;
        result.stderr_text = stderr_buffer;

        if (!initialize_response) {
            result.summary = DescribeProcessFailure(
                process_handle,
                "The process launched, but it did not answer the MCP initialize request within 5 seconds.",
                stderr_buffer);
            cleanup();
            return result;
        }

        if (initialize_response->contains("error")) {
            result.summary = "The process launched, but initialize returned an MCP error: " +
                             initialize_response->at("error").value("message", std::string("Unknown error."));
            result.stdout_text += "\n[initialize response]\n" + initialize_response->value("error", json::object()).dump(2);
            cleanup();
            return result;
        }

        result.stdout_text += "\n[initialize response]\n" + initialize_response->dump(2);
        if (initialize_response->contains("result")) {
            const auto& init_result = (*initialize_response)["result"];
            if (init_result.contains("serverInfo") && init_result["serverInfo"].is_object()) {
                const auto& server_info = init_result["serverInfo"];
                std::string name = server_info.value("name", "");
                std::string version = server_info.value("version", "");
                if (!name.empty()) {
                    result.detected_features.push_back("server: " + name + (version.empty() ? "" : (" v" + version)));
                }
            }
            if (init_result.contains("protocolVersion")) {
                result.detected_features.push_back("protocol: " + init_result.value("protocolVersion", ""));
            }
            if (init_result.contains("capabilities") && init_result["capabilities"].is_object()) {
                const auto& capabilities = init_result["capabilities"];
                for (auto it = capabilities.begin(); it != capabilities.end(); ++it) {
                    result.detected_features.push_back("capability: " + it.key());
                }
            }
        }

        const std::string initialized_line = json{
            {"jsonrpc", "2.0"},
            {"method", "notifications/initialized"},
            {"params", json::object()},
        }.dump() + "\n";
        result.stdin_text += "\n[initialized notification]\n" + initialized_line;
        WriteFile(stdin_write, initialized_line.data(), static_cast<DWORD>(initialized_line.size()), &written, nullptr);

        const std::string tools_id = "test-tools";
        const std::string tools_line = json{
            {"jsonrpc", "2.0"},
            {"id", tools_id},
            {"method", "tools/list"},
            {"params", json::object()},
        }.dump() + "\n";
        result.stdin_text += "\n[tools/list request]\n" + tools_line;

        if (!WriteFile(stdin_write, tools_line.data(), static_cast<DWORD>(tools_line.size()), &written, nullptr) || written != tools_line.size()) {
            result.success = true;
            result.summary = "Initialize succeeded. The server did not accept a follow-up tools/list request for diagnostics.";
            cleanup();
            return result;
        }

        const auto tools_response = WaitForJsonResponse(stdout_read, stderr_read, tools_id, result.stdout_text, result.stderr_text, std::chrono::seconds(3));
        if (tools_response && tools_response->contains("result")) {
            result.stdout_text += "\n[tools/list response]\n" + tools_response->dump(2);
            int tool_count = 0;
            if ((*tools_response)["result"].contains("tools") && (*tools_response)["result"]["tools"].is_array()) {
                tool_count = static_cast<int>((*tools_response)["result"]["tools"].size());
                for (const auto& tool : (*tools_response)["result"]["tools"]) {
                    if (!tool.is_object()) {
                        continue;
                    }
                    const std::string tool_name = tool.value("name", "");
                    const std::string tool_title = tool.value("title", "");
                    if (!tool_name.empty()) {
                        result.detected_tools.push_back(tool_title.empty() ? tool_name : (tool_title + " (" + tool_name + ")"));
                    }
                }
            }
            result.success = true;
            result.summary = "Launch and initialize succeeded. tools/list returned " + std::to_string(tool_count) + " tool(s).";
        } else {
            result.success = true;
            result.summary = "Launch and initialize succeeded.";
        }

        cleanup();
        return result;
    } catch (const std::exception& ex) {
        result.summary = ex.what();
        cleanup();
        return result;
    } catch (...) {
        result.summary = "Unexpected error while testing the MCP server.";
        cleanup();
        return result;
    }
}

void McpManager::NotifyStateChanged() const {
    if (state_changed_callback_) {
        state_changed_callback_();
    }
}
