# Aeon Profiler User Guide

## Getting Started

Start by downloading the [AeonProfiler.zip](https://github.com/botman99/AeonProfiler/releases) file and unzip it somewhere that your Visual Studio solution will have access to.  You could unzip it to your solution's folder and reference the files from there within Visual Studio.

You will need to include the `AeonProfiler.h` file in one of your source files (possibly in the source file that contains your `main()` or `WinMain()` function).  Like this:

```
#include "AeonProfiler.h"
```

You will then need to modify the project settings to add the `AeonProfiler\Inc` folder to your Include path.  In Visual Studio, right click on the project in the Solution Explorer and select 'Properties' then, under "Configuration Properties -> C/C++ -> General", edit the "Additional Include Directories" to include the path to the `AeonProfiler\Inc` folder, then click "OK".

Now you need to add the path to the `AeonProfiler\Release` folder for the Aeon Profiler library file.  Again, right click on the project in the Solution Explorer and select 'Properties' then, under "Configuration Properties -> Linker -> General", edit the "Additional Library Directories" to include the path to the `AeonProfiler\Release` folder, then click "OK".

At this point, you should be able to compile and link your solution (but it won't run yet).

Now you will need to copy the .dll file from the `AeonProfiler\Release` folder to wherever your .exe file is being built to.  If you are building a 32 bit application, you should copy the `AeonProfiler\Release\AeonProfiler.dll` file to your executable directory.  If you are building a 64 bit application, you should copy the  `AeonProfiler\Release\AeonProfiler64.dll` file to your executable directory.

At this point you should be able to run your executable, but you aren't actually profiling anything yet.

If you wish to have AeonProfiler symbol information available while you are debugging (in the event of a crash), you can copy the `AeonProfiler\Release\AeonProfiler.pdb` or `AeonProfiler\Release\AeonProfiler64.pdb` file to your executable directory (depending on whether you are running a 32 bit application or a 64 bit application respectively).

## Enabling Profiling

To enable profiling, you will need to add the compiler options `/Gh /GH` to either the project settings (if you want to profile the entire project) or to each individual source code file (if you only want to profile functions within that specific file).  You **MUST** remember to always add both the `/Gh` **and** `/GH` compiler options otherwise the profiler won't function properly and you may crash due to running out of memory.

If you want to enable profiling for the entire project, in Visual Studio, right click on the project in the Solution Explorer and select 'Properties' then, under "Configuration Properties -> C/C++ -> Command Line" add `/Gh /GH` in the "Additional Options" box and click "OK".  Like this:

![options](https://github.com/botman99/AeonProfiler/raw/master/img/ProjectCompileOptions.png)

If you want to enable profiling for specific source code files, in Visual Studio, right click on the source file in the Solution Explorer and select 'Properties' then, under "Configuration Properties -> C/C++ -> Command Line" add `/Gh /GH` in the "Additional Options" box and click "OK".

Enabling profiling for a single source code file will profile **ALL** functions in that source code file.  If you only want to profile a specific function, you will need to move that specific function to a new source code file and enable profiling on just that new source code file.

Once you've enabled profiling, rebuild your solution and when you run your executable, the **Aeon profiler** dialog should appear.  To capture profile data, click the "Capture" item in the menu.  The profiler dialog will automatically close when your application exits (and you can't keep the profiler open if the application is no longer running).

## Using The Aeon Profiler

Once the **Aeon profiler** dialog is running, you can use the 'Capture' item in the menu to capture profiling data.  The Aeon profiler is always collecting profiling data as long as the application is running.  Each time you use the 'Capture' menu item, it will copy the collected data into the dialog's ListView windows to allow you to analze the data.

There are 3 window panes (with splitters between them) that show you timing information for the code that you are profiling.  The upper left window pane is the 'Function' list which contains all the functions that have had data collected for them.  Each time you select a function from this list, it will show the parent(s) of that function in the 'Parents' window pane in the upper right.  The Parent function(s) are functions that have called the function that you selected in the 'Function' list.  You will also see any children that the selected function calls in the 'Children' window pane in the lower right.  You can double click on a function in the 'Parents' or 'Children' views to automatically select that function in the 'Function' list (and show the parents and children of that newly selected function).  See this screenshot as an example:

[![screenshot](https://github.com/botman99/AeonProfiler/raw/master/img/Viewer_PSK_Screenshot_Preview.png)](https://github.com/botman99/AeonProfiler/raw/master/img/Viewer_PSK_Screenshot.png)

Each time you select a function in the 'Function' list, the source code for that function will be displayed in the window pane in the lower left.  The code for the top of that function will automatically be centered (vertically) in the source code window pane.

The 'Function' view displays the following columns:

* Times Called - This is the total number of times this function has been called.
* Exclusive Time Sum - This is the total amount of time spent in this function excluding any time spent in any child functions.
* Inclusive Time Sum - This is the total amount of time spent in this function including any time spent in any child functions.
* Avg Exclusive Time - This is the average amount of time spent in this function excluding any time spent in any child functions (This is 'Exclusive Time Sum' divided by 'Times Called').
* Avg Inclusive Time - This is the average amount of time spent in this function including any time spent in any child functions. (This is 'Inclusive Time Sum' divided by 'Times Called').
* Max Recursion - This is the maximum depth of recursion for this function (Functions that are not recursive will show '1').
* Max Exclusive Time - This is the maximum time that was ever spent in this function excluding any time spent in any child functions (This is exclusive because you don't want slow children to make the parent look slow).

You can copy the data from any of the 'Function', 'Parents' or 'Children' views by right clicking in that view and select one of the following options from the popup menu:

* Copy to Clipboard as Formatted Text - This will copy the data to the Windows clipboard as text and format the data so that columns are aligned.
* Copy to Clipboard as Comma Separated Values (CSV format) - This will copy the data to the Windows clipboard using Windows CSV format so that you can paste the data directly into a spreadsheet for later analysis.
* Copy to Clipboard as Comma Separated Values (Text format) - This will copy the data to the Windows clipboard in text format.  You can paste this into a text file and then later load that text file into a speadsheet.

####Finding a symbol by name
You can search for a symbol by name in any of the profiler ListView child windows ('Function', 'Parents' or 'Children') by pressing Ctrl-F when that child window is active and then typing in the name of the symbol (function name) to search for.  You can then select the function name from the list and click the 'Select' button or double-click on the function name from the list to go to that symbol name in the child window.

![find_symbol](https://github.com/botman99/AeonProfiler/raw/master/img/FindFunction.png)

####Saving the profiler data
You can save the currently captured profiler data to a file and then load it up again using the AeonWin.exe application for later analysis by using the **File -> Save** menu item.  Use **File -> Load** when running the AeonWin.exe application to load the saved profiler data.

![save_profile](https://github.com/botman99/AeonProfiler/raw/master/img/SaveProfilerData.png)
