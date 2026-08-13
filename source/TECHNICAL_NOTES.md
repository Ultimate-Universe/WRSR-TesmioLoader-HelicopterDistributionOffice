# Technical notes — v1.2.0

Target game build: **SOVIET64.exe v1.1.1.9**  
TesmioLoader API: **4**

The plugin keeps the Workshop building as a native Distribution Office with
the `AIRPLANE` subtype. WRSR therefore owns the visible order list, cargo
configuration, thresholds, task reservations, route creation, loading,
unloading and vehicle movement. The plugin provides a compatibility and
dispatch layer that presents each resident vehicle with only the assignments
it can physically service.

No separate core DLL is required. No private task data is written to the save.

## Runtime model

The HDO has one ordinary Distribution Office assignment list. That shared list
may contain:

- normal road-connected sources and destinations;
- native cargo heliports;
- the HDO itself; and
- the Soviet and Western overseas pseudo-buildings.

WRSR builds 0x80-byte derived task rows from those assignments. Its normal
Distribution Office rebuild requires each assignment's road-reachability byte,
which would remove a heliport-only row from the derived task vector. During an
HDO rebuild, the plugin temporarily promotes assignment records that point to a
native cargo heliport or selected overseas node. The original WRSR rebuild then
creates one complete mixed task index. Every promoted byte is restored before
the hook returns.

For each assignment transaction, the mixed index is synchronously projected by
vehicle class:

- Cargo helicopters see only rows whose source and destination are air
  reachable: HDO, native cargo heliport or overseas node.
- Road vehicles see only currently road-reachable rows that do not use a
  dedicated heliport or HDO overseas endpoint.

The office's real vector header and rows are restored immediately afterward.
The filtered view therefore covers native task sorting and loaded-cargo paths
without deleting UI assignments or changing saved data.

## Repeating helicopter dispatch

The HDO queue engine uses the same native task preparation and per-vehicle
assignment routines as the Distribution Office monitor. It refreshes task
supply, demand, threshold and reservation state before offering work.

A parked helicopter is reconsidered when relevant HDO state changes, including:

- initial restoration of an HDO from a save;
- helicopter return or resident-fleet changes;
- assignment or configuration changes;
- HDO panel closure; and
- the native three-second destination-threshold cadence.

WRSR parks a returned helicopter with a flight state and current-building value
that its road Distribution Office scan does not treat as an assignable vehicle.
For the exact idle-on-its-own-HDO-pad case, the plugin temporarily presents a
load-compatible readiness view, calls the full native readiness predicate and
restores the original fields before calling native assignment. A real outbound
route is used as the pending-departure latch, preventing repeated route or
reservation replacement while the helicopter waits for take-off.

At `destination -> HDO` route advance, the plugin also offers the helicopter
one atomic native assignment opportunity after unloading but before the return
leg is selected. A successful native assignment replaces the return with a new
`source -> destination -> HDO` route. If no eligible task exists, the original
route advance runs unchanged and the helicopter returns to the office.

## Cargo-heliport selection without roads

The native Distribution Office selector owns collision discovery, validation,
the red/valid overlay and the final assignment transaction. Its road mode
rejects a building before consulting cached connectivity unless the candidate
contains a road-connection record.

Only while an HDO is selecting a completed native cargo heliport, the plugin:

1. projects one road-shaped connection record into the candidate's connection
   vector;
2. promotes the selector's cached graph result;
3. invokes the original selector to draw the valid overlay and create the
   ordinary assignment/back-reference; and
4. restores the original connection-vector header synchronously.

Normal Distribution Offices, non-heliport buildings, unfinished buildings,
duplicate targets and offices at the native target limit retain stock
validation.

## Vehicle purchase and rehoming

The vehicle/building compatibility hook is limited to a helicopter being
checked against the exact HDO building type. That case is delegated to WRSR's
native helicopter-pad compatibility helper; all other combinations use the
original compatibility routine.

During the synchronous native arrival update for an existing helicopter being
rehomed into an HDO, the plugin temporarily presents only that HDO descriptor as
`AIRPLANE_PARKING`. It does not write the vehicle's home, route, resident roster,
parking reservation or pad occupancy. The real descriptor is restored before
the native call returns.

## Menu and overseas controls

The asset contains two real `HELIPORT_STATION` records. During the bottom-menu
classification call only, those records are masked for the HDO descriptor so
the building remains grouped with Distribution Offices. The functional station
vector is restored synchronously.

The HDO panel hook draws Soviet and Western controls alongside the native
Distribution Office panel. Clicking one uses WRSR's normal assignment
allocation and back-reference helpers to add the corresponding overseas node.

## Relevant WRSR v1.1.1.9 targets

- `FUN_1400797e0` / RVA `0x0797E0` — bottom-menu classification.
- `FUN_1401c6050` / RVA `0x1C6050` — Distribution Office monitor.
- `FUN_1401de2b0` / RVA `0x1DE2B0` — per-task eligibility.
- `FUN_1401e2380` / RVA `0x1E2380` — derived task rebuild.
- `FUN_1401e3bd0` / RVA `0x1E3BD0` — live task refresh.
- `FUN_1401e5350` / RVA `0x1E5350` — per-vehicle assignment.
- `FUN_1402b78b0` / RVA `0x2B78B0` — Distribution Office target selector.
- `FUN_1403e2900` / RVA `0x3E2900` — vehicle/building compatibility.
- `FUN_1403e3b80` / RVA `0x3E3B80` — helicopter-pad compatibility helper.
- `FUN_14067db00` / RVA `0x67DB00` — route advance.
- `FUN_1406bd6b0` / RVA `0x6BD6B0` — native operational readiness.
- `FUN_1406cd1e0` / RVA `0x6CD1E0` — service/unload and residence update.
- `FUN_140741170` / RVA `0x741170` — Distribution Office panel.

## Safety boundaries

- Every required Init-phase detour target is preflighted before the first hook
  is installed.
- Direct native call targets are validated separately from detour prologues.
- A preflight mismatch declines before any HDO hook is installed.
- TesmioLoader has no unhook API. If an unexpected install failure happens
  after the first successful detour, the DLL remains resident and suppresses
  Start-phase UI installation so no patched function can retain a stale module
  pointer.
- Temporary descriptor, task-vector and connection-vector changes are restored
  within the same synchronous call.
- Private task projections use fixed-capacity storage and reject malformed or
  oversized native vectors.
- The plugin contains no networking, packer, obfuscation, CLR payload or
  self-modifying executable section.

The offsets and prologue validation in this release are specific to WRSR
v1.1.1.9. An unsupported game build is intentionally rejected rather than
patched optimistically.
