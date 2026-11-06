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
