# Changelog

## 1.2.0

- Updated the plugin for Workers & Resources: Soviet Republic v1.1.1.9 and
  TesmioLoader API 4.
- Reworked the HDO's mixed-fleet task index so road vehicles receive only
  road-compatible assignments and cargo helicopters receive only fully
  air-reachable assignments.
- Prevented road-only sources or destinations from blocking helicopter
  distribution work in the same HDO.
- Added a persistent helicopter dispatch bridge that rechecks work after load,
  helicopter return, unloading, HDO assignment/fleet changes, panel closure and
  destination-threshold updates.
- Allowed a helicopter to inherit another eligible HDO task immediately after
  unloading instead of always returning to the office between jobs.
- Added saved-HDO initialization so existing assignment lists rebuild their
  mixed derived task index when a save is loaded.
- Allowed the HDO to select completed cargo heliports without road connections
  as valid sources or destinations, including the correct valid-selection
  overlay.
- Fixed the Soviet and Western overseas controls for WRSR v1.1.1.9 while
  preserving native Distribution Office assignment behaviour.
- Preserved native cargo checks, percentage thresholds, reservations, route
  creation, loading, unloading and vehicle movement for every accepted task.
- Hardened hook preflight and partial-install handling for safer failure on an
  unsupported or conflicting game layout.
- Updated the release DLL metadata and packaging for v1.2.0.

## 1.1.0

- Fixed existing helicopters being accepted for reassignment to the Helicopter
  Distribution Office but failing to register as resident office vehicles after
  arrival.
- Rehomed helicopters now use WRSR's native helicopter residence and parking
  transition, allowing them to land, appear in the office vehicle list and
  receive Distribution Office work normally.
- Hardened plugin startup so the residence hook is validated before the
  established HDO hooks are applied.

## 1.0.0

- Initial public release.
