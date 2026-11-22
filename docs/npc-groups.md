# NPC groups

A group is a named set of conditions on who an NPC is (faction, sex, race). Every NPC that fits
wears the group's outfit rows (the Default and the situations) without being assigned one by one:
every blacksmith in clothes, a hood and a cloak in the rain; every Whiterun guard in another
armor. Groups are off by default; a setting adds the Groups tab the next time the window opens.

Not built. Built from `main` once custom situations (docs/custom-situations.md, branch
`feature/custom-situations`) is in `main`: the editor and the situation registry it needs live
there. Step 0 of §11 needs neither.

## 1. Scope

- In: non-unique NPCs (bandits, guards) as well as unique ones; conditions Faction, Sex, Race;
  presets; the Groups tab; a group's outfit rows edited on the Situations page.
- Not in the first version: items left off per row (`OutfitRows::Exclusion`) for a group's rows;
  what a corpse's loot becomes; any limit on what enemies may wear; choosing another single NPC
  as the Dress page's target; factions a mod adds, beyond what the picker reads from the
  window's NPC; a faction whose hostility changes in play.

## 2. Names

- The feature's code: `GroupRules` (the model, a pure header), `GroupFactions` (the shipped
  factions and presets), `GroupStore` (`groups.json`), `GroupPages.cpp` (the tab).
- `Settings::NpcGroup`, `AccessOf`, `kNpcGroups` and `GroupOf` (the hotkey's access per kind of
  NPC, docs/design.md "group") are renamed to an access tier (`Settings::AccessTier`, ...) in a
  change of their own before anything else, so "group" means one thing.
- `CaptureWindow::PieceGroup` (the Dress page's item groups) keeps its name.

## 3. Conditions

`src/GroupRules.h`, as `SituationRules` reads: rows are ANDed, a row's values are ORed, `not`
turns the row around. The kinds describe who the NPC is and are read once per 3D load, never
polled.

| Kind    | Values                                   | Read from                          |
| ------- | ---------------------------------------- | ---------------------------------- |
| Faction | forms (`PieceFile::PieceRef`), any of    | `Actor::IsInFaction`               |
| Sex     | `male`, `female`                         | `Actor::GetActorBase()->GetSex()`  |
| Race    | forms, any of                            | `Actor::GetRace()`                 |

- Faction rows may repeat (a Whiterun guard: `IsGuardFaction` AND `CrimeFactionWhiterun`, two
  rows); Sex and Race once each.
- A form whose plugin is not loaded stays in the file and never holds, as a situation's.
- A group with no row fits no one.
- Evaluation takes the sex, the race and a faction predicate as plain values, so the tests need
  no engine (`test/GroupRulesTests.cpp`).

## 4. Data

`Data/SKSE/Plugins/Livery/groups.json` (`src/GroupStore.cpp`), shared by every save, read at
`kDataLoaded` after `SituationStore::Load` whatever the setting says.

```json
{
  "version": 1,
  "nextKey": 2,
  "groups": [
    {
      "key": "Bandits",
      "name": "Bandits",
      "removed": false,
      "conditions": [
        { "kind": "faction", "not": false, "forms": [{ "plugin": "Skyrim.esm", "formId": "0x01BCC0", "name": "Bandits" }] }
      ]
    },
    {
      "key": "user:1",
      "name": "Whiterun guards",
      "conditions": [
        { "kind": "faction", "not": false, "forms": [{ "plugin": "Skyrim.esm", "formId": "0x086EEE", "name": "Guards" }] },
        { "kind": "faction", "not": false, "forms": [{ "plugin": "Skyrim.esm", "formId": "0x0267EA", "name": "Whiterun" }] },
        { "kind": "sex", "not": false, "values": ["female"] }
      ]
    }
  ],
  "rows": {
    "user:1": {
      "Default": { "outfits": [12], "random": false },
      "Town": { "outfits": [3, 4], "random": true, "chances": { "4": 150 } }
    }
  }
}
```

- `groups` is the window's list, top to bottom; the order is the priority (§5). A preset has a
  fixed key and a `removed` flag, a user's group `user:<n>` from `nextKey`, never reused, as
  situations.json keeps them.
- `rows` is apart from the definitions, by group key then situation key (`Situations::Name`,
  as the co-save's rows), so the rows alone can move to the co-save later. A row holds outfit
  ids, `random` and `chances`, the selection chances other than 100 by outfit id
  (docs/situation-management-ui.md §1; the object left out when there are none); `chosen` is
  each member's own and items left off are not stored (§1). An
  outfit id the library no longer has is dropped at load (`OutfitRows::WithoutGone`); a
  situation key the registry does not know is kept and written back.
- A group's name is unique among groups (ASCII case ignored).
- The same load and write rules as every shared file (CLAUDE.md §2.1): `GroupStore::Load` sets
  the writable state once; a write goes through a temporary file and a rename
  (`PieceFile::WriteText`), comes before any change to a member, and a refused or failed one
  changes none. `Library::Remove` takes the outfit off the groups' rows too, after its own
  write.
- Writers: the Groups tab's New popup (a name, no conditions, no outfit), the editor's save of
  the user's edits (name, conditions), a drag in the list (order), the Remove popup, Restore
  Presets' popup, and the Situations page's save of the rows the user dragged outfits onto
  while a group is its target. Nothing else writes the file.

## 5. Who fits

- A group **can dress** when it is not removed, has an outfit in at least one row, and holds
  no hostile faction while the hostile setting is off (§7).
- Candidates: every actor but the player that is humanoid (`Actor::IsHumanoid`, as the hotkey
  asks), not a child, not dead, and not assigned by hand (an assignment without a group
  marker). Someone assigned by hand never fits a group, whatever their rows hold. The hotkey's
  access setting gates the hotkey only and is not asked.
- Among the groups that can dress and whose conditions hold, the lowest in the list wins, as
  in the Situation List.
- When: at every 3D load (`TESObjectLoadedEvent`, `loaded`), and for the actors already loaded
  (`ProcessLists::ForEachHighActor`) after a save is loaded, after a write to `groups.json`
  took, and when either setting turns on. A member is judged again at each 3D load: someone
  who no longer fits, or fits another group, is released first.
- The sink: with the feature off or no group that can dress (one atomic flag), it returns
  before anything else, as today. Otherwise it puts the FormID on a pending list under a lock
  and queues one SKSE task; the event names any reference and its thread is not documented, so
  the task, on the game thread, looks the actor up and filters. One member is dressed per
  task; the rest wait for the next.

## 6. Members

A member is an ordinary assignment (`Assignments.cpp`) with a group key: its rows are a copy of
the group's (with its own `chosen` for a random row), its Default with no outfit is what they
wore when they fitted (`defaultPieces`), and the situation poll, the locks, the death release
and the trade filter treat it as any other.

- Rows changed (§4's last writer): every member's copy is replaced; the poll redresses those
  in memory, one per second.
- Group removed or no longer able to dress, feature turned off: members in reach are released,
  one per task; the others keep their record, marked for release, and are released at their
  next 3D load.
- Apply or a row edit on a member through the window makes the assignment the user's own: the
  group key goes, and the group never dresses them again while it lasts. There is no other way
  to leave someone out.
- Release everyone releases members as it releases anyone, and groups dress no one from then
  until a save is loaded, so the save made for an uninstall holds no one dressed again.
- Lifetime, decided by step 0 of §11: when a task queued from the unload event still reaches
  the actor and can release them, a member wears Livery's items only while their 3D is loaded
  (released at unload, dressed again at load), which bounds the records and leaves nothing on
  NPCs out of reach at an uninstall. Otherwise members stay dressed as anyone assigned by
  hand, and the README's uninstall section says what stays on NPCs out of reach.
- Co-save: `ASGN` v6 is unchanged and holds a member as any assignment. A record of its own,
  written after it, names the members: entry count; per entry the actor FormID and the group
  key (as a situation key is written). A Livery that does not know it reads the members as
  assigned by hand. At load, an entry whose group is gone or cannot dress marks the member for
  release.
- A member whose reference is dynamic (`TESForm::IsDynamicForm`) is written to neither record:
  the co-save cannot tell such ids apart. After a load they are met at 3D load as someone new.

## 7. Hostile factions

No relation to `PlayerFaction` marks them in Skyrim.esm (bandits, Forsworn, vampires and
necromancers have none; hostility comes from aggression), so the shipped list (§10) carries
Livery's own `hostile` flag. While the hostile setting is off:

- an NPC in any shipped hostile faction fits no group, a group without a Faction row included;
- a group holding a hostile faction stays in the file and in the list, marked, and dresses no
  one; its members are released as §6 says;
- hostile presets are not offered and the faction picker lists no hostile faction.

## 8. Settings

Under Optional Features on the settings page, both off by default, read when the window opens
as `ItemChests` is (labels and tooltips to settle before the build):

- `[Window] NpcGroups`: adds the Groups tab and the Situations page's target choice, and lets
  groups dress. Off: members are released (§6); `groups.json` is kept.
- `[Window] HostileGroups`: §7. Greyed while the first is off.

## 9. Window

- **Groups tab** (`Page::kGroups`, `src/GroupPages.cpp`, a new `Theme::TabIcon` in
  sarrai-common), shown with `Request::offersGroups`, never with `saveOnly`. The list, top to
  bottom, each group dragged by its row to another place (the order, saved shortly after the
  drop as the Situations page saves); `New Group...` (a name, as the New situation popup),
  Edit, Remove (a popup naming how many NPCs wear it now), Restore Presets. A group that
  cannot dress shows why (no outfit, no condition, hostile setting off). The handlers write
  first and end with `SetOutfits`, as the chests'.
- **Edit group popup**: the name and the conditions, in the condition editor's look (a box per
  row, `is` / `is not` / `is any of` / `is none of`, forms with ×, `Add Condition...`). The
  drawing is the tab's own over `GroupRules`; the parts it shares with the Situations page
  (the operator dropdown, the values dropdown, the remove cross, the box) move out of
  `SituationPage.cpp`'s file scope in a change of their own, with no change in behavior. The
  situation kinds are not extended: a group's kinds never reach situations.json or
  `Situations::RowHolds`.
- **Pickers**: the Add spell popup's structure (search, tick rows, the count line, Clear All,
  Add) becomes a picker over a name, a form and an icon, in a change of its own with no change
  in behavior, then lists factions and races too.
  - Factions: the shipped list (§10; hostile ones only with the setting), the factions of the
    window's NPC (`Actor::VisitFactions`; `TESFullName`, else the plugin and the FormID, as
    the engine keeps no editor ID for a faction), and the factions the existing groups hold.
    No history is kept.
  - Races: the playable races (`TESRace` `kPlayable`), read at Open.
- **Situations page target**: with `offersGroups`, the page's title is a dropdown of the
  window's NPC and the groups not removed (same font and size as the title today). On a
  group, the rows shown and saved are the group's (§4), through a handler of its own; there is
  no Live Preview, no dice or reroll, no note on the Default's own items, and the texts that
  name the NPC name the group. The Dress page has no target choice.

## 10. Shipped factions and presets

`src/GroupFactions.h`: plugin, local FormID, Livery's label (a `LIVERY_STRINGS` text), the
`hostile` flag. Skyrim.esm, read from the file on 2026-11-20 (editor ID, FormID):

| Faction                                   | FormID   | Hostile |
| ----------------------------------------- | -------- | ------- |
| BanditFaction                             | 0001BCC0 | yes     |
| ForswornFaction                           | 00043599 | yes     |
| NecromancerFaction                        | 00034B74 | yes     |
| VampireFaction                            | 00027242 | yes     |
| SilverHandFaction                         | 000AA0A4 | yes     |
| IsGuardFaction                            | 00086EEE |         |
| ThalmorFaction                            | 00039F26 |         |
| PenitusOculatusFaction                    | 0002584B |         |
| CWImperialFaction                         | 0002BF9A |         |
| CWSonsFaction                             | 0002BF9B |         |
| CompanionsFaction                         | 00048362 |         |
| CollegeofWinterholdFaction                | 0001F259 |         |
| DarkBrotherhoodFaction                    | 0001BDB3 |         |
| KhajiitCaravanFaction                     | 000EB091 |         |
| CrimeFactionWhiterun                      | 000267EA |         |
| CrimeFactionEastmarch                     | 000267E3 |         |
| CrimeFactionFalkreath                     | 00028170 |         |
| CrimeFactionHaafingar                     | 00029DB0 |         |
| CrimeFactionHjaalmarch                    | 0002816D |         |
| CrimeFactionPale                          | 0002816E |         |
| CrimeFactionReach                         | 0002816C |         |
| CrimeFactionRift                          | 0002816B |         |
| CrimeFactionWinterhold                    | 0002816F |         |
| JobBlacksmithFaction                      | 0005091D |         |
| JobInnkeeperFaction                       | 0005091B |         |
| JobMerchantFaction                        | 00051596 |         |
| JobApothecaryFaction                      | 0005091C |         |
| JobCourtWizardFaction                     | 0005091E |         |
| JobHousecarlFaction                       | 0005091F |         |
| JobJarlFaction                            | 00050920 |         |
| JobStewardFaction                         | 00050922 |         |
| JobFarmerFaction                          | 00051591 |         |
| JobLumberjackFaction                      | 00051595 |         |
| JobMinerFaction                           | 00051597 |         |
| JobPriestFaction                          | 0005159A |         |
| JobBardFaction                            | 00053514 |         |

- Which of these ship, their labels and the presets (Bandits, Forsworn, Guards, Blacksmiths,
  Innkeepers, ... each with its conditions and no outfit) are settled at step 2 of §11, with
  Dawnguard.esm and Dragonborn.esm read the same way.
- A preset has no outfit, so it dresses no one until the user gives it one.

## 11. Implementation order

Each step is a branch from `main`, built, checked and merged before the next.

0. In game, before any of it: (a) Release everyone with a dressed NPC out of reach, then what
   they wear and keep when met again; (b) with a debug log on a branch that is not merged,
   whether a task queued from `TESObjectLoadedEvent` with `loaded` false still finds the
   actor and can release them, for a persistent and a leveled NPC. Decides §6's lifetime.
1. The access tier rename (§2).
2. `GroupRules.h` and its tests (a new file in `CMakeLists.txt`'s test list), `GroupFactions.h`
   with the presets, `GroupStore` and its load at `kDataLoaded`. Checked by `ctest` and the
   log's load line.
3. The two settings (§8).
4. Fitting and members (§5, §6): the sink, the tasks, the group key, the co-save record, the
   release paths, Release everyone. Checked in game with a `groups.json` written by hand:
   bandits and guards dressed, someone assigned by hand left alone, a save and a load, the
   feature turned off.
5. The shared editor parts and the picker (§9), behavior unchanged.
6. The Groups tab, the Edit group popup, the pickers.
7. The Situations page's target.
8. Texts into `LIVERY_STRINGS` with their Japanese lines; docs/design.md, the README and the
   Nexus page; CLAUDE.md §2.1 gains the `groups.json` rules.

To check while building step 4:

- A dynamic member met again after a load wears locked copies and has no record: dressing
  them must not add copies.
- Whether a respawned leveled NPC keeps its reference with another sex or race.
- Whether leveled guards are in their hold's crime faction for `Actor::IsInFaction`.
- Whether `GetActorBase()->GetSex()` is the sex shown for a leveled NPC.
- The thread `TESObjectLoadedEvent` arrives on, and the cost of the pending list while a cell
  loads (every reference raises the event).
