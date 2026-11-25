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

static const WCHAR composed_property[] = L"wst.DwmComposedPresentation";
static char adapter_name[128 * 4];

static void record_adapter(ID3D11Device *device)
{
    IDXGIDevice *dxgi_device;
    IDXGIAdapter *adapter;
    DXGI_ADAPTER_DESC desc;

    adapter_name[0] = '\0';
    if (FAILED(ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgi_device))) return;
    if (SUCCEEDED(IDXGIDevice_GetAdapter(dxgi_device, &adapter))) {
        if (SUCCEEDED(IDXGIAdapter_GetDesc(adapter, &desc))) {
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, adapter_name,
                sizeof(adapter_name), NULL, NULL);
        }
        IDXGIAdapter_Release(adapter);
    }
    IDXGIDevice_Release(dxgi_device);
}

/* std/Gui streams each glyph's coverage runs through one dynamic vertex
   buffer, rewritten with WRITE_DISCARD many times per frame. The driver keeps
   every renamed copy alive while its frame is queued; on integrated GPUs those
   copies are system memory charged to this process, and a pool grown while
   the GPU was busy is never trimmed. Queue one frame instead of DXGI's three. */
static void limit_frame_latency(ID3D11Device *device)
{
    IDXGIDevice1 *dxgi_device;

    if (FAILED(ID3D11Device_QueryInterface(device, &IID_IDXGIDevice1, (void **)&dxgi_device))) return;
    IDXGIDevice1_SetMaximumFrameLatency(dxgi_device, 1);
    IDXGIDevice1_Release(dxgi_device);
}

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
    if (SUCCEEDED(status) && device && *device) {
        record_adapter(*device);
        limit_frame_latency(*device);
    }
    if (SUCCEEDED(status) && selected && selected->OutputWindow && swapchain && *swapchain) {
        DXGI_SWAP_CHAIN_DESC actual;
        if (SUCCEEDED(IDXGISwapChain_GetDesc(*swapchain, &actual)) &&
            actual.SwapEffect == DXGI_SWAP_EFFECT_DISCARD)
            SetPropW(selected->OutputWindow, composed_property, (HANDLE)(ULONG_PTR)1);
        else
            RemovePropW(selected->OutputWindow, composed_property);
    }
    return status;
}

/* std/Gui calls the SDK's dllimport symbol. Resolve that import within this
   executable, without patching its code or changing the installed runtime. */
CreateDeviceAndSwapChain __imp_D3D11CreateDeviceAndSwapChain = D3D11CreateDeviceAndSwapChain;

int wst_gui_presentation_composed(HWND window)
{
    return window && GetPropW(window, composed_property) != NULL;
}

void wst_gui_presentation_forget(HWND window)
{
    RemovePropW(window, composed_property);
}

const char *wst_gui_adapter_name(void)
{
    return adapter_name[0] ? adapter_name : "unavailable";
}
