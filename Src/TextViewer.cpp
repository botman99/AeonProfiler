
#include "windows.h"

#include "Allocator.h"
#include "TextViewer.h"

CAllocator TextViewerAllocator(false);

char TextViewerFileName[MAX_PATH] = {""};  // the most recent file loaded into the text viewer

TCHAR* TextViewerBuffer = nullptr;  // the unicode text buffer to display in the source code text window
int TextViewBuffer_TotalSize = 0;


int GetSizeOfTextBufferInUnicode(char* buffer, int buffer_length)
{
	int total_size = 0;
	int count = 0;

	const char* p = buffer;

	while( *p && count < buffer_length )
	{
		if( *p == '\n' )
		{
			total_size += 4;  // this will get converted to carriage return and line feed
		}
		else if( *p != '\r' )
		{
			total_size += 2;  // 2 bytes per unicode character
		}

		count++;
		p++;
	}

	return total_size;
}

void ConvertTextFileBufferToUnicode(char* buffer, int buffer_length, TCHAR* tchar_buffer)
{
	int count = 0;

	const char* p = buffer;
	TCHAR* tchar_p = tchar_buffer;

	while( *p && count < buffer_length )
	{
		if( *p == '\n' )
		{
			*tchar_p++ = TCHAR('\r');
			*tchar_p++ = TCHAR('\n');
		}
		else if( *p != '\r' )
		{
			*tchar_p++ = TCHAR(*p);
		}

		count++;
		p++;
	}
}

void LoadTextFile(char* filename)
{
	DWORD err;

	if( (filename == nullptr) || (*filename == 0) )
	{
		return;
	}

	TextViewerAllocator.FreeBlocks();  // free all the memory allocated by the TextViewerAllocator

	strncpy_s(TextViewerFileName, filename, MAX_PATH);

	char* TextViewer_FileBuffer = nullptr;

	size_t wNumChars = 0;
	WCHAR wFilename[MAX_PATH];
	mbstowcs_s(&wNumChars, wFilename, MAX_PATH, filename, strlen(filename));
	HANDLE hFile = CreateFile(wFilename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	if( hFile != INVALID_HANDLE_VALUE )
	{
		DWORD length;
		if( (length = GetFileSize(hFile, 0)) == INVALID_FILE_SIZE )
		{
			err = GetLastError();
			return;
		}
		length++;  // add one for additional null terminator

		TextViewer_FileBuffer = (char*)TextViewerAllocator.AllocateBytes(length, sizeof(void*));

		DWORD read_count = 0;
		ReadFile(hFile, TextViewer_FileBuffer, length, &read_count, 0);

		TextViewer_FileBuffer[length-1] = 0;  // make sure buffer is null terminated

		TextViewBuffer_TotalSize = GetSizeOfTextBufferInUnicode(TextViewer_FileBuffer, length) + 2;  // plus 2 for null at the end

		TextViewerBuffer = (TCHAR*)TextViewerAllocator.AllocateBytes(TextViewBuffer_TotalSize, sizeof(void*));

		ConvertTextFileBufferToUnicode(TextViewer_FileBuffer, length, TextViewerBuffer);

		CloseHandle(hFile);
	}
}
