from __future__ import annotations

import argparse
import asyncio
import base64
import json
import random
import re
import sys
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

DEPENDENCY_ERRORS: list[str] = []

try:
    from bs4 import BeautifulSoup
except ModuleNotFoundError:
    BeautifulSoup = None  # type: ignore[assignment]
    DEPENDENCY_ERRORS.append("beautifulsoup4")

try:
    from playwright.async_api import TimeoutError as PlaywrightTimeoutError
    from playwright.async_api import async_playwright
except ModuleNotFoundError:
    PlaywrightTimeoutError = TimeoutError  # type: ignore[assignment]
    async_playwright = None  # type: ignore[assignment]
    DEPENDENCY_ERRORS.append("playwright")

try:
    from undetected_playwright.async_api import async_playwright as undetected_async_playwright
except ImportError:
    undetected_async_playwright = None  # type: ignore[assignment]
    DEPENDENCY_ERRORS.append("undetected-playwright")


USER_AGENT = (
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
    "AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/122.0.0.0 Safari/537.36"
)

STEALTH_SCRIPT = """
Object.defineProperty(navigator, 'webdriver', { get: () => undefined });
Object.defineProperty(navigator, 'plugins', { get: () => [1, 2, 3] });
Object.defineProperty(navigator, 'languages', { get: () => ['en-US', 'en'] });
window.chrome = { runtime: {} };
"""

LAUNCH_ARGS = [
    "--no-sandbox",
    "--disable-blink-features=AutomationControlled",
    "--start-maximized",
]


def _clean_text(value: str | None) -> str:
    if not value:
        return ""
    return re.sub(r"\s+", " ", value).strip()


def _safe_name(url: str, ext: str) -> str:
    parsed = urlparse(url)
    base = (parsed.netloc + parsed.path).strip("/") or "page"
    safe = re.sub(r"[^A-Za-z0-9_.-]+", "_", base).strip("_")
    safe = re.sub(r"_+", "_", safe)
    return f"{safe[:80] or 'page'}.{ext}"


def _extract_text(html: str) -> str:
    try:
        soup = BeautifulSoup(html, "lxml")
    except Exception:
        soup = BeautifulSoup(html, "html.parser")
    for tag in soup(["script", "style", "noscript", "meta", "head"]):
        tag.decompose()
    lines = (line.strip() for line in soup.get_text(separator="\n").splitlines())
    return "\n".join(line for line in lines if line)


def _truncate(value: str, max_chars: int) -> tuple[str, bool]:
    if max_chars <= 0 or len(value) <= max_chars:
        return value, False
    return value[:max_chars] + "\n[truncated]", True


def _delay_range(payload: dict[str, Any], name: str, default_min: int, default_max: int) -> tuple[int, int]:
    delays = payload.get("delays") if isinstance(payload.get("delays"), dict) else {}
    item = delays.get(name) if isinstance(delays, dict) else None
    if isinstance(item, list) and len(item) >= 2:
        try:
            lo = max(0, int(item[0]))
            hi = max(lo, int(item[1]))
            return lo, hi
        except Exception:
            pass
    return default_min, max(default_min, default_max)


async def _delay(payload: dict[str, Any], name: str, default_min: int, default_max: int) -> None:
    lo, hi = _delay_range(payload, name, default_min, default_max)
    await asyncio.sleep(random.uniform(lo, hi) / 1000.0)


async def _build_context(playwright: Any, headless: bool, cookie_file: str | None):
    browser = await playwright.chromium.launch(headless=headless, args=LAUNCH_ARGS)
    storage_state = None
    if cookie_file:
        cookie_path = Path(cookie_file)
        if cookie_path.exists():
            storage_state = str(cookie_path)
    context = await browser.new_context(
        user_agent=USER_AGENT,
        viewport={"width": 1280, "height": 800},
        locale="en-US",
        timezone_id="America/New_York",
        storage_state=storage_state,
    )
    return context


async def _new_page(context: Any, timeout_seconds: int):
    page = await context.new_page()
    await page.add_init_script(STEALTH_SCRIPT)
    page.set_default_timeout(max(1, timeout_seconds) * 1000)
    return page


async def _goto(page: Any, url: str, wait_until: str, timeout_seconds: int) -> Any:
    try:
        return await page.goto(url, wait_until=wait_until, timeout=max(1, timeout_seconds) * 1000)
    except PlaywrightTimeoutError:
        return None


async def _type_humanly(page: Any, selector: str, text: str, payload: dict[str, Any]) -> None:
    await page.click(selector)
    for char in text:
        await page.keyboard.type(char)
        await _delay(payload, "keystroke", 150, 400)


async def _accept_google_cookies(page: Any, payload: dict[str, Any]) -> None:
    for selector in [
        "button:has-text('Accept all')",
        "button:has-text('I agree')",
        "button:has-text('Accept')",
    ]:
        try:
            await page.click(selector, timeout=2500)
            await _delay(payload, "click", 800, 1800)
            return
        except Exception:
            continue


def _parse_google(html: str, count: int) -> list[dict[str, Any]]:
    soup = BeautifulSoup(html, "lxml")
    results: list[dict[str, Any]] = []
    containers = soup.select("div.tF2Cxc") or soup.select("div.g")
    for container in containers:
        title_el = container.select_one("h3")
        link_el = container.select_one("a[href^='http']")
        snippet_el = container.select_one("div.VwiC3b, span.aCOpRe")
        display_el = container.select_one("cite, span.tjvcx")
        title = _clean_text(title_el.get_text(" ", strip=True) if title_el else "")
        href = link_el.get("href") if link_el else ""
        snippet = _clean_text(snippet_el.get_text(" ", strip=True) if snippet_el else "")
        display = _clean_text(display_el.get_text(" ", strip=True) if display_el else "")
        if title and href and href.startswith("http"):
            results.append({
                "title": title,
                "url": href,
                "display_url": display or None,
                "snippet": snippet or None,
            })
        if len(results) >= count:
            break
    return results


def _parse_bing(html: str, count: int) -> list[dict[str, Any]]:
    soup = BeautifulSoup(html, "lxml")
    results: list[dict[str, Any]] = []
    for container in soup.select("li.b_algo"):
        link_el = container.select_one("h2 a[href^='http']")
        title = _clean_text(link_el.get_text(" ", strip=True) if link_el else "")
        href = link_el.get("href") if link_el else ""
        snippet_el = container.select_one("p")
        display_el = container.select_one("cite")
        snippet = _clean_text(snippet_el.get_text(" ", strip=True) if snippet_el else "")
        display = _clean_text(display_el.get_text(" ", strip=True) if display_el else "")
        if title and href and href.startswith("http"):
            results.append({
                "title": title,
                "url": href,
                "display_url": display or None,
                "snippet": snippet or None,
            })
        if len(results) >= count:
            break
    return results


async def _search_with_engine(
    context: Any,
    engine: str,
    query: str,
    count: int,
    payload: dict[str, Any],
    timeout_seconds: int,
) -> dict[str, Any]:
    page = await _new_page(context, timeout_seconds)
    wait_until = payload.get("wait_until") or "networkidle"
    if wait_until not in {"load", "domcontentloaded", "networkidle"}:
        wait_until = "networkidle"

    if engine == "bing":
        await _goto(page, "https://www.bing.com", "domcontentloaded", timeout_seconds)
        await _delay(payload, "page_load", 2500, 5500)
        await _type_humanly(page, "textarea[name='q'], input[name='q']", query, payload)
        await _delay(payload, "pre_submit", 1000, 2500)
        await page.keyboard.press("Enter")
        try:
            await page.wait_for_load_state(wait_until, timeout=timeout_seconds * 1000)
        except PlaywrightTimeoutError:
            pass
        await _delay(payload, "post_results", 2500, 6000)
        html = await page.content()
        results = _parse_bing(html, count)
    else:
        await _goto(page, "https://www.google.com", "domcontentloaded", timeout_seconds)
        await _delay(payload, "page_load", 2500, 5500)
        await _accept_google_cookies(page, payload)
        await _type_humanly(page, "textarea[name='q'], input[name='q']", query, payload)
        await _delay(payload, "pre_submit", 1000, 2500)
        await page.keyboard.press("Enter")
        try:
            await page.wait_for_load_state(wait_until, timeout=timeout_seconds * 1000)
        except PlaywrightTimeoutError:
            pass
        await _delay(payload, "post_results", 2500, 6000)
        try:
            await context.storage_state(path=str(payload.get("cookie_file"))) if payload.get("cookie_file") else None
        except Exception:
            pass
        html = await page.content()
        results = _parse_google(html, count)

    await page.close()
    return {"engine": engine, "results": results}


async def _fetch_page(
    context: Any,
    url: str,
    content_type: str,
    payload: dict[str, Any],
    timeout_seconds: int,
) -> dict[str, Any]:
    page = await _new_page(context, timeout_seconds)
    wait_until = payload.get("wait_until") or "networkidle"
    if wait_until not in {"load", "domcontentloaded", "networkidle"}:
        wait_until = "networkidle"
    response = await _goto(page, url, wait_until, timeout_seconds)
    await _delay(payload, "page_load", 2500, 5500)
    await _delay(payload, "post_results", 2500, 6000)

    final_url = page.url
    title = await page.title()
    result: dict[str, Any] = {
        "url": final_url,
        "title": title,
        "status": response.status if response else None,
    }
    max_chars = int(payload.get("max_content_chars") or 60000)

    include_html = content_type in {"html", "text_html", "all"}
    include_text = content_type in {"text", "text_html", "all"}
    include_pdf = content_type in {"pdf", "all"}

    html = ""
    if include_html or include_text:
        html = await page.content()
    if include_text:
        text, truncated = _truncate(_extract_text(html), max_chars)
        result["text"] = text
        result["text_truncated"] = truncated
    if include_html:
        html_text, truncated = _truncate(html, max_chars)
        result["html"] = html_text
        result["html_truncated"] = truncated
    if include_pdf:
        output_path = payload.get("output_path") or ""
        if not output_path:
            output_dir = Path(payload.get("output_dir") or ".").expanduser().resolve()
            output_dir.mkdir(parents=True, exist_ok=True)
            output_path = str(output_dir / _safe_name(final_url, "pdf"))
        out = Path(output_path).expanduser().resolve()
        out.parent.mkdir(parents=True, exist_ok=True)
        await page.pdf(
            path=str(out),
            format=str(payload.get("page_format") or "A4"),
            print_background=bool(payload.get("print_background", True)),
        )
        result["pdf_path"] = str(out)
        result["pdf_bytes"] = out.stat().st_size if out.exists() else 0

    await page.close()
    return result


def _engine_sequence(payload: dict[str, Any]) -> list[str]:
    allowed = payload.get("allowed_engines")
    if not isinstance(allowed, list):
        allowed = ["google", "bing"]
    allowed_set = {str(engine).lower() for engine in allowed if str(engine).lower() in {"google", "bing"}}
    if not allowed_set:
        allowed_set = {"google"}

    requested = str(payload.get("engine") or "default").lower()
    default_engine = str(payload.get("default_engine") or "google").lower()
    order = payload.get("engine_order") if isinstance(payload.get("engine_order"), list) else ["google", "bing"]
    ordered = [str(engine).lower() for engine in order if str(engine).lower() in allowed_set]
    for engine in ["google", "bing"]:
        if engine in allowed_set and engine not in ordered:
            ordered.append(engine)

    if requested in {"google", "bing"}:
        return [requested] if requested in allowed_set else ordered
    if requested == "default" and default_engine in allowed_set:
        return [default_engine] + [engine for engine in ordered if engine != default_engine]
    return ordered


async def _run(payload: dict[str, Any]) -> dict[str, Any]:
    if DEPENDENCY_ERRORS:
        missing = ", ".join(sorted(set(DEPENDENCY_ERRORS)))
        return {
            "success": False,
            "error": (
                "Missing Python dependencies for Browser Web Search: "
                f"{missing}. Install with "
                "python -m pip install -r scripts/built_in_browser_search_requirements.txt "
                "and then run python -m playwright install chromium."
            ),
            "error_type": "MissingDependency",
        }

    action = str(payload.get("action") or "search").lower()
    if action not in {"search", "fetch", "search_and_fetch"}:
        return {"success": False, "error": f"Unknown action: {action}"}

    timeout_seconds = max(1, min(600, int(payload.get("timeout_seconds") or 180)))
    headless = not bool(payload.get("open_visual_browser", False))
    cookie_file = payload.get("cookie_file") or None
    if cookie_file:
        Path(cookie_file).expanduser().resolve().parent.mkdir(parents=True, exist_ok=True)

    async with undetected_async_playwright() as playwright:
        context = await _build_context(playwright, headless=headless, cookie_file=cookie_file)
        try:
            if action == "fetch":
                url = str(payload.get("url") or "").strip()
                if not url:
                    return {"success": False, "error": "Fetch action requires url."}
                content_type = str(payload.get("content_type") or "text").lower()
                fetched = await _fetch_page(context, url, content_type, payload, timeout_seconds)
                return {"success": True, "action": action, "fetched": fetched}

            query = str(payload.get("query") or "").strip()
            if not query:
                return {"success": False, "error": "Search action requires query."}
            count = max(1, min(20, int(payload.get("result_count") or 8)))
            engines = _engine_sequence(payload)
            tried: list[str] = []
            selected: dict[str, Any] | None = None
            for engine in engines:
                tried.append(engine)
                selected = await _search_with_engine(context, engine, query, count, payload, timeout_seconds)
                if selected.get("results"):
                    break

            response: dict[str, Any] = {
                "success": True,
                "action": action,
                "query": query,
                "engine": selected.get("engine") if selected else None,
                "engines_tried": tried,
                "results": selected.get("results") if selected else [],
            }

            if action == "search_and_fetch":
                results = response["results"]
                if not results:
                    response["success"] = False
                    response["error"] = "Search returned no fetchable results."
                    return response
                index = max(1, int(payload.get("fetch_result_index") or 1))
                index = min(index, len(results))
                content_type = str(payload.get("content_type") or "text").lower()
                fetched = await _fetch_page(context, results[index - 1]["url"], content_type, payload, timeout_seconds)
                response["fetch_result_index"] = index
                response["fetched"] = fetched
            return response
        finally:
            await context.browser.close()


def _load_payload() -> dict[str, Any]:
    parser = argparse.ArgumentParser()
    parser.add_argument("--payload-base64", required=True)
    args = parser.parse_args()
    data = base64.b64decode(args.payload_base64).decode("utf-8")
    payload = json.loads(data)
    if not isinstance(payload, dict):
        raise ValueError("Payload must be a JSON object.")
    return payload


def main() -> int:
    try:
        payload = _load_payload()
        result = asyncio.run(_run(payload))
    except Exception as exc:
        result = {"success": False, "error": str(exc), "error_type": type(exc).__name__}
    sys.stdout.write(json.dumps(result, ensure_ascii=False))
    sys.stdout.write("\n")
    return 0 if result.get("success") else 1


if __name__ == "__main__":
    raise SystemExit(main())
