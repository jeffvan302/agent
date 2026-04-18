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
  }
  bubble.querySelectorAll('pre code:not(.hljs)').forEach(el => {
    if (!isDiagramLanguage(getCodeLanguage(el))) {
      hljs.highlightElement(el);
    }
  });
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
    lang === 'vegalite' || lang === 'vega';
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
      spec = JSON.parse(source);
    } catch (err) {
      showDiagramError(host, 'Vega-Lite', err, source);
      return;
    }

    vegaEmbed(host, spec, { actions: false, renderer: 'svg' })
      .catch(err => showDiagramError(host, 'Vega-Lite', err, source));
  });
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
    bubble.innerHTML = renderMarkdown(accumulated, { streaming: true });
    postProcessMessageBubble(bubble, { renderDiagrams: false });
    bubble.appendChild(Object.assign(document.createElement('span'), {
      className: 'streaming-cursor',
    }));
    messagesEl.scrollTop = messagesEl.scrollHeight;
  }

  function finalize(finalText) {
    row.removeAttribute('id');
    bubble.removeAttribute('id');
    bubble.classList.remove('streaming');
    const text = finalText !== undefined ? finalText : accumulated;
    bubble.innerHTML = renderMarkdown(text);
    postProcessMessageBubble(bubble);
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
  if (attachBtn) attachBtn.disabled = false;
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

// ── File attachment ───────────────────────────────────────────────────────
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
    let icon = '';
    if (f.status === 'ingesting') icon = '⟳';
    else if (f.status === 'done') icon = '✓';
    else if (f.status === 'error') icon = '✗';
    else if (f.status === 'pending') icon = '○';
    chip.className = 'attach-chip attach-chip-' + (f.status || 'pending');
    chip.textContent = (icon ? icon + ' ' : '') + f.name;
    if (f.status === 'pending') {
      const rm = document.createElement('button');
      rm.textContent = '✕';
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
  const uploaded = [];
  for (const f of state.pendingFiles) {
    f.status = 'ingesting';
    renderAttachList();
    const fd = new FormData();
    fd.append('file', f);
    const resp = await fetch(`/api/chats/${chatId}/upload`, {
      method: 'POST',
      credentials: 'same-origin',
      body: fd,
    });
    if (!resp.ok) {
      f.status = 'error';
      renderAttachList();
      let errMsg = 'Upload failed';
      try { errMsg = (await resp.json()).error || errMsg; } catch (_) {}
      throw new Error(`${f.name}: ${errMsg}`);
    }
    const data = await resp.json();
    f.status = 'done';
    renderAttachList();
    uploaded.push(data.filename);
  }
  await new Promise(r => setTimeout(r, 800));
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
