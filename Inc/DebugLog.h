
#pragma once

#include <string>
#include <fstream>
#include <iostream>
#include <Windows.h>

using namespace std;

#define USE_DEBUGLOG 0  // change this to 1 if you want to enable the debug logging

class CDebugLog
{
private:
	char m_filename[MAX_PATH];
	bool bIsDebugLogOpen;
	ofstream DebugLogOutStream;

public:

	CDebugLog(const char* filename)
	{
		bIsDebugLogOpen = false;
		strncpy_s(m_filename, MAX_PATH, filename, strlen(filename));
	}

	~CDebugLog(void)
	{
		CloseDebugLog();
	}
	
	void OpenDebugLog()
	{
		if(!bIsDebugLogOpen)
		{
			DebugLogOutStream.open(m_filename, std::ios_base::app);
			bIsDebugLogOpen = true;
		}
	}

	void CloseDebugLog()
	{
		if(bIsDebugLogOpen)
		{
			DebugLogOutStream.close();
			bIsDebugLogOpen = false;
		}
	}

	void Log(char* msg)
	{
		static char Log_Buffer[4096];

		char* out_buff = Log_Buffer;  // use the global Log_Buffer by default
		size_t out_buff_len = sizeof(Log_Buffer);
		bool bFreeBuffer = false;

		size_t msg_len = strlen(msg);
		if( msg_len >= sizeof(Log_Buffer) )  // if the input msg is too big, allocate custom memory for it
		{
			out_buff = (char*)malloc(msg_len + 64);
			out_buff_len = msg_len + 64;
			bFreeBuffer = true;
		}

		SYSTEMTIME SystemTime;

		GetLocalTime(&SystemTime);

		sprintf_s(out_buff, out_buff_len, "[%02d-%02d-%04d %02d:%02d:%02d] - %s", SystemTime.wMonth, SystemTime.wDay, SystemTime.wYear, SystemTime.wHour, SystemTime.wMinute, SystemTime.wSecond, msg);

		OpenDebugLog();

		DebugLogOutStream << out_buff  << endl;
		DebugLogOutStream << flush;

		if( bFreeBuffer )
		{
			free(out_buff);
		}
	}

	void Log(const char* format, ... )
	{
		static char Log_Buffer[4096];

		va_list args;
		va_start(args, format);
		vsnprintf_s(Log_Buffer, sizeof(Log_Buffer), sizeof(Log_Buffer)-1, format, args);
		Log(Log_Buffer);
		va_end (args);
	}
};

extern CDebugLog* gDebugLog;

#if USE_DEBUGLOG
#define DebugLog(msg, ...) if(gDebugLog) gDebugLog->Log(msg, ##__VA_ARGS__)
#else
#define DebugLog(msg, ...)
#endif
