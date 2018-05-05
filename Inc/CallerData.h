//
// Copyright (c) 2015-2018 Jeffrey "botman" Broome
//

#pragma once

// This is the struct that is passed from the _penter and _pexit asm handler into ProfilerEnter() and ProfilerExit()

#pragma pack(push, 4)
struct CallerData_t
{
	DWORD64 Counter;  // RDTSC counter value at the time of the call
	const void* CallerAddress;
	DWORD ThreadId;
};
#pragma pack(pop)
