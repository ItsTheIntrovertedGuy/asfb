#define _DEFAULT_SOURCE

#include <unistd.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <termios.h>

#include "language_layer.h"

// NOTE(Felix): Resources:
// "execl":    To start a program to edit the file
// "dirent.h": Directory stuff
// "getcwd":   Get full path to working directory
// "chdir":    Change current working directory

// TODO(Felix): Maybe pull these console ANSI functions out into its include file

typedef struct {
	char Name[256];
	i32 NameLength;
	//u64 Size;
	enum { 
		ENTRY_TYPE_DIRECTORY,
		ENTRY_TYPE_FILE,
		ENTRY_TYPE_UNKNOWN, 
	} Type;
} internal_directory_entry;


internal void
ScreenClear(void)
{
	fprintf(stderr, "\033[2J");
}

internal void
ClearCurrentLine(void)
{
	fprintf(stderr, "\033[2K");
}

internal void
EchoDisable(void)
{
	struct termios TerminalSettings = { 0 };
	tcgetattr(STDOUT_FILENO, &TerminalSettings);
	TerminalSettings.c_lflag &= (tcflag_t) ~ECHO;
	tcsetattr(STDOUT_FILENO, TCSANOW, &TerminalSettings);
}

internal void
EchoEnable(void)
{
	struct termios TerminalSettings = { 0 };
	tcgetattr(STDOUT_FILENO, &TerminalSettings);
	TerminalSettings.c_lflag |= (tcflag_t) ECHO;
	tcsetattr(STDOUT_FILENO, TCSANOW, &TerminalSettings);
}

internal void
CursorHide(void)
{
	fprintf(stderr, "\033[?25l");
}

internal void
CursorShow(void)
{
	fprintf(stderr, "\033[?25h");
}

internal void
CursorMove(i32 Y, i32 X)
{
	// NOTE(Felix): This function is zero indexed, ANSI escape sequences apparently aren't though
	char Buffer[20] = { 0 };
	i32 CharactersToWrite = snprintf(Buffer, sizeof(Buffer), "\033[%d;%dH", Y+1, X+1);
	write(STDOUT_FILENO, Buffer, (size_t)CharactersToWrite);
}


internal void
SignalHandler(int Signal)
{
	// TODO(Felix): Cleanup
	ScreenClear();
	EchoEnable();
	CursorShow();
	CursorMove(0, 0);
	exit(-1);
}


internal void
DirectoryEntryPrint(struct dirent *DirectoryEntry)
{
	fprintf(stderr, "d_ino:    %ld\n",  DirectoryEntry->d_ino);
	fprintf(stderr, "d_off:    %ld\n",  DirectoryEntry->d_off);
	fprintf(stderr, "d_reclen: %d\n",   DirectoryEntry->d_reclen);
	fprintf(stderr, "d_type:   %d\n",   DirectoryEntry->d_type);
	fprintf(stderr, "d_name:   %s\n\n", DirectoryEntry->d_name);
}

internal b32
InternalEntryCompareName(internal_directory_entry *A, internal_directory_entry *B)
{
	// TODO(Felix): Proper sorting, this is garbage
	i32 Balance = 0;
	i32 Index = 0;
	while (Balance == 0 &&
		   A->Name[Index] != 0)
	{
		char CharacterA = CharToUpperIfIsLetter(A->Name[Index]);
		char CharacterB = CharToUpperIfIsLetter(B->Name[Index]);
		Balance = CharacterA - CharacterB;
		++Index;
	}
	return (Balance > 0);
}

internal i32
InternalEntryCompareType(internal_directory_entry *A, internal_directory_entry *B)
{
	i32 Balance = (i32)(A->Type - B->Type);
	return (Balance > 0);
}

internal void
InternalEntryListSort(internal_directory_entry *EntryList, i32 EntryCount,
					  b32 (*CompareFunction)(internal_directory_entry *A, internal_directory_entry *B))
{
	// TODO(Felix): Selection sort, replace with quicksort
	// (maybe generalize for include)
	
	for (i32 SortedIndex = 0; SortedIndex < EntryCount; ++SortedIndex)
	{
		i32 MinimumIndex = SortedIndex;
		for (i32 UnsortedIndex = SortedIndex+1; UnsortedIndex < EntryCount; ++UnsortedIndex)
		{
			if (CompareFunction(EntryList+MinimumIndex, EntryList+UnsortedIndex))
			{
				MinimumIndex = UnsortedIndex;
			}
		}

		// NOTE(Felix): Swap minimum of unsorted sublist to start
		if (MinimumIndex != SortedIndex)
		{
			internal_directory_entry Temp = EntryList[SortedIndex];
			EntryList[SortedIndex] = EntryList[MinimumIndex];
			EntryList[MinimumIndex] = Temp;
		}
	}
}

internal void
InternalEntryPrint(internal_directory_entry Entry)
{
	char *TypeString = 0;
	switch (Entry.Type)
	{
		case ENTRY_TYPE_UNKNOWN: {
			TypeString = "Unknown";
		} break;
		
		case ENTRY_TYPE_FILE: {
			TypeString = "File";
		} break;
		
		case ENTRY_TYPE_DIRECTORY: {
			TypeString = "Directory";
		} break;
	}

	fprintf(stderr, "Name: %s\n",   Entry.Name);
	fprintf(stderr, "Type: %s\n\n", TypeString);
}

internal internal_directory_entry
CreateInternalEntryFromDirent(struct dirent *Entry)
{
	internal_directory_entry Result = { 0 };
	MemoryCopy(&Result.Name, Entry->d_name, sizeof(Entry->d_name));
	Result.NameLength = (i32)StringLength(Result.Name);
	switch (Entry->d_type)
	{
		case DT_DIR: {
			Result.Type = ENTRY_TYPE_DIRECTORY;
		} break;

		case DT_REG: {
			Result.Type = ENTRY_TYPE_FILE;
		} break;
		
		default: {
			Assert(0);
		} break;
	}
	return (Result);
}

internal b32
DirentPassesFilter(struct dirent *Entry)
{
	// NOTE(Felix): We only want regular files and directories for now
	if (0 == 
		((Entry->d_type == DT_DIR) ||
		 (Entry->d_type == DT_REG)))
	{
		return (0);
	}
	
	// NOTE(Felix): We don't want "." and ".." directories
	if (Entry->d_name[0] == '.' && 
		((Entry->d_name[1] == 0) ||
		 (Entry->d_name[1] == '.' && Entry->d_name[2] == 0)))
	{
		return (0);
	}
	
	return (1);
}

int
main(void)
{
	// NOTE(Felix): Set signal so a CTRL-C restores console settings
	{
		struct sigaction SignalAction = { 0 };
		SignalAction.sa_handler = &SignalHandler;
		sigaction(SIGINT, &SignalAction, 0);
	}
	
	// TODO(Felix): "When the window size changes, a SIGWINCH signal is sent to the foreground process group"
	// We probably want to catch this signal and update console dimensions
	
	// NOTE(Felix): Disable buffering of input, we want to process it immediately
	{
		struct termios TerminalSettings = { 0 };
		tcgetattr(STDOUT_FILENO, &TerminalSettings);
		TerminalSettings.c_lflag &= (tcflag_t)~ICANON;
		tcsetattr(STDOUT_FILENO, TCSANOW, &TerminalSettings);
	}

	// NOTE(Felix): Get current directory and format properly
	char BufferCurrentDirectory[PATH_MAX] = { 0 };
	getcwd(BufferCurrentDirectory, sizeof(BufferCurrentDirectory));
	i32 BufferCurrentDirectoryIndex = (i32)StringLength(BufferCurrentDirectory);
	BufferCurrentDirectory[BufferCurrentDirectoryIndex] = '/';

	u32 DirectoryEntriesBufferSize = MEBIBYTES(1);
	internal_directory_entry *DirectoryEntriesBuffer = mmap(0, DirectoryEntriesBufferSize, PROT_READ|PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	u32 DirectoryEntriesBufferIndex = 0;

	// NOTE(Felix): Open directory stream
	DIR *DirectoryStream = opendir(BufferCurrentDirectory);
	
	// NOTE(Felix): Gather and store all valid entries
	struct dirent *DirectoryEntry = readdir(DirectoryStream);
	while (DirectoryEntry != 0)
	{
		if (DirentPassesFilter(DirectoryEntry))
		{
			//DirectoryEntryPrint(DirectoryEntry);
			internal_directory_entry InternalEntry = CreateInternalEntryFromDirent(DirectoryEntry);
			DirectoryEntriesBuffer[DirectoryEntriesBufferIndex] = InternalEntry;
			DirectoryEntriesBufferIndex++;
		}
		DirectoryEntry = readdir(DirectoryStream);
	}
	closedir(DirectoryStream);
	
	// NOTE(Felix): Sort 
	{
		InternalEntryListSort(DirectoryEntriesBuffer, (i32)DirectoryEntriesBufferIndex, &InternalEntryCompareType);
		
		// NOTE(Felix): Find end of directory / start of files and sort each sublist by name
		i32 EntryFilesStartIndex = 0;
		for (; DirectoryEntriesBuffer[EntryFilesStartIndex].Type != ENTRY_TYPE_FILE; ++EntryFilesStartIndex) {  }

		InternalEntryListSort(DirectoryEntriesBuffer, EntryFilesStartIndex, &InternalEntryCompareName);
		InternalEntryListSort(DirectoryEntriesBuffer+EntryFilesStartIndex, (i32)DirectoryEntriesBufferIndex-EntryFilesStartIndex, &InternalEntryCompareName);
	}
	
	// NOTE(Felix): Prepare for drawing
	EchoDisable();
	ScreenClear();
	CursorHide();
	CursorMove(0, 0);

	// NOTE(Felix): Get Console dimensions
	i32 ConsoleRows = 0;
	i32 ConsoleColumns = 0;
	{
		struct winsize ConsoleDimensions = { 0 };
		ioctl(STDOUT_FILENO, TIOCGWINSZ, &ConsoleDimensions);
		ConsoleRows = ConsoleDimensions.ws_row;
		ConsoleColumns = ConsoleDimensions.ws_col;
	}
	(void)ConsoleRows;
	(void)ConsoleColumns;

	// NOTE(Felix): Draw
	b32 ExitProgram = 0;
	i32 CurrentIndex = 0;
	while (0 == ExitProgram)
	{
		// NOTE(Felix): Print all valid entries
		for (u32 InternalEntryIndex = 0;
		     InternalEntryIndex < DirectoryEntriesBufferIndex;
		     ++InternalEntryIndex)
		{
			CursorMove((i32)InternalEntryIndex, 0);

			// NOTE(Felix): Highlight line
			if ((i32)InternalEntryIndex == CurrentIndex)
			{
				fprintf(stderr, "\033[30;47m");
			}
			else
			{
				fprintf(stderr, "\033[37;40m");
			}

			ClearCurrentLine();
			fprintf(stderr, "%s", DirectoryEntriesBuffer[InternalEntryIndex].Name);
		}
		
		// NOTE(Felix): Process input
		char InputCharacter = (char)getchar();
		switch (InputCharacter)
		{
			// NOTE(Felix): Move down
			case 'j': {
				CurrentIndex = MIN((i32)DirectoryEntriesBufferIndex-1, CurrentIndex+1);
			} break;
			
			// NOTE(Felix): Move Up
			case 'k': {

				CurrentIndex = MAX(0, CurrentIndex-1);
			} break;
			
			// TODO(Felix): Open file
			case 'l': {
			} break;
			
			// NOTE(Felix): Exit program
			case 'q': {
				ExitProgram = 1;
			} break;
			
			// NOTE(Felix): Unbound key
			default: {
				// noop;
			} break;
		}
	}

	// NOTE(Felix): Cleanup
	fprintf(stderr, "\033[37;40m");
	ScreenClear();
	EchoEnable();
	CursorShow();
	CursorMove(0, 0);
	munmap(DirectoryEntriesBuffer, DirectoryEntriesBufferSize);
	return (0);
}
