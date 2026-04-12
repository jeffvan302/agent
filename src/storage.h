#pragma once

#include "types.h"

#include <filesystem>
#include <vector>

class AppStorage {
public:
    explicit AppStorage(std::filesystem::path root_path);

    void EnsureInitialized() const;

    std::vector<ProviderConfig> LoadProviders() const;
    void SaveProviders(const std::vector<ProviderConfig>& providers) const;

    std::vector<McpServerConfig> LoadMcpServers() const;
    void SaveMcpServers(const std::vector<McpServerConfig>& servers) const;
    std::vector<McpServerVariable> LoadMcpGlobalVariables() const;
    void SaveMcpGlobalVariables(const std::vector<McpServerVariable>& variables) const;
    void SaveMcpConfiguration(const std::vector<McpServerConfig>& servers, const std::vector<McpServerVariable>& variables) const;

    std::vector<ProjectRecord> LoadProjects() const;
    ProjectInfo CreateProject(const std::string& name) const;
    ChatInfo CreateChat(const std::string& project_id, const std::string& name, const std::string& provider_id, const std::string& model_id) const;

    void SaveProject(const ProjectInfo& project) const;
    void SaveChat(const std::string& project_id, const ChatInfo& chat) const;

    std::vector<MessageRecord> LoadMessages(const std::string& project_id, const std::string& chat_id) const;
    void SaveMessages(const std::string& project_id, const std::string& chat_id, const std::vector<MessageRecord>& messages) const;

    std::vector<std::string> LoadApprovedMcpServers(const std::string& project_id) const;
    void SaveApprovedMcpServers(const std::string& project_id, const std::vector<std::string>& server_ids) const;
    std::vector<ProjectMcpServerBinding> LoadProjectMcpBindings(const std::string& project_id) const;
    void SaveProjectMcpBindings(const std::string& project_id, const std::vector<ProjectMcpServerBinding>& bindings) const;

    void RenameProject(const std::string& project_id, const std::string& new_name) const;
    void RenameChat(const std::string& project_id, const std::string& chat_id, const std::string& new_name) const;

    void DeleteProject(const std::string& project_id) const;
    void DeleteChat(const std::string& project_id, const std::string& chat_id) const;

    const std::filesystem::path& root_path() const { return root_path_; }

private:
    std::filesystem::path ProvidersPath() const;
    std::filesystem::path McpServersPath() const;
    std::filesystem::path DataRoot() const;
    std::filesystem::path ProjectsRoot() const;
    std::filesystem::path ProjectPath(const std::string& project_id) const;
    std::filesystem::path ProjectMetaPath(const std::string& project_id) const;
    std::filesystem::path ProjectMcpConsentPath(const std::string& project_id) const;
    std::filesystem::path ProjectMcpBindingsPath(const std::string& project_id) const;
    std::filesystem::path ChatsRoot(const std::string& project_id) const;
    std::filesystem::path ChatPath(const std::string& project_id, const std::string& chat_id) const;
    std::filesystem::path ChatMetaPath(const std::string& project_id, const std::string& chat_id) const;
    std::filesystem::path ChatMessagesPath(const std::string& project_id, const std::string& chat_id) const;

    std::filesystem::path root_path_;
};
