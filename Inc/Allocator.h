
#pragma once

#include <type_traits>

#pragma warning( push )
#pragma warning( disable : 4345 )  // ignore warning "behavior change: an object of POD type constructed with an initializer of the form () will be default-initialized"


class CAllocator  // super simple allocator that uses VirtualAlloc to allocate memory
{
private:
	struct AllocHeader
	{
		char* NextBlock;		// pointer to next block allocated using VirtualAlloc
		char* FreePointer;		// pointer to first byte free in this block
		size_t Size;			// size of this block (in bytes)
		size_t FreeRemaining;	// number of bytes free in this block
	};

	char* FirstBlock;			// pointer to first block that was allocated (head of list)
	char* CurrentBlock;			// pointer to current block (tail of list)

	bool bNeedsToBeThreadSafe;	// whether this allocator needs to be thread safe (is it used by more than one thread?)
	CRITICAL_SECTION AllocatorCriticalSection;

public:
	CAllocator(bool bInNeedsToBeThreadSafe);
	~CAllocator();

	void FreeBlocks();			// free all the allocations by freeing the blocks allocated from the operating system
	void GetAllocationStats(size_t& TotalSize, size_t& FreeSize);
	void PrintStats(char* Header, int NestLevel);

	void* AllocateBytes(size_t NumBytes, int Alignment);

	template<typename T>
	T * New()
	{
		T * obj = (T*)AllocateBytes(sizeof(T), std::alignment_of<T>::value);
		return new (obj) T();
	}

	// TODO: Variadic template.
	template<typename T, typename Arg1>
	T * New(Arg1 && arg1)
	{
		T * obj = (T*)AllocateBytes(sizeof(T), std::alignment_of<T>::value);
		return new (obj) T(std::forward<Arg1>(arg1));
	}

	template<typename T, typename Arg1, typename Arg2>
	T * New(Arg1 && arg1, Arg2 && arg2)
	{
		T * obj = (T*)AllocateBytes(sizeof(T), std::alignment_of<T>::value);
		return new (obj) T(std::forward<Arg1>(arg1), std::forward<Arg2>(arg2));
	}

	template<typename T, typename Arg1, typename Arg2, typename Arg3>
	T * New(Arg1 && arg1, Arg2 && arg2, Arg3 && arg3)
	{
		T * obj = (T*)AllocateBytes(sizeof(T), std::alignment_of<T>::value);
		return new (obj) T(std::forward<Arg1>(arg1), std::forward<Arg2>(arg2), std::forward<Arg3>(arg3));
	}
};

#pragma warning( pop )