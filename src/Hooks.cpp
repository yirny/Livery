// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Hook targets     - Address Library IDs for D3D init and input dispatch
//   ImGui lifecycle  - OnD3DInit creates the context on the game's device/window
//   Frame            - DrawFrame draws CaptureWindow once per frame while it is open
//   Menu             - CaptureMenu: a movie-less game menu; the game shows its own
//                      cursor over it, and its PostDisplay calls DrawFrame
//   Input            - InputDispatchHook routes events to the window; while it is
//                      open the game sees only mouse events
//   Install          - writes the two trampoline calls (SE/AE only), registers the menu
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
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	std::atomic_bool g_renderReady = false;
	std::atomic_bool g_menuRegistered = false;

	constexpr std::string_view kMenuName = "TailorOutfitCaptureMenu";

	// --- Hook targets ---------------------------------------------------------

	struct D3DInitHook
	{
		static constexpr auto id = REL::RelocationID(75595, 77226);
		static constexpr auto offset = REL::VariantOffset(0x9, 0x275, 0x0);

		static void                                    thunk();
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
		io.MouseDrawCursor = false;  // the game's Cursor Menu draws the cursor (CaptureMenu uses it)

		Theme::LoadFonts();  // style and scale are set per frame by Theme::Apply

		if (!ImGui_ImplWin32_Init(desc.OutputWindow) || !ImGui_ImplDX11_Init(device, context)) {
			logger::error("ImGui: backend init failed");
			return;
		}

		WndProcHook::func = reinterpret_cast<WNDPROC>(
			SetWindowLongPtrA(desc.OutputWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProcHook::thunk)));

		Theme::LoadTextures(device);
		g_renderReady = true;
		logger::info("ImGui initialised ({}x{})", desc.BufferDesc.Width, desc.BufferDesc.Height);
	}

	// --- Frame ----------------------------------------------------------------

	// Contract: called only from CaptureMenu::PostDisplay, so only while the menu
	// is open. No ImGui frame runs while the window is closed.
	void DrawFrame()
	{
		if (!g_renderReady || !CaptureWindow::IsOpen()) {
			return;
		}

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();  // sets io.DisplaySize; also queues the OS cursor, which FeedInput overrides
		Theme::Apply(Settings::Skin(), ImGui::GetIO().DisplaySize.y);
		CaptureWindow::FeedInput();
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

	// --- Menu -----------------------------------------------------------------

	// A game menu without a movie, flagged like Tailor 3.0's own ImGui menu.
	// While it is open the game shows its cursor (Cursor Menu, depth 13, drawn
	// above this one) and switches controls to menu mode, so mouse events move
	// the cursor instead of the camera.
	class CaptureMenu final : public RE::IMenu
	{
	public:
		CaptureMenu()
		{
			using Flag = RE::UI_MENU_FLAGS;
			menuFlags.set(Flag::kUsesCursor, Flag::kUpdateUsesCursor, Flag::kUsesMenuContext, Flag::kCustomRendering,
				Flag::kDisablePauseMenu);
			inputContext.set(Context::kMenuMode);
			depthPriority = 11;
		}

		static RE::IMenu* Create() { return new CaptureMenu(); }

		void PostDisplay() override { DrawFrame(); }

		RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& a_message) override
		{
			switch (*a_message.type) {
			case RE::UI_MESSAGE_TYPE::kHide:
			case RE::UI_MESSAGE_TYPE::kForceHide:
				// CaptureWindow::Close marks the window closed before it hides the menu,
				// so an open window here means the game closed the menu on its own:
				// close the window too, or time stays frozen with nothing on screen.
				if (CaptureWindow::IsOpen()) {
					SKSE::GetTaskInterface()->AddTask([] { CaptureWindow::Close(); });
				}
				break;
			case RE::UI_MESSAGE_TYPE::kUserEvent:
			case RE::UI_MESSAGE_TYPE::kScaleformEvent:
				return RE::UI_MESSAGE_RESULTS::kHandled;  // input reaches ImGui through InputDispatchHook
			default:
				break;
			}
			return RE::IMenu::ProcessMessage(a_message);
		}
	};

	void SendMenuMessage(RE::UI_MESSAGE_TYPE a_type)
	{
		if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
			queue->AddMessage(kMenuName, a_type, nullptr);
		}
	}

	// --- Input ----------------------------------------------------------------

	// Contract: game thread. While the window is open the game sees only mouse
	// events: its Cursor Menu moves the cursor from them, and CaptureMenu's menu
	// context keeps them from player controls. Keys and characters reach ImGui only.
	// The engine owns the event chain, so hidden events are unlinked for this
	// call only and every link is restored afterwards.
	void InputDispatchHook::thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events)
	{
		if (!a_events || !*a_events || !CaptureWindow::IsOpen()) {
			func(a_dispatcher, a_events);
			return;
		}
		CaptureWindow::QueueInput(a_events);

		static std::vector<std::pair<RE::InputEvent*, RE::InputEvent*>> links;  // event, its original next
		links.clear();
		for (auto* event = *a_events; event; event = event->next) {
			links.emplace_back(event, event->next);
		}
		RE::InputEvent* head = nullptr;
		RE::InputEvent* tail = nullptr;
		for (const auto& [event, next] : links) {
			if (event->GetDevice() == RE::INPUT_DEVICE::kMouse) {
				(tail ? tail->next : head) = event;
				tail = event;
			}
		}
		if (tail) {
			tail->next = nullptr;
		}
		RE::InputEvent* const visible[] = { head };
		func(a_dispatcher, visible);
		for (const auto& [event, next] : links) {
			event->next = next;
		}
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
		WriteCall<InputDispatchHook>(trampoline);
		logger::info("Hooks installed");
	}

	void RegisterMenu()
	{
		if (REL::Module::IsVR()) {
			return;
		}
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			logger::error("UI singleton missing: capture window disabled");
			return;
		}
		ui->Register(kMenuName, CaptureMenu::Create);
		g_menuRegistered = true;
		logger::info("Capture menu registered");
	}

	void ShowMenu()
	{
		SendMenuMessage(RE::UI_MESSAGE_TYPE::kShow);
	}

	void HideMenu()
	{
		SendMenuMessage(RE::UI_MESSAGE_TYPE::kHide);
	}

	bool IsRenderReady()
	{
		return g_renderReady && g_menuRegistered;
	}
}
