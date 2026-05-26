# Provider Compatibility and Regression Execution Plan

Date: 2026-04-23  
Updated: 2026-04-23

## Purpose

This document is the working execution guide for the current hardening cycle around:

1. provider compatibility
2. OpenAI OAuth provider support
3. LM Studio readiness
4. automated regression coverage

It now reflects both:

- what has already been implemented
- what still needs to be done next

## Documents This Plan Builds On

- [project_status_report_2026-04-22.md](C:/Users/TheunisvanNiekerk/Code/agent/project_status_report_2026-04-22.md)
- [automated_regression_harness_plan.md](C:/Users/TheunisvanNiekerk/Code/agent/automated_regression_harness_plan.md)
- [openai_auth_provider_implementation.md](C:/Users/TheunisvanNiekerk/Code/agent/openai_auth_provider_implementation.md)
- [binding_provider_requirements.md](C:/Users/TheunisvanNiekerk/Code/agent/binding_provider_requirements.md)
- [runtime_layout_requirements.md](C:/Users/TheunisvanNiekerk/Code/agent/runtime_layout_requirements.md)

## Progress Snapshot

| Phase | Status | Notes |
| --- | --- | --- |
| Phase 1. Provider compatibility foundation | Completed initial phase | Provider profiles, centralized request-shape handling, explicit model capability metadata, and shared `Test Connection` execution are in place. |
| Phase 2. Provider contract harness | Completed initial phase | The deterministic provider contract harness under `tests/provider_contract/` is implemented and already covering compatibility, streaming, OAuth transport, LM Studio catalog discovery, and binding routing. |
| Phase 3. Provider storage and Provider Manager refactor | Mostly completed | Provider definitions, separate protected auth records, provider-type-aware UI, and catalog-driven model selection are implemented. |
| Phase 4. OpenAI OAuth provider | Implemented initial working path | `openai_codex_oauth` works through the official Codex CLI login/Responses bridge path with bundled model manifest support. |
| Phase 5. LM Studio support | Partially completed | Catalog/discovery and provider-family scaffolding are present, but the full polished request path is not finished yet. |
| Phase 6. Expand regression harness beyond providers | Not started in a serious way | Provider coverage exists, but MCP, web, RAG, serialization, and remote-worker harnesses are still ahead. |
| Phase 7. Test scripts and release gates | Not started | We do not yet have standard fast/integration/heavy commands or release gates. |

## What Has Already Landed

### Provider compatibility foundation

Implemented in:

- [provider_profiles.h](C:/Users/TheunisvanNiekerk/Code/agent/src/provider_profiles.h)
- [openai_client.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/openai_client.cpp)
- [types.h](C:/Users/TheunisvanNiekerk/Code/agent/src/types.h)
- [storage.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/storage.cpp)

Delivered pieces:

- profile-driven request shaping
- explicit output-token preference handling
- model reasoning/verbosity metadata
- real provider-family branching for `openai_codex_oauth`
- routed binding-provider execution
- `Test Connection` sharing the real request path instead of a disconnected one-off flow

### Provider contract harness

Implemented in:

- [fake_openai_provider.py](C:/Users/TheunisvanNiekerk/Code/agent/tests/provider_contract/fake_openai_provider.py)
- [fake_codex_cli.py](C:/Users/TheunisvanNiekerk/Code/agent/tests/provider_contract/fake_codex_cli.py)
- [run_provider_contract_tests.py](C:/Users/TheunisvanNiekerk/Code/agent/tests/provider_contract/run_provider_contract_tests.py)
- headless entry points in [main.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/main.cpp)

Covered cases already include:

- `max_tokens` to `max_completion_tokens` fallback
- busy/retry then success
- streaming success with `[DONE]`
- truncated stream failure
- reasoning/verbosity request-body shaping
- OpenAI OAuth bundled catalog behavior
- Codex auth import behavior
- Codex OAuth text/tool transport behavior
- LM Studio catalog discovery
- binding-provider failover and round-robin routing

### OpenAI OAuth provider

Implemented in:

- [provider_manager.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/provider_manager.cpp)
- [provider_auth_bridge.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/provider_auth_bridge.cpp)
- [provider_catalog.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/provider_catalog.cpp)
- [openai_client.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/openai_client.cpp)
- [storage.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/storage.cpp)

Delivered pieces:

- real `openai_codex_oauth` provider type
- bundled OpenAI OAuth model manifest
- protected provider auth record storage
- account-aware Provider Manager UI
- sign-in import through the official Codex CLI login flow
- request execution through the official Codex Responses bridge path

### Binding provider

Implemented in:

- [provider_manager.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/provider_manager.cpp)
- [openai_client.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/openai_client.cpp)
- [storage.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/storage.cpp)

Delivered pieces:

- `binding_provider` provider family
- routed binding models
- `top_down_failover`
- `round_robin`
- persisted runtime cooldown/round-robin state
- provider contract coverage for routed execution

## What Still Needs To Be Done

## Phase 5. Finish LM Studio As A First-Class Provider

This is now the next provider-family step that should be finished before we expand into more provider types.

### Step 5.1. Complete LM Studio request execution

The app is ready to discover LM Studio models and represent the family in Provider Manager, but the full request path still needs to be finished and hardened.

Complete:

- provider request profile for LM Studio-specific quirks
- test connection against the real LM Studio request path
- streaming behavior validation
- tool-call behavior validation if supported

### Step 5.2. Normalize LM Studio capability handling

Make sure LM Studio models cleanly express:

- context window
- tools support
- vision support
- reasoning/verbosity fields, if applicable

This should be driven by the same capability metadata path already used for the other advanced provider families.

### Exit criteria for LM Studio

- LM Studio can be added, refreshed, tested, and used from Provider Manager without ad hoc model entry
- provider contract coverage exists for the expected local quirks

## Phase 6. Expand Regression Coverage Beyond Providers

This is now the highest leverage engineering step.

### Step 6.1. Add serialization and backward-compatibility tests

Cover:

- providers
- users/groups/bindings
- project settings
- compression configs
- model tools
- RAG bindings

These are high-value and low-drama tests that will protect the config surface as it keeps evolving.

### Step 6.2. Add MCP fixture servers

Add deterministic MCP fixtures for:

- happy-path tool calls
- timeout behavior
- malformed JSON
- filesystem edit flows

### Step 6.3. Add web auth and SSE smoke tests

Cover:

- login
- remember-me persistence
- project access enforcement
- streaming completion
- tool/status row rendering
- account editing

### Step 6.4. Add remote-worker contract tests

Build on [test_remote_agent.py](C:/Users/TheunisvanNiekerk/Code/agent/test_remote_agent.py) and turn it into a deterministic family that covers:

- queue assignment
- multi-instance usage
- stream passthrough
- shutdown cleanup

### Step 6.5. Add deterministic RAG fixtures

Cover:

- ingestion count
- persistence
- retrieval ordering
- bindings behavior

All of the above should run with isolated runtime roots.

### Exit criteria for Phase 6

- provider, MCP, web, remote-worker, and RAG all have at least one deterministic integration harness
- the most fragile integration points no longer rely only on manual UI testing

## Phase 7. Add Test Commands and Release Gates

Once the coverage exists, make it easy to run and hard to forget.

### Step 7.1. Define standard test entry points

Recommended commands:

- `fast`
- `integration`
- `heavy`

### Step 7.2. Make provider-affecting changes run the provider contract suite

Any change touching:

- [openai_client.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/openai_client.cpp)
- [provider_manager.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/provider_manager.cpp)
- [storage.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/storage.cpp)
- [types.h](C:/Users/TheunisvanNiekerk/Code/agent/src/types.h)

should run the provider contract harness at minimum.

### Step 7.3. Capture failure artifacts automatically

Preserve:

- isolated `.config/.data/.log`
- request/response traces
- SSE transcripts
- remote-worker status snapshots

### Exit criteria for Phase 7

- routine named test commands exist
- provider contract testing is part of normal workflow
- failures produce useful artifacts for debugging

## Immediate Recommended Next Sprint

If we continue in the most practical order, the next sprint should do this:

1. finish LM Studio request execution and contract coverage
2. add serialization tests
3. add MCP fixture-server tests
4. add web auth/SSE smoke coverage
5. add the first remote-worker contract cases

That sequence keeps the momentum on hardening instead of bouncing back into feature-only work.

## Bottom Line

The original execution plan has started landing well.

The biggest change is that we are no longer standing at the entrance of this roadmap. We are partway through it:

- provider-family architecture is materially better
- OpenAI OAuth is real
- binding providers are real
- the first deterministic regression harness exists

The most important next move is to carry that same discipline across the rest of the integration surface, especially MCP, web, RAG, serialization, and remote-worker behavior.
