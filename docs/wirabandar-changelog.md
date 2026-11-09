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
