#include "storage.h"

#include "util.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>

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
