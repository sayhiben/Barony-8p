from __future__ import annotations

import argparse
from pathlib import Path

from .inventory_lane import (
    cmd_inventory_churn,
    cmd_inventory_edge_cases,
    cmd_inventory_fast_pass,
    cmd_inventory_lifecycle,
)
from .parser_common import add_app_datadir_args


def _add_common_lane_args(parser: argparse.ArgumentParser, *, timeout_default: int) -> None:
    parser.add_argument("--size", default="1280x720", help="Window size.")
    parser.add_argument("--stagger", type=int, default=1, help="Delay between launches (seconds).")
    parser.add_argument("--timeout", type=int, default=timeout_default, help="Lane timeout in seconds.")
    parser.add_argument("--outdir", default=None, help="Artifact directory.")


def register_inventory_lane_parsers(
    sub: argparse._SubParsersAction[argparse.ArgumentParser],
    *,
    default_app: Path,
) -> None:
    lifecycle = sub.add_parser(
        "inventory-lifecycle",
        help="Run inventory packet lifecycle lane (USEI/EQUI/EQUS/EQUM/COOK)",
    )
    add_app_datadir_args(lifecycle, default_app=default_app)
    _add_common_lane_args(lifecycle, timeout_default=360)
    lifecycle.add_argument("--pulses", type=int, default=4, help="Inventory action pulses per opcode.")
    lifecycle.add_argument(
        "--inventory-delay",
        type=int,
        default=0,
        help="Delay between autopilot inventory actions (seconds).",
    )
    lifecycle.set_defaults(handler=cmd_inventory_lifecycle)

    edge = sub.add_parser(
        "inventory-edge-cases",
        help="Run inventory edge-case lane (invalid EQUM slot, COOK count=0)",
    )
    add_app_datadir_args(edge, default_app=default_app)
    _add_common_lane_args(edge, timeout_default=360)
    edge.add_argument("--pulses", type=int, default=4, help="Inventory action pulses per opcode.")
    edge.add_argument(
        "--inventory-delay",
        type=int,
        default=0,
        help="Delay between autopilot inventory actions (seconds).",
    )
    edge.set_defaults(handler=cmd_inventory_edge_cases)

    churn = sub.add_parser(
        "inventory-churn",
        help="Run high-slot inventory packet churn lane in active gameplay",
    )
    add_app_datadir_args(churn, default_app=default_app)
    _add_common_lane_args(churn, timeout_default=480)
    churn.add_argument("--instances", type=int, default=8, help="Total host+client instances (3..15).")
    churn.add_argument("--pulses", type=int, default=10, help="Inventory action pulses per opcode.")
    churn.add_argument(
        "--inventory-delay",
        type=int,
        default=0,
        help="Delay between autopilot inventory actions (seconds).",
    )
    churn.set_defaults(handler=cmd_inventory_churn)

    fast = sub.add_parser(
        "inventory-fast-pass",
        help="Run full inventory lifecycle+edge+churn coverage with minimal restarts",
    )
    add_app_datadir_args(fast, default_app=default_app)
    _add_common_lane_args(fast, timeout_default=720)
    fast.add_argument("--lifecycle-pulses", type=int, default=4, help="Lifecycle/edge pulses per opcode.")
    fast.add_argument("--churn-pulses", type=int, default=10, help="Churn pulses per opcode.")
    fast.add_argument("--churn-timeout", type=int, default=480, help="Timeout for high-slot churn lane.")
    fast.add_argument(
        "--inventory-delay",
        type=int,
        default=0,
        help="Delay between autopilot inventory actions (seconds).",
    )
    fast.add_argument("--instances", type=int, default=8, help="Total host+client instances for churn lane (3..15).")
    fast.set_defaults(handler=cmd_inventory_fast_pass)
