# Switching the target

The window's target (the actor, or the NPC group, whose outfits it edits) can be changed from
inside the window, without closing it and aiming the hotkey again. When the new target stands
near the player the camera moves over to them, keeping the view it had of the one before.

Not built. NPC groups (docs/npc-groups.md) are not built either; this spec covers a group as a
target, and the steps that need none come first (§10).

## 1. Scope

- In: a target dropdown on the Dress and Situations pages; an actor or a group as the target;
  the camera carried over to a near target; a far target or a group previewed on the actor in
  view; the Dress page on a target whose inventory cannot be read.
- Later, as a step of its own: keeping the carried camera out of walls (§8).
- Not settled: which near actors the dropdown offers, and their order (§2).

## 2. Names

- **Target**: who the window edits. An actor (the player or an NPC) or a group.
- **Near**: an actor whose 3D is loaded (`Actor::Is3DLoaded`) and who stands within
  `kNearUnits` (4096, a file constant in `src/Plugin.cpp`; an exterior cell's side) of the
  player, in a straight line, the height counted. Measured from the player, never from the
  target before, so a chain of switches cannot carry the camera away. Cells are not asked: an
  exterior cell border would refuse someone a step away, and an interior is one cell however
  large. **Far**: any other actor.
- **Actor in view**: the actor the camera and Live Preview show. The actor the window opened
  on, then the near actor chosen last. A far target or a group never becomes it.
- `FreeCamera::Retarget(RE::Actor&)`: sarrai-common's new function (§5).

## 3. Target dropdown

- The title of the Dress page and of the Situations page (`#{Name}`) is a dropdown, same font
  and size as the title today. No other page has one.
- Its list: actors first, then a separator line, then the groups not removed (only with
  `Request::offersGroups`). The target now is marked.
- Far actors offered: those Livery dresses in this save (an assignment in the co-save), named
  as the co-save names them. Nobody else far can be listed: an actor not in memory cannot be
  found.
- Near actors offered: not settled. Until it is, the player, the actor the window opened on
  and the actors Livery dresses.
- A window that only saves (`Request::saveOnly`) has no dropdown.

## 4. Switching

- The target before is left as the window's close leaves it: the Situations page's edits are
  saved, the Wardrobe page's unsaved-changes alert asks first when it would at Exit (Back
  cancels the switch), Live Preview ends on them, the redresses queued for the close run
  (`RedressAtClose`), and the Dress page's ticks and loaded outfit are let go.
- The window stays open on the page it shows, built again for the new target as the hotkey
  builds it (the item list, the rows, the handlers).
- A near actor becomes the actor in view: the camera moves (§5), the movement hold
  (`src/MovementHold.cpp`) goes to them, and with Live Preview on the preview begins on them;
  a refusal greys the tick with its reason, as at Open.
- A far actor or a group leaves the actor in view, the camera and the hold as they are (§6).
- Every step runs on the game thread (an SKSE task), in this order: the saves and the alert,
  `OutfitPreview::End`, the queued redresses, the new target's data, `FreeCamera::Retarget`,
  `OutfitPreview::Begin`.

## 5. Camera

`FreeCamera::Retarget(actor)` moves the free camera from the session's actor to `actor` and
makes `actor` the session's actor. Does nothing without `Begin`. Game thread.

- Read before the move, against the actor before, in that actor's own frame (their heading
  `GetAngleZ`, their feet `GetPosition`): the camera's offset forward of them, to their right
  and above their feet, and the camera's heading less theirs. A right drag's pan is part of
  the offset; nothing else is kept for it.
- Written: the new actor's feet plus the same three lengths in the new actor's frame; the
  camera's heading is the new actor's plus the same difference; the pitch stays. Someone seen
  from the front is followed by someone seen from the front.
- The height is measured from the feet and is not scaled by the actors' heights: on the same
  floor the camera keeps its height whether the new actor crouches or is shorter, and it goes
  up a floor with an actor upstairs.
- The session is read again as `Begin` reads it, for the new actor: the actor a left drag
  turns around, `anchor`, `actorStart`, `actorHeight`, `startDistance`, `startSide`,
  `startRise`. A glide or an eased wheel step under way ends. The place written always stays
  inside `MoveLimits`, since each bound stretches to the start.
- The move is made at once, with no glide.
- To the player while the game was in first person: their third-person model is shown as
  `Begin` shows it, and `End` hides it again. Away from the player: it stays shown until `End`.
- With no free camera (`Begin` refused: on a horse, seated) nothing moves; the rest of the
  switch is unchanged.
- sarrai-common change: Tableau builds with it unchanged.

## 6. Preview on the actor in view

- On a far actor or a group, a tick under Live Preview, `Preview on #{Name}` (the actor in
  view's name; the label and its tooltip to settle before the build), off each time such a
  target is chosen. A near target does not show it.
- Off: Live Preview is greyed with its reason; nothing is shown on anyone. The pages behave as
  they do on a target with Live Preview off.
- On: `OutfitPreview::Begin` runs on the actor in view with the ticked items, and the ticks,
  an outfit selected on the outfit pages and a row's outfit show on them as they would on a
  near target. The preview's own path covers the items they do not carry: a plain copy is
  added, and `End` takes it back; their own locks are off for the preview and back at `End`
  (`Assignments::SuspendLocks`). Enchantments and tempering of the target's copies are not
  shown.
- Nothing the preview does reaches the target or the actor in view's rows.
- On a group this replaces docs/npc-groups.md's "no Live Preview"; dice and reroll stay off.

## 7. Dress page on a far actor or a group

| Target | Item list | Apply |
| --- | --- | --- |
| Near actor | as today | as today |
| Far, 3D loaded | theirs, as today | as today; only the camera stays |
| Far, in memory without 3D | theirs when §9's check passes, else the next row | rows at once, dressed at their 3D load |
| Far, not in memory | the actor in view's and the player's | rows at once, dressed at their 3D load |
| Group | the actor in view's and the player's | to the group's rows |

- "The actor in view's and the player's": the list the Dress page builds for the actor in
  view as its target (headings 1–5 of docs/ui-redesign.md §1, the worn items ticked), with
  `Add from Wardrobe...` and `Add from Chest...` as usual. Opened on the player, the player's
  items alone.
- `Save to Wardrobe...` works on every target; `Create and Apply` follows this section's Apply.
- `Restore Outfit` is hidden on a group.
- Far actor without 3D: Apply writes their rows at once and puts nothing on them; they are
  dressed when their 3D loads, by the path that dresses a managed NPC then. For one not in
  memory this needs a row change that takes no `RE::Actor&` (`Assignments::Change` takes one
  today), and the copies of the pieces they do not carry are made at that dressing, not at
  the assignment (docs/design.md §2 step 2 makes them at assignment today). Nothing moves from
  the player's inventory to an actor not in memory (docs/ui-redesign.md §3); those pieces
  become copies.
- Group: a group's row holds outfit ids only. Ticks that are exactly a library outfit's items
  give that outfit to the group's Default (the popup asks first when the Default holds one,
  as on an actor). Any other ticks go to the Save page: `Apply` opens it, and `Create and
  Apply` saves the outfit and gives it to the group's Default. Nothing moves from the player's
  inventory; members get copies as they are dressed. The write to `groups.json` comes first,
  and a refused or failed one changes no member.

## 8. Keeping the camera out of walls

A later step. After §5's place is found, a ray from the new actor's chest to that place
(`TES::Pick` with a `bhkPickData`, or `bhkWorld::PickObject`); on a hit the camera stops a
little short of it, on the same line, so the angle stays and the distance shrinks.
`minActorDistance` wins over the hit. Not known yet: the collision filter that leaves out the
actor, other actors and worn items; to be taken from an open-source SKSE plugin that casts
such a ray, named in this section, then checked in game. A wall beside the camera, not between
it and the actor, is not seen by the ray.

## 9. Checks in game

- Camera: a switch between two NPCs facing different ways, on different floors, one
  crouching; to and from the player, from first and third person; with Live Preview on and
  off; someone just inside and just outside `kNearUnits`, outdoors across a cell border.
- Where `GetPosition` is for an actor riding, seated, in a bed, swimming or ragdolled; a
  camera that jumps there needs a rule of its own.
- An NPC in memory without 3D: whether their inventory and worn items read as they are, and
  whether Apply's moves reach them. Decides §7's third row.
- The player's third-person model after a switch to them from first person, and after `End`.
- Preview on the actor in view: copies gone after `End`; the actor in view's locks back on.

## 10. Implementation order

Each step is a branch from `main`, built, checked and merged before the next.

1. `FreeCamera::Retarget` and a switch among near actors on the Dress page (§3–§5), for the
   feel of the camera and of `kNearUnits`.
2. The Situations page's dropdown; far actors with 3D.
3. Far actors without 3D and not in memory (§7), after §9's check.
4. Preview on the actor in view (§6).
5. Groups as targets (§6, §7), once docs/npc-groups.md's steps reach its window.
6. The wall check (§8).
7. Texts into `LIVERY_STRINGS` with their Japanese lines; docs/design.md, docs/ui-redesign.md,
   the README and the Nexus page; CLAUDE.md §2.1 gains the Dress page's write to
   `groups.json` and the row change without the actor.
