// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants  - Tailor's font sizes and palette
//   Fonts      - default font, Tailor's Poppins/Montserrat, Windows fallback fonts
//   Styles     - one ImGuiStyle per skin and its resolution scale
//   Theme      - LoadFonts / Apply / HeadingText
//
// The Tailor skin copies Tailor 3.0 (GPL-3.0), src/ui/imgui/Theme.cpp and
// TailorScreen.cpp: palette, radii, spacing, font faces, Windows font fallback
// and the resolution curve. Tailor ships the fonts; this plugin reads them from
// Tailor's folder and falls back to ImGui's default font when they are missing.
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

	// Tailor's measured size adjustment for Poppins and Montserrat (its FontMetricScale).
	constexpr float kFontMetricScale = 4.0f / 3.0f;
	constexpr float kBodySize = 14.0f * kFontMetricScale;     // pixels at 1080p, before scaling
	constexpr float kHeadingSize = 18.0f * kFontMetricScale;  // pixels at 1080p, before scaling

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

	struct Fonts
	{
		ImFont* classic = nullptr;
		ImFont* body = nullptr;     // nullptr when Tailor's font file is missing
		ImFont* heading = nullptr;  // nullptr when Tailor's font file is missing
	};

	// Render thread only.
	Fonts       g_fonts;
	Theme::Skin g_skin = Theme::Skin::kClassic;  // skin of the frame being built

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

	// --- Styles ---------------------------------------------------------------

	// Readable at 1440p/4K with ImGui's default font.
	float ClassicScale(float a_height)
	{
		return (std::max)(1.0f, a_height / 1080.0f * 1.5f);
	}

	// Tailor's own curve: 1 up to 1080p, growing to 1.75 at 2160p.
	float TailorScale(float a_height)
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
}

namespace Theme
{
	void LoadFonts()
	{
		// Same face ImGui picks by itself at this window's sizes; also used by
		// the Tailor skin when Tailor's fonts are missing.
		g_fonts.classic = ImGui::GetIO().Fonts->AddFontDefaultVector();
		MergeWindowsFonts(L"segoeui.ttf");

		const auto folder = std::filesystem::current_path() / kTailorFontDir;
		g_fonts.body = AddFont(ReadFont(folder / L"Poppins-Regular.ttf"), kBodySize, false);
		if (g_fonts.body) {
			MergeWindowsFonts(L"segoeui.ttf");
		}
		g_fonts.heading = AddFont(ReadFont(folder / L"Montserrat-Black.ttf"), kHeadingSize, false);
		if (g_fonts.heading) {
			MergeWindowsFonts(L"seguibl.ttf");
		}

		if (g_fonts.body && g_fonts.heading) {
			logger::info("Tailor fonts loaded");
		} else {
			logger::warn("Tailor fonts missing in {}; the Tailor skin uses the default font", folder.string());
		}
	}

	void Apply(Skin a_skin, float a_displayHeight)
	{
		g_skin = a_skin;
		ImGuiStyle style;  // Dear ImGui's default dark style
		float      scale = ClassicScale(a_displayHeight);
		ImFont*    font = g_fonts.classic;
		if (a_skin == Skin::kTailor) {
			SetTailorStyle(style);
			scale = TailorScale(a_displayHeight);
			font = g_fonts.body ? g_fonts.body : g_fonts.classic;
		}
		style.ScaleAllSizes(scale);
		style.FontScaleMain = scale;
		ImGui::GetStyle() = style;  // FontSizeBase 0: NewFrame takes the default font's size
		ImGui::GetIO().FontDefault = font;
	}

	void HeadingText(const char* a_text)
	{
		if (g_skin != Skin::kTailor || !g_fonts.heading) {
			ImGui::TextUnformatted(a_text);
			return;
		}
		ImGui::PushFont(g_fonts.heading, kHeadingSize);
		ImGui::TextUnformatted(a_text);
		ImGui::PopFont();
	}
}
