# Teensy PDA applications

The display build (`SK_ORGANIZER`) adds `calendar`, `contacts`, `tasks`,
`convert`, `library`, `sudoku` and `mines`. See their pages in
[the application manual](../manual/apps.md#calendar).

Calendar, Contacts and Tasks share a CRC-checked version-2 store in
`/flash/.organizer/organizer.bin`, with SD fallback. Only active records are
serialized, through synced staging and backup/rollback. A valid interrupted-save
backup is recovered at startup; otherwise corrupt originals are preserved and
writes blocked until explicit restore. Backups include all three record types.
Records have stable IDs/UIDs; import updates matching UIDs transactionally and
never partially imports a bad file. Capacity is 256 appointments, 512 personal
contacts and 256 tasks. Storage, transaction copies and app state use PSRAM.

The personal contact store is independent of existing radio/MeshCore identities.
Fields include name/company, two phone numbers, email, postal address, birthday,
category and notes. Birthdays appear in Calendar. Appointment/task contact IDs
are optional references; deleting a person removes those links. Tasks support
due dates, priorities, completion and recurrence preserving its original anchor.

Calendar provides month/week/agenda views, search, local/floating appointments,
all-day events and simple recurrence. Its dedicated worker continues with apps
closed, uses the existing RTC/timezone and catches up reminders from the last
24 hours while powered. The small system page shows next appointment/reminder;
NES Start or Calendar's K dismisses it. Existing alarm audio may be unavailable
while another app owns audio. The board cannot power itself on for an event.
The reminder worker uses an external 8 KiB stack; the small-display task only
copies its cached labels and never parses/saves the organizer database.

ICS interchange supports floating local minute-resolution VEVENT/VTODO,
all-day dates, daily/weekly/monthly/yearly RRULEs, the canonical month-end clamp
rules emitted by this app, and display alarm offsets in minutes.
UTC/TZID times, complex rules/exclusions, multi-day events and unsupported nested
components are rejected explicitly. This is not CalDAV/account sync. vCard 3.0
supports FN/ORG, two TEL values, one EMAIL, LABEL/ADR, BDAY, NOTE, CATEGORIES and
UID. Long values, unsupported extra phone/email fields and capacity overflow
reject the whole import. Individual imports are capped at 2 MiB, folded logical
lines at 2047 bytes. Exports require a new filename. Backups are for this firmware
schema; ICS/vCard provide text interchange. Categories are free-text fields,
searched along with names/titles/notes rather than a separate category database.

`convert` supplies case-sensitive engineering/everyday units. US and Imperial
volumes, decimal/binary storage sizes and temperature offsets are explicit.
Definitions follow [NIST SP 811 Appendix B](https://www.nist.gov/pml/special-publication-811/nist-guide-si-appendix-b-conversion-factors/nist-guide-si-appendix-b9);
`hp` is mechanical horsepower, `BTU` the International Table unit, `cal` the
thermochemical calorie, `F` Fahrenheit and `Fcap` farads. Results use double
precision; dimension mismatches and invalid values are rejected.

`library [DIRECTORY]` indexes text/Markdown/EPUB and bundled manuals, opening
Reader as a child and returning to the catalog. It does not add PDF, DRM, web
browsing or browser CSS support. Default folders are documented in its manual.

Sudoku generates puzzles checked for a unique solution; clues are immutable,
conflicts visible and hints counted. Minesweeper has safe first reveal/neighbors,
flooding, flags and chording. Games save progress on exit/suspend and restore it
on reopening, using CRC-checked files under `.organizer`. Either NES input set
can drive a game on the local console. Ownership suppresses dashboard navigation,
releases on suspend/exit, and respects an existing small-display owner. Keyboard
operation remains available. The controllers stay separately sampled; these
puzzles are single-player. Both games fit the 26x16 fallback text console.

Validation:

```
bash scripts/ports/test_teensy41_pda_host.sh
python3 scripts/ports/test_teensy41_pda.py --reboot --log /tmp/pda-live.json
```

Host tests cover dates/recurrence, tasks, CRC persistence and invalid imports,
ICS/vCard escaping and UID merge, unit conversions, generated Sudoku uniqueness,
Minesweeper first-click/win/loss, all app forms/lifecycles, library child launch,
small layouts, NES direction mapping and saved-game resume. Device acceptance
uses unique SD fixtures and local FTP, backs up/restores organizer data and
pre-existing puzzle progress, and leaves Calendar visible. Keep physical inputs
idle during automation. Real third-party calendars with unsupported constructs
need conversion before import; physical controller button play remains a manual
acceptance item.

Monthly/yearly repeats entered in SolarOS clamp to the end of a short month.
`monthly-skip` and `yearly-skip` skip invalid dates instead, as ordinary
iCalendar FREQ rules do. Exports preserve the selected policy with explicit
month-end rules when needed.
