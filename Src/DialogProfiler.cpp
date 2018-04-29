
#include "targetver.h"
#include "resource.h"

// Windows Header Files:
#include <Windows.h>
#include <Commctrl.h>

#pragma warning( push )
#pragma warning( disable : 4091 )
#include <DbgHelp.h>
#pragma warning( pop )

#include <Psapi.h>

#include "Dialog.h"
#include "TextViewer.h"


extern CHash<CThreadIdRecord>* ThreadIdHashTable;

extern DWORD ApplicationThreadId;
extern int* AeonWinExitPointer;

CAllocator SymbolAllocator(false);  // allocator for storing the symbol names
CAllocator DialogAllocator(false);  // allocator for the Profiler Dialog window

HANDLE hApplicationProcess = nullptr;

void** CaptureCallTreeThreadArrayPointer = nullptr;
unsigned int CaptureCallTreeThreadArraySize = 0;
DWORD64 CaptureCallTreeTime;
int CaptureCallTreeSymbolsToInitialize = 0;

DialogThreadIdRecord_t* gDisplayCallTreeData_ListView_ThreadIdRecordForQSort;


DialogCallTreeRecord_t* FindCallTreeRecord_BinarySearch(const DialogThreadIdRecord_t* ThreadRec, const void* InAddress)
{
	if( (ThreadRec == nullptr) || (ThreadRec->CallTreeArraySize < 1) )
	{
		return nullptr;
	}

	if( ThreadRec->CallTreeArraySize < 4 )
	{
		for( unsigned int index = 0; index < ThreadRec->CallTreeArraySize; index++ )
		{
			DialogCallTreeRecord_t* CallTreeRec = (DialogCallTreeRecord_t*)ThreadRec->CallTreeArray[index];
			if( CallTreeRec->Address == InAddress )
			{
				return CallTreeRec;
			}
		}
	}

	int min_index = 0;
	int max_index = ThreadRec->CallTreeArraySize;

	while( max_index >= min_index )
	{
		int mid_index = (max_index + min_index) / 2;

		DialogCallTreeRecord_t* CallTreeRec = (DialogCallTreeRecord_t*)ThreadRec->CallTreeArray[mid_index];

		if( CallTreeRec->Address == InAddress )
		{
			return CallTreeRec;
		}
		else if( (size_t)CallTreeRec->Address < (size_t)InAddress )
		{
			min_index = mid_index + 1;
		}
		else
		{
			max_index = mid_index - 1;
		}
	}

	return nullptr;  // not found
}

int SortCallTreeHashTableCopy(const void* arg1, const void* arg2)
{
	DialogCallTreeRecord_t* rec1 = *(DialogCallTreeRecord_t**)arg1;
	DialogCallTreeRecord_t* rec2 = *(DialogCallTreeRecord_t**)arg2;
	if( (size_t)rec1->Address < (size_t)rec2->Address )
		return -1;
	else if( (size_t)rec1->Address > (size_t)rec2->Address )
		return 1;
	return 0;
}

int CaptureCallTreeData()  // return the number of symbols that need to be looked up
{
	DialogAllocator.FreeBlocks();  // free all the memory allocated by the DialogAllocator

	if( ThreadIdHashTable == nullptr )
	{
		return -1;  // there's no call tree data captured by the profiler yet, we're done
	}

	int registers[4];
	__cpuid(registers, 0);
	CaptureCallTreeTime = __rdtsc();

	CaptureCallTreeThreadArrayPointer = nullptr;
	CaptureCallTreeThreadArraySize = 0;

	CaptureCallTreeThreadArrayPointer = ThreadIdHashTable->CopyHashToArray(&DialogAllocator, CaptureCallTreeThreadArraySize, true);
	assert(CaptureCallTreeThreadArrayPointer);


	InitializeSymbolLookup();

	CaptureCallTreeSymbolsToInitialize = 0;

	// first, count how many symbol names need to be looked up (since looking up the symbol name can take some time)
	for( unsigned int ThreadIndex = 0; ThreadIndex < CaptureCallTreeThreadArraySize; ThreadIndex++ )
	{
		DialogThreadIdRecord_t* ThreadRec = (DialogThreadIdRecord_t*)CaptureCallTreeThreadArrayPointer[ThreadIndex];

		// for each ThreadIdHashTable, go through the call records (copy of CallTreeHashTable)...
		for( unsigned int CallRecordIndex = 0; CallRecordIndex < ThreadRec->CallTreeArraySize; CallRecordIndex++ )
		{
			DialogCallTreeRecord_t* CallTreeRec = (DialogCallTreeRecord_t*)ThreadRec->CallTreeArray[CallRecordIndex];

			if( CallTreeRec->SymbolName == nullptr )
			{
				CaptureCallTreeSymbolsToInitialize++;
			}
		}
	}

	return CaptureCallTreeSymbolsToInitialize;
}

void WINAPI ProcessCallTreeDataThread(LPVOID lpData)
{
	TCHAR DialogTextBuffer[256];
	int DialogTextBufferLen = _countof(DialogTextBuffer);

	int TotalSymbolsLookedUp = 0;

	if( ghLookupSymbolsModalDialogWnd )
	{
		swprintf(DialogTextBuffer, DialogTextBufferLen, TEXT("%d out of %d"), TotalSymbolsLookedUp, CaptureCallTreeSymbolsToInitialize);
		SetDlgItemText(ghLookupSymbolsModalDialogWnd, IDC_LOOKUPSYMBOLS_TEXT, DialogTextBuffer);
	}

	// go through the thread array (copy of ThreadId hash table)...
	for( unsigned int ThreadIndex = 0; ThreadIndex < CaptureCallTreeThreadArraySize; ThreadIndex++ )
	{
		DialogThreadIdRecord_t* ThreadRec = (DialogThreadIdRecord_t*)CaptureCallTreeThreadArrayPointer[ThreadIndex];

		// sort the CallTreeArray by Address (so we can quickly find a record by Address using a binary search)
		qsort(ThreadRec->CallTreeArray, ThreadRec->CallTreeArraySize, sizeof(void*), SortCallTreeHashTableCopy);

		// set the SymbolName for the main function of the thread...
		char* thread_sym = ThreadRec->SymbolName;
		if( thread_sym == nullptr )
		{
			thread_sym = LookupAddressSymbolName((DWORD64)ThreadRec->Address);
			if( thread_sym )
			{
				size_t length = strlen(thread_sym);
				char* pSymbolName = (char*)SymbolAllocator.AllocateBytes(length+1, 1);  // plus one for the null terminator
				strcpy_s(pSymbolName, length+1, thread_sym);

				ThreadRec->ThreadIdRecord->SetSymbolName(pSymbolName);
				ThreadRec->SymbolName = pSymbolName;
			}
		}

		// go through the Stack and calculate call durations for functions that have been entered but not exited yet...
		for( unsigned int StackIndex = 0; StackIndex < ThreadRec->StackArraySize; ++StackIndex )  // work from top of stack down to bottom
		{
			// handle calculating values the same way that CallerExit() does

			DialogStackCallerData_t& StackRec = ThreadRec->StackArray[StackIndex];

			if( StackRec.CurrentCallTreeRecord )
			{
				// initialize any uninitialized symbol names...
				char* sym = StackRec.CurrentCallTreeRecord->SymbolName;
				if( sym == nullptr )
				{
					sym = LookupAddressSymbolName((DWORD64)StackRec.CurrentCallTreeRecord->Address);
					if( sym )
					{
						size_t length = strlen(sym);
						char* pSymbolName = (char*)SymbolAllocator.AllocateBytes(length+1, 1);  // plus one for the null terminator
						strcpy_s(pSymbolName, length+1, sym);

						StackRec.CurrentCallTreeRecord->CallTreeRecord->SetSymbolName(pSymbolName);
						StackRec.CurrentCallTreeRecord->SymbolName = pSymbolName;
					}
				}
			}

			if( StackRec.CurrentCallTreeRecord && (StackRec.CurrentCallTreeRecord->EnterTime != 0) )
			{
				DialogCallTreeRecord_t* StackCallTreeRec = StackRec.CurrentCallTreeRecord;
				// find the array CallTree record for this stack CallTree record
				DialogCallTreeRecord_t* ArrayCallTreeRec = FindCallTreeRecord_BinarySearch(ThreadRec, StackCallTreeRec->Address);

				if( ArrayCallTreeRec && (ArrayCallTreeRec->EnterTime != 0) )
				{
					// calculate the duration of this function call (subtract _penter time from current time and then subtract the profiler overhead for _penter)
					__int64 CallDuration = (CaptureCallTreeTime - StackRec.Counter) - StackRec.ProfilerOverhead;
					if( CallDuration < 0 )
					{
						CallDuration = 0;
					}
					CallDuration = CallDuration / TicksPerHundredNanoseconds;  // duration is in 100ns units

					// update the total call duration inclusive time for this function...
					ArrayCallTreeRec->CallDurationInclusiveTimeSum += CallDuration;

					if( StackIndex != 0 )  // if we have a parent function
					{
						DialogStackCallerData_t& ParentStackRec = ThreadRec->StackArray[StackIndex-1];

						if( ParentStackRec.CurrentCallTreeRecord && (ParentStackRec.CurrentCallTreeRecord->EnterTime != 0) )
						{
							DialogCallTreeRecord_t* ParentStackCallTreeRec = ParentStackRec.CurrentCallTreeRecord;
							// find the array CallTree record for this stack CallTree record
							DialogCallTreeRecord_t* ParentArrayCallTreeRec = FindCallTreeRecord_BinarySearch(ThreadRec, ParentStackCallTreeRec->Address);
							assert(ParentArrayCallTreeRec);

							// ...add this child function's call duration to the parent's current children inclusive time (so it can subtract that to calculate parent's exclusive time)
							ParentArrayCallTreeRec->CurrentChildrenInclusiveTime += CallDuration;
						}
					}

					ArrayCallTreeRec->EnterTime = 0;  // indicate to the profiler dialog that this function has exited
				}
			}
		}

		if( AeonWinExitPointer == nullptr )  // if not running standalone AeonWin application
		{
			std::vector<std::string> FileList;  // array of unique source code filenames
			std::map<string, int> FileMap;
			int FileNameIndex = 0;

			// for each ThreadId, go through the call records (copy of CallTreeHashTable)...
			for( unsigned int CallRecordIndex = 0; CallRecordIndex < ThreadRec->CallTreeArraySize; CallRecordIndex++ )
			{
				DialogCallTreeRecord_t* CallTreeRec = (DialogCallTreeRecord_t*)ThreadRec->CallTreeArray[CallRecordIndex];

				// initialize any uninitialized symbol names...
				char* sym = CallTreeRec->SymbolName;
				if( sym == nullptr )
				{
					sym = LookupAddressSymbolName((DWORD64)CallTreeRec->Address);
					if( sym )
					{
						size_t length = strlen(sym);
						char* pSymbolName = (char*)SymbolAllocator.AllocateBytes(length+1, 1);  // plus one for the null terminator
						strcpy_s(pSymbolName, length+1, sym);

						CallTreeRec->CallTreeRecord->SetSymbolName(pSymbolName);
						CallTreeRec->SymbolName = pSymbolName;
					}

					TotalSymbolsLookedUp++;
				}

				int LineNumber;
				char FileName[MAX_PATH];

				if( GetSourceCodeLineFromAddress((DWORD64)CallTreeRec->Address, LineNumber, FileName, MAX_PATH) )
				{
					std::string strFileName = FileName;

					if( FileMap.find(strFileName) == FileMap.end() )
					{
						FileList.push_back(strFileName);  // add the new filename to the end of the array (at FileNameIndex)
						FileMap.emplace(FileName, FileNameIndex);  // add the filename to the map to help check for uniqueness
						FileNameIndex++;
					}

					auto FileMap_it = FileMap.find(strFileName);
					int file_index = FileMap_it->second;

					CallTreeRec->SourceFileIndex = file_index;
					CallTreeRec->SourceFileLineNumber = LineNumber;
				}
				else
				{
					CallTreeRec->SourceFileIndex = -1;  // source file name index and line number are unknown
					CallTreeRec->SourceFileLineNumber = -1;
				}

				if( CallTreeRec->EnterTime != 0 )
				{
					CallTreeRec->EnterTime = 0;
				}

				if( ghLookupSymbolsModalDialogWnd && ((TotalSymbolsLookedUp % 100) == 0) )
				{
					swprintf(DialogTextBuffer, DialogTextBufferLen, TEXT("%d out of %d"), TotalSymbolsLookedUp, CaptureCallTreeSymbolsToInitialize);

					SetDlgItemText(ghLookupSymbolsModalDialogWnd, IDC_LOOKUPSYMBOLS_TEXT, DialogTextBuffer);
				}
			}

			ThreadFileListMap.emplace(ThreadRec->ThreadId, FileList);  // add the list of filenames to the ThreadFileListMap
		}

		// since the CopyHashToArray function in the Hash table only copied the original CCallTreeRecord pointers for
		// the ParentHashTable and the ChildrenHashTable, we need to fix these up to point to the Dialog array version
		// of this record instead.

		// for each ThreadId, go through the call records (copy of CallTreeHashTable)...
		for( unsigned int CallRecordIndex = 0; CallRecordIndex < ThreadRec->CallTreeArraySize; CallRecordIndex++ )
		{
			DialogCallTreeRecord_t* CallTreeRec = (DialogCallTreeRecord_t*)ThreadRec->CallTreeArray[CallRecordIndex];

			for( unsigned int ParentIndex = 0; ParentIndex < CallTreeRec->ParentArraySize; ParentIndex++ )
			{
				if( AeonWinExitPointer )  // if running standalone AeonWin application
				{
					DialogCallTreeRecord_t* ParentCallTreeRec = (DialogCallTreeRecord_t*)CallTreeRec->ParentArray[ParentIndex];

					if( ParentCallTreeRec )
					{
						DialogCallTreeRecord_t* ArrayCallTreeRec = FindCallTreeRecord_BinarySearch(ThreadRec, ParentCallTreeRec->Address);
						assert(ArrayCallTreeRec);

						CallTreeRec->ParentArray[ParentIndex] = ArrayCallTreeRec;
					}
				}
				else
				{
					CCallTreeRecord* ParentCallTreeRec = (CCallTreeRecord*)CallTreeRec->ParentArray[ParentIndex];

					if( ParentCallTreeRec )
					{
						DialogCallTreeRecord_t* ArrayCallTreeRec = FindCallTreeRecord_BinarySearch(ThreadRec, ParentCallTreeRec->Address);
						assert(ArrayCallTreeRec);

						CallTreeRec->ParentArray[ParentIndex] = ArrayCallTreeRec;
					}
				}
			}

			for( unsigned int ChildIndex = 0; ChildIndex < CallTreeRec->ChildrenArraySize; ChildIndex++ )
			{
				if( AeonWinExitPointer )  // if running standalone AeonWin application
				{
					DialogCallTreeRecord_t* ChildCallTreeRec = (DialogCallTreeRecord_t*)CallTreeRec->ChildrenArray[ChildIndex];
				}
				else
				{
					CCallTreeRecord* ChildCallTreeRec = (CCallTreeRecord*)CallTreeRec->ChildrenArray[ChildIndex];

					if( ChildCallTreeRec )
					{
						DialogCallTreeRecord_t* ArrayCallTreeRec = FindCallTreeRecord_BinarySearch(ThreadRec, ChildCallTreeRec->Address);
						assert(ArrayCallTreeRec);

						CallTreeRec->ChildrenArray[ChildIndex] = ArrayCallTreeRec;
					}
				}
			}
		}
	}

	if( ghLookupSymbolsModalDialogWnd )
	{
		PostMessage(ghLookupSymbolsModalDialogWnd, WM_CAPTURECALLTREEDONE, 0, 0);
	}
	else
	{
		PostMessage(ghDialogWnd, WM_CAPTURECALLTREEDONE, 0, 0);
	}
}

void ResetCallTreeData()
{
	if( ThreadIdHashTable == nullptr )
	{
		return;  // there's no call tree data captured by the profiler yet, we're done
	}

	int registers[4];
	__cpuid(registers, 0);
	DWORD64 TimeNow = __rdtsc();

	ThreadIdHashTable->ResetCounters(TimeNow);

	DialogAllocator.FreeBlocks();  // free all the memory allocated by the DialogAllocator

	CaptureCallTreeThreadArrayPointer = nullptr;
	CaptureCallTreeThreadArraySize = 0;

	ListViewRowSelectedFunctions = -1;
	ListViewRowSelectedParentFunctions = -1;
	ListViewRowSelectedChildrenFunctions = -1;

	PostMessage(ghDialogWnd, WM_DISPLAYCALLTREEDATA, 0, 0);
}

void WINAPI DisplayCallTreeDataQsortThread(LPVOID lpData)
{
	qsort(gDisplayCallTreeData_ListView_ThreadIdRecordForQSort->CallTreeArray, gDisplayCallTreeData_ListView_ThreadIdRecordForQSort->CallTreeArraySize, sizeof(void*), ListView_SortCallTree);

	extern HWND ghPleaseWaitModalDialogWnd;
	if( ghPleaseWaitModalDialogWnd )
	{
		PostMessage(ghPleaseWaitModalDialogWnd, WM_PLEASEWAITDONE, 0, 0);
	}
}

void DisplayCallTreeData()
{
	TCHAR buffer[1024];
	TCHAR wSymbolName[1024];

	size_t buffer_len = _countof(buffer);
	size_t wSymbolName_len = _countof(wSymbolName);

	DialogListViewThreadIndex = -1;

	if( CaptureCallTreeThreadArrayPointer && CaptureCallTreeThreadArraySize )  // if we have a valid CallTree data capture...
	{
		// find the current DialogCallTreeThreadId in the array
		for( unsigned int index = 0; index < CaptureCallTreeThreadArraySize; index++ )
		{
			DialogThreadIdRecord_t* ThreadRec = (DialogThreadIdRecord_t*)CaptureCallTreeThreadArrayPointer[index];

			if( ThreadRec->ThreadId == DialogCallTreeThreadId )
			{
				DialogListViewThreadIndex = index;
				break;
			}
		}

		if( DialogListViewThreadIndex == -1 )
		{
			extern bool bDisableThreadIdCancelButton;
			bDisableThreadIdCancelButton = true;

			// thread id was not found, force the user to pick a different thread
			PostMessage(ghDialogWnd, WM_COMMAND, MAKEWPARAM(IDM_THREADID,0), 0);

			return;
		}

		DialogThreadIdRecord_t* ListView_ThreadIdRecord = (DialogThreadIdRecord_t*)CaptureCallTreeThreadArrayPointer[DialogListViewThreadIndex];
		assert(ListView_ThreadIdRecord);

		size_t len = min(wSymbolName_len, strlen(ListView_ThreadIdRecord->SymbolName));
		size_t num_chars;
		mbstowcs_s(&num_chars, wSymbolName, wSymbolName_len, ListView_ThreadIdRecord->SymbolName, len);
		wSymbolName[num_chars] = 0;

		extern TCHAR szTitle[MAX_LOADSTRING];

		swprintf(buffer, buffer_len, TEXT("%s - %s (ThreadId = %d)"), szTitle, wSymbolName, ListView_ThreadIdRecord->ThreadId);

		SetWindowText(ghWnd, buffer);

		// sort the newly collected data by whatever sort criteria is currently set for the ListView...
		hChildWindowCurrentlySorting = hChildWindowFunctions;

		gDisplayCallTreeData_ListView_ThreadIdRecordForQSort = ListView_ThreadIdRecord;

		extern ePleaseWaitType PleaseWaitType;
		PleaseWaitType = PleaseWait_DisplayCallTreeDataSort;

		extern HINSTANCE hInst;
		DialogBox(hInst, MAKEINTRESOURCE(IDD_PLEASEWAIT), ghWnd, PleaseWaitModalDialog);
	}
	else  // otherwise there is no CallTree data to display
	{
		ListView_SetItemCount(hChildWindowFunctions, 0);
		ListView_SetItemCount(hChildWindowParentFunctions, 0);
		ListView_SetItemCount(hChildWindowChildrenFunctions, 0);

		InvalidateRect(hChildWindowFunctions, NULL, FALSE);
		InvalidateRect(hChildWindowParentFunctions, NULL, FALSE);
		InvalidateRect(hChildWindowChildrenFunctions, NULL, FALSE);

		int text_length = GetWindowTextLength(hChildWindowTextViewer);
		SendMessage(hChildWindowTextViewer, EM_SETSEL, 0, text_length);

		SendMessage(hChildWindowTextViewer, EM_SETLIMITTEXT, 1, 0);

		SendMessage(hChildWindowTextViewer, EM_REPLACESEL, 0, (LPARAM)TEXT(""));

		SendMessage(hChildWindowTextViewer, EM_SETSEL, 0, 0);
		SendMessage(hChildWindowTextViewer, EM_SCROLLCARET, 0, 0);

		// ...and select the topmost item by default (for the middle child window)
		ListViewSetRowSelected(hChildWindowFunctions, 0, nullptr, false);

		ListViewSetFocus(hChildWindowFunctions);
	}
}

void LoadModules()
{
	const int MAX_MOD_HANDLES = 1024;
	HMODULE StaticModuleHandleArray[MAX_MOD_HANDLES];
	HMODULE* ModuleHandleArray;
	DWORD Needed;

	HANDLE hProcess = GetCurrentProcess();
	ModuleHandleArray = &StaticModuleHandleArray[0];

	BOOL result = EnumProcessModules(hProcess, ModuleHandleArray, sizeof(ModuleHandleArray), &Needed);

	if( !result )
	{
		DWORD error = GetLastError();
		DebugLog("EnumProcessModule failed: error = %d", error);
		return;
	}

	if( Needed > sizeof(ModuleHandleArray) )  // was our static array not big enough?
	{
		ModuleHandleArray = (HMODULE*)DialogAllocator.AllocateBytes(Needed, sizeof(void*));
		BOOL result = EnumProcessModules(hProcess, ModuleHandleArray, Needed, &Needed);

		if( !result )
		{
			DWORD error = GetLastError();
			DebugLog("EnumProcessModule(2) failed: error = %d", error);
			return;
		}
	}

	int NumModules = Needed / sizeof(HMODULE);

	MODULEINFO ModuleInfo;
	char ModuleFilePath[MAX_PATH];
	char ModuleName[256];
	char SearchFilePath[MAX_PATH];

	for( int i = 0; i < NumModules; i++ )
	{
		GetModuleInformation(hProcess, ModuleHandleArray[i], &ModuleInfo, sizeof(MODULEINFO));
		GetModuleFileNameExA(hProcess, ModuleHandleArray[i], ModuleFilePath, MAX_PATH);
		GetModuleBaseNameA(hProcess, ModuleHandleArray[i], ModuleName, 256);

		char* FileName = nullptr;
		GetFullPathNameA(ModuleFilePath, MAX_PATH, SearchFilePath, &FileName);
		*FileName = 0;

		SymSetSearchPath(hApplicationProcess, SearchFilePath);

		DWORD64 BaseAddress = SymLoadModule64(hApplicationProcess, ModuleHandleArray[i], ModuleFilePath, ModuleName, (DWORD64)ModuleInfo.lpBaseOfDll, (DWORD) ModuleInfo.SizeOfImage);
		if( !BaseAddress )
		{
			DWORD error = GetLastError();
			DebugLog("SymLoadModule64 failed: error = %d", error);
		}
	}
}

void InitializeSymbolLookup()
{
	static bool bIsInitialized = false;

	if( bIsInitialized )
	{
		return;
	}

	bIsInitialized = true;

	hApplicationProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, ApplicationProcessId);
	if( hApplicationProcess == NULL )
	{
		DWORD error = GetLastError();
		DebugLog("OpenProcess failed: error = %d", error);
		return;
	}

	DWORD SymOpts = 0;

	SymOpts |= SYMOPT_ALLOW_ABSOLUTE_SYMBOLS;
//	SymOpts |= SYMOPT_CASE_INSENSITIVE;
	SymOpts |= SYMOPT_DEBUG;
	SymOpts |= SYMOPT_DEFERRED_LOADS;
	SymOpts |= SYMOPT_EXACT_SYMBOLS;
	SymOpts |= SYMOPT_FAIL_CRITICAL_ERRORS;
	SymOpts |= SYMOPT_LOAD_LINES;
	SymOpts |= SYMOPT_UNDNAME;

	SymSetOptions(SymOpts);

	if( !SymInitialize(hApplicationProcess, NULL, TRUE) )  // defer the loading of process modules
	{
		DWORD error = GetLastError();
		DebugLog("SymInitialize failed: error = %d", error);
		return;
	}

	LoadModules();

	DWORD FileSize = 0;
	DWORD64 dwBaseAddress = 0;

	DWORD64 result = SymLoadModuleExW(hApplicationProcess, NULL, app_filename, NULL, dwBaseAddress, FileSize, NULL, 0);

	if( result == 0 )
	{
		DWORD error = GetLastError();
		DebugLog("SymLoadModuleEx failed: error = %d", error);

		SymUnloadModule64(hApplicationProcess, dwBaseAddress);

		return;
	}
}

char SymbolBuffer[sizeof(IMAGEHLP_SYMBOL64) + MAX_SYM_NAME];

char* LookupAddressSymbolName(DWORD64 dw64Address)
{
	static bool bHasFailed = false;  // for debugging purposes (so we only output the first time symbol lookup fails)

	if( dw64Address == 0 )
	{
		return nullptr;
	}

	PIMAGEHLP_SYMBOL64 pSymbol;
	pSymbol = (PIMAGEHLP_SYMBOL64)SymbolBuffer;
	pSymbol->SizeOfStruct = sizeof(SymbolBuffer);
	pSymbol->MaxNameLength = MAX_SYM_NAME;

	DWORD64 SymbolDisplacement64 = 0;

	//NOTE: Symbols that contain "dynamic initializer for" are for static initializers and can be REALLY slow to load

	if( SymGetSymFromAddr64(hApplicationProcess, dw64Address, &SymbolDisplacement64, pSymbol) )
	{
		char* p = pSymbol->Name;
		while( (*p < 32) || (*p > 127) )  // skip any strange characters at the beginning of the symbol name
		{
			p++;
		}

		return p;
	}
	else if( !bHasFailed )  // only output error message once (to prevent massive spam)
	{
		bHasFailed = true;

		DWORD error = GetLastError();
		DebugLog("SymGetSymFromAddr64 failed: error = %d", error);
	}

	return nullptr;
}

bool GetSourceCodeLineFromAddress(DWORD64 dw64Address, int& LineNumber, char* FileName, int FileNameSize)
{
	static bool bHasFailed = false;  // for debugging purposes (so we only output the first time symbol lookup fails)

	*FileName = 0;

	if( dw64Address == 0 )
	{
		return false;
	}

	PIMAGEHLP_SYMBOL64 pSymbol;
	pSymbol = (PIMAGEHLP_SYMBOL64)SymbolBuffer;
	pSymbol->SizeOfStruct = sizeof(SymbolBuffer);
	pSymbol->MaxNameLength = MAX_SYM_NAME;

	DWORD SymbolDisplacement = 0;

	IMAGEHLP_LINE64 Line;
	memset(&Line, 0, sizeof(Line));

	Line.SizeOfStruct = sizeof(Line);

	DWORD error;

	if( SymGetLineFromAddr64(hApplicationProcess, dw64Address, &SymbolDisplacement, &Line) )
	{
		LineNumber = Line.LineNumber;
		strcpy_s(FileName, FileNameSize, Line.FileName);

		return true;
	}
	else if( !bHasFailed )  // only output error message once (to prevent massive spam)
	{
		bHasFailed = true;

		error = GetLastError();
		DebugLog("SymGetLineFromAddr64 failed: error = %d", error);
	}

	return false;
}
