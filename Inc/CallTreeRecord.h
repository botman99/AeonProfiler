//
// Copyright (c) 2015-2018 Jeffrey "botman" Broome
//

#pragma once

// C RunTime Header Files
#include <assert.h>
#include <new>

#include "Allocator.h"
#include "Hash.h"
#include "Repository.h"

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
	int SourceFileIndex;
	int SourceFileLineNumber;

	void Serialize(Repository& Repo, bool bDeepCopy)
	{
		if( Repo.bIsDebugSave )
		{
			WORD StackCallerData_Signature = 0x3cc3;
			if( Repo.bIsLoading )
			{
				WORD Signature;
				Repo << Signature;
				assert( Signature == StackCallerData_Signature );
			}
			else
			{
				Repo << StackCallerData_Signature;
			}
		}

		if( Repo.bIsLoading )
		{
			NumCallTreeRecords++;
		}

		if( !bDeepCopy && !Repo.bIsLoading )
		{
			unsigned int TempParentArraySize = 0;
			Repo << TempParentArraySize;
		}
		else
		{
			Repo << ParentArraySize;

			if( Repo.bIsLoading )
			{
				ParentArray = (ParentArraySize > 0) ? (void**)Repo.Allocator->AllocateBytes(ParentArraySize * sizeof(void*), sizeof(void*)) : nullptr;
				NumCallTreeRecords++;
			}

			assert( (ParentArraySize == 0) || (ParentArray != nullptr) );
			for( unsigned int index = 0; index < ParentArraySize; ++index )
			{
				if( Repo.bIsLoading )
				{
					ParentArray[index] = Repo.Allocator->AllocateBytes(sizeof(DialogCallTreeRecord_t), sizeof(void*));
				}

				assert( ParentArray[index] != nullptr );
				DialogCallTreeRecord_t* CallTreeRec = (DialogCallTreeRecord_t*)ParentArray[index];
				CallTreeRec->Serialize(Repo, false);
			}
		}

		if( !bDeepCopy && !Repo.bIsLoading )
		{
			unsigned int TempChildrenArraySize = 0;
			Repo << TempChildrenArraySize;
		}
		else
		{
			Repo << ChildrenArraySize;

			if( Repo.bIsLoading )
			{
				ChildrenArray = (ChildrenArraySize > 0) ? (void**)Repo.Allocator->AllocateBytes(ChildrenArraySize * sizeof(void*), sizeof(void*)) : nullptr;
			}

			assert( (ChildrenArraySize == 0) || (ChildrenArray != nullptr) );
			for( unsigned int index = 0; index < ChildrenArraySize; ++index )
			{
				if( Repo.bIsLoading )
				{
					ChildrenArray[index] = Repo.Allocator->AllocateBytes(sizeof(DialogCallTreeRecord_t), sizeof(void*));
				}

				assert( ChildrenArray[index] != nullptr );
				DialogCallTreeRecord_t* CallTreeRec = (DialogCallTreeRecord_t*)ChildrenArray[index];
				CallTreeRec->Serialize(Repo, false);
			}
		}

		if( Repo.bIsLoading )
		{
			__int64 Address64;
			Repo << Address64;
			Address = (void*)Address64;
		}
		else
		{
			__int64 Address64 = (__int64)Address;
			Repo << Address64;
		}

		Repo << EnterTime;

		Repo << CallDurationInclusiveTimeSum;
		Repo << CallDurationExclusiveTimeSum;
		Repo << MaxCallDurationExclusiveTime;
		Repo << CurrentChildrenInclusiveTime;

		Repo << CallCount;
		Repo << StackDepth;
		Repo << MaxRecursionLevel;

		//assert( (Repo.bIsLoading == true) || (SymbolName != nullptr) );
		Repo << SymbolName;
		Repo << SourceFileIndex;
		Repo << SourceFileLineNumber;
	}
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

	void Lock()
	{
		// do nothing
	}

	void Unlock()
	{
		// do nothing
	}

	unsigned int GetNumRecordsToCopy()
	{
		if( CallCount )
		{
			return 1;
		}

		return 0;
	}

	DialogCallTreeRecord_t* GetArrayCopy(CAllocator* InCopyAllocator, bool bCopyMemberHashTables)
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

		return pRec;
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
