// AUTO-GENERATED — do not edit by hand.
// Including the header first gives const variables external linkage.
#include "web_assets_default.h"

namespace DefaultWebAssets {

const char kIndexHtml[] = R"ASSET(<!DOCTYPE html>
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
      <span id="header-username"></span>
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
        <button id="attach-btn" title="Attach file" disabled>📎</button>
        <input id="file-input" type="file" multiple style="display:none">
        <textarea id="message-input" placeholder="Type a message…"
                  rows="1" disabled></textarea>
        <button id="send-btn" disabled>Send</button>
      </div>
    </main>
  </div>
</div>

<script src="/js/vendor/highlight.min.js"></script>
<script src="/js/vendor/marked.min.js"></script>
<script src="/js/vendor/purify.min.js"></script>
<script src="/js/app.js"></script>
</body>
</html>
)ASSET";

const char kLoginHtml[] = R"ASSET(<!DOCTYPE html>
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
      <form id="login-form" autocomplete="on">
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
        <button type="submit" class="login-btn" id="login-btn">Sign In</button>
        <div id="login-error"></div>
      </form>
    </div>
  </div>

  <script>
    document.getElementById('login-form').addEventListener('submit', async function(e) {
      e.preventDefault();
      const btn   = document.getElementById('login-btn');
      const errEl = document.getElementById('login-error');
      const username = document.getElementById('username').value.trim();
      const password = document.getElementById('password').value;

      btn.disabled   = true;
      btn.textContent = 'Signing in…';
      errEl.style.display = 'none';

      try {
        const resp = await fetch('/login', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ username, password })
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
          errEl.textContent   = data.error || 'Login failed.';
          errEl.style.display = 'block';
          btn.disabled        = false;
          btn.textContent     = 'Sign In';
        }
      } catch (err) {
        errEl.textContent   = 'Network error — is the server running?';
        errEl.style.display = 'block';
        btn.disabled        = false;
        btn.textContent     = 'Sign In';
      }
    });
  </script>
</body>
</html>
)ASSET";

const char kChangePasswordHtml[] = R"ASSET(<!DOCTYPE html>
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
      <form id="cp-form" autocomplete="off">
        <div class="form-group" id="current-group">
          <label for="current-password">Current Password</label>
          <input type="password" id="current-password" name="current-password"
                 autocomplete="current-password">
        </div>
        <div class="form-group">
          <label for="new-password">New Password</label>
          <input type="password" id="new-password" name="new-password"
                 autocomplete="new-password" autofocus required
                 placeholder="Minimum 8 characters">
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

  <script>
    // Check if this is a forced reset (no current password needed) by peeking
    // at the URL param or the login redirect.  The server indicates forced reset
    // via the /login response body; we store it in sessionStorage for one page hop.
    const forced = sessionStorage.getItem('force_password_reset') === 'true';
    if (forced) {
      document.getElementById('current-group').style.display = 'none';
      document.getElementById('subtitle').textContent =
        'Please set a new password before continuing.';
      sessionStorage.removeItem('force_password_reset');
    }

    document.getElementById('cp-form').addEventListener('submit', async function(e) {
      e.preventDefault();
      const btn     = document.getElementById('cp-btn');
      const errEl   = document.getElementById('cp-error');
      const current = document.getElementById('current-password').value;
      const newPw   = document.getElementById('new-password').value;
      const confirm = document.getElementById('confirm-password').value;

      errEl.style.display = 'none';

      if (newPw.length < 8) {
        errEl.textContent   = 'Password must be at least 8 characters.';
        errEl.style.display = 'block';
        return;
      }
      if (newPw !== confirm) {
        errEl.textContent   = 'Passwords do not match.';
        errEl.style.display = 'block';
        return;
      }

      btn.disabled    = true;
      btn.textContent = 'Updating…';

      try {
        const body = { new_password: newPw };
        if (!forced) body.current_password = current;

        const resp = await fetch('/api/change-password', {
          method:      'POST',
          headers:     { 'Content-Type': 'application/json' },
          credentials: 'same-origin',
          body:        JSON.stringify(body),
        });
        const data = await resp.json();
        if (resp.ok) {
          window.location.href = '/';
        } else {
          errEl.textContent   = data.error || 'Failed to update password.';
          errEl.style.display = 'block';
          btn.disabled        = false;
          btn.textContent     = 'Update Password';
        }
      } catch (err) {
        errEl.textContent   = 'Network error — please try again.';
        errEl.style.display = 'block';
        btn.disabled        = false;
        btn.textContent     = 'Update Password';
      }
    });
  </script>
</body>
</html>
)ASSET";

const char kBaseCss[] = R"ASSET(/* ─────────────────────────────────────────────────────────────────────────
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
.message-role-label {
  font-size: 11px;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.05em;
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

/* Streaming indicator */
.streaming-cursor {
  display: inline-block;
  width: 8px; height: 1em;
  background: var(--color-accent-primary);
  vertical-align: text-bottom;
  animation: blink 0.7s step-end infinite;
}
@keyframes blink { 50% { opacity: 0; } }

/* Error message */
.message-row.error .message-bubble {
  background: #fef2f2;
  border: 1px solid #fecaca;
  color: var(--color-text-error);
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

/* ── Compose bar ──────────────────────────────────────────────────────── */
#compose {
  padding: 12px 20px 16px;
  border-top: 1px solid var(--color-border-main);
  display: flex;
  gap: 10px;
  align-items: flex-end;
  flex-shrink: 0;
  background: var(--color-bg-main);
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
  max-height: 160px;
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
#login-error {
  margin-top: 14px;
  padding: 9px 12px;
  background: #fef2f2;
  border-radius: var(--radius-input);
  color: var(--color-text-error);
  font-size: var(--font-size-small);
  display: none;
}
)ASSET";

const char kAppJs[] =
    R"ASSET(/* ─────────────────────────────────────────────────────────────────────────
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

// ── State ─────────────────────────────────────────────────────────────────
let state = {
  projects:          [],
  selectedProjectId: null,
  selectedChatId:    null,
  chats:             {},
  messages:          [],
  sending:           false,
  username:          '',
  pendingFiles:      [],     // File objects queued for upload before send
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
const attachBtn    = $('attach-btn');
const fileInput    = $('file-input');
const attachList   = $('attach-list');

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

// ── Markdown rendering ────────────────────────────────────────────────────
function renderMarkdown(text) {
  const raw = marked.parse(text || '');
  return DOMPurify.sanitize(raw, {
    ADD_TAGS: ['details', 'summary'],
    FORBID_ATTR: ['onerror', 'onload'],
  });
}

// ── Message rendering ─────────────────────────────────────────────────────
function renderMessages(messages) {
  messagesEl.innerHTML = '';
  if (messages.length === 0) {
    messagesEl.appendChild(emptyState);
    return;
  }
  for (const msg of messages) messagesEl.appendChild(buildMessageRow(msg.role, msg.content));
  messagesEl.scrollTop = messagesEl.scrollHeight;
}

function buildMessageRow(role, content) {
  const row = document.createElement('div');
  row.className = 'message-row ' + (role === 'user' ? 'user' : 'model');

  const lbl = document.createElement('div');
  lbl.className = 'message-role-label';
  lbl.textContent = role === 'user' ? 'You' : role === 'error' ? '⚠' : 'Assistant';

  const bubble = document.createElement('div');
  bubble.className = 'message-bubble' + (role === 'error' ? ' error' : '');

  if (role === 'assistant') {
    bubble.innerHTML = renderMarkdown(content);
    bubble.querySelectorAll('pre code:not(.hljs)').forEach(el => hljs.highlightElement(el));
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

// Create a streaming placeholder row.  Returns {row, bubble, updateText, finalize}.
function createStreamingRow() {
  const row = document.createElement('div');
  row.className = 'message-row model';
  row.id = 'streaming-row';

  const lbl = document.createElement('div');
  lbl.className = 'message-role-label';
  lbl.textContent = 'Assistant';

  const bubble = document.createElement('div');
  bubble.className = 'message-bubble streaming';
  bubble.id = 'streaming-bubble';
  bubble.innerHTML = '<span class="streaming-cursor"></span>';

  row.appendChild(lbl);
  row.appendChild(bubble);
  messagesEl.appendChild(row);
  messagesEl.scrollTop = messagesEl.scrollHeight;

  let accumulated = '';

  function updateText(delta) {
    accumulated += delta;
    // Show raw accumulated text while streaming (fast, no re-parse each token)
    bubble.textContent = accumulated;
    bubble.appendChild(Object.assign(document.createElement('span'),
      { className: 'streaming-cursor' }));
    messagesEl.scrollTop = messagesEl.scrollHeight;
  }

  function finalize(finalText) {
    row.removeAttribute('id');
    bubble.removeAttribute('id');
    bubble.classList.remove('streaming');
    const text = finalText !== undefined ? finalText : accumulated;
    bubble.innerHTML = renderMarkdown(text);
    bubble.querySelectorAll('pre code:not(.hljs)').forEach(el => hljs.highlightElement(el));
    messagesEl.scrollTop = messagesEl.scrollHeight;
    return text;
  }

  return { row, bubble, updateText, finalize, getAccumulated: () => accumulated };
}

// ── SSE stream reader ─────────────────────────────────────────────────────
// Reads a fetch() response body as a stream of SSE events.
// Calls onEvent(parsedObject) for each "data: {...}" line.
// Returns when the stream ends or the abort signal fires.
async function readSSEStream(response, onEvent, signal) {
  const reader = response.body.getReader();
  const decoder = new TextDecoder('utf-8');
  let buffer = '';

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
          onEvent(JSON.parse(jsonStr));
        } catch (_) { /* malformed event — skip */ }
      }
    }
    // Flush remaining buffer
    if (buffer.trim().startsWith('data:')) {
      const jsonStr = buffer.trim().slice(5).trim();
      if (jsonStr) {
        try { onEvent(JSON.parse(jsonStr)); } catch (_) {}
      }
    }
  } finally {
    reader.releaseLock();
  }
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
  document.querySelectorAll('.chat-entry').forEach(el =>
    el.classList.toggle('active', el.dataset.chatId === chatId));
  chatTitle.textContent = stripUserSuffix(chatName);
  messageInput.disabled = false;
  sendBtn.disabled      = false;
  newChatBtn.disabled   = false;
  await loadMessages(projectId, chatId);
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

// ── File attachment ─────────────────────────────────────────────────────)ASSET"
    R"ASSET(──
if (attachBtn && fileInput) {
  attachBtn.addEventListener('click', () => fileInput.click());
  fileInput.addEventListener('change', () => {
    for (const f of fileInput.files) state.pendingFiles.push(f);
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
    chip.className = 'attach-chip';
    chip.textContent = f.name;
    const rm = document.createElement('button');
    rm.textContent = '✕';
    rm.addEventListener('click', () => {
      state.pendingFiles.splice(i, 1);
      renderAttachList();
    });
    chip.appendChild(rm);
    attachList.appendChild(chip);
  }
  if (attachList.parentElement)
    attachList.parentElement.style.display = state.pendingFiles.length ? '' : 'none';
}

async function uploadPendingFiles(chatId) {
  const uploaded = [];
  for (const f of state.pendingFiles) {
    const fd = new FormData();
    fd.append('file', f);
    try {
      const resp = await fetch(`/api/chats/${chatId}/upload`, {
        method: 'POST',
        credentials: 'same-origin',
        body: fd,
      });
      if (resp.ok) {
        const data = await resp.json();
        uploaded.push(data.filename);
      }
    } catch (_) { /* upload failure is non-fatal */ }
  }
  state.pendingFiles = [];
  renderAttachList();
  return uploaded;
}

// ── Send message ──────────────────────────────────────────────────────────
async function sendMessage() {
  if (state.sending) return;
  const content = messageInput.value.trim();
  if (!content || !state.selectedChatId) return;

  state.sending      = true;
  messageInput.value = '';
  resizeTextarea();
  setInputEnabled(false);

  // Upload any queued file attachments first
  const uploadedFiles = await uploadPendingFiles(state.selectedChatId);

  // Build display content (append uploaded file names if any)
  let displayContent = content;
  if (uploadedFiles.length) {
    displayContent += '\n\n📎 ' + uploadedFiles.join(', ');
  }

  // Show user message immediately
  state.messages.push({ role: 'user', content: displayContent, created_at: '' });
  renderMessages(state.messages);

  // Create streaming row
  const { updateText, finalize, getAccumulated } = createStreamingRow();

  const abortCtrl = new AbortController();

  try {
    const resp = await fetch(`/api/chats/${state.selectedChatId}/messages/stream`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      credentials: 'same-origin',
      body: JSON.stringify({ content, attachments: uploadedFiles }),
      signal: abortCtrl.signal,
    });

    if (resp.status === 401) { window.location.href = '/login'; return; }

    if (!resp.ok) {
      let errMsg = 'Request failed';
      try { errMsg = (await resp.json()).error || errMsg; } catch (_) {}
      finalize('');
      const errRow = buildMessageRow('error', '⚠ ' + errMsg);
      const sr = $('streaming-row');
      if (sr) sr.replaceWith(errRow); else messagesEl.appendChild(errRow);
      return;
    }

    // Consume SSE stream
    let errorMsg = null;
    await readSSEStream(resp, ev => {
      if (ev.delta !== undefined) {
        updateText(ev.delta);
      } else if (ev.done) {
        // done event — finalize handled below
      } else if (ev.error) {
        errorMsg = ev.error;
      }
    }, abortCtrl.signal);

    if (errorMsg) {
      finalize('');
      const sr = $('streaming-row');
      const errRow = buildMessageRow('error', '⚠ ' + errorMsg);
      if (sr) sr.replaceWith(errRow); else messagesEl.appendChild(errRow);
    } else {
      const finalText = finalize();
      state.messages.push({ role: 'assistant', content: finalText, created_at: '' });
    }

  } catch (e) {
    if (e.name !== 'AbortError') {
      finalize('');
      const sr = $('streaming-row');
      if (sr) sr.remove();
      messagesEl.appendChild(buildMessageRow('error', '⚠ Network error: ' + e.message));
    }
  } finally {
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
  messageInput.style.height = Math.min(messageInput.scrollHeight, 160) + 'px';
}
messageInput.addEventListener('input', resizeTextarea);

function setInputEnabled(enabled) {
  messageInput.disabled = !enabled;
  sendBtn.disabled      = !enabled || !state.selectedChatId;
  if (attachBtn) attachBtn.disabled = !enabled;
}

// ── Utilities ─────────────────────────────────────────────────────────────
function escapeHtml(str) {
  return str.replace(/&/g,'&amp;').replace(/</g,'&lt;')
            .replace(/>/g,'&gt;').replace(/"/g,'&quot;');
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
    headerUser.textContent = me.display_name || me.username || '';
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

const char kThemeDefaultCss[] = R"ASSET(/* ─────────────────────────────────────────────────────────────────────────
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

const char kThemeDefaultJson[] = R"ASSET({
  "name": "Default Light",
  "author": "Nardana Inc.",
  "version": "1.0",
  "description": "Clean, professional light theme"
}
)ASSET";

} // namespace DefaultWebAssets
