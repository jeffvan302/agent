# Artifact / Code Memory Tool

This document explains the Artifact / Code Memory built-in tool, how it is used, and how it is implemented in this project.

For a higher-level overview of the memory system and Layer 0 context compaction, see `docs/memory.md`.

## Purpose

Artifact / Code Memory gives the agent durable, versioned storage for important generated or inspected material. It is designed to preserve exact content across long interactions, context-window compression, and later restore requests.

The memory tool is useful for:

- Preserving generated artifacts such as HTML, SVG, Mermaid, Vega-Lite, JSON, configs, and code blocks.
- Keeping version history for artifacts so prior versions can be restored exactly.
- Storing code/reference fragments with metadata such as file path, symbol, language, summary, change reason, and content hash.
- Searching known code fragments later without keeping the full source in the model context.
- Supporting auditable restore/revert workflows.

## Storage Location

The default storage root is:

```text
$ProjectFolder$\.agent\.memory\$CHATID$
```

This is resolved with project/chat variables before tools are exposed. A typical resolved layout is:

```text
<project>\.agent\.memory\<chat_id>\
  INDEX.md
  index.json
  artifacts\
  code\
```

Important implementation detail: `BuildArtifactMemoryToolSet(...)` only enables the toolset if the resolved storage root is absolute. In normal project use, `$ProjectFolder$` should resolve to an absolute path.

## Availability

Artifact / Code Memory is available when either condition is true:

- Project Settings -> Built-in Tools -> Artifact/Code Memory is enabled.
- The selected Context Window Compression config uses Hierarchical Structured compression with Layer 0 enabled.

Layer 0 forces memory on because it needs the same storage root for automatic artifact capture.

Implementation functions:

- `SelectedConfigHasLayer0(...)`: checks whether the selected compression config has Layer 0 enabled.
- `ShouldExposeArtifactMemoryTools(...)`: returns true when either Layer 0 is enabled or the explicit project setting is enabled.
- `BuildArtifactMemoryToolSet(...)`: resolves the storage folder, initializes runtime metadata, creates storage directories, and returns the tool definitions.

## Runtime Structure

The memory bridge is implemented in:

```text
src/artifact_memory_tool_bridge.h
```

The runtime state is represented by `ArtifactMemoryRuntime`:

```cpp
struct ArtifactMemoryRuntime {
    bool enabled = false;
    std::string project_id;
    std::string chat_id;
    std::string config_id;
    std::string config_name;
    std::string server_name = "Artifact Memory";
    std::filesystem::path storage_root;
    std::string storage_root_utf8;
    int max_injected_rows = 12;
};
```

The toolset returned to the model is represented by `ArtifactMemoryToolSet`:

```cpp
struct ArtifactMemoryToolSet {
    ArtifactMemoryRuntime runtime;
    std::vector<ChatToolDefinition> definitions;
};
```

## Exposed Tools

### Artifact Memory Tools

These tools are mostly read/restore oriented. Stored artifacts are typically produced by Layer 0 capture or existing memory files.

| Tool | Purpose |
| --- | --- |
| `artifact_memory_get_index` | Reads the memory index. Supports filtering by `artifact_key`, `type`, and `latest_only`. |
| `artifact_memory_get_artifact` | Reads a full Markdown record by `artifact_id`. |
| `artifact_memory_get_latest` | Reads the latest stored version for an `artifact_key`. |
| `artifact_memory_list_versions` | Lists every known version for an `artifact_key`. |
| `artifact_memory_get_version` | Reads a specific `artifact_key` and `version`. |
| `artifact_memory_restore_version` | Returns `restored_content` for an exact prior artifact version. |

### Code Reference Memory Tools

These tools support explicit model-managed code/reference storage.

| Tool | Purpose |
| --- | --- |
| `code_memory_store_fragment` | Stores a versioned code/reference Markdown record. |
| `code_memory_search` | Searches code/reference memory by symbol, behavior, file path, language, tags, summary, and other metadata. |
| `code_memory_list_versions` | Lists all versions for a code-memory `artifact_key`. |
| `code_memory_get_version` | Reads an exact code-memory version. |
| `code_memory_restore_version` | Returns exact prior code as `restored_content`. |

## Tool Dispatch

Tool calls are handled through:

```cpp
CallArtifactMemoryTool(runtime, tool_name, arguments_json)
```

This function:

- Verifies the runtime is enabled.
- Verifies `tool_name` is a known memory tool using `IsArtifactMemoryToolName(...)`.
- Parses JSON arguments.
- Loads `index.json` through `LoadIndexJson(...)`.
- Dispatches to the relevant operation.
- Returns a JSON `McpToolCallResult`.

The desktop path dispatches artifact memory calls in `src/main.cpp`.

The web path dispatches artifact memory calls in `src/web_server.cpp`.

## Index Files

Memory uses two index representations:

- `index.json`: machine-readable index used by tools.
- `INDEX.md`: human-readable index for inspection.

Important functions:

- `IndexPath(runtime)`: returns the path to `index.json`.
- `LoadIndexJson(runtime, ...)`: loads or initializes the JSON index.
- `SaveIndexJson(runtime, index, ...)`: writes `index.json` and `INDEX.md`.
- `BuildBridgeIndexMarkdown(index)`: converts the JSON index to a Markdown summary table.

The index records include metadata such as:

- `artifact_id`
- `artifact_key`
- `version`
- `latest`
- `status`
- `kind`
- `type`
- `language`
- `file_path`
- `memory_file_path`
- `content_hash`
- `summary`
- `change_summary`
- `change_reason`
- timestamps

## Versioning Model

Memory is append-only for stored code/reference fragments.

When a new code fragment is stored:

1. `BuildArtifactKeyFromCodeRecord(...)` creates or uses a stable `artifact_key`.
2. `Fnv1aHashHex(...)` computes a deterministic content hash.
3. Existing versions for the same key are found with `CollectArtifactsForKeyMutable(...)`.
4. If the latest version has the same `content_hash` and `force_new_version` is false, no new version is written.
5. Otherwise, the old latest version is marked `superseded` and a new latest version is created.

This makes restore and audit workflows safer because prior versions are retained rather than overwritten.

## Code Memory Storage Flow

The explicit code storage path is implemented by:

```cpp
StoreCodeMemoryFragment(runtime, index, args)
```

Required fields for `code_memory_store_fragment`:

- `reference_id`
- `reference_type`
- `file_path`
- `language`
- `symbol`
- `summary`
- `change_summary`
- `change_reason`
- `content`

Useful optional fields:

- `kind`
- `reference_name`
- `reference_version`
- `source_uri`
- `symbol_kind`
- `qualified_symbol`
- `start_line_hint`
- `end_line_hint`
- `locator_strategy`
- `context_hash`
- `behavior_delta`
- `tags`
- `artifact_key`
- `force_new_version`

Example call:

```json
{
  "reference_id": "agent",
  "reference_name": "Agent Desktop App",
  "reference_type": "project",
  "file_path": "src/project_settings_dialog.cpp",
  "language": "cpp",
  "symbol_kind": "method",
  "symbol": "RefreshInternalToolsList",
  "qualified_symbol": "ProjectSettingsDialog::RefreshInternalToolsList",
  "summary": "Rebuilds the Built-in Tools checklist and shows the selected tool settings panel.",
  "change_summary": "Captured current checklist behavior after adding planner and questionnaire tools.",
  "change_reason": "Needed for future debugging of built-in tool selection and persistence.",
  "tags": ["project_settings", "built_in_tools", "ui"],
  "content": "void RefreshInternalToolsList(bool save_current = true) { ... }"
}
```

The stored Markdown file is produced by `BuildCodeFragmentMarkdown(...)` and written below:

```text
code\<reference_id>\<artifact_key>_vNNN_<hash>.md
```

## Artifact Retrieval Flow

Artifact read/restore tools use the index to resolve the requested record.

Important functions:

- `FilterArtifacts(...)`: filters index entries by key/type/latest status.
- `FindArtifactById(...)`: resolves a specific `artifact_id`.
- `FindLatestArtifact(...)`: finds the latest version of an artifact key.
- `FindArtifactVersion(...)`: finds an exact version.
- `ResolveArtifactPath(...)`: resolves the Markdown file path safely under `storage_root`.
- `ReadArtifactRecordResult(...)`: reads the Markdown record and extracts fenced artifact content if present.
- `ExtractArtifactContentFromMarkdown(...)`: extracts exact content from the Markdown artifact section.

Restore tools are read-only. They return `restored_content`, but another filesystem or MCP tool must write that content back to a project file if the user requested an actual restore.

## Restore Workflow

When the user asks to revert, restore, go back, or recover a previous generated artifact:

1. Call `artifact_memory_get_index` to discover matching artifact keys.
2. Call `artifact_memory_list_versions` to inspect available versions.
3. Call `artifact_memory_get_version` if comparison is needed.
4. Call `artifact_memory_restore_version` to select the exact restore source.
5. Write `restored_content` back to the correct file using the appropriate editing/filesystem tool.
6. In the final response, say the restore came from Artifact Memory and identify the key/version used.

Example restore call:

```json
{
  "artifact_key": "login_page_html",
  "version": 3,
  "restore_reason": "User asked to return to the layout before the button styling change"
}
```

## Search Workflow For Code Memory

Use `code_memory_search` when you need to find a function, class, API pattern, or stored code reference.

Example:

```json
{
  "query": "built-in tools checklist",
  "language": "cpp",
  "latest_only": true,
  "max_results": 10
}
```

Search implementation details:

- `BuildCodeSearchHaystack(...)` builds searchable text from metadata fields.
- `CodeArtifactMatchesSearch(...)` applies exact filters and substring filters.
- `CompactCodeArtifact(...)` returns compact metadata without loading the full Markdown content.

Line hints are intentionally treated as hints only. The model must verify current source by `file_path`, `qualified_symbol`, `content_hash`, and the current file contents before editing.

## Safety And Path Handling

The bridge includes path safety checks:

- `PathIsAtOrInside(candidate, root)` ensures resolved artifact paths do not escape the memory root.
- `ResolveArtifactPath(...)` rejects index entries that point outside `runtime.storage_root`.
- `WriteWholeFileUtf8(...)` creates parent directories and writes UTF-8 files.

This is important because memory records are read from paths stored in `index.json`; those paths must not be allowed to escape the configured memory folder.

## Prompt Injection / Model Instructions

When memory tools are exposed, the system prompt includes `BuildArtifactMemoryUsageContext()`.

That context instructs the model to:

- Inspect memory first for restore/revert/go-back requests.
- Use artifact memory tools to discover keys and versions.
- Use code memory to store important functions, classes, APIs, and call patterns.
- Treat line numbers as hints only.
- State whether a restore came from Artifact Memory, compressed context, or manual reconstruction.

This prompt context is injected in both desktop and web flows when `ShouldExposeArtifactMemoryTools(...)` returns true.

## Desktop And Web Integration

Desktop integration is in `src/main.cpp`:

- Adds memory usage context to the system prompt.
- Builds the artifact memory toolset with `BuildArtifactMemoryToolSet(...)`.
- Adds memory tool definitions to the outgoing tool list.
- Dispatches memory tool calls through `CallArtifactMemoryTool(...)`.
- Shows tool trace titles with `TraceTitleForArtifactMemoryTool(...)`.

Web integration is in `src/web_server.cpp`:

- Adds memory usage context while building the web system prompt.
- Builds memory tools for both non-streaming and streaming paths.
- Dispatches memory tool calls through `CallArtifactMemoryTool(...)`.

Project Settings integration is in:

- `src/types.h`: `ProjectSettings::built_in_artifact_memory_enabled`.
- `src/storage.cpp`: JSON serialization/deserialization.
- `src/project_settings_dialog.h`: dialog option/result fields.
- `src/project_settings_dialog.cpp`: Built-in Tools UI checkbox and forced-on Layer 0 behavior.
- `src/main.cpp`: loads/saves the project setting.

## Relation To Layer 0 Compression

Layer 0 is the automatic capture side of the memory system. When a hierarchical compression config has Layer 0 enabled, artifact memory is forced on because Layer 0 writes captured artifacts into the memory root.

The explicit Artifact/Code Memory built-in setting is different:

- It exposes memory tools even when Layer 0 compression is not selected.
- It uses the default storage folder template if no Layer 0 config supplies one.
- It lets the model manually search, store code references, and restore versions.

In short:

- Layer 0 captures important artifacts automatically during compression.
- Artifact/Code Memory tools let the model inspect, search, store code references, and restore exact versions.

## Practical Usage Patterns

### Store A Useful Function

Use `code_memory_store_fragment` after analyzing a meaningful function or class so future turns can retrieve it by symbol or behavior.

### Find A Known Implementation

Use `code_memory_search` before reading large parts of the repository when the memory index may already know where the relevant code lives.

### Restore Prior Generated Output

Use artifact version tools when the user asks to revert generated HTML, SVG, diagrams, JSON specs, or configuration.

### Track External API Knowledge

Store SDK call patterns or API references as `api_reference`, `call_pattern`, or `dependency_note` records when the project repeatedly depends on them.

## Limitations

- Artifact Memory read tools depend on records already captured by Layer 0 or already present in the index.
- The current explicit write tool is for Code Reference Memory: `code_memory_store_fragment`.
- Restores are read-only until another tool writes `restored_content` back to a project file.
- Line hints can become stale after edits.
- The memory root is chat-scoped by default because `$CHATID$` is part of the default path.

## Summary

Artifact / Code Memory provides durable, versioned project memory for generated artifacts and code/reference knowledge. It combines automatic capture through Layer 0 with explicit model tools for search, storage, version inspection, and restore workflows. The implementation is centered on `src/artifact_memory_tool_bridge.h`, with project settings and dispatch wiring in the desktop and web request paths.
