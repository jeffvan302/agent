#pragma once

#include "types.h"

#include <optional>
#include <string>
#include <vector>
#include <windows.h>

struct ProjectSetupDialogOptions {
    std::wstring title;
    std::wstring accept_label = L"Save";
    std::wstring project_name;
    bool include_project_name = true;
    std::vector<McpServerConfig> servers;
    std::vector<McpServerVariable> global_variables;
    std::vector<ProjectMcpServerBinding> initial_bindings;
};

struct ProjectSetupDialogResult {
    std::string project_name;
    std::vector<ProjectMcpServerBinding> bindings;
};

std::optional<ProjectSetupDialogResult> ShowProjectSetupDialog(HWND owner, const ProjectSetupDialogOptions& options);
