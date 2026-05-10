# Hierarchical Structured Compression for Context Windows

## Overview

Hierarchical Structured Compression (HSC) is a multi-layered strategy for managing long conversation context windows. Rather than relying on a single compression technique, HSC combines four complementary layers — each designed to preserve a different type of information that degrades differently under compression.

The result is a compressed context that retains verbatim precision where it matters, narrative coherence for intent and arc, structured data for quick lookup, and full fidelity for the most recent exchanges.

See also: `l0_artifact_memory_requirements.md` for the implemented Layer 0 artifact-memory extension that adds per-chat code and diagram recall on top of the existing HSC layers.

---

## Architecture

The compressed context is assembled from four layers, each maintained independently:

```
┌─────────────────────────────────────────────────┐
│              COMPRESSED CONTEXT                 │
│                                                 │
│  ┌───────────────────────────────────────────┐  │
│  │  Layer 1: Verbatim Pinned Messages        │  │
│  │  (mechanically selected, never modified)  │  │
│  └───────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────┐  │
│  │  Layer 2: Model-Generated Running Summary │  │
│  │  (regenerated each cycle, not recursive)  │  │
│  └───────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────┐  │
│  │  Layer 3: Structured State Object         │  │
│  │  (schema-enforced key-value state)        │  │
│  └───────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────┐  │
│  │  Layer 4: Recency Window                  │  │
│  │  (last N turns, verbatim)                 │  │
│  └───────────────────────────────────────────┘  │
│                                                 │
└─────────────────────────────────────────────────┘
```

---

## Layer 1: Verbatim Pinning

### Purpose

Certain messages must be preserved exactly as written. Model summarization will paraphrase, round numbers, simplify code, or subtly reframe instructions. Verbatim pinning prevents this by mechanically identifying high-precision content and locking it in place — no model ever rewrites these messages.

### What Gets Pinned

| Content Type | Detection Method | Why It Must Be Verbatim |
|---|---|---|
| Code blocks | Regex for fenced code blocks (`` ``` ``) or indented blocks | A single changed character breaks code |
| URLs and file paths | Regex for `http(s)://`, `/path/to/`, file extensions | URLs cannot be paraphrased |
| Numerical values and data | Regex for numbers, currencies, percentages, dates | "About 97%" is not the same as "97.3%" |
| Explicit user instructions | Heuristic: imperative sentences in user turns, quoted requirements | The user's exact words define the task |
| The first user turn | Always pin turn index 0 | Establishes the original intent and scope |
| User-flagged messages | Keyword detection: "remember this", "important", "don't forget" | The user explicitly marked it as critical |

### Implementation

```python
import re

PIN_PATTERNS = [
    ("code_block", re.compile(r"```[\s\S]*?```")),
    ("url", re.compile(r"https?://\S+")),
    ("file_path", re.compile(r"(?:/[\w.\-]+){2,}")),
    ("numeric_data", re.compile(r"\b\d+[\d.,]*%?\b")),
    ("user_flag", re.compile(r"\b(remember this|important|don't forget|keep this)\b", re.I)),
]

def should_pin(message: dict, turn_index: int) -> bool:
    """Determine if a message should be preserved verbatim."""
    # Always pin the first user message
    if turn_index == 0 and message["role"] == "user":
        return True

    content = message["content"]

    # Pin if any high-precision pattern is found
    for pattern_name, pattern in PIN_PATTERNS:
        if pattern.search(content):
            return True

    return False

def extract_pinned_messages(conversation: list[dict]) -> list[dict]:
    """Return all messages that should be preserved verbatim."""
    pinned = []
    for i, message in enumerate(conversation):
        if should_pin(message, i):
            pinned.append({
                "turn_index": i,
                "role": message["role"],
                "content": message["content"],
                "pin_reason": "auto-detected high-precision content",
            })
    return pinned
```

### Token Budget

Verbatim pinned messages should be allocated no more than **20-30%** of the total compressed context budget. If pinned messages exceed this, prioritize by recency and by pin type (code and explicit instructions outrank bare numbers).

### Risks and Mitigations

- **Over-pinning:** If too many messages match patterns, the compression ratio drops. Mitigation: set a max pin count and rank by signal density (a message with both code and a URL outranks one with just a number).
- **Stale pins:** A code block from turn 5 may have been superseded by a corrected version in turn 30. Mitigation: when two pinned messages contain overlapping code blocks, keep only the later one unless both are referenced in the recency window.

---

## Layer 2: Model-Generated Running Summary

### Purpose

A narrative summary captures what no mechanical method can: the *meaning* of the conversation. Why is the user here? What approach was chosen and why? What was tried and failed? What's the current trajectory? This layer gives the model (in future turns) a coherent sense of the conversation's arc.

### Critical Design Decision: Regeneration, Not Recursion

The most important implementation detail is that the summary is **regenerated** from `(previous summary + new turns)`, not from `(previous summary alone)`. This distinction prevents lossy drift.

```
BAD (recursive summarization):
  summary_v1 = summarize(turns 1-10)
  summary_v2 = summarize(summary_v1)          ← drift compounds
  summary_v3 = summarize(summary_v2)          ← original meaning eroding

GOOD (regenerative summarization):
  summary_v1 = summarize(turns 1-10)
  summary_v2 = summarize(summary_v1 + turns 11-20)   ← anchored to raw turns
  summary_v3 = summarize(summary_v2 + turns 21-30)   ← drift is bounded
```

In the regenerative approach, the model always has access to the most recent raw turns, so it can correct any drift that crept into the previous summary. The summary decays gracefully rather than catastrophically.

### Prompt Template

```python
SUMMARY_PROMPT = """You are compressing a conversation for future context.

PREVIOUS SUMMARY (may be empty if this is the first compression):
{previous_summary}

NEW TURNS SINCE LAST SUMMARY:
{new_turns}

STRUCTURED STATE (for reference, do not duplicate):
{structured_state}

Generate a concise narrative summary that captures:
1. The user's original goal and any evolution of that goal
2. Key decisions made and their reasoning
3. Approaches attempted, including failures and why they failed
4. Current status and what the next step should be
5. Any constraints, preferences, or requirements the user has stated

Rules:
- Do NOT include specific code, URLs, or exact numbers (those are preserved elsewhere)
- Do NOT summarize-from-summary: treat the previous summary as context, but prioritize
  accuracy from the new turns if there are any contradictions
- Keep the summary under {max_tokens} tokens
- Write in third person past/present tense ("The user asked for...", "The current approach is...")
- Flag any ambiguity or unresolved questions explicitly
"""
```

### Implementation

```python
def generate_running_summary(
    previous_summary: str,
    new_turns: list[dict],
    structured_state: dict,
    model_client,
    max_tokens: int = 500,
) -> str:
    """Generate a new running summary incorporating recent turns."""
    new_turns_text = "\n".join(
        f"[{t['role']}]: {t['content']}" for t in new_turns
    )

    prompt = SUMMARY_PROMPT.format(
        previous_summary=previous_summary or "(No previous summary — this is the first compression.)",
        new_turns=new_turns_text,
        structured_state=json.dumps(structured_state, indent=2),
        max_tokens=max_tokens,
    )

    response = model_client.generate(prompt, max_tokens=max_tokens)
    return response.text
```

### When to Trigger Re-summarization

Re-summarization should occur when one of these conditions is met:

- The raw unsummarized turns exceed a token threshold (e.g., 2,000 tokens of new content since last summary).
- The turn count since last summary exceeds a fixed number (e.g., 8-10 turns).
- The total context window utilization exceeds 70% of the model's limit.

### Token Budget

The running summary should target **15-25%** of the total compressed context budget. Keeping it concise forces the model to prioritize what matters.

---

## Layer 3: Structured State Extraction

### Purpose

Narrative summaries are good for arc and intent but bad for precise lookup. "What constraints has the user mentioned?" is hard to answer from a paragraph of prose. Structured state extraction maintains a schema-enforced object that acts as a queryable ledger of facts, decisions, and open threads.

Because the format is rigid, it resists the drift that freeform summaries are prone to. A key either has a value or it doesn't — there's no room for subtle reframing.

### Schema Definition

```python
from dataclasses import dataclass, field
from typing import Optional

@dataclass
class ConversationState:
    # What the user is trying to accomplish
    primary_goal: str = ""

    # Hard requirements the user has stated
    constraints: list[str] = field(default_factory=list)

    # Preferences that are flexible but stated
    preferences: list[str] = field(default_factory=list)

    # Decisions that have been made (with reasoning)
    decisions: list[dict] = field(default_factory=list)
    # Each decision: {"what": str, "why": str, "turn": int}

    # Approaches that were tried and abandoned
    failed_approaches: list[dict] = field(default_factory=list)
    # Each: {"approach": str, "reason_abandoned": str, "turn": int}

    # Questions that are still unresolved
    open_questions: list[str] = field(default_factory=list)

    # Key entities referenced (people, files, tools, APIs)
    entities: dict[str, str] = field(default_factory=dict)
    # e.g. {"target_db": "PostgreSQL 15", "deploy_env": "AWS us-east-1"}

    # Current phase or step in the workflow
    current_phase: str = ""

    # Anything the user explicitly asked to remember
    user_flagged_notes: list[str] = field(default_factory=list)
```

### Extraction Prompt

```python
STATE_EXTRACTION_PROMPT = """You are updating a structured state object for a conversation.

CURRENT STATE:
{current_state_json}

NEW TURNS:
{new_turns}

Update the state object based on the new turns. Rules:
- Only ADD or MODIFY fields that the new turns provide evidence for.
- Do NOT remove existing entries unless the user explicitly contradicts or revokes them.
- For decisions and failed_approaches, include the turn index for traceability.
- Keep all values concise — single sentences, not paragraphs.
- Return ONLY valid JSON matching the schema. No commentary.

Schema:
{{
  "primary_goal": "string",
  "constraints": ["string"],
  "preferences": ["string"],
  "decisions": [{{"what": "string", "why": "string", "turn": int}}],
  "failed_approaches": [{{"approach": "string", "reason_abandoned": "string", "turn": int}}],
  "open_questions": ["string"],
  "entities": {{"key": "value"}},
  "current_phase": "string",
  "user_flagged_notes": ["string"]
}}
"""
```

### Implementation

```python
import json

def update_structured_state(
    current_state: ConversationState,
    new_turns: list[dict],
    model_client,
) -> ConversationState:
    """Use the model to update the structured state from new turns."""
    current_json = json.dumps(vars(current_state), indent=2)
    new_turns_text = "\n".join(
        f"[Turn {t['index']}][{t['role']}]: {t['content']}" for t in new_turns
    )

    prompt = STATE_EXTRACTION_PROMPT.format(
        current_state_json=current_json,
        new_turns=new_turns_text,
    )

    response = model_client.generate(prompt, max_tokens=800)

    try:
        updated = json.loads(response.text)
        return ConversationState(**updated)
    except (json.JSONDecodeError, TypeError):
        # If parsing fails, return the previous state unchanged
        return current_state
```

### Validation and Conflict Resolution

After extraction, apply mechanical validation:

```python
def validate_state(state: ConversationState) -> list[str]:
    """Check for inconsistencies in the state object."""
    warnings = []

    # Check for contradictory constraints
    for i, c1 in enumerate(state.constraints):
        for c2 in state.constraints[i+1:]:
            if appears_contradictory(c1, c2):  # simple keyword negation check
                warnings.append(f"Possible contradiction: '{c1}' vs '{c2}'")

    # Check that current_phase is not empty if decisions exist
    if state.decisions and not state.current_phase:
        warnings.append("Decisions exist but current_phase is empty")

    # Check for duplicate entries
    if len(state.constraints) != len(set(state.constraints)):
        warnings.append("Duplicate constraints detected")

    return warnings
```

### Token Budget

The structured state object should use **10-15%** of the compressed context budget. Its value is density — a lot of information in very few tokens.

---

## Layer 4: Recency Window

### Purpose

The most recent turns are almost always the most immediately relevant. They contain the active thread of conversation, the latest user request, and the freshest context. These are kept completely verbatim with no compression whatsoever.

### Sizing the Window

The recency window size is dynamic, not fixed. It should be calculated based on remaining token budget after the other three layers are accounted for:

```python
def calculate_recency_window(
    total_budget: int,
    pinned_tokens: int,
    summary_tokens: int,
    state_tokens: int,
    conversation: list[dict],
) -> list[dict]:
    """Determine how many recent turns fit in the remaining budget."""
    remaining = total_budget - pinned_tokens - summary_tokens - state_tokens

    # Reserve a small buffer for system prompt and formatting overhead
    remaining = int(remaining * 0.95)

    recent_turns = []
    token_count = 0

    # Walk backward from the most recent turn
    for turn in reversed(conversation):
        turn_tokens = count_tokens(turn["content"])
        if token_count + turn_tokens > remaining:
            break
        recent_turns.insert(0, turn)
        token_count += turn_tokens

    return recent_turns
```

### Overlap Handling

Pinned messages may also appear in the recency window. To avoid duplication:

```python
def deduplicate_recency_and_pins(
    recency_window: list[dict],
    pinned_messages: list[dict],
) -> list[dict]:
    """Remove pinned messages from the recency window to save tokens."""
    pinned_indices = {p["turn_index"] for p in pinned_messages}
    return [
        turn for turn in recency_window
        if turn.get("turn_index") not in pinned_indices
    ]
```

However, there's a tradeoff: keeping pinned messages in their natural position within the recency window preserves conversational flow. A practical compromise is to deduplicate only if token pressure is high (remaining budget < 80% utilized).

### Token Budget

The recency window gets **30-45%** of the total compressed context budget — the largest share, because recent context is the most actionable.

---

## Assembly and Orchestration

### Putting It All Together

```python
def compress_context(
    conversation: list[dict],
    previous_summary: str,
    previous_state: ConversationState,
    model_client,
    total_token_budget: int,
    last_compression_index: int,
) -> dict:
    """
    Run the full HSC pipeline and return the compressed context.
    """
    # Separate new turns from already-summarized turns
    new_turns = conversation[last_compression_index:]

    # Layer 1: Verbatim Pinning (mechanical, no model needed)
    pinned = extract_pinned_messages(conversation)
    pinned_tokens = sum(count_tokens(p["content"]) for p in pinned)

    # Layer 3: Structured State (model-assisted, schema-enforced)
    updated_state = update_structured_state(
        previous_state, new_turns, model_client
    )
    state_json = json.dumps(vars(updated_state), indent=2)
    state_tokens = count_tokens(state_json)

    # Layer 2: Running Summary (model-generated, regenerative)
    summary_budget = int(total_token_budget * 0.20)
    running_summary = generate_running_summary(
        previous_summary, new_turns, vars(updated_state),
        model_client, max_tokens=summary_budget,
    )
    summary_tokens = count_tokens(running_summary)

    # Layer 4: Recency Window (mechanical, fills remaining budget)
    recency_window = calculate_recency_window(
        total_token_budget, pinned_tokens, summary_tokens,
        state_tokens, conversation,
    )

    # Deduplicate if needed
    recency_window = deduplicate_recency_and_pins(recency_window, pinned)

    # Assemble the compressed context
    return {
        "pinned_messages": pinned,
        "running_summary": running_summary,
        "structured_state": vars(updated_state),
        "recency_window": recency_window,
        "metadata": {
            "total_turns_in_conversation": len(conversation),
            "compression_ratio": total_token_budget / sum(
                count_tokens(t["content"]) for t in conversation
            ),
            "last_compression_index": len(conversation),
        },
    }
```

### Injecting Compressed Context into a Prompt

When feeding the compressed context back into the model for the next turn, assemble it in a specific order that maximizes the model's attention:

```python
def build_prompt_from_compressed(compressed: dict) -> str:
    """Assemble compressed context into a prompt-ready string."""
    sections = []

    # Structured state first — dense, queryable, sets the frame
    sections.append("## Conversation State")
    sections.append(json.dumps(compressed["structured_state"], indent=2))

    # Narrative summary — fills in the arc and reasoning
    sections.append("## Conversation Summary")
    sections.append(compressed["running_summary"])

    # Pinned messages — exact-fidelity anchors
    sections.append("## Pinned Messages (verbatim)")
    for pin in compressed["pinned_messages"]:
        sections.append(
            f"[Turn {pin['turn_index']}][{pin['role']}]: {pin['content']}"
        )

    # Recency window — the live conversation thread
    sections.append("## Recent Conversation")
    for turn in compressed["recency_window"]:
        sections.append(f"[{turn['role']}]: {turn['content']}")

    return "\n\n".join(sections)
```

---

## Token Budget Summary

| Layer | Budget Share | Maintained By | Preserves |
|---|---|---|---|
| Verbatim Pinning | 20-30% | Mechanical rules | Exact code, URLs, numbers, instructions |
| Running Summary | 15-25% | Model (regenerative) | Intent, arc, reasoning, trajectory |
| Structured State | 10-15% | Model + schema enforcement | Facts, decisions, constraints, entities |
| Recency Window | 30-45% | Mechanical (fill remaining) | Active conversation thread |

---

## When to Trigger Compression

Compression should not run on every turn. Use these heuristics:

```python
def should_compress(
    conversation: list[dict],
    last_compression_index: int,
    context_window_limit: int,
) -> bool:
    """Decide whether to run the compression pipeline."""
    total_tokens = sum(count_tokens(t["content"]) for t in conversation)
    turns_since_compression = len(conversation) - last_compression_index

    # Compress if approaching context window limit
    if total_tokens > context_window_limit * 0.70:
        return True

    # Compress if enough new turns have accumulated
    if turns_since_compression >= 10:
        return True

    # Compress if a single recent turn is very large (e.g. large code paste)
    if any(
        count_tokens(t["content"]) > context_window_limit * 0.10
        for t in conversation[last_compression_index:]
    ):
        return True

    return False
```

---

## Failure Modes and Mitigations

| Failure Mode | Cause | Mitigation |
|---|---|---|
| Lossy drift | Summary subtly reframes intent over many cycles | Regenerative summarization (not recursive); periodic full-recompression from pinned messages + state |
| Hallucinated state | Model infers a constraint or decision that was never stated | Schema validation; only update fields with direct textual evidence |
| Over-pinning | Too many messages match pin patterns, starving other layers | Cap pin count; rank by signal density; drop older pins when superseded |
| Stale state | A decision was revoked but the state object still holds it | Model prompt explicitly instructs to check for contradictions with new turns |
| Context fragmentation | Pinned messages from distant turns lack surrounding context | Include 1 turn of context before/after each pin when budget allows |

---

## Future Extensions

- **Embedding-based pin selection:** Replace regex pattern matching with lightweight embedding similarity to detect semantically important turns rather than just syntactically distinctive ones.
- **User-controlled pinning UI:** Let users explicitly mark messages as "pin this" or "forget this" to give direct control over what survives compression.
- **Multi-conversation state merging:** When a user references a previous conversation, merge its structured state into the current one rather than re-reading the entire history.
- **Compression quality scoring:** After each compression, run a quick self-evaluation prompt asking the model whether any critical information appears to be missing, and trigger a re-compression if the score is low.
