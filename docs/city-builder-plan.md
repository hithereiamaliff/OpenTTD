# OpenTTD city-builder fork: feasibility and plan

Goal: add SimCity-style city-building depth to OpenTTD (traffic control,
zoning, city services and so on), adapted to OpenTTD's transport-company game,
and keep playing existing savegames.

Status (2026-11-04): feasibility confirmed. First feature (traffic lights) built
and tested. Nothing committed or published yet.

## Where things are

| What | Where |
|---|---|
| Fork checkout | `/root/work/openttd-city`, branch `city` (from `origin/release/16`, i.e. OpenTTD 16.0-RC1) |
| Build | `/root/work/openttd-city/build/openttd` (Linux, RelWithDebInfo) |
| Test kit | `/root/work/openttd-city-test` (official 14.1/15.3 builds, OpenGFX, test AI, scripts, saves, logs) |

## Ground rules

- **Old saves keep working.** The fork loads everything OpenTTD 16 loads: saves
  from 15.x, 14.x and much older. New features start empty in an old save
  (no traffic lights, no zones), and the player adds them as they go.
- **New state goes in new savegame chunks** (table chunks, so fields can be
  added later), never in a changed format of an existing chunk. Map bits are
  only used as caches rebuilt after loading. This keeps merges with future
  OpenTTD releases easy.
- **One-way compatibility, without risk.** Saves made by the fork don't open
  in regular OpenTTD (it rejects unknown chunks), so the fork saves as
  `.csav`. It loads `.sav` and `.csav`, never overwrites a `.sav`, and regular
  OpenTTD doesn't even list `.csav` files. Both games can share one folder.
- **Everything is a command**, so it works in multiplayer and AIs/game scripts
  can use it. Multiplayer needs everyone on the fork.
- **No SimCity assets or names.** Generic city-builder mechanics only; graphics
  are reused from the free base sets or drawn new.

## Done: M1 traffic lights

- Road toolbar button (hotkey `L`): click a junction to build lights,
  Ctrl+click or the remove toggle to take them away. Works on town roads and on
  the company's own roads. Costs about as much as a road depot.
- Lights are *actuated*: green stays on while traffic flows, and switches when
  vehicles wait on the other road (min 90, max 450 ticks of green; 30 ticks all
  red in between). Road vehicles and trams stop at red.
- Drawn with the base set's electric signal sprites, one post per approach, on
  the driving side.
- Land info shows "Road junction with traffic lights" and the owner.
- Lights disappear when the junction is removed; a bankrupt company's lights
  stay and become public.
- Script API: `ScriptRoad.HasTrafficLights`, `BuildTrafficLights`,
  `RemoveTrafficLights`, `IsTrafficLightGreen` (AI and game scripts).
- Savegame: chunk `TRFL`; map bit m8 bit 12 on plain road tiles is a cache.

Code: `src/traffic_light.{h,cpp}`, `src/traffic_light_cmd.h`,
`src/saveload/traffic_light_sl.cpp`, plus small hooks in `road_cmd.cpp`,
`roadveh_cmd.cpp`, `road_gui.cpp`, `landscape.cpp`, `economy.cpp`, `misc.cpp`,
`saveload/afterload.cpp`, the command lists and `lang/english.txt`.

### How it was tested

- OpenTTD's own test suite: 99/99 pass (unit tests and the AI/game script
  regression runs, which load old savegames).
- Saves from the official 15.3 (format 362) and 14.1 (334) releases, and
  OpenTTD's bundled 308 and 211 test saves: load, play, save, reload. A test AI
  then adds traffic lights to those old games.
- Test AI (`TLTest`) builds two crossing bus lines through a grid town and puts
  lights on all its junctions. Headless runs of up to 150,000 ticks (about
  5.5 game years) with 30 buses: no crash, no gridlock. The central junction
  switched 89 times in 40,000 ticks.
- Screenshots under a virtual screen: lights cycling, buses queueing, and
  building/removing through the toolbar (company money checked).

### What the tests taught us

In regular OpenTTD, road vehicles drive *through* each other at junctions.
An unlit junction therefore costs nothing, and lights can only add delay:
with 16 buses the effect was negligible, with 30 buses on a fully lit grid the
throughput dropped by about 28%. Traffic control only becomes worth building
once unlit junctions have a cost of their own, which is why M2 comes next.

## Roadmap

Sizes are rough: S = one session, M = a few, L = many, XL = a project in itself.

| # | Feature | What it means in OpenTTD | Size | Notes |
|---|---|---|---|---|
| M2 | Traffic realism | Vehicles give way to crossing traffic at unlit junctions, so busy crossings jam and lights pay off. Light settings window (timings, fixed-time mode, green-wave offsets), amber phase, pathfinder penalty for lights, congestion overlay that colours busy roads. | M–L | Touches road vehicle movement; must stay deadlock-free and fast. |
| M3 | Zoning | Paint residential / commercial / industrial / park zones around towns; town growth only builds matching buildings there. Demand bars (RCI) in the town window. | L | Deep change to town growth (`town_cmd.cpp`). Zones live in their own chunk. |
| M4 | City services and happiness | Player funds police, fire, health, education and parks as buildings with a coverage radius. Coverage drives town happiness, which drives growth and ratings. Monthly upkeep. | L | Builds on objects; needs new graphics or reused object sprites. |
| M5 | Town car traffic | Towns send private cars between homes and workplaces. Good public transport takes cars off the road. | XL | The most SimCity-like part; performance-sensitive on big maps. |
| M6 | City budget and ordinances | Per-town budget (taxes from population, spending on services and roads) and ordinances (transit subsidy, parking limits and so on). | L | Needs a design call: the player stays a transport company, or also acts as mayor. |
| M7 | Land value and pollution | Industries, stations and traffic pollute; parks and services raise land value; overlays show both. Affects which buildings appear. | M–L | Pairs well with M3 and M4. |
| M8 | Utilities | Power and water networks. | XL | Probably skip, or keep it light (arctic/tropic towns already need food/water). |

Recommended order: M2, then M3, M4 and M7 together (they share overlays and
town growth), then M6 and M5.

## Getting it onto the owner's computer (Windows)

- The owner plays on Windows. The code goes to a GitHub fork of OpenTTD, and
  `.github/workflows/city-windows.yml` builds a 64-bit Windows zip on every
  push to `city`: a workflow artifact, plus the rolling `city-latest`
  pre-release as a stable download link. Libraries come from vcpkg, cached in
  the Actions cache (no packages published). The exe isn't code-signed, so
  SmartScreen asks once ("More info", "Run anyway").
- Unzip anywhere and run `openttd.exe`. It uses the same `Documents\OpenTTD`
  folder as the normal game, so it sees the same saves, base graphics and
  downloaded NewGRFs; its own saves are `.csav` (see ground rules).
- Linux build exists on the VPS for testing.

Savegame locations (files end in `.sav`; autosaves are in `save/autosave`):

| OS | Folder |
|---|---|
| Windows | `Documents\OpenTTD\save` |
| macOS | `~/Documents/OpenTTD/save` |
| Linux | `~/.local/share/openttd/save` |

## Open questions for the owner

1. Can you share one of your saves, so it can be tested before you play it?
2. Which milestone next? Recommendation: M2.
3. A name for the fork.
