"""Keep Ethernet/lwIP code in cached flash, preserving RAM1 for the shell heap."""
Import("env")
from pathlib import Path
framework = Path(env.PioPlatform().get_package_dir("framework-arduinoteensy-ts"))
source = framework / "teensy4" / "imxrt1062_t41.ld"
script = source.read_text()
marker = "\t\t*(.flashmem*)"
assert script.count(marker) == 1, "Pinned Teensy linker layout changed"
script = script.replace(marker, marker + "\n\t\t*libQNEthernet.a:*(.text* .rodata*)"
                        + "\n\t\t*network_socket.cpp.o(.text* .rodata*)"
                        + "\n\t\t*python_network.c.o(.text* .rodata*)")
script = script.replace(marker, marker + "\n\t\t*libcjson.a:*(.text* .rodata*)")
script = script.replace(marker, marker + "\n\t\t*libLittleFS.a:*(.text* .rodata*)")
script = script.replace(marker, marker + "\n\t\t*libssh2.a:*(.text* .rodata*)\n\t\t*libmbedcrypto.a:*(.text* .rodata*)")
script = script.replace(marker, marker + "\n\t\t*libminiz.a:*(.text* .rodata*)\n\t\t*solar_os_files.c.o(.text* .rodata*)\n\t\t*solar_os_zip.c.o(.text* .rodata*)")
script = script.replace(marker, marker + "\n\t\t*libRA8875.a:*(.text* .rodata*)\n\t\t*libUSBHost_t36.a:*(.text* .rodata*)")
script = script.replace(marker, marker + "\n\t\t*libgfx_*.a:*(.text* .rodata*)")
# Calendar conversion is not latency-critical. Keep newlib timezone helpers in
# flash as well, avoiding a whole additional 32 KiB ITCM bank for the clock.
for member in ("mktime*", "lcltime*", "tz*", "gettzinfo", "setenv*", "svfiscanf", "siscanf"):
    script = script.replace(marker, marker + "\n\t\t*libc.a:libc_a-" + member + ".o(.text* .rodata*)")
# Job utilization pulls in software float helpers; keep them in flash to avoid
# consuming an extra 32 KiB ITCM bank and starving startup task allocations.
script = script.replace(marker, marker + "\n\t\t*libgcc.a:*(.text* .rodata*)\n\t\t*libm.a:*(.text* .rodata*)")
for obj in ("solar_os_ramfs.c", "ramfs_commands.cpp", "network_diagnostics.cpp", "hardware.cpp", "hardware_commands.cpp", "solar_os_resources.c", "solar_os_com.c", "buses.cpp", "python.c", "schedule_command.c", "schedule_services.c", "solar_os_jobs.c", "solar_os_scheduler.c", "solar_os_schedule.c", "workstation.cpp", "solar_os_shell_manual.c", "solar_os_docs_app.c", "solar_os_curl.c", "telnet_console.cpp", "solar_os_telnet_codec.c", "solar_os_scope.c", "solar_os_scope_model.c", "scope_capture.cpp", "ADC.cpp", "ADC_Module.cpp", "solar_os_power_app.c", "solar_os_pd_power.c", "solar_os_clock.c", "clock_services.c", "solar_os_timezone.c", "solar_os_obd_app.c", "solar_os_can.c", "solar_os_isotp.c", "solar_os_obd.c", "solar_os_obd_demo.c", "solar_os_mqtt_explorer.c", "solar_os_mqtt_capture.c", "solar_os_mqtt_capture_model.c", "solar_os_mqtt_wire.c", "u8g2*", "u8x8*", "stb_image_port.c", "solar_os_invaders.c", "graphics.cpp", "python_gfx.c", "python_hardware.c", "python_hardware_io.cpp", "solar_os_view.c", "solar_os_gfx.c", "solar_os_gfx_icons.c", "solar_os_vector.c", "solar_os_plot.c", "solar_os_playground_app.c", "solar_os_playground.c", "solar_os_json.c", "http_client.cpp", "tls_roots.cpp", "shell_dual.cpp", "lcd_terminal.cpp", "peripherals.cpp",
            "synth_backend.cpp", "synth_app.c", "solar_os_dsp.c", "solar_os_synth_voice.c", "settings.c", "solar_os_identity.c", "solar_os_less.c", "solar_os_notes.c", "solar_os_sheet.c", "solar_os_manual.c",
            "ssh_platform.cpp", "solar_os_ssh.c", "solar_os_ssh_transport.c", "solar_os_ssh_keys.c", "solar_os_crypto.c", "flash_storage.cpp", "flash_policy.c", "shell_storage.cpp", "usb_storage.cpp", "sd_storage.cpp", "MassStorageDriver.cpp", "network.cpp", "net_transport.c", "network_registry.cpp", "python_solaros_net.c",
            "solar_os_tui.c", "solar_os_tui_widgets.c", "solar_os_completion.c", "solar_os_completion_files.c", "solar_os_edit.c", "solar_os_calc.c",
            "solar_os_ltop.c", "solar_os_webradio.c", "solar_os_media_widgets.c", "solar_os_signal_widgets.c", "solar_os_dsp.c", "solar_os_webradio_catalog.c", "sshkey_command.cpp", "midi_commands.cpp", "serial_terminal.cpp", "radio_audio.cpp", "pd_controller.cpp", "solar_os_stusb4500.c", "solar_os_midi_codec.c", "solar_os_shell.c", "solar_os_shell_fs.c", "solar_os_shell_io.c", "solar_os_expr.c",
            "solar_os_net_session.c", "solar_os_network.c", "solar_os_shell_network_status.c"):
    script = script.replace(marker, marker + "\n\t\t*" + obj + ".o(.text* .rodata*)")
# Keep enough DTCM for the dynamically allocated console stack, startup state,
# and the 8 KiB core stack guard. Catch an ITCM-bank regression at link time.
script += '\nASSERT((_estack - _ebss) >= 73728, "Teensy RAM1 startup headroom below 72 KiB")\n'
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
env.Depends(env.subst("$BUILD_DIR/${PROGNAME}.elf"), str(output.resolve()))

env.Append(LINKFLAGS=["-Wl,-Map," + env.subst("$BUILD_DIR/firmware.map")])
