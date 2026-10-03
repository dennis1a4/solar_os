"""Select accurate embedded documentation for the Teensy workstation shell."""
APP_NAMES = set('ltop webradio io com calc edit hexedit python aplay arecord ssh files less notes sheet plot playground view invaders mqttx obd clock pdpower scope help curl'.split())
COMMAND_NAMES = set('sshkey midi pd grep head wc ramfs gpio i2c spi uart expansion help man commands echo wait watch apps mem uptime clear setterm identity network audio cd ls cat sh mkdir rm mv cp exit reboot version board status top port df pwd session sessions fg bg tail close ping netscan ntp jobs job schedule zip unzip rtc date time flash sd usb lcd telnetd'.split())
# The reduced service adapters intentionally expose narrower contracts than ESP.
OVERRIDES = {
    'midi': ('Record and replay USB/UART MIDI.', 'midi status\nmidi record usb|slotN NEWFILE.smr\nmidi play FILE.smr usb|slotN\nCtrl+C stops. One operation at a time, 31250-baud UART, shared pin ownership. SMR1 timestamped files; not standard .mid. SysEx unsupported. USB cable 0. Recording overflow is reported. Physical MIDI validation pending.'),
    'pd': ('Monitor an STUSB4500 USB-PD controller.', 'pd status\npd open i2cN ADDRESS BOARD_MAX_MV BOARD_MAX_MA\npd request MV MA\npd close\nNo hardware access until open. Explicit open requests 5 V discovery. Set limits from the actual board ratings. Requests require an advertised fixed profile. Status distinguishes pending from confirmed contracts. Close stops monitoring; it does not change voltage. No NVM writes or boot restore. Hardware validation pending.'),
    'webradio': ('Play MP3 internet radio through the audio shield.', 'webradio [--tui] [URL]\nwebradio list|add NAME URL|remove NAME|reset\nTeensy uses the text UI and a bounded 128 KiB PSRAM audio buffer. Requires network up and SGTL5000 audio hardware. MP3 streams only; normal shell app exit stops playback.'),
    'ltop': ('Interactive interval CPU monitor.', 'ltop\nShows task CPU deltas, single-core load and memory. The FREE column shows minimum unused stack bytes on Teensy.'),
    'echo': ('Print text; also see shell composition examples.', 'echo [TEXT...]\nExamples: date; time\nntp && time\ncat /flash/log.txt | grep error | head -n 5\nComposition: 191 bytes, eight commands maximum. Operators inside quotes remain literal.\nPipes use PSRAM: 8 KiB per intermediate result, 16 KiB per active pipeline, 64 KiB global buffer limit. Overflow is an error.\nOnly synchronous builtins can be chained; interactive apps, watch, wait, sh and lifecycle commands must run separately. && requires an audited command exit status. Background script jobs still require one command per line.'),
    'grep': ('Filter bounded pipe input by literal text.', '... | grep LITERAL\nCase sensitive substring matching; no regular expressions. Exit status 1 means no matching lines. Requires pipe input.'),
    'head': ('Show the first lines of pipe input.', '... | head [-n COUNT]\nDefault: 10 lines. COUNT: 0..8192. Requires pipe input.'),
    'wc': ('Count piped lines, words and bytes.', '... | wc [-l|-w|-c]\nWithout an option prints newline, whitespace-separated word and byte counts. Requires pipe input.'),
    'ramfs': ('Create volatile PSRAM filesystems.', 'ramfs [status]\nramfs mount /NAME SIZE\nramfs unmount /NAME\nUp to four top-level mounts; names use letters, digits, dash or underscore. /sd, /flash, /usb and / are reserved. SIZE: 1024..4194304 bytes, optional k/m suffix. Keeps 512 KiB PSRAM free for apps. No automatic mount or persistence; reboot/unmount loses all files. Open files/directories block unmount. Close directory iterators before removing or renaming entries. Files, shell and Python use ordinary paths; df includes RAMFS. Use explicit /sd paths; unknown mount paths never fall through to SD.'),
    'io': ('Inspect Teensy pins, buses and claims.', 'io [pins|claims|buses|release]\nrelease restores this console’s GPIO inputs and closes its raw UART/expansion leases. Built-in device reservations and COM app leases cannot be released here. Fixed board routing; no pin remapping or saved runtime claims.'),
    'gpio': ('Claim and control a free header pin.', 'gpio [list|PIN]\ngpio mode PIN in|pullup|pulldown|out [0|1]\ngpio read PIN\ngpio write PIN 0|1\ngpio release PIN\nPins 0..41 only. Existing board/bus reservations cannot be overridden. Claims belong to the calling console; release/disconnect restores input mode.'),
    'i2c': ('Probe or transfer on a fixed I2C bus.', 'i2c [list]\ni2c scan i2c0|i2c1|i2c2\ni2c xfer BUS ADDRESS HEX|- RX_COUNT\nFixed 100 kHz; 7-bit addresses 8..0x77, up to 32 bytes each direction. HEX is contiguous hex bytes; - means no write bytes. Scan is cancellable with Ctrl+C/Esc and skips reserved addresses.'),
    'spi': ('Transfer through an owned expansion CS.', 'spi [list]\nspi xfer slot0|slot1|slot2 MODE HZ HEX\nClaim the expansion slot first. Mode 0..3, 100000..12000000 Hz, up to 128 bytes. Returns received bytes as hex; MSB first. Display chip selects cannot be claimed.'),
    'uart': ('Open a fixed expansion UART.', 'uart [list]\nuart open uart7|uart8|uart3 [BAUD]\nuart close BUS\nuart read BUS [COUNT]\nuart write BUS TEXT\n300..1000000 baud, 8N1; default 115200. Up to 128 bytes per read/write, nonblocking. Pin conflicts reject the whole claim. COM uses the same UART leases; close a raw UART before com. UART3 conflicts with the active display WAIT pin.'),
    'expansion': ('Inspect and lease expansion chip selects.', 'expansion [list]\nexpansion claim|release slot0|slot1|slot2\nA lease owns its SPI chip-select pin. Shared SPI signals remain bus-owned. UART leases are separate. Current display wiring reserves slot0/slot2; slot1 is available. No automatic driver discovery or hotplug binding.'),
    'help': ('Browse the embedded manual.', 'help [TOPIC|status]\nUse arrows and Enter to browse; Q exits. Signed manual downloads are not integrated.\nUse commands for the command list.'),
    'mem': ('Show internal heap and PSRAM.', 'mem\nPrints free and total bytes and allocation limits. Ordinary PSRAM allocations leave a 128 KiB system reserve. Pipe buffers have a 64 KiB global limit and are freed when the chain finishes. No automatic migration or swapping.'),
    'uptime': ('Show uptime and calling task stack headroom.', 'uptime\nUptime is in milliseconds; stack-free is in words.'),
    'setterm': ('Configure persistent terminal preferences.', 'setterm\nsetterm size COLS ROWS\nsetterm startup auto|flash|sd\nsetterm timezone UTC|Manitoba|POSIX-TZ\nLCD size is fixed. Telnet geometry comes from the client. Manitoba is fixed UTC-5.'),
    'network': ('Inspect or start Ethernet.', 'network up\nnetwork down\nnetwork status\nnetwork routes\nEthernet starts explicitly. Wi-Fi and the upstream network settings TUI are not integrated.'),
    'audio': ('Inspect the audio shield and test output.', 'audio [status|tone|off|mictest new.wav]\nAudio needs the SGTL5000 shield; it was disconnected during LCD testing.'),
    'version': ('Show firmware identity.', 'version\nSolarOS source version and Teensy port identity.'),
    'board': ('Show board and processor.', 'board\nTeensy 4.1, i.MX RT1062, Cortex-M7 and current CPU frequency.'),
    'status': ('Show a compact system summary.', 'status\nFirmware, processor, uptime, memory, mounted-volume count and FreeRTOS task count.'),
    'top': ('Snapshot FreeRTOS tasks.', 'top\nTask state, priority, stack high-water headroom in bytes and cumulative CPU utilization.\nwatch -n 2 top repeats the snapshot. CPU is cumulative, not interval utilization.'),
    'port': ('List registered byte-stream ports.', 'port [list]\nShows names, owners and labels. Use uart list and com uart7 or com uart8 for hardware terminals.'),
    'df': ('Show space on mounted volumes.', 'df\nLists total, used and free KiB for SD, QSPI flash, USB and RAMFS when mounted.\nFAT free-space scanning can take time on large media.'),
    'pwd': ('Print the calling shell directory.', 'pwd'),
    'session': ('Manage retained app sessions.', 'session [list]\nsession fg [ID]\nsession close ID\nCtrl+Z suspends a resumable app chain; up to four suspended chains per console.\nfg resumes the most recently suspended app on this console, or the named app on its owning console.\nThe owner must be at an empty shell prompt. close discards unsaved state and stops the whole chain.\nUSB/Telnet disconnect closes that console’s apps; local keyboard disconnect preserves LCD sessions.\nFixed consoles have IDs 1-3; app IDs start at 4. Dynamic shells and session create/send are not integrated.'),
    'ping': ('Send bounded ICMP echo requests.', 'ping HOST [1..999]\nDefaults to four probes, one second timeout/interval, 16 data bytes. Ctrl+C/Esc stops. Ethernet must be up. Reports replies, TTL, RTT and packet loss. Only one ICMP probe can be active at a time.'),
    'netscan': ('Scan TCP ports on IPv4 targets.', 'netscan HOST|A.B.C.D-E|CIDR [PORTS]\nPORTS is a comma list or range, at most 128 unique ports. IPv4 CIDR supports /24 through /32.\nDefaults: 22,23,53,80,443,1883,8080. Sequential TCP connects; 350 ms per port. Ctrl+C/Esc stops.\nReports open ports and probe count; no UDP scanning or service fingerprinting. Ethernet must be up.'),
    'ntp': ('Query network time or synchronize the UTC RTC.', 'ntp [-q] [SERVER [PORT]]\nDefaults: pool.ntp.org, UDP 123, five-second reply timeout. -q queries without changing the clock.\nValidates server address/port, response mode/version, stratum, synchronization, echoed request token and RTC range, including the 2036 NTP era rollover.\nFailed, rejected or cancelled requests leave the RTC unchanged. Successful sync preserves the display timezone.\nPlain NTP is unauthenticated; use a trusted server. No automatic periodic synchronization is started.'),
    'tail': ('Show the last lines of a file.', 'tail [-n 1..10000] FILE\nDefaults to ten lines. Reads a snapshot while a logger may continue writing; no -f mode yet. Ctrl+C cancels.'),
    'bg': ('Continue a suspended Python process in background.', 'bg [ID]\nCtrl+Z first. One Python VM is admitted. Only standalone text Python sessions detach; graphical apps remain foreground.\nDetached jobs retain their directory, VM, pending input and last 8191 output bytes. Use jobs, job output ID, fg ID, or job stop ID.\nStopping is cooperative; resources are retained until the interpreter exits. Detached processes survive console disconnect. Waiting input does not consume shell keys.'),
    'fg': ('Resume a retained app.', 'fg [ID]\nSee man session. Retained apps remain on their owning console; detached Python processes attach to the calling console.'),
    'close': ('Close an app session.', 'close ID\nStops retained app chains and discards unsaved state. For Python, requests cooperative cancellation and waits for worker cleanup before reaping. Fixed shells cannot be closed. Use fg first to save edited files.'),
    'sessions': ('List fixed consoles and retained apps.', 'sessions\nAlias for session list. See man session.'),
    'jobs': ('List Python processes and background script jobs.', 'jobs\nOne detachable Python process with a numeric session ID, plus four cooperative script slots: script0 through script3.\nPython states include foreground, suspended, running, waiting-input, stopping and done. TIME is elapsed wall time. Jobs survive console disconnect but not reboot.'),
    'job': ('Run and inspect background scripts.', 'job start script /path.sh\njob start script0 /path.sh\njob status [script0|ID]\njob output script0|ID\njob stop script0|ID\njob kill ID\nNumeric IDs refer to Python processes. Stop is cooperative; cleanup occurs on the VM worker. Reap a completed job with fg ID or job stop ID.\nThe alias script picks a free slot; an explicit slot restarts that job. Output retains the last 4095 bytes.\nSupported: echo wait pwd cd ls cat mkdir cp mv rm version board status mem uptime top port date time.\nEach job starts at / with an isolated directory. One line per tick; wait yields cooperatively.\nNo interactive apps, nested scripts, pipes or redirection. Cancellation happens between commands; storage operations can delay other consoles.\nCommand diagnostics appear in the log; completion does not guarantee each file operation succeeded.'),
    'schedule': ('Schedule scripts and alarms.', 'schedule list\nschedule show NAME\nschedule add NAME in SECONDS run /path.sh\nschedule add NAME every SECONDS run /path.sh\nschedule add NAME daily HH:MM[:SS] run /path.sh\nschedule run NAME\nschedule enable NAME\nschedule disable NAME\nschedule remove NAME\nAlso: schedule add NAME at YYYY-MM-DD HH:MM[:SS] run /path.sh\nschedule add NAME weekly mon,wed HH:MM[:SS] run /path.sh\nReplace run /path.sh with alarm for an audible alarm; schedule stop silences it. Persistent entries use /flash/.solar/schedule.bin.\nScript triggers use the same four job slots; a full pool increments skipped. Calendar schedules need a valid RTC and use the configured timezone.\nNo power-off wake or hardware alarm IRQ. Job output is volatile; relative countdowns restart after reboot.'),
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
        if kind == 'app' and name == 'io':
            summary, usage = OVERRIDES['io']
            page['condition'] = 'SK_HW_RESOURCES'
            page['packages_any'] = []
            page['summary'] = summary
            page['markdown'] = '# IO\n\n```text\n' + usage + '\n```\n'
            page['body'] = render_text(page['markdown'])
            page['contract'] = usage
        if kind == 'app' and name == 'less':
            usage = ('less FILE\ncommands | less\n'
                     'Pipe input is limited to 8192 bytes in PSRAM. less must end the command line.\n'
                     'Arrows or j/k scroll; Space/b page; g/G beginning/end; / searches; n/N repeat; q exits.')
            page['summary'] = 'Page through a file or bounded pipe output.'
            page['markdown'] = '# Less\n\n```text\n' + usage + '\n```\n'
            page['body'] = render_text(page['markdown'])
            page['contract'] = usage
        if kind == 'app' and name == 'com':
            usage = 'com [--hex] [uart7|uart8|uart3]\nDefault uart7; configured baud, initially 115200, 8N1. Shared COM app; Ctrl+] exits, Ctrl+Z suspends while retaining its UART lease, fg resumes. RX is bounded; suspended sessions may lose data if the UART ring fills. Other consoles cannot steal the lease. --autobaud is unavailable. Hardware loopback/peripheral validation is recorded separately.'
            page['condition'] = 'SK_HW_RESOURCES'
            page['packages_any'] = []
            page['summary'] = 'Interactive hardware UART terminal.'
            page['markdown'] = '# COM\n\n```text\n' + usage + '\n```\n'
            page['body'] = render_text(page['markdown'])
            page['contract'] = usage
        if kind == 'app' and name == 'view':
            usage = ('view [-fit|-actual] <image>\n'
                     'Run from the LCD shell; text-only consoles cannot acquire the display.\n'
                     'Formats: PNG, JPEG, GIF (including animation), uncompressed BMP, and PBM/PGM/PPM (PNM P1-P6).\n'
                     'BMP: 1/4/8/24/32 bits per pixel; compressed BMP and 16-bit BMP are unsupported.\n'
                     'WebP is unavailable on Teensy. SVG, TIFF and PDF are unsupported.\n'
                     'Default: fit to screen. F toggles fit/actual; 0 selects actual size; 1 selects fit; arrows pan. Esc or Ctrl+] exits.\n'
                     'PNG/JPEG have device acceptance coverage; GIF/BMP/PNM decoder paths are integrated but not equally hardware-validated.\n'
                     'Decoded images are bounded by pixel and available PSRAM limits; fitting to the screen does not guarantee a large file will load.\n'
                     'Examples: view /sd/photo.png; view -actual /sd/photo.jpg')
            page['summary'] = 'View raster images on the Teensy LCD.'
            page['markdown'] = '# View on Teensy\n\n```text\n' + usage + '\n```\n'
            page['body'] = render_text(page['markdown'])
            page['contract'] = usage
        if kind == 'app' and name == 'python':
            usage = ('python [-c CODE | /path/script.py [args...]]\nOne MicroPython VM, 512 KiB PSRAM heap. Ctrl+C interrupts; Ctrl+D exits the REPL.\nStandalone text sessions support Ctrl+Z, bg, jobs, fg ID, and job stop ID. See man bg.\ninput() preserves partial lines across detaching. Detached input waits for fg and cannot consume shell keys.\nDetached workers cannot acquire graphics. Existing SolarOS network/gfx bindings remain available; Lua, CAN/DAQ and typed stream bindings are not added by the worker.')
            page['summary'] = 'MicroPython REPL, scripts and detachable text processes.'
            page['markdown'] = '# Python\n\n```text\n' + usage + '\n```\n'
            page['body'] = render_text(page['markdown'])
            page['contract'] = usage
        if kind == 'command':
            # Upstream conditions describe ESP packages, not the Teensy allowlist.
            page['condition'] = {'grep':'SK_SHELL_COMPOSE', 'head':'SK_SHELL_COMPOSE', 'wc':'SK_SHELL_COMPOSE', 'ramfs':'SK_RAMFS', 'io':'SK_HW_RESOURCES', 'gpio':'SK_HW_RESOURCES', 'i2c':'SK_HW_RESOURCES', 'spi':'SK_HW_RESOURCES', 'uart':'SK_HW_RESOURCES', 'expansion':'SK_HW_RESOURCES', 'date':'SK_CLOCK', 'time':'SK_CLOCK', 'rtc':'SK_PLAYGROUND',
                'ping':'SK_NET_DIAGNOSTICS', 'netscan':'SK_NET_DIAGNOSTICS', 'ntp':'SK_NET_DIAGNOSTICS', 'tail':'SK_BACKGROUND_JOBS', 'bg':'SK_BACKGROUND_JOBS', 'jobs':'SK_BACKGROUND_JOBS', 'job':'SK_BACKGROUND_JOBS', 'schedule':'SK_BACKGROUND_JOBS', 'fg':'SK_LCD_CONSOLE', 'close':'SK_LCD_CONSOLE', 'session':'SK_LCD_CONSOLE', 'sessions':'SK_LCD_CONSOLE', 'lcd':'SK_LCD_CONSOLE',
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
