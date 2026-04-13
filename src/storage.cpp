#include "storage.h"

#include "rag_service.h"
#include "util.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

using json = nlohmann::json;

namespace {
json ModelToJson(const ModelConfig& model) {
    return json{
        {"id", model.id},
        {"display_name", model.display_name},
        {"context_window", model.context_window},
        {"supports_streaming", model.supports_streaming},
        {"supports_tools", model.supports_tools},
        {"supports_vision", model.supports_vision},
    };
}

ModelConfig ModelFromJson(const json& item) {
    ModelConfig model;
    model.id = item.value("id", "");
    model.display_name = item.value("display_name", model.id);
    model.context_window = item.value("context_window", 0);
    model.supports_streaming = item.value("supports_streaming", true);
    model.supports_tools = item.value("supports_tools", false);
    model.supports_vision = item.value("supports_vision", false);
    return model;
}

json ProviderToJson(const ProviderConfig& provider) {
    json models = json::array();
    for (const auto& model : provider.models) {
        models.push_back(ModelToJson(model));
    }

    return json{
        {"id", provider.id},
        {"name", provider.name},
        {"base_url", provider.base_url},
        {"api_key", provider.api_key},
        {"models", std::move(models)},
    };
}

ProviderConfig ProviderFromJson(const json& item) {
    ProviderConfig provider;
    provider.id = item.value("id", MakeId("provider"));
    provider.name = item.value("name", "Unnamed Provider");
    provider.base_url = item.value("base_url", "");
    provider.api_key = item.value("api_key", "");
    if (item.contains("models") && item["models"].is_array()) {
        for (const auto& model_item : item["models"]) {
            provider.models.push_back(ModelFromJson(model_item));
        }
    }
    return provider;
}

std::string McpVariableKindToString(McpVariableKind kind) {
    switch (kind) {
    case McpVariableKind::Folder:
        return "folder";
    case McpVariableKind::File:
        return "file";
    case McpVariableKind::None:
    default:
        return "none";
    }
}

McpVariableKind McpVariableKindFromString(const std::string& value) {
    if (value == "folder") {
        return McpVariableKind::Folder;
    }
    if (value == "file") {
        return McpVariableKind::File;
    }
    return McpVariableKind::None;
}

std::string McpServerScopeToString(McpServerScope scope) {
    switch (scope) {
    case McpServerScope::Shared:
        return "shared";
    case McpServerScope::PerProject:
    default:
        return "per_project";
    }
}

McpServerScope McpServerScopeFromString(const std::string& value) {
    if (value == "shared") {
        return McpServerScope::Shared;
    }
    return McpServerScope::PerProject;
}

json McpServerVariableToJson(const McpServerVariable& variable) {
    return json{
        {"name", variable.name},
        {"description", variable.description},
        {"kind", McpVariableKindToString(variable.kind)},
        {"inject_into_context", variable.inject_into_context},
    };
}

McpServerVariable McpServerVariableFromJson(const json& item) {
    McpServerVariable variable;
    variable.name = item.value("name", "");
    variable.description = item.value("description", "");
    variable.kind = McpVariableKindFromString(item.value("kind", "none"));
    variable.inject_into_context = item.value("inject_into_context", false);
    return variable;
}

json ProjectMcpVariableValueToJson(const ProjectMcpVariableValue& variable) {
    return json{
        {"name", variable.name},
        {"value", variable.value},
    };
}

ProjectMcpVariableValue ProjectMcpVariableValueFromJson(const json& item) {
    ProjectMcpVariableValue variable;
    variable.name = item.value("name", "");
    variable.value = item.value("value", "");
    return variable;
}

json ProjectMcpServerBindingToJson(const ProjectMcpServerBinding& binding) {
    json variables = json::array();
    for (const auto& variable : binding.variables) {
        variables.push_back(ProjectMcpVariableValueToJson(variable));
    }
    return json{
        {"server_id", binding.server_id},
        {"variables", std::move(variables)},
    };
}

ProjectMcpServerBinding ProjectMcpServerBindingFromJson(const json& item) {
    ProjectMcpServerBinding binding;
    binding.server_id = item.value("server_id", "");
    if (item.contains("variables") && item["variables"].is_array()) {
        for (const auto& variable_item : item["variables"]) {
            binding.variables.push_back(ProjectMcpVariableValueFromJson(variable_item));
        }
    }
    return binding;
}

static std::string RagRetrievalModeToString(RagRetrievalMode mode) {
    switch (mode) {
        case RagRetrievalMode::PassiveOnly:    return "passive_only";
        case RagRetrievalMode::ActiveToolOnly: return "active_tool_only";
        case RagRetrievalMode::Disabled:       return "disabled";
        case RagRetrievalMode::Both:           // fallthrough
        default:                               return "both";
    }
}

static RagRetrievalMode RagRetrievalModeFromString(const std::string& s) {
    if (s == "passive_only")    return RagRetrievalMode::PassiveOnly;
    if (s == "active_tool_only") return RagRetrievalMode::ActiveToolOnly;
    if (s == "disabled")        return RagRetrievalMode::Disabled;
    return RagRetrievalMode::Both;
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
        {"retrieval_mode", RagRetrievalModeToString(binding.retrieval_mode)},
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
    binding.retrieval_mode = RagRetrievalModeFromString(item.value("retrieval_mode", "both"));
    return binding;
}

json McpServerToJson(const McpServerConfig& server) {
    json variables = json::array();
    for (const auto& variable : server.variables) {
        variables.push_back(McpServerVariableToJson(variable));
    }

    return json{
        {"id", server.id},
        {"name", server.name},
        {"command", server.command},
        {"arguments", server.arguments},
        {"working_directory", server.working_directory},
        {"env_entries", server.env_entries},
        {"scope", McpServerScopeToString(server.scope)},
        {"variables", std::move(variables)},
        {"enabled", server.enabled},
        {"auto_connect", server.auto_connect},
    };
}

McpServerConfig McpServerFromJson(const json& item) {
    McpServerConfig server;
    server.id = item.value("id", MakeId("mcp_server"));
    server.name = item.value("name", "Unnamed MCP Server");
    server.command = item.value("command", "");
    server.working_directory = item.value("working_directory", "");
    server.scope = McpServerScopeFromString(item.value("scope", "per_project"));
    server.enabled = item.value("enabled", true);
    server.auto_connect = item.value("auto_connect", false);
    if (item.contains("arguments") && item["arguments"].is_array()) {
        for (const auto& argument : item["arguments"]) {
            if (argument.is_string()) {
                server.arguments.push_back(argument.get<std::string>());
            }
        }
    }
    if (item.contains("env_entries") && item["env_entries"].is_array()) {
        for (const auto& entry : item["env_entries"]) {
            if (entry.is_string()) {
                server.env_entries.push_back(entry.get<std::string>());
            }
        }
    }
    if (item.contains("variables") && item["variables"].is_array()) {
        for (const auto& variable_item : item["variables"]) {
            server.variables.push_back(McpServerVariableFromJson(variable_item));
        }
    }
    return server;
}

json ProjectToJson(const ProjectInfo& project) {
    return json{
        {"id", project.id},
        {"name", project.name},
    };
}

ProjectInfo ProjectFromJson(const json& item, const std::string& fallback_id) {
    ProjectInfo project;
    project.id = item.value("id", fallback_id);
    project.name = item.value("name", fallback_id);
    return project;
}

json ChatToJson(const ChatInfo& chat) {
    return json{
        {"id", chat.id},
        {"name", chat.name},
        {"provider_id", chat.provider_id},
        {"model_id", chat.model_id},
        {"system_prompt", chat.system_prompt},
        {"temperature", chat.temperature},
        {"max_tokens", chat.max_tokens},
    };
}

ChatInfo ChatFromJson(const json& item, const std::string& fallback_id) {
    ChatInfo chat;
    chat.id = item.value("id", fallback_id);
    chat.name = item.value("name", fallback_id);
    chat.provider_id = item.value("provider_id", "");
    chat.model_id = item.value("model_id", "");
    chat.system_prompt = item.value("system_prompt", "");
    chat.temperature = item.value("temperature", 0.2);
    chat.max_tokens = item.value("max_tokens", 1024);
    return chat;
}

json MessageToJson(const MessageRecord& message) {
    json payload{
        {"role", message.role},
        {"content", message.content},
        {"created_at", message.created_at},
    };
    if (!message.name.empty()) {
        payload["name"] = message.name;
    }
    if (!message.tool_call_id.empty()) {
        payload["tool_call_id"] = message.tool_call_id;
    }
    if (!message.tool_calls_json.empty()) {
        try {
            payload["tool_calls"] = json::parse(message.tool_calls_json);
        } catch (...) {
            payload["tool_calls_json"] = message.tool_calls_json;
        }
    }
    return payload;
}

MessageRecord MessageFromJson(const json& item) {
    MessageRecord message;
    message.role = item.value("role", "assistant");
    message.content = item.value("content", "");
    message.created_at = item.value("created_at", "");
    message.name = item.value("name", "");
    message.tool_call_id = item.value("tool_call_id", "");
    if (item.contains("tool_calls")) {
        message.tool_calls_json = item["tool_calls"].dump();
    } else {
        message.tool_calls_json = item.value("tool_calls_json", "");
    }
    return message;
}

json ChatContextDebugEntryToJson(const ChatContextDebugEntry& entry) {
    json request_messages = json::array();
    for (const auto& message : entry.request_messages) {
        request_messages.push_back(MessageToJson(message));
    }

    return json{
        {"id", entry.id},
        {"created_at", entry.created_at},
        {"kind", entry.kind},
        {"user_message_index", entry.user_message_index},
        {"provider_id", entry.provider_id},
        {"model_id", entry.model_id},
        {"system_prompt", entry.system_prompt},
        {"request_messages", std::move(request_messages)},
        {"compressed_context", entry.compressed_context},
        {"mcp_context", entry.mcp_context},
        {"rag_context", entry.rag_context},
        {"rag_working_set_json", entry.rag_working_set_json},
    };
}

ChatContextDebugEntry ChatContextDebugEntryFromJson(const json& item) {
    ChatContextDebugEntry entry;
    entry.id = item.value("id", "");
    entry.created_at = item.value("created_at", "");
    entry.kind = item.value("kind", "request");
    entry.user_message_index = item.value("user_message_index", static_cast<size_t>(0));
    entry.provider_id = item.value("provider_id", "");
    entry.model_id = item.value("model_id", "");
    entry.system_prompt = item.value("system_prompt", "");
    entry.compressed_context = item.value("compressed_context", "");
    entry.mcp_context = item.value("mcp_context", "");
    entry.rag_context = item.value("rag_context", "");
    entry.rag_working_set_json = item.value("rag_working_set_json", "");
    if (item.contains("request_messages") && item["request_messages"].is_array()) {
        for (const auto& message_item : item["request_messages"]) {
            entry.request_messages.push_back(MessageFromJson(message_item));
        }
    }
    return entry;
}

json RagWorkingSetEntryToJson(const RagWorkingSetEntry& entry) {
    return json{
        {"chunk_id", entry.chunk_id},
        {"rag_id", entry.rag_id},
        {"rag_name", entry.rag_name},
        {"document_id", entry.document_id},
        {"document_title", entry.document_title},
        {"text", entry.text},
        {"score", entry.score},
        {"query", entry.query},
        {"retrieved_at", entry.retrieved_at},
    };
}

RagWorkingSetEntry RagWorkingSetEntryFromJson(const json& item) {
    RagWorkingSetEntry entry;
    entry.chunk_id = item.value("chunk_id", "");
    entry.rag_id = item.value("rag_id", "");
    entry.rag_name = item.value("rag_name", "");
    entry.document_id = item.value("document_id", "");
    entry.document_title = item.value("document_title", "");
    entry.text = item.value("text", "");
    entry.score = item.value("score", 0.0);
    entry.query = item.value("query", "");
    entry.retrieved_at = item.value("retrieved_at", "");
    return entry;
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
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        throw std::runtime_error("Unable to open file for writing: " + path.string());
    }
    output << data.dump(2);
}
}  // namespace

AppStorage::AppStorage(std::filesystem::path root_path) : root_path_(std::move(root_path)) {}

void AppStorage::EnsureInitialized() const {
    std::filesystem::create_directories(ProjectsRoot());
    if (!std::filesystem::exists(ProvidersPath())) {
        SaveJsonFile(ProvidersPath(), json{{"providers", json::array()}});
    }
    if (!std::filesystem::exists(McpServersPath())) {
        SaveJsonFile(McpServersPath(), json{{"servers", json::array()}, {"variables", json::array()}});
    }
}

std::vector<ProviderConfig> AppStorage::LoadProviders() const {
    EnsureInitialized();
    const json data = LoadJsonFile(ProvidersPath(), json{{"providers", json::array()}});

    std::vector<ProviderConfig> providers;
    if (data.contains("providers") && data["providers"].is_array()) {
        for (const auto& provider_item : data["providers"]) {
            providers.push_back(ProviderFromJson(provider_item));
        }
    }
    return providers;
}

void AppStorage::SaveProviders(const std::vector<ProviderConfig>& providers) const {
    EnsureInitialized();
    json payload;
    payload["providers"] = json::array();
    for (const auto& provider : providers) {
        payload["providers"].push_back(ProviderToJson(provider));
    }
    SaveJsonFile(ProvidersPath(), payload);
}

std::vector<McpServerConfig> AppStorage::LoadMcpServers() const {
    EnsureInitialized();
    const json data = LoadJsonFile(McpServersPath(), json{{"servers", json::array()}});

    std::vector<McpServerConfig> servers;
    if (data.contains("servers") && data["servers"].is_array()) {
        for (const auto& server_item : data["servers"]) {
            servers.push_back(McpServerFromJson(server_item));
        }
    }
    return servers;
}

void AppStorage::SaveMcpServers(const std::vector<McpServerConfig>& servers) const {
    SaveMcpConfiguration(servers, LoadMcpGlobalVariables());
}

std::vector<McpServerVariable> AppStorage::LoadMcpGlobalVariables() const {
    EnsureInitialized();
    const json data = LoadJsonFile(McpServersPath(), json{{"variables", json::array()}});

    std::vector<McpServerVariable> variables;
    if (data.contains("variables") && data["variables"].is_array()) {
        for (const auto& variable_item : data["variables"]) {
            McpServerVariable variable = McpServerVariableFromJson(variable_item);
            if (!variable.name.empty()) {
                variables.push_back(std::move(variable));
            }
        }
    }
    return variables;
}

void AppStorage::SaveMcpGlobalVariables(const std::vector<McpServerVariable>& variables) const {
    SaveMcpConfiguration(LoadMcpServers(), variables);
}

void AppStorage::SaveMcpConfiguration(const std::vector<McpServerConfig>& servers, const std::vector<McpServerVariable>& variables) const {
    EnsureInitialized();
    json payload;
    payload["servers"] = json::array();
    for (const auto& server : servers) {
        payload["servers"].push_back(McpServerToJson(server));
    }
    payload["variables"] = json::array();
    for (const auto& variable : variables) {
        if (!variable.name.empty()) {
            payload["variables"].push_back(McpServerVariableToJson(variable));
        }
    }
    SaveJsonFile(McpServersPath(), payload);
}

std::vector<ProjectRecord> AppStorage::LoadProjects() const {
    EnsureInitialized();

    std::vector<ProjectRecord> projects;
    for (const auto& entry : std::filesystem::directory_iterator(ProjectsRoot())) {
        if (!entry.is_directory()) {
            continue;
        }

        const std::string project_id = entry.path().filename().string();
        const json project_json = LoadJsonFile(ProjectMetaPath(project_id), json::object());

        ProjectRecord record;
        record.info = ProjectFromJson(project_json, project_id);

        const auto chats_root = ChatsRoot(project_id);
        if (std::filesystem::exists(chats_root)) {
            for (const auto& chat_entry : std::filesystem::directory_iterator(chats_root)) {
                if (!chat_entry.is_directory()) {
                    continue;
                }

                const std::string chat_id = chat_entry.path().filename().string();
                const json chat_json = LoadJsonFile(ChatMetaPath(project_id, chat_id), json::object());
                record.chats.push_back(ChatFromJson(chat_json, chat_id));
            }
        }

        projects.push_back(std::move(record));
    }

    return projects;
}

ProjectInfo AppStorage::CreateProject(const std::string& name) const {
    EnsureInitialized();

    ProjectInfo project;
    project.id = MakeId("project");
    project.name = name;

    std::filesystem::create_directories(ChatsRoot(project.id));
    SaveProject(project);
    return project;
}

ChatInfo AppStorage::CreateChat(const std::string& project_id, const std::string& name, const std::string& provider_id, const std::string& model_id) const {
    EnsureInitialized();

    ChatInfo chat;
    chat.id = MakeId("chat");
    chat.name = name;
    chat.provider_id = provider_id;
    chat.model_id = model_id;
    chat.temperature = 0.2;
    chat.max_tokens = 1024;

    std::filesystem::create_directories(ChatPath(project_id, chat.id));
    SaveChat(project_id, chat);
    SaveMessages(project_id, chat.id, {});
    return chat;
}

void AppStorage::SaveProject(const ProjectInfo& project) const {
    EnsureInitialized();
    std::filesystem::create_directories(ProjectPath(project.id));
    std::filesystem::create_directories(ChatsRoot(project.id));
    SaveJsonFile(ProjectMetaPath(project.id), ProjectToJson(project));
}

void AppStorage::SaveChat(const std::string& project_id, const ChatInfo& chat) const {
    EnsureInitialized();
    std::filesystem::create_directories(ChatPath(project_id, chat.id));
    SaveJsonFile(ChatMetaPath(project_id, chat.id), ChatToJson(chat));
}

std::vector<MessageRecord> AppStorage::LoadMessages(const std::string& project_id, const std::string& chat_id) const {
    const json data = LoadJsonFile(ChatMessagesPath(project_id, chat_id), json::array());
    std::vector<MessageRecord> messages;
    if (!data.is_array()) {
        return messages;
    }
    for (const auto& item : data) {
        messages.push_back(MessageFromJson(item));
    }
    return messages;
}

void AppStorage::SaveMessages(const std::string& project_id, const std::string& chat_id, const std::vector<MessageRecord>& messages) const {
    json payload = json::array();
    for (const auto& message : messages) {
        payload.push_back(MessageToJson(message));
    }
    SaveJsonFile(ChatMessagesPath(project_id, chat_id), payload);
}

std::vector<ChatContextDebugEntry> AppStorage::LoadChatContextDebugEntries(const std::string& project_id, const std::string& chat_id) const {
    const json data = LoadJsonFile(ChatContextDebugPath(project_id, chat_id), json{{"entries", json::array()}});
    std::vector<ChatContextDebugEntry> entries;
    if (data.contains("entries") && data["entries"].is_array()) {
        for (const auto& item : data["entries"]) {
            entries.push_back(ChatContextDebugEntryFromJson(item));
        }
    }
    return entries;
}

void AppStorage::SaveChatContextDebugEntries(const std::string& project_id, const std::string& chat_id, const std::vector<ChatContextDebugEntry>& entries) const {
    json payload;
    payload["entries"] = json::array();
    for (const auto& entry : entries) {
        payload["entries"].push_back(ChatContextDebugEntryToJson(entry));
    }
    std::filesystem::create_directories(ChatPath(project_id, chat_id));
    SaveJsonFile(ChatContextDebugPath(project_id, chat_id), payload);
}

void AppStorage::AppendChatContextDebugEntry(const std::string& project_id, const std::string& chat_id, const ChatContextDebugEntry& entry) const {
    auto entries = LoadChatContextDebugEntries(project_id, chat_id);
    entries.push_back(entry);
    SaveChatContextDebugEntries(project_id, chat_id, entries);
}

std::vector<std::string> AppStorage::LoadApprovedMcpServers(const std::string& project_id) const {
    const json data = LoadJsonFile(ProjectMcpConsentPath(project_id), json{{"approved_server_ids", json::array()}});
    std::vector<std::string> server_ids;
    if (data.contains("approved_server_ids") && data["approved_server_ids"].is_array()) {
        for (const auto& item : data["approved_server_ids"]) {
            if (item.is_string()) {
                server_ids.push_back(item.get<std::string>());
            }
        }
    }
    return server_ids;
}

void AppStorage::SaveApprovedMcpServers(const std::string& project_id, const std::vector<std::string>& server_ids) const {
    json payload;
    payload["approved_server_ids"] = server_ids;
    SaveJsonFile(ProjectMcpConsentPath(project_id), payload);
}

std::vector<ProjectMcpServerBinding> AppStorage::LoadProjectMcpBindings(const std::string& project_id) const {
    const json data = LoadJsonFile(ProjectMcpBindingsPath(project_id), json{{"bindings", json::array()}});
    std::vector<ProjectMcpServerBinding> bindings;

    if (data.contains("bindings") && data["bindings"].is_array()) {
        for (const auto& item : data["bindings"]) {
            ProjectMcpServerBinding binding = ProjectMcpServerBindingFromJson(item);
            if (!binding.server_id.empty()) {
                bindings.push_back(std::move(binding));
            }
        }
    }

    for (const auto& approved_id : LoadApprovedMcpServers(project_id)) {
        const auto it = std::find_if(bindings.begin(), bindings.end(), [&](const ProjectMcpServerBinding& binding) { return binding.server_id == approved_id; });
        if (it == bindings.end()) {
            ProjectMcpServerBinding binding;
            binding.server_id = approved_id;
            bindings.push_back(std::move(binding));
        }
    }

    return bindings;
}

void AppStorage::SaveProjectMcpBindings(const std::string& project_id, const std::vector<ProjectMcpServerBinding>& bindings) const {
    json payload;
    payload["bindings"] = json::array();
    std::vector<std::string> approved_ids;
    for (const auto& binding : bindings) {
        if (binding.server_id.empty()) {
            continue;
        }
        payload["bindings"].push_back(ProjectMcpServerBindingToJson(binding));
        approved_ids.push_back(binding.server_id);
    }
    SaveJsonFile(ProjectMcpBindingsPath(project_id), payload);
    SaveApprovedMcpServers(project_id, approved_ids);
}

void AppStorage::RenameProject(const std::string& project_id, const std::string& new_name) const {
    const json existing = LoadJsonFile(ProjectMetaPath(project_id), json::object());
    ProjectInfo info = ProjectFromJson(existing, project_id);
    info.name = new_name;
    SaveProject(info);
}

void AppStorage::RenameChat(const std::string& project_id, const std::string& chat_id, const std::string& new_name) const {
    const json existing = LoadJsonFile(ChatMetaPath(project_id, chat_id), json::object());
    ChatInfo info = ChatFromJson(existing, chat_id);
    info.name = new_name;
    SaveChat(project_id, info);
}

void AppStorage::DeleteProject(const std::string& project_id) const {
    std::filesystem::remove_all(ProjectPath(project_id));
}

void AppStorage::DeleteChat(const std::string& project_id, const std::string& chat_id) const {
    std::filesystem::remove_all(ChatPath(project_id, chat_id));
}

std::filesystem::path AppStorage::ProvidersPath() const {
    return root_path_ / "providers.json";
}

std::filesystem::path AppStorage::McpServersPath() const {
    return root_path_ / "mcp_servers.json";
}

std::filesystem::path AppStorage::DataRoot() const {
    return root_path_ / "data";
}

std::filesystem::path AppStorage::ProjectsRoot() const {
    return DataRoot() / "projects";
}

std::filesystem::path AppStorage::ProjectPath(const std::string& project_id) const {
    return ProjectsRoot() / project_id;
}

std::filesystem::path AppStorage::ProjectMetaPath(const std::string& project_id) const {
    return ProjectPath(project_id) / "project.json";
}

std::filesystem::path AppStorage::ProjectMcpConsentPath(const std::string& project_id) const {
    return ProjectPath(project_id) / "mcp_consent.json";
}

std::filesystem::path AppStorage::ProjectMcpBindingsPath(const std::string& project_id) const {
    return ProjectPath(project_id) / "project_mcp.json";
}

std::filesystem::path AppStorage::ChatsRoot(const std::string& project_id) const {
    return ProjectPath(project_id) / "chats";
}

std::filesystem::path AppStorage::ChatPath(const std::string& project_id, const std::string& chat_id) const {
    return ChatsRoot(project_id) / chat_id;
}

std::filesystem::path AppStorage::ChatMetaPath(const std::string& project_id, const std::string& chat_id) const {
    return ChatPath(project_id, chat_id) / "chat.json";
}

std::filesystem::path AppStorage::ChatMessagesPath(const std::string& project_id, const std::string& chat_id) const {
    return ChatPath(project_id, chat_id) / "messages.json";
}

std::filesystem::path AppStorage::ChatContextDebugPath(const std::string& project_id, const std::string& chat_id) const {
    return ChatPath(project_id, chat_id) / "context_debug.json";
}

std::filesystem::path AppStorage::CompressionConfigsPath() const {
    return root_path_ / "context_compression_configs.json";
}

std::filesystem::path AppStorage::ProjectCompressionPath(const std::string& project_id) const {
    return ProjectPath(project_id) / "context_compression.json";
}

std::filesystem::path AppStorage::ChatCompressionStatePath(const std::string& project_id, const std::string& chat_id) const {
    return ChatPath(project_id, chat_id) / "compression_state.json";
}

std::filesystem::path AppStorage::ChatCompressionHistoryPath(const std::string& project_id, const std::string& chat_id) const {
    return ChatPath(project_id, chat_id) / "compression_history.json";
}

std::filesystem::path AppStorage::ChatRagWorkingSetPath(const std::string& project_id, const std::string& chat_id) const {
    return ChatPath(project_id, chat_id) / "rag_working_set.json";
}

std::filesystem::path AppStorage::ProjectSettingsPath(const std::string& project_id) const {
    return ProjectPath(project_id) / "project_settings.json";
}

// ===== Compression Config JSON Helpers =====

json Layer1ConfigToJson(const Layer1Config& cfg) {
    return json{
        {"enabled", cfg.enabled},
        {"max_pins", cfg.max_pins},
        {"pin_code_blocks", cfg.pin_code_blocks},
        {"pin_urls", cfg.pin_urls},
        {"pin_numbers", cfg.pin_numbers},
        {"pin_first_message", cfg.pin_first_message},
        {"pin_explicit_instructions", cfg.pin_explicit_instructions},
        {"pin_user_flagged", cfg.pin_user_flagged},
    };
}

Layer1Config Layer1ConfigFromJson(const json& item) {
    Layer1Config cfg;
    cfg.enabled = item.value("enabled", true);
    cfg.max_pins = item.value("max_pins", 10);
    cfg.pin_code_blocks = item.value("pin_code_blocks", true);
    cfg.pin_urls = item.value("pin_urls", true);
    cfg.pin_numbers = item.value("pin_numbers", true);
    cfg.pin_first_message = item.value("pin_first_message", true);
    cfg.pin_explicit_instructions = item.value("pin_explicit_instructions", true);
    cfg.pin_user_flagged = item.value("pin_user_flagged", true);
    return cfg;
}

json Layer2ConfigToJson(const Layer2Config& cfg) {
    return json{
        {"enabled", cfg.enabled},
        {"model_id", cfg.model_id},
        {"model_provider_id", cfg.model_provider_id},
        {"max_tokens", cfg.max_tokens},
        {"trigger_threshold_turns", cfg.trigger_threshold_turns},
    };
}

Layer2Config Layer2ConfigFromJson(const json& item) {
    Layer2Config cfg;
    cfg.enabled = item.value("enabled", true);
    cfg.model_id = item.value("model_id", "");
    cfg.model_provider_id = item.value("model_provider_id", "");
    cfg.max_tokens = item.value("max_tokens", 500);
    cfg.trigger_threshold_turns = item.value("trigger_threshold_turns", 8);
    return cfg;
}

json Layer3ConfigToJson(const Layer3Config& cfg) {
    return json{
        {"enabled", cfg.enabled},
        {"model_id", cfg.model_id},
        {"model_provider_id", cfg.model_provider_id},
        {"max_tokens", cfg.max_tokens},
    };
}

Layer3Config Layer3ConfigFromJson(const json& item) {
    Layer3Config cfg;
    cfg.enabled = item.value("enabled", true);
    cfg.model_id = item.value("model_id", "");
    cfg.model_provider_id = item.value("model_provider_id", "");
    cfg.max_tokens = item.value("max_tokens", 800);
    return cfg;
}

json Layer4ConfigToJson(const Layer4Config& cfg) {
    return json{
        {"enabled", cfg.enabled},
        {"min_recent_turns", cfg.min_recent_turns},
    };
}

Layer4Config Layer4ConfigFromJson(const json& item) {
    Layer4Config cfg;
    cfg.enabled = item.value("enabled", true);
    cfg.min_recent_turns = item.value("min_recent_turns", 2);
    return cfg;
}

std::string StrategyToString(ContextCompressionStrategy strategy) {
    switch (strategy) {
    case ContextCompressionStrategy::TruncateTop:
        return "truncate_top";
    case ContextCompressionStrategy::HierarchicalStructured:
        return "hierarchical_structured";
    case ContextCompressionStrategy::None:
    default:
        return "none";
    }
}

ContextCompressionStrategy StrategyFromString(const std::string& value) {
    if (value == "truncate_top") {
        return ContextCompressionStrategy::TruncateTop;
    }
    if (value == "hierarchical_structured") {
        return ContextCompressionStrategy::HierarchicalStructured;
    }
    return ContextCompressionStrategy::None;
}

json ContextCompressionLayerSettingsToJson(const ContextCompressionLayerSettings& layers) {
    return json{
        {"layer1", Layer1ConfigToJson(layers.layer1)},
        {"layer2", Layer2ConfigToJson(layers.layer2)},
        {"layer3", Layer3ConfigToJson(layers.layer3)},
        {"layer4", Layer4ConfigToJson(layers.layer4)},
    };
}

ContextCompressionLayerSettings ContextCompressionLayerSettingsFromJson(const json& item) {
    ContextCompressionLayerSettings layers;
    if (item.contains("layer1")) {
        layers.layer1 = Layer1ConfigFromJson(item["layer1"]);
    }
    if (item.contains("layer2")) {
        layers.layer2 = Layer2ConfigFromJson(item["layer2"]);
    }
    if (item.contains("layer3")) {
        layers.layer3 = Layer3ConfigFromJson(item["layer3"]);
    }
    if (item.contains("layer4")) {
        layers.layer4 = Layer4ConfigFromJson(item["layer4"]);
    }
    return layers;
}

json ContextCompressionConfigToJson(const ContextCompressionConfig& config) {
    return json{
        {"id", config.id},
        {"name", config.name},
        {"strategy", StrategyToString(config.strategy)},
        {"layers", ContextCompressionLayerSettingsToJson(config.layers)},
        {"frequency_every_n_prompts", config.frequency_every_n_prompts},
        {"context_window_trigger_percent", config.context_window_trigger_percent},
        {"truncate_top_keep_messages", config.truncate_top_keep_messages},
    };
}

ContextCompressionConfig ContextCompressionConfigFromJson(const json& item) {
    ContextCompressionConfig config;
    config.id = item.value("id", "");
    config.name = item.value("name", "Unnamed Config");
    config.strategy = StrategyFromString(item.value("strategy", "none"));
    if (item.contains("layers")) {
        config.layers = ContextCompressionLayerSettingsFromJson(item["layers"]);
    }
    config.frequency_every_n_prompts = item.value("frequency_every_n_prompts", 10);
    config.context_window_trigger_percent = item.value("context_window_trigger_percent", 70);
    config.truncate_top_keep_messages = item.value("truncate_top_keep_messages", 20);
    return config;
}

json ChatCompressionStateToJson(const ChatCompressionState& state) {
    json pinned = json::array();
    for (const auto& msg : state.layer1_pinned_messages) {
        pinned.push_back(MessageToJson(msg));
    }
    return json{
        {"last_compression_message_index", state.last_compression_message_index},
        {"latest_snapshot_id", state.latest_snapshot_id},
        {"current_compressed_context", state.current_compressed_context},
        {"layer2_previous_summary", state.layer2_previous_summary},
        {"layer3_previous_state_json", state.layer3_previous_state_json},
        {"layer1_pinned_messages", std::move(pinned)},
    };
}

ChatCompressionState ChatCompressionStateFromJson(const json& item) {
    ChatCompressionState state;
    state.last_compression_message_index = item.value("last_compression_message_index", static_cast<size_t>(0));
    state.latest_snapshot_id = item.value("latest_snapshot_id", "");
    state.current_compressed_context = item.value("current_compressed_context", "");
    state.layer2_previous_summary = item.value("layer2_previous_summary", "");
    state.layer3_previous_state_json = item.value("layer3_previous_state_json", "");
    if (item.contains("layer1_pinned_messages") && item["layer1_pinned_messages"].is_array()) {
        for (const auto& msg_item : item["layer1_pinned_messages"]) {
            state.layer1_pinned_messages.push_back(MessageFromJson(msg_item));
        }
    }
    return state;
}

json ChatCompressionSnapshotToJson(const ChatCompressionSnapshot& snapshot) {
    json pinned = json::array();
    for (const auto& message : snapshot.pinned_messages) {
        pinned.push_back(MessageToJson(message));
    }

    json source_messages = json::array();
    for (const auto& message : snapshot.source_messages) {
        source_messages.push_back(MessageToJson(message));
    }

    return json{
        {"id", snapshot.id},
        {"created_at", snapshot.created_at},
        {"trigger_reason", snapshot.trigger_reason},
        {"config_id", snapshot.config_id},
        {"config_name", snapshot.config_name},
        {"strategy", snapshot.strategy},
        {"previous_snapshot_id", snapshot.previous_snapshot_id},
        {"previous_message_index", snapshot.previous_message_index},
        {"compressed_through_message_index", snapshot.compressed_through_message_index},
        {"previous_compressed_context", snapshot.previous_compressed_context},
        {"compressed_context", snapshot.compressed_context},
        {"layer2_summary", snapshot.layer2_summary},
        {"layer3_state_json", snapshot.layer3_state_json},
        {"pinned_messages", std::move(pinned)},
        {"source_messages", std::move(source_messages)},
    };
}

ChatCompressionSnapshot ChatCompressionSnapshotFromJson(const json& item) {
    ChatCompressionSnapshot snapshot;
    snapshot.id = item.value("id", "");
    snapshot.created_at = item.value("created_at", "");
    snapshot.trigger_reason = item.value("trigger_reason", "");
    snapshot.config_id = item.value("config_id", "");
    snapshot.config_name = item.value("config_name", "");
    snapshot.strategy = item.value("strategy", "");
    snapshot.previous_snapshot_id = item.value("previous_snapshot_id", "");
    snapshot.previous_message_index = item.value("previous_message_index", static_cast<size_t>(0));
    snapshot.compressed_through_message_index = item.value("compressed_through_message_index", static_cast<size_t>(0));
    snapshot.previous_compressed_context = item.value("previous_compressed_context", "");
    snapshot.compressed_context = item.value("compressed_context", "");
    snapshot.layer2_summary = item.value("layer2_summary", "");
    snapshot.layer3_state_json = item.value("layer3_state_json", "");
    if (item.contains("pinned_messages") && item["pinned_messages"].is_array()) {
        for (const auto& message : item["pinned_messages"]) {
            snapshot.pinned_messages.push_back(MessageFromJson(message));
        }
    }
    if (item.contains("source_messages") && item["source_messages"].is_array()) {
        for (const auto& message : item["source_messages"]) {
            snapshot.source_messages.push_back(MessageFromJson(message));
        }
    }
    return snapshot;
}

json ProjectCompressionSettingsToJson(const ProjectCompressionSettings& settings) {
    return json{
        {"enabled", settings.enabled},
        {"config_id", settings.config_id},
    };
}

ProjectCompressionSettings ProjectCompressionSettingsFromJson(const json& item) {
    ProjectCompressionSettings settings;
    settings.enabled = item.value("enabled", false);
    settings.config_id = item.value("config_id", "");
    return settings;
}

// ===== Project Settings JSON Helpers =====

json ProjectSettingsToJson(const ProjectSettings& settings) {
    json j;
    j["project_name"] = settings.project_name;
    j["project_instructions"] = settings.project_instructions;

    json mcp_arr = json::array();
    for (const auto& binding : settings.mcp_bindings) {
        mcp_arr.push_back(ProjectMcpServerBindingToJson(binding));
    }
    j["mcp_bindings"] = mcp_arr;

    json compression_arr = json::array();
    for (const auto& cfg : settings.compression_configs) {
        compression_arr.push_back(ContextCompressionConfigToJson(cfg));
    }
    j["compression_configs"] = compression_arr;

    j["selected_compression_config_id"] = settings.selected_compression_config_id;

    json rag_arr = json::array();
    for (const auto& binding : settings.rag_bindings) {
        rag_arr.push_back(ProjectRagBindingToJson(binding));
    }
    j["rag_bindings"] = rag_arr;

    return j;
}

ProjectSettings ProjectSettingsFromJson(const json& j) {
    ProjectSettings settings;
    settings.project_name = j.value("project_name", "");
    settings.project_instructions = j.value("project_instructions", "");
    settings.selected_compression_config_id = j.value("selected_compression_config_id", "");

    if (j.contains("mcp_bindings") && j["mcp_bindings"].is_array()) {
        for (const auto& item : j["mcp_bindings"]) {
            settings.mcp_bindings.push_back(ProjectMcpServerBindingFromJson(item));
        }
    }

    if (j.contains("compression_configs") && j["compression_configs"].is_array()) {
        for (const auto& item : j["compression_configs"]) {
            settings.compression_configs.push_back(ContextCompressionConfigFromJson(item));
        }
    }

    if (j.contains("rag_bindings") && j["rag_bindings"].is_array()) {
        for (const auto& item : j["rag_bindings"]) {
            settings.rag_bindings.push_back(ProjectRagBindingFromJson(item));
        }
    }

    return settings;
}

ProjectSettings AppStorage::LoadProjectSettings(const std::string& project_id) const {
    auto settings_path = ProjectSettingsPath(project_id);
    if (std::filesystem::exists(settings_path)) {
        const json data = LoadJsonFile(settings_path, json::object());
        ProjectSettings settings = ProjectSettingsFromJson(data);
        if (settings.selected_compression_config_id.empty()) {
            auto comp_path = ProjectCompressionPath(project_id);
            if (std::filesystem::exists(comp_path)) {
                const json comp_data = LoadJsonFile(comp_path, json::object());
                ProjectCompressionSettings legacy_compression = ProjectCompressionSettingsFromJson(comp_data);
                if (legacy_compression.enabled && !legacy_compression.config_id.empty()) {
                    settings.selected_compression_config_id = legacy_compression.config_id;
                }
            }
        }
        return settings;
    }

    // Migration: load from legacy files
    ProjectSettings settings;
    settings.mcp_bindings = LoadProjectMcpBindings(project_id);

    // Load compression config id from context_compression.json
    auto comp_path = ProjectCompressionPath(project_id);
    if (std::filesystem::exists(comp_path)) {
        const json comp_data = LoadJsonFile(comp_path, json::object());
        settings.selected_compression_config_id = comp_data.value("config_id", "");
    }

    // Load rag bindings from project_rag.json
    auto rag_path = ProjectPath(project_id) / "project_rag.json";
    if (std::filesystem::exists(rag_path)) {
        const json rag_data = LoadJsonFile(rag_path, json::object());
        if (rag_data.contains("bindings") && rag_data["bindings"].is_array()) {
            for (const auto& item : rag_data["bindings"]) {
                settings.rag_bindings.push_back(ProjectRagBindingFromJson(item));
            }
        }
    }

    // Save unified file after migration
    std::filesystem::create_directories(ProjectPath(project_id));
    SaveJsonFile(settings_path, ProjectSettingsToJson(settings));

    return settings;
}

void AppStorage::SaveProjectSettings(const std::string& project_id, const ProjectSettings& settings) const {
    std::filesystem::create_directories(ProjectPath(project_id));
    SaveJsonFile(ProjectSettingsPath(project_id), ProjectSettingsToJson(settings));
}

// ===== Compression Storage Methods =====

std::vector<ContextCompressionConfig> AppStorage::LoadCompressionConfigs() const {
    const json data = LoadJsonFile(CompressionConfigsPath(), json{{"configs", json::array()}});
    std::vector<ContextCompressionConfig> configs;
    std::unordered_set<std::string> used_ids;
    bool repaired = false;
    if (data.contains("configs") && data["configs"].is_array()) {
        for (const auto& item : data["configs"]) {
            ContextCompressionConfig config = ContextCompressionConfigFromJson(item);
            if (config.id.empty() || used_ids.find(config.id) != used_ids.end()) {
                do {
                    config.id = MakeId("cc");
                } while (used_ids.find(config.id) != used_ids.end());
                repaired = true;
            }
            used_ids.insert(config.id);
            configs.push_back(std::move(config));
        }
    }
    if (repaired) {
        SaveCompressionConfigs(configs);
    }
    return configs;
}

void AppStorage::SaveCompressionConfigs(const std::vector<ContextCompressionConfig>& configs) const {
    json payload;
    payload["configs"] = json::array();
    for (const auto& config : configs) {
        payload["configs"].push_back(ContextCompressionConfigToJson(config));
    }
    SaveJsonFile(CompressionConfigsPath(), payload);
}

ProjectCompressionSettings AppStorage::LoadProjectCompressionSettings(const std::string& project_id) const {
    const json data = LoadJsonFile(ProjectCompressionPath(project_id), json::object());
    return ProjectCompressionSettingsFromJson(data);
}

void AppStorage::SaveProjectCompressionSettings(const std::string& project_id, const ProjectCompressionSettings& settings) const {
    std::filesystem::create_directories(ProjectPath(project_id));
    SaveJsonFile(ProjectCompressionPath(project_id), ProjectCompressionSettingsToJson(settings));
}

ChatCompressionState AppStorage::LoadChatCompressionState(const std::string& project_id, const std::string& chat_id) const {
    const json data = LoadJsonFile(ChatCompressionStatePath(project_id, chat_id), json::object());
    return ChatCompressionStateFromJson(data);
}

void AppStorage::SaveChatCompressionState(const std::string& project_id, const std::string& chat_id, const ChatCompressionState& state) const {
    std::filesystem::create_directories(ChatPath(project_id, chat_id));
    SaveJsonFile(ChatCompressionStatePath(project_id, chat_id), ChatCompressionStateToJson(state));
}

std::vector<ChatCompressionSnapshot> AppStorage::LoadChatCompressionHistory(const std::string& project_id, const std::string& chat_id) const {
    const json data = LoadJsonFile(ChatCompressionHistoryPath(project_id, chat_id), json{{"snapshots", json::array()}});
    std::vector<ChatCompressionSnapshot> snapshots;
    if (data.contains("snapshots") && data["snapshots"].is_array()) {
        for (const auto& item : data["snapshots"]) {
            snapshots.push_back(ChatCompressionSnapshotFromJson(item));
        }
    }
    return snapshots;
}

void AppStorage::SaveChatCompressionHistory(const std::string& project_id, const std::string& chat_id, const std::vector<ChatCompressionSnapshot>& snapshots) const {
    json data;
    data["snapshots"] = json::array();
    for (const auto& snapshot : snapshots) {
        data["snapshots"].push_back(ChatCompressionSnapshotToJson(snapshot));
    }
    std::filesystem::create_directories(ChatPath(project_id, chat_id));
    SaveJsonFile(ChatCompressionHistoryPath(project_id, chat_id), data);
}

void AppStorage::AppendChatCompressionSnapshot(const std::string& project_id, const std::string& chat_id, const ChatCompressionSnapshot& snapshot) const {
    auto snapshots = LoadChatCompressionHistory(project_id, chat_id);
    snapshots.push_back(snapshot);
    SaveChatCompressionHistory(project_id, chat_id, snapshots);
}

std::vector<RagWorkingSetEntry> AppStorage::LoadChatRagWorkingSet(const std::string& project_id, const std::string& chat_id) const {
    const json data = LoadJsonFile(ChatRagWorkingSetPath(project_id, chat_id), json{{"entries", json::array()}});
    std::vector<RagWorkingSetEntry> entries;
    if (data.contains("entries") && data["entries"].is_array()) {
        for (const auto& item : data["entries"]) {
            entries.push_back(RagWorkingSetEntryFromJson(item));
        }
    }
    return entries;
}

void AppStorage::SaveChatRagWorkingSet(const std::string& project_id, const std::string& chat_id, const std::vector<RagWorkingSetEntry>& entries) const {
    json payload;
    payload["entries"] = json::array();
    for (const auto& entry : entries) {
        payload["entries"].push_back(RagWorkingSetEntryToJson(entry));
    }
    std::filesystem::create_directories(ChatPath(project_id, chat_id));
    SaveJsonFile(ChatRagWorkingSetPath(project_id, chat_id), payload);
}
