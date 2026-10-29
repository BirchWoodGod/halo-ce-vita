"""Tests for reading a Halo: The Master Chief Collection mod's campaign files
(port/linux/game/mod_campaign_files.c): CampaignInfo.json and the levels'
campaign/<level>.json, which name and order the mod's maps in the campaign's
level list (custom_edition_mods.c, docs/mcc_cea_maps.md).

The files are made up here (no mod's are stored). The report tool
port/tools/mod_campaign_report.c is compiled with warnings as errors,
undefined behaviour trapping and, where the compiler has it,
AddressSanitizer, so a crash or an out-of-bounds read on malformed input
fails the test that caused it; every truncation of the files and seeded
random corruptions of them must end in a clean verdict.
"""
import json
from pathlib import Path
import random
import shutil
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "port/linux/game/mod_campaign_files.c"
TOOL = ROOT / "port/tools/mod_campaign_report.c"

STRICT_FLAGS = ["-Wall", "-Wextra", "-Wpedantic", "-Werror"]
UB_TRAP_FLAGS = ["-fsanitize=undefined", "-fsanitize-trap=undefined"]

CAMPAIGN_INFO = {
    "CampaignGuid": "00000000-0000-0000-0000-000000000000",
    "Title": {"Neutral": "A TEST CAMPAIGN"},
    "Description": {"Neutral": "Made up for the tests."},
    "CampaignMaps": [f"campaign/{name}.json" for name in ("a10", "a30", "b30")],
}
LEVEL = {
    "MapGuid": "00000000-0000-0000-0000-000000000001",
    "ScenarioFile": "a10",
    "Title": {"Neutral": "The First Level"},
    "Description": {"Neutral": "Escape the made-up ship."},
    "Images": {"Large": "assets\\1.png", "Thumbnail": "assets\\1 small.png"},
    "Flags": ["DisableSavedFilms"],
    "InsertionPoints": [],
    "CampaignMetagame": {"IsNonScoring": False, "ParTimeInSeconds": 0.0},
}


def find_clang():
    for candidate in (shutil.which("clang"), r"C:\Program Files\LLVM\bin\clang.exe"):
        if candidate and Path(candidate).is_file():
            return candidate
    return None


@pytest.fixture(scope="session")
def report_tool(tmp_path_factory):
    clang = find_clang()
    if clang is None:
        pytest.skip("clang is needed to build the report tool")
    folder = tmp_path_factory.mktemp("mod-campaign-report")
    output = folder / ("mod_campaign_report.exe" if sys.platform == "win32" else "mod_campaign_report")
    errors = []
    for extra in (["-fsanitize=address", "-fno-omit-frame-pointer"], []):
        command = [clang, "-std=c99", *STRICT_FLAGS, *UB_TRAP_FLAGS, *extra, "-O1", "-g", f"-I{MODULE.parent}",
                   str(MODULE), str(TOOL), "-o", str(output)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode == 0:
            return output
        errors.append(result.stdout + result.stderr)
    pytest.fail("could not build the report tool:\n" + "\n".join(errors))


def test_module_compiles_as_game_code_without_warnings(tmp_path):
    clang = find_clang()
    if clang is None:
        pytest.skip("clang is needed")
    for target in (["-m32"], []):
        command = [clang, *target, "-std=gnu89", *STRICT_FLAGS, "-c", str(MODULE), "-o", str(tmp_path / "m.o")]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode == 0:
            return
    pytest.fail(result.stdout + result.stderr)


def report(tool, kind, path):
    result = subprocess.run([str(tool), f"--{kind}", str(path)], capture_output=True, text=True, errors="replace")
    assert result.returncode in (0, 1), (result.returncode, result.stdout, result.stderr[-4000:])
    fields = {}
    for line in result.stdout.splitlines():
        key, _, value = line.partition(": ")
        fields[key] = value
    return result.returncode, fields


def written(folder, data, name="file.json"):
    path = folder / name
    path.write_bytes(data if isinstance(data, bytes) else data.encode("utf-8"))
    return path


def test_campaign_info_gives_the_title_and_the_levels_in_order(report_tool, tmp_path):
    returncode, fields = report(report_tool, "info", written(tmp_path, json.dumps(CAMPAIGN_INFO, indent="\t")))
    assert returncode == 0
    assert fields["title"] == "A TEST CAMPAIGN"
    assert fields["levels"] == "3"
    assert [fields[f"level.{index}"] for index in range(3)] == CAMPAIGN_INFO["CampaignMaps"]


def test_a_level_file_gives_its_map_title_and_description(report_tool, tmp_path):
    # (with a UTF-8 byte order mark, as Windows editors save)
    returncode, fields = report(report_tool, "level", written(tmp_path, b"\xef\xbb\xbf" + json.dumps(LEVEL).encode()))
    assert returncode == 0
    assert fields["scenario_file"] == "a10"
    assert fields["title"] == "The First Level"
    assert fields["description"] == "Escape the made-up ship."
    assert fields["thumbnail"] == "assets\\1 small.png"
    assert fields["large_image"] == "assets\\1.png"


def test_text_beyond_printable_ascii_is_kept_as_question_marks(report_tool, tmp_path):
    level = dict(LEVEL, Title={"Neutral": "Caf\u00e9 \\ \"Q\" \u2014 end\ttab"})
    returncode, fields = report(report_tool, "level", written(tmp_path, json.dumps(level, ensure_ascii=False)))
    assert returncode == 0
    assert fields["title"] == "Caf? \\ \"Q\" ? end tab"
    returncode, fields = report(report_tool, "level", written(tmp_path, json.dumps(level, ensure_ascii=True)))
    assert returncode == 0
    assert fields["title"] == "Caf? \\ \"Q\" ? end tab"


def test_long_texts_and_many_levels_are_cut_to_the_room_kept(report_tool, tmp_path):
    info = dict(CAMPAIGN_INFO, Title={"Neutral": "T" * 500},
                CampaignMaps=[f"campaign/level{index}.json" for index in range(40)])
    returncode, fields = report(report_tool, "info", written(tmp_path, json.dumps(info)))
    assert returncode == 0
    assert fields["title"] == "T" * 63
    assert fields["levels"] == "32"
    level = dict(LEVEL, ScenarioFile="s" * 100, Description={"Neutral": "d" * 1000})
    returncode, fields = report(report_tool, "level", written(tmp_path, json.dumps(level)))
    assert returncode == 0
    assert fields["scenario_file"] == "s" * 31
    assert fields["description"] == "d" * 255


def test_fields_elsewhere_than_their_place_are_not_taken(report_tool, tmp_path):
    level = {"Nested": {"ScenarioFile": "b30", "Title": {"Neutral": "no"}}, "Title": "not an object",
             "Description": {"Other": "no"}}
    returncode, fields = report(report_tool, "level", written(tmp_path, json.dumps(level)))
    assert returncode == 0
    assert fields["scenario_file"] == fields["title"] == fields["description"] == ""


@pytest.mark.parametrize("text,status", [
    ("", "not a JSON object"),
    ("[]", "not a JSON object"),
    ("\"string\"", "not a JSON object"),
    ("{", "malformed JSON"),
    ("{\"a\":}", "malformed JSON"),
    ("{\"a\" 1}", "malformed JSON"),
    ("{\"a\":1,}", "malformed JSON"),
    ("{\"a\":1} trailing", "malformed JSON"),
    ("{\"a\":\"\\x\"}", "malformed JSON"),
    ("{\"a\":\"\\u12\"}", "malformed JSON"),
    ("{\"a\":\"line\nbreak\"}", "malformed JSON"),
    ("{\"a\":\"unterminated}", "malformed JSON"),
    ("{\"a\":" + "[" * 8 + "1" + "]" * 8 + "}", "nested too deeply"),
])
def test_malformed_files_are_refused(report_tool, tmp_path, text, status):
    returncode, fields = report(report_tool, "info", written(tmp_path, text))
    assert returncode == 1
    assert fields["status"] == status


def test_a_file_larger_than_any_mods_is_refused(report_tool, tmp_path):
    text = json.dumps(dict(CAMPAIGN_INFO, Padding="x" * 0x10000))
    returncode, fields = report(report_tool, "info", written(tmp_path, text))
    assert returncode == 1
    assert fields["status"] == "not a JSON object"


def test_every_truncation_is_refused_cleanly(report_tool, tmp_path):
    for kind, data in (("info", json.dumps(CAMPAIGN_INFO, indent="\t")), ("level", json.dumps(LEVEL, indent=2))):
        for length in range(len(data)):
            returncode, _ = report(report_tool, kind, written(tmp_path, data[:length]))
            # (a prefix that is a whole object, followed by nothing, is read)
            assert returncode in (0, 1)
        assert report(report_tool, kind, written(tmp_path, data))[0] == 0


def test_seeded_corruption_never_crashes_the_reader(report_tool, tmp_path):
    generator = random.Random(20261010)
    sources = [json.dumps(CAMPAIGN_INFO, indent="\t").encode(), json.dumps(LEVEL).encode()]
    for trial in range(300):
        data = bytearray(generator.choice(sources))
        for _ in range(generator.randint(1, 8)):
            choice = generator.random()
            offset = generator.randrange(len(data))
            if choice < 0.5:
                data[offset] = generator.randrange(256)
            elif choice < 0.75:
                data[offset:offset] = generator.choice([b"{", b"[", b"\"", b"\\", b"\\u", b",", b":"]) * generator.randint(1, 20)
            else:
                del data[offset:offset + generator.randint(1, 10)]
        report(report_tool, generator.choice(["info", "level"]), written(tmp_path, bytes(data)))
