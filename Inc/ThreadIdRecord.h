
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
	CAllocator* ThreadIdRecordAllocator;  // allocator for this specific thread

	CStack* CallStack;  // the current call stack for this thread
	CHash<CCallTreeRecord>* CallTreeHashTable;

	DWORD ThreadId;
	char* SymbolName;

	CThreadIdRecord(DWORD InThreadId, CAllocator& InAllocator) :
		ThreadId( InThreadId )
	{
		CallStack = nullptr;
		CallTreeHashTable = nullptr;

		// create an allocator specifically for this thread
		ThreadIdRecordAllocator = (CAllocator*)InAllocator.AllocateBytes(sizeof(CAllocator), sizeof(void*));

		if( ThreadIdRecordAllocator )
		{
			CallStack = (CStack*)ThreadIdRecordAllocator->AllocateBytes(sizeof(CStack), sizeof(void*));
			new(CallStack) CStack(ThreadIdRecordAllocator);

			CallTreeHashTable = (CHash<CCallTreeRecord>*)ThreadIdRecordAllocator->AllocateBytes(sizeof(CHash<CCallTreeRecord>), sizeof(void*));
			new(CallTreeHashTable) CHash<CCallTreeRecord>(ThreadIdRecordAllocator, CALLRECORD_HASH_TABLE_SIZE);
		}

		NumThreads++;
	}

	~CThreadIdRecord()
	{
		ThreadId = 0;
		SymbolName = NULL;

		if( ThreadIdRecordAllocator )
		{
			ThreadIdRecordAllocator->FreeBlocks();  // free all the memory allocated by this thread's allocator
		}

		ThreadIdRecordAllocator = nullptr;

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

		if( ThreadIdRecordAllocator )
		{
			ThreadIdRecordAllocator->PrintStats("ThreadIdRecordAllocator - ", NestLevel + 1);
		}

		if( CallTreeHashTable )
		{
			CallTreeHashTable->PrintStats("CallTreeHashTable - ", NestLevel + 1);
		}
	}

	unsigned int GetNumRecordsToCopy()
	{
		return 1;
	}

	void* GetArrayCopy(CAllocator* InCopyAllocator, bool bCopyMemberHashTables)
	{
		DialogThreadIdRecord_t* pRec = (DialogThreadIdRecord_t*)InCopyAllocator->AllocateBytes(sizeof(DialogThreadIdRecord_t), sizeof(void*));

		pRec->ThreadIdRecord = this;

		pRec->StackArray = nullptr;
		pRec->StackArraySize = 0;

		pRec->CallTreeArray = nullptr;
		pRec->CallTreeArraySize = 0;

		pRec->ThreadId = ThreadId;
		pRec->SymbolName = SymbolName;

		if( CallStack && CallStack->pBottom )  // copy the thread's Stack
		{
			pRec->StackArray = CallStack->CopyStackToArray(InCopyAllocator, pRec->StackArraySize);

			StackCallerData_t& StackBottomCallerData = CallStack->pBottom->value;
			pRec->Address = StackBottomCallerData.CallerAddress;
		}
		else
		{
			pRec->Address = nullptr;
		}

		if( CallTreeHashTable )  // copy the thread's CallTreeHashTable
		{
			pRec->CallTreeArray = CallTreeHashTable->CopyHashToArray(InCopyAllocator, pRec->CallTreeArraySize, true);
		}

		return (void*)pRec;
	}

	void ResetCounters(DWORD64 TimeNow)
	{
		// reset the calltree records in the hash table first (the calltree records on the stack need special handling)
		if( CallTreeHashTable )
		{
			CallTreeHashTable->ResetCounters(TimeNow);
		}

		// reset the calltree records on the stack last (to set the proper CallCount and MaxRecursionLevel)
		if( CallStack )
		{
			CallStack->ResetCounters(TimeNow);
		}
	}

	void SetSymbolName(char* InSymbolName)
	{
		SymbolName = InSymbolName;
	}

private:
	CThreadIdRecord(const CThreadIdRecord& other, CAllocator* InThreadIdRecordAllocator = nullptr)  // copy constructor (this should never get called)
	{
		assert(false);
	}

	CThreadIdRecord& operator=(const CThreadIdRecord&)  // assignment operator (this should never get called)
	{
		assert(false);
	}
};
