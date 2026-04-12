#pragma once

#include "storage.h"
#include "types.h"

#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
    RagLibraryStats GetStats(const std::string& rag_id) const;
    std::vector<RagDocumentSummary> ListDocuments(const std::string& rag_id) const;
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
    RagIngestionResult RebuildLibrary(const std::string& rag_id, std::function<void(const RagProgressUpdate&)> progress = {});
    std::vector<RagQueryResult> QueryRag(const std::string& rag_id, const std::string& query, int max_results) const;
    std::vector<RagQueryResult> QueryProject(const std::string& project_id, const std::string& query, int global_max_results) const;
    std::string BuildContextBlock(const std::string& project_id, const std::string& query, int global_max_results) const;
    RagEmbeddingRuntimeStatus GetEmbeddingRuntimeStatus(const RagLibraryConfig& library) const;
    RagEmbeddingRuntimeStatus StartEmbeddingRuntime(const RagLibraryConfig& library) const;
    RagEmbeddingRuntimeStatus StopEmbeddingRuntime(const RagLibraryConfig& library) const;
    RagEmbeddingRuntimeStatus LaunchEmbeddingRuntimeInstaller(const RagLibraryConfig& library) const;
    RagEmbeddingRuntimeStatus LaunchEmbeddingModelInstaller(const RagLibraryConfig& library) const;
    RagEmbeddingTestResult TestEmbeddingProvider(const RagLibraryConfig& library) const;
    std::vector<RagExtractionToolStatus> GetExtractionToolStatus() const;
    RagExtractionToolInstallResult LaunchExtractionToolInstaller(bool recommended_only) const;

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
    RagEmbeddingRuntimeStatus GetEmbeddingRuntimeStatusNoLock(const RagLibraryConfig& library) const;
    void EnsureEmbeddingRuntimeNoLock(const RagLibraryConfig& library) const;
    void AppendEmbeddingRuntimeLogNoLock(const std::string& message) const;
    std::string ReadEmbeddingRuntimeLogTailNoLock(size_t max_bytes = 12000) const;
    void ShutdownManagedEmbeddingRuntimes() const;

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
};
