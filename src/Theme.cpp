// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants  - font files per skin, Tailor's and Nordic's palettes
//   Fonts      - default font, each skin's body/heading fonts, Windows fallback fonts
//   Styles     - one ImGuiStyle per skin and its resolution scale
//   Ornaments  - Nordic's corner knots, heading rule and pointed button frame
//   Theme      - LoadFonts / Apply, then the skinned widgets
//
// The Tailor skin copies Tailor 3.0 (GPL-3.0), src/ui/imgui/Theme.cpp and
// TailorScreen.cpp: palette, radii, spacing, font faces, Windows font fallback
// and the resolution curve. Tailor ships its fonts; this plugin reads them from
// Tailor's folder. The Vanilla and Nordic skins approximate the game's menus and
// NORDIC UI with fonts this plugin ships (OFL): Barlow Condensed and Sovngarde.
// A skin whose font files are missing draws with ImGui's default font.
#include "PCH.h"

#include "Theme.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>

namespace
{
	constexpr auto kTailorFontDir = L"Data/SKSE/Plugins/Tailor/fonts";
	constexpr auto kOwnFontDir = L"Data/SKSE/Plugins/TailorOutfitCapture/fonts";

	// Tailor's measured size adjustment for Poppins and Montserrat (its FontMetricScale).
	constexpr float kFontMetricScale = 4.0f / 3.0f;

	struct FontFile
	{
		const wchar_t* name;
		float          size;   // pixels at 1080p, before scaling
		const wchar_t* segoe;  // Segoe UI file of the closest weight, for letters the font lacks
	};

	struct SkinFonts
	{
		const char*    skin;  // for the log
		const wchar_t* folder;
		FontFile       body;
		FontFile       heading;
	};

	constexpr SkinFonts kTailorFonts{ "Tailor", kTailorFontDir,
		{ L"Poppins-Regular.ttf", 14.0f * kFontMetricScale, L"segoeui.ttf" },
		{ L"Montserrat-Black.ttf", 18.0f * kFontMetricScale, L"seguibl.ttf" } };
	constexpr SkinFonts kVanillaFonts{ "Vanilla", kOwnFontDir,
		{ L"BarlowCondensed-Regular.ttf", 20.0f, L"segoeui.ttf" },
		{ L"BarlowCondensed-SemiBold.ttf", 26.0f, L"seguisb.ttf" } };
	constexpr SkinFonts kNordicFonts{ "Nordic", kOwnFontDir,
		{ L"Sovngarde-Light.ttf", 20.0f, L"segoeuil.ttf" },
		{ L"Sovngarde-Bold.ttf", 26.0f, L"segoeuib.ttf" } };

	// Tailor's Forge palette (only the tokens this window uses).
	constexpr ImU32 kFloor = IM_COL32(26, 19, 12, 255);
	constexpr ImU32 kSurface = IM_COL32(28, 24, 19, 255);
	constexpr ImU32 kRaised = IM_COL32(42, 33, 24, 255);
	constexpr ImU32 kAmber = IM_COL32(245, 158, 11, 255);
	constexpr ImU32 kAmberLight = IM_COL32(251, 191, 36, 255);
	constexpr ImU32 kCopper = IM_COL32(184, 115, 51, 255);
	constexpr ImU32 kText = IM_COL32(244, 235, 216, 255);
	constexpr ImU32 kGhost = IM_COL32(244, 235, 216, 102);
	constexpr ImU32 kBorder = IM_COL32(244, 235, 216, 20);
	constexpr ImU32 kFill1 = IM_COL32(244, 235, 216, 8);
	constexpr ImU32 kFill2 = IM_COL32(244, 235, 216, 15);
	constexpr ImU32 kFill3 = IM_COL32(244, 235, 216, 26);

	// NORDIC UI's palette: white on smoked black, grey labels, rules and highlights.
	constexpr ImU32 kNordicText = IM_COL32(240, 240, 240, 255);
	constexpr ImU32 kNordicLabel = IM_COL32(165, 165, 165, 255);
	constexpr ImU32 kNordicRule = IM_COL32(200, 200, 200, 90);
	constexpr ImU32 kNordicOrnament = IM_COL32(150, 150, 150, 220);

	struct LoadedFonts
	{
		ImFont* body = nullptr;     // nullptr when the file is missing
		ImFont* heading = nullptr;  // nullptr when the file is missing
	};

	// Render thread only.
	ImFont*     g_defaultFont = nullptr;
	LoadedFonts g_tailorFonts;
	LoadedFonts g_vanillaFonts;
	LoadedFonts g_nordicFonts;
	Theme::Skin g_skin = Theme::Skin::kClassic;  // skin of the frame being built
	float       g_scale = 1.0f;                  // its resolution scale, for hand-drawn ornaments

	// nullptr for Classic, which draws with ImGui's default font.
	const SkinFonts* FontFilesOf(Theme::Skin a_skin)
	{
		switch (a_skin) {
		case Theme::Skin::kTailor:
			return &kTailorFonts;
		case Theme::Skin::kVanilla:
			return &kVanillaFonts;
		case Theme::Skin::kNordic:
			return &kNordicFonts;
		default:
			return nullptr;
		}
	}

	// nullptr for Classic.
	LoadedFonts* LoadedFontsOf(Theme::Skin a_skin)
	{
		switch (a_skin) {
		case Theme::Skin::kTailor:
			return &g_tailorFonts;
		case Theme::Skin::kVanilla:
			return &g_vanillaFonts;
		case Theme::Skin::kNordic:
			return &g_nordicFonts;
		default:
			return nullptr;
		}
	}

	// --- Fonts ----------------------------------------------------------------

	// Contract: the atlas reads glyphs from this data as they are first drawn,
	// so it is read once and kept for the whole process. Empty when missing.
	std::vector<unsigned char>& ReadFont(const std::filesystem::path& a_path)
	{
		static std::map<std::filesystem::path, std::vector<unsigned char>> cache;
		if (const auto it = cache.find(a_path); it != cache.end()) {
			return it->second;
		}
		std::vector<unsigned char> data;
		if (std::ifstream in{ a_path, std::ios::binary }) {
			data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}
		return cache.emplace(a_path, std::move(data)).first->second;
	}

	std::vector<unsigned char>& FirstFont(const std::filesystem::path& a_folder, std::initializer_list<const wchar_t*> a_files)
	{
		static std::vector<unsigned char> none;
		for (const auto* file : a_files) {
			if (auto& data = ReadFont(a_folder / file); !data.empty()) {
				return data;
			}
		}
		return none;
	}

	ImFont* AddFont(std::vector<unsigned char>& a_data, float a_size, bool a_merge)
	{
		if (a_data.empty()) {
			return nullptr;
		}
		ImFontConfig config;
		config.MergeMode = a_merge;
		config.FontDataOwnedByAtlas = false;  // ReadFont's cache owns it
		return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(a_data.data(), static_cast<int>(a_data.size()), a_size, &config);
	}

	// Letters the last added font lacks come from Windows: Segoe UI at a close
	// weight (Cyrillic, Greek), then Chinese, Japanese and Korean with Windows'
	// display language first, since they draw shared characters differently.
	void MergeWindowsFonts(const wchar_t* a_segoe)
	{
		wchar_t    windows[MAX_PATH]{};
		const auto length = ::GetWindowsDirectoryW(windows, MAX_PATH);
		if (length == 0 || length >= MAX_PATH) {
			return;
		}
		const auto folder = std::filesystem::path(windows) / L"Fonts";
		auto       merge = [&](std::initializer_list<const wchar_t*> a_files) { AddFont(FirstFont(folder, a_files), 0.0f, true); };

		merge({ a_segoe, L"segoeui.ttf" });
		using Files = std::initializer_list<const wchar_t*>;
		const Files          chinese{ L"msyh.ttc", L"simsun.ttc" };
		const Files          japanese{ L"YuGothR.ttc", L"meiryo.ttc", L"msgothic.ttc" };
		const Files          korean{ L"malgun.ttf" };
		std::array<Files, 3> order{ chinese, japanese, korean };
		switch (PRIMARYLANGID(::GetUserDefaultUILanguage())) {
		case LANG_JAPANESE:
			order = { japanese, chinese, korean };
			break;
		case LANG_KOREAN:
			order = { korean, chinese, japanese };
			break;
		default:
			break;
		}
		for (const auto& files : order) {
			merge(files);
		}
	}

	ImFont* LoadFont(const std::filesystem::path& a_folder, const FontFile& a_file)
	{
		auto* font = AddFont(ReadFont(a_folder / a_file.name), a_file.size, false);
		if (font) {
			MergeWindowsFonts(a_file.segoe);
		}
		return font;
	}

	void LoadSkinFonts(Theme::Skin a_skin)
	{
		const auto& files = *FontFilesOf(a_skin);
		auto&       loaded = *LoadedFontsOf(a_skin);
		const auto  folder = std::filesystem::current_path() / files.folder;
		loaded.body = LoadFont(folder, files.body);
		loaded.heading = LoadFont(folder, files.heading);
		if (loaded.body && loaded.heading) {
			logger::info("{} fonts loaded", files.skin);
		} else {
			logger::warn("{} fonts missing in {}; the skin uses the default font", files.skin, folder.string());
		}
	}

	// --- Styles ---------------------------------------------------------------

	// Readable at 1440p/4K with ImGui's default font.
	float ClassicScale(float a_height)
	{
		return (std::max)(1.0f, a_height / 1080.0f * 1.5f);
	}

	// Tailor's own curve, shared by every skin with its own fonts: 1 up to
	// 1080p, growing to 1.75 at 2160p.
	float ResolutionScale(float a_height)
	{
		if (a_height <= 1080.0f) {
			return 1.0f;
		}
		if (a_height <= 1440.0f) {
			return 1.0f + (a_height - 1080.0f) / 1080.0f;
		}
		return (std::min)(1.75f, 4.0f / 3.0f + (a_height - 1440.0f) * (1.75f - 4.0f / 3.0f) / 720.0f);
	}

	// Tailor's ApplyTheme values. Tailor draws no title bar, so its colours
	// here are the nearest surfaces of the same palette.
	void SetTailorStyle(ImGuiStyle& a_style)
	{
		a_style.WindowRounding = 0.0f;
		a_style.ChildRounding = 10.0f;
		a_style.FrameRounding = 8.0f;
		a_style.PopupRounding = 10.0f;
		a_style.GrabRounding = 6.0f;
		a_style.ScrollbarRounding = 8.0f;
		a_style.WindowBorderSize = 0.0f;
		a_style.ChildBorderSize = 1.0f;
		a_style.FrameBorderSize = 1.0f;
		a_style.PopupBorderSize = 1.0f;
		a_style.WindowPadding = ImVec2(24.0f, 20.0f);
		a_style.FramePadding = ImVec2(14.0f, (40.0f - 14.0f * kFontMetricScale) / 2.0f);  // 40px controls
		a_style.ItemSpacing = ImVec2(10.0f, 10.0f);
		a_style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
		a_style.ScrollbarSize = 12.0f;
		a_style.ScrollbarPadding = 3.0f;
		a_style.GrabMinSize = 20.0f;
		a_style.DisabledAlpha = 0.45f;
		a_style.SelectableTextAlign = ImVec2(0.0f, 0.5f);

		auto set = [&](ImGuiCol a_index, ImU32 a_color) { a_style.Colors[a_index] = ImGui::ColorConvertU32ToFloat4(a_color); };
		set(ImGuiCol_Text, kText);
		set(ImGuiCol_TextDisabled, kGhost);
		set(ImGuiCol_WindowBg, kFloor);
		set(ImGuiCol_ChildBg, IM_COL32(10, 8, 7, 90));
		set(ImGuiCol_PopupBg, kSurface);
		set(ImGuiCol_TitleBg, kSurface);
		set(ImGuiCol_TitleBgActive, kRaised);
		set(ImGuiCol_TitleBgCollapsed, kSurface);
		set(ImGuiCol_ModalWindowDimBg, IM_COL32(5, 4, 2, 179));
		set(ImGuiCol_Border, kFill3);
		set(ImGuiCol_FrameBg, IM_COL32(244, 235, 216, 10));
		set(ImGuiCol_FrameBgHovered, kFill2);
		set(ImGuiCol_FrameBgActive, kFill2);
		set(ImGuiCol_Button, kFill1);
		set(ImGuiCol_ButtonHovered, kFill2);
		set(ImGuiCol_ButtonActive, kFill3);
		set(ImGuiCol_Header, IM_COL32(245, 158, 11, 31));
		set(ImGuiCol_HeaderHovered, kFill2);
		set(ImGuiCol_HeaderActive, kFill3);
		set(ImGuiCol_CheckMark, kAmber);
		set(ImGuiCol_SliderGrab, kCopper);
		set(ImGuiCol_SliderGrabActive, kAmber);
		set(ImGuiCol_Separator, kBorder);
		set(ImGuiCol_SeparatorHovered, kAmber);
		set(ImGuiCol_NavCursor, kAmber);
		set(ImGuiCol_InputTextCursor, kAmberLight);
		set(ImGuiCol_ScrollbarBg, kFill1);
		set(ImGuiCol_ScrollbarGrab, IM_COL32(244, 235, 216, 56));
		set(ImGuiCol_ScrollbarGrabHovered, kCopper);
		set(ImGuiCol_ScrollbarGrabActive, kAmber);
		set(ImGuiCol_TextSelectedBg, IM_COL32(245, 158, 11, 64));
	}

	// Shared shape of the Vanilla and Nordic skins: no rounding, one-pixel rules.
	void SetSquareFrames(ImGuiStyle& a_style)
	{
		a_style.WindowRounding = 0.0f;
		a_style.ChildRounding = 0.0f;
		a_style.FrameRounding = 0.0f;
		a_style.PopupRounding = 0.0f;
		a_style.GrabRounding = 0.0f;
		a_style.ScrollbarRounding = 0.0f;
		a_style.WindowBorderSize = 1.0f;
		a_style.ChildBorderSize = 1.0f;
		a_style.FrameBorderSize = 1.0f;
		a_style.PopupBorderSize = 1.0f;
		a_style.WindowPadding = ImVec2(20.0f, 16.0f);
		a_style.FramePadding = ImVec2(10.0f, 6.0f);
		a_style.ItemSpacing = ImVec2(10.0f, 8.0f);
		a_style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
		a_style.ScrollbarSize = 10.0f;
		a_style.GrabMinSize = 16.0f;
		a_style.DisabledAlpha = 0.45f;
	}

	// The game's own menus: square black glass, white text, thin grey rules,
	// buttons that are only text until hovered.
	void SetVanillaStyle(ImGuiStyle& a_style)
	{
		constexpr ImU32 kWhite = IM_COL32(255, 255, 255, 255);
		constexpr ImU32 kDim = IM_COL32(150, 150, 150, 255);
		constexpr ImU32 kRule = IM_COL32(255, 255, 255, 70);
		constexpr ImU32 kTint1 = IM_COL32(255, 255, 255, 15);
		constexpr ImU32 kTint2 = IM_COL32(255, 255, 255, 35);
		constexpr ImU32 kTint3 = IM_COL32(255, 255, 255, 60);
		constexpr ImU32 kGlass = IM_COL32(0, 0, 0, 220);

		SetSquareFrames(a_style);
		auto set = [&](ImGuiCol a_index, ImU32 a_color) { a_style.Colors[a_index] = ImGui::ColorConvertU32ToFloat4(a_color); };
		set(ImGuiCol_Text, kWhite);
		set(ImGuiCol_TextDisabled, kDim);
		set(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 200));
		set(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
		set(ImGuiCol_PopupBg, IM_COL32(0, 0, 0, 235));
		set(ImGuiCol_TitleBg, kGlass);
		set(ImGuiCol_TitleBgActive, kGlass);
		set(ImGuiCol_TitleBgCollapsed, kGlass);
		set(ImGuiCol_Border, kRule);
		set(ImGuiCol_FrameBg, kTint1);
		set(ImGuiCol_FrameBgHovered, kTint2);
		set(ImGuiCol_FrameBgActive, kTint3);
		set(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
		set(ImGuiCol_ButtonHovered, kTint2);
		set(ImGuiCol_ButtonActive, kTint3);
		set(ImGuiCol_Header, kTint2);
		set(ImGuiCol_HeaderHovered, kTint2);
		set(ImGuiCol_HeaderActive, kTint3);
		set(ImGuiCol_CheckMark, kWhite);
		set(ImGuiCol_Separator, kRule);
		set(ImGuiCol_NavCursor, kWhite);
		set(ImGuiCol_InputTextCursor, kWhite);
		set(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
		set(ImGuiCol_ScrollbarGrab, kTint3);
		set(ImGuiCol_ScrollbarGrabHovered, kRule);
		set(ImGuiCol_ScrollbarGrabActive, kWhite);
		set(ImGuiCol_TextSelectedBg, kTint3);
	}

	// NORDIC UI: smoked black panel with no outline (corner ornaments instead),
	// white text, grey labels, grey highlights.
	void SetNordicStyle(ImGuiStyle& a_style)
	{
		constexpr ImU32 kGrey1 = IM_COL32(255, 255, 255, 15);
		constexpr ImU32 kGrey2 = IM_COL32(255, 255, 255, 35);
		constexpr ImU32 kGrey3 = IM_COL32(255, 255, 255, 60);

		SetSquareFrames(a_style);
		a_style.WindowBorderSize = 0.0f;
		a_style.WindowPadding = ImVec2(28.0f, 22.0f);  // room for the corner ornaments

		auto set = [&](ImGuiCol a_index, ImU32 a_color) { a_style.Colors[a_index] = ImGui::ColorConvertU32ToFloat4(a_color); };
		set(ImGuiCol_Text, kNordicText);
		set(ImGuiCol_TextDisabled, kNordicLabel);
		set(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 190));
		set(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
		set(ImGuiCol_PopupBg, IM_COL32(10, 10, 10, 235));
		set(ImGuiCol_Border, kNordicRule);
		set(ImGuiCol_FrameBg, kGrey1);
		set(ImGuiCol_FrameBgHovered, kGrey2);
		set(ImGuiCol_FrameBgActive, kGrey3);
		set(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
		set(ImGuiCol_ButtonHovered, kGrey2);
		set(ImGuiCol_ButtonActive, kGrey3);
		set(ImGuiCol_Header, kGrey2);
		set(ImGuiCol_HeaderHovered, kGrey2);
		set(ImGuiCol_HeaderActive, kGrey3);
		set(ImGuiCol_CheckMark, kNordicText);
		set(ImGuiCol_Separator, kNordicRule);
		set(ImGuiCol_NavCursor, kNordicText);
		set(ImGuiCol_InputTextCursor, kNordicText);
		set(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
		set(ImGuiCol_ScrollbarGrab, kGrey3);
		set(ImGuiCol_ScrollbarGrabHovered, kNordicRule);
		set(ImGuiCol_ScrollbarGrabActive, kNordicText);
		set(ImGuiCol_TextSelectedBg, kGrey3);
	}

	// --- Ornaments ------------------------------------------------------------

	// Line-drawn stand-in for NORDIC UI's knotwork corner: two nested L shapes
	// and a small square in the corner. a_x / a_y (+1 or -1) point inwards.
	void DrawCornerKnot(ImDrawList* a_draw, ImVec2 a_corner, float a_x, float a_y)
	{
		const float inset = 6.0f * g_scale;
		const float outer = 16.0f * g_scale;
		const float inner = 9.0f * g_scale;
		const float gap = 4.0f * g_scale;
		const float box = 3.0f * g_scale;
		const float thick = (std::max)(1.0f, g_scale);
		const auto  color = ImGui::GetColorU32(kNordicOrnament);
		const auto  at = [&](float a_dx, float a_dy) { return ImVec2(a_corner.x + a_x * a_dx, a_corner.y + a_y * a_dy); };

		const ImVec2 outerL[] = { at(inset + outer, inset), at(inset, inset), at(inset, inset + outer) };
		a_draw->AddPolyline(outerL, 3, color, ImDrawFlags_None, thick);
		const float  in = inset + gap;
		const ImVec2 innerL[] = { at(in + inner, in), at(in, in), at(in, in + inner) };
		a_draw->AddPolyline(innerL, 3, color, ImDrawFlags_None, thick);
		const ImVec2 a = at(in + gap, in + gap);
		const ImVec2 b = at(in + gap + box, in + gap + box);
		a_draw->AddRectFilled(ImVec2((std::min)(a.x, b.x), (std::min)(a.y, b.y)), ImVec2((std::max)(a.x, b.x), (std::max)(a.y, b.y)), color);
	}

	// Thin rule under the heading with a small diamond at its centre.
	void DrawHeadingRule()
	{
		const float half = ImGui::GetContentRegionAvail().x * 0.3f;
		const float diamond = 3.5f * g_scale;
		const float height = 8.0f * g_scale;
		const auto  origin = ImGui::GetCursorScreenPos();
		const float centreX = origin.x + ImGui::GetContentRegionAvail().x * 0.5f;
		const float y = origin.y + height * 0.5f;
		auto*       draw = ImGui::GetWindowDrawList();

		draw->AddLine(ImVec2(centreX - half, y), ImVec2(centreX - diamond * 2.0f, y), ImGui::GetColorU32(kNordicRule));
		draw->AddLine(ImVec2(centreX + diamond * 2.0f, y), ImVec2(centreX + half, y), ImGui::GetColorU32(kNordicRule));
		draw->AddQuadFilled(ImVec2(centreX, y - diamond), ImVec2(centreX + diamond, y), ImVec2(centreX, y + diamond),
			ImVec2(centreX - diamond, y), ImGui::GetColorU32(kNordicOrnament));
		ImGui::Dummy(ImVec2(0.0f, height));
	}

	// NORDIC UI's main-menu button: a frame with pointed ends, grey at rest,
	// white with a grey fill when hovered, brighter while held.
	bool PointedButton(const char* a_label)
	{
		const auto&  style = ImGui::GetStyle();
		const float  height = ImGui::GetFrameHeight();
		const ImVec2 text = ImGui::CalcTextSize(a_label);
		const ImVec2 size(text.x + style.FramePadding.x * 2.0f + height, height);
		const ImVec2 topLeft = ImGui::GetCursorScreenPos();

		const bool pressed = ImGui::InvisibleButton(a_label, size);
		const bool hovered = ImGui::IsItemHovered();
		const bool held = ImGui::IsItemActive();

		const ImVec2 bottomRight(topLeft.x + size.x, topLeft.y + size.y);
		const float  midY = (topLeft.y + bottomRight.y) * 0.5f;
		const float  tip = height * 0.5f;
		const ImVec2 frame[] = { { topLeft.x, midY }, { topLeft.x + tip, topLeft.y }, { bottomRight.x - tip, topLeft.y }, { bottomRight.x, midY },
			{ bottomRight.x - tip, bottomRight.y }, { topLeft.x + tip, bottomRight.y } };
		auto*        draw = ImGui::GetWindowDrawList();
		if (hovered || held) {
			draw->AddConvexPolyFilled(frame, 6, ImGui::GetColorU32(IM_COL32(255, 255, 255, held ? 60 : 30)));
		}
		const float thick = (std::max)(1.0f, g_scale) * (hovered ? 1.5f : 1.0f);
		draw->AddPolyline(frame, 6, ImGui::GetColorU32(hovered ? kNordicText : kNordicOrnament), ImDrawFlags_Closed, thick);
		draw->AddText(ImVec2(topLeft.x + (size.x - text.x) * 0.5f, midY - text.y * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), a_label);
		return pressed;
	}
}

namespace Theme
{
	void LoadFonts()
	{
		// Same face ImGui picks by itself at this window's sizes; also stands in
		// for any skin whose font files are missing.
		g_defaultFont = ImGui::GetIO().Fonts->AddFontDefaultVector();
		MergeWindowsFonts(L"segoeui.ttf");

		LoadSkinFonts(Skin::kTailor);
		LoadSkinFonts(Skin::kVanilla);
		LoadSkinFonts(Skin::kNordic);
	}

	void Apply(Skin a_skin, float a_displayHeight)
	{
		g_skin = a_skin;
		ImGuiStyle style;  // Dear ImGui's default dark style, used as is by Classic
		switch (a_skin) {
		case Skin::kTailor:
			SetTailorStyle(style);
			break;
		case Skin::kVanilla:
			SetVanillaStyle(style);
			break;
		case Skin::kNordic:
			SetNordicStyle(style);
			break;
		default:
			break;
		}

		ImFont* font = g_defaultFont;
		float   scale = ClassicScale(a_displayHeight);
		if (const auto* files = FontFilesOf(a_skin)) {
			const auto& loaded = *LoadedFontsOf(a_skin);
			font = loaded.body ? loaded.body : g_defaultFont;
			scale = ResolutionScale(a_displayHeight);
			style.FontSizeBase = files->body.size;  // also sizes the default font when the skin's is missing
		}
		g_scale = scale;
		style.ScaleAllSizes(scale);
		style.FontScaleMain = scale;
		ImGui::GetStyle() = style;  // Classic keeps FontSizeBase 0: NewFrame takes the default font's size
		ImGui::GetIO().FontDefault = font;
	}

	bool HasTitleBar()
	{
		return g_skin != Skin::kNordic;
	}

	void DecorateWindow()
	{
		if (g_skin != Skin::kNordic) {
			return;
		}
		auto*        draw = ImGui::GetWindowDrawList();
		const ImVec2 topLeft = ImGui::GetWindowPos();
		const ImVec2 size = ImGui::GetWindowSize();
		const ImVec2 bottomRight(topLeft.x + size.x, topLeft.y + size.y);
		DrawCornerKnot(draw, topLeft, 1.0f, 1.0f);
		DrawCornerKnot(draw, ImVec2(bottomRight.x, topLeft.y), -1.0f, 1.0f);
		DrawCornerKnot(draw, ImVec2(topLeft.x, bottomRight.y), 1.0f, -1.0f);
		DrawCornerKnot(draw, bottomRight, -1.0f, -1.0f);
	}

	void HeadingText(const char* a_text)
	{
		const auto* files = FontFilesOf(g_skin);
		const auto* loaded = LoadedFontsOf(g_skin);
		const bool  ownFont = files && loaded->heading;
		if (ownFont) {
			ImGui::PushFont(loaded->heading, files->heading.size);
		}
		if (g_skin == Skin::kNordic) {
			const float width = ImGui::CalcTextSize(a_text).x;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
		}
		ImGui::TextUnformatted(a_text);
		if (ownFont) {
			ImGui::PopFont();
		}
		if (g_skin == Skin::kNordic) {
			DrawHeadingRule();
		}
	}

	void LabelText(const char* a_text)
	{
		if (g_skin != Skin::kNordic) {
			ImGui::TextUnformatted(a_text);
			return;
		}
		ImGui::PushStyleColor(ImGuiCol_Text, kNordicLabel);
		ImGui::TextUnformatted(a_text);
		ImGui::PopStyleColor();
	}

	bool Button(const char* a_label)
	{
		return g_skin == Skin::kNordic ? PointedButton(a_label) : ImGui::Button(a_label);
	}
}
