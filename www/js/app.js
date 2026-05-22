
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

const renderer = new marked.Renderer();
renderer.link = function(href, title, text) {
  let out = '<a href="' + href + '"';
  out += ' target="_blank" rel="noopener noreferrer"';
  if (title) {
    out += ' title="' + title + '"';
  }
  out += '>' + text + '</a>';
  return out;
};
marked.use({ renderer });

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
  selectedChatModelProviderId: null,
  selectedChatModelId: null,
  projectAgenticModes:       [],
  projectDefaultAgenticModeId: '',
  projectEnabledAgenticModeIds: [],
  projectUserSelectModel: false,
  projectDefaultModel: null,
  projectSelectableModels: [],
  projectAllowManualCompress: false,
  projectEnableWebDebugging: false,
  projectEnableAutomation: false,
  automationSequence: [],
  automationPanelOpen: false,
  automationJobs: {},
  automationQuestionnaireSelections: {},
  automationStatusTimer: null,
  streamRuns: {},
  streamRunStatusTimer: null,
  selectedAutomationStepIndex: -1,
  webDebuggingActive: false,
  plannerEnabled: false,
  plannerPlan: null,
  plannerPath: '',
  plannerError: '',
  plannerExpanded: {},
  plannerRefreshTimer: null,
  activeAbortController: null,
  activeAbortControllers: {},
  followChatTail: true,
  autoScrollingMessages: false,
};

// ── DOM refs ──────────────────────────────────────────────────────────────
const $ = id => document.getElementById(id);
const bodyRow      = $('body-row');
const sidebar      = $('sidebar');
const sidebarResizer = $('sidebar-resizer');
const projectList  = $('project-list');
const newChatBtn   = $('new-chat-btn');
const plannerPanel = $('planner-panel');
const mobilePlannerPanel = $('mobile-planner-panel');
const mobileMenuBtn = $('mobile-menu-btn');
const mobilePlanBtn = $('mobile-plan-btn');
const mobilePanelBackdrop = $('mobile-panel-backdrop');
const chatTitle    = $('chat-title');
const messagesEl   = $('messages');
const emptyState   = $('empty-state');
const messageInput = $('message-input');
const sendBtn      = $('send-btn');
const headerUser   = $('header-username');
const headerAccountBtn = $('header-account-btn');
const agenticModeLabel = $('agentic-mode-label');
let   agenticModePicker = null;
const modelSelectLabel = $('model-select-label');
let   modelSelectPicker = null;
const compressBtn      = $('compress-btn');
const debugBtn         = $('debug-btn');
const automateBtn      = $('automate-btn');
let automationPanelEl = null;
let automationStepsEl = null;
let automationStatusEl = null;
let automationAddStepBtn = null;
let automationClearBtn = null;
let automationDoneBtn = null;
let automationSendBtn = null;
let automationRepeatEl = null;
let automationCompressEl = null;
let automationModelFieldEl = null;
let automationModelSelectEl = null;
const cancelAgentBtn   = $('cancel-agent-btn');
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

const SIDEBAR_WIDTH_STORAGE_KEY = 'agent.sidebar.width';
const CHAT_TAIL_EPSILON_PX = 48;
const THINKING_COLLAPSE_DELAY_MS = 1600;

// Keep following new output only while the user is already at the chat tail.
function isMessagesNearBottom() {
  if (!messagesEl) return true;
  const remaining = messagesEl.scrollHeight - messagesEl.scrollTop - messagesEl.clientHeight;
  return remaining <= CHAT_TAIL_EPSILON_PX;
}

function scrollMessagesToBottom(force = false) {
  if (!messagesEl) return;
  if (force) state.followChatTail = true;
  if (!force && !state.followChatTail) return;
  state.autoScrollingMessages = true;
  messagesEl.scrollTop = messagesEl.scrollHeight;
  window.setTimeout(() => {
    state.autoScrollingMessages = false;
  }, 0);
}

function scrollMessagesToBottomIfPinned() {
  scrollMessagesToBottom(false);
}

function restoreMessagesScroll(previousTop) {
  if (!messagesEl) return;
  const maxTop = Math.max(0, messagesEl.scrollHeight - messagesEl.clientHeight);
  messagesEl.scrollTop = Math.min(previousTop, maxTop);
}

if (messagesEl) {
  messagesEl.addEventListener('scroll', () => {
    if (state.autoScrollingMessages) return;
    state.followChatTail = isMessagesNearBottom();
  }, { passive: true });
}

function sidebarWidthBounds() {
  const viewport = Math.max(0, window.innerWidth || 0);
  const min = 220;
  const max = Math.max(min, Math.min(620, viewport ? viewport - 420 : 620));
  return { min, max };
}

function clampSidebarWidth(width) {
  const bounds = sidebarWidthBounds();
  const numeric = Number.isFinite(width) ? width : 280;
  return Math.max(bounds.min, Math.min(bounds.max, numeric));
}

function applySidebarWidth(width, persist = false) {
  if (!sidebar) return;
  const clamped = clampSidebarWidth(width);
  sidebar.style.setProperty('--sidebar-width', clamped + 'px');
  if (persist) {
    try { localStorage.setItem(SIDEBAR_WIDTH_STORAGE_KEY, String(Math.round(clamped))); } catch (_) {}
  }
}

function setupSidebarResizer() {
  if (!sidebar || !sidebarResizer) return;
  try {
    const saved = parseInt(localStorage.getItem(SIDEBAR_WIDTH_STORAGE_KEY) || '', 10);
    if (Number.isFinite(saved)) applySidebarWidth(saved, false);
  } catch (_) {}

  let resizing = false;
  const updateFromPointer = ev => {
    const origin = bodyRow ? bodyRow.getBoundingClientRect().left : 0;
    applySidebarWidth(ev.clientX - origin, true);
  };
  sidebarResizer.addEventListener('pointerdown', ev => {
    resizing = true;
    sidebarResizer.setPointerCapture(ev.pointerId);
    document.body.classList.add('sidebar-resizing');
    updateFromPointer(ev);
    ev.preventDefault();
  });
  sidebarResizer.addEventListener('pointermove', ev => {
    if (resizing) updateFromPointer(ev);
  });
  const finish = ev => {
    if (!resizing) return;
    resizing = false;
    try { sidebarResizer.releasePointerCapture(ev.pointerId); } catch (_) {}
    document.body.classList.remove('sidebar-resizing');
  };
  sidebarResizer.addEventListener('pointerup', finish);
  sidebarResizer.addEventListener('pointercancel', finish);
  window.addEventListener('resize', () => {
    applySidebarWidth(sidebar.getBoundingClientRect().width, true);
  });
}

setupSidebarResizer();

function isMobileLayout() {
  return window.matchMedia && window.matchMedia('(max-width: 720px)').matches;
}

function updateMobilePanelBackdrop() {
  if (!mobilePanelBackdrop) return;
  const open = document.body.classList.contains('mobile-sidebar-open');
  mobilePanelBackdrop.hidden = !open;
}

function setMobileSidebarOpen(open) {
  document.body.classList.toggle('mobile-sidebar-open', !!open);
  if (open) document.body.classList.remove('mobile-planner-open');
  if (mobileMenuBtn) {
    mobileMenuBtn.setAttribute('aria-expanded', open ? 'true' : 'false');
  }
  if (mobilePlanBtn) {
    mobilePlanBtn.setAttribute(
      'aria-expanded',
      document.body.classList.contains('mobile-planner-open') ? 'true' : 'false',
    );
  }
  updateMobilePanelBackdrop();
}

function setMobilePlannerOpen(open) {
  if (open && mobilePlanBtn && mobilePlanBtn.hidden) open = false;
  document.body.classList.toggle('mobile-planner-open', !!open);
  if (open) document.body.classList.remove('mobile-sidebar-open');
  if (open && mobilePlannerPanel) mobilePlannerPanel.scrollTop = 0;
  if (mobilePlanBtn) {
    mobilePlanBtn.setAttribute('aria-expanded', open ? 'true' : 'false');
  }
  if (mobileMenuBtn) {
    mobileMenuBtn.setAttribute(
      'aria-expanded',
      document.body.classList.contains('mobile-sidebar-open') ? 'true' : 'false',
    );
  }
  updateMobilePanelBackdrop();
}

function closeMobilePanels() {
  setMobileSidebarOpen(false);
  setMobilePlannerOpen(false);
}

if (mobileMenuBtn) {
  mobileMenuBtn.addEventListener('click', () => {
    setMobileSidebarOpen(!document.body.classList.contains('mobile-sidebar-open'));
  });
}
if (mobilePlanBtn) {
  mobilePlanBtn.addEventListener('click', () => {
    setMobilePlannerOpen(!document.body.classList.contains('mobile-planner-open'));
  });
}
if (mobilePanelBackdrop) {
  mobilePanelBackdrop.addEventListener('click', closeMobilePanels);
}
window.addEventListener('resize', () => {
  if (!isMobileLayout()) closeMobilePanels();
});

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
  if (e.key === 'Escape' &&
      (document.body.classList.contains('mobile-sidebar-open') ||
       document.body.classList.contains('mobile-planner-open'))) {
    closeMobilePanels();
  }
  if (e.key === 'Escape' && accountModal && !accountModal.hidden) {
    closeAccountModal();
  }
});

// ── Markdown rendering ────────────────────────────────────────────────────
function renderMarkdown(text, options = {}) {
  const extracted = extractThinkingBlocks(text || '', {
    open: !!(options.streaming || options.thinkingOpen),
  });
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

function extractThinkingBlocks(text, options = {}) {
  const blocks = [];
  const defaultOpen = !!options.open;
  let markdown = text.replace(/<think(?:ing)?>([\s\S]*?)<\/think(?:ing)?>/gi,
    function(_match, content) {
      const placeholder = 'THINKING_BLOCK_' + blocks.length + '_PLACEHOLDER';
      blocks.push({ placeholder, content: content.trim(), open: defaultOpen });
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

function wireThinkingBlocks(root, options = {}) {
  if (!root) return;
  const delay = Number(options.collapseThinkingAfterMs || 0);
  root.querySelectorAll('details.thinking-block').forEach(details => {
    details.addEventListener('toggle', () => {
      if (details.dataset.autoToggle === 'true') return;
      details.dataset.userToggled = 'true';
    });
    if (delay > 0 && details.open) {
      window.setTimeout(() => {
        if (!details.isConnected ||
            !details.open ||
            details.dataset.userToggled === 'true') {
          return;
        }
        details.dataset.autoToggle = 'true';
        details.open = false;
        scrollMessagesToBottomIfPinned();
        window.setTimeout(() => { delete details.dataset.autoToggle; }, 0);
      }, delay);
    }
  });
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
      spec = JSON.parse(source);
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
function renderMessages(messages, options = {}) {
  const previousTop = messagesEl.scrollTop;
  const shouldFollow = !!options.forceScrollBottom ||
    state.followChatTail ||
    isMessagesNearBottom();
  messagesEl.innerHTML = '';
  if (messages.length === 0) {
    messagesEl.appendChild(emptyState);
    scrollMessagesToBottom(!!options.forceScrollBottom);
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
      messagesEl.appendChild(buildAssistantTraceRow(
        msg.ui_trace,
        msg.content || '',
        msg.name || '',
        msg.created_at || ''
      ));
    } else {
      messagesEl.appendChild(buildMessageRow(
        msg.role,
        msg.content,
        msg.name,
        msg.created_at || ''
      ));
    }
  }
  if (typeof renderAutomationLiveResponse === 'function') {
    renderAutomationLiveResponse();
  }
  if (typeof renderStreamRunLiveResponse === 'function') {
    renderStreamRunLiveResponse();
  }
  if (shouldFollow) {
    scrollMessagesToBottom(true);
  } else {
    state.followChatTail = false;
    restoreMessagesScroll(previousTop);
  }
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
  card.appendChild(body);
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
    const download = record.download_url || record.absolute_download_url;
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
  scrollMessagesToBottom(true);
  return live;
}

function formatContextUsageText(used, total) {
  const usedText = Number(used || 0).toLocaleString();
  if (Number(total || 0) > 0) {
    return 'CTX: ' + usedText + ' / ' + Number(total).toLocaleString();
  }
  return 'CTX: ' + usedText;
}

function normalizeContextUsageRecord(content) {
  let record = content;
  if (typeof record === 'string') {
    try {
      const parsed = JSON.parse(record);
      if (parsed && typeof parsed === 'object') record = parsed;
    } catch (_) {}
  }
  if (typeof record === 'string') {
    return { text: record };
  }
  record = record || {};
  const used = record.used_tokens !== undefined ? record.used_tokens : record.ctx_used;
  const total = record.total_tokens !== undefined ? record.total_tokens : record.ctx_total;
  return {
    text: record.text || formatContextUsageText(used, total),
    used_tokens: used,
    total_tokens: total,
    request_id: record.request_id || '',
    provider_name: record.provider_name || '',
    model_id: record.model_id || '',
    mode_name: record.mode_name || '',
  };
}

function buildContextUsageRow(content, pending = false) {
  const record = normalizeContextUsageRecord(content || '');
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
  const text = record.text || '';
  value.textContent = text.indexOf('CTX:') === 0 ? text.slice(4).trim() : text;
  bubble.appendChild(value);

  const metaParts = [];
  if (record.provider_name || record.model_id) {
    metaParts.push([record.provider_name, record.model_id].filter(Boolean).join(' / '));
  }
  if (record.mode_name) metaParts.push(record.mode_name);
  if (record.request_id) metaParts.push(record.request_id);
  if (metaParts.length) {
    const meta = document.createElement('span');
    meta.style.cssText = 'font-size:11px;color:var(--color-text-muted);margin-left:6px;';
    meta.textContent = metaParts.join('  ·  ');
    bubble.appendChild(meta);
  }

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
    before_tokens: record.before_tokens,
    after_tokens: record.after_tokens,
    compressed_context_tokens: record.compressed_context_tokens,
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
    if (typeof record.before_tokens === 'number' && typeof record.after_tokens === 'number') {
      parts.push(
        Number(record.before_tokens).toLocaleString() +
        ' \u2192 ' +
        Number(record.after_tokens).toLocaleString() +
        ' tokens'
      );
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

function buildHistoryNoticeRow(content) {
  let record = content;
  if (typeof record === 'string') {
    try {
      const parsed = JSON.parse(record);
      if (parsed && typeof parsed === 'object') record = parsed;
    } catch (_) {}
  }
  record = record || {};

  const row = document.createElement('div');
  row.className = 'message-row history-notice';

  const bubble = document.createElement('div');
  bubble.className = 'history-notice-bubble';

  const hidden = Number(record.hidden_records || 0);
  const parts = [];
  parts.push(record.message || 'Earlier chat history is hidden.');
  if (hidden > 0) {
    parts.push(hidden + ' earlier record' + (hidden === 1 ? '' : 's') + ' available in debug view.');
  }
  if (record.debug_hint) parts.push(record.debug_hint);
  bubble.textContent = parts.join(' ');

  row.appendChild(bubble);
  return row;
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
      if (segment.type === 'questionnaire' || segment.questionnaire) {
        const source = segment.record && typeof segment.record === 'object'
          ? segment.record
          : segment;
        normalized.push({
          type: 'questionnaire',
          record: normalizeQuestionnaireRecord(source),
        });
        return;
      }
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

function normalizeQuestionnaireRecord(content) {
  let record = content;
  if (typeof record === 'string') {
    try {
      const parsed = JSON.parse(record);
      if (parsed && typeof parsed === 'object') record = parsed;
    } catch (_) {
      record = { question: record };
    }
  }
  record = record || {};
  return {
    toolCallId: record.tool_call_id || record.toolCallId || '',
    chatId: record.chat_id || record.chatId || state.selectedChatId || '',
    question: record.question || '',
    options: Array.isArray(record.options) ? record.options : [],
    allowMultiple: !!(record.allow_multiple || record.allowMultiple),
    selectedIndices: Array.isArray(record.selected_indices)
      ? record.selected_indices
      : (Array.isArray(record.selectedIndices) ? record.selectedIndices : []),
    selectedLabels: Array.isArray(record.selected_labels)
      ? record.selected_labels
      : (Array.isArray(record.selectedLabels) ? record.selectedLabels : []),
    submitted: !!record.submitted || record.status === 'done',
    status: record.status === 'done' ? 'done'
      : record.status === 'error' ? 'error'
      : 'live',
    startedAt: record.started_at || record.startedAt || record.created_at || record.createdAt || '',
    updatedAt: record.updated_at || record.updatedAt || '',
  };
}

function formatMessageTimestamp(value) {
  if (!value) return '';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return String(value);
  return date.toLocaleTimeString([], {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  });
}

function messageRoleLabel(role, modeName = '', createdAt = '') {
  const time = formatMessageTimestamp(createdAt);
  const suffix = time ? ' [' + time + ']' : '';
  if (role === 'user') return 'You' + suffix;
  if (role === 'error') return '\u26A0';
  if (role === 'assistant' && modeName) {
    return 'Assistant (' + modeName + ')' + suffix;
  }
  return 'Assistant' + suffix;
}

function prettyToolUsageText(value) {
  if (value == null) return '';
  if (typeof value === 'string') {
    const trimmed = value.trim();
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
    startedAt: record.started_at || record.startedAt || record.created_at || record.createdAt || '',
    updatedAt: record.updated_at || record.updatedAt || record.finished_at || record.finishedAt || '',
    status: record.status === 'error' ? 'error'
      : record.status === 'live' ? 'live'
      : 'done',
  };
}

function formatToolUsageSummary(record) {
  const timestamp = formatMessageTimestamp(
    record.status === 'live'
      ? (record.startedAt || record.updatedAt)
      : (record.updatedAt || record.startedAt)
  );
  const suffix = timestamp ? ' [' + timestamp + ']' : '';
  if (record.status === 'live') {
    return 'Using ' + record.toolName + '...' + suffix;
  }
  if (record.status === 'error') {
    return 'Tool error: ' + record.toolName + suffix;
  }
  return 'Used ' + record.toolName + suffix;
}

function formatToolLiveDescription(record) {
  if (record.status !== 'live') return '';
  if (record.arguments) {
    return 'Tool is running with the captured arguments. Waiting for the result...';
  }
  return 'Tool is running. Waiting for arguments or result...';
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

  const liveDescription = document.createElement('div');
  liveDescription.className = 'tool-usage-live-description';

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
  details.appendChild(liveDescription);
  details.appendChild(body);

  function render(nextRecord) {
    record = normalizeToolUsageRecord(nextRecord);
    details.className = 'tool-usage-block status-' + record.status;
    icon.className = 'tool-usage-icon status-' + record.status;
    icon.textContent = record.status === 'done' ? '\u2713'
      : record.status === 'error' ? '!' : '';
    text.textContent = formatToolUsageSummary(record);
    const liveText = formatToolLiveDescription(record);
    liveDescription.textContent = liveText;
    liveDescription.hidden = !liveText;
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
    } else if (segment.type === 'questionnaire') {
      wrap.appendChild(createQuestionnaireBlock(segment.record, options));
    } else {
      wrap.innerHTML = renderMarkdown(segment.content || '', {
        streaming: !!(options.streaming && segment.live),
        thinkingOpen: !!options.thinkingOpen,
      });
      postProcessMessageBubble(wrap, {
        renderDiagrams: !(options.streaming && segment.live),
      });
      wireThinkingBlocks(wrap, options);
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

function buildAssistantTraceRow(trace, fallbackContent = '', modeName = '', createdAt = '') {
  const row = document.createElement('div');
  row.className = 'message-row model';

  const lbl = document.createElement('div');
  lbl.className = 'message-role-label';
  lbl.textContent = messageRoleLabel('assistant', modeName, createdAt);

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

function createQuestionnaireBlock(content, options = {}) {
  const record = normalizeQuestionnaireRecord(content);
  if (!record.chatId && options.questionnaireChatId) {
    record.chatId = options.questionnaireChatId;
  }
  const box = document.createElement('div');
  box.className = 'questionnaire-inline';

  const question = document.createElement('div');
  question.className = 'automation-question';
  question.textContent = record.question || 'The agent needs input.';
  box.appendChild(question);

  const selected = new Set(record.selectedIndices || []);
  let submitted = !!record.submitted || record.status !== 'live';

  const buttons = document.createElement('div');
  buttons.className = 'automation-question-options';
  const optionButtons = [];

  const errorEl = document.createElement('div');
  errorEl.className = 'automation-status-error';
  errorEl.hidden = true;

  const status = document.createElement('div');
  status.className = 'questionnaire-inline-status';
  const selectedSummary = record.selectedLabels.length
    ? record.selectedLabels.join(', ')
    : (record.selectedIndices || [])
        .map(idx => record.options[idx])
        .filter(Boolean)
        .join(', ');
  status.textContent = submitted
    ? ('Answer submitted' + (selectedSummary ? ': ' + selectedSummary : '') + '.')
    : '';

  let confirmBtn = null;

  function setError(message) {
    errorEl.textContent = message || '';
    errorEl.hidden = !message;
  }

  function setDisabled(disabled) {
    optionButtons.forEach(btn => { btn.disabled = disabled; });
    if (confirmBtn) confirmBtn.disabled = disabled || selected.size === 0;
  }

  function updateButtons() {
    optionButtons.forEach((btn, idx) => {
      btn.classList.toggle('selected', selected.has(idx));
    });
    if (confirmBtn) {
      confirmBtn.disabled = submitted || selected.size === 0;
    }
  }

  async function submit(indices) {
    if (submitted) return;
    submitted = true;
    setError('');
    setDisabled(true);
    try {
      const submitter = options.onQuestionnaireSubmit || defaultQuestionnaireSubmit;
      await submitter(Object.assign({}, record, {
        selectedIndices: indices,
      }), indices);
      selected.clear();
      indices.forEach(idx => selected.add(idx));
      updateButtons();
      const submittedLabels = indices
        .map(idx => record.options[idx])
        .filter(Boolean)
        .join(', ');
      status.textContent = 'Answer submitted' + (submittedLabels ? ': ' + submittedLabels : '') + '.';
      if (confirmBtn) confirmBtn.textContent = 'Submitted';
    } catch (err) {
      submitted = false;
      setDisabled(false);
      setError(err && err.message ? err.message : 'Could not submit answer.');
    }
  }

  (record.options || []).forEach((label, idx) => {
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'automation-question-option';
    btn.textContent = label;
    btn.disabled = submitted;
    btn.addEventListener('click', async () => {
      if (record.allowMultiple) {
        if (selected.has(idx)) selected.delete(idx);
        else selected.add(idx);
        updateButtons();
      } else {
        selected.clear();
        selected.add(idx);
        updateButtons();
        await submit([idx]);
      }
    });
    optionButtons.push(btn);
    buttons.appendChild(btn);
  });

  if (record.allowMultiple) {
    confirmBtn = document.createElement('button');
    confirmBtn.type = 'button';
    confirmBtn.className = 'automation-question-confirm';
    confirmBtn.textContent = submitted ? 'Submitted' : 'Confirm';
    confirmBtn.disabled = submitted || selected.size === 0;
    confirmBtn.addEventListener('click', async () => {
      await submit(Array.from(selected).sort((a, b) => a - b));
    });
  }

  updateButtons();
  box.appendChild(buttons);
  if (confirmBtn) box.appendChild(confirmBtn);
  box.appendChild(status);
  box.appendChild(errorEl);
  return box;
}

async function defaultQuestionnaireSubmit(record, indices) {
  const chatId = record.chatId || state.selectedChatId;
  const toolCallId = record.toolCallId || '';
  if (!chatId || !toolCallId) {
    throw new Error('Questionnaire target is missing.');
  }
  const resp = await api('POST', '/api/questionnaire-response', {
    chat_id: chatId,
    tool_call_id: toolCallId,
    selected_indices: indices,
  });
  if (!resp || !resp.ok) {
    const data = resp ? await resp.json().catch(() => ({})) : {};
    throw new Error(data.error || 'Could not submit answer.');
  }
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
  details.open = title === 'Request Summary' || title === 'System Prompt';

  const summary = document.createElement('summary');
  summary.textContent = title;
  details.appendChild(summary);

  const pre = document.createElement('pre');
  pre.textContent = text && String(text).trim() ? String(text) : '(empty)';
  details.appendChild(pre);
  return details;
}

function formatDebugValue(value) {
  if (value === undefined || value === null || value === '') return '(empty)';
  if (typeof value === 'string') return value;
  try {
    return JSON.stringify(value, null, 2);
  } catch (_) {
    return String(value);
  }
}

function formatDebugMessages(messages) {
  if (!Array.isArray(messages) || !messages.length) return '(none)';
  return messages.map((msg, idx) => {
    const role = msg && msg.role ? msg.role : 'message';
    const content = msg && msg.content ? msg.content : '';
    return `#${idx + 1} [${role}]\n${content}`;
  }).join('\n\n---\n\n');
}

function formatDebugSections(sections) {
  if (!Array.isArray(sections) || !sections.length) return '(none)';
  return sections.map(section => {
    const title = section && (section.title || section.name) ? (section.title || section.name) : 'Section';
    const content = section && section.content ? section.content : '';
    return '## ' + title + '\n' + content;
  }).join('\n\n---\n\n');
}

function formatDebugTools(tools) {
  if (!Array.isArray(tools) || !tools.length) return '(none)';
  return tools.map((tool, idx) => {
    const name = tool && tool.name ? tool.name : ('tool_' + (idx + 1));
    const desc = tool && tool.description ? tool.description : '';
    const params = tool && tool.parameters !== undefined
      ? tool.parameters
      : (tool ? tool.parameters_json : '');
    const tokenLine = tool && tool.estimated_tokens !== undefined
      ? '\nEstimated tokens: ' + tool.estimated_tokens
      : '';
    return '#' + (idx + 1) + ' ' + name +
      tokenLine +
      '\n\nDescription:\n' + (desc || '(empty)') +
      '\n\nParameters:\n' + formatDebugValue(params);
  }).join('\n\n---\n\n');
}

function formatRequestSummary(record) {
  const provider = record.provider || {};
  const model = record.model || {};
  const mode = record.mode || {};
  const ctx = record.context_window || {};
  const parts = [];
  if (record.request_id) parts.push('Request: ' + record.request_id);
  const providerText = [provider.name, provider.id].filter(Boolean).join(' / ');
  const modelText = [model.display_name, model.id].filter(Boolean).join(' / ');
  const modeText = [mode.name, mode.id].filter(Boolean).join(' / ');
  if (providerText) parts.push('Provider: ' + providerText);
  if (modelText) parts.push('Model: ' + modelText);
  if (modeText) parts.push('Mode: ' + modeText);
  if (ctx.used_tokens !== undefined) {
    parts.push('Context: ' + formatContextUsageText(ctx.used_tokens, ctx.total_tokens));
  }
  if (ctx.message_count !== undefined) parts.push('Context messages: ' + ctx.message_count);
  if (ctx.tool_count !== undefined) parts.push('Tools: ' + ctx.tool_count);
  return parts.filter(Boolean).join('\n');
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
  bubble.appendChild(createDebugSection('Request Summary', formatRequestSummary(record)));
  bubble.appendChild(createDebugSection('System Prompt', record.system_prompt || ''));
  bubble.appendChild(createDebugSection('System Sections', formatDebugSections(record.system_sections)));
  bubble.appendChild(createDebugSection('Tools (not chat history)', formatDebugTools(record.tools)));
  bubble.appendChild(createDebugSection(
    'Context Messages (compressible)',
    formatDebugMessages(record.compressible_messages || record.request_messages)
  ));
  bubble.appendChild(createDebugSection(
    'Chat Records Not Sent',
    formatDebugMessages(record.non_model_chat_records)
  ));
  bubble.appendChild(createDebugSection('Project Settings', formatDebugValue(record.project_settings)));
  bubble.appendChild(createDebugSection('Compression', formatDebugValue(record.compression)));
  bubble.appendChild(createDebugSection('Notes', formatDebugValue(record.notes)));
  if (record.user_prompt !== undefined) {
    bubble.appendChild(createDebugSection('User Prompt', record.user_prompt || ''));
  }

  row.appendChild(lbl);
  row.appendChild(bubble);
  return row;
}

function buildMessageRow(role, content, modeName, createdAt = '') {
  if (role === 'file') return buildFileUploadRow(content);
  if (role === 'context') return buildContextUsageRow(content);
  if (role === 'compression') return buildCompressionRow(content);
  if (role === 'history_notice') return buildHistoryNoticeRow(content);
  if (role === 'tool_usage') return createToolUsageRow(content).row;
  if (role === 'web_debug') return buildWebDebugRow(content);

  const row = document.createElement('div');
  row.className = 'message-row ' + (role === 'user' ? 'user' : role === 'error' ? 'error' : 'model');

  const lbl = document.createElement('div');
  lbl.className = 'message-role-label';
  lbl.textContent = messageRoleLabel(role, modeName, createdAt);

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

function createAssistantTurnRow(initialTrace = [], modeName = '', createdAt = '') {
  const row = document.createElement('div');
  row.className = 'message-row model';
  row.id = 'streaming-row';

  const lbl = document.createElement('div');
  lbl.className = 'message-role-label';
  lbl.textContent = messageRoleLabel('assistant', modeName, createdAt);

  const bubble = document.createElement('div');
  bubble.className = 'message-bubble streaming';
  bubble.id = 'streaming-bubble';

  row.appendChild(lbl);
  row.appendChild(bubble);
  messagesEl.appendChild(row);
  scrollMessagesToBottom(true);

  let trace = normalizeAssistantTrace(initialTrace);

  function ensureAttached() {
    if (!row.isConnected) {
      messagesEl.appendChild(row);
    }
  }

  function render(streaming, renderOptions = {}) {
    ensureAttached();
    renderAssistantTraceBubble(bubble, trace, Object.assign({
      streaming,
      questionnaireChatId: state.selectedChatId,
    }, renderOptions));
    scrollMessagesToBottomIfPinned();
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
    const now = new Date().toISOString();
    next.startedAt = next.startedAt || now;
    next.updatedAt = next.updatedAt || now;
    let index = -1;
    if (next.toolCallId) {
      index = trace.findIndex(segment =>
        (segment.type === 'tool_usage' || segment.type === 'questionnaire') &&
        segment.record.toolCallId === next.toolCallId);
    }
    if (index < 0) {
      const last = trace[trace.length - 1];
      if (last && last.type === 'text') {
        last.live = false;
      }
      trace.push({ type: 'tool_usage', record: next });
    } else {
      if (trace[index].type === 'questionnaire') {
        const previous = trace[index].record || {};
        const questionnaire = Object.assign({}, previous, {
          status: next.status === 'error' ? 'error' : (next.status === 'live' ? 'live' : 'done'),
          submitted: next.status !== 'live',
          startedAt: next.startedAt || previous.startedAt || now,
          updatedAt: next.updatedAt || previous.updatedAt || now,
        });
        if (next.result) {
          try {
            const parsed = JSON.parse(next.result);
            if (parsed && typeof parsed === 'object') {
              if (Array.isArray(parsed.selected_indices)) questionnaire.selectedIndices = parsed.selected_indices;
              if (Array.isArray(parsed.selectedLabels)) questionnaire.selectedLabels = parsed.selectedLabels;
              if (Array.isArray(parsed.selected_labels)) questionnaire.selectedLabels = parsed.selected_labels;
            }
          } catch (_) {}
        }
        trace[index] = {
          type: 'questionnaire',
          record: questionnaire,
        };
        render(true);
        return;
      }
      const previous = trace[index].record || {};
      trace[index] = {
        type: 'tool_usage',
        record: Object.assign({}, previous, next, {
          startedAt: next.startedAt || previous.startedAt || now,
          updatedAt: next.updatedAt || previous.updatedAt || now,
        }),
      };
    }
    render(true);
  }

  function upsertQuestionnaire(record) {
    const next = normalizeQuestionnaireRecord(record);
    const now = new Date().toISOString();
    next.startedAt = next.startedAt || now;
    next.updatedAt = next.updatedAt || now;
    let index = -1;
    if (next.toolCallId) {
      index = trace.findIndex(segment =>
        (segment.type === 'questionnaire' || segment.type === 'tool_usage') &&
        segment.record.toolCallId === next.toolCallId);
    }
    if (index < 0) {
      const last = trace[trace.length - 1];
      if (last && last.type === 'text') {
        last.live = false;
      }
      trace.push({ type: 'questionnaire', record: next });
    } else {
      trace[index] = {
        type: 'questionnaire',
        record: Object.assign({}, trace[index].record || {}, next),
      };
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
      return { type: segment.type === 'questionnaire' ? 'questionnaire' : 'tool_usage', record };
    });
    render(false, {
      thinkingOpen: true,
      collapseThinkingAfterMs: THINKING_COLLAPSE_DELAY_MS,
    });
    return {
      text: trace
        .filter(segment => segment.type === 'text')
        .map(segment => segment.content || '')
        .join(''),
      ui_trace: trace.map(segment =>
        segment.type === 'text'
          ? { type: 'text', content: segment.content || '' }
          : Object.assign({ type: segment.type }, segment.record)),
    };
  }

  function hasContent() {
    return trace.some(segment =>
      segment.type === 'tool_usage' ||
      segment.type === 'questionnaire' ||
      (segment.type === 'text' && (segment.content || '').trim()));
  }

  function remove() {
    row.remove();
  }

  render(true);
  return { row, bubble, appendTextDelta, upsertTool, upsertQuestionnaire, finalize, hasContent, remove };
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



const PLANNER_SECTIONS = [
  { key: 'goals', label: 'Goals' },
  { key: 'features', label: 'Features' },
  { key: 'steps', label: 'Steps' },
  { key: 'blockers', label: 'Blockers' },
  { key: 'notes', label: 'Notes' },
];

const PLANNER_CHILD_SECTIONS = [
  { key: 'subgoals', label: 'Subgoals' },
  { key: 'goals', label: 'Goals' },
  { key: 'features', label: 'Features' },
  { key: 'steps', label: 'Steps' },
  { key: 'blockers', label: 'Blockers' },
  { key: 'notes', label: 'Notes' },
];

function plannerArray(value, key) {
  return value && Array.isArray(value[key]) ? value[key] : [];
}

function plannerItemId(item) {
  if (!item || typeof item !== 'object' || Array.isArray(item)) return '';
  if (item.id === undefined || item.id === null) return '';
  return String(item.id);
}

function plannerItemTitle(item, fallback) {
  if (item == null) return fallback;
  if (typeof item !== 'object' || Array.isArray(item)) return String(item);
  return String(
    item.title || item.task || item.name || item.summary ||
    item.text || item.description || item.content || fallback
  );
}

function plannerItemStatus(item) {
  if (!item || typeof item !== 'object' || Array.isArray(item)) return '';
  return String(item.status || '').trim();
}

function plannerStatusLabel(status) {
  return (status || 'pending').replace(/_/g, ' ');
}

function plannerChildGroups(item) {
  if (!item || typeof item !== 'object' || Array.isArray(item)) return [];
  return PLANNER_CHILD_SECTIONS
    .map(section => ({ key: section.key, label: section.label, items: plannerArray(item, section.key) }))
    .filter(group => group.items.length > 0);
}

function plannerHasContent(plan) {
  if (!plan || typeof plan !== 'object') return false;
  if (String(plan.goal || '').trim()) return true;
  return PLANNER_SECTIONS.some(section => plannerArray(plan, section.key).length > 0);
}

function plannerStatsForItems(items, stats) {
  for (const item of items) {
    if (!item || typeof item !== 'object' || Array.isArray(item)) continue;
    const status = plannerItemStatus(item);
    if (status) {
      stats.total += 1;
      if (status === 'completed') stats.completed += 1;
      if (status === 'in_progress') stats.inProgress += 1;
      if (status === 'blocked') stats.blocked += 1;
    }
    for (const group of plannerChildGroups(item)) {
      plannerStatsForItems(group.items, stats);
    }
  }
}

function plannerStats(plan) {
  const stats = { total: 0, completed: 0, inProgress: 0, blocked: 0 };
  if (!plan || typeof plan !== 'object') return stats;
  for (const section of PLANNER_SECTIONS) {
    plannerStatsForItems(plannerArray(plan, section.key), stats);
  }
  return stats;
}

function plannerIsExpanded(key, defaultOpen) {
  if (Object.prototype.hasOwnProperty.call(state.plannerExpanded, key)) {
    return !!state.plannerExpanded[key];
  }
  return !!defaultOpen;
}

function togglePlannerExpanded(key, defaultOpen) {
  state.plannerExpanded[key] = !plannerIsExpanded(key, defaultOpen);
  renderPlannerPanel();
}

function resetPlannerState() {
  state.plannerEnabled = false;
  state.plannerPlan = null;
  state.plannerPath = '';
  state.plannerError = '';
  renderPlannerPanel();
}

function applyPlannerPayload(data) {
  state.plannerEnabled = !!(data && data.enabled);
  state.plannerPlan = data && data.plan ? data.plan : null;
  state.plannerPath = data && data.path ? data.path : '';
  state.plannerError = '';
  renderPlannerPanel();
}

async function loadPlanner(projectId, chatId) {
  if (!plannerPanels().length || !chatId) {
    resetPlannerState();
    return;
  }
  const requestedChatId = chatId;
  try {
    const resp = await api('GET', `/api/chats/${encodeURIComponent(chatId)}/planner`);
    if (!resp || state.selectedChatId !== requestedChatId) return;
    if (!resp.ok) {
      const data = await resp.json().catch(() => ({}));
      state.plannerEnabled = false;
      state.plannerPlan = null;
      state.plannerError = data.error || 'Could not load planner.';
      renderPlannerPanel();
      return;
    }
    applyPlannerPayload(await resp.json());
  } catch (e) {
    if (state.selectedChatId !== requestedChatId) return;
    state.plannerEnabled = false;
    state.plannerPlan = null;
    state.plannerError = 'Could not load planner.';
    renderPlannerPanel();
  }
}

function schedulePlannerRefresh(delayMs = 250) {
  if (!plannerPanels().length || !state.selectedChatId) return;
  if (state.plannerRefreshTimer) clearTimeout(state.plannerRefreshTimer);
  state.plannerRefreshTimer = setTimeout(() => {
    state.plannerRefreshTimer = null;
    if (state.selectedProjectId && state.selectedChatId) {
      loadPlanner(state.selectedProjectId, state.selectedChatId);
    }
  }, delayMs);
}


async function updatePlannerItemStatus(id, status, checkbox) {
  if (!id || !state.selectedChatId) return;
  const previousChecked = checkbox ? checkbox.checked : false;
  if (checkbox) checkbox.disabled = true;
  try {
    const resp = await api('PATCH', `/api/chats/${encodeURIComponent(state.selectedChatId)}/planner/items/${encodeURIComponent(id)}`, {
      status,
    });
    if (!resp || !resp.ok) {
      const data = resp ? await resp.json().catch(() => ({})) : {};
      throw new Error(data.error || 'Could not update planner item.');
    }
    applyPlannerPayload(await resp.json());
  } catch (e) {
    if (checkbox) checkbox.checked = !previousChecked;
    state.plannerError = e && e.message ? e.message : 'Could not update planner item.';
    renderPlannerPanel();
  }
}

function plannerPanels() {
  return [plannerPanel, mobilePlannerPanel].filter(Boolean);
}

function plannerPanelShouldShow() {
  return !!(state.selectedChatId && (state.plannerEnabled || state.plannerError));
}

function renderMobilePlanButton() {
  if (!mobilePlanBtn) return;
  const visible = plannerPanelShouldShow();
  document.body.classList.toggle('mobile-has-plan', visible);
  mobilePlanBtn.hidden = !visible;
  if (!visible) {
    document.body.classList.remove('mobile-planner-open');
  }
  mobilePlanBtn.setAttribute(
    'aria-expanded',
    document.body.classList.contains('mobile-planner-open') ? 'true' : 'false',
  );
  updateMobilePanelBackdrop();
}

function renderPlannerPanel() {
  const panels = plannerPanels();
  if (!panels.length) return;
  for (const panel of panels) {
    renderPlannerPanelInto(panel);
  }
  renderMobilePlanButton();
}

function renderPlannerPanelInto(targetPanel) {
  targetPanel.innerHTML = '';

  if (!plannerPanelShouldShow()) {
    targetPanel.hidden = true;
    return;
  }

  const hasContent = plannerHasContent(state.plannerPlan);
  targetPanel.hidden = false;

  const header = document.createElement('div');
  header.className = 'planner-panel-header';
  const title = document.createElement('div');
  title.className = 'planner-panel-title';
  title.textContent = 'Plan';
  const refresh = document.createElement('button');
  refresh.type = 'button';
  refresh.className = 'planner-refresh-btn';
  refresh.textContent = 'Refresh';
  refresh.addEventListener('click', () => loadPlanner(state.selectedProjectId, state.selectedChatId));
  header.appendChild(title);
  header.appendChild(refresh);
  targetPanel.appendChild(header);

  if (state.plannerError) {
    const error = document.createElement('div');
    error.className = 'planner-error';
    error.textContent = state.plannerError;
    targetPanel.appendChild(error);
    return;
  }

  const stats = plannerStats(state.plannerPlan);
  if (stats.total > 0) {
    const summary = document.createElement('div');
    summary.className = 'planner-summary';
    const parts = [`${stats.completed}/${stats.total} completed`];
    if (stats.inProgress) parts.push(`${stats.inProgress} active`);
    if (stats.blocked) parts.push(`${stats.blocked} blocked`);
    summary.textContent = parts.join(' · ');
    targetPanel.appendChild(summary);
  }

  const goal = String((state.plannerPlan && state.plannerPlan.goal) || '').trim();
  if (goal) {
    const goalEl = document.createElement('div');
    goalEl.className = 'planner-goal-text';
    goalEl.textContent = goal;
    targetPanel.appendChild(goalEl);
  }

  if (!hasContent) {
    const empty = document.createElement('div');
    empty.className = 'planner-empty';
    empty.textContent = 'Planner is ready. No plan items yet.';
    targetPanel.appendChild(empty);
    return;
  }

  for (const section of PLANNER_SECTIONS) {
    const items = plannerArray(state.plannerPlan, section.key);
    if (!items.length) continue;
    renderPlannerSection(section, items, targetPanel);
  }
}

function renderPlannerSection(section, items, container) {
  const key = `section:${section.key}`;
  const open = plannerIsExpanded(key, true);
  const sectionEl = document.createElement('div');
  sectionEl.className = 'planner-section';

  const header = document.createElement('button');
  header.type = 'button';
  header.className = 'planner-section-header';
  header.addEventListener('click', () => togglePlannerExpanded(key, true));

  const arrow = document.createElement('span');
  arrow.className = 'planner-arrow' + (open ? ' open' : '');
  arrow.textContent = '▶';
  const name = document.createElement('span');
  name.className = 'planner-section-name';
  name.textContent = section.label;
  const count = document.createElement('span');
  count.className = 'planner-section-count';
  count.textContent = String(items.length);

  header.appendChild(arrow);
  header.appendChild(name);
  header.appendChild(count);
  sectionEl.appendChild(header);

  if (open) {
    const list = document.createElement('div');
    list.className = 'planner-items';
    renderPlannerItems(items, list, `${section.key}`, 0);
    sectionEl.appendChild(list);
  }

  container.appendChild(sectionEl);
}

function renderPlannerItems(items, container, path, depth) {
  items.forEach((item, index) => {
    const id = plannerItemId(item);
    const status = plannerItemStatus(item);
    const completed = status === 'completed';
    const children = plannerChildGroups(item);
    const itemKey = id ? `item:${id}` : `item:${path}:${index}`;
    const defaultOpen = status === 'in_progress' || depth === 0;
    const open = children.length ? plannerIsExpanded(itemKey, defaultOpen) : false;

    const itemEl = document.createElement('div');
    itemEl.className = 'planner-item' + (completed ? ' completed' : '') + (status ? ` ${status}` : '');

    const row = document.createElement('div');
    row.className = 'planner-item-row';
    row.style.paddingLeft = `${Math.min(depth, 4) * 8 + 4}px`;

    if (children.length) {
      const expander = document.createElement('button');
      expander.type = 'button';
      expander.className = 'planner-item-expander' + (open ? ' open' : '');
      expander.textContent = '▶';
      expander.addEventListener('click', e => {
        e.stopPropagation();
        togglePlannerExpanded(itemKey, defaultOpen);
      });
      row.appendChild(expander);
    } else {
      const spacer = document.createElement('span');
      spacer.className = 'planner-item-spacer';
      row.appendChild(spacer);
    }

    const checkbox = document.createElement('input');
    checkbox.type = 'checkbox';
    checkbox.className = 'planner-checkbox';
    checkbox.checked = completed;
    checkbox.disabled = !id;
    checkbox.title = id ? 'Mark completed' : 'Planner item has no id';
    checkbox.addEventListener('click', e => e.stopPropagation());
    checkbox.addEventListener('change', () => {
      updatePlannerItemStatus(id, checkbox.checked ? 'completed' : 'pending', checkbox);
    });
    row.appendChild(checkbox);

    const main = document.createElement('div');
    main.className = 'planner-item-main';

    const title = document.createElement('div');
    title.className = 'planner-item-title';
    title.textContent = plannerItemTitle(item, `Item ${index + 1}`);
    main.appendChild(title);

    if (item && typeof item === 'object' && !Array.isArray(item)) {
      const metaParts = [];
      if (item.done_when) metaParts.push(`Done: ${item.done_when}`);
      if (item.tool_hint) metaParts.push(`Tool: ${item.tool_hint}`);
      if (metaParts.length) {
        const meta = document.createElement('div');
        meta.className = 'planner-item-meta';
        meta.textContent = metaParts.join(' · ');
        main.appendChild(meta);
      }
    }

    if (status) {
      const badge = document.createElement('span');
      badge.className = 'planner-status ' + status;
      badge.textContent = plannerStatusLabel(status);
      main.appendChild(badge);
    }

    row.appendChild(main);
    itemEl.appendChild(row);

    if (children.length && open) {
      for (const group of children) {
        const childGroup = document.createElement('div');
        childGroup.className = 'planner-child-group';
        const label = document.createElement('div');
        label.className = 'planner-child-label';
        label.textContent = group.label;
        childGroup.appendChild(label);
        renderPlannerItems(group.items, childGroup, `${itemKey}:${group.key}`, depth + 1);
        itemEl.appendChild(childGroup);
      }
    }

    container.appendChild(itemEl);
  });
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
  state.selectedChatModelProviderId = null;
  state.selectedChatModelId = null;
  state.plannerExpanded = {};
  if (typeof renderSelectedAutomationJobStatus === 'function') {
    renderSelectedAutomationJobStatus();
  }
  document.querySelectorAll('.chat-entry').forEach(el =>
    el.classList.toggle('active', el.dataset.chatId === chatId));
  chatTitle.textContent = stripUserSuffix(chatName);
  const selectedStreamActive = !!activeControllerForChat(chatId);
  messageInput.disabled = selectedStreamActive;
  sendBtn.disabled      = selectedStreamActive;
  newChatBtn.disabled   = false;
  if (attachBtn) attachBtn.disabled = selectedStreamActive;
  await Promise.all([
    loadMessages(projectId, chatId, { forceScrollBottom: true }),
    loadProjectAgenticModes(projectId),
    loadPlanner(projectId, chatId),
  ]);
  renderAgenticModeLabel();
  if (typeof refreshAutomationStatusForChat === 'function') {
    refreshAutomationStatusForChat(chatId, { reloadMessages: false });
  }
  if (typeof refreshStreamStatusForChat === 'function') {
    refreshStreamStatusForChat(chatId, { reloadMessages: false });
  }
  refreshSelectedChatSendingState();
  if (isMobileLayout()) setMobileSidebarOpen(false);
}

async function loadProjectAgenticModes(projectId) {
  const resp = await api('GET', `/api/projects/${projectId}/agentic-modes`);
  if (!resp || !resp.ok) {
    state.projectAgenticModes = [];
    state.projectDefaultAgenticModeId = '';
    state.projectEnabledAgenticModeIds = [];
    state.projectUserSelectModel = false;
    state.projectDefaultModel = null;
    state.projectSelectableModels = [];
    state.projectEnableWebDebugging = false;
    setWebDebuggingActive(false);
    renderModelSelectLabel();
    populateAutomationModelSelect();
    return;
  }
  const data = await resp.json();
  state.projectDefaultAgenticModeId = data.default_id || '';
  state.projectEnabledAgenticModeIds = data.enabled_ids || [];
  state.projectAgenticModes = data.modes || [];
  state.projectUserSelectModel = !!data.user_select_model_enabled;
  state.projectDefaultModel = data.default_model || null;
  state.projectSelectableModels = Array.isArray(data.selectable_models) ? data.selectable_models : [];
  state.projectAllowManualCompress = data.allow_manual_context_compression || false;
  state.projectEnableWebDebugging = data.enable_web_debugging || false;
  state.projectEnableAutomation = data.enable_automation || false;
  if (!state.projectEnableWebDebugging) {
    setWebDebuggingActive(false);
  }

  // Load chat-level override from chat metadata (not available via API yet)
  for (const chat of (state.chats[projectId] || [])) {
    if (chat.id === state.selectedChatId) {
      state.selectedChatAgenticModeId = chat.selected_agentic_mode_id || null;
      state.selectedChatModelProviderId = chat.provider_id || null;
      state.selectedChatModelId = chat.model_id || null;
      break;
    }
  }
  renderModelSelectLabel();
  const selectedStep = state.selectedAutomationStepIndex >= 0
    ? state.automationSequence[state.selectedAutomationStepIndex]
    : null;
  populateAutomationModelSelect(
    selectedStep && selectedStep.provider_id,
    selectedStep && selectedStep.model_id);
}

function currentAgenticModeId() {
  if (state.selectedChatAgenticModeId != null) return state.selectedChatAgenticModeId || '';
  return state.projectDefaultAgenticModeId || '';
}

function currentAgenticModeName() {
  const activeId = currentAgenticModeId();
  const mode = state.projectAgenticModes.find(m => m.id === activeId);
  if (mode && mode.name) return mode.name;
  return activeId || 'Default';
}

function modelKey(providerId, modelId) {
  return String(providerId || '') + '\n' + String(modelId || '');
}

function findSelectableModel(providerId, modelId) {
  if (!providerId || !modelId) return null;
  return (state.projectSelectableModels || []).find(model =>
    model.provider_id === providerId && model.model_id === modelId) || null;
}

function currentModelSelection() {
  const selected = findSelectableModel(
    state.selectedChatModelProviderId,
    state.selectedChatModelId);
  if (selected) return selected;
  if (state.projectDefaultModel) return state.projectDefaultModel;
  return (state.projectSelectableModels && state.projectSelectableModels[0]) || null;
}

function currentModelLabel() {
  const model = currentModelSelection();
  return model ? (model.label || model.model_name || model.model_id || 'Default') : 'Default';
}

function setChatModelSelection(model) {
  if (!model) return;
  state.selectedChatModelProviderId = model.provider_id || '';
  state.selectedChatModelId = model.model_id || '';
  const chats = state.selectedProjectId ? (state.chats[state.selectedProjectId] || []) : [];
  for (const chat of chats) {
    if (chat.id === state.selectedChatId) {
      chat.provider_id = state.selectedChatModelProviderId;
      chat.model_id = state.selectedChatModelId;
      break;
    }
  }
  renderModelSelectLabel();
  populateAutomationModelSelect();
}

function renderModelSelectLabel() {
  if (!modelSelectLabel) return;
  const choices = state.projectSelectableModels || [];
  if (!state.selectedChatId || !state.projectUserSelectModel || choices.length === 0) {
    modelSelectLabel.style.display = 'none';
    modelSelectLabel.classList.add('disabled');
    modelSelectLabel.style.pointerEvents = 'none';
    return;
  }
  modelSelectLabel.style.display = '';
  modelSelectLabel.textContent = 'Model: ' + currentModelLabel();
  const canSwitch = choices.length > 1;
  modelSelectLabel.classList.toggle('disabled', !canSwitch);
  modelSelectLabel.style.pointerEvents = canSwitch ? '' : 'none';
  modelSelectLabel.title = canSwitch ? 'Click to change model' : '';
}

function closeModelPicker() {
  if (modelSelectPicker) {
    modelSelectPicker.remove();
    modelSelectPicker = null;
  }
}

function openModelSelectPicker() {
  if (agenticModePicker) { agenticModePicker.remove(); agenticModePicker = null; }
  if (!state.projectUserSelectModel || !modelSelectLabel) return;
  if (modelSelectPicker) { closeModelPicker(); return; }
  const items = state.projectSelectableModels || [];
  if (items.length < 2) return;
  const current = currentModelSelection();
  const currentKey = current ? modelKey(current.provider_id, current.model_id) : '';

  const menu = document.createElement('div');
  menu.id = 'model-select-picker';
  menu.style.cssText = `
    position:absolute; bottom:8px; left:20px; z-index:100;
    background:var(--color-bg-main); border:1px solid var(--color-border-main);
    border-radius:var(--radius-input); box-shadow:0 4px 12px rgba(0,0,0,0.15);
    max-height:260px; overflow:auto; min-width:260px; max-width:min(520px, calc(100vw - 24px));
  `;
  for (const item of items) {
    const row = document.createElement('div');
    row.style.cssText = `
      padding:8px 12px; cursor:pointer;
      color: var(--color-text-primary); font-size:var(--font-size-base);
      border-bottom:1px solid var(--color-border-main);
      white-space:normal; overflow-wrap:anywhere;
    `;
    row.textContent = item.label || item.model_name || item.model_id || 'Model';
    if (modelKey(item.provider_id, item.model_id) === currentKey) {
      row.style.fontWeight = '700';
      row.style.color = 'var(--color-accent-primary)';
    }
    row.addEventListener('mouseenter', () => { row.style.background = 'var(--color-bg-sidebar-hover)'; });
    row.addEventListener('mouseleave', () => { row.style.background = ''; });
    row.addEventListener('click', async () => {
      setChatModelSelection(item);
      if (state.selectedChatId) {
        await api('POST', `/api/chats/${state.selectedChatId}/model`, {
          provider_id: state.selectedChatModelProviderId,
          model_id: state.selectedChatModelId,
        });
      }
      closeModelPicker();
    });
    menu.appendChild(row);
  }
  document.body.appendChild(menu);
  modelSelectPicker = menu;

  const close = e => {
    if (!menu.contains(e.target) && e.target !== modelSelectLabel) {
      closeModelPicker();
      document.removeEventListener('click', close);
    }
  };
  setTimeout(() => document.addEventListener('click', close), 0);
}

function activeControllerForChat(chatId) {
  if (!chatId || !state.activeAbortControllers) return null;
  return state.activeAbortControllers[chatId] || null;
}

function streamRunIsActive(run) {
  return !!(run && run.active &&
    (run.status === 'running' ||
     run.status === 'queued' ||
     run.status === 'cancelling'));
}

function selectedStreamRun() {
  return state.selectedChatId && state.streamRuns
    ? state.streamRuns[state.selectedChatId]
    : null;
}

function refreshSelectedChatSendingState() {
  const controller = activeControllerForChat(state.selectedChatId);
  const runActive = streamRunIsActive(selectedStreamRun());
  state.activeAbortController = controller;
  state.sending = !!(controller || runActive);
  setInputEnabled(!!state.selectedChatId && !state.sending);
  renderCancelAgentButton();
}

function removeWebDebugBubbles() {
  document.querySelectorAll('.message-row.web-debug').forEach(row => row.remove());
}

function isWebDebuggingEnabled() {
  return !!(state.projectEnableWebDebugging && state.webDebuggingActive);
}

function renderAutomateButton() {
  if (!automateBtn) return;
  if (state.projectEnableAutomation && state.selectedChatId) {
    automateBtn.style.display = '';
    automateBtn.disabled = false;
  } else {
    automateBtn.style.display = 'none';
    automateBtn.disabled = true;
    state.automationPanelOpen = false;
    if (automationPanelEl) automationPanelEl.style.display = 'none';
    updateSendButtonForAutomation();
  }
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

function renderCancelAgentButton() {
  if (!cancelAgentBtn) return;
  const automationJob = state.selectedChatId && state.automationJobs
    ? state.automationJobs[state.selectedChatId]
    : null;
  const automationActive = !!(automationJob && automationJob.active);
  const streamController = activeControllerForChat(state.selectedChatId);
  const streamRunActive = streamRunIsActive(selectedStreamRun());
  const streamActive = !!(streamController || streamRunActive);
  state.activeAbortController = streamController || null;
  state.sending = streamActive;
  const canCancel = !!((streamActive || automationActive) && state.selectedChatId);
  cancelAgentBtn.style.display = canCancel ? '' : 'none';
  cancelAgentBtn.disabled = !canCancel;
  if (canCancel) {
    cancelAgentBtn.textContent = automationActive && !streamActive
      ? 'Cancel Automation'
      : 'Cancel Agent';
  } else {
    cancelAgentBtn.textContent = 'Cancel Agent';
  }
}

async function cancelActiveAgent() {
  if (!state.selectedChatId) return;
  const automationJob = state.automationJobs
    ? state.automationJobs[state.selectedChatId]
    : null;
  const automationActive = !!(automationJob && automationJob.active);
  const streamController = activeControllerForChat(state.selectedChatId);
  const streamRunActive = streamRunIsActive(selectedStreamRun());
  if (!streamController && !streamRunActive && !automationActive) return;
  if (cancelAgentBtn) {
    cancelAgentBtn.disabled = true;
    cancelAgentBtn.textContent = 'Cancelling...';
  }
  try {
    if (automationActive && !streamController && !streamRunActive) {
      await fetch(`/api/chats/${state.selectedChatId}/automation`, {
        method: 'DELETE',
        credentials: 'same-origin',
      });
      if (typeof refreshAutomationStatusForChat === 'function') {
        await refreshAutomationStatusForChat(state.selectedChatId, { reloadMessages: false });
      }
    } else {
      await fetch(`/api/chats/${state.selectedChatId}/stream`, {
        method: 'DELETE',
        credentials: 'same-origin',
      });
      if (typeof refreshStreamStatusForChat === 'function') {
        await refreshStreamStatusForChat(state.selectedChatId, { reloadMessages: false });
      }
    }
  } catch (_) {}
  try {
    if (streamController) streamController.abort();
  } catch (_) {}
}

function setWebDebuggingActive(active) {
  const next = !!(active && state.projectEnableWebDebugging);
  const changed = state.webDebuggingActive !== next;
  state.webDebuggingActive = next;
  renderDebugButton();
  renderCancelAgentButton();
  if (changed && state.selectedProjectId && state.selectedChatId) {
    loadMessages(state.selectedProjectId, state.selectedChatId, {
      forceScrollBottom: false,
      debug: next,
    }).catch(err => {
      console.warn('Failed to reload messages for debug toggle', err);
      renderMessages(state.messages);
    });
  } else {
    renderMessages(state.messages);
  }
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
  scrollMessagesToBottomIfPinned();
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
  renderModelSelectLabel();
  renderDebugButton();
  renderAutomateButton();
  renderCancelAgentButton();
}

function openAgenticModePicker() {
  closeModelPicker();
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

if (modelSelectLabel) {
  modelSelectLabel.addEventListener('click', openModelSelectPicker);
}

if (compressBtn) {
  compressBtn.addEventListener('click', async () => {
    if (!state.selectedChatId || activeControllerForChat(state.selectedChatId)) return;
    const ok = confirm(
      'Compressing the context window will summarize older messages into a compressed block.\n' +
      'This action cannot be undone.\n\nDo you want to continue?'
    );
    if (!ok) return;
    compressBtn.style.pointerEvents = 'none';
    compressBtn.style.opacity = '0.5';

    const status = createCompressionRow({ text: 'Compressing context...', status: 'live' });
    messagesEl.appendChild(status.row);
    scrollMessagesToBottomIfPinned();

    try {
      const resp = await api('POST', `/api/chats/${state.selectedChatId}/compress`);
      if (resp && resp.ok) {
        const data = await resp.json();
        status.finalize(data.message || 'Context compressed.');
        scrollMessagesToBottomIfPinned();
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


if (cancelAgentBtn) {
  cancelAgentBtn.addEventListener('click', cancelActiveAgent);
}

async function loadMessages(projectId, chatId, options = {}) {
  const debug = options.debug !== undefined ? !!options.debug : isWebDebuggingEnabled();
  const resp = await api('GET', `/api/chats/${chatId}/messages${debug ? '?debug=1' : ''}`);
  if (!resp) return;
  if (resp.ok) {
    state.messages = await resp.json();
    renderMessages(state.messages, options);
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
    state.selectedChatModelProviderId = null;
    state.selectedChatModelId = null;
    chatTitle.textContent  = 'Select or create a chat';
    messageInput.disabled  = true;
    sendBtn.disabled       = true;
    state.messages         = [];
    renderMessages([]);
    resetPlannerState();
    renderModelSelectLabel();
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
  const sendProjectId = state.selectedProjectId;
  const sendChatId = state.selectedChatId;
  if (!sendChatId || activeControllerForChat(sendChatId) ||
      streamRunIsActive(state.streamRuns && state.streamRuns[sendChatId])) return;
  const content = messageInput.value.trim();
  if ((!content && state.pendingFiles.length === 0) || !sendChatId) return;

  const isCurrentSendChat = () =>
    state.selectedProjectId === sendProjectId && state.selectedChatId === sendChatId;
  const sendAgenticModeId = state.selectedChatAgenticModeId || '';
  const sendWebDebugRequested = isWebDebuggingEnabled();
  const sendAssistantModeName = currentAgenticModeName();
  const sendModelSelection = state.projectUserSelectModel ? currentModelSelection() : null;

  state.sending      = true;
  messageInput.value = '';
  resizeTextarea();
  setInputEnabled(false);

  // Upload any queued file attachments first
  let uploadedFiles = [];
  try {
    uploadedFiles = await uploadPendingFiles(sendChatId);
  } catch (e) {
    if (isCurrentSendChat()) {
      state.sending = false;
      messageInput.value = content;
      resizeTextarea();
      setInputEnabled(true);
      messagesEl.appendChild(buildMessageRow('error', 'Upload failed: ' + e.message));
      scrollMessagesToBottom(true);
    }
    return;
  }

  if (!content) {
    if (isCurrentSendChat()) {
      state.sending = false;
      setInputEnabled(true);
      messageInput.focus();
      scrollMessagesToBottom(true);
    }
    return;
  }

  // Build display content (append uploaded file names if any)
  let displayContent = content;
  if (uploadedFiles.length) {
    displayContent += '\n\n\uD83D\uDCCE ' + uploadedFiles.join(', ');
  }
  const assistantModeName = sendAssistantModeName;
  const userCreatedAt = new Date().toISOString();
  const assistantCreatedAt = new Date().toISOString();

  // Show user message immediately
  state.messages.push({ role: 'user', content: displayContent, created_at: userCreatedAt });
  renderMessages(state.messages, { forceScrollBottom: true });

  const pendingContextRow = buildContextUsageRow('', true);
  messagesEl.appendChild(pendingContextRow);
  scrollMessagesToBottom(true);

  const assistantTurn = createAssistantTurnRow([], assistantModeName, assistantCreatedAt);

  const abortCtrl = new AbortController();
  state.activeAbortControllers[sendChatId] = abortCtrl;
  if (!state.streamRuns) state.streamRuns = {};
  state.streamRuns[sendChatId] = {
    active: true,
    status: 'running',
    chat_id: sendChatId,
    project_id: sendProjectId,
    message: 'Chat run started.',
    live_mode_name: assistantModeName,
    live_started_at: assistantCreatedAt,
    updated_at: assistantCreatedAt,
    messages_revision: 0,
    live_response_revision: 0,
  };
  scheduleStreamRunStatusPolling();
  if (isCurrentSendChat()) {
    state.activeAbortController = abortCtrl;
    renderCancelAgentButton();
  }
  let contextUsageText = '';
  let contextRecorded = false;
  let compressionStatus = null;
  let compressionRecorded = false;
  let queueStatus = null;
  let activityStatus = null;
  let streamUiDetached = false;
  function ensureVisibleStreamRows() {
    if (!isCurrentSendChat()) return false;
    if (pendingContextRow && !pendingContextRow.isConnected) {
      messagesEl.appendChild(pendingContextRow);
    }
    return true;
  }

  try {
    const resp = await fetch(`/api/chats/${sendChatId}/messages/stream`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      credentials: 'same-origin',
      body: JSON.stringify({
        content,
        attachments: uploadedFiles,
        selected_agentic_mode_id: sendAgenticModeId,
        selected_provider_id: sendModelSelection ? (sendModelSelection.provider_id || '') : '',
        selected_model_id: sendModelSelection ? (sendModelSelection.model_id || '') : '',
        web_debug: sendWebDebugRequested,
      }),
      signal: abortCtrl.signal,
    });

    if (resp.status === 401) { window.location.href = '/login'; return; }

    if (!resp.ok) {
      let errMsg = 'Request failed';
      try { errMsg = (await resp.json()).error || errMsg; } catch (_) {}
      if (isCurrentSendChat()) {
        assistantTurn.remove();
        const errRow = buildMessageRow('error', '⚠ ' + errMsg);
        messagesEl.appendChild(errRow);
      }
      return;
    }

    // Consume SSE stream
    let errorMsg = null;
    let cancelledByServer = false;
    const streamState = await readSSEStream(resp, ev => {
      if (!isCurrentSendChat() &&
          ev.error === undefined &&
          !ev.cancelled &&
          !ev.done) {
        streamUiDetached = true;
        return;
      }
      if (isCurrentSendChat()) {
        ensureVisibleStreamRows();
      }
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
        scrollMessagesToBottomIfPinned();
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
        scrollMessagesToBottomIfPinned();
      } else if (ev.questionnaire) {
        assistantTurn.upsertQuestionnaire({
          type: 'questionnaire',
          tool_call_id: ev.tool_call_id || '',
          chat_id: sendChatId,
          question: ev.question || '',
          options: ev.options || [],
          allow_multiple: !!ev.allow_multiple,
          status: 'live',
          started_at: ev.started_at || new Date().toISOString(),
          updated_at: ev.updated_at || new Date().toISOString(),
        });
      } else if (ev.tool_event !== undefined || ev.tool_name !== undefined) {
        const record = {
          tool_call_id: ev.tool_call_id || '',
          tool_name: ev.tool_name || 'Tool',
          arguments: ev.tool_arguments || '',
          result: ev.tool_result || '',
          started_at: ev.started_at || ev.updated_at || '',
          updated_at: ev.updated_at || ev.started_at || '',
          status: ev.tool_status || (ev.tool_event === 'start' ? 'live' : 'done'),
        };
        assistantTurn.upsertTool(record);
        if (record.tool_name === 'project_planner' && record.status !== 'live') {
          schedulePlannerRefresh(100);
        }
        scrollMessagesToBottomIfPinned();
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
        scrollMessagesToBottomIfPinned();
      } else if (ev.ctx_used !== undefined) {
        contextUsageText = formatContextUsageText(ev.ctx_used, ev.ctx_total);
        pendingContextRow.classList.remove('is-pending');
        const valueEl = pendingContextRow.querySelector('.context-usage-value');
        if (valueEl) valueEl.textContent = contextUsageText.replace(/^CTX:\s*/, '');
        if (!contextRecorded) {
          state.messages.push({ role: 'context', content: contextUsageText, created_at: '' });
          contextRecorded = true;
        }
        scrollMessagesToBottomIfPinned();
      } else if (ev.cancelled) {
        cancelledByServer = true;
      } else if (ev.done) {
        // done event — finalize handled below
      } else if (ev.error) {
        errorMsg = ev.error;
      }
    }, abortCtrl.signal);

    if (!errorMsg && !streamState.completed && !streamState.aborted) {
      errorMsg = 'The response stream ended before the server sent a completion event.';
    }

    if (isCurrentSendChat()) {
    if (streamState.aborted || cancelledByServer) {
      const partialAssistant = assistantTurn.finalize();
      if (assistantTurn.hasContent()) {
        state.messages.push({
          role: 'assistant',
          content: partialAssistant.text,
          ui_trace: partialAssistant.ui_trace,
          name: assistantModeName,
          created_at: assistantCreatedAt,
        });
      } else {
        assistantTurn.remove();
      }
      messagesEl.appendChild(buildMessageRow('error', 'Agent cancelled.'));
    } else if (errorMsg) {
      const partialAssistant = assistantTurn.finalize();
      if (assistantTurn.hasContent()) {
        state.messages.push({
          role: 'assistant',
          content: partialAssistant.text,
          ui_trace: partialAssistant.ui_trace,
          name: assistantModeName,
          created_at: assistantCreatedAt,
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
        name: assistantModeName,
        created_at: assistantCreatedAt,
      });
    }
    } else {
      assistantTurn.remove();
    }

  } catch (e) {
    if (!isCurrentSendChat()) {
      assistantTurn.remove();
    } else if (e.name === 'AbortError') {
      const partialAssistant = assistantTurn.finalize();
      if (assistantTurn.hasContent()) {
        state.messages.push({
          role: 'assistant',
          content: partialAssistant.text,
          ui_trace: partialAssistant.ui_trace,
          name: assistantModeName,
          created_at: assistantCreatedAt,
        });
      } else {
        assistantTurn.remove();
      }
      messagesEl.appendChild(buildMessageRow('error', 'Agent cancelled.'));
    } else {
      const partialAssistant = assistantTurn.finalize();
      if (assistantTurn.hasContent()) {
        state.messages.push({
          role: 'assistant',
          content: partialAssistant.text,
          ui_trace: partialAssistant.ui_trace,
          name: assistantModeName,
          created_at: assistantCreatedAt,
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
    if (state.activeAbortControllers &&
        state.activeAbortControllers[sendChatId] === abortCtrl) {
      delete state.activeAbortControllers[sendChatId];
    }
    if (state.activeAbortController === abortCtrl) {
      state.activeAbortController = null;
    }
    if (state.streamRuns && state.streamRuns[sendChatId]) {
      state.streamRuns[sendChatId].active = false;
      if (!state.streamRuns[sendChatId].status ||
          state.streamRuns[sendChatId].status === 'running') {
        state.streamRuns[sendChatId].status = 'completed';
      }
    }
    try {
      await refreshStreamStatusForChat(sendChatId, { reloadMessages: false });
    } catch (_) {}
    if (isCurrentSendChat()) {
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
      refreshSelectedChatSendingState();
      if (streamUiDetached) {
        await loadMessages(sendProjectId, sendChatId);
      }
      messageInput.focus();
      scrollMessagesToBottomIfPinned();
    } else {
      refreshSelectedChatSendingState();
    }
  }
}

function updateSendButtonForAutomation() {
  if (!sendBtn) return;
  if (state.automationPanelOpen) {
    sendBtn.textContent = state.selectedAutomationStepIndex >= 0 ? 'Update Step' : 'Add Step';
  } else {
    sendBtn.textContent = 'Send';
  }
}

sendBtn.addEventListener('click', () => {
  if (state.automationPanelOpen) {
    addOrUpdateAutomationStep();
  } else {
    sendMessage();
  }
});
messageInput.addEventListener('keydown', e => {
  if (e.key === 'Enter' && !e.shiftKey) {
    e.preventDefault();
    if (state.automationPanelOpen) {
      addOrUpdateAutomationStep();
    } else {
      sendMessage();
    }
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
  renderCancelAgentButton();
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
  try {
    const meResp = await api('GET', '/api/me');
    if (!meResp) return;
    if (!meResp.ok) { window.location.href = '/login'; return; }

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

    const projResp = await api('GET', '/api/projects');
    if (!projResp) return;
    if (!projResp.ok) throw new Error('Could not load projects.');

    state.projects = await projResp.json();
    renderProjectList();
    emptyState.style.display = state.projects.length ? '' : 'flex';
    if (state.projects.length === 0)
      emptyState.querySelector('p').textContent =
        'No projects are assigned to your account yet. Ask your administrator.';
  } catch (err) {
    emptyState.style.display = 'flex';
    emptyState.querySelector('p').textContent =
      'Could not load your projects. Please refresh or sign in again.';
  }
}

init();

// Automation controls live here so the toggle shares app.js state and DOM bindings.
/* global api, escapeHtml, loadMessages, messageInput, messagesEl, postProcessMessageBubble, renderMarkdown, resizeTextarea, renderCancelAgentButton, schedulePlannerRefresh, state */

if (automateBtn) {
  automateBtn.addEventListener('click', () => {
    state.automationPanelOpen = !state.automationPanelOpen;
    renderAutomationUI();
    updateSendButtonForAutomation();
  });
}

automationPanelEl    = $('automation-panel');
automationStepsEl    = $('automation-steps');
automationStatusEl   = $('automation-status');
automationAddStepBtn = $('automation-add-step-btn');
automationClearBtn   = $('automation-clear-btn');
automationSendBtn    = $('automation-send-btn');
automationRepeatEl   = $('automation-repeat');
automationCompressEl = $('automation-compress');
automationModelFieldEl = $('automation-model-field');
automationModelSelectEl = $('automation-model');

function automationJobIsActive(job) {
  return !!(job && (job.active ||
    job.status === 'queued' ||
    job.status === 'running' ||
    job.status === 'cancelling'));
}

function selectedAutomationJob() {
  return state.selectedChatId && state.automationJobs
    ? state.automationJobs[state.selectedChatId]
    : null;
}

function populateAutomationModelSelect(selectedProviderId = '', selectedModelId = '') {
  if (!automationModelFieldEl || !automationModelSelectEl) return;
  const choices = state.projectUserSelectModel ? (state.projectSelectableModels || []) : [];
  if (!state.selectedChatId || choices.length === 0) {
    automationModelFieldEl.style.display = 'none';
    automationModelSelectEl.innerHTML = '';
    return;
  }
  const fallback = currentModelSelection();
  const selectedKey = modelKey(
    selectedProviderId || (fallback && fallback.provider_id),
    selectedModelId || (fallback && fallback.model_id));
  automationModelSelectEl.innerHTML = '';
  for (const item of choices) {
    const option = document.createElement('option');
    option.value = modelKey(item.provider_id, item.model_id);
    option.textContent = item.label || item.model_name || item.model_id || 'Model';
    option.dataset.providerId = item.provider_id || '';
    option.dataset.modelId = item.model_id || '';
    option.dataset.modelName = item.label || item.model_name || item.model_id || '';
    if (option.value === selectedKey) option.selected = true;
    automationModelSelectEl.appendChild(option);
  }
  automationModelFieldEl.style.display = '';
}

function selectedAutomationModelSelection() {
  if (!state.projectUserSelectModel) return null;
  if (automationModelSelectEl && automationModelSelectEl.options.length) {
    const option = automationModelSelectEl.options[automationModelSelectEl.selectedIndex];
    if (option) {
      return {
        provider_id: option.dataset.providerId || '',
        model_id: option.dataset.modelId || '',
        label: option.dataset.modelName || option.textContent || '',
      };
    }
  }
  return currentModelSelection();
}

function renderAutomationUI() {
  if (!automationPanelEl) return;
  const activeJob = automationJobIsActive(selectedAutomationJob());
  automationPanelEl.style.display = state.automationPanelOpen ? 'block' : 'none';
  populateAutomationModelSelect();
  if (automationStatusEl) {
    automationStatusEl.textContent = state.automationSequence.length + ' step(s)';
  }
  if (automationSendBtn) {
    automationSendBtn.style.display = state.automationSequence.length ? 'inline-flex' : 'none';
    automationSendBtn.disabled = activeJob;
  }
  renderAutomationSteps();
  renderSelectedAutomationJobStatus();
}

function renderAutomationSteps() {
  if (!automationStepsEl) return;
  automationStepsEl.innerHTML = '';
  for (let i = 0; i < state.automationSequence.length; ++i) {
    const step = state.automationSequence[i];
    const row = document.createElement('div');
    row.className = 'automation-step' + (i === state.selectedAutomationStepIndex ? ' selected' : '');
    const modelLabel = step.model_name || step.model_id || '';
    row.innerHTML = '<span class="step-index">' + (i + 1) + '.</span>' +
      '<span class="step-name">' + escapeHtml(step.mode_name || step.mode_id || 'default') + '</span>' +
      (modelLabel ? '<span class="step-model">' + escapeHtml(modelLabel) + '</span>' : '') +
      '<span class="step-prompt">"' + escapeHtml(step.prompt_preview || step.prompt || '') + '"</span>' +
      '<span class="step-meta">x' + step.repeat + (step.compress ? ' | Compress' : '') + '</span>';
    row.addEventListener('click', () => {
      if (state.selectedAutomationStepIndex === i) {
        state.selectedAutomationStepIndex = -1;
        clearComposerForAutomation();
      } else {
        state.selectedAutomationStepIndex = i;
        loadAutomationStepIntoComposer(i);
      }
      renderAutomationSteps();
      updateSendButtonForAutomation();
    });
    automationStepsEl.appendChild(row);
  }
}

function loadAutomationStepIntoComposer(index) {
  const step = state.automationSequence[index];
  if (!step) return;
  messageInput.value = step.prompt || '';
  resizeTextarea();
  if (automationRepeatEl) automationRepeatEl.value = step.repeat || 1;
  if (automationCompressEl) automationCompressEl.checked = !!step.compress;
  populateAutomationModelSelect(step.provider_id || '', step.model_id || '');
  messageInput.focus();
}

function clearComposerForAutomation() {
  messageInput.value = '';
  resizeTextarea();
  if (automationRepeatEl) automationRepeatEl.value = 1;
  if (automationCompressEl) automationCompressEl.checked = false;
  populateAutomationModelSelect();
  messageInput.focus();
}

function addOrUpdateAutomationStep() {
  const prompt = messageInput.value.trim();
  if (!prompt) return;
  const modeId = currentAgenticModeId();
  const mode = state.projectAgenticModes.find(m => m.id === modeId);
  const modeName = mode ? mode.name : (modeId || 'Default');
  const modelSelection = selectedAutomationModelSelection();
  const repeat = Math.max(1, Math.min(50, parseInt(automationRepeatEl && automationRepeatEl.value || '1', 10)));
  const compress = automationCompressEl ? automationCompressEl.checked : false;
  const stepData = {
    mode_id: modeId || '',
    mode_name: modeName,
    provider_id: modelSelection ? (modelSelection.provider_id || '') : '',
    model_id: modelSelection ? (modelSelection.model_id || '') : '',
    model_name: modelSelection ? (modelSelection.label || modelSelection.model_name || modelSelection.model_id || '') : '',
    prompt: prompt || '',
    prompt_preview: prompt ? prompt.slice(0, 60) + (prompt.length > 60 ? '...' : '') : '',
    compress: compress,
    repeat: repeat,
  };

  if (state.selectedAutomationStepIndex >= 0) {
    state.automationSequence[state.selectedAutomationStepIndex] = stepData;
    state.selectedAutomationStepIndex = -1;
  } else {
    state.automationSequence.push(stepData);
  }
  clearComposerForAutomation();
  renderAutomationUI();
  messageInput.focus();
}

function clearAutomationSequence() {
  state.automationSequence = [];
  state.selectedAutomationStepIndex = -1;
  clearComposerForAutomation();
  renderAutomationUI();
}

function automationStatusText(job) {
  if (!job) return '';
  const current = job.current_step && job.total_steps
    ? 'Step ' + job.current_step + '/' + job.total_steps
    : '';
  const repeat = job.total_repeats > 1
    ? ' (' + job.current_repeat + '/' + job.total_repeats + ')'
    : '';
  const progress = job.total_runs
    ? ' - ' + job.completed_runs + '/' + job.total_runs + ' run(s)'
    : '';
  return current ? current + repeat + progress : (job.status || 'Automation running.');
}

function formatAutomationTimestamp(value) {
  if (!value) return '';
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return value;
  return date.toLocaleTimeString([], {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  });
}

function automationStatusTargets() {
  return [
    $('automation-running-status'),
    $('mobile-automation-running-status'),
  ].filter(Boolean);
}

function automationQuestionnaireKey(job, questionnaire) {
  const chatId = job && job.chat_id ? job.chat_id : state.selectedChatId;
  const toolCallId = questionnaire && questionnaire.tool_call_id
    ? questionnaire.tool_call_id
    : '';
  return chatId + ':' + toolCallId;
}

function automationQuestionnaireSelection(job, questionnaire) {
  const key = automationQuestionnaireKey(job, questionnaire);
  if (!state.automationQuestionnaireSelections) {
    state.automationQuestionnaireSelections = {};
  }
  if (!state.automationQuestionnaireSelections[key]) {
    state.automationQuestionnaireSelections[key] = {
      selectedIndices: [],
      submitting: false,
    };
  }
  return state.automationQuestionnaireSelections[key];
}

function forgetAutomationQuestionnaireSelection(job, questionnaire) {
  if (!state.automationQuestionnaireSelections) return;
  delete state.automationQuestionnaireSelections[
    automationQuestionnaireKey(job, questionnaire)
  ];
}

function renderSelectedAutomationJobStatus() {
  const job = selectedAutomationJob();
  const active = automationJobIsActive(job);
  document.body.classList.toggle('automation-active', active);

  for (const statusEl of automationStatusTargets()) {
    renderAutomationJobStatusInto(statusEl, job, active);
  }
  renderCancelAgentButton();
}

function renderAutomationJobStatusInto(statusEl, job, active) {
  if (!job) {
    statusEl.style.display = 'none';
    statusEl.innerHTML = '';
    return;
  }

  statusEl.style.display = '';
  statusEl.innerHTML = '';

  const text = document.createElement('div');
  text.className = 'automation-status-text';
  text.textContent = automationStatusText(job);
  statusEl.appendChild(text);

  if (active) {
    const heartbeatTime = formatAutomationTimestamp(job.heartbeat_at || job.updated_at);
    const heartbeat = document.createElement('div');
    heartbeat.className = 'automation-status-detail automation-heartbeat';
    heartbeat.textContent = 'Server active - automation worker active' +
      (heartbeatTime ? ' - ' + heartbeatTime : '');
    statusEl.appendChild(heartbeat);
  }

  const currentStatus = job.activity_message || job.message || '';
  if (currentStatus) {
    const detail = document.createElement('div');
    detail.className = 'automation-status-detail';
    detail.textContent = 'Status: ' + currentStatus;
    statusEl.appendChild(detail);
  }

  if (job.error) {
    const error = document.createElement('div');
    error.className = 'automation-status-error';
    error.textContent = job.error;
    statusEl.appendChild(error);
  }

  if (job.queue_state === 'queued') {
    const queue = document.createElement('div');
    queue.className = 'automation-status-detail';
    queue.textContent = 'Provider queue position ' + (job.queue_position || '?') +
      ' of ' + (job.queue_depth || '?');
    statusEl.appendChild(queue);
  }

  if (active && job.current_tool_name) {
    const toolTime = formatMessageTimestamp(job.current_tool_at || job.updated_at);
    const tool = document.createElement('div');
    tool.className = 'automation-status-detail';
    tool.textContent = (job.current_tool_status === 'live' ? 'Running tool: ' : 'Last tool: ') +
      job.current_tool_name +
      (toolTime ? ' [' + toolTime + ']' : '');
    statusEl.appendChild(tool);
  }

  if (job.questionnaire && active) {
    const q = job.questionnaire;
    const selectionState = automationQuestionnaireSelection(job, q);
    const selected = new Set(selectionState.selectedIndices || []);
    const question = document.createElement('div');
    question.className = 'automation-question';
    question.textContent = q.question || 'Automation needs input.';
    statusEl.appendChild(question);

    const buttons = document.createElement('div');
    buttons.className = 'automation-question-options';
    const optionButtons = [];
    let confirm = null;

    const saveSelection = () => {
      selectionState.selectedIndices = Array.from(selected).sort((a, b) => a - b);
    };

    const updateButtons = () => {
      optionButtons.forEach((btn, idx) => {
        btn.classList.toggle('selected', selected.has(idx));
        btn.disabled = !!selectionState.submitting;
      });
      if (confirm) {
        confirm.disabled = !!selectionState.submitting || selected.size === 0;
      }
    };

    const submit = async indices => {
      if (selectionState.submitting || !indices.length) return;
      selectionState.submitting = true;
      saveSelection();
      updateButtons();
      try {
        await api('POST', '/api/questionnaire-response', {
          chat_id: job.chat_id || state.selectedChatId,
          tool_call_id: q.tool_call_id,
          selected_indices: indices,
        });
        forgetAutomationQuestionnaireSelection(job, q);
        await refreshAutomationStatusForChat(job.chat_id || state.selectedChatId, { reloadMessages: false });
      } catch (err) {
        selectionState.submitting = false;
        updateButtons();
        throw err;
      }
    };

    (q.options || []).forEach((opt, idx) => {
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'automation-question-option';
      btn.textContent = opt;
      btn.addEventListener('click', async () => {
        if (q.allow_multiple) {
          if (selected.has(idx)) selected.delete(idx);
          else selected.add(idx);
          saveSelection();
          updateButtons();
        } else {
          selected.clear();
          selected.add(idx);
          saveSelection();
          updateButtons();
          await submit([idx]);
        }
      });
      optionButtons.push(btn);
      buttons.appendChild(btn);
    });

    if (q.allow_multiple) {
      confirm = document.createElement('button');
      confirm.type = 'button';
      confirm.className = 'automation-question-confirm';
      confirm.textContent = selectionState.submitting ? 'Submitting...' : 'Confirm';
      confirm.addEventListener('click', async () => {
        await submit(Array.from(selected).sort((a, b) => a - b));
      });
      buttons.appendChild(confirm);
    }
    updateButtons();
    statusEl.appendChild(buttons);
  }

  if (!active) {
    const close = document.createElement('button');
    close.type = 'button';
    close.className = 'automation-status-dismiss';
    close.textContent = 'Dismiss';
    close.addEventListener('click', () => {
      delete state.automationJobs[state.selectedChatId];
      renderSelectedAutomationJobStatus();
      renderAutomationUI();
    });
    statusEl.appendChild(close);
  }
}

function clearAutomationLiveResponse() {
  document.querySelectorAll('.automation-live-response').forEach(row => row.remove());
}

function automationLiveTrace(job) {
  if (Array.isArray(job && job.live_trace) && job.live_trace.length) {
    return normalizeAssistantTrace(job.live_trace, job.live_response || '');
  }
  const trace = [];
  const tools = Array.isArray(job && job.live_tool_trace)
    ? job.live_tool_trace
    : [];
  tools.forEach(tool => {
    trace.push({
      type: 'tool_usage',
      record: normalizeToolUsageRecord(tool),
    });
  });
  if (job && job.live_response) {
    trace.push({
      type: 'text',
      content: job.live_response,
      live: true,
    });
  }
  return trace;
}

function renderAutomationLiveResponse(job = selectedAutomationJob()) {
  const previousTop = messagesEl ? messagesEl.scrollTop : 0;
  const shouldFollow = state.followChatTail && isMessagesNearBottom();
  clearAutomationLiveResponse();
  const trace = automationLiveTrace(job);
  if (!job ||
      !automationJobIsActive(job) ||
      job.chat_id !== state.selectedChatId ||
      !trace.length) {
    if (shouldFollow) scrollMessagesToBottom(true);
    else restoreMessagesScroll(previousTop);
    return;
  }

  const row = document.createElement('div');
  row.className = 'message-row model automation-live-response';

  const lbl = document.createElement('div');
  lbl.className = 'message-role-label';
  lbl.textContent = messageRoleLabel(
    'assistant',
    job.live_mode_name || '',
    job.live_started_at || job.updated_at || ''
  );

  const bubble = document.createElement('div');
  bubble.className = 'message-bubble streaming';
  renderAssistantTraceBubble(bubble, trace, {
    streaming: true,
    fallbackContent: job.live_response || '',
    questionnaireChatId: job.chat_id || state.selectedChatId,
  });

  row.appendChild(lbl);
  row.appendChild(bubble);
  row.classList.add('automation-live-response');
  messagesEl.appendChild(row);
  if (shouldFollow) scrollMessagesToBottom(true);
  else restoreMessagesScroll(previousTop);
}

function clearStreamRunLiveResponse() {
  document.querySelectorAll('.stream-run-live-response').forEach(row => row.remove());
}

function renderStreamRunLiveResponse(run = selectedStreamRun()) {
  const previousTop = messagesEl ? messagesEl.scrollTop : 0;
  const shouldFollow = state.followChatTail && isMessagesNearBottom();
  clearStreamRunLiveResponse();
  if (!run ||
      !streamRunIsActive(run) ||
      run.chat_id !== state.selectedChatId ||
      activeControllerForChat(run.chat_id || state.selectedChatId)) {
    if (shouldFollow) scrollMessagesToBottom(true);
    else restoreMessagesScroll(previousTop);
    return;
  }

  const trace = automationLiveTrace(run);
  if (!trace.length && !run.message && !run.activity_message && !run.heartbeat_message) {
    return;
  }

  const row = document.createElement('div');
  row.className = 'message-row model stream-run-live-response';

  const lbl = document.createElement('div');
  lbl.className = 'message-role-label';
  lbl.textContent = messageRoleLabel(
    'assistant',
    run.live_mode_name || '',
    run.live_started_at || run.updated_at || ''
  );

  const bubble = document.createElement('div');
  bubble.className = 'message-bubble streaming';
  if (trace.length) {
    renderAssistantTraceBubble(bubble, trace, {
      streaming: true,
      fallbackContent: run.live_response || '',
      questionnaireChatId: run.chat_id || state.selectedChatId,
    });
  } else {
    const p = document.createElement('p');
    p.textContent = run.activity_message || run.message || run.heartbeat_message || 'Agent is still working...';
    bubble.appendChild(p);
    const cursor = document.createElement('span');
    cursor.className = 'streaming-cursor';
    bubble.appendChild(cursor);
  }

  row.appendChild(lbl);
  row.appendChild(bubble);
  messagesEl.appendChild(row);
  if (shouldFollow) scrollMessagesToBottom(true);
  else restoreMessagesScroll(previousTop);
}

async function refreshStreamStatusForChat(chatId, options = {}) {
  if (!chatId) return null;
  const previous = state.streamRuns ? state.streamRuns[chatId] : null;
  const previousMessagesRevision = previous ? previous.messages_revision : 0;
  const previousPlannerRevision = previous ? (previous.planner_revision || 0) : 0;
  const resp = await api('GET', `/api/chats/${chatId}/stream`);
  if (!resp || !resp.ok) return previous || null;

  const data = await resp.json();
  const run = data.run || null;
  if (!state.streamRuns) state.streamRuns = {};
  if (run) state.streamRuns[chatId] = run;
  else delete state.streamRuns[chatId];

  if (chatId === state.selectedChatId) {
    if (run &&
        typeof schedulePlannerRefresh === 'function' &&
        (run.current_tool_name === 'project_planner' ||
         (run.planner_revision || 0) !== previousPlannerRevision)) {
      schedulePlannerRefresh(run.current_tool_status === 'live' ? 400 : 100);
    }
    if (options.reloadMessages !== false &&
        run &&
        run.messages_revision !== previousMessagesRevision) {
      await loadMessages(state.selectedProjectId, chatId);
    }
    renderStreamRunLiveResponse(run);
    refreshSelectedChatSendingState();
  }

  scheduleStreamRunStatusPolling();
  return run;
}

function activeStreamRunChatIds() {
  const ids = new Set();
  if (state.streamRuns) {
    for (const [chatId, run] of Object.entries(state.streamRuns)) {
      if (streamRunIsActive(run)) ids.add(chatId);
    }
  }
  if (state.selectedChatId) ids.add(state.selectedChatId);
  return Array.from(ids);
}

function scheduleStreamRunStatusPolling() {
  if (state.streamRunStatusTimer) return;
  const ids = activeStreamRunChatIds();
  if (!ids.length) return;
  state.streamRunStatusTimer = setTimeout(async () => {
    state.streamRunStatusTimer = null;
    for (const chatId of activeStreamRunChatIds()) {
      await refreshStreamStatusForChat(chatId);
    }
    if (activeStreamRunChatIds().some(id => streamRunIsActive(state.streamRuns[id]))) {
      scheduleStreamRunStatusPolling();
    }
  }, 750);
}

async function refreshAutomationStatusForChat(chatId, options = {}) {
  if (!chatId) return null;
  const previous = state.automationJobs ? state.automationJobs[chatId] : null;
  const previousMessagesRevision = previous ? previous.messages_revision : 0;
  const previousPlannerRevision = previous ? (previous.planner_revision || 0) : 0;
  const resp = await api('GET', `/api/chats/${chatId}/automation`);
  if (!resp || !resp.ok) return previous || null;

  const data = await resp.json();
  const job = data.job || null;
  if (!state.automationJobs) state.automationJobs = {};
  if (job) state.automationJobs[chatId] = job;
  else delete state.automationJobs[chatId];

  if (chatId === state.selectedChatId) {
    if (job &&
        typeof schedulePlannerRefresh === 'function' &&
        (job.current_tool_name === 'project_planner' ||
         (job.planner_revision || 0) !== previousPlannerRevision)) {
      schedulePlannerRefresh(job.current_tool_status === 'live' ? 400 : 100);
    }
    if (options.reloadMessages !== false &&
        job &&
        job.messages_revision !== previousMessagesRevision) {
      await loadMessages(state.selectedProjectId, chatId);
    }
    renderSelectedAutomationJobStatus();
    renderAutomationUI();
    renderAutomationLiveResponse(job);
  }

  scheduleAutomationStatusPolling();
  return job;
}

function activeAutomationChatIds() {
  const ids = new Set();
  if (state.automationJobs) {
    for (const [chatId, job] of Object.entries(state.automationJobs)) {
      if (automationJobIsActive(job)) ids.add(chatId);
    }
  }
  if (state.selectedChatId) ids.add(state.selectedChatId);
  return Array.from(ids);
}

function scheduleAutomationStatusPolling() {
  if (state.automationStatusTimer) return;
  const ids = activeAutomationChatIds();
  if (!ids.length) return;
  state.automationStatusTimer = setTimeout(async () => {
    state.automationStatusTimer = null;
    for (const chatId of activeAutomationChatIds()) {
      await refreshAutomationStatusForChat(chatId);
    }
    if (activeAutomationChatIds().some(id => automationJobIsActive(state.automationJobs[id]))) {
      scheduleAutomationStatusPolling();
    }
  }, 750);
}

async function runAutomationSequence() {
  const targetChatId = state.selectedChatId;
  if (!state.automationSequence.length || !targetChatId) return;

  const existingJob = state.automationJobs ? state.automationJobs[targetChatId] : null;
  if (automationJobIsActive(existingJob)) {
    renderSelectedAutomationJobStatus();
    return;
  }

  const steps = state.automationSequence.map(step => ({
    mode_id: step.mode_id || '',
    mode_name: step.mode_name || '',
    provider_id: step.provider_id || '',
    model_id: step.model_id || '',
    model_name: step.model_name || '',
    prompt: step.prompt || '',
    repeat: step.repeat || 1,
    compress: !!step.compress,
  }));

  if (automationSendBtn) automationSendBtn.disabled = true;
  try {
    const resp = await api('POST', `/api/chats/${targetChatId}/automation/run`, { steps });
    if (!resp) return;
    const data = await resp.json().catch(() => ({}));
    if (!resp.ok) {
      if (!state.automationJobs) state.automationJobs = {};
      state.automationJobs[targetChatId] = {
        active: false,
        status: 'failed',
        message: data.error || 'Could not start automation.',
        error: data.error || 'Could not start automation.',
      };
      renderSelectedAutomationJobStatus();
      return;
    }
    state.automationJobs[targetChatId] = data.job;
    renderSelectedAutomationJobStatus();
    scheduleAutomationStatusPolling();
  } finally {
    if (automationSendBtn) {
      automationSendBtn.disabled = automationJobIsActive(state.automationJobs[targetChatId]);
    }
  }
}

if (automationAddStepBtn) { automationAddStepBtn.addEventListener('click', addOrUpdateAutomationStep); }
if (automationClearBtn)  { automationClearBtn.addEventListener('click', clearAutomationSequence); }
if (automationSendBtn)   { automationSendBtn.addEventListener('click', runAutomationSequence); }
