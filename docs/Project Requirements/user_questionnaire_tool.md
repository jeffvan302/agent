# User Questionnaire Tool

The User Questionnaire tool is a built-in tool that lets the model pause an agentic workflow, present a multiple-choice question to the user, and wait for an explicit selection before continuing.

It is designed for decisions that should not be guessed by the model, such as requirements, preferences, trade-offs, approvals, and planning choices.

## Purpose

Use the questionnaire when the next action depends on a user decision and the answer can be represented as a small set of choices.

Good uses include:

- Choosing between implementation approaches.
- Confirming whether to make a risky or broad change.
- Collecting UI/design preferences.
- Selecting a scope for a task.
- Asking which dependency, provider, file, feature, or plan branch to use.
- Continuing a long agent loop only after user approval.

Avoid using it for:

- Rhetorical questions.
- Questions the agent can answer by inspecting the project.
- Large free-form input, because the tool currently supports only predefined options.
- Trivial confirmations that do not affect behavior.

## Tool Identity

The tool name exposed to models is:

```text
user_questionnaire
```

The trace title is:

```text
Built-in / User Questionnaire
```

Implementation constants are in:

```text
src/built_in_tools.h
```

Relevant constants/functions:

- `built_in_tools::kQuestionnaireToolName`
- `built_in_tools::IsQuestionnaireEnabled(...)`
- `built_in_tools::BuildDefinitions(...)`
- `built_in_tools::CallQuestionnaire(...)`
- `built_in_tools::CallTool(...)`

## Availability

The questionnaire tool is exposed only when all required conditions are true:

- The selected model supports tools.
- Project Settings -> Built-in Tools -> User Questionnaire is enabled.
- If mode restriction is enabled, the effective agentic mode matches the configured allowed mode.

Mode restriction is checked by:

```cpp
inline bool IsQuestionnaireEnabled(const ProjectSettings& settings,
                                    const std::string& current_agentic_mode_id = {}) {
    if (!settings.built_in_questionnaire_enabled) return false;
    if (!settings.questionnaire_restrict_by_mode) return true;
    if (settings.questionnaire_allowed_mode_id.empty()) return false;
    return settings.questionnaire_allowed_mode_id == current_agentic_mode_id;
}
```

For the web UI, the effective agentic mode is the per-chat selected mode when present, otherwise the project default mode.

## Project Settings

Questionnaire settings are stored on `ProjectSettings` in `src/types.h`:

```cpp
bool built_in_questionnaire_enabled = false;
int questionnaire_max_options = 8;
bool questionnaire_restrict_by_mode = false;
std::string questionnaire_allowed_mode_id;
```

They are serialized in `src/storage.cpp` as:

```json
{
  "built_in_questionnaire_enabled": true,
  "questionnaire_max_options": 8,
  "questionnaire_restrict_by_mode": true,
  "questionnaire_allowed_mode_id": "mode_id_here"
}
```

The settings UI is implemented in:

```text
src/project_settings_dialog.cpp
src/project_settings_dialog.h
```

The settings panel includes:

- `Enable User Questionnaire`
- `Max options`
- `Only when in this agentic mode`
- `Mode`

`questionnaire_max_options` is clamped to the range `2..50` when settings are loaded or saved. The default is `8`.

## Tool Schema

The model calls `user_questionnaire` with this JSON shape:

```json
{
  "question": "Which implementation should I use?",
  "options": [
    "Small targeted fix",
    "Refactor the whole module",
    "Stop and explain trade-offs first"
  ],
  "allow_multiple": false
}
```

Required fields:

- `question`: The prompt shown to the user.
- `options`: An array of clickable answer labels.

Optional fields:

- `allow_multiple`: If true, the user may select more than one option. Defaults to false.

The tool definition is created in `BuildDefinitions(...)`:

```cpp
q.name = kQuestionnaireToolName;
q.description =
    "Present a multiple-choice question to the user and wait for their selection before continuing. "
    "Use this for clarifying requirements, collecting preferences, confirming trade-offs, or gathering planning decisions. "
    "The question appears as a bubble with clickable options in the web UI. The model should not assume the answer; "
    "it must wait until the user selects one of the provided choices and the tool result is injected back into context. "
    "Do not ask rhetorical questions without this tool when a decision is needed.";
```

## Tool Result

Successful responses return JSON with the original question, selected option indices, and selected labels.

Single-choice result example:

```json
{
  "success": true,
  "tool": "user_questionnaire",
  "question": "Which implementation should I use?",
  "selected_indices": [0],
  "selected_labels": ["Small targeted fix"]
}
```

Multiple-choice result example:

```json
{
  "success": true,
  "tool": "user_questionnaire",
  "question": "Which checks should I run?",
  "selected_indices": [0, 2],
  "selected_labels": ["Unit tests", "Debug build"]
}
```

The model should continue from the selected labels rather than re-asking the same question.

## Validation

`CallQuestionnaire(...)` validates that:

- The arguments are valid JSON.
- `question` is non-empty after trimming.
- `options` is a non-empty array.

Invalid arguments return a tool error.

Desktop execution performs the same basic validation before opening the modal dialog.

Web execution parses string options only. Non-string option values are ignored while building the visible option list.

## Desktop Flow

The desktop UI uses a modal Win32 dialog implemented in:

```text
src/questionnaire_dialog.h
src/questionnaire_dialog.cpp
```

The dialog input structure is:

```cpp
struct QuestionnaireOptions {
    std::wstring title = L"Question";
    std::wstring question;
    std::vector<std::wstring> labels;
    bool allow_multiple = false;
    int width = 460;
    int height = 240;
};
```

The dialog function is:

```cpp
std::optional<std::vector<int>> ShowQuestionnaireDialog(
    HWND owner,
    const QuestionnaireOptions& options);
```

Desktop behavior:

- Single-choice mode behaves like radio buttons and returns immediately when an option is clicked.
- Multiple-choice mode uses checkboxes and requires the user to click `Confirm`.
- `Cancel` or closing the dialog returns `std::nullopt` and becomes a tool error.
- The result uses zero-based option indices.

The desktop model/tool bridge is in `src/main.cpp`:

```cpp
static McpToolCallResult RunDesktopQuestionnaire(const std::string& arguments_json)
```

That function converts UTF-8 JSON arguments into `QuestionnaireOptions`, calls `ShowQuestionnaireDialog(...)`, and converts selected indices back into a JSON tool result.

## Web Flow

The web path uses a pending-question state plus a streaming UI event.

The pending state is declared in `src/web_server.h`:

```cpp
struct PendingQuestionnaire {
    std::string tool_call_id;
    std::string question;
    std::vector<std::string> options;
    bool allow_multiple = false;
    std::string answer_result;
    std::mutex mtx;
    std::condition_variable cv;
    bool answered = false;
    bool abandoned = false;
};
```

The pending questionnaires map is keyed by:

```text
<project_id>:<chat_id>
```

When the model calls `user_questionnaire`, `src/web_server.cpp`:

1. Parses `question`, `options`, and `allow_multiple`.
2. Saves current messages so the request state is durable before waiting.
3. Creates a `PendingQuestionnaire`.
4. Emits a streaming SSE event to the browser.
5. Waits on the pending questionnaire condition variable.
6. Receives the browser response through `POST /api/questionnaire-response`.
7. Injects the selected result back into the model conversation as the tool result.

The SSE payload sent to the browser has this shape:

```json
{
  "questionnaire": true,
  "tool_call_id": "call_123",
  "question": "Which approach should I use?",
  "options": ["Option A", "Option B"],
  "allow_multiple": false
}
```

The browser posts the answer to:

```text
POST /api/questionnaire-response
```

Request body:

```json
{
  "project_id": "project_id_here",
  "chat_id": "chat_id_here",
  "tool_call_id": "call_123",
  "selected_indices": [1]
}
```

The endpoint validates:

- Auth/session access.
- `project_id`, `chat_id`, and `tool_call_id` are present.
- The user can access the project.
- A pending questionnaire exists for the project/chat.
- The posted `tool_call_id` matches the pending questionnaire.

The web frontend rendering is implemented in:

```text
www/js/app.js
src/web_assets_default.cpp
```

Important frontend behavior:

- The questionnaire appears as a `Question` message bubble.
- Single-choice buttons post immediately when clicked.
- Multiple-choice buttons toggle selection state.
- Multi-select mode shows a `Confirm` button only after at least one option is selected.
- After multi-select confirmation, the button is disabled and changed to `Sent`.

When editing the managed web asset, update both `www/js/app.js` and `src/web_assets_default.cpp`.

## Dispatch Flow

Questionnaire dispatch starts from the shared built-in tool dispatcher:

```cpp
inline McpToolCallResult CallTool(
    const std::string& name,
    const std::string& arguments_json,
    const ProjectSettings& settings,
    const std::vector<ProjectMcpVariableValue>& variables,
    const std::string& current_agentic_mode_id = {},
    std::function<McpToolCallResult(const std::string&)> questionnaire_wait = {})
```

For `user_questionnaire`, `CallTool(...)` verifies `IsQuestionnaireEnabled(...)`, then calls `CallQuestionnaire(...)`.

`CallQuestionnaire(...)` is intentionally UI-agnostic. It validates arguments and delegates waiting to the supplied `questionnaire_wait` callback when one is provided.

The concrete UI waiting behavior is supplied by:

- Desktop: `RunDesktopQuestionnaire(...)` in `src/main.cpp`.
- Web: pending questionnaire state and `/api/questionnaire-response` in `src/web_server.cpp`.

## Recommended Model Behavior

Use this tool when a decision is required before continuing.

Good questionnaire:

```json
{
  "question": "How should I handle the API change?",
  "options": [
    "Make the smallest compatible fix",
    "Refactor the API usage everywhere",
    "Stop and show a migration plan first"
  ],
  "allow_multiple": false
}
```

Poor questionnaire:

```json
{
  "question": "Should I proceed?",
  "options": ["Yes", "No"]
}
```

The poor version is vague because it does not say what proceeding means. Prefer options that encode concrete outcomes.

For multi-select questions, make each option independently actionable:

```json
{
  "question": "Which verification steps should I run after this change?",
  "options": [
    "Run the debug build",
    "Run frontend syntax checks",
    "Run provider contract tests",
    "Skip verification and explain why"
  ],
  "allow_multiple": true
}
```

## Interaction With Agentic Modes

The questionnaire can be restricted to a single agentic mode.

This is useful when only some modes should interrupt the user for choices. For example:

- A fully autonomous mode may avoid questionnaires unless blocked.
- A review or planning mode may ask before selecting scope.
- A guided mode may ask more often for preferences.

In the web UI, a chat-level selected mode overrides the project default mode for tool availability. If the chat has no selected mode, the project default mode is used.

If restriction is enabled but no allowed mode is configured, `IsQuestionnaireEnabled(...)` returns false.

## Error Cases

Common tool errors include:

- Invalid JSON arguments.
- Empty `question`.
- Missing or empty `options`.
- The tool is not enabled for the current project or current mode.
- User cancelled the desktop dialog.
- Web questionnaire is not found, already answered, or has a mismatched `tool_call_id`.

The model should surface the failure clearly and avoid pretending a selection was made.

## Current Limitations

- The tool only supports predefined options, not free-text answers.
- The `questionnaire_max_options` project setting is stored and clamped, but the current tool execution path does not enforce the cap against the supplied `options` array.
- The web UI does not currently expose a cancel button for an active questionnaire.
- The web response endpoint ignores out-of-range indices when building `selected_labels`, but the raw `selected_indices` array is still included in the result.
- Pending web questionnaires are keyed by project/chat, so only one active questionnaire per chat is expected.
- The current web wait path blocks until the questionnaire is answered or marked abandoned; there is no explicit questionnaire timeout in the pending wait itself.

## Implementation Map

| Area | Files | Responsibility |
| --- | --- | --- |
| Tool definition and validation | `src/built_in_tools.h` | Tool name, schema, enablement check, shared dispatcher, basic argument validation. |
| Desktop UI | `src/questionnaire_dialog.h`, `src/questionnaire_dialog.cpp` | Modal Win32 questionnaire dialog and selected-index return value. |
| Desktop dispatch | `src/main.cpp` | Converts tool JSON into dialog options and returns selected labels to the model. |
| Web pending state | `src/web_server.h` | `PendingQuestionnaire`, mutex, condition variable, pending map. |
| Web tool wait and response | `src/web_server.cpp` | SSE event, pending wait, `/api/questionnaire-response`, selected-label result JSON. |
| Web frontend | `www/js/app.js`, `src/web_assets_default.cpp` | Renders the question bubble and posts selected option indices. |
| Settings UI | `src/project_settings_dialog.cpp`, `src/project_settings_dialog.h` | Built-in tool checkbox, max options field, mode restriction controls. |
| Settings persistence | `src/types.h`, `src/storage.cpp` | Project settings fields and JSON serialization/deserialization. |

## Summary

The User Questionnaire tool gives the agent a structured way to stop, ask the user for a concrete decision, and resume with a machine-readable answer. It is a built-in tool named `user_questionnaire`, enabled from Project Settings, optionally restricted by agentic mode, and implemented with both desktop modal and web streaming UI paths.
