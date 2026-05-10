# Ollama Session Commands: `/clear` and `/set history`

This short reference explains how the `/clear` and `/set history` commands behave inside an interactive `ollama run` session.

---

## 🧠 What “history” means

During `ollama run`, Ollama constructs a prompt from prior turns:

```
[system prompt]
[user message 1]
[assistant reply 1]
[user message 2]
[assistant reply 2]
...
[current user message]
```

This accumulated text is the **history (context)** sent to the model each turn.

---

## 🔄 `/clear`

```text
/clear
```

**What it does**
- Resets the current session’s conversation
- Removes all prior messages from context
- Keeps the same model loaded

**When to use**
- Start a fresh conversation without restarting the model
- Fix confusion from earlier prompts

**What it does NOT do**
- Does not change model parameters
- Does not persist or delete anything on disk

---

## ⚙️ `/set history`

```text
/set history <N>
```

**What it does**
- Limits how many **previous turns** (user + assistant pairs) are included in future prompts
- Acts as a cap on conversation memory within the session

**Examples**

Limit to last 2 turns:
```text
/set history 2
```

Disable history (stateless mode):
```text
/set history 0
```

**Behavior details**
- `N` counts **turns** (a user message + assistant reply)
- Only the most recent `N` turns are included alongside the current message
- Older turns are ignored even if there is still room in the context window

---

## 🧩 Interaction with context window (`num_ctx`)

There are two layers of control:

1. **History limit (`/set history`)**
   - Manual cap on number of past turns

2. **Context window (`num_ctx`)**
   - Hard token limit for the model (e.g., 4k, 8k, 32k tokens)
   - If exceeded, oldest tokens are truncated automatically (sliding window)

**Implication**
- Even with large history, old content may be dropped when the context window fills
- Even with large context, `/set history` can intentionally keep prompts small and focused

---

## ⚠️ What these commands do NOT do

- Do not save chat history to disk
- Do not persist across sessions
- Do not make the model “learn” from prior chats

---

## ✅ Practical tips

- Use `/clear` to quickly reset without restarting
- Use `/set history 3–6` for coherent but efficient conversations
- Use `/set history 0` for API-like, stateless prompts

---

## 📌 Summary

| Command | Effect |
|--------|--------|
| `/clear` | Wipes all current session context |
| `/set history N` | Keeps only the last N turns in context |
| `/set history 0` | Stateless mode (no memory) |

---

This behavior helps you balance coherence, performance, and control over what the model sees each turn.


---

## 🧾 `/set format json`

```text
/set format json
```

**What it does**
- Instructs Ollama to return responses in **valid JSON format** instead of free-form text
- Useful for scripting, parsing, and tool integration

**Example**

Without JSON mode:
```text
What is 2 + 2?
```

Output:
```text
The answer is 4.
```

With JSON mode:
```text
/set format json
What is 2 + 2?
```

Output:
```json
{
  "answer": 4
}
```

**Important notes**
- You should **explicitly specify the desired JSON structure** in your prompt
  ```text
  Return a JSON object with a field called "answer".
  ```
- Output is not strictly schema-validated; it is **guided but not guaranteed**
- Rare formatting issues can still occur

---

## 🔄 Disable JSON mode

```text
/set format text
```

Returns responses to normal free-form text.

---

## 🧩 When to use JSON mode

- When piping output into tools (e.g., `jq`)
- When building scripts or automation
- When you need structured, machine-readable responses

---

## 📌 Summary (Updated)

| Command | Effect |
|--------|--------|
| `/clear` | Wipes all current session context |
| `/set history N` | Keeps only the last N turns in context |
| `/set history 0` | Stateless mode (no memory) |
| `/set format json` | Forces structured JSON output |
| `/set format text` | Returns to normal text output |

---

This extended reference now covers context control and structured output within an Ollama session.

---

## 🧩 Providing a JSON Schema (Best Practice)

While `/set format json` encourages valid JSON output, you still need to **define the structure (schema)** in your prompt to get reliable results.

### 🧠 Why this matters
- The model does **not inherently know your desired fields**
- Without guidance, it may:
  - Invent keys
  - Omit fields
  - Use inconsistent formats

---

## ✅ Simple schema via instructions

You can describe the schema in plain text:

```text
Return a JSON object with the following fields:
- name (string)
- age (number)
- is_student (boolean)
```

---

## ✅ Stronger schema via example

Providing an example improves reliability:

```text
Return JSON in this format:
{
  "name": "string",
  "age": 0,
  "is_student": true
}
```

---

## ✅ Strict schema-style prompt (most reliable)

```text
Return ONLY valid JSON matching this schema:
{
  "type": "object",
  "properties": {
    "name": { "type": "string" },
    "age": { "type": "number" }
  },
  "required": ["name", "age"]
}
```

### Notes
- This mimics JSON Schema but is **not strictly enforced**
- The model tries to comply but is not guaranteed to validate perfectly

---

## ⚠️ Common pitfalls

- Not specifying field names → inconsistent output
- Mixing text + JSON → breaks parsers
- Forgetting "ONLY JSON" → model may add explanations

---

## 💡 Practical tip

For best results, combine all three:

```text
/set format json

Return ONLY valid JSON.

Schema:
{
  "name": "string",
  "age": "number"
}
```

---

## 📌 Key takeaway

`/set format json` controls **format**, but your prompt defines the **structure**.

You must supply a schema (explicitly or implicitly) to get consistent, machine-usable output.


