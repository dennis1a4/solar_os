# Shared Tab completion on Teensy

The Teensy shell now uses the shared line-input completion engine. Tab completes
commands/apps/aliases, file arguments, directories for `cd`, retained session IDs
for `fg`/`bg`/`close`, active job IDs/names for `job` arguments, and supported
`setterm` keys and common timezone/startup values.

```text
ed<Tab>                         -> edit
edit /sd/proj<Tab>               -> edit /sd/projects/
cat "/sd/my no<Tab>              -> cat "/sd/my notes/"
fg <Tab>                        -> a retained session or detached Python ID
job status <Tab>                 -> an active job ID/name
setterm time<Tab>                -> setterm timezone
```

Completion operates at the cursor, including an earlier argument. It replaces
that argument (including its old suffix), preserving the rest of the command
line. Single/double quotes and backslash escapes are decoded; replacements use
safe double quoting when needed. A completed directory ends with `/`; the cursor
stays inside its closing quote so you can type the next component. Unique
non-directory tokens at the end of the line receive a trailing space.

For ambiguity, the first Tab inserts the longest common prefix. The second
consecutive Tab lists up to 20 candidates and reports the remaining count.
Typing or moving the cursor resets the repeated-Tab state. Hidden files are
suggested only when the filename prefix begins with `.`.

The filesystem provider opens only the directory named by the argument. It
streams entries, retaining one common prefix, not a directory listing or global
index. An ambiguous second Tab rescans that directory for display. Scratch
buffers are fixed; the shell allocates a small transient completion context in
PSRAM and releases it afterward. The Teensy directory iterator yields the console
gate between entries without consuming queued typing. Disconnect or enumeration
failure prevents insertion of an incomplete match. Directory traversal still
requires reading every entry to compute a correct prefix; very large directories
can take time, while the other console remains usable.

Decoded candidate tokens are limited to 255 bytes; the existing shell line limit
still applies. Capacity failures leave the input unchanged. The engine is
synchronous and has no persistent cache or task of its own.

## Reusable API

Headers:

- `src/services/solar_os_completion.h`: provider registry, cursor parser,
  streaming matching, quoting/replacement and bounded listings.
- `src/services/solar_os_tui_widgets.h`: `solar_os_tui_input_complete` integrates
  completion with the input widget's cursor and horizontal viewport.
- `src/shell/solar_os_shell_completion_providers.h`: installs shell/port adapters
  into a caller-owned registry, including live session/job providers.

Provider kinds are `FILE`, `DIRECTORY`, `COMMAND`, `APP`, `SESSION`, `JOB`,
`SETTING`, and `CUSTOM` (`SOLAR_OS_COMPLETE_*`). Each registry entry contains an
enumerator and a user pointer. Enumerators stream decoded candidates to the
supplied sink and return false on incomplete enumeration. Providers must not emit
duplicate tokens. Candidate storage can be reused immediately after `emit`.
The engine handles matching, longest common prefix, quoting and editing once.
Applications select the provider for their current field/argument.

```c
solar_os_completion_registry_t providers = {0};
solar_os_completion_files_t files = {.cwd = "/sd"};
providers.providers[SOLAR_OS_COMPLETE_FILE] =
    (solar_os_completion_provider_entry_t){solar_os_completion_files, &files};

/* In an opt-in input field's Tab handler. display_candidate renders one item;
   repeated_tab belongs to this field, and resets on any other input event. */
solar_os_completion_result_t result = solar_os_tui_input_complete(
    text, sizeof(text), &input_state, visible_cells,
    SOLAR_OS_COMPLETE_FILE, &providers, repeated_tab,
    display_candidate, display_user);
```

A shell-like line can call `solar_os_completion_apply` directly. A file dialog
with one raw path field calls `solar_os_completion_apply_field`; spaces are
literal there, with no shell quoting or trailing separator. Both use the same
matching engine and providers. Repaint a widget after changing its text/cursor.
A custom provider sets `providers.providers[SOLAR_OS_COMPLETE_CUSTOM]` and uses
the same APIs, rather than implementing token replacement again.

The C API is available to editors, dialogs, runtime bindings and future apps.
This change opts the Teensy shell into it; it does not change every existing
app's Tab-navigation policy or add Python/Lua language bindings. Lua itself is
not yet integrated on this port. The full non-core upstream shell retains its
existing specialized argument providers; migrating that separate shell is not
claimed by the Teensy acceptance tests.

## Validation

```sh
bash scripts/ports/test_teensy41_completion_host.sh
make -C tests/host completion_test tui_widgets_test shell_completion_test
tests/host/completion_test
tests/host/tui_widgets_test
tests/host/shell_completion_test
python3 scripts/ports/test_teensy41_completion.py --log /tmp/teensy-completion-device.json
```

Host tests cover cursor edits, quoting, raw fields, buffer/error atomicity,
filesystem filtering, bounded output and TUI viewport integration. The device
suite creates a unique `/sd/_completion_*` fixture, uses exclusive USB with an
idle keyboard, and checks paths with spaces, earlier arguments, retained Calc
and detached Python IDs, LCD listings and repeated memory recovery. Fixtures
remain available for inspection. Injected LCD input is not a physical-keyboard
confirmation.

Final-image device acceptance passed on 2026-09-30. Five cycles recovered exactly
29,188 internal / 8,163,984 PSRAM bytes free. Installed HEX SHA256:
`b97611bf4d690e500bfd3928143ad0cf0af29d0d724130fba58914f575c49123`.
Source, firmware and logs are preserved in
`../solar_os-baselines/2026-09-30-completion/`.

Hardware commands now supply subcommand, bus/slot and GPIO candidates through
the shared CUSTOM provider; see [hardware notes](teensy41-hardware-resources.md).
