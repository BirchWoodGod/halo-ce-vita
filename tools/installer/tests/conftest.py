# SPDX-License-Identifier: GPL-3.0-only
import os
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

import halo_ce_vita_installer as hcv  # noqa: E402

REAL_SETTINGS_PATH = hcv.settings_path


@pytest.fixture(autouse=True)
def own_settings(tmp_path, monkeypatch):
    """The window's settings file in the test's own folder (never the
    user's): each test starts with none."""
    path = tmp_path / "settings" / hcv.SETTINGS_FILE
    monkeypatch.setattr(hcv, "settings_path", lambda: str(path))
    # (and a home of its own: a real Steam library or MCC is never found, or read)
    home = tmp_path / "home"
    home.mkdir()
    monkeypatch.setenv("HOME", str(home))
    monkeypatch.setenv("USERPROFILE", str(home))
    monkeypatch.delenv("XDG_DATA_HOME", raising=False)
    monkeypatch.delenv("LOCALAPPDATA", raising=False)
    return path


@pytest.fixture
def real_settings_path():
    """The tool's own settings_path (own_settings replaces it)."""
    return REAL_SETTINGS_PATH
