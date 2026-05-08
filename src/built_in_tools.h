#pragma once

#include "mcp_manager.h"
#include "openai_client.h"
#include "types.h"
#include "util.h"
#include "variable_resolver.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>
#include <windows.h>

namespace built_in_tools {

inline constexpr const char* kPowerShellToolName = "powershell_execute";
inline constexpr const char* kQuestionnaireToolName = "user_questionnaire";
inline constexpr const char* kPlannerToolName = "project_planner";
inline constexpr const char* kCompletionDriverToolName = "completion_driver";
inline constexpr const char* kCompletionDriverContinuationPrefix = "[Completion Driver generated continuation prompt]";
inline constexpr const char* kDefaultPlannerStorageFolder = "$ProjectFolder$\\.agent";
inline constexpr const char* kPlannerFileName = "planner.json";
inline constexpr const char* kFilesystemToolName = "project_filesystem";

inline bool IsBuiltInToolName(const std::string& name) {
    return name == kPowerShellToolName || name == kQuestionnaireToolName ||
           name == kPlannerToolName || name == kCompletionDriverToolName ||
           name == kFilesystemToolName;
}

inline std::string TraceTitleForBuiltInTool(const std::string& name) {
    if (name == kPowerShellToolName) return "Built-in / PowerShell";
    if (name == kQuestionnaireToolName) return "Built-in / User Questionnaire";
    if (name == kPlannerToolName) return "Built-in / Planner";
    if (name == kCompletionDriverToolName) return "Built-in / Completion Driver";
    if (name == kFilesystemToolName) return "Built-in / Project Filesystem";
    return "Built-in / " + name;
}

inline std::string LowerAsciiCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

inline bool IsQuestionnaireEnabled(const ProjectSettings& settings,
                                     const std::string& current_agentic_mode_id = {}) {
    if (!settings.built_in_questionnaire_enabled) return false;
    if (!settings.questionnaire_restrict_by_mode) return true;
    if (settings.questionnaire_allowed_mode_id.empty()) return false;
    return settings.questionnaire_allowed_mode_id == current_agentic_mode_id;
}

inline bool IsCompletionDriverEnabled(const ProjectSettings& settings,
                                       const std::string& current_agentic_mode_id = {}) {
    if (!settings.built_in_completion_driver_enabled) return false;
    if (current_agentic_mode_id.empty()) return false;
    return std::find(settings.completion_driver_allowed_mode_ids.begin(),
                     settings.completion_driver_allowed_mode_ids.end(),
                     current_agentic_mode_id) != settings.completion_driver_allowed_mode_ids.end();
}

inline std::string QuestionnaireSystemPrompt() {
    return (
        "User Questionnaire Instructions:\n"
        "- When you need a clarification, preference, or decision from the user, call the user_questionnaire tool.\n"
        "- Do NOT ask the user indirectly inside normal assistant text; always route the question through the tool.\n"
        "- Provide a concise question and a short list of distinct, actionable options.\n"
        "- After the tool returns the user's selection, use that answer to continue.\n"
        "- The tool blocks until the user responds, so the conversation will pause cleanly."
    );
}

inline std::string FilesystemSystemPrompt() {
    return (
        "Project Filesystem Instructions:\n"
        "- Use the project_filesystem tool for all file read, write, directory listing, and edit operations.\n"
        "- Always specify paths relative to the configured working directory (default $ProjectFolder$). Templates like $ProjectFolder$ are expanded automatically.\n"
        "- Actions: read, write, edit, list_directory, create_directory.\n"
        "- read: Returns file content. Optionally pass start_line / end_line (1-based, inclusive) or start_offset / length (bytes).\n"
        "- write: Overwrites a file. Pass create_backup=true to snapshot the existing file into .agent/backups/<timestamp>/<path> before overwriting.\n"
        "- edit: Applies JSON diff edits. Each edit object uses either:\n"
        "  1) old_lines + new_lines — exact match-and-replace by contiguous lines, or\n"
        "  2) direct JSON-patch style: {\"op\": \"replace\", \"path\": \"...\"} (not yet supported; use old_lines/new_lines).\n"
        "- Edit matching tries exact lines first, then safe whitespace-tolerant matching. Python/YAML/Makefile-style files only allow indentation-preserving tolerance.\n"
        "- list_directory: Returns files and subdirectories for a path.\n"
        "- create_directory: Creates a directory (including parents).\n"
        "- Backups: When create_backup=true, the tool copies the original file or directory tree into $ProjectFolder$/.agent/backups/<timestamp>/<original_relative_path>.\n"
        "- The host auto-expands path templates. Do not escape backslashes unnecessarily; forward slashes are acceptable.\n"
        "- If a path is invalid, permission is denied, or a file is missing, the tool returns a clear error — do not retry blindly.\n"
        "Examples:\n"
        "  Read whole file: {\"action\":\"read\",\"path\":\"src/main.cpp\"}\n"
        "  Read lines 10-20: {\"action\":\"read\",\"path\":\"src/main.cpp\",\"start_line\":10,\"end_line\":20}\n"
        "  Write with backup: {\"action\":\"write\",\"path\":\"README.md\",\"content\":\"...\",\"create_backup\":true}\n"
        "  Edit by lines: {\"action\":\"edit\",\"path\":\"src/main.cpp\",\"edits\":[{\"old_lines\":[\"old line 1\",\"old line 2\"],\"new_lines\":[\"new line 1\"]}]}\n"
        "  List dir: {\"action\":\"list_directory\",\"path\":\"src\"}\n"
        "  Create dir: {\"action\":\"create_directory\",\"path\":\"src/utils\"}");
}

inline std::string PowerShellSystemPrompt() {
    return (
        "PowerShell Execution Instructions:\n"
        "- The powershell_execute tool runs a PowerShell command line on the host machine.\n"
        "- This is a high-risk tool. Use it ONLY when the user explicitly needs command execution.\n"
        "- Prefer the project_filesystem tool for reading, writing, or editing files instead of PowerShell.\n"
        "   * Use powershell_execute for: dependency installs, build scripts, git commands, environment checks.\n"
        "   * Use project_filesystem for: reading source code, editing files, listing directories.\n"
        "- Provide concise commands. Avoid destructive operations unless explicitly requested.\n"
        "- Set timeout_seconds (1-120) if the command may take long; default is 30s.\n"
        "- The tool returns stdout, stderr, exit code, and timed_out status. Do not silently swallow errors.\n"
        "- If a command fails, analyze the error output rather than blindly retrying.\n"
        "Examples:\n"
        "  Install dependency: {\"command\":\"npm install\",\"timeout_seconds\":60}\n"
        "  Check git status: {\"command\":\"git status\"}\n"
        "  Build project: {\"command\":\"cmake --build build --config Release\",\"timeout_seconds\":120}");
}

inline std::string PlannerSystemPrompt() {
    return (
        "Planner / Task Decomposition Instructions:\n"
        "- The project_planner tool maintains a persistent project plan stored as planner.json in the project folder (default $ProjectFolder$\\.agent).\n"
        "- Use it to track goals, subgoals, steps, features, blockers, notes, and tool hints across the full interaction.\n"
        "- Actions: get, create/replace, update, clear, add_item, update_item, remove_item.\n"
        "- get: Load the current plan. Do this at the start of a complex task to check existing state.\n"
        "- create/replace: Write a complete new plan. Use when establishing a new project or resetting the plan.\n"
        "- update: Merge top-level fields into the existing plan. Use for bulk updates (e.g., change the main goal).\n"
        "- clear: Delete all items from a section (goals/features/steps/blockers/notes/tool_hints) or the entire plan if section=all.\n"
        "- add_item: Append an item to a section. Optionally nest under a parent item via parent_id.\n"
        "- update_item: Modify fields of an existing item by id (e.g., mark status=completed).\n"
        "- remove_item: Delete an item by id from a section (or section=all to search all sections).\n"
        "Sections: goals, features, steps, blockers, notes, tool_hints.\n"
        "Status values for items: pending (not started), in_progress (active), completed (done), blocked (waiting), cancelled (abandoned).\n"
        "- When adding items, an id is auto-generated if omitted.\n"
        "- child_section defaults to 'subgoals' for goals, otherwise the requested section.\n"
        "- To edit an existing item, always use update_item with the existing id.\n"
        "- The only way to get strikethrough in the UI is status=completed.\n"
        "Examples:\n"
        "  Load plan: {\"action\":\"get\"}\n"
        "  Create full plan: {\"action\":\"create\",\"plan\":{\"goal\":\"Build app\",\"goals\":[{\"id\":\"g1\",\"title\":\"Setup\",\"status\":\"pending\"}]}}\n"
        "  Add a step: {\"action\":\"add_item\",\"section\":\"steps\",\"item\":{\"task\":\"Install deps\",\"status\":\"pending\"}}\n"
        "  Add nested subgoal: {\"action\":\"add_item\",\"section\":\"goals\",\"parent_id\":\"g1\",\"item\":{\"title\":\"Subtask\",\"status\":\"pending\"}}\n"
        "  Mark completed: {\"action\":\"update_item\",\"section\":\"all\",\"id\":\"s1\",\"item\":{\"status\":\"completed\"}}\n"
        "  Mark blocked: {\"action\":\"update_item\",\"section\":\"all\",\"id\":\"s1\",\"item\":{\"status\":\"blocked\"}}\n"
        "  Remove item: {\"action\":\"remove_item\",\"section\":\"all\",\"id\":\"s1\"}\n"
        "Notes:\n"
        "- Use the planner proactively. When the user gives a multi-step task, first get the plan, then update items as progress is made.\n"
        "- Tool hints in the planner help future turns choose the right tool without guessing.");
}

inline int NormalizedCompletionDriverMaxContinuations(const ProjectSettings& settings) {
    return std::max(0, settings.completion_driver_max_continuations);
}

inline std::string CompletionDriverSystemPrompt(int max_continuations = 0) {
    std::ostringstream text;
    text
        << "Completion Driver Instructions:\n"
        << "- The Completion Driver is active for this agentic mode. The host will keep the agent running by adding driver-generated continuation prompts until you call the completion_driver tool with completed=true or status=\"completed\"/\"done\".\n"
        << "- Treat any message beginning with " << kCompletionDriverContinuationPrefix
        << " as an internal continuation prompt generated by the Completion Driver, not as a new user request.\n"
        << "- Continue working across those prompts until the user's objective is fully complete.\n"
        << "- When the objective is complete, call completion_driver with completed=true and a concise final_summary. After that, provide the final user-facing answer.\n"
        << "- If more work remains, either keep working or call completion_driver with completed=false and remaining_work/next_action so the host can continue the loop.\n";
    if (max_continuations > 0) {
        text << "- This project limits host-generated Completion Driver continuation prompts to "
             << max_continuations
             << " per model run or automation step. Before the limit is reached, prefer concrete tool progress or a clear completion_driver status.";
    } else {
        text << "- This project has no fixed limit on host-generated Completion Driver continuation prompts.";
    }
    return text.str();
}

inline MessageRecord MakeCompletionDriverContinuationMessage(int turn_index,
                                                             int max_continuations = 0) {
    MessageRecord message;
    message.role = "user";
    message.created_at = CurrentTimestampUtc();
    std::ostringstream text;
    text << kCompletionDriverContinuationPrefix
         << "\nThis is not a new user message. Continue the current task from the previous state. "
         << "If the user's objective is now fully complete, call completion_driver with completed=true/status=completed and then provide the final answer. "
         << "Otherwise continue making progress and use available tools as needed.\nContinuation turn: "
         << turn_index;
    if (max_continuations > 0) {
        text << " of " << max_continuations;
    }
    message.content = text.str();
    return message;
}

inline bool IsCompletionDriverContinuationMessage(const MessageRecord& message) {
    return message.role == "user" &&
           message.content.rfind(kCompletionDriverContinuationPrefix, 0) == 0;
}

inline std::vector<ChatToolDefinition> BuildDefinitions(
    const ProjectSettings& settings,
    const std::string& current_agentic_mode_id = {}) {
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
    if (settings.built_in_planner_enabled) {
        ChatToolDefinition planner;
        planner.name = kPlannerToolName;
        planner.description =
            "Planner / Task Decomposition tool. Maintain a persistent project plan stored as planner.json in the "
            "configured project folder (default $ProjectFolder$\\.agent). Use this to track goals, subgoals, steps, "
            "features, blockers, notes, and tool hints across the full interaction.\n\n"
            "Valid actions:\n"
            "- get — Load the current plan from disk.\n"
            "- clear — Delete all items from a section (or the entire plan if section=all).\n"
            "- create or replace — Write a brand-new plan (or replace the existing one entirely). Requires 'plan'.\n"
            "- update — Merge top-level fields or sections into the existing plan. Requires 'plan'.\n"
            "- add_item — Add a new item to a section. Can be nested under a parent item.\n"
            "- update_item — Modify fields of an existing item by id (e.g. change status to completed).\n"
            "- remove_item — Delete an item by id.\n\n"
            "Sections: goals, features, steps, blockers, notes, tool_hints\n"
            "Status values (for item.status):\n"
            "- pending — Not started (unchecked in UI)\n"
            "- in_progress — Currently active (blue badge)\n"
            "- completed — Done. Checkbox checked + title struck through in UI.\n"
            "- blocked — Waiting on a blocker (red badge)\n"
            "- cancelled — Abandoned (grey badge, no strikethrough)\n\n"
            "Examples:\n"
            "1) Load plan: {\"action\":\"get\"}\n"
            "2) Create/replace full plan: {\"action\":\"create\",\"plan\":{\"goal\":\"Build app\",\"goals\":[{\"id\":\"g1\",\"title\":\"Setup\",\"status\":\"pending\"}]}}\n"
            "3) Add a top-level step: {\"action\":\"add_item\",\"section\":\"steps\",\"item\":{\"task\":\"Install deps\",\"status\":\"pending\"}}\n"
            "4) Add a nested subgoal under parent g1: {\"action\":\"add_item\",\"section\":\"goals\",\"parent_id\":\"g1\",\"item\":{\"title\":\"Subtask\",\"status\":\"pending\"}}\n"
            "5) Check off / mark completed: {\"action\":\"update_item\",\"section\":\"all\",\"id\":\"s1\",\"item\":{\"status\":\"completed\"}}\n"
            "6) Mark in progress: {\"action\":\"update_item\",\"section\":\"all\",\"id\":\"s1\",\"item\":{\"status\":\"in_progress\"}}\n"
            "7) Cancel / abandon: {\"action\":\"update_item\",\"section\":\"all\",\"id\":\"s1\",\"item\":{\"status\":\"cancelled\"}}\n"
            "8) Remove an item: {\"action\":\"remove_item\",\"section\":\"all\",\"id\":\"s1\"}\n\n"
            "Notes:\n"
            "- When adding items, an id is auto-generated if omitted.\n"
            "- child_section defaults to 'subgoals' for goals, otherwise the requested section.\n"
            "- To edit an existing item, always use update_item with the existing id.\n"
            "- The only way to get strikethrough in the UI is status=completed.\n"
            "- 'create' is an alias for 'replace' and is the preferred action when establishing a new plan.";
        planner.parameters_json = R"({
  "type": "object",
  "properties": {
    "action": {"type": "string", "enum": ["get", "create", "replace", "update", "clear", "add_item", "update_item", "remove_item"], "description": "Planner operation to perform. Use 'get' to read, 'create' or 'replace' to write a full plan, 'update' to merge fields, 'add_item' to append, 'update_item' to edit by id, 'remove_item' to delete by id, 'clear' to empty a section or all."},
    "section": {"type": "string", "enum": ["goals", "features", "steps", "blockers", "notes", "tool_hints", "all"], "description": "Plan section for item operations or section clears."},
    "id": {"description": "Item id for update_item or remove_item. String or number."},
    "parent_id": {"description": "Optional parent item id for add_item. Use this to add nested subgoals or nested steps."},
    "parent_section": {"type": "string", "enum": ["goals", "features", "steps", "blockers", "notes", "all"], "description": "Optional section to search for parent_id. Defaults to all searchable sections."},
    "child_section": {"type": "string", "enum": ["subgoals", "goals", "steps", "features", "blockers", "notes", "tool_hints"], "description": "Nested array on the parent for add_item. Defaults to subgoals when adding goals, otherwise the requested section."},
    "item": {"type": "object", "description": "Item to add or fields to merge into an existing item. Goals may include subgoals/goals and steps arrays. Steps may include nested steps. Steps should include task, status, tool_hint, and done_when when applicable."},
    "plan": {"type": "object", "description": "Complete replacement plan for create/replace, or top-level fields/sections to merge for update."}
  },
  "required": ["action"]
})";
        definitions.push_back(std::move(planner));
    }
    if (IsCompletionDriverEnabled(settings, current_agentic_mode_id)) {
        ChatToolDefinition driver;
        driver.name = kCompletionDriverToolName;
        driver.description =
            "Report whether the current user objective is complete. The host uses this built-in tool to decide "
            "whether to stop or send another Completion Driver continuation prompt. Call it with completed=true "
            "or status completed/done only when the objective is fully complete and ready for the final answer. "
            "Call it with completed=false/status continue when more work remains.";
        driver.parameters_json = R"({
  "type": "object",
  "properties": {
    "status": {"type": "string", "enum": ["continue", "completed", "done", "blocked"], "description": "Completion status for the current objective."},
    "completed": {"type": "boolean", "description": "True only when the current user objective is fully complete."},
    "final_summary": {"type": "string", "description": "Concise summary to use once complete."},
    "remaining_work": {"type": "string", "description": "What remains if the objective is not complete."},
    "next_action": {"type": "string", "description": "The next action the agent should take if continuing."}
  }
})";
        definitions.push_back(std::move(driver));
    }
    if (IsQuestionnaireEnabled(settings, current_agentic_mode_id)) {
        ChatToolDefinition q;
        q.name = kQuestionnaireToolName;
        q.description =
            "Present a multiple-choice question to the user and wait for their selection before continuing. "
            "Use this for clarifying requirements, collecting preferences, confirming trade-offs, or gathering planning decisions. "
            "The question appears as a bubble with clickable options in the web UI. The model should not assume the answer; "
            "it must wait until the user selects one of the provided choices and the tool result is injected back into context. "
            "Do not ask rhetorical questions without this tool when a decision is needed.";
        q.parameters_json = R"({"type":"object","properties":{"question":{"type":"string","description":"The question or prompt text shown to the user."},"options":{"type":"array","items":{"type":"string"},"description":"A list of distinct clickable answer choices."},"allow_multiple":{"type":"boolean","description":"If true, the user may select more than one option. Default false."}},"required":["question","options"]})";
        definitions.push_back(std::move(q));
    }
    if (settings.built_in_filesystem_enabled) {
        ChatToolDefinition fs;
        fs.name = kFilesystemToolName;
        fs.description =
            "Project Filesystem tool. Read, write, edit, list, and create files/directories under the configured working directory. "
            "All paths are relative to the working directory (default $ProjectFolder$) and templates are auto-expanded.\n\n"
            "Actions:\n"
            "- read — Return the full content of a file, or a portion if start_line/end_line or start_offset/length are provided.\n"
            "- write — Overwrite a file with new content. Set create_backup=true to snapshot the original into .agent/backups/<timestamp>.\n"
            "- edit — Apply diff edits using old_lines/new_lines replacements. Each edit must match contiguous existing lines exactly.\n"
            "- list_directory — Return files and subdirectories for the given path.\n"
            "- create_directory — Create a new directory (including intermediate parents).\n\n"
            "Edit matching tries exact lines first, then safe whitespace-tolerant matching. Python/YAML/Makefile-style files only allow indentation-preserving tolerance.\n\n"
            "Errors are explicit: file_not_found, permission_denied, invalid_path, or backup_failed. Do not retry blindly.";
        fs.parameters_json = R"({
  "type": "object",
  "properties": {
    "action": {"type": "string", "enum": ["read", "write", "edit", "list_directory", "create_directory"], "description": "Filesystem operation to perform."},
    "path": {"type": "string", "description": "Relative path within the working directory. Supports $ProjectFolder$ templates."},
    "content": {"type": "string", "description": "Full file content for write action."},
    "start_line": {"type": "integer", "description": "Optional 1-based start line for read."},
    "end_line": {"type": "integer", "description": "Optional 1-based end line for read (inclusive)."},
    "start_offset": {"type": "integer", "description": "Optional byte start offset for read."},
    "length": {"type": "integer", "description": "Optional byte length for read."},
    "create_backup": {"type": "boolean", "description": "If true, backup the original file or directory before write or edit."},
    "edits": {"type": "array", "description": "For edit action: list of {old_lines:[...], new_lines:[...]} objects. old_lines must match contiguous lines exactly."}
  },
  "required": ["action", "path"]
})";
        definitions.push_back(std::move(fs));
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

inline McpToolCallResult CallQuestionnaire(
    const std::string& arguments_json,
    std::function<McpToolCallResult(const std::string&)> wait_for_answer) {
    nlohmann::json args;
    try {
        args = nlohmann::json::parse(arguments_json.empty() ? "{}" : arguments_json);
    } catch (...) {
        return ErrorResult("Invalid questionnaire tool arguments: not valid JSON.");
    }
    const std::string question = Trim(args.value("question", ""));
    const auto options_j = args.value("options", nlohmann::json::array());
    if (question.empty() || !options_j.is_array() || options_j.empty()) {
        return ErrorResult(
            "The user questionnaire tool requires a non-empty question and at least one option.");
    }
    if (wait_for_answer) {
        return wait_for_answer(arguments_json);
    }
    return ErrorResult(
        "The user questionnaire is awaiting a response from the user interface. "
        "Please wait until the user selects an option before continuing.");
}

inline McpToolCallResult CallCompletionDriver(const std::string& arguments_json) {
    nlohmann::json args;
    try {
        args = nlohmann::json::parse(arguments_json.empty() ? "{}" : arguments_json);
    } catch (const std::exception& ex) {
        return ErrorResult(std::string("Invalid completion driver arguments: ") + ex.what());
    }
    const std::string status = LowerAsciiCopy(Trim(args.value("status", "")));
    bool completed = args.value("completed", false);
    if (status == "completed" || status == "complete" || status == "done") {
        completed = true;
    }
    const std::string final_summary = Trim(args.value("final_summary", ""));
    const std::string remaining_work = Trim(args.value("remaining_work", ""));
    const std::string next_action = Trim(args.value("next_action", ""));

    McpToolCallResult result;
    result.success = true;
    nlohmann::json payload = {
        {"tool", kCompletionDriverToolName},
        {"success", true},
        {"completed", completed},
        {"status", completed ? "completed" : (status.empty() ? "continue" : status)},
        {"final_summary", final_summary},
        {"remaining_work", remaining_work},
        {"next_action", next_action},
    };
    result.raw_result_json = payload.dump(2);
    std::ostringstream text;
    text << "Completion Driver status: " << (completed ? "completed" : "continue");
    if (!final_summary.empty()) text << "\nFinal summary: " << final_summary;
    if (!remaining_work.empty()) text << "\nRemaining work: " << remaining_work;
    if (!next_action.empty()) text << "\nNext action: " << next_action;
    result.content_text = text.str();
    return result;
}

inline bool IsCompletionDriverCompletedResult(const McpToolCallResult& result) {
    try {
        const auto payload = nlohmann::json::parse(result.raw_result_json.empty() ? "{}" : result.raw_result_json);
        if (payload.value("tool", "") != kCompletionDriverToolName) return false;
        if (payload.value("completed", false)) return true;
        const std::string status = LowerAsciiCopy(Trim(payload.value("status", "")));
        return status == "completed" || status == "complete" || status == "done";
    } catch (...) {
        return false;
    }
}

inline nlohmann::json EmptyPlannerDocument() {
    return nlohmann::json{
        {"goal", ""},
        {"goals", nlohmann::json::array()},
        {"features", nlohmann::json::array()},
        {"steps", nlohmann::json::array()},
        {"blockers", nlohmann::json::array()},
        {"notes", nlohmann::json::array()},
        {"tool_hints", nlohmann::json::array({
            "web_search",
            "rag_search",
            "filesystem",
            "file_edit",
            "execution_sandbox",
            "git",
            "command_line",
            "document_parser",
        })},
        {"updated_at", CurrentTimestampUtc()},
    };
}

inline bool IsPlannerSection(const std::string& section) {
    return section == "goals" || section == "features" || section == "steps" ||
           section == "blockers" || section == "notes" || section == "tool_hints";
}

inline bool IsPlannerChildSection(const std::string& section) {
    return IsPlannerSection(section) || section == "subgoals";
}

inline std::filesystem::path PlannerFilePath(
    const ProjectSettings& settings,
    const std::vector<ProjectMcpVariableValue>& variables,
    std::string* expanded_folder_out = nullptr) {
    std::string folder_template = Trim(settings.built_in_planner_storage_folder);
    if (folder_template.empty()) {
        folder_template = kDefaultPlannerStorageFolder;
    }
    std::string expanded_folder = Trim(variable_resolver::ExpandTemplate(folder_template, variables));
    if (expanded_folder.empty()) {
        expanded_folder = kDefaultPlannerStorageFolder;
    }
    if (expanded_folder_out) {
        *expanded_folder_out = expanded_folder;
    }
    return std::filesystem::path(Utf8ToWide(expanded_folder)) / Utf8ToWide(kPlannerFileName);
}

inline bool LoadPlannerDocument(const std::filesystem::path& file_path, nlohmann::json* plan, std::string* error) {
    if (!std::filesystem::exists(file_path)) {
        *plan = EmptyPlannerDocument();
        return true;
    }
    std::ifstream input(file_path, std::ios::binary);
    if (!input.is_open()) {
        if (error) *error = "Could not open planner file for reading: " + WideToUtf8(file_path.wstring());
        return false;
    }
    try {
        input >> *plan;
    } catch (const std::exception& ex) {
        if (error) *error = std::string("Planner file is not valid JSON: ") + ex.what();
        return false;
    }
    if (!plan->is_object()) {
        if (error) *error = "Planner file root must be a JSON object.";
        return false;
    }
    return true;
}

inline bool SavePlannerDocument(const std::filesystem::path& file_path, const nlohmann::json& plan, std::string* error) {
    try {
        std::filesystem::create_directories(file_path.parent_path());
        std::ofstream output(file_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            if (error) *error = "Could not open planner file for writing: " + WideToUtf8(file_path.wstring());
            return false;
        }
        output << plan.dump(2);
        return true;
    } catch (const std::exception& ex) {
        if (error) *error = std::string("Could not save planner file: ") + ex.what();
        return false;
    }
}

inline bool PlannerIdEquals(const nlohmann::json& lhs, const nlohmann::json& rhs) {
    if (lhs == rhs) return true;
    if ((lhs.is_number_integer() || lhs.is_number_unsigned()) && rhs.is_string()) {
        return std::to_string(lhs.get<long long>()) == rhs.get<std::string>();
    }
    if (lhs.is_string() && (rhs.is_number_integer() || rhs.is_number_unsigned())) {
        return lhs.get<std::string>() == std::to_string(rhs.get<long long>());
    }
    return false;
}

inline int NextPlannerItemId(const nlohmann::json& items) {
    int next_id = 1;
    if (!items.is_array()) return next_id;
    for (const auto& item : items) {
        if (item.is_object() && item.contains("id")) {
            const auto& id = item["id"];
            if (id.is_number_integer() || id.is_number_unsigned()) {
                next_id = std::max(next_id, id.get<int>() + 1);
            }
        }
    }
    return next_id;
}

inline void UpdateMaxPlannerItemId(const nlohmann::json& items, int* max_id) {
    if (!items.is_array() || !max_id) return;
    for (const auto& item : items) {
        if (!item.is_object()) continue;
        if (item.contains("id")) {
            const auto& id = item["id"];
            if (id.is_number_integer() || id.is_number_unsigned()) {
                *max_id = std::max(*max_id, id.get<int>());
            }
        }
        for (const char* key : {"goals", "subgoals", "features", "steps", "blockers", "notes"}) {
            if (item.contains(key)) {
                UpdateMaxPlannerItemId(item[key], max_id);
            }
        }
    }
}

inline int NextPlannerItemIdInPlan(const nlohmann::json& plan) {
    int max_id = 0;
    for (const char* section : {"goals", "features", "steps", "blockers", "notes"}) {
        if (plan.contains(section)) {
            UpdateMaxPlannerItemId(plan[section], &max_id);
        }
    }
    return max_id + 1;
}

inline nlohmann::json* FindPlannerItemRecursive(nlohmann::json& items, const nlohmann::json& id) {
    if (!items.is_array()) return nullptr;
    for (auto& item : items) {
        if (!item.is_object()) continue;
        if (item.contains("id") && PlannerIdEquals(item["id"], id)) {
            return &item;
        }
        for (const char* key : {"goals", "subgoals", "features", "steps", "blockers", "notes"}) {
            if (!item.contains(key)) continue;
            if (auto* found = FindPlannerItemRecursive(item[key], id)) {
                return found;
            }
        }
    }
    return nullptr;
}

inline nlohmann::json* FindPlannerItemInPlan(
    nlohmann::json& plan,
    const nlohmann::json& id,
    const std::string& preferred_section = {}) {
    if (!preferred_section.empty() && preferred_section != "all") {
        if (!IsPlannerSection(preferred_section)) return nullptr;
        if (plan.contains(preferred_section)) {
            if (auto* found = FindPlannerItemRecursive(plan[preferred_section], id)) {
                return found;
            }
        }
    }
    for (const char* section : {"goals", "features", "steps", "blockers", "notes"}) {
        if (preferred_section == section) continue;
        if (!plan.contains(section)) continue;
        if (auto* found = FindPlannerItemRecursive(plan[section], id)) {
            return found;
        }
    }
    return nullptr;
}

inline bool RemovePlannerItemRecursive(nlohmann::json& items, const nlohmann::json& id) {
    if (!items.is_array()) return false;
    for (auto it = items.begin(); it != items.end(); ++it) {
        if (!it->is_object()) continue;
        if (it->contains("id") && PlannerIdEquals((*it)["id"], id)) {
            items.erase(it);
            return true;
        }
        for (const char* key : {"goals", "subgoals", "features", "steps", "blockers", "notes"}) {
            if (!it->contains(key)) continue;
            if (RemovePlannerItemRecursive((*it)[key], id)) {
                return true;
            }
        }
    }
    return false;
}

inline bool RemovePlannerItemFromPlan(
    nlohmann::json& plan,
    const nlohmann::json& id,
    const std::string& preferred_section = {}) {
    if (!preferred_section.empty() && preferred_section != "all") {
        if (!IsPlannerSection(preferred_section)) return false;
        if (plan.contains(preferred_section) && RemovePlannerItemRecursive(plan[preferred_section], id)) {
            return true;
        }
    }
    for (const char* section : {"goals", "features", "steps", "blockers", "notes"}) {
        if (preferred_section == section) continue;
        if (!plan.contains(section)) continue;
        if (RemovePlannerItemRecursive(plan[section], id)) {
            return true;
        }
    }
    return false;
}

inline void PreparePlannerItem(nlohmann::json& item, const std::string& section, const nlohmann::json& plan) {
    if (!item.contains("id")) {
        item["id"] = NextPlannerItemIdInPlan(plan);
    }
    if ((section == "goals" || section == "subgoals" || section == "features" ||
         section == "steps" || section == "blockers") &&
        !item.contains("status")) {
        item["status"] = "pending";
    }
}

inline McpToolCallResult PlannerResult(
    const std::string& action,
    const std::string& file_path,
    const nlohmann::json& plan,
    bool changed) {
    McpToolCallResult result;
    result.success = true;
    nlohmann::json payload = {
        {"tool", kPlannerToolName},
        {"success", true},
        {"action", action},
        {"changed", changed},
        {"path", file_path},
        {"plan", plan},
    };
    result.raw_result_json = payload.dump(2);
    result.content_text = std::string("Planner ") + (changed ? "updated" : "loaded") + ".\nPath: " + file_path + "\n\n" + plan.dump(2);
    return result;
}

inline McpToolCallResult CallPlanner(
    const std::string& arguments_json,
    const ProjectSettings& settings,
    const std::vector<ProjectMcpVariableValue>& variables) {
    nlohmann::json args;
    try {
        args = nlohmann::json::parse(arguments_json.empty() ? "{}" : arguments_json);
    } catch (const std::exception& ex) {
        return ErrorResult(std::string("Invalid planner tool arguments: ") + ex.what());
    }
    const std::string action = Trim(args.value("action", ""));
    if (action.empty()) {
        return ErrorResult("Planner tool requires an action.");
    }

    std::string expanded_folder;
    const std::filesystem::path file_path = PlannerFilePath(settings, variables, &expanded_folder);
    const std::string file_path_utf8 = WideToUtf8(file_path.wstring());

    nlohmann::json plan;
    std::string error;
    if (!LoadPlannerDocument(file_path, &plan, &error)) {
        return ErrorResult(error);
    }

    bool changed = false;
    if (action == "get") {
        return PlannerResult(action, file_path_utf8, plan, false);
    }
    if (action == "clear") {
        const std::string section = Trim(args.value("section", "all"));
        if (section.empty() || section == "all") {
            plan = EmptyPlannerDocument();
        } else {
            if (!IsPlannerSection(section)) return ErrorResult("Invalid planner section: " + section);
            plan[section] = nlohmann::json::array();
        }
        changed = true;
    } else if (action == "create" || action == "replace") {
        if (!args.contains("plan") || !args["plan"].is_object()) {
            return ErrorResult("Planner create/replace requires a plan object.");
        }
        plan = args["plan"];
        changed = true;
    } else if (action == "update") {
        if (!args.contains("plan") || !args["plan"].is_object()) {
            return ErrorResult("Planner update requires a plan object.");
        }
        for (auto it = args["plan"].begin(); it != args["plan"].end(); ++it) {
            plan[it.key()] = it.value();
        }
        changed = true;
    } else if (action == "add_item") {
        const std::string section = Trim(args.value("section", "steps"));
        if (!IsPlannerSection(section) || section == "all") return ErrorResult("Invalid planner section: " + section);
        if (!args.contains("item") || !args["item"].is_object()) {
            return ErrorResult("Planner add_item requires an item object.");
        }
        nlohmann::json item = args["item"];
        if (args.contains("parent_id")) {
            const std::string parent_section = Trim(args.value("parent_section", "all"));
            if (!parent_section.empty() && parent_section != "all" && !IsPlannerSection(parent_section)) {
                return ErrorResult("Invalid planner parent_section: " + parent_section);
            }
            nlohmann::json* parent = FindPlannerItemInPlan(plan, args["parent_id"], parent_section);
            if (!parent) {
                return ErrorResult("Planner parent item not found.");
            }
            std::string child_section = Trim(args.value("child_section", ""));
            if (child_section.empty()) {
                child_section = section == "goals" ? "subgoals" : section;
            }
            if (!IsPlannerChildSection(child_section)) {
                return ErrorResult("Invalid planner child_section: " + child_section);
            }
            if (!parent->contains(child_section) || !(*parent)[child_section].is_array()) {
                (*parent)[child_section] = nlohmann::json::array();
            }
            PreparePlannerItem(item, child_section, plan);
            (*parent)[child_section].push_back(std::move(item));
        } else {
            if (!plan.contains(section) || !plan[section].is_array()) {
                plan[section] = nlohmann::json::array();
            }
            PreparePlannerItem(item, section, plan);
            plan[section].push_back(std::move(item));
        }
        changed = true;
    } else if (action == "update_item" || action == "remove_item") {
        const std::string section = Trim(args.value("section", "steps"));
        if (section != "all" && !IsPlannerSection(section)) return ErrorResult("Invalid planner section: " + section);
        if (!args.contains("id")) return ErrorResult("Planner " + action + " requires an id.");
        if (action == "remove_item") {
            if (!RemovePlannerItemFromPlan(plan, args["id"], section)) {
                return ErrorResult("Planner item not found in section " + section + ".");
            }
            changed = true;
        } else {
            nlohmann::json* item = FindPlannerItemInPlan(plan, args["id"], section);
            if (!item) return ErrorResult("Planner item not found in section " + section + ".");
            if (!args.contains("item") || !args["item"].is_object()) {
                return ErrorResult("Planner update_item requires an item object with fields to update.");
            }
            for (auto field = args["item"].begin(); field != args["item"].end(); ++field) {
                (*item)[field.key()] = field.value();
            }
            changed = true;
        }
    } else {
        return ErrorResult("Unknown planner action: " + action);
    }

    if (changed) {
        plan["updated_at"] = CurrentTimestampUtc();
        if (!SavePlannerDocument(file_path, plan, &error)) {
            return ErrorResult(error);
        }
    }
    return PlannerResult(action, file_path_utf8, plan, changed);
}

inline bool IsValidRelativePath(const std::string& path) {
    if (path.empty()) return false;
    if (path.find("..") != std::string::npos) return false;
    if (path.find(':') != std::string::npos) return false;
    return true;
}

inline std::filesystem::path ResolveFilesystemPath(
    const std::string& path_template,
    const std::string& working_directory_template,
    const std::vector<ProjectMcpVariableValue>& variables) {
    std::string expanded = variable_resolver::ExpandTemplate(
        working_directory_template.empty() ? "$ProjectFolder$" : working_directory_template,
        variables);
    expanded = Trim(expanded);
    if (expanded.empty()) expanded = "$ProjectFolder$";
    std::filesystem::path root(Utf8ToWide(expanded));
    std::string rel = Trim(path_template);
    rel = variable_resolver::ExpandTemplate(rel, variables);
    rel = Trim(rel);
    std::string root_utf8 = WideToUtf8(root.wstring());
    if (!root_utf8.empty() && rel.rfind(root_utf8, 0) == 0) {
        rel = rel.substr(root_utf8.size());
        while (!rel.empty() && (rel.front() == '\\' || rel.front() == '/')) rel = rel.substr(1);
    }
    if (rel.empty()) return root;
    std::filesystem::path target = root / Utf8ToWide(rel);
    return target;
}

inline bool ReadWholeFileUtf8(const std::filesystem::path& path, std::string* out, std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        if (error) *error = "Could not open file for reading: " + WideToUtf8(path.wstring());
        return false;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    *out = stream.str();
    return true;
}

inline bool WriteWholeFileUtf8(const std::filesystem::path& path, const std::string& content, std::string* error) {
    try {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            if (error) *error = "Could not open file for writing: " + WideToUtf8(path.wstring());
            return false;
        }
        output << content;
        return true;
    } catch (const std::exception& ex) {
        if (error) *error = std::string("Could not write file: ") + ex.what();
        return false;
    }
}

inline bool CreateFilesystemBackup(
    const std::filesystem::path& target,
    const std::filesystem::path& project_root,
    std::string* backup_rel_path_out,
    std::string* error) {
    try {
        std::error_code ec;
        const auto now = std::chrono::system_clock::now();
        const auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm utc{};
        gmtime_s(&utc, &time_t_now);
        std::ostringstream ts;
        ts << std::put_time(&utc, "%Y%m%d_%H%M%S");
        const std::string timestamp = ts.str();
        std::filesystem::path backup_root = project_root / ".agent" / "backups" / Utf8ToWide(timestamp);
        std::filesystem::path rel;
        if (target.wstring().rfind(project_root.wstring(), 0) == 0) {
            rel = std::filesystem::relative(target, project_root, ec);
        }
        if (ec || rel.empty()) {
            rel = target.filename();
        }
        std::filesystem::path backup_dest = backup_root / rel;
        std::filesystem::create_directories(backup_dest.parent_path(), ec);
        if (ec) {
            if (error) *error = "Failed to create backup directory: " + ec.message();
            return false;
        }
        if (std::filesystem::is_directory(target, ec)) {
            std::filesystem::copy(target, backup_dest,
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                if (error) *error = "Failed to backup directory: " + ec.message();
                return false;
            }
        } else {
            std::filesystem::copy_file(target, backup_dest,
                std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                if (error) *error = "Failed to backup file: " + ec.message();
                return false;
            }
        }
        if (backup_rel_path_out) {
            *backup_rel_path_out = WideToUtf8((std::filesystem::path(".agent") / "backups" / Utf8ToWide(timestamp) / rel).wstring());
        }
        return true;
    } catch (const std::exception& ex) {
        if (error) *error = std::string("Backup error: ") + ex.what();
        return false;
    }
}

inline std::string TrimRightCopy(std::string value) {
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

inline std::string LeadingWhitespace(const std::string& value) {
    size_t count = 0;
    while (count < value.size() && (value[count] == ' ' || value[count] == '\t')) {
        ++count;
    }
    return value.substr(0, count);
}

inline bool StartsWithString(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

inline std::string RemoveIndentPrefix(const std::string& value, const std::string& indent) {
    if (!indent.empty() && StartsWithString(value, indent)) {
        return value.substr(indent.size());
    }
    return value;
}

inline std::string CollapseWhitespaceForMatch(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    bool in_space = false;
    for (unsigned char ch : value) {
        if (ch == ' ' || ch == '\t' || ch == '\r') {
            in_space = true;
            continue;
        }
        if (in_space && !out.empty()) {
            out.push_back(' ');
        }
        out.push_back(static_cast<char>(ch));
        in_space = false;
    }
    return TrimRightCopy(out);
}

inline bool IsBlankLineForMatch(const std::string& value) {
    return TrimRightCopy(value).find_first_not_of(" \t\r") == std::string::npos;
}

inline size_t FirstNonBlankLineIndex(const std::vector<std::string>& lines) {
    for (size_t i = 0; i < lines.size(); ++i) {
        if (!IsBlankLineForMatch(lines[i])) {
            return i;
        }
    }
    return lines.size();
}

inline bool IsIndentationSensitiveFile(const std::filesystem::path& path) {
    const std::string ext = LowerAsciiCopy(WideToUtf8(path.extension().wstring()));
    const std::string filename = LowerAsciiCopy(WideToUtf8(path.filename().wstring()));
    return ext == ".py" || ext == ".pyw" ||
           ext == ".yml" || ext == ".yaml" ||
           filename == "makefile" || filename == "gnumakefile" ||
           ext == ".mk";
}

inline std::vector<std::string> ReindentReplacementLines(
    const std::vector<std::string>& new_lines,
    const std::string& old_base_indent,
    const std::string& candidate_base_indent) {
    if (old_base_indent == candidate_base_indent) {
        return new_lines;
    }

    std::vector<std::string> adjusted;
    adjusted.reserve(new_lines.size());
    for (const auto& line : new_lines) {
        if (IsBlankLineForMatch(line)) {
            adjusted.push_back(line);
        } else if (old_base_indent.empty()) {
            adjusted.push_back(candidate_base_indent + line);
        } else if (StartsWithString(line, old_base_indent)) {
            adjusted.push_back(candidate_base_indent + line.substr(old_base_indent.size()));
        } else {
            adjusted.push_back(line);
        }
    }
    return adjusted;
}

struct FilesystemEditMatch {
    bool found = false;
    size_t index = 0;
    std::vector<std::string> replacement_lines;
    std::string mode;
};

inline bool RelativeIndentBlockMatches(
    const std::vector<std::string>& file_lines,
    size_t start,
    const std::vector<std::string>& old_lines,
    bool collapse_inner_whitespace,
    std::string* old_base_indent,
    std::string* candidate_base_indent) {
    const size_t first_old = FirstNonBlankLineIndex(old_lines);
    if (first_old >= old_lines.size()) {
        return false;
    }

    const std::string old_base = LeadingWhitespace(old_lines[first_old]);
    const std::string candidate_base = LeadingWhitespace(file_lines[start + first_old]);
    for (size_t j = 0; j < old_lines.size(); ++j) {
        if (IsBlankLineForMatch(old_lines[j]) && IsBlankLineForMatch(file_lines[start + j])) {
            continue;
        }
        const std::string old_rel = RemoveIndentPrefix(old_lines[j], old_base);
        const std::string candidate_rel = RemoveIndentPrefix(file_lines[start + j], candidate_base);
        if (collapse_inner_whitespace) {
            if (CollapseWhitespaceForMatch(old_rel) != CollapseWhitespaceForMatch(candidate_rel)) {
                return false;
            }
        } else if (TrimRightCopy(old_rel) != TrimRightCopy(candidate_rel)) {
            return false;
        }
    }

    if (old_base_indent) *old_base_indent = old_base;
    if (candidate_base_indent) *candidate_base_indent = candidate_base;
    return true;
}

inline FilesystemEditMatch FindFilesystemEditMatch(
    const std::vector<std::string>& file_lines,
    const std::vector<std::string>& old_lines,
    const std::vector<std::string>& new_lines,
    const std::filesystem::path& target_path) {
    FilesystemEditMatch result;
    if (old_lines.empty() || old_lines.size() > file_lines.size()) {
        return result;
    }

    const bool indentation_sensitive = IsIndentationSensitiveFile(target_path);
    for (size_t i = 0; i + old_lines.size() <= file_lines.size(); ++i) {
        bool exact = true;
        for (size_t j = 0; j < old_lines.size(); ++j) {
            if (file_lines[i + j] != old_lines[j]) {
                exact = false;
                break;
            }
        }
        if (exact) {
            result.found = true;
            result.index = i;
            result.replacement_lines = new_lines;
            result.mode = "exact";
            return result;
        }
    }

    for (size_t i = 0; i + old_lines.size() <= file_lines.size(); ++i) {
        bool trailing = true;
        for (size_t j = 0; j < old_lines.size(); ++j) {
            if (TrimRightCopy(file_lines[i + j]) != TrimRightCopy(old_lines[j])) {
                trailing = false;
                break;
            }
        }
        if (trailing) {
            result.found = true;
            result.index = i;
            result.replacement_lines = new_lines;
            result.mode = "trailing_whitespace";
            return result;
        }
    }

    for (size_t i = 0; i + old_lines.size() <= file_lines.size(); ++i) {
        std::string old_base;
        std::string candidate_base;
        if (RelativeIndentBlockMatches(
                file_lines, i, old_lines, false, &old_base, &candidate_base)) {
            result.found = true;
            result.index = i;
            result.replacement_lines = ReindentReplacementLines(new_lines, old_base, candidate_base);
            result.mode = "relative_indentation";
            return result;
        }
    }

    if (!indentation_sensitive) {
        for (size_t i = 0; i + old_lines.size() <= file_lines.size(); ++i) {
            std::string old_base;
            std::string candidate_base;
            if (RelativeIndentBlockMatches(
                    file_lines, i, old_lines, true, &old_base, &candidate_base)) {
                result.found = true;
                result.index = i;
                result.replacement_lines = ReindentReplacementLines(new_lines, old_base, candidate_base);
                result.mode = "loose_whitespace";
                return result;
            }
        }
    }

    return result;
}

inline McpToolCallResult CallFilesystem(
    const std::string& arguments_json,
    const ProjectSettings& settings,
    const std::vector<ProjectMcpVariableValue>& variables) {
    nlohmann::json args;
    try {
        args = nlohmann::json::parse(arguments_json.empty() ? "{}" : arguments_json);
    } catch (const std::exception& ex) {
        return ErrorResult(std::string("Invalid filesystem tool arguments: ") + ex.what());
    }
    const std::string action = Trim(args.value("action", ""));
    const std::string path_template = Trim(args.value("path", ""));
    if (action.empty()) return ErrorResult("Filesystem tool requires an action.");
    if (path_template.empty()) return ErrorResult("Filesystem tool requires a path.");
    if (!IsValidRelativePath(path_template)) return ErrorResult("Filesystem path contains invalid characters or parent-directory references (..).");
    std::filesystem::path target = ResolveFilesystemPath(path_template, settings.built_in_filesystem_working_directory, variables);
    std::error_code ec;
    const bool target_exists = std::filesystem::exists(target, ec);
    const bool target_is_dir = target_exists && std::filesystem::is_directory(target, ec);
    const bool create_backup = args.value("create_backup", false);
    std::string expanded_root = variable_resolver::ExpandTemplate(
        settings.built_in_filesystem_working_directory.empty() ? "$ProjectFolder$" : settings.built_in_filesystem_working_directory,
        variables);
    expanded_root = Trim(expanded_root);
    if (expanded_root.empty()) expanded_root = "$ProjectFolder$";
    std::filesystem::path project_root = std::filesystem::weakly_canonical(std::filesystem::path(Utf8ToWide(expanded_root)), ec);
    if (action == "read") {
        if (!target_exists) return ErrorResult("File not found: " + path_template);
        if (target_is_dir) return ErrorResult("Cannot read a directory as a file. Use list_directory instead.");
        std::string content;
        std::string read_error;
        if (!ReadWholeFileUtf8(target, &content, &read_error)) return ErrorResult(read_error);
        int start_line = args.value("start_line", 0);
        int end_line = args.value("end_line", 0);
        int start_offset = args.value("start_offset", -1);
        int length = args.value("length", -1);
        if (start_line > 0 || end_line > 0) {
            std::vector<std::string> lines;
            std::istringstream stream(content);
            std::string line;
            while (std::getline(stream, line)) lines.push_back(line);
            if (start_line < 1) start_line = 1;
            if (end_line < 1 || end_line > static_cast<int>(lines.size())) end_line = static_cast<int>(lines.size());
            std::ostringstream out;
            for (int i = start_line - 1; i < end_line; ++i) {
                if (i > start_line - 1) out << "\n";
                out << lines[i];
            }
            content = out.str();
        } else if (start_offset >= 0 && length >= 0) {
            if (start_offset < static_cast<int>(content.size())) {
                content = content.substr(start_offset, static_cast<size_t>(length));
            } else { content.clear(); }
        }
        McpToolCallResult result;
        result.success = true;
        result.raw_result_json = nlohmann::json({ {"tool", kFilesystemToolName}, {"success", true}, {"action", "read"}, {"path", path_template}, {"content", content} }).dump(2);
        result.content_text = "Read file: " + path_template + "\n\n" + content;
        return result;
    }
    if (action == "list_directory") {
        if (!target_exists) return ErrorResult("Directory not found: " + path_template);
        if (!target_is_dir) return ErrorResult("Path is not a directory: " + path_template);
        nlohmann::json entries = nlohmann::json::array();
        for (const auto& entry : std::filesystem::directory_iterator(target, ec)) {
            if (ec) continue;
            entries.push_back(nlohmann::json({ {"name", WideToUtf8(entry.path().filename().wstring())}, {"is_directory", entry.is_directory(ec)} }));
        }
        McpToolCallResult result;
        result.success = true;
        result.raw_result_json = nlohmann::json({ {"tool", kFilesystemToolName}, {"success", true}, {"action", "list_directory"}, {"path", path_template}, {"entries", entries} }).dump(2);
        result.content_text = "Directory listing: " + path_template + "\n" + entries.dump(2);
        return result;
    }
    if (action == "create_directory") {
        std::filesystem::create_directories(target, ec);
        if (ec) return ErrorResult("Failed to create directory: " + path_template + " — " + ec.message());
        McpToolCallResult result;
        result.success = true;
        result.raw_result_json = nlohmann::json({ {"tool", kFilesystemToolName}, {"success", true}, {"action", "create_directory"}, {"path", path_template} }).dump(2);
        result.content_text = "Created directory: " + path_template;
        return result;
    }
    std::string backup_rel;
    if (create_backup && target_exists) {
        std::string backup_error;
        if (!CreateFilesystemBackup(target, project_root, &backup_rel, &backup_error)) return ErrorResult("Backup failed: " + backup_error);
    }
    if (action == "write") {
        const std::string content = args.value("content", "");
        std::string write_error;
        if (!WriteWholeFileUtf8(target, content, &write_error)) return ErrorResult(write_error);
        McpToolCallResult result;
        result.success = true;
        nlohmann::json payload = { {"tool", kFilesystemToolName}, {"success", true}, {"action", "write"}, {"path", path_template}, {"bytes_written", static_cast<int>(content.size())} };
        if (!backup_rel.empty()) payload["backup_path"] = backup_rel;
        result.raw_result_json = payload.dump(2);
        result.content_text = "Wrote file: " + path_template;
        if (!backup_rel.empty()) result.content_text += " (backup: " + backup_rel + ")";
        return result;
    }
    if (action == "edit") {
        if (!target_exists || target_is_dir) return ErrorResult("Edit target must be an existing file: " + path_template);
        std::string original;
        std::string read_error;
        if (!ReadWholeFileUtf8(target, &original, &read_error)) return ErrorResult(read_error);
        const auto edits = args.value("edits", nlohmann::json::array());
        if (!edits.is_array() || edits.empty()) return ErrorResult("Edit action requires a non-empty 'edits' array.");
        std::vector<std::string> lines;
        { std::istringstream stream(original); std::string line; while (std::getline(stream, line)) lines.push_back(line); }
        std::vector<std::string> match_modes;
        for (const auto& edit : edits) {
            if (!edit.is_object()) continue;
            const auto old_j = edit.value("old_lines", nlohmann::json::array());
            const auto new_j = edit.value("new_lines", nlohmann::json::array());
            if (!old_j.is_array() || old_j.empty()) continue;
            std::vector<std::string> old_lines; for (const auto& item : old_j) { if (item.is_string()) old_lines.push_back(item.get<std::string>()); }
            std::vector<std::string> new_lines; for (const auto& item : new_j) { if (item.is_string()) new_lines.push_back(item.get<std::string>()); }
            if (old_lines.empty()) continue;
            const FilesystemEditMatch match = FindFilesystemEditMatch(lines, old_lines, new_lines, target);
            if (!match.found) {
                return ErrorResult(
                    "Edit failed: old_lines not found in file. Tried exact and safe whitespace-tolerant matching. "
                    "For indentation-sensitive files, leading indentation must still match by relative block structure.");
            }
            lines.erase(lines.begin() + match.index, lines.begin() + match.index + old_lines.size());
            lines.insert(lines.begin() + match.index, match.replacement_lines.begin(), match.replacement_lines.end());
            match_modes.push_back(match.mode);
        }
        std::ostringstream out;
        for (size_t i = 0; i < lines.size(); ++i) { if (i > 0) out << "\n"; out << lines[i]; }
        std::string write_error;
        if (!WriteWholeFileUtf8(target, out.str(), &write_error)) return ErrorResult(write_error);
        McpToolCallResult result;
        result.success = true;
        nlohmann::json payload = { {"tool", kFilesystemToolName}, {"success", true}, {"action", "edit"}, {"path", path_template} };
        payload["match_modes"] = match_modes;
        if (!backup_rel.empty()) payload["backup_path"] = backup_rel;
        result.raw_result_json = payload.dump(2);
        result.content_text = "Edited file: " + path_template;
        if (std::any_of(match_modes.begin(), match_modes.end(), [](const std::string& mode) { return mode != "exact"; })) {
            result.content_text += " (used whitespace-tolerant matching)";
        }
        if (!backup_rel.empty()) result.content_text += " (backup: " + backup_rel + ")";
        return result;
    }
    return ErrorResult("Unknown filesystem action: " + action);
}

inline McpToolCallResult CallTool(
    const std::string& name,
    const std::string& arguments_json,
    const ProjectSettings& settings,
    const std::vector<ProjectMcpVariableValue>& variables,
    const std::string& current_agentic_mode_id = {},
    std::function<McpToolCallResult(const std::string&)> questionnaire_wait = {}) {
    if (name == kPowerShellToolName && settings.built_in_powershell_enabled) {
        return CallPowerShell(arguments_json, settings.built_in_powershell_working_directory, variables);
    }
    if (name == kPlannerToolName && settings.built_in_planner_enabled) {
        return CallPlanner(arguments_json, settings, variables);
    }
    if (name == kCompletionDriverToolName && IsCompletionDriverEnabled(settings, current_agentic_mode_id)) {
        return CallCompletionDriver(arguments_json);
    }
    if (name == kQuestionnaireToolName && IsQuestionnaireEnabled(settings, current_agentic_mode_id)) {
        return CallQuestionnaire(arguments_json, questionnaire_wait);
    }
    if (name == kFilesystemToolName && settings.built_in_filesystem_enabled) {
        return CallFilesystem(arguments_json, settings, variables);
    }
    return ErrorResult("Built-in tool is not enabled for this project: " + name);
}

}  // namespace built_in_tools
