// Object			OS				DXGI version	Feature level
// IDXGIDevice		Win7			1.0				11.0
// IDXGIDevice1		Win7			1.0				11.0
// IDXGIDevice2		Platform update	1.2				11.1
// IDXGIDevice3		Win8.1			1.3
// IDXGIDevice4						1.5
// 
// IDXGIAdapter		Win7			1.0				11.0
// IDXGIAdapter1	Win7			1.0				11.0
// IDXGIAdapter2	Platform update	1.2				11.1
// IDXGIAdapter3					1.3
// 
// IDXGIFactory		Win7			1.0				11.0
// IDXGIFactory1	Win7			1.0				11.0
// IDXGIFactory2	Platform update	1.2				11.1
// IDXGIFactory3	Win8.1			1.3
// IDXGIFactory4					1.4
// IDXGIFactory5					1.5
// 
// IDXGIOutput		Win7			1.0				11.0
// IDXGIOutput1		Platform update	1.2				11.1
// IDXGIOutput2		Win8.1			1.3
// IDXGIOutput3		Win8.1			1.3
// IDXGIOutput4		Win10			1.4
// IDXGIOutput5		Win10			1.5
// 
// IDXGIResource	Win7			1.0				11.0
// IDXGIResource1	Platform update	1.2				11.1
// 
// IDXGISwapChain	Win7			1.0				11.0
// IDXGISwapChain1	Platform update	1.2				11.1
// IDXGISwapChain2	Win8.1			1.3
// IDXGISwapChain3	Win10			1.4
// IDXGISwapChain4					1.5


// 1-15-18: New approach is keep a strict single-layer policy when wrapping
// objects like IDXGISwapChain1.  Only the top level object we are interested
// in can successfully wrapped, because otherwise the vtable is altered from
// the DX11 definition, which led to crashes.
// 
// Because of this, we are now creating only the HackerSwapChain and no
// other objects.  IDXGIFactory does not need to be wrapped, because it must
// be hooked in order to create the swap chains correctly. Device, Object,
// Adapter, Unknown were only wrapped in order to get us to the swap chain,
// and the new approach is to directly hook the CreateSwapChain and
// CreateSwapChainForHwnd, which saves a lot of complexity.
//
// We are making an IDXGISwapChain1, as the obvious descendant of SwapChain,
// and valid in platform_update scenarios.  It's only created by the
// CreateSwapChainForHwnd, but all our functionality is the same regardless.
// In the situation where the evil platform update is not installed, we will
// only create an IDXGISwapChain, but still reference it via composition in
// the HackerSwapChain.  The model is the same as that used in HackerDevice
// and HackerContext.

// Include before util.h (or any header that includes util.h) to get pretty
// version of LockResourceCreationMode:
#include "lock.h"

#include "HackerDXGI.h"
#include "HookedDevice.h"
#include "HookedDXGI.h"

#include "log.h"
#include "util.h"
#include "globals.h"
#include "Hunting.h"
#include "Override.h"
#include "IniHandler.h"
#include "CommandList.h"
#include "profiling.h"
#include "cursor.h" // For InstallHookLate

#include "SSMTBridge.h"

#include <mutex>
#include <algorithm>
#include <vector>


// ReShade is deliberately hosted from the final GIMI Present path instead of
// being injected as another graphics wrapper.  This keeps ownership of the
// D3D11 device and swap chain with GIMI while still allowing ReShade to process
// the fully-modified/upscaled back buffer immediately before it is presented.
namespace
{
using ReShadeCreateEffectRuntimeFn = bool (*)(uint32_t api, void *opaque_device,
	void *opaque_command_queue, void *opaque_swapchain, const char *config_path,
	void **out_runtime);
using ReShadeDestroyEffectRuntimeFn = void (*)(void *runtime);
using ReShadeUpdateAndPresentEffectRuntimeFn = void (*)(void *runtime);

constexpr uint32_t kReShadeD3D11Api = 0xb000;

// ReShade normally records the active DXGI color space from its SetColorSpace1
// hook under this private-data key. The hosted runtime intentionally disables
// graphics hooks, so GIMI must provide the equivalent metadata itself.
constexpr GUID kReShadeSwapChainColorSpaceKey =
	{ 0x18b57e4, 0x1493, 0x4953, { 0xad, 0xf2, 0xde, 0x6d, 0x99, 0xcc, 0x5, 0xe5 } };

static std::wstring GetEnvironmentValue(const wchar_t *name)
{
	const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
	if (required == 0)
		return std::wstring();

	std::vector<wchar_t> value(required);
	const DWORD copied = GetEnvironmentVariableW(name, value.data(), required);
	if (copied == 0 || copied >= required)
		return std::wstring();
	return std::wstring(value.data(), copied);
}

static void GetHostedReShadeSettings(std::wstring &dll_path, std::wstring &config_path)
{
	dll_path = GetEnvironmentValue(L"GIMI_HOSTED_RESHADE_DLL");
	config_path = GetEnvironmentValue(L"GIMI_HOSTED_RESHADE_CONFIG");
}

static bool HostedReShadeEnabled()
{
	static const bool enabled =
		GetEnvironmentVariableW(L"GIMI_HOSTED_RESHADE_DLL", nullptr, 0) > 1 &&
		GetEnvironmentVariableW(L"GIMI_HOSTED_RESHADE_CONFIG", nullptr, 0) > 1;
	return enabled;
}

static std::string ToUtf8(const std::wstring &value)
{
	if (value.empty())
		return std::string();
	const int required = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1,
		nullptr, 0, nullptr, nullptr);
	if (required <= 1)
		return std::string();
	std::string result(static_cast<size_t>(required), '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, &result[0], required,
		nullptr, nullptr);
	result.resize(static_cast<size_t>(required - 1));
	return result;
}

class GimiHostedReShadeRuntime
{
public:
	void Present(IDXGISwapChain1 *swap_chain, HackerContext *hacker_context)
	{
		if (swap_chain == nullptr || hacker_context == nullptr)
			return;

		std::lock_guard<std::mutex> lock(mMutex);
		if (mSwapChain != nullptr && mSwapChain != swap_chain)
			ResetLocked();
		mSwapChain = swap_chain;

		if (mRuntime == nullptr && !mInitializationFailed)
			InitializeLocked(swap_chain, hacker_context);
		if (mRuntime != nullptr && mUpdateAndPresent != nullptr)
			mUpdateAndPresent(mRuntime);
	}

	void Reset()
	{
		std::lock_guard<std::mutex> lock(mMutex);
		ResetLocked();
	}

private:
	void InitializeLocked(IDXGISwapChain1 *swap_chain, HackerContext *hacker_context)
	{
		std::wstring dll_path;
		std::wstring config_path;
		GetHostedReShadeSettings(dll_path, config_path);
		if (dll_path.empty() || config_path.empty())
		{
			mInitializationFailed = true;
			LogInfo("GIMI hosted ReShade disabled: GIMI_HOSTED_RESHADE_DLL or "
				"GIMI_HOSTED_RESHADE_CONFIG is not set\n");
			return;
		}

		// The DLL must be loaded with its graphics hooks disabled.  Its public
		// runtime API remains available, but it cannot replace GIMI's objects.
		SetEnvironmentVariableW(L"RESHADE_DISABLE_GRAPHICS_HOOK", L"1");
		mModule = LoadLibraryW(dll_path.c_str());
		if (mModule == nullptr)
		{
			mInitializationFailed = true;
			LogInfo("GIMI hosted ReShade failed to load %s (error %lu)\n",
				ToUtf8(dll_path).c_str(), GetLastError());
			return;
		}

		const auto create = reinterpret_cast<ReShadeCreateEffectRuntimeFn>(
			GetProcAddress(mModule, "ReShadeCreateEffectRuntime"));
		mDestroy = reinterpret_cast<ReShadeDestroyEffectRuntimeFn>(
			GetProcAddress(mModule, "ReShadeDestroyEffectRuntime"));
		mUpdateAndPresent = reinterpret_cast<ReShadeUpdateAndPresentEffectRuntimeFn>(
			GetProcAddress(mModule, "ReShadeUpdateAndPresentEffectRuntime"));
		if (create == nullptr || mDestroy == nullptr || mUpdateAndPresent == nullptr)
		{
			mInitializationFailed = true;
			LogInfo("GIMI hosted ReShade exports are unavailable in %s\n",
				ToUtf8(dll_path).c_str());
			return;
		}

		// Get the device through the exact swap chain passed to ReShade.  The
		// ReShade C API checks COM pointer identity and rejects a different view.
		ID3D11Device *device = nullptr;
		const HRESULT device_hr = swap_chain->GetDevice(IID_PPV_ARGS(&device));
		if (FAILED(device_hr) || device == nullptr)
		{
			mInitializationFailed = true;
			LogInfo("GIMI hosted ReShade could not get the swap-chain device: %x\n",
				device_hr);
			return;
		}

		ID3D11DeviceContext1 *context = hacker_context->GetPassThroughOrigContext1();

		// Genshin's native HDR path uses an R10G10B10A2 swap chain and exposes
		// IDXGISwapChain3 directly, so SetColorSpace1 bypasses this wrapper. Without
		// ReShade's normal DXGI hook, its hosted runtime otherwise assumes sRGB and
		// writes raw PQ values into an 8-bit SDR screenshot, producing a grey image.
		// Mirror ReShade's hook metadata before creating the hosted runtime so it
		// selects the tagged 16-bit HDR PNG path and compiles effects for HDR10/PQ.
		DXGI_SWAP_CHAIN_DESC swap_desc = {};
		if (SUCCEEDED(swap_chain->GetDesc(&swap_desc)) &&
			swap_desc.BufferDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM)
		{
			const DXGI_COLOR_SPACE_TYPE color_space =
				DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
			const HRESULT color_space_hr = swap_chain->SetPrivateData(
				kReShadeSwapChainColorSpaceKey, sizeof(color_space), &color_space);
			LogInfo("GIMI hosted ReShade HDR10/PQ metadata %s (%x)\n",
				SUCCEEDED(color_space_hr) ? "installed" : "failed", color_space_hr);
		}

		std::string utf8_config = ToUtf8(config_path);
		const bool created = create(kReShadeD3D11Api, device, context, swap_chain,
			utf8_config.c_str(), &mRuntime);
		device->Release();

		if (!created || mRuntime == nullptr)
		{
			mRuntime = nullptr;
			mInitializationFailed = true;
			LogInfo("GIMI hosted ReShade runtime initialization failed for %s\n",
				utf8_config.c_str());
			return;
		}

		LogInfo("GIMI hosted ReShade runtime initialized on final GIMI Present path\n");
	}

	void ResetLocked()
	{
		if (mRuntime != nullptr && mDestroy != nullptr)
			mDestroy(mRuntime);
		mRuntime = nullptr;
		mSwapChain = nullptr;
		mInitializationFailed = false;
	}

	std::mutex mMutex;
	HMODULE mModule = nullptr;
	IDXGISwapChain1 *mSwapChain = nullptr;
	void *mRuntime = nullptr;
	ReShadeDestroyEffectRuntimeFn mDestroy = nullptr;
	ReShadeUpdateAndPresentEffectRuntimeFn mUpdateAndPresent = nullptr;
	bool mInitializationFailed = false;
};

GimiHostedReShadeRuntime gGimiHostedReShadeRuntime;

std::mutex gFrameActionOwnerMutex;
HackerSwapChain *gPreferredFrameActionOwner = nullptr; // Borrowed; protected by the mutex.
std::vector<HackerSwapChain *> gPendingAuxiliarySwapChains;
}




// -----------------------------------------------------------------------------
// SetWindowPos hook, activated by full_screen=2 in d3dx.ini

static BOOL(WINAPI *fnOrigSetWindowPos)(_In_ HWND hWnd, _In_opt_ HWND hWndInsertAfter,
	_In_ int X, _In_ int Y, _In_ int cx, _In_ int cy, _In_ UINT uFlags) = nullptr;

static BOOL WINAPI Hooked_SetWindowPos(
	_In_ HWND hWnd,
	_In_opt_ HWND hWndInsertAfter,
	_In_ int X,
	_In_ int Y,
	_In_ int cx,
	_In_ int cy,
	_In_ UINT uFlags)
{
	if (G->SCREEN_UPSCALING != 0) {
		// Force desired upscaled resolution (only when desired resolution is provided!)
		if (cx != 0 && cy != 0) {
			cx = G->SCREEN_WIDTH;
			cy = G->SCREEN_HEIGHT;
			X = 0;
			Y = 0;
		}
	}
	else if (G->SCREEN_FULLSCREEN == 2) {
		// Do nothing - passing this call through could change the game
		// to a borderless window. Needed for The Witness.
		return true;
	}

	return fnOrigSetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}


void InstallSetWindowPosHook()
{
	HINSTANCE hUser32;
	int fail = 0;

	// Only attempt to hook it once:
	if (fnOrigSetWindowPos != nullptr)
		return;

	hUser32 = NktHookLibHelpers::GetModuleBaseAddress(L"User32.dll");
	fail |= InstallHookLate(hUser32, "SetWindowPos", (void**)&fnOrigSetWindowPos, Hooked_SetWindowPos);

	if (fail) {
		LogOverlay(LOG_DIRE, "Failed to hook SetWindowPos for full_screen=2\n");
		return;
	}

	LogInfo("Successfully hooked SetWindowPos for full_screen=2\n");
	return;
}

// -----------------------------------------------------------------------------

// In the Elite Dangerous case, they Release the HackerContext objects before creating the 
// swap chain.  That causes problems, because we are not expecting anyone to get here without
// having a valid context.  They later call GetImmediateContext, which will generate a wrapped
// context.  So, since we need the context for our Overlay, let's do that a litte early in
// this case, which will save the reference for their GetImmediateContext call.

HackerSwapChain::HackerSwapChain(IDXGISwapChain1 *pSwapChain, HackerDevice *pDevice, HackerContext *pContext)
{
	mOrigSwapChain1 = pSwapChain;
	mOrigSwapChain3 = nullptr;
	if (HostedReShadeEnabled() &&
		SUCCEEDED(pSwapChain->QueryInterface(__uuidof(IDXGISwapChain3),
		reinterpret_cast<void **>(&mOrigSwapChain3)))) {
		// mOrigSwapChain1 keeps the underlying COM object alive for this wrapper.
		mOrigSwapChain3->Release();
	}

	mHackerDevice = pDevice;
	mHackerContext = pContext;

	// Bump the refcounts on the device and context to make sure they can't
	// be released as long as the swap chain is alive and we may be
	// accessing them. We probably don't actually need to do this for the
	// device, since the DirectX swap chain should already hold a reference
	// to the DirectX device, but it shouldn't hurt and makes the code more
	// semantically correct since we access the device as well. We could
	// skip both by looking them up on demand, but that would need extra
	// lookups in fast paths and there's no real need.
	//
	// The overlay also bumps these refcounts, which is technically
	// unecessary given we now do so here, but also shouldn't hurt, and is
	// safer in case we ever change this again and forget about it.

	mHackerDevice->AddRef();
	if (mHackerContext) {
		mHackerContext->AddRef();
	} else {
		ID3D11DeviceContext *tmpContext = NULL;
		// GetImmediateContext will bump the refcount for us.
		// In the case of hooking, GetImmediateContext will not return
		// a HackerContext, so we don't use it's return directly, but
		// rather just use it to make GetHackerContext valid:
		mHackerDevice->GetImmediateContext(&tmpContext);
		mHackerContext = mHackerDevice->GetHackerContext();
	}

	mHackerDevice->SetHackerSwapChain(this);

	try {
		// Create Overlay class that will be responsible for drawing any text
		// info over the game. Using the Hacker Device and Context we gave the game.
		mOverlay = new Overlay(mHackerDevice, mHackerContext, mOrigSwapChain1);
	}
	catch (...) {
		LogInfo("  *** Failed to create Overlay. Exception caught.\n");
		mOverlay = NULL;
	}
}

void HackerSwapChain::RegisterFrameActionOwner(bool device_was_wrapped)
{
	if (!HostedReShadeEnabled())
		return;

	std::lock_guard<std::mutex> lock(gFrameActionOwnerMutex);
	if (device_was_wrapped) {
		// 游戏交换链使用 GIMI 返回的 HackerDevice。显卡插层可能先用原生设备
		// 创建辅助交换链；其 Present 触发的 Mod 资源重载仍须使用游戏设备。
		gPreferredFrameActionOwner = this;
		for (HackerSwapChain *auxiliary : gPendingAuxiliarySwapChains) {
			AddRef();
			auxiliary->mFrameActionOwner.store(this, std::memory_order_release);
			LogInfo("SSMT frame actions: auxiliary %p uses game swap chain %p\n",
				auxiliary, this);
		}
		gPendingAuxiliarySwapChains.clear();
	} else if (gPreferredFrameActionOwner) {
		gPreferredFrameActionOwner->AddRef();
		mFrameActionOwner.store(gPreferredFrameActionOwner, std::memory_order_release);
		LogInfo("SSMT frame actions: auxiliary %p uses game swap chain %p\n",
			this, gPreferredFrameActionOwner);
	} else {
		gPendingAuxiliarySwapChains.push_back(this);
	}
}

IDXGISwapChain1* HackerSwapChain::GetOrigSwapChain1()
{
	LogDebug("HackerSwapChain::GetOrigSwapChain returns %p\n", mOrigSwapChain1);
	return mOrigSwapChain1;
}


// Called at each DXGI::Present() to give us reliable time to execute user
// input and hunting commands.

void HackerSwapChain::RunFrameActions()
{
	LogDebug("Running frame actions.  Device: %p\n", mHackerDevice);

	// Regardless of log settings, since this runs every frame, let's flush the log
	// so that the most lost will be one frame worth.  Tradeoff of performance to accuracy
	if (LogFile) fflush(LogFile);

	uint64_t system_tick_count = GetSystemTicks();

	G->gFrameTime = (system_tick_count - G->gSystemTickCount) / 1'000'000.0f;
	G->gTime = (system_tick_count - G->ticks_at_launch) / 1'000'000.0f;

	G->gSystemTickCount = system_tick_count;

	G->gFPSCounter.Update(system_tick_count);

	// Run the command list here, before drawing the overlay so that a
	// custom shader on the present call won't remove the overlay. Also,
	// run this before most frame actions so that this can be considered as
	// a pre-present command list. We have a separate post-present command
	// list after the present call in case we need to restore state or
	// affect something at the start of the frame.
	RunCommandList(mHackerDevice, mHackerContext, &G->present_command_list, NULL, false);

	if (G->analyse_frame) {
		// We don't allow hold to be changed mid-frame due to potential
		// for filename conflicts, so use def_analyse_options:
		if (G->def_analyse_options & FrameAnalysisOptions::HOLD) {
			// If using analyse_options=hold we don't stop the
			// analysis at the frame boundary (it will be stopped
			// at the key up event instead), but we do increment
			// the frame count and reset the draw count:
			G->analyse_frame_no++;
		} else {
			G->analyse_frame = false;
			if (G->DumpUsage)
				DumpUsage(G->ANALYSIS_PATH);
			LogOverlayW(LOG_INFO, L"Frame analysis saved to %ls\n", G->ANALYSIS_PATH);
		}
	}

	// NOTE: Now that key overrides can check an ini param, the ordering of
	// this and the present_command_list is significant. We might set an
	// ini param during a frame for scene detection, which is checked on
	// override activation, then cleared from the command list run on
	// present. If we ever needed to run the command list before this
	// point, we should consider making an explicit "pre" command list for
	// that purpose rather than breaking the existing behaviour.
	bool newEvent = DispatchInputEvents(mHackerDevice);

	CurrentTransition.UpdatePresets(mHackerDevice);
	CurrentTransition.UpdateTransitions(mHackerDevice);

	// The config file is not safe to reload from within the input handler
	// since it needs to change the key bindings, so it sets this flag
	// instead and we handle it now.
	if (G->gReloadConfigPending)
		ReloadConfig(mHackerDevice);

	// Regular LoadConfigFile() on startup fails to properly load all resources in some edge cases 
	// So, as bandaid solution, it has some sense to force ReloadConfig() after DLL is fully initialized
	// This way resources will be loaded properly before modded object appear on screen and cause crash
	if (G->gConfigInitialized) {
		// Autosave persistent variables every gSettingsAutoSaveInterval seconds
		if (G->gTime - G->gSettingsSaveTime > G->gSettingsAutoSaveInterval) {
			if (SavePersistentSettings())
				SaveUnknownPersistentSettings();
			//LogOverlay(LOG_INFO, "Saved Persistent Variables\n");
		}
	}
	else {
		if (G->gTime > G->gConfigInitializationDelay) {
			G->gConfigInitialized = true;
			ReloadConfig(mHackerDevice);
		}
	}

	// Draw the on-screen overlay text with hunting and informational
	// messages, before final Present. We now do this after the shader and
	// config reloads, so if they have any notices we will see them this
	// frame (just in case we crash next frame or something).
	if (mOverlay && !G->suppress_overlay)
		mOverlay->DrawOverlay();
	G->suppress_overlay = false;

	// This must happen on the same side of the config and shader reloads
	// to ensure the config reload can't clear messages from the shader
	// reload. It doesn't really matter which side we do it on at the
	// moment, but let's do it last, because logically it makes sense to be
	// incremented when we call the original present call:
	G->frame_no++;
	mHackerContext->ResetCallCounters();

	// When not hunting most keybindings won't have been registered, but
	// still skip the below logic that only applies while hunting.
	if (G->hunting != HUNTING_MODE_ENABLED)
		return;

	// Update the huntTime whenever we get fresh user input.
	if (newEvent)
		G->huntTime = time(NULL);

	// Clear buffers after some user idle time.  This allows the buffers to be
	// stable during a hunt, and cleared after one minute of idle time.  The idea
	// is to make the arrays of shaders stable so that hunting up and down the arrays
	// is consistent, while the user is engaged.  After 1 minute, they are likely onto
	// some other spot, and we should start with a fresh set, to keep the arrays and
	// active shader list small for easier hunting.  Until the first keypress, the arrays
	// are cleared at each thread wake, just like before. 
	// The arrays will be continually filled by the SetShader sections, but should 
	// rapidly converge upon all active shaders.

	if (difftime(time(NULL), G->huntTime) > 60) {
		EnterCriticalSectionPretty(&G->mCriticalSection);
		TimeoutHuntingBuffers();
		LeaveCriticalSection(&G->mCriticalSection);
	}
}


// -----------------------------------------------------------------------------
/** IUnknown **/

// In the game Elex, we see them call do the unusual SwapChain->QueryInterface(SwapChain).  
// We need to return This when that happens, because otherwise they disconnect us and
// we never get calls to Present.  Rather than do just this one-off, let's always 
// return This for any time this might happen, as we've seen it happen in HackerContext
// too, for Mafia 3.  So any future instances cannot leak.
//
// From: https://msdn.microsoft.com/en-us/library/windows/desktop/ms682521(v=vs.85).aspx
// And: https://blogs.msdn.microsoft.com/oldnewthing/20040326-00/?p=40033
//
//  For any one object, a specific query for the IUnknown interface on any of the object's 
//	interfaces must always return the same pointer value. This enables a client to determine 
//	whether two pointers point to the same component by calling QueryInterface with 
//	IID_IUnknown and comparing the results. 
//	It is specifically not the case that queries for interfaces other than IUnknown (even 
//	the same interface through the same pointer) must return the same pointer value.
//
STDMETHODIMP HackerSwapChain::QueryInterface(THIS_
	/* [in] */ REFIID riid,
	/* [iid_is][out] */ _COM_Outptr_ void __RPC_FAR *__RPC_FAR *ppvObject)
{
	LogInfo("HackerSwapChain::QueryInterface(%s@%p) called with IID: %s\n", type_name(this), this, NameFromIID(riid).c_str());

	HRESULT hr = mOrigSwapChain1->QueryInterface(riid, ppvObject);
	if (FAILED(hr) || !*ppvObject)
	{
		LogInfo("  failed result = %x for %p\n", hr, ppvObject);
		return hr;
	}

	// A native v2/v3 pointer lets the caller bypass our Present frame actions.
	// Hosted ReShade requires those actions for GIMI's delayed Mod load and NR.
	if (HostedReShadeEnabled() && mOrigSwapChain3 &&
		(riid == __uuidof(IDXGISwapChain2) || riid == __uuidof(IDXGISwapChain3)))
	{
		*ppvObject = static_cast<IDXGISwapChain3 *>(this);
		LogInfo("  retaining hosted IDXGISwapChain2/3 wrapper %p\n", this);
		return hr;
	}

	// Native HDR swap chains use IDXGISwapChain3/4 to negotiate the HDR
	// color space (SetColorSpace1) and frame-latency state.  The old GIMI
	// wrapper deliberately rejected these interfaces because returning the
	// wrapper with a newer vtable was unsafe for some older games.  Returning
	// E_NOINTERFACE here prevents Unity/Genshin from completing HDR setup and
	// leaves a black back buffer.  For the R10G10B10A2 HDR path, expose the
	// original interface pointer instead; calls that need the newer vtable
	// then use the DXGI implementation with the correct ABI, while the
	// IDXGISwapChain1 methods remain handled by this wrapper.
	DXGI_SWAP_CHAIN_DESC current_desc = {};
	const bool native_hdr_swap_chain =
		SUCCEEDED(mOrigSwapChain1->GetDesc(&current_desc)) &&
		current_desc.BufferDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM;
	if (native_hdr_swap_chain &&
		(riid == __uuidof(IDXGISwapChain2) ||
		 riid == __uuidof(IDXGISwapChain3) ||
		 riid == __uuidof(IDXGISwapChain4)))
	{
		LogInfo("  exposing native HDR %s interface %p\n", NameFromIID(riid).c_str(), *ppvObject);
		return hr;
	}

	// For TheDivision, only upon Win10, it will request these.  Even though the object
	// we would return is the exact same pointer in memory, it still calls into the object
	// with a vtable entry that does not match what they expected. Somehow they decide
	// they are on Win10, and know these APIs ought to exist.  Does not crash on Win7.
	//
	// Returning an E_NOINTERFACE here seems to work, but this does call into question our 
	// entire wrapping strategy.  If the object we've wrapped is a superclass of the
	// object they desire, the vtable is not going to match.

	if (riid == __uuidof(IDXGISwapChain2))
	{
		// Return interface without wrapper to support Endfield's pipeline.
		LogInfo("  return IDXGISwapChain2 interface (%p) without wrapper.\n", ppvObject);
		LogInfo("  returns result = %x for %p\n", hr, ppvObject);
		return hr;
	}
	if (riid == __uuidof(IDXGISwapChain3))
	{
		// Return interface without wrapper to support HDR color space setup.
		LogInfo("  return IDXGISwapChain3 interface (%p) without wrapper.\n", ppvObject);
		LogInfo("  returns result = %x for %p\n", hr, ppvObject);
		return hr;
	}
	if (riid == __uuidof(IDXGISwapChain4))
	{
		LogInfo("***  returns E_NOINTERFACE as error for IDXGISwapChain4.\n");
		*ppvObject = NULL;
		return E_NOINTERFACE;
	}

	IUnknown* unk_this;
	HRESULT hr_this = mOrigSwapChain1->QueryInterface(__uuidof(IUnknown), (void**)&unk_this);

	IUnknown* unk_ppvObject;
	HRESULT hr_ppvObject = reinterpret_cast<IUnknown*>(*ppvObject)->QueryInterface(__uuidof(IUnknown), (void**)&unk_ppvObject);

	if (SUCCEEDED(hr_this) && SUCCEEDED(hr_ppvObject))
	{
		// For an actual case of this->QueryInterface(this), just return our HackerSwapChain object.
		if (unk_this == unk_ppvObject)
			*ppvObject = this;

		unk_this->Release();
		unk_ppvObject->Release();

		LogInfo("  return HackerSwapChain(%s@%p) wrapper of %p\n", type_name(this), this, mOrigSwapChain1);
		return hr;
	}

	LogInfo("  returns result = %x for %p\n", hr, ppvObject);
	return hr;
}

STDMETHODIMP_(ULONG) HackerSwapChain::AddRef(THIS)
{
	ULONG ulRef = mOrigSwapChain1->AddRef();
	LogInfo("HackerSwapChain::AddRef(%s@%p), counter=%d, this=%p\n", type_name(this), this, ulRef, this);
	return ulRef;
}

STDMETHODIMP_(ULONG) HackerSwapChain::Release(THIS)
{
	ULONG ulRef = mOrigSwapChain1->Release();
	LogInfo("HackerSwapChain::Release(%s@%p), counter=%d, this=%p\n", type_name(this), this, ulRef, this);

	if (ulRef <= 0)
	{
		HackerSwapChain *frame_owner = nullptr;
		{
			std::lock_guard<std::mutex> lock(gFrameActionOwnerMutex);
			if (gPreferredFrameActionOwner == this)
				gPreferredFrameActionOwner = nullptr;
			auto it = std::find(gPendingAuxiliarySwapChains.begin(),
				gPendingAuxiliarySwapChains.end(), this);
			if (it != gPendingAuxiliarySwapChains.end())
				gPendingAuxiliarySwapChains.erase(it);
			frame_owner = mFrameActionOwner.exchange(nullptr, std::memory_order_acq_rel);
		}
		if (frame_owner)
			frame_owner->Release();

		if (mHackerDevice) {
			if (mHackerDevice->GetHackerSwapChain() == this) {
				LogInfo("  Clearing mHackerDevice->mHackerSwapChain\n");
				mHackerDevice->SetHackerSwapChain(nullptr);
			} else
				LogInfo("  mHackerDevice %p not using mHackerSwapchain %p\n", mHackerDevice, this);
			mHackerDevice->Release();
		}

		if (mHackerContext)
			mHackerContext->Release();

		if (mOverlay)
			delete mOverlay;

		if (last_fullscreen_swap_chain == mOrigSwapChain1)
			last_fullscreen_swap_chain = NULL;

		LogInfo("  counter=%d, this=%p, deleting self.\n", ulRef, this);

		delete this;
		return 0L;
	}
	return ulRef;
}

// -----------------------------------------------------------------------------
/** IDXGIObject **/

STDMETHODIMP HackerSwapChain::SetPrivateData(THIS_
	/* [annotation][in] */
	__in  REFGUID Name,
	/* [in] */ UINT DataSize,
	/* [annotation][in] */
	__in_bcount(DataSize)  const void *pData)
{
	LogInfo("HackerSwapChain::SetPrivateData(%s@%p) called with GUID: %s\n", type_name(this), this, NameFromIID(Name).c_str());
	LogInfo("  DataSize = %d\n", DataSize);

	HRESULT hr = mOrigSwapChain1->SetPrivateData(Name, DataSize, pData);
	LogInfo("  returns result = %x\n", hr);
	return hr;
}

STDMETHODIMP HackerSwapChain::SetPrivateDataInterface(THIS_
	/* [annotation][in] */
	__in  REFGUID Name,
	/* [annotation][in] */
	__in  const IUnknown *pUnknown)
{
	LogInfo("HackerSwapChain::SetPrivateDataInterface(%s@%p) called with GUID: %s\n", type_name(this), this, NameFromIID(Name).c_str());

	HRESULT hr = mOrigSwapChain1->SetPrivateDataInterface(Name, pUnknown);
	LogInfo("  returns result = %x\n", hr);
	return hr;
}

STDMETHODIMP HackerSwapChain::GetPrivateData(THIS_
	/* [annotation][in] */
	__in  REFGUID Name,
	/* [annotation][out][in] */
	__inout  UINT *pDataSize,
	/* [annotation][out] */
	__out_bcount(*pDataSize)  void *pData)
{
	LogInfo("HackerSwapChain::GetPrivateData(%s@%p) called with GUID: %s\n", type_name(this), this, NameFromIID(Name).c_str());

	HRESULT hr = mOrigSwapChain1->GetPrivateData(Name, pDataSize, pData);
	LogInfo("  returns result = %x\n", hr);
	return hr;
}


// More details: https://msdn.microsoft.com/en-us/library/windows/apps/hh465096.aspx
//
// This is the root class object, expected to be used for HackerDXGIAdapter, and
// HackerDXGIDevice GetParent() calls.  It would be legitimate for a caller to
// QueryInterface their objects to get the DXGIObject, and call GetParent, so
// this should be more robust.
//
// If the parent request is for the IDXGIAdapter or IDXGIFactory, that must mean 
// we are taking the secret path for getting the swap chain. 
//
// We no longer return wrapped objects here, because our CreateSwapChain hooks 
// will correctly catch creation.

STDMETHODIMP HackerSwapChain::GetParent(THIS_
	/* [annotation][in] */
	__in  REFIID riid,
	/* [annotation][retval][out] */
	__out  void **ppParent)
{
	LogInfo("HackerSwapChain::GetParent(%s@%p) called with IID: %s\n", type_name(this), this, NameFromIID(riid).c_str());

	HRESULT hr = mOrigSwapChain1->GetParent(riid, ppParent);
	if (FAILED(hr))
	{
		LogInfo("  failed result = %x for %p\n", hr, ppParent);
		return hr;
	}

	LogInfo("  returns result = %#x\n", hr);
	return hr;
}

// -----------------------------------------------------------------------------
/** IDXGIDeviceSubObject **/

STDMETHODIMP HackerSwapChain::GetDevice(
	/* [annotation][in] */
	_In_  REFIID riid,
	/* [annotation][retval][out] */
	_Out_  void **ppDevice)
{
	LogDebug("HackerSwapChain::GetDevice(%s@%p) called with IID: %s\n", type_name(this), this, NameFromIID(riid).c_str());

	HRESULT hr = mOrigSwapChain1->GetDevice(riid, ppDevice);
	LogDebug("  returns result = %x, handle = %p\n", hr, *ppDevice);
	return hr;
}


// -----------------------------------------------------------------------------
/** IDXGISwapChain **/

STDMETHODIMP HackerSwapChain::Present(THIS_
	/* [in] */ UINT SyncInterval,
	/* [in] */ UINT Flags)
{
	Profiling::State profiling_state = {0};
	bool profiling = false;
	LogDebug("HackerSwapChain::Present(%s@%p) called with\n", type_name(this), this);
	LogDebug("  SyncInterval = %d\n", SyncInterval);
	LogDebug("  Flags = %d\n", Flags);

	if (!(Flags & DXGI_PRESENT_TEST)) {
		// Profiling::mode may change below, so make a copy
		profiling = Profiling::mode == Profiling::Mode::SUMMARY;
		if (profiling)
			Profiling::start(&profiling_state);

		if (G->hunting == HUNTING_MODE_ENABLED) {
			if (G->overlay_buffer_hash_lifetime >= 0)
				PurgeStaleVisitedBufferHashes(mHackerDevice);
			if (G->mSelectedIndexBufferPos == INT_MAX) {
				G->mSelectedIndexBufferPos = G->mVisitedIndexBuffers.size() - 1;
				G->mSelectedIndexBuffer = *std::prev(G->mVisitedIndexBuffers.end());
			}
			if (G->mSelectedVertexBufferPos == INT_MAX) {
				G->mSelectedVertexBufferPos = G->mVisitedVertexBuffers.size() - 1;
				G->mSelectedVertexBuffer = *std::prev(G->mVisitedVertexBuffers.end());
			}
			if (G->gResetSelectedVertexBufferSlotId) {
				if (!G->mVisitedVertexBuffers.empty()) {
					G->mSelectedVertexBuffer = *G->mVisitedVertexBuffers.begin();
					G->mSelectedVertexBufferPos = 0;
					G->gResetSelectedVertexBufferSlotId = false;
				}
			}
		}

		if (G->track_region_hashes) {
			ClearRegionHashesGlobalCache();
		}

		// Every presented frame, we want to take some CPU time to run our actions,
		// which enables hunting, and snapshots, and aiming overrides and other inputs
		HackerSwapChain *frame_owner = mFrameActionOwner.load(std::memory_order_acquire);
		if (frame_owner)
			frame_owner->RunFrameActions();
		else
			RunFrameActions();

		SSMTBridge::NotifyPresent(
			mHackerDevice,
			mHackerContext,
			this,
			SyncInterval,
			Flags
		);
		if (HostedReShadeEnabled())
			gGimiHostedReShadeRuntime.Present(mOrigSwapChain1, mHackerContext);

		if (profiling)
			Profiling::end(&profiling_state, &Profiling::present_overhead);
	}

	get_tls()->hooking_quirk_protection = true; // Present may call D3D11CreateDevice, which we may have hooked
	HRESULT hr = mOrigSwapChain1->Present(SyncInterval, Flags);
	get_tls()->hooking_quirk_protection = false;

	if (!(Flags & DXGI_PRESENT_TEST)) {
		if (profiling)
			Profiling::start(&profiling_state);

		G->bb_is_upscaling_bb = !!G->SCREEN_UPSCALING && G->upscaling_command_list_using_explicit_bb_flip;

		// Run the post present command list now, which can be used to restore
		// state changed in the pre-present command list, or to perform some
		// action at the start of a frame:
		RunCommandList(mHackerDevice, mHackerContext, &G->post_present_command_list, NULL, true);

		if (profiling)
			Profiling::end(&profiling_state, &Profiling::present_overhead);
	}

	LogDebug("  returns %x\n", hr);
	return hr;
}

STDMETHODIMP HackerSwapChain::GetBuffer(THIS_
	/* [in] */ UINT Buffer,
	/* [annotation][in] */
	_In_  REFIID riid,
	/* [annotation][out][in] */
	_Out_  void **ppSurface)
{
	LogDebug("HackerSwapChain::GetBuffer(%s@%p) called with IID: %s\n", type_name(this), this, NameFromIID(riid).c_str());

	HRESULT hr = mOrigSwapChain1->GetBuffer(Buffer, riid, ppSurface);
	LogDebug("  returns %x\n", hr);
	return hr;
}

STDMETHODIMP HackerSwapChain::SetFullscreenState(THIS_
	/* [in] */ BOOL Fullscreen,
	/* [annotation][in] */
	_In_opt_  IDXGIOutput *pTarget)
{
	LogInfo("HackerSwapChain::SetFullscreenState(%s@%p) called with\n", type_name(this), this);
	LogInfo("  Fullscreen = %d\n", Fullscreen);
	LogInfo("  Target = %p\n", pTarget);

	if (G->SCREEN_FULLSCREEN > 0)
	{
		if (G->SCREEN_FULLSCREEN == 2) {
			// We install this hook on demand to avoid any possible
			// issues with hooking the call when we don't need it.
			// Unconfirmed, but possibly related to:
			// https://forums.geforce.com/default/topic/685657/3d-vision/3dmigoto-now-open-source-/post/4801159/#4801159
			InstallSetWindowPosHook();
		}

		Fullscreen = true;
		LogInfo("->Fullscreen forced = %d\n", Fullscreen);
	}

	//if (pTarget)	
	//	hr = mOrigSwapChain1->SetFullscreenState(Fullscreen, pTarget->m_pOutput);
	//else
	//	hr = mOrigSwapChain1->SetFullscreenState(Fullscreen, 0);

	if (Fullscreen)
		last_fullscreen_swap_chain = mOrigSwapChain1;

	HRESULT hr = mOrigSwapChain1->SetFullscreenState(Fullscreen, pTarget);
	LogInfo("  returns %x\n", hr);
	return hr;
}

STDMETHODIMP HackerSwapChain::GetFullscreenState(THIS_
	/* [annotation][out] */
	_Out_opt_  BOOL *pFullscreen,
	/* [annotation][out] */
	_Out_opt_  IDXGIOutput **ppTarget)
{
	LogDebug("HackerSwapChain::GetFullscreenState(%s@%p) called\n", type_name(this), this);

	//IDXGIOutput *origOutput;
	//HRESULT hr = mOrigSwapChain1->GetFullscreenState(pFullscreen, &origOutput);
	//if (hr == S_OK)
	//{
	//	*ppTarget = IDXGIOutput::GetDirectOutput(origOutput);
	//	if (pFullscreen) LogInfo("  returns Fullscreen = %d\n", *pFullscreen);
	//	if (ppTarget) LogInfo("  returns target IDXGIOutput = %x, wrapper = %x\n", origOutput, *ppTarget);
	//}

	HRESULT hr = mOrigSwapChain1->GetFullscreenState(pFullscreen, ppTarget);
	LogDebug("  returns result = %x\n", hr);
	return hr;
}

STDMETHODIMP HackerSwapChain::GetDesc(THIS_
	/* [annotation][out] */
	_Out_  DXGI_SWAP_CHAIN_DESC *pDesc)
{
	LogDebug("HackerSwapChain::GetDesc(%s@%p) called\n", type_name(this), this);

	HRESULT hr = mOrigSwapChain1->GetDesc(pDesc);

	if (hr == S_OK)
	{
		if (pDesc) LogDebug("  returns Windowed = %d\n", pDesc->Windowed);
		if (pDesc) LogDebug("  returns Width = %d\n", pDesc->BufferDesc.Width);
		if (pDesc) LogDebug("  returns Height = %d\n", pDesc->BufferDesc.Height);
		if (pDesc) LogDebug("  returns Refresh rate = %f\n",
			(float)pDesc->BufferDesc.RefreshRate.Numerator / (float)pDesc->BufferDesc.RefreshRate.Denominator);
	}

	LogDebug("  returns result = %x\n", hr);
	return hr;
}

STDMETHODIMP HackerSwapChain::ResizeBuffers(THIS_
	/* [in] */ UINT BufferCount,
	/* [in] */ UINT Width,
	/* [in] */ UINT Height,
	/* [in] */ DXGI_FORMAT NewFormat,
	/* [in] */ UINT SwapChainFlags)
{
	LogInfo("HackerSwapChain::ResizeBuffers(%s@%p) called\n", type_name(this), this);

	if (G->mResolutionInfo.from == GetResolutionFrom::SWAP_CHAIN)
	{
		G->mResolutionInfo.width = Width;
		G->mResolutionInfo.height = Height;
		LogInfo("  Got resolution from swap chain: %ix%i\n",
			G->mResolutionInfo.width, G->mResolutionInfo.height);
	}

	// ReShade caches views for the current back buffer. Tear down the hosted
	// runtime before the game recreates those buffers, then reinitialize lazily.
	if (HostedReShadeEnabled())
		gGimiHostedReShadeRuntime.Reset();

	// ReShade (and some other post-processing wrappers) may request an sRGB
	// format while the game is using the HDR R10G10B10A2 swap chain. Passing
	// that format through this wrapper makes DXGI reject the resize with
	// E_INVALIDARG. DXGI_FORMAT_UNKNOWN means "keep the existing format" for
	// ResizeBuffers, preserving the HDR swap-chain contract.
	DXGI_SWAP_CHAIN_DESC current_desc = {};
	if (SUCCEEDED(mOrigSwapChain1->GetDesc(&current_desc)) &&
		current_desc.BufferDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM)
	{
		if (NewFormat != DXGI_FORMAT_UNKNOWN && NewFormat != current_desc.BufferDesc.Format)
		{
			LogInfo("  Preserving HDR swap-chain format %d instead of requested format %d\n",
				static_cast<int>(current_desc.BufferDesc.Format), static_cast<int>(NewFormat));
			NewFormat = DXGI_FORMAT_UNKNOWN;
		}
		// Flip-model HDR swap chains require at least two buffers. ReShade's
		// resize path uses BufferCount=1, which is invalid for the original
		// chain, so retain its existing count as well.
		if (current_desc.BufferCount >= 2 && BufferCount != 0 && BufferCount < 2)
		{
			LogInfo("  Preserving HDR buffer count %u instead of requested count %u\n",
				current_desc.BufferCount, BufferCount);
			BufferCount = current_desc.BufferCount;
		}
	}

	HRESULT hr = mOrigSwapChain1->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);

	LogInfo("  returns result = %x\n", hr);
	return hr;
}

STDMETHODIMP HackerSwapChain::ResizeTarget(THIS_
	/* [annotation][in] */
	_In_  const DXGI_MODE_DESC *pNewTargetParameters)
{
	DXGI_MODE_DESC new_desc;

	LogInfo("HackerSwapChain::ResizeTarget(%s@%p) called\n", type_name(this), this);
	LogInfo("  Width: %d, Height: %d\n", pNewTargetParameters->Width, pNewTargetParameters->Height);
	LogInfo("     Refresh rate = %f\n",
		(float)pNewTargetParameters->RefreshRate.Numerator / (float)pNewTargetParameters->RefreshRate.Denominator);

	// Historically we have only forced the refresh rate when full-screen.
	// I don't know if we ever had a good reason for that, but it
	// complicates forcing the refresh rate in games that start windowed
	// and later switch to full screen, depending on the order in which
	// the game calls ResizeTarget and SetFullscreenState so now forcing it
	// unconditionally to see how that goes. Helps Unity games work with 3D
	// TV play. If we need to restore the old behaviour for some reason,
	// check the git history, but we will need further heroics.
	//
	// UE4 does SetFullscreenState -> ResizeBuffers -> ResizeTarget
	// Unity does ResizeTarget -> SetFullscreenState -> ResizeBuffers

	memcpy(&new_desc, pNewTargetParameters, sizeof(DXGI_MODE_DESC));
	ForceDisplayMode(&new_desc);

	HRESULT hr = mOrigSwapChain1->ResizeTarget(&new_desc);
	LogInfo("  returns result = %x\n", hr);
	return hr;
}

STDMETHODIMP HackerSwapChain::GetContainingOutput(THIS_
	/* [annotation][out] */
	_Out_  IDXGIOutput **ppOutput)
{
	LogInfo("HackerSwapChain::GetContainingOutput(%s@%p) called\n", type_name(this), this);
	HRESULT hr = mOrigSwapChain1->GetContainingOutput(ppOutput);
	LogInfo("  returns result = %#x\n", hr);
	return hr;
}

STDMETHODIMP HackerSwapChain::GetFrameStatistics(THIS_
	/* [annotation][out] */
	_Out_  DXGI_FRAME_STATISTICS *pStats)
{
	LogInfo("HackerSwapChain::GetFrameStatistics(%s@%p) called\n", type_name(this), this);
	HRESULT hr = mOrigSwapChain1->GetFrameStatistics(pStats);
	LogInfo("  returns result = %x\n", hr);
	return hr;
}

STDMETHODIMP HackerSwapChain::GetLastPresentCount(THIS_
	/* [annotation][out] */
	_Out_  UINT *pLastPresentCount)
{
	LogInfo("HackerSwapChain::GetLastPresentCount(%s@%p) called\n", type_name(this), this);
	HRESULT hr = mOrigSwapChain1->GetLastPresentCount(pLastPresentCount);
	LogInfo("  returns result = %x\n", hr);
	return hr;
}


// -----------------------------------------------------------------------------
/** IDXGISwapChain1 **/

// IDXGISwapChain1 requires platform update
// IDXGISwapChain2 requires Win8.1
// IDXGISwapChain3 requires Win10

STDMETHODIMP HackerSwapChain::GetDesc1(THIS_
	/* [annotation][out] */
	_Out_  DXGI_SWAP_CHAIN_DESC1 *pDesc)
{
	LogInfo("HackerSwapChain::GetDesc1(%s@%p) called\n", type_name(this), this);

	HRESULT hr = mOrigSwapChain1->GetDesc1(pDesc);
	if (hr == S_OK)
	{
		if (pDesc) LogInfo("  returns Stereo = %d\n", pDesc->Stereo);
		if (pDesc) LogInfo("  returns Width = %d\n", pDesc->Width);
		if (pDesc) LogInfo("  returns Height = %d\n", pDesc->Height);
	}
	LogInfo("  returns result = %x\n", hr);

	return hr;
}

STDMETHODIMP HackerSwapChain::GetFullscreenDesc(THIS_
	/* [annotation][out] */
	_Out_  DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pDesc)
{
	LogInfo("HackerSwapChain::GetFullscreenDesc(%s@%p) called\n", type_name(this), this);

	HRESULT hr = mOrigSwapChain1->GetFullscreenDesc(pDesc);
	if (hr == S_OK)
	{
		if (pDesc) LogInfo("  returns Windowed = %d\n", pDesc->Windowed);
		if (pDesc) LogInfo("  returns Refresh rate = %f\n",
			(float)pDesc->RefreshRate.Numerator / (float)pDesc->RefreshRate.Denominator);
	}
	LogInfo("  returns result = %x\n", hr);

	return hr;
}

STDMETHODIMP HackerSwapChain::GetHwnd(THIS_
	/* [annotation][out] */
	_Out_  HWND *pHwnd)
{
	LogInfo("HackerSwapChain::GetHwnd(%s@%p) called\n", type_name(this), this);
	HRESULT hr = mOrigSwapChain1->GetHwnd(pHwnd);
	LogInfo("  returns result = %x\n", hr);
	return hr;
}

STDMETHODIMP HackerSwapChain::GetCoreWindow(THIS_
	/* [annotation][in] */
	_In_  REFIID refiid,
	/* [annotation][out] */
	_Out_  void **ppUnk)
{
	LogInfo("HackerSwapChain::GetCoreWindow(%s@%p) called with IID: %s\n", type_name(this), this, NameFromIID(refiid).c_str());

	HRESULT hr = mOrigSwapChain1->GetCoreWindow(refiid, ppUnk);
	LogInfo("  returns result = %x\n", hr);
	return hr;
}


// IDXGISwapChain1 requires the platform update, but will be the default
// swap chain we build whenever possible.
//
// ToDo: never seen this in action.  Setting to always log.  Once we see
// it in action and works OK, remove the gLogDebug sets, because debug log
// is too chatty for Present calls.

STDMETHODIMP HackerSwapChain::Present1(THIS_
	/* [in] */ UINT SyncInterval,
	/* [in] */ UINT PresentFlags,
	/* [annotation][in] */
	_In_  const DXGI_PRESENT_PARAMETERS *pPresentParameters)
{
	Profiling::State profiling_state = {0};
	gLogDebug = true;
	bool profiling = false;

	LogDebug("HackerSwapChain::Present1(%s@%p) called\n", type_name(this), this);
	LogDebug("  SyncInterval = %d\n", SyncInterval);
	LogDebug("  Flags = %d\n", PresentFlags);

	if (!(PresentFlags & DXGI_PRESENT_TEST)) {
		// Profiling::mode may change below, so make a copy
		profiling = Profiling::mode == Profiling::Mode::SUMMARY;
		if (profiling)
			Profiling::start(&profiling_state);

		// Every presented frame, we want to take some CPU time to run our actions,
		// which enables hunting, and snapshots, and aiming overrides and other inputs
		HackerSwapChain *frame_owner = mFrameActionOwner.load(std::memory_order_acquire);
		if (frame_owner)
			frame_owner->RunFrameActions();
		else
			RunFrameActions();

		SSMTBridge::NotifyPresent(
			mHackerDevice,
			mHackerContext,
			this,
			SyncInterval,
			PresentFlags
		);
		if (HostedReShadeEnabled())
			gGimiHostedReShadeRuntime.Present(mOrigSwapChain1, mHackerContext);

		if (profiling)
			Profiling::end(&profiling_state, &Profiling::present_overhead);
	}

	get_tls()->hooking_quirk_protection = true; // Present may call D3D11CreateDevice, which we may have hooked
	HRESULT hr = mOrigSwapChain1->Present1(SyncInterval, PresentFlags, pPresentParameters);
	get_tls()->hooking_quirk_protection = false;

	if (!(PresentFlags & DXGI_PRESENT_TEST)) {
		if (profiling)
			Profiling::start(&profiling_state);

		G->bb_is_upscaling_bb = !!G->SCREEN_UPSCALING && G->upscaling_command_list_using_explicit_bb_flip;

		// Run the post present command list now, which can be used to restore
		// state changed in the pre-present command list, or to perform some
		// action at the start of a frame:
		RunCommandList(mHackerDevice, mHackerContext, &G->post_present_command_list, NULL, true);

		if (profiling)
			Profiling::end(&profiling_state, &Profiling::present_overhead);
	}

	LogDebug("  returns %x\n", hr);

	gLogDebug = false;
	return hr;
}

STDMETHODIMP_(BOOL) HackerSwapChain::IsTemporaryMonoSupported(THIS)
{
	LogInfo("HackerSwapChain::IsTemporaryMonoSupported(%s@%p) called\n", type_name(this), this);
	BOOL ret = mOrigSwapChain1->IsTemporaryMonoSupported();
	LogInfo("  returns %d\n", ret);
	return ret;
}

STDMETHODIMP HackerSwapChain::GetRestrictToOutput(THIS_
	/* [annotation][out] */
	_Out_  IDXGIOutput **ppRestrictToOutput)
{
	LogInfo("HackerSwapChain::GetRestrictToOutput(%s@%p) called\n", type_name(this), this);
	HRESULT hr = mOrigSwapChain1->GetRestrictToOutput(ppRestrictToOutput);
	LogInfo("  returns result = %x, handle = %p\n", hr, *ppRestrictToOutput);
	return hr;
}

STDMETHODIMP HackerSwapChain::SetBackgroundColor(THIS_
	/* [annotation][in] */
	_In_  const DXGI_RGBA *pColor)
{
	LogInfo("HackerSwapChain::SetBackgroundColor(%s@%p) called\n", type_name(this), this);
	HRESULT hr = mOrigSwapChain1->SetBackgroundColor(pColor);
	LogInfo("  returns result = %x\n", hr);
	return hr;
}

STDMETHODIMP HackerSwapChain::GetBackgroundColor(THIS_
	/* [annotation][out] */
	_Out_  DXGI_RGBA *pColor)
{
	LogInfo("HackerSwapChain::GetBackgroundColor(%s@%p) called\n", type_name(this), this);
	HRESULT hr = mOrigSwapChain1->GetBackgroundColor(pColor);
	LogInfo("  returns result = %x\n", hr);
	return hr;
}

STDMETHODIMP HackerSwapChain::SetRotation(THIS_
	/* [annotation][in] */
	_In_  DXGI_MODE_ROTATION Rotation)
{
	LogInfo("HackerSwapChain::SetRotation(%s@%p) called\n", type_name(this), this);
	HRESULT hr = mOrigSwapChain1->SetRotation(Rotation);
	LogInfo("  returns result = %x\n", hr);
	return hr;
}

STDMETHODIMP HackerSwapChain::GetRotation(THIS_
	/* [annotation][out] */
	_Out_  DXGI_MODE_ROTATION *pRotation)
{
	LogInfo("HackerSwapChain::GetRotation(%s@%p) called\n", type_name(this), this);
	HRESULT hr = mOrigSwapChain1->GetRotation(pRotation);
	LogInfo("  returns result = %x\n", hr);
	return hr;
}

STDMETHODIMP HackerSwapChain::SetSourceSize(UINT Width, UINT Height)
{
	return mOrigSwapChain3 ? mOrigSwapChain3->SetSourceSize(Width, Height) : E_NOINTERFACE;
}

STDMETHODIMP HackerSwapChain::GetSourceSize(UINT *pWidth, UINT *pHeight)
{
	return mOrigSwapChain3 ? mOrigSwapChain3->GetSourceSize(pWidth, pHeight) : E_NOINTERFACE;
}

STDMETHODIMP HackerSwapChain::SetMaximumFrameLatency(UINT MaxLatency)
{
	return mOrigSwapChain3 ? mOrigSwapChain3->SetMaximumFrameLatency(MaxLatency) : E_NOINTERFACE;
}

STDMETHODIMP HackerSwapChain::GetMaximumFrameLatency(UINT *pMaxLatency)
{
	return mOrigSwapChain3 ? mOrigSwapChain3->GetMaximumFrameLatency(pMaxLatency) : E_NOINTERFACE;
}

HANDLE STDMETHODCALLTYPE HackerSwapChain::GetFrameLatencyWaitableObject(void)
{
	return mOrigSwapChain3 ? mOrigSwapChain3->GetFrameLatencyWaitableObject() : nullptr;
}

STDMETHODIMP HackerSwapChain::SetMatrixTransform(const DXGI_MATRIX_3X2_F *pMatrix)
{
	return mOrigSwapChain3 ? mOrigSwapChain3->SetMatrixTransform(pMatrix) : E_NOINTERFACE;
}

STDMETHODIMP HackerSwapChain::GetMatrixTransform(DXGI_MATRIX_3X2_F *pMatrix)
{
	return mOrigSwapChain3 ? mOrigSwapChain3->GetMatrixTransform(pMatrix) : E_NOINTERFACE;
}

UINT STDMETHODCALLTYPE HackerSwapChain::GetCurrentBackBufferIndex(void)
{
	return mOrigSwapChain3 ? mOrigSwapChain3->GetCurrentBackBufferIndex() : 0;
}

STDMETHODIMP HackerSwapChain::CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace,
	UINT *pColorSpaceSupport)
{
	return mOrigSwapChain3 ? mOrigSwapChain3->CheckColorSpaceSupport(ColorSpace, pColorSpaceSupport) : E_NOINTERFACE;
}

STDMETHODIMP HackerSwapChain::SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace)
{
	return mOrigSwapChain3 ? mOrigSwapChain3->SetColorSpace1(ColorSpace) : E_NOINTERFACE;
}

STDMETHODIMP HackerSwapChain::ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height,
	DXGI_FORMAT Format, UINT SwapChainFlags, const UINT *pCreationNodeMask,
	IUnknown *const *ppPresentQueue)
{
	if (!mOrigSwapChain3)
		return E_NOINTERFACE;
	if (G->mResolutionInfo.from == GetResolutionFrom::SWAP_CHAIN) {
		G->mResolutionInfo.width = Width;
		G->mResolutionInfo.height = Height;
	}
	if (HostedReShadeEnabled())
		gGimiHostedReShadeRuntime.Reset();
	return mOrigSwapChain3->ResizeBuffers1(BufferCount, Width, Height, Format,
		SwapChainFlags, pCreationNodeMask, ppPresentQueue);
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------

// HackerUpscalingSwapChain, to provide post-process upscaling to arbitrary
// resolutions.  Particularly good for 4K passive 3D.

HackerUpscalingSwapChain::HackerUpscalingSwapChain(IDXGISwapChain1 *pSwapChain, HackerDevice *pHackerDevice, HackerContext *pHackerContext,
	DXGI_SWAP_CHAIN_DESC* pFakeSwapChainDesc, UINT newWidth, UINT newHeight)
	: HackerSwapChain(pSwapChain, pHackerDevice, pHackerContext),
	mFakeBackBuffer(nullptr), mFakeSwapChain1(nullptr), mWidth(0), mHeight(0)
{
	CreateRenderTarget(pFakeSwapChainDesc);

	mWidth = newWidth;
	mHeight = newHeight;
}


HackerUpscalingSwapChain::~HackerUpscalingSwapChain()
{
	if (mFakeSwapChain1)
		mFakeSwapChain1->Release();
	if (mFakeBackBuffer)
		mFakeBackBuffer->Release();
}

void HackerUpscalingSwapChain::CreateRenderTarget(DXGI_SWAP_CHAIN_DESC* pFakeSwapChainDesc)
{
	HRESULT hr;

	switch (G->UPSCALE_MODE)
	{
	case 0:
	{
		// TODO: multisampled swap chain
		// TODO: multiple buffers within one spaw chain
		// ==> in this case upscale_mode = 1 should be used at the moment
		D3D11_TEXTURE2D_DESC fake_buffer_desc;
		std::memset(&fake_buffer_desc, 0, sizeof(D3D11_TEXTURE2D_DESC));
		fake_buffer_desc.ArraySize = 1;
		fake_buffer_desc.MipLevels = 1;
		fake_buffer_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		fake_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
		fake_buffer_desc.SampleDesc.Count = 1;
		fake_buffer_desc.Format = pFakeSwapChainDesc->BufferDesc.Format;
		fake_buffer_desc.MiscFlags = 0;
		fake_buffer_desc.Width = pFakeSwapChainDesc->BufferDesc.Width;
		fake_buffer_desc.Height = pFakeSwapChainDesc->BufferDesc.Height;
		fake_buffer_desc.CPUAccessFlags = 0;

		LockResourceCreationMode();
		hr = mHackerDevice->GetPassThroughOrigDevice1()->CreateTexture2D(&fake_buffer_desc, nullptr, &mFakeBackBuffer);
		UnlockResourceCreationMode();
	}
	break;
	case 1:
	{
		IDXGIFactory *pFactory = nullptr;

		hr = mOrigSwapChain1->GetParent(IID_PPV_ARGS(&pFactory));
		if (FAILED(hr))
		{
			LogOverlay(LOG_DIRE, "HackerUpscalingSwapChain::createRenderTarget failed to get DXGIFactory\n");
			// Not positive if we will be able to get an overlay to
			// display the error, so also issue an audible warning:
			BeepFailure2();
			return;
		}
		const UINT flagBackup = pFakeSwapChainDesc->Flags;

		// fake swap chain should have no influence on window
		pFakeSwapChainDesc->Flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		IDXGISwapChain* swapChain;
		get_tls()->hooking_quirk_protection = true;
		pFactory->CreateSwapChain(mHackerDevice->GetPossiblyHookedOrigDevice1(), pFakeSwapChainDesc, &swapChain);
		get_tls()->hooking_quirk_protection = false;

		pFactory->Release();

		HRESULT res = swapChain->QueryInterface(IID_PPV_ARGS(&mFakeSwapChain1));
		if (SUCCEEDED(res))
			swapChain->Release();
		else
			mFakeSwapChain1 = reinterpret_cast<IDXGISwapChain1*>(swapChain);

		// restore old state in case fall back is required ToDo: Unlikely needed now.
		pFakeSwapChainDesc->Flags = flagBackup;
	}
	break;
	default:
		LogOverlay(LOG_DIRE, "*** HackerUpscalingSwapChain::HackerUpscalingSwapChain() failed ==> provided upscaling mode is not valid.\n");
		// Not positive if we will be able to get an overlay to
		// display the error, so also issue an audible warning:
		BeepFailure2();
		return;
	}

	LogInfo("HackerUpscalingSwapChain::HackerUpscalingSwapChain(): result %d\n", hr);

	if (FAILED(hr))
	{
		LogOverlay(LOG_DIRE, "*** HackerUpscalingSwapChain::HackerUpscalingSwapChain() failed\n");
		// Not positive if we will be able to get an overlay to
		// display the error, so also issue an audible warning:
		BeepFailure2();
	}
}

STDMETHODIMP HackerUpscalingSwapChain::GetBuffer(THIS_
	/* [in] */ UINT Buffer,
	/* [annotation][in] */
	_In_  REFIID riid,
	/* [annotation][out][in] */
	_Out_  void **ppSurface)
{
	LogDebug("HackerUpscalingSwapChain::GetBuffer(%s@%p) called with IID: %s\n", type_name(this), this, NameFromIID(riid).c_str());

	HRESULT hr = S_OK;

	// if upscaling is on give the game fake back buffer
	if (mFakeBackBuffer)
	{
		// Use QueryInterface on mFakeBackBuffer, which validates that
		// the requested interface is supported, that ppSurface is not
		// NULL, and bumps the refcount if successful:
		hr = mFakeBackBuffer->QueryInterface(riid, ppSurface);
	}
	else if (mFakeSwapChain1)
	{
		hr = mFakeSwapChain1->GetBuffer(Buffer, riid, ppSurface);
	}
	else
	{
		LogInfo("BUG: HackerUpscalingDXGISwapChain::GetBuffer(): Missing upscaling object\n");
		DoubleBeepExit();
	}

	LogDebug("  returns %x\n", hr);
	return hr;
}

STDMETHODIMP HackerUpscalingSwapChain::SetFullscreenState(THIS_
	/* [in] */ BOOL Fullscreen,
	/* [annotation][in] */
	_In_opt_  IDXGIOutput *pTarget)
{
	LogInfo("HackerUpscalingSwapChain::SetFullscreenState(%s@%p) called with\n", type_name(this), this);
	LogInfo("  Fullscreen = %d\n", Fullscreen);
	LogInfo("  Target = %p\n", pTarget);

	HRESULT hr;

	BOOL fullscreen_state = FALSE;
	IDXGIOutput *target = nullptr;
	mOrigSwapChain1->GetFullscreenState(&fullscreen_state, &target);

	if (target)
		target->Release();

	// dont call setfullscreenstate again to avoid starting mode switching and flooding winproc with unnecessary messages
	// can disable fullscreen mode somehow
	if (fullscreen_state && Fullscreen)
	{
		hr = S_OK;
	}
	else
	{
		if (G->SCREEN_UPSCALING == 2)
		{
			hr = mOrigSwapChain1->SetFullscreenState(TRUE, pTarget); // Witcher seems to require forcing the fullscreen
		}
		else
		{
			hr = mOrigSwapChain1->SetFullscreenState(Fullscreen, pTarget);
		}
	}

	LogInfo("  returns %x\n", hr);
	return hr;
}

STDMETHODIMP HackerUpscalingSwapChain::GetDesc(THIS_
	/* [annotation][out] */
	_Out_  DXGI_SWAP_CHAIN_DESC *pDesc)
{
	LogDebug("HackerUpscalingSwapChain::GetDesc(%s@%p) called\n", type_name(this), this);

	HRESULT hr = mOrigSwapChain1->GetDesc(pDesc);

	if (hr == S_OK)
	{
		if (pDesc)
		{
			//TODO: not sure whether the upscaled resolution or game resolution should be returned
			// all tested games did not use this function only migoto does
			// I let them be the game resolution at the moment
			if (mFakeBackBuffer)
			{
				D3D11_TEXTURE2D_DESC fd;
				mFakeBackBuffer->GetDesc(&fd);
				pDesc->BufferDesc.Width = fd.Width;
				pDesc->BufferDesc.Height = fd.Height;
				LogDebug("->Using fake SwapChain Sizes.\n");
			}

			if (mFakeSwapChain1)
			{
				hr = mFakeSwapChain1->GetDesc(pDesc);
			}
		}

		if (pDesc) LogDebug("  returns Windowed = %d\n", pDesc->Windowed);
		if (pDesc) LogDebug("  returns Width = %d\n", pDesc->BufferDesc.Width);
		if (pDesc) LogDebug("  returns Height = %d\n", pDesc->BufferDesc.Height);
		if (pDesc) LogDebug("  returns Refresh rate = %f\n",
			(float)pDesc->BufferDesc.RefreshRate.Numerator / (float)pDesc->BufferDesc.RefreshRate.Denominator);
	}
	LogDebug("  returns result = %x\n", hr);
	return hr;
}

STDMETHODIMP HackerUpscalingSwapChain::ResizeBuffers(THIS_
	/* [in] */ UINT BufferCount,
	/* [in] */ UINT Width,
	/* [in] */ UINT Height,
	/* [in] */ DXGI_FORMAT NewFormat,
	/* [in] */ UINT SwapChainFlags)
{
	LogInfo("HackerSwapChain::ResizeBuffers(%s@%p) called\n", type_name(this), this);

	// TODO: not sure if it belongs here, in the resize target function or in both
	// or maybe it is better to put it in the getviewport function?
	// Require in case the software mouse and upscaling are on at the same time
	G->GAME_INTERNAL_WIDTH = Width;
	G->GAME_INTERNAL_HEIGHT = Height;

	if (G->mResolutionInfo.from == GetResolutionFrom::SWAP_CHAIN)
	{
		G->mResolutionInfo.width = Width;
		G->mResolutionInfo.height = Height;
		LogInfo("Got resolution from swap chain: %ix%i\n",
			G->mResolutionInfo.width, G->mResolutionInfo.height);
	}

	HRESULT hr;

	if (mFakeBackBuffer) // UPSCALE_MODE 0
	{
		// TODO: need to consider the new code (G->gForceStereo == 2)
		// would my stuff work this way? i guess yes. What is with the games that are not calling resize buffer
		// just try to recreate texture with new game resolution
		// should be possible without any issues (texture just like the swap chain should not be used at this time point)

		D3D11_TEXTURE2D_DESC fd;
		mFakeBackBuffer->GetDesc(&fd);

		if (!(fd.Width == Width && fd.Height == Height))
		{
			mFakeBackBuffer->Release();

			fd.Width = Width;
			fd.Height = Height;
			fd.Format = NewFormat;
			// just recreate texture with new width and height
			LockResourceCreationMode();
			hr = mHackerDevice->GetPassThroughOrigDevice1()->CreateTexture2D(&fd, nullptr, &mFakeBackBuffer);
			UnlockResourceCreationMode();
		}
		else  // nothing to resize
			hr = S_OK;
	}
	else if (mFakeSwapChain1) // UPSCALE_MODE 1
	{
		// the last parameter have to be zero to avoid the influence of the faked swap chain on the resize target function 
		hr = mFakeSwapChain1->ResizeBuffers(BufferCount, Width, Height, NewFormat, 0);
	}
	else
	{
		LogInfo("BUG: HackerUpscalingSwapChain::ResizeBuffers(): Missing upscaling object\n");
		DoubleBeepExit();
	}

	LogInfo("  returns result = %x\n", hr);
	return hr;
}

STDMETHODIMP HackerUpscalingSwapChain::ResizeTarget(THIS_
	/* [annotation][in] */
	_In_  const DXGI_MODE_DESC *pNewTargetParameters)
{
	LogInfo("HackerUpscalingSwapChain::ResizeTarget(%s@%p) called\n", type_name(this), this);

	if (pNewTargetParameters != nullptr)
	{
		// TODO: not sure if it belongs here, in the resize buffers function or in both
		// or maybe it is better to put it in the getviewport function?
		// Require in case the software mouse and upscaling are on at the same time
		G->GAME_INTERNAL_WIDTH = pNewTargetParameters->Width;
		G->GAME_INTERNAL_HEIGHT = pNewTargetParameters->Height;
	}

	// Some games like Witcher seems to drop fullscreen everytime the resizetarget is called (original one)
	// Some other games seems to require the function 
	// I did it the way the faked texture mode (upscale_mode == 1) dont call resize target
	// the other mode does

	HRESULT hr;

	if (G->SCREEN_UPSCALING == 2)
	{
		DEVMODE dmScreenSettings;
		memset(&dmScreenSettings, 0, sizeof(dmScreenSettings));
		dmScreenSettings.dmSize = sizeof(dmScreenSettings);
		dmScreenSettings.dmPelsWidth = (unsigned long)mWidth;
		dmScreenSettings.dmPelsHeight = (unsigned long)mHeight;
		dmScreenSettings.dmBitsPerPel = 32;
		dmScreenSettings.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;

		// Change the display settings to full screen.
		LONG displ_chainge_res = ChangeDisplaySettingsEx(NULL, &dmScreenSettings, nullptr, CDS_FULLSCREEN, 0);
		hr = displ_chainge_res == 0 ? S_OK : DXGI_ERROR_INVALID_CALL;
	}
	else if (G->SCREEN_UPSCALING == 1)
	{
		DXGI_MODE_DESC md = *pNewTargetParameters;

		// force upscaled resolution
		md.Width = mWidth;
		md.Height = mHeight;

		// Temporarily disable the GetClientRect() hook since DirectX
		// itself will call that and we want it to get the real
		// resolution. Fixes upscaling in ARK: Survival Evolved
		G->upscaling_hooks_armed = false;
		hr = mOrigSwapChain1->ResizeTarget(&md);
		G->upscaling_hooks_armed = true;
	}

	LogInfo("  returns result = %x\n", hr);
	return hr;
}
