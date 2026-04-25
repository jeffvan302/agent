# Runtime Layout Requirements

## Status

This layout is now implemented as the app's runtime-path model.

### Implemented

- `RuntimePaths` abstraction
- runtime separation into:
  - `.config`
  - `.data`
  - `.log`
- command-line flags:
  - `--startup`
  - `--config-dir`
  - `--data-dir`
  - `--log-dir`
- one-time legacy migration into the new layout
- storage and major subsystems updated to use the runtime split

### Still To Do

- continue cleaning up repo-local development artifacts and temporary test output
- extend automated coverage around runtime-path migration and override precedence
- finish the broader secret-storage strategy for shared/service-account deployment

## Goal

Separate source code from runtime state so the repository is no longer acting as the live data directory.

This change is intended to:

- reduce noise in the working tree
- make deployments easier to reason about
- allow one user to configure the app and another user or service account to run it
- allow config, data, and logs to live on different volumes when needed

## Core Design

Use three runtime directories:

- `.config`
- `.data`
- `.log`

By default, these should live next to the executable under a startup folder.

Default layout:

```text
<startup>\
  .config\
  .data\
  .log\
```

If no explicit startup folder is given, default to:

```text
<exe_dir>\
  .config\
  .data\
  .log\
```

Note:

- do not use the current Windows user's profile folder as the default runtime location
- do not scatter app state into `%APPDATA%`, `%LOCALAPPDATA%`, or the source repo by default

## Important Operational Note

The user explicitly wants a scenario where one user configures the app and another user runs the app or web service.

That means runtime storage must be:

- machine-visible
- permission-controlled by folder ACLs
- not tied to the configuring user's profile path

This also affects secret storage:

- user-scoped encryption would be a bad default for shared/service deployment
- if we later encrypt secrets at rest, the encryption strategy must support the actual runtime account model

Recommended future direction for secrets:

- machine-scope protection, or
- service-account-owned secrets, or
- explicit external secret provider

Do not assume `CurrentUser`-scoped secret storage if a different Windows user may run the service.

## Command Line Requirements

Add startup-path and explicit folder overrides.

### New Flags

```text
--startup <dir>
--config-dir <dir>
--data-dir <dir>
--log-dir <dir>
```

Also support `--flag=value` form:

```text
--startup=C:\AgentRuntime
--config-dir=D:\AgentConfig
--data-dir=E:\AgentData
--log-dir=F:\AgentLogs
```

### Resolution Rules

Path resolution precedence should be:

1. explicit folder flags
   - `--config-dir`
   - `--data-dir`
   - `--log-dir`
2. `--startup`
   - maps to:
     - `<startup>\.config`
     - `<startup>\.data`
     - `<startup>\.log`
3. default
   - `<exe_dir>\.config`
   - `<exe_dir>\.data`
   - `<exe_dir>\.log`

This allows:

- one shared startup root with all three folders
- or one startup root plus a separate data volume
- or fully separate config/data/log locations

### Example Usage

Single startup root:

```text
agent.exe --startup D:\AgentRuntime
```

Result:

```text
D:\AgentRuntime\.config
D:\AgentRuntime\.data
D:\AgentRuntime\.log
```

Split data onto another drive:

```text
agent.exe --startup D:\AgentRuntime --data-dir E:\AgentData
```

Result:

```text
config -> D:\AgentRuntime\.config
data   -> E:\AgentData
log    -> D:\AgentRuntime\.log
```

Fully custom:

```text
agent.exe --config-dir D:\AgentConfig --data-dir E:\AgentData --log-dir F:\AgentLogs
```

## Folder Contents

## `.config`

Contains durable configuration and admin-defined state.

Examples:

```text
.config\
  providers.json
  mcp_servers.json
  model_tools.json
  context_compression_configs.json
  web_settings.json
  users.json
  web_groups.json            (if later split out)
  project_index.json         (optional future aggregate)
  projects\
    <project_id>\
      project.json
      project_settings.json
      project_mcp.json
      mcp_consent.json
      context_compression.json
      project_rag.json
  rag\
    rag_libraries.json
    rag_image_ingest_settings.json
```

Notes:

- project definitions belong in `.config`
- project settings belong in `.config`
- web server settings belong in `.config`
- user/group/project permission bindings belong in `.config`
- RAG library definitions and image-ingest settings belong in `.config`

## `.data`

Contains runtime data, chat history, generated state, and ingested RAG content.

Examples:

```text
.data\
  projects\
    <project_id>\
      chats\
        <chat_id>\
          chat.json
          messages.json
          context_debug.json
          compression_state.json
          compression_history.json
          rag_working_set.json
  rag\
    libraries\
      <rag_id>\
        documents...
        extracted...
        hnsw...
        sqlite...
    ingestion_jobs.json
  web\
    remembered_sessions.json
    csrf_or_session_runtime_state.json   (if added later)
```

Notes:

- chat transcripts are runtime data and belong in `.data`
- per-chat compression state/history belongs in `.data`
- RAG ingested content, extracted markdown, vector indexes, and ingestion job state belong in `.data`
- remembered login/session runtime state belongs in `.data`

Important refinement:

- RAG configuration belongs in `.config`
- RAG document bodies, extracted content, indexes, and runtime job state belong in `.data`

## `.log`

Contains all logs and diagnostics.

Examples:

```text
.log\
  agent\
    agent.log
  web\
    web_audit.log
  rag\
    rag_embedding_runtime.log
    rag_image_ingest_runtime.log
  remote_worker\
    remote_ollama_worker.log   (if/when added)
```

Notes:

- no log file should live in the source root or `.data`
- logs should be grouped by subsystem where useful

## Mapping From Current Layout

Current behavior mixes config and data under a single root:

- repo root:
  - `providers.json`
  - `mcp_servers.json`
  - `web_settings.json`
  - `users.json`
  - `context_compression_configs.json`
  - `model_tools.json`
- `data\projects\...`
- `data\rag...`
- `logs\agent.log`

Target behavior:

- repo/source tree contains source only
- runtime tree contains:
  - `.config`
  - `.data`
  - `.log`

## Code Areas That Will Need Refactoring

### 1. Startup Path Resolution

Current:

- `DetermineAppRoot()` in `src/main.cpp`
- `Logger::Initialize(std::filesystem::current_path() / "logs", ...)`

Needed:

- introduce a runtime path resolver object
- compute:
  - startup root
  - config root
  - data root
  - log root
- pass those resolved paths to all subsystems

### 2. `AppStorage`

Current `AppStorage` uses a single `root_path_` and derives:

- root-level config files
- `data\projects`

Needed:

- split storage roots into:
  - `config_root_`
  - `data_root_`
- keep helper methods explicit

Suggested shape:

```cpp
class AppStorage {
public:
    AppStorage(std::filesystem::path config_root,
               std::filesystem::path data_root);
};
```

Or:

```cpp
struct RuntimePaths {
    std::filesystem::path startup_root;
    std::filesystem::path config_root;
    std::filesystem::path data_root;
    std::filesystem::path log_root;
};
```

and then:

```cpp
AppStorage(RuntimePaths paths);
```

### 3. Logger

Current:

- `src/util.cpp`
- uses a single `log_dir`

Needed:

- initialize logger from resolved `log_root`
- use subsystem folders where helpful

Minimum phase-1:

- `log_root\agent\agent.log`

### 4. Web User Store / Web Server

Current:

- `users.json` is constructed from the app root in `src/main.cpp`
- `web_audit.log` is also rooted from app root

Needed:

- user/admin config should come from `.config`
- session-like runtime state should come from `.data`
- audit logs should go to `.log\web`

### 5. RAG Service

Current:

- some RAG settings/logs are under `storage_->root_path() / "data" / ...`

Needed:

- config:
  - library definitions
  - ingest settings
  - project RAG bindings
  -> `.config`
- runtime data:
  - library content
  - extracted files
  - vector indexes
  - ingestion jobs
  -> `.data`
- logs:
  - embedding/image runtime logs
  -> `.log`

### 6. Headless / Remote Worker Commands

Current:

- `--ollama-setup`
- `--ollama-remote`

Needed:

- they should also honor `--startup`, `--config-dir`, `--data-dir`, `--log-dir`
- relative paths given to these commands should resolve against:
  - config root when appropriate, or
  - current working directory only when explicitly intended

Recommendation:

- keep explicit file arguments explicit
- but runtime-created logs/state should use the resolved runtime roots

## Migration Behavior

We need a safe migration path from the current mixed layout.

### Recommended Phase-1 Migration

On startup:

1. resolve runtime paths
2. create missing directories
3. if new config/data roots are empty but legacy files exist in the old root:
   - offer migration
   - or auto-migrate with a clear log entry

Suggested migration policy:

- prefer copy + verify + switch over move-only
- do not delete legacy files automatically on first migration
- write a migration marker file after success

Suggested marker:

```text
.config\migration_state.json
```

## Backward Compatibility

### Phase 1

- preserve old defaults if nothing is passed and no new runtime folders exist yet, only if needed for compatibility during transition
- but the intended end state should be the new runtime layout

### Better Approach

Instead of silently continuing to write to the repo root forever:

- detect legacy layout
- migrate once
- then standardize on the new layout

## Recommended Defaults

If no flags are supplied:

```text
<exe_dir>\.config
<exe_dir>\.data
<exe_dir>\.log
```

This is the simplest deployment story and matches the user's stated goal.

## Recommended First Implementation Order

1. Add `RuntimePaths` resolution and command-line parsing
2. Switch logger to `.log`
3. Refactor `AppStorage` to split config/data roots
4. Move web config/user config to `.config`
5. Move chat state to `.data`
6. Move RAG settings vs RAG runtime data to the correct roots
7. Add migration path from legacy layout

## Bottom Line

The proposed layout is good.

The one change I strongly recommend is:

- keep configuration in `.config`
- keep runtime/chat/RAG content in `.data`
- keep diagnostics in `.log`

That keeps deployment simple without blurring config and data together again.
