
#include "targetver.h"
#include "resource.h"

#define WIN32_LEAN_AND_MEAN  // Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <Windows.h>
#include <Commctrl.h>
#include <unordered_map>

#include "Dialog.h"
#include "TextViewer.h"


int DialogListViewThreadIndex = -1;  // the index of the thread currently selected from the ThreadArray (-1 means invalid thread)
int gPreviouslySelectedRow = -1;


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


class CClipboardOutput
{
public:
	CClipboardOutput() {};
	~CClipboardOutput() {};

	size_t Log(char* output, const char* format, ... )
	{
		static char buffer[4096];

		va_list args;
		va_start(args, format);
		vsnprintf_s(buffer, sizeof(buffer), sizeof(buffer)-1, format, args);

		size_t length = strlen(buffer) + 1;

		if( output )
		{
			memcpy(output, buffer, length);
		}

		va_end (args);

		return length;  // note: length includes the null terminator
	}
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

				qsort(ListView_ThreadIdRecord->CallTreeArray, ListView_ThreadIdRecord->CallTreeArraySize, sizeof(void*), ListView_SortCallTree);

				if( RowCallTreeAddress )
				{
					ListViewRowSelectedFunctions = FindRowForAddress(hChildWindowFunctions, RowCallTreeAddress);

					if( ListViewRowSelectedFunctions != -1 )
					{
						ListView_SetItemState(hChildWindowFunctions, ListViewRowSelectedFunctions, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
						ListView_EnsureVisible(hChildWindowFunctions, ListViewRowSelectedFunctions, FALSE);
					}
				}
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

		gPreviouslySelectedRow = row;  // save this so that NM_RCLICK can keep this row selected

		DialogThreadIdRecord_t* ListView_ThreadIdRecord = (DialogThreadIdRecord_t*)CaptureCallTreeThreadArrayPointer[DialogListViewThreadIndex];
		assert(ListView_ThreadIdRecord);

		ListViewSetRowSelected(lpnmlv->hdr.hwndFrom, row, ListView_ThreadIdRecord, (lpnmh->code == NM_DBLCLK));
	}
	else if( (lpnmh->code == NM_RCLICK) || (lpnmh->code == LVN_BEGINRDRAG) )
	{
		LPNMLISTVIEW  lpnmlv = (LPNMLISTVIEW) lParam;

		if( gPreviouslySelectedRow >= 0 )
		{
			SetFocus(lpnmlv->hdr.hwndFrom);

			ListView_SetItemState(lpnmlv->hdr.hwndFrom, gPreviouslySelectedRow, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
			ListView_EnsureVisible(lpnmlv->hdr.hwndFrom, gPreviouslySelectedRow, FALSE);
		}

		HMENU hPopupMenu = CreatePopupMenu();

		AppendMenu(hPopupMenu, MF_ENABLED | MF_STRING, 1, TEXT("Copy to Clipboard as Formatted Text"));
		AppendMenu(hPopupMenu, MF_ENABLED | MF_STRING, 2, TEXT("Copy to Clipboard as Comma Separated Values (CSV format)"));
		AppendMenu(hPopupMenu, MF_ENABLED | MF_STRING, 3, TEXT("Copy to Clipboard as Comma Separated Values (Text format)"));

		POINT cursor_pos;
		GetCursorPos(&cursor_pos);
		SetForegroundWindow(hWnd);

		int result = TrackPopupMenu(hPopupMenu, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RETURNCMD, cursor_pos.x, cursor_pos.y, 0, hWnd, NULL);

		switch ( result )
		{
			// To output data to the clipboard, we go through the data in two passes.  First pass determines the total length of the buffer
			// that needs to be allocated to hold all the data, then we allocate that buffer using VirtualAlloc.  In the second pass, we go
			// through the data outputting EXACTLY the same thing again except this time we output it to the buffer we just allocated.  We
			// then copy this buffer to the clipboard and free the buffer using VirtualFree.

			case 1:  // copy as formatted text
				{
					CClipboardOutput clipboard_output;

					char* txt_functions_name_header = {"Functions"};
					char* txt_parents_name_header = {"Parents"};
					char* txt_children_name_header = {"Children"};

					// NOTE: The header text here does not include the function name (which is of variable length).  It is handled separately.
					// We format time values as 14 characters wide.  This gives us 6 digits before the decimal point, the decimal, 3 digits more plus a space, 4 characters (see ConvertTicksToTime() for details)
					char* txt_header_functions = {"Times Called  Exclusive Time Sum  Inclusive Time Sum  Avg. Exclusive Time  Avg. Inclusive Time  Max Recursion  Max Exclusive Time"};
					char* txt_header_parents = {"Times Called"};
					char* txt_header_children = {"Times Called  Exclusive Time Sum  Inclusive Time Sum  Avg. Exclusive Time  Avg. Inclusive Time  Max Recursion  Max Exclusive Time"};

					int number_rows = 0;
					size_t name_header_length = 0;

					// find the length of the largest function name (so we can format all function names to the same width)

					DialogThreadIdRecord_t* ListView_thread_record = (DialogThreadIdRecord_t*)CaptureCallTreeThreadArrayPointer[DialogListViewThreadIndex];
					assert(ListView_thread_record);

					// determine the number of rows and set the length of the header text for the function category
					if( lpnmlv->hdr.hwndFrom == hChildWindowFunctions )
					{
						number_rows = ListView_thread_record->CallTreeArraySize;
						name_header_length = strlen(txt_functions_name_header);
					}
					else if( lpnmlv->hdr.hwndFrom == hChildWindowParentFunctions )
					{
						DialogCallTreeRecord_t* ListView_CallTreeRecord = (DialogCallTreeRecord_t*)ListView_thread_record->CallTreeArray[ListViewRowSelectedFunctions];
						assert(ListView_CallTreeRecord);

						number_rows =  ListView_CallTreeRecord->ParentArraySize;
						name_header_length = strlen(txt_parents_name_header);
					}
					else if( lpnmlv->hdr.hwndFrom == hChildWindowChildrenFunctions )
					{
						DialogCallTreeRecord_t* ListView_CallTreeRecord = (DialogCallTreeRecord_t*)ListView_thread_record->CallTreeArray[ListViewRowSelectedFunctions];
						assert(ListView_CallTreeRecord);

						number_rows =  ListView_CallTreeRecord->ChildrenArraySize;
						name_header_length = strlen(txt_children_name_header);
					}

					size_t max_function_name_length = name_header_length;

					// determine the max length of the function names
					for( int row = 0; row < number_rows; ++row )
					{
						DialogCallTreeRecord_t* ListView_record = GetListViewRecordForRow(lpnmlv->hdr.hwndFrom, row);

						size_t len = strlen(ListView_record->SymbolName);

						if( len > max_function_name_length )
						{
							max_function_name_length = len;
						}
					}

					size_t number_of_spaces_after_header = max_function_name_length - name_header_length;

					size_t total_length = 0;

					// calculate the length of the formatted header
					if( lpnmlv->hdr.hwndFrom == hChildWindowFunctions )
					{
						// don't count the null terminator
						total_length += clipboard_output.Log(nullptr, txt_functions_name_header) - 1;
						total_length += number_of_spaces_after_header + 2;  // we add 2 spaces after each column
						total_length += clipboard_output.Log(nullptr, txt_header_functions) - 1;
					}
					else if( lpnmlv->hdr.hwndFrom == hChildWindowParentFunctions )
					{
						// don't count the null terminator
						total_length += clipboard_output.Log(nullptr, txt_parents_name_header) - 1;
						total_length += number_of_spaces_after_header + 2;  // we add 2 spaces after each column
						total_length += clipboard_output.Log(nullptr, txt_header_parents) - 1;
					}
					else if( lpnmlv->hdr.hwndFrom == hChildWindowChildrenFunctions )
					{
						// don't count the null terminator
						total_length += clipboard_output.Log(nullptr, txt_children_name_header) - 1;
						total_length += number_of_spaces_after_header + 2;  // we add 2 spaces after each column
						total_length += clipboard_output.Log(nullptr, txt_header_children) - 1;
					}

					char FormatBuffer[16];
					int FormatBufferLen = sizeof(FormatBuffer);

					// calculate the length of each of the rows
					for( int row = 0; row < number_rows; ++row )
					{
						DialogCallTreeRecord_t* ListView_record = GetListViewRecordForRow(lpnmlv->hdr.hwndFrom, row);

						if( ListView_record )
						{
							total_length += 1 + max_function_name_length + 2;  // newline plus function name max length plus 2 spaces

							if( lpnmlv->hdr.hwndFrom == hChildWindowFunctions )
							{
								total_length += clipboard_output.Log(nullptr, "%12d  ", ListView_record->CallCount) - 1;

								ConvertTicksToTime(FormatBuffer, FormatBufferLen, ListView_record->CallDurationExclusiveTimeSum);
								total_length += clipboard_output.Log(nullptr, "%18s  ", FormatBuffer) - 1;

								ConvertTicksToTime(FormatBuffer, FormatBufferLen, ListView_record->CallDurationInclusiveTimeSum);
								total_length += clipboard_output.Log(nullptr, "%18s  ", FormatBuffer) - 1;

								float ExclusiveTimeAvg = (ListView_record->CallCount > 0) ? ((float)ListView_record->CallDurationExclusiveTimeSum / (float)ListView_record->CallCount) : 0.f;
								ConvertTicksToTime(FormatBuffer, FormatBufferLen, ExclusiveTimeAvg);
								total_length += clipboard_output.Log(nullptr, "%19s  ", FormatBuffer) - 1;

								float InclusiveTimeAvg = (ListView_record->CallCount > 0) ? ((float)ListView_record->CallDurationInclusiveTimeSum / (float)ListView_record->CallCount) : 0.f;
								ConvertTicksToTime(FormatBuffer, FormatBufferLen, ExclusiveTimeAvg);
								total_length += clipboard_output.Log(nullptr, "%19s  ", FormatBuffer) - 1;

								total_length += clipboard_output.Log(nullptr, "%13d  ", ListView_record->MaxRecursionLevel) - 1;

								ConvertTicksToTime(FormatBuffer, FormatBufferLen, ListView_record->MaxCallDurationExclusiveTime);
								total_length += clipboard_output.Log(nullptr, "%18s", FormatBuffer) - 1;  // no spaces at the end of the last field
							}
							else if( lpnmlv->hdr.hwndFrom == hChildWindowParentFunctions )
							{
								total_length += clipboard_output.Log(nullptr, "%12d", ListView_record->CallCount) - 1;  // no spaces at the end of the last field
							}
							else if( lpnmlv->hdr.hwndFrom == hChildWindowChildrenFunctions )
							{
								total_length += clipboard_output.Log(nullptr, "%12d  ", ListView_record->CallCount) - 1;

								ConvertTicksToTime(FormatBuffer, FormatBufferLen, ListView_record->CallDurationExclusiveTimeSum);
								total_length += clipboard_output.Log(nullptr, "%18s  ", FormatBuffer) - 1;

								ConvertTicksToTime(FormatBuffer, FormatBufferLen, ListView_record->CallDurationInclusiveTimeSum);
								total_length += clipboard_output.Log(nullptr, "%18s  ", FormatBuffer) - 1;

								float ExclusiveTimeAvg = (ListView_record->CallCount > 0) ? ((float)ListView_record->CallDurationExclusiveTimeSum / (float)ListView_record->CallCount) : 0.f;
								ConvertTicksToTime(FormatBuffer, FormatBufferLen, ExclusiveTimeAvg);
								total_length += clipboard_output.Log(nullptr, "%19s  ", FormatBuffer) - 1;

								float InclusiveTimeAvg = (ListView_record->CallCount > 0) ? ((float)ListView_record->CallDurationInclusiveTimeSum / (float)ListView_record->CallCount) : 0.f;
								ConvertTicksToTime(FormatBuffer, FormatBufferLen, ExclusiveTimeAvg);
								total_length += clipboard_output.Log(nullptr, "%19s  ", FormatBuffer) - 1;

								total_length += clipboard_output.Log(nullptr, "%13d  ", ListView_record->MaxRecursionLevel) - 1;

								ConvertTicksToTime(FormatBuffer, FormatBufferLen, ListView_record->MaxCallDurationExclusiveTime);
								total_length += clipboard_output.Log(nullptr, "%18s", FormatBuffer) - 1;  // no spaces at the end of the last field
							}
						}
					}

					size_t page_size = ((total_length / 4096) + 1) * 4096;  // page size is multiple of 4K

					char* AllocPtr = (char*)VirtualAlloc( NULL, page_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );

					if( AllocPtr )
					{
						char* Ptr = AllocPtr;  // Ptr is where we write each formatted string into the VirtualAlloc buffer

						// output the header text
						if( lpnmlv->hdr.hwndFrom == hChildWindowFunctions )
						{
							// don't count the null terminator
							Ptr += clipboard_output.Log(Ptr, txt_functions_name_header) - 1;
							for( size_t index = 0; index < number_of_spaces_after_header; ++index )
							{
								Ptr += clipboard_output.Log(Ptr, " ") - 1;
							}
							Ptr += clipboard_output.Log(Ptr, "  ") - 1;  // we add 2 spaces after each column
							Ptr += clipboard_output.Log(Ptr, txt_header_functions) - 1;
						}
						else if( lpnmlv->hdr.hwndFrom == hChildWindowParentFunctions )
						{
							// don't count the null terminator
							Ptr += clipboard_output.Log(Ptr, txt_parents_name_header) - 1;
							for( size_t index = 0; index < number_of_spaces_after_header; ++index )
							{
								Ptr += clipboard_output.Log(Ptr, " ") - 1;
							}
							Ptr += clipboard_output.Log(Ptr, "  ") - 1;  // we add 2 spaces after each column
							Ptr += clipboard_output.Log(Ptr, txt_header_parents) - 1;
						}
						else if( lpnmlv->hdr.hwndFrom == hChildWindowChildrenFunctions )
						{
							// don't count the null terminator
							Ptr += clipboard_output.Log(Ptr, txt_children_name_header) - 1;
							for( size_t index = 0; index < number_of_spaces_after_header; ++index )
							{
								Ptr += clipboard_output.Log(Ptr, " ") - 1;
							}
							Ptr += clipboard_output.Log(Ptr, "  ") - 1;  // we add 2 spaces after each column
							Ptr += clipboard_output.Log(Ptr, txt_header_children) - 1;
						}

						// output the data for each row
						for( int row = 0; row < number_rows; ++row )
						{
							DialogCallTreeRecord_t* ListView_record = GetListViewRecordForRow(lpnmlv->hdr.hwndFrom, row);

							if( ListView_record )
							{
								Ptr += clipboard_output.Log(Ptr, "\n%s", ListView_record->SymbolName) - 1;

								size_t spaces_to_output = max_function_name_length - strlen(ListView_record->SymbolName);

								for( size_t index = 0; index < spaces_to_output + 2; ++index )
								{
									Ptr += clipboard_output.Log(Ptr, " ") - 1;
								}

								if( lpnmlv->hdr.hwndFrom == hChildWindowFunctions )
								{
									Ptr += clipboard_output.Log(Ptr, "%12d  ", ListView_record->CallCount) - 1;

									ConvertTicksToTime(FormatBuffer, FormatBufferLen, ListView_record->CallDurationExclusiveTimeSum);
									Ptr += clipboard_output.Log(Ptr, "%18s  ", FormatBuffer) - 1;

									ConvertTicksToTime(FormatBuffer, FormatBufferLen, ListView_record->CallDurationInclusiveTimeSum);
									Ptr += clipboard_output.Log(Ptr, "%18s  ", FormatBuffer) - 1;

									float ExclusiveTimeAvg = (ListView_record->CallCount > 0) ? ((float)ListView_record->CallDurationExclusiveTimeSum / (float)ListView_record->CallCount) : 0.f;
									ConvertTicksToTime(FormatBuffer, FormatBufferLen, ExclusiveTimeAvg);
									Ptr += clipboard_output.Log(Ptr, "%19s  ", FormatBuffer) - 1;

									float InclusiveTimeAvg = (ListView_record->CallCount > 0) ? ((float)ListView_record->CallDurationInclusiveTimeSum / (float)ListView_record->CallCount) : 0.f;
									ConvertTicksToTime(FormatBuffer, FormatBufferLen, ExclusiveTimeAvg);
									Ptr += clipboard_output.Log(Ptr, "%19s  ", FormatBuffer) - 1;

									Ptr += clipboard_output.Log(Ptr, "%13d  ", ListView_record->MaxRecursionLevel) - 1;

									ConvertTicksToTime(FormatBuffer, FormatBufferLen, ListView_record->MaxCallDurationExclusiveTime);
									Ptr += clipboard_output.Log(Ptr, "%18s", FormatBuffer) - 1;  // no spaces at the end of the last field
								}
								else if( lpnmlv->hdr.hwndFrom == hChildWindowParentFunctions )
								{
									Ptr += clipboard_output.Log(Ptr, "%12d", ListView_record->CallCount) - 1;  // no spaces at the end of the last field
								}
								else if( lpnmlv->hdr.hwndFrom == hChildWindowChildrenFunctions )
								{
									Ptr += clipboard_output.Log(Ptr, "%12d  ", ListView_record->CallCount) - 1;

									ConvertTicksToTime(FormatBuffer, FormatBufferLen, ListView_record->CallDurationExclusiveTimeSum);
									Ptr += clipboard_output.Log(Ptr, "%18s  ", FormatBuffer) - 1;

									ConvertTicksToTime(FormatBuffer, FormatBufferLen, ListView_record->CallDurationInclusiveTimeSum);
									Ptr += clipboard_output.Log(Ptr, "%18s  ", FormatBuffer) - 1;

									float ExclusiveTimeAvg = (ListView_record->CallCount > 0) ? ((float)ListView_record->CallDurationExclusiveTimeSum / (float)ListView_record->CallCount) : 0.f;
									ConvertTicksToTime(FormatBuffer, FormatBufferLen, ExclusiveTimeAvg);
									Ptr += clipboard_output.Log(Ptr, "%19s  ", FormatBuffer) - 1;

									float InclusiveTimeAvg = (ListView_record->CallCount > 0) ? ((float)ListView_record->CallDurationInclusiveTimeSum / (float)ListView_record->CallCount) : 0.f;
									ConvertTicksToTime(FormatBuffer, FormatBufferLen, ExclusiveTimeAvg);
									Ptr += clipboard_output.Log(Ptr, "%19s  ", FormatBuffer) - 1;

									Ptr += clipboard_output.Log(Ptr, "%13d  ", ListView_record->MaxRecursionLevel) - 1;

									ConvertTicksToTime(FormatBuffer, FormatBufferLen, ListView_record->MaxCallDurationExclusiveTime);
									Ptr += clipboard_output.Log(Ptr, "%18s", FormatBuffer) - 1;  // no spaces at the end of the last field
								}
							}
						}

						*Ptr = 0;  // add null terminator at the end of the buffer

						HGLOBAL hMem =  GlobalAlloc(GMEM_MOVEABLE, total_length+1);  // plus one for null terminator
						memcpy(GlobalLock(hMem), AllocPtr, total_length+1);
						GlobalUnlock(hMem);

						OpenClipboard(hWnd);
						EmptyClipboard();
						SetClipboardData(CF_TEXT, hMem);
						CloseClipboard();

						VirtualFree(AllocPtr, page_size, MEM_RELEASE);
					}
				}
				break;

			case 2:  // copy as Comma Separated Values (CVS format)
			case 3:  // copy as Comma Separated Values (Text format)
				{
					CClipboardOutput clipboard_output;

					size_t total_length = 0;

					char* csv_header_functions = {"\"Function\",\"Times Called\",\"Exclusive Time Sum (usec)\",\"Inclusive Time Sum (usec)\",\"Avg. Exclusive Time (usec)\",\"Avg. Inclusive Time (usec)\",\"Max Recursion\",\"Max Exclusive Time (usec)\""};
					char* csv_header_parents = {"\"Parents\",\"Times Called\""};
					char* csv_header_children = {"\"Children\",\"Times Called\",\"Exclusive Time Sum (usec)\",\"Inclusive Time Sum (usec)\",\"Avg. Exclusive Time (usec)\",\"Avg. Inclusive Time (usec)\",\"Max Recursion\",\"Max Exclusive Time (usec)\""};

					// these are the format text for the Log() call to output the call tree record data as CSV
					char* csv_line_functions = {"\n\"%s\",%d,%I64d,%I64d,%I64d,%I64d,%d,%I64d"};
					char* csv_line_parents = {"\n\"%s\",%d"};
					char* csv_line_children = {"\n\"%s\",%d,%I64d,%I64d,%I64d,%I64d,%d,%I64d"};

					int number_rows = 0;

					DialogThreadIdRecord_t* ListView_thread_record = (DialogThreadIdRecord_t*)CaptureCallTreeThreadArrayPointer[DialogListViewThreadIndex];
					assert(ListView_thread_record);

					// calculate the length of the header and determine the number of rows of data
					if( lpnmlv->hdr.hwndFrom == hChildWindowFunctions )
					{
						total_length += clipboard_output.Log(nullptr, csv_header_functions) - 1;  // don't count the null terminator

						number_rows = ListView_thread_record->CallTreeArraySize;
					}
					else if( lpnmlv->hdr.hwndFrom == hChildWindowParentFunctions )
					{
						total_length += clipboard_output.Log(nullptr, csv_header_parents) - 1;  // don't count the null terminator

						DialogCallTreeRecord_t* ListView_CallTreeRecord = (DialogCallTreeRecord_t*)ListView_thread_record->CallTreeArray[ListViewRowSelectedFunctions];
						assert(ListView_CallTreeRecord);

						number_rows =  ListView_CallTreeRecord->ParentArraySize;
					}
					else if( lpnmlv->hdr.hwndFrom == hChildWindowChildrenFunctions )
					{
						total_length += clipboard_output.Log(nullptr, csv_header_children) - 1;  // don't count the null terminator

						DialogCallTreeRecord_t* ListView_CallTreeRecord = (DialogCallTreeRecord_t*)ListView_thread_record->CallTreeArray[ListViewRowSelectedFunctions];
						assert(ListView_CallTreeRecord);

						number_rows =  ListView_CallTreeRecord->ChildrenArraySize;
					}

					// calculate the length of each of the rows
					for( int row = 0; row < number_rows; ++row )
					{
						DialogCallTreeRecord_t* ListView_record = GetListViewRecordForRow(lpnmlv->hdr.hwndFrom, row);

						if( ListView_record )
						{
							if( lpnmlv->hdr.hwndFrom == hChildWindowFunctions )
							{
								long long ExclusiveTimeAvg = (ListView_record->CallCount > 0) ? (ListView_record->CallDurationExclusiveTimeSum / (ListView_record->CallCount * 10)) : 0;
								long long InclusiveTimeAvg = (ListView_record->CallCount > 0) ? (ListView_record->CallDurationInclusiveTimeSum / (ListView_record->CallCount * 10)) : 0;

								total_length += clipboard_output.Log(nullptr, csv_line_functions,
																		ListView_record->SymbolName,
																		ListView_record->CallCount,
																		ListView_record->CallDurationExclusiveTimeSum / 10,
																		ListView_record->CallDurationInclusiveTimeSum / 10,
																		ExclusiveTimeAvg,
																		InclusiveTimeAvg,
																		ListView_record->MaxRecursionLevel,
																		ListView_record->MaxCallDurationExclusiveTime / 10) - 1;  // don't count the null terminator
							}
							else if( lpnmlv->hdr.hwndFrom == hChildWindowParentFunctions )
							{
								total_length += clipboard_output.Log(nullptr, csv_line_parents,
																		ListView_record->SymbolName,
																		ListView_record->CallCount) - 1;  // don't count the null terminator
							}
							else if( lpnmlv->hdr.hwndFrom == hChildWindowChildrenFunctions )
							{
								long long ExclusiveTimeAvg = (ListView_record->CallCount > 0) ? (ListView_record->CallDurationExclusiveTimeSum / (ListView_record->CallCount * 10)) : 0;
								long long InclusiveTimeAvg = (ListView_record->CallCount > 0) ? (ListView_record->CallDurationInclusiveTimeSum / (ListView_record->CallCount * 10)) : 0;

								total_length += clipboard_output.Log(nullptr, csv_line_children,
																		ListView_record->SymbolName,
																		ListView_record->CallCount,
																		ListView_record->CallDurationExclusiveTimeSum / 10,
																		ListView_record->CallDurationInclusiveTimeSum / 10,
																		ExclusiveTimeAvg,
																		InclusiveTimeAvg,
																		ListView_record->MaxRecursionLevel,
																		ListView_record->MaxCallDurationExclusiveTime / 10) - 1;  // don't count the null terminator
							}
						}
					}

					size_t page_size = ((total_length / 4096) + 1) * 4096;  // page size is multiple of 4K

					char* AllocPtr = (char*)VirtualAlloc( NULL, page_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );

					if( AllocPtr )
					{
						char* Ptr = AllocPtr;  // Ptr is where we write each formatted string into the VirtualAlloc buffer

						// output the header text
						if( lpnmlv->hdr.hwndFrom == hChildWindowFunctions )
						{
							Ptr += clipboard_output.Log(Ptr, csv_header_functions) - 1;  // don't count the null terminator
						}
						else if( lpnmlv->hdr.hwndFrom == hChildWindowParentFunctions )
						{
							Ptr += clipboard_output.Log(Ptr, csv_header_parents) - 1;  // don't count the null terminator
						}
						else if( lpnmlv->hdr.hwndFrom == hChildWindowChildrenFunctions )
						{
							Ptr += clipboard_output.Log(Ptr, csv_header_children) - 1;  // don't count the null terminator
						}

						// output the data for each row
						for( int row = 0; row < number_rows; ++row )
						{
							DialogCallTreeRecord_t* ListView_record = GetListViewRecordForRow(lpnmlv->hdr.hwndFrom, row);

							if( ListView_record )
							{
								if( lpnmlv->hdr.hwndFrom == hChildWindowFunctions )
								{
									long long ExclusiveTimeAvg = (ListView_record->CallCount > 0) ? (ListView_record->CallDurationExclusiveTimeSum / (ListView_record->CallCount * 10)) : 0;
									long long InclusiveTimeAvg = (ListView_record->CallCount > 0) ? (ListView_record->CallDurationInclusiveTimeSum / (ListView_record->CallCount * 10)) : 0;

									Ptr += clipboard_output.Log(Ptr, csv_line_functions,
																			ListView_record->SymbolName,
																			ListView_record->CallCount,
																			ListView_record->CallDurationExclusiveTimeSum / 10,
																			ListView_record->CallDurationInclusiveTimeSum / 10,
																			ExclusiveTimeAvg,
																			InclusiveTimeAvg,
																			ListView_record->MaxRecursionLevel,
																			ListView_record->MaxCallDurationExclusiveTime / 10) - 1;  // don't count the null terminator
								}
								else if( lpnmlv->hdr.hwndFrom == hChildWindowParentFunctions )
								{
									Ptr += clipboard_output.Log(Ptr, csv_line_parents,
																			ListView_record->SymbolName,
																			ListView_record->CallCount) - 1;  // don't count the null terminator
								}
								else if( lpnmlv->hdr.hwndFrom == hChildWindowChildrenFunctions )
								{
									long long ExclusiveTimeAvg = (ListView_record->CallCount > 0) ? (ListView_record->CallDurationExclusiveTimeSum / (ListView_record->CallCount * 10)) : 0;
									long long InclusiveTimeAvg = (ListView_record->CallCount > 0) ? (ListView_record->CallDurationInclusiveTimeSum / (ListView_record->CallCount * 10)) : 0;

									Ptr += clipboard_output.Log(Ptr, csv_line_children,
																			ListView_record->SymbolName,
																			ListView_record->CallCount,
																			ListView_record->CallDurationExclusiveTimeSum / 10,
																			ListView_record->CallDurationInclusiveTimeSum / 10,
																			ExclusiveTimeAvg,
																			InclusiveTimeAvg,
																			ListView_record->MaxRecursionLevel,
																			ListView_record->MaxCallDurationExclusiveTime / 10) - 1;  // don't count the null terminator
								}
							}
						}

						*Ptr = 0;  // add null terminator at the end of the buffer

						HGLOBAL hMem =  GlobalAlloc(GMEM_MOVEABLE, total_length+1);  // plus one for null terminator
						memcpy(GlobalLock(hMem), AllocPtr, total_length+1);
						GlobalUnlock(hMem);

						OpenClipboard(hWnd);
						EmptyClipboard();

						if( result == 2 )
						{
							UINT format = RegisterClipboardFormat(TEXT("Csv"));
							if( format )
							{
								SetClipboardData(format, hMem);
							}
						}
						else if( result == 3 )
						{
							SetClipboardData(CF_TEXT, hMem);
						}

						CloseClipboard();

						VirtualFree(AllocPtr, page_size, MEM_RELEASE);
					}
				}
				break;
		}

		DestroyMenu(hPopupMenu);
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

						GetSourceCodeLineFromAddress((DWORD64)ListView_CallTreeRecord->Address, LineNumber, FileName, MAX_PATH);

						extern char TextViewerFileName[];
						extern TCHAR* TextViewerBuffer;
						extern int TextViewBuffer_TotalSize;
						extern std::unordered_map<int, int> LineNumberToBufferPositionMap;

						if( _stricmp(FileName, TextViewerFileName) != 0 )
						{
							LoadTextFile(FileName);

							int text_length = GetWindowTextLength(hChildWindowTextViewer);
							SendMessage(hChildWindowTextViewer, EM_SETSEL, 0, text_length);

							SendMessage(hChildWindowTextViewer, EM_SETLIMITTEXT, TextViewBuffer_TotalSize, 0);

							SendMessage(hChildWindowTextViewer, EM_REPLACESEL, 0, (LPARAM)TextViewerBuffer);  // replace the Edit control text with the source code file text
						}

						SetFocus(hChildWindowTextViewer);

						auto offset_it = LineNumberToBufferPositionMap.find(LineNumber - 3);  // offset the line number down slightly in the text window
						if( offset_it != LineNumberToBufferPositionMap.end() )
						{
							SendMessage(hChildWindowTextViewer, EM_SETSEL, offset_it->second, offset_it->second);
						}
						else
						{
							SendMessage(hChildWindowTextViewer, EM_SETSEL, 0, 0);
						}

						SendMessage(hChildWindowTextViewer, EM_SCROLLCARET, 0, 0);

						offset_it = LineNumberToBufferPositionMap.find(LineNumber - 1);  // now set the cursor to the line number of the symbol
						if( offset_it != LineNumberToBufferPositionMap.end() )
						{
							SendMessage(hChildWindowTextViewer, EM_SETSEL, offset_it->second, offset_it->second);
							SendMessage(hChildWindowTextViewer, EM_SCROLLCARET, 0, 0);
						}


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
