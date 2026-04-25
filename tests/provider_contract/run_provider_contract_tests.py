import argparse
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path
from uuid import uuid4


REPO_ROOT = Path(__file__).resolve().parents[2]
FAKE_PROVIDER = REPO_ROOT / "tests" / "provider_contract" / "fake_openai_provider.py"
FAKE_CODEX = REPO_ROOT / "tests" / "provider_contract" / "fake_codex_cli.py"
FAKE_OLLAMA = REPO_ROOT / "tests" / "provider_contract" / "fake_ollama_cli.py"
DEFAULT_AGENT = REPO_ROOT / "build" / "agent.exe"
WORK_TMP_ROOT = REPO_ROOT / "tests" / "provider_contract" / ".tmp"


def find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def start_fake_provider(scenario: str):
    port = find_free_port()
    process = subprocess.Popen(
        [sys.executable, str(FAKE_PROVIDER), "--scenario", scenario, "--port", str(port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        cwd=str(REPO_ROOT),
    )
    ready_line = process.stdout.readline().strip()
    if not ready_line:
        stderr = process.stderr.read()
        process.terminate()
        raise RuntimeError(f"Fake provider failed to start for {scenario}: {stderr}")
    ready = json.loads(ready_line)
    return process, ready["port"]


def stop_process(process: subprocess.Popen):
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def get_server_state(port: int):
    with urllib.request.urlopen(f"http://127.0.0.1:{port}/state", timeout=5) as response:
        return json.loads(response.read().decode("utf-8"))


def run_contract(agent_path: Path, payload, env_overrides=None):
    WORK_TMP_ROOT.mkdir(parents=True, exist_ok=True)
    contract_path = WORK_TMP_ROOT / f"provider-contract-{uuid4().hex}.json"
    try:
        contract_path.write_text(json.dumps(payload), encoding="utf-8")
        env = os.environ.copy()
        if env_overrides:
            env.update(env_overrides)
        process = subprocess.run(
            [str(agent_path), "--provider-contract", str(contract_path)],
            cwd=str(REPO_ROOT),
            capture_output=True,
            text=True,
            timeout=120,
            env=env,
        )
        stdout_lines = [line for line in process.stdout.splitlines() if line.strip()]
        if not stdout_lines:
            raise RuntimeError(
                f"Provider contract command produced no JSON output.\nSTDERR:\n{process.stderr}"
            )
        result = json.loads(stdout_lines[-1])
        return process.returncode, result, process.stderr
    finally:
        try:
            contract_path.unlink(missing_ok=True)
        except Exception:
            pass


def make_runtime_paths():
    temp_root = Path(tempfile.mkdtemp(prefix="agent-provider-contract-"))
    return temp_root, {
        "startup_root": str(temp_root),
        "config_root": str(temp_root / ".config"),
        "data_root": str(temp_root / ".data"),
        "log_root": str(temp_root / ".log"),
    }


def assert_case(name: str, condition: bool, detail: str):
    if not condition:
        raise AssertionError(f"{name}: {detail}")


def create_fake_codex_wrapper(runtime_root: Path) -> Path:
    wrapper_path = runtime_root / "fake_codex.cmd"
    wrapper_path.write_text(
        f'@echo off\r\n"{sys.executable}" "{FAKE_CODEX}" %*\r\n',
        encoding="utf-8",
    )
    return wrapper_path


def create_fake_ollama_wrapper(runtime_root: Path) -> Path:
    wrapper_path = runtime_root / "fake_ollama.cmd"
    wrapper_path.write_text(
        f'@echo off\r\n"{sys.executable}" "{FAKE_OLLAMA}" %*\r\n',
        encoding="utf-8",
    )
    return wrapper_path


def read_json_lines(path: Path):
    if not path.exists():
        return []
    items = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        items.append(json.loads(line))
    return items


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--agent", default=str(DEFAULT_AGENT))
    args = parser.parse_args()

    agent_path = Path(args.agent)
    if not agent_path.exists():
        raise FileNotFoundError(f"Agent executable not found: {agent_path}")

    cases = [
        {
            "name": "max_completion_tokens_fallback",
            "scenario": "max_completion_tokens_fallback",
            "mode": "simple",
            "expect_success": True,
        },
        {
            "name": "busy_retry_then_success",
            "scenario": "busy_retry_then_success",
            "mode": "simple",
            "expect_success": True,
        },
        {
            "name": "stream_success_done",
            "scenario": "stream_success_done",
            "mode": "stream",
            "expect_success": True,
        },
        {
            "name": "stream_truncated",
            "scenario": "stream_truncated",
            "mode": "stream",
            "expect_success": False,
        },
        {
            "name": "reasoning_and_verbosity_defaults",
            "scenario": "request_body_capture_success",
            "mode": "simple",
            "expect_success": True,
        },
        {
            "name": "oauth_bundled_catalog",
            "scenario": None,
            "mode": "catalog",
            "expect_success": True,
        },
        {
            "name": "oauth_import_from_codex_auth",
            "scenario": None,
            "mode": "oauth_import",
            "expect_success": True,
        },
        {
            "name": "oauth_codex_transport_text_success",
            "scenario": None,
            "mode": "test_connection",
            "expect_success": True,
        },
        {
            "name": "oauth_codex_transport_tool_call",
            "scenario": None,
            "mode": "tool_aware",
            "expect_success": True,
        },
        {
            "name": "lmstudio_catalog_discovery",
            "scenario": "lmstudio_models_catalog",
            "mode": "catalog",
            "expect_success": True,
        },
        {
            "name": "ollama_native_chat_keep_alive",
            "scenario": "ollama_native_chat",
            "mode": "simple",
            "expect_success": True,
        },
        {
            "name": "ollama_native_stream",
            "scenario": "ollama_native_chat",
            "mode": "stream",
            "expect_success": True,
        },
        {
            "name": "ollama_native_tool_call",
            "scenario": "ollama_native_chat",
            "mode": "tool_aware",
            "expect_success": True,
        },
        {
            "name": "ollama_model_info",
            "scenario": "ollama_model_info",
            "mode": "model_info",
            "expect_success": True,
        },
        {
            "name": "binding_top_down_primary_busy_secondary_success",
            "scenario": None,
            "mode": "simple",
            "expect_success": True,
        },
        {
            "name": "binding_round_robin_rotates_targets",
            "scenario": None,
            "mode": "simple",
            "expect_success": True,
        },
        {
            "name": "binding_limit_sets_cooldown",
            "scenario": None,
            "mode": "simple",
            "expect_success": True,
        },
    ]

    failures = []

    for case in cases:
        server_process = None
        server_processes = []
        runtime_root = None
        try:
            runtime_root, runtime_paths = make_runtime_paths()

            payload = {"mode": case["mode"], "runtime_paths": runtime_paths}
            env_overrides = None

            if case["name"] == "oauth_bundled_catalog":
                payload["provider"] = {
                    "id": "provider_oauth_catalog",
                    "name": "OpenAI OAuth Catalog",
                    "provider_type": "openai_codex_oauth",
                    "base_url": "https://api.openai.com/v1",
                }
            elif case["name"] == "oauth_import_from_codex_auth":
                codex_home = runtime_root / "isolated-codex-home"
                codex_home.mkdir(parents=True, exist_ok=True)
                (codex_home / "auth.json").write_text(json.dumps({
                    "auth_mode": "chatgpt",
                    "OPENAI_API_KEY": "sk-test-imported-from-codex-home",
                    "tokens": {
                        "access_token": "access-token-value",
                        "refresh_token": "refresh-token-value",
                        "account_id": "account-123",
                        "id_token": "eyJhbGciOiJub25lIn0.eyJlbWFpbCI6InRlc3RlckBleGFtcGxlLmNvbSIsIm5hbWUiOiJUZXN0ZXIgRXhhbXBsZSIsImV4cCI6MTc2NzIwMDAwMH0."
                    }
                }), encoding="utf-8")
                payload["provider"] = {
                    "id": "provider_oauth_import",
                    "name": "OpenAI OAuth Import",
                    "provider_type": "openai_codex_oauth",
                    "base_url": "https://api.openai.com/v1",
                }
                payload["codex_home"] = str(codex_home)
                payload["credential_id"] = "provider_auth_import_case"
            elif case["name"] in {"oauth_codex_transport_text_success", "oauth_codex_transport_tool_call"}:
                bridge_home = runtime_root / ".config" / "provider_auth_bridge" / "provider_oauth_access_only" / "codex_home"
                bridge_home.mkdir(parents=True, exist_ok=True)
                (bridge_home / "auth.json").write_text(json.dumps({
                    "auth_mode": "chatgpt",
                    "tokens": {
                        "access_token": "fake-access-token",
                        "refresh_token": "fake-refresh-token",
                        "account_id": "fake-account-id",
                    }
                }), encoding="utf-8")
                wrapper_path = create_fake_codex_wrapper(runtime_root)
                env_overrides = {
                    "AGENT_CODEX_CLI_PATH": str(wrapper_path),
                    "FAKE_CODEX_SCENARIO": "tool_call" if case["name"] == "oauth_codex_transport_tool_call" else "text_success",
                }
                payload["provider"] = {
                    "id": "provider_oauth_access_only",
                    "name": "OpenAI OAuth Access Only",
                    "provider_type": "openai_codex_oauth",
                    "oauth_authenticated": True,
                    "oauth_account_label": "tester@example.com",
                }
                payload["model"] = {
                    "id": "gpt-5.3-codex",
                    "display_name": "GPT-5.3 Codex",
                    "supports_streaming": True,
                }
                payload["request"] = {
                    "temperature": 0.0,
                    "messages": [{"role": "user", "content": "Reply with the single word pong."}],
                }
                if case["name"] == "oauth_codex_transport_tool_call":
                    payload["tools"] = [{
                        "name": "get_weather",
                        "description": "Get weather",
                        "parameters": {
                            "type": "object",
                            "properties": {
                                "city": {"type": "string"}
                            },
                            "required": ["city"]
                        }
                    }]
                    payload["request"]["messages"] = [{"role": "user", "content": "Call the get_weather tool for Boston."}]
            elif case["name"].startswith("binding_"):
                scenarios = {
                    "binding_top_down_primary_busy_secondary_success": ("always_busy", "request_body_capture_success"),
                    "binding_round_robin_rotates_targets": ("request_body_capture_success", "request_body_capture_success"),
                    "binding_limit_sets_cooldown": ("quota_exhausted", "request_body_capture_success"),
                }
                primary_process, primary_port = start_fake_provider(scenarios[case["name"]][0])
                secondary_process, secondary_port = start_fake_provider(scenarios[case["name"]][1])
                server_processes.extend([primary_process, secondary_process])
                target_model = {
                    "id": "fake-model",
                    "display_name": "Fake Model",
                    "context_window": 128000,
                    "supports_streaming": True,
                    "supports_tools": True,
                }
                payload["providers"] = [
                    {
                        "id": "provider_primary",
                        "name": "Primary Concrete",
                        "provider_type": "openai_compatible",
                        "base_url": f"http://127.0.0.1:{primary_port}",
                        "models": [target_model],
                    },
                    {
                        "id": "provider_secondary",
                        "name": "Secondary Concrete",
                        "provider_type": "openai_compatible",
                        "base_url": f"http://127.0.0.1:{secondary_port}",
                        "models": [target_model],
                    },
                ]
                payload["provider"] = {
                    "id": "provider_binding",
                    "name": "Binding Router",
                    "provider_type": "binding_provider",
                }
                payload["model"] = {
                    "id": "binding-model",
                    "display_name": "Binding Model",
                    "context_window": 128000,
                    "supports_streaming": True,
                    "supports_tools": True,
                    "is_binding_model": True,
                    "binding_routing_mode": "round_robin" if case["name"] == "binding_round_robin_rotates_targets" else "top_down_failover",
                    "binding_targets": [
                        {
                            "provider_id": "provider_primary",
                            "model_id": "fake-model",
                            "enabled": True,
                            "priority": 10,
                            "busy_retry_interval_seconds": 1,
                            "busy_retry_budget_seconds": 1,
                            "busy_cooldown_seconds": 60,
                            "limit_cooldown_seconds": 600,
                            "error_cooldown_seconds": 60,
                        },
                        {
                            "provider_id": "provider_secondary",
                            "model_id": "fake-model",
                            "enabled": True,
                            "priority": 20,
                            "busy_retry_interval_seconds": 1,
                            "busy_retry_budget_seconds": 1,
                            "busy_cooldown_seconds": 60,
                            "limit_cooldown_seconds": 600,
                            "error_cooldown_seconds": 60,
                        },
                    ],
                }
                payload["request"] = {
                    "temperature": 0.0,
                    "max_tokens": 8,
                    "messages": [{"role": "user", "content": "Reply with the single word pong."}],
                }
            elif case["name"].startswith("ollama_"):
                wrapper_path = create_fake_ollama_wrapper(runtime_root)
                ollama_log_path = runtime_root / "fake_ollama_log.jsonl"
                env_overrides = {
                    "AGENT_OLLAMA_CLI_PATH": str(wrapper_path),
                    "FAKE_OLLAMA_LOG_PATH": str(ollama_log_path),
                }
                payload["provider"] = {
                    "name": "Fake Ollama Provider",
                    "provider_type": "ollama_local",
                }
                payload["model"] = {
                    "id": "minimax-m2:cloud",
                    "display_name": "MiniMax M2 Cloud",
                    "supports_streaming": True,
                    "supports_tools": True,
                    "ollama_keep_alive_seconds": 300,
                }
                if case["mode"] in {"simple", "stream", "tool_aware"}:
                    payload["request"] = {
                        "temperature": 0.0,
                        "max_tokens": 8,
                        "messages": [
                            {"role": "user", "content": "Reply with the single word pong."}
                        ],
                    }
                if case["name"] == "ollama_native_tool_call":
                    payload["tools"] = [{
                        "name": "get_weather",
                        "description": "Get weather",
                        "parameters": {
                            "type": "object",
                            "properties": {
                                "city": {"type": "string"}
                            },
                            "required": ["city"]
                        }
                    }]
                    payload["request"]["messages"] = [{"role": "user", "content": "Call the get_weather tool for Boston."}]
            else:
                if case["scenario"] is not None:
                    server_process, port = start_fake_provider(case["scenario"])
                payload["provider"] = {
                    "name": "Fake Contract Provider",
                    "provider_type": "lmstudio_local" if case["name"] == "lmstudio_catalog_discovery" else "openai_compatible",
                    "base_url": f"http://127.0.0.1:{port}",
                }
                if case["mode"] in {"simple", "stream"}:
                    payload["model"] = {
                        "id": "fake-model",
                        "display_name": "Fake Model",
                        "supports_streaming": True,
                        "supports_tools": True,
                        "output_tokens_parameter": "auto",
                        "default_reasoning_effort": "xhigh" if case["name"] == "reasoning_and_verbosity_defaults" else "",
                        "default_text_verbosity": "high" if case["name"] == "reasoning_and_verbosity_defaults" else "",
                    }
                    payload["request"] = {
                        "temperature": 0.0,
                        "max_tokens": 8,
                        "messages": [
                            {"role": "user", "content": "Reply with the single word pong."}
                        ],
                    }

            if case["name"] == "binding_round_robin_rotates_targets":
                return_code, result, stderr = run_contract(agent_path, payload, env_overrides)
                assert_case(case["name"], return_code == 0, f"unexpected exit code {return_code}, stderr: {stderr}")
                assert_case(case["name"], result.get("success") is True, f"unexpected first result: {result}")
                return_code, result, stderr = run_contract(agent_path, payload, env_overrides)
                assert_case(case["name"], return_code == 0, f"unexpected exit code {return_code}, stderr: {stderr}")
                assert_case(case["name"], result.get("success") is True, f"unexpected second result: {result}")
                time.sleep(0.2)
                state = {"request_count": 0}
            elif case["name"] == "binding_limit_sets_cooldown":
                return_code, result, stderr = run_contract(agent_path, payload, env_overrides)
                assert_case(case["name"], return_code == 0, f"unexpected exit code {return_code}, stderr: {stderr}")
                assert_case(case["name"], result.get("success") is True, f"unexpected first result: {result}")
                return_code, result, stderr = run_contract(agent_path, payload, env_overrides)
                time.sleep(0.2)
                state = {"request_count": 0}
            else:
                return_code, result, stderr = run_contract(agent_path, payload, env_overrides)
                time.sleep(0.2)
                if case["name"].startswith("ollama_"):
                    state = {"events": read_json_lines(ollama_log_path)}
                else:
                    state = get_server_state(port) if server_process is not None else {"request_count": 0}

            assert_case(case["name"], return_code == 0, f"unexpected exit code {return_code}, stderr: {stderr}")
            assert_case(case["name"], result.get("success") == case["expect_success"], f"unexpected success payload: {result}")

            if case["name"] == "max_completion_tokens_fallback":
                assert_case(case["name"], state["request_count"] == 2, f"expected 2 requests, got {state['request_count']}")
                first_body = state["request_bodies"][0]
                second_body = state["request_bodies"][1]
                assert_case(case["name"], "max_tokens" in first_body, "first request did not use max_tokens")
                assert_case(case["name"], "max_completion_tokens" in second_body, "retry did not switch to max_completion_tokens")
                assert_case(case["name"], result.get("assistant_text") == "pong", f"unexpected assistant text: {result}")
            elif case["name"] == "busy_retry_then_success":
                assert_case(case["name"], state["request_count"] == 3, f"expected 3 requests, got {state['request_count']}")
                assert_case(case["name"], result.get("assistant_text") == "pong", f"unexpected assistant text: {result}")
            elif case["name"] == "stream_success_done":
                assert_case(case["name"], state["request_count"] == 1, f"expected 1 request, got {state['request_count']}")
                assert_case(case["name"], result.get("full_text") == "pong", f"unexpected stream text: {result}")
                assert_case(case["name"], result.get("chunks") == ["po", "ng"], f"unexpected stream chunks: {result}")
            elif case["name"] == "stream_truncated":
                assert_case(case["name"], state["request_count"] == 1, f"expected 1 request, got {state['request_count']}")
                assert_case(case["name"], "unexpectedly before completion" in result.get("error", "").lower(), f"unexpected error: {result}")
            elif case["name"] == "reasoning_and_verbosity_defaults":
                assert_case(case["name"], state["request_count"] == 1, f"expected 1 request, got {state['request_count']}")
                body = state["request_bodies"][0]
                assert_case(case["name"], body.get("reasoning_effort") == "xhigh", f"missing reasoning_effort: {body}")
                assert_case(case["name"], body.get("verbosity") == "high", f"missing verbosity: {body}")
            elif case["name"] == "oauth_bundled_catalog":
                models = result.get("models", [])
                model_ids = {item["id"] for item in models}
                assert_case(case["name"], "gpt-5.4" in model_ids, f"expected bundled manifest models, got {models}")
                assert_case(case["name"], all(item.get("catalog_source") == "bundled" for item in models), f"expected bundled catalog_source: {models}")
            elif case["name"] == "oauth_import_from_codex_auth":
                assert_case(case["name"], result.get("has_api_key") is True, f"expected imported API key: {result}")
                assert_case(case["name"], result.get("has_access_token") is True, f"expected imported access token: {result}")
                assert_case(case["name"], result.get("account_email") == "tester@example.com", f"unexpected account email: {result}")
                assert_case(case["name"], result.get("account_display_name") == "Tester Example", f"unexpected account display name: {result}")
            elif case["name"] == "oauth_codex_transport_text_success":
                message = result.get("message", "").lower()
                assert_case(case["name"], "pong" in message, f"unexpected OAuth Codex transport message: {result}")
            elif case["name"] == "oauth_codex_transport_tool_call":
                tool_calls = result.get("tool_calls", [])
                assert_case(case["name"], len(tool_calls) == 1, f"expected one tool call: {result}")
                assert_case(case["name"], tool_calls[0].get("name") == "get_weather", f"unexpected tool name: {result}")
                assert_case(case["name"], "\"city\":\"Boston\"" in tool_calls[0].get("arguments_json", ""), f"unexpected tool arguments: {result}")
            elif case["name"] == "lmstudio_catalog_discovery":
                assert_case(case["name"], state["request_count"] == 0, f"/models should not be counted as POST requests: {state}")
                models = result.get("models", [])
                model_ids = {item["id"] for item in models}
                assert_case(case["name"], {"qwen2.5-coder:7b", "llama3.2:3b"}.issubset(model_ids), f"unexpected LM Studio models: {models}")
                assert_case(case["name"], all(item.get("catalog_source") == "discovered" for item in models), f"expected discovered catalog_source: {models}")
            elif case["name"] == "ollama_native_chat_keep_alive":
                events = state["events"]
                commands = [item["value"] for item in events if item.get("event") == "command"]
                prompts = [item for item in events if item.get("event") == "prompt"]
                assert_case(case["name"], any(item.get("event") == "run_start" for item in events), f"missing run_start event: {events}")
                assert_case(case["name"], "/set nohistory" in commands, f"missing /set nohistory command: {events}")
                assert_case(case["name"], "/set noformat" in commands, f"missing /set noformat command: {events}")
                assert_case(case["name"], "/clear" in commands, f"missing /clear command: {events}")
                assert_case(case["name"], any(item.get("event") == "bye" for item in events), f"missing /bye shutdown: {events}")
                assert_case(case["name"], len(prompts) == 1, f"expected exactly one prompt: {events}")
                assert_case(case["name"], "<Conversation>" in prompts[0]["value"], f"expected full conversation prompt: {prompts}")
                assert_case(case["name"], result.get("assistant_text") == "pong", f"unexpected assistant text: {result}")
            elif case["name"] == "ollama_native_stream":
                events = state["events"]
                prompts = [item for item in events if item.get("event") == "prompt"]
                assert_case(case["name"], len(prompts) == 1, f"expected one streamed prompt: {events}")
                assert_case(case["name"], result.get("full_text") == "pong", f"unexpected stream text: {result}")
                assert_case(case["name"], result.get("chunks") == ["pong"], f"unexpected stream chunks: {result}")
            elif case["name"] == "ollama_native_tool_call":
                events = state["events"]
                commands = [item["value"] for item in events if item.get("event") == "command"]
                tool_calls = result.get("tool_calls", [])
                assert_case(case["name"], "/set format json" in commands, f"expected JSON mode command: {events}")
                assert_case(case["name"], len(tool_calls) == 1, f"expected one tool call: {result}")
                assert_case(case["name"], tool_calls[0].get("name") == "get_weather", f"unexpected tool name: {result}")
                assert_case(case["name"], '"city":"Boston"' in tool_calls[0].get("arguments_json", ""), f"unexpected tool arguments: {result}")
            elif case["name"] == "ollama_model_info":
                events = state["events"]
                model = result.get("model", {})
                assert_case(case["name"], any(item.get("event") == "show" for item in events), f"expected show command: {events}")
                assert_case(case["name"], model.get("context_window") == 204800, f"unexpected context window: {result}")
                assert_case(case["name"], model.get("supports_tools") is True, f"expected tool support: {result}")
                assert_case(case["name"], model.get("supports_vision") is False, f"unexpected vision support: {result}")
            elif case["name"] == "binding_top_down_primary_busy_secondary_success":
                primary_state = get_server_state(primary_port)
                secondary_state = get_server_state(secondary_port)
                assert_case(case["name"], primary_state["request_count"] >= 1, f"expected busy primary to receive at least one request: {primary_state}")
                assert_case(case["name"], secondary_state["request_count"] == 1, f"expected secondary success after failover: {secondary_state}")
                assert_case(case["name"], result.get("assistant_text") == "pong", f"unexpected assistant text: {result}")
            elif case["name"] == "binding_round_robin_rotates_targets":
                primary_state = get_server_state(primary_port)
                secondary_state = get_server_state(secondary_port)
                assert_case(case["name"], primary_state["request_count"] == 1, f"expected round robin to hit primary once: {primary_state}")
                assert_case(case["name"], secondary_state["request_count"] == 1, f"expected round robin to hit secondary once: {secondary_state}")
            elif case["name"] == "binding_limit_sets_cooldown":
                primary_state = get_server_state(primary_port)
                secondary_state = get_server_state(secondary_port)
                assert_case(case["name"], result.get("success") is True, f"unexpected second result: {result}")
                assert_case(case["name"], primary_state["request_count"] == 1, f"expected limited target to be skipped during cooldown: {primary_state}")
                assert_case(case["name"], secondary_state["request_count"] == 2, f"expected secondary to service both calls: {secondary_state}")

            print(f"[PASS] {case['name']}")
        except Exception as exc:
            failures.append((case["name"], str(exc)))
            print(f"[FAIL] {case['name']}: {exc}")
        finally:
            if server_process is not None:
                stop_process(server_process)
            for process in server_processes:
                stop_process(process)
            if runtime_root is not None:
                try:
                    import shutil
                    shutil.rmtree(runtime_root, ignore_errors=True)
                except Exception:
                    pass

    if failures:
        print("\nFailures:")
        for name, error in failures:
            print(f"- {name}: {error}")
        return 1

    print("\nAll provider contract tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
