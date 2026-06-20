# Agent Desktop

This application is a proof of concept of how large language models can be integrated into most systems.  Using Ollama, we can try different models that are available to us.  Some are really efficient at coding using very few resources to do so.   Others ideal for certain gatekeeping tasks.

This is an executable that has a web server built into it. With this app, you can install all the required features by clicking the Setup System button. This app contains all of the wrapper tools that you might need, such as MCP servers, RAG databases and even other built in tools.
***Caution should be taken when running this application, since the default settings will allow the large language models will have access to your files.***
With this, you can configure certain examples, use certain models, and test them for what you might want to use in real implementations. 
You can check whether they do follow the rules like you want them to. Or are they going to give some sort of apple pie recipe to a user if they can be fooled to do so? 
On the other hand, you can go all extreme and set up a model to code actual applications for you, all automated with some question-asking capabilities in between. 
Agent Desktop is the local setup and service host for the web-based agent system. Most day-to-day work happens in the website after the app has been configured and started.

## Quick Start

1. Run `agent.exe`.
2. Click **Setup System**.
3. Confirm the overwrite warning.
4. Let the setup terminal finish.
5. Restart the app when prompted.
6. Open the website.

[![Quick Start Guide](/quick-guide.jpg)](/quick-guide.jpg)

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

## Try Coding:

After setting up everything with the system setup, you can go to the website, click on Create Applications, and create a new chat for your application. 
At the bottom, make sure your mode is Planner with tools. And ask the following: 
``` I'd like to develop an application, maybe in Node.js. That can monitor the CPU and memory usage and display that on a website. ```

Answer any prompts that are asked of you. And when you're happy with the planning that it's done so far, you can switch the mode to Task Execution And tell it to execute.
If you want to do any additional refinement to the project, switch the mode to Refinement. And at this point, you can ask it to do a step-by-step refinement of items. Or add specific tasks that you want the system to complete. 

## Gate Keeping

Next, we can try some gatekeeping examples. Click on the gatekeeping example and click on need chat. Create the chat. And then at the bottom, you've got different modes. There's a grumpy old man as well as a deflective comedian.  At the bottom, you can also select the different model types. And see if you can break the model by asking it different questions.  Inside the actual application in the GUI, there is an Agentic Modes button.  There you can examine under which rules the grumpy old man or the deflective comedian operate. 

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
