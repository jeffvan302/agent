#pragma once

#include "mcp_manager.h"
#include "openai_client.h"
#include "util.h"
#include "variable_resolver.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace artifact_memory_tools {

constexpr char kGetIndexToolName[] = "artifact_memory_get_index";
constexpr char kGetArtifactToolName[] = "artifact_memory_get_artifact";
constexpr char kGetLatestToolName[] = "artifact_memory_get_latest";
constexpr char kListVersionsToolName[] = "artifact_memory_list_versions";
constexpr char kGetVersionToolName[] = "artifact_memory_get_version";
constexpr char kRestoreVersionToolName[] = "artifact_memory_restore_version";
constexpr char kCodeStoreFragmentToolName[] = "code_memory_store_fragment";
constexpr char kCodeSearchToolName[] = "code_memory_search";
constexpr char kCodeListVersionsToolName[] = "code_memory_list_versions";
constexpr char kCodeGetVersionToolName[] = "code_memory_get_version";
constexpr char kCodeRestoreVersionToolName[] = "code_memory_restore_version";
constexpr char kServerName[] = "Artifact Memory";
constexpr char kDefaultStorageFolderTemplate[] = "$ProjectFolder$\\.agent\\.memory\\$CHATID$";

struct ArtifactMemoryRuntime {
    bool enabled = false;
    std::string project_id;
    std::string chat_id;
    std::string config_id;
    std::string config_name;
    std::string server_name = kServerName;
    std::filesystem::path storage_root;
    std::string storage_root_utf8;
    int max_injected_rows = 12;
};

struct ArtifactMemoryToolSet {
    ArtifactMemoryRuntime runtime;
    std::vector<ChatToolDefinition> definitions;
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

inline McpToolCallResult MakeJsonToolResult(nlohmann::json payload, bool success = true) {
    McpToolCallResult result;
    result.success = success;
    result.is_tool_error = !success;
    result.raw_result_json = payload.dump(2);
    result.content_text = result.raw_result_json;
    return result;
}

inline McpToolCallResult MakeArtifactToolError(
    const ArtifactMemoryRuntime& runtime,
    const std::string& tool_name,
    const std::string& message,
    nlohmann::json details = nlohmann::json::object()) {
    nlohmann::json payload = {
        {"success", false},
        {"tool", tool_name},
        {"server", runtime.server_name},
        {"project_id", runtime.project_id},
        {"chat_id", runtime.chat_id},
        {"error", message},
    };
    if (!details.empty()) {
        payload["details"] = std::move(details);
    }
    return MakeJsonToolResult(std::move(payload), false);
}

inline std::string LowerTrimmed(std::string value) {
    value = Trim(std::move(value));
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

inline bool IsArtifactMemoryToolName(const std::string& name) {
    return name == kGetIndexToolName ||
           name == kGetArtifactToolName ||
           name == kGetLatestToolName ||
           name == kListVersionsToolName ||
           name == kGetVersionToolName ||
           name == kRestoreVersionToolName ||
           name == kCodeStoreFragmentToolName ||
           name == kCodeSearchToolName ||
           name == kCodeListVersionsToolName ||
           name == kCodeGetVersionToolName ||
           name == kCodeRestoreVersionToolName;
}

inline std::string FriendlyArtifactToolTitle(const std::string& tool_name) {
    if (tool_name == kGetIndexToolName) return "Get Index";
    if (tool_name == kGetArtifactToolName) return "Get Artifact";
    if (tool_name == kGetLatestToolName) return "Get Latest";
    if (tool_name == kListVersionsToolName) return "List Versions";
    if (tool_name == kGetVersionToolName) return "Get Version";
    if (tool_name == kRestoreVersionToolName) return "Restore Version";
    if (tool_name == kCodeStoreFragmentToolName) return "Store Code Fragment";
    if (tool_name == kCodeSearchToolName) return "Search Code Memory";
    if (tool_name == kCodeListVersionsToolName) return "List Code Versions";
    if (tool_name == kCodeGetVersionToolName) return "Get Code Version";
    if (tool_name == kCodeRestoreVersionToolName) return "Restore Code Version";
    return tool_name;
}

inline std::string BuildArtifactMemoryUsageContext() {
    return
        "Artifact Memory and Code Reference Memory Rules:\n"
        "Layer 0 Artifact Memory preserves exact prior versions of generated code, diagrams, HTML, SVG, configuration, and other durable artifacts.\n"
        "Code Reference Memory is an append-only, version-aware index for project code, external libraries, SDKs, vendored code, and generated artifacts. It stores Markdown records with exact code/reference fragments, summaries, reference identity, source file paths, symbol metadata, hashes, and change history.\n"
        "- For requests like go back, restore, revert, previous version, older version, undo this change, or return to a prior artifact state, inspect Artifact Memory first when these tools are available.\n"
        "- Use artifact_memory_get_index to discover keys, artifact_memory_list_versions to inspect history, and artifact_memory_restore_version or artifact_memory_get_version to retrieve the exact prior content.\n"
        "- Use code_memory_store_fragment after analyzing meaningful functions, classes, modules, APIs, or call patterns so future turns can find them without keeping full code in context. Store a new version when code or analysis changes, and include change_summary and change_reason.\n"
        "- Use code_memory_search to find where a function/class/API is located and what it does. Line numbers in code memory are hints only; verify current code by content_hash, qualified_symbol, file_path, and current source before editing.\n"
        "- Use code_memory_list_versions and code_memory_restore_version for auditable function/class/API rollback over time.\n"
        "- If the user wants a file restored, write the returned restored_content/content exactly with the appropriate filesystem or MCP tool after selecting the version.\n"
        "- In the final answer, state whether the restore came from Artifact Memory, compressed context, or manual reconstruction.";
}

inline bool SelectedConfigHasLayer0(
    const std::optional<ContextCompressionConfig>& selected_config) {
    return selected_config &&
        selected_config->strategy == ContextCompressionStrategy::HierarchicalStructured &&
        selected_config->layers.layer0.enabled;
}

inline bool ShouldExposeArtifactMemoryTools(
    const std::optional<ContextCompressionConfig>& selected_config,
    bool explicit_tool_enabled) {
    return explicit_tool_enabled || SelectedConfigHasLayer0(selected_config);
}

inline std::string TraceTitleForArtifactMemoryTool(const std::string& tool_name) {
    return std::string(kServerName) + " / " + FriendlyArtifactToolTitle(tool_name);
}

inline bool PathIsAtOrInside(const std::filesystem::path& candidate,
                             const std::filesystem::path& root) {
    std::error_code ec;
    const auto canonical_root = std::filesystem::weakly_canonical(root, ec);
    if (ec) return false;
    const auto canonical_candidate = std::filesystem::weakly_canonical(candidate, ec);
    if (ec) return false;

    auto root_it = canonical_root.begin();
    auto candidate_it = canonical_candidate.begin();
    for (; root_it != canonical_root.end(); ++root_it, ++candidate_it) {
        if (candidate_it == canonical_candidate.end() || *root_it != *candidate_it) {
            return false;
        }
    }
    return true;
}

inline std::optional<std::string> ReadWholeFileUtf8(const std::filesystem::path& path,
                                                    std::string* error = nullptr) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        if (error) {
            *error = "Could not open file: " + WideToUtf8(path.wstring());
        }
        return std::nullopt;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

inline std::filesystem::path IndexPath(const ArtifactMemoryRuntime& runtime) {
    return runtime.storage_root / "index.json";
}

inline bool LoadIndexJson(const ArtifactMemoryRuntime& runtime,
                          nlohmann::json* index_out,
                          bool* existed = nullptr,
                          std::string* error = nullptr) {
    if (!index_out) {
        if (error) *error = "Artifact memory index output was not provided.";
        return false;
    }
    *index_out = nlohmann::json{
        {"schema_version", 1},
        {"project_id", runtime.project_id},
        {"chat_id", runtime.chat_id},
        {"artifacts", nlohmann::json::array()},
    };

    const auto index_path = IndexPath(runtime);
    if (existed) *existed = std::filesystem::exists(index_path);
    if (!std::filesystem::exists(index_path)) {
        return true;
    }

    const auto text = ReadWholeFileUtf8(index_path, error);
    if (!text) {
        return false;
    }

    try {
        *index_out = nlohmann::json::parse(*text);
        if (!index_out->is_object()) {
            if (error) *error = "Artifact memory index is not a JSON object.";
            return false;
        }
        if (!index_out->contains("artifacts") || !(*index_out)["artifacts"].is_array()) {
            (*index_out)["artifacts"] = nlohmann::json::array();
        }
        return true;
    } catch (const std::exception& ex) {
        if (error) *error = std::string("Could not parse artifact memory index: ") + ex.what();
        return false;
    } catch (...) {
        if (error) *error = "Could not parse artifact memory index.";
        return false;
    }
}

inline std::vector<nlohmann::json> FilterArtifacts(
    const nlohmann::json& index,
    const std::string& artifact_key_filter,
    const std::string& type_filter,
    bool latest_only) {
    std::vector<nlohmann::json> matches;
    if (!index.is_object() || !index.contains("artifacts") || !index["artifacts"].is_array()) {
        return matches;
    }

    const std::string key_filter = LowerTrimmed(artifact_key_filter);
    const std::string normalized_type = LowerTrimmed(type_filter);

    for (const auto& artifact : index["artifacts"]) {
        if (!artifact.is_object()) continue;
        if (!key_filter.empty() &&
            LowerTrimmed(artifact.value("artifact_key", "")) != key_filter) {
            continue;
        }
        if (!normalized_type.empty() &&
            LowerTrimmed(artifact.value("type", "")) != normalized_type) {
            continue;
        }
        if (latest_only && artifact.contains("latest") &&
            artifact["latest"].is_boolean() && !artifact["latest"].get<bool>()) {
            continue;
        }
        matches.push_back(artifact);
    }

    std::sort(matches.begin(), matches.end(),
        [](const nlohmann::json& left, const nlohmann::json& right) {
            const std::string left_key = left.value("artifact_key", "");
            const std::string right_key = right.value("artifact_key", "");
            if (left_key != right_key) return left_key < right_key;
            const int left_version = left.value("version", 0);
            const int right_version = right.value("version", 0);
            return left_version > right_version;
        });
    return matches;
}

inline std::optional<nlohmann::json> FindArtifactById(const nlohmann::json& index,
                                                      const std::string& artifact_id) {
    if (!index.is_object() || !index.contains("artifacts") || !index["artifacts"].is_array()) {
        return std::nullopt;
    }
    for (const auto& artifact : index["artifacts"]) {
        if (artifact.is_object() && artifact.value("artifact_id", "") == artifact_id) {
            return artifact;
        }
    }
    return std::nullopt;
}

inline std::optional<nlohmann::json> FindLatestArtifact(const nlohmann::json& index,
                                                        const std::string& artifact_key) {
    auto matches = FilterArtifacts(index, artifact_key, {}, false);
    if (matches.empty()) return std::nullopt;

    for (const auto& artifact : matches) {
        if (artifact.value("latest", false)) {
            return artifact;
        }
    }

    return *std::max_element(matches.begin(), matches.end(),
        [](const nlohmann::json& left, const nlohmann::json& right) {
            return left.value("version", 0) < right.value("version", 0);
        });
}

inline std::optional<nlohmann::json> FindArtifactVersion(const nlohmann::json& index,
                                                        const std::string& artifact_key,
                                                        int version) {
    if (!index.is_object() || !index.contains("artifacts") || !index["artifacts"].is_array()) {
        return std::nullopt;
    }
    const std::string key = LowerTrimmed(artifact_key);
    for (const auto& artifact : index["artifacts"]) {
        if (!artifact.is_object()) continue;
        if (LowerTrimmed(artifact.value("artifact_key", "")) != key) continue;
        if (artifact.value("version", 0) == version) {
            return artifact;
        }
    }
    return std::nullopt;
}

inline int JsonIntValue(const nlohmann::json& value, const char* key, int fallback = 0) {
    if (!value.is_object() || !value.contains(key) || value[key].is_null()) {
        return fallback;
    }
    try {
        if (value[key].is_number_integer()) return value[key].get<int>();
        if (value[key].is_string()) return std::stoi(Trim(value[key].get<std::string>()));
    } catch (...) {
    }
    return fallback;
}

inline std::string JsonStringValue(const nlohmann::json& value,
                                   const char* key,
                                   const std::string& fallback = {}) {
    if (!value.is_object() || !value.contains(key) || value[key].is_null()) {
        return fallback;
    }
    if (value[key].is_string()) {
        return Trim(value[key].get<std::string>());
    }
    if (value[key].is_number_integer()) {
        return std::to_string(value[key].get<int>());
    }
    if (value[key].is_number_unsigned()) {
        return std::to_string(value[key].get<unsigned long long>());
    }
    if (value[key].is_number_float()) {
        std::ostringstream stream;
        stream << value[key].get<double>();
        return stream.str();
    }
    if (value[key].is_boolean()) {
        return value[key].get<bool>() ? "true" : "false";
    }
    return fallback;
}

inline std::vector<std::string> JsonStringArrayValue(const nlohmann::json& value,
                                                     const char* key) {
    std::vector<std::string> out;
    if (!value.is_object() || !value.contains(key) || !value[key].is_array()) {
        return out;
    }
    for (const auto& item : value[key]) {
        if (item.is_string()) {
            const std::string text = Trim(item.get<std::string>());
            if (!text.empty()) out.push_back(text);
        }
    }
    return out;
}

inline std::string StableKeyPart(std::string value) {
    value = Trim(std::move(value));
    std::string normalized;
    normalized.reserve(value.size());
    bool last_was_underscore = false;
    for (const unsigned char ch : value) {
        if (std::isalnum(ch)) {
            normalized.push_back(static_cast<char>(std::tolower(ch)));
            last_was_underscore = false;
        } else if (!last_was_underscore) {
            normalized.push_back('_');
            last_was_underscore = true;
        }
    }
    while (!normalized.empty() && normalized.front() == '_') normalized.erase(normalized.begin());
    while (!normalized.empty() && normalized.back() == '_') normalized.pop_back();
    return normalized;
}

inline std::string Fnv1aHashHex(const std::string& value) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : value) {
        hash ^= static_cast<uint64_t>(ch);
        hash *= 1099511628211ULL;
    }
    std::ostringstream stream;
    stream << std::hex;
    stream.width(16);
    stream.fill('0');
    stream << hash;
    return stream.str();
}

inline std::string ShortHash(const std::string& value) {
    const std::string hash = Fnv1aHashHex(value);
    return hash.substr(0, std::min<size_t>(hash.size(), 8));
}

inline bool WriteWholeFileUtf8(const std::filesystem::path& path,
                               const std::string& text,
                               std::string* error = nullptr) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        if (error) *error = "Could not create directory: " + WideToUtf8(path.parent_path().wstring());
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        if (error) *error = "Could not open file for writing: " + WideToUtf8(path.wstring());
        return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output.good()) {
        if (error) *error = "Could not write file: " + WideToUtf8(path.wstring());
        return false;
    }
    return true;
}

inline std::string YamlEscape(std::string value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        if (ch == '\\' || ch == '"') escaped.push_back('\\');
        if (ch == '\r') continue;
        if (ch == '\n') {
            escaped += "\\n";
        } else {
            escaped.push_back(ch);
        }
    }
    return escaped;
}

inline std::string MarkdownFenceForContent(const std::string& content) {
    return content.find("```") == std::string::npos ? "```" : "````";
}

inline std::string BuildArtifactKeyFromCodeRecord(const nlohmann::json& args) {
    const std::string explicit_key = StableKeyPart(JsonStringValue(args, "artifact_key"));
    if (!explicit_key.empty()) return explicit_key;

    std::string seed = JsonStringValue(args, "reference_id");
    const std::string qualified_symbol = JsonStringValue(args, "qualified_symbol");
    const std::string symbol = JsonStringValue(args, "symbol");
    const std::string file_path = JsonStringValue(args, "file_path");
    if (!qualified_symbol.empty()) {
        seed += " " + qualified_symbol;
    } else if (!symbol.empty()) {
        seed += " " + symbol;
    } else {
        seed += " " + file_path;
    }
    const std::string stable = StableKeyPart(seed);
    return stable.empty() ? ("code_fragment_" + ShortHash(args.dump())) : ("code_" + stable);
}

inline std::filesystem::path SafeCodeMemoryRelativePath(const nlohmann::json& artifact) {
    std::string key = StableKeyPart(artifact.value("artifact_key", "code_fragment"));
    if (key.empty()) key = "code_fragment";
    const std::string hash = ShortHash(key + artifact.value("artifact_id", ""));
    if (key.size() > 80) key = key.substr(0, 80);
    std::ostringstream file_name;
    file_name << key << "_v";
    file_name.width(3);
    file_name.fill('0');
    file_name << artifact.value("version", 1) << "_" << hash << ".md";

    std::string reference = StableKeyPart(artifact.value("reference_id", "reference"));
    if (reference.empty()) reference = "reference";
    if (reference.size() > 64) reference = reference.substr(0, 64);
    return std::filesystem::path("code") / reference / file_name.str();
}

inline std::string BuildCodeFragmentMarkdown(const nlohmann::json& artifact,
                                             const std::string& content) {
    std::ostringstream stream;
    auto yaml_line = [&](const std::string& key, const std::string& value) {
        stream << key << ": \"" << YamlEscape(value) << "\"\n";
    };
    auto yaml_int_line = [&](const std::string& key, int value) {
        if (value > 0) stream << key << ": " << value << "\n";
    };

    stream << "---\n";
    yaml_line("artifact_id", artifact.value("artifact_id", ""));
    yaml_line("artifact_key", artifact.value("artifact_key", ""));
    stream << "version: " << artifact.value("version", 1) << "\n";
    yaml_line("kind", artifact.value("kind", "code_fragment"));
    yaml_line("type", artifact.value("type", "code"));
    yaml_line("language", artifact.value("language", ""));
    yaml_line("status", artifact.value("status", "active"));
    stream << "latest: " << (artifact.value("latest", false) ? "true" : "false") << "\n";
    yaml_line("supersedes", artifact.value("supersedes", ""));
    yaml_line("project_id", artifact.value("project_id", ""));
    yaml_line("chat_id", artifact.value("chat_id", ""));
    yaml_line("reference_id", artifact.value("reference_id", ""));
    yaml_line("reference_name", artifact.value("reference_name", ""));
    yaml_line("reference_type", artifact.value("reference_type", ""));
    yaml_line("reference_version", artifact.value("reference_version", ""));
    yaml_line("source_uri", artifact.value("source_uri", ""));
    yaml_line("file_path", artifact.value("file_path", ""));
    yaml_line("memory_file_path", artifact.value("memory_file_path", ""));
    yaml_line("symbol_kind", artifact.value("symbol_kind", ""));
    yaml_line("symbol", artifact.value("symbol", ""));
    yaml_line("qualified_symbol", artifact.value("qualified_symbol", ""));
    yaml_int_line("start_line_hint", artifact.value("start_line_hint", 0));
    yaml_int_line("end_line_hint", artifact.value("end_line_hint", 0));
    yaml_line("locator_strategy", artifact.value("locator_strategy", "symbol_plus_hash"));
    yaml_line("content_hash", artifact.value("content_hash", ""));
    yaml_line("context_hash", artifact.value("context_hash", ""));
    yaml_line("summary", artifact.value("summary", ""));
    yaml_line("change_summary", artifact.value("change_summary", ""));
    yaml_line("change_reason", artifact.value("change_reason", ""));
    stream << "tags:\n";
    if (artifact.contains("tags") && artifact["tags"].is_array() && !artifact["tags"].empty()) {
        for (const auto& tag : artifact["tags"]) {
            if (tag.is_string()) stream << "  - \"" << YamlEscape(tag.get<std::string>()) << "\"\n";
        }
    } else {
        stream << "  - \"code_memory\"\n";
    }
    stream << "behavior_delta:\n";
    if (artifact.contains("behavior_delta") && artifact["behavior_delta"].is_array() && !artifact["behavior_delta"].empty()) {
        for (const auto& item : artifact["behavior_delta"]) {
            if (item.is_string()) stream << "  - \"" << YamlEscape(item.get<std::string>()) << "\"\n";
        }
    } else {
        stream << "  - \"No behavior delta recorded.\"\n";
    }
    yaml_line("created_at", artifact.value("created_at", ""));
    yaml_line("updated_at", artifact.value("updated_at", ""));
    stream << "---\n\n";

    stream << "# " << artifact.value("qualified_symbol", artifact.value("symbol", artifact.value("artifact_key", "Code Fragment"))) << "\n\n";
    stream << "## Reference\n\n";
    stream << "- Reference: " << artifact.value("reference_name", artifact.value("reference_id", "")) << "\n";
    stream << "- Type: " << artifact.value("reference_type", "") << "\n";
    stream << "- Version: " << artifact.value("reference_version", "") << "\n";
    stream << "- Source URI: " << artifact.value("source_uri", "") << "\n";
    stream << "- Source file: " << artifact.value("file_path", "") << "\n";
    stream << "- Line hints: " << artifact.value("start_line_hint", 0) << "-" << artifact.value("end_line_hint", 0) << "\n\n";
    stream << "## Summary\n\n" << artifact.value("summary", "") << "\n\n";
    stream << "## Change Summary\n\n" << artifact.value("change_summary", "") << "\n\n";
    stream << "## Change Reason\n\n" << artifact.value("change_reason", "") << "\n\n";
    stream << "## Behavior Delta\n\n";
    if (artifact.contains("behavior_delta") && artifact["behavior_delta"].is_array()) {
        for (const auto& item : artifact["behavior_delta"]) {
            if (item.is_string()) stream << "- " << item.get<std::string>() << "\n";
        }
    }
    stream << "\n## Locator Notes\n\n";
    stream << "Line numbers are hints only. Verify current code by content_hash, qualified_symbol, source file path, and parser/search before editing.\n\n";
    stream << "## Artifact\n\n";
    const std::string fence = MarkdownFenceForContent(content);
    stream << fence << artifact.value("language", "") << "\n";
    stream << content << "\n";
    stream << fence << "\n";
    return stream.str();
}

inline std::string BuildBridgeIndexMarkdown(const nlohmann::json& index) {
    std::vector<nlohmann::json> latest;
    if (index.is_object() && index.contains("artifacts") && index["artifacts"].is_array()) {
        for (const auto& artifact : index["artifacts"]) {
            if (artifact.is_object() && artifact.value("latest", false)) {
                latest.push_back(artifact);
            }
        }
    }
    std::sort(latest.begin(), latest.end(), [](const nlohmann::json& left, const nlohmann::json& right) {
        return left.value("artifact_key", "") < right.value("artifact_key", "");
    });

    std::ostringstream stream;
    stream << "# Artifact Memory Index\n\n";
    if (latest.empty()) {
        stream << "(No artifacts stored yet)\n";
        return stream.str();
    }
    stream << "| Kind | Key | Version | Reference | Symbol | Type | Summary | Status | Record |\n";
    stream << "| --- | --- | --- | --- | --- | --- | --- | --- | --- |\n";
    for (const auto& artifact : latest) {
        stream << "| " << artifact.value("kind", "artifact") << " | "
               << artifact.value("artifact_key", "") << " | "
               << artifact.value("version", 1) << " | "
               << artifact.value("reference_id", "") << " | "
               << artifact.value("qualified_symbol", artifact.value("symbol", "")) << " | "
               << artifact.value("type", "") << " | "
               << artifact.value("summary", "") << " | "
               << artifact.value("status", "active") << " | "
               << artifact.value("memory_file_path", artifact.value("file_path", "")) << " |\n";
    }
    return stream.str();
}

inline bool SaveIndexJson(const ArtifactMemoryRuntime& runtime,
                          const nlohmann::json& index,
                          std::string* error = nullptr) {
    if (!WriteWholeFileUtf8(IndexPath(runtime), index.dump(2), error)) {
        return false;
    }
    return WriteWholeFileUtf8(runtime.storage_root / "INDEX.md", BuildBridgeIndexMarkdown(index), error);
}

inline std::vector<nlohmann::json*> CollectArtifactsForKeyMutable(nlohmann::json& index,
                                                                  const std::string& artifact_key) {
    std::vector<nlohmann::json*> out;
    if (!index.is_object() || !index.contains("artifacts") || !index["artifacts"].is_array()) return out;
    const std::string key = LowerTrimmed(artifact_key);
    for (auto& artifact : index["artifacts"]) {
        if (artifact.is_object() && LowerTrimmed(artifact.value("artifact_key", "")) == key) {
            out.push_back(&artifact);
        }
    }
    return out;
}

inline bool IsCodeMemoryKind(const std::string& kind) {
    const std::string normalized = LowerTrimmed(kind);
    return normalized == "code_fragment" ||
           normalized == "api_reference" ||
           normalized == "module_summary" ||
           normalized == "class_summary" ||
           normalized == "call_pattern" ||
           normalized == "dependency_note";
}

inline bool IsCodeMemoryArtifact(const nlohmann::json& artifact) {
    if (!artifact.is_object()) return false;
    if (IsCodeMemoryKind(artifact.value("kind", ""))) return true;
    return artifact.contains("reference_id") && artifact.contains("qualified_symbol");
}

inline std::string BuildCodeSearchHaystack(const nlohmann::json& artifact) {
    std::ostringstream stream;
    const char* keys[] = {
        "artifact_key", "kind", "type", "language", "reference_id", "reference_name",
        "reference_type", "reference_version", "source_uri", "file_path", "symbol_kind",
        "symbol", "qualified_symbol", "summary", "change_summary", "change_reason",
        "content_hash", "context_hash"
    };
    for (const char* key : keys) {
        if (artifact.contains(key) && artifact[key].is_string()) {
            stream << artifact[key].get<std::string>() << "\n";
        }
    }
    for (const char* key : {"tags", "behavior_delta"}) {
        if (artifact.contains(key) && artifact[key].is_array()) {
            for (const auto& item : artifact[key]) {
                if (item.is_string()) stream << item.get<std::string>() << "\n";
            }
        }
    }
    return LowerTrimmed(stream.str());
}

inline bool ContainsLower(const std::string& haystack, const std::string& needle) {
    const std::string normalized = LowerTrimmed(needle);
    return normalized.empty() || haystack.find(normalized) != std::string::npos;
}

inline bool CodeArtifactMatchesSearch(const nlohmann::json& artifact,
                                      const nlohmann::json& args) {
    if (!IsCodeMemoryArtifact(artifact)) return false;
    if (args.value("latest_only", true) && !artifact.value("latest", false)) return false;

    auto exact_filter = [&](const char* key) {
        const std::string filter = LowerTrimmed(JsonStringValue(args, key));
        return filter.empty() || LowerTrimmed(artifact.value(key, "")) == filter;
    };
    if (!exact_filter("reference_id")) return false;
    if (!exact_filter("reference_type")) return false;
    if (!exact_filter("kind")) return false;
    if (!exact_filter("language")) return false;

    const std::string haystack = BuildCodeSearchHaystack(artifact);
    if (!ContainsLower(haystack, JsonStringValue(args, "query"))) return false;
    if (!ContainsLower(haystack, JsonStringValue(args, "symbol"))) return false;
    if (!ContainsLower(haystack, JsonStringValue(args, "file_path"))) return false;
    return true;
}

inline nlohmann::json CompactCodeArtifact(const nlohmann::json& artifact) {
    nlohmann::json compact = nlohmann::json::object();
    const char* keys[] = {
        "artifact_id", "artifact_key", "version", "latest", "status", "kind", "type",
        "language", "reference_id", "reference_name", "reference_type", "reference_version",
        "source_uri", "file_path", "memory_file_path", "symbol_kind", "symbol",
        "qualified_symbol", "start_line_hint", "end_line_hint", "locator_strategy",
        "content_hash", "context_hash", "summary", "change_summary", "change_reason",
        "tags", "behavior_delta", "created_at", "updated_at", "supersedes"
    };
    for (const char* key : keys) {
        if (artifact.contains(key)) compact[key] = artifact[key];
    }
    return compact;
}

inline McpToolCallResult StoreCodeMemoryFragment(const ArtifactMemoryRuntime& runtime,
                                                 nlohmann::json& index,
                                                 const nlohmann::json& args) {
    const std::string content = JsonStringValue(args, "content");
    const std::string reference_id = JsonStringValue(args, "reference_id");
    const std::string reference_type = JsonStringValue(args, "reference_type");
    const std::string file_path = JsonStringValue(args, "file_path");
    const std::string language = JsonStringValue(args, "language");
    const std::string symbol = JsonStringValue(args, "symbol");
    const std::string summary = JsonStringValue(args, "summary");
    const std::string change_summary = JsonStringValue(args, "change_summary");
    const std::string change_reason = JsonStringValue(args, "change_reason");
    if (content.empty() || reference_id.empty() || reference_type.empty() || file_path.empty() ||
        language.empty() || symbol.empty() || summary.empty() || change_summary.empty() ||
        change_reason.empty()) {
        return MakeArtifactToolError(runtime, kCodeStoreFragmentToolName,
            "code_memory_store_fragment requires reference_id, reference_type, file_path, language, symbol, summary, change_summary, change_reason, and content.");
    }

    if (!index.contains("artifacts") || !index["artifacts"].is_array()) {
        index["artifacts"] = nlohmann::json::array();
    }

    const std::string artifact_key = BuildArtifactKeyFromCodeRecord(args);
    const std::string content_hash = Fnv1aHashHex(content);
    const bool force_new_version = args.value("force_new_version", false);
    auto existing = CollectArtifactsForKeyMutable(index, artifact_key);

    nlohmann::json* latest = nullptr;
    int next_version = 1;
    for (auto* artifact : existing) {
        next_version = std::max(next_version, artifact->value("version", 0) + 1);
        if (artifact->value("latest", false) && (!latest || artifact->value("version", 0) > latest->value("version", 0))) {
            latest = artifact;
        }
    }

    if (latest && latest->value("content_hash", "") == content_hash && !force_new_version) {
        return MakeJsonToolResult({
            {"success", true},
            {"tool", kCodeStoreFragmentToolName},
            {"server", runtime.server_name},
            {"project_id", runtime.project_id},
            {"chat_id", runtime.chat_id},
            {"stored", false},
            {"reason", "Latest code memory version already has the same content_hash. No new version was created."},
            {"artifact", CompactCodeArtifact(*latest)},
        });
    }

    const std::string now = CurrentTimestampUtc();
    std::string supersedes;
    for (auto* artifact : existing) {
        if (artifact->value("latest", false)) {
            (*artifact)["latest"] = false;
            (*artifact)["status"] = "superseded";
            (*artifact)["updated_at"] = now;
            supersedes = artifact->value("artifact_id", supersedes);
        }
    }

    nlohmann::json artifact = {
        {"artifact_id", MakeId("codefrag")},
        {"artifact_key", artifact_key},
        {"version", next_version},
        {"latest", true},
        {"status", "active"},
        {"kind", JsonStringValue(args, "kind", "code_fragment")},
        {"type", JsonStringValue(args, "type", language)},
        {"language", language},
        {"project_id", runtime.project_id},
        {"chat_id", runtime.chat_id},
        {"reference_id", reference_id},
        {"reference_name", JsonStringValue(args, "reference_name", reference_id)},
        {"reference_type", reference_type},
        {"reference_version", JsonStringValue(args, "reference_version")},
        {"source_uri", JsonStringValue(args, "source_uri")},
        {"file_path", file_path},
        {"symbol_kind", JsonStringValue(args, "symbol_kind", "function")},
        {"symbol", symbol},
        {"qualified_symbol", JsonStringValue(args, "qualified_symbol", symbol)},
        {"start_line_hint", JsonIntValue(args, "start_line_hint")},
        {"end_line_hint", JsonIntValue(args, "end_line_hint")},
        {"locator_strategy", JsonStringValue(args, "locator_strategy", "symbol_plus_hash")},
        {"content_hash", content_hash},
        {"context_hash", JsonStringValue(args, "context_hash")},
        {"summary", summary},
        {"change_summary", change_summary},
        {"change_reason", change_reason},
        {"tags", JsonStringArrayValue(args, "tags")},
        {"behavior_delta", JsonStringArrayValue(args, "behavior_delta")},
        {"supersedes", supersedes},
        {"created_at", now},
        {"updated_at", now},
        {"last_seen_at", now},
    };
    if (artifact["tags"].empty()) {
        artifact["tags"] = nlohmann::json::array({"code_memory", artifact["kind"], language});
    }

    const auto relative_path = SafeCodeMemoryRelativePath(artifact);
    artifact["memory_file_path"] = WideToUtf8(relative_path.wstring());
    const auto markdown = BuildCodeFragmentMarkdown(artifact, content);
    std::string write_error;
    if (!WriteWholeFileUtf8(runtime.storage_root / relative_path, markdown, &write_error)) {
        return MakeArtifactToolError(runtime, kCodeStoreFragmentToolName, write_error);
    }

    index["artifacts"].push_back(artifact);
    index["updated_at"] = now;
    if (!SaveIndexJson(runtime, index, &write_error)) {
        return MakeArtifactToolError(runtime, kCodeStoreFragmentToolName, write_error);
    }

    return MakeJsonToolResult({
        {"success", true},
        {"tool", kCodeStoreFragmentToolName},
        {"server", runtime.server_name},
        {"project_id", runtime.project_id},
        {"chat_id", runtime.chat_id},
        {"stored", true},
        {"artifact", CompactCodeArtifact(artifact)},
        {"memory_path", WideToUtf8((runtime.storage_root / relative_path).wstring())},
        {"versioning", "append-only; prior latest versions are retained and marked superseded in the index"},
    });
}

inline std::optional<std::string> ExtractArtifactContentFromMarkdown(const std::string& markdown) {
    size_t search_from = markdown.find("\n## Artifact");
    if (search_from == std::string::npos) {
        search_from = markdown.find("## Artifact");
    }
    if (search_from == std::string::npos) {
        return std::nullopt;
    }

    const size_t fence = markdown.find("```", search_from);
    if (fence == std::string::npos) {
        return std::nullopt;
    }
    const size_t line_end = markdown.find('\n', fence + 3);
    if (line_end == std::string::npos) {
        return std::nullopt;
    }
    const size_t content_start = line_end + 1;
    size_t closing = markdown.rfind("\n```");
    if (closing == std::string::npos || closing < content_start) {
        closing = markdown.find("\n```", content_start);
    }
    if (closing == std::string::npos || closing < content_start) {
        return std::nullopt;
    }
    return markdown.substr(content_start, closing - content_start);
}

inline std::optional<std::filesystem::path> ResolveArtifactPath(
    const ArtifactMemoryRuntime& runtime,
    const nlohmann::json& artifact,
    std::string* error = nullptr) {
    std::string file_path_text = artifact.value("memory_file_path", "");
    if (Trim(file_path_text).empty()) {
        file_path_text = artifact.value("file_path", "");
    }
    if (Trim(file_path_text).empty()) {
        if (error) *error = "Artifact index entry does not contain file_path.";
        return std::nullopt;
    }

    std::filesystem::path candidate = std::filesystem::path(Utf8ToWide(file_path_text));
    if (candidate.is_relative()) {
        candidate = runtime.storage_root / candidate;
    }

    std::error_code ec;
    const auto canonical_candidate = std::filesystem::weakly_canonical(candidate, ec);
    if (ec || !PathIsAtOrInside(canonical_candidate, runtime.storage_root)) {
        if (error) {
            *error = "Resolved artifact path escapes the configured artifact memory folder.";
        }
        return std::nullopt;
    }
    return canonical_candidate;
}

inline McpToolCallResult ReadArtifactRecordResult(
    const ArtifactMemoryRuntime& runtime,
    const std::string& tool_name,
    const nlohmann::json& artifact) {
    std::string path_error;
    const auto artifact_path = ResolveArtifactPath(runtime, artifact, &path_error);
    if (!artifact_path) {
        return MakeArtifactToolError(runtime, tool_name, path_error);
    }
    std::string read_error;
    const auto markdown = ReadWholeFileUtf8(*artifact_path, &read_error);
    if (!markdown) {
        return MakeArtifactToolError(runtime, tool_name, read_error);
    }

    nlohmann::json payload = {
        {"success", true},
        {"tool", tool_name},
        {"server", runtime.server_name},
        {"project_id", runtime.project_id},
        {"chat_id", runtime.chat_id},
        {"artifact", artifact},
        {"artifact_path", WideToUtf8(artifact_path->wstring())},
        {"markdown", *markdown},
    };
    if (const auto content = ExtractArtifactContentFromMarkdown(*markdown)) {
        payload["content"] = *content;
        payload["content_hash"] = artifact.value("content_hash", "");
    }
    return MakeJsonToolResult(std::move(payload));
}

inline ArtifactMemoryToolSet BuildArtifactMemoryToolSet(
    const std::optional<ContextCompressionConfig>& selected_config,
    const std::string& project_id,
    const std::string& chat_id,
    const std::vector<ProjectMcpVariableValue>& project_variables = {},
    bool explicit_tool_enabled = false) {
    ArtifactMemoryToolSet set;
    const bool layer0_enabled = SelectedConfigHasLayer0(selected_config);
    if (!layer0_enabled && !explicit_tool_enabled) {
        return set;
    }

    std::string storage_folder_template;
    if (selected_config) {
        storage_folder_template = selected_config->layers.layer0.storage_folder_template;
    }
    if (Trim(storage_folder_template).empty()) {
        storage_folder_template = kDefaultStorageFolderTemplate;
    }

    const std::string resolved_folder = Trim(variable_resolver::ExpandTemplate(
        storage_folder_template,
        project_variables));
    if (resolved_folder.empty()) {
        return set;
    }

    const std::filesystem::path root = std::filesystem::path(Utf8ToWide(resolved_folder));
    if (!root.is_absolute()) {
        return set;
    }

    std::error_code ec;
    std::filesystem::create_directories(root / "artifacts", ec);
    if (!ec) {
        std::filesystem::create_directories(root / "code", ec);
    }
    if (ec && !std::filesystem::exists(root)) {
        return set;
    }

    set.runtime.enabled = true;
    set.runtime.project_id = project_id;
    set.runtime.chat_id = chat_id;
    set.runtime.config_id = layer0_enabled && selected_config
        ? selected_config->id
        : std::string("built_in_artifact_memory");
    set.runtime.config_name = layer0_enabled && selected_config
        ? selected_config->name
        : std::string("Built-in Artifact/Code Memory");
    set.runtime.storage_root = root;
    set.runtime.storage_root_utf8 = WideToUtf8(root.wstring());
    set.runtime.max_injected_rows = layer0_enabled && selected_config
        ? std::max(1, selected_config->layers.layer0.max_injected_rows)
        : 12;

    ChatToolDefinition get_index;
    get_index.name = kGetIndexToolName;
    get_index.description =
        "Read the built-in Artifact Memory index for this chat. "
        "This read-only MCP-style server tracks code and diagram artifacts preserved by Layer 0 compression. "
        "Use this to discover what stable artifacts already exist before asking for revisions or handling restore/revert/go-back requests.";
    get_index.parameters_json = JsonObjectSchema({
        {"artifact_key", {{"type", "string"}, {"description", "Optional artifact_key to filter the index."}}},
        {"type", {{"type", "string"}, {"description", "Optional artifact type filter such as mermaid, svg, html, cpp, or vega-lite."}}},
        {"latest_only", {{"type", "boolean"}, {"description", "When true, return only latest versions. Defaults to true."}}}
    }).dump();
    set.definitions.push_back(std::move(get_index));

    ChatToolDefinition get_artifact;
    get_artifact.name = kGetArtifactToolName;
    get_artifact.description =
        "Read a full stored artifact Markdown record from the built-in Artifact Memory server by artifact_id. "
        "Use this when you need the full code block, diagram, summary, extracted content, and metadata for a specific prior artifact.";
    get_artifact.parameters_json = JsonObjectSchema({
        {"artifact_id", {{"type", "string"}, {"description", "The artifact_id from artifact_memory_get_index or artifact_memory_list_versions."}}}
    }, {"artifact_id"}).dump();
    set.definitions.push_back(std::move(get_artifact));

    ChatToolDefinition get_latest;
    get_latest.name = kGetLatestToolName;
    get_latest.description =
        "Read the latest stored artifact for an artifact_key from the built-in Artifact Memory server. "
        "Use this when continuing work on a known code block or diagram family.";
    get_latest.parameters_json = JsonObjectSchema({
        {"artifact_key", {{"type", "string"}, {"description", "The stable artifact_key to resolve to its latest version."}}}
    }, {"artifact_key"}).dump();
    set.definitions.push_back(std::move(get_latest));

    ChatToolDefinition list_versions;
    list_versions.name = kListVersionsToolName;
    list_versions.description =
        "List all known versions for an artifact_key from the built-in Artifact Memory server. "
        "Use this when you need history before deciding which version to inspect, revise, restore, or compare.";
    list_versions.parameters_json = JsonObjectSchema({
        {"artifact_key", {{"type", "string"}, {"description", "The stable artifact_key to list version history for."}}}
    }, {"artifact_key"}).dump();
    set.definitions.push_back(std::move(list_versions));

    ChatToolDefinition get_version;
    get_version.name = kGetVersionToolName;
    get_version.description =
        "Read an exact stored artifact version by artifact_key and version. "
        "Use this for previous-version comparisons and before restoring older content.";
    get_version.parameters_json = JsonObjectSchema({
        {"artifact_key", {{"type", "string"}, {"description", "The stable artifact_key to read."}}},
        {"version", {{"type", "integer"}, {"description", "The exact version number to read."}}}
    }, {"artifact_key", "version"}).dump();
    set.definitions.push_back(std::move(get_version));

    ChatToolDefinition restore_version;
    restore_version.name = kRestoreVersionToolName;
    restore_version.description =
        "Select an exact prior artifact version as the restore source and return restored_content. "
        "This tool is read-only and auditable; if the user wants a file restored, write restored_content exactly with a filesystem or MCP tool after this call.";
    restore_version.parameters_json = JsonObjectSchema({
        {"artifact_key", {{"type", "string"}, {"description", "The stable artifact_key to restore from."}}},
        {"version", {{"type", "integer"}, {"description", "The exact version number to restore."}}},
        {"restore_reason", {{"type", "string"}, {"description", "Optional concise reason for selecting this version."}}}
    }, {"artifact_key", "version"}).dump();
    set.definitions.push_back(std::move(restore_version));

    ChatToolDefinition store_code;
    store_code.name = kCodeStoreFragmentToolName;
    store_code.description =
        "Append a versioned code/reference-memory Markdown record for a function, class, module, library API, SDK call pattern, or dependency note. "
        "Use this after analyzing meaningful code or external library behavior. It preserves prior versions instead of overwriting them. "
        "Line numbers are stored as hints only; content_hash and symbol/file metadata are used for verification.";
    store_code.parameters_json = JsonObjectSchema({
        {"kind", {{"type", "string"}, {"description", "Record kind. Suggested: code_fragment, api_reference, module_summary, class_summary, call_pattern, dependency_note. Defaults to code_fragment."}}},
        {"reference_id", {{"type", "string"}, {"description", "Stable id for the project, library, SDK, vendored package, or generated artifact this code belongs to."}}},
        {"reference_name", {{"type", "string"}, {"description", "Human-readable reference name, such as Agent, NumPy, internal_math_lib, or Vendor SDK."}}},
        {"reference_type", {{"type", "string"}, {"description", "project, external_library, sdk, vendored_code, generated_artifact, or other source type."}}},
        {"reference_version", {{"type", "string"}, {"description", "Optional library/package/project version or commit label."}}},
        {"source_uri", {{"type", "string"}, {"description", "Optional source URI such as file://, git URL, package path, or generated:// reference."}}},
        {"file_path", {{"type", "string"}, {"description", "Source file path within the reference. This is not the Markdown memory path."}}},
        {"language", {{"type", "string"}, {"description", "Programming or artifact language, e.g. cpp, python, javascript, typescript, java."}}},
        {"symbol_kind", {{"type", "string"}, {"description", "function, class, method, module, api, call_pattern, dependency, etc."}}},
        {"symbol", {{"type", "string"}, {"description", "Local symbol/function/class/API name."}}},
        {"qualified_symbol", {{"type", "string"}, {"description", "Fully qualified symbol when available, e.g. package.module.Class.method."}}},
        {"start_line_hint", {{"type", "integer"}, {"description", "Optional source start line hint. It may shift after edits and must not be treated as authoritative."}}},
        {"end_line_hint", {{"type", "integer"}, {"description", "Optional source end line hint. It may shift after edits and must not be treated as authoritative."}}},
        {"locator_strategy", {{"type", "string"}, {"description", "How to relocate current code later. Defaults to symbol_plus_hash."}}},
        {"context_hash", {{"type", "string"}, {"description", "Optional hash of nearby source context, if known."}}},
        {"summary", {{"type", "string"}, {"description", "Concise summary of what this code/reference does and how it is used."}}},
        {"change_summary", {{"type", "string"}, {"description", "What this stored version adds, changes, or captures."}}},
        {"change_reason", {{"type", "string"}, {"description", "Why this version is being stored or why the change exists."}}},
        {"behavior_delta", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Optional behavior changes compared with the previous version."}}},
        {"tags", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Optional search tags."}}},
        {"content", {{"type", "string"}, {"description", "Exact code/reference fragment content to preserve."}}},
        {"artifact_key", {{"type", "string"}, {"description", "Optional stable key. If omitted, one is derived from reference_id and qualified_symbol/symbol/file_path."}}},
        {"force_new_version", {{"type", "boolean"}, {"description", "When true, create a new version even if content_hash matches the latest version."}}}
    }, {"reference_id", "reference_type", "file_path", "language", "symbol", "summary", "change_summary", "change_reason", "content"}).dump();
    set.definitions.push_back(std::move(store_code));

    ChatToolDefinition search_code;
    search_code.name = kCodeSearchToolName;
    search_code.description =
        "Search versioned Code Reference Memory by behavior, symbol, reference, source file path, language, tags, or summary. "
        "Use this to locate functions/classes/APIs without loading full source into context.";
    search_code.parameters_json = JsonObjectSchema({
        {"query", {{"type", "string"}, {"description", "Optional free-text query over symbol, summary, reference, file path, tags, and change notes."}}},
        {"reference_id", {{"type", "string"}, {"description", "Optional reference_id filter."}}},
        {"reference_type", {{"type", "string"}, {"description", "Optional reference_type filter."}}},
        {"kind", {{"type", "string"}, {"description", "Optional kind filter such as code_fragment or api_reference."}}},
        {"symbol", {{"type", "string"}, {"description", "Optional symbol or qualified_symbol substring filter."}}},
        {"file_path", {{"type", "string"}, {"description", "Optional source file path substring filter."}}},
        {"language", {{"type", "string"}, {"description", "Optional language filter."}}},
        {"latest_only", {{"type", "boolean"}, {"description", "When true, return only latest versions. Defaults to true."}}},
        {"max_results", {{"type", "integer"}, {"description", "Maximum results to return. Defaults to 20."}}}
    }).dump();
    set.definitions.push_back(std::move(search_code));

    ChatToolDefinition code_list_versions;
    code_list_versions.name = kCodeListVersionsToolName;
    code_list_versions.description =
        "List all stored versions for a code/reference-memory artifact_key, including change summaries and reasons.";
    code_list_versions.parameters_json = JsonObjectSchema({
        {"artifact_key", {{"type", "string"}, {"description", "The code memory artifact_key from code_memory_search or code_memory_store_fragment."}}}
    }, {"artifact_key"}).dump();
    set.definitions.push_back(std::move(code_list_versions));

    ChatToolDefinition code_get_version;
    code_get_version.name = kCodeGetVersionToolName;
    code_get_version.description =
        "Read an exact versioned code/reference-memory record by artifact_key and version, including exact content.";
    code_get_version.parameters_json = JsonObjectSchema({
        {"artifact_key", {{"type", "string"}, {"description", "The code memory artifact_key."}}},
        {"version", {{"type", "integer"}, {"description", "The exact version number."}}}
    }, {"artifact_key", "version"}).dump();
    set.definitions.push_back(std::move(code_get_version));

    ChatToolDefinition code_restore_version;
    code_restore_version.name = kCodeRestoreVersionToolName;
    code_restore_version.description =
        "Select an exact prior code/reference-memory version as an auditable restore source and return restored_content. "
        "After this call, write restored_content exactly with a filesystem or MCP tool if the user requested a source-file restore.";
    code_restore_version.parameters_json = JsonObjectSchema({
        {"artifact_key", {{"type", "string"}, {"description", "The code memory artifact_key."}}},
        {"version", {{"type", "integer"}, {"description", "The exact version number to restore."}}},
        {"restore_reason", {{"type", "string"}, {"description", "Optional concise reason for selecting this version."}}}
    }, {"artifact_key", "version"}).dump();
    set.definitions.push_back(std::move(code_restore_version));

    return set;
}

inline McpToolCallResult CallArtifactMemoryTool(
    const ArtifactMemoryRuntime& runtime,
    const std::string& tool_name,
    const std::string& arguments_json) {
    if (!runtime.enabled) {
        return MakeArtifactToolError(runtime, tool_name, "Artifact Memory is not enabled for this chat.");
    }
    if (!IsArtifactMemoryToolName(tool_name)) {
        return MakeArtifactToolError(runtime, tool_name, "Unknown Artifact Memory tool: " + tool_name);
    }

    nlohmann::json args = nlohmann::json::object();
    try {
        if (!Trim(arguments_json).empty()) {
            args = nlohmann::json::parse(arguments_json);
        }
    } catch (const std::exception& ex) {
        return MakeArtifactToolError(runtime, tool_name,
            std::string("Artifact Memory tool arguments were not valid JSON: ") + ex.what());
    } catch (...) {
        return MakeArtifactToolError(runtime, tool_name,
            "Artifact Memory tool arguments were not valid JSON.");
    }

    if (!args.is_object()) {
        return MakeArtifactToolError(runtime, tool_name,
            "Artifact Memory tool arguments must be a JSON object.");
    }

    nlohmann::json index;
    bool index_exists = false;
    std::string load_error;
    if (!LoadIndexJson(runtime, &index, &index_exists, &load_error)) {
        return MakeArtifactToolError(runtime, tool_name, load_error);
    }

    if (tool_name == kCodeStoreFragmentToolName) {
        return StoreCodeMemoryFragment(runtime, index, args);
    }

    if (tool_name == kCodeSearchToolName) {
        int max_results = JsonIntValue(args, "max_results", 20);
        if (max_results <= 0) max_results = 20;
        if (max_results > 100) max_results = 100;

        std::vector<nlohmann::json> matches;
        if (index.is_object() && index.contains("artifacts") && index["artifacts"].is_array()) {
            for (const auto& artifact : index["artifacts"]) {
                if (CodeArtifactMatchesSearch(artifact, args)) {
                    matches.push_back(CompactCodeArtifact(artifact));
                }
            }
        }
        std::sort(matches.begin(), matches.end(), [](const nlohmann::json& left, const nlohmann::json& right) {
            if (left.value("latest", false) != right.value("latest", false)) {
                return left.value("latest", false) && !right.value("latest", false);
            }
            const std::string left_time = left.value("updated_at", left.value("created_at", ""));
            const std::string right_time = right.value("updated_at", right.value("created_at", ""));
            if (left_time != right_time) return left_time > right_time;
            return left.value("artifact_key", "") < right.value("artifact_key", "");
        });
        if (static_cast<int>(matches.size()) > max_results) {
            matches.resize(static_cast<size_t>(max_results));
        }

        nlohmann::json result_array = nlohmann::json::array();
        for (const auto& match : matches) result_array.push_back(match);
        return MakeJsonToolResult({
            {"success", true},
            {"tool", tool_name},
            {"server", runtime.server_name},
            {"project_id", runtime.project_id},
            {"chat_id", runtime.chat_id},
            {"storage_path", runtime.storage_root_utf8},
            {"count", result_array.size()},
            {"results", std::move(result_array)},
            {"line_hint_warning", "start_line_hint and end_line_hint may shift after edits; verify with content_hash, qualified_symbol, and current source before editing."},
        });
    }

    if (tool_name == kCodeListVersionsToolName) {
        const std::string artifact_key = Trim(args.value("artifact_key", std::string{}));
        if (artifact_key.empty()) {
            return MakeArtifactToolError(runtime, tool_name,
                "code_memory_list_versions requires artifact_key.");
        }
        auto versions = FilterArtifacts(index, artifact_key, {}, false);
        versions.erase(std::remove_if(versions.begin(), versions.end(), [](const nlohmann::json& artifact) {
            return !IsCodeMemoryArtifact(artifact);
        }), versions.end());
        if (versions.empty()) {
            return MakeArtifactToolError(runtime, tool_name,
                "No code memory versions were found for the requested artifact_key.");
        }
        nlohmann::json version_array = nlohmann::json::array();
        for (const auto& version : versions) version_array.push_back(CompactCodeArtifact(version));
        return MakeJsonToolResult({
            {"success", true},
            {"tool", tool_name},
            {"server", runtime.server_name},
            {"project_id", runtime.project_id},
            {"chat_id", runtime.chat_id},
            {"artifact_key", artifact_key},
            {"count", version_array.size()},
            {"versions", std::move(version_array)},
        });
    }

    if (tool_name == kCodeGetVersionToolName || tool_name == kCodeRestoreVersionToolName) {
        const std::string artifact_key = Trim(args.value("artifact_key", std::string{}));
        const int version = JsonIntValue(args, "version");
        if (artifact_key.empty()) {
            return MakeArtifactToolError(runtime, tool_name,
                std::string(tool_name) + " requires artifact_key.");
        }
        if (version <= 0) {
            return MakeArtifactToolError(runtime, tool_name,
                std::string(tool_name) + " requires a positive integer version.");
        }
        const auto artifact = FindArtifactVersion(index, artifact_key, version);
        if (!artifact || !IsCodeMemoryArtifact(*artifact)) {
            return MakeArtifactToolError(runtime, tool_name,
                "No code memory artifact was found for the requested artifact_key and version.");
        }

        auto result = ReadArtifactRecordResult(runtime, tool_name, *artifact);
        if (tool_name == kCodeRestoreVersionToolName && result.success && !result.raw_result_json.empty()) {
            try {
                auto payload = nlohmann::json::parse(result.raw_result_json);
                payload["restore_mode"] = "exact_code_memory_version";
                payload["restore_reason"] = Trim(args.value("restore_reason", std::string{}));
                payload["restore_instruction"] =
                    "Use restored_content exactly as the source of truth. If the user requested source-file restore, write restored_content with an appropriate filesystem or MCP tool after verifying the current target location.";
                payload["line_hint_warning"] =
                    "Line hints may have shifted; verify by source file path, qualified_symbol, and content/context hashes before editing current source.";
                if (payload.contains("content") && payload["content"].is_string()) {
                    payload["restored_content"] = payload["content"];
                } else {
                    payload["restored_content"] = "";
                }
                return MakeJsonToolResult(std::move(payload));
            } catch (...) {
            }
        }
        return result;
    }

    if (tool_name == kGetIndexToolName) {
        const bool latest_only = !args.contains("latest_only") || args["latest_only"].is_null()
            ? true
            : args.value("latest_only", true);
        const auto artifacts = FilterArtifacts(
            index,
            args.value("artifact_key", std::string{}),
            args.value("type", std::string{}),
            latest_only);

        nlohmann::json artifact_array = nlohmann::json::array();
        for (const auto& artifact : artifacts) {
            artifact_array.push_back(artifact);
        }

        return MakeJsonToolResult({
            {"success", true},
            {"tool", tool_name},
            {"server", runtime.server_name},
            {"project_id", runtime.project_id},
            {"chat_id", runtime.chat_id},
            {"storage_path", runtime.storage_root_utf8},
            {"index_exists", index_exists},
            {"count", artifact_array.size()},
            {"artifacts", std::move(artifact_array)},
        });
    }

    if (tool_name == kGetArtifactToolName) {
        const std::string artifact_id = Trim(args.value("artifact_id", std::string{}));
        if (artifact_id.empty()) {
            return MakeArtifactToolError(runtime, tool_name,
                "artifact_memory_get_artifact requires artifact_id.");
        }
        const auto artifact = FindArtifactById(index, artifact_id);
        if (!artifact) {
            return MakeArtifactToolError(runtime, tool_name,
                "Artifact not found in this chat's Artifact Memory index.");
        }
        return ReadArtifactRecordResult(runtime, tool_name, *artifact);
    }

    if (tool_name == kGetLatestToolName) {
        const std::string artifact_key = Trim(args.value("artifact_key", std::string{}));
        if (artifact_key.empty()) {
            return MakeArtifactToolError(runtime, tool_name,
                "artifact_memory_get_latest requires artifact_key.");
        }
        const auto artifact = FindLatestArtifact(index, artifact_key);
        if (!artifact) {
            return MakeArtifactToolError(runtime, tool_name,
                "No artifact versions were found for the requested artifact_key.");
        }
        return ReadArtifactRecordResult(runtime, tool_name, *artifact);
    }

    if (tool_name == kListVersionsToolName) {
        const std::string artifact_key = Trim(args.value("artifact_key", std::string{}));
        if (artifact_key.empty()) {
            return MakeArtifactToolError(runtime, tool_name,
                "artifact_memory_list_versions requires artifact_key.");
        }
        const auto versions = FilterArtifacts(index, artifact_key, {}, false);
        if (versions.empty()) {
            return MakeArtifactToolError(runtime, tool_name,
                "No artifact versions were found for the requested artifact_key.");
        }
        nlohmann::json version_array = nlohmann::json::array();
        for (const auto& version : versions) {
            version_array.push_back(version);
        }
        return MakeJsonToolResult({
            {"success", true},
            {"tool", tool_name},
            {"server", runtime.server_name},
            {"project_id", runtime.project_id},
            {"chat_id", runtime.chat_id},
            {"artifact_key", artifact_key},
            {"count", version_array.size()},
            {"versions", std::move(version_array)},
        });
    }

    if (tool_name == kGetVersionToolName || tool_name == kRestoreVersionToolName) {
        const std::string artifact_key = Trim(args.value("artifact_key", std::string{}));
        const int version = JsonIntValue(args, "version");
        if (artifact_key.empty()) {
            return MakeArtifactToolError(runtime, tool_name,
                std::string(tool_name) + " requires artifact_key.");
        }
        if (version <= 0) {
            return MakeArtifactToolError(runtime, tool_name,
                std::string(tool_name) + " requires a positive integer version.");
        }
        const auto artifact = FindArtifactVersion(index, artifact_key, version);
        if (!artifact) {
            return MakeArtifactToolError(runtime, tool_name,
                "No artifact was found for the requested artifact_key and version.");
        }

        auto result = ReadArtifactRecordResult(runtime, tool_name, *artifact);
        if (tool_name == kRestoreVersionToolName && result.success && !result.raw_result_json.empty()) {
            try {
                auto payload = nlohmann::json::parse(result.raw_result_json);
                payload["restore_mode"] = "exact_artifact_version";
                payload["restore_reason"] = Trim(args.value("restore_reason", std::string{}));
                payload["restore_instruction"] =
                    "Use restored_content exactly as the source of truth. If the user requested a file restore, write restored_content with an appropriate filesystem or MCP tool and mention this Artifact Memory version in the final answer.";
                if (payload.contains("content") && payload["content"].is_string()) {
                    payload["restored_content"] = payload["content"];
                } else {
                    payload["restored_content"] = "";
                }
                return MakeJsonToolResult(std::move(payload));
            } catch (...) {
            }
        }
        return result;
    }

    return MakeArtifactToolError(runtime, tool_name, "Unknown Artifact Memory tool: " + tool_name);
}

}  // namespace artifact_memory_tools
