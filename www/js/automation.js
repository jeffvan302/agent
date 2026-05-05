if (automateBtn) {
  automateBtn.addEventListener('click', () => {
    state.automationRecording = true;
    state.automationSequence = [];
    renderAutomationUI();
  });
}

automationPanelEl   = $('automation-panel');
automationStepsEl   = $('automation-steps');
automationStatusEl  = $('automation-status');
automationAddStepBtn = $('automation-add-step-btn');
automationClearBtn  = $('automation-clear-btn');
automationDoneBtn   = $('automation-done-btn');
automationSendBtn   = $('automation-send-btn');
automationRepeatEl  = $('automation-repeat');
automationCompressEl = $('automation-compress');

function renderAutomationUI() {
  if (!automationPanelEl) return;
  if (state.automationRecording) {
    automationPanelEl.style.display = 'block';
    if (automationStatusEl) automationStatusEl.textContent = 'Recording sequence';
    if (automationSendBtn) automationSendBtn.style.display = 'none';
    renderAutomationSteps();
  } else {
    automationPanelEl.style.display = state.automationSequence.length ? 'block' : 'none';
    if (automationStatusEl) automationStatusEl.textContent = '' + state.automationSequence.length + ' step(s)';
    if (automationSendBtn) automationSendBtn.style.display = state.automationSequence.length ? 'inline-flex' : 'none';
    renderAutomationSteps();
  }
}

function renderAutomationSteps() {
  if (!automationStepsEl) return;
  automationStepsEl.innerHTML = '';
  for (let i = 0; i < state.automationSequence.length; ++i) {
    const step = state.automationSequence[i];
    const row = document.createElement('div');
    row.className = 'automation-step';
    row.innerHTML = '<span class="step-index">' + (i + 1) + '.</span>' +
      '<span class="step-name">' + escapeHtml(step.mode_name || step.mode_id || 'default') + '</span>' +
      '<span class="step-prompt">"' + escapeHtml(step.prompt_preview || step.prompt || '') + '"</span>' +
      '<span class="step-meta">×' + step.repeat + (step.compress ? ' | Compress' : '') + '</span>';
    automationStepsEl.appendChild(row);
  }
}

function addAutomationStep() {
  const prompt = messageInput.value.trim();
  if (!prompt) return;
  const modeId = currentAgenticModeId();
  const mode = state.projectAgenticModes.find(m => m.id === modeId);
  const modeName = mode ? mode.name : (modeId || 'Default');
  const repeat = Math.max(1, Math.min(50, parseInt(automationRepeatEl && automationRepeatEl.value || '1', 10)));
  const compress = automationCompressEl ? automationCompressEl.checked : false;
  state.automationSequence.push({
    mode_id: modeId || '',
    mode_name: modeName,
    prompt: prompt || '',
    prompt_preview: prompt ? prompt.slice(0, 60) + (prompt.length > 60 ? '…' : '') : '',
    compress: compress,
    repeat: repeat,
  });
  messageInput.value = '';
  resizeTextarea();
  renderAutomationUI();
  messageInput.focus();
}

function clearAutomationSequence() {
  state.automationSequence = [];
  renderAutomationUI();
}

function finishAutomationRecording() {
  state.automationRecording = false;
  renderAutomationUI();
}

async function runAutomationSequence() {
  if (state.automationRunning || !state.automationSequence.length || !state.selectedChatId) return;
  state.automationRunning = true;
  if (automationSendBtn) automationSendBtn.disabled = true;
  setInputEnabled(false);
  sendBtn.disabled = true;
  try {
    for (const step of state.automationSequence) {
      if (!state.selectedChatId) break;
      for (let r = 0; r < step.repeat; ++r) {
        if (!state.selectedChatId) break;
        if (step.mode_id) {
          state.selectedChatAgenticModeId = step.mode_id;
          await api('POST', `/api/chats/${state.selectedChatId}/agentic-mode`, {
            selected_agentic_mode_id: step.mode_id,
          });
          renderAgenticModeLabel();
        }
        if (step.prompt) {
          messageInput.value = step.prompt;
          await sendMessage();
        }
        if (step.compress && state.projectAllowManualCompress) {
          await new Promise(res => setTimeout(res, 400));
          if (compressBtn) compressBtn.click();
        }
      }
    }
  } finally {
    state.automationRunning = false;
    if (automationSendBtn) automationSendBtn.disabled = false;
    setInputEnabled(true);
    sendBtn.disabled = !state.selectedChatId;
  }
}

if (automationAddStepBtn) { automationAddStepBtn.addEventListener('click', addAutomationStep); }
if (automationClearBtn)  { automationClearBtn.addEventListener('click', clearAutomationSequence); }
if (automationDoneBtn)   { automationDoneBtn.addEventListener('click', finishAutomationRecording); }
if (automationSendBtn)   { automationSendBtn.addEventListener('click', runAutomationSequence); }
