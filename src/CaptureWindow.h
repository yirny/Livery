// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "OutfitRows.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace RE
{
	class InputEvent;
}

// The "name this outfit" window drawn with ImGui (see CaptureWindow.cpp).
//
// Contract: Open, Close, SetError, SetOutfits and RefusePreview run on the game
// thread; FeedInput and Draw run in the capture menu's PostDisplay (Hooks.cpp);
// QueueInput runs on the game thread from the input dispatch hook. All state is
// guarded by one mutex. Nothing here touches the outfit library or the
// assignments: the Save page's Create and Update hand the outfit and the rows to
// change to Request::onSave, Manage Wardrobe's Update and Remove the outfit to
// Request::onManageUpdate and onManageRemove and its Apply the outfit and a row to onManageApply,
// Manage Situations' Save Changes, Remove (a situation) and Restore Presets go to
// Request::onSituationsSave, onSituationRemove and onRestorePresets,
// Apply and Restore Outfit go to
// Request::onApply and Request::onRestore, each piece tick to
// Request::onPieceToggled, each outfit selected on the outfit pages to
// Request::onPreviewPieces and the Live Preview tick to Request::onPreviewOn /
// onPreviewOff, in an SKSE task.
namespace CaptureWindow
{
	// The groups of Request::categories, in their order (docs/outfit-management-ui.md §1).
	// The categories (Library::Categories: Light, Heavy, Clothing) are the Save page's row of
	// checkboxes.
	constexpr const char* kClassGroup = "Category";
	// Default and the situations: the actor's "Situations for #{Name}" rows. Never stored in the library.
	constexpr const char* kSituationGroup = "Situation";

	struct CategoryOption
	{
		std::string group;  // kClassGroup or kSituationGroup
		std::string name;   // the category's name as the library stores it; a row's name for kSituationGroup
	};

	// Where a piece comes from: the Capture page lists the pieces under one heading per
	// group, in this order.
	enum class PieceGroup
	{
		kLiveryOutfit,  // worn, put on by Livery from the library outfit Request::outfitName names
		kNotInLibrary,  // worn, put on by Livery, not a library outfit (outfit id 0, or the outfit left the library); "Not in Wardrobe"
		kUnmanaged,     // worn, not put on by Livery
		kCarried,       // in the actor's inventory, not worn
		kPlayer,        // an NPC's window only: in the player's inventory, not the NPC's
		kNotCarried     // only a library outfit has it; listed while that outfit is loaded
	};

	struct Piece
	{
		std::string   name;              // the armor's display name
		bool          selected = false;  // initial state of its checkbox
		std::uint32_t slots = 0;         // body slot mask; ticking the piece unticks others sharing a slot
		PieceGroup    group = PieceGroup::kCarried;
		std::string   type;            // the Type column (ArmorInfo::TypeOf); "" for none
		int           typeOrder = 0;   // what the Type column sorts by
		std::string   armorClass;      // the Class column (ArmorInfo::ClassOf)
		int           classOrder = 0;  // what the Class column sorts by
		// The ticked piece with the lowest rank decides the Create tab's suggested category and
		// gender: the index of its first slot of Body, Head, Hands, Feet, Circlet; -1 for none.
		int           suggestRank = -1;
		bool          femaleOnly = false;  // none of its armor addons has a male model, one has a female model
		std::string   icon;                // PieceIcons' key of the icon left of its name; "" for none
		std::uint32_t iconColor = 0;       // the icon's tint, 0xRRGGBB
	};

	// One frame of mouse and key input over the game rather than the window,
	// for the preview camera.
	struct ViewInput
	{
		float lookX = 0.0f;        // pixels of a left drag, right
		float lookY = 0.0f;        // pixels of a left drag, down
		float panX = 0.0f;         // pixels of a right drag, right
		float panY = 0.0f;         // pixels of a right drag, down
		float moveForward = 0.0f;  // -1, 0 or 1 from S / W
		float moveRight = 0.0f;    // -1, 0 or 1 from A / D
		float zoomSteps = 0.0f;    // wheel steps, positive towards the view
		bool  slow = false;        // Shift held
		float seconds = 0.0f;      // the frame's length
	};

	// An outfit already in the library, listed on the outfit pages.
	struct ExistingOutfit
	{
		std::string              name;
		std::vector<std::string> categories;  // the stored ones; the Update tab starts its ticks from them, and those the window does not offer stay unshown
		std::vector<std::size_t> pieces;      // indices into Request::pieces of its loaded pieces, ascending, no repeats
		// Its pieces whose plugin is not loaded. They have no armor form to list, so an
		// update leaves them in the outfit.
		std::size_t   unloadedPieces = 0;
		std::uint32_t id = 0;       // the library's id, handed back in SaveRequest::outfit
		int           gender = -1;  // -1 unisex, 0 male, 1 female
		// Who has it among Livery's assignments in this save, one line each, for Remove's
		// popup: "Lydia: Default, Town", the actor's FormID when not in memory.
		std::vector<std::string> users;
	};

	// One of the actor's rows as they have it now (docs/situation-management-ui.md §1): the
	// Default, or a situation.
	struct RowAssignment
	{
		OutfitRows::Row row;
		bool            pieces = false;  // the Default only: it has armor not in the wardrobe, worn while the row wears no outfit
	};

	// A row of Request::assigned: the Default or a situation not removed, as situations.json
	// has it (docs/situation-management-ui.md §2).
	struct SituationInfo
	{
		Situations::Situation situation = Situations::Situation::kDefault;  // its key, which the handlers take
		std::string           name;
		std::string           description;  // "" for none
		std::string           conditions;   // when it applies, shown read only on Manage Situations
		// Who has outfits on its list in this save, one line each, for the Remove situation? popup:
		// "Lydia: 2 outfit(s)", the actor's FormID when not in memory. Empty for the Default.
		std::vector<std::string> holders;
	};

	// A situation's name and description as Manage Situations leaves them.
	struct SituationEdit
	{
		Situations::Situation situation = Situations::Situation::kDefault;
		std::string           name;         // trimmed, non-empty
		std::string           description;  // trimmed, maybe empty
	};

	// One of the actor's rows as Manage Situations leaves it.
	struct RowEdit
	{
		Situations::Situation situation = Situations::Situation::kDefault;
		OutfitRows::Row       row;
	};

	// What Manage Situations' Save Changes hands over.
	struct SituationSave
	{
		std::vector<SituationEdit> edits;                      // the situations whose name or description changed; empty for none
		std::vector<RowEdit>       rows;                       // every row of Request::assigned when any changed, else empty
		bool                       dropDefaultPieces = false;  // with `rows`: the Default's pieces not in the wardrobe go
		// The outfit whose details show, as indices into Request::pieces: a preview begun again
		// after the redress shows it; nullopt for what the actor wears then.
		std::optional<std::vector<std::size_t>> previewPieces;
	};

	// What the Save page hands over: the outfit to write, then the rows of "Situations for #{Name}"
	// (indices into Request::assigned) to change for the actor. Manage Wardrobe's Update hands
	// over the outfit alone, and its Update and Apply the one row in `assign` it dresses for.
	struct SaveRequest
	{
		std::uint32_t            outfit = 0;  // the outfit the Update tab replaces; 0 for Create
		std::string              name;        // trimmed, non-empty
		int                      gender = -1;
		std::vector<std::size_t> pieces;      // ticked indices into Request::pieces, ascending, never empty
		std::vector<std::string> categories;  // the ticked categories (kClassGroup), in the order of Request::categories, maybe none
		std::vector<std::size_t> assign;      // rows that get the outfit, ascending; a row another outfit is worn for only after Confirm
		std::vector<std::size_t> remove;      // Update only: rows that had the outfit and are unticked, ascending
		// Create and Apply: once saved and given its rows, the outfit goes on as the Inventory
		// page's Apply puts it on, the pieces in `fromPlayer` (those of `pieces` the window lists
		// under Player's Inventory) moved first. Always false and empty for Update.
		bool                     apply = false;
		std::vector<std::size_t> fromPlayer;
	};

	struct Request
	{
		std::string        actorName;          // the title line names them
		std::string        playerName;         // PieceGroup::kPlayer's heading names them; "" for "Player"
		std::string        actorSummary;       // shown greyed under the title line; "" for none
		std::string        outfitName;         // the library outfit of PieceGroup::kLiveryOutfit's heading
		std::uint32_t      wornOutfit = 0;     // that outfit's library id, which the Apply popup offers to update; 0 for none
		std::string        wornSituation;      // what it is worn for: a situation's name, "" for the default
		std::vector<Piece> pieces;             // shown as a checkbox list; Save and Apply need at least one ticked
		bool               previewOn = false;  // Live Preview is on as the window opens; off, the window freezes the game
		std::string        previewNote;        // why Live Preview cannot be on, shown over its greyed tick; "" when it can
		std::string        suggestedName;      // initial text of the name field
		// What `wornOutfit` is worn for as a row's key, kDefault for the default: Manage
		// Wardrobe's Apply offers that situation.
		Situations::Situation wornFor = Situations::Situation::kDefault;
		// One run per group, in the groups' order: kClassGroup's categories, then kSituationGroup
		// with one entry per row of `assigned`, in its order. The outfit lists tag and sort by
		// them; the Save page ticks only kClassGroup's.
		std::vector<CategoryOption> categories;
		std::vector<ExistingOutfit> existingOutfits;  // listed on the Load page and the Update tab's selection, in display order
		std::vector<SituationInfo>  situations;       // one per row of `assigned`: the Default, then the situations not removed
		std::vector<RowAssignment>  assigned;         // one per row, in `situations`' order
		std::vector<std::string>    restoreChanges;   // what Restore Presets would change, one line each; empty while every preset is as installed
		// Apply moves PieceGroup::kPlayer pieces to the NPC; off, the NPC gets copies.
		bool movesFromPlayer = true;
		// The player's favorites are left out of PieceGroup::kPlayer (quest items always are).
		bool protectsFavorites = true;

		// Runs on the game thread when the user saves on the Save page: after Create or
		// Create and Apply, or after confirming the Update tab's popup. It must end by calling Close()
		// (done) or SetError() (keep the window open).
		std::function<void(const SaveRequest& save)> onSave;

		// Runs on the game thread when the user applies the ticked pieces, with their indices
		// into `pieces` (ascending, never empty), those of them the window lists under
		// Player's Inventory now (PieceGroup::kPlayer) in `fromPlayer`, and `outfit`: with
		// `updateWorn` (Yes in the Apply popup) `wornOutfit`, which the ticks are to replace the
		// pieces of first; else the outfit last loaded, or `wornOutfit`, when the ticks are
		// exactly its pieces, or 0. It must end by calling Close() or SetError().
		std::function<void(const std::vector<std::size_t>& pieces, const std::vector<std::size_t>& fromPlayer, std::uint32_t outfit, bool updateWorn)>
			onApply;

		// Runs on the game thread when the user presses Restore Outfit. Empty
		// when Livery has no pieces on the character, which hides the button. It must end
		// by calling Close() or SetError().
		std::function<void()> onRestore;

		// Runs on the game thread when the user confirms Manage Wardrobe's Update: `edit` holds
		// the outfit, its new name, gender, pieces and categories; no row changes. The library
		// write comes first; a refused or failed write assigns nothing. The window stays open: it
		// must end by calling SetOutfits() with the library as written, or SetError(). With
		// `apply` (Update and Apply, from Apply to #{Name} with unsaved edits) it ends by
		// calling Close() and dresses the actor as onManageApply does, for the one row in
		// `edit.assign`. Empty greys Update.
		std::function<void(const SaveRequest& edit, bool apply)> onManageUpdate;

		// Runs on the game thread when the user confirms Manage Wardrobe' Remove of library
		// outfit `outfit`. As onManageUpdate, it must end by calling SetOutfits() or SetError().
		// Empty greys Remove.
		std::function<void(std::uint32_t outfit)> onManageRemove;

		// Runs on the game thread when the user presses Manage Wardrobe's "Apply to #{Name}"
		// with no unsaved edits: library outfit `outfit`, as the library has it, goes to the
		// actor for row `row` of `assigned` (the situation they wear another outfit for now,
		// when the user agreed in Apply's popup, the one they wear it for, or the Default), and
		// they are dressed for their situations now. It must end by calling Close() or
		// SetError(). Empty greys the button.
		std::function<void(std::uint32_t outfit, std::size_t row)> onManageApply;

		// Run on the game thread from Manage Situations (docs/situation-management-ui.md §5, §6):
		// onSituationsSave with Save Changes' edits, onSituationRemove with the situation Remove
		// Situation's popup confirmed, onRestorePresets after Restore Presets' popup. Each writes
		// situations.json first, when it has anything to write, and a refused or failed write
		// changes no row; each must end by calling SetOutfits() or SetError(). Empty greys the
		// button.
		std::function<void(const SituationSave& save)>       onSituationsSave;
		std::function<void(Situations::Situation situation)> onSituationRemove;
		std::function<void()>                                onRestorePresets;

		// Run on the game thread when the user ticks Live Preview: onPreviewOn
		// with the ticked indices into `pieces` (ascending, maybe empty) returns
		// "" once the preview is on, or why it cannot be (the tick then stays
		// greyed for the rest of the window); onPreviewOff ends it. Set both or
		// neither; neither greys the tick. The window keeps the game running while
		// the preview is on and freezes it while it is off.
		std::function<std::string(const std::vector<std::size_t>& pieces)> onPreviewOn;
		std::function<void()>                                              onPreviewOff;

		// Runs on the game thread each time the user ticks (true) or unticks
		// (false) pieces[index], in click order, Live Preview on or off. Empty
		// for nothing to do.
		std::function<void(std::size_t index, bool ticked)> onPieceToggled;

		// Runs on the game thread when the live preview should show exactly these
		// indices into `pieces` (ascending, maybe empty): an outfit selected on the
		// Load or Update Existing page, then the ticked pieces again when the user
		// deselects it or goes back without loading it, Live Preview on or off.
		// Empty for nothing to do.
		std::function<void(const std::vector<std::size_t>& pieces)> onPreviewPieces;

		// Runs on the game thread for each frame with input over the game rather
		// than the window. Keys count only while no item of the window is active
		// (a name being typed, a popup open); a drag only when it began over the
		// game. Only while Live Preview is on, when the window starts on the left
		// of the screen. Empty for none.
		std::function<void(const ViewInput& input)> onViewInput;

		// Runs on the game thread once the window has closed, whatever closed it:
		// Cancel, the game hiding the menu, a load, or onSave / onUpdate calling
		// Close(). Empty for nothing to do.
		std::function<void()> onClose;
	};

	// The captured actor's pieces after they were redressed while the window is open with
	// Live Preview on: what the Inventory page lists, as Open would list it now.
	struct ActorPieces
	{
		std::vector<PieceGroup>  groups;          // one per Request::pieces
		std::vector<std::size_t> ticked;          // the listed pieces they wear (wigs left out, as at Open), ascending
		std::string              outfitName;      // as Request::outfitName
		std::uint32_t            wornOutfit = 0;  // as Request::wornOutfit
		std::string              wornSituation;   // as Request::wornSituation
		// As Request::wornFor.
		Situations::Situation wornFor = Situations::Situation::kDefault;
	};

	// What SetOutfits replaces in the open window's Request, as Open had them.
	struct OutfitList
	{
		std::vector<ExistingOutfit> existingOutfits;  // indices into Request::pieces as the window has them
		std::vector<CategoryOption> categories;
		std::vector<RowAssignment>  assigned;
		std::vector<SituationInfo>  situations;
		std::vector<std::string>    restoreChanges;
		std::string                 actorSummary;
		// Set when the change redressed the captured actor with Live Preview on, whose preview
		// the handler began again; the Inventory page then follows what they wear.
		std::optional<ActorPieces> actorPieces;
	};

	void Open(Request request);
	void Close();
	void SetError(const std::string& message);
	bool IsOpen();

	// Ends a Manage Wardrobe or Manage Situations change the library, situations.json or the
	// actor's rows took: the window shows `list` and lets the user edit again. The pages'
	// selections, the loaded outfit and the Update tab's outfit are found again by id (none
	// when gone, the Update tab then left), the rows' ticks of the Save page and Manage
	// Wardrobe follow their situations (a removed one's go, a restored one's start unticked),
	// the Update tab's "Situations for #{Name}" ticks follow the rows that changed for its outfit, each
	// list keeps its search, sort and folded headings, the Inventory page drops ticks on
	// pieces it no longer lists, Manage Situations' edits start again from the rows and the
	// situations (its selected situation and folded headings found again by key), and Manage
	// Wardrobe's edits start again from the library, or
	// the preview shows the Inventory page's ticks when its outfit is gone. The heading of the pieces Livery put on
	// from Request::wornOutfit takes the outfit's name now; when the outfit is gone they
	// join the other worn pieces and Apply no longer offers to update it.
	// With `list.actorPieces` the Inventory page lists the pieces as they say, its ticks become
	// the pieces worn, the loaded outfit is let go, and the preview is left as the handler
	// began it.
	// Contract: `list.actorPieces->groups` has one group per Request::pieces.
	void SetOutfits(OutfitList list);

	// The live preview could not start again after a change made while it was on: the
	// Live Preview tick goes off and greys with `reason`, and the game freezes, as when
	// it cannot start at Open.
	void RefusePreview(const std::string& reason);

	// Also moves the game's cursor from the mouse moves, except during a drag
	// over the game, whose moves go to onViewInput instead: the game itself sees
	// no input while the window is open (Hooks.cpp).
	void QueueInput(RE::InputEvent* const* events);

	// Contract: after ImGui_ImplWin32_NewFrame and before ImGui::NewFrame.
	void FeedInput();

	// Contract: between ImGui::NewFrame and ImGui::Render.
	void Draw();
}
