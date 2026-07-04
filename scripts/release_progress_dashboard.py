#!/usr/bin/env python3
"""Local read-only D1L release progress dashboard.

This server reads existing release-gate and hardware-validation artifacts and
renders progress bars. It never opens serial ports, flashes hardware, runs
firmware builds, or calls GitHub by default.
"""

from __future__ import annotations

import argparse
import html
import json
import os
import sys
import webbrowser
from dataclasses import dataclass
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse


CATEGORY_RULES = [
    ("UI", ("ui_", "com12_smoke", "manual_physical_ui_review")),
    ("SD", ("sd_", "rp2040", "official_seeed")),
    ("RF/DM", ("rf", "dm", "route")),
    ("Soak", ("soak",)),
    ("CI/Docs", ("ci_", "release_notices", "docs_")),
]


@dataclass
class DashboardConfig:
    root: Path
    host: str
    port: int
    refresh_sec: int
    open_browser: bool


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def rel(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return str(path)


def latest_file(directory: Path, pattern: str) -> Path | None:
    if not directory.is_dir():
        return None
    files = [path for path in directory.glob(pattern) if path.is_file()]
    if not files:
        return None
    return max(files, key=lambda path: path.stat().st_mtime)


def read_json(path: Path | None) -> dict[str, Any]:
    if not path:
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    return data if isinstance(data, dict) else {}


def gate_category(gate_id: str) -> str:
    for category, needles in CATEGORY_RULES:
        if any(needle in gate_id for needle in needles):
            return category
    return "Other"


def percent(done: int, total: int) -> int:
    return round((done / total) * 100) if total else 0


def gate_groups(gates: list[dict[str, Any]]) -> dict[str, dict[str, int]]:
    groups: dict[str, dict[str, int]] = {}
    for gate in gates:
        category = gate_category(str(gate.get("id") or ""))
        bucket = groups.setdefault(category, {"done": 0, "total": 0, "p0_done": 0, "p0_total": 0})
        bucket["total"] += 1
        if gate.get("ok") is True:
            bucket["done"] += 1
        if gate.get("severity") == "P0":
            bucket["p0_total"] += 1
            if gate.get("ok") is True:
                bucket["p0_done"] += 1
    return groups


def summarize_hardware_validation(root: Path) -> dict[str, Any]:
    path = latest_file(root / "artifacts" / "hardware", "d1l-autonomous-hardware-validation-*.json")
    data = read_json(path)
    if not data:
        return {"path": rel(path, root) if path else None, "present": False}
    runs = data.get("runs") if isinstance(data.get("runs"), list) else []
    return {
        "present": True,
        "path": rel(path, root) if path else None,
        "ok": data.get("ok"),
        "commit": data.get("commit"),
        "github_actions_run": data.get("github_actions_run"),
        "rp2040_uf2_flash": data.get("rp2040_uf2_flash"),
        "sd_suite_enabled": data.get("sd_suite_enabled"),
        "step_count": len(data.get("steps") or []),
        "passed_runs": sum(1 for run in runs if isinstance(run, dict) and run.get("ok") is True),
        "total_runs": len(runs),
    }


def build_status(root: Path) -> dict[str, Any]:
    gate_path = latest_file(root / "artifacts" / "release-gate", "release-gate-audit-*.json")
    gate_data = read_json(gate_path)
    gates = gate_data.get("gates") if isinstance(gate_data.get("gates"), list) else []
    gates = [gate for gate in gates if isinstance(gate, dict)]
    total = len(gates)
    done = sum(1 for gate in gates if gate.get("ok") is True)
    p0_gates = [gate for gate in gates if gate.get("severity") == "P0"]
    p0_done = sum(1 for gate in p0_gates if gate.get("ok") is True)
    failed = [gate for gate in gates if gate.get("ok") is not True]
    p0_failed = [gate for gate in failed if gate.get("severity") == "P0"]
    return {
        "generated_at": utc_now(),
        "root": str(root),
        "release_gate": {
            "path": rel(gate_path, root) if gate_path else None,
            "present": bool(gate_data),
            "commit": gate_data.get("commit"),
            "github_run_id": gate_data.get("github_run_id"),
            "ready_for_public_release": gate_data.get("ready_for_public_release"),
            "failed_count": gate_data.get("failed_count"),
            "p0_failed_count": gate_data.get("p0_failed_count"),
            "total": total,
            "done": done,
            "percent": percent(done, total),
            "p0_total": len(p0_gates),
            "p0_done": p0_done,
            "p0_percent": percent(p0_done, len(p0_gates)),
            "groups": gate_groups(gates),
            "failed": failed,
            "p0_failed": p0_failed,
        },
        "hardware_validation": summarize_hardware_validation(root),
    }


def css() -> str:
    return """
    :root { color-scheme: dark; --bg:#0b1117; --panel:#121b24; --text:#edf6ff; --muted:#8fa2b4; --line:#243546; --ok:#12d18e; --bad:#ff5d73; --warn:#f5c451; --accent:#3fb7ff; }
    * { box-sizing: border-box; }
    body { margin:0; font:14px/1.45 system-ui, Segoe UI, sans-serif; background:var(--bg); color:var(--text); }
    header { display:flex; justify-content:space-between; align-items:flex-start; gap:24px; padding:22px 26px; border-bottom:1px solid var(--line); background:#0e1620; }
    h1 { margin:0 0 4px; font-size:24px; font-weight:700; }
    h2 { margin:0 0 12px; font-size:16px; }
    main { padding:22px 26px 36px; display:grid; gap:18px; }
    .grid { display:grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap:16px; }
    .panel { background:var(--panel); border:1px solid var(--line); border-radius:8px; padding:16px; }
    .muted { color:var(--muted); }
    .pill { display:inline-flex; align-items:center; height:26px; padding:0 10px; border-radius:999px; font-weight:700; background:#203142; color:var(--text); }
    .pill.ok { background:#11382d; color:#b8ffe4; }
    .pill.bad { background:#421923; color:#ffd5dc; }
    .bar { height:14px; border-radius:999px; overflow:hidden; background:#243242; border:1px solid #32485c; }
    .fill { height:100%; width:0; background:linear-gradient(90deg,var(--accent),var(--ok)); }
    .fill.bad { background:linear-gradient(90deg,var(--warn),var(--bad)); }
    .metric { display:grid; gap:8px; margin:12px 0; }
    .metric-row { display:flex; justify-content:space-between; gap:12px; }
    table { width:100%; border-collapse:collapse; }
    th, td { padding:9px 8px; border-bottom:1px solid var(--line); text-align:left; vertical-align:top; }
    th { color:#b6c9d9; font-weight:700; }
    .status-ok { color:var(--ok); font-weight:700; }
    .status-bad { color:var(--bad); font-weight:700; }
    code { color:#d5ecff; background:#0a141d; border:1px solid #1f3446; padding:1px 5px; border-radius:4px; }
    a { color:#8ed7ff; }
    """


def esc(value: Any) -> str:
    return html.escape("" if value is None else str(value))


def progress_block(label: str, done: int, total: int, danger: bool = False) -> str:
    value = percent(done, total)
    return (
        f'<div class="metric"><div class="metric-row"><strong>{esc(label)}</strong>'
        f'<span>{done}/{total} ({value}%)</span></div>'
        f'<div class="bar"><div class="fill {"bad" if danger else ""}" style="width:{value}%"></div></div></div>'
    )


def render_html(status: dict[str, Any], refresh_sec: int) -> str:
    gate = status["release_gate"]
    hw = status["hardware_validation"]
    ready = gate.get("ready_for_public_release") is True
    failed_rows = []
    for item in gate.get("p0_failed") or []:
        evidence = item.get("evidence") if isinstance(item.get("evidence"), list) else []
        failed_rows.append(
            "<tr>"
            f"<td><code>{esc(item.get('id'))}</code></td>"
            f"<td>{esc(item.get('title'))}<br><span class='muted'>{esc(item.get('message'))}</span></td>"
            f"<td>{esc(item.get('severity'))}</td>"
            f"<td>{esc(', '.join(str(path) for path in evidence[:2]))}</td>"
            "</tr>"
        )
    group_blocks = []
    for name, bucket in sorted(gate.get("groups", {}).items()):
        group_blocks.append(progress_block(name, bucket["done"], bucket["total"]))
        if bucket["p0_total"]:
            group_blocks.append(progress_block(f"{name} P0", bucket["p0_done"], bucket["p0_total"], danger=bucket["p0_done"] < bucket["p0_total"]))
    html_body = f"""<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="refresh" content="{refresh_sec}">
<title>D1L Release Progress</title><style>{css()}</style></head>
<body>
<header>
  <div>
    <h1>MeshCore DeskOS D1L Release Progress</h1>
    <div class="muted">Read-only local dashboard. Refreshes every {refresh_sec}s. Generated {esc(status.get('generated_at'))}.</div>
  </div>
  <span class="pill {'ok' if ready else 'bad'}">{'READY' if ready else 'NOT READY'}</span>
</header>
<main>
  <section class="grid">
    <div class="panel">
      <h2>Release Gate</h2>
      <div class="muted">Artifact: <code>{esc(gate.get('path') or 'missing')}</code></div>
      <div class="muted">Commit: <code>{esc(gate.get('commit') or 'unknown')}</code> &nbsp; Run: <code>{esc(gate.get('github_run_id') or 'unknown')}</code></div>
      {progress_block('All Gates', gate.get('done') or 0, gate.get('total') or 0)}
      {progress_block('P0 Gates', gate.get('p0_done') or 0, gate.get('p0_total') or 0, danger=(gate.get('p0_failed_count') or 0) > 0)}
      <div><span class="pill bad">{esc(gate.get('p0_failed_count'))} P0 failed</span> <span class="pill">{esc(gate.get('failed_count'))} total failed</span></div>
    </div>
    <div class="panel">
      <h2>Latest Hardware Validation</h2>
      <div class="muted">Artifact: <code>{esc(hw.get('path') or 'missing')}</code></div>
      <p>Status: <span class="{'status-ok' if hw.get('ok') is True else 'status-bad'}">{esc(hw.get('ok'))}</span></p>
      <p>Runs: {esc(hw.get('passed_runs'))}/{esc(hw.get('total_runs'))} passed</p>
      <p>RP2040 UF2 flash: <code>{esc(hw.get('rp2040_uf2_flash'))}</code> &nbsp; SD suite: <code>{esc(hw.get('sd_suite_enabled'))}</code></p>
    </div>
  </section>
  <section class="grid">{''.join(f'<div class="panel">{block}</div>' for block in group_blocks)}</section>
  <section class="panel">
    <h2>Open P0 Evidence Gates</h2>
    <table><thead><tr><th>Gate</th><th>Issue</th><th>Severity</th><th>Evidence</th></tr></thead><tbody>
    {''.join(failed_rows) if failed_rows else '<tr><td colspan="4">No failed P0 gates.</td></tr>'}
    </tbody></table>
  </section>
</main>
</body></html>"""
    return html_body


class DashboardHandler(BaseHTTPRequestHandler):
    config: DashboardConfig

    def log_message(self, format: str, *args: Any) -> None:
        sys.stderr.write("%s - %s\n" % (self.address_string(), format % args))

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        status = build_status(self.config.root)
        if parsed.path == "/api/status":
            payload = json.dumps(status, indent=2).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
            return
        if parsed.path in {"/", "/index.html"}:
            payload = render_html(status, self.config.refresh_sec).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
            return
        self.send_error(404, "Not found")


def serve(config: DashboardConfig) -> None:
    DashboardHandler.config = config
    server = ThreadingHTTPServer((config.host, config.port), DashboardHandler)
    url = f"http://{config.host}:{config.port}/"
    print(f"Serving D1L release dashboard at {url}")
    print("Read-only: no serial, no flashing, no builds.")
    if config.open_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        server.server_close()


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=Path(__file__).resolve().parents[1])
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=int(os.environ.get("D1L_PROGRESS_PORT", "8765")))
    parser.add_argument("--refresh-sec", type=int, default=10)
    parser.add_argument("--open", action="store_true", help="Open the dashboard in the default browser.")
    parser.add_argument("--once-json", action="store_true", help="Print one JSON snapshot and exit.")
    parser.add_argument("--once-html", action="store_true", help="Print one HTML snapshot and exit.")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    config = DashboardConfig(
        root=Path(args.root).resolve(),
        host=args.host,
        port=args.port,
        refresh_sec=max(2, args.refresh_sec),
        open_browser=args.open,
    )
    status = build_status(config.root)
    if args.once_json:
        print(json.dumps(status, indent=2))
        return 0
    if args.once_html:
        print(render_html(status, config.refresh_sec))
        return 0
    serve(config)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
