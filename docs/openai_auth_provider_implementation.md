# OpenAI OAuth Provider Implementation

## Status

This document now reflects both the implemented foundation and the remaining work.

### Implemented

- dedicated provider type: `openai_codex_oauth`
- protected auth-record storage separate from plain provider config
- bundled OpenAI OAuth model manifest
- provider-type-aware Provider Manager flow
- reasoning-effort and verbosity selection for supported catalog models
- auth import through the official Codex CLI login flow
- request execution through the official Codex Responses bridge path
- provider contract coverage for catalog, auth import, text transport, and tool-call transport

### Still To Do

- app-native OAuth/browser callback flow if we want to remove the Codex CLI dependency
- app-native device-code polling flow if we want to own that UX directly
- richer live account diagnostics and clearer usage-limit reporting
- optional future catalog refresh from a stable documented endpoint, if OpenAI exposes one for this mode
- broader automated coverage outside the provider contract harness

## Purpose

This document describes how the OpenAI OAuth-backed provider is structured in the app and what remains to be finished around it.

This is not a claim that every endpoint used by third-party tools is part of the public self-serve API surface. Where behavior is inferred from OpenCode, Codex-related tooling, or the installed Codex CLI, it is labeled as such.

## Why This Needs Its Own Provider Type

The provider layer is broader now than it was when this work started, but OAuth-backed ChatGPT/Codex access still needs its own dedicated provider family.

The app now already supports multiple shapes such as:

- `openai_compatible`
- `agent_https`
- `openai_codex_oauth`
- `binding_provider`
- `lmstudio_local` readiness paths

Current schema and persistence:

- `src/types.h`
- `src/storage.cpp`
- `src/provider_manager.cpp`
- `src/openai_client.cpp`

The current `ProviderConfig` shape is:

```cpp
struct ProviderConfig {
    std::string id;
    std::string name;
    std::string provider_type = "openai_compatible";
    std::string base_url;
    std::string api_key;
    std::string tls_certificate_fingerprint;
    std::string auth_mode;
    std::string oauth_credential_id;
    std::string oauth_account_label;
    bool oauth_authenticated = false;
    bool oauth_store_remote_history = false;
    std::string model_catalog_mode = "manual";
    int max_active_requests = 0;
    int max_queue_size = 0;
    std::vector<ModelConfig> models;
};
```

That shape is not sufficient for OAuth-backed access because:

- there may be no user-entered API key
- access tokens expire and must be refreshed
- credentials should not live in plain `providers.json`
- model discovery is different from a normal API-key provider
- the Provider Manager UI must expose `Sign In`, `Sign Out`, and auth status instead of just URL + secret

## What OpenCode Appears To Be Doing

Based on the OpenCode docs and the OpenHax Codex plugin:

- OpenCode stores provider credentials in a local auth file, not inside the provider definition:
  - `~/.local/share/opencode/auth.json`
- The OpenHax Codex plugin says it uses OpenAI OAuth with the same flow as the official Codex CLI.
- The plugin references the OpenAI OAuth authorization server on `chatgpt.com/oauth`.
- The device-code helper references ChatGPT Codex security settings and a device auth flow.
- OpenCode/OpenHax ship a curated set of model presets rather than relying on user-entered freeform model IDs by default.

Practical implication:

- this should be implemented in our app as a dedicated provider type with dedicated auth storage and a curated model catalog
- it should not be jammed into the existing API-key flow

## Recommended Provider Type

Add a new provider type:

```text
openai_codex_oauth
```

This name is intentionally explicit:

- `openai`: provider family
- `codex`: this is intended for the ChatGPT subscription / Codex-style path
- `oauth`: the auth mechanism is not an API key

## Recommended Schema Changes

### 1. Extend `ProviderConfig`

Add metadata needed for UI and routing, but do not store access tokens here.

Suggested additions:

```cpp
struct ProviderConfig {
    std::string id;
    std::string name;
    std::string provider_type = "openai_compatible";
    std::string base_url;
    std::string api_key;
    std::string tls_certificate_fingerprint;
    int max_active_requests = 0;
    int max_queue_size = 0;
    std::vector<ModelConfig> models;

    // New for OAuth-backed OpenAI/Codex provider
    std::string auth_mode;                 // "browser_oauth" | "device_code"
    std::string oauth_credential_id;       // stable key into secure auth store
    std::string oauth_account_label;       // email or display name for UI
    bool oauth_authenticated = false;      // cached status for UI only
    bool oauth_store_remote_history = false; // maps to request "store"
};
```

Notes:

- `oauth_authenticated` is cached UI metadata only. The source of truth is whether a valid credential exists in the secure auth store.
- `oauth_store_remote_history` should default to `false`. This matches the OpenHax examples and also explains why users should not expect these chats to show up as normal ChatGPT history.

### 2. Add a Secure Auth Store

Do not store OAuth tokens inside `providers.json`.

Add a separate auth record structure:

```cpp
struct ProviderAuthRecord {
    std::string credential_id;
    std::string provider_id;
    std::string auth_mode;
    std::string api_key;
    std::string id_token;
    std::string access_token;
    std::string refresh_token;
    std::string token_type;
    std::string account_id;
    std::string account_email;
    std::string account_display_name;
    std::string scope;
    std::string expires_at;
    std::string last_refresh;
};
```

Recommended storage on Windows:

- first choice: DPAPI-protected local file
- acceptable alternative: Windows Credential Manager

Recommended path if using DPAPI file storage:

```text
.config/provider_auth.json
```

but with token fields encrypted at rest before serialization.

`providers.json` should only contain the metadata needed to reconnect the provider to its auth record.

## Authentication Modes

## Current Implemented Authentication Approach

The current implementation does not yet own a raw browser-callback OAuth exchange inside the app.

Instead it uses the official Codex CLI as the authentication bridge:

1. launch `codex login` or `codex login --device-auth` in an isolated provider-specific `CODEX_HOME`
2. import the resulting auth record into the app's protected provider auth store
3. rebuild the bridge cache when needed for future requests
4. execute model requests through the Codex `responses` bridge path

This has two practical advantages for the current phase:

- it keeps the provider aligned with the official Codex login flow
- it avoids pretending that a standard API-key `/chat/completions` path is equivalent to ChatGPT/Codex OAuth

It also has one practical limitation:

- the app currently depends on the installed Codex CLI for sign-in and request bridging

## Future Native Authentication Modes

Support two modes:

1. `browser_oauth`
2. `device_code`

### Browser OAuth

Use this when the app is running on a machine with a browser.

Flow:

1. User adds provider type `OpenAI OAuth (ChatGPT/Codex)`.
2. User clicks `Sign In`.
3. App launches a local callback listener.
4. App opens the OpenAI authorization URL in the browser.
5. User completes auth with their ChatGPT account.
6. App exchanges the code for tokens.
7. App stores the tokens in the secure auth store.
8. Provider UI updates to show authenticated account label.

### Device Code

Use this when:

- the app is remote/headless
- the browser is on another device
- the browser callback path is unreliable

Flow:

1. User chooses `Device Code`.
2. App requests a device code.
3. App shows:
   - verification URL
   - one-time code
4. User completes the login in any browser.
5. App polls until tokens are issued.
6. App stores tokens in the secure auth store.

Recommendation for the future native version:

- keep both browser OAuth and device code under the same provider type
- only replace the current Codex-CLI bridge once the app-native version is equally reliable

## Model Discovery Strategy

Do not make model discovery depend solely on a live undocumented endpoint.

Instead use:

1. a bundled manifest as the primary source of available models
2. optional provider-side refresh if a stable endpoint is later confirmed

This is the safest interpretation of the OpenCode approach because OpenHax clearly ships curated model presets. It does not look like they depend on the user discovering models from an editable provider URL at runtime.

Important implementation rule:

- do not infer reasoning capability from whether a model name contains `codex`
- store reasoning support per model in the manifest or discovered metadata

This matters because non-Codex models may still support advanced reasoning tiers. For example, the official OpenAI model docs for GPT-5.4 state that `reasoning.effort` supports `none`, `low`, `medium`, `high`, and `xhigh`.

### Recommended Bundled Manifest

Add a bundled manifest file such as:

```text
data/provider_manifests/openai_codex_oauth_models.json
```

Suggested shape:

```json
{
  "provider_type": "openai_codex_oauth",
  "models": [
    {
      "id": "gpt-5.4",
      "display_name": "GPT 5.4",
      "context_window": 1050000,
      "max_output_tokens": 128000,
      "supports_streaming": true,
      "supports_tools": true,
      "supports_vision": true,
      "prefer_max_completion_tokens": false,
      "reasoning_efforts": ["none", "low", "medium", "high", "xhigh"],
      "text_verbosity_modes": ["low", "medium", "high"]
    },
    {
      "id": "gpt-5.3-codex",
      "display_name": "GPT 5.3 Codex",
      "context_window": 400000,
      "max_output_tokens": 128000,
      "supports_streaming": true,
      "supports_tools": true,
      "supports_vision": true,
      "prefer_max_completion_tokens": false,
      "reasoning_efforts": ["low", "medium", "high", "xhigh"],
      "text_verbosity_modes": ["low", "medium", "high"]
    },
    {
      "id": "gpt-5.2-codex",
      "display_name": "GPT 5.2 Codex",
      "context_window": 400000,
      "max_output_tokens": 128000,
      "supports_streaming": true,
      "supports_tools": true,
      "supports_vision": true,
      "prefer_max_completion_tokens": false
    },
    {
      "id": "gpt-5-codex",
      "display_name": "GPT 5 Codex",
      "context_window": 400000,
      "max_output_tokens": 128000,
      "supports_streaming": true,
      "supports_tools": true,
      "supports_vision": true,
      "prefer_max_completion_tokens": false
    }
  ]
}
```

### Why a Manifest Is Better Than Freeform Entry

- Codex-family OAuth access is specialized
- models available to that auth path may differ from normal API-key expectations
- we can ship tested defaults with correct context/tool/vision flags
- the Provider Manager becomes simpler and safer

The manifest should also be treated as the source of truth for:

- supported reasoning efforts
- context window
- max output
- tool/vision capability

The app should not guess these from the model ID pattern.

### Optional Refresh Strategy

If OpenAI later exposes a stable model metadata endpoint for this auth mode, support:

- `Refresh Available Models`

Behavior:

1. query the provider-specific discovery endpoint
2. merge results with the bundled manifest
3. mark models as:
   - `Bundled`
   - `Discovered`
4. never delete bundled entries automatically

If no discovery endpoint is available or documented:

- keep the bundled manifest as the source of truth

## Provider Manager Changes

### Provider Type Selector

In `src/provider_manager.cpp`, extend the provider type combo to include:

```text
OpenAI Compatible
Agent HTTPS
OpenAI OAuth (ChatGPT/Codex)
```

### Provider Editor UI for OAuth Type

For `openai_codex_oauth`, the editor should show:

- Provider name
- Auth mode:
  - Browser OAuth
  - Device Code
- Auth status
- Account label
- `Sign In` button
- `Sign Out` button
- `Refresh Models` button
- Queue settings:
  - Max active requests
  - Max queue size
- Optional advanced toggle:
  - `Store remote conversation history`

Fields that should be hidden or read-only for this provider type:

- raw API key
- editable base URL
- TLS certificate fingerprint

Reason:

- this provider should manage endpoint details internally
- the user should authenticate an account, not manually type a secret

### Test Connection Behavior

Current behavior assumes:

- provider selected
- model selected
- request sent against `/chat/completions`

For the OAuth provider, `Test Connection` should:

1. verify a valid auth record exists
2. refresh the access token if needed
3. verify that at least one OAuth-compatible model is configured
4. send a tiny test request with:
   - `store = false`
   - low token count
   - a minimal prompt
5. return:
   - authenticated account label
   - selected model
   - provider response success/failure

If no auth record exists, the test dialog should say:

```text
This provider is not authenticated. Use Sign In first.
```

## Add Model and Edit Model Changes

This is the part that should change the most.

### Current Behavior

The current model editor is a generic freeform editor:

- provider dropdown
- model id text field
- display name text field
- context window text field
- capability checkboxes

This works for `openai_compatible` and `agent_https`, but it is the wrong UX for OAuth-backed Codex access.

### Recommended Add Model UX for OAuth Provider

If the selected provider is `openai_codex_oauth`, `Add Model` should open a provider-specific picker instead of the generic freeform dialog.

Recommended layout:

- `Available Models` list sourced from the bundled manifest and optional refresh results
- columns:
  - Display name
  - Model ID
  - Context window
  - Vision
  - Tools
  - Source (`Bundled` / `Discovered`)
- action buttons:
  - `Add Selected`
  - `Refresh Models`
  - `Cancel`

What should be auto-filled when the user adds one:

- `model.id`
- `display_name`
- `context_window`
- `supports_streaming`
- `supports_tools`
- `supports_vision`
- `prefer_max_completion_tokens`

No manual typing should be required in the common case.

### Recommended Edit Model UX for OAuth Provider

For OAuth-backed models, `Edit Model` should be restricted.

Editable fields:

- display name override
- enabled/disabled
- optional advanced overrides if we explicitly allow them later

Read-only fields:

- model ID
- context window from manifest
- core capability flags
- supported reasoning levels from manifest

Reason:

- these values should stay aligned with the curated catalog
- freeform edits increase the chance that the user creates an invalid model/provider pairing

### Advanced Override Mode

If we want flexibility for power users, add an `Advanced Override` checkbox that unlocks:

- context window override
- capability overrides

This should be off by default.

## Request Path Changes

The current implemented `openai_codex_oauth` request path does not use the normal OpenAI API-key transport.

Instead it:

1. looks up the protected provider auth record using `oauth_credential_id`
2. rebuilds the isolated bridge cache if needed
3. invokes the installed Codex CLI bridge
4. sends a Responses-style payload through `codex responses`
5. forces provider defaults required by that path, including `store = false`

This matters because the standard API path and the ChatGPT/Codex OAuth path are not the same thing operationally.

### Important Rule

Do not make the generic UI own the backend URL for this provider type.

Whether the transport remains the Codex CLI bridge or later becomes an app-native OAuth client, endpoint resolution should stay inside the provider adapter rather than becoming manual user input.

## Request Body Additions

For this provider type, the request builder already supports provider defaults like:

```json
{
  "store": false
}
```

Optional provider defaults we may continue expanding at provider/model level:

- `reasoningEffort`
- `reasoningSummary`
- `textVerbosity`
- `include`

These are present in the OpenHax examples and should be treated as provider/model options, not user message content.

Important:

- validation of `reasoningEffort` must be based on the selected model's allowed values
- do not assume `xhigh` is limited to `*-codex*` models
- the selected model manifest entry should drive which options appear in the UI

## Persistence Changes

### `providers.json`

Continue storing provider definitions and chosen models here, but do not store raw OAuth tokens.

### New auth store

Use the protected auth store keyed by `oauth_credential_id`.

### Save/load behavior

Current implementation behavior:

- provider definitions and model selections live in normal provider storage
- sensitive auth material lives in the protected provider auth store
- provider UI state such as account label and authenticated flag is rehydrated from that auth store on load

## Current Remaining Work

The provider is now working, but these items are still open:

1. better UI for usage-limit and scope/account failures
2. clearer explanation of non-stored remote history for this transport
3. optional native OAuth/browser/device flow if we want to stop depending on the Codex CLI
4. broader automated coverage beyond the current provider contract harness

- loading providers should recompute `oauth_authenticated`
- loading Provider Manager should display the account label if the auth store has one
- `Sign Out` should:
  - clear the auth record
  - set `oauth_authenticated = false`
  - clear `oauth_account_label`

## Suggested Implementation Order

1. Add schema support in:
   - `src/types.h`
   - `src/storage.cpp`
2. Add secure auth store abstraction
3. Add `openai_codex_oauth` provider type to Provider Manager UI
4. Add browser OAuth flow
5. Add bundled model manifest
6. Replace freeform Add Model flow for OAuth providers with a catalog picker
7. Add OAuth provider request branch in `src/openai_client.cpp`
8. Add device-code auth flow
9. Add optional model refresh action if a stable discovery path is confirmed

## Recommended First Release Scope

For phase 1, keep it tight:

- one new provider type: `openai_codex_oauth`
- browser OAuth only
- bundled manifest only
- `store = false` default
- restricted Add Model picker
- restricted Edit Model behavior

Do not do in phase 1:

- arbitrary custom base URLs for OAuth provider
- multi-account rotation
- commercial/shared-account behavior
- undocumented runtime model scraping

## Expected User Experience

1. User opens Provider Manager.
2. User clicks `Add Provider`.
3. User chooses `OpenAI OAuth (ChatGPT/Codex)`.
4. User enters a friendly provider name.
5. User clicks `Sign In`.
6. Authentication flow completes.
7. UI shows:
   - authenticated account label
   - auth status: connected
8. User clicks `Add Model`.
9. A curated list of Codex-capable models appears.
10. User selects one or more.
11. Provider is ready for chats.

## Notes About Chat History Visibility

Users may expect these chats to appear in normal ChatGPT history.

We should document the opposite expectation:

- this provider is intended for Codex-style backend access
- by default, requests should use `store = false`
- users should not assume chats will appear in the standard ChatGPT web history

This is based on the OpenHax configuration examples and the observed behavior the user reported.

## Sources

- OpenCode auth storage docs:
  - https://open-code.ai/en/docs/cli
- OpenHax Codex plugin README:
  - https://github.com/open-hax/codex
- OpenAI OAuth / Codex references cited by that plugin:
  - https://chatgpt.com/oauth
- OpenCode device-code helper:
  - https://github.com/tumf/opencode-openai-device-auth
- OpenAI developer docs model references:
  - https://developers.openai.com/api/docs/models/gpt-5.4
  - https://developers.openai.com/api/docs/models/gpt-5.3-codex
  - https://developers.openai.com/api/docs/models/gpt-5.2-codex
  - https://developers.openai.com/api/docs/models/gpt-5-codex
