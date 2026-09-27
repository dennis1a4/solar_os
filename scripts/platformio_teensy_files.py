"""Build the shared ZIP service's pinned miniz dependency in program flash."""
Import("env", "projenv")
from pathlib import Path
import subprocess

root = Path(env.subst("$PROJECT_DIR"))
path = root / ".pio/teensy-files-deps/miniz"
revision = "77d0dce8627735138c51770d1799a1ef48f2117d"  # miniz 3.1.2
if not path.exists():
    subprocess.run(["git", "clone", "--no-checkout",
                    "https://github.com/richgel999/miniz.git", str(path)], check=True)
    subprocess.run(["git", "-C", str(path), "checkout", "--detach", revision], check=True)
actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
assert actual == revision, f"Unexpected miniz revision: {actual}"
generated = Path(env.subst("$BUILD_DIR")) / "miniz-config"
generated.mkdir(parents=True, exist_ok=True)
(generated / "miniz_export.h").write_text("#pragma once\n#define MINIZ_EXPORT\n")
# SolarOS owns the archive container, file I/O and caller-allocated buffers.
defines = ["MINIZ_NO_ARCHIVE_APIS", "MINIZ_NO_STDIO", "MINIZ_NO_TIME",
           "MINIZ_NO_ZLIB_APIS", "MINIZ_NO_MALLOC"]
for build_env in (env, projenv):
    build_env.Append(CPPPATH=[str(path), str(generated)], CPPDEFINES=defines)
env.Append(LIBS=[env.BuildLibrary(env.subst("$BUILD_DIR") + "/miniz", str(path),
    src_filter=["-<*>", "+<miniz.c>", "+<miniz_tdef.c>", "+<miniz_tinfl.c>"])])
