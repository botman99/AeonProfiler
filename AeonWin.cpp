// WinMin.cpp : Defines the entry point for the application.
//

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <windows.h>

// Global Variables:
HMODULE hModule;

typedef bool (*AeonWinCallbackFunc)(int* ExitApplication);
AeonWinCallbackFunc CallbackFunc;

int ExitApplication = -1;

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Initialize global strings
    MyRegisterClass(hInstance);

	// Load the AeonProfiler DLL and get the "ExitApplication" callback function
	hModule = LoadLibrary(TEXT("AeonProfiler.dll"));
	if( hModule != nullptr )
	{
		CallbackFunc = (AeonWinCallbackFunc)GetProcAddress(hModule, "AeonWinCallback");

		if( CallbackFunc != nullptr )
		{
			CallbackFunc(&ExitApplication);
		}
	}

	// Wait until the AeonProfiler DLL has exited
	while (ExitApplication == 0)
	{
		Sleep(100);
	}

	FreeLibrary(hModule);

	return 0;
}

//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = NULL;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = NULL;
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = NULL;
    wcex.lpszClassName  = TEXT("AeonWinClass");
    wcex.hIconSm        = NULL;

    return RegisterClassExW(&wcex);
}
