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
// Contract: Open, Close and SetError run on the game thread; Draw runs on the
// render thread (DXGI Present hook); QueueInput runs on the game thread from
// the input dispatch hook. All state is guarded by one mutex. Nothing here
// calls Tailor: Save hands the typed name to Request::onSave in an SKSE task.
namespace CaptureWindow
{
	struct Request
	{
		std::string              actorName;      // shown as the title line
		std::vector<std::string> pieceNames;     // shown as a read-only list
		std::string              suggestedName;  // initial text of the name field

		// Runs on the game thread with the trimmed, non-empty name. It must end
		// by calling Close() (done) or SetError() (keep the window open).
		std::function<void(const std::string& name)> onSave;
	};

	void Open(Request request);
	void Close();
	void SetError(const std::string& message);
	bool IsOpen();

	void QueueInput(RE::InputEvent* const* events);
	void Draw();
}
