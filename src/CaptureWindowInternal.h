// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The capture window's constants, state and shared helpers, private to CaptureWindow.cpp
// (the window and its tab column, the Inventory and Save pages), OutfitPages.cpp (the
// outfit browser and lists, the Load, Select, Manage Wardrobe and Edit pages) and
// SituationPage.cpp (the Manage Situations page). The helpers are defined in
// CaptureWindow.cpp, the pages in OutfitPages.cpp and SituationPage.cpp; each definition
// carries its comment.
//
// Contract: included after PCH.h. The functions that take a State run with State::mutex
// held: the draw functions in the capture menu's PostDisplay, as CaptureWindow.h describes.

#include "CaptureWindow.h"
#include "OutfitBrowser.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace CaptureWindow::Internal
{
	constexpr std::size_t kNameCapacity = 64;                 // bytes, UTF-8, including the terminator
	constexpr float       kMaxListHeightRatio = 0.6f;         // of the screen height, for every scrolling list
	constexpr const char* kConfirmPopup = "Replace pieces?";  // IDs shared by OpenPopup and BeginPopupModal
	constexpr const char* kReplacePopup = "Replace outfits?";
	constexpr const char* kApplyPopup = "Update the outfit?";
	// Manage Wardrobe' popups.
	constexpr const char* kDiscardPopup = "Discard changes?###discardSelect";
	constexpr const char* kManageConfirmPopup = "Save changes?";
	constexpr const char* kManageApplyPopup = "Apply outfit?";
	constexpr const char* kRemovePopup = "Remove outfit?";
	constexpr const char* kRestorePopup = "Restore original outfit?";
	// The unsaved-changes alert of Manage Wardrobe and Manage Situations (DrawLeavePopup).
	constexpr const char* kLeavePopup = "Unsaved changes";
	// Reasons a button is greyed out, shown on hover (and in the empty name field).
	constexpr const char* kNeedName = "Enter a name.";
	constexpr const char* kNeedPiece = "Tick at least one piece.";
	constexpr const char* kNeedOutfit = "Select an outfit.";
	constexpr const char* kNoOutfits = "No outfits in the wardrobe.";
	constexpr const char* kNoLoadedPiece = "None of its pieces is loaded.";
	constexpr const char* kNoChanges = "No changes to save.";
	// Frames the window's place is pinned after Open or a page change: an
	// auto-resized window fits new content a frame late, and PlaceWindow sees
	// the fitted height a frame after that.
	constexpr int kRecentreFrames = 3;
	// One heading per CaptureWindow::PieceGroup.
	constexpr std::size_t kPieceGroupCount = static_cast<std::size_t>(CaptureWindow::PieceGroup::kNotCarried) + 1;
	using GroupHeadings = std::array<std::string, kPieceGroupCount>;

	// The outfit lists' Sort dropdown, in its order.
	struct SortOption
	{
		const char*            label;
		OutfitBrowser::SortKey key;
	};
	constexpr std::array<SortOption, 4> kSortOptions{ {
		{ "Name", OutfitBrowser::SortKey::kName },
		{ "Gender", OutfitBrowser::SortKey::kGender },
		{ CaptureWindow::kClassGroup, OutfitBrowser::SortKey::kClass },
		{ CaptureWindow::kSituationGroup, OutfitBrowser::SortKey::kSituation },
	} };

	// The outfit browser's Filter dropdown, hidden for now; its code stays, as it is likely
	// to return (docs/outfit-management-ui.md §1).
	constexpr bool kShowFilter = false;

	// The Save page's gender radio buttons, in their order.
	struct GenderOption
	{
		const char* label;
		int         value;  // as the library stores it
	};
	constexpr std::array<GenderOption, 3> kGenderOptions{ {
		{ "Unisex", -1 },
		{ "Male", 0 },
		{ "Female", 1 },
	} };

	// The piece list's columns, in their order (State::sortColumn).
	constexpr int                        kNameColumn = 0;
	constexpr int                        kTypeColumn = 1;
	constexpr int                        kClassColumn = 2;
	constexpr std::array<const char*, 3> kColumnLabels{ "Name", "Type", "Category" };

	// What an outfit row of Manage Situations carries while it is dragged (OfferOutfitDrag).
	constexpr const char* kOutfitPayload = "LiveryOutfit";
	struct OutfitPayload
	{
		int           from = -1;   // the situation it is dragged from: index into request.situations; -1 for the outfit list
		std::uint32_t outfit = 0;  // its library id; 0 for the Default's pieces not in the wardrobe
	};

	// Sound descriptors in Skyrim.esm, whose load index is always 00.
	constexpr RE::FormID kOpenSound = 0x0003F2BD;    // UIMenuBladeOpenSD
	constexpr RE::FormID kOkSound = 0x0003C751;      // UIMenuOKSD
	constexpr RE::FormID kCancelSound = 0x0003C752;  // UIMenuCancelSD
	constexpr RE::FormID kFocusSound = 0x0003C759;   // UIMenuFocus

	enum class Status
	{
		kClosed,
		kEditing,
		kSaving  // Create, Confirm, Apply or Restore Outfit pressed; waiting for the handler to Close() or SetError()
	};

	enum class Page
	{
		kCapture,  // the Inventory page
		kSave,
		kLoad,
		kSelect,
		kManage,     // Manage Wardrobe
		kEdit,       // Manage Wardrobe' piece editor
		kSituations  // Manage Situations
	};

	struct InputRecord
	{
		enum class Kind
		{
			kKey,
			kMouseButton,
			kMouseWheel,
			kChar
		};
		Kind  kind;
		int   code;  // ImGuiKey / mouse button / character
		bool  down;
		float wheel;  // wheel steps
	};

	// --- State ----------------------------------------------------------------

	// One tab of the Save page: what it saves, kept while the window is open. bool[] because
	// Theme::Checkbox needs bool*.
	struct SaveForm
	{
		std::array<char, kNameCapacity> name{};
		std::unique_ptr<bool[]>         classTicked;  // one per request.categories; only the Armor Classes' are used
		int                             gender = -1;  // -1 unisex, 0 male, 1 female
		std::unique_ptr<bool[]>         wearsFor;     // one per row of "Situations for #{Name}" (request.assigned)
	};

	// Manage Wardrobe' edits of the selected outfit, kept while the window is open; selecting
	// another outfit starts them again from the library.
	struct ManageForm
	{
		int                             outfit = -1;  // index into request.existingOutfits; -1 for none selected
		std::array<char, kNameCapacity> name{};
		std::unique_ptr<bool[]>         classTicked;  // one per request.categories; only kClassGroup's are used
		int                             gender = -1;  // -1 unisex, 0 male, 1 female
		std::vector<std::size_t>        pieces;       // its loaded pieces as edited: indices into request.pieces, ascending
		std::unique_ptr<bool[]>         wearsFor;     // one per row of "Situations for #{Name}" (request.assigned), Default first
	};

	// One situation of Manage Situations as edited, in request.situations' order.
	struct SituationEntry
	{
		std::array<char, kNameCapacity> name{};
		std::array<char, kNameCapacity> description{};
		std::string                     nameBefore;      // its name as its field's edit began, put back for an empty or taken one
		OutfitRows::Row                 row;             // the actor's row for it
		bool                            folded = false;  // its heading in the Situation List is folded
	};

	// Manage Situations' edits, kept while the window is open; a change a handler made
	// (SetOutfits) and Discard Changes start them again from the request.
	struct SituationForm
	{
		std::vector<SituationEntry> entries;
		bool                        defaultPieces = false;     // the Default keeps its pieces not in the wardrobe
		int                         selected = -1;             // index into entries; -1 for none
		bool                        details = false;           // the right pane shows the selected one (Show Outfit List not pressed since)
		bool                        scrollToSelected = false;  // the Situation List scrolls to the selected heading on its next draw
		int                         nameEditing = -1;          // the entry whose name field is active: index into entries; -1 for none
		ImGuiID                     nameField = 0;             // that field's ImGui ID
		int                         dragHover = -1;            // the folded heading a drag hovers: index into entries; -1 for none
		double                      dragHoverSince = 0.0;      // since when, ImGui::GetTime() seconds
		float                       outfitPaneHeight = 0.0f;   // the right pane's height with the outfit list, last drawn; the Situation List keeps it
	};

	// Where leaving a page with unsaved edits goes once they are saved or dropped: the
	// window's close (Exit, Escape), or a tab's page.
	struct PendingLeave
	{
		bool exit = false;
		Page page = Page::kCapture;
	};

	struct State
	{
		std::mutex               mutex;
		Status                   status = Status::kClosed;
		CaptureWindow::Request   request;
		std::unique_ptr<bool[]>  pieceTicked;  // one per request.pieces; bool[] because Theme::TickRow needs bool*
		SaveForm                 createForm;
		SaveForm                 updateForm;
		bool                     updateTab = false;     // the Save page shows the Update tab
		int                      chosenOutfit = -1;     // the Update tab's outfit: index into request.existingOutfits, -1 for none yet
		int                      selectingOutfit = -1;  // on the Select page: index into request.existingOutfits, -1 for none
		bool                     openConfirm = false;   // open the Update tab's confirm popup on the next frame
		std::string              situationsLabel;       // "Situations for #{Name}", over the Save page's and Manage Wardrobe's rows
		std::string              error;
		bool                     focusNameField = false;
		std::vector<InputRecord> pending;
		bool                     resetImGui = false;  // set by Open, handled on the next frame
		Page                     page = Page::kCapture;
		std::string              captureTitle;    // the Capture page's title line
		std::string              applyTooltip;    // "Dress #{Name} in the ticked pieces."
		GroupHeadings            headings;        // over each group's pieces (CaptureWindow::PieceGroup)
		std::vector<std::size_t> pieceOrder;      // indices into request.pieces as the list shows them (SortPieces)
		int                      sortColumn = 0;  // the piece list's sorted column: Name, Type or Class
		bool                     sortDescending = false;
		int                      previewedOutfit = -1;   // on the Load page: index into request.existingOutfits, -1 for none
		bool                     loadIntoEdit = false;   // the Load page was opened from the Edit page, whose ticks it adds to
		int                      loadedOutfit = -1;      // the last one loaded: index into request.existingOutfits, -1 for none
		OutfitBrowser::State     loadBrowser;            // search, filter and sort of the Load page's outfit list
		OutfitBrowser::State     selectBrowser;          // the Select page's
		OutfitBrowser::State     manageBrowser;          // Manage Wardrobe'
		bool                     showCategories = true;  // the outfit lists show each outfit's tags (Show Categories)
		ManageForm               manage;
		int                      discardFor = -1;   // the outfit to select once Discard changes is chosen: index, -1 for none
		std::string              manageApplyLabel;  // "Apply to #{Name}"
		std::unique_ptr<bool[]>  editTicked;        // the Edit page's ticks, one per request.pieces
		std::vector<std::size_t> editShown;         // the edited pieces it opened with and those loaded since, so those nobody carries stay listed
		std::string              editTitle;         // "Edit \"#{Outfit}\""
		std::vector<std::string> editOutfitNames;   // the outfits those come from: the edited one, then those loaded that brought one
		std::string              editShownHeading;  // over those nobody carries: "From \"#{Outfit}\", \"#{Loaded}\" (Not Carried)"
		OutfitBrowser::State     situationBrowser;  // Manage Situations' outfit list
		SituationForm            situationForm;
		std::string              situationsTitle;  // "#{Name}'s Situational Outfits"
		// Set while the unsaved-changes alert is open, and while its Save Changes saves.
		std::optional<PendingLeave> leave;
		bool                        leaveAfterSave = false;  // the alert's Save Changes is saving: the leave follows a save that took
		bool                        openLeave = false;       // open the alert on the next frame, in the page's ID scope
		int                         recentreFrames = 0;      // frames left to pin the window to its place
		std::optional<ImVec2>       placeCentre;             // where a pinned window's centre goes; none for the start place
		ImVec2                      windowPos{};             // of the window in the last frame drawn
		ImVec2                      windowSize{};
		bool                        userMoved = false;    // the user moved the window since Open, so turning Live Preview on keeps it there
		unsigned                    session = 0;          // counts Opens, so a Live Preview task queued before a close does nothing
		bool                        previewOn = false;    // Live Preview is on
		bool                        previewBusy = false;  // a Live Preview tick waits for its SKSE task
		bool                        frozeGame = false;    // the window froze the game (Live Preview off), so Close unfreezes it
		int                         viewDragButton = -1;  // ImGui mouse button held since a press over the game; -1 for none
		float                       viewDragX = 0.0f;     // mouse movement since the last frame while viewDragButton is held
		float                       viewDragY = 0.0f;
	};

	// The piece list a page shows (DrawPieceRows): the Inventory page's ticks with the
	// pieces of the outfit last loaded, or the Edit page's own ticks with the edited pieces
	// it opened with. Pieces someone carries are always listed.
	struct PieceList
	{
		bool*                           ticked;   // one per request.pieces (bool* for Theme::TickRow)
		const std::vector<std::size_t>* shown;    // the other pieces listed: indices into request.pieces, ascending; null for none
		const std::string*              heading;  // over those that nobody carries
	};

	// --- Shared helpers (CaptureWindow.cpp) -----------------------------------

	PieceList                InventoryPieces(const State& a_state);
	std::vector<std::size_t> Ticked(const State& a_state, const PieceList& a_list);
	void                     DrawPieceRows(State& a_state, const PieceList& a_list);
	bool                     DrawPieceCount(const State& a_state, const PieceList& a_list, bool a_withWardrobeLink = false);
	void                     DrawPreviewToggle(State& a_state, const PieceList& a_list);
	std::string              Trimmed(const char* a_text);
	void                     DrawError(const std::string& a_message);
	void                     RequestCancel();
	bool                     DrawNameField(State& a_state, std::array<char, kNameCapacity>& a_name);
	void                     DrawGenderField(int& a_gender);
	bool                     BeginModal(const char* a_id);
	std::vector<std::string> TickedCategories(const State& a_state, const bool* a_ticked);
	const char*              GenderName(int a_gender);
	void                     SetGroupHeadings(State& a_state);

	void                                 QueueMenuSound(RE::FormID a_sound);
	void                                 GoToPage(State& a_state, Page a_page);
	std::vector<std::size_t>             TickedPieces(const State& a_state);
	const CaptureWindow::ExistingOutfit& OutfitAt(const State& a_state, int a_index);
	int                                  IndexOfOutfit(const State& a_state, std::uint32_t a_id);
	bool                                 HasPiece(const CaptureWindow::ExistingOutfit& a_outfit, std::size_t a_piece);
	void                                 QueuePreview(const State& a_state, std::vector<std::size_t> a_pieces);
	void                                 PreviewOutfitOrTicks(const State& a_state, int a_outfit);
	float                                FittedListHeight(float a_rowsHeight, float a_minHeight, float a_reserved = 0.0f);
	float                                ListHeight(std::size_t a_rows, float a_rowHeight, float a_reserved = 0.0f);
	void                                 PushListRowSpacing();
	bool                                 ReasonButton(const char* a_label, const char* a_reason, const char* a_tooltip = nullptr);
	void                                 BeginButtonRow(std::initializer_list<const char*> a_labels);
	void                                 EndButtonRow();
	void                                 ResetCellBaseline();
	bool                                 BeginColumns(const char* a_id);
	void                                 EndColumns();
	std::size_t                          GroupIndex(CaptureWindow::PieceGroup a_group);
	std::string                          JoinNames(const std::vector<std::string>& a_names);
	bool                                 DrawCategoryRow(const char* a_label, bool& a_ticked, bool a_showMark = true);
	bool                                 IsClass(const CaptureWindow::CategoryOption& a_option);
	void                                 ChooseOutfit(State& a_state, int a_index);
	void                                 DrawHelpTooltip(const std::string& a_text);
	std::size_t                          RowCount(const State& a_state);
	const char*                          RowName(const State& a_state, std::size_t a_row);
	bool                                 HoldsOutfit(const State& a_state, std::size_t a_row, std::uint32_t a_outfit);
	// Whether giving row `a_row` library outfit `a_outfit` (0 for one not saved yet) replaces
	// what it wears: random off, and another `chosen`, or the Default's pieces with none.
	bool        IsTaken(const State& a_state, std::size_t a_row, std::uint32_t a_outfit);
	std::string HeldOutfitName(const State& a_state, std::size_t a_row);
	std::string QuotedOutfitName(const State& a_state, std::uint32_t a_outfit);
	// The rows a form's "Situations for #{Name}" ticks (`a_wearsFor`, one per row of request.assigned)
	// change for library outfit `a_outfit`: those ticked whose worn outfit the save replaces
	// (IsTaken), which it gives the outfit only after Confirm; those the save gives it; those
	// that hold it and are unticked.
	std::vector<std::size_t> TakenRows(const State& a_state, const bool* a_wearsFor, std::uint32_t a_outfit);
	std::vector<std::size_t> AssignedRows(const State& a_state, const bool* a_wearsFor);
	std::vector<std::size_t> RemovedRows(const State& a_state, const bool* a_wearsFor, std::uint32_t a_outfit);
	// The lines a confirm popup shows for those rows: `Town: added`, `Town: removed`, and for
	// the taken ones `Town: "B" replaces "A"` (`a_name` is the outfit's name).
	void DrawRowChangeLines(const State& a_state, const bool* a_wearsFor, std::uint32_t a_outfit, const std::string& a_name);

	// Leaving a page: Exit, Escape or a tab. With unsaved edits (Manage Wardrobe, Manage
	// Situations) the page first shows the unsaved-changes alert (kLeavePopup); else it goes.
	void RequestLeave(State& a_state, PendingLeave a_leave);
	// Goes without asking: closes the window, or switches to the tab's page, which keeps every
	// page's ticks and edits; the preview shows Manage Wardrobe's selected outfit as edited
	// there, the Inventory page's ticks on the other tabs.
	void Leave(State& a_state, const PendingLeave& a_leave);

	enum class LeaveChoice
	{
		kNone,
		kSave,
		kDiscard,
		kBack
	};

	// The unsaved-changes alert: `a_text`, then Save Changes (greyed with `a_noSave`, which shows
	// on hover), Discard Changes and Back. Back and Escape forget the leave; the page acts on
	// the other two.
	LeaveChoice DrawLeavePopup(State& a_state, const std::string& a_text, const char* a_noSave);

	// --- Pages (OutfitPages.cpp) ----------------------------------------------

	void DrawLoadPage(State& a_state);
	void DrawSelectPage(State& a_state);
	void DrawManagePage(State& a_state);
	void DrawEditPage(State& a_state);

	// Manage Wardrobe's selected outfit differs from the library, or its rows from the actor's.
	bool HasManageEdits(const State& a_state);

	// Starts Manage Wardrobe' edits of outfit `a_index` (-1 for none) from the library,
	// without touching the preview.
	void FillManageForm(State& a_state, int a_index);

	struct BrowserControls
	{
		bool  escapeTaken = false;  // Escape cleared the search, so it is not also Back
		float height = 0.0f;        // pixels the controls took, which the outfit list's height cap leaves room for
	};

	// The outfit list's search (and Filter, while kShowFilter) at the top of its column.
	BrowserControls DrawBrowserControls(const State& a_state, OutfitBrowser::State& a_browser);

	// The outfit list under the browser (`a_reserved` is its height). Returns the index into
	// request.existingOutfits of the outfit clicked, or -1. `a_draggable`: Manage Situations'
	// list, whose rows have a grip and drag a copy of their outfit, and whose clicks select
	// nothing.
	int DrawOutfitList(State& a_state, OutfitBrowser::State& a_browser, int a_selected, float a_reserved, bool a_draggable = false);

	// One outfit of a list, `a_index` into request.existingOutfits, with its tags and marks;
	// `a_grip` draws the drag grip. Returns true when clicked.
	bool DrawOutfitRow(const State& a_state, const OutfitBrowser::State& a_browser, int a_index, float a_tagsWidth, bool a_selected, bool a_striped,
		bool a_grip = false);

	// The form fields Manage Wardrobe and the Save page share: a multi-choice dropdown over
	// `a_ticked` (one per request.categories, kClassGroup's used) under the Category label,
	// and one over the actor's rows (`a_ticked`, one per request.assigned: Default, then the
	// situations) under `a_label` with its "(?)", which returns true while that mark is
	// hovered, for the caller's help tooltip. Each frame names the ticked ones, or `None`
	// greyed.
	void DrawCategoryDropdown(const State& a_state, bool* a_ticked);
	bool DrawRowsDropdown(const State& a_state, const char* a_label, bool* a_ticked);

	// --- Manage Situations (SituationPage.cpp) --------------------------------

	void DrawSituationPage(State& a_state);

	// Starts Manage Situations' edits from the request: each situation's name, description
	// and row, and the Default's pieces. The selected situation and the folded headings follow
	// their situations from `a_before`, the request's situations the edits were made for;
	// none for a situation gone.
	void FillSituationForm(State& a_state, const std::vector<CaptureWindow::SituationInfo>& a_before);

	// A situation's name or description differs from situations.json, or a row from the actor's.
	bool HasSituationEdits(const State& a_state);

	// Drags the outfit row just laid out, carrying `a_payload`, with `a_label` beside the cursor.
	void OfferOutfitDrag(const OutfitPayload& a_payload, const char* a_label);
}
