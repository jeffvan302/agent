#pragma once

#include "context_compression.h"
#include "rag_service.h"
#include "storage.h"
#include "types.h"

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

struct ProjectSettingsOptions {
    std::wstring title = L"Project Settings";
    std::wstring accept_label = L"Save";
    std::wstring project_name;
    std::vector<McpServerConfig> servers;
    std::vector<McpServerVariable> global_variables;
    std::vector<ProjectMcpServerBinding> initial_mcp_bindings;
    std::vector<ContextCompressionConfig> compression_configs;
    std::string selected_compression_config_id;
    std::vector<RagLibraryConfig> available_rags;
    std::vector<ProjectRagBinding> initial_rag_bindings;
    std::string project_instructions;
    std::vector<ProviderConfig> providers;      // all configured providers+models
    std::string preferred_provider_id;          // currently selected project model
    std::string preferred_model_id;
    std::vector<ModelToolConfig> model_tools;          // all configured model tools
    std::vector<std::string> initial_model_tool_ids;   // currently enabled tool IDs for this project
    std::vector<ProjectMcpVariableValue> initial_project_variables;  // existing project-level variables
};

struct ProjectSettingsResult {
    std::string project_name;
    std::string project_instructions;
    std::vector<ProjectMcpServerBinding> mcp_bindings;
    std::vector<ContextCompressionConfig> compression_configs;
    std::string selected_compression_config_id;
    std::vector<ProjectRagBinding> rag_bindings;
    std::string preferred_provider_id;
    std::string preferred_model_id;
    std::vector<std::string> model_tool_ids;   // enabled tool IDs after editing
    std::vector<ProjectMcpVariableValue> project_variables;  // project-level variables after editing
};

std::optional<ProjectSettingsResult> ShowProjectSettingsDialog(HWND owner, const ProjectSettingsOptions& options);
void ShowRemoteOllamaSetupDialog(HWND owner, AppStorage* storage, std::vector<ProviderConfig>* providers);
