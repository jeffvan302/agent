# Configuration Reference for AI Assistance

This chapter is a locator for administrators or AI assistants asked to implement, diagnose or reproduce a configuration. It maps user-visible names to the implemented storage and behavior.

## UI Surface Locator

| Request language | Open this desktop surface | Important controls |
| --- | --- | --- |
| Add/use ChatGPT web login or Codex model | `Providers` | Provider type `OpenAI OAuth (ChatGPT/Codex)`, `Auth mode`, `Sign In...`, `Auth status`, model capability flags |
| Add Ollama, LM Studio or API provider | `Providers` | Provider type, URL/key, models and `Test Connection` |
| Failover or model evaluation routing | `Providers` | `Binding Provider`, binding model targets, routing mode |
| Add external tools | `MCP Servers` | Add server process, variables, test, project assignment |
| Set a project's model/tool behavior | `Project Settings` | `AI Model`, tool/RAG selections, variables, instructions, mode, diagnostics options |
| Allow web users to choose models | `Project Settings` | `User Select Model`, `User Selectable Models (stream + tools)` |
| Add a sub-agent/model tool | `Model Tools` | Description, `AI Model`, `Agentic Mode`, MCP/built-in/RAG access and private instructions |
| Edit "genetic mode" behavior | `Agentic Modes` | Implemented term is `Agentic Modes`; edit name/instructions |
| Reduce chat token context | `Context Window` and `Project Settings` | Strategy definitions; project assignment and threshold checkbox |
| Configure web networking/styles | `Web Config` | port/bind/base URL/web root/theme/TLS |
| Configure web login/access | `Admin Config` | Users, Groups, Bindings |
| Allow server folder selection in new web chat | `Admin Config` and `Project Settings` | User `Allow browsing folders` plus project privileged browse checkbox |
| Configure searchable documents | `RAG Service` | Libraries, attachments, ingestion, import/export, image settings |
| Configure remote worker | `Remote Model Config` | Worker JSON, exported models, HTTPS secret/certificate |

## Stored Configuration Files

Paths below are relative to the application startup root.

| File | Contains |
| --- | --- |
| `.config/providers.json` | Provider definitions and configured model properties. |
| `.config/provider_auth.json` | Protected imported provider OAuth credential records. |
| `.config/provider_auth_bridge/<provider_id>/codex_home/` | Isolated Codex login bridge cache/log data. |
| `.config/mcp_servers.json` | MCP server commands, environment entries, variable definitions and global variables. |
| `.config/context_compression_configs.json` | Named compression configurations and layer settings. |
| `.config/model_tools.json` | Model-backed tool definitions and their scoped permissions. |
| `.config/agentic_modes.json` | Named instruction modes. |
| `.config/web_settings.json` | Web networking, theme, upload limit and TLS configuration. |
| `.config/users.json` | Web user records, groups and project/group bindings. |
| `.config/projects/<project_id>/project.json` | Project identity/name metadata. |
| `.config/projects/<project_id>/project_settings.json` | Unified per-project behavior. |
| `.config/projects/<project_id>/project_mcp.json` | Project MCP binding compatibility/runtime data. |
| `.config/projects/<project_id>/mcp_consent.json` | Approved project MCP server IDs. |
| `.config/projects/<project_id>/project_rag.json` | Project RAG bindings. |
| `.config/projects/<project_id>/context_compression.json` | Selected project compression compatibility assignment. |
| `.config/rag/rag_image_ingest_settings.json` | Global RAG image extraction settings. |
| `.data/projects/<project_id>/chats/<chat_id>/chat.json` | Per-chat metadata, chosen model/mode and user-defined variables. |
| `.data/projects/<project_id>/chats/<chat_id>/messages.json` | Active stored message history. |
| `.data/projects/<project_id>/chats/<chat_id>/context_debug.json` | Detailed context snapshots when diagnostics/logging is enabled. |
| `.data/projects/<project_id>/chats/<chat_id>/compression_state.json` | Current compressed state. |
| `.data/projects/<project_id>/chats/<chat_id>/compression_history.json` | Compression snapshots/history. |
| `.data/projects/<project_id>/chats/<chat_id>/rag_working_set.json` | Retrieved RAG chunks retained for the chat working set. |

## Key Project Settings JSON Fields

The following visible project features correspond to fields in `project_settings.json`:

| Visible setting | JSON field |
| --- | --- |
| Project name | `project_name` |
| Web Description | `project_description` |
| Project Instructions | `project_instructions` |
| AI Model | `preferred_provider_id`, `preferred_model_id` |
| User Select Model | `user_select_model_enabled`, `user_selectable_models` |
| Context Window Compression | `selected_compression_config_id` |
| Force compression at input tokens | `force_context_compression_token_threshold`, `context_compression_token_threshold` |
| Allow manual compression | `allow_manual_context_compression` |
| Enabled model tools | `model_tool_ids` |
| Project variables | `project_variables` |
| Default/Enabled Agentic Modes | `selected_agentic_mode_id`, `enabled_agentic_mode_ids` |
| Detailed chat logging | `enable_chat_logging` |
| Web Debugging | `enable_web_debugging` |
| Inline `/data` and `/rag` links | `serve_web_links_inline` |
| Automation recording | `enable_automation` |
| Privileged new-chat folder browsing | `allow_privileged_user_project_folder_browse` |
| PowerShell tool and folder | `built_in_powershell_enabled`, `built_in_powershell_working_directory` |
| Artifact/code memory | `built_in_artifact_memory_enabled` |
| Planner and storage | `built_in_planner_enabled`, `built_in_planner_storage_folder` |
| Completion Driver | `built_in_completion_driver_enabled`, `completion_driver_allowed_mode_ids`, `completion_driver_max_continuations`, `completion_driver_overload_delay_seconds` |
| Questionnaire | `built_in_questionnaire_enabled`, `questionnaire_max_options`, `questionnaire_restrict_by_mode`, `questionnaire_allowed_mode_id` |
| Project Filesystem | `built_in_filesystem_enabled`, `built_in_filesystem_auto_archive`, `built_in_filesystem_working_directory` |
| Sleep/Pause | `built_in_sleep_enabled`, `built_in_sleep_max_seconds` |
| Model timeout | `model_timeout_seconds` |

## Variables and Resolution

| Feature | Rules |
| --- | --- |
| Reference syntax | Configured text may refer to a variable as `$Name$` or `$<Name>$`. |
| `ProjectFolder` | Special project-folder path used by tools; web override requires privileged browse permissions rather than ordinary `Allow user definition`. |
| User-defined variables | Project variables checked as `Allow user definition` appear in the web new-chat window as text input. |
| Model tools | Receive resolved parent project/runtime variables automatically; they do not independently discover these values. |
| Chat name folders | Templates using `$CHATNAME$` or equivalent runtime chat-name resolution update preview as the name changes; folder creation occurs only on `Create Chat`. |

## Capability and Permission Rules

| Behavior required | Must be configured |
| --- | --- |
| A project can use a model | Provider has the model and project selects it as `AI Model`, or web user selects an allowed alternative. |
| Web user can change model | `User Select Model` enabled and model checked in list; model must support streaming and tools. |
| Model can use an MCP tool | MCP server defined, enabled/selected for project, connected or auto-connected, with required variables supplied. |
| Model can use a built-in tool | Built-in enabled in project; for a model tool it must also be checked in that model tool. |
| Model can actively search RAG | Project RAG binding has `Enabled`, `Read`, `Tool`; passive injection is currently inactive. |
| Model can write RAG document to drive | RAG binding has `Read`, `Tool`, `Write file`, and destination folder configured. |
| Model can ingest generated RAG material | RAG binding has appropriate `Write`/tool permission and optionally is `Default ingest`. |
| User can browse server folders at new-chat time | User has `Allow browsing folders` and project has privileged browse enabled. |

## Current Limitations to Preserve in Troubleshooting

| UI/config item | Current implementation condition |
| --- | --- |
| TLS `PFX / PKCS#12 bundle` | UI accepts configuration but web server runtime reports `pfx` as unsupported. Use self-signed or PEM. |
| RAG passive retrieval controls | Present, but passive/pre-searched injection is inactive; use active RAG tools. |
| Model tool context compression | Intentionally not used; model tools get invocation/variable/tool context instead. |
| Exact usage tokens in desktop chat dashboard | Provider-reported exact usage is not persisted; dashboard displays estimates. |

## Configuration Request Template for an AI Assistant

When asking an AI assistant to configure or troubleshoot this app, provide:

```text
Application: Agent Desktop
Goal: <what users/models should be able to do>
Active project name/id: <value>
UI surface involved: <Providers / Project Settings / RAG Service / ...>
Provider and model chosen: <display names and capability flags>
MCP servers needed: <names and required paths/variables>
Built-in tools needed: <PowerShell / Filesystem / Planner / Completion Driver / ...>
Agentic mode needed: <name or none>
Context strategy needed: <name, strategy, pre-pass, threshold>
RAG libraries and permissions: <names, Read/Write/Tool/Write file settings>
Web user/group restrictions: <groups and folder browsing requirements>
Remote worker requirement: <none or host/port/provider>
Error/log evidence: <exact error plus relevant .config/.data file location>
Do not expose: API keys, tokens, worker shared secrets, TLS private keys.
```

## Verification Checklist

After making configuration changes, verify in this order:

1. Test the provider/model in `Providers`.
2. Test/connect required MCP servers in `MCP Servers`.
3. Use `Project Settings` -> `Check context window` to verify instructions, variables, selected tools and RAG context.
4. Test embeddings/image status in `RAG Service` where RAG is involved.
5. Confirm `Web Config` shows a running server on the intended address/port/TLS mode.
6. Confirm user group/project binding and folder permissions in `Admin Config`.
7. Log in through the website as the intended user, create a chat, verify permitted model/mode/variables, and send a small tool-requiring request.

