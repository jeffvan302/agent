# Planner / Task Decomposition Tool

The Planner tool is a built-in tool for keeping a persistent, machine-readable plan for a project or long-running interaction. It helps the agent break broad goals into smaller goals, subgoals, features, steps, blockers, notes, and tool hints.

By default, the planner stores its state in:

```text
$ProjectFolder$\.agent\planner.json
```

The storage folder can be changed in Project Settings under Built-in Tools -> Planner / Task Decomposition.

## When To Use It

Use the Planner tool when the request is too large to solve cleanly in one step, such as:

- Building or modifying an application.
- Planning a refactor.
- Breaking a feature into implementation steps.
- Tracking research progress.
- Tracking multi-document analysis.
- Capturing blockers and follow-up work.
- Preserving progress across a long interaction.

The tool is especially useful when the agent needs to keep track of what has already been done, what remains, and why the current next step matters.

## Plan Structure

The planner file is JSON. A typical plan looks like this:

```json
{
  "goal": "Build a basic web application",
  "goals": [
    {
      "id": 1,
      "title": "Understand the project",
      "status": "completed",
      "done_when": "Project structure and dependencies are known",
      "steps": [
        {
          "id": 2,
          "task": "Inspect project files",
          "tool_hint": "filesystem",
          "status": "completed",
          "done_when": "Important folders and entry points are identified"
        }
      ]
    }
  ],
  "features": [],
  "steps": [],
  "blockers": [],
  "notes": [],
  "tool_hints": [
    "web_search",
    "rag_search",
    "filesystem",
    "file_edit",
    "execution_sandbox",
    "git",
    "command_line",
    "document_parser"
  ],
  "updated_at": "2026-04-30T00:00:00Z"
}
```

## Supported Actions

The Planner tool accepts an `action` field.

### `get`

Loads the current plan without changing it.

```json
{
  "action": "get"
}
```

### `replace`

Replaces the entire plan with a new JSON object.

```json
{
  "action": "replace",
  "plan": {
    "goal": "Ship the release",
    "goals": [],
    "features": [],
    "steps": []
  }
}
```

### `update`

Merges top-level fields into the existing plan.

```json
{
  "action": "update",
  "plan": {
    "goal": "Build and validate the reporting feature"
  }
}
```

### `clear`

Clears the whole plan or a section.

```json
{
  "action": "clear",
  "section": "steps"
}
```

Use `"section": "all"` or omit `section` to reset the full planner document.

### `add_item`

Adds an item to a top-level section or to a nested child section under another item.

```json
{
  "action": "add_item",
  "section": "steps",
  "item": {
    "task": "Run validation",
    "tool_hint": "execution_sandbox",
    "status": "pending",
    "done_when": "Tests pass or failures are summarized"
  }
}
```

### `update_item`

Updates an existing item by `id`. Nested goals and steps are searched recursively.

```json
{
  "action": "update_item",
  "section": "steps",
  "id": 4,
  "item": {
    "status": "completed"
  }
}
```

Use `"section": "all"` to search all supported top-level sections.

### `remove_item`

Removes an item by `id`. Nested goals and steps are searched recursively.

```json
{
  "action": "remove_item",
  "section": "all",
  "id": 4
}
```

## Nested Goals And Steps

Nested planning uses `parent_id`, `parent_section`, and `child_section`.

- `parent_id`: The id of the goal, subgoal, or step to attach the new item to.
- `parent_section`: Optional section to search for the parent. Use `all` if unsure.
- `child_section`: The nested array to add to, such as `subgoals`, `goals`, `steps`, or `features`.

### Add A Subgoal Under A Goal

```json
{
  "action": "add_item",
  "section": "goals",
  "parent_id": 1,
  "parent_section": "goals",
  "child_section": "subgoals",
  "item": {
    "title": "Build authentication",
    "status": "pending",
    "done_when": "Users can register, log in, and log out"
  }
}
```

### Add A Step Under A Goal

```json
{
  "action": "add_item",
  "section": "steps",
  "parent_id": 1,
  "parent_section": "goals",
  "child_section": "steps",
  "item": {
    "task": "Create login form",
    "tool_hint": "file_edit",
    "status": "pending",
    "done_when": "Login form submits credentials"
  }
}
```

### Add A Nested Step Under Another Step

```json
{
  "action": "add_item",
  "section": "steps",
  "parent_id": 2,
  "parent_section": "all",
  "child_section": "steps",
  "item": {
    "task": "Add client-side validation",
    "tool_hint": "file_edit",
    "status": "pending",
    "done_when": "Invalid inputs show clear validation errors"
  }
}
```

### Add A Nested Feature Under A Goal

```json
{
  "action": "add_item",
  "section": "features",
  "parent_id": 1,
  "parent_section": "goals",
  "child_section": "features",
  "item": {
    "title": "Password reset",
    "status": "pending",
    "done_when": "Users can request and complete a password reset"
  }
}
```

## Recommended Item Fields

For goals and features:

```json
{
  "id": 1,
  "title": "Implement billing",
  "status": "pending",
  "done_when": "Users can subscribe, update plans, and cancel billing",
  "tool_hint": "file_edit"
}
```

For steps:

```json
{
  "id": 2,
  "task": "Run application tests",
  "status": "pending",
  "tool_hint": "execution_sandbox",
  "done_when": "All tests pass or failures are summarized with next actions"
}
```

For blockers:

```json
{
  "id": 3,
  "title": "Missing API credentials",
  "status": "blocked",
  "done_when": "Credentials are provided or a mock path is approved"
}
```

## Status Values

Recommended status values are:

- `pending`
- `in_progress`
- `completed`
- `blocked`
- `cancelled`

These are conventions rather than hard restrictions, so the model can use a different value if the project needs it.

## Tool Hints

Tool hints help the model choose the right capability for a step. Recommended values are:

- `web_search`
- `rag_search`
- `filesystem`
- `file_edit`
- `execution_sandbox`
- `git`
- `command_line`
- `document_parser`

The model may still choose a different tool if the plan is wrong or new information changes the best path.

## Practical Workflow

1. Start with `get` to see if a plan already exists.
2. Use `replace` or `update` to set the main project goal.
3. Use `add_item` to create top-level goals.
4. Use nested `add_item` calls to add subgoals, features, and steps under the relevant parent.
5. Use `update_item` as work progresses.
6. Use `remove_item` or `clear` when work becomes irrelevant.
7. Keep `done_when` current so completion is objective.

## Notes

- The planner is persistent across the interaction because it writes to disk.
- The planner is project-scoped through the configured storage folder.
- The default location under `.agent` keeps planning metadata near the project without mixing it into source files.
- The planner is meant to guide the model, not lock it into a bad plan. If discoveries invalidate the plan, update the plan.
