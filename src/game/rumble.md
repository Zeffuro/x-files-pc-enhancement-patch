# Controller vibration

Enable **Controller vibration** in F10 (`Input/Vibration=1`). It defaults on.
The patch replays the PS1-derived action cues on the selected XInput controller.

## Catalogs

Builds validate all three UTF-8, headered TSVs and embed the effect/clip lookup
in QuickTime.qts. Runtime files are not required. Edit the catalogs and rebuild.

| Catalog | Contents |
|---|---|
| `data/rumble-effects.tsv` | Motor strengths, durations, source ticks and overlap policy |
| `data/rumble-clips.tsv` | 17 canonical clip paths and their zero-offset effects |
| `data/rumble-events.tsv` | 41 guarded selector outcomes, including negatives and helper calls |

| Effect | Low / high motor | Duration | PS1 large / small / ticks |
|---|---|---|---|
| `ps1_short` | 44975 / 65535 | 83 ms | 175 / 1 / 5 |
| `ps1_long` | 64250 / 65535 | 1000 ms | 250 / 1 / 60 |

Amplitudes use large-byte * 257 and binary-small * 65535, not physical-force
calibration. Durations assume **60 timer updates/sec**, which has not been
measured. Preserve `ps1_ticks` and `timing_basis` when revising timing.
An incoming effect replaces strengths and extends the end to the later deadline.

## Trigger and cancellation contract

- Match the complete relative path, ignoring ASCII case and slash direction.
  Trigger once on a stopped-to-forward start at zero. Only an explicit rewind
  rearms the cue, so even a pause/resume at zero cannot replay it.
- Direct action cues use their mapped clip. Helper cues use the successful
  `xs/92148.amv` start and replace the old authored gunfire pulse. There is no
  additional dispatcher/consequence pulse. Clip 21782 starts its long cue immediately.
- The event table documents native guards. Clip fallback accepts the asset in
  any script context, not only those selectors. `none` rows and failed helpers
  do not add a trigger. Selectors 461/470 can run a consequence twice but call
  the gunfire helper only once.
- Stop, seek, completion, disposal and deactivation cancel only the owning
  movie's effect. They preserve the controller lease for native stop/rewind/play.
  Focus loss, menus, settings-off, disconnect and shutdown revoke eligibility.
- Eligible controller polling refreshes a 100 ms lease, including during normal
  cinematic message servicing. A separate 10 ms watchdog silences stalled output.
  Neither a movie cue nor the watchdog grants or refreshes ownership.
- DVD numeric action clips currently use QuickTime fallback. If MPEG coverage
  expands, route through the canonical XMV identity and the same shared motor state.

## Native reference

Selector IDs (context +0x18), action cases and movie IDs are separate domains.
Example: selector **441** -> case **18** -> asset **21782** -> `xv/21782.xmv`.
`active_type` guards compare with 7, `special_state` with 23. `any`, `not_7`
and `not_23` are literal predicates. `gunfire_helper` requires successful sound
lookup/start. `clip_fallback=1` links a direct event to its clip row.

| Exact executable | Dispatcher RVA | Consequence RVA |
|---|---|---|
| CD 1.00.12 | 0x0B4470 | 0x0B4A20 |
| CD 1.00.19 | 0x0B4180 | 0x0B4730 |
| CD 1.00.20 | 0x0B4390 | 0x0B4940 |
| DVD 2.00.00 | 0x0F4510 | 0x0F4AC0 |

Add the verified module base. Both use three 32-bit stack arguments and callee
cleanup. Dispatcher argument 1 is the context, consequence argument 2 the case.
The active-type virtual slot differs in CD 1.00.12. Native memory access must
retain exact-build identity checks. Physical motor feel and PS1 timer parity
still require hardware validation.
