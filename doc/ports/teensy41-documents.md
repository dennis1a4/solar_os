# Reader and Writer on Teensy 4.1

The display firmware enables the upstream `reader` and `writer` apps with
`SK_DOCUMENT_APPS=1`, including the current `teensy41_power_board` build. Both use
the main 800x480 RA8875 and PSRAM. The small display retains its system dashboard;
these graphical apps require the main display.

```
reader /sd/books/book.epub
reader /sd/notes/document.md
reader --pager /sd/books/plain.txt
reader man:app.writer
writer /sd/notes/draft.md
writer
```

Reader supports plain text, Markdown and EPUB, scroll/page navigation, zoom,
case-insensitive find and saved position/EPUB chapter. `+`/`-` zoom, `/` searches,
`n`/`N` repeat, Home/End jump, arrows scroll and Page Up/Down page. `--pager`
makes Up/Down page as well. EPUB navigation continues into adjacent chapters.
Ctrl+Z suspends Reader; `fg` resumes it. Esc/q/Ctrl+] closes it. Files recognizes
its document extensions and can launch it as a child, returning to Files on exit.

Writer keeps the upstream Markdown/source editing behavior, selection, clipboard,
undo/redo, find/replace, formatting menu and idle recovery. Ctrl+S saves; Ctrl+A/C/X/V
select/copy/cut/paste; Ctrl+B/I/K apply bold/italic/link; F1 opens formatting;
Ctrl+F finds, F3 repeats and Ctrl+R replaces. Ctrl+Z is **Undo**, Ctrl+Y is Redo,
and **F12 suspends Writer** on Teensy. `fg` resumes it. Esc/Ctrl+] requests exit,
with Save/Discard/Cancel for unsaved work. The recovery prompt offers
Recover/Discard/Cancel. Without a filename the first save asks for a path.

Files may live on SD (`/sd`), USB (`/usb`) or flash (`/flash`). Reading positions
are stored in `/flash/.reader/positions`, and Writer's per-path metadata/recovery under
`/flash/.writer` (SD fallback when flash is unavailable). Writer saves
through a synced staging file, backup/rename, verification and rollback. Files
above 256 KiB are rejected before editing. Reader input is bounded to 2 MiB;
allocation/layout failures return an error rather than using internal RAM as a
large-document fallback. Document text, layout and editing buffers use PSRAM.

The existing PNG/JPEG decoder handles local and embedded EPUB images. WebP is
unsupported on this Teensy backend. EPUB uses the upstream bounded ZIP/XML/HTML
services; DRM/encrypted books, browser-style CSS and general web rendering are
outside this reader's scope.

`lcd key ctrls`, `lcd key ctrlz`, `lcd key f1`, `lcd key f3` and `lcd key f12`
allow USB-side diagnostic input. Other apps keep Ctrl+Z suspend behavior. Display
updates use the existing changed-tile presenter; Writer processes keystrokes
before its next UI tick and does not repaint for every input byte.

Validation commands:

```
bash scripts/ports/test_teensy41_documents_host.sh
python3 scripts/ports/test_teensy41_documents.py --log /tmp/documents-live.json
```

Host tests cover document/source layout, Writer UTF-8 editing and undo/redo,
safe-save failure rollback, and a real deflated two-chapter EPUB with a relative
PNG asset. Device acceptance uses unique SD documents and local FTP upload;
it exercises navigation, position/chapter persistence, editing, recovery,
retained sessions, Files child launch, guarded errors and PSRAM cleanup.

Validated on 2026-10-26 with the power-board image: sanitizer host suites,
manual generation, and live RA8875/USB acceptance passed. Live checks include
Markdown/image rendering, compressed EPUB chapters and saved chapter/zoom,
graphical manual pages, Writer save/undo/redo/replace/clipboard, replacing an
older recovery snapshot, F12 suspend and recovery after close/reopen, both apps
retained together, Files child return and invalid/oversized inputs. Three repeated
Reader/Writer cycles returned both heaps exactly to baseline: 327,252 bytes
internal free and 8,123,076 bytes PSRAM free. The retained-session regression also
passed 1,000 lifecycle cycles. Test documents and their per-document metadata
were removed; the graphical Writer manual was left visible.

The final image uses 1,772,672 bytes flash, 449,184 bytes RAM1 and 256,196 bytes
RAM2. Document code is placed in cached flash, with buffers/layout/undo in PSRAM;
the existing RAM startup guard remains enabled. Reading-position updates use the
storage service's staged replacement/rollback, and Writer uses its verified
replacement path for metadata and recovery as well as document saves.
