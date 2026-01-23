#include "pch.h"
#include "defs.h"
#include "Utils.hpp"
#include "Memory.hpp"
#include "GS2Context.h"
#include <detours.h>
#include <fstream>
#include <sstream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <shellapi.h>
#pragma comment(lib, "ws2_32.lib")
void (*TGraalVar_SetScript)(TGraalVar* instance, TString* bytecode) = nullptr;
void (*TGraalVar_Constructor)(TGraalVar* instance, TString* var_name) = nullptr;
void (*THashList_encodesimple) (TString* out_str, TString* str) = nullptr;
void (*TScriptUniverse_addStaticObject)(void*, TGraalVar*) = nullptr;
void (*echo)(void* instance, TString* error) = nullptr;
void* TGameEnvironment_universe = nullptr;
void echo_hook(TString* log, double a1, double a2, double a3, char const* chr);
uintptr_t base = 0;
bool g_EnableConsole = false;
struct hostent* (WSAAPI* True_gethostbyname)(const char* name) = nullptr;
static std::string g_selectedHost, g_selectedPort;
static HHOOK g_hKeyboardHook = NULL;
static HANDLE g_hHookThread = NULL;
static DWORD g_HookThreadId = 0;
static const int HOTKEY_SERVER = 1;
static const int HOTKEY_CONSOLE = 2;
static HWND g_hHiddenWindow = NULL;
static uintptr_t g_echo_address = 0;
static bool g_echo_hooked = false;

extern "C" __declspec(dllexport) void export_function() {
    // Hi
}

LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_HOTKEY) {
        if (wParam == HOTKEY_SERVER) {
            HKEY hKey;
            if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\GLauncher", 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                DWORD value = 0;
                DWORD size = sizeof(value);
                RegQueryValueExA(hKey, "SelectServer", NULL, NULL, (BYTE*)&value, &size);
                value = (value == 1) ? 0 : 1;
                RegSetValueExA(hKey, "SelectServer", 0, REG_DWORD, (BYTE*)&value, sizeof(value));
                RegCloseKey(hKey);
                if (value == 1) MessageBoxA(NULL, "Server selection enabled. Dialog will show on every launch.", "GLauncher", MB_OK | MB_ICONINFORMATION);
                else MessageBoxA(NULL, "Server selection disabled.", "GLauncher", MB_OK | MB_ICONINFORMATION);
            }
        }
        if (wParam == HOTKEY_CONSOLE) {
            g_EnableConsole = !g_EnableConsole;
            HKEY hKey;
            if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\GLauncher", 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                DWORD value = g_EnableConsole ? 1 : 0;
                RegSetValueExA(hKey, "EnableConsole", 0, REG_DWORD, (BYTE*)&value, sizeof(value));
                RegCloseKey(hKey);
            }
            if (g_EnableConsole) {
                SetupConsole();
                ShowWindow(GetConsoleWindow(), SW_SHOW);
                if (g_echo_address && !g_echo_hooked) {
                    *((uintptr_t*)&echo) = g_echo_address;
                    Memory::hook((void*)g_echo_address, (void*)echo_hook, 18);
                    g_echo_hooked = true;
                }
                MessageBoxA(NULL, "Console enabled.", "GLauncher", MB_OK | MB_ICONINFORMATION);
            } else {
                CloseConsole();
                MessageBoxA(NULL, "Console disabled.", "GLauncher", MB_OK | MB_ICONINFORMATION);
            }
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

DWORD WINAPI HotkeyThread(LPVOID lpParam) {
    HMODULE hModule = (HMODULE)lpParam;
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = hModule;
    wc.lpszClassName = "GLauncherHidden";
    RegisterClassExA(&wc);
    g_hHiddenWindow = CreateWindowExA(0, "GLauncherHidden", "GLauncher Hidden", 0, 0, 0, 0, 0, NULL, NULL, hModule, NULL);

    RegisterHotKey(g_hHiddenWindow, HOTKEY_SERVER, MOD_CONTROL | MOD_SHIFT, 0x4C);
    RegisterHotKey(g_hHiddenWindow, HOTKEY_CONSOLE, MOD_CONTROL | MOD_SHIFT, 0x4F);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

struct ServerEntry { std::string ip, port; };
static std::vector<ServerEntry> g_servers;
static int g_selectedServer = -1;
static HWND g_hFoundWindow = NULL;

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    DWORD windowPid;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid == (DWORD)lParam && IsWindowVisible(hwnd)) {
        char className[256];
        GetClassNameA(hwnd, className, sizeof(className));
        if (strcmp(className, "UnityWndClass") == 0) {
            g_hFoundWindow = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

static std::string GetExeDir() {
    char exePath[MAX_PATH];
    return GetModuleFileNameA(NULL, exePath, MAX_PATH) ? (exePath[strrchr(exePath, '\\') - exePath] = '\0', std::string(exePath)) : std::string();
}

static std::vector<std::string> GetLicensePaths() {
    std::vector<std::string> paths;
    std::string exeDir = GetExeDir();
    if (!exeDir.empty()) { paths.push_back(exeDir + "\\license.graal"); paths.push_back(exeDir + "\\license\\license.graal"); }
    paths.push_back("license.graal");
    paths.push_back("license/license.graal");
    return paths;
}

LRESULT CALLBACK ServerDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wParam) >= 1000 && LOWORD(wParam) < 1000 + (int)g_servers.size()) { g_selectedServer = LOWORD(wParam) - 1000; DestroyWindow(hwnd); return 0; }
            break;
        case WM_CLOSE: g_selectedServer = g_selectedServer < 0 ? 0 : g_selectedServer; DestroyWindow(hwnd); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool ShowServerDialog(const std::string& file) {
    std::ifstream input(file);
    if (input.fail()) return false;
    g_servers.clear();
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        ServerEntry entry{line, ""};
        if (std::getline(input, line)) entry.port = line;
        g_servers.push_back(entry);
    }
    input.close();
    if (g_servers.empty()) return false;

    int dialogWidth = 235;
    int dialogHeight = 100 + (g_servers.size() * 40);
    int posX = CW_USEDEFAULT, posY = CW_USEDEFAULT;
    g_hFoundWindow = NULL;
    DWORD pid = GetCurrentProcessId();
    EnumWindows(EnumWindowsProc, pid);
    HWND hGraal = g_hFoundWindow;
    if (hGraal) {
        RECT graalRect;
        GetWindowRect(hGraal, &graalRect);
        posX = graalRect.left + (graalRect.right - graalRect.left - dialogWidth) / 2;
        posY = graalRect.top + (graalRect.bottom - graalRect.top - dialogHeight) / 2;
    }
    HICON hIcon = (HICON)LoadImageA(NULL, MAKEINTRESOURCEA(32512), IMAGE_ICON, 0, 0, LR_SHARED);
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH)) hIcon = ExtractIconA(GetModuleHandle(NULL), exePath, 0);
    WNDCLASSEXA wc = {sizeof(WNDCLASSEXA)}; wc.lpfnWndProc = ServerDlgProc; wc.hInstance = GetModuleHandle(NULL); wc.lpszClassName = "GLauncherSel"; wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1); wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hIcon = hIcon; wc.hIconSm = hIcon;
    RegisterClassExA(&wc);
    if (hGraal) ShowWindow(hGraal, SW_HIDE);
    HWND hwnd = CreateWindowExA(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, "GLauncherSel", "GLauncher - Select Server", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, posX, posY, dialogWidth, dialogHeight, NULL, NULL, GetModuleHandle(NULL), NULL);
    if (!hwnd) {
        if (hGraal) ShowWindow(hGraal, SW_SHOW);
        return false;
    }
    SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon); SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HWND hLabel = CreateWindowExA(0, "STATIC", "Select a server:", WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 10, 210, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
    SendMessageA(hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    for (size_t i = 0; i < g_servers.size(); i++) {
        std::string btnText = g_servers[i].ip + ":" + g_servers[i].port;
        HWND hBtn = CreateWindowExA(0, "BUTTON", btnText.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_TEXT, 10, 40 + (i * 40), 200, 30, hwnd, (HMENU)(1000 + i), GetModuleHandle(NULL), NULL);
        SendMessageA(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
    }
    ShowWindow(hwnd, SW_SHOW); UpdateWindow(hwnd);
    MSG msg; g_selectedServer = -1;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    if (hGraal) ShowWindow(hGraal, SW_SHOW);
    if (g_selectedServer < 0) return false;
    g_selectedHost = g_servers[g_selectedServer].ip;
    g_selectedPort = g_servers[g_selectedServer].port;
    return true;
}

void echo_hook(TString* log, double a1, double a2, double a3, char const* chr) {
    if (g_EnableConsole) printf("[Worlds]: %s\n", log->to_chars());
}

struct hostent* WSAAPI Hooked_gethostbyname(const char* name) { // this is a pointless precaution I added incase the hooking fails for some reason
    if (name && (strstr(name, "con.quattroplay.com") || strstr(name, "con2.quattroplay.com") || strstr(name, "conworldsios.quattroplay.com") || strstr(name, "conworldsios2.quattroplay.com"))) return True_gethostbyname("127.0.0.1");
    return True_gethostbyname(name);
}

void __cdecl enterNextConnectorMode(int mode) {
    TGameEnvironment_universe = *(void**)(base + (0x7FF97FC191B0 - 0x7FF97E840000));
    TString* encoded_name = (TString*)alloc(8);
    TString* var_name = new TString("StartScript_Connector");
    THashList_encodesimple(encoded_name, var_name);
    TGraalVar* StartScript_Connector = reinterpret_cast<TGraalVar*>(alloc(600));
    TGraalVar_Constructor(StartScript_Connector, encoded_name);
    ((void(__fastcall*)(TGraalVar*, int64_t))(StartScript_Connector->vtable[20]))(StartScript_Connector, 2);
    TScriptUniverse_addStaticObject(TGameEnvironment_universe, StartScript_Connector);
    std::string gs2_code;
    bool found_local = false;
    std::string exeDir = GetExeDir();
    std::vector<std::string> gs2paths;
    if (!exeDir.empty()) { gs2paths.push_back(exeDir + "\\bootstrap.gs2"); gs2paths.push_back(exeDir + "\\license\\bootstrap.gs2"); }
    gs2paths.push_back("bootstrap.gs2");
    gs2paths.push_back("license/bootstrap.gs2");
    for (const auto& gs2Path : gs2paths) {
        std::ifstream input(gs2Path);
        if (input.good()) {
            std::stringstream buffer;
            buffer << input.rdbuf();
            gs2_code = buffer.str();
            found_local = true;
            input.close();
            break;
        }
    }
    if (!found_local) {
        HMODULE hModule = GetModuleHandleA("GLauncherW.dll");
        HRSRC hRes = FindResourceA(hModule, MAKEINTRESOURCE(101), RT_RCDATA);
        if (!hRes) return;
        HGLOBAL hLoaded = LoadResource(hModule, hRes);
        void* pData = LockResource(hLoaded);
        size_t size = SizeofResource(hModule, hRes);
        gs2_code = std::string((char*)pData, size);
    }
    if (g_EnableConsole) printf("[GS2Parser]: Original GS2 size: %zu\n", gs2_code.size());
    if (!g_selectedHost.empty()) {
        auto replace = [](std::string& s, const char* search, const char* repl) { size_t pos = s.find(search); if (pos != std::string::npos) s.replace(pos + strlen(search), s.find("\"", pos + strlen(search)) - (pos + strlen(search)), repl); };
        replace(gs2_code, "this.loginhost = \"", g_selectedHost.c_str());
        replace(gs2_code, "this.loginport = \"", g_selectedPort.c_str());
        if (g_EnableConsole) printf("[GS2Parser]: After replacement GS2 size: %zu\n", gs2_code.size());
    }
    auto response = GS2Context::Compile(gs2_code);
    if (g_EnableConsole) printf("[GS2Parser]: Compiled bytecode size: %zu\n", response.bytecode.length());
    if (!response.success && g_EnableConsole) printf("GS2 Compile Error: %s\n", response.errors.empty() ? "Unknown error" : response.errors[0].msg().c_str());
    auto* bytecode = new TString((char*)response.bytecode.buffer(), response.bytecode.length());
    TGraalVar_SetScript(StartScript_Connector, bytecode);
}

void initialize(HMODULE hModule) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\GLauncher", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD value = 0;
        DWORD size = sizeof(value);
        if (RegQueryValueExA(hKey, "EnableConsole", NULL, NULL, (BYTE*)&value, &size) == ERROR_SUCCESS) g_EnableConsole = value == 1;
        RegCloseKey(hKey);
    }
    if (g_EnableConsole) SetupConsole();
    g_hHookThread = CreateThread(NULL, 0, HotkeyThread, hModule, 0, &g_HookThreadId);
    bool showDialog = false;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\GLauncher", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD value = 0;
        DWORD size = sizeof(value);
        if (RegQueryValueExA(hKey, "SelectServer", NULL, NULL, (BYTE*)&value, &size) == ERROR_SUCCESS && value == 1) showDialog = true;
        RegCloseKey(hKey);
    }
    if (showDialog) for (const auto& file : GetLicensePaths()) if (ShowServerDialog(file)) break;
    if (g_EnableConsole) ShowWindow(GetConsoleWindow(), SW_SHOW);
    HMODULE hWs2_32 = GetModuleHandleA("ws2_32.dll");
    if (!hWs2_32) hWs2_32 = LoadLibraryA("ws2_32.dll");
    if (hWs2_32) {
        True_gethostbyname = (struct hostent* (WSAAPI*)(const char*))GetProcAddress(hWs2_32, "gethostbyname");
        if (True_gethostbyname) {
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourAttach(&(PVOID&)True_gethostbyname, Hooked_gethostbyname);
            DetourTransactionCommit();
        }
    }
    HMODULE Graal3DEngine = NULL;
    for (int i = 0; i < 50; i++) {
        Graal3DEngine = GetModuleHandleA("Graal3DEngine.dll");
        if (Graal3DEngine) break;
        Sleep(50);
    }
    if (!Graal3DEngine) return;
    base = reinterpret_cast<uintptr_t>(Graal3DEngine);
    uintptr_t enterNextConnectorMode_address = base + (0x00007FF97F303210 - 0x7FF97E840000);
    *((uintptr_t*)&TGraalVar_Constructor) = base + (0x7FF97F319BF0 - 0x7FF97E840000);
    *((uintptr_t*)&TGraalVar_SetScript) = base + (0x7FF97F31FCD0 - 0x7FF97E840000);
    *((uintptr_t*)&THashList_encodesimple) = base + (0x7FF97F194430 - 0x7FF97E840000);
    *((uintptr_t*)&TScriptUniverse_addStaticObject) = base + (0x7FF97F342E80 - 0x7FF97E840000);
    g_echo_address = base + (0x7FF97F1BD350 - 0x7FF97E840000);
    if (g_EnableConsole) {
        *((uintptr_t*)&echo) = g_echo_address;
        //printf("module: %d\n", __LINE__);
        Memory::hook((void*)g_echo_address, (void*)echo_hook, 18);
        g_echo_hooked = true;
    }
    Memory::hook((void*)enterNextConnectorMode_address, (void*)enterNextConnectorMode, 17);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            initialize(hModule);
            break;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}