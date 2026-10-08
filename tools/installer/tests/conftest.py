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
    user's), and no HCV_THEME from outside: each test starts with the
    defaults."""
    path = tmp_path / "settings" / hcv.SETTINGS_FILE
    monkeypatch.setattr(hcv, "settings_path", lambda: str(path))
    monkeypatch.delenv("HCV_THEME", raising=False)
    return path


@pytest.fixture
def real_settings_path():
    """The tool's own settings_path (own_settings replaces it)."""
    return REAL_SETTINGS_PATH
