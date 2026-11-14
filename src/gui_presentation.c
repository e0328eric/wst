/* Keep std/Gui's Direct3D renderer on a DWM-redirection surface: HWND opacity
   is ignored by flip-model swapchains. This app-local import adapter changes
   only presentation setup; std/Gui still owns drawing and the swapchain.
   Remove it when Gui exposes an opacity-compatible presentation option. */
#define COBJMACROS
#define D3D11CreateDeviceAndSwapChain sdk_D3D11CreateDeviceAndSwapChain
#include <d3d11.h>
#undef D3D11CreateDeviceAndSwapChain

typedef HRESULT (WINAPI *CreateDeviceAndSwapChain)(
    IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL *, UINT, UINT, const DXGI_SWAP_CHAIN_DESC *,
    IDXGISwapChain **, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

static HWND presentation_window;
static int presentation_composed;

HRESULT WINAPI D3D11CreateDeviceAndSwapChain(
    IDXGIAdapter *adapter, D3D_DRIVER_TYPE type, HMODULE software, UINT flags,
    const D3D_FEATURE_LEVEL *levels, UINT count, UINT sdk,
    const DXGI_SWAP_CHAIN_DESC *description, IDXGISwapChain **swapchain,
    ID3D11Device **device, D3D_FEATURE_LEVEL *chosen, ID3D11DeviceContext **context)
{
    static CreateDeviceAndSwapChain create;
    if (!create) {
        HMODULE library = LoadLibraryExW(L"d3d11.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!library) return HRESULT_FROM_WIN32(GetLastError());
        create = (CreateDeviceAndSwapChain)GetProcAddress(library, "D3D11CreateDeviceAndSwapChain");
        if (!create) {
            DWORD error = GetLastError();
            FreeLibrary(library);
            return HRESULT_FROM_WIN32(error);
        }
        /* Retain the system runtime for the lifetime of its devices. */
    }
    DXGI_SWAP_CHAIN_DESC composed;
    const DXGI_SWAP_CHAIN_DESC *selected = description;
    if (description && description->Windowed && description->OutputWindow) {
        composed = *description;
        composed.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        composed.BufferCount = 1;
        selected = &composed;
    }
    HRESULT status = create(adapter, type, software, flags, levels, count, sdk,
        selected, swapchain, device, chosen, context);
    if (SUCCEEDED(status) && selected && swapchain && *swapchain) {
        DXGI_SWAP_CHAIN_DESC actual;
        presentation_window = selected->OutputWindow;
        presentation_composed = SUCCEEDED(IDXGISwapChain_GetDesc(*swapchain, &actual)) &&
            actual.SwapEffect == DXGI_SWAP_EFFECT_DISCARD;
    }
    return status;
}

/* std/Gui calls the SDK's dllimport symbol. Resolve that import within this
   executable, without patching its code or changing the installed runtime. */
CreateDeviceAndSwapChain __imp_D3D11CreateDeviceAndSwapChain = D3D11CreateDeviceAndSwapChain;

int wst_gui_presentation_composed(HWND window)
{
    return window && window == presentation_window && presentation_composed;
}
