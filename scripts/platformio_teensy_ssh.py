"""Build pinned SSH/crypto sources in program flash, with PSRAM allocation."""
Import("env", "projenv")
from pathlib import Path
import subprocess
import hashlib
root = Path(env.subst("$PROJECT_DIR"))
deps = root / ".pio/teensy-ssh-deps"
for name, url, revision in (
    ("libssh2", "https://github.com/libssh2/libssh2.git", "2e1717456b8dd4c980e8e48d6dbfec524c2e62d1"),
    ("mbedtls", "https://github.com/Mbed-TLS/mbedtls.git", "068ff080b369adfac81509f9b57b2afabaf82dc5"),
):
    path = deps / name
    if not path.exists():
        subprocess.run(["git", "clone", "--no-checkout", url, str(path)], check=True)
        subprocess.run(["git", "-C", str(path), "checkout", "--detach", revision], check=True)
    actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
    assert actual == revision, f"Unexpected {name} revision: {actual}"
env.Append(CPPPATH=[str(deps / "libssh2/include"), str(deps / "mbedtls/include")])
config = root / "src/platform/imxrt1062/teensy41/ssh_compat/teensy_mbedtls_config.h"
# SCons cannot discover a header included through MBEDTLS_CONFIG_FILE. Make
# that configuration an explicit input to every crypto consumer's signature.
defines = [("MBEDTLS_CONFIG_FILE", '\\"teensy_mbedtls_config.h\\"'),
           ("SOLAR_CRYPTO_CONFIG_ID", "0x" + hashlib.sha256(config.read_bytes()).hexdigest()[:8])]
env.Append(CPPDEFINES=defines)
projenv.Append(CPPPATH=[str(deps / "libssh2/include"), str(deps / "mbedtls/include")],
               CPPDEFINES=defines)
crypto = env.Clone()
crypto.Append(CPPPATH=[str(deps / "mbedtls/library")])
ssh = env.Clone()
ssh.Append(CPPDEFINES=["HAVE_CONFIG_H", "LIBSSH2_MBEDTLS"])
ssh.Append(CPPPATH=[str(deps / "libssh2/src")])
env.Append(LIBS=[ssh.BuildLibrary(env.subst("$BUILD_DIR") + "/ssh2", str(deps / "libssh2/src")),
                 crypto.BuildLibrary(env.subst("$BUILD_DIR") + "/mbedcrypto", str(deps / "mbedtls/library"))])
