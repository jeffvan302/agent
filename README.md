# Agent Desktop

Agent Desktop is the local setup and service host for the web-based agent system. Most day-to-day work happens in the website after the app has been configured and started.

## Quick Start

1. Run `agent.exe`.
2. Click **Setup System**.
3. Confirm the overwrite warning.
4. Let the setup terminal finish.
5. Restart the app when prompted.
6. Open the website.

Setup System restores the bundled default configuration, creates `C:\Temp` if needed, installs required system tools including OpenSSL for HTTPS/TLS, pulls local Ollama models, and prepares the default web server configuration.

The setup step checks for OpenSSL 4 on PATH and uses winget to install or upgrade `ShiningLight.OpenSSL.Light` when needed. If the light package cannot be installed, it falls back to `ShiningLight.OpenSSL.Dev`.

By default, the web server listens on HTTPS port `8080`, with HTTP port `80` redirecting to HTTPS.

Default website login:

- Username: `admin`
- Password: `password`

Change the default password after first login.

## Ollama API Key

Before using the default Ollama Direct provider:

1. Create an API key at [https://ollama.com/settings/keys](https://ollama.com/settings/keys).
2. In the desktop app, click **Providers**.
3. Select **Ollama Direct**.
4. Click **Edit Provider**.
5. Paste the key into the **API key** text box.
6. Click **Save**.

## Using The Website

The website is the main interface for projects, chats, automation, planning, tool use, and agent modes. Use the desktop app primarily for initial setup, provider configuration, web server configuration, and local service hosting.

After signing in, select a project and chat, choose the desired mode/model settings, and send messages from the chat interface. Automation sequences can be created from the chat UI and run against the selected chat.

## Project Folder

The default project folder is under `C:\Temp`.

To change it:

1. Select a project, such as **Create Applications**.
2. Click **Project Settings**.
3. Scroll to **Project variables**.
4. Change the project folder value.
5. Save the settings.

## Built-in Variables

| Variable | Description |
|----------|-------------|
| \$ProjectFolder\$ | The project's root folder path |
| \$PROJECTNAME\$ | The project name |
| \$CHATNAME\$ | The current chat name |
| \$CHATFULLNAME\$ | The full chat name (includes username suffix) |
| \$CHATID\$ | The unique chat identifier |
| \$USERNAME\$ | The user's login name |
| \$USER\$ | The user's display name |
| \$USEREMAIL\$ | The user's email address |

Syntax Options

- \$VARNAME\$ - standard form
- \$<VARNAME>\$ - angle-bracket form
- \$VARNAME_\$ - underscore modifier (replaces spaces with underscores)

Notes

- Variable names are case-insensitive (e.g., $username$ and $USERNAME$ resolve to the same value)
- Variables support nesting - one variable's value can reference another variable
- You can also define custom project variables in the project settings dialog, which can reference these built-ins (e.g., "value": "$ProjectFolder$\\backups")
- \$ProjectFolder\$ is special - it is seeded from the MCP global binding configuration before other variables are resolved

The variables are used throughout the app for path templates (planner storage, filesystem working directory, artifact memory folders, etc.) and MCP server variable bindings.

For variable names that end with `_`, spaces are converted to underscores. For example, a chat-name-style variable with a trailing underscore can be used when a path-safe version is needed.

## Setup System Models

Setup System pulls these Ollama models for local use:

- `qwen2.5vl:7b`
- `nomic-embed-text`
- `moondream:1.8b`
- `qwen3-embedding:0.6b`
- `qwen3-embedding:latest`

If a model pull fails because Ollama is still starting or the network is unavailable, rerun Setup System or run `ollama pull <model>` manually.
