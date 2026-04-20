#pragma once

#include "storage.h"
#include "types.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct HnswHandle;  // defined in rag_service.cpp

struct RagMarkdownExtractionResult {
    bool success = false;
    bool processed_document = false;
    std::string markdown;
    std::string extractor_id;
    std::string mime_type;
    std::string error;
};

class RagService {
public:
    explicit RagService(AppStorage* storage);
    ~RagService();

    void EnsureInitialized() const;

    std::vector<RagLibraryConfig> ListLibraries() const;
    std::optional<RagLibraryConfig> GetLibrary(const std::string& rag_id) const;
    RagLibraryConfig CreateLibrary(const RagLibraryConfig& config);
    bool UpdateLibrary(const RagLibraryConfig& config, std::string* error);
    bool DeleteLibrary(const std::string& rag_id, std::string* error);
    // Registers an existing on-disk RAG library folder that is not yet in the registry.
    bool ReattachLibrary(const std::filesystem::path& library_path, std::string* error);
    // Exports a library to a compressed .rag series.  base_filename should not include
    // the extension or series number (e.g. "my_library" -> "my_library-001.rag").
    RagExportResult ExportLibrary(
        const std::string& rag_id,
        const std::filesystem::path& output_dir,
        const std::string& base_filename,
        std::function<void(int current, int total, const std::string& name)> progress = {}) const;
    // Imports a .rag series.  first_rag_file is the -001.rag file.  target_dir is
    // where to unpack the library; a sub-folder named after the library ID is created.
    RagImportResult ImportLibrary(
        const std::filesystem::path& first_rag_file,
        const std::filesystem::path& target_dir,
        std::function<void(int current, int total, const std::string& name)> progress = {});
    RagLibraryStats GetStats(const std::string& rag_id) const;
    std::vector<RagDocumentSummary> ListDocuments(const std::string& rag_id) const;
    std::optional<RagDocumentRecord> GetDocument(const std::string& rag_id, const std::string& document_id) const;
    std::string LoadDocumentText(const std::string& rag_id, const std::string& document_id, size_t max_chars, bool* truncated, std::string* error) const;
    RagImportPreview PreviewFiles(const std::string& rag_id, const std::vector<std::filesystem::path>& files) const;
    RagImportPreview PreviewFolder(const std::string& rag_id, const std::filesystem::path& folder, bool recursive) const;
    RagIngestionResult ReindexDocument(const std::string& rag_id, const std::string& document_id);
    bool DeleteDocument(const std::string& rag_id, const std::string& document_id, bool delete_managed_files, std::string* error);

    std::vector<ProjectRagBinding> LoadProjectBindings(const std::string& project_id) const;
    void SaveProjectBindings(const std::string& project_id, const std::vector<ProjectRagBinding>& bindings) const;
    bool UpsertProjectBinding(const std::string& project_id, const ProjectRagBinding& binding, std::string* error);
    bool RemoveProjectBinding(const std::string& project_id, const std::string& rag_id, std::string* error);

    RagIngestionResult IngestFiles(const std::string& rag_id, const std::vector<std::filesystem::path>& files);
    RagIngestionResult IngestFolder(const std::string& rag_id, const std::filesystem::path& folder, bool recursive);
    RagIngestionResult IngestGeneratedDocument(const std::string& rag_id, const std::string& title, const std::string& content, const std::string& metadata_json, const std::string& source_uri);
    RagIngestionResult RebuildLibrary(const std::string& rag_id, std::function<void(const RagProgressUpdate&)> progress = {});
    std::vector<RagQueryResult> QueryRag(const std::string& rag_id, const std::string& query, int max_results) const;
    std::vector<RagQueryResult> QueryProject(const std::string& project_id, const std::string& query, int global_max_results) const;
    // Builds a passive context block. max_token_budget > 0 trims chunks to fit the budget;
    // 0 means no budget (use global_max_results as the only limit).
    std::string BuildContextBlock(const std::string& project_id, const std::string& query, int global_max_results, int max_token_budget = 0) const;

    // Ingestion job tracking (persisted to rag root)
    IngestionJobRecord CreateIngestionJob(const std::string& rag_id, const std::string& kind, const std::string& source_description);
    void CompleteIngestionJob(const std::string& job_id, const RagIngestionResult& result);
    std::vector<IngestionJobRecord> ListIngestionJobs(int max_jobs = 200) const;
    RagEmbeddingRuntimeStatus GetEmbeddingRuntimeStatus(const RagLibraryConfig& library) const;
    RagEmbeddingRuntimeStatus StartEmbeddingRuntime(const RagLibraryConfig& library) const;
    RagEmbeddingRuntimeStatus StopEmbeddingRuntime(const RagLibraryConfig& library) const;
    RagEmbeddingRuntimeStatus LaunchEmbeddingRuntimeInstaller(const RagLibraryConfig& library) const;
    RagEmbeddingRuntimeStatus LaunchEmbeddingModelInstaller(const RagLibraryConfig& library) const;
    RagEmbeddingTestResult TestEmbeddingProvider(const RagLibraryConfig& library) const;
    std::vector<RagExtractionToolStatus> GetExtractionToolStatus() const;
    RagExtractionToolInstallResult LaunchExtractionToolInstaller(bool recommended_only) const;
    RagImageIngestSettings LoadImageIngestSettings() const;
    void SaveImageIngestSettings(const RagImageIngestSettings& settings) const;
    RagMarkdownExtractionResult ExtractFileToMarkdown(const std::filesystem::path& file) const;
    RagImageIngestRuntimeStatus GetImageIngestRuntimeStatus(const RagImageIngestSettings& settings) const;
    RagExtractionToolInstallResult LaunchImageIngestToolInstaller(const RagImageIngestSettings& settings, const std::string& tool_id) const;
    RagExtractionToolInstallResult LaunchImageVisionModelInstaller(const RagImageIngestSettings& settings) const;

    std::filesystem::path RagRoot() const;

private:
    struct RagCatalog {
        std::vector<RagDocumentRecord> documents;
        std::vector<RagChunkRecord> chunks;
    };

    std::filesystem::path LibraryPath(const std::string& rag_id) const;
    std::filesystem::path LibraryConfigPath(const std::string& rag_id) const;
    std::filesystem::path LibraryCatalogPath(const std::string& rag_id) const;
    std::filesystem::path RegistryPath() const;
    std::filesystem::path ProjectRagBindingsPath(const std::string& project_id) const;
    std::filesystem::path EmbeddingRuntimeLogPath() const;
    std::filesystem::path ImageIngestSettingsPath() const;
    std::filesystem::path ImageIngestLogPath() const;
    std::filesystem::path IngestionJobsPath() const;
    std::filesystem::path HnswIndexPath(const std::string& rag_id) const;
    std::filesystem::path HnswLabelsPath(const std::string& rag_id) const;

    HnswHandle* GetOrCreateHnswHandle(const std::string& rag_id, size_t dims) const;
    void        SaveHnswHandle(const std::string& rag_id) const;
    void        InvalidateHnswHandle(const std::string& rag_id) const;
    void        SyncHnswIndex(const std::string& rag_id, void* db,
                              const RagLibraryConfig& library,
                              void* provider) const;

    RagEmbeddingRuntimeStatus GetEmbeddingRuntimeStatusNoLock(const RagLibraryConfig& library) const;
    void EnsureEmbeddingRuntimeNoLock(const RagLibraryConfig& library) const;
    void AppendEmbeddingRuntimeLogNoLock(const std::string& message) const;
    std::string ReadEmbeddingRuntimeLogTailNoLock(size_t max_bytes = 12000) const;
    RagImageIngestSettings LoadImageIngestSettingsNoLock() const;
    void AppendImageIngestLogNoLock(const std::string& message) const;
    std::string ReadImageIngestLogTailNoLock(size_t max_bytes = 12000) const;
    void ShutdownManagedEmbeddingRuntimes() const;
    void ShutdownManagedImageIngestRuntimes() const;
    void EnsureImageVisionRuntimesNoLock(const RagImageIngestSettings& settings) const;
    int ManagedImageOllamaProcessCountNoLock() const;

    std::vector<std::pair<std::string, std::filesystem::path>> LoadRegistryNoLock() const;
    void SaveRegistryNoLock(const std::vector<std::pair<std::string, std::filesystem::path>>& entries) const;
    void UpsertRegistryNoLock(const std::string& rag_id, const std::filesystem::path& path) const;
    void RemoveRegistryNoLock(const std::string& rag_id) const;

    RagCatalog LoadCatalogNoLock(const std::string& rag_id) const;
    void SaveCatalogNoLock(const std::string& rag_id, const RagCatalog& catalog) const;

    AppStorage* storage_ = nullptr;
    mutable std::mutex mutex_;
    mutable void* started_ollama_process_ = nullptr;
    mutable unsigned long started_ollama_process_id_ = 0;
    mutable std::unordered_map<std::string, void*> started_image_ollama_processes_;
    mutable std::unordered_map<std::string, unsigned long> started_image_ollama_process_ids_;
    // Per-library HNSW index cache (rag_id -> handle).  Lives only for the
    // duration of the process; saved to disk after each mutation.
    mutable std::unordered_map<std::string, std::unique_ptr<HnswHandle>> hnsw_cache_;
};
