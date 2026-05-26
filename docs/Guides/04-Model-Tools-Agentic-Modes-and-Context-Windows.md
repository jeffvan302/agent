# Model Tools, Agentic Modes, and Context Windows

## Model Tools

A model tool is a sub-agent presented to a parent model as a callable tool. When called, it receives the caller's task instructions and executes using its own chosen model, tool access and optional agentic mode.

Click `Model Tools` in the desktop toolbar. Use `+ Add`, select the new tool and configure it, then click `Save`.

### Model tool fields

| Setting | Behavior |
| --- | --- |
| `Tool Name` | Tool identity. The callable alias is sanitized and prefixed as an agent tool. |
| `Description (shown to calling model)` | Helps the parent decide when to call the tool. Include purpose and expected result. |
| `AI Model` | Provider/model used by the sub-agent. |
| `Agentic Mode` | Optional instruction framework always applied when this model tool runs. |
| `MCP Server Access` and `Use this MCP server` | External MCP servers accessible to this sub-agent, including any configured variable values. |
| `Built-in Tool Access` | Check individual built-ins: `PowerShell`, `Project Filesystem`, `Planner`, `Completion Driver`, `Questionnaire`, `Sleep`. |
| `RAG Library Access` | RAG permissions scoped to the sub-agent: enabled/read/write/tool/delete/write-file/default ingest and related retrieval fields. |
| `Tool Instructions (not exposed to calling model)` | Private instructions supplied to the sub-agent after it is invoked. `Import Markdown` loads this from a file. |
| `Test` | Shows caller-visible declaration context and the sub-agent prompt/context preview. |

### Model tool behavior

- A model tool does not use context window compression. It is given the information needed for the invocation rather than maintaining an independent chat history.
- Project and runtime variables resolved for the parent chat are passed to the model tool. The sub-agent should use these supplied values directly for paths, MCP arguments and built-in tools.
- MCP and built-in tool access is scoped independently for each model tool. Grant the minimum required capabilities.
- RAG libraries exposed to a model tool operate as active RAG tool servers. The visible `Inject on start (inactive)` option is not an active passive-retrieval feature.
- A configured `Completion Driver` should be granted when a long sub-agent workflow must explicitly signal completion.

### Good model tool design

Use one narrow purpose per tool, for example document researcher, code auditor or deployment verifier. In the description state what the parent should supply; in private instructions state output format, tool expectations and completion requirements.

## Agentic Modes

The UI calls these `Agentic Modes`. If a request or older note says "genetic modes," it refers to this same feature.

1. Click `Agentic Modes`.
2. Click `New Mode`.
3. Enter `Name` and `Instructions`.
4. Optionally use `Import` to load instructions from a file.
5. Close the manager when saved.

Modes are reusable instruction profiles and can be applied in three places:

| Placement | Result |
| --- | --- |
| `Project Settings` -> `Default Agentic Mode` | Default behavior for new/existing project chats unless overridden. |
| `Project Settings` -> `Enabled Agentic Modes` | Allows web users to select among checked modes in a chat. |
| `Model Tools` -> `Agentic Mode` | Applies a fixed mode whenever that sub-agent tool runs. |

Mode-aware built-in tools can be restricted in `Project Settings`, such as Completion Driver or User Questionnaire.

## Context Window Configurations

Click `Context Window` to create global configurations. A project chooses one of these definitions through `Project Settings` -> `Context Window Compression`.

Every configuration has:

| Setting | Purpose |
| --- | --- |
| `Name` | Selectable configuration name. |
| `Strategy` | Compression algorithm. |
| `Pre-pass` | A separately configured compression definition run before this strategy. Default is none. |
| `Frequency (every N prompts, 0 = manual)` | Normal scheduled compression interval. |
| `Context trigger (% of context window)` | Context capacity trigger for automatic compression. |

Use `Add`, `Delete` and `Duplicate` to manage definitions. Use the inline help area when adjusting a field.

### Strategies

| Strategy | What it sends forward | Best use |
| --- | --- | --- |
| `Truncate Top (Rolling Window)` | Only the configured number of newest model-visible messages. | Simple cost control or stateless command/task projects. |
| `Rolling Summary` | A model-generated or deterministic accumulated summary plus a recent verbatim tail. | Long discussions requiring continuity at lower token cost. |
| `Tool Trace Distillation` | A distilled account of tool calls/results rather than verbose raw traces. | Tool-heavy projects; commonly configured as a pre-pass. |
| `Hierarchical Structured Compression` | Configurable memory, pinned text, narrative summary, structured state and recent turns. | Complex long-running projects needing durable decisions and state. |

### Configure a pre-pass

A pre-pass selection comes only from configurations that have already been created:

1. Add a new configuration, for example `CleanTools`.
2. Set its strategy to `Tool Trace Distillation`, configure its model/prompt and save it.
3. Edit the primary context configuration.
4. Set `Pre-pass` to `CleanTools`.
5. Save, then assign the primary configuration to a project.

This allows a tool-trace cleanup pass to be combined with Rolling Summary or Hierarchical Structured Compression without changing their main behavior.

## Truncate Top and Stateless Tasks

`Keep recent messages` determines how many prior model-visible messages survive compression.

To make each user prompt independent while still allowing the model to call tools and complete the current prompt:

1. Create a configuration with strategy `Truncate Top (Rolling Window)`.
2. Set `Keep recent messages` to `0`.
3. Set `Frequency` to `1`.
4. Assign it in the project's `Context Window Compression` setting.

During an active request, the request and its tool loop still continue normally. When the next user prompt is sent, no messages from the preceding prompt/tool execution are retained through this compression strategy.

Do not use `Rolling Summary` for this stateless behavior. A Rolling Summary configuration with `Keep recent messages` set to `0` removes its verbatim tail, but its summary still carries prior information.

## Rolling Summary Settings

For `Rolling Summary`, the visible editable controls include:

| Setting | Meaning |
| --- | --- |
| `Keep recent messages` | Verbatim tail retained beside the summary. |
| `Enable rolling summary` | Turns the summarization component on. |
| `Model` | Provider/model used to write the summary. |
| `Max tokens` | Target cap for summary output. |
| `Trigger (turns)` | Turn threshold before refreshing its summary. |
| `Summary instructions` | Prompt used to produce the rolling summary; `Default` restores the built-in prompt. |

This summary is performed by a selected model when configured and available; the service also has deterministic fallback behavior for summary construction.

## Tool Trace Distillation Settings

Tool Trace Distillation uses the same model/prompt panel as summary strategies:

| Setting | Meaning |
| --- | --- |
| `Enable tool trace distillation` | Activates tool-trace reduction. |
| `Model` | Model used for distilled tool history. |
| `Max tokens` | Bound on distilled trace output. |
| `Trigger (turns)` | Turn threshold for refresh. |
| Distillation instructions | Controls what conclusions, file changes, errors and tool outcomes survive. |

## Hierarchical Structured Compression Layers

| Layer | Controls | Purpose |
| --- | --- | --- |
| `Layer 0 - Artifact Memory` | `L0 Memory Tool`, capture/selection models and instructions, storage folder, max injected rows | Stores and selects durable artifacts/code memory. Default storage template falls back to `$ProjectFolder$\.agent\.memory\$CHATID$`. |
| `Layer 1 - Verbatim Pinning` | Enable, max pins, pin code blocks, URLs/paths, numbers/versions, first user message, `[PIN]` markers, explicit instructions | Preserves exact high-value messages. |
| `Layer 2 - Summary` | Enable, model, max tokens, trigger turns, summary instructions | Maintains narrative history. |
| `Layer 3 - Structured State` | Enable, model, max tokens, state instructions | Extracts durable structured project state. |
| `Layer 4 - Recency Window` | Enable, minimum recent turns | Retains raw recent conversation turns. |

## Project-Level Additional Trigger

The project setting `Force compression at input tokens` is independent of the selected strategy's own frequency and context percentage. Enable it when the project should compress more aggressively once the incoming context reaches a fixed estimated token size.

## Persistence

Global compression configurations are stored in `.config/context_compression_configs.json`. The selected configuration for a project is stored in project settings and compatibility assignment files. Per-chat compressed state and history are stored under `.data/projects/<project_id>/chats/<chat_id>/`.

