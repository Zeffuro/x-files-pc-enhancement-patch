# Native game profiles

| Folder | Contents |
| --- | --- |
| `profiles/` | `builds.json` has executable hashes, RVAs and offsets. `functions.json` identifies verified call targets. `generated.h` is the generated C++ profile. |
| `layouts/` | Typed native memory views for application, inventory, dialogue, variables and input event lists. |
| `assets/` | Bounded clip-label index read from the installed game database. No game data is bundled. |
| `scripts/` | Profile generator and IDA naming bridge. |

Run `python src/game/scripts/generate_profiles.py` after editing `profiles/builds.json`. Add `--check` to detect stale generated C++. The build runs this check automatically. The generator also validates `profiles/functions.json`. Function entries refer to profile fields and do not repeat build addresses.

`build_for_hash` identifies an exact executable. Callers must check `Build::profile` before attaching native hooks. Layout views use profile offsets for known build differences. Add measured values only after validating the executable hash and layout. Exposed prefixes are not native allocation sizes.

Profiles cover CD 1.00.12, CD 1.00.19, Japanese CD 1.00.20 and the DVD build.
Italian CDs share the CD 1.00.19 executable. Italian and Japanese editions have
complete seven-disc setup catalogs. Japanese native resource strings use code page 932 to match
the executable's Shift-JIS font selection. The other profiles use code page 1252.

`profiles/functions.json` is the canonical list of native call targets, their roles and their observed argument passing. It refers to fields in `builds.json`, which supplies the RVA for each executable hash. The call shapes are not IDA type declarations. Data globals, vtables and layout offsets remain outside the function catalog.

`text_draw` wraps DrawTextA with a native drawing context, rectangle, string and
text style. `text_out` wraps TextOutA with a context, point and string. Both use
the HDC at context offset 0x0c. `string_resource` loads a module string into a
native string, with the module handle at owner offset 0x04. These addresses are
mapped for each supported profile. Native graphic ownership, fonts and repaint
dispatch still need a caller-specific integration before reusing them for new UI.

Inventory display names come from the loaded asset reference's cached path.
The `asset_reference` vtable validates the resource type before bounded reads.
The inspector shows the basename and full asset path. These authoring names are
distinct from localized hover text and registered inventory selection values.

`XFiles.hdb` holds game definitions. `XFiles.gam` supplies the initial state, while
named `.x` saves hold serialized player state in the same container family.
The native loader validates a save beyond its header. `src/saves/catalog.*`
provides read-only discovery of existing loose saves. The browser stores numbered
slots separately under `saves/slots`. Each published metadata record identifies
an immutable save generation and optional thumbnail. Replacement publishes the
metadata atomically after both files are complete, then removes obsolete data.

The optional browser composes through the final native canvas transfer. Its artwork
comes from the installed PFF and its language from the resource DLL. Supplemental
text uses DLG.TTR or the Japanese executable's MS Gothic face. Browser controls are
patch-owned rather than native script widgets.

New saves may have a generation-matched `.preview` sidecar containing a relative
movie path, video track ID and displayed sample. Silent preview decoding is independent
of the game's movie handles and callbacks. Only recorded XN scenes animate. NAVM
archives stay on the recorded sample, and other clips remain static. There is no HDB
mutation or guessed serialized-state mapping for older saves. Preview failure falls
back to the thumbnail. Deleting a slot unpublishes its metadata before removing its
generation files, leaving unrelated files alone.

In IDA, execute `scripts/ida_import.py` to preview `XFiles_<field>` names for the input-file hash. To apply, import the script and explicitly call `run(apply=True)`. The bridge waits for autoanalysis, requires a matching 32-bit input hash, rebases each RVA, and requires each cataloged function to start at its target address. It skips user names, collisions and unmapped addresses. It never applies type declarations or changes existing types. It does not create functions, structures or data items.

`layouts/input_events.h` maps the 12 native event lists shared by supported builds.
The developer overlay reads registered pointer-event lists without dispatching
them. A click event can contain button-specific script conditions, so its presence
does not by itself identify a left-click or right-click action.

`profiles/variables.h` keeps registered variable slots and optional verified labels.
Keep registration names visible when adding friendly labels. Unknown meanings
remain unnamed rather than being inferred from current values.
