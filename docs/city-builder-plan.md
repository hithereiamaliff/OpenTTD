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
