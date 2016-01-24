
#include "targetver.h"
#include "resource.h"

#include <Windows.h>
#include <windowsx.h>
#include <TlHelp32.h>

#include "Splitter.h"
#include "Dialog.h"
#include "TextViewer.h"
#include "Config.h"

#include "DebugLog.h"


extern CAllocator GlobalAllocator;
extern CAllocator SymbolAllocator;
extern CAllocator DialogAllocator;
extern CAllocator TextViewerAllocator;

extern TextLineBuffer line_buffer;

// Global Variables:
HINSTANCE hInst;								// current instance
TCHAR szTitle[MAX_LOADSTRING];					// The title bar text
TCHAR szWindowClass[MAX_LOADSTRING];			// the main window class name

CConfig* gConfig = nullptr;

int NumThreads;  // for stat tracking
int NumCallTreeRecords;  // for stat tracking

// child windows
HWND hChildWindowFunctions;
HWND hChildWindowParentFunctions;
HWND hChildWindowChildrenFunctions;
HWND hChildWindowTextViewer;

HWND ghWnd;  // global hWnd for application window (so we can set the window text in the titlebar)
HWND ghDialogWnd;  // global hWnd for the main dialog
HWND ghLookupSymbolsModalDialogWnd;  // global hWnd for the 'LookupSymbols' dialog

HANDLE ProcessCallTreeDataThreadHandle = NULL;
DWORD ProcessCallTreeDataThreadID = 0;
bool bIsCaptureInProgress = false;  // don't allow a capture to start while one is already in progress

DWORD DialogCallTreeThreadId;  // the thread id of the thread to display CallTree data for in the ListView dialog


BOOL InitInstance(HINSTANCE hInstance, int nCmdShow);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK WndProcLeftChildren(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK WndProcRightChildren(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK WndEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);


void WINAPI DialogThread(LPVOID lpData)
{
	MSG msg;
	HACCEL hAccelTable;

	int nCmdShow = SW_SHOW;

	hInst = (HINSTANCE)ModuleHandle;

	// Initialize global strings
	LoadString(hInst, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadString(hInst, IDC_AEON_PROFILER, szWindowClass, MAX_LOADSTRING);

	gConfig = (CConfig*)new CConfig();

	int window_pos_x = gConfig->GetInt(CONFIG_WINDOW_POS_X);
	int window_pos_y = gConfig->GetInt(CONFIG_WINDOW_POS_Y);
	int window_width = gConfig->GetInt(CONFIG_WINDOW_SIZE_WIDTH);
	int window_height = gConfig->GetInt(CONFIG_WINDOW_SIZE_HEIGHT);

	float middle_splitter_precent = gConfig->GetFloat(CONFIG_MIDDLE_SPLITTER_PERCENT);

	// this will create the application's class and main window...
	CSplitter* top_splitter = (CSplitter*)GlobalAllocator.AllocateBytes(sizeof(CSplitter), sizeof(void*));
	new(top_splitter) CSplitter(hInst, WndProc, NULL, nullptr, MAKEINTRESOURCE(IDC_AEON_PROFILER), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
								window_pos_x, window_pos_y, window_width, window_height, 1, ESplitterOrientation_Vertical, middle_splitter_precent, 20, 20);

	ghWnd = top_splitter->m_hwnd_Splitter;

	hAccelTable = LoadAccelerators(hInst, MAKEINTRESOURCE(IDC_AEON_PROFILER));

	// Main message loop:
	while (GetMessage(&msg, NULL, 0, 0))
	{
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	if( hApplicationProcess )
	{
		CloseHandle(hApplicationProcess);
	}
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_CREATE:
			{
				float left_splitter_precent = gConfig->GetFloat(CONFIG_LEFT_SPLITTER_PERCENT);

				CSplitter* life_child_splitter = (CSplitter*)GlobalAllocator.AllocateBytes(sizeof(CSplitter), sizeof(void*));
				new(life_child_splitter) CSplitter(hInst, WndProcLeftChildren, hWnd, nullptr, nullptr, WS_CHILD | WS_VISIBLE,
													CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
													1, ESplitterOrientation_Horizontal, left_splitter_precent, 20, 20);

				float right_splitter_precent = gConfig->GetFloat(CONFIG_RIGHT_SPLITTER_PERCENT);

				CSplitter* right_child_splitter = (CSplitter*)GlobalAllocator.AllocateBytes(sizeof(CSplitter), sizeof(void*));
				new(right_child_splitter) CSplitter(hInst, WndProcRightChildren, hWnd, nullptr, nullptr, WS_CHILD | WS_VISIBLE,
													CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
													1, ESplitterOrientation_Horizontal, right_splitter_precent, 20, 20);

				ListViewInitChildWindows();

				if( SetTimer(hWnd, 1, 100, NULL) == 0)  // set a timer to occur every 100ms
				{
					DebugLog("SetTimer() failed, GetLastError() = %d", GetLastError());
				}

				ghDialogWnd = hWnd;  // save this dialog's handle so that other dialogs and threads can send it messages
			}
			break;

		case WM_SIZE:
			{
				RECT rect;
				GetWindowRect(hWnd, &rect);

				INT nWidth = rect.right - rect.left;
				INT nHeight = rect.bottom - rect.top;

				if( gConfig && (nWidth > 0) && (nHeight > 0) )
				{
					gConfig->SetInt(CONFIG_WINDOW_SIZE_WIDTH, nWidth);
					gConfig->SetInt(CONFIG_WINDOW_SIZE_HEIGHT, nHeight);
				}

				return DefWindowProc(hWnd, message, wParam, lParam);
			}
			break;

		case WM_MOVE:
			{
				// NOTE: We don't prevent you from moving the window down below the taskbar (where you can't grab it again with the mouse).
				// If this happens, the simplest workaround is to hover over the profiler window in the taskbar, right click on it, select
				// "Move" and USE THE KEYBOARD to move the monitor back to a position where you can grab it with the mouse.  Trying to prevent
				// this failure case (of moving the window below the taskbar) proved diffcult to determine since you have to get the screenspace
				// for each monitor, get the working area of each monitor, subtract the working area from the screenspace to determine where
				// the taskbar lives, then force the profiler window back to a safe position in the event that it has moved into taskbar space.  Ugh!

				RECT rect;
				GetWindowRect(hWnd, &rect);  // get our window's position

				INT nPosX = rect.left;
				INT nPosY = rect.top;

				if( gConfig && (nPosX >= 0) && (nPosY >= 0) )  // don't save when off the screen or minimizing
				{
					gConfig->SetInt(CONFIG_WINDOW_POS_X, nPosX);
					gConfig->SetInt(CONFIG_WINDOW_POS_Y, nPosY);
				}

				return DefWindowProc(hWnd, message, wParam, lParam);
			}
			break;

		case WM_SPLITTER_PERCENT:
			if( gConfig )
			{
				float percent = *(float*)lParam;
				gConfig->SetFloat(CONFIG_MIDDLE_SPLITTER_PERCENT, percent);
			}
			break;

		case WM_SETFOCUS:
			ListViewSetFocus(hWnd);
			break;

		case WM_TIMER:
			if( gConfig )
			{
				gConfig->Timer();
			}
			break;

		case WM_COMMAND:
			{
				int wmId = LOWORD(wParam);
				int wmEvent = HIWORD(wParam);

				switch( wmId )
				{
					case IDM_STATS:
						{
							DialogBox(hInst, MAKEINTRESOURCE(IDD_STATS), hWnd, StatsModalDialog);
						}
						break;

					case IDM_EXIT:
						KillTimer(NULL, 1);
						PostMessage( hWnd, WM_CLOSE, NULL, 0L );
						break;

					case IDM_CAPTURE:
						{
							if( !bIsCaptureInProgress )  // is a capture not already in progress?
							{
								bIsCaptureInProgress = true;

								int NumSymbolsToLookup = CaptureCallTreeData();

								ghLookupSymbolsModalDialogWnd = 0;

								// if there's a lot of symbols to look up, display a dialog box with the progress...
								if( NumSymbolsToLookup > 1000 )
								{
									DialogBox(hInst, MAKEINTRESOURCE(IDD_LOOKUPSYMBOLS), hWnd, LookupSymbolsModalDialog);
								}
								else  // otherwise, just look up the symbols without a dialog box...
								{
									ProcessCallTreeDataThreadHandle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ProcessCallTreeDataThread, NULL, 0, &ProcessCallTreeDataThreadID);
								}
							}
						}
						break;

					case IDM_RESET:
						{
							DialogBox(hInst, MAKEINTRESOURCE(IDD_RESETID), hWnd, ResetModalDialog);
						}
						break;

					case IDM_THREADID:
						{
							DialogBox(hInst, MAKEINTRESOURCE(IDD_THREADID), hWnd, ThreadIdModalDialog);
						}
						break;

					default:
						return DefWindowProc(hWnd, message, wParam, lParam);
				}
			}
			break;

		case WM_CAPTURECALLTREEDONE:
			bIsCaptureInProgress = false;

			CloseHandle(ProcessCallTreeDataThreadHandle);

			PostMessage(ghDialogWnd, WM_DISPLAYCALLTREEDATA, 0, 0);
			break;

		case WM_DISPLAYCALLTREEDATA:
			DisplayCallTreeData();
			break;

		case WM_DESTROY:
			PostQuitMessage(0);
			break;

		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
	}

	return 0;
}

LRESULT CALLBACK WndProcLeftChildren(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message)
	{
		case WM_CREATE:
		{
			hChildWindowFunctions = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, TEXT(""),
				WS_CHILD  | WS_BORDER | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_ALIGNLEFT | LVS_OWNERDATA,
				0, 0, 0, 0, hWnd, NULL, hInst, NULL );

			hChildWindowTextViewer = CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), TEXT(""), 
				WS_VSCROLL | ES_AUTOVSCROLL | WS_HSCROLL | ES_AUTOHSCROLL | WS_CHILD | WS_VISIBLE | ES_MULTILINE | WS_CLIPCHILDREN, 0, 0, 0, 0, 
				hWnd, 0, hInst, 0);

			// subclass the EDIT control so that we can intercept keystrokes (to make the EDIT control "read only")
			// https://msdn.microsoft.com/en-us/library/bb773183%28VS.85%29.aspx
			if( !SetWindowSubclass(hChildWindowTextViewer, WndEditSubclassProc, 0, 0) )
			{
				DWORD err = GetLastError();
				DebugLog("WndProcLeftChildren(): SetWindowSubclass() failed - err = %d", err);
				return 0;
			}

			HFONT hFont = CreateFont(14, 0, 0, 0, 0, FALSE, FALSE, FALSE, 0, 0, 0, 0, FIXED_PITCH, TEXT("Courier"));
			SendMessage(hChildWindowTextViewer, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));

			return 0;
		}

		case WM_SPLITTER_PERCENT:
			if( gConfig )
			{
				float percent = *(float*)lParam;
				gConfig->SetFloat(CONFIG_LEFT_SPLITTER_PERCENT, percent);
			}
			break;

		case WM_NOTIFY:
			ListViewNotify(hWnd, lParam);
			break;

		default:
			break;
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT CALLBACK WndProcRightChildren(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message)
	{
		case WM_CREATE:
		{
			hChildWindowParentFunctions = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, TEXT(""),
				WS_CHILD  | WS_BORDER | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_ALIGNLEFT | LVS_OWNERDATA,
				0, 0, 0, 0, hWnd, NULL, hInst, NULL );

			hChildWindowChildrenFunctions = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, TEXT(""),
				WS_CHILD  | WS_BORDER | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_ALIGNLEFT | LVS_OWNERDATA,
				0, 0, 0, 0, hWnd, NULL, hInst, NULL );

			return 0;
		}

		case WM_SPLITTER_PERCENT:
			if( gConfig )
			{
				float percent = *(float*)lParam;
				gConfig->SetFloat(CONFIG_RIGHT_SPLITTER_PERCENT, percent);
			}
			break;

		case WM_NOTIFY:
			ListViewNotify(hWnd, lParam);
			break;

		default:
			break;
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT CALLBACK WndEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	switch (uMsg)
	{
		case WM_RBUTTONDOWN:
			return 0;  // we don't want the right click menu (since it allows things like "Paste")

		case WM_CHAR:
			if( wParam == 0x03 )  // allow Ctrl-C to copy text
			{
				return DefSubclassProc(hWnd, uMsg, wParam, lParam);
			}
			return 0;  // don't allow any other keystroke
	}

	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

BOOL CenterWindow (HWND hWnd)
{
    RECT    rRect, rParentRect;
    HWND    hParentWnd;
    int     wParent, hParent, xNew, yNew;
    int     w, h;

    GetWindowRect (hWnd, &rRect);
    w = rRect.right - rRect.left;
    h = rRect.bottom - rRect.top;

    if (NULL == (hParentWnd = GetParent( hWnd )))
       hParentWnd = GetDesktopWindow();

    GetWindowRect( hParentWnd, &rParentRect );

    wParent = rParentRect.right - rParentRect.left;
    hParent = rParentRect.bottom - rParentRect.top;

    xNew = wParent/2 - w/2 + rParentRect.left;
    yNew = hParent/2 - h/2 + rParentRect.top;

    if (xNew < 0) xNew = 0;
    if (yNew < 0) yNew = 0;

    return SetWindowPos (hWnd, NULL, xNew, yNew, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

void ConvertTicksToTime(char* Buffer, size_t buffer_len, __int64 Ticks)
{
	if( Ticks > 10000000 )  // greater than 1 second?
	{
		float FloatTime = (float)Ticks / 10000000.f;
		sprintf_s(Buffer, buffer_len, "%.3f sec", FloatTime);
	}
	else if( Ticks > 10000 )  // greater than 1 millisecond?
	{
		float FloatTime = (float)Ticks / 10000.f;
		sprintf_s(Buffer, buffer_len, "%.3f msec", FloatTime);
	}
	else
	{
		float FloatTime = (float)Ticks / 10.f;
		sprintf_s(Buffer, buffer_len, "%.3f usec", FloatTime);
	}
}

void ConvertTicksToTime(TCHAR* Buffer, size_t buffer_len, __int64 Ticks)
{
	if( Ticks > 10000000 )  // greater than 1 second?
	{
		float FloatTime = (float)Ticks / 10000000.f;
		swprintf(Buffer, buffer_len, TEXT("%.3f sec"), FloatTime);
	}
	else if( Ticks > 10000 )  // greater than 1 millisecond?
	{
		float FloatTime = (float)Ticks / 10000.f;
		swprintf(Buffer, buffer_len, TEXT("%.3f msec"), FloatTime);
	}
	else
	{
		float FloatTime = (float)Ticks / 10.f;
		swprintf(Buffer, buffer_len, TEXT("%.3f usec"), FloatTime);
	}
}

void ConvertTicksToTime(char* Buffer, size_t buffer_len, float AvgTicks)
{
	if( AvgTicks > 10000000.f )  // greater than 1 second?
	{
		float FloatTime = AvgTicks / 10000000.f;
		sprintf_s(Buffer, buffer_len, "%.3f sec", FloatTime);
	}
	else if( AvgTicks > 10000.f )  // greater than 1 millisecond?
	{
		float FloatTime = AvgTicks / 10000.f;
		sprintf_s(Buffer, buffer_len, "%.3f msec", FloatTime);
	}
	else
	{
		float FloatTime = AvgTicks / 10.f;
		sprintf_s(Buffer, buffer_len, "%.3f usec", FloatTime);
	}
}

void ConvertTicksToTime(TCHAR* Buffer, size_t buffer_len, float AvgTicks)
{
	if( AvgTicks > 10000000.f )  // greater than 1 second?
	{
		float FloatTime = AvgTicks / 10000000.f;
		swprintf(Buffer, buffer_len, TEXT("%.3f sec"), FloatTime);
	}
	else if( AvgTicks > 10000.f )  // greater than 1 millisecond?
	{
		float FloatTime = AvgTicks / 10000.f;
		swprintf(Buffer, buffer_len, TEXT("%.3f msec"), FloatTime);
	}
	else
	{
		float FloatTime = AvgTicks / 10.f;
		swprintf(Buffer, buffer_len, TEXT("%.3f usec"), FloatTime);
	}
}

void ConvertTCHARtoCHAR(TCHAR* InBuffer, char* OutBuffer, unsigned int OutBufferSize)
{
	size_t wlen = wcslen(InBuffer);

	if( OutBufferSize <= wlen )
	{
		OutBuffer[0] = 0;
		return;
	}

	size_t num_chars;
	wcstombs_s(&num_chars, OutBuffer, OutBufferSize, InBuffer, wlen);
}

INT_PTR CALLBACK LookupSymbolsModalDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_INITDIALOG:
			ghLookupSymbolsModalDialogWnd = hDlg;

			CenterWindow(hDlg);

			ProcessCallTreeDataThreadHandle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ProcessCallTreeDataThread, NULL, 0, &ProcessCallTreeDataThreadID);

			return (INT_PTR)TRUE;

		case WM_CAPTURECALLTREEDONE:
			bIsCaptureInProgress = false;

			CloseHandle(ProcessCallTreeDataThreadHandle);

			PostMessage(ghDialogWnd, WM_DISPLAYCALLTREEDATA, 0, 0);

			EndDialog(hDlg, LOWORD(wParam));

			return (INT_PTR)TRUE;
	}

	return (INT_PTR)FALSE;
}

INT_PTR CALLBACK ResetModalDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_INITDIALOG:
			CenterWindow(hDlg);
			return (INT_PTR)TRUE;

		case WM_COMMAND:
			if( LOWORD(wParam) == IDYES )
			{
				ResetCallTreeData();

				EndDialog(hDlg, LOWORD(wParam));
				return (INT_PTR)TRUE;
			}
			else if( LOWORD(wParam) == IDNO )
			{
				EndDialog(hDlg, LOWORD(wParam));
				return (INT_PTR)TRUE;
			}
			break;
	}

	return (INT_PTR)FALSE;
}

INT_PTR CALLBACK ThreadIdModalDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HDC hDC;
	TEXTMETRIC tm;
	SIZE size;
	TCHAR buffer[1024];
	TCHAR wSymbolName[1024];

	static DWORD ProcessThreadIds[512];
	static unsigned int NumberOfThreads = 0;

	switch (message)
	{
		case WM_INITDIALOG:
			{
				DWORD PID = GetCurrentProcessId();

				// get the list of threads running in this process
				HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
				if (hSnapshot != INVALID_HANDLE_VALUE)
				{
					THREADENTRY32 te;
					te.dwSize = sizeof(te);
					if( Thread32First(hSnapshot, &te) )
					{
						do
						{
							if( te.th32OwnerProcessID == PID )
							{
								ProcessThreadIds[NumberOfThreads++] = te.th32ThreadID;
							}
						} while( Thread32Next(hSnapshot, &te) );
					}

					CloseHandle(hSnapshot);
				}

				hDC = GetDC( hDlg );
				SelectObject(hDC, (HFONT)SendDlgItemMessage(hDlg, IDC_THREADID_LIST, WM_GETFONT, NULL, NULL));
				GetTextMetrics( hDC, &tm );

				CenterWindow(hDlg);

				SendDlgItemMessage(hDlg, IDC_THREADID_LIST, LB_RESETCONTENT, 0, 0);

				int max_len = 0;
				size_t buffer_len = _countof(buffer);
				size_t wSymbolName_len = _countof(wSymbolName);

				// populate the ListBox
				for( unsigned int ThreadIndex = 0; ThreadIndex < CaptureCallTreeThreadArraySize; ThreadIndex++ )
				{
					DialogThreadIdRecord_t* ThreadRec = (DialogThreadIdRecord_t*)CaptureCallTreeThreadArrayPointer[ThreadIndex];

					if( ThreadRec )
					{
						// see if the thread id is in the list of threads currently running in this process
						bool bIsThreadRunning = false;
						for( unsigned int index = 0; index < NumberOfThreads; index++ )
						{
							if( ProcessThreadIds[index] == ThreadRec->ThreadId )
							{
								bIsThreadRunning = true;
								break;
							}
						}

						bool bShowInactiveThreads = true;  // OPTIONS
						bool bShowThreadsWithNoData = true;  // NAME THIS SOMETHING BETTER

						bool bShouldShowThread = (bShowInactiveThreads || bIsThreadRunning) && (bShowThreadsWithNoData || (ThreadRec->CallTreeArraySize > 0));

						if( bShouldShowThread )
						{
							size_t len = min(wSymbolName_len, strlen(ThreadRec->SymbolName));
							size_t num_chars;
							mbstowcs_s(&num_chars, wSymbolName, wSymbolName_len, ThreadRec->SymbolName, len);
							wSymbolName[num_chars] = 0;

							swprintf(buffer, buffer_len, TEXT("%s (ThreadId = %d)"), wSymbolName, ThreadRec->ThreadId);

							int listbox_index = (int)SendDlgItemMessage(hDlg, IDC_THREADID_LIST, LB_ADDSTRING, 0, (LPARAM)buffer);
							SendDlgItemMessage(hDlg, IDC_THREADID_LIST, LB_SETITEMDATA, (WPARAM)listbox_index, (LPARAM)ThreadRec);  // store the DialogThreadIdRecord_t so get can get it later

							len = wcslen(buffer);

							GetTextExtentPoint32(hDC, buffer, (int)len, &size);

							if( size.cx > max_len )
							{
								max_len = size.cx;
							}
						}
					}
				}

				SendDlgItemMessage( hDlg, IDC_THREADID_LIST, LB_SETHORIZONTALEXTENT, max_len + tm.tmAveCharWidth, 0);

				ReleaseDC( hDlg, hDC );

				return (INT_PTR)TRUE;
			}

		case WM_COMMAND:
			if( (LOWORD(wParam) == IDOK) || ((LOWORD(wParam) == IDC_THREADID_LIST) && (HIWORD(wParam) == LBN_DBLCLK)) )
			{
				EndDialog(hDlg, LOWORD(wParam));

				int listbox_index = (int)SendDlgItemMessage(hDlg, IDC_THREADID_LIST, LB_GETCURSEL, 0, 0);

				if( listbox_index >= 0 )
				{
					DialogThreadIdRecord_t* ThreadRec = (DialogThreadIdRecord_t*)SendDlgItemMessage(hDlg, IDC_THREADID_LIST, LB_GETITEMDATA, listbox_index, 0);

					DialogCallTreeThreadId = ThreadRec->ThreadId;

					PostMessage(ghDialogWnd, WM_DISPLAYCALLTREEDATA, 0, 0);
				}

				return (INT_PTR)TRUE;
			}
			else if( LOWORD(wParam) == IDCANCEL )
			{
				EndDialog(hDlg, LOWORD(wParam));
				return (INT_PTR)TRUE;
			}
			break;
	}

	return (INT_PTR)FALSE;
}

INT_PTR CALLBACK StatsModalDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	TCHAR buffer[256];
	size_t buffer_len = _countof(buffer);
	size_t TotalSize, FreeSize;
	size_t callrec_total_size, callrec_free_size;
	size_t symbol_total_size, symbol_free_size;
	size_t dialog_total_size, dialog_free_size;
	size_t textviewer_total_size, textviewer_free_size;
	size_t OverheadSize;

	switch (message)
	{
		case WM_INITDIALOG:
			CenterWindow(hDlg);

			swprintf(buffer, buffer_len, TEXT("Number of Threads: %d"), NumThreads);
			SetDlgItemText(hDlg, IDC_STATIC_THREADS, buffer);

			swprintf(buffer, buffer_len, TEXT("Number of Call Tree Records: %d"), NumCallTreeRecords);
			SetDlgItemText(hDlg, IDC_STATIC_CALLREC, buffer);

			TotalSize = 0;
			FreeSize = 0;

			GlobalAllocator.GetAllocationStats(callrec_total_size, callrec_free_size);
			SymbolAllocator.GetAllocationStats(symbol_total_size, symbol_free_size);
			DialogAllocator.GetAllocationStats(dialog_total_size, dialog_free_size);
			TextViewerAllocator.GetAllocationStats(textviewer_total_size, textviewer_free_size);

			TotalSize = callrec_total_size + symbol_total_size + dialog_total_size + textviewer_total_size;

			swprintf(buffer, buffer_len, TEXT("Total Memory Used: %zd"), TotalSize);
			SetDlgItemText(hDlg, IDC_STATIC_MEM_TOTAL, buffer);

			swprintf(buffer, buffer_len, TEXT("Total Memory Used for Call Records: %zd"), callrec_total_size - callrec_free_size);
			SetDlgItemText(hDlg, IDC_STATIC_MEM_CALLREC, buffer);

			swprintf(buffer, buffer_len, TEXT("Total Memory Used for Dialog ListView: %zd"), dialog_total_size - dialog_free_size);
			SetDlgItemText(hDlg, IDC_STATIC_MEM_LISTVIEW, buffer);

			swprintf(buffer, buffer_len, TEXT("Total Memory Used for Symbols: %zd"), symbol_total_size - symbol_free_size);
			SetDlgItemText(hDlg, IDC_STATIC_MEM_SYMBOLS, buffer);

			OverheadSize = callrec_free_size + symbol_free_size + dialog_free_size + textviewer_free_size;
			swprintf(buffer, buffer_len, TEXT("Memory Overhead for Data Structures: %zd"), OverheadSize);
			SetDlgItemText(hDlg, IDC_STATIC_MEM_OVERHEAD, buffer);

			return (INT_PTR)TRUE;

		case WM_COMMAND:
			if( LOWORD(wParam) == IDOK )
			{
				EndDialog(hDlg, LOWORD(wParam));
				return (INT_PTR)TRUE;
			}
			break;
	}

	return (INT_PTR)FALSE;
}
