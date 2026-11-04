# Transit operator gameplay: research and proposal

Research done on 2026-11-04 for the owner's requests to add City Bus Manager
style gameplay (and a train/metro equivalent) to the fork; to model real public
bus funding and contracting from Malaysia and Singapore; to add town traffic,
pedestrians, bus lanes, busways and BRT, expressways, tolls and interchanges;
and newer diesel and electric buses with charging and better depots. This file
keeps the findings, numbers and sources for balancing; the proposal at the end
is not built yet.

Owner's decisions so far:

- Staff: Claude decides (proposal: light budgets, optional).
- Rush hours: both, as an option (steady demand or a city clock with peaks).
- Fares: consider all options.
- Contracts: both, in normal play and as optional goals.
- Multiplayer: mostly single player for now.

## City Bus Manager

PeDePe, published by Aerosoft; early access 10 Nov 2022, full release
25 Jun 2024; console version May 2025. Real cities from OpenStreetMap.

- Lines: stops on real streets; a line runs from a first to a last departure
  at a fixed interval. Halving the interval doubles buses and drivers needed.
- Shifts: trips are bundled into shifts, a shift goes onto a bus, a driver
  works it. Shifts can't overlap; about 8 hours is fine, more raises stress
  and complaints; research unlocks shifts up to 20 hours.
- Staff: drivers, mechanics, office workers, ticket inspectors. Staff needs
  (kitchen, toilets, rest) affect efficiency.
- Depot built room by room: parking, garages, wash bays, parts store, offices,
  kitchen, toilets, a service centre selling tickets. Repairs need spare parts;
  maintenance is scheduled between shifts.
- Passengers rate trips on punctuality, ticket price and cleanliness.
  Single, day and weekly tickets; inspectors cut fare dodging.
- Research tree (ticket machines, passenger app, work schedules), random
  events (rail replacement), scenario mode with goals, sandbox.
- Add-ons: E-Bus & Green Energy (charging at depots and stops, solar,
  electricity contracts, CO2 bonuses); Holiday Resorts & Sightseeing (hotel
  shuttle and sightseeing route types, contracts from attractions, day
  ticket); School Bus (school route type, morning and afternoon runs, student
  ticket, school hours, punctuality and satisfaction ratings from schools);
  International Bus Pack.

Sources: https://store.steampowered.com/app/1364210/City_Bus_Manager/ ,
https://steamcommunity.com/app/1364210/discussions/0/6895657216186498044/ ,
https://steamcommunity.com/app/1364210/discussions/0/690868475627886163/ ,
https://www.aerosoft.com/en/shop/move/building/4219/city-bus-manager-e-bus-green-energy ,
https://www.aerosoft.com/en/shop/move/building/4713/city-bus-manager-school-bus-student-transportation ,
https://www.aerosoft.com/en/shop/move/building/4415/city-bus-manager-holiday-resorts-sightseeing-buses

## Train, metro and tram games

No train or metro version of City Bus Manager exists. Mechanics worth
borrowing:

- NIMBY Rails: weekly repeating timetables (per line, train, station), train
  shifts from one order template, demand by time of day and week, platform
  capacity. v1.20 removed its satisfaction system as "half broken" and moved
  to per-km fares, a transfer penalty, 8 passenger categories and commuters
  who travel home again (https://carloscarrasco.com/).
- Subway Builder: commuters from census homes and workplaces, routed over the
  real timetable; walking counts 1.39x and waiting 1.37x of riding; transit
  competes with driving on time and cost
  (https://www.subwaybuilder.com/simulation).
- Mini Metro: overcrowded stations are the failure signal.
- Railway Empire 2: staff as budgets, not people; crew per train (3
  engineers, 3 heaters, 4 conductors, 5 security) for an 8-hour day; quality
  of work and filled jobs from spending
  (https://www.magicgameworld.com/railway-empire-2-guide-to-personnel-and-tasks/).
- Rail Route: contracts that pay only while trains run on time.
- Cities: Skylines II: ticket price, vehicle count and operating hours per line.
- Bus Simulator 21: full-time and peak-time buses on a timetable.
- Small 2025-2026 indies (Metro. Manage the Underground, Shane's Trains,
  Metro Architect, Steam & Steel): service intervals, rush hour, crews,
  cleanliness, strikes.

## OpenTTD today

Vanilla 16 (checked in our source):

- Shared orders act as lines; groups have names and liveries; timetables with
  autofill, start dates, "spread vehicles evenly"; the Unbunch depot order
  spaces vehicles by round trip / vehicle count.
- Wallclock timekeeping (14.0) with timetables in seconds; no time of day.
- Cargodist gives passengers destinations and transfers (off in ~90% of play).
- Payment is a fixed formula: rate x distance x time decay. No fare setting.
- Station rating: speed and age of the last vehicle, time since pickup,
  waiting cargo, statue, adverts.
- Road types can restrict vehicles only per whole tile; no bus lanes.

JGR's patch pack adds scheduled dispatch (clock-face departure slots),
departure boards, auto separation, routing restrictions, realistic braking,
signals in tunnels, improved breakdowns. Nothing on staff, fares,
satisfaction, crowding or rush hours.

Gaps: lines as objects (number, colour, map), fares, staff, satisfaction,
time of day, analytics (ridership per line, on-time %), bus priority, depot
capacity, contracts and progression, a real metro underground.

Company finances: `yearly_expenses` is saved as 3 x 13 values, so new expense
categories would break old saves. Contract money has to be booked under an
existing category (for example road vehicle income) and broken down in a new
window instead.

## Singapore

Money in SGD.

- Bus Contracting Model (from 1 Sep 2016): the government owns buses, depots
  and fleet systems and keeps all fares; operators get a fixed fee (their bid,
  indexed for inflation, wages and fuel, adjusted when LTA changes mileage)
  plus or minus performance payments. Operators hire drivers, maintain buses
  and run depots and interchanges.
- 14 packages, each one depot plus 1-4 interchanges, 300-500 buses, 17-40
  routes. Contracts 5 years plus 2-5 years extension for good performance.
  Tampines (2025): quality-price weighting 60:40, Go-Ahead won with the best
  quality at about $646m; SBS Transit's lowest bid ($596m) lost. Roughly
  $105-130m a year per package, about $320k per bus per year.
- Reliability framework: frequent services judged on excess wait time
  (actual minus scheduled wait), infrequent ones on punctuality (2 min early to
  5 min late). Trial payments per 0.1 min of change against the route's
  baseline: $2,000/$1,300 bonus/penalty for under 10 buses, $4,000/$2,600 for
  10-19, $6,000/$4,000 for 20+; neutral band of 0.1 min. Under BCM, net
  incentives ranged -0.1% to +5.9% of fees (2017-2025); 84.9-100% of services
  met their baselines.
- Standards: every 15 min or better at peak (half every 10 min, feeders every
  8 min); extra buses when peak-hour loading stays above 80-85%.
- LTA plans routes from fare-card and GPS data; operators only run them.
- History: before 2016 operators owned buses and kept fares under licences
  (80% of services every 10 min at peak, max 95% loading, fines up to $100k a
  month). BSEP (2012-2017): $1.1b, 1,000 government-funded buses, 80 new
  routes. BCEP (2024-2032): $900m.
- Fares (Public Transport Council): one distance fare for the whole journey
  with up to 5 transfers, 45 min between rides, 2 h first to last boarding;
  same service number doesn't count as a transfer. Adults $1.28-2.57, students
  $0.52-0.78, seniors/disabled $0.69-1.07. Monthly passes: adult $122, senior
  $55, students $24-81. Yearly fare cap formula from inflation, wages and
  energy.
- Bus priority: part-day and full-day bus lanes (211 km in 2019), Mandatory
  Give-Way to Buses at about 330 stops, "B" signals giving buses a head start.
- Scale: about 5,800 buses, 356 routes, 3.84m rides a day (2025). Fares cover
  about half the bus cost; 345 of 356 routes lost money even before COVID.

Sources: https://www.mot.gov.sg/news-resources/newsroom/evaluation-of-bids-for-tampines-bus-package/ ,
https://www.mot.gov.sg/news-resources/newsroom/breakdown-of-bus-operator-key-performance-indicators--incentive-payments-and-penalties-since-introduction-of-bus-contracting-model/ ,
https://www.ptc.gov.sg/fares/distance-fares-and-transfer-rules/ ,
https://landtransportguru.net/bus/bus-contracting-model/ ,
https://eprints.whiterose.ac.uk/106500/ ,
https://www.lta.gov.sg/content/ltagov/en/who_we_are/our_work/public_transport_system/bus/bus_priority_schemes.html

## Malaysia: federal

Money in MYR.

- BAS.MY (Ministry of Transport, run by APAD; formerly the Stage Bus Service
  Transformation): gross-cost contracts, now mostly 5 years, paid monthly "based
  on scheduled trips and distances, regardless of passenger numbers"; the
  government keeps the fares. Over RM200m a year federal spend (2026). Roughly
  RM330-420k per bus per year. Operators own the buses; contracts require new
  buses with GPS, CCTV and e-ticketing. Penalties for missed trips,
  punctuality and bus age: 3.3-4.0% of contract revenue (Handal Indah). 11
  networks (Kangar, Seremban, Ipoh, Kuala Terengganu, Johor Bahru, Melaka,
  Kuching, Kota Setar, Kota Bharu, Kuantan, Kota Kinabalu); 16.2m riders in
  2025. Fares RM0.94 for 2 km plus RM0.094 per km; 30-day pass RM30 for
  citizens; concession card for students, seniors, disabled. Expired contracts
  left some cities on weaker interim service.
- Prasarana / RapidKL: owned by the Finance Ministry, operating deficit about
  RM1.1bn a year, government buys the fleet. My50: riders pay RM50 for 30 days
  (citizens), the government adds about RM100 per pass from a fixed pot (2025
  shortfall about RM100m, mostly absorbed by Prasarana). MRT feeder buses owned
  by MRT Corp, run by Prasarana.
- Rapid On-Demand: shared 10-12 seat vans booked in an app, zones within about
  5 km of rail stations, 6am-11:30pm. Over 20,000 riders a day by Dec 2025; RM2
  a trip since Feb 2026 (each trip cost about RM5 against an RM1 fare in 2025).
  Weak feeder routes were replaced by vans in Jan 2026; in Oct 2026 two zones
  trialled fixed-schedule van loops without booking.
- Interim Stage Bus Support Fund (rural routes, since 2012): net-cost top-ups;
  operators keep fares; missing 80% punctuality on any route cancels the
  subsidy for all that operator's routes.
- Fleet diesel at a subsidised RM1.88 a litre for licensed buses.

Sources: https://bas.my/pasperjalanan.php ,
https://www.bernama.com/en/news.php?id=2518171 ,
https://www.thestar.com.my/metro/metro-news/2026/01/20/on-demand-shuttle-service-to-expand-with-53-new-zones ,
https://www.malaymail.com/news/malaysia/2026/01/07/transport-minister-govt-committed-to-ensure-my50-price-remains-unchanged-despite-rm100m-funding-shortfall/204613 ,
https://www.insage.com.my/BursaNews/Attachment/202503/20250307/HI-AN20250307A2-2.pdf

## Malaysia: states and cities

- Common pattern: governments buy service from operators for "a fixed fee for
  services rendered less any penalty" (penalties 3-4%), contracts 1-5 years,
  or pay per pass on someone else's network.
- Penang: Pas Mutiara, a free 30-day pass on all Rapid Penang buses for
  Malaysian citizens; the state (now with federal help) pays Rapid Penang per
  pass (RM7m for up to 280,000 passes in 2025 and 2026). CAT free shuttles paid
  by the state or the island city council: George Town loop (since 2009),
  CATBridge peak-hour shuttles to the Bayan Lepas factory zone (since 2020),
  CAT Hospital (2026, RM900k a year, 3 buses, 600 riders a day). 2018 feeder
  CATs were cut in 2020 for low ridership. RM16.8m budgeted for 2027.
- Johor: Bas Muafakat Johor, free for citizens, tourists buy a QR pass in an
  app; state and councils fund it (about RM30m a year); 65 routes, 79 buses,
  about 6,000 riders a day (about RM14 per trip). Routes overlapping BAS.MY
  are dropped.
- Selangor: Smart Selangor Bus, free for Malaysians (foreigners 90 sen); the
  state passes money to councils, which contract operators; 57-60 routes, 156
  buses; ridership falling, weak routes cut; electric buses ordered (50% by
  2028). Now gives residents RM30 a month travel credit instead of running its
  own vans.
- Kuala Lumpur GoKL: free for citizens, foreigners RM1, about RM30m a year from
  City Hall, 86 electric buses.
- Sarawak: RM1 flat fare in its cities since 2020; one operator quit after
  waiting 20 months for repayment.
- Melaka: state-owned Panorama Melaka closed at the end of 2024 and its routes
  went to BAS.MY.

Sources: https://www.buletinmutiara.com/penang-budget-2027-rm16-8mil-for-buses-rm150mil-flood-mitigation-programme ,
https://paultan.org/2024/12/20/pas-mutiara-my50-free-penang-bus-rides-to-continue-in-2025-rm7-mil-subsidy-allocated-for-280000-passes/ ,
https://landtransportguru.net/jbbusp311/ ,
https://en.wikipedia.org/wiki/Smart_Selangor_Bus ,
https://klccc.dbkl.gov.my/gokl-2

## Town traffic and pedestrians: OpenTTD prior art

- Real car traffic has been tried and always went badly: the TownTraffic patch
  (2018; unsaved real vehicles, a desync recipe, crashed with eye-candy GRFs),
  TownCars AI (cars hunting for depots took 83% of frame time; four AIs with
  200 cars stopped fast-forward), CityLifeAI (CPU loops at the vehicle cap,
  cars blocking town roads, 2-3x the planned cars in some towns). An early
  forum conclusion: "virtual traffic is the way forward". Simutrans players
  call city cars the main cause of jams.
- Pedestrians: only TownTraffic tried walkers (as road vehicles). Station and
  road stop NewGRFs already show crowds by waiting cargo
  (`cargo_amount_waiting`, OpenTTD 14 road stops).
- Effect vehicles are game state (pooled, saved, use Random()), so scenery
  can't be made of them. Purely visual sprites drawn from a tile hash and the
  tick counter, with no pool objects, map writes or Random(), are safe for
  saves and multiplayer (like text effects and our road traffic sampling).
- Road vehicles may only enter tiles in `compatible_roadtypes` (set from the
  road type's powered list at build, articulation and load; checked at tile
  entry, overtaking, stops, depots and in the pathfinder). A fork-level
  "buses only" road type can add itself for `IsBus()` vehicles (and on
  refit). A tile has one road type and one tram type, so no half-tile lanes.
- No elevated road type upstream; bridges keep vehicles apart by height;
  stations can be under bridges (15.0) but never on them. JGRPP has road type
  collision modes NONE and ELEVATED.
- No toll booth anywhere; the HighwayToll game script (2018) charged other
  companies on roads and was easily exploited. Expressways exist as NewGRF
  road types (NoHouses, NoLevelCrossing, speed limit; U&RaTT, Timberwolf).
- 63 road and tram types in total; road tiles have m8 bits 13-15 free.
- A road vehicle blocked for 1,480 ticks drives through the blocker.

Sources: https://www.tt-forums.net/viewtopic.php?t=82853 ,
https://github.com/F1rrel/CityLifeAI ,
https://bananas.openttd.org/package/ai/54434152 ,
https://github.com/OpenTTD/OpenTTD/pull/10144 ,
https://jgrennison.github.io/OpenTTD-patches/newgrf-additions.html ,
https://www.tt-forums.net/viewtopic.php?t=82732

## BRT, tolls, road pricing, interchanges, expressways

- Sunway BRT: 5.4 km fully elevated busway, 7 stations, opened 2 Jun 2015,
  15 BYD electric buses, RM634m (Prasarana 70%, Sunway 15%), gated stations
  (pay before boarding), links to KTM and LRT. Free at first; when fares
  started riders fell 63%; end-to-end fare cut to RM4.30 in 2018; 6.71m riders
  in 2025 (about 21,700 a day); peak every 4-5 min.
- Johor's Iskandar BRT was shelved on 17 May 2024 because 13.8 km (25%) would
  have shared the road with traffic; replaced by an elevated ART (RM10bn
  approved 2026). Kuching's ART (hydrogen "trackless trams" on dedicated
  lanes and viaducts) is due from 2027. KL-Klang BRT on Federal Highway was
  cancelled in 2017 (overlapped LRT3). Singapore has no BRT yet: Tengah Link
  is a bus-only road; a Tuas BRT study started in 2026; the North-South
  Corridor will be the longest bus-priority corridor (2029-2031).
- ITDP BRT Standard basics: dedicated lanes, busway in the centre of the
  road, fares paid before boarding (boarding 3 s to 0.3 s per person), junction
  treatment and signal priority, level platforms. Needs 3 km+ of dedicated
  lanes; Bronze 55-69.9, Silver 70-84.9, Gold 85+. "BRT creep": keeping the
  name after cutting the essentials.
- Malaysian tolls: about 2,000 km of tolled expressways by 33 concessionaires
  who build, collect tolls for decades and hand back (PLUS: 1988, extended to
  2058 in exchange for an 18% toll cut and freeze). Open systems charge a flat
  plaza fee; closed systems charge per km (about 11 sen/km for cars on the
  NSE). Class multipliers: bus about 0.75x car, lorry 1.8-2.4x, taxi 0.5x.
  Toll freezes are compensated by government (RM591.5m for 10 highways in
  2026). Free-flow tolling (MLFF) delayed to 2027 or later.
- Singapore ERP: charges per half-hour slot, reviewed quarterly in $1 steps
  to keep expressways at 45-65 km/h and main roads at 20-30 km/h; vehicle
  weights motorcycle 0.5, car 1, big bus 2. Public buses pay ERP. The 1975
  cordon cut traffic about 45%. ERP 2.0 satellite units compulsory from 2027.
  COE: a 10-year right to own a vehicle; car numbers may not grow until 2028.
- Interchanges: Singapore about 32 bus interchanges and 11 air-conditioned
  Integrated Transport Hubs next to MRT (Toa Payoh and Boon Lay 21 bays);
  LTA owns them, the package operator runs them. Malaysia: TBS (60 platforms,
  links to KTM, LRT and airport rail), Pasar Seni hub (29 bays, keeps buses out
  of the centre), JB Sentral (11 berths by the border).
- Expressways: grade-separated access only, no frontage; Malaysia designed for
  120 km/h, signed 110, buses and lorries limited to 90; Singapore 90 km/h.
  An expressway lane carries about 2,250-2,400 cars an hour against about 900
  for a town road lane with traffic lights (about 2.5x).

Sources: https://en.wikipedia.org/wiki/BRT_Sunway_Line ,
https://www.thestar.com.my/metro/metro-news/2024/05/17/iskandar-malaysia-transit-bus-cant-tackle-traffic-shelved ,
https://brtguide.itdp.org/branch/master/guide/system-speed-and-capacity/optimizing-the-station-to-vehicle-interface ,
https://development.asia/case-study/case-electronic-road-pricing ,
https://www.lta.gov.sg/content/ltagov/en/who_we_are/statistics_and_publications/Connect/integrated-transport-hubs.html ,
https://www.motorist.my/article/2829/list-of-malaysia-highway-toll-rates-plus-klk-lpt-and-others

## Electric and modern buses, depots

- Battery-electric 12 m buses carry about 300-430 kWh; use about 0.9-1.4
  kWh/km in the tropics (air-con adds 13-29%); real range 250-280 km a day
  with a 20% reserve (Singapore), against about 450 km for a diesel tank.
  Battery fades about 2% a year; replacement around year 8.
- Charging: depot overnight 2-4 h at 90-150 kW, or under 1.5 h at 360 kW;
  terminal roof chargers up to 450 kW add about 48 km in a 10-15 min layover.
- Cost: an e-bus is about 2x a diesel (Singapore S$450-600k vs about
  S$270k); maintenance about 30% lower; cheaper per km only over long (about
  15-year) contracts, so Bogota and Santiago give e-bus contracts 14-15 years.
- Singapore: about 480 e-buses of about 6,070 (May 2026), 1,140 by 2027;
  half electric by 2030. New 5-storey depots (Gali Batu opened Oct 2025:
  6.28 ha, about 500 buses, 240 dual-gun 360 kW chargers, staff lounge,
  canteen, dormitory); charging is scheduled by software; planned capacity
  shrank to fit chargers and fire safety.
- Malaysia: Rapid Bus plans 1,600 e-buses by 2031; phase 1 250 (175 KL,
  75 Penang), RM1.2-1.5m each estimated, about one charger per bus. Sunway
  BRT electric since 2015; GoKL 86 e-buses; Selangor 50% electric by 2028;
  BAS.MY mostly Euro 5 diesel for cost reasons.
- Diesel: Euro VI in Singapore since 2018, Malaysia buys Euro 5; 40-53 L per
  100 km with air-con. Capacities: 12 m single-deck about 80, double-deck 139,
  18 m articulated about 150; 17-year life in Singapore. Useful types: on-
  demand van, midibus, 12 m, double-deck, articulated, BRT bus with level
  boarding, trolleybus charging under wires.
- Depots: about 50 buses per hectare on the surface, about 80 multi-storey;
  100-175 buses is a good size; 7 repair bays and 1 fuel/wash lane per 100
  buses; about 1 charger per e-bus in new depots (2-4 buses per charger
  elsewhere); empty runs to and from routes can be 30-50 km a day.
- City Bus Manager's e-bus add-on: battery kWh, consumption and charging
  power per model; chargers on depot parking and at stops; the shift planner
  checks range. No OpenTTD set models range or charging.
- OpenTTD engine notes: built-in engines are numbered in one sequence across
  vehicle types, and NewGRFs can claim unreserved built-in slots with the same
  local id (EngineOverrideManager::UseUnreservedID). New built-in buses need
  their own id range and must be appended to old saves' engine maps on load.

Sources: https://www.malaymail.com/news/malaysia/2026/01/12/cleaner-rides-ahead-as-rapid-bus-maps-out-plan-for-1600-electric-buses-by-2031-says-loke/205156 ,
https://landtransportguru.net/gali-batu-bus-depot/ ,
https://theicct.org/wp-content/uploads/2024/01/ID-44-%E2%80%93-Transjakarta-TCO-Working-Paper-A4-50108-PUBLIC-fv-1.pdf ,
https://www.lta.gov.sg/content/ltagov/en/newsroom/2020/10/news-releases/update-on-deployment-of-electric-buses.html ,
https://brtguide.itdp.org/branch/master/guide/depots/design-layout

## Mapping to the game

| Real scheme | In the game |
|---|---|
| Commercial operation (old Singapore, Malaysian private operators, classic OpenTTD) | Default: you keep the fares and carry the risk |
| Gross-cost contract (BCM, BAS.MY) | A town (or a national agency) tenders a route or package; pays a fixed fee per scheduled trip or km; keeps the fares; bonuses/penalties from regularity and punctuality; 5 years plus extension |
| Government-owned fleet (BCM, Prasarana) | Contract can come with town-owned vehicles you operate but don't own; they stay if you lose the contract |
| Free town line (CAT, Bas Muafakat Johor, Smart Selangor, GoKL) | Town pays a fee per bus per year; riders free; more riders and happier town; outsiders may pay a token fare |
| Resident pass (Pas Mutiara, My50, BAS.MY RM30 pass) | Town pays you per pass holder per month from a capped yearly pot; pass holders ride free |
| Concessions (students, seniors, disabled) | A share of riders pays half or nothing; whoever keeps the fares carries the loss unless the town compensates |
| Targeted shuttle (CAT Hospital, CATBridge) | Town asks for a shuttle to a clinic, school, factory zone or station, maybe peak-only |
| Net-cost top-up (rural support fund) | You keep fares, the town tops up a weak route while punctuality stays above 80% |
| On-demand vans (Rapid On-Demand) | Vans serve any stop in a zone by demand at a flat fare; weak routes can switch to vans and back |
| Fleet funding (BSEP, BCEP) | Town or agency funds extra vehicles on its routes |
| Fare regulation (PTC) | Authority sets or caps fares; one journey fare across transfers |
| Bus priority (bus lanes, give-way to buses, B signals) | Town-built bus lanes (part-day or full-day), cars give way to buses leaving stops, bus head start at traffic lights |
| Ridership reviews and overlap (Penang 2020 cuts, Johor drops overlapping routes) | Towns cut routes whose cost per rider is too high and drop routes a bigger network covers |

## Proposal (merged plan, not built yet)

Visible traffic and pedestrians are scenery drawn from the game clock (no
saved vehicles, no gridlock, safe in multiplayer); a calculated congestion
level per road is what slows vehicles, unless they use bus lanes or busways.

| # | Part | Size |
|---|---|---|
| 1 | Lines: number, colour, name; line window (stops, live vehicles, frequency, regularity, punctuality, load, riders, profit); frequency target with vehicles needed and even spacing; network map; departure boards | L |
| 2 | Street life and traffic: congestion per road from town size, homes and jobs, services and (optional) rush hours, by road capacity; animated cars, vans, lorries and motorbikes in proportion; pedestrians on sidewalks, crossing at lights, crowds at stops | L |
| 3 | Fares: classic, distance (BAS.MY), flat, zones, free; per transport type and per line; one fare across transfers; passes and concessions; demand reacting to price | M |
| 4 | Town transit authorities: budgets; route contracts, free lines, resident passes, concessions, shuttles, top-ups, fleet funding, on-demand zones; tenders (quality and price) against simulated rivals; KPI bonuses and penalties; renewals; route reviews; optional national agency; e-bus quotas and longer e-bus terms; renameable scheme names | L |
| 5 | Modern buses: built-in van, midibus, 12 m, double-deck, articulated (if feasible), BRT bus; diesel and battery-electric versions with range, air-con load, battery fading, costs | L |
| 6 | Depots that work like depots: a depot plus neighbouring tiles for parking (capacity, multi-storey), charging bays (slow/fast, power limit), repair bays, wash and fuel lane, staff building, solar roof; home depots and empty runs; parked buses visible; fast chargers at end stops; optional staff budgets | L |
| 7 | Bus priority: painted bus lanes (part-day or full-day), give-way to buses at stops, bus signals at traffic lights, bus-only links | M |
| 8 | Busways and BRT: kerbed busway and elevated busway (Sunway style; stations beside or under), BRT stations with fare gates and level boarding, a BRT score from the basics, penalties for shared sections | L |
| 9 | Expressways, tolls, road pricing: expressway road type; toll plazas run as concessions (flat or per km, vehicle classes, fee caps), toll freezes, free-flow upgrade; town congestion charges | L |
| 10 | Interchanges: big bus interchanges with many bays, air-conditioned hub upgrade next to rail with transfer and comfort bonuses | M |
| 11 | Satisfaction: from waiting, crowding, punctuality, fare, vehicle condition and comfort; feeds ratings and happiness | M |
| 12 | Rush hours and on-demand: optional city clock with peaks; peak and off-peak frequencies; peak-only shuttles; on-demand van zones | L |
| 13 | Metro and rail: line maps, driverless lines, platform crowding, feeder contracts | M |
| 14 | Land value and pollution (old M7): traffic and diesel pollute; e-buses, parks and services help | M-L |

Order: 1 lines (everything measures lines), 2 street life (gives bus lanes,
busways and tolls a point), 3-4 fares and town authorities, 5-6 buses and
depots, 7-10 roads and BRT, then 11-14.
