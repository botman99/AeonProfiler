
#pragma once

#include <Windows.h>

enum ESplitterOrientation
{
	ESplitterOrientation_Horizontal,
	ESplitterOrientation_Vertical
};

class CSplitter
{
private:
	HINSTANCE m_hInstance;
	WNDPROC m_wndproc;  // the application's WndProc function

	const WCHAR* m_szAppName;

	int m_old_pos;;
	int m_window_width;  // parent window width
	int m_window_height;  // parent window height

	int m_nSplitterPos;  // this is the pixel position of the top/left of the splitter bar
	float m_splitter_percent;
	int m_nSplitterCursorOffset;  // offset from top/left of splitter where cursor grabbed the splitter

	BOOL m_fDragMode;

	static POINT m_minmaxinfo;

public:
	static const int nSplitterSize = 5;

	HWND m_hwnd_Splitter;
	HWND m_hwnd_Child1;
	HWND m_hwnd_Child2;

	ESplitterOrientation m_orientation;

	int m_min_pos_offset;  // minimum splitter position offset from top/left
	int m_max_pos_offset;  // maximum splitter position offset from bottom/right

public:
	CSplitter(HINSTANCE hInstance, WNDPROC wndproc, HWND parent_hwnd, const WCHAR* szAppName, const WCHAR* menuName, DWORD style,
				int window_pos_x, int window_pos_y, int window_width, int window_height, int nShowCmd,
				ESplitterOrientation orientation, float percent, int min_pos_offset, int max_pos_offset);

	static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	LRESULT CSplitter::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	HWND GetHWND() { return m_hwnd_Splitter; }

	void SizeWindowContents(int nWidth, int nHeight);
	void DrawXorBar(HDC hdc, int x1, int y1, int width, int height);

	LRESULT Splitter_OnLButtonDown(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam);
	LRESULT Splitter_OnLButtonUp(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam);
	LRESULT Splitter_OnMouseMove(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam);

	POINT Splitter_CalcMinMaxWindowSize();  // returns min X and min Y size of window
};
