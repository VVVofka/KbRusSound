// cl /EHsc /W4 /DUNICODE /DWIN32 /D_WIN32_WINNT=0x0601 key_sound_ru.cpp user32.lib winmm.lib shell32.lib gdi32.lib
#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <string>
#include <shobjidl.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "winmm.lib")

// Настройки
static const wchar_t* REG_APP_RUN =
L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* REG_APP_KEY =
L"Software\\RuKeySound";
static const wchar_t* REG_VAL_WAV = L"WavPath";
static const wchar_t* REG_VAL_AUTORUN = L"AutoRun";

static WCHAR g_wavPath[MAX_PATH] = L"C:\\Windows\\Media\\Windows Navigation Start.wav";
static bool  g_muted = false;
static bool  g_autorun = false;

enum : UINT{
    ID_TRAY_MUTE = 1001,
    ID_TRAY_SELECT = 1003,
    ID_TRAY_AUTORUN = 1004,
    ID_TRAY_EXIT = 1002
};

// Трей
static NOTIFYICONDATAW g_nid{};
static const UINT WM_TRAY = WM_APP + 1;
static HMENU g_hMenu = nullptr;

// Хук
static HHOOK g_hHook = nullptr;

// Получить HKL активного окна (правильная раскладка потока)
static HKL GetForegroundHKL(){
    HWND hwnd = GetForegroundWindow();
    if(!hwnd) return GetKeyboardLayout(0);
    DWORD pid = 0;
    DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
    return GetKeyboardLayout(tid);
}

// RU раскладка?
static bool IsRuLayout(){
    HKL hkl = GetForegroundHKL();
    LANGID lang = LOWORD((UINT_PTR)hkl);
    return lang == 0x0419; // RU
}

// Проверка: клавиша даёт «печатаемый» символ с учётом текущей раскладки и модификаторов
static bool IsPrintableKey(DWORD vkCode, DWORD scanCode, bool isKeyDown){
    if(!isKeyDown) return false;

    // Исключение для явных непечатаемых
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

    // Считываем состояние клавиш
    BYTE keyState[256]{};
    if(!GetKeyboardState(keyState)) return false;

    // Важный момент: ToUnicodeEx зависит от состояния dead keys; для получения «есть ли печать»
    // достаточно проверить, даёт ли буфер ненулевую длину.
    WCHAR out[8]{};
    int res = ToUnicodeEx(
        static_cast<UINT>(vkCode),
        static_cast<UINT>(scanCode),
        keyState,
        out,
        8,
        0,
        GetForegroundHKL()
    );
    // res > 0 => выданы символы (печать на экран); res == -1 => dead key, не печатается сейчас
    return res > 0;
}

static void PlayKeySound(){
    if(g_muted) return;
    PlaySoundW(g_wavPath, nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam){
    if(nCode == HC_ACTION){
        const KBDLLHOOKSTRUCT* p = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        if(IsRuLayout() && IsPrintableKey(p->vkCode, p->scanCode, isDown)){
            PlayKeySound();
        }
    }
    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}

// Показ контекстного меню у иконки трея
static void ShowTrayMenu(HWND hWnd, POINT pt){
    if(!g_hMenu){
        g_hMenu = CreatePopupMenu();
        AppendMenuW(g_hMenu, MF_STRING | (g_muted ? MF_CHECKED : 0), 1001, L"Mute");
        AppendMenuW(g_hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(g_hMenu, MF_STRING, 1002, L"Exit");
    } else{
        // Обновить чекбокс
        CheckMenuItem(g_hMenu, 1001, MF_BYCOMMAND | (g_muted ? MF_CHECKED : MF_UNCHECKED));
    }

    // Требование API: окно должно быть foreground до TrackPopupMenu
    SetForegroundWindow(hWnd);
    TrackPopupMenu(g_hMenu, TPM_RIGHTBUTTON | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, nullptr);
    PostMessageW(hWnd, WM_NULL, 0, 0);
}

// Окно-приёмник сообщений
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam){
    switch(msg){
    case WM_CREATE: {
        // Создать иконку в трее
        g_nid = {};
        g_nid.cbSize = sizeof(g_nid);
        g_nid.hWnd = hWnd;
        g_nid.uID = 1;
        g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAY;
        g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        lstrcpynW(g_nid.szTip, L"RU Key Sound", ARRAYSIZE(g_nid.szTip));
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        break;
    }
    case WM_TRAY: {
        if(LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU){
            POINT pt{};
            GetCursorPos(&pt);
            ShowTrayMenu(hWnd, pt);
        } else if(LOWORD(lParam) == WM_LBUTTONDBLCLK){
            // Быстрый toggle mute по двойному клику
            g_muted = !g_muted;
        }
        break;
    }
    case WM_COMMAND: {
        switch(LOWORD(wParam)){
        case 1001: // Mute
            g_muted = !g_muted;
            break;
        case 1002: // Exit
            PostQuitMessage(0);
            break;
        }
        break;
    }
    case WM_DESTROY: {
        // Удалить иконку
        if(g_nid.cbSize) Shell_NotifyIconW(NIM_DELETE, &g_nid);
        if(g_hMenu){ DestroyMenu(g_hMenu); g_hMenu = nullptr; }
        PostQuitMessage(0);
        break;
    }
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int){
    // Окно-приёмник для трея и меню
    const wchar_t* kClass = L"RuKeySoundWnd";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);
    HWND hWnd = CreateWindowExW(0, kClass, L"RuKeySound", 0, 0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);

    // Устанавливаем глобальный хук
    g_hHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, nullptr, 0);
    if(!g_hHook) return 1;

    // Цикл сообщений
    MSG msg;
    while(GetMessageW(&msg, nullptr, 0, 0) > 0){
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(g_hHook);
    return 0;
}
