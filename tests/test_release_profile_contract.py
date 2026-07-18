import os
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_core_profile_is_the_deterministic_build_default():
    root_cmake = read("CMakeLists.txt")
    component_cmake = read("main/CMakeLists.txt")

    assert 'set(D1L_RELEASE_PROFILE "core_1_0" CACHE STRING' in root_cmake
    assert 'set(D1L_SD_HISTORY_MODE "conditional" CACHE STRING' in root_cmake
    assert '"app/release_profile.c"' in component_cmake
    assert "D1L_RELEASE_PROFILE=${D1L_RELEASE_PROFILE_DEFINE}" in component_cmake
    assert "D1L_SD_HISTORY_MODE=${D1L_SD_HISTORY_MODE_DEFINE}" in component_cmake


def test_release_profile_is_one_immutable_authority_with_no_runtime_setter():
    header = read("main/app/release_profile.h")
    source = read("main/app/release_profile.c")
    app_header = read("main/app/app_model.h")
    app_source = read("main/app/app_model.c")

    assert "D1L_RELEASE_PROFILE_CORE_1_0" in header
    assert "d1l_release_feature_available" in header
    assert "d1l_release_profile_set" not in header
    assert "d1l_release_sd_history_mode_set" not in header
    assert '.map = false' in source
    assert '.wifi_user_control = false' in source
    assert '.ble = false' in source
    assert '.multi_channel_management = false' in source
    assert '.admin = false' in source
    assert '.observer_mqtt = false' in source
    assert '.signed_update = false' in source
    assert '.location = false' in source
    assert "d1l_release_capabilities_t release_capabilities;" in app_header
    assert "snapshot->release_profile = d1l_release_profile_name();" in app_source
    assert "snapshot->release_capabilities = *d1l_release_capabilities();" in app_source


def test_release_profile_native_matrix(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for release profile tests")

    cases = (
        (
            "core_conditional",
            "D1L_RELEASE_PROFILE_CORE_1_0",
            "D1L_SD_HISTORY_MODE_CONDITIONAL",
            ("EXPECT_CORE=1", "EXPECT_FULL=0", "EXPECT_SD_CONDITIONAL=1",
             "EXPECT_SD_SUPPORTED=0"),
        ),
        (
            "core_supported_sd",
            "D1L_RELEASE_PROFILE_CORE_1_0",
            "D1L_SD_HISTORY_MODE_SUPPORTED_OPTIONAL",
            ("EXPECT_CORE=1", "EXPECT_FULL=0", "EXPECT_SD_CONDITIONAL=0",
             "EXPECT_SD_SUPPORTED=1"),
        ),
        (
            "development",
            "D1L_RELEASE_PROFILE_DEVELOPMENT",
            "D1L_SD_HISTORY_MODE_DISABLED",
            ("EXPECT_CORE=0", "EXPECT_FULL=0", "EXPECT_SD_CONDITIONAL=0",
             "EXPECT_SD_SUPPORTED=0"),
        ),
        (
            "full",
            "D1L_RELEASE_PROFILE_FULL_FEATURE",
            "D1L_SD_HISTORY_MODE_SUPPORTED_OPTIONAL",
            ("EXPECT_CORE=0", "EXPECT_FULL=1", "EXPECT_SD_CONDITIONAL=0",
             "EXPECT_SD_SUPPORTED=1"),
        ),
    )
    for name, profile, sd_mode, expectations in cases:
        executable = tmp_path / (
            f"release_profile_{name}.exe"
            if os.name == "nt"
            else f"release_profile_{name}"
        )
        command = [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "main"),
            f"-DD1L_RELEASE_PROFILE={profile}",
            f"-DD1L_SD_HISTORY_MODE={sd_mode}",
            *(f"-D{item}" for item in expectations),
            str(ROOT / "main/app/release_profile.c"),
            str(ROOT / "tests/native/release_profile_test.c"),
            "-o",
            str(executable),
        ]
        subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
        completed = subprocess.run(
            [str(executable)], cwd=ROOT, check=True, capture_output=True, text=True
        )
        assert completed.stdout.strip() == "native release profile: ok"
