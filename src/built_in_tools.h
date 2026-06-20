#pragma once

#include "mcp_manager.h"
#include "openai_client.h"
#include "types.h"
#include "util.h"
#include "variable_resolver.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>
#include <windows.h>
#include <combaseapi.h>
#include <UIAutomation.h>
#include <wrl/client.h>

namespace built_in_tools {

inline constexpr const char* kPowerShellToolName = "powershell_execute";
inline constexpr const char* kQuestionnaireToolName = "user_questionnaire";
inline constexpr const char* kPlannerToolName = "project_planner";
inline constexpr const char* kCompletionDriverToolName = "completion_driver";
inline constexpr const char* kCompletionDriverContinuationPrefix = "[Completion Driver generated continuation prompt]";
inline constexpr const char* kDefaultPlannerStorageFolder = "$ProjectFolder$\\.agent";
inline constexpr const char* kPlannerFileName = "planner.json";
inline constexpr const char* kFilesystemToolName = "project_filesystem";
inline constexpr const char* kSleepToolName = "sleep_seconds";
inline constexpr const char* kBrowserSearchToolName = "browser_web_search";
inline constexpr const char* kWindowAutomationToolName = "window_automation";

inline bool IsBuiltInToolName(const std::string& name) {
    return name == kPowerShellToolName || name == kQuestionnaireToolName ||
           name == kPlannerToolName || name == kCompletionDriverToolName ||
           name == kFilesystemToolName || name == kSleepToolName ||
           name == kBrowserSearchToolName || name == kWindowAutomationToolName;
}

inline std::string TraceTitleForBuiltInTool(const std::string& name) {
    if (name == kPowerShellToolName) return "Built-in / PowerShell";
    if (name == kQuestionnaireToolName) return "Built-in / User Questionnaire";
    if (name == kPlannerToolName) return "Built-in / Planner";
    if (name == kCompletionDriverToolName) return "Built-in / Completion Driver";
    if (name == kFilesystemToolName) return "Built-in / Project Filesystem";
    if (name == kSleepToolName) return "Built-in / Sleep / Pause";
    if (name == kBrowserSearchToolName) return "Built-in / Browser Web Search";
    if (name == kWindowAutomationToolName) return "Built-in / Window Automation";
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
        "- read: Returns UTF-8 text file content. Optionally pass start_line / end_line (1-based, inclusive) or start_offset / length (bytes).\n"
        "- Do not read binary files such as .jar, .zip, .png, .jpg, .ico, .exe, .dll, .pdf, or generated build artifacts as text. Use list_directory to identify them; binary/non-UTF-8 files return a clear tool error instead of content.\n"
        "- write: Overwrites a file. Pass create_backup=true to snapshot the existing file into .agent/backups/<timestamp>/<path> before overwriting.\n"
        "- edit: Applies JSON diff edits. Each edit object uses either:\n"
        "  1) old_lines + new_lines — match-and-replace by contiguous lines; each array entry may be one line or a multiline block, or\n"
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
        "- The powershell_execute tool runs a non-interactive Windows PowerShell command line on the host machine via powershell.exe, not Bash, sh, cmd.exe, or PowerShell 7.\n"
        "- This is a high-risk tool. Use it ONLY when the user explicitly needs command execution.\n"
        "- Prefer the project_filesystem tool for reading, writing, or editing files instead of PowerShell.\n"
        "   * Use powershell_execute for: dependency installs, build scripts, git commands, environment checks.\n"
        "   * Use project_filesystem for: reading source code, editing files, listing directories.\n"
        "- Write valid Windows PowerShell 5.1 syntax. Avoid Bash-only constructs such as `&&`, `||`, `cat <<EOF`, `grep`, `sed`, `awk`, `export NAME=value`, `VAR=value command`, `rm -rf`, `cp -r`, and `/tmp/...` paths.\n"
        "- For multiple commands, use semicolons or newlines in the JSON string. If the second command must depend on the first, check `$LASTEXITCODE` or use PowerShell control flow.\n"
        "- Use `$env:NAME` for environment variables, `Get-ChildItem`/`Select-String`/`Remove-Item` for shell operations, `Join-Path` for paths, and `-LiteralPath` for user-provided paths.\n"
        "- To run a batch/cmd-only command, explicitly use `cmd /c \"...\"`. To run an executable or script path with spaces, use the call operator: `& 'C:\\Path With Spaces\\tool.exe' arg1`.\n"
        "- This tool has no interactive stdin. Do not run commands that wait for prompts; pass non-interactive flags instead.\n"
        "- Provide concise commands. Avoid destructive operations unless explicitly requested.\n"
        "- Set timeout_seconds (1-120) if the command may take long; default is 30s.\n"
        "- The tool returns stdout, stderr, exit code, and timed_out status. Do not silently swallow errors.\n"
        "- If a command fails, analyze the error output rather than blindly retrying.\n"
        "Examples:\n"
        "  Install dependency: {\"command\":\"npm install\",\"timeout_seconds\":60}\n"
        "  Check git status: {\"command\":\"git status\"}\n"
        "  Build project: {\"command\":\"cmake --build build --config Release\",\"timeout_seconds\":120}\n"
        "  Conditional command: {\"command\":\"npm test; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }\\nnpm run build\",\"timeout_seconds\":120}\n"
        "  Search text: {\"command\":\"Get-ChildItem -Recurse -File -Filter *.cpp | Select-String -Pattern 'CompletionDriver'\",\"timeout_seconds\":60}");
}

inline std::string SleepSystemPrompt(int max_sleep_seconds = 0) {
    std::ostringstream text;
    text
        << "Sleep / Pause Instructions:\n"
        << "- The sleep_seconds tool pauses the current agent/tool loop for a requested number of seconds, then returns control so you can continue.\n"
        << "- Use it when you have started or requested background work and need to wait briefly before checking status, reading output, polling a file, or continuing a sequence.\n"
        << "- The tool only waits. It does not start work in parallel, monitor a process for you, or prove that external work has completed.\n"
        << "- Keep waits purposeful and short when possible, then follow the sleep with a concrete status check or next action.\n";
    if (max_sleep_seconds > 0) {
        text << "- This project limits each sleep_seconds call to "
             << max_sleep_seconds
             << " seconds. Do not request more than that in one call.";
    } else {
        text << "- This project has no configured maximum sleep duration. Choose a reasonable duration for the current task.";
    }
    return text.str();
}

inline std::string BrowserSearchEngineList(const ProjectSettings& settings) {
    std::vector<std::string> engines;
    const auto add_engine = [&](const std::string& engine) {
        if (engine == "google" && !settings.browser_search_google_enabled) return;
        if (engine == "bing" && !settings.browser_search_bing_enabled) return;
        if (std::find(engines.begin(), engines.end(), engine) == engines.end()) {
            engines.push_back(engine);
        }
    };
    for (const auto& engine : settings.browser_search_engine_order) {
        add_engine(LowerAsciiCopy(engine));
    }
    add_engine("google");
    add_engine("bing");
    if (engines.empty()) engines.push_back("google");

    std::ostringstream text;
    for (size_t i = 0; i < engines.size(); ++i) {
        if (i > 0) text << ", ";
        text << engines[i];
    }
    return text.str();
}

inline std::string BrowserSearchSystemPrompt(const ProjectSettings& settings) {
    std::ostringstream text;
    const std::string description = Trim(settings.browser_search_context_description).empty()
        ? std::string(kDefaultBrowserSearchDescription)
        : Trim(settings.browser_search_context_description);
    text
        << "Browser Web Search Instructions:\n"
        << "- " << description << "\n"
        << "- Tool name: " << kBrowserSearchToolName << ". Actions: search, fetch, search_and_fetch.\n"
        << "- Available engines in configured priority order: " << BrowserSearchEngineList(settings) << ". "
        << "Default engine: " << (Trim(settings.browser_search_default_engine).empty()
            ? std::string("google")
            : LowerAsciiCopy(settings.browser_search_default_engine)) << ".\n"
        << "- Use search for broad discovery. Use fetch when the user supplied a URL or when a search result must be read before answering. Use search_and_fetch when you already know you need the content from the best matching search result.\n"
        << "- Tool call JSON examples:\n"
        << "  - Search only: {\"action\":\"search\",\"query\":\"topic or question\",\"engine\":\"default\",\"result_count\":8}\n"
        << "  - Search and read one result: {\"action\":\"search_and_fetch\",\"query\":\"topic or question\",\"fetch_result_index\":1,\"content_type\":\"text\"}\n"
        << "  - Fetch a known URL: {\"action\":\"fetch\",\"url\":\"https://example.com/page\",\"content_type\":\"text\"}\n"
        << "- Valid content_type values are text, html, text_html, pdf, and all. Use engine google or bing only when a specific engine is needed; otherwise use default so the configured priority is honored.\n"
        << "- The tool can return search results, rendered page text, rendered HTML, or a saved PDF path. Request text for ordinary research, HTML when exact page markup matters, and PDF when the user needs a durable browser-rendered copy.\n"
        << "- The browser is " << (settings.browser_search_open_visual_browser ? "visible by default" : "headless by default")
        << "; override open_visual_browser only when visual inspection, login, cookie consent, or troubleshooting requires it.\n";
    if (settings.browser_search_primary) {
        text << "- This project marks browser_web_search as the primary web search tool. Prefer it for broad search and research loops. Use DuckDuckGo/web MCP as a fallback, comparison source, or when it has a stronger fetch/download tool for a known URL.\n";
    } else {
        text << "- DuckDuckGo/web MCP and browser_web_search may both be available. Choose browser_web_search for slower human-paced Google/Bing browser searches, JavaScript-rendered content, visible-browser troubleshooting, and PDF capture. Choose DuckDuckGo when it is sufficient and faster for simple retrieval.\n";
    }
    text << "- Include source URLs in answers that rely on web research. Do not guess current or documentation-backed details when this tool can verify them.";
    return text.str();
}

inline std::string WindowAutomationSystemPrompt() {
    return (
        "Window Automation Instructions:\n"
        "- The window_automation tool uses native Windows UI Automation (UIA3 COM) and Win32 window APIs to inspect and interact with visible desktop applications.\n"
        "- Use list_windows first when you do not know the target title or handle. Use activate_window to bring a matching window forward. Use inspect_window to read its UI tree before clicking or typing.\n"
        "- WebView2 panes often expose only the host pane through UIA. For Microsoft.UI.Xaml.Controls.WebView2 or other Chromium WebView2 content, use the webview2_* actions, which attach through Chrome DevTools Protocol. The target app must have WebView2 remote debugging enabled before its WebView2 control is created, usually by launching it with WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=9222.\n"
        "- Tool call JSON examples:\n"
        "  - {\"action\":\"list_windows\",\"title_contains\":\"notepad\"}\n"
        "  - {\"action\":\"activate_window\",\"title_contains\":\"Settings\"}\n"
        "  - {\"action\":\"inspect_window\",\"title_contains\":\"Calculator\",\"max_depth\":5,\"max_elements\":200}\n"
        "  - {\"action\":\"click\",\"title_contains\":\"Calculator\",\"name\":\"Seven\",\"control_type\":\"Button\"}\n"
        "  - {\"action\":\"set_text\",\"title_contains\":\"Login\",\"automation_id\":\"usernameInput\",\"value\":\"user@example.com\"}\n"
        "  - {\"action\":\"webview2_list_targets\",\"debug_port\":9222}\n"
        "  - {\"action\":\"webview2_inspect\",\"debug_port\":9222,\"target_index\":0,\"max_elements\":200}\n"
        "  - {\"action\":\"webview2_click\",\"debug_port\":9222,\"selector\":\"button[data-testid='save']\"}\n"
        "  - {\"action\":\"webview2_set_text\",\"debug_port\":9222,\"label\":\"Email\",\"value\":\"user@example.com\"}\n"
        "- Prefer stable selectors in this order: hwnd, automation_id, exact name plus control_type, then element_index from a recent inspect_window result. Element indexes can change when the UI changes.\n"
        "- For WebView2 content, prefer CSS selector, label, placeholder, or exact DOM accessible name before using element_index from a recent webview2_inspect result.\n"
        "- For click, the tool tries UIA Invoke/Selection/Toggle patterns first, then a mouse click at the clickable point or element center. For set_text, it tries ValuePattern first, then focuses the element and types with keyboard input.\n"
        "- This tool manipulates the real desktop. Inspect before acting, avoid destructive clicks unless the user requested them, and report what window/element was targeted.");
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
        "- clear: Delete all items from a section (goals/features/steps/blockers/notes/tool_hints/phases) or the entire plan if section=all.\n"
        "- add_item: Append an item to a section. Optionally nest under a parent item via parent_id.\n"
        "- update_item: Modify fields of an existing item by id (e.g., mark status=completed).\n"
        "- remove_item: Delete an item by id from a section (or section=all to search all sections).\n"
        "Sections: goals, features, steps, blockers, notes, tool_hints, phases.\n"
        "- The project_planner file is the source of truth for task progress. Do not create a separate progress/plan document instead of using this tool unless the user explicitly asks for that deliverable.\n"
        "Status values for items: pending (not started), in_progress (active), completed (done), blocked (waiting), cancelled (abandoned).\n"
        "- When adding items, an id is auto-generated if omitted.\n"
        "- Full-plan create/update also normalizes missing ids. Phase steps get stable ids like p3s6 (phase 3, step 6).\n"
        "- child_section defaults to 'subgoals' for goals, otherwise the requested section.\n"
        "- To edit an existing item, always use update_item with the existing id.\n"
        "- If update_item or remove_item reports that an item was not found, do not retry the same id. Call get, inspect the returned ids, then update an existing id, add a missing item, or update the full plan.\n"
        "- The only way to get strikethrough in the UI is status=completed.\n"
        "Examples:\n"
        "  Load plan: {\"action\":\"get\"}\n"
        "  Create full plan: {\"action\":\"create\",\"plan\":{\"goal\":\"Build app\",\"goals\":[{\"id\":\"g1\",\"title\":\"Setup\",\"status\":\"pending\"}]}}\n"
        "  Create a phase plan: {\"action\":\"create\",\"plan\":{\"goal\":\"Build app\",\"phases\":[{\"id\":\"p1\",\"title\":\"Phase 1\",\"status\":\"pending\",\"steps\":[{\"id\":\"p1s1\",\"task\":\"Setup\",\"status\":\"pending\"}]}]}}\n"
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

inline int NormalizedCompletionDriverOverloadDelaySeconds(const ProjectSettings& settings) {
    return std::max(0, settings.completion_driver_overload_delay_seconds);
}

inline int NormalizedSleepMaxSeconds(const ProjectSettings& settings) {
    return std::max(0, settings.built_in_sleep_max_seconds);
}

inline bool IsTransientProviderOverloadError(const std::string& error) {
    const std::string lower = LowerAsciiCopy(error);
    return lower.find("temporarily overloaded") != std::string::npos ||
           lower.find("model overloaded") != std::string::npos ||
           lower.find("provider overloaded") != std::string::npos ||
           lower.find("server overloaded") != std::string::npos ||
           lower.find("api error 503") != std::string::npos ||
           lower.find("http 503") != std::string::npos ||
           lower.find("status 503") != std::string::npos ||
           lower.find("503 service unavailable") != std::string::npos ||
           lower.find("service unavailable") != std::string::npos ||
           lower.find("temporarily unavailable") != std::string::npos;
}

inline std::string CompletionDriverSystemPrompt(int max_continuations = 0) {
    std::ostringstream text;
    text
        << "Completion Driver Instructions:\n"
        << "- The Completion Driver is active for this agentic mode. The completion_driver tool is the required stop signal for the host loop.\n"
        << "- The host will keep the agent running by adding driver-generated continuation prompts until you call the completion_driver tool with completed=true or status=\"completed\"/\"done\".\n"
        << "- Treat any message beginning with " << kCompletionDriverContinuationPrefix
        << " as an internal continuation prompt generated by the Completion Driver, not as a new user request.\n"
        << "- Continue working across those prompts until the user's objective is fully complete.\n"
        << "- When the objective is complete and your next response would be the final answer, first call completion_driver with completed=true and a concise final_summary. After that, provide the final user-facing answer.\n"
        << "- Do not finish with only normal assistant text while this tool is active. A final answer without completed=true/status=done is treated as unfinished and will be continued.\n"
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
         << "If your previous response only contained hidden thinking/reasoning, do not continue with more hidden-only text; either call the concrete tool needed for the next action or call completion_driver if the objective is done. "
         << "If the user's objective is now fully complete, call completion_driver with completed=true/status=completed and then provide the final answer. This tool call is mandatory before the final answer while the Completion Driver is active. "
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
            "Execute a non-interactive Windows PowerShell 5.1 command line on the host machine via powershell.exe -EncodedCommand. "
            "This is not Bash/sh/cmd.exe. Use valid PowerShell syntax: semicolons or newlines for sequencing, $env:NAME for environment variables, "
            "Get-ChildItem/Select-String/Remove-Item instead of ls -la/grep/rm -rf, -LiteralPath for user-provided paths, "
            "and cmd /c only when intentionally running a cmd/batch command. Prefer project_filesystem for reading, writing, editing, or listing project files. "
            "This is a high-risk built-in tool: avoid destructive actions unless explicitly requested, and report stdout/stderr back to the user.";
        tool.parameters_json = R"({
  "type": "object",
  "properties": {
    "command": {"type": "string", "description": "Windows PowerShell 5.1 command to execute with powershell.exe -EncodedCommand. Do not use Bash-only syntax such as &&, ||, cat <<EOF, grep, sed, awk, export NAME=value, VAR=value command, rm -rf, cp -r, or /tmp paths. For multiple commands use semicolons or newline escapes (\\n); for dependent steps check $LASTEXITCODE or use if/throw/exit. Use cmd /c only for cmd/batch-specific commands."},
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
            "Sections: goals, features, steps, blockers, notes, tool_hints, phases\n"
            "The project_planner file is the source of truth for task progress. Do not create a separate progress/plan document "
            "instead of using this tool unless the user explicitly asks for that deliverable.\n"
            "Status values (for item.status):\n"
            "- pending — Not started (unchecked in UI)\n"
            "- in_progress — Currently active (blue badge)\n"
            "- completed — Done. Checkbox checked + title struck through in UI.\n"
            "- blocked — Waiting on a blocker (red badge)\n"
            "- cancelled — Abandoned (grey badge, no strikethrough)\n\n"
            "Examples:\n"
            "1) Load plan: {\"action\":\"get\"}\n"
            "2) Create/replace full plan: {\"action\":\"create\",\"plan\":{\"goal\":\"Build app\",\"goals\":[{\"id\":\"g1\",\"title\":\"Setup\",\"status\":\"pending\"}]}}\n"
            "3) Create/replace a phase plan: {\"action\":\"create\",\"plan\":{\"goal\":\"Build app\",\"phases\":[{\"id\":\"p1\",\"title\":\"Phase 1\",\"status\":\"pending\",\"steps\":[{\"id\":\"p1s1\",\"task\":\"Setup\",\"status\":\"pending\"}]}]}}\n"
            "4) Add a top-level step: {\"action\":\"add_item\",\"section\":\"steps\",\"item\":{\"task\":\"Install deps\",\"status\":\"pending\"}}\n"
            "5) Add a nested subgoal under parent g1: {\"action\":\"add_item\",\"section\":\"goals\",\"parent_id\":\"g1\",\"item\":{\"title\":\"Subtask\",\"status\":\"pending\"}}\n"
            "6) Check off / mark completed: {\"action\":\"update_item\",\"section\":\"all\",\"id\":\"s1\",\"item\":{\"status\":\"completed\"}}\n"
            "7) Mark in progress: {\"action\":\"update_item\",\"section\":\"all\",\"id\":\"s1\",\"item\":{\"status\":\"in_progress\"}}\n"
            "8) Cancel / abandon: {\"action\":\"update_item\",\"section\":\"all\",\"id\":\"s1\",\"item\":{\"status\":\"cancelled\"}}\n"
            "9) Remove an item: {\"action\":\"remove_item\",\"section\":\"all\",\"id\":\"s1\"}\n\n"
            "Notes:\n"
            "- When adding items, an id is auto-generated if omitted.\n"
            "- Full-plan create/update also normalizes missing ids. Phase steps get stable ids like p3s6 (phase 3, step 6).\n"
            "- child_section defaults to 'subgoals' for goals, otherwise the requested section.\n"
            "- To edit an existing item, always use update_item with the existing id.\n"
            "- If update_item or remove_item reports that an item was not found, do not retry the same id. Call get, inspect the returned ids, then update an existing id, add a missing item, or update the full plan.\n"
            "- The only way to get strikethrough in the UI is status=completed.\n"
            "- 'create' is an alias for 'replace' and is the preferred action when establishing a new plan.";
        planner.parameters_json = R"({
  "type": "object",
  "properties": {
    "action": {"type": "string", "enum": ["get", "create", "replace", "update", "clear", "add_item", "update_item", "remove_item"], "description": "Planner operation to perform. Use 'get' to read, 'create' or 'replace' to write a full plan, 'update' to merge fields, 'add_item' to append, 'update_item' to edit by id, 'remove_item' to delete by id, 'clear' to empty a section or all."},
    "section": {"type": "string", "enum": ["goals", "features", "steps", "blockers", "notes", "tool_hints", "phases", "all"], "description": "Plan section for item operations or section clears."},
    "id": {"description": "Item id for update_item or remove_item. String or number."},
    "parent_id": {"description": "Optional parent item id for add_item. Use this to add nested subgoals or nested steps."},
    "parent_section": {"type": "string", "enum": ["goals", "features", "steps", "blockers", "notes", "phases", "all"], "description": "Optional section to search for parent_id. Defaults to all searchable sections."},
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
            "Required stop/continue signal for the current user objective. When this tool is active, the host treats "
            "normal assistant text without a completed=true/status=done tool call as unfinished and may send another "
            "Continuation Driver prompt. Call completion_driver with completed=true (or status completed/done) immediately "
            "before the final user-facing answer, only when the objective is fully complete. Call it with completed=false/status continue "
            "when more work remains.";
        driver.parameters_json = R"({
  "type": "object",
  "properties": {
    "status": {"type": "string", "enum": ["continue", "completed", "complete", "done", "blocked"], "description": "Completion status for the current objective. Use completed/done only when the final answer is ready."},
    "completed": {"type": "boolean", "description": "Required. True only when the current user objective is fully complete and the next assistant response should be the final answer; false when more work remains."},
    "final_summary": {"type": "string", "description": "Concise summary to use once complete."},
    "remaining_work": {"type": "string", "description": "What remains if the objective is not complete."},
    "next_action": {"type": "string", "description": "The next action the agent should take if continuing."}
  },
  "required": ["completed"]
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
    if (settings.built_in_sleep_enabled) {
        const int max_sleep_seconds = NormalizedSleepMaxSeconds(settings);
        ChatToolDefinition sleep;
        sleep.name = kSleepToolName;
        std::ostringstream desc;
        desc
            << "Pause the current agent/tool loop for a requested number of seconds, then continue. "
            << "Use this when background work has been started or requested and you need to wait before checking status, "
            << "polling output, reading a file, or taking the next step. This tool only waits; it does not start, monitor, "
            << "or verify external work. Prefer short, purposeful waits followed by a concrete status check. ";
        if (max_sleep_seconds > 0) {
            desc << "This project limits each sleep_seconds call to " << max_sleep_seconds
                 << " seconds; requests above that limit will fail.";
        } else {
            desc << "This project has no configured maximum sleep duration, so choose a reasonable duration for the task.";
        }
        sleep.description = desc.str();
        nlohmann::json schema = {
            {"type", "object"},
            {"properties", {
                {"seconds", {
                    {"type", "integer"},
                    {"description", max_sleep_seconds > 0
                        ? "Number of seconds to pause before continuing. Must be between 0 and the configured project maximum."
                        : "Number of seconds to pause before continuing. Use a reasonable duration for the task."},
                    {"minimum", 0}
                }}
            }},
            {"required", {"seconds"}}
        };
        if (max_sleep_seconds > 0) {
            schema["properties"]["seconds"]["maximum"] = max_sleep_seconds;
        }
        sleep.parameters_json = schema.dump(2);
        definitions.push_back(std::move(sleep));
    }
    if (settings.built_in_filesystem_enabled) {
        ChatToolDefinition fs;
        fs.name = kFilesystemToolName;
        fs.description =
            "Project Filesystem tool. Read, write, edit, list, and create files/directories under the configured working directory. "
            "All paths are relative to the working directory (default $ProjectFolder$) and templates are auto-expanded.\n\n"
            "Actions:\n"
            "- read - Return UTF-8 text file content, or a portion if start_line/end_line or start_offset/length are provided. Binary/non-UTF-8 files return a clear error instead of raw content.\n"
            "- write — Overwrite a file with new content. Set create_backup=true to snapshot the original into .agent/backups/<timestamp>.\n"
            "- edit — Apply diff edits using old_lines/new_lines replacements. Each entry may be a single line or multiline block; embedded newlines are normalized before matching.\n"
            "- list_directory — Return files and subdirectories for the given path.\n"
            "- create_directory — Create a new directory (including intermediate parents).\n\n"
            "Do not read .jar, .zip, image, executable, PDF, or generated build artifacts as text; list them or inspect source/config files instead.\n\n"
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
    "edits": {
      "type": "array",
      "description": "For edit action: list of {old_lines:[...], new_lines:[...]} objects. Each array entry may be a single line or multiline block. old_lines must match contiguous lines after newline normalization.",
      "items": {
        "type": "object",
        "properties": {
          "old_lines": {
            "type": "array",
            "description": "Contiguous lines to match and replace.",
            "items": {"type": "string"}
          },
          "new_lines": {
            "type": "array",
            "description": "Replacement lines. Use an empty array to delete the old_lines block.",
            "items": {"type": "string"}
          }
        },
        "required": ["old_lines", "new_lines"]
      }
    }
  },
  "required": ["action", "path"]
})";
        definitions.push_back(std::move(fs));
    }
    if (settings.built_in_browser_search_enabled) {
        ChatToolDefinition web;
        web.name = kBrowserSearchToolName;
        std::ostringstream desc;
        desc
            << "Built-in browser web search and retrieval tool using a real Chromium browser via Playwright. "
            << "Actions: search returns Google/Bing result titles, URLs, display URLs, snippets, and engines tried; "
            << "fetch reads a known URL; search_and_fetch searches and then fetches a selected result in one call. "
            << "Available engines in project priority order: " << BrowserSearchEngineList(settings) << ". "
            << "Use this for current information, research loops, JavaScript-rendered pages, visible-browser troubleshooting, "
            << "and saving a rendered website as PDF. "
            << (settings.browser_search_primary
                ? "This project marks browser_web_search as the primary web search path; prefer it over DuckDuckGo for broad search unless DuckDuckGo is specifically better for a known URL. "
                : "Use alongside DuckDuckGo; choose this when Google/Bing browser search, rendered content, or PDF output is useful. ")
            << "For content_type use text for readable extraction, html for raw rendered HTML, text_html for both, pdf for a saved PDF, or all for text, HTML, and PDF.";
        web.description = desc.str();
        web.parameters_json = R"({
  "type": "object",
  "properties": {
    "action": {"type": "string", "enum": ["search", "fetch", "search_and_fetch"], "description": "search returns result metadata only; fetch retrieves a known URL; search_and_fetch searches and fetches one result in the same browser-backed call."},
    "query": {"type": "string", "description": "Search query for search or search_and_fetch. Use normal search syntax such as site:example.com when appropriate."},
    "url": {"type": "string", "description": "URL to retrieve for fetch action."},
    "engine": {"type": "string", "enum": ["default", "auto", "google", "bing"], "description": "Optional search engine. default uses the configured default engine first; auto uses the configured priority order; google/bing request a specific engine if enabled."},
    "result_count": {"type": "integer", "description": "Number of search results to return, 1-20. Default 8.", "minimum": 1, "maximum": 20},
    "fetch_result_index": {"type": "integer", "description": "For search_and_fetch, 1-based result index to fetch. Default 1.", "minimum": 1},
    "content_type": {"type": "string", "enum": ["text", "html", "text_html", "pdf", "all"], "description": "For fetch/search_and_fetch. text returns extracted readable text; html returns rendered HTML; text_html returns both; pdf saves a rendered PDF and returns the path; all returns text, HTML, and a PDF path."},
    "open_visual_browser": {"type": "boolean", "description": "Override the project default and show or hide the browser window for this call."},
    "wait_until": {"type": "string", "enum": ["load", "domcontentloaded", "networkidle"], "description": "Browser load event to wait for. networkidle is thorough; domcontentloaded is faster for heavy pages."},
    "timeout_seconds": {"type": "integer", "description": "Optional per-call timeout from 1 to 600 seconds. Defaults to the project setting.", "minimum": 1, "maximum": 600},
    "output_path": {"type": "string", "description": "Optional path/template for saved PDF output. If omitted, PDF files are saved under $ProjectFolder$/.agent/browser_search."},
    "page_format": {"type": "string", "description": "PDF paper format such as A4, Letter, Legal, or A3. Default A4."},
    "print_background": {"type": "boolean", "description": "For PDF output, include background colors and images. Default true."},
    "max_content_chars": {"type": "integer", "description": "Maximum text or HTML characters returned for fetched content. Default 60000.", "minimum": 1000, "maximum": 200000}
  },
  "required": ["action"]
})";
        definitions.push_back(std::move(web));
    }
    if (settings.built_in_window_automation_enabled) {
        ChatToolDefinition win;
        win.name = kWindowAutomationToolName;
        win.description =
            "Native Windows UI Automation tool for desktop app testing and interaction. "
            "List open top-level windows, bring a target window to the foreground, inspect the UIA element tree, click/invoke controls, set text in editable controls, or type text into a focused element. "
            "For WebView2 panes, attach to a remote debugging port and inspect/click/fill Chromium DOM content through the webview2_* actions. "
            "Use inspect_window or webview2_inspect before manipulating unfamiliar windows. Prefer hwnd, automation_id, CSS selector, label, or exact name when available; element_index is also supported.";
        win.parameters_json = R"({
  "type": "object",
  "properties": {
    "action": {"type": "string", "enum": ["list_windows", "activate_window", "inspect_window", "click", "set_text", "type_text", "webview2_list_targets", "webview2_inspect", "webview2_click", "webview2_set_text", "webview2_type_text"], "description": "Window automation operation. list_windows discovers native windows; activate_window brings one forward; inspect_window reads UIA elements; click/set_text/type_text interact through UIA/Win32. webview2_* actions attach to a WebView2/Chromium remote debugging endpoint to inspect DOM text/elements and click/fill inside WebView2 content."},
    "hwnd": {"description": "Target top-level window handle as a hex string such as 0x00123456 or decimal string/integer. Prefer this after list_windows."},
    "window_index": {"type": "integer", "description": "0-based index from list_windows to target when hwnd is not supplied."},
    "title": {"type": "string", "description": "Exact top-level window title to target."},
    "title_contains": {"type": "string", "description": "Case-insensitive substring of the top-level window title."},
    "process_id": {"type": "integer", "description": "Target process id for the top-level window."},
    "process_name": {"type": "string", "description": "Case-insensitive process executable name or substring, such as notepad.exe or notepad."},
    "include_minimized": {"type": "boolean", "description": "For list_windows, include minimized windows. Default true."},
    "max_windows": {"type": "integer", "description": "For list_windows, maximum windows to return. Default 100.", "minimum": 1, "maximum": 500},
    "max_depth": {"type": "integer", "description": "For inspect_window and element targeting, max UIA tree depth below the root. Default 6.", "minimum": 0, "maximum": 20},
    "max_elements": {"type": "integer", "description": "For inspect_window and element targeting, max UIA elements to scan. Default 300.", "minimum": 1, "maximum": 2000},
    "include_offscreen": {"type": "boolean", "description": "Include offscreen UIA elements during inspection/targeting. Default false."},
    "element_index": {"type": "integer", "description": "0-based element index from inspect_window or webview2_inspect. Useful but less stable than automation_id/CSS selector."},
    "automation_id": {"type": "string", "description": "AutomationId of the target UIA element."},
    "name": {"type": "string", "description": "Exact accessible name of the target UIA element or WebView2 DOM element."},
    "name_contains": {"type": "string", "description": "Case-insensitive substring of the target UIA or WebView2 DOM element name."},
    "control_type": {"type": "string", "description": "Target UIA control type, e.g. Button, Edit, Text, Hyperlink, CheckBox, ComboBox, ListItem, MenuItem, TabItem."},
    "class_name": {"type": "string", "description": "Optional UIA class name filter for target element."},
    "debug_port": {"type": "integer", "description": "WebView2 remote debugging port, e.g. 9222. The target app must be launched with WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=PORT before WebView2 starts."},
    "debug_ports": {"type": "array", "items": {"type": "integer"}, "description": "Optional list of WebView2 remote debugging ports to probe."},
    "debug_url": {"type": "string", "description": "Optional full WebView2/Chromium CDP endpoint URL such as http://127.0.0.1:9222."},
    "debug_host": {"type": "string", "description": "Host for debug_port/debug_ports. Default 127.0.0.1."},
    "scan_ports": {"type": "boolean", "description": "For webview2_list_targets, scan a small port range when no explicit debug_port/debug_ports is supplied. Default false; otherwise common ports are probed."},
    "port_range_start": {"type": "integer", "description": "Start port for scan_ports. Default 9222."},
    "port_range_end": {"type": "integer", "description": "End port for scan_ports, capped to 50 ports after the start."},
    "target_index": {"type": "integer", "description": "0-based WebView2/Chromium target index from webview2_list_targets."},
    "target_id": {"type": "string", "description": "Specific WebView2/Chromium target id from webview2_list_targets."},
    "target_url_contains": {"type": "string", "description": "Case-insensitive substring filter for the WebView2 target URL."},
    "target_title_contains": {"type": "string", "description": "Case-insensitive substring filter for the WebView2 target title."},
    "selector": {"type": "string", "description": "CSS selector for a WebView2 DOM element to click/fill."},
    "label": {"type": "string", "description": "Associated label text for a WebView2 input/control."},
    "placeholder": {"type": "string", "description": "Placeholder text for a WebView2 input/control."},
    "text": {"type": "string", "description": "Exact visible text for a WebView2 DOM element."},
    "text_contains": {"type": "string", "description": "Case-insensitive visible text substring for a WebView2 DOM element."},
    "max_text_chars": {"type": "integer", "description": "For webview2_inspect, maximum page text characters to return. Default 60000.", "minimum": 1000, "maximum": 200000},
    "value": {"type": "string", "description": "Text to set or type for set_text/type_text."},
    "clear_existing": {"type": "boolean", "description": "For set_text/type_text keyboard fallback, select existing text first. Default true for set_text, false for type_text."},
    "press_enter": {"type": "boolean", "description": "After set_text/type_text, press Enter. Default false."},
    "prefer_mouse": {"type": "boolean", "description": "For click, skip UIA Invoke/Selection/Toggle and click with mouse coordinates. Default false."},
    "prefer_js_click": {"type": "boolean", "description": "For webview2_click, use DOM element.click() instead of a mouse click. Default false."},
    "activate_first": {"type": "boolean", "description": "Bring the target window to foreground before native click/set_text/type_text. Default true."},
    "timeout_seconds": {"type": "integer", "description": "Optional timeout for WebView2 CDP actions. Default 10 seconds.", "minimum": 1, "maximum": 120}
  },
  "required": ["action"]
})";
        definitions.push_back(std::move(win));
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

    try {
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
    } catch (const std::exception& ex) {
        return ErrorResult(std::string("PowerShell tool crashed: ") + ex.what());
    } catch (...) {
        return ErrorResult("PowerShell tool crashed with an unknown error.");
    }
}

struct ProcessRunResult {
    bool started = false;
    bool timed_out = false;
    bool truncated = false;
    DWORD exit_code = 1;
    std::string output;
    std::string error;
};

inline std::wstring QuoteCommandArgument(const std::wstring& value) {
    std::wstring out = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'\"') out += L'\\';
        out += ch;
    }
    out += L"\"";
    return out;
}

inline ProcessRunResult RunProcessCaptureOutput(
    const std::wstring& command_line,
    const std::filesystem::path& working_directory,
    int timeout_seconds,
    size_t max_output_bytes) {
    ProcessRunResult result;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
        result.error = "Failed to create process output pipe.";
        return result;
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
    std::wstring cwd_w = working_directory.empty() ? std::wstring() : working_directory.wstring();
    const wchar_t* cwd = cwd_w.empty() ? nullptr : cwd_w.c_str();

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
        result.error = "CreateProcess error: " + std::to_string(err);
        return result;
    }

    result.started = true;
    const DWORD timeout_ms = static_cast<DWORD>(std::max(1, timeout_seconds)) * 1000;
    const DWORD start = GetTickCount();
    for (;;) {
        result.output += ReadAvailablePipe(
            read_pipe,
            max_output_bytes - std::min(result.output.size(), max_output_bytes),
            &result.truncated);
        const DWORD wait = WaitForSingleObject(process.hProcess, 50);
        if (wait == WAIT_OBJECT_0) break;
        if (GetTickCount() - start >= timeout_ms) {
            result.timed_out = true;
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, 2000);
            break;
        }
    }
    result.output += ReadAvailablePipe(
        read_pipe,
        max_output_bytes - std::min(result.output.size(), max_output_bytes),
        &result.truncated);

    GetExitCodeProcess(process.hProcess, &result.exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(read_pipe);
    if (result.truncated) {
        result.output += "\n[output truncated]";
    }
    return result;
}

inline std::filesystem::path FindBrowserSearchRunnerScript() {
    std::vector<std::filesystem::path> candidates;
    std::error_code ec;
    candidates.push_back(std::filesystem::current_path(ec) / "scripts" / "built_in_browser_search_tool.py");

    wchar_t exe_path[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        const std::filesystem::path exe_dir = std::filesystem::path(exe_path).parent_path();
        candidates.push_back(exe_dir / "scripts" / "built_in_browser_search_tool.py");
        candidates.push_back(exe_dir.parent_path() / "scripts" / "built_in_browser_search_tool.py");
    }

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
    }
    return candidates.empty()
        ? std::filesystem::path("scripts") / "built_in_browser_search_tool.py"
        : candidates.front();
}

inline std::filesystem::path FindWebView2CdpRunnerScript() {
    std::vector<std::filesystem::path> candidates;
    std::error_code ec;
    candidates.push_back(std::filesystem::current_path(ec) / "scripts" / "built_in_webview2_cdp_tool.py");

    wchar_t exe_path[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        const std::filesystem::path exe_dir = std::filesystem::path(exe_path).parent_path();
        candidates.push_back(exe_dir / "scripts" / "built_in_webview2_cdp_tool.py");
        candidates.push_back(exe_dir.parent_path() / "scripts" / "built_in_webview2_cdp_tool.py");
    }

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
    }
    return candidates.empty()
        ? std::filesystem::path("scripts") / "built_in_webview2_cdp_tool.py"
        : candidates.front();
}

inline std::filesystem::path BrowserSearchProjectFolder(
    const std::vector<ProjectMcpVariableValue>& variables) {
    std::string project_folder = Trim(
        variable_resolver::ExpandTemplate("$ProjectFolder$", variables));
    if (project_folder.empty() || project_folder.find("$ProjectFolder$") != std::string::npos) {
        std::error_code ec;
        return std::filesystem::current_path(ec);
    }
    return std::filesystem::path(Utf8ToWide(project_folder));
}

inline std::string BrowserSearchNormalizeEngine(std::string engine) {
    engine = LowerAsciiCopy(Trim(engine));
    if (engine != "google" && engine != "bing" && engine != "auto" && engine != "default") {
        return "default";
    }
    return engine;
}

inline std::string BrowserSearchNormalizeContentMode(std::string mode) {
    mode = LowerAsciiCopy(Trim(mode));
    if (mode != "text" && mode != "html" && mode != "text_html" &&
        mode != "pdf" && mode != "all") {
        return "text";
    }
    return mode;
}

inline nlohmann::json ParseBrowserSearchJsonOutput(const std::string& output) {
    try {
        return nlohmann::json::parse(output);
    } catch (...) {
        const size_t first = output.find('{');
        const size_t last = output.rfind('}');
        if (first != std::string::npos && last != std::string::npos && last > first) {
            return nlohmann::json::parse(output.substr(first, last - first + 1));
        }
        throw;
    }
}

inline McpToolCallResult CallBrowserSearch(
    const std::string& arguments_json,
    const ProjectSettings& settings,
    const std::vector<ProjectMcpVariableValue>& variables) {
    nlohmann::json args;
    try {
        args = nlohmann::json::parse(arguments_json.empty() ? "{}" : arguments_json);
    } catch (const std::exception& ex) {
        return ErrorResult(std::string("Invalid browser web search arguments: ") + ex.what());
    }
    if (!args.is_object()) {
        return ErrorResult("Browser web search arguments must be a JSON object.");
    }

    const std::string action = LowerAsciiCopy(Trim(args.value("action", "search")));
    if (action != "search" && action != "fetch" && action != "search_and_fetch") {
        return ErrorResult("browser_web_search action must be search, fetch, or search_and_fetch.");
    }

    std::vector<std::string> allowed_engines;
    if (settings.browser_search_google_enabled) allowed_engines.push_back("google");
    if (settings.browser_search_bing_enabled) allowed_engines.push_back("bing");
    if (allowed_engines.empty()) allowed_engines.push_back("google");

    std::vector<std::string> engine_order;
    for (const auto& engine : settings.browser_search_engine_order) {
        const std::string normalized = LowerAsciiCopy(Trim(engine));
        if ((normalized == "google" || normalized == "bing") &&
            std::find(allowed_engines.begin(), allowed_engines.end(), normalized) != allowed_engines.end() &&
            std::find(engine_order.begin(), engine_order.end(), normalized) == engine_order.end()) {
            engine_order.push_back(normalized);
        }
    }
    for (const auto& engine : allowed_engines) {
        if (std::find(engine_order.begin(), engine_order.end(), engine) == engine_order.end()) {
            engine_order.push_back(engine);
        }
    }

    const std::filesystem::path project_folder = BrowserSearchProjectFolder(variables);
    const std::filesystem::path output_dir = project_folder / ".agent" / "browser_search";
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);

    std::string output_path = Trim(args.value("output_path", ""));
    if (!output_path.empty()) {
        output_path = variable_resolver::ExpandTemplate(output_path, variables);
        std::filesystem::path out_path(Utf8ToWide(output_path));
        if (out_path.is_relative()) {
            out_path = output_dir / out_path;
        }
        output_path = WideToUtf8(out_path.wstring());
    }

    const int timeout_seconds = std::clamp(
        args.value("timeout_seconds", settings.browser_search_timeout_seconds),
        1,
        600);

    nlohmann::json payload = {
        {"action", action},
        {"engine", BrowserSearchNormalizeEngine(args.value("engine", "default"))},
        {"default_engine", BrowserSearchNormalizeEngine(settings.browser_search_default_engine)},
        {"allowed_engines", allowed_engines},
        {"engine_order", engine_order},
        {"result_count", std::clamp(args.value("result_count", 8), 1, 20)},
        {"fetch_result_index", std::max(1, args.value("fetch_result_index", 1))},
        {"content_type", BrowserSearchNormalizeContentMode(
            args.value("content_type", settings.browser_search_default_content_mode))},
        {"open_visual_browser", args.contains("open_visual_browser") && args["open_visual_browser"].is_boolean()
            ? args["open_visual_browser"].get<bool>()
            : settings.browser_search_open_visual_browser},
        {"wait_until", args.value("wait_until", "networkidle")},
        {"timeout_seconds", timeout_seconds},
        {"output_dir", WideToUtf8(output_dir.wstring())},
        {"cookie_file", WideToUtf8((output_dir / "cookies.json").wstring())},
        {"page_format", args.value("page_format", "A4")},
        {"print_background", args.value("print_background", true)},
        {"max_content_chars", std::clamp(args.value("max_content_chars", 60000), 1000, 200000)},
        {"delays", {
            {"page_load", {settings.browser_search_page_load_delay_min_ms, settings.browser_search_page_load_delay_max_ms}},
            {"keystroke", {settings.browser_search_keystroke_delay_min_ms, settings.browser_search_keystroke_delay_max_ms}},
            {"click", {settings.browser_search_click_delay_min_ms, settings.browser_search_click_delay_max_ms}},
            {"pre_submit", {settings.browser_search_pre_submit_delay_min_ms, settings.browser_search_pre_submit_delay_max_ms}},
            {"post_results", {settings.browser_search_post_results_delay_min_ms, settings.browser_search_post_results_delay_max_ms}},
        }},
    };
    if (args.contains("query") && args["query"].is_string()) {
        payload["query"] = args["query"].get<std::string>();
    }
    if (args.contains("url") && args["url"].is_string()) {
        payload["url"] = args["url"].get<std::string>();
    }
    if (!output_path.empty()) {
        payload["output_path"] = output_path;
    }

    const std::filesystem::path script = FindBrowserSearchRunnerScript();
    if (!std::filesystem::exists(script, ec)) {
        return ErrorResult("Browser web search runner script was not found: " + WideToUtf8(script.wstring()));
    }

    const std::string payload_text = payload.dump();
    const std::string encoded = Base64Encode(
        reinterpret_cast<const unsigned char*>(payload_text.data()),
        payload_text.size());

    const std::wstring quoted_script = QuoteCommandArgument(script.wstring());
    const std::wstring quoted_payload = QuoteCommandArgument(Utf8ToWide(encoded));
    const std::vector<std::wstring> commands = {
        L"python.exe -u " + quoted_script + L" --payload-base64 " + quoted_payload,
        L"py.exe -3 -u " + quoted_script + L" --payload-base64 " + quoted_payload,
    };

    ProcessRunResult run;
    for (size_t i = 0; i < commands.size(); ++i) {
        run = RunProcessCaptureOutput(
            commands[i],
            script.parent_path(),
            timeout_seconds + 15,
            768 * 1024);
        if (run.started || i + 1 == commands.size()) break;
    }

    if (!run.started) {
        return ErrorResult(
            "Failed to start the browser web search runner. Install Python, Playwright, undetected-playwright, and BeautifulSoup. Last error: " +
            run.error);
    }

    nlohmann::json parsed;
    bool parsed_json = false;
    try {
        parsed = ParseBrowserSearchJsonOutput(run.output);
        parsed_json = parsed.is_object();
    } catch (const std::exception& ex) {
        return ErrorResult(
            std::string("Browser web search runner did not return valid JSON: ") + ex.what() +
            "\nOutput:\n" + run.output);
    }

    if (parsed_json) {
        parsed["tool"] = kBrowserSearchToolName;
        parsed["exit_code"] = static_cast<int>(run.exit_code);
        parsed["timed_out"] = run.timed_out;
        parsed["output_truncated"] = run.truncated;
    }

    McpToolCallResult result;
    result.success = !run.timed_out && parsed.value("success", false);
    result.is_tool_error = !result.success;
    result.raw_result_json = parsed.dump(2);
    std::ostringstream content;
    content << "Browser web search " << (result.success ? "completed" : "failed")
            << " (exit code " << run.exit_code;
    if (run.timed_out) content << ", timed out";
    content << ").\n";
    if (parsed.contains("error") && parsed["error"].is_string()) {
        content << "Error: " << parsed["error"].get<std::string>() << "\n";
    }
    content << parsed.dump(2);
    result.content_text = content.str();
    return result;
}

inline bool WindowAutomationIsWebView2Action(const std::string& action) {
    return action == "webview2listtargets" ||
           action == "webview2inspect" ||
           action == "webview2click" ||
           action == "webview2settext" ||
           action == "webview2typetext";
}

inline std::string WindowAutomationCanonicalWebView2Action(const std::string& action) {
    if (action == "webview2listtargets") return "webview2_list_targets";
    if (action == "webview2inspect") return "webview2_inspect";
    if (action == "webview2click") return "webview2_click";
    if (action == "webview2settext") return "webview2_set_text";
    if (action == "webview2typetext") return "webview2_type_text";
    return action;
}

inline McpToolCallResult CallWindowAutomationWebView2(
    const std::string& normalized_action,
    const nlohmann::json& args) {
    nlohmann::json payload = args;
    payload["action"] = WindowAutomationCanonicalWebView2Action(normalized_action);

    const int timeout_seconds = std::clamp(
        payload.value("timeout_seconds", 10),
        1,
        120);
    payload["timeout_seconds"] = timeout_seconds;

    const std::filesystem::path script = FindWebView2CdpRunnerScript();
    std::error_code ec;
    if (!std::filesystem::exists(script, ec)) {
        return ErrorResult("WebView2 CDP runner script was not found: " + WideToUtf8(script.wstring()));
    }

    const std::string payload_text = payload.dump();
    const std::string encoded = Base64Encode(
        reinterpret_cast<const unsigned char*>(payload_text.data()),
        payload_text.size());

    const std::wstring quoted_script = QuoteCommandArgument(script.wstring());
    const std::wstring quoted_payload = QuoteCommandArgument(Utf8ToWide(encoded));
    const std::vector<std::wstring> commands = {
        L"python.exe -u " + quoted_script + L" --payload-base64 " + quoted_payload,
        L"py.exe -3 -u " + quoted_script + L" --payload-base64 " + quoted_payload,
    };

    ProcessRunResult run;
    for (size_t i = 0; i < commands.size(); ++i) {
        run = RunProcessCaptureOutput(
            commands[i],
            script.parent_path(),
            timeout_seconds + 10,
            1024 * 1024);
        if (run.started || i + 1 == commands.size()) break;
    }

    if (!run.started) {
        return ErrorResult(
            "Failed to start the WebView2 CDP runner. Install Python and Playwright via Setup System. Last error: " +
            run.error);
    }

    nlohmann::json parsed;
    try {
        parsed = ParseBrowserSearchJsonOutput(run.output);
    } catch (const std::exception& ex) {
        return ErrorResult(
            std::string("WebView2 CDP runner did not return valid JSON: ") + ex.what() +
            "\nOutput:\n" + run.output);
    }

    parsed["tool"] = kWindowAutomationToolName;
    parsed["cdp_mode"] = "webview2";
    parsed["exit_code"] = static_cast<int>(run.exit_code);
    parsed["timed_out"] = run.timed_out;
    parsed["output_truncated"] = run.truncated;

    McpToolCallResult result;
    result.success = !run.timed_out && parsed.value("success", false);
    result.is_tool_error = !result.success;
    result.raw_result_json = parsed.dump(2);

    std::ostringstream content;
    content << "Window Automation WebView2 CDP action "
            << payload.value("action", std::string("webview2"))
            << (result.success ? " completed" : " failed")
            << " (exit code " << run.exit_code;
    if (run.timed_out) content << ", timed out";
    content << ").\n";
    if (parsed.contains("error") && parsed["error"].is_string()) {
        content << "Error: " << parsed["error"].get<std::string>() << "\n";
    }
    if (parsed.contains("hint") && parsed["hint"].is_string() &&
        !parsed["hint"].get<std::string>().empty()) {
        content << "Hint: " << parsed["hint"].get<std::string>() << "\n";
    }
    content << parsed.dump(2);
    result.content_text = content.str();
    return result;
}

struct WindowAutomationComApartment {
    HRESULT hr = E_FAIL;
    bool initialized = false;

    WindowAutomationComApartment() {
        hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        initialized = SUCCEEDED(hr);
        if (hr == RPC_E_CHANGED_MODE) {
            initialized = false;
            hr = S_OK;
        }
    }

    ~WindowAutomationComApartment() {
        if (initialized) {
            CoUninitialize();
        }
    }
};

struct WindowAutomationWindowInfo {
    HWND hwnd = nullptr;
    std::wstring title;
    std::wstring class_name;
    DWORD process_id = 0;
    std::wstring process_path;
    bool visible = false;
    bool minimized = false;
    bool foreground = false;
};

inline std::string WindowAutomationHresult(HRESULT hr) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << static_cast<unsigned long>(hr);
    return out.str();
}

inline std::string WindowAutomationHwndString(HWND hwnd) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << reinterpret_cast<std::uintptr_t>(hwnd);
    return out.str();
}

inline std::string WindowAutomationNormalize(std::string value) {
    value = LowerAsciiCopy(Trim(value));
    value.erase(std::remove_if(value.begin(), value.end(),
        [](unsigned char ch) { return ch == ' ' || ch == '_' || ch == '-'; }), value.end());
    return value;
}

inline std::optional<HWND> WindowAutomationParseHwnd(const nlohmann::json& value) {
    try {
        if (value.is_number_unsigned()) {
            return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(value.get<std::uint64_t>()));
        }
        if (value.is_number_integer()) {
            const auto parsed = value.get<std::int64_t>();
            if (parsed > 0) {
                return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(parsed));
            }
        }
        if (value.is_string()) {
            std::string text = Trim(value.get<std::string>());
            if (text.empty()) return std::nullopt;
            int base = 10;
            if (text.rfind("0x", 0) == 0 || text.rfind("0X", 0) == 0) {
                base = 16;
                text = text.substr(2);
            }
            const auto parsed = std::stoull(text, nullptr, base);
            if (parsed > 0) {
                return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(parsed));
            }
        }
    } catch (...) {
    }
    return std::nullopt;
}

inline std::wstring WindowAutomationWindowText(HWND hwnd) {
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return {};
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    const int copied = GetWindowTextW(hwnd, text.data(), static_cast<int>(text.size()));
    text.resize(std::max(0, copied));
    return text;
}

inline std::wstring WindowAutomationClassName(HWND hwnd) {
    wchar_t buffer[256] = {};
    const int len = GetClassNameW(hwnd, buffer, static_cast<int>(std::size(buffer)));
    return len > 0 ? std::wstring(buffer, static_cast<size_t>(len)) : std::wstring();
}

inline std::wstring WindowAutomationProcessPath(DWORD process_id) {
    std::wstring result;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (!process) return result;
    wchar_t buffer[32768] = {};
    DWORD size = static_cast<DWORD>(std::size(buffer));
    if (QueryFullProcessImageNameW(process, 0, buffer, &size) && size > 0) {
        result.assign(buffer, buffer + size);
    }
    CloseHandle(process);
    return result;
}

inline std::string WindowAutomationProcessName(const std::wstring& path) {
    if (path.empty()) return {};
    return WideToUtf8(std::filesystem::path(path).filename().wstring());
}

inline nlohmann::json WindowAutomationWindowJson(const WindowAutomationWindowInfo& info, int index) {
    RECT rect{};
    GetWindowRect(info.hwnd, &rect);
    return {
        {"index", index},
        {"hwnd", WindowAutomationHwndString(info.hwnd)},
        {"title", WideToUtf8(info.title)},
        {"class_name", WideToUtf8(info.class_name)},
        {"process_id", static_cast<int>(info.process_id)},
        {"process_name", WindowAutomationProcessName(info.process_path)},
        {"process_path", WideToUtf8(info.process_path)},
        {"visible", info.visible},
        {"minimized", info.minimized},
        {"foreground", info.foreground},
        {"bounds", {
            {"left", rect.left},
            {"top", rect.top},
            {"right", rect.right},
            {"bottom", rect.bottom},
            {"width", rect.right - rect.left},
            {"height", rect.bottom - rect.top}
        }}
    };
}

inline std::vector<WindowAutomationWindowInfo> WindowAutomationListWindows(const nlohmann::json& args) {
    struct EnumState {
        std::vector<WindowAutomationWindowInfo> windows;
    } state;

    EnumWindows([](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* state = reinterpret_cast<EnumState*>(lparam);
        if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) {
            return TRUE;
        }
        WindowAutomationWindowInfo info;
        info.hwnd = hwnd;
        info.title = WindowAutomationWindowText(hwnd);
        info.class_name = WindowAutomationClassName(hwnd);
        info.visible = IsWindowVisible(hwnd) != FALSE;
        info.minimized = IsIconic(hwnd) != FALSE;
        info.foreground = GetForegroundWindow() == hwnd;
        GetWindowThreadProcessId(hwnd, &info.process_id);
        info.process_path = WindowAutomationProcessPath(info.process_id);
        if (!info.title.empty() || !info.class_name.empty()) {
            state->windows.push_back(std::move(info));
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&state));

    const std::string title = LowerAsciiCopy(Trim(args.value("title", "")));
    const std::string title_contains = LowerAsciiCopy(Trim(args.value("title_contains", "")));
    const std::string process_name = LowerAsciiCopy(Trim(args.value("process_name", "")));
    const int process_id = args.value("process_id", 0);
    const bool include_minimized = args.value("include_minimized", true);

    std::vector<WindowAutomationWindowInfo> filtered;
    for (auto& info : state.windows) {
        if (!include_minimized && info.minimized) continue;
        const std::string win_title = LowerAsciiCopy(WideToUtf8(info.title));
        if (!title.empty() && win_title != title) continue;
        if (!title_contains.empty() && win_title.find(title_contains) == std::string::npos) continue;
        if (process_id > 0 && static_cast<int>(info.process_id) != process_id) continue;
        if (!process_name.empty()) {
            const std::string name = LowerAsciiCopy(WindowAutomationProcessName(info.process_path));
            const std::string path = LowerAsciiCopy(WideToUtf8(info.process_path));
            if (name.find(process_name) == std::string::npos &&
                path.find(process_name) == std::string::npos) {
                continue;
            }
        }
        filtered.push_back(std::move(info));
    }
    return filtered;
}

inline std::optional<WindowAutomationWindowInfo> WindowAutomationResolveWindow(
    const nlohmann::json& args,
    std::string* error) {
    if (args.contains("hwnd")) {
        const auto parsed = WindowAutomationParseHwnd(args["hwnd"]);
        if (!parsed || !IsWindow(*parsed)) {
            if (error) *error = "The supplied hwnd is not a valid window handle.";
            return std::nullopt;
        }
        WindowAutomationWindowInfo info;
        info.hwnd = *parsed;
        info.title = WindowAutomationWindowText(*parsed);
        info.class_name = WindowAutomationClassName(*parsed);
        info.visible = IsWindowVisible(*parsed) != FALSE;
        info.minimized = IsIconic(*parsed) != FALSE;
        info.foreground = GetForegroundWindow() == *parsed;
        GetWindowThreadProcessId(*parsed, &info.process_id);
        info.process_path = WindowAutomationProcessPath(info.process_id);
        return info;
    }

    auto windows = WindowAutomationListWindows(args);
    if (windows.empty()) {
        if (error) *error = "No matching top-level window was found. Call list_windows first or provide hwnd/title/title_contains/process_id.";
        return std::nullopt;
    }
    int index = args.value("window_index", 0);
    if (index < 0 || index >= static_cast<int>(windows.size())) {
        if (error) {
            *error = "window_index is out of range for the filtered window list.";
        }
        return std::nullopt;
    }
    return windows[static_cast<size_t>(index)];
}

inline bool WindowAutomationActivateWindow(HWND hwnd) {
    if (!IsWindow(hwnd)) return false;
    const DWORD current_thread_id = GetCurrentThreadId();
    const DWORD target_thread_id = GetWindowThreadProcessId(hwnd, nullptr);
    DWORD foreground_thread_id = 0;
    if (HWND foreground = GetForegroundWindow()) {
        foreground_thread_id = GetWindowThreadProcessId(foreground, nullptr);
    }
    const bool attached_target =
        target_thread_id != 0 && target_thread_id != current_thread_id &&
        AttachThreadInput(current_thread_id, target_thread_id, TRUE) != FALSE;
    const bool attached_foreground =
        foreground_thread_id != 0 && foreground_thread_id != current_thread_id &&
        foreground_thread_id != target_thread_id &&
        AttachThreadInput(current_thread_id, foreground_thread_id, TRUE) != FALSE;

    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    } else {
        ShowWindow(hwnd, SW_SHOW);
    }
    BringWindowToTop(hwnd);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    const bool foreground_set = SetForegroundWindow(hwnd) != FALSE;
    SetFocus(hwnd);

    if (attached_foreground) AttachThreadInput(current_thread_id, foreground_thread_id, FALSE);
    if (attached_target) AttachThreadInput(current_thread_id, target_thread_id, FALSE);
    return foreground_set || GetForegroundWindow() == hwnd;
}

inline std::string WindowAutomationControlTypeName(CONTROLTYPEID id) {
    switch (id) {
    case UIA_ButtonControlTypeId: return "Button";
    case UIA_CalendarControlTypeId: return "Calendar";
    case UIA_CheckBoxControlTypeId: return "CheckBox";
    case UIA_ComboBoxControlTypeId: return "ComboBox";
    case UIA_CustomControlTypeId: return "Custom";
    case UIA_DataGridControlTypeId: return "DataGrid";
    case UIA_DataItemControlTypeId: return "DataItem";
    case UIA_DocumentControlTypeId: return "Document";
    case UIA_EditControlTypeId: return "Edit";
    case UIA_GroupControlTypeId: return "Group";
    case UIA_HeaderControlTypeId: return "Header";
    case UIA_HeaderItemControlTypeId: return "HeaderItem";
    case UIA_HyperlinkControlTypeId: return "Hyperlink";
    case UIA_ImageControlTypeId: return "Image";
    case UIA_ListControlTypeId: return "List";
    case UIA_ListItemControlTypeId: return "ListItem";
    case UIA_MenuControlTypeId: return "Menu";
    case UIA_MenuBarControlTypeId: return "MenuBar";
    case UIA_MenuItemControlTypeId: return "MenuItem";
    case UIA_PaneControlTypeId: return "Pane";
    case UIA_ProgressBarControlTypeId: return "ProgressBar";
    case UIA_RadioButtonControlTypeId: return "RadioButton";
    case UIA_ScrollBarControlTypeId: return "ScrollBar";
    case UIA_SemanticZoomControlTypeId: return "SemanticZoom";
    case UIA_SeparatorControlTypeId: return "Separator";
    case UIA_SliderControlTypeId: return "Slider";
    case UIA_SpinnerControlTypeId: return "Spinner";
    case UIA_SplitButtonControlTypeId: return "SplitButton";
    case UIA_StatusBarControlTypeId: return "StatusBar";
    case UIA_TabControlTypeId: return "Tab";
    case UIA_TabItemControlTypeId: return "TabItem";
    case UIA_TableControlTypeId: return "Table";
    case UIA_TextControlTypeId: return "Text";
    case UIA_ThumbControlTypeId: return "Thumb";
    case UIA_TitleBarControlTypeId: return "TitleBar";
    case UIA_ToolBarControlTypeId: return "ToolBar";
    case UIA_ToolTipControlTypeId: return "ToolTip";
    case UIA_TreeControlTypeId: return "Tree";
    case UIA_TreeItemControlTypeId: return "TreeItem";
    case UIA_WindowControlTypeId: return "Window";
    default: return std::to_string(id);
    }
}

inline std::optional<CONTROLTYPEID> WindowAutomationControlTypeId(std::string value) {
    value = WindowAutomationNormalize(value);
    if (value == "button") return UIA_ButtonControlTypeId;
    if (value == "calendar") return UIA_CalendarControlTypeId;
    if (value == "checkbox" || value == "check") return UIA_CheckBoxControlTypeId;
    if (value == "combobox" || value == "combo") return UIA_ComboBoxControlTypeId;
    if (value == "custom") return UIA_CustomControlTypeId;
    if (value == "datagrid") return UIA_DataGridControlTypeId;
    if (value == "dataitem") return UIA_DataItemControlTypeId;
    if (value == "document") return UIA_DocumentControlTypeId;
    if (value == "edit" || value == "textbox" || value == "textinput") return UIA_EditControlTypeId;
    if (value == "group") return UIA_GroupControlTypeId;
    if (value == "hyperlink" || value == "link") return UIA_HyperlinkControlTypeId;
    if (value == "image") return UIA_ImageControlTypeId;
    if (value == "list") return UIA_ListControlTypeId;
    if (value == "listitem") return UIA_ListItemControlTypeId;
    if (value == "menu") return UIA_MenuControlTypeId;
    if (value == "menubar") return UIA_MenuBarControlTypeId;
    if (value == "menuitem") return UIA_MenuItemControlTypeId;
    if (value == "pane") return UIA_PaneControlTypeId;
    if (value == "radiobutton" || value == "radio") return UIA_RadioButtonControlTypeId;
    if (value == "tab") return UIA_TabControlTypeId;
    if (value == "tabitem") return UIA_TabItemControlTypeId;
    if (value == "table") return UIA_TableControlTypeId;
    if (value == "text" || value == "label") return UIA_TextControlTypeId;
    if (value == "tree") return UIA_TreeControlTypeId;
    if (value == "treeitem") return UIA_TreeItemControlTypeId;
    if (value == "window") return UIA_WindowControlTypeId;
    return std::nullopt;
}

inline std::string WindowAutomationTakeBstr(BSTR value) {
    if (!value) return {};
    std::wstring wide(value, SysStringLen(value));
    SysFreeString(value);
    return WideToUtf8(wide);
}

inline std::string WindowAutomationElementName(IUIAutomationElement* element) {
    BSTR value = nullptr;
    return SUCCEEDED(element->get_CurrentName(&value)) ? WindowAutomationTakeBstr(value) : std::string();
}

inline std::string WindowAutomationElementAutomationId(IUIAutomationElement* element) {
    BSTR value = nullptr;
    return SUCCEEDED(element->get_CurrentAutomationId(&value)) ? WindowAutomationTakeBstr(value) : std::string();
}

inline std::string WindowAutomationElementClassName(IUIAutomationElement* element) {
    BSTR value = nullptr;
    return SUCCEEDED(element->get_CurrentClassName(&value)) ? WindowAutomationTakeBstr(value) : std::string();
}

inline std::string WindowAutomationElementLocalizedType(IUIAutomationElement* element) {
    BSTR value = nullptr;
    return SUCCEEDED(element->get_CurrentLocalizedControlType(&value)) ? WindowAutomationTakeBstr(value) : std::string();
}

inline std::string WindowAutomationElementFrameworkId(IUIAutomationElement* element) {
    BSTR value = nullptr;
    return SUCCEEDED(element->get_CurrentFrameworkId(&value)) ? WindowAutomationTakeBstr(value) : std::string();
}

inline bool WindowAutomationBoolProperty(IUIAutomationElement* element, PROPERTYID id) {
    VARIANT value;
    VariantInit(&value);
    bool result = false;
    if (SUCCEEDED(element->GetCurrentPropertyValue(id, &value)) && value.vt == VT_BOOL) {
        result = value.boolVal == VARIANT_TRUE;
    }
    VariantClear(&value);
    return result;
}

inline bool WindowAutomationPatternAvailable(IUIAutomationElement* element, PROPERTYID id) {
    return WindowAutomationBoolProperty(element, id);
}

inline std::string WindowAutomationValue(IUIAutomationElement* element) {
    Microsoft::WRL::ComPtr<IUIAutomationValuePattern> value_pattern;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value_pattern))) && value_pattern) {
        BSTR value = nullptr;
        if (SUCCEEDED(value_pattern->get_CurrentValue(&value))) {
            return WindowAutomationTakeBstr(value);
        }
    }
    return {};
}

inline void WindowAutomationCollectElementsRecursive(
    IUIAutomationTreeWalker* walker,
    IUIAutomationElement* element,
    int depth,
    int max_depth,
    int max_elements,
    bool include_offscreen,
    std::vector<Microsoft::WRL::ComPtr<IUIAutomationElement>>& elements) {
    if (!element || static_cast<int>(elements.size()) >= max_elements) return;

    BOOL offscreen = FALSE;
    element->get_CurrentIsOffscreen(&offscreen);
    if (include_offscreen || !offscreen || depth == 0) {
        Microsoft::WRL::ComPtr<IUIAutomationElement> retained;
        element->AddRef();
        retained.Attach(element);
        elements.push_back(retained);
    }

    if (depth >= max_depth || static_cast<int>(elements.size()) >= max_elements) return;
    Microsoft::WRL::ComPtr<IUIAutomationElement> child;
    if (FAILED(walker->GetFirstChildElement(element, &child)) || !child) return;
    while (child && static_cast<int>(elements.size()) < max_elements) {
        WindowAutomationCollectElementsRecursive(
            walker, child.Get(), depth + 1, max_depth, max_elements, include_offscreen, elements);
        Microsoft::WRL::ComPtr<IUIAutomationElement> next;
        if (FAILED(walker->GetNextSiblingElement(child.Get(), &next))) break;
        child = next;
    }
}

inline std::vector<Microsoft::WRL::ComPtr<IUIAutomationElement>> WindowAutomationCollectElements(
    IUIAutomation* automation,
    IUIAutomationElement* root,
    int max_depth,
    int max_elements,
    bool include_offscreen) {
    std::vector<Microsoft::WRL::ComPtr<IUIAutomationElement>> elements;
    Microsoft::WRL::ComPtr<IUIAutomationTreeWalker> walker;
    if (FAILED(automation->get_ControlViewWalker(&walker)) || !walker) {
        return elements;
    }
    WindowAutomationCollectElementsRecursive(
        walker.Get(), root, 0, max_depth, max_elements, include_offscreen, elements);
    return elements;
}

inline nlohmann::json WindowAutomationElementJson(IUIAutomationElement* element, int index) {
    CONTROLTYPEID control_type = 0;
    element->get_CurrentControlType(&control_type);
    BOOL enabled = FALSE;
    BOOL offscreen = FALSE;
    element->get_CurrentIsEnabled(&enabled);
    element->get_CurrentIsOffscreen(&offscreen);
    int process_id = 0;
    element->get_CurrentProcessId(&process_id);
    RECT rect{};
    element->get_CurrentBoundingRectangle(&rect);
    POINT point{};
    BOOL has_clickable_point = FALSE;
    element->GetClickablePoint(&point, &has_clickable_point);

    std::vector<std::string> patterns;
    const auto add_pattern = [&](PROPERTYID id, const char* name) {
        if (WindowAutomationPatternAvailable(element, id)) patterns.push_back(name);
    };
    add_pattern(UIA_IsInvokePatternAvailablePropertyId, "Invoke");
    add_pattern(UIA_IsValuePatternAvailablePropertyId, "Value");
    add_pattern(UIA_IsTextPatternAvailablePropertyId, "Text");
    add_pattern(UIA_IsSelectionItemPatternAvailablePropertyId, "SelectionItem");
    add_pattern(UIA_IsTogglePatternAvailablePropertyId, "Toggle");
    add_pattern(UIA_IsScrollItemPatternAvailablePropertyId, "ScrollItem");

    nlohmann::json item = {
        {"index", index},
        {"name", WindowAutomationElementName(element)},
        {"automation_id", WindowAutomationElementAutomationId(element)},
        {"control_type", WindowAutomationControlTypeName(control_type)},
        {"control_type_id", control_type},
        {"localized_control_type", WindowAutomationElementLocalizedType(element)},
        {"class_name", WindowAutomationElementClassName(element)},
        {"framework_id", WindowAutomationElementFrameworkId(element)},
        {"process_id", process_id},
        {"enabled", enabled == TRUE},
        {"offscreen", offscreen == TRUE},
        {"patterns", patterns},
        {"bounds", {
            {"left", rect.left},
            {"top", rect.top},
            {"right", rect.right},
            {"bottom", rect.bottom},
            {"width", rect.right - rect.left},
            {"height", rect.bottom - rect.top}
        }}
    };
    const std::string value = WindowAutomationValue(element);
    if (!value.empty()) item["value"] = value;
    if (has_clickable_point) {
        item["clickable_point"] = {{"x", point.x}, {"y", point.y}};
    }
    return item;
}

inline bool WindowAutomationHasElementSelector(const nlohmann::json& args) {
    return args.contains("element_index") || args.contains("automation_id") ||
        args.contains("name") || args.contains("name_contains") ||
        args.contains("control_type") || args.contains("class_name");
}

inline bool WindowAutomationElementMatches(IUIAutomationElement* element, const nlohmann::json& args) {
    if (!WindowAutomationHasElementSelector(args)) return false;
    if (args.contains("automation_id") && args["automation_id"].is_string()) {
        if (LowerAsciiCopy(WindowAutomationElementAutomationId(element)) !=
            LowerAsciiCopy(Trim(args["automation_id"].get<std::string>()))) {
            return false;
        }
    }
    if (args.contains("name") && args["name"].is_string()) {
        if (LowerAsciiCopy(WindowAutomationElementName(element)) !=
            LowerAsciiCopy(Trim(args["name"].get<std::string>()))) {
            return false;
        }
    }
    if (args.contains("name_contains") && args["name_contains"].is_string()) {
        const std::string needle = LowerAsciiCopy(Trim(args["name_contains"].get<std::string>()));
        if (LowerAsciiCopy(WindowAutomationElementName(element)).find(needle) == std::string::npos) {
            return false;
        }
    }
    if (args.contains("class_name") && args["class_name"].is_string()) {
        if (LowerAsciiCopy(WindowAutomationElementClassName(element)) !=
            LowerAsciiCopy(Trim(args["class_name"].get<std::string>()))) {
            return false;
        }
    }
    if (args.contains("control_type") && args["control_type"].is_string()) {
        const auto wanted = WindowAutomationControlTypeId(args["control_type"].get<std::string>());
        if (!wanted) return false;
        CONTROLTYPEID actual = 0;
        element->get_CurrentControlType(&actual);
        if (actual != *wanted) return false;
    }
    return true;
}

inline Microsoft::WRL::ComPtr<IUIAutomationElement> WindowAutomationFindElement(
    const std::vector<Microsoft::WRL::ComPtr<IUIAutomationElement>>& elements,
    const nlohmann::json& args,
    std::string* error) {
    if (args.contains("element_index")) {
        const int index = args.value("element_index", -1);
        if (index >= 0 && index < static_cast<int>(elements.size())) {
            return elements[static_cast<size_t>(index)];
        }
        if (error) *error = "element_index is out of range for the inspected UIA tree.";
        return {};
    }
    if (!WindowAutomationHasElementSelector(args)) {
        if (error) *error = "No element selector was supplied. Use element_index, automation_id, name/name_contains, or control_type.";
        return {};
    }
    for (const auto& element : elements) {
        if (WindowAutomationElementMatches(element.Get(), args)) {
            return element;
        }
    }
    if (error) *error = "No matching UIA element was found in the target window.";
    return {};
}

inline void WindowAutomationSendKey(WORD vk, bool key_down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    if (!key_down) input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

inline void WindowAutomationSendCtrlA() {
    WindowAutomationSendKey(VK_CONTROL, true);
    WindowAutomationSendKey('A', true);
    WindowAutomationSendKey('A', false);
    WindowAutomationSendKey(VK_CONTROL, false);
}

inline void WindowAutomationSendEnter() {
    WindowAutomationSendKey(VK_RETURN, true);
    WindowAutomationSendKey(VK_RETURN, false);
}

inline void WindowAutomationSendUnicodeText(const std::string& value) {
    const std::wstring text = Utf8ToWide(value);
    for (wchar_t ch : text) {
        INPUT inputs[2]{};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wScan = ch;
        inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wScan = ch;
        inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(2, inputs, sizeof(INPUT));
    }
}

inline bool WindowAutomationMouseClickPoint(POINT point) {
    if (!SetCursorPos(point.x, point.y)) return false;
    INPUT inputs[2]{};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    return SendInput(2, inputs, sizeof(INPUT)) == 2;
}

inline std::optional<POINT> WindowAutomationElementClickPoint(IUIAutomationElement* element) {
    POINT point{};
    BOOL has_clickable_point = FALSE;
    if (SUCCEEDED(element->GetClickablePoint(&point, &has_clickable_point)) && has_clickable_point) {
        return point;
    }
    RECT rect{};
    if (SUCCEEDED(element->get_CurrentBoundingRectangle(&rect)) &&
        rect.right > rect.left && rect.bottom > rect.top) {
        return POINT{rect.left + (rect.right - rect.left) / 2,
                     rect.top + (rect.bottom - rect.top) / 2};
    }
    return std::nullopt;
}

inline void WindowAutomationScrollIntoView(IUIAutomationElement* element) {
    Microsoft::WRL::ComPtr<IUIAutomationScrollItemPattern> scroll_item;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ScrollItemPatternId, IID_PPV_ARGS(&scroll_item))) && scroll_item) {
        scroll_item->ScrollIntoView();
    }
}

inline McpToolCallResult WindowAutomationJsonResult(const nlohmann::json& payload, bool success = true) {
    McpToolCallResult result;
    result.success = success;
    result.is_tool_error = !success;
    result.raw_result_json = payload.dump(2);
    result.content_text = payload.dump(2);
    return result;
}

inline McpToolCallResult CallWindowAutomation(const std::string& arguments_json) {
    nlohmann::json args;
    try {
        args = nlohmann::json::parse(arguments_json.empty() ? "{}" : arguments_json);
    } catch (const std::exception& ex) {
        return ErrorResult(std::string("Invalid window_automation arguments: ") + ex.what());
    }
    if (!args.is_object()) {
        return ErrorResult("window_automation arguments must be a JSON object.");
    }

    const std::string action = WindowAutomationNormalize(args.value("action", "list_windows"));
    if (WindowAutomationIsWebView2Action(action)) {
        return CallWindowAutomationWebView2(action, args);
    }

    if (action == "listwindows") {
        const auto windows = WindowAutomationListWindows(args);
        const int max_windows = std::clamp(args.value("max_windows", 100), 1, 500);
        nlohmann::json arr = nlohmann::json::array();
        for (int i = 0; i < static_cast<int>(windows.size()) && i < max_windows; ++i) {
            arr.push_back(WindowAutomationWindowJson(windows[static_cast<size_t>(i)], i));
        }
        return WindowAutomationJsonResult({
            {"success", true},
            {"action", "list_windows"},
            {"count", arr.size()},
            {"windows", arr}
        });
    }

    std::string window_error;
    const auto window = WindowAutomationResolveWindow(args, &window_error);
    if (!window) {
        return ErrorResult(window_error);
    }

    if (action == "activatewindow") {
        const bool activated = WindowAutomationActivateWindow(window->hwnd);
        return WindowAutomationJsonResult({
            {"success", activated},
            {"action", "activate_window"},
            {"activated", activated},
            {"window", WindowAutomationWindowJson(*window, 0)}
        }, activated);
    }

    WindowAutomationComApartment apartment;
    if (FAILED(apartment.hr)) {
        return ErrorResult("Could not initialize COM for Windows UI Automation: " + WindowAutomationHresult(apartment.hr));
    }
    Microsoft::WRL::ComPtr<IUIAutomation> automation;
    HRESULT hr = CoCreateInstance(
        CLSID_CUIAutomation,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&automation));
    if (FAILED(hr) || !automation) {
        return ErrorResult("Could not create the Windows UI Automation object: " + WindowAutomationHresult(hr));
    }

    Microsoft::WRL::ComPtr<IUIAutomationElement> root;
    hr = automation->ElementFromHandle(window->hwnd, &root);
    if (FAILED(hr) || !root) {
        return ErrorResult("Could not get a UI Automation element for the target window: " + WindowAutomationHresult(hr));
    }

    const int max_depth = std::clamp(args.value("max_depth", 6), 0, 20);
    const int max_elements = std::clamp(args.value("max_elements", 300), 1, 2000);
    const bool include_offscreen = args.value("include_offscreen", false);
    auto elements = WindowAutomationCollectElements(
        automation.Get(), root.Get(), max_depth, max_elements, include_offscreen);

    if (action == "inspectwindow") {
        nlohmann::json arr = nlohmann::json::array();
        for (int i = 0; i < static_cast<int>(elements.size()); ++i) {
            arr.push_back(WindowAutomationElementJson(elements[static_cast<size_t>(i)].Get(), i));
        }
        return WindowAutomationJsonResult({
            {"success", true},
            {"action", "inspect_window"},
            {"window", WindowAutomationWindowJson(*window, 0)},
            {"max_depth", max_depth},
            {"max_elements", max_elements},
            {"include_offscreen", include_offscreen},
            {"element_count", arr.size()},
            {"elements", arr}
        });
    }

    if (action != "click" && action != "settext" && action != "typetext") {
        return ErrorResult("window_automation action must be list_windows, activate_window, inspect_window, click, set_text, type_text, webview2_list_targets, webview2_inspect, webview2_click, webview2_set_text, or webview2_type_text.");
    }

    const bool activate_first = args.value("activate_first", true);
    if (activate_first) {
        WindowAutomationActivateWindow(window->hwnd);
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }

    Microsoft::WRL::ComPtr<IUIAutomationElement> target;
    if (action == "typetext" && !WindowAutomationHasElementSelector(args)) {
        target = root;
    } else {
        std::string element_error;
        target = WindowAutomationFindElement(elements, args, &element_error);
        if (!target) {
            return ErrorResult(element_error);
        }
    }

    WindowAutomationScrollIntoView(target.Get());
    if (action == "click") {
        const bool prefer_mouse = args.value("prefer_mouse", false);
        if (!prefer_mouse) {
            Microsoft::WRL::ComPtr<IUIAutomationInvokePattern> invoke;
            if (SUCCEEDED(target->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke))) && invoke) {
                hr = invoke->Invoke();
                if (SUCCEEDED(hr)) {
                    return WindowAutomationJsonResult({
                        {"success", true},
                        {"action", "click"},
                        {"method", "invoke_pattern"},
                        {"window", WindowAutomationWindowJson(*window, 0)},
                        {"element", WindowAutomationElementJson(target.Get(), -1)}
                    });
                }
            }
            Microsoft::WRL::ComPtr<IUIAutomationSelectionItemPattern> selection;
            if (SUCCEEDED(target->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&selection))) && selection) {
                hr = selection->Select();
                if (SUCCEEDED(hr)) {
                    return WindowAutomationJsonResult({
                        {"success", true},
                        {"action", "click"},
                        {"method", "selection_item_pattern"},
                        {"window", WindowAutomationWindowJson(*window, 0)},
                        {"element", WindowAutomationElementJson(target.Get(), -1)}
                    });
                }
            }
            Microsoft::WRL::ComPtr<IUIAutomationTogglePattern> toggle;
            if (SUCCEEDED(target->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&toggle))) && toggle) {
                hr = toggle->Toggle();
                if (SUCCEEDED(hr)) {
                    return WindowAutomationJsonResult({
                        {"success", true},
                        {"action", "click"},
                        {"method", "toggle_pattern"},
                        {"window", WindowAutomationWindowJson(*window, 0)},
                        {"element", WindowAutomationElementJson(target.Get(), -1)}
                    });
                }
            }
        }
        const auto point = WindowAutomationElementClickPoint(target.Get());
        if (!point) {
            return ErrorResult("The target UIA element does not expose a clickable point or usable bounding rectangle.");
        }
        const bool clicked = WindowAutomationMouseClickPoint(*point);
        return WindowAutomationJsonResult({
            {"success", clicked},
            {"action", "click"},
            {"method", "mouse"},
            {"point", {{"x", point->x}, {"y", point->y}}},
            {"window", WindowAutomationWindowJson(*window, 0)},
            {"element", WindowAutomationElementJson(target.Get(), -1)}
        }, clicked);
    }

    const std::string value = args.value("value", "");
    if (action == "settext") {
        target->SetFocus();
        Microsoft::WRL::ComPtr<IUIAutomationValuePattern> value_pattern;
        if (SUCCEEDED(target->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value_pattern))) && value_pattern) {
            BOOL read_only = FALSE;
            value_pattern->get_CurrentIsReadOnly(&read_only);
            if (!read_only) {
                const std::wstring wide = Utf8ToWide(value);
                BSTR bstr_value = SysAllocStringLen(wide.data(), static_cast<UINT>(wide.size()));
                hr = value_pattern->SetValue(bstr_value);
                SysFreeString(bstr_value);
                if (SUCCEEDED(hr)) {
                    if (args.value("press_enter", false)) WindowAutomationSendEnter();
                    return WindowAutomationJsonResult({
                        {"success", true},
                        {"action", "set_text"},
                        {"method", "value_pattern"},
                        {"window", WindowAutomationWindowJson(*window, 0)},
                        {"element", WindowAutomationElementJson(target.Get(), -1)}
                    });
                }
            }
        }
    }

    if (const auto point = WindowAutomationElementClickPoint(target.Get())) {
        WindowAutomationMouseClickPoint(*point);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    } else {
        target->SetFocus();
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }
    const bool clear_existing = action == "settext"
        ? args.value("clear_existing", true)
        : args.value("clear_existing", false);
    if (clear_existing) {
        WindowAutomationSendCtrlA();
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    WindowAutomationSendUnicodeText(value);
    if (args.value("press_enter", false)) {
        WindowAutomationSendEnter();
    }
    return WindowAutomationJsonResult({
        {"success", true},
        {"action", action == "settext" ? "set_text" : "type_text"},
        {"method", "keyboard"},
        {"window", WindowAutomationWindowJson(*window, 0)},
        {"element", WindowAutomationElementJson(target.Get(), -1)}
    });
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
    std::string status;
    if (args.contains("status")) {
        if (args["status"].is_string()) {
            status = LowerAsciiCopy(Trim(args["status"].get<std::string>()));
        } else {
            status = LowerAsciiCopy(Trim(args["status"].dump()));
        }
    }
    bool completed = false;
    if (args.contains("completed")) {
        if (args["completed"].is_boolean()) {
            completed = args["completed"].get<bool>();
        } else if (args["completed"].is_string()) {
            const std::string completed_text =
                LowerAsciiCopy(Trim(args["completed"].get<std::string>()));
            completed = completed_text == "true" ||
                        completed_text == "yes" ||
                        completed_text == "1" ||
                        completed_text == "completed" ||
                        completed_text == "complete" ||
                        completed_text == "done";
        }
    }
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

inline McpToolCallResult CallSleep(
    const std::string& arguments_json,
    int max_sleep_seconds,
    std::function<bool()> should_cancel = {}) {
    nlohmann::json args;
    try {
        args = nlohmann::json::parse(arguments_json.empty() ? "{}" : arguments_json);
    } catch (const std::exception& ex) {
        return ErrorResult(std::string("Invalid sleep_seconds arguments: ") + ex.what());
    }

    if (!args.contains("seconds")) {
        return ErrorResult("sleep_seconds requires a seconds value.");
    }

    long long requested_seconds = 0;
    try {
        const auto& seconds_j = args["seconds"];
        if (seconds_j.is_number_unsigned()) {
            const auto raw_seconds = seconds_j.get<unsigned long long>();
            if (raw_seconds > static_cast<unsigned long long>(std::numeric_limits<long long>::max())) {
                return ErrorResult("sleep_seconds seconds value is too large.");
            }
            requested_seconds = static_cast<long long>(raw_seconds);
        } else if (seconds_j.is_number_integer()) {
            requested_seconds = seconds_j.get<long long>();
        } else if (seconds_j.is_number_float()) {
            const double raw_seconds = seconds_j.get<double>();
            if (raw_seconds < 0.0) {
                return ErrorResult("sleep_seconds seconds must be zero or greater.");
            }
            if (raw_seconds > static_cast<double>(std::numeric_limits<long long>::max())) {
                return ErrorResult("sleep_seconds seconds value is too large.");
            }
            requested_seconds = static_cast<long long>(raw_seconds);
            if (static_cast<double>(requested_seconds) != raw_seconds) {
                return ErrorResult("sleep_seconds seconds must be a whole number.");
            }
        } else if (seconds_j.is_string()) {
            std::string seconds_text = Trim(seconds_j.get<std::string>());
            size_t parsed = 0;
            requested_seconds = std::stoll(seconds_text, &parsed);
            if (parsed != seconds_text.size()) {
                return ErrorResult("sleep_seconds seconds must be a whole number.");
            }
        } else {
            return ErrorResult("sleep_seconds seconds must be a whole number.");
        }
    } catch (const std::exception&) {
        return ErrorResult("sleep_seconds seconds must be a whole number.");
    }

    if (requested_seconds < 0) {
        return ErrorResult("sleep_seconds seconds must be zero or greater.");
    }

    max_sleep_seconds = std::max(0, max_sleep_seconds);
    if (max_sleep_seconds > 0 && requested_seconds > max_sleep_seconds) {
        std::ostringstream err;
        err << "sleep_seconds requested " << requested_seconds
            << " seconds, but this project limits each sleep to "
            << max_sleep_seconds << " seconds.";
        return ErrorResult(err.str());
    }

    long long slept_seconds = 0;
    for (; slept_seconds < requested_seconds; ++slept_seconds) {
        if (should_cancel && should_cancel()) {
            McpToolCallResult result;
            result.success = false;
            result.is_tool_error = true;
            nlohmann::json payload = {
                {"tool", kSleepToolName},
                {"success", false},
                {"cancelled", true},
                {"requested_seconds", requested_seconds},
                {"slept_seconds", slept_seconds},
                {"max_sleep_seconds", max_sleep_seconds},
            };
            result.raw_result_json = payload.dump(2);
            result.content_text = "Sleep cancelled after " + std::to_string(slept_seconds) +
                " of " + std::to_string(requested_seconds) + " seconds.";
            return result;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    McpToolCallResult result;
    result.success = true;
    nlohmann::json payload = {
        {"tool", kSleepToolName},
        {"success", true},
        {"cancelled", false},
        {"requested_seconds", requested_seconds},
        {"slept_seconds", slept_seconds},
        {"max_sleep_seconds", max_sleep_seconds},
    };
    result.raw_result_json = payload.dump(2);
    result.content_text = "Slept for " + std::to_string(slept_seconds) + " seconds.";
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
           section == "blockers" || section == "notes" || section == "tool_hints" ||
           section == "phases";
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

inline std::string PlannerIdAsString(const nlohmann::json& id) {
    if (id.is_string()) return id.get<std::string>();
    if (id.is_number_integer()) return std::to_string(id.get<long long>());
    if (id.is_number_unsigned()) return std::to_string(id.get<unsigned long long>());
    return id.dump();
}

inline void CollectPlannerIdsRecursive(
    const nlohmann::json& value,
    std::unordered_set<std::string>* ids) {
    if (!ids) return;
    if (value.is_object()) {
        if (value.contains("id")) {
            ids->insert(PlannerIdAsString(value["id"]));
        }
        for (const auto& item : value.items()) {
            CollectPlannerIdsRecursive(item.value(), ids);
        }
    } else if (value.is_array()) {
        for (const auto& item : value) {
            CollectPlannerIdsRecursive(item, ids);
        }
    }
}

inline std::string UniquePlannerId(
    const std::string& preferred,
    std::unordered_set<std::string>* ids) {
    if (!ids) return preferred;
    std::string candidate = preferred.empty() ? "item" : preferred;
    if (ids->insert(candidate).second) {
        return candidate;
    }
    for (int suffix = 2; ; ++suffix) {
        const std::string next = candidate + "_" + std::to_string(suffix);
        if (ids->insert(next).second) {
            return next;
        }
    }
}

inline int ExtractPlannerPhaseNumber(const nlohmann::json& phase, size_t fallback_index) {
    std::string text;
    if (phase.is_object()) {
        if (phase.contains("name") && phase["name"].is_string()) {
            text = phase["name"].get<std::string>();
        } else if (phase.contains("title") && phase["title"].is_string()) {
            text = phase["title"].get<std::string>();
        }
    }
    const std::string lowered = LowerAsciiCopy(text);
    size_t pos = lowered.find("phase");
    if (pos != std::string::npos) {
        pos += 5;
        while (pos < lowered.size() &&
               !std::isdigit(static_cast<unsigned char>(lowered[pos]))) {
            ++pos;
        }
        int value = 0;
        while (pos < lowered.size() &&
               std::isdigit(static_cast<unsigned char>(lowered[pos]))) {
            value = value * 10 + (lowered[pos] - '0');
            ++pos;
        }
        if (value > 0) {
            return value;
        }
    }
    return static_cast<int>(fallback_index + 1);
}

inline bool PlannerSectionShouldHaveStatus(const std::string& section) {
    return section == "goals" || section == "subgoals" ||
           section == "features" || section == "steps" ||
           section == "blockers";
}

inline std::string PlannerChildIdPrefix(
    const std::string& parent_id,
    const std::string& section,
    size_t index) {
    const size_t ordinal = index + 1;
    if (section == "steps") return parent_id + "s" + std::to_string(ordinal);
    if (section == "goals" || section == "subgoals") return parent_id + "g" + std::to_string(ordinal);
    if (section == "features") return parent_id + "f" + std::to_string(ordinal);
    if (section == "blockers") return parent_id + "b" + std::to_string(ordinal);
    if (section == "notes") return parent_id + "n" + std::to_string(ordinal);
    if (section == "tool_hints") return parent_id + "t" + std::to_string(ordinal);
    return parent_id + "_" + std::to_string(ordinal);
}

inline std::string PlannerTopLevelIdPrefix(
    const std::string& section,
    const nlohmann::json& item,
    size_t index) {
    const size_t ordinal = index + 1;
    if (section == "phases") {
        return "p" + std::to_string(ExtractPlannerPhaseNumber(item, index));
    }
    if (section == "steps") return "s" + std::to_string(ordinal);
    if (section == "goals") return "g" + std::to_string(ordinal);
    if (section == "features") return "f" + std::to_string(ordinal);
    if (section == "blockers") return "b" + std::to_string(ordinal);
    if (section == "notes") return "n" + std::to_string(ordinal);
    if (section == "tool_hints") return "t" + std::to_string(ordinal);
    return "item" + std::to_string(ordinal);
}

inline bool PlannerObjectLooksTrackable(const nlohmann::json& item) {
    if (!item.is_object()) return false;
    for (const char* key : {
             "id", "status", "task", "title", "name", "done_when",
             "tool_hint", "goals", "subgoals", "features", "steps",
             "blockers", "notes", "tool_hints"}) {
        if (item.contains(key)) {
            return true;
        }
    }
    return false;
}

inline void NormalizePlannerArray(
    nlohmann::json& items,
    const std::string& section,
    const std::string& parent_id,
    std::unordered_set<std::string>* ids,
    bool* changed);

inline void NormalizePlannerObject(
    nlohmann::json& item,
    const std::string& section,
    const std::string& suggested_id,
    std::unordered_set<std::string>* ids,
    bool* changed) {
    if (!item.is_object()) return;

    if (!item.contains("id") && PlannerObjectLooksTrackable(item)) {
        item["id"] = UniquePlannerId(suggested_id, ids);
        if (changed) *changed = true;
    }
    const std::string item_id =
        item.contains("id") ? PlannerIdAsString(item["id"]) : suggested_id;

    if (PlannerSectionShouldHaveStatus(section) && !item.contains("status")) {
        item["status"] = "pending";
        if (changed) *changed = true;
    }

    for (const char* child_section : {
             "goals", "subgoals", "features", "steps", "blockers",
             "notes", "tool_hints"}) {
        if (item.contains(child_section) && item[child_section].is_array()) {
            NormalizePlannerArray(
                item[child_section],
                child_section,
                item_id,
                ids,
                changed);
        }
    }
}

inline void NormalizePlannerArray(
    nlohmann::json& items,
    const std::string& section,
    const std::string& parent_id,
    std::unordered_set<std::string>* ids,
    bool* changed) {
    if (!items.is_array()) return;
    for (size_t i = 0; i < items.size(); ++i) {
        if (!items[i].is_object()) continue;
        const std::string suggested_id = parent_id.empty()
            ? PlannerTopLevelIdPrefix(section, items[i], i)
            : PlannerChildIdPrefix(parent_id, section, i);
        NormalizePlannerObject(items[i], section, suggested_id, ids, changed);
    }
}

inline bool NormalizePlannerDocument(nlohmann::json& plan) {
    if (!plan.is_object()) return false;
    bool changed = false;
    std::unordered_set<std::string> ids;
    CollectPlannerIdsRecursive(plan, &ids);
    for (const char* section : {
             "goals", "features", "steps", "blockers", "notes",
             "tool_hints", "phases"}) {
        if (plan.contains(section) && plan[section].is_array()) {
            NormalizePlannerArray(plan[section], section, "", &ids, &changed);
        }
    }
    return changed;
}

inline std::string PlannerItemNotFoundMessage(const std::string& section) {
    return "Planner item not found in section " + section + ". "
        "Do not retry the same id blindly. Call project_planner with {\"action\":\"get\"} "
        "and use an id from the returned planner, or update the full plan with action=update if the item does not exist yet.";
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
        for (const char* key : {"goals", "subgoals", "features", "steps", "blockers", "notes", "tool_hints", "phases"}) {
            if (item.contains(key)) {
                UpdateMaxPlannerItemId(item[key], max_id);
            }
        }
    }
}

inline int NextPlannerItemIdInPlan(const nlohmann::json& plan) {
    int max_id = 0;
    for (const char* section : {"goals", "features", "steps", "blockers", "notes", "tool_hints", "phases"}) {
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
        for (const char* key : {"goals", "subgoals", "features", "steps", "blockers", "notes", "tool_hints", "phases"}) {
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
    for (const char* section : {"goals", "features", "steps", "blockers", "notes", "tool_hints", "phases"}) {
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
        for (const char* key : {"goals", "subgoals", "features", "steps", "blockers", "notes", "tool_hints", "phases"}) {
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
    for (const char* section : {"goals", "features", "steps", "blockers", "notes", "tool_hints", "phases"}) {
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
    bool normalized = NormalizePlannerDocument(plan);

    bool changed = false;
    if (action == "get") {
        if (normalized) {
            plan["updated_at"] = CurrentTimestampUtc();
            if (!SavePlannerDocument(file_path, plan, &error)) {
                return ErrorResult(error);
            }
        }
        return PlannerResult(action, file_path_utf8, plan, normalized);
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
                return ErrorResult(PlannerItemNotFoundMessage(section));
            }
            changed = true;
        } else {
            nlohmann::json* item = FindPlannerItemInPlan(plan, args["id"], section);
            if (!item) return ErrorResult(PlannerItemNotFoundMessage(section));
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

    if (NormalizePlannerDocument(plan)) {
        changed = true;
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

inline std::string FormatFilesystemByte(unsigned char value) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex
           << std::setw(2) << std::setfill('0')
           << static_cast<int>(value);
    return stream.str();
}

inline bool ValidateUtf8Text(const std::string& value,
                             size_t* invalid_index,
                             unsigned char* invalid_byte) {
    for (size_t i = 0; i < value.size();) {
        const unsigned char byte = static_cast<unsigned char>(value[i]);
        if (byte == 0) {
            if (invalid_index) *invalid_index = i;
            if (invalid_byte) *invalid_byte = byte;
            return false;
        }
        if (byte <= 0x7F) {
            ++i;
            continue;
        }

        size_t continuation_count = 0;
        uint32_t codepoint = 0;
        if ((byte & 0xE0) == 0xC0) {
            if (byte < 0xC2) {
                if (invalid_index) *invalid_index = i;
                if (invalid_byte) *invalid_byte = byte;
                return false;
            }
            continuation_count = 1;
            codepoint = byte & 0x1F;
        } else if ((byte & 0xF0) == 0xE0) {
            continuation_count = 2;
            codepoint = byte & 0x0F;
        } else if ((byte & 0xF8) == 0xF0) {
            if (byte > 0xF4) {
                if (invalid_index) *invalid_index = i;
                if (invalid_byte) *invalid_byte = byte;
                return false;
            }
            continuation_count = 3;
            codepoint = byte & 0x07;
        } else {
            if (invalid_index) *invalid_index = i;
            if (invalid_byte) *invalid_byte = byte;
            return false;
        }

        if (i + continuation_count >= value.size()) {
            if (invalid_index) *invalid_index = i;
            if (invalid_byte) *invalid_byte = byte;
            return false;
        }
        for (size_t j = 1; j <= continuation_count; ++j) {
            const unsigned char next = static_cast<unsigned char>(value[i + j]);
            if ((next & 0xC0) != 0x80) {
                if (invalid_index) *invalid_index = i + j;
                if (invalid_byte) *invalid_byte = next;
                return false;
            }
            codepoint = (codepoint << 6) | (next & 0x3F);
        }
        if ((continuation_count == 2 && codepoint < 0x800) ||
            (continuation_count == 3 && codepoint < 0x10000) ||
            (continuation_count == 2 && codepoint >= 0xD800 && codepoint <= 0xDFFF) ||
            codepoint > 0x10FFFF) {
            if (invalid_index) *invalid_index = i;
            if (invalid_byte) *invalid_byte = byte;
            return false;
        }
        i += continuation_count + 1;
    }
    return true;
}

inline bool ReadWholeFileUtf8(const std::filesystem::path& path, std::string* out, std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        if (error) *error = "Could not open file for reading: " + WideToUtf8(path.wstring());
        return false;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    std::string content = stream.str();
    size_t invalid_index = 0;
    unsigned char invalid_byte = 0;
    if (!ValidateUtf8Text(content, &invalid_index, &invalid_byte)) {
        if (error) {
            *error = "File is not valid UTF-8 text or appears to be binary: " +
                WideToUtf8(path.wstring()) + " (invalid byte " +
                FormatFilesystemByte(invalid_byte) + " at offset " +
                std::to_string(invalid_index) + "). Use list_directory for binary files; do not read them as text.";
        }
        return false;
    }
    *out = std::move(content);
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

inline std::vector<std::string> SplitFilesystemEditLineItem(const std::string& value, bool* normalized_multiline) {
    std::vector<std::string> lines;
    std::string current;
    bool saw_line_break = false;
    for (size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        if (ch == '\r' || ch == '\n') {
            saw_line_break = true;
            lines.push_back(current);
            current.clear();
            if (ch == '\r' && i + 1 < value.size() && value[i + 1] == '\n') {
                ++i;
            }
            continue;
        }
        current.push_back(ch);
    }

    if (!saw_line_break || !current.empty()) {
        lines.push_back(current);
    }
    if (lines.empty()) {
        lines.push_back("");
    }
    if (normalized_multiline && saw_line_break) {
        *normalized_multiline = true;
    }
    return lines;
}

inline std::vector<std::string> ParseFilesystemEditLines(const nlohmann::json& json_lines, bool* normalized_multiline) {
    std::vector<std::string> lines;
    if (!json_lines.is_array()) {
        return lines;
    }
    for (const auto& item : json_lines) {
        if (!item.is_string()) {
            continue;
        }
        const std::vector<std::string> split = SplitFilesystemEditLineItem(item.get<std::string>(), normalized_multiline);
        lines.insert(lines.end(), split.begin(), split.end());
    }
    return lines;
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

struct FilesystemEditDiagnostics {
    bool has_near_match = false;
    size_t near_match_index = 0;
    size_t near_match_score = 0;
    std::string summary;
};

inline FilesystemEditDiagnostics ComputeEditDiagnostics(
    const std::vector<std::string>& file_lines,
    const std::vector<std::string>& old_lines) {
    FilesystemEditDiagnostics diag;
    if (old_lines.empty() || file_lines.empty() || old_lines.size() > file_lines.size()) {
        diag.summary = "old_lines is empty, or longer than the file.";
        return diag;
    }
    size_t best_score = 0;
    size_t best_index = 0;
    for (size_t i = 0; i + old_lines.size() <= file_lines.size(); ++i) {
        size_t score = 0;
        for (size_t j = 0; j < old_lines.size(); ++j) {
            if (TrimRightCopy(file_lines[i + j]) == TrimRightCopy(old_lines[j])) {
                ++score;
            }
        }
        if (score > best_score) {
            best_score = score;
            best_index = i;
        }
    }
    diag.near_match_score = best_score;
    diag.near_match_index = best_index;
    diag.has_near_match = best_score > 0;
    if (diag.has_near_match) {
        std::ostringstream oss;
        oss << "Closest match at line " << (best_index + 1) << " (score " << best_score << "/" << old_lines.size() << ").\n";
        for (size_t j = 0; j < old_lines.size() && j < 5; ++j) {
            const size_t file_idx = best_index + j;
            const bool line_match = TrimRightCopy(file_lines[file_idx]) == TrimRightCopy(old_lines[j]);
            oss << "  [" << (file_idx + 1) << "] " << (line_match ? "OK   " : "DIFF ") << "expected: " << old_lines[j] << "\n";
            oss << "       actual:   " << file_lines[file_idx] << "\n";
        }
        if (old_lines.size() > 5) {
            oss << "  ... (" << (old_lines.size() - 5) << " more lines)\n";
        }
        diag.summary = oss.str();
    } else {
        std::ostringstream oss;
        oss << "No near match found. First expected line: \"" << old_lines[0] << "\". ";
        bool first_line_found = false;
        for (size_t i = 0; i < file_lines.size(); ++i) {
            if (TrimRightCopy(file_lines[i]) == TrimRightCopy(old_lines[0])) {
                oss << "That line alone appears at line " << (i + 1) << ", but the surrounding block doesn't match.";
                first_line_found = true;
                break;
            }
        }
        if (!first_line_found) {
            oss << "That line does not appear anywhere in the file.";
        }
        diag.summary = oss.str();
    }
    return diag;
}

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
        bool changed = false;
        for (const auto& edit : edits) {
            if (!edit.is_object()) continue;
            const auto old_j = edit.value("old_lines", nlohmann::json::array());
            const auto new_j = edit.value("new_lines", nlohmann::json::array());

            // Validate old_lines format
            if (!old_j.is_array()) {
                return ErrorResult(
                    "Edit failed: 'old_lines' must be a JSON array of strings, e.g. [\"line 1\", \"line 2\"]. "
                    "Received a non-array value. If you sent a single string, wrap it in an array.");
            }
            if (old_j.empty()) {
                return ErrorResult("Edit failed: 'old_lines' array is empty. Provide at least one line to match.");
            }
            for (const auto& item : old_j) {
                if (!item.is_string()) {
                    return ErrorResult(
                        "Edit failed: 'old_lines' must contain only strings. "
                        "Ensure each array element is a quoted string, not a number or object.");
                }
            }

            bool normalized_multiline = false;
            std::vector<std::string> old_lines = ParseFilesystemEditLines(old_j, &normalized_multiline);
            if (old_lines.empty()) {
                return ErrorResult("Edit failed: 'old_lines' parsed to empty lines. Each element must contain text.");
            }

            // Validate new_lines format
            if (!new_j.is_null() && !new_j.empty()) {
                if (!new_j.is_array()) {
                    return ErrorResult(
                        "Edit failed: 'new_lines' must be a JSON array of strings. "
                        "Received a non-array value.");
                }
                for (const auto& item : new_j) {
                    if (!item.is_string()) {
                        return ErrorResult(
                            "Edit failed: 'new_lines' must contain only strings.");
                    }
                }
            }
            std::vector<std::string> new_lines = ParseFilesystemEditLines(new_j, &normalized_multiline);

            const FilesystemEditMatch match = FindFilesystemEditMatch(lines, old_lines, new_lines, target);
            if (!match.found) {
                if (!new_lines.empty()) {
                    const FilesystemEditMatch already_applied = FindFilesystemEditMatch(lines, new_lines, new_lines, target);
                    if (already_applied.found) {
                        match_modes.push_back(normalized_multiline ? "normalized_multiline+already_applied" : "already_applied");
                        continue;
                    }
                }
                const auto diag = ComputeEditDiagnostics(lines, old_lines);
                std::string msg = "Edit failed: old_lines not found in file. " + diag.summary +
                    " Tried exact, trailing-whitespace-tolerant, and relative-indentation matching. "
                    "For indentation-sensitive files, the relative block structure must still match. "
                    "Tip: re-read the file to get the exact current text before retrying.";
                return ErrorResult(msg);
            }
            lines.erase(lines.begin() + match.index, lines.begin() + match.index + old_lines.size());
            lines.insert(lines.begin() + match.index, match.replacement_lines.begin(), match.replacement_lines.end());
            changed = true;
            match_modes.push_back(normalized_multiline ? "normalized_multiline+" + match.mode : match.mode);
        }
        std::ostringstream out;
        for (size_t i = 0; i < lines.size(); ++i) { if (i > 0) out << "\n"; out << lines[i]; }
        if (changed) {
            std::string write_error;
            if (!WriteWholeFileUtf8(target, out.str(), &write_error)) return ErrorResult(write_error);
        }
        McpToolCallResult result;
        result.success = true;
        nlohmann::json payload = { {"tool", kFilesystemToolName}, {"success", true}, {"action", "edit"}, {"path", path_template}, {"changed", changed} };
        payload["match_modes"] = match_modes;
        if (!backup_rel.empty()) payload["backup_path"] = backup_rel;
        result.raw_result_json = payload.dump(2);
        result.content_text = changed ? "Edited file: " + path_template : "No filesystem changes needed: " + path_template;
        if (std::any_of(match_modes.begin(), match_modes.end(), [](const std::string& mode) { return mode.find("normalized_multiline") != std::string::npos; })) {
            result.content_text += " (normalized multiline edit blocks)";
        }
        if (std::any_of(match_modes.begin(), match_modes.end(), [](const std::string& mode) { return mode.find("whitespace") != std::string::npos || mode.find("relative_indentation") != std::string::npos; })) {
            result.content_text += " (used whitespace-tolerant matching)";
        }
        if (std::any_of(match_modes.begin(), match_modes.end(), [](const std::string& mode) { return mode.find("already_applied") != std::string::npos; })) {
            result.content_text += " (some edits were already applied)";
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
    std::function<McpToolCallResult(const std::string&)> questionnaire_wait = {},
    std::function<bool()> should_cancel = {}) {
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
    if (name == kSleepToolName && settings.built_in_sleep_enabled) {
        return CallSleep(arguments_json, NormalizedSleepMaxSeconds(settings), should_cancel);
    }
    if (name == kFilesystemToolName && settings.built_in_filesystem_enabled) {
        return CallFilesystem(arguments_json, settings, variables);
    }
    if (name == kBrowserSearchToolName && settings.built_in_browser_search_enabled) {
        return CallBrowserSearch(arguments_json, settings, variables);
    }
    if (name == kWindowAutomationToolName && settings.built_in_window_automation_enabled) {
        return CallWindowAutomation(arguments_json);
    }
    return ErrorResult("Built-in tool is not enabled for this project: " + name);
}

}  // namespace built_in_tools
