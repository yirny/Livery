// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
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
// change to Request::onSave, Manage Outfits' Update and Remove the outfit to
// Request::onManageUpdate and onManageRemove, Apply and Restore Original Outfit go to
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
	// Default and the situations: the actor's "wears it for" rows. Never stored in the library.
	constexpr const char* kSituationGroup = "Situation";

	struct CategoryOption
	{
		std::string group;             // kClassGroup or kSituationGroup
		std::string name;              // the category's name as the library stores it; a row's name for kSituationGroup
		bool        selected = false;  // kClassGroup: initial state of its tick on the Create tab
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

	// What one row of the Save page's "wears it for" field holds for the actor now.
	struct RowAssignment
	{
		bool          taken = false;  // the row has an outfit
		std::uint32_t outfit = 0;     // its library id; 0 with `taken` for a default of pieces that are not a saved outfit
	};

	// What the Save page hands over: the outfit to write, then the rows of "wears it for"
	// (indices into Request::assigned) to change for the actor.
	struct SaveRequest
	{
		std::uint32_t            outfit = 0;  // the outfit the Update tab replaces; 0 for Create
		std::string              name;        // trimmed, non-empty
		int                      gender = -1;
		std::vector<std::size_t> pieces;      // ticked indices into Request::pieces, ascending, never empty
		std::vector<std::string> categories;  // the ticked categories (kClassGroup), in the order of Request::categories, maybe none
		std::vector<std::size_t> assign;      // rows that get the outfit, ascending; a row taken by another outfit only after Replace
		std::vector<std::size_t> remove;      // Update only: rows that had the outfit and are unticked, ascending
	};

	struct Request
	{
		std::string        actorName;          // the title line names them
		std::string        actorSummary;       // shown greyed under the title line; "" for none
		std::string        outfitName;         // the library outfit of PieceGroup::kLiveryOutfit's heading
		std::uint32_t      wornOutfit = 0;     // that outfit's library id, which the Apply popup offers to update; 0 for none
		std::string        wornSituation;      // what it is worn for: a situation's name, "" for the default
		std::vector<Piece> pieces;             // shown as a checkbox list; Save and Apply need at least one ticked
		bool               previewOn = false;  // Live Preview is on as the window opens; off, the window freezes the game
		std::string        previewNote;        // why Live Preview cannot be on, shown over its greyed tick; "" when it can
		std::string        suggestedName;      // initial text of the name field
		// One run per group, in the groups' order: kClassGroup's categories, then kSituationGroup
		// with one entry per row of `assigned`, in its order. The outfit lists tag and sort by
		// them; the Save page ticks only kClassGroup's.
		std::vector<CategoryOption> categories;
		std::vector<ExistingOutfit> existingOutfits;       // listed on the Load page and the Update tab's selection, in display order
		int                         suggestedGender = -1;  // initial gender choice: -1 unisex, 0 male, 1 female
		std::vector<std::string>    situations;            // the rows of "wears it for" after Default
		std::vector<RowAssignment>  assigned;              // one per row: Default, then `situations`
		// Apply moves PieceGroup::kPlayer pieces to the NPC; off, the NPC gets copies.
		bool movesFromPlayer = true;

		// Runs on the game thread when the user saves on the Save page: after Create,
		// or after confirming the Update tab's popup. It must end by calling Close()
		// (done) or SetError() (keep the window open).
		std::function<void(const SaveRequest& save)> onSave;

		// Runs on the game thread when the user applies the ticked pieces, with their indices
		// into `pieces` (ascending, never empty) and `outfit`: with `updateWorn` (Yes in the
		// Apply popup) `wornOutfit`, which the ticks are to replace the pieces of first; else
		// the outfit last loaded, or `wornOutfit`, when the ticks are exactly its pieces, or 0.
		// It must end by calling Close() or SetError().
		std::function<void(const std::vector<std::size_t>& pieces, std::uint32_t outfit, bool updateWorn)> onApply;

		// Runs on the game thread when the user presses Restore Original Outfit. Empty
		// when Livery has no pieces on the character, which hides the button. It must end
		// by calling Close() or SetError().
		std::function<void()> onRestore;

		// Runs on the game thread when the user confirms Manage Outfits' Update: `edit` holds
		// the outfit, its new name, gender, pieces and Armor Classes, and no rows. The window
		// stays open: it must end by calling SetOutfits() with the library as written, or
		// SetError() when the write was refused or failed. Empty greys Update.
		std::function<void(const SaveRequest& edit)> onManageUpdate;

		// Runs on the game thread when the user confirms Manage Outfits' Remove of library
		// outfit `outfit`; `shown` are the Inventory page's ticks (indices into `pieces`), which
		// the preview shows once the outfit is gone. As onManageUpdate, it must end by calling
		// SetOutfits() or SetError(). Empty greys Remove.
		std::function<void(std::uint32_t outfit, const std::vector<std::size_t>& shown)> onManageRemove;

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

	// What SetOutfits replaces in the open window's Request, as Open had them.
	struct OutfitList
	{
		std::vector<ExistingOutfit> existingOutfits;  // indices into Request::pieces as the window has them
		std::vector<CategoryOption> categories;
		std::vector<RowAssignment>  assigned;
		std::string                 actorSummary;
	};

	void Open(Request request);
	void Close();
	void SetError(const std::string& message);
	bool IsOpen();

	// Ends a Manage Outfits change the library took: the window shows `list` and lets the
	// user edit again. The pages' selections, the loaded outfit and the Update tab's outfit
	// are found again by id (none when gone, the Update tab then left), each list keeps its
	// search and sort, the Inventory page drops ticks on pieces it no longer lists, and
	// Manage Outfits' edits start again from the library, or the preview shows the
	// Inventory page's ticks when its outfit is gone. The heading of the pieces Livery put on
	// from Request::wornOutfit takes the outfit's name now; when the outfit is gone they
	// join the other worn pieces and Apply no longer offers to update it.
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
