# Providers and Models

## Open the Manager

Click `Providers` in the desktop toolbar. The manager has a provider list, a model list for the selected provider, and actions:

- `Add Provider`, `Edit Provider`, `Remove Provider`
- `Add Model`, `Edit Model`, `Remove Model`
- `Test Connection`

A project cannot send useful model requests until it has a configured model selected in `Project Settings`.

## Add a Provider

1. Click `Add Provider`.
2. Select a provider type.
3. Give it a unique, understandable `Provider name`.
4. Fill in the fields required by that type.
5. Save the provider.
6. Add or refresh its models.
7. Select a model and click `Test Connection`.

### Provider types

| Provider type in UI | Intended use | Key configuration |
| --- | --- | --- |
| `OpenAI-compatible HTTPS/API` | API endpoint compatible with OpenAI chat operations | `Base URL`, `API key`, request/queue limits; models are managed manually. |
| `Agent HTTPS Remote Worker` | Another Agent Desktop worker exposing selected provider models over HTTPS | Load its remote JSON or enter its HTTPS URL and shared secret; certificate fingerprint protects the connection. |
| `OpenAI OAuth (ChatGPT/Codex)` | Sign in through the official Codex login flow with a ChatGPT/Codex account | Use `Sign In...`; model catalog comes from the bundled OAuth manifest. |
| `LM Studio Local` | Local LM Studio server | Local endpoint; `Refresh Models` queries `/models`. |
| `Ollama Local` | App/local Ollama model runtime | Local Ollama URL and port; per-model load/GPU/context options. |
| `Ollama Cloud` | Ollama-compatible hosted service | URL/API key and models. |
| `Binding Provider` | Virtual routing/failover provider | Create binding models and assign concrete provider/model targets. |

### Common provider fields

| Field | Meaning |
| --- | --- |
| `Provider name` | Display name used in model selection lists. |
| `Base URL / Agent HTTPS URL` | Endpoint for direct or remote-worker providers. The label changes for OAuth/Ollama providers. |
| `API key / shared secret` | Credential for API providers or the remote worker bearer secret. |
| `Certificate fingerprint` | Read-only trust value for an Agent HTTPS worker loaded from configuration. |
| `Max active requests (0 = no limit)` | App-level provider concurrency limit. |
| `Max queue size (0 = no limit)` | App-level pending request limit. |
| `Load Remote JSON` | Loads an exported Agent HTTPS remote-worker configuration. |

## ChatGPT/Codex Browser Sign-In

Use `OpenAI OAuth (ChatGPT/Codex)` when a user should authenticate through the Codex/ChatGPT browser flow rather than paste a platform API key.

1. Add or edit a provider and choose `OpenAI OAuth (ChatGPT/Codex)`.
2. Choose `Auth mode`: `Browser OAuth` or `Device Code`.
3. Click `Sign In...`.
4. Complete the Codex sign-in process in the browser or device-code page that opens.
5. Return to the provider dialog. `Auth status` and `Account label` show the imported authentication state.
6. Use `Refresh Models` if applicable, then save.
7. In `Project Settings`, select a tool-capable streaming model from this provider as `AI Model`.

Implementation behavior:

- The app launches `codex login` or `codex login --device-auth` using an app-owned isolated `CODEX_HOME`.
- It imports the resulting authentication cache into the protected provider authentication store.
- Model requests use the official Codex Responses bridge.
- ChatGPT/Codex sessions use non-stored requests; `Store remote conversation history` is disabled for this type.
- `Sign Out` removes the provider's imported authentication and isolated Codex auth cache.

If a request reports that no API key was supplied while this provider should be signed in, first reopen the provider and confirm its authentication status; a missing or invalid imported credential means it is not using the expected OAuth transport.

## Add or Edit a Model

Select a provider and choose `Add Model` or `Edit Model`. Available fields depend on the provider type.

| Model field | Meaning |
| --- | --- |
| `Model ID` | Endpoint/model identifier sent to the provider. |
| `Display Name` | Human-readable label in project/model selectors. |
| `Context window` | Known input context capacity; `0` or blank means unknown. |
| `Output tokens` | Maximum configured output tokens; blank/zero uses the app default of 8192. |
| `Streaming capable` | Required before a model can be made user-selectable for web chat. |
| `Tool capable` | Required for agent tools and for the web user-selectable model list. |
| `Vision capable` | Allows the model to appear as an image-ingestion vision provider. |
| `Embedding capable` | Records embedding capability for applicable workflows. |
| `Thinking capable` | Records reasoning/thinking support and enables related UI behavior. |
| `Reasoning effort` / `Verbosity` | Defaults shown where the provider exposes those controls. |
| `Max active` / `Max queue` | Optional model-specific queue limits; `0` uses provider defaults. |
| `Self-managed queue` | Indicates that request concurrency is managed at model level. |

### Ollama model controls

An Ollama model can also configure:

- `Load Info` and `Search`
- `Idle unload (sec)`
- `CPU threads (0=auto)`
- `Force CPU only`
- `GPU layers (0=auto)`
- `Ollama ctx len (0=auto)`
- `Verbose CLI stats`
- `Pull Model`

## Binding Provider

A `Binding Provider` presents one model while routing calls to existing concrete provider/model targets.

1. Add a `Binding Provider`.
2. Add a model under it.
3. Set its `Context window` and capability flags.
4. Choose `Routing mode`: `Top-down failover` or `Round robin`.
5. Use `Add Target` to add provider/model targets in the desired order.
6. Configure each target's priority and retry/cooldown values.
7. Save, test, and select the binding model in a project.

A binding model must have a context window and at least one valid target. Capability selections must be supported by its target models; its context window may not exceed the smallest known target context window.

## Remote Agent Provider

For a model served by another Agent instance:

1. Generate a worker JSON using `Remote Model Config` on the machine that will host the model.
2. On the client app, add an `Agent HTTPS Remote Worker` provider.
3. Click `Load Remote JSON`, or manually enter the worker URL and shared secret.
4. Add/refresh models and test the connection.

See [07 - Remote Models and Command Line](07-Remote-Models-and-Command-Line.md) for worker execution.

## Assign Providers to Projects

Providers and models are global definitions. A project chooses its runtime model in `Project Settings`:

- `AI Model` selects the default.
- `User Select Model` permits web users to switch among administrator-approved streaming, tool-capable models.
- The model selected at the top of `Project Settings` remains the default model.

