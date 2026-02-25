from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path

from .common import log
from .csvio import append_csv_row, write_csv_header
from .fs import normalize_outdir, prune_models_cache, reset_paths
from .lane_helpers import (
    build_default_helo_lane_args,
    require_uint_specs,
    run_ns_helo_child_lane,
    write_single_lane_result_files,
)
from .lane_matrix import compute_lane_result, update_lane_counts
from .orchestration import RunnerOrchestrator
from .summary import write_summary_env
from .tokens import parse_key_value_tokens

SCRIPT_DIR = Path(__file__).resolve().parent.parent
RUNNER = SCRIPT_DIR / "smoke_runner.py"
RUNNER_PYTHON = sys.executable or "python3"
_ORCH = RunnerOrchestrator(runner_path=RUNNER, runner_python=RUNNER_PYTHON)
validate_lane_environment = _ORCH.validate_lane_environment
run_helo_child_lane = _ORCH.run_helo_child_lane

_BASE_OPS: tuple[str, ...] = ("USEI", "EQUI", "EQUS", "EQUM", "COOK")
_POST_PASS_WAIT_SECONDS = 8

_CSV_HEADER: list[str] = [
    "lane",
    "result",
    "child_result",
    "total_lines",
    "fail_lines",
    "cleanup_required_lines",
    "cleanup_missing_clear_lines",
    "op_usei",
    "op_equi",
    "op_equs",
    "op_equm",
    "op_cook",
    "edge_invalid_slot_lines",
    "edge_count_zero_lines",
    "host_log",
    "outdir",
]


@dataclass(frozen=True)
class InventoryLaneRun:
    lane_name: str
    child_result: str
    metrics: dict[str, int]
    lane_outdir: Path
    host_log: Path


def _inventory_env(*, pulses: int, inventory_delay: int, include_edge_cases: bool) -> dict[str, str]:
    return {
        "BARONY_SMOKE_TRACE_INVENTORY_PACKETS": "1",
        "BARONY_SMOKE_AUTO_INVENTORY_PULSES": str(pulses),
        "BARONY_SMOKE_AUTO_INVENTORY_DELAY_SECS": str(inventory_delay),
        "BARONY_SMOKE_AUTO_INVENTORY_INCLUDE_EDGE_CASES": "1" if include_edge_cases else "0",
        "BARONY_SMOKE_AUTO_INVENTORY_CLIENT_SLOT": "0",
    }


def _read_log_lines(path: Path) -> list[str]:
    if not path.is_file():
        return []
    with path.open("r", encoding="utf-8", errors="replace") as f:
        return [line.rstrip("\n") for line in f]


def collect_inventory_metrics(host_log: Path) -> dict[str, int]:
    metrics: dict[str, int] = {
        "total_lines": 0,
        "fail_lines": 0,
        "cleanup_required_lines": 0,
        "cleanup_missing_clear_lines": 0,
        "op_usei": 0,
        "op_equi": 0,
        "op_equs": 0,
        "op_equm": 0,
        "op_cook": 0,
        "edge_invalid_slot_lines": 0,
        "edge_count_zero_lines": 0,
    }
    prefix = "[SMOKE]: inventory packet "
    for line in _read_log_lines(host_log):
        if prefix not in line:
            continue
        tokens = parse_key_value_tokens(line)
        op = tokens.get("op", "").upper()
        status = tokens.get("status", "")
        edge = tokens.get("edge", "")
        cleanup_required = int(tokens.get("cleanup_required", "0")) if tokens.get("cleanup_required", "0").isdigit() else 0
        cleanup_cleared = int(tokens.get("cleanup_cleared", "0")) if tokens.get("cleanup_cleared", "0").isdigit() else 0

        metrics["total_lines"] += 1
        if status != "ok":
            metrics["fail_lines"] += 1
        if cleanup_required == 1:
            metrics["cleanup_required_lines"] += 1
            if cleanup_cleared != 1:
                metrics["cleanup_missing_clear_lines"] += 1

        if op == "USEI":
            metrics["op_usei"] += 1
        elif op == "EQUI":
            metrics["op_equi"] += 1
        elif op == "EQUS":
            metrics["op_equs"] += 1
        elif op == "EQUM":
            metrics["op_equm"] += 1
        elif op == "COOK":
            metrics["op_cook"] += 1

        if edge == "invalid-slot":
            metrics["edge_invalid_slot_lines"] += 1
        elif edge == "count-zero":
            metrics["edge_count_zero_lines"] += 1
    return metrics


def _has_base_coverage(metrics: dict[str, int], *, minimum_count: int) -> bool:
    if minimum_count <= 0:
        return True
    return all(metrics[f"op_{op.lower()}"] >= minimum_count for op in _BASE_OPS)


def _has_base_presence(metrics: dict[str, int]) -> bool:
    return _has_base_coverage(metrics, minimum_count=1)


def _row_for_result(
    *,
    run: InventoryLaneRun,
    lane_result: str,
) -> list[str | int | Path]:
    m = run.metrics
    return [
        run.lane_name,
        lane_result,
        run.child_result,
        m["total_lines"],
        m["fail_lines"],
        m["cleanup_required_lines"],
        m["cleanup_missing_clear_lines"],
        m["op_usei"],
        m["op_equi"],
        m["op_equs"],
        m["op_equm"],
        m["op_cook"],
        m["edge_invalid_slot_lines"],
        m["edge_count_zero_lines"],
        run.host_log,
        run.lane_outdir,
    ]


def _run_inventory_helo_child(
    ns: argparse.Namespace,
    *,
    lane_name: str,
    instances: int,
    lane_outdir: Path,
    pulses: int,
    inventory_delay: int,
    include_edge_cases: bool,
    timeout: int,
) -> InventoryLaneRun:
    _rc, child_result, _values, _summary_file = run_ns_helo_child_lane(
        run_helo_child_lane,
        ns,
        instances=instances,
        expected_players=instances,
        timeout=timeout,
        lane_outdir=lane_outdir,
        lane_args=build_default_helo_lane_args(
            "--post-pass-wait",
            str(_POST_PASS_WAIT_SECONDS),
            "--auto-start-delay",
            "2",
            "--auto-enter-dungeon",
            "1",
            "--auto-enter-dungeon-delay",
            "3",
            "--require-mapgen",
            "1",
            "--mapgen-samples",
            "1",
            auto_start=1,
        ),
        extra_env=_inventory_env(
            pulses=pulses,
            inventory_delay=inventory_delay,
            include_edge_cases=include_edge_cases,
        ),
    )
    host_log = lane_outdir / "instances/home-1/.barony/log.txt"
    metrics = collect_inventory_metrics(host_log)
    return InventoryLaneRun(
        lane_name=lane_name,
        child_result=child_result,
        metrics=metrics,
        lane_outdir=lane_outdir,
        host_log=host_log,
    )


def _validate_lifecycle_args(ns: argparse.Namespace) -> None:
    require_uint_specs(
        ns,
        (
            ("--stagger", "stagger", None, None),
            ("--timeout", "timeout", None, None),
            ("--pulses", "pulses", 1, 512),
            ("--inventory-delay", "inventory_delay", 0, 120),
        ),
    )


def _validate_churn_args(ns: argparse.Namespace) -> None:
    require_uint_specs(
        ns,
        (
            ("--stagger", "stagger", None, None),
            ("--timeout", "timeout", None, None),
            ("--instances", "instances", 3, 15),
            ("--pulses", "pulses", 1, 512),
            ("--inventory-delay", "inventory_delay", 0, 120),
        ),
    )


def _validate_fast_pass_args(ns: argparse.Namespace) -> None:
    require_uint_specs(
        ns,
        (
            ("--stagger", "stagger", None, None),
            ("--timeout", "timeout", None, None),
            ("--lifecycle-pulses", "lifecycle_pulses", 1, 512),
            ("--churn-pulses", "churn_pulses", 1, 512),
            ("--churn-timeout", "churn_timeout", 1, None),
            ("--inventory-delay", "inventory_delay", 0, 120),
            ("--instances", "instances", 3, 15),
        ),
    )


def _lifecycle_lane_result(run: InventoryLaneRun, *, pulses: int) -> str:
    metrics = run.metrics
    return compute_lane_result(
        run.child_result,
        metrics["fail_lines"] == 0,
        metrics["cleanup_missing_clear_lines"] == 0,
        _has_base_coverage(metrics, minimum_count=pulses),
    )


def _edge_lane_result(run: InventoryLaneRun, *, pulses: int) -> str:
    metrics = run.metrics
    return compute_lane_result(
        run.child_result,
        metrics["fail_lines"] == 0,
        metrics["cleanup_missing_clear_lines"] == 0,
        _has_base_coverage(metrics, minimum_count=pulses),
        metrics["edge_invalid_slot_lines"] >= pulses,
        metrics["edge_count_zero_lines"] >= pulses,
    )


def _churn_lane_result(run: InventoryLaneRun) -> str:
    metrics = run.metrics
    return compute_lane_result(
        run.child_result,
        metrics["fail_lines"] == 0,
        metrics["cleanup_missing_clear_lines"] == 0,
        _has_base_presence(metrics),
        metrics["edge_invalid_slot_lines"] >= 1,
        metrics["edge_count_zero_lines"] >= 1,
    )


def cmd_inventory_lifecycle(ns: argparse.Namespace) -> int:
    validate_lane_environment(ns.app, ns.datadir)
    _validate_lifecycle_args(ns)

    outdir = normalize_outdir(ns.outdir, "inventory-lifecycle")
    lane_outdir = outdir / "lane-lifecycle"
    csv_path = outdir / "inventory_lifecycle_results.csv"
    summary_path = outdir / "summary.env"
    reset_paths(lane_outdir, csv_path, summary_path)

    run = _run_inventory_helo_child(
        ns,
        lane_name="inventory-lifecycle",
        instances=2,
        lane_outdir=lane_outdir,
        pulses=ns.pulses,
        inventory_delay=ns.inventory_delay,
        include_edge_cases=False,
        timeout=ns.timeout,
    )
    lane_result = _lifecycle_lane_result(run, pulses=ns.pulses)

    payload = write_single_lane_result_files(
        summary_path=summary_path,
        csv_path=csv_path,
        csv_header=_CSV_HEADER,
        csv_row=_row_for_result(run=run, lane_result=lane_result),
        lane_result=lane_result,
        outdir=outdir,
        app=ns.app,
        datadir=ns.datadir,
        lane_outdir=lane_outdir,
        summary_extra={
            "PULSES": ns.pulses,
            "INVENTORY_DELAY_SECONDS": ns.inventory_delay,
            "HOST_LOG": run.host_log,
        },
    )

    prune_models_cache(lane_outdir)
    overall = str(payload["RESULT"])
    log(
        "result="
        f"{overall} lane={lane_result} total={run.metrics['total_lines']} "
        f"fail={run.metrics['fail_lines']} cleanupMissing={run.metrics['cleanup_missing_clear_lines']}"
    )
    log(f"csv={csv_path}")
    log(f"summary={summary_path}")
    return 1 if overall != "pass" else 0


def cmd_inventory_edge_cases(ns: argparse.Namespace) -> int:
    validate_lane_environment(ns.app, ns.datadir)
    _validate_lifecycle_args(ns)

    outdir = normalize_outdir(ns.outdir, "inventory-edge-cases")
    lane_outdir = outdir / "lane-edge"
    csv_path = outdir / "inventory_edge_case_results.csv"
    summary_path = outdir / "summary.env"
    reset_paths(lane_outdir, csv_path, summary_path)

    run = _run_inventory_helo_child(
        ns,
        lane_name="inventory-edge-cases",
        instances=2,
        lane_outdir=lane_outdir,
        pulses=ns.pulses,
        inventory_delay=ns.inventory_delay,
        include_edge_cases=True,
        timeout=ns.timeout,
    )
    lane_result = _edge_lane_result(run, pulses=ns.pulses)

    payload = write_single_lane_result_files(
        summary_path=summary_path,
        csv_path=csv_path,
        csv_header=_CSV_HEADER,
        csv_row=_row_for_result(run=run, lane_result=lane_result),
        lane_result=lane_result,
        outdir=outdir,
        app=ns.app,
        datadir=ns.datadir,
        lane_outdir=lane_outdir,
        summary_extra={
            "PULSES": ns.pulses,
            "INVENTORY_DELAY_SECONDS": ns.inventory_delay,
            "HOST_LOG": run.host_log,
        },
    )

    prune_models_cache(lane_outdir)
    overall = str(payload["RESULT"])
    log(
        "result="
        f"{overall} lane={lane_result} total={run.metrics['total_lines']} "
        f"edgeInvalid={run.metrics['edge_invalid_slot_lines']} edgeCookZero={run.metrics['edge_count_zero_lines']}"
    )
    log(f"csv={csv_path}")
    log(f"summary={summary_path}")
    return 1 if overall != "pass" else 0


def cmd_inventory_churn(ns: argparse.Namespace) -> int:
    validate_lane_environment(ns.app, ns.datadir)
    _validate_churn_args(ns)

    outdir = normalize_outdir(
        ns.outdir,
        f"inventory-churn-p{ns.instances}",
    )
    lane_outdir = outdir / "lane-churn"
    csv_path = outdir / "inventory_churn_results.csv"
    summary_path = outdir / "summary.env"
    reset_paths(lane_outdir, csv_path, summary_path)

    run = _run_inventory_helo_child(
        ns,
        lane_name="inventory-churn",
        instances=ns.instances,
        lane_outdir=lane_outdir,
        pulses=ns.pulses,
        inventory_delay=ns.inventory_delay,
        include_edge_cases=True,
        timeout=ns.timeout,
    )
    lane_result = _churn_lane_result(run)

    payload = write_single_lane_result_files(
        summary_path=summary_path,
        csv_path=csv_path,
        csv_header=_CSV_HEADER,
        csv_row=_row_for_result(run=run, lane_result=lane_result),
        lane_result=lane_result,
        outdir=outdir,
        app=ns.app,
        datadir=ns.datadir,
        lane_outdir=lane_outdir,
        summary_extra={
            "INSTANCES": ns.instances,
            "PULSES": ns.pulses,
            "INVENTORY_DELAY_SECONDS": ns.inventory_delay,
            "HOST_LOG": run.host_log,
        },
    )

    prune_models_cache(lane_outdir)
    overall = str(payload["RESULT"])
    log(
        "result="
        f"{overall} lane={lane_result} total={run.metrics['total_lines']} "
        f"fail={run.metrics['fail_lines']} cleanupMissing={run.metrics['cleanup_missing_clear_lines']}"
    )
    log(f"csv={csv_path}")
    log(f"summary={summary_path}")
    return 1 if overall != "pass" else 0


def cmd_inventory_fast_pass(ns: argparse.Namespace) -> int:
    validate_lane_environment(ns.app, ns.datadir)
    _validate_fast_pass_args(ns)

    outdir = normalize_outdir(
        ns.outdir,
        f"inventory-fast-pass-p{ns.instances}",
    )
    lane_lifecycle_outdir = outdir / "lane-lifecycle-edge"
    lane_churn_outdir = outdir / "lane-churn"
    csv_path = outdir / "inventory_fast_pass_results.csv"
    summary_path = outdir / "summary.env"
    reset_paths(lane_lifecycle_outdir, lane_churn_outdir, csv_path, summary_path)
    write_csv_header(csv_path, _CSV_HEADER)

    lifecycle_run = _run_inventory_helo_child(
        ns,
        lane_name="inventory-lifecycle-edge",
        instances=2,
        lane_outdir=lane_lifecycle_outdir,
        pulses=ns.lifecycle_pulses,
        inventory_delay=ns.inventory_delay,
        include_edge_cases=True,
        timeout=ns.timeout,
    )
    lifecycle_row = InventoryLaneRun(
        lane_name="inventory-lifecycle",
        child_result=lifecycle_run.child_result,
        metrics=lifecycle_run.metrics,
        lane_outdir=lifecycle_run.lane_outdir,
        host_log=lifecycle_run.host_log,
    )
    edge_row = InventoryLaneRun(
        lane_name="inventory-edge-cases",
        child_result=lifecycle_run.child_result,
        metrics=lifecycle_run.metrics,
        lane_outdir=lifecycle_run.lane_outdir,
        host_log=lifecycle_run.host_log,
    )
    lifecycle_result = _lifecycle_lane_result(lifecycle_row, pulses=ns.lifecycle_pulses)
    edge_result = _edge_lane_result(edge_row, pulses=ns.lifecycle_pulses)

    churn_run = _run_inventory_helo_child(
        ns,
        lane_name="inventory-churn",
        instances=ns.instances,
        lane_outdir=lane_churn_outdir,
        pulses=ns.churn_pulses,
        inventory_delay=ns.inventory_delay,
        include_edge_cases=True,
        timeout=ns.churn_timeout,
    )
    churn_result = _churn_lane_result(churn_run)

    append_csv_row(csv_path, _row_for_result(run=lifecycle_row, lane_result=lifecycle_result))
    append_csv_row(csv_path, _row_for_result(run=edge_row, lane_result=edge_result))
    append_csv_row(csv_path, _row_for_result(run=churn_run, lane_result=churn_result))

    total_lanes = 0
    pass_lanes = 0
    fail_lanes = 0
    for lane_result in (lifecycle_result, edge_result, churn_result):
        total_lanes, pass_lanes, fail_lanes = update_lane_counts(
            lane_result,
            total_lanes=total_lanes,
            pass_lanes=pass_lanes,
            fail_lanes=fail_lanes,
        )
    overall = "pass" if fail_lanes == 0 else "fail"

    write_summary_env(
        summary_path,
        {
            "RESULT": overall,
            "OUTDIR": outdir,
            "APP": ns.app,
            "DATADIR": ns.datadir if ns.datadir else "",
            "TOTAL_LANES": total_lanes,
            "PASS_LANES": pass_lanes,
            "FAIL_LANES": fail_lanes,
            "CSV_PATH": csv_path,
            "LIFECYCLE_LANE_OUTDIR": lifecycle_row.lane_outdir,
            "CHURN_LANE_OUTDIR": churn_run.lane_outdir,
            "LIFECYCLE_PULSES": ns.lifecycle_pulses,
            "CHURN_PULSES": ns.churn_pulses,
            "CHURN_TIMEOUT_SECONDS": ns.churn_timeout,
            "INVENTORY_DELAY_SECONDS": ns.inventory_delay,
        },
    )

    prune_models_cache(lane_lifecycle_outdir)
    prune_models_cache(lane_churn_outdir)
    log(
        "result="
        f"{overall} pass={pass_lanes}/{total_lanes} "
        f"lifecycle={lifecycle_result} edge={edge_result} churn={churn_result}"
    )
    log(f"csv={csv_path}")
    log(f"summary={summary_path}")
    return 1 if overall != "pass" else 0
