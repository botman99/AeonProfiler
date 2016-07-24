
#pragma once

#define WIN32_LEAN_AND_MEAN  // Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <Windows.h>
#include <Commctrl.h>

#include "DebugLog.h"

#include "Allocator.h"
#include "CallTreeRecord.h"
#include "ThreadIdRecord.h"

#define WM_SETSCROLLPOSITION		(WM_USER+1)  /* set the thumbtab scroll position in the source code text file window */
#define WM_CAPTURECALLTREEDONE		(WM_USER+2)  /* notify the main window that the thread capturing the call tree data is done */
#define WM_DISPLAYCALLTREEDATA		(WM_USER+3)  /* notify the main window that the call tree data should be displayed */
#define WM_SPLITTER_PERCENT			(WM_USER+4)  /* notify the window that the splitter position has changed */

#define MAX_LOADSTRING 100

enum SortType
{
	SORT_Unused,
	SORT_Increasing,
	SORT_Decreasing
};

extern HMODULE ModuleHandle;

extern TCHAR app_filename[];  // filename of the application that loaded the DLL
extern DWORD ApplicationProcessId;

extern HANDLE hApplicationProcess;

extern DWORD DialogCallTreeThreadId;  // the thread id of the thread to display CallTree data for in the ListView dialog

extern void** CaptureCallTreeThreadArrayPointer;
extern unsigned int CaptureCallTreeThreadArraySize;

extern int DialogListViewThreadIndex;  // the index of the thread currently selected from the ThreadArray (-1 means invalid thread)

extern int ListViewRowSelectedFunctions;
extern int ListViewRowSelectedParentFunctions;
extern int ListViewRowSelectedChildrenFunctions;

extern HWND hChildWindowCurrentlySorting;  // which child window we are currently sorting

extern HWND hChildWindowFunctions;
extern HWND hChildWindowParentFunctions;
extern HWND hChildWindowChildrenFunctions;
extern HWND hChildWindowTextViewer;

extern HWND ghWnd;  // global hWnd for application window (so we can set the window text in the titlebar)
extern HWND ghDialogWnd;  // global hWnd for this dialog
extern HWND ghLookupSymbolsModalDialogWnd;  // global hWnd for the 'LookupSymbols' dialog


int CaptureCallTreeData();
void WINAPI ProcessCallTreeDataThread(LPVOID lpData);
void ResetCallTreeData();
void DisplayCallTreeData();

void ConvertTicksToTime(char* Buffer, size_t buffer_len, __int64 Ticks);
void ConvertTicksToTime(TCHAR* Buffer, size_t buffer_len, __int64 Ticks);
void ConvertTicksToTime(char* Buffer, size_t buffer_len, float AvgTicks);
void ConvertTicksToTime(TCHAR* Buffer, size_t buffer_len, float AvgTicks);

void ConvertTCHARtoCHAR(TCHAR* InBuffer, char* OutBuffer, unsigned int OutBufferSize);

INT_PTR CALLBACK LookupSymbolsModalDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK ResetModalDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK ThreadIdModalDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK FindSymbolModalDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK StatsModalDialog(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
void GetSourceCodeLineFromAddress(DWORD64 dw64Address, int& LineNumber, char* FileName, int FileNameSize);

void ListViewInitChildWindows();
void ListViewSetFocus(HWND hWnd);
void ListViewSetColumnSortDirection(HWND hWnd, int column, SortType sort_direction);
void ListViewSetRowSelected(HWND hWnd, int row, DialogThreadIdRecord_t* ListView_ThreadIdRecord, bool bIsDoubleClick);
int ListView_SortCallTree(const void* arg1, const void* arg2);
void ListViewNotify(HWND hWnd, LPARAM lParam);

DialogCallTreeRecord_t* GetListViewRecordForRow(HWND hwndFrom, unsigned int row);

int FindRowForAddress(HWND hWnd, const void* Address);
