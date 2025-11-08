// cl /EHsc /W4 /DUNICODE /DWIN32 /D_WIN32_WINNT=0x0601 key_sound.cpp user32.lib winmm.lib
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

static HHOOK g_hHook = nullptr;
static WCHAR g_wavPath[MAX_PATH] = L"C:\\Windows\\Media\\Windows Navigation Start.wav"; // замените на короткий WAV

static bool IsRuLayoutForForegroundThread(){
	// Берем активное окно и поток, чтобы получить корректный HKL
	HWND hwnd = GetForegroundWindow();
	if(!hwnd) return false;
	DWORD pid = 0;
	DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
	HKL hkl = GetKeyboardLayout(tid);
	LANGID lang = LOWORD((UINT_PTR)hkl);
	return lang == 0x0419; // RU
}

static void PlayKeySound(){
	// Быстрый короткий звук асинхронно
	PlaySoundW(g_wavPath, nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT); // можно добавить SND_NOSTOP
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam){
	if(nCode == HC_ACTION){
		const KBDLLHOOKSTRUCT* p = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
		if(wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN){
			// Игнор системных модификаторов при желании:
			//if(p->vkCode == VK_SHIFT || p->vkCode == VK_CONTROL || p->vkCode == VK_MENU){
				if(IsRuLayoutForForegroundThread()){
					PlayKeySound();
				}
			
		}
	}
	return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int){
	// При желании: загрузить путь к WAV из argv/конфигурации
	g_hHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, nullptr, 0);
	if(!g_hHook) return 1;

	MSG msg;
	while(GetMessageW(&msg, nullptr, 0, 0) > 0){
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	UnhookWindowsHookEx(g_hHook);
	return 0;
}
