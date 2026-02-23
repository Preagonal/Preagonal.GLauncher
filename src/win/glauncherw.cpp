#include "pch.h"
#include "defs.h"
#include "Utils.hpp"
#include "Memory.hpp"
#include "GS2Context.h"
#include <detours.h>
#include <map>
#include <fstream>
#include <thread>
#include <sstream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <shellapi.h>
#pragma comment(lib, "ws2_32.lib")

void (*TGraalVar_SetScript)(TGraalVar* instance, TString* bytecode) = nullptr;
void (*TGraalVar_Constructor)(TGraalVar* instance, TString* var_name) = nullptr;
void (*THashList_encodesimple)(TString* out_str, TString* str) = nullptr;
void (*TScriptUniverse_addStaticObject)(void*, TGraalVar*) = nullptr;
void (*TScriptUniverse_addClassScript)(void*, TString*, void*) = nullptr;
void (*echo)(int64_t* instance, int64_t p2, int64_t p3, int64_t p4, int64_t p5, char p6) = nullptr;
void* TGameEnvironment_universe = nullptr;
void echo_hook(int64_t* instance, int64_t p2, int64_t p3, int64_t p4, int64_t p5, char p6);
uintptr_t base = 0;
bool g_EnableConsole = false;
struct hostent* (WSAAPI* True_gethostbyname)(const char* name) = nullptr;
static std::string g_selectedHost, g_selectedPort, g_startParams;
static HHOOK g_hKeyboardHook = NULL;
static HANDLE g_hHookThread = NULL;
static DWORD g_HookThreadId = 0;
static const int HOTKEY_SERVER = 1;
static const int HOTKEY_CONSOLE = 2;
static HWND g_hHiddenWindow = NULL;
static uintptr_t g_echo_address = 0;
static bool g_echo_hooked = false;
static std::string g_weaponDir, g_classDir;
static HANDLE g_watcherThread = NULL;
static bool g_watcherRunning = false;
static std::map<std::string, TGraalVar*> g_injectedWeapons;
static std::map<std::string, bool> g_injectedClasses;

static void tlog(const char* tag, const char* fmt, ...) {
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    char full[1100];
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(full, sizeof(full), "[%02d:%02d %s] [%s] %s",
        st.wHour % 12 == 0 ? 12 : st.wHour % 12, st.wMinute,
        st.wHour >= 12 ? "PM" : "AM", tag, msg);
    if (g_EnableConsole) {
        printf("%s\n", full);
    } else if (TGameEnvironment_universe) {
        char colored[1200];
        snprintf(colored, sizeof(colored), "<b><font color=#FF1493>%s</font></b>", msg);
        TString* ts = new TString(colored);
        TString* channel = new TString("game");
        echo((int64_t*)ts, 0, 0, 0, (int64_t)channel, 0);
    }
}

static std::string hashFile(const std::string& content) {
    size_t h = std::hash<std::string>{}(content);
    char buf[32];
    snprintf(buf, sizeof(buf), "%zx", h);
    return std::string(buf);
}

extern "C" __declspec(dllexport) void export_function() {}

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
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
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
        if (strcmp(className, "UnityWndClass") == 0) { g_hFoundWindow = hwnd; return FALSE; }
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
    if (!hwnd) { if (hGraal) ShowWindow(hGraal, SW_SHOW); return false; }
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

void echo_hook(int64_t* instance, int64_t p2, int64_t p3, int64_t p4, int64_t p5, char p6) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    const char* msg = instance && *instance ? (const char*)(*instance + 8) : "";
    if (strstr(msg, "PluginMain")) return;
    printf("[%02d:%02d %s] [Worlds] %s\n",
        st.wHour % 12 == 0 ? 12 : st.wHour % 12, st.wMinute,
        st.wHour >= 12 ? "PM" : "AM", msg);
}

struct hostent* WSAAPI Hooked_gethostbyname(const char* name) {
    if (name && (strstr(name, "con.quattroplay.com") || strstr(name, "con2.quattroplay.com") || strstr(name, "conworldsios.quattroplay.com") || strstr(name, "conworldsios2.quattroplay.com"))) return True_gethostbyname("127.0.0.1");
    return True_gethostbyname(name);
}

void injectWeapon(const std::string& name, const std::string& gs2_code);
void injectClass(const std::string& name, const std::string& gs2_code);


DWORD WINAPI WatcherThread(LPVOID lpParam) {
    struct DirWatch { std::string path; bool isClass; };
    std::vector<std::pair<std::string, bool>> dirs = { { g_weaponDir, false }, { g_classDir, true } };
    std::map<std::string, std::string> lastHash;

    auto watchDir = [&](const std::string& path, bool isClass) {
        HANDLE h = CreateFileA(path.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (h == INVALID_HANDLE_VALUE) return;
        char buf[4096];
        DWORD returned;
        while (g_watcherRunning) {
            if (!ReadDirectoryChangesW(h, buf, sizeof(buf), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME, &returned, NULL, NULL)) break;
            FILE_NOTIFY_INFORMATION* info = (FILE_NOTIFY_INFORMATION*)buf;
            do {
                if (info->Action == FILE_ACTION_MODIFIED || info->Action == FILE_ACTION_ADDED) {
                    char filename[MAX_PATH];
                    WideCharToMultiByte(CP_ACP, 0, info->FileName, info->FileNameLength / sizeof(WCHAR), filename, MAX_PATH, NULL, NULL);
                    filename[info->FileNameLength / sizeof(WCHAR)] = '\0';
                    std::string fname(filename);
                    if (fname.size() > 4 && fname.substr(fname.size() - 4) == ".gs2") {
                        std::string name = fname.substr(0, fname.size() - 4);
                        Sleep(100);
                        std::ifstream f(path + "\\" + fname);
                        if (f.good()) {
                            std::stringstream sbuf; sbuf << f.rdbuf();
                            std::string content = sbuf.str();
                            std::string key = (isClass ? "class:" : "weapon:") + name;
                            std::string hash = hashFile(content);
                            if (!lastHash.count(key) || lastHash[key] != hash) {
                                lastHash[key] = hash;
                                if (TGameEnvironment_universe) {
                                    if (isClass) injectClass(name, content);
                                    else injectWeapon(name, content);
                                }
                            }
                        }
                    }
                }
                if (info->Action == FILE_ACTION_REMOVED) {
                    char filename[MAX_PATH];
                    WideCharToMultiByte(CP_ACP, 0, info->FileName, info->FileNameLength / sizeof(WCHAR), filename, MAX_PATH, NULL, NULL);
                    filename[info->FileNameLength / sizeof(WCHAR)] = '\0';
                    std::string fname(filename);
                    if (fname.size() > 4 && fname.substr(fname.size() - 4) == ".gs2") {
                        std::string name = fname.substr(0, fname.size() - 4);
                        if (!isClass && g_injectedWeapons.count(name)) {
                            auto* empty = new TString("", 0);
                            TGraalVar_SetScript(g_injectedWeapons[name], empty);
                            g_injectedWeapons.erase(name);
                            tlog("Compiler", "Weapon %s deleted", name.c_str());
                        }
                    }
                }
                if (!info->NextEntryOffset) break;
                info = (FILE_NOTIFY_INFORMATION*)((char*)info + info->NextEntryOffset);
            } while (true);
        }
        CloseHandle(h);
    };

    std::thread t([&]() { watchDir(g_classDir, true); });
    watchDir(g_weaponDir, false);
    t.join();
    return 0;
}

void injectWeapon(const std::string& name, const std::string& gs2_code) {
    TGraalVar* obj = nullptr;
    if (g_injectedWeapons.count(name)) {
        obj = g_injectedWeapons[name];
    } else {
        TString* encoded_name = (TString*)alloc(8);
        TString* var_name = new TString(name.c_str());
        THashList_encodesimple(encoded_name, var_name);
        obj = reinterpret_cast<TGraalVar*>(alloc(600));
        TGraalVar_Constructor(obj, encoded_name);
        ((void(__fastcall*)(TGraalVar*, int64_t))(obj->vtable[20]))(obj, 2);
        TScriptUniverse_addStaticObject(TGameEnvironment_universe, obj);
        g_injectedWeapons[name] = obj;
    }
    auto response = GS2Context::Compile(gs2_code);
    tlog("Compiler", "Weapon %s added/updated", name.c_str());
    if (!response.success) tlog("Compiler", "Compile error in %s: %s", name.c_str(), response.errors.empty() ? "unknown" : response.errors[0].msg().c_str());
    auto* bytecode = new TString((char*)response.bytecode.buffer(), response.bytecode.length());
    TGraalVar_SetScript(obj, bytecode);
}

void injectClass(const std::string& name, const std::string& gs2_code) {
    auto response = GS2Context::Compile(gs2_code);
    tlog("Compiler", "Script %s added/updated", name.c_str());
    if (!response.success) {
        tlog("Compiler", "Compile error in %s: %s", name.c_str(), response.errors.empty() ? "unknown" : response.errors[0].msg().c_str());
        return;
    }
    auto* className = new TString(name.c_str());
    auto* bytecode = new TString((char*)response.bytecode.buffer(), response.bytecode.length());
    TScriptUniverse_addClassScript(TGameEnvironment_universe, className, bytecode);
    g_injectedClasses[name] = true;
}

void scanAndInjectFolder(const std::string& folder, bool isClass) {
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA((folder + "\\*.gs2").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        std::string filename = fd.cFileName;
        std::string name = filename.substr(0, filename.size() - 4);
        std::ifstream f(folder + "\\" + filename);
        if (!f.good()) continue;
        std::stringstream buf;
        buf << f.rdbuf();
        tlog("Compiler", "Loading %s as '%s'", filename.c_str(), name.c_str());
        if (isClass) injectClass(name, buf.str());
        else injectWeapon(name, buf.str());
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
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
    if (!g_selectedHost.empty()) {
        auto checkPacketLog = [](const std::string& path) { std::ifstream f(path); std::string line; return std::getline(f, line) && line == "true"; };
        auto replaceStr = [](std::string& s, const char* search, const char* repl) { size_t pos = s.find(search); if (pos != std::string::npos) s.replace(pos + strlen(search), s.find("\"", pos + strlen(search)) - (pos + strlen(search)), repl); };
        auto replaceBool = [](std::string& s, const char* search, const char* repl) { size_t pos = s.find(search); if (pos != std::string::npos) s.replace(pos, strlen(search), repl); };
        bool hasNP = g_selectedPort.size() >= 2 && g_selectedPort.substr(g_selectedPort.size() - 2) == "NP";
        bool hasPacketLog = checkPacketLog("license.packet") || checkPacketLog("license/license.packet") || (!exeDir.empty() && (checkPacketLog(exeDir + "\\license.packet") || checkPacketLog(exeDir + "\\license\\license.packet")));
        if (hasNP) replaceBool(gs2_code, "this.loginnewprotocol = false;", "this.loginnewprotocol = true;");
        if (hasPacketLog) replaceBool(gs2_code, "this.loginpacketlog = false;", "this.loginpacketlog = true;");
        replaceStr(gs2_code, "this.loginhost = \"", g_selectedHost.c_str());
        replaceStr(gs2_code, "this.loginport = \"", (hasNP ? g_selectedPort.substr(0, g_selectedPort.size() - 2) : g_selectedPort).c_str());
        if (!g_startParams.empty()) {
            if (gs2_code.find("serverstartconnect = \"") != std::string::npos)
                replaceStr(gs2_code, "serverstartconnect = \"", g_startParams.c_str());
            else
                gs2_code += "\nserverstartconnect = \"" + g_startParams + "\";";
        }
    }
    auto response = GS2Context::Compile(gs2_code);
    if (!response.success) tlog("GS2Parser", "Compile error: %s", response.errors.empty() ? "Unknown error" : response.errors[0].msg().c_str());
    auto* bytecode = new TString((char*)response.bytecode.buffer(), response.bytecode.length());
    TGraalVar_SetScript(StartScript_Connector, bytecode);

    std::string weaponDir = exeDir.empty() ? "offline\\weapons" : exeDir + "\\offline\\weapons";
    std::string classDir  = exeDir.empty() ? "offline\\classes" : exeDir + "\\offline\\classes";
    g_weaponDir = weaponDir;
    g_classDir  = classDir;

    scanAndInjectFolder(weaponDir, false);
    scanAndInjectFolder(classDir, true);

    g_watcherRunning = true;
    g_watcherThread = CreateThread(NULL, 0, WatcherThread, NULL, 0, NULL);
    tlog("Compiler", "Watching '%s' and '%s' for changes", weaponDir.c_str(), classDir.c_str());
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
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; i++) {
        std::wstring arg(argv[i]);
        if (arg.substr(0, 8) == L"graal://" || arg.substr(0, 9) == L"graal3://") {
            std::wstring url = arg.substr(arg.find(L"//") + 2);
            size_t slash = url.find(L'/');
            g_startParams = std::string(url.begin(), slash != std::wstring::npos ? url.begin() + slash : url.end());
        }
    }
    LocalFree(argv);
    g_hHookThread = CreateThread(NULL, 0, HotkeyThread, hModule, 0, &g_HookThreadId);
    bool showDialog = false;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\GLauncher", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD value = 0;
        DWORD size = sizeof(value);
        if (RegQueryValueExA(hKey, "SelectServer", NULL, NULL, (BYTE*)&value, &size) == ERROR_SUCCESS && value == 1) showDialog = true;
        RegCloseKey(hKey);
    }
    if (showDialog) {
        for (const auto& file : GetLicensePaths()) if (ShowServerDialog(file)) break;
    } else {
        for (const auto& file : GetLicensePaths()) {
            std::ifstream input(file);
            if (input.fail()) continue;
            std::string ip, port;
            if (std::getline(input, ip) && !ip.empty() && std::getline(input, port)) {
                g_selectedHost = ip;
                g_selectedPort = port;
                break;
            }
            input.close();
        }
    }
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
    *((uintptr_t*)&TGraalVar_Constructor)         = base + (0x7FF97F319BF0 - 0x7FF97E840000);
    *((uintptr_t*)&TGraalVar_SetScript)           = base + (0x7FF97F31FCD0 - 0x7FF97E840000);
    *((uintptr_t*)&THashList_encodesimple)        = base + (0x7FF97F194430 - 0x7FF97E840000);
    *((uintptr_t*)&TScriptUniverse_addStaticObject) = base + (0x7FF97F342E80 - 0x7FF97E840000);
    *((uintptr_t*)&TScriptUniverse_addClassScript)  = base + (0x7FF97F342C60 - 0x7FF97E840000);
    *((uintptr_t*)&echo)                          = base + (0x7FF97F1BD350 - 0x7FF97E840000);
    g_echo_address = base + (0x7FF97F1BD350 - 0x7FF97E840000);
    if (g_EnableConsole) {
        Memory::hook((void*)g_echo_address, (void*)echo_hook, 18);
        g_echo_hooked = true;
    }
    Memory::hook((void*)enterNextConnectorMode_address, (void*)enterNextConnectorMode, 17);
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string command = std::string("\"") + exePath + "\" \"%1\"";
    std::string iconPath = std::string(exePath) + ",0";
    tlog("GLauncher", "Registering graal:// protocol...");
    if (RegCreateKeyExA(HKEY_CLASSES_ROOT, "graal", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, NULL, 0, REG_SZ, (BYTE*)"URL:GRAAL Protocol", 17);
        DWORD editFlags = 2;
        RegSetValueExA(hKey, "EditFlags", 0, REG_DWORD, (BYTE*)&editFlags, 4);
        RegSetValueExA(hKey, "URL Protocol", 0, REG_SZ, (BYTE*)"", 1);
        RegCloseKey(hKey);
        RegCreateKeyExA(HKEY_CLASSES_ROOT, "graal\\DefaultIcon", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
        RegSetValueExA(hKey, NULL, 0, REG_SZ, (BYTE*)iconPath.c_str(), iconPath.length() + 1);
        RegCloseKey(hKey);
        RegCreateKeyExA(HKEY_CLASSES_ROOT, "graal\\Shell\\open\\command", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
        RegSetValueExA(hKey, NULL, 0, REG_SZ, (BYTE*)command.c_str(), command.length() + 1);
        RegCloseKey(hKey);
        tlog("GLauncher", "graal:// Registered to %s", exePath);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH: initialize(hModule); break;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH: break;
    }
    return TRUE;
}