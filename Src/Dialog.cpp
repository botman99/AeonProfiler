
#include "targetver.h"
#include "resource.h"

#include <Windows.h>
#include <Commctrl.h>
#include <windowsx.h>
#include <TlHelp32.h>
#include <Shlwapi.h>

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
HWND ghFindSymbolDialogParent;  // global hWnd for the ListView used to call FindSymbolModalDialog() since WS_POPUP windows won't have the proper Parent set

HANDLE ProcessCallTreeDataThreadHandle = NULL;
DWORD ProcessCallTreeDataThreadID = 0;
bool bIsCaptureInProgress = false;  // don't allow a capture to start while one is already in progress

DWORD DialogCallTreeThreadId;  // the thread id of the thread to display CallTree data for in the ListView dialog


BOOL InitInstance(HINSTANCE hInstance, int nCmdShow);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK WndProcLeftChildren(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK WndProcRightChildren(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK WndEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
LRESULT CALLBACK WndListViewSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);


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

			// subclass the ListView control so that we can intercept keystrokes
			// https://msdn.microsoft.com/en-us/library/bb773183%28VS.85%29.aspx
			if( !SetWindowSubclass(hChildWindowFunctions, WndListViewSubclassProc, 0, 0) )
			{
				DWORD err = GetLastError();
				DebugLog("WndProcLeftChildren(): SetWindowSubclass() for ListView failed - err = %d", err);
				return 0;
			}

			hChildWindowTextViewer = CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), TEXT(""), 
				WS_VSCROLL | ES_AUTOVSCROLL | WS_HSCROLL | ES_AUTOHSCROLL | WS_CHILD | WS_VISIBLE | ES_MULTILINE | WS_CLIPCHILDREN, 0, 0, 0, 0, 
				hWnd, 0, hInst, 0);

			// subclass the EDIT control so that we can intercept keystrokes (to make the EDIT control "read only")
			// https://msdn.microsoft.com/en-us/library/bb773183%28VS.85%29.aspx
			if( !SetWindowSubclass(hChildWindowTextViewer, WndEditSubclassProc, 0, 0) )
			{
				DWORD err = GetLastError();
				DebugLog("WndProcLeftChildren(): SetWindowSubclass() for EDIT failed - err = %d", err);
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

			// subclass the ListView control so that we can intercept keystrokes
			// https://msdn.microsoft.com/en-us/library/bb773183%28VS.85%29.aspx
			if( !SetWindowSubclass(hChildWindowParentFunctions, WndListViewSubclassProc, 0, 0) )
			{
				DWORD err = GetLastError();
				DebugLog("WndProcRightChildren(): SetWindowSubclass() for Parent ListView failed - err = %d", err);
				return 0;
			}

			hChildWindowChildrenFunctions = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, TEXT(""),
				WS_CHILD  | WS_BORDER | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_ALIGNLEFT | LVS_OWNERDATA,
				0, 0, 0, 0, hWnd, NULL, hInst, NULL );

			// subclass the ListView control so that we can intercept keystrokes
			// https://msdn.microsoft.com/en-us/library/bb773183%28VS.85%29.aspx
			if( !SetWindowSubclass(hChildWindowChildrenFunctions, WndListViewSubclassProc, 0, 0) )
			{
				DWORD err = GetLastError();
				DebugLog("WndProcRightChildren(): SetWindowSubclass() for Children ListView failed - err = %d", err);
				return 0;
			}

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


class CClipboardOutput
{
public:
	CClipboardOutput() {};
	~CClipboardOutput() {};

	size_t Log(char* output, const char* format, ... )
	{
		static char buffer[4096];

		va_list args;
		va_start(args, format);
		vsnprintf_s(buffer, sizeof(buffer), sizeof(buffer)-1, format, args);

		size_t length = strlen(buffer) + 1;

		if( output )
		{
			memcpy(output, buffer, length);
		}

		va_end (args);

		return length;  // note: length includes the null terminator
	}
};


LRESULT CALLBACK WndListViewSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	switch (uMsg)
	{
		case WM_CHAR:
			{
				if( wParam == 0x06 )  // ctrl-F
				{
					if( (hWnd == hChildWindowFunctions) || (hWnd == hChildWindowParentFunctions) || (hWnd == hChildWindowChildrenFunctions) )
					{
						ghFindSymbolDialogParent = hWnd;

						int row = (int)DialogBox(hInst, MAKEINTRESOURCE(IDD_FINDSYMBOL), hWnd, FindSymbolModalDialog);

						if( row >= 0 )
						{
							SetFocus(ghDialogWnd);  // set focus back to main dialog (after closing the DialogBox

							DialogThreadIdRecord_t* ListView_ThreadIdRecord = (DialogThreadIdRecord_t*)CaptureCallTreeThreadArrayPointer[DialogListViewThreadIndex];
							assert(ListView_ThreadIdRecord);

							SetFocus(ghFindSymbolDialogParent);  // set focus to ListView (so selected line will be highlighted)

							ListViewSetRowSelected(ghFindSymbolDialogParent, row, ListView_ThreadIdRecord, (ghFindSymbolDialogParent == hChildWindowFunctions));
						}

						return 0;
					}
				}

				return DefSubclassProc(hWnd, uMsg, wParam, lParam);
			}

		case WM_RBUTTONDOWN:
			{
				SetFocus(hWnd);

				HMENU hPopupMenu = CreatePopupMenu();

				AppendMenu(hPopupMenu, MF_ENABLED | MF_STRING, 1, TEXT("Copy to Clipboard as Formatted Text"));
				AppendMenu(hPopupMenu, MF_ENABLED | MF_STRING, 2, TEXT("Copy to Clipboard as Comma Separated Values (CSV format)"));
				AppendMenu(hPopupMenu, MF_ENABLED | MF_STRING, 3, TEXT("Copy to Clipboard as Comma Separated Values (Text format)"));

				POINT cursor_pos;
				GetCursorPos(&cursor_pos);
				SetForegroundWindow(hWnd);

				int result = TrackPopupMenu(hPopupMenu, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RETURNCMD, cursor_pos.x, cursor_pos.y, 0, hWnd, NULL);

				switch ( result )
				{
					// To output data to the clipboard, we go through the data in two passes.  First pass determines the total length of the buffer
					// that needs to be allocated to hold all the data, then we allocate that buffer using VirtualAlloc.  In the second pass, we go
					// through the data outputting EXACTLY the same thing again except this time we output it to the buffer we just allocated.  We
					// then copy this buffer to the clipboard and free the buffer using VirtualFree.

					case 1:  // copy as formatted text
						{
							CClipboardOutput clipboard_output;

							char* txt_functions_name_header = {"Functions"};
							char* txt_parents_name_header = {"Parents"};
							char* txt_children_name_header = {"Children"};

							// NOTE: The header text here does not include the function name (which is of variable length).  It is handled separately.
							// We format time values as 14 characters wide.  This gives us 6 digits before the decimal point, the decimal, 3 digits more plus a space, 4 characters (see ConvertTicksToTime() for details)
							char* txt_header = {"Times Called  Exclusive Time Sum  Inclusive Time Sum  Avg. Exclusive Time  Avg. Inclusive Time  Max Recursion  Max Exclusive Time"};

							int number_rows = 0;
							size_t name_header_length = 0;

							// find the length of the largest function name (so we can format all function names to the same width)

							DialogThreadIdRecord_t* ListView_thread_record = (DialogThreadIdRecord_t*)CaptureCallTreeThreadArrayPointer[DialogListViewThreadIndex];
							assert(ListView_thread_record);

							// determine the number of rows and set the length of the header text for the function category
							if( hWnd == hChildWindowFunctions )
							{
								number_rows = ListView_thread_record->CallTreeArraySize;
								name_header_length = strlen(txt_functions_name_header);
							}
							else if( hWnd == hChildWindowParentFunctions )
							{
								DialogCallTreeRecord_t* ListView_CallTreeRecord = (DialogCallTreeRecord_t*)ListView_thread_record->CallTreeArray[ListViewRowSelectedFunctions];
								assert(ListView_CallTreeRecord);

								number_rows =  ListView_CallTreeRecord->ParentArraySize;
								name_header_length = strlen(txt_parents_name_header);
							}
							else if( hWnd == hChildWindowChildrenFunctions )
							{
								DialogCallTreeRecord_t* ListView_CallTreeRecord = (DialogCallTreeRecord_t*)ListView_thread_record->CallTreeArray[ListViewRowSelectedFunctions];
								assert(ListView_CallTreeRecord);

								number_rows =  ListView_CallTreeRecord->ChildrenArraySize;
								name_header_length = strlen(txt_children_name_header);
							}

							size_t max_function_name_length = name_header_length;

							// determine the max length of the function names
							for( int row = 0; row < number_rows; ++row )
							{
								DialogCallTreeRecord_t* ListView_record = GetListViewRecordForRow(hWnd, row);

								size_t len = strlen(ListView_record->SymbolName);

								if( len > max_function_name_length )
								{
									max_function_name_length = len;
								}
							}

							size_t number_of_spaces_after_header = max_function_name_length - name_header_length;

							size_t total_length = 0;

							// calculate the length of the formatted header
							if( hWnd == hChildWindowFunctions )
							{
								// don't count the null terminator
								total_length += clipboard_output.Log(nullptr, txt_functions_name_header) - 1;
							}
							else if( hWnd == hChildWindowParentFunctions )
							{
								// don't count the null terminator
								total_length += clipboard_output.Log(nullptr, txt_parents_name_header) - 1;
							}
							else if( hWnd == hChildWindowChildrenFunctions )
							{
								// don't count the null terminator
								total_length += clipboard_output.Log(nullptr, txt_children_name_header) - 1;
							}

							total_length += number_of_spaces_after_header + 2;  // we add 2 spaces after each column
							total_length += clipboard_output.Log(nullptr, txt_header) - 1;

							char FormatBuffer[16];
							int FormatBufferLen = sizeof(FormatBuffer);

							// calculate the length of each of the rows
							for( int row = 0; row < number_rows; ++row )
							{
								DialogCallTreeRecord_t* ListView_record = GetListViewRecordForRow(hWnd, row);

								if( ListView_record )
								{
									total_length += 1 + max_function_name_length + 2;  // newline plus function name max length plus 2 spaces

									total_length += clipboard_output.Log(nullptr, "%12d  ", ListView_record->CallCount) - 1;

									ConvertTicksToTime(FormatBuffer, FormatBufferLen, ListView_record->CallDurationExclusiveTimeSum);
									total_length += clipboard_output.Log(nullptr, "%18s  ", FormatBuffer) - 1;

									ConvertTicksToTime(FormatBuffer, FormatBufferLen, ListView_record->CallDurationInclusiveTimeSum);
									total_length += clipboard_output.Log(nullptr, "%18s  ", FormatBuffer) - 1;

									float ExclusiveTimeAvg = (ListView_record->CallCount > 0) ? ((float)ListView_record->CallDurationExclusiveTimeSum / (float)ListView_record->CallCount) : 0.f;
									ConvertTicksToTime(FormatBuffer, FormatBufferLen, ExclusiveTimeAvg);
									total_length += clipboard_output.Log(nullptr, "%19s  ", FormatBuffer) - 1;

									float InclusiveTimeAvg = (ListView_record->CallCount > 0) ? ((float)ListView_record->CallDurationInclusiveTimeSum / (float)ListView_record->CallCount) : 0.f;
									ConvertTicksToTime(FormatBuffer, FormatBufferLen, InclusiveTimeAvg);
									total_length += clipboard_output.Log(nullptr, "%19s  ", FormatBuffer) - 1;

									total_length += clipboard_output.Log(nullptr, "%13d  ", ListView_record->MaxRecursionLevel) - 1;

									ConvertTicksToTime(FormatBuffer, FormatBufferLen, ListView_record->MaxCallDurationExclusiveTime);
									total_length += clipboard_output.Log(nullptr, "%18s", FormatBuffer) - 1;  // no spaces at the end of the last field
								}
							}

							size_t page_size = ((total_length / 4096) + 1) * 4096;  // page size is multiple of 4K

							char* AllocPtr = (char*)VirtualAlloc( NULL, page_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );

							if( AllocPtr )
							{
								char* Ptr = AllocPtr;  // Ptr is where we write each formatted string into the VirtualAlloc buffer

								// output the header text
								if( hWnd == hChildWindowFunctions )
								{
									// don't count the null terminator
									Ptr += clipboard_output.Log(Ptr, txt_functions_name_header) - 1;
									for( size_t index = 0; index < number_of_spaces_after_header; ++index )
									{
										Ptr += clipboard_output.Log(Ptr, " ") - 1;
									}
								}
								else if( hWnd == hChildWindowParentFunctions )
								{
									// don't count the null terminator
									Ptr += clipboard_output.Log(Ptr, txt_parents_name_header) - 1;
									for( size_t index = 0; index < number_of_spaces_after_header; ++index )
									{
										Ptr += clipboard_output.Log(Ptr, " ") - 1;
									}
								}
								else if( hWnd == hChildWindowChildrenFunctions )
								{
									// don't count the null terminator
									Ptr += clipboard_output.Log(Ptr, txt_children_name_header) - 1;
									for( size_t index = 0; index < number_of_spaces_after_header; ++index )
									{
										Ptr += clipboard_output.Log(Ptr, " ") - 1;
									}
								}

								Ptr += clipboard_output.Log(Ptr, "  ") - 1;  // we add 2 spaces after each column
								Ptr += clipboard_output.Log(Ptr, txt_header) - 1;

								// output the data for each row
								for( int row = 0; row < number_rows; ++row )
								{
									DialogCallTreeRecord_t* ListView_record = GetListViewRecordForRow(hWnd, row);

									if( ListView_record )
									{
										Ptr += clipboard_output.Log(Ptr, "\n%s", ListView_record->SymbolName) - 1;

										size_t spaces_to_output = max_function_name_length - strlen(ListView_record->SymbolName);

										for( size_t index = 0; index < spaces_to_output + 2; ++index )
										{
											Ptr += clipboard_output.Log(Ptr, " ") - 1;
										}

										Ptr += clipboard_output.Log(Ptr, "%12d  ", ListView_record->CallCount) - 1;

										ConvertTicksToTime(FormatBuffer, FormatBufferLen, ListView_record->CallDurationExclusiveTimeSum);
										Ptr += clipboard_output.Log(Ptr, "%18s  ", FormatBuffer) - 1;

										ConvertTicksToTime(FormatBuffer, FormatBufferLen, ListView_record->CallDurationInclusiveTimeSum);
										Ptr += clipboard_output.Log(Ptr, "%18s  ", FormatBuffer) - 1;

										float ExclusiveTimeAvg = (ListView_record->CallCount > 0) ? ((float)ListView_record->CallDurationExclusiveTimeSum / (float)ListView_record->CallCount) : 0.f;
										ConvertTicksToTime(FormatBuffer, FormatBufferLen, ExclusiveTimeAvg);
										Ptr += clipboard_output.Log(Ptr, "%19s  ", FormatBuffer) - 1;

										float InclusiveTimeAvg = (ListView_record->CallCount > 0) ? ((float)ListView_record->CallDurationInclusiveTimeSum / (float)ListView_record->CallCount) : 0.f;
										ConvertTicksToTime(FormatBuffer, FormatBufferLen, InclusiveTimeAvg);
										Ptr += clipboard_output.Log(Ptr, "%19s  ", FormatBuffer) - 1;

										Ptr += clipboard_output.Log(Ptr, "%13d  ", ListView_record->MaxRecursionLevel) - 1;

										ConvertTicksToTime(FormatBuffer, FormatBufferLen, ListView_record->MaxCallDurationExclusiveTime);
										Ptr += clipboard_output.Log(Ptr, "%18s", FormatBuffer) - 1;  // no spaces at the end of the last field
									}
								}

								*Ptr = 0;  // add null terminator at the end of the buffer

								HGLOBAL hMem =  GlobalAlloc(GMEM_MOVEABLE, total_length+1);  // plus one for null terminator
								memcpy(GlobalLock(hMem), AllocPtr, total_length+1);
								GlobalUnlock(hMem);

								OpenClipboard(hWnd);
								EmptyClipboard();
								SetClipboardData(CF_TEXT, hMem);
								CloseClipboard();

								VirtualFree(AllocPtr, page_size, MEM_RELEASE);
							}
						}
						break;

					case 2:  // copy as Comma Separated Values (CVS format)
					case 3:  // copy as Comma Separated Values (Text format)
						{
							CClipboardOutput clipboard_output;

							size_t total_length = 0;

							char* csv_header_functions = {"\"Function\",\"Times Called\",\"Exclusive Time Sum (usec)\",\"Inclusive Time Sum (usec)\",\"Avg. Exclusive Time (usec)\",\"Avg. Inclusive Time (usec)\",\"Max Recursion\",\"Max Exclusive Time (usec)\""};
							char* csv_header_parents = {"\"Parents\",\"Times Called\",\"Exclusive Time Sum (usec)\",\"Inclusive Time Sum (usec)\",\"Avg. Exclusive Time (usec)\",\"Avg. Inclusive Time (usec)\",\"Max Recursion\",\"Max Exclusive Time (usec)\""};
							char* csv_header_children = {"\"Children\",\"Times Called\",\"Exclusive Time Sum (usec)\",\"Inclusive Time Sum (usec)\",\"Avg. Exclusive Time (usec)\",\"Avg. Inclusive Time (usec)\",\"Max Recursion\",\"Max Exclusive Time (usec)\""};

							// these are the format text for the Log() call to output the call tree record data as CSV
							char* csv_line = {"\n\"%s\",%d,%I64d,%I64d,%I64d,%I64d,%d,%I64d"};

							int number_rows = 0;

							DialogThreadIdRecord_t* ListView_thread_record = (DialogThreadIdRecord_t*)CaptureCallTreeThreadArrayPointer[DialogListViewThreadIndex];
							assert(ListView_thread_record);

							// calculate the length of the header and determine the number of rows of data
							if( hWnd == hChildWindowFunctions )
							{
								total_length += clipboard_output.Log(nullptr, csv_header_functions) - 1;  // don't count the null terminator

								number_rows = ListView_thread_record->CallTreeArraySize;
							}
							else if( hWnd == hChildWindowParentFunctions )
							{
								total_length += clipboard_output.Log(nullptr, csv_header_parents) - 1;  // don't count the null terminator

								DialogCallTreeRecord_t* ListView_CallTreeRecord = (DialogCallTreeRecord_t*)ListView_thread_record->CallTreeArray[ListViewRowSelectedFunctions];
								assert(ListView_CallTreeRecord);

								number_rows =  ListView_CallTreeRecord->ParentArraySize;
							}
							else if( hWnd == hChildWindowChildrenFunctions )
							{
								total_length += clipboard_output.Log(nullptr, csv_header_children) - 1;  // don't count the null terminator

								DialogCallTreeRecord_t* ListView_CallTreeRecord = (DialogCallTreeRecord_t*)ListView_thread_record->CallTreeArray[ListViewRowSelectedFunctions];
								assert(ListView_CallTreeRecord);

								number_rows =  ListView_CallTreeRecord->ChildrenArraySize;
							}

							// calculate the length of each of the rows
							for( int row = 0; row < number_rows; ++row )
							{
								DialogCallTreeRecord_t* ListView_record = GetListViewRecordForRow(hWnd, row);

								if( ListView_record )
								{
									long long ExclusiveTimeAvg = (ListView_record->CallCount > 0) ? (ListView_record->CallDurationExclusiveTimeSum / (ListView_record->CallCount * 10)) : 0;
									long long InclusiveTimeAvg = (ListView_record->CallCount > 0) ? (ListView_record->CallDurationInclusiveTimeSum / (ListView_record->CallCount * 10)) : 0;

									total_length += clipboard_output.Log(nullptr, csv_line,
																			ListView_record->SymbolName,
																			ListView_record->CallCount,
																			ListView_record->CallDurationExclusiveTimeSum / 10,
																			ListView_record->CallDurationInclusiveTimeSum / 10,
																			ExclusiveTimeAvg,
																			InclusiveTimeAvg,
																			ListView_record->MaxRecursionLevel,
																			ListView_record->MaxCallDurationExclusiveTime / 10) - 1;  // don't count the null terminator
								}
							}

							size_t page_size = ((total_length / 4096) + 1) * 4096;  // page size is multiple of 4K

							char* AllocPtr = (char*)VirtualAlloc( NULL, page_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );

							if( AllocPtr )
							{
								char* Ptr = AllocPtr;  // Ptr is where we write each formatted string into the VirtualAlloc buffer

								// output the header text
								if( hWnd == hChildWindowFunctions )
								{
									Ptr += clipboard_output.Log(Ptr, csv_header_functions) - 1;  // don't count the null terminator
								}
								else if( hWnd == hChildWindowParentFunctions )
								{
									Ptr += clipboard_output.Log(Ptr, csv_header_parents) - 1;  // don't count the null terminator
								}
								else if( hWnd == hChildWindowChildrenFunctions )
								{
									Ptr += clipboard_output.Log(Ptr, csv_header_children) - 1;  // don't count the null terminator
								}

								// output the data for each row
								for( int row = 0; row < number_rows; ++row )
								{
									DialogCallTreeRecord_t* ListView_record = GetListViewRecordForRow(hWnd, row);

									if( ListView_record )
									{
										long long ExclusiveTimeAvg = (ListView_record->CallCount > 0) ? (ListView_record->CallDurationExclusiveTimeSum / (ListView_record->CallCount * 10)) : 0;
										long long InclusiveTimeAvg = (ListView_record->CallCount > 0) ? (ListView_record->CallDurationInclusiveTimeSum / (ListView_record->CallCount * 10)) : 0;

										Ptr += clipboard_output.Log(Ptr, csv_line,
																				ListView_record->SymbolName,
																				ListView_record->CallCount,
																				ListView_record->CallDurationExclusiveTimeSum / 10,
																				ListView_record->CallDurationInclusiveTimeSum / 10,
																				ExclusiveTimeAvg,
																				InclusiveTimeAvg,
																				ListView_record->MaxRecursionLevel,
																				ListView_record->MaxCallDurationExclusiveTime / 10) - 1;  // don't count the null terminator
									}
								}

								*Ptr = 0;  // add null terminator at the end of the buffer

								HGLOBAL hMem =  GlobalAlloc(GMEM_MOVEABLE, total_length+1);  // plus one for null terminator
								memcpy(GlobalLock(hMem), AllocPtr, total_length+1);
								GlobalUnlock(hMem);

								OpenClipboard(hWnd);
								EmptyClipboard();

								if( result == 2 )
								{
									UINT format = RegisterClipboardFormat(TEXT("Csv"));
									if( format )
									{
										SetClipboardData(format, hMem);
									}
								}
								else if( result == 3 )
								{
									SetClipboardData(CF_TEXT, hMem);
								}

								CloseClipboard();

								VirtualFree(AllocPtr, page_size, MEM_RELEASE);
							}
						}
						break;
				}

				DestroyMenu(hPopupMenu);

				return 0;
			}

		default:
			break;
	}

	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
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

		case WM_KILLFOCUS:
			return 0;  // don't hide the caret

		default:
			break;
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

	hParentWnd = GetParent(hWnd);

    if( hParentWnd == NULL )
	{
       hParentWnd = GetDesktopWindow();
	}

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

#define MAX_FUNCTION_NAME_LEN 2048

INT_PTR CALLBACK FindSymbolModalDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_INITDIALOG:
			{
				CenterWindow(hDlg);

				TCHAR buffer[64];
				swprintf(buffer, sizeof(buffer), TEXT("Find Function by name - %s"), (ghFindSymbolDialogParent == hChildWindowFunctions) ? TEXT("Functions") : (ghFindSymbolDialogParent == hChildWindowParentFunctions) ? TEXT("Parents") : TEXT("Children"));

				SetWindowText(hDlg, buffer);

				int case_sensitive = 0;

				if( gConfig )
				{
					case_sensitive = gConfig->GetInt(CONFIG_FIND_FUNCTION_CASE_SENSITIVE);
				}

				CheckDlgButton(hDlg, IDC_CASE_SENSITIVE, case_sensitive ? BST_CHECKED : BST_UNCHECKED);

				HWND hWndList = GetDlgItem(hDlg, IDC_FUNCTIONLIST);
				SendMessage(hWndList, LB_RESETCONTENT, 0, 0);

				HWND hWndEditControl = GetDlgItem(hDlg, IDC_FUNCTIONNAME);
				SetFocus(hWndEditControl);  // automatically set focus to the edit control for the function name

				return (INT_PTR)TRUE;
			}

			break;

		case WM_COMMAND:
			{
				switch(LOWORD(wParam))
				{
					case IDOK:  // Enter was pressed in the "Enter Function Name to search for" edit control
					{
						if( DialogListViewThreadIndex >= 0 )
						{
							HWND hWndList = GetDlgItem(hDlg, IDC_FUNCTIONLIST);
							SendMessage(hWndList, LB_RESETCONTENT, 0, 0);

							HDC hDC = GetDC( hDlg );
							SelectObject(hDC, (HFONT)SendDlgItemMessage(hDlg, IDC_FUNCTIONLIST, WM_GETFONT, NULL, NULL));

							TEXTMETRIC tm;
							GetTextMetrics( hDC, &tm );

							DialogThreadIdRecord_t* ListView_thread_record = (DialogThreadIdRecord_t*)CaptureCallTreeThreadArrayPointer[DialogListViewThreadIndex];
							assert(ListView_thread_record);

							int number_rows = 0;

							// determine the number of rows and set the length of the header text for the function category
							if( ghFindSymbolDialogParent == hChildWindowFunctions )
							{
								number_rows = ListView_thread_record->CallTreeArraySize;
							}
							else if( ghFindSymbolDialogParent == hChildWindowParentFunctions )
							{
								DialogCallTreeRecord_t* ListView_CallTreeRecord = (DialogCallTreeRecord_t*)ListView_thread_record->CallTreeArray[ListViewRowSelectedFunctions];
								assert(ListView_CallTreeRecord);

								number_rows =  ListView_CallTreeRecord->ParentArraySize;
							}
							else if( ghFindSymbolDialogParent == hChildWindowChildrenFunctions )
							{
								DialogCallTreeRecord_t* ListView_CallTreeRecord = (DialogCallTreeRecord_t*)ListView_thread_record->CallTreeArray[ListViewRowSelectedFunctions];
								assert(ListView_CallTreeRecord);

								number_rows =  ListView_CallTreeRecord->ChildrenArraySize;
							}

							TCHAR wbuffer[MAX_FUNCTION_NAME_LEN];
							GetDlgItemText(hDlg, IDC_FUNCTIONNAME, wbuffer, _countof(wbuffer));

							char buffer[MAX_FUNCTION_NAME_LEN];
							ConvertTCHARtoCHAR(wbuffer, buffer, sizeof(buffer));

							bool case_sensitive = (IsDlgButtonChecked(hDlg, IDC_CASE_SENSITIVE) == BST_CHECKED);

							bool bWasMatchFound = false;
							int max_len = 0;

							for( int row = 0; row < number_rows; ++row )
							{
								DialogCallTreeRecord_t* ListView_record = GetListViewRecordForRow(ghFindSymbolDialogParent, row);

								size_t len = strlen(ListView_record->SymbolName);

								if( (case_sensitive && (StrStrA(ListView_record->SymbolName, buffer) != 0)) ||
									(!case_sensitive && (StrStrIA(ListView_record->SymbolName, buffer) != 0)) )
								{
									bWasMatchFound = true;

									size_t len = min(MAX_FUNCTION_NAME_LEN, strlen(ListView_record->SymbolName));
									size_t num_chars;

									mbstowcs_s(&num_chars, wbuffer, MAX_FUNCTION_NAME_LEN, ListView_record->SymbolName, len);

									int pos = (int)SendMessage(hWndList, LB_ADDSTRING, 0, (LPARAM)wbuffer);
									SendMessage(hWndList, LB_SETITEMDATA, pos, (LPARAM)row);

									len = wcslen(wbuffer);

									SIZE size;
									GetTextExtentPoint32(hDC, wbuffer, (int)len, &size);

									if( size.cx > max_len )
									{
										max_len = size.cx;
									}
								}
							}

							SendDlgItemMessage( hDlg, IDC_FUNCTIONLIST, LB_SETHORIZONTALEXTENT, max_len + tm.tmAveCharWidth, 0);

							ReleaseDC( hDlg, hDC );

							EnableWindow( GetDlgItem(hDlg, IDSELECT), bWasMatchFound ? TRUE : FALSE);
						}

						return (INT_PTR)TRUE;
					}

					case IDSELECT:
					{
						if( gConfig )
						{
							UINT checked = (int)IsDlgButtonChecked(hDlg, IDC_CASE_SENSITIVE);
							gConfig->SetInt(CONFIG_FIND_FUNCTION_CASE_SENSITIVE, (checked == BST_CHECKED));
						}

						HWND hWndList = GetDlgItem(hDlg, IDC_FUNCTIONLIST);

						int item = (int)SendMessage(hWndList, LB_GETCURSEL, 0, 0);
						int row = (int)SendMessage(hWndList, LB_GETITEMDATA, item, 0);

						EndDialog(hDlg, row);
						return (INT_PTR)TRUE;
					}

					case IDCANCEL:
					{
						if( gConfig )
						{
							UINT checked = (int)IsDlgButtonChecked(hDlg, IDC_CASE_SENSITIVE);
							gConfig->SetInt(CONFIG_FIND_FUNCTION_CASE_SENSITIVE, (checked == BST_CHECKED));
						}

						EndDialog(hDlg, -1);
						return (INT_PTR)TRUE;
					}

					case IDC_FUNCTIONLIST:
					{
						if( HIWORD(wParam) == LBN_DBLCLK )
						{
							SendMessage(hDlg, WM_COMMAND, IDSELECT, 0);
						}

						return (INT_PTR)TRUE;
					}
				}
			}
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
