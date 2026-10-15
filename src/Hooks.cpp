// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Hook targets     - Address Library IDs for D3D init, DXGI Present, input dispatch
//   ImGui lifecycle  - OnD3DInit creates the context on the game's device/window
//   Frame            - OnPresent draws CaptureWindow once per frame while it is open
//   Input            - InputDispatchHook routes events to the window and hides them
//                      from the game while the window is open
//   Install          - writes the three trampoline calls (SE/AE only)
//
// The IDs and offsets match two independent, widely used ImGui mods
// (Wheeler, SKSE Menu Framework); see docs in CLAUDE.md §2.2 before changing.
#include "PCH.h"

#include "Hooks.h"

#include "CaptureWindow.h"
#include "Settings.h"
#include "Theme.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <atomic>
#include <cstdint>

namespace
{
	std::atomic_bool g_renderReady = false;

	// --- Hook targets ---------------------------------------------------------

	struct D3DInitHook
	{
		static constexpr auto id = REL::RelocationID(75595, 77226);
		static constexpr auto offset = REL::VariantOffset(0x9, 0x275, 0x0);

		static void                                    thunk();
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct DXGIPresentHook
	{
		static constexpr auto id = REL::RelocationID(75461, 77246);
		static constexpr auto offset = REL::VariantOffset(0x9, 0x9, 0x0);

		static void                                    thunk(std::uint32_t a_timer);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct InputDispatchHook
	{
		static constexpr auto id = REL::RelocationID(67315, 68617);
		static constexpr auto offset = REL::VariantOffset(0x7B, 0x7B, 0x0);

		static void                                    thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events);
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct WndProcHook
	{
		static LRESULT        thunk(HWND a_hwnd, UINT a_msg, WPARAM a_wParam, LPARAM a_lParam);
		static inline WNDPROC func = nullptr;
	};

	// --- ImGui lifecycle ------------------------------------------------------

	// Contract: runs on the render thread, once, right after the game created
	// its D3D11 device and swap chain. Everything ImGui needs lives in the
	// renderer singleton at that point.
	void OnD3DInit()
	{
		auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
		if (!renderer) {
			logger::error("ImGui: renderer singleton missing");
			return;
		}

		auto& data = renderer->GetRuntimeData();
		auto* swapChain = reinterpret_cast<IDXGISwapChain*>(data.renderWindows[0].swapChain);
		auto* device = reinterpret_cast<ID3D11Device*>(data.forwarder);
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(data.context);
		if (!swapChain || !device || !context) {
			logger::error("ImGui: swap chain, device or context missing");
			return;
		}

		DXGI_SWAP_CHAIN_DESC desc{};
		if (FAILED(swapChain->GetDesc(&desc))) {
			logger::error("ImGui: GetDesc failed");
			return;
		}

		ImGui::CreateContext();
		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;  // never write imgui.ini into the game folder
		io.LogFilename = nullptr;
		io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
		io.MouseDrawCursor = true;  // the game hides the OS cursor; draw our own

		Theme::LoadFonts();  // style and scale are set per frame by Theme::Apply

		if (!ImGui_ImplWin32_Init(desc.OutputWindow) || !ImGui_ImplDX11_Init(device, context)) {
			logger::error("ImGui: backend init failed");
			return;
		}

		WndProcHook::func = reinterpret_cast<WNDPROC>(
			SetWindowLongPtrA(desc.OutputWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProcHook::thunk)));

		g_renderReady = true;
		logger::info("ImGui initialised ({}x{})", desc.BufferDesc.Width, desc.BufferDesc.Height);
	}

	// --- Frame ----------------------------------------------------------------

	// Contract: render thread, just before the game presents. Nothing is drawn
	// (and no ImGui frame is started) while the window is closed, so the cost
	// in normal play is one atomic load.
	void OnPresent()
	{
		if (!g_renderReady || !CaptureWindow::IsOpen()) {
			return;
		}

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();  // sets io.DisplaySize
		Theme::Apply(Settings::Skin(), ImGui::GetIO().DisplaySize.y);
		ImGui::NewFrame();

		CaptureWindow::Draw();

		ImGui::Render();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	}

	void D3DInitHook::thunk()
	{
		func();
		OnD3DInit();
	}

	void DXGIPresentHook::thunk(std::uint32_t a_timer)
	{
		func(a_timer);
		OnPresent();
	}

	// --- Input ----------------------------------------------------------------

	// Contract: game thread. While the window is open the game sees an empty
	// event list, so no key or mouse button reaches player controls or menus.
	void InputDispatchHook::thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events)
	{
		if (a_events && CaptureWindow::IsOpen()) {
			CaptureWindow::QueueInput(a_events);
			RE::InputEvent* const none[] = { nullptr };
			func(a_dispatcher, none);
			return;
		}
		func(a_dispatcher, a_events);
	}

	LRESULT WndProcHook::thunk(HWND a_hwnd, UINT a_msg, WPARAM a_wParam, LPARAM a_lParam)
	{
		if (a_msg == WM_KILLFOCUS && g_renderReady) {
			ImGui::GetIO().ClearInputKeys();
		}
		return func(a_hwnd, a_msg, a_wParam, a_lParam);
	}

	// --- Install --------------------------------------------------------------

	template <class T>
	void WriteCall(SKSE::Trampoline& a_trampoline)
	{
		const REL::Relocation<std::uintptr_t> target{ T::id, T::offset };
		T::func = a_trampoline.write_call<5>(target.address(), T::thunk);
	}
}

namespace Hooks
{
	void Install()
	{
		if (REL::Module::IsVR()) {
			logger::info("Skyrim VR: overlay disabled, automatic outfit names only");
			return;
		}

		auto& trampoline = SKSE::GetTrampoline();
		WriteCall<D3DInitHook>(trampoline);
		WriteCall<DXGIPresentHook>(trampoline);
		WriteCall<InputDispatchHook>(trampoline);
		logger::info("Hooks installed");
	}

	bool IsRenderReady()
	{
		return g_renderReady;
	}
}
