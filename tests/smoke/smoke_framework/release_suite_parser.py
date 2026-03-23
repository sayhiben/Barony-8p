from __future__ import annotations

import argparse
from pathlib import Path

from .parser_common import add_app_datadir_args
from .release_suite_lane import cmd_release_suite


def register_release_suite_parser(
    sub: argparse._SubParsersAction[argparse.ArgumentParser],
    *,
    default_app: Path,
) -> None:
    suite = sub.add_parser(
        "release-suite",
        help="Run the documented cross-platform release smoke suite",
    )
    add_app_datadir_args(
        suite,
        default_app=default_app,
        datadir_help="Optional runtime assets directory passed through to Barony via -datadir=<path>.",
    )
    suite.add_argument(
        "--profile",
        choices=("sanity", "release", "full"),
        default="release",
        help="Smoke profile to execute.",
    )
    suite.add_argument(
        "--platform",
        choices=("auto", "macos", "windows"),
        default="auto",
        help="Platform-specific suite tuning. 'auto' uses the current host platform.",
    )
    suite.add_argument("--size", default="1280x720", help="Window size for launched instances.")
    suite.add_argument("--stagger", type=int, default=1, help="Delay between instance launches (seconds).")
    suite.add_argument(
        "--mapgen-levels",
        default="1,7,16,33",
        help="Comma-separated procedural floors used by mapgen suite steps.",
    )
    suite.add_argument(
        "--mapgen-min-players",
        type=int,
        default=1,
        help="Minimum player count used by suite mapgen steps.",
    )
    suite.add_argument(
        "--mapgen-max-players",
        type=int,
        default=15,
        help="Maximum player count used by suite mapgen steps.",
    )
    suite.add_argument(
        "--mapgen-integration-runs",
        type=int,
        default=2,
        help="Runs per player for the headless mapgen integration preflight step.",
    )
    suite.add_argument(
        "--mapgen-matrix-runs",
        type=int,
        default=2,
        help="Runs per player for the scripted mapgen level-matrix step.",
    )
    suite.add_argument(
        "--full-lobby-mapgen-runs",
        type=int,
        default=1,
        help="Runs per player for the full-profile full-lobby mapgen sweep.",
    )
    suite.add_argument(
        "--helo-soak-runs",
        type=int,
        default=5,
        help="Run count for the full-profile HELO soak step.",
    )
    suite.add_argument("--outdir", default=None, help="Artifact directory for the overall suite.")
    suite.add_argument(
        "--fail-fast",
        action="store_true",
        help="Stop the suite after the first failing step instead of collecting the full report.",
    )
    suite.set_defaults(handler=cmd_release_suite)
