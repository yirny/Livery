// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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
// mutex. Nothing here calls Tailor: Create New hands the typed name to
// Request::onSave, and Update Existing the chosen outfit to Request::onUpdate,
// in an SKSE task.
namespace CaptureWindow
{
	struct CategoryOption
	{
		std::string group;             // small heading shown above the first option of each group
		std::string name;              // the category's name as Tailor shows it
		bool        selected = false;  // initial state of its tick
	};

	struct Piece
	{
		std::string name;      // the armor's display name
		bool        selected;  // initial state of its checkbox
	};

	// An outfit already in Tailor's library, listed on the Update Existing page.
	struct ExistingOutfit
	{
		std::string              name;
		std::vector<std::string> categories;  // the known categories it is in; shown ticked and locked
		std::vector<std::string> pieces;      // display names of its loaded pieces
	};

	struct Request
	{
		std::string                 actorName;        // shown as the title line
		std::string                 actorSummary;     // shown greyed under the title line; "" for none
		std::vector<Piece>          pieces;           // shown as a checkbox list; both actions need at least one ticked
		std::string                 suggestedName;    // initial text of the name field
		std::vector<CategoryOption> categories;       // offered in the multi-select dropdown
		std::vector<ExistingOutfit> existingOutfits;  // listed on the Update Existing page, in display order

		// Runs on the game thread with the trimmed, non-empty name, the indices
		// into `pieces` the user left ticked (ascending, never empty) and the
		// categories the user ticked (in the order of `categories`). It must end
		// by calling Close() (done) or SetError() (keep the window open).
		std::function<void(const std::string& name, const std::vector<std::size_t>& pieces,
			const std::vector<std::string>& categories)>
			onSave;

		// Runs on the game thread once the user confirmed replacing the pieces of
		// the outfit `name` (one of existingOutfits) with the ticked `pieces`
		// (as in onSave) and adding it to `categories`: only those the user newly
		// ticked, never one it is already in (in the order of `categories`, maybe
		// empty). It must end by calling Close() or SetError().
		std::function<void(const std::string& name, const std::vector<std::size_t>& pieces,
			const std::vector<std::string>& categories)>
			onUpdate;
	};

	void Open(Request request);
	void Close();
	void SetError(const std::string& message);
	bool IsOpen();

	void QueueInput(RE::InputEvent* const* events);

	// Contract: after ImGui_ImplWin32_NewFrame and before ImGui::NewFrame.
	void FeedInput();

	// Contract: between ImGui::NewFrame and ImGui::Render.
	void Draw();
}
