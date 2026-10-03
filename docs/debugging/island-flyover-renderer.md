# Island Flyover renderer investigation

Run: `out/UserData/Logs/base_1790969838_pid29321`.

The final abort is Aurora's `invalid wrap mode 3`, while the guest is in
`GX::CallDisplayList` (0x80039910), called by `RFL::DrawXluCore` at 0x80103518.
The list at 0x80D17880 is 0x35D bytes. Walking the saved bytes as position,
normal and texture-coordinate 8-bit indices consumes exactly 861 bytes:
14 triangle strips followed by 156 triangle vertices. The data itself is
structurally valid for that layout.

Earlier console output contains repeated ignored nested display lists, invalid
indexed XF loads, and matrix float bytes interpreted as draw commands. This
supports investigating decoder state rather than treating the final wrap-mode
value as a legitimate texture setting.

## Corrected defects

- Register-only CP lists bypassed the HLE CP decoder, leaving its vertex layout
  different from the state sent to Aurora.
- Register-only nested lists could reach Aurora with guest call addresses;
  Aurora deliberately does not follow those calls. They now pass through the
  scan/flatten path.
- Indexed XF lists need guest array translation even when they contain no draw.
- The applied-state cache copied all guest VAT rows and marked them current
  after publishing only one row. Immediate drawing can also change the Aurora
  encoder independently. Submission now invalidates the vertex-state cache;
  VAT validity is tracked separately for each format.

Patch: `patches/runtime-wsr/0020-display-list-state-coherence.patch`.

## Verification and limits

`python3 scripts/test_gx_display_lists.py` links against the actual built runtime
and captures only final Aurora display-list submissions. It tests CP-only state,
nested XF commands, rewritten child lists, and restoring both single and mixed
vertex formats after direct encoder changes. The original runtime failed the
CP-only test. No tracing was added.

The executable was rebuilt with `bash scripts/build.sh --jobs 6`. A full Island
Flyover replay is still needed to establish that the reported stretching and
crash are resolved. These tests do not exercise GPU rendering. The run also
contains unmapped guest reads; their relationship to the visual failure has
not been established.

## Follow-up: empty draws consume later commands

The user's next run, `base_1790976935_pid42687`, still failed, now with
`comp_type_size: Unsupported component type GX_RGBA8`. Thus patch 0020 did
not resolve the command corruption. Its final material list at 0x9073B620
contains valid BP commands; the fatal error can occur while draining earlier
queued commands before that list is processed.

A debugger stop at Aurora's first ignored nested call captured the pending host
FIFO, rather than adding guest instruction tracing. It contains an empty
`GX_LINES` draw (`a8 00 00`) followed by guest display-list call packets in the
host FIFO. Further along, XF command payloads are shortened/misaligned.

Both HLE GXBegin and the raw FIFO decoder set `inBegin=true` for zero vertices.
The completion logic decrements the count only when it is greater than zero,
so an empty draw never closes. Later command bytes are consumed and emitted as
vertex attributes. Patch 0021 leaves the parser at a command boundary for empty
draws, including headers split across writes and empty draws inside bursts.

The regression test fails on the previous executable and passes after the fix.
It also checks that CP commands immediately following an empty burst draw update
the guest layout and leave no queued bytes. The executable was rebuilt. A complete
Island Flyover playthrough remains unverified; diagnostic startup runs alone do
not establish that all flight rendering or the separate unmapped reads are fixed.
