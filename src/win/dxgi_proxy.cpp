#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <windows.h>
#include <detours.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wininet.h>
#include <shellapi.h>
#include <string>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <thread>
#include <chrono>
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ws2_32.lib")

static HMODULE g_hRealDxgi = NULL;
static HMODULE g_hThisModule = NULL;
static bool g_bKeepAlive = true;
static bool g_bHooksAttached = false;
static HHOOK g_hKeyboardHook = NULL;
static uint16_t g_httpServerPort = 0;


struct LicenseDataStruct {
    LicenseDataStruct() : hostPort("14900"), targetExe("") { }
    std::unordered_map<std::string, std::string> hostResolves;
    std::string hostPort;
    std::string targetExe;
};

static LicenseDataStruct LicenseData{};
static std::string ogHostNames[6];

static void InitHostNames() {
    ogHostNames[0] = "loginserverworlds.graalonline.com";
    ogHostNames[1] = "listserver.graalonline.com";
    ogHostNames[2] = "loginserver2.graalonline.com";
    ogHostNames[3] = "loginserver3.graalonline.com";
}

static std::string GetExeDirectory() {
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH)) {
        char* lastSlash = strrchr(exePath, '\\');
        if (lastSlash) {
            *lastSlash = '\0';
            return std::string(exePath);
        }
    }
    return std::string();
}

static std::vector<std::string> GetLicenseFilePaths() {
    std::vector<std::string> paths;
    std::string exeDir = GetExeDirectory();
    if (!exeDir.empty()) {
        paths.push_back(exeDir + "\\license.graal");
        paths.push_back(exeDir + "\\license\\license.graal");
    }
    paths.push_back("license.graal");
    paths.push_back("license/license.graal");
    return paths;
}


bool ReadLicensesFile(const std::string& file) {
    LicenseData = {};
    std::ifstream input(file);
    if (input.fail()) return false;
    std::string line;
    auto idx = 0;
    while (std::getline(input, line)) {
        if (idx == 0) LicenseData.hostResolves[ogHostNames[0]] = line;
        else if (idx == 1) LicenseData.hostPort = line;
        else if (idx == 2) {
            if (line.length() > 4 && line.substr(line.length() - 4) == ".exe") {
                LicenseData.targetExe = line;
            } else {
                LicenseData.hostResolves[ogHostNames[1]] = line;
            }
        }
        else if (idx == 5) LicenseData.hostResolves[ogHostNames[2]] = line;
        else if (idx == 6) LicenseData.hostResolves[ogHostNames[3]] = line;
        ++idx;
    }
    input.close();
    return LicenseData.hostResolves.size() > 0;
}

struct ServerEntry {
    std::string ip;
    std::string port;
};

static std::vector<ServerEntry> g_servers;
static int g_selectedServer = -1;

LRESULT CALLBACK ServerDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wParam) >= 1000 && LOWORD(wParam) < 1000 + (int)g_servers.size()) {
                g_selectedServer = LOWORD(wParam) - 1000;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            g_selectedServer = -1;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool ShowServerSelectDialog(const std::string& licenseFile) {
    std::ifstream input(licenseFile);
    if (input.fail()) return false;
    g_servers.clear();
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        ServerEntry entry;
        entry.ip = line;
        if (std::getline(input, line)) {
            entry.port = line;
            g_servers.push_back(entry);
        }
    }
    input.close();
    if (g_servers.empty()) return false;
    HICON hIcon = (HICON)LoadImageA(NULL, MAKEINTRESOURCEA(32512), IMAGE_ICON, 0, 0, LR_SHARED);
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH)) {
        hIcon = ExtractIconA(GetModuleHandle(NULL), exePath, 0);
    }
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = ServerDialogProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "GLauncherServerSelect";
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = hIcon;
    wc.hIconSm = hIcon;
    RegisterClassExA(&wc);
    HWND hwnd = CreateWindowExA(WS_EX_DLGMODALFRAME, "GLauncherServerSelect", "GLauncher - Select Server", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 400, 100 + (g_servers.size() * 40), NULL, NULL, GetModuleHandle(NULL), NULL);
    if (!hwnd) return false;
    SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HWND hLabel = CreateWindowExA(0, "STATIC", "Select a server:", WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 10, 380, 20, hwnd, NULL, GetModuleHandle(NULL), NULL);
    SendMessageA(hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    for (size_t i = 0; i < g_servers.size(); i++) {
        std::string btnText = g_servers[i].ip + ":" + g_servers[i].port;
        HWND hBtn = CreateWindowExA(0, "BUTTON", btnText.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_TEXT, 10, 40 + (i * 40), 280, 30, hwnd, (HMENU)(1000 + i), GetModuleHandle(NULL), NULL);
        SendMessageA(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    MSG msg;
    g_selectedServer = -1;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (g_selectedServer < 0) return false;
    LicenseData = {};
    LicenseData.hostResolves[ogHostNames[0]] = g_servers[g_selectedServer].ip;
    LicenseData.hostResolves[ogHostNames[1]] = g_servers[g_selectedServer].ip;
    LicenseData.hostPort = g_servers[g_selectedServer].port;
    return true;
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* pKbd = (KBDLLHOOKSTRUCT*)lParam;
        if (pKbd->vkCode == 'L' && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
            HKEY hKey;
            if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\GLauncher", 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                DWORD value = 0;
                DWORD size = sizeof(value);
                RegQueryValueExA(hKey, "SelectServer", NULL, NULL, (BYTE*)&value, &size);
                value = (value == 1) ? 0 : 1;
                RegSetValueExA(hKey, "SelectServer", 0, REG_DWORD, (BYTE*)&value, sizeof(value));
                RegCloseKey(hKey);
                if (value == 1) {
                    MessageBoxA(NULL, "Server selection enabled. Dialog will show on every launch.", "GLauncher", MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBoxA(NULL, "Server selection disabled.", "GLauncher", MB_OK | MB_ICONINFORMATION);
                }
            }
        }
    }
    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

HMODULE (WINAPI * True_LoadLibraryA)(LPCSTR lpLibFileName) = LoadLibraryA;
HMODULE (WINAPI * True_LoadLibraryW)(LPCWSTR lpLibFileName) = LoadLibraryW;
HMODULE (WINAPI * True_LoadLibraryExA)(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) = LoadLibraryExA;
FARPROC (WINAPI * True_GetProcAddress)(HMODULE hModule, LPCSTR lpProcName) = GetProcAddress;
hostent* (WSAAPI * True_gethostbyname)(const char* name) = gethostbyname;
int (WSAAPI * True_connect)(SOCKET s, const sockaddr* name, int namelen) = connect;
int (WSAAPI * True_WSAConnect)(SOCKET s, const sockaddr* name, int namelen, LPWSABUF lpCallerData, LPWSABUF lpCalleeData, LPQOS lpSQOS, LPQOS lpGQOS) = WSAConnect;
int (WSAAPI * True_getaddrinfo)(PCSTR pNodeName, PCSTR pServiceName, const ADDRINFOA* pHints, PADDRINFOA* ppResult) = getaddrinfo;
SOCKET (WSAAPI * True_socket)(int af, int type, int protocol) = socket;
SOCKET (WSAAPI * True_WSASocketA)(int af, int type, int protocol, LPWSAPROTOCOL_INFOA lpProtocolInfo, GROUP g, DWORD dwFlags) = WSASocketA;
HINTERNET (WINAPI * True_InternetOpenA)(LPCSTR lpszAgent, DWORD dwAccessType, LPCSTR lpszProxy, LPCSTR lpszProxyBypass, DWORD dwFlags) = InternetOpenA;
HINTERNET (WINAPI * True_InternetConnectA)(HINTERNET hInternet, LPCSTR lpszServerName, INTERNET_PORT nServerPort, LPCSTR lpszUsername, LPCSTR lpszPassword, DWORD dwService, DWORD dwFlags, DWORD_PTR dwContext) = InternetConnectA;

HMODULE WINAPI Hooked_LoadLibraryA(LPCSTR lpLibFileName) {
    return True_LoadLibraryA(lpLibFileName);
}

HMODULE WINAPI Hooked_LoadLibraryW(LPCWSTR lpLibFileName) {
    return True_LoadLibraryW(lpLibFileName);
}

HMODULE WINAPI Hooked_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
    return True_LoadLibraryExA(lpLibFileName, hFile, dwFlags);
}

FARPROC WINAPI Hooked_GetProcAddress(HMODULE hModule, LPCSTR lpProcName) {
    return True_GetProcAddress(hModule, lpProcName);
}

hostent* WSAAPI Hooked_gethostbyname(const char* name) {
    if (name) {
        if (strstr(name, "con.quattroplay.com") || strstr(name, "con2.quattroplay.com") || 
            strstr(name, "conworldsios.quattroplay.com") || strstr(name, "conworldsios2.quattroplay.com")) {
            return True_gethostbyname("127.0.0.1");
        }
        if (!LicenseData.hostResolves.empty()) {
            auto it = LicenseData.hostResolves.find(name);
            if (it != LicenseData.hostResolves.end()) {
                return True_gethostbyname(it->second.c_str());
            }
            if (strstr(name, "loginserver") && strstr(name, "graalonline.com")) {
                if (LicenseData.hostResolves.find(ogHostNames[0]) != LicenseData.hostResolves.end()) {
                    const auto& redirectHost = LicenseData.hostResolves[ogHostNames[0]];
                    return True_gethostbyname(redirectHost.c_str());
                }
            }
            if (strstr(name, "conworlds") || strstr(name, "quattroplay.com")) {
                if (LicenseData.hostResolves.find(ogHostNames[4]) != LicenseData.hostResolves.end()) {
                    const auto& redirectHost = LicenseData.hostResolves[ogHostNames[4]];
                    return True_gethostbyname(redirectHost.c_str());
                }
            }
        }
    }
    return True_gethostbyname(name);
}

int WSAAPI Hooked_getaddrinfo(PCSTR pNodeName, PCSTR pServiceName, const ADDRINFOA* pHints, PADDRINFOA* ppResult) {
    if (pNodeName && !LicenseData.hostResolves.empty()) {
        auto it = LicenseData.hostResolves.find(pNodeName);
        if (it != LicenseData.hostResolves.end()) {
            return True_getaddrinfo(it->second.c_str(), pServiceName, pHints, ppResult);
        }
        if (strstr(pNodeName, "loginserver") && strstr(pNodeName, "graalonline.com")) {
            if (LicenseData.hostResolves.find(ogHostNames[0]) != LicenseData.hostResolves.end()) {
                const auto& redirectHost = LicenseData.hostResolves[ogHostNames[0]];
                return True_getaddrinfo(redirectHost.c_str(), pServiceName, pHints, ppResult);
            }
        }
        if (strstr(pNodeName, "conworlds") || strstr(pNodeName, "quattroplay.com")) {
            if (LicenseData.hostResolves.find(ogHostNames[4]) != LicenseData.hostResolves.end()) {
                const auto& redirectHost = LicenseData.hostResolves[ogHostNames[4]];
                return True_getaddrinfo(redirectHost.c_str(), pServiceName, pHints, ppResult);
            }
        }
    }
    return True_getaddrinfo(pNodeName, pServiceName, pHints, ppResult);
}

SOCKET WSAAPI Hooked_socket(int af, int type, int protocol) {
    return True_socket(af, type, protocol);
}

SOCKET WSAAPI Hooked_WSASocketA(int af, int type, int protocol, LPWSAPROTOCOL_INFOA lpProtocolInfo, GROUP g, DWORD dwFlags) {
    return True_WSASocketA(af, type, protocol, lpProtocolInfo, g, dwFlags);
}

int WSAAPI Hooked_connect(SOCKET s, const sockaddr* name, int namelen) {
    struct sockaddr_in *addr = (sockaddr_in *)name;
    if (addr && addr->sin_family == AF_INET) {
        uint16_t port = ntohs(addr->sin_port);
        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr->sin_addr, ipStr, INET_ADDRSTRLEN);
    if (strcmp(ipStr, "127.0.0.1") == 0 && (port == 80 || port == 443) && g_httpServerPort != 0) {
        addr->sin_port = htons(g_httpServerPort);
    }
        else if (port >= 14000 && port < 15000 && !LicenseData.hostPort.empty() && !LicenseData.hostResolves.empty()) {
            const auto& redirectIP = LicenseData.hostResolves.find(ogHostNames[0]);
            if (redirectIP != LicenseData.hostResolves.end()) {
                uint16_t newPort = std::stoi(LicenseData.hostPort);
                struct sockaddr_in newAddr;
                memset(&newAddr, 0, sizeof(newAddr));
                newAddr.sin_family = AF_INET;
                newAddr.sin_port = htons(newPort);
                if (inet_pton(AF_INET, redirectIP->second.c_str(), &newAddr.sin_addr) == 1) {
                    *addr = newAddr;
                } else {
                    addr->sin_port = htons(newPort);
                }
            } else {
                uint16_t newPort = std::stoi(LicenseData.hostPort);
                if (newPort != port) {
                    addr->sin_port = htons(newPort);
                }
            }
        }
    }
    return True_connect(s, (struct sockaddr *)addr, namelen);
}

int WSAAPI Hooked_WSAConnect(SOCKET s, const sockaddr* name, int namelen, LPWSABUF lpCallerData, LPWSABUF lpCalleeData, LPQOS lpSQOS, LPQOS lpGQOS) {
    struct sockaddr_in *addr = (sockaddr_in *)name;
    if (addr && addr->sin_family == AF_INET) {
        uint16_t port = ntohs(addr->sin_port);
        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr->sin_addr, ipStr, INET_ADDRSTRLEN);
        if (port >= 14000 && port < 15000 && !LicenseData.hostPort.empty() && !LicenseData.hostResolves.empty()) {
            const auto& redirectIP = LicenseData.hostResolves.find(ogHostNames[0]);
            if (redirectIP != LicenseData.hostResolves.end()) {
                uint16_t newPort = std::stoi(LicenseData.hostPort);
                struct sockaddr_in newAddr;
                memset(&newAddr, 0, sizeof(newAddr));
                newAddr.sin_family = AF_INET;
                newAddr.sin_port = htons(newPort);
                if (inet_pton(AF_INET, redirectIP->second.c_str(), &newAddr.sin_addr) == 1) {
                    *addr = newAddr;
                } else {
                    addr->sin_port = htons(newPort);
                }
            } else {
                uint16_t newPort = std::stoi(LicenseData.hostPort);
                if (newPort != port) {
                    addr->sin_port = htons(newPort);
                }
            }
        }
    }
    return True_WSAConnect(s, (struct sockaddr *)addr, namelen, lpCallerData, lpCalleeData, lpSQOS, lpGQOS);
}

HINTERNET WINAPI Hooked_InternetOpenA(LPCSTR lpszAgent, DWORD dwAccessType, LPCSTR lpszProxy, LPCSTR lpszProxyBypass, DWORD dwFlags) {
    return True_InternetOpenA(lpszAgent, dwAccessType, lpszProxy, lpszProxyBypass, dwFlags);
}

HINTERNET WINAPI Hooked_InternetConnectA(HINTERNET hInternet, LPCSTR lpszServerName, INTERNET_PORT nServerPort, LPCSTR lpszUsername, LPCSTR lpszPassword, DWORD dwService, DWORD dwFlags, DWORD_PTR dwContext) {
    if (lpszServerName) {
        if (strstr(lpszServerName, "quattroplay.com")) {
            return True_InternetConnectA(hInternet, "127.0.0.1", g_httpServerPort, lpszUsername, lpszPassword, dwService, dwFlags, dwContext);
        }
        if (!LicenseData.hostResolves.empty()) {
            auto it = LicenseData.hostResolves.find(lpszServerName);
            if (it != LicenseData.hostResolves.end()) {
                return True_InternetConnectA(hInternet, it->second.c_str(), nServerPort, lpszUsername, lpszPassword, dwService, dwFlags, dwContext);
            }
        }
    }
    return True_InternetConnectA(hInternet, lpszServerName, nServerPort, lpszUsername, lpszPassword, dwService, dwFlags, dwContext);
}

typedef long HRESULT;
typedef HRESULT (WINAPI* PFN_CreateDXGIFactory)(const void* riid, void** ppFactory);
typedef HRESULT (WINAPI* PFN_CreateDXGIFactory1)(const void* riid, void** ppFactory);
typedef HRESULT (WINAPI* PFN_CreateDXGIFactory2)(unsigned int Flags, const void* riid, void** ppFactory);

extern "C" HRESULT WINAPI CreateDXGIFactory(const void* riid, void** ppFactory) {
    if (!g_hRealDxgi) return 0x80004005;
    PFN_CreateDXGIFactory pfn = (PFN_CreateDXGIFactory)GetProcAddress(g_hRealDxgi, "CreateDXGIFactory");
    return pfn ? pfn(riid, ppFactory) : 0x80004005;
}

extern "C" HRESULT WINAPI CreateDXGIFactory1(const void* riid, void** ppFactory) {
    if (!g_hRealDxgi) return 0x80004005;
    PFN_CreateDXGIFactory1 pfn = (PFN_CreateDXGIFactory1)GetProcAddress(g_hRealDxgi, "CreateDXGIFactory1");
    return pfn ? pfn(riid, ppFactory) : 0x80004005;
}

extern "C" HRESULT WINAPI CreateDXGIFactory2(unsigned int Flags, const void* riid, void** ppFactory) {
    if (!g_hRealDxgi) return 0x80004005;
    PFN_CreateDXGIFactory2 pfn = (PFN_CreateDXGIFactory2)GetProcAddress(g_hRealDxgi, "CreateDXGIFactory2");
    return pfn ? pfn(Flags, riid, ppFactory) : 0x80004005;
}

static void HTTPServerThread(HMODULE hModule) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) return;
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    serverAddr.sin_port = 0;
    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(listenSocket);
        return;
    }
    int addrLen = sizeof(serverAddr);
    if (getsockname(listenSocket, (sockaddr*)&serverAddr, &addrLen) == 0) {
        g_httpServerPort = ntohs(serverAddr.sin_port);
    }
    listen(listenSocket, SOMAXCONN);
    while (g_bKeepAlive) {
        SOCKET clientSocket = accept(listenSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) continue;
        char buffer[4096];
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0';
            std::string request(buffer);
            const char* resourceName = nullptr;
            const char* contentType = nullptr;
            if (request.find("GET /con.png") != std::string::npos) {
                resourceName = "CONPNG";
                contentType = "application/octet-stream";
            } else if (request.find("GET /conf.gs") != std::string::npos) {
                resourceName = "CONFGS";
                contentType = "application/octet-stream";
            } else if (bytesReceived >= 3 && (unsigned char)buffer[0] == 0x16 && (unsigned char)buffer[1] == 0x03 && (unsigned char)buffer[2] == 0x03) {
                closesocket(clientSocket);
                continue;
            }
            if (resourceName) {
                HRSRC hRes = FindResourceA(g_hThisModule, resourceName, RT_RCDATA);
                std::string filename = strcmp((const char*)resourceName, "CONPNG") == 0 ? "con.png" : "conf.gs";
                if (hRes) {
                    HGLOBAL hData = LoadResource(g_hThisModule, hRes);
                    if (hData) {
                        DWORD size = SizeofResource(g_hThisModule, hRes);
                        void* pData = LockResource(hData);
                        std::string response = "HTTP/1.1 200 OK\r\nContent-Type: " + std::string(contentType) + "\r\nContent-Length: " + std::to_string(size) + "\r\nAccess-Control-Allow-Origin: *\r\n\r\n";
                        send(clientSocket, response.c_str(), response.length(), 0);
                        send(clientSocket, (const char*)pData, size, 0);
                    } else {
                        std::string response = "HTTP/1.1 404 Not Found\r\n\r\n";
                        send(clientSocket, response.c_str(), response.length(), 0);
                    }
                } else {
                    std::string response = "HTTP/1.1 404 Not Found\r\n\r\n";
                    send(clientSocket, response.c_str(), response.length(), 0);
                }
            } else {
                std::string response = "HTTP/1.1 404 Not Found\r\n\r\n";
                send(clientSocket, response.c_str(), response.length(), 0);
            }
        }
        closesocket(clientSocket);
    }
    closesocket(listenSocket);
    WSACleanup();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH: {
            g_hThisModule = hModule;
            char procName[MAX_PATH];
            GetModuleFileNameA(NULL, procName, MAX_PATH);
            char* exeName = strrchr(procName, '\\');
            bool isWorlds = exeName && _stricmp(exeName + 1, "Worlds.exe") == 0;
            bool isEra = exeName && _stricmp(exeName + 1, "Era.exe") == 0;
            if (!isWorlds && !isEra) {
                return TRUE;
            }

            char systemPath[MAX_PATH];
            GetSystemDirectoryA(systemPath, MAX_PATH);
            strcat_s(systemPath, MAX_PATH, "\\dxgi.dll");
            g_hRealDxgi = LoadLibraryA(systemPath);

            InitHostNames();
            g_hKeyboardHook = SetWindowsHookExA(WH_KEYBOARD_LL, KeyboardProc, hModule, 0);
            HMODULE hWs2 = GetModuleHandleA("ws2_32.dll");
            if (!hWs2) {
                LoadLibraryA("ws2_32.dll");
                hWs2 = GetModuleHandleA("ws2_32.dll");
            }
            if (hWs2) {
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());
                DetourAttach(&(PVOID&)True_LoadLibraryA, Hooked_LoadLibraryA);
                DetourAttach(&(PVOID&)True_LoadLibraryW, Hooked_LoadLibraryW);
                DetourAttach(&(PVOID&)True_LoadLibraryExA, Hooked_LoadLibraryExA);
                DetourAttach(&(PVOID&)True_gethostbyname, Hooked_gethostbyname);
                DetourAttach(&(PVOID&)True_getaddrinfo, Hooked_getaddrinfo);
                DetourAttach(&(PVOID&)True_connect, Hooked_connect);
                DetourAttach(&(PVOID&)True_WSAConnect, Hooked_WSAConnect);
                LONG lError = DetourTransactionCommit();
                if (lError == NO_ERROR) {
                    g_bHooksAttached = true;
                }
            }

            bool showDialog = false;
            HKEY hKey;
            if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\GLauncher", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                DWORD value = 0;
                DWORD size = sizeof(value);
                if (RegQueryValueExA(hKey, "SelectServer", NULL, NULL, (BYTE*)&value, &size) == ERROR_SUCCESS && value == 1) {
                    showDialog = true;
                }
                RegCloseKey(hKey);
            }
            auto licensePaths = GetLicenseFilePaths();
            bool loaded = false;
            for (const auto& file : licensePaths) {
                if (showDialog) {
                    if (ShowServerSelectDialog(file)) {
                        loaded = true;
                        break;
                    }
                }
                if (!loaded && ReadLicensesFile(file)) {
                    loaded = true;
                    break;
                }
            }
            if (!loaded) {
                g_servers.clear();
                g_servers.push_back({"listserver.graal.in", "14900"});
                g_servers.push_back({"listserver.graal.in", "14911"});
                g_selectedServer = 0;
                LicenseData.hostResolves[ogHostNames[0]] = g_servers[0].ip;
                LicenseData.hostResolves[ogHostNames[1]] = g_servers[0].ip;
                LicenseData.hostPort = g_servers[0].port;
            }

            std::thread([]() {
                int checkCount = 0;
                while (g_bKeepAlive && checkCount < 300) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    checkCount++;
                }
            }).detach();
            std::thread([hModule]() {
                HTTPServerThread(hModule);
            }).detach();
        } break;
        case DLL_PROCESS_DETACH: {
            g_bKeepAlive = false;
            if (g_hKeyboardHook) {
                UnhookWindowsHookEx(g_hKeyboardHook);
                g_hKeyboardHook = NULL;
            }
            if (g_bHooksAttached) {
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());
                DetourDetach(&(PVOID&)True_LoadLibraryA, Hooked_LoadLibraryA);
                DetourDetach(&(PVOID&)True_LoadLibraryW, Hooked_LoadLibraryW);
                DetourDetach(&(PVOID&)True_LoadLibraryExA, Hooked_LoadLibraryExA);
                DetourDetach(&(PVOID&)True_gethostbyname, Hooked_gethostbyname);
                DetourDetach(&(PVOID&)True_getaddrinfo, Hooked_getaddrinfo);
                DetourDetach(&(PVOID&)True_connect, Hooked_connect);
                DetourDetach(&(PVOID&)True_WSAConnect, Hooked_WSAConnect);
                DetourTransactionCommit();
            }
            if (g_hRealDxgi) {
                FreeLibrary(g_hRealDxgi);
                g_hRealDxgi = NULL;
            }
        } break;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }
    return TRUE;
}
