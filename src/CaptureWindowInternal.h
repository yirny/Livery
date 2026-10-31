// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The capture window's constants, state and shared helpers, private to CaptureWindow.cpp
// (the window and its tab column, the Dress and Save pages), OutfitPages.cpp (the
// outfit browser and lists, the Load, the Wardrobe page and Edit pages) and
// SituationPage.cpp (the Situations page). The helpers are defined in
// CaptureWindow.cpp, the pages in OutfitPages.cpp and SituationPage.cpp; each definition
// carries its comment.
//
// Contract: included after PCH.h. The functions that take a State run with State::mutex
// held: the draw functions in the capture menu's PostDisplay, as CaptureWindow.h describes.

#include "CaptureWindow.h"
#include "OutfitBrowser.h"
#include "sarrai/Theme.h"

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
	constexpr std::size_t kNameCapacity = 64;                // bytes, UTF-8, including the terminator
	constexpr float       kMaxListHeightRatio = 0.6f;        // of the screen height, for every scrolling list
	constexpr const char* kConfirmPopup = "Replace items?";  // IDs shared by OpenPopup and BeginPopupModal
	constexpr const char* kCreateApplyPopup = "Apply outfit?###createAndApply";
	constexpr const char* kApplyPopup = "Update the outfit?";
	// the Wardrobe page's popups.
	constexpr const char* kDiscardPopup = "Discard changes?###discardSelect";
	constexpr const char* kManageConfirmPopup = "Save changes?";
	constexpr const char* kManageApplyPopup = "Apply outfit?";
	constexpr const char* kRemovePopup = "Remove outfit?";
	constexpr const char* kNotCarriedPopup = "Items not carried";
	constexpr const char* kIconPopup = "Choose icon";
	constexpr const char* kRestorePopup = "Restore original outfit?";
	// The unsaved-changes alert of the Wardrobe page and the Situations page (DrawLeavePopup).
	constexpr const char* kLeavePopup = "Unsaved changes";
	// Reasons a button is greyed out, shown on hover (and in the empty name field).
	constexpr const char* kNeedName = "Enter a name.";
	constexpr const char* kNeedPiece = "Tick at least one item.";
	constexpr const char* kNeedOutfit = "Select an outfit.";
	constexpr const char* kAddFrom = "Add from";  // the words of the links under a piece list, before their icon (DrawCountLink)
	constexpr const char* kNoOutfits = "No outfits in the wardrobe.";
	constexpr const char* kNoLoadedPiece = "None of its items is loaded.";
	// The Chests page's and the chest editor's (docs/chest-ui.md).
	constexpr const char* kChestCreatePopup = "New chest";
	constexpr const char* kChestRemovePopup = "Remove chest?";
	constexpr const char* kNeedChest = "Select a chest.";
	constexpr const char* kNoChests = "No chests yet.";
	constexpr const char* kNoChanges = "No changes to save.";
	constexpr const char* kWornAlready = "Already worn as is.";
	// A piece list row of an item the player would not get (Request::carriedOnly): its opacity
	// and its hover text. Shared by the piece lists of CaptureWindow.cpp and OutfitPages.cpp.
	constexpr float       kNotCarriedAlpha = 0.5f;
	constexpr const char* kNotCarriedHint = "You don't carry this item. Dressing from inventory only leaves it off.";
	// Frames the window's place is pinned after Open or a page change: an
	// auto-resized window fits new content a frame late, and PlaceWindow sees
	// the fitted height a frame after that.
	constexpr int kRecentreFrames = 3;
	// One heading per CaptureWindow::PieceGroup.
	constexpr std::size_t kPieceGroupCount = static_cast<std::size_t>(CaptureWindow::PieceGroup::kNotCarried) + 1;
	using GroupHeadings = std::array<std::string, kPieceGroupCount>;

	// The outfit browser's Filter dropdown, hidden for now; its code stays, as it is likely
	// to return (docs/outfit-management-ui.md §1).
	constexpr bool kShowFilter = false;

	// The piece list's columns, in their order (State::sortColumn).
	constexpr int                        kNameColumn = 0;
	constexpr int                        kTypeColumn = 1;
	constexpr int                        kSlotColumn = 2;
	constexpr std::array<const char*, 3> kColumnLabels{ "Name", "Type", "Slot" };

	// What an outfit row of the Situations page carries while it is dragged (OfferOutfitDrag).
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
		kSaving  // Create, Confirm, Apply or Restore pressed; waiting for the handler to Close(), SetOutfits() or SetError()
	};

	enum class Page
	{
		kCapture,  // the Dress page
		kSave,
		kLoad,
		kManage,      // the Wardrobe page
		kEdit,        // the Wardrobe page's piece editor
		kSituations,  // the Situations page
		kChests,      // the Chests page (docs/chest-ui.md)
		kChestEdit,   // the chest editor, reached from the Chests page
		kAppearance   // the window's skin and menu sounds (Settings)
	};

	// Where the Load page's Load goes, and what its list offers.
	enum class LoadTarget
	{
		kInventory,  // the Dress page: an outfit's items replace the ticks, a chest's are listed
		kEdit,       // the Edit page: an outfit's items join the ticks, a chest's are listed
		kChest       // the chest editor: the items join the chest's
	};

	enum class LoadSource
	{
		kOutfits,
		kChests
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

	// the Wardrobe page's edits of the selected outfit, kept while the window is open; selecting
	// another outfit starts them again from the library.
	struct ManageForm
	{
		int                             outfit = -1;  // index into request.existingOutfits; -1 for none selected
		OutfitIcons::Icon               icon;
		std::array<char, kNameCapacity> name{};
		std::vector<std::size_t>        pieces;  // its loaded pieces as edited: indices into request.pieces, ascending
		// The armor (FormIDs, ascending) of `pieces` unticked in its piece list, which Apply has
		// the outfit leave off on the row it gives it; never every one of `pieces`. Not an
		// unsaved edit: selecting an outfit, or SetOutfits, starts it from the actor's rows.
		std::vector<std::uint32_t> excluded;
	};

	// The Choose icon popup (the Wardrobe page's and the Save page's Create tab's) while it is open.
	struct IconPicker
	{
		OutfitIcons::Icon            pick;        // the choice, from the edited icon as it opens; OK hands it back
		std::optional<std::uint32_t> hoverColor;  // the colour the mouse was over last frame, which every icon shows
	};

	// One situation of the Situations page as edited, in request.situations' order.
	struct SituationEntry
	{
		std::array<char, kNameCapacity> name{};
		std::string                     nameBefore;      // its name as its field's edit began, put back for an empty or taken one
		OutfitRows::Row                 row;             // the actor's row for it
		bool                            folded = false;  // its heading in the Situation List is folded
	};

	// the Situations page's edits, kept while the window is open; a change a handler made
	// (SetOutfits) and Discard Changes start them again from the request.
	struct SituationForm
	{
		std::vector<SituationEntry> entries;
		bool                        defaultPieces = false;     // the Default keeps its pieces not in the wardrobe
		int                         selected = -1;             // index into entries; -1 for none
		bool                        details = false;           // the right pane shows the selected one (Show Outfit List not pressed since)
		std::uint32_t               viewedOutfit = 0;          // the library outfit whose details the right pane shows, clicked in either pane; 0 for none
		int                         viewedRow = -1;            // the entry under whose heading it was clicked, whose row its piece list's ticks edit; -1 for the outfit list's (read only)
		bool                        scrollToSelected = false;  // the Situation List scrolls to the selected heading on its next draw
		int                         nameEditing = -1;          // the entry whose name field is active: index into entries; -1 for none
		ImGuiID                     nameField = 0;             // that field's ImGui ID
		int                         dragHover = -1;            // the folded heading a drag hovers: index into entries; -1 for none
		double                      dragHoverSince = 0.0;      // since when, ImGui::GetTime() seconds
		float                       outfitPaneHeight = 0.0f;   // the right pane's height with the outfit list, last drawn; the Situation List keeps it
	};

	// The chest editor's edits of the chest it opened on (docs/chest-ui.md), kept until the
	// page is left; Update saves them, and a change the handler made (SetOutfits) starts them
	// again from the chest.
	struct ChestForm
	{
		int                             chest = -1;  // index into request.chests; -1 for none
		std::array<char, kNameCapacity> name{};
		std::vector<std::size_t>        pieces;     // its items as edited: indices into request.pieces, ascending
		std::vector<std::size_t>        takenOut;   // items taken out that nobody carries, listed left under their own heading until the page is left; ascending
		std::array<char, kNameCapacity> newName{};  // the New chest popup's field
	};

	// An outfit or a chest loaded on the Dress page, found again by id at SetOutfits, so the
	// list follows a rename, an edit or a removal made with the window open.
	struct ShownSource
	{
		bool                     chest = false;  // `id` is a chest's; else a library outfit's
		std::uint32_t            id = 0;
		std::string              name;
		std::vector<std::size_t> pieces;        // what it brings to the list: indices into request.pieces, ascending
		bool                     whole = true;  // all its pieces; false once a later Load left only its ticked ones
	};

	// Where leaving a page with unsaved edits goes once they are saved or dropped: the
	// window's close (Exit, Escape), or a tab's page; the Wardrobe page with `manageOutfit`, the
	// library outfit to select there (the Situations page's Edit; 0 keeps its selection).
	struct PendingLeave
	{
		bool          exit = false;
		Page          page = Page::kCapture;
		std::uint32_t manageOutfit = 0;
	};

	struct State
	{
		std::mutex               mutex;
		Status                   status = Status::kClosed;
		CaptureWindow::Request   request;
		std::unique_ptr<bool[]>  pieceTicked;        // one per request.pieces; bool[] because Theme::TickRow needs bool*
		bool                     updateTab = false;  // the Save page shows the Update tab
		int                      chosenOutfit = -1;  // the Update tab's outfit: index into request.existingOutfits, -1 for none
		std::string              situationsLabel;    // "SITUATIONS (#{Name})", over the Situations page's outfit details
		std::string              error;
		bool                     focusNameField = false;
		std::vector<InputRecord> pending;
		bool                     resetImGui = false;  // set by Open, handled on the next frame
		Page                     page = Page::kCapture;
		std::string              applyTooltip;       // "Dress #{Name} in the ticked items."
		GroupHeadings            headings;           // over each group's pieces (CaptureWindow::PieceGroup)
		std::vector<std::size_t> pieceOrder;         // indices into request.pieces as the list shows them (SortPieces)
		int                      sortColumn = 0;     // the piece list's sorted column: Name, Type or Slot
		bool                     showSlots = false;  // the piece list has the Slot column (Settings::ShowSlotColumn at Open)
		bool                     sortDescending = false;
		int                      previewedOutfit = -1;                 // on the Load page: index into request.existingOutfits, -1 for none
		int                      previewedChest = -1;                  // on the Load page with LoadSource::kChests: index into request.chests, -1 for none
		LoadTarget               loadTarget = LoadTarget::kInventory;  // the page the Load page was opened from, which its Load returns to
		LoadSource               loadSource = LoadSource::kOutfits;
		int                      loadedOutfit = -1;  // the last one loaded: index into request.existingOutfits, -1 for none
		// What the Dress page has loaded, in load order: an outfit's Load replaces them, a
		// chest's joins those with a ticked piece. `inventoryShown` is every piece they bring,
		// ascending, and the heading over those nobody carries names them (SetInventoryShown).
		std::vector<ShownSource> inventorySources;
		std::vector<std::size_t> inventoryShown;
		OutfitBrowser::State     loadBrowser;    // search, filter and sort of the Load page's outfit list
		OutfitBrowser::State     updateBrowser;  // the Save page's Update tab's
		OutfitBrowser::State     manageBrowser;  // the Wardrobe page's
		ManageForm               manage;
		ChestForm                chest;
		IconPicker               iconPicker;
		int                      discardFor = -1;             // the outfit to select once Discard changes is chosen: index, -1 for none
		std::string              manageApplyLabel;            // "Apply to #{Name}"
		std::string              manageApplyTip;              // its tooltip, what it dresses them for (FillManageForm); "" with no outfit selected
		std::unique_ptr<bool[]>  editTicked;                  // the Edit page's ticks, one per request.pieces
		std::vector<std::size_t> editShown;                   // the edited pieces it opened with and those loaded since, so those nobody carries stay listed
		std::string              editTitle;                   // "Edit \"#{Outfit}\""
		std::vector<std::string> editOutfitNames;             // the outfits those come from: the edited one, then those loaded that brought one
		bool                     editShownFromChest = false;  // a chest brought one last, so that heading has the chest icon
		std::string              editShownHeading;            // over those nobody carries: "\"#{Outfit}\", \"#{Loaded}\""
		OutfitBrowser::State     situationBrowser;            // the Situations page's outfit list
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
		// The Save page's tabs' names, each kept while the window is open.
		std::array<char, kNameCapacity> createName{};
		std::array<char, kNameCapacity> updateName{};
		// The Create tab's icon as chosen in Choose icon; nullopt follows the ticks (CreateIcon).
		std::optional<OutfitIcons::Icon> createIcon;
	};

	// The piece list a page shows (DrawPieceRows): the Dress page's ticks with the
	// pieces of the outfit last loaded, or the Edit page's own ticks with the edited pieces
	// it opened with. Pieces someone carries are always listed.
	struct PieceList
	{
		bool*                           ticked;                                     // one per request.pieces (bool* for Theme::TickRow)
		const std::vector<std::size_t>* shown;                                      // the other pieces listed: indices into request.pieces, ascending; null for none
		const std::string*              heading;                                    // over those that nobody carries
		Theme::HeadingIcon              shownIcon = Theme::HeadingIcon::kBareForm;  // that heading's: the chest's when a chest brought them last
	};

	// --- Shared helpers (CaptureWindow.cpp) -----------------------------------

	// The links under a piece list, which open the Load page: Add from Wardrobe...
	// (Request::offersWardrobe) and Add from Chest... (with Request::offersChests too). Each is
	// true the frame it is clicked.
	struct CountLinks
	{
		bool wardrobe = false;
		bool chest = false;
	};

	PieceList                InventoryPieces(const State& a_state);
	void                     SetInventoryShown(State& a_state);  // State::inventoryShown and the heading over it, from State::inventorySources
	std::vector<std::size_t> Ticked(const State& a_state, const PieceList& a_list);
	void                     DrawPieceRows(State& a_state, const PieceList& a_list);
	CountLinks               DrawPieceCount(const State& a_state, const PieceList& a_list, bool a_withLinks = false);
	// A link under a list (Theme::LabelLink), greyed with `a_reason` (its hover text):
	// "[+] Add from <a_icon> <a_name>", with the Wardrobe tab's wardrobe or the Chests tab's chest.
	bool        DrawCountLink(const char* a_name, const char* a_reason, Theme::HeadingIcon a_icon, Theme::LinkLine a_line = Theme::LinkLine::kSame,
		float a_reserveRight = 0.0f);
	void        DrawPreviewToggle(State& a_state, const PieceList& a_list);
	std::string Trimmed(const char* a_text);
	void        DrawError(const std::string& a_message);
	void        RequestCancel();
	bool        DrawNameField(State& a_state, std::array<char, kNameCapacity>& a_name, const OutfitIcons::Icon* a_icon = nullptr,
		bool* a_iconClicked = nullptr, const char* a_hint = "Outfit Name");
	bool        BeginModal(const char* a_id);
	void        SetGroupHeadings(State& a_state);

	// Choose icon (OutfitPages.cpp), opened as kIconPopup with State::iconPicker set: returns
	// the choice on OK, else nullopt. `a_keepOpen` false closes it (its outfit is gone).
	std::optional<OutfitIcons::Icon> DrawIconPopup(State& a_state, bool a_keepOpen);

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
	void                                 CentreButtonRow(std::initializer_list<const char*> a_labels);
	void                                 BeginButtonRow(std::initializer_list<const char*> a_labels);
	void                                 EndButtonRow();
	void                                 ResetCellBaseline();
	bool                                 BeginColumns(const char* a_id, int a_count = 2);
	void                                 EndColumns(int a_count = 2);
	std::size_t                          GroupIndex(CaptureWindow::PieceGroup a_group);
	Theme::HeadingIcon                   GroupIcon(CaptureWindow::PieceGroup a_group);  // the icon of a group's heading
	bool                                 FadesNotCarried(const State& a_state, std::size_t a_piece);
	bool                                 DrawCategoryRow(const char* a_label, bool& a_ticked, bool a_showMark = true);
	void                                 DrawHelpTooltip(const std::string& a_text);
	std::size_t                          RowCount(const State& a_state);
	const char*                          RowName(const State& a_state, std::size_t a_row);
	bool                                 HoldsOutfit(const State& a_state, std::size_t a_row, std::uint32_t a_outfit);
	// Whether giving row `a_row` library outfit `a_outfit` (0 for one not saved yet) replaces
	// what it wears: random off, and another `chosen`, or the Default's pieces with none.
	bool        IsTaken(const State& a_state, std::size_t a_row, std::uint32_t a_outfit);
	std::string HeldOutfitName(const State& a_state, std::size_t a_row);
	std::string QuotedOutfitName(const State& a_state, std::uint32_t a_outfit);

	// Leaving a page: Exit, Escape or a tab. With unsaved edits (the Wardrobe page, Manage
	// Situations) the page first shows the unsaved-changes alert (kLeavePopup); else it goes.
	void RequestLeave(State& a_state, PendingLeave a_leave);
	// Goes without asking: closes the window, or switches to the tab's page, which keeps every
	// page's ticks and edits but the Situations page's outfit details, which close; with
	// `manageOutfit`, the Wardrobe page first selects it. The preview shows the Wardrobe page's
	// selected outfit as edited there, the Dress page's ticks on the other tabs.
	void Leave(State& a_state, const PendingLeave& a_leave);

	enum class LeaveChoice
	{
		kNone,
		kSave,
		kDiscard,
		kBack
	};

	// The unsaved-changes alert: `a_text`, the lines `a_drawDetails` draws (what is unsaved; none
	// with nullptr), then Save Changes (greyed with `a_noSave`, which shows on hover), Discard
	// Changes and Back. Back and Escape forget the leave; the page acts on the other two.
	LeaveChoice DrawLeavePopup(State& a_state, const std::string& a_text, const char* a_noSave, void (*a_drawDetails)(const State&) = nullptr);

	// --- Pages (OutfitPages.cpp) ----------------------------------------------

	void DrawLoadPage(State& a_state);
	void DrawManagePage(State& a_state);
	void DrawEditPage(State& a_state);

	// "\"A\", \"B\"": the outfits and chests the pieces nobody carries come from, for the heading
	// over them.
	std::string EditShownHeading(const std::vector<std::string>& a_names);

	// --- Chest pages (ChestPages.cpp, docs/chest-ui.md) -----------------------

	void DrawChestsPage(State& a_state);
	void DrawChestEditPage(State& a_state);

	const CaptureWindow::ChestInfo& ChestAt(const State& a_state, int a_index);
	int                             IndexOfChest(const State& a_state, std::uint32_t a_id);  // -1 for none or 0

	// The chest editor's name or items differ from the chest.
	bool HasChestEdits(const State& a_state);

	// Starts the editor's edits of chest `a_index` (-1 for none) from the chest; the items
	// taken out are forgotten.
	void FillChestForm(State& a_state, int a_index);

	// The Load page's Add for the chest editor: `a_pieces` (indices into request.pieces,
	// ascending) join the chest's items.
	void AddToChest(State& a_state, const std::vector<std::size_t>& a_pieces);

	// The chest list (the Chests page's, the Load page's with LoadSource::kChests): every chest
	// but `a_except` (an index into request.chests; -1 for none), `a_selected` highlighted, over
	// its count line. Returns the index into request.chests of the one clicked, or -1.
	int DrawChestList(const State& a_state, int a_selected, int a_except, float a_reserved);

	// The chest's name and its items, read only, as the Load page's right column; `a_hint` as a
	// field label while `a_index` is -1.
	void DrawChestDetails(const State& a_state, int a_index, const char* a_hint);

	// the Wardrobe page's selected outfit differs from the library, or its rows from the actor's.
	bool HasManageEdits(const State& a_state);

	// Starts the Wardrobe page's edits of outfit `a_index` (-1 for none) from the library,
	// without touching the preview, and words Apply's tooltip for it.
	// Contract: every change of the selection, or of what the actor wears (SetOutfits), comes
	// through here, so State::manageApplyTip stays current.
	void FillManageForm(State& a_state, int a_index);

	struct BrowserControls
	{
		bool  escapeTaken = false;  // Escape cleared the search, so it is not also Back
		float height = 0.0f;        // pixels the controls took, which the outfit list's height cap leaves room for
	};

	// The outfit list's search (and Filter, while kShowFilter) at the top of its column.
	BrowserControls DrawBrowserControls(const State& a_state, OutfitBrowser::State& a_browser);

	// The outfit list under the browser (`a_reserved` is its height). Returns the index into
	// request.existingOutfits of the outfit clicked, or -1. `a_draggable`: the Situations page's
	// list, whose rows have a grip and drag a copy of their outfit.
	int DrawOutfitList(State& a_state, OutfitBrowser::State& a_browser, int a_selected, float a_reserved, bool a_draggable = false);

	// One outfit of a list, `a_index` into request.existingOutfits, with its marks (the star of
	// one chosen for a situation only with `a_situationMark`); `a_grip` places the drag grip.
	// Returns true when clicked.
	bool DrawOutfitRow(const State& a_state, const OutfitBrowser::State& a_browser, int a_index, bool a_selected, bool a_striped,
		Theme::Grip a_grip = Theme::Grip::kNone, bool a_situationMark = true);

	// Outfit `a_index` (into request.existingOutfits) in the Wardrobe page's order: its name,
	// `a_situations` under the "SITUATIONS (#{Name})" label, then its pieces under their count
	// line, their list ending by screen Y `a_bottom` and scrolling when they need more (one row
	// shows at least). Read only with no `a_excluded`; else the pieces tick (DrawPieceChoices),
	// and it returns true when `a_excluded` changed.
	bool DrawOutfitSummary(const State& a_state, int a_index, const std::string& a_situations, float a_bottom,
		std::vector<std::uint32_t>* a_excluded = nullptr);

	// An outfit's piece count line, the pieces whose plugin is not loaded included.
	std::string PieceCountText(std::size_t a_loaded, std::size_t a_unloaded);

	// Pieces' names (indices into request.pieces) in read-only rows laid out as the outfit
	// list's, ending by screen Y `a_bottom` and scrolling when they need more; one row shows at
	// least.
	void DrawPieceNames(const State& a_state, const std::vector<std::size_t>& a_pieces, float a_bottom = FLT_MAX);

	// An outfit's pieces (indices into request.pieces) as the Dress page's tick rows, with
	// their icons: a ticked one is worn with the outfit, an unticked one left off, its armor in
	// `a_excluded` (FormIDs, ascending). A click ticks or unticks one, but the last ticked one
	// stays ticked. The list ends by screen Y `a_bottom` and scrolls when it needs more; one row
	// shows at least. Returns true when `a_excluded` changed.
	bool DrawPieceChoices(const State& a_state, const std::vector<std::size_t>& a_pieces, std::vector<std::uint32_t>& a_excluded, float a_bottom = FLT_MAX);

	// `a_pieces` less those whose armor `a_excluded` names, or all of them when that leaves
	// none, as dressing wears them (OutfitRows::WornPieces).
	std::vector<std::size_t> WornOf(const State& a_state, const std::vector<std::size_t>& a_pieces, const std::vector<std::uint32_t>& a_excluded);

	// Those of `a_excluded` that name one of `a_pieces`; none when they name every one, as
	// dressing then wears them all.
	std::vector<std::uint32_t> ExcludedAmong(const State& a_state, const std::vector<std::size_t>& a_pieces, std::vector<std::uint32_t> a_excluded);

	// What an Apply popup dresses the actor in (docs/outfit-management-ui.md §4): Manage
	// Wardrobe's selected outfit, or the outfit the Save page's Create and Apply creates.
	struct ApplySubject
	{
		std::string   name;        // as the library has it, or as it is created
		std::uint32_t id = 0;      // its library id; 0 for one not created yet
		std::string   applyLabel;  // the button of the popup that offers no situation: "Apply", "Update and Apply", "Create and Apply"
		// What is saved first, which the question starts with (`Save your changes to "A"`,
		// `Create "A"`), and its sentence in Apply as Default's help; "" for nothing.
		std::string saveFirst;
		std::string saveFirstHelp;
		void (*drawSaveLines)(const State&) = nullptr;  // the lines under the question saying what is saved; nullptr for none
	};

	// The situation row Apply offers to give library outfit `a_outfit` (0 for one not created
	// yet) beside the Default: the one the actor wears another outfit for now. nullopt when
	// they wear the Default, nothing of Livery's or that outfit itself.
	std::optional<std::size_t> OfferedRow(const State& a_state, std::uint32_t a_outfit);

	// The row Apply gives the outfit when no row is offered (OfferedRow): the situation the
	// actor wears it for now, which it is dressed again for, else the Default.
	std::size_t DirectApplyRow(const State& a_state);

	// Whether Apply of library outfit `a_outfit` (0 for one not created yet) asks first,
	// library edits aside: a situation row is offered, or the Default's worn outfit would be
	// replaced.
	bool ApplyAsksFirst(const State& a_state, std::uint32_t a_outfit);

	// The body of an Apply popup, inside BeginModal: with a row offered (OfferedRow) Yes, Apply
	// as Default (?) and Cancel, else `a_subject.applyLabel` and Cancel. Returns the row the
	// user applies the outfit to, or nullopt.
	std::optional<std::size_t> DrawApplyPopupBody(const State& a_state, const ApplySubject& a_subject);

	// --- the Situations page (SituationPage.cpp) --------------------------------

	void DrawSituationPage(State& a_state);

	// Starts the Situations page's edits from the request: each situation's name and row, and
	// the Default's pieces. The selected situation and the folded headings follow
	// their situations from `a_before`, the request's situations the edits were made for;
	// none for a situation gone.
	void FillSituationForm(State& a_state, const std::vector<CaptureWindow::SituationInfo>& a_before);

	// A situation's name differs from situations.json, or a row from the actor's.
	bool HasSituationEdits(const State& a_state);

	// The star's tooltip from the page's edits, as OutfitBrowser::State::situations from the
	// actor's rows: "For Town, Cold", the situations (never Default) whose edited row holds
	// `a_outfit` by their edited names; "" for none.
	std::string EditedSituationsText(const State& a_state, std::uint32_t a_outfit);

	// The pieces of the outfit whose details show (SituationForm::viewedOutfit), less those its
	// row leaves off when they were opened from a row that holds it; empty with none showing.
	std::vector<std::size_t> ViewedOutfitPieces(const State& a_state);

	// Drags the outfit row just laid out, carrying `a_payload`, shown as a see-through copy of
	// the row (Theme::OutfitDragGhost) named `a_label`, with its `a_grip` and `a_icon` (nullptr
	// for none).
	void OfferOutfitDrag(const OutfitPayload& a_payload, const char* a_label, Theme::Grip a_grip, const OutfitIcons::Icon* a_icon);

	// An outfit's icon as the skins draw it. Contract: the key is a view into `a_icon`, so the
	// result lives no longer than it.
	inline Theme::RowIcon RowIconOf(const OutfitIcons::Icon& a_icon)
	{
		return { a_icon.key, a_icon.rgb };
	}
}
