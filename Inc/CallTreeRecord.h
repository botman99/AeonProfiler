
#pragma once

// C RunTime Header Files
#include <assert.h>
#include <new>

#include "Allocator.h"
#include "Hash.h"

#define CALLRECORD_HASH_TABLE_SIZE 256  /* default size of hash table for all callrecords within a thread */
#define PARENT_CALLRECORD_HASH_TABLE_SIZE 8  /* default size of hash table for parents within a child callrecord */
#define CHILDREN_CALLRECORD_HASH_TABLE_SIZE 8  /* default size of hash table for children within a parent callrecord */

extern int NumCallTreeRecords;

// forward declarations
class CCallTreeRecord;
class CThreadIdRecord;


struct DialogCallTreeRecord_t  // "static" copy of CCallTreeRecord for the Dialog to display data that's not constantly changing
{
	CCallTreeRecord* CallTreeRecord;  // pointer to original CCallTreeRecord record (so we can set its SymbolName)

	void** ParentArray;  // array of pointers
	unsigned int ParentArraySize;
	void** ChildrenArray;  // array of pointers
	unsigned int ChildrenArraySize;

	const void* Address;

	DWORD64 EnterTime;  // the time the function called the Enter handler (this gets zeroed out on Exit, so we can tell which functions on the stack were entered but haven't exited yet)

	__int64 CallDurationInclusiveTimeSum;  // the sum of all durations calling this function and its children (so we can calc the average call time)
	__int64 CallDurationExclusiveTimeSum;  // the sum of all durations calling this function (minus the CallDurationInclusiveTimeSum of the children)
	__int64 MaxCallDurationExclusiveTime;  // this is the maximum CallDurationExclusiveTime for this function
	__int64 CurrentChildrenInclusiveTime;  // the parent keeps track of the total inclusive time of children (so it can subtract this from delta time to get exclusive time)

	int CallCount;
	int StackDepth;
	int MaxRecursionLevel;

	char* SymbolName;
};

class CCallTreeRecord
{
public:
	CHash<CCallTreeRecord>* ParentHashTable;		// parent functions that called this function
	CHash<CCallTreeRecord>* ChildrenHashTable;		// child functions that this function calls

	const void* Address;

	DWORD64 EnterTime;  // the time the function called the Enter handler (this gets zeroed out on Exit, so we can tell which functions on the stack were entered but haven't exited yet)

	__int64 CallDurationInclusiveTimeSum;  // the sum of all durations calling this function and its children (so we can calc the average call time)
	__int64 CallDurationExclusiveTimeSum;  // the sum of all durations calling this function (minus the CallDurationInclusiveTimeSum of the children)
	__int64 MaxCallDurationExclusiveTime;  // this is the maximum CallDurationExclusiveTime for this function
	__int64 CurrentChildrenInclusiveTime;  // the parent keeps track of the total inclusive time of children (so it can subtract this from delta time to get exclusive time)

	int CallCount;
	int StackDepth;
	int MaxRecursionLevel;

	char* SymbolName;

	CCallTreeRecord(const void* InAddress) :
		Address( InAddress )
		,SymbolName( nullptr )
		,CallCount( 0 )
		,CallDurationInclusiveTimeSum( 0 )
		,CallDurationExclusiveTimeSum( 0 )
		,MaxCallDurationExclusiveTime( 0 )
		,StackDepth( 0 )
		,MaxRecursionLevel( 0 )
		,CurrentChildrenInclusiveTime( 0 )
	{
		ParentHashTable = nullptr;
		ChildrenHashTable = nullptr;

		NumCallTreeRecords++;
	}

	~CCallTreeRecord()
	{
		Address = NULL;
		SymbolName = NULL;

		NumCallTreeRecords--;
	}

	void PrintStats(char* Header, int NestLevel)
	{
	}

	unsigned int GetNumRecordsToCopy()
	{
		if( CallCount )
		{
			return 1;
		}

		return 0;
	}

	void* GetArrayCopy(CAllocator* InCopyAllocator, bool bCopyMemberHashTables)
	{
		DialogCallTreeRecord_t* pRec = (DialogCallTreeRecord_t*)InCopyAllocator->AllocateBytes(sizeof(DialogCallTreeRecord_t), sizeof(void*));

		pRec->CallTreeRecord = this;

		pRec->ParentArray = nullptr;
		pRec->ParentArraySize = 0;
		pRec->ChildrenArray = nullptr;
		pRec->ChildrenArraySize = 0;

		if( bCopyMemberHashTables )
		{
			if( ParentHashTable )
			{
				// when copying the hash table elements, don't copy their hash table elements as well (otherwise this causes recursion hell)
				pRec->ParentArray = ParentHashTable->CopyHashToArray(InCopyAllocator, pRec->ParentArraySize, false);
			}
			if( ChildrenHashTable )
			{
				// when copying the hash table elements, don't copy their hash table elements as well (otherwise this causes recursion hell)
				pRec->ChildrenArray = ChildrenHashTable->CopyHashToArray(InCopyAllocator, pRec->ChildrenArraySize, false);
			}
		}

		pRec->Address = Address;

		pRec->EnterTime = EnterTime;

		pRec->CallDurationInclusiveTimeSum = CallDurationInclusiveTimeSum;
		pRec->CallDurationExclusiveTimeSum = CallDurationExclusiveTimeSum;
		pRec->MaxCallDurationExclusiveTime = MaxCallDurationExclusiveTime;
		pRec->CurrentChildrenInclusiveTime = CurrentChildrenInclusiveTime;

		pRec->CallCount = CallCount;
		pRec->StackDepth = StackDepth;
		pRec->MaxRecursionLevel = MaxRecursionLevel;

		pRec->SymbolName = SymbolName;

		return (void*)pRec;
	}

	void ResetCounters(DWORD64 TimeNow)
	{
		CallDurationInclusiveTimeSum = 0;
		CallDurationExclusiveTimeSum = 0;
		MaxCallDurationExclusiveTime = 0;
		CurrentChildrenInclusiveTime = 0;

		CallCount = 0;
		StackDepth = 0;
		MaxRecursionLevel = 0;
	}

	void SetSymbolName(char* InSymbolName)
	{
		SymbolName = InSymbolName;
	}

private:
	CCallTreeRecord(const CCallTreeRecord& other, CAllocator* InThreadIdRecordAllocator = nullptr)  // copy constructor (this should never get called)
	{
		assert(false);
	}

	CCallTreeRecord& operator=(const CCallTreeRecord&)  // assignment operator (this should never get called)
	{
		assert(false);
	}
};
