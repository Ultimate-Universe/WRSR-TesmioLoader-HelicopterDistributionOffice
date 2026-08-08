# Technical notes

Target: **SOVIET64.exe v1.1.1.7**  
TesmioLoader API: **3**

The plugin keeps the HDO as a normal `TYPE_DISTRIBUTION_OFFICE` / `SUBTYPE_AIRPLANE` Workshop building and changes only the specific native checks needed for mixed road/helicopter operation.

## Hook map

- `FUN_1403e2860` / RVA `0x3E2860` — vehicle/building purchase compatibility. HDO helicopter checks are routed through the vanilla helicopter-pad helper at `FUN_1403e3ae0`.
- `FUN_1401de240` / RVA `0x1DE240` — Distribution Office per-vehicle task eligibility. Road vehicles owned by the HDO reject tasks whose source or destination is a cargo heliport; helicopters retain native eligibility.
- `FUN_1400797e0` / RVA `0x797E0` — bottom construction-menu classification. The HDO descriptor's helipad station vector is masked only for the synchronous classification call so the game groups it with ordinary Distribution Offices. The real station vector is restored immediately.
- `FUN_1406cd0c0` / RVA `0x6CD0C0` — vehicle residence/arrival update. When an existing helicopter is physically arriving at an HDO after a vehicle-UI rehome, only that HDO instance is temporarily presented with a private descriptor copy whose type is `AIRPLANE_PARKING`. This admits the helicopter through WRSR's native residence transition. The game remains responsible for home ownership, resident-roster membership, parking state and helipad occupancy.
- `FUN_140741050` / RVA `0x741050` — normal Distribution Office panel. The plugin adds Soviet and Western overseas buttons while preserving the native panel.

## Rehome safety

The v1.1.0 rehome shim does not directly write the helicopter's home pointer, route state, resident-office vector, parking reservation or helipad occupancy. It changes one type field in a private descriptor copy for one synchronous native residence-update call, then restores the building's original descriptor pointer before returning.

The residence hook is installed before the established HDO hooks. If that hook cannot be safely relocated, plugin initialization fails before applying the other HDO detours, avoiding a partial-plugin startup state.
