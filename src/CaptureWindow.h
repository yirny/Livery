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
// Contract: Open, Close and SetError run on the game thread; FeedInput and
// Draw run in the capture menu's PostDisplay (Hooks.cpp); QueueInput runs on
// the game thread from the input dispatch hook. All state is guarded by one
// mutex. Nothing here touches the outfit library or the assignments: the Save
// page's Create and Update hand the outfit and the rows to change to
// Request::onSave, Apply and Restore Original Outfit go to Request::onApply and
// Request::onRestore, each piece tick to Request::onPieceToggled, each outfit
// selected on the Load page and the Update tab's selection to
// Request::onPreviewPieces and the Live Preview tick to Request::onPreviewOn /
// onPreviewOff, in an SKSE task.
namespace CaptureWindow
{
	// Group of the class categories (ArmorInfo's SkyUI classes), shown as a row of
	// checkboxes rather than a dropdown.
	constexpr const char* kClassGroup = "Class";

	struct CategoryOption
	{
		std::string group;             // label of the field it is listed in
		std::string name;              // the category's name as the library stores it
		bool        selected = false;  // initial state of its tick
	};

	// Where a piece comes from: the Capture page lists the pieces under one heading per
	// group, in this order.
	enum class PieceGroup
	{
		kLiveryOutfit,  // worn, put on by Livery from the library outfit Request::outfitName names
		kWorn,          // the other worn pieces
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

	// An outfit already in the library, listed on the Load page and the Update tab's selection.
	struct ExistingOutfit
	{
		std::string              name;
		std::vector<std::string> categories;  // the offered categories it is in; the Update tab starts its ticks from them
		std::vector<std::size_t> pieces;      // indices into Request::pieces of its loaded pieces, ascending, no repeats
		// Its pieces whose plugin is not loaded. They have no armor form to list, so an
		// update leaves them in the outfit.
		std::size_t   unloadedPieces = 0;
		std::uint32_t id = 0;       // the library's id, handed back in SaveRequest::outfit
		int           gender = -1;  // -1 unisex, 0 male, 1 female
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
		std::vector<std::string> categories;  // ticked, in the order of Request::categories, maybe none
		std::vector<std::size_t> assign;      // rows that get the outfit, ascending; a row taken by another outfit only after Replace
		std::vector<std::size_t> remove;      // Update only: rows that had the outfit and are unticked, ascending
	};

	struct Request
	{
		std::string                 actorName;             // the title line names them
		std::string                 actorSummary;          // shown greyed under the title line; "" for none
		std::string                 outfitName;            // the library outfit of PieceGroup::kLiveryOutfit's heading
		std::vector<Piece>          pieces;                // shown as a checkbox list; Save and Apply need at least one ticked
		bool                        previewOn = false;     // Live Preview is on as the window opens; off, the window freezes the game
		std::string                 previewNote;           // why Live Preview cannot be on, shown over its greyed tick; "" when it can
		std::string                 suggestedName;         // initial text of the name field
		std::vector<CategoryOption> categories;            // offered as the Class checkboxes
		std::vector<ExistingOutfit> existingOutfits;       // listed on the Load page and the Update tab's selection, in display order
		int                         suggestedGender = -1;  // initial gender choice: -1 unisex, 0 male, 1 female
		std::vector<std::string>    situations;            // the rows of "wears it for" after Default
		std::vector<RowAssignment>  assigned;              // one per row: Default, then `situations`

		// Runs on the game thread when the user saves on the Save page: after Create,
		// or after confirming the Update tab's popup. It must end by calling Close()
		// (done) or SetError() (keep the window open).
		std::function<void(const SaveRequest& save)> onSave;

		// Runs on the game thread when the user presses Apply, with the ticked indices
		// into `pieces` (ascending, never empty) and the id of the outfit last loaded
		// when the ticks are exactly its pieces, else 0. It must end by calling Close()
		// or SetError().
		std::function<void(const std::vector<std::size_t>& pieces, std::uint32_t outfit)> onApply;

		// Runs on the game thread when the user presses Restore Original Outfit. Empty
		// when Livery has no pieces on the character, which hides the button. It must end
		// by calling Close() or SetError().
		std::function<void()> onRestore;

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

	void Open(Request request);
	void Close();
	void SetError(const std::string& message);
	bool IsOpen();

	// Also moves the game's cursor from the mouse moves, except during a drag
	// over the game, whose moves go to onViewInput instead: the game itself sees
	// no input while the window is open (Hooks.cpp).
	void QueueInput(RE::InputEvent* const* events);

	// Contract: after ImGui_ImplWin32_NewFrame and before ImGui::NewFrame.
	void FeedInput();

	// Contract: between ImGui::NewFrame and ImGui::Render.
	void Draw();
}
