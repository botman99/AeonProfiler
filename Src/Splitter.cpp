
#include <Windows.h>
#include <math.h>
#include <unordered_map>

#include "Splitter.h"
#include "Dialog.h"

// This is a bit lame, we don't know the splitter window's hwnd until after the WM_CREATE message is called, so we store the 'this'
// pointer for the class we are constructing here and use it in the static StaticWndProc() function to add this hwnd to the map.
CSplitter* CreateSplitterWindow;

// This map is used to look up the Splitter class instance given the hwnd for the parent of that splitter window
std::unordered_map<HWND, CSplitter*> SplitterWindowMap;

WCHAR splitter_classname[16];
int splitter_count = 0;

bool bIsInitializing = false;

POINT CSplitter::m_minmaxinfo;


POINT RecursiveMinMaxInfo(HWND hwnd)
{
	POINT minmax;
	minmax.x = 0;
	minmax.y = 0;

	auto splitter_it = SplitterWindowMap.find(hwnd);
	if( splitter_it != SplitterWindowMap.end() )
	{
		CSplitter* splitter = splitter_it->second;

		if( splitter->m_orientation == ESplitterOrientation_Horizontal )
		{
			POINT child1_minmax = RecursiveMinMaxInfo(splitter->m_hwnd_Child1);
			POINT child2_minmax = RecursiveMinMaxInfo(splitter->m_hwnd_Child2);

			minmax.x = max(child1_minmax.x, child2_minmax.x);
			minmax.y = max(splitter->m_min_pos_offset, child1_minmax.y) + max(splitter->m_max_pos_offset, child2_minmax.y);

			// adjust this splitters min and max offset values based on the min size of its children
			splitter->m_min_pos_offset = max(splitter->m_min_pos_offset, child1_minmax.y);
			splitter->m_max_pos_offset = max(splitter->m_max_pos_offset, child2_minmax.y);

			minmax.y += CSplitter::nSplitterSize;
		}
		else
		{
			POINT child1_minmax = RecursiveMinMaxInfo(splitter->m_hwnd_Child1);
			POINT child2_minmax = RecursiveMinMaxInfo(splitter->m_hwnd_Child2);

			minmax.x = max(splitter->m_min_pos_offset, child1_minmax.x) + max(splitter->m_max_pos_offset, child2_minmax.x);
			minmax.y = max(child1_minmax.y, child2_minmax.y);

			// adjust this splitters min and max offset values based on the min size of its children
			splitter->m_min_pos_offset = max(splitter->m_min_pos_offset, child1_minmax.x);
			splitter->m_max_pos_offset = max(splitter->m_max_pos_offset, child2_minmax.x);

			minmax.x += CSplitter::nSplitterSize;
		}

		return minmax;
	}

	return minmax;
}


CSplitter::CSplitter(HINSTANCE hInstance, WNDPROC wndproc, HWND parent_hwnd, const WCHAR* szAppName, const WCHAR* menuName, DWORD style,
						int window_pos_x, int window_pos_y, int window_width, int window_height, int nShowCmd,
						ESplitterOrientation orientation, float percent, int min_pos_offset, int max_pos_offset)
	: m_hInstance(hInstance)
	, m_wndproc(wndproc)
	, m_szAppName(szAppName)
	, m_hwnd_Child1(nullptr)
	, m_hwnd_Child2(nullptr)
	, m_hwnd_Splitter(nullptr)
	, m_orientation(orientation)
	, m_splitter_percent(percent)
	, m_min_pos_offset(min_pos_offset)
	, m_max_pos_offset(max_pos_offset)
{
	if( m_max_pos_offset < nSplitterSize )  // top of the splitter must be at least the splitter width from the bottom of the window
	{
		m_max_pos_offset = nSplitterSize;
	}

	m_old_pos = 0;
	m_window_width = 0;
	m_window_height = 0;

	m_nSplitterPos = 0;
	m_nSplitterCursorOffset = 0;

	m_fDragMode = FALSE;

	//
	//	Register our main window class
	//
	WNDCLASSEX	wc;

	wsprintf(splitter_classname, TEXT("splitter_%d"), splitter_count++);

	wc.cbSize			= sizeof(wc);

	wc.style			= 0;
	wc.lpfnWndProc		= &CSplitter::StaticWndProc;
	wc.cbClsExtra		= 0;
	wc.cbWndExtra		= 0;
	wc.hInstance		= m_hInstance;
	wc.hIcon			= LoadIcon (NULL, IDI_APPLICATION);

	if( m_orientation == ESplitterOrientation_Horizontal )
	{
		wc.hCursor			= LoadCursor (NULL, IDC_SIZENS);
	}
	else
	{
		wc.hCursor			= LoadCursor (NULL, IDC_SIZEWE);
	}

	wc.hbrBackground	= CreateSolidBrush(RGB(220,220,220));  // splitter color
	wc.lpszMenuName		= menuName;
	wc.lpszClassName	= splitter_classname;
	wc.hIconSm			= LoadIcon (NULL, IDI_APPLICATION);

	RegisterClassEx(&wc);

	//
	//	Create the splitter window. This window will host two child controls.
	//
	CreateSplitterWindow = this;

	if( parent_hwnd == nullptr )  // main window
	{
		bIsInitializing = true;
	}

	m_hwnd_Splitter = CreateWindowEx(0,		// extended style (not needed)
				splitter_classname,			// window class name
				m_szAppName,				// window caption
				style,						// window style
				window_pos_x,				// initial x position
				window_pos_y,				// initial y position
				window_width,				// initial x size
				window_height,				// initial y size
				parent_hwnd,				// parent window handle
				NULL,						// use window class menu
				m_hInstance,				// program instance handle
				NULL);						// creation parameters

	if( parent_hwnd == nullptr )  // main window
	{
		bIsInitializing = false;

		RECT client_rect;
		RECT window_rect;

		GetClientRect(m_hwnd_Splitter, &client_rect);
		GetWindowRect(m_hwnd_Splitter, &window_rect);

		int border_x = (window_rect.right - window_rect.left) - client_rect.right;
		int border_y = (window_rect.bottom - window_rect.top) - client_rect.bottom;

		m_minmaxinfo = RecursiveMinMaxInfo(m_hwnd_Splitter);

		m_minmaxinfo.x += border_x;
		m_minmaxinfo.y += border_y;

		RedrawWindow(m_hwnd_Splitter, NULL, NULL, RDW_INVALIDATE);

		ShowWindow(m_hwnd_Splitter, nShowCmd);
		UpdateWindow(m_hwnd_Splitter);
	}
}

BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam)
{
	auto splitter_it = SplitterWindowMap.find((HWND)lParam);
	if( splitter_it != SplitterWindowMap.end() )
	{
		if( GetParent(hwnd) == splitter_it->second->m_hwnd_Splitter )
		{
			if( splitter_it->second->m_hwnd_Child1 == nullptr )
			{
				splitter_it->second->m_hwnd_Child1 = hwnd;
			}
			else
			{
				splitter_it->second->m_hwnd_Child2 = hwnd;
			}
		}
	}

	return TRUE;
}

LRESULT CALLBACK CSplitter::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if( msg == WM_CREATE )
	{
		CreateSplitterWindow->m_hwnd_Splitter = hwnd;

		std::pair<HWND, CSplitter*> splitter_pair(hwnd, CreateSplitterWindow);
		SplitterWindowMap.insert(splitter_pair);

		LRESULT result = CreateSplitterWindow->m_wndproc(hwnd, msg, wParam, lParam);

		EnumChildWindows(hwnd, EnumChildProc, (LPARAM)hwnd);

		return result;
	}

	auto splitter_it = SplitterWindowMap.find(hwnd);
	if( splitter_it != SplitterWindowMap.end() )
	{
		if( !splitter_it->second->WndProc(hwnd, msg, wParam, lParam) )
		{
			return 0;
		}
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}


LRESULT CSplitter::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch(msg)
	{
		case WM_LBUTTONDOWN:
			Splitter_OnLButtonDown(hwnd, msg, wParam, lParam);
			break;

		case WM_LBUTTONUP:
			Splitter_OnLButtonUp(hwnd, msg, wParam, lParam);
			break;

		case WM_MOUSEMOVE:
			Splitter_OnMouseMove(hwnd, msg, wParam, lParam);
			break;

		case WM_GETMINMAXINFO:  // control the minimum size of the parent window (to prevent splitter from being hidden)
			{
				MINMAXINFO* mmi = (MINMAXINFO*)lParam;
				mmi->ptMinTrackSize = m_minmaxinfo;
			}
			break;

		case WM_SIZE:
			if( HIWORD(lParam) > 0 )  // don't resize when minimizing
			{
				SizeWindowContents(LOWORD(lParam), HIWORD(lParam));
			}
			break;

		default:
			break;
	}

	// pass the hwnd, msg, wParam and lParam on along to the creator of the splitter
	return this->m_wndproc(hwnd, msg, wParam, lParam);
}

void CSplitter::SizeWindowContents(int nWidth, int nHeight)
{
	if( bIsInitializing )
	{
		return;  // don't do anything if we are still initializing the main parent window (and child windows)
	}

	if( m_orientation == ESplitterOrientation_Horizontal )
	{
		if( nHeight != m_window_height )  // if we are adjusting the size of the window, calculate the splitter position relative to the new size
		{
			m_nSplitterPos = (int)((float)nHeight * m_splitter_percent) - (nSplitterSize / 2);
		}
	}
	else
	{
		if( nWidth != m_window_width )  // if we are adjusting the size of the window, calculate the splitter position relative to the new size
		{
			m_nSplitterPos = (int)((float)nWidth * m_splitter_percent) - (nSplitterSize / 2);
		}
	}

	if( m_nSplitterPos < m_min_pos_offset )
	{
		m_nSplitterPos = m_min_pos_offset;
		if( m_orientation == ESplitterOrientation_Horizontal )
		{
			m_splitter_percent = (float)(m_nSplitterPos + (nSplitterSize / 2)) / (float)nHeight;
		}
		else
		{
			m_splitter_percent = (float)(m_nSplitterPos + (nSplitterSize / 2)) / (float)nWidth;
		}
	}

	if( m_orientation == ESplitterOrientation_Horizontal )
	{
		if( m_nSplitterPos > (nHeight - m_max_pos_offset - nSplitterSize) ) 
		{
			m_nSplitterPos = nHeight - m_max_pos_offset - nSplitterSize;
			m_splitter_percent = (float)(m_nSplitterPos + (nSplitterSize / 2)) / (float)nHeight;
		}
	}
	else
	{
		if( m_nSplitterPos > (nWidth - m_max_pos_offset - nSplitterSize) ) 
		{
			m_nSplitterPos = nWidth - m_max_pos_offset - nSplitterSize;
			m_splitter_percent = (float)(m_nSplitterPos + (nSplitterSize / 2)) / (float)nWidth;
		}
	}

	m_window_width = nWidth;
	m_window_height = nHeight;

	if( m_hwnd_Child1 && m_hwnd_Child2 )
	{
		if( m_orientation == ESplitterOrientation_Horizontal )
		{
			MoveWindow(m_hwnd_Child1, 0, 0, m_window_width, m_nSplitterPos, TRUE);
			MoveWindow(m_hwnd_Child2, 0, m_nSplitterPos + nSplitterSize, m_window_width, m_window_height - m_nSplitterPos - nSplitterSize, TRUE);
		}
		else
		{
			MoveWindow(m_hwnd_Child1, 0, 0, m_nSplitterPos, m_window_height, TRUE);
			MoveWindow(m_hwnd_Child2, m_nSplitterPos + nSplitterSize, 0, m_window_width - m_nSplitterPos - nSplitterSize, m_window_height, TRUE);
		}
	}
}

void CSplitter::DrawXorBar(HDC hdc, int x1, int y1, int width, int height)
{
	static WORD _dotPatternBmp[8] = 
	{ 
		0x00aa, 0x0055, 0x00aa, 0x0055, 
		0x00aa, 0x0055, 0x00aa, 0x0055
	};

	HBITMAP hbm;
	HBRUSH  hbr, hbrushOld;

	hbm = CreateBitmap(8, 8, 1, 1, _dotPatternBmp);
	hbr = CreatePatternBrush(hbm);
	
	SetBrushOrgEx(hdc, x1, y1, 0);
	hbrushOld = (HBRUSH)SelectObject(hdc, hbr);
	
	PatBlt(hdc, x1, y1, width, height, PATINVERT);
	
	SelectObject(hdc, hbrushOld);
	
	DeleteObject(hbr);
	DeleteObject(hbm);
}

LRESULT CSplitter::Splitter_OnLButtonDown(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	POINT pt;
	RECT rect;
	HDC hdc;

	pt.x = (short)LOWORD(lParam);  // horizontal position of cursor 
	pt.y = (short)HIWORD(lParam);

	if( m_orientation == ESplitterOrientation_Horizontal )
	{
		m_nSplitterCursorOffset = pt.y - m_nSplitterPos;
	}
	else
	{
		m_nSplitterCursorOffset = pt.x - m_nSplitterPos;
	}

	GetWindowRect(hwnd, &rect);

	//convert the mouse coordinates relative to the top-left of the window
	ClientToScreen(hwnd, &pt);
	pt.x -= rect.left;
	pt.y -= rect.top;
	
	//same for the window coordinates - make them relative to 0,0
	OffsetRect(&rect, -rect.left, -rect.top);
	
	m_fDragMode = TRUE;

	SetCapture(hwnd);

	hdc = GetWindowDC(hwnd);
	if( m_orientation == ESplitterOrientation_Horizontal )
	{
		DrawXorBar(hdc, 0, pt.y - m_nSplitterCursorOffset, rect.right - 1, nSplitterSize);
	}
	else
	{
		DrawXorBar(hdc, pt.x - m_nSplitterCursorOffset, 0, nSplitterSize, rect.bottom - 1 );
	}
	ReleaseDC(hwnd, hdc);

	if( m_orientation == ESplitterOrientation_Horizontal )
	{
		m_old_pos = pt.y;
	}
	else
	{
		m_old_pos = pt.x;
	}
		
	return 0;
}

LRESULT CSplitter::Splitter_OnLButtonUp(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	POINT pt;
	RECT rect;
	HDC hdc;

	if( m_fDragMode == FALSE )
	{
		return 0;
	}

	pt.x = (short)LOWORD(lParam);  // horizontal position of cursor 
	pt.y = (short)HIWORD(lParam);

	if( m_orientation == ESplitterOrientation_Horizontal )
	{
		pt.y -= m_nSplitterCursorOffset;

		if( pt.y < m_min_pos_offset )
		{
			pt.y = m_min_pos_offset;
		}

		if( pt.y > (m_window_height - m_max_pos_offset - nSplitterSize) ) 
		{
			pt.y = m_window_height - m_max_pos_offset - nSplitterSize;
		}
	}
	else
	{
		pt.x -= m_nSplitterCursorOffset;

		if( pt.x < m_min_pos_offset )
		{
			pt.x = m_min_pos_offset;
		}

		if( pt.x > (m_window_width - m_max_pos_offset - nSplitterSize) ) 
		{
			pt.x = m_window_width - m_max_pos_offset - nSplitterSize;
		}
	}

	GetWindowRect(hwnd, &rect);

	ClientToScreen(hwnd, &pt);
	pt.x -= rect.left;
	pt.y -= rect.top;
	
	OffsetRect(&rect, -rect.left, -rect.top);

	hdc = GetWindowDC(hwnd);
	if( m_orientation == ESplitterOrientation_Horizontal )
	{
		DrawXorBar(hdc, 0, m_old_pos - m_nSplitterCursorOffset, rect.right - 1, nSplitterSize);
	}
	else
	{
		DrawXorBar(hdc, m_old_pos - m_nSplitterCursorOffset, 0, nSplitterSize, rect.bottom - 1);
	}
	ReleaseDC(hwnd, hdc);

	if( m_orientation == ESplitterOrientation_Horizontal )
	{
		m_old_pos = pt.y;
	}
	else
	{
		m_old_pos = pt.x;
	}

	m_fDragMode = FALSE;

	//convert the splitter position back to screen coords.
	GetWindowRect(hwnd, &rect);
	pt.x += rect.left;
	pt.y += rect.top;

	//now convert into CLIENT coordinates
	ScreenToClient(hwnd, &pt);
	GetClientRect(hwnd, &rect);

	if( m_orientation == ESplitterOrientation_Horizontal )
	{
		m_nSplitterPos = pt.y;
		m_nSplitterCursorOffset = 0;
		m_splitter_percent = (float)(m_nSplitterPos + (nSplitterSize / 2)) / (float)m_window_height;
	}
	else
	{
		m_nSplitterPos = pt.x;
		m_nSplitterCursorOffset = 0;
		m_splitter_percent = (float)(m_nSplitterPos + (nSplitterSize / 2)) / (float)m_window_width;
	}

	//position the child controls
	SizeWindowContents(rect.right,rect.bottom);

	ReleaseCapture();

	PostMessage(hwnd, WM_SPLITTER_PERCENT, 0, (LPARAM)&m_splitter_percent);

	return 0;
}

LRESULT CSplitter::Splitter_OnMouseMove(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	POINT pt;
	RECT rect;
	HDC hdc;

	if( m_fDragMode == FALSE )
	{
		return 0;
	}

	pt.x = (short)LOWORD(lParam);  // horizontal position of cursor 
	pt.y = (short)HIWORD(lParam);

	if( m_orientation == ESplitterOrientation_Horizontal )
	{
		if( pt.y < (m_min_pos_offset + m_nSplitterCursorOffset) )
		{
			pt.y = (m_min_pos_offset + m_nSplitterCursorOffset);
		}

		if( pt.y > (m_window_height - m_max_pos_offset + m_nSplitterCursorOffset - nSplitterSize) ) 
		{
			pt.y = m_window_height - m_max_pos_offset + m_nSplitterCursorOffset - nSplitterSize;
		}
	}
	else
	{
		if( pt.x < (m_min_pos_offset + m_nSplitterCursorOffset) )
		{
			pt.x = (m_min_pos_offset + m_nSplitterCursorOffset);
		}

		if( pt.x > (m_window_width - m_max_pos_offset + m_nSplitterCursorOffset - nSplitterSize) ) 
		{
			pt.x = m_window_width - m_max_pos_offset + m_nSplitterCursorOffset - nSplitterSize;
		}
	}

	GetWindowRect(hwnd, &rect);

	ClientToScreen(hwnd, &pt);
	pt.x -= rect.left;
	pt.y -= rect.top;

	OffsetRect(&rect, -rect.left, -rect.top);

	if( (((m_orientation == ESplitterOrientation_Horizontal) && (pt.y != m_old_pos)) ||
		((m_orientation == ESplitterOrientation_Vertical) && (pt.x != m_old_pos))) &&
		(wParam & MK_LBUTTON) )
	{
		hdc = GetWindowDC(hwnd);
		if( m_orientation == ESplitterOrientation_Horizontal )
		{
			DrawXorBar(hdc, 0, m_old_pos - m_nSplitterCursorOffset, rect.right - 1, nSplitterSize);
			DrawXorBar(hdc, 0, pt.y - m_nSplitterCursorOffset, rect.right - 1, nSplitterSize);
		}
		else
		{
			DrawXorBar(hdc, m_old_pos - m_nSplitterCursorOffset, 0, nSplitterSize, rect.bottom - 1);
			DrawXorBar(hdc, pt.x - m_nSplitterCursorOffset, 0, nSplitterSize, rect.bottom - 1);
		}
		ReleaseDC(hwnd, hdc);

		if( m_orientation == ESplitterOrientation_Horizontal )
		{
			m_old_pos = pt.y;
		}
		else
		{
			m_old_pos = pt.x;
		}
	}

	return 0;
}
