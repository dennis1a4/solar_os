"""Build the shared software graphics/fonts and image decoders for RA8875."""
Import("env", "projenv")
from pathlib import Path
root = Path(env.subst("$PROJECT_DIR"))
includes = [root / "components/u8g2/src/clib", root / "components/stb_image/include",
            root / "components/webp_decoder/include"]
for build in (env, projenv):
    build.Append(CPPPATH=[str(p) for p in includes])
env.Append(LINKFLAGS=["-Wl,--wrap=solar_os_gfx_prepare_surface", "-Wl,--wrap=solar_os_gfx_release_surface"])
# Keep these libraries in cached program flash, preserving internal RAM.

for name, path, pattern in [
    ("u8g2", "components/u8g2/src", ["+<clib/*.c>", "-<clib/u8g2_d_*.c>", "-<clib/u8g2_d_setup.c>", "-<clib/u8x8_d_*.c>", "-<clib/u8g2_fonts.c>", "-<clib/u8x8_fonts.c>", "+<fonts/*.c>"]),
    ("fonts", "fonts/build/u8g2", ["+<*.c>"]),
    ("stb", "components/stb_image", ["+<stb_image_port.c>"]),
]:
    env.Append(LIBS=[env.BuildLibrary(env.subst("$BUILD_DIR") + "/gfx_" + name, str(root/path), src_filter=pattern)])
# Object placement is handled by the linker script, including generated fonts.
# PlatformIO's bundled CLI 2.2 fails at the 0x60100000 HEX boundary. Use the
# pinned upstream loader and explicitly select the 4.1's 7.75 MiB user flash.
import sys
if sys.platform.startswith("linux"):
    env.Replace(UPLOADCMD='"' + sys.executable + '" "$PROJECT_DIR/scripts/ports/upload_teensy41.py" $SOURCES')
