








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

function renderAutomationUI() {
  if (!automationPanelEl) return;
  const activeJob = automationJobIsActive(selectedAutomationJob());
  automationPanelEl.style.display = state.automationPanelOpen ? 'block' : 'none';
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
    row.innerHTML = '<span class="step-index">' + (i + 1) + '.</span>' +
      '<span class="step-name">' + escapeHtml(step.mode_name || step.mode_id || 'default') + '</span>' +
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
  messageInput.focus();
}

function clearComposerForAutomation() {
  messageInput.value = '';
  resizeTextarea();
  if (automationRepeatEl) automationRepeatEl.value = 1;
  if (automationCompressEl) automationCompressEl.checked = false;
  messageInput.focus();
}

function addOrUpdateAutomationStep() {
  const prompt = messageInput.value.trim();
  if (!prompt) return;
  const modeId = currentAgenticModeId();
  const mode = state.projectAgenticModes.find(m => m.id === modeId);
  const modeName = mode ? mode.name : (modeId || 'Default');
  const repeat = Math.max(1, Math.min(50, parseInt(automationRepeatEl && automationRepeatEl.value || '1', 10)));
  const compress = automationCompressEl ? automationCompressEl.checked : false;
  const stepData = {
    mode_id: modeId || '',
    mode_name: modeName,
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
  const base = job.message || job.activity_message || job.status || 'Automation running.';
  return (current ? current + repeat + progress + ': ' : '') + base;
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

  if (active) {
    const heartbeatTime = formatAutomationTimestamp(job.heartbeat_at || job.updated_at);
    const heartbeat = document.createElement('div');
    heartbeat.className = 'automation-status-detail automation-heartbeat';
    heartbeat.textContent = 'Server active' +
      (job.heartbeat_message ? ': ' + job.heartbeat_message : '') +
      (heartbeatTime ? ' - ' + heartbeatTime : '');
    statusEl.appendChild(heartbeat);
  }

  if (active && job.current_tool_name) {
    const tool = document.createElement('div');
    tool.className = 'automation-status-detail';
    tool.textContent = (job.current_tool_status === 'live' ? 'Running tool: ' : 'Last tool: ') +
      job.current_tool_name;
    statusEl.appendChild(tool);
  }

  if (job.questionnaire && active) {
    const q = job.questionnaire;
    const question = document.createElement('div');
    question.className = 'automation-question';
    question.textContent = q.question || 'Automation needs input.';
    statusEl.appendChild(question);

    const selected = new Set();
    const buttons = document.createElement('div');
    buttons.className = 'automation-question-options';
    const submit = async indices => {
      buttons.querySelectorAll('button').forEach(btn => { btn.disabled = true; });
      await api('POST', '/api/questionnaire-response', {
        chat_id: state.selectedChatId,
        tool_call_id: q.tool_call_id,
        selected_indices: indices,
      });
      await refreshAutomationStatusForChat(state.selectedChatId, { reloadMessages: false });
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
          btn.classList.toggle('selected', selected.has(idx));
          confirm.style.display = selected.size ? '' : 'none';
        } else {
          await submit([idx]);
        }
      });
      buttons.appendChild(btn);
    });

    const confirm = document.createElement('button');
    confirm.type = 'button';
    confirm.className = 'automation-question-confirm';
    confirm.textContent = 'Confirm';
    confirm.style.display = 'none';
    confirm.addEventListener('click', async () => {
      await submit(Array.from(selected).sort((a, b) => a - b));
    });
    buttons.appendChild(confirm);
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

function renderAutomationLiveResponse(job = selectedAutomationJob()) {
  clearAutomationLiveResponse();
  if (!job ||
      !automationJobIsActive(job) ||
      job.chat_id !== state.selectedChatId ||
      !job.live_response) {
    return;
  }

  const row = document.createElement('div');
  row.className = 'message-row model automation-live-response';

  const lbl = document.createElement('div');
  lbl.className = 'message-role-label';
  lbl.textContent = job.live_mode_name
    ? 'Assistant (' + job.live_mode_name + ')'
    : 'Assistant';

  const bubble = document.createElement('div');
  bubble.className = 'message-bubble streaming';
  bubble.innerHTML = renderMarkdown(job.live_response, { streaming: true });
  postProcessMessageBubble(bubble);

  const cursor = document.createElement('span');
  cursor.className = 'streaming-cursor';
  bubble.appendChild(cursor);

  row.appendChild(lbl);
  row.appendChild(bubble);
  row.classList.add('automation-live-response');
  messagesEl.appendChild(row);
  messagesEl.scrollTop = messagesEl.scrollHeight;
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




