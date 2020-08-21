#define _DEFAULT_SOURCE

#include <unistd.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <poll.h>
#include <errno.h>

#include "language_layer.h"

// NOTE(Felix): Resources:
// "execl":    To start a program to edit the file
// "dirent.h": Directory stuff
// "getcwd":   Get full path to working directory
// "chdir":    Change current working directory

// TODO(Felix): Maybe pull these console ANSI functions out into its include file

typedef struct 
{
	char *FileEnding;
	char *PathToProgram;
	b32 IsConsoleApplication;
} file_type_config;

global_variable file_type_config GLOBALFileTypeConfig[] = {
	{ "",        "/bin/nvim", 1 }, // NOTE(Felix): Default

	{ ".pdf",    "/bin/zathura" },
	{ ".djvu",   "/bin/zathura" },

	{ ".png",    "/bin/feh" },
	{ ".jpeg",   "/bin/feh" },
	{ ".jpg",    "/bin/feh" },
	{ ".gif",    "/bin/feh" },

	{ ".mp4",    "/bin/mpv" },
};


global_variable b32 GLOBALUpdateConsoleDimensions = 0;

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

internal b32
StringEqual(char *A, char *B)
{
	i32 Index = 0;
	for (; 
		 A[Index] == B[Index] && A[Index] != 0 && B[Index] != 0;
		++Index)
	{
		// noop
	}
	
	return (A[Index] == B[Index]);
}

internal char *
GetProgramNameFromFullPath(char *FullPath)
{
	i32 LastSlashIndex = 0;
	for (i32 Index = 0; FullPath[Index] != 0; ++Index)
	{
		if (FullPath[Index] == '/')
		{
			LastSlashIndex = Index;
		}
	}
	return (FullPath+LastSlashIndex+1);
}

internal char *
GetFileType(char *FileName)
{
	i32 LastDotIndex = 0;
	for (i32 Index = 0; FileName[Index] != 0; ++Index)
	{
		if (FileName[Index] == '.')
		{
			LastDotIndex = Index;
		}
	}
	if (LastDotIndex > 0)
	{
		return (FileName+LastDotIndex);
	}
	else
	{
		return (0);
	}
}

internal file_type_config
GetProgramToUseConfig(char *FileName)
{
	char *FileType = GetFileType(FileName);
	if (FileType != 0)
	{
		for (i32 ConfigIndex = 1; ConfigIndex < (i32)ARRAYCOUNT(GLOBALFileTypeConfig); ++ConfigIndex)
		{
			if (StringEqual(FileType, GLOBALFileTypeConfig[ConfigIndex].FileEnding))
			{
				return (GLOBALFileTypeConfig[ConfigIndex]);
			}
		}
	}

	// NOTE(Felix): No match, use default software
	return (GLOBALFileTypeConfig[0]);
}

internal b32
FileIsExecutable(char *FileName)
{
	struct stat FileData = { 0 };
	stat(FileName, &FileData);
	return (FileData.st_mode & S_IXUSR);
}

internal void
ReadCurrentDirectoryNameIntoBuffer(char *BufferToReadInto, char *PathBuffer)
{
	// NOTE(Felix): Find Slashes
	i32 LastSlashIndex = 0;
	i32 SecondLastSlashIndex = 0;
	for (i32 BufferIndex = 0; PathBuffer[BufferIndex] != 0; ++BufferIndex)
	{
		if (PathBuffer[BufferIndex] == '/')
		{
			SecondLastSlashIndex = LastSlashIndex;
			LastSlashIndex = BufferIndex;
		}
	}
	
	// NOTE(Felix): Read String inbetween slashes into Buffer
	if (LastSlashIndex != 0 && SecondLastSlashIndex != 0)
	{
		i32 Index = 0;
		for (; SecondLastSlashIndex+Index+1 != LastSlashIndex; ++Index)
		{
			BufferToReadInto[Index] = PathBuffer[SecondLastSlashIndex+Index+1];
		}
		BufferToReadInto[Index] = 0;
	}
}

internal void 
LeaveDirectory(char *PathBuffer)
{
	// NOTE(Felix): Find Slashes
	i32 LastSlashIndex = 0;
	i32 SecondLastSlashIndex = 0;
	for (i32 BufferIndex = 0; PathBuffer[BufferIndex] != 0; ++BufferIndex)
	{
		if (PathBuffer[BufferIndex] == '/')
		{
			SecondLastSlashIndex = LastSlashIndex;
			LastSlashIndex = BufferIndex;
		}
	}
	
	// NOTE(Felix): End string after second last slash
	if (SecondLastSlashIndex != 0 && LastSlashIndex != 0)
	{
		PathBuffer[SecondLastSlashIndex+1] = 0;
	}
	
	chdir(PathBuffer);
}

internal color
LineColorGetFromEntry(internal_directory_entry Entry, b32 EntrySelected)
{
	color Result = { 0 };
	
	if (EntrySelected)
	{
		ansi_color_code BackgroundColor = 0;
		switch (Entry.Type)
		{
			case ENTRY_TYPE_FILE: {
				BackgroundColor = COLOR_SELECTED_BACKGROUND_FILE;
	 		} break;

			case ENTRY_TYPE_DIRECTORY: {
				BackgroundColor = COLOR_SELECTED_BACKGROUND_DIRECTORY;
			} break;
			
			default: {
				BackgroundColor = COLOR_SELECTED_BACKGROUND_FILE;
			} break;
		}

		Result.Foreground = COLOR_SELECTED_FOREGROUND;
		Result.Background = BackgroundColor;
	}
	else
	{
		ansi_color_code ForegroundColor = 0;
		switch (Entry.Type)
		{
			case ENTRY_TYPE_FILE: {
				ForegroundColor = COLOR_UNSELECTED_FOREGROUND_FILE;
			} break;

			case ENTRY_TYPE_DIRECTORY: {
				ForegroundColor = COLOR_UNSELECTED_FOREGROUND_DIRECTORY;
			} break;

			default: {
				ForegroundColor = COLOR_UNSELECTED_FOREGROUND_FILE;
			} break;
		}

		Result.Foreground = ForegroundColor;
		Result.Background = COLOR_UNSELECTED_BACKGROUND;
	}
	
	return (Result);
}

internal void
ColorSet(color Color)
{
	fprintf(stderr, "\033[%d;%dm", Color.Background, Color.Foreground);
}

internal void
ColorResetToDefault()
{
	color Default = { 0 };
	Default.Background = COLOR_DEFAULT_BACKGROUND;
	Default.Foreground = COLOR_DEFAULT_FOREGROUND;
	ColorSet(Default);
}


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

internal void
SortDirectoryEntries(internal_directory_entry *Buffer, u32 Count)
{
	InternalEntryListSort(Buffer, (i32)Count, &InternalEntryCompareType);

	// NOTE(Felix): Find end of directory / start of files and sort each sublist by name
	i32 EntryFilesStartIndex = 0;
	for (; Buffer[EntryFilesStartIndex].Type != ENTRY_TYPE_FILE; ++EntryFilesStartIndex) {  }

	InternalEntryListSort(Buffer, EntryFilesStartIndex, &InternalEntryCompareName);
	InternalEntryListSort(Buffer+EntryFilesStartIndex, (i32)Count-EntryFilesStartIndex, &InternalEntryCompareName);
}

internal u32
DirectoryReadIntoBuffer(internal_directory_entry *Buffer, char *DirectoryPath)
{
	// NOTE(Felix): Open directory stream
	DIR *DirectoryStream = opendir(DirectoryPath);
	
	// NOTE(Felix): Gather and store all valid entries
	struct dirent *DirectoryEntry = readdir(DirectoryStream);
	u32 EntryCount = 0;
	while (DirectoryEntry != 0)
	{
		if (DirentPassesFilter(DirectoryEntry))
		{
			//DirectoryEntryPrint(DirectoryEntry);
			internal_directory_entry InternalEntry = CreateInternalEntryFromDirent(DirectoryEntry);
			Buffer[EntryCount] = InternalEntry;
			EntryCount++;
		}
		DirectoryEntry = readdir(DirectoryStream);
	}
	closedir(DirectoryStream);
	
	SortDirectoryEntries(Buffer, EntryCount);
	return (EntryCount);
}

internal i32
DirectoryGetIndexFromName(internal_directory_entry *Buffer, char *EntryName)
{
	i32 Index = 0;
	while (0 == StringEqual(Buffer[Index].Name, EntryName))
	{
		++Index;
	}
	return (Index);
}

internal void
DirectoryEnter(char *PathBuffer, char *DirectoryName)
{
	// NOTE(Felix): Append DirectoryName to PathBuffer
	i32 EndOfPathIndex = (i32)StringLength(PathBuffer);
	i32 DirectoryNameLength = (i32)StringLength(DirectoryName);
	for (i32 Index = 0; Index < DirectoryNameLength; ++Index)
	{
		PathBuffer[EndOfPathIndex+Index] = DirectoryName[Index];
	}
	
	PathBuffer[EndOfPathIndex+DirectoryNameLength+0] = '/';
	PathBuffer[EndOfPathIndex+DirectoryNameLength+1] =   0;
	chdir(PathBuffer);
}

internal void
ConsoleSetup(void)
{
	EchoDisable();
	CursorHide();
}

internal void
ConsoleCleanup(void)
{
	ColorResetToDefault();
	ScreenClear();
	EchoEnable();
	CursorShow();
	CursorMove(0, 0);
}

internal void
SignalSIGINTHandler(int Signal)
{
	ConsoleCleanup();
	exit(-1);
}

internal void
SignalSIGWINCHHandler(int Signal)
{
	GLOBALUpdateConsoleDimensions = 1;
}

int
main(void)
{
	// NOTE(Felix): Set signal so a CTRL-C restores console settings
	{
		struct sigaction SignalAction = { 0 };
		SignalAction.sa_handler = &SignalSIGINTHandler;
		sigaction(SIGINT, &SignalAction, 0);
	}
	
	// NOTE(Felix): Handle resize signal so we can reformat the window
#if 0
	// Unused at the moment as it we don't even have scrolling
	{
		struct sigaction SignalAction = { 0 };
		SignalAction.sa_handler = &SignalSIGWINCHHandler;
		sigaction(SIGWINCH, &SignalAction, 0);
	}
#endif
	
	// NOTE(Felix): Disable buffering of input, we want to process it immediately
	{
		struct termios TerminalSettings = { 0 };
		tcgetattr(STDIN_FILENO, &TerminalSettings);
		TerminalSettings.c_lflag &= (tcflag_t)~ICANON;
		tcsetattr(STDIN_FILENO, TCSANOW, &TerminalSettings);
	}

	// NOTE(Felix): Get current directory string and format properly
	char PathBuffer[PATH_MAX] = { 0 };
	getcwd(PathBuffer, sizeof(PathBuffer));
	PathBuffer[(i32)StringLength(PathBuffer)] = '/';

	// NOTE(Felix): Create and fill buffer that holds contents of current directory
	u32 DirectoryEntriesBufferSize = MEBIBYTES(1);
	internal_directory_entry *DirectoryEntriesBuffer = mmap(0, DirectoryEntriesBufferSize, PROT_READ|PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	u32 DirectoryEntriesBufferIndex = DirectoryReadIntoBuffer(DirectoryEntriesBuffer, PathBuffer);
	SortDirectoryEntries(DirectoryEntriesBuffer, DirectoryEntriesBufferIndex);
	
	// NOTE(Felix): Prepare for drawing
	ConsoleSetup();

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
	i32 SelectedIndex = 1;
	while (0 == ExitProgram)
	{
		ColorResetToDefault();
		ScreenClear();

		// NOTE(Felix): Print all valid entries
		for (u32 InternalEntryIndex = 0;
		     InternalEntryIndex < DirectoryEntriesBufferIndex;
		     ++InternalEntryIndex)
		{
			internal_directory_entry CurrentEntry = DirectoryEntriesBuffer[InternalEntryIndex];
			CursorMove((i32)InternalEntryIndex, 0);
			
			color LineColor = LineColorGetFromEntry(CurrentEntry, (i32)InternalEntryIndex == SelectedIndex);
			ColorSet(LineColor);
			ClearCurrentLine();

			fprintf(stderr, "%s", CurrentEntry.Name);
		}
		
		// NOTE(Felix): Process input
#if 1
		int InputCharacter = 0;
		read(STDIN_FILENO, &InputCharacter, sizeof(InputCharacter));
#else
		// NOTE(Felix): At the moment, this is useless.
		// if we actually format the window properly, this may come in handy in the future
		int InputCharacter = 0;
		{
			struct pollfd PollRequest = { 0 };
			PollRequest.fd = STDIN_FILENO;
			PollRequest.events = POLLIN;

			while (0 == PollRequest.revents &&
				   0 == GLOBALUpdateConsoleDimensions)
			{
				poll(&PollRequest, 1, 500);
			}
			
			if (GLOBALUpdateConsoleDimensions)
			{
				// TODO(Felix): Maybe pullout into function
				struct winsize ConsoleDimensions = { 0 };
				ioctl(STDOUT_FILENO, TIOCGWINSZ, &ConsoleDimensions);
				ConsoleRows = ConsoleDimensions.ws_row;
				ConsoleColumns = ConsoleDimensions.ws_col;
				GLOBALUpdateConsoleDimensions = 0;
				
				// NOTE(Felix): Force redraw
				continue;
			}
			
			read(STDIN_FILENO, &InputCharacter, sizeof(InputCharacter));
		}
#endif
		switch (InputCharacter)
		{
			// NOTE(Felix): Move down
			case 'j': {
				// TODO(Felix): Scrolling
				SelectedIndex = MIN((i32)DirectoryEntriesBufferIndex-1, SelectedIndex+1);
			} break;
			
			// NOTE(Felix): Move Up
			case 'k': {
				// TODO(Felix): Scrolling
				SelectedIndex = MAX(0, SelectedIndex-1);
			} break;
			
			// NOTE(Felix): Leave directory
			case 'h': {
				// NOTE(Felix): Make sure we're not in the "root directory"
				if (0 == (PathBuffer[0] == '/' && PathBuffer[1] == 0))
				{
					// NOTE(Felix): We want to automatically select the folder we just left
					char PreviousDirectoryStringBuffer[50] = { 0 };
					ReadCurrentDirectoryNameIntoBuffer(PreviousDirectoryStringBuffer, PathBuffer);
					LeaveDirectory(PathBuffer);
					DirectoryEntriesBufferIndex = DirectoryReadIntoBuffer(DirectoryEntriesBuffer, PathBuffer);
					SelectedIndex = DirectoryGetIndexFromName(DirectoryEntriesBuffer, PreviousDirectoryStringBuffer);
					ScreenClear();
				}
			} break;
			
			// NOTE(Felix): Open file or enter directory
			case 'l': {
				internal_directory_entry CurrentEntry = DirectoryEntriesBuffer[SelectedIndex];
				switch (CurrentEntry.Type)
				{
					case ENTRY_TYPE_FILE: {
						if (FileIsExecutable(CurrentEntry.Name))
						{
							// NOTE(Felix): Append executable to path
							u32 PathLength = StringLength(PathBuffer);
							u32 FileLength = StringLength(CurrentEntry.Name);
							MemoryCopy(PathBuffer+PathLength, CurrentEntry.Name, FileLength);
							ConsoleCleanup();

							// NOTE(Felix): Execl replaces current process if successfull
							int ReturnValue = execl(PathBuffer, CurrentEntry.Name, 0);
							ScreenClear();
							fprintf(stderr, "Error: %d\n", errno);
							exit(-1);
						}
						else
						{
							file_type_config ProgramToUseConfig = GetProgramToUseConfig(CurrentEntry.Name);
							char *ProgramName = GetProgramNameFromFullPath(ProgramToUseConfig.PathToProgram);
							fprintf(stderr, "\n%s\n%s\n%s\n%s\n", CurrentEntry.Name, ProgramToUseConfig.PathToProgram, ProgramName, PathBuffer);

							ConsoleCleanup();
							pid_t ChildProcessID = fork();
							if (0 == ChildProcessID)
							{
								// NOTE(Felix): This is the child process
								if (ProgramToUseConfig.IsConsoleApplication)
								{
									// NOTE(Felix): Unlink from parent
									setsid();
								}
								execl(ProgramToUseConfig.PathToProgram, ProgramName, CurrentEntry.Name, 0);
							}
							else
							{
								// NOTE(Felix): This is the parent process

								if (ProgramToUseConfig.IsConsoleApplication)
								{
									// NOTE(Felix): Wait for child to finish, as it is using the console drawing 
									waitpid(ChildProcessID, 0, 0);
								}
							}
							ConsoleSetup();
						}
					} break;

					case ENTRY_TYPE_DIRECTORY: {
						DirectoryEnter(PathBuffer, CurrentEntry.Name);
						DirectoryEntriesBufferIndex = DirectoryReadIntoBuffer(DirectoryEntriesBuffer, PathBuffer);
						SelectedIndex = 0;
						ScreenClear();
					} break;

					default: {
						// noop
					} break;
				}
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

	// NOTE(Felix): Shutdown
	ConsoleCleanup();
	munmap(DirectoryEntriesBuffer, DirectoryEntriesBufferSize);
	return (0);
}
