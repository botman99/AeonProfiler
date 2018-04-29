
#include <stdio.h>
#include <assert.h>

#include "Repository.h"

Repository::Repository(WCHAR* InFilename, CAllocator* InAllocator)
	: hFile(NULL)
{
	assert(Allocator != nullptr);
	wcscpy_s(Filename, InFilename);
	Allocator = InAllocator;
}

Repository::~Repository()
{
	if( hFile != nullptr )
	{
		CloseHandle(hFile);
		hFile = NULL;
	}
}

bool Repository::OpenForReading()
{
	if( hFile != NULL )
	{
		CloseHandle(hFile);
		hFile = NULL;
	}

	bIsLoading = true;

	hFile = CreateFile(Filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);

	if( hFile == INVALID_HANDLE_VALUE )
	{
		ErrorCode = GetLastError();
		hFile = NULL;
		return false;
	}

	ErrorCode = 0;

	return true;
}

bool Repository::OpenForWriting()
{
	if( hFile != NULL )
	{
		CloseHandle(hFile);
		hFile = NULL;
	}

	bIsLoading = false;

	hFile = CreateFile(Filename, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);

	if( hFile == INVALID_HANDLE_VALUE )
	{
		ErrorCode = GetLastError();
		hFile = NULL;
		return false;
	}

	ErrorCode = 0;

	return true;
}

void Repository::Close()
{
	if( hFile != nullptr )
	{
		CloseHandle(hFile);
	}

	hFile = NULL;
}

Repository& Repository::Serialize( int size, void* value )
{
	assert(hFile != NULL);
	DWORD count = 0;

	if( ErrorCode != 0 )
	{
		return *this;
	}

	if( size == 0 )
	{
		return *this;
	}

	if( bIsLoading )
	{
		if( ReadFile(hFile, value, size, &count, 0) == 0 )
		{
			ErrorCode = GetLastError();
			return *this;
		}
	}
	else
	{
		if( WriteFile(hFile, value, size, &count, 0) == 0 )
		{
			ErrorCode = GetLastError();
			return *this;
		}
	}

	assert(count == size);
	if( count != size )
	{
		ErrorCode = LONG_MAX;
	}

	return *this;
}

Repository& Repository::operator<<( WORD& value )
{
	return Serialize(sizeof(value), &value);
}

Repository& Repository::operator<<( DWORD& value )
{
	return Serialize(sizeof(value), &value);
}

Repository& Repository::operator<<( DWORD64& value )
{
	return Serialize(sizeof(value), &value);
}

Repository& Repository::operator<<( char& value )
{
	return Serialize(sizeof(value), &value);
}

Repository& Repository::operator<<( int& value )
{
	return Serialize(sizeof(value), &value);
}

Repository& Repository::operator<<( unsigned int& value )
{
	return Serialize(sizeof(value), &value);
}

Repository& Repository::operator<<( float& value )
{
	return Serialize(sizeof(value), &value);
}

Repository& Repository::operator<<( __int64& value )
{
	return Serialize(sizeof(value), &value);
}

Repository& Repository::operator<<( char*& value )
{
	int length = 0;

	if( bIsLoading )
	{
		*this << length;
		if( length <= 0 )
		{
			value = nullptr;
		}
		else if( length > 4096 )
		{
			value = nullptr;
			ErrorCode = LONG_MAX;
		}
		else
		{
			value = (char*)Allocator->AllocateBytes(length+1, 1);  // plus one for null terminator

			Serialize(length, value);
			value[length] = 0;
		}
	}
	else
	{
		if( value != nullptr )
		{
			length = (int)strlen(value);
			if( length > 4096 )  // cap string length at 4k
			{
				length = 4096;
			}
		}

		*this << length;
		Serialize(length, value);
	}

	return *this;
}
