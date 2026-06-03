# MCP Servers and Project Settings

## MCP Servers

MCP servers are external processes whose tools can be offered to selected projects. Click `MCP Servers` from the desktop toolbar.

The manager includes:

- A global variable list with add, edit and remove actions.
- A server list and a tools list for the selected server.
- `Add Server`, `Edit Server`, `Remove Server`.
- `Connect`, `Disconnect`, `Refresh Tools`.
- `Project MCP`, which assigns server access for the active project.

## Add an MCP Server

1. Click `MCP Servers`, then `Add Server`.
2. Enter the process launch fields.
3. Add variables if project-specific paths or values are required.
4. Check `Enabled`.
5. Optionally check `Auto-connect when selected for a project`.
6. Click `Test`. Review `Diagnostics` until initialization succeeds.
7. Click `Add`.
8. Select a project and use `Project MCP` or `Project Settings` to enable the server for that project.

### MCP server fields

| Field | Description |
| --- | --- |
| `Name` | Display/server name supplied to model context. Use a clear unique name. |
| `Command` | Executable launcher, for example `npx` or `uvx`. |
| `Working Directory` | Startup directory. May contain a per-project variable for a per-project server. |
| `Arguments (one per line)` | One command argument per line, preserving spaces inside each line. |
| `Environment Variables (KEY=VALUE per line)` | Environment entries used when starting the process. |
| `Per project (default)` | Starts/configures the MCP server in project context and allows project variables. |
| `Shared per process` | Shares a process globally. Shared servers cannot use project/global project-resolved variables. |
| `Project Variables` | Variable definitions the project supplies when the MCP server is selected. |
| `Enabled` | Allows the definition to be used. |
| `Auto-connect when selected for a project` | Connects the process after it is assigned. |
| `Test` | Launches and initializes the draft configuration and shows diagnostics. |

### MCP variables

A variable definition has:

| Field | Description |
| --- | --- |
| Name | Letters, numbers, `_` and `-` only. |
| Description | Explains what a project administrator must provide. |
| Kind | Text/no special picker, `Folder`, or `File`. Folder/file variables show a browse control when assigned. |
| Inject into context | Includes the resolved variable and its description in model context. |

References in commands, paths, arguments and environment entries may use `$Name$` or `$<Name>$`.

`ProjectFolder` is the common path variable. A typical filesystem MCP definition uses `$ProjectFolder$` as its working directory and as an argument specifying the accessible root.

### Supplied/default MCP server patterns

The application includes setup code for the following typical definitions when initial server configuration is empty:

| Name | Launch form | Scope |
| --- | --- | --- |
| `duckduckgo` | `uvx duckduckgo-mcp-server` | Shared |
| `file-system` | `npx -y @modelcontextprotocol/server-filesystem $ProjectFolder$` | Per project |
| `sequential-thinking` | `npx -y @modelcontextprotocol/server-sequential-thinking` | Per project |
| `server-time` | `uvx mcp-server-time` | Shared |
| `server-git` | `uvx mcp-server-git` | Per project |
| `desktop-commander` | `npx -y @wonderwhy-er/desktop-commander@latest` | Per project |

Availability depends on the current configuration and installed runtimes. Always use `Test` before depending on a server in a project.

## Project Settings

Select a project in the left tree and click `Project Settings`. Settings are specific to that selected project.

### MCP server and model tool selection

The left side assigns tool sources:

| Control | Purpose |
| --- | --- |
| MCP server list and `Use this MCP server` | Allows the selected external MCP server in this project. |
| `Variables` beneath an MCP server | Supplies project-specific values for that selected server. |
| `Model Tools` list | Enables globally defined model tools for this project. |

MCP tool access is permission-bearing: enable only server processes and paths appropriate for the project and its web users.

### Basic model and web description

| Setting | Behavior |
| --- | --- |
| `Web Description (max 300 characters)` | One-line project purpose shown beside the selected web chat name. Text is normalized and limited to 300 characters. |
| `AI Model` | Default provider/model used by this project. |
| `Model timeout (seconds, 0 = infinite)` | Request time limit for model execution; `0` disables the limit. |
| `User Select Model` | Allows web users to choose among the permitted model list while chatting. |
| `User Selectable Models (stream + tools)` | Approved alternatives for web users. Only models supporting streaming and tool use are offered. The `AI Model` selection remains the default. |

The web chat persists the user's most recently selected permitted model for that chat workflow.

### Context compression

| Setting | Behavior |
| --- | --- |
| `Context Window Compression` | Selects one global configuration created in `Context Window`. |
| `Force compression at input tokens` | When checked, forces compression once estimated input tokens reach the entered threshold, separately from the strategy's normal trigger. |
| `Allow manual context window compression` | Permits an operator/user flow to manually request compression for this project. |

See [04 - Model Tools, Agentic Modes, and Context Windows](04-Model-Tools-Agentic-Modes-and-Context-Windows.md) for strategy selection and stateless prompt configuration.

### Built-in tools

Select a built-in tool in the list to display its tool settings. The visible tool controls are:

| Tool / setting | Behavior |
| --- | --- |
| `Enable PowerShell command execution` | Gives the model command execution ability. `Default folder` defaults to `$ProjectFolder$`. Use only for trusted workflows. |
| `Enable Artifact/Code Memory tools` | Enables stored artifact/code memory tools used by configured workflows, including hierarchical context memory where applicable. |
| `Enable Planner / Task Decomposition` | Enables plan/task tools. `Storage folder` defaults to `$ProjectFolder$\.agent`. |
| `Enable Completion Driver` | Makes the completion driver available. It expects the agent to use its done/complete operation at the end of a completed task. |
| `Allowed agentic modes` | Restricts completion-driver availability by mode. |
| `Max continuations` | Limits completion-driven continuation cycles; `0` means no configured limit. |
| `Overload Delay(s)` | Delay used by completion continuation behavior when overloaded. |
| `Enable User Questionnaire` | Allows the model to request structured user choices. |
| `Max options` | Limits questionnaire choices; accepted settings are constrained to 2 through 50. |
| `Only when in this agentic mode` and `Mode` | Restrict questionnaire use to a selected mode. |
| `Enable Project Filesystem tool` | Lets the model read/write within its configured `Working directory`, default `$ProjectFolder$`. |
| `Auto-archive file reads/writes into Artifact/Code Memory` | Captures filesystem activity into memory when both workflows are used. |
| `Enable Sleep / Pause tool` | Allows deliberate waiting/pause operations. |
| `Max sleep (seconds, 0 = unlimited)` | Upper limit on a sleep request. |
| `Enable Browser Web Search` | Exposes the built-in `browser_web_search` tool for undetected-playwright-backed Google/Bing search, rendered page text/HTML retrieval, and PDF capture. |
| `Primary web search tool` | Marks `browser_web_search` as the preferred broad-search path; DuckDuckGo/web MCP remains available as fallback, comparison, or URL retrieval when enabled. |
| `Configure...` | Opens the Browser Web Search configuration window for allowed engines, engine priority, default engine, visible/headless browser behavior, default content type, delay ranges, timeout, and the model-visible tool description. |
| `Enable Window Automation` | Exposes the built-in `window_automation` tool for listing top-level Windows, bringing a window forward, inspecting its native UI Automation tree, clicking controls, filling text fields, and inspecting/interacting with WebView2 DOM content through CDP when a remote debugging port is available. |

The tool definitions supplied to models include usage guidance. For PowerShell, still ensure the working directory and project instructions describe Windows-specific constraints when commands are important.

Browser Web Search uses a project-local `.agent/browser_search` folder for cookies and generated PDF output by default. The tool can perform `search`, `fetch`, or `search_and_fetch`, so a model can search and retrieve the selected result content in one tool call when exact page content is needed. `Setup System` installs the Python, Playwright, undetected-playwright, and Chromium dependencies for this tool. For manual setup, run `python -m pip install -r scripts/built_in_browser_search_requirements.txt`, then `python -m playwright install chromium` before enabling the tool on a machine for the first time.

Window Automation uses Windows UI Automation directly for native controls, so it does not need FlaUI or .NET at runtime. Some apps expose richer UIA trees than others; inspect the window first and prefer `automation_id` or exact `name` plus `control_type` selectors before using element indexes.

For WebView2 panes, UIA usually sees the host control but not the Chromium DOM. The same `window_automation` tool has `webview2_list_targets`, `webview2_inspect`, `webview2_click`, `webview2_set_text`, and `webview2_type_text` actions that attach through Chrome DevTools Protocol. Start the target app with `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=9222` before the WebView2 control is created, then call the tool with `debug_port: 9222`. `Setup System` installs the Playwright dependency used by this CDP path.

### RAG services

Select each attached/available RAG library and set its project binding:

| Setting | Behavior |
| --- | --- |
| `Enabled` | Allows the binding for this project. |
| `Read` | Permits retrieval and document reads. |
| `Write` | Permits generated content or ingest operations where exposed. |
| `Tool` | Exposes the RAG as active model tools. |
| `Delete` | Permits document deletion operations. |
| `Write file` | Permits the RAG tool to materialize an existing original or extracted document into `Write file folder`. |
| `Default ingest` | Uses this library as the default destination for generated RAG content when applicable. |
| `Priority` | Relative retrieval priority. |
| `Max chunks` | Project-specific result limit. |
| `Min confidence` / `Max confidence` | Default retrieval score limits. |
| `Write file folder` | Destination template for RAG write-to-drive tools; may use project variables. Missing destination folders can be created by the RAG tool. |
| `Retrieval mode` | `Both`, `Passive only`, `Active tool only`, or `Disabled`. |

Current behavior note: passive/pre-search RAG injection is inactive in this phase. Configure `Read` and `Tool` and instruct the model to use the active RAG tools when it needs library content.

### Project variables

The `Project Variables` area defines values available to runtime instructions and tools:

| Field | Meaning |
| --- | --- |
| Variable name | Name used by `$Name$` or `$<Name>$` references. |
| Description | Explanation presented in context when injection is enabled. |
| Value | Administrator-set default value. |
| `Inject this variable into the context window` | Supplies the resolved value and description to the model. |
| `Allow user definition` | Adds a text value field to the web new-chat dialog so the web user can set a per-chat value. |

`ProjectFolder` is special. It cannot be enabled as ordinary user-defined text. Its web override is controlled through the privileged folder browsing settings described below.

Runtime values such as project name, chat name, chat ID and user-specific folder values can be resolved into templates. A new-chat folder using `$CHATNAME$` updates its preview as a name is typed and creates the folder only after the user clicks `Create Chat`.

### Agentic modes and instructions

| Setting | Behavior |
| --- | --- |
| `Default Agentic Mode` | Instruction mode applied unless the web chat has an allowed per-chat override. |
| `Enabled Agentic Modes` | Modes a web user may select for this project. |
| `Project Instructions` | Main project-level instruction text supplied to the model. |
| `Import Markdown` | Loads project instructions from a Markdown file. |
| `Check context window` | Shows a preview of system context, selected servers/RAGs, variables and estimated model-visible contents. |

### Diagnostics, web access and automation

| Setting | Behavior |
| --- | --- |
| `Enable detailed chat logging` | Persists expanded request/chat diagnostics used in desktop debugging. |
| `Enable Web Debugging` | Makes web debugging detail available for this project. |
| `Serve /data and /rag web file links inline (risky)` | Allows browser inline display rather than forced download for served files; use only when users are trusted to view the content. |
| `Enable automation sequence recording` | Records automation sequences for the project where supported by the web workflow. |
| `Allow privileged users to browse new chat locations, (set ProjectFolder variable)` | Permits folder selection at web chat creation only when the web user is also granted `Allow browsing folders` in `Admin Config`. |

### Privileged project folder workflow

Folder browsing requires both permissions:

1. In `Admin Config` -> `Users`, edit the user and enable `Allow browsing folders`.
2. In `Project Settings`, enable `Allow privileged users to browse new chat locations, (set ProjectFolder variable)`.

When both are enabled, the web `New Chat` dialog shows the default folder and a browse control. A typed target path that does not exist is created when the user confirms `Create Chat`, not while the user is typing.

## Persistence

The unified settings are stored at `.config/projects/<project_id>/project_settings.json`. MCP bindings and RAG bindings may also be persisted in their project-specific binding files for compatibility and runtime access:

- `.config/projects/<project_id>/project_mcp.json`
- `.config/projects/<project_id>/mcp_consent.json`
- `.config/projects/<project_id>/project_rag.json`
- `.config/projects/<project_id>/context_compression.json`
