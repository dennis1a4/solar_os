# Lightweight Python syntax highlighting

Implemented and installed 2026-10-01 in the shared `edit` application.
Open a `.py` or `.pyw` file normally; no setting or language server is needed.
The existing Lua lexer uses the same cache and color infrastructure.

`edit` is the MicroPython script editor; there is no separate MicroPython-specific
editor. For example, run `edit /sd/example.py`, save with `Ctrl+S`, exit with
`Ctrl+]`, then run `python /sd/example.py`. The interactive `python` REPL is
separate and does not have syntax highlighting.

| Token | Color |
| --- | --- |
| Python keywords | Bright blue |
| Built-ins, including print/len/range | Bright magenta |
| Name immediately following def/class | Bright yellow, bold |
| Strings, including triple-quoted strings | Bright green |
| Comments | Gray |
| Numbers | Bright cyan |
| True, False, None | Bright white |

Colors pass through the shared TUI's existing one-byte cell attributes, using
previously unused bits; no extra color buffer is needed. The Teensy LCD already
supports these ANSI colors, as do USB/Telnet terminal emulators. Monochrome
local terminals retain bold/italic/underline distinctions. Dumb ports receive
no color controls; physical monochrome VT100s may ignore color SGRs. Selection
uses default foreground plus inverse video so token colors do not hide selected
text. Editor exit restores the prior foreground.

## Work and memory limits

The shared lexer is reused rather than duplicated inside the editor. A compact
cache stores three bytes per logical line: incoming lexer mode, Lua delimiter
state and a validity marker. Allocation follows the actual line count, with
128 spare entries; very newline-dense files can therefore require more metadata.
An additional bounded cache stores styles for at most 64 visible rows, 96 columns
per row, plus row keys. Both allocations prefer PSRAM and are freed on close.
If allocation fails, the editor remains usable with plain text.

Only visible character spans receive style arrays. State-only lexing proceeds
through the viewport plus two lines, in batches of at most 64 lines / about
8 KiB per render/tick. A line is processed atomically, so a single long line
can exceed the byte budget (bounded by the existing 256 KiB file limit).
Unresolved rows temporarily render plain until a 25 ms tick catches up.

Edits invalidate affected line states and visible row styles. Newline changes
shift the compact state table. Propagation stops once a known unchanged suffix
has the same incoming state. Multiple edits during pending propagation retain
the invalid boundary. Cursor movement reuses both valid line states and unchanged
visible styles; it does not repeat full-file lexing. Existing cursor/line-location
helpers still scan text to locate lines; this is not a new indexed text buffer.

This is lexical highlighting, not Python validation. Built-ins remain colored
when shadowed by user variables. Attribute names such as `object.print` are not
classified as built-ins. Function/class names get definition styling; ordinary
calls are not semantically resolved. String prefixes are styled with strings;
f-string contents are colored as a whole, without parsing embedded expressions.
Triple-quoted strings carry state across lines; ordinary backslash-continued
quoted strings are not tracked across physical lines in this implementation.

## Validation

- Host ASan/UBSan: token categories, incremental state equivalence against a
  full lex through 4,000 randomized splices, partially propagated multiple edits,
  line-budget bounds, no cursor-only state lexing and one-line convergence in an
  1,800-line cached file.
- Existing five editor key-policy tests pass.
- Device `/tmp/teensy-syntax-device.json`: all token colors checked through a
  terminal emulator, selection contrast, triple-quote insertion/backspace,
  retained-session resume, default shell color restoration, LCD text rendering,
  and five editor cycles with exact memory recovery (28,404 internal /
  7,896,528 PSRAM free while the 256 KiB test RAMFS is mounted).
- Test RAMFS was unmounted and test editors closed afterward. Physical LCD
  readability is not a user-confirmed result; automated checks verify the ANSI
  colors and LCD text, not a photograph of the panel.

The test emulator calls bright yellow `brightbrown`; the initial assertion was
corrected to that name. No firmware change was needed for that test correction.
See [demo source](../../examples/teensy41/syntax_demo.py). Copy it to a mounted
volume and open it with `edit` to inspect colors at the keyboard.
