//
// Copyright (c) 2015-2018 Jeffrey "botman" Broome
//

// Include this header file in the application that you wish to profile and copy the
// AeonProfiler.lib or AeonProfiler64.lib to a folder containing your libraries then
// copy the AeonProfiler.dll or AeonProfiler64.dll to the directory containing your .exe
//
// Then add "/Gh /GH" to the C/C++ compiler settings (Properties -> Configuration Properties
// -> C/C++ -> Command Line -> Advanced) for either your project (to profile the entire project),
// or for specific files within the project (to profile the functions in those specific files). 

#pragma once

#ifdef _M_X64
	#pragma comment(lib, "AeonProfiler64.lib")
#else
	#pragma comment(lib, "AeonProfiler.lib")
#endif
