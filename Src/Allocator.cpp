
#include <Windows.h>
#include <assert.h>

#include "DebugLog.h"

#include "Allocator.h"

const int DEFAULT_PAGESIZE = 65536;

CAllocator::CAllocator(bool bInNeedsToBeThreadSafe) :
	bNeedsToBeThreadSafe(bInNeedsToBeThreadSafe)
{
	if( bNeedsToBeThreadSafe )
	{
		InitializeCriticalSection(&AllocatorCriticalSection);
		SetCriticalSectionSpinCount(&AllocatorCriticalSection, 4000);  // 4000 is what the Windows heap manager uses (https://msdn.microsoft.com/en-us/library/windows/desktop/ms686197%28v=vs.85%29.aspx)
	}

	FirstBlock = nullptr;
	CurrentBlock = nullptr;
}

CAllocator::~CAllocator()
{
	if( FirstBlock )
	{
		FreeBlocks();
	}

	if( bNeedsToBeThreadSafe )
	{
		DeleteCriticalSection(&AllocatorCriticalSection);
	}
}

void CAllocator::FreeBlocks()
{
	// free all the blocks...
	void* Ptr = FirstBlock;
	while( Ptr )
	{
		void* NextPtr = ((AllocHeader*)Ptr)->NextBlock;

		VirtualFree(Ptr, ((AllocHeader*)Ptr)->Size, MEM_RELEASE);

		Ptr = NextPtr;
	}

	FirstBlock = nullptr;
	CurrentBlock = nullptr;
}

void CAllocator::GetAllocationStats(size_t& TotalSize, size_t& FreeSize)
{
	size_t total_size = 0;
	size_t free_remaining = 0;

	void* Ptr = FirstBlock;
	while( Ptr )
	{
		total_size += ((AllocHeader*)Ptr)->Size;
		free_remaining += ((AllocHeader*)Ptr)->FreeRemaining;
		Ptr = ((AllocHeader*)Ptr)->NextBlock;
	}

	TotalSize = total_size;
	FreeSize = free_remaining;
}

void CAllocator::PrintStats(char* Header, int NestLevel)
{
	char buffer[64];
	buffer[0] = 0;

	for( int i = 0; (i < NestLevel) && (i < 32); i++ )
	{
		strcat_s(buffer, sizeof(buffer), "  ");
	}

	int number_blocks = 0;
	size_t total_size = 0;
	size_t free_remaining = 0;

	void* Ptr = FirstBlock;
	while( Ptr )
	{
		number_blocks++;
		total_size += ((AllocHeader*)Ptr)->Size;
		free_remaining += ((AllocHeader*)Ptr)->FreeRemaining;
		Ptr = ((AllocHeader*)Ptr)->NextBlock;
	}
	DebugLog("%s%sCAllocator stats: number of blocks = %d, free remaining = %d, total size = %d, total used = %d", buffer, Header, number_blocks, (int)free_remaining, (int)total_size, (int)(total_size - free_remaining));
}

void* CAllocator::AllocateBytes(size_t NumBytes, int Alignment)
{
	if( NumBytes <= 0 )
	{
		return nullptr;
	}

	if( bNeedsToBeThreadSafe )
	{
		EnterCriticalSection(&AllocatorCriticalSection);
	}

	if( CurrentBlock == nullptr )
	{
		size_t page_size = max(NumBytes + sizeof(AllocHeader) + Alignment, DEFAULT_PAGESIZE);
		page_size = ((page_size + DEFAULT_PAGESIZE - 1) / DEFAULT_PAGESIZE) * DEFAULT_PAGESIZE;

		// allocate first block
		char* Ptr = (char*)VirtualAlloc( NULL, page_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );

		assert(Ptr);

		if( Ptr == NULL )
		{
			DWORD error = GetLastError();
			DebugLog("VirtualAlloc failed: error = %d", error);

			if( bNeedsToBeThreadSafe )
			{
				LeaveCriticalSection(&AllocatorCriticalSection);
			}

			return nullptr;
		}

		FirstBlock = Ptr;
		CurrentBlock = Ptr;

		AllocHeader* FirstBlockHeader = (AllocHeader*)CurrentBlock;
		FirstBlockHeader->NextBlock = nullptr;
		FirstBlockHeader->FreePointer = (char*)FirstBlockHeader + sizeof(AllocHeader);
		FirstBlockHeader->Size = page_size;
		FirstBlockHeader->FreeRemaining = page_size - sizeof(AllocHeader);
	}

	AllocHeader* Header = (AllocHeader*)CurrentBlock;

	char* pAlignedFreePointer = (char*)(((uintptr_t)(Header->FreePointer + Alignment - 1) / Alignment) * Alignment);
	size_t AlignedOffset = pAlignedFreePointer - Header->FreePointer;
	size_t AlignedFreeRemaining = Header->FreeRemaining - AlignedOffset;

	if( NumBytes >= AlignedFreeRemaining )  // not enough free space for aligned allocation?
	{
		size_t page_size = max(NumBytes + sizeof(AllocHeader) + Alignment, DEFAULT_PAGESIZE);
		page_size = ((page_size + DEFAULT_PAGESIZE - 1) / DEFAULT_PAGESIZE) * DEFAULT_PAGESIZE;

		// allocate next block and link it in
		char* Ptr = (char*)VirtualAlloc( NULL, page_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );

		assert(Ptr);

		if( Ptr == NULL )
		{
			DWORD error = GetLastError();
			DebugLog("VirtualAlloc failed: error = %d", error);

			if( bNeedsToBeThreadSafe )
			{
				LeaveCriticalSection(&AllocatorCriticalSection);
			}

			return nullptr;
		}

		Header->NextBlock = Ptr;

		CurrentBlock = Ptr;

		Header = (AllocHeader*)CurrentBlock;
		Header->NextBlock = nullptr;
		Header->FreePointer = (char*)Header + sizeof(AllocHeader);
		Header->Size = page_size;
		Header->FreeRemaining = page_size - sizeof(AllocHeader);

		pAlignedFreePointer = (char*)(((uintptr_t)(Header->FreePointer + Alignment - 1) / Alignment) * Alignment);
		AlignedOffset = pAlignedFreePointer - Header->FreePointer;
		AlignedFreeRemaining = Header->FreeRemaining - AlignedOffset;
	}

	Header->FreePointer = pAlignedFreePointer + NumBytes;
	Header->FreeRemaining = AlignedFreeRemaining - NumBytes;

	if( bNeedsToBeThreadSafe )
	{
		LeaveCriticalSection(&AllocatorCriticalSection);
	}

	return (void*)pAlignedFreePointer;
}
