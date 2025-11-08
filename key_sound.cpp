// cl /EHsc /W4 /DUNICODE /DWIN32 /D_WIN32_WINNT=0x0601 key_sound_ru.cpp user32.lib winmm.lib shell32.lib gdi32.lib shlwapi.lib ole32.lib
#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <string>
#include "resource.h"

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shlwapi.lib")

// ----------------------------- Реестр и настройки -----------------------------
static const wchar_t* REG_APP_RUN = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* REG_APP_KEY = L"Software\\RuKeySound";
static const wchar_t* REG_VAL_WAV_RU = L"WavPathRu";
static const wchar_t* REG_VAL_WAV_EN = L"WavPathEn";
static const wchar_t* REG_VAL_WAV_TO_RU = L"WavPathToRu";
static const wchar_t* REG_VAL_WAV_TO_EN = L"WavPathToEn";
static const wchar_t* REG_VAL_AUTORUN = L"AutoRun";
static const wchar_t* REG_VAL_MUTED_ALL = L"MutedAll";
static const wchar_t* REG_VAL_MUTED_RU = L"MutedRu";
static const wchar_t* REG_VAL_MUTED_EN = L"MutedEn";
static const wchar_t* REG_VAL_MUTED_SR = L"MutedSwitchToRu";
static const wchar_t* REG_VAL_MUTED_SE = L"MutedSwitchToEn";

// Дефолтные WAV (замените при желании)
static WCHAR g_wavPathRu[MAX_PATH] = L"C:\\Windows\\Media\\Windows Navigation Start.wav";
static WCHAR g_wavPathEn[MAX_PATH] = L"C:\\Windows\\Media\\Windows Feed Discovered.wav";
static WCHAR g_wavPathToRu[MAX_PATH] = L"C:\\Windows\\Media\\Windows Unlock.wav";
static WCHAR g_wavPathToEn[MAX_PATH] = L"C:\\Windows\\Media\\Windows Battery Low.wav";

static bool  g_muted_all = false;
static bool  g_muted_ru = false;
static bool  g_muted_en = false;
static bool  g_muted_switch_to_ru = false;
static bool  g_muted_switch_to_en = false;
static bool  g_autorun = false;

// ----------------------------- Меню и трей -----------------------------
enum : UINT{
    ID_TRAY_MUTE_ALL = 1000,
    ID_TRAY_OPTIONS = 1001, // заголовок (используется как MF_POPUP)
    ID_TRAY_MUTE_RU = 1002,
    ID_TRAY_MUTE_EN = 1003,
    ID_TRAY_MUTE_SWITCH_TO_RU = 1004,
    ID_TRAY_MUTE_SWITCH_TO_EN = 1005,
    ID_TRAY_SELECT_RU = 1006,
    ID_TRAY_SELECT_EN = 1007,
    ID_TRAY_SELECT_TO_RU = 1008,
    ID_TRAY_SELECT_TO_EN = 1009,
    ID_TRAY_AUTORUN = 1010,
    ID_TRAY_EXIT = 1011
};

static NOTIFYICONDATAW g_nid{};
static const UINT WM_TRAY = WM_APP + 1;
static HMENU g_hMenu = nullptr;
static HMENU g_hOptions = nullptr;

// ----------------------------- Иконки -----------------------------
static HICON g_hIconRu = nullptr;
static HICON g_hIconEn = nullptr;

// ----------------------------- Хук -----------------------------
static HHOOK g_hHook = nullptr;

// ----------------------------- Утилиты -----------------------------
static void LoadTrayIcons(HINSTANCE hInst){
    // Лучше многоразмерные .ico, система выберет подходящий размер (обычно 16x16) [web:84][web:87]
    g_hIconRu = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON_RU), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    g_hIconEn = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ICON_EN), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
} // -----------------------------------------------------------------------------------------------------

static void TraySetIcon(HICON hIcon){
    if(!hIcon) return;
    g_nid.uFlags = NIF_ICON;
    g_nid.hIcon = hIcon;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
} // ----------------------------------------------------------------------------------------------------- 

// HKL активного окна
static HKL GetForegroundHKL(){
    HWND hwnd = GetForegroundWindow();
    if(!hwnd) return GetKeyboardLayout(0);
    DWORD pid = 0;
    DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
    return GetKeyboardLayout(tid);
} // -----------------------------------------------------------------------------------------------------

static bool IsRuLayout(){
    HKL hkl = GetForegroundHKL();
    LANGID lang = LOWORD((UINT_PTR)hkl);
    return lang == 0x0419;
} // ----------------------------------------------------------------------------------------------------- 

// Печатаемые символы: ToUnicodeEx > 0
static bool IsPrintableKey(DWORD vkCode, DWORD scanCode, bool isKeyDown){
    if(!isKeyDown) return false;
    switch(vkCode){
    case VK_SHIFT: case VK_CONTROL: case VK_MENU:
    case VK_LSHIFT: case VK_RSHIFT:
    case VK_LCONTROL: case VK_RCONTROL:
    case VK_LMENU: case VK_RMENU:
    case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
    case VK_TAB: case VK_ESCAPE:
    case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
    case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
    case VK_INSERT: case VK_DELETE:
    case VK_F1: case VK_F2: case VK_F3: case VK_F4: case VK_F5:
    case VK_F6: case VK_F7: case VK_F8: case VK_F9: case VK_F10:
    case VK_F11: case VK_F12:
        return false;
    }
    BYTE keyState[256]{};
    if(!GetKeyboardState(keyState)) return false;
    WCHAR out[8]{};
    int res = ToUnicodeEx((UINT)vkCode, (UINT)scanCode, keyState, out, 8, 0, GetForegroundHKL());
    return res > 0;
} // -----------------------------------------------------------------------------------------------------

// ----------------------------- Проигрывание звука -----------------------------
static void PlayKeySoundRu(){
    if(g_muted_all || g_muted_ru) return;
    PlaySoundW(g_wavPathRu, nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
} // -----------------------------------------------------------------------------------------------------

static void PlayKeySoundEn(){
    if(g_muted_all || g_muted_en) return;
    PlaySoundW(g_wavPathEn, nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
} // -----------------------------------------------------------------------------------------------------

static void PlayKeySoundToRu(){
    if(g_muted_all || g_muted_switch_to_ru) return;
    PlaySoundW(g_wavPathToRu, nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
} // -----------------------------------------------------------------------------------------------------

static void PlayKeySoundToEn(){
    if(g_muted_all || g_muted_switch_to_en) return;
    PlaySoundW(g_wavPathToEn, nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
} // -----------------------------------------------------------------------------------------------------

// ----------------------------- Хук клавиатуры -----------------------------
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam){
    static bool isRusPrev = IsRuLayout();
    if(nCode == HC_ACTION){
        const KBDLLHOOKSTRUCT* p = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

        bool isRus = IsRuLayout();
        if(isRus != isRusPrev){
            // Смена раскладки: иконка и звук переключения
            TraySetIcon(isRus ? g_hIconRu : g_hIconEn);
            if(isRus) PlayKeySoundToRu(); else PlayKeySoundToEn();
            isRusPrev = isRus;
        }

        if(IsPrintableKey(p->vkCode, p->scanCode, isDown)){
            if(isRus) PlayKeySoundRu(); else PlayKeySoundEn();
        }
    }
    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
} // -----------------------------------------------------------------------------------------------------

// ----------------------------- Реестр: загрузка/сохранение -----------------------------
static void LoadSettings(){
    HKEY hKey;
    if(RegCreateKeyExW(HKEY_CURRENT_USER, REG_APP_KEY, 0, nullptr, 0, KEY_READ, nullptr, &hKey, nullptr) == ERROR_SUCCESS){
        DWORD type = 0, cb = 0;

        cb = sizeof(g_wavPathRu);
        RegQueryValueExW(hKey, REG_VAL_WAV_RU, nullptr, &type, reinterpret_cast<LPBYTE>(g_wavPathRu), &cb);

        cb = sizeof(g_wavPathEn);
        RegQueryValueExW(hKey, REG_VAL_WAV_EN, nullptr, &type, reinterpret_cast<LPBYTE>(g_wavPathEn), &cb);

        cb = sizeof(g_wavPathToRu);
        RegQueryValueExW(hKey, REG_VAL_WAV_TO_RU, nullptr, &type, reinterpret_cast<LPBYTE>(g_wavPathToRu), &cb);

        cb = sizeof(g_wavPathToEn);
        RegQueryValueExW(hKey, REG_VAL_WAV_TO_EN, nullptr, &type, reinterpret_cast<LPBYTE>(g_wavPathToEn), &cb);

        DWORD d = 0; cb = sizeof(d);
        if(RegQueryValueExW(hKey, REG_VAL_AUTORUN, nullptr, &type, reinterpret_cast<LPBYTE>(&d), &cb) == ERROR_SUCCESS && type == REG_DWORD) g_autorun = d != 0;
        if(RegQueryValueExW(hKey, REG_VAL_MUTED_ALL, nullptr, &type, reinterpret_cast<LPBYTE>(&d), &cb) == ERROR_SUCCESS && type == REG_DWORD) g_muted_all = d != 0;
        if(RegQueryValueExW(hKey, REG_VAL_MUTED_RU, nullptr, &type, reinterpret_cast<LPBYTE>(&d), &cb) == ERROR_SUCCESS && type == REG_DWORD) g_muted_ru = d != 0;
        if(RegQueryValueExW(hKey, REG_VAL_MUTED_EN, nullptr, &type, reinterpret_cast<LPBYTE>(&d), &cb) == ERROR_SUCCESS && type == REG_DWORD) g_muted_en = d != 0;
        if(RegQueryValueExW(hKey, REG_VAL_MUTED_SR, nullptr, &type, reinterpret_cast<LPBYTE>(&d), &cb) == ERROR_SUCCESS && type == REG_DWORD) g_muted_switch_to_ru = d != 0;
        if(RegQueryValueExW(hKey, REG_VAL_MUTED_SE, nullptr, &type, reinterpret_cast<LPBYTE>(&d), &cb) == ERROR_SUCCESS && type == REG_DWORD) g_muted_switch_to_en = d != 0;

        RegCloseKey(hKey);
    }
} // -----------------------------------------------------------------------------------------------------

static void SaveSettings(){
    HKEY hKey;
    if(RegCreateKeyExW(HKEY_CURRENT_USER, REG_APP_KEY, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS){
        RegSetValueExW(hKey, REG_VAL_WAV_RU, 0, REG_SZ, reinterpret_cast<const BYTE*>(g_wavPathRu), (DWORD)((wcslen(g_wavPathRu) + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, REG_VAL_WAV_EN, 0, REG_SZ, reinterpret_cast<const BYTE*>(g_wavPathEn), (DWORD)((wcslen(g_wavPathEn) + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, REG_VAL_WAV_TO_RU, 0, REG_SZ, reinterpret_cast<const BYTE*>(g_wavPathToRu), (DWORD)((wcslen(g_wavPathToRu) + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, REG_VAL_WAV_TO_EN, 0, REG_SZ, reinterpret_cast<const BYTE*>(g_wavPathToEn), (DWORD)((wcslen(g_wavPathToEn) + 1) * sizeof(wchar_t)));

        DWORD d = g_autorun ? 1u : 0u; RegSetValueExW(hKey, REG_VAL_AUTORUN, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&d), sizeof(d));
        d = g_muted_all ? 1u : 0u;     RegSetValueExW(hKey, REG_VAL_MUTED_ALL, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&d), sizeof(d));
        d = g_muted_ru ? 1u : 0u;      RegSetValueExW(hKey, REG_VAL_MUTED_RU, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&d), sizeof(d));
        d = g_muted_en ? 1u : 0u;      RegSetValueExW(hKey, REG_VAL_MUTED_EN, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&d), sizeof(d));
        d = g_muted_switch_to_ru ? 1u : 0u; RegSetValueExW(hKey, REG_VAL_MUTED_SR, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&d), sizeof(d));
        d = g_muted_switch_to_en ? 1u : 0u; RegSetValueExW(hKey, REG_VAL_MUTED_SE, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&d), sizeof(d));

        RegCloseKey(hKey);
    }
} // -----------------------------------------------------------------------------------------------------

static void UpdateAutoRun(bool enable){
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    HKEY hKey;
    if(RegOpenKeyExW(HKEY_CURRENT_USER, REG_APP_RUN, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS){
        if(enable){
            RegSetValueExW(hKey, L"RuKeySound", 0, REG_SZ, reinterpret_cast<const BYTE*>(exePath), (DWORD)((wcslen(exePath) + 1) * sizeof(wchar_t)));
        } else{
            RegDeleteValueW(hKey, L"RuKeySound");
        }
        RegCloseKey(hKey);
    }
} // -----------------------------------------------------------------------------------------------------

// ----------------------------- Диалог выбора WAV -----------------------------
static bool SelectWavFile(HWND owner, std::wstring& outPath){
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool needUninit = SUCCEEDED(hr);
    IFileOpenDialog* pDlg = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pDlg));
    if(FAILED(hr)){ if(needUninit) CoUninitialize(); return false; }

    COMDLG_FILTERSPEC filters[] = {{ L"WAV files", L"*.wav" }, { L"All files", L"*.*" }};
    pDlg->SetFileTypes(2, filters);
    pDlg->SetFileTypeIndex(1);
    pDlg->SetTitle(L"Select WAV file");

    hr = pDlg->Show(owner);
    if(hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)){ pDlg->Release(); if(needUninit) CoUninitialize(); return false; }
    IShellItem* pItem = nullptr;
    hr = pDlg->GetResult(&pItem);
    if(SUCCEEDED(hr)){
        PWSTR psz = nullptr;
        if(SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &psz))){
            outPath = psz;
            CoTaskMemFree(psz);
        }
        pItem->Release();
    }
    pDlg->Release();
    if(needUninit) CoUninitialize();
    return !outPath.empty();
} // -----------------------------------------------------------------------------------------------------

// ----------------------------- Меню трея -----------------------------
static void BuildOrUpdateTrayMenu(){
    if(g_hMenu){ DestroyMenu(g_hMenu); g_hMenu = nullptr; }
    if(g_hOptions){ DestroyMenu(g_hOptions); g_hOptions = nullptr; }

    g_hMenu = CreatePopupMenu();
    g_hOptions = CreatePopupMenu();

    // Верхний уровень
    AppendMenuW(g_hMenu, MF_STRING | (g_muted_all ? MF_CHECKED : 0), ID_TRAY_MUTE_ALL, L"Mute all");
    AppendMenuW(g_hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(g_hOptions), L"Options");
    AppendMenuW(g_hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    // Подменю Options
    AppendMenuW(g_hOptions, MF_STRING | (g_muted_ru ? MF_CHECKED : 0), ID_TRAY_MUTE_RU, L"Mute Ru");
    AppendMenuW(g_hOptions, MF_STRING | (g_muted_en ? MF_CHECKED : 0), ID_TRAY_MUTE_EN, L"Mute En");
    AppendMenuW(g_hOptions, MF_STRING | (g_muted_switch_to_ru ? MF_CHECKED : 0), ID_TRAY_MUTE_SWITCH_TO_RU, L"Mute switch to ru");
    AppendMenuW(g_hOptions, MF_STRING | (g_muted_switch_to_en ? MF_CHECKED : 0), ID_TRAY_MUTE_SWITCH_TO_EN, L"Mute switch to en");
    AppendMenuW(g_hOptions, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_hOptions, MF_STRING, ID_TRAY_SELECT_RU, L"Select file ru...");
    AppendMenuW(g_hOptions, MF_STRING, ID_TRAY_SELECT_EN, L"Select file en...");
    AppendMenuW(g_hOptions, MF_STRING, ID_TRAY_SELECT_TO_RU, L"Select file switch ru...");
    AppendMenuW(g_hOptions, MF_STRING, ID_TRAY_SELECT_TO_EN, L"Select file switch en...");
    AppendMenuW(g_hOptions, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_hOptions, MF_STRING | (g_autorun ? MF_CHECKED : 0), ID_TRAY_AUTORUN, L"Autostart");
} // -----------------------------------------------------------------------------------------------------

static void ShowTrayMenu(HWND hWnd, POINT pt){
    BuildOrUpdateTrayMenu();
    SetForegroundWindow(hWnd);
    TrackPopupMenu(g_hMenu, TPM_RIGHTBUTTON | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, nullptr);
    PostMessageW(hWnd, WM_NULL, 0, 0);
} // -----------------------------------------------------------------------------------------------------

// ----------------------------- Окно и сообщения -----------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam){
    std::wstring sel;
    switch(msg){
    case WM_CREATE: {
        // Загрузка настроек и иконок
        LoadSettings();
        LoadTrayIcons((HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE));

        // Трей-иконка
        g_nid = {};
        g_nid.cbSize = sizeof(g_nid);
        g_nid.hWnd = hWnd;
        g_nid.uID = 1;
        g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAY;
        // Начальная иконка по текущему языку
        bool isRu = IsRuLayout();
        g_nid.hIcon = isRu ? g_hIconRu : g_hIconEn;
        lstrcpynW(g_nid.szTip, isRu ? L"RU Key Sound" : L"EN Key Sound", ARRAYSIZE(g_nid.szTip));
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        break;
    }
    case WM_INPUTLANGCHANGE: {
        // При смене раскладки для нашего потока — обновить иконку/подсказку
        LANGID lang = LOWORD((UINT_PTR)lParam);
        bool isRu = (lang == 0x0419);
        TraySetIcon(isRu ? g_hIconRu : g_hIconEn);
        g_nid.uFlags = NIF_TIP | NIF_ICON;
        lstrcpynW(g_nid.szTip, isRu ? L"RU Key Sound" : L"EN Key Sound", ARRAYSIZE(g_nid.szTip));
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        return 0;
    } 
    case WM_TRAY: {
        if(LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU){
            POINT pt{}; GetCursorPos(&pt);
            ShowTrayMenu(hWnd, pt);
        } else if(LOWORD(lParam) == WM_LBUTTONDBLCLK){
            // Быстрый toggle «Mute all»
            g_muted_all = !g_muted_all;
            SaveSettings();
        }
        break;
    }
    case WM_COMMAND: {
        switch(LOWORD(wParam)){
        case ID_TRAY_MUTE_ALL:
            g_muted_all = !g_muted_all;
            SaveSettings();
            break;

        case ID_TRAY_MUTE_RU:
            g_muted_ru = !g_muted_ru;
            SaveSettings();
            break;
        case ID_TRAY_MUTE_EN:
            g_muted_en = !g_muted_en;
            SaveSettings();
            break;
        case ID_TRAY_MUTE_SWITCH_TO_RU:
            g_muted_switch_to_ru = !g_muted_switch_to_ru;
            SaveSettings();
            break;
        case ID_TRAY_MUTE_SWITCH_TO_EN:
            g_muted_switch_to_en = !g_muted_switch_to_en;
            SaveSettings();
            break;

        case ID_TRAY_SELECT_RU:
            if(SelectWavFile(hWnd, sel)){ wcsncpy_s(g_wavPathRu, sel.c_str(), _TRUNCATE); SaveSettings(); }
            break;
        case ID_TRAY_SELECT_EN:
            if(SelectWavFile(hWnd, sel)){ wcsncpy_s(g_wavPathEn, sel.c_str(), _TRUNCATE); SaveSettings(); }
            break;
        case ID_TRAY_SELECT_TO_RU:
            if(SelectWavFile(hWnd, sel)){ wcsncpy_s(g_wavPathToRu, sel.c_str(), _TRUNCATE); SaveSettings(); }
            break;
        case ID_TRAY_SELECT_TO_EN:
            if(SelectWavFile(hWnd, sel)){ wcsncpy_s(g_wavPathToEn, sel.c_str(), _TRUNCATE); SaveSettings(); }
            break;

        case ID_TRAY_AUTORUN:
            g_autorun = !g_autorun; UpdateAutoRun(g_autorun); SaveSettings();
            break;

        case ID_TRAY_EXIT:
            PostQuitMessage(0);
            break;
        }
        break;
    }
    case WM_DESTROY: {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        if(g_hMenu){ DestroyMenu(g_hMenu); g_hMenu = nullptr; }
        if(g_hOptions){ DestroyMenu(g_hOptions); g_hOptions = nullptr; }
        if(g_hIconRu){ DestroyIcon(g_hIconRu); g_hIconRu = nullptr; }
        if(g_hIconEn){ DestroyIcon(g_hIconEn); g_hIconEn = nullptr; }
        PostQuitMessage(0);
        break;
    }
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
} // -----------------------------------------------------------------------------------------------------

// ----------------------------- Точка входа -----------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int){
    const wchar_t* kClass = L"RuKeySoundWnd";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);

    //HWND hWnd = 
    CreateWindowExW(0, kClass, L"RuKeySound", 0, 0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);

    // Низкоуровневый хук клавиатуры
    g_hHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, nullptr, 0);
    if(!g_hHook) return 1;

    LoadSettings();
    if(g_autorun) UpdateAutoRun(true);

    // Инициализировать начальную иконку (если CreateWindow уже сделал — этот шаг может быть опционален)
    // но оставим основную логику в WM_CREATE.

    MSG msg;
    while(GetMessageW(&msg, nullptr, 0, 0) > 0){
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(g_hHook);
    return 0;
} // -----------------------------------------------------------------------------------------------------
