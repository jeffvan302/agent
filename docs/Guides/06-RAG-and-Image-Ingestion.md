# RAG and Image Ingestion

## RAG Service Overview

Click `RAG Service` to manage reusable searchable libraries. A library has its own managed storage and index, then is attached to projects with read/write/tool permissions.

The RAG manager actions are:

| Button | Purpose |
| --- | --- |
| `Add RAG` / `Edit` / `Remove` | Manage library definitions. |
| `Attach Read` / `Attach RW` / `Detach` | Grant or remove the active project's base binding. |
| `Install Tools` | Inspect and install recommended extraction tools. |
| `Image Ingest Settings` | Configure system-wide image extraction behavior. |
| `View Jobs` | Inspect recent ingestion/rebuild job results and errors. |
| `Reattach` | Register an existing on-disk RAG folder containing `rag.json`. |
| `Export RAG` / `Import RAG` | Transfer a library as `.rag` archive series files. |
| `Ingest Files` / `Ingest Folder` | Add supported sources to the selected library. |
| `Rebuild DB` | Clear/re-ingest saved originals to rebuild the database/index. |
| `Browse Docs` / `Reindex Doc` / `Delete Doc` | Inspect and maintain individual indexed documents. |
| `Search` | Test retrieval from the selected library. |

## Create a RAG Library

1. Click `RAG Service`, then `Add RAG`.
2. Enter a name and useful description.
3. Choose a storage parent folder. A dedicated RAG subfolder is created inside it.
4. Choose chunking and file-size settings.
5. Configure embeddings, or leave `Embedding Provider` as `None` for non-vector behavior where suitable.
6. Check/test the embedding runtime if embeddings are selected.
7. Check `Enabled`, then click `Add`.
8. Select a project before using `Attach Read` or `Attach RW`.

### Library fields

| Field | Description |
| --- | --- |
| `Name` | Library display name and identity in RAG tool context. |
| `Description` | Explains its contents so a model/operator can select the correct RAG. |
| `Storage Parent Folder` | Chosen at creation; for an existing library, moving storage is not performed by editing this field. |
| `Chunk Size (characters)` | Chunk target; must be at least 500. |
| `Chunk Overlap (characters)` | Overlap between chunks; must be non-negative and less than chunk size. |
| `Default Max Retrieved Chunks` | Default result count, at least 1. |
| `Max File Size (MB)` | Maximum ingested input size, at least 1 MB. |
| `Embedding Provider` | `None`, `Ollama`, or `LM Studio`. |
| `Embedding Model` | Model identifier required for Ollama or LM Studio embeddings. |
| `Embedding Dimensions` | Expected vector size; `0` permits unspecified configuration. |
| `Vector Backend` | Defaults to `sqlite_vector_scan`. |
| `Embedding Base URL` | Ollama defaults to `http://localhost:11434`; LM Studio defaults to `http://localhost:1234/v1`. |
| `Split large extracted Markdown/text into segment files` | Persists large extracted content as segments before chunking/indexing. |
| `Split Threshold (MB)` | Size at which extracted content is split. |
| `Segment Size (MB)` | Target size of each extracted segment. |
| `Segment Overlap (chars)` | Text overlap between segment files. |
| `Enabled` | Allows the RAG library to be used. |

### Embedding runtime actions

The library editor provides `Check`, `Test`, `Start`, `Stop`, `Install`, `Install Embed Text Model` and a `Runtime Diagnostics Log`.

- For Ollama, `Start`, `Stop`, `Install` and model installation help operate an app-managed runtime.
- `Test` verifies an actual embedding call and displays provider/model/dimensions/elapsed time.
- LM Studio is configured by endpoint/model and can be tested, but is not started/installed by these Ollama-specific controls.

## Attach a Library to a Project

The quick attachment buttons create a base binding:

| Action | Initial effect |
| --- | --- |
| `Attach Read` | Attaches enabled/read-only behavior with normal retrieval defaults. |
| `Attach RW` | Attaches enabled/read/write and makes it a default ingest candidate. |

For full runtime permissions, open the selected project's `Project Settings` and configure `Enabled`, `Read`, `Write`, `Tool`, `Delete`, `Write file`, `Default ingest`, priority, chunk/confidence limits, destination template and retrieval mode.

Active RAG tool access requires the library to be selected for a project with `Enabled`, `Read` and `Tool`.

## Ingest Files

1. Select a RAG library.
2. If a project is active, ensure the library is attached with write permission.
3. Click `Ingest Files`.
4. Select supported files.
5. Review the import preview listing supported/skipped files.
6. Confirm the ingestion.
7. Review the result and `View Jobs` if errors occur.

Supported picker types include text and code formats (`.txt`, `.md`, `.json`, `.csv`, `.log`, `.xml`, `.cpp`, `.h`, `.cs`, `.js`, `.ts`, `.py`, `.ps1`, `.yaml`, `.html`, `.sql` and others), Office/document inputs (`.docx`, `.docm`, `.xlsx`, `.xlsm`, `.pdf`), and image inputs (`.png`, `.jpg`, `.jpeg`, `.bmp`, `.tif`, `.tiff`, `.webp`).

## Ingest a Folder

1. Select the library and click `Ingest Folder`.
2. Choose the source directory.
3. Answer whether subfolders should be included.
4. Review the preview and confirm.
5. Monitor the job and inspect skipped files/errors.

Only supported files within the selected scope are ingested. This is the preferred way to populate a RAG from a project documentation tree.

## Browse, Reindex, Delete and Rebuild

| Workflow | Operation |
| --- | --- |
| Inspect documents | Click `Browse Docs` to see document IDs, stored names, chunk/embedding counts and import information. |
| Reindex a document | Click `Reindex Doc`, paste the Document ID from `Browse Docs`, and allow the app to regenerate its indexed content. |
| Delete a document | Click `Delete Doc`, paste its Document ID, then choose whether to also remove managed original/extracted files. |
| Rebuild a library | Click `Rebuild DB` to clear document/chunk tables and re-ingest its saved originals. Useful after embedding or extraction changes. |

## Export a RAG

1. Select the library.
2. Click `Export RAG`.
3. Select the destination directory.
4. Confirm the generated base filename.
5. Wait for export completion and keep every produced series file.

Exports are compressed series files named like `<library-name>-001.rag`, `<library-name>-002.rag`, and so on. The completion results show series count and compressed/uncompressed totals.

## Import a RAG

1. Click `Import RAG`.
2. Select the first archive series file, specifically `*-001.rag`.
3. Select a target folder where a RAG library subfolder may be extracted.
4. Confirm the operation.
5. After import, select a project and use `Attach Read`, `Attach RW`, or `Project Settings` to grant access.

Import registers the library but does not automatically attach it to projects.

## Built-In Documentation RAG

`Setup System` includes the exported `Agent App Documentation` library as an executable resource. During setup:

1. The app checks for a registered library named `Agent App Documentation`.
2. If the library is absent, the app temporarily extracts its embedded `Agent_App_Documentation-001.rag` resource and imports it under `.data\.app_rag`.
3. If the library is already registered, setup retains it and does not import a second copy.
4. Setup does not create any project binding for the library. Models cannot access it unless a project is explicitly configured to do so.

To provide an example project with access, include a read-only binding in that project's `project_rag.json` inside `.config.zip`, using the internal RAG ID stored in the exported archive. For a project configured after setup, select `Agent App Documentation` in `RAG Service` and click `Attach Read`; enable `Tool` in `Project Settings` only when that project's model should search or read the guide documents.

To ship updated guide content, replace `docs\Guides\Agent_App_Documentation-001.rag` and rebuild the executable. An installation that already has `Agent App Documentation` registered is intentionally not overwritten by a later setup run; remove or replace the installed RAG as an explicit maintenance action when updating an existing deployment.

## Image Ingest Settings

`Image Ingest Settings` is system-wide: it affects new image processing for all RAG libraries.

### Select a processing mode

| Mode | Behavior |
| --- | --- |
| `CPU default: Tesseract OCR only` | Extracts visible text using Tesseract; least infrastructure. |
| `GPU OCR: PaddleOCR, with Tesseract fallback` | Uses PaddleOCR when available and falls back to Tesseract. |
| `Full vision: OCR plus model-generated image description` | Adds a vision-model description suitable for diagrams, charts and visual content. |

Use `Enable image ingestion for all RAG libraries` to turn the pipeline on or off.

### Image settings fields

| Field | Purpose |
| --- | --- |
| `Tesseract language` | OCR language code, default `eng`. |
| `PaddleOCR Python command` | Python command used for PaddleOCR, default `python`. |
| `PaddleOCR language` | PaddleOCR language value, default `en`. |
| `Vision provider` | `None`, `Ollama`, or `Provider`. |
| `Provider vision model` | A normal configured provider/model marked `Vision capable`; enabled when `Provider` is selected. |
| `Vision host / base URL` | Ollama vision service host/base URL. |
| `Vision model` | Ollama vision model, default `qwen2.5vl:7b`; clicking the label opens model search. |
| `Ollama worker instances` | Number of local Ollama vision endpoints to use. |
| `Ollama starting port` | Initial endpoint port, default `11434`. |
| `Start Ollama locally when needed` | Allows the app to launch needed Ollama endpoints. |
| `Vision description prompt` | Instructions controlling image-to-Markdown description output. |
| `Include OCR text in extracted Markdown` | Persists recognized text in the indexed Markdown. |
| `Include visual description in extracted Markdown when full vision mode is selected` | Persists the model's image interpretation. |

### Configure provider-backed vision

Use this for cloud, OAuth or Agent HTTPS remote-worker vision models:

1. In `Providers`, configure a model and check `Vision capable`.
2. Open `RAG Service` -> `Image Ingest Settings`.
3. Select full vision mode.
4. Set `Vision provider` to `Provider`.
5. Select the provider/model from `Provider vision model`.
6. Click `Check Status`, then save.

Remote Agent JSON is no longer configured directly inside Image Ingest Settings. Add a remote worker as a normal Provider and choose its vision-capable model through this workflow.

### Configure local Ollama vision

1. Select full vision mode and set `Vision provider` to `Ollama`.
2. Set the model and endpoint/port settings.
3. Use `Install Ollama` if missing.
4. Use `Pull Vision Model` to install the selected model.
5. Optionally enable `Start Ollama locally when needed`.
6. Click `Check Status` and review diagnostics before ingesting images.

### Extraction tool installation

Use `Check Status`, `Install Tesseract`, `Install PaddleOCR`, `Install Ollama` and `Pull Vision Model`. These launch visible installation/runtime work and update diagnostics. RAG document extraction also relies on tools such as Poppler, Pandoc or LibreOffice depending on input type; use `Install Tools` and review its reported status.

## Storage

| Item | Location |
| --- | --- |
| RAG library registry/libraries | `.data/rag_libraries/` or configured storage location |
| Per-library definition | The library directory's `rag.json` |
| Project RAG permissions | `.config/projects/<project_id>/project_rag.json` |
| Image ingestion settings | `.config/rag/rag_image_ingest_settings.json` |
| Image diagnostics log | Under the app RAG configuration/log location reported in `Check Status` |
