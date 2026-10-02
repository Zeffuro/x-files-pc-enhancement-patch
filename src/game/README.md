# Native game profiles

| Folder | Contents |
| --- | --- |
| `profiles/` | `builds.json` has executable hashes, RVAs and offsets. `functions.json` identifies verified call targets. `generated.h` is the generated C++ profile. |
| `layouts/` | Typed native memory views. `database/` holds cached objects, story objects, variables and asset references. `input/` holds event lists and inventory selection. `ui.h` holds application, inventory and dialogue views. |
| `assets/` | Bounded clip-label index read from the installed game database. No game data is bundled. |
| `database/` | Read-only HDB parsing and bounded file access. The browser and cached snapshot tools live in `src/devtools/database/`. |
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

`layouts/input/events.h` maps the 12 native event lists shared by supported builds.
The developer overlay reads registered pointer-event lists without dispatching
them. A click event can contain button-specific script conditions, so its presence
does not by itself identify a left-click or right-click action.

The HDB developer browser copies cached native objects on the game thread. Typed
prefixes include VCName, VCHotSpot, VCTitle and VCStdAction. Their verified name,
action and trigger-list IDs link only to uniquely identified cached objects.
Unknown scalar fields remain raw. Name strings are bounded and currently show
printable ASCII only. Copied prefixes do not describe serialized record lengths.
Stored VCVariable class 0x53 uses a 24-byte version 1 descriptor with a name
mark and length, value bits, owner ID and two raw bytes. The browser preserves
the unsigned value and its signed 32-bit projection. Its stored name reader
accepts 2..1024 counted bytes of printable ASCII ending in NUL. Other names
remain raw.
Variable comparisons use class, ID and database identity across explicit refreshes.
New, unreadable or ambiguous objects do not establish a change baseline.

VCTriggerList and VCActionList expose IDs from an already cached class-0x0b
resource. VCTrigger links its action-list ID and labels type 8 as Object Activation.
Its runtime arrays supply 8 integer, 12 BOOL32 and 4 signed-byte parameters.
Action operand kind 1 selects slots 0..7, 10000..10011 or 20000..20003.
Unsupported selectors stay explicit. Copied parameter values depend on dispatch
context and do not establish the values used when an action ran.
Stored VCTrigger class 0x51 decoding supports schema 0x501, four-byte marks and
record version 1. The reader consumes 11 bytes containing two raw flag bytes,
the big-endian version, a big-endian action-list ID and an event byte.
Stored links resolve only unique class 0x42 records in the same file. Event 8
uses the verified Object Activation label. Other event meanings remain raw.
Runtime parameter arrays are absent from this serialized definition.
Stored VCActionList 0x42 and VCTriggerList 0x52 decoding supports version 1
descriptors and class 0x0b ID resources. Class 0x06 branches contain child marks.
Resource trees flatten in stored order, preserving duplicate and null IDs.
The aggregate member count must match the complete resource traversal.
Unsupported descriptors, versions, cyclic or overlapping nodes and exceeded
limits retain raw bytes. The browser limits lists to 4096 members, 512 resource
nodes and 32 levels. Decoded byte counts describe consumed fields across nodes,
not allocation lengths. Action IDs link to class 0x41 and trigger IDs to class
0x51, using unique nonzero same-file identities without native object loading.
Stored VCAssetRefList class 0x36 uses the same version 1 descriptor and class 0x0b
ID resource tree. Its complete ordered IDs and raw descriptor metadata are
available in Overview and search. Raw includes the exact 32 consumed descriptor
bytes and every complete consumed resource node. The browser preserves duplicate,
null and unsigned IDs and applies the same member, node and depth limits.
Descriptor aliases and indexed definitions inside consumed spans are rejected.
The native iterator uses generic member routing through the raw descriptor
word at offset 22. A fixed member class is not established, so these IDs have no new
stored candidate links. Existing Asset and 3D Sound links can reach the list.
Decoding does not load native members or establish asset execution or effects.
The stored browser also decodes the following version 1 classes in format 5,
schema 0x501 databases. Registered names and widths follow the native readers.
The [upstream format documentation](https://github.com/mthcore/x-files-game)
provides additional context, but some published class IDs and layouts differ.

| Classes | Stored fields |
| --- | --- |
| 0x27 VCTitle, 0x28 VCNode, 0x29 VCLocaton, 0x2a VCViewPoint | Ordered reference lists and complete inline suffixes. VCTitle also has three counted byte resources. |
| 0x2b VCView, 0x2c VCCharacter, 0x2d VCCharView, 0x31 VCConversation, 0x33 VCNav | Fixed scalar fields and inline geometry where present. |
| 0x2f VCName, 0x35 VCAssetRef, 0x50 VCString | Counted byte resources with explicit marks and lengths. VCAssetRef retains its trailing raw fields. |
| 0x2e, 0x32, 0x34, 0x38, 0x3a, 0x3b, 0x4d, 0x4e, 0x55 | Ordered reference lists. VCIFaceLayout 0x4e retains its extra word and byte. |
| 0x37, 0x39, 0x3c, 0x3d, 0x3e, 0x40 | Rectangle, point, category and fixed three-word array fields. |
| 0x46 VCEnabled | Full target ID, raw control byte and unsigned query-class byte. A unique nonzero same-file target is a candidate link. |
| 0x47 through 0x4c, 0x4f | Icon, inventory and interface fields with complete unsigned scalar values. |
| 0x54 VCStdAction | Three big endian words. |
| 0x56, 0x58, 0x5a, 0x5b, 0x5c | Counted raw resources and ordered little-endian 32-bit words. Trailing bytes remain available. |
| 0x57 VCGameState, 0x59 VCPhoto | Complete inline fields, five opaque GameState resources and one counted Photo resource. |

Fixed descriptors start with two raw flag bytes and a four-byte big endian
version. Their remaining widths vary by class. Counted resources preserve binary
bytes, including embedded NULs. Each resource is bounded to 65536 bytes. Overview
shows an escaped 256-byte preview, while search and Raw retain the complete data.
Signed 16-bit projections are shown only for native signed geometry and proven
signed fields. Original unsigned values remain visible.
Scene composites validate the whole descriptor, every counted resource and the
complete ordered list together. Conflicting resource spans or invalid suffixes
retain the raw record. Generic list members have no inferred fixed-class links.
Opaque resources and unknown scalar meanings stay explicit. Stored decoding
neither loads native objects nor establishes execution, current state or effects.
Database text candidates are ASCII scan fragments that preserve line breaks.
Their Text tab shows source text, including HTML. Binary boundaries do not
establish complete strings, current records or complete documents.
Stored VCAction 0x41 decoding supports schema 0x501, four-byte marks and version 1.
Its 15-byte descriptor contains flags, version, an absolute payload mark, a byte
length and a raw type byte. Payload operands are big endian on disk and differ
from copied native operands. A flagged 12-byte predicate and an exact 11-byte
Statement body expose encoded operands and operators. An exact five-byte Asset
body exposes a big endian asset-list ID and a raw selector byte. Its reference
links only to a unique nonzero same-file VCAssetRefList class 0x36 identity.
An exact six-byte Timer body exposes a big endian unsigned duration word,
an unsigned timer ID byte and a raw control byte. The control's low seven bits
and high flag bit remain numeric. The ID matches runtime timers and has no
stored database definition link. These fields do not establish current timers
or execution timing.
An exact nine-byte Enable body exposes a big endian target ID, a second big
endian word with unknown meaning and a raw control byte. The native reader
tests that byte as nonzero. Target lookup requests VCEnabled class 0x46 through
native database routing. No same-file stored definition link is established.
The browser preserves both complete words and the byte without loading or
enabling native objects. Conditional predicate variable links remain available.
An exact 16-byte Set View body exposes four big endian IDs for VCView class 0x2b,
VCNode class 0x28, VCLocaton class 0x29 and VCViewPoint class 0x2a. VCLocaton is
the native class spelling. Each may link to a unique nonzero same-file definition
of that exact class. These stored candidates do not establish native runtime
resolution, execution context or view changes. Conditional bodies retain the
12-byte predicate prefix. Complete raw bytes remain available.
An exact five-byte Interface body exposes a big endian layout ID and a raw
unsigned control byte. Native lookup requests VCIFaceLayout class 0x4e.
The ID may link to a unique nonzero same-file definition of that exact class.
The candidate and control byte do not establish runtime resolution or effects.
Conditional bodies retain the 12-byte predicate prefix and complete raw bytes.
The C++ Function body contains 15 raw name bytes, an unsigned argument count,
big endian argument words and a separate unsigned kind-byte array. Its exact
length is 16 plus five times the count, following any 12-byte predicate prefix.
The browser bounds the name display to 15 bytes and the first NUL, escapes
nonprintable bytes and preserves the complete name field including trailing bytes.
Missing name termination stays explicit. Argument words retain all 32 bits and
kinds remain numeric. Function lookup, argument evaluation and execution are not
performed. These fields add no stored definition links.
An exact ten-byte 3D Sound body exposes a big endian asset-list ID, a raw selector,
a raw byte at body offset 5 and two big endian 16-bit words at offsets 6 and 8.
The browser preserves both unsigned words and their native signed 16-bit
projections. Their axes, units and effects remain unknown. The ID may link to a
unique nonzero same-file VCAssetRefList class 0x36 definition. This candidate does
not establish native resolution or sound effects. Conditional bodies retain the
12-byte predicate prefix and complete raw bytes. Other bodies remain raw.
The browser copies up to 4096 payload bytes and rejects descriptor or index-node
overlap, other indexed definition starts inside consumed ranges and out-of-file
payloads. Variable links use
unique nonzero same-file class 0x53 identities without loading native objects.
Operand kind 4 encodes an unsigned index into a separate trigger-context
collection. The browser preserves the index without reading or evaluating that
collection. Its current length and standard inline capacity do not establish
an action's execution context. Kind 4 destinations are ignored by the eight
supported Statement operations, which also skip their right operand evaluation.
VCAction copies at most 4096 cached payload bytes. A flagged 12-byte predicate
prefix and an exact 11-byte Statement body decode operand kinds, IDs and native
operator labels. Variable operands link to class 0x53. Unknown kinds, operators,
subtypes and extended bodies remain raw. These fields describe encoded actions
and do not assert execution or evaluate the game's trigger context.

`profiles/variables.h` keeps registered variable slots and optional verified labels.
Keep registration names visible when adding friendly labels. Unknown meanings
remain unnamed rather than being inferred from current values.
