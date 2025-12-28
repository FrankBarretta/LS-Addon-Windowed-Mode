#include "dxgi_proxy.hpp"
#include "logger.hpp"
#include <MinHook.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <windows.h>

#include "../../../LosslessProxy/src/addon_api.hpp"
#include "imgui.h"

#include <mutex>

// Global variables
ImGuiContext* g_ImGuiContext = nullptr;
std::mutex g_StateMutex;

// Settings
struct Settings {
  bool SplitMode = false;
  int SplitType = 0; // 0: Left, 1: Right, 2: Top, 3: Bottom
  bool PositionMode = false;
  int PositionSide = 1; // 0: Left, 1: Right, 2: Top, 3: Bottom
};

Settings g_Settings;

// Fake Monitor Handle
const HMONITOR FAKE_VIRTUAL_MONITOR = (HMONITOR)0xBADF00D;
const wchar_t *FAKE_MONITOR_NAME = L"\\\\.\\DISPLAY_WINDOWED";
const wchar_t *FAKE_MONITOR_DEVICE = L"Windowed Mode";

// Target Window Info
RECT g_TargetRect = {0, 0, 1920, 1080}; // Default
RECT g_LSRect = {0, 0, 1920, 1080}; // Position for LS window
HWND g_hTargetWindow = nullptr;

void UpdateTargetRect() {
  HWND hForeground = GetForegroundWindow();
  if (!hForeground)
    return;

  DWORD pid;
  GetWindowThreadProcessId(hForeground, &pid);
  if (pid == GetCurrentProcessId())
    return; // It's us (LS)

  // It's another app. Assume it's the target.
  // Use GetClientRect + ClientToScreen to get the content area, excluding title
  // bar/borders
  RECT rcClient;
  if (GetClientRect(hForeground, &rcClient)) {
    POINT tl = {rcClient.left, rcClient.top};
    POINT br = {rcClient.right, rcClient.bottom};

    ClientToScreen(hForeground, &tl);
    ClientToScreen(hForeground, &br);

    RECT rcScreen = {tl.x, tl.y, br.x, br.y};

    // Ensure valid rect
    if (rcScreen.right > rcScreen.left && rcScreen.bottom > rcScreen.top) {
      std::lock_guard<std::mutex> lock(g_StateMutex);
      g_TargetRect = rcScreen;
      g_hTargetWindow = hForeground;
      
      // Initialize g_LSRect if it's empty or we are not in position mode yet
      if (g_LSRect.right == 0 || !g_Settings.PositionMode) {
          g_LSRect = g_TargetRect;
      }
    }
  }
}

// Function pointers for original functions
typedef BOOL(WINAPI *EnumDisplayMonitors_t)(HDC, LPCRECT, MONITORENUMPROC,
                                            LPARAM);
typedef BOOL(WINAPI *GetMonitorInfoW_t)(HMONITOR, LPMONITORINFO);
typedef HRESULT(WINAPI *CreateDXGIFactory1_t)(REFIID, void **);

EnumDisplayMonitors_t fpEnumDisplayMonitors = nullptr;
GetMonitorInfoW_t fpGetMonitorInfoW = nullptr;
CreateDXGIFactory1_t fpCreateDXGIFactory1 = nullptr;

// Forward declarations
void InitHooks();
void RemoveHooks();

std::string IIDToString(REFIID riid) {
  if (riid == __uuidof(IDXGIFactory))
    return "IDXGIFactory";
  if (riid == __uuidof(IDXGIFactory1))
    return "IDXGIFactory1";
  if (riid == __uuidof(IDXGIFactory2))
    return "IDXGIFactory2";
  if (riid == __uuidof(IDXGIFactory3))
    return "IDXGIFactory3";
  if (riid == __uuidof(IDXGIFactory4))
    return "IDXGIFactory4";
  if (riid == __uuidof(IDXGIFactory5))
    return "IDXGIFactory5";
  if (riid == __uuidof(IDXGIFactory6))
    return "IDXGIFactory6";

  OLECHAR *guidString;
  if (StringFromCLSID(riid, &guidString) == S_OK) {
    std::wstring wstr(guidString);
    CoTaskMemFree(guidString);
    std::string str(wstr.begin(), wstr.end());
    return str;
  }
  return "Unknown IID";
}

// Global flag to track if we should inject fake monitor
static bool g_FactoryAlive = false;
bool g_Running = true;

HWND g_FoundOverlay = nullptr;
BOOL CALLBACK FindOverlayProc(HWND hwnd, LPARAM lParam) {
  DWORD pid;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == GetCurrentProcessId()) {
    if (!IsWindowVisible(hwnd))
      return TRUE;

    RECT rc;
    GetWindowRect(hwnd, &rc);

    // Check match with g_TargetRect OR g_LSRect
    // We need to lock because we access g_TargetRect/g_LSRect
    // But we can't lock inside callback easily if EnumWindows is called with lock held.
    // EnumWindows is called from ApplyWindowRegion which is called from WatcherThread.
    // WatcherThread should hold the lock or copy the rects before calling EnumWindows.
    // For now, let's assume we pass the rects via lParam or use globals carefully.
    // Since this runs in WatcherThread, and UpdateTargetRect runs in Hook thread, we need to be careful.
    // But FindOverlayProc is called synchronously by EnumWindows.
    
    // Let's use the rects passed via lParam to be safe and avoid locking here if possible, 
    // or just access globals assuming the caller (ApplyWindowRegion) took a snapshot?
    // No, ApplyWindowRegion calls EnumWindows.
    
    // Better: Check if it matches EITHER g_TargetRect OR g_LSRect.
    // Note: g_TargetRect and g_LSRect might be modified by other thread.
    // But we are just reading. Torn reads are possible but unlikely to cause crash, just miss.
    
    bool matchTarget = (rc.left == g_TargetRect.left && rc.top == g_TargetRect.top &&
        rc.right == g_TargetRect.right && rc.bottom == g_TargetRect.bottom);
        
    bool matchLS = (rc.left == g_LSRect.left && rc.top == g_LSRect.top &&
        rc.right == g_LSRect.right && rc.bottom == g_LSRect.bottom);

    if (matchTarget || matchLS) {
      g_FoundOverlay = hwnd;
      return FALSE;
    }
  }
  return TRUE;
}

void ApplyWindowRegion() {
  // Snapshot needed state
  HWND targetWindow;
  {
      std::lock_guard<std::mutex> lock(g_StateMutex);
      targetWindow = g_hTargetWindow;
  }

  if (!targetWindow)
    return;

  g_FoundOverlay = nullptr;
  EnumWindows(FindOverlayProc, 0);

  if (g_FoundOverlay) {
    if (g_Settings.SplitMode) {
      // Need current rects
      RECT targetRect;
      {
          std::lock_guard<std::mutex> lock(g_StateMutex);
          targetRect = g_TargetRect;
      }
      
      int w = targetRect.right - targetRect.left;
      int h = targetRect.bottom - targetRect.top;
      HRGN hrgn = nullptr;


      // Note: SetWindowRgn coordinates are relative to the window's upper-left
      // corner (0,0)
      switch (g_Settings.SplitType) {
      case 0: // Left
        hrgn = CreateRectRgn(0, 0, w / 2, h);
        break;
      case 1: // Right
        hrgn = CreateRectRgn(w / 2, 0, w, h);
        break;
      case 2: // Top
        hrgn = CreateRectRgn(0, 0, w, h / 2);
        break;
      case 3: // Bottom
        hrgn = CreateRectRgn(0, h / 2, w, h);
        break;
      }

      if (hrgn) {
        SetWindowRgn(g_FoundOverlay, hrgn, TRUE);
      }
    } else {
      // Reset region
      SetWindowRgn(g_FoundOverlay, NULL, TRUE);
    }
  }
}

void UpdateWindowPositions() {
  if (!g_Settings.PositionMode) {
      std::lock_guard<std::mutex> lock(g_StateMutex);
      g_LSRect = g_TargetRect;
      return;
  }
  
  HWND targetWindow;
  RECT targetRect;
  {
      std::lock_guard<std::mutex> lock(g_StateMutex);
      targetWindow = g_hTargetWindow;
      targetRect = g_TargetRect;
  }

  if (!targetWindow || !IsWindow(targetWindow)) return;

  // Get Target Window Rect (Frame) - This is the actual window on screen
  RECT targetWindowRect;
  if (!GetWindowRect(targetWindow, &targetWindowRect)) return;

  // LS Window Size (based on Target Client Area usually, stored in g_TargetRect)
  int lsWidth = targetRect.right - targetRect.left;
  int lsHeight = targetRect.bottom - targetRect.top;
  
  if (lsWidth <= 0 || lsHeight <= 0) return;

  int targetWidth = targetWindowRect.right - targetWindowRect.left;
  int targetHeight = targetWindowRect.bottom - targetWindowRect.top;

  RECT newLSRect = {0, 0, lsWidth, lsHeight};
  RECT newTargetRect = targetWindowRect;
  int gap = 0;

  // Calculate combined size
  int combinedWidth = 0;
  int combinedHeight = 0;

  if (g_Settings.PositionSide == 0 || g_Settings.PositionSide == 1) { // Left/Right
    combinedWidth = targetWidth + lsWidth + gap;
    combinedHeight = max(targetHeight, lsHeight);
  } else { // Top/Bottom
    combinedWidth = max(targetWidth, lsWidth);
    combinedHeight = targetHeight + lsHeight + gap;
  }

  // Position LS relative to Target.
  if (g_Settings.PositionSide == 0) { // Left
      newLSRect.left = targetWindowRect.left - lsWidth - gap;
      int centerY = targetWindowRect.top + targetHeight / 2;
      newLSRect.top = centerY - lsHeight / 2;
  } else if (g_Settings.PositionSide == 1) { // Right
      newLSRect.left = targetWindowRect.right + gap;
      int centerY = targetWindowRect.top + targetHeight / 2;
      newLSRect.top = centerY - lsHeight / 2;
  } else if (g_Settings.PositionSide == 2) { // Top
      int centerX = targetWindowRect.left + targetWidth / 2;
      newLSRect.left = centerX - lsWidth / 2;
      newLSRect.top = targetWindowRect.top - lsHeight - gap;
  } else { // Bottom
      int centerX = targetWindowRect.left + targetWidth / 2;
      newLSRect.left = centerX - lsWidth / 2;
      newLSRect.top = targetWindowRect.bottom + gap;
  }
  newLSRect.right = newLSRect.left + lsWidth;
  newLSRect.bottom = newLSRect.top + lsHeight;

  {
      std::lock_guard<std::mutex> lock(g_StateMutex);
      g_LSRect = newLSRect;
  }

  // Move LS Window (Overlay)
  if (g_FoundOverlay && IsWindow(g_FoundOverlay)) {
      RECT currentOverlayRect;
      GetWindowRect(g_FoundOverlay, &currentOverlayRect);
      if (abs(currentOverlayRect.left - newLSRect.left) > 2 || abs(currentOverlayRect.top - newLSRect.top) > 2) {
          SetWindowPos(g_FoundOverlay, NULL, newLSRect.left, newLSRect.top, lsWidth, lsHeight, SWP_NOZORDER | SWP_NOACTIVATE);
      }
  }
}

DWORD WINAPI WatcherThread(LPVOID lpParam) {
  while (g_Running) {
    ApplyWindowRegion();
    UpdateWindowPositions();
    Sleep(200);
  }
  return 0;
}

// Hook functions
BOOL WINAPI Detour_EnumDisplayMonitors(HDC hdc, LPCRECT lprcClip,
                                       MONITORENUMPROC lpfnEnum,
                                       LPARAM dwData) {
  // Call original first
  BOOL result = fpEnumDisplayMonitors(hdc, lprcClip, lpfnEnum, dwData);

  // Now ALWAYS inject our fake Virtual monitor
  if (result) {
    __try {
      BOOL callbackResult =
          lpfnEnum(FAKE_VIRTUAL_MONITOR, hdc, nullptr, dwData);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      Log("[LS_Windowed] EXCEPTION in callback! Code: 0x%08X",
          GetExceptionCode());
    }
  }

  return result;
}

BOOL WINAPI Detour_GetMonitorInfoW(HMONITOR hMonitor, LPMONITORINFO lpmi) {
  if (hMonitor == FAKE_VIRTUAL_MONITOR) {
    if (!lpmi)
      return FALSE;

    UpdateTargetRect();

    if (g_Settings.PositionMode) {
        std::lock_guard<std::mutex> lock(g_StateMutex);
        lpmi->rcMonitor = g_LSRect;
    } else {
        std::lock_guard<std::mutex> lock(g_StateMutex);
        lpmi->rcMonitor = g_TargetRect;
    }
    lpmi->rcWork = lpmi->rcMonitor;
    lpmi->dwFlags = 0; // Not primary

    // Handle MONITORINFOEXW if size allows
    if (lpmi->cbSize >= sizeof(MONITORINFOEXW)) {
      LPMONITORINFOEXW lpmiex = (LPMONITORINFOEXW)lpmi;
      wcsncpy_s(lpmiex->szDevice, FAKE_MONITOR_NAME, CCHDEVICENAME);
    }

    return TRUE;
  }

  return fpGetMonitorInfoW(hMonitor, lpmi);
}

HRESULT WINAPI Detour_CreateDXGIFactory1(REFIID riid, void **ppFactory) {
  HRESULT hr = fpCreateDXGIFactory1(riid, ppFactory);
  if (SUCCEEDED(hr) && ppFactory && *ppFactory) {
    bool shouldWrap = false;
    if (riid == __uuidof(IDXGIFactory) || riid == __uuidof(IDXGIFactory1) ||
        riid == __uuidof(IDXGIFactory2) || riid == __uuidof(IDXGIFactory3) ||
        riid == __uuidof(IDXGIFactory4) || riid == __uuidof(IDXGIFactory5) ||
        riid == __uuidof(IDXGIFactory6)) {
      shouldWrap = true;
    }

    if (shouldWrap) {
      IUnknown *pUnk = (IUnknown *)*ppFactory;
      IDXGIFactory6 *pRealFactory6 = nullptr;

      g_FactoryAlive = true; // Enable fake monitor injection

      // Try to get the highest interface we support (IDXGIFactory6)
      if (SUCCEEDED(pUnk->QueryInterface(__uuidof(IDXGIFactory6),
                                         (void **)&pRealFactory6))) {
        *ppFactory = new ProxyDXGIFactory(pRealFactory6);
        pUnk->Release(); // Release original, we returned the proxy
        Log("[LS_Windowed] Factory wrapped successfully (IDXGIFactory6).");
      } else {
        // Fallback to Factory2 if 6 is not available (older windows?)
        IDXGIFactory2 *pRealFactory2 = nullptr;
        if (SUCCEEDED(pUnk->QueryInterface(__uuidof(IDXGIFactory2),
                                           (void **)&pRealFactory2))) {
          Log("[LS_Windowed] Warning: IDXGIFactory6 not supported, trying "
              "IDXGIFactory2 path.");
          *ppFactory = new ProxyDXGIFactory(
              (IDXGIFactory6 *)
                  pRealFactory2); // DANGEROUS CAST if we use 6 methods
          pUnk->Release();
        } else {
          Log("[LS_Windowed] Failed to QI for IDXGIFactory2/6. Cannot wrap.");
        }
      }
    }
  }
  return hr;
}

extern "C" __declspec(dllexport) void AddonInitialize(IHost* host, ImGuiContext* ctx, void* alloc_func, void* free_func, void* user_data) {
    g_ImGuiContext = ctx;
    Log("[LS_Windowed] AddonInitialize called");
}

extern "C" __declspec(dllexport) void AddonShutdown() {
    Log("[LS_Windowed] AddonShutdown called");
}

extern "C" __declspec(dllexport) void AddonRenderSettings() {
    if (!g_ImGuiContext) return;
    ImGui::SetCurrentContext(g_ImGuiContext);

    ImGui::TextWrapped("Windowed Mode Addon allows you to force windowed mode and split screen.");
    ImGui::Separator();

    bool splitMode = g_Settings.SplitMode;
    bool positionMode = g_Settings.PositionMode;

    // Disable Split Mode if Position Mode is active
    if (positionMode) ImGui::BeginDisabled();
    if (ImGui::Checkbox("Enable Split Mode", &splitMode)) {
        g_Settings.SplitMode = splitMode;
        Log("[LS_Windowed] SplitMode changed to %d", splitMode);
    }
    if (positionMode) ImGui::EndDisabled();

    if (splitMode) {
        const char* splitTypes[] = { "Left", "Right", "Top", "Bottom" };
        int currentSplitType = g_Settings.SplitType;
        if (ImGui::Combo("Split Type", &currentSplitType, splitTypes, 4)) {
            g_Settings.SplitType = currentSplitType;
            Log("[LS_Windowed] SplitType changed to %d", currentSplitType);
        }
    }

    ImGui::Separator();
    
    // Disable Position Mode if Split Mode is active
    if (splitMode) ImGui::BeginDisabled();
    if (ImGui::Checkbox("Enable Window Positioning", &positionMode)) {
        g_Settings.PositionMode = positionMode;
        Log("[LS_Windowed] PositionMode changed to %d", positionMode);
    }
    if (splitMode) ImGui::EndDisabled();

    if (positionMode) {
        const char* posTypes[] = { "Left", "Right", "Top", "Bottom" };
        int currentPosSide = g_Settings.PositionSide;
        if (ImGui::Combo("Position Side", &currentPosSide, posTypes, 4)) {
            g_Settings.PositionSide = currentPosSide;
            Log("[LS_Windowed] PositionSide changed to %d", currentPosSide);
        }
        ImGui::TextWrapped("Positions the virtual window relative to the target window.");
    }
}

extern "C" __declspec(dllexport) uint32_t GetAddonCapabilities() {
    return ADDON_CAP_HAS_SETTINGS;
}

extern "C" __declspec(dllexport) const char* GetAddonName() {
    return "Windowed Mode";
}

extern "C" __declspec(dllexport) const char* GetAddonVersion() {
    return "1.0.0";
}

// DLL Entry Point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call,
                      LPVOID lpReserved) {
  switch (ul_reason_for_call) {
  case DLL_PROCESS_ATTACH: {
    DisableThreadLibraryCalls(hModule);

    // Initialize Logger
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(hModule, buffer, MAX_PATH);
    std::filesystem::path dllPath(buffer);
    std::wstring logPath =
        (dllPath.parent_path() / "LS_Windowed.log").wstring();
    Logger::Init(logPath);

    Log("[LS_Windowed] DLL_PROCESS_ATTACH");

    CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)InitHooks, nullptr, 0,
                 nullptr);
    CreateThread(nullptr, 0, WatcherThread, nullptr, 0, nullptr);
  } break;
  case DLL_PROCESS_DETACH:
    g_Running = false;
    Log("[LS_Windowed] DLL_PROCESS_DETACH");
    RemoveHooks();
    Logger::Close();
    break;
  }
  return TRUE;
}

void InitHooks() {
  Log("[LS_Windowed] Initializing hooks...");
  if (MH_Initialize() != MH_OK) {
    Log("[LS_Windowed] Failed to initialize MinHook.");
    return;
  }

  // Hook User32 functions
  if (MH_CreateHookApi(L"user32.dll", "EnumDisplayMonitors",
                       &Detour_EnumDisplayMonitors,
                       (LPVOID *)&fpEnumDisplayMonitors) != MH_OK) {
    Log("[LS_Windowed] Failed to hook EnumDisplayMonitors.");
  }

  if (MH_CreateHookApi(L"user32.dll", "GetMonitorInfoW",
                       &Detour_GetMonitorInfoW,
                       (LPVOID *)&fpGetMonitorInfoW) != MH_OK) {
    Log("[LS_Windowed] Failed to hook GetMonitorInfoW.");
  }

  // Hook DXGI
  if (MH_CreateHookApi(L"dxgi.dll", "CreateDXGIFactory1",
                       &Detour_CreateDXGIFactory1,
                       (LPVOID *)&fpCreateDXGIFactory1) != MH_OK) {
    Log("[LS_Windowed] Failed to hook CreateDXGIFactory1.");
  }

  // Enable hooks
  if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
    Log("[LS_Windowed] Failed to enable hooks.");
    return;
  }

  Log("[LS_Windowed] Hooks initialized.");
}

void RemoveHooks() {
  MH_DisableHook(MH_ALL_HOOKS);
  MH_Uninitialize();
}
