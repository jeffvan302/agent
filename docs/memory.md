# Memory System

## Overview

The agent maintains two complementary memory systems that share a single storage root:

1. **Artifact / Code Memory** — an MCP-style toolset the model can call to read, write, and search durable project artifacts.
2. **Context Window Compaction Layer 0** — a deterministic extraction layer that runs during context-window compression and records artifacts whether the model remembers to or not.

Together they ensure that:
- prior generated code / HTML / SVG / configuration / diagrams survive context-window pressure,
- every version remains auditable,
- code and library references can be indexed incrementally,
- rollback to a prior state is tool-backed rather than reconstructed from compressed summaries.

For implementation details, tool schemas, dispatch flow, and restore examples, see `docs/artifact_code_memory_tool.md`.

---

## Common Storage Layout

Both systems write under a single chat-scoped directory:
```
$ProjectFolder$\.agent\.memory\$CHATID$
```
This path resolves automatically from project variables (`ProjectFolder`, `CHATID`) and is created lazily on first write.

Default sub-directories:
- `artifacts/` — Artifact Memory Markdown records (diagrams, HTML, config, JSON objects, generated code).
- `code/` — Code/Reference Memory Markdown records (project code, libraries, SDK usage, call patterns).
- `INDEX.md` and `index.json` — human- and machine-readable index of stored entries.

---

## 1. Artifact / Code Memory as a Toolset

When enabled (Project Settings → Built-in Tools → Artifact/Code Memory, or forced when Layer 0 is active), the model is exposed to the following tools:

### Artifact Memory Tools
| Tool | Purpose |
|------|---------|
| `artifact_memory_get_index` | Discover what artifacts exist, filter by type or key, optionally latest only. |
| `artifact_memory_get_artifact` | Read a full historical Markdown record by `artifact_id`. |
| `artifact_memory_get_latest` | Read the latest version for a given `artifact_key`. |
| `artifact_memory_list_versions` | List all versions of an `artifact_key` with timestamps / hashes. |
| `artifact_memory_get_version` | Read an exact version by `artifact_key` + `version` number. |
| `artifact_memory_restore_version` | Return exact prior content to use as a restore source. |

### Code Reference Memory Tools
| Tool | Purpose |
|------|---------|
| `code_memory_store_fragment` | Append a versioned Markdown record for a function, class, API, or module. |
| `code_memory_search` | Find indexed code by symbol, behavior, language, file path, tags, or change notes. |
| `code_memory_list_versions` | Show history for a code `artifact_key`. |
| `code_memory_get_version` | Read an exact versioned record including exact content. |
| `code_memory_restore_version` | Return exact prior code as `restored_content`. |

### Tool Rules Shown in Prompt
When memory tools are exposed, the model receives usage instructions such as:
- Inspect artifact memory first for restore/revert/go-back requests.
- Use `code_memory_search` to locate code without keeping full source in context.
- Verify line-number hints against `content_hash`, `qualified_symbol`, and current source.
- State whether a restore came from **Artifact Memory**, **Code Memory**, **compressed context**, or **manual reconstruction**.

---

## 2. Layer 0: Automatic Context Window Compaction

Layer 0 is a deterministic extraction pass that runs **automatically** during hierarchical context compression. It does not depend on a model remembering to save something.

### What it captures
- Fenced code blocks with language tags (e.g. `cpp`, `python`, `javascript`, `java`, `html`, `mermaid`, `svg`, `json`, `vega-lite`, `ts`).
- Raw inline SVG or HTML fragments (e.g. `<svg>...</svg>`).
- Tool-call outputs that contain code or structured data.

### What it stores
Each captured item is written as a Markdown file containing:
- `artifact_key` — stable family identifier (derived from filename, symbol, or inferred type).
- `version` — monotonically increasing integer per key.
- `content` — exact fenced block / raw markup / JSON object preserved verbatim.
- `content_hash` — SHA-256 for deduplication and verification.
- `type` — language or content category (`cpp`, `html`, `svg`, `json`, `mermaid`, …).
- `summary` — one-line description (usually derived from preceding assistant text or nearest heading).
- `source_message_index` — original chat position so compressed summaries can point back.

### Index maintenance
After every pass, `index.json` and `INDEX.md` are rewritten so both humans and tools can browse the inventory without scanning the folder.

---

## When Memory Tools Are Available

Two conditions can expose the tools for a given project:
1. **Explicit toggle** — Project Settings → Built-in Tools → Artifact/Code Memory is checked.
2. **L0 force** — the selected Context Window Compression config uses Hierarchical Structured strategy with Layer 0 enabled.

If Layer 0 is selected, the tool is automatically enabled and the explicit checkbox is shown as forced-on in Project Settings.

---

## Use Cases for Artifact/Code Memory

### Graphic Design Items as JSON Objects
When a model generates a design token, icon specification, color palette, or component schema, it can be stored as an Artifact Memory record by Layer 0 automatic capture:

```json
{
  "type": "icon",
  "id": "settings-cog",
  "path": "M12 15.5A3.5 3.5 ...",
  "viewBox": "0 0 24 24",
  "strokeWidth": 2,
  "category": "navigation"
}
```

This preserves versioned creative decisions so later turns can retrieve or revert to earlier palettes without regenerating.

### Code Snippets and Library References
During analysis, a model may index a useful function or class:

```cpp
// stored under code/cpp/Agent::BuildResolvedVariablesForChat
std::vector<ProjectMcpVariableValue> MainWindow::BuildResolvedVariablesForChat(
    const std::string& project_id,
    const std::string& chat_id,
    const ProjectSettings& project_settings) {
    std::vector<ProjectMcpVariableValue> resolved;
    ...
    return resolved;
}
```

Metadata kept alongside it:
- `file_path` — source file in the project.
- `qualified_symbol` — `MainWindow::BuildResolvedVariablesForChat`.
- `language` — `cpp`.
- `change_summary` / `change_reason` — why this version exists.
- `content_hash` — deterministic fingerprint for later verification.

Later, if the user asks “How do we build resolved variables now?”, `code_memory_search` can locate the snippet even if the full source is no longer in context.

### Configuration Artifacts
Project-specific YAML, JSON, or INI generated during setup can be stored as artifact records so that later turns can inspect the exact prior configuration rather than reconstructing it from prose.

### Restoring a Prior Version
If a user says “go back to the dial before the 100 label was removed”, the model:
1. Calls `artifact_memory_get_index` (or `list_versions`) to discover entries matching `rpm_gauge`.
2. Identifies the version just before the change (via summaries / timestamps).
3. Calls `artifact_memory_restore_version` to get exact prior SVG/HTML content.
4. Writes that exact content back to the filesystem and cites the restored source.

---

## Notes on Reliability

- **Line numbers are hints only.** Code Memory stores `start_line_hint` and `end_line_hint`, but edits shift lines. Always verify by `content_hash`, `qualified_symbol`, `file_path`, and current source before making edits.
- **Append-only versioning.** Same content will not create a duplicate version unless `force_new_version` is true. Prior latest versions are retained and marked `superseded`.
- **Storage root is resolved per-chat.** `CHATID` is part of the path, so each chat has its own memory scope, but tools can search across a project if the resolved folder is shared.
- **Default path if no L0 config.** When explicitly enabled without Layer 0, the fallback root is `$ProjectFolder$\.agent\.memory\$CHATID$`.

---

## Summary Table

| Capability | Artifact Memory | Code Memory | Layer 0 Capture |
|------------|-----------------|-------------|-----------------|
| Stores exact content | yes | yes | yes |
| Versioned append-only | yes | yes | auto |
| Searchable by symbol / behavior | basic | rich | index-based |
| Restore / revert tooling | yes | yes | via re-read |
| Automatic capture during compression | no | no | yes |
| Does not overwrite prior versions | yes | yes | yes |
| Content-hash verification | yes | yes | yes |
| Requires model to call tool | yes | yes | no — deterministic |

---

## Quickstart Checklist

1. Open Project Settings → Context Window Compression.
2. Choose (or create) a Hierarchical Structured config with **Layer 0 enabled**, or check **Artifact/Code Memory** in Built-in Tools.
3. Chat as normal. Watch `\your_project\.agent\.memory\chat_…` for automatically-indexed artifacts.
4. To force a code reference, ask the model to store a fragment with `code_memory_store_fragment`.
5. To restore, ask the model to use `artifact_memory_restore_version` or `code_memory_restore_version`.
