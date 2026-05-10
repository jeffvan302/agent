# Project Plan

## Goal
Improve the web UI automation feature so that:
1. The automation panel persists across chat switches and does not vanish after clicking **Done**.
2. Users can select any step in the sequence, edit its prompt/repeat/compress values, and update it.
3. The **Send** button acts as an **Add / Update Step** button while the automation panel is open (it does not send to the model).
4. The **Automate** button toggles the panel open and closed.
5. Steps are selectable and deselectable via click to indicate which one is being edited.

## Project Type
Coding (frontend JavaScript + minor HTML/CSS)

## Audience / End User
End-user interacting with the web UI to run multi-step prompt sequences across different chats.

## Confirmed Requirements
- Automation sequence remains visible and runnable after switching chats.
- Clicking a step in the list loads its fields (prompt, repeat, compress) into the composer controls for editing.
- Clicking a selected step again deselects it.
- When the automation panel is open, the main **Send** button becomes "Add Step" (or "Update Step") and appends / overwrites the step accordingly.
- The **Automate** button toggles the automation panel on/off.
- Existing **Clear** and **Send Sequence** buttons remain operational.
- Sequence does not need to survive full page reloads (session-level persistence is enough).

## Assumptions
- When the panel is open and no step is selected, the composer functions as "Add new step at end of sequence".
- When a step is selected, changing the input and clicking the send button updates that exact step in place.
- We will remove the **Done** button; the **Automate** button itself serves as the toggle to show/hide the panel.
- Selected step will be visually distinguished via a CSS class (e.g. `.selected`), using existing colour variables where possible.
- No server/API changes are required; only frontend `www/js/automation.js`, `www/js/app.js`, and `www/index.html`.

## Out of Scope
- Saving / restoring automation sequences across full page reloads (localStorage).
- Reordering steps via drag-and-drop.
- Multi-select or batch edit of steps.
- Server-side changes or new API endpoints.

## Success Criteria
- [ ] Clicking **Automate** opens the panel; clicking **Automate** again hides it and restores normal send behaviour.
- [ ] Switching between chats does not destroy the automation sequence.
- [ ] Clicking a step highlights it, populates `message-input` and `automation-repeat` / `automation-compress`, and the send button reads "Update Step".
- [ ] Clicking the highlighted step again deselects it, clears composer fields, and the send button reads "Add Step".
- [ ] Pressing the repurposed send button adds or updates the step correctly.
- [ ] **Clear** wipes the sequence and resets selection.
- [ ] **Send Sequence** runs the full sequence on the currently selected chat.
- [ ] All existing non-automation chat behaviour remains unchanged when the panel is closed.

## Risks / Blockers
- Risk: Hijacking the **Send** button may accidentally affect normal message sending if the panel-open detection is fragile.  
  Mitigation: Gate send-button override strictly on automation-panel visibility (`display !== 'none'`).
- Risk: Step-click events may conflict with other click handlers.  
  Mitigation: Attach handler to the step container and use event delegation.

## Deliverables
- Updated `www/js/automation.js`
- Updated `www/js/app.js` (send-button integration, `renderAutomateButton` changes)
- Minor updates to `www/index.html` (button labels / remove Done button)
- Optional CSS addition in `www/css/style.css` for selected step highlight

## Phases

### Phase 1: State & Logic Refactor
Purpose: Prepare the codebase to support persistent sequence and step selection.

Steps:
- [ ] Simplify automation state
  - Purpose: Remove `automationRecording` boolean; only track whether panel is open/closed and which step is selected.
  - Depends on: Nothing
  - Output: `automation.js` refactored state model (`selectedAutomationStepIndex`, `automationPanelOpen`).
  - Done when: `renderAutomationUI()` uses a single flag to toggle panel visibility and renders steps without a separate "recording" vs "done" branch.
  - Verification: No console errors; panel visibility toggles correctly with a temporary flag.
  - Tool hint: `read`, `edit`
  - Status: pending

- [ ] Add step selection state & click handling
  - Purpose: Enable selecting/deselecting a step and storing its index.
  - Depends on: Simplify automation state
  - Output: Clicking a step sets `state.selectedAutomationStepIndex`; clicking again sets `-1`.
  - Done when: Step container has event listener that updates the selected index and re-renders with a `.selected` class.
  - Verification: Console logs correct index on each click; visual selection toggles.
  - Tool hint: `edit`
  - Status: pending

- [ ] Populate composer when step selected
  - Purpose: Load step data into composer controls for editing.
  - Depends on: Add step selection state
  - Output: `messageInput.value`, `automationRepeatEl.value`, `automationCompressEl.checked` reflect selected step.
  - Done when: Selecting a step fills the three fields; deselecting clears them.
  - Verification: Manual click test on at least two different steps.
  - Tool hint: `edit`
  - Status: pending

### Phase 2: UI & Button Behaviour
Purpose: Wire the send button and automate toggle to the new logic.

Steps:
- [ ] Repurpose Send button while panel is open
  - Purpose: Make the global Send button behave as add/update step instead of calling `sendMessage()`.
  - Depends on: Add step selection state
  - Output: Send button label changes to "Add Step" or "Update Step"; click handler routes to `addOrUpdateAutomationStep()`.
  - Done when:
    - `sendBtn` textContent changes based on selection.
    - Clicking it pushes a new step when nothing selected, or updates the selected step.
    - After action, composer fields clear, selection resets, list re-renders.
  - Verification: Add two steps, select the first, edit prompt, hit send → first step updates; second step untouched.
  - Tool hint: `edit`
  - Status: pending

- [ ] Make Automate button toggle panel
  - Purpose: Allow user to open/close automation panel with the same button.
  - Depends on: Simplify automation state
  - Output: `automateBtn` click handler toggles `state.automationPanelOpen` instead of only opening.
  - Done when: Repeated clicks on **Automate** show/hide the panel and restore normal Send button behaviour when hidden.
  - Verification: Toggle three times; assert panel visibility and send-button label.
  - Tool hint: `edit`
  - Status: pending

- [ ] Remove / repurpose Done button
  - Purpose: Eliminate the old "Done" concept and provide a close button if desired.
  - Depends on: Make Automate button toggle panel
  - Output: `automation-done-btn` removed from HTML or changed to a **Close** button that simply hides the panel.
  - Done when: No "Done" button remains; panel can still be closed.
  - Verification: `index.html` inspected for absence of Done button.
  - Tool hint: `edit`
  - Status: pending

### Phase 3: Persist Sequence Across Chat Switches
Purpose: Fix the issue where switching chats destroys the automation sequence.

Steps:
- [ ] Prevent `renderAutomateButton` from clearing sequence
  - Purpose: Keep `state.automationSequence` intact when the selected chat changes.
  - Depends on: Nothing
  - Output: `renderAutomateButton` only updates visibility/disabled state of the Automate button; it no longer resets `automationSequence`.
  - Done when: Sequence array is unchanged after switching chats via UI and `automationPanelEl` stays visible if previously open.
  - Verification: Add three steps, switch chat, confirm steps still listed.
  - Tool hint: `edit`
  - Status: pending

### Phase 4: Styling & Polish
Purpose: Provide clear visual affordance for selected step and updated button labels.

Steps:
- [ ] Add CSS for selected automation step
  - Purpose: Let users see which step is active for editing.
  - Depends on: Add step selection state
  - Output: `.automation-step.selected` rule added to stylesheet.
  - Done when: Selected step has a differentiable background/border colour.
  - Verification: Visual inspection in browser.
  - Tool hint: `edit`
  - Status: pending

- [ ] Update button labels in HTML/CSS if needed
  - Purpose: Ensure static HTML matches new dynamic labels.
  - Depends on: Repurpose Send button while panel is open
  - Output: `automation-send-btn` label stays as "Send Sequence"; other labels reflect new terminology.
  - Done when: No misleading or static outdated labels remain.
  - Verification: Review `index.html` text content.
  - Tool hint: `edit`
  - Status: pending

### Phase 5: Testing & QA
Purpose: Confirm all acceptance criteria are met.

Steps:
- [ ] Manual smoke test
  - Purpose: Walk through the entire user flow.
  - Depends on: All prior phases
  - Output: Passed checklist
  - Done when:
    1. Open panel → add 3 steps.
    2. Switch chat → sequence still visible.
    3. Click step 2 → fields populate.
    4. Click step 2 again → deselects.
    5. Select step 1, change prompt & repeat, hit send → step 1 updated.
    6. Close panel → Send button normal, message sends to model.
    7. Open panel → Send button becomes Add/Update Step again.
    8. Press Send Sequence → runs on current chat.
  - Verification: Each sub-step observed without console errors.
  - Tool hint: Browser dev tools
  - Status: pending

## Execution Handoff
The execution agent should start with:
1. Reading `www/js/automation.js`, `www/js/app.js`, and `www/index.html` to confirm current code context.
2. Performing Phase 1 changes (state refactor and selection logic).
3. Performing Phase 2 changes (button hijack and toggle).
4. Performing Phase 3 changes (chat switch persistence).
5. Performing Phase 4 changes (CSS/HTML polish).
6. Running the Phase 5 manual smoke test checklist.

Do not begin execution until:
- [ ] Planner state is populated.
- [ ] Requirements are clear enough to proceed.
- [ ] Each actionable step has `done_when`.
- [ ] Each actionable step has verification criteria.
- [ ] Blockers are resolved or explicitly marked.
