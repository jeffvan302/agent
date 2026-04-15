# AI Agent Desktop Application - Requirements Specification

**Platform:** Windows (Win32/Win64)
**Language:** C++ (C++20 or later)
**Version:** 0.1.0 - Active Proof-of-Concept Implementation
**Date:** April 15, 2026

---

## 1. Project Overview

A native Windows desktop application with a full graphical user interface (GUI) that serves as an AI agent interface. The application connects to any OpenAI-compatible API endpoint, supports the Model Context Protocol (MCP) for tool integration, and introduces a planned "Model-as-Tool" system where loaded LLM models can be registered as MCP-compatible tools for specialized sub-tasks.

**Every feature described in this document - configuration, model management, MCP server management, project/chat management, Model Tool authoring, logging, and diagnostics - must be accessible and manageable through the GUI.** While JSON config files remain human-editable as a fallback, the GUI is the primary interface for all operations.

### 1.1 Current Implementation Snapshot - April 15, 2026

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
- System-wide RAG image ingestion settings, with CPU Tesseract OCR as the default path, optional PaddleOCR mode, and optional Ollama vision-language description mode for richer image/graph understanding.
- Local embedding provider support for Ollama and LM Studio, plus a `none` provider for keyword-only or diagnostic use.
- Current RAG retrieval using SQLite metadata, SQLite FTS5 keyword search, embedding BLOB cosine scan, and hybrid result merging.
- Built-in active RAG tools can be exposed to tool-capable models per project binding: list RAG libraries, search with confidence windows, retrieve extracted document text, and ingest generated documents into write-enabled RAGs.

### 1.1A Detailed Implementation Baseline - April 15, 2026

This section is the current source-of-truth implementation checkpoint. It captures features that have been added during the proof-of-concept, including several items that were added before they were fully reflected in the original phase roadmap.

#### Application Shell, Storage, And Build

- The application is a native Win32 C++20 desktop app built by `build.bat` into `build/agent.exe`.
- The main window currently uses a project/chat tree, model dropdown, provider/MCP/project/RAG/context buttons, transcript pane, tool trace pane, multiline prompt input, Send, Compress, Context Msgs, and status label.
- Application data is stored as JSON under the repository/app root during the proof-of-concept rather than in a Windows profile data directory.
- Provider config is stored in `providers.json`.
- MCP server config and global MCP variables are stored in `mcp_servers.json`.
- Projects are stored under `data/projects/<project_id>`.
- Chat metadata and messages are stored under `data/projects/<project_id>/chats/<chat_id>`.
- Chat request diagnostics are stored in `context_debug.json`.
- Chat compression state and history are stored in `compression_state.json` and `compression_history.json`.
- Unified project settings are stored in `project_settings.json`, with migration from older `project_mcp.json`, `project_rag.json`, and `context_compression.json` files.
- Context compression global configs are stored in `context_compression_configs.json`.

#### Chat And OpenAI-Compatible API

- Chat completions use the OpenAI-compatible `/v1/chat/completions` style request format.
- Streaming chat output is rendered incrementally into the transcript pane and auto-scrolls while responses arrive.
- Tool-aware streaming is implemented so assistant text and tool calls can be processed in the same request flow.
- Message history persists assistant tool-call requests and tool result messages, including tool call IDs and raw tool-call JSON when available.
- Invalid tool-call argument JSON is caught and returned to the model as a tool error instead of crashing or sending malformed arguments onward.
- The tool loop supports up to 8 tool-call rounds before failing with a loop-limit error.
- Model configuration includes optional `context_window`, `supports_streaming`, `supports_tools`, and `supports_vision` flags.
- If `context_window` is positive, the app estimates request input plus reserved output tokens and warns/blocks before sending requests that are likely too large.
- If `context_window` is blank or zero, context window preflight is ignored.

#### Provider And Model Management

- The Provider Manager can add, edit, remove, and save provider/model records.
- Providers include name, base URL, API key, and model list.
- Models include display name, context window, streaming/tool/vision support flags, and are selectable per chat.
- A provider test path exists for verifying a minimal connection.
- API keys are still stored in plaintext JSON; DPAPI encryption is not implemented yet.

#### MCP Implementation

- Stdio MCP servers are implemented using Windows process spawning and JSON-RPC over stdin/stdout.
- MCP lifecycle currently includes `initialize`, `notifications/initialized`, `tools/list`, and `tools/call`.
- Tool discovery supports pagination by continuing `tools/list` when `nextCursor` is returned.
- MCP tools are bridged into OpenAI-compatible `tools` definitions and invoked through the chat tool loop.
- Tool results are displayed in the tool trace pane and persisted as tool messages in chat history.
- MCP server add/edit is handled in one editor screen with name, command, arguments, working directory, environment entries, scope, variables, enabled, auto-connect, and Test.
- Multiline arguments and environment entries are supported.
- The MCP test workflow launches the configured process, sends initialize/initialized, calls `tools/list`, and captures stdin/stdout/stderr diagnostics.
- Windows command launching handles direct executables, `.cmd`/`.bat` through `cmd.exe`, and PowerShell scripts through PowerShell.
- MCP servers can be scoped as per-project process or shared process.
- Shared MCP servers cannot use project variables.
- MCP variables can be declared per server and globally.
- MCP variables support `None`, `Folder`, and `File` value kinds.
- Variables can be substituted in command, working directory, arguments, and environment entries using `$Name$` or `$<Name>$`.
- Variables can be marked for prompt-context injection; injected values include both value and description.
- Project Settings can select MCP servers and fill required variable values for the active project.
- Auto-connect is supported for selected servers.
- Streamable HTTP MCP, resources, prompts, roots, sampling, elicitation, cancellation, progress notifications, logging notifications, and ping are not implemented yet.

#### Project Settings

- Project Settings is now the central per-project configuration screen.
- It can edit project name and selected MCP servers.
- It can edit per-project MCP variable values, including browse buttons for Folder/File variable kinds.
- It can select a global context compression configuration for the project.
- It can enable/disable project RAG libraries and set Read, Write, Tool, Delete, Export, and Default ingest target flags.
- It can edit per-binding RAG retrieval priority, maximum chunks, and default minimum/maximum confidence thresholds.
- It can configure a per-binding RAG write-file folder path template for direct document export/write-to-drive workflows.
- The RAG write-file folder path template supports project variable placeholders such as `$<ProjectFolder>$`, resolved from the active project's MCP variable values at tool-call time.
- RAG Tool and Write file/export imply Enabled and Read.
- RAG Write implies Enabled and Read.
- RAG Delete and Default ingest target imply Enabled, Read, and Write.
- Only one RAG binding can be marked as the project's default ingest target at a time.
- Disabling RAG Read or Enabled clears dependent Tool, Write, Delete, Export, and Default ingest options.
- Project instructions are editable in a multiline scrollable text box.
- Project instructions can be imported from a Markdown file.
- Project instructions are injected into the chat system prompt as "Project Instructions".

#### Context Compression And Debugging

- A Context Window manager exists and stores global context compression configs.
- The manager supports a None strategy in data, plus editable Truncate Top and Hierarchical Structured strategies.
- Truncate Top keeps a configured number of recent messages and builds a compressed block from the older context.
- Hierarchical Structured compression implements Layer 1 pinned messages, Layer 2 running summary, Layer 3 structured state JSON, and Layer 4 recency window.
- Layer 1 settings include max pins plus code block, URL, number, first-message, explicit-instruction, and user-flagged pin options.
- Layer 2 and Layer 3 can each choose provider/model and max token settings.
- Layer 4 has a minimum recent-turn setting.
- Compression configs include frequency and context-window trigger percentage settings.
- Manual Compress rebuilds from the latest sent/received message range rather than repeatedly compressing an already-current compressed context.
- If no new messages exist since the last compression, Compress shows the existing compressed context instead of creating a duplicate snapshot.
- Compression snapshots preserve previous snapshot ID, previous message index, compressed-through index, previous compressed context, new compressed context, Layer 2 summary, Layer 3 state JSON, pinned messages, and source messages.
- The Context Msgs debug window shows a left-side list and right-side detail pane for saved prompts, assistant replies, tool calls, tool results, request context, and compression snapshots.
- Context debug entries capture system prompt, exact request messages, compressed context, MCP project context, and RAG context for each user send.
- Message pinning UI is not implemented yet.
- Token counting is still approximate and not tokenizer-specific.
- Compression failure handling and model-call diagnostics need more hardening.

#### RAG Implementation Summary

- Current RAG status: operational proof-of-concept. It can create reusable local libraries, ingest and rebuild documents, generate embeddings when configured, retrieve from attached libraries, inject passive context into chat requests, and expose model-callable RAG tools. It is usable for experimentation, but not yet production-grade for very large stores or high-safety persistent memory workflows.
- RAG libraries are independent from projects and stored in user-selected storage folders.
- The RAG Service Manager can create, edit, remove, attach, detach, ingest, rebuild, browse, reindex, delete, search, inspect extraction tools, and open system-wide Image Ingest Settings.
- The RAG library editor includes one-screen settings for name, description, storage location, chunking, max file size, embedding provider, embedding model, dimensions, vector backend, base URL, large extracted-document segmentation, enabled state, runtime controls, and save/cancel.
- Supported embedding providers are `none`, Ollama, and LM Studio.
- Ollama runtime controls include status, start, stop, install Ollama, install embed text model, test embedding, and recent log display.
- The app can start Ollama automatically for selected Ollama RAG libraries and stop the app-managed process on shutdown.
- RAG ingestion supports individual files and folders, including recursive folder ingestion.
- Ingest preview reports supported/skipped files, bytes, and reasons before ingestion starts.
- Rebuild clears database rows and re-ingests saved originals with progress updates.
- Rich document extraction supports HTML, DOCX/DOCM, XLSX/XLSM, and PDF using Poppler `pdftotext`, MuPDF `mutool`, Python `pypdf`, and a simple fallback.
- Standalone image ingestion supports PNG, JPG/JPEG, BMP, TIF/TIFF, and WebP by preserving the original image and generating extracted Markdown from OCR and optional vision-language description.
- Image ingest settings are global/system-wide, not per-project. CPU mode uses Tesseract OCR, GPU OCR mode attempts PaddleOCR with Tesseract fallback, and full vision mode attempts OCR plus an Ollama-served vision-language description.
- Large extracted rich documents can be split into overlapping Markdown segments with a manifest before chunking.
- RAG retrieval supports SQLite FTS5 keyword search, SQLite embedding BLOB cosine scan, hybrid merging, and keyword fallback.
- Passive RAG context injection adds a "Retrieved Project Knowledge" block to the system prompt when readable attached RAGs return results.
- Active built-in RAG tools are exposed to tool-capable models when a project RAG binding has Tool enabled.
- Implemented active RAG tools are `rag_list_libraries`, `rag_search`, `rag_get_document`, `rag_write_document_to_drive`, and `rag_ingest_generated_document`.
- RAG tool definitions include explicit model-facing workflow guidance so the model knows when to list libraries, search, retrieve a document, write a document to drive, or ingest generated content.
- `rag_list_libraries` returns per-library `available_tool_actions` and recommended workflow guidance, including which actions are permitted by the active project binding.
- Per-project RAG confidence defaults are applied to both passive context retrieval and active `rag_search` when the tool call does not provide explicit confidence thresholds.
- `rag_write_document_to_drive` can write either the managed original document or the extracted Markdown/text representation into the project-configured write-file folder without using filesystem MCP tools.
- `rag_write_document_to_drive` accepts either a full relative target file path or a relative target folder plus optional file name; any missing configured folder or nested target folders are created automatically by the tool.
- Generated RAG tool documents are saved under `documents/generated_sources` before being indexed, so rebuild can re-ingest them.
- Highest-priority outstanding RAG work: retrieval mode policy, per-chat RAG working set, token-budget-aware context trimming, persistent ingestion jobs, write/export audit history, project/web ingestion tools, production vector backend, metadata filters, diversity controls, reranking, scanned-PDF OCR, and GPU image-ingest hardening.

#### Diagnostics Currently Available

- Main transcript streaming preview.
- Tool trace pane for MCP and built-in RAG tool calls.
- MCP server test diagnostics with stdin/stdout/stderr and detected tools.
- MCP status and last-error display in the MCP Server Manager.
- RAG import previews and ingestion/rebuild summaries.
- RAG rebuild progress bar and current item status.
- RAG embedding runtime log tail in the RAG library editor.
- RAG extraction tool diagnostics and installer launch result.
- RAG Image Ingest Settings diagnostics, installer launch buttons, and image ingest runtime log tail.
- Context compression preview window.
- Context messages debug window with saved request context and compression snapshot details.

#### Web Server Implementation (April 15, 2026)

The embedded web server and multi-user chat interface are now in a working Phase 1 state. The full design spec is in `web_requirements.md`.

- The web server is compiled directly into `agent.exe` using **cpp-httplib** (header-only). No separate process or installer is required.
- **HTTPS/TLS** is fully implemented and gated by `#ifdef CPPHTTPLIB_OPENSSL_SUPPORT`. OpenSSL 3.x is linked statically (no DLLs required on end-user machines). Three certificate modes are supported: self-signed (auto-generated via OpenSSL EVP_PKEY_CTX RSA-2048), PEM file pair, and PFX/PKCS#12 bundle.
- **OpenSSL Windows applink fix**: `src/openssl_applink.c` is compiled into the exe to provide the OPENSSL_Applink jump table required for static OpenSSL on Windows. Without it the app crashes immediately with "no OPENSSL_Applink".
- **Static lib detection in build.bat** handles both the old `libssl_static.lib` naming (OpenSSL ≤3.3) and the new `libssl.lib` naming (OpenSSL ≥3.4) in the `lib\VC\x64\MD[d]\` subdirectory. The download script `scripts/download_openssl.ps1` fetches the Shining Light Productions full installer automatically.
- An optional **HTTP→HTTPS redirect listener** runs on a configurable secondary port.
- **Session management**: secure random session tokens, sliding expiry, in-memory session store with mutex, per-IP rate limiting on login (5 failures → 15-minute lockout), audit log.
- **Web Config dialog** (desktop): port, bind address, base URL, TLS mode, certificate file pickers, self-signed certificate generation, certificate expiry display (days remaining), theme selection, start/stop, active session count.
- **Admin Config dialog** (desktop): create/edit/delete web users with bcrypt-hashed passwords (cost ≥ 12), display name, email, enabled flag, force-password-reset flag, active session list with per-user forced logout. Groups and project bindings are defined in spec but not yet implemented in the dialog.
- **Web user store** (`web_user_store.h/.cpp`): User struct, bcrypt password verification via OpenSSL EVP, `users.json` persistence with atomic write (temp-file + rename).
- **Chat API routes** (all require session cookie auth): `GET /api/projects`, `GET /api/projects/:id/chats`, `POST /api/projects/:id/chats`, `PATCH /api/chats/:id` (rename), `DELETE /api/chats/:id`, `GET /api/chats/:id/messages`, `POST /api/chats/:id/messages` (blocking), `GET /api/chats/:id/stream` (SSE streaming), `POST /api/chats/:id/upload` (file attachment).
- **Default web assets** are embedded as C++ string literals in `src/web_assets_default.cpp` and written to `www/` on first launch. The `www/` folder is then served from disk, allowing admin customization without recompiling. Vendor libraries (highlight.js, marked.js, DOMPurify) are downloaded from CDN into `www/js/vendor/` on first start in a background thread.
- The chat streaming path uses **Server-Sent Events (SSE)** over HTTPS. WebSocket upgrade is planned for a future web phase.
- **Not yet implemented in the web server**: groups and group-to-project bindings (Admin Config dialog), `$<UserName>` / `$<UserProjectFolder>` variable injection, file attachment pipeline wired into model calls, security hardening headers (HSTS, CSP, X-Frame-Options), dark theme, sidebar collapse, thinking-block and tool-call rendering in the web UI, WebSocket streaming, per-user project folders, and admin web panel. See `web_requirements.md` for the full phase roadmap.

#### Important Current Gaps

- No central log viewer for API requests, MCP JSON-RPC traffic, RAG jobs, and app errors.
- No DPAPI encryption for API keys.
- No Streamable HTTP MCP transport.
- No MCP resources, prompts, roots, sampling, elicitation, progress/cancellation UI, or ping.
- No Model-as-Tool implementation yet.
- No Python bridge or plugin runtime yet.
- No workflow engine yet.
- No chat attachment/multi-modal prompt input pipeline in the desktop chat yet; RAG standalone image-file ingestion is implemented separately. File upload in the web interface is partially wired (upload route exists; attachment-to-model-request pipeline not yet complete).
- No theming, compact mode, or collapsible left panel in the desktop app.
- No local llama.cpp provider yet.
- No automated test suite or structured regression harness.
- Build artifacts and runtime data are currently present in the working tree during development and should be separated or ignored before release packaging.
- Concurrent chat execution is implemented (April 2026). The single global `request_in_flight_` lock has been replaced with a per-chat `chats_in_flight_` map. Each chat runs on its own worker thread. Only the active chat's input controls lock while it is in flight; the tree, all navigation, and all manager buttons remain fully interactive at all times. Background chats stream and complete independently, tree labels show a live ● indicator for in-flight chats, and a configurable concurrency limit (default 8) prevents resource exhaustion. See §2.2 for the full design.
- RAG-specific gaps are tracked in detail in `rag_service_requirements.md`, with the current highest priorities being retrieval mode policy, RAG working set, token-budget-aware RAG context, ingestion job persistence, and production vector indexing.
- Web server gaps are tracked in detail in `web_requirements.md`, with the current highest priorities being groups/project bindings, security headers, and file attachment pipeline.

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
- Thread architecture: the UI runs on the main thread; API calls, MCP communication, and tool execution each run on dedicated worker threads or a thread pool, communicating with the UI via message queues or callbacks; each active chat request runs on its own isolated worker thread so that multiple chats can be in flight simultaneously without blocking one another or the UI (see §2.2)

### 2.1 Future Python Integration Architecture

- The application will embed a Python interpreter (CPython) to enable direct in-process execution of Python code
- This will be used for: loading Python-based MCP servers without subprocess overhead, running Python-based condensation and retrieval strategies, executing Python plugins and custom tool logic, and interfacing with Python ML/AI libraries (transformers, sentence-transformers, etc.)
- The Python bridge will use pybind11 or the CPython C API to expose C++ objects to Python and call Python functions from C++
- Python execution will run on dedicated threads with the GIL managed appropriately to avoid blocking the UI
- This is a **future phase** item — the initial implementation uses subprocess-based stdio for all Python MCP servers, with the in-process bridge added later as an optimization

### 2.2 Concurrent Chat Execution

Multiple chats must be able to run simultaneously. A chat that is waiting for an API response, streaming tokens, or executing a tool-call loop must not prevent the user from interacting with any other chat, managing projects, or using any manager dialog.

#### 2.2.1 Design Intent

- The application is intended to support heavy multi-chat workloads — for example, running several long-context or tool-heavy agent sessions in parallel, or dispatching research tasks to multiple chats while monitoring them from a single window.
- When the user has sufficient API capacity and local resources, the limiting factor should be the API and the model, not the application itself.
- This is a deliberate step toward making the application act as a true multi-agent desktop host rather than a single serial chat window.

#### 2.2.2 Per-Chat Execution State

- Each chat maintains its own independent `in_flight` flag rather than a single global flag.
- A chat's `in_flight` flag is set when a request is dispatched for that chat and cleared when that request's full tool-call loop completes or fails.
- No global application-wide "busy" state exists. The application remains fully interactive while any number of chats are active.

#### 2.2.3 UI Locking Scope

- When a chat is in flight, only that chat's own input controls are locked — its text input field, its send button, its compress button, and its stop button becomes the active cancel action.
- All other UI remains fully interactive while that chat is running:
  - The project/chat tree is navigable — the user can switch to any other chat at any time.
  - Other chats can receive new user messages and start their own requests independently.
  - All manager dialogs (Providers, MCP Servers, Project Settings, RAG Service, Context Window Manager) remain openable and usable.
  - The model selector remains changeable for the currently viewed chat, as long as that chat is not itself in flight.
- A chat that is currently selected/visible and in flight shows its input area as locked and displays a stop button for that specific chat.
- A chat that is in flight but not currently visible continues to run in the background; its input area will appear locked if the user navigates to it while the request is still running.

#### 2.2.4 Per-Chat Worker Thread Isolation

- Each chat request is dispatched on its own dedicated worker thread.
- Worker threads are not shared across chats. A slow or blocked request on one chat does not delay or starve another chat's thread.
- Each worker thread owns its entire request lifecycle for that send event: context building, API streaming, tool-call loop (up to the configured round limit), tool result ingestion, and final message persistence.
- Worker threads communicate with the UI thread exclusively via `PostMessage` (or equivalent message-queue dispatch), carrying the `project_id` and `chat_id` in each message payload so the UI can route delta and completion events to the correct chat.
- Delta and completion messages from a chat that is not currently selected/visible are buffered or applied to that chat's in-memory state and persisted to disk; the transcript for that chat refreshes the next time the user navigates to it.

#### 2.2.5 Visual Status Indicators

- The project/chat tree should indicate per-chat execution state so the user can see at a glance which chats are active:
  - **Idle** — no decoration or a neutral icon.
  - **Streaming** — a pulsing or animated indicator (e.g., a spinner icon or animated dot next to the chat name).
  - **Waiting / tool-call** — a distinct indicator showing the model is executing tools.
  - **Error** — a warning icon with the last error accessible on hover or in the transcript.
- The currently visible chat shows its status in the existing status label at the bottom of the main window.
- Background chats' streaming indicators in the tree update in real time as their worker threads post progress events.

#### 2.2.6 Stopping A Running Chat

- A stop button is available for the currently visible chat when it is in flight.
- The stop action cancels the in-flight HTTP request for that chat, records an interrupted-response message in the chat history, clears that chat's `in_flight` flag, and re-enables that chat's input controls.
- Stopping one chat has no effect on any other chat currently in flight.
- A background chat that is in flight can also be stopped via a right-click context menu on its entry in the project/chat tree, or by navigating to it and using the stop button.

#### 2.2.7 Concurrency Limit

- A configurable maximum number of simultaneously in-flight chats is supported to prevent resource exhaustion on resource-constrained machines or when using rate-limited API providers.
- The default limit should be reasonable for a desktop workstation — a suggested default is 4–8 concurrent chats.
- When the limit is reached and the user attempts to send from another chat, the application displays an informative message rather than silently queuing or failing.
- The limit is configurable in global settings and can be set to unlimited if the user prefers.

#### 2.2.8 Shared Resource Thread Safety

The following shared resources are accessed by multiple concurrent chat worker threads and must be protected accordingly:

- **Chat message history files** — each chat's file path is unique, so file-level locking is sufficient; in-memory message lists must be protected with per-chat mutexes.
- **MCP server connections** — shared `McpManager` and individual `McpClient` instances must be safe for concurrent `tools/call` requests from different chat threads. If a server connection is inherently single-threaded, calls to it must be serialized through a per-connection queue rather than blocking the calling chat thread.
- **RAG service** — the `RagService` class already uses a global mutex; this remains correct for concurrent queries and ingestion from multiple chat threads.
- **Context compression state** — per-chat compression state files are unique per chat; in-memory compression state access must be guarded by per-chat mutexes.
- **Provider and model config** — read-only during a request; no additional locking required beyond the existing load-on-start model.
- **Project settings and variable resolution** — read-mostly during a request; accessed under a read lock if any write path exists.

#### 2.2.9 Implementation Approach

The refactor from the current global lock to per-chat locks involves these primary changes:

1. Replace the single `request_in_flight_` bool in `MainWindow` with a `std::unordered_map<std::string, bool>` keyed by `chat_id`, or an equivalent per-chat state structure.
2. Replace `SetRequestBusy(bool)` — which currently disables the entire app — with `SetChatBusy(chat_id, bool)`, which locks/unlocks only that chat's input controls and updates that chat's tree indicator.
3. Update all `PostMessage` dispatch paths in worker threads to include `project_id` and `chat_id` in the message payload so the UI routes updates to the correct chat's transcript rather than always updating the visible transcript.
4. Update the UI's `kChatDeltaMessage` and `kChatFinishedMessage` handlers to apply updates only if the posted `chat_id` matches the currently selected chat, and to persist/buffer updates otherwise.
5. Add per-chat status icon rendering in the project/chat tree.
6. Add the concurrency limit check before dispatching a new worker thread.

#### 2.2.10 Current Implementation Gap

The current proof-of-concept (as of April 2026) uses a single global `request_in_flight_` bool and a `SetRequestBusy` function that disables the entire application UI when any chat is active. This is the correct behavior for a single-chat proof-of-concept but must be replaced before the application is used for serious multi-agent or parallel research workflows. The refactor is straightforward in principle — the worker thread architecture is already correct — and primarily involves scoping the busy state and UI update routing to individual chats rather than the whole window.

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
- Support for **function calling / tool use** as defined by the OpenAI API (the `tools` and `tool_choice` parameters) - this is how MCP tools and built-in project RAG tools are surfaced to the model
- Request construction must include: model selection, message history, system prompt, temperature, max tokens, and the tools array
- Responses must be parsed for: assistant content, tool call requests, finish reason, and usage statistics
- HTTP communication should use a robust library (e.g., libcurl, cpp-httplib, or WinHTTP) with TLS support
- Connection errors, rate limits (HTTP 429), and API errors must be handled gracefully with retry logic and user-visible feedback
- Each chat's API request executes on its own worker thread and is completely independent of any other chat's in-flight request — see §2.2 for the full concurrency model

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
- Right-click context menus on projects and chats for: rename, delete, move, export, duplicate, and stop (when a chat is currently in flight)
- Each chat entry in the tree shows a per-chat execution status indicator: idle, streaming, waiting-for-tool, or error — updated in real time as concurrent chats run in the background (see §2.2.5)

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
- A stop button that appears during streaming to cancel the current generation for the currently visible chat (sends cancellation to that chat's in-flight API request and any active MCP tool calls for that chat); stopping this chat has no effect on any other chat currently running in the background — see §2.2.6

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
- Each project-to-RAG binding can control enabled/read/write/delete/export permissions, active tool exposure, default ingest target, retrieval priority, per-binding max chunks, and default minimum/maximum confidence thresholds.
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
  - Standalone image extraction for PNG, JPG/JPEG, BMP, TIF/TIFF, and WebP through the system-wide Image Ingest Settings pipeline.
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
  - Project binding default confidence thresholds filter passive RAG context results.
- **Active RAG tool mode**:
  - RAG can be exposed as model-callable tools rather than only passive auto-injection when the selected model supports tools and the project RAG binding has Tool enabled.
  - The model can list project-attached RAG libraries and see each library's description, permissions, retrieval priority, max chunks, default confidence window, write-file folder template, embedding settings, and intended use.
  - `rag_search` supports top-N results, candidate limits, minimum confidence thresholds, confidence ranges, maximum result counts, specific RAG IDs, and optional chunk text.
  - If a `rag_search` call omits minimum or maximum confidence, the selected RAG library's project binding default is used for that side of the confidence window.
  - RAG search results should always be ordered by confidence unless another ordering is explicitly requested.
  - If no result meets the requested threshold, the model should be able to retry with top five/top ten results, lower the threshold, search another RAG, search a lower-confidence band, or abandon the search.
  - `rag_get_document` can retrieve metadata, managed source information, and extracted document text for a search result.
  - `rag_write_document_to_drive` can copy a RAG document directly into a configured project folder as either the original file or the extracted Markdown/text conversion, with path-safety checks and automatic creation of missing nested folders.
  - `rag_ingest_generated_document` can write generated Markdown/text content into a write-enabled exposed RAG with provenance metadata.
  - The model should be able to select RAG results into a per-chat working set so selected references remain available in later context until abandoned, expired, or compressed.
  - A write-capable RAG should eventually allow project files or web-discovered documents/summaries to be ingested with provenance metadata.
  - Project Settings includes a persisted **Tool** checkbox per RAG binding; active RAG tools are wired into chat execution.
- **Not yet implemented**:
  - Automatic indexing of chat messages, condensed summaries, and chat attachments.
  - A production vector backend such as HNSWlib, FAISS, Qdrant, sqlite-vss, or ChromaDB.
  - Token-budget-aware RAG context trimming based on the selected model's `context_window`.
  - Per-chat RAG working sets, project-file/web-document RAG ingestion tools, metadata-filtered RAG search, and reranking.
  - OCR for scanned/image-only PDFs, audio transcription, production-hardening for GPU image ingestion, model reranking, and advanced metadata filters.
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

The phases are ordered by the principle of **prove it works, then build on it**. Each phase produces a usable increment that can be tested end-to-end before moving on.

**Status key: ✅ Complete · 🔄 In Progress / Partial · ⬜ Not Started**

---

### Phase 1 — GUI Shell & Model Connectivity ✅ Complete

All items in this phase are implemented and working.

- C++ project structure, `build.bat` build script with debug/release modes, MSVC toolchain auto-detection
- Native Win32 three-panel GUI: project/chat tree (left), transcript pane (center), prompt input (bottom)
- Provider & Model Manager dialog: add/edit/remove providers, name/URL/API-key/model-list fields
- `providers.json` persistence, model selector dropdown in main window
- OpenAI-compatible API layer: WinHTTP-based HTTP client, `/v1/chat/completions` request construction, SSE streaming response parsing, incremental token rendering with auto-scroll
- Error handling: connection failures, 401/429/malformed responses
- Basic chat message flow: send → stream → store
- Project and chat persistence: `data/projects/<id>/chats/<id>/messages.json`
- Left panel: project/chat tree with create, select, rename, delete
- Test connection button in Provider Manager

---

### Phase 2 — Basic MCP Tool Integration ✅ Complete

All items in this phase are implemented and working.

- MCP stdio transport: `CreateProcess` subprocess spawning, JSON-RPC 2.0 over stdin/stdout (newline-delimited), stderr capture
- MCP lifecycle: `initialize` handshake, capability negotiation, `initialized` notification, graceful shutdown
- Tool discovery: `tools/list` with `nextCursor` pagination
- Tool invocation: `tools/call` with text/image/structured result handling, up to 8 tool-call rounds
- MCP Server Manager dialog: add/edit/remove stdio servers (command, args, env, working dir, scope, variables), enable/disable, connect/disconnect, live status
- `mcp_servers.json` persistence
- MCP-to-OpenAI bridge: convert discovered tools to `tools` array, parse tool call requests from streamed response, dispatch to MCP, feed results back
- Tool trace pane in main window showing tool name, arguments, and results
- Per-server consent model: show tools on first connect, persist consent state per project
- `notifications/tools/list_changed` handling
- MCP variables (global + per-server), `$Name$` / `$<Name>$` substitution, context injection

---

### Phase 3 — MCP Full Compliance & Streamable HTTP 🔄 Partial

Stdio MCP is complete. The following items remain unimplemented:

**Streamable HTTP transport** (all of these are required together before any HTTP MCP server can be used):
1. In `src/mcp_manager.cpp`: add a new `McpHttpClient` class alongside `McpStdioClient`. It sends JSON-RPC as `POST` to the server's MCP endpoint URL with `Content-Type: application/json` and reads the response body. Use cpp-httplib for the HTTP calls (already linked).
2. In `McpHttpClient::Connect`: send the `initialize` request as a POST; read the response JSON; send the `notifications/initialized` POST.
3. For server-initiated streams: open a persistent `GET` request to the same endpoint, read the SSE stream in a background thread, and dispatch incoming JSON-RPC frames to registered notification handlers.
4. Session management: store the `MCP-Session-Id` response header after initialize; include it as a request header on all subsequent calls. If any call returns 404, treat the session as expired and reconnect. Send `DELETE` to the endpoint on shutdown.
5. Update `mcp_servers.json` schema and MCP Server Manager dialog to support HTTP servers: add a "Transport" dropdown (stdio / HTTP), show URL + auth-header fields when HTTP is selected, hide command/args/env fields.
6. Test with a real HTTP MCP server (e.g., a locally hosted MCP server or a remote service).

**Resources** (implement in `McpManager` after HTTP transport works):
7. `resources/list` with pagination → store discovered resource URIs per server.
8. `resources/read` → return resource content as text or binary.
9. `resources/subscribe` and `resources/unsubscribe` → track subscriptions; handle `notifications/resources/updated` and `notifications/resources/list_changed`.
10. Expose a resource browser panel in the MCP Server Manager or a separate Resources dialog.

**Prompts**:
11. `prompts/list` with pagination → store prompt definitions per server.
12. `prompts/get` with argument filling → return rendered prompt messages.
13. Handle `notifications/prompts/list_changed`.
14. Surface prompts in the UI: a "Prompts" button in the toolbar or right-click on a project to list and apply prompts.

**Client features provided to servers**:
15. Roots: respond to `roots/list` with the current project's root directories. Emit `notifications/roots/list_changed` when the user switches projects.
16. Sampling: handle `sampling/createMessage` requests — route through the currently active model, return the result. Gate behind the per-server consent flag (sampling consent is separate from tool consent).
17. Elicitation: handle `elicitation/create` — show the requested form/prompt to the desktop user, block until they respond, return the response to the server.

**Utilities**:
18. Progress: handle `notifications/progress` — show a progress bar in the tool trace pane (use the `progressToken` from the original tool call request to correlate).
19. Cancellation: send `notifications/cancelled` when the user presses Stop. The `requestId` must match the in-flight tool call's JSON-RPC id.
20. Logging: handle `notifications/message` from servers — route to a central log viewer (see item 21).
21. Log viewer panel: a dedicated dialog (or dockable pane) showing a filterable/searchable list of entries. Each entry has timestamp, level (debug/info/warning/error), source (server name or "API" or "App"), and message. This also covers API request logs, RAG job logs, and application errors. Log level selector filters the view. Export-to-file button.
22. Ping: respond to server `ping` requests with an empty result.

---

### Phase 4 — Model-as-Tool System ⬜ Not Started

Enable models to be wrapped as tools so the orchestrating model can delegate to specialized sub-models.

**Data layer** (do this first — no UI yet):
1. Define the `ModelTool` struct in a new `src/model_tools_manager.h`: fields for id, name, display_name, description, target_provider_id, target_model_id, system_prompt, input_schema (JSON Schema string), output_format_instructions, temperature_override (optional double), max_tokens_override (optional int), context_inclusion_policy (enum: none / summary / full), enabled, chaining_enabled, max_chain_depth, allowed_tool_names (string list), allowed_model_tool_names (string list).
2. Implement `ModelToolsManager` in `src/model_tools_manager.cpp` with `LoadFromFile`, `SaveToFile`, `GetAll`, `GetById`, `Add`, `Update`, `Remove`. File: `model_tools.json`.
3. Write unit-testable `ExecuteModelTool(tool_id, arguments_json, calling_context)` method. It constructs a new OpenAI-compatible request to the target provider/model, sends it synchronously (or with a callback), and returns the result as a `nlohmann::json` tool result.

**Tool registration**:
4. In `src/openai_client.cpp` (or wherever the unified tool list is assembled for a chat request): after adding MCP tools, call `ModelToolsManager::GetEnabledToolsForProject` and append them as additional `tools` array entries. The OpenAI tool name is the Model Tool's `name` field; the description and input_schema are taken directly from the definition.

**Tool dispatch**:
5. In the tool-call loop in the chat worker thread: when a tool call's function name matches a Model Tool name (check `ModelToolsManager::IsModelTool(name)`), call `ExecuteModelTool` instead of dispatching to MCP.
6. Log the sub-model call in the tool trace pane: show tool name, arguments, which provider/model was called, and the result.

**Chaining and permissions**:
7. When `ExecuteModelTool` is called and the Model Tool has `chaining_enabled = true`, pass an allowed-tools context to the sub-request construction so only tools in `allowed_tool_names` and `allowed_model_tool_names` are included in the sub-request's `tools` array. Max chain depth is tracked via a call-depth counter passed through `calling_context`; when depth >= `max_chain_depth`, return an error tool result.

**UI — Model Tool Editor dialog**:
8. New `src/model_tools_dialog.cpp/.h`: a dialog listing all Model Tools in a list view. Toolbar buttons: New, Edit, Duplicate, Delete, Export JSON, Import JSON.
9. Edit panel (same dialog, right side or modal): all fields from the `ModelTool` struct. Target provider/model shown as two dropdowns populated from `ProviderManager`. Input schema has a multiline edit field for raw JSON Schema with a "Validate" button.
10. Test/preview panel at the bottom of the edit form: a multiline input for sample arguments JSON, a "Test" button that calls `ExecuteModelTool` with those arguments and displays the result. The result pane shows both the raw JSON returned and any formatted text content.
11. Wire the "Model Tools" toolbar button in `src/main.cpp` to open this dialog.

---

### Phase 5 — Context Management & RAG 🔄 Substantially Complete

The proof-of-concept for this phase is working. The following items remain:

**Context compression gaps**:
1. Message pinning UI: in the transcript pane, add a right-click context menu on individual messages with a "Pin" / "Unpin" option. A pinned message gets a visual indicator (small pin icon). Pinned message IDs are stored in `project_settings.json` or `compression_state.json`. Pinned messages are excluded from condensation input even when they fall in the oldest range.
2. Tokenizer-specific token counting: the current estimate is approximate. Add an optional tiktoken-compatible token counter (this can be a simple lookup table for GPT models and a character/word heuristic for others). Wire it into the context window preflight check.
3. Compression failure hardening: if the compression model call fails (timeout, API error), the app currently may leave the context in an inconsistent state. Add explicit error recovery: log the failure, leave the message list untouched, and show a retry option in the UI.

**RAG production hardening** (priority order):
4. Retrieval mode policy: add a "retrieval mode" setting per project-RAG binding (passive-only / tool-only / both). Currently both modes are always active based on the Tool checkbox. Passive-only means the model never sees the RAG tool definitions but always gets context injected; tool-only means context injection is off and the model must search explicitly.
5. Per-chat RAG working set: after the model calls `rag_search` and selects results, those chunk IDs should be stored in a per-chat working set so they remain available in later turns without re-searching. Add `rag_add_to_working_set` and `rag_clear_working_set` tool variants, or track selected chunk IDs automatically when the model calls `rag_get_document`.
6. Token-budget-aware RAG context trimming: before injecting the "Retrieved Project Knowledge" block into the system prompt, calculate its estimated token count. If it would push the total request over `context_window * 0.8`, trim the block by removing the lowest-confidence chunks until it fits.
7. Production vector backend: replace the SQLite embedding BLOB cosine scan with HNSWlib (header-only C++ library, Apache 2.0 license). HNSWlib gives O(log n) approximate nearest-neighbor search instead of O(n) full scan. The library stores its index as a file on disk alongside the SQLite DB. The `RagService` class already has a clean abstraction boundary for the retrieval step.
8. Persistent ingestion jobs: if the app crashes or is closed during a large folder ingest, the job is lost. Add a `pending_jobs.json` that records in-progress ingestion jobs. On app start, if pending jobs exist, offer to resume or discard them.

---

### Phase 6 — Web Server & Multi-User Access 🔄 Phase 1 Complete, Phase 2 Pending

See `web_requirements.md` for the full design specification. The web server is compiled into the executable and is operational. Refer to `web_requirements.md §Phase Roadmap` for the detailed per-task breakdown.

**Phase 1 of the web feature is complete** as of April 15, 2026:
- HTTPS/TLS with self-signed cert, PEM, and PFX modes; OpenSSL statically linked
- Session management, rate limiting, audit log
- Chat API routes (projects, chats, messages, SSE streaming, file upload endpoint)
- Web Config and Admin Config dialogs (users)
- Default web assets (HTML/CSS/JS) embedded and served from `www/`

**Phase 2 of the web feature is the current priority** (see `web_requirements.md` for full task list):
- Groups and project bindings in Admin Config
- `$<UserName>` and `$<UserProjectFolder>` variable injection in web sessions
- Security hardening headers (HSTS, CSP, X-Frame-Options, X-Content-Type-Options)
- File attachment pipeline wired into model calls
- Dark theme
- Thinking block and tool-call rendering in `www/js/app.js`
- Forced-password-reset flow and change-password page
- WebSocket upgrade for streaming (currently SSE)

---

### Phase 7 — Desktop UI Polish & Multi-Modal Input ⬜ Not Started

Clean up the desktop experience and add rich attachment support.

**Multi-modal input** (do in this order):
1. Add an **Attach** button to the chat input area. On click, open a file picker (`GetOpenFileName`) filtered to supported types (images, PDF, DOCX, XLSX, text, code).
2. After selection, display attachment chips above the input field showing filename, type icon, and a remove (×) button. Chips persist until the message is sent.
3. On send, for each attachment: detect type by extension, apply the appropriate input filter:
   - Images (PNG, JPG, WebP, GIF): base64-encode; add to the `content` array as `{"type": "image_url", "image_url": {"url": "data:image/...;base64,..."}}` — only when the selected model has `supports_vision = true`.
   - PDF / DOCX / XLSX: run through the existing RAG extraction pipeline to get plain text; include as a `{"type": "text", ...}` block prepended to the user message.
   - Plain text / code files: read content and include as a fenced code block with the appropriate language tag.
4. Copy attached files to `data/projects/<id>/chats/<id>/attachments/<uuid>.<ext>` and store a reference in the message JSON.
5. Preview-before-send: after processing, show the extracted text (or image thumbnail) in a modal with "Send as-is" / "Summarize first" / "Cancel" options. The "Summarize first" path calls the active model with just the document text and a summarization prompt before appending the summary to the user message.
6. Add a `max_attachment_tokens` global setting (default: 8000 tokens estimated). Warn if an attachment would push the request over budget.

**Desktop theming**:
7. Add a `Theme` setting to global settings (`settings.json`): values `light`, `dark`, `system`.
8. Implement Win32 dark mode support: set `DWMWA_USE_IMMERSIVE_DARK_MODE` on the main window and dialogs, update `WM_CTLCOLOR*` handlers for all controls to use dark brushes when dark mode is active.
9. Detect Windows system theme via `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize\AppsUseLightTheme` registry key and watch for `WM_SETTINGCHANGE` to react to OS theme changes at runtime.
10. Add a theme toggle button to the main window toolbar.

**Compact mode**:
11. Add a compact mode toggle: keyboard shortcut `Ctrl+Shift+M` and a toolbar button. Compact mode hides the left panel entirely, collapses the toolbar to icon-only, reduces font sizes, and makes the input area single-line (expandable on click).
12. Implement "always on top" via `SetWindowPos(..., HWND_TOPMOST, ...)`. Expose as a checkbox in compact mode toolbar.
13. Auto-activate compact mode when the window width drops below a configurable threshold (default: 600px). Add this threshold to global settings.

**Collapsible left panel**:
14. Add a collapse/expand toggle button at the top of the left panel (a `◀` / `▶` arrow). Clicking it animates the panel width from its current value to 0 (or ~40px icon strip) over ~150ms using a `SetTimer` animation loop. Store the collapsed state in `settings.json`.
15. In the icon strip (collapsed state), show one project icon per project. Hovering shows a tooltip with project name. Clicking expands back to full width and selects that project.

**API key encryption**:
16. In `ProviderManager::SaveToFile`: before writing, encrypt each API key string using `CryptProtectData` (Windows DPAPI). Write the encrypted bytes as base64 in a `"api_key_encrypted"` field; omit `"api_key"`.
17. In `ProviderManager::LoadFromFile`: if `"api_key_encrypted"` is present, decrypt with `CryptUnprotectData`; fall back to plaintext `"api_key"` for backward compatibility.
18. On first load after upgrade (plaintext keys exist): automatically re-encrypt and re-save.
19. Update the Provider Manager dialog so the API key field shows `●●●●●●` when a key is stored, with a show/hide toggle eye button.

---

### Phase 8 — Python Bridge & Plugins ⬜ Not Started

Embed Python to unlock the plugin system and in-process execution.

**CPython embedding** (prerequisite for everything else in this phase):
1. Download the CPython embeddable package (e.g., `python-3.12.x-embed-amd64.zip`) into `third_party/python/`. Add a download step to `scripts/setup.ps1`.
2. In `build.bat`, when `third_party/python/python312.dll` exists, add `/DHAVE_PYTHON`, `/I"third_party/python/include"` to CFLAGS, and `third_party/python/python312.lib` to LIBS.
3. Implement `src/python_bridge.h/.cpp`: `PythonBridge` class that calls `Py_InitializeEx`, sets `sys.path` to include the bundled `Lib/` and a `plugins/` directory, and exposes `ExecScript(path, args)` and `CallFunction(module, func, args)` via the C API. All calls go through a single dedicated `std::thread` to avoid GIL contention with the UI thread.

**Plugin loader**:
4. Define the plugin manifest format: a `plugin.json` in each plugin folder under `plugins/` containing `{"name": "...", "extension_point": "context_processor|context_tool|input_filter|output_filter|data_transformer", "entry_module": "plugin_main", "entry_func": "run", "config_schema": {...}}`.
5. At startup, `PythonBridge::DiscoverPlugins` scans `plugins/` for `plugin.json` files, imports each entry module, and registers the plugin in an in-memory registry.
6. Implement each extension point dispatch:
   - **Context processor**: called with the current message list before the request is sent; returns a (possibly modified) message list. Wire into `OpenAiClient::BuildRequest`.
   - **Context tool**: exposed as a virtual MCP tool; when called by the model, dispatches to the plugin's `run` function with the tool arguments.
   - **Input filter**: called with raw file content when an attachment is processed (Phase 7); returns processed text.
   - **Output filter**: called with the assistant's final response text before it is displayed; returns (possibly modified) text.
   - **Data transformer**: used in workflow steps (Phase 9) to transform data between steps.

**Plugin Management UI**:
7. New `src/plugin_manager_dialog.cpp/.h`: a dialog listing all discovered plugins with columns for name, extension point, status (enabled/disabled), and config.
8. Toolbar: Enable/Disable toggle, Open Folder (opens the plugin's directory in Explorer), Install Dependencies button (calls `pip install -r requirements.txt` in the managed virtual environment via `PythonBridge`).
9. Per-plugin config form: generated dynamically from the plugin's `config_schema` JSON Schema. Simple text/bool/number fields rendered as Win32 controls; persisted to `plugins/<name>/config.json`.
10. Plugin output log: a `RICHEDIT` control at the bottom of the dialog showing print output and errors from plugin execution.

**In-process Python MCP servers** (optional optimization, do last):
11. Add a `"transport": "python_inprocess"` option to MCP server config. When selected, instead of spawning a subprocess, `PythonBridge` imports the server module and calls its `handle_request(json_rpc_request_str)` function directly. Response is returned as a string. This eliminates stdin/stdout overhead for Python MCP servers.

**Local embedding via Python**:
12. Implement `src/rag_embedding_python.cpp`: an embedding provider that calls a Python plugin function `embed_text(texts: list[str]) -> list[list[float]]`. This allows sentence-transformers or any Python embedding library to be used without requiring Ollama or LM Studio.

---

### Phase 9 — Agent Workflows ⬜ Not Started

Add the workflow system for automated multi-step agent processes.

**Data layer** (do first):
1. Define the `WorkflowStep` and `Workflow` structs in `src/workflow_engine.h`. Step types: `model_call`, `tool_call`, `conditional`, `loop`, `user_prompt`, `transform`. Each step has an `id`, `type`, `input_mapping` (how to pull data from previous step outputs), `output_key` (name for this step's output in the workflow context), and type-specific fields.
2. Implement `WorkflowEngine::LoadFromFile` / `SaveToFile` for `workflows.json`.
3. Implement `WorkflowEngine::Execute(workflow_id, input_args, on_step_complete_callback)` in a worker thread. The execution context is a `std::map<string, nlohmann::json>` (step outputs keyed by output_key). Steps execute sequentially; conditional steps evaluate a JSONPath or simple expression against the context. Loop steps run a sub-sequence until a condition is false or max iterations is reached.
4. Implement `WorkflowEngine::Pause` and `WorkflowEngine::Resume`: serialize the current execution context and step index to `data/workflow_state/<execution_id>.json`; reload and continue on resume.

**Workflow as a tool**:
5. When building the tool list for a chat request, add each enabled workflow as a tool. The tool name is the workflow's machine-readable id; the description is the workflow's description; the input schema is the workflow's `input_schema`.
6. When the model calls a workflow tool, dispatch to `WorkflowEngine::Execute` and return the workflow's final output as the tool result.

**Workflow Editor UI**:
7. New `src/workflow_editor_dialog.cpp/.h`: a dialog listing workflows. Toolbar: New, Edit, Delete, Duplicate, Export, Import, Run (with input form).
8. The step editor is a vertically stacked list of step cards. Each card shows the step type, a summary of its config, and drag handles for reordering. An "Add Step" button appends a new step; a step's expand button opens its full configuration in a panel below the list.
9. Test/dry-run mode: a "Run" button opens a small input form (from the workflow's `input_schema`), then executes the workflow and displays each step's input, output, and result in a trace view — similar to the tool trace pane in the main window.

---

### Phase 10 — Local Inference ⬜ Not Started

Add llama.cpp as a built-in local model provider.

1. Add `third_party/llama.cpp` as a git submodule. Build the static library as part of `build.bat` when the submodule is present (compile `llama.cpp`, `ggml.c`, `ggml-alloc.c`, `ggml-backend.cpp`, and CUDA/Vulkan backend files if GPU is detected).
2. Implement `src/llama_provider.h/.cpp`: a provider class that satisfies the same interface as `OpenAiClient` but calls llama.cpp's C API directly. Constructor takes model path, context size, GPU layers. `SendMessage` streams tokens via the `llama_decode` / `llama_sampling_sample` loop on a worker thread, posting delta events to the UI via `PostMessage`.
3. Add GPU detection at build time: if CUDA is available (`nvcc` on PATH), compile ggml-cuda.cu and set `-DGGML_CUDA=ON`. If Vulkan SDK is present, compile ggml-vulkan.cpp and set `-DGGML_VULKAN=ON`. Fall back to CPU if neither is available.
4. Add a "Local Model" provider type in the Provider Manager. When type = local, replace URL + API key fields with: model file path (file picker for `.gguf`), context size, GPU layers slider (0 = CPU only, n = offload n layers to GPU), thread count.
5. Wire `llama_provider` into the model selector so it appears identically to API-based providers in the dropdown.
6. Implement **conversation branching**: right-click any user or assistant message in the transcript → "Branch from here". Creates a new chat in the same project with all messages up to (and including) the selected message copied, then opens the new chat for editing. The branch-point message id is stored in `chat.json` metadata for traceability.

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
- **Multi-modal input**: Yes as a product direction. The chat attachment pipeline with preview-before-send is still future work; the RAG subsystem now has standalone image-file ingestion for OCR/vision-derived Markdown, while audio remains future work (§6.6, §7.4)
- **Model Tool chaining**: Yes — Model Tools can call other Model Tools with configurable depth limits (§5A.1)
- **Model Tool access permissions**: Yes — each Model Tool declares an explicit access list of what tools and capabilities it can use, enforced by the host (§5A.2)
- **Consent model**: Per-server. Consent is granted when an MCP server is first connected and applies to all tools on that server. This supports long-running autonomous agent tasks that may run for hours or days (§9)
- **RAG support**: Yes - independent reusable local RAG libraries can be attached to projects with detailed binding permissions and retrieval defaults. Current implementation includes local ingestion, rich document extraction, standalone image-file OCR/vision extraction, SQLite/FTS5 metadata, local embedding providers, rebuild, diagnostics, passive chat context injection, Project Settings binding controls, and active built-in RAG tools for list/search/document-read/write-to-drive/generated-document-ingest. A production vector backend, RAG working set, token-budget-aware RAG context, project/web ingestion tools, and automatic chat/message indexing remain future work (§7.4)
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
