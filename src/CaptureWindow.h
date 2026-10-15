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
// mutex. Nothing here calls Tailor: Save hands the typed name to
// Request::onSave in an SKSE task.
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

	struct Request
	{
		std::string                 actorName;      // shown as the title line
		std::vector<Piece>          pieces;         // shown as a checkbox list; Save needs at least one ticked
		std::string                 suggestedName;  // initial text of the name field
		std::vector<CategoryOption> categories;     // offered in the multi-select dropdown

		// Runs on the game thread with the trimmed, non-empty name, the indices
		// into `pieces` the user left ticked (ascending, never empty) and the
		// categories the user ticked (in the order of `categories`). It must end
		// by calling Close() (done) or SetError() (keep the window open).
		std::function<void(const std::string& name, const std::vector<std::size_t>& pieces,
			const std::vector<std::string>& categories)>
			onSave;
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
