#!/usr/bin/env python3
"""Host-only checks; not an Android build or ARM64/Dobby device test."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("analyze_handoff", root / "tools/analyze_handoff.py")
analyzer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analyzer)
with tempfile.TemporaryDirectory(prefix="gibox-handoff-") as tmp:
    tmp = Path(tmp)
    (tmp / "dobby.h").write_text('extern "C" int DobbyHook(void*, void*, void**);\n')
    exe = tmp / "handoff_test"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread", "-DGIBOX_HANDOFF_DIAGNOSTICS=1", "-DGIBOX_HOST_TEST",
                    "-I" + str(tmp), "-I" + str(root / "app/src/main/cpp"),
                    str(root / "tests/handoff_test.cpp"), "-o", str(exe)], check=True)
    for mode in ("mismatch", "partial", "badlog", "normal"):
        log = tmp / (mode + ".jsonl")
        if mode == "badlog":
            log.mkdir()  # A directory cannot be opened as the JSONL file.
        subprocess.run([str(exe), mode, str(log)], check=True)
        if mode != "normal":
            continue
        events = [json.loads(line) for line in log.read_text().splitlines()]
        snaps = [e for e in events if e["kind"] == "snapshot"]
        assert len(snaps) == 6, snaps
        assert {s["frame"] for s in snaps} == {1024}
        assert any(s["phase"] == "instantiate_return" and s["renderers"][0]["mesh"] != "0x0" for s in snaps)
        report = analyzer.summarize(events)
        assert report["first_draw_proven"] is False
        assert report["all_snapshot_tids_match_tick"] is True
        assert report["handoffs"][0]["renderers"][0]["mesh_changed"] is True
        assert report["handoffs"][0]["renderers"][0]["material_elements_changed"] is False
        assert report["handoffs"][0]["renderers"][0]["completed_external_setter_calls"] == [1]
        assert {e["source"] for e in events if e["kind"] == "mesh_setter"} == {"external", "gibox"}
        assert any(e["kind"] == "skip" and e.get("reason") == "not_verified_tick_thread" for e in events)
        assert len({e["seq"] for e in events}) == len(events)
        assert len([e for e in events if e["kind"] == "probe_error"]) == 2
        last = max(events, key=lambda e: e["seq"])
        assert last["foreign_roots"] == 1 and last["empty_spawns"] == 1, last
        pooled = dict(next(s for s in snaps if s["phase"] == "activate_enter"))
        pooled["seq"] = max(e["seq"] for e in events) + 100
        pooled_report = analyzer.summarize(events + [pooled])
        assert pooled_report["handoffs"][-1]["reference_phase"] is None
        old_events = [dict(e, capture_start_ns=1) for e in events]
        report = analyzer.summarize(old_events + events)
        assert report["snapshot_count"] == 6 and any("Multiple captures" in w for w in report["warnings"])
print("handoff log semantics PASS")
