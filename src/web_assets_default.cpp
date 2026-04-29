// AUTO-GENERATED — do not edit by hand.
// Including the header first gives const variables external linkage.
#include "web_assets_default.h"

namespace DefaultWebAssets {

const char kIndexHtml[] =
R"ASSET(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Agent Chat</title>
  <link rel="stylesheet" href="/css/base.css">
  <link rel="stylesheet" href="{{THEME_PATH}}/style.css">
  <!-- Vendor libraries — served from local cache after first-start download.
       The server 302-redirects to cdnjs until the background download
       completes, so syntax highlighting works from day one. -->
  <link rel="stylesheet" href="/css/vendor/vs2015.min.css">
</head>
<body>
<div id="app">
  <!-- Header -->
  <header id="header">
    <span id="header-title">Agent Chat</span>
    <div id="header-user">
      <button id="header-account-btn" type="button">
        <span id="header-username"></span>
      </button>
      <a id="logout-link" href="/logout">Sign out</a>
    </div>
  </header>

  <!-- Body row -->
  <div id="body-row">
    <!-- Sidebar -->
    <nav id="sidebar">
      <div id="sidebar-header">Projects</div>
      <div id="project-list">
        <!-- Populated by app.js -->
      </div>
      <button id="new-chat-btn" disabled>+ New Chat</button>
    </nav>

    <!-- Main chat area -->
    <main id="main">
      <div id="chat-title">Select or create a chat</div>
      <div id="messages">
        <div id="empty-state">
          <div class="empty-icon">💬</div>
          <p>Select a chat from the sidebar or create a new one to get started.</p>
        </div>
      </div>
      <div id="attach-bar" style="display:none">
        <div id="attach-list"></div>
      </div>
      <div id="compose">
        <div id="compose-top">
          <div id="compose-top-left">
            <span id="agentic-mode-label" class="agentic-mode-none">Mode: None</span>
            <span id="compress-btn" title="Compress context window" style="display:none; margin-left:8px; cursor:pointer; font-size:var(--font-size-small); color:var(--color-accent-primary); user-select:none;">Compress</span>
          </div>
          <button id="debug-btn" type="button" style="display:none">Enable debugging</button>
        </div>
        <div id="compose-bottom">
          <button id="attach-btn" title="Attach file" disabled>&#128206;</button>
          <input id="file-input" type="file" multiple style="display:none">
          <textarea id="message-input" placeholder="Type a message…"
                    rows="1" disabled></textarea>
          <button id="send-btn" disabled>Send</button>
        </div>
      </div>
    </main>
  </div>
</div>

<div id="account-modal" class="app-modal" hidden>
  <div class="app-modal-backdrop" data-close-account-modal="true"></div>
  <div class="app-modal-panel" role="dialog" aria-modal="true" aria-labelledby="account-modal-title">
    <div class="app-modal-header">
      <h2 id="account-modal-title">Account</h2>
      <button type="button" class="app-modal-close" id="account-close-btn" aria-label="Close">×</button>
    </div>
    <form id="account-form" class="account-form">
      <div class="form-group">
        <label for="account-username">Username</label>
        <input type="text" id="account-username" readonly>
      </div>
      <div class="form-group">
        <label for="account-display-name">Display Name</label>
        <input type="text" id="account-display-name" autocomplete="name">
      </div>
      <div class="form-group">
        <label for="account-email">Email</label>
        <input type="email" id="account-email" autocomplete="email">
      </div>
      <div class="account-form-divider">Change password</div>
      <div class="form-group">
        <label for="account-current-password">Current Password</label>
        <input type="password" id="account-current-password" autocomplete="current-password">
      </div>
      <div class="form-group">
        <label for="account-new-password">New Password</label>
        <input type="password" id="account-new-password" autocomplete="new-password">
      </div>
      <div class="form-group">
        <label for="account-confirm-password">Confirm New Password</label>
        <input type="password" id="account-confirm-password" autocomplete="new-password">
      </div>
      <p class="account-form-help">Leave the password boxes blank if you only want to update your display name or email.</p>
      <div id="account-error"></div>
      <div class="app-modal-actions">
        <button type="button" class="secondary-btn" id="account-cancel-btn">Cancel</button>
        <button type="submit" class="login-btn" id="account-save-btn">Save Changes</button>
      </div>
    </form>
  </div>
</div>

<script src="/js/vendor/highlight.min.js"></script>
<script src="/js/vendor/marked.min.js"></script>
<script src="/js/vendor/purify.min.js"></script>
<script src="/js/vendor/mermaid.min.js"></script>
<script src="/js/vendor/vega.min.js"></script>
<script src="/js/vendor/vega-lite.min.js"></script>
<script src="/js/vendor/vega-embed.min.js"></script>
<script src="/js/vendor/cytoscape.min.js"></script>
<script src="/js/app.js"></script>
</body>
</html>
)ASSET";

const char kLoginHtml[] =
R"ASSET(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Sign In</title>
  <link rel="stylesheet" href="/css/base.css">
  <link rel="stylesheet" href="{{THEME_PATH}}/style.css">
</head>
<body>
  <div id="login-page">
    <div class="login-card">
      <h1>Welcome back</h1>
      <p class="login-subtitle">Sign in to continue</p>
      <form id="login-form" autocomplete="on" method="POST" action="/login">
        <div class="form-group">
          <label for="username">Username</label>
          <input type="text" id="username" name="username"
                 autocomplete="username" autofocus required>
        </div>
        <div class="form-group">
          <label for="password">Password</label>
          <input type="password" id="password" name="password"
                 autocomplete="current-password" required>
        </div>
        <label class="checkbox-row" for="remember-me">
          <input type="checkbox" id="remember-me" name="remember_me" value="true">
          <span>Remember me</span>
        </label>
        <button type="submit" class="login-btn" id="login-btn">Sign In</button>
        <div id="login-error"></div>
      </form>
    </div>
  </div>

  <script src="/js/login.js"></script>
</body>
</html>
)ASSET";

const char kChangePasswordHtml[] =
R"ASSET(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Change Password</title>
  <link rel="stylesheet" href="/css/base.css">
  <link rel="stylesheet" href="{{THEME_PATH}}/style.css">
</head>
<body>
  <div id="login-page">
    <div class="login-card">
      <h1>Change Password</h1>
      <p class="login-subtitle" id="subtitle">
        You must set a new password before continuing.
      </p>
      <form id="cp-form" autocomplete="off" method="POST" action="/api/change-password">
        <div class="form-group" id="current-group">
          <label for="current-password">Current Password</label>
          <input type="password" id="current-password" name="current-password"
                 autocomplete="current-password">
        </div>
        <div class="form-group">
          <label for="new-password">New Password</label>
          <input type="password" id="new-password" name="new-password"
                 autocomplete="new-password" autofocus required
                 placeholder="Minimum 10 characters">
        </div>
        <div class="form-group">
          <label for="confirm-password">Confirm New Password</label>
          <input type="password" id="confirm-password" name="confirm-password"
                 autocomplete="new-password" required>
        </div>
        <button type="submit" class="login-btn" id="cp-btn">Update Password</button>
        <div id="cp-error"></div>
      </form>
    </div>
  </div>

  <script src="/js/change-password.js"></script>
</body>
</html>
)ASSET";

const char kBaseCss[] =
R"ASSET(
/* ─────────────────────────────────────────────────────────────────────────
   base.css — Structural layout only.
   All colours, fonts, and radii are CSS custom properties defined by the
   active theme's style.css.  This file contains NO hardcoded values.
   ───────────────────────────────────────────────────────────────────────── */

*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

html, body {
  height: 100%;
  font-family: var(--font-body);
  font-size: var(--font-size-base);
  background: var(--color-bg-page);
  color: var(--color-text-primary);
  line-height: 1.5;
}

/* ── App shell ────────────────────────────────────────────────────────── */
#app {
  display: flex;
  flex-direction: column;
  height: 100vh;
}

/* ── Header ──────────────────────────────────────────────────────────── */
#header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 16px;
  height: 48px;
  background: var(--color-bg-header);
  flex-shrink: 0;
}
#header-title {
  color: var(--color-text-sidebar-active);
  font-size: var(--font-size-large);
  font-weight: 600;
  letter-spacing: 0.01em;
}
#header-user {
  display: flex;
  align-items: center;
  gap: 12px;
  color: var(--color-text-sidebar);
  font-size: var(--font-size-small);
}
#header-account-btn {
  appearance: none;
  border: none;
  background: transparent;
  color: var(--color-text-sidebar);
  font: inherit;
  cursor: pointer;
  padding: 0;
  text-decoration: underline;
}
#header-account-btn:hover {
  color: var(--color-text-sidebar-active);
}
#header-user a { color: var(--color-text-sidebar); text-decoration: underline; cursor: pointer; }

/* ── Body row (sidebar + main) ────────────────────────────────────────── */
#body-row {
  display: flex;
  flex: 1;
  overflow: hidden;
}

/* ── Sidebar ──────────────────────────────────────────────────────────── */
#sidebar {
  width: 240px;
  background: var(--color-bg-sidebar);
  display: flex;
  flex-direction: column;
  overflow-y: auto;
  flex-shrink: 0;
}
#sidebar-header {
  padding: 12px 16px 8px;
  font-size: var(--font-size-small);
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  color: var(--color-text-muted);
}
.project-item {
  border-bottom: 1px solid rgba(255,255,255,0.04);
}
.project-label {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 16px;
  cursor: pointer;
  color: var(--color-text-sidebar);
  font-weight: 500;
  user-select: none;
}
.project-label:hover { background: var(--color-bg-sidebar-hover); }
.project-arrow { font-size: 10px; transition: transform 0.15s; }
.project-arrow.open { transform: rotate(90deg); }
.chat-list { display: none; padding: 0 0 4px 0; }
.chat-list.open { display: block; }
.chat-entry {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 6px 16px 6px 28px;
  cursor: pointer;
  color: var(--color-text-sidebar);
  font-size: var(--font-size-small);
  border-radius: 0;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.chat-entry:hover { background: var(--color-bg-sidebar-hover); }
.chat-entry.active { background: var(--color-bg-sidebar-active);
                     color: var(--color-text-sidebar-active); }
.chat-delete-btn {
  background: none; border: none; cursor: pointer; padding: 0 2px;
  color: var(--color-text-muted); font-size: 12px; opacity: 0;
  transition: opacity 0.1s;
}
.chat-entry:hover .chat-delete-btn { opacity: 1; }
.chat-delete-btn:hover { color: var(--color-accent-danger); }

#new-chat-btn {
  margin: 10px 12px;
  padding: 7px 0;
  background: var(--color-bg-button-primary);
  color: var(--color-text-button);
  border: none; border-radius: var(--radius-button);
  cursor: pointer; font-size: var(--font-size-small); font-weight: 500;
  transition: background 0.15s;
}
#new-chat-btn:hover { background: var(--color-bg-button-hover); }
#new-chat-btn:disabled { opacity: 0.4; cursor: default; }

/* ── Main area ────────────────────────────────────────────────────────── */
#main {
  flex: 1;
  display: flex;
  flex-direction: column;
  background: var(--color-bg-main);
  overflow: hidden;
}
#chat-title {
  padding: 12px 20px;
  font-weight: 600;
  border-bottom: 1px solid var(--color-border-main);
  color: var(--color-text-secondary);
  font-size: var(--font-size-small);
  flex-shrink: 0;
}

/* ── Message area ─────────────────────────────────────────────────────── */
#messages {
  flex: 1;
  overflow-y: auto;
  padding: 20px;
  display: flex;
  flex-direction: column;
  gap: 16px;
}
.message-row {
  display: flex;
  flex-direction: column;
  max-width: 820px;
  width: 100%;
}
.message-row.user { align-self: flex-end; align-items: flex-end; }
.message-row.model { align-self: flex-start; align-items: flex-start; }
.message-row.file { align-self: stretch; align-items: stretch; max-width: 920px; }
.message-row.web-debug { align-self: stretch; align-items: stretch; max-width: 920px; }
.message-role-label {
  font-size: 11px;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0;
  color: var(--color-text-muted);
  margin-bottom: 4px;
}
.message-bubble {
  padding: 12px 16px;
  border-radius: var(--radius-message);
  line-height: 1.6;
  word-break: break-word;
}
.message-row.user  .message-bubble { background: var(--color-bg-message-user); }
.message-row.model .message-bubble { background: var(--color-bg-message-model);
  border: 1px solid var(--color-border-main);
  box-shadow: 0 1px 3px rgba(0,0,0,0.06); }
.message-row.model .message-bubble.diagram-only {
  width: 100%;
  max-width: 100%;
}

/* Markdown content inside model bubble */
.message-bubble h1,.message-bubble h2,.message-bubble h3,
.message-bubble h4,.message-bubble h5,.message-bubble h6 {
  margin: 0.8em 0 0.3em; font-weight: 600; line-height: 1.3;
}
.message-bubble h1 { font-size: 1.3em; } .message-bubble h2 { font-size: 1.15em; }
.message-bubble h3 { font-size: 1.05em; }
.message-bubble p  { margin: 0.5em 0; }
.message-bubble ul,.message-bubble ol { margin: 0.5em 0 0.5em 1.4em; }
.message-bubble li { margin: 0.2em 0; }
.message-bubble table { border-collapse: collapse; width: 100%; margin: 0.6em 0; font-size: 0.9em; }
.message-bubble th,.message-bubble td { border: 1px solid var(--color-border-main);
  padding: 5px 10px; text-align: left; }
.message-bubble th { background: var(--color-bg-tool-call); font-weight: 600; }
.message-bubble a  { color: var(--color-text-link); }
.message-bubble blockquote {
  border-left: 3px solid var(--color-accent-thinking);
  margin: 0.5em 0; padding: 0 0 0 12px;
  color: var(--color-text-secondary); font-style: italic;
}
.message-bubble pre {
  background: var(--color-bg-code);
  color: var(--color-text-code);
  border-radius: var(--radius-card);
  padding: 12px 16px;
  overflow-x: auto;
  margin: 0.6em 0;
  font-family: var(--font-mono);
  font-size: 0.85em;
  line-height: 1.5;
}
.message-bubble code:not(pre code) {
  font-family: var(--font-mono);
  font-size: 0.85em;
  background: var(--color-bg-tool-call);
  padding: 1px 5px;
  border-radius: 3px;
}

/* Thinking block */
.thinking-block {
  background: var(--color-bg-thinking);
  border-left: 3px solid var(--color-accent-thinking);
  border-radius: var(--radius-card);
  margin-bottom: 8px;
  font-size: var(--font-size-small);
  overflow: hidden;
}
.thinking-block summary {
  padding: 8px 12px;
  cursor: pointer;
  color: var(--color-text-thinking);
  font-weight: 600;
  list-style: none;
  display: flex;
  align-items: center;
  gap: 6px;
}
.thinking-block summary::before { content: "▶"; font-size: 9px; }
.thinking-block[open] summary::before { content: "▼"; }
.thinking-block .thinking-content {
  padding: 0 12px 10px;
  color: var(--color-text-thinking);
  white-space: pre-wrap;
  font-family: var(--font-mono);
  font-size: var(--font-size-small);
  line-height: 1.5;
}

.thinking-block summary::before { content: ">"; font-size: 10px; }
.thinking-block[open] summary::before { content: "v"; }
.thinking-block.thinking-live {
  box-shadow: inset 0 0 0 1px var(--color-accent-thinking);
}
.thinking-block.thinking-live summary::after {
  content: "";
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: var(--color-accent-thinking);
  animation: blink 0.9s ease-in-out infinite;
}

/* Diagrams and charts */
.diagram-block {
  width: 100%;
  overflow-x: auto;
  margin: 0.75em 0;
  padding: 10px;
  border: 1px solid var(--color-border-main);
  border-radius: var(--radius-card);
  background: var(--color-bg-main);
}
.diagram-block svg,
.diagram-block canvas {
  display: block;
  max-width: 100%;
}
.svg-diagram > svg {
  width: 100%;
  max-width: 100%;
  height: auto;
}
.diagram-error {
  color: var(--color-text-error);
}
.diagram-error pre {
  margin-top: 8px;
}
.cytoscape-host {
  width: 100%;
  min-height: 420px;
}
.message-row.context-usage {
  align-self: flex-start;
  align-items: flex-start;
}
.message-row.context-usage .message-role-label {
  margin: 0;
  height: 0;
  overflow: hidden;
}
.context-usage-bubble {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 6px 10px;
  border: 1px solid var(--color-border-main);
  border-radius: 999px;
  background: var(--color-bg-message-model);
  color: var(--color-text-muted);
  font-size: 12px;
  line-height: 1.2;
  box-shadow: 0 1px 2px rgba(0,0,0,0.04);
}
.context-usage-prefix {
  color: var(--color-text-muted);
  font-weight: 600;
}
.message-row.context-usage.is-pending {
  display: none;
}

.message-row.compression-status {
  align-self: flex-start;
  align-items: flex-start;
}
.message-row.compression-status .message-role-label {
  margin: 0;
  height: 0;
  overflow: hidden;
}
.compression-status-bubble {
  display: inline-flex;
  align-items: center;
  gap: 10px;
  padding: 10px 12px;
  border: 1px solid var(--color-border-main);
  border-radius: var(--radius-card);
  background: var(--color-bg-tool-call);
  color: var(--color-text-secondary);
  font-size: 12px;
  line-height: 1.35;
  box-shadow: 0 1px 2px rgba(0,0,0,0.04);
}
.compression-status-icon {
  width: 16px;
  height: 16px;
  flex: 0 0 16px;
  border-radius: 50%;
}
.compression-status-icon.status-live {
  width: 10px;
  height: 10px;
  border: none;
  background-color: #8da1b9;
  border-radius: 50%;
  animation: compression-pulse 1.2s ease-in-out infinite;
}
@keyframes compression-pulse {
  0% { opacity: 1; transform: scale(1); }
  50% { opacity: 0.4; transform: scale(0.8); }
  100% { opacity: 1; transform: scale(1); }
}
.compression-status-icon.status-done {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  border: 2px solid var(--color-accent-success, #16a34a);
  color: var(--color-accent-success, #16a34a);
  font-size: 11px;
  font-weight: 700;
  line-height: 1;
}
.compression-status-bubble.status-live .compression-status-text {
  color: var(--color-text-secondary);
}
.compression-status-bubble.status-done .compression-status-text {
  color: var(--color-text-primary);
}

.message-row.provider-queue-status {
  align-self: flex-start;
  align-items: flex-start;
}
.message-row.provider-queue-status .message-role-label {
  margin: 0;
  height: 0;
  overflow: hidden;
}
.provider-queue-status-bubble {
  display: inline-flex;
  align-items: center;
  gap: 10px;
  padding: 10px 12px;
  border: 1px solid var(--color-border-main);
  border-radius: var(--radius-card);
  background: var(--color-bg-tool-call);
  color: var(--color-text-secondary);
  font-size: 12px;
  line-height: 1.35;
  box-shadow: 0 1px 2px rgba(0,0,0,0.04);
}
.pro)ASSET"
R"ASSET(
vider-queue-status-icon {
  width: 16px;
  height: 16px;
  flex: 0 0 16px;
  border-radius: 50%;
}
.provider-queue-status-icon.status-live {
  border: 2px solid var(--color-accent-primary);
  border-top-color: transparent;
  animation: compression-spin 0.8s linear infinite;
}
.provider-queue-status-icon.status-done {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  border: 2px solid var(--color-accent-success, #16a34a);
  color: var(--color-accent-success, #16a34a);
  font-size: 11px;
  font-weight: 700;
  line-height: 1;
}
.provider-queue-status-bubble.status-live .provider-queue-status-text {
  color: var(--color-text-secondary);
}
.provider-queue-status-bubble.status-done .provider-queue-status-text {
  color: var(--color-text-primary);
}

.message-row.provider-activity-status {
  align-self: flex-start;
  align-items: flex-start;
}
.message-row.provider-activity-status .message-role-label {
  margin: 0;
  height: 0;
  overflow: hidden;
}
.provider-activity-status-bubble {
  display: inline-flex;
  align-items: center;
  gap: 10px;
  padding: 10px 12px;
  border: 1px solid var(--color-border-main);
  border-radius: var(--radius-card);
  background: var(--color-bg-tool-call);
  color: var(--color-text-secondary);
  font-size: 12px;
  line-height: 1.35;
  box-shadow: 0 1px 2px rgba(0,0,0,0.04);
}
.provider-activity-status-icon {
  width: 16px;
  height: 16px;
  flex: 0 0 16px;
  border-radius: 50%;
}
.provider-activity-status-icon.status-live {
  border: 2px solid var(--color-accent-primary);
  border-top-color: transparent;
  animation: compression-spin 0.8s linear infinite;
}
.provider-activity-status-icon.status-done {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  border: 2px solid var(--color-accent-success, #16a34a);
  color: var(--color-accent-success, #16a34a);
  font-size: 11px;
  font-weight: 700;
  line-height: 1;
}
.provider-activity-status-bubble.status-live .provider-activity-status-text {
  color: var(--color-text-secondary);
}
.provider-activity-status-bubble.status-done .provider-activity-status-text {
  color: var(--color-text-primary);
}

.message-row.tool-usage-status {
  align-self: flex-start;
  align-items: flex-start;
  max-width: 920px;
}
.tool-usage-block {
  width: 100%;
  border: 1px solid var(--color-border-main);
  border-left: 3px solid var(--color-accent-primary);
  border-radius: var(--radius-card);
  background: var(--color-bg-tool-call);
  box-shadow: 0 1px 2px rgba(0,0,0,0.04);
  overflow: hidden;
}
.tool-usage-block.status-done {
  border-left-color: var(--color-accent-success);
}
.tool-usage-block.status-error {
  border-left-color: var(--color-accent-danger);
}
.tool-usage-block summary {
  list-style: none;
  display: flex;
  align-items: center;
  gap: 10px;
  cursor: pointer;
  padding: 10px 12px;
  color: var(--color-text-primary);
  font-size: 12px;
  font-weight: 600;
}
.tool-usage-block summary::-webkit-details-marker {
  display: none;
}
.tool-usage-block summary::before {
  content: ">";
  font-size: 10px;
  color: var(--color-text-muted);
}
.tool-usage-block[open] summary::before {
  content: "v";
}
.tool-usage-icon {
  width: 16px;
  height: 16px;
  flex: 0 0 16px;
  border-radius: 50%;
}
.tool-usage-icon.status-live {
  border: 2px solid var(--color-accent-primary);
  border-top-color: transparent;
  animation: compression-spin 0.8s linear infinite;
}
.tool-usage-icon.status-done {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  border: 2px solid var(--color-accent-success);
  color: var(--color-accent-success);
  font-size: 11px;
  font-weight: 700;
  line-height: 1;
}
.tool-usage-icon.status-error {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  border: 2px solid var(--color-accent-danger);
  color: var(--color-accent-danger);
  font-size: 11px;
  font-weight: 700;
  line-height: 1;
}
.tool-usage-block.status-live .tool-usage-summary-text {
  color: var(--color-text-secondary);
}
.tool-usage-block.status-error .tool-usage-summary-text {
  color: var(--color-accent-danger);
}
.tool-usage-content {
  padding: 0 12px 12px;
}
.tool-usage-section + .tool-usage-section {
  margin-top: 12px;
}
.tool-usage-section-label {
  font-size: 11px;
  font-weight: 600;
  color: var(--color-text-muted);
  text-transform: uppercase;
  margin-bottom: 6px;
}
.tool-usage-pre {
  margin: 0;
  white-space: pre-wrap;
  word-break: break-word;
  background: var(--color-bg-main);
  color: var(--color-text-primary);
  border: 1px solid var(--color-border-main);
  border-radius: 6px;
  padding: 10px;
  font-family: var(--font-mono);
  font-size: 12px;
  line-height: 1.5;
}
.assistant-trace-bubble {
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.assistant-bubble-segment {
  width: 100%;
}
.assistant-bubble-text:empty {
  display: none;
}

/* Streaming indicator */
.streaming-cursor {
  display: inline-block;
  width: 8px; height: 1em;
  background: var(--color-accent-primary);
  vertical-align: text-bottom;
  animation: blink 0.7s step-end infinite;
}
@keyframes blink { 50% { opacity: 0; } }
@keyframes compression-spin {
  to { transform: rotate(360deg); }
}

/* Error message */
.message-row.error .message-bubble {
  background: #fef2f2;
  border: 1px solid #fecaca;
  color: var(--color-text-error);
}

.web-debug-bubble {
  width: 100%;
  padding: 12px 14px;
  border: 1px dashed var(--color-border-main);
  border-radius: var(--radius-card);
  background: var(--color-bg-tool-call);
  color: var(--color-text-primary);
  font-size: var(--font-size-small);
}
.web-debug-title {
  font-weight: 700;
  color: var(--color-text-secondary);
  margin-bottom: 8px;
}
.web-debug-section + .web-debug-section { margin-top: 8px; }
.web-debug-section summary {
  cursor: pointer;
  font-weight: 700;
  color: var(--color-accent-primary);
  user-select: none;
}
.web-debug-section pre {
  margin-top: 6px;
  max-height: 260px;
  overflow: auto;
  white-space: pre-wrap;
  word-break: break-word;
  background: var(--color-bg-main);
  border: 1px solid var(--color-border-main);
  border-radius: 6px;
  padding: 10px;
  font-family: var(--font-mono);
  font-size: 12px;
  line-height: 1.45;
}

/* ── File upload timeline rows ────────────────────────────────────────── */
.file-upload-card {
  --file-upload-active: var(--color-accent-primary);
  display: flex;
  gap: 12px;
  width: 100%;
  padding: 14px 16px;
  border: 1px solid var(--color-border-main);
  border-radius: var(--radius-card);
  background: var(--color-bg-message-model);
  box-shadow: 0 1px 3px rgba(0,0,0,0.06);
}
.file-upload-card.status-done { --file-upload-active: var(--color-accent-success, #16a34a); }
.file-upload-card.status-warning { --file-upload-active: var(--color-accent-warning, #d97706); }
.file-upload-card.status-error { --file-upload-active: var(--color-accent-danger); }
.file-upload-status-mark {
  width: 36px;
  height: 36px;
  border: 2px solid var(--file-upload-active);
  border-radius: 8px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  flex: 0 0 36px;
  color: var(--file-upload-active);
  font-weight: 700;
  font-size: 18px;
  line-height: 1;
  background: var(--color-bg-main);
}
.file-upload-status-mark.uploading {
  animation: file-upload-bob 1s ease-in-out infinite;
}
.file-upload-status-mark.ingesting::before {
  content: '';
  width: 18px;
  height: 18px;
  border: 3px solid var(--file-upload-active);
  border-top-color: transparent;
  border-radius: 50%;
  animation: file-upload-spin 0.8s linear infinite;
}
.file-upload-body {
  min-width: 0;
  flex: 1;
}
.file-upload-top {
  display: flex;
  gap: 12px;
  align-items: flex-start;
  justify-content: space-between;
}
.file-upload-name {
  min-width: 0;
  overflow-wrap: anywhere;
  font-weight: 600;
}
.file-upload-status {
  flex: 0 0 auto;
  color: var(--file-upload-active);
  font-size: var(--font-size-small);
  font-weight: 700;
}
.file-upload-meta,
.file-upload-detail,
.file-upload-links {
  margin-top: 4px;
  color: var(--color-text-secondary);
  font-size: var(--font-size-small);
  overflow-wrap: anywhere;
}
.file-upload-detail { color: var(--color-text-primary); }
.file-upload-links {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
}
.file-upload-links a {
  color: var(--color-text-link);
  font-weight: 600;
}
.file-upload-progress {
  position: relative;
  overflow: hidden;
  height: 7px;
  margin-top: 10px;
  border-radius: 7px;
  background: var(--color-bg-tool-call);
}
.file-upload-progress span {
  position: absolute;
  inset: 0;
  width: 45%;
  border-radius: inherit;
  background: var(--file-upload-active);
  animation: file-upload-progress 1.1s ease-in-out infinite;
}
@keyframes file-upload-spin {
  to { transform: rotate(360deg); }
}
@keyframes file-upload-bob {
  0%, 100% { transform: translateY(0); }
  50% { transform: translateY(-2px); }
}
@keyframes file-upload-progress {
  0% { transform: translateX(-110%); }
  100% { transform: translateX(240%); }
}
@media (prefers-reduced-motion: reduce) {
  .file-upload-status-mark.uploading,
  .file-upload-status-mark.ingesting::before,
  .file-upload-progress span {
    animation: none;
  }
}

/* ── Attach bar ───────────────────────────────────────────────────────── */
#attach-bar {
  padding: 6px 20px 0;
  background: var(--color-bg-main);
  border-top: 1px solid var(--color-border-subtle);
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  flex-shrink: 0;
}
.attach-chip {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 2px 8px;
  background: var(--color-bg-tool-call);
  border-radius: 12px;
  font-size: var(--font-size-small);
  color: var(--color-text-secondary);
}
.attach-chip button {
  background: none; border: none; cursor: pointer;
  color: var(--color-text-muted); padding: 0; line-height: 1;
  font-size: 10px;
}
.attach-chip button:hover { color: var(--color-accent-danger); }
.attach-chip.uploading { opacity: 0.6; }
.attach-chip-uploading { color: var(--color-accent-primary); }
.attach-chip-ingesting { color: var(--color-accent-primary); }
.attach-chip-ingesting::before {
  content: '';
  display: inline-block;
  width: 12px;
  height: 12px;
  border: 2px solid var(--color-accent-primary);
  border-top-color: transparent;
  border-radius: 50%;
  animation: spin 0.8s linear infinite;
  margin-right: 2px;
  vertical-align: middle;
}
.attach-chip-done { color: var(--color-accent-success, #16a34a); }
.attach-chip-warning { color: var(--color-accent-warning, #d97706); }
.attach-chip-error { color: var(--color-accent-danger); }
.attach-chip-pending { color: var(--color-text-secondary); }
@keyframes spin { to { transform: rotate(360deg); } }

/* ── Compose bar ──────────────────────────────────────────────────────── */
#compose {
  padding: 4px 20px 16px;
  border-top: 1px solid var(--color-border-main);
  display: flex;
  flex-direction: column;
  gap: 4px;
  flex-shrink: 0;
  background: var(--color-bg-main);
}
#compose-top {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  min-height: 22px;
}
#compose-top-left {
  display: flex;
  align-items: center;
  min-width: 0;
}
#compose-bottom {
  display: flex;
  gap: 10px;
  align-items: flex-end;
}
#agentic-mode-label {
  font-size: var(--font-size-small);
  font-weight: 600;
  cursor: pointer;
  user-select: none;
  padding: 2px 8px;
  border-radius: var(--radius-button);
  line-height: 1.3;
}
.agentic-mode-none {
  color: var(--color-accent-danger);
  background: rgba(220,53,69,0.08);
}
.agentic-mode-active {
  color: var(--)ASSET"
R"ASSET(
color-accent-primary);
  background: rgba(0,150,255,0.08);
}
.agentic-mode-active.disabled {
  color: var(--color-text-secondary);
  background: transparent;
  cursor: default;
}
#debug-btn {
  border: 1px solid var(--color-border-input);
  border-radius: var(--radius-button);
  background: var(--color-bg-input);
  color: var(--color-text-secondary);
  cursor: pointer;
  font-size: var(--font-size-small);
  font-weight: 600;
  padding: 4px 10px;
  white-space: nowrap;
}
#debug-btn.active {
  border-color: var(--color-accent-primary);
  color: var(--color-accent-primary);
  background: rgba(0,150,255,0.08);
}
#debug-btn:disabled {
  opacity: 0.45;
  cursor: default;
}
#attach-btn {
  flex-shrink: 0;
  width: 36px; height: 44px;
  background: none;
  border: 1px solid var(--color-border-input);
  border-radius: var(--radius-button);
  cursor: pointer;
  font-size: 16px;
  color: var(--color-text-muted);
  transition: color 0.15s, border-color 0.15s;
}
#attach-btn:hover { color: var(--color-accent-primary); border-color: var(--color-border-focus); }
#attach-btn:disabled { opacity: 0.35; cursor: default; }
#message-input {
  flex: 1;
  padding: 10px 14px;
  border: 1px solid var(--color-border-input);
  border-radius: var(--radius-input);
  font-family: var(--font-body);
  font-size: var(--font-size-base);
  background: var(--color-bg-input);
  color: var(--color-text-primary);
  resize: none;
  min-height: 44px;
  line-height: 1.5;
  transition: border-color 0.15s;
  outline: none;
}
#message-input:focus { border-color: var(--color-border-focus); }
#message-input::placeholder { color: var(--color-text-muted); }
#send-btn {
  padding: 10px 20px;
  background: var(--color-bg-button-primary);
  color: var(--color-text-button);
  border: none; border-radius: var(--radius-button);
  cursor: pointer; font-weight: 600; font-size: var(--font-size-base);
  white-space: nowrap;
  transition: background 0.15s;
  height: 44px;
}
#send-btn:hover { background: var(--color-bg-button-hover); }
#send-btn:disabled { opacity: 0.45; cursor: default; }

/* ── Empty state ──────────────────────────────────────────────────────── */
#empty-state {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  color: var(--color-text-muted);
  text-align: center;
  padding: 40px;
  gap: 8px;
}
#empty-state .empty-icon { font-size: 40px; opacity: 0.4; }
#empty-state p { font-size: var(--font-size-small); max-width: 280px; line-height: 1.6; }

/* ── Login page ───────────────────────────────────────────────────────── */
#login-page {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--color-bg-page);
}
.login-card {
  background: white;
  border-radius: var(--radius-card);
  box-shadow: 0 4px 24px rgba(0,0,0,0.08);
  padding: 40px;
  width: 100%;
  max-width: 380px;
}
.login-card h1 {
  font-size: 1.4em;
  font-weight: 700;
  color: var(--color-text-primary);
  margin-bottom: 6px;
}
.login-card .login-subtitle {
  color: var(--color-text-muted);
  font-size: var(--font-size-small);
  margin-bottom: 28px;
}
.form-group { margin-bottom: 16px; }
.form-group label {
  display: block;
  font-size: var(--font-size-small);
  font-weight: 500;
  color: var(--color-text-secondary);
  margin-bottom: 5px;
}
.form-group input {
  width: 100%;
  padding: 9px 12px;
  border: 1px solid var(--color-border-input);
  border-radius: var(--radius-input);
  font-family: var(--font-body);
  font-size: var(--font-size-base);
  background: var(--color-bg-input);
  color: var(--color-text-primary);
  outline: none;
  transition: border-color 0.15s;
}
.form-group input:focus { border-color: var(--color-border-focus); }
.checkbox-row {
  display: inline-flex;
  align-items: center;
  gap: 10px;
  margin: 2px 0 12px;
  color: var(--color-text-secondary);
  font-size: var(--font-size-small);
  user-select: none;
}
.checkbox-row input {
  width: auto;
  margin: 0;
}
.app-modal[hidden] { display: none; }
.app-modal {
  position: fixed;
  inset: 0;
  z-index: 1000;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 20px;
}
.app-modal-backdrop {
  position: absolute;
  inset: 0;
  background: rgba(15, 23, 42, 0.45);
}
.app-modal-panel {
  position: relative;
  width: min(100%, 460px);
  max-height: calc(100vh - 40px);
  overflow-y: auto;
  background: var(--color-bg-main);
  border: 1px solid var(--color-border-main);
  border-radius: var(--radius-card);
  box-shadow: 0 18px 48px rgba(0,0,0,0.2);
  padding: 18px;
}
.app-modal-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  margin-bottom: 12px;
}
.app-modal-header h2 {
  font-size: 1.1rem;
  font-weight: 600;
  color: var(--color-text-primary);
}
.app-modal-close {
  appearance: none;
  border: none;
  background: transparent;
  color: var(--color-text-secondary);
  font-size: 22px;
  line-height: 1;
  cursor: pointer;
  padding: 0;
}
.account-form .form-group {
  margin-bottom: 14px;
}
.account-form-divider {
  margin: 18px 0 12px;
  padding-top: 14px;
  border-top: 1px solid var(--color-border-main);
  color: var(--color-text-secondary);
  font-size: var(--font-size-small);
  font-weight: 600;
}
.account-form-help {
  margin-top: -2px;
  margin-bottom: 14px;
  color: var(--color-text-muted);
  font-size: var(--font-size-small);
}
.app-modal-actions {
  display: flex;
  justify-content: flex-end;
  gap: 10px;
  margin-top: 4px;
}
.app-modal-actions .login-btn {
  width: auto;
  margin-top: 0;
  padding: 10px 14px;
}
.secondary-btn {
  border: 1px solid var(--color-border-main);
  background: var(--color-bg-input);
  color: var(--color-text-primary);
  border-radius: var(--radius-button);
  padding: 10px 14px;
  font: inherit;
  cursor: pointer;
}
.secondary-btn:hover {
  background: var(--color-bg-sidebar-hover);
}
#account-error {
  margin-top: 14px;
  padding: 9px 12px;
  background: #fef2f2;
  border-radius: var(--radius-input);
  color: var(--color-text-error);
  font-size: var(--font-size-small);
  display: none;
}
.login-btn {
  width: 100%;
  padding: 10px;
  background: var(--color-bg-button-primary);
  color: var(--color-text-button);
  border: none; border-radius: var(--radius-button);
  font-size: var(--font-size-base); font-weight: 600;
  cursor: pointer; margin-top: 8px;
  transition: background 0.15s;
}
.login-btn:hover { background: var(--color-bg-button-hover); }
.login-btn:disabled { opacity: 0.5; cursor: default; }
#login-error,
#cp-error {
  margin-top: 14px;
  padding: 9px 12px;
  background: #fef2f2;
  border-radius: var(--radius-input);
  color: var(--color-text-error);
  font-size: var(--font-size-small);
  display: none;
}
)ASSET";

const char kLoginJs[] =
R"ASSET(
'use strict';

document.getElementById('login-form').addEventListener('submit', async function(e) {
  e.preventDefault();

  const btn = document.getElementById('login-btn');
  const errEl = document.getElementById('login-error');
  const username = document.getElementById('username').value.trim();
  const password = document.getElementById('password').value;
  const rememberMe = !!document.getElementById('remember-me')?.checked;

  btn.disabled = true;
  btn.textContent = 'Signing in...';
  errEl.style.display = 'none';

  try {
    const resp = await fetch('/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username, password, remember_me: rememberMe }),
    });
    const data = await resp.json();

    if (resp.ok) {
      if (data.force_password_reset) {
        sessionStorage.setItem('force_password_reset', 'true');
        window.location.href = '/change-password';
      } else {
        window.location.href = '/';
      }
    } else {
      errEl.textContent = data.error || 'Login failed.';
      errEl.style.display = 'block';
      btn.disabled = false;
      btn.textContent = 'Sign In';
    }
  } catch (err) {
    errEl.textContent = 'Network error - is the server running?';
    errEl.style.display = 'block';
    btn.disabled = false;
    btn.textContent = 'Sign In';
  }
});
)ASSET";

const char kChangePasswordJs[] =
R"ASSET(
'use strict';

let forced = sessionStorage.getItem('force_password_reset') === 'true';
if (forced) {
  document.getElementById('current-group').style.display = 'none';
  document.getElementById('subtitle').textContent =
    'Please set a new password before continuing.';
  sessionStorage.removeItem('force_password_reset');
}

document.getElementById('cp-form').addEventListener('submit', async function(e) {
  e.preventDefault();

  const btn = document.getElementById('cp-btn');
  const errEl = document.getElementById('cp-error');
  const current = document.getElementById('current-password').value;
  const newPw = document.getElementById('new-password').value;
  const confirm = document.getElementById('confirm-password').value;

  errEl.style.display = 'none';

  if (newPw.length < 10) {
    errEl.textContent = 'Password must be at least 10 characters.';
    errEl.style.display = 'block';
    return;
  }
  if (newPw !== confirm) {
    errEl.textContent = 'Passwords do not match.';
    errEl.style.display = 'block';
    return;
  }

  btn.disabled = true;
  btn.textContent = 'Updating...';

  try {
    const body = { new_password: newPw };
    if (!forced) body.current_password = current;

    const resp = await fetch('/api/change-password', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      credentials: 'same-origin',
      body: JSON.stringify(body),
    });
    const data = await resp.json();

    if (resp.ok) {
      window.location.href = '/';
    } else {
      errEl.textContent = data.error || 'Failed to update password.';
      errEl.style.display = 'block';
      btn.disabled = false;
      btn.textContent = 'Update Password';
    }
  } catch (err) {
    errEl.textContent = 'Network error - please try again.';
    errEl.style.display = 'block';
    btn.disabled = false;
    btn.textContent = 'Update Password';
  }
});
)ASSET";

const char kAppJs[] =
R"ASSET(
/* ─────────────────────────────────────────────────────────────────────────
   app.js — Web chat application
   Vanilla JS, no framework.  Uses Server-Sent Events for streaming responses.
   ───────────────────────────────────────────────────────────────────────── */

'use strict';

// ── Configure marked.js ───────────────────────────────────────────────────
marked.setOptions({
  highlight: function(code, lang) {
    if (lang && hljs.getLanguage(lang)) {
      try { return hljs.highlight(code, { language: lang }).value; } catch (_) {}
    }
    return hljs.highlightAuto(code).value;
  },
  breaks: true,
  gfm: true,
});

if (window.mermaid) {
  mermaid.initialize({
    startOnLoad: false,
    securityLevel: 'strict',
    theme: 'default',
    flowchart: {
      htmlLabels: false,
    },
  });
}

let diagramRenderId = 0;

// ── State ─────────────────────────────────────────────────────────────────
let state = {
  projects:          [],
  selectedProjectId: null,
  selectedChatId:    null,
  chats:             {},
  messages:          [],
  sending:           false,
  username:          '',
  displayName:       '',
  email:             '',
  pendingFiles:      [],     // File objects queued for upload before send
  selectedChatAgenticModeId: null,
  projectAgenticModes:       [],
  projectDefaultAgenticModeId: '',
  projectEnabledAgenticModeIds: [],
  projectAllowManualCompress: false,
  projectEnableWebDebugging: false,
  webDebuggingActive: false,
};

// ── DOM refs ──────────────────────────────────────────────────────────────
const $ = id => document.getElementById(id);
const projectList  = $('project-list');
const newChatBtn   = $('new-chat-btn');
const chatTitle    = $('chat-title');
const messagesEl   = $('messages');
const emptyState   = $('empty-state');
const messageInput = $('message-input');
const sendBtn      = $('send-btn');
const headerUser   = $('header-username');
const headerAccountBtn = $('header-account-btn');
const agenticModeLabel = $('agentic-mode-label');
let   agenticModePicker = null;
const compressBtn      = $('compress-btn');
const debugBtn         = $('debug-btn');
const attachBtn    = $('attach-btn');
const fileInput    = $('file-input');
const attachList   = $('attach-list');
const accountModal = $('account-modal');
const accountForm = $('account-form');
const accountCloseBtn = $('account-close-btn');
const accountCancelBtn = $('account-cancel-btn');
const accountUsernameInput = $('account-username');
const accountDisplayNameInput = $('account-display-name');
const accountEmailInput = $('account-email');
const accountCurrentPasswordInput = $('account-current-password');
const accountNewPasswordInput = $('account-new-password');
const accountConfirmPasswordInput = $('account-confirm-password');
const accountSaveBtn = $('account-save-btn');
const accountError = $('account-error');

// ── API helpers ───────────────────────────────────────────────────────────
async function api(method, path, body) {
  const opts = {
    method,
    headers: { 'Content-Type': 'application/json' },
    credentials: 'same-origin',
  };
  if (body !== undefined) opts.body = JSON.stringify(body);
  const resp = await fetch(path, opts);
  if (resp.status === 401 || resp.status === 302) {
    window.location.href = '/login';
    return null;
  }
  return resp;
}

function setAccountError(message) {
  if (!accountError) return;
  if (!message) {
    accountError.style.display = 'none';
    accountError.textContent = '';
    return;
  }
  accountError.textContent = message;
  accountError.style.display = 'block';
}

function populateAccountForm() {
  if (!accountForm) return;
  accountUsernameInput.value = state.username || '';
  accountDisplayNameInput.value = state.displayName || '';
  accountEmailInput.value = state.email || '';
  accountCurrentPasswordInput.value = '';
  accountNewPasswordInput.value = '';
  accountConfirmPasswordInput.value = '';
  setAccountError('');
}

function openAccountModal() {
  if (!accountModal) return;
  populateAccountForm();
  accountModal.hidden = false;
  document.body.style.overflow = 'hidden';
  setTimeout(() => {
    if (accountDisplayNameInput) accountDisplayNameInput.focus();
  }, 0);
}

function closeAccountModal() {
  if (!accountModal) return;
  accountModal.hidden = true;
  document.body.style.overflow = '';
  setAccountError('');
}

async function saveAccountSettings(e) {
  e.preventDefault();
  if (!accountSaveBtn) return;

  setAccountError('');
  accountSaveBtn.disabled = true;
  accountSaveBtn.textContent = 'Saving...';

  const body = {
    display_name: accountDisplayNameInput.value.trim(),
    email: accountEmailInput.value.trim(),
    current_password: accountCurrentPasswordInput.value,
    new_password: accountNewPasswordInput.value,
    confirm_password: accountConfirmPasswordInput.value,
  };

  try {
    const resp = await api('PATCH', '/api/me', body);
    if (!resp) return;
    const data = await resp.json();
    if (!resp.ok) {
      setAccountError(data.error || 'Could not update the account.');
      return;
    }

    state.displayName = data.display_name || '';
    state.email = data.email || '';
    headerUser.textContent = state.displayName || state.username || '';
    closeAccountModal();
  } catch (err) {
    setAccountError('Network error while updating the account.');
  } finally {
    accountSaveBtn.disabled = false;
    accountSaveBtn.textContent = 'Save Changes';
  }
}

if (headerAccountBtn) {
  headerAccountBtn.addEventListener('click', openAccountModal);
}
if (accountCloseBtn) {
  accountCloseBtn.addEventListener('click', closeAccountModal);
}
if (accountCancelBtn) {
  accountCancelBtn.addEventListener('click', closeAccountModal);
}
if (accountForm) {
  accountForm.addEventListener('submit', saveAccountSettings);
}
if (accountModal) {
  accountModal.addEventListener('click', e => {
    if (e.target && e.target.dataset && e.target.dataset.closeAccountModal === 'true') {
      closeAccountModal();
    }
  });
}
window.addEventListener('keydown', e => {
  if (e.key === 'Escape' && accountModal && !accountModal.hidden) {
    closeAccountModal();
  }
});

// ── Markdown rendering ────────────────────────────────────────────────────
function renderMarkdown(text, options = {}) {
  const extracted = extractThinkingBlocks(text || '', !!options.streaming);
  const raw = marked.parse(extracted.markdown);
  let result = DOMPurify.sanitize(raw, {
    ADD_TAGS: ['details', 'summary'],
    ADD_ATTR: ['open'],
    FORBID_ATTR: ['onerror', 'onload'],
  });

  for (const block of extracted.blocks) {
    result = replacePlaceholder(result, block.placeholder,
      renderThinkingBlock(block.content, block.open, !!options.streaming));
  }
  return result;
}

function extractThinkingBlocks(text, streaming) {
  const blocks = [];
  let markdown = text.replace(/<think(?:ing)?>([\s\S]*?)<\/think(?:ing)?>/gi,
    function(_match, content) {
      const placeholder = 'THINKING_BLOCK_' + blocks.length + '_PLACEHOLDER';
      blocks.push({ placeholder, content: content.trim(), open: streaming });
      return '\n\n' + placeholder + '\n\n';
    });

  const openMatch = /<think(?:ing)?>/i.exec(markdown);
  if (openMatch) {
    const placeholder = 'THINKING_BLOCK_' + blocks.length + '_PLACEHOLDER';
    const contentStart = openMatch.index + openMatch[0].length;
    const content = markdown.slice(contentStart).replace(/<\/think(?:ing)?>/ig, '');
    blocks.push({ placeholder, content: content.trim(), open: true });
    markdown = markdown.slice(0, openMatch.index) + '\n\n' + placeholder + '\n\n';
  }

  markdown = markdown.replace(/<\/think(?:ing)?>/ig, '');
  return { markdown, blocks };
}

function renderThinkingBlock(content, open, streaming) {
  const classes = 'thinking-block' + (streaming ? ' thinking-live' : '');
  const state = streaming ? 'Thinking now' : 'Thinking';
  const body = content && content.trim() ? content.trim() : 'Thinking...';
  return '<details class="' + classes + '"' + (open ? ' open' : '') + '>' +
    '<summary>' + state + '</summary>' +
    '<div class="thinking-content">' + escapeHtml(body) + '</div>' +
    '</details>';
}

function replacePlaceholder(html, placeholder, replacement) {
  const escaped = escapeRegExp(placeholder);
  html = html.replace(new RegExp('<p>\\s*' + escaped + '\\s*</p>', 'g'), replacement);
  return html.replace(new RegExp(escaped, 'g'), replacement);
}

function postProcessMessageBubble(bubble, options = {}) {
  const renderDiagrams = options.renderDiagrams !== false;
  if (renderDiagrams) {
    renderMermaidBlocks(bubble);
    renderVegaBlocks(bubble);
    renderCytoscapeBlocks(bubble);
    renderSvgBlocks(bubble);
  }
  const hasDiagram = !!bubble.querySelector('.diagram-block');
  bubble.classList.toggle('has-diagram', hasDiagram);
  bubble.classList.toggle('diagram-only', hasDiagram && bubbleHasOnlyDiagrams(bubble));
  bubble.querySelectorAll('pre code:not(.hljs)').forEach(el => {
    if (!isDiagramLanguage(getCodeLanguage(el))) {
      hljs.highlightElement(el);
    }
  });
}

function bubbleHasOnlyDiagrams(bubble) {
  const meaningfulNodes = Array.from(bubble.childNodes).filter(node => {
    if (node.nodeType === Node.TEXT_NODE) {
      return !!node.textContent.trim();
    }
    if (node.nodeType !== Node.ELEMENT_NODE) {
      return false;
    }
    const el = node;
    return el.tagName !== 'BR';
  });
  return meaningfulNodes.length > 0 &&
    meaningfulNodes.every(node =>
      node.nodeType === Node.ELEMENT_NODE &&
      node.classList.contains('diagram-block'));
}

function getCodeLanguage(codeEl) {
  for (const cls of codeEl.classList) {
    if (cls.indexOf('language-') === 0) return cls.slice(9).toLowerCase();
    if (cls.indexOf('lang-') === 0) return cls.slice(5).toLowerCase();
  }
  return '';
}

function isDiagramLanguage(lang) {
  return lang === 'mermaid' || lang === 'vega-lite' ||
    lang === 'vegalite' || lang === 'vega' ||
    lang === 'cytoscape' || lang === 'cytoscapejs' ||
    lang === 'svg';
}

function renderMermaidBlocks(container) {
  if (!window.mermaid) return;
  container.querySelectorAll('pre code').forEach(codeEl => {
    if (getCodeLanguage(codeEl) !== 'mermaid') return;
    const pre = codeEl.closest('pre');
    if (!pre || pre.dataset.diagramRendered) return;
    pre.dataset.diagramRendered = '1';
    const source = codeEl.textContent.trim();
    if (!source) return;

    const host = document.createElement('div');
    host.className = 'diagram-block mermaid-diagram';
    host.textContent = 'Rendering Mermaid diagram...';
    pre.replaceWith(host);

    const id = 'mermaid-diagram-' + (++diagramRenderId);
    try {
      mermaid.render(id, source).then(result => {
        host.innerHTML = DOMPurify.sanitize(result.svg, {
          USE_PROFILES: { svg: true, svgFilters: true },
        });
      }).catch(err => showDiagramError(host, 'Mermaid', err, source));
    } catch (err) {
      showDiagramError(host, 'Mermaid', err, source);
    }
  });
}

function renderVegaBlocks(container) {
  if (!window.vegaEmbed) return;
  container.querySelectorAll('pre code').forEach(codeEl => {
    const lang = getCodeLanguage(codeEl);
    if (lang !== 'vega-lite' && lang !== 'vegalite' && lang !== 'vega') return;
    const pre = codeEl.closest('pre');
    if (!pre || pre.dataset.diagramRendered) return;
    pre.dataset.diagramRendered = '1';
    const source = codeEl.textContent.trim();
    if (!source) return;

    const host = document.createElement('div');
    host.className = 'diagram-block vega-diagram';
    host.textContent = 'Rendering chart...';
    pre.replaceWith(host);

    let spec;
    try {
      spec )ASSET"
R"ASSET(
= JSON.parse(source);
      if (spec && typeof spec === 'object' && spec.spec !== undefined &&
          (spec.type === 'vega-lite' || spec.type === 'vega' || spec.type === 'vegalite')) {
        spec = spec.spec;
      }
    } catch (err) {
      showDiagramError(host, 'Vega-Lite', err, source);
      return;
    }

    vegaEmbed(host, spec, { actions: false, renderer: 'svg' })
      .catch(err => showDiagramError(host, 'Vega-Lite', err, source));
  });
}

function defaultCytoscapeStyle() {
  return [
    {
      selector: 'node',
      style: {
        'background-color': '#2563eb',
        'label': 'data(label)',
        'color': '#f8fafc',
        'text-valign': 'center',
        'text-halign': 'center',
        'font-size': '12px',
        'text-wrap': 'wrap',
        'text-max-width': '120px',
        'border-width': 1,
        'border-color': '#1d4ed8',
        'width': 'label',
        'height': 'label',
        'padding': '14px',
        'shape': 'round-rectangle',
      },
    },
    {
      selector: 'edge',
      style: {
        'curve-style': 'bezier',
        'target-arrow-shape': 'triangle',
        'line-color': '#94a3b8',
        'target-arrow-color': '#94a3b8',
        'width': 2,
        'arrow-scale': 1,
        'label': 'data(label)',
        'font-size': '11px',
        'text-background-color': '#ffffff',
        'text-background-opacity': 0.85,
        'text-background-padding': '2px',
      },
    },
  ];
}

function renderCytoscapeBlocks(container) {
  if (!window.cytoscape) return;
  container.querySelectorAll('pre code').forEach(codeEl => {
    const lang = getCodeLanguage(codeEl);
    if (lang !== 'cytoscape' && lang !== 'cytoscapejs') return;
    const pre = codeEl.closest('pre');
    if (!pre || pre.dataset.diagramRendered) return;
    pre.dataset.diagramRendered = '1';
    const source = codeEl.textContent.trim();
    if (!source) return;

    const host = document.createElement('div');
    host.className = 'diagram-block cytoscape-diagram';
    host.textContent = 'Rendering graph...';
    pre.replaceWith(host);

    let spec;
    try {
      spec = JSON.parse(source);
      if (spec && typeof spec === 'object' && spec.spec !== undefined &&
          (spec.type === 'cytoscape' || spec.type === 'cytoscapejs')) {
        spec = spec.spec;
      }
    } catch (err) {
      showDiagramError(host, 'Cytoscape.js', err, source);
      return;
    }

    if (!spec || typeof spec !== 'object') {
      showDiagramError(host, 'Cytoscape.js', new Error('Spec must be a JSON object.'), source);
      return;
    }

    const graphHost = document.createElement('div');
    graphHost.className = 'cytoscape-host';
    host.textContent = '';
    host.appendChild(graphHost);

    const elements = Array.isArray(spec.elements) ? spec.elements.map(el => {
      const next = Object.assign({}, el);
      if (next && next.data && next.data.id && !next.data.label &&
          next.data.source === undefined && next.data.target === undefined) {
        next.data = Object.assign({}, next.data, { label: next.data.id });
      }
      return next;
    }) : [];

    try {
      const cy = cytoscape({
        container: graphHost,
        elements,
        style: Array.isArray(spec.style) && spec.style.length ? spec.style : defaultCytoscapeStyle(),
        layout: spec.layout || { name: 'cose', animate: false, fit: true, padding: 24 },
        minZoom: 0.2,
        maxZoom: 3,
        wheelSensitivity: 0.2,
        userZoomingEnabled: true,
        userPanningEnabled: true,
      });
      requestAnimationFrame(() => {
        try { cy.resize(); cy.fit(undefined, 24); } catch (_) {}
      });
    } catch (err) {
      showDiagramError(host, 'Cytoscape.js', err, source);
    }
  });
}

function renderSvgBlocks(container) {
  container.querySelectorAll('pre code').forEach(codeEl => {
    if (getCodeLanguage(codeEl) !== 'svg') return;
    const pre = codeEl.closest('pre');
    if (!pre || pre.dataset.diagramRendered) return;
    pre.dataset.diagramRendered = '1';
    const source = codeEl.textContent.trim();
    if (!source) return;

    const host = document.createElement('div');
    host.className = 'diagram-block svg-diagram';
    host.textContent = 'Rendering SVG...';
    pre.replaceWith(host);

    if (!/^<svg[\s>]/i.test(source)) {
      showDiagramError(host, 'SVG', new Error('SVG blocks must start with <svg>.'), source);
      return;
    }

    try {
      host.innerHTML = DOMPurify.sanitize(source, {
        USE_PROFILES: { svg: true, svgFilters: true },
      });
      normalizeSvgDiagram(host);
    } catch (err) {
      showDiagramError(host, 'SVG', err, source);
    }
  });
}

function normalizeSvgDiagram(host) {
  const svg = host.querySelector('svg');
  if (!svg) return;
  const widthText = svg.getAttribute('width') || '';
  const heightText = svg.getAttribute('height') || '';
  const width = Number.parseFloat(widthText);
  const height = Number.parseFloat(heightText);
  if (!svg.getAttribute('viewBox') &&
      Number.isFinite(width) && width > 0 &&
      Number.isFinite(height) && height > 0) {
    svg.setAttribute('viewBox', `0 0 ${width} ${height}`);
  }
  if (svg.getAttribute('viewBox')) {
    svg.removeAttribute('width');
    svg.removeAttribute('height');
    svg.style.width = '100%';
    svg.style.maxWidth = '100%';
    svg.style.height = 'auto';
  } else {
    svg.style.maxWidth = '100%';
    svg.style.height = 'auto';
  }
  svg.style.display = 'block';
}

function showDiagramError(host, kind, err, source) {
  host.classList.add('diagram-error');
  const message = err && err.message ? err.message : String(err || 'Render failed');
  host.innerHTML = '<strong>' + kind + ' render failed.</strong>' +
    '<div>' + escapeHtml(message) + '</div>' +
    '<pre><code>' + escapeHtml(source) + '</code></pre>';
}

// ── Message rendering ─────────────────────────────────────────────────────
function renderMessages(messages) {
  messagesEl.innerHTML = '';
  if (messages.length === 0) {
    messagesEl.appendChild(emptyState);
    return;
  }
  for (const msg of messages) {
    if (msg && msg.role === 'web_debug' && !state.webDebuggingActive) {
      continue;
    }
    if (msg &&
        msg.role === 'assistant' &&
        Array.isArray(msg.ui_trace) &&
        msg.ui_trace.length) {
      messagesEl.appendChild(buildAssistantTraceRow(msg.ui_trace, msg.content || ''));
    } else {
      messagesEl.appendChild(buildMessageRow(msg.role, msg.content));
    }
  }
  messagesEl.scrollTop = messagesEl.scrollHeight;
}

const INGESTIBLE_UPLOAD_EXTS = new Set([
  'pdf', 'png', 'jpg', 'jpeg', 'gif', 'bmp', 'webp', 'tif', 'tiff',
  'doc', 'docx', 'docm', 'xls', 'xlsx', 'xlsm', 'ppt', 'pptx', 'pptm'
]);

function fileExtension(name) {
  const text = String(name || '');
  const idx = text.lastIndexOf('.');
  return idx >= 0 ? text.slice(idx + 1).toLowerCase() : '';
}

function fileNeedsIngestion(record) {
  if (!record) return false;
  if (record.file_kind === 'processed') return true;
  if (record.needs_ingestion === true) return true;
  return INGESTIBLE_UPLOAD_EXTS.has(fileExtension(record.filename || record.name));
}

function formatBytes(bytes) {
  const n = Number(bytes);
  if (!Number.isFinite(n) || n < 0) return '';
  if (n < 1024) return n + ' B';
  const units = ['KB', 'MB', 'GB', 'TB'];
  let value = n / 1024;
  let unit = units[0];
  for (let i = 1; value >= 1024 && i < units.length; i++) {
    value /= 1024;
    unit = units[i];
  }
  return value.toFixed(value >= 10 ? 1 : 2).replace(/\.0$/, '') + ' ' + unit;
}

function normalizeFileUploadRecord(input) {
  let record = {};
  if (typeof File !== 'undefined' && input instanceof File) {
    record = {
      filename: input.name,
      display_name: input.name,
      size: input.size,
      mime_type: input.type,
      needs_ingestion: fileNeedsIngestion({ filename: input.name }),
    };
  } else if (typeof input === 'string') {
    try {
      record = JSON.parse(input);
    } catch (_) {
      record = { filename: input };
    }
  } else if (input && typeof input === 'object') {
    record = Object.assign({}, input);
  }

  record.filename = record.filename || record.display_name || record.name || 'Uploaded file';
  record.display_name = record.display_name || record.filename;
  if (record.size === undefined && record.bytes !== undefined) record.size = record.bytes;
  if (!record.status) {
    record.status = record.extraction_success === false ? 'warning' : 'done';
  }
  return record;
}

function fileUploadIcon(status) {
  switch (status) {
    case 'done': return '\u2713';
    case 'warning': return '\u26A0';
    case 'error': return '!';
    case 'pending': return '\u2191';
    case 'uploading': return '\u2191';
    case 'ingesting': return '';
    default: return '\u2191';
  }
}

function fileUploadStatusText(record) {
  const ingestible = fileNeedsIngestion(record);
  switch (record.status) {
    case 'pending': return ingestible ? 'Ready for upload and ingestion' : 'Ready for upload';
    case 'uploading': return 'Uploading';
    case 'ingesting': return 'Ingesting';
    case 'done': return ingestible ? 'Ingested' : 'Uploaded';
    case 'warning': return 'Saved with warning';
    case 'error': return 'Upload failed';
    default: return 'Uploading';
  }
}

function fileUploadDetailText(record) {
  const ingestible = fileNeedsIngestion(record);
  if (record.status === 'error') return record.error || 'The upload could not be completed.';
  if (record.status === 'warning') {
    return record.extraction_error || 'The file was saved, but the processed Markdown copy needs attention.';
  }
  if (record.status === 'pending') {
    return ingestible
      ? 'Waiting to create the readable .agent Markdown copy.'
      : 'Waiting to place the file in the project folder.';
  }
  if (record.status === 'uploading') return 'Moving into the project folder.';
  if (record.status === 'ingesting') return 'Creating the readable .agent Markdown copy.';
  return ingestible
    ? 'Ready in the project folder and available in this chat.'
    : 'Ready in the project folder.';
}

function createFileUploadRow(input, initialStatus) {
  let record = normalizeFileUploadRecord(input);
  if (initialStatus) record.status = initialStatus;

  const row = document.createElement('div');
  row.className = 'message-row file';

  const lbl = document.createElement('div');
  lbl.className = 'message-role-label';
  lbl.textContent = 'File';

  const card = document.createElement('div');
  card.className = 'file-upload-card';

  const mark = document.createElement('div');
  mark.className = 'file-upload-status-mark';

  const body = document.createElement('div');
  body.className = 'file-upload-body';

  const top = document.createElement('div');
  top.className = 'file-upload-top';

  const name = document.createElement('div');
  name.className = 'file-upload-name';

  const status = document.createElement('div');
  status.className = 'file-upload-status';

  const meta = document.createElement('div');
  meta.className = 'file-upload-meta';

  const detail = document.createElement('div');
  detail.className = 'file-upload-detail';

  const links = document.createElement('div');
  links.className = 'file-upload-links';

  const progress = document.createElement('div');
  progress.className = 'file-upload-progress';
  progress.appendChild(document.createElement('span'));

  top.appendChild(name);
  top.appendChild(status);
  body.appendChild(top);
  body.appendChild(meta);
  body.appendChild(detail);
  body.appendChild(links);
  body.appendChild(progress);
  card.appendChild(mark);
  card.appendChild(b)ASSET"
R"ASSET(
ody);
  row.appendChild(lbl);
  row.appendChild(card);

  function render(nextRecord) {
    record = normalizeFileUploadRecord(nextRecord);
    const currentStatus = record.status || 'uploading';
    row.dataset.status = currentStatus;
    card.className = 'file-upload-card status-' + currentStatus;
    mark.className = 'file-upload-status-mark ' + currentStatus;
    mark.textContent = fileUploadIcon(currentStatus);
    name.textContent = record.display_name || record.filename;
    status.textContent = fileUploadStatusText(record);

    const bits = [];
    const size = formatBytes(record.size);
    if (size) bits.push(size);
    bits.push(fileNeedsIngestion(record) ? 'Processed file' : 'Project file');
    if (record.project_folder_variable) bits.push(record.project_folder_variable);
    meta.textContent = bits.join(' · ');
    detail.textContent = fileUploadDetailText(record);

    links.innerHTML = '';
    const download = record.absolute_download_url || record.download_url;
    if (download) {
      const a = document.createElement('a');
      a.href = download;
      a.target = '_blank';
      a.rel = 'noopener';
      a.textContent = 'Download';
      links.appendChild(a);
    }
    if (record.agent_index_path) {
      const span = document.createElement('span');
      span.textContent = '.agent index ready';
      links.appendChild(span);
    }
    links.hidden = links.childNodes.length === 0;
    progress.hidden = !['pending', 'uploading', 'ingesting'].includes(currentStatus);
  }

  render(record);
  return {
    row,
    update(update) {
      render(Object.assign({}, record, update || {}));
    },
    snapshot() {
      return normalizeFileUploadRecord(record);
    },
  };
}

function buildFileUploadRow(content) {
  return createFileUploadRow(content).row;
}

function appendLiveFileUploadRow(file) {
  const live = createFileUploadRow(file, 'pending');
  if (messagesEl.contains(emptyState)) emptyState.remove();
  messagesEl.appendChild(live.row);
  messagesEl.scrollTop = messagesEl.scrollHeight;
  return live;
}

function formatContextUsageText(used, total) {
  const usedText = Number(used || 0).toLocaleString();
  if (Number(total || 0) > 0) {
    return 'CTX: ' + usedText + ' / ' + Number(total).toLocaleString();
  }
  return 'CTX: ' + usedText;
}

function buildContextUsageRow(content, pending = false) {
  content = content || '';
  const row = document.createElement('div');
  row.className = 'message-row context-usage';
  if (pending) row.classList.add('is-pending');

  const lbl = document.createElement('div');
  lbl.className = 'message-role-label';
  lbl.textContent = ' ';
  lbl.setAttribute('aria-hidden', 'true');

  const bubble = document.createElement('div');
  bubble.className = 'context-usage-bubble';

  const prefix = document.createElement('span');
  prefix.className = 'context-usage-prefix';
  prefix.textContent = 'CTX:';
  bubble.appendChild(prefix);

  const value = document.createElement('span');
  value.className = 'context-usage-value';
  value.textContent = content.indexOf('CTX:') === 0 ? content.slice(4).trim() : content;
  bubble.appendChild(value);

  row.appendChild(lbl);
  row.appendChild(bubble);
  return row;
}

function normalizeCompressionRecord(content) {
  let record = content;
  if (typeof record === 'string') {
    try {
      const parsed = JSON.parse(record);
      if (parsed && typeof parsed === 'object') record = parsed;
    } catch (_) {}
  }
  if (typeof record === 'string') {
    record = { text: record, status: 'done' };
  }
  record = record || {};
  return {
    text: record.text || 'Context window compressed.',
    status: record.status === 'live' ? 'live' : 'done',
    before_messages: record.before_messages,
    after_messages: record.after_messages,
    compressed_through: record.compressed_through,
    created_at: record.created_at,
  };
}

function createCompressionRow(content) {
  let record = normalizeCompressionRecord(content);

  const row = document.createElement('div');
  row.className = 'message-row compression-status';

  const lbl = document.createElement('div');
  lbl.className = 'message-role-label';
  lbl.textContent = ' ';
  lbl.setAttribute('aria-hidden', 'true');

  const bubble = document.createElement('div');
  bubble.className = 'compression-status-bubble status-' + record.status;

  // Icon
  const icon = document.createElement('span');
  icon.className = 'compression-status-icon status-' + record.status;
  if (record.status === 'done') icon.textContent = '\u2713';
  bubble.appendChild(icon);

  // Text column
  const col = document.createElement('div');
  col.style.display = 'flex';
  col.style.flexDirection = 'column';
  col.style.gap = '2px';

  const text = document.createElement('span');
  text.className = 'compression-status-text';
  text.textContent = record.text;
  col.appendChild(text);

  // Metadata line (only if present)
  if (record.before_messages !== undefined || record.created_at) {
    const meta = document.createElement('span');
    meta.style.cssText = 'font-size:11px;color:var(--color-text-muted);';
    const parts = [];
    if (typeof record.before_messages === 'number' && typeof record.after_messages === 'number') {
      parts.push(record.before_messages + ' \u2192 ' + record.after_messages + ' messages');
    }
    if (record.created_at) {
      parts.push(new Date(record.created_at).toLocaleString());
    }
    meta.textContent = parts.join('  \u00B7  ');
    col.appendChild(meta);
  }

  bubble.appendChild(col);
  row.appendChild(lbl);
  row.appendChild(bubble);

  return {
    row,
    update(nextRecord) {
      const r = normalizeCompressionRecord(Object.assign({}, record, nextRecord || {}));
      record = r;
      bubble.className = 'compression-status-bubble status-' + r.status;
      icon.className = 'compression-status-icon status-' + r.status;
      icon.textContent = r.status === 'done' ? '\u2713' : '';
      text.textContent = r.text;
    },
    finalize(finalText) {
      this.update({
        text: finalText || record.text || 'Context window compressed.',
        status: 'done',
      });
    },
    snapshot() {
      return normalizeCompressionRecord(record);
    },
  };
}

function buildCompressionRow(content) {
  return createCompressionRow(content).row;
}

function normalizeQueueRecord(content) {
  let record = content;
  if (typeof record === 'string') {
    try {
      const parsed = JSON.parse(record);
      if (parsed && typeof parsed === 'object') record = parsed;
    } catch (_) {}
  }
  record = record || {};
  return {
    state: record.state || 'queued',
    provider: record.provider || '',
    position: Number(record.position || 0),
    depth: Number(record.depth || 0),
    active: Number(record.active || 0),
    maxActive: Number(record.maxActive || 0),
  };
}

function formatQueueStatusText(record) {
  const providerSuffix = record.provider ? ' for ' + record.provider : '';
  if (record.state === 'queued') {
    if (record.position > 0 && record.depth > 0) {
      return 'Waiting' + providerSuffix + ': ' + record.position + ' / ' + record.depth + ' in queue';
    }
    if (record.depth > 0) {
      return 'Waiting' + providerSuffix + '. Queue depth: ' + record.depth;
    }
    return 'Waiting' + providerSuffix + '...';
  }
  return 'Provider slot acquired' + providerSuffix + '.';
}

function createQueueStatusRow(content) {
  let record = normalizeQueueRecord(content);

  const row = document.createElement('div');
  row.className = 'message-row provider-queue-status';

  const lbl = document.createElement('div');
  lbl.className = 'message-role-label';
  lbl.textContent = ' ';
  lbl.setAttribute('aria-hidden', 'true');

  const bubble = document.createElement('div');
  bubble.className = 'provider-queue-status-bubble';

  const icon = document.createElement('span');
  icon.className = 'provider-queue-status-icon';
  bubble.appendChild(icon);

  const text = document.createElement('span');
  text.className = 'provider-queue-status-text';
  bubble.appendChild(text);

  row.appendChild(lbl);
  row.appendChild(bubble);

  function render(nextRecord) {
    record = normalizeQueueRecord(nextRecord);
    const visualState = record.state === 'queued' ? 'live' : 'done';
    bubble.className = 'provider-queue-status-bubble status-' + visualState;
    icon.className = 'provider-queue-status-icon status-' + visualState;
    icon.textContent = visualState === 'done' ? '\u2713' : '';
    text.textContent = formatQueueStatusText(record);
  }

  render(record);

  return {
    row,
    update(nextRecord) {
      render(Object.assign({}, record, nextRecord || {}));
    },
  };
}

function normalizeActivityRecord(content) {
  let record = content;
  if (typeof record === 'string') {
    try {
      const parsed = JSON.parse(record);
      if (parsed && typeof parsed === 'object') record = parsed;
    } catch (_) {}
  }
  record = record || {};
  return {
    code: record.code || 'sending_request',
    text: record.text || 'Working with the provider...',
    status: record.status === 'done' ? 'done' : 'live',
  };
}

function createActivityStatusRow(content) {
  let record = normalizeActivityRecord(content);

  const row = document.createElement('div');
  row.className = 'message-row provider-activity-status';

  const lbl = document.createElement('div');
  lbl.className = 'message-role-label';
  lbl.textContent = ' ';
  lbl.setAttribute('aria-hidden', 'true');

  const bubble = document.createElement('div');
  bubble.className = 'provider-activity-status-bubble';

  const icon = document.createElement('span');
  icon.className = 'provider-activity-status-icon';
  bubble.appendChild(icon);

  const text = document.createElement('span');
  text.className = 'provider-activity-status-text';
  bubble.appendChild(text);

  row.appendChild(lbl);
  row.appendChild(bubble);

  function render(nextRecord) {
    record = normalizeActivityRecord(nextRecord);
    bubble.className = 'provider-activity-status-bubble status-' + record.status;
    icon.className = 'provider-activity-status-icon status-' + record.status;
    icon.textContent = record.status === 'done' ? '\u2713' : '';
    text.textContent = record.text;
  }

  render(record);

  return {
    row,
    update(nextRecord) {
      render(Object.assign({}, record, nextRecord || {}));
    },
  };
}

function normalizeAssistantTrace(trace, fallbackContent = '') {
  const normalized = [];
  if (Array.isArray(trace)) {
    trace.forEach(segment => {
      if (!segment || typeof segment !== 'object') return;
      if (segment.type === 'tool_usage' ||
          segment.tool_name !== undefined ||
          segment.tool_call_id !== undefined) {
        const toolSource = segment.record && typeof segment.record === 'object'
          ? segment.record
          : segment;
        normalized.push({
          type: 'tool_usage',
          record: normalizeToolUsageRecord(toolSource),
        });
        return;
      }
      if (segment.type === 'text' || segment.content !== undefined) {
        const text = typeof segment.content === 'string'
          ? segment.content
          : String(segment.content || '');
        if (text) {
          normalized.push({
            type: 'text',
            content: text,
            live: !!segment.live,
          });
        }
      }
    });
  }
  if (!normalized.length && fallbackContent) {
    normalized.push({ type: 'text', content: fallbackContent, live: false });
  }
  return normalized;
}

function prettyToolUsageText(value) {
  if (value == null) return '';
  if (typeof value === 'string') {
    const trim)ASSET"
R"ASSET(
med = value.trim();
    if (!trimmed) return '';
    try {
      return JSON.stringify(JSON.parse(trimmed), null, 2);
    } catch (_) {
      return value;
    }
  }
  try {
    return JSON.stringify(value, null, 2);
  } catch (_) {
    return String(value);
  }
}

function normalizeToolUsageRecord(content) {
  let record = content;
  if (typeof record === 'string') {
    try {
      const parsed = JSON.parse(record);
      if (parsed && typeof parsed === 'object') record = parsed;
    } catch (_) {
      record = { result: record };
    }
  }
  record = record || {};
  return {
    toolCallId: record.tool_call_id || record.toolCallId || '',
    toolName: record.tool_name || record.toolName || 'Tool',
    arguments: prettyToolUsageText(record.arguments || record.tool_arguments || ''),
    result: prettyToolUsageText(record.result || record.tool_result || ''),
    status: record.status === 'error' ? 'error'
      : record.status === 'live' ? 'live'
      : 'done',
  };
}

function formatToolUsageSummary(record) {
  if (record.status === 'live') {
    return 'Using ' + record.toolName + '...';
  }
  if (record.status === 'error') {
    return 'Tool error: ' + record.toolName;
  }
  return 'Used ' + record.toolName;
}

function createToolUsageBlock(content) {
  let record = normalizeToolUsageRecord(content);

  const details = document.createElement('details');
  details.className = 'tool-usage-block';

  const summary = document.createElement('summary');
  const icon = document.createElement('span');
  icon.className = 'tool-usage-icon';
  const text = document.createElement('span');
  text.className = 'tool-usage-summary-text';
  summary.appendChild(icon);
  summary.appendChild(text);

  const body = document.createElement('div');
  body.className = 'tool-usage-content';

  const argsWrap = document.createElement('div');
  argsWrap.className = 'tool-usage-section';
  const argsLabel = document.createElement('div');
  argsLabel.className = 'tool-usage-section-label';
  argsLabel.textContent = 'Arguments';
  const argsPre = document.createElement('pre');
  argsPre.className = 'tool-usage-pre';
  argsWrap.appendChild(argsLabel);
  argsWrap.appendChild(argsPre);

  const resultWrap = document.createElement('div');
  resultWrap.className = 'tool-usage-section';
  const resultLabel = document.createElement('div');
  resultLabel.className = 'tool-usage-section-label';
  resultLabel.textContent = 'Result';
  const resultPre = document.createElement('pre');
  resultPre.className = 'tool-usage-pre';
  resultWrap.appendChild(resultLabel);
  resultWrap.appendChild(resultPre);

  body.appendChild(argsWrap);
  body.appendChild(resultWrap);
  details.appendChild(summary);
  details.appendChild(body);

  function render(nextRecord) {
    record = normalizeToolUsageRecord(nextRecord);
    details.className = 'tool-usage-block status-' + record.status;
    icon.className = 'tool-usage-icon status-' + record.status;
    icon.textContent = record.status === 'done' ? '\u2713'
      : record.status === 'error' ? '!' : '';
    text.textContent = formatToolUsageSummary(record);
    argsPre.textContent = record.arguments || 'No arguments provided.';
    resultPre.textContent = record.result || (record.status === 'live'
      ? 'Waiting for tool result...'
      : 'No tool result captured.');
    argsWrap.hidden = !record.arguments;
    resultWrap.hidden = !record.result && record.status === 'live';
    details.open = record.status === 'live';
  }

  render(record);

  return {
    element: details,
    update(nextRecord) {
      render(Object.assign({}, record, nextRecord || {}));
    },
    snapshot() {
      return normalizeToolUsageRecord(record);
    },
  };
}

function renderAssistantTraceBubble(bubble, trace, options = {}) {
  const normalized = normalizeAssistantTrace(trace, options.fallbackContent || '');
  bubble.innerHTML = '';
  bubble.classList.toggle('assistant-trace-bubble', normalized.length > 0);

  let lastLiveText = null;
  normalized.forEach(segment => {
    const wrap = document.createElement('div');
    wrap.className = 'assistant-bubble-segment assistant-bubble-' + segment.type;
    if (segment.type === 'tool_usage') {
      const block = createToolUsageBlock(segment.record);
      wrap.appendChild(block.element);
    } else {
      wrap.innerHTML = renderMarkdown(segment.content || '', {
        streaming: !!(options.streaming && segment.live),
      });
      postProcessMessageBubble(wrap, {
        renderDiagrams: !(options.streaming && segment.live),
      });
      if (options.streaming && segment.live) {
        lastLiveText = wrap;
      }
    }
    bubble.appendChild(wrap);
  });

  if (options.streaming) {
    const cursor = document.createElement('span');
    cursor.className = 'streaming-cursor';
    if (lastLiveText) {
      lastLiveText.appendChild(cursor);
    } else {
      bubble.appendChild(cursor);
    }
  }

  return normalized;
}

function buildAssistantTraceRow(trace, fallbackContent = '') {
  const row = document.createElement('div');
  row.className = 'message-row model';

  const lbl = document.createElement('div');
  lbl.className = 'message-role-label';
  lbl.textContent = 'Assistant';

  const bubble = document.createElement('div');
  bubble.className = 'message-bubble';
  renderAssistantTraceBubble(bubble, trace, { fallbackContent });

  row.appendChild(lbl);
  row.appendChild(bubble);
  return row;
}

function createToolUsageRow(content) {
  const row = document.createElement('div');
  row.className = 'message-row tool-usage-status';

  const lbl = document.createElement('div');
  lbl.className = 'message-role-label';
  lbl.textContent = 'Tool';

  const block = createToolUsageBlock(content);
  row.appendChild(lbl);
  row.appendChild(block.element);

  return {
    row,
    update(nextRecord) {
      block.update(nextRecord);
    },
    snapshot() {
      return block.snapshot();
    },
  };
}

function parseWebDebugRecord(content) {
  if (!content) return {};
  if (typeof content === 'object') return content;
  try {
    return JSON.parse(content);
  } catch (_) {
    return { system_prompt: String(content || '') };
  }
}

function createDebugSection(title, text) {
  const details = document.createElement('details');
  details.className = 'web-debug-section';
  details.open = title === 'System Prompt';

  const summary = document.createElement('summary');
  summary.textContent = title;
  details.appendChild(summary);

  const pre = document.createElement('pre');
  pre.textContent = text && String(text).trim() ? String(text) : '(empty)';
  details.appendChild(pre);
  return details;
}

function formatDebugMessages(messages) {
  if (!Array.isArray(messages) || !messages.length) return '(none)';
  return messages.map((msg, idx) => {
    const role = msg && msg.role ? msg.role : 'message';
    const content = msg && msg.content ? msg.content : '';
    return `#${idx + 1} [${role}]\n${content}`;
  }).join('\n\n---\n\n');
}

function buildWebDebugRow(content) {
  const record = parseWebDebugRecord(content);
  const row = document.createElement('div');
  row.className = 'message-row web-debug';

  const lbl = document.createElement('div');
  lbl.className = 'message-role-label';
  lbl.textContent = 'Debug';

  const bubble = document.createElement('div');
  bubble.className = 'web-debug-bubble';

  const title = document.createElement('div');
  title.className = 'web-debug-title';
  title.textContent = 'Prompt sent to model';
  bubble.appendChild(title);
  bubble.appendChild(createDebugSection('System Prompt', record.system_prompt || ''));
  bubble.appendChild(createDebugSection('User Prompt', record.user_prompt || ''));
  bubble.appendChild(createDebugSection('Context Messages', formatDebugMessages(record.request_messages)));

  row.appendChild(lbl);
  row.appendChild(bubble);
  return row;
}

function buildMessageRow(role, content) {
  if (role === 'file') return buildFileUploadRow(content);
  if (role === 'context') return buildContextUsageRow(content);
  if (role === 'compression') return buildCompressionRow(content);
  if (role === 'tool_usage') return createToolUsageRow(content).row;
  if (role === 'web_debug') return buildWebDebugRow(content);

  const row = document.createElement('div');
  row.className = 'message-row ' + (role === 'user' ? 'user' : role === 'error' ? 'error' : 'model');

  const lbl = document.createElement('div');
  lbl.className = 'message-role-label';
  lbl.textContent = role === 'user' ? 'You' : role === 'error' ? '\u26A0' : 'Assistant';

  const bubble = document.createElement('div');
  bubble.className = 'message-bubble' + (role === 'error' ? ' error' : '');

  if (role === 'assistant') {
    bubble.innerHTML = renderMarkdown(content);
    postProcessMessageBubble(bubble);
  } else {
    const p = document.createElement('p');
    p.style.whiteSpace = 'pre-wrap';
    p.textContent = content;
    bubble.appendChild(p);
  }

  row.appendChild(lbl);
  row.appendChild(bubble);
  return row;
}

function createAssistantTurnRow(initialTrace = []) {
  const row = document.createElement('div');
  row.className = 'message-row model';
  row.id = 'streaming-row';

  const lbl = document.createElement('div');
  lbl.className = 'message-role-label';
  lbl.textContent = 'Assistant';

  const bubble = document.createElement('div');
  bubble.className = 'message-bubble streaming';
  bubble.id = 'streaming-bubble';

  row.appendChild(lbl);
  row.appendChild(bubble);
  messagesEl.appendChild(row);
  messagesEl.scrollTop = messagesEl.scrollHeight;

  let trace = normalizeAssistantTrace(initialTrace);

  function render(streaming) {
    renderAssistantTraceBubble(bubble, trace, { streaming });
    messagesEl.scrollTop = messagesEl.scrollHeight;
  }

  function appendTextDelta(delta) {
    if (!delta) return;
    const last = trace[trace.length - 1];
    if (last && last.type === 'text' && last.live) {
      last.content += delta;
    } else {
      trace.push({ type: 'text', content: delta, live: true });
    }
    render(true);
  }

  function upsertTool(record) {
    const next = normalizeToolUsageRecord(record);
    let index = -1;
    if (next.toolCallId) {
      index = trace.findIndex(segment =>
        segment.type === 'tool_usage' &&
        segment.record.toolCallId === next.toolCallId);
    }
    if (index < 0) {
      const last = trace[trace.length - 1];
      if (last && last.type === 'text') {
        last.live = false;
      }
      trace.push({ type: 'tool_usage', record: next });
    } else {
      trace[index].record = Object.assign({}, trace[index].record, next);
    }
    render(true);
  }

  function finalize() {
    row.removeAttribute('id');
    bubble.removeAttribute('id');
    bubble.classList.remove('streaming');
    trace = trace.map(segment => {
      if (segment.type === 'text') {
        return Object.assign({}, segment, { live: false });
      }
      const record = Object.assign({}, segment.record);
      if (record.status === 'live') {
        record.status = 'done';
      }
      return { type: 'tool_usage', record };
    });
    render(false);
    return {
      text: trace
        .filter(segment => segment.type === 'text')
        .map(segment => segment.content || '')
        .join(''),
      ui_trace: trace.map(segment =>
        segment.type === 'text'
          ? { type: 'text', content: segment.content || '' }
          : Object.assign({ type: 'tool_usage' }, segment.record)),
    };
  }

  function hasContent() {
    return trace.some(segment =>
      segment.type === 'tool_usage' ||
      (segment.type === 'text' && (segment.content || '').trim()));
  }

  function remove() {
    row.remove();
)ASSET"
R"ASSET(
  }

  render(true);
  return { row, bubble, appendTextDelta, upsertTool, finalize, hasContent, remove };
}

// ── SSE stream reader ─────────────────────────────────────────────────────
// Reads a fetch() response body as a stream of SSE events.
// Calls onEvent(parsedObject) for each "data: {...}" line.
// Returns when the stream ends or the abort signal fires.
async function readSSEStream(response, onEvent, signal) {
  const reader = response.body.getReader();
  const decoder = new TextDecoder('utf-8');
  let buffer = '';
  let sawDoneEvent = false;
  let sawErrorEvent = false;

  try {
    while (true) {
      if (signal && signal.aborted) break;
      const { done, value } = await reader.read();
      if (done) break;

      buffer += decoder.decode(value, { stream: true });

      // Process all complete events in the buffer
      const lines = buffer.split('\n');
      buffer = lines.pop();  // keep the last (possibly incomplete) line

      for (const line of lines) {
        const trimmed = line.trim();
        if (!trimmed.startsWith('data:')) continue;
        const jsonStr = trimmed.slice(5).trim();
        if (!jsonStr) continue;
        try {
          const event = JSON.parse(jsonStr);
          if (event && event.done) sawDoneEvent = true;
          if (event && event.error) sawErrorEvent = true;
          onEvent(event);
        } catch (_) { /* malformed event — skip */ }
      }
    }
    // Flush remaining buffer
    if (buffer.trim().startsWith('data:')) {
      const jsonStr = buffer.trim().slice(5).trim();
      if (jsonStr) {
        try {
          const event = JSON.parse(jsonStr);
          if (event && event.done) sawDoneEvent = true;
          if (event && event.error) sawErrorEvent = true;
          onEvent(event);
        } catch (_) {}
      }
    }
  } finally {
    reader.releaseLock();
  }
  return {
    completed: sawDoneEvent,
    errored: sawErrorEvent,
    aborted: !!(signal && signal.aborted),
  };
}

// ── Project / chat list ───────────────────────────────────────────────────
function renderProjectList() {
  projectList.innerHTML = '';
  for (const proj of state.projects) {
    const item = document.createElement('div');
    item.className = 'project-item';

    const label = document.createElement('div');
    label.className = 'project-label';
    const isOpen = state.chats[proj.id] !== undefined;
    label.innerHTML = `<span>${escapeHtml(proj.name)}</span><span class="project-arrow ${isOpen ? 'open' : ''}">▶</span>`;
    label.addEventListener('click', () => toggleProject(proj.id, label));

    const chatListEl = document.createElement('div');
    chatListEl.className = 'chat-list' + (isOpen ? ' open' : '');
    chatListEl.id = 'chats-' + proj.id;
    if (state.chats[proj.id]) renderChatItems(proj.id, chatListEl);

    item.appendChild(label);
    item.appendChild(chatListEl);
    projectList.appendChild(item);
  }
}

function renderChatItems(projectId, container) {
  container.innerHTML = '';
  const chats = state.chats[projectId] || [];
  for (const chat of chats) {
    const entry = document.createElement('div');
    entry.className = 'chat-entry' + (chat.id === state.selectedChatId ? ' active' : '');
    entry.dataset.chatId    = chat.id;
    entry.dataset.projectId = projectId;

    const displayName = stripUserSuffix(chat.name);
    const nameSpan = document.createElement('span');
    nameSpan.style.overflow     = 'hidden';
    nameSpan.style.textOverflow = 'ellipsis';
    nameSpan.textContent        = displayName;

    const delBtn = document.createElement('button');
    delBtn.className   = 'chat-delete-btn';
    delBtn.title       = 'Delete chat';
    delBtn.textContent = '✕';
    delBtn.addEventListener('click', async e => {
      e.stopPropagation();
      if (!confirm(`Delete "${displayName}"?`)) return;
      await deleteChat(projectId, chat.id);
    });

    entry.appendChild(nameSpan);
    entry.appendChild(delBtn);

    // Single click → select; double-click → inline rename
    entry.addEventListener('click', () => selectChat(projectId, chat.id, chat.name));
    entry.addEventListener('dblclick', e => {
      e.stopPropagation();
      startInlineRename(entry, nameSpan, projectId, chat.id, displayName);
    });

    container.appendChild(entry);
  }
}

function stripUserSuffix(name) {
  const m = name.match(/^(.*)\s+\[([^\]]+)\]$/);
  return m ? m[1] : name;
}

async function toggleProject(projectId, labelEl) {
  const listEl = $('chats-' + projectId);
  if (!listEl) return;
  if (listEl.classList.contains('open')) {
    listEl.classList.remove('open');
    labelEl.querySelector('.project-arrow').classList.remove('open');
    return;
  }
  if (!state.chats[projectId]) await loadChats(projectId);
  renderChatItems(projectId, listEl);
  listEl.classList.add('open');
  labelEl.querySelector('.project-arrow').classList.add('open');
  state.selectedProjectId = projectId;
  newChatBtn.disabled = false;
}

async function loadChats(projectId) {
  const resp = await api('GET', `/api/projects/${projectId}/chats`);
  if (!resp) return;
  if (resp.ok) state.chats[projectId] = await resp.json();
}

async function selectChat(projectId, chatId, chatName) {
  state.selectedProjectId = projectId;
  state.selectedChatId    = chatId;
  state.selectedChatAgenticModeId = null;
  document.querySelectorAll('.chat-entry').forEach(el =>
    el.classList.toggle('active', el.dataset.chatId === chatId));
  chatTitle.textContent = stripUserSuffix(chatName);
  messageInput.disabled = false;
  sendBtn.disabled      = false;
  newChatBtn.disabled   = false;
  if (attachBtn) attachBtn.disabled = false;
  await Promise.all([
    loadMessages(projectId, chatId),
    loadProjectAgenticModes(projectId),
  ]);
  renderAgenticModeLabel();
}

async function loadProjectAgenticModes(projectId) {
  const resp = await api('GET', `/api/projects/${projectId}/agentic-modes`);
  if (!resp || !resp.ok) {
    state.projectAgenticModes = [];
    state.projectDefaultAgenticModeId = '';
    state.projectEnabledAgenticModeIds = [];
    state.projectEnableWebDebugging = false;
    setWebDebuggingActive(false);
    return;
  }
  const data = await resp.json();
  state.projectDefaultAgenticModeId = data.default_id || '';
  state.projectEnabledAgenticModeIds = data.enabled_ids || [];
  state.projectAgenticModes = data.modes || [];
  state.projectAllowManualCompress = data.allow_manual_context_compression || false;
  state.projectEnableWebDebugging = data.enable_web_debugging || false;
  if (!state.projectEnableWebDebugging) {
    setWebDebuggingActive(false);
  }

  // Load chat-level override from chat metadata (not available via API yet)
  for (const chat of (state.chats[projectId] || [])) {
    if (chat.id === state.selectedChatId) {
      state.selectedChatAgenticModeId = chat.selected_agentic_mode_id || null;
      break;
    }
  }
}

function currentAgenticModeId() {
  if (state.selectedChatAgenticModeId != null) return state.selectedChatAgenticModeId || '';
  return state.projectDefaultAgenticModeId || '';
}

function removeWebDebugBubbles() {
  state.messages = state.messages.filter(msg => !(msg && msg.role === 'web_debug'));
  document.querySelectorAll('.message-row.web-debug').forEach(row => row.remove());
}

function isWebDebuggingEnabled() {
  return !!(state.projectEnableWebDebugging && state.webDebuggingActive);
}

function renderDebugButton() {
  if (!debugBtn) return;
  if (!state.projectEnableWebDebugging || !state.selectedChatId) {
    debugBtn.style.display = 'none';
    debugBtn.disabled = true;
    debugBtn.textContent = 'Enable debugging';
    debugBtn.classList.remove('active');
    return;
  }
  debugBtn.style.display = '';
  debugBtn.disabled = false;
  debugBtn.textContent = state.webDebuggingActive ? 'Disable debugging' : 'Enable debugging';
  debugBtn.classList.toggle('active', state.webDebuggingActive);
}

function setWebDebuggingActive(active) {
  state.webDebuggingActive = !!(active && state.projectEnableWebDebugging);
  if (!state.webDebuggingActive) {
    removeWebDebugBubbles();
  }
  renderDebugButton();
}

function findLastUserRow() {
  const rows = Array.from(messagesEl.querySelectorAll('.message-row.user'));
  return rows.length ? rows[rows.length - 1] : null;
}

function insertWebDebugMessage(record) {
  if (!isWebDebuggingEnabled()) return;
  const msg = {
    role: 'web_debug',
    content: JSON.stringify(record || {}),
    created_at: '',
  };
  let insertAt = state.messages.length;
  for (let i = state.messages.length - 1; i >= 0; --i) {
    if (state.messages[i] && state.messages[i].role === 'user') {
      insertAt = i;
      break;
    }
  }
  state.messages.splice(insertAt, 0, msg);

  const row = buildMessageRow('web_debug', msg.content);
  const userRow = findLastUserRow();
  if (userRow && userRow.parentNode === messagesEl) {
    messagesEl.insertBefore(row, userRow);
  } else {
    messagesEl.appendChild(row);
  }
  messagesEl.scrollTop = messagesEl.scrollHeight;
}

function renderAgenticModeLabel() {
  if (!agenticModeLabel) return;
  const activeId = currentAgenticModeId();
  const available = state.projectAgenticModes.filter(m =>
    state.projectEnabledAgenticModeIds.includes(m.id));

  let labelText = 'None';
  let hasChoices = available.length > 0;

  if (activeId) {
    const mode = state.projectAgenticModes.find(m => m.id === activeId);
    if (mode) labelText = mode.name;
  } else if (state.projectDefaultAgenticModeId) {
    const mode = state.projectAgenticModes.find(m => m.id === state.projectDefaultAgenticModeId);
    if (mode) labelText = mode.name;
  }

  agenticModeLabel.textContent = 'Mode: ' + labelText;
  agenticModeLabel.className = activeId
    ? (hasChoices ? 'agentic-mode-active' : 'agentic-mode-active disabled')
    : 'agentic-mode-none';

  // Clickable only if there are multiple enabled modes (or more than just the default)
  const canSwitch = available.length > 1 ||
    (available.length === 1 && activeId !== available[0].id) ||
    (available.length === 0 && state.projectAgenticModes.length > 0);

  if (canSwitch) {
    agenticModeLabel.style.pointerEvents = '';
    agenticModeLabel.title = 'Click to change agentic mode';
  } else {
    agenticModeLabel.style.pointerEvents = 'none';
    agenticModeLabel.title = '';
  }
  // Compress button visibility
  if (compressBtn) {
    compressBtn.style.display = state.projectAllowManualCompress ? 'inline' : 'none';
  }
  renderDebugButton();
}

function openAgenticModePicker() {
  if (agenticModePicker) { agenticModePicker.remove(); agenticModePicker = null; return; }

  const available = state.projectAgenticModes.filter(m =>
    state.projectEnabledAgenticModeIds.includes(m.id));
  // Always include current default even if not enabled
  const current = currentAgenticModeId();
  const items = [];

  // None option
  items.push({ id: '', name: 'None' });

  for (const m of available) {
    items.push(m);
  }
  if (current && !items.some(i => i.id === current)) {
    const m = state.projectAgenticModes.find(x => x.id === current);
    if (m) items.push(m);
  }

  const menu = document.createElement('div');
  menu.id = 'agentic-mode-picker';
  menu.style.cssText = `
    position:absolute; bottom:8px; left:20px; z-index:100;
    background:var(--color-bg-main); border:1px solid var(--color-border-main);
    border-radius:var(--radius-input); box-shadow:0 4px 12px rgba(0,0,0,0.15);
    max-height:240px; overflow:auto; min-width:180px;
  `;
  for (const item of items) {
    const row = document.createElement('div');
    row.style.cssText = `
      padding:8px 12px; cursor:pointer;
      color: var(--color-text-primary); font-size:var(--font-size-base);
      border-bottom:1px solid var(--color-border-main);
 )ASSET"
R"ASSET(
   `;
    row.textContent = item.name || 'None';
    if (item.id === current) {
      row.style.fontWeight = '700';
      row.style.color = 'var(--color-accent-primary)';
    }
    row.addEventListener('mouseenter', () => { row.style.background = 'var(--color-bg-sidebar-hover)'; });
    row.addEventListener('mouseleave', () => { row.style.background = ''; });
    row.addEventListener('click', async () => {
      state.selectedChatAgenticModeId = item.id || '';
      renderAgenticModeLabel();
      // Persist per-chat override
      if (state.selectedChatId) {
        await api('POST', `/api/chats/${state.selectedChatId}/agentic-mode`, {
          selected_agentic_mode_id: state.selectedChatAgenticModeId,
        });
      }
      menu.remove(); agenticModePicker = null;
    });
    menu.appendChild(row);
  }
  document.body.appendChild(menu);
  agenticModePicker = menu;

  // Close on outside click
  const close = e => {
    if (!menu.contains(e.target) && e.target !== agenticModeLabel) {
      menu.remove(); agenticModePicker = null;
      document.removeEventListener('click', close);
    }
  };
  setTimeout(() => document.addEventListener('click', close), 0);
}

if (agenticModeLabel) {
  agenticModeLabel.addEventListener('click', openAgenticModePicker);
}

if (compressBtn) {
  compressBtn.addEventListener('click', async () => {
    if (!state.selectedChatId || state.sending) return;
    const ok = confirm(
      'Compressing the context window will summarize older messages into a compressed block.\n' +
      'This action cannot be undone.\n\nDo you want to continue?'
    );
    if (!ok) return;
    compressBtn.style.pointerEvents = 'none';
    compressBtn.style.opacity = '0.5';

    const status = createCompressionRow({ text: 'Compressing context...', status: 'live' });
    messagesEl.appendChild(status.row);
    messagesEl.scrollTop = messagesEl.scrollHeight;

    try {
      const resp = await api('POST', `/api/chats/${state.selectedChatId}/compress`);
      if (resp && resp.ok) {
        const data = await resp.json();
        status.finalize(data.message || 'Context compressed.');
        messagesEl.scrollTop = messagesEl.scrollHeight;
        // Reload messages so the newly persisted compression record appears
        await loadMessages(state.selectedProjectId, state.selectedChatId);
      } else {
        const data = await resp.json().catch(() => ({}));
        status.finalize(data.message || 'Compression failed.');
        const icon = status.row.querySelector('.compression-status-icon');
        if (icon) {
          icon.textContent = '!';
          icon.style.borderColor = 'var(--color-accent-danger)';
          icon.style.color = 'var(--color-accent-danger)';
        }
      }
    } catch (e) {
      console.error('Compression failed', e);
      status.finalize('Compression failed.');
      const icon = status.row.querySelector('.compression-status-icon');
      if (icon) {
        icon.textContent = '!';
        icon.style.borderColor = 'var(--color-accent-danger)';
        icon.style.color = 'var(--color-accent-danger)';
      }
    } finally {
      compressBtn.style.pointerEvents = '';
      compressBtn.style.opacity = '';
    }
  });
}

if (debugBtn) {
  debugBtn.addEventListener('click', () => {
    setWebDebuggingActive(!state.webDebuggingActive);
  });
}

async function loadMessages(projectId, chatId) {
  const resp = await api('GET', `/api/chats/${chatId}/messages`);
  if (!resp) return;
  if (resp.ok) {
    state.messages = await resp.json();
    renderMessages(state.messages);
  }
}

newChatBtn.addEventListener('click', async () => {
  if (!state.selectedProjectId) return;
  const name = prompt('Chat name:', 'New Chat');
  if (!name) return;
  const resp = await api('POST', `/api/projects/${state.selectedProjectId}/chats`,
                         { name: name.trim() });
  if (!resp || !resp.ok) return;
  const chat = await resp.json();
  await loadChats(state.selectedProjectId);
  const listEl = $('chats-' + state.selectedProjectId);
  if (listEl) renderChatItems(state.selectedProjectId, listEl);
  await selectChat(state.selectedProjectId, chat.id, chat.name);
});

async function deleteChat(projectId, chatId) {
  const resp = await api('DELETE', `/api/chats/${chatId}`);
  if (!resp || !resp.ok) return;
  if (state.selectedChatId === chatId) {
    state.selectedChatId   = null;
    chatTitle.textContent  = 'Select or create a chat';
    messageInput.disabled  = true;
    sendBtn.disabled       = true;
    state.messages         = [];
    renderMessages([]);
  }
  await loadChats(projectId);
  const listEl = $('chats-' + projectId);
  if (listEl) renderChatItems(projectId, listEl);
}

// ── Chat rename ───────────────────────────────────────────────────────────
async function renameChat(projectId, chatId, newName) {
  const resp = await api('PATCH', `/api/chats/${chatId}`, { name: newName });
  if (!resp || !resp.ok) return false;
  const updated = await resp.json();
  // Update local cache
  if (state.chats[projectId]) {
    const chat = state.chats[projectId].find(c => c.id === chatId);
    if (chat) chat.name = updated.name;
  }
  // If currently selected, update title bar
  if (state.selectedChatId === chatId) {
    chatTitle.textContent = stripUserSuffix(updated.name);
  }
  return true;
}

function startInlineRename(entry, nameSpan, projectId, chatId, currentName) {
  // Prevent re-entrancy
  if (entry.querySelector('input.chat-rename-input')) return;

  const input = document.createElement('input');
  input.type        = 'text';
  input.className   = 'chat-rename-input';
  input.value       = currentName;
  input.style.cssText = `
    width: 100%; border: none; outline: none; background: transparent;
    font: inherit; color: inherit; padding: 0; margin: 0;
  `;

  nameSpan.replaceWith(input);
  input.select();

  let committed = false;

  async function commit() {
    if (committed) return;
    committed = true;
    const newName = input.value.trim();
    if (newName && newName !== currentName) {
      await renameChat(projectId, chatId, newName);
    }
    // Refresh sidebar list (which rebuilds nameSpan)
    await loadChats(projectId);
    const listEl = $('chats-' + projectId);
    if (listEl) renderChatItems(projectId, listEl);
  }

  input.addEventListener('keydown', e => {
    if (e.key === 'Enter')  { e.preventDefault(); commit(); }
    if (e.key === 'Escape') {
      committed = true;   // cancel — just re-render
      loadChats(projectId).then(() => {
        const listEl = $('chats-' + projectId);
        if (listEl) renderChatItems(projectId, listEl);
      });
    }
  });
  input.addEventListener('blur', commit);
  input.addEventListener('click', e => e.stopPropagation());
}

// ── File attachment ───────────────────────────────────────────────────────
if (attachBtn && fileInput) {
  attachBtn.addEventListener('click', () => fileInput.click());
  fileInput.addEventListener('change', () => {
    for (const f of fileInput.files) {
      f.status = 'pending';
      state.pendingFiles.push(f);
    }
    fileInput.value = '';
    renderAttachList();
  });
}

function renderAttachList() {
  if (!attachList) return;
  attachList.innerHTML = '';
  for (let i = 0; i < state.pendingFiles.length; i++) {
    const f = state.pendingFiles[i];
    const chip = document.createElement('span');
    const chipStatus = f.status || 'pending';
    const icon = chipStatus === 'ingesting' ? '' : fileUploadIcon(chipStatus);
    chip.className = 'attach-chip attach-chip-' + chipStatus;
    chip.textContent = (icon ? icon + ' ' : '') + f.name;
    if (chipStatus === 'pending' || chipStatus === 'error' || chipStatus === 'warning') {
      const rm = document.createElement('button');
      rm.textContent = '\u00D7';
      rm.addEventListener('click', () => {
        state.pendingFiles.splice(i, 1);
        renderAttachList();
      });
      chip.appendChild(rm);
    }
    attachList.appendChild(chip);
  }
  if (attachList.parentElement)
    attachList.parentElement.style.display = state.pendingFiles.length ? '' : 'none';
}

async function uploadPendingFiles(chatId) {
  const files = [...state.pendingFiles];
  const uploaded = [];
  const errors = [];
  const uploadOne = async (f) => {
    const live = appendLiveFileUploadRow(f);
    f.status = 'uploading';
    renderAttachList();
    live.update({ status: 'uploading' });

    let ingestTimer = null;
    if (fileNeedsIngestion({ filename: f.name })) {
      ingestTimer = setTimeout(() => {
        f.status = 'ingesting';
        renderAttachList();
        live.update({ status: 'ingesting' });
      }, 500);
    }

    const fd = new FormData();
    fd.append('file', f);
    try {
      const resp = await fetch(`/api/chats/${chatId}/upload`, {
        method: 'POST',
        credentials: 'same-origin',
        body: fd,
      });
      if (ingestTimer) {
        clearTimeout(ingestTimer);
        ingestTimer = null;
      }
      if (!resp.ok) {
        f.status = 'error';
        renderAttachList();
        let errMsg = 'Upload failed';
        try { errMsg = (await resp.json()).error || errMsg; } catch (_) {}
        const failed = {
          filename: f.name,
          size: f.size,
          status: 'error',
          error: errMsg,
          needs_ingestion: fileNeedsIngestion({ filename: f.name }),
        };
        live.update(failed);
        state.messages.push({ role: 'file', content: JSON.stringify(failed), created_at: '' });
        throw new Error(`${f.name}: ${errMsg}`);
      }
      const data = await resp.json();
      const finalStatus = data.extraction_success === false ? 'warning' : 'done';
      const finalRecord = Object.assign({}, data, { status: finalStatus });
      f.status = finalStatus;
      live.update(finalRecord);
      state.messages.push({ role: 'file', content: JSON.stringify(finalRecord), created_at: data.created_at || '' });

      const idx = state.pendingFiles.indexOf(f);
      if (idx >= 0) state.pendingFiles.splice(idx, 1);
      renderAttachList();
      return data.filename || f.name;
    } catch (e) {
      if (ingestTimer) clearTimeout(ingestTimer);
      throw e;
    }
  };

  for (const f of files) {
    try {
      uploaded.push(await uploadOne(f));
    } catch (e) {
      errors.push(e && e.message ? e.message : String(e));
      const idx = state.pendingFiles.indexOf(f);
      if (idx >= 0) state.pendingFiles.splice(idx, 1);
      renderAttachList();
    }
  }

  renderAttachList();
  if (errors.length) {
    throw new Error(errors.join('; '));
  }
  return uploaded;
}

// ── Send message ──────────────────────────────────────────────────────────
async function sendMessage() {
  if (state.sending) return;
  const content = messageInput.value.trim();
  if ((!content && state.pendingFiles.length === 0) || !state.selectedChatId) return;

  state.sending      = true;
  messageInput.value = '';
  resizeTextarea();
  setInputEnabled(false);

  // Upload any queued file attachments first
  let uploadedFiles = [];
  try {
    uploadedFiles = await uploadPendingFiles(state.selectedChatId);
  } catch (e) {
    state.sending = false;
    messageInput.value = content;
    resizeTextarea();
    setInputEnabled(true);
    messagesEl.appendChild(buildMessageRow('error', 'Upload failed: ' + e.message));
    messagesEl.scrollTop = messagesEl.scrollHeight;
    return;
  }

  if (!content) {
    state.sending = false;
    setInputEnabled(true);
    messageInput.focus();
    messagesEl.scrollTop = messagesEl.scrollHeight;
    return;
  }

  // Build display content (append uploaded file names if any)
  let displayContent = content;
  if (uploa)ASSET"
R"ASSET(
dedFiles.length) {
    displayContent += '\n\n\uD83D\uDCCE ' + uploadedFiles.join(', ');
  }

  // Show user message immediately
  state.messages.push({ role: 'user', content: displayContent, created_at: '' });
  renderMessages(state.messages);

  const pendingContextRow = buildContextUsageRow('', true);
  messagesEl.appendChild(pendingContextRow);
  messagesEl.scrollTop = messagesEl.scrollHeight;

  const assistantTurn = createAssistantTurnRow();

  const abortCtrl = new AbortController();
  let contextUsageText = '';
  let contextRecorded = false;
  let compressionStatus = null;
  let compressionRecorded = false;
  let queueStatus = null;
  let activityStatus = null;

  try {
    const resp = await fetch(`/api/chats/${state.selectedChatId}/messages/stream`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      credentials: 'same-origin',
      body: JSON.stringify({
        content,
        attachments: uploadedFiles,
        selected_agentic_mode_id: state.selectedChatAgenticModeId || '',
        web_debug: isWebDebuggingEnabled(),
      }),
      signal: abortCtrl.signal,
    });

    if (resp.status === 401) { window.location.href = '/login'; return; }

    if (!resp.ok) {
      let errMsg = 'Request failed';
      try { errMsg = (await resp.json()).error || errMsg; } catch (_) {}
      assistantTurn.remove();
      const errRow = buildMessageRow('error', '⚠ ' + errMsg);
      messagesEl.appendChild(errRow);
      return;
    }

    // Consume SSE stream
    let errorMsg = null;
    const streamState = await readSSEStream(resp, ev => {
      if (ev.delta !== undefined) {
        assistantTurn.appendTextDelta(ev.delta);
      } else if (ev.web_debug) {
        insertWebDebugMessage({
          system_prompt: ev.system_prompt || '',
          user_prompt: ev.user_prompt || '',
          request_messages: ev.request_messages || [],
        });
      } else if (ev.queue_state !== undefined) {
        const record = {
          state: ev.queue_state,
          provider: ev.queue_provider || '',
          position: ev.queue_position,
          depth: ev.queue_depth,
          active: ev.queue_active,
          maxActive: ev.queue_max_active,
        };
        if (record.state === 'queued') {
          if (!queueStatus) {
            queueStatus = createQueueStatusRow(record);
            messagesEl.insertBefore(queueStatus.row, pendingContextRow);
          } else {
            queueStatus.update(record);
          }
        } else if (queueStatus && queueStatus.row.parentNode) {
          queueStatus.row.remove();
          queueStatus = null;
        }
        messagesEl.scrollTop = messagesEl.scrollHeight;
      } else if (ev.activity_status !== undefined) {
        const record = {
          code: ev.activity_status,
          text: ev.activity_message || 'Working with the provider...',
          status: 'live',
        };
        if (!activityStatus) {
          activityStatus = createActivityStatusRow(record);
          messagesEl.insertBefore(activityStatus.row, pendingContextRow);
        } else {
          activityStatus.update(record);
        }
        messagesEl.scrollTop = messagesEl.scrollHeight;
      } else if (ev.tool_event !== undefined || ev.tool_name !== undefined) {
        const record = {
          tool_call_id: ev.tool_call_id || '',
          tool_name: ev.tool_name || 'Tool',
          arguments: ev.tool_arguments || '',
          result: ev.tool_result || '',
          status: ev.tool_status || (ev.tool_event === 'start' ? 'live' : 'done'),
        };
        assistantTurn.upsertTool(record);
        messagesEl.scrollTop = messagesEl.scrollHeight;
      } else if (ev.compression_status !== undefined) {
        const isDone = ev.compression_status === 'compression_done';
        const message = ev.compression_message || (isDone
          ? 'Context window compressed.'
          : 'Compressing context window...');
        if (!compressionStatus) {
          compressionStatus = createCompressionRow({
            text: message,
            status: isDone ? 'done' : 'live',
          });
          messagesEl.insertBefore(compressionStatus.row, pendingContextRow);
        } else if (isDone) {
          compressionStatus.finalize(message);
        } else {
          compressionStatus.update({ text: message, status: 'live' });
        }
        if (isDone && !compressionRecorded) {
          state.messages.push({
            role: 'compression',
            content: JSON.stringify({ text: message, status: 'done' }),
            created_at: '',
          });
          compressionRecorded = true;
        }
        messagesEl.scrollTop = messagesEl.scrollHeight;
      } else if (ev.ctx_used !== undefined) {
        contextUsageText = formatContextUsageText(ev.ctx_used, ev.ctx_total);
        pendingContextRow.classList.remove('is-pending');
        const valueEl = pendingContextRow.querySelector('.context-usage-value');
        if (valueEl) valueEl.textContent = contextUsageText.replace(/^CTX:\s*/, '');
        if (!contextRecorded) {
          state.messages.push({ role: 'context', content: contextUsageText, created_at: '' });
          contextRecorded = true;
        }
        messagesEl.scrollTop = messagesEl.scrollHeight;
      } else if (ev.done) {
        // done event — finalize handled below
      } else if (ev.error) {
        errorMsg = ev.error;
      }
    }, abortCtrl.signal);

    if (!errorMsg && !streamState.completed && !streamState.aborted) {
      errorMsg = 'The response stream ended before the server sent a completion event.';
    }

    if (errorMsg) {
      const partialAssistant = assistantTurn.finalize();
      if (assistantTurn.hasContent()) {
        state.messages.push({
          role: 'assistant',
          content: partialAssistant.text,
          ui_trace: partialAssistant.ui_trace,
          created_at: '',
        });
      } else {
        assistantTurn.remove();
      }
      const errRow = buildMessageRow('error', '⚠ ' + errorMsg);
      messagesEl.appendChild(errRow);
    } else {
      const finalAssistant = assistantTurn.finalize();
      state.messages.push({
        role: 'assistant',
        content: finalAssistant.text,
        ui_trace: finalAssistant.ui_trace,
        created_at: '',
      });
    }

  } catch (e) {
    if (e.name !== 'AbortError') {
      const partialAssistant = assistantTurn.finalize();
      if (assistantTurn.hasContent()) {
        state.messages.push({
          role: 'assistant',
          content: partialAssistant.text,
          ui_trace: partialAssistant.ui_trace,
          created_at: '',
        });
      } else {
        assistantTurn.remove();
      }
      if (queueStatus && queueStatus.row.parentNode) {
        queueStatus.row.remove();
      }
      if (activityStatus && activityStatus.row.parentNode) {
        activityStatus.row.remove();
      }
      if (compressionStatus && !compressionRecorded && compressionStatus.row.parentNode) {
        compressionStatus.row.remove();
      }
      messagesEl.appendChild(buildMessageRow('error', '⚠ Network error: ' + e.message));
    }
  } finally {
    if (!contextUsageText && pendingContextRow.parentNode) {
      pendingContextRow.remove();
    }
    if (queueStatus && queueStatus.row.parentNode) {
      queueStatus.row.remove();
    }
    if (activityStatus && activityStatus.row.parentNode) {
      activityStatus.row.remove();
    }
    if (compressionStatus && !compressionRecorded && compressionStatus.row.parentNode) {
      compressionStatus.row.remove();
    }
    state.sending = false;
    setInputEnabled(true);
    messageInput.focus();
    messagesEl.scrollTop = messagesEl.scrollHeight;
  }
}

sendBtn.addEventListener('click', sendMessage);
messageInput.addEventListener('keydown', e => {
  if (e.key === 'Enter' && !e.shiftKey) {
    e.preventDefault();
    sendMessage();
  }
});

// ── Textarea auto-resize ──────────────────────────────────────────────────
function resizeTextarea() {
  messageInput.style.height = 'auto';
  const maxH = Math.floor(window.innerHeight / 3);
  messageInput.style.height = Math.min(messageInput.scrollHeight, maxH) + 'px';
}
messageInput.addEventListener('input', resizeTextarea);

function setInputEnabled(enabled) {
  messageInput.disabled = !enabled;
  sendBtn.disabled      = !enabled || !state.selectedChatId;
  if (attachBtn) attachBtn.disabled = !enabled;
}

// ── Utilities ─────────────────────────────────────────────────────────────
function escapeHtml(str) {
  str = str == null ? '' : String(str);
  return str.replace(/&/g,'&amp;').replace(/</g,'&lt;')
            .replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

function escapeRegExp(str) {
  return String(str).replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

// ── Initialisation ────────────────────────────────────────────────────────
async function init() {
  const [meResp, projResp] = await Promise.all([
    api('GET', '/api/me'),
    api('GET', '/api/projects'),
  ]);
  if (!meResp || !projResp) return;
  if (!projResp.ok) { window.location.href = '/login'; return; }

  if (meResp.ok) {
    const me = await meResp.json();
    // Redirect immediately if admin has flagged a password reset
    if (me.force_password_reset) {
      window.location.href = '/change-password';
      return;
    }
    state.username = me.username || '';
    state.displayName = me.display_name || '';
    state.email = me.email || '';
    headerUser.textContent = state.displayName || me.username || '';
  }

  state.projects = await projResp.json();
  renderProjectList();
  emptyState.style.display = state.projects.length ? '' : 'flex';
  if (state.projects.length === 0)
    emptyState.querySelector('p').textContent =
      'No projects are assigned to your account yet. Ask your administrator.';
}

init();
)ASSET";

const char kThemeDefaultCss[] =
R"ASSET(
/* ─────────────────────────────────────────────────────────────────────────
   Default Light Theme — overrides CSS custom properties defined in base.css
   ───────────────────────────────────────────────────────────────────────── */
:root {
  /* Backgrounds */
  --color-bg-page:            #f5f7fa;
  --color-bg-sidebar:         #1e2433;
  --color-bg-sidebar-hover:   #2a3245;
  --color-bg-sidebar-active:  #2563eb;
  --color-bg-header:          #1e2433;
  --color-bg-main:            #ffffff;
  --color-bg-message-user:    #e8f0fe;
  --color-bg-message-model:   #ffffff;
  --color-bg-thinking:        #fffbeb;
  --color-bg-tool-call:       #f3f4f6;
  --color-bg-code:            #1e1e1e;
  --color-bg-input:           #ffffff;
  --color-bg-button-primary:  #2563eb;
  --color-bg-button-hover:    #1d4ed8;
  --color-bg-button-danger:   #dc2626;

  /* Text */
  --color-text-primary:       #111827;
  --color-text-secondary:     #4b5563;
  --color-text-muted:         #9ca3af;
  --color-text-thinking:      #92400e;
  --color-text-code:          #d4d4d4;
  --color-text-sidebar:       #cbd5e1;
  --color-text-sidebar-active:#ffffff;
  --color-text-button:        #ffffff;
  --color-text-error:         #dc2626;
  --color-text-link:          #2563eb;

  /* Accents */
  --color-accent-primary:     #2563eb;
  --color-accent-hover:       #1d4ed8;
  --color-accent-thinking:    #d97706;
  --color-accent-danger:      #dc2626;
  --color-accent-success:     #16a34a;
  --color-accent-warning:     #d97706;

  /* Borders */
  --color-border-main:        #e5e7eb;
  --color-border-subtle:      #f3f4f6;
  --color-border-input:       #d1d5db;
  --color-border-focus:       #2563eb;

  /* Typography */
  --font-body:      "Segoe UI", system-ui, -apple-system, sans-serif;
  --font-mono:      "Cascadia Code", "Consolas", "Courier New", monospace;
  --font-size-base: 14px;
  --font-size-small:12px;
  --font-size-large:16px;

  /* Shape */
  --radius-message: 12px;
  --radius-button:  6px;
  --radius-input:   6px;
  --radius-card:    8px;
}
)ASSET";

const char kThemeDefaultJson[] =
R"ASSET(
{
  "name": "Default Light",
  "author": "Nardana Inc.",
  "version": "1.0",
  "description": "Clean, professional light theme"
}
)ASSET";

const char kThemeDarkCss[] =
R"ASSET(
/* ─────────────────────────────────────────────────────────────────────────
   Dark Theme — overrides CSS custom properties for a dark UI
   ───────────────────────────────────────────────────────────────────────── */
:root {
  /* Backgrounds */
  --color-bg-page:            #1e1e1e;
  --color-bg-sidebar:         #252526;
  --color-bg-sidebar-hover:   #2d2d30;
  --color-bg-sidebar-active:  #094771;
  --color-bg-header:          #2d2d2d;
  --color-bg-main:            #1e1e1e;
  --color-bg-message-user:    #1a3a5c;
  --color-bg-message-model:   #2d2d2d;
  --color-bg-thinking:        #3d3520;
  --color-bg-tool-call:        #2d2d30;
  --color-bg-code:            #1e1e1e;
  --color-bg-input:           #3c3c3c;
  --color-bg-button-primary:  #0e639c;
  --color-bg-button-hover:    #1177bb;
  --color-bg-button-danger:   #c42b1c;

  /* Text */
  --color-text-primary:       #d4d4d4;
  --color-text-secondary:     #9d9d9d;
  --color-text-muted:         #656565;
  --color-text-thinking:      #e0c070;
  --color-text-code:          #d4d4d4;
  --color-text-sidebar:       #cccccc;
  --color-text-sidebar-active:#ffffff;
  --color-text-button:        #ffffff;
  --color-text-error:         #f48771;
  --color-text-link:          #4fc3f7;

  /* Accents */
  --color-accent-primary:     #4fc3f7;
  --color-accent-hover:       #7dd5f5;
  --color-accent-thinking:    #c9a227;
  --color-accent-danger:      #f48771;
  --color-accent-success:     #2ea043;
  --color-accent-warning:     #d29922;

  /* Borders */
  --color-border-main:        #3c3c3c;
  --color-border-subtle:      #2d2d2d;
  --color-border-input:       #3c3c3c;
  --color-border-focus:       #4fc3f7;

  /* Typography */
  --font-body:      "Segoe UI", system-ui, -apple-system, sans-serif;
  --font-mono:      "Cascadia Code", "Consolas", "Courier New", monospace;
  --font-size-base: 14px;
  --font-size-small:12px;
  --font-size-large:16px;

  /* Shape */
  --radius-message: 8px;
  --radius-button:  4px;
  --radius-input:   4px;
  --radius-card:    6px;
}
)ASSET";

const char kThemeDarkJson[] =
R"ASSET(
{
  "name": "Dark",
  "author": "Nardana Inc.",
  "version": "1.0",
  "description": "Dark theme"
}
)ASSET";

} // namespace DefaultWebAssets
