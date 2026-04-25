import json
import os
import sys


def emit(event):
    sys.stdout.write(json.dumps(event) + "\n")
    sys.stdout.flush()


def run_responses():
    scenario = os.environ.get("FAKE_CODEX_SCENARIO", "text_success")
    payload = json.loads(sys.stdin.read() or "{}")

    if payload.get("stream") is not True:
        print("Error: codex responses expects a streaming payload with \"stream\": true", file=sys.stderr)
        return 1
    if payload.get("store") is not False:
        print("Error: store must be false for the Codex OAuth transport", file=sys.stderr)
        return 1
    if not payload.get("instructions"):
        print("Error: HTTP 400 Bad Request: Some({\"detail\":\"Instructions are required\"})", file=sys.stderr)
        return 1

    if scenario == "text_success":
        emit({"type": "response.created", "response": {}})
        emit({"type": "response.output_item.added", "item": {"type": "message", "role": "assistant", "content": [], "phase": "final_answer"}})
        emit({"type": "response.output_text.delta", "delta": "pong"})
        emit({"type": "response.output_item.done", "item": {"type": "message", "role": "assistant", "content": [{"type": "output_text", "text": "pong"}], "phase": "final_answer"}})
        emit({"type": "response.completed", "response": {"id": "resp_fake"}})
        return 0

    if scenario == "tool_call":
        tools = payload.get("tools", [])
        tool_name = tools[0]["name"] if tools else "demo_tool"
        emit({"type": "response.created", "response": {}})
        emit({"type": "response.output_item.added", "item": {"type": "function_call", "name": tool_name, "arguments": "", "call_id": "call_fake_1"}})
        emit({"type": "response.output_item.done", "item": {"type": "function_call", "name": tool_name, "arguments": "{\"city\":\"Boston\"}", "call_id": "call_fake_1"}})
        emit({"type": "response.completed", "response": {"id": "resp_fake_tool"}})
        return 0

    if scenario == "busy":
        print("Selected model is at capacity. Please try a different model.", file=sys.stderr)
        return 1

    if scenario == "usage_limit":
        print("You've hit your usage limit.", file=sys.stderr)
        return 1

    print(f"Unknown fake Codex scenario: {scenario}", file=sys.stderr)
    return 1


def main():
    if len(sys.argv) < 2:
        print("Expected a Codex subcommand.", file=sys.stderr)
        return 1

    command = sys.argv[1]
    if command == "responses":
        return run_responses()
    if command == "login" and len(sys.argv) > 2 and sys.argv[2] == "status":
        print("Logged in using ChatGPT")
        return 0

    print(f"Unsupported fake Codex command: {' '.join(sys.argv[1:])}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
