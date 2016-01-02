
#include "windows.h"
#include "Allocator.h"
#include "TextViewer.h"

CAllocator TextViewerAllocator;
TextLineBuffer line_buffer = { nullptr, 0, 0, 0 };

char* TextViewer_FileBuffer = nullptr;
char TextViewerFileName[MAX_PATH] = {""};  // the most recent file loaded into the text viewer


void InitializeTextLineBuffer(char* buffer, int length)
{
	char* p = buffer;
	int char_count = 0;
	int line_count = 0;
	TextLineNode* first_node = nullptr;
	TextLineNode* previous_node = nullptr;

	int line_length = 0;
	int max_line_length = 0;  // we need to keep track of the length of the longest line so we know how wide to make the horizontal scroll bar

	while( p && (char_count < length) )
	{
		TextLineNode* node = (TextLineNode*)TextViewerAllocator.AllocateBytes(sizeof(TextLineNode), sizeof(void*));

		node->text = p;  // point to the start of the line of text
		node->next = nullptr;

		if( first_node == nullptr )
		{
			first_node = node;
		}

		while( p && (*p != '\n') )
		{
			char_count++;

			if( *p == '\r' )
			{
				*p = 0;  // replace carriage return with null terminator for the text line
			}
			else
			{
				line_length++;  // we don't include carriage return characters as part of the line length
				if( *p == '\t')
				{
					while( line_length % 4 )
					{
						line_length++;
					}
				}
			}

			p++;

			if( char_count == length )
			{
				break;
			}
		}

		if( *p == '\n' )
		{
			*p = 0;  // replace newline with null terminator for the text line
			char_count++;
			p++;
		}

		if( line_length > max_line_length )
		{
			max_line_length = line_length;
		}

		if( previous_node )
		{
			previous_node->next = node;
		}

		previous_node = node;
		line_count++;
		line_length = 0;
	}

	line_buffer.num_lines = line_count;
	line_buffer.max_line_length = max_line_length;

	line_buffer.linenode = (TextLineNode**)TextViewerAllocator.AllocateBytes(line_count * sizeof(TextLineNode*), sizeof(void*));

	TextLineNode* line_node = first_node;
	for( int i = 0; i < line_buffer.num_lines; i++ )
	{
		line_buffer.linenode[i] = line_node;
		line_node = line_node->next;
	}
}

void LoadTextFile(char* filename)
{
	DWORD err;

	TextViewerAllocator.FreeBlocks();  // free all the memory allocated by the TextViewerAllocator

	strncpy_s(TextViewerFileName, filename, MAX_PATH);

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

		InitializeTextLineBuffer(TextViewer_FileBuffer, length);
	}
}
