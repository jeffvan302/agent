

if (automateBtn) {
  automateBtn.addEventListener('click', () => {
    state.automationPanelOpen = !state.automationPanelOpen;
    renderAutomationUI();
    updateSendButtonForAutomation();
  });
}

automationPanelEl   = $('automation-panel');
automationStepsEl   = $('automation-steps');
automationStatusEl  = $('automation-status');
automationAddStepBtn = $('automation-add-step-btn');
automationClearBtn  = $('automation-clear-btn');
automationSendBtn   = $('automation-send-btn');
automationRepeatEl  = $('automation-repeat');
automationCompressEl = $('automation-compress');

function renderAutomationUI() {
  if (!automationPanelEl) return;
  automationPanelEl.style.display = state.automationPanelOpen ? 'block' : 'none';
  if (automationStatusEl) automationStatusEl.textContent = '' + state.automationSequence.length + ' step(s)';
  if (automationSendBtn) automationSendBtn.style.display = state.automationSequence.length ? 'inline-flex' : 'none';
  renderAutomationSteps();
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
      '<span class="step-meta">×' + step.repeat + (step.compress ? ' | Compress' : '') + '</span>';
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
    prompt_preview: prompt ? prompt.slice(0, 60) + (prompt.length > 60 ? '…' : '') : '',
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

function updateAutomationStatus(stepIndex, repeatIndex, totalSteps) {
  const statusEl = $('automation-running-status');
  if (!statusEl) return;
  if (!state.automationRunning) {
    statusEl.style.display = 'none';
    statusEl.textContent = '';
    return;
  }
  statusEl.style.display = '';
  const step = state.automationSequence[stepIndex];
  const stepName = step ? (step.mode_name || step.mode_id || 'default') : '';
  const repeatLabel = step && step.repeat > 1 ? ' (' + (repeatIndex + 1) + '/' + step.repeat + ')' : '';
  statusEl.textContent = 'Automation: Step ' + (stepIndex + 1) + '/' + totalSteps +
    ' — ' + stepName + repeatLabel;
}

async function sendBackgroundMessage(chatId, prompt, modeId) {
  const resp = await fetch(`/api/chats/${chatId}/messages`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    credentials: 'same-origin',
    body: JSON.stringify({
      content: prompt,
      selected_agentic_mode_id: modeId || '',
    }),
  });
  if (!resp.ok) {
    const data = await resp.json().catch(() => ({}));
    throw new Error(data.error || 'Request failed');
  }
  return resp.json();
}

async function runAutomationSequence() {
  const targetChatId = state.selectedChatId;
  if (state.automationRunning || !state.automationSequence.length || !targetChatId) return;
  state.automationRunning = true;
  if (automationSendBtn) automationSendBtn.disabled = true;
  const totalSteps = state.automationSequence.length;
  try {
    for (let si = 0; si < state.automationSequence.length; ++si) {
      const step = state.automationSequence[si];
      for (let r = 0; r < step.repeat; ++r) {
        updateAutomationStatus(si, r, totalSteps);
        if (step.mode_id) {
          await api('POST', `/api/chats/${targetChatId}/agentic-mode`, {
            selected_agentic_mode_id: step.mode_id,
          });
        }
        if (step.prompt) {
          await sendBackgroundMessage(targetChatId, step.prompt, step.mode_id);
        }
        if (step.compress && state.projectAllowManualCompress) {
          await new Promise(res => setTimeout(res, 400));
          await api('POST', `/api/chats/${targetChatId}/compress`);
        }
      }
    }
  } finally {
    state.automationRunning = false;
    if (automationSendBtn) automationSendBtn.disabled = false;
    updateAutomationStatus(0, 0, 0);
  }
}

if (automationAddStepBtn) { automationAddStepBtn.addEventListener('click', addOrUpdateAutomationStep); }
if (automationClearBtn)  { automationClearBtn.addEventListener('click', clearAutomationSequence); }
if (automationSendBtn)   { automationSendBtn.addEventListener('click', runAutomationSequence); }

