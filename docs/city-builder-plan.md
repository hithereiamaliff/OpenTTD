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

- Road toolbar button (hotkey `S`, as for rail signals): click a junction to build lights,
  Ctrl+click or the remove toggle to take them away. Works on town roads and on
  the company's own roads. Costs about as much as a road depot.
- Lights are *actuated*: green stays on while traffic flows, and switches when
  vehicles wait on the other road (min 90, max 450 ticks of green; 30 ticks all
  red in between). Road vehicles and trams stop at red.
- Drawn with the base set's electric signal sprites, one post per approach, on
  the driving side (own graphics since M2 part 3).
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

## Done: M2 part 1, giving way at junctions

- Setting "Road vehicles give way at junctions" (Settings, Vehicles, Routing;
  `vehicle.road_give_way`, on by default, saved in the savegame).
- A road vehicle only drives onto a junction when no vehicle that came in over
  the crossing road is on it. At an unlit junction, vehicles waiting on both
  roads take turns, longest waiting first (the vehicle's existing, saved
  blocked counter; ties by vehicle number).
- "Don't block the box": a vehicle stays out of a junction (lit or not) when the
  road it leaves by is backed up right behind it (from its cached route).
- Safety valves: a vehicle stuck on a junction for 32 tries stops holding up
  crossing traffic, and one that waited 400 tries goes anyway. No deadlock
  can last.
- Nothing new is saved per vehicle or junction. Code: `src/road_junction.{h,cpp}`,
  one hook in `RoadFindPathToDest`.

Results (test AI, grid town, two crossing bus lines, about 5.5 game years):

| Setup, 30 buses | Average profit | vs regular OpenTTD |
|---|---|---|
| Regular OpenTTD | 18,183 | — |
| Give-way, no lights | 13,215 | −27% |
| Give-way + lights on the 3 central junctions | 19,138 | +5% |
| Give-way + lights on all 13 junctions | 18,329 | +1% |

With 50 buses nothing deadlocks: the number of moving buses keeps recovering
(26 of 50 moving on average without lights, 34 with three lights). Lights
now pay off at busy junctions, and too many lights still cost a little.

## Done: M2 part 2, traffic light settings and the traffic map

- Clicking existing lights with the traffic light tool opens their window:
  **Smart** (reacts to traffic) or **Fixed time** (a set cycle), green per
  road, shortest green (smart), and an offset in the cycle (fixed) for green
  waves. A live line shows which road has green. Only the owner (or anyone,
  for public lights) can change them; it is a command, so it works in
  multiplayer and is saved (new TRFL fields `mode`, `green_x`, `green_y`,
  `min_green`, `offset`; older saves get the defaults).
- Fixed lights follow the global tick counter, so lights with the same cycle
  stay in step and offsets are exact, also after loading.
- Map window: new **Road traffic** view. Roads are coloured by how much traffic
  was held up there lately (jam, slow, some waiting, flowing, quiet), lit
  junctions show light blue. The data is sampled every 16 ticks from the road
  vehicles; it is display-only, not saved and never affects the game.
- Script API: `ScriptRoad.SetTrafficLightTimings`.
- Code: `src/traffic_light_gui.cpp`, `src/road_traffic.{h,cpp}`,
  `src/widgets/traffic_light_widget.h`, plus the map window.

Same test town, 30 buses, give-way on, about 5.5 years:

| Lights | Average profit |
|---|---|
| Smart, 3 central junctions | 19,138 |
| Fixed 4.1 s green, 3 central junctions | 16,927 |
| Smart, all 13 junctions | 18,329 |
| Fixed 4.1 s green, all 13, all in step | 25,667 |
| Fixed, all 13, simple green-wave offsets | 14,093 to 16,008 |

So the timings matter a lot, and good green waves need tuning to real travel
times, which is now something players can experiment with.

## Done: M2 part 3, traffic polish

- Own traffic light graphics: a post with red, amber and green lamps (posts
  facing away show their back, unless no other post of that road faces the
  viewer). The light that had green shows amber for the first 20 of the 30
  all-red ticks. New toolbar icons for the light tool and the overlay.
- Graphics live in OpenTTD's own extra graphics file: `media/baseset/openttd/`
  `traffic_lights.png` + `traffic_lights.nfo` as Action 5 type 0x1C
  (`SPR_TRAFFIC_LIGHTS_BASE`). With grfcodec installed the build regenerates
  `openttd.grf`; the regenerated file is committed, so other builds (the
  Windows CI) don't need grfcodec. Base sets don't know this block, so the game
  always uses ours. If upstream ever takes type 0x1C, renumber ours.
- Road traffic overlay in the main view: road toolbar toggle (hotkey O), roads
  tinted green/yellow/orange/red by recent traffic (company-colour overlay
  sprites recoloured per level). It switches off with the road toolbar.
- Routing around jams: the road pathfinder adds `pf.yapf.road_junction_queue_penalty`
  (2 tiles) per vehicle held up on or right in front of a junction, and
  `pf.yapf.road_traffic_light_penalty` (half a tile) for lights. Both are
  expert settings saved from 368 on. Without lights, routing around jams
  raised the test town's average profit by 30% (13,215 to 17,229) and kept
  more buses moving in the worst moment (17 instead of 11 of 30). A bigger
  light penalty made buses avoid the lit junctions and did worse, hence the
  small default.

## Done: M3 zoning

- Towns menu, **Zoning** (hotkey Shift+Z) opens a small toolbar: homes
  (residential), shops and offices (commercial), industry, parks, and clear.
  Drag over the map to zone an area, up to 128 by 128 tiles at a time. Zoning
  is free and works anywhere; it is a command, so it works in multiplayer and
  for scripts.
- While the toolbar is open, the main view tints zoned land: green for homes,
  light blue for shops and offices, yellow for industry, dark green for parks.
  The toolbar icons use the same colours.
- Town growth follows the zones: houses and flats only on residential land;
  businesses (offices, shops, hotels, theatres, cinemas, shopping centres, the
  warehouse) only on commercial land; public buildings (churches, stadiums,
  statues, fountains, parks) on either. Industrial and park land stays free of
  town buildings and of new town roads (no road, road stub, bridge end or
  tunnel exit there), so it can be kept for a factory, an airport or a
  station. A building on several tiles needs all of them zoned for it.
  Unzoned land works as in regular OpenTTD, and existing buildings and roads
  are left alone.
- Commercial land gets businesses anywhere in town, not only in the centre
  ring where OpenTTD normally puts offices. Without that, commercial zones at
  the edge of town stayed almost empty.
- Homes and businesses are told apart by building type. A NewGRF house counts
  as the original building it names as its stand-in (the one the game shows
  when the NewGRF is missing). Going by goods acceptance, the first try,
  counted flats as shops.
- Town window: "Homes for X, jobs for Y", and whether the town wants more
  homes or more shops and offices (about one job per two residents counts as
  balanced). This is advice; it doesn't change how the town grows. Typical
  towns full of office blocks ask for homes, villages ask for shops.
- Land info shows a tile's zone, and for a town building whether zoning counts
  it as a home, a business or a public building.
- Script API: `ScriptTown.SetZone`, `GetZone`, `GetBuildingKind`.
- Savegame: chunk `ZONE` (tile, zone), zoned tiles only.
- Code: `src/town_zoning.{h,cpp}`, `src/town_zoning_cmd.h`,
  `src/town_zoning_gui.cpp`, `src/widgets/town_zoning_widget.h`,
  `src/saveload/town_zoning_sl.cpp`, plus hooks in `town_cmd.cpp`,
  `town_gui.cpp`, `viewport.cpp`, `misc_gui.cpp` and `toolbar_gui.cpp`.
- Hotkeys: zoning is Shift+Z (Shift+F5 is already zoom in). The traffic light
  tool moved from L to S, because L in the road toolbar hid the landscaping
  hotkey.
- Known limits: in multiplayer any company can change any zone (fine for
  single player and friendly games; a setting or a cost could come later).
  Zones are kept per tile in a hash map, which is fine for zoning around towns
  but not meant for zoning a whole large map.

Results (test AI, a town of 1,106 people, four quarters of about 24 by 24
tiles zoned commercial, residential, park or left alone, fast town growth,
about 3 years). Counts are new tiles; a stadium covers 4.

| Quarter | Zoned: home / business / public tiles, new road tiles | No zoning: same |
|---|---|---|
| Commercial | 0 / 3 / 3, 1 road | 3 / 1 / 0, 1 road |
| Residential | 5 / 0 / 6, 10 roads | 9 / 1 / 1, 7 roads |
| Park | 0 / 0 / 0, 0 roads | 3 / 3 / 0, 6 roads |
| Left alone | 9 / 4 / 0, 13 roads | 5 / 7 / 0, 10 roads |

No building broke its zone, and the park quarter got no new road. The zoned
town reached 2,316 people against 3,094 without zoning, mostly because the
park quarter stayed empty.

## Done: M4 part 1, city services and happiness

- Towns menu, **City services** (hotkey Shift+C) opens a toolbar: police
  station, fire station, clinic, school and park. While a tool is picked, the
  main view tints the land that service already reaches, and a dotted square
  around the cursor shows what the new building would reach.
- Each service covers the houses within 10 tiles (parks 5), in a square.
  Building costs about £22,500 (clinic £27,000, park £3,750) at the default
  construction costs, and upkeep is a fifth of that a year, charged monthly
  as property maintenance; both follow inflation. Only the owner can remove them, towns never do.
- Once a month every town works out what share of its people each service
  reaches. Happiness is 40% without services and 100% when every service
  reaches everybody. Happy towns grow up to a third faster (the time between
  new houses shrinks by up to 33%). Towns without services work as in
  regular OpenTTD.
- Running services for a town raises the owner's rating there every month,
  12 points per kind of service, up to "Very good". A row of parks counts as
  one kind, so it buys no extra favour.
- Town window: "Happiness: 98%. City services reach:" and an icon with a
  percentage per service. Land info on a service shows its reach and upkeep;
  on any tile it lists the services that reach it.
- Own graphics: five small buildings (police with a blue chequered band and a
  roof lamp, fire station with a hose tower and red doors, white clinic with a
  red cross sign, school with a clock tower and a playground, park with
  trees, a pond and paths), flags in the owner's colours, and five tool icons.
  `media/baseset/openttd/make_city_sprites.py` draws all of the fork's
  sprites (renamed from `traffic_lights.*` to `city.*`).
- Script API: `ScriptTown.BuildCityService`, `GetCityService`,
  `GetCityServiceCoverage`, `GetHappiness`, `GetCityServiceRadius`,
  `GetCityServiceCost`.
- How it is stored: the buildings are objects with five object types
  reserved at the top of the object type range (63984 and up), which NewGRF
  objects never get. Old savegames store the full NewGRF object table, empty
  slots included, so the table keeps its size; only handing out those types
  is blocked. Each town's coverage (as of the last monthly update, because
  growth uses it until the next one) is saved in a new chunk `CSVC`.
- Code: `src/city_services.{h,cpp}`, `src/city_services_type.h`,
  `src/city_services_gui.cpp`, `src/widgets/city_services_widget.h`,
  `src/saveload/city_services_sl.cpp`, plus hooks in `object_cmd.cpp`,
  `newgrf_object.cpp`, `newgrf_commons.cpp`, `town_cmd.cpp`, `town_gui.cpp`,
  `viewport.cpp`, `misc_gui.cpp` and `toolbar_gui.cpp`.

Results (test AI `SvcTest`, the zoning test town of 1,106 people, one of each
service plus a second park within 3 tiles of the centre):

| | With services | Without |
|---|---|---|
| Happiness after a month | 97% | 40% |
| Coverage police/fire/health/school/park | 100/100/100/100/78% | 0 |
| Days between new houses (funded / not funded) | 40 / 103 | 58 / 152 |
| Rating of the owner | Very good | none |
| Houses after 5.5 years, funding all the time | 55 | 45 |
| People after 5.5 years, same | 2,901 | 3,254 |
| Upkeep per year | £20,388 | — |

The services town built 22% more houses but ended with fewer people: the
services took six tiles right at the centre, where towns would otherwise put
their biggest office blocks. Services reach 10 tiles, so they do as well a
few tiles out.

## Roadmap

Sizes are rough: S = one session, M = a few, L = many, XL = a project in itself.

| # | Feature | What it means in OpenTTD | Size | Notes |
|---|---|---|---|---|
| M2 | Traffic realism | Done: give way, light settings, traffic map and overlay, amber, routing around jams. | — | |
| M3 | Zoning | Done: zones, town growth follows them, homes/jobs line in the town window, zones in land info. | — | |
| M4 | City services and happiness | Part 1 done: police, fire, clinic, school and park with coverage, happiness, growth, ratings and upkeep. Later: bigger buildings (hospital, university), services that also make passengers. | L | |
| M5 | Town car traffic | Towns send private cars between homes and workplaces. Good public transport takes cars off the road. | XL | The most SimCity-like part; performance-sensitive on big maps. |
| M6 | City budget and ordinances | Per-town budget (taxes from population, spending on services and roads) and ordinances (transit subsidy, parking limits and so on). | L | Needs a design call: the player stays a transport company, or also acts as mayor. |
| M7 | Land value and pollution | Industries, stations and traffic pollute; parks and services raise land value; overlays show both. Affects which buildings appear. | M–L | Pairs well with M3 and M4. |
| M8 | Utilities | Power and water networks. | XL | Probably skip, or keep it light (arctic/tropic towns already need food/water). |

Recommended order: M7 next (pollution lowers happiness, parks and services
raise land value), then M6 and M5.

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
2. Which milestone next? Recommendation: M7 (land value and pollution),
   which ties into happiness and zoning.
3. A name for the fork.
