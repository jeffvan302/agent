# AI Agent Desktop Application - Requirements Specification

**Platform:** Windows (Win32/Win64)
**Language:** C++ (C++20 or later)
**Version:** 0.1.0 - Active Proof-of-Concept Implementation
**Date:** April 12, 2026

---

## 1. Project Overview

A native Windows desktop application with a full graphical user interface (GUI) that serves as an AI agent interface. The application connects to any OpenAI-compatible API endpoint, supports the Model Context Protocol (MCP) for tool integration, and introduces a planned "Model-as-Tool" system where loaded LLM models can be registered as MCP-compatible tools for specialized sub-tasks.

**Every feature described in this document - configuration, model management, MCP server management, project/chat management, Model Tool authoring, logging, and diagnostics - must be accessible and manageable through the GUI.** While JSON config files remain human-editable as a fallback, the GUI is the primary interface for all operations.

### 1.1 Current Implementation Snapshot - April 12, 2026

The current proof-of-concept is a native Win32 C++20 desktop application with these implemented or partially implemented foundations:

- Provider and model management through `providers.json`, including model feature flags and an optional `context_window` value.
- Chat persistence under `data/projects/<project_id>/chats/<chat_id>`, with chat metadata and message history stored as JSON.
- Project persistence under `data/projects/<project_id>`, including project metadata, MCP bindings, MCP consent, and project variables.
- Real-time streaming chat output in the transcript pane, including auto-scroll while response text arrives.
- Tool-aware OpenAI-compatible chat requests, with MCP tools exposed through the `tools` array when available.
- Context-window preflight checks based on an estimated token count when a model has a positive `context_window`; blank or zero context windows are ignored.
- Stdio MCP server management, including one-screen add/edit, multiline arguments and environment variables, process testing, MCP initialize, `tools/list` diagnostics, and captured stdout/stderr.
- MCP server variables and global variables using `$Name$` or `$<Name>$` placeholders in command, working directory, arguments, and environment values.
- Per-project and shared MCP server scopes, with per-project variable values and context injection for variables marked for prompt context.
- Independent RAG libraries stored outside individual projects and attached to projects with read-only or read/write bindings.
- RAG library management, local document ingestion, folder ingestion with recursive option, rebuild workflow, progress display, rich document extraction, embedding runtime controls, and project RAG context injection.
- Local embedding provider support for Ollama and LM Studio, plus a `none` provider for keyword-only or diagnostic use.
- Current RAG retrieval using SQLite metadata, SQLite FTS5 keyword search, embedding BLOB cosine scan, and hybrid result merging.

### 1.2 Why C++

C++ was chosen as the implementation language for two strategic reasons:

- **Python integration path**: C++ provides a natural bridge to Python via embedding (CPython embedding API, pybind11, or Boost.Python). This is critical for the project's future roadmap — many MCP servers are written in Python, many AI/ML libraries are Python-native, and planned features (custom condensation strategies, embedding-based retrieval, local model inference) will leverage Python libraries. Embedding Python into a C++ host gives us the ability to call Python code at native speed without shelling out to a subprocess for every operation, share memory directly between C++ and Python, load Python-based MCP servers in-process when advantageous (reducing stdio overhead), and eventually run Python-based plugins and extensions within the agent itself.
- **Performance**: C++ delivers the low-level control and execution speed needed for high-throughput operations — concurrent MCP server communication across many simultaneous connections, real-time streaming token rendering with minimal latency, efficient JSON parsing and serialization for high-volume JSON-RPC traffic, memory-efficient management of large conversation histories and context windows, and future local model inference integration (GGML/llama.cpp are C/C++ native).

---

## 2. Core Architecture

- **GUI-first design**: The application is a fully graphical desktop application — there is no CLI-only mode. All configuration, management, and interaction happens through the GUI, with JSON files serving as the underlying persistence format that advanced users can also edit by hand
- The application acts as an **MCP Host** per the MCP specification — it creates and manages multiple MCP client instances, controls permissions, enforces security, and coordinates AI/LLM integration
- Each MCP server connection is handled by an isolated **MCP Client** instance with a 1:1 relationship to its server
- The application maintains a clear separation between these architectural layers:
  - **UI Layer** — All GUI rendering, user interaction, and display logic
  - **API Communication Layer** — HTTP client, SSE streaming, OpenAI-format request/response handling
  - **MCP Client Layer** — JSON-RPC messaging, transport management, protocol state machines
  - **Model-as-Tool Engine** — Virtual tool registry, sub-model dispatch, result formatting
  - **Project/Session Manager** — Chat persistence, context window management, condensation
  - **Python Bridge Layer** (future) — Embedded Python interpreter for in-process Python execution
- All networking must be asynchronous and non-blocking to keep the UI responsive
- The application must be fully self-contained on Windows with no dependency on WSL or Linux subsystems
- Thread architecture: the UI runs on the main thread; API calls, MCP communication, and tool execution each run on dedicated worker threads or a thread pool, communicating with the UI via message queues or callbacks

### 2.1 Future Python Integration Architecture

- The application will embed a Python interpreter (CPython) to enable direct in-process execution of Python code
- This will be used for: loading Python-based MCP servers without subprocess overhead, running Python-based condensation and retrieval strategies, executing Python plugins and custom tool logic, and interfacing with Python ML/AI libraries (transformers, sentence-transformers, etc.)
- The Python bridge will use pybind11 or the CPython C API to expose C++ objects to Python and call Python functions from C++
- Python execution will run on dedicated threads with the GIL managed appropriately to avoid blocking the UI
- This is a **future phase** item — the initial implementation uses subprocess-based stdio for all Python MCP servers, with the in-process bridge added later as an optimization

---

## 3. OpenAI-Compatible API Layer

### 3.1 Model Provider Configuration

- A JSON configuration file (e.g., `providers.json`) stores one or more API provider entries
- Each provider entry contains:
  - A human-readable **name** (e.g., "OpenAI", "Local Ollama", "Anthropic-via-OpenRouter")
  - The **base URL** for the API endpoint (e.g., `https://api.openai.com/v1`, `http://localhost:11434/v1`)
  - An **API key** field (can be empty for local endpoints that don't require auth)
  - A list of **available models** under that provider, each with:
    - Model ID string (e.g., `gpt-4o`, `claude-sonnet-4-20250514`)
    - A human-readable display name
    - Context window size (token limit)
    - Supported features flags (vision, function calling, streaming, etc.)
    - Cost metadata (input price per 1K tokens, output price per 1K tokens) - optional, for tracking
- Context window size is optional. If it is blank or zero, the application ignores it. If it is positive, the app estimates input plus reserved output tokens and warns/blocks before sending a request that is likely to exceed the model limit.
- The configuration file must be editable from within the application through a settings UI as well as by hand in a text editor
- Providers and models can be added, edited, reordered, and removed at runtime

### 3.2 API Communication

- All API calls follow the **OpenAI chat completions** format (`/v1/chat/completions`)
- Support for **streaming responses** via Server-Sent Events (SSE) is required - tokens must render incrementally in the chat window
- The current implementation renders streaming deltas into the main transcript pane while a request is running and auto-scrolls to the newest text.
- Tool-aware streaming is implemented so assistant text and tool calls can arrive during the same streamed response.
- Support for **function calling / tool use** as defined by the OpenAI API (the `tools` and `tool_choice` parameters) - this is how MCP tools are surfaced to the model
- Request construction must include: model selection, message history, system prompt, temperature, max tokens, and the tools array
- Responses must be parsed for: assistant content, tool call requests, finish reason, and usage statistics
- HTTP communication should use a robust library (e.g., libcurl, cpp-httplib, or WinHTTP) with TLS support
- Connection errors, rate limits (HTTP 429), and API errors must be handled gracefully with retry logic and user-visible feedback

### 3.3 Model Selection at Runtime

- A dropdown or selector in the main UI allows choosing the active model before or during a conversation
- The selector is grouped by provider and shows model display name, context window, and feature tags
- Switching models mid-conversation is allowed — the new model picks up the existing message history
- The selected model and provider are persisted per-chat so reopening a chat restores the last-used model

---

## 4. Model Context Protocol (MCP) Integration

### 4.1 MCP Server Configuration

- A JSON configuration file (e.g., `mcp_servers.json`) defines which MCP servers are available
- Each server entry contains:
  - A unique **identifier / name**
  - The **transport type**: either `stdio` or `streamable-http`
  - For `stdio` servers:
    - The **command** to launch the server process (e.g., `python`, `node`, `npx`, a path to an executable)
    - **Arguments** array (e.g., `["-m", "some_mcp_server"]`)
    - Optional **environment variables** to set for the subprocess
    - Optional **working directory**
  - For `streamable-http` servers:
    - The **URL** of the MCP endpoint
    - Optional **authentication headers** or token
  - An **enabled/disabled** flag
  - An **auto-connect** flag (whether to start this server on application launch)
- This configuration must be editable from within the application and by hand
- The current stdio MCP server editor keeps all add/edit settings on one screen: name, command, working directory, multiline arguments, multiline environment variables, scope, variables, diagnostics, test, save, and cancel.
- MCP server scope can be **per-project** or **shared**. New MCP servers default to per-project unless explicitly changed.
- MCP variables can be defined globally in the MCP Server Manager or per server. Variables support `None`, `Folder`, and `File` value types.
- Variable placeholders can be used in command, working directory, arguments, and environment variables with `$Name$` or `$<Name>$` syntax.
- Variables can be marked for context injection. When injected, both the current value and the variable description are included so the model understands what the value represents.
- The MCP test workflow launches the process, sends the MCP initialize request, sends the initialized notification, calls `tools/list`, and reports discovered tools plus stdout/stderr diagnostics.
- Connected and approved MCP tools are included in later chat requests for the selected project.

### 4.2 MCP Protocol Implementation — Full Spec Compliance

The application must implement the MCP specification version **2025-11-25** as both a compliant Host and Client.

#### 4.2.1 Base Protocol

- All messages use **JSON-RPC 2.0** encoding, UTF-8
- Support both defined transports:
  - **stdio**: Launch MCP server as a subprocess, write JSON-RPC to its stdin, read from its stdout, newline-delimited, capture stderr for logging
  - **Streamable HTTP**: POST JSON-RPC messages to the server's MCP endpoint, handle SSE streams for responses, support GET for server-initiated streams
- Implement the full **lifecycle**: initialization handshake (`initialize` / `initialized`), capability negotiation, normal operation, and graceful shutdown
- Implement **capability negotiation**: declare client capabilities (sampling, roots, elicitation, notifications) and read server capabilities (tools, resources, prompts, subscriptions, listChanged)
- Support **session management** for Streamable HTTP (track `MCP-Session-Id`, handle 404 for expired sessions, send DELETE on cleanup)

#### 4.2.2 Server Features (consumed by our client)

- **Tools**: Discover via `tools/list` (with pagination), invoke via `tools/call`, handle all result content types (text, image, audio, resource links, embedded resources, structured content), respect `outputSchema` when present, handle `notifications/tools/list_changed`
- **Resources**: Discover via `resources/list`, read via `resources/read`, support `resources/subscribe` and `resources/unsubscribe`, handle `notifications/resources/list_changed` and `notifications/resources/updated`
- **Prompts**: Discover via `prompts/list`, retrieve via `prompts/get` with arguments, handle `notifications/prompts/list_changed`

#### 4.2.3 Client Features (provided by our host to servers)

- **Roots**: Respond to `roots/list` requests from servers, provide filesystem or URI boundaries relevant to the current project, emit `notifications/roots/list_changed` when project context changes
- **Sampling**: Handle `sampling/createMessage` requests from servers — route them through the currently active model, present results back to the server, enforce user consent before allowing sampling
- **Elicitation**: Handle `elicitation/create` requests — surface prompts to the user for additional input, return user responses to the server

#### 4.2.4 Utilities

- **Progress tracking**: Handle `notifications/progress` from servers, display progress indicators in the UI
- **Cancellation**: Send `notifications/cancelled` when the user aborts an operation, handle cancellation from servers
- **Logging**: Handle `notifications/message` for server-side log output, route to an application log viewer
- **Ping**: Respond to `ping` requests for keepalive

### 4.3 MCP Server Lifecycle Management

- For stdio servers: manage the subprocess lifecycle (spawn, monitor, restart on crash if configured, kill on shutdown)
- For HTTP servers: manage connection state, reconnection, and session expiry
- Provide a UI panel or status bar showing which MCP servers are connected, their status (connecting, ready, error, disconnected), and their available capabilities
- Allow connecting and disconnecting individual servers at runtime without restarting the application

---

## 5. Model-as-Tool System (Custom Feature)

This is the novel extension beyond standard MCP. It allows any loaded model/provider to be wrapped and exposed as if it were an MCP tool, so the primary orchestrating model can delegate specialized sub-tasks to other models.

### 5.1 Concept

- A "Model Tool" is a virtual MCP tool that, when invoked, sends a request to a specific model on a specific provider and returns the result
- This enables multi-model workflows: e.g., a general-purpose orchestrator model can call a specialized coding model for code generation, a vision model for image analysis, or a small fast model for quick classification
- Model Tools appear in the tools array alongside all other MCP tools — the orchestrating model sees no difference

### 5.2 Model Tool Definition

- Model Tools are defined in a configuration file (e.g., `model_tools.json`) or created through a UI wizard
- Each Model Tool definition contains:
  - **Tool name**: A unique identifier following MCP tool naming rules (alphanumeric, underscore, hyphen, dot, 1–128 chars)
  - **Display name / title**: Human-readable label
  - **Description**: What this tool does, written to be consumed by the orchestrating model — this is critical for the model to know when and how to use it
  - **Target provider and model**: Which provider/model combination handles requests to this tool
  - **System prompt**: A specialized system prompt injected when calling the target model — this shapes the sub-model's behavior for this specific task
  - **Input schema**: JSON Schema defining the parameters the orchestrating model must supply when calling this tool (e.g., `{"type": "object", "properties": {"code_request": {"type": "string", "description": "Description of the code to write"}}, "required": ["code_request"]}`)
  - **Output schema** (optional): JSON Schema defining the expected structure of the output, enabling structured content extraction
  - **Output format instructions**: Additional instructions appended to the sub-model's prompt to enforce output formatting (e.g., "Respond only with the code block, no explanation")
  - **Temperature override**: Optional temperature setting for the sub-model call
  - **Max tokens override**: Optional max_tokens for the sub-model call
  - **Context inclusion policy**: Whether to pass the full conversation history to the sub-model, a summary, or only the tool input — this controls cost and relevance
  - **Enabled/disabled flag**

### 5.3 Model Tool Execution Flow

- When the orchestrating model emits a tool call targeting a Model Tool, the agent:
  1. Extracts the arguments according to the input schema
  2. Constructs a new API request to the target provider/model using the defined system prompt, input, and any context per the inclusion policy
  3. Sends the request and awaits the response (streaming internally if desired, but the final result is collected)
  4. Formats the response according to the output schema and returns it as a standard MCP tool result (text content, structured content, etc.)
  5. The orchestrating model receives the result and continues its reasoning
- Errors from the sub-model call are returned as tool errors so the orchestrating model can react

### 5.4 Model Tool Management UI

- A dedicated section in settings for creating, editing, duplicating, and deleting Model Tools
- A test/preview function that lets the user invoke the Model Tool manually with sample input to verify behavior before exposing it to the orchestrating model
- Import/export of Model Tool definitions as JSON for sharing configurations

---

## 5A. Model Tool Chaining & Tool Access Permissions

### 5A.1 Model Tool Chaining

- Model Tools support **chaining**: a Model Tool can invoke other Model Tools as part of its execution
- When defining a Model Tool, the author specifies whether chaining is enabled and, if so, which other Model Tools (by name) it is allowed to call
- The chaining depth is configurable with a maximum recursion limit (default: 5) to prevent infinite loops
- Each link in the chain is logged and visible in the tool call trace in the chat UI
- The orchestrating model does not need to be aware of chaining — it calls a single tool and receives the final result; the chain executes internally

### 5A.2 Tool Access Permissions

- Each Model Tool definition includes a **tool access list** that explicitly declares which other tools (MCP tools, other Model Tools, and built-in capabilities) the sub-model is allowed to use during its execution
- Built-in capabilities that can be individually granted or denied per Model Tool:
  - **File read**: Read files from disk within specified directories
  - **File write**: Write or create files within specified directories
  - **Web search**: Perform web searches via a configured search tool/MCP server
  - **Code execution**: Run code in a sandboxed environment (if available)
  - **MCP tool access**: A whitelist of specific MCP tools by name that this Model Tool may invoke
  - **Model Tool access**: A whitelist of other Model Tools that this Model Tool may chain into
- If no access list is specified, the Model Tool operates in **isolated mode** — it receives input, calls its target model, and returns the output with no access to any other tools
- The access list is enforced by the host: when the sub-model requests a tool call, the host checks it against the permission list and blocks unauthorized calls, returning an error to the sub-model
- This design lets you create tightly scoped tools (e.g., a "code reviewer" that can only read files and nothing else) alongside more capable tools (e.g., a "full-stack developer" that can read/write files, search the web, and chain into a testing tool)

---

## 5B. Agent Workflows

### 5B.1 Concept

- An **Agent Workflow** is a predefined, reusable sequence of steps that can be triggered as a single action by the user or by the orchestrating model
- Workflows automate multi-step processes that would otherwise require manual prompting at each stage
- Each workflow is a named, configured template that defines: the steps to execute, which tools and models are used at each step, how data flows between steps, and under what conditions the workflow branches or terminates

### 5B.2 Workflow Definition

- Workflows are defined in a configuration file (e.g., `workflows.json`) or created through a GUI workflow editor
- Each workflow contains:
  - **Name and description**: Human-readable identifier and purpose description
  - **Trigger**: How the workflow is invoked — manually by the user (button/command), automatically when the orchestrating model calls it as a tool, or on a schedule/event
  - **Input schema**: What parameters the workflow expects when started
  - **Steps**: An ordered list of steps, where each step is one of:
    - **Model call**: Send a prompt to a specified model with specified input, collect the output
    - **Tool call**: Invoke a specific MCP tool or Model Tool with specified arguments
    - **Conditional branch**: Evaluate a condition on the output of a previous step and branch to different subsequent steps
    - **Loop**: Repeat a step or group of steps until a condition is met or a max iteration count is reached
    - **User prompt**: Pause the workflow and ask the user for input or confirmation before continuing
    - **Transform**: Apply a data transformation (string manipulation, JSON extraction, format conversion) to the output of a previous step — these can be Python plugins (see §5C)
  - **Output mapping**: How the final output of the workflow is formatted and returned
  - **Error handling**: What to do when a step fails — retry, skip, abort, or branch to an error-handling path
  - **Enabled/disabled flag**

### 5B.3 Workflow Execution

- Workflows execute asynchronously — the user can observe progress in the chat UI as each step completes
- Each step's input and output is logged and visible in a workflow trace view
- The user can pause, resume, or cancel a running workflow at any step
- Workflows can run for extended periods (hours or days for long-running agent tasks) — the application must handle persistence of workflow state so that a workflow can survive application restarts

### 5B.4 Workflow Management UI

- A dedicated workflow editor in the GUI for creating, editing, and testing workflows
- Visual step editor — ideally a flowchart-style or sequential list view where steps can be added, reordered, and connected
- A test/dry-run mode that executes the workflow with sample input and shows results at each step
- Import/export of workflow definitions as JSON

---

## 5C. Python Plugin System

### 5C.1 Concept

- Beyond MCP tools and Model Tools, the application supports **Python plugins** as a third extension mechanism
- Python plugins are lightweight Python scripts or modules that hook into specific extension points in the application
- This is distinct from the Python bridge for MCP servers (§2.1) — plugins are application-level extensions, not protocol servers

### 5C.2 Plugin Extension Points

- **Context window processors**: Plugins that transform, filter, or augment the context window before it is sent to the model. Use cases include custom condensation strategies (replacing the default summarization approach), injecting context from external sources (databases, APIs, knowledge bases), redacting sensitive information from the context, reformatting or restructuring the message history for specific model preferences
- **Context tools**: Python-defined tools that operate on the conversation context itself — for example, a tool that searches the current conversation for specific information, a tool that extracts and organizes code blocks from the conversation, or a tool that generates a structured summary of decisions made so far
- **Input/output filters**: Plugins that process user input before it reaches the model or process model output before it is displayed — for example, auto-formatting, language detection, content moderation, or custom markdown extensions
- **Data transformers**: Plugins used within Agent Workflows (§5B) to transform data between steps — JSON manipulation, text extraction, format conversion, etc.

### 5C.3 Plugin Definition

- Each plugin is a Python file or package in a designated plugins directory
- Plugins declare their metadata via a standard header or manifest:
  - **Name and description**
  - **Extension point**: Which hook(s) this plugin implements
  - **Priority/order**: When multiple plugins hook the same extension point, priority determines execution order
  - **Configuration schema**: Any user-configurable parameters the plugin accepts
  - **Dependencies**: Required Python packages (installed via the bundled pip)
- Plugins are loaded by the embedded Python interpreter and executed in-process

### 5C.4 Plugin Management UI

- A GUI panel listing all discovered plugins, their status (enabled/disabled), and their extension point
- Per-plugin configuration forms generated from the plugin's configuration schema
- An install button for plugin dependencies (triggers pip install into the managed environment)
- A plugin log/output viewer for debugging

---

## 6. User Interface Layout

The GUI is the **sole interface** for this application. Every configurable aspect of the system — provider setup, model management, MCP server configuration, Model Tool authoring, project management, and diagnostics — must be reachable through the GUI without requiring the user to manually edit files.

### 6.1 Overall Structure

- A single-window application with a responsive layout
- Three primary regions: **Left Panel** (navigation/history), **Center Panel** (chat), and a potential **Right Panel** (reserved for future expansion — e.g., tool output, resource viewer, context inspector)
- The application should use a modern Windows UI framework: options include Win32 with custom rendering, Dear ImGui, or a webview-based approach (e.g., WebView2 with a React/HTML frontend) — decision to be made in design phase
- The window must support standard Windows behaviors: resizing, minimizing, maximizing, taskbar integration, system tray option
- A top **menu bar** or **toolbar** provides access to all management screens:
  - **File**: New Project, Open Project, Import/Export
  - **Settings**: Providers & Models, MCP Servers, Model Tools, Workflows, Python Plugins, Global Preferences
  - **View**: Toggle Left Panel, Toggle Log Viewer, Toggle Status Bar, Compact Mode, Always on Top, Theme (Light/Dark/System)
  - **Help**: About, Documentation links

### 6.2 Left Panel — Project & Chat Navigation

- The left panel is **collapsible**: it can be collapsed to a narrow strip showing only icons/minimal indicators, or expanded to show full project and chat names
- Collapse/expand is triggered by a toggle button or drag handle
- Content hierarchy:
  - **Projects** are the top-level organizer
    - Each project is associated with a **folder path** on disk where its data is stored
    - Projects are listed in the panel, each expandable to show its chats
  - **Chats** are listed under each project
    - Each chat has a title (auto-generated from first message or user-named)
    - Each chat shows a timestamp of last activity
    - Chats are sorted by most recent activity within their project
  - A "New Chat" button within each project to create a fresh conversation
  - A "New Project" button to create a new project (prompts for name and folder location)
- Clicking a chat loads it into the center panel
- Right-click context menus on projects and chats for: rename, delete, move, export, duplicate

### 6.3 Center Panel — Chat Interface

- The main conversation view showing the message history as a scrollable list
- Each message displays:
  - Role indicator (User, Assistant, System, Tool Result)
  - The message content with proper formatting (markdown rendering for assistant messages)
  - Timestamps
  - For tool calls: a collapsible section showing the tool name, arguments sent, and the result received
  - For Model Tool calls: an indicator showing which sub-model was used
- The **input area** at the bottom of the center panel includes:
  - A multi-line text input field for composing messages
  - A send button
  - An **attachment button** for multi-modal input (see §6.6)
  - The **model selector** — a dropdown showing the currently selected model, grouped by provider, allowing switching at any time
- A stop button that appears during streaming to cancel the current generation (sends cancellation to the API and to any active MCP tool calls)

### 6.4 Settings & Management Dialogs (GUI-Managed)

All of the following must be fully manageable through dedicated GUI dialogs or panels — not just by editing JSON files:

- **Provider & Model Manager**: A dialog for adding/editing/removing API providers and their models. Includes fields for name, base URL, API key (masked input with show/hide toggle), and a sub-list of models with all their properties. A "Test Connection" button to verify the endpoint is reachable and the key is valid
- **MCP Server Manager**: A dialog listing all configured MCP servers with their transport type, connection status, and capabilities. Includes forms for adding stdio servers (command, args, env vars, working directory) and HTTP servers (URL, auth). Connect/disconnect buttons per server. A live capability inspector showing what tools, resources, and prompts each connected server offers
- **Model Tool Editor**: A dedicated editor for creating and modifying Model Tools with fields for all properties defined in §5.2. Includes a live preview/test panel where the user can input sample arguments and see the sub-model's response before deploying the tool
- **Workflow Editor**: Visual editor for Agent Workflows as described in §5B.4
- **Python Plugin Manager**: Plugin discovery, enable/disable, configuration, and dependency management as described in §5C.4
- **Global Settings Panel**: UI for all settings in §8 — default model, temperature, UI theme, auto-connect preferences, etc.
- **Per-Project Settings**: Accessible via right-click on a project or a project properties dialog — system prompt, model override, active MCP servers for that project, root directories

### 6.5 Bottom Bar / Status Bar

- Shows the currently connected MCP servers and their status
- Shows token usage for the current conversation (total tokens used, context window remaining)
- Shows cost estimate if cost metadata is configured for the active model

### 6.6 Multi-Modal Input Handling

The application supports attaching files and media to messages, with intelligent preprocessing before content reaches the model.

- **Supported input types**:
  - **Images**: PNG, JPEG, GIF, WebP — passed directly to vision-capable models as base64 or URL references per the OpenAI API format
  - **PDFs**: Processed through a file processing pipeline (see below) rather than sent raw to the model
  - **Text files**: .txt, .md, .csv, .json, .xml, .log — content extracted and included as text in the message
  - **Code files**: Common source code extensions — syntax-detected and included as formatted code blocks
  - **Office documents**: .docx, .xlsx — content extracted to text/structured format via processing pipeline
  - **Audio files**: .mp3, .wav, .ogg — passed to models that support audio input, or transcribed first via a transcription tool/model
- **File processing pipeline**:
  - When a file is attached, the application determines the file type and applies the appropriate **input filter**
  - Input filters are configurable per file type — the user can choose how each type is processed:
    - **Pass-through**: Send the raw content directly (suitable for images, small text files)
    - **Extract text**: Parse the file and extract text content only (for PDFs, DOCX, XLSX)
    - **Summarize**: Run the extracted content through a designated model to produce a summary, then attach the summary instead of the full content (for large documents)
    - **Custom filter**: Route through a Python plugin (§5C) for custom extraction or transformation
  - The user can preview the processed output before sending, and choose a different filter if the result isn't right
- **Size management**: Large files are flagged with a warning showing estimated token count. The user can choose to include the full content, summarize, or extract only specific pages/sections
- **Attachment storage**: Attached files are copied into the chat's `attachments/` directory and referenced by path in the message history

### 6.7 Theming

- The application supports **light mode** and **dark mode** as built-in themes
- The active theme is selectable in global settings and can be toggled via a quick-access button in the toolbar or status bar
- The application should respect the Windows system theme preference by default (follow OS dark/light setting) with the option to override
- Custom accent colors are a future consideration but not required for initial release
- All UI elements, including dialogs, panels, the chat view, and code blocks, must adapt consistently to the active theme

### 6.8 Compact Mode

- The application supports a **compact mode** designed for running in a small window — e.g., tucked into a corner of the screen while the agent works autonomously
- Compact mode can be activated via a menu option, keyboard shortcut, or by resizing the window below a threshold
- In compact mode:
  - The left panel is fully hidden (not just collapsed — completely removed to save space)
  - The menu bar / toolbar is reduced to a minimal set of essential buttons (stop, model selector, settings gear)
  - The chat message display uses a denser layout with smaller fonts and reduced padding
  - The input area is minimized to a single-line input with an expand button
  - The status bar is condensed to a single-line summary (active model, connection status indicator)
- Compact mode is essential for long-running agent tasks where the application needs to stay visible and accessible without dominating screen real estate
- The window should support an **always-on-top** option (toggled in settings or via the compact mode toolbar) so it stays visible over other applications
- The application must remain fully functional in compact mode — the user can still send messages, switch models, and observe tool execution, just in a more condensed view

---

## 7. Project & Chat Data Management

### 7.1 Project Structure on Disk

- Each project maps to a directory on the filesystem
- Directory structure:
  ```
  <project_folder>/
    project.json            — Project metadata (name, creation date, settings overrides)
    chats/
      <chat_id>/
        chat.json           — Chat metadata (title, creation date, model used, etc.)
        messages.json       — Full message history in structured format
        context_summary.json — Condensed context summary (see §7.3)
        attachments/        — Any files attached to this chat
  ```
- All data is stored as human-readable JSON for transparency and debuggability

### 7.2 Message Format

- Each message in `messages.json` is an object containing:
  - `id`: Unique message identifier (UUID)
  - `role`: One of `system`, `user`, `assistant`, `tool`
  - `content`: The message text or structured content
  - `timestamp`: ISO 8601 timestamp
  - `model`: The model that generated this message (for assistant messages)
  - `tool_calls`: Array of tool call objects (for assistant messages that invoke tools)
  - `tool_call_id`: Reference to the tool call this message responds to (for tool result messages)
  - `token_usage`: Token counts for this message (prompt tokens, completion tokens) if available
  - `metadata`: Extensible metadata object for future use

### 7.3 Context Condensation Protocol

- As conversations grow long, a **context condensation** process runs to keep the active context within model limits
- The standard (v1) condensation protocol:
  - When the conversation approaches the context window limit (e.g., 80% of max tokens), the system:
    1. Takes the oldest N messages that are not pinned or system messages
    2. Sends them to the active model (or a designated summarization model) with a prompt asking for a structured summary
    3. Replaces those messages in the active context with a single `system` message containing the summary
    4. The full original messages are preserved in `messages.json` — only the active context window is affected
  - The summary is stored in `context_summary.json` with versioning so multiple rounds of condensation are tracked
- This protocol is designed to be **replaceable** — Python plugins (§5C) can implement alternative condensation strategies that are swapped in via configuration
- Pinning: Users can pin specific messages to prevent them from being condensed

### 7.4 RAG - Reusable Local Knowledge Libraries

- The application supports independent, reusable **RAG libraries** instead of one vector store owned by each project.
- A project can attach to one or more RAG libraries at the same time.
- Each project-to-RAG binding can be read-only or read/write. The data model also supports delete permission, retrieval priority, default ingest target, and per-binding max chunks.
- RAG libraries can be domain-specific, such as legal documents, HR documents, engineering notes, project source material, or task-specific research sets.
- RAG libraries are stored under `data/rag_libraries` by default, but each library can be created in a user-selected storage parent folder because these databases can become large.
- Each library stores configuration, managed source documents, extracted text, SQLite metadata, FTS indexes, embeddings, and future vector index files in its own folder.
- **Current ingestion support**:
  - Ingest individual files.
  - Ingest folders, with a recursive option.
  - Preview supported, skipped, and errored files before ingest.
  - Preserve original source metadata, including absolute source path and relative path from an ingested folder.
  - Copy managed originals into the RAG library so the database can be rebuilt later.
  - Rebuild a library by clearing indexed database rows and re-ingesting the saved original documents.
- **Current file support**:
  - Plain text, Markdown, JSON, CSV, logs, XML, common code/config file types, HTML, CSS, and SQL.
  - DOCX/DOCM extraction through native OOXML parsing.
  - XLSX/XLSM extraction through native OOXML parsing into Markdown table-style text.
  - PDF extraction through Poppler `pdftotext.exe`, MuPDF `mutool.exe`, Python `pypdf`, and a simple built-in fallback.
  - Large extracted documents can be split into overlapping Markdown segments before chunking.
- **Current embedding support**:
  - `none` for keyword-only or diagnostic indexing.
  - Ollama, defaulting to `nomic-embed-text` at `http://localhost:11434`.
  - LM Studio, defaulting to `nomic-embed-text-v1.5` at `http://localhost:1234/v1`.
  - Ollama status, start, stop, install, install-model, and embedding-test controls in the RAG library editor.
- **Current retrieval support**:
  - SQLite FTS5 keyword search.
  - SQLite embedding BLOB scan with cosine similarity.
  - Hybrid merge of vector and keyword results.
  - Fallback keyword scan when richer retrieval does not return results.
  - Project-level retrieval across all enabled readable libraries attached to the active project.
- **How RAG integrates with context management**:
  - When constructing a chat request, the app queries attached readable RAG libraries using the current user message.
  - The top matching chunks are formatted into a "Retrieved Project Knowledge" block and appended to the system prompt.
  - The block includes provenance such as RAG name, source document, chunk ID, source path, search method, score, metadata, and retrieved text.
- **Not yet implemented**:
  - Automatic indexing of chat messages, condensed summaries, and chat attachments.
  - A production vector backend such as HNSWlib, FAISS, Qdrant, sqlite-vss, or ChromaDB.
  - Token-budget-aware RAG context trimming based on the selected model's `context_window`.
  - Full per-project RAG binding editor for priority, max chunks, delete permission, and default ingest target.
  - OCR for scanned PDFs, audio transcription, image ingestion, model reranking, and advanced metadata filters.
- The long-term RAG pipeline remains a plugin/service boundary candidate. The current implementation runs in-process inside `agent.exe`, but the module boundary should remain clean enough to expose later as a plugin, DLL, sidecar service, or MCP server.

---

## 8. Configuration & Settings

- **Global settings** stored in a `settings.json` in the application data directory:
  - Default model and provider
  - UI preferences (theme selection: light/dark/system, font size, panel widths, compact mode defaults)
  - Default temperature, max tokens, and other API parameters
  - MCP server auto-connect preferences
  - File processing pipeline defaults (default filter per file type for multi-modal input)
  - Logging verbosity
  - Always-on-top preference
  - Compact mode threshold (window size at which compact mode auto-activates, or manual-only)
- **Per-project settings** that can override globals:
  - Project-specific system prompt
  - Project-specific model preference
  - Project-specific MCP server selection (which servers are active for this project, with per-server consent state)
  - Project-specific roots for MCP (filesystem boundaries)
  - RAG library bindings, including attached libraries, read/write mode, retrieval priority, max chunks, and default ingest target
  - Active Python plugins and their per-project configuration
  - Active workflows assigned to this project
- All settings files are JSON and human-editable

---

## 9. Security & Privacy

- API keys are stored encrypted at rest using Windows DPAPI (Data Protection API) — they must never be written to config files in plaintext
- **Consent model — per-server**: Tool invocations are authorized at the **server level**, not per-tool or per-invocation. When an MCP server is first connected, the user is shown the server's declared capabilities and the list of tools it offers. The user grants or denies consent for that server as a whole. Once a server is approved, all of its tools can be invoked without further confirmation. This is essential for long-running agent tasks that may execute for hours or days without user interaction — per-invocation approval would make autonomous operation impossible. The user can revoke a server's consent at any time, which immediately blocks all further tool calls to that server. The consent state is persisted per-project so different projects can trust different servers
- Sampling requests from MCP servers (where a server asks to use the LLM) follow the same per-server consent model — once a server is approved for sampling, it can issue sampling requests without per-request approval
- The application must validate the `Origin` header for any Streamable HTTP MCP connections to prevent DNS rebinding
- Model Tool access permissions (§5A.2) provide an additional layer of security — even within an approved server, Model Tools are restricted to only the tools explicitly listed in their access configuration
- No telemetry or data collection — all data stays local

---

## 10. Logging & Diagnostics

- An internal log viewer accessible from the UI showing:
  - API requests and responses (with sensitive data redacted)
  - MCP protocol messages (JSON-RPC traffic per server)
  - Tool call execution details and timing
  - Error and warning conditions
- Logs can be exported to a file for debugging
- Log levels: Error, Warning, Info, Debug, Trace

---

## 11. Implementation Phases

The phases are ordered by the principle of **prove it works, then build on it**. Each phase produces a usable increment that can be tested end-to-end before moving on. The first thing we need is a GUI that can talk to a model — everything else builds from there.

### Phase 1 — GUI Shell & Model Connectivity

The goal of this phase is a running application where you can configure a model provider, select a model, type a prompt, and get a streamed response back in a chat window. This is the proof-of-life that validates the entire API layer before anything else is built on top of it.

- Set up the C++ project structure, source file layout, and the `build.bat` build script (§12)
- Stand up the basic GUI window with the three-panel layout (left panel placeholder, center chat panel, bottom input area)
- Implement the **Provider & Model Manager** dialog — add/edit/remove providers with name, base URL, API key fields, and model list
- Implement `providers.json` persistence (read/write)
- Implement the **model selector** dropdown in the chat input area, populated from configured providers
- Implement the OpenAI-compatible API communication layer:
  - HTTP client with TLS support
  - `/v1/chat/completions` request construction (model, messages, system prompt, temperature, max_tokens)
  - **Streaming SSE** response parsing with incremental token rendering in the chat window
  - Error handling for connection failures, invalid keys (401), rate limits (429), and malformed responses
- Implement the basic chat message flow: user types message → request sent to selected model → streamed response rendered → both messages stored in history
- Implement basic project and chat persistence (create project, create chat, save/load `messages.json`)
- Implement the left panel with project/chat tree navigation (create, select, rename, delete)
- **Test connection** button in the Provider Manager that sends a minimal request to verify the endpoint and key work
- At the end of this phase: you can open the app, configure an OpenAI endpoint (or Ollama, or any OpenAI-compatible API), pick a model, and have a working streaming conversation

### Phase 2 — Basic MCP Tool Integration

The goal of this phase is to get MCP tools working end-to-end with practical servers — web search, fetch, filesystem access — so the model can actually do useful things beyond chat.

- Implement the MCP client for the **stdio transport**: subprocess spawning (`CreateProcess`), JSON-RPC 2.0 over stdin/stdout (newline-delimited), stderr capture for logging
- Implement the MCP **lifecycle**: `initialize` handshake, capability negotiation, `initialized` notification, graceful shutdown
- Implement **tool discovery** (`tools/list` with pagination) and **tool invocation** (`tools/call`) with all result content types (text, image, structured)
- Implement the **MCP Server Manager** dialog — add/edit/remove stdio servers (command, args, env vars, working directory), enable/disable, connect/disconnect per server, live status indicators
- Implement `mcp_servers.json` persistence
- **Bridge MCP tools to the OpenAI API layer**: convert discovered MCP tools into the OpenAI `tools` array format (function calling), parse tool call requests from model responses, dispatch to the correct MCP server, return results, and feed them back to the model for continued reasoning
- Display tool calls in the chat UI: collapsible sections showing tool name, arguments, and results
- Implement the **per-server consent model** (§9): on first connect, show the server's tools and get user approval; persist consent state per project
- Implement `notifications/tools/list_changed` handling so the tool list stays current
- **Validation target**: configure a web search MCP server and a fetch server, then have the model search the web and retrieve page content as part of a conversation

### Phase 3 — MCP Full Compliance & Streamable HTTP

Complete the MCP specification implementation so any MCP server — local or remote, stdio or HTTP — works correctly.

- Implement the **Streamable HTTP transport**: POST for sending JSON-RPC messages, SSE stream handling for responses, GET for server-initiated streams
- Implement **session management** for HTTP transport (`MCP-Session-Id` tracking, 404 handling for expired sessions, DELETE on cleanup)
- Implement **resources**: `resources/list`, `resources/read`, `resources/subscribe`/`resources/unsubscribe`, `notifications/resources/list_changed`, `notifications/resources/updated`
- Implement **prompts**: `prompts/list`, `prompts/get` with arguments, `notifications/prompts/list_changed`
- Implement **client features provided to servers**: roots (`roots/list`, `notifications/roots/list_changed`), sampling (`sampling/createMessage` routed through active model), elicitation (`elicitation/create` surfaced to the user)
- Implement all **utilities**: progress tracking (`notifications/progress` with UI progress bars), cancellation (`notifications/cancelled`), logging (`notifications/message` routed to log viewer), ping keepalive
- Implement full **capability negotiation** — declare all client capabilities, read and respect all server capabilities
- Add the **log viewer** panel for MCP JSON-RPC traffic inspection
- Update the MCP Server Manager to support HTTP server configuration (URL, auth headers)

### Phase 4 — Model-as-Tool System

Enable models to be wrapped as tools so the orchestrating model can delegate to specialized sub-models.

- Implement the **Model Tool definition format** and `model_tools.json` persistence
- Implement the **Model Tool execution engine**: intercept tool calls targeting a Model Tool, construct a sub-request to the target provider/model with the defined system prompt and input schema, collect the response, format it as a standard tool result
- Implement **Model Tool chaining** (§5A.1) with configurable max depth and loop detection
- Implement **tool access permissions** (§5A.2): whitelist enforcement for file read/write, web search, MCP tool access, and Model Tool chaining permissions
- Build the **Model Tool Editor** dialog: create, edit, duplicate, delete Model Tools with all fields from §5.2, plus the access permission configuration from §5A.2
- Implement the **test/preview panel** in the Model Tool Editor for invoking with sample input
- Integrate Model Tools into the unified tool list alongside MCP tools — the orchestrating model sees them identically
- Implement import/export of Model Tool definitions as JSON

### Phase 5 - Context Management & RAG

Give the application intelligent context handling so it can manage long conversations and recall information efficiently.

- Implement the **context condensation protocol** (§7.3): token counting, threshold detection, summarization via model call, summary versioning, message pinning. This remains pending.
- Implement optional model `context_window` support. This is implemented as a preflight estimate and warning/blocking check; blank or zero values are ignored.
- Implement the **RAG library system** (§7.4). The current proof-of-concept includes reusable libraries, project bindings, storage folders, SQLite metadata, FTS5, embedding storage, rich document extraction, ingest preview, rebuild, and query workflows.
- Implement the **embedding pipeline**. Local Ollama and LM Studio embedding providers are implemented; remote OpenAI-compatible embedding providers and local ONNX/Python embeddings remain pending.
- Implement **RAG integration with context construction**. Current chat requests can retrieve project RAG chunks and inject them into the system prompt as retrieved project knowledge.
- Implement **RAG indexing**. Explicit file and folder ingestion are implemented; automatic indexing of messages, summaries, and chat attachments remains pending.
- Build the **RAG configuration UI**. A RAG Service Manager and one-screen RAG library editor are implemented; the deeper project binding editor remains pending.
- Implement message pinning UI in the chat view (pin/unpin individual messages to protect from condensation). This remains pending.

### Phase 6 — Multi-Modal Input & UI Polish

Make the application handle diverse input types and refine the user experience.

- Implement **multi-modal input handling** (§6.6): attachment button, file type detection, processing pipeline with configurable filters (pass-through, extract text, summarize, custom)
- Implement file processing for: images (base64 encoding for vision models), PDFs (text extraction), text/code files (direct inclusion), office documents (text extraction)
- Implement the **preview-before-send** flow for processed attachments
- Implement **theming** (§6.7): light/dark mode toggle, system theme detection, consistent styling across all UI elements
- Implement **compact mode** (§6.8): minimal layout, always-on-top option, auto-activate on window resize threshold
- Implement **API key encryption** using Windows DPAPI — migrate plaintext keys on first run after upgrade
- Implement all remaining **settings UI** for global and per-project configuration
- Implement the **collapsible left panel** animation and drag handle
- Performance optimization, error handling hardening, edge case coverage across all existing features

### Phase 7 — Python Bridge & Plugins

Embed Python to unlock the plugin system and in-process Python execution.

- Bundle CPython into the application distribution (python3.dll + standard library + pip)
- Implement the pybind11 (or C API) bridge layer with proper GIL management on dedicated threads
- Expose core C++ APIs to Python: project context access, tool registry, message history, context window state
- Implement the **Python plugin system** (§5C): loader, extension point dispatch (context processors, context tools, input/output filters, data transformers), plugin lifecycle management
- Build the **plugin management UI** (§5C.4): discover plugins, enable/disable, per-plugin configuration forms, dependency installation via bundled pip
- Enable in-process loading of Python-based MCP servers as an optional alternative to stdio subprocess
- Implement local embedding models via the Python bridge (sentence-transformers) as an alternative to remote embedding APIs for RAG

### Phase 8 — Agent Workflows

Add the workflow system for automated multi-step agent processes.

- Implement the **workflow definition format** and `workflows.json` persistence (§5B)
- Implement the **workflow execution engine**: step sequencing, model calls, tool calls, conditional branching, loops, user prompts, transform steps
- Implement **workflow state persistence** so workflows survive application restarts
- Build the **workflow editor UI** (§5B.4): sequential/visual step editor, add/reorder/connect steps, test/dry-run mode
- Integrate workflows as callable tools so the orchestrating model can trigger them
- Implement workflow progress display in the chat UI with step-by-step trace view
- Implement workflow import/export

### Phase 9 — Local Inference & Future Features

Add local model execution and remaining future features.

- Integrate **llama.cpp** as a built-in provider (§12.3): model file loading (GGUF), GPU detection (CUDA, Vulkan), quantization options
- Implement the local inference configuration UI: model file browser, GPU settings, performance tuning
- Wire the local inference provider into the same provider abstraction so it works identically to API-based providers (streaming, tool use, model selector)
- Implement **conversation branching**: fork a conversation at any message point, creating a new chat that shares history up to the branch point and diverges from there

---

## 12. Build System & Compilation

### 12.1 Core Build Requirement

The project **must be compilable from a single batch file** (`build.bat`). This is a hard requirement — no IDE project files, no manual Visual Studio configuration, and no dependency on CMake GUIs or IDE-driven builds. A developer should be able to clone the repository, open a command prompt, run `build.bat`, and get a working executable.

### 12.2 Compiler

- The project uses the **Microsoft Visual C++ (MSVC) compiler** (`cl.exe`) from Visual Studio Build Tools or a full Visual Studio installation
- The batch file must locate and initialize the MSVC environment automatically by calling `vcvarsall.bat` (or `vcvars64.bat`) from the Visual Studio installation. It should search common installation paths and the `vswhere.exe` utility to find the correct version
- **C++ standard**: C++20 (`/std:c++20`) minimum — required for coroutines, concepts, `std::format`, `std::jthread`, `std::stop_token`
- Target architecture: **x64 only** (`/arch:x64` or by invoking the x64-native toolchain)

### 12.3 Batch File Structure (`build.bat`)

The batch file must handle the complete build lifecycle:

- **Environment setup**: Locate the MSVC toolchain, call `vcvarsall.bat x64` to set up the compiler environment (PATH, INCLUDE, LIB)
- **Dependency check**: Verify that required tools are available (cl.exe, link.exe, and any additional tools like rc.exe for resource compilation). Print clear error messages if anything is missing, including instructions on what to install
- **Compile**: Invoke `cl.exe` with the correct flags for all source files. Flags must include: C++20 standard, optimization level (debug vs release), warning level (`/W4` minimum), include paths for all dependencies (JSON library headers, UI framework headers, etc.), and any preprocessor definitions
- **Link**: Invoke `link.exe` (or let cl.exe drive linking) with the correct libraries — Windows system libraries (user32.lib, ws2_32.lib, crypt32.lib, etc.), any third-party static/dynamic libraries, and the application manifest/resources
- **Resource compilation**: If the application uses Windows resources (icons, version info, manifest), compile them with `rc.exe` and link the resulting `.res` file
- **Output**: Place the final executable and any required DLLs/assets into a clean output directory (e.g., `build/`)
- **Build configurations**: Support at least two configurations via a command-line argument:
  - `build.bat debug` — Debug build with symbols (`/Zi`), no optimization (`/Od`), debug runtime (`/MDd`), and debug preprocessor defines
  - `build.bat release` — Release build with full optimization (`/O2`), release runtime (`/MD`), and no debug symbols in the final binary
  - Default (no argument) should build debug

### 12.4 Batch File Example Structure

```batch
@echo off
setlocal enabledelayedexpansion

REM === Configuration ===
set BUILD_TYPE=%1
if "%BUILD_TYPE%"=="" set BUILD_TYPE=debug

REM === Locate MSVC ===
REM Try vswhere first, then fall back to known paths
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath 2^>nul`) do set VSINSTALL=%%i
if not defined VSINSTALL (
    echo ERROR: Visual Studio installation not found.
    echo Install Visual Studio Build Tools with C++ workload.
    exit /b 1
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (
    echo ERROR: Failed to initialize MSVC environment.
    exit /b 1
)

REM === Compiler Flags ===
if "%BUILD_TYPE%"=="release" (
    set CFLAGS=/std:c++20 /O2 /MD /W4 /EHsc /DNDEBUG
) else (
    set CFLAGS=/std:c++20 /Od /MDd /W4 /EHsc /Zi /D_DEBUG
)

REM === Build ===
REM ... compile sources, link, copy assets ...
```

This is illustrative — the actual batch file will grow as dependencies are added, but the pattern must remain: a single `build.bat` that handles everything.

### 12.5 Dependency Management

- All third-party dependencies should be either **header-only** (checked into the repository or fetched by a setup script), **pre-built static libraries** committed or downloaded by a setup step, or **git submodules** that are compiled as part of the batch file build
- A separate `setup.bat` (or a `--setup` flag on `build.bat`) may be provided for one-time tasks: cloning submodules, downloading pre-built libraries, or extracting the bundled Python distribution. This runs once after clone, not on every build
- The main `build.bat` must not require internet access after the initial setup — all dependencies are local after setup completes

### 12.6 No CMake Requirement

- While CMake is a common choice for C++ projects, this project **does not use CMake** as its build system. The batch file is the build system
- This decision is deliberate: the batch file keeps the build process transparent, auditable, and free of abstraction layers. Every compiler flag, include path, and library reference is visible in a single file
- If the project grows to the point where the batch file becomes unwieldy, a transition to CMake or a custom build script (Python or PowerShell) may be considered — but the batch file remains the starting point and must always be maintained as a working build path

---

## 13. Dependencies & Technology Decisions (To Be Finalized)

- **HTTP client**: libcurl, cpp-httplib, or WinHTTP — needs evaluation for SSE streaming support
- **JSON library**: nlohmann/json (widely used, header-only, good JSON Schema support) or RapidJSON (faster, more verbose)
- **UI framework**: To be decided — candidates:
  - **WebView2 + HTML/JS frontend**: Fastest to iterate on UI, modern look, but adds complexity in C++↔JS bridging
  - **Dear ImGui**: Quick to prototype, fully C++, but requires custom rendering and has a "dev tool" aesthetic
  - **Win32 + Custom Controls**: Maximum control, native feel, but highest development effort
  - **Qt**: Mature, cross-platform, rich widgets, but large dependency and licensing considerations
- **JSON-RPC library**: Likely custom-built on top of the JSON library, as MCP has specific requirements
- **Process management (stdio transport)**: Windows `CreateProcess` API for spawning MCP server subprocesses
- **Encryption**: Windows DPAPI for API key storage
- **Markdown rendering**: Depends on UI framework choice — may need a library or custom parser
- **Threading**: C++20 `std::jthread` and `std::stop_token` for cancellable worker threads, or a thread pool library
- **Async I/O**: Consider ASIO (standalone, non-Boost) for scalable async networking across many simultaneous MCP connections

### 13.1 Python Integration Dependencies

- **Python distribution**: **Bundled** — the application ships with an embedded CPython distribution (python3.dll + standard library + pip). This guarantees version consistency and eliminates dependency on the user's system Python installation. The bundled Python is isolated from any system Python. Estimated size overhead: ~30–40 MB
- **C++↔Python binding**: pybind11 (header-only, modern C++, widely used) or direct CPython C API
- **Python package management**: pip is included in the bundle. The application manages a dedicated virtual environment where plugin dependencies are installed. The GUI provides an interface for installing/removing packages (§5C.4)
- **GIL management**: Python calls must release the GIL when performing I/O or waiting, and acquire it properly when executing Python code — this is critical to avoid blocking the C++ UI thread

### 13.2 Platform Requirements

- **Minimum Windows version**: Windows 10 version 1903 (May 2019 Update) or later. This is required for WebView2 runtime support and modern Win32 APIs
- **Architecture**: x64 (64-bit) only — no 32-bit support. This simplifies the build and aligns with Python, CUDA, and llama.cpp requirements

### 13.3 Future: Local Model Inference

- The architecture should reserve space for a **local model inference** provider using llama.cpp / GGML
- This would appear as a built-in provider (alongside API-based providers) that runs models directly on the user's hardware without needing a separate server like Ollama
- Not required in the initial phases, but the provider abstraction (§3.1) must be designed so that a local inference backend can be added later without restructuring the API layer
- When implemented, this provider would support: model file loading (GGUF format), GPU acceleration (CUDA, Vulkan), quantization options, and all the same features as API providers (streaming, tool use) but running entirely locally

---

## 14. Resolved Decisions

The following items were previously open questions and have been decided:

- **Plugin/extension model**: The application supports three extension mechanisms: MCP tools (protocol-standard), Model Tools (custom model-as-tool), and Python plugins (for context processing, filters, and data transforms). MCP and Model Tools are not the only extension paths — Python plugins are a first-class system
- **Agent workflows**: Yes — Agent Workflows (§5B) are a defined feature. They allow single-action triggers that execute predefined multi-step sequences of tool calls and model interactions
- **Multi-modal input**: Yes — the application handles images, PDFs, documents, code files, and audio through a configurable file processing pipeline with preview-before-send (§6.6)
- **Model Tool chaining**: Yes — Model Tools can call other Model Tools with configurable depth limits (§5A.1)
- **Model Tool access permissions**: Yes — each Model Tool declares an explicit access list of what tools and capabilities it can use, enforced by the host (§5A.2)
- **Consent model**: Per-server. Consent is granted when an MCP server is first connected and applies to all tools on that server. This supports long-running autonomous agent tasks that may run for hours or days (§9)
- **RAG support**: Yes - independent reusable local RAG libraries can be attached to projects with read-only or read/write bindings. Current implementation includes local ingestion, SQLite/FTS5 metadata, local embedding providers, rebuild, diagnostics, and chat context injection. A production vector backend and automatic chat/message indexing remain future work (§7.4)
- **Conversation branching**: Future feature (Phase 9) — not needed initially but planned
- **Python distribution**: Bundled. The application ships with an embedded CPython distribution for guaranteed compatibility (§12.1)
- **Python MCP server execution**: Out-of-process only — via stdio or HTTP transport. In-process loading via the Python bridge is an optional optimization for later (§2.1)
- **Local model inference**: Planned for Phase 9 via llama.cpp. The provider abstraction is designed to accommodate it, but it is not required in initial phases (§12.3)
- **Minimum Windows version**: Windows 10 version 1903 (May 2019 Update) or later (§12.2)
- **Theming**: Required — light mode and dark mode with system theme detection (§6.7)
- **Compact mode**: Required — the application must function in a small window for autonomous operation, with always-on-top support (§6.8)

---

## 15. Remaining Open Questions

- What is the final UI framework choice? (WebView2, Dear ImGui, Qt, or Win32 custom) — this is the most impactful remaining decision
- Should there be a marketplace or registry for sharing Model Tool definitions and workflow templates?
- What accessibility requirements should the UI meet (screen reader support, keyboard navigation, high contrast)?
- Should the application support custom accent colors beyond light/dark mode?
- Should workflows support triggering on external events (file system changes, webhooks, scheduled times) in addition to manual and model-initiated triggers?
- What production vector backend should be added first for RAG? Current proof-of-concept uses SQLite embedding BLOB scan; candidates include HNSWlib, FAISS, Qdrant, sqlite-vss, and ChromaDB.
- How should advanced RAG metadata filtering, reranking, and result diversity be configured in the UI?
- When should the RAG engine be extracted from in-process `agent.exe` into a plugin, DLL, sidecar service, or MCP server?
- Cross-project RAG access is now handled by attaching shared libraries to projects. What export, security, and federation rules should exist for sharing those libraries across machines or users?
- Should the application support exporting entire projects (chats, context, vector store) as portable archives for backup or sharing?
