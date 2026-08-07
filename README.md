# Helicopter Distribution Office

A TesmioLoader plugin and Workshop building for **Workers & Resources: Soviet Republic** that combines normal road distribution with helicopter cargo distribution in a single office.

## Features

- 12 road-vehicle parking bays.
- 2 built-in helicopter pads.
- Supports additional attached heliports through the game's native heliport-area system.
- Combined fleet cap of 20 vehicles.
- Uses the normal Distribution Office order, percentage and dispatch system.
- Road vehicles assigned to this office ignore cargo-heliport jobs.
- Cargo helicopters can service compatible cargo heliports.
- Soviet and Western overseas targets are available directly from the Distribution Office panel for helicopter import/export jobs.
- Appears with the normal Distribution Office construction options while retaining functional built-in helicopter pads.

## Requirements

- Workers & Resources: Soviet Republic v1.1.1.7.
- TesmioLoader API 3.
- Steam Workshop item: `3778940406`.

## Installation

Subscribe to the Workshop item so the building assets are installed normally.

Then copy:

`Steam\steamapps\workshop\content\784150\3778940406\plugins\helicopter_distribution_office.dll`

to:

`Steam\steamapps\common\SovietRepublic\tesmioloader\build\plugins\`

Launch the game through `tesmiolauncher.exe` and make sure `helicopter_distribution_office.dll` is enabled.

## Usage

Build a **Helicopter Distribution Office** and configure it like a normal Distribution Office.

Ordinary road assignments are handled by road cargo vehicles. Assignments involving cargo heliports are reserved for helicopters from this office. Additional heliports can be attached to expand helicopter parking.

The Soviet and Western buttons on the office panel add the game's overseas destinations as Distribution Office targets, allowing helicopters to import from or export across the border without using a road customs house.

Helicopter dispatch is not automatically limited by the number of pads at a destination. Avoid assigning a very large helicopter fleet to a single-pad cargo heliport unless you are happy with an expensive aerial queue.

## Source and Building

The plugin source is in `source/` and the ready-to-use Workshop files are in `mod/`.

Build instructions are in `source/BUILD.md`.

Repository:

https://github.com/Ultimate-Universe/WRSR-TesmioLoader-HelicopterDistributionOffice

## License

The plugin source code is distributed under the GNU General Public License version 3. See `LICENSE`.

The building model and other game-derived asset files remain subject to the rights of their original rights holders and are not relicensed by the GPL.

This project is not affiliated with 3DIVISION or Hooded Horse.
