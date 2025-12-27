#include "dxgi_proxy.hpp"
#include "logger.hpp"
#include <iostream>
#include <string>
#include <map>

// Global flag to track factory lifetime
bool g_FactoryAlive = false;

// External globals from main.cpp
extern RECT g_TargetRect;
extern void UpdateTargetRect();

// Helper to compare LUIDs
struct LUIDComparator {
    bool operator()(const LUID& a, const LUID& b) const {
        if (a.HighPart != b.HighPart) return a.HighPart < b.HighPart;
        return a.LowPart < b.LowPart;
    }
};

// Static cache for wrapped adapters to prevent double-wrapping (keyed by LUID)
static std::map<LUID, ProxyDXGIAdapter*, LUIDComparator> g_AdapterCache;

// Fake output instance
static ProxyDXGIOutput* g_FakeOutput = nullptr;

// --- ProxyDXGIFactory ---

ProxyDXGIFactory::ProxyDXGIFactory(IDXGIFactory6* pFactory) : m_pFactory(pFactory), m_refCount(1) {
    Log("[LS_Windowed] ProxyDXGIFactory created.");
}

ProxyDXGIFactory::~ProxyDXGIFactory() {
    Log("[LS_Windowed] ProxyDXGIFactory::~ProxyDXGIFactory() called - about to release real factory");
    
    g_FactoryAlive = false; // Disable fake monitor injection
    
    // Clear the adapter cache when factory is destroyed
    Log("[LS_Windowed] Clearing adapter cache (size: %d)", g_AdapterCache.size());
    for (auto& pair : g_AdapterCache) {
        if (pair.second) {
            Log("[LS_Windowed] Releasing cached adapter from factory destructor");
            pair.second->Release(); // Release the extra reference we kept
        }
    }
    g_AdapterCache.clear();
    
    if (g_FakeOutput) {
        g_FakeOutput->Release();
        g_FakeOutput = nullptr;
    }

    if (m_pFactory) m_pFactory->Release();
    Log("[LS_Windowed] ProxyDXGIFactory destroyed.");
}

HRESULT ProxyDXGIFactory::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIFactory) || 
        riid == __uuidof(IDXGIFactory1) || riid == __uuidof(IDXGIFactory2) || riid == __uuidof(IDXGIFactory3) ||
        riid == __uuidof(IDXGIFactory4) || riid == __uuidof(IDXGIFactory5) || riid == __uuidof(IDXGIFactory6)) {
        *ppvObject = this;
        AddRef();
        return S_OK;
    }
    return m_pFactory->QueryInterface(riid, ppvObject);
}

ULONG ProxyDXGIFactory::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

ULONG ProxyDXGIFactory::Release() {
    ULONG ref = InterlockedDecrement(&m_refCount);
    if (ref == 0) delete this;
    return ref;
}

HRESULT ProxyDXGIFactory::SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) {
    return m_pFactory->SetPrivateData(Name, DataSize, pData);
}

HRESULT ProxyDXGIFactory::SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown) {
    return m_pFactory->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT ProxyDXGIFactory::GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) {
    return m_pFactory->GetPrivateData(Name, pDataSize, pData);
}

HRESULT ProxyDXGIFactory::GetParent(REFIID riid, void** ppParent) {
    return m_pFactory->GetParent(riid, ppParent);
}

HRESULT ProxyDXGIFactory::EnumAdapters(UINT Adapter, IDXGIAdapter** ppAdapter) {
    IDXGIAdapter* pRealAdapter = nullptr;
    HRESULT hr = m_pFactory->EnumAdapters(Adapter, &pRealAdapter);
    if (SUCCEEDED(hr)) {
        IDXGIAdapter4* pAdapter4 = nullptr;
        if (SUCCEEDED(pRealAdapter->QueryInterface(__uuidof(IDXGIAdapter4), (void**)&pAdapter4))) {
            // Get adapter LUID for stable caching
            DXGI_ADAPTER_DESC2 desc;
            pAdapter4->GetDesc2(&desc);
            LUID adapterLuid = desc.AdapterLuid;
            
            // Check if we already have a wrapper for this adapter
            auto it = g_AdapterCache.find(adapterLuid);
            if (it != g_AdapterCache.end()) {
                *ppAdapter = it->second;
                it->second->AddRef();
                pAdapter4->Release(); // Release our temp ref
                pRealAdapter->Release(); // Release original ref
                return S_OK;
            }
            
            // Create new wrapper
            ProxyDXGIAdapter* pProxy = new ProxyDXGIAdapter(pAdapter4, Adapter);
            pProxy->AddRef(); // Add ref for cache
            g_AdapterCache[adapterLuid] = pProxy;
            
            *ppAdapter = pProxy;
            pProxy->AddRef(); // Add ref for caller
            
            pRealAdapter->Release(); // Release original ref, proxy took ownership of pAdapter4
            return S_OK;
        }
        // Fallback
        *ppAdapter = pRealAdapter;
    }
    return hr;
}

HRESULT ProxyDXGIFactory::MakeWindowAssociation(HWND WindowHandle, UINT Flags) {
    return m_pFactory->MakeWindowAssociation(WindowHandle, Flags);
}

HRESULT ProxyDXGIFactory::GetWindowAssociation(HWND* pWindowHandle) {
    return m_pFactory->GetWindowAssociation(pWindowHandle);
}

HRESULT ProxyDXGIFactory::CreateSwapChain(IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain) {
    return m_pFactory->CreateSwapChain(pDevice, pDesc, ppSwapChain);
}

HRESULT ProxyDXGIFactory::CreateSoftwareAdapter(HMODULE Module, IDXGIAdapter** ppAdapter) {
    return m_pFactory->CreateSoftwareAdapter(Module, ppAdapter);
}

HRESULT ProxyDXGIFactory::EnumAdapters1(UINT Adapter, IDXGIAdapter1** ppAdapter) {
    // Redirect to EnumAdapters logic to ensure wrapping
    IDXGIAdapter* pAdapterBase = nullptr;
    HRESULT hr = this->EnumAdapters(Adapter, &pAdapterBase);
    if (SUCCEEDED(hr)) {
        hr = pAdapterBase->QueryInterface(__uuidof(IDXGIAdapter1), (void**)ppAdapter);
        pAdapterBase->Release();
    }
    return hr;
}

BOOL ProxyDXGIFactory::IsCurrent() {
    return m_pFactory->IsCurrent();
}

BOOL ProxyDXGIFactory::IsWindowedStereoEnabled() {
    return m_pFactory->IsWindowedStereoEnabled();
}

HRESULT ProxyDXGIFactory::CreateSwapChainForHwnd(IUnknown *pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) {
    return m_pFactory->CreateSwapChainForHwnd(pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
}

HRESULT ProxyDXGIFactory::CreateSwapChainForCoreWindow(IUnknown *pDevice, IUnknown *pWindow, const DXGI_SWAP_CHAIN_DESC1 *pDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) {
    return m_pFactory->CreateSwapChainForCoreWindow(pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
}

HRESULT ProxyDXGIFactory::GetSharedResourceAdapterLuid(HANDLE hResource, LUID *pLuid) {
    return m_pFactory->GetSharedResourceAdapterLuid(hResource, pLuid);
}

HRESULT ProxyDXGIFactory::RegisterStereoStatusWindow(HWND WindowHandle, UINT wMsg, DWORD *pdwCookie) {
    return m_pFactory->RegisterStereoStatusWindow(WindowHandle, wMsg, pdwCookie);
}

HRESULT ProxyDXGIFactory::RegisterStereoStatusEvent(HANDLE hEvent, DWORD *pdwCookie) {
    return m_pFactory->RegisterStereoStatusEvent(hEvent, pdwCookie);
}

void ProxyDXGIFactory::UnregisterStereoStatus(DWORD dwCookie) {
    m_pFactory->UnregisterStereoStatus(dwCookie);
}

HRESULT ProxyDXGIFactory::RegisterOcclusionStatusWindow(HWND WindowHandle, UINT wMsg, DWORD *pdwCookie) {
    return m_pFactory->RegisterOcclusionStatusWindow(WindowHandle, wMsg, pdwCookie);
}

HRESULT ProxyDXGIFactory::RegisterOcclusionStatusEvent(HANDLE hEvent, DWORD *pdwCookie) {
    return m_pFactory->RegisterOcclusionStatusEvent(hEvent, pdwCookie);
}

void ProxyDXGIFactory::UnregisterOcclusionStatus(DWORD dwCookie) {
    m_pFactory->UnregisterOcclusionStatus(dwCookie);
}

HRESULT ProxyDXGIFactory::CreateSwapChainForComposition(IUnknown *pDevice, const DXGI_SWAP_CHAIN_DESC1 *pDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) {
    return m_pFactory->CreateSwapChainForComposition(pDevice, pDesc, pRestrictToOutput, ppSwapChain);
}

UINT ProxyDXGIFactory::GetCreationFlags() {
    return m_pFactory->GetCreationFlags();
}

HRESULT ProxyDXGIFactory::EnumAdapterByLuid(LUID AdapterLuid, REFIID riid, void **ppvAdapter) {
    return m_pFactory->EnumAdapterByLuid(AdapterLuid, riid, ppvAdapter);
}

HRESULT ProxyDXGIFactory::EnumWarpAdapter(REFIID riid, void **ppvAdapter) {
    return m_pFactory->EnumWarpAdapter(riid, ppvAdapter);
}

HRESULT ProxyDXGIFactory::CheckFeatureSupport(DXGI_FEATURE Feature, void *pFeatureSupportData, UINT FeatureSupportDataSize) {
    return m_pFactory->CheckFeatureSupport(Feature, pFeatureSupportData, FeatureSupportDataSize);
}

HRESULT ProxyDXGIFactory::EnumAdapterByGpuPreference(UINT Adapter, DXGI_GPU_PREFERENCE GpuPreference, REFIID riid, void **ppvAdapter) {
    return m_pFactory->EnumAdapterByGpuPreference(Adapter, GpuPreference, riid, ppvAdapter);
}

// --- ProxyDXGIAdapter ---

ProxyDXGIAdapter::ProxyDXGIAdapter(IDXGIAdapter4* pAdapter, UINT index) : m_pAdapter(pAdapter), m_refCount(1), m_adapterIndex(index) {
    Log("[LS_Windowed] ProxyDXGIAdapter created for index %d", index);
}

ProxyDXGIAdapter::~ProxyDXGIAdapter() {
    Log("[LS_Windowed] ProxyDXGIAdapter::~ProxyDXGIAdapter() called for index %d", m_adapterIndex);
    if (m_pAdapter) {
        m_pAdapter->Release();
    }
}

HRESULT ProxyDXGIAdapter::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIAdapter) || 
        riid == __uuidof(IDXGIAdapter1) || riid == __uuidof(IDXGIAdapter2) || riid == __uuidof(IDXGIAdapter3) ||
        riid == __uuidof(IDXGIAdapter4)) {
        *ppvObject = this;
        AddRef();
        return S_OK;
    }
    return m_pAdapter->QueryInterface(riid, ppvObject);
}

ULONG ProxyDXGIAdapter::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

ULONG ProxyDXGIAdapter::Release() {
    ULONG ref = InterlockedDecrement(&m_refCount);
    if (ref == 0) delete this;
    return ref;
}

HRESULT ProxyDXGIAdapter::SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) {
    return m_pAdapter->SetPrivateData(Name, DataSize, pData);
}

HRESULT ProxyDXGIAdapter::SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown) {
    return m_pAdapter->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT ProxyDXGIAdapter::GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) {
    return m_pAdapter->GetPrivateData(Name, pDataSize, pData);
}

HRESULT ProxyDXGIAdapter::GetParent(REFIID riid, void** ppParent) {
    return m_pAdapter->GetParent(riid, ppParent);
}

HRESULT ProxyDXGIAdapter::EnumOutputs(UINT Output, IDXGIOutput** ppOutput) {
    IDXGIOutput* pRealOutput = nullptr;
    HRESULT hr = m_pAdapter->EnumOutputs(Output, &pRealOutput);
    
    if (hr == DXGI_ERROR_NOT_FOUND) {
        if (m_adapterIndex == 0) {
            bool isNextSlot = false;
            if (Output == 0) {
                isNextSlot = true;
            } else {
                IDXGIOutput* pPrev = nullptr;
                if (SUCCEEDED(m_pAdapter->EnumOutputs(Output - 1, &pPrev))) {
                    pPrev->Release();
                    isNextSlot = true;
                }
            }
            
            if (isNextSlot) {
                Log("[LS_Windowed] Injecting Fake DXGI Output at index %d", Output);
                // Create or reuse the fake output
                if (!g_FakeOutput) {
                    g_FakeOutput = new ProxyDXGIOutput(nullptr, true);
                    g_FakeOutput->AddRef(); // Keep an extra reference so it never gets destroyed
                }
                *ppOutput = g_FakeOutput;
                g_FakeOutput->AddRef(); // Add reference for the caller
                return S_OK;
            }
        }
        return hr;
    }
    
    if (SUCCEEDED(hr)) {
        IDXGIOutput6* pOutput6 = nullptr;
        if (SUCCEEDED(pRealOutput->QueryInterface(__uuidof(IDXGIOutput6), (void**)&pOutput6))) {
            *ppOutput = new ProxyDXGIOutput(pOutput6, false);
            pRealOutput->Release();
        } else {
             *ppOutput = pRealOutput;
        }
    }
    
    return hr;
}

HRESULT ProxyDXGIAdapter::GetDesc(DXGI_ADAPTER_DESC* pDesc) {
    return m_pAdapter->GetDesc(pDesc);
}

HRESULT ProxyDXGIAdapter::CheckInterfaceSupport(REFGUID InterfaceName, LARGE_INTEGER* pUMDVersion) {
    return m_pAdapter->CheckInterfaceSupport(InterfaceName, pUMDVersion);
}

HRESULT ProxyDXGIAdapter::GetDesc1(DXGI_ADAPTER_DESC1* pDesc) {
    return m_pAdapter->GetDesc1(pDesc);
}

HRESULT ProxyDXGIAdapter::GetDesc2(DXGI_ADAPTER_DESC2* pDesc) {
    return m_pAdapter->GetDesc2(pDesc);
}

HRESULT ProxyDXGIAdapter::RegisterHardwareContentProtectionTeardownStatusEvent(HANDLE hEvent, DWORD *pdwCookie) {
    return m_pAdapter->RegisterHardwareContentProtectionTeardownStatusEvent(hEvent, pdwCookie);
}

void ProxyDXGIAdapter::UnregisterHardwareContentProtectionTeardownStatus(DWORD dwCookie) {
    m_pAdapter->UnregisterHardwareContentProtectionTeardownStatus(dwCookie);
}

HRESULT ProxyDXGIAdapter::QueryVideoMemoryInfo(UINT NodeIndex, DXGI_MEMORY_SEGMENT_GROUP MemorySegmentGroup, DXGI_QUERY_VIDEO_MEMORY_INFO *pVideoMemoryInfo) {
    return m_pAdapter->QueryVideoMemoryInfo(NodeIndex, MemorySegmentGroup, pVideoMemoryInfo);
}

HRESULT ProxyDXGIAdapter::SetVideoMemoryReservation(UINT NodeIndex, DXGI_MEMORY_SEGMENT_GROUP MemorySegmentGroup, UINT64 Reservation) {
    return m_pAdapter->SetVideoMemoryReservation(NodeIndex, MemorySegmentGroup, Reservation);
}

HRESULT ProxyDXGIAdapter::RegisterVideoMemoryBudgetChangeNotificationEvent(HANDLE hEvent, DWORD *pdwCookie) {
    return m_pAdapter->RegisterVideoMemoryBudgetChangeNotificationEvent(hEvent, pdwCookie);
}

void ProxyDXGIAdapter::UnregisterVideoMemoryBudgetChangeNotification(DWORD dwCookie) {
    m_pAdapter->UnregisterVideoMemoryBudgetChangeNotification(dwCookie);
}

HRESULT ProxyDXGIAdapter::GetDesc3(DXGI_ADAPTER_DESC3 *pDesc) {
    return m_pAdapter->GetDesc3(pDesc);
}

// --- ProxyDXGIOutput ---

ProxyDXGIOutput::ProxyDXGIOutput(IDXGIOutput6* pOutput, bool isFake) : m_pOutput(pOutput), m_refCount(1), m_isFake(isFake) {
    if (m_isFake) {
        Log("[LS_Windowed] ProxyDXGIOutput (FAKE) created.");
    }
}

ProxyDXGIOutput::~ProxyDXGIOutput() {
    if (m_isFake) {
        Log("[LS_Windowed] ProxyDXGIOutput (FAKE) destroyed.");
    }
    if (m_pOutput) m_pOutput->Release();
}

HRESULT ProxyDXGIOutput::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIOutput) || 
        riid == __uuidof(IDXGIOutput1) || riid == __uuidof(IDXGIOutput2) || riid == __uuidof(IDXGIOutput3) ||
        riid == __uuidof(IDXGIOutput4) || riid == __uuidof(IDXGIOutput5) || riid == __uuidof(IDXGIOutput6)) {
        *ppvObject = this;
        AddRef();
        return S_OK;
    }
    if (m_isFake) {
        return E_NOINTERFACE;
    }
    if (m_pOutput) return m_pOutput->QueryInterface(riid, ppvObject);
    return E_NOINTERFACE;
}

ULONG ProxyDXGIOutput::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

ULONG ProxyDXGIOutput::Release() {
    ULONG ref = InterlockedDecrement(&m_refCount);
    if (ref == 0) delete this;
    return ref;
}

HRESULT ProxyDXGIOutput::SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) {
    if (m_isFake) return S_OK;
    return m_pOutput->SetPrivateData(Name, DataSize, pData);
}

HRESULT ProxyDXGIOutput::SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown) {
    if (m_isFake) return S_OK;
    return m_pOutput->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT ProxyDXGIOutput::GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) {
    if (m_isFake) return DXGI_ERROR_NOT_FOUND;
    return m_pOutput->GetPrivateData(Name, pDataSize, pData);
}

HRESULT ProxyDXGIOutput::GetParent(REFIID riid, void** ppParent) {
    if (m_isFake) return E_NOTIMPL;
    return m_pOutput->GetParent(riid, ppParent);
}

HRESULT ProxyDXGIOutput::GetDesc(DXGI_OUTPUT_DESC* pDesc) {
    if (m_isFake) {
        if (!pDesc) return E_INVALIDARG;
        wcscpy_s(pDesc->DeviceName, 32, L"\\\\.\\DISPLAY_VIRTUAL");
        
        UpdateTargetRect(); // Update target rect before returning
        
        pDesc->DesktopCoordinates = g_TargetRect;
        
        pDesc->AttachedToDesktop = TRUE;
        pDesc->Rotation = DXGI_MODE_ROTATION_IDENTITY;
        pDesc->Monitor = (HMONITOR)0xBADF00D; 
        Log("[LS_Windowed] ProxyDXGIOutput::GetDesc (FAKE) returning %dx%d @ (%d,%d)", 
            g_TargetRect.right - g_TargetRect.left, g_TargetRect.bottom - g_TargetRect.top,
            g_TargetRect.left, g_TargetRect.top);
        return S_OK;
    }
    return m_pOutput->GetDesc(pDesc);
}

HRESULT ProxyDXGIOutput::GetDisplayModeList(DXGI_FORMAT EnumFormat, UINT Flags, UINT* pNumModes, DXGI_MODE_DESC* pDesc) {
    if (m_isFake) {
        if (!pNumModes) return E_INVALIDARG;
        if (!pDesc) {
            *pNumModes = 1;
            return S_OK;
        }
        if (*pNumModes < 1) return DXGI_ERROR_MORE_DATA;
        
        UpdateTargetRect();

        pDesc[0].Width = g_TargetRect.right - g_TargetRect.left;
        pDesc[0].Height = g_TargetRect.bottom - g_TargetRect.top;
        pDesc[0].RefreshRate.Numerator = 60; // Default to 60Hz
        pDesc[0].RefreshRate.Denominator = 1;
        pDesc[0].Format = EnumFormat; 
        pDesc[0].ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
        pDesc[0].Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
        
        *pNumModes = 1;
        return S_OK;
    }
    return m_pOutput->GetDisplayModeList(EnumFormat, Flags, pNumModes, pDesc);
}

HRESULT ProxyDXGIOutput::FindClosestMatchingMode(const DXGI_MODE_DESC* pModeToMatch, DXGI_MODE_DESC* pClosestMatch, IUnknown* pConcernedDevice) {
    if (m_isFake) {
        if (!pClosestMatch) return E_INVALIDARG;
        if (pModeToMatch) *pClosestMatch = *pModeToMatch;
        return S_OK;
    }
    return m_pOutput->FindClosestMatchingMode(pModeToMatch, pClosestMatch, pConcernedDevice);
}

HRESULT ProxyDXGIOutput::WaitForVBlank() {
    if (m_isFake) {
        Sleep(16); 
        return S_OK;
    }
    return m_pOutput->WaitForVBlank();
}

HRESULT ProxyDXGIOutput::TakeOwnership(IUnknown* pDevice, BOOL Exclusive) {
    if (m_isFake) return S_OK;
    return m_pOutput->TakeOwnership(pDevice, Exclusive);
}

void ProxyDXGIOutput::ReleaseOwnership() {
    if (m_isFake) return;
    m_pOutput->ReleaseOwnership();
}

HRESULT ProxyDXGIOutput::GetGammaControlCapabilities(DXGI_GAMMA_CONTROL_CAPABILITIES* pGammaCaps) {
    if (m_isFake) return E_NOTIMPL;
    return m_pOutput->GetGammaControlCapabilities(pGammaCaps);
}

HRESULT ProxyDXGIOutput::SetGammaControl(const DXGI_GAMMA_CONTROL* pArray) {
    if (m_isFake) return S_OK;
    return m_pOutput->SetGammaControl(pArray);
}

HRESULT ProxyDXGIOutput::GetGammaControl(DXGI_GAMMA_CONTROL* pArray) {
    if (m_isFake) return E_NOTIMPL;
    return m_pOutput->GetGammaControl(pArray);
}

HRESULT ProxyDXGIOutput::SetDisplaySurface(IDXGISurface* pScanoutSurface) {
    if (m_isFake) return S_OK;
    return m_pOutput->SetDisplaySurface(pScanoutSurface);
}

HRESULT ProxyDXGIOutput::GetDisplaySurfaceData(IDXGISurface* pDestination) {
    if (m_isFake) return S_OK;
    return m_pOutput->GetDisplaySurfaceData(pDestination);
}

HRESULT ProxyDXGIOutput::GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats) {
    if (m_isFake) return E_NOTIMPL;
    return m_pOutput->GetFrameStatistics(pStats);
}

HRESULT ProxyDXGIOutput::GetDisplayModeList1(DXGI_FORMAT EnumFormat, UINT Flags, UINT* pNumModes, DXGI_MODE_DESC1* pDesc) {
    if (m_isFake) {
        if (!pNumModes) return E_INVALIDARG;
        if (!pDesc) {
            *pNumModes = 1;
            return S_OK;
        }
        if (*pNumModes < 1) return DXGI_ERROR_MORE_DATA;
        
        UpdateTargetRect();

        pDesc[0].Width = g_TargetRect.right - g_TargetRect.left;
        pDesc[0].Height = g_TargetRect.bottom - g_TargetRect.top;
        pDesc[0].RefreshRate.Numerator = 60;
        pDesc[0].RefreshRate.Denominator = 1;
        pDesc[0].Format = EnumFormat; 
        pDesc[0].ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
        pDesc[0].Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
        pDesc[0].Stereo = FALSE; 
        
        *pNumModes = 1;
        return S_OK;
    }
    return m_pOutput->GetDisplayModeList1(EnumFormat, Flags, pNumModes, pDesc);
}

HRESULT ProxyDXGIOutput::FindClosestMatchingMode1(const DXGI_MODE_DESC1* pModeToMatch, DXGI_MODE_DESC1* pClosestMatch, IUnknown* pConcernedDevice) {
    if (m_isFake) {
        if (!pClosestMatch) return E_INVALIDARG;
        if (pModeToMatch) *pClosestMatch = *pModeToMatch;
        return S_OK;
    }
    return m_pOutput->FindClosestMatchingMode1(pModeToMatch, pClosestMatch, pConcernedDevice);
}

HRESULT ProxyDXGIOutput::GetDisplaySurfaceData1(IDXGIResource* pDestination) {
    if (m_isFake) return S_OK;
    return m_pOutput->GetDisplaySurfaceData1(pDestination);
}

HRESULT ProxyDXGIOutput::DuplicateOutput(IUnknown* pDevice, IDXGIOutputDuplication** ppOutputDuplication) {
    if (m_isFake) return DXGI_ERROR_UNSUPPORTED;
    return m_pOutput->DuplicateOutput(pDevice, ppOutputDuplication);
}

BOOL ProxyDXGIOutput::SupportsOverlays() {
    if (m_isFake) return FALSE;
    return m_pOutput->SupportsOverlays();
}

HRESULT ProxyDXGIOutput::CheckOverlaySupport(DXGI_FORMAT EnumFormat, IUnknown *pConcernedDevice, UINT *pFlags) {
    if (m_isFake) return E_NOTIMPL;
    return m_pOutput->CheckOverlaySupport(EnumFormat, pConcernedDevice, pFlags);
}

HRESULT ProxyDXGIOutput::CheckOverlayColorSpaceSupport(DXGI_FORMAT Format, DXGI_COLOR_SPACE_TYPE ColorSpace, IUnknown *pConcernedDevice, UINT *pFlags) {
    if (m_isFake) return E_NOTIMPL;
    return m_pOutput->CheckOverlayColorSpaceSupport(Format, ColorSpace, pConcernedDevice, pFlags);
}

HRESULT ProxyDXGIOutput::DuplicateOutput1(IUnknown *pDevice, UINT Flags, UINT SupportedFormatsCount, const DXGI_FORMAT *pSupportedFormats, IDXGIOutputDuplication **ppOutputDuplication) {
    if (m_isFake) return DXGI_ERROR_UNSUPPORTED;
    return m_pOutput->DuplicateOutput1(pDevice, Flags, SupportedFormatsCount, pSupportedFormats, ppOutputDuplication);
}

HRESULT ProxyDXGIOutput::GetDesc1(DXGI_OUTPUT_DESC1 *pDesc) {
    if (m_isFake) {
        if (!pDesc) return E_INVALIDARG;
        wcscpy_s(pDesc->DeviceName, 32, L"\\\\.\\DISPLAY_VIRTUAL");
        
        UpdateTargetRect();

        pDesc->DesktopCoordinates = g_TargetRect;
        
        pDesc->AttachedToDesktop = TRUE;
        pDesc->Rotation = DXGI_MODE_ROTATION_IDENTITY;
        pDesc->Monitor = (HMONITOR)0xBADF00D;
        pDesc->BitsPerColor = 8;
        pDesc->ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        pDesc->RedPrimary[0] = 0.64f; pDesc->RedPrimary[1] = 0.33f;
        pDesc->GreenPrimary[0] = 0.30f; pDesc->GreenPrimary[1] = 0.60f;
        pDesc->BluePrimary[0] = 0.15f; pDesc->BluePrimary[1] = 0.06f;
        pDesc->WhitePoint[0] = 0.3127f; pDesc->WhitePoint[1] = 0.3290f;
        pDesc->MinLuminance = 0.0f;
        pDesc->MaxLuminance = 100.0f;
        pDesc->MaxFullFrameLuminance = 100.0f;
        return S_OK;
    }
    return m_pOutput->GetDesc1(pDesc);
}

HRESULT ProxyDXGIOutput::CheckHardwareCompositionSupport(UINT *pFlags) {
    if (m_isFake) return E_NOTIMPL;
    return m_pOutput->CheckHardwareCompositionSupport(pFlags);
}
