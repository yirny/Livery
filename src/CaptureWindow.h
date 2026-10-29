// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "OutfitIcons.h"
#include "OutfitRows.h"
#include "sarrai/FreeCamera.h"

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
// thread; FeedInput and Draw run in the overlay menu's PostDisplay (sarrai-common, Overlay.cpp);
// QueueInput runs on the game thread from the input dispatch hook. All state is
// guarded by one mutex. Nothing here touches the outfit library or the
// assignments: the Save page's Create and Update hand the outfit (and Create and Apply
// the row it is worn for) to Request::onSave, the Wardrobe page's Update and Remove the outfit to
// Request::onManageUpdate and onManageRemove and its Apply the outfit and a row to onManageApply,
// the Situations page's Save Changes, Remove (a situation) and Restore Presets go to
// Request::onSituationsSave, onSituationRemove and onRestorePresets,
// Apply and Restore go to
// Request::onApply and Request::onRestore, each piece tick to
// Request::onPieceToggled, each outfit selected on the outfit pages to
// Request::onPreviewPieces and the Live Preview tick to Request::onPreviewOn /
// onPreviewOff, in an SKSE task.
namespace CaptureWindow
{
	// The group of Request::categories (docs/outfit-management-ui.md §1): Default and the
	// situations, the actor's "Situations for #{Name}" rows. Never stored in the library.
	constexpr const char* kSituationGroup = "Situation";

	struct CategoryOption
	{
		std::string group;  // kSituationGroup
		std::string name;   // the row's name
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
		std::string   type;           // the Type column (ArmorInfo::TypeOf); "" for none
		int           typeOrder = 0;  // what the Type column sorts by
		std::string   slotText;       // the Slot column: its body slots by number, lowest first ("32, 34")
		int           slotOrder = 0;  // what the Slot column sorts by: its lowest slot
		std::string   icon;           // PieceIcons' key of the icon left of its name; "" for none
		std::uint32_t iconColor = 0;  // the icon's tint, 0xRRGGBB
		std::uint32_t armor = 0;      // the armor's FormID, which the items a row's outfit leaves off name (OutfitRows::Exclusion)
	};

	// One frame of mouse and key input over the game rather than the window,
	// for the preview camera.
	using ViewInput = FreeCamera::ViewInput;

	// An outfit already in the library, listed on the outfit pages.
	struct ExistingOutfit
	{
		std::string              name;
		OutfitIcons::Icon        icon;
		std::vector<std::size_t> pieces;  // indices into Request::pieces of its loaded pieces, ascending, no repeats
		// Its pieces whose plugin is not loaded. They have no armor form to list, so an
		// update leaves them in the outfit.
		std::size_t   unloadedPieces = 0;
		std::uint32_t id = 0;  // the library's id, handed back in SaveRequest::outfit
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
		std::string           conditions;  // when it applies, shown read only on the Situations page
		// Who has outfits on its list in this save, one line each, for the Remove situation? popup:
		// "Lydia: 2 outfit(s)", the actor's FormID when not in memory. Empty for the Default.
		std::vector<std::string> holders;
	};

	// A situation's name as the Situations page leaves it.
	struct SituationEdit
	{
		Situations::Situation situation = Situations::Situation::kDefault;
		std::string           name;  // trimmed, non-empty
	};

	// One of the actor's rows as the Situations page leaves it.
	struct RowEdit
	{
		Situations::Situation situation = Situations::Situation::kDefault;
		OutfitRows::Row       row;
	};

	// What the Situations page's Save Changes hands over.
	struct SituationSave
	{
		std::vector<SituationEdit> edits;                      // the situations whose name changed; empty for none
		std::vector<RowEdit>       rows;                       // every row of Request::assigned when any changed, else empty
		bool                       dropDefaultPieces = false;  // with `rows`: the Default's pieces not in the wardrobe go
		// The outfit whose details show, less the items the row they were opened from leaves
		// off, as indices into Request::pieces: a preview begun again after the redress shows
		// it; nullopt for what the actor wears then.
		std::optional<std::vector<std::size_t>> previewPieces;
	};

	// What the Save page and the Wardrobe page's Update hand over: the outfit to write, and for
	// Create and Apply or the Wardrobe page's Update and Apply the one row (an index into
	// Request::assigned) that gets it and is dressed for.
	struct SaveRequest
	{
		std::uint32_t            outfit = 0;  // the outfit the Update tab replaces; 0 for Create
		std::string              name;        // trimmed, non-empty
		std::vector<std::size_t> pieces;      // ticked indices into Request::pieces, ascending, never empty
		// The outfit's icon: the Wardrobe page's, or the Create tab's for a new outfit; nullopt
		// keeps the one it has (the Save page's Update tab).
		std::optional<OutfitIcons::Icon> icon;
		// The row the outfit goes to, as the Wardrobe page's Apply gives it (the situation the
		// actor wears another outfit for now, after the user agreed, else the Default); nullopt
		// for a save that changes no row.
		std::optional<std::size_t> applyRow;
		// Create and Apply: those of `pieces` the window lists under Player's Inventory, which
		// move first; those that do not move stay out of the dressing. Empty otherwise.
		std::vector<std::size_t> fromPlayer;
		// With `applyRow` from the Wardrobe page: the armor (FormIDs) the outfit leaves off on that
		// row, the items unticked in its piece list; empty wears all its items, as Create and
		// Apply always does.
		std::vector<std::uint32_t> excluded;
	};

	struct Request
	{
		std::string        actorName;          // the title line names them
		std::string        playerName;         // PieceGroup::kPlayer's heading names them; "" for "Player"
		std::string        actorSummary;       // shown greyed under the title line; "" for none
		bool               dressed = false;    // Livery has pieces on the character, which shows Restore
		std::size_t        piecesNotWorn = 0;  // of those, the ones they wear no copy of now (taken back in the trade menu, taken off)
		std::size_t        extraWorn = 0;      // worn armor Livery did not put on, while it has pieces on them
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
		// kSituationGroup, one entry per row of `assigned`, in its order. The outfit lists tag
		// and sort by them.
		std::vector<CategoryOption> categories;
		std::vector<ExistingOutfit> existingOutfits;  // listed on the outfit pages and the Update tab, in display order
		std::vector<SituationInfo>  situations;       // one per row of `assigned`: the Default, then the situations not removed
		std::vector<RowAssignment>  assigned;         // one per row, in `situations`' order
		std::vector<std::string>    restoreChanges;   // what Restore Presets would change, one line each; empty while every preset is as installed
		// Apply moves PieceGroup::kPlayer pieces to the NPC; off, the NPC gets copies.
		bool movesFromPlayer = true;
		// The player's favorites are left out of PieceGroup::kPlayer (quest items always are).
		bool protectsFavorites = true;
		// The Inventory page's count line ends in `Add items from Wardrobe...`; off for the
		// player while they dress from their inventory only (Settings::PlayerAddsMissingPieces).
		bool offersWardrobe = true;

		// Runs on the game thread when the user saves on the Save page: after Create, after
		// Create and Apply (its popup first when it gives a situation or replaces the Default),
		// or after confirming the Update tab's popup. The library write comes first; a refused or
		// failed write assigns nothing. Only Create and Apply changes a row (`save.applyRow`).
		// It must end by calling Close() (done) or SetError() (keep the window open).
		std::function<void(const SaveRequest& save)> onSave;

		// Runs on the game thread when the user applies the ticked pieces, with their indices
		// into `pieces` (ascending, never empty), those of them the window lists under
		// Player's Inventory now (PieceGroup::kPlayer) in `fromPlayer`, and `outfit`: with
		// `updateWorn` (Yes in the Apply popup) `wornOutfit`, which the ticks are to replace the
		// pieces of first; else `wornOutfit` when the ticks are all or some of its pieces and
		// nothing else, `excluded` holding the armor (FormIDs) of those left unticked, which it
		// is to leave off on the row it is worn for; else the outfit last loaded when the ticks
		// are exactly its pieces, or 0. The window stays open: it must end by calling
		// SetOutfits(), with the actor's pieces as they are dressed now, or SetError().
		std::function<void(const std::vector<std::size_t>& pieces, const std::vector<std::size_t>& fromPlayer, std::uint32_t outfit, bool updateWorn,
			const std::vector<std::uint32_t>& excluded)>
			onApply;

		// Runs on the game thread when the user presses Restore, shown while
		// `dressed`. Empty hides it. It must end by calling Close() or SetError().
		std::function<void()> onRestore;

		// Runs on the game thread when the user confirms the Wardrobe page's Update: `edit` holds
		// the outfit, its new name and pieces; no row changes. The library
		// write comes first; a refused or failed write assigns nothing. The window stays open: it
		// must end by calling SetOutfits() with the library as written, or SetError(). With
		// `apply` (Update and Apply, from Apply to #{Name} with unsaved edits) it then dresses
		// the actor as onManageApply does, for `edit.applyRow`, and hands
		// SetOutfits() their pieces as they are dressed now. Empty greys Update.
		std::function<void(const SaveRequest& edit, bool apply)> onManageUpdate;

		// Runs on the game thread when the user confirms the Wardrobe page's Remove of library
		// outfit `outfit`. As onManageUpdate, it must end by calling SetOutfits() or SetError().
		// Empty greys Remove.
		std::function<void(std::uint32_t outfit)> onManageRemove;

		// Runs on the game thread when the user presses the Wardrobe page's "Apply to #{Name}"
		// with no unsaved edits: library outfit `outfit`, as the library has it, goes to the
		// actor for row `row` of `assigned` (the situation they wear another outfit for now,
		// when the user agreed in Apply's popup, the one they wear it for, or the Default),
		// leaving `excluded` (armor FormIDs, the items unticked in its piece list) off there,
		// and they are dressed for their situations now. The window stays open: it must end by
		// calling SetOutfits(), with the actor's pieces as they are dressed now, or SetError().
		// Empty greys the button.
		std::function<void(std::uint32_t outfit, std::size_t row, const std::vector<std::uint32_t>& excluded)> onManageApply;

		// Run on the game thread from the Situations page (docs/situation-management-ui.md §5, §6):
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
		// than the window, and for each frame while the preview camera glides.
		// Keys count only while no item of the window is active (a name being
		// typed, a popup open); a drag only when it began over the game. Only
		// while Live Preview is on, when the window starts on the left of the
		// screen. Empty for none.
		std::function<void(const ViewInput& input)> onViewInput;

		// Runs on the game thread once the window has closed, whatever closed it:
		// Exit, the game hiding the menu, a load, or onSave / onRestore calling
		// Close(). Empty for nothing to do.
		std::function<void()> onClose;
	};

	// The captured actor's pieces after they were redressed while the window is open: what the
	// Inventory page lists, as Open would list it now.
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
		bool                        dressed = false;    // as Request::dressed
		std::size_t                 piecesNotWorn = 0;  // as Request::piecesNotWorn
		std::size_t                 extraWorn = 0;      // as Request::extraWorn
		// Set when the change redressed the captured actor (with Live Preview on, the handler
		// began the preview again); the Inventory page then follows what they wear.
		std::optional<ActorPieces> actorPieces;
	};

	void Open(Request request);
	void Close();
	void SetError(const std::string& message);
	bool IsOpen();

	// Ends an Apply, or a Wardrobe page or Situations page change the library,
	// situations.json or the actor's rows took: the window shows `list` and lets the user
	// edit again, Restore shown while `list.dressed`. The pages'
	// selections, the loaded outfit and the Update tab's outfit are found again by id (none
	// when gone; the Update tab is left when the wardrobe is empty), each
	// list keeps its search, sort and folded headings, the Inventory page drops ticks on
	// pieces it no longer lists, the Situations page's edits start again from the rows and the
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
	// no input while the window is open (sarrai-common, Overlay.cpp).
	void QueueInput(RE::InputEvent* const* events);

	// Contract: after ImGui_ImplWin32_NewFrame and before ImGui::NewFrame.
	void FeedInput();

	// Contract: between ImGui::NewFrame and ImGui::Render.
	void Draw();
}
