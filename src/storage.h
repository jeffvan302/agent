#pragma once

#include "types.h"

#include <filesystem>
#include <optional>
#include <vector>

struct RuntimePaths {
    std::filesystem::path startup_root;
    std::filesystem::path config_root;
    std::filesystem::path data_root;
    std::filesystem::path log_root;
};

class AppStorage {
public:
    explicit AppStorage(RuntimePaths runtime_paths);
    // Backward-compatible constructor for code that hasn't migrated to RuntimePaths.
    explicit AppStorage(std::filesystem::path root_path)
        : AppStorage(RuntimePaths{root_path, root_path / ".config", root_path / ".data", root_path / ".log"}) {}

    void EnsureInitialized() const;

    std::vector<ProviderConfig> LoadProviders() const;
    void SaveProviders(const std::vector<ProviderConfig>& providers) const;
    std::vector<BindingModelRuntimeState> LoadBindingProviderRuntimeStates() const;
    void SaveBindingProviderRuntimeStates(const std::vector<BindingModelRuntimeState>& states) const;
    std::vector<ModelConfig> LoadProviderManifestModels(const std::string& provider_type) const;
    std::optional<ProviderAuthRecord> LoadProviderAuthRecord(const std::string& credential_id) const;
    std::optional<ProviderAuthRecord> LoadProviderAuthRecordForProvider(const std::string& provider_id) const;
    void SaveProviderAuthRecord(const ProviderAuthRecord& record) const;
    void DeleteProviderAuthRecord(const std::string& credential_id) const;
    std::filesystem::path ProviderAuthBridgeRoot() const;

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
    std::vector<ChatContextDebugEntry> LoadChatContextDebugEntries(const std::string& project_id, const std::string& chat_id) const;
    void SaveChatContextDebugEntries(const std::string& project_id, const std::string& chat_id, const std::vector<ChatContextDebugEntry>& entries) const;
    void AppendChatContextDebugEntry(const std::string& project_id, const std::string& chat_id, const ChatContextDebugEntry& entry) const;

    std::vector<std::string> LoadApprovedMcpServers(const std::string& project_id) const;
    void SaveApprovedMcpServers(const std::string& project_id, const std::vector<std::string>& server_ids) const;
    std::vector<ProjectMcpServerBinding> LoadProjectMcpBindings(const std::string& project_id) const;
    void SaveProjectMcpBindings(const std::string& project_id, const std::vector<ProjectMcpServerBinding>& bindings) const;

    std::vector<ContextCompressionConfig> LoadCompressionConfigs() const;
    void SaveCompressionConfigs(const std::vector<ContextCompressionConfig>& configs) const;
    ProjectCompressionSettings LoadProjectCompressionSettings(const std::string& project_id) const;
    void SaveProjectCompressionSettings(const std::string& project_id, const ProjectCompressionSettings& settings) const;
    ChatCompressionState LoadChatCompressionState(const std::string& project_id, const std::string& chat_id) const;
    void SaveChatCompressionState(const std::string& project_id, const std::string& chat_id, const ChatCompressionState& state) const;
    std::vector<ChatCompressionSnapshot> LoadChatCompressionHistory(const std::string& project_id, const std::string& chat_id) const;
    void SaveChatCompressionHistory(const std::string& project_id, const std::string& chat_id, const std::vector<ChatCompressionSnapshot>& snapshots) const;
    void AppendChatCompressionSnapshot(const std::string& project_id, const std::string& chat_id, const ChatCompressionSnapshot& snapshot) const;

    std::filesystem::path ProjectSettingsPath(const std::string& project_id) const;
    ProjectSettings LoadProjectSettings(const std::string& project_id) const;
    void SaveProjectSettings(const std::string& project_id, const ProjectSettings& settings) const;

    std::vector<ModelToolConfig> LoadModelTools() const;
    void SaveModelTools(const std::vector<ModelToolConfig>& tools) const;

    std::vector<AgenticModeConfig> LoadAgenticModes() const;
    void SaveAgenticModes(const std::vector<AgenticModeConfig>& modes) const;

    std::vector<RagWorkingSetEntry> LoadChatRagWorkingSet(const std::string& project_id, const std::string& chat_id) const;
    void SaveChatRagWorkingSet(const std::string& project_id, const std::string& chat_id, const std::vector<RagWorkingSetEntry>& entries) const;

    void RenameProject(const std::string& project_id, const std::string& new_name) const;
    void RenameChat(const std::string& project_id, const std::string& chat_id, const std::string& new_name) const;

    void DeleteProject(const std::string& project_id) const;
    void DeleteChat(const std::string& project_id, const std::string& chat_id) const;

    const RuntimePaths& runtime_paths() const { return runtime_paths_; }
    const std::filesystem::path& startup_root() const { return runtime_paths_.startup_root; }
    const std::filesystem::path& config_root() const { return runtime_paths_.config_root; }
    const std::filesystem::path& data_root() const { return runtime_paths_.data_root; }
    const std::filesystem::path& log_root() const { return runtime_paths_.log_root; }
    const std::filesystem::path& root_path() const { return runtime_paths_.startup_root; }

private:
    std::filesystem::path ProvidersPath() const;
    std::filesystem::path BindingProviderRuntimePath() const;
    std::filesystem::path ProviderManifestsRoot() const;
    std::filesystem::path ProviderManifestPath(const std::string& provider_type) const;
    std::filesystem::path ProviderAuthPath() const;
    std::filesystem::path McpServersPath() const;
    std::filesystem::path DataRoot() const;
    std::filesystem::path ConfigProjectsRoot() const;
    std::filesystem::path DataProjectsRoot() const;
    std::filesystem::path ProjectConfigPath(const std::string& project_id) const;
    std::filesystem::path ProjectDataPath(const std::string& project_id) const;
    std::filesystem::path ProjectMetaPath(const std::string& project_id) const;
    std::filesystem::path ProjectMcpConsentPath(const std::string& project_id) const;
    std::filesystem::path ProjectMcpBindingsPath(const std::string& project_id) const;
    std::filesystem::path ChatsRoot(const std::string& project_id) const;
    std::filesystem::path ChatPath(const std::string& project_id, const std::string& chat_id) const;
    std::filesystem::path ChatMetaPath(const std::string& project_id, const std::string& chat_id) const;
    std::filesystem::path ChatMessagesPath(const std::string& project_id, const std::string& chat_id) const;
    std::filesystem::path ChatContextDebugPath(const std::string& project_id, const std::string& chat_id) const;
    std::filesystem::path CompressionConfigsPath() const;
    std::filesystem::path ProjectCompressionPath(const std::string& project_id) const;
    std::filesystem::path ChatCompressionStatePath(const std::string& project_id, const std::string& chat_id) const;
    std::filesystem::path ChatCompressionHistoryPath(const std::string& project_id, const std::string& chat_id) const;
    std::filesystem::path ChatRagWorkingSetPath(const std::string& project_id, const std::string& chat_id) const;
    std::filesystem::path ModelToolsPath() const;
    std::filesystem::path AgenticModesPath() const;

    RuntimePaths runtime_paths_;
};
