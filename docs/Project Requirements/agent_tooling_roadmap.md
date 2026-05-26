# Agent Tooling Roadmap

## Selected Tools and Implementation Notes

This document summarizes the agent tools worth investing in for this project, what needs to be implemented, and how each tool becomes practically useful.

---

## 1. Execution Sandbox

### Status
Recommended for implementation.

### Why It Is Useful
An execution sandbox lets the agent move beyond explanation and actually run code, test assumptions, process files, transform data, and generate outputs.

It is one of the highest-value tools because it turns the model from a conversational assistant into a working assistant.

### What Needs To Be Implemented

#### Python Runtime
You will need a controlled Python environment.

Possible approaches:
- Install Python directly on the server
- Use a portable Python distribution
- Run Python inside a container
- Use a per-task isolated environment

Recommended direction:
- Containerized Python sandbox if security matters
- Portable Python if deployment simplicity matters more than isolation

#### Package Management
The sandbox should support common packages such as:
- pandas
- numpy
- matplotlib
- openpyxl
- python-docx
- pypdf / pymupdf
- requests, if network access is allowed

You may want package allowlists rather than unrestricted installs.

#### JavaScript Runtime
Optional, but useful.

Possible approaches:
- Node.js installed on the server
- A containerized Node runtime
- Lightweight JS execution for quick scripts

Useful for:
- Web tooling
- JSON manipulation
- Frontend project work
- Running tests for JavaScript/TypeScript projects

#### File Access Rules
The sandbox should have controlled access to:
- A working directory
- Uploaded files
- Agent-generated files
- Temporary output folders

Avoid giving it unrestricted system access.

#### Output Capture
The system should capture:
- stdout
- stderr
- exit code
- generated files
- execution timeout
- runtime errors

#### Safety Controls
Add:
- Timeouts
- Memory limits
- File size limits
- Network access controls
- Process isolation
- Allowlisted commands or libraries

### Best Use Cases
- Running code snippets
- Testing generated code
- Data analysis
- Spreadsheet processing
- File conversion
- Chart generation
- Report generation
- Simulation or calculation tasks

---

## 2. Planner / Task Decomposition Tool

### Status
Recommended for implementation.

### Why It Is Useful
A planner helps the agent break large goals into smaller executable steps before acting.

This is especially useful when a user asks for something broad, such as:
- “Build this application for me”
- “Research this and create a report”
- “Analyze these files and summarize what matters”
- “Fix this project”

Without a planner, the model may jump straight into tool use without knowing the full sequence of work.

### What Needs To Be Implemented

#### Structured Plan Output
The planner should produce a machine-readable plan.

Example schema:

```json
{
  "goal": "Build a basic web application",
  "steps": [
    {
      "id": 1,
      "task": "Inspect the project files",
      "tool_hint": "filesystem",
      "status": "pending"
    },
    {
      "id": 2,
      "task": "Identify the framework and dependencies",
      "tool_hint": "filesystem",
      "status": "pending"
    },
    {
      "id": 3,
      "task": "Implement the requested feature",
      "tool_hint": "file_edit",
      "status": "pending"
    },
    {
      "id": 4,
      "task": "Run tests or validation",
      "tool_hint": "execution_sandbox",
      "status": "pending"
    }
  ]
}
```

#### Plan Updating
The planner should not be a one-time step only.

It should be able to:
- Mark tasks complete
- Add new tasks when discoveries happen
- Remove irrelevant tasks
- Reorder work
- Capture blockers

#### Tool Selection Hints
Each step can include a suggested tool:
- web_search
- rag_search
- filesystem
- file_edit
- execution_sandbox
- git
- command_line
- document_parser

The model should still be allowed to adjust if the plan is wrong.

#### Completion Criteria
Each task should have a clear done condition.

Example:

```json
{
  "task": "Run the application tests",
  "done_when": "All tests pass or failures are summarized with next actions"
}
```

### Best Use Cases
- Software development tasks
- Multi-document analysis
- Research workflows
- Large refactors
- File generation
- Agent loops

---

## 3. Goal / Task Runner With Agent Looping

### Status
Highest-priority recommendation.

### Why It Is Useful
A goal/task runner lets the agent work through a complex objective over multiple tool-use cycles instead of trying to answer in one pass.

This is what makes an agent feel truly agentic.

The user gives a goal, and the agent repeatedly:
1. Plans the next action
2. Uses a tool
3. Observes the result
4. Updates the plan
5. Continues until the goal is complete or blocked

---

# How The Goal / Task Runner Works

## Basic Loop

A useful loop looks like this:

```text
User goal
   ↓
Create or update plan
   ↓
Choose next task
   ↓
Choose tool
   ↓
Execute tool
   ↓
Observe result
   ↓
Update state
   ↓
Continue, stop, or ask for input
```

The key is that the agent does not simply answer once. It keeps moving through the work until it reaches a stopping condition.

---

## What Gets Looped?

You do not loop the exact same prompt repeatedly.

Instead, each loop passes the agent the current task state:

```json
{
  "original_goal": "Develop this application for me",
  "current_plan": [],
  "completed_steps": [],
  "current_files": [],
  "latest_observation": "",
  "next_decision_needed": "What should be done next?"
}
```

Each iteration asks:

> Given the goal, the current state, and the latest observation, what is the next best action?

That action may be:
- Search
- Read a file
- Edit a file
- Run code
- Run tests
- Commit changes
- Ask the user for a missing decision
- Stop and summarize

---

## Example: “How Can You Develop This Application For Me?”

Assume the user asks:

> Can you develop this application for me?

A looping agent would not try to do everything in one response.

It would break the goal into multiple loops.

---

## Loop 1: Understand the Goal

### Agent State
```json
{
  "goal": "Develop this application for the user",
  "known_information": [],
  "unknowns": [
    "What kind of application?",
    "What features are required?",
    "Is there an existing codebase?",
    "What tech stack should be used?"
  ]
}
```

### Possible Action
Ask the user for requirements, unless enough context already exists.

### Output
A requirements question or an initial inferred plan.

---

## Loop 2: Inspect Existing Materials

If the user provides files or a repository, the agent inspects them.

### Tool Use
- filesystem
- git
- document parser
- RAG search, if project docs exist

### Agent Looks For
- README
- package files
- app structure
- framework
- existing routes/components
- tests
- build scripts

### Result
The agent updates the plan based on reality instead of guessing.

---

## Loop 3: Create a Development Plan

The agent creates a concrete implementation plan.

Example:

```json
{
  "steps": [
    "Identify app framework",
    "Install or verify dependencies",
    "Create feature branch",
    "Implement backend endpoint",
    "Implement frontend UI",
    "Add validation",
    "Add tests",
    "Run test suite",
    "Fix errors",
    "Summarize changes"
  ]
}
```

---

## Loop 4: Implement First Change

The agent chooses one task and executes it.

Example:
- Edit backend route
- Add database model
- Create UI component
- Update config file

### Tool Use
- file edit
- command line
- git

### Result
Files are changed.

---

## Loop 5: Validate

The agent runs validation.

### Tool Use
- execution sandbox
- command line
- test runner

### Possible Results
- Tests pass
- Tests fail
- Dependencies missing
- Build fails
- Type errors appear

---

## Loop 6: React to Validation

If tests fail, the agent loops again.

### Example Observation
```text
TypeScript build failed because UserProfileCard requires a missing prop.
```

### Next Action
Edit the component or caller to fix the missing prop.

The agent then validates again.

---

## Loop 7: Continue Until Done

The agent repeats:

```text
edit → run → observe → fix → run → observe
```

until:
- The feature works
- Tests pass
- The goal is blocked
- The agent hits a configured iteration limit
- The user needs to make a decision

---

## Loop 8: Final Summary

When complete, the agent summarizes:
- What changed
- Which files were edited
- What tests were run
- What passed or failed
- Remaining risks
- Suggested next steps

---

# Important Design Concepts For The Goal Runner

## 1. State Object

The loop needs durable state.

Recommended fields:

```json
{
  "goal": "",
  "plan": [],
  "completed_steps": [],
  "current_step": "",
  "observations": [],
  "files_changed": [],
  "tools_used": [],
  "blockers": [],
  "iteration_count": 0,
  "status": "running"
}
```

---

## 2. Stop Conditions

The agent should stop when:

- The goal is complete
- The next action requires user input
- A safety rule blocks the task
- The same error repeats too many times
- The maximum loop count is reached
- The agent lacks required permissions
- The plan has no remaining useful actions

Without stop conditions, agents can spin forever.

---

## 3. Iteration Limits

Recommended defaults:

```json
{
  "max_iterations": 10,
  "max_tool_calls_per_iteration": 3,
  "max_repeated_error_count": 2
}
```

For larger development tasks, allow manual continuation.

---

## 4. Tool Result Summaries

Each loop should summarize tool results into state.

Do not dump the entire raw output back into the next loop unless necessary.

Example:

```json
{
  "latest_observation": "npm test failed: 2 TypeScript errors in src/App.tsx caused by missing props."
}
```

This keeps context manageable.

---

## 5. Planner + Runner Relationship

The planner creates and updates the plan.

The runner executes it.

Recommended separation:

```text
Planner:
- What should be done?

Runner:
- Do the next thing.

Critic / Validator:
- Did it work?
```

Even if these are all powered by the same model, treating them as separate roles improves reliability.

---

## 6. User Interrupts

The user should be able to interrupt the loop.

Examples:
- “Stop”
- “Use React instead”
- “Skip tests”
- “Do not edit that file”
- “Explain what you changed”

The runner should merge the new instruction into the state and update the plan.

---

## 7. Human Approval Gates

For risky actions, require approval.

Examples:
- Deleting files
- Running destructive commands
- Installing dependencies globally
- Pushing to git
- Making external API calls with side effects
- Modifying production configs

---

## 8. Git Integration

For coding tasks, the runner should use git defensively.

Useful actions:
- Check status before changes
- Create a branch
- Track changed files
- Show diffs
- Commit only after validation
- Never overwrite user changes silently

---

# Possible Structured Data Query Tool

## Status
Not planned for this project right now.

## Why It Is Still Worth Noting
A structured data query tool can be very useful later if the agent needs reliable answers from databases, analytics systems, or structured business data.

RAG is good for fuzzy retrieval.

SQL or graph queries are good for exact answers.

### Possible Future Uses
- Querying app telemetry
- Looking up user records
- Analyzing product usage
- Pulling business metrics
- Searching normalized knowledge graphs
- Combining exact database facts with RAG context

### Recommendation
Do not implement now, but keep the architecture open so a SQL or graph query tool can be added later.

---

# Document Parsing Expansion

## Status
Already included, but should be expanded.

## Current Limitation
The agent can parse documents uploaded by the user, but if the agent downloads a PDF or Word document itself, it may not automatically be able to parse that downloaded file.

## Desired Capability
The agent should be able to parse documents it obtains during its own workflow.

Examples:
- Downloaded PDFs from web research
- Word documents retrieved from a file system
- Documents generated by another tool
- Attachments pulled from an internal source

## What Needs To Be Implemented

### Unified File Intake Pipeline
Any file acquired by the agent should enter a common file processing pipeline.

Pipeline:

```text
File acquired
   ↓
Detect file type
   ↓
Extract text / metadata
   ↓
Store parsed representation
   ↓
Make content available to the model
```

### Supported Formats
Prioritize:
- PDF
- DOCX
- TXT
- Markdown
- HTML
- CSV
- XLSX

### Metadata Extraction
Capture:
- File name
- File type
- Source URL or path
- Created/downloaded time
- Page count
- Author/title if available

### Parsing Output
Return structured chunks:

```json
{
  "file_name": "example.pdf",
  "type": "pdf",
  "chunks": [
    {
      "page": 1,
      "text": "..."
    }
  ]
}
```

### RAG Integration
Optionally index parsed documents into the RAG database.

This is especially useful when:
- The document is long
- The agent will need to refer back to it
- Multiple documents are being compared

---

# Recommended Priority Order

## Priority 1: Goal / Task Runner
Most important for agentic behavior.

Build this first because it coordinates everything else.

## Priority 2: Planner / Task Decomposition Tool
Very useful with the goal runner.

The planner gives the runner a structured path instead of making each loop improvise.

## Priority 3: Execution Sandbox
Powerful, but heavier to implement.

This gives the runner the ability to test, calculate, analyze, and generate real outputs.

## Priority 4: Document Parsing Expansion
Important quality-of-life improvement.

Especially useful for web research, downloaded files, and automated document workflows.

## Priority 5: Structured Data Query Tool
Do not build now.

Keep it as a future architectural possibility.

---

# Suggested Minimum Viable Implementation

A practical first version could include:

1. Planner produces JSON task list
2. Runner stores task state
3. Runner executes one step at a time
4. Runner can call existing tools
5. Runner summarizes observations after each step
6. Runner stops after max iterations or completion
7. User can approve continuation for longer tasks

Minimum state object:

```json
{
  "goal": "",
  "plan": [],
  "completed_steps": [],
  "current_step": "",
  "latest_observation": "",
  "iteration_count": 0,
  "status": "running"
}
```

Minimum loop:

```text
plan → act → observe → update → repeat
```

This would already be enough to make the system feel significantly more capable.
