# Wira Bandar PC: changes by version

Wira Bandar PC is a fork of OpenTTD: OpenTTD's code with changes. Its own
version is `MAJOR.MINOR.PATCH`, shown in the window title, at the bottom of
the main menu and in the About window, and it goes up with every release:

- a release that only fixes things raises the last number (0.1.1, 0.1.2, ...);
- a release with new features raises the middle one (0.2.0, 0.3.0, ...);
- 1.0.0 is for when the owner calls the game complete and stable.

The About window also shows the build (date, branch and commit), which says
exactly which code a copy was made from. The version is set in one place,
`WIRA_BANDAR_VERSION` in `src/rev.h`; raise it, and add the release here,
with every release.

Downloads carry the version in their name: `wirabandar-0.2.0-windows-x64.zip`
and so on. Every version has its own release on GitHub (tag `v0.2.0` and so
on), with its part of this file as notes; a newer build of the same version
replaces its zip. The rolling `latest` release always has the newest build,
under its versioned name and under the old name `wirabandar-windows-x64.zip`,
so links to that name keep working.

## 0.4.1 (2026-11-09)

Fixes for problems found while writing the guides for 0.4.0:

- The first general election of a game made the five-year plan twice:
  "Five-year plan 1!" on polling day, before the results, and "Five-year plan
  2!" with the same projects two days later, which pushed the results back
  in the news. The plan now comes once, with the results.
- A federal territory's transit agency is three times as likely as a town to
  invite a tender each month, as meant; it invited one every month it could.
- Under Roads and Growth the five-year plan's expressways can also join two
  capitals in the same region, on the region's own land; it only joined
  capitals on the main land. The plan still offers three at most, the pairs
  closest together.
- The agency's transit window and tender news say the agency pays and
  invites tenders (they said the town), and point to the agency's window.
- The national programmes say the transit fund and the faster growth are for
  the towns in the states, as they work (they said every town): the federal
  territories have the federal platform as their own government instead.
- Headlines about regions ("Sarawak keeps its own laws!", the assembly
  dissolved, nomination day, polling day, a first election called), a plan
  without projects and a new federal hospital or university end with "!", so
  the message history no longer runs them into the next sentence.
- "caretaker" is written in lower case in the States and town windows, as
  everywhere else ("Governed by the federal government: caretaker").

## 0.4.0 (2026-11-09)

Elections you see coming, and a federal government that does more than pay
grants (the owner's picks).

The run-up to every election, with news popups:

- Two months before polling day the assembly is dissolved (for a general
  election parliament and the assemblies of the states on the main land) and
  the government carries on as a caretaker: it keeps paying for what it runs
  but starts nothing new (no funded buildings, no new plan). The States
  window says so. Before the first election the news says
  the election is called.
- A month before polling day, nomination day: every platform stands in every
  seat, and the news gives the polls.
- Polling day (1 May for general elections, 1 September for regions): the
  election night window opens and counts the seats town by town, the
  smallest towns first and the big cities last, with a bar for each platform
  against a majority and the latest towns declared; then the winner, and for
  a general election the states' assemblies. Skip to the result, or count
  again; the States window's Last election button opens it again.
- Two days later the results are declared in the news.

The federal government:

- National programmes: the platform elected to parliament works across the
  whole country, on top of each state's own. Transit First pays a national
  transit fund of 5% of every town's taxes into its transit budget, and half
  of the monthly passes riders buy, so more of them buy one (as Malaysia's
  My50). Roads and Growth subsidises fuel, 20% off the running costs of road
  vehicles that burn it (not trams or electric buses), and towns grow 5%
  faster. Homes and Services builds, each January, a hospital for the biggest
  town of 6,000 people without one, or else a university for the biggest of
  12,000 without one; the town runs it.
- A five-year plan, as the Rancangan Malaysia: when the results of a general
  election are declared, the government offers companies projects that match
  its platform. Transit First: a metro line for the biggest city (a railway
  line with four stations in it and eight departures a month) and BRT lines
  for the capitals (a bus line graded BRT with three stops and eight
  departures). Roads and Growth: expressways joining capitals. Homes and
  Services: hospitals and a university. The first company to deliver a
  project is paid; what nobody delivers lapses when the next plan begins.
  Each project delivered gives the federal government 2 more votes in every
  seat at the next general election (at most 8). Games with a federal
  government already get their first plan at once.
- Federal transit agencies, as Prasarana in Kuala Lumpur: a federal
  territory's transit budget gets 25% of its taxes on top from federal money,
  and its agency invites tenders three times as often as a town, picking
  railway and BRT lines four times as readily. Its transit window and
  tenders name the agency.
- The States window's Federal government button shows all of it: the
  government, its programmes, its plan with each project and what it needs,
  and what it spent.

Also in this download, the fixes of 0.3.1, which had no download of its own:

- Bus lanes, bus-only roads, kerbed busways and expressway markings show on
  roads up a slope, and tram tracks show over them.
- On a line with even spacing, the departure boards after the first stop
  count each bus's wait at the first stop.
- The tutorial's Finish closes it, and a step keeps its count when you come
  back to it.
- The States window fits the selected state; the help text of States and
  elections, the news when a region forms and the States window describe
  regions as they are since 0.2.0.

## 0.3.1 (2026-11-09)

No download of its own: these fixes came out with 0.4.0.

Fixes for problems found while updating the player guides for 0.1.1-0.3.0,
and two the owner found:

- Bus lanes (all day and peak hours), bus-only roads, kerbed busways and the
  expressway's lanes and barrier show on roads up a slope too; they showed on
  flat road only.
- Tram tracks show over bus lanes, busways, bus-only roads and expressway
  markings, on roads and through drive-through stops; the paint hid them.
- On a line with even spacing, the departure boards of the stops after the
  first count the time each bus waits at the first stop for its turn. Two
  buses leaving the first stop 7 days apart showed "in 17.4 days" and "in
  17.7 days" at the next stop.
- The tutorial's Finish, at the end of a chapter, closes the tutorial, as its
  last line says (it showed the chapters). Tutorial in the main menu, or the ?
  button on the main toolbar and then Tutorial, opens the chapters again.
- The tutorial keeps the count of what you did at a step ("Bus stops built: 1
  of 3") when you click Continue, open the tutorial again or load a game; it
  started from 0 again. Picking a chapter starts it afresh.
- The States window fits the selected state's details: since 0.2.0 it was as
  tall as a region's laws need, with empty space for the other states.
- The help text of States and elections describes regions as they are since
  0.2.0: work passes, the checkpoint and permits. Each state of a region keeps
  its own checkpoint, towards the region's other states too.
- When the states form, the news of a state named Sabah or Sarawak no longer
  reads "Sarawak ... is run like Sabah and Sarawak": it "keeps the rights it
  joined the federation with". The news says so when a region's other states
  are behind a checkpoint too, and the States window's region line drops the
  comparison for Sabah and Sarawak as well.

## 0.3.0 (2026-11-09)

Malaysian and Singaporean names:

- New games name their towns after Malaysia and Singapore: real towns and
  neighbourhoods (Kampar, Sungai Petani, Ang Mo Kio, Tampines ...) and
  made-up names from two Malay words (Bukit Indah, Taman Jaya, Sungai
  Perdana ...). It is the first choice in the map's Town names list and on
  by default; Settings, Towns, "Malaysian and Singaporean town names" turns
  it off for new towns.
- When the states form, the federal territory's city becomes Kuala Lumpur
  (a second one Putrajaya), the states' capitals take the names of the
  state capitals of Malaysia from the biggest down (Johor Bahru, George
  Town, Ipoh, Shah Alam ...), the regions' capitals Kuching and Kota
  Kinabalu, and the states the names of their states (Johor, Pulau Pinang
  ... Sarawak, Sabah). Only towns with a Malaysian name are renamed, never
  one a player named, so older games keep their names.

## 0.2.0 (2026-11-09)

Regions run like Sabah and Sarawak decide who comes in, instead of shutting
people out:

- Commuting across a region's border: people may live on one side and work
  on the other. A checkpoint adds 15 minutes to every such commute, and a
  region gives work passes to only part of the people from elsewhere who
  would work in its towns: more when its towns lack workers, fewer when
  they lack jobs, more under Roads and Growth and fewer under Homes and
  Services (at least 20%). The rest find work on their own side. Its own
  people may work outside it.
- New towns founded by companies and land reclaimed in a region's waters
  need its permit. The council of the region's nearest town must rate the
  company Mediocre or better. When the region wants to grow (its towns lack
  workers, or Roads and Growth governs), the state co-funds them and pays
  half; otherwise the company pays the full cost and a permit fee of 25% to
  the state.
- The States window shows a region's work passes, its jobs and workers, the
  checkpoint and its permit terms.

An in-game tutorial:

- A tutorial window walks the player through the game step by step: it says
  what to do with the labels on screen, makes the button to click flash and
  moves on once the step is done. Three chapters: Start a game, Your first
  bus line, and What's new for OpenTTD players. Leave it at any step (Exit
  tutorial or its close box) and open it again at any time with Tutorial in
  the main menu, or the ? button on the main toolbar, then Tutorial; it
  starts where you left off.
- It is offered on the first start, and the first time an OpenTTD save
  (`.sav`) is loaded. It works in any game, old saves included, and keeps
  your place in `openttd.cfg`, never in saves.
- The tutorial's welcome and the About window say what the name means:
  Wira Bandar is "town hero" (wira is Malay for hero, bandar for town), and
  the game's goal is a city that moves people, not just cars.

## 0.1.1 (2026-11-09)

Fixes for problems found while writing the player guides.

- Council news and the Budget window name the building: "Majlis Perbandaran
  Rarnwell opens a new clinic", "New services: a police station next"
  (they said "a new health", "a police next").
- The Budget window's estates line says "1 year old", not "1 years old".
- The Budget and Transit windows open wide enough for their longest line and
  widen when a line grows (a big town's spending, a congestion charge); the
  Tolls window fits its lines (it opened very wide and mostly empty); the
  line map can be made as short as three stops (it opens at eight).
- Towns under 500 people, which never count commuters, say so ("not counted
  (towns count them from 500 people)") instead of "not counted yet".
- Towns from a savegame made before councils have their council right after
  loading (a town of 10,720 people showed a district council until January);
  so do towns when councils are switched on.
- The line window updates after its own buttons: Even spacing can be used
  as soon as a frequency is set, and Even spacing and On demand show
  pressed when on.
- "which needs N vehicles" no longer counts the time vehicles wait for their
  turn at the first stop (held there or queued behind) with even spacing on:
  12 buses held to a gap of 340 ticks said 12, now 8-10 (8 without holding).
- The Bus Priority and Tolls toolbars open under the Road Construction
  toolbar instead of over its last buttons.
- The main menu no longer says the base graphics are missing 301 sprites:
  those are Wira Bandar's own, which come with the game.
- The Air pollution and Land value tints leave water untinted, as the map
  window does.
- Docks and ship depots can be built in a region's waters (they keep the
  sea); turning sea into land there is still refused.
- A region's native customary land is given up to companies the town rates
  Mediocre or better, as the message says (it refused part of Mediocre).
- The States window's Policy line lost "(paused: the treasury is empty)",
  which could never show (a state never spends more than its treasury has).

## 0.1.0 (2026-11-08)

The first release under the name Wira Bandar PC, a fork of OpenTTD: traffic
lights and give way, zoning, city services and councils, transit (lines,
fares, transit authorities, modern buses, depots, bus priority, busways and
BRT, expressways and tolls, interchanges, passenger satisfaction, rush hours,
metro and rail), land value and pollution, town budgets and ordinances,
commuting, Malaysian and Singaporean buildings, shops, states, elections,
federal territories and regions. See `docs/city-builder-plan.md`.
