#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct ModelConfig {
    std::string id;
    std::string display_name;
    int context_window = 0;
    bool supports_streaming = true;
    bool supports_tools = false;
    bool supports_vision = false;
};

struct ProviderConfig {
    std::string id;
    std::string name;
    std::string base_url;
    std::string api_key;
    std::vector<ModelConfig> models;
};

struct MessageRecord {
    std::string role;
    std::string content;
    std::string created_at;
    std::string name;
    std::string tool_call_id;
    std::string tool_calls_json;
};

struct ChatInfo {
    std::string id;
    std::string name;
    std::string provider_id;
    std::string model_id;
    std::string system_prompt;
    double temperature = 0.2;
    int max_tokens = 1024;
};

struct ProjectInfo {
    std::string id;
    std::string name;
};

struct ProjectRecord {
    ProjectInfo info;
    std::vector<ChatInfo> chats;
};

enum class McpVariableKind {
    None,
    Folder,
    File,
};

enum class McpServerScope {
    PerProject,
    Shared,
};

struct McpServerVariable {
    std::string name;
    std::string description;
    McpVariableKind kind = McpVariableKind::None;
    bool inject_into_context = false;
};

struct ProjectMcpVariableValue {
    std::string name;
    std::string value;
};

struct ProjectMcpServerBinding {
    std::string server_id;
    std::vector<ProjectMcpVariableValue> variables;
};

struct McpServerConfig {
    std::string id;
    std::string name;
    std::string command;
    std::vector<std::string> arguments;
    std::string working_directory;
    std::vector<std::string> env_entries;
    McpServerScope scope = McpServerScope::PerProject;
    std::vector<McpServerVariable> variables;
    bool enabled = true;
    bool auto_connect = false;
};

enum class RagDocumentStorageMode {
    CopyIntoRagStore,
    ReferenceInPlace,
    CopyAndTrackOriginal,
};

struct RagLibraryConfig {
    std::string id;
    std::string name;
    std::string description;
    std::string storage_path;
    std::string embedding_provider = "none";
    std::string embedding_base_url = "http://localhost:11434";
    std::string embedding_model;
    int embedding_dimensions = 0;
    std::string vector_backend = "sqlite_vector_scan";
    int max_file_size_mb = 512;
    RagDocumentStorageMode storage_mode = RagDocumentStorageMode::CopyAndTrackOriginal;
    bool enabled = true;
    int chunk_size_chars = 3500;
    int chunk_overlap_chars = 350;
    int default_max_chunks = 8;
    bool rebuild_required = false;
    std::string rebuild_reason;
    bool split_large_extracted_documents = true;
    int extracted_segment_threshold_mb = 1;
    int extracted_segment_size_mb = 1;
    int extracted_segment_overlap_chars = 8000;
    std::string created_at;
    std::string updated_at;
};

struct ProjectRagBinding {
    std::string rag_id;
    bool enabled = true;
    bool can_read = true;
    bool can_write = false;
    bool can_delete = false;
    bool default_ingest_target = false;
    int retrieval_priority = 10;
    int max_chunks = 8;
};

struct RagDocumentRecord {
    std::string id;
    std::string rag_id;
    std::string display_name;
    std::string original_source_uri;
    std::string original_source_type;
    std::string stored_relative_path;
    std::string extracted_relative_path;
    std::string content_hash;
    uintmax_t file_size = 0;
    std::string mime_type;
    std::string imported_at;
    std::string last_indexed_at;
    std::string metadata_json;
};

struct RagChunkRecord {
    std::string id;
    std::string document_id;
    std::string rag_id;
    std::string text;
    std::string content_hash;
    int chunk_index = 0;
    int token_estimate = 0;
    std::string metadata_json;
};

struct RagLibraryStats {
    size_t document_count = 0;
    size_t chunk_count = 0;
    size_t embedding_count = 0;
    uintmax_t original_bytes = 0;
};

struct RagDocumentSummary {
    RagDocumentRecord document;
    int chunk_count = 0;
    int embedding_count = 0;
};

struct RagImportPreviewItem {
    std::string source_path;
    std::string display_name;
    std::string reason;
    std::string metadata_json;
    uintmax_t file_size = 0;
    bool supported = false;
};

struct RagImportPreview {
    bool success = false;
    int total_files = 0;
    int supported_files = 0;
    int skipped_files = 0;
    uintmax_t supported_bytes = 0;
    std::vector<RagImportPreviewItem> items;
    std::vector<std::string> errors;
};

struct RagIngestionResult {
    bool success = false;
    int files_ingested = 0;
    int files_skipped = 0;
    int chunks_added = 0;
    std::vector<std::string> errors;
};

struct RagProgressUpdate {
    int total_items = 0;
    int processed_items = 0;
    std::string stage;
    std::string current_item;
};

struct RagEmbeddingRuntimeStatus {
    std::string provider;
    std::string base_url;
    bool supported = false;
    bool installed = false;
    bool running = false;
    bool managed_by_app = false;
    std::string message;
    std::string install_command;
    std::string log_path;
    std::string recent_log;
};

struct RagEmbeddingTestResult {
    bool success = false;
    std::string provider;
    std::string model;
    int dimensions = 0;
    double elapsed_ms = 0.0;
    std::string message;
    RagEmbeddingRuntimeStatus runtime_status;
};

struct RagExtractionToolStatus {
    std::string id;
    std::string name;
    std::string executable;
    std::string purpose;
    std::string install_command;
    std::string notes;
    bool installed = false;
    bool installable = false;
    bool recommended = false;
};

struct RagExtractionToolInstallResult {
    bool launched = false;
    std::string command;
    std::string message;
};

struct RagQueryResult {
    std::string rag_id;
    std::string rag_name;
    std::string document_id;
    std::string document_title;
    std::string source_path;
    std::string chunk_id;
    std::string text;
    std::string retrieval_method;
    std::string metadata_json;
    std::string last_indexed_at;
    double score = 0.0;
};
