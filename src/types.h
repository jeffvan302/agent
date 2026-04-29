#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class BindingRoutingMode {
    TopDownFailover,
    RoundRobin,
};

struct BindingTargetConfig {
    std::string provider_id;
    std::string model_id;
    bool enabled = true;
    int priority = 100;
    int busy_retry_interval_seconds = 15;
    int busy_retry_budget_seconds = 90;
    int busy_cooldown_seconds = 300;
    int limit_cooldown_seconds = 900;
    int error_cooldown_seconds = 300;
};

struct BindingTargetRuntimeState {
    std::string provider_id;
    std::string model_id;
    std::string last_status;
    std::string last_used_at;
    std::string last_success_at;
    std::string last_busy_at;
    std::string last_limit_at;
    std::string last_error_at;
    std::string cooldown_until;
    int consecutive_busy_count = 0;
    int consecutive_limit_count = 0;
    int consecutive_failure_count = 0;
};

struct BindingModelRuntimeState {
    std::string provider_id;
    std::string model_id;
    int next_round_robin_index = 0;
    std::vector<BindingTargetRuntimeState> targets;
};

struct ModelConfig {
    std::string id;
    std::string display_name;
    int context_window = 0;
    int max_output_tokens = 0;
    bool supports_streaming = true;
    bool supports_tools = false;
    bool supports_vision = false;
    bool supports_embedding = false;
    bool supports_thinking = false;
    bool prefer_max_completion_tokens = false;
    std::string output_tokens_parameter = "auto";  // auto | max_tokens | max_completion_tokens
    std::string catalog_source = "manual";         // manual | bundled | discovered | agent_health
    std::vector<std::string> reasoning_efforts;
    std::vector<std::string> text_verbosity_modes;
    std::string default_reasoning_effort;
    std::string default_text_verbosity;
    int ollama_keep_alive_seconds = 0; // 0 = let Ollama use its default unload policy
    int ollama_num_threads = 0;        // 0 = auto (Ollama decides)
    bool ollama_no_gpu = false;        // true = force CPU-only
    int ollama_gpu_layers = 0;           // 0 = let Ollama decide; >0 = specific layer count
    int ollama_context_length = 0;       // 0 = model default
    bool ollama_verbose = false;           // show LLM stats in CLI output
    bool is_binding_model = false;
    BindingRoutingMode binding_routing_mode = BindingRoutingMode::TopDownFailover;
    std::vector<BindingTargetConfig> binding_targets;
};

struct ProviderConfig {
    std::string id;
    std::string name;
    std::string provider_type = "openai_compatible";
    std::string base_url;
    std::string api_key;
    std::string tls_certificate_fingerprint;
    std::string auth_mode;
    std::string oauth_credential_id;
    std::string oauth_account_label;
    bool oauth_authenticated = false;
    bool oauth_store_remote_history = false;
    std::string model_catalog_mode = "manual"; // manual | bundled | remote_endpoint
    int max_active_requests = 0;
    int max_queue_size = 0;
    int ollama_local_port = 0; // 0 = use default Ollama port (11434); custom port for managed local server
    std::vector<ModelConfig> models;
};

struct ProviderAuthRecord {
    std::string credential_id;
    std::string provider_id;
    std::string auth_mode;
    std::string api_key;
    std::string id_token;
    std::string access_token;
    std::string refresh_token;
    std::string token_type = "Bearer";
    std::string account_id;
    std::string account_email;
    std::string account_display_name;
    std::string scope;
    std::string expires_at;
    std::string last_refresh;
};

struct MessageRecord {
    std::string role;
    std::string content;
    std::string created_at;
    std::string name;
    std::string tool_call_id;
    std::string tool_calls_json;
};

struct ChatContextDebugEntry {
    std::string id;
    std::string created_at;
    std::string kind = "request";
    size_t user_message_index = 0;
    std::string provider_id;
    std::string model_id;
    std::string system_prompt;
    std::vector<MessageRecord> request_messages;
    std::string compressed_context;
    std::string mcp_context;
    std::string rag_context;
    std::string rag_working_set_json; // JSON array of RagWorkingSetEntry records injected this turn
};

struct ChatInfo {
    std::string id;
    std::string name;
    std::string provider_id;
    std::string model_id;
    std::string system_prompt;
    double temperature = 0.2;
    int max_tokens = 1024;
    std::string selected_agentic_mode_id;  // per-chat override; empty means use project default
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
    std::string description;
    bool inject_into_context = false;
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

enum class RagRetrievalMode {
    Both,           // passive context injection AND active tool (default)
    PassiveOnly,    // passive context injection only; not exposed as active tool
    ActiveToolOnly, // exposed as active tool only; not used for passive injection
    Disabled,       // neither passive injection nor active tool
};

struct ProjectRagBinding {
    std::string rag_id;
    bool enabled = true;
    bool can_read = true;
    bool can_write = false;
    bool expose_as_tool = false;
    bool can_delete = false;
    bool can_export = false;
    std::string export_path_template;
    bool default_ingest_target = false;
    int retrieval_priority = 10;
    int max_chunks = 8;
    double default_min_confidence = 0.0;
    double default_max_confidence = 1.0;
    RagRetrievalMode retrieval_mode = RagRetrievalMode::Both;
    // For model tools: search this RAG with the task instructions and inject results
    // into the sub-agent's system prompt before the first model call.
    bool inject_on_start = false;
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
    // Non-fatal notices (e.g. a chunk truncated to fit the model context window).
    std::vector<std::string> warnings;
};

struct RagExportResult {
    bool success = false;
    std::vector<std::string> output_files;  // absolute paths of generated .rag series files
    uint64_t total_uncompressed = 0;        // total source bytes across all files
    uint64_t total_compressed   = 0;        // total bytes written to .rag series
    int series_count = 0;
    std::string error;
};

struct RagImportResult {
    bool success = false;
    std::string rag_id;
    std::string library_name;
    std::string error;
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

struct RagImageIngestSettings {
    bool enabled = true;
    std::string mode = "tesseract_cpu";
    std::string tesseract_language = "eng";
    std::string paddle_python_command = "python";
    std::string paddle_language = "en";
    std::string vision_provider = "ollama";
    std::string vision_base_url = "http://localhost";
    std::string vision_model = "qwen2.5vl:7b";
    std::string remote_agent_model;
    std::string vision_prompt;
    int ollama_instance_count = 1;
    int ollama_start_port = 11434;
    bool ollama_start_locally = false;
    std::string remote_agent_base_url = "https://127.0.0.1";
    int remote_agent_https_port = 8765;
    std::string remote_agent_shared_secret;
    std::string remote_agent_certificate_fingerprint;
    std::string remote_agent_worker_name;
    std::string remote_agent_config_json;
    bool include_ocr_text = true;
    bool include_visual_description = true;
};

struct RagImageIngestRuntimeStatus {
    std::string mode;
    std::string message;
    bool enabled = true;
    bool tesseract_installed = false;
    bool python_installed = false;
    bool paddleocr_installed = false;
    bool ollama_installed = false;
    bool vision_endpoint_running = false;
    bool vision_ollama_start_locally = false;
    int vision_ollama_instance_count = 1;
    int vision_ollama_running_count = 0;
    int vision_ollama_managed_count = 0;
    int vision_queue_pending = 0;
    int vision_queue_active = 0;
    int vision_queue_workers = 0;
    int document_queue_pending = 0;
    int document_queue_active = 0;
    int document_queue_workers = 0;
    bool remote_agent_configured = false;
    std::string remote_agent_worker_name;
    std::string remote_agent_model;
    int remote_agent_https_port = 8765;
    std::string vision_endpoint_summary;
    std::string log_path;
    std::string recent_log;
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

// Entry in a per-chat RAG working set: a chunk retrieved via rag_search tool call.
struct RagWorkingSetEntry {
    std::string chunk_id;
    std::string rag_id;
    std::string rag_name;
    std::string document_id;
    std::string document_title;
    std::string text;
    double score = 0.0;
    std::string query;       // the rag_search query that retrieved this chunk
    std::string retrieved_at;
};

enum class IngestionJobStatus {
    Running,
    Completed,
    Failed,
};

struct IngestionJobRecord {
    std::string id;
    std::string rag_id;
    std::string rag_name;
    std::string kind;             // "files", "folder", "rebuild", "reindex", "generated"
    std::string source_description;
    IngestionJobStatus status = IngestionJobStatus::Running;
    int files_ingested = 0;
    int files_skipped = 0;
    int chunks_added = 0;
    std::vector<std::string> errors;
    std::string started_at;
    std::string finished_at;
};

// ===== Context Compression Types =====

enum class ContextCompressionStrategy {
    None,
    TruncateTop,
    HierarchicalStructured,
};

struct Layer1Config {
    bool enabled = true;
    int max_pins = 10;
    bool pin_code_blocks = true;
    bool pin_urls = true;
    bool pin_numbers = true;
    bool pin_first_message = true;
    bool pin_explicit_instructions = true;
    bool pin_user_flagged = true;  // user may add [PIN] or explicit remember/important markers
};

struct Layer0Config {
    bool enabled = false;
    std::string capture_model_id;
    std::string capture_model_provider_id;
    std::string capture_prompt_template;
    std::string selection_model_id;
    std::string selection_model_provider_id;
    std::string selection_prompt_template;
    std::string storage_folder_template = "$ProjectFolder$\\.agent\\.memory\\$CHATID$";
    int max_injected_rows = 12;
};

struct Layer2Config {
    bool enabled = true;
    std::string model_id;
    std::string model_provider_id;
    int max_tokens = 500;
    int trigger_threshold_turns = 8;
    std::string prompt_template;
};

struct Layer3Config {
    bool enabled = true;
    std::string model_id;
    std::string model_provider_id;
    int max_tokens = 800;
    std::string prompt_template;
};

struct Layer4Config {
    bool enabled = true;
    int min_recent_turns = 2;
};

struct ContextCompressionLayerSettings {
    Layer0Config layer0;
    Layer1Config layer1;
    Layer2Config layer2;
    Layer3Config layer3;
    Layer4Config layer4;
};

struct ContextCompressionConfig {
    std::string id;
    std::string name;
    ContextCompressionStrategy strategy = ContextCompressionStrategy::None;

    // Common settings for all strategies
    int frequency_every_n_prompts = 10;        // 0 = manual only
    int context_window_trigger_percent = 70;  // 0 = manual only

    // TruncateTop-specific settings
    int truncate_top_keep_messages = 20;

    // HierarchicalStructured-specific settings
    ContextCompressionLayerSettings layers;
};

struct ChatCompressionState {
    size_t last_compression_message_index = 0;
    std::string latest_snapshot_id;
    std::string current_compressed_context;
    size_t layer0_last_processed_message_index = 0;
    std::string layer0_current_index_block;
    std::string layer0_last_index_hash;
    std::string layer0_storage_path;
    std::string layer2_previous_summary;
    std::string layer3_previous_state_json;
    std::vector<MessageRecord> layer1_pinned_messages;
};

struct ChatCompressionSnapshot {
    std::string id;
    std::string created_at;
    std::string trigger_reason;
    std::string config_id;
    std::string config_name;
    std::string strategy;
    std::string previous_snapshot_id;
    size_t previous_message_index = 0;
    size_t compressed_through_message_index = 0;
    std::string previous_compressed_context;
    std::string compressed_context;
    std::vector<std::string> layer0_selected_artifact_ids;
    std::string layer0_index_block;
    std::string layer0_previous_index_hash;
    std::string layer0_index_hash;
    std::string layer2_summary;
    std::string layer3_state_json;
    std::vector<MessageRecord> pinned_messages;
    std::vector<MessageRecord> source_messages;
};

struct ProjectCompressionSettings {
    bool enabled = false;
    std::string config_id;
};

// A model tool is a named sub-agent that the main model can invoke as an MCP tool.
// It runs its own model instance with its own instructions, MCP access, and RAG access.
// Exposed to the calling model as a single tool named "agent_<sanitized_name>".
struct ModelToolConfig {
    std::string id;
    std::string name;            // human display name, e.g. "Legal Reviewer"
    std::string description;     // shown to calling model as the tool description
    std::string preferred_provider_id;
    std::string preferred_model_id;
    std::string instructions;    // system prompt injected into the sub-agent
    std::string selected_compression_config_id;  // optional context compression for sub-agent loop
    std::vector<ProjectMcpServerBinding> mcp_bindings;  // which servers this tool may use
    std::vector<ProjectRagBinding> rag_bindings;        // which RAG libraries this tool may use
};

struct AgenticModeConfig {
    std::string id;
    std::string name;
    std::string instructions;  // system prompt / instructions for this mode, stored as markdown
};

struct ProjectSettings {
    std::string project_name;
    std::string project_instructions;
    std::vector<ProjectMcpServerBinding> mcp_bindings;
    std::vector<ContextCompressionConfig> compression_configs;
    std::string selected_compression_config_id;
    std::vector<ProjectRagBinding> rag_bindings;
    std::string preferred_provider_id;
    std::string preferred_model_id;
    std::vector<std::string> model_tool_ids;  // IDs of model tools enabled for this project
    std::vector<ProjectMcpVariableValue> project_variables;  // Project-level key-value variables
    std::string selected_agentic_mode_id;       // ID of the default agentic mode for this project
    std::vector<std::string> enabled_agentic_mode_ids; // IDs of agentic modes enabled for this project
    bool enable_chat_logging = false;             // Enable detailed per-chat/request logging for this project
    bool allow_manual_context_compression = false; // Allow manual context window compression from web UI
    bool enable_web_debugging = false;             // Allow prompt/context debugging bubbles in the web UI
    bool serve_web_links_inline = false;            // Serve /data and /rag file links inline instead of forced downloads
    bool built_in_powershell_enabled = false;      // Enable high-risk built-in PowerShell execution tool
    std::string built_in_powershell_working_directory = "$ProjectFolder$";
    int model_timeout_seconds = 0;                  // 0 = wait forever (default), otherwise max seconds per model request
};
