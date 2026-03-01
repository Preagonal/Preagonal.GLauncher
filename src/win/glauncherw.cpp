#include "pch.h"
#include "defs.h"
#include "Utils.hpp"
#include "Memory.hpp"
#include "GS2Context.h"
#include <detours.h>
#include <map>
#include <set>
#include <fstream>
#include <thread>
#include <sstream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <shellapi.h>
#pragma comment(lib, "ws2_32.lib")
void (*TGraalVar_SetScript)(TGraalVar*, TString*) = nullptr;
void (*TGraalVar_Constructor)(TGraalVar*, TString*) = nullptr;
void (*THashList_encodesimple)(TString*, TString*) = nullptr;
void (*TScriptUniverse_addStaticObject)(void*, TGraalVar*) = nullptr;
void (*TScriptUniverse_addClassScript)(void*, TString*, void*) = nullptr;
void (*echo)(int64_t*, int64_t, int64_t, int64_t, const char*, char) = nullptr;
void* TGameEnvironment_universe = nullptr;
void (*TLog_echo)(int64_t*, int64_t, int64_t, int64_t, const char*, char) = nullptr;
uintptr_t base = 0;
bool g_EnableConsole = false, g_DebugMode = false;
static const int g_DevMode = 2;
struct hostent* (WSAAPI* True_gethostbyname)(const char*) = nullptr;
static std::string g_selectedHost, g_selectedPort, g_startParams;
static HANDLE g_hHookThread = NULL;
static DWORD g_HookThreadId = 0;
static const int HOTKEY_SERVER = 1, HOTKEY_CONSOLE = 2;
static HWND g_hHiddenWindow = NULL;
static bool g_TLog_echoed = false;
static HANDLE (WINAPI* True_CreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) = nullptr;
static HANDLE (WINAPI* True_CreateFile2)(LPCWSTR, DWORD, DWORD, DWORD, LPCREATEFILE2_EXTENDED_PARAMETERS) = nullptr;
static BOOL (WINAPI* True_CreateDirectoryW)(LPCWSTR, LPSECURITY_ATTRIBUTES) = nullptr;
static DWORD (WINAPI* True_GetFileAttributesW)(LPCWSTR) = nullptr;
static DWORD (WINAPI* True_GetFileAttributesA)(LPCSTR) = nullptr;
static HANDLE (WINAPI* True_FindFirstFileW)(LPCWSTR, LPWIN32_FIND_DATAW) = nullptr;
static HANDLE (WINAPI* True_FindFirstFileA)(LPCSTR, LPWIN32_FIND_DATAA) = nullptr;
static std::wstring g_newBase, g_oldBase;
static std::set<std::wstring> g_madeDirs;
static std::string g_weaponDir, g_classDir, g_bytecodeWeaponDir, g_bytecodeClassDir, g_bootstrapPath;
static TGraalVar* g_startScript = nullptr;
static HANDLE g_watcherThread = NULL;
static bool g_watcherRunning = false;
static std::map<std::string, TGraalVar*> g_weaponTable;
static std::map<std::string, bool> g_classTable;
struct ServerEntry { std::string ip, port; };
static std::vector<ServerEntry> g_servers;
static int g_selectedServer = -1;
static HWND g_hFoundWindow = NULL;
static std::string parseConsoleMarkup(const std::string& input) {
    static const std::pair<const char*, const char*> colors[] = {
        {"red","\033[38;2;255;0;0m"},         {"blue","\033[38;2;0;100;255m"},       {"green","\033[38;2;0;200;0m"},
        {"yellow","\033[38;2;255;255;0m"},    {"white","\033[38;2;255;255;255m"},    {"orange","\033[38;2;255;165;0m"},
        {"purple","\033[38;2;160;32;240m"},   {"pink","\033[38;2;255;105;180m"},     {"cyan","\033[38;2;0;255;255m"},
        {"lime","\033[38;2;50;205;50m"},      {"gray","\033[38;2;128;128;128m"},     {"brown","\033[38;2;139;69;19m"},
        {"gold","\033[38;2;255;215;0m"},      {"lightred","\033[38;2;255;102;102m"}, {"lightblue","\033[38;2;100;180;255m"},
        {"lightgreen","\033[38;2;144;238;144m"},{"lightpurple","\033[38;2;200;150;255m"},{"lightpink","\033[38;2;255;182;193m"},
        {"lightyellow","\033[38;2;255;255;153m"},{"lightcyan","\033[38;2;160;255;255m"},{"lightgray","\033[38;2;211;211;211m"},
        {"darkred","\033[38;2;139;0;0m"},     {"darkblue","\033[38;2;0;0;139m"},     {"darkgreen","\033[38;2;0;100;0m"},
        {}
    };
    auto findColor = [&](const std::string& val) -> std::string {
        for (int j = 0; colors[j].first; j++)
            if (val == colors[j].first) return colors[j].second;
        return "";
    };
    std::string out;
    size_t i = 0;
    while (i < input.size()) {
        if (input[i] != '<') { out += input[i++]; continue; }
        size_t end = input.find('>', i);
        if (end == std::string::npos) { out += input[i++]; continue; }
        std::string tag = input.substr(i + 1, end - i - 1);
        i = end + 1;
        if (tag == "b") out += "\033[1m";
        else if (tag == "/b") out += "\033[22m";
        else if (tag == "i") out += "\033[3m";
        else if (tag == "/i") out += "\033[23m";
        else if (tag == "/font" || tag == "/a") out += "\033[39m";
        else if (auto c = findColor(tag); !c.empty()) out += c;
        else if (tag.substr(0, 10) == "font color") {
            size_t eq = tag.find('=');
            if (eq != std::string::npos) {
                std::string val = tag.substr(eq + 1);
                while (!val.empty() && (val[0] == '"' || val[0] == ' ')) val = val.substr(1);
                while (!val.empty() && (val.back() == '"' || val.back() == ' ')) val.pop_back();
                auto named = findColor(val);
                if (!named.empty()) { out += named; }
                else {
                    if (!val.empty() && val[0] == '#') val = val.substr(1);
                    if (val.size() == 6) {
                        int r = strtol(val.substr(0,2).c_str(),nullptr,16), g = strtol(val.substr(2,2).c_str(),nullptr,16), b = strtol(val.substr(4,2).c_str(),nullptr,16);
                        char ansi[32]; snprintf(ansi, sizeof(ansi), "\033[38;2;%d;%d;%dm", r, g, b); out += ansi;
                    }
                }
            }
        } else if (tag.find("://") != std::string::npos || tag.substr(0, 1) == "a") {
            size_t hq1 = tag.find('"'), hq2 = tag.rfind('"');
            if (hq1 != std::string::npos && hq2 != hq1) {
                std::string url = tag.substr(hq1 + 1, hq2 - hq1 - 1);
                out += "\033]8;;" + url + "\033\\";
                size_t aend = input.find("</a>", i);
                if (aend != std::string::npos) { out += input.substr(i, aend - i); out += "\033]8;;\033\\"; i = aend + 4; }
            }
        }
    }
    return out + "\033[0m";
}
static std::string hashFile(const std::string& c) { char b[32]; snprintf(b, sizeof(b), "%zx", std::hash<std::string>{}(c)); return b; }
extern "C" __declspec(dllexport) void export_function() {}
LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg != WM_HOTKEY) return DefWindowProc(hwnd, msg, wParam, lParam);
    if (wParam == HOTKEY_SERVER) {
        HKEY hKey;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\GLauncher", 0, NULL, 0, KEY_READ|KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            DWORD value = 0, size = sizeof(value);
            RegQueryValueExA(hKey, "SelectServer", NULL, NULL, (BYTE*)&value, &size);
            value = value == 1 ? 0 : 1;
            RegSetValueExA(hKey, "SelectServer", 0, REG_DWORD, (BYTE*)&value, sizeof(value));
            RegCloseKey(hKey);
            MessageBoxA(NULL, value == 1 ? "Server selection enabled. Dialog will show on every launch." : "Server selection disabled.", "GLauncher", MB_OK|MB_ICONINFORMATION);
        }
    } else if (wParam == HOTKEY_CONSOLE) {
        g_EnableConsole = !g_EnableConsole;
        HKEY hKey;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\GLauncher", 0, NULL, 0, KEY_READ|KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            DWORD value = g_EnableConsole ? 1 : 0;
            RegSetValueExA(hKey, "EnableConsole", 0, REG_DWORD, (BYTE*)&value, sizeof(value));
            RegCloseKey(hKey);
        }
        if (g_EnableConsole) { SetupConsole(); ShowWindow(GetConsoleWindow(), SW_SHOW); MessageBoxA(NULL, "Console enabled.", "GLauncher", MB_OK|MB_ICONINFORMATION); }
        else { CloseConsole(); MessageBoxA(NULL, "Console disabled.", "GLauncher", MB_OK|MB_ICONINFORMATION); }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
DWORD WINAPI HotkeyThread(LPVOID lpParam) {
    HMODULE hModule = (HMODULE)lpParam;
    WNDCLASSEXA wc = {}; wc.cbSize = sizeof(WNDCLASSEXA); wc.lpfnWndProc = HiddenWndProc; wc.hInstance = hModule; wc.lpszClassName = "GLauncherHidden";
    RegisterClassExA(&wc);
    g_hHiddenWindow = CreateWindowExA(0, "GLauncherHidden", "GLauncher Hidden", 0, 0, 0, 0, 0, NULL, NULL, hModule, NULL);
    RegisterHotKey(g_hHiddenWindow, HOTKEY_SERVER, MOD_CONTROL|MOD_SHIFT, 0x4C);
    RegisterHotKey(g_hHiddenWindow, HOTKEY_CONSOLE, MOD_CONTROL|MOD_SHIFT, 0x4F);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    DWORD pid; GetWindowThreadProcessId(hwnd, &pid);
    char cls[256];
    if (pid == (DWORD)lParam && IsWindowVisible(hwnd) && GetClassNameA(hwnd, cls, sizeof(cls)) && strcmp(cls, "UnityWndClass") == 0) { g_hFoundWindow = hwnd; return FALSE; }
    return TRUE;
}

LRESULT CALLBACK ServerDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_COMMAND && LOWORD(wParam) >= 1000 && LOWORD(wParam) < 1000 + (int)g_servers.size()) { g_selectedServer = LOWORD(wParam) - 1000; DestroyWindow(hwnd); return 0; }
    if (msg == WM_CLOSE) { if (g_selectedServer < 0) g_selectedServer = 0; DestroyWindow(hwnd); return 0; }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
bool ShowServerDialog(const std::string& file) {
    std::ifstream input(file);
    if (input.fail()) return false;
    g_servers.clear();
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        ServerEntry e{line, ""}; if (std::getline(input, line)) e.port = line;
        g_servers.push_back(e);
    }
    if (g_servers.empty()) return false;
    int W = 235, H = 100 + (g_servers.size() * 40), posX = CW_USEDEFAULT, posY = CW_USEDEFAULT;
    g_hFoundWindow = NULL;
    EnumWindows(EnumWindowsProc, GetCurrentProcessId());
    HWND hGraal = g_hFoundWindow;
    if (hGraal) { RECT r; GetWindowRect(hGraal, &r); posX = r.left + (r.right - r.left - W) / 2; posY = r.top + (r.bottom - r.top - H) / 2; }
    char exePath[MAX_PATH]; GetModuleFileNameA(NULL, exePath, MAX_PATH);
    HICON hIcon = ExtractIconA(GetModuleHandle(NULL), exePath, 0);
    WNDCLASSEXA wc = {sizeof(WNDCLASSEXA)}; wc.lpfnWndProc = ServerDlgProc; wc.hInstance = GetModuleHandle(NULL); wc.lpszClassName = "GLauncherSel"; wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1); wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hIcon = wc.hIconSm = hIcon;
    RegisterClassExA(&wc);
    if (hGraal) ShowWindow(hGraal, SW_HIDE);
    HWND hwnd = CreateWindowExA(WS_EX_DLGMODALFRAME|WS_EX_TOPMOST, "GLauncherSel", "GLauncher - Select Server", WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU, posX, posY, W, H, NULL, NULL, GetModuleHandle(NULL), NULL);
    if (!hwnd) { if (hGraal) ShowWindow(hGraal, SW_SHOW); return false; }
    SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon); SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HWND hLabel = CreateWindowExA(0, "STATIC", "Select a server:", WS_CHILD|WS_VISIBLE|SS_LEFT, 10, 10, 210, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
    SendMessageA(hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    for (size_t i = 0; i < g_servers.size(); i++) {
        std::string btn = g_servers[i].ip + ":" + g_servers[i].port;
        HWND hBtn = CreateWindowExA(0, "BUTTON", btn.c_str(), WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON|BS_TEXT, 10, 40 + (i * 40), 200, 30, hwnd, (HMENU)(1000 + i), GetModuleHandle(NULL), NULL);
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
namespace TLog {
    static void enableAnsiConsole() {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0; GetConsoleMode(h, &mode);
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    static void WriteMessage(const char* tag, const char* fmt, ...) {
        char msg[1024]; va_list args; va_start(args, fmt); vsnprintf(msg, sizeof(msg), fmt, args); va_end(args);
        SYSTEMTIME st; GetLocalTime(&st);
        int h = st.wHour % 12 == 0 ? 12 : st.wHour % 12;
        const char* ap = st.wHour >= 12 ? "PM" : "AM";
        if (g_EnableConsole) {
            char prefix[128]; snprintf(prefix, sizeof(prefix), "[%02d:%02d %s] [%s] ", h, st.wMinute, ap, tag);
            printf("%s%s\n", prefix, parseConsoleMarkup(msg).c_str());
        } else if (TGameEnvironment_universe) {
            char colored[1200]; snprintf(colored, sizeof(colored), "<b><font color=#FF1493>%s</font></b>", msg);
            echo((int64_t*)new TString(colored), 0, 0, 0, "game", 0);
        }
    }
    void __cdecl echo(int64_t* p1, int64_t p2, int64_t p3, int64_t p4, const char* p5, char p6) {
        const char* msg = (p1 && *p1) ? (const char*)(*p1 + 8) : "";
        if (strstr(msg, "PluginMain")) return;
        if (g_DebugMode) printf("[echo_rgb] p2=%lld p3=%lld p4=%lld p5=%s p6=%d\n", p2, p3, p4, p5 ? p5 : "null", (int)p6);
        SYSTEMTIME st; GetLocalTime(&st);
        int h = st.wHour % 12 == 0 ? 12 : st.wHour % 12;
        printf("[%02d:%02d %s] [%s] %s\n", h, st.wMinute, st.wHour >= 12 ? "PM" : "AM", p5 ? p5 : "unity", parseConsoleMarkup(msg).c_str());
    }
}
struct hostent* WSAAPI Hooked_gethostbyname(const char* name) {
    if (name && (strstr(name, "con.quattroplay.com") || strstr(name, "con2.quattroplay.com") || strstr(name, "conworldsios.quattroplay.com") || strstr(name, "conworldsios2.quattroplay.com")))
        return True_gethostbyname("127.0.0.1");
    return True_gethostbyname(name);
}
static std::string cleanName(const std::string& name) {
    std::string n = (name.size() > 6 && name.substr(0, 6) == "weapon") ? name.substr(6) : name;
    std::string out; out.reserve(n.size());
    for (size_t i = 0; i < n.size(); i++) {
        if (n[i] == '%' && i + 3 < n.size() && isdigit(n[i+1]) && isdigit(n[i+2]) && isdigit(n[i+3])) { out += (char)strtol(n.substr(i+1, 3).c_str(), nullptr, 10); i += 3; }
        else out += n[i];
    }
    return out;
}
static std::string urlDecode(const std::string& s) {
    std::string out; out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && isxdigit(s[i+1]) && isxdigit(s[i+2])) { out += (char)strtol(s.substr(i+1, 2).c_str(), nullptr, 16); i += 2; }
        else out += s[i];
    }
    return out;
}

namespace TFileManager { std::string GetExeDir(); std::vector<std::string> GetLicensePaths(); DWORD WINAPI WatcherThread(LPVOID); }

namespace TScriptUniverse { 
    static TGraalVar* getOrCreateClass(const std::string& name) {
        if (g_weaponTable.count("class:" + name)) return g_weaponTable["class:" + name];
        TString* enc = (TString*)alloc(8);
        THashList_encodesimple(enc, new TString(name.c_str()));
        TGraalVar* obj = reinterpret_cast<TGraalVar*>(alloc(600));
        TGraalVar_Constructor(obj, enc);
        ((void(__fastcall*)(TGraalVar*, int64_t))(obj->vtable[20]))(obj, 1);
        TScriptUniverse_addStaticObject(TGameEnvironment_universe, obj);
        return g_weaponTable["class:" + name] = obj;
    }
    static TGraalVar* getOrCreateWeapon(const std::string& name) {
        if (g_weaponTable.count(name)) return g_weaponTable[name];
        TString* enc = (TString*)alloc(8);
        THashList_encodesimple(enc, new TString(name.c_str()));
        TGraalVar* obj = reinterpret_cast<TGraalVar*>(alloc(600));
        TGraalVar_Constructor(obj, enc);
        ((void(__fastcall*)(TGraalVar*, int64_t))(obj->vtable[20]))(obj, 2);
        TScriptUniverse_addStaticObject(TGameEnvironment_universe, obj);
        return g_weaponTable[name] = obj;
    }
    void addWeaponScriptRaw(const std::string& name, const std::string& bytes) {
        TGraalVar* obj = TScriptUniverse::getOrCreateWeapon(name);
        TLog::WriteMessage("bytecode", "Weapon %s loaded", cleanName(name).c_str());
        TGraalVar_SetScript(obj, new TString((char*)bytes.data(), bytes.size()));
    }
    void addClassRaw(const std::string& name, const std::string& bytes) {
        const std::string dname = name.find('%') != std::string::npos ? urlDecode(name) : name;
        TGraalVar* obj = TScriptUniverse::getOrCreateClass(dname);
        TLog::WriteMessage("bytecode", "Script %s loaded", cleanName(dname).c_str());
        TScriptUniverse_addClassScript(TGameEnvironment_universe, new TString(dname.c_str()), new TString((char*)bytes.data(), bytes.size()));
        g_classTable[dname] = true;
    }
    void patchBootstrap(std::string& s) {
        if (g_selectedHost.empty()) return;
        std::string exeDir = TFileManager::GetExeDir();
        auto checkPL = [](const std::string& p) { std::ifstream f(p); std::string l; return std::getline(f, l) && l == "true"; };
        auto replStr = [](std::string& s, const char* k, const char* v) { size_t p = s.find(k); if (p != std::string::npos) s.replace(p + strlen(k), s.find("\"", p + strlen(k)) - (p + strlen(k)), v); };
        auto replBool = [](std::string& s, const char* k, const char* v) { size_t p = s.find(k); if (p != std::string::npos) s.replace(p, strlen(k), v); };
        bool hasNP = g_selectedPort.size() >= 2 && g_selectedPort.substr(g_selectedPort.size() - 2) == "NP";
        bool hasPL = checkPL("license.packet") || checkPL("license/license.packet") || (!exeDir.empty() && (checkPL(exeDir + "\\license.packet") || checkPL(exeDir + "\\license\\license.packet")));
        if (hasNP) replBool(s, "this.loginnewprotocol = false;", "this.loginnewprotocol = true;");
        if (hasPL) replBool(s, "this.loginpacketlog = false;", "this.loginpacketlog = true;");
        replStr(s, "this.loginhost = \"", g_selectedHost.c_str());
        replStr(s, "this.loginport = \"", (hasNP ? g_selectedPort.substr(0, g_selectedPort.size() - 2) : g_selectedPort).c_str());
        if (!g_startParams.empty()) {
            s.find("serverstartconnect = \"") != std::string::npos
                ? replStr(s, "serverstartconnect = \"", g_startParams.c_str())
                : (void)(s += "\nserverstartconnect = \"" + g_startParams + "\";");
        }
    }
    void reloadBootstrap() {
        if (!g_startScript || g_bootstrapPath.empty()) return;
        std::ifstream f(g_bootstrapPath);
        if (!f.good()) return;
        std::string gs2((std::istreambuf_iterator<char>(f)), {});
        patchBootstrap(gs2);
        auto resp = GS2Context::Compile(gs2);
        TLog::WriteMessage("compiler", "Bootstrap reloaded");
        if (!resp.success) TLog::WriteMessage("GS2Parser", "<red>Script compiler output for Bootstrap (Internal)</red>:\n %s", resp.errors.empty() ? "Unknown error" : resp.errors[0].msg().c_str());
        TGraalVar_SetScript(g_startScript, new TString((char*)resp.bytecode.buffer(), resp.bytecode.length()));
    }
    void addWeaponScript(const std::string& name, const std::string& gs2_code) {
        TGraalVar* obj = TScriptUniverse::getOrCreateWeapon(name);
        auto resp = GS2Context::Compile(gs2_code);
        TLog::WriteMessage("compiler", "Weapon/GUI-script %s added/updated", name.c_str());
        if (!resp.success) TLog::WriteMessage("compiler", "<red>Script compiler output for %s</red>:\n %s", name.c_str(), resp.errors.empty() ? "unknown" : resp.errors[0].msg().c_str());
        TGraalVar_SetScript(obj, new TString((char*)resp.bytecode.buffer(), resp.bytecode.length()));
    }
    void addClassScript(const std::string& name, const std::string& gs2_code) {
        TGraalVar* obj = TScriptUniverse::getOrCreateClass(name);
        auto resp = GS2Context::Compile(gs2_code);
        TLog::WriteMessage("compiler", "Script %s added/updated", name.c_str());
        if (!resp.success) { TLog::WriteMessage("compiler", "<red>Script compiler output for %s</red>:\n %s", name.c_str(), resp.errors.empty() ? "unknown" : resp.errors[0].msg().c_str()); return; }
        TScriptUniverse_addClassScript(TGameEnvironment_universe, new TString(name.c_str()), new TString((char*)resp.bytecode.buffer(), resp.bytecode.length()));
        g_classTable[name] = true;
    }
    static void scanFolders(const std::string& folder, bool isClass, bool isBytecode) {
        int extLen = isBytecode ? 6 : 4;
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA((folder + (isBytecode ? "\\*.gs2bc" : "\\*.gs2")).c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE) return;
        do {
            std::string fname = fd.cFileName, name = fname.substr(0, fname.size() - extLen);
            std::ifstream f(folder + "\\" + fname, isBytecode ? (std::ios::in|std::ios::binary) : std::ios::in);
            if (!f.good()) continue;
            std::string content((std::istreambuf_iterator<char>(f)), {});
            TLog::WriteMessage(isBytecode ? "bytecode" : "compiler", "Loading %s as '%s'", fname.c_str(), isBytecode ? cleanName(name).c_str() : name.c_str());
    if (isBytecode) { isClass ? TScriptUniverse::addClassRaw(name, content) : TScriptUniverse::addWeaponScriptRaw(name, content); }
            else { isClass ? TScriptUniverse::addClassScript(name, content) : TScriptUniverse::addWeaponScript(name, content); }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
}

namespace TServerList {
    void __cdecl enterNextConnectorMode(int mode) {
        TGameEnvironment_universe = *(void**)(base + (0x7FF97FC191B0 - 0x7FF97E840000));
        TString* enc = (TString*)alloc(8);
        THashList_encodesimple(enc, new TString("StartScript_Connector"));
        TGraalVar* sc = reinterpret_cast<TGraalVar*>(alloc(600));
        TGraalVar_Constructor(sc, enc);
        ((void(__fastcall*)(TGraalVar*, int64_t))(sc->vtable[20]))(sc, 2);
        TScriptUniverse_addStaticObject(TGameEnvironment_universe, sc);
        g_startScript = sc;
        std::string gs2, exeDir = TFileManager::GetExeDir();
        bool found = false;
        std::vector<std::string> paths;
        if (!exeDir.empty()) { paths.push_back(exeDir + "\\bootstrap.gs2"); paths.push_back(exeDir + "\\license\\bootstrap.gs2"); }
        paths.push_back("bootstrap.gs2"); paths.push_back("license/bootstrap.gs2");
        for (const auto& p : paths) {
            std::ifstream f(p);
            if (f.good()) { std::stringstream buf; buf << f.rdbuf(); gs2 = buf.str(); g_bootstrapPath = p; found = true; break; }
        }
        if (!found) {
            HMODULE hm = GetModuleHandleA("GLauncherW.dll");
            HRSRC hr = FindResourceA(hm, MAKEINTRESOURCE(101), RT_RCDATA);
            if (!hr) return;
            HGLOBAL hl = LoadResource(hm, hr);
            gs2 = std::string((char*)LockResource(hl), SizeofResource(hm, hr));
        }
        TScriptUniverse::patchBootstrap(gs2);
        auto resp = GS2Context::Compile(gs2);
        if (!resp.success) TLog::WriteMessage("GS2Parser", "Compile error: %s", resp.errors.empty() ? "Unknown error" : resp.errors[0].msg().c_str());
        TGraalVar_SetScript(sc, new TString((char*)resp.bytecode.buffer(), resp.bytecode.length()));
        if (g_DevMode >= 0) {
            g_bytecodeWeaponDir = exeDir.empty() ? "bytecode\\weapons" : exeDir + "\\bytecode\\weapons";
            g_bytecodeClassDir  = exeDir.empty() ? "bytecode\\classes" : exeDir + "\\bytecode\\classes";
            TScriptUniverse::scanFolders(g_bytecodeClassDir, true, true);
            TScriptUniverse::scanFolders(g_bytecodeWeaponDir, false, true);
        }
        if (g_DevMode >= 1) {
            g_weaponDir = exeDir.empty() ? "offline\\weapons" : exeDir + "\\offline\\weapons";
            g_classDir  = exeDir.empty() ? "offline\\classes" : exeDir + "\\offline\\classes";
            TScriptUniverse::scanFolders(g_weaponDir, false, false);
            TScriptUniverse::scanFolders(g_classDir, true, false);
        }
        if (g_DevMode >= 0) {
            g_watcherRunning = true;
            g_watcherThread = CreateThread(NULL, 0, TFileManager::WatcherThread, NULL, 0, NULL);
            TLog::WriteMessage("compiler", "Watching offline/, bytecode/, and bootstrap.gs2 for changes");
        }
    }
}
namespace TFileManager {
    DWORD WINAPI WatcherThread(LPVOID) {
        std::map<std::string, std::string> lastHash;
        auto removeScript = [](const std::string& name, bool isClass) {
                if (!isClass && g_weaponTable.count(name)) {
                TGraalVar_SetScript(g_weaponTable[name], new TString("", 0));
                g_weaponTable.erase(name);
                TLog::WriteMessage("compiler", "Weapon %s deleted", name.c_str());
            } else if (isClass && g_classTable.count(name)) {
                TScriptUniverse_addClassScript(TGameEnvironment_universe, new TString(name.c_str()), new TString("", 0));
                g_classTable.erase(name);
                TLog::WriteMessage("compiler", "Script %s deleted", name.c_str());
            }
        };
        auto watchDir = [&](const std::string& path, const std::string& ext, bool isBinary, bool isClass,
            std::function<void(const std::string&, const std::string&)> addFn,
            std::function<void(const std::string&, bool)> removeFn) {
            HANDLE h = CreateFileA(path.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
            if (h == INVALID_HANDLE_VALUE) return;
            char buf[4096]; DWORD returned;
            while (g_watcherRunning) {
                if (!ReadDirectoryChangesW(h, buf, sizeof(buf), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE|FILE_NOTIFY_CHANGE_FILE_NAME, &returned, NULL, NULL)) break;
                FILE_NOTIFY_INFORMATION* info = (FILE_NOTIFY_INFORMATION*)buf;
                do {
                    char fname[MAX_PATH];
                    WideCharToMultiByte(CP_ACP, 0, info->FileName, info->FileNameLength / sizeof(WCHAR), fname, MAX_PATH, NULL, NULL);
                    fname[info->FileNameLength / sizeof(WCHAR)] = '\0';
                    std::string fn(fname);
                    if (fn.size() > ext.size() && fn.substr(fn.size() - ext.size()) == ext) {
                        std::string name = fn.substr(0, fn.size() - ext.size());
                        if (info->Action == FILE_ACTION_MODIFIED || info->Action == FILE_ACTION_ADDED) {
                            Sleep(100);
                            std::ifstream f(path + "\\" + fn, isBinary ? (std::ios::in|std::ios::binary) : std::ios::in);
                            if (f.good()) {
                                std::string content((std::istreambuf_iterator<char>(f)), {});
                                std::string key = (isClass ? "class:" : "weapon:") + name + ext, hash = hashFile(content);
                                if (!lastHash.count(key) || lastHash[key] != hash) { lastHash[key] = hash; if (TGameEnvironment_universe) addFn(name, content); }
                            }
                        } else if (info->Action == FILE_ACTION_REMOVED) removeFn(name, isClass);
                    }
                    if (!info->NextEntryOffset) break;
                    info = (FILE_NOTIFY_INFORMATION*)((char*)info + info->NextEntryOffset);
                } while (true);
            }
            CloseHandle(h);
        };
        auto watchBootstrap = [&]() {
            if (g_bootstrapPath.empty()) return;
            size_t sep = g_bootstrapPath.rfind('\\');
            std::string dir = sep != std::string::npos ? g_bootstrapPath.substr(0, sep) : ".";
            std::string bname = sep != std::string::npos ? g_bootstrapPath.substr(sep + 1) : g_bootstrapPath;
            HANDLE h = CreateFileA(dir.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
            if (h == INVALID_HANDLE_VALUE) return;
            char buf[4096]; DWORD returned; std::string lastBsHash;
            while (g_watcherRunning) {
                if (!ReadDirectoryChangesW(h, buf, sizeof(buf), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE, &returned, NULL, NULL)) break;
                FILE_NOTIFY_INFORMATION* info = (FILE_NOTIFY_INFORMATION*)buf;
                do {
                    char fname[MAX_PATH];
                    WideCharToMultiByte(CP_ACP, 0, info->FileName, info->FileNameLength / sizeof(WCHAR), fname, MAX_PATH, NULL, NULL);
                    fname[info->FileNameLength / sizeof(WCHAR)] = '\0';
                    if (_stricmp(fname, bname.c_str()) == 0 && (info->Action == FILE_ACTION_MODIFIED || info->Action == FILE_ACTION_ADDED)) {
                        Sleep(100);
                        std::ifstream f(g_bootstrapPath);
                        if (f.good()) {
                            std::string content((std::istreambuf_iterator<char>(f)), {}), hash = hashFile(content);
                            if (lastBsHash != hash) { lastBsHash = hash; TScriptUniverse::reloadBootstrap(); }
                        }
                    }
                    if (!info->NextEntryOffset) break;
                    info = (FILE_NOTIFY_INFORMATION*)((char*)info + info->NextEntryOffset);
                } while (true);
            }
            CloseHandle(h);
        };
    std::thread t1([&]{ watchDir(g_classDir, ".gs2", false, true, TScriptUniverse::addClassScript, removeScript); });
        std::thread t2([&]{ watchDir(g_bytecodeWeaponDir, ".gs2bc", true, false, TScriptUniverse::addWeaponScriptRaw, removeScript); });
        std::thread t3([&]{ watchDir(g_bytecodeClassDir, ".gs2bc", true, true, TScriptUniverse::addClassRaw, removeScript); });
        std::thread t4([&]{ watchBootstrap(); });
        watchDir(g_weaponDir, ".gs2", false, false, TScriptUniverse::addWeaponScript, removeScript);
        t1.join(); t2.join(); t3.join(); t4.join();
        return 0;
    }
    std::string GetExeDir() {
        char p[MAX_PATH];
        return GetModuleFileNameA(NULL, p, MAX_PATH) ? (p[strrchr(p, '\\') - p] = '\0', std::string(p)) : std::string();
    }
    std::vector<std::string> GetLicensePaths() {
        std::vector<std::string> paths;
        std::string d = GetExeDir();
        if (!d.empty()) { paths.push_back(d + "\\license.graal"); paths.push_back(d + "\\license\\license.graal"); }
        paths.push_back("license.graal");
        paths.push_back("license/license.graal");
        return paths;
    }

    static std::wstring patchPathW(const wchar_t* raw) {
        if (!raw) return {};
        std::wstring path(raw);
        for (auto& c : path) if (c == L'\\') c = L'/';
        if (path.find(L"GraalOnline Worlds") == std::wstring::npos) return {};
        if (g_oldBase.empty()) g_oldBase = path.substr(0, path.find(L"GraalOnline Worlds/") + wcslen(L"GraalOnline Worlds/"));
        std::wstring patched = g_newBase + path.substr(g_oldBase.size());
        for (auto& c : patched) if (c == L'/') c = L'\\';
        std::wstring dir = patched.substr(0, patched.rfind(L'\\'));
        if (!g_madeDirs.count(dir)) {
            for (size_t i = 0; i < dir.size(); i++) if (dir[i] == L'\\' && i > 2) { std::wstring sub = dir.substr(0, i); True_CreateDirectoryW(sub.c_str(), NULL); }
            True_CreateDirectoryW(dir.c_str(), NULL);
            g_madeDirs.insert(dir);
        }
        return patched;
    }
    static HANDLE WINAPI Hook_CreateFileW(LPCWSTR f, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD fl, HANDLE t) { std::wstring p = patchPathW(f); return True_CreateFileW(p.empty() ? f : p.c_str(), a, s, sa, c, fl, t); }
    static HANDLE WINAPI Hook_CreateFile2(LPCWSTR f, DWORD a, DWORD s, DWORD c, LPCREATEFILE2_EXTENDED_PARAMETERS e) { std::wstring p = patchPathW(f); return True_CreateFile2(p.empty() ? f : p.c_str(), a, s, c, e); }
    static DWORD WINAPI Hook_GetFileAttributesW(LPCWSTR p) {
        if (g_DebugMode) { char tmp[MAX_PATH]; WideCharToMultiByte(CP_ACP, 0, p, -1, tmp, MAX_PATH, NULL, NULL); TLog::WriteMessage("AttrW", "%s", tmp); }
        std::wstring pw = patchPathW(p); return True_GetFileAttributesW(pw.empty() ? p : pw.c_str());
    }
    static DWORD WINAPI Hook_GetFileAttributesA(LPCSTR p) {
        if (g_DebugMode) TLog::WriteMessage("AttrA", "%s", p);
        if (p && strstr(p, "GraalOnline Worlds")) {
            wchar_t w[MAX_PATH]; MultiByteToWideChar(CP_ACP, 0, p, -1, w, MAX_PATH);
            std::wstring pw = patchPathW(w);
            if (!pw.empty()) { char n[MAX_PATH]; WideCharToMultiByte(CP_ACP, 0, pw.c_str(), -1, n, MAX_PATH, NULL, NULL); return True_GetFileAttributesA(n); }
        }
        return True_GetFileAttributesA(p);
    }
    static HANDLE WINAPI Hook_FindFirstFileW(LPCWSTR p, LPWIN32_FIND_DATAW fd) {
        if (g_DebugMode) { char tmp[MAX_PATH]; WideCharToMultiByte(CP_ACP, 0, p, -1, tmp, MAX_PATH, NULL, NULL); TLog::WriteMessage("FindW", "%s", tmp); }
        std::wstring pw = patchPathW(p); return True_FindFirstFileW(pw.empty() ? p : pw.c_str(), fd);
    }
    static HANDLE WINAPI Hook_FindFirstFileA(LPCSTR p, LPWIN32_FIND_DATAA fd) {
        if (g_DebugMode) TLog::WriteMessage("FindA", "%s", p);
        if (p && strstr(p, "GraalOnline Worlds")) {
            wchar_t w[MAX_PATH]; MultiByteToWideChar(CP_ACP, 0, p, -1, w, MAX_PATH);
            std::wstring pw = patchPathW(w);
            if (!pw.empty()) { char n[MAX_PATH]; WideCharToMultiByte(CP_ACP, 0, pw.c_str(), -1, n, MAX_PATH, NULL, NULL); return True_FindFirstFileA(n, fd); }
        }
        return True_FindFirstFileA(p, fd);
    }
    static BOOL WINAPI Hook_CreateDirectoryW(LPCWSTR p, LPSECURITY_ATTRIBUTES sa) {
        if (p) {
            std::wstring path(p);
            if (path.find(L"GraalOnline Worlds") != std::wstring::npos) {
                for (auto& c : path) if (c == L'\\') c = L'/';
                if (g_oldBase.empty()) g_oldBase = path.substr(0, path.find(L"GraalOnline Worlds/") + wcslen(L"GraalOnline Worlds/"));
                std::wstring pt = g_newBase + path.substr(g_oldBase.size());
                for (auto& c : pt) if (c == L'/') c = L'\\';
                return True_CreateDirectoryW(pt.c_str(), sa);
            }
        }
        return True_CreateDirectoryW(p, sa);
    }
}
namespace TClient {
    void initialize(HMODULE hModule) {
        HMODULE hGraal = NULL;
        for (int i = 0; i < 50 && !hGraal; i++) { hGraal = GetModuleHandleA("Graal3DEngine.dll"); if (!hGraal) Sleep(50); }
        if (!hGraal) return;
        base = reinterpret_cast<uintptr_t>(hGraal);
        void(__cdecl* TServerList_enterNextConnectorMode)(int);
        *((uintptr_t*)&TServerList_enterNextConnectorMode)  = base + (0x7FF97F303210 - 0x7FF97E840000);
        *((uintptr_t*)&TGraalVar_Constructor)               = base + (0x7FF97F319BF0 - 0x7FF97E840000);
        *((uintptr_t*)&TGraalVar_SetScript)                 = base + (0x7FF97F31FCD0 - 0x7FF97E840000);
        *((uintptr_t*)&THashList_encodesimple)              = base + (0x7FF97F194430 - 0x7FF97E840000);
        *((uintptr_t*)&TScriptUniverse_addStaticObject)     = base + (0x7FF97F342E80 - 0x7FF97E840000);
        *((uintptr_t*)&TScriptUniverse_addClassScript)      = base + (0x7FF97F342C60 - 0x7FF97E840000);
        *((uintptr_t*)&TLog_echo)                           = base + (0x7FF97F1BD350 - 0x7FF97E840000);
        HMODULE hWs2 = GetModuleHandleA("ws2_32.dll");
        if (!hWs2) hWs2 = LoadLibraryA("ws2_32.dll");
        if (hWs2) {
            True_gethostbyname = (struct hostent*(WSAAPI*)(const char*))GetProcAddress(hWs2, "gethostbyname");
            if (True_gethostbyname) {
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());
                DetourAttach(&(PVOID&)True_gethostbyname, Hooked_gethostbyname);
                DetourTransactionCommit();
            }
        }
        HMODULE hK = GetModuleHandleA("kernel32.dll");
        True_CreateFileW        = (HANDLE(WINAPI*)(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE))GetProcAddress(hK, "CreateFileW");
        True_CreateFile2        = (HANDLE(WINAPI*)(LPCWSTR,DWORD,DWORD,DWORD,LPCREATEFILE2_EXTENDED_PARAMETERS))GetProcAddress(hK, "CreateFile2");
        True_CreateDirectoryW   = (BOOL(WINAPI*)(LPCWSTR,LPSECURITY_ATTRIBUTES))GetProcAddress(hK, "CreateDirectoryW");
        True_GetFileAttributesW = (DWORD(WINAPI*)(LPCWSTR))GetProcAddress(hK, "GetFileAttributesW");
        True_GetFileAttributesA = (DWORD(WINAPI*)(LPCSTR))GetProcAddress(hK, "GetFileAttributesA");
        True_FindFirstFileW     = (HANDLE(WINAPI*)(LPCWSTR,LPWIN32_FIND_DATAW))GetProcAddress(hK, "FindFirstFileW");
        True_FindFirstFileA     = (HANDLE(WINAPI*)(LPCSTR,LPWIN32_FIND_DATAA))GetProcAddress(hK, "FindFirstFileA");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        if (True_CreateFileW)        DetourAttach(&(PVOID&)True_CreateFileW, TFileManager::Hook_CreateFileW);
        if (True_CreateFile2)        DetourAttach(&(PVOID&)True_CreateFile2, TFileManager::Hook_CreateFile2);
        if (True_CreateDirectoryW)   DetourAttach(&(PVOID&)True_CreateDirectoryW, TFileManager::Hook_CreateDirectoryW);
        if (True_GetFileAttributesW) DetourAttach(&(PVOID&)True_GetFileAttributesW, TFileManager::Hook_GetFileAttributesW);
        if (True_GetFileAttributesA) DetourAttach(&(PVOID&)True_GetFileAttributesA, TFileManager::Hook_GetFileAttributesA);
        if (True_FindFirstFileW)     DetourAttach(&(PVOID&)True_FindFirstFileW, TFileManager::Hook_FindFirstFileW);
        if (True_FindFirstFileA)     DetourAttach(&(PVOID&)True_FindFirstFileA, TFileManager::Hook_FindFirstFileA);
        DetourTransactionCommit();
        Memory::hook((void*)TServerList_enterNextConnectorMode, (void*)TServerList::enterNextConnectorMode, 17);
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\GLauncher", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD v = 0, sz = sizeof(v);
            if (RegQueryValueExA(hKey, "EnableConsole", NULL, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS) g_EnableConsole = v == 1;
            RegCloseKey(hKey);
        }
        if (g_EnableConsole) { SetupConsole(); TLog::enableAnsiConsole(); Memory::hook((void*)TLog_echo, (void*)TLog::echo, 18); g_TLog_echoed = true; }
        std::string exeDir = TFileManager::GetExeDir();
        std::wstring exeDirW(exeDir.begin(), exeDir.end());
        g_newBase = exeDirW + L"\\Reborn_cache\\";
        int argc; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i < argc; i++) {
            std::wstring arg(argv[i]);
            if (arg.substr(0, 8) == L"graal://" || arg.substr(0, 9) == L"graal3://") {
                std::wstring url = arg.substr(arg.find(L"//") + 2);
                size_t sl = url.find(L'/');
                g_startParams = std::string(url.begin(), sl != std::wstring::npos ? url.begin() + sl : url.end());
            }
        }
        LocalFree(argv);
        g_hHookThread = CreateThread(NULL, 0, HotkeyThread, hModule, 0, &g_HookThreadId);
        bool showDialog = false;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\GLauncher", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD v = 0, sz = sizeof(v);
            if (RegQueryValueExA(hKey, "SelectServer", NULL, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS && v == 1) showDialog = true;
            RegCloseKey(hKey);
        }
        if (showDialog) { for (const auto& f : TFileManager::GetLicensePaths()) if (ShowServerDialog(f)) break; }
        else {
            for (const auto& f : TFileManager::GetLicensePaths()) {
                std::ifstream in(f); if (in.fail()) continue;
                std::string ip, port;
                if (std::getline(in, ip) && !ip.empty() && std::getline(in, port)) { g_selectedHost = ip; g_selectedPort = port; break; }
            }
        }
        if (g_EnableConsole) ShowWindow(GetConsoleWindow(), SW_SHOW);
        char exePath[MAX_PATH]; GetModuleFileNameA(NULL, exePath, MAX_PATH);
        std::string cmd = std::string("\"") + exePath + "\" \"%1\"", icon = std::string(exePath) + ",0";
        TLog::WriteMessage("glauncher", "Registering graal:// protocol...");
        if (RegCreateKeyExA(HKEY_CLASSES_ROOT, "graal", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            RegSetValueExA(hKey, NULL, 0, REG_SZ, (BYTE*)"URL:GRAAL Protocol", 17);
            DWORD ef = 2; RegSetValueExA(hKey, "EditFlags", 0, REG_DWORD, (BYTE*)&ef, 4);
            RegSetValueExA(hKey, "URL Protocol", 0, REG_SZ, (BYTE*)"", 1);
            RegCloseKey(hKey);
            RegCreateKeyExA(HKEY_CLASSES_ROOT, "graal\\DefaultIcon", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
            RegSetValueExA(hKey, NULL, 0, REG_SZ, (BYTE*)icon.c_str(), icon.length() + 1);
            RegCloseKey(hKey);
            RegCreateKeyExA(HKEY_CLASSES_ROOT, "graal\\Shell\\open\\command", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL);
            RegSetValueExA(hKey, NULL, 0, REG_SZ, (BYTE*)cmd.c_str(), cmd.length() + 1);
            RegCloseKey(hKey);
            TLog::WriteMessage("glauncher", "graal:// Registered to %s", exePath);
        }
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) TClient::initialize(hModule);
    return TRUE;
}