"""Pinned JSON dependency and board facts for the Teensy Playground profile."""
Import("env", "projenv")
from pathlib import Path
import subprocess
root = Path(env.subst("$PROJECT_DIR"))
path = root / ".pio/teensy-ssh-deps/cjson"
revision = "c859b25da02955fef659d658b8f324b5cde87be3"  # cJSON 1.7.19
if not path.exists():
    subprocess.run(["git", "clone", "--no-checkout",
                    "https://github.com/DaveGamble/cJSON.git", str(path)], check=True)
    subprocess.run(["git", "-C", str(path), "checkout", "--detach", revision], check=True)
actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
assert actual == revision
for build in (env, projenv):
    build.Append(CPPPATH=[str(path)])
    build.Append(CPPDEFINES=[("SOLAR_OS_VERSION", '\\"' + (root / "version.txt").read_text().strip() + '\\"')])
    for capability in ("PSRAM", "DISPLAY", "GFX", "CDC", "SD", "KEY"):
        build.Append(CPPDEFINES=[("SOLAR_OS_BOARD_HAS_" + capability, 1)])
cjson = env.Clone()
cjson.Append(CPPDEFINES=[("CJSON_NESTING_LIMIT", 32)])
env.Append(LIBS=[cjson.BuildLibrary(env.subst("$BUILD_DIR") + "/cjson", str(path), src_filter="+<cJSON.c>")])
