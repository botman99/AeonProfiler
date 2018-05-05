//
// Copyright (c) 2015-2018 Jeffrey "botman" Broome
//

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

CAllocator GlobalAllocator(true);  // needs to be thread safe

extern CAllocator SymbolAllocator;  // allocator for storing the symbol names
extern CAllocator DialogAllocator;  // allocator for the Profiler Dialog window
extern CAllocator TextViewerAllocator;  // allocator for viewing source code text

#define THREADID_HASH_TABLE_SIZE 32
CHash<CThreadIdRecord>* ThreadIdHashTable = nullptr;

DWORD64 ProfilerOverheadFudgeFactor = 2000;  // arbitrary amount of ticks to add to profiler overhead to account for the fact that we can't measure enter/exit code accurately (arrived at by experimentation)


void HandleExit()
{
	GlobalAllocator.FreeBlocks();
	SymbolAllocator.FreeBlocks();
	DialogAllocator.FreeBlocks();
	TextViewerAllocator.FreeBlocks();

	// call the destructor for any global classes
	if( gConfig )
	{
		delete gConfig;
		gConfig = nullptr;
	}
}


void CallerEnter(CallerData_t& Call)
{
	if( ThreadIdHashTable == nullptr )
	{
		ThreadIdHashTable = GlobalAllocator.New<CHash<CThreadIdRecord>>(&GlobalAllocator, THREADID_HASH_TABLE_SIZE, true);
	}

	assert(ThreadIdHashTable);

	// look up the thread id record
	__int64 pTemp = (__int64)Call.ThreadId;  // cast the DWORD ThreadId to a 64 bit value so we can safely cast that to a void pointer
	CThreadIdRecord* pThreadIdRec = ThreadIdHashTable->EmplaceIfNecessary((void*)pTemp, Call.ThreadId, Call.CallerAddress).second;
	assert(pThreadIdRec);

	EnterCriticalSection(&pThreadIdRec->ThreadIdCriticalSection);

	CCallTreeRecord* pCallTreeRec = pThreadIdRec->CallTreeHashTable.EmplaceIfNecessary((void*)Call.CallerAddress, Call.CallerAddress).second;
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

	// This dirty hack is to set the "proper" caller address for the bottommost function on the stack.  When the application starts the first thread
	// created will be for 'main' or 'WinMain' but mainCRTStartup or WinMainCRTStartup will call global ctors (global static constructors) before
	// calling 'main' or 'WinMain' and we don't want the main threead to use the symbol name of the global ctor, so when we detect that the main
	// thread's CallerAddress has changed, we update it with the most recent one (which should never change again once 'main' or 'WinMain' is called
	// unless you have a global ctor with a destructor that run's code after 'main' or 'WinMain' has exited).  Of course, this is all under the
	// assumption that you have /Gh /GH set for your application's 'main' or 'WinMain' function.  :)
	if( (pThreadIdRec->CallStack.StackSize == 0) && pThreadIdRec->CallerAddress != Call.CallerAddress )  // stack is empty and caller address has changed?
	{
		pThreadIdRec->CallerAddress = Call.CallerAddress;
	}

	int registers[4];
	__cpuid(registers, 0);  // slower but more accurate across multiple threads running on different cores
	DWORD64 CurrentTime = __rdtsc();  // get the "current time" in CPU ticks (do this as late as possible before returning to the program being profiled)

	CurrentCallerData.ProfilerOverhead = CurrentTime - Call.Counter;
	if( CurrentCallerData.ProfilerOverhead < 0 )
	{
		CurrentCallerData.ProfilerOverhead = 0;
	}

	pThreadIdRec->CallStack.Push(&CurrentCallerData);

	LeaveCriticalSection(&pThreadIdRec->ThreadIdCriticalSection);
}


void CallerExit(CallerData_t& Call)
{
	assert(ThreadIdHashTable);

	// look up the thread id record
	__int64 pTemp = (__int64)Call.ThreadId;  // cast the DWORD ThreadId to a 64 bit value so we can safely cast that to a void pointer
	CThreadIdRecord** pThreadIdRecPtr = ThreadIdHashTable->LookupPointer((void*)pTemp);
	CThreadIdRecord* pThreadIdRec = *pThreadIdRecPtr;
	assert(pThreadIdRec);

	if( pThreadIdRec )
	{
		EnterCriticalSection(&pThreadIdRec->ThreadIdCriticalSection);

		assert( !pThreadIdRec->CallStack.IsEmpty() );

		if( !pThreadIdRec->CallStack.IsEmpty() )  // sometimes the stack is empty at CallerExit (not really sure how this can happen, but handle it and don't crash)
		{
			// get the current call record off of this thread's call stack
			StackCallerData_t CurrentCallerData;

			pThreadIdRec->CallStack.Pop(&CurrentCallerData);

			assert( CurrentCallerData.CurrentCallTreeRecord );

			CurrentCallerData.CurrentCallTreeRecord->StackDepth--;

			assert(CurrentCallerData.CurrentCallTreeRecord->StackDepth >= 0);

			// get the parent call record off the top of this thread's call stack
			StackCallerData_t* ParentCallerData = pThreadIdRec->CallStack.Top();

			// if we have a parent, then set up the relationship between parent(s) and children...
			// (each parent has a hash table listing their children and each child has a hash table listing their parents)
			if( ParentCallerData )
			{
				// find the parent calltree record for this child...
				if( CurrentCallerData.CurrentCallTreeRecord->ParentHashTable == nullptr )  // create the parent hash table if needed
				{
					CurrentCallerData.CurrentCallTreeRecord->ParentHashTable =
						pThreadIdRec->ThreadIdRecordAllocator.New<CHash<CCallTreeRecord>>(&pThreadIdRec->ThreadIdRecordAllocator, PARENT_CALLRECORD_HASH_TABLE_SIZE, false);
				}

				// see if the parent already exists in this child's ParentHashTable
				CCallTreeRecord** pParentCallTreeRecPtr = CurrentCallerData.CurrentCallTreeRecord->ParentHashTable->LookupPointer((void*)ParentCallerData->CallerAddress);
				*pParentCallTreeRecPtr = ParentCallerData->CurrentCallTreeRecord;
				assert(ParentCallerData->CurrentCallTreeRecord);

				// find the child calltree record in this child's parent...
				if( ParentCallerData->CurrentCallTreeRecord->ChildrenHashTable == nullptr )
				{
					ParentCallerData->CurrentCallTreeRecord->ChildrenHashTable =
						pThreadIdRec->ThreadIdRecordAllocator.New<CHash<CCallTreeRecord>>(&pThreadIdRec->ThreadIdRecordAllocator, CHILDREN_CALLRECORD_HASH_TABLE_SIZE, false);
				}

				// see if this child already exists in the parent's ChildrenHashTable
				CCallTreeRecord** pChildCallTreeRecPtr = ParentCallerData->CurrentCallTreeRecord->ChildrenHashTable->LookupPointer((void*)CurrentCallerData.CallerAddress);
				*pChildCallTreeRecPtr = CurrentCallerData.CurrentCallTreeRecord;
				assert(CurrentCallerData.CurrentCallTreeRecord);
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
				ParentCallerData->ProfilerOverhead += (CurrentTime - Call.Counter) + ProfilerOverheadFudgeFactor;
				if( ParentCallerData->ProfilerOverhead < 0 )
				{
					ParentCallerData->ProfilerOverhead = 0;
				}
			}
		}

		LeaveCriticalSection(&pThreadIdRec->ThreadIdCriticalSection);
	}
}
