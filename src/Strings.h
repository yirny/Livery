// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// FILE MAP
//   Table      - LIVERY_STRINGS: every text Livery shows the player, by id, with its English
//   Detail     - the translation file's parse and checks, with no SKSE, so the unit tests reach them
//   Strings    - Load (Strings.cpp) reads the chosen language's file once; Get, Format, Pieces and
//                Ref hand out the text in it, or the English
//
// Livery's player-facing text (docs/design.md §10). A translation file,
// Data/Interface/Translations/Livery_<LANGUAGE>.txt, holds one text a line as the game's own
// translation files do: the id after "$Livery_", a tab, the text.
//   $Livery_PageDress	Dress
// The file is UTF-16 LE with its byte order mark, as the game's are, or UTF-8. In a text, "\n" is
// a line break and "\\" a backslash. A text with values names them {0}, {1}... as its English
// does, in any order, and may leave some out; "{{" and "}}" are braces. Lines that do not start
// with "$" are ignored, so they can hold notes. An id the file lacks, or a text that names a value
// its English has not, keeps the English; a time of day alone is given a value its English
// leaves out (Detail::ValuesGiven).

// --- Table -------------------------------------------------------------------

// X(id, English text), one list per part of Livery, joined in LIVERY_STRINGS. The id names the
// text in the file without its "k".

// Words the window uses on several pages: page names, buttons, reasons a button is greyed.
#define LIVERY_STRINGS_COMMON(X)                                                                                                    \
	X(kPageDress, "Dress")                                                                                                          \
	X(kPageWardrobe, "Wardrobe")                                                                                                    \
	X(kPageSituations, "Situations")                                                                                                \
	X(kPageChests, "Chests")                                                                                                        \
	X(kPageAppearance, "Appearance")                                                                                                \
	X(kApply, "Apply")                                                                                                              \
	X(kBack, "Back")                                                                                                                \
	X(kCancel, "Cancel")                                                                                                            \
	X(kConfirm, "Confirm")                                                                                                          \
	X(kCreate, "Create")                                                                                                            \
	X(kExit, "Exit")                                                                                                                \
	X(kRestore, "Restore")                                                                                                          \
	X(kUpdate, "Update")                                                                                                            \
	X(kSaveChanges, "Save Changes")                                                                                                 \
	X(kDiscardChanges, "Discard Changes")                                                                                           \
	X(kApplying, "Applying...")                                                                                                     \
	X(kSaving, "Saving...")                                                                                                         \
	X(kEditHeading, "Edit {0}")                                                                                                     \
	X(kQuotedName, "\"{0}\"")                                                                                                       \
	X(kNewName, "New name: {0}")                                                                                                    \
	X(kOutfitIcon, "Outfit Icon")                                                                                                   \
	X(kOutfitName, "Outfit Name")                                                                                                   \
	X(kColumnName, "Name")                                                                                                          \
	X(kColumnType, "Type")                                                                                                          \
	X(kColumnSlot, "Slot")                                                                                                          \
	X(kNeedName, "Enter a name.")                                                                                                   \
	X(kNeedPiece, "Select at least one item.")                                                                                      \
	X(kNeedOutfit, "Select an outfit.")                                                                                             \
	X(kNeedChest, "Select a chest.")                                                                                                \
	X(kNoOutfits, "No outfits in the Wardrobe.")                                                                                    \
	X(kNoLoadedPiece, "None of its items is loaded.")                                                                               \
	X(kNoChests, "No chests yet.")                                                                                                  \
	X(kNoChanges, "No changes to save.")                                                                                            \
	X(kWornAlready, "Already worn as is.")                                                                                          \
	X(kNotCarriedHint, "This item isn't in your inventory.\nWith \"Dress yourself from inventory only\" on, it won't be equipped.") \
	X(kOutfitGone, "an outfit no longer in the Wardrobe")                                                                           \
	X(kItemsNotSaved, "items that are not a saved outfit")                                                                          \
	X(kChestsSubtitle, "Items in a chest can be loaded on the {0} page.")                                                           \
	X(kAppearanceSubtitle, "Other settings are in Livery.ini or on the SKSE Menu Framework page.")                                  \
	X(kOk, "OK")                                                                                                                    \
	X(kYes, "Yes")                                                                                                                  \
	X(kRemove, "Remove")                                                                                                            \
	X(kSave, "Save")                                                                                                                \
	X(kEdit, "Edit")                                                                                                                \
	X(kListSeparator, ", ")

// The Dress page: its item list, Live Preview and its buttons and popups.
#define LIVERY_STRINGS_DRESS(X)                                                                                                                    \
	X(kGroupWornOutfit, "Outfit (\"{0}\")")                                                                                                        \
	X(kGroupNotInWardrobe, "Outfit (Not in Wardrobe)")                                                                                             \
	X(kGroupUnmanaged, "Outfit (Unmanaged)")                                                                                                       \
	X(kGroupInventory, "Inventory")                                                                                                                \
	X(kGroupPlayerInventory, "{0}'s Inventory")                                                                                                    \
	X(kPlayer, "Player")                                                                                                                           \
	X(kPlayerHelpMove, "\"Apply\" moves the selected items from your inventory to {0}.")                                                           \
	X(kPlayerHelpCopy, "\"Apply\" gives {0} a new copy of each selected item; yours stay with you.")                                               \
	X(kPlayerHelpNotListed, "Quest items are not listed.")                                                                                         \
	X(kPlayerHelpNotListedFavorites, "Quest items and your favorites are not listed.")                                                             \
	X(kItemsSelected, "{0} of {1} items selected")                                                                                                 \
	X(kAddFrom, "Add from")                                                                                                                        \
	X(kAddFromWardrobe, "Wardrobe...")                                                                                                             \
	X(kAddFromChest, "Chest...")                                                                                                                   \
	X(kRestoreOutfit, "Restore Outfit")                                                                                                            \
	X(kRestoreOutfitHelp, "Takes Livery's outfit off {0} and forgets their situational outfits.\nItems moved from your inventory stay with them.") \
	X(kLivePreview, "Live Preview")                                                                                                                \
	X(kPreviewSummary, "Keeps the game running and dresses the character in the selected items.")                                                  \
	X(kPreviewCamera, "Camera, with the cursor over the game:")                                                                                    \
	X(kPreviewLeftDrag, "Left drag - turns around the character")                                                                                  \
	X(kPreviewRightDrag, "Right drag - pans")                                                                                                      \
	X(kPreviewWheel, "Mouse wheel - zooms")                                                                                                        \
	X(kPreviewMove, "W / A / S / D - moves (hold Shift for half speed)")                                                                           \
	X(kSaveToWardrobe, "Save to Wardrobe...")                                                                                                      \
	X(kSaveToWardrobeTooltip, "Save the selected items as a new outfit, or into an existing one.")                                                 \
	X(kApplyTooltip, "Dress {0} in the selected items.")                                                                                           \
	X(kApplyTooltipMove, "Dress {0} in the selected items.\nItems from your inventory move to {0}.")                                               \
	X(kApplyTooltipCopy, "Dress {0} in the selected items.\n{0} gets a new copy of each item from your inventory; yours stay with you.")           \
	X(kUpdateWithSelected, "Update {0} with the selected items?")                                                                                  \
	X(kWornForSituation, "This is {0}'s {1} outfit.")                                                                                              \
	X(kApplyOnly, "Apply Only")                                                                                                                    \
	X(kApplyOnlyHelp, "Sets the selected items as {0}'s default outfit without adding them to the Wardrobe.")                                      \
	X(kApplyOnlyHelpSituations, "The new default outfit takes effect when no situational outfit applies.")                                         \
	X(kRestoreOriginalOutfit, "Restore {0}'s original outfit?")                                                                                    \
	X(kSituationalOutfitsCleared, "Their situational outfits are cleared too.")                                                                    \
	X(kApplyToActor, "Apply to {0}")

// The Save page: its Create and Update tabs and the Update tab's popup.
#define LIVERY_STRINGS_SAVE(X)                                               \
	X(kCreateNewOutfit, "Create New Outfit")                                 \
	X(kUpdateExistingOutfit, "Update Existing Outfit")                       \
	X(kCreateAndApply, "Create and Apply")                                   \
	X(kUpdateOutfit, "Update {0}")                                           \
	X(kCurrentItems, "Current Items")                                        \
	X(kNewItems, "New Items")                                                \
	X(kSelectOutfitToUpdate, "Select an outfit to update")                   \
	X(kReplaceItems, "Replace {0}'s {1} items with the {2} selected items?") \
	X(kUnloadedItemsStay, "{0} items from unloaded plugins stay in it.")

// The Appearance page.
#define LIVERY_STRINGS_APPEARANCE(X)                                                                \
	X(kWindowSkin, "Window skin")                                                                   \
	X(kRestoreOriginalPalette, "Restore Original Palette")                                          \
	X(kRestoreOriginalPaletteTooltip, "Writes the palette file back as Livery ships it.")           \
	X(kRestorePaletteQuestion, "Restore {0}.ini to its original state? Your changes will be lost.") \
	X(kPlayMenuSounds, "Play menu sounds")                                                          \
	X(kPlayMenuSoundsTooltip, "Plays the game's menu sounds for clicks in Livery.")

// The outfit lists (their search, header and counts), an outfit's details and the Load page.
#define LIVERY_STRINGS_OUTFITS(X)                        \
	X(kSearchOutfits, "Search by outfit name")           \
	X(kSortByIcon, "Sort by icon")                       \
	X(kNoOutfitMatches, "No outfit matches.")            \
	X(kOutfitsInWardrobe, "{0} outfits in the Wardrobe") \
	X(kOutfitsShown, "{0} of {1} outfits")               \
	X(kItemsCount, "{0} items")                          \
	X(kItemsCountUnloaded, "{0} items, {1} not loaded")  \
	X(kLoad, "Load")                                     \
	X(kAdd, "Add")                                       \
	X(kSelectChest, "Select a chest")                    \
	X(kSelectOutfitToLoad, "Select an outfit to load")   \
	X(kUseSelectedItems, "Use Selected Items")           \
	X(kCurrentOutfit, "Current Outfit")                  \
	X(kDefaultOutfit, "Default Outfit")

// The Wardrobe page, its popups and the Apply popup it shares with Create and Apply. An Apply
// question's values: {0} the outfit, {1} the situation (Offer) or the character (ApplyAs,
// ApplyFor), {2} the situation; "Create" and "Edits" forms save the outfit or the changes first.
#define LIVERY_STRINGS_WARDROBE(X)                                                                                                                              \
	X(kSelectOutfitToEdit, "Select an outfit to edit")                                                                                                          \
	X(kItemsSelectedUnloaded, "{0} of {1} items selected, {2} not loaded")                                                                                      \
	X(kUnsavedChangesTo, "Unsaved changes to {0}.")                                                                                                             \
	X(kWardrobeApplyOffer, "Dress {0} in this outfit as their {1} outfit, or make it their default outfit.")                                                    \
	X(kWardrobeApplyDefault, "Dress {0} in this outfit as their default outfit.")                                                                               \
	X(kWardrobeApplyFor, "Dress {0} in this outfit for {1}.")                                                                                                   \
	X(kChangesLost, "Changes to {0} will be lost.")                                                                                                             \
	X(kDiscardEdits, "Discard changes")                                                                                                                         \
	X(kKeepEditing, "Keep editing")                                                                                                                             \
	X(kItemsChange, "Items: {0} to {1}")                                                                                                                        \
	X(kIconChanged, "Icon: changed")                                                                                                                            \
	X(kRemoveItemsTitle, "Remove items from the outfit?")                                                                                                       \
	X(kUnselectedRemoved, "These unselected items will be removed from {0}:")                                                                                   \
	X(kKeepItemsHint, "To keep these items in the outfit, choose {0} instead.")                                                                                 \
	X(kSaveChangesTo, "Save your changes to {0}?")                                                                                                              \
	X(kSaveAndApply, "Save and Apply")                                                                                                                          \
	X(kSavedFirstHelp, "Your changes are saved first.")                                                                                                         \
	X(kOfferMake, "Make {0} their new {1} outfit?")                                                                                                             \
	X(kOfferMakeCreate, "Create {0}, then make it their new {1} outfit?")                                                                                       \
	X(kOfferMakeEdits, "Save changes and make {0} their new {1} outfit?")                                                                                       \
	X(kOfferSwitch, "Switch to {0} for {1}?")                                                                                                                   \
	X(kOfferSwitchCreate, "Create {0}, then switch to it for {1}?")                                                                                             \
	X(kOfferSwitchEdits, "Save changes and switch to {0} for {1}?")                                                                                             \
	X(kOfferAdd, "Add {0} to their {1} outfits and dress them in it?")                                                                                          \
	X(kOfferAddCreate, "Create {0}, add it to their {1} outfits, and dress them in it?")                                                                        \
	X(kOfferAddEdits, "Save changes, add {0} to their {1} outfits, and dress them in it?")                                                                      \
	X(kApplyAs, "Apply {0} as {1}'s {2}?")                                                                                                                      \
	X(kApplyAsCreate, "Create {0}, then apply it as {1}'s {2}?")                                                                                                \
	X(kApplyAsEdits, "Save changes and apply {0} as {1}'s {2}?")                                                                                                \
	X(kApplyFor, "Apply {0} to {1} for {2}?")                                                                                                                   \
	X(kApplyForCreate, "Create {0}, then apply it to {1} for {2}?")                                                                                             \
	X(kApplyForEdits, "Save changes and apply {0} to {1} for {2}?")                                                                                             \
	X(kWearingFor, "{0} is wearing {1} for {2}.")                                                                                                               \
	X(kWearingOneOf, "{0} is wearing {1}, one of their {2} outfits.")                                                                                           \
	X(kApplyAsDefault, "Apply as Default")                                                                                                                      \
	X(kAsDefaultHelp, "Sets \"{0}\" as {1}'s default outfit.\nTheir current situational outfit stays equipped.")                                                \
	X(kAsDefaultHelpReplacing, "Sets \"{0}\" as {1}'s default outfit, replacing {2}.\nTheir current situational outfit stays equipped.")                        \
	X(kThisReplaces, "This replaces {0}.")                                                                                                                      \
	X(kRemoveFromWardrobe, "Remove {0} from the Wardrobe?")                                                                                                     \
	X(kUsedBy, "Used by:")                                                                                                                                      \
	X(kRemoveFallback, "Situations using it fall back to the default outfit. Where it is the default outfit, its items stay as an outfit not in the Wardrobe.") \
	X(kNotCarriedFrom, "You don't carry these items from {0}:")                                                                                                 \
	X(kOnlyCarriedEquipped, "Only carried items will be equipped.")                                                                                             \
	X(kCarryNone, "You carry none of these items.")

// The Chests page, its New chest and Remove popups, and the chest editor.
#define LIVERY_STRINGS_CHESTS(X)                                                                   \
	X(kNoOtherChests, "No other chests.")                                                          \
	X(kChestsCount, "{0} chests")                                                                  \
	X(kNameNewChest, "Name the new chest.")                                                        \
	X(kChestName, "Chest Name")                                                                    \
	X(kCreateNewChest, "Create New Chest...")                                                      \
	X(kSelectChestToOpen, "Select a chest to open")                                                \
	X(kTakenOutOfChest, "Taken Out of Chest")                                                      \
	X(kNoItemsToPutIn, "No items to put in.")                                                      \
	X(kEmpty, "Empty.")                                                                            \
	X(kDressWithChest, "Dress with Chest...")                                                      \
	X(kNotWhileInventoryOnly, "Not available while \"Dress yourself from inventory only\" is on.") \
	X(kRemoveChest, "Remove {0}?")                                                                 \
	X(kChestItems, "Items")                                                                        \
	X(kInChest, "In Chest")

// The Situations page: its list and outfit list, the Edit situation popup with its conditions,
// the New situation, Add location, Add spell, Remove and Restore presets popups, and why a
// change to situations.json was refused. The words of a condition and of the Applies line are
// in LIVERY_STRINGS_CONDITIONS.
#define LIVERY_STRINGS_SITUATIONS(X)                                                                                                                                                                                 \
	X(kSituationsLabel, "SITUATIONS ({0})")                                                                                                                                                                          \
	X(kSituationalOutfits, "Situational Outfits ({0})")                                                                                                                                                              \
	X(kNewSituation, "New Situation...")                                                                                                                                                                             \
	X(kSituationList, "Situation List")                                                                                                                                                                              \
	X(kSituationListHelp, "Each situation lists the outfits {0} can wear in it.\nThe lowest applicable situation takes priority. Otherwise, the default outfit is worn.")                                            \
	X(kSituationListHelpDrag, "Drag a situation by its grip to reorder it.")                                                                                                                                         \
	X(kOrderCaption, "The lowest that applies is worn.")                                                                                                                                                             \
	X(kDefaultConditions, "Worn where no situation applies,\nand in combat.")                                                                                                                                        \
	X(kItemsNotInWardrobe, "Items not in the Wardrobe")                                                                                                                                                              \
	X(kOutfitNotCarriedHint, "Some items in this outfit aren't in your inventory.\nWith \"Dress yourself from inventory only\" on, they won't be equipped.")                                                         \
	X(kRandomLine, "One of these outfits is picked at random each time this situation begins.")                                                                                                                      \
	X(kRandomNotWearing, "{0} is not wearing one of them now.")                                                                                                                                                      \
	X(kRandomAddAnother, "Add another to pick from.")                                                                                                                                                                \
	X(kRandomClick, "Click to pick another now.")                                                                                                                                                                    \
	X(kOutfitList, "Outfit List")                                                                                                                                                                                    \
	X(kOutfitListHelp, "Drag an outfit onto a situation to add it, or between outfits to set its position.\nDrag it to another situation to move it, or back here to remove it.\nClick an outfit to see its items.") \
	X(kDefaultOutfitOf, "This is {0}'s default outfit.")                                                                                                                                                             \
	X(kDefaultWornWhere, "Worn where no {0}situation applies,")                                                                                                                                                      \
	X(kDefaultWornInCombat, "and in combat.")                                                                                                                                                                        \
	X(kAlwaysOverrides, "Always overrides {0}.")                                                                                                                                                                     \
	X(kHeldByNone, "None")                                                                                                                                                                                           \
	X(kOpenInWardrobe, "Open in Wardrobe")                                                                                                                                                                           \
	X(kRestorePresets, "Restore Presets")                                                                                                                                                                            \
	X(kRestorePresetsHelp, "Brings the preset situations back as installed: a removed one returns, empty, and a renamed or edited one gets its name and conditions back.\nTheir order and nobody's outfits change.") \
	X(kPresetsAsInstalled, "Every preset situation is as installed.")                                                                                                                                                \
	X(kRestorePresetsQuestion, "Restore the preset situations?")                                                                                                                                                     \
	X(kRemoveSituationQuestion, "Remove {0}?")                                                                                                                                                                       \
	X(kHolderLine, "{0}: {1} outfits")                                                                                                                                                                               \
	X(kRestoreBringsBack, "Restore Presets can bring it back.")                                                                                                                                                      \
	X(kRestoreBringsBackNoOutfits, "Restore Presets can bring it back, but not their outfits.")                                                                                                                      \
	X(kNameNewSituation, "Name the new situation.")                                                                                                                                                                  \
	X(kSituationNameHint, "Situation Name")                                                                                                                                                                          \
	X(kUnnamedSituation, "Unnamed Situation")                                                                                                                                                                        \
	X(kNameLabel, "Name")                                                                                                                                                                                            \
	X(kNeverWorn, "Never worn: {0} takes priority whenever this situation applies.")                                                                                                                                 \
	X(kNeverWornFix, "Add a condition, edit {0}, or change their order.")                                                                                                                                            \
	X(kApplies, "Applies")                                                                                                                                                                                           \
	X(kConditions, "Conditions")                                                                                                                                                                                     \
	X(kConditionsHelp, "Applies when all conditions are met.\nIf a condition has multiple values, meeting any one of them satisfies that condition.")                                                                \
	X(kMetNow, "Met now.")                                                                                                                                                                                           \
	X(kNotMetNow, "Not met now.")                                                                                                                                                                                    \
	X(kTurnOnCustom, "Turn on custom situations in the settings to edit.")                                                                                                                                           \
	X(kRandomOutfitFor, "Random Outfit (for {0})")                                                                                                                                                                   \
	X(kRandomOutfitHelp, "On: Each time this situation begins, {0} wears a random outfit from the list until it ends.")                                                                                              \
	X(kDone, "Done")                                                                                                                                                                                                 \
	X(kRemoveSituation, "Remove Situation")                                                                                                                                                                          \
	X(kDuplicateSituation, "Duplicate Situation")                                                                                                                                                                    \
	X(kAddCondition, "Add Condition...")                                                                                                                                                                             \
	X(kTargetHelp, "{0}: checks this character\nPlayer: checks you")                                                                                                                                                 \
	X(kTargetHelpPlayer, "{0}: checks each character wearing the outfit\nPlayer: always checks you")                                                                                                                 \
	X(kAllowDuringCombat, "Allow during combat")                                                                                                                                                                     \
	X(kAllowDuringCombatTip, "In combat, the default outfit is normally worn.\nOn, this situation can apply in combat too.")                                                                                         \
	X(kAllowDuringCombatForced, "On while the situation has an In combat condition.")                                                                                                                                \
	X(kAddLocation, "Add Location...")                                                                                                                                                                               \
	X(kAddSpell, "Add Spell...")                                                                                                                                                                                     \
	X(kRemoveThisLocation, "Remove this location.")                                                                                                                                                                  \
	X(kRemoveThisSpell, "Remove this spell.")                                                                                                                                                                        \
	X(kPluginNotLoaded, "{0} is not loaded.")                                                                                                                                                                        \
	X(kAddPlaceLead, "Add the place {0} is in.")                                                                                                                                                                     \
	X(kThisLocation, "This Location")                                                                                                                                                                                \
	X(kThisLocationTip, "The named place {0} is in.\nMay include multiple rooms or outdoor areas.")                                                                                                                  \
	X(kParentLocation, "Parent Location")                                                                                                                                                                            \
	X(kParentLocationTip, "Includes {0} and every place in it.")                                                                                                                                                     \
	X(kThisCell, "This Cell")                                                                                                                                                                                        \
	X(kThisCellTip, "The individual area {0} is in.\nTypically an entire building interior or a section of the outdoors.")                                                                                           \
	X(kPlaceChoice, "{0} ({1})")                                                                                                                                                                                     \
	X(kNothingToAdd, "Nothing to add here.")                                                                                                                                                                         \
	X(kAlreadyListed, "Already in the list.")                                                                                                                                                                        \
	X(kAddSpellLead, "Add a spell to look for on {0}.")                                                                                                                                                              \
	X(kAddSpellLeadYou, "Add a spell to look for on you.")                                                                                                                                                           \
	X(kSearchByName, "Search by name")                                                                                                                                                                               \
	X(kShowAllSpells, "Show all spells")                                                                                                                                                                             \
	X(kShowAllSpellsTip, "All spells, powers, potions and scrolls, including those added by mods.\n\nOff: Only spells known by {0} and those currently affecting them.")                                             \
	X(kShowPlayerSpells, "Show player spells")                                                                                                                                                                       \
	X(kShowPlayerSpellsTip, "Also lists spells you know and those currently affecting you.")                                                                                                                         \
	X(kOneSpellSelected, "1 spell selected")                                                                                                                                                                         \
	X(kSpellsSelected, "{0} spells selected")                                                                                                                                                                        \
	X(kClearAll, "Clear All")                                                                                                                                                                                        \
	X(kTypeToSearch, "Type to search.")                                                                                                                                                                              \
	X(kNothingFound, "Nothing found.")                                                                                                                                                                               \
	X(kMoreSpells, "{0} more. Type more to narrow.")                                                                                                                                                                 \
	X(kSelectSpell, "Select at least one spell.")                                                                                                                                                                    \
	X(kTooManySituations, "Livery cannot hold more situations.")                                                                                                                                                     \
	X(kTooManySituationsRestart, "Livery cannot hold more situations; removed ones still count until the game is restarted.")                                                                                        \
	X(kSituationRemoved, "\"{0}\" has been removed.")                                                                                                                                                                \
	X(kSituationRemovedAlready, "\"{0}\" has been removed already.")                                                                                                                                                 \
	X(kDefaultNoConditions, "The default outfit has no conditions.")                                                                                                                                                 \
	X(kSituationOrderMismatch, "The situations' order does not match the list. Open the window again.")                                                                                                              \
	X(kDefaultNotDuplicated, "The default outfit cannot be duplicated.")                                                                                                                                             \
	X(kDefaultNotRemoved, "The default outfit cannot be removed.")

// A situation's conditions (SituationRules.h): the preset situations' names, each kind's name,
// help and row words in the editor, its values, the operators, the Applies line, the
// times of day, and the lines about situations' names. An Applies clause is one condition's
// whole wording, {0} its values joined by ListSeparator and AppliesOr ("a town, a castle or a
// dungeon"); a line joins its clauses by ListSeparator and ends in AppliesEnd. A time's values:
// {0} the hour of 12, {1} the minutes in two digits, {2} the hour of 24, for a translation
// alone ("{2}:{1}").
#define LIVERY_STRINGS_CONDITIONS(X)                                                                                                                                                                                                          \
	X(kPresetDefault, "Default")                                                                                                                                                                                                              \
	X(kPresetTown, "Town")                                                                                                                                                                                                                    \
	X(kPresetCastle, "Castle")                                                                                                                                                                                                                \
	X(kPresetHome, "Home")                                                                                                                                                                                                                    \
	X(kPresetSleep, "Sleep")                                                                                                                                                                                                                  \
	X(kPresetCold, "Cold")                                                                                                                                                                                                                    \
	X(kKindState, "State")                                                                                                                                                                                                                    \
	X(kKindLocationType, "Location type")                                                                                                                                                                                                     \
	X(kKindPlace, "Specific Location")                                                                                                                                                                                                        \
	X(kKindWeather, "Weather")                                                                                                                                                                                                                \
	X(kKindTimeOfDay, "Time of day")                                                                                                                                                                                                          \
	X(kKindSpell, "Spell Effect")                                                                                                                                                                                                             \
	X(kTipState, "In combat: fighting.\nWeapon drawn: a weapon, spell or fists at the ready.\nIn water: any water underfoot, however shallow.\nSwimming: swimming only, not wading.\nSleeping: going to bed, in it or getting up.")           \
	X(kTipLocationType, "Town: a city, town, settlement, inn or house.\nCastle: a jarl's palace or keep.\nPlayer home: a house you own.\nDungeon: a cave, ruin, fort or other dungeon.\nIndoors: inside a building, cave or other interior.") \
	X(kTipPlace, "In one of the locations listed.\nA location counts with every room and area in it.")                                                                                                                                        \
	X(kTipWeather, "Rain and Snow: the weather now, outdoors only.\nWarm, Cool and Freezing region: the climate of the area, whatever the weather.\nIndoors is none of them, except ice caves.")                                              \
	X(kTipTimeOfDay, "Game time.\nA range past midnight wraps, as 8 PM to 6 AM.")                                                                                                                                                             \
	X(kTipSpell, "A spell, power, shout, potion or other effect currently affecting {0}.\nListed by spell name, not effect name.\nAny spell, scroll or potion with the same effects counts.")                                                 \
	X(kSubjectLocationType, "The location type")                                                                                                                                                                                              \
	X(kSubjectLocation, "The location")                                                                                                                                                                                                       \
	X(kSubjectWeather, "The weather")                                                                                                                                                                                                         \
	X(kSubjectTime, "The time is")                                                                                                                                                                                                            \
	X(kOperatorIs, "is")                                                                                                                                                                                                                      \
	X(kOperatorIsNot, "is not")                                                                                                                                                                                                               \
	X(kOperatorIsAnyOf, "is any of")                                                                                                                                                                                                          \
	X(kOperatorIsNoneOf, "is none of")                                                                                                                                                                                                        \
	X(kOperatorHas, "has")                                                                                                                                                                                                                    \
	X(kOperatorHasNot, "has not")                                                                                                                                                                                                             \
	X(kOperatorHasAnyOf, "has any of")                                                                                                                                                                                                        \
	X(kOperatorHasNoneOf, "has none of")                                                                                                                                                                                                      \
	X(kOperatorBetween, "between")                                                                                                                                                                                                            \
	X(kOperatorNotBetween, "not between")                                                                                                                                                                                                     \
	X(kTimeRangeAnd, "and")                                                                                                                                                                                                                   \
	X(kSelectValues, "Select...")                                                                                                                                                                                                             \
	X(kValueTown, "Town")                                                                                                                                                                                                                     \
	X(kValueCastle, "Castle")                                                                                                                                                                                                                 \
	X(kValuePlayerHome, "Player home")                                                                                                                                                                                                        \
	X(kValueDungeon, "Dungeon")                                                                                                                                                                                                               \
	X(kValueIndoors, "Indoors")                                                                                                                                                                                                               \
	X(kValueRain, "Rain")                                                                                                                                                                                                                     \
	X(kValueSnow, "Snow")                                                                                                                                                                                                                     \
	X(kValueWarmRegion, "Warm region")                                                                                                                                                                                                        \
	X(kValueCoolRegion, "Cool region")                                                                                                                                                                                                        \
	X(kValueFreezingRegion, "Freezing region")                                                                                                                                                                                                \
	X(kValueInCombat, "In combat")                                                                                                                                                                                                            \
	X(kValueWeaponDrawn, "Weapon drawn")                                                                                                                                                                                                      \
	X(kValueInWater, "In water")                                                                                                                                                                                                              \
	X(kValueSwimming, "Swimming")                                                                                                                                                                                                             \
	X(kValueSleeping, "Sleeping")                                                                                                                                                                                                             \
	X(kTipWarmRegion, "Southern forests such as the Rift and Falkreath.")                                                                                                                                                                     \
	X(kTipCoolRegion, "Plains and hills such as Whiterun and the Reach.\nA mod's land too, unless it often snows there.")                                                                                                                     \
	X(kTipFreezingRegion, "Snowy lands, high mountains, the northern coast and ice caves.")                                                                                                                                                   \
	X(kTipInCombat, "Fighting.")                                                                                                                                                                                                              \
	X(kTipInCombatNotAllowed, "Fighting.\n\nNever holds while Allow during combat is off.")                                                                                                                                                   \
	X(kTipWeaponDrawn, "A weapon, spell or fists at the ready.")                                                                                                                                                                              \
	X(kTipInWater, "Any water underfoot, however shallow.")                                                                                                                                                                                   \
	X(kTipSwimming, "Swimming only, not wading.")                                                                                                                                                                                             \
	X(kTipSleeping, "Going to bed, in it or getting up.")                                                                                                                                                                                     \
	X(kPhraseTown, "a town")                                                                                                                                                                                                                  \
	X(kPhraseCastle, "a castle")                                                                                                                                                                                                              \
	X(kPhrasePlayerHome, "a player home")                                                                                                                                                                                                     \
	X(kPhraseDungeon, "a dungeon")                                                                                                                                                                                                            \
	X(kPhraseIndoors, "an interior")                                                                                                                                                                                                          \
	X(kPhraseRain, "rain")                                                                                                                                                                                                                    \
	X(kPhraseSnow, "snow")                                                                                                                                                                                                                    \
	X(kPhraseWarmRegion, "a warm region")                                                                                                                                                                                                     \
	X(kPhraseCoolRegion, "a cool region")                                                                                                                                                                                                     \
	X(kPhraseFreezingRegion, "a freezing region")                                                                                                                                                                                             \
	X(kPhraseInCombat, "in combat")                                                                                                                                                                                                           \
	X(kPhraseWeaponDrawn, "at the ready")                                                                                                                                                                                                     \
	X(kPhraseInWater, "in water")                                                                                                                                                                                                             \
	X(kPhraseSwimming, "swimming")                                                                                                                                                                                                            \
	X(kPhraseSleeping, "sleeping")                                                                                                                                                                                                            \
	X(kClauseState, "{0}")                                                                                                                                                                                                                    \
	X(kClauseNotState, "not {0}")                                                                                                                                                                                                             \
	X(kClauseYouState, "while you are {0}")                                                                                                                                                                                                   \
	X(kClauseYouNotState, "while you are not {0}")                                                                                                                                                                                            \
	X(kClauseLocationType, "in {0}")                                                                                                                                                                                                          \
	X(kClauseNotLocationType, "not in {0}")                                                                                                                                                                                                   \
	X(kClausePlace, "at {0}")                                                                                                                                                                                                                 \
	X(kClauseNotPlace, "not at {0}")                                                                                                                                                                                                          \
	X(kClauseWeather, "in {0}")                                                                                                                                                                                                               \
	X(kClauseNotWeather, "not in {0}")                                                                                                                                                                                                        \
	X(kClauseTime, "from {0} to {1}")                                                                                                                                                                                                         \
	X(kClauseNotTime, "not from {0} to {1}")                                                                                                                                                                                                  \
	X(kClauseAnyHour, "at any hour")                                                                                                                                                                                                          \
	X(kClauseNotAnyHour, "not at any hour")                                                                                                                                                                                                   \
	X(kClauseSpell, "while {0} is on")                                                                                                                                                                                                        \
	X(kClauseNotSpell, "not while {0} is on")                                                                                                                                                                                                 \
	X(kClauseYouSpell, "while {0} is on you")                                                                                                                                                                                                 \
	X(kClauseYouNotSpell, "not while {0} is on you")                                                                                                                                                                                          \
	X(kAppliesOr, " or ")                                                                                                                                                                                                                     \
	X(kAppliesEnd, ".")                                                                                                                                                                                                                       \
	X(kNeverApplies, "Never applies.")                                                                                                                                                                                                        \
	X(kNothingSelected, "{0} (nothing selected)")                                                                                                                                                                                             \
	X(kTimeAm, "{0} AM")                                                                                                                                                                                                                      \
	X(kTimeAmMinutes, "{0}:{1} AM")                                                                                                                                                                                                           \
	X(kTimePm, "{0} PM")                                                                                                                                                                                                                      \
	X(kTimePmMinutes, "{0}:{1} PM")                                                                                                                                                                                                           \
	X(kForSituations, "For {0}")                                                                                                                                                                                                              \
	X(kCopyName, "{0} - Copy")                                                                                                                                                                                                                \
	X(kCopyNameNumbered, "{0} - Copy {1}")                                                                                                                                                                                                    \
	X(kNameEverySituation, "Enter a name for every situation.")                                                                                                                                                                               \
	X(kTwoSituationsNamed, "Two situations are named \"{0}\".")                                                                                                                                                                               \
	X(kRestoreLineRestored, "{0}: restored")                                                                                                                                                                                                  \
	X(kRestoreLineName, "{0}: name reset")                                                                                                                                                                                                    \
	X(kRestoreLineConditions, "{0}: conditions reset")                                                                                                                                                                                        \
	X(kRestoreLineBoth, "{0}: name and conditions reset")

// Why a write to outfits.json or chests.json was refused or failed, shown in the window and
// logged.
#define LIVERY_STRINGS_FILES(X)                                                              \
	X(kOutfitNameTaken, "An outfit named \"{0}\" already exists.")                           \
	X(kOutfitNoLongerExists, "The outfit no longer exists.")                                 \
	X(kChestNameTaken, "A chest named \"{0}\" already exists.")                              \
	X(kChestNoLongerExists, "The chest no longer exists.")                                   \
	X(kFileUnreadable, "Livery could not read {0}, so it changes nothing (see Livery.log).") \
	X(kFileNotWritten, "Livery could not write {0} (see Livery.log).")                       \
	X(kCreatedInGame, "{0} was created in game and cannot be saved.")

// What the window lists: SkyUI's armor types (the Type column), the enchanted mark's fallback
// and the outfit icon colours.
#define LIVERY_STRINGS_ITEMS(X)                               \
	X(kTypeBody, "Body")                                      \
	X(kTypeHead, "Head")                                      \
	X(kTypeHands, "Hands")                                    \
	X(kTypeForearms, "Forearms")                              \
	X(kTypeFeet, "Feet")                                      \
	X(kTypeCalves, "Calves")                                  \
	X(kTypeShield, "Shield")                                  \
	X(kTypeAmulet, "Amulet")                                  \
	X(kTypeRing, "Ring")                                      \
	X(kTypeEars, "Ears")                                      \
	X(kTypeCirclet, "Circlet")                                \
	X(kTypeTail, "Tail")                                      \
	X(kTypeCloak, "Cloak")                                    \
	X(kTypeBack, "Back")                                      \
	X(kEnchanted, "Enchanted")                                \
	X(kColorBoneWhite, "Bone white")                          \
	X(kColorSnowberryRed, "Snowberry red")                    \
	X(kColorSheogorathPink, "Sheogorath pink")                \
	X(kColorDeathbellPurple, "Deathbell purple")              \
	X(kColorJazbayBlue, "Jazbay blue")                        \
	X(kColorColdharbourCyan, "Coldharbour cyan")              \
	X(kColorGlassTeal, "Glass teal")                          \
	X(kColorSprigganGreen, "Spriggan green")                  \
	X(kColorLunaYellow, "Luna yellow")                        \
	X(kColorDwarvenGold, "Dwarven gold")                      \
	X(kColorNordicSilver, "Nordic silver")                    \
	X(kColorSnowberryRedLight, "Snowberry red - Light")       \
	X(kColorSheogorathPinkLight, "Sheogorath pink - Light")   \
	X(kColorDeathbellPurpleLight, "Deathbell purple - Light") \
	X(kColorJazbayBlueLight, "Jazbay blue - Light")           \
	X(kColorColdharbourCyanLight, "Coldharbour cyan - Light") \
	X(kColorGlassTealLight, "Glass teal - Light")             \
	X(kColorSprigganGreenLight, "Spriggan green - Light")     \
	X(kColorLunaYellowLight, "Luna yellow - Light")           \
	X(kColorDwarvenGoldLight, "Dwarven gold - Light")         \
	X(kColorAshGrey, "Ash grey")                              \
	X(kColorSnowberryRedMuted, "Snowberry red - Muted")       \
	X(kColorSheogorathPinkMuted, "Sheogorath pink - Muted")   \
	X(kColorDeathbellPurpleMuted, "Deathbell purple - Muted") \
	X(kColorJazbayBlueMuted, "Jazbay blue - Muted")           \
	X(kColorColdharbourCyanMuted, "Coldharbour cyan - Muted") \
	X(kColorGlassTealMuted, "Glass teal - Muted")             \
	X(kColorSprigganGreenMuted, "Spriggan green - Muted")     \
	X(kColorLunaYellowMuted, "Luna yellow - Muted")           \
	X(kColorDwarvenGoldMuted, "Dwarven gold - Muted")

// What the window is told about the character (the line under their name, who uses an outfit)
// and why an action it asked for did not happen. A summary sentence's values: {0} the outfit,
// {1} the situation, {2} the Gap texts after it.
#define LIVERY_STRINGS_CHARACTER(X)                                                                    \
	X(kNpc, "NPC")                                                                                     \
	X(kSummaryNotDressed, "Not dressed by Livery right now.")                                          \
	X(kSummaryDressed, "Dressed by Livery{0}.")                                                        \
	X(kSummaryWearingAs, "Wearing {0} as {1}{2}.")                                                     \
	X(kSummaryWearingFor, "Wearing {0} for {1}{2}.")                                                   \
	X(kSummaryAssignedAs, "Assigned {0} as {1}{2}.")                                                   \
	X(kSummaryAssignedFor, "Assigned {0} for {1}{2}.")                                                 \
	X(kGapNotWornOne, ", {0} item not worn")                                                           \
	X(kGapNotWornMany, ", {0} items not worn")                                                         \
	X(kGapExtraOne, ", {0} extra item worn")                                                           \
	X(kGapExtraMany, ", {0} extra items worn")                                                         \
	X(kSummarySaveOnly, "{0} dresses as they please. You can save what they wear, but not change it.") \
	X(kUsedByLine, "{0}: {1}")                                                                         \
	X(kNewOutfitName, "New Outfit {0}")                                                                \
	X(kPreviewCharacterGone, "Preview off: the character is gone.")                                    \
	X(kNoLongerHere, "{0} is no longer here.")

// HUD messages, each a whole sentence with "Livery" in it. An outfit update's values: {0} its
// name, {1} its items, {2} those from plugins not loaded, {3} its old name; a move's: {0} the
// items moved, {1} those ticked, {2} the character, {3} the items not moved.
#define LIVERY_STRINGS_HUD(X)                                                                                                                  \
	X(kHudSaved, "Livery: saved {0} ({1} items).")                                                                                             \
	X(kHudUpdated, "Livery: updated {0} ({1} items).")                                                                                         \
	X(kHudUpdatedUnloaded, "Livery: updated {0} ({1} items); {2} items from plugins that are not loaded stay in it.")                          \
	X(kHudUpdatedRenamed, "Livery: updated {0}, renamed from {3} ({1} items).")                                                                \
	X(kHudUpdatedRenamedUnloaded, "Livery: updated {0}, renamed from {3} ({1} items); {2} items from plugins that are not loaded stay in it.") \
	X(kHudMoved, "Livery: moved {0} of {1} items from you to {2}.")                                                                            \
	X(kHudMovedNotAll, "Livery: moved {0} of {1} items from you to {2}; not moved: {3}.")                                                      \
	X(kHudThem, "them")                                                                                                                        \
	X(kHudRemovedOutfit, "Livery: removed \"{0}\".")                                                                                           \
	X(kHudRemovedOutfitFrom, "Livery: removed \"{0}\"; cleared from {1} characters.")                                                          \
	X(kHudCreatedChest, "Livery: created the chest \"{0}\".")                                                                                  \
	X(kHudRemovedChest, "Livery: removed the chest \"{0}\".")                                                                                  \
	X(kHudNotReady, "Livery is not ready.")                                                                                                    \
	X(kHudWindowUnavailable, "Livery: the window is not available.")                                                                           \
	X(kHudNoChildren, "Livery: children are not supported.")                                                                                   \
	X(kHudOwnWardrobe, "Livery: {0} keeps their own wardrobe (see Settings).")                                                                 \
	X(kHudLoadSaveFirst, "Livery: load a save and close the window first.")                                                                    \
	X(kHudUninstallMode, "Livery: uninstall mode is on (Livery.ini: [Uninstall] ReleaseEveryone=1).")

// HUD messages about a dressing. Whole sentences where one says it all; the others are built
// from a main clause ("Livery: Lydia now wears these for Town"), notes that take the sentence so
// far as {0} and add to it (NoteAdded: {1} the count; NoteNotCarried, NoteRefused: {1} item
// names; NoteJoin: {1} a Fallback clause or the next Part) and End, which closes it. In a main
// clause {0} is the character, {1} the situation, or {1} the outfit (QuotedName, OutfitGone or
// DefaultItems) and {2} the situation. A Fallback clause says why a situation's outfit gave way
// to the Default: {0} the outfit, {1} the situation, {2} the character. A Part is one change to
// who wears what for which situations: {0} the outfit and {1} the situations, NoOutfit's {0}
// the situations; ChangeHead takes the character and the parts joined, and DressedNow,
// DressedNowFor ({1} the situation) and OwnClothesNow add what they wear after it.
#define LIVERY_STRINGS_HUD_DRESSING(X)                                                                               \
	X(kHudYouWearOwn, "Livery: you now wear your own clothes.")                                                      \
	X(kHudWearsOwn, "Livery: {0} now wears their own clothes.")                                                      \
	X(kHudYouWearDefault, "Livery: you now wear the default outfit.")                                                \
	X(kHudWearsDefault, "Livery: {0} now wears the default outfit.")                                                 \
	X(kHudYouWearFor, "Livery: you now wear {1} for {2}.")                                                           \
	X(kHudWearsFor, "Livery: {0} now wears {1} for {2}.")                                                            \
	X(kHudFallbackNotLoaded, "no item of {0} for {1} is loaded")                                                     \
	X(kHudFallbackNotCarried, "you carry none of the items of {0} for {1}")                                          \
	X(kHudFallbackRefusedYou, "none of the items of {0} for {1} could be put on you")                                \
	X(kHudFallbackRefused, "none of the items of {0} for {1} could be put on {2}")                                   \
	X(kHudFallbackAlone, "Livery: {0}")                                                                              \
	X(kHudWillWearDefault, "Livery: {0} will wear these by default; dressed now.")                                   \
	X(kHudWillWearDefaultFor, "Livery: {0} will wear these by default; dressed now for {1}.")                        \
	X(kHudYouWearThese, "Livery: you now wear these")                                                                \
	X(kHudYouWearTheseFor, "Livery: you now wear these for {1}")                                                     \
	X(kHudWearsThese, "Livery: {0} now wears these")                                                                 \
	X(kHudWearsTheseFor, "Livery: {0} now wears these for {1}")                                                      \
	X(kHudWearsOutfit, "Livery: {0} now wears {1}")                                                                  \
	X(kHudWearsOutfitFor, "Livery: {0} now wears {1} for {2}")                                                       \
	X(kHudDefaultItems, "the default items")                                                                         \
	X(kHudOwnClothes, "Livery: {0}: own clothes now")                                                                \
	X(kHudNoteAdded, "{0}, {1} added")                                                                               \
	X(kHudNoteNotCarried, "{0}; not carried: {1}")                                                                   \
	X(kHudNoteRefused, "{0}; could not be put on: {1}")                                                              \
	X(kHudNoteJoin, "{0}; {1}")                                                                                      \
	X(kHudEnd, "{0}.")                                                                                               \
	X(kHudChangeHead, "Livery: {0}: {1}")                                                                            \
	X(kHudPartFor, "{0} for {1}")                                                                                    \
	X(kHudPartNoOutfit, "no outfit for {0} any more")                                                                \
	X(kHudPartNoLonger, "{0} no longer for {1}")                                                                     \
	X(kHudDressedNow, "{0}. Dressed now")                                                                            \
	X(kHudDressedNowFor, "{0}. Dressed now for {1}")                                                                 \
	X(kHudOwnClothesNow, "{0}. Own clothes now")                                                                     \
	X(kHudChildrenNotDressed, "Livery: children cannot be dressed.")                                                 \
	X(kHudDead, "Livery: {0} is dead.")                                                                              \
	X(kHudReleased, "Livery: released {0} characters.")                                                              \
	X(kHudReleasedSome, "Livery: released {0} of {1} characters; the others are out of reach and keep their items.") \
	X(kHudRestoredYours, "Livery: restored your original outfit.")                                                   \
	X(kHudRestored, "Livery: restored {0}'s original outfit.")                                                       \
	X(kHudCreatedSituation, "Livery: created the situation \"{0}\".")                                                \
	X(kHudRemovedSituation, "Livery: removed the situation \"{0}\".")                                                \
	X(kHudRemovedSituationFrom, "Livery: removed the situation \"{0}\"; cleared from {1} characters.")               \
	X(kHudRestoredPresets, "Livery: restored the preset situations.")                                                \
	X(kKeyNumber, "Key {0}")

// The settings page on SKSE Menu Framework, drawn in the framework's font (Settings.cpp draws
// it in English while that font lacks the language's letters). Its tooltips end in
// kSetDefault's line after a blank one. LanguageName is the language's name in English
// letters, for kSetFontHint, which is always shown in English.
#define LIVERY_STRINGS_SETTINGS(X)                                                                                                                                                                                                                                                                           \
	X(kLanguageName, "English")                                                                                                                                                                                                                                                                              \
	X(kSetFontHint, "To show this page in {0}, turn on {0} in the font settings of SKSE Menu Framework.")                                                                                                                                                                                                    \
	X(kSetDefault, "Default: {0}")                                                                                                                                                                                                                                                                           \
	X(kSetOn, "ON")                                                                                                                                                                                                                                                                                          \
	X(kSetOff, "OFF")                                                                                                                                                                                                                                                                                        \
	X(kSetNone, "None")                                                                                                                                                                                                                                                                                      \
	X(kNo, "No")                                                                                                                                                                                                                                                                                             \
	X(kSetGeneral, "General")                                                                                                                                                                                                                                                                                \
	X(kSetImmersion, "Immersion")                                                                                                                                                                                                                                                                            \
	X(kSetWindow, "Window")                                                                                                                                                                                                                                                                                  \
	X(kSetSystem, "System")                                                                                                                                                                                                                                                                                  \
	X(kSetOptionalFeatures, "Optional Features")                                                                                                                                                                                                                                                             \
	X(kSetHotkey, "Hotkey")                                                                                                                                                                                                                                                                                  \
	X(kSetPressKey, "Press a key (Esc cancels)")                                                                                                                                                                                                                                                             \
	X(kSetHotkeyTip, "Opens Livery for the NPC under your crosshair, or for yourself if no NPC is targeted.")                                                                                                                                                                                                \
	X(kSetControllerHotkey, "Controller hotkey")                                                                                                                                                                                                                                                             \
	X(kSetControllerTip, "Opens Livery from your controller, as the hotkey does.\nThe first list is a button to hold with it.")                                                                                                                                                                              \
	X(kPadStart, "Start")                                                                                                                                                                                                                                                                                    \
	X(kPadBack, "Back")                                                                                                                                                                                                                                                                                      \
	X(kPadUp, "D-pad Up")                                                                                                                                                                                                                                                                                    \
	X(kPadDown, "D-pad Down")                                                                                                                                                                                                                                                                                \
	X(kPadLeft, "D-pad Left")                                                                                                                                                                                                                                                                                \
	X(kPadRight, "D-pad Right")                                                                                                                                                                                                                                                                              \
	X(kSetSituationNotifications, "Show situation notifications")                                                                                                                                                                                                                                            \
	X(kSetSituationNotificationsTip, "Notifies you when a character changes outfit as a situation starts or ends.")                                                                                                                                                                                          \
	X(kSetEnableChests, "Enable chests")                                                                                                                                                                                                                                                                     \
	X(kSetEnableChestsTip, "Adds a Chests tab to Livery.\nEach chest can hold any number of items, including several for the same slot.\nLoad a chest on the Dress page to choose between variants.")                                                                                                        \
	X(kSetEnableCustomSituations, "Enable custom situations")                                                                                                                                                                                                                                                \
	X(kSetEnableCustomSituationsTip, "Lets you create your own situations and edit the preset ones' conditions.\nSituations you made stay in use while this is off.")                                                                                                                                        \
	X(kSetUnusedSlots, "Gear in unused slots")                                                                                                                                                                                                                                                               \
	X(kSetAllowAll, "Allow all")                                                                                                                                                                                                                                                                             \
	X(kSetExceptHeadgear, "Except headgear")                                                                                                                                                                                                                                                                 \
	X(kSetBlockAll, "Block all")                                                                                                                                                                                                                                                                             \
	X(kSetUnusedSlotsTip, "Controls whether NPCs can equip extra gear in slots their Livery outfit leaves empty.\nFor example, with \"{0}\" they can wear a helmet you give them if the outfit has no headgear.\nTheir original outfit never goes back on.\n\n\"{1}\" blocks helmets, hoods, and circlets.") \
	X(kSetFollowers, "Followers and spouse")                                                                                                                                                                                                                                                                 \
	X(kSetFollowersWho, "Your followers and spouse.\nYou can always open Livery for yourself.")                                                                                                                                                                                                              \
	X(kSetFriends, "Friends")                                                                                                                                                                                                                                                                                \
	X(kSetFriendsWho, "NPCs who count you as a friend or better.")                                                                                                                                                                                                                                           \
	X(kSetOtherNpcs, "Other NPCs")                                                                                                                                                                                                                                                                           \
	X(kSetOtherNpcsWho, "Every other adult NPC.")                                                                                                                                                                                                                                                            \
	X(kSetFullManagement, "Full management")                                                                                                                                                                                                                                                                 \
	X(kSetSaveOnly, "Save only")                                                                                                                                                                                                                                                                             \
	X(kSetDisabled, "Disabled")                                                                                                                                                                                                                                                                              \
	X(kSetAccessHelp, "Full management: you can dress them.\nSave only: you can save what they wear as an outfit.\nDisabled: the hotkey does nothing on them.")                                                                                                                                              \
	X(kSetNpcReaction, "NPC reaction")                                                                                                                                                                                                                                                                       \
	X(kSetNpcReactionTip, "With Live Preview on, the NPC turns toward you and greets you when the window opens.")                                                                                                                                                                                            \
	X(kSetTransfer, "Transfer items to NPCs")                                                                                                                                                                                                                                                                \
	X(kSetTransferTip, "On Apply, transfers items from your inventory to the NPC, preserving enchantments and tempering.\n\nOff: the NPC receives plain copies, and you keep yours.")                                                                                                                        \
	X(kSetInventoryOnly, "Dress yourself from inventory only")                                                                                                                                                                                                                                               \
	X(kSetInventoryOnlyTip, "Dressing yourself uses only items you carry.\nNPCs always get what they need, but you can't take those items from them.\n\nOff: missing items are created in your inventory for free, which can break game balance.\nKeeping this on is strongly recommended.")                 \
	X(kSetLivePreviewOnOpen, "Live Preview on open")                                                                                                                                                                                                                                                         \
	X(kSetLivePreviewOnOpenTip, "Starts with Live Preview enabled each time you open Livery.")                                                                                                                                                                                                               \
	X(kSetCameraSpeed, "Preview camera speed")                                                                                                                                                                                                                                                               \
	X(kSetCameraSpeedTip, "Adjusts camera movement speed.\nUse WASD, mouse drag, and scroll wheel to control the camera.")                                                                                                                                                                                   \
	X(kSetListInventory, "List your inventory for NPCs")                                                                                                                                                                                                                                                     \
	X(kSetListInventoryTip, "In an NPC's window, also lists your armor under \"<your name>'s Inventory\".")                                                                                                                                                                                                  \
	X(kSetExcludeEquipped, "Exclude equipped items")                                                                                                                                                                                                                                                         \
	X(kSetExcludeEquippedTip, "Leaves out items you're wearing unless you carry another copy.")                                                                                                                                                                                                              \
	X(kSetExcludeFavorites, "Exclude favorites")                                                                                                                                                                                                                                                             \
	X(kSetExcludeFavoritesTip, "Excludes items marked as favorites.\nQuest items are always excluded.")                                                                                                                                                                                                      \
	X(kSetShowSlotColumn, "Show Slot column")                                                                                                                                                                                                                                                                \
	X(kSetShowSlotColumnTip, "Shows each item's body slot numbers (e.g., 32, 34).")                                                                                                                                                                                                                          \
	X(kSetWindowSkinTip, "Takes effect the next time you open the window.")                                                                                                                                                                                                                                  \
	X(kSetRestorePalette, "Restore original palette")                                                                                                                                                                                                                                                        \
	X(kSetRestorePaletteTitle, "Restore original palette?")                                                                                                                                                                                                                                                  \
	X(kSetRestoreDefaults, "Restore default settings")                                                                                                                                                                                                                                                       \
	X(kSetRestoreDefaultsTip, "Puts every setting on this page back to its default.\nWindow skin keeps your choice.")                                                                                                                                                                                        \
	X(kSetRestoreDefaultsTitle, "Restore default settings?")                                                                                                                                                                                                                                                 \
	X(kSetRestoreDefaultsQuestion, "Restore this page's default settings? Window skin stays unchanged.")                                                                                                                                                                                                     \
	X(kSetReleaseEveryone, "Release everyone")                                                                                                                                                                                                                                                               \
	X(kSetReleaseEveryoneTip, "Takes Livery's outfits off everyone, player included, and forgets their situational outfits.\nDo this before you uninstall Livery.")                                                                                                                                          \
	X(kSetReleaseEveryoneTitle, "Release everyone?")                                                                                                                                                                                                                                                         \
	X(kSetReleaseEveryoneQuestion, "Release all characters dressed by Livery? Do this before uninstalling.")

#define LIVERY_STRINGS(X)          \
	LIVERY_STRINGS_COMMON(X)       \
	LIVERY_STRINGS_DRESS(X)        \
	LIVERY_STRINGS_SAVE(X)         \
	LIVERY_STRINGS_APPEARANCE(X)   \
	LIVERY_STRINGS_OUTFITS(X)      \
	LIVERY_STRINGS_WARDROBE(X)     \
	LIVERY_STRINGS_CHESTS(X)       \
	LIVERY_STRINGS_SITUATIONS(X)   \
	LIVERY_STRINGS_CONDITIONS(X)   \
	LIVERY_STRINGS_FILES(X)        \
	LIVERY_STRINGS_ITEMS(X)        \
	LIVERY_STRINGS_CHARACTER(X)    \
	LIVERY_STRINGS_HUD(X)          \
	LIVERY_STRINGS_HUD_DRESSING(X) \
	LIVERY_STRINGS_SETTINGS(X)

namespace Strings
{
	enum class Text : std::uint16_t
	{
#define LIVERY_STRING_ID(a_id, a_english) a_id,
		LIVERY_STRINGS(LIVERY_STRING_ID)
#undef LIVERY_STRING_ID
	};

	// --- Detail ----------------------------------------------------------------

	namespace Detail
	{
		inline constexpr std::string_view kKeyPrefix = "$Livery_";  // before a text's id in the file

		struct Entry
		{
			std::string_view id;       // as the file names it: the enum's name without its "k"
			const char*      english;  // shown when the file has no text for it
		};

		inline constexpr std::array kEntries{
#define LIVERY_STRING_ENTRY(a_id, a_english) Entry{ std::string_view{ #a_id }.substr(1), a_english },
			LIVERY_STRINGS(LIVERY_STRING_ENTRY)
#undef LIVERY_STRING_ENTRY
		};

		inline constexpr std::size_t kCount = kEntries.size();

		using Table = std::array<const char*, kCount>;

		inline constexpr Table kEnglish{
#define LIVERY_STRING_ENGLISH(a_id, a_english) a_english,
			LIVERY_STRINGS(LIVERY_STRING_ENGLISH)
#undef LIVERY_STRING_ENGLISH
		};

		// The number of values a text names, one more than its highest {n}; nullopt for a brace
		// that is not "{{", "}}" or {n} with n a number up to 99.
		inline std::optional<std::size_t> ValueCount(std::string_view a_text)
		{
			constexpr std::size_t kMaxIndex = 99;
			std::size_t           count = 0;
			for (std::size_t i = 0; i < a_text.size(); ++i) {
				const char c = a_text[i];
				if (c == '}') {
					if (i + 1 < a_text.size() && a_text[i + 1] == '}') {
						++i;
						continue;
					}
					return std::nullopt;
				}
				if (c != '{') {
					continue;
				}
				if (i + 1 < a_text.size() && a_text[i + 1] == '{') {
					++i;
					continue;
				}
				const auto close = a_text.find('}', i + 1);
				if (close == std::string_view::npos || close == i + 1) {
					return std::nullopt;
				}
				std::size_t index = 0;
				for (std::size_t j = i + 1; j < close; ++j) {
					if (a_text[j] < '0' || a_text[j] > '9') {
						return std::nullopt;
					}
					index = index * 10 + static_cast<std::size_t>(a_text[j] - '0');
					if (index > kMaxIndex) {
						return std::nullopt;
					}
				}
				count = (std::max)(count, index + 1);
				i = close;
			}
			return count;
		}

		// The number of values the text at `a_index` is given: those its English names, and for a
		// time of day the hour of 24 too, which only a translation names ("{2}:{1}").
		inline std::size_t ValuesGiven(std::size_t a_index)
		{
			constexpr std::size_t kTimeValues = 3;  // the hour of 12, the minutes, the hour of 24
			switch (static_cast<Text>(a_index)) {
			case Text::kTimeAm:
			case Text::kTimeAmMinutes:
			case Text::kTimePm:
			case Text::kTimePmMinutes:
				return kTimeValues;
			default:
				return ValueCount(kEntries[a_index].english).value_or(0);
			}
		}

		inline void AppendUtf8(std::string& a_out, char32_t a_code)
		{
			if (a_code < 0x80) {
				a_out += static_cast<char>(a_code);
			} else if (a_code < 0x800) {
				a_out += static_cast<char>(0xC0 | (a_code >> 6));
				a_out += static_cast<char>(0x80 | (a_code & 0x3F));
			} else if (a_code < 0x10000) {
				a_out += static_cast<char>(0xE0 | (a_code >> 12));
				a_out += static_cast<char>(0x80 | ((a_code >> 6) & 0x3F));
				a_out += static_cast<char>(0x80 | (a_code & 0x3F));
			} else {
				a_out += static_cast<char>(0xF0 | (a_code >> 18));
				a_out += static_cast<char>(0x80 | ((a_code >> 12) & 0x3F));
				a_out += static_cast<char>(0x80 | ((a_code >> 6) & 0x3F));
				a_out += static_cast<char>(0x80 | (a_code & 0x3F));
			}
		}

		// UTF-16 LE bytes (no byte order mark) as UTF-8; a lone surrogate becomes U+FFFD and an
		// odd last byte is dropped.
		inline std::string Utf16LeToUtf8(std::string_view a_bytes)
		{
			constexpr char32_t kReplacement = 0xFFFD;
			const auto         unitAt = [a_bytes](std::size_t a_index) {
				return static_cast<char32_t>(static_cast<unsigned char>(a_bytes[a_index * 2]) | (static_cast<unsigned char>(a_bytes[a_index * 2 + 1]) << 8));
			};
			const std::size_t units = a_bytes.size() / 2;
			std::string       utf8;
			utf8.reserve(a_bytes.size());
			for (std::size_t i = 0; i < units; ++i) {
				const auto unit = unitAt(i);
				if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < units) {
					if (const auto low = unitAt(i + 1); low >= 0xDC00 && low <= 0xDFFF) {
						AppendUtf8(utf8, 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00));
						++i;
						continue;
					}
				}
				AppendUtf8(utf8, unit >= 0xD800 && unit <= 0xDFFF ? kReplacement : unit);
			}
			return utf8;
		}

		// Whether every byte sequence is well formed UTF-8 (lead and continuation bytes).
		inline bool IsUtf8(std::string_view a_text)
		{
			for (std::size_t i = 0; i < a_text.size();) {
				const auto  lead = static_cast<unsigned char>(a_text[i]);
				std::size_t length = 0;
				if (lead < 0x80) {
					length = 1;
				} else if (lead >= 0xC2 && lead <= 0xDF) {
					length = 2;
				} else if (lead >= 0xE0 && lead <= 0xEF) {
					length = 3;
				} else if (lead >= 0xF0 && lead <= 0xF4) {
					length = 4;
				} else {
					return false;
				}
				if (i + length > a_text.size()) {
					return false;
				}
				for (std::size_t k = 1; k < length; ++k) {
					if ((static_cast<unsigned char>(a_text[i + k]) & 0xC0) != 0x80) {
						return false;
					}
				}
				i += length;
			}
			return true;
		}

		// A text as the file writes it: a line break as "\n", a backslash as "\\" (Unescape reads
		// them back).
		inline std::string Escape(std::string_view a_text)
		{
			std::string text;
			text.reserve(a_text.size());
			for (const char c : a_text) {
				if (c == '\n') {
					text += "\\n";
				} else if (c == '\\') {
					text += "\\\\";
				} else {
					text += c;
				}
			}
			return text;
		}

		// A text as the file writes it, with "\n" and "\\" read.
		inline std::string Unescape(std::string_view a_text)
		{
			std::string text;
			text.reserve(a_text.size());
			for (std::size_t i = 0; i < a_text.size(); ++i) {
				if (a_text[i] == '\\' && i + 1 < a_text.size() && (a_text[i + 1] == 'n' || a_text[i + 1] == '\\')) {
					text += a_text[i + 1] == 'n' ? '\n' : '\\';
					++i;
				} else {
					text += a_text[i];
				}
			}
			return text;
		}

		// What a file gives: a text for each id it translates, the English kept for the rest.
		struct Translation
		{
			std::vector<std::string> texts = std::vector<std::string>(kCount);  // at each Text's index; empty keeps the English
			std::size_t              read = 0;                                  // texts taken from the file
			std::vector<std::string> problems;                                  // a sentence each, for the log
		};

		// Reads a whole translation file's bytes. A file that is neither UTF-16 LE with its byte
		// order mark nor UTF-8 gives nothing, with one problem.
		inline Translation Translate(std::string_view a_bytes)
		{
			constexpr std::string_view kUtf16Bom = "\xFF\xFE";
			constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

			Translation result;
			std::string decoded;
			if (a_bytes.starts_with(kUtf16Bom)) {
				decoded = Utf16LeToUtf8(a_bytes.substr(kUtf16Bom.size()));
			} else {
				decoded = a_bytes.starts_with(kUtf8Bom) ? a_bytes.substr(kUtf8Bom.size()) : a_bytes;
				if (!IsUtf8(decoded)) {
					result.problems.push_back("The file is neither UTF-16 LE with a byte order mark nor UTF-8, so it was not read.");
					return result;
				}
			}

			std::unordered_map<std::string_view, std::size_t> indexOf;
			for (std::size_t i = 0; i < kCount; ++i) {
				indexOf.emplace(kEntries[i].id, i);
			}
			std::string_view rest = decoded;
			while (!rest.empty()) {
				const auto end = rest.find('\n');
				auto       line = rest.substr(0, end);
				rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
				if (line.ends_with('\r')) {
					line.remove_suffix(1);
				}
				if (!line.starts_with('$')) {
					continue;
				}
				const auto tab = line.find('\t');
				const auto start = tab == std::string_view::npos ? tab : line.find_first_not_of('\t', tab);
				const auto key = line.substr(0, tab);
				if (start == std::string_view::npos || !key.starts_with(kKeyPrefix)) {
					continue;
				}
				const auto found = indexOf.find(key.substr(kKeyPrefix.size()));
				if (found == indexOf.end()) {
					result.problems.push_back(std::format("{} is not a text Livery has; ignored.", key));
					continue;
				}
				auto       text = Unescape(line.substr(start));
				const auto values = ValueCount(text);
				if (!values || *values > ValuesGiven(found->second)) {
					result.problems.push_back(std::format("{} has a brace or value its English text has not; the English is kept.", key));
					continue;
				}
				auto& slot = result.texts[found->second];
				result.read += slot.empty() ? 1 : 0;
				slot = std::move(text);
			}
			return result;
		}

		// The first code point of `a_text` past ASCII, 0 for none. Contract: `a_text` is UTF-8 (IsUtf8).
		inline char32_t FirstNonAscii(std::string_view a_text)
		{
			for (std::size_t i = 0; i < a_text.size(); ++i) {
				const auto lead = static_cast<unsigned char>(a_text[i]);
				if (lead < 0x80) {
					continue;
				}
				const std::size_t length = lead >= 0xF0 ? 4 : (lead >= 0xE0 ? 3 : 2);
				char32_t          code = lead & (0x7F >> length);
				for (std::size_t k = 1; k < length && i + k < a_text.size(); ++k) {
					code = (code << 6) | (static_cast<unsigned char>(a_text[i + k]) & 0x3F);
				}
				return code;
			}
			return 0;
		}

		// Whether a language name can be part of a file name: letters, digits and "_" only.
		inline bool IsLanguageName(std::string_view a_name)
		{
			return !a_name.empty() && std::ranges::all_of(a_name, [](char a_c) {
				return (a_c >= 'A' && a_c <= 'Z') || (a_c >= 'a' && a_c <= 'z') || (a_c >= '0' && a_c <= '9') || a_c == '_';
			});
		}

		// A text cut at its values, for Strings::Pieces: runs of words ("{{" and "}}" read as
		// braces) and the values' indexes, in order; an empty run is left out.
		struct Cut
		{
			std::string words;       // when `value` is negative
			int         value = -1;  // {n}: n
		};

		// Contract: `a_text` passes ValueCount, as every text Load keeps does.
		inline std::vector<Cut> CutAtValues(std::string_view a_text)
		{
			std::vector<Cut> cuts;
			std::string      words;
			const auto       endWords = [&] {
				if (!words.empty()) {
					cuts.push_back({ .words = std::move(words) });
					words.clear();
				}
			};
			for (std::size_t i = 0; i < a_text.size(); ++i) {
				const char c = a_text[i];
				if ((c == '{' || c == '}') && i + 1 < a_text.size() && a_text[i + 1] == c) {
					words += c;
					++i;
				} else if (c == '{') {
					const auto close = a_text.find('}', i + 1);
					endWords();
					int index = 0;
					for (std::size_t j = i + 1; j < close; ++j) {
						index = index * 10 + (a_text[j] - '0');
					}
					cuts.push_back({ .value = index });
					i = close;
				} else {
					words += c;
				}
			}
			endWords();
			return cuts;
		}
	}

	// --- Strings ---------------------------------------------------------------

	// Reads Livery_<language>.txt from Data/Interface/Translations, `a_language` empty for the
	// game's own (its sLanguage); without the file every text stays English.
	// Contract: call once, at kDataLoaded, before anything shows text. Get may run on any thread
	// meanwhile: until the read is done it hands out the English.
	void Load(std::string_view a_language);

	// The text in the chosen language. The pointer stays valid for the whole game. Safe from any thread.
	const char* Get(Text a_text);

	// The text as Livery ships it, in English, whatever the chosen language: for the log, and
	// for a page whose font lacks the language's letters. Safe from any thread.
	inline const char* English(Text a_text)
	{
		return Detail::kEnglish[static_cast<std::size_t>(a_text)];
	}

	// A letter of the chosen language past ASCII (the first in the file's texts, in the
	// table's order), for testing whether a font draws the language; 0 while every text is
	// ASCII, English included. Safe from any thread.
	char32_t Probe();

	// A part of a text cut at its values (Pieces): words, or where value `value` goes.
	struct Piece
	{
		const char* words = nullptr;  // when `value` is negative
		int         value = -1;
	};

	// The text in the chosen language cut at its {0}, {1}..., for a line that draws a value in a
	// look of its own (an outfit's name after its icon). Empty until Load is done. The pieces
	// stay valid for the whole game. Safe from any thread.
	std::span<const Piece> Pieces(Text a_text);

	// A text named where it is defined and read where it is used, in the chosen language: a
	// constant that stands for a const char*.
	struct Ref
	{
		Text text;

		operator const char*() const { return Get(text); }  // NOLINT(google-explicit-constructor): stands in for a text constant
	};

	// The text with its {0}, {1}... filled by `a_args`, as many as its English names. Safe from any thread.
	template <class... Args>
	std::string Format(Text a_text, const Args&... a_args)
	{
		return std::vformat(Get(a_text), std::make_format_args(a_args...));
	}
}
