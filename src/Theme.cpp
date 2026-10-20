// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants  - font files per skin, Tailor's and Nordic's palettes
//   Fonts      - default font, each skin's body/heading fonts, Windows fallback fonts
//   Styles     - one ImGuiStyle per skin and its resolution scale
//   Textures   - NORDIC UI's corner knot, heading rope and MCM page arrow (white
//                alpha masks), the game's and NORDIC UI's equipped-item marks,
//                the game's checkbox and dropdown arrow; the row hover glow, made
//                in code
//   Ornaments  - Nordic's corner knots, heading rule and pointed button frame;
//                list rows (Tailor's tick row, the SkyUI-style tick row of the
//                Vanilla and Nordic skins) and their hover fill (Vanilla's and
//                Nordic's soft ellipse); the dropdown arrows; the game's
//                checkbox of the Vanilla and Nordic skins; the fainter field
//                border; the selected single-choice row's mark; an outfit row's
//                tags (Tailor 3.0's chips and gender pills, or grey text)
//   Labels     - a field label's line spacing; the colour of clickable labels;
//                LabelLink's plus-in-square mark
//   Theme      - LoadFonts / LoadTextures / Apply, then the skinned widgets, the
//                lists between rules (BeginList / EndList) and their columns
//                (HeaderRow over multi-column tick rows), list headings (and the
//                outfit lists' folding ones) and outfit rows with tags
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
#include <imgui_internal.h>  // BeginComboPreview / EndComboPreview, RenderTextEllipsis

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <string>

namespace
{
	constexpr auto kTailorFontDir = L"Data/SKSE/Plugins/Tailor/fonts";
	constexpr auto kOwnFontDir = L"Data/SKSE/Plugins/Livery/fonts";
	constexpr auto kTextureDir = L"Data/SKSE/Plugins/Livery/textures";

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
	// Clear room between that rule and the row's text (pixels at 1080p).
	constexpr float kTailorRuleGap = 8.0f;
	// Room above and below the text of a single-choice list row; its fill covers it (pixels at 1080p).
	constexpr float kSelectRowPadY = 4.0f;
	// Room between an outfit row's name and its tags (OutfitRow; pixels at 1080p).
	constexpr float kOutfitTagsGap = 2.0f;
	// A tab of the tab column, a square with its fill, and the room between the tabs and the
	// rule beside them (pixels at 1080p).
	constexpr float kTabSide = 34.0f;
	constexpr float kTabRuleGap = 8.0f;
	// How far right of the list's indent a list row's text sits in Vanilla and
	// Nordic (pixels at 1080p); Tailor clears its rule instead (RowTextShift).
	constexpr float kRowTextShift = 4.0f;
	// Room above and below an underlined field's text, between it and its rule (pixels at 1080p).
	constexpr float kUnderlinedPadY = 4.0f;
	// Width (and height) of a field's icon, of the font size.
	constexpr float kFieldIconRatio = 0.9f;

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
	constexpr ImU32 kCopperLight = IM_COL32(217, 149, 99, 255);
	constexpr ImU32 kRose = IM_COL32(224, 160, 142, 255);   // the Female tag
	constexpr ImU32 kSteel = IM_COL32(158, 184, 217, 255);  // the Male tag
	constexpr ImU32 kText = IM_COL32(244, 235, 216, 255);
	constexpr ImU32 kGhost = IM_COL32(244, 235, 216, 102);
	constexpr ImU32 kBorder = IM_COL32(244, 235, 216, 20);
	constexpr ImU32 kFill1 = IM_COL32(244, 235, 216, 8);
	constexpr ImU32 kFill2 = IM_COL32(244, 235, 216, 15);
	constexpr ImU32 kFill3 = IM_COL32(244, 235, 216, 26);

	constexpr ImU32 kErrorText = IM_COL32(255, 102, 102, 255);  // every skin's error red

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
	Ornament    g_tabRope{ nullptr, ImVec2(7.0f, 48.0f) };           // one repeat of NORDIC UI's level-up rope, tiled down the tab rule
	Ornament    g_vanillaEquipped{ nullptr, ImVec2(20.0f, 20.0f) };  // the game's equipped mark, a white mask tinted on draw
	Ornament    g_nordicEquipped{ nullptr, ImVec2(23.0f, 23.0f) };   // NORDIC UI's, in its own two colours
	Ornament    g_checkBox{ nullptr, ImVec2(22.0f, 22.0f) };         // the game's checkbox, in its own colours
	Ornament    g_checkMark{ nullptr, ImVec2(16.5f, 16.5f) };        // its tick, centred on the box (the game's ratio)
	Ornament    g_dropdownArrow{ nullptr, ImVec2(13.0f, 16.0f) };    // the game's option stepper arrow, turned to point down
	Ornament    g_selectArrow{ nullptr, ImVec2(15.0f, 16.0f) };      // NORDIC UI's MCM page arrow, a white mask tinted on draw
	Ornament    g_hoverGlow{ nullptr, ImVec2(0.0f, 0.0f) };          // a white ellipse fading out to its edges (MakeHoverGlow), stretched over a hovered row

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

	// A white ellipse filling the texture, opaque at its centre and fading to nothing at its
	// edges, made here rather than shipped: stretched over a hovered row, it lights the row
	// as SkyUI lights its MCM title bar.
	// Contract: render thread, on the game's D3D11 device; the view lives as long as the
	// process. Stays null (with a warning) when the device refuses it.
	void MakeHoverGlow(ID3D11Device* a_device)
	{
		constexpr UINT  kWidth = 128;  // texels; drawn stretched and filtered, so a small one stays smooth
		constexpr UINT  kHeight = 32;
		constexpr float kFalloff = 3.0f;  // of the Gaussian: how fast it fades from the centre

		const float               edge = std::exp(-kFalloff);  // the Gaussian at the ellipse's edge, taken off so it ends at 0
		std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kWidth) * kHeight * 4);
		for (UINT y = 0; y < kHeight; ++y) {
			for (UINT x = 0; x < kWidth; ++x) {
				const float dx = (static_cast<float>(x) + 0.5f) / kWidth * 2.0f - 1.0f;
				const float dy = (static_cast<float>(y) + 0.5f) / kHeight * 2.0f - 1.0f;
				const float r2 = dx * dx + dy * dy;
				const float alpha = r2 < 1.0f ? (std::exp(-kFalloff * r2) - edge) / (1.0f - edge) : 0.0f;
				auto*       texel = &pixels[(static_cast<std::size_t>(y) * kWidth + x) * 4];
				texel[0] = texel[1] = texel[2] = 255;
				texel[3] = static_cast<std::uint8_t>(std::lround(alpha * 255.0f));
			}
		}
		if (const auto hr = CreateTexture(a_device, kWidth, kHeight, pixels.data(), g_hoverGlow.view.GetAddressOf()); FAILED(hr)) {
			g_hoverGlow.view.Reset();
			logger::warn("Could not make the row hover texture (HRESULT {:#x}); hovered rows get a flat tint instead", static_cast<unsigned>(hr));
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
		set(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 204));  // 0.8 opaque
		set(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
		set(ImGuiCol_PopupBg, IM_COL32(0, 0, 0, 235));
		set(ImGuiCol_ModalWindowDimBg, IM_COL32(0, 0, 0, 51));  // a faint black (0.2) behind a popup
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
		set(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 204));  // 0.8 opaque
		set(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
		set(ImGuiCol_PopupBg, IM_COL32(10, 10, 10, 235));
		set(ImGuiCol_ModalWindowDimBg, IM_COL32(0, 0, 0, 51));  // a faint black (0.2) behind a popup
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

	// How far right of the list's indent a list row's text sits: in Tailor, clear
	// of the amber rule a selected row draws on its left edge.
	float RowTextShift()
	{
		return (g_skin == Theme::Skin::kTailor ? kTailorRuleWidth + kTailorRuleGap : kRowTextShift) * g_scale;
	}

	// How far right of a list's indent a tick row's text starts: in Tailor clear of
	// the amber rule, in Vanilla and Nordic past the equipped mark's column.
	float TickTextShift()
	{
		if (g_skin == Theme::Skin::kTailor) {
			return RowTextShift();
		}
		const Ornament& mark = g_skin == Theme::Skin::kNordic ? g_nordicEquipped : g_vanillaEquipped;
		return mark.size.x * g_scale + ImGui::GetStyle().ItemInnerSpacing.x;
	}

	// One row of a Theme::BeginList list, laid out as an invisible button that
	// runs edge to edge of the list; its fills span only the rules' width
	// (EndList). The text starts `inset` in, on the list's indent, so it lines
	// up with the content around the list.
	// Contract: the current window is a BeginList child (no horizontal padding).
	struct ListRow
	{
		ImVec2 min;  // screen rect of the click area, edge to edge
		ImVec2 max;
		ImVec2 fillMin;  // screen rect of the fills, between the rules' ends
		ImVec2 fillMax;
		float  inset;  // from min.x to where the text starts
		bool   pressed;
		bool   hovered;
	};

	ListRow LayOutListRow(const char* a_label, float a_height)
	{
		const ImVec2 cursor = ImGui::GetCursorScreenPos();
		const float  left = ImGui::GetWindowPos().x;
		const float  right = cursor.x + ImGui::GetContentRegionAvail().x;
		const float  bottom = cursor.y + a_height;
		// The rules' ends, as EndList draws them, kept clear of a scrollbar.
		const float fillLeft = left + g_listReach.x;
		const float fillRight = (std::min)(right, left + ImGui::GetWindowWidth() - g_listReach.y);
		ImGui::SetCursorScreenPos(ImVec2(left, cursor.y));
		ListRow row{ ImVec2(left, cursor.y), ImVec2(right, bottom), ImVec2(fillLeft, cursor.y), ImVec2(fillRight, bottom), cursor.x - left, false, false };
		row.pressed = ImGui::InvisibleButton(a_label, ImVec2((std::max)(1.0f, right - left), a_height));
		row.hovered = ImGui::IsItemHovered();
		return row;
	}

	// The fill of a hovered row from `a_min` to `a_max` (screen pixels): Tailor's muted
	// brown, or in Vanilla and Nordic the hover glow stretched over it, a soft white
	// ellipse brightest at the row's centre (a flat tint without the texture).
	void DrawRowHover(ImDrawList* a_draw, ImVec2 a_min, ImVec2 a_max, float a_rounding)
	{
		constexpr int kGlowAlpha = 64;  // of the glow's centre, out of 255

		if (g_skin == Theme::Skin::kTailor || !g_hoverGlow.view) {
			a_draw->AddRectFilled(a_min, a_max, ImGui::GetColorU32(ImGuiCol_HeaderHovered), a_rounding);
			return;
		}
		a_draw->AddImage(g_hoverGlow.Id(), a_min, a_max, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImGui::GetColorU32(IM_COL32(255, 255, 255, kGlowAlpha)));
	}

	// A tick row's cells, each at its column (Theme::HeaderRow) from `a_textX`,
	// centred on `a_midY`; one too long for its column ends in an ellipsis.
	void DrawCells(ImDrawList* a_draw, float a_textX, float a_midY, float a_right, std::span<const char* const> a_cells,
		std::span<const float> a_columnX)
	{
		const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
		for (std::size_t i = 0; i < a_cells.size(); ++i) {
			const float  left = a_textX + a_columnX[i];
			const float  end = i + 1 < a_cells.size() ? a_textX + a_columnX[i + 1] - Theme::ColumnGap() : a_right;
			const ImVec2 size = ImGui::CalcTextSize(a_cells[i]);
			const ImVec2 topLeft(left, a_midY - size.y * 0.5f);
			if (left + size.x <= end) {
				a_draw->AddText(topLeft, color, a_cells[i]);
			} else if (end > left) {
				ImGui::RenderTextEllipsis(a_draw, topLeft, ImVec2(end, topLeft.y + size.y), end, a_cells[i], nullptr, &size);
			}
		}
	}

	// Tailor 3.0's selected list row: while ticked, the row's fill in the faint
	// amber selection colour with an amber rule down its left edge; the muted
	// brown hover fill while hovered. The text sits clear of the rule.
	bool TailorRow(std::span<const char* const> a_cells, std::span<const float> a_columnX, bool* a_ticked)
	{
		const float   height = ImGui::GetFrameHeight();
		const ListRow row = LayOutListRow(a_cells[0], height);
		if (row.pressed) {
			*a_ticked = !*a_ticked;
		}

		auto* draw = ImGui::GetWindowDrawList();
		if (*a_ticked) {
			draw->AddRectFilled(row.fillMin, row.fillMax, ImGui::GetColorU32(ImGuiCol_Header));
		}
		if (row.hovered) {
			DrawRowHover(draw, row.fillMin, row.fillMax, 0.0f);
		}
		if (*a_ticked) {
			draw->AddRectFilled(row.fillMin, ImVec2(row.fillMin.x + kTailorRuleWidth * g_scale, row.fillMax.y), ImGui::GetColorU32(kAmber));
		}
		const float right = row.fillMax.x - ImGui::GetStyle().FramePadding.x;
		DrawCells(draw, row.min.x + row.inset + TickTextShift(), row.min.y + height * 0.5f, right, a_cells, a_columnX);
		return row.pressed;
	}

	// A SkyUI-style inventory row: while ticked, a white bar fading in from the
	// left edge to the right and the skin's equipped mark at the left; a faint
	// tint while hovered. The mark's column stays on unticked rows so the names
	// line up. Without the texture the mark is a small filled triangle.
	bool InventoryRow(std::span<const char* const> a_cells, std::span<const float> a_columnX, bool* a_ticked)
	{
		constexpr ImU32 kVanillaMark = IM_COL32(204, 204, 204, 255);  // the game's #cccccc
		constexpr int   kBarAlpha = 70;                               // of the bar's white right end

		const auto&   style = ImGui::GetStyle();
		const float   height = ImGui::GetFrameHeight();
		const ListRow row = LayOutListRow(a_cells[0], height);
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
			draw->AddRectFilledMultiColor(row.fillMin, row.fillMax, clear, white, white, clear);
		}
		if (row.hovered) {
			DrawRowHover(draw, row.fillMin, row.fillMax, 0.0f);
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
		DrawCells(draw, topLeft.x + row.inset + TickTextShift(), midY, row.fillMax.x - style.FramePadding.x, a_cells, a_columnX);
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

	// --- Fields ---------------------------------------------------------------

	// The colour a field's frame or rule is drawn in: Vanilla and Nordic draw
	// a normal one fainter than the lists' and window's rules.
	ImVec4 FieldBorderColor(Theme::FieldLook a_look)
	{
		constexpr float kFieldBorderAlpha = 0.5f;  // of the skin's border colour

		switch (a_look) {
		case Theme::FieldLook::kHighlighted:
			return ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
		case Theme::FieldLook::kError:
			return ImGui::ColorConvertU32ToFloat4(kErrorText);
		default:
			break;
		}
		ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Border);
		if (g_skin != Theme::Skin::kTailor) {
			color.w *= kFieldBorderAlpha;
		}
		return color;
	}

	float FieldPadY(Theme::FieldShape a_shape)
	{
		switch (a_shape) {
		case Theme::FieldShape::kUnderlined:
			return kUnderlinedPadY * g_scale;
		case Theme::FieldShape::kBare:
			return 0.0f;
		default:
			return ImGui::GetStyle().FramePadding.y;
		}
	}

	// Room between a field's left edge and its text: a boxed field's frame
	// padding, then the icon and a gap after it.
	// Contract: called with the skin's frame padding in force (not PushFieldStyle's).
	float FieldTextInset(const Theme::FieldStyle& a_style)
	{
		const auto& style = ImGui::GetStyle();
		float       inset = a_style.shape == Theme::FieldShape::kBoxed ? style.FramePadding.x : 0.0f;
		if (a_style.icon != Theme::FieldIcon::kNone) {
			inset += ImGui::GetFontSize() * kFieldIconRatio + style.ItemInnerSpacing.x;
		}
		return inset;
	}

	struct PushedStyle
	{
		int colors = 0;
		int vars = 0;
	};

	// What a field of this style is laid out and drawn with; the border colour
	// is also the one its dropdown popup opens with. A field without a frame
	// has square corners and no fill, except the selected rows' fill while
	// highlighted. Undo with PopFieldStyle.
	PushedStyle PushFieldStyle(const Theme::FieldStyle& a_style)
	{
		const bool  boxed = a_style.shape == Theme::FieldShape::kBoxed;
		PushedStyle pushed;
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(FieldTextInset(a_style), FieldPadY(a_style.shape)));
		ImGui::PushStyleColor(ImGuiCol_Border, FieldBorderColor(boxed ? a_style.look : Theme::FieldLook::kNormal));
		pushed = { 1, 1 };
		if (!boxed) {
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
			const ImVec4 fill = a_style.look == Theme::FieldLook::kHighlighted ? ImGui::GetStyleColorVec4(ImGuiCol_Header) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
			for (const auto col : { ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive }) {
				ImGui::PushStyleColor(col, fill);
			}
			pushed.vars += 2;
			pushed.colors += 3;
		}
		if (a_style.look == Theme::FieldLook::kError) {
			ImGui::PushStyleColor(ImGuiCol_Text, kErrorText);
			++pushed.colors;
		}
		return pushed;
	}

	void PopFieldStyle(PushedStyle a_pushed)
	{
		ImGui::PopStyleColor(a_pushed.colors);
		ImGui::PopStyleVar(a_pushed.vars);
	}

	// An underlined field's rule along the bottom of its rect (screen pixels).
	void DrawFieldRule(const Theme::FieldStyle& a_style, ImVec2 a_min, ImVec2 a_max)
	{
		if (a_style.shape != Theme::FieldShape::kUnderlined) {
			return;
		}
		const ImU32 color = ImGui::GetColorU32(FieldBorderColor(a_style.look));
		ImGui::GetWindowDrawList()->AddLine(ImVec2(a_min.x, a_max.y - 0.5f), ImVec2(a_max.x, a_max.y - 0.5f), color);
	}

	// The field's icon in grey, at the start of its text inset and centred on
	// its rect (screen pixels). The shapes are laid out in fractions of the
	// icon's size.
	// Contract: called with the skin's frame padding in force (not PushFieldStyle's).
	void DrawFieldIcon(const Theme::FieldStyle& a_style, ImVec2 a_min, ImVec2 a_max)
	{
		constexpr float kThicknessRatio = 0.08f;  // of the font size, as the clear button's cross

		if (a_style.icon == Theme::FieldIcon::kNone) {
			return;
		}
		const float size = ImGui::GetFontSize() * kFieldIconRatio;
		const float left = a_min.x + (a_style.shape == Theme::FieldShape::kBoxed ? ImGui::GetStyle().FramePadding.x : 0.0f);
		const float top = (a_min.y + a_max.y - size) * 0.5f;
		const auto  at = [&](float a_x, float a_y) { return ImVec2(left + size * a_x, top + size * a_y); };
		const ImU32 color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		const float thickness = (std::max)(1.0f, ImGui::GetFontSize() * kThicknessRatio);
		auto*       draw = ImGui::GetWindowDrawList();
		if (a_style.icon == Theme::FieldIcon::kSearch) {
			const ImVec2 centre = at(0.42f, 0.42f);
			const float  radius = size * 0.3f;
			const float  rim = radius * 0.7071f;  // the lens's rim towards the handle, at 45 degrees
			draw->AddCircle(centre, radius, color, 0, thickness);
			draw->AddLine(ImVec2(centre.x + rim, centre.y + rim), at(0.9f, 0.9f), color, thickness);
			return;
		}
		const ImVec2 funnel[] = { at(0.1f, 0.15f), at(0.9f, 0.15f), at(0.58f, 0.52f), at(0.58f, 0.85f), at(0.42f, 0.95f), at(0.42f, 0.52f) };
		draw->AddPolyline(funnel, 6, color, ImDrawFlags_Closed, thickness);
	}

	// A combo's preview, drawn for every skin (ImGuiComboFlags_CustomPreview):
	// the field's rule and icon, the text clipped short of the arrow's column
	// (greyed as a hint when the style says so), then the skin's own arrow, if
	// it has one, at the right edge.
	// Contract: called right after the combo's BeginCombo / EndCombo block,
	// where the combo frame is still the last item (BeginComboPreview asserts it).
	void DrawComboPreview(const char* a_preview, const Theme::FieldStyle& a_style)
	{
		const auto&  style = ImGui::GetStyle();
		const ImVec2 frameMin = ImGui::GetItemRectMin();
		const ImVec2 frameMax = ImGui::GetItemRectMax();
		const bool   ownArrow = HasCustomDropdownArrow();
		const ImVec2 arrow = DropdownArrowSize();
		const float  arrowLeft = frameMax.x - (a_style.shape == Theme::FieldShape::kBoxed ? style.FramePadding.x : 0.0f) - arrow.x;
		// Without its own arrow, ImGui's arrow button already lies outside the preview's rect.
		const float textRight = ownArrow ? arrowLeft - style.ItemInnerSpacing.x : frameMax.x;
		DrawFieldRule(a_style, frameMin, frameMax);
		DrawFieldIcon(a_style, frameMin, frameMax);

		const auto pushed = PushFieldStyle(a_style);  // BeginComboPreview places the text by the frame padding
		if (a_style.previewIsHint) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		}
		if (ImGui::BeginComboPreview()) {
			ImGui::PushClipRect(frameMin, ImVec2(textRight, frameMax.y), true);
			ImGui::TextUnformatted(a_preview);
			ImGui::PopClipRect();
			ImGui::EndComboPreview();
		}
		if (a_style.previewIsHint) {
			ImGui::PopStyleColor();
		}
		PopFieldStyle(pushed);

		if (ownArrow) {
			const float midY = (frameMin.y + frameMax.y) * 0.5f;
			DrawDropdownArrow(ImGui::GetWindowDrawList(), ImVec2(arrowLeft, midY - arrow.y * 0.5f), arrow);
		}
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
	// Like ImGui::Checkbox, it reports the label's baseline (frame padding below
	// the top), so text put on the same line lines up with the label.
	bool GameCheckbox(const char* a_label, bool a_ticked, bool a_radio)
	{
		const auto&  style = ImGui::GetStyle();
		const float  height = ImGui::GetFrameHeight();
		const ImVec2 box(g_checkBox.size.x * g_scale, g_checkBox.size.y * g_scale);
		const ImVec2 text = ImGui::CalcTextSize(a_label);
		const ImVec2 topLeft = ImGui::GetCursorScreenPos();

		ImGui::AlignTextToFramePadding();
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

	// The mark left of a selected single-choice row's text: NORDIC UI's MCM
	// page arrow in Vanilla and Nordic (a small triangle without the texture),
	// Tailor's amber rule on the fill's left edge.
	void DrawSelectMark(ImDrawList* a_draw, const ListRow& a_row, float a_textX)
	{
		if (g_skin == Theme::Skin::kTailor) {
			a_draw->AddRectFilled(a_row.fillMin, ImVec2(a_row.fillMin.x + kTailorRuleWidth * g_scale, a_row.fillMax.y), ImGui::GetColorU32(kAmber));
			return;
		}
		// Centred in the indent between the list's left edge and the text.
		const ImU32  tint = ImGui::GetColorU32(g_skin == Theme::Skin::kNordic ? kNordicOrnament : IM_COL32_WHITE);
		const ImVec2 size(g_selectArrow.size.x * g_scale, g_selectArrow.size.y * g_scale);
		const ImVec2 centre((a_row.min.x + a_textX) * 0.5f, (a_row.min.y + a_row.max.y) * 0.5f);
		const ImVec2 topLeft(centre.x - size.x * 0.5f, centre.y - size.y * 0.5f);
		if (g_selectArrow.view) {
			a_draw->AddImage(g_selectArrow.Id(), topLeft, ImVec2(topLeft.x + size.x, topLeft.y + size.y), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
			return;
		}
		const float half = size.y * 0.3f;
		a_draw->AddTriangleFilled(ImVec2(centre.x - half * 0.6f, centre.y - half), ImVec2(centre.x + half * 0.8f, centre.y),
			ImVec2(centre.x - half * 0.6f, centre.y + half), tint);
	}

	constexpr ImU32 WithAlpha(ImU32 a_color, unsigned a_alpha)
	{
		return (a_color & ~IM_COL32_A_MASK) | (a_alpha << IM_COL32_A_SHIFT);
	}

	// One of an outfit row's tags in Tailor: Tailor 3.0's category chip (TailorScreen.cpp
	// Chip), or its rose or steel gender pill (SexTag); `a_pill` with `a_ink` its colour.
	// Returns its size, and draws it at `a_topLeft` when `a_draw` is set.
	ImVec2 TailorTag(ImDrawList* a_draw, ImVec2 a_topLeft, const char* a_text, bool a_pill, ImU32 a_ink)
	{
		constexpr float    kChipPadX = 7.0f;  // pixels at 1080p, from the frame to the text
		constexpr float    kPillPadX = 10.0f;
		constexpr float    kPillMinWidth = 56.0f;  // so Female and Male pills match
		constexpr float    kPadY = 2.0f;
		constexpr float    kChipRounding = 4.0f;
		constexpr unsigned kChipFill = 36;  // alpha of Copper
		constexpr unsigned kPillFill = 31;  // alpha of the gender's ink
		constexpr unsigned kBorder = 64;

		const ImVec2 text = ImGui::CalcTextSize(a_text);
		const float  width = a_pill ? (std::max)(kPillMinWidth * g_scale, text.x + kPillPadX * 2.0f * g_scale) : text.x + kChipPadX * 2.0f * g_scale;
		const ImVec2 size(width, text.y + kPadY * 2.0f * g_scale);
		if (!a_draw) {
			return size;
		}
		const ImVec2 bottomRight(a_topLeft.x + size.x, a_topLeft.y + size.y);
		const float  rounding = a_pill ? size.y * 0.5f : kChipRounding * g_scale;
		const ImU32  frame = a_pill ? a_ink : kCopper;
		a_draw->AddRectFilled(a_topLeft, bottomRight, ImGui::GetColorU32(WithAlpha(frame, a_pill ? kPillFill : kChipFill)), rounding);
		a_draw->AddRect(a_topLeft, bottomRight, ImGui::GetColorU32(WithAlpha(frame, kBorder)), rounding);
		a_draw->AddText(ImVec2(a_topLeft.x + (size.x - text.x) * 0.5f, a_topLeft.y + kPadY * g_scale), ImGui::GetColorU32(a_ink), a_text);
		return size;
	}

	// An outfit row's tags left to right from `a_topLeft`, wrapping onto a new line where the
	// next one would pass `a_width`: its gender unless unisex, then `a_tags`. Tailor draws
	// them as framed tags (TailorTag), Vanilla and Nordic as grey text, comma separated.
	// Draws them when `a_draw` is set. Returns their height; 0 for no tag.
	// Contract: in the small font (Theme::PushSmallFont).
	float LayOutTags(ImDrawList* a_draw, ImVec2 a_topLeft, float a_width, int a_gender, std::span<const std::string> a_tags)
	{
		constexpr float kChipGap = 6.0f;  // pixels at 1080p, between Tailor's tags and their lines

		const char*       gender = a_gender == 1 ? "Female" : (a_gender == 0 ? "Male" : nullptr);
		const std::size_t count = a_tags.size() + (gender ? 1 : 0);
		if (count == 0) {
			return 0.0f;
		}
		const bool  tailor = g_skin == Theme::Skin::kTailor;
		const float gapX = tailor ? kChipGap * g_scale : ImGui::CalcTextSize(" ").x;
		const float gapY = tailor ? kChipGap * g_scale : 0.0f;
		const float commaWidth = ImGui::CalcTextSize(",").x;
		const ImU32 grey = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		ImVec2      at = a_topLeft;
		float       lineHeight = 0.0f;
		for (std::size_t i = 0; i < count; ++i) {
			const bool  isGender = gender && i == 0;
			const char* text = isGender ? gender : a_tags[i - (gender ? 1 : 0)].c_str();
			const bool  last = i + 1 == count;
			ImVec2      size = ImGui::CalcTextSize(text);
			if (tailor) {
				size = TailorTag(nullptr, at, text, isGender, 0);
			} else if (!last) {
				size.x += commaWidth;
			}
			if (at.x > a_topLeft.x && at.x + size.x > a_topLeft.x + a_width) {
				at = ImVec2(a_topLeft.x, at.y + lineHeight + gapY);
			}
			if (a_draw && tailor) {
				TailorTag(a_draw, at, text, isGender, isGender ? (a_gender == 1 ? kRose : kSteel) : kCopperLight);
			} else if (a_draw) {
				a_draw->AddText(at, grey, text);
				if (!last) {
					a_draw->AddText(ImVec2(at.x + size.x - commaWidth, at.y), grey, ",");
				}
			}
			lineHeight = size.y;
			at.x += size.x + gapX;
		}
		return at.y + lineHeight - a_topLeft.y;
	}

	// The square a tab column icon is drawn in; its points are given as fractions of the side.
	struct IconFrame
	{
		ImVec2 min;
		float  side;

		ImVec2 At(float a_x, float a_y) const { return ImVec2(min.x + a_x * side, min.y + a_y * side); }
	};

	// A backpack: its body with the flap's edge across it and a front pocket, a handle on top.
	void DrawBackpackIcon(ImDrawList* a_draw, const IconFrame& a_frame, ImU32 a_color, float a_thick)
	{
		const float s = a_frame.side;
		a_draw->AddRect(a_frame.At(0.22f, 0.30f), a_frame.At(0.78f, 0.88f), a_color, 0.10f * s, ImDrawFlags_None, a_thick);
		a_draw->PathArcTo(a_frame.At(0.50f, 0.30f), 0.11f * s, IM_PI, 2.0f * IM_PI);
		a_draw->PathStroke(a_color, ImDrawFlags_None, a_thick);
		a_draw->AddLine(a_frame.At(0.22f, 0.48f), a_frame.At(0.78f, 0.48f), a_color, a_thick);
		a_draw->AddRect(a_frame.At(0.34f, 0.62f), a_frame.At(0.66f, 0.79f), a_color, 0.04f * s, ImDrawFlags_None, a_thick);
	}

	// A dress form: a padded torso with a neck, on a pole with two feet.
	void DrawDressFormIcon(ImDrawList* a_draw, const IconFrame& a_frame, ImU32 a_color, float a_thick)
	{
		const std::array<ImVec2, 16> torso{ a_frame.At(0.44f, 0.12f), a_frame.At(0.56f, 0.12f), a_frame.At(0.57f, 0.18f),
			a_frame.At(0.72f, 0.23f), a_frame.At(0.74f, 0.32f), a_frame.At(0.66f, 0.42f), a_frame.At(0.62f, 0.52f), a_frame.At(0.68f, 0.62f),
			a_frame.At(0.66f, 0.68f), a_frame.At(0.34f, 0.68f), a_frame.At(0.32f, 0.62f), a_frame.At(0.38f, 0.52f), a_frame.At(0.34f, 0.42f),
			a_frame.At(0.26f, 0.32f), a_frame.At(0.28f, 0.23f), a_frame.At(0.43f, 0.18f) };
		a_draw->AddPolyline(torso.data(), static_cast<int>(torso.size()), a_color, ImDrawFlags_Closed, a_thick);
		a_draw->AddLine(a_frame.At(0.50f, 0.68f), a_frame.At(0.50f, 0.86f), a_color, a_thick);
		a_draw->AddLine(a_frame.At(0.50f, 0.86f), a_frame.At(0.34f, 0.93f), a_color, a_thick);
		a_draw->AddLine(a_frame.At(0.50f, 0.86f), a_frame.At(0.66f, 0.93f), a_color, a_thick);
	}

	// The rule beside the tab column: Tailor's 1px border (its sidebar divider), Vanilla's
	// 2px rule, Nordic's rope, or 1px without its texture (pixels on the back buffer).
	float TabRuleWidth()
	{
		const float pixel = (std::max)(1.0f, std::floor(g_scale));
		if (g_skin == Theme::Skin::kNordic && g_tabRope.view) {
			return g_tabRope.size.x * g_scale;
		}
		return g_skin == Theme::Skin::kVanilla ? pixel * 2.0f : pixel;
	}

	// --- Labels ---------------------------------------------------------------

	// Item spacing for a field label's line: a little tighter than the style's,
	// so the label sits closer to the control below it. Pop with PopStyleVar.
	void PushLabelSpacing()
	{
		constexpr float kLabelGapTrim = 3.0f;  // pixels at 1080p

		const auto& spacing = ImGui::GetStyle().ItemSpacing;
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing.x, (std::max)(0.0f, spacing.y - kLabelGapTrim * g_scale)));
	}

	// LabelText's colour, or the lit one while hovered: amber in Tailor, whose label
	// is already the text colour.
	ImU32 LinkColor(bool a_hovered)
	{
		if (g_skin == Theme::Skin::kTailor) {
			return a_hovered ? ImGui::GetColorU32(kAmber) : ImGui::GetColorU32(ImGuiCol_Text);
		}
		return a_hovered ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(kNordicLabel);
	}

	// A filled square `a_side` pixels across, its corners rounded, with a plus
	// cut out of its middle so the window shows through. ImGui cannot cut a
	// hole, so four rotated copies of one piece are filled: a corner square and
	// the strip beyond one plus tip. They never overlap, so a translucent colour
	// stays even, and they meet on whole pixels, where ImGui's anti-aliasing
	// fringe (half a pixel each way) leaves no seam.
	void DrawPlusSquare(ImDrawList* a_draw, ImVec2 a_topLeft, float a_side, ImU32 a_color)
	{
		constexpr float kRounding = 4.0f;         // pixels at 1080p
		constexpr float kArmRatio = 0.12f;        // the plus's arm thickness, of the side
		constexpr float kTipMarginRatio = 0.22f;  // from the square's edge to the plus's tips, of the side
		constexpr int   kArcSegments = 6;

		const float s = std::floor(a_side);
		float       arm = (std::max)(2.0f, std::round(s * kArmRatio));
		if (std::fmod(s - arm, 2.0f) != 0.0f) {
			arm += 1.0f;  // whole-pixel arm edges, so the plus stays sharp
		}
		const float armLo = (s - arm) * 0.5f;  // the arms' edges
		const float armHi = (s + arm) * 0.5f;
		const float tip = std::round(s * kTipMarginRatio);
		const float r = (std::min)(kRounding * g_scale, armLo - 1.0f);  // short of the corner square's side, so no point repeats

		// The top-left piece, clockwise (ImGui's fill and fringe need it): the
		// rounded corner, along the top past the upper arm's tip strip, then
		// back left under the corner square.
		std::array<ImVec2, kArcSegments + 1 + 5> piece{};
		std::size_t                              n = 0;
		for (int i = 0; i <= kArcSegments; ++i) {
			const float a = IM_PI + IM_PI * 0.5f * static_cast<float>(i) / kArcSegments;
			piece[n++] = ImVec2(r + std::cos(a) * r, r + std::sin(a) * r);
		}
		for (const ImVec2 p : { ImVec2(armHi, 0.0f), ImVec2(armHi, tip), ImVec2(armLo, tip), ImVec2(armLo, armLo), ImVec2(0.0f, armLo) }) {
			piece[n++] = p;
		}

		const ImVec2 origin(std::floor(a_topLeft.x), std::floor(a_topLeft.y));
		for (int turn = 0; turn < 4; ++turn) {
			for (auto p : piece) {
				for (int k = 0; k < turn; ++k) {
					p = ImVec2(s - p.y, p.x);  // a quarter turn clockwise about the centre
				}
				a_draw->PathLineTo(ImVec2(origin.x + p.x, origin.y + p.y));
			}
			a_draw->PathFillConcave(a_color);
		}
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
			LoadOrnament(a_device, wic.Get(), folder, L"nordic-rope.png", g_tabRope);
			LoadOrnament(a_device, wic.Get(), folder, L"vanilla-equipped.png", g_vanillaEquipped);
			LoadOrnament(a_device, wic.Get(), folder, L"nordic-equipped.png", g_nordicEquipped);
			LoadOrnament(a_device, wic.Get(), folder, L"vanilla-checkbox.png", g_checkBox);
			LoadOrnament(a_device, wic.Get(), folder, L"vanilla-checkmark.png", g_checkMark);
			LoadOrnament(a_device, wic.Get(), folder, L"vanilla-dropdown.png", g_dropdownArrow);
			LoadOrnament(a_device, wic.Get(), folder, L"nordic-select-arrow.png", g_selectArrow);
		}
		MakeHoverGlow(a_device);
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
		SectionGap();
		// The gap below an item comes from the item spacing in force while it is laid out.
		const bool grey = g_skin != Skin::kTailor;
		PushLabelSpacing();
		if (grey) {
			ImGui::PushStyleColor(ImGuiCol_Text, kNordicLabel);  // NORDIC UI's label grey, in Vanilla too
		}
		PushSmallFont();
		ImGui::TextUnformatted(a_text);
		PopSmallFont();
		if (grey) {
			ImGui::PopStyleColor();
		}
		ImGui::PopStyleVar();
	}

	bool LabelLink(const char* a_label)
	{
		PushSmallFont();
		const auto&  style = ImGui::GetStyle();
		const float  side = ImGui::GetFontSize();
		const ImVec2 text = ImGui::CalcTextSize(a_label);
		const float  width = side + style.ItemInnerSpacing.x + text.x;

		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, ImGui::GetContentRegionAvail().x - width));
		const ImVec2 topLeft = ImGui::GetCursorScreenPos();
		PushLabelSpacing();  // the line's gap below stays LabelText's
		const bool clicked = ImGui::InvisibleButton(a_label, ImVec2(width, (std::max)(side, text.y)));
		ImGui::PopStyleVar();

		const ImU32 color = LinkColor(ImGui::IsItemHovered());
		auto*       draw = ImGui::GetWindowDrawList();
		DrawPlusSquare(draw, topLeft, side, color);
		draw->AddText(ImVec2(topLeft.x + side + style.ItemInnerSpacing.x, topLeft.y), color, a_label);
		PopSmallFont();
		return clicked;
	}

	bool LabelHelpMark()
	{
		PushSmallFont();
		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
		PushLabelSpacing();  // the line's gap below stays LabelText's
		ImGui::TextDisabled("(?)");
		const bool hovered = ImGui::IsItemHovered();
		ImGui::PopStyleVar();
		PopSmallFont();
		return hovered;
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

	float ColumnsWidth()
	{
		return (std::max)(0.0f, ImGui::GetContentRegionAvail().x - TickTextShift() - ImGui::GetStyle().FramePadding.x);
	}

	float ColumnGap()
	{
		constexpr float kColumnGap = 16.0f;  // pixels at 1080p
		return kColumnGap * g_scale;
	}

	int HeaderRow(std::span<const char* const> a_labels, std::span<const float> a_columnX, int a_sorted, bool a_descending)
	{
		constexpr float kArrowRatio = 0.4f;  // the sort arrow's width, of the font size

		SectionGap();
		PushLabelSpacing();  // the gap below is LabelText's
		PushSmallFont();
		const auto&  style = ImGui::GetStyle();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float  textX = origin.x + TickTextShift();
		const float  arrow = ImGui::GetFontSize() * kArrowRatio;
		auto*        draw = ImGui::GetWindowDrawList();
		int          clicked = -1;
		for (std::size_t i = 0; i < a_labels.size(); ++i) {
			const bool   sorted = static_cast<int>(i) == a_sorted;
			const ImVec2 text = ImGui::CalcTextSize(a_labels[i]);
			const float  width = text.x + (sorted ? style.ItemInnerSpacing.x + arrow : 0.0f);
			if (i > 0) {
				ImGui::SameLine();
			}
			ImGui::SetCursorScreenPos(ImVec2(textX + a_columnX[i], origin.y));
			if (ImGui::InvisibleButton(a_labels[i], ImVec2((std::max)(1.0f, width), (std::max)(1.0f, text.y)))) {
				clicked = static_cast<int>(i);
			}
			const ImU32  color = LinkColor(ImGui::IsItemHovered());
			const ImVec2 pos = ImGui::GetItemRectMin();
			draw->AddText(pos, color, a_labels[i]);
			if (sorted) {
				const float left = pos.x + text.x + style.ItemInnerSpacing.x;
				const float midY = pos.y + text.y * 0.5f;
				const float half = arrow * 0.5f;
				const float tip = a_descending ? half * 0.6f : -half * 0.6f;  // the point's offset from midY
				draw->AddTriangleFilled(ImVec2(left, midY - tip), ImVec2(left + arrow, midY - tip), ImVec2(left + half, midY + tip), color);
			}
		}
		PopSmallFont();
		ImGui::PopStyleVar();
		return clicked;
	}

	bool TickRow(std::span<const char* const> a_cells, std::span<const float> a_columnX, bool* a_ticked)
	{
		return g_skin == Skin::kTailor ? TailorRow(a_cells, a_columnX, a_ticked) : InventoryRow(a_cells, a_columnX, a_ticked);
	}

	bool TextTab(const char* a_label, bool a_selected)
	{
		constexpr float kUnselectedAlpha = 0.5f;  // "half transparent" (docs/ui-redesign.md §4)

		const ImVec2 text = ImGui::CalcTextSize(a_label);
		const ImVec2 topLeft = ImGui::GetCursorScreenPos();
		const bool   clicked = ImGui::InvisibleButton(a_label, ImVec2((std::max)(1.0f, text.x), (std::max)(1.0f, text.y)));
		const float  alpha = a_selected || ImGui::IsItemHovered() ? 1.0f : kUnselectedAlpha;
		ImGui::GetWindowDrawList()->AddText(topLeft, ImGui::GetColorU32(ImGuiCol_Text, alpha), a_label);
		return clicked;
	}

	// Unselected, Tailor's icon is ghosted: its LabelText is the text colour, which the
	// selected tab has.
	bool Tab(TabIcon a_icon, const char* a_name, bool a_selected)
	{
		constexpr float kIconInset = 0.12f;  // of the side, between the fill's edge and the icon
		constexpr float kLineWidth = 1.5f;   // pixels at 1080p

		const float  side = kTabSide * g_scale;
		const ImVec2 topLeft = ImGui::GetCursorScreenPos();
		const bool   clicked = ImGui::InvisibleButton(a_name, ImVec2(side, side));
		const bool   hovered = ImGui::IsItemHovered();
		auto*        draw = ImGui::GetWindowDrawList();
		ImU32        color = LinkColor(hovered);
		if (a_selected) {
			draw->AddRectFilled(topLeft, ImVec2(topLeft.x + side, topLeft.y + side), ImGui::GetColorU32(ImGuiCol_Header), ImGui::GetStyle().FrameRounding);
			color = ImGui::GetColorU32(ImGuiCol_Text);
		} else if (!hovered && g_skin == Skin::kTailor) {
			color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		}
		const IconFrame frame{ ImVec2(topLeft.x + side * kIconInset, topLeft.y + side * kIconInset), side * (1.0f - kIconInset * 2.0f) };
		const float     thick = (std::max)(1.0f, kLineWidth * g_scale);
		switch (a_icon) {
		case TabIcon::kInventory:
			DrawBackpackIcon(draw, frame, color, thick);
			break;
		case TabIcon::kOutfits:
			DrawDressFormIcon(draw, frame, color, thick);
			break;
		}
		if (hovered) {
			ImGui::SetTooltip("%s", a_name);
		}
		return clicked;
	}

	float TabColumnWidth()
	{
		return (kTabSide + kTabRuleGap) * g_scale + TabRuleWidth() + ImGui::GetStyle().WindowPadding.x;
	}

	// The tab's centre goes halfway between the window's left edge (the window's padding left
	// of the content) and the rule's middle (TabRule).
	float TabOffsetX()
	{
		const float ruleMiddle = (kTabSide + kTabRuleGap) * g_scale + TabRuleWidth() * 0.5f;
		return (ruleMiddle - ImGui::GetStyle().WindowPadding.x) * 0.5f - kTabSide * g_scale * 0.5f;
	}

	float TabTopGap()
	{
		constexpr float kNordicTopGap = 8.0f;  // pixels at 1080p, below the corner knot

		return g_skin == Skin::kNordic ? kNordicTopGap * g_scale : 0.0f;
	}

	// The rope is tiled from the top, its last repeat cut short at the bottom. The clip rect
	// is the rule's own, so it reaches the window's edges across its padding.
	void TabRule(float a_left, float a_top, float a_bottom)
	{
		const float  x = a_left + (kTabSide + kTabRuleGap) * g_scale;
		const float  width = TabRuleWidth();
		const ImVec2 topLeft(x, a_top);
		const ImVec2 bottomRight(x + width, a_bottom);
		auto*        draw = ImGui::GetWindowDrawList();
		draw->PushClipRect(topLeft, bottomRight, false);
		if (g_skin == Skin::kNordic && g_tabRope.view) {
			const float tile = g_tabRope.size.y * g_scale;
			const ImU32 tint = ImGui::GetColorU32(kNordicOrnament);
			for (float y = a_top; y < a_bottom; y += tile) {
				const float end = (std::min)(y + tile, a_bottom);
				draw->AddImage(g_tabRope.Id(), ImVec2(x, y), ImVec2(x + width, end), ImVec2(0.0f, 0.0f), ImVec2(1.0f, (end - y) / tile), tint);
			}
		} else {
			draw->AddRectFilled(topLeft, bottomRight, ImGui::GetColorU32(ImGuiCol_Border));
		}
		draw->PopClipRect();
	}

	bool TextButton(const char* a_label)
	{
		const ImVec2 text = ImGui::CalcTextSize(a_label);
		const ImVec2 topLeft = ImGui::GetCursorScreenPos();
		const bool   clicked = ImGui::InvisibleButton(a_label, ImVec2((std::max)(1.0f, text.x), (std::max)(1.0f, text.y)));
		ImGui::GetWindowDrawList()->AddText(topLeft, LinkColor(ImGui::IsItemHovered()), a_label);
		return clicked;
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

	float FieldHeight(const FieldStyle& a_style)
	{
		return ImGui::GetFontSize() + FieldPadY(a_style.shape) * 2.0f;
	}

	bool InputText(const char* a_id, char* a_buffer, std::size_t a_size, int a_flags, const char* a_hint, const FieldStyle& a_style)
	{
		const auto pushed = PushFieldStyle(a_style);
		const bool result = a_hint ? ImGui::InputTextWithHint(a_id, a_hint, a_buffer, a_size, a_flags) :
		                             ImGui::InputText(a_id, a_buffer, a_size, a_flags);
		PopFieldStyle(pushed);
		DrawFieldRule(a_style, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		DrawFieldIcon(a_style, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		return result;
	}

	unsigned ErrorColor()
	{
		return kErrorText;
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

	bool BeginCombo(const char* a_id, int a_flags, const FieldStyle& a_style)
	{
		// The popup takes the window padding and border colour in force as BeginCombo opens it.
		const ImVec2 padding = g_skin == Skin::kTailor ? kTailorDropdownPadding : kSquareDropdownPadding;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding.x * g_scale, padding.y * g_scale));
		const auto pushed = PushFieldStyle(a_style);
		// ImGui requires an empty preview with CustomPreview; EndCombo draws it.
		const int  flags = a_flags | ImGuiComboFlags_CustomPreview | (HasCustomDropdownArrow() ? ImGuiComboFlags_NoArrowButton : 0);
		const bool open = ImGui::BeginCombo(a_id, "", flags);
		PopFieldStyle(pushed);
		ImGui::PopStyleVar();
		return open;
	}

	void EndCombo(bool a_open, const char* a_preview, const FieldStyle& a_style)
	{
		if (a_open) {
			ImGui::EndCombo();
		}
		DrawComboPreview(a_preview, a_style);
	}

	float ComboWidth(const char* a_preview, const FieldStyle& a_style)
	{
		const auto& style = ImGui::GetStyle();
		const float text = FieldTextInset(a_style) + ImGui::CalcTextSize(a_preview).x;
		if (!HasCustomDropdownArrow()) {
			return text + FieldHeight(a_style);  // ImGui's square arrow button
		}
		const float right = a_style.shape == FieldShape::kBoxed ? style.FramePadding.x : 0.0f;
		return text + style.ItemInnerSpacing.x + DropdownArrowSize().x + right;
	}

	bool ClearButton(const char* a_id, float a_side)
	{
		constexpr float kCrossRatio = 0.22f;      // half the cross's width, of the font size
		constexpr float kThicknessRatio = 0.08f;  // of the font size, as the category rows' check mark

		const ImVec2 topLeft = ImGui::GetCursorScreenPos();
		const bool   clicked = ImGui::InvisibleButton(a_id, ImVec2(a_side, a_side));
		const ImU32  color = ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled);
		const float  half = ImGui::GetFontSize() * kCrossRatio;
		const float  thickness = (std::max)(1.0f, ImGui::GetFontSize() * kThicknessRatio);
		const ImVec2 centre(topLeft.x + a_side * 0.5f, topLeft.y + a_side * 0.5f);
		auto*        draw = ImGui::GetWindowDrawList();
		draw->AddLine(ImVec2(centre.x - half, centre.y - half), ImVec2(centre.x + half, centre.y + half), color, thickness);
		draw->AddLine(ImVec2(centre.x - half, centre.y + half), ImVec2(centre.x + half, centre.y - half), color, thickness);
		return clicked;
	}

	void PushSmallFont()
	{
		constexpr float kSmallFontRatio = 0.85f;  // of the body font's size

		ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kSmallFontRatio);
	}

	void PopSmallFont()
	{
		ImGui::PopFont();
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
		// Vanilla and Nordic: past the content across the window's padding to its
		// edges; in a table, only on the table's outer sides, so the gap between
		// columns stays clear. Tailor: within the content, as its fields are.
		const float padding = g_skin == Skin::kTailor ? 0.0f : ImGui::GetStyle().WindowPadding.x;
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
		// Square corners: the rules and the first and last rows' fills are square.
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, ListPaddingY()));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
		const bool visible = ImGui::BeginChild(a_id, ImVec2(width, a_height), ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::PopStyleVar(2);
		if (visible && left > 0.0f) {
			ImGui::Indent(left);  // the rows' text lines up with the content around the list (0 would mean the default indent)
		}
		return visible;
	}

	void EndList()
	{
		ImGui::EndChild();
		// The rules span the content only, as the rows' fills do (LayOutListRow).
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

	bool SelectRow(const char* a_label, bool a_selected)
	{
		const ListRow row = LayOutListRow(a_label, SelectRowHeight());
		const float   textX = row.min.x + row.inset + RowTextShift();
		auto*         draw = ImGui::GetWindowDrawList();
		if (a_selected) {
			draw->AddRectFilled(row.fillMin, row.fillMax, ImGui::GetColorU32(ImGuiCol_Header));
			DrawSelectMark(draw, row, textX);
		} else if (row.hovered) {
			DrawRowHover(draw, row.fillMin, row.fillMax, 0.0f);
		}
		draw->AddText(ImVec2(textX, row.min.y + kSelectRowPadY * g_scale), ImGui::GetColorU32(ImGuiCol_Text), a_label);
		return row.pressed;
	}

	void TextRow(const char* a_label)
	{
		const ImVec2 cursor = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, SelectRowHeight()));
		const ImVec2 text(cursor.x + RowTextShift(), cursor.y + kSelectRowPadY * g_scale);
		ImGui::GetWindowDrawList()->AddText(text, ImGui::GetColorU32(ImGuiCol_Text), a_label);
	}

	void ListHeading(const char* a_text)
	{
		PushSmallFont();
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		TextRow(a_text);
		ImGui::PopStyleColor();
		PopSmallFont();
	}

	// The chevron sits where ListHeading's text would start, the text after it; the rule ends
	// where the rows' fills end.
	bool FoldHeading(const char* a_text, bool a_open)
	{
		constexpr float kChevronRatio = 0.5f;  // the chevron's box, of the font size
		constexpr float kRulePad = 4.0f;       // pixels at 1080p, from the text to the rule and from the rule to the list's right end

		PushSmallFont();
		const ListRow row = LayOutListRow(a_text, SelectRowHeight());
		const float   side = ImGui::GetFontSize() * kChevronRatio;
		const float   midY = (row.min.y + row.max.y) * 0.5f;
		const float   left = row.min.x + row.inset + RowTextShift();
		const ImU32   color = row.hovered ? LinkColor(true) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
		const float   thick = (std::max)(1.0f, 1.5f * g_scale);
		auto*         draw = ImGui::GetWindowDrawList();
		if (a_open) {
			const ImVec2 points[] = { { left, midY - side * 0.25f }, { left + side * 0.5f, midY + side * 0.25f }, { left + side, midY - side * 0.25f } };
			draw->AddPolyline(points, 3, color, ImDrawFlags_None, thick);
		} else {
			const ImVec2 points[] = { { left + side * 0.25f, midY - side * 0.5f }, { left + side * 0.75f, midY }, { left + side * 0.25f, midY + side * 0.5f } };
			draw->AddPolyline(points, 3, color, ImDrawFlags_None, thick);
		}
		const float textX = left + side + ImGui::GetStyle().ItemInnerSpacing.x;
		draw->AddText(ImVec2(textX, row.min.y + kSelectRowPadY * g_scale), color, a_text);
		const float ruleLeft = textX + ImGui::CalcTextSize(a_text).x + kRulePad * g_scale;
		const float ruleRight = row.fillMax.x - kRulePad * g_scale;
		if (ruleRight > ruleLeft) {
			const float y = std::floor(midY) + 0.5f;  // on a pixel's centre, so a 1px line stays sharp
			draw->AddLine(ImVec2(ruleLeft, y), ImVec2(ruleRight, y), ImGui::GetColorU32(ImGuiCol_Border));
		}
		PopSmallFont();
		return row.pressed;
	}

	void HoverFill(float a_left, float a_top, float a_right, float a_bottom)
	{
		DrawRowHover(ImGui::GetWindowDrawList(), ImVec2(a_left, a_top), ImVec2(a_right, a_bottom), ImGui::GetStyle().FrameRounding);
	}

	float ListHeadingHeight()
	{
		PushSmallFont();
		const float height = SelectRowHeight();
		PopSmallFont();
		return height;
	}

	float OutfitTagsWidth()
	{
		return (std::max)(0.0f, ImGui::GetContentRegionAvail().x - RowTextShift() - ImGui::GetStyle().FramePadding.x);
	}

	float OutfitRowHeight(int a_gender, std::span<const std::string> a_tags, float a_tagsWidth)
	{
		PushSmallFont();
		const float tags = LayOutTags(nullptr, ImVec2(0.0f, 0.0f), a_tagsWidth, a_gender, a_tags);
		PopSmallFont();
		return SelectRowHeight() + (tags > 0.0f ? kOutfitTagsGap * g_scale + tags : 0.0f);
	}

	// SelectRow with the tags under the name, inside the row's bottom padding. The selected
	// row's Vanilla and Nordic arrow stays level with the name.
	bool OutfitRow(const char* a_name, int a_gender, std::span<const std::string> a_tags, float a_tagsWidth, bool a_selected)
	{
		const ListRow row = LayOutListRow(a_name, OutfitRowHeight(a_gender, a_tags, a_tagsWidth));
		const float   textX = row.min.x + row.inset + RowTextShift();
		const float   textY = row.min.y + kSelectRowPadY * g_scale;
		auto*         draw = ImGui::GetWindowDrawList();
		if (a_selected) {
			draw->AddRectFilled(row.fillMin, row.fillMax, ImGui::GetColorU32(ImGuiCol_Header));
			ListRow nameLine = row;
			nameLine.max.y = row.min.y + SelectRowHeight();
			DrawSelectMark(draw, nameLine, textX);
		} else if (row.hovered) {
			DrawRowHover(draw, row.fillMin, row.fillMax, 0.0f);
		}
		draw->AddText(ImVec2(textX, textY), ImGui::GetColorU32(ImGuiCol_Text), a_name);
		const float tagsY = textY + ImGui::GetTextLineHeight() + kOutfitTagsGap * g_scale;
		PushSmallFont();
		LayOutTags(draw, ImVec2(textX, tagsY), a_tagsWidth, a_gender, a_tags);
		PopSmallFont();
		return row.pressed;
	}
}
