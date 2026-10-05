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
selection_spec = importlib.util.spec_from_file_location("teensy_manual_selection", root / "scripts/ports/teensy41_manual.py")
selection = importlib.util.module_from_spec(selection_spec)
selection_spec.loader.exec_module(selection)
output = manual.render_header(selection.select_pages(pages, manual.markdown_to_terminal_text), root / "doc/manual", embed_all=True)
header = generated / "solar_os_manual_data.h"
if not header.exists() or header.read_text() != output:
    header.write_text(output)
env.Append(CPPPATH=[str(generated)])
