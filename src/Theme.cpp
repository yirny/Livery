// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants  - font files per skin, Tailor's and Nordic's palettes
//   Fonts      - default font, each skin's body/heading fonts, Windows fallback fonts
//   Styles     - one ImGuiStyle per skin and its resolution scale
//   Textures   - NORDIC UI's corner knot, heading rope and MCM page arrow (white
//                alpha masks), the game's and NORDIC UI's equipped-item marks,
//                the game's checkbox and dropdown arrow
//   Ornaments  - Nordic's corner knots, heading rule and pointed button frame;
//                list rows (Tailor's tick row, the SkyUI-style tick row of the
//                Vanilla and Nordic skins); the dropdown arrows; the game's
//                checkbox of the Vanilla and Nordic skins; the fainter field
//                border; the selected single-choice row's mark
//   Theme      - LoadFonts / LoadTextures / Apply, then the skinned widgets
//                and the edge-to-edge lists (BeginList / EndList)
//
// The Tailor skin copies Tailor 3.0 (GPL-3.0), src/ui/imgui/Theme.cpp and
// TailorScreen.cpp: palette, radii, spacing, font faces, Windows font fallback
// and the resolution curve. Tailor ships its fonts; this plugin reads them from
// Tailor's folder. The Vanilla and Nordic skins approximate the game's menus and
// NORDIC UI with fonts this plugin ships (OFL): Barlow Condensed and Sovngarde.
// Nordic's ornaments and equipped mark are NORDIC UI's own art (used with the
// author's open permission, see textures/NORDIC-UI-CREDITS.txt); Vanilla's
// equipped mark and both skins' checkbox are the game's own
// (textures/BETHESDA-CREDITS.txt). A skin whose font files are missing draws
// with ImGui's default font; missing textures fall back to plain lines and
// triangles, and the checkbox to ImGui's own.
#include "PCH.h"

#include "Theme.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <imgui.h>
#include <imgui_internal.h>  // BeginComboPreview / EndComboPreview

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
	constexpr auto kTextureDir = L"Data/SKSE/Plugins/TailorOutfitCapture/textures";

	// Tailor's measured size adjustment for Poppins and Montserrat (its FontMetricScale).
	constexpr float kFontMetricScale = 4.0f / 3.0f;

	// Extra room under the heading (under its rule in Nordic) beyond the item spacing (pixels at 1080p).
	constexpr float kHeadingGapExtra = 16.0f;
	// Extra room above each field (label and control) beyond the item spacing (pixels at 1080p).
	constexpr float kSectionGap = 6.0f;

	// Tailor 3.0's tight lists: the gap between rows, and the dropdown popup's
	// padding (pixels at 1080p).
	constexpr float  kTailorListRowGap = 2.0f;
	constexpr ImVec2 kTailorDropdownPadding{ 14.0f, 10.0f };
	// The dropdown popup's padding in Vanilla and Nordic, which would otherwise
	// take the window's (Nordic's is wider for its corner ornaments; pixels at 1080p).
	constexpr ImVec2 kSquareDropdownPadding{ 20.0f, 12.0f };
	// Width of Tailor's amber rule on a selected row's left edge (pixels at 1080p).
	constexpr float kTailorRuleWidth = 2.0f;
	// Room above and below the text of a single-choice list row; its fill covers it (pixels at 1080p).
	constexpr float kSelectRowPadY = 4.0f;

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
	constexpr ImU32 kNordicOrnament = IM_COL32(222, 222, 222, 255);  // NORDIC UI's #dedede

	// A texture and the size it is drawn at, in pixels at 1080p before scaling
	// (the PNGs are rendered at six to eight times that from the UIs' vector art).
	struct Ornament
	{
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
		ImVec2                                           size;

		ImTextureID Id() const { return reinterpret_cast<ImTextureID>(view.Get()); }
	};

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
	Theme::Skin g_skin = Theme::Skin::kTailor;  // skin of the frame being built
	float       g_scale = 1.0f;                 // its resolution scale, for hand-drawn ornaments
	float       g_itemSpacingY = 0.0f;          // its item spacing on the back buffer, before any push
	ImVec2      g_listReach{};                  // how far the open BeginList list runs past the content, left and right
	Ornament    g_cornerKnot{ nullptr, ImVec2(25.0f, 25.0f) };
	Ornament    g_headingRope{ nullptr, ImVec2(169.0f, 7.0f) };
	Ornament    g_vanillaEquipped{ nullptr, ImVec2(20.0f, 20.0f) };  // the game's equipped mark, a white mask tinted on draw
	Ornament    g_nordicEquipped{ nullptr, ImVec2(23.0f, 23.0f) };   // NORDIC UI's, in its own two colours
	Ornament    g_checkBox{ nullptr, ImVec2(22.0f, 22.0f) };         // the game's checkbox, in its own colours
	Ornament    g_checkMark{ nullptr, ImVec2(16.5f, 16.5f) };        // its tick, centred on the box (the game's ratio)
	Ornament    g_dropdownArrow{ nullptr, ImVec2(13.0f, 16.0f) };    // the game's option stepper arrow, turned to point down
	Ornament    g_selectArrow{ nullptr, ImVec2(15.0f, 16.0f) };      // NORDIC UI's MCM page arrow, a white mask tinted on draw

	const SkinFonts& FontFilesOf(Theme::Skin a_skin)
	{
		switch (a_skin) {
		case Theme::Skin::kVanilla:
			return kVanillaFonts;
		case Theme::Skin::kNordic:
			return kNordicFonts;
		default:
			return kTailorFonts;
		}
	}

	LoadedFonts& LoadedFontsOf(Theme::Skin a_skin)
	{
		switch (a_skin) {
		case Theme::Skin::kVanilla:
			return g_vanillaFonts;
		case Theme::Skin::kNordic:
			return g_nordicFonts;
		default:
			return g_tailorFonts;
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
		const auto& files = FontFilesOf(a_skin);
		auto&       loaded = LoadedFontsOf(a_skin);
		const auto  folder = std::filesystem::current_path() / files.folder;
		loaded.body = LoadFont(folder, files.body);
		loaded.heading = LoadFont(folder, files.heading);
		if (loaded.body && loaded.heading) {
			logger::info("{} fonts loaded", files.skin);
		} else {
			logger::warn("{} fonts missing in {}; the skin uses the default font", files.skin, folder.string());
		}
	}

	// --- Textures -------------------------------------------------------------

	// Decodes a PNG with Windows Imaging Component into straight-alpha RGBA.
	// Contract: COM must be initialised on the calling thread.
	HRESULT DecodePng(IWICImagingFactory* a_wic, const wchar_t* a_path, UINT& a_width, UINT& a_height, std::vector<std::uint8_t>& a_pixels)
	{
		using Microsoft::WRL::ComPtr;
		ComPtr<IWICBitmapDecoder> decoder;
		if (const auto hr = a_wic->CreateDecoderFromFilename(a_path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder); FAILED(hr)) {
			return hr;
		}
		ComPtr<IWICBitmapFrameDecode> frame;
		if (const auto hr = decoder->GetFrame(0, &frame); FAILED(hr)) {
			return hr;
		}
		ComPtr<IWICFormatConverter> converter;
		if (const auto hr = a_wic->CreateFormatConverter(&converter); FAILED(hr)) {
			return hr;
		}
		if (const auto hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom); FAILED(hr)) {
			return hr;
		}
		if (const auto hr = converter->GetSize(&a_width, &a_height); FAILED(hr)) {
			return hr;
		}
		const UINT stride = a_width * 4;
		a_pixels.resize(static_cast<std::size_t>(stride) * a_height);
		return converter->CopyPixels(nullptr, stride, static_cast<UINT>(a_pixels.size()), a_pixels.data());
	}

	HRESULT CreateTexture(ID3D11Device* a_device, UINT a_width, UINT a_height, const std::uint8_t* a_pixels, ID3D11ShaderResourceView** a_view)
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = a_width;
		desc.Height = a_height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA                  data{ a_pixels, a_width * 4, 0 };
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		if (const auto hr = a_device->CreateTexture2D(&desc, &data, &texture); FAILED(hr)) {
			return hr;
		}
		return a_device->CreateShaderResourceView(texture.Get(), nullptr, a_view);
	}

	// Contract: render thread, on the game's D3D11 device; the view lives as
	// long as the process. Stays null (with a warning) when the file is missing.
	void LoadOrnament(ID3D11Device* a_device, IWICImagingFactory* a_wic, const std::filesystem::path& a_folder, const wchar_t* a_file, Ornament& a_out)
	{
		const auto                path = a_folder / a_file;
		UINT                      width = 0;
		UINT                      height = 0;
		std::vector<std::uint8_t> pixels;
		auto                      hr = DecodePng(a_wic, path.c_str(), width, height, pixels);
		if (SUCCEEDED(hr)) {
			hr = CreateTexture(a_device, width, height, pixels.data(), a_out.view.GetAddressOf());
		}
		if (FAILED(hr)) {
			a_out.view.Reset();
			logger::warn("Could not load {} (HRESULT {:#x}); the skin draws a plain stand-in instead", path.string(), static_cast<unsigned>(hr));
		}
	}

	// --- Styles ---------------------------------------------------------------

	// Tailor's own curve, shared by every skin: 1 up to 1080p, growing to 1.75
	// at 2160p.
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
		set(ImGuiCol_HeaderHovered, kRaised);  // Tailor's muted brown row hover
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

	// NORDIC UI's knotwork corner; the top-left texture is mirrored into the
	// other corners by drawing it from the corner inwards. Without the texture:
	// two nested L shapes and a small square. a_x / a_y (+1 or -1) point inwards.
	void DrawCornerKnot(ImDrawList* a_draw, ImVec2 a_corner, float a_x, float a_y)
	{
		if (g_cornerKnot.view) {
			const float  inset = 3.0f * g_scale;
			const ImVec2 start(a_corner.x + a_x * inset, a_corner.y + a_y * inset);
			const ImVec2 end(start.x + a_x * g_cornerKnot.size.x * g_scale, start.y + a_y * g_cornerKnot.size.y * g_scale);
			a_draw->AddImage(g_cornerKnot.Id(), start, end, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImGui::GetColorU32(kNordicOrnament));
			return;
		}
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

	// NORDIC UI's rope under the heading. Without the texture: a thin rule
	// with a small diamond at its centre.
	void DrawHeadingRule()
	{
		const float half = ImGui::GetContentRegionAvail().x * 0.3f;
		const float diamond = 3.5f * g_scale;
		const float height = 8.0f * g_scale;
		const auto  origin = ImGui::GetCursorScreenPos();
		const float centreX = origin.x + ImGui::GetContentRegionAvail().x * 0.5f;
		const float y = origin.y + height * 0.5f;
		auto*       draw = ImGui::GetWindowDrawList();

		if (g_headingRope.view) {
			const ImVec2 halfSize(g_headingRope.size.x * g_scale * 0.5f, g_headingRope.size.y * g_scale * 0.5f);
			draw->AddImage(g_headingRope.Id(), ImVec2(centreX - halfSize.x, y - halfSize.y), ImVec2(centreX + halfSize.x, y + halfSize.y),
				ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImGui::GetColorU32(kNordicOrnament));
			ImGui::Dummy(ImVec2(0.0f, height));
			return;
		}

		draw->AddLine(ImVec2(centreX - half, y), ImVec2(centreX - diamond * 2.0f, y), ImGui::GetColorU32(kNordicRule));
		draw->AddLine(ImVec2(centreX + diamond * 2.0f, y), ImVec2(centreX + half, y), ImGui::GetColorU32(kNordicRule));
		draw->AddQuadFilled(ImVec2(centreX, y - diamond), ImVec2(centreX + diamond, y), ImVec2(centreX, y + diamond),
			ImVec2(centreX - diamond, y), ImGui::GetColorU32(kNordicOrnament));
		ImGui::Dummy(ImVec2(0.0f, height));
	}

	// NORDIC UI's main-menu button: a frame with pointed ends, grey at rest,
	// white with a grey fill when hovered, brighter while held.
	float PointedButtonWidth(const char* a_label)
	{
		return ImGui::CalcTextSize(a_label).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFrameHeight();
	}

	bool PointedButton(const char* a_label)
	{
		const float  height = ImGui::GetFrameHeight();
		const ImVec2 text = ImGui::CalcTextSize(a_label);
		const ImVec2 size(PointedButtonWidth(a_label), height);
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

	// One row of a Theme::BeginList list, laid out as an invisible button that
	// runs edge to edge of the list; the text starts `inset` in, on the list's
	// indent, so it lines up with the content around the list.
	// Contract: the current window is a BeginList child (no horizontal padding).
	struct ListRow
	{
		ImVec2 min;  // screen rect, edge to edge
		ImVec2 max;
		float  inset;  // from min.x to where the text starts
		bool   pressed;
		bool   hovered;
	};

	ListRow LayOutListRow(const char* a_label, float a_height)
	{
		const ImVec2 cursor = ImGui::GetCursorScreenPos();
		const float  left = ImGui::GetWindowPos().x;
		const float  right = cursor.x + ImGui::GetContentRegionAvail().x;
		ImGui::SetCursorScreenPos(ImVec2(left, cursor.y));
		ListRow row{ ImVec2(left, cursor.y), ImVec2(right, cursor.y + a_height), cursor.x - left, false, false };
		row.pressed = ImGui::InvisibleButton(a_label, ImVec2((std::max)(1.0f, right - left), a_height));
		row.hovered = ImGui::IsItemHovered();
		return row;
	}

	// Tailor 3.0's selected list row: while ticked, the whole row in the faint
	// amber selection fill with an amber rule down its left edge; the muted
	// brown hover fill while hovered.
	bool TailorRow(const char* a_label, bool* a_ticked)
	{
		const float   height = ImGui::GetFrameHeight();
		const ListRow row = LayOutListRow(a_label, height);
		if (row.pressed) {
			*a_ticked = !*a_ticked;
		}

		auto* draw = ImGui::GetWindowDrawList();
		if (*a_ticked) {
			draw->AddRectFilled(row.min, row.max, ImGui::GetColorU32(ImGuiCol_Header));
		}
		if (row.hovered) {
			draw->AddRectFilled(row.min, row.max, ImGui::GetColorU32(ImGuiCol_HeaderHovered));
		}
		if (*a_ticked) {
			draw->AddRectFilled(row.min, ImVec2(row.min.x + kTailorRuleWidth * g_scale, row.max.y), ImGui::GetColorU32(kAmber));
		}
		const ImVec2 text = ImGui::CalcTextSize(a_label);
		draw->AddText(ImVec2(row.min.x + row.inset, row.min.y + (height - text.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), a_label);
		return row.pressed;
	}

	// A SkyUI-style inventory row: while ticked, a white bar fading in from the
	// left edge to the right and the skin's equipped mark at the left; a faint
	// tint while hovered. The mark's column stays on unticked rows so the names
	// line up. Without the texture the mark is a small filled triangle.
	bool InventoryRow(const char* a_label, bool* a_ticked)
	{
		constexpr ImU32 kVanillaMark = IM_COL32(204, 204, 204, 255);  // the game's #cccccc
		constexpr int   kBarAlpha = 70;                               // of the bar's white right end

		const auto&   style = ImGui::GetStyle();
		const float   height = ImGui::GetFrameHeight();
		const ListRow row = LayOutListRow(a_label, height);
		if (row.pressed) {
			*a_ticked = !*a_ticked;
		}

		const ImVec2 topLeft = row.min;
		const ImVec2 bottomRight = row.max;
		const float  midY = (topLeft.y + bottomRight.y) * 0.5f;
		auto*        draw = ImGui::GetWindowDrawList();
		if (*a_ticked) {
			const ImU32 clear = IM_COL32(255, 255, 255, 0);
			const ImU32 white = IM_COL32(255, 255, 255, kBarAlpha);
			draw->AddRectFilledMultiColor(topLeft, bottomRight, clear, white, white, clear);
		}
		if (row.hovered) {
			draw->AddRectFilled(topLeft, bottomRight, ImGui::GetColorU32(ImGuiCol_HeaderHovered));
		}

		const bool      nordic = g_skin == Theme::Skin::kNordic;
		const Ornament& mark = nordic ? g_nordicEquipped : g_vanillaEquipped;
		const ImVec2    markSize(mark.size.x * g_scale, mark.size.y * g_scale);
		const ImVec2    markTopLeft(topLeft.x + row.inset, midY - markSize.y * 0.5f);
		if (*a_ticked) {
			if (mark.view) {
				draw->AddImage(mark.Id(), markTopLeft, ImVec2(markTopLeft.x + markSize.x, markTopLeft.y + markSize.y),
					ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), nordic ? IM_COL32_WHITE : kVanillaMark);
			} else {
				const float centreX = markTopLeft.x + markSize.x * 0.5f;
				const float half = markSize.y * 0.3f;
				draw->AddTriangleFilled(ImVec2(centreX - half * 0.6f, midY - half), ImVec2(centreX + half * 0.8f, midY),
					ImVec2(centreX - half * 0.6f, midY + half), ImGui::GetColorU32(nordic ? kNordicOrnament : kVanillaMark));
			}
		}
		const ImVec2 text = ImGui::CalcTextSize(a_label);
		draw->AddText(ImVec2(markTopLeft.x + markSize.x + style.ItemInnerSpacing.x, midY - text.y * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), a_label);
		return row.pressed;
	}

	// Tailor draws a dropdown's arrow itself too: a small amber chevron.
	bool HasOwnDropdownArrow()
	{
		return g_skin == Theme::Skin::kTailor;
	}

	// Width and height the dropdown arrow is drawn at.
	ImVec2 DropdownArrowSize()
	{
		if (HasOwnDropdownArrow()) {
			const float width = ImGui::GetFontSize() * 0.4f;
			return ImVec2(width, width * 0.6f);
		}
		return ImVec2(g_dropdownArrow.size.x * g_scale, g_dropdownArrow.size.y * g_scale);
	}

	// Tailor's chevron, or the game's texture untinted but through the style alpha.
	void DrawDropdownArrow(ImDrawList* a_draw, ImVec2 a_topLeft, ImVec2 a_size)
	{
		if (HasOwnDropdownArrow()) {
			const ImVec2 points[] = { a_topLeft, { a_topLeft.x + a_size.x * 0.5f, a_topLeft.y + a_size.y }, { a_topLeft.x + a_size.x, a_topLeft.y } };
			a_draw->AddPolyline(points, 3, ImGui::GetColorU32(ImGuiCol_CheckMark), ImDrawFlags_None, (std::max)(1.0f, 1.5f * g_scale));
			return;
		}
		a_draw->AddImage(g_dropdownArrow.Id(), a_topLeft, ImVec2(a_topLeft.x + a_size.x, a_topLeft.y + a_size.y), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
			ImGui::GetColorU32(IM_COL32_WHITE));
	}

	// Vanilla and Nordic draw the game's checkbox when both of its textures loaded.
	bool HasGameCheckbox()
	{
		return (g_skin == Theme::Skin::kVanilla || g_skin == Theme::Skin::kNordic) && g_checkBox.view && g_checkMark.view;
	}

	// Vanilla and Nordic draw the game's dropdown arrow when its texture loaded.
	bool HasGameDropdownArrow()
	{
		return (g_skin == Theme::Skin::kVanilla || g_skin == Theme::Skin::kNordic) && g_dropdownArrow.view;
	}

	// Whether this skin replaces ImGui's combo arrow (and so draws the preview itself).
	bool HasCustomDropdownArrow()
	{
		return HasOwnDropdownArrow() || HasGameDropdownArrow();
	}

	// The preview of a combo opened with ImGuiComboFlags_CustomPreview: the
	// text clipped short of the arrow's column, then the skin's arrow at the
	// right edge of the frame.
	// Contract: called right after the combo's BeginCombo / EndCombo block,
	// where the combo frame is still the last item (BeginComboPreview asserts it).
	void DrawCustomComboPreview(const char* a_preview)
	{
		const auto&  style = ImGui::GetStyle();
		const ImVec2 frameMin = ImGui::GetItemRectMin();
		const ImVec2 frameMax = ImGui::GetItemRectMax();
		const ImVec2 arrow = DropdownArrowSize();
		const float  arrowLeft = frameMax.x - style.FramePadding.x - arrow.x;

		if (!ImGui::BeginComboPreview()) {
			return;
		}
		ImGui::PushClipRect(frameMin, ImVec2(arrowLeft - style.ItemInnerSpacing.x, frameMax.y), true);
		ImGui::TextUnformatted(a_preview);
		ImGui::PopClipRect();
		ImGui::EndComboPreview();

		const float midY = (frameMin.y + frameMax.y) * 0.5f;
		DrawDropdownArrow(ImGui::GetWindowDrawList(), ImVec2(arrowLeft, midY - arrow.y * 0.5f), arrow);
	}

	// The game's diamond box with its top-left at a_topLeft, and the tick
	// centred on it when ticked. Untinted, but through the style alpha, so it
	// fades inside BeginDisabled.
	void DrawGameCheckbox(ImDrawList* a_draw, ImVec2 a_topLeft, bool a_ticked)
	{
		const ImU32  tint = ImGui::GetColorU32(IM_COL32_WHITE);
		const ImVec2 box(g_checkBox.size.x * g_scale, g_checkBox.size.y * g_scale);
		a_draw->AddImage(g_checkBox.Id(), a_topLeft, ImVec2(a_topLeft.x + box.x, a_topLeft.y + box.y), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
		if (!a_ticked) {
			return;
		}
		const ImVec2 mark(g_checkMark.size.x * g_scale, g_checkMark.size.y * g_scale);
		const ImVec2 markTopLeft(a_topLeft.x + (box.x - mark.x) * 0.5f, a_topLeft.y + (box.y - mark.y) * 0.5f);
		a_draw->AddImage(g_checkMark.Id(), markTopLeft, ImVec2(markTopLeft.x + mark.x, markTopLeft.y + mark.y), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
	}

	// The game's settings checkbox: the box, a faint diamond over it while
	// hovered, then the label; the box and label are one click target.
	// Returns true when pressed; the caller changes the value. Drawn as it
	// will be after the press: a checkbox turns over, a radio button turns on.
	bool GameCheckbox(const char* a_label, bool a_ticked, bool a_radio)
	{
		const auto&  style = ImGui::GetStyle();
		const float  height = ImGui::GetFrameHeight();
		const ImVec2 box(g_checkBox.size.x * g_scale, g_checkBox.size.y * g_scale);
		const ImVec2 text = ImGui::CalcTextSize(a_label);
		const ImVec2 topLeft = ImGui::GetCursorScreenPos();

		const bool pressed = ImGui::InvisibleButton(a_label, ImVec2(box.x + style.ItemInnerSpacing.x + text.x, height));
		const bool hovered = ImGui::IsItemHovered();
		const bool ticked = pressed ? (a_radio || !a_ticked) : a_ticked;

		auto*        draw = ImGui::GetWindowDrawList();
		const float  midY = topLeft.y + height * 0.5f;
		const ImVec2 boxTopLeft(topLeft.x, midY - box.y * 0.5f);
		DrawGameCheckbox(draw, boxTopLeft, ticked);
		if (hovered) {
			const float centreX = boxTopLeft.x + box.x * 0.5f;
			draw->AddQuadFilled(ImVec2(centreX, boxTopLeft.y), ImVec2(boxTopLeft.x + box.x, midY), ImVec2(centreX, boxTopLeft.y + box.y),
				ImVec2(boxTopLeft.x, midY), ImGui::GetColorU32(ImGuiCol_FrameBgHovered));
		}
		draw->AddText(ImVec2(boxTopLeft.x + box.x + style.ItemInnerSpacing.x, midY - text.y * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), a_label);
		return pressed;
	}

	// Vanilla and Nordic frame dropdowns and text fields with a fainter rule
	// than the lists' and window's. Returns true when it pushed a colour.
	bool PushFieldBorder()
	{
		constexpr float kFieldBorderAlpha = 0.5f;  // of the skin's border colour

		if (g_skin == Theme::Skin::kTailor) {
			return false;
		}
		ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Border);
		color.w *= kFieldBorderAlpha;
		ImGui::PushStyleColor(ImGuiCol_Border, color);
		return true;
	}

	// The mark left of a selected single-choice row's text: NORDIC UI's MCM
	// page arrow in Vanilla and Nordic (a small triangle without the texture),
	// Tailor's amber rule on the row's left edge.
	void DrawSelectMark(ImDrawList* a_draw, ImVec2 a_rowMin, ImVec2 a_rowMax, float a_textX)
	{
		if (g_skin == Theme::Skin::kTailor) {
			a_draw->AddRectFilled(a_rowMin, ImVec2(a_rowMin.x + kTailorRuleWidth * g_scale, a_rowMax.y), ImGui::GetColorU32(kAmber));
			return;
		}
		// Centred in the indent between the row's left edge and its text.
		const ImU32  tint = ImGui::GetColorU32(g_skin == Theme::Skin::kNordic ? kNordicOrnament : IM_COL32_WHITE);
		const ImVec2 size(g_selectArrow.size.x * g_scale, g_selectArrow.size.y * g_scale);
		const ImVec2 centre((a_rowMin.x + a_textX) * 0.5f, (a_rowMin.y + a_rowMax.y) * 0.5f);
		const ImVec2 topLeft(centre.x - size.x * 0.5f, centre.y - size.y * 0.5f);
		if (g_selectArrow.view) {
			a_draw->AddImage(g_selectArrow.Id(), topLeft, ImVec2(topLeft.x + size.x, topLeft.y + size.y), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
			return;
		}
		const float half = size.y * 0.3f;
		a_draw->AddTriangleFilled(ImVec2(centre.x - half * 0.6f, centre.y - half), ImVec2(centre.x + half * 0.8f, centre.y),
			ImVec2(centre.x - half * 0.6f, centre.y + half), tint);
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

	void LoadTextures(ID3D11Device* a_device)
	{
		// The game's render thread already has COM; S_FALSE / RPC_E_CHANGED_MODE just mean that.
		const auto                                 init = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
		const auto                                 hr = ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
		if (FAILED(hr)) {
			logger::warn("WIC unavailable (HRESULT {:#x}); the skins draw plain stand-ins instead", static_cast<unsigned>(hr));
		} else {
			const auto folder = std::filesystem::current_path() / kTextureDir;
			LoadOrnament(a_device, wic.Get(), folder, L"nordic-corner.png", g_cornerKnot);
			LoadOrnament(a_device, wic.Get(), folder, L"nordic-heading.png", g_headingRope);
			LoadOrnament(a_device, wic.Get(), folder, L"vanilla-equipped.png", g_vanillaEquipped);
			LoadOrnament(a_device, wic.Get(), folder, L"nordic-equipped.png", g_nordicEquipped);
			LoadOrnament(a_device, wic.Get(), folder, L"vanilla-checkbox.png", g_checkBox);
			LoadOrnament(a_device, wic.Get(), folder, L"vanilla-checkmark.png", g_checkMark);
			LoadOrnament(a_device, wic.Get(), folder, L"vanilla-dropdown.png", g_dropdownArrow);
			LoadOrnament(a_device, wic.Get(), folder, L"nordic-select-arrow.png", g_selectArrow);
		}
		wic.Reset();
		if (SUCCEEDED(init)) {
			::CoUninitialize();
		}
	}

	void Apply(Skin a_skin, float a_displayHeight)
	{
		g_skin = a_skin;
		ImGuiStyle style;
		switch (a_skin) {
		case Skin::kVanilla:
			SetVanillaStyle(style);
			break;
		case Skin::kNordic:
			SetNordicStyle(style);
			break;
		default:
			SetTailorStyle(style);
			break;
		}

		const auto& loaded = LoadedFontsOf(a_skin);
		const float scale = ResolutionScale(a_displayHeight);
		g_scale = scale;
		style.FontSizeBase = FontFilesOf(a_skin).body.size;  // also sizes the default font when the skin's is missing
		style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
		style.ScaleAllSizes(scale);
		style.FontScaleMain = scale;
		g_itemSpacingY = style.ItemSpacing.y;
		ImGui::GetStyle() = style;
		ImGui::GetIO().FontDefault = loaded.body ? loaded.body : g_defaultFont;
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

	void HeadingText(const char* a_text, const char* a_subtitle)
	{
		const auto& files = FontFilesOf(g_skin);
		const auto& loaded = LoadedFontsOf(g_skin);
		const bool  ownFont = loaded.heading != nullptr;
		const bool  nordic = g_skin == Skin::kNordic;
		const bool  subtitled = a_subtitle && *a_subtitle;
		const auto  centre = [](float a_width) {
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, (ImGui::GetContentRegionAvail().x - a_width) * 0.5f));
		};
		constexpr float kSubtitleGapTrim = 4.0f;  // how much closer the subtitle sits under the heading than the item spacing (pixels at 1080p)
		constexpr float kRuleGapExtra = 4.0f;     // Nordic: extra room above the rule beyond the item spacing (pixels at 1080p)

		// The gap below an item comes from the item spacing in force while it is laid out:
		// the subtitle sits close under the heading, and the wider gap follows only the
		// last element (the rule in Nordic).
		// Contract: every page's body opens with a LabelText, whose section gap
		// completes the heading's gap; the heading leaves that much out.
		const ImVec2 spacing = ImGui::GetStyle().ItemSpacing;
		const ImVec2 wider(spacing.x, spacing.y + (kHeadingGapExtra - kSectionGap) * g_scale);
		const ImVec2 tight(spacing.x, (std::max)(0.0f, spacing.y - kSubtitleGapTrim * g_scale));
		const ImVec2 aboveRule(spacing.x, spacing.y + kRuleGapExtra * g_scale);
		const ImVec2 afterLast = nordic ? aboveRule : wider;

		if (ownFont) {
			ImGui::PushFont(loaded.heading, files.heading.size);
		}
		if (nordic) {
			centre(ImGui::CalcTextSize(a_text).x);
		}
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, subtitled ? tight : afterLast);
		ImGui::TextUnformatted(a_text);
		ImGui::PopStyleVar();
		if (ownFont) {
			ImGui::PopFont();
		}

		if (subtitled) {
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, afterLast);
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			// Nordic centres a subtitle that fits on one line; a longer one wraps from the left.
			const float width = ImGui::CalcTextSize(a_subtitle).x;
			if (nordic && width <= ImGui::GetContentRegionAvail().x) {
				centre(width);
				ImGui::TextUnformatted(a_subtitle);
			} else {
				ImGui::TextWrapped("%s", a_subtitle);
			}
			ImGui::PopStyleColor();
			ImGui::PopStyleVar();
		}

		if (nordic) {
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, wider);
			DrawHeadingRule();
			ImGui::PopStyleVar();
		}
	}

	float Scale()
	{
		return g_scale;
	}

	void SectionGap()
	{
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + kSectionGap * g_scale);
	}

	void LabelText(const char* a_text)
	{
		constexpr float kLabelGapTrim = 2.0f;  // how much closer the label sits to its control than the item spacing (pixels at 1080p)

		SectionGap();
		// The gap below an item comes from the item spacing in force while it is laid out.
		const auto& spacing = ImGui::GetStyle().ItemSpacing;
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing.x, (std::max)(0.0f, spacing.y - kLabelGapTrim * g_scale)));
		if (g_skin == Skin::kNordic) {
			ImGui::PushStyleColor(ImGuiCol_Text, kNordicLabel);
		}
		ImGui::TextUnformatted(a_text);
		if (g_skin == Skin::kNordic) {
			ImGui::PopStyleColor();
		}
		ImGui::PopStyleVar();
	}

	bool Button(const char* a_label)
	{
		return g_skin == Skin::kNordic ? PointedButton(a_label) : ImGui::Button(a_label);
	}

	float ButtonWidth(const char* a_label)
	{
		if (g_skin == Skin::kNordic) {
			return PointedButtonWidth(a_label);
		}
		return ImGui::CalcTextSize(a_label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	}

	bool TickRow(const char* a_label, bool* a_ticked)
	{
		return g_skin == Skin::kTailor ? TailorRow(a_label, a_ticked) : InventoryRow(a_label, a_ticked);
	}

	bool Checkbox(const char* a_label, bool* a_value)
	{
		if (HasGameCheckbox()) {
			const bool pressed = GameCheckbox(a_label, *a_value, false);
			if (pressed) {
				*a_value = !*a_value;
			}
			return pressed;
		}
		if (g_skin != Skin::kTailor) {
			return ImGui::Checkbox(a_label, a_value);
		}
		// Tailor: ImGui's checkbox with a smaller, rounder box than its 40px
		// controls would give (the box is the frame height).
		constexpr float kBoxPaddingY = 2.0f;  // pixels at 1080p, above and below the tick
		constexpr float kBoxRounding = 5.0f;  // pixels at 1080p

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, kBoxPaddingY * g_scale));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kBoxRounding * g_scale);
		const bool toggled = ImGui::Checkbox(a_label, a_value);
		ImGui::PopStyleVar(2);
		return toggled;
	}

	bool RadioButton(const char* a_label, bool a_active)
	{
		return HasGameCheckbox() ? GameCheckbox(a_label, a_active, true) : ImGui::RadioButton(a_label, a_active);
	}

	bool InputText(const char* a_id, char* a_buffer, std::size_t a_size, int a_flags)
	{
		const bool pushed = PushFieldBorder();
		const bool result = ImGui::InputText(a_id, a_buffer, a_size, a_flags);
		if (pushed) {
			ImGui::PopStyleColor();
		}
		return result;
	}

	bool DrawTickedBox(float a_right, float a_midY)
	{
		if (!HasGameCheckbox()) {
			return false;
		}
		const ImVec2 box(g_checkBox.size.x * g_scale, g_checkBox.size.y * g_scale);
		DrawGameCheckbox(ImGui::GetWindowDrawList(), ImVec2(a_right - box.x, a_midY - box.y * 0.5f), true);
		return true;
	}

	bool BeginCombo(const char* a_id, const char* a_preview, int a_flags)
	{
		// The popup takes the window padding and border colour in force as BeginCombo opens it.
		const ImVec2 padding = g_skin == Skin::kTailor ? kTailorDropdownPadding : kSquareDropdownPadding;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding.x * g_scale, padding.y * g_scale));
		const bool pushedBorder = PushFieldBorder();
		bool       open = false;
		if (HasCustomDropdownArrow()) {
			// ImGui requires an empty preview with CustomPreview; EndCombo draws it.
			open = ImGui::BeginCombo(a_id, "", a_flags | ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_CustomPreview);
		} else {
			open = ImGui::BeginCombo(a_id, a_preview, a_flags);
		}
		if (pushedBorder) {
			ImGui::PopStyleColor();
		}
		ImGui::PopStyleVar();
		return open;
	}

	void EndCombo(bool a_open, const char* a_preview)
	{
		if (a_open) {
			ImGui::EndCombo();
		}
		if (HasCustomDropdownArrow()) {
			DrawCustomComboPreview(a_preview);
		}
	}

	float ListPaddingY()
	{
		// The piece list's row gap, whatever item spacing a list's rows push.
		return ListRowGap(g_itemSpacingY);
	}

	float ListRowGap(float a_fallback)
	{
		return g_skin == Skin::kTailor ? kTailorListRowGap * g_scale : a_fallback;
	}

	bool BeginList(const char* a_id, float a_height)
	{
		// Past the content across the window's padding to its edges; in a table,
		// only on the table's outer sides, so the gap between columns stays clear.
		const float padding = ImGui::GetStyle().WindowPadding.x;
		float       left = padding;
		float       right = padding;
		if (ImGui::GetCurrentTable()) {
			const int column = ImGui::TableGetColumnIndex();
			left = column == 0 ? padding : 0.0f;
			right = column == ImGui::TableGetColumnCount() - 1 ? padding : 0.0f;
		}
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() - left);
		const float width = ImGui::GetContentRegionAvail().x + right;
		g_listReach = ImVec2(left, right);

		// A child without a border drops its padding unless told to keep it.
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, ListPaddingY()));
		const bool visible = ImGui::BeginChild(a_id, ImVec2(width, a_height), ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::PopStyleVar();
		if (visible && left > 0.0f) {
			ImGui::Indent(left);  // the rows' text lines up with the content around the list (0 would mean the default indent)
		}
		return visible;
	}

	void EndList()
	{
		ImGui::EndChild();
		// The rules span the content only; the rows run on past it.
		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		const float  left = min.x + g_listReach.x;
		const float  right = max.x - g_listReach.y;
		auto*        draw = ImGui::GetWindowDrawList();
		const ImU32  color = ImGui::GetColorU32(ImGuiCol_Border);
		draw->AddLine(ImVec2(left, min.y + 0.5f), ImVec2(right, min.y + 0.5f), color);
		draw->AddLine(ImVec2(left, max.y - 0.5f), ImVec2(right, max.y - 0.5f), color);
	}

	float SelectRowHeight()
	{
		return ImGui::GetTextLineHeight() + kSelectRowPadY * 2.0f * g_scale;
	}

	float SelectRowGap()
	{
		constexpr float kSelectRowGap = 2.0f;  // pixels at 1080p, between two rows' fills
		return kSelectRowGap * g_scale;
	}

	bool SelectRow(const char* a_label, bool a_selected)
	{
		constexpr float kTextShift = 4.0f;  // pixels at 1080p, right of the list's indent

		const ListRow row = LayOutListRow(a_label, SelectRowHeight());
		const float   textX = row.min.x + row.inset + kTextShift * g_scale;
		auto*         draw = ImGui::GetWindowDrawList();
		if (a_selected || row.hovered) {
			draw->AddRectFilled(row.min, row.max, ImGui::GetColorU32(a_selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered));
		}
		if (a_selected) {
			DrawSelectMark(draw, row.min, row.max, textX);
		}
		draw->AddText(ImVec2(textX, row.min.y + kSelectRowPadY * g_scale), ImGui::GetColorU32(ImGuiCol_Text), a_label);
		return row.pressed;
	}
}
