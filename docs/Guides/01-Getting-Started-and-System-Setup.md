# Getting Started and System Setup

## Desktop Purpose

Agent Desktop is the administrator and diagnostics application. The top toolbar opens configuration managers:

| Button | Purpose |
| --- | --- |
| `Providers` | Configure model connections and model capabilities. |
| `MCP Servers` | Configure external tool processes and shared variables. |
| `Project Settings` | Configure the selected project's runtime behavior. |
| `Model Tools` | Configure model-backed sub-agent tools. |
| `Agentic Modes` | Maintain reusable instruction modes. |
| `Web Config` | Configure and start the web chat server. |
| `Admin Config` | Manage web users, groups and project access. |
| `Remote Model Config` | Generate or edit HTTPS remote worker configurations. |
| `RAG Service` | Create/search/ingest/export RAG libraries and image ingestion settings. |
| `Context Window` | Create global compression strategies. |
| `Setup System` | Restore packaged defaults and install dependencies. |

The left tree lists projects and, when created from the website, their chats. The desktop includes `Check Context` and chat diagnostics rather than being the normal chat sending surface.

## First Configuration Without Resetting Existing Data

Use this approach if there might already be useful projects or configuration:

1. Open `Providers` and configure a provider/model.
2. Open `MCP Servers` and add or test external tools, if required.
3. Create a `Context Window` configuration if the project should compress history.
4. Create a project with `New Project`, or select an existing project.
5. Open `Project Settings` and choose its model, tools, RAG access and instructions.
6. Open `Web Config`, choose the interface/port/TLS settings, click `Save`, and click `Start`.
7. Open `Admin Config` to create web users and bind their groups to projects.
8. Open the served site from a browser and create a chat within an assigned project.

## Setup System

`Setup System` is an assisted bootstrap operation for a new or disposable installation. It does all of the following:

1. Requires confirmation of an overwrite warning.
2. Ensures `C:\Temp` exists because supplied project/tool workflows may use it.
3. Deletes all existing projects and project chat data.
4. Extracts the packaged `.config.zip` into the app's `.config` directory. Packaged config files overwrite matching existing config files; unrelated config files that are not in the package remain.
5. Reuses the registered `Agent App Documentation` RAG library when present, or imports its embedded archive into `.data\.app_rag` when absent.
6. Opens a visible terminal to install or update system tools.
7. Offers to restart the app after the terminal process completes.

The setup terminal installs or checks:

| Dependency | Reason it is installed |
| --- | --- |
| Node.js, `npm`, `npx` | JavaScript-based MCP servers |
| `uv`, `uvx` | Python-packaged MCP servers |
| Python 3.12 | Python runtime for browser-backed and WebView2 CDP built-in tools |
| Playwright + Chromium | Browser Web Search automation plus WebView2 CDP attach/DOM inspection support |
| undetected-playwright | Browser Web Search stealth launch layer to reduce Playwright detection signals |
| BeautifulSoup + lxml | Browser Web Search page text extraction |
| Poppler `pdftotext` | PDF text extraction |
| Tesseract | OCR for image ingestion |
| Pandoc | Document conversion |
| LibreOffice | Office document conversion |
| OpenSSL | HTTPS/TLS support |
| Ollama | Local embedding and vision model runtime |

It also attempts to pull Ollama models including `qwen2.5vl:7b`, `nomic-embed-text`, `moondream:1.8b`, `qwen3-embedding:0.6b` and `qwen3-embedding:latest`.

The built-in documentation library is compiled into `agent.exe` as the exported archive `Agent_App_Documentation-001.rag`. Setup registers it, but does not attach it to any project or expose it to any model. A project restored from `.config.zip` may contain an intentional read-only binding to it as an example, or an administrator can attach it later through `RAG Service` -> `Attach Read` and enable `Tool` only where a model should search it.

When refreshing the packaged documentation, replace `docs\Guides\Agent_App_Documentation-001.rag` before rebuilding. The build embeds the replacement archive. Setup imports it only when `Agent App Documentation` is not already registered; it does not replace an existing installed library. If the replacement export has a different internal RAG ID, update any `project_rag.json` example binding in `.config.zip` to that new ID.

Do not choose `Setup System` on an installation that contains project or chat data that has not been backed up.

## Projects

### Create a project

1. Click `New Project`.
2. Enter a project name in the creation dialog.
3. Configure initial MCP bindings if shown by the creation dialog.
4. Select the new project and use `Project Settings` for its full configuration.

Creating a project in the desktop does not create a web chat. Web users create chats after the project has been assigned to them.

### Clone a project

1. Select a source project in the left tree.
2. Click `Clone Project`.
3. Supply the name for the new project.
4. Open `Project Settings` for the clone and adjust it as needed.

A clone copies the operational configuration: project settings, effective MCP bindings, RAG bindings and the selected context compression assignment. It does not copy chats, message history, runtime state, user/group web assignments or generated data.

### Rename or delete

Use `Rename` for either a selected project or chat. Use `Delete` carefully: removing a project removes its project configuration and project chat data.

## Desktop Diagnostics

Select a project/chat and use the desktop diagnostic panel or `Check Context` to inspect:

- Resolved `ProjectFolder`.
- Chat directory and files.
- Web server state, bind address, ports, TLS mode and active sessions.
- Estimated stored user, assistant and tool-result token counts.
- Stored tool calls and model errors.
- The context sections that will be supplied to a model.

Exact provider-reported token usage is not persisted; displayed token values are local estimates.
