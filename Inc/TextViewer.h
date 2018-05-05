//
// Copyright (c) 2015-2018 Jeffrey "botman" Broome
//

#pragma once

struct TextLineNode  // linked list pointing to the start of each line
{
	char* text;
	TextLineNode* next;
};

struct TextLineBuffer
{
	TextLineNode** linenode;  // array of TextLineNode pointers
	int num_lines;  // number of text lines in the buffer
	int max_line_length;  // length of the longest line (with tabs replaced by 4 spaces), so we know how wide to make the horizontal scroll bar
	int current_line_index;  // the line that should be displayed at the center of the window
};

void LoadTextFile(char* filename);
void InitializeTextLineBuffer(char* buffer, int length);

