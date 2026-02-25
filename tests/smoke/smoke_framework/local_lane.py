from __future__ import annotations

import json
import shutil
from dataclasses import dataclass
from pathlib import Path

from .fs import reset_paths

SMOKE_MIN_ALL_SETTINGS_VERSION = 25
SMOKE_MIN_CONTROLS_VERSION = 5
SMOKE_CONTROL_DEFAULTS = {
    "numkeys_change_hotbar_slot_enabled": True,
    "gamepad_deadzone_left": 25.0,
    "gamepad_deadzone_right": 25.0,
    "quick_turn_speed_control": 1.0,
    "quick_turn_speed_mkb_control": 1.0,
    "mouse_event_limit_mkb_control": 1000,
    "spell_quickcast_mkb_enabled": False,
    "spell_quickcast_controller_enabled": False,
}


@dataclass(frozen=True)
class LocalSingleLanePaths:
    outdir: Path
    stdout_dir: Path
    instance_root: Path
    home_dir: Path
    stdout_log: Path
    host_log: Path
    pid_file: Path


def _apply_deterministic_control_defaults(config: dict) -> None:
    controls = config.get("controls")
    if not isinstance(controls, list):
        return
    version = config.get("version")
    if not isinstance(version, int) or version < SMOKE_MIN_ALL_SETTINGS_VERSION:
        config["version"] = SMOKE_MIN_ALL_SETTINGS_VERSION
    for control in controls:
        if not isinstance(control, dict):
            continue
        control_version = control.get("version")
        if not isinstance(control_version, int) or control_version < SMOKE_MIN_CONTROLS_VERSION:
            control["version"] = SMOKE_MIN_CONTROLS_VERSION
        for key, value in SMOKE_CONTROL_DEFAULTS.items():
            control.setdefault(key, value)


def seed_smoke_home_profile(home_dir: Path, seed_config_path: Path, seed_books_path: Path) -> None:
    seed_root = home_dir / ".barony"
    config_dest = seed_root / "config/config.json"
    wrote_config = False
    if seed_config_path.is_file():
        config_dest.parent.mkdir(parents=True, exist_ok=True)
        try:
            with seed_config_path.open("r", encoding="utf-8", errors="replace") as src:
                config = json.load(src)
            if isinstance(config, dict):
                config["skipintro"] = True
                config["mods"] = []
                _apply_deterministic_control_defaults(config)
            with config_dest.open("w", encoding="utf-8") as dst:
                json.dump(config, dst)
            wrote_config = True
        except Exception:
            shutil.copyfile(seed_config_path, config_dest)
            wrote_config = True
    if not wrote_config:
        config_dest.parent.mkdir(parents=True, exist_ok=True)
        config_dest.write_text('{"skipintro":true,"mods":[]}\n', encoding="utf-8")
    if seed_books_path.is_file():
        books_dest = seed_root / "books/compiled_books.json"
        books_dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(seed_books_path, books_dest)


def prepare_single_local_instance_lane(outdir: Path, *extra_cleanup: Path) -> LocalSingleLanePaths:
    stdout_dir = outdir / "stdout"
    instance_root = outdir / "instance"
    home_dir = instance_root / "home-1"
    stdout_log = stdout_dir / "instance-1.stdout.log"
    host_log = home_dir / ".barony/log.txt"
    pid_file = outdir / "pid.txt"

    reset_paths(stdout_dir, instance_root, pid_file, *extra_cleanup)
    home_dir.mkdir(parents=True, exist_ok=True)
    seed_smoke_home_profile(
        home_dir,
        Path.home() / ".barony/config/config.json",
        Path.home() / ".barony/books/compiled_books.json",
    )
    return LocalSingleLanePaths(
        outdir=outdir,
        stdout_dir=stdout_dir,
        instance_root=instance_root,
        home_dir=home_dir,
        stdout_log=stdout_log,
        host_log=host_log,
        pid_file=pid_file,
    )
