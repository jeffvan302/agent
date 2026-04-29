# Automated Regression Harness Plan

Date: 2026-04-23

## Status

This plan is now partially in flight.

### Implemented so far

- deterministic provider contract harness under `tests/provider_contract/`
- fake OpenAI-compatible server
- fake Codex CLI bridge
- headless `agent.exe --provider-contract ...` execution path
- isolated runtime roots for provider contract runs
- contract cases for:
  - `max_tokens` to `max_completion_tokens` fallback
  - busy/retry behavior
  - streaming success and truncated-stream failure
  - reasoning/verbosity request shaping
  - OpenAI OAuth bundled catalog and auth import
  - Codex OAuth transport
  - LM Studio catalog discovery scaffolding
  - binding-provider failover and round-robin behavior

### Still missing

- serialization/backward-compatibility tests
- MCP fixture servers
- web auth and SSE smoke tests
- deterministic RAG fixture tests
- remote-worker contract family integration
- standard fast/integration/heavy runner scripts

## Purpose

This document defines a practical regression-testing plan for the current agent platform.

The goal is not to build a huge or slow test framework first. The goal is to add a small set of deterministic harnesses that reduce fragility across the highest-risk integration points:

- provider requests and streaming
- MCP lifecycle and tool calls
- RAG ingestion and retrieval
- web authentication and SSE chat behavior
- remote worker startup, queueing, streaming, and shutdown

## Current Testing Gap

The codebase already has a broad working surface area, but most validation is still manual or button-driven. That is no longer enough for the current level of feature breadth.

The highest-risk integration areas are:

- [openai_client.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/openai_client.cpp)
- [mcp_manager.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/mcp_manager.cpp)
- [rag_service.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/rag_service.cpp)
- [web_server.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/web_server.cpp)
- [remote_ollama_worker.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/remote_ollama_worker.cpp)

There is already one useful seed harness in [test_remote_agent.py](C:/Users/TheunisvanNiekerk/Code/agent/test_remote_agent.py), but there is not yet a strong automated suite that covers the platform systematically.

## Guiding Principles

1. Prefer deterministic tests over live third-party dependencies.
2. Keep fast tests fast and heavy tests optional.
3. Use isolated runtime roots for every integration test.
4. Capture useful failure artifacts automatically.
5. Test protocol and persistence boundaries first.

## Recommended Test Tiers

### 1. Unit Tests

Use for:

- JSON serialization and deserialization
- default values and backward compatibility
- path resolution and configuration merging
- small pure functions and state transforms

These should run quickly and not start external processes.

### 2. Integration Tests

Use for:

- fake provider server interactions
- fake MCP stdio server interactions
- local web server auth and API routes
- RAG ingestion and retrieval with controlled fixtures
- isolated runtime root validation

These should run automatically in normal development.

### 3. End-to-End Tests

Use for:

- browser automation
- remote worker orchestration
- live streaming flows
- optional real Ollama or real provider checks

These can be slower and may be marked optional/manual if needed.

## Core Improvement Areas

## Provider Contract Harness

This was the first major harness added, and it is now the most mature automated test area in the repo.

Instead of relying mainly on live vendor testing, create a small local fake OpenAI-compatible server that can return deterministic responses for:

- normal non-streaming completion
- streaming completion
- tool-call response
- busy or overloaded response
- retry-after response
- truncated stream
- missing done marker
- malformed JSON
- `max_tokens` rejected, `max_completion_tokens` required

This harness should validate behavior in [openai_client.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/openai_client.cpp) without depending on external provider uptime, spend, or rate limits.

### What it should verify

- request shape by provider/model profile
- retry handling
- long-running stream handling
- tool-aware follow-up flow
- stream termination correctness
- queue status behavior where applicable

## MCP Fixture Servers

Status: not started yet.

Add a small set of tiny scripted MCP servers used only for automated testing.

Recommended fixtures:

- `echo_mcp`
- `slow_mcp`
- `invalid_json_mcp`
- `filesystem_edit_mcp`

These should validate [mcp_manager.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/mcp_manager.cpp) for:

- stdio startup and shutdown
- tool discovery
- tool call forwarding
- timeout behavior
- malformed response handling
- variable substitution
- large-file edit behavior through `edit_file`

## RAG Deterministic Fixture Suite

Status: not started yet.

RAG should not rely only on live embeddings or ad hoc manual tests.

Create a small deterministic test corpus:

- 2 plain text documents
- 1 markdown document
- 1 code file
- optional image/PDF fixture only when extraction can be stubbed deterministically

These tests should verify [rag_service.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/rag_service.cpp) for:

- library creation
- ingestion count
- chunk creation
- metadata persistence
- bindings persistence
- expected retrieval hit order for known queries

Phase 1 should avoid depending on a real embedding runtime when possible. A fake or deterministic embedding layer is preferable for regression purposes.

## Web Auth and SSE Smoke Tests

Status: not started yet.

The web server needs both HTTP-level and browser-level checks.

### HTTP-Level Tests

Validate:

- login
- logout
- remember-me session persistence
- unauthorized access rejection
- project access enforcement
- upload route behavior
- download route behavior

Focus on [web_server.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/web_server.cpp) and [web_user_store.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/web_user_store.cpp).

### Browser-Level Tests

Use Playwright or equivalent for a small smoke suite:

- login page works
- chat submission works
- SSE response completes
- tool trace appears
- compression status appears
- account dialog opens and updates

These tests do not need to be large. Even 4-5 browser smoke tests would catch a lot of regressions.

## Remote Worker Regression Family

Status: partially started through [test_remote_agent.py](C:/Users/TheunisvanNiekerk/Code/agent/test_remote_agent.py), but not yet unified into the main regression harness.

The remote worker already has a useful base harness in [test_remote_agent.py](C:/Users/TheunisvanNiekerk/Code/agent/test_remote_agent.py).

Expand it into a family of modes:

- `contract mode`: fake Ollama backend, deterministic
- `live mode`: real Ollama, optional/manual
- `queue mode`: verifies queue positions and worker assignment
- `shutdown mode`: verifies cleanup and orphan-process behavior
- `stream mode`: verifies chunking and long-running output flow

This should focus on [remote_ollama_worker.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/remote_ollama_worker.cpp).

## Serialization and Backward-Compatibility Tests

Status: not started yet.

This is a high-value, low-drama area to lock down.

Test round-tripping and tolerant loading for:

- providers
- users, groups, and bindings
- project settings
- compression configs
- model tools
- project RAG bindings

These tests should verify:

- missing fields get sane defaults
- old files still load
- save/load preserves expected values
- unrelated unknown fields do not destroy important persisted data

This protects [storage.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/storage.cpp) from subtle schema regressions.

## Isolated Runtime Roots

Every integration test should use a fresh runtime root.

Recommended layout per test case:

```text
<temp>/case-001/.config
<temp>/case-001/.data
<temp>/case-001/.log
```

This is especially important now that runtime layout separation exists in:

- [main.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/main.cpp#L144)
- [main.cpp](C:/Users/TheunisvanNiekerk/Code/agent/src/main.cpp#L5509)
- [storage.h](C:/Users/TheunisvanNiekerk/Code/agent/src/storage.h#L8)

Benefits:

- no test pollution of real user data
- no dependence on repo-root live state
- reproducible failures
- easier artifact capture

## Failure Artifact Capture

When an integration or e2e test fails, collect:

- request JSON
- response JSON
- SSE transcript
- `.log` contents for that case
- `.config` and `.data` snapshot for that case

This is especially important for:

- streaming failures
- MCP protocol failures
- remote worker queue behavior
- web auth/session bugs

## Suggested Repository Structure

One reasonable starting layout:

```text
tests/
  unit/
    storage/
    serialization/
    runtime_paths/
  integration/
    provider_contract/
    mcp/
    rag/
    web/
    remote_worker/
  fixtures/
    provider/
    mcp/
    rag/
    web/
  scripts/
    run_smoke.ps1
    run_integration.ps1
```

This structure is only a suggestion. The more important point is keeping deterministic fixtures and isolated runtime roots together and easy to run.

## Suggested Execution Model

### Fast Local Check

Should run:

- unit tests
- provider contract tests
- MCP fixture tests
- serialization tests

### Standard Integration Check

Should run:

- fast local check
- web auth/API integration
- RAG deterministic fixture tests
- remote worker contract mode

### Heavy or Optional Check

Should run:

- browser automation
- live Ollama tests
- live provider compatibility checks

## First 10 Tests To Implement

These are the most valuable first additions.

1. [x] Provider request fallback test for `max_tokens` to `max_completion_tokens`
2. [x] Provider streaming test with proper done marker
3. [x] Provider streaming failure test with unexpected EOF
4. [x] Provider retry test for busy/overloaded responses
5. [ ] MCP server connect/discover/call happy path
6. [ ] MCP invalid JSON response handling
7. [ ] Project settings serialization round-trip
8. [ ] Web login and remember-me persistence
9. [ ] RAG small fixture ingest and retrieval
10. [ ] Remote worker queue test with two concurrent jobs

## Recommended Priority Order

If we want the highest impact first:

1. serialization and backward-compatibility tests
2. MCP fixture-server tests
3. web auth and SSE smoke tests
4. remote worker contract tests
5. RAG deterministic fixture tests
6. heavier browser and live-environment checks

The reason the provider harness is no longer at the top is simple: it already exists and is the strongest test area we currently have.

## What Not To Do First

Avoid starting with a giant live end-to-end suite against real vendors and real local models.

That approach tends to be:

- slower
- more expensive
- less deterministic
- harder to diagnose

The strongest first move is a deterministic middle layer with fake providers and fake MCP servers, plus isolated runtime roots.

## Bottom Line

The project is ready for a real regression harness, but it should be built in focused layers:

- deterministic contract testing first
- isolated integration testing second
- heavy live end-to-end testing last

That sequence will reduce fragility much faster than trying to automate everything at once.
