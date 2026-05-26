# Web Server and Administration

## Web Config

Click `Web Config` to configure the website used for user chats. The dialog includes current status and a `Start`/`Stop` control, plus `Save`, `OK` and `Cancel`.

### Network and hosting settings

| Field | Description |
| --- | --- |
| `Port` | Main HTTP or HTTPS serving port, depending on TLS mode. |
| `Bind address` | Network interface address. Use `0.0.0.0` to listen on all appropriate interfaces, or a specific server LAN address to restrict it. |
| `Base URL` | Public address used when an absolute server address must be constructed, for example `https://server-name:8443`. |
| `Web root` -> `Folder` | Optional custom web asset directory. Empty uses the app's detected/default `www` assets. |
| `Theme` | Active theme found within `<web_root>/themes/<theme-name>`. |
| `Thread pool` | HTTP worker pool setting, accepted from 1 through 64. |
| `Session timeout` | Login session timeout in minutes, accepted from 1 through 10080. |
| `Start web server automatically on launch` | Saves the server as enabled for future app launches. |

The server configuration is stored in `.config/web_settings.json`. It also stores `max_upload_mb`, whose default is 50 MB; there is currently no visible edit control for that value in the Web Config dialog.

### Network access guidance

- `127.0.0.1` is useful only from the same computer running the app.
- For users on other computers, set a reachable bind/interface configuration and use the server host name or LAN address to open the site.
- Chat-generated links to data should normally be relative links beginning with `/data/` or `/rag/`, so they resolve against the browser's current server rather than against the wrong local machine.

## TLS and HTTPS

The `Security (HTTPS / TLS)` section provides:

| Mode in UI | Purpose | Current runtime status |
| --- | --- | --- |
| `Off (HTTP)` | Plain HTTP only. | Supported. |
| `Self-signed (auto-generate)` | Generate/use app-managed certificate and private key. | Supported when the build has OpenSSL support. |
| `PEM files (cert + key)` | Use administrator-provided certificate and private key paths. | Supported. |
| `PFX / PKCS#12 bundle` | UI fields for a PFX path and passphrase. | Present in configuration UI, but the current server runtime rejects `pfx` TLS mode as unsupported. Do not choose this for an active service until implemented. |

TLS-related fields:

| Field | Used with |
| --- | --- |
| `Certificate (.crt/.pem)` | PEM mode |
| `Private key (.key/.pem)` | PEM mode |
| `PFX bundle (.pfx)` and `PFX passphrase` | PFX option, currently not usable by runtime |
| `HTTP redirect port` | Any enabled TLS mode; `0` disables HTTP-to-HTTPS redirection |
| `Cert expires` | Inspection of the configured certificate |
| `Generate Self-Signed Certificate` | Self-signed mode |

For a real shared deployment, prefer a PEM certificate trusted by user browsers. Self-signed mode is suitable for controlled environments after users understand the certificate warning/trust requirement.

## Styles and Themes

Themes are filesystem-based web assets:

1. Provide a `Web root`, or use the app-managed default web root.
2. Place each theme in `<web_root>/themes/<theme-name>/`.
3. At minimum, maintain the appropriate theme assets such as `style.css` and `theme.json` following the existing `default` and `dark` themes.
4. Reopen or refresh `Web Config`; its `Theme` list scans theme subdirectories.
5. Choose the theme, save, and reload the web browser.

For an empty custom themes directory, the dialog still offers `default`. The built-in default asset root includes `default` and `dark` themes. When using a custom `Web root`, treat its content as administrator-owned web assets.

## Admin Config

Click `Admin Config` to open `Web Server Administration`. It contains three tabs: `Users`, `Groups` and `Bindings`.

### Users

Use `Add...`, `Edit...`, `Delete`, `Enable/Disable`, `Reset Password...` and `Force Logout`.

The user editor fields are:

| Field | Purpose |
| --- | --- |
| `Username` | Unique login name. It is also used when resolving per-user paths. |
| `Display name` | Friendly display identity. |
| `Email` | Administrative/user record contact field. |
| `Password` | Required when creating a user. Password reset is a separate action after creation. |
| `Account enabled` | Controls whether the user can authenticate. |
| `Force password reset on next login` | Requires replacement of a newly assigned/reset password. |
| `Allow browsing folders` | Grants the user half of the permission required to browse server filesystem locations during web new-chat creation. |

Password reset requires at least 8 characters and makes the user change it on the next login. `Force Logout` invalidates active access for that user through the running server.

### Groups

Groups determine which users may access assigned projects:

1. Open `Groups`.
2. Use `New...` to add a group.
3. Select it, edit `Group name` if needed.
4. In `Members assigned (check users)`, check the users belonging to that group.

### Bindings

Bindings connect projects to allowed groups:

1. Open `Bindings`.
2. Select a project.
3. Check permitted groups in `Allowed groups (click to toggle)`.
4. Optionally configure the per-user project-folder base.

Binding folder controls:

| Field | Purpose |
| --- | --- |
| `Enable per-user project folder` | Establishes a base root for project-specific per-user locations. |
| `Base folder` | Root path; a user's resolved path is derived under that root using the username. |
| `Browse...` | Chooses the base folder on the server machine. |

## Allow Web Users to Browse New Chat Folders

To allow a web user to set `ProjectFolder` when creating a chat, all of these must be true:

1. In `Admin Config` -> `Users`, enable `Allow browsing folders` for that user.
2. In `Project Settings`, enable `Allow privileged users to browse new chat locations, (set ProjectFolder variable)`.
3. Assign the user, through a group binding, to the project.

When authorized, the web new-chat window shows the default location, a folder input and server-side browser. The folder path preview can update as the chat name is typed. A non-existing typed path is created only when `Create Chat` is confirmed.

Other variables with `Allow user definition` enabled appear as text fields in the same new-chat window. They do not provide filesystem browsing.

## Web Chat Model and Mode Selection

A project may make the following available in the web chat:

| Project configuration | Web behavior |
| --- | --- |
| `User Select Model` plus checked models | User may choose approved streaming/tool-capable models instead of the default. |
| `Enabled Agentic Modes` | User may select an allowed behavior mode for the chat. |
| Project variable `Allow user definition` | User supplies per-chat variable values during chat creation. |
| Privileged folder browsing permissions | User selects `ProjectFolder` at chat creation. |

## Security Checklist

- Bind only to interfaces necessary for users.
- Enable HTTPS before transmitting credentials over an untrusted network.
- Grant projects through groups, not by sharing administrator accounts.
- Give `Allow browsing folders` only to users who may inspect server paths.
- Enable `Serve /data and /rag web file links inline (risky)` only after reviewing document exposure requirements.
- Treat custom web roots, TLS private keys, remote worker JSON and provider credentials as administrative data.

