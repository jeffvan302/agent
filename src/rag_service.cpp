#include "rag_service.h"

#include "util.h"

#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <shellapi.h>
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

namespace {
std::string BytesToHex(uint64_t value);
std::string StableHash(const std::string& text);
bool LooksLikeText(const std::string& content);
int EstimateTokens(const std::string& text);
std::string ColumnText(sqlite3_stmt* statement, int column);
std::string ReadWholeFile(const std::filesystem::path& path);
std::vector<std::string> ChunkText(const std::string& text, int chunk_size, int overlap);
std::wstring LowerExtension(const std::filesystem::path& path);
bool IsHtmlExtension(const std::filesystem::path& path);
std::string MimeTypeForPath(const std::filesystem::path& path);
std::string HtmlToMarkdownText(const std::string& html);
bool IsRichExtractionExtension(const std::filesystem::path& path);
bool IsImageExtension(const std::filesystem::path& path);
std::string ExtractRichDocumentToMarkdown(const std::filesystem::path& path, std::string* extractor_id);
std::string ExtractImageToMarkdown(const std::filesystem::path& path, const RagImageIngestSettings& settings, std::string* extractor_id);
std::string SanitizeUtf8ForJson(const std::string& text);
bool PythonModuleAvailable(const std::wstring& python_executable, const std::string& module_name);

std::string RagStorageModeToString(RagDocumentStorageMode mode) {
    switch (mode) {
    case RagDocumentStorageMode::CopyIntoRagStore:
        return "copy_into_rag_store";
    case RagDocumentStorageMode::ReferenceInPlace:
        return "reference_in_place";
    case RagDocumentStorageMode::CopyAndTrackOriginal:
    default:
        return "copy_and_track_original";
    }
}

RagDocumentStorageMode RagStorageModeFromString(const std::string& value) {
    if (value == "copy_into_rag_store") {
        return RagDocumentStorageMode::CopyIntoRagStore;
    }
    if (value == "reference_in_place") {
        return RagDocumentStorageMode::ReferenceInPlace;
    }
    return RagDocumentStorageMode::CopyAndTrackOriginal;
}

std::string DefaultImageVisionPrompt() {
    return "Describe this image for RAG ingestion. Include visible text, objects, layout, chart or graph interpretation, axes, legends, units, notable trends, and any uncertainty. Return concise Markdown with factual observations only.";
}

std::string NormalizeImageIngestMode(std::string mode) {
    mode = Trim(mode);
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (mode == "paddle" || mode == "paddleocr" || mode == "paddle_ocr" || mode == "paddle_ocr_gpu") {
        return "paddle_ocr_gpu";
    }
    if (mode == "vision" || mode == "vlm" || mode == "vision_language" || mode == "vision_language_gpu") {
        return "vision_language_gpu";
    }
    return "tesseract_cpu";
}

std::string NormalizeImageVisionProvider(std::string provider) {
    provider = Trim(provider);
    std::transform(provider.begin(), provider.end(), provider.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (provider == "ollama") {
        return "ollama";
    }
    return provider.empty() ? "none" : provider;
}

json RagImageIngestSettingsToJson(const RagImageIngestSettings& settings) {
    return json{
        {"enabled", settings.enabled},
        {"mode", NormalizeImageIngestMode(settings.mode)},
        {"tesseract_language", settings.tesseract_language.empty() ? "eng" : settings.tesseract_language},
        {"paddle_python_command", settings.paddle_python_command.empty() ? "python" : settings.paddle_python_command},
        {"paddle_language", settings.paddle_language.empty() ? "en" : settings.paddle_language},
        {"vision_provider", NormalizeImageVisionProvider(settings.vision_provider)},
        {"vision_base_url", settings.vision_base_url.empty() ? "http://localhost:11434" : settings.vision_base_url},
        {"vision_model", settings.vision_model.empty() ? "qwen2.5vl:7b" : settings.vision_model},
        {"vision_prompt", settings.vision_prompt.empty() ? DefaultImageVisionPrompt() : settings.vision_prompt},
        {"include_ocr_text", settings.include_ocr_text},
        {"include_visual_description", settings.include_visual_description},
    };
}

RagImageIngestSettings RagImageIngestSettingsFromJson(const json& item) {
    RagImageIngestSettings settings;
    settings.enabled = item.value("enabled", true);
    settings.mode = NormalizeImageIngestMode(item.value("mode", "tesseract_cpu"));
    settings.tesseract_language = item.value("tesseract_language", "eng");
    if (Trim(settings.tesseract_language).empty()) {
        settings.tesseract_language = "eng";
    }
    settings.paddle_python_command = item.value("paddle_python_command", "python");
    if (Trim(settings.paddle_python_command).empty()) {
        settings.paddle_python_command = "python";
    }
    settings.paddle_language = item.value("paddle_language", "en");
    if (Trim(settings.paddle_language).empty()) {
        settings.paddle_language = "en";
    }
    settings.vision_provider = NormalizeImageVisionProvider(item.value("vision_provider", "ollama"));
    settings.vision_base_url = item.value("vision_base_url", "http://localhost:11434");
    if (Trim(settings.vision_base_url).empty()) {
        settings.vision_base_url = "http://localhost:11434";
    }
    settings.vision_model = item.value("vision_model", "qwen2.5vl:7b");
    if (Trim(settings.vision_model).empty()) {
        settings.vision_model = "qwen2.5vl:7b";
    }
    settings.vision_prompt = item.value("vision_prompt", DefaultImageVisionPrompt());
    if (Trim(settings.vision_prompt).empty()) {
        settings.vision_prompt = DefaultImageVisionPrompt();
    }
    settings.include_ocr_text = item.value("include_ocr_text", true);
    settings.include_visual_description = item.value("include_visual_description", true);
    return settings;
}

json RagLibraryToJson(const RagLibraryConfig& config) {
    return json{
        {"id", config.id},
        {"name", config.name},
        {"description", config.description},
        {"storage_path", config.storage_path},
        {"embedding_provider", config.embedding_provider},
        {"embedding_base_url", config.embedding_base_url},
        {"embedding_model", config.embedding_model},
        {"embedding_dimensions", config.embedding_dimensions},
        {"vector_backend", config.vector_backend},
        {"max_file_size_mb", config.max_file_size_mb},
        {"storage_mode", RagStorageModeToString(config.storage_mode)},
        {"enabled", config.enabled},
        {"chunk_size_chars", config.chunk_size_chars},
        {"chunk_overlap_chars", config.chunk_overlap_chars},
        {"default_max_chunks", config.default_max_chunks},
        {"rebuild_required", config.rebuild_required},
        {"rebuild_reason", config.rebuild_reason},
        {"split_large_extracted_documents", config.split_large_extracted_documents},
        {"extracted_segment_threshold_mb", config.extracted_segment_threshold_mb},
        {"extracted_segment_size_mb", config.extracted_segment_size_mb},
        {"extracted_segment_overlap_chars", config.extracted_segment_overlap_chars},
        {"created_at", config.created_at},
        {"updated_at", config.updated_at},
    };
}

RagLibraryConfig RagLibraryFromJson(const json& item, const std::string& fallback_id) {
    RagLibraryConfig config;
    config.id = item.value("id", fallback_id);
    config.name = item.value("name", fallback_id);
    config.description = item.value("description", "");
    config.storage_path = item.value("storage_path", "");
    config.embedding_provider = item.value("embedding_provider", "none");
    config.embedding_base_url = item.value("embedding_base_url", "http://localhost:11434");
    config.embedding_model = item.value("embedding_model", "");
    config.embedding_dimensions = std::max(0, item.value("embedding_dimensions", 0));
    config.vector_backend = item.value("vector_backend", "sqlite_vector_scan");
    config.max_file_size_mb = std::max(1, item.value("max_file_size_mb", 512));
    config.storage_mode = RagStorageModeFromString(item.value("storage_mode", "copy_and_track_original"));
    config.enabled = item.value("enabled", true);
    config.chunk_size_chars = std::max(500, item.value("chunk_size_chars", 3500));
    config.chunk_overlap_chars = std::max(0, item.value("chunk_overlap_chars", 350));
    if (config.chunk_overlap_chars >= config.chunk_size_chars) {
        config.chunk_overlap_chars = config.chunk_size_chars / 10;
    }
    config.default_max_chunks = std::max(1, item.value("default_max_chunks", 8));
    config.rebuild_required = item.value("rebuild_required", false);
    config.rebuild_reason = item.value("rebuild_reason", "");
    config.split_large_extracted_documents = item.value("split_large_extracted_documents", true);
    config.extracted_segment_threshold_mb = std::max(1, item.value("extracted_segment_threshold_mb", 1));
    config.extracted_segment_size_mb = std::max(1, item.value("extracted_segment_size_mb", 1));
    config.extracted_segment_overlap_chars = std::max(0, item.value("extracted_segment_overlap_chars", 8000));
    config.created_at = item.value("created_at", "");
    config.updated_at = item.value("updated_at", "");
    return config;
}

json ProjectRagBindingToJson(const ProjectRagBinding& binding) {
    return json{
        {"rag_id", binding.rag_id},
        {"enabled", binding.enabled},
        {"can_read", binding.can_read},
        {"can_write", binding.can_write},
        {"expose_as_tool", binding.expose_as_tool},
        {"can_delete", binding.can_delete},
        {"can_export", binding.can_export},
        {"export_path_template", binding.export_path_template},
        {"default_ingest_target", binding.default_ingest_target},
        {"retrieval_priority", binding.retrieval_priority},
        {"max_chunks", binding.max_chunks},
        {"default_min_confidence", binding.default_min_confidence},
        {"default_max_confidence", binding.default_max_confidence},
    };
}

ProjectRagBinding ProjectRagBindingFromJson(const json& item) {
    ProjectRagBinding binding;
    binding.rag_id = item.value("rag_id", "");
    binding.enabled = item.value("enabled", true);
    binding.can_read = item.value("can_read", true);
    binding.can_write = item.value("can_write", false);
    binding.expose_as_tool = item.value("expose_as_tool", false);
    binding.can_delete = item.value("can_delete", false);
    binding.can_export = item.value("can_export", false);
    binding.export_path_template = item.value("export_path_template", "");
    binding.default_ingest_target = item.value("default_ingest_target", false);
    binding.retrieval_priority = item.value("retrieval_priority", 10);
    binding.max_chunks = std::max(1, item.value("max_chunks", 8));
    binding.default_min_confidence = std::clamp(item.value("default_min_confidence", 0.0), 0.0, 1.0);
    binding.default_max_confidence = std::clamp(item.value("default_max_confidence", 1.0), 0.0, 1.0);
    if (binding.default_min_confidence > binding.default_max_confidence) {
        binding.default_min_confidence = 0.0;
        binding.default_max_confidence = 1.0;
    }
    return binding;
}

json RagDocumentToJson(const RagDocumentRecord& document) {
    return json{
        {"id", document.id},
        {"rag_id", document.rag_id},
        {"display_name", document.display_name},
        {"original_source_uri", document.original_source_uri},
        {"original_source_type", document.original_source_type},
        {"stored_relative_path", document.stored_relative_path},
        {"extracted_relative_path", document.extracted_relative_path},
        {"content_hash", document.content_hash},
        {"file_size", document.file_size},
        {"mime_type", document.mime_type},
        {"imported_at", document.imported_at},
        {"last_indexed_at", document.last_indexed_at},
        {"metadata_json", document.metadata_json},
    };
}

RagDocumentRecord RagDocumentFromJson(const json& item) {
    RagDocumentRecord document;
    document.id = item.value("id", "");
    document.rag_id = item.value("rag_id", "");
    document.display_name = item.value("display_name", "");
    document.original_source_uri = item.value("original_source_uri", "");
    document.original_source_type = item.value("original_source_type", "");
    document.stored_relative_path = item.value("stored_relative_path", "");
    document.extracted_relative_path = item.value("extracted_relative_path", "");
    document.content_hash = item.value("content_hash", "");
    document.file_size = item.value("file_size", static_cast<uintmax_t>(0));
    document.mime_type = item.value("mime_type", "");
    document.imported_at = item.value("imported_at", "");
    document.last_indexed_at = item.value("last_indexed_at", "");
    document.metadata_json = item.value("metadata_json", "");
    return document;
}

json RagChunkToJson(const RagChunkRecord& chunk) {
    return json{
        {"id", chunk.id},
        {"document_id", chunk.document_id},
        {"rag_id", chunk.rag_id},
        {"text", chunk.text},
        {"content_hash", chunk.content_hash},
        {"chunk_index", chunk.chunk_index},
        {"token_estimate", chunk.token_estimate},
        {"metadata_json", chunk.metadata_json},
    };
}

RagChunkRecord RagChunkFromJson(const json& item) {
    RagChunkRecord chunk;
    chunk.id = item.value("id", "");
    chunk.document_id = item.value("document_id", "");
    chunk.rag_id = item.value("rag_id", "");
    chunk.text = item.value("text", "");
    chunk.content_hash = item.value("content_hash", "");
    chunk.chunk_index = item.value("chunk_index", 0);
    chunk.token_estimate = item.value("token_estimate", 0);
    chunk.metadata_json = item.value("metadata_json", "");
    return chunk;
}

json LoadJsonFile(const std::filesystem::path& path, const json& fallback) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return fallback;
    }
    try {
        json data;
        input >> data;
        return data;
    } catch (...) {
        return fallback;
    }
}

void SaveJsonFile(const std::filesystem::path& path, const json& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error("Unable to open file for writing: " + path.string());
    }
    output << data.dump(2);
}

struct SqliteDeleter {
    void operator()(sqlite3* db) const {
        if (db) {
            sqlite3_close(db);
        }
    }
};

struct StatementDeleter {
    void operator()(sqlite3_stmt* statement) const {
        if (statement) {
            sqlite3_finalize(statement);
        }
    }
};

using SqliteDb = std::unique_ptr<sqlite3, SqliteDeleter>;
using SqliteStatement = std::unique_ptr<sqlite3_stmt, StatementDeleter>;

void ThrowSqlite(sqlite3* db, const std::string& action) {
    throw std::runtime_error(action + ": " + sqlite3_errmsg(db));
}

SqliteDb OpenDatabase(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    sqlite3* raw = nullptr;
    const std::string path_utf8 = WideToUtf8(path.wstring());
    if (sqlite3_open_v2(path_utf8.c_str(), &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        std::string message = raw ? sqlite3_errmsg(raw) : "unknown SQLite open error";
        if (raw) {
            sqlite3_close(raw);
        }
        throw std::runtime_error("Could not open RAG database: " + message);
    }
    return SqliteDb(raw);
}

void ExecSql(sqlite3* db, const char* sql) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = error ? error : sqlite3_errmsg(db);
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}

SqliteStatement PrepareSql(sqlite3* db, const char* sql) {
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK) {
        ThrowSqlite(db, "Could not prepare SQL statement");
    }
    return SqliteStatement(raw);
}

void BindText(sqlite3_stmt* statement, int index, const std::string& value) {
    sqlite3_bind_text(statement, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

void BindBlob(sqlite3_stmt* statement, int index, const std::string& value) {
    sqlite3_bind_blob(statement, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

struct InternetHandleCloser {
    void operator()(void* handle) const {
        if (handle) {
            WinHttpCloseHandle(static_cast<HINTERNET>(handle));
        }
    }
};

using UniqueInternetHandle = std::unique_ptr<void, InternetHandleCloser>;

struct ParsedUrl {
    bool secure = false;
    INTERNET_PORT port = 0;
    std::wstring host;
    std::wstring path;
};

ParsedUrl CrackUrl(const std::string& url_utf8) {
    std::wstring url = Utf8ToWide(url_utf8);
    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);

    wchar_t host[2048] = {};
    wchar_t path[4096] = {};
    components.lpszHostName = host;
    components.dwHostNameLength = static_cast<DWORD>(std::size(host));
    components.lpszUrlPath = path;
    components.dwUrlPathLength = static_cast<DWORD>(std::size(path));

    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
        throw std::runtime_error("Invalid URL: " + url_utf8);
    }

    ParsedUrl parsed;
    parsed.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    parsed.port = components.nPort;
    parsed.host.assign(components.lpszHostName, components.dwHostNameLength);
    parsed.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (parsed.path.empty()) {
        parsed.path = L"/";
    }
    return parsed;
}

std::string JoinUrlPath(std::string base_url, const std::string& path) {
    while (!base_url.empty() && base_url.back() == '/') {
        base_url.pop_back();
    }
    return base_url + path;
}

std::string HttpPostJson(const std::string& url, const std::string& body) {
    ParsedUrl parsed = CrackUrl(url);
    UniqueInternetHandle session(WinHttpOpen(L"AgentRagService/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        throw std::runtime_error("Could not open HTTP session.");
    }

    UniqueInternetHandle connection(WinHttpConnect(static_cast<HINTERNET>(session.get()), parsed.host.c_str(), parsed.port, 0));
    if (!connection) {
        throw std::runtime_error("Could not connect to embedding provider.");
    }

    const DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
    UniqueInternetHandle request(WinHttpOpenRequest(static_cast<HINTERNET>(connection.get()), L"POST", parsed.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) {
        throw std::runtime_error("Could not create embedding provider request.");
    }

    WinHttpSetTimeouts(static_cast<HINTERNET>(request.get()), 10000, 10000, 30000, 180000);
    const std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";
    if (!WinHttpSendRequest(static_cast<HINTERNET>(request.get()), headers.c_str(), static_cast<DWORD>(headers.size()), const_cast<char*>(body.data()), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)) {
        throw std::runtime_error("Could not send embedding provider request.");
    }
    if (!WinHttpReceiveResponse(static_cast<HINTERNET>(request.get()), nullptr)) {
        throw std::runtime_error("Could not receive embedding provider response.");
    }

    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    WinHttpQueryHeaders(static_cast<HINTERNET>(request.get()), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX);

    std::string response;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(static_cast<HINTERNET>(request.get()), &available)) {
            throw std::runtime_error("Could not read embedding provider response.");
        }
        if (available == 0) {
            break;
        }
        std::string buffer(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(static_cast<HINTERNET>(request.get()), buffer.data(), available, &read)) {
            throw std::runtime_error("Could not read embedding provider response body.");
        }
        buffer.resize(read);
        response += buffer;
    }

    if (status_code < 200 || status_code >= 300) {
        std::ostringstream message;
        message << "Embedding provider HTTP " << status_code;
        if (!response.empty()) {
            message << ": " << response.substr(0, 500);
        }
        throw std::runtime_error(message.str());
    }
    return response;
}

std::vector<float> ExtractEmbeddingArray(const json& value) {
    if (!value.is_array()) {
        throw std::runtime_error("Embedding response did not contain an array.");
    }
    std::vector<float> vector;
    vector.reserve(value.size());
    for (const auto& item : value) {
        if (!item.is_number()) {
            throw std::runtime_error("Embedding response contained a non-numeric value.");
        }
        vector.push_back(item.get<float>());
    }
    if (vector.empty()) {
        throw std::runtime_error("Embedding response was empty.");
    }
    return vector;
}

bool IsUtf8ContinuationByte(unsigned char ch) {
    return (ch & 0xC0) == 0x80;
}

uint32_t DecodeAnsiByte(unsigned char ch) {
    if (ch < 0x80) {
        return ch;
    }

    const char input = static_cast<char>(ch);
    wchar_t output[2]{};
    const int count = MultiByteToWideChar(CP_ACP, 0, &input, 1, output, 2);
    if (count <= 0) {
        return L' ';
    }
    return static_cast<uint32_t>(output[0]);
}

void AppendSanitizedCodePoint(std::wstring& output, uint32_t code_point) {
    if (code_point == L'\r' || code_point == L'\n' || code_point == L'\t') {
        output.push_back(static_cast<wchar_t>(code_point));
        return;
    }
    if (code_point < 32 || code_point == 127 || (code_point >= 0x80 && code_point <= 0x9F) ||
        (code_point >= 0xD800 && code_point <= 0xDFFF) || code_point > 0x10FFFF) {
        output.push_back(L' ');
        return;
    }
    if (code_point <= 0xFFFF) {
        output.push_back(static_cast<wchar_t>(code_point));
        return;
    }

    code_point -= 0x10000;
    output.push_back(static_cast<wchar_t>(0xD800 + (code_point >> 10)));
    output.push_back(static_cast<wchar_t>(0xDC00 + (code_point & 0x3FF)));
}

std::string SanitizeUtf8ForJson(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    std::wstring cleaned;
    cleaned.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const unsigned char first = static_cast<unsigned char>(text[i]);
        if (first < 0x80) {
            AppendSanitizedCodePoint(cleaned, first);
            ++i;
            continue;
        }

        uint32_t code_point = 0;
        uint32_t minimum = 0;
        size_t continuation_count = 0;
        if ((first & 0xE0) == 0xC0) {
            code_point = first & 0x1F;
            minimum = 0x80;
            continuation_count = 1;
        } else if ((first & 0xF0) == 0xE0) {
            code_point = first & 0x0F;
            minimum = 0x800;
            continuation_count = 2;
        } else if ((first & 0xF8) == 0xF0) {
            code_point = first & 0x07;
            minimum = 0x10000;
            continuation_count = 3;
        } else {
            AppendSanitizedCodePoint(cleaned, DecodeAnsiByte(first));
            ++i;
            continue;
        }

        bool valid = i + continuation_count < text.size();
        for (size_t offset = 1; valid && offset <= continuation_count; ++offset) {
            const unsigned char next = static_cast<unsigned char>(text[i + offset]);
            if (!IsUtf8ContinuationByte(next)) {
                valid = false;
                break;
            }
            code_point = (code_point << 6) | (next & 0x3F);
        }

        if (!valid || code_point < minimum || code_point > 0x10FFFF || (code_point >= 0xD800 && code_point <= 0xDFFF)) {
            AppendSanitizedCodePoint(cleaned, DecodeAnsiByte(first));
            ++i;
            continue;
        }

        AppendSanitizedCodePoint(cleaned, code_point);
        i += continuation_count + 1;
    }

    return WideToUtf8(cleaned);
}

class IRagEmbeddingProvider {
public:
    virtual ~IRagEmbeddingProvider() = default;
    virtual std::vector<std::vector<float>> Embed(const std::vector<std::string>& texts) = 0;
    virtual std::string ProviderId() const = 0;
    virtual std::string ModelId() const = 0;
};

class OllamaEmbeddingProvider final : public IRagEmbeddingProvider {
public:
    explicit OllamaEmbeddingProvider(std::string base_url, std::string model)
        : base_url_(std::move(base_url)), model_(std::move(model)) {}

    std::vector<std::vector<float>> Embed(const std::vector<std::string>& texts) override {
        if (texts.empty()) {
            return {};
        }

        std::vector<std::string> clean_texts;
        clean_texts.reserve(texts.size());
        for (const auto& text : texts) {
            clean_texts.push_back(SanitizeUtf8ForJson(text));
        }

        try {
            json body;
            body["model"] = model_;
            body["input"] = clean_texts;
            const std::string response_text = HttpPostJson(JoinUrlPath(base_url_, "/api/embed"), body.dump());
            const json response = json::parse(response_text);
            if (!response.contains("embeddings") || !response["embeddings"].is_array()) {
                throw std::runtime_error("Ollama /api/embed response did not include embeddings.");
            }
            std::vector<std::vector<float>> embeddings;
            for (const auto& item : response["embeddings"]) {
                embeddings.push_back(ExtractEmbeddingArray(item));
            }
            if (embeddings.size() != clean_texts.size()) {
                throw std::runtime_error("Ollama returned a different number of embeddings than requested.");
            }
            return embeddings;
        } catch (...) {
            std::vector<std::vector<float>> embeddings;
            embeddings.reserve(clean_texts.size());
            for (const auto& text : clean_texts) {
                json body;
                body["model"] = model_;
                body["prompt"] = text;
                const std::string response_text = HttpPostJson(JoinUrlPath(base_url_, "/api/embeddings"), body.dump());
                const json response = json::parse(response_text);
                if (!response.contains("embedding")) {
                    throw std::runtime_error("Ollama embedding response did not include an embedding.");
                }
                embeddings.push_back(ExtractEmbeddingArray(response["embedding"]));
            }
            return embeddings;
        }
    }

    std::string ProviderId() const override {
        return "ollama";
    }

    std::string ModelId() const override {
        return model_;
    }

private:
    std::string base_url_;
    std::string model_;
};

class LmStudioEmbeddingProvider final : public IRagEmbeddingProvider {
public:
    explicit LmStudioEmbeddingProvider(std::string base_url, std::string model)
        : base_url_(std::move(base_url)), model_(std::move(model)) {}

    std::vector<std::vector<float>> Embed(const std::vector<std::string>& texts) override {
        if (texts.empty()) {
            return {};
        }
        std::vector<std::string> clean_texts;
        clean_texts.reserve(texts.size());
        for (const auto& text : texts) {
            clean_texts.push_back(SanitizeUtf8ForJson(text));
        }
        json body;
        body["model"] = model_;
        body["input"] = clean_texts;
        const std::string response_text = HttpPostJson(JoinUrlPath(base_url_, "/embeddings"), body.dump());
        const json response = json::parse(response_text);
        if (!response.contains("data") || !response["data"].is_array()) {
            throw std::runtime_error("LM Studio embedding response did not include data.");
        }

        std::vector<std::pair<int, std::vector<float>>> indexed_embeddings;
        for (size_t fallback_index = 0; fallback_index < response["data"].size(); ++fallback_index) {
            const auto& item = response["data"][fallback_index];
            if (!item.contains("embedding")) {
                throw std::runtime_error("LM Studio embedding response item did not include an embedding.");
            }
            const int index = item.value("index", static_cast<int>(fallback_index));
            indexed_embeddings.emplace_back(index, ExtractEmbeddingArray(item["embedding"]));
        }
        std::sort(indexed_embeddings.begin(), indexed_embeddings.end(), [](const auto& left, const auto& right) {
            return left.first < right.first;
        });

        std::vector<std::vector<float>> embeddings;
        embeddings.reserve(indexed_embeddings.size());
        for (auto& item : indexed_embeddings) {
            embeddings.push_back(std::move(item.second));
        }
        if (embeddings.size() != clean_texts.size()) {
            throw std::runtime_error("LM Studio returned a different number of embeddings than requested.");
        }
        return embeddings;
    }

    std::string ProviderId() const override {
        return "lmstudio";
    }

    std::string ModelId() const override {
        return model_;
    }

private:
    std::string base_url_;
    std::string model_;
};

std::unique_ptr<IRagEmbeddingProvider> CreateEmbeddingProvider(const RagLibraryConfig& library) {
    std::string provider = library.embedding_provider;
    std::transform(provider.begin(), provider.end(), provider.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (provider.empty() || provider == "none") {
        return nullptr;
    }
    if (provider == "ollama") {
        if (library.embedding_model.empty()) {
            throw std::runtime_error("Ollama embedding provider requires an embedding model.");
        }
        const std::string base_url = library.embedding_base_url.empty() ? "http://localhost:11434" : library.embedding_base_url;
        return std::make_unique<OllamaEmbeddingProvider>(base_url, library.embedding_model);
    }
    if (provider == "lmstudio" || provider == "lm studio") {
        if (library.embedding_model.empty()) {
            throw std::runtime_error("LM Studio embedding provider requires an embedding model.");
        }
        const std::string base_url = library.embedding_base_url.empty() ? "http://localhost:1234/v1" : library.embedding_base_url;
        return std::make_unique<LmStudioEmbeddingProvider>(base_url, library.embedding_model);
    }
    throw std::runtime_error("Unsupported embedding provider: " + library.embedding_provider);
}

bool IsHttpEndpointAvailable(const std::string& url) {
    try {
        ParsedUrl parsed = CrackUrl(url);
        UniqueInternetHandle session(WinHttpOpen(L"AgentRagService/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session) {
            return false;
        }
        UniqueInternetHandle connection(WinHttpConnect(static_cast<HINTERNET>(session.get()), parsed.host.c_str(), parsed.port, 0));
        if (!connection) {
            return false;
        }
        const DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
        UniqueInternetHandle request(WinHttpOpenRequest(static_cast<HINTERNET>(connection.get()), L"GET", parsed.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
        if (!request) {
            return false;
        }
        WinHttpSetTimeouts(static_cast<HINTERNET>(request.get()), 1000, 1000, 1000, 1000);
        if (!WinHttpSendRequest(static_cast<HINTERNET>(request.get()), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            return false;
        }
        if (!WinHttpReceiveResponse(static_cast<HINTERNET>(request.get()), nullptr)) {
            return false;
        }
        DWORD status_code = 0;
        DWORD status_size = sizeof(status_code);
        if (!WinHttpQueryHeaders(static_cast<HINTERNET>(request.get()), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX)) {
            return false;
        }
        return status_code >= 200 && status_code < 500;
    } catch (...) {
        return false;
    }
}

std::string NormalizeRuntimeProvider(std::string provider) {
    provider = Trim(provider);
    std::transform(provider.begin(), provider.end(), provider.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (provider == "lm studio" || provider == "lm-studio" || provider == "lm_studio") {
        return "lmstudio";
    }
    if (provider == "ollama") {
        return "ollama";
    }
    return provider.empty() ? "none" : provider;
}

std::wstring QuoteCommandArgument(const std::wstring& value) {
    std::wstring quoted = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'"') {
            quoted += L"\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += L"\"";
    return quoted;
}

std::optional<std::wstring> FindExecutableOnPath(const std::wstring& executable_name) {
    DWORD size = SearchPathW(nullptr, executable_name.c_str(), nullptr, 0, nullptr, nullptr);
    if (size == 0) {
        return std::nullopt;
    }
    std::wstring buffer(size, L'\0');
    DWORD written = SearchPathW(nullptr, executable_name.c_str(), nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (written == 0 || written >= buffer.size()) {
        return std::nullopt;
    }
    buffer.resize(written);
    return buffer;
}

std::string SerializeVector(const std::vector<float>& vector) {
    std::string blob(vector.size() * sizeof(float), '\0');
    if (!vector.empty()) {
        std::memcpy(blob.data(), vector.data(), blob.size());
    }
    return blob;
}

std::vector<float> DeserializeVector(const void* data, int bytes) {
    if (!data || bytes <= 0 || bytes % static_cast<int>(sizeof(float)) != 0) {
        return {};
    }
    std::vector<float> vector(static_cast<size_t>(bytes) / sizeof(float));
    std::memcpy(vector.data(), data, static_cast<size_t>(bytes));
    return vector;
}

double CosineSimilarity(const std::vector<float>& left, const std::vector<float>& right) {
    if (left.empty() || left.size() != right.size()) {
        return -1.0;
    }
    double dot = 0.0;
    double left_norm = 0.0;
    double right_norm = 0.0;
    for (size_t i = 0; i < left.size(); ++i) {
        dot += static_cast<double>(left[i]) * static_cast<double>(right[i]);
        left_norm += static_cast<double>(left[i]) * static_cast<double>(left[i]);
        right_norm += static_cast<double>(right[i]) * static_cast<double>(right[i]);
    }
    if (left_norm <= 0.0 || right_norm <= 0.0) {
        return -1.0;
    }
    return dot / (std::sqrt(left_norm) * std::sqrt(right_norm));
}

void EnsureRagDatabase(sqlite3* db) {
    ExecSql(db, "PRAGMA journal_mode=WAL;");
    ExecSql(db, "PRAGMA synchronous=NORMAL;");
    ExecSql(db, "PRAGMA foreign_keys=ON;");
    ExecSql(db,
        "CREATE TABLE IF NOT EXISTS documents ("
        "id TEXT PRIMARY KEY,"
        "rag_id TEXT NOT NULL,"
        "display_name TEXT NOT NULL,"
        "original_source_uri TEXT NOT NULL,"
        "original_source_type TEXT NOT NULL,"
        "stored_relative_path TEXT,"
        "extracted_relative_path TEXT,"
        "content_hash TEXT,"
        "file_size INTEGER DEFAULT 0,"
        "mime_type TEXT,"
        "imported_at TEXT,"
        "last_indexed_at TEXT,"
        "metadata_json TEXT,"
        "modified_time INTEGER DEFAULT 0,"
        "deleted INTEGER DEFAULT 0"
        ");");
    ExecSql(db, "CREATE UNIQUE INDEX IF NOT EXISTS idx_documents_source ON documents(original_source_uri);");
    ExecSql(db,
        "CREATE TABLE IF NOT EXISTS chunks ("
        "id TEXT PRIMARY KEY,"
        "document_id TEXT NOT NULL,"
        "rag_id TEXT NOT NULL,"
        "text TEXT NOT NULL,"
        "content_hash TEXT,"
        "chunk_index INTEGER DEFAULT 0,"
        "token_estimate INTEGER DEFAULT 0,"
        "metadata_json TEXT"
        ");");
    ExecSql(db, "CREATE INDEX IF NOT EXISTS idx_chunks_document ON chunks(document_id);");
    ExecSql(db, "CREATE INDEX IF NOT EXISTS idx_chunks_rag ON chunks(rag_id);");
    ExecSql(db, "CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5(chunk_id UNINDEXED, document_id UNINDEXED, rag_id UNINDEXED, text);");
    ExecSql(db,
        "CREATE TABLE IF NOT EXISTS embeddings ("
        "chunk_id TEXT NOT NULL,"
        "provider TEXT NOT NULL,"
        "model TEXT NOT NULL,"
        "dimensions INTEGER NOT NULL,"
        "vector_blob BLOB,"
        "created_at TEXT,"
        "PRIMARY KEY(chunk_id, provider, model)"
        ");");
    ExecSql(db, "CREATE INDEX IF NOT EXISTS idx_embeddings_provider_model ON embeddings(provider, model, dimensions);");
    ExecSql(db,
        "CREATE TABLE IF NOT EXISTS ingestion_events ("
        "id TEXT PRIMARY KEY,"
        "event_time TEXT,"
        "level TEXT,"
        "message TEXT"
        ");");
}

std::optional<RagDocumentRecord> FindDocumentBySource(sqlite3* db, const std::string& source) {
    auto statement = PrepareSql(db,
        "SELECT id, rag_id, display_name, original_source_uri, original_source_type, stored_relative_path, "
        "extracted_relative_path, content_hash, file_size, mime_type, imported_at, last_indexed_at, metadata_json "
        "FROM documents WHERE original_source_uri = ? AND deleted = 0;");
    BindText(statement.get(), 1, source);
    if (sqlite3_step(statement.get()) != SQLITE_ROW) {
        return std::nullopt;
    }

    RagDocumentRecord document;
    document.id = ColumnText(statement.get(), 0);
    document.rag_id = ColumnText(statement.get(), 1);
    document.display_name = ColumnText(statement.get(), 2);
    document.original_source_uri = ColumnText(statement.get(), 3);
    document.original_source_type = ColumnText(statement.get(), 4);
    document.stored_relative_path = ColumnText(statement.get(), 5);
    document.extracted_relative_path = ColumnText(statement.get(), 6);
    document.content_hash = ColumnText(statement.get(), 7);
    document.file_size = static_cast<uintmax_t>(sqlite3_column_int64(statement.get(), 8));
    document.mime_type = ColumnText(statement.get(), 9);
    document.imported_at = ColumnText(statement.get(), 10);
    document.last_indexed_at = ColumnText(statement.get(), 11);
    document.metadata_json = ColumnText(statement.get(), 12);
    return document;
}

std::optional<RagDocumentRecord> FindDocumentById(sqlite3* db, const std::string& document_id) {
    auto statement = PrepareSql(db,
        "SELECT id, rag_id, display_name, original_source_uri, original_source_type, stored_relative_path, "
        "extracted_relative_path, content_hash, file_size, mime_type, imported_at, last_indexed_at, metadata_json "
        "FROM documents WHERE id = ? AND deleted = 0;");
    BindText(statement.get(), 1, document_id);
    if (sqlite3_step(statement.get()) != SQLITE_ROW) {
        return std::nullopt;
    }

    RagDocumentRecord document;
    document.id = ColumnText(statement.get(), 0);
    document.rag_id = ColumnText(statement.get(), 1);
    document.display_name = ColumnText(statement.get(), 2);
    document.original_source_uri = ColumnText(statement.get(), 3);
    document.original_source_type = ColumnText(statement.get(), 4);
    document.stored_relative_path = ColumnText(statement.get(), 5);
    document.extracted_relative_path = ColumnText(statement.get(), 6);
    document.content_hash = ColumnText(statement.get(), 7);
    document.file_size = static_cast<uintmax_t>(sqlite3_column_int64(statement.get(), 8));
    document.mime_type = ColumnText(statement.get(), 9);
    document.imported_at = ColumnText(statement.get(), 10);
    document.last_indexed_at = ColumnText(statement.get(), 11);
    document.metadata_json = ColumnText(statement.get(), 12);
    return document;
}

std::vector<RagDocumentRecord> LoadActiveDocuments(sqlite3* db) {
    auto statement = PrepareSql(db,
        "SELECT id, rag_id, display_name, original_source_uri, original_source_type, stored_relative_path, "
        "extracted_relative_path, content_hash, file_size, mime_type, imported_at, last_indexed_at, metadata_json "
        "FROM documents WHERE deleted = 0 ORDER BY display_name, id;");

    std::vector<RagDocumentRecord> documents;
    int rc = SQLITE_OK;
    while ((rc = sqlite3_step(statement.get())) == SQLITE_ROW) {
        RagDocumentRecord document;
        document.id = ColumnText(statement.get(), 0);
        document.rag_id = ColumnText(statement.get(), 1);
        document.display_name = ColumnText(statement.get(), 2);
        document.original_source_uri = ColumnText(statement.get(), 3);
        document.original_source_type = ColumnText(statement.get(), 4);
        document.stored_relative_path = ColumnText(statement.get(), 5);
        document.extracted_relative_path = ColumnText(statement.get(), 6);
        document.content_hash = ColumnText(statement.get(), 7);
        document.file_size = static_cast<uintmax_t>(sqlite3_column_int64(statement.get(), 8));
        document.mime_type = ColumnText(statement.get(), 9);
        document.imported_at = ColumnText(statement.get(), 10);
        document.last_indexed_at = ColumnText(statement.get(), 11);
        document.metadata_json = ColumnText(statement.get(), 12);
        documents.push_back(std::move(document));
    }
    if (rc != SQLITE_DONE) {
        ThrowSqlite(db, "Could not load RAG documents");
    }
    return documents;
}

std::filesystem::path ResolveRebuildSourcePath(const std::filesystem::path& library_path, const RagDocumentRecord& document) {
    std::filesystem::path stored_candidate;
    if (!document.stored_relative_path.empty()) {
        std::filesystem::path stored_path(Utf8ToWide(document.stored_relative_path));
        if (stored_path.is_absolute()) {
            stored_candidate = stored_path;
        } else {
            stored_candidate = library_path / stored_path;
        }
        if (std::filesystem::exists(stored_candidate)) {
            return stored_candidate;
        }
    }
    if (document.original_source_type == "file" && !document.original_source_uri.empty()) {
        std::filesystem::path original_path(Utf8ToWide(document.original_source_uri));
        if (std::filesystem::exists(original_path) || stored_candidate.empty()) {
            return original_path;
        }
    }
    return stored_candidate;
}

void SaveDocument(sqlite3* db, const RagDocumentRecord& document) {
    auto statement = PrepareSql(db,
        "INSERT INTO documents (id, rag_id, display_name, original_source_uri, original_source_type, stored_relative_path, "
        "extracted_relative_path, content_hash, file_size, mime_type, imported_at, last_indexed_at, metadata_json, deleted) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0) "
        "ON CONFLICT(id) DO UPDATE SET "
        "rag_id=excluded.rag_id, display_name=excluded.display_name, original_source_uri=excluded.original_source_uri, "
        "original_source_type=excluded.original_source_type, stored_relative_path=excluded.stored_relative_path, "
        "extracted_relative_path=excluded.extracted_relative_path, content_hash=excluded.content_hash, "
        "file_size=excluded.file_size, mime_type=excluded.mime_type, last_indexed_at=excluded.last_indexed_at, "
        "metadata_json=excluded.metadata_json, deleted=0;");
    BindText(statement.get(), 1, document.id);
    BindText(statement.get(), 2, document.rag_id);
    BindText(statement.get(), 3, document.display_name);
    BindText(statement.get(), 4, document.original_source_uri);
    BindText(statement.get(), 5, document.original_source_type);
    BindText(statement.get(), 6, document.stored_relative_path);
    BindText(statement.get(), 7, document.extracted_relative_path);
    BindText(statement.get(), 8, document.content_hash);
    sqlite3_bind_int64(statement.get(), 9, static_cast<sqlite3_int64>(document.file_size));
    BindText(statement.get(), 10, document.mime_type);
    BindText(statement.get(), 11, document.imported_at);
    BindText(statement.get(), 12, document.last_indexed_at);
    BindText(statement.get(), 13, document.metadata_json);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        ThrowSqlite(db, "Could not save RAG document");
    }
}

void DeleteChunksForDocument(sqlite3* db, const std::string& document_id) {
    {
        auto statement = PrepareSql(db, "DELETE FROM chunks WHERE document_id = ?;");
        BindText(statement.get(), 1, document_id);
        if (sqlite3_step(statement.get()) != SQLITE_DONE) {
            ThrowSqlite(db, "Could not delete old RAG chunks");
        }
    }
    {
        auto statement = PrepareSql(db, "DELETE FROM chunks_fts WHERE document_id = ?;");
        BindText(statement.get(), 1, document_id);
        if (sqlite3_step(statement.get()) != SQLITE_DONE) {
            ThrowSqlite(db, "Could not delete old RAG full-text entries");
        }
    }
    {
        auto statement = PrepareSql(db, "DELETE FROM embeddings WHERE chunk_id NOT IN (SELECT id FROM chunks);");
        if (sqlite3_step(statement.get()) != SQLITE_DONE) {
            ThrowSqlite(db, "Could not clean stale RAG embeddings");
        }
    }
}

void ClearRagDatabaseForRebuild(sqlite3* db) {
    ExecSql(db, "BEGIN IMMEDIATE;");
    try {
        ExecSql(db, "DELETE FROM embeddings;");
        ExecSql(db, "DELETE FROM chunks_fts;");
        ExecSql(db, "DELETE FROM chunks;");
        ExecSql(db, "DELETE FROM documents;");
        ExecSql(db, "DELETE FROM ingestion_events;");
        ExecSql(db, "COMMIT;");
    } catch (...) {
        try {
            ExecSql(db, "ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

void InsertChunk(sqlite3* db, const RagChunkRecord& chunk) {
    {
        auto statement = PrepareSql(db,
            "INSERT INTO chunks (id, document_id, rag_id, text, content_hash, chunk_index, token_estimate, metadata_json) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?);");
        BindText(statement.get(), 1, chunk.id);
        BindText(statement.get(), 2, chunk.document_id);
        BindText(statement.get(), 3, chunk.rag_id);
        BindText(statement.get(), 4, chunk.text);
        BindText(statement.get(), 5, chunk.content_hash);
        sqlite3_bind_int(statement.get(), 6, chunk.chunk_index);
        sqlite3_bind_int(statement.get(), 7, chunk.token_estimate);
        BindText(statement.get(), 8, chunk.metadata_json);
        if (sqlite3_step(statement.get()) != SQLITE_DONE) {
            ThrowSqlite(db, "Could not insert RAG chunk");
        }
    }
    {
        auto statement = PrepareSql(db, "INSERT INTO chunks_fts (chunk_id, document_id, rag_id, text) VALUES (?, ?, ?, ?);");
        BindText(statement.get(), 1, chunk.id);
        BindText(statement.get(), 2, chunk.document_id);
        BindText(statement.get(), 3, chunk.rag_id);
        BindText(statement.get(), 4, chunk.text);
        if (sqlite3_step(statement.get()) != SQLITE_DONE) {
            ThrowSqlite(db, "Could not insert RAG full-text chunk");
        }
    }
}

void InsertEmbedding(sqlite3* db, const RagChunkRecord& chunk, const IRagEmbeddingProvider& provider, const std::vector<float>& vector) {
    auto statement = PrepareSql(db,
        "INSERT INTO embeddings (chunk_id, provider, model, dimensions, vector_blob, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(chunk_id, provider, model) DO UPDATE SET "
        "dimensions=excluded.dimensions, vector_blob=excluded.vector_blob, created_at=excluded.created_at;");
    const std::string blob = SerializeVector(vector);
    BindText(statement.get(), 1, chunk.id);
    BindText(statement.get(), 2, provider.ProviderId());
    BindText(statement.get(), 3, provider.ModelId());
    sqlite3_bind_int(statement.get(), 4, static_cast<int>(vector.size()));
    BindBlob(statement.get(), 5, blob);
    BindText(statement.get(), 6, CurrentTimestampUtc());
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        ThrowSqlite(db, "Could not save RAG embedding");
    }
}

std::string HashFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Could not open file.");
    }
    uint64_t hash = 1469598103934665603ull;
    std::vector<char> buffer(1024 * 1024);
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<size_t>(i)]);
            hash *= 1099511628211ull;
        }
    }
    return BytesToHex(hash);
}

bool FileLooksLikeText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }
    std::string sample(4096, '\0');
    input.read(sample.data(), static_cast<std::streamsize>(sample.size()));
    sample.resize(static_cast<size_t>(input.gcount()));
    return LooksLikeText(sample);
}

void InsertEmbeddingBatch(sqlite3* db, const RagLibraryConfig& library, const RagDocumentRecord& document, IRagEmbeddingProvider* embedding_provider, std::vector<RagChunkRecord>& embedding_batch, RagIngestionResult& result) {
    if (!embedding_provider || embedding_batch.empty()) {
        embedding_batch.clear();
        return;
    }
    try {
        std::vector<std::string> texts;
        texts.reserve(embedding_batch.size());
        for (const auto& chunk : embedding_batch) {
            texts.push_back(chunk.text);
        }
        const auto vectors = embedding_provider->Embed(texts);
        if (vectors.size() != embedding_batch.size()) {
            throw std::runtime_error("Embedding provider returned the wrong number of vectors.");
        }
        for (size_t i = 0; i < embedding_batch.size(); ++i) {
            if (library.embedding_dimensions > 0 && vectors[i].size() != static_cast<size_t>(library.embedding_dimensions)) {
                std::ostringstream message;
                message << "Embedding dimension mismatch for " << document.display_name
                        << ": expected " << library.embedding_dimensions
                        << ", got " << vectors[i].size() << ".";
                throw std::runtime_error(message.str());
            }
            InsertEmbedding(db, embedding_batch[i], *embedding_provider, vectors[i]);
        }
    } catch (const std::exception& ex) {
        result.errors.push_back(document.display_name + ": embedding failed: " + ex.what());
    } catch (...) {
        result.errors.push_back(document.display_name + ": embedding failed unexpectedly.");
    }
    embedding_batch.clear();
}

void InsertPreparedChunk(sqlite3* db, const RagDocumentRecord& document, std::string text, int& chunk_index, IRagEmbeddingProvider* embedding_provider, std::vector<RagChunkRecord>& embedding_batch, const std::string& metadata_json = {}) {
    text = Trim(SanitizeUtf8ForJson(text));
    if (text.empty()) {
        return;
    }
    RagChunkRecord chunk;
    chunk.id = MakeId("chunk");
    chunk.document_id = document.id;
    chunk.rag_id = document.rag_id;
    chunk.text = std::move(text);
    chunk.content_hash = StableHash(chunk.text);
    chunk.chunk_index = chunk_index++;
    chunk.token_estimate = EstimateTokens(chunk.text);
    chunk.metadata_json = metadata_json;
    InsertChunk(db, chunk);
    if (embedding_provider) {
        embedding_batch.push_back(std::move(chunk));
    }
}

int InsertChunksFromTextStream(sqlite3* db, const RagLibraryConfig& library, const RagDocumentRecord& document, const std::filesystem::path& source, const std::filesystem::path& extracted_target, IRagEmbeddingProvider* embedding_provider, RagIngestionResult& result) {
    std::ifstream input(source, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Could not open file.");
    }
    std::filesystem::create_directories(extracted_target.parent_path());
    std::ofstream extracted(extracted_target, std::ios::binary | std::ios::trunc);
    if (!extracted.is_open()) {
        throw std::runtime_error("Could not write extracted text.");
    }

    const int chunk_size = std::max(500, library.chunk_size_chars);
    const int overlap = std::max(0, std::min(library.chunk_overlap_chars, chunk_size - 1));
    std::vector<char> read_buffer(256 * 1024);
    std::string pending;
    int chunk_index = 0;
    std::vector<RagChunkRecord> embedding_batch;

    while (input) {
        input.read(read_buffer.data(), static_cast<std::streamsize>(read_buffer.size()));
        const std::streamsize count = input.gcount();
        if (count <= 0) {
            continue;
        }
        extracted.write(read_buffer.data(), count);
        pending.append(read_buffer.data(), read_buffer.data() + count);

        while (pending.size() >= static_cast<size_t>(chunk_size)) {
            size_t end = static_cast<size_t>(chunk_size);
            const size_t paragraph_break = pending.rfind("\n\n", end);
            if (paragraph_break != std::string::npos && paragraph_break > static_cast<size_t>(chunk_size / 2)) {
                end = paragraph_break + 2;
            }
            InsertPreparedChunk(db, document, pending.substr(0, end), chunk_index, embedding_provider, embedding_batch);
            if (embedding_batch.size() >= 8) {
                InsertEmbeddingBatch(db, library, document, embedding_provider, embedding_batch, result);
            }
            pending.erase(0, end > static_cast<size_t>(overlap) ? end - static_cast<size_t>(overlap) : end);
        }
    }

    InsertPreparedChunk(db, document, pending, chunk_index, embedding_provider, embedding_batch);
    InsertEmbeddingBatch(db, library, document, embedding_provider, embedding_batch, result);
    return chunk_index;
}

uintmax_t MegabytesToBytes(int megabytes) {
    return static_cast<uintmax_t>(std::max(1, megabytes)) * 1024ull * 1024ull;
}

bool ShouldSegmentExtractedText(const RagLibraryConfig& library, const std::string& extracted_text) {
    return library.split_large_extracted_documents &&
        extracted_text.size() > static_cast<size_t>(MegabytesToBytes(library.extracted_segment_threshold_mb));
}

struct ExtractedSegmentRange {
    size_t start = 0;
    size_t end = 0;
};

std::vector<ExtractedSegmentRange> PlanExtractedSegments(const std::string& text, size_t segment_size, size_t overlap) {
    std::vector<ExtractedSegmentRange> ranges;
    if (text.empty()) {
        return ranges;
    }

    segment_size = std::max<size_t>(1024, segment_size);
    overlap = std::min(overlap, segment_size / 2);

    size_t start = 0;
    while (start < text.size()) {
        size_t end = std::min(text.size(), start + segment_size);
        if (end < text.size()) {
            const size_t paragraph_break = text.rfind("\n\n", end);
            if (paragraph_break != std::string::npos && paragraph_break > start + (segment_size / 2)) {
                end = paragraph_break + 2;
            }
        }
        if (end <= start) {
            end = std::min(text.size(), start + segment_size);
        }
        ranges.push_back({start, end});
        if (end >= text.size()) {
            break;
        }
        const size_t next_start = end > overlap ? end - overlap : end;
        start = next_start > start ? next_start : end;
    }
    return ranges;
}

std::wstring SegmentFileName(size_t index) {
    std::wostringstream stream;
    stream << L"segment_" << std::setw(5) << std::setfill(L'0') << (index + 1) << L".md";
    return stream.str();
}

std::string BuildSegmentChunkMetadata(const std::filesystem::path& segment_relative, size_t segment_index, size_t segment_count, size_t start, size_t end, size_t overlap) {
    return json{
        {"extracted_segment_index", segment_index},
        {"extracted_segment_count", segment_count},
        {"extracted_segment_relative_path", WideToUtf8(segment_relative.generic_wstring())},
        {"extracted_segment_start", start},
        {"extracted_segment_end", end},
        {"extracted_segment_overlap_chars", overlap},
    }.dump();
}

int InsertChunksFromExtractedText(sqlite3* db, const RagLibraryConfig& library, const RagDocumentRecord& document, const std::string& extracted_text, const std::filesystem::path& library_path, const std::filesystem::path& extracted_relative, IRagEmbeddingProvider* embedding_provider, RagIngestionResult& result) {
    const std::string clean_extracted_text = SanitizeUtf8ForJson(extracted_text);
    const std::filesystem::path extracted_target = library_path / extracted_relative;
    if (ShouldSegmentExtractedText(library, clean_extracted_text)) {
        const size_t segment_size = static_cast<size_t>(MegabytesToBytes(library.extracted_segment_size_mb));
        const size_t overlap = std::min<size_t>(static_cast<size_t>(std::max(0, library.extracted_segment_overlap_chars)), segment_size / 2);
        const auto ranges = PlanExtractedSegments(clean_extracted_text, segment_size, overlap);
        const std::filesystem::path segment_dir_relative = extracted_relative.parent_path();
        const std::filesystem::path segment_dir = library_path / segment_dir_relative;
        std::error_code ec;
        std::filesystem::remove_all(segment_dir, ec);
        std::filesystem::create_directories(segment_dir);

        int chunk_index = 0;
        std::vector<RagChunkRecord> embedding_batch;
        json manifest;
        manifest["document_id"] = document.id;
        manifest["source"] = document.original_source_uri;
        manifest["extracted_content_type"] = "text/markdown";
        manifest["split"] = true;
        manifest["segment_size_mb"] = library.extracted_segment_size_mb;
        manifest["segment_overlap_chars"] = library.extracted_segment_overlap_chars;
        manifest["segments"] = json::array();

        for (size_t i = 0; i < ranges.size(); ++i) {
            const std::filesystem::path segment_relative = segment_dir_relative / SegmentFileName(i);
            const std::filesystem::path segment_target = library_path / segment_relative;
            const std::string segment_text = clean_extracted_text.substr(ranges[i].start, ranges[i].end - ranges[i].start);
            std::ofstream segment_output(segment_target, std::ios::binary | std::ios::trunc);
            if (!segment_output.is_open()) {
                throw std::runtime_error("Could not write extracted text segment.");
            }
            segment_output.write(segment_text.data(), static_cast<std::streamsize>(segment_text.size()));

            manifest["segments"].push_back(json{
                {"index", i},
                {"relative_path", WideToUtf8(segment_relative.generic_wstring())},
                {"start", ranges[i].start},
                {"end", ranges[i].end},
                {"bytes", segment_text.size()},
            });

            const std::string metadata_json = BuildSegmentChunkMetadata(segment_relative, i, ranges.size(), ranges[i].start, ranges[i].end, overlap);
            const auto chunks = ChunkText(segment_text, library.chunk_size_chars, library.chunk_overlap_chars);
            for (const auto& chunk_text : chunks) {
                InsertPreparedChunk(db, document, chunk_text, chunk_index, embedding_provider, embedding_batch, metadata_json);
                if (embedding_batch.size() >= 8) {
                    InsertEmbeddingBatch(db, library, document, embedding_provider, embedding_batch, result);
                }
            }
        }
        InsertEmbeddingBatch(db, library, document, embedding_provider, embedding_batch, result);

        std::ofstream manifest_output(extracted_target, std::ios::binary | std::ios::trunc);
        if (!manifest_output.is_open()) {
            throw std::runtime_error("Could not write extracted segment manifest.");
        }
        manifest_output << manifest.dump(2);
        return chunk_index;
    }

    std::filesystem::create_directories(extracted_target.parent_path());
    std::ofstream extracted(extracted_target, std::ios::binary | std::ios::trunc);
    if (!extracted.is_open()) {
        throw std::runtime_error("Could not write extracted text.");
    }
    extracted.write(clean_extracted_text.data(), static_cast<std::streamsize>(clean_extracted_text.size()));

    int chunk_index = 0;
    std::vector<RagChunkRecord> embedding_batch;
    const auto chunks = ChunkText(clean_extracted_text, library.chunk_size_chars, library.chunk_overlap_chars);
    for (const auto& chunk_text : chunks) {
        InsertPreparedChunk(db, document, chunk_text, chunk_index, embedding_provider, embedding_batch);
        if (embedding_batch.size() >= 8) {
            InsertEmbeddingBatch(db, library, document, embedding_provider, embedding_batch, result);
        }
    }
    InsertEmbeddingBatch(db, library, document, embedding_provider, embedding_batch, result);
    return chunk_index;
}

std::string BuildFtsQuery(const std::string& query) {
    std::vector<std::string> terms;
    std::string current;
    for (unsigned char ch : query) {
        if (std::isalnum(ch) != 0) {
            current.push_back(static_cast<char>(std::tolower(ch)));
        } else if (!current.empty()) {
            if (current.size() > 1) {
                terms.push_back(current);
            }
            current.clear();
        }
    }
    if (current.size() > 1) {
        terms.push_back(current);
    }

    std::sort(terms.begin(), terms.end());
    terms.erase(std::unique(terms.begin(), terms.end()), terms.end());

    std::ostringstream stream;
    for (size_t i = 0; i < terms.size(); ++i) {
        if (i > 0) {
            stream << " OR ";
        }
        stream << terms[i];
    }
    return stream.str();
}

std::string ColumnText(sqlite3_stmt* statement, int column) {
    const unsigned char* text = sqlite3_column_text(statement, column);
    return text ? reinterpret_cast<const char*>(text) : std::string();
}

std::string BytesToHex(uint64_t value) {
    std::ostringstream stream;
    stream << std::hex << value;
    return stream.str();
}

std::string StableHash(const std::string& text) {
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char ch : text) {
        hash ^= ch;
        hash *= 1099511628211ull;
    }
    return BytesToHex(hash);
}

bool IsSupportedTextExtension(const std::filesystem::path& path) {
    const std::wstring ext = LowerExtension(path);
    static const std::set<std::wstring> extensions = {
        L".txt", L".md", L".markdown", L".json", L".csv", L".log", L".xml",
        L".cpp", L".c", L".h", L".hpp", L".cs", L".js", L".ts", L".tsx",
        L".jsx", L".py", L".ps1", L".bat", L".cmd", L".ini", L".toml",
        L".yaml", L".yml", L".html", L".htm", L".docx", L".docm",
        L".xlsx", L".xlsm", L".pdf", L".css", L".sql",
        L".png", L".jpg", L".jpeg", L".bmp", L".tif", L".tiff", L".webp",
    };
    return extensions.find(ext) != extensions.end();
}

std::string ReadWholeFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Could not open file.");
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool LooksLikeText(const std::string& content) {
    if (content.empty()) {
        return true;
    }
    const size_t sample_size = std::min<size_t>(content.size(), 4096);
    size_t control_count = 0;
    for (size_t i = 0; i < sample_size; ++i) {
        const unsigned char ch = static_cast<unsigned char>(content[i]);
        if (ch == 0) {
            return false;
        }
        if (ch < 9 || (ch > 13 && ch < 32)) {
            ++control_count;
        }
    }
    return control_count < sample_size / 20;
}

int EstimateTokens(const std::string& text) {
    if (text.empty()) {
        return 0;
    }
    return static_cast<int>(std::max<size_t>(1, (text.size() + 2) / 3));
}

std::vector<std::string> ChunkText(const std::string& text, int chunk_size, int overlap) {
    std::vector<std::string> chunks;
    const int bounded_chunk_size = std::max(500, chunk_size);
    const int bounded_overlap = std::max(0, std::min(overlap, bounded_chunk_size - 1));
    size_t offset = 0;
    while (offset < text.size()) {
        size_t end = std::min(text.size(), offset + static_cast<size_t>(bounded_chunk_size));
        if (end < text.size()) {
            const size_t paragraph_break = text.rfind("\n\n", end);
            if (paragraph_break != std::string::npos && paragraph_break > offset + static_cast<size_t>(bounded_chunk_size / 2)) {
                end = paragraph_break + 2;
            }
        }
        std::string chunk = Trim(text.substr(offset, end - offset));
        if (!chunk.empty()) {
            chunks.push_back(std::move(chunk));
        }
        if (end >= text.size()) {
            break;
        }
        offset = end > static_cast<size_t>(bounded_overlap) ? end - static_cast<size_t>(bounded_overlap) : end;
    }
    return chunks;
}

std::wstring LowerExtension(const std::filesystem::path& path) {
    std::wstring ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t ch) { return static_cast<wchar_t>(::towlower(ch)); });
    return ext;
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool IsHtmlExtension(const std::filesystem::path& path) {
    const std::wstring ext = LowerExtension(path);
    return ext == L".html" || ext == L".htm";
}

std::string MimeTypeForPath(const std::filesystem::path& path) {
    const std::wstring ext = LowerExtension(path);
    if (ext == L".html" || ext == L".htm") {
        return "text/html";
    }
    if (ext == L".docx") {
        return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
    }
    if (ext == L".docm") {
        return "application/vnd.ms-word.document.macroEnabled.12";
    }
    if (ext == L".xlsx") {
        return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
    }
    if (ext == L".xlsm") {
        return "application/vnd.ms-excel.sheet.macroEnabled.12";
    }
    if (ext == L".pdf") {
        return "application/pdf";
    }
    if (ext == L".png") {
        return "image/png";
    }
    if (ext == L".jpg" || ext == L".jpeg") {
        return "image/jpeg";
    }
    if (ext == L".bmp") {
        return "image/bmp";
    }
    if (ext == L".tif" || ext == L".tiff") {
        return "image/tiff";
    }
    if (ext == L".webp") {
        return "image/webp";
    }
    if (ext == L".md" || ext == L".markdown") {
        return "text/markdown";
    }
    if (ext == L".json") {
        return "application/json";
    }
    if (ext == L".csv") {
        return "text/csv";
    }
    if (ext == L".xml") {
        return "application/xml";
    }
    if (ext == L".css") {
        return "text/css";
    }
    if (ext == L".sql") {
        return "application/sql";
    }
    return "text/plain";
}

size_t FindCaseInsensitive(const std::string& lower_haystack, const std::string& lower_needle, size_t start) {
    return lower_haystack.find(lower_needle, start);
}

std::string HtmlTagName(const std::string& tag) {
    size_t pos = 0;
    while (pos < tag.size() && (std::isspace(static_cast<unsigned char>(tag[pos])) || tag[pos] == '/')) {
        ++pos;
    }
    size_t start = pos;
    while (pos < tag.size() && (std::isalnum(static_cast<unsigned char>(tag[pos])) || tag[pos] == '-' || tag[pos] == ':')) {
        ++pos;
    }
    return LowerAscii(tag.substr(start, pos - start));
}

bool HtmlTagIsClosing(const std::string& tag) {
    size_t pos = 0;
    while (pos < tag.size() && std::isspace(static_cast<unsigned char>(tag[pos]))) {
        ++pos;
    }
    return pos < tag.size() && tag[pos] == '/';
}

void AppendMarkdownBreak(std::string& output, int desired_newlines) {
    int existing = 0;
    for (size_t i = output.size(); i > 0 && output[i - 1] == '\n'; --i) {
        ++existing;
    }
    while (existing < desired_newlines) {
        output.push_back('\n');
        ++existing;
    }
}

int HexValue(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + ch - 'a';
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + ch - 'A';
    }
    return -1;
}

void AppendUtf8Codepoint(std::string& output, uint32_t codepoint) {
    if (codepoint <= 0x7f) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | ((codepoint >> 6) & 0x1f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | ((codepoint >> 12) & 0x0f)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0x10ffff) {
        output.push_back(static_cast<char>(0xf0 | ((codepoint >> 18) & 0x07)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

std::string DecodeHtmlEntities(const std::string& text) {
    std::string output;
    output.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            output.push_back(text[i]);
            continue;
        }
        const size_t semicolon = text.find(';', i + 1);
        if (semicolon == std::string::npos || semicolon - i > 16) {
            output.push_back(text[i]);
            continue;
        }

        const std::string entity = text.substr(i + 1, semicolon - i - 1);
        const std::string lower = LowerAscii(entity);
        if (lower == "amp") {
            output.push_back('&');
        } else if (lower == "lt") {
            output.push_back('<');
        } else if (lower == "gt") {
            output.push_back('>');
        } else if (lower == "quot") {
            output.push_back('"');
        } else if (lower == "apos") {
            output.push_back('\'');
        } else if (lower == "nbsp") {
            output.push_back(' ');
        } else if (!lower.empty() && lower[0] == '#') {
            uint32_t value = 0;
            bool ok = true;
            if (lower.size() > 2 && lower[1] == 'x') {
                for (size_t pos = 2; pos < lower.size(); ++pos) {
                    const int digit = HexValue(lower[pos]);
                    if (digit < 0) {
                        ok = false;
                        break;
                    }
                    value = value * 16 + static_cast<uint32_t>(digit);
                }
            } else {
                for (size_t pos = 1; pos < lower.size(); ++pos) {
                    if (!std::isdigit(static_cast<unsigned char>(lower[pos]))) {
                        ok = false;
                        break;
                    }
                    value = value * 10 + static_cast<uint32_t>(lower[pos] - '0');
                }
            }
            if (ok) {
                AppendUtf8Codepoint(output, value);
            } else {
                output += "&" + entity + ";";
            }
        } else {
            output += "&" + entity + ";";
        }
        i = semicolon;
    }
    return output;
}

std::string NormalizeMarkdownWhitespace(const std::string& text) {
    std::string output;
    output.reserve(text.size());
    bool pending_space = false;
    int newlines = 2;
    for (unsigned char ch : text) {
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            while (!output.empty() && output.back() == ' ') {
                output.pop_back();
            }
            if (newlines < 2) {
                output.push_back('\n');
            }
            ++newlines;
            pending_space = false;
            continue;
        }
        if (std::isspace(ch)) {
            pending_space = true;
            continue;
        }
        if (pending_space && !output.empty() && output.back() != '\n') {
            output.push_back(' ');
        }
        output.push_back(static_cast<char>(ch));
        pending_space = false;
        newlines = 0;
    }
    return Trim(output);
}

std::string HtmlToMarkdownText(const std::string& html) {
    const std::string lower_html = LowerAscii(html);
    std::string output;
    output.reserve(html.size());

    for (size_t i = 0; i < html.size();) {
        if (html[i] != '<') {
            output.push_back(html[i]);
            ++i;
            continue;
        }

        if (lower_html.compare(i, 4, "<!--") == 0) {
            const size_t end = lower_html.find("-->", i + 4);
            i = end == std::string::npos ? html.size() : end + 3;
            continue;
        }

        const size_t close = html.find('>', i + 1);
        if (close == std::string::npos) {
            output.push_back(html[i]);
            ++i;
            continue;
        }

        const std::string tag = html.substr(i + 1, close - i - 1);
        const std::string tag_name = HtmlTagName(tag);
        const bool closing = HtmlTagIsClosing(tag);

        if (!closing && (tag_name == "script" || tag_name == "style" || tag_name == "head" || tag_name == "noscript" || tag_name == "svg")) {
            const size_t block_end = FindCaseInsensitive(lower_html, "</" + tag_name, close + 1);
            if (block_end == std::string::npos) {
                i = close + 1;
            } else {
                const size_t block_close = html.find('>', block_end);
                i = block_close == std::string::npos ? html.size() : block_close + 1;
            }
            continue;
        }

        if (tag_name.size() == 2 && tag_name[0] == 'h' && tag_name[1] >= '1' && tag_name[1] <= '6') {
            AppendMarkdownBreak(output, closing ? 2 : 2);
            if (!closing) {
                output.append(static_cast<size_t>(tag_name[1] - '0'), '#');
                output.push_back(' ');
            }
        } else if (tag_name == "p" || tag_name == "div" || tag_name == "section" || tag_name == "article" ||
                   tag_name == "header" || tag_name == "footer" || tag_name == "main" || tag_name == "aside" ||
                   tag_name == "table" || tag_name == "tr" || tag_name == "pre" || tag_name == "blockquote") {
            AppendMarkdownBreak(output, 2);
        } else if (tag_name == "br") {
            AppendMarkdownBreak(output, 1);
        } else if (!closing && tag_name == "li") {
            AppendMarkdownBreak(output, 1);
            output += "- ";
        } else if (tag_name == "ul" || tag_name == "ol") {
            AppendMarkdownBreak(output, 2);
        } else if (tag_name == "td" || tag_name == "th") {
            output += " | ";
        } else if (tag_name == "hr") {
            AppendMarkdownBreak(output, 2);
            output += "---";
            AppendMarkdownBreak(output, 2);
        } else if (tag_name == "title" && !closing) {
            AppendMarkdownBreak(output, 2);
            output += "# ";
        } else if (tag_name == "title" && closing) {
            AppendMarkdownBreak(output, 2);
        }
        i = close + 1;
    }

    return NormalizeMarkdownWhitespace(DecodeHtmlEntities(output));
}

bool IsDocxExtension(const std::filesystem::path& path) {
    const std::wstring ext = LowerExtension(path);
    return ext == L".docx" || ext == L".docm";
}

bool IsXlsxExtension(const std::filesystem::path& path) {
    const std::wstring ext = LowerExtension(path);
    return ext == L".xlsx" || ext == L".xlsm";
}

bool IsPdfExtension(const std::filesystem::path& path) {
    return LowerExtension(path) == L".pdf";
}

bool IsImageExtension(const std::filesystem::path& path) {
    const std::wstring ext = LowerExtension(path);
    return ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp" ||
        ext == L".tif" || ext == L".tiff" || ext == L".webp";
}

bool IsRichExtractionExtension(const std::filesystem::path& path) {
    return IsHtmlExtension(path) || IsDocxExtension(path) || IsXlsxExtension(path) || IsPdfExtension(path);
}

std::filesystem::path CreateTempDirectory(const std::string& prefix) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / Utf8ToWide(MakeId(prefix));
    std::filesystem::create_directories(root);
    return root;
}

bool RunProcessAndWait(std::wstring command_line, DWORD timeout_ms, std::string* error) {
    std::vector<wchar_t> buffer(command_line.begin(), command_line.end());
    buffer.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    if (!created) {
        if (error) {
            *error = "Could not start process.";
        }
        return false;
    }

    const DWORD wait_result = WaitForSingleObject(process.hProcess, timeout_ms);
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        if (error) {
            *error = "Process timed out.";
        }
        return false;
    }

    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (exit_code != 0) {
        if (error) {
            *error = "Process exited with code " + std::to_string(exit_code) + ".";
        }
        return false;
    }
    return true;
}

std::string Base64Encode(const std::string& bytes) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((bytes.size() + 2) / 3) * 4);
    size_t index = 0;
    while (index < bytes.size()) {
        const unsigned char a = static_cast<unsigned char>(bytes[index++]);
        const bool has_b = index < bytes.size();
        const unsigned char b = has_b ? static_cast<unsigned char>(bytes[index++]) : 0;
        const bool has_c = index < bytes.size();
        const unsigned char c = has_c ? static_cast<unsigned char>(bytes[index++]) : 0;

        output.push_back(alphabet[(a >> 2) & 0x3F]);
        output.push_back(alphabet[((a & 0x03) << 4) | ((b >> 4) & 0x0F)]);
        output.push_back(has_b ? alphabet[((b & 0x0F) << 2) | ((c >> 6) & 0x03)] : '=');
        output.push_back(has_c ? alphabet[c & 0x3F] : '=');
    }
    return output;
}

std::string ImageModeLabel(const std::string& mode) {
    const std::string normalized = NormalizeImageIngestMode(mode);
    if (normalized == "paddle_ocr_gpu") {
        return "GPU OCR (PaddleOCR)";
    }
    if (normalized == "vision_language_gpu") {
        return "Full vision understanding (OCR + vision-language model)";
    }
    return "CPU OCR (Tesseract)";
}

std::optional<std::string> ExtractImageWithTesseract(const std::filesystem::path& path, const RagImageIngestSettings& settings, std::string* error) {
    const auto tesseract = FindExecutableOnPath(L"tesseract.exe");
    if (!tesseract) {
        if (error) {
            *error = "tesseract.exe was not found on PATH.";
        }
        return std::nullopt;
    }

    const std::filesystem::path temp = CreateTempDirectory("rag_tesseract");
    try {
        const std::filesystem::path output_base = temp / "ocr";
        const std::filesystem::path output_text = temp / "ocr.txt";
        std::wstring command = QuoteCommandArgument(*tesseract) + L" " +
            QuoteCommandArgument(path.wstring()) + L" " +
            QuoteCommandArgument(output_base.wstring());
        const std::string language = Trim(settings.tesseract_language).empty() ? "eng" : Trim(settings.tesseract_language);
        command += L" -l " + QuoteCommandArgument(Utf8ToWide(language));

        std::string process_error;
        if (!RunProcessAndWait(command, 180000, &process_error) || !std::filesystem::exists(output_text)) {
            if (error) {
                *error = process_error.empty() ? "Tesseract did not produce OCR output." : process_error;
            }
            std::error_code ec;
            std::filesystem::remove_all(temp, ec);
            return std::nullopt;
        }

        std::string text = NormalizeMarkdownWhitespace(ReadWholeFile(output_text));
        std::error_code ec;
        std::filesystem::remove_all(temp, ec);
        if (Trim(text).empty()) {
            if (error) {
                *error = "Tesseract produced no readable OCR text.";
            }
            return std::nullopt;
        }
        return text;
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove_all(temp, ec);
        throw;
    }
}

std::optional<std::string> ExtractImageWithPaddleOcr(const std::filesystem::path& path, const RagImageIngestSettings& settings, std::string* error) {
    std::wstring python_command = Utf8ToWide(Trim(settings.paddle_python_command));
    if (python_command.empty()) {
        python_command = L"python";
    }
    if (python_command == L"python" || python_command == L"python.exe") {
        const auto python = FindExecutableOnPath(L"python.exe");
        if (!python) {
            if (error) {
                *error = "python.exe was not found on PATH.";
            }
            return std::nullopt;
        }
        python_command = *python;
    }

    const std::filesystem::path temp = CreateTempDirectory("rag_paddleocr");
    try {
        const std::filesystem::path script = temp / "paddle_ocr_extract.py";
        const std::filesystem::path output = temp / "ocr.txt";
        const std::filesystem::path error_output = temp / "ocr_error.txt";
        {
            std::ofstream script_file(script, std::ios::binary | std::ios::trunc);
            script_file
                << "import sys, traceback\n"
                << "from pathlib import Path\n"
                << "image_path = sys.argv[1]\n"
                << "out_path = Path(sys.argv[2])\n"
                << "err_path = Path(sys.argv[3])\n"
                << "lang = sys.argv[4] if len(sys.argv) > 4 else 'en'\n"
                << "texts = []\n"
                << "def add_text(value):\n"
                << "    value = str(value).strip()\n"
                << "    if value and value not in texts:\n"
                << "        texts.append(value)\n"
                << "def collect(obj):\n"
                << "    if obj is None:\n"
                << "        return\n"
                << "    if isinstance(obj, dict):\n"
                << "        for key in ('rec_texts', 'texts'):\n"
                << "            value = obj.get(key)\n"
                << "            if isinstance(value, (list, tuple)):\n"
                << "                for item in value:\n"
                << "                    add_text(item)\n"
                << "        value = obj.get('text')\n"
                << "        if isinstance(value, str):\n"
                << "            add_text(value)\n"
                << "        for value in obj.values():\n"
                << "            collect(value)\n"
                << "        return\n"
                << "    if isinstance(obj, (list, tuple)):\n"
                << "        if len(obj) >= 2 and isinstance(obj[1], (list, tuple)) and obj[1] and isinstance(obj[1][0], str):\n"
                << "            add_text(obj[1][0])\n"
                << "        for item in obj:\n"
                << "            collect(item)\n"
                << "        return\n"
                << "    for attr in ('json', 'res'):\n"
                << "        if hasattr(obj, attr):\n"
                << "            try:\n"
                << "                collect(getattr(obj, attr))\n"
                << "            except Exception:\n"
                << "                pass\n"
                << "try:\n"
                << "    from paddleocr import PaddleOCR\n"
                << "    try:\n"
                << "        ocr = PaddleOCR(use_angle_cls=True, lang=lang)\n"
                << "    except TypeError:\n"
                << "        try:\n"
                << "            ocr = PaddleOCR(lang=lang)\n"
                << "        except TypeError:\n"
                << "            ocr = PaddleOCR()\n"
                << "    try:\n"
                << "        result = ocr.ocr(image_path, cls=True)\n"
                << "    except Exception:\n"
                << "        try:\n"
                << "            result = ocr.predict(image_path)\n"
                << "        except TypeError:\n"
                << "            result = ocr.predict(input=image_path)\n"
                << "    collect(result)\n"
                << "    out_path.write_text('\\n'.join(texts), encoding='utf-8', errors='replace')\n"
                << "    sys.exit(0 if texts else 4)\n"
                << "except Exception:\n"
                << "    err_path.write_text(traceback.format_exc(), encoding='utf-8', errors='replace')\n"
                << "    sys.exit(2)\n";
        }

        std::string process_error;
        const std::string language = Trim(settings.paddle_language).empty() ? "en" : Trim(settings.paddle_language);
        const std::wstring command = QuoteCommandArgument(python_command) + L" " +
            QuoteCommandArgument(script.wstring()) + L" " +
            QuoteCommandArgument(path.wstring()) + L" " +
            QuoteCommandArgument(output.wstring()) + L" " +
            QuoteCommandArgument(error_output.wstring()) + L" " +
            QuoteCommandArgument(Utf8ToWide(language));
        if (!RunProcessAndWait(command, 300000, &process_error) || !std::filesystem::exists(output)) {
            if (error) {
                std::string details = process_error;
                if (std::filesystem::exists(error_output)) {
                    details = ReadWholeFile(error_output);
                }
                *error = details.empty() ? "PaddleOCR did not produce OCR output." : details.substr(0, 1000);
            }
            std::error_code ec;
            std::filesystem::remove_all(temp, ec);
            return std::nullopt;
        }

        std::string text = NormalizeMarkdownWhitespace(ReadWholeFile(output));
        std::error_code ec;
        std::filesystem::remove_all(temp, ec);
        if (Trim(text).empty()) {
            if (error) {
                *error = "PaddleOCR produced no readable OCR text.";
            }
            return std::nullopt;
        }
        return text;
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove_all(temp, ec);
        throw;
    }
}

std::optional<std::string> DescribeImageWithOllamaVision(const std::filesystem::path& path, const RagImageIngestSettings& settings, std::string* error) {
    if (NormalizeImageVisionProvider(settings.vision_provider) != "ollama") {
        if (error) {
            *error = "Only Ollama vision-language image description is currently wired in.";
        }
        return std::nullopt;
    }
    if (Trim(settings.vision_model).empty()) {
        if (error) {
            *error = "No vision-language model is configured.";
        }
        return std::nullopt;
    }

    try {
        json body;
        body["model"] = Trim(settings.vision_model);
        body["prompt"] = Trim(settings.vision_prompt).empty() ? DefaultImageVisionPrompt() : settings.vision_prompt;
        body["stream"] = false;
        body["images"] = json::array({Base64Encode(ReadWholeFile(path))});
        const std::string base_url = Trim(settings.vision_base_url).empty() ? "http://localhost:11434" : Trim(settings.vision_base_url);
        const json response = json::parse(HttpPostJson(JoinUrlPath(base_url, "/api/generate"), body.dump()));
        std::string description = response.value("response", "");
        description = NormalizeMarkdownWhitespace(description);
        if (Trim(description).empty()) {
            if (error) {
                *error = "Ollama returned an empty image description.";
            }
            return std::nullopt;
        }
        return description;
    } catch (const std::exception& ex) {
        if (error) {
            *error = ex.what();
        }
    } catch (...) {
        if (error) {
            *error = "Unexpected Ollama vision-language error.";
        }
    }
    return std::nullopt;
}

std::string ExtractImageToMarkdown(const std::filesystem::path& path, const RagImageIngestSettings& settings, std::string* extractor_id) {
    if (!settings.enabled) {
        throw std::runtime_error("System-wide image ingestion is disabled.");
    }

    const std::string mode = NormalizeImageIngestMode(settings.mode);
    std::vector<std::string> warnings;
    std::string ocr_text;
    std::string ocr_engine;

    if (mode == "paddle_ocr_gpu" || mode == "vision_language_gpu") {
        std::string paddle_error;
        if (const auto paddle_text = ExtractImageWithPaddleOcr(path, settings, &paddle_error)) {
            ocr_text = *paddle_text;
            ocr_engine = "paddleocr";
        } else if (!paddle_error.empty()) {
            warnings.push_back("PaddleOCR was requested but did not produce OCR text: " + paddle_error);
        }
    }

    if (ocr_text.empty() && settings.include_ocr_text) {
        std::string tesseract_error;
        if (const auto tesseract_text = ExtractImageWithTesseract(path, settings, &tesseract_error)) {
            ocr_text = *tesseract_text;
            ocr_engine = "tesseract";
        } else if (!tesseract_error.empty()) {
            warnings.push_back("Tesseract OCR did not produce text: " + tesseract_error);
        }
    }

    std::string visual_description;
    if (mode == "vision_language_gpu" && settings.include_visual_description) {
        std::string vision_error;
        if (const auto description = DescribeImageWithOllamaVision(path, settings, &vision_error)) {
            visual_description = *description;
        } else if (!vision_error.empty()) {
            warnings.push_back("Vision-language description was requested but did not produce output: " + vision_error);
        }
    }

    if (ocr_text.empty() && visual_description.empty()) {
        std::string message = "Image ingestion produced no OCR text or visual description.";
        if (!warnings.empty()) {
            message += " Last warning: " + warnings.back();
        }
        throw std::runtime_error(message);
    }

    if (extractor_id) {
        if (mode == "vision_language_gpu" && !visual_description.empty()) {
            *extractor_id = ocr_engine.empty() ? "ollama_vision" : (ocr_engine + "_plus_ollama_vision");
        } else if (!ocr_engine.empty()) {
            *extractor_id = ocr_engine;
        } else {
            *extractor_id = "image_ingest";
        }
    }

    std::ostringstream markdown;
    markdown << "# " << WideToUtf8(path.filename().wstring()) << "\n\n";
    markdown << "## Image Ingest Metadata\n\n";
    markdown << "- Pipeline mode: " << ImageModeLabel(mode) << "\n";
    markdown << "- OCR engine: " << (ocr_engine.empty() ? "none" : ocr_engine) << "\n";
    markdown << "- Vision provider: " << (mode == "vision_language_gpu" ? NormalizeImageVisionProvider(settings.vision_provider) : "none") << "\n";
    markdown << "- Vision model: " << (mode == "vision_language_gpu" ? Trim(settings.vision_model) : "none") << "\n";
    markdown << "- Original image: preserved in the RAG document store.\n\n";

    if (!warnings.empty()) {
        markdown << "## Image Ingest Warnings\n\n";
        for (const auto& warning : warnings) {
            markdown << "- " << NormalizeMarkdownWhitespace(warning) << "\n";
        }
        markdown << "\n";
    }

    if (!visual_description.empty()) {
        markdown << "## Visual Description\n\n";
        markdown << visual_description << "\n\n";
    }

    if (!ocr_text.empty()) {
        markdown << "## OCR Text\n\n";
        markdown << ocr_text << "\n";
    }

    return NormalizeMarkdownWhitespace(markdown.str());
}

bool ExtractZipWithTar(const std::filesystem::path& source, const std::filesystem::path& destination, std::string* error) {
    const auto tar = FindExecutableOnPath(L"tar.exe");
    if (!tar) {
        if (error) {
            *error = "tar.exe was not found on PATH. Windows tar is required to extract Office Open XML files.";
        }
        return false;
    }
    std::filesystem::create_directories(destination);
    const std::wstring command = QuoteCommandArgument(*tar) + L" -xf " + QuoteCommandArgument(source.wstring()) + L" -C " + QuoteCommandArgument(destination.wstring());
    return RunProcessAndWait(command, 120000, error);
}

std::string XmlAttribute(const std::string& tag, const std::string& name) {
    for (char quote : {'"', '\''}) {
        const std::string needle = name + "=" + quote;
        size_t pos = tag.find(needle);
        if (pos == std::string::npos) {
            continue;
        }
        pos += needle.size();
        const size_t end = tag.find(quote, pos);
        if (end != std::string::npos) {
            return DecodeHtmlEntities(tag.substr(pos, end - pos));
        }
    }
    return {};
}

bool XmlTagMatches(const std::string& tag_name, const std::string& local_name) {
    return tag_name == local_name ||
        (tag_name.size() > local_name.size() &&
         tag_name[tag_name.size() - local_name.size() - 1] == ':' &&
         tag_name.compare(tag_name.size() - local_name.size(), local_name.size(), local_name) == 0);
}

std::string ExtractTagTextRuns(const std::string& xml, const std::string& local_tag_name) {
    std::string text;
    for (size_t i = 0; i < xml.size();) {
        const size_t open = xml.find('<', i);
        if (open == std::string::npos) {
            break;
        }
        const size_t close = xml.find('>', open + 1);
        if (close == std::string::npos) {
            break;
        }
        const std::string tag = xml.substr(open + 1, close - open - 1);
        const std::string tag_name = HtmlTagName(tag);
        if (HtmlTagIsClosing(tag) || !XmlTagMatches(tag_name, local_tag_name)) {
            i = close + 1;
            continue;
        }

        const std::string closing = "</" + tag_name + ">";
        std::string lower_xml = LowerAscii(xml);
        const size_t end = lower_xml.find(LowerAscii(closing), close + 1);
        if (end == std::string::npos) {
            i = close + 1;
            continue;
        }
        text += DecodeHtmlEntities(xml.substr(close + 1, end - close - 1));
        i = end + closing.size();
    }
    return text;
}

std::string ExtractWordXmlText(const std::string& xml) {
    std::string output;
    const std::string lower_xml = LowerAscii(xml);
    for (size_t i = 0; i < xml.size();) {
        const size_t open = xml.find('<', i);
        if (open == std::string::npos) {
            break;
        }
        const size_t close = xml.find('>', open + 1);
        if (close == std::string::npos) {
            break;
        }
        const std::string tag = xml.substr(open + 1, close - open - 1);
        const std::string tag_name = HtmlTagName(tag);
        const bool closing = HtmlTagIsClosing(tag);

        if (!closing && XmlTagMatches(tag_name, "t")) {
            const std::string closing_tag = "</" + tag_name + ">";
            const size_t end = lower_xml.find(LowerAscii(closing_tag), close + 1);
            if (end == std::string::npos) {
                i = close + 1;
                continue;
            }
            output += DecodeHtmlEntities(xml.substr(close + 1, end - close - 1));
            i = end + closing_tag.size();
            continue;
        }
        if (!closing && XmlTagMatches(tag_name, "tab")) {
            output.push_back('\t');
        } else if (!closing && (XmlTagMatches(tag_name, "br") || XmlTagMatches(tag_name, "cr"))) {
            AppendMarkdownBreak(output, 1);
        } else if (closing && XmlTagMatches(tag_name, "p")) {
            AppendMarkdownBreak(output, 2);
        }
        i = close + 1;
    }
    return NormalizeMarkdownWhitespace(output);
}

std::string ExtractDocxToMarkdown(const std::filesystem::path& path) {
    const std::filesystem::path temp = CreateTempDirectory("rag_docx");
    try {
        std::string error;
        if (!ExtractZipWithTar(path, temp, &error)) {
            throw std::runtime_error(error);
        }

        std::ostringstream markdown;
        markdown << "# " << WideToUtf8(path.filename().wstring()) << "\n\n";

        const std::filesystem::path document_xml = temp / "word" / "document.xml";
        if (std::filesystem::exists(document_xml)) {
            const std::string text = ExtractWordXmlText(ReadWholeFile(document_xml));
            if (!text.empty()) {
                markdown << text << "\n\n";
            }
        }

        const std::filesystem::path word_dir = temp / "word";
        if (std::filesystem::exists(word_dir)) {
            for (const auto& entry : std::filesystem::directory_iterator(word_dir)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                const std::wstring name = entry.path().filename().wstring();
                const std::wstring lower_name = LowerAscii(WideToUtf8(name)).empty() ? L"" : Utf8ToWide(LowerAscii(WideToUtf8(name)));
                if (lower_name.rfind(L"header", 0) != 0 && lower_name.rfind(L"footer", 0) != 0 &&
                    lower_name != L"footnotes.xml" && lower_name != L"endnotes.xml") {
                    continue;
                }
                const std::string text = ExtractWordXmlText(ReadWholeFile(entry.path()));
                if (!text.empty()) {
                    markdown << "## " << WideToUtf8(entry.path().stem().wstring()) << "\n\n" << text << "\n\n";
                }
            }
        }

        std::error_code ec;
        std::filesystem::remove_all(temp, ec);
        const std::string result = NormalizeMarkdownWhitespace(markdown.str());
        if (result.size() < 8) {
            throw std::runtime_error("DOCX extraction produced no text.");
        }
        return result;
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove_all(temp, ec);
        throw;
    }
}

std::vector<std::string> ExtractXlsxSharedStrings(const std::filesystem::path& shared_strings_path) {
    std::vector<std::string> shared;
    if (!std::filesystem::exists(shared_strings_path)) {
        return shared;
    }
    const std::string xml = ReadWholeFile(shared_strings_path);
    const std::string lower_xml = LowerAscii(xml);
    for (size_t pos = 0;;) {
        const size_t open = lower_xml.find("<si", pos);
        if (open == std::string::npos) {
            break;
        }
        const size_t open_close = lower_xml.find('>', open + 1);
        const size_t close = lower_xml.find("</si>", open_close == std::string::npos ? open + 1 : open_close + 1);
        if (open_close == std::string::npos || close == std::string::npos) {
            break;
        }
        shared.push_back(NormalizeMarkdownWhitespace(ExtractTagTextRuns(xml.substr(open_close + 1, close - open_close - 1), "t")));
        pos = close + 5;
    }
    return shared;
}

std::map<std::string, std::string> ExtractWorkbookRelationships(const std::filesystem::path& rels_path) {
    std::map<std::string, std::string> relationships;
    if (!std::filesystem::exists(rels_path)) {
        return relationships;
    }
    const std::string xml = ReadWholeFile(rels_path);
    for (size_t pos = 0;;) {
        const size_t open = xml.find("<Relationship", pos);
        if (open == std::string::npos) {
            break;
        }
        const size_t close = xml.find('>', open + 1);
        if (close == std::string::npos) {
            break;
        }
        const std::string tag = xml.substr(open + 1, close - open - 1);
        const std::string id = XmlAttribute(tag, "Id");
        std::string target = XmlAttribute(tag, "Target");
        if (!id.empty() && !target.empty()) {
            while (!target.empty() && target.front() == '/') {
                target.erase(target.begin());
            }
            if (target.rfind("xl/", 0) != 0) {
                target = "xl/" + target;
            }
            relationships[id] = target;
        }
        pos = close + 1;
    }
    return relationships;
}

struct XlsxSheetInfo {
    std::string name;
    std::string rel_id;
};

std::vector<XlsxSheetInfo> ExtractWorkbookSheets(const std::filesystem::path& workbook_path) {
    std::vector<XlsxSheetInfo> sheets;
    if (!std::filesystem::exists(workbook_path)) {
        return sheets;
    }
    const std::string xml = ReadWholeFile(workbook_path);
    for (size_t pos = 0;;) {
        const size_t open = xml.find("<sheet", pos);
        if (open == std::string::npos) {
            break;
        }
        const size_t close = xml.find('>', open + 1);
        if (close == std::string::npos) {
            break;
        }
        const std::string tag = xml.substr(open + 1, close - open - 1);
        XlsxSheetInfo sheet;
        sheet.name = XmlAttribute(tag, "name");
        sheet.rel_id = XmlAttribute(tag, "r:id");
        if (!sheet.name.empty() || !sheet.rel_id.empty()) {
            sheets.push_back(std::move(sheet));
        }
        pos = close + 1;
    }
    return sheets;
}

std::string FirstTagText(const std::string& xml, const std::string& local_tag_name) {
    const std::string lower_xml = LowerAscii(xml);
    for (size_t pos = 0;;) {
        const size_t open = xml.find('<', pos);
        if (open == std::string::npos) {
            return {};
        }
        const size_t close = xml.find('>', open + 1);
        if (close == std::string::npos) {
            return {};
        }
        const std::string tag = xml.substr(open + 1, close - open - 1);
        const std::string tag_name = HtmlTagName(tag);
        if (HtmlTagIsClosing(tag) || !XmlTagMatches(tag_name, local_tag_name)) {
            pos = close + 1;
            continue;
        }
        const std::string closing_tag = "</" + tag_name + ">";
        const size_t end = lower_xml.find(LowerAscii(closing_tag), close + 1);
        if (end == std::string::npos) {
            return {};
        }
        return DecodeHtmlEntities(xml.substr(close + 1, end - close - 1));
    }
}

std::string ExtractXlsxCellValue(const std::string& cell_tag, const std::string& cell_body, const std::vector<std::string>& shared_strings) {
    const std::string type = XmlAttribute(cell_tag, "t");
    if (type == "s") {
        const std::string index_text = Trim(FirstTagText(cell_body, "v"));
        if (!index_text.empty()) {
            try {
                const size_t index = static_cast<size_t>(std::stoul(index_text));
                if (index < shared_strings.size()) {
                    return shared_strings[index];
                }
            } catch (...) {
            }
        }
        return {};
    }
    if (type == "inlineStr") {
        return NormalizeMarkdownWhitespace(ExtractTagTextRuns(cell_body, "t"));
    }
    if (type == "b") {
        const std::string value = Trim(FirstTagText(cell_body, "v"));
        return value == "1" ? "TRUE" : value == "0" ? "FALSE" : value;
    }
    std::string value = Trim(FirstTagText(cell_body, "v"));
    if (value.empty()) {
        value = NormalizeMarkdownWhitespace(ExtractTagTextRuns(cell_body, "t"));
    }
    return value;
}

std::string ExtractWorksheetMarkdown(const std::filesystem::path& worksheet_path, const std::vector<std::string>& shared_strings) {
    const std::string xml = ReadWholeFile(worksheet_path);
    const std::string lower_xml = LowerAscii(xml);
    std::ostringstream markdown;
    for (size_t row_pos = 0;;) {
        const size_t row_open = lower_xml.find("<row", row_pos);
        if (row_open == std::string::npos) {
            break;
        }
        const size_t row_tag_end = lower_xml.find('>', row_open + 1);
        const size_t row_close = lower_xml.find("</row>", row_tag_end == std::string::npos ? row_open + 1 : row_tag_end + 1);
        if (row_tag_end == std::string::npos || row_close == std::string::npos) {
            break;
        }
        const std::string row_body = xml.substr(row_tag_end + 1, row_close - row_tag_end - 1);
        const std::string lower_row = LowerAscii(row_body);

        std::vector<std::string> values;
        for (size_t cell_pos = 0;;) {
            const size_t cell_open = lower_row.find("<c", cell_pos);
            if (cell_open == std::string::npos) {
                break;
            }
            const size_t cell_tag_end = lower_row.find('>', cell_open + 1);
            if (cell_tag_end == std::string::npos) {
                break;
            }
            const std::string cell_tag = row_body.substr(cell_open + 1, cell_tag_end - cell_open - 1);
            size_t cell_close = lower_row.find("</c>", cell_tag_end + 1);
            std::string cell_body;
            if (cell_close == std::string::npos) {
                cell_body.clear();
                cell_pos = cell_tag_end + 1;
            } else {
                cell_body = row_body.substr(cell_tag_end + 1, cell_close - cell_tag_end - 1);
                cell_pos = cell_close + 4;
            }
            values.push_back(ExtractXlsxCellValue(cell_tag, cell_body, shared_strings));
        }

        bool any_value = false;
        for (const auto& value : values) {
            if (!Trim(value).empty()) {
                any_value = true;
                break;
            }
        }
        if (any_value) {
            markdown << "|";
            for (const auto& value : values) {
                std::string clean = NormalizeMarkdownWhitespace(value);
                std::replace(clean.begin(), clean.end(), '|', '/');
                markdown << " " << clean << " |";
            }
            markdown << "\n";
        }
        row_pos = row_close + 6;
    }
    return markdown.str();
}

std::string ExtractXlsxToMarkdown(const std::filesystem::path& path) {
    const std::filesystem::path temp = CreateTempDirectory("rag_xlsx");
    try {
        std::string error;
        if (!ExtractZipWithTar(path, temp, &error)) {
            throw std::runtime_error(error);
        }

        const auto shared_strings = ExtractXlsxSharedStrings(temp / "xl" / "sharedStrings.xml");
        const auto relationships = ExtractWorkbookRelationships(temp / "xl" / "_rels" / "workbook.xml.rels");
        auto sheets = ExtractWorkbookSheets(temp / "xl" / "workbook.xml");

        std::ostringstream markdown;
        markdown << "# " << WideToUtf8(path.filename().wstring()) << "\n\n";
        int fallback_index = 1;
        if (sheets.empty()) {
            const std::filesystem::path worksheets_dir = temp / "xl" / "worksheets";
            if (std::filesystem::exists(worksheets_dir)) {
                for (const auto& entry : std::filesystem::directory_iterator(worksheets_dir)) {
                    if (entry.is_regular_file() && LowerExtension(entry.path()) == L".xml") {
                        XlsxSheetInfo info;
                        info.name = WideToUtf8(entry.path().stem().wstring());
                        info.rel_id = WideToUtf8(entry.path().filename().wstring());
                        sheets.push_back(std::move(info));
                    }
                }
            }
        }

        for (const auto& sheet : sheets) {
            std::filesystem::path sheet_path;
            const auto rel = relationships.find(sheet.rel_id);
            if (rel != relationships.end()) {
                sheet_path = temp / std::filesystem::path(Utf8ToWide(rel->second));
            } else if (sheet.rel_id.rfind("sheet", 0) == 0) {
                sheet_path = temp / "xl" / "worksheets" / Utf8ToWide(sheet.rel_id);
            } else {
                sheet_path = temp / "xl" / "worksheets" / (L"sheet" + std::to_wstring(fallback_index++) + L".xml");
            }
            if (!std::filesystem::exists(sheet_path)) {
                continue;
            }
            const std::string sheet_text = ExtractWorksheetMarkdown(sheet_path, shared_strings);
            if (!Trim(sheet_text).empty()) {
                markdown << "## Sheet: " << (sheet.name.empty() ? WideToUtf8(sheet_path.stem().wstring()) : sheet.name) << "\n\n";
                markdown << sheet_text << "\n\n";
            }
        }

        std::error_code ec;
        std::filesystem::remove_all(temp, ec);
        const std::string result = NormalizeMarkdownWhitespace(markdown.str());
        if (result.size() < 8) {
            throw std::runtime_error("XLSX extraction produced no text.");
        }
        return result;
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove_all(temp, ec);
        throw;
    }
}

std::string DecodePdfLiteralString(const std::string& raw) {
    std::string output;
    for (size_t i = 0; i < raw.size(); ++i) {
        char ch = raw[i];
        if (ch != '\\') {
            output.push_back(ch);
            continue;
        }
        if (i + 1 >= raw.size()) {
            break;
        }
        char next = raw[++i];
        switch (next) {
        case 'n':
            output.push_back('\n');
            break;
        case 'r':
            output.push_back('\n');
            break;
        case 't':
            output.push_back('\t');
            break;
        case 'b':
        case 'f':
            output.push_back(' ');
            break;
        case '(':
        case ')':
        case '\\':
            output.push_back(next);
            break;
        default:
            if (next >= '0' && next <= '7') {
                int value = next - '0';
                int count = 1;
                while (count < 3 && i + 1 < raw.size() && raw[i + 1] >= '0' && raw[i + 1] <= '7') {
                    value = value * 8 + (raw[++i] - '0');
                    ++count;
                }
                output.push_back(static_cast<char>(value));
            } else {
                output.push_back(next);
            }
            break;
        }
    }
    return output;
}

bool LooksLikeUsefulPdfText(const std::string& text) {
    const std::string trimmed = Trim(text);
    if (trimmed.size() < 3) {
        return false;
    }
    size_t useful = 0;
    size_t controls = 0;
    for (unsigned char ch : trimmed) {
        if (std::isalnum(ch) || std::isspace(ch) || std::ispunct(ch)) {
            ++useful;
        }
        if (ch < 9 || (ch > 13 && ch < 32)) {
            ++controls;
        }
    }
    return controls < trimmed.size() / 20 && useful > trimmed.size() * 3 / 4;
}

std::string ExtractPdfTextNaive(const std::filesystem::path& path) {
    const std::string data = ReadWholeFile(path);
    std::ostringstream output;
    output << "# " << WideToUtf8(path.filename().wstring()) << "\n\n";
    size_t extracted = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        if (data[i] != '(') {
            continue;
        }
        std::string literal;
        int depth = 1;
        for (++i; i < data.size() && depth > 0; ++i) {
            const char ch = data[i];
            if (ch == '\\') {
                literal.push_back(ch);
                if (i + 1 < data.size()) {
                    literal.push_back(data[++i]);
                }
                continue;
            }
            if (ch == '(') {
                ++depth;
                literal.push_back(ch);
            } else if (ch == ')') {
                --depth;
                if (depth > 0) {
                    literal.push_back(ch);
                }
            } else {
                literal.push_back(ch);
            }
        }
        const std::string decoded = NormalizeMarkdownWhitespace(DecodePdfLiteralString(literal));
        if (LooksLikeUsefulPdfText(decoded)) {
            output << decoded << "\n";
            ++extracted;
        }
    }
    if (extracted == 0) {
        throw std::runtime_error("No PDF text extractor was found and the built-in fallback could not find readable text. Install Poppler pdftotext or MuPDF mutool for reliable PDF extraction.");
    }
    return NormalizeMarkdownWhitespace(output.str());
}

std::optional<std::string> ExtractPdfWithExternalTool(const std::filesystem::path& path, std::string* extractor_id) {
    const std::filesystem::path temp = CreateTempDirectory("rag_pdf");
    const std::filesystem::path output = temp / "pdf.txt";
    try {
        std::string error;
        if (const auto pdftotext = FindExecutableOnPath(L"pdftotext.exe")) {
            const std::wstring command = QuoteCommandArgument(*pdftotext) + L" -layout -enc UTF-8 " + QuoteCommandArgument(path.wstring()) + L" " + QuoteCommandArgument(output.wstring());
            if (RunProcessAndWait(command, 180000, &error) && std::filesystem::exists(output)) {
                const std::string text = NormalizeMarkdownWhitespace(ReadWholeFile(output));
                if (!text.empty()) {
                    if (extractor_id) {
                        *extractor_id = "pdftotext";
                    }
                    std::error_code ec;
                    std::filesystem::remove_all(temp, ec);
                    return "# " + WideToUtf8(path.filename().wstring()) + "\n\n" + text;
                }
            }
        }
        if (const auto mutool = FindExecutableOnPath(L"mutool.exe")) {
            const std::wstring command = QuoteCommandArgument(*mutool) + L" draw -F text -o " + QuoteCommandArgument(output.wstring()) + L" " + QuoteCommandArgument(path.wstring());
            if (RunProcessAndWait(command, 180000, &error) && std::filesystem::exists(output)) {
                const std::string text = NormalizeMarkdownWhitespace(ReadWholeFile(output));
                if (!text.empty()) {
                    if (extractor_id) {
                        *extractor_id = "mutool_text";
                    }
                    std::error_code ec;
                    std::filesystem::remove_all(temp, ec);
                    return "# " + WideToUtf8(path.filename().wstring()) + "\n\n" + text;
                }
            }
        }
        std::error_code ec;
        std::filesystem::remove_all(temp, ec);
        return std::nullopt;
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove_all(temp, ec);
        throw;
    }
}

std::optional<std::string> ExtractPdfWithPythonPypdf(const std::filesystem::path& path, std::string* extractor_id) {
    const auto python = FindExecutableOnPath(L"python.exe");
    if (!python) {
        return std::nullopt;
    }

    const std::filesystem::path temp = CreateTempDirectory("rag_pdf_pypdf");
    const std::filesystem::path script = temp / "extract_pdf.py";
    const std::filesystem::path output = temp / "pdf.txt";
    const std::filesystem::path error_output = temp / "pdf.err";
    try {
        {
            std::ofstream script_file(script, std::ios::binary | std::ios::trunc);
            if (!script_file.is_open()) {
                throw std::runtime_error("Could not write temporary Python PDF extractor.");
            }
            script_file <<
                "import sys\n"
                "from pathlib import Path\n"
                "try:\n"
                "    from pypdf import PdfReader\n"
                "except Exception as exc:\n"
                "    Path(sys.argv[3]).write_text('pypdf import failed: ' + str(exc), encoding='utf-8', errors='replace')\n"
                "    sys.exit(3)\n"
                "pdf_path = Path(sys.argv[1])\n"
                "out_path = Path(sys.argv[2])\n"
                "err_path = Path(sys.argv[3])\n"
                "try:\n"
                "    reader = PdfReader(str(pdf_path))\n"
                "    if getattr(reader, 'is_encrypted', False):\n"
                "        try:\n"
                "            reader.decrypt('')\n"
                "        except Exception:\n"
                "            pass\n"
                "    parts = []\n"
                "    for index, page in enumerate(reader.pages):\n"
                "        try:\n"
                "            text = page.extract_text() or ''\n"
                "        except Exception as exc:\n"
                "            text = ''\n"
                "            parts.append('\\n\\n[Page %d extraction warning: %s]\\n' % (index + 1, exc))\n"
                "        if text.strip():\n"
                "            parts.append('\\n\\n## Page %d\\n\\n%s\\n' % (index + 1, text))\n"
                "    out_path.write_text(''.join(parts), encoding='utf-8', errors='replace')\n"
                "    sys.exit(0 if ''.join(parts).strip() else 4)\n"
                "except Exception as exc:\n"
                "    err_path.write_text('pypdf extraction failed: ' + str(exc), encoding='utf-8', errors='replace')\n"
                "    sys.exit(2)\n";
        }

        std::string error;
        const std::wstring command = QuoteCommandArgument(*python) + L" " +
            QuoteCommandArgument(script.wstring()) + L" " +
            QuoteCommandArgument(path.wstring()) + L" " +
            QuoteCommandArgument(output.wstring()) + L" " +
            QuoteCommandArgument(error_output.wstring());
        if (RunProcessAndWait(command, 180000, &error) && std::filesystem::exists(output)) {
            const std::string text = NormalizeMarkdownWhitespace(ReadWholeFile(output));
            if (!text.empty()) {
                if (extractor_id) {
                    *extractor_id = "python_pypdf";
                }
                std::error_code ec;
                std::filesystem::remove_all(temp, ec);
                return "# " + WideToUtf8(path.filename().wstring()) + "\n\n" + text;
            }
        }

        std::error_code ec;
        std::filesystem::remove_all(temp, ec);
        return std::nullopt;
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove_all(temp, ec);
        throw;
    }
}

std::string ExtractPdfToMarkdown(const std::filesystem::path& path, std::string* extractor_id) {
    if (const auto external = ExtractPdfWithExternalTool(path, extractor_id)) {
        return *external;
    }
    if (const auto pypdf = ExtractPdfWithPythonPypdf(path, extractor_id)) {
        return *pypdf;
    }
    if (extractor_id) {
        *extractor_id = "builtin_pdf_literal_fallback";
    }
    return ExtractPdfTextNaive(path);
}

std::string ExtractRichDocumentToMarkdown(const std::filesystem::path& path, std::string* extractor_id) {
    if (IsHtmlExtension(path)) {
        if (extractor_id) {
            *extractor_id = "native_html_to_markdown";
        }
        return HtmlToMarkdownText(ReadWholeFile(path));
    }
    if (IsDocxExtension(path)) {
        if (extractor_id) {
            *extractor_id = "native_docx_ooxml";
        }
        return ExtractDocxToMarkdown(path);
    }
    if (IsXlsxExtension(path)) {
        if (extractor_id) {
            *extractor_id = "native_xlsx_ooxml";
        }
        return ExtractXlsxToMarkdown(path);
    }
    if (IsPdfExtension(path)) {
        return ExtractPdfToMarkdown(path, extractor_id);
    }
    throw std::runtime_error("No rich document extractor is available for this file type.");
}

bool PythonModuleAvailable(const std::wstring& python_executable, const std::string& module_name) {
    const std::wstring command = QuoteCommandArgument(python_executable) + L" -c " +
        QuoteCommandArgument(L"import " + Utf8ToWide(module_name));
    std::string error;
    return RunProcessAndWait(command, 10000, &error);
}

std::vector<std::string> TokenizeForSearch(const std::string& text) {
    std::vector<std::string> terms;
    std::string current;
    for (unsigned char ch : text) {
        if (std::isalnum(ch) != 0 || ch == '_' || ch == '-' || ch == '\\' || ch == '/' || ch == '.') {
            current.push_back(static_cast<char>(std::tolower(ch)));
        } else if (!current.empty()) {
            if (current.size() > 1) {
                terms.push_back(current);
            }
            current.clear();
        }
    }
    if (current.size() > 1) {
        terms.push_back(current);
    }
    return terms;
}

double ScoreChunk(const std::vector<std::string>& query_terms, const RagChunkRecord& chunk, const RagDocumentRecord* document) {
    if (query_terms.empty()) {
        return 0.0;
    }
    std::string haystack = chunk.text;
    if (document) {
        haystack += "\n";
        haystack += document->display_name;
        haystack += "\n";
        haystack += document->original_source_uri;
    }
    std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    double score = 0.0;
    std::set<std::string> unique_terms(query_terms.begin(), query_terms.end());
    for (const auto& term : unique_terms) {
        size_t pos = haystack.find(term);
        if (pos == std::string::npos) {
            continue;
        }
        score += 1.0;
        while ((pos = haystack.find(term, pos + term.size())) != std::string::npos) {
            score += 0.25;
        }
    }
    return score / std::sqrt(static_cast<double>(std::max<size_t>(1, chunk.text.size() / 500)));
}

std::string ToGenericUtf8(const std::filesystem::path& path) {
    return WideToUtf8(path.generic_wstring());
}

std::wstring SafeFolderName(const std::string& name) {
    std::wstring value = Utf8ToWide(name);
    for (wchar_t& ch : value) {
        if (std::iswalnum(ch) == 0 && ch != L'-' && ch != L'_') {
            ch = L'_';
        }
    }
    value = TrimWide(value);
    while (!value.empty() && value.back() == L'_') {
        value.pop_back();
    }
    if (value.empty()) {
        return L"rag_library";
    }
    if (value.size() > 48) {
        value.resize(48);
    }
    return value;
}

struct RagFileIngestionSource {
    std::filesystem::path source_path;
    std::string original_source_uri;
    std::string display_name;
    std::string metadata_json;
};

std::string SourcePathForError(const RagFileIngestionSource& source) {
    if (!source.display_name.empty()) {
        return source.display_name;
    }
    if (!source.source_path.empty()) {
        return source.source_path.string();
    }
    return "document";
}

std::string DirectFileMetadata(const std::filesystem::path& file) {
    const std::filesystem::path absolute = std::filesystem::absolute(file);
    return json{
        {"ingest_source", "files"},
        {"source_absolute_path", WideToUtf8(absolute.wstring())},
    }.dump();
}

std::string FolderFileMetadata(const std::filesystem::path& root, const std::filesystem::path& file, bool recursive) {
    const std::filesystem::path absolute_root = std::filesystem::absolute(root);
    const std::filesystem::path absolute_file = std::filesystem::absolute(file);
    std::error_code ec;
    std::filesystem::path relative = std::filesystem::relative(absolute_file, absolute_root, ec);
    if (ec || relative.empty()) {
        relative = absolute_file.filename();
    }
    return json{
        {"ingest_source", "folder"},
        {"ingest_root", WideToUtf8(absolute_root.wstring())},
        {"relative_path", ToGenericUtf8(relative)},
        {"source_absolute_path", WideToUtf8(absolute_file.wstring())},
        {"recursive", recursive},
    }.dump();
}

std::string AddExtractionMetadata(const std::string& metadata_json, const std::string& extractor, const std::string& extracted_content_type) {
    json metadata = json::object();
    try {
        metadata = json::parse(metadata_json);
        if (!metadata.is_object()) {
            metadata = json::object();
        }
    } catch (...) {
        metadata = json::object();
    }
    metadata["extractor"] = extractor;
    metadata["extracted_content_type"] = extracted_content_type;
    return metadata.dump();
}

void RemoveManagedExtractedArtifact(const std::filesystem::path& library_path, const std::string& relative_path) {
    if (relative_path.empty()) {
        return;
    }
    const std::filesystem::path target = library_path / std::filesystem::path(Utf8ToWide(relative_path));
    std::error_code ec;
    if (target.filename() == L"manifest.json" && std::filesystem::exists(target.parent_path(), ec)) {
        std::filesystem::remove_all(target.parent_path(), ec);
    } else if (std::filesystem::is_directory(target, ec)) {
        std::filesystem::remove_all(target, ec);
    } else {
        std::filesystem::remove(target, ec);
    }
}

RagImportPreviewItem BuildImportPreviewItem(const RagLibraryConfig& library, const RagImageIngestSettings& image_settings, const std::filesystem::path& file, std::string metadata_json) {
    RagImportPreviewItem item;
    item.source_path = WideToUtf8(std::filesystem::absolute(file).wstring());
    item.display_name = WideToUtf8(file.filename().wstring());
    item.metadata_json = std::move(metadata_json);

    try {
        if (!std::filesystem::exists(file) || !std::filesystem::is_regular_file(file)) {
            item.reason = "Not a regular file.";
            return item;
        }

        item.file_size = std::filesystem::file_size(file);
        if (!IsSupportedTextExtension(file)) {
            item.reason = "Unsupported file type in this RAG build.";
            return item;
        }

        const uintmax_t max_file_size = static_cast<uintmax_t>(std::max(1, library.max_file_size_mb)) * 1024ull * 1024ull;
        if (item.file_size > max_file_size) {
            item.reason = "File is larger than this RAG library's configured max file size.";
            return item;
        }

        const bool image_document = IsImageExtension(file);
        const bool rich_document = IsRichExtractionExtension(file);
        if (image_document) {
            if (!image_settings.enabled) {
                item.reason = "Image file detected, but system-wide image ingestion is disabled.";
                return item;
            }
            const std::string mode = NormalizeImageIngestMode(image_settings.mode);
            const bool tesseract_available = FindExecutableOnPath(L"tesseract.exe").has_value();
            const bool python_available = FindExecutableOnPath(L"python.exe").has_value();
            const bool vision_endpoint_available = NormalizeImageVisionProvider(image_settings.vision_provider) == "ollama" &&
                IsHttpEndpointAvailable(JoinUrlPath(Trim(image_settings.vision_base_url).empty() ? std::string("http://localhost:11434") : Trim(image_settings.vision_base_url), "/api/tags"));
            if (mode == "tesseract_cpu") {
                if (!tesseract_available) {
                    item.reason = "Image ingestion is enabled, but tesseract.exe is missing.";
                    return item;
                }
                item.supported = true;
                item.reason = "Ready to ingest as image Markdown with CPU Tesseract OCR.";
            } else if (mode == "paddle_ocr_gpu") {
                if (!python_available && !tesseract_available) {
                    item.reason = "PaddleOCR mode needs python.exe for PaddleOCR or tesseract.exe for fallback.";
                    return item;
                }
                item.supported = true;
                item.reason = "Ready to ingest as image Markdown with PaddleOCR when available, falling back to Tesseract OCR.";
            } else {
                if (!vision_endpoint_available && !python_available && !tesseract_available) {
                    item.reason = "Full vision mode needs a running Ollama vision endpoint, python.exe for PaddleOCR, or tesseract.exe fallback.";
                    return item;
                }
                item.supported = true;
                item.reason = "Ready to ingest as image Markdown with OCR plus configured vision-language description when available.";
            }
            return item;
        }

        if (!rich_document && !FileLooksLikeText(file)) {
            item.reason = "File appears to be binary.";
            return item;
        }

        item.supported = true;
        if (rich_document) {
            if (IsPdfExtension(file) && !FindExecutableOnPath(L"pdftotext.exe") && !FindExecutableOnPath(L"mutool.exe")) {
                item.reason = FindExecutableOnPath(L"python.exe")
                    ? "Ready to ingest with Python pypdf or built-in PDF fallback. Install pdftotext or mutool for best PDF extraction."
                    : "Ready to ingest with built-in PDF fallback. Install pdftotext, mutool, or Python+pypdf for reliable PDF extraction.";
            } else {
                item.reason = "Ready to ingest with rich document to Markdown extraction.";
            }
        } else {
            item.reason = "Ready to ingest.";
        }
    } catch (const std::exception& ex) {
        item.reason = ex.what();
    } catch (...) {
        item.reason = "Unexpected preview error.";
    }
    return item;
}

void AddPreviewItem(RagImportPreview& preview, RagImportPreviewItem item) {
    ++preview.total_files;
    if (item.supported) {
        ++preview.supported_files;
        preview.supported_bytes += item.file_size;
    } else {
        ++preview.skipped_files;
    }
    preview.items.push_back(std::move(item));
}

bool IngestOneSourceNoLock(sqlite3* db, const RagLibraryConfig& library, const std::filesystem::path& library_path, const RagFileIngestionSource& source, bool skip_unchanged, const RagImageIngestSettings& image_settings, IRagEmbeddingProvider* embedding_provider, RagIngestionResult& result) {
    const std::filesystem::path file = source.source_path;
    const std::string error_prefix = SourcePathForError(source);
    try {
        if (file.empty() || !std::filesystem::exists(file) || !std::filesystem::is_regular_file(file)) {
            ++result.files_skipped;
            result.errors.push_back(error_prefix + ": not a regular file.");
            return false;
        }
        if (!IsSupportedTextExtension(file)) {
            ++result.files_skipped;
            result.errors.push_back(error_prefix + ": unsupported file type in this RAG build.");
            return false;
        }
        const uintmax_t max_file_size = static_cast<uintmax_t>(std::max(1, library.max_file_size_mb)) * 1024ull * 1024ull;
        const uintmax_t file_size = std::filesystem::file_size(file);
        if (file_size > max_file_size) {
            ++result.files_skipped;
            result.errors.push_back(error_prefix + ": file is larger than this RAG library's configured max file size.");
            return false;
        }
        const bool image_document = IsImageExtension(file);
        const bool rich_document = IsRichExtractionExtension(file);
        if (image_document && !image_settings.enabled) {
            ++result.files_skipped;
            result.errors.push_back(error_prefix + ": image ingestion is disabled.");
            return false;
        }
        if (!image_document && !rich_document && !FileLooksLikeText(file)) {
            ++result.files_skipped;
            result.errors.push_back(error_prefix + ": file appears to be binary.");
            return false;
        }

        const std::string original_source_uri = source.original_source_uri.empty()
            ? WideToUtf8(std::filesystem::absolute(file).wstring())
            : source.original_source_uri;
        const std::string content_hash = HashFile(file);
        std::optional<RagDocumentRecord> existing = FindDocumentBySource(db, original_source_uri);
        if (skip_unchanged && existing && existing->content_hash == content_hash) {
            ++result.files_skipped;
            return false;
        }

        RagDocumentRecord document;
        if (existing) {
            document = *existing;
        } else {
            document.id = MakeId("doc");
            document.rag_id = library.id;
            document.imported_at = CurrentTimestampUtc();
        }

        document.display_name = source.display_name.empty() ? WideToUtf8(file.filename().wstring()) : source.display_name;
        document.original_source_uri = original_source_uri;
        document.original_source_type = "file";
        document.content_hash = content_hash;
        document.file_size = file_size;
        document.mime_type = MimeTypeForPath(file);
        std::string extractor_id;
        const std::string extracted_text = image_document
            ? ExtractImageToMarkdown(file, image_settings, &extractor_id)
            : (rich_document ? ExtractRichDocumentToMarkdown(file, &extractor_id) : std::string());
        const bool extracted_markdown_document = rich_document || image_document;
        const bool segmented_extraction = extracted_markdown_document && ShouldSegmentExtractedText(library, extracted_text);
        document.metadata_json = extracted_markdown_document
            ? AddExtractionMetadata(source.metadata_json, extractor_id, "text/markdown")
            : AddExtractionMetadata(source.metadata_json, "plain_text_stream", "text/plain");
        document.last_indexed_at = CurrentTimestampUtc();
        const std::string previous_extracted_relative_path = document.extracted_relative_path;

        ExecSql(db, "BEGIN IMMEDIATE;");
        try {
            if (library.storage_mode == RagDocumentStorageMode::ReferenceInPlace) {
                document.stored_relative_path.clear();
            } else {
                const std::filesystem::path original_relative = std::filesystem::path("documents") / "original" / Utf8ToWide(document.id + "_" + WideToUtf8(file.filename().wstring()));
                const std::filesystem::path original_target = library_path / original_relative;
                std::filesystem::create_directories(original_target.parent_path());
                std::error_code equivalent_error;
                const bool same_file = std::filesystem::exists(original_target) && std::filesystem::equivalent(file, original_target, equivalent_error);
                if (!same_file) {
                    std::filesystem::copy_file(file, original_target, std::filesystem::copy_options::overwrite_existing);
                }
                document.stored_relative_path = ToGenericUtf8(original_relative);
            }

            const std::filesystem::path extracted_relative = segmented_extraction
                ? std::filesystem::path("documents") / "extracted" / Utf8ToWide(document.id) / "manifest.json"
                : std::filesystem::path("documents") / "extracted" / Utf8ToWide(document.id + (extracted_markdown_document ? ".md" : ".txt"));
            const std::filesystem::path extracted_target = library_path / extracted_relative;
            document.extracted_relative_path = ToGenericUtf8(extracted_relative);
            if (!previous_extracted_relative_path.empty() && previous_extracted_relative_path != document.extracted_relative_path) {
                RemoveManagedExtractedArtifact(library_path, previous_extracted_relative_path);
            }

            SaveDocument(db, document);
            DeleteChunksForDocument(db, document.id);
            const int chunks_added = extracted_markdown_document
                ? InsertChunksFromExtractedText(db, library, document, extracted_text, library_path, extracted_relative, embedding_provider, result)
                : InsertChunksFromTextStream(db, library, document, file, extracted_target, embedding_provider, result);
            ExecSql(db, "COMMIT;");

            ++result.files_ingested;
            result.chunks_added += chunks_added;
            return true;
        } catch (...) {
            try {
                ExecSql(db, "ROLLBACK;");
            } catch (...) {
            }
            throw;
        }
    } catch (const std::exception& ex) {
        ++result.files_skipped;
        result.errors.push_back(error_prefix + ": " + ex.what());
        return false;
    } catch (...) {
        ++result.files_skipped;
        result.errors.push_back(error_prefix + ": unexpected ingestion error.");
        return false;
    }
}
}  // namespace

RagService::RagService(AppStorage* storage) : storage_(storage) {}

RagService::~RagService() {
    ShutdownManagedEmbeddingRuntimes();
}

void RagService::ShutdownManagedEmbeddingRuntimes() const {
    if (started_ollama_process_) {
        HANDLE process = static_cast<HANDLE>(started_ollama_process_);
        DWORD exit_code = 0;
        if (GetExitCodeProcess(process, &exit_code) && exit_code == STILL_ACTIVE) {
            AppendEmbeddingRuntimeLogNoLock("Stopping app-managed Ollama process.");
            TerminateProcess(process, 0);
            WaitForSingleObject(process, 3000);
        }
        CloseHandle(process);
        started_ollama_process_ = nullptr;
        started_ollama_process_id_ = 0;
    }
}

std::filesystem::path RagService::EmbeddingRuntimeLogPath() const {
    return storage_->root_path() / "data" / "rag_embedding_runtime.log";
}

std::filesystem::path RagService::ImageIngestSettingsPath() const {
    return storage_->root_path() / "data" / "rag_image_ingest_settings.json";
}

std::filesystem::path RagService::ImageIngestLogPath() const {
    return storage_->root_path() / "data" / "rag_image_ingest_runtime.log";
}

void RagService::AppendEmbeddingRuntimeLogNoLock(const std::string& message) const {
    const std::filesystem::path path = EmbeddingRuntimeLogPath();
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output.is_open()) {
        return;
    }
    output << "[" << CurrentTimestampUtc() << "] " << message << "\r\n";
}

std::string RagService::ReadEmbeddingRuntimeLogTailNoLock(size_t max_bytes) const {
    const std::filesystem::path path = EmbeddingRuntimeLogPath();
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    const std::streamoff offset = size > static_cast<std::streamoff>(max_bytes) ? size - static_cast<std::streamoff>(max_bytes) : 0;
    input.seekg(offset, std::ios::beg);
    std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (offset > 0) {
        text = "[log truncated]\r\n" + text;
    }
    return text;
}

void RagService::AppendImageIngestLogNoLock(const std::string& message) const {
    const std::filesystem::path path = ImageIngestLogPath();
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output.is_open()) {
        return;
    }
    output << "[" << CurrentTimestampUtc() << "] " << message << "\r\n";
}

std::string RagService::ReadImageIngestLogTailNoLock(size_t max_bytes) const {
    const std::filesystem::path path = ImageIngestLogPath();
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    const std::streamoff offset = size > static_cast<std::streamoff>(max_bytes) ? size - static_cast<std::streamoff>(max_bytes) : 0;
    input.seekg(offset, std::ios::beg);
    std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (offset > 0) {
        text = "[log truncated]\r\n" + text;
    }
    return text;
}

RagImageIngestSettings RagService::LoadImageIngestSettingsNoLock() const {
    return RagImageIngestSettingsFromJson(LoadJsonFile(ImageIngestSettingsPath(), RagImageIngestSettingsToJson(RagImageIngestSettings{})));
}

RagImageIngestSettings RagService::LoadImageIngestSettings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return LoadImageIngestSettingsNoLock();
}

void RagService::SaveImageIngestSettings(const RagImageIngestSettings& settings) const {
    std::lock_guard<std::mutex> lock(mutex_);
    SaveJsonFile(ImageIngestSettingsPath(), RagImageIngestSettingsToJson(settings));
    AppendImageIngestLogNoLock("Saved image ingest settings. Mode: " + NormalizeImageIngestMode(settings.mode) + ".");
}

RagImageIngestRuntimeStatus RagService::GetImageIngestRuntimeStatus(const RagImageIngestSettings& settings) const {
    std::lock_guard<std::mutex> lock(mutex_);
    RagImageIngestRuntimeStatus status;
    status.enabled = settings.enabled;
    status.mode = NormalizeImageIngestMode(settings.mode);
    status.log_path = WideToUtf8(ImageIngestLogPath().wstring());
    status.tesseract_installed = FindExecutableOnPath(L"tesseract.exe").has_value();
    status.python_installed = FindExecutableOnPath(L"python.exe").has_value();
    if (const auto python = FindExecutableOnPath(L"python.exe")) {
        status.paddleocr_installed = PythonModuleAvailable(*python, "paddleocr");
    }
    status.ollama_installed = FindExecutableOnPath(L"ollama.exe").has_value();
    const std::string provider = NormalizeImageVisionProvider(settings.vision_provider);
    const std::string base_url = Trim(settings.vision_base_url).empty() ? "http://localhost:11434" : Trim(settings.vision_base_url);
    status.vision_endpoint_running = provider == "ollama" && IsHttpEndpointAvailable(JoinUrlPath(base_url, "/api/tags"));

    if (!settings.enabled) {
        status.message = "Image ingestion is disabled.";
    } else if (status.mode == "tesseract_cpu") {
        status.message = status.tesseract_installed
            ? "CPU image ingestion is ready with Tesseract OCR."
            : "CPU image ingestion needs Tesseract OCR installed.";
    } else if (status.mode == "paddle_ocr_gpu") {
        if (status.paddleocr_installed) {
            status.message = "GPU OCR mode is configured with PaddleOCR. Tesseract remains available as a fallback when installed.";
        } else if (status.tesseract_installed) {
            status.message = "PaddleOCR is missing, but Tesseract OCR fallback is available.";
        } else {
            status.message = "GPU OCR mode needs PaddleOCR or Tesseract fallback installed.";
        }
    } else {
        if (status.vision_endpoint_running && !Trim(settings.vision_model).empty()) {
            status.message = "Full vision mode can call the configured Ollama vision-language endpoint.";
        } else if (status.ollama_installed) {
            status.message = "Full vision mode needs Ollama running and the configured vision model pulled.";
        } else {
            status.message = "Full vision mode needs Ollama and a vision-language model.";
        }
    }
    status.recent_log = ReadImageIngestLogTailNoLock();
    return status;
}

RagExtractionToolInstallResult RagService::LaunchImageIngestToolInstaller(const RagImageIngestSettings& settings, const std::string& tool_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    RagExtractionToolInstallResult result;
    std::string command;
    if (tool_id == "tesseract") {
        command = "winget install --id tesseract-ocr.tesseract -e --accept-package-agreements --accept-source-agreements";
    } else if (tool_id == "paddleocr") {
        const std::string python = Trim(settings.paddle_python_command).empty() ? "python" : Trim(settings.paddle_python_command);
        command = python + " -m pip install paddleocr paddlepaddle";
    } else if (tool_id == "ollama") {
        command = "winget install --id Ollama.Ollama -e --accept-package-agreements --accept-source-agreements";
    }

    if (command.empty()) {
        result.message = "Unknown image ingest tool installer requested.";
        return result;
    }

    result.command = command;
    AppendImageIngestLogNoLock("Launching visible image ingest installer command: " + command);
    const std::wstring shell_command = L"/k " + Utf8ToWide(command);
    HINSTANCE launched = ShellExecuteW(nullptr, L"open", L"cmd.exe", shell_command.c_str(), nullptr, SW_SHOWNORMAL);
    result.launched = reinterpret_cast<intptr_t>(launched) > 32;
    result.message = result.launched ? "Installer command launched in a visible command window." : "Failed to launch installer command.";
    return result;
}

RagExtractionToolInstallResult RagService::LaunchImageVisionModelInstaller(const RagImageIngestSettings& settings) const {
    std::lock_guard<std::mutex> lock(mutex_);
    RagExtractionToolInstallResult result;
    if (NormalizeImageVisionProvider(settings.vision_provider) != "ollama") {
        result.message = "Vision model pull is currently available for Ollama only.";
        return result;
    }
    const std::string model = Trim(settings.vision_model).empty() ? "qwen2.5vl:7b" : Trim(settings.vision_model);
    result.command = "ollama pull " + model;
    AppendImageIngestLogNoLock("Launching visible Ollama vision model pull command: " + result.command);
    const std::wstring shell_command = L"/k " + Utf8ToWide(result.command);
    HINSTANCE launched = ShellExecuteW(nullptr, L"open", L"cmd.exe", shell_command.c_str(), nullptr, SW_SHOWNORMAL);
    result.launched = reinterpret_cast<intptr_t>(launched) > 32;
    result.message = result.launched ? "Vision model pull launched in a visible command window." : "Failed to launch vision model pull command.";
    return result;
}

RagEmbeddingRuntimeStatus RagService::GetEmbeddingRuntimeStatusNoLock(const RagLibraryConfig& library) const {
    RagEmbeddingRuntimeStatus status;
    status.provider = NormalizeRuntimeProvider(library.embedding_provider);
    status.base_url = library.embedding_base_url;
    status.log_path = WideToUtf8(EmbeddingRuntimeLogPath().wstring());
    status.recent_log = ReadEmbeddingRuntimeLogTailNoLock();

    if (status.provider != "ollama") {
        status.supported = false;
        status.message = status.provider == "none"
            ? "No embedding runtime selected."
            : "Runtime lifecycle controls are currently available for Ollama only.";
        return status;
    }

    status.supported = true;
    status.base_url = library.embedding_base_url.empty() ? "http://localhost:11434" : library.embedding_base_url;
    status.install_command = "winget install --id Ollama.Ollama -e";
    status.installed = FindExecutableOnPath(L"ollama.exe").has_value();
    status.running = IsHttpEndpointAvailable(JoinUrlPath(status.base_url, "/api/tags"));
    if (started_ollama_process_) {
        HANDLE process = static_cast<HANDLE>(started_ollama_process_);
        DWORD exit_code = 0;
        status.managed_by_app = GetExitCodeProcess(process, &exit_code) && exit_code == STILL_ACTIVE;
    }
    if (!status.installed) {
        status.message = "Ollama is not installed or ollama.exe is not on PATH.";
    } else if (status.running && status.managed_by_app) {
        status.message = "Ollama is running and was started by this app.";
    } else if (status.running) {
        status.message = "Ollama is running externally.";
    } else {
        status.message = "Ollama is installed but not responding at the configured base URL.";
    }
    return status;
}

RagEmbeddingRuntimeStatus RagService::GetEmbeddingRuntimeStatus(const RagLibraryConfig& library) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return GetEmbeddingRuntimeStatusNoLock(library);
}

RagEmbeddingRuntimeStatus RagService::StartEmbeddingRuntime(const RagLibraryConfig& library) const {
    std::lock_guard<std::mutex> lock(mutex_);
    RagEmbeddingRuntimeStatus status;
    const std::string provider = NormalizeRuntimeProvider(library.embedding_provider);
    if (provider != "ollama") {
        status.provider = provider;
        status.log_path = WideToUtf8(EmbeddingRuntimeLogPath().wstring());
        status.message = "Start is only supported for Ollama.";
        status.recent_log = ReadEmbeddingRuntimeLogTailNoLock();
        return status;
    }
    try {
        AppendEmbeddingRuntimeLogNoLock("Start requested for Ollama at " + (library.embedding_base_url.empty() ? std::string("http://localhost:11434") : library.embedding_base_url) + ".");
        EnsureEmbeddingRuntimeNoLock(library);
        AppendEmbeddingRuntimeLogNoLock("Ollama start/status check completed.");
    } catch (const std::exception& ex) {
        AppendEmbeddingRuntimeLogNoLock(std::string("Ollama start failed: ") + ex.what());
    } catch (...) {
        AppendEmbeddingRuntimeLogNoLock("Ollama start failed unexpectedly.");
    }
    return GetEmbeddingRuntimeStatusNoLock(library);
}

RagEmbeddingRuntimeStatus RagService::StopEmbeddingRuntime(const RagLibraryConfig& library) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string provider = NormalizeRuntimeProvider(library.embedding_provider);
    if (provider == "ollama" && started_ollama_process_) {
        ShutdownManagedEmbeddingRuntimes();
    } else if (provider == "ollama") {
        AppendEmbeddingRuntimeLogNoLock("Stop requested, but Ollama was not started by this app. Leaving external process alone.");
    }
    return GetEmbeddingRuntimeStatusNoLock(library);
}

RagEmbeddingRuntimeStatus RagService::LaunchEmbeddingRuntimeInstaller(const RagLibraryConfig& library) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string provider = NormalizeRuntimeProvider(library.embedding_provider);
    if (provider != "ollama") {
        RagEmbeddingRuntimeStatus status;
        status.provider = provider;
        status.log_path = WideToUtf8(EmbeddingRuntimeLogPath().wstring());
        status.message = "Install helper is only available for Ollama.";
        status.recent_log = ReadEmbeddingRuntimeLogTailNoLock();
        return status;
    }

    AppendEmbeddingRuntimeLogNoLock("Launching visible Ollama installer command: winget install --id Ollama.Ollama -e");
    const std::wstring command = L"/k winget install --id Ollama.Ollama -e";
    HINSTANCE result = ShellExecuteW(nullptr, L"open", L"cmd.exe", command.c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<intptr_t>(result) <= 32) {
        AppendEmbeddingRuntimeLogNoLock("Failed to launch winget installer command.");
    }
    return GetEmbeddingRuntimeStatusNoLock(library);
}

RagEmbeddingRuntimeStatus RagService::LaunchEmbeddingModelInstaller(const RagLibraryConfig& library) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string provider = NormalizeRuntimeProvider(library.embedding_provider);
    if (provider != "ollama") {
        RagEmbeddingRuntimeStatus status;
        status.provider = provider;
        status.log_path = WideToUtf8(EmbeddingRuntimeLogPath().wstring());
        status.message = "Model install helper is only available for Ollama.";
        status.recent_log = ReadEmbeddingRuntimeLogTailNoLock();
        return status;
    }

    const std::string model = library.embedding_model.empty() ? "nomic-embed-text" : library.embedding_model;
    const std::wstring command = L"/k ollama pull " + Utf8ToWide(model);
    AppendEmbeddingRuntimeLogNoLock("Launching visible Ollama model pull command: ollama pull " + model);
    HINSTANCE result = ShellExecuteW(nullptr, L"open", L"cmd.exe", command.c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<intptr_t>(result) <= 32) {
        AppendEmbeddingRuntimeLogNoLock("Failed to launch Ollama model pull command.");
    }
    return GetEmbeddingRuntimeStatusNoLock(library);
}

RagEmbeddingTestResult RagService::TestEmbeddingProvider(const RagLibraryConfig& library) const {
    std::lock_guard<std::mutex> lock(mutex_);
    RagEmbeddingTestResult result;
    result.provider = NormalizeRuntimeProvider(library.embedding_provider);
    result.model = library.embedding_model;
    if (result.provider == "none") {
        result.message = "No embedding provider is selected.";
        result.runtime_status = GetEmbeddingRuntimeStatusNoLock(library);
        return result;
    }

    try {
        AppendEmbeddingRuntimeLogNoLock("Embedding test requested for provider " + result.provider + " model " + result.model + ".");
        EnsureEmbeddingRuntimeNoLock(library);
        std::unique_ptr<IRagEmbeddingProvider> embedding_provider = CreateEmbeddingProvider(library);
        if (!embedding_provider) {
            result.message = "No embedding provider is selected.";
            result.runtime_status = GetEmbeddingRuntimeStatusNoLock(library);
            return result;
        }

        const auto start = std::chrono::steady_clock::now();
        const auto embeddings = embedding_provider->Embed({"RAG embedding diagnostic sample text."});
        const auto end = std::chrono::steady_clock::now();
        result.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
        if (embeddings.empty() || embeddings.front().empty()) {
            throw std::runtime_error("Embedding provider returned an empty vector.");
        }

        result.success = true;
        result.provider = embedding_provider->ProviderId();
        result.model = embedding_provider->ModelId();
        result.dimensions = static_cast<int>(embeddings.front().size());
        std::ostringstream message;
        message << "Embedding test succeeded: " << result.provider << " / " << result.model
                << " returned " << result.dimensions << " dimensions in "
                << std::fixed << std::setprecision(1) << result.elapsed_ms << " ms.";
        result.message = message.str();
        AppendEmbeddingRuntimeLogNoLock(result.message);
    } catch (const std::exception& ex) {
        result.message = std::string("Embedding test failed: ") + ex.what();
        AppendEmbeddingRuntimeLogNoLock(result.message);
    } catch (...) {
        result.message = "Embedding test failed unexpectedly.";
        AppendEmbeddingRuntimeLogNoLock(result.message);
    }
    result.runtime_status = GetEmbeddingRuntimeStatusNoLock(library);
    return result;
}

std::vector<RagExtractionToolStatus> RagService::GetExtractionToolStatus() const {
    std::vector<RagExtractionToolStatus> tools;

    auto add_executable_tool = [&](std::string id, std::string name, std::wstring executable, std::string purpose, std::string install_command, bool recommended, std::string notes) {
        RagExtractionToolStatus tool;
        tool.id = std::move(id);
        tool.name = std::move(name);
        tool.executable = WideToUtf8(executable);
        tool.purpose = std::move(purpose);
        tool.install_command = std::move(install_command);
        tool.notes = std::move(notes);
        tool.installed = FindExecutableOnPath(executable).has_value();
        tool.installable = !tool.install_command.empty();
        tool.recommended = recommended;
        tools.push_back(std::move(tool));
    };

    add_executable_tool(
        "poppler_pdftotext",
        "Poppler pdftotext",
        L"pdftotext.exe",
        "Best current PDF text extraction path for normal text PDFs.",
        "winget install --id oschwartz10612.Poppler -e --accept-package-agreements --accept-source-agreements",
        true,
        "Provides pdftotext.exe. Recommended because the built-in PDF fallback is intentionally limited.");

    add_executable_tool(
        "mupdf_mutool",
        "MuPDF mutool",
        L"mutool.exe",
        "Alternative PDF text extraction path.",
        "",
        false,
        "Optional. If installed manually and available on PATH, the app will use mutool after pdftotext.");

    {
        RagExtractionToolStatus tool;
        tool.id = "python_pypdf";
        tool.name = "Python pypdf";
        tool.executable = "python.exe + pypdf";
        tool.purpose = "Fallback PDF text extraction when pdftotext/mutool are not installed.";
        tool.recommended = false;
        const auto python = FindExecutableOnPath(L"python.exe");
        if (python) {
            tool.installed = PythonModuleAvailable(*python, "pypdf");
            tool.install_command = tool.installed ? "" : "python -m pip install pypdf";
            tool.installable = !tool.installed;
            tool.notes = tool.installed
                ? "Available. The app can use pypdf as a fallback PDF extractor."
                : "Python is installed, but pypdf is missing.";
        } else {
            tool.installed = false;
            tool.install_command = "";
            tool.installable = false;
            tool.notes = "Python was not found on PATH. Install Python and pypdf manually if this fallback is needed.";
        }
        tools.push_back(std::move(tool));
    }

    add_executable_tool(
        "tesseract_ocr",
        "Tesseract OCR",
        L"tesseract.exe",
        "CPU-friendly OCR for image ingestion.",
        "winget install --id tesseract-ocr.tesseract -e --accept-package-agreements --accept-source-agreements",
        true,
        "Recommended for the default image ingestion pipeline. The app converts supported image files into Markdown OCR text while preserving the original image.");

    add_executable_tool(
        "pandoc",
        "Pandoc",
        L"pandoc.exe",
        "Optional high-quality document conversion helper for future import workflows.",
        "winget install --id JohnMacFarlane.Pandoc -e --accept-package-agreements --accept-source-agreements",
        false,
        "Optional future tool. Current DOCX/XLSX extraction uses native OOXML parsing.");

    add_executable_tool(
        "libreoffice",
        "LibreOffice",
        L"soffice.exe",
        "Optional Office conversion helper for unusual Word/Excel files.",
        "winget install --id TheDocumentFoundation.LibreOffice -e --accept-package-agreements --accept-source-agreements",
        false,
        "Optional future tool. Useful if we add external Office conversion fallback.");

    return tools;
}

RagExtractionToolInstallResult RagService::LaunchExtractionToolInstaller(bool recommended_only) const {
    RagExtractionToolInstallResult result;
    const auto winget = FindExecutableOnPath(L"winget.exe");
    const auto tools = GetExtractionToolStatus();
    std::vector<std::string> commands;
    for (const auto& tool : tools) {
        if (tool.installed || !tool.installable || tool.install_command.empty()) {
            continue;
        }
        if (recommended_only && !tool.recommended) {
            continue;
        }
        if (tool.install_command.rfind("winget ", 0) == 0 && !winget) {
            continue;
        }
        commands.push_back(tool.install_command);
    }

    if (commands.empty()) {
        result.message = recommended_only
            ? "No missing recommended tools with an available installer were found."
            : "No missing installable tools were found.";
        return result;
    }

    std::ostringstream command_stream;
    for (size_t i = 0; i < commands.size(); ++i) {
        if (i > 0) {
            command_stream << " && ";
        }
        command_stream << commands[i];
    }
    result.command = command_stream.str();
    const std::wstring command = L"/k " + Utf8ToWide(result.command);
    HINSTANCE launched = ShellExecuteW(nullptr, L"open", L"cmd.exe", command.c_str(), nullptr, SW_SHOWNORMAL);
    result.launched = reinterpret_cast<intptr_t>(launched) > 32;
    result.message = result.launched
        ? "Installer command launched in a visible command window."
        : "Failed to launch installer command.";
    return result;
}

void RagService::EnsureEmbeddingRuntimeNoLock(const RagLibraryConfig& library) const {
    std::string provider = NormalizeRuntimeProvider(library.embedding_provider);
    if (provider != "ollama") {
        return;
    }

    const std::string base_url = library.embedding_base_url.empty() ? "http://localhost:11434" : library.embedding_base_url;
    if (IsHttpEndpointAvailable(JoinUrlPath(base_url, "/api/tags"))) {
        return;
    }

    if (started_ollama_process_) {
        HANDLE process = static_cast<HANDLE>(started_ollama_process_);
        DWORD exit_code = 0;
        if (GetExitCodeProcess(process, &exit_code) && exit_code == STILL_ACTIVE) {
            Sleep(1200);
            if (IsHttpEndpointAvailable(JoinUrlPath(base_url, "/api/tags"))) {
                return;
            }
        } else {
            CloseHandle(process);
            started_ollama_process_ = nullptr;
            started_ollama_process_id_ = 0;
        }
    }

    const auto executable = FindExecutableOnPath(L"ollama.exe");
    if (!executable) {
        AppendEmbeddingRuntimeLogNoLock("Ollama executable was not found on PATH.");
        throw std::runtime_error("Ollama is selected but ollama.exe was not found on PATH. Install it first, for example with: winget install Ollama.Ollama");
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    std::filesystem::create_directories(EmbeddingRuntimeLogPath().parent_path());
    HANDLE log_handle = CreateFileW(
        EmbeddingRuntimeLogPath().wstring().c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (log_handle != INVALID_HANDLE_VALUE) {
        startup.dwFlags |= STARTF_USESTDHANDLES;
        startup.hStdOutput = log_handle;
        startup.hStdError = log_handle;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    PROCESS_INFORMATION process_info{};
    std::wstring command_line = QuoteCommandArgument(*executable) + L" serve";
    std::vector<wchar_t> command_buffer(command_line.begin(), command_line.end());
    command_buffer.push_back(L'\0');
    std::wstring previous_ollama_host;
    DWORD previous_size = GetEnvironmentVariableW(L"OLLAMA_HOST", nullptr, 0);
    const bool had_previous_ollama_host = previous_size > 0;
    if (had_previous_ollama_host) {
        previous_ollama_host.resize(previous_size, L'\0');
        DWORD written = GetEnvironmentVariableW(L"OLLAMA_HOST", previous_ollama_host.data(), previous_size);
        previous_ollama_host.resize(written);
    }
    try {
        const ParsedUrl parsed = CrackUrl(base_url);
        std::wstring ollama_host = parsed.host + L":" + std::to_wstring(parsed.port);
        SetEnvironmentVariableW(L"OLLAMA_HOST", ollama_host.c_str());
    } catch (...) {
    }
    AppendEmbeddingRuntimeLogNoLock("Starting Ollama with command: " + WideToUtf8(command_line));
    if (!CreateProcessW(nullptr, command_buffer.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process_info)) {
        if (log_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(log_handle);
        }
        if (had_previous_ollama_host) {
            SetEnvironmentVariableW(L"OLLAMA_HOST", previous_ollama_host.c_str());
        } else {
            SetEnvironmentVariableW(L"OLLAMA_HOST", nullptr);
        }
        throw std::runtime_error("Could not start Ollama. Try starting it manually with: ollama serve");
    }
    if (log_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(log_handle);
    }
    if (had_previous_ollama_host) {
        SetEnvironmentVariableW(L"OLLAMA_HOST", previous_ollama_host.c_str());
    } else {
        SetEnvironmentVariableW(L"OLLAMA_HOST", nullptr);
    }

    CloseHandle(process_info.hThread);
    started_ollama_process_ = process_info.hProcess;
    started_ollama_process_id_ = process_info.dwProcessId;

    for (int attempt = 0; attempt < 20; ++attempt) {
        Sleep(500);
        if (IsHttpEndpointAvailable(JoinUrlPath(base_url, "/api/tags"))) {
            AppendEmbeddingRuntimeLogNoLock("Ollama is responding at " + base_url + ".");
            return;
        }
    }
    AppendEmbeddingRuntimeLogNoLock("Ollama process started but did not become ready at " + base_url + ".");
    throw std::runtime_error("Started Ollama, but it did not become ready at " + base_url + ". Try running: ollama serve");
}

void RagService::EnsureInitialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::filesystem::create_directories(RagRoot());
    if (!std::filesystem::exists(RegistryPath())) {
        SaveJsonFile(RegistryPath(), json{{"libraries", json::array()}});
    }
    if (!std::filesystem::exists(ImageIngestSettingsPath())) {
        SaveJsonFile(ImageIngestSettingsPath(), RagImageIngestSettingsToJson(RagImageIngestSettings{}));
    }
    for (const auto& [rag_id, library_path] : LoadRegistryNoLock()) {
        const json data = LoadJsonFile(library_path / "rag.json", json::object());
        if (!data.is_object() || data.empty()) {
            continue;
        }
        const RagLibraryConfig library = RagLibraryFromJson(data, rag_id);
        try {
            EnsureEmbeddingRuntimeNoLock(library);
        } catch (...) {
            // Startup should remain usable even if an optional embedding runtime cannot be started.
        }
    }
}

std::filesystem::path RagService::RagRoot() const {
    return storage_->root_path() / "data" / "rag_libraries";
}

std::filesystem::path RagService::LibraryPath(const std::string& rag_id) const {
    for (const auto& [id, path] : LoadRegistryNoLock()) {
        if (id == rag_id && !path.empty()) {
            return path;
        }
    }
    return RagRoot() / Utf8ToWide(rag_id);
}

std::filesystem::path RagService::LibraryConfigPath(const std::string& rag_id) const {
    return LibraryPath(rag_id) / "rag.json";
}

std::filesystem::path RagService::LibraryCatalogPath(const std::string& rag_id) const {
    return LibraryPath(rag_id) / "rag_catalog.json";
}

std::filesystem::path RagService::RegistryPath() const {
    return RagRoot() / "rag_libraries.json";
}

std::filesystem::path RagService::ProjectRagBindingsPath(const std::string& project_id) const {
    return storage_->root_path() / "data" / "projects" / Utf8ToWide(project_id) / "project_rag.json";
}

std::vector<std::pair<std::string, std::filesystem::path>> RagService::LoadRegistryNoLock() const {
    std::vector<std::pair<std::string, std::filesystem::path>> entries;
    const json data = LoadJsonFile(RegistryPath(), json{{"libraries", json::array()}});
    if (!data.contains("libraries") || !data["libraries"].is_array()) {
        return entries;
    }
    for (const auto& item : data["libraries"]) {
        const std::string id = item.value("id", "");
        const std::string path = item.value("storage_path", "");
        if (!id.empty() && !path.empty()) {
            entries.emplace_back(id, std::filesystem::path(Utf8ToWide(path)));
        }
    }
    return entries;
}

void RagService::SaveRegistryNoLock(const std::vector<std::pair<std::string, std::filesystem::path>>& entries) const {
    json data;
    data["libraries"] = json::array();
    for (const auto& [id, path] : entries) {
        if (!id.empty() && !path.empty()) {
            data["libraries"].push_back({
                {"id", id},
                {"storage_path", WideToUtf8(std::filesystem::absolute(path).wstring())},
            });
        }
    }
    SaveJsonFile(RegistryPath(), data);
}

void RagService::UpsertRegistryNoLock(const std::string& rag_id, const std::filesystem::path& path) const {
    auto entries = LoadRegistryNoLock();
    const auto absolute_path = std::filesystem::absolute(path);
    auto it = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
        return entry.first == rag_id;
    });
    if (it == entries.end()) {
        entries.emplace_back(rag_id, absolute_path);
    } else {
        it->second = absolute_path;
    }
    SaveRegistryNoLock(entries);
}

void RagService::RemoveRegistryNoLock(const std::string& rag_id) const {
    auto entries = LoadRegistryNoLock();
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const auto& entry) {
                      return entry.first == rag_id;
                  }),
                  entries.end());
    SaveRegistryNoLock(entries);
}

std::vector<RagLibraryConfig> RagService::ListLibraries() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::filesystem::create_directories(RagRoot());
    std::vector<RagLibraryConfig> libraries;
    std::set<std::string> seen_ids;
    for (const auto& [rag_id, library_path] : LoadRegistryNoLock()) {
        const json data = LoadJsonFile(library_path / "rag.json", json::object());
        if (!data.is_object() || data.empty()) {
            continue;
        }
        RagLibraryConfig config = RagLibraryFromJson(data, rag_id);
        if (!config.id.empty()) {
            config.storage_path = WideToUtf8(std::filesystem::absolute(library_path).wstring());
            libraries.push_back(std::move(config));
            seen_ids.insert(rag_id);
        }
    }
    for (const auto& entry : std::filesystem::directory_iterator(RagRoot())) {
        if (!entry.is_directory()) {
            continue;
        }
        const std::string rag_id = WideToUtf8(entry.path().filename().wstring());
        if (seen_ids.find(rag_id) != seen_ids.end()) {
            continue;
        }
        const json data = LoadJsonFile(LibraryConfigPath(rag_id), json::object());
        if (!data.is_object() || data.empty()) {
            continue;
        }
        RagLibraryConfig config = RagLibraryFromJson(data, rag_id);
        if (!config.id.empty()) {
            config.storage_path = WideToUtf8(std::filesystem::absolute(entry.path()).wstring());
            libraries.push_back(std::move(config));
        }
    }
    std::sort(libraries.begin(), libraries.end(), [](const RagLibraryConfig& left, const RagLibraryConfig& right) {
        return left.name < right.name;
    });
    return libraries;
}

std::optional<RagLibraryConfig> RagService::GetLibrary(const std::string& rag_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const json data = LoadJsonFile(LibraryConfigPath(rag_id), json::object());
    if (!data.is_object() || data.empty()) {
        return std::nullopt;
    }
    RagLibraryConfig config = RagLibraryFromJson(data, rag_id);
    config.storage_path = WideToUtf8(std::filesystem::absolute(LibraryPath(rag_id)).wstring());
    return config;
}

RagLibraryConfig RagService::CreateLibrary(const RagLibraryConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    RagLibraryConfig created = config;
    created.id = MakeId("rag");
    created.created_at = CurrentTimestampUtc();
    created.updated_at = created.created_at;
    if (created.name.empty()) {
        created.name = "New RAG Library";
    }

    const std::filesystem::path storage_parent = created.storage_path.empty()
        ? RagRoot()
        : std::filesystem::path(Utf8ToWide(created.storage_path));
    const std::filesystem::path library_path = std::filesystem::absolute(storage_parent / (SafeFolderName(created.name) + L"_" + Utf8ToWide(created.id)));
    created.storage_path = WideToUtf8(library_path.wstring());

    UpsertRegistryNoLock(created.id, library_path);
    std::filesystem::create_directories(library_path / "documents" / "original");
    std::filesystem::create_directories(library_path / "documents" / "extracted");
    std::filesystem::create_directories(library_path / "indexes");
    SaveJsonFile(library_path / "rag.json", RagLibraryToJson(created));
    {
        auto db = OpenDatabase(library_path / "rag.sqlite");
        EnsureRagDatabase(db.get());
    }
    SaveCatalogNoLock(created.id, RagCatalog{});
    return created;
}

bool RagService::UpdateLibrary(const RagLibraryConfig& config, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (config.id.empty() || !std::filesystem::exists(LibraryPath(config.id))) {
        if (error) {
            *error = "RAG library not found.";
        }
        return false;
    }
    const std::filesystem::path library_path = LibraryPath(config.id);
    const RagLibraryConfig previous = RagLibraryFromJson(LoadJsonFile(library_path / "rag.json", json::object()), config.id);
    RagLibraryConfig updated = config;
    updated.storage_path = WideToUtf8(std::filesystem::absolute(library_path).wstring());
    updated.updated_at = CurrentTimestampUtc();
    const bool embedding_changed = previous.embedding_provider != updated.embedding_provider ||
        previous.embedding_base_url != updated.embedding_base_url ||
        previous.embedding_model != updated.embedding_model ||
        previous.embedding_dimensions != updated.embedding_dimensions ||
        previous.vector_backend != updated.vector_backend;
    const bool chunking_changed = previous.chunk_size_chars != updated.chunk_size_chars ||
        previous.chunk_overlap_chars != updated.chunk_overlap_chars;
    const bool segmentation_changed = previous.split_large_extracted_documents != updated.split_large_extracted_documents ||
        previous.extracted_segment_threshold_mb != updated.extracted_segment_threshold_mb ||
        previous.extracted_segment_size_mb != updated.extracted_segment_size_mb ||
        previous.extracted_segment_overlap_chars != updated.extracted_segment_overlap_chars;
    if (embedding_changed || chunking_changed || segmentation_changed) {
        updated.rebuild_required = true;
        updated.rebuild_reason = embedding_changed && chunking_changed
            ? "Embedding/vector and chunking settings changed. Rebuild DB to refresh chunks and vectors."
            : embedding_changed
                ? "Embedding/vector settings changed. Rebuild DB to refresh vectors."
                : chunking_changed
                    ? "Chunking settings changed. Rebuild DB to refresh chunks."
                    : "Extracted document segmentation settings changed. Rebuild DB to refresh extracted segment files and chunk metadata.";
    }
    UpsertRegistryNoLock(updated.id, library_path);
    SaveJsonFile(library_path / "rag.json", RagLibraryToJson(updated));
    {
        auto db = OpenDatabase(library_path / "rag.sqlite");
        EnsureRagDatabase(db.get());
    }
    return true;
}

bool RagService::DeleteLibrary(const std::string& rag_id, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (rag_id.empty() || !std::filesystem::exists(LibraryPath(rag_id))) {
        if (error) {
            *error = "RAG library not found.";
        }
        return false;
    }
    const std::filesystem::path library_path = LibraryPath(rag_id);
    std::filesystem::remove_all(library_path);
    RemoveRegistryNoLock(rag_id);
    return true;
}

RagLibraryStats RagService::GetStats(const std::string& rag_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    RagLibraryStats stats;
    try {
        auto db = OpenDatabase(LibraryPath(rag_id) / "rag.sqlite");
        EnsureRagDatabase(db.get());
        {
            auto statement = PrepareSql(db.get(), "SELECT COUNT(*), COALESCE(SUM(file_size), 0) FROM documents WHERE deleted = 0;");
            if (sqlite3_step(statement.get()) == SQLITE_ROW) {
                stats.document_count = static_cast<size_t>(sqlite3_column_int64(statement.get(), 0));
                stats.original_bytes = static_cast<uintmax_t>(sqlite3_column_int64(statement.get(), 1));
            }
        }
        {
            auto statement = PrepareSql(db.get(), "SELECT COUNT(*) FROM chunks;");
            if (sqlite3_step(statement.get()) == SQLITE_ROW) {
                stats.chunk_count = static_cast<size_t>(sqlite3_column_int64(statement.get(), 0));
            }
        }
        {
            auto statement = PrepareSql(db.get(), "SELECT COUNT(*) FROM embeddings;");
            if (sqlite3_step(statement.get()) == SQLITE_ROW) {
                stats.embedding_count = static_cast<size_t>(sqlite3_column_int64(statement.get(), 0));
            }
        }
    } catch (...) {
        const RagCatalog catalog = LoadCatalogNoLock(rag_id);
        stats.document_count = catalog.documents.size();
        stats.chunk_count = catalog.chunks.size();
        for (const auto& document : catalog.documents) {
            stats.original_bytes += document.file_size;
        }
    }
    return stats;
}

std::vector<RagDocumentSummary> RagService::ListDocuments(const std::string& rag_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<RagDocumentSummary> summaries;
    try {
        auto db = OpenDatabase(LibraryPath(rag_id) / "rag.sqlite");
        EnsureRagDatabase(db.get());
        const std::vector<RagDocumentRecord> documents = LoadActiveDocuments(db.get());
        summaries.reserve(documents.size());
        for (const auto& document : documents) {
            RagDocumentSummary summary;
            summary.document = document;
            {
                auto statement = PrepareSql(db.get(), "SELECT COUNT(*) FROM chunks WHERE document_id = ?;");
                BindText(statement.get(), 1, document.id);
                if (sqlite3_step(statement.get()) == SQLITE_ROW) {
                    summary.chunk_count = sqlite3_column_int(statement.get(), 0);
                }
            }
            {
                auto statement = PrepareSql(db.get(),
                    "SELECT COUNT(*) "
                    "FROM embeddings e "
                    "JOIN chunks c ON c.id = e.chunk_id "
                    "WHERE c.document_id = ?;");
                BindText(statement.get(), 1, document.id);
                if (sqlite3_step(statement.get()) == SQLITE_ROW) {
                    summary.embedding_count = sqlite3_column_int(statement.get(), 0);
                }
            }
            summaries.push_back(std::move(summary));
        }
    } catch (...) {
        const RagCatalog catalog = LoadCatalogNoLock(rag_id);
        summaries.reserve(catalog.documents.size());
        for (const auto& document : catalog.documents) {
            RagDocumentSummary summary;
            summary.document = document;
            summary.chunk_count = static_cast<int>(std::count_if(catalog.chunks.begin(), catalog.chunks.end(), [&](const RagChunkRecord& chunk) {
                return chunk.document_id == document.id;
            }));
            summaries.push_back(std::move(summary));
        }
    }
    return summaries;
}

std::optional<RagDocumentRecord> RagService::GetDocument(const std::string& rag_id, const std::string& document_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        const json library_json = LoadJsonFile(LibraryConfigPath(rag_id), json::object());
        if (!library_json.is_object() || library_json.empty()) {
            return std::nullopt;
        }

        auto db = OpenDatabase(LibraryPath(rag_id) / "rag.sqlite");
        EnsureRagDatabase(db.get());
        auto document = FindDocumentById(db.get(), document_id);
        if (!document || document->rag_id != rag_id) {
            return std::nullopt;
        }
        return document;
    } catch (...) {
        return std::nullopt;
    }
}

std::string RagService::LoadDocumentText(const std::string& rag_id, const std::string& document_id, size_t max_chars, bool* truncated, std::string* error) const {
    if (truncated) {
        *truncated = false;
    }
    if (error) {
        error->clear();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    try {
        const json library_json = LoadJsonFile(LibraryConfigPath(rag_id), json::object());
        if (!library_json.is_object() || library_json.empty()) {
            if (error) {
                *error = "RAG library not found.";
            }
            return {};
        }

        const std::filesystem::path library_path = LibraryPath(rag_id);
        auto db = OpenDatabase(library_path / "rag.sqlite");
        EnsureRagDatabase(db.get());
        const auto document = FindDocumentById(db.get(), document_id);
        if (!document || document->rag_id != rag_id) {
            if (error) {
                *error = "Document not found.";
            }
            return {};
        }
        if (document->extracted_relative_path.empty()) {
            if (error) {
                *error = "Document does not have extracted text.";
            }
            return {};
        }

        const size_t limit = max_chars == 0 ? std::numeric_limits<size_t>::max() : max_chars;
        std::string combined;
        auto append_text = [&](const std::string& value) {
            if (combined.size() >= limit) {
                if (truncated) {
                    *truncated = true;
                }
                return;
            }
            const size_t remaining = limit - combined.size();
            if (value.size() > remaining) {
                combined.append(value.data(), static_cast<std::string::size_type>(remaining));
                if (truncated) {
                    *truncated = true;
                }
            } else {
                combined += value;
            }
        };

        const std::filesystem::path extracted_path = library_path / std::filesystem::path(Utf8ToWide(document->extracted_relative_path));
        if (!std::filesystem::exists(extracted_path)) {
            if (error) {
                *error = "Extracted text file was not found.";
            }
            return {};
        }

        if (extracted_path.filename() == L"manifest.json") {
            const json manifest = LoadJsonFile(extracted_path, json::object());
            if (!manifest.contains("segments") || !manifest["segments"].is_array()) {
                if (error) {
                    *error = "Extracted segment manifest is invalid.";
                }
                return {};
            }

            for (const auto& segment : manifest["segments"]) {
                const std::string relative_path = segment.value("relative_path", "");
                if (relative_path.empty()) {
                    continue;
                }
                const std::filesystem::path segment_path = library_path / std::filesystem::path(Utf8ToWide(relative_path));
                if (!std::filesystem::exists(segment_path)) {
                    continue;
                }
                if (!combined.empty()) {
                    append_text("\n\n");
                }
                append_text(ReadWholeFile(segment_path));
                if (truncated && *truncated) {
                    break;
                }
            }
        } else {
            append_text(ReadWholeFile(extracted_path));
        }

        return combined;
    } catch (const std::exception& ex) {
        if (error) {
            *error = ex.what();
        }
    } catch (...) {
        if (error) {
            *error = "Unexpected error while reading document text.";
        }
    }
    return {};
}

RagImportPreview RagService::PreviewFiles(const std::string& rag_id, const std::vector<std::filesystem::path>& files) const {
    std::lock_guard<std::mutex> lock(mutex_);
    RagImportPreview preview;
    const json library_json = LoadJsonFile(LibraryConfigPath(rag_id), json::object());
    if (!library_json.is_object() || library_json.empty()) {
        preview.errors.push_back("RAG library not found.");
        return preview;
    }

    const RagLibraryConfig library = RagLibraryFromJson(library_json, rag_id);
    const RagImageIngestSettings image_settings = LoadImageIngestSettingsNoLock();
    for (const auto& file : files) {
        AddPreviewItem(preview, BuildImportPreviewItem(library, image_settings, file, DirectFileMetadata(file)));
    }
    preview.success = preview.errors.empty();
    return preview;
}

RagImportPreview RagService::PreviewFolder(const std::string& rag_id, const std::filesystem::path& folder, bool recursive) const {
    std::lock_guard<std::mutex> lock(mutex_);
    RagImportPreview preview;
    const json library_json = LoadJsonFile(LibraryConfigPath(rag_id), json::object());
    if (!library_json.is_object() || library_json.empty()) {
        preview.errors.push_back("RAG library not found.");
        return preview;
    }
    if (!std::filesystem::exists(folder) || !std::filesystem::is_directory(folder)) {
        preview.errors.push_back(folder.string() + ": folder does not exist.");
        return preview;
    }

    const RagLibraryConfig library = RagLibraryFromJson(library_json, rag_id);
    const RagImageIngestSettings image_settings = LoadImageIngestSettingsNoLock();
    try {
        if (recursive) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied)) {
                if (entry.is_regular_file()) {
                    AddPreviewItem(preview, BuildImportPreviewItem(library, image_settings, entry.path(), FolderFileMetadata(folder, entry.path(), recursive)));
                }
            }
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied)) {
                if (entry.is_regular_file()) {
                    AddPreviewItem(preview, BuildImportPreviewItem(library, image_settings, entry.path(), FolderFileMetadata(folder, entry.path(), recursive)));
                }
            }
        }
    } catch (const std::exception& ex) {
        preview.errors.push_back(folder.string() + ": " + ex.what());
    } catch (...) {
        preview.errors.push_back(folder.string() + ": unexpected preview error.");
    }
    preview.success = preview.errors.empty();
    return preview;
}

RagIngestionResult RagService::ReindexDocument(const std::string& rag_id, const std::string& document_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    RagIngestionResult result;
    const json library_json = LoadJsonFile(LibraryConfigPath(rag_id), json::object());
    if (!library_json.is_object() || library_json.empty()) {
        result.errors.push_back("RAG library not found.");
        return result;
    }

    const RagLibraryConfig library = RagLibraryFromJson(library_json, rag_id);
    const std::filesystem::path library_path = LibraryPath(rag_id);
    auto db = OpenDatabase(library_path / "rag.sqlite");
    EnsureRagDatabase(db.get());
    const auto document = FindDocumentById(db.get(), document_id);
    if (!document) {
        result.errors.push_back("Document not found.");
        return result;
    }

    std::unique_ptr<IRagEmbeddingProvider> embedding_provider;
    try {
        EnsureEmbeddingRuntimeNoLock(library);
        embedding_provider = CreateEmbeddingProvider(library);
    } catch (const std::exception& ex) {
        result.errors.push_back(ex.what());
        return result;
    }
    const RagImageIngestSettings image_settings = LoadImageIngestSettingsNoLock();

    RagFileIngestionSource source;
    source.source_path = ResolveRebuildSourcePath(library_path, *document);
    source.original_source_uri = document->original_source_uri;
    source.display_name = document->display_name;
    source.metadata_json = document->metadata_json;
    IngestOneSourceNoLock(db.get(), library, library_path, source, false, image_settings, embedding_provider.get(), result);
    result.success = result.errors.empty();
    return result;
}

bool RagService::DeleteDocument(const std::string& rag_id, const std::string& document_id, bool delete_managed_files, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    const json library_json = LoadJsonFile(LibraryConfigPath(rag_id), json::object());
    if (!library_json.is_object() || library_json.empty()) {
        if (error) {
            *error = "RAG library not found.";
        }
        return false;
    }

    const std::filesystem::path library_path = LibraryPath(rag_id);
    auto db = OpenDatabase(library_path / "rag.sqlite");
    EnsureRagDatabase(db.get());
    const auto document = FindDocumentById(db.get(), document_id);
    if (!document) {
        if (error) {
            *error = "Document not found.";
        }
        return false;
    }

    try {
        ExecSql(db.get(), "BEGIN IMMEDIATE;");
        DeleteChunksForDocument(db.get(), document_id);
        auto statement = PrepareSql(db.get(), "DELETE FROM documents WHERE id = ?;");
        BindText(statement.get(), 1, document_id);
        if (sqlite3_step(statement.get()) != SQLITE_DONE) {
            ThrowSqlite(db.get(), "Could not delete RAG document");
        }
        ExecSql(db.get(), "COMMIT;");
    } catch (const std::exception& ex) {
        try {
            ExecSql(db.get(), "ROLLBACK;");
        } catch (...) {
        }
        if (error) {
            *error = ex.what();
        }
        return false;
    } catch (...) {
        try {
            ExecSql(db.get(), "ROLLBACK;");
        } catch (...) {
        }
        if (error) {
            *error = "Unexpected document delete error.";
        }
        return false;
    }

    if (delete_managed_files) {
        std::error_code ec;
        if (!document->stored_relative_path.empty()) {
            std::filesystem::remove(library_path / std::filesystem::path(Utf8ToWide(document->stored_relative_path)), ec);
        }
        RemoveManagedExtractedArtifact(library_path, document->extracted_relative_path);
    }
    return true;
}

std::vector<ProjectRagBinding> RagService::LoadProjectBindings(const std::string& project_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ProjectRagBinding> bindings;
    const json data = LoadJsonFile(ProjectRagBindingsPath(project_id), json{{"bindings", json::array()}});
    if (data.contains("bindings") && data["bindings"].is_array()) {
        for (const auto& item : data["bindings"]) {
            ProjectRagBinding binding = ProjectRagBindingFromJson(item);
            if (!binding.rag_id.empty()) {
                bindings.push_back(std::move(binding));
            }
        }
    }
    return bindings;
}

void RagService::SaveProjectBindings(const std::string& project_id, const std::vector<ProjectRagBinding>& bindings) const {
    std::lock_guard<std::mutex> lock(mutex_);
    json data;
    data["bindings"] = json::array();
    for (const auto& binding : bindings) {
        if (!binding.rag_id.empty()) {
            data["bindings"].push_back(ProjectRagBindingToJson(binding));
        }
    }
    SaveJsonFile(ProjectRagBindingsPath(project_id), data);
}

bool RagService::UpsertProjectBinding(const std::string& project_id, const ProjectRagBinding& binding, std::string* error) {
    if (project_id.empty()) {
        if (error) {
            *error = "Select a project first.";
        }
        return false;
    }
    auto bindings = LoadProjectBindings(project_id);
    auto it = std::find_if(bindings.begin(), bindings.end(), [&](const ProjectRagBinding& item) { return item.rag_id == binding.rag_id; });
    if (it == bindings.end()) {
        bindings.push_back(binding);
    } else {
        *it = binding;
    }
    SaveProjectBindings(project_id, bindings);
    return true;
}

bool RagService::RemoveProjectBinding(const std::string& project_id, const std::string& rag_id, std::string* error) {
    if (project_id.empty()) {
        if (error) {
            *error = "Select a project first.";
        }
        return false;
    }
    auto bindings = LoadProjectBindings(project_id);
    const auto old_size = bindings.size();
    bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [&](const ProjectRagBinding& binding) { return binding.rag_id == rag_id; }), bindings.end());
    if (bindings.size() == old_size) {
        if (error) {
            *error = "This RAG is not attached to the active project.";
        }
        return false;
    }
    SaveProjectBindings(project_id, bindings);
    return true;
}

RagService::RagCatalog RagService::LoadCatalogNoLock(const std::string& rag_id) const {
    RagCatalog catalog;
    const json data = LoadJsonFile(LibraryCatalogPath(rag_id), json{{"documents", json::array()}, {"chunks", json::array()}});
    if (data.contains("documents") && data["documents"].is_array()) {
        for (const auto& item : data["documents"]) {
            RagDocumentRecord document = RagDocumentFromJson(item);
            if (!document.id.empty()) {
                catalog.documents.push_back(std::move(document));
            }
        }
    }
    if (data.contains("chunks") && data["chunks"].is_array()) {
        for (const auto& item : data["chunks"]) {
            RagChunkRecord chunk = RagChunkFromJson(item);
            if (!chunk.id.empty()) {
                catalog.chunks.push_back(std::move(chunk));
            }
        }
    }
    return catalog;
}

void RagService::SaveCatalogNoLock(const std::string& rag_id, const RagCatalog& catalog) const {
    json data;
    data["documents"] = json::array();
    data["chunks"] = json::array();
    for (const auto& document : catalog.documents) {
        data["documents"].push_back(RagDocumentToJson(document));
    }
    for (const auto& chunk : catalog.chunks) {
        data["chunks"].push_back(RagChunkToJson(chunk));
    }
    SaveJsonFile(LibraryCatalogPath(rag_id), data);
}

RagIngestionResult RagService::IngestFiles(const std::string& rag_id, const std::vector<std::filesystem::path>& files) {
    std::lock_guard<std::mutex> lock(mutex_);
    RagIngestionResult result;
    const json library_json = LoadJsonFile(LibraryConfigPath(rag_id), json::object());
    if (!library_json.is_object() || library_json.empty()) {
        result.errors.push_back("RAG library not found.");
        return result;
    }
    const RagLibraryConfig library = RagLibraryFromJson(library_json, rag_id);
    const std::filesystem::path library_path = LibraryPath(rag_id);
    auto db = OpenDatabase(library_path / "rag.sqlite");
    EnsureRagDatabase(db.get());
    std::unique_ptr<IRagEmbeddingProvider> embedding_provider;
    try {
        EnsureEmbeddingRuntimeNoLock(library);
        embedding_provider = CreateEmbeddingProvider(library);
    } catch (const std::exception& ex) {
        result.errors.push_back(ex.what());
        return result;
    }
    const RagImageIngestSettings image_settings = LoadImageIngestSettingsNoLock();

    for (const auto& file : files) {
        RagFileIngestionSource source;
        source.source_path = file;
        source.original_source_uri = WideToUtf8(std::filesystem::absolute(file).wstring());
        source.display_name = WideToUtf8(file.filename().wstring());
        source.metadata_json = DirectFileMetadata(file);
        IngestOneSourceNoLock(db.get(), library, library_path, source, true, image_settings, embedding_provider.get(), result);
    }

    result.success = result.errors.empty();
    return result;
}

RagIngestionResult RagService::IngestFolder(const std::string& rag_id, const std::filesystem::path& folder, bool recursive) {
    RagIngestionResult result;
    if (!std::filesystem::exists(folder) || !std::filesystem::is_directory(folder)) {
        result.errors.push_back(folder.string() + ": folder does not exist.");
        return result;
    }

    std::vector<RagFileIngestionSource> sources;
    try {
        if (recursive) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
                if (entry.is_regular_file()) {
                    if (IsSupportedTextExtension(entry.path())) {
                        RagFileIngestionSource source;
                        source.source_path = entry.path();
                        source.original_source_uri = WideToUtf8(std::filesystem::absolute(entry.path()).wstring());
                        source.display_name = WideToUtf8(entry.path().filename().wstring());
                        source.metadata_json = FolderFileMetadata(folder, entry.path(), recursive);
                        sources.push_back(std::move(source));
                    } else {
                        ++result.files_skipped;
                    }
                }
            }
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(folder)) {
                if (entry.is_regular_file()) {
                    if (IsSupportedTextExtension(entry.path())) {
                        RagFileIngestionSource source;
                        source.source_path = entry.path();
                        source.original_source_uri = WideToUtf8(std::filesystem::absolute(entry.path()).wstring());
                        source.display_name = WideToUtf8(entry.path().filename().wstring());
                        source.metadata_json = FolderFileMetadata(folder, entry.path(), recursive);
                        sources.push_back(std::move(source));
                    } else {
                        ++result.files_skipped;
                    }
                }
            }
        }
    } catch (const std::exception& ex) {
        result.errors.push_back(folder.string() + ": " + ex.what());
        return result;
    }

    if (sources.empty()) {
        result.success = true;
        return result;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const json library_json = LoadJsonFile(LibraryConfigPath(rag_id), json::object());
    if (!library_json.is_object() || library_json.empty()) {
        result.errors.push_back("RAG library not found.");
        return result;
    }
    const RagLibraryConfig library = RagLibraryFromJson(library_json, rag_id);
    const std::filesystem::path library_path = LibraryPath(rag_id);
    auto db = OpenDatabase(library_path / "rag.sqlite");
    EnsureRagDatabase(db.get());
    std::unique_ptr<IRagEmbeddingProvider> embedding_provider;
    try {
        EnsureEmbeddingRuntimeNoLock(library);
        embedding_provider = CreateEmbeddingProvider(library);
    } catch (const std::exception& ex) {
        result.errors.push_back(ex.what());
        return result;
    }
    const RagImageIngestSettings image_settings = LoadImageIngestSettingsNoLock();

    for (const auto& source : sources) {
        IngestOneSourceNoLock(db.get(), library, library_path, source, true, image_settings, embedding_provider.get(), result);
    }
    result.success = result.errors.empty();
    return result;
}

RagIngestionResult RagService::IngestGeneratedDocument(const std::string& rag_id, const std::string& title, const std::string& content, const std::string& metadata_json, const std::string& source_uri) {
    std::lock_guard<std::mutex> lock(mutex_);
    RagIngestionResult result;
    const json library_json = LoadJsonFile(LibraryConfigPath(rag_id), json::object());
    if (!library_json.is_object() || library_json.empty()) {
        result.errors.push_back("RAG library not found.");
        return result;
    }
    if (Trim(content).empty()) {
        result.errors.push_back("Generated document content is empty.");
        return result;
    }

    const RagLibraryConfig library = RagLibraryFromJson(library_json, rag_id);
    const std::filesystem::path library_path = LibraryPath(rag_id);
    auto db = OpenDatabase(library_path / "rag.sqlite");
    EnsureRagDatabase(db.get());

    std::unique_ptr<IRagEmbeddingProvider> embedding_provider;
    try {
        EnsureEmbeddingRuntimeNoLock(library);
        embedding_provider = CreateEmbeddingProvider(library);
    } catch (const std::exception& ex) {
        result.errors.push_back(ex.what());
        return result;
    }

    const std::string generated_id = MakeId("generated");
    const std::string display_name = Trim(title).empty() ? "Generated document" : Trim(title);
    const std::filesystem::path generated_dir = library_path / "documents" / "generated_sources";
    const std::filesystem::path generated_path = generated_dir / (SafeFolderName(display_name) + L"_" + Utf8ToWide(generated_id) + L".md");
    try {
        std::filesystem::create_directories(generated_dir);
        std::ofstream output(generated_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            result.errors.push_back("Could not write generated document into the RAG store.");
            return result;
        }
        output << content;
    } catch (const std::exception& ex) {
        result.errors.push_back(std::string("Could not save generated document: ") + ex.what());
        return result;
    } catch (...) {
        result.errors.push_back("Could not save generated document: unexpected error.");
        return result;
    }

    json metadata = json::object();
    try {
        if (!Trim(metadata_json).empty()) {
            metadata = json::parse(metadata_json);
            if (!metadata.is_object()) {
                metadata = json::object();
            }
        }
    } catch (...) {
        metadata = json::object();
    }
    metadata["ingest_source"] = "rag_tool_generated_document";
    metadata["generated_at"] = CurrentTimestampUtc();
    metadata["source_absolute_path"] = WideToUtf8(std::filesystem::absolute(generated_path).wstring());
    metadata["generated_document_id"] = generated_id;

    RagFileIngestionSource source;
    source.source_path = generated_path;
    source.original_source_uri = Trim(source_uri).empty() ? ("generated://rag-tool/" + generated_id) : Trim(source_uri);
    source.display_name = display_name;
    source.metadata_json = metadata.dump();
    IngestOneSourceNoLock(db.get(), library, library_path, source, false, LoadImageIngestSettingsNoLock(), embedding_provider.get(), result);
    result.success = result.errors.empty();
    return result;
}

RagIngestionResult RagService::RebuildLibrary(const std::string& rag_id, std::function<void(const RagProgressUpdate&)> progress) {
    std::lock_guard<std::mutex> lock(mutex_);
    RagIngestionResult result;
    const json library_json = LoadJsonFile(LibraryConfigPath(rag_id), json::object());
    if (!library_json.is_object() || library_json.empty()) {
        result.errors.push_back("RAG library not found.");
        return result;
    }

    const RagLibraryConfig library = RagLibraryFromJson(library_json, rag_id);
    const std::filesystem::path library_path = LibraryPath(rag_id);
    auto db = OpenDatabase(library_path / "rag.sqlite");
    EnsureRagDatabase(db.get());
    std::unique_ptr<IRagEmbeddingProvider> embedding_provider;
    try {
        EnsureEmbeddingRuntimeNoLock(library);
        embedding_provider = CreateEmbeddingProvider(library);
    } catch (const std::exception& ex) {
        result.errors.push_back(ex.what());
        return result;
    }
    const RagImageIngestSettings image_settings = LoadImageIngestSettingsNoLock();

    const std::vector<RagDocumentRecord> documents = LoadActiveDocuments(db.get());
    std::vector<RagFileIngestionSource> sources;
    sources.reserve(documents.size());
    for (const auto& document : documents) {
        RagFileIngestionSource source;
        source.source_path = ResolveRebuildSourcePath(library_path, document);
        source.original_source_uri = document.original_source_uri;
        source.display_name = document.display_name;
        source.metadata_json = document.metadata_json;
        sources.push_back(std::move(source));
    }

    auto publish = [&](int processed, const std::string& stage, const std::string& current_item) {
        if (!progress) {
            return;
        }
        RagProgressUpdate update;
        update.total_items = static_cast<int>(sources.size());
        update.processed_items = processed;
        update.stage = stage;
        update.current_item = current_item;
        progress(update);
    };

    publish(0, "Clearing database", "");
    try {
        ClearRagDatabaseForRebuild(db.get());
    } catch (const std::exception& ex) {
        result.errors.push_back(std::string("Could not clear RAG database: ") + ex.what());
        return result;
    } catch (...) {
        result.errors.push_back("Could not clear RAG database: unexpected error.");
        return result;
    }

    int processed = 0;
    for (const auto& source : sources) {
        publish(processed, "Re-ingesting", source.display_name);
        IngestOneSourceNoLock(db.get(), library, library_path, source, false, image_settings, embedding_provider.get(), result);
        ++processed;
        publish(processed, "Re-ingested", source.display_name);
    }

    try {
        ExecSql(db.get(), "INSERT INTO chunks_fts(chunks_fts) VALUES('optimize');");
    } catch (...) {
    }

    if (result.errors.empty()) {
        RagLibraryConfig rebuilt = library;
        rebuilt.storage_path = WideToUtf8(std::filesystem::absolute(library_path).wstring());
        rebuilt.rebuild_required = false;
        rebuilt.rebuild_reason.clear();
        rebuilt.updated_at = CurrentTimestampUtc();
        SaveJsonFile(library_path / "rag.json", RagLibraryToJson(rebuilt));
    }

    publish(processed, "Rebuild complete", "");
    result.success = result.errors.empty();
    return result;
}

std::vector<RagQueryResult> RagService::QueryRag(const std::string& rag_id, const std::string& query, int max_results) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const json library_json = LoadJsonFile(LibraryConfigPath(rag_id), json::object());
    if (!library_json.is_object() || library_json.empty()) {
        return {};
    }
    const RagLibraryConfig library = RagLibraryFromJson(library_json, rag_id);
    if (!library.enabled) {
        return {};
    }

    const int limit = std::max(1, max_results);
    const std::string fts_query = BuildFtsQuery(query);
    std::map<std::string, RagQueryResult> merged;

    auto add_result = [&](RagQueryResult result) {
        const auto it = merged.find(result.chunk_id);
        if (it == merged.end()) {
            merged.emplace(result.chunk_id, std::move(result));
            return;
        }
        it->second.score += result.score;
        if (it->second.retrieval_method != result.retrieval_method) {
            it->second.retrieval_method = "hybrid_vector_fts";
        }
    };

    try {
        auto db = OpenDatabase(LibraryPath(rag_id) / "rag.sqlite");
        EnsureRagDatabase(db.get());

        bool vector_available = false;
        try {
            EnsureEmbeddingRuntimeNoLock(library);
            std::unique_ptr<IRagEmbeddingProvider> embedding_provider = CreateEmbeddingProvider(library);
            if (embedding_provider) {
                const auto query_embeddings = embedding_provider->Embed({query});
                if (!query_embeddings.empty() && !query_embeddings.front().empty()) {
                    const std::vector<float>& query_vector = query_embeddings.front();
                    const int dimensions = static_cast<int>(query_vector.size());
                    auto statement = PrepareSql(db.get(),
                        "SELECT c.id, c.document_id, c.text, "
                        "COALESCE(d.display_name, c.document_id), COALESCE(d.original_source_uri, ''), "
                        "COALESCE(d.metadata_json, ''), COALESCE(d.last_indexed_at, ''), "
                        "e.vector_blob "
                        "FROM embeddings e "
                        "JOIN chunks c ON c.id = e.chunk_id "
                        "LEFT JOIN documents d ON d.id = c.document_id AND d.deleted = 0 "
                        "WHERE e.provider = ? AND e.model = ? AND e.dimensions = ?;");
                    BindText(statement.get(), 1, embedding_provider->ProviderId());
                    BindText(statement.get(), 2, embedding_provider->ModelId());
                    sqlite3_bind_int(statement.get(), 3, dimensions);

                    std::vector<RagQueryResult> vector_results;
                    int rc = SQLITE_OK;
                    while ((rc = sqlite3_step(statement.get())) == SQLITE_ROW) {
                        const void* blob = sqlite3_column_blob(statement.get(), 7);
                        const int blob_bytes = sqlite3_column_bytes(statement.get(), 7);
                        const std::vector<float> chunk_vector = DeserializeVector(blob, blob_bytes);
                        const double similarity = CosineSimilarity(query_vector, chunk_vector);
                        if (similarity <= 0.0) {
                            continue;
                        }

                        RagQueryResult result;
                        result.rag_id = rag_id;
                        result.rag_name = library.name;
                        result.chunk_id = ColumnText(statement.get(), 0);
                        result.document_id = ColumnText(statement.get(), 1);
                        result.text = ColumnText(statement.get(), 2);
                        result.document_title = ColumnText(statement.get(), 3);
                        result.source_path = ColumnText(statement.get(), 4);
                        result.metadata_json = ColumnText(statement.get(), 5);
                        result.last_indexed_at = ColumnText(statement.get(), 6);
                        result.retrieval_method = "vector";
                        result.score = similarity * 0.70;
                        vector_results.push_back(std::move(result));
                    }
                    if (rc != SQLITE_DONE) {
                        ThrowSqlite(db.get(), "Could not query RAG vectors");
                    }
                    std::sort(vector_results.begin(), vector_results.end(), [](const RagQueryResult& left, const RagQueryResult& right) {
                        return left.score > right.score;
                    });
                    if (vector_results.size() > static_cast<size_t>(limit * 3)) {
                        vector_results.resize(static_cast<size_t>(limit * 3));
                    }
                    vector_available = !vector_results.empty();
                    for (auto& result : vector_results) {
                        add_result(std::move(result));
                    }
                }
            }
        } catch (...) {
            vector_available = false;
        }

        if (!fts_query.empty()) {
            auto statement = PrepareSql(db.get(),
                "SELECT chunks_fts.chunk_id, chunks_fts.document_id, c.text, "
                "COALESCE(d.display_name, c.document_id), COALESCE(d.original_source_uri, ''), "
                "COALESCE(d.metadata_json, ''), COALESCE(d.last_indexed_at, ''), bm25(chunks_fts) "
                "FROM chunks_fts "
                "JOIN chunks c ON c.id = chunks_fts.chunk_id "
                "LEFT JOIN documents d ON d.id = c.document_id AND d.deleted = 0 "
                "WHERE chunks_fts MATCH ? "
                "ORDER BY bm25(chunks_fts) "
                "LIMIT ?;");
            BindText(statement.get(), 1, fts_query);
            sqlite3_bind_int(statement.get(), 2, vector_available ? limit * 3 : limit);

            int row = 0;
            while (sqlite3_step(statement.get()) == SQLITE_ROW) {
                RagQueryResult result;
                result.rag_id = rag_id;
                result.rag_name = library.name;
                result.chunk_id = ColumnText(statement.get(), 0);
                result.document_id = ColumnText(statement.get(), 1);
                result.text = ColumnText(statement.get(), 2);
                result.document_title = ColumnText(statement.get(), 3);
                result.source_path = ColumnText(statement.get(), 4);
                result.metadata_json = ColumnText(statement.get(), 5);
                result.last_indexed_at = ColumnText(statement.get(), 6);
                result.retrieval_method = "sqlite_fts";
                const double rank_score = 1.0 / static_cast<double>(row + 1);
                result.score = rank_score * (vector_available ? 0.30 : 1.0);
                add_result(std::move(result));
                ++row;
            }
        }

        if (merged.empty()) {
            const auto query_terms = TokenizeForSearch(query);
            auto statement = PrepareSql(db.get(),
                "SELECT c.id, c.document_id, c.text, c.content_hash, c.chunk_index, c.token_estimate, c.metadata_json, "
                "COALESCE(d.display_name, c.document_id), COALESCE(d.original_source_uri, ''), "
                "COALESCE(d.metadata_json, ''), COALESCE(d.last_indexed_at, '') "
                "FROM chunks c "
                "LEFT JOIN documents d ON d.id = c.document_id AND d.deleted = 0;");
            while (sqlite3_step(statement.get()) == SQLITE_ROW) {
                RagChunkRecord chunk;
                chunk.id = ColumnText(statement.get(), 0);
                chunk.document_id = ColumnText(statement.get(), 1);
                chunk.rag_id = rag_id;
                chunk.text = ColumnText(statement.get(), 2);
                chunk.content_hash = ColumnText(statement.get(), 3);
                chunk.chunk_index = sqlite3_column_int(statement.get(), 4);
                chunk.token_estimate = sqlite3_column_int(statement.get(), 5);
                chunk.metadata_json = ColumnText(statement.get(), 6);

                RagDocumentRecord document;
                document.id = chunk.document_id;
                document.display_name = ColumnText(statement.get(), 7);
                document.original_source_uri = ColumnText(statement.get(), 8);
                document.metadata_json = ColumnText(statement.get(), 9);
                document.last_indexed_at = ColumnText(statement.get(), 10);
                const double score = ScoreChunk(query_terms, chunk, &document);
                if (score <= 0.0) {
                    continue;
                }

                RagQueryResult result;
                result.rag_id = rag_id;
                result.rag_name = library.name;
                result.document_id = chunk.document_id;
                result.document_title = document.display_name.empty() ? chunk.document_id : document.display_name;
                result.source_path = document.original_source_uri;
                result.chunk_id = chunk.id;
                result.text = chunk.text;
                result.metadata_json = document.metadata_json;
                result.last_indexed_at = document.last_indexed_at;
                result.retrieval_method = "keyword_scan";
                result.score = score;
                add_result(std::move(result));
            }
        }
    } catch (...) {
        return {};
    }

    std::vector<RagQueryResult> results;
    results.reserve(merged.size());
    for (auto& item : merged) {
        results.push_back(std::move(item.second));
    }
    std::sort(results.begin(), results.end(), [](const RagQueryResult& left, const RagQueryResult& right) {
        return left.score > right.score;
    });
    if (results.size() > static_cast<size_t>(limit)) {
        results.resize(static_cast<size_t>(limit));
    }
    return results;
}

std::vector<RagQueryResult> RagService::QueryProject(const std::string& project_id, const std::string& query, int global_max_results) const {
    std::vector<ProjectRagBinding> bindings = LoadProjectBindings(project_id);
    std::sort(bindings.begin(), bindings.end(), [](const ProjectRagBinding& left, const ProjectRagBinding& right) {
        return left.retrieval_priority > right.retrieval_priority;
    });

    std::vector<RagQueryResult> all_results;
    for (const auto& binding : bindings) {
        if (!binding.enabled || !binding.can_read) {
            continue;
        }
        double min_confidence = std::clamp(binding.default_min_confidence, 0.0, 1.0);
        double max_confidence = std::clamp(binding.default_max_confidence, 0.0, 1.0);
        if (min_confidence > max_confidence) {
            std::swap(min_confidence, max_confidence);
        }
        auto results = QueryRag(binding.rag_id, query, binding.max_chunks);
        for (auto& result : results) {
            const double confidence = std::clamp(result.score, 0.0, 1.0);
            if (confidence >= min_confidence && confidence <= max_confidence) {
                all_results.push_back(std::move(result));
            }
        }
    }

    std::sort(all_results.begin(), all_results.end(), [](const RagQueryResult& left, const RagQueryResult& right) {
        return left.score > right.score;
    });
    if (global_max_results > 0 && all_results.size() > static_cast<size_t>(global_max_results)) {
        all_results.resize(static_cast<size_t>(global_max_results));
    }
    return all_results;
}

std::string RagService::BuildContextBlock(const std::string& project_id, const std::string& query, int global_max_results) const {
    if (project_id.empty() || Trim(query).empty()) {
        return {};
    }

    const auto results = QueryProject(project_id, query, global_max_results);
    if (results.empty()) {
        return {};
    }

    std::ostringstream stream;
    stream << "Retrieved Project Knowledge:\n";
    stream << "The following excerpts were retrieved from RAG libraries attached to this project. Use them when relevant and preserve their source labels when helpful.\n";
    for (const auto& result : results) {
        stream << "\n[RAG: " << result.rag_name
               << " | Source: " << result.document_title
               << " | Chunk: " << result.chunk_id
               << " | Method: " << result.retrieval_method
               << " | Score: " << result.score << "]\n";
        if (!result.source_path.empty()) {
            stream << "Source path: " << result.source_path << "\n";
        }
        if (!result.metadata_json.empty()) {
            stream << "Source metadata: " << result.metadata_json << "\n";
        }
        stream << result.text << "\n";
    }
    return stream.str();
}
