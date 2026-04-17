#pragma once

#include "mcp_manager.h"
#include "openai_client.h"
#include "rag_service.h"
#include "util.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rag_tools {

constexpr char kListLibrariesToolName[] = "rag_list_libraries";
constexpr char kSearchToolName[] = "rag_search";
constexpr char kGetDocumentToolName[] = "rag_get_document";
constexpr char kIngestGeneratedDocumentToolName[] = "rag_ingest_generated_document";
constexpr char kWriteDocumentToDriveToolName[] = "rag_write_document_to_drive";

struct RagToolLibrary {
    RagLibraryConfig library;
    ProjectRagBinding binding;
};

struct RagToolRoute {
    std::string base_tool_name;
    std::string rag_id;
    std::string rag_name;
};

struct RagToolSet {
    std::vector<ChatToolDefinition> definitions;
    std::vector<McpExposedTool> exposed_tools;
    std::unordered_map<std::string, RagToolRoute> routes;
};

inline nlohmann::json JsonObjectSchema(
    std::initializer_list<std::pair<const char*, nlohmann::json>> properties,
    std::vector<std::string> required = {}) {
    nlohmann::json props = nlohmann::json::object();
    for (const auto& property : properties) {
        props[property.first] = property.second;
    }
    nlohmann::json schema = {
        {"type", "object"},
        {"properties", props},
        {"additionalProperties", false},
    };
    if (!required.empty()) {
        schema["required"] = required;
    }
    return schema;
}

inline nlohmann::json ParseJsonOrRaw(const std::string& value) {
    if (Trim(value).empty()) {
        return nullptr;
    }
    try {
        return nlohmann::json::parse(value);
    } catch (...) {
        return nlohmann::json{{"raw", value}};
    }
}

inline std::string JsonStringOr(const nlohmann::json& value,
                                const char* name,
                                const std::string& fallback = {}) {
    if (!value.is_object() || !value.contains(name) || !value[name].is_string()) {
        return fallback;
    }
    return value[name].get<std::string>();
}

inline bool JsonBoolOr(const nlohmann::json& value, const char* name, bool fallback) {
    if (!value.is_object() || !value.contains(name)) {
        return fallback;
    }
    if (value[name].is_boolean()) {
        return value[name].get<bool>();
    }
    return fallback;
}

inline int JsonIntOr(const nlohmann::json& value, const char* name, int fallback) {
    if (!value.is_object() || !value.contains(name)) {
        return fallback;
    }
    if (value[name].is_number_integer()) {
        return value[name].get<int>();
    }
    if (value[name].is_number()) {
        return static_cast<int>(value[name].get<double>());
    }
    return fallback;
}

inline double JsonDoubleOr(const nlohmann::json& value, const char* name, double fallback) {
    if (!value.is_object() || !value.contains(name) || !value[name].is_number()) {
        return fallback;
    }
    return value[name].get<double>();
}

inline std::vector<std::string> JsonStringArrayOrEmpty(const nlohmann::json& value,
                                                       const char* name) {
    std::vector<std::string> strings;
    if (!value.is_object() || !value.contains(name) || !value[name].is_array()) {
        return strings;
    }
    for (const auto& item : value[name]) {
        if (item.is_string()) {
            const std::string text = Trim(item.get<std::string>());
            if (!text.empty()) {
                strings.push_back(text);
            }
        }
    }
    return strings;
}

inline McpToolCallResult MakeJsonToolResult(nlohmann::json payload, bool success = true) {
    McpToolCallResult result;
    result.success = success;
    result.is_tool_error = !success;
    result.raw_result_json = payload.dump(2);
    result.content_text = result.raw_result_json;
    return result;
}

inline McpToolCallResult MakeRagToolError(const std::string& message,
                                          nlohmann::json details = nlohmann::json::object()) {
    nlohmann::json payload = {
        {"success", false},
        {"error", message},
    };
    if (!details.empty()) {
        payload["details"] = std::move(details);
    }
    return MakeJsonToolResult(std::move(payload), false);
}

inline std::string SanitizeIdentifier(const std::string& value) {
    std::string output;
    output.reserve(value.size());
    for (char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9')) {
            output.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        } else {
            output.push_back('_');
        }
    }
    while (!output.empty() && output.front() == '_') {
        output.erase(output.begin());
    }
    while (!output.empty() && output.back() == '_') {
        output.pop_back();
    }
    if (output.empty()) {
        output = "rag";
    }
    return output;
}

inline std::string RagLibraryDisplayName(const RagLibraryConfig& library) {
    std::string name = Trim(library.name);
    if (!name.empty()) {
        return name;
    }
    if (!Trim(library.storage_path).empty()) {
        name = Trim(WideToUtf8(std::filesystem::path(Utf8ToWide(library.storage_path)).filename().wstring()));
        if (!name.empty()) {
            return name;
        }
    }
    return library.id.empty() ? "Unnamed RAG Library" : library.id;
}

inline std::string RagLibraryDescription(const RagLibraryConfig& library) {
    const std::string description = Trim(library.description);
    return description.empty()
        ? "No RAG library description is configured."
        : description;
}

inline std::string RagToolAliasAction(const std::string& base_tool_name) {
    if (base_tool_name == kListLibrariesToolName) return "list_library";
    if (base_tool_name == kSearchToolName) return "search";
    if (base_tool_name == kGetDocumentToolName) return "get_document";
    if (base_tool_name == kWriteDocumentToDriveToolName) return "write_document";
    if (base_tool_name == kIngestGeneratedDocumentToolName) return "ingest_document";
    return SanitizeIdentifier(base_tool_name);
}

inline std::string HashHex(const std::string& value) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : value) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }

    std::ostringstream stream;
    stream << std::hex << std::setw(8) << std::setfill('0')
           << static_cast<uint32_t>(hash & 0xffffffffu);
    return stream.str();
}

inline std::string BuildRagToolAlias(const RagLibraryConfig& library,
                                     const std::string& base_tool_name) {
    std::string library_slug = SanitizeIdentifier(RagLibraryDisplayName(library));
    if (library_slug == "rag") {
        library_slug = "rag_library";
    }
    if (!library_slug.empty() &&
        library_slug.front() >= '0' && library_slug.front() <= '9') {
        library_slug = "library_" + library_slug;
    }
    const std::string action_slug = SanitizeIdentifier(RagToolAliasAction(base_tool_name));
    const std::string prefix =
        library_slug.substr(0, 28) + "_" + action_slug.substr(0, 22);
    return prefix + "_" + HashHex(library.id + "::" + base_tool_name);
}

inline std::string FriendlyRagActionTitle(const std::string& base_tool_name) {
    if (base_tool_name == kListLibrariesToolName) return "List Library";
    if (base_tool_name == kSearchToolName) return "Search";
    if (base_tool_name == kGetDocumentToolName) return "Get Document";
    if (base_tool_name == kWriteDocumentToDriveToolName) return "Write Document To Drive";
    if (base_tool_name == kIngestGeneratedDocumentToolName) return "Ingest Generated Document";
    return base_tool_name;
}

inline std::string RagStorageModeLabel(RagDocumentStorageMode mode) {
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

inline std::vector<RagToolLibrary> GetProjectRagToolLibraries(
    RagService* rag_service,
    const std::string& project_id,
    bool require_write = false,
    bool require_export = false) {
    std::vector<RagToolLibrary> libraries;
    if (!rag_service || project_id.empty()) {
        return libraries;
    }

    const auto bindings = rag_service->LoadProjectBindings(project_id);
    for (const auto& binding : bindings) {
        if (!binding.enabled || !binding.expose_as_tool || !binding.can_read) {
            continue;
        }
        if (binding.retrieval_mode == RagRetrievalMode::PassiveOnly ||
            binding.retrieval_mode == RagRetrievalMode::Disabled) {
            continue;
        }
        if (require_write && !binding.can_write) {
            continue;
        }
        if (require_export && (!binding.can_export || Trim(binding.export_path_template).empty())) {
            continue;
        }

        auto library = rag_service->GetLibrary(binding.rag_id);
        if (!library || !library->enabled) {
            continue;
        }

        libraries.push_back({*library, binding});
    }

    std::sort(libraries.begin(), libraries.end(),
        [](const RagToolLibrary& left, const RagToolLibrary& right) {
            if (left.binding.retrieval_priority != right.binding.retrieval_priority) {
                return left.binding.retrieval_priority > right.binding.retrieval_priority;
            }
            return left.library.name < right.library.name;
        });
    return libraries;
}

inline const RagToolLibrary* FindRagToolLibrary(const std::vector<RagToolLibrary>& libraries,
                                                const std::string& rag_id) {
    const auto it = std::find_if(libraries.begin(), libraries.end(),
        [&](const RagToolLibrary& item) {
            return item.library.id == rag_id;
        });
    return it == libraries.end() ? nullptr : &*it;
}

inline void ReplaceAll(std::string& text,
                       const std::string& needle,
                       const std::string& replacement) {
    if (needle.empty()) {
        return;
    }
    size_t position = 0;
    while ((position = text.find(needle, position)) != std::string::npos) {
        text.replace(position, needle.size(), replacement);
        position += replacement.size();
    }
}

inline bool IsVariableNameCandidate(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    for (const unsigned char ch : name) {
        if (!std::isalnum(ch) && ch != '_' && ch != '-' && ch != '.') {
            return false;
        }
    }
    return true;
}

inline std::vector<std::string> FindVariablePlaceholders(const std::string& text) {
    std::vector<std::string> names;
    auto add_name = [&](std::string name) {
        name = Trim(name);
        if (IsVariableNameCandidate(name) &&
            std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(std::move(name));
        }
    };

    for (size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '$') {
            continue;
        }
        if (index + 1 < text.size() && text[index + 1] == '<') {
            const size_t end = text.find(">$", index + 2);
            if (end != std::string::npos) {
                add_name(text.substr(index + 2, end - (index + 2)));
                index = end + 1;
            }
            continue;
        }

        const size_t end = text.find('$', index + 1);
        if (end != std::string::npos) {
            add_name(text.substr(index + 1, end - (index + 1)));
            index = end;
        }
    }
    return names;
}

inline std::unordered_map<std::string, std::string> CollectProjectVariableValues(
    const McpManager* mcp_manager,
    const std::string& project_id) {
    std::unordered_map<std::string, std::string> values;
    if (!mcp_manager || project_id.empty()) {
        return values;
    }
    for (const auto& binding : mcp_manager->GetProjectBindings(project_id)) {
        for (const auto& variable : binding.variables) {
            const std::string name = Trim(variable.name);
            if (!name.empty()) {
                values[name] = variable.value;
            }
        }
    }
    return values;
}

inline std::optional<std::string> ExpandProjectVariableTemplate(
    std::string text,
    const std::unordered_map<std::string, std::string>& values,
    std::string* error) {
    std::vector<std::string> missing;
    for (const auto& name : FindVariablePlaceholders(text)) {
        const auto value_it = values.find(name);
        if (value_it == values.end() || Trim(value_it->second).empty()) {
            missing.push_back("$<" + name + ">$");
            continue;
        }
        ReplaceAll(text, "$" + name + "$", value_it->second);
        ReplaceAll(text, "$<" + name + ">$", value_it->second);
    }

    if (!missing.empty()) {
        std::ostringstream stream;
        stream << "Missing project variable values for export path: ";
        for (size_t i = 0; i < missing.size(); ++i) {
            if (i > 0) {
                stream << ", ";
            }
            stream << missing[i];
        }
        if (error) {
            *error = stream.str();
        }
        return std::nullopt;
    }
    return text;
}

inline std::wstring LowercasePathString(std::filesystem::path path) {
    std::wstring text = path.lexically_normal().wstring();
    std::replace(text.begin(), text.end(), L'/', L'\\');
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return text;
}

inline bool PathIsAtOrInside(const std::filesystem::path& path,
                             const std::filesystem::path& root) {
    std::wstring root_text = LowercasePathString(root);
    std::wstring path_text = LowercasePathString(path);
    while (root_text.size() > 1 && (root_text.back() == L'\\' || root_text.back() == L'/')) {
        root_text.pop_back();
    }
    if (path_text == root_text) {
        return true;
    }
    root_text += L"\\";
    return path_text.rfind(root_text, 0) == 0;
}

inline std::wstring SanitizePathComponent(std::wstring component) {
    static constexpr wchar_t kInvalidChars[] = L"<>:\"/\\|?*";
    for (wchar_t& ch : component) {
        if (ch < 32 || std::wcschr(kInvalidChars, ch)) {
            ch = L'_';
        }
    }
    while (!component.empty() && (component.back() == L'.' || component.back() == L' ')) {
        component.pop_back();
    }
    while (!component.empty() && component.front() == L' ') {
        component.erase(component.begin());
    }
    return component.empty() ? L"document" : component;
}

inline std::filesystem::path SafeRelativeExportPath(
    const std::string& requested_relative_path,
    const std::wstring& default_file_name,
    std::string* error) {
    std::filesystem::path raw = Trim(requested_relative_path).empty()
        ? std::filesystem::path(default_file_name)
        : std::filesystem::path(Utf8ToWide(Trim(requested_relative_path)));

    if (raw.is_absolute() || raw.has_root_name() || raw.has_root_directory()) {
        if (error) {
            *error = "target_relative_path must be relative to the configured write-file folder.";
        }
        return {};
    }

    std::filesystem::path safe;
    for (const auto& part : raw) {
        const std::wstring text = part.wstring();
        if (text.empty() || text == L".") {
            continue;
        }
        if (text == L"..") {
            if (error) {
                *error = "target_relative_path cannot contain '..'.";
            }
            return {};
        }
        safe /= SanitizePathComponent(text);
    }
    if (safe.empty()) {
        safe = SanitizePathComponent(default_file_name);
    }
    return safe;
}

inline std::filesystem::path SafeRelativeFolderPath(const std::string& requested_folder_path,
                                                    std::string* error) {
    const std::string trimmed = Trim(requested_folder_path);
    if (trimmed.empty()) {
        return {};
    }

    const std::filesystem::path raw(Utf8ToWide(trimmed));
    if (raw.is_absolute() || raw.has_root_name() || raw.has_root_directory()) {
        if (error) {
            *error = "target_folder_relative_path must be relative to the configured write-file folder.";
        }
        return {};
    }

    std::filesystem::path safe;
    for (const auto& part : raw) {
        const std::wstring text = part.wstring();
        if (text.empty() || text == L".") {
            continue;
        }
        if (text == L"..") {
            if (error) {
                *error = "target_folder_relative_path cannot contain '..'.";
            }
            return {};
        }
        safe /= SanitizePathComponent(text);
    }
    return safe;
}

inline std::wstring DefaultExtractedExportFileName(const RagDocumentRecord& document) {
    std::filesystem::path name(Utf8ToWide(document.display_name.empty() ?
        document.id : document.display_name));
    std::wstring stem = name.stem().wstring();
    if (stem.empty()) {
        stem = name.filename().wstring();
    }
    if (stem.empty()) {
        stem = Utf8ToWide(document.id.empty() ? "document" : document.id);
    }
    return SanitizePathComponent(stem) + L".md";
}

inline std::wstring DefaultOriginalExportFileName(const RagDocumentRecord& document,
                                                  const std::filesystem::path& source_path) {
    std::wstring file_name = source_path.filename().wstring();
    if (file_name.empty()) {
        file_name = Utf8ToWide(document.display_name.empty() ? document.id : document.display_name);
    }
    return SanitizePathComponent(file_name);
}

inline nlohmann::json RagToolLibraryToJson(const RagToolLibrary& item) {
    nlohmann::json actions = nlohmann::json::array();
    if (item.binding.can_read) {
        actions.push_back({
            {"name", kSearchToolName},
            {"purpose", "Search this RAG for relevant chunks."},
            {"returns", "Ranked chunks with confidence, document_id, chunk_id, source path, metadata, and optional text."},
        });
        actions.push_back({
            {"name", kGetDocumentToolName},
            {"purpose", "Retrieve metadata and extracted document text after search identifies a useful document_id."},
            {"returns", "Document metadata, managed original path information, and optional extracted text."},
        });
    }
    if (item.binding.can_export && !Trim(item.binding.export_path_template).empty()) {
        actions.push_back({
            {"name", kWriteDocumentToDriveToolName},
            {"purpose", "Write a RAG document to the configured drive folder without using a filesystem MCP tool. The tool creates the configured folder and requested subfolders automatically when needed."},
            {"versions", {"original", "extracted_markdown"}},
            {"creates_missing_directories", true},
        });
    }
    if (item.binding.can_write) {
        actions.push_back({
            {"name", kIngestGeneratedDocumentToolName},
            {"purpose", "Persist generated Markdown/text content into this RAG for future retrieval."},
            {"returns", "Ingestion status, generated source URI, errors, and document id when available."},
        });
    }

    return nlohmann::json{
        {"id", item.library.id},
        {"name", RagLibraryDisplayName(item.library)},
        {"description", RagLibraryDescription(item.library)},
        {"enabled", item.library.enabled},
        {"storage_path", item.library.storage_path},
        {"storage_mode", RagStorageModeLabel(item.library.storage_mode)},
        {"embedding_provider", item.library.embedding_provider},
        {"embedding_base_url", item.library.embedding_base_url},
        {"embedding_model", item.library.embedding_model},
        {"vector_backend", item.library.vector_backend},
        {"permissions", {
            {"can_read", item.binding.can_read},
            {"can_write", item.binding.can_write},
            {"can_delete", item.binding.can_delete},
            {"can_export", item.binding.can_export},
            {"can_write_to_drive", item.binding.can_export},
            {"default_ingest_target", item.binding.default_ingest_target},
            {"exposed_as_tool", item.binding.expose_as_tool},
        }},
        {"write_to_drive_path_template", item.binding.export_path_template},
        {"retrieval_priority", item.binding.retrieval_priority},
        {"default_max_chunks_for_project", item.binding.max_chunks},
        {"default_confidence_window", {
            {"min_confidence", item.binding.default_min_confidence},
            {"max_confidence", item.binding.default_max_confidence},
        }},
        {"available_tool_actions", actions},
        {"usage_guidance", {
            {"search_first", "Use this RAG server's search tool before reading or exporting unless you already have a document_id."},
            {"read_more", "Use get_document when a search result is relevant but the chunk text is not enough."},
            {"write_to_drive", item.binding.can_export ? "Use write_document_to_drive to write original or extracted_markdown output to the configured folder. The configured folder and any requested subfolders are created automatically." : "Not available for this RAG binding."},
            {"write_to_rag", item.binding.can_write ? "Use ingest_generated_document only for generated Markdown/text that should become persistent RAG content." : "Not available for this RAG binding."},
            {"passive_retrieval", "Passive retrieval is currently inactive; use active RAG tools explicitly."},
        }},
    };
}

inline std::string RagServerDescriptionPrefix(const RagToolLibrary& item) {
    const std::string library_name = RagLibraryDisplayName(item.library);
    const std::string library_description = RagLibraryDescription(item.library);
    std::string description = "MCP server name: \"" + library_name + "\". ";
    description += "This is a RAG library exposed as its own MCP-style server for this project. ";
    description += "RAG library name: \"" + library_name + "\". ";
    description += "RAG library description: " + library_description + " ";
    description += "When listing available MCP servers, identify this server as \"" + library_name +
                   "\", not as RAG (Anonymous) or generic RAG Tools. ";
    description += "Use this server only when the request matches that RAG library description. "
                   "Passive RAG retrieval is inactive for now; call these active RAG tools explicitly.";
    return description;
}

inline nlohmann::json SearchParametersSchema(bool scoped_to_one_rag) {
    nlohmann::json schema = JsonObjectSchema({
        {"query", {{"type", "string"}, {"description", "Natural-language information need, keywords, file title, concept, or question to search for."}}},
        {"max_results", {{"type", "integer"}, {"minimum", 1}, {"maximum", 50}, {"description", "Maximum final results after threshold filtering. Default: 8."}}},
        {"candidate_limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}, {"description", "Candidate pool before confidence filtering. Default: max(50, max_results * 5)."}}},
        {"min_confidence", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}, {"description", "Minimum normalized confidence to return. If omitted, the project binding default is used."}}},
        {"max_confidence", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}, {"description", "Maximum normalized confidence to return. Use with min_confidence to search a confidence band."}}},
        {"include_text", {{"type", "boolean"}, {"description", "Whether to include chunk excerpt text. Default: true."}}},
        {"retrieval_mode", {{"type", "string"}, {"enum", {"hybrid", "vector", "keyword", "reranked"}}, {"description", "Requested retrieval intent. The current backend executes hybrid/fallback retrieval and reports actual retrieval_method per result."}}},
    }, {"query"});
    if (!scoped_to_one_rag) {
        schema["properties"]["rag_ids"] = {
            {"type", "array"},
            {"items", {{"type", "string"}}},
            {"description", "Optional exposed RAG library ids. Omit to search all exposed readable RAGs."},
        };
    }
    return schema;
}

inline nlohmann::json GetDocumentParametersSchema(bool scoped_to_one_rag) {
    nlohmann::json schema = JsonObjectSchema({
        {"document_id", {{"type", "string"}, {"description", "Document id returned by RAG search."}}},
        {"include_text", {{"type", "boolean"}, {"description", "Whether to include extracted Markdown/text. Default: true."}}},
        {"max_chars", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 200000}, {"description", "Maximum extracted text characters to return. Default: 20000."}}},
    }, {"document_id"});
    if (!scoped_to_one_rag) {
        schema["properties"]["rag_id"] = {
            {"type", "string"},
            {"description", "Exposed readable RAG library id."},
        };
        schema["required"].push_back("rag_id");
    }
    return schema;
}

inline nlohmann::json WriteDocumentParametersSchema(bool scoped_to_one_rag) {
    nlohmann::json schema = JsonObjectSchema({
        {"document_id", {{"type", "string"}, {"description", "Document id returned by RAG search or get_document."}}},
        {"version", {{"type", "string"}, {"enum", {"original", "extracted_markdown"}}, {"description", "Output version to write."}}},
        {"target_relative_path", {{"type", "string"}, {"description", "Optional full file path relative to the configured write-file folder. Subfolders are allowed and created automatically; absolute paths and '..' are rejected."}}},
        {"target_folder_relative_path", {{"type", "string"}, {"description", "Optional nested folder path relative to the configured write-file folder. Missing folders are created automatically."}}},
        {"target_file_name", {{"type", "string"}, {"description", "Optional file name to use with target_folder_relative_path."}}},
        {"overwrite", {{"type", "boolean"}, {"description", "Whether to overwrite an existing target file. Default: false."}}},
    }, {"document_id", "version"});
    if (!scoped_to_one_rag) {
        schema["properties"]["rag_id"] = {
            {"type", "string"},
            {"description", "Readable exposed RAG id with Write file enabled."},
        };
        schema["required"].push_back("rag_id");
    }
    return schema;
}

inline nlohmann::json IngestGeneratedDocumentParametersSchema(bool scoped_to_one_rag) {
    nlohmann::json schema = JsonObjectSchema({
        {"title", {{"type", "string"}, {"description", "Human-readable document title for future search results."}}},
        {"content", {{"type", "string"}, {"description", "Markdown or plain text content to ingest. Must be non-empty."}}},
        {"source_uri", {{"type", "string"}, {"description", "Optional stable source URI for idempotent updates."}}},
        {"metadata", {{"type", "object"}, {"description", "Optional provenance metadata."}}},
    }, {"title", "content"});
    if (!scoped_to_one_rag) {
        schema["properties"]["rag_id"] = {
            {"type", "string"},
            {"description", "Write-enabled exposed RAG id."},
        };
    }
    return schema;
}

inline void AddVirtualRagTool(RagToolSet& set,
                              const RagToolLibrary& item,
                              const std::string& base_tool_name,
                              const std::string& purpose,
                              const nlohmann::json& parameters) {
    McpExposedTool exposed;
    exposed.alias = BuildRagToolAlias(item.library, base_tool_name);
    exposed.server_id = "rag:" + item.library.id;
    exposed.server_name = RagLibraryDisplayName(item.library);
    exposed.tool_name = base_tool_name;
    exposed.title = FriendlyRagActionTitle(base_tool_name);
    exposed.description = RagServerDescriptionPrefix(item) + " Action: " + purpose;
    exposed.input_schema_json = parameters.dump();

    ChatToolDefinition definition;
    definition.name = exposed.alias;
    definition.description = exposed.description + " (MCP server: " +
        exposed.server_name + ", tool: " + exposed.tool_name + ")";
    definition.parameters_json = exposed.input_schema_json;

    set.routes[exposed.alias] = {base_tool_name, item.library.id, exposed.server_name};
    set.definitions.push_back(std::move(definition));
    set.exposed_tools.push_back(std::move(exposed));
}

inline RagToolSet BuildRagToolSet(RagService* rag_service, const std::string& project_id) {
    RagToolSet set;
    const auto read_libraries = GetProjectRagToolLibraries(rag_service, project_id);
    if (read_libraries.empty()) {
        return set;
    }

    for (const auto& item : read_libraries) {
        AddVirtualRagTool(
            set,
            item,
            kListLibrariesToolName,
            "List this RAG library's description, permissions, retrieval defaults, write-file folder, and allowed RAG actions.",
            JsonObjectSchema({}));

        AddVirtualRagTool(
            set,
            item,
            kSearchToolName,
            "Search this RAG library for relevant chunks and document_id values.",
            SearchParametersSchema(true));

        AddVirtualRagTool(
            set,
            item,
            kGetDocumentToolName,
            "Load metadata and extracted text for a document from this RAG library.",
            GetDocumentParametersSchema(true));

        if (item.binding.can_export && !Trim(item.binding.export_path_template).empty()) {
            AddVirtualRagTool(
                set,
                item,
                kWriteDocumentToDriveToolName,
                "Write a document from this RAG library to the project-configured write-file folder. The tool creates the configured folder and requested subfolders automatically when needed.",
                WriteDocumentParametersSchema(true));
        }

        if (item.binding.can_write) {
            AddVirtualRagTool(
                set,
                item,
                kIngestGeneratedDocumentToolName,
                "Persist generated Markdown/text content into this RAG library.",
                IngestGeneratedDocumentParametersSchema(true));
        }
    }

    return set;
}

inline std::string BuildRagProjectContext(RagService* rag_service, const std::string& project_id) {
    const auto libraries = GetProjectRagToolLibraries(rag_service, project_id);
    if (libraries.empty()) {
        return {};
    }

    std::ostringstream stream;
    stream << "Project RAG MCP Servers:\n";
    stream << "Each RAG library below is exposed as its own MCP-style server. "
              "Use the server name and description to choose the correct RAG. "
              "When listing MCP servers, use these library names as the server names; "
              "do not call them RAG (Anonymous) or generic RAG Tools. "
              "Passive RAG retrieval is inactive in this phase; use the active RAG tools explicitly.\n";
    for (const auto& item : libraries) {
        const std::string library_name = RagLibraryDisplayName(item.library);
        stream << "- MCP server: " << library_name << "\n";
        stream << "  RAG library id: " << item.library.id << "\n";
        stream << "  RAG library name: " << library_name << "\n";
        stream << "  RAG library description: " << RagLibraryDescription(item.library) << "\n";
        stream << "  Available tools:";
        stream << " " << kSearchToolName << " (" << BuildRagToolAlias(item.library, kSearchToolName) << ")";
        stream << ", " << kGetDocumentToolName << " (" << BuildRagToolAlias(item.library, kGetDocumentToolName) << ")";
        stream << ", " << kListLibrariesToolName << " (" << BuildRagToolAlias(item.library, kListLibrariesToolName) << ")";
        if (item.binding.can_export && !Trim(item.binding.export_path_template).empty()) {
            stream << ", " << kWriteDocumentToDriveToolName << " ("
                   << BuildRagToolAlias(item.library, kWriteDocumentToDriveToolName) << ")";
        }
        if (item.binding.can_write) {
            stream << ", " << kIngestGeneratedDocumentToolName << " ("
                   << BuildRagToolAlias(item.library, kIngestGeneratedDocumentToolName) << ")";
        }
        stream << "\n";
    }
    return stream.str();
}

inline bool IsBaseRagToolName(const std::string& name) {
    return name == kListLibrariesToolName ||
        name == kSearchToolName ||
        name == kGetDocumentToolName ||
        name == kIngestGeneratedDocumentToolName ||
        name == kWriteDocumentToDriveToolName;
}

inline bool IsRagToolName(
    const std::string& name,
    const std::unordered_map<std::string, RagToolRoute>* routes = nullptr) {
    if (routes && routes->find(name) != routes->end()) {
        return true;
    }
    return IsBaseRagToolName(name);
}

inline std::string BuildToolCallUtcTimestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32]{};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

inline McpToolCallResult CallRagTool(
    RagService* rag_service,
    const McpManager* mcp_manager,
    const std::string& project_id,
    const std::string& exposed_tool_name,
    const std::string& arguments_json,
    const std::unordered_map<std::string, RagToolRoute>* routes = nullptr,
    std::vector<RagWorkingSetEntry>* working_set_out = nullptr) {
    if (!rag_service) {
        return MakeRagToolError("RAG service is not available.");
    }

    std::string tool_name = exposed_tool_name;
    std::string forced_rag_id;
    std::string forced_rag_name;
    if (routes) {
        const auto route_it = routes->find(exposed_tool_name);
        if (route_it != routes->end()) {
            tool_name = route_it->second.base_tool_name;
            forced_rag_id = route_it->second.rag_id;
            forced_rag_name = route_it->second.rag_name;
        }
    }

    nlohmann::json arguments = nlohmann::json::object();
    try {
        if (!Trim(arguments_json).empty()) {
            arguments = nlohmann::json::parse(arguments_json);
        }
        if (!arguments.is_object()) {
            return MakeRagToolError("RAG tool arguments must be a JSON object.");
        }
    } catch (const std::exception& ex) {
        return MakeRagToolError(std::string("RAG tool arguments were not valid JSON: ") + ex.what());
    } catch (...) {
        return MakeRagToolError("RAG tool arguments were not valid JSON.");
    }

    if (!forced_rag_id.empty()) {
        if (tool_name == kSearchToolName) {
            arguments["rag_ids"] = nlohmann::json::array({forced_rag_id});
        } else {
            arguments["rag_id"] = forced_rag_id;
        }
    }

    const auto read_libraries = GetProjectRagToolLibraries(rag_service, project_id);
    if (read_libraries.empty()) {
        return MakeRagToolError("No readable RAG libraries are exposed as tools for this project.");
    }

    const auto add_route_metadata = [&](nlohmann::json& payload) {
        if (!forced_rag_id.empty()) {
            payload["exposed_tool"] = exposed_tool_name;
            payload["virtual_mcp_server"] = {
                {"server_name", forced_rag_name},
                {"rag_id", forced_rag_id},
                {"base_tool", tool_name},
            };
        }
    };

    if (tool_name == kListLibrariesToolName) {
        const std::string requested_rag_id = Trim(JsonStringOr(arguments, "rag_id"));
        nlohmann::json libraries = nlohmann::json::array();
        for (const auto& item : read_libraries) {
            if (!requested_rag_id.empty() && item.library.id != requested_rag_id) {
                continue;
            }
            libraries.push_back(RagToolLibraryToJson(item));
        }
        nlohmann::json payload = {
            {"success", true},
            {"tool", tool_name},
            {"project_id", project_id},
            {"count", libraries.size()},
            {"libraries", libraries},
            {"available_functions", {
                {kListLibrariesToolName, "Discover this RAG library's description, permissions, retrieval defaults, and allowed actions."},
                {kSearchToolName, "Search for relevant chunks and document_id values."},
                {kGetDocumentToolName, "Read document metadata and extracted Markdown/text."},
                {kWriteDocumentToDriveToolName, "Write original or extracted_markdown document versions to the configured folder when Write file is enabled. The configured folder and requested subfolders are created automatically."},
                {kIngestGeneratedDocumentToolName, "Persist generated Markdown/text into a write-enabled RAG."},
            }},
            {"notes", "Only libraries selected for this project with Enable, Read, and Tool checked are exposed. Passive RAG retrieval is inactive; use these active tools explicitly."},
        };
        add_route_metadata(payload);
        return MakeJsonToolResult(std::move(payload));
    }

    if (tool_name == kSearchToolName) {
        const std::string query = Trim(JsonStringOr(arguments, "query"));
        if (query.empty()) {
            return MakeRagToolError("rag_search requires a non-empty query.");
        }

        const int max_results = std::clamp(JsonIntOr(arguments, "max_results", 8), 1, 50);
        const int default_candidate_limit = std::max(50, max_results * 5);
        const int candidate_limit =
            std::clamp(JsonIntOr(arguments, "candidate_limit", default_candidate_limit), 1, 200);
        const bool has_min_confidence =
            arguments.contains("min_confidence") && arguments["min_confidence"].is_number();
        const bool has_max_confidence =
            arguments.contains("max_confidence") && arguments["max_confidence"].is_number();
        const double requested_min_confidence =
            std::clamp(JsonDoubleOr(arguments, "min_confidence", 0.0), 0.0, 1.0);
        const double requested_max_confidence =
            std::clamp(JsonDoubleOr(arguments, "max_confidence", 1.0), 0.0, 1.0);
        if (has_min_confidence && has_max_confidence &&
            requested_min_confidence > requested_max_confidence) {
            return MakeRagToolError("min_confidence cannot be greater than max_confidence.");
        }
        const bool include_text = JsonBoolOr(arguments, "include_text", true);
        const std::string retrieval_mode = Trim(JsonStringOr(arguments, "retrieval_mode", "hybrid"));
        const auto requested_rag_ids = JsonStringArrayOrEmpty(arguments, "rag_ids");

        std::vector<RagToolLibrary> selected_libraries;
        nlohmann::json skipped = nlohmann::json::array();
        if (requested_rag_ids.empty()) {
            selected_libraries = read_libraries;
        } else {
            for (const auto& rag_id : requested_rag_ids) {
                if (const RagToolLibrary* library = FindRagToolLibrary(read_libraries, rag_id)) {
                    selected_libraries.push_back(*library);
                } else {
                    skipped.push_back({
                        {"rag_id", rag_id},
                        {"reason", "RAG is not exposed as a readable tool for this project."},
                    });
                }
            }
        }
        if (selected_libraries.empty()) {
            return MakeRagToolError(
                "No requested RAG libraries are available to search.",
                {{"skipped_rag_ids", skipped}});
        }

        struct SearchHit {
            RagQueryResult result;
            double confidence = 0.0;
        };
        std::vector<SearchHit> hits;
        nlohmann::json searched = nlohmann::json::array();
        nlohmann::json thresholds_by_rag = nlohmann::json::array();
        for (const auto& item : selected_libraries) {
            searched.push_back(item.library.id);
            double min_confidence =
                has_min_confidence ? requested_min_confidence : item.binding.default_min_confidence;
            double max_confidence =
                has_max_confidence ? requested_max_confidence : item.binding.default_max_confidence;
            min_confidence = std::clamp(min_confidence, 0.0, 1.0);
            max_confidence = std::clamp(max_confidence, 0.0, 1.0);
            if (min_confidence > max_confidence) {
                std::swap(min_confidence, max_confidence);
            }
            thresholds_by_rag.push_back({
                {"rag_id", item.library.id},
                {"rag_name", RagLibraryDisplayName(item.library)},
                {"min_confidence", min_confidence},
                {"max_confidence", max_confidence},
                {"min_source", has_min_confidence ? "request" : "project_binding_default"},
                {"max_source", has_max_confidence ? "request" : "project_binding_default"},
            });
            const int per_library_limit = std::max(candidate_limit, item.binding.max_chunks);
            for (auto& result : rag_service->QueryRag(item.library.id, query, per_library_limit)) {
                const double confidence = std::clamp(result.score, 0.0, 1.0);
                if (confidence < min_confidence || confidence > max_confidence) {
                    continue;
                }
                hits.push_back({std::move(result), confidence});
            }
        }

        std::sort(hits.begin(), hits.end(), [](const SearchHit& left, const SearchHit& right) {
            return left.result.score > right.result.score;
        });
        if (hits.size() > static_cast<size_t>(max_results)) {
            hits.resize(static_cast<size_t>(max_results));
        }

        if (working_set_out) {
            const std::string ts = BuildToolCallUtcTimestamp();
            for (const auto& hit : hits) {
                if (hit.result.text.empty()) {
                    continue;
                }
                const bool already = std::any_of(
                    working_set_out->begin(),
                    working_set_out->end(),
                    [&](const RagWorkingSetEntry& entry) {
                        return entry.chunk_id == hit.result.chunk_id;
                    });
                if (already) {
                    continue;
                }
                RagWorkingSetEntry ws;
                ws.chunk_id = hit.result.chunk_id;
                ws.rag_id = hit.result.rag_id;
                ws.rag_name = hit.result.rag_name;
                ws.document_id = hit.result.document_id;
                ws.document_title = hit.result.document_title;
                ws.text = hit.result.text;
                ws.score = hit.result.score;
                ws.query = query;
                ws.retrieved_at = ts;
                working_set_out->push_back(std::move(ws));
            }
        }

        nlohmann::json results = nlohmann::json::array();
        for (const auto& hit : hits) {
            nlohmann::json item = {
                {"result_id", hit.result.rag_id + ":" + hit.result.document_id + ":" + hit.result.chunk_id},
                {"confidence", hit.confidence},
                {"raw_score", hit.result.score},
                {"retrieval_method", hit.result.retrieval_method},
                {"rag_id", hit.result.rag_id},
                {"rag_name", hit.result.rag_name},
                {"document_id", hit.result.document_id},
                {"document_title", hit.result.document_title},
                {"source_path", hit.result.source_path},
                {"chunk_id", hit.result.chunk_id},
                {"last_indexed_at", hit.result.last_indexed_at},
                {"metadata", ParseJsonOrRaw(hit.result.metadata_json)},
            };
            if (include_text) {
                item["text"] = hit.result.text;
            }
            results.push_back(std::move(item));
        }

        nlohmann::json notes = nlohmann::json::array();
        if (!retrieval_mode.empty() && retrieval_mode != "hybrid") {
            notes.push_back("Requested retrieval_mode was accepted for intent, but the current local backend runs hybrid/fallback retrieval and reports actual retrieval_method per result.");
        }
        if (results.empty()) {
            notes.push_back("No results matched the requested confidence window. Try lowering min_confidence, increasing candidate_limit, or searching a different RAG.");
        }

        nlohmann::json payload = {
            {"success", true},
            {"tool", tool_name},
            {"query", query},
            {"requested", {
                {"max_results", max_results},
                {"candidate_limit", candidate_limit},
                {"min_confidence", has_min_confidence ? nlohmann::json(requested_min_confidence) : nlohmann::json(nullptr)},
                {"max_confidence", has_max_confidence ? nlohmann::json(requested_max_confidence) : nlohmann::json(nullptr)},
                {"confidence_defaults", "Omitted thresholds use each RAG project's binding defaults."},
                {"include_text", include_text},
                {"retrieval_mode", retrieval_mode.empty() ? "hybrid" : retrieval_mode},
            }},
            {"searched_rag_ids", searched},
            {"skipped_rag_ids", skipped},
            {"confidence_windows_by_rag", thresholds_by_rag},
            {"count", results.size()},
            {"results", results},
            {"notes", notes},
        };
        add_route_metadata(payload);
        return MakeJsonToolResult(std::move(payload));
    }

    if (tool_name == kGetDocumentToolName) {
        const std::string rag_id = Trim(JsonStringOr(arguments, "rag_id"));
        const std::string document_id = Trim(JsonStringOr(arguments, "document_id"));
        if (rag_id.empty() || document_id.empty()) {
            return MakeRagToolError("rag_get_document requires rag_id and document_id.");
        }
        const RagToolLibrary* library = FindRagToolLibrary(read_libraries, rag_id);
        if (!library) {
            return MakeRagToolError("The requested RAG library is not exposed as a readable tool for this project.");
        }

        auto document = rag_service->GetDocument(rag_id, document_id);
        if (!document) {
            return MakeRagToolError("Document not found in the requested RAG library.");
        }

        const bool include_text = JsonBoolOr(arguments, "include_text", true);
        const int max_chars = std::clamp(JsonIntOr(arguments, "max_chars", 20000), 1000, 200000);
        bool truncated = false;
        std::string load_error;
        std::string extracted_text;
        if (include_text) {
            extracted_text = rag_service->LoadDocumentText(
                rag_id, document_id, static_cast<size_t>(max_chars), &truncated, &load_error);
            if (!load_error.empty()) {
                return MakeRagToolError(load_error);
            }
        }

        std::string managed_original_path;
        if (!document->stored_relative_path.empty() && !library->library.storage_path.empty()) {
            const std::filesystem::path path =
                std::filesystem::path(Utf8ToWide(library->library.storage_path)) /
                std::filesystem::path(Utf8ToWide(document->stored_relative_path));
            managed_original_path = WideToUtf8(std::filesystem::absolute(path).wstring());
        }

        nlohmann::json payload = {
            {"success", true},
            {"tool", tool_name},
            {"rag_id", rag_id},
            {"rag_name", RagLibraryDisplayName(library->library)},
            {"document", {
                {"id", document->id},
                {"display_name", document->display_name},
                {"original_source_uri", document->original_source_uri},
                {"original_source_type", document->original_source_type},
                {"stored_relative_path", document->stored_relative_path},
                {"managed_original_path", managed_original_path},
                {"extracted_relative_path", document->extracted_relative_path},
                {"mime_type", document->mime_type},
                {"file_size", document->file_size},
                {"imported_at", document->imported_at},
                {"last_indexed_at", document->last_indexed_at},
                {"metadata", ParseJsonOrRaw(document->metadata_json)},
            }},
            {"include_text", include_text},
            {"max_chars", max_chars},
            {"truncated", truncated},
        };
        if (include_text) {
            payload["extracted_text"] = extracted_text;
        }
        add_route_metadata(payload);
        return MakeJsonToolResult(std::move(payload));
    }

    if (tool_name == kWriteDocumentToDriveToolName) {
        const auto export_libraries = GetProjectRagToolLibraries(rag_service, project_id, false, true);
        if (export_libraries.empty()) {
            return MakeRagToolError("No RAG libraries are exposed with Write file enabled and a configured write-file folder.");
        }

        const std::string rag_id = Trim(JsonStringOr(arguments, "rag_id"));
        const std::string document_id = Trim(JsonStringOr(arguments, "document_id"));
        std::string version = Trim(JsonStringOr(arguments, "version", "extracted_markdown"));
        if (version == "markdown" || version == "extracted") {
            version = "extracted_markdown";
        }
        if (rag_id.empty() || document_id.empty()) {
            return MakeRagToolError("rag_write_document_to_drive requires rag_id and document_id.");
        }
        if (version != "original" && version != "extracted_markdown") {
            return MakeRagToolError("version must be either 'original' or 'extracted_markdown'.");
        }

        const RagToolLibrary* library = FindRagToolLibrary(export_libraries, rag_id);
        if (!library) {
            nlohmann::json writable = nlohmann::json::array();
            for (const auto& item : export_libraries) {
                writable.push_back(RagToolLibraryToJson(item));
            }
            return MakeRagToolError(
                "The requested RAG library is not exposed with Write file enabled for this project.",
                {{"write_file_enabled_libraries", writable}});
        }

        std::string expand_error;
        const auto expanded_folder = ExpandProjectVariableTemplate(
            library->binding.export_path_template,
            CollectProjectVariableValues(mcp_manager, project_id),
            &expand_error);
        if (!expanded_folder) {
            return MakeRagToolError(
                expand_error.empty() ? "Could not expand the configured write-file folder." : expand_error);
        }

        std::filesystem::path base_folder(Utf8ToWide(Trim(*expanded_folder)));
        if (base_folder.empty()) {
            return MakeRagToolError("The configured write-file folder is empty.");
        }
        if (!base_folder.is_absolute()) {
            return MakeRagToolError("The configured write-file folder must expand to an absolute path.");
        }

        std::error_code ec;
        const bool configured_folder_existed = std::filesystem::exists(base_folder, ec);
        ec.clear();
        std::filesystem::create_directories(base_folder, ec);
        if (ec) {
            return MakeRagToolError("Could not create the configured write-file folder: " + ec.message());
        }
        if (!std::filesystem::is_directory(base_folder, ec)) {
            return MakeRagToolError("The configured write-file folder is not a directory.");
        }

        std::filesystem::path base_root = std::filesystem::weakly_canonical(base_folder, ec);
        if (ec) {
            ec.clear();
            base_root = std::filesystem::absolute(base_folder, ec).lexically_normal();
            if (ec) {
                return MakeRagToolError("Could not resolve the configured write-file folder: " + ec.message());
            }
        }

        auto document = rag_service->GetDocument(rag_id, document_id);
        if (!document) {
            return MakeRagToolError("Document not found in the requested RAG library.");
        }

        const bool overwrite = JsonBoolOr(arguments, "overwrite", false);
        const std::string requested_relative_path = Trim(JsonStringOr(arguments, "target_relative_path"));
        const std::string requested_folder_path = Trim(JsonStringOr(arguments, "target_folder_relative_path"));
        const std::string requested_file_name = Trim(JsonStringOr(arguments, "target_file_name"));
        std::string path_error;
        std::filesystem::path default_name;
        std::filesystem::path original_source_path;
        std::string extracted_text;
        const bool write_extracted_text = version == "extracted_markdown";

        if (write_extracted_text) {
            bool truncated = false;
            std::string load_error;
            extracted_text = rag_service->LoadDocumentText(rag_id, document_id, 0, &truncated, &load_error);
            if (!load_error.empty()) {
                return MakeRagToolError(load_error);
            }
            default_name = DefaultExtractedExportFileName(*document);
        } else {
            if (!document->stored_relative_path.empty()) {
                if (library->library.storage_path.empty()) {
                    return MakeRagToolError("The RAG library storage path is not configured, so the managed original cannot be located.");
                }
                original_source_path =
                    std::filesystem::path(Utf8ToWide(library->library.storage_path)) /
                    std::filesystem::path(Utf8ToWide(document->stored_relative_path));
            } else if (document->original_source_type == "file" &&
                       !document->original_source_uri.empty()) {
                original_source_path = std::filesystem::path(Utf8ToWide(document->original_source_uri));
            }

            if (original_source_path.empty()) {
                return MakeRagToolError("Document does not have a managed or file-backed original to write.");
            }
            if (!std::filesystem::exists(original_source_path, ec) ||
                !std::filesystem::is_regular_file(original_source_path, ec)) {
                return MakeRagToolError("Original file was not found: " + WideToUtf8(original_source_path.wstring()));
            }
            default_name = DefaultOriginalExportFileName(*document, original_source_path);
        }

        std::filesystem::path relative_target;
        if (!requested_relative_path.empty()) {
            relative_target = SafeRelativeExportPath(
                requested_relative_path, default_name.wstring(), &path_error);
        } else {
            const std::filesystem::path relative_folder =
                SafeRelativeFolderPath(requested_folder_path, &path_error);
            const std::wstring file_name = requested_file_name.empty()
                ? default_name.wstring()
                : SanitizePathComponent(Utf8ToWide(requested_file_name));
            relative_target = relative_folder / file_name;
        }
        if (!path_error.empty()) {
            return MakeRagToolError(path_error);
        }

        const std::filesystem::path target_path = (base_root / relative_target).lexically_normal();
        if (!PathIsAtOrInside(target_path, base_root)) {
            return MakeRagToolError("Resolved target path escapes the configured write-file folder.");
        }

        const bool target_folder_existed = std::filesystem::exists(target_path.parent_path(), ec);
        ec.clear();
        std::filesystem::create_directories(target_path.parent_path(), ec);
        if (ec) {
            return MakeRagToolError("Could not create target folder: " + ec.message());
        }
        if (std::filesystem::exists(target_path, ec) && !overwrite) {
            return MakeRagToolError(
                "Target file already exists. Retry with overwrite=true or choose another target_relative_path.",
                {{"target_path", WideToUtf8(target_path.wstring())}});
        }

        if (write_extracted_text) {
            std::ofstream output(target_path, std::ios::binary | std::ios::trunc);
            if (!output.is_open()) {
                return MakeRagToolError("Could not open target file for writing: " +
                                        WideToUtf8(target_path.wstring()));
            }
            output.write(extracted_text.data(), static_cast<std::streamsize>(extracted_text.size()));
            if (!output.good()) {
                return MakeRagToolError("Failed while writing extracted Markdown/text to the target file.");
            }
        } else {
            std::filesystem::copy_file(
                original_source_path,
                target_path,
                overwrite ? std::filesystem::copy_options::overwrite_existing
                          : std::filesystem::copy_options::none,
                ec);
            if (ec) {
                return MakeRagToolError("Could not copy original file: " + ec.message());
            }
        }

        ec.clear();
        const uintmax_t bytes_written = std::filesystem::file_size(target_path, ec);
        nlohmann::json payload = {
            {"success", true},
            {"tool", tool_name},
            {"rag_id", rag_id},
            {"rag_name", RagLibraryDisplayName(library->library)},
            {"document_id", document_id},
            {"document_title", document->display_name},
            {"version", version},
            {"target_path", WideToUtf8(target_path.wstring())},
            {"configured_folder_template", library->binding.export_path_template},
            {"resolved_folder", WideToUtf8(base_root.wstring())},
            {"relative_path", WideToUtf8(relative_target.generic_wstring())},
            {"target_folder_relative_path", requested_folder_path},
            {"target_file_name", requested_file_name.empty()
                ? WideToUtf8(default_name.wstring())
                : requested_file_name},
            {"missing_directories_created", !configured_folder_existed || !target_folder_existed},
            {"creates_missing_directories", true},
            {"source_path", write_extracted_text
                ? document->extracted_relative_path
                : WideToUtf8(original_source_path.wstring())},
            {"overwrite", overwrite},
            {"bytes_written", ec ? 0 : bytes_written},
        };
        add_route_metadata(payload);
        return MakeJsonToolResult(std::move(payload));
    }

    if (tool_name == kIngestGeneratedDocumentToolName) {
        const auto write_libraries = GetProjectRagToolLibraries(rag_service, project_id, true);
        if (write_libraries.empty()) {
            return MakeRagToolError("No write-enabled RAG libraries are exposed as tools for this project.");
        }

        std::string rag_id = Trim(JsonStringOr(arguments, "rag_id"));
        if (rag_id.empty()) {
            const auto default_it = std::find_if(
                write_libraries.begin(),
                write_libraries.end(),
                [](const RagToolLibrary& item) {
                    return item.binding.default_ingest_target;
                });
            if (default_it != write_libraries.end()) {
                rag_id = default_it->library.id;
            } else if (write_libraries.size() == 1) {
                rag_id = write_libraries.front().library.id;
            }
        }
        const RagToolLibrary* library = FindRagToolLibrary(write_libraries, rag_id);
        if (!library) {
            nlohmann::json writable = nlohmann::json::array();
            for (const auto& item : write_libraries) {
                writable.push_back(RagToolLibraryToJson(item));
            }
            return MakeRagToolError(
                "rag_ingest_generated_document requires a write-enabled exposed rag_id when no default ingest target is available.",
                {{"write_enabled_libraries", writable}});
        }

        const std::string title = Trim(JsonStringOr(arguments, "title"));
        const std::string content = JsonStringOr(arguments, "content");
        if (title.empty() || Trim(content).empty()) {
            return MakeRagToolError("rag_ingest_generated_document requires non-empty title and content.");
        }

        std::string source_uri = Trim(JsonStringOr(arguments, "source_uri"));
        if (source_uri.empty()) {
            source_uri = "generated://rag-tool/" + MakeId("source");
        }

        std::string metadata_json;
        if (arguments.contains("metadata")) {
            if (arguments["metadata"].is_object()) {
                metadata_json = arguments["metadata"].dump();
            } else if (arguments["metadata"].is_string()) {
                metadata_json = arguments["metadata"].get<std::string>();
            }
        }

        RagIngestionResult ingestion =
            rag_service->IngestGeneratedDocument(rag_id, title, content, metadata_json, source_uri);
        nlohmann::json errors = nlohmann::json::array();
        for (const auto& error : ingestion.errors) {
            errors.push_back(error);
        }

        std::string document_id;
        if (ingestion.success) {
            for (const auto& summary : rag_service->ListDocuments(rag_id)) {
                if (summary.document.original_source_uri == source_uri) {
                    document_id = summary.document.id;
                    break;
                }
            }
        }

        nlohmann::json payload = {
            {"success", ingestion.success},
            {"tool", tool_name},
            {"rag_id", rag_id},
            {"rag_name", RagLibraryDisplayName(library->library)},
            {"document_id", document_id},
            {"title", title},
            {"source_uri", source_uri},
            {"files_ingested", ingestion.files_ingested},
            {"files_skipped", ingestion.files_skipped},
            {"chunks_added", ingestion.chunks_added},
            {"errors", errors},
        };
        add_route_metadata(payload);
        return MakeJsonToolResult(std::move(payload), ingestion.success);
    }

    return MakeRagToolError("Unknown RAG tool: " + exposed_tool_name);
}

inline std::string TraceTitleForRagTool(
    const std::string& exposed_tool_name,
    const std::unordered_map<std::string, RagToolRoute>& routes) {
    const auto it = routes.find(exposed_tool_name);
    if (it == routes.end()) {
        return "RAG / " + exposed_tool_name;
    }
    return it->second.rag_name + " / " + it->second.base_tool_name;
}

}  // namespace rag_tools
