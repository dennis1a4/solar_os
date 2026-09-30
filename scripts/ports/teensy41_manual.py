"""Select accurate embedded documentation for the Teensy workstation shell."""
APP_NAMES = set('calc edit hexedit python aplay arecord ssh files less notes sheet plot playground view invaders mqttx obd clock pdpower scope help curl'.split())
COMMAND_NAMES = set('help man commands echo wait watch apps mem uptime clear setterm identity network audio cd ls cat sh mkdir rm mv cp exit reboot version board status top port df pwd session sessions zip unzip rtc date time flash sd usb lcd telnetd'.split())
# The reduced service adapters intentionally expose narrower contracts than ESP.
OVERRIDES = {
    'help': ('Browse the embedded manual.', 'help [TOPIC|status]\nUse arrows and Enter to browse; Q exits. Signed manual downloads are not integrated.\nUse commands for the command list.'),
    'mem': ('Show internal heap and PSRAM.', 'mem\nPrints free and total bytes. Allocation-policy diagnostics are not integrated.'),
    'uptime': ('Show uptime and calling task stack headroom.', 'uptime\nUptime is in milliseconds; stack-free is in words.'),
    'setterm': ('Configure persistent terminal preferences.', 'setterm\nsetterm size COLS ROWS\nsetterm startup auto|flash|sd\nsetterm timezone UTC|Manitoba|POSIX-TZ\nLCD size is fixed. Telnet geometry comes from the client. Manitoba is fixed UTC-5.'),
    'network': ('Inspect or start Ethernet.', 'network up\nnetwork down\nnetwork status\nnetwork routes\nEthernet starts explicitly. Wi-Fi and the upstream network settings TUI are not integrated.'),
    'audio': ('Inspect the audio shield and test output.', 'audio [status|tone|off|mictest new.wav]\nAudio needs the SGTL5000 shield; it was disconnected during LCD testing.'),
    'version': ('Show firmware identity.', 'version\nSolarOS source version and Teensy port identity.'),
    'board': ('Show board and processor.', 'board\nTeensy 4.1, i.MX RT1062, Cortex-M7 and current CPU frequency.'),
    'status': ('Show a compact system summary.', 'status\nFirmware, processor, uptime, memory, mounted-volume count and FreeRTOS task count.'),
    'top': ('Snapshot FreeRTOS tasks.', 'top\nTask state, priority, stack high-water headroom in bytes and cumulative CPU utilization.\nwatch -n 2 top repeats the snapshot. CPU is cumulative, not interval utilization.'),
    'port': ('List registered byte-stream ports.', 'port [list]\nShows names, owners and labels. UART terminal attachment is not integrated.'),
    'df': ('Show space on mounted volumes.', 'df\nLists total, used and free KiB for SD, QSPI flash and USB when mounted.\nFAT free-space scanning can take time on large media.'),
    'pwd': ('Print the calling shell directory.', 'pwd'),
    'session': ('Inspect the fixed console sessions.', 'session [list]\nLists USB, LCD and Telnet consoles, connection state, foreground app and directory.\nAn asterisk marks the calling console. Detached creation, switching and retained-app fg are not integrated.'),
    'sessions': ('List the fixed console sessions.', 'sessions\nAlias for session list. See man session.'),
    'rtc': ('Inspect or set the UTC hardware clock.', 'rtc\nrtc set UNIX_SECONDS\nAccepts UTC epochs from 2000 through the 32-bit RTC limit in 2106.\nDisplay timezone is configured separately with setterm timezone.'),
    'exit': ('Exit the remote shell or reset a local prompt.', 'exit\nTelnet disconnects. The fixed USB/LCD consoles remain available.'),
}

def select_pages(pages, render_text):
    selected = []
    for original in pages:
        kind, _, name = original['id'].partition('.')
        if not ((kind == 'app' and name in APP_NAMES) or (kind == 'command' and name in COMMAND_NAMES)):
            continue
        page = dict(original)
        if kind == 'command':
            # Upstream conditions describe ESP packages, not the Teensy allowlist.
            page['condition'] = {'date':'SK_CLOCK', 'time':'SK_CLOCK', 'rtc':'SK_PLAYGROUND',
                'session':'SK_LCD_CONSOLE', 'sessions':'SK_LCD_CONSOLE', 'lcd':'SK_LCD_CONSOLE',
                'telnetd':'SK_TELNETD', 'usb':'SK_USB_STORAGE', 'sd':'SK_SD_RECOVERY',
                'flash':'SK_QSPI_FLASH', 'audio':'SK_AUDIO_PLAYER', 'network':'SK_ETHERNET',
                'zip':'SK_FILES', 'unzip':'SK_FILES'}.get(name, '')
            page['packages_any'] = []
            page['aliases'] = [name]
            if name in OVERRIDES:
                summary, usage = OVERRIDES[name]
                page['summary'] = summary
                page['markdown'] = f'# {name}\n\n{summary}\n\n```text\n{usage}\n```\n'
                page['body'] = render_text(page['markdown'])
                page['contract'] = usage
        selected.append(page)
    # Port-specific builtins do not have an upstream derived topic.
    found = {p['id'] for p in selected}
    for name in sorted(COMMAND_NAMES):
        if f'command.{name}' not in found and name in OVERRIDES:
            summary, usage = OVERRIDES[name]
            markdown = f'# {name}\n\n{summary}\n\n```text\n{usage}\n```\n'
            selected.append(dict(id=f'command.{name}', title=f'{name} command', section='command',
                section_title='Commands', summary=summary, aliases=[name], keywords=name,
                body=render_text(markdown), contract=usage, markdown=markdown, packages_any=[]))
    return sorted(selected, key=lambda p: (p['section'], p['id']))
