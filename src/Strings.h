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
// its English has not, keeps the English.

// --- Table -------------------------------------------------------------------

// X(id, English text), one list per part of Livery, joined in LIVERY_STRINGS. The id names the
// text in the file without its "k".

// Words the window uses on several pages: page names, buttons, reasons a button is greyed.
#define LIVERY_STRINGS_COMMON(X)                                                                   \
	X(kPageDress, "Dress")                                                                         \
	X(kPageWardrobe, "Wardrobe")                                                                   \
	X(kPageSituations, "Situations")                                                               \
	X(kPageChests, "Chests")                                                                       \
	X(kPageAppearance, "Appearance")                                                               \
	X(kApply, "Apply")                                                                             \
	X(kBack, "Back")                                                                               \
	X(kCancel, "Cancel")                                                                           \
	X(kConfirm, "Confirm")                                                                         \
	X(kCreate, "Create")                                                                           \
	X(kExit, "Exit")                                                                               \
	X(kRestore, "Restore")                                                                         \
	X(kUpdate, "Update")                                                                           \
	X(kSaveChanges, "Save Changes")                                                                \
	X(kDiscardChanges, "Discard Changes")                                                          \
	X(kApplying, "Applying...")                                                                    \
	X(kSaving, "Saving...")                                                                        \
	X(kEditHeading, "Edit {0}")                                                                    \
	X(kQuotedName, "\"{0}\"")                                                                      \
	X(kNewName, "New name: {0}")                                                                   \
	X(kOutfitIcon, "Outfit Icon")                                                                  \
	X(kOutfitName, "Outfit Name")                                                                  \
	X(kColumnName, "Name")                                                                         \
	X(kColumnType, "Type")                                                                         \
	X(kColumnSlot, "Slot")                                                                         \
	X(kNeedName, "Enter a name.")                                                                  \
	X(kNeedPiece, "Select at least one item.")                                                     \
	X(kNeedOutfit, "Select an outfit.")                                                            \
	X(kNeedChest, "Select a chest.")                                                               \
	X(kNoOutfits, "No outfits in the Wardrobe.")                                                   \
	X(kNoLoadedPiece, "None of its items is loaded.")                                              \
	X(kNoChests, "No chests yet.")                                                                 \
	X(kNoChanges, "No changes to save.")                                                           \
	X(kWornAlready, "Already worn as is.")                                                         \
	X(kNotCarriedHint, "You don't carry this item.\nDressing from inventory only leaves it off.")  \
	X(kOutfitGone, "an outfit no longer in the Wardrobe")                                          \
	X(kItemsNotSaved, "items that are not a saved outfit")                                         \
	X(kChestsSubtitle, "Items in a chest can be loaded on the {0} page.")                          \
	X(kAppearanceSubtitle, "Other settings are in Livery.ini or on the SKSE Menu Framework page.") \
	X(kOk, "OK")                                                                                   \
	X(kYes, "Yes")                                                                                 \
	X(kRemove, "Remove")                                                                           \
	X(kSave, "Save")                                                                               \
	X(kEdit, "Edit")                                                                               \
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
	X(kUseSelectedItems, "Use Selected Items")

// The Wardrobe page, its popups and the Apply popup it shares with Create and Apply. An Apply
// question's values: {0} the outfit, {1} the situation (Offer) or the character (ApplyAs,
// ApplyFor), {2} the situation; "Create" and "Edits" forms save the outfit or the changes first.
#define LIVERY_STRINGS_WARDROBE(X)                                                                                                       \
	X(kSelectOutfitToEdit, "Select an outfit to edit")                                                                                   \
	X(kItemsSelectedUnloaded, "{0} of {1} items selected, {2} not loaded")                                                               \
	X(kUnsavedChangesTo, "Unsaved changes to {0}.")                                                                                      \
	X(kWardrobeApplyOffer, "Dress {0} in this outfit as their {1} outfit, or make it their default outfit.")                             \
	X(kWardrobeApplyDefault, "Dress {0} in this outfit as their default outfit.")                                                        \
	X(kWardrobeApplyFor, "Dress {0} in this outfit for {1}.")                                                                            \
	X(kChangesLost, "Changes to {0} will be lost.")                                                                                      \
	X(kDiscardEdits, "Discard changes")                                                                                                  \
	X(kKeepEditing, "Keep editing")                                                                                                      \
	X(kItemsChange, "Items: {0} to {1}")                                                                                                 \
	X(kIconChanged, "Icon: changed")                                                                                                     \
	X(kRemoveItemsTitle, "Remove items from the outfit?")                                                                                \
	X(kUnselectedRemoved, "These unselected items will be removed from {0}:")                                                            \
	X(kKeepItemsHint, "To keep these items in the outfit, choose {0} instead.")                                                          \
	X(kSaveChangesTo, "Save your changes to {0}?")                                                                                       \
	X(kSaveAndApply, "Save and Apply")                                                                                                   \
	X(kSavedFirstHelp, "Your changes are saved first.")                                                                                  \
	X(kOfferMake, "Make {0} their new {1} outfit?")                                                                                      \
	X(kOfferMakeCreate, "Create {0}, then make it their new {1} outfit?")                                                                \
	X(kOfferMakeEdits, "Save changes and make {0} their new {1} outfit?")                                                                \
	X(kOfferSwitch, "Switch to {0} for {1}?")                                                                                            \
	X(kOfferSwitchCreate, "Create {0}, then switch to it for {1}?")                                                                      \
	X(kOfferSwitchEdits, "Save changes and switch to {0} for {1}?")                                                                      \
	X(kOfferAdd, "Add {0} to their {1} outfits and dress them in it?")                                                                   \
	X(kOfferAddCreate, "Create {0}, add it to their {1} outfits, and dress them in it?")                                                 \
	X(kOfferAddEdits, "Save changes, add {0} to their {1} outfits, and dress them in it?")                                               \
	X(kApplyAs, "Apply {0} as {1}'s {2}?")                                                                                               \
	X(kApplyAsCreate, "Create {0}, then apply it as {1}'s {2}?")                                                                         \
	X(kApplyAsEdits, "Save changes and apply {0} as {1}'s {2}?")                                                                         \
	X(kApplyFor, "Apply {0} to {1} for {2}?")                                                                                            \
	X(kApplyForCreate, "Create {0}, then apply it to {1} for {2}?")                                                                      \
	X(kApplyForEdits, "Save changes and apply {0} to {1} for {2}?")                                                                      \
	X(kWearingFor, "{0} is wearing {1} for {2}.")                                                                                        \
	X(kWearingOneOf, "{0} is wearing {1}, one of their {2} outfits.")                                                                    \
	X(kApplyAsDefault, "Apply as Default")                                                                                               \
	X(kAsDefaultHelp, "Sets \"{0}\" as {1}'s default outfit.\nTheir current situational outfit stays equipped.")                         \
	X(kAsDefaultHelpReplacing, "Sets \"{0}\" as {1}'s default outfit, replacing {2}.\nTheir current situational outfit stays equipped.") \
	X(kThisReplaces, "This replaces {0}.")                                                                                               \
	X(kRemoveFromWardrobe, "Remove {0} from the Wardrobe?")                                                                              \
	X(kUsedBy, "Used by:")                                                                                                               \
	X(kRemoveFallback, "Situations using it fall back to the default outfit. Default outfits made from it keep their items.")            \
	X(kNotCarriedFrom, "You don't carry these items from {0}:")                                                                          \
	X(kOnlyCarriedEquipped, "Only carried items will be equipped.")                                                                      \
	X(kCarryNone, "You carry none of these items.")

// The Chests page, its New chest and Remove popups, and the chest editor.
#define LIVERY_STRINGS_CHESTS(X)                                                        \
	X(kNoOtherChests, "No other chests.")                                               \
	X(kChestsCount, "{0} chests")                                                       \
	X(kNameNewChest, "Name the new chest.")                                             \
	X(kChestName, "Chest Name")                                                         \
	X(kCreateNewChest, "Create New Chest...")                                           \
	X(kSelectChestToOpen, "Select a chest to open")                                     \
	X(kTakenOutOfChest, "Taken Out of Chest")                                           \
	X(kNoItemsToPutIn, "No items to put in.")                                           \
	X(kEmpty, "Empty.")                                                                 \
	X(kDressWithChest, "Dress with Chest...")                                           \
	X(kNotWhileInventoryOnly, "Not available while dressing from your inventory only.") \
	X(kRemoveChest, "Remove {0}?")                                                      \
	X(kChestItems, "Items")                                                             \
	X(kInChest, "In Chest")

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

#define LIVERY_STRINGS(X)        \
	LIVERY_STRINGS_COMMON(X)     \
	LIVERY_STRINGS_DRESS(X)      \
	LIVERY_STRINGS_SAVE(X)       \
	LIVERY_STRINGS_APPEARANCE(X) \
	LIVERY_STRINGS_OUTFITS(X)    \
	LIVERY_STRINGS_WARDROBE(X)   \
	LIVERY_STRINGS_CHESTS(X)     \
	LIVERY_STRINGS_FILES(X)

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
				if (!values || *values > *ValueCount(kEntries[found->second].english)) {
					result.problems.push_back(std::format("{} has a brace or value its English text has not; the English is kept.", key));
					continue;
				}
				auto& slot = result.texts[found->second];
				result.read += slot.empty() ? 1 : 0;
				slot = std::move(text);
			}
			return result;
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
