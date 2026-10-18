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
// mutex. Nothing here touches the outfit library: Create New hands the typed name to
// Request::onSave, Update Existing the chosen outfit to Request::onUpdate,
// Assign and Release to Request::onAssign and Request::onRelease,
// each piece tick to Request::onPieceToggled, each outfit selected on the
// Load and Update Existing pages to Request::onPreviewPieces and the Live
// Preview tick to Request::onPreviewOn / onPreviewOff, in an SKSE task.
namespace CaptureWindow
{
	// Group of the armor-type categories, shown as a row of checkboxes
	// rather than a dropdown.
	constexpr const char* kArmorTypeGroup = "Armor Type";

	struct CategoryOption
	{
		std::string group;             // label of the field it is listed in
		std::string name;              // the category's name as the library stores it
		bool        selected = false;  // initial state of its tick
	};

	struct Piece
	{
		std::string   name;          // the armor's display name
		bool          selected;      // initial state of its checkbox
		std::uint32_t slots = 0;     // body slot mask; ticking the piece unticks others sharing a slot
		bool          owned = true;  // in the actor's inventory; one only a library outfit has is listed while that outfit is loaded
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

	// An outfit already in the library, listed on the Load and Update Existing pages.
	struct ExistingOutfit
	{
		std::string              name;
		std::vector<std::string> categories;  // the offered categories it is in; the Update page starts its ticks from them
		std::vector<std::size_t> pieces;      // indices into Request::pieces of its loaded pieces, ascending, no repeats
		// Its pieces whose plugin is not loaded. They have no armor form to list, so an
		// update leaves them in the outfit.
		std::size_t   unloadedPieces = 0;
		std::uint32_t id = 0;  // the library's id, handed back to onUpdate
	};

	struct Request
	{
		std::string                 actorName;             // shown as the title line
		std::string                 actorSummary;          // shown greyed under the title line; "" for none
		std::vector<Piece>          pieces;                // shown as a checkbox list; both actions need at least one ticked
		bool                        previewOn = false;     // Live Preview is on as the window opens; off, the window freezes the game
		std::string                 previewNote;           // why Live Preview cannot be on, shown over its greyed tick; "" when it can
		std::string                 suggestedName;         // initial text of the name field
		std::vector<CategoryOption> categories;            // offered in the multi-select dropdown
		std::vector<ExistingOutfit> existingOutfits;       // listed on the Update Existing page, in display order
		int                         suggestedGender = -1;  // initial gender choice: -1 unisex, 0 male, 1 female

		// Runs on the game thread with the trimmed, non-empty name, the indices
		// into `pieces` the user left ticked (ascending, never empty), the
		// categories the user ticked (in the order of `categories`) and the chosen
		// gender (-1, 0 or 1). It must end by calling Close() (done) or SetError()
		// (keep the window open).
		std::function<void(const std::string& name, const std::vector<std::size_t>& pieces,
			const std::vector<std::string>& categories, int gender)>
			onSave;

		// Runs on the game thread once the user confirmed replacing the pieces of
		// the outfit `id` (one of existingOutfits) with the ticked `pieces` (as in
		// onSave) and its categories with `categories`: every one ticked on the
		// Update page (in the order of `categories`, maybe empty). It must end by
		// calling Close() or SetError().
		std::function<void(std::uint32_t id, const std::vector<std::size_t>& pieces, const std::vector<std::string>& categories)> onUpdate;

		// Runs on the game thread when the user presses Assign, with the ticked indices
		// into `pieces` (ascending, never empty) and the id of the outfit last loaded
		// when the ticks are exactly its pieces, else 0. It must end by calling Close()
		// or SetError().
		std::function<void(const std::vector<std::size_t>& pieces, std::uint32_t outfit)> onAssign;

		// Runs on the game thread when the user presses Release. Empty when Livery does
		// not dress the character, which greys the button. It must end by calling
		// Close() or SetError().
		std::function<void()> onRelease;

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
