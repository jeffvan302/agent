from __future__ import annotations

import argparse
import asyncio
import base64
import json
import re
import sys
import urllib.error
import urllib.request
from typing import Any
from urllib.parse import urlparse

DEPENDENCY_ERRORS: list[str] = []

try:
    from playwright.async_api import TimeoutError as PlaywrightTimeoutError
    from playwright.async_api import async_playwright
except ModuleNotFoundError:
    PlaywrightTimeoutError = TimeoutError  # type: ignore[assignment]
    async_playwright = None  # type: ignore[assignment]
    DEPENDENCY_ERRORS.append("playwright")


DEFAULT_PORTS = [9222, 9223, 9224, 9333, 9515]


INSPECT_SCRIPT = r"""
(args) => {
  const maxElements = Math.max(1, Math.min(2000, Number(args.maxElements || 300)));
  const maxTextChars = Math.max(1000, Math.min(200000, Number(args.maxTextChars || 60000)));
  const normalize = (value) => (value || "").replace(/\s+/g, " ").trim();
  const cssEscape = (value) => {
    if (window.CSS && CSS.escape) return CSS.escape(value);
    return String(value).replace(/[^A-Za-z0-9_-]/g, "\\$&");
  };
  const visible = (element) => {
    const rect = element.getBoundingClientRect();
    const style = window.getComputedStyle(element);
    return rect.width > 0 && rect.height > 0 &&
      style.visibility !== "hidden" && style.display !== "none" &&
      Number(style.opacity || "1") > 0;
  };
  const roleFor = (element) => {
    const explicit = normalize(element.getAttribute("role"));
    if (explicit) return explicit;
    const tag = element.tagName.toLowerCase();
    const type = (element.getAttribute("type") || "").toLowerCase();
    if (tag === "button") return "button";
    if (tag === "a" && element.hasAttribute("href")) return "link";
    if (tag === "textarea") return "textbox";
    if (tag === "select") return "combobox";
    if (tag === "input") {
      if (type === "button" || type === "submit" || type === "reset") return "button";
      if (type === "checkbox") return "checkbox";
      if (type === "radio") return "radio";
      if (type === "range") return "slider";
      return "textbox";
    }
    return "";
  };
  const labelText = (element) => {
    if (element.id) {
      const label = document.querySelector(`label[for="${cssEscape(element.id)}"]`);
      if (label) return normalize(label.innerText || label.textContent);
    }
    const parentLabel = element.closest("label");
    return parentLabel ? normalize(parentLabel.innerText || parentLabel.textContent) : "";
  };
  const nameFor = (element) => {
    const aria = normalize(element.getAttribute("aria-label"));
    if (aria) return aria;
    const labelledBy = normalize(element.getAttribute("aria-labelledby"));
    if (labelledBy) {
      const parts = labelledBy.split(/\s+/).map((id) => {
        const labelled = document.getElementById(id);
        return labelled ? normalize(labelled.innerText || labelled.textContent) : "";
      }).filter(Boolean);
      if (parts.length) return parts.join(" ");
    }
    const label = labelText(element);
    if (label) return label;
    const placeholder = normalize(element.getAttribute("placeholder"));
    if (placeholder) return placeholder;
    const alt = normalize(element.getAttribute("alt"));
    if (alt) return alt;
    const title = normalize(element.getAttribute("title"));
    if (title) return title;
    const value = normalize(element.value);
    const tag = element.tagName.toLowerCase();
    const type = (element.getAttribute("type") || "").toLowerCase();
    if (tag === "input" && ["button", "submit", "reset"].includes(type) && value) return value;
    return normalize(element.innerText || element.textContent);
  };
  const selectorFor = (element) => {
    if (element.id) return `#${cssEscape(element.id)}`;
    const attrs = ["data-testid", "data-test", "data-qa", "name", "aria-label", "placeholder"];
    for (const attr of attrs) {
      const value = element.getAttribute(attr);
      if (!value) continue;
      const selector = `${element.tagName.toLowerCase()}[${attr}="${String(value).replace(/\\/g, "\\\\").replace(/"/g, "\\\"")}"]`;
      if (document.querySelectorAll(selector).length === 1) return selector;
    }
    const parts = [];
    let current = element;
    while (current && current.nodeType === Node.ELEMENT_NODE && current !== document.body && parts.length < 6) {
      let part = current.tagName.toLowerCase();
      const parent = current.parentElement;
      if (parent) {
        const siblings = Array.from(parent.children).filter((child) => child.tagName === current.tagName);
        if (siblings.length > 1) part += `:nth-of-type(${siblings.indexOf(current) + 1})`;
      }
      parts.unshift(part);
      current = parent;
    }
    return parts.length ? parts.join(" > ") : element.tagName.toLowerCase();
  };
  const describe = (element, index) => {
    const rect = element.getBoundingClientRect();
    const tag = element.tagName.toLowerCase();
    const type = (element.getAttribute("type") || "").toLowerCase();
    const rawValue = "value" in element ? String(element.value || "") : "";
    return {
      index,
      tag,
      role: roleFor(element),
      type,
      name: nameFor(element).slice(0, 500),
      text: normalize(element.innerText || element.textContent).slice(0, 500),
      label: labelText(element).slice(0, 500),
      placeholder: normalize(element.getAttribute("placeholder")).slice(0, 500),
      selector: selectorFor(element),
      href: element.href || "",
      value: type === "password" ? "" : rawValue.slice(0, 500),
      checked: "checked" in element ? Boolean(element.checked) : undefined,
      disabled: Boolean(element.disabled || element.getAttribute("aria-disabled") === "true"),
      bounds: {
        x: Math.round(rect.left),
        y: Math.round(rect.top),
        width: Math.round(rect.width),
        height: Math.round(rect.height)
      }
    };
  };
  const query = [
    "a", "button", "input", "textarea", "select", "option", "summary",
    "[role]", "[aria-label]", "[placeholder]", "[contenteditable]",
    "[tabindex]", "label"
  ].join(",");
  const seen = new Set();
  const elements = [];
  for (const element of Array.from(document.querySelectorAll(query))) {
    if (seen.has(element) || !visible(element)) continue;
    seen.add(element);
    elements.push(describe(element, elements.length));
    if (elements.length >= maxElements) break;
  }
  return {
    title: document.title || "",
    url: location.href,
    text: normalize(document.body ? document.body.innerText : document.documentElement.innerText).slice(0, maxTextChars),
    element_count: elements.length,
    elements
  };
}
"""


RESOLVE_SCRIPT = r"""
(args) => {
  const normalize = (value) => (value || "").replace(/\s+/g, " ").trim();
  const lower = (value) => normalize(value).toLowerCase();
  const cssEscape = (value) => {
    if (window.CSS && CSS.escape) return CSS.escape(value);
    return String(value).replace(/[^A-Za-z0-9_-]/g, "\\$&");
  };
  const visible = (element) => {
    const rect = element.getBoundingClientRect();
    const style = window.getComputedStyle(element);
    return rect.width > 0 && rect.height > 0 &&
      style.visibility !== "hidden" && style.display !== "none" &&
      Number(style.opacity || "1") > 0;
  };
  const labelText = (element) => {
    if (element.id) {
      const label = document.querySelector(`label[for="${cssEscape(element.id)}"]`);
      if (label) return normalize(label.innerText || label.textContent);
    }
    const parentLabel = element.closest("label");
    return parentLabel ? normalize(parentLabel.innerText || parentLabel.textContent) : "";
  };
  const nameFor = (element) => {
    const aria = normalize(element.getAttribute("aria-label"));
    if (aria) return aria;
    const label = labelText(element);
    if (label) return label;
    const placeholder = normalize(element.getAttribute("placeholder"));
    if (placeholder) return placeholder;
    const title = normalize(element.getAttribute("title"));
    if (title) return title;
    const value = normalize(element.value);
    const tag = element.tagName.toLowerCase();
    const type = (element.getAttribute("type") || "").toLowerCase();
    if (tag === "input" && ["button", "submit", "reset"].includes(type) && value) return value;
    return normalize(element.innerText || element.textContent);
  };
  const roleFor = (element) => {
    const explicit = normalize(element.getAttribute("role"));
    if (explicit) return explicit;
    const tag = element.tagName.toLowerCase();
    const type = (element.getAttribute("type") || "").toLowerCase();
    if (tag === "button") return "button";
    if (tag === "a" && element.hasAttribute("href")) return "link";
    if (tag === "textarea") return "textbox";
    if (tag === "select") return "combobox";
    if (tag === "input") {
      if (["button", "submit", "reset"].includes(type)) return "button";
      if (type === "checkbox") return "checkbox";
      if (type === "radio") return "radio";
      return "textbox";
    }
    return "";
  };
  const selectorFor = (element) => {
    if (element.id) return `#${cssEscape(element.id)}`;
    const attrs = ["data-testid", "data-test", "data-qa", "name", "aria-label", "placeholder"];
    for (const attr of attrs) {
      const value = element.getAttribute(attr);
      if (!value) continue;
      const selector = `${element.tagName.toLowerCase()}[${attr}="${String(value).replace(/\\/g, "\\\\").replace(/"/g, "\\\"")}"]`;
      if (document.querySelectorAll(selector).length === 1) return selector;
    }
    const parts = [];
    let current = element;
    while (current && current.nodeType === Node.ELEMENT_NODE && current !== document.body && parts.length < 6) {
      let part = current.tagName.toLowerCase();
      const parent = current.parentElement;
      if (parent) {
        const siblings = Array.from(parent.children).filter((child) => child.tagName === current.tagName);
        if (siblings.length > 1) part += `:nth-of-type(${siblings.indexOf(current) + 1})`;
      }
      parts.unshift(part);
      current = parent;
    }
    return parts.length ? parts.join(" > ") : element.tagName.toLowerCase();
  };
  const editableSelector = [
    "input:not([type=button]):not([type=submit]):not([type=reset])",
    "textarea", "select", "[contenteditable]", "[role=textbox]"
  ].join(",");
  const allSelector = [
    "a", "button", "input", "textarea", "select", "option", "summary",
    "[role]", "[aria-label]", "[placeholder]", "[contenteditable]",
    "[tabindex]", "label"
  ].join(",");
  const selector = args.editable ? editableSelector : allSelector;
  const candidates = Array.from(document.querySelectorAll(selector)).filter(visible);
  let element = null;
  if (args.selector) element = document.querySelector(args.selector);
  if (!element && Number.isInteger(args.elementIndex)) element = candidates[args.elementIndex] || null;
  if (!element && args.label) {
    const wanted = lower(args.label);
    element = candidates.find((candidate) => lower(labelText(candidate)) === wanted) || null;
    if (!element) element = candidates.find((candidate) => lower(labelText(candidate)).includes(wanted)) || null;
  }
  if (!element && args.placeholder) {
    const wanted = lower(args.placeholder);
    element = candidates.find((candidate) => lower(candidate.getAttribute("placeholder")) === wanted) || null;
    if (!element) element = candidates.find((candidate) => lower(candidate.getAttribute("placeholder")).includes(wanted)) || null;
  }
  if (!element && args.name) {
    const wanted = lower(args.name);
    element = candidates.find((candidate) => lower(nameFor(candidate)) === wanted) || null;
  }
  if (!element && args.nameContains) {
    const wanted = lower(args.nameContains);
    element = candidates.find((candidate) => lower(nameFor(candidate)).includes(wanted)) || null;
  }
  if (!element && args.text) {
    const wanted = lower(args.text);
    element = candidates.find((candidate) => lower(candidate.innerText || candidate.textContent) === wanted) || null;
    if (!element) element = candidates.find((candidate) => lower(nameFor(candidate)) === wanted) || null;
  }
  if (!element && args.textContains) {
    const wanted = lower(args.textContains);
    element = candidates.find((candidate) => lower(candidate.innerText || candidate.textContent).includes(wanted) ||
      lower(nameFor(candidate)).includes(wanted)) || null;
  }
  if (!element) {
    return { found: false, error: "No matching WebView2 DOM element was found.", candidate_count: candidates.length };
  }
  element.scrollIntoView({ block: "center", inline: "center" });
  const rect = element.getBoundingClientRect();
  return {
    found: true,
    selector: selectorFor(element),
    element: {
      tag: element.tagName.toLowerCase(),
      role: roleFor(element),
      type: (element.getAttribute("type") || "").toLowerCase(),
      name: nameFor(element).slice(0, 500),
      text: normalize(element.innerText || element.textContent).slice(0, 500),
      label: labelText(element).slice(0, 500),
      placeholder: normalize(element.getAttribute("placeholder")).slice(0, 500),
      selector: selectorFor(element),
      bounds: {
        x: Math.round(rect.left),
        y: Math.round(rect.top),
        width: Math.round(rect.width),
        height: Math.round(rect.height)
      }
    },
    center: {
      x: Math.round(rect.left + rect.width / 2),
      y: Math.round(rect.top + rect.height / 2)
    }
  };
}
"""


SET_VALUE_SCRIPT = r"""
(args) => {
  const element = document.querySelector(args.selector);
  if (!element) return { success: false, error: "Element selector no longer resolves." };
  element.focus();
  if (element.isContentEditable) {
    element.textContent = args.value;
  } else if (element.tagName && element.tagName.toLowerCase() === "select") {
    element.value = args.value;
  } else if ("value" in element) {
    element.value = args.value;
  } else {
    return { success: false, error: "Element is not editable." };
  }
  element.dispatchEvent(new InputEvent("input", { bubbles: true, inputType: "insertText", data: args.value }));
  element.dispatchEvent(new Event("change", { bubbles: true }));
  return { success: true };
}
"""


def _normalize_action(value: Any) -> str:
    return re.sub(r"[\s_-]+", "", str(value or "webview2_list_targets").strip().lower())


def _normalize_endpoint(value: str) -> str:
    value = value.strip()
    if not value:
        return ""
    if not value.startswith(("http://", "https://")):
        value = "http://" + value
    parsed = urlparse(value)
    if not parsed.netloc:
        return value.rstrip("/")
    return f"{parsed.scheme}://{parsed.netloc}"


def _debug_endpoints(payload: dict[str, Any]) -> list[str]:
    endpoints: list[str] = []
    for key in ("debug_url", "cdp_url", "endpoint"):
        value = str(payload.get(key) or "").strip()
        if value:
            endpoints.append(_normalize_endpoint(value))

    host = str(payload.get("debug_host") or "127.0.0.1").strip() or "127.0.0.1"
    ports: list[int] = []
    raw_port = payload.get("debug_port")
    if raw_port not in (None, ""):
        try:
            ports.append(int(raw_port))
        except (TypeError, ValueError):
            pass
    raw_ports = payload.get("debug_ports")
    if isinstance(raw_ports, list):
        for item in raw_ports:
            try:
                ports.append(int(item))
            except (TypeError, ValueError):
                pass
    if payload.get("scan_ports") and not ports:
        start = int(payload.get("port_range_start") or 9222)
        end = int(payload.get("port_range_end") or 9232)
        start, end = max(1, min(start, end)), min(65535, max(start, end))
        ports.extend(range(start, min(end, start + 50) + 1))
    if not endpoints and not ports:
        ports.extend(DEFAULT_PORTS)
    for port in ports:
        if 1 <= port <= 65535:
            endpoints.append(f"http://{host}:{port}")

    unique: list[str] = []
    for endpoint in endpoints:
        endpoint = endpoint.rstrip("/")
        if endpoint and endpoint not in unique:
            unique.append(endpoint)
    return unique


def _fetch_json(url: str, timeout_seconds: float) -> Any:
    request = urllib.request.Request(url, headers={"Accept": "application/json"})
    with urllib.request.urlopen(request, timeout=timeout_seconds) as response:
        return json.loads(response.read().decode("utf-8", errors="replace"))


def _list_targets(payload: dict[str, Any]) -> dict[str, Any]:
    timeout = max(0.1, min(5.0, float(payload.get("connect_timeout_seconds") or 0.7)))
    include_non_pages = bool(payload.get("include_non_page_targets", False))
    endpoints = _debug_endpoints(payload)
    targets: list[dict[str, Any]] = []
    errors: list[dict[str, str]] = []
    for endpoint in endpoints:
        try:
            data = _fetch_json(endpoint + "/json/list", timeout)
        except Exception as exc:
            errors.append({"endpoint": endpoint, "error": str(exc)})
            continue
        if not isinstance(data, list):
            continue
        for item in data:
            if not isinstance(item, dict):
                continue
            target_type = str(item.get("type") or "")
            if not include_non_pages and target_type not in ("page", "webview"):
                continue
            targets.append({
                "index": len(targets),
                "endpoint": endpoint,
                "id": item.get("id") or "",
                "type": target_type,
                "title": item.get("title") or "",
                "url": item.get("url") or "",
                "description": item.get("description") or "",
                "web_socket_debugger_url": item.get("webSocketDebuggerUrl") or "",
            })
    return {
        "success": bool(targets),
        "action": "webview2_list_targets",
        "targets": targets,
        "endpoints_tried": endpoints,
        "errors": errors,
        "hint": "" if targets else (
            "No WebView2 CDP targets were found. Start the target app with "
            "WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=9222 before the WebView2 control is created, "
            "then call this action with debug_port=9222."
        ),
    }


def _target_matches(target: dict[str, Any], payload: dict[str, Any]) -> bool:
    target_id = str(payload.get("target_id") or "").strip()
    if target_id and str(target.get("id") or "") != target_id:
        return False
    url_contains = str(payload.get("target_url_contains") or payload.get("url_contains") or "").lower()
    if url_contains and url_contains not in str(target.get("url") or "").lower():
        return False
    title_contains = str(payload.get("target_title_contains") or payload.get("title_contains") or "").lower()
    if title_contains and title_contains not in str(target.get("title") or "").lower():
        return False
    return True


def _resolve_target(payload: dict[str, Any]) -> tuple[str, dict[str, Any], list[dict[str, Any]]]:
    listed = _list_targets(payload)
    targets = listed.get("targets") or []
    if not targets:
        raise RuntimeError(listed.get("hint") or "No WebView2 CDP targets were found.")
    if payload.get("target_index") not in (None, ""):
        index = int(payload.get("target_index"))
        if index < 0 or index >= len(targets):
            raise RuntimeError("target_index is out of range for webview2_list_targets.")
        return targets[index]["endpoint"], targets[index], targets
    for target in targets:
        if _target_matches(target, payload):
            return target["endpoint"], target, targets
    return targets[0]["endpoint"], targets[0], targets


async def _page_metadata(page: Any) -> dict[str, str]:
    try:
        title = await page.title()
    except Exception:
        title = ""
    return {"title": title, "url": page.url}


async def _select_page(browser: Any, target: dict[str, Any], payload: dict[str, Any]) -> Any:
    pages = []
    for context in browser.contexts:
        pages.extend(context.pages)
    if not pages:
        raise RuntimeError("Connected to CDP, but no pages were exposed by the WebView2 endpoint.")

    wanted_url = str(target.get("url") or "")
    wanted_title = str(target.get("title") or "")
    best_page = pages[0]
    for page in pages:
        meta = await _page_metadata(page)
        if wanted_url and meta["url"] == wanted_url:
            return page
        if wanted_title and meta["title"] == wanted_title:
            best_page = page
    title_filter = str(payload.get("target_title_contains") or payload.get("title_contains") or "").lower()
    url_filter = str(payload.get("target_url_contains") or payload.get("url_contains") or "").lower()
    for page in pages:
        meta = await _page_metadata(page)
        if title_filter and title_filter in meta["title"].lower():
            return page
        if url_filter and url_filter in meta["url"].lower():
            return page
    return best_page


async def _with_page(payload: dict[str, Any]):
    if async_playwright is None:
        missing = ", ".join(DEPENDENCY_ERRORS)
        raise RuntimeError(
            "Missing Python dependencies for WebView2 CDP automation: "
            f"{missing}. Run Setup System or install the Browser Web Search requirements."
        )
    endpoint, target, targets = _resolve_target(payload)
    timeout_ms = max(1000, min(120000, int(float(payload.get("timeout_seconds") or 10) * 1000)))
    playwright = await async_playwright().start()
    browser = None
    try:
        browser = await playwright.chromium.connect_over_cdp(endpoint, timeout=timeout_ms)
        page = await _select_page(browser, target, payload)
        try:
            await page.wait_for_load_state(str(payload.get("wait_until") or "domcontentloaded"), timeout=min(timeout_ms, 5000))
        except Exception:
            pass
        return playwright, browser, page, target, targets
    except Exception:
        await playwright.stop()
        raise


def _resolve_args(payload: dict[str, Any], editable: bool) -> dict[str, Any]:
    args = {
        "selector": payload.get("selector") or "",
        "editable": editable,
        "elementIndex": payload.get("element_index") if isinstance(payload.get("element_index"), int) else None,
        "name": payload.get("name") or "",
        "nameContains": payload.get("name_contains") or "",
        "text": payload.get("text") or "",
        "textContains": payload.get("text_contains") or "",
        "label": payload.get("label") or "",
        "placeholder": payload.get("placeholder") or "",
    }
    return args


async def _inspect(payload: dict[str, Any]) -> dict[str, Any]:
    playwright, browser, page, target, targets = await _with_page(payload)
    try:
        data = await page.evaluate(INSPECT_SCRIPT, {
            "maxElements": int(payload.get("max_elements") or 300),
            "maxTextChars": int(payload.get("max_text_chars") or 60000),
        })
        return {
            "success": True,
            "action": "webview2_inspect",
            "target": target,
            "target_count": len(targets),
            "page": data,
        }
    finally:
        await browser.close()
        await playwright.stop()


async def _click(payload: dict[str, Any]) -> dict[str, Any]:
    playwright, browser, page, target, _targets = await _with_page(payload)
    try:
        resolved = await page.evaluate(RESOLVE_SCRIPT, _resolve_args(payload, editable=False))
        if not resolved.get("found"):
            return {"success": False, "action": "webview2_click", "target": target, "error": resolved.get("error"), "details": resolved}
        if payload.get("prefer_js_click"):
            await page.evaluate("(selector) => document.querySelector(selector).click()", resolved["selector"])
            method = "js_click"
        else:
            center = resolved["center"]
            await page.mouse.click(center["x"], center["y"])
            method = "mouse"
        return {
            "success": True,
            "action": "webview2_click",
            "method": method,
            "target": target,
            "element": resolved.get("element"),
        }
    finally:
        await browser.close()
        await playwright.stop()


async def _set_text(payload: dict[str, Any], type_text: bool = False) -> dict[str, Any]:
    playwright, browser, page, target, _targets = await _with_page(payload)
    try:
        value = str(payload.get("value") or "")
        selector = str(payload.get("selector") or "").strip()
        if selector:
            try:
                locator = page.locator(selector).first()
                if type_text:
                    if payload.get("clear_existing", False):
                        await locator.press("Control+A")
                    await locator.type(value)
                else:
                    await locator.fill(value)
                element = {"selector": selector}
                method = "playwright_locator"
            except PlaywrightTimeoutError:
                raise
            except Exception:
                resolved = await page.evaluate(RESOLVE_SCRIPT, _resolve_args(payload, editable=True))
                if not resolved.get("found"):
                    return {"success": False, "action": "webview2_set_text", "target": target, "error": resolved.get("error"), "details": resolved}
                await page.evaluate(SET_VALUE_SCRIPT, {"selector": resolved["selector"], "value": value})
                element = resolved.get("element")
                method = "dom_value"
        else:
            resolved = await page.evaluate(RESOLVE_SCRIPT, _resolve_args(payload, editable=True))
            if not resolved.get("found"):
                return {"success": False, "action": "webview2_set_text", "target": target, "error": resolved.get("error"), "details": resolved}
            if type_text:
                center = resolved["center"]
                await page.mouse.click(center["x"], center["y"])
                if payload.get("clear_existing", False):
                    await page.keyboard.press("Control+A")
                await page.keyboard.type(value)
                method = "keyboard"
            else:
                await page.evaluate(SET_VALUE_SCRIPT, {"selector": resolved["selector"], "value": value})
                method = "dom_value"
            element = resolved.get("element")
        if payload.get("press_enter", False):
            await page.keyboard.press("Enter")
        return {
            "success": True,
            "action": "webview2_type_text" if type_text else "webview2_set_text",
            "method": method,
            "target": target,
            "element": element,
        }
    finally:
        await browser.close()
        await playwright.stop()


async def _run(payload: dict[str, Any]) -> dict[str, Any]:
    action = _normalize_action(payload.get("action"))
    if action == "webview2listtargets":
        return _list_targets(payload)
    if action == "webview2inspect":
        return await _inspect(payload)
    if action == "webview2click":
        return await _click(payload)
    if action == "webview2settext":
        return await _set_text(payload, type_text=False)
    if action == "webview2typetext":
        return await _set_text(payload, type_text=True)
    return {"success": False, "error": "Unsupported WebView2 action."}


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
