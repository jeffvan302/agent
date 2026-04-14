# Web Server & Multi-User Access — Requirements

**Document version:** April 14 2026 (rev 3)  
**Codebase:** Native Win32 C++20 desktop application (MSVC)  
**Scope:** Phase 1 design requirements for the embedded web server and web-based chat interface

---

## 1. Overview

The system adds a companion web server **compiled directly into the desktop executable** — there is no separate installer, no separate process to manage, and no external web server dependency. The desktop executable retains its current role as the **admin interface** — the only surface from which the system is fully configured. The web server exposes a **multi-user chat interface** accessible from any browser, secured with HTTPS, and governed by users and groups that the admin defines in the desktop app.

The web server is started and stopped from within the app. When running, it serves HTML, CSS, and JavaScript files from a configurable root folder on the host file system. This makes the front-end independently editable and themeable without recompiling the executable. In a later phase, the default assets will be embedded as binary resources inside the executable itself so the app works out-of-the-box with no external files, but Phase 1 uses the file-system approach for development flexibility.

---

## 2. Admin Desktop Application Changes

### 2.1 Access Model

- The desktop executable **always runs as admin**. There is no login prompt on the executable itself.
- All user management, group management, project binding, and web server configuration is done exclusively through the admin UI.
- The web server login system is entirely separate from the desktop app's operation.

### 2.2 New Toolbar Buttons

Two new buttons are inserted in the main toolbar between **Model Tools** and **Setup System**:

| Position | Button Label | Function |
|---|---|---|
| Before Setup System | **Admin Config** | Opens the Admin Configuration dialog — user management, group management, group-to-project bindings, user project folder settings. |
| Before Admin Config | **Web Config** | Opens the Web Server Configuration dialog — port, TLS certificate, theme selection, base URL, start/stop controls. |

### 2.3 Admin Config Dialog — Users

- Create, edit, and delete user accounts.
- Fields per user: **username** (unique, used as the substitution variable `$<UserName>`), **display name**, **email address**, **password** (stored as a salted bcrypt hash — never plaintext), **enabled/disabled flag**.
- Force-password-reset flag that requires the user to change their password on next login.
- No user account has admin rights; admin access is exclusively the desktop executable.

### 2.4 Admin Config Dialog — Groups

- Create, edit, and delete groups.
- Assign any number of users to a group.
- A user may belong to multiple groups.

### 2.5 Admin Config Dialog — Project Bindings

- Assign one or more groups to a project.
- When a group is assigned to a project, all users in that group gain access to that project in the web interface.
- A project can have multiple groups bound to it; a group can be bound to multiple projects.
- Per-project settings exposed in Admin Config:
  - **Enable user project folder** — toggle whether the `$<UserProjectFolder>` variable is active for this project.
  - **User project folder root** — the base path on the server host into which per-user subdirectories are created or mapped (e.g., `C:\Output\Projects\MyProject\`). The full per-user path becomes `<root>\<username>\`.
  - These settings complement (and may reference) the existing `project_variables` system.

---

## 3. User & Group Variable System

Two new built-in project variables are automatically available when a web user is active in a project. They follow the existing `$<VarName>` substitution syntax.

| Variable | Value | Availability |
|---|---|---|
| `$<UserName>` | The authenticated user's username | Always available for web sessions |
| `$<UserProjectFolder>` | The user's resolved project folder path (`<root>\<username>`) | Only when the project has **Enable user project folder** turned on |

These variables are injected at request time (alongside other project variables) and are available in:

- Model tool instructions
- Project instructions (system prompt)
- MCP server command / arguments / working directory / env entries (via `ApplyProjectVariables`)

Admin sessions (desktop) do not inject `$<UserName>` or `$<UserProjectFolder>` unless explicitly set as regular project variables.

---

## 4. Web Server

### 4.1 Architecture — Embedded in the Executable

The web server is a set of C++ classes compiled directly into the main executable. It is **not** a separate process or a third-party server binary. Recommended implementation approach:

- Use a lightweight, header-friendly HTTP/HTTPS server library suitable for embedding in a C++ application (e.g., **cpp-httplib** with OpenSSL, or a similar single-header or small-footprint library that supports TLS and WebSockets).
- The server object is constructed at startup and owned by the main application object. `Start()` and `Stop()` methods are called from the Web Config dialog.
- The server shares the same process address space as the desktop app, meaning it can call into the same `McpManager`, `RagService`, `AppStorage`, and chat-processing code that the desktop UI uses — no IPC layer required.

### 4.2 Multi-Threading Model

Supporting multiple concurrent users requires careful thread design:

- The HTTP/WebSocket server runs its accept loop on a **dedicated listener thread**, separate from the Win32 message pump.
- Each accepted HTTP connection is handled on a **thread-pool worker thread**. A pool of 8–16 threads (configurable) handles all concurrent requests.
- Model inference requests (chat message sends) are processed on **their own threads**, exactly as the desktop app already does for sub-agent calls. The web handler submits the request and blocks the worker thread (or responds asynchronously via WebSocket) until the model result is ready.
- WebSocket connections are long-lived. Each open WebSocket is associated with one worker thread that services its message loop for the duration of the connection.
- All shared state accessed by web-handler threads (session store, user DB, project settings) must be protected by appropriate mutexes. The existing `AppStorage` and `McpManager` locking conventions apply.
- The Win32 UI thread is never blocked by web activity. Any web-triggered action that would touch UI state (e.g., updating a chat in the desktop view) is posted as a message to the UI thread via `PostMessage`.

### 4.3 Static File Serving

- Static assets (HTML, CSS, JS, images, fonts) are served from the **web root folder** defined in `web_settings.json` (see §5).
- The server maps URL paths to file system paths under the web root. For example, a request for `/themes/default/style.css` maps to `<web_root>/themes/default/style.css`.
- Only files within the web root are served; path traversal attacks (`../`) are blocked by normalizing the resolved path and verifying it is still within the web root before opening the file.
- MIME types are determined from file extension. At minimum: `.html`, `.css`, `.js`, `.json`, `.svg`, `.png`, `.jpg`, `.ico`, `.woff`, `.woff2`, `.ttf`.
- Static files are served with appropriate cache headers (`Cache-Control: max-age=3600` for assets, `no-store` for API routes and the login page).

### 4.4 TLS / HTTPS

- **Certificate sources** (configurable in `web_settings.json` and the Web Config dialog):
  - *Self-signed* — generated automatically on first launch using OpenSSL; suitable for LAN / internal use. The admin can export the certificate for import into client browsers.
  - *PEM file pair* — admin provides a certificate file and private key file (e.g., issued by an internal CA or Let's Encrypt / ACME).
  - *PKCS#12 / PFX* — admin provides a `.pfx` bundle and an optional passphrase.
- All connections use **TLS 1.2 or higher**; older protocol versions are disabled in the OpenSSL context.
- Certificate expiry date is displayed in Web Config with a warning indicator when within 30 days of expiry.
- Private key files are stored in a dedicated, access-controlled location on disk and are never exposed through any API or log.
- An optional HTTP→HTTPS redirect listener can run on port 80 (configurable) to catch accidental non-HTTPS access.

### 4.5 Web Config Dialog (Desktop)

| Setting | Description |
|---|---|
| Listen port | TCP port (default 8443) |
| HTTP redirect port | Optional plain-HTTP port that redirects to HTTPS (default off) |
| Bind address | `0.0.0.0` (all interfaces) or a specific IP |
| Base URL | Canonical external URL (used for HTTPS redirect and cookie `Domain`) |
| TLS certificate source | Self-signed / PEM files / PFX |
| Certificate / key file paths | File picker for PEM or PFX |
| Web root folder | Path to the folder containing HTML/CSS/JS assets (mirrors `web_settings.json`) |
| Start / Stop server | Toggle button; shows current status and uptime |
| Active connections | Read-only count of live sessions |
| Theme | Dropdown of available theme folders detected under `<web_root>/themes/` |
| Max upload file size | Per-request file attachment limit (default 50 MB) |
| Session timeout | Minutes of inactivity before a web session expires (default 60) |
| Thread pool size | Number of worker threads (default 8) |

All changes in this dialog are written back to `web_settings.json` on Save.

---

## 5. Configuration Files

All web server settings are stored in two JSON files that live in the application data folder (same directory as the main settings files). Both files are **auto-created with sensible defaults** if they do not exist when the application starts.

---

### 5.1 `web_settings.json`

Owns all web server operational configuration. The Web Config dialog reads from and writes to this file.

**Default content (auto-created):**

```json
{
  "enabled": false,
  "port": 8443,
  "http_redirect_port": 0,
  "bind_address": "0.0.0.0",
  "base_url": "",
  "tls": {
    "mode": "self_signed",
    "cert_file": "",
    "key_file": "",
    "pfx_file": "",
    "pfx_passphrase": ""
  },
  "web_root": "",
  "active_theme": "default",
  "session_timeout_minutes": 60,
  "max_upload_mb": 50,
  "thread_pool_size": 8,
  "http_redirect_enabled": false
}
```

**Field notes:**

- `enabled` — whether the server should auto-start when the application launches.
- `web_root` — absolute path to the folder that contains the `themes/`, `js/`, and `css/` asset directories. If empty or the folder does not exist, the application looks for an `www/` folder adjacent to the executable and uses that if found. If neither exists, it creates the `www/` folder and writes the default theme files into it (see §5.3).
- `active_theme` — name of the subdirectory under `<web_root>/themes/` to serve as the active theme.
- `tls.mode` — `"self_signed"`, `"pem"`, or `"pfx"`. Self-signed certificates are auto-generated and stored alongside this file as `server.crt` and `server.key` in the app data folder.

---

### 5.2 `users.json`

Owns all user accounts, groups, and group-to-project bindings. The Admin Config dialog reads from and writes to this file. This file never leaves the server; it is not served over HTTP under any circumstances.

**Default content (auto-created):**

```json
{
  "users": [],
  "groups": [],
  "project_bindings": []
}
```

**Schema:**

```json
{
  "users": [
    {
      "id": "<uuid>",
      "username": "jsmith",
      "display_name": "Jane Smith",
      "email": "jsmith@example.com",
      "password_hash": "<bcrypt hash>",
      "enabled": true,
      "force_password_reset": false,
      "created_at": "<ISO8601>",
      "last_login_at": "<ISO8601 or null>"
    }
  ],
  "groups": [
    {
      "id": "<uuid>",
      "name": "Engineering",
      "user_ids": ["<uuid>", "<uuid>"]
    }
  ],
  "project_bindings": [
    {
      "project_id": "<project id>",
      "group_ids": ["<uuid>"],
      "user_project_folder_enabled": false,
      "user_project_folder_root": ""
    }
  ]
}
```

**Security notes:**

- `password_hash` is always a bcrypt hash (cost ≥ 12). The plaintext password is never stored or logged.
- The file should be created with read/write permissions restricted to the owning user account on the host OS (e.g., `chmod 600` equivalent on Windows via ACL).
- Any write to `users.json` (user create/edit/delete) is an atomic operation: write to a temp file first, then rename, to prevent corruption on crash.

---

### 5.3 Auto-Creation of Web Assets

If the resolved `web_root` folder does not contain a `themes/default/` directory, the application **automatically creates the default theme** by writing the following files:

```
<web_root>/
  themes/
    default/
      theme.json
      style.css
      logo.svg
      favicon.ico
  js/
    app.js          ← main SPA logic
    markdown.js     ← marked.js (bundled copy)
    highlight.js    ← highlight.js (bundled copy)
    purify.js       ← DOMPurify (bundled copy)
  css/
    base.css        ← structural layout (not theme-specific)
  index.html        ← single-page app shell
  login.html        ← login page
```

These default files are **embedded as string literals or binary resources** inside the executable so they can always be written out without depending on an installer. In a future phase, the executable will serve them directly from memory without writing them to disk at all.

The auto-creation is a one-time operation. Once the files exist, the application never overwrites them, allowing the admin to freely customise the defaults.

---

## 6. Authentication & Session Security

### 6.1 Login Flow

- The web interface presents a login page as the only unauthenticated route.
- Login accepts **username + password**. Passwords are verified against the bcrypt hash stored by the admin.
- On success, the server issues a **signed, encrypted session token** (JWT or equivalent) delivered as an `HttpOnly`, `Secure`, `SameSite=Strict` cookie. The token is never accessible to JavaScript.
- Failed login attempts are rate-limited: after 5 consecutive failures within 5 minutes, the account is temporarily locked for 15 minutes. The admin can manually unlock accounts in Admin Config.
- No "remember me" persistent sessions beyond the configured session timeout.

### 6.2 Session Management

- Session tokens carry: user ID, session creation timestamp, last-activity timestamp, and a server-side random nonce.
- The server keeps a session store (in-memory or lightweight SQLite table) to allow forced logout and session revocation by the admin.
- Session expiry is sliding: activity resets the inactivity timer.
- On password change or admin-forced logout, all active sessions for that user are immediately invalidated.

### 6.3 Password Policy

- Minimum 10 characters; must include at least one uppercase, one lowercase, and one digit.
- Passwords are hashed with bcrypt (cost factor ≥ 12) before storage.
- The admin can trigger a forced password reset for any user.

---

## 7. Web Interface — Layout & UX

### 7.1 Overall Layout

```
┌─────────────────────────────────────────────────────────────────────────┐
│  [Logo / App Name]                              [User Name ▼]  [Logout] │
├────────────────────────┬────────────────────────────────────────────────┤
│  ▼ Project A           │                                                │
│    ▼ My chat 1 [Jeff]  │         Chat message area                      │
│      My chat 2 [Jeff]  │                                                │
│  ▶ Project B           │                                                │
│                        │                                                │
│  [+ New Chat]          ├────────────────────────────────────────────────┤
│                        │  [📎 Attach]  [Message input]      [Send ▶]   │
└────────────────────────┴────────────────────────────────────────────────┘
```

- **Left sidebar** — collapsible list of projects the user has access to, with their chats nested under each project. Projects collapse/expand independently. Active chat is highlighted.
- **Main area** — scrollable chat message area, with a fixed compose bar at the bottom.
- **Header** — app name/logo (themed), username display, logout button.

### 7.2 Chat Naming

- When a user creates a chat, they provide a name.
- The stored and displayed name is `<user-chosen name> [username]` — the username is **appended in brackets automatically** by the server. Users cannot remove this suffix.
- Users only see **their own chats**; chats belonging to other users in the same project are not visible.

### 7.3 Message Rendering

Each message in the chat area is rendered according to its role:

| Message Type | Visual Treatment |
|---|---|
| **User message** | Right-aligned or clearly distinguished bubble/block; plain text or light formatting |
| **Model response** | Left-aligned; full Markdown rendering (see §6.4) |
| **Thinking / reasoning** | Collapsible block visually separated from the response — e.g., a muted/indented disclosure widget labelled *"Thinking…"* that can be expanded. Thinking text is rendered as plain text or lightly formatted, never as rich markdown. |
| **Tool call / result** | Compact collapsed block: *"Called: tool_name"* with an expand control to view arguments and result JSON. |
| **Error** | Red-tinted block with error message and, where available, a retry option. |
| **System / context** | Subtle separator or badge; not shown by default in the normal flow. |

### 7.4 Markdown Rendering

Model responses are parsed and rendered as HTML using a client-side Markdown library (e.g., **marked.js** or **markdown-it**). Required support:

- Headings (H1–H6), bold, italic, strikethrough
- Ordered and unordered lists (nested)
- Tables (GitHub-Flavored Markdown style)
- Fenced code blocks with **syntax highlighting** (e.g., highlight.js)
- Inline code
- Blockquotes
- Horizontal rules
- Hyperlinks (opened in a new tab, with `rel="noopener noreferrer"`)
- Images embedded in markdown responses (rendered inline, constrained to chat width)
- LaTeX math rendering is **out of scope for Phase 1** but the architecture should not prevent adding it later (e.g., MathJax)
- All rendered HTML from model output is **sanitized** (e.g., DOMPurify) before insertion into the DOM to prevent XSS.

### 7.5 Streaming Responses

- Model responses stream token-by-token to the browser via a **WebSocket connection** (TLS).
- The Markdown renderer updates incrementally as tokens arrive — ideally re-rendering only the changed tail to avoid flicker.
- A visible **stop/cancel** button is available while a response is streaming.
- If the WebSocket drops mid-stream, the UI shows a reconnect indicator and attempts to resume or displays what was received.

### 7.6 File Attachments

- An **Attach** button (or drag-and-drop target) in the compose bar allows the user to attach files before sending a message.
- Supported types: images (PNG, JPG, WEBP, GIF), PDF, Word (.docx), Excel (.xlsx), plain text, Markdown.
- Attached files are shown as thumbnail chips in the compose area before sending. After sending, they appear as compact file chips in the user message bubble.
- Per-attachment size limit enforced on both client (UI warning) and server (hard reject).
- Files are stored server-side in a session-scoped or chat-scoped temporary area; the model receives them via the same mechanism as the desktop app's attachment handling.
- File names in the UI are the original names provided by the user.

---

## 8. Theming & Front-End Asset Structure

### 8.1 Web Root Layout

The web root folder (configured in `web_settings.json`) contains everything the browser needs. The structure is:

```
<web_root>/
│
├── index.html              ← Single-page app shell (loads base.css + active theme + JS)
├── login.html              ← Standalone login page (minimal dependencies)
│
├── css/
│   └── base.css            ← Structural layout only — uses CSS custom properties throughout;
│                              no hardcoded colors, fonts, or decorative values
│
├── js/
│   ├── app.js              ← Main SPA logic: routing, chat, WebSocket, API calls
│   ├── markdown.js         ← Bundled marked.js — Markdown → HTML parser
│   ├── highlight.js        ← Bundled highlight.js — syntax highlighting for code blocks
│   └── purify.js           ← Bundled DOMPurify — HTML sanitizer applied to all model output
│
└── themes/
    ├── default/            ← Ships with the app; created automatically if missing
    │   ├── theme.json
    │   ├── style.css       ← Overrides CSS custom properties; may add decorative rules
    │   ├── logo.svg        ← Header logo (SVG preferred for scaling)
    │   ├── favicon.ico
    │   └── fonts/          ← Optional: local webfonts referenced from style.css
    │
    ├── dark/               ← Example second theme (admin-created)
    │   ├── theme.json
    │   ├── style.css
    │   └── logo.svg
    │
    └── <custom>/           ← Admin drops any new folder here; auto-detected at runtime
```

### 8.2 index.html — App Shell

`index.html` is the entry point for all authenticated pages. It:

- Links `css/base.css` first (structure), then the active theme's `style.css` second (appearance). This load order means theme rules always win over base rules without needing `!important`.
- Loads `js/purify.js`, `js/highlight.js`, `js/markdown.js`, then `js/app.js` in that order.
- Contains a minimal DOM skeleton: header, sidebar `<nav>`, main `<section>`, and compose bar. All dynamic content is injected by `app.js`.
- The active theme path is injected into the HTML by the server at request time (a simple template substitution of `{{THEME_PATH}}`) so the correct `style.css` is referenced without client-side theme-switching logic.

### 8.3 login.html — Login Page

A standalone page with no dependency on `app.js`. It:

- Links `css/base.css` and the active theme's `style.css`.
- Contains only the login form: username field, password field, submit button, and an error message area.
- On successful POST to `/login`, the server responds with a redirect to `/`.
- If the user's `force_password_reset` flag is set, the server redirects to `/change-password` instead.

### 8.4 base.css — Structural Stylesheet

`base.css` defines the complete layout skeleton using **only** CSS custom properties for any value that a theme should be able to change. It does not set any colors, fonts, or decorative border-radius values directly — those all reference variables.

```css
/* Example of the convention used throughout base.css */
body {
  background-color: var(--color-bg-page);
  color: var(--color-text-primary);
  font-family: var(--font-body);
  font-size: var(--font-size-base);
}
.sidebar {
  background-color: var(--color-bg-sidebar);
  border-right: 1px solid var(--color-border-main);
}
.message-model {
  background-color: var(--color-bg-message-model);
  border-radius: var(--radius-message);
}
.thinking-block {
  background-color: var(--color-bg-thinking);
  color: var(--color-text-thinking);
  border-left: 3px solid var(--color-accent-thinking);
}
```

### 8.5 theme.json

Each theme folder contains a `theme.json` metadata file:

```json
{
  "name": "Default Light",
  "author": "Nardana Inc.",
  "version": "1.0",
  "description": "Clean, professional light theme"
}
```

The Web Config dialog reads all `theme.json` files under `<web_root>/themes/` to populate the theme dropdown. Folders without a valid `theme.json` are ignored.

### 8.6 style.css — Theme Stylesheet

Each theme's `style.css` defines values for all CSS custom properties that `base.css` uses, plus any additional decorative rules specific to that theme. A complete theme must at minimum provide all variables in the following categories:

| Variable Group | Variables |
|---|---|
| Page backgrounds | `--color-bg-page`, `--color-bg-sidebar`, `--color-bg-header` |
| Message bubbles | `--color-bg-message-user`, `--color-bg-message-model`, `--color-bg-thinking`, `--color-bg-tool-call` |
| Text | `--color-text-primary`, `--color-text-secondary`, `--color-text-muted`, `--color-text-thinking` |
| Accent / interactive | `--color-accent-primary`, `--color-accent-hover`, `--color-accent-thinking`, `--color-accent-danger` |
| Borders | `--color-border-main`, `--color-border-subtle` |
| Code blocks | `--color-bg-code`, `--color-text-code` |
| Typography | `--font-body`, `--font-mono`, `--font-size-base`, `--font-size-small` |
| Shape | `--radius-message`, `--radius-button`, `--radius-input` |

### 8.7 Theme Switching at Runtime

- Changing the active theme in the Web Config dialog writes the new `active_theme` value to `web_settings.json`.
- The change takes effect immediately for new page loads — no server restart required.
- Currently connected users will see the new theme on their next page navigation or browser refresh. Live mid-session theme switching is not required in Phase 1.
- The server scans the `themes/` folder at each Web Config open to discover newly added themes. No restart is required to make a new theme folder appear in the dropdown.

### 8.8 Default Theme — Visual Specification

The default theme shipped with the app should be clean, professional, and readable. Design targets:

- Light background (`#FFFFFF` / `#F8F9FA` page), dark text (`#212529`).
- Sidebar in a slightly darker or tinted panel (`#F1F3F5`).
- User messages right-aligned, light blue bubble (`#E3F2FD`).
- Model messages left-aligned, white card with subtle shadow.
- Thinking blocks: left-bordered, muted amber background (`#FFF8E1`), italic text.
- Tool call blocks: collapsed by default, monospace font, gray background.
- Accent color: `#1565C0` (deep blue) for buttons, links, active states.
- Code blocks: near-black background (`#1E1E1E`), light text — matches VS Code Dark+ palette for familiarity.
- Font stack: `"Segoe UI", system-ui, -apple-system, sans-serif` for body; `"Cascadia Code", "Consolas", monospace` for code.

---

## 9. API Design (Internal)

The web server exposes the following route categories. All routes except `/login` and `/static/*` require a valid session cookie.

| Route Group | Purpose |
|---|---|
| `POST /login` | Authenticate; issues session cookie |
| `POST /logout` | Invalidates session |
| `GET /api/projects` | List projects accessible to the current user |
| `GET /api/projects/:id/chats` | List this user's chats in a project |
| `POST /api/projects/:id/chats` | Create a new chat |
| `GET /api/chats/:id/messages` | Fetch message history for a chat |
| `POST /api/chats/:id/messages` | Send a message (triggers model request) |
| `DELETE /api/chats/:id` | Delete a chat (user's own only) |
| `POST /api/chats/:id/attachments` | Upload a file attachment for a pending message |
| `WS /ws/chats/:id` | WebSocket for streaming model responses |
| `GET /static/*` | Theme assets, JS, CSS (no auth required) |

---

## 10. Data Storage

- User accounts, groups, group-project bindings, and session records are stored in a **dedicated SQLite database** (`users.db`) in the app data folder, separate from project and chat storage.
- Chat messages created via the web interface are stored in the same chat/project storage as desktop chats, but tagged with the originating user ID so they can be filtered per-user.
- Session tokens are stored in-memory with a serialized fallback to `users.db` so that a server restart does not immediately invalidate all active browser sessions (within the expiry window).
- File attachments are stored in a configurable `uploads/` directory (default: app data folder). The path is configurable in Web Config.

---

## 11. Security Hardening

Beyond authentication, the following measures are required:

- **HTTPS only** — no plaintext HTTP handling of authenticated requests.
- **HSTS** header (`Strict-Transport-Security`) sent on all responses.
- **CSP** header (`Content-Security-Policy`) restricting script sources to self and the explicitly bundled libraries.
- **X-Frame-Options: DENY** — prevents clickjacking.
- **X-Content-Type-Options: nosniff**
- **Rate limiting** — login endpoint and message submission endpoint are rate-limited per IP.
- **Input validation** — all API inputs validated server-side; reject unexpected fields.
- **Output sanitization** — all model output HTML-sanitized before client rendering (DOMPurify or equivalent).
- **No sensitive data in logs** — passwords, session tokens, and file contents are never written to log files.
- **Audit log** — login successes/failures, chat creation/deletion, and admin actions are written to a separate audit log with timestamp and IP address.

---

## 12. Phase 1 Scope Boundary (Out of Scope)

The following are explicitly deferred to later phases:

- **Embedded asset resources** — default HTML/CSS/JS files served from memory inside the executable (Phase 1 uses the file-system `web_root` folder; embedding is a future hardening step)
- Per-user theme selection
- Admin web panel (all admin actions are desktop-only in Phase 1)
- Email-based password reset / account recovery (admin manually resets in Phase 1)
- Two-factor authentication (2FA)
- OAuth / SSO / LDAP integration
- Public project sharing or guest access
- Real-time collaborative chat (multiple users in the same chat simultaneously)
- LaTeX / math rendering
- Push notifications
- Mobile native app
- Horizontal scaling / multi-instance deployment

---

## 13. Implementation Milestones (Suggested Order)

1. **`web_settings.json` + `users.json` bootstrap** — auto-create both files with defaults on startup; write default web asset files to `web_root` if missing.
2. **Embedded HTTP/HTTPS server skeleton** — integrate cpp-httplib (or equivalent), TLS context setup with self-signed cert generation, start/stop lifecycle.
3. **Thread pool + static file serving** — worker thread pool, path-traversal-safe static file handler, MIME type map, cache headers.
4. **Web Config dialog** — all settings fields wired to `web_settings.json`; theme dropdown scans `themes/` folder.
5. **Default theme HTML/CSS/JS files** — `index.html`, `login.html`, `base.css`, default `style.css`, bundled `markdown.js`, `highlight.js`, `purify.js`, `app.js` skeleton.
6. **`users.json` + Admin Config dialog** — user CRUD, group CRUD, group-project binding, user project folder settings, bcrypt password hashing.
7. **Authentication layer** — `/login` route, session store, `HttpOnly`+`Secure` cookie, rate limiting, forced password reset flow.
8. **Project and chat API** — list projects/chats (filtered by user), create chat, fetch message history.
9. **Message send + synchronous response** — POST message → model call → return full response.
10. **WebSocket streaming** — upgrade to streaming WS delivery; real-time token-by-token rendering in `app.js`.
11. **File attachment upload and handling.**
12. **Markdown rendering + thinking block + tool call UI** — wire in marked.js, highlight.js, collapsible thinking/tool sections.
13. **Toolbar buttons wired up** — Web Config and Admin Config buttons inserted in desktop toolbar.
14. **Security hardening pass** — HSTS, CSP, X-Frame-Options, audit log, DOMPurify integration review.
15. **Second (dark) theme** as a reference for theme authors.
16. **Future: embed default assets as binary resources** inside the executable for zero-file-dependency deployment.

---

---

## 14. Phased Implementation Plan

The implementation is structured into five phases. **Phase 0 is a proof-of-concept** designed to produce a working, end-to-end web chat session as quickly as possible so the integration between the server, the model, and the browser can be validated before building out the full feature set. Each subsequent phase builds incrementally on the last without requiring re-architecture.

---

### Phase 0 — Proof of Concept: "It Works"

**Goal:** A developer can open a browser, log in as a test user, select a project, send a message, and receive a streaming response from the model. Nothing needs to be production-quality; everything needs to be demonstrably functional.

**Exit criteria:**
- Browser can reach the app over HTTPS (self-signed cert, browser exception accepted).
- Login form accepts a hardcoded or `users.json`-defined test user and rejects bad credentials.
- After login, the user sees at least one accessible project and can create a chat.
- Sending a message triggers a real model call and the response appears in the browser (streaming preferred; full-response fallback acceptable).
- The session is isolated — a second browser tab logging in as a different user sees only that user's chats.

**In scope for Phase 0:**

| Component | What to build | Shortcuts permitted |
|---|---|---|
| HTTP/HTTPS server | Integrate cpp-httplib; self-signed cert auto-generated; single listen thread + small fixed thread pool (4 threads) | No persistent config yet; port/cert hardcoded or read from a minimal `web_settings.json` |
| Static file serving | Serve `index.html`, `login.html`, `base.css`, one `style.css`, and `app.js` from a `www/` folder next to the executable | Files hand-written for POC; no auto-creation logic yet |
| Authentication | `POST /login` reads `users.json`; issues a signed session cookie; `GET /logout` clears it | Password stored as bcrypt hash; rate limiting can be a TODO comment |
| Session store | In-memory `std::unordered_map<token, UserSession>` protected by a mutex | No persistence across restart |
| Project & chat listing | `GET /api/projects` — returns projects the user is bound to; `GET /api/projects/:id/chats` — returns this user's chats | Group binding logic can be a flat user→project list in `users.json` for now |
| Chat creation | `POST /api/projects/:id/chats` — creates a chat record in existing storage | Chat name gets `[username]` suffix applied server-side |
| Message send + response | `POST /api/chats/:id/messages` — calls into the existing model pipeline synchronously; returns full response as JSON | WebSocket streaming is the next phase; blocking POST is fine for POC |
| Basic HTML/CSS/JS | `login.html` with form; `index.html` with sidebar (project/chat list), message area, compose bar; vanilla JS fetch calls; no Markdown rendering yet | Inline styles acceptable; no theme system yet |
| Toolbar buttons | Add **Web Config** and **Admin Config** stubs to the desktop toolbar that show a "not yet implemented" message box | Full dialogs come in Phase 1 |
| `users.json` bootstrap | Auto-create with one default test user (`admin` / `changeme`) if the file does not exist | Minimal schema only |

**What Phase 0 deliberately omits:** full Web Config dialog, Admin Config dialog, WebSocket streaming, Markdown rendering, file attachments, theming system, security hardening, rate limiting, audit log, path traversal protection, proper error pages.

**Estimated complexity:** Medium — the largest single piece of new work is integrating cpp-httplib with OpenSSL and bridging the HTTP handler threads safely to the existing model-call code.

---

### Phase 1 — Foundation: Configuration, Admin UI & Proper Auth

**Goal:** Everything needed to hand the app to a second person and have them use it correctly — proper dialogs, persistent config, real session security, and clean server lifecycle management.

**Builds on Phase 0 by adding:**

- **Web Config dialog** (desktop) — all fields from §4.5 wired to `web_settings.json`. Start/Stop button with live status. Theme dropdown (scans `themes/` folder). Thread pool size control.
- **Admin Config dialog** (desktop) — user CRUD, group CRUD, group-to-project bindings, user project folder toggle and root path. All changes write atomically to `users.json`.
- **`web_settings.json` full schema** — all fields from §5.1 honoured. Auto-created with defaults on first run. `web_root` resolution with fallback to `www/` folder.
- **`users.json` full schema** — UUIDs, `last_login_at`, `force_password_reset`, `created_at`. Group and binding structures fully implemented.
- **Proper session management** — sliding expiry, server-side session store with revocation, admin-forced logout from Admin Config.
- **Force password reset flow** — `/change-password` route and form; blocks access to the rest of the app until completed.
- **Auto-creation of default web assets** — on startup, if `<web_root>/themes/default/` is missing, write all default files from embedded string literals (§5.3). Never overwrites existing files.
- **HTTP → HTTPS redirect** — optional plain-HTTP listener that issues a 301 redirect.
- **Security basics** — HSTS header, `X-Frame-Options: DENY`, `X-Content-Type-Options: nosniff`, path traversal prevention in static file handler.
- **Rate limiting** — login endpoint: 5 failures → 15-minute lockout, tracked in memory per IP.
- **Audit log** — append-only log file: login success/failure, chat creation/deletion, admin actions, with timestamp and IP.

---

### Phase 2 — Real-Time Streaming & Full Chat UI

**Goal:** The chat experience in the browser feels as responsive as the desktop app, with streaming responses and properly rendered output.

**Builds on Phase 1 by adding:**

- **WebSocket streaming** — upgrade the message-send flow from blocking POST to a WebSocket session (`WS /ws/chats/:id`). The model pipeline pushes tokens to the WebSocket as they arrive. `app.js` renders tokens incrementally.
- **Thinking block rendering** — model thinking output is wrapped in a collapsible `<details>` element, visually muted. Collapsed by default; click to expand.
- **Tool call block rendering** — each tool call/result pair rendered as a compact collapsible card showing tool name, arguments, and result.
- **Markdown rendering** — integrate marked.js for model response bodies. DOMPurify sanitizes all rendered HTML before DOM insertion. highlight.js applied to fenced code blocks.
- **Stop/cancel** — a cancel button is active during streaming; sends a cancel signal to the server which aborts the model call and closes the WebSocket message loop.
- **Reconnect handling** — if the WebSocket drops mid-stream, `app.js` displays a reconnect indicator and attempts to re-attach to the in-progress response.
- **File attachments** — `POST /api/chats/:id/attachments` multipart upload. Attachment chips in compose bar. Images rendered inline in the user message bubble; other file types shown as labelled file chips.
- **Chat management** — rename a chat (user can edit the name portion; `[username]` suffix is immutable), delete a chat (user's own only), and a confirmation prompt before deletion.
- **Message history pagination** — `GET /api/chats/:id/messages` supports a `before` cursor for loading older messages on scroll.

---

### Phase 3 — Theming, Polish & Security Hardening

**Goal:** The interface is presentable to end users, customisable by the admin, and hardened against common web vulnerabilities.

**Builds on Phase 2 by adding:**

- **Full theme system** — `base.css` CSS custom property convention fully enforced. `theme.json` metadata. Web Config theme dropdown live. Theme change takes effect on next page load without restart.
- **Dark theme** — a complete second theme as a reference for future theme authors, using the variable set defined in §8.6.
- **CSP header** — `Content-Security-Policy` restricting script execution to self and the bundled JS files.
- **`$<UserName>` and `$<UserProjectFolder>` injection** — web-session variables populated at request time and passed into the model pipeline alongside existing project variables.
- **User project folder creation** — on first chat message from a user in a project where `user_project_folder_enabled` is true, the server creates `<root>/<username>/` if it does not exist.
- **Change password page** — accessible from the user menu at any time (not just on forced reset). Requires current password confirmation.
- **Session list in Admin Config** — shows active web sessions (user, IP, last activity) with a "Force logout" button per session.
- **Error pages** — custom styled 404, 403, and 500 pages consistent with the active theme.
- **Responsive layout** — sidebar collapses to a hamburger menu at narrow viewport widths for tablet/phone browser access.
- **Favicon and logo** served from active theme folder.

---

### Phase 4 — Hardening & Future Features

**Goal:** Production-ready packaging and extensibility hooks. These items are planned but not scheduled.

| Item | Notes |
|---|---|
| Embed default assets as binary resources | Compile `www/` files into the executable as resource data; serve from memory. App works with zero external files. |
| Per-user theme preference | User can pick from available themes; preference stored in `users.json`. |
| Admin web panel | Lightweight admin-only section of the web UI for user management without needing the desktop app. |
| Email-based password reset | Requires SMTP config; admin sets up an outbound mail server in Web Config. |
| Two-factor authentication (TOTP) | Authenticator app QR code enrolment; enforced per-group or per-user. |
| OAuth / SSO | Azure AD, Google Workspace, or generic OIDC for organisations with an identity provider. |
| LaTeX / math rendering | Integrate KaTeX or MathJax for mathematical notation in model responses. |
| Real-time collaborative chat | Multiple users sharing a single chat session; out of scope until chat architecture is revisited. |

---

### Phase Summary Table

| Phase | Theme | Key Deliverable | When You Know It's Done |
|---|---|---|---|
| **0 — POC** | "Does it work?" | End-to-end browser chat with real model output | Two different users log in from different browser tabs and each see only their own chats |
| **1 — Foundation** | "Is it configurable?" | Admin & Web Config dialogs, full `users.json` / `web_settings.json`, proper auth | Admin creates a user via the dialog, that user logs in, forced-reset flow works |
| **2 — Full Chat** | "Is it usable?" | WebSocket streaming, Markdown, thinking blocks, file attachments | Chat experience feels equivalent to the desktop app |
| **3 — Polish** | "Is it presentable?" | Theming, dark mode, CSP, user project folders | Admin drops a new theme folder in and it appears in Web Config without restart |
| **4 — Production** | "Is it embeddable?" | Assets compiled in, per-user prefs, optional SSO | App runs with zero external files; organisation can deploy to staff without a manual setup step |

---

*End of document.*
