#pragma once

#include "storage.h"
#include "types.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

enum class McpServerStatus {
    Disconnected,
    Connecting,
    Ready,
    Error,
};

struct McpToolDefinition {
    std::string name;
    std::string title;
    std::string description;
    std::string input_schema_json;
};

struct McpToolCallResult {
    bool success = false;
    bool is_tool_error = false;
    std::string content_text;
    std::string raw_result_json;
};

struct McpServerSnapshot {
    McpServerConfig config;
    McpServerStatus status = McpServerStatus::Disconnected;
    std::string last_error;
    std::vector<McpToolDefinition> tools;
    bool selected_for_project = false;
};

struct McpExposedTool {
    std::string alias;
    std::string server_id;
    std::string server_name;
    std::string tool_name;
    std::string title;
    std::string description;
    std::string input_schema_json;
};

struct McpServerTestResult {
    bool success = false;
    std::string summary;
    std::string stdin_text;
    std::string stdout_text;
    std::string stderr_text;
    std::vector<std::string> detected_features;
    std::vector<std::string> detected_tools;
};

class McpManager {
public:
    using StateChangedCallback = std::function<void()>;

    explicit McpManager(AppStorage* storage);
    ~McpManager();

    void Load();
    const std::vector<McpServerConfig>& configs() const;
    const std::vector<McpServerVariable>& global_variables() const;
    void SaveConfigs(const std::vector<McpServerConfig>& configs);
    void SaveGlobalVariables(const std::vector<McpServerVariable>& variables);

    void SetStateChangedCallback(StateChangedCallback callback);

    std::vector<McpServerSnapshot> GetServerSnapshots(const std::string& project_id) const;
    bool ConnectServer(const std::string& server_id,
                       const std::string& project_id,
                       std::string* error,
                       const std::vector<ProjectMcpVariableValue>& runtime_variables = {});
    bool RefreshServerTools(const std::string& server_id, const std::string& project_id, std::string* error);
    void DisconnectServer(const std::string& server_id, const std::string& project_id);
    void ConnectAutoServers(const std::string& project_id);

    std::vector<ProjectMcpServerBinding> GetProjectBindings(const std::string& project_id) const;
    void SaveProjectBindings(const std::string& project_id, const std::vector<ProjectMcpServerBinding>& bindings);
    bool IsServerSelectedForProject(const std::string& project_id, const std::string& server_id) const;

    std::vector<McpExposedTool> GetExposedToolsForProject(
        const std::string& project_id,
        const std::vector<ProjectMcpVariableValue>& runtime_variables = {}) const;
    std::string BuildWebResearchUsageContext(const std::string& project_id) const;
    McpToolCallResult CallExposedTool(
        const std::string& project_id,
        const std::string& alias,
        const std::string& arguments_json,
        const std::vector<ProjectMcpVariableValue>& runtime_variables = {}) const;
    McpServerTestResult TestServerConfig(const McpServerConfig& config) const;

private:
    class Connection;
    struct Impl;

    void NotifyStateChanged() const;
    std::vector<ProjectMcpServerBinding> LoadProjectBindings(const std::string& project_id) const;
    std::optional<ProjectMcpServerBinding> FindProjectBinding(const std::string& project_id, const std::string& server_id) const;
    std::optional<McpServerConfig> ResolveConfigForProject(
        const McpServerConfig& config,
        const std::string& project_id,
        std::string* error,
        const std::vector<ProjectMcpVariableValue>& runtime_variables = {}) const;

    AppStorage* storage_ = nullptr;
    mutable StateChangedCallback state_changed_callback_;
    std::vector<McpServerConfig> configs_;
    std::vector<McpServerVariable> global_variables_;
    std::unique_ptr<Impl> impl_;
};
