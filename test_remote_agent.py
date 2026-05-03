#!/usr/bin/env python3
"""
Remote Agent test bed.

Place this file next to:
  - agent.exe
  - remote.json
  - sample1.jpg
  - sample2.jpg

It launches agent.exe with --ollama-remote remote.json, waits for the HTTPS
worker to become healthy, submits both sample images at the same time, records
queue/worker-port behavior, then tries to shut the worker down cleanly.

The script uses only the Python standard library.
"""

from __future__ import annotations

import argparse
import base64
import concurrent.futures
import hashlib
import http.client
import json
import os
from pathlib import Path
import signal
import ssl
import subprocess
import threading
import time
from typing import Any


DEFAULT_PROMPT = (
    "Describe this image in detail for document ingestion. "
    "Include visible text, layout, objects, and anything relevant for search."
)


class AgentRequestError(RuntimeError):
    pass


def now() -> float:
    return time.perf_counter()


def elapsed(start: float) -> float:
    return round(time.perf_counter() - start, 3)


def normalize_fingerprint(value: str) -> str:
    value = value.strip()
    if ":" in value and "sha256" in value.split(":", 1)[0].lower():
        value = value.split(":", 1)[1]
    return "".join(ch.lower() for ch in value if ch.lower() in "0123456789abcdef")


def fingerprint_from_pem(pem: str) -> str:
    if not pem.strip():
        return ""
    der = ssl.PEM_cert_to_DER_cert(pem)
    return hashlib.sha256(der).hexdigest()


def read_remote_config(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def get_nested(data: dict[str, Any], *keys: str, default: Any = None) -> Any:
    current: Any = data
    for key in keys:
        if not isinstance(current, dict) or key not in current:
            return default
        current = current[key]
    return current


def parse_remote_config(config: dict[str, Any]) -> dict[str, Any]:
    cert_pem = str(get_nested(config, "agent_server", "certificate_pem", default="") or "")
    configured_fingerprint = str(
        get_nested(config, "agent_server", "certificate_fingerprint", default="") or ""
    )
    if not configured_fingerprint and cert_pem:
        configured_fingerprint = fingerprint_from_pem(cert_pem)

    https_port = int(get_nested(config, "agent_server", "https_port", default=8765) or 8765)
    start_port = int(get_nested(config, "ollama", "start_port", default=11434) or 11434)
    instance_count = int(get_nested(config, "ollama", "instance_count", default=1) or 1)
    num_thread = int(
        get_nested(config, "ollama", "num_thread", default=get_nested(config, "ollama", "num_threads", default=0)) or 0
    )
    accelerator = str(get_nested(config, "ollama", "accelerator", default="both") or "both").strip().lower()
    cpu_thread_percent = int(get_nested(config, "ollama", "cpu_thread_percent", default=50) or 50)
    num_parallel = int(get_nested(config, "ollama", "num_parallel", default=1) or 1)
    max_loaded_models = int(get_nested(config, "ollama", "max_loaded_models", default=1) or 1)
    logical_cpus = os.cpu_count() or 1
    cpu_thread_percent = min(100, max(1, cpu_thread_percent))
    usable_cpus = max(1, (logical_cpus * cpu_thread_percent + 99) // 100)
    effective_num_thread = num_thread if num_thread > 0 else max(1, usable_cpus // max(1, instance_count))
    return {
        "worker_name": str(config.get("worker_name") or "Remote Ollama Worker"),
        "https_port": https_port,
        "shared_secret": str(get_nested(config, "agent_server", "shared_secret", default="") or ""),
        "certificate_fingerprint": normalize_fingerprint(configured_fingerprint),
        "model_name": str(get_nested(config, "model", "name", default="qwen2.5vl:7b") or "qwen2.5vl:7b"),
        "model_purpose": str(get_nested(config, "model", "purpose", default="image_ingestion") or ""),
        "ollama_start_port": start_port,
        "ollama_instance_count": max(1, instance_count),
        "ollama_ports": [start_port + i for i in range(max(1, instance_count))],
        "ollama_accelerator": accelerator if accelerator in {"cpu", "gpu", "both"} else "both",
        "ollama_cpu_thread_percent": cpu_thread_percent,
        "ollama_num_thread": num_thread,
        "ollama_effective_num_thread": effective_num_thread,
        "ollama_num_parallel": num_parallel,
        "ollama_max_loaded_models": max_loaded_models,
    }


class AgentHttpsClient:
    def __init__(self, host: str, port: int, secret: str, fingerprint: str, timeout: float) -> None:
        self.host = host
        self.port = port
        self.secret = secret
        self.fingerprint = normalize_fingerprint(fingerprint)
        self.timeout = timeout
        self.context = ssl._create_unverified_context()

    def _verify_peer(self, conn: http.client.HTTPSConnection) -> None:
        if not self.fingerprint:
            return
        if not conn.sock:
            raise AgentRequestError("TLS socket was not available for certificate verification.")
        peer_cert = conn.sock.getpeercert(binary_form=True)
        actual = hashlib.sha256(peer_cert).hexdigest()
        if normalize_fingerprint(actual) != self.fingerprint:
            raise AgentRequestError(
                "Remote Agent certificate fingerprint mismatch. "
                f"expected={self.fingerprint} actual={actual}"
            )

    def request(
        self,
        method: str,
        path: str,
        payload: dict[str, Any] | None = None,
        extra_headers: dict[str, str] | None = None,
    ) -> tuple[int, dict[str, str], str]:
        body = None if payload is None else json.dumps(payload).encode("utf-8")
        headers = {
            "Accept": "application/json",
            "Authorization": f"Bearer {self.secret}",
        }
        if body is not None:
            headers["Content-Type"] = "application/json"
        if extra_headers:
            headers.update(extra_headers)

        conn = http.client.HTTPSConnection(
            self.host,
            self.port,
            timeout=self.timeout,
            context=self.context,
        )
        try:
            conn.connect()
            self._verify_peer(conn)
            conn.request(method, path, body=body, headers=headers)
            response = conn.getresponse()
            response_body = response.read().decode("utf-8", errors="replace")
            response_headers = {k.lower(): v for k, v in response.getheaders()}
            return response.status, response_headers, response_body
        finally:
            conn.close()

    def json_request(
        self,
        method: str,
        path: str,
        payload: dict[str, Any] | None = None,
        expected: tuple[int, ...] = (200,),
    ) -> tuple[dict[str, Any], dict[str, str]]:
        status, headers, body = self.request(method, path, payload)
        if status not in expected:
            raise AgentRequestError(f"{method} {path} returned HTTP {status}: {body[:1000]}")
        try:
            return json.loads(body), headers
        except json.JSONDecodeError as exc:
            raise AgentRequestError(f"{method} {path} returned non-JSON: {body[:1000]}") from exc

    def stream_lines(
        self,
        method: str,
        path: str,
        payload: dict[str, Any],
    ):
        body = json.dumps(payload).encode("utf-8")
        headers = {
            "Accept": "application/x-ndjson, application/json",
            "Authorization": f"Bearer {self.secret}",
            "Content-Type": "application/json",
        }
        conn = http.client.HTTPSConnection(
            self.host,
            self.port,
            timeout=self.timeout,
            context=self.context,
        )
        try:
            conn.connect()
            self._verify_peer(conn)
            conn.request(method, path, body=body, headers=headers)
            response = conn.getresponse()
            if response.status < 200 or response.status >= 300:
                response_body = response.read().decode("utf-8", errors="replace")
                raise AgentRequestError(f"{method} {path} returned HTTP {response.status}: {response_body[:1000]}")
            while True:
                line = response.readline()
                if not line:
                    break
                text = line.decode("utf-8", errors="replace").strip()
                if text:
                    yield text
        finally:
            conn.close()


def find_default_agent(script_dir: Path) -> Path:
    local = script_dir / "agent.exe"
    if local.exists():
        return local
    build = script_dir / "build" / "agent.exe"
    if build.exists():
        return build
    return local


def start_agent(agent_path: Path, config_path: Path, verbose: bool) -> subprocess.Popen[str]:
    if not agent_path.exists():
        raise FileNotFoundError(f"agent.exe was not found at {agent_path}")
    command = [str(agent_path), "--ollama-remote", str(config_path)]
    creationflags = 0
    if os.name == "nt":
        creationflags |= getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)

    print("Launching:", " ".join(f'"{part}"' if " " in part else part for part in command))
    proc = subprocess.Popen(
        command,
        cwd=str(config_path.parent),
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
        creationflags=creationflags,
    )

    if proc.stdout:
        def read_stdout() -> None:
            for line in proc.stdout:
                line = line.rstrip()
                if verbose:
                    print(f"[agent] {line}")

        thread = threading.Thread(target=read_stdout, daemon=True)
        thread.start()
    return proc


def wait_for_health(client: AgentHttpsClient, timeout: float) -> dict[str, Any]:
    start = now()
    last_error = ""
    while elapsed(start) < timeout:
        try:
            health, _ = client.json_request("GET", "/health")
            return health
        except Exception as exc:
            last_error = str(exc)
            time.sleep(1.0)
    raise TimeoutError(f"Remote Agent did not become healthy within {timeout}s. Last error: {last_error}")


def ollama_port_responding(port: int, timeout: float = 1.0) -> bool:
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=timeout)
    try:
        conn.request("GET", "/api/tags")
        response = conn.getresponse()
        response.read()
        return 200 <= response.status < 500
    except Exception:
        return False
    finally:
        conn.close()


def listening_pids_for_port(port: int) -> list[int]:
    if os.name != "nt":
        return []
    command = [
        "powershell",
        "-NoProfile",
        "-Command",
        (
            "Get-NetTCPConnection -LocalPort "
            f"{int(port)} -State Listen -ErrorAction SilentlyContinue | "
            "Select-Object -ExpandProperty OwningProcess -Unique"
        ),
    ]
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=10)
    except Exception:
        return []
    pids: list[int] = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if line.isdigit():
            pids.append(int(line))
    return sorted(set(pids))


def kill_pids(pids: list[int]) -> None:
    if not pids or os.name != "nt":
        return
    for pid in sorted(set(pids)):
        try:
            subprocess.run(
                ["powershell", "-NoProfile", "-Command", f"Stop-Process -Id {pid} -Force"],
                capture_output=True,
                text=True,
                timeout=10,
            )
        except Exception as exc:
            print(f"Warning: could not stop PID {pid}: {exc}")


def encode_image(path: Path) -> str:
    return base64.b64encode(path.read_bytes()).decode("ascii")


def generate_payload(image_path: Path, model_name: str, prompt: str, stream: bool = False) -> dict[str, Any]:
    return {
        "model": model_name,
        "prompt": prompt,
        "stream": stream,
        "images": [encode_image(image_path)],
    }


def submit_image_job(
    client: AgentHttpsClient,
    image_path: Path,
    model_name: str,
    prompt: str,
) -> dict[str, Any]:
    payload = {
        "endpoint": "/api/generate",
        "payload": generate_payload(image_path, model_name, prompt, stream=False),
    }
    started = now()
    data, _ = client.json_request("POST", "/jobs", payload, expected=(202,))
    return {
        "image": image_path.name,
        "job_id": data.get("job_id", ""),
        "initial_status": data.get("status", ""),
        "initial_queue_position": data.get("queue_position", 0),
        "submit_elapsed_seconds": elapsed(started),
        "timeline": [],
    }


def poll_jobs(
    client: AgentHttpsClient,
    jobs: list[dict[str, Any]],
    timeout: float,
    poll_interval: float,
) -> list[dict[str, Any]]:
    start = now()
    pending = {job["job_id"]: job for job in jobs if job.get("job_id")}
    last_printed: dict[str, tuple[Any, Any, Any]] = {}

    while pending and elapsed(start) < timeout:
        for job_id, job in list(pending.items()):
            try:
                snapshot, _ = client.json_request("GET", f"/jobs/{job_id}")
            except Exception as exc:
                job.setdefault("timeline", []).append({
                    "at_seconds": elapsed(start),
                    "error": str(exc),
                })
                continue

            status = snapshot.get("status", "")
            position = snapshot.get("queue_position", 0)
            worker_port = snapshot.get("worker_port", 0)
            key = (status, position, worker_port)
            if last_printed.get(job_id) != key:
                print(
                    f"{job['image']}: {status}"
                    f" queue_position={position}"
                    f" worker_port={worker_port or '-'}"
                )
                last_printed[job_id] = key
            job.setdefault("timeline", []).append({
                "at_seconds": elapsed(start),
                "status": status,
                "queue_position": position,
                "worker_port": worker_port,
            })

            if status in {"completed", "failed"}:
                job["final"] = snapshot
                response_body = snapshot.get("response_body", "")
                job["response_status"] = snapshot.get("response_status", 0)
                job["worker_port"] = worker_port
                job["completed_at_seconds"] = elapsed(start)
                try:
                    parsed_body = json.loads(response_body) if response_body else {}
                    response_text = str(parsed_body.get("response", ""))
                    job["response_chars"] = len(response_text)
                    job["response_excerpt"] = response_text[:500]
                except Exception:
                    job["response_chars"] = len(response_body)
                    job["response_excerpt"] = response_body[:500]
                pending.pop(job_id, None)

        if pending:
            time.sleep(poll_interval)

    for job in pending.values():
        job["timed_out"] = True
    return jobs


def run_jobs_mode(
    client: AgentHttpsClient,
    image_paths: list[Path],
    model_name: str,
    prompt: str,
    timeout: float,
    poll_interval: float,
) -> dict[str, Any]:
    print("\nSubmitting image jobs in parallel...")
    start = now()
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(image_paths)) as executor:
        futures = [
            executor.submit(submit_image_job, client, image_path, model_name, prompt)
            for image_path in image_paths
        ]
        jobs = [future.result() for future in concurrent.futures.as_completed(futures)]
    jobs.sort(key=lambda item: item["image"])

    for job in jobs:
        print(
            f"{job['image']}: submitted job_id={job['job_id']} "
            f"initial_position={job['initial_queue_position']}"
        )

    jobs = poll_jobs(client, jobs, timeout, poll_interval)
    total = elapsed(start)
    ports = sorted({int(job.get("worker_port") or 0) for job in jobs if int(job.get("worker_port") or 0) > 0})
    print(f"Job test completed in {total}s; worker ports used: {ports or 'none'}")
    return {
        "mode": "jobs",
        "elapsed_seconds": total,
        "worker_ports_used": ports,
        "jobs": jobs,
    }


def post_generate_sync(
    client: AgentHttpsClient,
    image_path: Path,
    model_name: str,
    prompt: str,
) -> dict[str, Any]:
    started = now()
    status, headers, body = client.request(
        "POST",
        "/api/generate",
        generate_payload(image_path, model_name, prompt, stream=False),
    )
    result: dict[str, Any] = {
        "image": image_path.name,
        "http_status": status,
        "elapsed_seconds": elapsed(started),
        "job_id": headers.get("x-agent-job-id", ""),
        "queue_position": headers.get("x-agent-queue-position", ""),
        "worker_port": headers.get("x-agent-worker-port", ""),
    }
    try:
        parsed = json.loads(body)
        response_text = str(parsed.get("response", ""))
        result["response_chars"] = len(response_text)
        result["response_excerpt"] = response_text[:500]
    except Exception:
        result["response_chars"] = len(body)
        result["response_excerpt"] = body[:500]
    return result


def run_sync_mode(
    client: AgentHttpsClient,
    image_paths: list[Path],
    model_name: str,
    prompt: str,
) -> dict[str, Any]:
    print("\nPosting /api/generate requests in parallel...")
    start = now()
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(image_paths)) as executor:
        futures = [
            executor.submit(post_generate_sync, client, image_path, model_name, prompt)
            for image_path in image_paths
        ]
        results = [future.result() for future in concurrent.futures.as_completed(futures)]
    results.sort(key=lambda item: item["image"])
    for item in results:
        print(
            f"{item['image']}: http={item['http_status']} "
            f"port={item.get('worker_port') or '-'} "
            f"elapsed={item['elapsed_seconds']}s"
        )
    ports = sorted({int(item.get("worker_port") or 0) for item in results if str(item.get("worker_port") or "").isdigit()})
    total = elapsed(start)
    print(f"Sync test completed in {total}s; worker ports used: {ports or 'none'}")
    return {
        "mode": "sync",
        "elapsed_seconds": total,
        "worker_ports_used": ports,
        "results": results,
    }


def post_generate_stream(
    client: AgentHttpsClient,
    image_path: Path,
    model_name: str,
    prompt: str,
) -> dict[str, Any]:
    started = now()
    first_chunk_seconds = None
    response_text = ""
    chunks = 0
    errors: list[str] = []
    payload = generate_payload(image_path, model_name, prompt, stream=True)
    for line in client.stream_lines("POST", "/api/generate", payload):
        if first_chunk_seconds is None:
            first_chunk_seconds = elapsed(started)
        chunks += 1
        try:
            item = json.loads(line)
        except json.JSONDecodeError:
            continue
        if "error" in item:
            errors.append(str(item["error"]))
        piece = str(item.get("response", ""))
        response_text += piece
    return {
        "image": image_path.name,
        "elapsed_seconds": elapsed(started),
        "first_chunk_seconds": first_chunk_seconds,
        "chunks": chunks,
        "response_chars": len(response_text),
        "response_excerpt": response_text[:500],
        "errors": errors,
    }


def run_stream_mode(
    client: AgentHttpsClient,
    image_paths: list[Path],
    model_name: str,
    prompt: str,
) -> dict[str, Any]:
    print("\nStreaming /api/generate requests in parallel...")
    start = now()
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(image_paths)) as executor:
        futures = [
            executor.submit(post_generate_stream, client, image_path, model_name, prompt)
            for image_path in image_paths
        ]
        results = [future.result() for future in concurrent.futures.as_completed(futures)]
    results.sort(key=lambda item: item["image"])
    for item in results:
        print(
            f"{item['image']}: chunks={item['chunks']} "
            f"first_chunk={item['first_chunk_seconds']}s "
            f"elapsed={item['elapsed_seconds']}s"
        )
    total = elapsed(start)
    print(f"Streaming test completed in {total}s.")
    return {
        "mode": "stream",
        "elapsed_seconds": total,
        "results": results,
    }


def stop_agent(proc: subprocess.Popen[str], graceful_timeout: float) -> dict[str, Any]:
    result: dict[str, Any] = {
        "attempted": True,
        "graceful_signal": "CTRL_BREAK_EVENT" if os.name == "nt" else "SIGINT",
        "terminated": False,
        "killed": False,
    }
    if proc.poll() is not None:
        result["already_exited"] = True
        result["returncode"] = proc.returncode
        return result

    start = now()
    print("\nStopping remote agent...")
    signal_sent = False
    try:
        if os.name == "nt":
            proc.send_signal(signal.CTRL_BREAK_EVENT)
        else:
            proc.send_signal(signal.SIGINT)
        signal_sent = True
        proc.wait(timeout=graceful_timeout)
        result["graceful_exit"] = True
    except Exception as exc:
        if not isinstance(exc, subprocess.TimeoutExpired):
            result["signal_error"] = str(exc)
            print(f"Could not send graceful stop signal: {exc}")
        result["graceful_exit"] = False
        if signal_sent:
            print(f"Agent did not exit within {graceful_timeout}s; terminating process.")
        else:
            print("Terminating agent process.")
        try:
            proc.terminate()
            result["terminated"] = True
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            print("Agent still did not exit; killing process.")
            proc.kill()
            result["killed"] = True
            proc.wait(timeout=10)
    finally:
        result["elapsed_seconds"] = elapsed(start)
        result["returncode"] = proc.poll()
    return result


def require_files(paths: list[Path]) -> None:
    missing = [str(path) for path in paths if not path.exists()]
    if missing:
        raise FileNotFoundError("Missing required file(s): " + ", ".join(missing))


def write_report(path: Path, report: dict[str, Any]) -> None:
    safe_report = dict(report)
    safe_report.pop("shared_secret", None)
    path.write_text(json.dumps(safe_report, indent=2), encoding="utf-8")
    print(f"\nWrote report: {path}")


def parse_args() -> argparse.Namespace:
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description="Test local Remote Agent HTTPS Ollama worker.")
    parser.add_argument("--agent", type=Path, default=find_default_agent(script_dir), help="Path to agent.exe.")
    parser.add_argument("--config", type=Path, default=script_dir / "remote.json", help="Path to remote.json.")
    parser.add_argument("--host", default="127.0.0.1", help="Remote Agent host to connect to.")
    parser.add_argument("--sample1", type=Path, default=script_dir / "sample1.jpg", help="First sample image.")
    parser.add_argument("--sample2", type=Path, default=script_dir / "sample2.jpg", help="Second sample image.")
    parser.add_argument("--mode", choices=("jobs", "sync", "stream", "both", "all"), default="jobs", help="Which parallel test to run.")
    parser.add_argument("--prompt", default=DEFAULT_PROMPT, help="Vision prompt to send with both images.")
    parser.add_argument("--startup-timeout", type=float, default=180.0, help="Seconds to wait for /health.")
    parser.add_argument("--request-timeout", type=float, default=900.0, help="HTTPS request timeout.")
    parser.add_argument("--job-timeout", type=float, default=1800.0, help="Seconds to wait for all jobs.")
    parser.add_argument("--poll-interval", type=float, default=0.75, help="Job polling interval.")
    parser.add_argument("--shutdown-timeout", type=float, default=20.0, help="Seconds to wait after CTRL+BREAK.")
    parser.add_argument("--no-launch", action="store_true", help="Do not start agent.exe; connect to an already-running worker.")
    parser.add_argument("--keep-running", action="store_true", help="Leave the launched agent.exe running after the test.")
    parser.add_argument("--kill-orphan-ollama", action="store_true", help="Kill newly-opened Ollama listener PIDs after shutdown.")
    parser.add_argument("--verbose-agent-log", action="store_true", help="Print agent.exe stdout while it runs.")
    parser.add_argument("--report", type=Path, default=script_dir / "remote_agent_test_report.json", help="JSON report path.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    args.config = args.config.resolve()
    args.agent = args.agent.resolve()
    args.sample1 = args.sample1.resolve()
    args.sample2 = args.sample2.resolve()
    args.report = args.report.resolve()

    require_files([args.config, args.sample1, args.sample2])
    if not args.no_launch:
        require_files([args.agent])

    config = read_remote_config(args.config)
    parsed = parse_remote_config(config)
    if not parsed["shared_secret"]:
        raise RuntimeError("remote.json has no agent_server.shared_secret.")

    print("Remote Agent test bed")
    print(f"Worker: {parsed['worker_name']}")
    print(f"Model: {parsed['model_name']} ({parsed['model_purpose'] or 'no purpose set'})")
    print(f"Agent HTTPS: https://{args.host}:{parsed['https_port']}")
    print(f"Ollama ports: {parsed['ollama_ports']}")
    print(
        "Ollama tuning: "
        f"accelerator={parsed['ollama_accelerator']} "
        f"cpu_budget={parsed['ollama_cpu_thread_percent']}% "
        f"num_thread={parsed['ollama_num_thread']} "
        f"effective_num_thread={parsed['ollama_effective_num_thread']} "
        f"num_parallel={parsed['ollama_num_parallel']} "
        f"max_loaded_models={parsed['ollama_max_loaded_models']}"
    )

    ports_before = {
        str(port): {
            "responding": ollama_port_responding(port),
            "pids": listening_pids_for_port(port),
        }
        for port in parsed["ollama_ports"]
    }
    print("Ollama ports before launch:", ports_before)

    proc: subprocess.Popen[str] | None = None
    report: dict[str, Any] = {
        "config": {
            "worker_name": parsed["worker_name"],
            "model_name": parsed["model_name"],
            "model_purpose": parsed["model_purpose"],
            "agent_https": f"https://{args.host}:{parsed['https_port']}",
            "ollama_ports": parsed["ollama_ports"],
            "ollama_instance_count": parsed["ollama_instance_count"],
            "ollama_accelerator": parsed["ollama_accelerator"],
            "ollama_cpu_thread_percent": parsed["ollama_cpu_thread_percent"],
            "ollama_num_thread": parsed["ollama_num_thread"],
            "ollama_effective_num_thread": parsed["ollama_effective_num_thread"],
            "ollama_num_parallel": parsed["ollama_num_parallel"],
            "ollama_max_loaded_models": parsed["ollama_max_loaded_models"],
        },
        "ports_before": ports_before,
        "tests": [],
    }

    try:
        if not args.no_launch:
            proc = start_agent(args.agent, args.config, args.verbose_agent_log)

        client = AgentHttpsClient(
            args.host,
            parsed["https_port"],
            parsed["shared_secret"],
            parsed["certificate_fingerprint"],
            args.request_timeout,
        )
        print("\nWaiting for /health...")
        health = wait_for_health(client, args.startup_timeout)
        report["health"] = health
        print(json.dumps(health, indent=2))

        image_paths = [args.sample1, args.sample2]
        if args.mode in {"jobs", "both", "all"}:
            report["tests"].append(
                run_jobs_mode(
                    client,
                    image_paths,
                    parsed["model_name"],
                    args.prompt,
                    args.job_timeout,
                    args.poll_interval,
                )
            )
        if args.mode in {"sync", "both", "all"}:
            report["tests"].append(
                run_sync_mode(
                    client,
                    image_paths,
                    parsed["model_name"],
                    args.prompt,
                )
            )
        if args.mode in {"stream", "all"}:
            report["tests"].append(
                run_stream_mode(
                    client,
                    image_paths,
                    parsed["model_name"],
                    args.prompt,
                )
            )

        queue_snapshot, _ = client.json_request("GET", "/queue")
        report["queue_after_tests"] = queue_snapshot
        print("\nQueue after tests:")
        print(json.dumps(queue_snapshot, indent=2))

    finally:
        if proc and not args.keep_running:
            report["shutdown"] = stop_agent(proc, args.shutdown_timeout)
            time.sleep(2.0)
        elif proc:
            report["shutdown"] = {"attempted": False, "reason": "--keep-running was used"}
        else:
            report["shutdown"] = {"attempted": False, "reason": "--no-launch was used"}

        ports_after = {
            str(port): {
                "responding": ollama_port_responding(port),
                "pids": listening_pids_for_port(port),
            }
            for port in parsed["ollama_ports"]
        }
        report["ports_after_shutdown"] = ports_after
        print("\nOllama ports after shutdown:", ports_after)

        orphan_ports: list[int] = []
        for port in parsed["ollama_ports"]:
            before = bool(ports_before[str(port)]["responding"])
            after = bool(ports_after[str(port)]["responding"])
            if not before and after:
                orphan_ports.append(port)
        report["new_ollama_ports_still_running"] = orphan_ports

        if orphan_ports:
            print(
                "Warning: these Ollama ports were not responding before the test "
                f"but are still responding now: {orphan_ports}"
            )
            if args.kill_orphan_ollama:
                pids = sorted(
                    {
                        pid
                        for port in orphan_ports
                        for pid in ports_after[str(port)].get("pids", [])
                    }
                )
                if pids:
                    print(f"Killing orphan candidate Ollama PID(s): {pids}")
                    kill_pids(pids)
                    report["killed_orphan_pids"] = pids
                else:
                    print("No listener PIDs found for orphan candidate ports.")

        write_report(args.report, report)

    failures = []
    for test in report.get("tests", []):
        ports = test.get("worker_ports_used", [])
        expected_parallel_ports = min(2, int(parsed["ollama_instance_count"]))
        if expected_parallel_ports > 1 and len(ports) < expected_parallel_ports:
            failures.append(
                f"{test.get('mode')} used {len(ports)} worker port(s), expected {expected_parallel_ports}."
            )
    if report.get("new_ollama_ports_still_running"):
        failures.append("New Ollama listener port(s) were still running after agent shutdown.")
    shutdown = report.get("shutdown", {})
    if shutdown.get("attempted") and not shutdown.get("graceful_exit", False):
        failures.append("agent.exe did not exit gracefully after CTRL+BREAK/SIGINT.")

    if failures:
        print("\nTest completed with warning(s):")
        for failure in failures:
            print(f"  - {failure}")
        return 2

    print("\nTest completed successfully.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nInterrupted.")
        raise SystemExit(130)
