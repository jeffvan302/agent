#pragma once

#include "mcp_manager.h"
#include "openai_client.h"
#include "util.h"
#include "variable_resolver.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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
constexpr char kServerName[] = "Artifact Memory";

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
           name == kListVersionsToolName;
}

inline std::string FriendlyArtifactToolTitle(const std::string& tool_name) {
    if (tool_name == kGetIndexToolName) return "Get Index";
    if (tool_name == kGetArtifactToolName) return "Get Artifact";
    if (tool_name == kGetLatestToolName) return "Get Latest";
    if (tool_name == kListVersionsToolName) return "List Versions";
    return tool_name;
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

inline std::optional<std::filesystem::path> ResolveArtifactPath(
    const ArtifactMemoryRuntime& runtime,
    const nlohmann::json& artifact,
    std::string* error = nullptr) {
    const std::string file_path_text = artifact.value("file_path", "");
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

inline ArtifactMemoryToolSet BuildArtifactMemoryToolSet(
    const std::optional<ContextCompressionConfig>& selected_config,
    const std::string& project_id,
    const std::string& chat_id,
    const std::vector<ProjectMcpVariableValue>& project_variables = {}) {
    ArtifactMemoryToolSet set;
    if (!selected_config ||
        selected_config->strategy != ContextCompressionStrategy::HierarchicalStructured ||
        !selected_config->layers.layer0.enabled) {
        return set;
    }

    const std::string resolved_folder = Trim(variable_resolver::ExpandTemplate(
        selected_config->layers.layer0.storage_folder_template,
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
    if (ec && !std::filesystem::exists(root)) {
        return set;
    }

    set.runtime.enabled = true;
    set.runtime.project_id = project_id;
    set.runtime.chat_id = chat_id;
    set.runtime.config_id = selected_config->id;
    set.runtime.config_name = selected_config->name;
    set.runtime.storage_root = root;
    set.runtime.storage_root_utf8 = WideToUtf8(root.wstring());
    set.runtime.max_injected_rows = std::max(1, selected_config->layers.layer0.max_injected_rows);

    ChatToolDefinition get_index;
    get_index.name = kGetIndexToolName;
    get_index.description =
        "Read the built-in Artifact Memory index for this chat. "
        "This read-only MCP-style server tracks code and diagram artifacts preserved by Layer 0 compression. "
        "Use this to discover what stable artifacts already exist before asking for revisions.";
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
        "Use this when you need the full code block, diagram, summary, and metadata for a specific prior artifact.";
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
        "Use this when you need history before deciding which version to inspect or revise.";
    list_versions.parameters_json = JsonObjectSchema({
        {"artifact_key", {{"type", "string"}, {"description", "The stable artifact_key to list version history for."}}}
    }, {"artifact_key"}).dump();
    set.definitions.push_back(std::move(list_versions));

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
        std::string path_error;
        const auto artifact_path = ResolveArtifactPath(runtime, *artifact, &path_error);
        if (!artifact_path) {
            return MakeArtifactToolError(runtime, tool_name, path_error);
        }
        std::string read_error;
        const auto markdown = ReadWholeFileUtf8(*artifact_path, &read_error);
        if (!markdown) {
            return MakeArtifactToolError(runtime, tool_name, read_error);
        }
        return MakeJsonToolResult({
            {"success", true},
            {"tool", tool_name},
            {"server", runtime.server_name},
            {"project_id", runtime.project_id},
            {"chat_id", runtime.chat_id},
            {"artifact", *artifact},
            {"artifact_path", WideToUtf8(artifact_path->wstring())},
            {"markdown", *markdown},
        });
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
        std::string path_error;
        const auto artifact_path = ResolveArtifactPath(runtime, *artifact, &path_error);
        if (!artifact_path) {
            return MakeArtifactToolError(runtime, tool_name, path_error);
        }
        std::string read_error;
        const auto markdown = ReadWholeFileUtf8(*artifact_path, &read_error);
        if (!markdown) {
            return MakeArtifactToolError(runtime, tool_name, read_error);
        }
        return MakeJsonToolResult({
            {"success", true},
            {"tool", tool_name},
            {"server", runtime.server_name},
            {"project_id", runtime.project_id},
            {"chat_id", runtime.chat_id},
            {"artifact", *artifact},
            {"artifact_path", WideToUtf8(artifact_path->wstring())},
            {"markdown", *markdown},
        });
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

    return MakeArtifactToolError(runtime, tool_name, "Unknown Artifact Memory tool: " + tool_name);
}

}  // namespace artifact_memory_tools
