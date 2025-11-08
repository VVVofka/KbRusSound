// cl /EHsc /W4 /DUNICODE /DWIN32 /D_WIN32_WINNT=0x0601 key_sound_ru.cpp user32.lib winmm.lib shell32.lib gdi32.lib
#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <string>
#include <shobjidl.h>
#include <shlwapi.h>
#include "resource.h"

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

static void LoadSettings(){
	HKEY hKey;
	if(RegCreateKeyExW(HKEY_CURRENT_USER, REG_APP_KEY, 0, nullptr, 0, KEY_READ, nullptr, &hKey, nullptr) == ERROR_SUCCESS){
		DWORD type = 0, cb = sizeof(g_wavPath);
		if(RegQueryValueExW(hKey, REG_VAL_WAV, nullptr, &type, reinterpret_cast<LPBYTE>(g_wavPath), &cb) == ERROR_SUCCESS && type == REG_SZ){}
		DWORD autorun = 0; cb = sizeof(autorun);
		if(RegQueryValueExW(hKey, REG_VAL_AUTORUN, nullptr, &type, reinterpret_cast<LPBYTE>(&autorun), &cb) == ERROR_SUCCESS && type == REG_DWORD){
			g_autorun = autorun != 0;
		}
		RegCloseKey(hKey);
	}
} // -----------------------------------------------------------------------------------------------------------

static void SaveSettings(){
	HKEY hKey;
	if(RegCreateKeyExW(HKEY_CURRENT_USER, REG_APP_KEY, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS){
		RegSetValueExW(hKey, REG_VAL_WAV, 0, REG_SZ, reinterpret_cast<const BYTE*>(g_wavPath), (DWORD)((wcslen(g_wavPath) + 1) * sizeof(wchar_t)));
		DWORD autorun = g_autorun ? 1u : 0u;
		RegSetValueExW(hKey, REG_VAL_AUTORUN, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&autorun), sizeof(autorun));
		RegCloseKey(hKey);
	}
} // -----------------------------------------------------------------------------------------------------------

static void UpdateAutoRun(bool enable){
	wchar_t exePath[MAX_PATH];
	GetModuleFileNameW(nullptr, exePath, MAX_PATH);
	HKEY hKey;
	if(RegOpenKeyExW(HKEY_CURRENT_USER, REG_APP_RUN, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS){
		if(enable){
			RegSetValueExW(hKey, L"RuKeySound", 0, REG_SZ, reinterpret_cast<const BYTE*>(exePath),
						   (DWORD)((wcslen(exePath) + 1) * sizeof(wchar_t)));
		} else{
			RegDeleteValueW(hKey, L"RuKeySound");
		}
		RegCloseKey(hKey);
	}
} // -----------------------------------------------------------------------------------------------------------

static bool SelectWavFile(HWND owner, std::wstring& outPath){
	HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	bool needUninit = SUCCEEDED(hr);
	IFileOpenDialog* pDlg = nullptr;
	hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pDlg));
	if(FAILED(hr)){ if(needUninit) CoUninitialize(); return false; }

	// фильтр WAV
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
} // -----------------------------------------------------------------------------------------------------------

static void BuildOrUpdateTrayMenu(){
	if(!g_hMenu) g_hMenu = CreatePopupMenu();
	else{ DestroyMenu(g_hMenu); g_hMenu = CreatePopupMenu(); }

	AppendMenuW(g_hMenu, MF_STRING | (g_muted ? MF_CHECKED : 0), ID_TRAY_MUTE, L"Mute");
	AppendMenuW(g_hMenu, MF_STRING, ID_TRAY_SELECT, L"Select file...");
	AppendMenuW(g_hMenu, MF_STRING | (g_autorun ? MF_CHECKED : 0), ID_TRAY_AUTORUN, L"Autostart");
	AppendMenuW(g_hMenu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(g_hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");
}// -----------------------------------------------------------------------------------------------------------

static void ShowTrayMenu(HWND hWnd, POINT pt){
	BuildOrUpdateTrayMenu();
	SetForegroundWindow(hWnd);
	TrackPopupMenu(g_hMenu, TPM_RIGHTBUTTON | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, nullptr); // [web:47]
	PostMessageW(hWnd, WM_NULL, 0, 0);
}// -----------------------------------------------------------------------------------------------------------

// Получить HKL активного окна (правильная раскладка потока)
static HKL GetForegroundHKL(){
	HWND hwnd = GetForegroundWindow();
	if(!hwnd) return GetKeyboardLayout(0);
	DWORD pid = 0;
	DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
	return GetKeyboardLayout(tid);
} // -----------------------------------------------------------------------------------------------------------

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

// Окно-приёмник сообщений
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam){
	switch(msg){
	case WM_CREATE: {
		LoadSettings(); // считать wav и флаг автозапуска
		// Создать иконку в трее
		g_nid = {};
		g_nid.cbSize = sizeof(g_nid);
		g_nid.hWnd = hWnd;
		g_nid.uID = 1;
		g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
		g_nid.uCallbackMessage = WM_TRAY;
		g_nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON)); // из ресурсов
		lstrcpynW(g_nid.szTip, L"RU Key Sound", ARRAYSIZE(g_nid.szTip));
		Shell_NotifyIconW(NIM_ADD, &g_nid); // [web:52][web:58]
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
		case ID_TRAY_MUTE:
			g_muted = !g_muted; SaveSettings();
			break;
		case ID_TRAY_SELECT: {
			std::wstring sel;
			if(SelectWavFile(hWnd, sel)){
				wcsncpy_s(g_wavPath, sel.c_str(), _TRUNCATE);
				SaveSettings();
			}
			break;
		}
		case ID_TRAY_AUTORUN:
			g_autorun = !g_autorun;
			UpdateAutoRun(g_autorun);
			SaveSettings();
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

	LoadSettings();
	if(g_autorun) UpdateAutoRun(true); // убедиться, что запись есть

	// Цикл сообщений
	MSG msg;
	while(GetMessageW(&msg, nullptr, 0, 0) > 0){
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	UnhookWindowsHookEx(g_hHook);
	return 0;
}
