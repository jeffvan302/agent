#pragma once

#include "context_compression.h"
#include "rag_service.h"
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
};

struct ProjectSettingsResult {
    std::string project_name;
    std::string project_instructions;
    std::vector<ProjectMcpServerBinding> mcp_bindings;
    std::vector<ContextCompressionConfig> compression_configs;
    std::string selected_compression_config_id;
    std::vector<ProjectRagBinding> rag_bindings;
};

std::optional<ProjectSettingsResult> ShowProjectSettingsDialog(HWND owner, const ProjectSettingsOptions& options);
