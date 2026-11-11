"""Keep Ethernet/lwIP code in cached flash, preserving RAM1 for the shell heap."""
Import("env")
from pathlib import Path
# QNEthernet's include order prefers its own lwipopts.h over project headers.
# Append a guarded extension to the pinned dependency, reproducibly on builds.
qnet_opts = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "QNEthernet/src/lwipopts.h"
if qnet_opts.exists():
    original = qnet_opts.read_text()
    extension_marker = "// SolarOS Wi-Fi multi-interface configuration"
    base = original.split(extension_marker)[0].rstrip() + "\n"
    extension = (Path(env.subst("$PROJECT_DIR")) /
                 "src/platform/imxrt1062/teensy41/qnethernet_wifi_opts.h").read_text()
    configured = base + "\n" + extension
    if configured != original:
        qnet_opts.write_text(configured)
# The pinned driver and Ping class also hard-code checksum offload rather
# than consulting lwIP options. Keep the software-checksum build consistent.
if qnet_opts.exists():
    root = qnet_opts.parent
    ping = root / "qnethernet/QNPing.cpp"
    source_text = ping.read_text()
    old = "#if QNETHERNET_ENABLE_RAW_FRAME_SUPPORT || \\\n    !defined(QNETHERNET_INTERNAL_DRIVER_TEENSY41)"
    new = "#if CHECKSUM_GEN_ICMP"
    # Restrict replacement to the two checksum guards inside send().
    if old in source_text:
        assert source_text.count(old) == 2, "Pinned QNPing checksum guards changed"
        ping.write_text(source_text.replace(old, new))
    else:
        assert source_text.count(new) == 2, "Pinned QNPing checksum guards changed"
    driver = root / "qnethernet/drivers/driver_teensy41.cpp"
    source_text = driver.read_text()
    old = """#if !QNETHERNET_ENABLE_RAW_FRAME_SUPPORT
                          | kEnetTxBdProtChecksum
                          | kEnetTxBdIpHdrChecksum
#endif  // !QNETHERNET_ENABLE_RAW_FRAME_SUPPORT"""
    new = """#if !CHECKSUM_GEN_UDP || !CHECKSUM_GEN_TCP || !CHECKSUM_GEN_ICMP
                          | kEnetTxBdProtChecksum
#endif
#if !CHECKSUM_GEN_IP
                          | kEnetTxBdIpHdrChecksum
#endif  // SolarOS: descriptor flags match software checksum options"""
    if old in source_text:
        assert source_text.count(old) == 1, "Pinned ENET checksum descriptor changed"
        driver.write_text(source_text.replace(old, new))
    else:
        assert new in source_text, "Pinned ENET checksum descriptor changed"
# QNEthernet normally closes both sides on FIN. DNP3 relays opt in to keeping
# the write side alive; the owner hook also covers FIN before listener accept.
if qnet_opts.exists():
    manager = qnet_opts.parent / "qnethernet/internal/ConnectionManager.cpp"
    manager_text = manager.read_text()
    hook_marker = "// SolarOS opt-in TCP half-close"
    if hook_marker not in manager_text:
        include = '#include "QNEthernet.h"'
        assert manager_text.count(include) == 1
        manager_text = manager_text.replace(include, include + '\n#if SK_DNP3\n' + hook_marker + '\nextern "C" bool sk_net_tcp_fin(void *);\nextern "C" void sk_net_tcp_forget(void *);\n#endif')
        guard = '  // Check for errors and null packets'
        assert manager_text.count(guard) == 1
        manager_text = manager_text.replace(guard, '#if SK_DNP3\n  if ((p == nullptr) && (err == ERR_OK) && sk_net_tcp_fin(tpcb)) return ERR_OK;\n  if (err != ERR_OK) sk_net_tcp_forget(tpcb);\n#endif\n\n' + guard)
        error_guard = '  if ((holder->state != nullptr) && (err != ERR_OK)) {'
        assert manager_text.count(error_guard) == 1
        manager_text = manager_text.replace(error_guard, error_guard + '\n#if SK_DNP3\n    sk_net_tcp_forget(holder->state->pcb);\n#endif')
        manager.write_text(manager_text)
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
# The small display uses synchronous SPI, not its optional DMA framebuffer.
# Keep its drawing/font code in cached flash instead of taking another ITCM bank.
script = script.replace(marker, marker + "\n\t\t*libST7735_t3.a:*(.text* .rodata*)")
script = script.replace(marker, marker + "\n\t\t*libFlexIO_t4.a:*(.text* .rodata*)")
script = script.replace(marker, marker + "\n\t\t*libgfx_*.a:*(.text* .rodata*)")
# Calendar conversion is not latency-critical. Keep newlib timezone helpers in
# flash as well, avoiding a whole additional 32 KiB ITCM bank for the clock.
for member in ("mktime*", "lcltime*", "tz*", "gettzinfo", "setenv*", "svfiscanf", "siscanf"):
    script = script.replace(marker, marker + "\n\t\t*libc.a:libc_a-" + member + ".o(.text* .rodata*)")
# Job utilization pulls in software float helpers; keep them in flash to avoid
# consuming an extra 32 KiB ITCM bank and starving startup task allocations.
script = script.replace(marker, marker + "\n\t\t*libgcc.a:*(.text* .rodata*)\n\t\t*libm.a:*(.text* .rodata*)")
for obj in ("dmm.cpp", "adc_guard.cpp", "motor_module.cpp", "usb_host_power.cpp", "wifi_network.cpp", "wireless_bridge.cpp", "mesh_service.cpp", "solar_os_mesh_view.c", "mesh_client.c", "mesh_bridge.c", "mesh_meshcore.c", "mesh_meshtastic.c", "meshtastic_client.pb.c", "meshcore_companion.c", "pb_common.c", "pb_decode.c", "pb_encode.c", "gameboy_presenter.cpp", "solar_os_gameboy.c", "solar_os_gameboy_rom.c", "solar_os_gameboy_video.c", "solar_os_gameboy_audio.c", "ftp_socket.cpp", "solar_os_ftp.c", "solar_os_ftp_app.c", "solar_os_ftpd_job.c", "solar_os_ramfs.c", "ramfs_commands.cpp", "network_diagnostics.cpp", "hardware.cpp", "hardware_commands.cpp", "solar_os_resources.c", "solar_os_com.c", "buses.cpp", "python.c", "schedule_command.c", "schedule_services.c", "solar_os_jobs.c", "solar_os_scheduler.c", "solar_os_schedule.c", "workstation.cpp", "solar_os_shell_manual.c", "solar_os_docs_app.c", "solar_os_curl.c", "telnet_console.cpp", "solar_os_telnet_codec.c", "solar_os_scope.c", "solar_os_scope_model.c", "scope_capture.cpp", "ADC.cpp", "ADC_Module.cpp", "solar_os_power_app.c", "solar_os_pd_power.c", "solar_os_clock.c", "clock_services.c", "solar_os_timezone.c", "solar_os_obd_app.c", "solar_os_can.c", "solar_os_isotp.c", "solar_os_obd.c", "solar_os_obd_demo.c", "solar_os_mqtt_explorer.c", "solar_os_mqtt_capture.c", "solar_os_mqtt_capture_model.c", "solar_os_mqtt_wire.c", "u8g2*", "u8x8*", "stb_image_port.c", "solar_os_invaders.c", "graphics.cpp", "python_gfx.c", "python_hardware.c", "python_hardware_io.cpp", "solar_os_view.c", "solar_os_gfx.c", "solar_os_gfx_icons.c", "solar_os_vector.c", "solar_os_plot.c", "solar_os_playground_app.c", "solar_os_playground.c", "solar_os_json.c", "http_client.cpp", "tls_roots.cpp", "shell_dual.cpp", "lcd_terminal.cpp", "peripherals.cpp",
            "synth_backend.cpp", "synth_app.c", "solar_os_dsp.c", "solar_os_synth_voice.c", "settings.c", "solar_os_identity.c", "solar_os_less.c", "solar_os_notes.c", "solar_os_sheet.c", "solar_os_manual.c",
            "ssh_platform.cpp", "solar_os_ssh.c", "solar_os_ssh_transport.c", "solar_os_ssh_keys.c", "solar_os_crypto.c", "flash_storage.cpp", "flash_policy.c", "shell_storage.cpp", "usb_storage.cpp", "sd_storage.cpp", "MassStorageDriver.cpp", "network.cpp", "net_transport.c", "network_registry.cpp", "python_solaros_net.c",
            "solar_os_tui.c", "solar_os_tui_widgets.c", "solar_os_completion.c", "solar_os_completion_files.c", "solar_os_edit.c", "solar_os_calc.c",
            "solar_os_ltop.c", "solar_os_webradio.c", "solar_os_player.c", "solar_os_recorder.c", "player_folder.c", "solar_os_storage_browser.c", "solar_os_media_widgets.c", "solar_os_signal_widgets.c", "solar_os_dsp.c", "solar_os_webradio_catalog.c", "sshkey_command.cpp", "midi_commands.cpp", "serial_terminal.cpp", "radio_audio.cpp", "pd_controller.cpp", "power_shutdown.cpp", "power_button.cpp", "solar_os_stusb4500.c", "solar_os_midi_codec.c", "solar_os_shell.c", "solar_os_shell_fs.c", "solar_os_shell_io.c", "solar_os_expr.c",
            "solar_os_net_session.c", "solar_os_network.c", "solar_os_shell_network_status.c"):
    script = script.replace(marker, marker + "\n\t\t*" + obj + ".o(.text* .rodata*)")
# DNP3 decoding, simulation and UI are bounded worker tasks, not interrupt code.
for obj in ("solar_os_app_registry.c", "organizer.cpp", "solar_os_organizer_apps.c", "solar_os_organizer_model.c", "solar_os_organizer_io.c", "solar_os_convert.c", "solar_os_library.c", "solar_os_puzzle_apps.c", "solar_os_puzzles.c", "solar_os_units.c", "solar_os_reader.c", "solar_os_writer.c", "solar_os_writer_buffer.c", "solar_os_writer_files.c",
            "solar_os_doc.c", "solar_os_epub.c", "solar_os_html.c", "solar_os_xml.c",
            "solar_os_dnp3_app.c", "solar_os_dnp3_lab.c", "solar_os_dnp3_wire.c",
            "solar_os_dnp3_bridge.c", "dnp3_service.cpp", "crc.c", "functions.c", "decode.c",
            "motion_service.cpp", "motion_commands.c", "solar_os_imu.c", "solar_os_mag.c",
            "solar_os_shell_imu.c", "solar_os_shell_mag.c"):
    script = script.replace(marker, marker + "\n\t\t*" + obj + ".o(.text* .rodata*)")
# Keep DTCM for startup/network allocations and the 8 KiB core stack guard.
# The console stack is now in OCRAM. Catch an ITCM-bank regression at link time.
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
