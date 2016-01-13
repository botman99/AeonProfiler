
#pragma once

// C RunTime Header Files
#include <assert.h>
#include <new>

#include "Allocator.h"
#include "CallTreeRecord.h"

struct DialogStackCallerData_t
{
	DWORD ThreadId;
	DWORD64 Counter;
	__int64 ProfilerOverhead;  // the total amount of time spent in the profiler tracking this call
	const void* CallerAddress;
	struct DialogCallTreeRecord_t* CurrentCallTreeRecord;  // pointer to a static copy of the current function's CallTreeRecord_t
};

struct StackCallerData_t
{
	DWORD ThreadId;
	DWORD64 Counter;
	__int64 ProfilerOverhead;  // the total amount of time spent in the profiler tracking this call
	const void* CallerAddress;
	class CCallTreeRecord* CurrentCallTreeRecord;  // pointer to the current function's CallTreeRecord_t (so child can update parent's inclusive time for the current call)
};

class CStack
{
public:
	struct Stack_t
	{
		StackCallerData_t value;
		Stack_t* Next;
	};

	CAllocator* StackAllocator;

	Stack_t* pFree;
	Stack_t* pTop;  // the top node of the stack (if this is null the stack is empty)

	int StackSize;

	CStack(CAllocator* InStackAllocator) :
		StackAllocator(InStackAllocator)
		,pFree(nullptr)
		,pTop(nullptr)
		,StackSize(0)
	{
	}

	~CStack() {}

	void Push(StackCallerData_t && InValue)
	{
		Stack_t * item;
		if (pFree)
		{
			item = new(pFree) Stack_t;
			pFree = pFree->Next;
		}
		else
		{
			item = StackAllocator->New<Stack_t>();
		}

		item->Next = pTop;
		pTop = item;

		pTop->value = std::move(InValue);

		StackSize++;
	}

	void Pop(StackCallerData_t && OutValue)
	{
		assert(pTop);

		OutValue = std::move(pTop->value);
		Stack_t * next = pTop->Next;
		pTop->Next = pFree;
		pFree = pTop;
		pTop = next;

		StackSize--;
		assert(StackSize >= 0);
	}

	StackCallerData_t* Top()
	{
		if( pTop )
		{
			return &pTop->value;
		}

		return nullptr;
	}

	bool IsEmpty()
	{
		return (pTop == nullptr);
	}

	DialogStackCallerData_t* CopyStackToArray(CAllocator* InCopyAllocator, unsigned int& OutArraySize)  // copy this Stack to a fixed size array
	{
		OutArraySize = 0;

		if( InCopyAllocator )
		{
			DialogStackCallerData_t* pArray = (DialogStackCallerData_t*)InCopyAllocator->AllocateBytes(StackSize * sizeof(DialogStackCallerData_t), sizeof(void*));

			int index = 0;
			Stack_t* pNode = pTop;

			while( pNode )
			{
				pArray[index].ThreadId = pNode->value.ThreadId;
				pArray[index].Counter = pNode->value.Counter;
				pArray[index].ProfilerOverhead = pNode->value.ProfilerOverhead;
				pArray[index].CallerAddress = pNode->value.CallerAddress;

				// replace the thread's CallTreeRecord pointer data with a statically allocated copy (so it doesn't change while the Windows Dialog copies other data)
				pArray[index].CurrentCallTreeRecord = (DialogCallTreeRecord_t*)pNode->value.CurrentCallTreeRecord->GetArrayCopy(InCopyAllocator, false);

				index++;
				pNode = pNode->Next;
			}

			assert(index == StackSize);

			OutArraySize = StackSize;

			return pArray;
		}

		return nullptr;
	}

	void ResetCounters(DWORD64 TimeNow)
	{
		int index = 0;
		Stack_t* pNode = pTop;  // base of array is the bottom of the stack

		while( pNode && (index < StackSize) )
		{
			pNode->value.Counter = TimeNow;
			pNode->value.ProfilerOverhead = 0;

			// calltree records on the stack have been called so update their CallCount and MaxRecursionLevel
			pNode->value.CurrentCallTreeRecord->CallCount = 1;
			pNode->value.CurrentCallTreeRecord->StackDepth = 1;
			pNode->value.CurrentCallTreeRecord->MaxRecursionLevel = 1;

			index++;
			pNode = pNode->Next;
		}
	}
};
