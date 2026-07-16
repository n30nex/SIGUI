import os
import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]


def test_production_identity_exchange_rejects_adversarial_points(tmp_path):
    executable = tmp_path / "meshcore_identity_exchange_test"
    ed = ROOT / "third_party/MeshCore/lib/ed25519"
    sources = [
        ROOT / "tests/native/meshcore_identity_exchange_test.c",
        ROOT / "main/mesh/meshcore_identity_exchange.c",
        ROOT / "overlays/meshcore_ed25519_defined/fe.c",
        ROOT / "overlays/meshcore_ed25519_defined/ge.c",
        ROOT / "overlays/meshcore_ed25519_defined/sc.c",
        ed / "key_exchange.c",
        ed / "keypair.c",
        ed / "sha512.c",
    ]
    subprocess.run(
        [
            os.environ.get("CC", "gcc"), "-std=c11", "-Wall", "-Wextra",
            "-Werror", "-I", str(ROOT / "main"), "-I", str(ed),
            *map(str, sources), "-Wl,--wrap=ed25519_key_exchange",
            "-o", str(executable),
        ],
        cwd=ROOT, check=True, capture_output=True, text=True,
    )
    completed = subprocess.run(
        [str(executable)], cwd=ROOT, check=True, capture_output=True, text=True,
    )
    assert completed.stdout.strip() == "meshcore_identity_exchange_test: ok"


def test_production_and_oracle_share_strict_and_zero_secret_gates():
    production = (ROOT / "main/mesh/meshcore_identity_exchange.c").read_text()
    oracle = (ROOT / "tests/meshcore_oracle/meshcore_oracle.cpp").read_text()
    for source in (production, oracle):
        assert "d1l_ed25519_encoded_point_is_strict" in source
        assert "ed25519_key_exchange" in source
    assert "nonzero == 0U" in production
    assert "secret_or == 0U" in oracle
