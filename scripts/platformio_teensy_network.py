"""Keep Ethernet/lwIP code in cached flash, preserving RAM1 for the shell heap."""
Import("env")
from pathlib import Path
framework = Path(env.PioPlatform().get_package_dir("framework-arduinoteensy-ts"))
source = framework / "teensy4" / "imxrt1062_t41.ld"
script = source.read_text()
marker = "\t\t*(.flashmem*)"
assert script.count(marker) == 1, "Pinned Teensy linker layout changed"
script = script.replace(marker, marker + "\n\t\t*libQNEthernet.a:*(.text* .rodata*)")
output = Path(env.subst("$BUILD_DIR")) / "teensy41_network.ld"
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(script)
env.Replace(LDSCRIPT_PATH=str(output.resolve()))
# The pinned platform expands -T before post scripts run.
flags = list(env["LINKFLAGS"])
index = flags.index("-T") + 1
assert Path(str(flags[index])).name == "imxrt1062_t41.ld"
flags[index] = str(output.resolve())
env.Replace(LINKFLAGS=flags)
