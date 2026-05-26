# 🧠 Model-Driven Visualization System

## Using Mermaid + Vega-Lite + Cytoscape.js + SVG Together

This document defines a **unified approach** for enabling a model to generate rich, visual explanations using multiple web-native rendering tools.

---

# 🎯 Goal

Allow a model to:

* Express ideas visually
* Choose the best visualization format
* Output structured, renderable specifications (not images)
* Be interpreted consistently by a web frontend

---

# 🧩 The Four-Tool System

| Tool             | Purpose                     | Best For                               |
| ---------------- | --------------------------- | -------------------------------------- |
| **Mermaid**      | Text-based diagrams         | Flows, processes, simple structures    |
| **Vega-Lite**    | Data visualization grammar  | Charts, metrics, analytics             |
| **Cytoscape.js** | Graph/network visualization | Relationships, dependencies            |
| **SVG (raw)**    | Freeform drawing            | Custom diagrams, annotations, overlays |

---

# 🧠 Model Responsibilities

The model should:

1. **Understand the intent**
2. **Select the correct visualization type**
3. **Output structured JSON or text**
4. **Avoid raw HTML/JS unless explicitly allowed**

---

# 🧭 Visualization Selection Rules (Model Logic)

```
IF data is tabular or numeric:
  → Use Vega-Lite

ELSE IF data is nodes + edges:
  → Use Cytoscape.js

ELSE IF diagram is simple flow/process:
  → Use Mermaid

ELSE:
  → Use SVG
```

---

# 📦 Unified Output Schema

The model should ALWAYS respond in this format:

```json
{
  "type": "mermaid | vega-lite | cytoscape | svg",
  "title": "Short description",
  "spec": {},
  "notes": "Optional explanation"
}
```

---

# 🔧 Tool-Specific Output Formats

## 1. Mermaid

```json
{
  "type": "mermaid",
  "title": "User Checkout Flow",
  "spec": "flowchart LR\n  User --> Cart --> Payment --> Confirmation"
}
```

---

## 2. Vega-Lite

```json
{
  "type": "vega-lite",
  "title": "Request Volume",
  "spec": {
    "$schema": "https://vega.github.io/schema/vega-lite/v5.json",
    "data": {
      "values": [
        {"day": "Mon", "count": 10},
        {"day": "Tue", "count": 20}
      ]
    },
    "mark": "bar",
    "encoding": {
      "x": {"field": "day", "type": "nominal"},
      "y": {"field": "count", "type": "quantitative"}
    }
  }
}
```

---

## 3. Cytoscape.js

```json
{
  "type": "cytoscape",
  "title": "Service Dependencies",
  "spec": {
    "elements": [
      { "data": { "id": "frontend" } },
      { "data": { "id": "backend" } },
      { "data": { "source": "frontend", "target": "backend" } }
    ]
  }
}
```

---

## 4. SVG

```json
{
  "type": "svg",
  "title": "Annotated Flow",
  "spec": "<svg width='300' height='200'>\n  <rect x='10' y='10' width='100' height='50' fill='lightblue'/>\n  <text x='20' y='40'>Start</text>\n</svg>"
}
```

---

# 🌐 Website Responsibilities

The frontend must:

## 1. Parse Model Output

* Read JSON response
* Validate `type` and `spec`

---

## 2. Route to Renderer

```javascript
switch (type) {
  case "mermaid":
    renderMermaid(spec)
    break
  case "vega-lite":
    renderVega(spec)
    break
  case "cytoscape":
    renderCytoscape(spec)
    break
  case "svg":
    renderSVG(spec)
    break
}
```

---

## 3. Rendering Implementations

### Mermaid

```html
<div class="mermaid">...</div>
```

### Vega-Lite

```javascript
vegaEmbed('#chart', spec)
```

### Cytoscape.js

```javascript
cytoscape({
  container: document.getElementById('graph'),
  elements: spec.elements
})
```

### SVG

```javascript
container.innerHTML = spec
```

---

# ⚙️ Recommended Enhancements

## Validation Layer

* Ensure valid Mermaid syntax
* Validate Vega schema
* Check graph size limits

---

## Auto-Simplification

If data is too large:

* Reduce nodes
* Aggregate values
* Add `"warnings"` field

---

## Styling Layer

Apply consistent:

* Colors
* Fonts
* Spacing

---

# 🧠 Prompt Instructions for the Model

Use this as a system or context prompt:

---

### MODEL INSTRUCTIONS

You are a visualization engine.

Your job is to:

1. Analyze the input
2. Choose the best visualization type:

   * mermaid
   * vega-lite
   * cytoscape
   * svg
3. Output ONLY valid JSON using the required schema

---

### RULES

* Do NOT output HTML
* Do NOT output images
* Do NOT mix formats
* Keep diagrams readable (limit complexity)
* Prefer clarity over completeness

---

### FORMAT

```json
{
  "type": "...",
  "title": "...",
  "spec": {},
  "notes": "..."
}
```

---

### PRIORITIES

1. Clarity
2. Correct structure
3. Simplicity
4. Visual usefulness

---

# 🚀 Architecture Summary

```
User Input
   ↓
Model (chooses format + outputs spec)
   ↓
Frontend Router
   ↓
Renderer (Mermaid / Vega / Cytoscape / SVG)
   ↓
Visual Output
```

---

# 💡 Key Insight

This system works because:

* The **model describes** visuals
* The **browser renders** visuals
* The **schema enforces consistency**

---

# ✅ Result

You now have:

* A multi-format visualization system
* A model protocol
* A frontend rendering strategy

---

If extended properly, this becomes a **universal visual language layer for AI systems**.
