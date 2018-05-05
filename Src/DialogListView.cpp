//
// Copyright (c) 2015-2018 Jeffrey "botman" Broome
//

#include "targetver.h"
#include "resource.h"

// Windows Header Files:
#include <Windows.h>
#include <Commctrl.h>

#include "Dialog.h"
#include "TextViewer.h"


int DialogListViewThreadIndex = -1;  // the index of the thread currently selected from the ThreadArray (-1 means invalid thread)

DialogThreadIdRecord_t* gListView_ThreadIdRecordForQsort;
const void* gRowCallTreeAddressForQsort;
HWND ghWndFromforQsort;

// ListView default settings for the three child windows...
struct ListViewColumnsDefaults
{
	TCHAR* ColumnName;
	SortType ColumnSortType;
	bool bIsDefaultSortColumn;
	int ColumnWidth;
	bool bLeftJustify;
};

ListViewColumnsDefaults ChildWindowFunctionsDefaults[] = {
	{ TEXT("#"), SORT_Unused, false, 50, false },							// column 1 (the row number)
	{ TEXT("Function"), SORT_Increasing, false, 200, true },				// column 2 (the function name)
	{ TEXT("Times Called"), SORT_Decreasing, false, 80, false },			// column 3 (number of times called)
	{ TEXT("Exclusive Time Sum"), SORT_Decreasing, false, 120, false },		// column 4 (exclusive time)
	{ TEXT("Inclusive Time Sum"), SORT_Decreasing, false, 120, false },		// column 5 (inclusive time)
	{ TEXT("Avg. Exclusive Time"), SORT_Decreasing, true, 120, false },		// column 6 (average exclusive time)
	{ TEXT("Avg. Inclusive Time"), SORT_Decreasing, false, 120, false },	// column 7 (average inclusive time)
	{ TEXT("Max Recursion"), SORT_Decreasing, false, 90, false },			// column 8 (maximum recursion level)
	{ TEXT("Max Exclusive Time"), SORT_Decreasing, false, 120, false },		// column 9 (maximum exclusive time)
};

ListViewColumnsDefaults ChildWindowParentFunctionsDefaults[] = {
	{ TEXT("#"), SORT_Unused, false, 50, false },							// column 1 (the row number)
	{ TEXT("Parents"), SORT_Increasing, true, 200, true },					// column 2 (the parent function name)
	{ TEXT("Times Called"), SORT_Decreasing, false, 80, false },			// column 3 (number of times called)
	{ TEXT("Exclusive Time Sum"), SORT_Decreasing, false, 120, false },		// column 4 (exclusive time)
	{ TEXT("Inclusive Time Sum"), SORT_Decreasing, false, 120, false },		// column 5 (inclusive time)
	{ TEXT("Avg. Exclusive Time"), SORT_Decreasing, true, 120, false },		// column 6 (average exclusive time)
	{ TEXT("Avg. Inclusive Time"), SORT_Decreasing, false, 120, false },	// column 7 (average inclusive time)
	{ TEXT("Max Recursion"), SORT_Decreasing, false, 90, false },			// column 8 (maximum recursion level)
	{ TEXT("Max Exclusive Time"), SORT_Decreasing, false, 120, false },		// column 9 (maximum exclusive time)
};

ListViewColumnsDefaults ChildWindowChildrenFunctionsDefaults[] = {
	{ TEXT("#"), SORT_Unused, false, 50, false },							// column 1 (the row number)
	{ TEXT("Children"), SORT_Increasing, false, 200, true },				// column 2 (the child function name)
	{ TEXT("Times Called"), SORT_Decreasing, false, 80, false },			// column 3 (number of times called)
	{ TEXT("Exclusive Time Sum"), SORT_Decreasing, false, 120, false },		// column 4 (exclusive time)
	{ TEXT("Inclusive Time Sum"), SORT_Decreasing, false, 120, false },		// column 5 (inclusive time)
	{ TEXT("Avg. Exclusive Time"), SORT_Decreasing, true, 120, false },		// column 6 (average exclusive time)
	{ TEXT("Avg. Inclusive Time"), SORT_Decreasing, false, 120, false },	// column 7 (average inclusive time)
	{ TEXT("Max Recursion"), SORT_Decreasing, false, 90, false },			// column 8 (maximum recursion level)
	{ TEXT("Max Exclusive Time"), SORT_Decreasing, false, 120, false },		// column 9 (maximum exclusive time)
};


// the default selected column of each of the three child windows
int ChildWindowFunctionsCurrentSortColumn = 6;
int ChildWindowParentFunctionsCurrentSortColumn = 2;
int ChildWindowChildrenFunctionsCurrentSortColumn = 6;

HWND hChildWindowCurrentlySorting;  // which of the three child windows is currently being sorted

int ListViewRowSelectedFunctions = -1;
int ListViewRowSelectedParentFunctions = -1;
int ListViewRowSelectedChildrenFunctions = -1;


DialogCallTreeRecord_t* GetListViewRecordForRow(HWND hwndFrom, unsigned int row)
{
	DialogThreadIdRecord_t* ListView_thread_record = (DialogThreadIdRecord_t*)CaptureCallTreeThreadArrayPointer[DialogListViewThreadIndex];
	assert(ListView_thread_record);

	DialogCallTreeRecord_t* ListView_record = nullptr;

	if( hwndFrom == hChildWindowFunctions )
	{
		if( row < ListView_thread_record->CallTreeArraySize )
		{
			ListView_record = (DialogCallTreeRecord_t*)ListView_thread_record->CallTreeArray[row];
			assert(ListView_record);
			return ListView_record;
		}
	}
	else if( (hwndFrom == hChildWindowParentFunctions) && (ListViewRowSelectedFunctions >= 0) )
	{
		DialogCallTreeRecord_t* ListView_CallTreeRecord = (DialogCallTreeRecord_t*)ListView_thread_record->CallTreeArray[ListViewRowSelectedFunctions];
		assert(ListView_CallTreeRecord);

		if( row < ListView_CallTreeRecord->ParentArraySize )
		{
			ListView_record = (DialogCallTreeRecord_t*)ListView_CallTreeRecord->ParentArray[row];
			assert(ListView_record);
			return ListView_record;
		}
	}
	else if( (hwndFrom == hChildWindowChildrenFunctions) && (ListViewRowSelectedFunctions >= 0) )
	{
		DialogCallTreeRecord_t* ListView_CallTreeRecord = (DialogCallTreeRecord_t*)ListView_thread_record->CallTreeArray[ListViewRowSelectedFunctions];
		assert(ListView_CallTreeRecord);

		if( row < ListView_CallTreeRecord->ChildrenArraySize )
		{
			ListView_record = (DialogCallTreeRecord_t*)ListView_CallTreeRecord->ChildrenArray[row];
			assert(ListView_record);
			return ListView_record;
		}
	}

	return nullptr;
}

void WINAPI ListViewNotifyQsortThread(LPVOID lpData)
{
	qsort(gListView_ThreadIdRecordForQsort->CallTreeArray, gListView_ThreadIdRecordForQsort->CallTreeArraySize, sizeof(void*), ListView_SortCallTree);

	extern HWND ghPleaseWaitModalDialogWnd;
	if( ghPleaseWaitModalDialogWnd )
	{
		PostMessage(ghPleaseWaitModalDialogWnd, WM_PLEASEWAITDONE, 0, 0);
	}
}

void ListViewNotify(HWND hWnd, LPARAM lParam)
{
	LPNMHDR  lpnmh = (LPNMHDR) lParam;
	TCHAR Buffer[256];  // ListView controls can only display 259 characters per column (http://support.microsoft.com/kb/321104)
	size_t buffer_len = _countof(Buffer);

	if( DialogListViewThreadIndex == -1 )
	{
		return;
	}

	if( lpnmh->code == LVN_COLUMNCLICK )
	{
		LPNMLISTVIEW  lpnmlv = (LPNMLISTVIEW) lParam;

		int column = lpnmlv->iSubItem;
		bool bDidSortChange = false;

		if( lpnmlv->hdr.hwndFrom == hChildWindowFunctions )
		{
			if( column > 0 )  // column 0 ("#") can't be sorted
			{
				ChildWindowFunctionsCurrentSortColumn = column;

				if( ChildWindowFunctionsDefaults[column].ColumnSortType == SORT_Increasing )
				{
					ChildWindowFunctionsDefaults[column].ColumnSortType = SORT_Decreasing;
				}
				else
				{
					ChildWindowFunctionsDefaults[column].ColumnSortType = SORT_Increasing;
				}

				for( int i = 0; i <= 8; i++ )
				{
					ListViewSetColumnSortDirection(lpnmlv->hdr.hwndFrom, i, SORT_Unused);
				}

				ListViewSetColumnSortDirection(lpnmlv->hdr.hwndFrom, column, ChildWindowFunctionsDefaults[column].ColumnSortType);

				bDidSortChange = true;
			}
		}
		else if( lpnmlv->hdr.hwndFrom == hChildWindowParentFunctions )
		{
			if( column > 0 )  // column 0 ("#") can't be sorted
			{
				ChildWindowParentFunctionsCurrentSortColumn = column;

				if( ChildWindowParentFunctionsDefaults[column].ColumnSortType == SORT_Increasing )
				{
					ChildWindowParentFunctionsDefaults[column].ColumnSortType = SORT_Decreasing;
				}
				else
				{
					ChildWindowParentFunctionsDefaults[column].ColumnSortType = SORT_Increasing;
				}

				for( int i = 0; i <= 2; i++ )
				{
					ListViewSetColumnSortDirection(lpnmlv->hdr.hwndFrom, i, SORT_Unused);
				}

				ListViewSetColumnSortDirection(lpnmlv->hdr.hwndFrom, column, ChildWindowParentFunctionsDefaults[column].ColumnSortType);

				bDidSortChange = true;
			}
		}
		else if( lpnmlv->hdr.hwndFrom == hChildWindowChildrenFunctions )
		{
			if( column > 0 )  // column 0 ("#") can't be sorted
			{
				ChildWindowChildrenFunctionsCurrentSortColumn = column;

				if( ChildWindowChildrenFunctionsDefaults[column].ColumnSortType == SORT_Increasing )
				{
					ChildWindowChildrenFunctionsDefaults[column].ColumnSortType = SORT_Decreasing;
				}
				else
				{
					ChildWindowChildrenFunctionsDefaults[column].ColumnSortType = SORT_Increasing;
				}

				for( int i = 0; i <= 8; i++ )
				{
					ListViewSetColumnSortDirection(lpnmlv->hdr.hwndFrom, i, SORT_Unused);
				}

				ListViewSetColumnSortDirection(lpnmlv->hdr.hwndFrom, column, ChildWindowChildrenFunctionsDefaults[column].ColumnSortType);

				bDidSortChange = true;
			}
		}

		if( bDidSortChange && CaptureCallTreeThreadArrayPointer )
		{
			DialogThreadIdRecord_t* ListView_ThreadIdRecord = (DialogThreadIdRecord_t*)CaptureCallTreeThreadArrayPointer[DialogListViewThreadIndex];
			assert(ListView_ThreadIdRecord);

			hChildWindowCurrentlySorting = lpnmlv->hdr.hwndFrom;

			if( hChildWindowCurrentlySorting == hChildWindowFunctions )
			{
				const void* RowCallTreeAddress = nullptr;

				if( ListViewRowSelectedFunctions != -1 )
				{
					DialogCallTreeRecord_t* ListView_CallTreeRecord = (DialogCallTreeRecord_t*)ListView_ThreadIdRecord->CallTreeArray[ListViewRowSelectedFunctions];
					assert(ListView_CallTreeRecord);
					RowCallTreeAddress = ListView_CallTreeRecord->Address;
				}

				gListView_ThreadIdRecordForQsort = ListView_ThreadIdRecord;
				gRowCallTreeAddressForQsort = RowCallTreeAddress;
				ghWndFromforQsort = lpnmlv->hdr.hwndFrom;

				extern ePleaseWaitType PleaseWaitType;
				PleaseWaitType = PleaseWait_ListViewNotifySort;

				extern HWND ghPleaseWaitNotifyWnd;
				ghPleaseWaitNotifyWnd = hWnd;

				extern HINSTANCE hInst;
				DialogBox(hInst, MAKEINTRESOURCE(IDD_PLEASEWAIT), hWnd, PleaseWaitModalDialog);
			}
			else if( hChildWindowCurrentlySorting == hChildWindowParentFunctions )
			{
				DialogCallTreeRecord_t* ListView_CallTreeRecord = (DialogCallTreeRecord_t*)ListView_ThreadIdRecord->CallTreeArray[ListViewRowSelectedFunctions];
				assert(ListView_CallTreeRecord);

				const void* RowCallTreeAddress = nullptr;

				if( ListViewRowSelectedParentFunctions != -1 )
				{
					DialogCallTreeRecord_t* ListView_CallTreeRecordParent = (DialogCallTreeRecord_t*)ListView_CallTreeRecord->ParentArray[ListViewRowSelectedParentFunctions];
					assert(ListView_CallTreeRecordParent);
					RowCallTreeAddress = ListView_CallTreeRecordParent->Address;
				}

				qsort(ListView_CallTreeRecord->ParentArray, ListView_CallTreeRecord->ParentArraySize, sizeof(void*), ListView_SortCallTree);

				if( RowCallTreeAddress )
				{
					ListViewRowSelectedParentFunctions = FindRowForAddress(hChildWindowParentFunctions, RowCallTreeAddress);

					if( ListViewRowSelectedParentFunctions != -1 )
					{
						ListView_SetItemState(hChildWindowParentFunctions, ListViewRowSelectedParentFunctions, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
						ListView_EnsureVisible(hChildWindowParentFunctions, ListViewRowSelectedParentFunctions, FALSE);
					}
				}
			}
			else
			{
				DialogCallTreeRecord_t* ListView_CallTreeRecord = (DialogCallTreeRecord_t*)ListView_ThreadIdRecord->CallTreeArray[ListViewRowSelectedFunctions];
				assert(ListView_CallTreeRecord);

				const void* RowCallTreeAddress = nullptr;

				if( ListViewRowSelectedChildrenFunctions != -1 )
				{
					DialogCallTreeRecord_t* ListView_CallTreeRecordChild = (DialogCallTreeRecord_t*)ListView_CallTreeRecord->ChildrenArray[ListViewRowSelectedChildrenFunctions];
					assert(ListView_CallTreeRecordChild);
					RowCallTreeAddress = ListView_CallTreeRecordChild->Address;
				}

				qsort(ListView_CallTreeRecord->ChildrenArray, ListView_CallTreeRecord->ChildrenArraySize, sizeof(void*), ListView_SortCallTree);

				if( RowCallTreeAddress )
				{
					ListViewRowSelectedChildrenFunctions = FindRowForAddress(hChildWindowChildrenFunctions, RowCallTreeAddress);

					if( ListViewRowSelectedChildrenFunctions != -1 )
					{
						ListView_SetItemState(hChildWindowChildrenFunctions, ListViewRowSelectedChildrenFunctions, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
						ListView_EnsureVisible(hChildWindowChildrenFunctions, ListViewRowSelectedChildrenFunctions, FALSE);
					}
				}
			}

			InvalidateRect(lpnmlv->hdr.hwndFrom, NULL, FALSE);
		}
	}
	else if( (lpnmh->code == NM_CLICK) || (lpnmh->code == NM_DBLCLK) )
	{
		LPNMLISTVIEW  lpnmlv = (LPNMLISTVIEW) lParam;

		int row = lpnmlv->iItem;

		DialogThreadIdRecord_t* ListView_ThreadIdRecord = (DialogThreadIdRecord_t*)CaptureCallTreeThreadArrayPointer[DialogListViewThreadIndex];
		assert(ListView_ThreadIdRecord);

		ListViewSetRowSelected(lpnmlv->hdr.hwndFrom, row, ListView_ThreadIdRecord, (lpnmh->code == NM_DBLCLK));
	}
	else if( lpnmh->code == LVN_GETDISPINFO )
	{
		LPNMLISTVIEW  lpnmlv = (LPNMLISTVIEW) lParam;
		LV_DISPINFO *lpdi = (LV_DISPINFO *)lParam;

		unsigned int row = lpdi->item.iItem;
		unsigned int column = lpdi->item.iSubItem;

		if( CaptureCallTreeThreadArrayPointer )
		{
			DialogCallTreeRecord_t* ListView_record = GetListViewRecordForRow(lpnmlv->hdr.hwndFrom, row);

			if( ListView_record )
			{
				if( (lpdi->item.mask & LVIF_TEXT) )
				{
					if( column == 0 )  // column #
					{
						int row_num = row + 1;
						swprintf(Buffer, buffer_len, TEXT("%d"), row_num);

						wcsncpy_s(lpdi->item.pszText, lpdi->item.cchTextMax, Buffer, ((row_num / 10) + 1));
					}
					else if( column == 1 )  // function name
					{
						if( ListView_record->SymbolName )
						{
							size_t len = min(buffer_len, strlen(ListView_record->SymbolName));  // ListView control can only display 259 characters per column
							size_t num_chars;

							mbstowcs_s(&num_chars, lpdi->item.pszText, lpdi->item.cchTextMax, ListView_record->SymbolName, len);
						}
					}
					else if( column == 2 )  // times called
					{
						swprintf(Buffer, buffer_len, TEXT("%d"), ListView_record->CallCount);

						wcsncpy_s(lpdi->item.pszText, lpdi->item.cchTextMax, Buffer, buffer_len);
					}
					else if( column == 3 )  // exclusive time
					{
						ConvertTicksToTime(Buffer, buffer_len, ListView_record->CallDurationExclusiveTimeSum);

						wcsncpy_s(lpdi->item.pszText, lpdi->item.cchTextMax, Buffer, buffer_len);
					}
					else if( column == 4 )  // inclusive time
					{
						ConvertTicksToTime(Buffer, buffer_len, ListView_record->CallDurationInclusiveTimeSum);

						wcsncpy_s(lpdi->item.pszText, lpdi->item.cchTextMax, Buffer, buffer_len);
					}
					else if( column == 5 )  // average exclusive time
					{
						float ExclusiveTimeAvg = (ListView_record->CallCount > 0) ? ((float)ListView_record->CallDurationExclusiveTimeSum / (float)ListView_record->CallCount) : 0.f;

						ConvertTicksToTime(Buffer, buffer_len, ExclusiveTimeAvg);

						wcsncpy_s(lpdi->item.pszText, lpdi->item.cchTextMax, Buffer, buffer_len);
					}
					else if( column == 6 )  // average inclusive time
					{
						float InclusiveTimeAvg = (ListView_record->CallCount > 0) ? ((float)ListView_record->CallDurationInclusiveTimeSum / (float)ListView_record->CallCount) : 0.f;

						ConvertTicksToTime(Buffer, buffer_len, InclusiveTimeAvg);

						wcsncpy_s(lpdi->item.pszText, lpdi->item.cchTextMax, Buffer, buffer_len);
					}
					else if( column == 7 )  // max recursion
					{
						swprintf(Buffer, buffer_len, TEXT("%d"), ListView_record->MaxRecursionLevel);

						wcsncpy_s(lpdi->item.pszText, lpdi->item.cchTextMax, Buffer, buffer_len);
					}
					else if( column == 8 )  // max call time
					{
						ConvertTicksToTime(Buffer, buffer_len, ListView_record->MaxCallDurationExclusiveTime);

						wcsncpy_s(lpdi->item.pszText, lpdi->item.cchTextMax, Buffer, buffer_len);
					}
				}
			}
		}
	}
}

void ListViewInitChildWindows()
{
	LVCOLUMN lvc;

	ListView_SetExtendedListViewStyle(hChildWindowFunctions, (ListView_GetExtendedListViewStyle(hChildWindowFunctions) | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER) & ~LVS_EX_TRACKSELECT);

	for( int i = 0; i < _countof(ChildWindowFunctionsDefaults); i++ )
	{
		ListViewColumnsDefaults& ColumnDefaults = ChildWindowFunctionsDefaults[i];

		lvc.iSubItem = i;
		lvc.pszText = ColumnDefaults.ColumnName;
		lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_FMT | LVCF_SUBITEM;
		lvc.cx = ColumnDefaults.ColumnWidth;

		if( ColumnDefaults.bLeftJustify )
		{
			lvc.fmt = LVCFMT_LEFT;
		}
		else
		{
			lvc.fmt = LVCFMT_RIGHT;
		}

		if( ColumnDefaults.bIsDefaultSortColumn)
		{
			ChildWindowFunctionsCurrentSortColumn = i;
		}

		ListView_InsertColumn(hChildWindowFunctions, i, &lvc);
		ListViewSetColumnSortDirection(hChildWindowFunctions, i, SORT_Unused);  // turn off column sort direction indicator by default
	}

	ListView_DeleteAllItems(hChildWindowFunctions);
	ListView_SetItemCount(hChildWindowFunctions, 0);

	ListViewSetColumnSortDirection(hChildWindowFunctions, ChildWindowFunctionsCurrentSortColumn, ChildWindowFunctionsDefaults[ChildWindowFunctionsCurrentSortColumn].ColumnSortType);


	ListView_SetExtendedListViewStyle(hChildWindowParentFunctions, (ListView_GetExtendedListViewStyle(hChildWindowParentFunctions) | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER) & ~LVS_EX_TRACKSELECT);

	for( int i = 0; i < _countof(ChildWindowParentFunctionsDefaults); i++ )
	{
		ListViewColumnsDefaults& ColumnDefaults = ChildWindowParentFunctionsDefaults[i];

		lvc.iSubItem = i;
		lvc.pszText = ColumnDefaults.ColumnName;
		lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_FMT | LVCF_SUBITEM;
		lvc.cx = ColumnDefaults.ColumnWidth;

		if( ColumnDefaults.bLeftJustify )
		{
			lvc.fmt = LVCFMT_LEFT;
		}
		else
		{
			lvc.fmt = LVCFMT_RIGHT;
		}

		if( ColumnDefaults.bIsDefaultSortColumn)
		{
			ChildWindowParentFunctionsCurrentSortColumn = i;
		}

		ListView_InsertColumn(hChildWindowParentFunctions, i, &lvc);
		ListViewSetColumnSortDirection(hChildWindowParentFunctions, i, SORT_Unused);  // turn off column sort direction indicator by default
	}

	ListView_DeleteAllItems(hChildWindowParentFunctions);
	ListView_SetItemCount(hChildWindowParentFunctions, 0);

	ListViewSetColumnSortDirection(hChildWindowParentFunctions, ChildWindowParentFunctionsCurrentSortColumn, ChildWindowParentFunctionsDefaults[ChildWindowParentFunctionsCurrentSortColumn].ColumnSortType);


	ListView_SetExtendedListViewStyle(hChildWindowChildrenFunctions, (ListView_GetExtendedListViewStyle(hChildWindowChildrenFunctions) | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER) & ~LVS_EX_TRACKSELECT);

	for( int i = 0; i < _countof(ChildWindowChildrenFunctionsDefaults); i++ )
	{
		ListViewColumnsDefaults& ColumnDefaults = ChildWindowChildrenFunctionsDefaults[i];

		lvc.iSubItem = i;
		lvc.pszText = ColumnDefaults.ColumnName;
		lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_FMT | LVCF_SUBITEM;
		lvc.cx = ColumnDefaults.ColumnWidth;

		if( ColumnDefaults.bLeftJustify )
		{
			lvc.fmt = LVCFMT_LEFT;
		}
		else
		{
			lvc.fmt = LVCFMT_RIGHT;
		}

		if( ColumnDefaults.bIsDefaultSortColumn)
		{
			ChildWindowChildrenFunctionsCurrentSortColumn = i;
		}

		ListView_InsertColumn(hChildWindowChildrenFunctions, i, &lvc);
		ListViewSetColumnSortDirection(hChildWindowChildrenFunctions, i, SORT_Unused);  // turn off column sort direction indicator by default
	}

	ListView_DeleteAllItems(hChildWindowChildrenFunctions);
	ListView_SetItemCount(hChildWindowChildrenFunctions, 0);

	ListViewSetColumnSortDirection(hChildWindowChildrenFunctions, ChildWindowChildrenFunctionsCurrentSortColumn, ChildWindowChildrenFunctionsDefaults[ChildWindowChildrenFunctionsCurrentSortColumn].ColumnSortType);
}

void ListViewSetFocus(HWND hWnd)
{
	SetFocus(hWnd);

	if( hWnd == hChildWindowFunctions )
	{
		if( ListViewRowSelectedFunctions != -1 )
		{
			ListView_SetItemState(hChildWindowFunctions, ListViewRowSelectedFunctions, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
			ListView_EnsureVisible(hChildWindowFunctions, ListViewRowSelectedFunctions, FALSE);
		}
	}
	else if( hWnd == hChildWindowParentFunctions )
	{
		if( ListViewRowSelectedParentFunctions != -1 )
		{
			ListView_SetItemState(hChildWindowParentFunctions, ListViewRowSelectedParentFunctions, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
			ListView_EnsureVisible(hChildWindowParentFunctions, ListViewRowSelectedParentFunctions, FALSE);
		}
	}
	else if( hWnd == hChildWindowChildrenFunctions )
	{
		if( ListViewRowSelectedChildrenFunctions != -1 )
		{
			ListView_SetItemState(hChildWindowChildrenFunctions, ListViewRowSelectedChildrenFunctions, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
			ListView_EnsureVisible(hChildWindowChildrenFunctions, ListViewRowSelectedChildrenFunctions, FALSE);
		}
	}
}

void ListViewSetColumnSortDirection(HWND hWnd, int column, SortType sort_direction)
{
	if( column < 0 )
	{
		return;
	}

	HWND hheader = ListView_GetHeader(hWnd);

	HDITEM item;
	memset(&item, sizeof(item), 0);

	item.mask = HDI_FORMAT;

	if( sort_direction == SORT_Unused )
	{
		item.fmt = HDF_STRING;
	}
	else if( sort_direction == SORT_Increasing )
	{
		item.fmt = HDF_STRING | HDF_SORTUP;
	}
	else
	{
		item.fmt = HDF_STRING | HDF_SORTDOWN;
	}

	if( hWnd == hChildWindowFunctions )
	{
		ChildWindowFunctionsDefaults[column].bLeftJustify ? (item.fmt |= HDF_LEFT) : (item.fmt |= HDF_RIGHT);
	}
	else if( hWnd == hChildWindowParentFunctions )
	{
		ChildWindowParentFunctionsDefaults[column].bLeftJustify ? (item.fmt |= HDF_LEFT) : (item.fmt |= HDF_RIGHT);
	}
	else if( hWnd == hChildWindowChildrenFunctions )
	{
		ChildWindowChildrenFunctionsDefaults[column].bLeftJustify ? (item.fmt |= HDF_LEFT) : (item.fmt |= HDF_RIGHT);
	}

	Header_SetItem(hheader, column, &item);
}

int FindRowForAddress(HWND hWnd, const void* Address)
{
	if( DialogListViewThreadIndex == -1 )
	{
		return -1;
	}

	DialogThreadIdRecord_t* ListView_ThreadIdRecord = (DialogThreadIdRecord_t*)CaptureCallTreeThreadArrayPointer[DialogListViewThreadIndex];
	assert(ListView_ThreadIdRecord);

	if( hWnd == hChildWindowFunctions )
	{
		for( unsigned int index = 0; index < ListView_ThreadIdRecord->CallTreeArraySize; index++ )
		{
			DialogCallTreeRecord_t* ListView_CallTreeRecord = (DialogCallTreeRecord_t*)ListView_ThreadIdRecord->CallTreeArray[index];
			assert(ListView_CallTreeRecord);

			if( ListView_CallTreeRecord->Address == Address )
			{
				return index;
			}
		}
	}
	else if( hWnd == hChildWindowParentFunctions )
	{
		DialogCallTreeRecord_t* ListView_CallTreeRecord = (DialogCallTreeRecord_t*)ListView_ThreadIdRecord->CallTreeArray[ListViewRowSelectedFunctions];
		assert(ListView_CallTreeRecord);

		for( unsigned int index = 0; index < ListView_CallTreeRecord->ParentArraySize; index++ )
		{
			DialogCallTreeRecord_t* ListView_CallTreeRecordParent = (DialogCallTreeRecord_t*)ListView_CallTreeRecord->ParentArray[index];
			assert(ListView_CallTreeRecordParent);

			if( ListView_CallTreeRecordParent->Address == Address )
			{
				return index;
			}
		}
	}
	else if(hWnd == hChildWindowChildrenFunctions )
	{
		DialogCallTreeRecord_t* ListView_CallTreeRecord = (DialogCallTreeRecord_t*)ListView_ThreadIdRecord->CallTreeArray[ListViewRowSelectedFunctions];
		assert(ListView_CallTreeRecord);

		for( unsigned int index = 0; index < ListView_CallTreeRecord->ChildrenArraySize; index++ )
		{
			DialogCallTreeRecord_t* ListView_CallTreeRecordChild = (DialogCallTreeRecord_t*)ListView_CallTreeRecord->ChildrenArray[index];
			assert(ListView_CallTreeRecordChild);

			if( ListView_CallTreeRecordChild->Address == Address )
			{
				return index;
			}
		}
	}

	return -1;
}

void ListViewSetRowSelected(HWND hWnd, int row, DialogThreadIdRecord_t* ListView_ThreadIdRecord, bool bIsDoubleClick)
{
	if( DialogListViewThreadIndex == -1 )
	{
		return;
	}

	if( CaptureCallTreeThreadArrayPointer && (ListView_ThreadIdRecord->CallTreeArraySize > 0) )
	{
		if( row >= 0 )
		{
			if( hWnd == hChildWindowFunctions )
			{
				if( (unsigned int)DialogListViewThreadIndex < CaptureCallTreeThreadArraySize )
				{
					DialogCallTreeRecord_t* ListView_CallTreeRecord = (DialogCallTreeRecord_t*)ListView_ThreadIdRecord->CallTreeArray[row];
					assert(ListView_CallTreeRecord);

					if( ListView_CallTreeRecord )
					{
						int LineNumber;
						char FileName[MAX_PATH];
						FileName[0] = 0;

						if( ListView_CallTreeRecord->SourceFileIndex >= 0 )
						{
							std::map<DWORD, std::vector<std::string>>::iterator it = ThreadFileListMap.find(ListView_ThreadIdRecord->ThreadId);
							std::vector<std::string>&FileList = it->second;

							LineNumber = ListView_CallTreeRecord->SourceFileLineNumber;
							strcpy_s(FileName, FileList[ListView_CallTreeRecord->SourceFileIndex].c_str());
						}
						else
						{
							// NOTE: LineNumber will be the source code line number right after the symbol name (typically this will be the opening brace of a function)
							GetSourceCodeLineFromAddress((DWORD64)ListView_CallTreeRecord->Address, LineNumber, FileName, MAX_PATH);
						}

						if( FileName[0] == 0 )  // do we not have a valid source code file?
						{
							// clear the source code text window
							int text_length = GetWindowTextLength(hChildWindowTextViewer);
							SendMessage(hChildWindowTextViewer, EM_SETSEL, 0, text_length);
							SendMessage(hChildWindowTextViewer, EM_SETLIMITTEXT, 0, 0);
							SendMessage(hChildWindowTextViewer, EM_REPLACESEL, 0, (LPARAM)NULL);
						}
						else
						{
							extern char TextViewerFileName[];
							extern TCHAR* TextViewerBuffer;
							extern int TextViewBuffer_TotalSize;

							SetFocus(hChildWindowTextViewer);  // set focus to text window to show the caret

							if( _stricmp(FileName, TextViewerFileName) != 0 )
							{
								LoadTextFile(FileName);

								int text_length = GetWindowTextLength(hChildWindowTextViewer);
								SendMessage(hChildWindowTextViewer, EM_SETSEL, 0, text_length);

								SendMessage(hChildWindowTextViewer, EM_SETLIMITTEXT, TextViewBuffer_TotalSize, 0);

								SendMessage(hChildWindowTextViewer, EM_REPLACESEL, 0, (LPARAM)TextViewerBuffer);  // replace the Edit control text with the source code file text
							}

							extern int TextWindowFontHeight;

							int char_index = (int)SendMessage(hChildWindowTextViewer, EM_LINEINDEX, LineNumber - 1, 0);  // minus one because the EDIT window line numbers are zero-based
							if( char_index >= 0 )
							{
								SendMessage(hChildWindowTextViewer, EM_SETSEL, char_index, char_index);
							}
							else
							{
								SendMessage(hChildWindowTextViewer, EM_SETSEL, 0, 0);
							}

							SendMessage(hChildWindowTextViewer, EM_SCROLLCARET, 0, 0);

							// get the height of the font
							HDC hdc = GetDC(hChildWindowTextViewer);

							TEXTMETRIC tm;
							GetTextMetrics(hdc, &tm);

							ReleaseDC(hChildWindowTextViewer, hdc);

							// determine where this line is displayed within the window
							LPARAM pos = SendMessage(hChildWindowTextViewer, EM_POSFROMCHAR, char_index, 0);
							int y_pos = HIWORD(pos) - 1;  // make zero relative

							// get the height of the text client window
							RECT rect;
							SendMessage(hChildWindowTextViewer, EM_GETRECT, 0, (LPARAM)&rect);
							int client_height = rect.bottom - rect.top;

							int client_num_lines = client_height / tm.tmAscent;  // why is this tmAscent and not tmHeight + tmExternalLeading?

							// we want the symbol to be on line number 3
							if( client_num_lines > 3 )
							{
								int lines_to_scroll = (y_pos / tm.tmAscent) - 3;

								if( lines_to_scroll < 0 )
								{
									for( int x = 0; x > lines_to_scroll; x-- )
									{
										SendMessage(hChildWindowTextViewer, EM_SCROLL, SB_LINEUP, 0);
									}
								}
								else if( lines_to_scroll > 0 )
								{
									for( int x = 0; x < lines_to_scroll; x++ )
									{
										SendMessage(hChildWindowTextViewer, EM_SCROLL, SB_LINEDOWN, 0);
									}
								}
							}
						}

						SetFocus(hChildWindowFunctions);  // set focus back to ListView window

						ListViewRowSelectedFunctions = row;  // remember which row was selected

						ListView_SetItemState(hChildWindowFunctions, ListViewRowSelectedFunctions, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
						ListView_EnsureVisible(hChildWindowFunctions, ListViewRowSelectedFunctions, FALSE);

						// update the parent ListView for the newly selected function...
						ListView_DeleteAllItems(hChildWindowParentFunctions);
						ListView_SetItemCount(hChildWindowParentFunctions, 0);

						// sort the parent records...
						hChildWindowCurrentlySorting = hChildWindowParentFunctions;
						qsort(ListView_CallTreeRecord->ParentArray, ListView_CallTreeRecord->ParentArraySize, sizeof(void*), ListView_SortCallTree);

						ListView_SetItemCount(hChildWindowParentFunctions, ListView_CallTreeRecord->ParentArraySize);

						InvalidateRect(hChildWindowParentFunctions, NULL, FALSE);

						// ...and select the topmost item by default (for the parent functions window)
						ListViewSetRowSelected(hChildWindowParentFunctions, 0, ListView_ThreadIdRecord, false);

						// update the children ListView for the newly selected function...
						ListView_DeleteAllItems(hChildWindowChildrenFunctions);
						ListView_SetItemCount(hChildWindowChildrenFunctions, 0);

						// sort the child records...
						hChildWindowCurrentlySorting = hChildWindowChildrenFunctions;
						qsort(ListView_CallTreeRecord->ChildrenArray, ListView_CallTreeRecord->ChildrenArraySize, sizeof(void*), ListView_SortCallTree);

						ListView_SetItemCount(hChildWindowChildrenFunctions, ListView_CallTreeRecord->ChildrenArraySize);

						InvalidateRect(hChildWindowChildrenFunctions, NULL, FALSE);

						// ...and select the topmost item by default  (for the children functions window)
						ListViewSetRowSelected(hChildWindowChildrenFunctions, 0, ListView_ThreadIdRecord, false);
					}
				}
			}
			else if( hWnd == hChildWindowParentFunctions )
			{
				ListViewRowSelectedParentFunctions = row;  // remember which row was selected

				if( ListViewRowSelectedParentFunctions != -1 )
				{
					if( bIsDoubleClick )
					{
						DialogThreadIdRecord_t* ListView_ThreadIdRecord = (DialogThreadIdRecord_t*)CaptureCallTreeThreadArrayPointer[DialogListViewThreadIndex];
						assert(ListView_ThreadIdRecord);

						DialogCallTreeRecord_t* ListView_CallTreeRecord = (DialogCallTreeRecord_t*)ListView_ThreadIdRecord->CallTreeArray[ListViewRowSelectedFunctions];
						assert(ListView_CallTreeRecord);

						DialogCallTreeRecord_t* ListView_CallTreeRecordParent = (DialogCallTreeRecord_t*)ListView_CallTreeRecord->ParentArray[ListViewRowSelectedParentFunctions];
						assert(ListView_CallTreeRecordParent);
						const void* RowCallTreeAddress = ListView_CallTreeRecordParent->Address;

						if( RowCallTreeAddress )
						{
							int ChildWindowMiddleRowToSelect = FindRowForAddress(hChildWindowFunctions, RowCallTreeAddress);

							ListViewSetRowSelected(hChildWindowFunctions, ChildWindowMiddleRowToSelect, ListView_ThreadIdRecord, false);

							ListViewSetFocus(hChildWindowFunctions);
						}
					}
					else
					{
						ListView_SetItemState(hChildWindowParentFunctions, ListViewRowSelectedParentFunctions, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
						ListView_EnsureVisible(hChildWindowParentFunctions, ListViewRowSelectedParentFunctions, FALSE);
					}
				}
			}
			else if( hWnd == hChildWindowChildrenFunctions )
			{
				ListViewRowSelectedChildrenFunctions = row;  // remember which row was selected

				if( ListViewRowSelectedChildrenFunctions != -1 )
				{
					if( bIsDoubleClick )
					{
						DialogThreadIdRecord_t* ListView_ThreadIdRecord = (DialogThreadIdRecord_t*)CaptureCallTreeThreadArrayPointer[DialogListViewThreadIndex];
						assert(ListView_ThreadIdRecord);

						DialogCallTreeRecord_t* ListView_CallTreeRecord = (DialogCallTreeRecord_t*)ListView_ThreadIdRecord->CallTreeArray[ListViewRowSelectedFunctions];
						assert(ListView_CallTreeRecord);

						DialogCallTreeRecord_t* ListView_CallTreeRecordChild = (DialogCallTreeRecord_t*)ListView_CallTreeRecord->ChildrenArray[ListViewRowSelectedChildrenFunctions];
						assert(ListView_CallTreeRecordChild);
						const void* RowCallTreeAddress = ListView_CallTreeRecordChild->Address;

						if( RowCallTreeAddress )
						{
							int ChildWindowMiddleRowToSelect = FindRowForAddress(hChildWindowFunctions, RowCallTreeAddress);

							ListViewSetRowSelected(hChildWindowFunctions, ChildWindowMiddleRowToSelect, ListView_ThreadIdRecord, false);

							ListViewSetFocus(hChildWindowFunctions);
						}
					}
					else
					{
						ListView_SetItemState(hChildWindowChildrenFunctions, ListViewRowSelectedChildrenFunctions, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
						ListView_EnsureVisible(hChildWindowChildrenFunctions, ListViewRowSelectedChildrenFunctions, FALSE);
					}
				}
			}
		}
	}
	else
	{
		ListView_DeleteAllItems(hChildWindowFunctions);
		ListView_SetItemCount(hChildWindowFunctions, 0);

		ListView_DeleteAllItems(hChildWindowParentFunctions);
		ListView_SetItemCount(hChildWindowParentFunctions, 0);

		ListView_DeleteAllItems(hChildWindowChildrenFunctions);
		ListView_SetItemCount(hChildWindowChildrenFunctions, 0);
	}
}

int ListView_SortCallTree(const void* arg1, const void* arg2)
{
	SortType sort_type;
	int sort_column;

	if( (arg1 == nullptr) || (arg2 == nullptr) )
	{
		assert(false);
		return 0;
	}

	DialogCallTreeRecord_t* CallTreeRec1 = *(DialogCallTreeRecord_t**)arg1;
	DialogCallTreeRecord_t* CallTreeRec2 = *(DialogCallTreeRecord_t**)arg2;

	if( hChildWindowCurrentlySorting == hChildWindowFunctions )
	{
		sort_type = ChildWindowFunctionsDefaults[ChildWindowFunctionsCurrentSortColumn].ColumnSortType;
		sort_column = ChildWindowFunctionsCurrentSortColumn;
	}
	else if( hChildWindowCurrentlySorting == hChildWindowParentFunctions )
	{
		sort_type = ChildWindowParentFunctionsDefaults[ChildWindowParentFunctionsCurrentSortColumn].ColumnSortType;
		sort_column = ChildWindowParentFunctionsCurrentSortColumn;
	}
	else if( hChildWindowCurrentlySorting == hChildWindowChildrenFunctions )
	{
		sort_type = ChildWindowChildrenFunctionsDefaults[ChildWindowChildrenFunctionsCurrentSortColumn].ColumnSortType;
		sort_column = ChildWindowChildrenFunctionsCurrentSortColumn;
	}

	if( sort_column == 1 )  // sort by SymbolName
	{
		if( (CallTreeRec1->SymbolName == nullptr) || (CallTreeRec2->SymbolName == nullptr) )
		{
			return 0;
		}

		if( sort_type == SORT_Increasing )
		{
			return strcmp(CallTreeRec1->SymbolName, CallTreeRec2->SymbolName);  // by SymbolName
		}

		return strcmp(CallTreeRec2->SymbolName, CallTreeRec1->SymbolName);  // by SymbolName
	}
	else if( sort_column == 2 )  // sort by Times Called
	{
		if( sort_type == SORT_Increasing )
		{
			return (CallTreeRec1->CallCount - CallTreeRec2->CallCount);
		}

		return (CallTreeRec2->CallCount - CallTreeRec1->CallCount);
	}
	else if( sort_column == 3 )  // sort by Exclusive Time
	{
		if( CallTreeRec1->CallDurationExclusiveTimeSum == CallTreeRec2->CallDurationExclusiveTimeSum )
		{
			return 0;
		}
		else if( sort_type == SORT_Increasing )
		{
			return (CallTreeRec1->CallDurationExclusiveTimeSum < CallTreeRec2->CallDurationExclusiveTimeSum) ? -1 : 1;
		}

		return (CallTreeRec1->CallDurationExclusiveTimeSum > CallTreeRec2->CallDurationExclusiveTimeSum) ? -1 : 1;
	}
	else if( sort_column == 4 )  // sort by Inclusive Time
	{
		if( CallTreeRec1->CallDurationInclusiveTimeSum == CallTreeRec2->CallDurationInclusiveTimeSum )
		{
			return 0;
		}
		else if( sort_type == SORT_Increasing )
		{
			return (CallTreeRec1->CallDurationInclusiveTimeSum < CallTreeRec2->CallDurationInclusiveTimeSum) ? -1 : 1;
		}

		return (CallTreeRec1->CallDurationInclusiveTimeSum > CallTreeRec2->CallDurationInclusiveTimeSum) ? -1 : 1;
	}
	else if( sort_column == 5 )  // sort by Average Exclusive Time
	{
		float ExclusiveTimeAvgRec1 = (CallTreeRec1->CallCount > 0) ? ((float)CallTreeRec1->CallDurationExclusiveTimeSum / (float)CallTreeRec1->CallCount) : 0.f;
		float ExclusiveTimeAvgRec2 = (CallTreeRec2->CallCount > 0) ? ((float)CallTreeRec2->CallDurationExclusiveTimeSum / (float)CallTreeRec2->CallCount) : 0.f;

		if( sort_type == SORT_Increasing )
		{
			return (ExclusiveTimeAvgRec1 < ExclusiveTimeAvgRec2) ? -1 : 1;
		}

		return (ExclusiveTimeAvgRec1 > ExclusiveTimeAvgRec2) ? -1 : 1;
	}
	else if( sort_column == 6 )  // sort by Average Inclusive Time
	{
		float InclusiveTimeAvgRec1 = (CallTreeRec1->CallCount > 0) ? ((float)CallTreeRec1->CallDurationInclusiveTimeSum  / (float)CallTreeRec1->CallCount) : 0.f;
		float InclusiveTimeAvgRec2 = (CallTreeRec2->CallCount > 0) ? ((float)CallTreeRec2->CallDurationInclusiveTimeSum  / (float)CallTreeRec2->CallCount) : 0.f;

		if( sort_type == SORT_Increasing )
		{
			return (InclusiveTimeAvgRec1 < InclusiveTimeAvgRec2) ? -1 : 1;
		}

		return (InclusiveTimeAvgRec1 > InclusiveTimeAvgRec2) ? -1 : 1;
	}
	else if( sort_column == 7 )  // sort by Max Recursion
	{
		if( sort_type == SORT_Increasing )
		{
			return (CallTreeRec1->MaxRecursionLevel - CallTreeRec2->MaxRecursionLevel);
		}

		return (CallTreeRec2->MaxRecursionLevel - CallTreeRec1->MaxRecursionLevel);
	}
	else if( sort_column == 8 )  // sort by Max Call Time
	{
		if( CallTreeRec1->MaxCallDurationExclusiveTime == CallTreeRec2->MaxCallDurationExclusiveTime )
		{
			return 0;
		}
		if( sort_type == SORT_Increasing )
		{
			return (CallTreeRec1->MaxCallDurationExclusiveTime < CallTreeRec2->MaxCallDurationExclusiveTime) ? -1 : 1;
		}

		return (CallTreeRec1->MaxCallDurationExclusiveTime > CallTreeRec2->MaxCallDurationExclusiveTime) ? -1 : 1;
	}

	return 0;  // unknown sort type
}
