# Agent Desktop User and Configuration Guides (This Web Tool)

This guide set describes the implemented configuration surfaces in Agent Desktop and the web chat it serves. It is written for administrators, project owners, and anyone supplying accurate setup context to an AI assistant.

## Guide Index

| Guide | Use it for |
| --- | --- |
| [01 - Getting Started and System Setup](01-Getting-Started-and-System-Setup.md) | Desktop navigation, initial setup, project creation and cloning, storage locations |
| [02 - Providers and Models](02-Providers-and-Models.md) | Adding model providers, ChatGPT/Codex browser sign-in, local and remote models, binding providers |
| [03 - MCP Servers and Project Settings](03-MCP-Servers-and-Project-Settings.md) | MCP configuration and the complete Project Settings field reference |
| [04 - Model Tools, Agentic Modes, and Context Windows](04-Model-Tools-Agentic-Modes-and-Context-Windows.md) | Sub-agent tools, modes, compression strategies and pre-passes |
| [05 - Web Server and Administration](05-Web-Server-and-Administration.md) | Web Config, themes/styles, TLS, users, groups and browser project access |
| [06 - RAG and Image Ingestion](06-RAG-and-Image-Ingestion.md) | RAG libraries, ingesting data, import/export, image processing |
| [07 - Remote Models and Command Line](07-Remote-Models-and-Command-Line.md) | Remote worker JSON, running workers, command-line modes |
| [08 - Configuration Reference for AI Assistance](08-Configuration-Reference-for-AI-Assistance.md) | Settings-to-file map, implementation terms and troubleshooting handoff checklist |

## Configuration Flow

A useful setup order is:

1. Start the app and decide whether to use `Setup System`. Read its overwrite warning first.
2. Open `Providers`, add at least one provider and one model, and test it.
3. Open `MCP Servers`, configure any external tools and their variables.
4. Open `Context Window`, create any compression configurations needed by projects.
5. Open `RAG Service`, create libraries and configure image ingestion if documents or images will be searched.
6. Create or select a project, then open `Project Settings` to assign models, MCP servers, tools, RAG libraries, variables, instructions and web behavior.
7. Open `Web Config`, set the network and TLS configuration, and start the web server.
8. Open `Admin Config`, create users/groups and grant project access.

## Terminology

| Term | Meaning in this app |
| --- | --- |
| Provider | A model connection, for example OpenAI OAuth, Ollama Local, LM Studio, or a remote Agent worker. |
| Model | A configured model underneath a provider, including capability flags such as tools or vision. |
| MCP server | An external process exposing tools to selected projects or model tools. |
| Built-in tool | A tool implemented by the app, such as Project Filesystem, PowerShell, Planner or Completion Driver. |
| Model tool | A model-backed tool invoked by another model as a sub-agent. |
| Agentic mode | A named instruction set that may be selected for a project, a web chat, or a model tool. This is the feature sometimes referred to as "genetic modes." |
| RAG library | A managed searchable document store that may also be exposed as tools to a model. |
| Context compression | A configuration that controls what prior conversation information is sent on later prompts. |

## Important Operating Notes

- The desktop app is primarily an administration and diagnostics surface. Live user chats are intended to be used through the web interface.
- A project must be selected before most project-scoped settings, MCP assignments, or RAG attachments are meaningful.
- `Setup System` is destructive: it deletes existing projects and project chat data before restoring packaged configuration.
- Do not place passwords, provider keys, worker secrets or token values into instructions copied to another person or model.
- Use relative browser links such as `/data/...` and `/rag/...` when sharing files through the served website. A `127.0.0.1` URL points at the browsing user's own computer.

## Storage Overview

The app stores configuration under `.config` and runtime/chat data under `.data` beneath the application startup root.

| Data | Location |
| --- | --- |
| Providers | `.config/providers.json` |
| Provider authentication store | `.config/provider_auth.json` and `.config/provider_auth_bridge/` |
| MCP servers and global variables | `.config/mcp_servers.json` |
| Context configurations | `.config/context_compression_configs.json` |
| Model tools | `.config/model_tools.json` |
| Agentic modes | `.config/agentic_modes.json` |
| Web server configuration | `.config/web_settings.json` |
| Web users, groups and project grants | `.config/users.json` |
| Per-project settings | `.config/projects/<project_id>/project_settings.json` |
| Per-project RAG bindings | `.config/projects/<project_id>/project_rag.json` |
| RAG image ingestion configuration | `.config/rag/rag_image_ingest_settings.json` |
| Chat/runtime data | `.data/projects/<project_id>/chats/<chat_id>/` |

