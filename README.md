# Aeon Profiler

[aeon - "a period of time"](https://en.wikipedia.org/wiki/Aeon)

The **Aeon profiler** is an instramented profiler for native (non-managed) Windows applications that are using the Microsoft Visual Studio compiler.  The **Aeon profiler** supports 32 bit and 64 bit applications.  It supports single threaded and multithreaded applications.  Instrumenting the code that you wish to profile is as simple as adding a couple of compiler options.  No manual modification of source code is necessary (other than modifying one source code file to include the AeonProfiler.h file).  You can select to profile individual source files or an entire project by just modifying one setting.

I created the **Aeon profiler** because I wanted an instrumented profiler that was fast and easy to use.  I wanted something that didn't require me to manually instrument each function that I wanted to profile.  I wanted something that didn't take a long time to create the instrumented executable each time I changed the source code and rebuilt my project or solution.

The **Aeon profiler** works by using the Microsoft Visual Studio compiler options `/Gh` and `/GH` to insert hooks to external functions `_penter()` and `_pexit()` (respectively).  The **Aeon profiler** implements the `_penter()` and `_pexit()` functions and uses them to calculate the amount of time that was spent inside each function.  It also maintains a stack of the functions being called so that it can build a [call graph](https://en.wikipedia.org/wiki/Call_graph) which can then be viewed as a table of functions showing the parent function (the function that called this function) and the children functions (the functions that are called by this function).  See the [UserGuide.md](/UserGuide.md) file for details and theory of operation.

Some of the unique features of the **Aeon profiler** is the ability to not only show the average time spent in a function, but to also show the maximum amount time spent in that function (so you can see what the worst case looks like for functions that are only occasionally slow).  It also can show you the depth of recursion for recursive functions so you can get a sense of how "deep" the recursion level can go (to get an idea of how much stack space is being used).

Here's an example screenshot:

[![screenshot](https://github.com/botman99/AeonProfiler/raw/master/img/Viewer_PSK_Screenshot_Preview.png)](https://github.com/botman99/AeonProfiler/raw/master/img/Viewer_PSK_Screenshot.png)

See the [Releases](https://github.com/botman99/AeonProfiler/releases) page to download the latest release.

* Author: Jeffrey "botman" Broome
* License: [MIT](http://opensource.org/licenses/mit-license.php)
* [User Guide](/UserGuide.md)
