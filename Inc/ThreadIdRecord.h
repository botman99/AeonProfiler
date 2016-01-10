
#pragma once

// C RunTime Header Files
#include <assert.h>
#include <new>

#include "Allocator.h"
#include "Stack.h"
#include "Hash.h"

extern int NumThreads;

struct DialogThreadIdRecord_t  // "static" copy of CThreadIdRecord for the Dialog to display data that's not constantly changing
{
	CThreadIdRecord* ThreadIdRecord;  // pointer to original CThreadIdRecord record (so we can set its SymbolName)

	DialogStackCallerData_t* StackArray;  // array of StackCallerData_t structs
	unsigned int StackArraySize;

	void** CallTreeArray;  // array of pointers
	unsigned int CallTreeArraySize;

	const void* Address;

	DWORD ThreadId;
	char* SymbolName;
};

class CThreadIdRecord
{
public:
	CAllocator ThreadIdRecordAllocator;  // allocator for this specific thread

	CStack CallStack;  // the current call stack for this thread
	CHash<CCallTreeRecord> CallTreeHashTable;

	DWORD ThreadId;
	char* SymbolName;

	CThreadIdRecord(DWORD InThreadId, CAllocator& InAllocator) :
		ThreadIdRecordAllocator(),
		CallStack(&ThreadIdRecordAllocator),
		CallTreeHashTable(&ThreadIdRecordAllocator, CALLRECORD_HASH_TABLE_SIZE),
		ThreadId( InThreadId )
	{
		NumThreads++;
	}

	~CThreadIdRecord()
	{
		ThreadId = 0;
		SymbolName = NULL;

		ThreadIdRecordAllocator.FreeBlocks();  // free all the memory allocated by this thread's allocator

		NumThreads--;
	}

	void PrintStats(char* Header, int NestLevel)
	{
		char buffer[64];
		buffer[0] = 0;

		for( int i = 0; (i < NestLevel) && (i < 32); i++ )
		{
			strcat_s(buffer, sizeof(buffer), "  ");
		}

		DebugLog("%s%sCThreadIdRecord Stats: ThreadId = %d (0x%08x), SymbolName = '%s'", buffer, Header, ThreadId, ThreadId, SymbolName ? SymbolName : "(Unknown)" );

		ThreadIdRecordAllocator.PrintStats("ThreadIdRecordAllocator - ", NestLevel + 1);
		CallTreeHashTable.PrintStats("CallTreeHashTable - ", NestLevel + 1);
	}

	unsigned int GetNumRecordsToCopy()
	{
		return 1;
	}

	DialogThreadIdRecord_t* GetArrayCopy(CAllocator* InCopyAllocator, bool bCopyMemberHashTables)
	{
		DialogThreadIdRecord_t* pRec = (DialogThreadIdRecord_t*)InCopyAllocator->AllocateBytes(sizeof(DialogThreadIdRecord_t), sizeof(void*));

		pRec->ThreadIdRecord = this;

		pRec->StackArray = nullptr;
		pRec->StackArraySize = 0;

		pRec->CallTreeArray = nullptr;
		pRec->CallTreeArraySize = 0;

		pRec->ThreadId = ThreadId;
		pRec->SymbolName = SymbolName;

		if( CallStack.pTop )  // copy the thread's Stack
		{
			pRec->StackArray = CallStack.CopyStackToArray(InCopyAllocator, pRec->StackArraySize);

			pRec->Address = pRec->StackArray[0].CallerAddress;
		}
		else
		{
			pRec->Address = nullptr;
		}

		pRec->CallTreeArray = CallTreeHashTable.CopyHashToArray(InCopyAllocator, pRec->CallTreeArraySize, true);

		return pRec;
	}

	void ResetCounters(DWORD64 TimeNow)
	{
		// reset the calltree records in the hash table first (the calltree records on the stack need special handling){
		CallTreeHashTable.ResetCounters(TimeNow);

		// reset the calltree records on the stack last (to set the proper CallCount and MaxRecursionLevel)
		CallStack.ResetCounters(TimeNow);
	}

	void SetSymbolName(char* InSymbolName)
	{
		SymbolName = InSymbolName;
	}

private:
	CThreadIdRecord(const CThreadIdRecord& other, CAllocator* InThreadIdRecordAllocator = nullptr);
	CThreadIdRecord& operator=(const CThreadIdRecord&);
};
