# L0 Artifact Memory Requirements

## Status

Initial implementation is now in place.

### Implemented

- Layer 0 configuration and persistence inside hierarchical structured compression
- Context Window Editor UI for:
  - enable/disable
  - capture model
  - selection model
  - capture prompt
  - selection prompt
  - storage folder template
  - max injected rows
- compression-time artifact extraction and per-chat storage
- `index.json`, `INDEX.md`, and artifact-file persistence under the configured storage root
- compact Layer 0 index reinjection into the compressed context
- built-in read-only Artifact Memory tool surface gated to hierarchical compression with L0 enabled

### Still To Do

- deepen regression coverage for capture/selection behavior
- improve revisioning/deduplication polish and operator diagnostics
- consider richer UI for browsing stored artifacts and revision history

## Purpose

Layer 0 (L0) Artifact Memory is a proposed extension to Hierarchical Structured Compression (HSC).
Its job is to preserve stable code and diagram artifacts across long conversations without forcing the
full artifact content back into every future context window.

L0 is intended for cases where the conversation repeatedly creates or revises:

- source code
- HTML files
- SVG content
- Mermaid diagrams
- Vega-Lite specs
- Cytoscape.js graph definitions
- configuration blocks
- other fenced code artifacts that benefit from version-aware recall

The core idea is:

1. Extract durable artifacts from the conversation during compression.
2. Store them as Markdown files in a per-chat memory folder.
3. Maintain a machine-readable index that tracks versions and latest artifacts.
4. Inject only a compact selected index view back into the compressed context.
5. Expose a built-in virtual MCP-style tool surface so the chat model can retrieve full artifacts on demand.

## Goals

- Preserve code and diagram stability across compression cycles.
- Prevent large code blocks from being repeatedly paraphrased by L2/L3 summaries.
- Keep memory per chat, not mixed across unrelated chats.
- Allow later lookup of full artifacts without injecting all artifact content into the prompt.
- Reuse the app's existing project-variable and virtual-tool architecture.

## Non-Goals

- L0 is not a replacement for L2 summaries or L3 structured state.
- L0 does not replace RAG libraries.
- L0 does not expose write/update tools to the chat model during normal chat use.
- L0 does not automatically share artifacts across chats or projects.
- L0 does not store arbitrary binary files; it stores Markdown artifact records and indexes.

## Position In HSC

Current HSC layers:

- L1: pinned verbatim messages
- L2: running summary
- L3: structured state
- L4: recency window

Proposed HSC layers after this feature:

- L0: artifact memory index
- L1: pinned verbatim messages
- L2: running summary
- L3: structured state
- L4: recency window

L0 should be optional and only available for the `hierarchical_structured` strategy.

## Design Principles

### 1. App-owned storage, model-owned meaning

The model may decide:

- what artifacts exist
- what an artifact is about
- whether an artifact supersedes a prior artifact
- which artifacts are most relevant to inject

The app must decide:

- folder creation
- file naming
- version numbering
- index persistence
- deduplication by key and content hash
- safe path handling

The model should return structured JSON outputs. The app should write files and update indexes.

### 2. Per-chat isolation

Artifact memory must be isolated by chat. Different chats must not share the same index by default.

### 3. Read-only tool surface during chat use

The normal chat model should be able to read the artifact memory but should not directly mutate it.
Artifact creation and index updates happen only during the compression pipeline.

### 4. Compact reinjection

The context window should never inject all artifact files. It should inject only a compact selected
index block.

## Storage Location

L0 must support a configurable folder template using project/chat variables.

Recommended default:

```text
$ProjectFolder$\.agent\.memory\$CHATID$
```

Rationale:

- `$ProjectFolder$` already participates in project/chat variable expansion.
- `$CHATID$` guarantees per-chat separation even when multiple chats share the same project folder.
- `.agent\.memory` keeps the storage internal and predictable.

The resolved storage path must:

- expand variables using the same runtime/project variable pipeline as project tools
- resolve to an absolute path
- be created automatically if it does not exist
- be constrained to a normal directory path, not a file path

## Folder Layout

Recommended on-disk structure:

```text
<resolved_l0_folder>\
  index.json
  INDEX.md
  artifacts\
    artifact_<key>_v001.md
    artifact_<key>_v002.md
    artifact_<key>_v003.md
```

## Artifact Types

The extraction pass should recognize at least these artifact types:

- `code`
- `html`
- `css`
- `javascript`
- `typescript`
- `python`
- `cpp`
- `json`
- `yaml`
- `sql`
- `svg`
- `mermaid`
- `vega-lite`
- `cytoscape`
- `markdown`
- `config`

Type may be derived from fenced language tags, content heuristics, or both.

## Artifact Identity Model

Each artifact must have:

- `artifact_id`: immutable unique ID
- `artifact_key`: stable logical key for the evolving artifact
- `version`: monotonically increasing integer
- `content_hash`: hash of normalized artifact content
- `status`: `active`, `superseded`, or `partial`

`artifact_key` is the logical identity used to group revisions of the same artifact.

Examples:

- `login_form_html`
- `sailing_right_of_way_mermaid`
- `formula_cost_chart_vega_lite`

If the same logical artifact changes, the system creates a new version under the same `artifact_key`.

## Artifact File Format

Each artifact file must be Markdown with YAML front matter.

Required structure:

```md
---
artifact_id: artifact_1777000000000_1
artifact_key: sailing_right_of_way_mermaid
version: 2
type: mermaid
language: mermaid
status: active
supersedes: artifact_1776999999000_1
project_id: project_...
chat_id: chat_...
source_turn_start: 42
source_turn_end: 48
summary: Decision flow for right-of-way rules between sailing boats.
user_intent: Build a durable visual explanation of the rule logic.
problem_it_solves: Preserves the latest flowchart definition across compression cycles.
tags:
  - diagram
  - sailing
  - rules
content_hash: ...
created_at: ...
updated_at: ...
---

# Summary

Decision flow for right-of-way rules between sailing boats.

## User Intent

Build a durable visual explanation of the rule logic.

## Problem Solved

Preserves the latest flowchart definition across compression cycles.

## Artifact

```mermaid
flowchart TD
...
```
```

## Index Files

### index.json

`index.json` is the canonical machine-readable index.

Proposed top-level structure:

```json
{
  "schema_version": 1,
  "project_id": "project_...",
  "chat_id": "chat_...",
  "updated_at": "2026-04-21T12:00:00Z",
  "artifacts": [
    {
      "artifact_id": "artifact_...",
      "artifact_key": "sailing_right_of_way_mermaid",
      "version": 2,
      "latest": true,
      "status": "active",
      "type": "mermaid",
      "language": "mermaid",
      "summary": "Decision flow for right-of-way rules.",
      "user_intent": "Create a stable sailing rules flowchart.",
      "problem_it_solves": "Preserves the current diagram across compression cycles.",
      "tags": ["diagram", "sailing"],
      "content_hash": "...",
      "file_path": "artifacts/artifact_sailing_right_of_way_mermaid_v002.md",
      "supersedes": "artifact_...",
      "created_at": "...",
      "updated_at": "...",
      "last_seen_at": "..."
    }
  ]
}
```

### INDEX.md

`INDEX.md` is a human-readable companion view generated from `index.json`.

It should include:

- artifact key
- latest version
- type
- short summary
- status
- file path

## Compression Pipeline

### Step 1: Determine input scope

L0 capture should run on new turns since the last successful compression, not the entire conversation,
except when rebuilding from scratch.

### Step 2: L0 capture model call

Input:

- new turns since last compression
- compact current index summary
- current project/chat identifiers
- capture prompt template

Output:

- structured JSON describing extracted artifacts
- summaries and intent statements
- artifact key suggestions
- supersedes relationships
- normalized code-block payloads

The model should not write files directly.

### Step 3: App persistence

The app must:

- normalize artifact keys
- dedupe by content hash
- assign version numbers
- write artifact Markdown files
- update `index.json`
- regenerate `INDEX.md`

### Step 4: L2 and L3 compression

Existing HSC L2 and L3 behavior remains in place.

### Step 5: L0 selection model call

Input:

- updated `index.json`
- current L3 state
- recent turns
- selection prompt template

Output:

- selected artifact IDs
- short relevance notes
- an ordered compact index subset for reinjection

### Step 6: Reinjection

The compressed context should include a compact L0 block before or near the L3/L2 sections.

Recommended order:

1. L0 artifact memory
2. L3 conversation state
3. L2 conversation summary
4. L1 pinned verbatim messages
5. L4 recent conversation

## Initial Chat Open Behavior

If a chat opens and:

- L0 is enabled
- the configured L0 folder exists
- `index.json` exists

then the app should be able to load the index even before the first new compression cycle.

If compressed state already exists for the chat, use the last selected L0 block from compression state.
If compressed state does not exist yet, generate a compact initial L0 block from the stored index.

## Chat Tool Surface

L0 should be exposed to the chat model as a built-in virtual MCP-style server, similar to the app's
virtual RAG tool server pattern.

Recommended server name:

```text
Artifact Memory
```

Its description should explain:

- this memory is per chat
- it stores extracted code/diagram artifacts from prior compression cycles
- the compact index may already be present in context
- full artifact content can be retrieved on demand

### Required chat-facing tools

#### 1. `artifact_memory_get_index`

Returns the full machine-readable index or a filtered subset.

Suggested arguments:

- `artifact_key` optional
- `type` optional
- `latest_only` optional, default true

#### 2. `artifact_memory_get_artifact`

Returns the full stored Markdown file for a specific `artifact_id`.

Suggested arguments:

- `artifact_id` required

#### 3. `artifact_memory_get_latest`

Returns the latest artifact file for a logical key.

Suggested arguments:

- `artifact_key` required

#### 4. `artifact_memory_list_versions`

Returns version history for one logical artifact.

Suggested arguments:

- `artifact_key` required

### Explicit constraint

No chat-facing mutation tool should be exposed in phase 1 of L0. Compression owns writes. Chat usage owns reads.

## Configuration Requirements

L0 must be configurable only inside the `hierarchical_structured` compression strategy.

### Proposed data model

```json
{
  "layers": {
    "layer0": {
      "enabled": true,
      "capture_model_id": "model-id",
      "capture_model_provider_id": "provider-id",
      "capture_prompt_template": "...",
      "selection_model_id": "model-id",
      "selection_model_provider_id": "provider-id",
      "selection_prompt_template": "...",
      "storage_folder_template": "$ProjectFolder$\\.agent\\.memory\\$CHATID$",
      "max_injected_rows": 12
    }
  }
}
```

### Required UI fields

- `Enable L0 Artifact Memory`
- `Capture Model`
- `Capture Prompt`
- `Selection Model`
- `Selection Prompt`
- `Storage Folder`

### Recommended UI fields

- `Max Injected Rows`
- `Default` button for Capture Prompt
- `Default` button for Selection Prompt

These controls should only appear when the selected compression strategy is `hierarchical_structured`.

## Compression State Additions

`ChatCompressionState` should be extended with enough information to avoid rescanning the full memory
folder on every turn.

Suggested additional fields:

- `layer0_last_processed_message_index`
- `layer0_current_index_block`
- `layer0_last_index_hash`
- `layer0_storage_path`

`ChatCompressionSnapshot` should store:

- selected artifact IDs
- selected compact index block
- previous index hash
- new index hash

## Default Prompt Template: L0 Capture

```text
You are extracting durable artifacts from a conversation for long-term chat memory.

INPUT:
- New turns since the last compression cycle
- A compact current artifact index

TASK:
Identify any code, diagram, markup, configuration, or structured artifacts that should be preserved as durable references.

For each artifact:
1. Decide whether it is new or a revision of an existing artifact.
2. Propose a stable artifact_key.
3. Summarize what the artifact is.
4. Summarize the user intent behind it.
5. Summarize what problem it solves.
6. Identify artifact type and language.
7. Return the normalized artifact content.
8. If it supersedes a prior artifact, identify the prior artifact_id or artifact_key.

RULES:
- Prefer durable artifacts over incidental snippets.
- Prefer the latest corrected or revised version of an artifact.
- Do not invent code that is not present in the conversation.
- Do not write files.
- Return JSON only.
```

## Default Prompt Template: L0 Selection

```text
You are selecting which artifact memory entries should be surfaced in the next compressed context window.

INPUT:
- The current artifact index
- The latest structured conversation state
- Recent turns

TASK:
Choose the latest and most relevant artifacts that the next model call should know about.

For each selected artifact:
1. Include artifact_id
2. Include artifact_key
3. Include version
4. Include type
5. Include a short relevance note

RULES:
- Prefer latest versions unless an older version is explicitly relevant.
- Keep the result compact.
- Do not include full artifact content.
- Return JSON only.
```

## Injection Format

Recommended compact injected block:

```text
## Artifact Memory (Layer 0)
- sailing_right_of_way_mermaid v2 | mermaid | Latest decision flow for sailing right-of-way rules
- formula_cost_chart_vega_lite v4 | vega-lite | Current cost comparison chart with corrected labels
- login_form_html v3 | html | Latest form layout under active discussion
```

This block should be small, stable, and retrieval-oriented.

## Versioning Rules

- If `artifact_key` matches and content hash is unchanged, do not create a new version.
- If `artifact_key` matches and content changed, create a new version and mark the prior latest version as `superseded`.
- If the model is unsure whether an artifact supersedes another, the app may keep it as a new artifact with `status = partial`.

## Failure Handling

If L0 capture fails:

- continue with L1-L4 compression
- retain the previous L0 index block if one exists
- record diagnostics

If storage path expansion fails:

- disable L0 for that compression cycle only
- continue with the remaining layers

If index parsing fails:

- rebuild `INDEX.md` from `index.json` if possible
- otherwise preserve artifact files and create a fresh index on the next successful cycle

## Security And Safety

- Resolved storage path must remain a local absolute path.
- File writes must stay within the configured L0 folder.
- Chat-facing L0 tools must be read-only.
- No artifact should be written outside the configured folder tree.

## Relationship To RAG

L0 artifact memory is not a RAG library.

Differences:

- L0 is per chat and compression-driven.
- RAG is project-attachable and retrieval-oriented.
- L0 stores curated artifact Markdown files and a local index.
- RAG stores general documents and chunks for search.

Possible future bridge:

- manual import of L0 artifact files into a RAG library
- manual export of artifact memory for reuse in another chat

## Recommended Phase Breakdown

### Phase 1

- L0 config fields
- storage folder resolution
- capture model call
- app-owned artifact persistence
- `index.json` and `INDEX.md`
- compact index reinjection
- read-only built-in tool surface

### Phase 2

- better artifact-key conflict resolution
- richer artifact-type detection
- UI previews of stored artifacts
- manual artifact promote/archive actions
- optional export/import workflows

## Open Questions

- Should `INDEX.md` be required in phase 1, or can it be added after `index.json`?
- Should the compact L0 block have its own token budget knob?
- Should some artifact types be excluded by default?
- Should HTML/SVG/diagram artifact rendering previews be available from the desktop UI?
- Should L0 eventually support manual promotion of artifacts into project-wide RAG libraries?
