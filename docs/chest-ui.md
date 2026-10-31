# Chests

A chest is a named set of items that is never worn: any number of them, several on one body
slot too, kept so the variants of one armor (a visor open and closed, a wig in each colour, an
alternate design) can be picked from when dressing someone, without one outfit per variant and
without the real items in an inventory. Chests are off by default: the `Item chests` setting
(`[Window] ItemChests`, SKSE Menu Framework page, Window section) adds the Chests tab and the
`Add from Chest...` link the next time the window opens. `chests.json` is read either way.

## 1. Data

- `Data/SKSE/Plugins/Livery/chests.json` (`src/Chests.cpp`), beside `outfits.json` and
  shared by every save: `{ "version": 1, "nextId": 2, "chests": [ { "id": 1, "name": "Visors",
  "pieces": [ { "plugin": "Skyrim.esm", "formId": "0x013921", "name": "Imperial Light Armor" } ] } ] }`.
  A piece is stored as the outfit library stores one (`PieceFile::PieceRef`: the plugin's file
  name and the local FormID); each form once; `pieces` may be empty. A chest's name is unique
  among chests (ASCII case ignored), independent of the outfits'. Ids are never reused.
- The same load and write rules as the library (`Chests::Load` sets the writable state once; a
  missing or empty file is no chests and may be written; a file that cannot be read or that a
  newer version wrote leaves the chests empty and is never written; a write goes through a
  temporary file and a rename). Loaded at `kDataLoaded` after `Library::Load`.
- `Chests::Create(name)` adds an empty chest; `Chests::Update(id, name, armor)` replaces its
  loaded pieces (pieces from plugins that are not loaded stay) and its name; `Chests::Remove(id)`
  takes it out. Each writes first and changes nothing on a refusal. Nothing else writes the
  file: no chest is ever assigned to a row or worn, so the co-save never names one, and turning
  the setting off or removing Livery changes nothing on anyone.
- What the library and the chests share (`src/PieceFile.h` / `.cpp`): the piece record and its
  JSON, the armor a piece names, the read at load and the write through a temporary file.

## 2. The window

The window lists the chests' items beside the outfits': every loaded item of every chest is
in `Request::pieces` at Open as `PieceGroup::kNotCarried` (listed only while a chest is loaded),
so a chest edit moves indices of that fixed list and never meets a new form. `Request::chests`
(`ChestInfo`: name, items as indices, items not loaded, id) is in name order;
`Request::offersChests` is the setting at Open, never with `saveOnly`. The handlers
(`onChestCreate`, `onChestUpdate`, `onChestRemove`) write first and end with `SetOutfits`, whose
`OutfitList::chests` the window takes as it takes the outfits, finding the edited and the
selected chest again by id.

### Chests tab

The fifth tab (a chest: a box with a domed lid), between Situations and Appearance, while
`offersChests`. Its heading's subtitle: `Items in a chest can be loaded on the Inventory page.`
Under `Select a chest to open` the page lists the chests by name over a count line; a click
opens the editor on that chest. `Create New Chest` opens the `New chest` popup: a name field (`Chest Name` on
hover), `Create` (Enter) and `Cancel` (Escape); Create hands the name to `onChestCreate`, which
adds an empty chest, and the list shows it. A taken or empty name is refused with the message
under the list. `Exit` or Escape closes the window.

### Chest editor

Reached from the Chests page; no tab column. Title `Edit "#{Chest}"`. The name field, then two
columns, `Update` / `Back` and `Remove` as a link at the row's right end, as the Wardrobe page's:

- Left, `Items` (the backpack before the label): the items someone carries that are not in the chest, under the Inventory
  page's group headings in its order (`State::pieceOrder`), each row its icon and name; then,
  under `Taken out of chest` (the chest icon), the items taken out that nobody carries, listed
  until the page is left. A click puts the item in. On one line under the list, at the column's
  right end, `Add from Wardrobe...` (the Wardrobe tab's form in place of the link's plus) and `Add from
  Chest...` (the chest) open the Load page (`LoadTarget::kChest`) listing the outfits or the other
  chests; its `Add` puts the selection's items in and returns. `No other chests.` greys the
  second link while this is the only chest.
- Right, `In Chest` (the chest icon before the label): the chest's items as edited, in the same order, over a count line that
  counts the items not loaded. A click takes the item out.
- `Update` (greyed with `Enter a name.` or `No changes to save.`) hands the name and items to
  `onChestUpdate`; the editor stays on the chest as written. `Remove` asks
  (`Remove the chest "#{Chest}"?`, Remove / Cancel) and hands the chest to `onChestRemove`; the
  editor returns to the Chests page. `Back` or Escape returns; with unsaved edits the
  unsaved-changes alert comes first (`You have unsaved changes for "#{Chest}".`, the rename and
  the items put in and taken out, Save Changes / Discard Changes / Back).
- The editor dresses nobody and changes no tick: the preview keeps showing the Inventory page's
  ticks. Dressing from inventory only (`Request::carriedOnly`) does not apply here.

### Using a chest when dressing

- The Inventory page's and the Edit page's count line end in `Add from Wardrobe...`; while
  `offersChests`, `Add from Chest...` (greyed with `No chests yet.`) is right-aligned on the
  line below it (`Theme::LinkLine::kNext`, 4px further apart). Each link has its icon in
  place of the plus, on the same square: the Wardrobe tab's form, the chest. On the Inventory page both are hidden for the
  player while they dress from their inventory only, as before. The chest link opens
  the Load page with the chests listed (`LoadSource::kChests`): a plain list by name on the
  left, the selected chest's name and items on the right, no preview change, `Load` greyed while
  none of its items is loaded.
- On the Inventory page, `Load` lists the chest's items under the heading over the pieces nobody
  carries and ticks none: the ticks stay, so the listed pieces nobody carries that are ticked
  stay listed and the unticked ones go (`DrawPieceRows`' contract: a piece that is not listed
  is never ticked). The heading names what the list came from, `"Guard", "Visors"`
  (`State::inventorySources`, found again by id at `SetOutfits`, so the list and the heading
  follow a rename, an edit or a removal made with the window open, and a tick on an item that
  leaves the list goes), and has the chest icon while a chest was loaded last, the bare
  form after an outfit's Load. Loading an outfit still replaces every tick with its items
  and lists them alone. Ticking a chest item unticks the ticked items on its body slots, as any
  tick does; Apply then dresses the actor as it would in any ticked items (an NPC gets a copy of
  an item they do not carry; the player gets one only with `AddMissingPieces`).
- On the Edit page, `Load` of a chest lists its items unticked beside the page's ticks, under
  the heading naming the outfits and chests they came from, with the chest icon when a chest
  brought an item to it last.
- The worn result is an ordinary set of items: with chest items among the ticks it is not a
  library outfit (`Outfit (Not in Wardrobe)`), unless saved as one.

## 3. Files

`src/Chests.h` / `.cpp` (the store), `src/PieceFile.h` / `.cpp` (shared with `src/Library.cpp`),
`src/ChestPages.cpp` (the Chests page and the editor; the chest list and details the Load page
uses), `src/OutfitPages.cpp` (the Load page's chest mode and targets), `src/CaptureWindow.cpp`
(the tab, the count line's links, `State::inventoryShown`, `SetOutfits`),
`src/CaptureWindowInternal.h` (`Page::kChests`, `Page::kChestEdit`, `LoadTarget`, `LoadSource`,
`ChestForm`), `src/CaptureWindow.h` (`ChestInfo`, `ChestEdit`, the handlers), `src/Plugin.cpp`
(`Chests::Load` at `kDataLoaded`, the chests in the request, the handlers), `src/Settings.cpp`
(`[Window] ItemChests`), `lib/sarrai-common` (`Theme::TabIcon::kChests`,
`Theme::HeadingIcon::kChest`, `Theme::LinkLine`, `LabelText` and `LabelLink` with an icon), `test/PieceFileTests.cpp`.

## 4. Checks

- Build; `dumpbin /exports` shows the three SKSE exports.
- Log: `No chests.json yet; no chests` on the first start, `Loaded N chest(s) from chests.json`
  after; `ItemChests = 1` once the setting is on.
- Setting off: the window has four tabs and no `Add from Chest...` link.
- Setting on: Chests tab; Create New Chest writes `chests.json` with the empty chest; the editor
  moves items between the columns, Update writes them, Back with edits asks; Remove takes the
  chest out of the file.
- Inventory page: `Add from Chest...` lists the chest's items unticked under their
  heading; ticking one unticks the item on its slot; Apply dresses the NPC in it (HUD message,
  the window's Inventory page follows).
- Load a save: nothing changes for anyone (chests are never assigned).
