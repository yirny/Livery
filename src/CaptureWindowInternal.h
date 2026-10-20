// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The capture window's constants, state and shared helpers, private to CaptureWindow.cpp
// (the window and its tab column, the Inventory and Save pages) and OutfitPages.cpp (the
// outfit browser and lists, the Load, Select, Manage Outfits and Edit pages). The helpers
// are defined in CaptureWindow.cpp, the pages in OutfitPages.cpp; each definition carries
// its comment.
//
// Contract: included after PCH.h. The functions that take a State run with State::mutex
// held: the draw functions in the capture menu's PostDisplay, as CaptureWindow.h describes.

#include "CaptureWindow.h"
#include "OutfitBrowser.h"

#include <imgui.h>

#include <array>
#include <cstddef>
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
	// Manage Outfits' popups. The two that discard share a title, told apart after "###".
	constexpr const char* kDiscardPopup = "Discard changes?###discardSelect";
	constexpr const char* kDiscardExitPopup = "Discard changes?###discardExit";
	constexpr const char* kUnsavedPopup = "Unsaved changes";
	constexpr const char* kManageConfirmPopup = "Save changes?";
	constexpr const char* kApplyChangesPopup = "Update and apply?";
	constexpr const char* kRemovePopup = "Remove outfit?";
	constexpr const char* kManageReplacePopup = "Replace outfit?";
	constexpr const char* kArrow = "\xE2\x86\x92";  // U+2192, UTF-8: the replace popups' `Town: "A" → "B"`
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

	// Sound descriptors in Skyrim.esm, whose load index is always 00.
	constexpr RE::FormID kOpenSound = 0x0003F2BD;    // UIMenuBladeOpenSD
	constexpr RE::FormID kOkSound = 0x0003C751;      // UIMenuOKSD
	constexpr RE::FormID kCancelSound = 0x0003C752;  // UIMenuCancelSD
	constexpr RE::FormID kFocusSound = 0x0003C759;   // UIMenuFocus

	enum class Status
	{
		kClosed,
		kEditing,
		kSaving  // Create, Confirm, Apply or Restore Original Outfit pressed; waiting for the handler to Close() or SetError()
	};

	enum class Page
	{
		kCapture,  // the Inventory page
		kSave,
		kLoad,
		kSelect,
		kManage,  // Manage Outfits
		kEdit     // Manage Outfits' piece editor
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
		std::unique_ptr<bool[]>         wearsFor;     // one per row of "wears it for" (request.assigned)
	};

	// Manage Outfits' edits of the selected outfit, kept while the window is open; selecting
	// another outfit starts them again from the library.
	struct ManageForm
	{
		int                             outfit = -1;  // index into request.existingOutfits; -1 for none selected
		std::array<char, kNameCapacity> name{};
		std::unique_ptr<bool[]>         classTicked;  // one per request.categories; only the Armor Classes' are used
		int                             gender = -1;  // -1 unisex, 0 male, 1 female
		std::vector<std::size_t>        pieces;       // its loaded pieces as edited: indices into request.pieces, ascending
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
		bool                     keepTaken = false;     // the replace popup's Keep: rows another outfit holds stay out of the save
		bool                     openConfirm = false;   // open the Update tab's confirm popup on the next frame
		std::string              wearsForLabel;         // "#{Name} wears it for"
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
		int                      loadedOutfit = -1;      // the last one loaded: index into request.existingOutfits, -1 for none
		OutfitBrowser::State     loadBrowser;            // search, filter and sort of the Load page's outfit list
		OutfitBrowser::State     selectBrowser;          // the Select page's
		OutfitBrowser::State     manageBrowser;          // Manage Outfits'
		bool                     showCategories = true;  // the outfit lists show each outfit's tags (Show Categories)
		ManageForm               manage;
		int                      discardFor = -1;            // the outfit to select once Discard changes is chosen: index, -1 for none
		bool                     openUnsaved = false;        // open the Unsaved changes popup on the next frame (Manage Outfits)
		std::string              manageSituationsLabel;      // "Situations for #{Name}"
		std::string              manageApplyLabel;           // "Apply to #{Name}"
		std::string              manageApplyTooltip;         // "Dress #{Name} in this outfit as their Default."
		std::size_t              replaceRow = 0;             // the situation row Manage Outfits' Replace popup is about: index into request.assigned
		bool                     openManageReplace = false;  // open that popup on the next frame, in the page's ID scope
		std::unique_ptr<bool[]>  editTicked;                 // the Edit page's ticks, one per request.pieces
		std::vector<std::size_t> editShown;                  // the edited pieces it opened with, so those nobody carries stay listed
		std::string              editTitle;                  // "Edit \"#{Outfit}\""
		std::string              editShownHeading;           // over those nobody carries: "From \"#{Outfit}\" (Not Carried)"
		int                      recentreFrames = 0;         // frames left to pin the window to its place
		std::optional<ImVec2>    placeCentre;                // where a pinned window's centre goes; none for the start place
		ImVec2                   windowPos{};                // of the window in the last frame drawn
		ImVec2                   windowSize{};
		bool                     userMoved = false;    // the user moved the window since Open, so turning Live Preview on keeps it there
		unsigned                 session = 0;          // counts Opens, so a Live Preview task queued before a close does nothing
		bool                     previewOn = false;    // Live Preview is on
		bool                     previewBusy = false;  // a Live Preview tick waits for its SKSE task
		bool                     frozeGame = false;    // the window froze the game (Live Preview off), so Close unfreezes it
		int                      viewDragButton = -1;  // ImGui mouse button held since a press over the game; -1 for none
		float                    viewDragX = 0.0f;     // mouse movement since the last frame while viewDragButton is held
		float                    viewDragY = 0.0f;
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
	bool                     DrawPieceCount(const State& a_state, const PieceList& a_list, bool a_withRestore = false);
	void                     DrawPreviewToggle(State& a_state, const PieceList& a_list);
	std::string              Trimmed(const char* a_text);
	void                     DrawError(const std::string& a_message);
	void                     RequestCancel();
	bool                     DrawNameField(State& a_state, std::array<char, kNameCapacity>& a_name);
	void                     DrawClassField(const State& a_state, bool* a_ticked);
	void                     DrawGenderField(int& a_gender);
	bool                     BeginModal(const char* a_id);
	std::vector<std::string> TickedCategories(const State& a_state, const bool* a_ticked);
	const char*              GenderName(int a_gender);
	void                     SetGroupHeadings(State& a_state);

	void                                 QueueMenuSound(RE::FormID a_sound);
	void                                 GoToPage(State& a_state, Page a_page);
	std::vector<std::size_t>             TickedPieces(const State& a_state);
	const CaptureWindow::ExistingOutfit& OutfitAt(const State& a_state, int a_index);
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
	std::string                          HeldOutfitName(const State& a_state, std::size_t a_row);

	// --- Pages (OutfitPages.cpp) ----------------------------------------------

	void DrawLoadPage(State& a_state);
	void DrawSelectPage(State& a_state);
	void DrawManagePage(State& a_state);
	void DrawEditPage(State& a_state);

	// The tab column's switches: to Manage Outfits from the Inventory page, and back, which
	// first shows the Unsaved changes notice while the selected outfit has unsaved edits.
	void EnterManagePage(State& a_state);
	void LeaveManagePage(State& a_state);

	// Starts Manage Outfits' edits of outfit `a_index` (-1 for none) from the library,
	// without touching the preview.
	void FillManageForm(State& a_state, int a_index);
}
