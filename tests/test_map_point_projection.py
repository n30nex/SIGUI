import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_point_projection_is_registered_and_matches_map_math_contract():
    cmake = read("main/CMakeLists.txt")
    header = read("main/map/map_point_projection.h")
    source = read("main/map/map_point_projection.c")

    assert '"map/map_point_projection.c"' in cmake
    assert "d1l_map_point_project_e6" in header
    assert "center_lat_e6" in header
    assert "center_lon_e6" in header
    assert "pan_x_pixels" in header
    assert "pan_y_pixels" in header
    assert "D1L_MAP_VIEW_MIN_ZOOM" in source
    assert "D1L_MAP_VIEW_MAX_ZOOM" in source
    assert "D1L_MAP_TILE_PIXEL_SIZE" in source
    assert "D1L_MAP_MERCATOR_MAX_LAT_E7 850511288LL" in source
    assert "delta_x > half_world" in source
    assert "delta_x < -half_world" in source
    assert "bounded_screen_coordinate" in source


def test_point_projection_deterministic_native_vectors(tmp_path):
    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise AssertionError("A C compiler is required for the native map projection vectors")

    executable = tmp_path / "map_point_projection_test"
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "main"),
        str(ROOT / "main/map/map_point_projection.c"),
        str(ROOT / "tests/native/map_point_projection_test.c"),
        "-lm",
        "-o",
        str(executable),
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
    subprocess.run([str(executable)], cwd=ROOT, check=True, capture_output=True, text=True)
