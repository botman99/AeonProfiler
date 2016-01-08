
#define WIN32_LEAN_AND_MEAN  // Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <Windows.h>

// C RunTime Header Files
#include <assert.h>
#include <new>

#include "DebugLog.h"

#include "CallerData.h"
#include "CallTreeRecord.h"
#include "ThreadIdRecord.h"
#include "Dialog.h"
#include "Config.h"

extern CConfig* gConfig;
extern CDebugLog* GDebugLog;

CAllocator GlobalAllocator;

extern CAllocator SymbolAllocator;  // allocator for storing the symbol names
extern CAllocator DialogAllocator;  // allocator for the Profiler Dialog window

#define THREADID_HASH_TABLE_SIZE 32
CHash<CThreadIdRecord>* ThreadIdHashTable = nullptr;

extern int TicksPerHundredNanoseconds;


void HandleExit()
{
	GlobalAllocator.PrintStats("GlobalAllocator - ", 0);
	DebugLog("");

	SymbolAllocator.PrintStats("SymbolAllocator - ", 0);
	DebugLog("");

	DialogAllocator.PrintStats("DialogAllocator - ", 0);
	DebugLog("");

	if( ThreadIdHashTable )
	{
		ThreadIdHashTable->PrintStats("ThreadIdHashTable - ", 0);
		DebugLog("");
	}

	// call the destructor for any global classes
	if( gConfig )
	{
		delete gConfig;
		gConfig = nullptr;
	}
}


void CallerEnter(CallerData_t& Call)
{
	EnterCriticalSection(&gCriticalSection);

	if( ThreadIdHashTable == nullptr )
	{
		ThreadIdHashTable = (CHash<CThreadIdRecord>*)GlobalAllocator.AllocateBytes(sizeof(CHash<CThreadIdRecord>), sizeof(void*));
		new(ThreadIdHashTable) CHash<CThreadIdRecord>(&GlobalAllocator, THREADID_HASH_TABLE_SIZE, true);
	}

	assert(ThreadIdHashTable);

	// look up the thread id record
	__int64 pTemp = (__int64)Call.ThreadId;  // cast the DWORD ThreadId to a 64 bit value so we can safely cast that to a void pointer
	CThreadIdRecord** pThreadIdRecPtr = ThreadIdHashTable->LookupPointer((void*)pTemp);
	CThreadIdRecord* pThreadIdRec = *pThreadIdRecPtr;
	if( pThreadIdRec == nullptr )
	{
		pThreadIdRec = (CThreadIdRecord*)GlobalAllocator.AllocateBytes(sizeof(CThreadIdRecord), sizeof(void*));
		new(pThreadIdRec) CThreadIdRecord(Call.ThreadId, GlobalAllocator);
		*pThreadIdRecPtr = pThreadIdRec;  // store the pointer to the new record in the hash
	}

	assert(pThreadIdRec);

	if( pThreadIdRec && Call.CallerAddress )
	{
		CCallTreeRecord** pCallTreeRecPtr = pThreadIdRec->CallTreeHashTable->LookupPointer((void*)Call.CallerAddress);
		CCallTreeRecord* pCallTreeRec = *pCallTreeRecPtr;
		if( pCallTreeRec == nullptr )
		{
			pCallTreeRec = (CCallTreeRecord*)pThreadIdRec->ThreadIdRecordAllocator->AllocateBytes(sizeof(CCallTreeRecord), sizeof(void*));
			new(pCallTreeRec) CCallTreeRecord(Call.CallerAddress);
			*pCallTreeRecPtr = pCallTreeRec;  // store the pointer to the new record in the hash
		}

		assert(pCallTreeRec);

		pCallTreeRec->EnterTime = Call.Counter;  // keep track of when we entered this function (so the profiler can identify functions that haven't exited yet, things like "main()")
		pCallTreeRec->CallCount++;
		pCallTreeRec->StackDepth++;

		if( pCallTreeRec->StackDepth > pCallTreeRec->MaxRecursionLevel )
		{
			// non-recursive functions will have a MaxRecursionLevel of 1 (since they are called at least once in a call stack)
			pCallTreeRec->MaxRecursionLevel = pCallTreeRec->StackDepth;
		}

		pCallTreeRec->CurrentChildrenInclusiveTime = 0;

		// add this call record to the thread's call stack
		StackCallerData_t CurrentCallerData;
		CurrentCallerData.ThreadId = Call.ThreadId;
		CurrentCallerData.Counter = Call.Counter;
		CurrentCallerData.CallerAddress = Call.CallerAddress;
		CurrentCallerData.CurrentCallTreeRecord = pCallTreeRec;

		int registers[4];
		__cpuid(registers, 0);  // slower but more accurate across multiple threads running on different cores
		DWORD64 CurrentTime = __rdtsc();  // get the "current time" in CPU ticks (do this as late as possible before returning to the program being profiled)

		CurrentCallerData.ProfilerOverhead = CurrentTime - Call.Counter;
		if( CurrentCallerData.ProfilerOverhead < 0 )
		{
			CurrentCallerData.ProfilerOverhead = 0;
		}

		assert(pThreadIdRec->CallStack);

		pThreadIdRec->CallStack->Push(&CurrentCallerData);
	}
	else
	{
		DebugLog("CallerEnter: Caller Address was NULL!!!");
	}

	LeaveCriticalSection(&gCriticalSection);
}


void CallerExit(CallerData_t& Call)
{
	EnterCriticalSection(&gCriticalSection);

	if( ThreadIdHashTable == nullptr )  // this should never happen (thread id hash table should have been created in CallerEnter)
	{
		ThreadIdHashTable = (CHash<CThreadIdRecord>*)GlobalAllocator.AllocateBytes(sizeof(CHash<CThreadIdRecord>), sizeof(void*));
		new(ThreadIdHashTable) CHash<CThreadIdRecord>(&GlobalAllocator, THREADID_HASH_TABLE_SIZE, true);
	}

	assert(ThreadIdHashTable);

	// look up the thread id record
	__int64 pTemp = (__int64)Call.ThreadId;  // cast the DWORD ThreadId to a 64 bit value so we can safely cast that to a void pointer
	CThreadIdRecord** pThreadIdRecPtr = ThreadIdHashTable->LookupPointer((void*)pTemp);
	CThreadIdRecord* pThreadIdRec = *pThreadIdRecPtr;
	if( pThreadIdRec == nullptr )  // this should never happen (thread id should have been added in the Enter handler)
	{
		pThreadIdRec = (CThreadIdRecord*)GlobalAllocator.AllocateBytes(sizeof(CThreadIdRecord), sizeof(void*));
		new(pThreadIdRec) CThreadIdRecord(Call.ThreadId, GlobalAllocator);
		*pThreadIdRecPtr = pThreadIdRec;
	}

	assert(pThreadIdRec);

	if( pThreadIdRec && Call.CallerAddress )
	{
		assert( pThreadIdRec->CallStack );
		assert( !pThreadIdRec->CallStack->IsEmpty() );

		// get the current call record off of this thread's call stack
		StackCallerData_t CurrentCallerData;
		memset(&CurrentCallerData, 0, sizeof(StackCallerData_t));

		pThreadIdRec->CallStack->Pop(&CurrentCallerData);

		assert( CurrentCallerData.CurrentCallTreeRecord );

		CurrentCallerData.CurrentCallTreeRecord->StackDepth--;

		if( CurrentCallerData.CurrentCallTreeRecord->StackDepth < 0 )
		{
			CurrentCallerData.CurrentCallTreeRecord->StackDepth = 0;
		}

		// get the parent call record off the top of this thread's call stack
		StackCallerData_t* ParentCallerData = pThreadIdRec->CallStack->Top();

		// if we have a parent, then set up the relationship between parent(s) and children...
		// (each parent has a hash table listing their children and each child has a hash table listing their parents)
		if( ParentCallerData )
		{
			// find the parent calltree record for this child...
			if( CurrentCallerData.CurrentCallTreeRecord->ParentHashTable == nullptr )  // create the parent hash table if needed
			{
				CurrentCallerData.CurrentCallTreeRecord->ParentHashTable = (CHash<CCallTreeRecord>*)pThreadIdRec->ThreadIdRecordAllocator->AllocateBytes(sizeof(CHash<CCallTreeRecord>), sizeof(void*));
				new(CurrentCallerData.CurrentCallTreeRecord->ParentHashTable) CHash<CCallTreeRecord>(pThreadIdRec->ThreadIdRecordAllocator, PARENT_CALLRECORD_HASH_TABLE_SIZE);
			}

			// see if the parent already exists in this child's ParentHashTable
			CCallTreeRecord** pParentCallTreeRecPtr = CurrentCallerData.CurrentCallTreeRecord->ParentHashTable->LookupPointer((void*)ParentCallerData->CallerAddress);
			CCallTreeRecord* pParentCallTreeRec = *pParentCallTreeRecPtr;
			if( pParentCallTreeRec == nullptr )  // if parent doesn't already exist...
			{
				// see if the parent already exists in the Thread's CallTreeHashTable...
				CCallTreeRecord** pCallTreeRecPtr = pThreadIdRec->CallTreeHashTable->LookupPointer((void*)ParentCallerData->CallerAddress);
				CCallTreeRecord* pCallTreeRec = *pCallTreeRecPtr;
				if( pCallTreeRec == nullptr )  // if not, add the new parent
				{
					pParentCallTreeRec = (CCallTreeRecord*)pThreadIdRec->ThreadIdRecordAllocator->AllocateBytes(sizeof(CCallTreeRecord), sizeof(void*));
					new(pParentCallTreeRec) CCallTreeRecord(ParentCallerData->CallerAddress);
					*pCallTreeRecPtr = pParentCallTreeRec;  // store the pointer to the new record in the Thread's CallTreeHashTable
					*pParentCallTreeRecPtr = pParentCallTreeRec;  // store the pointer to the new record in this child's ParentHashTable
				}
				else  // otherwise, add the parent record from the Thread's CallTreeHashTable to this child's ParentHashTable
				{
					*pParentCallTreeRecPtr = pCallTreeRec;  // store the pointer to the new record in this child's ParentHashTable
					pParentCallTreeRec = pCallTreeRec;
				}
			}

			assert(pParentCallTreeRec);


			// find the child calltree record in this child's parent...
			if( ParentCallerData->CurrentCallTreeRecord->ChildrenHashTable == nullptr )
			{
				ParentCallerData->CurrentCallTreeRecord->ChildrenHashTable = (CHash<CCallTreeRecord>*)pThreadIdRec->ThreadIdRecordAllocator->AllocateBytes(sizeof(CHash<CCallTreeRecord>), sizeof(void*));
				new(ParentCallerData->CurrentCallTreeRecord->ChildrenHashTable) CHash<CCallTreeRecord>(pThreadIdRec->ThreadIdRecordAllocator, PARENT_CALLRECORD_HASH_TABLE_SIZE);
			}

			// see if this child already exists in the parent's ChildrenHashTable
			CCallTreeRecord** pChildCallTreeRecPtr = ParentCallerData->CurrentCallTreeRecord->ChildrenHashTable->LookupPointer((void*)CurrentCallerData.CallerAddress);
			CCallTreeRecord* pChildCallTreeRec = *pChildCallTreeRecPtr;
			if( pChildCallTreeRec == nullptr )  // if this child doesn't already exist...
			{
				// see if this child already exists in the Thread's CallTreeHashTable (it should)...
				CCallTreeRecord** pCallTreeRecPtr = pThreadIdRec->CallTreeHashTable->LookupPointer((void*)CurrentCallerData.CallerAddress);
				CCallTreeRecord* pCallTreeRec = *pCallTreeRecPtr;
				if( pCallTreeRec == nullptr )  // if not, add the new child (this should NEVER happen)
				{
					assert(false);
					pChildCallTreeRec = (CCallTreeRecord*)pThreadIdRec->ThreadIdRecordAllocator->AllocateBytes(sizeof(CCallTreeRecord), sizeof(void*));
					new(pChildCallTreeRec) CCallTreeRecord(CurrentCallerData.CallerAddress);
					*pCallTreeRecPtr = pChildCallTreeRec;  // store the pointer to the new record in the Thread's CallTreeHashTable
					*pChildCallTreeRecPtr = pChildCallTreeRec;  // store the pointer to the new record in the parent's ChildrenHashTable
				}
				else  // otherwise, add the child record from the Thread's CallTreeHashTable to this parent's ChildrenHashTable
				{
					*pChildCallTreeRecPtr = pCallTreeRec;  // store the pointer to the new record in this child's ParentHashTable
					pChildCallTreeRec = pCallTreeRec;
				}
			}

			assert(pChildCallTreeRec);
		}

		// calculate the duration of this function call (subtract _penter time from _pexit time and then subtract the profiler overhead for _penter)
		__int64 CallDuration = (Call.Counter - CurrentCallerData.Counter) - CurrentCallerData.ProfilerOverhead;
		if( CallDuration < 0 )
		{
			CallDuration = 0;
		}
		CallDuration = CallDuration / TicksPerHundredNanoseconds;  // duration is in 100ns units

		// update the total call duration inclusive time for this function...
		CurrentCallerData.CurrentCallTreeRecord->CallDurationInclusiveTimeSum += CallDuration;

		// calculate the call duration exclusive time for this function (subtract the children's inclusive time from this call's duration)
		__int64 CallDurationExclusiveTime = CallDuration - CurrentCallerData.CurrentCallTreeRecord->CurrentChildrenInclusiveTime;
		if( CallDurationExclusiveTime < 0 )
		{
			CallDurationExclusiveTime = 0;
		}
		// ...and add it to the total call duration exclusive time...
		CurrentCallerData.CurrentCallTreeRecord->CallDurationExclusiveTimeSum += CallDurationExclusiveTime;

		// keep track of the maximum call duration for this function...
		if( CallDurationExclusiveTime > CurrentCallerData.CurrentCallTreeRecord->MaxCallDurationExclusiveTime )
		{
			CurrentCallerData.CurrentCallTreeRecord->MaxCallDurationExclusiveTime = CallDurationExclusiveTime;
		}

		if( ParentCallerData )  // if we have a parent function
		{
			// ...add this child function's call duration to the parent's current children inclusive time (so it can subtract that to calculate parent's exclusive time)
			ParentCallerData->CurrentCallTreeRecord->CurrentChildrenInclusiveTime += CallDuration;
		}

		CurrentCallerData.CurrentCallTreeRecord->EnterTime = 0;  // indicate to the profiler dialog that this function has exited

		int registers[4];
		__cpuid(registers, 0);  // slower but more accurate across multiple threads running on different cores
		DWORD64 CurrentTime = __rdtsc();  // get the "current time" in CPU ticks (do this as late as possible before returning to the program being profiled)

		// add the pexit overhead to the parent's penter overhead (so that the parent can subtract out this time for its call duration)
		if( ParentCallerData )
		{
			ParentCallerData->ProfilerOverhead += (CurrentTime - Call.Counter);
			if( ParentCallerData->ProfilerOverhead < 0 )
			{
				ParentCallerData->ProfilerOverhead = 0;
			}
		}
	}
	else
	{
		DebugLog("CallerExit: Caller Address was NULL!!!");
	}

	LeaveCriticalSection(&gCriticalSection);
}
