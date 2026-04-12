# RAG Service Requirements Specification

**Status:** Implementation in progress - proof-of-concept RAG engine active  
**Last Updated:** April 12, 2026  
**Purpose:** Define the requirements for a standalone, reusable Retrieval-Augmented Generation (RAG) service that can be attached to one or more agent projects.  
**Primary Goal:** Provide local, efficient, low-cost document ingestion, indexing, vector search, and retrieval for use in AI chat/project context.

---

## 1. Overview

The RAG service is a local knowledge storage and retrieval system. It is responsible for ingesting documents, splitting them into searchable chunks, generating embeddings, storing metadata and vectors, and returning relevant context for model prompts.

The RAG service must be independent of individual projects. Projects do not own RAG stores directly. Instead, projects attach to one or more RAG libraries with explicit permissions such as read-only, read/write, or disabled.

This allows multiple projects to share the same knowledge bases, while still controlling which projects can read from or write to each RAG.

The RAG service should be implemented as a standalone internal engine module, not as logic tightly embedded into the chat window or main application flow. In the first implementation, the engine will run in-process inside `agent.exe` for simplicity, performance, and easier UI integration. However, the module boundary must be clean enough that the same RAG engine can later be exposed as a plugin, DLL, separate `rag_service.exe`, or MCP server.

Example:

```text
RAG Library: Legal Documents
  Used by Contract Review Project as read-only
  Used by Legal Intake Project as read/write

RAG Library: Internal HR Documents
  Used by HR Assistant Project as read/write
  Used by Management Policy Project as read-only

Project: Compliance Assistant
  Reads from Legal Documents
  Reads from Internal HR Documents
  Writes to Compliance Intake RAG
```

---

## 2. Goals

- Support multiple independent RAG libraries.
- Allow a project to attach to multiple RAG libraries at the same time.
- Allow each project-to-RAG binding to define read/write permissions.
- Support domain-specific knowledge stores, such as legal, HR, product, engineering, or project-specific document collections.
- Support high-volume local document ingestion.
- Keep ongoing cost low by supporting local embedding and local vector storage.
- Preserve source provenance for every retrieved chunk.
- Support hybrid search: vector similarity plus keyword/full-text search.
- Support diagnostics so the user can see what was indexed, what was retrieved, and what was injected into model context.
- Keep the storage and indexing layers modular so vector backends and embedding providers can be swapped later.

---

## 3. Non-Goals For Initial Version

- Cloud-hosted RAG is not required for the first implementation.
- Multi-user network permissions are not required initially.
- Complex access control lists beyond project-level read/write permissions are not required initially.
- OCR for scanned PDFs is not required initially.
- Fully automatic semantic deduplication is not required initially.
- Cross-machine synchronization is not required initially.
- Model-based reranking is not required initially, but the architecture should allow it later.

---

## 4. Core Concepts

### 4.1 RAG Library

A RAG Library is an independent knowledge store.

Each RAG library has:

- Unique ID
- Name
- Description
- Storage location
- Enabled/disabled flag
- Embedding provider configuration
- Embedding model ID
- Embedding dimensions
- Vector backend type
- Chunking settings
- Ingestion settings
- Retrieval defaults
- Metadata schema
- Created timestamp
- Updated timestamp

Examples:

```text
Legal Documents
Internal HR Documents
Engineering Knowledge Base
Customer Support Archive
Project Source Code Index
Research Papers
```

### 4.2 Project Binding

A project binding connects one project to one RAG library.

The binding controls how that project may use the RAG library.

Each binding has:

- Project ID
- RAG library ID
- Enabled/disabled flag
- Can read
- Can write
- Can delete
- Default ingest target flag
- Retrieval priority
- Maximum chunks to retrieve from this RAG
- Optional retrieval filters

Example:

```json
{
  "rag_id": "rag_legal_documents",
  "enabled": true,
  "can_read": true,
  "can_write": false,
  "can_delete": false,
  "default_ingest_target": false,
  "retrieval_priority": 10,
  "max_chunks": 6
}
```

### 4.3 Document

A document is a source item ingested into a RAG library.

Documents may come from:

- Local files
- Folders
- Chat messages
- MCP tool results
- User-pasted text
- Generated summaries
- Imported JSON/CSV/text data

Each document has:

- Document ID
- RAG library ID
- Source type
- Source URI or path
- Title
- Content hash
- Metadata JSON
- Ingestion status
- Created timestamp
- Updated timestamp
- Last indexed timestamp

### 4.4 Chunk

A chunk is a smaller searchable unit derived from a document.

Each chunk has:

- Chunk ID
- Document ID
- RAG library ID
- Chunk text
- Chunk hash
- Chunk index/order
- Token estimate
- Metadata JSON
- Created timestamp
- Updated timestamp

### 4.5 Embedding

An embedding is a vector representation of a chunk.

Each embedding has:

- Chunk ID
- RAG library ID
- Embedding provider ID
- Embedding model ID
- Vector dimensions
- Vector data
- Created timestamp

Embeddings from different models must not be mixed in the same vector index unless the index explicitly supports model partitioning.

---

## 4A. Implementation Architecture

The RAG service must be designed as a separate engine module with a clear API boundary.

Initial deployment:

```text
agent.exe
  hosts the RAG engine in-process
```

Future deployment options:

```text
rag_service.dll
rag_service.exe
RAG MCP server
Python/plugin-hosted RAG adapter
```

The first implementation should not require IPC, networking, or a separate background service. Keeping it in-process makes the proof-of-concept simpler and gives the UI direct access to ingestion progress, diagnostics, project bindings, and retrieved context.

At the same time, the RAG engine must not be coupled directly to Win32 controls, chat rendering, or `main.cpp`. The GUI should call the RAG service through an internal API.

Recommended source layout:

```text
src/
  rag/
    rag_service.h/.cpp
    rag_types.h
    rag_library_store.h/.cpp
    rag_project_bindings.h/.cpp
    rag_sqlite_store.h/.cpp
    rag_embedding_provider.h/.cpp
    rag_ollama_embedding_provider.h/.cpp
    rag_vector_index.h/.cpp
    rag_hnsw_index.h/.cpp
    rag_ingestion_queue.h/.cpp
    rag_retriever.h/.cpp
    rag_context_builder.h/.cpp
```

The rest of the application should communicate with the RAG system through operations such as:

```text
ListLibraries()
CreateLibrary(config)
UpdateLibrary(rag_id, config)
DeleteLibrary(rag_id)

ListProjectBindings(project_id)
AttachLibraryToProject(project_id, rag_id, permissions)
UpdateProjectBinding(project_id, rag_id, permissions)
DetachLibraryFromProject(project_id, rag_id)

QueueIngestion(rag_id, sources, options)
GetIngestionStatus(job_id)
CancelIngestion(job_id)

QueryProject(project_id, query, options)
BuildContextBlock(project_id, query, options)
```

### 4A.1 Why In-Process First

The first RAG implementation should run inside `agent.exe`.

Reasons:

- Simpler implementation
- Lower latency
- No IPC or service lifecycle complexity
- Easier debugging
- Easier access to GUI progress updates
- Easier context injection into chat requests
- Fewer moving parts during proof-of-concept validation

### 4A.2 Why Keep The Engine Separate

Even though the first version runs in-process, the RAG engine must remain separable.

Reasons:

- Future reuse across multiple projects
- Future extraction to a background service
- Future exposure as an MCP server
- Future support for other agent applications
- Better testability
- Better separation between UI, storage, indexing, ingestion, and retrieval

### 4A.3 Future Separate Service

A separate `rag_service.exe` should be considered later if one or more of the following becomes important:

- Indexing should continue while the main app is closed
- Multiple apps should access the same RAG service concurrently
- Large ingestion jobs need stronger isolation from the GUI
- Python-heavy document extraction should run outside the main process
- GPU/CPU resource management needs to be isolated
- The RAG service should be independently updated or restarted
- The RAG service should be shared with other MCP-capable clients

### 4A.4 Future MCP Server Shape

The RAG engine should eventually be exposeable as an MCP server.

Possible MCP tools:

```text
rag_search
rag_ingest_file
rag_ingest_folder
rag_list_libraries
rag_get_document
rag_get_chunk
rag_rebuild_index
rag_get_ingestion_status
```

The internal app does not need to use this MCP interface for its own direct RAG operations. Direct internal calls are preferred for the main app because they are faster and easier to integrate with the UI. The MCP layer is primarily for future interoperability.

---

## 5. Storage Architecture

The RAG service should separate source metadata from vector index storage.

Each RAG library must have a configurable storage location. RAG libraries can become large, so the user must be able to choose a fast or high-capacity folder/drive when creating a library. The app should keep a lightweight registry of known RAG libraries, while the heavy RAG data lives in the selected storage location.

The first implementation should ask for a parent storage folder when creating a RAG library, then create a dedicated subfolder for that RAG library inside the selected location. This avoids mixing multiple RAG libraries together and prevents accidental deletion of unrelated files if the RAG library is removed.

Example:

```text
Selected parent folder:
  D:\RAGStores

Created RAG library folder:
  D:\RAGStores\Legal_Documents_rag_12345
```

The app-level registry should store:

```text
rag_id
absolute storage path
```

The RAG library itself should also store its storage path in `rag.json` for diagnostics and portability checks.

Recommended layout:

```text
<chosen-rag-storage-folder>/
  Legal_Documents_rag_12345/
    rag.json
    rag.sqlite
    documents/
      original/
        imported_or_cached_documents/
      extracted/
        extracted_text/
    indexes/
      hnsw_modelname_768.bin
      faiss_modelname_768.index

<app-data>/
  rag_libraries/
    rag_libraries.json
```

The vector index should be treated as rebuildable. The durable source of truth is the SQLite metadata plus the managed document repository.

Core rule:

```text
The RAG index is disposable.
The SQLite metadata is authoritative.
The managed document folder is the durable source cache.
The original source path is provenance, not the only copy.
```

### 5.1 SQLite Metadata Store

Each RAG library should have a local SQLite database.

The SQLite database stores:

- Documents
- Chunks
- Chunk text
- Metadata
- Ingestion state
- Full-text search index
- Embedding/index state
- Source hashes
- Errors and diagnostics
- Stored document relative paths
- Extracted text relative paths
- Original source URI/path

SQLite should use WAL mode for better ingestion and read concurrency.

### 5.2 Vector Index Store

The vector index may be stored separately from SQLite.

Possible vector backends:

- HNSWlib
- FAISS
- Qdrant local sidecar
- SQLite vector extension
- Future custom backend

The first implementation should define a vector backend interface so the app can start with one backend and migrate later.

### 5.3 Managed Document Repository

Each RAG library should support a managed document repository for ingested files.

The managed repository allows the RAG library to:

- Rebuild vector indexes
- Re-chunk documents after chunking settings change
- Re-embed documents after the embedding model changes
- Serve or open original documents later
- Preserve documents even if the original source path is moved or deleted
- Audit retrieved chunks back to their original document
- Become portable if the whole RAG library folder is moved

Recommended managed document layout:

```text
documents/
  original/
    <relative-import-path-or-generated-layout>
  extracted/
    <matching-extracted-text-layout>
```

The `original/` folder stores the original document, or a byte-for-byte managed copy when possible.

The `extracted/` folder stores extracted text or normalized content used for chunking.

### 5.4 Document Storage Modes

Each RAG library should define a document storage mode.

Supported modes:

- `copy_into_rag_store`
- `reference_in_place`
- `copy_and_track_original`

Recommended default:

```text
copy_and_track_original
```

In `copy_and_track_original` mode:

- The original source path or URI is recorded.
- A managed copy is stored inside the RAG library.
- Extracted text is stored separately.
- The RAG can be rebuilt even if the original file disappears.

In `reference_in_place` mode:

- The original file is not copied.
- The RAG stores the source path and content hash.
- Rebuilds depend on the original file still being available.
- The UI must warn the user that the RAG is not self-contained.

In `copy_into_rag_store` mode:

- The document is copied into the RAG library.
- The original source may be recorded if known, but the managed copy becomes the primary source.

### 5.5 Document Path Metadata

The SQLite database must track document paths using relative paths where possible.

Suggested document fields:

```text
id
rag_id
display_name
original_source_uri
original_source_type
stored_relative_path
extracted_relative_path
content_hash
file_size
mime_type
created_at
imported_at
last_indexed_at
metadata_json
```

Relative paths should be resolved against the RAG library root.

Example:

```text
stored_relative_path = documents/original/2026/handbook.pdf
extracted_relative_path = documents/extracted/2026/handbook.txt
```

This makes the RAG library portable across folders or drives.

### 5.6 Rebuild Requirements

The RAG service must support rebuilding from the managed repository and SQLite metadata.

Rebuild scenarios:

- Vector backend changes
- Embedding model changes
- Chunking settings change
- Index corruption
- User requests full rebuild
- User moves the RAG library to a new disk/folder

If the RAG uses `reference_in_place` mode and source files are missing, the rebuild must report which documents cannot be rebuilt.

---

## 6. Recommended Backend Strategy

### 6.1 First Serious Implementation

Use:

```text
SQLite metadata store
SQLite FTS5 keyword index
HNSWlib vector index
Ollama embedding provider
```

Reasons:

- Works locally
- Low recurring cost
- Fast approximate nearest-neighbor search
- Practical for large document collections
- Smaller dependency burden than a full database service
- Good fit for a native C++ desktop app

### 6.2 Scale-Up Option

Add FAISS as a later backend.

FAISS is appropriate when:

- Datasets become very large
- Batch search performance matters
- Product quantization or compressed indexes are needed
- GPU acceleration becomes important

### 6.3 Optional Sidecar Option

Add Qdrant as an optional local sidecar backend.

Qdrant is appropriate when:

- Payload filtering becomes complex
- A durable vector database service is desired
- A local HTTP/gRPC service is acceptable
- Users want advanced vector DB features without the app owning all index internals

### 6.4 Avoid Hard-Coding ChromaDB

ChromaDB may be useful for prototyping or Python-heavy workflows, but it should not be the initial hard dependency for the native C++ app.

Reasons:

- Adds Python/runtime complexity
- Less ideal for a self-contained native Windows app
- Makes packaging more complicated

The architecture may support Chroma later through a plugin or sidecar adapter.

---

## 7. Embedding Providers

The RAG service must support pluggable embedding providers.

### 7.1 Local Ollama Embeddings

Initial local embedding provider.

Requirements:

- Configure Ollama base URL
- Configure embedding model name
- Support batch embedding where available
- Validate embedding dimensions
- Detect model changes and require reindexing
- Show embedding errors in diagnostics

### 7.2 OpenAI-Compatible Embeddings

Optional provider for remote embeddings.

Requirements:

- Use configured provider base URL and API key
- Support `/v1/embeddings`
- Support model selection
- Track estimated embedding cost where pricing metadata exists
- Warn user that document text will be sent to a remote API

### 7.3 ONNX Runtime Local Embeddings

Future preferred embedded local provider.

Requirements:

- Load local ONNX embedding model
- Run inference locally
- Support CPU execution
- Support optional GPU acceleration later
- Bundle or configure tokenizer support

---

## 8. Ingestion Pipeline

The ingestion pipeline must run asynchronously so the UI remains responsive.

Pipeline:

```text
Discover source
-> Hash source
-> Extract text
-> Split into chunks
-> Hash chunks
-> Skip unchanged chunks
-> Generate embeddings in batches
-> Store metadata and chunks in SQLite
-> Add vectors to index
-> Persist index
-> Update diagnostics/status
```

### 8.1 Supported Initial Input Types

Initial version:

- `.txt`
- `.md`
- `.json`
- `.csv`
- `.log`
- `.xml`
- `.cpp`
- `.h`
- `.hpp`
- `.c`
- `.cs`
- `.js`
- `.ts`
- `.py`
- `.ps1`
- `.bat`

Later versions:

- PDF text extraction
- DOCX
- XLSX
- HTML
- Images with OCR
- Audio transcription

### 8.2 Folder Ingestion

The service must support ingesting folders.

Folder ingestion requirements:

- Recursive scan option
- Include glob patterns
- Exclude glob patterns
- Maximum file size limit
- Binary file detection
- Hidden/system file handling option
- Re-scan unchanged files quickly using modified time and hash

### 8.3 Incremental Updates

The ingestion system must avoid reprocessing unchanged content.

Requirements:

- Track source file path
- Track file modified timestamp
- Track file size
- Track content hash
- Track chunk hashes
- Re-embed only changed chunks when possible
- Mark deleted source files as removed or stale

### 8.4 Chunking

Chunking settings should be configurable per RAG library.

Settings:

- Target chunk size
- Chunk overlap
- Maximum chunk size
- Minimum chunk size
- Preserve headings
- Preserve code blocks
- Preserve paragraph boundaries
- File-type-specific chunking strategy

Default:

```text
Target chunk size: 500-900 tokens
Overlap: 50-150 tokens
```

### 8.5 Ingestion Queue

The ingestion queue should support:

- Background processing
- Pause/resume
- Cancel
- Retry failed documents
- Progress display
- Error capture per document
- Concurrent extraction where safe
- Batched embedding generation
- Batched database/index writes

---

## 9. Retrieval Pipeline

The RAG service must support querying one or more RAG libraries at the same time.

Pipeline:

```text
User prompt
-> Query embedding
-> Vector search per attached RAG
-> Keyword/FTS search per attached RAG
-> Merge results
-> Deduplicate
-> Apply project/RAG filters
-> Rank final results
-> Return retrieved chunks with provenance
-> Inject selected chunks into model context
```

### 9.1 Hybrid Search

Retrieval must combine:

- Vector similarity search
- Keyword/full-text search
- Metadata filtering

Vector search is good for semantic similarity.

Keyword search is important for:

- File paths
- Function names
- Error codes
- Legal clause numbers
- Policy IDs
- Names
- Dates
- Configuration keys

### 9.2 Multi-RAG Retrieval

When a project has multiple readable RAG bindings, the service should query all enabled RAG libraries.

Each RAG should have:

- Retrieval priority
- Max chunks
- Score threshold
- Optional metadata filters

The final result set should preserve the originating RAG name and source.

Example injected context:

```text
Retrieved Context:

[Legal Documents / Employment Contracts / chunk 18]
Source: D:\Docs\Contracts\employment_template.md
Text: ...

[Internal HR Documents / Remote Work Policy / chunk 4]
Source: D:\HR\Policies\remote_work.md
Text: ...
```

### 9.3 Provenance

Every retrieved chunk must include:

- RAG library name
- Document title
- Source path or source URI
- Chunk ID
- Score
- Retrieval method
- Last indexed timestamp
- Metadata

The model should receive enough source information to understand where the context came from.

The UI should show this provenance in diagnostics.

### 9.4 Result Limits

The service must support:

- Global max retrieved chunks
- Per-RAG max retrieved chunks
- Max injected characters/tokens
- Score threshold
- Diversity/deduplication controls

---

## 10. Context Injection

Retrieved chunks are injected into the model request context.

Requirements:

- Inject retrieved context before the model call
- Include source labels and RAG library names
- Include enough metadata for model reasoning
- Avoid injecting duplicate chunks
- Respect model context window limits
- Show injected chunks in diagnostics
- Allow the user to disable RAG injection per request or per project

The injection should be separate from normal chat history so it is clear which content came from retrieval.

Suggested format:

```text
Retrieved Project Knowledge:
The following excerpts were retrieved from attached RAG libraries. Use them when relevant and cite their source labels when helpful.

[RAG: Legal Documents | Source: employment_template.md | Chunk: 18]
...

[RAG: Internal HR Documents | Source: remote_work_policy.md | Chunk: 4]
...
```

---

## 11. Permissions

Permissions are defined per project-to-RAG binding.

### 11.1 Read Permission

If `can_read` is true:

- Project can query the RAG
- Retrieved chunks may be injected into chat context
- User can view matching documents/chunks

### 11.2 Write Permission

If `can_write` is true:

- Project can ingest new documents into the RAG
- Project can update existing documents
- Project can trigger reindexing

### 11.3 Delete Permission

If `can_delete` is true:

- Project can delete documents from the RAG
- Project can remove chunks/index entries

Delete should default to false.

### 11.4 Read-Only Knowledge Stores

Some RAG libraries should be read-only for most projects.

Examples:

- Approved legal documents
- HR policy manuals
- Compliance documents
- Versioned product documentation

### 11.5 Writable Intake Stores

Some RAG libraries should be writable for ingestion workflows.

Examples:

- New document intake
- Project scratch knowledge
- Research collection
- User-uploaded files

---

## 12. Management UI Requirements

### 12.1 RAG Library Manager

The app must provide a GUI for managing RAG libraries.

Features:

- Create RAG library
- Edit RAG library
- Delete/archive RAG library
- Enable/disable RAG library
- Set name and description
- Set storage location
- Choose embedding provider/model
- Choose vector backend
- Configure chunking settings
- Configure ingestion settings
- View document count
- View chunk count
- View index size
- View embedding model/dimensions
- View last indexed timestamp
- Rebuild index
- Compact/optimize index
- Export diagnostics

### 12.2 Project RAG Settings

Each project must have a GUI for attaching RAG libraries.

Features:

- List available RAG libraries
- Attach/detach RAG libraries
- Enable/disable binding
- Set read permission
- Set write permission
- Set delete permission
- Set default ingest target
- Set retrieval priority
- Set max chunks per RAG
- Test retrieval query

### 12.3 Ingestion UI

The app must provide an ingestion UI.

Features:

- Add files
- Add folders
- Paste text
- Select target RAG library
- Show whether project has write access
- Preview extracted text
- Configure include/exclude patterns
- Start ingestion
- Pause/resume/cancel ingestion
- Show progress
- Show errors
- Retry failed items

### 12.4 Retrieval Diagnostics UI

The app must show retrieval diagnostics.

Features:

- User query
- RAG libraries queried
- Vector results
- Keyword results
- Final merged results
- Scores
- Source paths
- Chunk text preview
- Injected context preview
- Token/character estimate

---

## 13. Service API Requirements

The internal RAG service should expose operations similar to:

```text
CreateRagLibrary(config)
UpdateRagLibrary(rag_id, config)
DeleteRagLibrary(rag_id)
ListRagLibraries()
GetRagLibrary(rag_id)

AttachRagToProject(project_id, rag_id, permissions)
UpdateProjectRagBinding(project_id, rag_id, permissions)
DetachRagFromProject(project_id, rag_id)
ListProjectRagBindings(project_id)

QueueIngestion(rag_id, sources, options)
PauseIngestion(job_id)
ResumeIngestion(job_id)
CancelIngestion(job_id)
GetIngestionStatus(job_id)

QueryProjectRags(project_id, query, options)
QueryRag(rag_id, query, options)
BuildContextBlock(project_id, query, options)

RebuildIndex(rag_id)
CompactIndex(rag_id)
ValidateIndex(rag_id)
```

---

## 14. Performance Requirements

The RAG service should be designed for large local collections.

Initial target:

- Tens of thousands of documents
- Hundreds of thousands of chunks
- Fast incremental reindexing
- Query latency suitable for interactive chat

Performance goals:

- Ingestion should batch embeddings
- Vector search should be approximate nearest-neighbor for large indexes
- Keyword search should use SQLite FTS5 or equivalent
- The UI must remain responsive during ingestion and retrieval
- Index writes should be batched
- Index persistence should be safe against application crash

The service should report:

- Documents indexed
- Chunks indexed
- Embeddings generated
- Index size on disk
- Average ingestion rate
- Average query latency
- Last error

---

## 15. Security And Privacy

- Local RAG libraries should remain local by default.
- If remote embeddings are used, the UI must warn that document text is sent to the embedding provider.
- API keys must not be stored in plaintext.
- RAG storage locations should be user-visible and configurable.
- Retrieved context should be shown in diagnostics to avoid hidden data leakage.
- Project permissions must be enforced before ingestion, deletion, or retrieval.
- Read-only RAG bindings must not allow document ingestion.
- Deleting a project must not delete shared RAG libraries unless explicitly requested.

---

## 16. Error Handling

The service must handle:

- Missing source files
- Access-denied files
- Binary/unsupported files
- Oversized files
- Text extraction failures
- Embedding provider failures
- Embedding dimension mismatch
- Vector index corruption
- SQLite write failures
- Interrupted ingestion jobs
- Deleted or moved source folders

Errors should be visible per document and per ingestion job.

---

## 17. Initial Implementation Phases

### Phase RAG-1: Foundation

- Create the RAG engine as a standalone internal module hosted by `agent.exe`
- Define the internal `RagService` API boundary so the engine can later be extracted
- Add RAG library data model
- Add project-to-RAG bindings
- Add RAG Library Manager UI
- Add Project RAG Settings UI
- Add SQLite metadata database
- Add basic document/chunk schema
- Add text/code file ingestion
- Add SQLite FTS5 keyword search
- Add retrieval diagnostics
- Add context injection from retrieval results

### Phase RAG-2: Local Embeddings And Vector Search

- Add embedding provider abstraction
- Add Ollama embedding provider
- Add vector backend abstraction
- Add HNSWlib backend
- Store vectors and chunk mappings
- Add vector search
- Add hybrid vector + keyword retrieval
- Add index rebuild
- Add embedding model change detection

### Phase RAG-3: Higher Volume And Reliability

- Add folder watch/rescan
- Add incremental chunk updates
- Add ingestion queue controls
- Add pause/resume/cancel
- Add batched embedding improvements
- Add index compaction
- Add crash recovery for ingestion jobs
- Add better document diagnostics

### Phase RAG-4: Advanced Backends

- Add FAISS backend
- Add optional Qdrant local sidecar backend
- Evaluate extracting the RAG engine into `rag_service.exe` if ingestion/indexing has become heavy enough
- Add optional MCP server wrapper for RAG interoperability
- Add compressed indexes where available
- Add advanced metadata filtering
- Add optional reranking

### Phase RAG-5: Rich Document Processing

- Add PDF text extraction
- Add DOCX/XLSX extraction
- Add HTML extraction
- Add OCR option
- Add audio transcription ingestion
- Add preview-before-ingest

---

## 18. Current Implementation Progress - April 12, 2026

This section records the current proof-of-concept implementation state observed in the application code. It should be treated as the baseline for the next RAG development steps.

### 18.1 Snapshot

- The RAG engine exists as an in-process C++ module exposed through `RagService`.
- RAG libraries are independent from projects and can be attached to projects through project-to-RAG bindings.
- The app now has a RAG Service Manager screen launched from the main window.
- RAG library add/edit is handled on one editor screen with name, description, storage location, embedding settings, chunk settings, segmentation settings, runtime controls, and save/cancel actions.
- A RAG library can be attached to the active project as read-only or read/write.
- Project chat prompts can retrieve from attached RAG libraries and inject a "Retrieved Project Knowledge" context block into the request.
- Local embeddings are supported through Ollama and LM Studio compatible HTTP endpoints.
- A "none" embedding provider is also supported for keyword-only or diagnostic use.
- The initial vector search is implemented as a SQLite embedding BLOB scan with cosine similarity, not a production vector index.
- Hybrid retrieval currently merges vector similarity with SQLite FTS5 keyword search and falls back to a simple keyword scan when needed.
- Text, code, Markdown, JSON, CSV, XML, HTML, DOCX/DOCM, XLSX/XLSM, and PDF ingestion paths exist.
- Rebuild Database now clears the indexed database rows and re-ingests saved original documents, rather than merely trying to refresh existing rows.

### 18.2 Implemented Data Model

The following data structures exist in the application:

- `RagLibraryConfig`
  - Unique ID, name, description, storage path, enabled flag.
  - Embedding provider, base URL, model, dimensions, and vector backend setting.
  - File size limit, storage mode, chunk size, overlap, and default max chunks.
  - Rebuild-required flag and rebuild reason.
  - Large extracted-document segmentation settings: enabled flag, threshold, segment size, and segment overlap.
- `ProjectRagBinding`
  - RAG library ID.
  - Enabled flag.
  - Read, write, and delete permissions.
  - Default ingest target flag.
  - Retrieval priority.
  - Per-binding max chunks.
- `RagDocumentRecord`
  - Document ID, RAG ID, display name, original source URI, source type.
  - Stored original relative path and extracted relative path.
  - Content hash, file size, MIME value, imported timestamp, indexed timestamp.
  - Free-form metadata JSON.
- `RagChunkRecord`
  - Chunk ID, document ID, chunk index, text, offsets, token estimate, metadata JSON.
- Diagnostic and workflow types
  - Library stats, document previews, ingestion progress, embedding runtime status, embedding test results, extraction tool status, and query results.

### 18.3 Current Storage And Data Layout

- The default RAG registry root is `data/rag_libraries`.
- `rag_libraries.json` stores the registered RAG library IDs and storage paths.
- Each RAG library is created in a dedicated folder under the user-selected storage parent.
- Each RAG library folder contains:
  - `rag.json` for library configuration.
  - `rag.sqlite` for metadata, chunks, FTS, embeddings, and ingestion tables.
  - `rag_catalog.json` as a fallback catalog.
  - `documents/original` for managed copies of source documents.
  - `documents/extracted` for extracted text or Markdown-like representations.
  - `indexes` for future external vector index files.
- SQLite is configured with WAL mode, normal synchronous mode, and foreign keys.
- The SQLite schema currently includes:
  - `documents`
  - `chunks`
  - `chunks_fts` using FTS5
  - `embeddings`
  - `ingestion_events`
- The `ingestion_events` table exists but is not yet used as a full persistent job history.

### 18.4 Current Ingestion Pipeline

- File ingestion and folder ingestion are implemented.
- Folder ingestion asks whether the folder should be traversed recursively.
- Folder ingestion stores folder-level metadata including:
  - Original folder root.
  - Relative path from the selected folder.
  - Absolute source path.
  - Whether recursive ingestion was used.
- Direct file ingestion stores file-level metadata including:
  - Original absolute source path.
  - Source mode of `files`.
- The ingest preview workflow reports supported files, skipped files, and errors before import.
- Supported plain-text or text-like extensions include TXT, Markdown, JSON, CSV, logs, XML, common C/C++/C#/JavaScript/TypeScript/Python/PowerShell/batch/config formats, HTML, CSS, and SQL.
- Supported rich document extensions currently include DOCX, DOCM, XLSX, XLSM, and PDF.
- The import path computes a content hash and can skip unchanged files during normal ingestion.
- Existing chunks for a document are deleted and replaced when a source is re-ingested.
- If the library uses managed storage, source files are copied into `documents/original`.
- Extracted rich text is written into `documents/extracted`.
- A configurable max file size is enforced before ingestion.
- Text is sanitized before JSON embedding requests and before storage to avoid invalid UTF-8 failures.

### 18.5 Rich Document Extraction

- DOCX and DOCM extraction are implemented natively by extracting OOXML content and reading document XML, headers, footers, footnotes, and endnotes.
- XLSX and XLSM extraction are implemented natively by extracting OOXML workbook content, shared strings, and sheets into Markdown table-style text.
- HTML extraction converts basic HTML into Markdown-like text.
- PDF extraction is implemented with a best-effort cascade:
  - `pdftotext.exe` from Poppler, with UTF-8 output.
  - `mutool.exe` from MuPDF, when available.
  - Python plus `pypdf`, when available.
  - A built-in literal text fallback for simple PDFs.
- Extraction tool diagnostics list recommended and optional tools.
- The installer workflow can launch `winget` to install recommended missing tools such as Poppler.
- OCR for scanned PDFs is not yet implemented. Tesseract is listed as a future extraction tool.

### 18.6 Large Extracted Document Segmentation

- Large extracted rich documents can be split into segment Markdown files before chunking.
- The split threshold, segment size, and segment overlap are configurable per RAG library.
- Segment files are written as `segment_00001.md`, `segment_00002.md`, and so on.
- A segment manifest is written with segment metadata.
- Chunk metadata records segment index, segment count, segment relative path, segment start, segment end, and segment overlap.
- This allows very large PDF, Word, or Excel extractions to be processed in smaller pieces while preserving overlap for semantic continuity.

### 18.7 Embedding Providers And Runtime Controls

- The embedding provider editor is a dropdown with:
  - `none`
  - `Ollama`
  - `LM Studio`
- Ollama defaults to:
  - Base URL: `http://localhost:11434`
  - Model: `nomic-embed-text`
- LM Studio defaults to:
  - Base URL: `http://localhost:1234/v1`
  - Model: `nomic-embed-text-v1.5`
- Ollama embedding requests use `/api/embed` and fall back to `/api/embeddings`.
- LM Studio embedding requests use `/embeddings`.
- The RAG library editor includes runtime controls for Ollama:
  - Check status.
  - Test embedding.
  - Start.
  - Stop.
  - Install Ollama.
  - Install embed text model.
  - View recent runtime log output.
- When a selected Ollama provider is not running, the app can start `ollama serve` automatically.
- If the app started Ollama, it attempts to stop that managed process when the app shuts down.
- Runtime diagnostics are written to `data/rag_embedding_runtime.log`.
- Remote OpenAI-compatible embeddings and local ONNX/sentence-transformer embeddings are not yet implemented.

### 18.8 Retrieval And Context Injection

- `QueryRag` performs retrieval against a single library.
- `QueryProject` performs retrieval against all enabled readable libraries attached to the active project.
- Project retrieval sorts attached RAG bindings by retrieval priority before querying them.
- Results are globally sorted and capped by the requested maximum result count.
- Vector retrieval currently scans stored embedding BLOBs and computes cosine similarity in-process.
- Keyword retrieval uses SQLite FTS5 with BM25 ranking.
- If no FTS/vector result is available, a simple keyword fallback scan is used.
- Retrieval result metadata includes library name, document name, chunk ID, source path, score, and search method.
- The chat request builder appends a "Retrieved Project Knowledge" block to the system prompt when RAG results are found.
- RAG context injection currently uses a chunk count limit but does not yet apply token-budget-aware trimming.

### 18.9 Current UI Workflows

- The main window includes a bottom-left `RAG Service` button.
- The RAG Service Manager supports:
  - Add library.
  - Edit library.
  - Remove library.
  - Attach selected library to the active project as read-only.
  - Attach selected library to the active project as read/write.
  - Detach selected library from the active project.
  - Install extraction tools.
  - Ingest files.
  - Ingest folder.
  - Rebuild database.
  - Browse indexed documents.
  - Reindex a document by ID.
  - Delete a document by ID.
  - Search selected library or active project RAG context.
  - View progress and status messages.
- The manager displays library details, stats, binding state, and rebuild-required warnings.
- The rebuild workflow asks for confirmation, clears database content, re-ingests saved originals, and updates the progress bar.
- The folder ingest workflow asks whether ingestion should recurse into child folders.
- The document browse and search workflows expose provenance and metadata for diagnostics.
- There is no full binding editor yet for changing retrieval priority, per-binding max chunks, default ingest target, or delete permission after quick attach.

### 18.10 Reliability Fixes Already Applied

- PDF and embedding text paths now sanitize invalid UTF-8 and problematic control bytes before JSON serialization.
- Working rich document extraction writes sanitized extracted text.
- Rebuild no longer depends on the previous chunk/index state. It clears rows and re-ingests saved originals.
- Large extracted documents can be segmented before chunking to avoid processing one huge extracted file at once.
- The Poppler `pdftotext.exe` path is now a visible diagnostic/install workflow instead of a hidden dependency.

### 18.11 Partially Implemented Or Important Gaps

- A production vector backend is not implemented yet. The `vector_backend` setting exists, but only the SQLite vector BLOB scan is active.
- HNSWlib, FAISS, Qdrant, sqlite-vss, and ChromaDB integrations are not yet implemented.
- There is no persistent ingestion job queue with job IDs, pause, resume, cancel, retry, or crash recovery.
- `ingestion_events` exists in SQLite but is not yet used as a complete diagnostic/job table.
- Incremental indexing is file-level only. There is no per-chunk diffing or partial re-embedding.
- There is no folder watcher, scheduled background reindexing, or deleted-source detector.
- Folder ingest preview handles permission-denied skips, but actual folder ingestion still needs stronger skip/continue behavior for inaccessible files.
- Include/exclude glob filters are not yet implemented.
- Hidden/system file policy is not yet exposed.
- Paste-text ingestion, chat-message ingestion, summary ingestion, attachment ingestion, and MCP-result ingestion are not implemented.
- RAG indexing of chat messages and condensed summaries is not implemented.
- OCR for scanned/image PDFs is not implemented.
- Audio transcription ingestion is not implemented.
- Image ingestion and image captioning are not implemented.
- Advanced metadata fields, tags, and metadata filters are not implemented.
- Query reranking is not implemented.
- Query diversity controls and score thresholds are not implemented.
- RAG context injection does not yet show a dedicated "what was injected" diagnostic screen.
- RAG context injection does not yet integrate with the configured model context window for token-budget-aware trimming.
- Per-project RAG bindings store read/write/delete/default/priority/max-chunks values, but the UI only exposes quick read-only/read-write attach and detach.
- Delete permission exists in the model but document deletion currently follows the manager workflow rather than a complete per-project permission editor.
- The app still runs the RAG engine in-process. It has not been extracted to a plugin, DLL, separate `rag_service.exe`, or MCP server.
- API key encryption and remote embedding privacy warnings are not complete.
- Storage folder migration for an existing RAG library is not implemented. The edit screen treats storage location as effectively fixed after creation.

### 18.12 Recommended Next Implementation Order

1. Add a full project RAG binding editor so the user can adjust read/write/delete permissions, retrieval priority, max chunks, and default ingest target.
2. Add token-budget-aware RAG context construction using the selected model's optional context window value.
3. Add a persistent ingestion job table and diagnostics screen using the existing `ingestion_events` schema.
4. Add include/exclude filters, hidden/system file handling, and stronger inaccessible-file handling for real folder ingestion.
5. Add "injected context" diagnostics so the user can see exactly which chunks were sent to the model for a chat turn.
6. Add the first production vector backend, with HNSWlib as the likely local-first candidate and FAISS/Qdrant reserved for higher-volume or optional advanced paths.
7. Add chat/message/summary indexing after the library ingestion and diagnostics workflow is stable.
8. Add OCR for scanned PDFs after the text-PDF pipeline is stable.

---

## 19. Open Questions

- Should the first production vector backend be HNSWlib, FAISS, Qdrant, sqlite-vss, or another local-first option?
- Should the current SQLite vector scan remain as a small-library fallback after a production vector backend is added?
- Should each RAG library have one embedding model only, or support multiple parallel indexes per embedding model?
- Should retrieval use a global merge strategy or a configurable per-project ranking strategy?
- Should RAG libraries support tags and user-defined metadata fields before chat-message indexing is added?
- Should the service support scheduled background reindexing in the first production RAG phase?
- Should source documents always be copied into the RAG library, or should reference-in-place remain a supported storage mode?
- Should read-only RAG libraries be cryptographically sealed or only permission-protected in the app?
- When should the in-process RAG engine be extracted into a separate `rag_service.exe`?
- Should the future external RAG interface be a private app protocol, an MCP server, or both?
