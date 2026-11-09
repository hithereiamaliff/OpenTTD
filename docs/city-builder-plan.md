# Wira Bandar PC (a fork of OpenTTD): feasibility and plan

Since 2026-11-08 this fork is called Wira Bandar PC: a fork of OpenTTD,
OpenTTD's code with changes, not a game made from scratch (see "Done: the
name Wira Bandar" below). The rest of this file was written as "the fork".

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

## Done: transit part 1, lines

The transit plan (City Bus Manager style play, Malaysian and Singaporean
funding models, roads and BRT, buses and depots) is in
`docs/transit-operator-research.md`. Its first part is lines.

- A line is the vehicles sharing one order list (a single vehicle is a line
  too). It can have a number like 101, T420 or CAT (up to 8 characters), a
  name and one of the 16 colours (automatic by default), shown as a badge.
- Vehicle lists grouped by "Lines (shared orders)" show each line's badge,
  name and riders last month. Clicking a line opens its window (Shift+click
  opens the vehicle list as before, Ctrl+click the orders). The timetable has
  a Line button too.
- Line window: vehicles and the time of one round trip (measured, or from
  the timetable), how often vehicles leave the first stop, the average wait
  there and how much longer that is than with even spacing (Singapore's
  "excess wait time"), a target frequency with the number of vehicles it
  needs, riders last month and this month, how full vehicles leave their
  stops (average and fullest), on-time share of timetabled departures, and
  profit. Buttons for number, name, colour, frequency, even spacing,
  vehicles, orders and timetable. Times show with tenths of a day.
- Even spacing: with a target set, vehicles wait at the first stop until the
  target time since the last departure has passed (they keep loading).
- Network map: the map window (F4) has a Lines view (its own button). Each
  of your lines is drawn from stop to stop in its colour (all companies'
  when spectating), stops are white squares, and lines sharing a stretch run
  side by side instead of on top of each other.
- Departure boards: the station window has a Departures button. The board
  lists the next 14 vehicles to leave, soonest first: line badge, next two
  stops ("to X, then Y"), vehicle and "now" or "in 3.4 days". Click a row
  to show the vehicle, Ctrl+click for its line. Times are worked out from
  where each vehicle is on its orders and the timetabled or estimated times
  of those orders; at the first stop of a line with even spacing, vehicles
  are spaced by the target as they will be held. "Go via" calls are left
  out. Refreshes every second.
- Steadier estimates: for orders without a timetable, OpenTTD keeps the time
  the last vehicle took, so one slow trip (breakdown, depot visit, jam)
  changed the estimate for the whole line. The estimate now moves a part of
  the way towards each new measurement (half with one or two vehicles, an
  eighth with eight or more), so it reflects about one round trip. The
  timetable window shows these as "not timetabled" times as before, and the
  vehicles a line needs and the departure boards use them. Timetabled times
  and autofill are unchanged.
- Statistics are counted when vehicles leave stops and when passengers get
  off, kept per month, and saved with the line in a new chunk `LINE` (by
  order list). Old saves get lines without numbers and fresh statistics.
- Script API (AI and game scripts): `ScriptOrder.SetLineNumber`,
  `GetLineNumber`, `SetLineName`, `SetLineTargetGap`,
  `SetLineHoldForSpacing`, `GetLineRiders`, `GetLineDepartures`,
  `GetLineAverageGap`, `GetLineAverageWait`, `GetLineVehiclesNeeded`,
  `GetLineLoad`.
- Code: `src/line.{h,cpp}`, `src/line_type.h`, `src/line_cmd.h`,
  `src/line_gui.cpp`, `src/widgets/line_widget.h`,
  `src/saveload/line_sl.cpp`, plus hooks in `vehicle.cpp` (leaving a stop,
  holding), `economy.cpp` (riders), `vehicle_gui.cpp`, `group_gui.cpp`,
  `timetable_gui.cpp`, `timetable_cmd.cpp` (estimates), `smallmap_gui.cpp`
  (Lines view) and `station_gui.cpp` (Departures button). The board is in
  `line_gui.cpp` too.

Results (test AI, two town bus lines of 12 buses sharing orders, no
lights, about 2 years; times in ticks, 74 per day):

| Setup | Average gap | Average wait | Even spacing would be |
|---|---|---|---|
| No target | 154-207 | 120-312 | 77-104 |
| Target 170, no holding | 140-202 | 146-264 | 70-101 |
| Target 170, even spacing | 171-194 | 86-107 | 85-97 |

With even spacing the average wait is almost exactly half the gap, so the
buses stop bunching. The line says it needs 11-17 buses for that target,
against the 12 it has.

Departure board accuracy (same save, 12 buses, about 2 years; a temporary
probe noted each bus's predicted departure 4-6 and 14-16 days ahead and
compared it with when it actually left; errors in days):

| Estimates | 5 days ahead: median, 90% | 15 days ahead: median, 90% | Leaves later than shown, on average |
|---|---|---|---|
| Last trip only (OpenTTD as it was) | 2.1, 5.1 | 2.8, 8.2 | 1.5-1.8 |
| Moving a quarter of the way | 1.6, 4.4 | 2.4, 6.4 | 0.9 |
| Moving an eighth of the way (what a 12-bus line gets now) | 1.5, 4.4 | 2.1, 5.9 | 0.1-0.5 |

The remaining error comes from bunching, breakdowns and depot visits, which
no estimate from past trips can foresee.

## Transit part 2, street life and traffic

### Done: town traffic (step 1)

Towns don't put real vehicles on the road (every attempt at that in
OpenTTD went badly: desyncs, gridlock, frame time). Instead every road tile
has a congestion level from 0 to 7 that real vehicles feel.

- The level comes from the homes, jobs and city services within 3 tiles
  (offices count a bit more than homes), the town's size (25% more traffic
  per doubling above 500 people), how central the road is (town zone), the
  road's capacity (one-way roads 1.6x, road types where towns build no
  houses 2.5x, junctions 0.75x or 0.85x with traffic lights) and the
  setting. The more of the town's passengers the player carries, the fewer
  cars: up to 60% fewer when all of them ride.
- It is kept in the map (road tiles, m8 bits 13-15), so it is saved and the
  same for every player. The tile loop moves it up to two steps towards the
  target every other visit (about once a week). Old saves start at 0 and
  catch up within a month.
- From level 2 on, road vehicles (trams too, where they share the road) go
  at most 70, 55, 42, 32, 22 and 14 km/h on the tile. The road pathfinder
  adds 0.1 to 1.8 tiles of cost per congested tile (setting
  `pf.yapf.road_congestion_penalty`, percent).
- Setting "Town traffic" (Environment, Towns): off, light, normal (default)
  or heavy.
- Shown in the town window ("Traffic: moderate, 6% of roads congested" and
  how many cars the player's services take off the road), land info ("level
  2 of 7, moves at 70 km/h"), the traffic map and the traffic overlay.
  Script API: `ScriptRoad.GetCongestion(tile)`.
- Code: `src/town_traffic.{h,cpp}`, hooks in `road_cmd.cpp` (tile loop),
  `roadveh_cmd.cpp` (top speed), `yapf_road.cpp`, `road_traffic.cpp`,
  `town_gui.cpp`, `misc_gui.cpp`.

Calibration (normal setting, after two months):

| Town | Road tiles at level 0-7 | Congested (4+) |
|---|---|---|
| Gredinghall, 10,100 people | 54 28 25 22 6 2 0 0 | 6% |
| Abernbury, 9,200 | 48 10 24 18 14 2 2 0 | 15% |
| Grintfield (15.3 save), 11,800 | 33 27 21 39 6 6 0 0 | 9% |
| Towns of 3,800-6,400 | mostly 0-2 | 0-1% |

In the lines demo, the two 12-bus lines through Gredinghall's centre took
about 12% longer per round trip with town traffic on (average gap 200-250
ticks against 180-200). On a 1024x1024 map with many towns and no vehicles,
the game loop took about 13% longer (0.06 ms per tick); every-other-visit
updates and skipping roads that can't congest halved the first version's
cost.

### Done: street life vehicles (step 2)

Cars, vans, small lorries and motorbikes drive the roads in view, as many as
the town traffic calls for. They are scenery: every player's game makes its
own, they are never saved, use no game randomness and never change the game,
so network games stay in sync. Only views at normal zoom up to 2x out show
them.

- Up to 1,500 at once, about 0.3 per tile on quiet town streets up to 3.4 on
  jammed ones (fewer on the outskirts and in the country). When a view opens
  they fill its roads at once; after that new ones mostly come in from the
  edges of the view and vanish once they leave it.
- They drive in lanes on the side the game drives on (left or right), go
  straight on more often than they turn and prefer busier roads, turn around
  at dead ends, bridges, tunnels, depots and bay stops, and pass through
  drive-through stops and level crossings. They keep their distance to
  street vehicles and real vehicles ahead, queue behind buses, stop at red
  lights and closed crossings, and drive no faster than the road type and
  the town traffic allow. Knots at junctions untangle after a moment.
- Smart traffic lights now also switch for town traffic: on busy roads
  (congestion 1 and up) someone always waits, on quiet town roads someone
  turns up once the other road had its longest green. Out of town only real
  vehicles count, as before.
- Sprites: 4 kinds in 8 directions, drawn by
  `media/baseset/openttd/street_life_sprites.py` (boxes in world units,
  projected like the game, bodies in company colours so each vehicle gets one
  of 20 colours, mostly white, grey and dark ones).
- Client setting "Street life" (Interface, Viewports): off, vehicles, or
  vehicles and people (people come in step 3).
- Code: `src/street_life.{h,cpp}`, drawn from `viewport.cpp` after the
  vehicles, reset in `misc.cpp`.

Tests: no street vehicle entered a junction against a red light in 489
junction entries; in Gredinghall at normal zoom about 115 vehicles cost
0.07-0.08 ms per tick and drawing went from about 0.5 to 1.2 ms per frame.

### Done: people on the pavements (step 3)

- People walk the town pavements (roads with paved sidewalks, street lights
  or trees, which towns lay out from the outer suburbs inwards): about 3.2
  per tile in the centre, 1.8 in the inner suburbs, 0.9 in the outer ones.
  They keep to one side, follow the pavement round corners, cross side
  streets at junctions, turn back where the pavement ends and now and then
  go into a building. Two walking steps, two skin tones, clothes in any of
  the 16 colours.
- Crowds wait at bus stops in view: one person per 8 passengers waiting at
  the station, shared over its bus stops, at most 8 per stop tile, standing
  in two rows at the kerb on the far side of the road (the shelters hide the
  near side).
- Part of the "Street life" setting ("vehicles and people", the default).
- Sprites 73-78 in `make_city_sprites.py`.

With vehicles and people in Gredinghall at normal zoom, drawing took about
1.4-1.6 ms per frame (0.5 ms without street life).

Not in this part, for later: pedestrians waiting at red lights and on
zebra crossings, people at train and tram stations, rush hours (part 12).

## Done: transit part 3, fares

Every company sets passenger fares per transport type (buses, trams,
trains, ships, aircraft) in a Fares window (Finances, "Fares"); a line can
have its own fare and price (line window, "Fare" and "Price").

- Schemes: classic (OpenTTD's payment: by distance, less for slow trips),
  distance (BAS.MY style: what 3 tiles cost to board plus 90% of the
  standard rate per tile, however long the trip takes), flat (what 20 tiles
  cost, for every journey), zones (what 16 tiles cost per zone: 1 within a
  town, 2 to another town, 1 more per 40 tiles) and free. The standard fare
  is OpenTTD's payment for a trip that takes no time.
- Price level 25-300%. Concessions: a fifth of riders (students, seniors,
  disabled) pay the full fare, half or nothing. Monthly pass for 10-30
  single fares: the cheaper, the more riders buy one ((40 - price) x 1.5%,
  so 20 fares means 30%); pass holders ride free and each pass holder makes
  40 trips, so passes are paid at the end of each month.
- A journey pays one fare, worked out at the end for the whole trip from
  where the passengers first boarded, however often they changed (transfer
  credits use the same fare). The defaults (classic, 100%, no concessions,
  no pass) pay exactly as OpenTTD does.
- Riders weigh what they pay: each station keeps what riders who boarded
  there paid against what the classic fare would have been, monthly. The
  passenger rating moves by 4 points (of 255) for every 3% below or above
  the classic fare, at most +60 and down to -150. The station window's
  ratings view shows it ("Riders from here paid 63% of the classic fare:
  passengers +19%").
- Saved in chunks FARE (companies) and FSTA (stations); line fares in LINE.
- Script API: `ScriptCompany.SetFareScheme`, `SetFarePrice`,
  `SetConcessionFare`, `SetMonthlyPass`, `GetFareScheme`, `GetFarePrice`,
  `GetConcessionFare`, `GetMonthlyPass`, `GetPassIncome`;
  `ScriptOrder.SetLineFareScheme`, `SetLineFarePrice`.
- Code: `src/fare.{h,cpp}`, `src/fare_type.h`, `src/fare_cmd.h`,
  `src/fare_gui.{h,cpp}`, `src/saveload/fare_sl.cpp`, hooks in
  `economy.cpp` (payment), `station_cmd.cpp` (rating), `line.cpp`,
  `line_gui.cpp`, `company_gui.cpp`, `station_gui.cpp`.

Results (lines demo, two 12-bus town lines, a year and a half, averages
over the last year; income per quarter):

| Bus fares | Riders a month | Passenger rating | Income |
|---|---|---|---|
| Classic 100% (OpenTTD as it was) | 436 | 70% | about 8,930 |
| Classic 50% | 491 | 94% | about 4,960 |
| Classic 125% | 362 | 58% | about 9,290 |
| Classic 150% | 271 | 44% | about 8,310 |
| Classic 200% | 87 | 17% | about 3,330 |
| Classic 300% | 56 | 11% | about 2,880 |
| Flat 100% | 394 | 64% | about 9,050 |
| Distance 100% | 416 | 67% | about 9,000 |
| Zones 100% | 456 | 76% | about 8,410 |
| Free | 491 | 94% | 0 |
| Pass for 20 fares, half fare concessions | 500 | 90% | about 8,050 (600 a month from passes) |

The first version weighed the price level of departing vehicles gently;
then the dearest price always paid best and a flat fare on short trips
charged several times the classic fare unpunished. Weighing what riders
actually paid against the classic fare fixed both.

Not in this part: fare money from towns (part 4: free lines, resident
passes and concessions paid by towns), separate fares per transfer
(without integrated ticketing), satisfaction (part 11).

## Transit part 4, town transit authorities

### Done: budgets, tenders and contracts (step 1)

- Towns from 1,500 people have a yearly transit budget: 12 bus-years per
  10,000 people (a bus-year is three times a town bus's running costs: the
  bus, its wear and the operator), plus last year's fares from their
  contracts. Shown in a Transit window (town window, "Transit").
- Now and then (a chance of one in four a month, at least six months
  apart) a town invites tenders for a line that serves it (at least half
  its stops in the town, 4 or more departures a month), lines losing money
  three times as often, if its budget allows: a contract (the town keeps
  the fares, like Singapore's Bus Contracting Model and BAS.MY) or, one
  time in three, a free line (riders ride free, like Penang's CAT and Bas
  Muafakat Johor). Any company's lines, AI ones too.
- Bids: a margin of 0-40% on the line's monthly cost (running costs plus
  value spread over its vehicles' lifetime). Two or three simulated rival
  operators bid 5-35% with quality 45-90; the town scores quality and price
  60:40, as Singapore did for Tampines. The company's quality comes from the
  line: regularity (40%), reliability (30%) and age of its vehicles (30%).
  No bid means no contract; the line simply runs on its own account.
- A contract runs 5 years. Contracted lines charge the town's fare (a
  distance fare at 100%; none on free lines) and the fares go to the town.
  Each month the town pays the fee for the departures made (at most the
  90% of the starting month's departures it asked for), plus or minus up
  to 5%: +2/-2 for regularity against the starting excess wait, +3/-3 for
  punctuality where there is a timetable, -2 for missed departures. The
  payment shows in the vehicles' profits.
- At the end of the term: a review ends contracts that cost more than four
  standard 20-tile fares per rider; contracts that met every target in 80%
  of their months are extended by 3 years; the rest go back to tender.
  Two months without departures end a contract; a company can end one
  early for three months' fees.
- News for tenders, results, extensions and endings. The line window shows
  a line's contract instead of its fare.
- Saved in chunks TTRA (towns) and TCON (contracts).
- Script API: `ScriptTown.GetTransitBudget`; `ScriptOrder.GetLineTenderTown`,
  `GetLineTenderCost`, `BidLineTender`, `GetLineContractFee`,
  `GetLineContractPayment`.
- Code: `src/transit_authority*.{h,cpp}`, `src/saveload/transit_authority_sl.cpp`,
  hooks in `fare.cpp`, `economy.cpp`, `line.cpp`, `line_gui.cpp`,
  `town_gui.cpp`.

Test (lines demo, test AI bidding 15%, three years): Gredinghall (10,100
people) had a budget of 27,232 a year. It tendered Line 1 in the first
month (cost 1,108 a month), the AI won at 1,274 a month and was paid 1,299
(+2% for regularity), less in months with missed departures (815). Six
months later Line 2 followed at 1,201 a month, affordable thanks to the
fares Line 1 brought the town. Company income moved from about 8,300 to
7,500 a quarter: on these profitable lines cost plus 15% pays almost what
the fares did, but steadily. Contracts survive saving and loading.

### Done: resident passes and concessions (step 2)

- Once a town has 1,500 people it decides, once: four in ten give their
  residents a free pass (like Penang's Pas Mutiara), half pay back
  concession fares. Game scripts can set both
  (`ScriptTown.SetTransitSchemes`).
- Resident pass: a third of riders boarding in the town ride free on every
  operator's lines there (not on contracted lines, whose fares go to the
  town anyway). The town pays each operator 30 standard 20-tile fares a
  month per pass, a pass being 40 trips: three quarters of what those riders
  would have paid, made up by the extra riders free travel brings (the
  fares they pay count towards the station ratings).
- Concessions: where a company gives concessions, the town pays back what
  concession riders on journeys from the town didn't pay.
- Both come from what is left of the town's yearly budget after contracts;
  when it runs out, they are paid in part until the new year (with a news
  message once a year).
- Shown in the town's Transit window; news when a town introduces a pass.
- Script API: `ScriptOrder.EndLineContract`.

Test: with Line 2's contract ended, Gredinghall (which chose a resident
pass) paid for 2 passes in the first month (280 at 20 fares; now 30).

Later, if wanted: shuttles to a place (clinic, school, factory zone,
station), fleet funding, renameable scheme names, an optional national
agency. On-demand zones belong with part 12, e-bus quotas need part 5.

## Done: transit part 5, modern buses

Eleven new built-in buses in temperate, sub-arctic and sub-tropic (not
toyland), with invented Malaysian-flavoured names. Prices and running costs
are at base costs, without inflation (2020 test game); the Foster MkII is
6,562 and 1,125 a year for 40 seats at 127 km/h.

| Bus | From | Seats | km/h | Doors (boarding a step) | Price | Running a year | Battery |
|---|---|---|---|---|---|---|---|
| Tanjung Minibus (van) | 1985 | 16 | 100 | 1 (3) | 2,871 | 421 | |
| Seri M9 Midibus | 1992 | 40 | 90 | 2 (5) | 5,742 | 796 | |
| Kenari City 12 (12 m) | 1995 | 80 | 90 | 2 (7) | 9,433 | 1,406 | |
| Merbok Twin Decker | 1998 | 120 | 80 | 2 (7) | 13,125 | 1,875 | |
| Kenari Bendi 18 (articulated) | 2000 | 150 | 80 | 3 (10) | 15,585 | 2,156 | |
| Kenari City 12e | 2012 | 80 | 90 | 2 (7) | 16,816 | 843 | 350 kWh |
| Tanjung e-Minibus | 2015 | 16 | 100 | 1 (3) | 5,167 | 257 | 110 kWh |
| Laju BRT 18e (articulated) | 2015 | 140 | 100 | 4 (16) | 29,531 | 1,406 | 500 kWh |
| Kenari Bendi 18e (articulated) | 2016 | 140 | 80 | 3 (10) | 27,890 | 1,312 | 500 kWh |
| Seri M9e Midibus | 2016 | 40 | 90 | 2 (5) | 10,253 | 487 | 230 kWh |
| Merbok Twin Decker E | 2018 | 115 | 80 | 2 (7) | 22,968 | 1,125 | 450 kWh |

- Electric buses cost about 1.8 times as much and about 0.6 times as much to
  run; over 15 years a 12 m bus costs about the same either way, before the
  time electric buses spend charging. Big buses cost more to run but less
  per seat, so they pay on busy lines and lose on quiet ones.
- Lengths: minibus 5, midibus 6, 12 m and double-deck 8 (a tile is 16),
  articulated 6 + 6. Articulated buses need drive-through stops (as all
  articulated road vehicles do). Boarding speed comes from the doors.
- Drawn in two company colours (body in the first, roof and window pillars
  in the second; with the default livery both are the company colour), air-
  con on diesel roofs, battery packs on electric ones; 8 directions, rendered
  from boxes like the street life (`street_life_sprites.py`, sprites 79-190
  of `city.png`). The purchase list shows them right after the original
  buses, with a doors and boarding line and the battery and today's range.

Batteries:

- A tile counts as about 2 km: a 12 m bus uses 2.2 kWh a tile, so a new
  battery lasts about 150-170 tiles. Air-con and heating add: sub-tropic
  +20% all year, temperate +10% June-August and +15% December-February,
  sub-arctic +25% November-March and +5% otherwise.
- Batteries fade 2% a year of age, down to 60%.
- Below 25% a bus heads for a depot not far out of its way (four times the
  service detour), unless it is loading. A flat battery makes it crawl at
  15 km/h to the nearest depot, with a news message.
- In a depot a bus charges and leaves at 95%; its status reads "Charging:
  72%". (Part 5 gave every depot 90 kWh a day per bus; part 6 replaced that
  with the depot's own two chargers of 60 kWh a day plus its charging bays.)
  The vehicle details show charge, size, health, tiles left and air-con or
  heating.
- Towns add up to 10 quality points to tenders for electric lines, and lines
  at least half electric get 8-year contracts instead of 5 (part 4's "longer
  e-bus terms"; e-bus quotas are left for later).

Saves and NewGRFs:

- The city buses are kept in the engine map under their own pseudo NewGRF ID
  (`\xFFCTY`, local ids 88-101), so a NewGRF vehicle set's vehicles 88+ are new
  engines as before and can't take over the city buses (vehicles 0-87 still
  replace the original ones, as in OpenTTD). New games put them after the
  256 original engines, which keep their ids; older saves get them appended
  after their last engine on load and started then (models already
  introduced are available at once).
- Battery charge is saved in chunk BATT, by vehicle. `RoadVehicleInfo` cost
  and running cost factors are 16 bits now (the big buses need more than 255).
- Script API: `ScriptEngine.GetBatteryCapacity`, `ScriptVehicle.GetBatteryCharge`,
  `GetBatteryHealth`, `ScriptOrder.GetLineContractEnd`.
- Code: `src/city_bus.{h,cpp}`, engine tables in `src/table/engines.h`,
  `src/saveload/city_bus_sl.cpp`; hooks in `engine.cpp`, `saveload/engine_sl.cpp`,
  `newgrf.cpp`, `newgrf_engine.cpp`, `articulated_vehicles.cpp`, `roadveh_cmd.cpp`,
  `roadveh_gui.cpp`, `vehicle_gui.cpp`, `build_vehicle_gui.cpp`, `transit_authority.cpp`.

Tests (test AI BusTest; save `city-bus-demo.csav`):

- 99/99 unit tests; vanilla 15.3 and 14.1, bundled and fork demo saves load,
  save and reload. In the vanilla saves (mid-1995) the Tanjung Minibus,
  Seri M9 and Kenari City 12 are on sale at once, as engines 256-258.
- A test NewGRF with road vehicles 0, 88 and 89: vehicle 0 takes over the
  MPS Regal as in OpenTTD, 88 and 89 become new engines, all city buses stay
  (dynamic engines on and off), also after saving and loading.
- 2020 game, one of each bus on a 16-tile town line: electric buses lost
  about 18% per 20 days, went to the depot below 25% (lowest 12%), charged in
  about 3-4 days and spent 4-8% of their time there; health 97% after 1.5
  years. Without a depot for 250 days they ran flat after 90-130 days,
  crawled at 15 km/h and recovered once the depot was back.
- Tenders: an all-electric line won an 8-year contract (2021-08 to 2029-07),
  a line with 6 of 13 buses electric a 5-year one.
- Autoreplace from the diesel to the electric models works, the articulated
  ones too (Kenari Bendi 18 to 18e, a different rear section); the new buses
  start with a full battery.

## Done: transit part 6, depots

### Depot yards (step 1)

- A road depot's yard is the facilities its owner builds next to it, or next
  to its other facilities (a facility between two yards goes to the nearer
  depot). Build them from the depot window's new **Yard** button; the yard
  window shows what the yard offers and the vehicles based there.

| Facility | Cost | Upkeep a year | What it does |
|---|---|---|---|
| Bus park | 2,000 | 200 | room for 8 vehicles |
| Solar bus park | 6,000 | 320 | room for 8; powers 6 electric buses based here: they run 15% cheaper |
| Multi-storey park | 20,000 | 1,600 | room for 24 vehicles |
| Charging bay | 6,000 | 800 | two chargers of 180 kWh a day, room for 2 |
| Fast charging bay | 16,000 | 2,000 | two chargers of 500 kWh a day, room for 2 |
| Substation | 12,000 | 1,200 | 3,000 kWh a day more for the chargers |
| Repair bay | 8,000 | 1,600 | keeps 20 vehicles in shape |
| Wash and fuel lane | 6,000 | 1,200 | bulk fuel for 50 vehicles: they run 8% cheaper (not electric ones) |
| Staff building | 8,000 | 1,200 | staff rooms for 40 vehicles (with a staff budget) |

  Costs at base prices; upkeep is paid monthly as property maintenance.
- A depot itself has room for 4 vehicles, two chargers of 60 kWh a day, a
  grid connection of 600 kWh a day, a workshop for 5 and staff rooms for 10.
- **Home depots**: a road vehicle is based at the depot it was bought in, or
  the depot it was last stopped in (a vehicle from an older game at the first
  depot it enters; autoreplace keeps the home). It goes home for service and
  to charge when home is at most 48 tiles away (and, to charge, when its
  battery gets it there with room to spare), so a far home means empty runs.
  The vehicle details show the home depot.
- What the yard means for the vehicles based there:
  - more vehicles than places: the rest park on the street, up to 15% higher
    running costs;
  - more vehicles than the workshops look after: up to 50% faster wear (more
    breakdowns; nothing without breakdowns);
  - fuel lanes and solar roofs lower running costs as above.
- **Charging**: once a day the electric buses standing in a depot get its
  chargers, emptiest battery first and fastest charger first; together they
  draw at most the grid connection, shared out when they would draw more.
  The others wait ("Waiting for a charger"). Charging buses stand at the
  charging bays, other buses in the depot on the bus parks, all drawn.
- Saved: the home depot with the battery (chunk BATT, all road vehicles with
  either); yards are worked out from the map and the vehicles again.
- Object types: the facilities use reserved object types after the city
  services (OBJECT_DEPOT_FACILITY_FIRST), like those.

### Chargers at stops (step 2)

- A road toolbar tool (lightning icon, hotkey G; Ctrl+Click removes) puts a
  charger over one of your bus stops: 10,000, upkeep 1,000 a year. Electric
  buses standing there get 200 kWh a day, tick by tick, and one below 40%
  waits under it until it gets there. Drawn as a gantry with a pantograph
  over each lane; saved in chunk STCH; removed with the stop.

### Staff budgets (step 3)

- A company setting, off by default ("Staff budget for road vehicles"):
  lean pay makes road vehicles 6% cheaper to run but they wear 25% faster;
  generous pay costs 6% more and they wear 15% slower. With a staff budget
  depots also need staff rooms (10 in the depot, 40 per staff building);
  vehicles based at a depot without room for their staff wear up to 25%
  faster. Shown in the yard window when on.

Script API: `ScriptRoad.BuildDepotFacility`, `GetDepotFacility`,
`GetDepotYardPlaces`, `GetDepotYardVehicles`, `BuildStopCharger`,
`RemoveStopCharger`, `HasStopCharger` (with `DF_*` facility values);
`ScriptVehicle.GetHomeDepot`; `ScriptCompany.SetStaffBudget`, `GetStaffBudget`.

Code: `src/depot_yard*.{h,cpp}`, `src/stop_charger*.{h,cpp}`, chunks in
`src/saveload/city_bus_sl.cpp`; hooks in `object_cmd.cpp`, `newgrf_object.cpp`,
`road_cmd.cpp`, `depot.cpp`, `station_cmd.cpp`, `vehicle.cpp`, `vehicle_cmd.cpp`,
`autoreplace_cmd.cpp`, `roadveh_cmd.cpp`, `depot_gui.cpp`, `road_gui.cpp`,
`roadveh_gui.cpp`, `vehicle_gui.cpp`, `misc_gui.cpp`, company settings.

Tests (test AI BusTest settings `yard`, `park`, `stopcharge`, `staff`; saves
`city-yard-demo.csav`, `city-stopcharger-demo.csav`):

- 99/99 unit tests; the vanilla, bundled and fork demo saves load, save and
  reload.
- A full yard next to the test depot: 48 places for 13 buses; electric buses
  ran at 85% of their standard running costs (solar), diesel ones at 92%
  (fuel lane); all based at the depot.
- Six electric buses with only the depot's own chargers were in the depot in
  5-16% of samples; with a fast charging bay and a substation, in none
  (charged within a day).
- Chargers over both end stops, no yard: the electric buses stayed between
  50% and full (81-94% on average) and stopped going to the depot to charge.
- 13 buses at a bare depot: running costs 110% without a staff budget (the
  street parking), 104% with lean pay, 116% with generous pay.
- Parked buses show on the bus parks and under the solar roof; the charger
  gantries show over the stops after saving and loading.

## Done: transit part 7, bus priority

Town traffic slows road vehicles on congested roads (part 2). Bus priority
lets buses past it. A new road toolbar button (bus over a red lane, hotkey
Y) opens the Bus Priority toolbar; Ctrl removes with every tool there.

- **Bus lanes, all day** (drag along a straight road, 160 a tile): buses
  ignore congestion on the tile; the other traffic (road vehicles and the
  street-life cars), a lane short, is one congestion level worse. Drawn as
  red kerbside lanes with a white line.
- **Bus lanes, peak hours** (120 a tile): buses get two congestion levels
  less; the other traffic keeps its lanes. Dashed red lanes. Without a city
  clock (part 12) this averages the day: buses gain at the busy times, cars
  lose nothing.
- Lanes go on roads and drive-through stops (own, town or nobody's roads),
  not on bus-only roads; they stay when a stop is built on them or removed,
  and go when the road goes. The pathfinder takes them into account, so
  buses prefer routes along bus lanes.
- **Give-way markings at a stop** (400): yellow zigzags; on the roads next
  to the stop buses ignore congestion, pulling out ahead of the queue. They
  go with the stop.
- **Bus signals at traffic lights** (1,000; also a toggle in the traffic
  light window): buses get green 10 ticks before the rest of the traffic,
  at the end of the all-red time. Smart lights also cut the other road's
  green short for a waiting bus (after half the shortest green) and keep
  the green for a bus up to two tiles before the junction (up to the
  longest green), like transit signal priority. Fixed lights keep their
  cycle, so green waves stay; only the head start applies there. A "B"
  lamp on the posts lights up when buses may go.
- **Bus-only roads**: a third built-in road type ("Bus-only road", label
  CBUS) in the road construction menu; build it, or convert your own road
  to it (town roads can't be converted to a road type towns don't build
  houses along, as in vanilla). Only buses may use it (buses' compatible
  road types include it; lorries route around), town traffic and
  street-life cars stay off it, towns don't build it or houses along it.
  Drawn as road with a red carriageway. NewGRF road types that were at its
  index in older games are moved by the road type label map on loading.
- Saved: lanes in chunk BLAN, give-way stops in GWAY, bus signals as a new
  field of the traffic lights (TRFL).
- Script API: `ScriptRoad.BuildBusLane`, `RemoveBusLane`, `GetBusLane`
  (`BL_*`), `BuildGiveWayStop`, `HasGiveWayStop`,
  `SetTrafficLightBusPriority`, `HasTrafficLightBusPriority`,
  `ROADTYPE_BUS`.
- Code: `src/bus_priority*.{h,cpp}`, `src/saveload/bus_priority_sl.cpp`; hooks
  in `town_traffic.cpp` (congestion per vehicle), `yapf_road.cpp`,
  `roadveh_cmd.cpp`, `road_junction.cpp`, `traffic_light*.cpp`, `road_cmd.cpp`,
  `station_cmd.cpp`, `landscape.cpp` (DoClearSquare), `street_life.cpp`,
  `table/roadtypes.h`, `road_gui.cpp`.

Tests (test AI BusTest setting `prio`: one 16-tile line through a town
centre with 13 buses, the same map and seed each time, 55,000 ticks, 17
monthly samples; saves `city-prio1/2/3/6-demo.csav`,
`city-busroad-demo.csav`):

- 99/99 unit tests; the vanilla, bundled and fork demo saves load, save and
  reload, the new ones with their lanes, markings and bus signals.
- Congestion along the line (levels 0-7): 2 1 3 3 5 4 4 4 5 4 4 3 4 2 1.

| Setup | Departures a month | Range |
|---|---|---|
| Nothing | 13.2 | 12-16 |
| Bus lanes all day | 17.8 (+35%) | 12-23 |
| Bus lanes peak hours | 16.5 (+25%) | 12-21 |
| Give-way at both end stops | 12.5 (no gain: the end stops are on quiet roads) | 10-16 |
| 4 smart traffic lights | 10.1 | 3-14 |
| 4 smart traffic lights with bus signals | 13.4 (+33% on the lights) | 11-18 |

- The first bus signals (head start, and switching for a waiting bus only
  after the shortest green) gave 10.4, within the noise; cutting the green
  short and keeping it for a bus about to cross made the difference.
- Bus-only road: a bus bought in a depot next to it drove along it to its
  stop; a mail lorry ordered to a lorry stop on it stayed in its depot (the
  road in front is bus-only). Converting town road to it is refused.
- A lane stays when its stop is removed and when a stop is built on it;
  give-way markings go with their stop.
- Lanes, peak lanes, give-way zigzags, the bus road and the "B" lamps
  show in screenshots after saving and loading.

## Done: transit part 8, busways and BRT

Part 7 let buses past town traffic. Part 8 takes them out of it: busways
apart from the traffic, an elevated busway with stations on the viaduct
(Sunway style), BRT stations, and a BRT score for every bus line. Two more
buttons on the Bus Priority toolbar (Ctrl removes with both).

- **Kerbed busway** (button 5, drag along a road, 400 a tile): a red
  busway in the middle of the road between kerbs, on any road the company may
  paint, town roads too (town roads can't become bus-only roads). Buses ignore
  congestion, as on all-day bus lanes; the other traffic, a lane short, is
  one congestion level worse. Unlike painted lanes it counts as a busway for
  the BRT score. Stored with the bus lanes (a third kind of lane).
- **Concrete viaduct**: a new bridge type ("Viaduct, Concrete", from 1965,
  320 km/h, any length up to the "maximum bridge length" setting), for roads
  and railways; it is the concrete bridge in grey. Built with the bus-only road
  it is an elevated busway. Towns and NewGRF bridge sets keep to the 13
  original bridge types.
- **Stations on the viaduct**: OpenTTD can't put a station on a bridge, so a
  drive-through stop between the ends of two concrete road viaducts (flat
  ground, same height, viaducts leading away from it) is raised to the
  viaducts' height, and so is a run of up to four such stops. The viaduct
  ends next to it are drawn flat, as part of the viaduct, and buses stay at
  the viaduct's height: up the first viaduct, through the station and on
  without touching the ground. Build: viaduct, one empty tile, viaduct; then a
  drive-through stop on the tile. Removing a viaduct puts the stop back on the
  ground. Nothing is saved for this: it follows from the map.
- **BRT stations** (button 6, click a drive-through bus stop, 4,000, upkeep
  400 a year): fare gates and level platforms, drawn as platforms with a glass
  wall, a roof with a company colour edge and gates, in place of the stop's
  shelters. Riders pay before they board and use every door: buses load and
  unload twice as fast, the low-floor city buses (part 5) three times as fast.
  They go with the stop.
- **BRT score** of every bus line, worked out each month from what its buses
  did, after the ITDP BRT Standard's basics; shown in the line window with
  the parts:

| Part | Points | From |
|---|---|---|
| Dedicated lanes | 25 | share of the distance on busways (in full), all-day bus lanes (60%), peak bus lanes (30%) |
| Apart from the traffic | 25 | share on busways: bus-only roads, kerbed busways, bus-only viaducts and tunnels |
| Pay before boarding | 15 | share of stops made at BRT stations |
| Level boarding | 15 | share of stops made at BRT stations by low-floor buses |
| Priority at junctions | 20 | share of junctions and level crossings crossed that have bus signals (plain traffic lights count a quarter); full without any |
| Shared sections | up to -30 | a point off for each percent of the distance in mixed traffic over 10% (Johor's Iskandar BRT, 25% shared, was shelved) |

  A line is BRT when at least half its distance is on busways or all-day bus
  lanes and it scores 40 or more: basic BRT, bronze from 55, silver from 70,
  gold from 85.
- **What the grade does**: passengers rate the stations a BRT line stops at
  better (basic +4%, bronze +7%, silver +10%, gold +14%; the best line
  counts), and towns add quality points in tenders for BRT lines (2, 4, 7,
  10).
- Saved: BRT stations in chunk BRTS; kerbed busways in BLAN; the line
  counters in LINE; each station's grade in FSTA.
- Script API: `ScriptRoad.BL_BUSWAY`, `BuildBRTStation`, `RemoveBRTStation`,
  `HasBRTStation`, `IsElevatedStation`; `ScriptOrder.GetLineBRTScore`,
  `GetLineBRTGrade`, `GetLineBuswayShare`; the viaduct is bridge type 13.
- Code: `src/brt.{h,cpp}`; hooks in `bus_priority*.cpp`,
  `tunnelbridge_cmd.cpp` (flat viaduct ends, the span drawing),
  `station_cmd.cpp` (raised stops, BRT stations, ratings), `roadveh_cmd.cpp`
  (counting), `economy.cpp` (boarding), `line*.cpp`, `transit_authority.cpp`,
  `table/bridge_land.h`, `saveload/bus_priority_sl.cpp`, `line_sl.cpp`,
  `fare_sl.cpp`.

Tests (test AI BusTest settings `prio` 8-12; the town line is part 7's: one
16-tile line through a town centre with 13 buses, the same map and seed,
55,000 ticks, 17 monthly samples; saves `city-busway-demo.csav`,
`city-brt-demo.csav`, `city-elevated-demo.csav`):

- 99/99 unit tests; all 27 test saves (vanilla 14.1 and 15.3, bundled, the
  fork's demos and the new ones) load, save and reload; the new chunk BRTS,
  the line counters and the stations' grade are in the saves.

| Setup | Departures a month | Range | Riders a month | BRT score |
|---|---|---|---|---|
| Nothing | 13.2 (as in part 7) | 12-16 | 258 | 0 |
| Kerbed busway | 17.9 (like all-day lanes, 17.8) | 10-22 | 241 | 37: not BRT (busway 85%, no BRT stations, plain junctions) |
| BRT stations at both stops | 14.6 (+11%) | 9-21 | 277 | 0: all in mixed traffic (30 off) |
| Busway, BRT stations and 4 traffic lights with bus signals | 17.5 | 12-21 | 303 (+17%) | 89 on average: gold in 15 of 16 months, silver once |

- Passenger ratings at the two stops: 94-95% on average without BRT, 99%
  with the gold line (the cap is 100%).
- Bus signals add the lights' delay back (part 7: lights alone 10.1), so the
  full BRT line runs about as often as the busway alone, but with faster
  boarding and better ratings it carries the most riders.
- Viaduct line (prio 11): depot, bus-only road, stop A on the ground, 5
  tiles of viaduct, stop B raised between two viaducts, 5 tiles of viaduct,
  stop C on the ground; two Laju BRT 18e, two Kenari City 12 and a Seri M9
  run A, B, C, B. B is raised and A isn't; the buses pass through B at the
  viaduct's height, articulated ones too; the line scores 90-92 (gold, 95%
  busway; the points it loses are for stop C, which is no BRT station, and
  the road to the depot).
- Edge cases (prio 12): a bridge across a raised stop is refused; removing a
  viaduct puts the stop back on the ground and building it again raises it;
  a wooden bridge in its place doesn't.
- Screenshots: the viaduct with the raised BRT station and flat viaduct ends,
  the ground BRT station with the ramp up, the busway between kerbs with
  street-life cars kept off it (they still cross at junctions), the toolbar,
  and the line window ("BRT score last month: 90 of 100, BRT gold (2 off for
  shared sections)").

## Done: transit part 9, expressways, tolls and road pricing

- **Expressway** (road construction menu: "Expressway construction"): a
  fourth built-in road type (label CEXP). No houses along it and towns don't
  build it; no level crossings (railways cross it on bridges or in
  tunnels); three times the building cost and twice the upkeep of road; no
  speed limit. Road vehicles may drive on it. It takes 2.5 times the town
  traffic of a town road (part 2's capacity rule for road types without
  houses). Drawn with two lanes each way, edge lines and a barrier in the
  middle on straight, flat stretches. Older games that had a NewGRF road type
  at its index get it moved by the road type label map, as in part 7.
- **Toll plazas** (road toolbar: new **Tolls** button, hotkey K, opens the
  Tolls toolbar; Ctrl removes): on a straight stretch of your own
  expressway, 12,000, upkeep 1,000 a year. Everyone slows to 20 km/h at the
  booths. A **free-flow gantry** (8,000 to upgrade, upkeep 400 a year)
  removes the slowdown and gives 10% more trips.
  - **Town traffic pays**: each month the expressway network around a plaza
    (your expressway tiles joined to it, through bridges and tunnels) is
    followed to the roads that join it; each such road counts for the
    closest town within 32 tiles. Between each pair of linked towns there are
    population x population / 5,000,000 x 60 / (distance + 20) trips a month
    (two towns of 10,000 people 40 tiles apart: 20), each standing for many
    cars. A trip pays the standard toll (50 at base prices) per 40 tiles of
    distance between the towns (closed system, the default) or once (open
    system), times 1.1 for the mix of cars, motorbikes and lorries. A network
    with several plazas is paid once; the figures are shared out over them.
  - **Other companies' road vehicles pay** at each plaza they pass: the
    standard toll times 0.75 for buses, 1 for mail vans, 2 for lorries
    (Malaysian toll classes), booked as their running costs and your income.
  - **Toll level** 50-150% (the regulator's cap) and **scheme** (per km or
    flat) in the Tolls window (third button of the Tolls toolbar), which
    also lists the plazas. Higher tolls put drivers off: 150% makes a
    quarter fewer trips, 50% a quarter more.
  - **Concessions**: 30 years from building; then extended by 20 years in
    exchange for an 18% toll cut at that plaza (PLUS, 2011), up to 54%.
  - **Toll freezes**: each year a company with toll plazas has a 1 in 8
    chance that the government freezes its tolls for five years: at most
    90% of the standard toll meanwhile, and the government pays half of the
    income that costs (news message).
- **Congestion charges**: a town with one charges road vehicles except
  buses (and passenger trams) each time they drive into its centre and inner
  suburbs: 40 at base prices, 80 for lorries. Town traffic there drops by
  30% (London's charge cut traffic in the zone by about that). The town's
  own drivers pay too (80 per thousand people a month, less the share of
  car trips transit takes), and the money goes to the town's transit budget
  the year after, like the fares of its contracts: more tenders and
  contracts for bus lines. Towns of 15,000+ people whose centre is at
  congestion level 3 on average bring one in on their own (news message); a
  company can campaign for one from the town's Transit window ("Propose
  congestion charge", 5,000): the town agrees from 5,000 people when its
  centre is at level 2 on average and it rates the company Good or better.
  The Transit window shows the charge, the land info the charging zone.
- Saved: toll plazas in chunk TOLL, the companies' tolls in TOLC, the
  towns' charges with their transit (TTRA). Old games start without them.
- Script API: `ScriptRoad.ROADTYPE_EXPRESSWAY`, `BuildTollPlaza`,
  `RemoveTollPlaza`, `UpgradeTollPlaza`, `HasTollPlaza`,
  `IsFreeFlowTollPlaza`, `GetTollPlazaIncome`, `GetTollPlazaTrips`,
  `GetTollPlazaVehicles`, `GetTollPlazaTowns`; `ScriptCompany.SetTollLevel`,
  `GetTollLevel`, `SetTollScheme`, `GetTollScheme`, `GetTollIncome`;
  `ScriptTown.HasCongestionCharge`, `ProposeCongestionCharge`,
  `GetCongestionChargeIncome`, `GetCongestionChargeVehicles`,
  `GetCentreCongestion`.
- Code: `src/road_pricing*.{h,cpp}`, `src/saveload/road_pricing_sl.cpp`;
  hooks in `table/roadtypes.h`, `road_cmd.cpp` (drawing), `roadveh_cmd.cpp`
  (speed at booths, paying on entering a tile, expressway compatibility),
  `town_traffic.cpp`, `transit_authority*.cpp`, `landscape.cpp`,
  `road_gui.cpp`, `misc_gui.cpp`.

Tests (test AI TollTest: role 1 links two towns with an expressway and a
toll plaza, role 2 runs two buses and a mail van over it, role 3 runs mail
vans through the biggest town's centre and proposes a congestion charge;
saves `city-tolls-demo.csav`, `city-freeflow-demo.csav`,
`city-tolls-long-demo.csav`, `city-charge-demo.csav`):

- 99/99 unit tests; all 31 test saves load, save and reload; the new chunks
  are in the saves. Part 7's bus line without any of this runs exactly as
  before, month by month.
- An 11-tile expressway between Grubourne (4,500 people) and Brondinghattan
  Ridge (7,100), 28 tiles apart, with a plaza in the middle: 2 towns
  linked, 8-9 trips and 4-7 of the other company's vehicles a month, about
  420-460 a month at 100% per km. Flat at 150%: 6-7 trips (a quarter fewer),
  670-720 a month; free-flow from month 6: 7 trips and more vehicles
  through. A toll plaza on a town road is refused.
- 31 years with the same two companies: the towns grew to 27,000 and
  40,000 people, 270-280 trips and about 6,000-7,500 a month. A freeze from
  2045 to 2050 took about 10% off each trip (half of it paid back); in
  January 2050 the concession ran out and was extended to December 2069
  for an 18% cut (income down 18.6% the month after).
- Congestion charge in Kinningbury (10,600 people): its centre averaged
  congestion 2.4; the company's proposal after six months was accepted; the
  centre fell to 1.5-1.8 at once and stayed there. The charge took 730-860
  a month (3-6 company vans paid each month, the rest from the town's
  drivers), which shows up in the town's free transit budget the year after.
- Screenshots: the expressway lanes and barrier, the toll plaza and the
  free-flow gantry, the Tolls toolbar and window ("concession to December
  2069, 18% cut"), the Transit window with the charge.

## Done: transit part 10, interchanges and transport hubs

A new button in the station window (bottom row, after Departures):
**Interchange**, and once that is built, **Transport hub**.

- **Bus interchange**: a station with at least six bus stops (bays or
  drive-through, any mix, joined stops further away count too) gets a roof
  over its bus stops (BRT stations keep their own). 2,000 per bus stop at
  base prices, upkeep 200 per bus stop a year. Passengers rate it 8% better,
  and a crowd waiting there counts half for the rating. It closes, keeping
  the upgrade, while the station has fewer than six bus stops.
- **Integrated transport hub**: a bus interchange with a railway station in
  the same station becomes air-conditioned (the roof gets glass and
  air-conditioning units): 30,000, upkeep 4,000 a year more. Passengers rate
  it 16% better instead of 8%, its catchment is 2 tiles wider (people walk
  further to it), and its town's transit counts a quarter more towards
  taking cars off its roads, because changing between bus and train is easy
  (part 2's car reduction).
- **Towns pay half** of either upgrade from their transit budget when they
  have it to spare (Singapore's LTA builds the interchanges; the operators
  run them).
- The station window's ratings view shows the interchange or hub, the land
  info says so on its tiles.
- Saved: each station's upgrade in chunk FSTA, each town's hubs in TTRA.
- Script API: `ScriptStation.BuildInterchange`, `BuildTransportHub`,
  `GetInterchangeLevel`, `IsTransportHub`.
- Code: `src/interchange*.{h,cpp}`; hooks in `station_cmd.cpp` (rating,
  roofs), `station.cpp` (catchment), `town_traffic.cpp`, `station_gui.cpp`,
  `misc_gui.cpp`.

Tests (test AI HubTest: six drive-through stops around Kinningbury's centre
joined into one station, a railway platform of one tile in it (a house
pulled down), six buses to a stop on the far side of town; interchange after
four months, hub after eight; save `city-hub-demo.csav`):

- 99/99 unit tests; all 32 test saves load, save and reload; part 7's bus
  line runs exactly as before.
- The town paid half of both upgrades: the company paid 4,511 for the
  interchange and 11,271 for the hub. The catchment went from 4 to 6 tiles.
- Passenger ratings against the plain stop at the far end of the line:
  76-80% both before; 87% against 80% as an interchange; 91-96% against
  74-80% as a hub. Without any upgrade the same station rated 93% in 2021,
  as an interchange 98-100%.
- The town centre's congestion was 1.2-1.4 after the hub, 1.5-1.6 with the
  interchange alone.
- An interchange of a single stop, a hub before the interchange, and a hub
  without a railway station are refused.
- Screenshots: the hub roofs over the bus stops next to the platform, the
  station window with its Transport hub button and ratings line.

## Done: transit part 11, passenger satisfaction

Each month every passenger line (any transport) gets a satisfaction score
from 0 to 100% out of six parts, each 0 to 100:

| Part | Weight | 100 when | Falling to |
|---|---|---|---|
| Waiting | 25% | the average wait at its first stop is at most a tenth of a round trip (two days at least) | a quarter less for each such stretch beyond |
| Crowding | 20% | vehicles leave stops at most 60% full on average | two points less per percent above; 10 less when one left full |
| Punctuality | 20% | departures on time (with a timetable), or evenly spaced (without) | 30 less for each extra wait as long as the wait with even spacing |
| Fares | 15% | free, or half the standard fare | 75 at the standard fare, a point less per 2% dearer |
| Vehicle condition | 10% | reliable vehicles | their average reliability |
| Comfort | 10% | air-conditioned modern city buses (85), quieter electric ones (95) | older buses 55 (35 in the tropics), other vehicles 70; 2 less a year over five years old |

- **Stations**: the average of the passenger lines that stop there;
  passengers rate a station up to 5% better (at 100%) or 8% worse (at 0%),
  neutral at 60%.
- **Towns**: the average of their stations. Happiness goes up or down by up
  to 10 points (neutral at 50%), and each company gains or loses standing
  with the town every month from its own stations there (up to "very good").
- Shown in the line window (score and parts), the station window's ratings
  view and the town window's happiness line.
- Saved: lines' satisfaction in LINE, stations' in FSTA, towns' in TTRA.
- Script API: `ScriptOrder.GetLineSatisfaction`, `GetLineSatisfactionPart`;
  `ScriptStation.GetSatisfaction`; `ScriptTown.GetTransitSatisfaction`.
- Code: `src/satisfaction.{h,cpp}`; hooks in `line.cpp` (monthly),
  `station_cmd.cpp` (rating), `city_services.cpp` (happiness),
  `line_gui.cpp`, `station_gui.cpp`, `town_gui.cpp`.

Tests (test AI BusTest with new settings `fare`, `maxbuses`, `oldonly`;
part 7's 16-tile line through Kinningbury, 55,000 ticks, 17 monthly samples;
save `city-satisfaction-demo.csav`):

| Setup | Satisfaction | Weakest part | Station rating |
|---|---|---|---|
| 13 modern buses | 78% | punctuality 38 (the buses bunch) | 96% |
| The same at 200% fares | 71% | fares 25 | 62% (with part 3's fare effect) |
| Only 4 buses | 63% | waiting 32 | 79% |
| 8 old buses (Foster) | 58% | comfort 55, punctuality 8 | 76% |

- 99/99 unit tests; all 33 test saves load, save and reload.
- Kinningbury's happiness went from 40% to 45% with its riders at 76%.
- Screenshots: the line window ("Passengers' satisfaction last month: 76%",
  waiting 95, crowding 100, punctuality 19, fares 75, condition 87, comfort
  85), the station's ratings view, the town window.

## Done: transit part 12, rush hours and on-demand lines

A **city clock** (setting "City clock with rush hours" under Environment,
Towns; off by default, stored in the save, can be changed during a game)
runs a city day per game year or per game month. The time shows next to
the date in the status bar, orange in the rush hours.

| Time | Part of the day | Passengers from houses | Town traffic |
|---|---|---|---|
| 00:00-06:00 | night | 30% | 2 levels less |
| 06:00-09:00 | morning rush hour | 200% | 1 level more |
| 09:00-16:00 | day | 100% | as set by the town |
| 16:00-19:00 | evening rush hour | 180% | 1 level more |
| 19:00-24:00 | evening | 60% | 1 level less |

- Over a whole day passengers come to about 97% of the steady amount.
  Traffic changes only on roads that have some; it applies to speeds, the
  pathfinder, the traffic overlay, street life, land info, traffic lights
  and the script API's congestion level. The stored level stays the
  town's daily level, so town economics don't swing with the clock.
- **A day per year** keeps pace with the vehicles: a rush hour lasts about
  45 days and a bus round trip is a city half-hour or so. **A day per
  month** is faster to watch, but a rush hour then lasts about 4 days,
  shorter than most round trips.
- **Peak bus lanes**: with the clock on they are bus lanes in the rush hours
  only (like all-day lanes then) and open to all the rest of the day. With
  the clock off they work as in part 7.
- **Off-peak service** (line window, new "Off-peak" dropdown): all vehicles
  all day, a number of vehicles between the rush hours, or rush hours only
  (a peak shuttle). When a rush hour ends, the others go, emptiest first, to
  the nearest depot and stop there: no running costs, and no "waiting in
  depot" news. When the next rush hour starts they set out again; any still
  on their way to the depot turn back. A line parks vehicles only when the
  stretch until the next rush hour lasts at least two of its round trips,
  otherwise they would spend it driving to the depot and back; with a day
  per month only short lines park. The line window's "Service" line says
  how many wait in a depot, or that the stretches are too short for the
  line. Renewed or replaced vehicles keep waiting, ones their owner starts
  again just run, and the time spent waiting doesn't count as travel time
  (or the line's round trip would seem far longer).
- **On-demand lines** (line window, new "On demand" button): after each stop
  a vehicle heads for the stop of its line where its passengers get off
  (counted double), where the most passengers it has room for wait (less the
  room on the line's other vehicles already heading there), or that waited
  longest for a pickup; nearer ones first. With cargo distribution, who gets
  off where comes from each stop's planned flows for the station they first
  boarded at (passengers on board don't store their next stop); without it
  everyone gets off at the next stop. Stops on the way don't become
  implicit orders, and "Even spacing" doesn't hold its vehicles. For
  off-peak parking an on-demand line's trip counts as two legs between
  stops.
- Saved: the setting by name; a line's off-peak number and on-demand switch
  in LINE; the vehicles waiting for the next rush hour in a new chunk PEAK.
- Script API: `ScriptDate.GetCityTime`, `IsRushHour`;
  `ScriptOrder.SetLineOffPeakVehicles`, `GetLineOffPeakVehicles`,
  `GetLineVehiclesWaitingForPeak`, `SetLineOnDemand`, `IsLineOnDemand`;
  `ScriptRoad.GetCongestion` now gives the level at this time of day.
- Code: `src/city_clock.{h,cpp}`, `src/saveload/city_clock_sl.cpp`; hooks in
  `town_cmd.cpp` (passengers), `bus_priority.cpp` (traffic, peak lanes),
  `road_traffic.cpp`, `street_life.cpp`, `town_traffic.cpp`, `misc_gui.cpp`,
  `vehicle.cpp` (on-demand next stop, depot news, deleted vehicles),
  `vehicle_base.h` (next stations), `autoreplace_cmd.cpp`, `line.cpp`,
  `line_gui.cpp`, `statusbar_gui.cpp`, the settings tables.

Tests (test AI PeakTest; 3 years each; Kinningbury, 10,400 people, with 6
Merbok Twin Decker buses on part 7's 16-tile line, a day per year):

| | Night | Morning rush | Day | Evening rush | Evening | Clock off |
|---|---|---|---|---|---|---|
| Town traffic in the centre (average level) | 0.9 | 3.9 | 2.8 | 3.8 | 1.8 | 2.8 |
| Passengers waiting at the two stops | 19 | 167 | 100 | 175 | 71 | 112 |

| Off-peak service | Buses out between rush hours | Running costs 2021 | Riders 2021 | Satisfaction |
|---|---|---|---|---|
| All day | 6 | 11,905 | 2,447 | 55% |
| 2 between rush hours | 2 (night, day, evening) | 6,384 (-46%) | 2,575 | 69% |
| Rush hours only | 0 | 3,321 (-71%) | 754 | 49% |

- With the clock off the same line carried 3,186 riders: peaky demand is
  harder to serve with the same buses. Parking 4 of them off-peak carried
  more people than running all 6, as fewer buses bunched.
- A day per month: the line (round trip 28 days) kept all 6 running, as the
  stretches between rush hours are too short for it.

| On-demand (6 stops around the centre), riders 2021 | Fixed loop | On demand |
|---|---|---|
| Kinningbury, 3 buses | 7,968, satisfaction 52% | 9,177, 72% |
| Grubourne (4,500 people), 2 buses | 3,503, 63% | 5,531, 69% |
| Kinningbury, 3 buses, cargo distribution (symmetric) | 4,821, 66% | 7,960, 64% |
| Grubourne, 2 buses, cargo distribution | 3,553, 58% | 5,616, 67% |
| Different stops the buses head for at a time | 1.7 and 1.0 (they bunch) | 3.0 and 1.9 |

- An on-demand line keeping 1 of 3 buses between the rush hours parked the
  other 2 at night, in the day and in the evening.
- With cargo distribution the first version did worse than the fixed loop
  (3,286 riders in Grubourne): it counted on passengers storing their next
  stop on board, which they don't, so full buses headed for crowds they had
  no room for. Reading the stops' planned flows fixed it.

- A save with 4 buses parked (`city-rushhour-demo.csav`) reloads with them
  still waiting; all 6 set out at 06:04.
- With the clock off, part 11's baseline run gives identical monthly
  results; 99/99 unit tests; all 35 test saves load, save and reload.
- The first try used a day per month as the main option: buses spent the
  whole stretch driving to the depot and back, and the rush-hours-only line
  never finished a trip. Hence a day per year first and the round-trip
  rule. A bus's waiting time also counted as travel time, which made the
  round trip look like 60+ days and stopped parking in the day; fixed.
- Screenshots: status bar "Apr 28th, 2022 07:43" (time in orange); the
  line window with "Service: 2 vehicles between rush hours, 4 waiting in a
  depot now", the Off-peak list and the On demand button; the setting with
  its three values.

## Done: transit part 13, metro and rail

Four pieces for running trains like a metro: a map of each line, platforms
that fill up, platform screen doors, driverless lines, and feeder buses the
towns ask for. They work for any line, but platforms, doors and driverless
running only concern railway stations and trains.

**Line map.** The line window has a new **Map** button (bottom row, after
Timetable). The map shows the line's stops from top to bottom in the line's
colour, like the map above the doors of a metro train:

- A line that goes out and comes back the same way (stops A B C D C B) shows
  each stop once; any other line is a loop that ends in a dashed stretch and
  "Back to" its first stop.
- Under each stop: passengers waiting and, at railway stations, how full the
  platforms are (red when crowded) and whether they have screen doors.
- Stops where other passenger lines stop too get a ring and those lines'
  badges, so you see where to change.
- The vehicles are arrows beside the line, on the left on their way down and
  on the right on their way back, placed by how far into each trip they are
  (from the timetabled or estimated trip times). Refreshes twice a second.
- Click a stop to scroll to it, Ctrl+click to open it; click a vehicle to
  open it, Ctrl+click for its line.

**Platform crowding.** Railway platforms hold 120 passengers a tile, plus
30 for each bus or tram stop of the same station. Above that they are
crowded:

| Effect | Without screen doors | With screen doors |
|---|---|---|
| Boarding and getting off (trains) | 1% slower per 4% over, at most 30% | not slowed |
| Passenger rating | 1 less per 3% over, at most 40 of 255 (16%) | half that |
| Riders' satisfaction (crowding part) | the worse of crowded vehicles and crowded platforms (100 up to 70% full, a point less per percent above) | the same |

- How crowded the platforms are is followed over the last ten days or so
  (saved with the station). The station window's ratings view has a line
  "Platforms: 637 waiting, room for 240 (265% full): passengers -7%, screen
  doors".
- People stand on the platforms in the main view, up to 12 a tile when they
  are full (street life setting "people").
- Stations without railway platforms aren't affected: bus stops keep their
  street-side crowds from part 2.

**Platform screen doors.** A new station window button, **Screen doors**
(bottom row, after Interchange; only at stations with railway platforms):
glass doors along the edges of all platforms, half height as on elevated
MRT stations, with a green light over each door. About 1,800 a platform
tile at base prices (60 times the object price), upkeep 120 a tile a year;
the town pays half when its transit budget allows, as for interchanges.

**Driverless lines.** A new **Driverless** button in the line window (next
to On demand) for train lines, like Singapore's North East Line or Kuala
Lumpur's Kelana Jaya Line:

- Needs electric trains (electric, monorail or maglev engines), screen doors
  at every station a passenger line stops at, and no level crossings this
  month or last month. The line window says what is still missing
  ("Trains: with drivers; driverless needs screen doors at every stop (1 of
  2 have them)").
- Fitting the trains costs a fifth of the price of their engines. Then
  running costs are 20% lower and boarding is 10% faster (automatic doors).
- A driverless train that enters a level crossing gets its drivers back at
  once, with a message; so does a line that later buys a diesel train or
  stops at a station without doors (checked monthly).
- Level crossings that a line's trains enter are counted each month (saved
  with the line).

**Feeder contracts.** Towns with a railway station that passenger trains
stopped at last month now and then (one chance in six a month, at most every
18 months, if the budget allows a year of it) ask for a feeder bus between
the station and houses 8 to 24 tiles away that no bus stop or station
reaches, like Rapid KL's MRT feeder buses:

- News: "Trinborough wants a feeder bus between Trinborough and houses 11
  tiles away that no stop reaches: about ... a month for 5 years to the
  first company whose bus line links them, by ..." (the estimate is two to
  four buses' costs plus 20%, the deadline a year on). The town's Transit
  window lists it as "Feeder wanted"; its Line button scrolls to the
  houses.
- Any company's bus line with a stop at the station or within 4 tiles of it
  and a stop within reach of the houses, leaving its first stop 4 times a
  month or more, gets the contract at the next month's end: the line's costs
  plus 20%, 5 years, the town keeps the fares, paid by departures with the
  same targets, reviews and extensions as part 4's contracts. It ends if the
  line stops linking the station and the houses.
- Requests nobody takes up end after a year.

- Saved: lines' driverless flag and crossings in LINE; stations' screen
  doors and crowding in FSTA; feeder requests and contracts in TCON (new
  fields station and target, new contract type). Old saves start with
  none.
- Script API: `ScriptOrder.SetLineDriverless`, `IsLineDriverless`,
  `GetLineDriverlessProblem` (DP_NONE, DP_NOT_TRAINS, DP_NOT_ELECTRIC,
  DP_NO_SCREEN_DOORS, DP_LEVEL_CROSSINGS), `GetLineLevelCrossings`,
  `IsLineFeeder`; `ScriptStation.BuildScreenDoors`, `HasScreenDoors`,
  `GetPlatformPlaces`, `GetPlatformCrowding`; `ScriptTown.GetFeederOfferStation`,
  `GetFeederOfferTile`.
- Code: `src/metro.{h,cpp}`, `src/metro_cmd.h`, `src/line_map_gui.cpp`;
  hooks in `station_cmd.cpp` (crowding, rating, drawing the doors),
  `economy.cpp` (boarding speed), `train_cmd.cpp` (running costs, level
  crossings), `line.cpp`, `line_gui.cpp`, `satisfaction.cpp`,
  `street_life.cpp`, `station_gui.cpp`, `transit_authority*.cpp`; sprites
  244-247.

Tests (test AI MetroTest: one train per track on parallel tracks between
Trinborough, 3,100 people, and Slundston, 7,300, 38 tiles apart; settings
for tracks, platform length, wagons, a station in the town centre, bridges
or level crossings, diesel, and the month to fit doors, go driverless, build
a road across the tracks; it also runs the feeder lines towns ask for):

- Driverless: refused at the start (no screen doors) and for diesel trains
  even with doors. Doors at both stations (8 platform tiles) cost the
  company 7,200 (the towns paid the other half); fitting 2 AsiaStar trains
  40,312; running costs 7,030 -> 5,624 a year. A road across the tracks in
  month 9: the next month the line ran with drivers again (2 crossings).
- Crowding (one 40-seat train, 240 places at each station, rush hours on):

| 2021 | Platforms at the end of the year | Passenger rating (average) | Riders |
|---|---|---|---|
| No screen doors | 320-340% | 59% | 920 |
| Screen doors | 310-330% | 64% | 1,040 |
| Screen doors and driverless | 160-195% | 70% | 1,102 |

- Feeders: both towns asked within the first months; the AI's two-bus lines
  (one stop joined to the railway station) were signed the month after at
  547 a month and paid 349-547 a month by departures (2-4 a month, 3
  asked), with 200-440 riders.
- Trains spend 50-90% of their time at stations in these runs. That is how
  OpenTTD loads: a train keeps loading while passengers trickle in, 40 ticks
  a step, and a train longer than its platform loads at a quarter of the
  speed (the AsiaStar has an engine at each end, so engine and wagon are
  1.5 tiles long). The tests use platforms the trains fit.
- A bus-only game (part 11's baseline run) gives identical monthly results;
  towns without passenger trains don't roll for feeders, so nothing changes
  for them. 99/99 unit tests; all 36 test saves (with the new
  `city-metro-demo.csav`) load, save and reload.
- Screenshots: the Slundston Woods platforms with screen doors, a train and
  a crowd; the line map with the feeder line's badge at Trinborough and
  "265% crowded" at Slundston Woods; a bus loop's map ending "Back to
  Kinningbury"; the line window with "Trains: driverless, running costs
  -20%, boarding +10%"; the station's ratings view; Trinborough's Transit
  window with "Line F2: feeder contract, 547 a month until June 2025".

## Done: transit part 14, land value and pollution

The roadmap's M7. A new setting, **Air pollution and land value** (Settings,
Environment, Towns, after the city clock): Off, Light pollution, Normal
pollution (default) or Heavy pollution. Off works as in OpenTTD.

**Air pollution.** Once a month the map is surveyed in cells of 4 by 4
tiles. The air is measured on Malaysia's and Singapore's air pollutant
index (Malaysia's API, Singapore's PSI): good up to 50, moderate up to 100,
unhealthy up to 200, very unhealthy up to 300, then hazardous.

| Source | Pollution a month (units) |
|---|---|
| Road tile with town traffic | 3 x its congestion level squared (level 4: 48, level 7: 147) |
| Diesel bus | 80 where it is at the month's end |
| Lorry or van | 120 |
| Diesel train / steam train | 120 / 200 |
| Ship | 60 |
| Aircraft at or over an airport | 150 |
| Processing plant or power station tile (factories, refineries, steel mills, sawmills...) | 150 at normal production, more or less with it |
| Mine or oil well tile | 60 |
| Farms, forests, plantations, banks, water towers, toy shops | none |
| Electric buses, trams and trolleybuses under wires, electric trains | none |
| A tree / a cell a park reaches | takes up 3 / 40 |

The wind spreads pollution over about two cells around (two passes of a 3
by 3 average); the index is 15/32 of the net units and follows the sources
over a few months. Light and Heavy make the sources 60% and 150%.

**Land value**, 0 to 100 a cell, also monthly:

| Part | Value |
|---|---|
| Town centre / inner suburb / outer suburb / outskirts / edge | 56 / 48 / 40 / 34 / 30; countryside 10 |
| Town size | +3 a doubling of the population above 1,000, at most +15 |
| Police, fire, clinic, school reaching it | +4 each |
| A park reaching it | +8 |
| A good passenger station reaching it | up to +12 for a railway station, +5 for a bus or tram stop, by its passenger rating; +4 at a transport hub or BRT station |
| Water in the cell | +5 |
| Industry in the cell | -5 |
| Air pollution | minus a fifth of the index, at most -50 |

**Effects:**

- **Happiness**: a point less per 10 of a town's index above 50 (where its
  people live), at most 15.
- **Growth**: polluted towns take longer between new houses, a percent per 6
  above 50, at most 25%.
- **What towns build** (new houses and the ones towns rebuild): on land
  worth 30 to 59, most of an ordinary town, as in OpenTTD. Dearer land makes
  homes and offices with more people a tile likelier, up to 3.5 times; land
  under 30 (dirty air, industry, far out) does the same for small houses.
  Public buildings are left alone.
- **Towns' opinion of companies**: each month a town whose air is moderate
  or worse thinks less of companies whose diesel road vehicles and trains
  are in it (about 2 rating points a bus, at most 30, down to Poor); one
  that runs only clean ones there (3 or more) gains 10 a month up to Good.
- **News** when a town's air turns unhealthy ("Haze over Kinningbury!");
  the air has to clear (under 90) before the next.

**Seeing it:**

- **Map** (F4): two new views at the bottom of its buttons, **Air
  pollution** (a factory under smoke) and **Land value** (a house with an
  arrow up), with legends.
- **Towns menu**: **Air pollution map** and **Land value map** tint the land
  in the main view in the same colours (again to hide).
- **Town window**: "Air: API 72 (moderate); land value 50 (medium)".
- **Land info** (any tile): "Air pollution: API 113 (unhealthy)" and "Land
  value: 45 of 100 (medium)".

- Saved: the two maps in chunk ENVR, each town's figures in TENV. Old saves
  start without them; the first month works them out.
- Script API: `ScriptTile.GetPollution`, `GetLandValue`;
  `ScriptTown.GetPollution`, `GetLandValue`.
- Code: `src/environment.{h,cpp}`, `src/saveload/environment_sl.cpp`; hooks
  in `town_cmd.cpp` (building choice, growth), `city_services.cpp`
  (happiness), `smallmap_gui.cpp`, `viewport.cpp`, `toolbar_gui.cpp`,
  `town_gui.cpp`, `misc_gui.cpp`; sprites 248-249.
- Cost: the survey walks the whole map once a month: about 10 ms on a 1024
  by 1024 map, 40-50 ms on 2048 by 2048 (a quarter of a second on the
  largest maps).

Tests (test AI EnvDump logs the biggest towns and the whole map monthly;
BusTest's 13-bus line through Kinningbury, 10,500 people, mid-2021):

| Kinningbury | Town's index | Centre | Land value | Town's view of the bus company |
|---|---|---|---|---|
| Normal, 13 diesel buses | 48 | 75 (moderate) | 53 | Excellent |
| Normal, 13 electric buses | 34 | 51 | 55 | Outstanding |
| Heavy, diesel | 72 | 103 (unhealthy) | 50 | Excellent |
| Heavy, electric | 47 | 71 | 53 | Outstanding |

- The dirtiest cell on the map is next to Shorningwell's oil refinery: 106
  (Normal), 161 (Heavy). Under Normal pollution 98% of the map's cells are
  good, 2% moderate.
- What towns build (test AI SvcTest: a town of 1,100 people funded to grow
  for 6 years, with and without its city services; people at the end):

| Town | Setting on | Off |
|---|---|---|
| Without services, three maps | 3,448 / 2,818 / 3,057 | 3,410 / 2,770 / 3,348 |
| With all services (land value 75, centre 84), two maps | 3,903 / 4,515 | 3,528 / 2,877 |

  Ordinary towns grow as before (within the runs' spread); towns with city
  services build bigger homes and offices. The first version weighed every
  building by land value and grew ordinary towns 29% denser (or 12% smaller
  once the edges counted as cheap land); land of medium value now builds as
  in OpenTTD.
- With the setting off, part 13's baseline run gives identical monthly
  results; 99/99 unit tests; all 37 test saves (with the new
  `city-pollution-demo.csav`, Heavy pollution) load, save and reload.
- Screenshots: the main view tinted by air pollution over Kinningbury's
  centre; the map's Air pollution and Land value views; the town window;
  land info on a statue (API 113, land value 45).

## Done: M6, town budgets and ordinances

The owner's design call: **towns decide, companies lobby**. The player stays
a transport company; towns run their own budgets and bring in their own
rules, and a company can propose a rule to a town. A new setting, **Town
budgets and ordinances** (Settings, Environment, Towns, after Air pollution
and land value), on by default. Off works as part 14 did.

**Budgets.** Each town collects taxes: about £10.80 a resident a year at
normal costs, in proportion to 50 plus its land value (+30% on land worth
80, -30% on land worth 20). What it spends is reckoned from its taxes at the
normal rate, whatever its tax rate:

| Spending | Share of the town's taxes |
|---|---|
| Transit: the transit authority's budget for contracts, passes and concessions (part 4) | 25% (as much as before on land worth 50); 40% with a transit fund |
| Running the town: roads, staff, upkeep | 60% |
| Ordinances | 0 to 6% each (below) |
| Left over, into reserves | 15% without ordinances |

A town starts with half a year's taxes put by. Each January it looks at its
money:

- in debt: taxes up 10 points; deep in debt (more than half a year's taxes):
  it also repeals its dearest ordinance that has been in force two years
  ("Kinningbury repeals its ordinance Transit fund to balance its budget");
- after a year in surplus with more than a year's taxes put by: taxes down
  10 points;
- after a year in deficit with less than a quarter of a year's put by: up 10.

Rates go from 70% to 150% of normal. Each 5% above normal costs a point of
happiness (and each 5% below adds one), and the time between new houses
follows the rate (at 80% they come a fifth sooner).

**Ordinances.** A town brings in at most one a year on its own, when it has
the problem and the money (what is left of the 15%, or two years of the cost
put by). A company proposes one from the town's Budget window; the town
agrees under the same conditions, plus a rating (only checked once the town
has rated the company). A proposal costs the same as a congestion charge
proposal (£3,750 at normal costs).

| Ordinance | Effect | Cost a year | A company can propose it with | A town brings it in on its own when (chance a year) |
|---|---|---|---|---|
| Transit fund | 40% of taxes for transit instead of 25% | 15% of taxes (into transit) | 1,500 people, the company's buses or trains in the town, Good rating | transit takes 20% of cars off the road and it has a year's taxes put by (1 in 3) |
| Parking limits | town traffic -15% in the centre and inner suburbs; a parking levy brings in 3% of taxes; happiness -1 | 1% | 3,000 people, a congested centre (level 1.5 on average), Mediocre rating | 6,000 people and level 2.5 (1 in 2) |
| Low-emission zone | traffic pollution in the centre and inner suburbs -30%; company road vehicles that aren't electric pay to drive in (£22.50 a bus, £30 a mail van, £60 a lorry at normal costs) | 2% | 3,000 people, air moderate or worse (API over 50), Good rating | API 100 or more (1 in 2) |
| Car-free centre | town traffic -60% in the centre; happiness +3; land value +5 there | 6% | 5,000 people, transit taking 15% of cars off the road, Good rating | 8,000 people, 25% of cars off (1 in 3) |
| Tree planting | the town plants 2 to 8 young trees a month (more for bigger towns) on grass and rough land that isn't zoned for buildings, or is zoned park | 3% | 1,000 people, Mediocre rating | API 60 or more (1 in 2) |
| Transit-oriented development | land value +10 around railway stations with passenger trains, so towns build bigger there | nothing | a railway station with passenger trains, Mediocre rating | it has one (1 in 4) |

The congestion charge (part 9) is listed with them and proposed the same
way. News: "Kinningbury brings in a new ordinance: Parking limits!" (or "...,
as [your company] proposed!").

**Seeing it:** the town window has a new **Budget** button (between Transit
and Coverage). The window shows the taxes, spending, fees, reserves and what
taxes and ordinances do to happiness and growth; then each ordinance with
"in force since ..." or "not in force", its effect and cost, and what it
needs ("You can propose it" in green when the town would agree). Click a
row, then **Propose**; a refusal says why ("... its air is good").

- Saved: each town's budget in chunk TBUD. Old saves start without one; the
  first month sets it up.
- Script API: `ScriptTown.HasOrdinance`, `ProposeOrdinance`, `SetOrdinance`
  (game scripts), `GetTaxRate`, `GetTaxIncome`, `GetReserves`,
  `GetCarReduction`, `GetLowEmissionZoneVehicles`; `ORDINANCE_*`.
- Code: `src/town_budget.{h,cpp}`, `town_budget_type.h`, `town_budget_cmd.h`,
  `town_budget_gui.cpp`, `src/saveload/town_budget_sl.cpp`; hooks in
  `transit_authority.cpp` (the transit budget), `town_traffic.cpp`
  (traffic), `road_pricing.cpp` (the zone fee), `environment.cpp`
  (pollution and land value), `city_services.cpp` (happiness),
  `town_cmd.cpp` (growth), `town_gui.cpp`.

Tests (test AI BudgetTest proposes ordinances at set months and logs the
town monthly; BusTest's line through Kinningbury, 10,500 people):

- Money: Kinningbury collects £118,000 to £124,000 a year; its transit budget
  is £29,600 to £31,000, the same as before; reserves grow by about 15% of
  taxes a year.
- Proposals: parking limits accepted (centre congestion 2.3-2.4 down to
  1.6-1.7); tree planting accepted (the town's API 48 down to 38 within a
  year); transit fund accepted (transit budget £29,500 to £47,000; reserves
  stay near £68,000); low-emission zone refused (air good), car-free centre
  refused (transit took 7-11% of cars off with a two-stop line),
  transit-oriented development refused (no railway).
- Car-free centre with a better line (20 buses, 8 stops, 30-50% of cars
  off), against the same game without the proposal:

| Kinningbury, 2021 | Car-free centre | Without |
|---|---|---|
| Centre and inner suburbs' congestion | 0.0-0.2 | 0.8-1.2 |
| Town's API | 28-32 | 37-42 |
| Land value | 61-62 | 57-58 |
| Happiness, the first half year | 48-51 | 45-48 |

  The town without the proposal brought in a car-free centre itself at the
  2022 review.
- Low-emission zone under Heavy pollution, proposed at API 66: API 76 six
  months later against 90 without; 16 to 25 company diesel vehicles paid to
  drive in a month. The town without the proposal brought one in itself at
  its 2021 review (API about 100); after that both stay near 90 as the town grows.
- Ten years, nobody proposing: the four biggest towns bring in parking
  limits on their own (Kinningbury 2021, Nennville and Gredinghall 2022,
  Slundston 2023); their taxes settle at 80% from 2025 with reserves of
  about £135,000. With five times the polluting traffic, Kinningbury
  brought in tree planting at API 67-74.
- First versions taught: spending that followed the tax rate let reserves
  grow forever (now reckoned at the normal rate); taxes fell three years
  running (a cut now needs a year in surplus); a car-free centre that needed
  parking limits first was out of reach for towns with strong transit.
- With the setting off, part 13's and part 14's baseline runs give identical
  monthly results; 99/99 unit tests; all 38 test saves (with the new
  `city-budget-demo.csav`) load, save and reload.
- Screenshots: the Budget window for Kinningbury (transit fund, parking
  limits and tree planting in force) and a refused low-emission zone.

## Done: M5, commuting

Towns work out where their people work and how they get there, and town
traffic follows the routes the drivers take. A new setting, **Commuting**
(Settings, Environment, Towns, after Town budgets and ordinances), on by
default; it needs town traffic. Off works as M6 did.

**Who goes where.** Once a month (each town on its own day) a town cuts its
area into cells of 4 by 4 tiles (8 by 8 when more than 200 cells have
homes). 45% of its people go to work on a day: from each cell by the people
living there (homes; offices, shops and public buildings count a quarter
and a tenth of their people as flats above), to cells by their jobs
(offices, shops and public buildings; homes a tenth; city services 30 a
tile; the town's industries 10), fewer the further away (half as many at
10 tiles; a gravity model).

**Drive or ride?** Times count a town tile as about 75 m and a tick as a
third of a second:

| Commuter by car | Time |
|---|---|
| The quickest way over the town's roads | 52 ticks a tile at 60 km/h (corners, crossings, lights), slower with town traffic (part 2's speed caps), 100 km/h on expressways |
| Junction / with traffic lights / level crossing / drive-through stop | +15 / +25 / +20 / +10 ticks |
| Parking and walking to work | 5 minutes in the centre, 3 inner suburbs, 2 outer, 1 outskirts, half at the edge; half as long again with parking limits; 5 more from the edge of a car-free centre |
| A congestion charge into the zone | feels like 3 more minutes |

| Commuter by transit | Time |
|---|---|
| Walking to a stop within reach (a station's catchment covers the home) and from the last stop | 130 ticks a tile, feels twice as long |
| Waiting | the line's measured wait (longer when its vehicles bunch), up to 4 times longer when they leave more than 70% full on average; feels half as long again |
| Boarding / changing | feels like a minute / two minutes |
| The fare | feels like 2 minutes at the standard price, nothing when free; changing pays no new fare |
| Riding | the line's measured driving time between its stops, plus up to 120 ticks standing at each stop on the way (OpenTTD's loading takes far longer than a commuter would notice) |

Of those who could do either, the share taking transit is 1 / (1 + r^2),
r being how long transit feels against the car: half when it feels as long,
a fifth at twice as long, at most 85%. Only homes and jobs within reach of
a stop can choose transit. Every passenger line counts (any company, any
transport but aircraft), with its stops in the town and around it.

At least as many commuters take transit as the share of the town's
passengers your services carry says (part 2's measure: 60% at most when
they carry all of them). So nothing does worse than before: a bus line
that carries passengers takes cars off the road as it did. Those extra
riders are found among the drivers within reach of stops (up to the 85%);
only if there aren't enough of them do the rest leave their cars at home
all over town.

**What it does:**

- **Traffic follows the routes.** Every drivers' route adds its cars to the
  roads it takes; town traffic counts half the old traffic of the homes and
  jobs around a road (errands, deliveries) and 25 per commuter's car a day.
  The roads between homes and jobs get busy; side streets stay quieter.
  Routes avoid congestion, and each month's routes are averaged with the
  last, so traffic spreads over parallel roads as jams build.
- **Transit takes cars off those routes**: what transit takes off the road
  for the whole town (the town window, the M6 ordinances, the congestion
  charge's residents) is now the share of commuters who take it.
- **Riders**: homes and workplaces whose commuters take transit because it
  is quick (journey times alone) send more passengers: 2% more for each
  percent above 15%, up to twice as many at 65%. Never fewer than before.

**Seeing it:**

- **Town window**: "Commuters: 5,516 a day, 28% by transit" and "Door to
  door: 13 min by car, 32 min by transit" (or "transit doesn't go where they
  work"), in place of "Your services take ...% of cars off the road".
- **Towns menu**: **Commuter traffic map** tints roads by commuters' cars a
  day, both ways: green from 100, yellow 400, orange 1,000, red 2,000 (again
  to hide).
- **Land info**: a road's "Commuters: 872 cars a day"; a house's
  "Commuting from here: 48% by transit" (homes) or "Commuting to work here:
  31% by transit" (other buildings).

- Saved: each town's figures, its grid of shares and the cars on each road
  in chunk TCOM. Old saves start without them; until a town's first month
  its traffic is worked out as before.
- Script API: `ScriptTown.GetCommuters`, `GetTransitCommuters`,
  `GetCommuteTime` (ticks, by car or by transit); `ScriptRoad.GetCommuterTraffic`.
- Code: `src/commuting.{h,cpp}`, `commuting_type.h`,
  `src/saveload/commuting_sl.cpp`; hooks in `town_traffic.cpp` (traffic,
  the town's car reduction), `town_cmd.cpp` (passengers, a removed town's
  cars), `town_gui.cpp`, `misc_gui.cpp`, `environment.cpp` and
  `toolbar_gui.cpp` (the map), the settings tables.
- Cost: 0.2-0.8 ms for a town of 10,000 people, 4 ms for 28,000 and 7 ms
  for 41,000, once a month each, on its own day.

Tests (test AI CommuteDump logs the biggest towns monthly: commuters, the
shares, times, congestion levels and commuters' cars on their roads;
Kinningbury, about 11,000 people, 17 months from mid-2021):

| Kinningbury | By transit (journey times alone) | Door to door, car / transit | Busiest road, cars a day | Roads at level 4+ (5+) | Before M5 |
|---|---|---|---|---|---|
| No transit | 0% | 9 min / - | 2,800 | 22% (9%) | 17% (5%) |
| BusTest's line: 2 stops, 13 buses | 11% (0%) | 9 / 20 min | 2,400 | 11% (6%) | 12% (4%), 10% of cars off |
| PeakTest's 8-stop loop, 8 buses | 27% (4%) | 9 / 21 min | 1,900 | 5% (3%) | 1% (0%), 24% off |
| The same loop, 20 buses | 40% (11%) | 11 / 15 min | 1,700 | 1% (0%) | 0%, 42% off |

- Over the four biggest towns congestion stays about as before (roads at
  level 4 and up: 22% against 20% without transit), but more of it is on
  the main roads (level 5 and up: 8% against 4.5%) and less on side streets.
- With the 20-bus loop the loop's stops cover most of the centre: homes
  there send 47-53% of their commuters by transit, the uncovered edges 0%.
- A car-free centre (M6) makes the centre a five-minute walk from the car:
  journey times alone send 16% to transit instead of 11%.
- The first versions taught a lot: OpenTTD's towns are small and its buses
  slow (about 100 ticks a tile between stops, against 26 for a free car at
  the same speed), so true-to-life costs sent almost nobody to transit, and
  a 2-stop line that carried a fifth of the town's passengers took no cars
  off the road. Hence town-scale driving (52 ticks a tile), standing time
  capped at stops, and the floor of riders from the passengers carried.
  Counting people only in homes gave Kinningbury 540 commuters (its people
  live mostly in offices, by OpenTTD's count); a stop counted for getting
  on and off at once made rides out of nothing.
- With the setting off, part 13's and part 14's baseline runs give
  identical monthly results; 99/99 unit tests; all 39 test saves (with the
  new `city-commute-demo.csav`) load, save and reload.
- Screenshots: the town window's commuting lines; the commuter traffic map
  over Kinningbury with the 20-bus loop, and land info on a road (872 cars
  a day).

## Done: M5 part 2, commuting between towns

People also work in other towns. Each town's commuters can now go to the
jobs of up to four other towns within 64 tiles (between the centres): the
ones that draw them most, big and near (people / (distance + 20)^2). The
same gravity model spreads them over the jobs of their own town and those
towns, so a suburb next to a big city sends many of its people there, two
towns of the same size few.

- **Only where they can get to.** Commuters go only to jobs they can reach:
  by road (the town's area, the other towns' areas and the land between
  them make one road network; OpenTTD doesn't connect towns by road, so
  that is up to you), or by transit (any line with stops in both). A job
  reachable only by transit draws only people living within reach of a
  stop, to jobs within reach of the other stop. Jobs nobody can reach draw
  nobody: those people work in their own town. (Before, inside a town, too:
  commuters sent to unreachable jobs were lost.)
- **On the roads between towns.** Drivers' routes run over country roads
  and expressways, so roads far from any town now get commuter traffic and
  congestion too (town traffic used to stop at twice a town's radius).
  Parking, ordinances and congestion charges are those of the town the job
  is in.
- **Transit between towns.** Trains, trams and buses with stops in both
  towns carry commuters as inside a town: walking, waiting, riding, changing
  and the fare against the drive. The share of a town's passengers its
  services carry still sets the least share taking transit, now of all its
  commuters, those to other towns included.
- **Town window**: a line "Other towns: 502 work there (0% by transit), 510
  come to work here (0%)", when there are any.
- Saved: each town's commuters to each other town (TCOM).
- Script API: `ScriptTown.GetCommutersTo(from, to)`,
  `GetTransitCommutersTo(from, to)`.
- Cost: a town works out the roads of the other towns too: 0.2-0.6 ms for
  a town of 10,000 people, 5.5 ms for 28,000 and 7.4 ms for 41,000 (linked
  by expressway), once a month.

Tests:

- TollTest's 11-tile expressway with a toll plaza between Grubourne (5,600
  people) and Brondinghattan Ridge (7,400), 28 tiles apart: about 500
  commuters a day each way, all by car; 2,008 commuters' cars a day on the
  expressway (level 1 of 7, flows freely); the average commuters' cars on
  the two towns' roads up from 596 to 747 and from 478 to 734; their
  drivers' door-to-door time up from 8 to 9 minutes (longer trips). Without
  the expressway, nobody commutes between them.
- The saved game with an expressway between towns of 41,000 and 28,000
  people (and buses between them): 4,077 of the big city's 18,497
  commuters work in the other town (282 by bus), 3,994 of the other's
  12,659 the other way.
- MetroTest's railway between Trinborough (2,400-3,300 people) and
  Slundston (7,100-8,500) with its stations at the towns' edges and 3
  trains: 10 commuters a day from Trinborough by train and 1 back. A
  station at the edge reaches few homes and jobs; stations in the centres,
  or feeder buses to them, would bring more.
- Kinningbury has no town within 64 tiles: its figures are those of part 1.
- With the setting off, part 13's and part 14's baseline runs give
  identical monthly results; 99/99 unit tests; all 40 test saves (with the
  new `city-commute-towns-demo.csav`) load, save and reload.
- Screenshots: Brondinghattan Ridge's town window ("Other towns: 502 work
  there, 510 come to work here"); the commuter traffic map with the
  expressway red and land info on it (2,008 cars a day).

## Done: M4 part 2, Malaysian and Singaporean town buildings

The owner asked for high rises and building types that show how a city
develops, after Malaysia and Singapore, the fork's theme. A new setting,
**Town buildings** (Settings, Environment, Towns, before Town budgets and
ordinances): **Malaysian and Singaporean** (default), OpenTTD's, or Both.
It counts in the temperate and sub-tropical climates; the others keep
OpenTTD's buildings.

| Building | Built from | Where in town | Town of at least | People | Colours |
|---|---|---|---|---|---|
| Kampung house: timber, on stilts, zinc roof, a coconut palm | 1900 to 1985 | edge, outskirts, outer suburbs | (not over 20,000) | 14 | timber |
| Terrace (link) houses: three two-storey units, tiled roofs, porches | 1950 | edge to inner suburbs | - | 36 | light |
| Shophouses: three storeys, five-foot way, shutters, signboards | 1900 | outer suburbs to centre | - | 60 | bright |
| Walk-up flats: five storeys, open corridors | 1955 | outer, inner suburbs | - | 70 | bright |
| HDB slab block: twelve storeys on a void deck, two tiles long (along X or Y) | 1965 | outer suburbs to centre | 1,000 | 200 | bright |
| Office block: concrete, ribbon windows | 1965 | inner suburbs, centre | 1,500 | 140 | own |
| HDB point block: twenty storeys | 1975 | outer suburbs to centre | 2,500 | 160 | bright |
| Condominium: glass balconies, pool deck on a podium | 1990 | inner suburbs, centre | 4,000 | 180 | quiet |
| Shopping mall: four storeys, glass atrium, two by two tiles | 1985 | inner suburbs, centre | 6,000 | 240 | quiet |
| Glass office tower: curtain wall, set-back crown, mast | 1985 | centre | 8,000 | 220 | own |
| Mixed development: shopping podium with roof garden, residential tower with a sky garden | 1995 | inner suburbs, centre | 12,000 | 200 | quiet |
| Landmark tower: tapering steel and glass, a spire; one a town | 1998 | centre | 40,000 | 255 | own |
| Mosque: white walls, golden dome, minaret | 1900 | edge to inner suburbs | one each 6,000 people | 6 | own |
| Chinese temple: red walls, two-tier orange roof | 1900 | outer suburbs to centre | one each 6,000 people | 5 | own |

- **How a town develops**: by year (the table), by zone (as OpenTTD's: a
  bigger town has a bigger centre and inner suburbs), by town size (the
  taller ones only in bigger towns) and by land value (part 14: dear land
  near the centre, good stations, parks and city services draws the dense
  ones). Towns rebuild old houses as they grow, as in OpenTTD, so the
  kampungs give way to terraces and the shophouses and terraces to blocks
  and towers; the blocks and towers stand at least 25 to 60 years.
- **Colours**: each building gets one of eight facade colours (company
  colour ramps) by where it stands: pastel houses, bright HDB blocks and
  shophouses, quieter towers.
- **New games**: a generated town grows from a village, so its centre would
  keep the small buildings it began with; once generated, about half the
  houses of its centre and inner suburbs are rebuilt the way a town of its
  size builds them, so the tall ones stand in the middle.
- **Old games**: towns keep their buildings; what they build and rebuild
  from now on is Malaysian and Singaporean. OpenTTD's churches, parks and
  stadiums stay, and the houses that take food or water (towns in the
  desert need them); statues and fountains aren't built any more. NewGRF
  houses are always built.
- Built-in house types with the last 64 house IDs (4032 on; NewGRF houses
  get the ones below), drawn by `make_city_sprites.py` (sprites 250-272:
  three stages of construction and 20 buildings and parts). Saves keep the
  house types; nothing new in the save but the setting.
- Code: `src/city_houses.{h,cpp}`; hooks in `newgrf_house.cpp` (the house
  list, NewGRF houses below), `town_cmd.cpp` (drawing, which houses a town
  builds, new towns' centres), `town_gui.cpp` (the house picker),
  `town_zoning.cpp` (homes, businesses and public buildings for zoning and
  commuting), the settings tables.

Tests:

- A new 256 by 256 game in 2020 (seed 42): the biggest towns have 10,429,
  10,002, 9,910 and 9,245 people, about as many as with OpenTTD's
  buildings (10,500, 10,000, 9,500). Restricting the dense ones to big towns
  at first left them at a quarter of that (towns grow from villages while
  they are generated); hence the lower thresholds and the rebuilt centres.
- 22 years from 1950 with the fastest town growth: in 1972 the map has 273
  kampung houses, 396 terraces, 443 shophouses, 23 walk-up flats, 7 HDB
  slab blocks and 53 office blocks, with 21 mosques and 13 temples; the
  biggest town grew from 5,507 to 9,884 people.
- 22 years from 2020: in 2042, 642 terraces, 213 shophouses, 69 walk-up
  flats, 74 HDB slab and 92 point blocks, 49 condominiums, 155 office
  blocks, 26 glass towers, 4 malls and 11 mixed developments; no kampungs.
  The biggest town grew from 12,179 to 18,767 people; none reached 40,000
  for a landmark tower.
- With OpenTTD's buildings, the baseline runs give identical monthly
  results; 99/99 unit tests; all 43 test saves (with the new
  `city-malaysia-demo.csav`, `city-malaysia-1972-demo.csav` and
  `city-malaysia-2042-demo.csav`) load, save and reload.
- Screenshots: Funnford's centre in 2020 (10,600 people) and Fewood (1,770);
  Mendinghattan in 1972 (shophouse rows, office blocks, a mosque and a
  temple) and Funnford in 2042 (18,700 people, a city of towers).

## Done: M4 part 3, step 1: hospital, university, stadium and hawker centre

The owner asked for bigger city services and for town authorities with the
powers of Malaysia's councils (majlis daerah, perbandaran, bandaraya) and
Singapore's town councils, then states with elections, federal territories,
and regions on other islands run like Sabah and Sarawak. Agreed order
(owner's call): (1) the bigger services, (2) councils, (3) states. Councils
will build services themselves; companies can still build them too.
Elections: voters judge what a company's service does for them (traffic,
transit, air, services), nothing else.

Four new city services in the City Services toolbar (Towns menu, City
services; hotkeys 6 to 9):

| Service | Size | Reach | Counts as | Build / upkeep (in base object prices; a clinic is 900 / 180) | Draws |
|---|---|---|---|---|---|
| Hospital | 2 by 2 | 22 tiles | health care (like clinics) | 6,000 / 900 | visitors: passengers like a house of 70 a tile; 60 jobs a tile |
| University | 2 by 2 | 22 tiles | education (like schools); land around +4 | 5,000 / 700 | students and staff: like a house of 110 a tile; 45 jobs a tile |
| Stadium | 2 by 2 | 16 tiles | up to 3 points of happiness | 4,000 / 400 | crowds on match days (one day a week: four times like a house of 255), few otherwise; 12 jobs a tile |
| Hawker centre | 1 tile | 8 tiles | up to 4 points of happiness; land +2 | 600 / 100 | shoppers: like a house of 60; 40 jobs |

- They send passengers to stations within reach, like houses (the town's
  passengers, counted in its figures), take passengers in (stations near
  them accept passengers), and their jobs draw commuters (M5).
- A hospital's reach is health care and a university's schooling, so one of
  each brings a whole town's health and education coverage (and the
  happiness, growth and land value that come with them) to 100%.
- The town window shows all nine services' coverage.
- Saved: as objects (the bigger ones have their own 16 object types below
  the first services'); each town's coverage of the new ones in chunk CSVC.
- Script API: `ScriptTown.CITY_SERVICE_HOSPITAL`, `_UNIVERSITY`, `_STADIUM`,
  `_MARKET` for `BuildCityService`, `GetCityServiceCoverage` and the rest.
- Code: `city_services*.{h,cpp}`, `city_services_type.h`; hooks in
  `object_cmd.cpp` (passengers, acceptance), `town_cmd.cpp` (passenger
  generation), `environment.cpp` (land value), `commuting.cpp` (jobs);
  sprites 273-289.

Tests (test AI CivicTest builds the four near the biggest town's centre in
the third month; Funnford, 12,000 people at the start, against the same
game without them, a year on):

| Funnford | Without | With |
|---|---|---|
| Happiness | 43-48% | 73-75% |
| Health / education coverage | 0 / 0% | 100 / 100% |
| Land value | 51 | 55, rising to 64 |
| Passengers a month from the town | 1,750-1,860 | 1,970-2,330 |
| Time between new houses | 25 days | 21 days |
| People in February 2021 | 12,400 | 13,298 |

- With OpenTTD's buildings and no new services, the baseline runs give
  identical monthly results; 99/99 unit tests; all 44 test saves (with the
  new `city-civic-demo.csav`) load, save and reload.
- Screenshots: the four in Funnford with the City Services toolbar; the
  town window's coverage row.

### Research for steps 2 and 3

- **Malaysia's local authorities** (Local Government Act 1976, Act 171;
  Town and Country Planning Act 1976, Act 172; Street, Drainage and
  Building Act 1974, Act 133): three kinds, district councils (majlis
  daerah) for rural areas, municipal councils (majlis perbandaran: at least
  150,000 people and RM20 million a year since 2008) and city councils or
  city halls (majlis or dewan bandaraya: 500,000 people and RM100 million;
  city status is granted by the King). Led by a president (Yang DiPertua)
  or a mayor (Datuk Bandar); councillors appointed by the state since local
  elections ended in 1965 and 1976. They collect assessment tax, make
  by-laws, license trade, plan and control development, run drains, local
  roads, street lighting, markets, parks, public places, sanitation and
  waste, burial grounds. 155 in all: 20 city, 40 municipal, 91 district
  councils and 4 statutory bodies.
- **Singapore's town councils** (Town Councils Act 1988): led by the
  constituency's elected MPs; run the common property of HDB estates:
  cleaning, lifts, repainting, landscaping, new communal facilities; funded
  by service and conservancy charges (S&CC) and government grants (per flat,
  per lift), with sinking funds for cyclical works; rated in a yearly
  management report (green, amber, red) on cleanliness, maintenance, lifts
  and arrears. HDB upgrades old estates (Neighbourhood Renewal, Home
  Improvement, lift upgrading) and redevelops some en bloc (SERS).
- **States**: 13 states with assemblies (Dewan Undangan Negeri) elected
  first past the post in single-member seats at least every five years; the
  majority forms the government under a Menteri Besar or Chief Minister.
  States hold land, local government and natural resources; planning,
  water, health are shared with the federation; little revenue of their
  own (land), federal grants for the rest. Three federal territories (Kuala
  Lumpur, Putrajaya, Labuan) have no state government: city halls or
  corporations under the federal ministry. Sabah and Sarawak, across the
  sea, keep their own land laws and control immigration (to be researched
  further for step 3).

## Done: M4 part 3, step 2: shops and town councils

The owner said go for step 2 (councils) and asked for malls and
supermarkets, "super common features in Malaysia and Singapore", which the
towns lacked: a 22-year test game had ended with 4 malls on the whole map and
there were no supermarkets at all.

### Shops

New town buildings in the Malaysian and Singaporean style (setting **Town
buildings**), and the mall redrawn so it reads as one: five storeys of shops
with a big sign band and banners in its colours, a glass atrium at the front
corner, a skylight and cars on its roof.

| Building | Size | From | Where in town | Town of at least | A town wants | People | Takes |
|---|---|---|---|---|---|---|---|
| Supermarket: a two-storey store, glass front, sign band, a car park | 1 tile | 1965 | outskirts to centre | 800 | 1, and 1 more for each 2,500 people | 40 | goods, food (full) |
| Shopping complex (1970s and 1980s): a podium of shops with rows of signboards, cars on the roof, an office tower over one end | 2 tiles, along X or Y | 1973 | outer suburbs to centre | 3,000 | 1, and 1 more for each 10,000 | 150 | goods, food (half a tile) |
| Shopping mall (redrawn) | 2 by 2 | 1985 | inner suburbs, centre | 6,000 | 1, and 1 more for each 8,000 | 240 | goods, food (half a tile) |
| Hypermarket: a big box with an entrance canopy, a sign band and a pylon, a car park with rows of cars, lamp posts and a trolley shelter | 2 by 2 | 1993 | edge, outskirts, outer suburbs | 5,000 | 1, and 1 more for each 15,000 | 160 | goods, food (half a tile) |

- A town builds only as many of each as it wants for its people, and the
  ones it lacks three times as readily as other buildings.
- Bigger shops need free land of their own. Once a year a town of 3,000 that
  lacks a mall, a complex or a hypermarket and has a site of old small
  houses of its own (kampung houses, terraces, shophouses, walk-up flats past
  their minimum age, nobody's protected ones) or bare land where the shop may
  stand, clears it and builds the shop, as developers buy up old houses.
- Their signs come in many chains' colours (red, green, orange, blue,
  yellow, dark green, purple, pink); no real brands.
- Shops are businesses for zoning and commuting (their people are jobs),
  and take food in the sub-tropics as well as goods, so deliveries reach
  them.
- Code: `city_houses.{h,cpp}` (house types 20-28, quotas, food,
  `GetCityShopToRedevelop`), `town_cmd.cpp` (probability, acceptance,
  `RedevelopForShop` once a year), sprites 290-298 and the redrawn mall
  (265-268) in `make_city_sprites.py`.

Tests (map 256 by 256, seed 42, counts at the yearly census):

| World | Supermarkets | Shopping complexes | Malls | Hypermarkets | Sites cleared |
|---|---|---|---|---|---|
| 2020 to 2030, normal growth (towns of 6,500 to 9,800) | 27 | 5 | 2 | 4 | 2 |
| 1960 to 2000, fastest growth (towns of 13,900 to 15,800) | 56 | 13 | 3 | 9 | 3 |

Screenshots: Rudinghall in 2000 (a hypermarket and its car park, a
shopping complex), supermarkets with their car parks close up.

### Town councils

Every town is run by a council, as in Malaysia; a new setting **Town
councils** (Settings, Environment, Towns, after Town budgets and
ordinances), on by default; it needs town budgets. Off works as before.

| Council | Becomes one | Builds |
|---|---|---|
| Majlis Daerah (district council) | every town at first | police and fire stations, clinics, schools, parks, hawker centres |
| Majlis Perbandaran (municipal council) | 4,000 people and £37,800 a year in taxes and licences (at normal costs) | also a hospital and a stadium |
| Majlis Bandaraya (city council) | 12,000 people and £108,000 a year, three years after becoming a municipality | also a university |

Real thresholds are 150,000 and 500,000 people (and RM20 and 100 million a
year); towns here are smaller. **Council names** (Settings, Environment,
Towns, after Town councils; a player's own setting, not saved with the
game): Malay (Majlis Perbandaran Funnford, with "a municipal council" beside
it in the windows) or English (Funnford Municipal Council, District Council,
City Council), as the owner asked. News already in the list keeps the
names it was written with. A new game's towns, and an old game's when it
is loaded, start with the standing they qualify for; after that they go up
each January, with news ("Gredinghall becomes a municipality: Majlis
Perbandaran Gredinghall", "City status for Gredinghall! Majlis Bandaraya
Gredinghall"). They never go down.

**Money.** The town's taxes (part M6) are the council's assessment tax.
Beside them:

- **Business licences**: 30% of the taxes at the normal rate, in the share
  of the town's people who work in shops and offices: 15% of the taxes in a
  town of 8,500, 24% in a city of 23,000 with more jobs than homes.
- **Services fund**: a tenth of the taxes goes into a fund for new
  services, out of the town's running costs (60% before: now 50% running,
  10% fund), so a town spends no more than before.
- A council builds from the fund, from what companies gave it, and from the
  town's reserves above half a year's taxes. When it needs nothing new, it
  keeps two years' worth in the fund and gives the rest back to the reserves,
  so the town can cut its taxes.

**Services.** Each month a council builds the service its town lacks most,
when it has the money: health and schooling up to 80% of the town reached,
police and fire 70%, parks 60%, hawker centres 50% (15 points more when it
has more than two years' taxes put by). A municipality of 6,000 people builds
a hospital instead of a clinic, a city of 12,000 a university instead of a
school; a municipality of 8,000 wants a hospital and a stadium of its own and
a city a university, however well its clinics and schools serve it. The
bigger ones only when it can pay for them within three years. It builds on
free land (grass, rough land, trees; not zoned for buildings or industry)
nearer to it than to any other town, where the new service reaches the most
of its people not yet reached by that kind of service (at least 100; for the
two by two ones the most people, at least 600). Its services fly dark blue
flags; it runs them out of its running costs (no upkeep for companies, no
rating for anyone); companies can't remove them. News: "Majlis Perbandaran
Gredinghall opens a new clinic".

**Co-funding.** A company can co-fund a council's next service from the
Budget window: it pays what the council lacks for it (at least a quarter of
its cost), the council builds it at once, the news names the company
("..., co-funded by ..."), and the town's rating of the company goes up 200
points, up to Very good. Companies still build services of their own too.

**Estates.** Councils run the estates of flats (walk-up flats, HDB slab and
point blocks, OpenTTD's blocks of flats; condominiums run themselves) as
Singapore's town councils run HDB estates:

- Flats pay service and conservancy charges (S&CC) into a sinking fund:
  20% of a resident's taxes at the normal rate a year.
- Routine upkeep (cleaning, lifts, repairs) comes out of it: 16% a
  resident, half a point more for each year of the estates' age above 20.
  The council tops it up from the town's reserves when the fund runs dry.
  The estates' condition heads for 85% when the upkeep is paid in full, down
  to 40% when nothing is.
- Every six years the council repaints its estates (25% a resident): the
  condition goes to 100% and **the blocks change colour**, all at once, with
  news.
- Estates 20 years old on average are renewed every fifteen years
  (neighbourhood renewal; 60% a resident, the government paying three
  quarters): condition 100%, happiness +2 and land value +5 in the suburbs
  for ten years.
- Municipalities and cities rebuild one of their oldest blocks (35 years, on
  land worth 50 or more) each year as a new HDB point block, as Singapore's
  selective en bloc redevelopment does.
- Condition counts for happiness, up to 3 points either way (70% neutral), in
  the share of the town's people who live in flats; land value -5 in the
  suburbs while estates are run down (below 50%).

**Yearly report.** Each January, as Singapore's town council management
reports: services (the share of the town reached by those the council may
build: 75% green, 45% amber, below red), estates (condition 75% green, 55%
amber) and finances (in credit with taxes at most 10% over normal green, in
credit amber, in debt red).

**Seeing it:** the town window has a line "Council: Majlis Bandaraya
Gredinghall (city council)". The Budget window is titled with the council's
name and has six more lines: the council and since when, its next standing,
licences and charges, new services (the next one and what it can spend, or
none needed), estates (people in flats, age, condition, last repainted and
renewed, fund) and the last report; then a **Co-fund the next service**
button beside Propose. The spending line shows the services fund.

- Saved: each town's council in chunk TCNL, with what the town had at the
  start of the month (so every player of a network game reckons alike). Old
  saves set up their councils in the first month.
- Script API: `ScriptTown.GetCouncilLevel`, `GetLicenceIncome`,
  `GetEstateCondition`, `GetCouncilServicesBuilt`,
  `GetCouncilPlannedService`, `CofundCouncilService`.
- Code: `src/council.{h,cpp}`, `council_type.h`, `council_cmd.h`,
  `src/saveload/council_sl.cpp`; hooks in `town_budget.cpp` (licences,
  monthly and yearly), `town_budget_gui.cpp`, `town_gui.cpp`,
  `city_services.cpp` (happiness, the council's flag colour),
  `environment.cpp` (land value), `city_houses.cpp` (repainting),
  `town_cmd.cpp` (rebuilding old blocks), `object_cmd.cpp` (objects owned
  by a town).

Tests (test AI CouncilTest logs the four biggest towns monthly and can
co-fund the biggest town's next service at a set month; land value on):

- Co-funding in the sixth month: Traningstone (8,564 people) lacked £12,549
  for a clinic; the company paid it, the council built the clinic at once
  (health 74% of the town), and the town rated the company Very good. In
  the next months the council built a school and a police station from its
  own money; happiness 40% to 64% within the year.
- 2020 to 2030 (towns of 5,800 to 8,600 at first), against the same game
  without councils:

| 2029 | Traningstone | Gredinghall | Sindworth | Tunnford |
|---|---|---|---|---|
| Services built by the council | 9 | 10 | 8 | 8 |
| Happiness with / without | 96 / 43 | 99 / 44 | 99 / 44 | 100 / 44 |
| Land value with / without | 74 / 56 | 72 / 55 | 78 / 54 | 77 / 52 |
| People with / without | 8,960 / 9,998 | 8,456 / 9,187 | 7,113 / 6,579 | 6,391 / 6,057 |

  The councils keep taxes at 100% for their first five years while they
  build (70% after), and their services take free land the dense towns would
  have grown onto, so the two biggest towns end 8-10% smaller and the two
  smaller ones 5-8% bigger.
- 1960 to 2010 with the fastest growth (about 20 towns): the councils built
  165 services (39 parks, 26 schools, 21 clinics, 19 hawker centres, 18
  police and 18 fire stations, 9 hospitals, 8 universities, 7 stadiums); 9
  towns became cities at 12,000 people; 13 estate renewals and 14 old blocks
  rebuilt. Against the same game without councils:

| 2009 | Gredinghall | Keningbury | Rudinghall |
|---|---|---|---|
| People with / without | 23,003 / 16,209 | 20,542 / 18,383 | 25,922 / 19,138 |
| Happiness with / without | 99 / 39 | 100 / 38 | 97 / 42 |
| Land value with / without | 80 / 52 | 81 / 49 | 75 / 53 |
| Taxes with / without | 70% / 90% | 70% / 90% | 70% / 80% |

- Big cities run out of free land for more services; then their reserves
  pile up (£300,000 to £750,000 by 2009) with taxes at the 70% floor. Step 3's
  states could take a share.
- With budgets (and so councils), commuting and the new buildings off, the
  baseline runs give identical results; 99/99 unit tests; all 46 test saves
  (with the new `city-shops-demo.csav`, from before councils, and
  `city-council-demo.csav`) load, save and reload.
- Screenshots: Gredinghall in 2010 (23,003 people; the council's hospital,
  university and stadium, a mall, a hypermarket, supermarkets); its town
  window with the council line; its council's Budget window, with Malay and
  with English names.

## Done: M4 part 3, step 3: states, elections, federal territories and regions

The owner asked for towns grouped like Malaysia's states (negeri), each with
its own government in charge of the councils and with elections; one or two
cities as federal territories (wilayah persekutuan); and a land across the
sea, or behind a land border, run like Sabah and Sarawak: its own land laws
and immigration, and no land reclamation (tambak tanah) toward its shores.
The owner's calls: states made automatically, named after their capital,
renamable; elections judged only on the service voters get, with made-up
platforms.

Background for the regions: under the Malaysia Agreement 1963, Sabah and
Sarawak keep their own land laws (the Sabah Land Ordinance and the Sarawak
Land Code, not the National Land Code of the peninsula), which recognise
native customary rights over land natives have long used, and their own
immigration control under the Immigration Act 1959/63: other Malaysians may
visit, but need a pass to work there. Land reclamation is the kind of thing
neighbours argue over (Malaysia and Singapore took theirs in the Johor Strait
to the tribunal of the Law of the Sea in 2003).

### States, federal territories and regions

- Setting **States and elections** (Environment, Towns; on by default; needs
  town councils). The states form in the first month, in new games and in
  older saves alike.
- Lands: the map is read in cells of 4 by 4 tiles. Land joined by land is one
  land; the sea splits lands, and so does a neck of land at most 8 tiles wide
  (16 on maps over 256 tiles, 24 over 1,024), where a land border runs. The
  main land is the one with the most people.
- The biggest city on the main land becomes a federal territory, Wilayah
  Persekutuan Funnford (Federal Territory of Funnford), run by Dewan
  Bandaraya Funnford (Funnford City Hall) with a city council's powers and a
  federal grant of a tenth of its taxes into its services fund. Maps of 512
  by 512 or more with 20 towns on the main land get a second one: the biggest
  city a quarter of the map away from the first.
- State capitals are the biggest towns, spread apart: about one for every six
  towns, at most 13 on the main land and 3 in a region. Every other town joins
  the nearest capital on its land; towns on small islands the nearest one
  across the sea. A state is called Negeri Funnford (Funnford State) after its
  capital; players can rename it (only the server in network games).
- Another land with two towns or more, at least 1,024 tiles and a sixteenth of
  the main land, is a region run like Sabah and Sarawak, with states of its
  own.
- The map of states covers all land: each cell goes to the state of the
  nearest town along its land; a federal territory covers only its city and
  the state around it the land beyond; land without towns goes to the nearest
  state. Worked out when the states form, each January and when a town is
  founded; saved, so every player in a network game has the same.

### Governments and elections

- Each state elects its assembly (Dewan Undangan Negeri) every five years: a
  seat a town and one more for each 10,000 people; the platform with the most
  seats governs. General elections in May elect parliament (every town's
  seats) and the assemblies of the states on the main land; the federal
  government, which also runs the federal territories, is the platform with
  the most seats in parliament. Regions vote on their own, in September. The
  first general election is in May of the year after the states form, the
  regions' a year later; until then caretakers spend nothing on a platform.
- Voters judge only what they get in their town: traffic jams (the share of
  its roads that crawl), transit use (the share of commutes by transit),
  passenger satisfaction, dirty air, city services lacking, run-down estates,
  taxes, and in small towns how much they want to grow. Three made-up
  platforms (no real parties):
  - Transit First appeals to people who ride and like it, to drivers stuck in
    traffic and to people breathing dirty air;
  - Roads and Growth to people who drive, to drivers stuck in traffic, to
    small towns and to people with high taxes;
  - Homes and Services to people short of city services or living in run-down
    estates, and to people with high taxes.
- Each seat goes to the platform that appeals most, give or take a little
  chance. A government whose voters rate their service better than at its
  election gains up to 10 points in every seat; one that made it worse loses
  them. Companies can't campaign or pay: only their service moves votes.
- What a government does with its money (a tenth of its towns' taxes a year,
  up to double with money beyond six months' grant in its treasury):
  - Transit First: its towns' transit budgets get 10% of their taxes on top
    (up to 20%), for more contracts, free lines and passes; towns are readier
    to bring in a transit fund, a car-free centre, transit-oriented
    development, parking limits and low-emission zones.
  - Roads and Growth: towns grow 15% faster (up to 30%), land premiums are
    halved, and each quarter new buildings are funded for three months in one
    town in turn; towns are slower to limit cars.
  - Homes and Services: the councils' services and estates funds get 10% of
    their towns' taxes (up to 20%); estates are kept up to 95% instead of 85%;
    towns build estates of flats half again as readily.

### Money

- States hold the land: a company pays a land premium for each tile of land
  (grass, rough land, fields, trees, a house) it clears, builds on or
  reshapes: 40% of the price of a piece of railway track on land worth 50, in
  proportion to what the land is worth (a fifth of that on cheap land, up to
  twice in a city centre); twice that in a region; half under Roads and
  Growth.
- The federal government gives each state a tenth of its towns' taxes a year.
  Each January a state takes a third of what a council keeps beyond two years'
  taxes: the reserves rich big cities piled up at the 70% tax floor (step 2).
- A treasury keeps six months of the grant; money beyond it (land premiums,
  levies, caretaker years) makes the platform stronger, up to double.

### Regions run like Sabah and Sarawak

- Their own land laws: land premiums twice as high; forest and farmland in a
  town's area are native customary land, which the town gives up only to
  companies it rates at least Mediocre. Cutting trees lowers the rating, so a
  company can clear only so much before it has to win the town back.
- Their own immigration: nobody commutes across a region's border (work there
  needs a pass), and companies can't found towns there.
- No land reclamation: no company may turn sea into land within 12 tiles of
  the region's shores (on a narrow strait that reaches the other shore).
  Bridges, docks and ships are fine.
- Changed in 0.2.0 (the owner's call, 2026-11-09): commuting across the
  border, new towns and reclamation are allowed by the region's work passes
  and permits instead; see "Done: region work passes and permits (0.2.0)".

### Windows, map, names

- Towns menu, **States**: the federal government, parliament if elected today
  and the federal grants; the states and territories (colour, towns, people,
  government and seats, next election); the selected one's capital,
  government, policy and its strength, voters' concerns, assembly if elected
  today, treasury and last year's money, land premium, and a region's laws.
  Buttons: Rename, Go to capital, Map of states.
- Town window: a line for its state and who governs it, its council as City
  Hall in a federal territory, and a **State** button.
- Map: a new view **States** (the button with the map icon): each state in its
  colour, federal territories white, regions hatched, regions' waters dark
  blue, borders black.
- Setting **Council and state names** (was Council names): Malay (Negeri
  Funnford, Wilayah Persekutuan Funnford, Dewan Bandaraya Funnford) or English
  (Funnford State, Federal Territory of Funnford, Funnford City Hall).
- News: the states formed, each region, each state's election, general
  elections.
- Script API: `ScriptTown.GetState`, `IsFederalTerritory`, `IsInRegion`,
  `GetStateGovernment`, `GetStateTreasury`, `SetStateGovernment` (game
  scripts, for scenarios); `ScriptTile.GetLandPremium`.
- Saved in chunks STAT (states), SFED (federation), SMAP (map of states) and
  TSTA (each town's state). Code: `state*.{h,cpp}`,
  `saveload/state_sl.cpp`; hooks in `landscape.cpp` (premium, native land),
  `water_cmd.cpp` (reclamation), `town_cmd.cpp` (founding, growth, house
  choice), `commuting.cpp`, `transit_authority.cpp`, `town_budget.cpp`,
  `council.cpp`, `smallmap_gui.cpp`, `town_gui.cpp`; sprite 299 (map icon).

### Tests

- Formation: on the usual 256 by 256 test map (2020), 3 states and a federal
  territory; with 60% sea (seed 3), a main land of 8 towns (a state and the
  federal territory of Great Brunthill), a region of 4 towns across the
  strait (Negeri Dudingbury) and an island town that joined the main land's
  state. Loading step 2's council save forms 3 states and Rudinghall as a
  federal territory.
- Land premiums (2020 prices): £34 a tile in Great Brunthill's centre (land
  worth 58), £6 in the country (worth 10); a piece of track on grass cost £252
  with a £12 premium; £80 in Dudingbury's centre (worth 67, in a region).
- A company's land reclamation was refused next to Dudingbury and at Great
  Brunthill across a narrow strait (within 12 tiles of the region). Native
  customary land: after nine trees cut near Dudingbury, its rating of the
  company fell to 185 and it refused more.
- Platforms (a test game script puts one in power in every state from March
  2020; mid-2023, one bus company):

| Platform | People | Biggest town's transit budget | Estates |
|---|---|---|---|
| Caretaker | 55,487 | £37,851 | 85% |
| Transit First | 55,544 | £53,901 | 85% |
| Roads and Growth | 59,488 (+7%) | £34,799 | 85% |
| Homes and Services | 56,288 (+1%) | £38,222 | 95% |

- Voting: in seven-year games with one bus company serving one city (6% of
  its commutes by transit), Roads and Growth and Homes and Services won every
  seat; villages without services or transit split between them. By the
  voters' sums, Transit First wins a congested city once about a fifth of its
  commutes go by transit.
- With town budgets (and so councils and states), commuting and the new
  buildings off, the baseline runs give identical results; 99/99 unit tests;
  all 47 test saves (with the new `city-states-demo.csav`: the 60% sea map in
  November 2022) load, save and reload.
- Screenshots: the map of states; the States window (the federal territory,
  the region, English names); the federal territory's town window; the
  refusal to reclaim land in a region's waters.

## Done: the name Wira Bandar (a fork of OpenTTD)

The owner picked the name Wira Bandar ("town hero") for this game and for a
future phone game (in the fork-future session), then said to rename the PC
game here, with two calls: its own folder, with the settings and saves
copied over from OpenTTD's, and the GitHub repo renamed to `wirabandar`. And:
wherever it is named, say it is a fork of OpenTTD, so it never claims to be
made from scratch.

- Licence: OpenTTD is free software under the GNU GPL version 2, which lets
  anyone change it and share it under another name, as long as the result
  stays under the GPL with its source code available, and the copyright
  notices and credits stay. Wira Bandar keeps OpenTTD's licence (COPYING.md),
  credits (About window) and copyright lines; its source is the public repo.
  A name of its own also keeps people from taking it for OpenTTD or reporting
  its bugs to the OpenTTD team.
- In the game: the window title "Wira Bandar PC (version) (a fork of
  OpenTTD)"; a WIRA BANDAR wordmark over the main menu instead of OpenTTD's
  letters (sprite 300); the menu's caption "Wira Bandar PC, a fork of
  OpenTTD" and version line; the About window ("About Wira Bandar PC": a fork
  of OpenTTD, OpenTTD's code with changes, not made from scratch, OpenTTD's
  credits, the source link, OpenTTD's website and copyright); "Wira Bandar"
  where text means this game (quit, full screen, missing graphics and the
  like). Text that means OpenTTD itself keeps the name: its content service,
  NewGRF version checks, the help window's website, comparisons in help
  texts. The other languages lost their translations of the renamed lines
  (1,394 lines in 64 files), so those lines show in English in every
  language; the note on missing strings now says the city and transit
  features are in English only.
- Windows: `wirabandar.exe` with its own icon (a skyline and a red bus,
  `os/windows/make_wirabandar_icon.py`) and file details ("Wira Bandar PC, a
  fork of OpenTTD"); the zip `wirabandar-windows-x64.zip` from the release
  `latest`. Test builds on Linux keep the program name `openttd`, which the
  test kit expects.
- Folder: `Documents\Wira Bandar` instead of `Documents\OpenTTD`. The first
  time it starts (while its folder has no `openttd.cfg` and OpenTTD's has
  one) it copies the settings files (`openttd.cfg`, `hotkeys.cfg`,
  `windows.cfg`, `private.cfg`, `secrets.cfg`, `favs.cfg`, `hs.dat`) and the
  `save` folder; it moves and deletes nothing, and keeps what is already
  there. OpenTTD's folder and its `content_download` stay search paths
  (searched last), so the graphics, NewGRFs, AIs, game scripts and scenarios
  installed for OpenTTD are still found; so does OpenTTD's install folder
  (`C:\Program Files\OpenTTD`), where players put NewGRFs too: on the owner's
  first start, 30 of their NewGRFs were there and showed as missing until this
  was added. Unit test `CopyOpenTTDFolder`; the new search places were checked
  on Linux by pointing them at a test folder (a NewGRF and a packed one found).
- OpenTTD's opt-in survey is off: it went to OpenTTD's servers, and is for
  OpenTTD's own builds.
- Internals keep their names (source files, `openttd.cfg`, the `.csav`
  saves), so saves load as before and the fork can still take OpenTTD's
  updates.

## Done: the in-game tutorial

The owner asked (2026-11-08) for an interactive tutorial inside the game, for
new players and for players who load an old save, that they can leave at any
step and open again at any time.

- **Approach: a window built into the game** (`src/tutorial_gui.cpp`), not a
  game script. A game script is picked in World Generation and lives in that
  save, so it can't help in old saves, nor in games that use another game
  script. The window works in any game: it only looks at the game and its
  windows, and changes nothing in them, so saves stay as they are.
- **What the player sees:** a small window ("Tutorial: chapter, step N of M")
  with what to do, using the labels on screen, and a status line (waiting,
  progress such as "Bus stops built: 1 of 3", or "Done!"). The button to
  click flashes, with the highlight game scripts use
  (`Window::SetWidgetHighlight`). The step moves on by itself once it is done
  (a window opened, stops built, buses bought, a line numbered, fares
  changed). **Next** skips a step, **Back** goes back (and then doesn't move on
  by itself), **Contents** lists the chapters, and **Exit tutorial** or the
  window's close box leaves it at any step.
- **Placement:** it stays above ordinary windows and above World Generation,
  Game Options and Load Game (the z-priority of the progress window). When the
  step or the flashing buttons change, it moves out of the way of the window
  it points at, of construction toolbars and of those big windows; a player
  who moves it keeps it there until then.
- **Chapters:** Start a game (4 steps, in the main menu; goes on into the
  next), Your first bus line (18 steps, from the guide's tutorial "Your first
  city bus network"), What's new for OpenTTD players (11 steps, from the
  guide's "Your first game" and "Saves").
- **When it appears:** offered on the first start (a welcome box next to the
  main menu); offered once when an OpenTTD save (any `.sav`, not `.csav`) is
  loaded; **Tutorial** in the main menu and in the Help menu (the ? button) at
  any time. While the player is in it (and hasn't left it), it comes back by
  itself at the same step in the next game and at the main menu. Steps that
  need a game say so in the main menu and flash New Game and Load Game, then
  Generate or Load.
- **Saving:** client settings in `openttd.cfg`, `[gui]`: `tutorial_seen`,
  `tutorial_seen_old_save`, `tutorial_dismissed` and `tutorial_step` (the
  step's name, so steps added later don't shift it). Nothing goes in saves.
- **Engine changes:** `WindowClass::Tutorial` (end of the list) and its
  z-priority in `window.cpp`; `WID_SGI_TUTORIAL` (end of the main menu's
  widgets); Help menu item 2 (the items after it moved down one); calls at the
  end of `LoadIntroGame` (after the title game, whose loading resets the
  windows), after a new game and after a loaded game (`openttd.cpp`). The
  close box is the window's own button drawn like one: a real close box would
  also be "pressed" when games start or load, as `UnInitWindowSystem` closes
  every window. The window has `NoClose`, so "close all windows" leaves it.
- **Text:** `english.txt` (`STR_TUTORIAL_*`, `STR_ABOUT_MENU_TUTORIAL`,
  `STR_INTRO_TUTORIAL`, `STR_INTRO_TOOLTIP_TUTORIAL`).
- **The name and the goal** (the owner's wording, approved 2026-11-09 with
  the guides session): the welcome page says what Wira Bandar means ("town
  hero": wira is Malay for hero, bandar for town) and the game's goal (a city
  that moves people, not just cars); the About window has the short version
  in two lines after the fork credits (`STR_ABOUT_NAME_MEANING`,
  `STR_ABOUT_GOAL`); the chapters' last steps point at
  wirabandar.techmavie.digital/about, which has the full version.
- **Tested** on the Linux build under Xvfb (2560x1600, 200%): the welcome
  offer; Start a game (New Game, climate, the year raised from 1995 to 2000
  moves it on, Generate); the whole bus chapter in the new game (three stops,
  a depot, a Kenari City 12, orders, three clones, start, the list grouped by
  lines, number G1, a month at fast forward, frequency and even spacing, half
  fares, departures, the transit authority), Finish and the chapters; What's
  new in a new game and in `vanilla-15.3-buses.sav` (offered on load); left
  with the close box, back at the main menu it stays closed, reopened with the
  main menu's Tutorial button at the same step (with the needs-a-game note),
  reopened with Help, Tutorial; the settings saved on quitting and the step
  resumed after a restart; the road tools step flashes the five new buttons.
  Unit tests pass; the demo saves load, save and load again (`compat-test`).
- **Found while testing:** in Go To mode, clicking a stop's name sign aims at
  the tile under the sign, not the stop (as in OpenTTD), so the step says to
  click the stop itself. Road vehicles and Finances open a company menu first;
  their steps say "choose your company".

## Done: fixes from writing the guides (0.1.1)

Five writers played the build of 8 November (430892a9ae) while writing the
player guides and reported 15 problems. Thirteen were real and are fixed;
each was checked in the game or with a test AI. Wira Bandar now has a
version of its own (see `docs/wirabandar-changelog.md`): this batch is
0.1.1, the first release under the name was 0.1.0.

- Texts: council news and the Budget window name the building ("opens a new
  clinic", "a police station next"), "1 year old" in the estates line, and
  towns under 500 people say they never count commuters.
- Windows: the Budget and Transit windows open as wide as their longest
  line and widen when one grows; the Tolls window fits its lines; the line
  map can be made as short as three stops; the line window updates after
  its own buttons; the Bus Priority and Tolls toolbars open under the Road
  Construction toolbar.
- Councils start right after loading a savegame from before them (and when
  switched on), instead of showing district councils until the next month.
- Lines: "which needs N vehicles" leaves out the time vehicles wait for
  their turn at the first stop with even spacing on, held there or queued
  behind (a new monthly statistic, saved with the line); 12 buses held to a
  340-tick gap said 12, now 8-10, against 8 without holding.
- The main menu's "missing 301 sprites" notice is gone (Wira Bandar's own
  sprites are not counted); the Air pollution and Land value tints leave
  water untinted.
- Regions: docks and ship depots may be built in a region's waters; native
  customary land goes to companies the town rates Mediocre or better, as
  its message says; the States window lost a "paused" note that could
  never show.
- Not changed: a company can't found a town at a random location (only the
  scenario editor, with no company, can), so the region check can't be
  skipped that way; the console's `zoomto 1` works (a new game at 200%
  interface size already starts at that zoom).
- Tests: 96/96 unit tests; all 47 test saves load, save and reload; test
  AI `HoldNeed` (vehicles needed with even spacing) and `StateTest` (now
  also docks and ship depots in a region's waters) in the test kit.

## Done: region work passes and permits (0.2.0)

The owner's call (2026-11-09): a region run like Sabah and Sarawak should
decide who comes in rather than shut people out, as the real states do with
immigration checkpoints, passes and permits. In Sabah and Sarawak other
Malaysians enter with their identity card for a 90-day social visit pass
(IMM.114), need a work permit (from the state's immigration and labour
departments) to work there, and employers need a licence to employ
non-residents; the state may bar anyone or limit their stay (Immigration Act
1959/63, sections 64-66). The Merapok-Sindumin checkpoint between Sarawak and
Sabah can take one to two hours to pass at busy times. Reclamation (Tanjung
Aru in Kota Kinabalu, for one) is approved by the state, which then has
developers build on it, partly as joint ventures.

- Work passes: people may live on one side of a region's border and work on
  the other. The region gives work passes to a share of the people from
  elsewhere who would work in its towns: 50% plus half its labour need
  (from -100, far more workers than jobs, to 100, far more jobs than
  workers, by the homes and jobs of its towns, as zoning counts them), 20
  more under Roads and Growth and 20 fewer under Homes and Services, at
  least 20%. Those without one work on their own side. Leaving a region
  needs no pass. Every commute across the border passes the checkpoint: 15
  minutes more by car and by transit.
- Permits for new towns founded by companies and for sea turned into land
  in a region's waters: the council of the region's town nearest the place
  must rate the company Mediocre or better (as for native customary land).
  When the region wants to grow (its towns lack workers, labour need 20 or
  more, or Roads and Growth governs) the state co-funds it and pays half,
  from its treasury as far as it can and the federal government the rest;
  otherwise the company pays the full cost and a permit fee of 25% to the
  state treasury. Docks and ship depots need no permit.
- The States window shows a region's work passes, its jobs and workers, the
  checkpoint and its permit terms; the map legend says "reclamation by
  permit"; the news when regions form names the passes and permits.
- Not saved: the work passes and terms are worked out from the towns when
  needed, so savegames are unchanged and every player of a network game
  gets the same.
- Code: `state.cpp` (`GetRegionWorkPassPercent`, `GetRegionPermit`,
  `ApplyRegionPermit`, `GetCommuteWorkPassPercent`), `commuting.cpp` (the
  checkpoint and the passes per job cell), `town_cmd.cpp` (founding),
  `water_cmd.cpp` (reclamation), `state_gui.cpp`.


Sizes are rough: S = one session, M = a few, L = many, XL = a project in itself.

| # | Feature | What it means in OpenTTD | Size | Notes |
|---|---|---|---|---|
| M2 | Traffic realism | Done: give way, light settings, traffic map and overlay, amber, routing around jams. | — | |
| M3 | Zoning | Done: zones, town growth follows them, homes/jobs line in the town window, zones in land info. | — | |
| M4 | City services and happiness | Part 1 done: police, fire, clinic, school and park with coverage, happiness, growth, ratings and upkeep. Part 2 done: Malaysian and Singaporean town buildings, from kampungs and shophouses to HDB blocks, condominiums and towers, by year, zone, town size and land value. Part 3 step 1 done: hospital, university, stadium and hawker centre that serve a whole town and draw commuters and passengers. Step 2 done: supermarkets, shopping complexes, malls and hypermarkets; town councils (district, municipal, city) that collect licences, build services (companies can co-fund) and run estates as Singapore's town councils do, with a yearly report. Step 3 done: states around the biggest cities with assemblies and a parliament elected on the service voters get (Transit First, Roads and Growth, Homes and Services), federal territories run by City Halls, land premiums, and regions across the sea or a land border run like Sabah and Sarawak (own land laws and immigration, no reclamation in their waters). | — | The owner's calls: councils build, companies can too; states made automatically; elections judge service only. |
| M5 | Town car traffic | Done: commuters drive between homes and jobs along real roads, in their town and up to four towns around, or take transit when it is quick enough; traffic follows their routes, also between towns; transit takes cars off them (see M5, commuting, and part 2). | — | |
| M6 | City budget and ordinances | Done: towns collect taxes and spend them on transit, running the town and ordinances; they set their own tax rates and bring in ordinances (transit fund, parking limits, low-emission zone, car-free centre, tree planting, transit-oriented development); companies propose them. | — | The owner's call: towns decide, companies lobby. |
| M7 | Land value and pollution | Done as transit part 14: traffic, diesel vehicles and industry pollute; parks, services and good stations raise land value; map views, tints and lines show both; towns build to fit. | — | |
| M8 | Utilities | Power and water networks. | XL | Probably skip, or keep it light (arctic/tropic towns already need food/water). |

Recommended order: M4 is complete. Next is the owner's call: renaming the
PC game to Wira Bandar (decided with the phone game), M8 only if wanted, or
new ideas.

## Getting it onto the owner's computer (Windows)

- The owner plays on Windows. The code goes to the GitHub repo
  `hithereiamaliff/wirabandar` (a fork of OpenTTD), and
  `.github/workflows/city-windows.yml` builds a 64-bit Windows zip on every
  push to `city`: a workflow artifact, plus the rolling `latest` pre-release
  as a stable download link
  (`releases/download/latest/wirabandar-windows-x64.zip`). Since 0.2.0 the
  zip is named after the version, `wirabandar-<version>-windows-x64.zip`
  (`WIRA_BANDAR_VERSION` in `src/rev.h`), and every version also gets its own
  release, tag `v<version>`, with its part of `docs/wirabandar-changelog.md`
  as notes; `latest` has the versioned zip and the same file under the old
  name, so old links keep working. Libraries come from vcpkg, cached in
  the Actions cache (no packages published). The exe isn't code-signed, so
  SmartScreen asks once ("More info", "Run anyway").
- Unzip anywhere and run `wirabandar.exe`. It keeps its own folder,
  `Documents\Wira Bandar`, copies the settings and saves from
  `Documents\OpenTTD` the first time, and still finds the base graphics and
  downloaded NewGRFs there; its own saves are `.csav` (see ground rules).
- Linux build exists on the VPS for testing.

Savegame locations (files end in `.sav`; autosaves are in `save/autosave`):

| OS | Folder |
|---|---|
| Windows | `Documents\OpenTTD\save`; Wira Bandar's in `Documents\Wira Bandar\save` |
| macOS | `~/Documents/OpenTTD/save` |
| Linux | `~/.local/share/openttd/save` |

## Open questions for the owner

1. Can you share one of your saves, so it can be tested before you play it?
2. Which milestone next? Open: M4 part 3 is done (states were the last
   step); the rename to Wira Bandar is ready to do.
3. A name for the fork: Wira Bandar (PC: "Wira Bandar PC, a fork of
   OpenTTD"); done 2026-11-08.
