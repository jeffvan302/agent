import argparse
import json
import os
import sys
from pathlib import Path


def append_log(event):
    log_path = os.environ.get("FAKE_OLLAMA_LOG_PATH", "")
    if not log_path:
        return
    path = Path(log_path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(event) + "\n")


def write_prompt():
    sys.stdout.write(">>> ")
    sys.stdout.flush()


def model_card(model: str) -> str:
    capabilities = ["completion", "tools"]
    if model == "minimax-m2:cloud":
        context_length = 204800
        architecture = "minimax-m2"
        parameters = "230000000000"
    else:
        context_length = 196608
        architecture = "minimax-m2"
        parameters = "0"
    lines = [
        "  Model",
        f"    architecture        {architecture}",
        f"    parameters          {parameters}",
        f"    context length      {context_length}",
        "    embedding length    3072",
        "    quantization        ",
        "",
        "  Capabilities",
    ]
    for capability in capabilities:
        lines.append(f"    {capability}")
    lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("command", nargs="?")
    parser.add_argument("rest", nargs="*")
    args = parser.parse_args()

    if args.command == "show":
        model = next((item for item in args.rest if not item.startswith("-")), "")
        append_log({"event": "show", "model": model, "args": args.rest})
        sys.stdout.write(model_card(model))
        return 0

    if args.command == "stop":
        model = next((item for item in args.rest if not item.startswith("-")), "")
        append_log({"event": "stop", "model": model, "args": args.rest})
        return 0

    if args.command == "run":
        model = next((item for item in args.rest if not item.startswith("-")), "")
        append_log({"event": "run_start", "model": model, "args": args.rest})
        sys.stdout.write(f"Connecting to '{model}' on 'ollama.com' ⚡\n")
        write_prompt()

        format_json = False
        verbose = False
        multiline = False
        prompt_lines = []

        for raw_line in sys.stdin:
            line = raw_line.rstrip("\r\n")

            if multiline:
                if line == '"""':
                    prompt_text = "\n".join(prompt_lines)
                    append_log({
                        "event": "prompt",
                        "model": model,
                        "format_json": format_json,
                        "verbose": verbose,
                        "value": prompt_text,
                    })
                    if format_json and "get_weather" in prompt_text:
                        response = json.dumps({
                            "assistant_text": "",
                            "tool_calls": [
                                {
                                    "id": "call_ollama_1",
                                    "name": "get_weather",
                                    "arguments": {"city": "Boston"},
                                }
                            ],
                        })
                    elif format_json:
                        response = json.dumps({
                            "assistant_text": "pong",
                            "tool_calls": [],
                        })
                    else:
                        response = "pong"
                    sys.stdout.write(response + "\n\n")
                    if verbose:
                        sys.stdout.write("total duration:       1s\n")
                        sys.stdout.write("prompt eval count:    10 token(s)\n")
                        sys.stdout.write("eval count:           1 token(s)\n")
                    write_prompt()
                    sys.stdout.flush()
                    multiline = False
                    prompt_lines = []
                else:
                    prompt_lines.append(line)
                continue

            if not line:
                continue

            append_log({"event": "command", "model": model, "value": line})

            if line == '"""':
                multiline = True
                prompt_lines = []
                continue
            if line == "/clear":
                write_prompt()
                sys.stdout.flush()
                continue
            if line == "/set nohistory":
                write_prompt()
                sys.stdout.flush()
                continue
            if line == "/set format json":
                format_json = True
                sys.stdout.write("Set JSON mode.\n")
                write_prompt()
                sys.stdout.flush()
                continue
            if line == "/set noformat":
                format_json = False
                write_prompt()
                sys.stdout.flush()
                continue
            if line == "/set verbose":
                verbose = True
                sys.stdout.write("Set 'verbose' mode.\n")
                write_prompt()
                sys.stdout.flush()
                continue
            if line == "/set quiet":
                verbose = False
                write_prompt()
                sys.stdout.flush()
                continue
            if line == "/bye":
                append_log({"event": "bye", "model": model})
                return 0

            append_log({"event": "single_prompt", "model": model, "value": line})
            if format_json:
                sys.stdout.write('{"assistant_text":"pong","tool_calls":[]}\n\n')
            else:
                sys.stdout.write("pong\n\n")
            write_prompt()
            sys.stdout.flush()

        append_log({"event": "stdin_closed", "model": model})
        return 0

    if args.command in {"--help", "help", None}:
        sys.stdout.write("Fake Ollama CLI\n")
        return 0

    sys.stderr.write(f"Unsupported fake ollama command: {args.command}\n")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
