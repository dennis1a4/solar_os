"""Select accurate embedded documentation for the Teensy workstation shell."""
APP_NAMES = set('synth ltop webradio io com calc edit hexedit python aplay arecord ssh files less notes sheet plot playground view invaders mqttx obd clock pdpower scope help curl'.split())
COMMAND_NAMES = set('poweroff serial sshkey midi pd grep head wc ramfs gpio i2c spi uart expansion help man commands echo wait watch apps mem uptime clear setterm identity network audio cd ls cat sh mkdir rm mv cp exit reboot version board status top port df pwd session sessions fg bg tail close ping netscan ntp jobs job schedule zip unzip rtc date time flash sd usb lcd telnetd'.split())
# Audited Teensy contracts. Hardware-neutral shared app pages remain upstream.
OVERRIDES = {
    'aplay': ('Play WAV or MP3 through the SGTL5000 shield.',
        'aplay [-v volume] FILE.wav|FILE.mp3\n'
        'Example: aplay -v 20 /sd/music.wav\n'
        'Volume is 0-100 percent. Uses the SGTL5000 headphone/output hardware; audio and the foreground worker must be free. Files may be on mounted SD, flash or USB. Playback ends at EOF; Esc or Ctrl+] stops early and releases audio. No Bluetooth audio output is integrated.'),
    'apps': ('List installed foreground apps.',
        'apps\n'
        'Lists apps compiled into this firmware. Use man APP for usage and help for the manual browser. Graphical apps require the LCD console; text apps also run over USB/Telnet.'),
    'arecord': ('Record the SGTL5000 microphone input to WAV.',
        'arecord [-d seconds] [-i mic] NEWFILE.wav\n'
        'Example: arecord -d 10 /sd/recording.wav\n'
        'Requires the SGTL5000 audio shield and microphone wiring. Records mono 16-bit PCM at 44100 Hz (about 88.2 kB/second). mic is the only capture source; other stream names are unsupported. Existing files are refused.\n'
        'Without -d, records until stopped, storage/error limits, or the one-hour limit. Esc or Ctrl+] stops and finalizes the partial WAV header. Stop recording before removing storage; a sudden power loss can leave an unfinished header. Audio and the foreground worker must be available. No USB microphone or selectable line-input recording is integrated.'),
    'audio': ('Inspect the SGTL5000 shield and test playback/capture.',
        'audio [status|tone|off|mictest NEWFILE.wav]\n'
        'Requires the SGTL5000 audio shield and the relevant analog wiring. tone produces a one-second test tone; off stops the tone. mictest records a diagnostic microphone WAV to a new file. Use arecord for normal recording and aplay for WAV/MP3 playback. Audio status reports capture/playback counters and allocated ring bytes. Playback uses a 16 KiB internal ring; recording uses a 32 KiB internal ring. Rings allocate on start and release after stop/error cleanup. Test tones and clock alarms do not allocate rings. Diagnostics refuse while an audio app is active or retained; microphone diagnostics also block competing audio app starts. Digital counters alone do not confirm analog wiring or sound quality.'),
    'bg': ('Continue a suspended Python process in background.',
        'bg [ID]\n'
        'Ctrl+Z first. One Python VM is admitted. Only standalone text Python sessions detach; graphical apps remain foreground.\n'
        'Detached jobs retain their directory, VM, pending input and last 8191 output bytes. Use jobs, job output ID, fg ID, or job stop ID.\n'
        'Stopping is cooperative; resources are retained until the interpreter exits. Detached processes survive console disconnect. Waiting input does not consume shell keys.'),
    'board': ('Show board and processor.',
        'board\n'
        'Teensy 4.1, i.MX RT1062, Cortex-M7 and current CPU frequency.'),
    'calc': ('Scientific calculator with a text worksheet.',
        'calc [--tui]\n'
        'calc -e "EXPRESSION"\n'
        "All current Teensy consoles use the text REPL, including the LCD. The graphical calculator pane is not selected by this port's console adapter; use plot for LCD graphs.\n"
        'Supports +, -, multiplication (*), /, %, powers (^), parentheses, scientific notation and implicit multiplication. Functions include sin/cos/tan, inverse trigonometry, sqrt, abs, exp, ln, log, floor, ceil, round, min, max, pow and atan2. Trigonometry uses radians; rad/deg converts. Constants: pi and e.\n'
        'Examples: a = 2; f(x) = sin(x) / x; f(pi / a) (enter as separate REPL lines).\n'
        ':list/:vars lists rows; :del N deletes a row; :clear clears; :save FILE and :load FILE persist the worksheet. Use /sd/calc.txt or another mounted path; the default calc.txt is relative to the working directory. :help shows controls; :quit or Ctrl+] exits. Up/Down recalls input; Left/Right, Home/End, Backspace/Delete edits.'),
    'cat': ('Print a text file or matched files.',
        'cat PATH|PATTERN\n'
        'Example: cat /sd/readme.txt\n'
        'Use less FILE for paging; cat FILE | grep TEXT filters bounded text output. Pipe results have an 8 KiB limit; a long file can overflow it.'),
    'cd': ("Change this console's working directory.",
        'cd [PATH]\n'
        'Example: cd /sd\n'
        'Each console has its own directory. / is a virtual list of mounted volumes, not writable storage. Use pwd to show the current directory.'),
    'clear': ('Clear the current console screen.',
        'clear\n'
        'Does not erase saved command history or files.'),
    'clock': ('Display local time, a countdown or stopwatch on the LCD.',
        'clock\n'
        'clock -a mm:ss\n'
        'clock -s\n'
        'Requires the LCD console. Reads the onboard UTC RTC and displays the timezone from setterm timezone; rtc itself reports UTC. Invalid dates display dashes.\n'
        'In stopwatch mode Space starts/stops; another ordinary key resets. Esc or Ctrl+] exits. A countdown continues while suspended with Ctrl+Z, but exiting cancels it. The SGTL5000 shield sounds the alarm when audio is free; missing/busy audio leaves the zero display as indication.\n'
        'No RTC alarm interrupt, sleep wake or power-off wake is implemented. See schedule for persistent schedules while the OS is running.'),
    'close': ('Close an app session.',
        'close ID\n'
        'Stops retained app chains and discards unsaved state. For Python, requests cooperative cancellation and waits for worker cleanup before reaping. Fixed shells cannot be closed. Use fg first to save edited files.'),
    'commands': ('List built-in shell commands.',
        'commands\n'
        'Use commands | less to page through the list, apps for applications, and man NAME for detailed help.'),
    'cp': ('Copy a file or matched files.',
        'cp SOURCE|PATTERN DEST\n'
        'Example: cp /sd/readme.txt /flash/readme.txt\n'
        'Mounted SD, flash, USB and RAMFS paths use the same adapter. Multiple sources require a destination directory. Files uses an interactive copy/progress interface.'),
    'curl': ('Download HTTP/HTTPS content over Ethernet.',
        'curl [-L] [-o FILE] URL\n'
        'Example: curl -L -o /sd/page.txt https://example.com/\n'
        'Ethernet must be up. -L follows supported redirects; -o writes a file instead of terminal output. The Teensy adapter supports GET requests. HTTPS checks hostname, chain and certificate dates using the UTC RTC and embedded CA roots; an unsupported CA fails validation. HTTPS redirects cannot downgrade to HTTP. Esc or Ctrl+] cancels. Shares the foreground worker with other network/audio apps.'),
    'date': ('Show or set the local calendar date.',
        'date [YYYY-MM-DD]\n'
        'Uses setterm timezone. Setting the date preserves the local time and writes the corresponding UTC RTC value. date; time prints both. rtc reports UTC; ntp can synchronize the RTC over Ethernet.'),
    'df': ('Show space on mounted volumes.',
        'df [--refresh]\n'
        'Lists total, used and free KiB for mounted SD, flash, USB and RAMFS. FAT usage is cached after its first scan and maintained as clusters are allocated/freed. --refresh resets USB accounting and recounts; close USB files/apps first. Reconnect starts a fresh count. USB reads use bounded 4 KiB batches; the first scan can still take time on large drives.'),
    'echo': ('Print text; also see shell composition examples.',
        'echo [TEXT...]\n'
        'Examples: date; time\n'
        'ntp && time\n'
        'cat /flash/log.txt | grep error | head -n 5\n'
        'Composition: 191 bytes, eight commands maximum. Operators inside quotes remain literal.\n'
        'Pipes use PSRAM: 8 KiB per intermediate result, 16 KiB per active pipeline, 64 KiB global buffer limit. Overflow is an error.\n'
        'Only synchronous builtins can be chained; interactive apps, watch, wait, sh and lifecycle commands must run separately. && requires an audited command exit status. Background script jobs still require one command per line.'),
    'exit': ('Disconnect Telnet or reset a local shell prompt.',
        'exit\n'
        'Telnet disconnects. USB and LCD are fixed consoles: exit resets the prompt but does not close the PC serial monitor or power off Teensy.\n'
        'For a PC monitor started with pio device monitor --exit-char 28, press Ctrl+\\ to disconnect on the PC. Use poweroff for an orderly Teensy shutdown; see man poweroff.'),
    'expansion': ('Inspect and lease expansion chip selects.',
        'expansion [list]\n'
        'expansion claim|release slot0|slot1|slot2\n'
        'A lease owns its SPI chip-select pin. Shared SPI signals remain bus-owned. UART leases are separate. Current display wiring reserves slot0/slot2; slot1 is available. No automatic driver discovery or hotplug binding.'),
    'fg': ('Resume a retained app.',
        'fg [ID]\n'
        'See man session. Retained apps remain on their owning console; detached Python processes attach to the calling console.'),
    'files': ('Manage files on SD, flash, USB and RAMFS.',
        'files [--launcher] [PATH]\n'
        'Example: files /sd\n'
        'Two panes: arrows navigate, Tab switches panes, Enter opens. F3/V views, F4/E edits, F5/C copies, F6/M moves, F7/k/K creates a directory, F8/D deletes, F9/Z creates a ZIP. Operations use selected/marked entries. Esc cancels an active operation; Ctrl+] exits. Completed items remain after cancellation; a partial current copy is removed.\n'
        'The virtual / lists mounted /sd, /flash, /usb and RAMFS volumes. Unknown paths do not become SD paths. Only installed app associations are offered; use aplay for audio, view for images, python for scripts, sheet for CSV, less/edit for text.\n'
        '--launcher is a read-only single pane: Enter opens, r refreshes, Backspace goes to parent, q exits. A startup launcher must use an existing directory, for example files --launcher /sd/apps in /flash/.shell/startup with setterm startup flash.\n'
        'Close files/apps before sd eject or usb eject; RAMFS contents vanish on unmount/reboot.'),
    'flash': ('Inspect or mount external QSPI flash storage.',
        'flash [status|mount|scan|init]\n'
        '/flash is the external LittleFS data volume, separate from firmware program flash. init accepts only verified-blank media; it never formats a nonblank chip. Settings, SSH keys and shell history are stored here. df shows filesystem space.'),
    'gpio': ('Claim and control a free header pin.',
        'gpio [list|PIN]\n'
        'gpio mode PIN in|pullup|pulldown|out [0|1]\n'
        'gpio read PIN\n'
        'gpio write PIN 0|1\n'
        'gpio release PIN\n'
        'Pins 0..41 only; Teensy signals are 3.3 V and not 5 V tolerant. Existing board/bus reservations cannot be overridden. Claims belong to the calling console; release/disconnect restores input mode.'),
    'grep': ('Filter bounded pipe input by literal text.',
        '... | grep LITERAL\n'
        'Case sensitive substring matching; no regular expressions. Exit status 1 means no matching lines. Requires pipe input.'),
    'head': ('Show the first lines of pipe input.',
        '... | head [-n COUNT]\n'
        'Default: 10 lines. COUNT: 0..8192. Requires pipe input.'),
    'help': ('Browse all embedded Teensy manual pages offline.',
        'help [TOPIC]\n'
        'help status\n'
        'All compiled app and command pages are included in firmware; no network download or SD manual is required. Signed manual downloads are not integrated on Teensy.\n'
        'Up/Down, Page Up/Page Down and Home/End navigate. Left/Right, Enter or Space folds groups; Enter or Right opens a topic in less. q, Esc or Ctrl+] returns to the shell.\n'
        'Use man TOPIC, man -k QUERY or man --list. For the status command use man command.status (help status describes the manual).'),
    'i2c': ('Probe or transfer on a fixed I2C bus.',
        'i2c [list]\n'
        'i2c scan i2c0|i2c1|i2c2\n'
        'i2c xfer BUS ADDRESS HEX|- RX_COUNT\n'
        'Fixed 100 kHz; 7-bit addresses 8..0x77, up to 32 bytes each direction. HEX is contiguous hex bytes; - means no write bytes. Scan is cancellable with Ctrl+C/Esc and skips reserved addresses.'),
    'identity': ('Show or save the user and hostname in flash.',
        'identity [status]\n'
        'identity user NAME\n'
        'identity hostname NAME\n'
        'Names: 1-31 letters, digits, dot, dash or underscore. Preferences are stored under /flash/.solar-settings and survive reboot. The user becomes the default SSH username; identity also supplies the shell prompt. This does not configure networking or enable a wireless interface.'),
    'io': ('Inspect Teensy pins, buses and claims.',
        'io [pins|claims|buses|release]\n'
        'release restores this console’s GPIO inputs and closes its raw UART/expansion leases. Built-in device reservations and COM app leases cannot be released here. Fixed board routing; no pin remapping or saved runtime claims.'),
    'job': ('Run and inspect background scripts.',
        'job start script /path.sh\n'
        'job start script0 /path.sh\n'
        'job status [script0|ID]\n'
        'job output script0|ID\n'
        'job stop script0|ID\n'
        'job kill ID\n'
        'Numeric IDs refer to Python processes. Stop is cooperative; cleanup occurs on the VM worker. Reap a completed job with fg ID or job stop ID.\n'
        'The alias script picks a free slot; an explicit slot restarts that job. Output retains the last 4095 bytes.\n'
        'Supported: echo wait pwd cd ls cat mkdir cp mv rm version board status mem uptime top port date time.\n'
        'Each job starts at / with an isolated directory. One line per tick; wait yields cooperatively.\n'
        'No interactive apps, nested scripts, pipes or redirection. Cancellation happens between commands; storage operations can delay other consoles.\n'
        'Command diagnostics appear in the log; completion does not guarantee each file operation succeeded.'),
    'jobs': ('List Python processes and background script jobs.',
        'jobs\n'
        'One detachable Python process with a numeric session ID, plus four cooperative script slots: script0 through script3.\n'
        'Python states include foreground, suspended, running, waiting-input, stopping and done. TIME is elapsed wall time. Jobs survive console disconnect but not reboot.'),
    'lcd': ('Configure the RA8875 text console and inspect local input.',
        'lcd\n'
        'lcd font 1|2|3\n'
        'lcd colors\n'
        'lcd color FG BG\n'
        'lcd reset\n'
        'lcd dump\n'
        'lcd send "COMMAND"\n'
        'lcd key exit|ctrlc|ctrlz|esc|up|down|left|right|home|end|tab|space|enter\n'
        'Font scales give 100x30, 50x15 or 33x10 text cells on the 800x480 LCD. colors lists the 16 accepted color names; foreground/background must differ. reset restores 1x white-on-black. Font/color preferences are saved in flash. Changes require an idle LCD without active or suspended apps.\n'
        'Use dump/send/key from another console to inspect text or inject input; dump does not capture graphics. Graphical applications own the LCD while running.'),
    'ls': ('List files and directories.',
        'ls [-a] [-h] [PATH|PATTERN]\n'
        '-a includes dot-prefixed entries; -h prints human-readable sizes. With no path, lists the current directory. ls / shows mounted volumes.'),
    'ltop': ('Monitor interval CPU load, task stacks and memory.',
        'ltop\n'
        'One Cortex-M7 core. Samples FreeRTOS runtime deltas once per second; top instead shows cumulative CPU use. IRAM and ERAM bars show internal heap and fitted PSRAM usage. The FREE column is minimum unused task stack bytes (stack headroom), not peak stack consumption.\n'
        'Up/Down or j/k scrolls; Page Up/Page Down, Home/End or g/G jumps; r resets the sample baseline; q, Esc or Ctrl+] exits.'),
    'man': ('Read or search the offline Teensy manual.',
        'man TOPIC\n'
        'man -k QUERY...\n'
        'man --list\n'
        'Examples: man synth; man arecord; man lcd; man network\n'
        'Every compiled app/command has an embedded page. Full IDs such as app.help and command.help disambiguate topics. Pages open in less: arrows or j/k scroll, Space/b pages, / searches, n/N repeats, q exits. help opens the topic browser. No manual download is needed.'),
    'mem': ('Show internal heap and PSRAM.',
        'mem\n'
        'Internal heap combines DTCM and OCRAM; the next line shows each region separately. It excludes static data/code and reserved stacks. On-demand Telnet, foreground and Python stacks return memory after task cleanup. Normal libc malloc still uses DTCM; SolarOS internal-preferred allocations use OCRAM first. Ordinary PSRAM allocations leave a 128 KiB system reserve. Pipe buffers have a 64 KiB global limit and are freed when the chain finishes. No automatic migration or swapping.'),
    'midi': ('Record and replay USB/UART MIDI.',
        'midi status\n'
        'midi record usb|slotN NEWFILE.smr\n'
        'midi play FILE.smr usb|slotN\n'
        'Ctrl+C stops. One operation at a time, 31250-baud UART, shared pin ownership. SMR1 timestamped files; not standard .mid. SysEx unsupported. USB cable 0. Recording overflow is reported. Physical MIDI validation pending.'),
    'mkdir': ('Create directories on mounted storage.',
        'mkdir PATH [PATH...]\n'
        'Example: mkdir /sd/projects\n'
        'Create parents first. The virtual root is not writable; use ramfs mount to create a new volatile top-level volume.'),
    'mv': ('Rename or move files.',
        'mv SOURCE|PATTERN DEST\n'
        'Same-volume moves rename; cross-volume moves copy then remove the source. Multiple sources require a destination directory.'),
    'netscan': ('Scan TCP ports on IPv4 targets.',
        'netscan HOST|A.B.C.D-E|CIDR [PORTS]\n'
        'PORTS is a comma list or range, at most 128 unique ports. IPv4 CIDR supports /24 through /32.\n'
        'Defaults: 22,23,53,80,443,1883,8080. Sequential TCP connects; 350 ms per port. Ctrl+C/Esc stops.\n'
        'Reports open ports and probe count; no UDP scanning or service fingerprinting. Ethernet must be up.'),
    'network': ('Inspect or start Teensy wired Ethernet.',
        'network up\n'
        'network down\n'
        'network status\n'
        'network routes\n'
        'Uses the Teensy Ethernet hardware and DHCP. Wi-Fi is not integrated. Connect the Ethernet hardware/cable before network up.\n'
        'For boot startup, put network up on its own line in /flash/.shell/startup and select setterm startup flash. setterm shows the selected startup path. Startup scripts run at boot; there is no periodic NTP synchronization unless explicitly arranged.'),
    'ntp': ('Query network time or synchronize the UTC RTC.',
        'ntp [-q] [SERVER [PORT]]\n'
        'Defaults: pool.ntp.org, UDP 123, five-second reply timeout. -q queries without changing the clock.\n'
        'Validates server address/port, response mode/version, stratum, synchronization, echoed request token and RTC range, including the 2036 NTP era rollover.\n'
        'Failed, rejected or cancelled requests leave the RTC unchanged. Successful sync preserves the display timezone.\n'
        'Plain NTP is unauthenticated; use a trusted server. No automatic periodic synchronization is started.'),
    'pd': ('Monitor an STUSB4500 USB-PD controller.',
        'pd status\n'
        'pd open i2cN ADDRESS BOARD_MAX_MV BOARD_MAX_MA\n'
        'pd request MV MA\n'
        'pd close\n'
        'No hardware access until open. Explicit open requests 5 V discovery. Set limits from the actual board ratings. Requests require an advertised fixed profile. Status distinguishes pending from confirmed contracts. Close stops monitoring; it does not change voltage. No NVM writes or boot restore. Hardware validation pending.'),
    'ping': ('Send bounded ICMP echo requests.',
        'ping HOST [1..999]\n'
        'Defaults to four probes, one second timeout/interval, 16 data bytes. Ctrl+C/Esc stops. Ethernet must be up. Reports replies, TTL, RTT and packet loss. Only one ICMP probe can be active at a time.'),
    'playground': ('Browse and install compatible Python community apps.',
        'playground\n'
        'playground search QUERY...\n'
        'playground refresh\n'
        'playground reload\n'
        'playground install APP-ID [auto|flash|sd]\n'
        'playground run APP-ID [ARG...]\n'
        'playground source [repository-or-catalog-url|reset]\n'
        'playground storage [flash|sd]\n'
        'playground delete\n'
        'Uses Ethernet for downloads; network up first. HTTPS verifies certificates against the UTC RTC and embedded CA roots; use ntp to set time if needed.\n'
        'Refresh downloads the catalog; reload and normal browsing use its saved offline copy. / searches; Enter opens details; i installs; u uninstalls after confirmation; q/Esc exits the top-level tree. Scripts install under /sd/playground or /flash/playground according to the saved selection (auto prefers mounted SD). delete removes the entire Playground directory on selected storage, including installed apps; source/storage preferences remain.\n'
        'Python is the available runtime; Lua is not integrated. Requirements for unsupported devices/APIs can prevent a catalog app from running. Installed apps run with normal Python filesystem and hardware permissions, without a sandbox. Package size/hash verification does not establish that a community program is trustworthy.'),
    'plot': ('Plot CSV data or registered scalar streams on the LCD.',
        'plot STREAM... [--rate MS]\n'
        'plot -f FILE.csv [COLUMN...]\n'
        'Examples: plot uptime --rate 250\n'
        'plot -f /sd/readings.csv temperature\n'
        'Requires the LCD console. uptime is a built-in scalar stream measured in seconds. Additional live sensors require registered stream drivers; a DAQ acquisition job is not provided by this port.\n'
        'Left/Right pans; Up/Down selects a series; +/- or Page Up/Page Down changes zoom/window; a/r resets; Space pauses/resumes live sampling; q, Esc or Ctrl+] exits. Rate is best effort; screen redraw is limited to once per 25 ms.'),
    'port': ('List registered byte-stream ports.',
        'port [list]\n'
        'Shows names, owners and labels. Use uart list and com uart7 or com uart8 for hardware terminals.'),
    'poweroff': ('Gracefully stop jobs and power off Teensy.',
        'poweroff [--check|status]\n'
        '--check performs cleanup but leaves power on. Close other apps first. Python gets 2 seconds to cooperate, then one KeyboardInterrupt. A 15-second deadline or cleanup error leaves power on. The On/Off pin invokes the same path; long hold is emergency cutoff.'),
    'pwd': ('Print the calling shell directory.',
        'pwd'),
    'ramfs': ('Create volatile PSRAM filesystems.',
        'ramfs [status]\n'
        'ramfs mount /NAME SIZE\n'
        'ramfs unmount /NAME\n'
        'Up to four top-level mounts; names use letters, digits, dash or underscore. /sd, /flash, /usb and / are reserved. SIZE: 1024..4194304 bytes, optional k/m suffix. Keeps 512 KiB PSRAM free for apps. No automatic mount or persistence; reboot/unmount loses all files. Open files/directories block unmount. Close directory iterators before removing or renaming entries. Files, shell and Python use ordinary paths; df includes RAMFS. Use explicit /sd paths; unknown mount paths never fall through to SD.'),
    'reboot': ('Restart Teensy firmware.',
        'reboot\n'
        'Flushes shell history before restarting. Save work and stop file-writing apps first; use poweroff for the full graceful shutdown path.'),
    'rm': ('Remove files or directories.',
        'rm [-f|-r|-rf] PATH|PATTERN [PATH|PATTERN...]\n'
        '-f permits empty directories; -r recursively removes directory contents. Deletions are permanent. Close live files/directory iterators first. Mounted volume roots cannot be removed.'),
    'rtc': ('Inspect or set the UTC hardware clock.',
        'rtc\n'
        'rtc set UNIX_SECONDS\n'
        'Accepts UTC epochs from 2000 through the 32-bit RTC limit in 2106.\n'
        'Display timezone is configured separately with setterm timezone.'),
    'schedule': ('Schedule scripts and alarms.',
        'schedule list\n'
        'schedule show NAME\n'
        'schedule add NAME in SECONDS run /path.sh\n'
        'schedule add NAME every SECONDS run /path.sh\n'
        'schedule add NAME daily HH:MM[:SS] run /path.sh\n'
        'schedule run NAME\n'
        'schedule enable NAME\n'
        'schedule disable NAME\n'
        'schedule remove NAME\n'
        'Also: schedule add NAME at YYYY-MM-DD HH:MM[:SS] run /path.sh\n'
        'schedule add NAME weekly mon,wed HH:MM[:SS] run /path.sh\n'
        'Replace run /path.sh with alarm for an audible alarm; schedule stop silences it. Persistent entries use /flash/.solar/schedule.bin.\n'
        'Script triggers use the same four job slots; a full pool increments skipped. Calendar schedules need a valid RTC and use the configured timezone.\n'
        'No power-off wake or hardware alarm IRQ. Job output is volatile; relative countdowns restart after reboot.'),
    'sd': ('Inspect, mount or safely eject the SD card.',
        'sd [status|mount|eject]\n'
        'The built-in SD slot mounts at /sd. Close SD files/apps before eject. mount retries initialization without a board reboot. This command does not format cards.'),
    'serial': ('Record UART data in the background.',
        'serial config BUS BAUD FORMAT [none|xonxoff]\n'
        'serial status\n'
        'serial record uart7|uart8|uart3 BAUD NEWFILE [--timestamp]\n'
        'serial stop BUS\n'
        'Formats: 8N1/8N2, 7E1/7E2/7O1/7O2, 8E1/8E2/8O1/8O2. Configure only while closed; settings reset on reboot and apply to COM/serial, not Python or raw uart. RTS/CTS, 5/6/9 bits, 7N1, mark/space parity and 1.5 stop bits are unsupported. XON/XOFF interprets 0x11/0x13; disable for arbitrary binary protocols. Raw logs preserve received bytes, including flow controls. --timestamp logs elapsed milliseconds, RX/TX and hexadecimal bytes per chunk. Existing files are never replaced. Recording survives COM exit, suspension and console disconnect; explicitly stop before ejecting storage or powering off. 64 KiB PSRAM per log; status reports queue/display drops and storage errors. Hardware overruns are not measured. Attach com BUS to view and transmit. UART3 is blocked by the current LCD wiring.'),
    'session': ('Manage retained app sessions.',
        'session [list]\n'
        'session fg [ID]\n'
        'session close ID\n'
        'Ctrl+Z suspends a resumable app chain; up to four suspended chains per console.\n'
        'fg resumes the most recently suspended app on this console, or the named app on its owning console.\n'
        'The owner must be at an empty shell prompt. close discards unsaved state and stops the whole chain.\n'
        'USB/Telnet disconnect closes that console’s apps; local keyboard disconnect preserves LCD sessions.\n'
        'Fixed consoles have IDs 1-3; app IDs start at 4. Dynamic shells and session create/send are not integrated.'),
    'sessions': ('List fixed consoles and retained apps.',
        'sessions\n'
        'Alias for session list. See man session.'),
    'setterm': ('Save terminal, keyboard, startup and timezone preferences.',
        'setterm\n'
        'setterm size COLS ROWS\n'
        'setterm startup auto|flash|sd\n'
        'setterm numlock [on|off]\n'
        'setterm timezone [UTC|Manitoba|UTC+/-offset|Europe/Berlin|POSIX-TZ]\n'
        'Preferences are saved in /flash/.solar-settings. USB size: columns 20-300, rows 8-120; default 80x24. For LCD use lcd font 1|2|3. Telnet dimensions come from the client window.\n'
        'numlock on enables Num Lock now and at each USB host keyboard connection/reboot. off disables automatic enabling; the physical key still works.\n'
        'Startup files are /flash/.shell/startup or /sd/.shell/startup; setterm shows the resolved selection. Use one command per line. auto resolves the source from available storage.\n'
        'Timezone affects date, time, Clock and calendar schedules; RTC remains UTC. Manitoba is a fixed UTC-5 alias, without daylight-saving changes. Use a suitable POSIX-TZ rule if seasonal changes are wanted.'),
    'sh': ('Run a simple SolarOS command script.',
        'sh FILE\n'
        'Example: sh /sd/start.sh\n'
        'Use one command per line. This is the SolarOS script runner, not a Unix shell or Python interpreter. For cooperative background scripts see man job; for boot scripts see man setterm.'),
    'spi': ('Transfer through an owned expansion CS.',
        'spi [list]\n'
        'spi xfer slot0|slot1|slot2 MODE HZ HEX\n'
        'Claim the expansion slot first. Mode 0..3, 100000..12000000 Hz, up to 128 bytes. Returns received bytes as hex; MSB first. Display chip selects cannot be claimed.'),
    'ssh': ('Connect to an SSH server over Ethernet.',
        'ssh [user@]HOST [PORT]\n'
        'Example: ssh dennis@192.168.1.100\n'
        'Requires Ethernet (network up). Default port 22; omitted user uses identity user. Supports password and RSA-key authentication. Configuration, host aliases, known hosts and keys live under /flash/.ssh; flash must be mounted. Host aliases are in /flash/.ssh/hosts. See sshkey for key generation/public keys.\n'
        'Keys are sent to the remote terminal. Ctrl+] closes the connection; Ctrl+Z suspends the app and fg resumes it. Console disconnect closes its sessions. SSH shares the foreground worker with audio and other network apps; a busy worker rejects a conflicting launch.'),
    'sshkey': ('Manage the default RSA SSH key pair in flash.',
        'sshkey [status]\n'
        'sshkey gen [-f] [2048|3072|4096]\n'
        'sshkey pub\n'
        'sshkey rm\n'
        'Keys: /flash/.ssh/id_rsa and /flash/.ssh/id_rsa.pub. Flash must be mounted. gen defaults to 2048 bits; -f permits replacing an existing pair. Generation uses the foreground worker and can take time; wait for completion. pub prints the public key to install on the server. Keep the private key private. rm deletes the default pair.'),
    'status': ('Show a compact system summary.',
        'status\n'
        'Firmware, processor, uptime, memory, mounted-volume count and FreeRTOS task count.'),
    'synth': ('Play the eight-voice SGTL5000 terminal synthesizer.',
        'synth [--headless]\n'
        'Requires the SGTL5000 audio shield and free audio/foreground worker resources. Stereo output at 44100 Hz; initial volume 20 percent.\n'
        'a w s e d f t g y h u j k plays chromatic C4-C5. Notes pulse for 220 ms; H toggles hold, where pressing a note again releases it. Space releases all notes. z/x changes octave (2-6); 1-5 selects square, triangle, saw, sine or noise; -/+ changes volume.\n'
        '[/] selects attack, decay, sustain, release, cutoff, resonance or oscillator-2 mix; comma/period adjusts it; ? shows controls and performance. q, Ctrl+C or Ctrl+] exits.\n'
        'Eight simultaneous voices; no physical key-up tracking. No graphical preset editor, MIDI-to-synth routing or persistent presets in this port. midi is a separate recording/replay command.'),
    'tail': ('Show the last lines of a file.',
        'tail [-n 1..10000] FILE\n'
        'Defaults to ten lines. Reads a snapshot while a logger may continue writing; no -f mode yet. Ctrl+C cancels.'),
    'telnetd': ('Serve one authenticated terminal over Ethernet.',
        'telnetd start /absolute/password-file [PORT]\n'
        'telnetd status\n'
        'telnetd stop\n'
        'Default port 23; off until started. Its 40 KiB internal stack is allocated at start and released after stop finishes session cleanup. status shows stack bytes; cleanup may be pending briefly. exit disconnects a client but keeps the server and stack available. Requires Ethernet. Password file: one line of 1-63 printable ASCII characters. One remote shell; client window size is negotiated. exit disconnects the remote shell. Telnet carries passwords/data unencrypted; use it only on a trusted network.'),
    'time': ('Show or set the local clock time.',
        'time [HH:MM[:SS]]\n'
        'Uses setterm timezone. Setting the time preserves the local date and writes the corresponding UTC RTC value. date; time prints both. rtc reports UTC; ntp can synchronize the RTC over Ethernet.'),
    'top': ('Snapshot FreeRTOS tasks.',
        'top\n'
        'Task state, priority, stack high-water headroom in bytes and cumulative CPU utilization.\n'
        'watch -n 2 top repeats the snapshot. CPU is cumulative, not interval utilization.'),
    'uart': ('Open a fixed expansion UART.',
        'uart [list]\n'
        'uart open uart7|uart8|uart3 [BAUD]\n'
        'uart close BUS\n'
        'uart read BUS [COUNT]\n'
        'uart write BUS TEXT\n'
        '3.3 V TTL UART, not RS-232 voltage levels. 300..1000000 baud, 8N1; default 115200. Up to 128 bytes per read/write, nonblocking. Pin conflicts reject the whole claim. COM uses the same UART leases; close a raw UART before com. UART3 conflicts with the active display WAIT pin.'),
    'unzip': ('List or extract a ZIP archive.',
        'unzip [-l] ARCHIVE.zip [DEST]\n'
        '-l lists archive entries; without it, extracts to DEST or the current directory. Select a writable mounted directory, for example /sd/unpacked.'),
    'uptime': ('Show uptime and calling task stack headroom.',
        'uptime\n'
        'Uptime is in milliseconds; stack-free is in words.'),
    'usb': ('Inspect, mount or safely eject USB host storage.',
        'usb [status|mount|eject]\n'
        'One supported mass-storage drive/partition mounts at /usb on the USB host connector. A powered hub can share the host port with a keyboard. This is separate from the PC-facing USB serial console. Close USB files/apps before eject. mount retries/remounts; no formatting command is provided. FAT32 is tested; exFAT still needs device validation.'),
    'version': ('Show firmware identity.',
        'version\n'
        'SolarOS source version and Teensy port identity.'),
    'wait': ('Pause the calling shell or script.',
        'wait SECONDS\n'
        'Range: 0-86400 seconds. Background script waits yield cooperatively. Run separately from command chains/pipes.'),
    'watch': ('Repeat a shell command until stopped.',
        'watch [-n SECONDS] COMMAND [ARGS...]\n'
        'Example: watch -n 2 top\n'
        'Esc, q or Ctrl+] stops. Use a noninteractive status command. Run watch separately from chains/pipes.'),
    'wc': ('Count piped lines, words and bytes.',
        '... | wc [-l|-w|-c]\n'
        'Without an option prints newline, whitespace-separated word and byte counts. Requires pipe input.'),
    'webradio': ('Play MP3 internet radio over Ethernet.',
        'webradio [--tui] [URL]\n'
        'webradio list\n'
        'webradio add NAME URL\n'
        'webradio remove NAME\n'
        'webradio reset\n'
        'Requires Ethernet (network up), a valid UTC RTC for HTTPS, and the SGTL5000 audio shield. Direct HTTP/HTTPS MP3 streams only; no playlists, HLS or AAC. Teensy always uses the text station list, including on the LCD.\n'
        'Up/Down or j/k selects; Enter plays; Space stops; R reconnects; +/- changes volume. A adds, E edits, Delete removes a station; Enter advances/saves a dialog and Esc cancels it. Q, Esc or Ctrl+] exits and stops playback.\n'
        'Catalog changes are saved in /flash/.solar/webradio/catalog.bin; flash must be mounted. Audio uses a bounded 128 KiB PSRAM buffer. Playback continues while suspended with Ctrl+Z; fg resumes. Audio and the foreground network worker must be available; conflicting launches are rejected.'),
    'zip': ('Create a ZIP archive on mounted storage.',
        'zip [-0] ARCHIVE.zip PATH|PATTERN [PATH|PATTERN...]\n'
        '-0 stores entries without compression. Use explicit mounted paths; Files also provides an archive action.'),
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
            usage = 'com [--hex] [--baud N] [--enter cr|lf|crlf] [uart7|uart8|uart3]\nDefault uart7; initially 115200 8N1. Use serial config for data bits, parity, stop bits and none/xonxoff flow. Enter defaults to CR. Ctrl+] exits; Ctrl+Z suspends; fg resumes. A 4 KiB PSRAM display queue retains received bytes while suspended; excess display bytes are counted and dropped. serial record runs independently with its own 64 KiB log queue. COM exit does not stop a recording; use serial stop BUS. Only one terminal can attach per bus. --autobaud is unavailable. USB host serial adapters are not supported.'
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
            usage = ('python [-c CODE | /path/script.py [args...]]\nOne MicroPython VM, 512 KiB PSRAM heap. Ctrl+C interrupts; Ctrl+D exits the REPL.\nStandalone text sessions support Ctrl+Z, bg, jobs, fg ID, and job stop ID. See man bg.\ninput() preserves partial lines across detaching. Detached input waits for fg and cannot consume shell keys.\nDetached workers cannot acquire graphics. Existing SolarOS network/gfx bindings remain available. Imports also search /flash/lib and /sd/lib. The offline machine/os/time compatibility bundle adds resource-managed pins, buses, ADC/PWM and filesystem/timing helpers. See /flash/lib/README.txt for exact limits and examples under /sd/python-examples. MIDI, audio, USB-PD, CAN/DAQ and typed stream Python bindings remain pending.')
            page['summary'] = 'MicroPython REPL, scripts and detachable text processes.'
            page['markdown'] = '# Python\n\n```text\n' + usage + '\n```\n'
            page['body'] = render_text(page['markdown'])
            page['contract'] = usage
        if kind == 'command':
            # Conditions already come from the actual shell registry, including SK gates.
            page['packages_any'] = []
            page['aliases'] = [name]
            if name in OVERRIDES:
                summary, usage = OVERRIDES[name]
                page['summary'] = summary
                page['markdown'] = f'# {name}\n\n{summary}\n\n```text\n{usage}\n```\n'
                page['body'] = render_text(page['markdown'])
                page['contract'] = usage
        if kind == 'app' and name in OVERRIDES:
            summary, usage = OVERRIDES[name]
            page['summary'] = summary
            page['markdown'] = f'# {name}\n\n{summary}\n\n```text\n{usage}\n```\n'
            page['body'] = render_text(page['markdown'])
            page['contract'] = usage
        notes = {
            'edit': 'Use mounted paths such as edit /sd/main.py. Ctrl+Z suspends and fg resumes without discarding edits. Terminal font support depends on the console; lcd font changes the LCD shell size while idle.',
            'hexedit': 'Use mounted paths such as hexedit /sd/data.bin. Save before exiting; Ctrl+Z/fg retains a session. Terminal font support depends on the console.',
            'notes': 'On Teensy use an explicit mounted path, for example notes /sd/tasks.md. The upstream default /.notes directory is not writable on the virtual root. Changes are saved to the selected Markdown file.',
            'sheet': 'Example: sheet /sd/readings.csv. Reads existing CSV files; the upstream DAQ acquisition job is not integrated.',
            'invaders': 'Requires the LCD console and USB host keyboard; USB serial and Telnet cannot acquire graphics.',
            'scope': 'The current legacy bench firmware disables hardware ADC capture because pin 40 drives AmpEn. Use scope --demo. The display candidate uses ADC40/AmpEn0 only after the documented wiring change. Never attach a scope input to the current AmpEn pin.',
            'pdpower': 'This app remains a simulator. The separate pd command implements the STUSB4500 I2C driver with explicit board limits; the controller is not wired on the current bench and hardware testing remains pending.',
            'mqttx': 'Requires wired Ethernet: network up. Credentials and MQTT payloads travel over plain TCP; use a trusted network. No wireless interface is required.',
        }
        if kind == 'app' and name in notes:
            page['markdown'] += '\n' + notes[name] + '\n'
            page['body'] = render_text(page['markdown'])
            page['contract'] += '\n' + notes[name]
        page['aliases'] = [name]
        page['keywords'] = name + ' ' + page['summary']
        selected.append(page)
    expected = {f'app.{name}' for name in APP_NAMES} | {f'command.{name}' for name in COMMAND_NAMES}
    missing = expected - {page['id'] for page in selected}
    if missing:
        raise ValueError('Missing Teensy manual pages: ' + ', '.join(sorted(missing)))
    return sorted(selected, key=lambda p: (p['section'], p['id']))
