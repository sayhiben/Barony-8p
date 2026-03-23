from __future__ import annotations

import argparse
import sys
import unittest
from pathlib import Path

SMOKE_ROOT = Path(__file__).resolve().parents[1]
if str(SMOKE_ROOT) not in sys.path:
    sys.path.insert(0, str(SMOKE_ROOT))

from smoke_framework.release_suite_lane import (  # noqa: E402
    build_release_suite_steps,
    resolve_release_suite_platform,
    resolve_release_suite_step_names,
)


def _suite_namespace() -> argparse.Namespace:
    return argparse.Namespace(
        app=Path("/tmp/barony"),
        datadir=Path("/tmp/datadir"),
        size="1280x720",
        stagger=1,
        mapgen_levels="1,7,16,33",
        mapgen_min_players=1,
        mapgen_max_players=15,
        mapgen_integration_runs=2,
        mapgen_matrix_runs=2,
        full_lobby_mapgen_runs=1,
        helo_soak_runs=5,
    )


class ReleaseSuiteTests(unittest.TestCase):
    def test_release_suite_profile_names(self) -> None:
        sanity = resolve_release_suite_step_names("sanity")
        release = resolve_release_suite_step_names("release")
        full = resolve_release_suite_step_names("full")

        self.assertIn("framework-self-check", sanity)
        self.assertIn("mapgen-integration-preflight", sanity)
        self.assertNotIn("helo-soak", sanity)

        self.assertIn("status-effect-queue-init", release)
        self.assertIn("mapgen-level-matrix-sim", release)
        self.assertNotIn("lobby-kick-target", release)

        self.assertIn("helo-soak", full)
        self.assertIn("lobby-kick-target", full)
        self.assertIn("mapgen-sweep-full-lobby", full)

    def test_release_suite_platform_passthrough(self) -> None:
        self.assertEqual(resolve_release_suite_platform("macos"), "macos")
        self.assertEqual(resolve_release_suite_platform("windows"), "windows")

    def test_release_suite_builds_expected_steps(self) -> None:
        ns = _suite_namespace()
        outdir = Path("/tmp/release-suite")
        steps = build_release_suite_steps(
            ns,
            profile="release",
            platform_name="windows",
            outdir=outdir,
        )

        step_names = [step.name for step in steps]
        self.assertEqual(step_names[0], "framework-self-check")
        self.assertIn("lan-helo-lobby-15p", step_names)
        self.assertIn("join-leave-churn-ready-6p", step_names)
        self.assertIn("mapgen-integration-preflight", step_names)
        self.assertIn("mapgen-level-matrix-sim", step_names)

        integration_step = next(step for step in steps if step.name == "mapgen-integration-preflight")
        self.assertEqual(integration_step.kind, "app")
        self.assertIn("HOME", integration_step.extra_env or {})
        self.assertEqual(integration_step.summary_path, integration_step.outdir / "summary.env")

        matrix_step = next(step for step in steps if step.name == "mapgen-level-matrix-sim")
        self.assertEqual(matrix_step.group, "mapgen")
        self.assertIn("--auto-start-delay", matrix_step.cmd)
        delay_index = matrix_step.cmd.index("--auto-start-delay")
        self.assertEqual(matrix_step.cmd[delay_index + 1], "2")


if __name__ == "__main__":
    unittest.main()
