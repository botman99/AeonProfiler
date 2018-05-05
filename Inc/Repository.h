//
// Copyright (c) 2015-2018 Jeffrey "botman" Broome
//

#pragma once

#include <Windows.h>

#include "Allocator.h"

class Repository
{
public:
	Repository(WCHAR* InFilename, CAllocator*);
	~Repository();

	bool OpenForReading();
	bool OpenForWriting();
	void Close();

	Repository& Serialize( int size, void* value);

	Repository& operator<<( WORD& value );
	Repository& operator<<( DWORD& value );
	Repository& operator<<( DWORD64& value );
	Repository& operator<<( char& value );
	Repository& operator<<( int& value );
	Repository& operator<<( unsigned int& value );
	Repository& operator<<( float& value );
	Repository& operator<<( __int64& value );
	Repository& operator<<( char*& value );

	WCHAR Filename[MAX_PATH];
	CAllocator* Allocator;
	HANDLE hFile;
	DWORD ErrorCode;

	WORD Version;
	bool bIsLoading;
	bool bIsDebugSave;
};

#define HEADER_VERSION 1

struct Header_t
{
	char* Name = "AeonProfiler";
	WORD Version;
	int TicksPerHundredNanoseconds;
	WORD bIsDebugSave;

	friend Repository& operator<<( Repository& Repo, Header_t& Header )
	{
		if( !Repo.bIsLoading )
		{
			Header.Version = HEADER_VERSION;
			extern int TicksPerHundredNanoseconds;
			Header.TicksPerHundredNanoseconds = TicksPerHundredNanoseconds;
			Header.bIsDebugSave = 0;
#ifdef _DEBUG
			Header.bIsDebugSave = 1;
#endif
		}

		Repo << Header.Name;
		Repo << Header.Version;
		Repo << Header.TicksPerHundredNanoseconds;
		Repo << Header.bIsDebugSave;  // Debug save injects unique "signatures" into the records in the file that helps detect serialization errors

		Repo.Version = Header.Version;
		Repo.bIsDebugSave = (Header.bIsDebugSave) ? true : false;

		return Repo;
	}
};

