import argparse
import json
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class ScenarioState:
    def __init__(self, scenario: str):
        self.scenario = scenario
        self.lock = threading.Lock()
        self.requests = []

    def record_request(self, path, body):
        with self.lock:
            self.requests.append({"path": path, "body": body})

    def snapshot(self):
        with self.lock:
            return {
                "scenario": self.scenario,
                "request_count": len(self.requests),
                "request_bodies": [item["body"] for item in self.requests],
                "request_paths": [item["path"] for item in self.requests],
            }


def build_handler(state: ScenarioState):
    class Handler(BaseHTTPRequestHandler):
        def _send_json(self, status_code: int, payload, extra_headers=None):
            encoded = json.dumps(payload).encode("utf-8")
            self.send_response(status_code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(encoded)))
            if extra_headers:
                for key, value in extra_headers.items():
                    self.send_header(key, value)
            self.end_headers()
            self.wfile.write(encoded)

        def _send_sse(self, lines):
            body = "".join(lines).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "close")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _send_ndjson(self, lines):
            body = "".join(lines).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/x-ndjson")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "close")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if self.path == "/state":
                self._send_json(200, state.snapshot())
                return
            if self.path == "/models" and state.scenario == "lmstudio_models_catalog":
                self._send_json(200, {
                    "object": "list",
                    "data": [
                        {"id": "qwen2.5-coder:7b", "display_name": "Qwen 2.5 Coder 7B"},
                        {"id": "llama3.2:3b"},
                    ],
                })
                return
            self._send_json(404, {"error": "not_found"})

        def do_POST(self):
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length).decode("utf-8") if length > 0 else "{}"
            try:
                body = json.loads(raw)
            except json.JSONDecodeError:
                self._send_json(400, {"error": {"message": "invalid json"}})
                return

            state.record_request(self.path, body)
            scenario = state.scenario
            request_index = state.snapshot()["request_count"]
            stream = bool(body.get("stream", False))

            if self.path == "/api/show" and scenario == "ollama_model_info":
                self._send_json(200, {
                    "parameters": "temperature 0.7\nnum_ctx 131072",
                    "capabilities": ["completion", "tools", "vision"],
                    "details": {
                        "family": "kimi-k2.6",
                        "format": "gguf",
                    },
                    "model_info": {
                        "kimi-k2.6.context_length": 131072,
                    },
                })
                return

            if self.path == "/api/chat" and scenario == "ollama_native_chat":
                if stream:
                    self._send_ndjson([
                        '{"model":"fake-ollama","message":{"role":"assistant","content":"po"},"done":false}\n',
                        '{"model":"fake-ollama","message":{"role":"assistant","content":"ng"},"done":true,"done_reason":"stop"}\n',
                    ])
                    return
                if body.get("tools"):
                    self._send_json(200, {
                        "model": "fake-ollama",
                        "message": {
                            "role": "assistant",
                            "content": "",
                            "tool_calls": [
                                {
                                    "id": "call_ollama_1",
                                    "type": "function",
                                    "function": {
                                        "name": "get_weather",
                                        "arguments": {"city": "Boston"},
                                    },
                                }
                            ],
                        },
                        "done": True,
                        "done_reason": "tool_call",
                    })
                    return
                self._send_json(200, {
                    "model": "fake-ollama",
                    "message": {"role": "assistant", "content": "pong"},
                    "done": True,
                    "done_reason": "stop",
                })
                return

            if self.path != "/chat/completions":
                self._send_json(404, {"error": "not_found"})
                return

            if scenario == "max_completion_tokens_fallback":
                if "max_completion_tokens" in body:
                    self._send_json(200, {
                        "id": "chatcmpl-fallback",
                        "object": "chat.completion",
                        "choices": [{
                            "index": 0,
                            "finish_reason": "stop",
                            "message": {"role": "assistant", "content": "pong"},
                        }],
                    })
                    return
                self._send_json(400, {
                    "error": {
                        "message": "unsupported parameter max_tokens is not supported with this model. Use max_completion_tokens instead."
                    }
                })
                return

            if scenario == "busy_retry_then_success":
                if request_index < 3:
                    self._send_json(429, {
                        "error": {"message": "provider busy, please retry"},
                    }, extra_headers={"Retry-After": "1"})
                    return
                self._send_json(200, {
                    "id": "chatcmpl-retry",
                    "object": "chat.completion",
                    "choices": [{
                        "index": 0,
                        "finish_reason": "stop",
                        "message": {"role": "assistant", "content": "pong"},
                    }],
                })
                return

            if scenario == "always_busy":
                self._send_json(429, {
                    "error": {"message": "provider busy, please retry"},
                }, extra_headers={"Retry-After": "1"})
                return

            if scenario == "quota_exhausted":
                self._send_json(400, {
                    "error": {"message": "insufficient_quota: usage limit reached"},
                })
                return

            if scenario == "request_body_capture_success":
                self._send_json(200, {
                    "id": "chatcmpl-capture",
                    "object": "chat.completion",
                    "choices": [{
                        "index": 0,
                        "finish_reason": "stop",
                        "message": {"role": "assistant", "content": "pong"},
                    }],
                })
                return

            if scenario == "stream_success_done":
                if not stream:
                    self._send_json(400, {"error": {"message": "stream expected"}})
                    return
                self._send_sse([
                    'data: {"choices":[{"delta":{"content":"po"}}]}\n\n',
                    'data: {"choices":[{"delta":{"content":"ng"}}]}\n\n',
                    'data: [DONE]\n\n',
                ])
                return

            if scenario == "stream_truncated":
                if not stream:
                    self._send_json(400, {"error": {"message": "stream expected"}})
                    return
                self._send_sse([
                    'data: {"choices":[{"delta":{"content":"par"}}]}\n\n',
                    'data: {"choices":[{"delta":{"content":"tial"}}]}\n\n',
                ])
                return

            self._send_json(400, {"error": {"message": f"unknown scenario: {scenario}"}})

        def log_message(self, format, *args):
            return

    return Handler


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenario", required=True)
    parser.add_argument("--port", type=int, default=0)
    args = parser.parse_args()

    state = ScenarioState(args.scenario)
    server = ThreadingHTTPServer(("127.0.0.1", args.port), build_handler(state))
    print(json.dumps({"port": server.server_address[1], "scenario": args.scenario}), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
