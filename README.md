# Helicopter Distribution Office

A hybrid Distribution Office for **Workers & Resources: Soviet Republic** that
can manage road cargo vehicles and cargo helicopters from one building.

Current version: **1.2.0**  
Workshop item: **3778940406**

The Workshop asset provides the building, parking and helipads. The included
TesmioLoader plugin supplies the mixed-fleet dispatch logic that WRSR does not
natively provide.

## What the mod adds

- 12 road-vehicle parking bays.
- 2 built-in helicopter pads.
- Support for additional attached heliports through WRSR's native heliport-area
  system.
- A combined office capacity of 20 vehicles.
- One normal Distribution Office order list shared by road vehicles and cargo
  helicopters.
- Direct Soviet and Western overseas buttons for helicopter import/export jobs.
- Support for purchasing helicopters through the HDO or rehoming an existing
  helicopter through its vehicle window.
- Cargo heliports remain valid HDO sources and destinations even when they have
  no road connection.
- Existing HDO configurations are reconstructed from their saved assignment
  lists when a game is loaded.

## How the dispatch script works

You configure the HDO through the normal Distribution Office interface: add
sources and destinations, choose cargo, and set the percentage thresholds.
WRSR still controls those settings, cargo availability, demand, reservations,
loading, unloading and route creation.

Before WRSR assigns a vehicle, the plugin gives that vehicle a compatible view
of the HDO's shared task list:

| Vehicle | Jobs it can receive |
| --- | --- |
| Road cargo vehicle | Road-reachable jobs that do not require a dedicated cargo heliport or HDO overseas endpoint |
| Cargo helicopter | Jobs whose source and destination are the HDO, a cargo heliport, or a selected overseas endpoint |

Road-only assignments therefore do not block the helicopters, and
heliport-only assignments do not block the road fleet.

The HDO checks for eligible helicopter work when a save is loaded, when a
helicopter returns, after unloading, after assignment or fleet changes, when the
HDO window closes, and on the normal destination-threshold update cycle. If a
new eligible job exists after unloading, a helicopter can inherit it immediately
instead of returning to the HDO first. If no job is available, it returns home
normally and waits.

## Cargo heliports without roads

When the HDO is selecting a source or destination, a completed cargo heliport
is accepted even if it has no road connection. It receives the normal valid
selection overlay and is added to the HDO through WRSR's ordinary assignment
system.

This exception is specific to the Helicopter Distribution Office. It does not
make roadless heliports valid for ordinary road Distribution Offices, and it
does not make an ordinary road building accessible to helicopters.

## Overseas helicopter jobs

The Soviet and Western buttons on the HDO panel add WRSR's overseas endpoints
as Distribution Office targets. They can then be configured like other HDO
assignments for helicopter import or export work.

These are air endpoints. A road vehicle will not be sent to them through the
HDO as a substitute for a road customs house.

## Requirements

- **Workers & Resources: Soviet Republic v1.1.1.9**.
- **TesmioLoader API 4** / the current v1.1.1.9-compatible TesmioLoader.
- Steam Workshop item **3778940406**.
- A 64-bit Windows installation of WRSR.

No DLC is required by this mod.

The plugin validates the WRSR functions it needs and is intentionally limited
to game version 1.1.1.9. It will decline to initialize on an unsupported build
instead of applying unverified hooks.

## Installation or update

1. Subscribe to the Helicopter Distribution Office Workshop item.
2. Copy:

   `Steam\steamapps\workshop\content\784150\3778940406\plugins\helicopter_distribution_office.dll`

   to:

   `Steam\steamapps\common\SovietRepublic\tesmioloader\build\plugins\`

3. Replace any older copy of `helicopter_distribution_office.dll` in that
   TesmioLoader plugins folder.
4. Launch WRSR through `tesmiolauncher.exe` and ensure the plugin is enabled.

The Workshop subscription installs the building files, but the DLL must be in
TesmioLoader's own plugins directory for the script to run.

## Using the HDO

1. Build a **Helicopter Distribution Office** from the Distribution Office
   construction menu.
2. Purchase suitable road cargo vehicles and cargo helicopters, or rehome an
   existing helicopter into the HDO through its vehicle window.
3. Add sources and destinations through the normal Distribution Office panel.
4. Configure cargo types and source/destination thresholds normally.
5. Use cargo heliports for helicopter collection and delivery points. They do
   not need road access for HDO selection.
6. Use the Soviet or Western button when an assignment should use an overseas
   helicopter endpoint.

## Recommended use

The HDO is intended for fast, long-range or difficult-to-access deliveries:
remote settlements, isolated industries, mountain facilities and other places
where conventional infrastructure would be awkward or excessive.

Helicopters are expensive and are not a high-volume replacement for rail or
ship logistics. If a route can sensibly carry large continuous volumes by train
or ship, that will generally remain the better option.

Destination pad capacity is not used as a fleet limit. Sending many helicopters
to a single-pad cargo heliport can create a costly aerial queue.

## Important limits

- Helicopters can use only air-reachable endpoints: the HDO, cargo heliports
  and the HDO's overseas targets.
- The mod does not let helicopters land at arbitrary road-only buildings.
- WRSR's normal cargo compatibility still applies. A heliport must be able to
  store or transfer the selected resource.
- Passenger helicopters are not a substitute for cargo helicopters.
- The building may remain visible in a save without the plugin, but its hybrid
  dispatch behaviour requires the DLL to be installed and enabled.
- Other TesmioLoader plugins that modify the same Distribution Office or
  helicopter routines may be incompatible. Check `tesmioloader.log` if a plugin
  is declined or an expected feature is unavailable.

## Troubleshooting

If the HDO building appears but its scripted behaviour does not work:

1. Confirm the DLL was copied into `tesmioloader\build\plugins`, not left only
   in the Workshop folder.
2. Confirm the launcher shows `helicopter_distribution_office.dll` as enabled.
3. Confirm the game is WRSR v1.1.1.9.
4. Search `tesmioloader.log` for `helido`. A successful load reports
   `v1.2.0 initialized`.
5. If another plugin hooks the same game function first, try changing plugin
   load order or disabling the conflicting plugin before reporting the issue.

When reporting a problem, include `tesmioloader.log`, `tesmioloader.reads.log`,
the enabled plugin list, and a short description of the HDO assignments and
resident vehicles involved.

## Repository layout and building

- `mod/` contains the Workshop/runtime files users need.
- `source/` contains the C++ plugin source, no-CRT support, build script,
  PE finalizer and reverse-engineered technical notes.
- `source/BUILD.md` contains reproducible build instructions.
- `CHANGELOG.md` contains the public release history.
- `STEAM_WORKSHOP_CHANGELOG.md` contains a ready-to-paste Workshop update note.

Repository:

https://github.com/Ultimate-Universe/WRSR-TesmioLoader-HelicopterDistributionOffice

TesmioLoader:

https://github.com/MaxLegend/TesmioLoader

## Licence and attribution

The plugin source code is distributed under the GNU General Public License
version 3. See `LICENSE`.

The building model and other game-derived asset files remain subject to the
rights of their original rights holders and are not relicensed by the GPL.

This project is not affiliated with 3DIVISION or Hooded Horse.
