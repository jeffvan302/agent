# Binding Provider Requirements

Date: 2026-04-23

## Status

Phase 1 of this feature is now implemented.

### Implemented

- provider type: `binding_provider`
- routed binding models stored with binding metadata
- routing modes:
  - `top_down_failover`
  - `round_robin`
- persisted runtime cooldown/round-robin state in `.data/provider_binding_runtime.json`
- Provider Manager UI for:
  - binding providers
  - binding models
  - target add/edit/remove/reorder
- provider contract coverage for:
  - primary busy to secondary success
  - round-robin rotation
  - limit cooldown behavior

### Still To Do

- richer UI diagnostics for live target health/cooldown state
- more visibility in chat/debug output about which concrete target was chosen
- broader automated coverage outside the provider contract suite
- continued validation as more concrete provider families, especially LM Studio, mature underneath it

## Purpose

This document defines the internal routed-provider family now implemented in the app:

```text
binding_provider
```

The purpose of a binding provider is to let one logical model route requests across multiple real provider/model targets.

This is intended for:

- fail-safe model fallback when a preferred provider is busy, rate-limited, or unavailable
- round-robin load sharing across equivalent models
- controlled routing across providers that expose different operational limits

This is not meant to replace normal providers. It is an advanced routing layer built on top of them.

## Why This Should Be A Provider Type

The current app stores model selection as a normal:

- `preferred_provider_id`
- `preferred_model_id`

pair in project settings and chat settings.

Relevant current code:

- [types.h](C:/Users/TheunisvanNiekerk/Code/agent/src/types.h)
- [openai_client.h](C:/Users/TheunisvanNiekerk/Code/agent/src/openai_client.h)
- [openai_client.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/openai_client.cpp)
- [provider_manager.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/provider_manager.cpp)
- [project_settings_dialog.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/project_settings_dialog.cpp)

That means the least disruptive architecture is:

- keep project/chat model selection exactly as it is
- add a new provider type whose models are virtual routed models
- resolve the real target only when a request is about to be sent

This avoids rewriting the rest of the app to understand “many providers for one model” everywhere.

## Core Concept

A binding provider contains one or more **binding models**.

A binding model contains one or more **binding targets**.

Each binding target points to a normal provider/model pair already configured in the app.

Example:

```text
Binding Provider: "OpenAI Failover"
  Binding Model: "GPT-5.4 Routed"
    Target 1: OpenAI OAuth Work / gpt-5.4
    Target 2: OpenAI OAuth Personal / gpt-5.4
    Target 3: LM Studio Local / gpt-oss-120b
```

When the project selects:

```text
Provider = OpenAI Failover
Model    = GPT-5.4 Routed
```

the app routes that request to one real target according to the binding model’s routing policy.

## Terms

### Concrete provider

A normal provider that can actually receive a request:

- `openai_compatible`
- `openai_codex_oauth`
- `lmstudio_local`
- `agent_https`

### Binding provider

A virtual provider that cannot send requests by itself. It must resolve to a concrete provider/model target first.

### Binding model

A virtual model exposed by a binding provider. It represents a routed model entry that the rest of the app can select normally.

### Binding target

One concrete `provider_id + model_id` candidate inside a binding model.

## Phase 1 Design Rules

To keep the first implementation stable:

1. no nested binding providers
2. no mid-stream failover
3. one routed request chooses one concrete target and stays there for the duration of that request
4. binding providers do not add a second queue layer on top of concrete providers
5. existing provider queues remain the source of truth for concurrency control

These constraints are important. They keep the feature understandable and avoid surprising behavior.

## New Provider Type

Add:

```text
binding_provider
```

Suggested display name in Provider Manager:

```text
Binding Provider
```

## Recommended Schema

## 1. Routing mode

```cpp
enum class BindingRoutingMode {
    TopDownFailover,
    RoundRobin,
};
```

### TopDownFailover

Try targets in priority order from top to bottom.

If a target is busy, out of limit, or unavailable, mark it accordingly and try the next eligible target.

### RoundRobin

Distribute requests across eligible targets in rotation.

Skip targets that are disabled or currently in cooldown.

## 2. Binding target config

```cpp
struct BindingTargetConfig {
    std::string provider_id;
    std::string model_id;
    bool enabled = true;
    int priority = 100;

    int busy_retry_interval_seconds = 15;
    int busy_retry_budget_seconds = 90;
    int busy_cooldown_seconds = 300;
    int limit_cooldown_seconds = 900;
    int error_cooldown_seconds = 300;
};
```

### Meaning

- `busy_retry_interval_seconds`
  - how long to wait between retries on the same target while it reports busy/capacity/rate-limit style errors
- `busy_retry_budget_seconds`
  - how long to keep retrying that target before we give up and fail over to another target
- `busy_cooldown_seconds`
  - how long to quarantine the target after a busy budget is exhausted
- `limit_cooldown_seconds`
  - how long to quarantine the target after quota/credits/usage-limit exhaustion
- `error_cooldown_seconds`
  - how long to quarantine the target after transport or non-busy HTTP failures

## 3. Binding target runtime state

This belongs in runtime data, not in the provider config file.

```cpp
struct BindingTargetRuntimeState {
    std::string provider_id;
    std::string model_id;

    std::string last_status;       // healthy | busy | limited | error | disabled
    std::string last_used_at;
    std::string last_success_at;
    std::string last_busy_at;
    std::string last_limit_at;
    std::string last_error_at;
    std::string cooldown_until;

    int consecutive_busy_count = 0;
    int consecutive_limit_count = 0;
    int consecutive_failure_count = 0;
};
```

## 4. Binding model config

This should be explicit instead of trying to overload plain `ModelConfig` in memory with half-hidden routing fields.

Suggested structure:

```cpp
struct BindingModelConfig {
    std::string id;
    std::string display_name;

    int context_window = 0;            // mandatory
    int max_output_tokens = 0;
    bool supports_streaming = false;
    bool supports_tools = false;
    bool supports_vision = false;

    std::vector<std::string> reasoning_efforts;
    std::vector<std::string> text_verbosity_modes;
    std::string default_reasoning_effort;
    std::string default_text_verbosity;

    BindingRoutingMode routing_mode = BindingRoutingMode::TopDownFailover;
    std::vector<BindingTargetConfig> targets;
};
```

The Provider Manager can adapt these into normal `ModelConfig` objects for the rest of the app, but the config should retain the richer routed-model metadata.

## 5. Binding provider config

```cpp
struct BindingProviderConfig {
    std::string id;
    std::string name;
    std::string provider_type = "binding_provider";
    std::vector<BindingModelConfig> binding_models;
};
```

For compatibility with the current provider list, phase 1 may persist routed models into `ProviderConfig.models`, but the long-term cleaner version is to store `binding_models` explicitly and expose adapter views at runtime.

## Capability Rules

## 1. Context window is mandatory

Binding models must require an explicit context window.

Reason:

- routed models must present a stable context window to the rest of the app
- this cannot be inferred safely when targets differ

Validation rule:

- `binding_model.context_window` must be `> 0`
- it must be `<= min(target.context_window for all enabled targets with known context windows)`

If any enabled target has `0` or unknown context, the UI should warn clearly.

## 2. Capability flags are explicit but validated

Binding models expose:

- streaming
- tools
- vision

These should be editable in the binding-model UI, but validation must enforce:

- if `supports_streaming == true`, all enabled targets must support streaming
- if `supports_tools == true`, all enabled targets must support tools
- if `supports_vision == true`, all enabled targets must support vision

In other words, the binding model must not advertise a feature that some of its enabled targets cannot fulfill.

## 3. Reasoning and verbosity capabilities

The binding model may expose:

- supported reasoning levels
- supported verbosity levels
- default reasoning
- default verbosity

Phase 1 rule:

- the binding model’s supported values must be the intersection of its enabled targets
- the selected defaults must be within that intersection

If the intersection is empty, leave those fields blank.

## Routing Behavior

## 1. Request-level stickiness

One request chooses one target.

After the request starts successfully, the app stays on that target until the request completes or fails.

Do not switch targets mid-stream.

That rule applies to:

- normal streaming responses
- tool-aware completions
- simple completions

## 2. TopDownFailover algorithm

1. Sort enabled targets by priority
2. Remove targets whose cooldown is still active
3. Try the first eligible target
4. If it succeeds, record success and stop
5. If it returns busy/limit/unavailable according to the retry rules:
   - retry that target within its configured budget
   - if still not usable, mark cooldown and move to the next target
6. If all targets fail, return the last meaningful error

## 3. RoundRobin algorithm

1. Maintain a persistent “next target” cursor per binding model
2. Start from that cursor
3. Skip disabled/cooling targets
4. Try the next eligible target
5. On success, advance the cursor to the next target for future requests
6. On busy/error, apply cooldown and continue searching

## 4. Busy vs limit vs error classification

The binding router should reuse the same classification families already used in [openai_client.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/openai_client.cpp):

- busy / overloaded / rate limited / queue full
- quota / credits / insufficient quota / usage limit reached
- transport or generic error

Recommended mapping:

- busy-like status
  - respect `busy_retry_interval_seconds`
  - retry up to `busy_retry_budget_seconds`
  - then set `busy_cooldown_seconds`
- limit-like status
  - immediately set `limit_cooldown_seconds`
- other error
  - set `error_cooldown_seconds`

## Queueing Rules

Do not create a second independent queue manager for binding providers in phase 1.

Instead:

1. binding router chooses a concrete target
2. concrete target request goes through the existing concrete provider queue

Relevant existing queue logic:

- [openai_client.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/openai_client.cpp)

This avoids:

- double-queueing
- misleading queue position reporting
- hidden dead time before the real provider queue even starts

## Persistence

## 1. Configuration

Store binding provider definitions in normal provider config storage alongside other providers:

- [storage.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/storage.cpp)

## 2. Runtime state

Store runtime routing/cooldown state in `.data`, not `.config`.

Suggested file:

```text
.data/provider_binding_runtime.json
```

Suggested key structure:

```json
{
  "providers": {
    "provider_binding_123": {
      "models": {
        "gpt54_routed": {
          "next_round_robin_index": 1,
          "targets": [
            {
              "provider_id": "provider_oauth_work",
              "model_id": "gpt-5.4",
              "last_status": "busy",
              "cooldown_until": "2026-04-23T14:22:00Z"
            }
          ]
        }
      }
    }
  }
}
```

This is runtime data and should be safe to reset if needed.

## UI Requirements

## 1. Provider Manager

Add provider type:

```text
Binding Provider
```

Provider editor for this type should show:

- provider name
- help text explaining that this is a virtual routed provider

Do not show:

- URL
- API key
- TLS fingerprint
- OAuth sign-in controls

## 2. Add Model for binding provider

This must use a dedicated routed-model dialog.

Required fields:

- model id
- display name
- context window (mandatory)
- max output tokens
- streaming capable
- tool capable
- vision capable
- routing mode
- default reasoning effort
- default verbosity

Target list area:

- list of current targets
- add target
- remove target
- move up
- move down
- enable/disable target

Per-target editor:

- provider selector
- model selector
- priority
- busy retry interval
- busy retry budget
- busy cooldown
- limit cooldown
- error cooldown

## 3. Add Target validation

- target provider must not be another binding provider in phase 1
- target model must exist on the selected provider
- duplicate `provider_id + model_id` pairs should be blocked

## 4. Capability summary

The UI should show a computed summary:

- minimum target context window
- intersection of streaming/tools/vision support
- intersection of reasoning/verbosity support

This should be visible while editing so the user can understand what the routed model is really promising.

## Request Pipeline Changes

## 1. New routing resolution step

Before a request is submitted through `OpenAIClient`, the app must resolve:

```text
binding provider + binding model
```

into:

```text
concrete provider + concrete model
```

This should happen in a dedicated resolver, for example:

```cpp
ResolvedProviderTarget ResolveBindingTarget(
    const ProviderConfig& binding_provider,
    const ModelConfig& binding_model,
    const std::vector<ProviderConfig>& all_providers,
    BindingRuntimeStore* runtime_store);
```

## 2. Where it should be used

Every major request path should route through the resolver first:

- desktop chat send path
- web chat send path
- tool-aware follow-up rounds
- simple completion path
- streaming completion path
- model tools if they resolve project provider/model
- compression model calls only if later configured to use binding providers

## 3. Debugging and visibility

For diagnostics, log:

- routed provider/model selected
- routing mode
- whether failover happened
- cooldown decision if a target was skipped

This should be included in:

- chat context debug entries where practical
- log output for failure analysis

## Web UI Status Recommendations

The web UI should eventually be able to show provider-routing status messages such as:

- `Routing through binding model GPT-5.4 Routed`
- `Primary target busy, failing over to OpenAI OAuth Personal / gpt-5.4`
- `Using round-robin target 2 of 3`

This is not mandatory for phase 1, but it will make this feature much easier to diagnose.

## Failure Semantics

If every target is unavailable:

- return the last most-relevant error
- include the routed model name in the error
- mention that all binding targets were unavailable

Example shape:

```text
All binding targets for "GPT-5.4 Routed" were unavailable. Last error: HTTP 429 (rate limited): usage limit reached.
```

## Recommended Initial Defaults

For new binding targets:

- `busy_retry_interval_seconds = 15`
- `busy_retry_budget_seconds = 90`
- `busy_cooldown_seconds = 300`
- `limit_cooldown_seconds = 900`
- `error_cooldown_seconds = 300`

For new binding models:

- `routing_mode = TopDownFailover`

Reason:

- failover is more predictable than round-robin
- this is the safer default for critical workflows

## Regression Tests To Add

This feature must be covered by the regression harness described in:

- [automated_regression_harness_plan.md](C:/Users/TheunisvanNiekerk/Code/agent/automated_regression_harness_plan.md)

Minimum phase 1 tests:

1. `binding_top_down_primary_success`
2. `binding_top_down_primary_busy_secondary_success`
3. `binding_top_down_limit_sets_cooldown`
4. `binding_round_robin_rotates_targets`
5. `binding_round_robin_skips_cooling_target`
6. `binding_rejects_nested_binding_provider_target`
7. `binding_context_window_validation`
8. `binding_capability_intersection_validation`

## Recommended Implementation Order

1. schema additions for binding provider/model/target config
2. runtime state store in `.data`
3. binding provider UI in Provider Manager
4. target resolution layer
5. request-path integration
6. logging/debug surfacing
7. regression tests

## Explicit Non-Goals For Phase 1

- nested binding providers
- splitting one user request across multiple targets
- mid-stream migration from one target to another
- weighted round robin
- latency-based adaptive routing
- target-specific prompt transformations

Those can come later if the basic routed-provider foundation proves stable.

## Recommended Summary

The clean phase 1 implementation is:

- one new virtual provider family: `binding_provider`
- routed models inside that provider
- top-down failover or round-robin target selection
- explicit cooldown tracking per target
- mandatory binding-model context window
- request-level target stickiness
- no second queue layer

That gets the operational value you want without making the rest of the app forget what a normal provider is.
