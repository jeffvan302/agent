# RAG Service Requirements Specification

**Status:** Implementation in progress - proof-of-concept RAG engine active  
**Last Updated:** April 13, 2026
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
- OCR for scanned PDFs is not required initially. Standalone image-file OCR/vision ingestion is now implemented as a proof-of-concept pipeline, but scanned/image-only PDF page OCR remains separate future work.
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

Implemented/prototype version:

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
- `.cmd`
- `.ini`
- `.toml`
- `.yaml`
- `.yml`
- `.html`
- `.htm`
- `.css`
- `.sql`
- `.docx`
- `.docm`
- `.xlsx`
- `.xlsm`
- `.pdf`
- `.png`
- `.jpg`
- `.jpeg`
- `.bmp`
- `.tif`
- `.tiff`
- `.webp`

Later versions:

- Richer PDF/OCR handling for scanned or image-only PDFs
- More robust GPU PaddleOCR packaging and model management
- More vision-language providers beyond the first Ollama path
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
- Add standalone image OCR/vision ingestion
- Add OCR option for scanned/image-only PDFs
- Add audio transcription ingestion
- Add preview-before-ingest

---

## 18. Current Implementation Progress - April 13, 2026

This section records the current proof-of-concept implementation state observed in the application code. It should be treated as the baseline for the next RAG development steps.

### 18.1 Snapshot

- Overall status: operational proof-of-concept. The RAG engine can be used today for local library creation, file/folder ingestion, rich document extraction, standalone image-file OCR/vision extraction, embedding-backed or keyword-only indexing, passive chat context, active model-callable tools, rebuild, and diagnostics.
- Production readiness status: not production-ready yet for large-scale stores, long-running unattended ingestion, high-safety persistent memory, or fully controlled retrieval budgets. The highest-risk gaps are production vector indexing, ingestion job persistence, token-budget-aware context construction, RAG working sets, metadata filters, and safety/audit controls for write-enabled RAGs.
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
- Text, code, Markdown, JSON, CSV, XML, HTML, DOCX/DOCM, XLSX/XLSM, PDF, and standalone image ingestion paths exist.
- Rebuild Database now clears the indexed database rows and re-ingests saved original documents, rather than merely trying to refresh existing rows.
- Active model-callable RAG tools are now wired into chat execution for models with tool support when a project RAG binding has Enable, Read, and Tool checked.
- Current built-in RAG tools include `rag_list_libraries`, `rag_search`, `rag_get_document`, `rag_write_document_to_drive`, and `rag_ingest_generated_document`.
- Project Settings now includes a fuller RAG binding editor for enabled/read/write/tool/delete/export/write-file permissions, write-file folder path template, default ingest target, retrieval priority, per-binding max chunks, and default confidence thresholds.

### 18.1A Detailed Current RAG Baseline

This subsection is the current detailed RAG implementation baseline. It captures features that are already present in code even when they were originally described as future requirements.

#### Engine Boundary

- The RAG engine runs in-process inside `agent.exe`.
- The engine is exposed through the `RagService` C++ class.
- The RAG service is not directly coupled to Win32 controls; UI windows call the service API.
- The service is still designed as a separable internal module that can later become a DLL, sidecar service, plugin, or MCP server.
- There is no external `rag_service.exe`, plugin interface, or MCP server wrapper yet.

#### Public RAG Service Capabilities

The current `RagService` API supports:

- Initialize and locate the RAG root.
- List, get, create, update, and delete RAG libraries.
- Get library stats.
- List indexed documents.
- Get a single document record.
- Load extracted document text, including reassembling segmented extracted documents from a segment manifest.
- Preview files and folders before ingestion.
- Ingest selected files.
- Ingest selected folders, with recursive option.
- Ingest generated Markdown/text documents created by the active RAG write tool.
- Reindex a single document.
- Delete a single document, with optional managed file deletion.
- Load, save, upsert, and remove project RAG bindings.
- Rebuild a library from saved original documents with progress callbacks.
- Query one library.
- Query all readable project-attached libraries.
- Build passive chat context blocks.
- Check, start, stop, install, and test embedding runtimes.
- Inspect and launch installation for extraction tools.
- Load and save system-wide image ingest settings.
- Check image ingest runtime status for Tesseract, Python, PaddleOCR, Ollama, and the configured vision endpoint.
- Launch visible installer commands for Tesseract, PaddleOCR, Ollama, and the configured Ollama vision model.

#### Project Binding And Permissions

- A project can attach to multiple RAG libraries.
- Project bindings currently store Enabled, Read, Write, Tool, Delete, Export/Write file, Write-file folder path template, Default ingest target, Retrieval priority, Max chunks, and default minimum/maximum confidence thresholds.
- The RAG Service Manager quick-attach buttons can attach the selected RAG as read-only or read/write.
- Project Settings can toggle Enabled, Read, Write, Tool, Delete, Write file, and Default ingest target.
- Project Settings can configure the write-file folder path template for RAG document exports; the path may include project variable placeholders such as `$<ProjectFolder>$`.
- Project Settings can edit Retrieval priority, Max chunks, default minimum confidence, and default maximum confidence.
- Tool exposure requires Enabled and Read.
- Write file/export requires Enabled and Read.
- Write requires Enabled and Read.
- Write-capable active tools require Write.
- Delete permission and Default ingest target require Enabled, Read, and Write.
- Only one RAG binding can be marked as the project's default ingest target at a time.
- Delete and Write file/export permissions are configurable in Project Settings; write-file export is enforced by the active RAG tool, while document deletion still needs a separate project-permission check.

#### Passive RAG Context Injection

- Passive RAG context injection still runs during chat send for readable attached RAGs.
- The query is based on the current user message.
- Returned chunks are formatted into a "Retrieved Project Knowledge" system prompt block.
- The context block includes RAG name, document title, chunk ID, source path, retrieval method, score, metadata JSON, and text.
- Passive injection uses a fixed global result limit in the chat send path and per-binding max chunks in project querying.
- Passive injection filters results through each binding's default minimum/maximum confidence thresholds.
- Passive injection does not yet use the selected model's `context_window` for token-budget-aware trimming.
- There is no per-project or per-RAG switch yet to disable passive injection while leaving active RAG tools enabled.

#### Active RAG Tool Mode

- Active RAG tools are advertised only when the selected model supports tools and the active project has at least one enabled readable RAG binding with Tool checked.
- Active RAG tools are inserted into the same OpenAI-compatible `tools` array as MCP tools.
- RAG tool calls are routed inside the chat tool loop rather than through an external MCP server.
- RAG tool calls are shown in the existing tool trace pane as `RAG / <tool name>`.
- RAG tool results are returned to the model as JSON text in standard tool result messages.
- RAG tool definitions are intentionally explicit about when to use each tool, what each tool returns, what defaults apply, and which follow-up tool should be used next.
- `rag_list_libraries` returns the active project's exposed RAG libraries with name, description, storage policy, embedding settings, vector backend, permissions, retrieval priority, max chunks, default confidence window, and configured write-file folder template.
- `rag_list_libraries` also returns per-library `available_tool_actions` and a top-level recommended workflow so the model can choose between search, document retrieval, write-to-drive export, and generated-document ingestion without guessing.
- `rag_search` can search all exposed readable RAGs or a caller-provided list of RAG IDs.
- `rag_search` supports query, max results, candidate limit, minimum confidence, maximum confidence, include text, and retrieval mode intent.
- When `rag_search` omits minimum or maximum confidence, it uses the selected RAG's project binding default for that side of the confidence window.
- `rag_search` returns the confidence window used for each searched RAG library for diagnostics.
- `rag_search` returns results sorted by score/confidence, with source, document, chunk, retrieval method, metadata, and optional text.
- `rag_search` confidence is currently normalized by clamping the existing retrieval score into the `0.0` to `1.0` range.
- `rag_search` accepts `retrieval_mode`, but the current backend still runs the hybrid/fallback retrieval path and reports the actual retrieval method per result.
- `rag_get_document` returns document metadata, managed source path information, extracted relative path, original URI, MIME/file metadata, provenance metadata, and optional extracted text.
- `rag_get_document` can reassemble segmented rich-document extraction output from the segment manifest up to the requested character limit.
- `rag_write_document_to_drive` writes either the managed/file-backed original document or the extracted Markdown/text representation to the RAG binding's configured write-file folder without using filesystem MCP tools.
- `rag_write_document_to_drive` accepts either a full relative `target_relative_path` or a `target_folder_relative_path` plus optional `target_file_name`.
- `rag_write_document_to_drive` expands project variable placeholders in the configured folder, requires an absolute resolved folder, automatically creates the configured folder and any missing nested target folders, rejects absolute or `..` target paths, and refuses to overwrite existing files unless `overwrite=true`.
- `rag_ingest_generated_document` writes generated Markdown/text content into a write-enabled exposed RAG.
- If `rag_ingest_generated_document` does not receive a `rag_id`, it uses the default ingest target if configured, otherwise the only write-enabled exposed RAG when exactly one exists.
- Generated documents are stored under `documents/generated_sources` before normal ingestion and indexing.
- Generated documents receive provenance metadata including generated timestamp and generated document ID.
- There is no active RAG working set yet; search results are not pinned into future prompts unless the model summarizes them into the conversation or the passive RAG injector retrieves them later.
- The active write-file tool can export managed originals and extracted Markdown/text into the configured project folder. A richer export history/provenance screen is still pending.
- There is no active tool for ingesting an existing project file or web-discovered document yet.

#### UI And Diagnostics

- The RAG Service Manager is launched from the main window's `RAG Service` button.
- The manager displays libraries, details, stats, binding state, and status messages.
- The manager supports Add, Edit, Remove, Attach Read-Only, Attach Read/Write, Detach, Install Tools, Image Ingest Settings, Ingest Files, Ingest Folder, Rebuild Database, Browse Docs, Reindex Doc, Delete Doc, Search, and Close.
- File and folder ingestion run on background threads and post completion back to the UI.
- Rebuild posts progress updates back to the UI and updates the progress bar.
- Browse Docs shows document IDs, titles, source URIs, stored/extracted paths, metadata, chunk count, and embedding count.
- Reindex and Delete currently ask for a document ID from the user.
- Delete can remove database rows only or database rows plus managed original/extracted files.
- The extraction tool screen lists installed/missing tools, recommended status, purpose, notes, and install command.
- The extraction tool installer opens a visible command window and uses `winget` where available.
- The Image Ingest Settings screen is system-wide rather than per-project or per-RAG. It provides CPU Tesseract OCR mode, GPU PaddleOCR mode, full vision-language mode, Tesseract/Paddle/Ollama installer buttons, Ollama vision-model pull, provider/model/base-URL fields, prompt editing, and diagnostics.
- The RAG library editor has one screen for name, description, storage location, enabled state, file limits, chunking, embedding provider/base/model/dimensions, vector backend, segmentation settings, runtime diagnostics, runtime control buttons, install buttons, test embedding, and recent log output.
- The embedding runtime log is written to `data/rag_embedding_runtime.log`.
- The image ingest runtime/settings log is written to `data/rag_image_ingest_runtime.log`.
- There is not yet a dedicated RAG injected-context viewer or RAG working-set diagnostics screen.

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
  - Export/write-file permission.
  - Write-file folder path template.
  - Tool exposure flag for model-callable RAG tools.
  - Default ingest target flag.
  - Retrieval priority.
  - Per-binding max chunks.
  - Default minimum and maximum confidence thresholds.
- `RagDocumentRecord`
  - Document ID, RAG ID, display name, original source URI, source type.
  - Stored original relative path and extracted relative path.
  - Content hash, file size, MIME value, imported timestamp, indexed timestamp.
  - Free-form metadata JSON.
- `RagChunkRecord`
  - Chunk ID, document ID, chunk index, text, offsets, token estimate, metadata JSON.
- Diagnostic and workflow types
  - Library stats, document previews, ingestion progress, embedding runtime status, embedding test results, extraction tool status, and query results.
- `RagImageIngestSettings`
  - System-wide enabled flag.
  - Mode: `tesseract_cpu`, `paddle_ocr_gpu`, or `vision_language_gpu`.
  - Tesseract language.
  - PaddleOCR Python command and language.
  - Vision provider, base URL, model, and description prompt.
  - Flags controlling whether OCR text and visual descriptions are included in extracted Markdown.
- `RagImageIngestRuntimeStatus`
  - Current mode and readiness message.
  - Tesseract, Python, PaddleOCR, Ollama, and vision-endpoint status.
  - Diagnostics log path and recent log tail.

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
  - `documents/generated_sources` for generated documents created by the active RAG write tool before indexing.
  - `indexes` for future external vector index files.
- System-wide image ingest settings are stored at `data/rag_image_ingest_settings.json`.
- Image ingest diagnostics are written to `data/rag_image_ingest_runtime.log`.
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
- Supported standalone image extensions currently include PNG, JPG/JPEG, BMP, TIF/TIFF, and WebP.
- The import path computes a content hash and can skip unchanged files during normal ingestion.
- Existing chunks for a document are deleted and replaced when a source is re-ingested.
- If the library uses managed storage, source files are copied into `documents/original`.
- Extracted rich text and image-derived Markdown are written into `documents/extracted`.
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
- OCR for scanned/image-only PDFs is not yet implemented. Standalone image files use the separate image ingest pipeline below.

### 18.5A Image Ingestion And Vision Extraction

- Image ingestion is implemented as a system-wide setting opened from the RAG Service Manager using the `Image Ingest Settings` button beside `Install Tools`.
- Image settings are not per-project and not per-RAG. All RAG libraries use the same image ingest pipeline when image files are imported.
- Default mode is CPU-safe Tesseract OCR.
- GPU OCR mode attempts PaddleOCR through a configured Python command and falls back to Tesseract OCR when PaddleOCR is unavailable or fails.
- Full vision mode attempts OCR plus an Ollama vision-language description using the configured base URL, model, and prompt.
- The default configured vision model is `qwen2.5vl:7b`, but the field is editable so the user can select an available Qwen2.5-VL, InternVL-style, or other Ollama-served vision model.
- The settings window includes installer buttons for Tesseract, PaddleOCR, and Ollama, plus a model-pull button that runs `ollama pull <configured model>`.
- Image originals are preserved under the same managed original-document storage path as other ingested files.
- Image-derived Markdown contains metadata, warnings, optional visual description, and OCR text sections.
- The extracted Markdown is chunked, embedded, searched, rebuilt, exported, and retrieved through the same RAG document pipeline as rich documents.
- Import preview checks whether the selected image pipeline has at least one viable path before marking image files as ready.
- Ingested image metadata records the extractor path used and marks the extracted content as Markdown.
- The first VLM integration path uses Ollama `/api/generate` with image input. LM Studio/OpenAI-compatible vision providers are not wired in yet.
- The app does not yet automatically start an Ollama vision runtime from the image settings screen; the endpoint must be running for full vision descriptions to succeed.

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
- The chat request builder also exposes built-in RAG tools when the selected model supports tools and the active project has tool-enabled readable RAG bindings.
- RAG context injection currently uses a chunk count limit but does not yet apply token-budget-aware trimming.

### 18.8A Active RAG Tool Mode

The current RAG implementation supports both passive and active usage.

Passive mode automatically queries readable project RAG libraries at send time and injects matching chunks into the system prompt.

Active mode exposes built-in RAG tools to the model through the same OpenAI-compatible `tools` array used for MCP tools. A tool is exposed only when the selected model supports tools and the current project binding has Enable, Read, and Tool checked. Write actions additionally require the binding's Write permission.

In active RAG tool mode, the model can decide when to search, which RAG library to search, whether the confidence is good enough, whether more document text is needed, and whether generated content should be persisted into a write-enabled RAG. Ongoing selected-reference context and abandonment are still future work.

Both modes should remain available:

- Passive auto-injection for simple projects where RAG should always provide background context.
- Active RAG tool mode for agent workflows, deep searches, research tasks, document retrieval, and selective context building.

#### 18.8A.1 Model-Visible RAG Library Descriptions

Each RAG library's name and description is visible to the model through `rag_list_libraries` when RAG tools are exposed.

The description is important because it tells the model what the RAG is for and how it should be used.

Examples:

- "Internal HR Documents: Use for company policy, benefits, onboarding, and employee-process questions."
- "Legal Reference Library: Use for contract templates, prior legal research, regulatory summaries, and source document retrieval."
- "Project Generated Research: Use as a writable project research memory for web-found documents and generated summaries."

The model-visible RAG listing currently includes:

- RAG library ID.
- Name.
- Description.
- Whether it is enabled for the active project.
- Read permission.
- Write permission.
- Delete permission.
- Export/write-file permission.
- Write-file folder path template.
- Default ingest target flag.
- Retrieval priority.
- Default maximum chunks.
- Default minimum and maximum confidence thresholds.
- Storage/source policy summary, such as managed-copy or reference-in-place.
- Embedding provider, model, base URL, and vector backend.

Future listing fields should include metadata schemas and configured retrieval filters.

#### 18.8A.2 Implemented And Planned RAG Tool Functions

The active RAG tool interface currently exposes:

```text
rag_list_libraries()
```

Returns project-attached RAG libraries that are enabled, readable, and exposed as tools. The response includes model-visible descriptions, read/write/delete/export/default-ingest permissions, retrieval priority, max chunks, default confidence window, configured write-file folder template, storage policy, and embedding/vector settings.

```text
rag_search(query, rag_ids?, max_results?, candidate_limit?, min_confidence?, max_confidence?, retrieval_mode?, include_text?)
```

Searches one or more RAG libraries.

Results are returned ordered by score/confidence from highest to lowest.

The implemented tool supports these result-selection strategies:

- Return the top N results regardless of confidence.
- Return only results above a minimum confidence threshold.
- Return only results between a confidence range, such as 0.25 to 0.45, for needle-in-a-haystack exploration.
- Return at most N results from that confidence window.
- Search one RAG library or all readable project-attached RAG libraries.
- Increase `candidate_limit` before threshold filtering for broader searches.
- Optionally include chunk text or metadata-only results.
- Use each RAG binding's default confidence threshold when a request omits `min_confidence` or `max_confidence`.
- Return per-RAG confidence windows in the tool response for debugging and repeatability.

The current backend always executes the existing hybrid/fallback retrieval path and reports the actual `retrieval_method` per result. Requested `retrieval_mode` is accepted as intent, but vector-only, keyword-only, and reranked execution modes are not separate backends yet.

If no result reaches the requested threshold, the tool should return a clear "no sufficiently relevant results" response rather than injecting weak results automatically.

The model may then decide to:

- Lower the threshold.
- Ask for the top five or top ten results regardless of score.
- Search a different RAG.
- Search a lower-confidence band.
- Abandon the RAG search path.

```text
rag_get_document(rag_id, document_id, include_text?, max_chars?)
```

Retrieves document metadata, extracted text, managed original path information, source URI, MIME type, stored relative paths, provenance metadata, and truncation status. Segmented extracted documents are reassembled from their segment manifest up to the requested character limit.

```text
rag_write_document_to_drive(rag_id, document_id, version, target_relative_path?, target_folder_relative_path?, target_file_name?, overwrite?)
```

Writes a RAG document directly to the configured write-file folder for that project binding. This does not call any filesystem MCP server or external tool.

Supported `version` values:

- `original` copies the managed original file when available, or the file-backed original path for reference-in-place libraries.
- `extracted_markdown` writes the extracted Markdown/text representation created during ingestion. Segmented rich-document extractions are reassembled before writing.

The tool enforces these safety rules:

- The RAG binding must be Enabled, Read, Tool, and Write file/export enabled.
- The configured folder path must expand from project variable placeholders into an absolute path.
- The caller can provide either a full relative target file path or a relative target folder plus optional file name.
- The target path and target folder are relative to the configured folder.
- Absolute target paths and `..` path traversal are rejected.
- Missing configured folders and missing nested target folders are created automatically by the tool; the model does not need to call any separate folder creation tool.
- Existing files are not overwritten unless `overwrite=true`.
- The tool response includes the resolved folder, final target path, relative path, source version, document title, whether missing directories were created, and byte count.

```text
rag_ingest_generated_document(rag_id?, title, content, source_uri?, metadata?)
```

Allows a model to create a Markdown/text document and ingest it into a write-enabled RAG library. If `rag_id` is omitted, the tool uses the default ingest target when configured, otherwise the only write-enabled exposed RAG if there is exactly one. Generated documents are first written under `documents/generated_sources`, receive provenance metadata, and are then passed through the normal ingestion/indexing pipeline so rebuild can use the saved generated original.

The remaining planned active RAG functions are:

```text
rag_add_to_context(result_ids, reason?, expiry?)
```

Adds selected search results into a per-chat RAG working set.

The selected results should be injected into later prompts until removed, expired, superseded, or compressed into the chat context.

```text
rag_abandon_context(result_ids? or all?)
```

Removes selected RAG references from the working set when the model decides they are irrelevant or no longer useful.

```text
rag_ingest_project_file(rag_id, project_relative_path, metadata?)
```

Adds an existing file from the active project folder into a write-enabled RAG.

This supports workflows where another tool creates the file first, then RAG indexes it.

```text
rag_ingest_web_document(rag_id, source_url, title, content or captured_file, metadata?)
```

Future workflow for adding web-sourced documents discovered during research.

If the model uses web search or a browser/search MCP tool to find relevant documents, it should be able to preserve those documents or summaries into a write-enabled RAG.

The stored metadata should include:

- Original URL.
- Retrieval timestamp.
- Source title.
- Content hash.
- Tool/source that found the document.
- Project ID and chat ID, when applicable.
- Model-generated summary, if available.
- User confirmation status, if confirmation is required.

#### 18.8A.3 RAG Working Set

Active RAG search should not automatically inject every search result into future prompts.

Instead, selected results should be stored in a per-chat RAG working set.

Each working-set item should include:

- RAG ID and RAG name.
- Document ID.
- Chunk ID or document-level reference.
- Source path or URL.
- Confidence score.
- Retrieval method.
- Selected text.
- Metadata JSON.
- Reason selected.
- Selected by model/user/system.
- Created timestamp.
- Expiry policy.
- Abandoned flag and abandonment reason.

The request builder should inject the active working set into the context window with token-budget limits.

Context compression should be able to fold selected RAG working-set facts into compressed context when appropriate, while preserving source labels.

#### 18.8A.4 Writable RAG Safety And Provenance

Write-enabled RAG libraries are powerful and should be treated as persistent project memory.

Any tool that writes generated or web-derived content into a RAG must preserve provenance and avoid silently polluting high-trust libraries.

Recommended controls:

- Project binding controls whether a RAG is readable, writable, deletable, exportable, and a default ingest target.
- RAG descriptions should tell the model whether the library is authoritative, experimental, generated, temporary, legal/HR-specific, project-specific, or archival.
- Generated content should default to a generated/research RAG rather than an authoritative source RAG unless explicitly configured.
- The UI should expose pending or recent RAG writes for diagnostics.
- For higher-safety modes, generated/web ingestion should require user confirmation before indexing.

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
  - Edit system-wide Image Ingest Settings.
  - Ingest files.
  - Ingest folder.
  - Rebuild database.
  - Browse indexed documents.
  - Reindex a document by ID.
  - Delete a document by ID.
  - Search selected library or active project RAG context.
  - View progress and status messages.
- Project Settings supports enabling a RAG binding, setting read/write/delete/write-file access, configuring the write-file folder path template, marking that binding for active RAG tool exposure, selecting the default ingest target, and editing retrieval priority, max chunks, and default confidence thresholds.
- When the selected model supports tools, the chat execution loop exposes active RAG tools beside MCP tools and routes RAG tool results back through the normal tool-call loop.
- The manager displays library details, stats, binding state, and rebuild-required warnings.
- The rebuild workflow asks for confirmation, clears database content, re-ingests saved originals, and updates the progress bar.
- The folder ingest workflow asks whether ingestion should recurse into child folders.
- The document browse and search workflows expose provenance and metadata for diagnostics.
- RAG tool calls are shown in the existing tool trace pane as `RAG / <tool name>`.

### 18.10 Reliability Fixes Already Applied

- PDF and embedding text paths now sanitize invalid UTF-8 and problematic control bytes before JSON serialization.
- Working rich document extraction writes sanitized extracted text.
- Standalone image ingestion now converts supported images into Markdown through the selected system-wide OCR/vision pipeline while preserving the original image.
- Rebuild no longer depends on the previous chunk/index state. It clears rows and re-ingests saved originals.
- Large extracted documents can be segmented before chunking to avoid processing one huge extracted file at once.
- The Poppler `pdftotext.exe` path is now a visible diagnostic/install workflow instead of a hidden dependency.

### 18.11 Partially Implemented Or Important Gaps

The following gaps are current as of April 13, 2026. Items are grouped by risk rather than by original phase.

#### 18.11.1 Retrieval And Context Gaps

- A production vector backend is not implemented yet. The `vector_backend` setting exists, but only the SQLite vector BLOB scan is active.
- HNSWlib, FAISS, Qdrant, sqlite-vss, and ChromaDB integrations are not yet implemented.
- A per-chat RAG working set for selected/pinned/abandoned references is not implemented.
- Passive RAG context injection and active RAG tool mode can both be enabled at the same time. There is no per-project/per-RAG policy yet to choose passive-only, active-only, both, or disabled retrieval behavior.
- RAG context injection does not yet show a dedicated "what was injected" diagnostic screen.
- RAG context injection does not yet integrate with the configured model context window for token-budget-aware trimming.
- Advanced metadata fields, tags, and metadata filters are not implemented.
- Query reranking is not implemented.
- Query diversity controls are not implemented.
- Tool-driven RAG search with minimum confidence, maximum confidence, top-N fallback, lower-confidence-band search, and per-call result windows is implemented for active RAG tool mode, but it does not yet support metadata filters, diversity controls, independent vector-only/keyword-only execution, or reranking.

#### 18.11.2 Ingestion And Index Maintenance Gaps

- There is no persistent ingestion job queue with job IDs, pause, resume, cancel, retry, or crash recovery.
- `ingestion_events` exists in SQLite but is not yet used as a complete diagnostic/job table.
- Incremental indexing is file-level only. There is no per-chunk diffing or partial re-embedding.
- There is no folder watcher, scheduled background reindexing, or deleted-source detector.
- Folder ingest preview handles permission-denied skips, but actual folder ingestion still needs stronger skip/continue behavior for inaccessible files.
- Include/exclude glob filters are not yet implemented.
- Hidden/system file policy is not yet exposed.
- Paste-text ingestion, chat-message ingestion, summary ingestion, attachment ingestion, and MCP-result ingestion are not implemented.
- RAG indexing of chat messages and condensed summaries is not implemented.

#### 18.11.3 Rich Document, Image, And Media Gaps

- OCR for scanned/image PDFs is not implemented.
- Audio transcription ingestion is not implemented.
- Image ingestion is implemented for standalone image files, but the GPU path is still an early adapter: PaddleOCR installation/runtime packaging, GPU detection, automatic model downloads, and non-Ollama VLM providers need hardening.
- Full image captioning/description currently depends on an already-running Ollama endpoint and a compatible local vision model.

#### 18.11.4 Write, Export, And Safety Gaps

- RAG document write-to-drive/export is implemented for original files and extracted Markdown/text through `rag_write_document_to_drive`; export history and a richer provenance/audit UI remain pending.
- Generated-document ingestion through model-callable RAG tools is implemented for write-enabled exposed RAGs. Project-file ingestion and web-document ingestion remain future work.
- Generated-document ingestion through the active tool does not yet have an optional user-confirmation workflow or a recent-write review screen.
- Model-visible RAG descriptions are exposed through `rag_list_libraries` for agent planning.
- Per-project RAG bindings now expose read/write/delete/write-file/default-ingest/priority/max-chunks/confidence-threshold values in Project Settings, but RAG Service Manager quick attach/detach remains a simpler shortcut workflow.
- Delete permission exists in the model and Project Settings UI, but document deletion currently follows the manager workflow rather than project binding policy.

#### 18.11.5 Packaging, Security, And Service Boundary Gaps

- The app still runs the RAG engine in-process. It has not been extracted to a plugin, DLL, separate `rag_service.exe`, or MCP server.
- API key encryption and remote embedding privacy warnings are not complete.
- Storage folder migration for an existing RAG library is not implemented. The edit screen treats storage location as effectively fixed after creation.

### 18.12 Recommended Next Implementation Order

1. Add a per-binding retrieval mode policy so each RAG can be passive-only, active-tool-only, both, or disabled for retrieval.
2. Add a per-chat RAG working set with selected-reference context pinning, abandonment, expiry, and diagnostics.
3. Add token-budget-aware RAG context construction using the selected model's optional `context_window` value.
4. Add "injected context" and "RAG working set" diagnostics so the user can see exactly which chunks were sent to the model for a chat turn and which references were selected by the model.
5. Add write-file/export history and provenance diagnostics for documents written from RAG to drive.
6. Add project-file and web-document ingestion tools for write-enabled RAG libraries.
7. Add recent-write diagnostics and optional user-confirmation policy for generated/web RAG writes.
8. Add a persistent ingestion job table and diagnostics screen using the existing `ingestion_events` schema.
9. Add include/exclude filters, hidden/system file handling, and stronger inaccessible-file handling for real folder ingestion.
10. Add metadata filters, diversity controls, independent vector/keyword modes, and optional reranking to active `rag_search`.
11. Add the first production vector backend, with HNSWlib as the likely local-first candidate and FAISS/Qdrant reserved for higher-volume or optional advanced paths.
12. Add chat/message/summary indexing after the library ingestion and diagnostics workflow is stable.
13. Harden image ingestion: GPU detection, PaddleOCR package/runtime validation, Ollama vision runtime start/stop, additional VLM providers, model availability checks, and batch/progress handling for many images.
14. Add OCR for scanned/image-only PDFs after the standalone image pipeline and text-PDF pipeline are stable.

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
- Which local-first vision provider should become the preferred production image understanding path: Ollama-served Qwen2.5-VL, InternVL-style models, LM Studio/OpenAI-compatible vision endpoints, or an embedded Python/transformers sidecar?
- Should image ingestion store a separate structured vision metadata record in addition to the extracted Markdown, especially for charts, tables, and diagrams?
- Should write-enabled RAG tool ingestion require user confirmation by default for authoritative libraries, while allowing automatic writes for scratch/research libraries?

---

## 20. Next Steps Toward The Full RAG Service

This section is the actionable implementation roadmap from the current proof-of-concept toward a full local RAG service. The earlier sections describe the target requirements and current implementation status; this section identifies what should be done next, in priority order.

### 20.1 Critical Next Steps

These are the most important items to implement next because they affect correctness, model behavior, diagnostics, and the ability to trust RAG output.

1. Implement per-binding retrieval mode policy.
   - Add project/RAG binding settings for passive-only, active-tool-only, both, or disabled.
   - Use this policy when building passive context and active tool definitions.
   - This prevents duplicate retrieval behavior and lets projects decide whether RAG should be automatic background context or model-controlled search.

2. Implement a per-chat RAG working set.
   - Allow selected RAG search results or document references to be retained across later prompts.
   - Track selected, abandoned, expired, and compressed references.
   - Add model-callable tools such as `rag_add_to_context` and `rag_abandon_context`.
   - This is the missing bridge between active RAG search and stable context over a multi-turn task.

3. Add token-budget-aware RAG context construction.
   - Use the selected model's optional `context_window` value when deciding how much RAG context can be injected.
   - Budget for system prompt, project instructions, MCP context, tool definitions, compressed context, chat history, and retrieved RAG content.
   - Trim by priority, confidence, recency, and working-set selection instead of only fixed chunk counts.

4. Add RAG context diagnostics.
   - Create a dedicated view showing exactly which RAG chunks were retrieved, selected, filtered, injected, skipped, or trimmed for a chat turn.
   - Include RAG ID/name, document ID/title, chunk ID, score, retrieval method, metadata, and final injected character/token estimate.
   - This is critical for debugging why the model did or did not know something.

5. Add write/export audit history.
   - Record every `rag_write_document_to_drive` and `rag_ingest_generated_document` event.
   - Show source document, target path, generated/source URI, model/tool caller, timestamp, overwrite status, and project/chat ID.
   - This is important before write-enabled RAGs become a normal workflow.

### 20.2 High-Priority Reliability Work

These items make ingestion safer and more repeatable for larger document sets.

1. Implement persistent ingestion jobs.
   - Use or extend the existing `ingestion_events` schema.
   - Add job IDs, per-file status, progress, errors, retry state, cancellation, and crash recovery.
   - Surface the job history in the RAG Service Manager.

2. Harden folder ingestion.
   - Add include/exclude glob filters.
   - Add hidden/system file policy.
   - Continue safely through permission-denied or locked files.
   - Preserve skipped-file diagnostics in the persistent job record.

3. Add project-file and web-document ingestion tools.
   - `rag_ingest_project_file` should ingest an existing file from the active project folder into a write-enabled RAG.
   - `rag_ingest_web_document` should store web-sourced documents or generated summaries with source URL, retrieval timestamp, tool provenance, and optional user confirmation.

4. Add recent-write review and optional confirmation.
   - Let authoritative RAGs require confirmation for generated/web writes.
   - Allow scratch/research RAGs to accept automatic model writes when configured.

### 20.3 Retrieval Quality Work

These items improve search result quality, especially as libraries grow.

1. Add metadata filters and user-defined tags.
   - Support filtering by document type, source folder, original path, ingest source, date, tags, author/source, and custom metadata fields.
   - Expose safe filter options through active RAG tools.

2. Add query diversity controls.
   - Avoid returning ten near-identical chunks from the same document when broader coverage would help.
   - Add per-document caps and optional diversity scoring.

3. Add independent retrieval modes.
   - Implement true keyword-only, vector-only, hybrid, and fallback modes rather than treating `retrieval_mode` as intent only.
   - Return the actual mode used for every result.

4. Add optional reranking.
   - Add a local or provider-backed reranker after candidate retrieval.
   - Keep reranking optional because it adds latency and may require another model/provider.

5. Add a production vector backend.
   - Keep SQLite vector BLOB scan as a simple fallback.
   - Add HNSWlib first if the goal is local, fast, low-cost vector search.
   - Keep FAISS/Qdrant as advanced options for larger or specialized deployments.

### 20.4 Rich Document And Image Work

These items expand ingestion quality for non-text sources.

1. Harden standalone image ingestion.
   - Add GPU detection and clearer CPU/GPU status.
   - Validate PaddleOCR package/runtime availability more accurately.
   - Add model availability checks for Ollama vision models.
   - Add batch progress for many images.
   - Add LM Studio/OpenAI-compatible vision endpoint support if needed.

2. Add scanned/image-only PDF OCR.
   - Detect PDFs where text extraction returns little or no text.
   - Render pages to images using Poppler or MuPDF.
   - Run the existing image OCR/vision pipeline per page.
   - Store page-level Markdown with page numbers and extraction metadata.

3. Add structured image metadata.
   - For charts, diagrams, and tables, store structured observations in metadata JSON in addition to Markdown.
   - Include axes, units, legend labels, table headers, detected text, and uncertainty notes when available.

4. Add audio transcription ingestion.
   - Treat audio as another extraction pipeline that generates Markdown/text plus provenance metadata.

### 20.5 Service Boundary And Packaging Work

These items prepare the RAG engine to become reusable outside the current app process.

1. Keep the in-process `RagService` API clean while adding features.
   - Avoid coupling retrieval, ingestion, and storage logic to Win32 UI classes.
   - Keep UI workflows as callers of the service API.

2. Decide the future external interface.
   - Candidate forms are DLL/plugin, sidecar `rag_service.exe`, MCP server, or a combination.
   - A sidecar or MCP server becomes more attractive once ingestion jobs and production vector indexing are heavier.

3. Add packaging and data-location cleanup.
   - Move runtime data out of the source tree for packaged builds.
   - Add migration rules for existing proof-of-concept data.
   - Add backup/export/import for RAG libraries and project bindings.

4. Add security hardening.
   - Add DPAPI or equivalent protection where secrets are stored.
   - Add explicit warnings for remote embedding/vision providers.
   - Add stronger write/delete permission enforcement in all UI and tool paths.

### 20.6 Recommended Immediate Sprint

The next implementation sprint should focus on these items in order:

1. Per-binding retrieval mode policy.
2. Per-chat RAG working set.
3. RAG injected-context and working-set diagnostics.
4. Token-budget-aware RAG context trimming.
5. Persistent ingestion job records.

These five items are more critical than adding more file types because they determine whether RAG behavior is understandable, controllable, and safe during real project chats.
