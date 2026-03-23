from __future__ import annotations

import argparse
import html
import os
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from .common import fail, log
from .csvio import append_csv_row, write_csv_header
from .fs import normalize_outdir, prune_models_cache, reset_paths
from .orchestration import RunnerOrchestrator
from .reports import run_optional_aggregate
from .summary import parse_summary_key_last, write_summary_env

SCRIPT_DIR = Path(__file__).resolve().parent.parent
RUNNER = SCRIPT_DIR / "smoke_runner.py"
RUNNER_PYTHON = sys.executable or "python3"
AGGREGATE = SCRIPT_DIR / "generate_smoke_aggregate_report.py"
ORCH = RunnerOrchestrator(runner_path=RUNNER, runner_python=RUNNER_PYTHON)

SUITE_RESULT_HEADER = [
    "step",
    "group",
    "kind",
    "status",
    "rc",
    "duration_sec",
    "outdir",
    "summary_status",
    "stdout_log",
    "command_file",
]

SANITY_STEPS = (
    "framework-self-check",
    "lan-helo-lobby-15p",
    "lan-helo-gameplay-4p",
    "join-leave-churn-ready-6p",
    "inventory-fast-pass",
    "splitscreen-cap",
    "mapgen-integration-preflight",
)

RELEASE_STEPS = (
    "framework-self-check",
    "lan-helo-lobby-15p",
    "lan-helo-gameplay-4p",
    "helo-adversarial-4p",
    "join-leave-churn-ready-6p",
    "save-reload-compat",
    "status-effect-queue-init",
    "inventory-fast-pass",
    "lobby-slot-lock-kick-copy",
    "lobby-page-navigation",
    "remote-combat-slot-bounds",
    "splitscreen-baseline",
    "splitscreen-cap",
    "mapgen-integration-preflight",
    "mapgen-level-matrix-sim",
)

FULL_ONLY_STEPS = (
    "helo-soak",
    "lobby-kick-target",
    "mapgen-sweep-full-lobby",
)


@dataclass(frozen=True)
class SuiteStep:
    name: str
    group: str
    kind: str
    outdir: Path
    cmd: list[str]
    summary_path: Path | None = None
    extra_env: dict[str, str] | None = None
    post_run: Callable[[int], None] | None = None
    cwd: Path | None = None


def resolve_release_suite_step_names(profile: str) -> tuple[str, ...]:
    if profile == "sanity":
        return SANITY_STEPS
    if profile == "release":
        return RELEASE_STEPS
    if profile == "full":
        return (*RELEASE_STEPS, *FULL_ONLY_STEPS)
    fail(f"unsupported release-suite profile: {profile}")
    raise AssertionError("unreachable")


def resolve_release_suite_platform(platform_name: str) -> str:
    if platform_name != "auto":
        return platform_name
    if sys.platform == "darwin":
        return "macos"
    if sys.platform.startswith("win"):
        return "windows"
    fail("release-suite auto platform detection supports only macOS and Windows; pass --platform explicitly")
    raise AssertionError("unreachable")


def _step_group(step_name: str) -> str:
    if step_name.startswith("mapgen-"):
        return "mapgen"
    return "lanes"


def _suite_auto_start_delay(platform_name: str) -> int:
    if platform_name == "windows":
        return 2
    return 2


def _display_args(ns: argparse.Namespace, *, include_stagger: bool = True) -> list[str]:
    args = ["--size", ns.size]
    if include_stagger:
        args.extend(["--stagger", str(ns.stagger)])
    return args


def _runner_step(
    ns: argparse.Namespace,
    *,
    name: str,
    outdir: Path,
    lane: str,
    lane_args: list[str],
    expect_summary: bool = True,
) -> SuiteStep:
    return SuiteStep(
        name=name,
        group=_step_group(name),
        kind="lane",
        outdir=outdir,
        cmd=ORCH.build_nested_runner_cmd(
            lane=lane,
            app=ns.app,
            datadir=ns.datadir,
            lane_outdir=outdir,
            lane_args=lane_args,
        ),
        summary_path=(outdir / "summary.env") if expect_summary else None,
    )


def _framework_self_check_step(outdir: Path) -> SuiteStep:
    return SuiteStep(
        name="framework-self-check",
        group="lanes",
        kind="lane",
        outdir=outdir,
        cmd=ORCH.build_runner_subcommand_prefix("framework-self-check"),
        summary_path=None,
    )


def _build_mapgen_integration_cmd(
    ns: argparse.Namespace,
    *,
    outdir: Path,
) -> list[str]:
    csv_path = outdir / "mapgen_level_matrix.csv"
    cmd = [
        str(ns.app),
        "-windowed",
        f"-size={ns.size}",
        "-nosound",
    ]
    if ns.datadir:
        cmd.append(f"-datadir={ns.datadir}")
    cmd.extend(
        [
            "-smoke-mapgen-integration",
            f"-smoke-mapgen-integration-csv={csv_path}",
            f"-smoke-mapgen-integration-levels={ns.mapgen_levels}",
            f"-smoke-mapgen-integration-min-players={ns.mapgen_min_players}",
            f"-smoke-mapgen-integration-max-players={ns.mapgen_max_players}",
            f"-smoke-mapgen-integration-runs={ns.mapgen_integration_runs}",
        ]
    )
    return cmd


def _mapgen_integration_post_run(ns: argparse.Namespace, outdir: Path) -> Callable[[int], None]:
    def _post_run(rc: int) -> None:
        csv_path = outdir / "mapgen_level_matrix.csv"
        report_path = outdir / "mapgen_integration_aggregate_report.html"
        result = "pass" if rc == 0 and csv_path.is_file() else "fail"
        run_optional_aggregate(AGGREGATE, report_path, ["--mapgen-matrix-csv", str(csv_path)])
        write_summary_env(
            outdir / "summary.env",
            {
                "RESULT": result,
                "OUTDIR": outdir,
                "APP": ns.app,
                "DATADIR": ns.datadir or "",
                "CSV_PATH": csv_path,
                "AGGREGATE_HTML_PATH": report_path,
                "LEVELS": ns.mapgen_levels,
                "MIN_PLAYERS": ns.mapgen_min_players,
                "MAX_PLAYERS": ns.mapgen_max_players,
                "RUNS_PER_PLAYER": ns.mapgen_integration_runs,
                "RETURN_CODE": rc,
            },
        )

    return _post_run


def _mapgen_integration_step(ns: argparse.Namespace, *, outdir: Path) -> SuiteStep:
    home_dir = outdir / "home"
    return SuiteStep(
        name="mapgen-integration-preflight",
        group="mapgen",
        kind="app",
        outdir=outdir,
        cmd=_build_mapgen_integration_cmd(ns, outdir=outdir),
        summary_path=outdir / "summary.env",
        extra_env={"HOME": str(home_dir)},
        post_run=_mapgen_integration_post_run(ns, outdir),
        cwd=home_dir / ".barony",
    )


def build_release_suite_steps(
    ns: argparse.Namespace,
    *,
    profile: str,
    platform_name: str,
    outdir: Path,
) -> list[SuiteStep]:
    auto_start_delay = _suite_auto_start_delay(platform_name)
    steps: list[SuiteStep] = []

    for step_name in resolve_release_suite_step_names(profile):
        step_outdir = outdir / _step_group(step_name) / step_name
        if step_name == "framework-self-check":
            steps.append(_framework_self_check_step(step_outdir))
        elif step_name == "lan-helo-lobby-15p":
            steps.append(
                _runner_step(
                    ns,
                    name=step_name,
                    outdir=step_outdir,
                    lane="lan-helo-chunk",
                    lane_args=[
                        *_display_args(ns),
                        "--instances",
                        "15",
                        "--timeout",
                        "360",
                        "--force-chunk",
                        "1",
                        "--chunk-payload-max",
                        "200",
                        "--trace-account-labels",
                        "1",
                        "--require-account-labels",
                        "1",
                    ],
                )
            )
        elif step_name == "lan-helo-gameplay-4p":
            steps.append(
                _runner_step(
                    ns,
                    name=step_name,
                    outdir=step_outdir,
                    lane="lan-helo-chunk",
                    lane_args=[
                        *_display_args(ns),
                        "--instances",
                        "4",
                        "--timeout",
                        "300",
                        "--force-chunk",
                        "1",
                        "--chunk-payload-max",
                        "200",
                        "--auto-start",
                        "1",
                        "--auto-start-delay",
                        str(auto_start_delay),
                        "--auto-enter-dungeon",
                        "1",
                        "--auto-enter-dungeon-delay",
                        "3",
                        "--require-mapgen",
                        "1",
                    ],
                )
            )
        elif step_name == "helo-adversarial-4p":
            steps.append(
                _runner_step(
                    ns,
                    name=step_name,
                    outdir=step_outdir,
                    lane="helo-adversarial",
                    lane_args=[
                        *_display_args(ns),
                        "--instances",
                        "4",
                        "--chunk-payload-max",
                        "200",
                    ],
                )
            )
        elif step_name == "join-leave-churn-ready-6p":
            steps.append(
                _runner_step(
                    ns,
                    name=step_name,
                    outdir=step_outdir,
                    lane="join-leave-churn",
                    lane_args=[
                        *_display_args(ns),
                        "--instances",
                        "6",
                        "--churn-cycles",
                        "2",
                        "--churn-count",
                        "2",
                        "--initial-timeout",
                        "180",
                        "--cycle-timeout",
                        "240",
                        "--auto-ready",
                        "1",
                        "--trace-ready-sync",
                        "1",
                        "--require-ready-sync",
                        "1",
                        "--trace-join-rejects",
                        "1",
                    ],
                )
            )
        elif step_name == "save-reload-compat":
            steps.append(
                _runner_step(
                    ns,
                    name=step_name,
                    outdir=step_outdir,
                    lane="save-reload-compat",
                    lane_args=[*_display_args(ns)],
                )
            )
        elif step_name == "status-effect-queue-init":
            steps.append(
                _runner_step(
                    ns,
                    name=step_name,
                    outdir=step_outdir,
                    lane="status-effect-queue-init",
                    lane_args=[*_display_args(ns)],
                )
            )
        elif step_name == "inventory-fast-pass":
            steps.append(
                _runner_step(
                    ns,
                    name=step_name,
                    outdir=step_outdir,
                    lane="inventory-fast-pass",
                    lane_args=[
                        *_display_args(ns),
                        "--instances",
                        "8",
                    ],
                )
            )
        elif step_name == "lobby-slot-lock-kick-copy":
            steps.append(
                _runner_step(
                    ns,
                    name=step_name,
                    outdir=step_outdir,
                    lane="lobby-slot-lock-kick-copy",
                    lane_args=[*_display_args(ns)],
                )
            )
        elif step_name == "lobby-page-navigation":
            steps.append(
                _runner_step(
                    ns,
                    name=step_name,
                    outdir=step_outdir,
                    lane="lobby-page-navigation",
                    lane_args=[
                        *_display_args(ns),
                        "--instances",
                        "15",
                    ],
                )
            )
        elif step_name == "remote-combat-slot-bounds":
            steps.append(
                _runner_step(
                    ns,
                    name=step_name,
                    outdir=step_outdir,
                    lane="remote-combat-slot-bounds",
                    lane_args=[
                        *_display_args(ns),
                        "--instances",
                        "15",
                    ],
                )
            )
        elif step_name == "splitscreen-baseline":
            steps.append(
                _runner_step(
                    ns,
                    name=step_name,
                    outdir=step_outdir,
                    lane="splitscreen-baseline",
                    lane_args=["--size", ns.size],
                )
            )
        elif step_name == "splitscreen-cap":
            steps.append(
                _runner_step(
                    ns,
                    name=step_name,
                    outdir=step_outdir,
                    lane="splitscreen-cap",
                    lane_args=[
                        "--size",
                        ns.size,
                        "--requested-players",
                        "15",
                    ],
                )
            )
        elif step_name == "mapgen-integration-preflight":
            steps.append(_mapgen_integration_step(ns, outdir=step_outdir))
        elif step_name == "mapgen-level-matrix-sim":
            steps.append(
                _runner_step(
                    ns,
                    name=step_name,
                    outdir=step_outdir,
                    lane="mapgen-level-matrix",
                    lane_args=[
                        "--levels",
                        ns.mapgen_levels,
                        "--min-players",
                        str(ns.mapgen_min_players),
                        "--max-players",
                        str(ns.mapgen_max_players),
                        "--runs-per-player",
                        str(ns.mapgen_matrix_runs),
                        "--size",
                        ns.size,
                        "--stagger",
                        "0",
                        "--timeout",
                        "240",
                        "--auto-start-delay",
                        str(auto_start_delay),
                        "--auto-enter-dungeon",
                        "1",
                        "--auto-enter-dungeon-delay",
                        "3",
                        "--force-chunk",
                        "1",
                        "--chunk-payload-max",
                        "200",
                        "--simulate-mapgen-players",
                        "1",
                        "--inprocess-sim-batch",
                        "1",
                        "--inprocess-player-sweep",
                        "1",
                        "--mapgen-reload-same-level",
                        "1",
                    ],
                )
            )
        elif step_name == "helo-soak":
            steps.append(
                _runner_step(
                    ns,
                    name=step_name,
                    outdir=step_outdir,
                    lane="helo-soak",
                    lane_args=[
                        *_display_args(ns),
                        "--runs",
                        str(ns.helo_soak_runs),
                        "--instances",
                        "4",
                        "--auto-start-delay",
                        str(auto_start_delay),
                        "--auto-enter-dungeon",
                        "1",
                        "--auto-enter-dungeon-delay",
                        "3",
                        "--require-mapgen",
                        "1",
                    ],
                )
            )
        elif step_name == "lobby-kick-target":
            steps.append(
                _runner_step(
                    ns,
                    name=step_name,
                    outdir=step_outdir,
                    lane="lobby-kick-target",
                    lane_args=[
                        *_display_args(ns),
                        "--min-players",
                        "2",
                        "--max-players",
                        "15",
                    ],
                )
            )
        elif step_name == "mapgen-sweep-full-lobby":
            steps.append(
                _runner_step(
                    ns,
                    name=step_name,
                    outdir=step_outdir,
                    lane="mapgen-sweep",
                    lane_args=[
                        "--min-players",
                        str(ns.mapgen_min_players),
                        "--max-players",
                        str(ns.mapgen_max_players),
                        "--runs-per-player",
                        str(ns.full_lobby_mapgen_runs),
                        "--size",
                        ns.size,
                        "--stagger",
                        str(ns.stagger),
                        "--timeout",
                        "300",
                        "--auto-start-delay",
                        str(auto_start_delay),
                        "--auto-enter-dungeon",
                        "1",
                        "--auto-enter-dungeon-delay",
                        "3",
                        "--force-chunk",
                        "1",
                        "--chunk-payload-max",
                        "200",
                        "--simulate-mapgen-players",
                        "0",
                    ],
                )
            )
        else:
            fail(f"unsupported release-suite step: {step_name}")

    return steps


def _write_step_command(command_file: Path, cmd: list[str], extra_env: dict[str, str] | None) -> None:
    lines = ["ARGS:"]
    lines.extend(cmd)
    if extra_env:
        lines.append("")
        lines.append("ENV:")
        for key in sorted(extra_env):
            lines.append(f"{key}={extra_env[key]}")
    command_file.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _run_step(step: SuiteStep) -> tuple[str, int, int, str, Path, Path]:
    reset_paths(step.outdir)
    step.outdir.mkdir(parents=True, exist_ok=True)
    stdout_log = step.outdir / "suite_step.stdout.log"
    command_file = step.outdir / "command.txt"
    _write_step_command(command_file, step.cmd, step.extra_env)

    env = os.environ.copy()
    if step.extra_env:
        env.update(step.extra_env)
    run_cwd = step.cwd
    if run_cwd is not None:
        run_cwd.mkdir(parents=True, exist_ok=True)

    log(f"Suite step start: {step.name}")
    started = time.monotonic()
    with stdout_log.open("w", encoding="utf-8", errors="replace") as out:
        proc = subprocess.run(
            step.cmd,
            check=False,
            stdout=out,
            stderr=subprocess.STDOUT,
            env=env,
            cwd=run_cwd,
        )
    duration = int(round(time.monotonic() - started))
    if step.post_run is not None:
        step.post_run(proc.returncode)
    prune_models_cache(step.outdir)

    summary_status = ""
    if step.summary_path is not None:
        summary_status = parse_summary_key_last(step.summary_path, "RESULT")
    status = "pass"
    if proc.returncode != 0:
        status = "fail"
    elif summary_status and summary_status != "pass":
        status = "fail"

    log(
        f"Suite step complete: {step.name} status={status} rc={proc.returncode} duration={duration}s"
    )
    return status, proc.returncode, duration, summary_status, stdout_log, command_file


def _write_suite_report(
    *,
    report_path: Path,
    profile: str,
    platform_name: str,
    app: Path,
    datadir: Path | None,
    total_steps: int,
    pass_steps: int,
    fail_steps: int,
    failed_steps: list[str],
    rows: list[dict[str, str]],
) -> None:
    lines: list[str] = []
    lines.append("<!doctype html>")
    lines.append("<html><head><meta charset='utf-8'><title>Barony Release Smoke Suite</title>")
    lines.append("<style>")
    lines.append("body{font-family:Menlo,Monaco,Consolas,monospace;background:#10151b;color:#e9eef5;padding:20px;}")
    lines.append("h1{font-size:24px;margin:0 0 12px 0;} h2{margin-top:28px;}")
    lines.append("table{border-collapse:collapse;margin:10px 0 18px 0;width:100%;}")
    lines.append("th,td{border:1px solid #2b3440;padding:6px 8px;text-align:left;vertical-align:top;}")
    lines.append("th{background:#1b2330;} tr:nth-child(even) td{background:#141b24;}")
    lines.append(".pass{color:#86d993;} .fail{color:#ff8e8e;} code{background:#1b2330;padding:2px 4px;border-radius:4px;}")
    lines.append("</style></head><body>")
    lines.append("<h1>Barony Release Smoke Suite</h1>")
    lines.append("<ul>")
    lines.append(f"<li>Profile: <code>{html.escape(profile)}</code></li>")
    lines.append(f"<li>Platform: <code>{html.escape(platform_name)}</code></li>")
    lines.append(f"<li>App: <code>{html.escape(str(app))}</code></li>")
    lines.append(f"<li>Datadir: <code>{html.escape(str(datadir or ''))}</code></li>")
    lines.append(f"<li>Steps: {total_steps} ({pass_steps} pass / {fail_steps} fail)</li>")
    if failed_steps:
        lines.append(f"<li>Failed steps: <code>{html.escape(';'.join(failed_steps))}</code></li>")
    lines.append("</ul>")
    lines.append("<h2>Steps</h2>")
    lines.append("<table>")
    lines.append(
        "<tr><th>Step</th><th>Group</th><th>Kind</th><th>Status</th><th>RC</th><th>Duration</th><th>Summary</th><th>Outdir</th></tr>"
    )
    for row in rows:
        status_class = "pass" if row["status"] == "pass" else "fail"
        lines.append(
            "<tr>"
            f"<td>{html.escape(row['step'])}</td>"
            f"<td>{html.escape(row['group'])}</td>"
            f"<td>{html.escape(row['kind'])}</td>"
            f"<td class='{status_class}'>{html.escape(row['status'])}</td>"
            f"<td>{html.escape(row['rc'])}</td>"
            f"<td>{html.escape(row['duration_sec'])}s</td>"
            f"<td>{html.escape(row['summary_status'])}</td>"
            f"<td><code>{html.escape(row['outdir'])}</code></td>"
            "</tr>"
        )
    lines.append("</table>")
    lines.append("</body></html>")
    report_path.write_text("\n".join(lines), encoding="utf-8")


def _validate_suite_args(ns: argparse.Namespace) -> None:
    ORCH.validate_lane_environment(ns.app, ns.datadir)
    if ns.mapgen_min_players < 1 or ns.mapgen_min_players > 15:
        fail("--mapgen-min-players must be in 1..15")
    if ns.mapgen_max_players < 1 or ns.mapgen_max_players > 15:
        fail("--mapgen-max-players must be in 1..15")
    if ns.mapgen_min_players > ns.mapgen_max_players:
        fail("--mapgen-min-players must be <= --mapgen-max-players")
    for name in (
        "--mapgen-integration-runs",
        "--mapgen-matrix-runs",
        "--full-lobby-mapgen-runs",
        "--helo-soak-runs",
        "--stagger",
    ):
        value = getattr(ns, name[2:].replace("-", "_"))
        if value < 0:
            fail(f"{name} must be >= 0")
    if ns.mapgen_integration_runs < 1:
        fail("--mapgen-integration-runs must be >= 1")
    if ns.mapgen_matrix_runs < 1:
        fail("--mapgen-matrix-runs must be >= 1")
    if ns.full_lobby_mapgen_runs < 1:
        fail("--full-lobby-mapgen-runs must be >= 1")
    if ns.helo_soak_runs < 1:
        fail("--helo-soak-runs must be >= 1")


def cmd_release_suite(ns: argparse.Namespace) -> int:
    _validate_suite_args(ns)
    platform_name = resolve_release_suite_platform(ns.platform)
    outdir = normalize_outdir(ns.outdir, f"release-suite-{platform_name}-{ns.profile}")
    results_csv = outdir / "suite_results.csv"
    summary_path = outdir / "summary.env"
    report_path = outdir / "release_suite_report.html"
    write_csv_header(results_csv, SUITE_RESULT_HEADER)

    steps = build_release_suite_steps(ns, profile=ns.profile, platform_name=platform_name, outdir=outdir)
    pass_steps = 0
    fail_steps = 0
    failed_steps: list[str] = []
    executed_rows: list[dict[str, str]] = []
    suite_started = time.monotonic()

    log(f"Release suite start: profile={ns.profile} platform={platform_name} outdir={outdir}")
    for step in steps:
        status, rc, duration, summary_status, stdout_log, command_file = _run_step(step)
        if status == "pass":
            pass_steps += 1
        else:
            fail_steps += 1
            failed_steps.append(step.name)
        row = {
            "step": step.name,
            "group": step.group,
            "kind": step.kind,
            "status": status,
            "rc": str(rc),
            "duration_sec": str(duration),
            "outdir": str(step.outdir),
            "summary_status": summary_status,
            "stdout_log": str(stdout_log),
            "command_file": str(command_file),
        }
        executed_rows.append(row)
        append_csv_row(
            results_csv,
            [
                row["step"],
                row["group"],
                row["kind"],
                row["status"],
                row["rc"],
                row["duration_sec"],
                row["outdir"],
                row["summary_status"],
                row["stdout_log"],
                row["command_file"],
            ],
        )
        if ns.fail_fast and status != "pass":
            log(f"Release suite stopping early due to --fail-fast after step={step.name}")
            break

    total_duration = int(round(time.monotonic() - suite_started))
    total_steps = len(executed_rows)
    result = "pass" if fail_steps == 0 and total_steps == len(steps) else "fail"

    _write_suite_report(
        report_path=report_path,
        profile=ns.profile,
        platform_name=platform_name,
        app=ns.app,
        datadir=ns.datadir,
        total_steps=total_steps,
        pass_steps=pass_steps,
        fail_steps=fail_steps,
        failed_steps=failed_steps,
        rows=executed_rows,
    )
    write_summary_env(
        summary_path,
        {
            "RESULT": result,
            "PROFILE": ns.profile,
            "PLATFORM": platform_name,
            "OUTDIR": outdir,
            "APP": ns.app,
            "DATADIR": ns.datadir or "",
            "PLANNED_STEPS": len(steps),
            "EXECUTED_STEPS": total_steps,
            "PASS_STEPS": pass_steps,
            "FAIL_STEPS": fail_steps,
            "FAILED_STEPS": ";".join(failed_steps),
            "FAIL_FAST": int(ns.fail_fast),
            "TOTAL_DURATION_SECONDS": total_duration,
            "CSV_PATH": results_csv,
            "HTML_REPORT_PATH": report_path,
        },
    )

    log(
        f"Release suite complete: result={result} pass_steps={pass_steps} fail_steps={fail_steps} duration={total_duration}s"
    )
    return 0 if result == "pass" else 1
