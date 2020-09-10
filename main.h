#pragma once

typedef struct 
{
	char *FileEnding;
	char *PathToProgram;
	b32 IsConsoleApplication;
} file_type_config;

typedef enum 
{
	// NOTE(Felix): Magic ANSI constants
	// http://ascii-table.com/ansi-escape-sequences.php
	COLOR_DEFAULT_BACKGROUND              = 40,
	COLOR_DEFAULT_FOREGROUND              = 37,

	COLOR_UNSELECTED_BACKGROUND           = 40,
	COLOR_UNSELECTED_FOREGROUND_FILE      = 37,
	COLOR_UNSELECTED_FOREGROUND_DIRECTORY = 34,

	COLOR_SELECTED_BACKGROUND_FILE        = 47,
	COLOR_SELECTED_BACKGROUND_DIRECTORY   = 44,
	COLOR_SELECTED_FOREGROUND             = 30,
} ansi_color_code;

typedef struct 
{
	ansi_color_code Background;
	ansi_color_code Foreground;
} color;

typedef struct
{
	char Name[256];
	i32 NameLength;
	//u64 Size;
	enum { 
		ENTRY_TYPE_DIRECTORY,
		ENTRY_TYPE_FILE,
		ENTRY_TYPE_UNKNOWN, 
	} Type;
} internal_directory_entry;
