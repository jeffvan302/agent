# Project Status Report

Original Report Date: 2026-04-22  
Updated: 2026-04-23

## Executive Summary

The app is still very much a broad native Windows AI workstation, but it has moved out of the purely exploratory phase in a few important areas.

Since the original report:

- provider compatibility has been refactored onto a stronger profile-driven base
- the first deterministic provider contract harness is in place
- `OpenAI OAuth (ChatGPT/Codex)` is now a real provider type with secure auth-record storage and a Codex CLI bridge transport
- `binding_provider` is now implemented as a routed virtual provider/model layer
- runtime layout is now split into `.config`, `.data`, and `.log`
- Layer 0 artifact memory is now implemented as an initial working feature inside hierarchical structured compression

So the overall picture has changed a bit:

- the feature surface is still large
- fragility is still real
- but some of the highest-risk foundational work has already started landing

The project no longer feels like "features first, architecture later" in quite the same way. We now have the beginning of a hardening track. The next gap is carrying that same discipline across MCP, web, RAG, and remote-worker coverage.

## Current State By Area

| Area | Status | Evidence | Notes |
| --- | --- | --- | --- |
| Desktop shell | Implemented and actively used | `src/main.cpp`, `build.bat` | Main window remains the operational control surface for projects, chats, providers, MCP, RAG, compression, web, and admin tooling. |
| Chat execution | Implemented | `src/main.cpp`, `src/openai_client.cpp` | Streaming, tool-aware loops, context compression, and project-aware execution are working across desktop and web flows. |
| Provider/model management | Implemented and expanded | `src/provider_manager.cpp`, `src/storage.cpp` | Supports normal OpenAI-compatible providers, `agent_https`, `openai_codex_oauth`, LM Studio-ready catalog handling, and routed binding providers. |
| Provider compatibility layer | Implemented foundation, still maturing | `src/provider_profiles.h`, `src/openai_client.cpp` | Request compatibility is now more profile-driven, including output-token fallback, reasoning/verbosity handling, OAuth transport branching, and binding-provider routing. |
| OpenAI OAuth provider | Implemented initial working path | `src/provider_manager.cpp`, `src/openai_client.cpp`, `src/provider_auth_bridge.cpp`, `src/storage.cpp` | Uses the official Codex login/Responses bridge path, secure auth records, and a bundled model manifest. It is usable, but still needs polish around UX, limits, and account diagnostics. |
| LM Studio readiness | Partially implemented | `src/provider_manager.cpp`, `src/provider_catalog.cpp`, `src/storage.cpp` | Catalog/discovery scaffolding exists, but this is not yet a fully rounded provider path with the same maturity as the other families. |
| Binding provider | Implemented phase 1 | `src/provider_manager.cpp`, `src/openai_client.cpp`, `src/storage.cpp` | Top-down failover and round-robin routing are implemented with persisted runtime cooldown state and provider contract coverage. |
| MCP management | Implemented and fairly mature | `src/mcp_manager.cpp`, `src/mcp_server_manager.cpp`, `mcp_servers.json` | Stdio MCP lifecycle, bindings, variables, tool exposure, and project-aware approval flows are already substantial. |
| Project settings | Implemented | `src/project_settings_dialog.cpp`, project settings JSON | Project-level provider/model selection, MCP bindings, RAG bindings, instructions, and compression config selection are persisted. |
| RAG engine | Implemented and usable | `src/rag_service.cpp`, `src/rag_service_manager.cpp`, `rag_service_requirements.md` | Libraries, ingestion, retrieval, bindings, and tool exposure are working. Quality/control improvements are still ahead of us. |
| Context compression | Implemented and differentiated | `src/context_compression.cpp`, `src/context_compression_manager.cpp` | Multiple strategies exist, including hierarchical structured compression with working Layer 0 artifact memory support. |
| Layer 0 artifact memory | Implemented initial phase | `src/context_compression.cpp`, `src/context_compression_manager.cpp`, `src/artifact_memory_tool_bridge.h` | Per-chat artifact extraction, index persistence, compact reinjection, and read-only tool access are working when hierarchical compression with L0 enabled is selected. |
| Model tools | Implemented as an orchestration layer | `src/model_tools_manager.cpp`, `model_tools.json`, `src/main.cpp` | Model tools can bind provider/model plus MCP/RAG access and compression configuration. |
| Web server | Implemented in a strong working phase | `src/web_server.cpp`, `src/web_config_dialog.cpp`, `src/admin_config_dialog.cpp` | HTTPS, auth, sessions, SSE chat, uploads, downloads, inline tool traces, provider status rows, and admin/user/group/project binding are already present. |
| Remote Ollama worker | Implemented, still operationally sensitive | `src/remote_ollama_worker.cpp`, remote-worker JSON configs | HTTPS remote mode, queueing, multi-instance local Ollama backing, streaming passthrough, and status reporting exist, but lifecycle hardening is still important. |
| Storage/persistence | Implemented and improved | `src/storage.cpp`, `src/storage.h`, `src/main.cpp` | Runtime layout now supports `.config`, `.data`, and `.log` with `--startup`, `--config-dir`, `--data-dir`, and `--log-dir`. |
| Automated testing | Early deterministic harness in place | `tests/provider_contract`, `test_remote_agent.py` | Provider contract coverage now exists. MCP, web, RAG, serialization, and remote-worker deterministic suites are still mostly missing. |

## What Has Improved Since The Original Report

### 1. Provider compatibility is no longer entirely ad hoc

The app now has a more explicit provider-profile/request-shaping layer instead of relying on scattered one-off conditionals. That does not mean provider compatibility is "done," but it is a much healthier base than before.

Concrete wins already landed:

- `max_tokens` to `max_completion_tokens` fallback coverage
- reasoning and verbosity metadata on models
- provider-type-aware model catalogs
- a real `openai_codex_oauth` branch
- request-level routing through binding providers

### 2. The first real regression harness now exists

The provider contract harness under `tests/provider_contract/` is small, but it is exactly the kind of deterministic middle layer the project needed.

It already covers:

- output-token compatibility fallback
- busy/retry behavior
- streaming success and truncated-stream failure
- reasoning and verbosity request shaping
- OpenAI OAuth manifest/auth import behavior
- Codex OAuth transport behavior
- LM Studio catalog discovery scaffolding
- binding-provider failover and round-robin behavior

That is a very meaningful shift from pure manual clicking.

### 3. Runtime layout has been separated

The app no longer has to treat the repo root as the natural live runtime directory. The runtime split into `.config`, `.data`, and `.log` is now implemented, and that gives us a much better foundation for deployment and testing.

### 4. Layer 0 artifact memory is no longer just a concept

The HSC Layer 0 work now exists in code and UI. It is not a paper design anymore.

That matters because it turns one of the project’s differentiators into a real runtime feature rather than a future-note.

## Where The Project Still Feels Fragile

- Provider compatibility is improved, but still not finished. The architecture is better; the long tail of provider-family drift is still ahead of us.
- OpenAI OAuth is now real, but it still needs UX hardening around live account status, usage-limit reporting, and clearer operational diagnostics.
- LM Studio is only partially landed. The catalog/discovery path is there; the full polished provider path is not.
- There is still no strong automated regression harness for MCP, web auth/SSE, RAG retrieval, serialization, or remote-worker lifecycle.
- Traditional API-key providers still store their API keys in plain provider config. OAuth auth records are handled separately, but the broader secrets story is not finished.
- Remote worker lifecycle and cleanup are better understood than before, but still delicate enough to deserve more contract coverage.
- The runtime layout is fixed in architecture, but some repo-local artifacts and test temp output are still showing up during development, so the working tree is not fully clean by default.

## Best Places To Continue Developing

### 1. Expand deterministic test coverage beyond providers

This is now the clearest next leverage point.

- Add serialization tests around config evolution.
- Add MCP fixture servers.
- Add web auth/SSE smoke coverage.
- Add deterministic remote-worker contract cases.
- Add a small RAG fixture suite.

Why this is first:

- the provider layer now has the start of a safety net
- the rest of the platform still mostly does not

### 2. Finish the second half of provider hardening

The provider foundation is in place, but the next pieces still matter:

- LM Studio full request-path support
- better live-limit and busy-state reporting
- cleaner account-state handling for OpenAI OAuth
- stronger per-provider capability normalization

### 3. Harden secret handling for non-OAuth providers

OAuth records are now separated. API keys for ordinary providers are still not.

That makes secret protection the next obvious storage/security step.

### 4. Deepen remote-worker regression and lifecycle coverage

The remote worker is strategically valuable enough that it deserves a deterministic test family, not just manual observation and one-off scripts.

### 5. Improve RAG quality and control

The RAG subsystem already works well enough that the next gains should be about retrieval quality, working-set control, and token-budget-aware behavior.

### 6. Polish Layer 0 artifact memory

The feature exists now, which changes the next question from "should we build it?" to "how do we make it robust and pleasant to operate?"

## Suggested Priority Order From Here

1. Expand automated regression coverage across MCP, web, RAG, serialization, and remote worker
2. Finish LM Studio and remaining provider-family hardening
3. Improve secret storage for API-key providers
4. Deepen remote-worker lifecycle reliability
5. Improve RAG retrieval quality and working-set policy
6. Polish artifact-memory revisioning and diagnostics

## Bottom-Line Assessment

The project is still broad, but it is less improvisational than it was in the previous report.

The most important change is not a single feature. It is that the app now has the beginning of a real hardening track:

- provider compatibility is being normalized
- runtime storage is being separated
- a deterministic provider harness exists
- advanced provider families are no longer just design notes

The main recommendation now is:

- keep that hardening momentum going
- avoid slipping back into manual-only confidence for the rest of the platform
- use the new provider/testing foundation as the template for MCP, web, RAG, and remote-worker stabilization

That will do more for the project’s next phase than adding another major subsystem right away.
