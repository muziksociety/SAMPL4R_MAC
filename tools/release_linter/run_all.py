#!/usr/bin/env python3
"""
SM5 Release Linter — Unified runner.

Executes all 5 checks, collects their JSON reports, and emits:
  - combined_report.json      (single machine-readable report)
  - release_linter.dot        (Obsidian-style vault map, Graphviz DOT)
  - release_linter.html       (self-contained HTML summary, no server needed)

Exit code:
    0 = all checks clean
    1 = at least one check failed (drift detected)
    2 = tool error (a check script crashed)

Usage:
    python tools/release_linter/run_all.py
    python tools/release_linter/run_all.py --out-dir reports/
"""
from __future__ import annotations
import argparse
import json
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path

LINTER_DIR = Path(__file__).resolve().parent
REPO_ROOT = LINTER_DIR.parents[1]

CHECKS = [
    {
        "id": "sources",
        "title": "Source/CMake drift",
        "script": LINTER_DIR / "check_sources.py",
        "summary": "Detects .cpp files missing from CMakeLists.txt target_sources",
    },
    {
        "id": "engines",
        "title": "Engine registry sync",
        "script": LINTER_DIR / "check_engines.py",
        "summary": "Detects IVariationEngine subclasses missing from backend or UI",
    },
    {
        "id": "installer",
        "title": "Installer manifest sync",
        "script": LINTER_DIR / "check_installer.py",
        "summary": "Detects runtime resources referenced in code but not shipped by installer",
    },
    {
        "id": "events",
        "title": "Event bus topic sync",
        "script": LINTER_DIR / "check_events.py",
        "summary": "Detects topics with publishers but no subscribers (or vice versa)",
    },
    {
        "id": "services",
        "title": "IService registration sync",
        "script": LINTER_DIR / "check_services.py",
        "summary": "Detects IService subclasses that are never registered",
    },
    {
        "id": "runtime_deps",
        "title": "Runtime DLL dependency walker",
        "script": LINTER_DIR / "check_runtime_deps.py",
        "summary": "Verifies every DLL the exe imports is shipped or a fresh-Windows system DLL",
    },
    {
        "id": "hardcoded_paths",
        "title": "Hardcoded dev-path scanner",
        "script": LINTER_DIR / "check_hardcoded_paths.py",
        "summary": "Detects absolute dev-machine paths in source or baked into the exe",
    },
    {
        "id": "staging_sync",
        "title": "Staging directory sync",
        "script": LINTER_DIR / "check_staging_sync.py",
        "summary": "Verifies SM5_Portable/ binaries match the build output (prevents shipping stale exes)",
    },
]


def run_one(check: dict, out_dir: Path) -> dict:
    """Run a single check and return {status, exit_code, report}."""
    json_path = out_dir / f"{check['id']}.json"
    proc = subprocess.run(
        [sys.executable, str(check["script"]), "--quiet", "--json", str(json_path)],
        capture_output=True,
        text=True,
    )
    report: dict = {}
    if json_path.exists():
        try:
            report = json.loads(json_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            pass
    status = "clean" if proc.returncode == 0 else ("drift" if proc.returncode == 1 else "error")
    return {
        "id": check["id"],
        "title": check["title"],
        "summary": check["summary"],
        "exit_code": proc.returncode,
        "status": status,
        "report": report,
        "stderr": proc.stderr,
    }


def count_findings(result: dict) -> int:
    r = result.get("report", {})
    if not r:
        return 0
    n = 0
    for key in ("orphans", "phantoms", "missing_backend", "missing_ui",
                "missing_dirs", "missing_bins", "published_only",
                "subscribed_only", "missing",
                "source_findings", "binary_findings"):
        v = r.get(key)
        if isinstance(v, list):
            n += len(v)
    return n


def render_dot(results: list[dict], path: Path) -> None:
    """Emit an Obsidian-style vault map as Graphviz DOT."""
    lines = [
        "digraph sm5_release {",
        '  rankdir=LR;',
        '  graph [label="SM5 Release Linter — Vault Map", labelloc=t, fontsize=20, fontname="Helvetica", bgcolor="#1e1e1e", fontcolor="#ffffff"];',
        '  node [shape=box, style="rounded,filled", fontname="Helvetica", fontcolor="#ffffff"];',
        '  edge [color="#888888", fontcolor="#aaaaaa"];',
        '  root [label="SM5 Release", shape=doublecircle, fillcolor="#4a90e2"];',
    ]
    for result in results:
        node_id = result["id"]
        title = result["title"].replace('"', '\\"')
        n = count_findings(result)
        if result["status"] == "clean":
            color = "#2ecc71"  # green
            sublabel = "clean"
        elif result["status"] == "drift":
            color = "#e74c3c"  # red
            sublabel = f"{n} finding(s)"
        else:
            color = "#f39c12"  # orange
            sublabel = "error"
        lines.append(f'  {node_id} [label="{title}\\n({sublabel})", fillcolor="{color}"];')
        lines.append(f'  root -> {node_id};')

        # Drill-down: list actual findings as leaf nodes
        report = result.get("report", {})
        findings: list[str] = []
        for key in ("orphans", "phantoms", "missing_backend", "missing_ui",
                    "missing_dirs", "missing_bins", "published_only",
                    "subscribed_only", "missing"):
            items = report.get(key, [])
            if isinstance(items, list):
                for item in items[:8]:  # cap per-bucket
                    findings.append(f"{key}: {item}")
        for i, finding in enumerate(findings[:12]):
            leaf_id = f"{node_id}_{i}"
            safe = re.sub(r'[^\w: /.\-]', '_', finding)[:80]
            lines.append(f'  {leaf_id} [label="{safe}", shape=note, fillcolor="#555555", fontsize=10];')
            lines.append(f'  {node_id} -> {leaf_id};')
    lines.append("}")
    path.write_text("\n".join(lines), encoding="utf-8")


def render_html(results: list[dict], combined: dict, path: Path) -> None:
    """
    Emit an interactive, Obsidian-style force-directed graph using vis-network
    (loaded from CDN). Nodes: root + 5 checks + findings + scanned entities.
    Edges: containment. Physics enabled — drag nodes, zoom, hover highlights.
    """
    COLORS = {
        "root":    {"bg": "#4a90e2", "border": "#2c5d8f"},
        "clean":   {"bg": "#2ecc71", "border": "#1e8449"},
        "drift":   {"bg": "#e74c3c", "border": "#922b21"},
        "error":   {"bg": "#f39c12", "border": "#9a6208"},
        "finding": {"bg": "#8e44ad", "border": "#5b2c6f"},
        "entity":  {"bg": "#7f8c8d", "border": "#4d5656"},
    }

    nodes: list[dict] = []
    edges: list[dict] = []

    # Root node
    nodes.append({
        "id": "root",
        "label": "SM5 Release",
        "shape": "dot",
        "size": 38,
        "color": COLORS["root"],
        "font": {"color": "#ffffff", "size": 20, "face": "Inter, sans-serif"},
        "title": f"Overall: {combined['overall_status'].upper()} — {combined['total_findings']} findings",
    })

    for r in results:
        check_id = f"check_{r['id']}"
        status = r["status"]
        n = count_findings(r)
        color = COLORS.get(status, COLORS["error"])
        label = r["title"]
        if n > 0:
            label += f"\n({n})"

        nodes.append({
            "id": check_id,
            "label": label,
            "shape": "dot",
            "size": 26,
            "color": color,
            "font": {"color": "#ffffff", "size": 15, "multi": True, "face": "Inter, sans-serif"},
            "title": f"{r['title']}: {status.upper()} — {r['summary']}",
        })
        edges.append({"from": "root", "to": check_id, "color": {"color": "#666"}, "width": 2})

        report = r.get("report", {})

        # Emit scanned-entity nodes so the graph has structure even when clean
        if r["id"] == "engines":
            for engine in report.get("results", []):
                eid = f"engine_{engine['name']}"
                ok = engine.get("status") == "OK"
                nodes.append({
                    "id": eid,
                    "label": engine["name"],
                    "shape": "box",
                    "color": COLORS["clean"] if ok else COLORS["drift"],
                    "font": {"color": "#ffffff", "size": 11},
                    "title": f"Backend: {engine.get('backend')} · UI surfaces: {len(engine.get('ui_surfaces', []))}",
                })
                edges.append({"from": check_id, "to": eid, "color": {"color": "#555"}, "width": 1})
        elif r["id"] == "services":
            for svc in report.get("results", []):
                sid = f"svc_{svc['name']}"
                status_col = COLORS["clean"] if svc["status"].startswith("OK") else (
                    COLORS["entity"] if svc["status"] in ("OWNED", "ALLOWED") else COLORS["drift"]
                )
                nodes.append({
                    "id": sid,
                    "label": svc["name"],
                    "shape": "box",
                    "color": status_col,
                    "font": {"color": "#ffffff", "size": 11},
                    "title": f"{svc['status']} · app={svc['standalone']} plugin={svc['plugin']}",
                })
                edges.append({"from": check_id, "to": sid, "color": {"color": "#555"}, "width": 1})
        elif r["id"] == "events":
            for topic in report.get("matched_topics", [])[:20]:
                tid = f"topic_{topic}"
                nodes.append({
                    "id": tid,
                    "label": topic,
                    "shape": "ellipse",
                    "color": COLORS["clean"],
                    "font": {"color": "#ffffff", "size": 10},
                    "title": f"Topic: {topic} (matched)",
                })
                edges.append({"from": check_id, "to": tid, "color": {"color": "#555"}, "width": 1})

        # Emit finding leaf nodes (drift items)
        for key in ("orphans", "phantoms", "missing_backend", "missing_ui",
                    "missing_dirs", "missing_bins", "published_only",
                    "subscribed_only", "missing"):
            items = report.get(key, [])
            if not isinstance(items, list):
                continue
            for item in items[:15]:
                fid = f"finding_{check_id}_{re.sub(r'[^A-Za-z0-9]', '_', str(item))}"[:120]
                nodes.append({
                    "id": fid,
                    "label": str(item),
                    "shape": "box",
                    "color": COLORS["finding"],
                    "font": {"color": "#ffffff", "size": 10},
                    "title": f"{key}: {item}",
                })
                edges.append({"from": check_id, "to": fid, "color": {"color": "#c0392b"}, "width": 2, "dashes": True})

    nodes_json = json.dumps(nodes)
    edges_json = json.dumps(edges)

    overall_class = "pass" if combined['overall_status'] == "clean" else "fail"
    gen_time = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    n_findings = combined['total_findings']
    n_checks = len(results)

    html = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>SM5 Release Linter — Vault Map</title>
<script type="text/javascript" src="https://unpkg.com/vis-network@9.1.9/standalone/umd/vis-network.min.js"></script>
<style>
  html, body {{ margin: 0; padding: 0; height: 100%; background: #1a1a1a; color: #eee;
                font-family: Inter, -apple-system, 'Segoe UI', sans-serif; overflow: hidden; }}
  #header {{ position: fixed; top: 0; left: 0; right: 0; padding: 14px 24px;
             background: linear-gradient(180deg, rgba(26,26,26,0.95), rgba(26,26,26,0.6));
             z-index: 10; display: flex; align-items: center; gap: 20px; }}
  #header h1 {{ margin: 0; font-size: 18px; font-weight: 600; letter-spacing: 0.3px; }}
  #header .meta {{ font-size: 12px; color: #888; }}
  .badge {{ padding: 4px 12px; border-radius: 20px; font-size: 12px; font-weight: 700;
            letter-spacing: 0.5px; }}
  .badge.pass {{ background: #2ecc71; color: #0f3e1e; }}
  .badge.fail {{ background: #e74c3c; color: #3b0f0a; }}
  #graph {{ position: absolute; top: 0; left: 0; right: 0; bottom: 0; }}
  #legend {{ position: fixed; bottom: 16px; left: 16px; background: rgba(0,0,0,0.6);
             padding: 12px 16px; border-radius: 8px; font-size: 12px;
             backdrop-filter: blur(6px); z-index: 10; }}
  #legend .item {{ display: flex; align-items: center; gap: 8px; margin: 4px 0; }}
  #legend .dot {{ width: 10px; height: 10px; border-radius: 50%; }}
  #help {{ position: fixed; bottom: 16px; right: 16px; background: rgba(0,0,0,0.6);
           padding: 10px 14px; border-radius: 8px; font-size: 11px; color: #aaa;
           backdrop-filter: blur(6px); z-index: 10; }}
</style>
</head>
<body>
  <div id="header">
    <h1>SM5 Release Linter — Vault Map</h1>
    <span class="badge {overall_class}">{combined['overall_status'].upper()}</span>
    <span class="meta">{n_findings} findings · {n_checks} checks · {gen_time}</span>
  </div>

  <div id="graph"></div>

  <div id="legend">
    <div class="item"><div class="dot" style="background:#4a90e2"></div>Root</div>
    <div class="item"><div class="dot" style="background:#2ecc71"></div>Clean check / passing entity</div>
    <div class="item"><div class="dot" style="background:#e74c3c"></div>Drift / failing check</div>
    <div class="item"><div class="dot" style="background:#8e44ad"></div>Finding (dashed edge)</div>
    <div class="item"><div class="dot" style="background:#7f8c8d"></div>Owned / allowlisted</div>
  </div>

  <div id="help">drag to pan · scroll to zoom · click to focus · hover for details</div>

  <script>
    const nodes = new vis.DataSet({nodes_json});
    const edges = new vis.DataSet({edges_json});

    const container = document.getElementById('graph');
    const data = {{ nodes, edges }};
    const options = {{
      physics: {{
        enabled: true,
        solver: 'forceAtlas2Based',
        forceAtlas2Based: {{
          gravitationalConstant: -60,
          centralGravity: 0.008,
          springLength: 120,
          springConstant: 0.08,
          damping: 0.5,
          avoidOverlap: 0.6
        }},
        stabilization: {{ iterations: 400, fit: true }}
      }},
      interaction: {{
        hover: true,
        tooltipDelay: 150,
        zoomView: true,
        dragView: true,
        navigationButtons: false
      }},
      nodes: {{
        borderWidth: 2,
        shadow: {{ enabled: true, color: 'rgba(0,0,0,0.5)', size: 10 }}
      }},
      edges: {{
        smooth: {{ type: 'continuous' }},
        arrows: {{ to: {{ enabled: false }} }},
        selectionWidth: 3
      }}
    }};
    const network = new vis.Network(container, data, options);

    // Click-to-focus: highlight node's neighborhood
    network.on('click', function(params) {{
      if (params.nodes.length > 0) {{
        const nodeId = params.nodes[0];
        network.focus(nodeId, {{ scale: 1.2, animation: {{ duration: 400 }} }});
      }}
    }});
  </script>
</body>
</html>"""
    path.write_text(html, encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description="SM5 release linter — unified runner")
    ap.add_argument("--out-dir", type=Path, default=LINTER_DIR / "reports",
                    help="Directory to write combined reports")
    args = ap.parse_args()

    out_dir: Path = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    results = [run_one(c, out_dir) for c in CHECKS]

    total_findings = sum(count_findings(r) for r in results)
    all_clean = all(r["status"] == "clean" for r in results)
    any_error = any(r["status"] == "error" for r in results)
    overall = "clean" if all_clean else ("error" if any_error else "drift")

    combined = {
        "generated_at": datetime.now().isoformat(),
        "overall_status": overall,
        "total_findings": total_findings,
        "checks": results,
    }

    # Write outputs
    combined_path = out_dir / "combined_report.json"
    dot_path = out_dir / "release_linter.dot"
    html_path = out_dir / "release_linter.html"

    combined_path.write_text(json.dumps(combined, indent=2, default=str), encoding="utf-8")
    render_dot(results, dot_path)
    render_html(results, combined, html_path)

    # Console summary
    print("=" * 70)
    print("SM5 RELEASE LINTER — Unified Report")
    print("=" * 70)
    for r in results:
        n = count_findings(r)
        marker = "OK " if r["status"] == "clean" else ("!! " if r["status"] == "drift" else "?? ")
        print(f"  {marker}{r['title']:<35} {r['status']:<8} ({n} finding(s))")
    print()
    print(f"OVERALL: {overall.upper()}  —  {total_findings} total finding(s)")
    print()
    print(f"  JSON: {combined_path}")
    print(f"  DOT:  {dot_path}")
    print(f"  HTML: {html_path}")
    print("=" * 70)

    return 0 if all_clean else (2 if any_error else 1)


if __name__ == "__main__":
    sys.exit(main())
