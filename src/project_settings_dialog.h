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
    std::vector<AgenticModeConfig> agentic_modes;      // all available agentic modes
    std::string selected_agentic_mode_id;                // currently selected default mode
    std::vector<std::string> enabled_agentic_mode_ids;  // currently enabled mode IDs
    bool enable_chat_logging = false;                    // current chat logging setting
    bool allow_manual_context_compression = false;       // allow manual compression from web UI
    bool enable_web_debugging = false;                   // allow prompt debugging in web UI
    bool serve_web_links_inline = false;                  // serve web file links inline instead of forced downloads
    bool enable_automation = false;                       // Enable automation sequence recording and playback in web UI
    bool built_in_powershell_enabled = false;
    std::string built_in_powershell_working_directory = "$ProjectFolder$";
    bool built_in_artifact_memory_enabled = false;
    bool built_in_planner_enabled = false;
    std::string built_in_planner_storage_folder = "$ProjectFolder$\\.agent";
    bool built_in_completion_driver_enabled = false;
    std::vector<std::string> completion_driver_allowed_mode_ids;
    int completion_driver_max_continuations = 0;      // 0 = unlimited, otherwise max host continuations per run/automation step
    bool built_in_questionnaire_enabled = false;
    int questionnaire_max_options = 8;
    bool questionnaire_restrict_by_mode = false;
    std::string questionnaire_allowed_mode_id;
    bool built_in_filesystem_enabled = false;
    bool built_in_filesystem_auto_archive = false;
    std::string built_in_filesystem_working_directory = "$ProjectFolder$";
    int model_timeout_seconds = 0;                    // 0 = infinite (default), otherwise max seconds per model request
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
    std::string selected_agentic_mode_id;       // selected default agentic mode
    std::vector<std::string> enabled_agentic_mode_ids;  // enabled agentic mode IDs after editing
    bool enable_chat_logging = false;             // whether detailed chat logging is enabled
    bool allow_manual_context_compression = false; // allow manual compression from web UI
    bool enable_web_debugging = false;             // allow prompt debugging in web UI
    bool serve_web_links_inline = false;            // serve web file links inline instead of forced downloads
    bool enable_automation = false;                 // Enable automation sequence recording and playback in web UI
    bool built_in_powershell_enabled = false;
    std::string built_in_powershell_working_directory = "$ProjectFolder$";
    bool built_in_artifact_memory_enabled = false;
    bool built_in_planner_enabled = false;
    std::string built_in_planner_storage_folder = "$ProjectFolder$\\.agent";
    bool built_in_completion_driver_enabled = false;
    std::vector<std::string> completion_driver_allowed_mode_ids;
    int completion_driver_max_continuations = 0;      // 0 = unlimited, otherwise max host continuations per run/automation step
    bool built_in_questionnaire_enabled = false;
    int questionnaire_max_options = 8;
    bool questionnaire_restrict_by_mode = false;
    std::string questionnaire_allowed_mode_id;
    bool built_in_filesystem_enabled = false;
    bool built_in_filesystem_auto_archive = false;
    std::string built_in_filesystem_working_directory = "$ProjectFolder$";
    int model_timeout_seconds = 0;                    // 0 = infinite (default), otherwise max seconds per model request
};

std::optional<ProjectSettingsResult> ShowProjectSettingsDialog(HWND owner, const ProjectSettingsOptions& options);
void ShowRemoteOllamaSetupDialog(HWND owner, AppStorage* storage, std::vector<ProviderConfig>* providers);
