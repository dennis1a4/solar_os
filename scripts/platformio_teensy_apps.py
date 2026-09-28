"""Generate the shared package-gated manual used by the text pager."""
Import("env")
from pathlib import Path
import importlib.util

root = Path(env.subst("$PROJECT_DIR"))
generated = Path(env.subst("$BUILD_DIR")) / "manual-generated"
generated.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location("teensy_manual", root / "scripts/generate_manual.py")
manual = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manual)
pages = manual.load_pages(root / "doc/manual", root / "packages/solar_os_packages.toml")
# Embed focused references for these apps. The full desktop/ESP manual contains
# large unrelated service pages; it need not consume the port's firmware space.
selected = {"app." + name for name in ("calc", "edit", "python", "aplay", "arecord",
                                      "ssh", "files", "less", "notes", "sheet", "plot", "playground", "view", "invaders")}
output = manual.render_header([page for page in pages if page["id"] in selected], root / "doc/manual")
header = generated / "solar_os_manual_data.h"
if not header.exists() or header.read_text() != output:
    header.write_text(output)
env.Append(CPPPATH=[str(generated)])
