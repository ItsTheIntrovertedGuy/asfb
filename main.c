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
#include "main.h"
#include "config.h"

// NOTE(Felix): Resources:
// "execl":    To start a program to edit the file
// "dirent.h": Directory stuff
// "getcwd":   Get full path to working directory
// "chdir":    Change current working directory
// TODO(Felix): Maybe pull these console ANSI functions out into its include file

// TODO(Felix): Known Bugs:
// Resizing the window sometimes yeets current selection into the shadow realm

// TODO(Felix): A few features we may want to implement:
// - "/"   - search / filter within directories
// - "C-F" - skip a "page" (one terminal height of entries)
// - Some kind of mechanism that doesn't make our program crash if we don't have enough memory
//     to hold all directory entries at once

global_variable b32 GLOBALUpdateConsoleDimensions = 0;
global_variable b32 GLOBALFilterHiddenItems = 0;


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
	if (LastSlashIndex != 0)
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
	if (LastSlashIndex != 0)
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
	printf("\033[%d;%dm", Color.Background, Color.Foreground);
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
	printf("\033[2J");
}

internal void
ClearCurrentLine(void)
{
	printf("\033[2K");
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
	printf("\033[?25l");
}

internal void
CursorShow(void)
{
	printf("\033[?25h");
}

internal void
CursorMove(i32 Y, i32 X)
{
	// NOTE(Felix): This function is zero indexed, ANSI escape sequences apparently aren't though
	printf("\033[%d;%dH", Y+1, X+1);
}

internal void
DirectoryEntryPrint(struct dirent *DirectoryEntry)
{
	printf("d_ino:    %ld\n",  DirectoryEntry->d_ino);
	printf("d_off:    %ld\n",  DirectoryEntry->d_off);
	printf("d_reclen: %d\n",   DirectoryEntry->d_reclen);
	printf("d_type:   %d\n",   DirectoryEntry->d_type);
	printf("d_name:   %s\n\n", DirectoryEntry->d_name);
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

	printf("Name: %s\n",   Entry.Name);
	printf("Type: %s\n\n", TypeString);
}

internal internal_directory_entry
CreateInternalEntryFromDirent(struct dirent *Entry)
{
	internal_directory_entry Result = { 0 };
	Result.NameLength = (i32)StringLength(Entry->d_name);
	MemoryCopy(&Result.Name, Entry->d_name, (u32)Result.NameLength);
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
	
	// NOTE(Felix): Additional filter: Hidden items (any entry starting with ".")
	if (GLOBALFilterHiddenItems && Entry->d_name[0] == '.')
	{ 
		return (0);
	}
	
	return (1);
}

internal i32
DirectoryGetFirstFileEntryIndex(internal_directory_entry *Buffer, u32 Count)
{
	i32 ResultIndex = 0;
	for (; 
		 (Buffer[ResultIndex].Type != ENTRY_TYPE_FILE) && (ResultIndex+1 < (i32)Count); 
		 ++ResultIndex) 
	{ 
		// noop;
	}
	return (ResultIndex);
}

internal void
SortDirectoryEntries(internal_directory_entry *Buffer, u32 Count)
{
	InternalEntryListSort(Buffer, (i32)Count, &InternalEntryCompareType);

	// NOTE(Felix): Find end of directory / start of files and sort each sublist by name
	i32 EntryFilesStartIndex = DirectoryGetFirstFileEntryIndex(Buffer, Count);
	InternalEntryListSort(Buffer, EntryFilesStartIndex, &InternalEntryCompareName);
	InternalEntryListSort(Buffer+EntryFilesStartIndex, (i32)Count-EntryFilesStartIndex, &InternalEntryCompareName);
}

internal void
DirectoryReadIntoBuffer(internal_directory_entry *Buffer, char *DirectoryPath, u32 *EntryCount)
{
	// NOTE(Felix): Open directory stream
	DIR *DirectoryStream = opendir(DirectoryPath);
	
	// NOTE(Felix): Gather and store all valid entries
	struct dirent *DirectoryEntry = readdir(DirectoryStream);
	u32 EntryCountResult = 0;
	while (DirectoryEntry != 0)
	{
		if (DirentPassesFilter(DirectoryEntry))
		{
			internal_directory_entry InternalEntry = CreateInternalEntryFromDirent(DirectoryEntry);
			Buffer[EntryCountResult] = InternalEntry;
			EntryCountResult++;
		}
		DirectoryEntry = readdir(DirectoryStream);
	}
	closedir(DirectoryStream);
	
	SortDirectoryEntries(Buffer, EntryCountResult);
	*EntryCount = EntryCountResult;
}

internal i32
DirectoryGetIndexFromName(internal_directory_entry *Buffer, u32 EntryCount, char *EntryName)
{
	for (i32 Index = 0;
		 Index < (i32)EntryCount;
		 ++Index)
	{
		if (StringEqual(Buffer[Index].Name, EntryName))
		{
			return (Index);
		}
	}
	return (0);
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

internal i32
CalculateStartDrawIndex(i32 NumberOfEntries, i32 SelectedIndex, i32 ConsoleRows)
{
	i32 StartDrawIndex = 0;
	if (SelectedIndex < ConsoleRows - SCROLL_OFF)
	{
		StartDrawIndex = 0;
	}
	else if (SelectedIndex >= NumberOfEntries - ConsoleRows + SCROLL_OFF)
	{
		StartDrawIndex = NumberOfEntries - ConsoleRows;
	}
	else
	{
		StartDrawIndex = SelectedIndex - (ConsoleRows/2);
	}
	return (StartDrawIndex);
}

internal void
RefreshCurrentDirectory(internal_directory_entry *EntriesBuffer, u32 *EntryCount, i32 *SelectedIndex, char *PathBuffer)
{
	// NOTE(Felix): Refresh directory by saving current name, reloading directory and finding the name we saved
	internal_directory_entry SelectedEntry = EntriesBuffer[*SelectedIndex];
	DirectoryReadIntoBuffer(EntriesBuffer, PathBuffer, EntryCount);
	*SelectedIndex = DirectoryGetIndexFromName(EntriesBuffer, *EntryCount, SelectedEntry.Name);
}

internal void
ConsoleUpdateDimensions(i32 *ConsoleRows, i32 *ConsoleColumns)
{
	struct winsize ConsoleDimensions = { 0 };
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &ConsoleDimensions);
	*ConsoleRows = ConsoleDimensions.ws_row;
	*ConsoleColumns = ConsoleDimensions.ws_col;
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
main(i32 ArgumentCount, char **Arguments)
{
	// NOTE(Felix): Disable printf stdout buffering
	setvbuf(stdout, 0, _IONBF, 0);

	// NOTE(Felix): Set signal so a CTRL-C restores console settings
	{
		struct sigaction SignalAction = { 0 };
		SignalAction.sa_handler = &SignalSIGINTHandler;
		sigaction(SIGINT, &SignalAction, 0);
	}
	
	// NOTE(Felix): Handle resize signal so we can reformat the window
	{
		struct sigaction SignalAction = { 0 };
		SignalAction.sa_handler = &SignalSIGWINCHHandler;
		sigaction(SIGWINCH, &SignalAction, 0);
	}
	
	// NOTE(Felix): Disable buffering of input, we want to process it immediately
	{
		struct termios TerminalSettings = { 0 };
		tcgetattr(STDIN_FILENO, &TerminalSettings);
		TerminalSettings.c_lflag &= (tcflag_t)~ICANON;
		tcsetattr(STDIN_FILENO, TCSANOW, &TerminalSettings);
	}
	
	// NOTE(Felix): The user is able to pass a path as an argument - we'll just try to enter this directory
	// if it doesn't work we stay in the current directory
	// We'll also just use the first argument (after the program name itself)
	if (ArgumentCount >= 2) // First argument is program name itself
	{
		chdir(Arguments[1]);
	}

	// NOTE(Felix): Get current directory string and format properly
	char PathBuffer[PATH_MAX] = { 0 };
	getcwd(PathBuffer, sizeof(PathBuffer));
	PathBuffer[(i32)StringLength(PathBuffer)] = '/';

	// NOTE(Felix): Create and fill buffer that holds contents of current directory
	u32 CurrentDirectoryEntriesBufferSize = MEBIBYTES(2);
	internal_directory_entry *CurrentDirectoryEntriesBuffer = mmap(0, CurrentDirectoryEntriesBufferSize, PROT_READ|PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	u32 CurrentDirectoryEntryCount = 0;
	DirectoryReadIntoBuffer(CurrentDirectoryEntriesBuffer, PathBuffer, &CurrentDirectoryEntryCount);
	SortDirectoryEntries(CurrentDirectoryEntriesBuffer, CurrentDirectoryEntryCount);
	
	// NOTE(Felix): Prepare for drawing
	ConsoleSetup();

	// NOTE(Felix): Get Console dimensions
	i32 ConsoleRows = 0;
	i32 ConsoleColumns = 0;
	ConsoleUpdateDimensions(&ConsoleRows, &ConsoleColumns);

	// NOTE(Felix): Draw
	b32 ExitProgram = 0;
	i32 SelectedIndex = 0;
	i32 StartDrawIndex = 0;
	while (0 == ExitProgram)
	{
		ColorResetToDefault();
		ScreenClear();

		if (CurrentDirectoryEntryCount > 0)
		{
			// NOTE(Felix): Print all valid entries
			for (i32 InternalEntryIndex = StartDrawIndex;
			     InternalEntryIndex < MIN(StartDrawIndex + ConsoleRows, (i32)CurrentDirectoryEntryCount);
			     ++InternalEntryIndex)
			{
				internal_directory_entry CurrentEntry = CurrentDirectoryEntriesBuffer[InternalEntryIndex];
				CursorMove((i32)InternalEntryIndex-StartDrawIndex, 0);

				color LineColor = LineColorGetFromEntry(CurrentEntry, (i32)InternalEntryIndex == SelectedIndex);
				ColorSet(LineColor);
				ClearCurrentLine();
				printf("%s", CurrentEntry.Name);
			}
		}
		else
		{
			// NOTE(Felix): Display that this directory is empty
			CursorMove(0, 0);
			color LineColor = { 0 };
			LineColor.Background = COLOR_DEFAULT_BACKGROUND;
			LineColor.Foreground = COLOR_UNSELECTED_FOREGROUND_DIRECTORY;
			ColorSet(LineColor);
			ClearCurrentLine();
			printf("<empty>");
		}
		
		
		// NOTE(Felix): Process input
		int InputCharacter = 0;
		{
			struct pollfd PollRequest = { 0 };
			PollRequest.fd = STDIN_FILENO;
			PollRequest.events = POLLIN;

#if 0
			// NOTE(Felix): In 500ms intervall
			while (0 == PollRequest.revents &&
				   0 == GLOBALUpdateConsoleDimensions)
			{
				poll(&PollRequest, 1, 500);
			}
#else
			// NOTE(Felix): Wait for either
			//  - Buffered Input
			//  - Interrupt of any kind (including resizing of console)
			poll(&PollRequest, 1, -1);
#endif
			
			if (GLOBALUpdateConsoleDimensions)
			{
				// NOTE(Felix): Update Dimensions and force redraw
				// TODO(Felix): I'm pretty sure resizing the window may yeet
				// some entries off the grid right here. 
				ConsoleUpdateDimensions(&ConsoleRows, &ConsoleColumns);
				GLOBALUpdateConsoleDimensions = 0;
				continue;
			}
			
			read(STDIN_FILENO, &InputCharacter, sizeof(InputCharacter));
		}

		switch (InputCharacter)
		{
			// NOTE(Felix): Move down
			case 'j': {
				SelectedIndex = MIN((i32)CurrentDirectoryEntryCount-1, SelectedIndex+1);
				
				// NOTE(Felix): Scrolling
				if (SelectedIndex - StartDrawIndex >= ConsoleRows - SCROLL_OFF &&
					SelectedIndex + SCROLL_OFF < (i32)CurrentDirectoryEntryCount) // Only scroll if not all entries are displayed
				{
					StartDrawIndex++;
				}
			} break;
			
			// NOTE(Felix): Move Up
			case 'k': {
				SelectedIndex = MAX(0, SelectedIndex-1);
				
				// NOTE(Felix): Scrolling
				if (SelectedIndex - StartDrawIndex <= SCROLL_OFF)
				{
					StartDrawIndex = MAX(StartDrawIndex-1, 0);
				}
			} break;
			
			// NOTE(Felix): Leave directory
			case 'h': {
				// NOTE(Felix): Make sure we're not in the "root directory"
				if (0 == (PathBuffer[0] == '/' && PathBuffer[1] == 0))
				{
					// NOTE(Felix): We want to automatically select the folder we just left
					char PreviousDirectoryStringBuffer[PATH_MAX] = { 0 };
					ReadCurrentDirectoryNameIntoBuffer(PreviousDirectoryStringBuffer, PathBuffer);
					LeaveDirectory(PathBuffer);
					DirectoryReadIntoBuffer(CurrentDirectoryEntriesBuffer, PathBuffer, &CurrentDirectoryEntryCount);
					SelectedIndex = DirectoryGetIndexFromName(CurrentDirectoryEntriesBuffer, CurrentDirectoryEntryCount, PreviousDirectoryStringBuffer);
					
					// NOTE(Felix): Center selection
					StartDrawIndex = CalculateStartDrawIndex((i32)CurrentDirectoryEntryCount, SelectedIndex, ConsoleRows);
				}
			} break;
			
			// NOTE(Felix): Open file or enter directory
			case 'l': {
				internal_directory_entry CurrentEntry = CurrentDirectoryEntriesBuffer[SelectedIndex];
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

							ConsoleCleanup();
							pid_t ChildProcessID = fork();
							if (0 == ChildProcessID)
							{
								// NOTE(Felix): This is the child process
								if (0 == ProgramToUseConfig.IsConsoleApplication)
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
						DirectoryReadIntoBuffer(CurrentDirectoryEntriesBuffer, PathBuffer, &CurrentDirectoryEntryCount);
						SelectedIndex = 0;
						StartDrawIndex = CalculateStartDrawIndex((i32)CurrentDirectoryEntryCount, SelectedIndex, ConsoleRows);
					} break;

					default: {
						// noop
					} break;
				}
			} break;

			// NOTE(Felix): Toggle hidden files 
			case 't': {
				GLOBALFilterHiddenItems = !GLOBALFilterHiddenItems;
				RefreshCurrentDirectory(CurrentDirectoryEntriesBuffer, &CurrentDirectoryEntryCount, &SelectedIndex, PathBuffer);
			} break;
			
			// NOTE(Felix): Force refresh
			case 'r': {
				RefreshCurrentDirectory(CurrentDirectoryEntriesBuffer, &CurrentDirectoryEntryCount, &SelectedIndex, PathBuffer);
			} break;
			
			// NOTE(Felix): Jump to top (first Directory)
			case 'd': {
				SelectedIndex = 0;
				StartDrawIndex = 0;
			} break;

			// NOTE(Felix): Jump to first file 
			case 'f': {
				i32 FirstFileIndex = DirectoryGetFirstFileEntryIndex(CurrentDirectoryEntriesBuffer, CurrentDirectoryEntryCount);
				
				// NOTE(Felix): Check if we need to do scrolling
				if (FirstFileIndex - StartDrawIndex >= ConsoleRows - SCROLL_OFF &&
					FirstFileIndex + SCROLL_OFF < (i32)CurrentDirectoryEntryCount) // Only scroll if not all entries are displayed
				{
					// TODO(Felix): Center selection
				}
				
				SelectedIndex = FirstFileIndex;
			} break;
			
			// NOTE(Felix): Jump to end
			case 'g': {
				SelectedIndex = (i32)CurrentDirectoryEntryCount-1;
				
				// NOTE(Felix): Scroll if not all entries fit in the window
				StartDrawIndex = MAX((i32)CurrentDirectoryEntryCount - ConsoleRows, 0);
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
	munmap(CurrentDirectoryEntriesBuffer, CurrentDirectoryEntriesBufferSize);
	return (0);
}
