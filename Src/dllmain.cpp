
#include <Windows.h>

#include "DebugLog.h"

#include "CallerData.h"
#include "Allocator.h"

extern CAllocator GlobalAllocator;
extern DWORD DialogCallTreeThreadId;  // the thread id of the thread to display CallTree data for in the ListView dialog

bool bTrackCallerData = false;

TCHAR app_filename[MAX_PATH];  // filename of the application that loaded the DLL

HMODULE ModuleHandle;

DWORD ApplicationProcessId = 0;
DWORD ApplicationThreadId = 0;

__int64 ClockFreq;  // the frequency of this computers CPU clock
__int64 ClockDivisor = 10000000;  // to convert from clock ticks to 100 nanosecond increments
int TicksPerHundredNanoseconds = 0;  // how many CPU ticks happen in 100 nanoseconds (divide ticks by this and then divide by 10,000 to get milliseconds)

HANDLE DialogThreadHandle = NULL;
DWORD DialogThreadID;

CDebugLog* gDebugLog = NULL;

int* AeonWinExitPointer = nullptr;


void WINAPI DialogThread(LPVOID lpData);

void HandleExit();

void CallerEnter(CallerData_t& Call);
void CallerExit(CallerData_t& Call);

void ProfilerEnter(__int64 InCounter, void* InCallerAddress)
{
	CallerData_t Call;

	Call.ThreadId = GetCurrentThreadId();

	Call.Counter = InCounter;
	Call.CallerAddress = InCallerAddress;

	if( bTrackCallerData )
	{
		CallerEnter(Call);
	}
}

void ProfilerExit(__int64 InCounter, void* InCallerAddress)
{
	CallerData_t Call;

	Call.ThreadId = GetCurrentThreadId();

	Call.Counter = InCounter;
	Call.CallerAddress = InCallerAddress;

	if( bTrackCallerData )
	{
		CallerExit(Call);
	}
}


BOOL APIENTRY DllMain( HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved )
{
	switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:

#if _M_X64
			gDebugLog = GlobalAllocator.New<CDebugLog>("AeonProfiler64.log");
#else
			gDebugLog = GlobalAllocator.New<CDebugLog>("AeonProfiler32.log");
#endif

			DebugLog("***** DLL_PROCESS_ATTACH *****");

			ModuleHandle = hModule;
			ApplicationProcessId = GetCurrentProcessId();
			ApplicationThreadId = GetCurrentThreadId();

			DWORD64 Counter_Freq, Counter_Before, Counter_After;
			DWORD64 RDTSC_Before, RDTSC_After;

			QueryPerformanceFrequency((LARGE_INTEGER*)&Counter_Freq);
			QueryPerformanceCounter((LARGE_INTEGER*)&Counter_Before);
			RDTSC_Before = __rdtsc();
			Sleep(1000);
			RDTSC_After = __rdtsc();
			QueryPerformanceCounter((LARGE_INTEGER*)&Counter_After);

			ClockFreq = Counter_Freq * (RDTSC_After - RDTSC_Before) / (Counter_After - Counter_Before);
			TicksPerHundredNanoseconds = (int)(ClockFreq / ClockDivisor);

			DialogCallTreeThreadId = ApplicationThreadId;  // get the current thread id (this should be "main()" or "WinMain()")

			// Get the process name that's loading us
			GetModuleFileName( GetModuleHandle( NULL ), app_filename, _countof(app_filename) );

			DebugLog("Application: %s", app_filename);

			DialogThreadHandle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)DialogThread, NULL, 0, &DialogThreadID);

			bTrackCallerData = true;

			break;

		case DLL_PROCESS_DETACH:

			bTrackCallerData = false;

			DebugLog("***** DLL_PROCESS_DETACH *****");

			HandleExit();

			if( DialogThreadHandle )
			{
				if( WaitForSingleObject(DialogThreadHandle, INFINITE) == WAIT_TIMEOUT )
				{
					TerminateThread(DialogThreadHandle, 0);
				}

				CloseHandle(DialogThreadHandle);
			}

			if( gDebugLog )
			{
				gDebugLog->~CDebugLog();
				gDebugLog = nullptr;
			}

			break;
	}

	return TRUE;
}

extern "C"
{
	__declspec(dllexport) void AeonWinCallbackFunc(int* InExitApplication)
	{
		AeonWinExitPointer = InExitApplication;

		if( AeonWinExitPointer )
		{
			*AeonWinExitPointer = 0;
		}
	}
}