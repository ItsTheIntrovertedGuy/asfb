#define _DEFAULT_SOURCE

#include <linux/limits.h>
#include <fcntl.h>
#include <stdlib.h>
#include <dirent.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <poll.h>
#include <errno.h>

#include "language_layer.h"
#include "console.c"
#include "main.h"
#include "config.h"

// NOTE(Felix): Resources:
// "execl":    To start a program to edit the file
// "dirent.h": Directory stuff
// "getcwd":   Get full path to working directory
// "chdir":    Change current working directory
// TODO(Felix): Maybe pull these console ANSI functions out into its include file

// TODO(Felix): A few features we may want to implement:
// - Help page that displays all hotkeys
// - I think we should rename "filter" to "search", but meh
// - Some kind of mechanism that doesn't make our program crash if we don't have enough memory
//     to hold all directory entries at once

// TODO(Felix): Bugs:
//  - Sometimes our selection is not within the view
//  - Entering a folder after search does not get rid of the filter (visually)
//  - Filtering for words when scrolled may result in empty view even though items pass filter (enter pushes them into view)

global_variable b32 GLOBALUpdateConsoleDimensions = 0;

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
	// TODO(Felix): This sorting does only work for ascii, so rip my asian tab files
	// Also we're not sorting numbers only files correctly, reee 
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
FilterKeepEntry(char *EntryName, b32 FilterHiddenEntries, 
                char *SearchString, b32 FilterIsCaseSensitive)
{
	b32 KeepEntry = 1;
	
	// NOTE(Felix): We don't want "." and ".." directory links
	if ((EntryName[0] == '.' && EntryName[1] == 0)  ||
	    (EntryName[0] == '.' && EntryName[1] == '.' && EntryName[2] == 0))
	{
		KeepEntry = 0;
	}

	// NOTE(Felix): Hidden items (any entry starting with ".")
	if (FilterHiddenEntries && (EntryName[0] == '.'))
	{ 
		KeepEntry = 0;
	}

	// NOTE(Felix): Apply check if entry contains search string
	if (SearchString)
	{
		if (0 == StringContains(EntryName, SearchString, FilterIsCaseSensitive))
		{
			KeepEntry = 0;
		}
	}
	
	return (KeepEntry);
}

internal i32
DirectoryGetFirstFileEntryIndex(internal_directory_entry *Buffer, u32 Count)
{
	i32 ResultIndex = 0;
	for (; 
		 (Buffer[ResultIndex].Type != ENTRY_TYPE_FILE) && (ResultIndex < (i32)Count); 
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
DirectoryReadIntoBufferAndFilter(internal_directory_entry *Buffer, u32 *EntryCount,
								 char *DirectoryPath, b32 FilterHiddenEntries,
								 char *FilterBuffer, b32 FilterIsCaseSensitive)
{
	// NOTE(Felix): Open directory stream
	DIR *DirectoryStream = opendir(DirectoryPath);
	
	// NOTE(Felix): Gather and store all valid entries
	struct dirent *DirectoryEntry = readdir(DirectoryStream);
	u32 EntryCountResult = 0;
	while (DirectoryEntry != 0)
	{
		// NOTE(Felix): We only want regular files and directories for now
		if ((DirectoryEntry->d_type == DT_DIR) || (DirectoryEntry->d_type == DT_REG)) 
		{
			if (FilterKeepEntry(DirectoryEntry->d_name, FilterHiddenEntries, FilterBuffer, FilterIsCaseSensitive))
			{
				internal_directory_entry InternalEntry = CreateInternalEntryFromDirent(DirectoryEntry);
				Buffer[EntryCountResult] = InternalEntry;
				EntryCountResult++;
			}
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
	CursorMoveTo(0, 0);
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
RefreshCurrentDirectory(internal_directory_entry *EntriesBuffer, u32 *EntryCount, i32 *SelectedIndex, char *DirectoryPath,
						b32 FilterHiddenEntries, char *FilterBuffer, b32 FilterIsCaseSensitive)
{
	// NOTE(Felix): Refresh directory by saving current name, reloading directory and finding the name we saved
	internal_directory_entry SelectedEntry = EntriesBuffer[*SelectedIndex];
	DirectoryReadIntoBufferAndFilter(EntriesBuffer, EntryCount, DirectoryPath, 
	                                 FilterHiddenEntries, FilterBuffer, FilterIsCaseSensitive);
	*SelectedIndex = DirectoryGetIndexFromName(EntriesBuffer, *EntryCount, SelectedEntry.Name);
}

internal void
OpenFileOrEnterDirectory(internal_directory_entry *Entry, 
						 internal_directory_entry *EntriesBuffer, u32 *EntryCount,
						 i32 *SelectedIndex, i32 *StartDrawIndex, i32 ConsoleRows,
						 char *PathBuffer, b32 FilterHiddenEntries,
						 char *FilterBuffer, b32 FilterIsCaseSensitive)
{
	switch (Entry->Type)
	{
		case ENTRY_TYPE_FILE: {
			if (FileIsExecutable(Entry->Name))
			{
				// NOTE(Felix): Append executable to path
				u32 PathLength = StringLength(PathBuffer);
				u32 FileLength = StringLength(Entry->Name);
				MemoryCopy(PathBuffer+PathLength, Entry->Name, FileLength);
				ConsoleCleanup();

				// NOTE(Felix): Execl replaces current process if successfull
				// If it fails, we'll simply try open it with something
				execl(PathBuffer, Entry->Name, 0);
			}

			file_type_config ProgramToUseConfig = GetProgramToUseConfig(Entry->Name);
			char *ProgramName = GetProgramNameFromFullPath(ProgramToUseConfig.PathToProgram);
			ConsoleCleanup();
			pid_t ChildProcessID = fork();
			if (0 == ChildProcessID)
			{
				// NOTE(Felix): This is the child process

				// NOTE(Felix): If graphical application, perform another fork
				// to prevent zombie child processes
				// so it becomes the OS' responsibility to clean up after that process terminates
				if (0 == ProgramToUseConfig.IsConsoleApplication)
				{
					if (0 == fork())
					{
						// NOTE(Felix): This is the process which will run
						// the graphical application in a moment.
						// Detach all standard file descriptors
						int NullFd = open("/dev/null", O_RDWR);
						Assert(NullFd > 0);
						dup2(NullFd, STDIN_FILENO);
						dup2(NullFd, STDOUT_FILENO);
						dup2(NullFd, STDERR_FILENO);
					}
					else
					{
						// There's three processes:
						// asfb processs - *this process* - graphical process
						// This process will simply terminate
						exit(0);
					}
				}
				execl(ProgramToUseConfig.PathToProgram, ProgramName, Entry->Name, 0);
				exit(0); // Exit if execl failes for some reason
			}
			else
			{
				// NOTE(Felix): This is the parent process
				// Wait for child to finish, as it is using the console drawing 
				// or is still in the process of performing the double fork
				waitpid(ChildProcessID, 0, 0);
			}
			ConsoleSetup();
		} break;

		case ENTRY_TYPE_DIRECTORY: {
			DirectoryEnter(PathBuffer, Entry->Name);
			DirectoryReadIntoBufferAndFilter(EntriesBuffer, EntryCount, PathBuffer, 
			                                 FilterHiddenEntries, FilterBuffer, FilterIsCaseSensitive);
			*SelectedIndex = 0;
			*StartDrawIndex = CalculateStartDrawIndex((i32)*EntryCount, *SelectedIndex, ConsoleRows);
		} break;

		default: {
			// noop
		} break;
	}
}

internal void
ClearFilter(char *FilterBuffer, u32 *FilterBufferIndex)
{
	FilterBuffer[0] = 0;
	*FilterBufferIndex = 0;
}

internal void
SearchFilterInputCharacter(internal_directory_entry *EntriesBuffer, u32 *EntryCount, 
						   i32 *SelectedIndex, i32 *StartDrawIndex, i32 ConsoleRows,
						   char *DirectoryPath, b32 FilterHiddenEntries,
                           char *FilterBuffer, u32 *FilterBufferIndex, u32 FilterBufferSize,
                           program_state *ProgramState, i32 InputCharacter, b32 FilterIsCaseSensitive)
{
	// TODO(Felix): I'm not a fan of how I'm solving this
	// Whenever we "broaden" up our search, we re-read the whole directory into our buffer
	// this makes the code easy, but is probably really unperformant on bigger directories
	// then again I haven't measured stuff so it may be fine I dunno

	switch (InputCharacter)
	{
		// NOTE(Felix): Delete last character of filter
		// We just refill the buffer and reapply the updated filter
		case 127: // DEL, terminal emulators (at least mine) sends this when pressing backspace
		case '\b': { 
			if (*FilterBufferIndex > 0)
			{
				*FilterBufferIndex -= 1;
				FilterBuffer[*FilterBufferIndex] = 0;
				DirectoryReadIntoBufferAndFilter(EntriesBuffer, EntryCount,
				                                 DirectoryPath, FilterHiddenEntries,
				                                 FilterBuffer, FilterIsCaseSensitive);
			}
		} break;
		
		// NOTE(Felix): Reset, but don't abort search
		case 23: { // Control-W
			ClearFilter(FilterBuffer, FilterBufferIndex);
			DirectoryReadIntoBufferAndFilter(EntriesBuffer, EntryCount,
											 DirectoryPath, FilterHiddenEntries,
											 0, 0);
		} break;
		
		// NOTE(Felix): Abort search
		case 27: { // ESC
			ClearFilter(FilterBuffer, FilterBufferIndex);
			DirectoryReadIntoBufferAndFilter(EntriesBuffer, EntryCount,
											 DirectoryPath, FilterHiddenEntries,
											 0, 0);
			*ProgramState = PROGRAM_STATE_BROWSING;
		} break;
		
		// NOTE(Felix): Return to browsing mode.
		// If there's just one item left, also enter / open that entry as well
		/* I don't think '\r' is actually used (on linux at least), so just delete this comment if it actually does */ //case '\r': 
		case '\n': { // Enter
			*StartDrawIndex = 0;
			*SelectedIndex = 0;
			*ProgramState = PROGRAM_STATE_BROWSING;
			
			if (*EntryCount == 1)
			{
				internal_directory_entry *Entry = &EntriesBuffer[0];
				OpenFileOrEnterDirectory(Entry,
				                         EntriesBuffer, EntryCount,
				                         SelectedIndex, StartDrawIndex, ConsoleRows,
				                         DirectoryPath, FilterHiddenEntries,
				                         FilterBuffer, FilterIsCaseSensitive);
			}
		} break;
		
		// NOTE(Felix): Add character to search (if it is valid) and update filter
		default: {
			if (0 == CharIsAsciiControlCharacter((char)InputCharacter) &&
				*FilterBufferIndex+2 < FilterBufferSize) // two spaces free - one for the new char, another one for 0 terminator
			{
				FilterBuffer[*FilterBufferIndex] = (char)InputCharacter;
				*FilterBufferIndex += 1;
				FilterBuffer[*FilterBufferIndex] = 0; // Zero terminate string
				
				u32 Index = 0;
				while (Index < *EntryCount)
				{
					if (FilterKeepEntry(EntriesBuffer[Index].Name, FilterHiddenEntries,
										FilterBuffer, FilterIsCaseSensitive))
					{
						++Index;
					}
					else
					{
						// NOTE(Felix): Remove entry by copying (and thus overwriting)
						// the rest of the entries one slot "down"
						*EntryCount -= 1;
						u32 SlotsToMove = *EntryCount - Index;
						MemoryCopy(&EntriesBuffer[Index], &EntriesBuffer[Index+1], 
								   sizeof(EntriesBuffer[0]) * SlotsToMove);
					}
				}
			}
		} break;
	}
}

internal b32
IndexIsOffscreen(i32 SelectedIndex, i32 StartDrawIndex, i32 ConsoleRows)
{
	if (SelectedIndex < StartDrawIndex ||
		SelectedIndex - StartDrawIndex >= ConsoleRows)
	{
		return (1);
	}
	else
	{
		return (0);
	}
}

internal i32
CenterIndexByReturningStartDrawIndex(i32 SelectedIndex, i32 StartDrawIndex, 
                                     u32 EntryCount, i32 ConsoleRows)
{
	i32 HalfHeight = ConsoleRows / 2;

	if (SelectedIndex - HalfHeight < 0)
	{
		// NOTE(Felix): Centering the selection is not possible because we're
		// too close to the start, clamp
		return (0);
	}
	else if (SelectedIndex + HalfHeight >= (i32)EntryCount)
	{
		// NOTE(Felix): Centering the selection is not possible because we're
		// too close to the end, clamp
		return ((i32)EntryCount - ConsoleRows);
	}
	else
	{
		return (SelectedIndex - HalfHeight);
	}
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

	// NOTE(Felix): Setup state and filter buffer two states will use
	program_state ProgramState = PROGRAM_STATE_BROWSING;
	b32 FilterHiddenEntries = 0;
	b32 FilterIsCaseSensitive = 0;
	enum { FILTER_BUFFER_SIZE = 256 };
	char FilterBuffer[FILTER_BUFFER_SIZE] = { 0 };
	u32 FilterBufferIndex = 0;

	// NOTE(Felix): Get current directory string and format properly
	char PathBuffer[PATH_MAX] = { 0 };
	getcwd(PathBuffer, sizeof(PathBuffer));
	PathBuffer[(i32)StringLength(PathBuffer)] = '/';

	// NOTE(Felix): Create and fill buffer that holds contents of current directory
	u32 CurrentDirectoryEntriesBufferSize = MEBIBYTES(2);
	internal_directory_entry *CurrentDirectoryEntriesBuffer = mmap(0, CurrentDirectoryEntriesBufferSize, PROT_READ|PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	u32 CurrentDirectoryEntryCount = 0;
	DirectoryReadIntoBufferAndFilter(CurrentDirectoryEntriesBuffer, &CurrentDirectoryEntryCount, PathBuffer, FilterHiddenEntries, 0, 0);
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
				CursorMoveTo((i32)InternalEntryIndex-StartDrawIndex, 0);

				color LineColor = LineColorGetFromEntry(CurrentEntry, (i32)InternalEntryIndex == SelectedIndex);
				ColorSet(LineColor);
				ClearCurrentLine();
				printf("%s", CurrentEntry.Name);
			}
		}
		else
		{
			// NOTE(Felix): Display that this directory is empty
			CursorMoveTo(0, 0);
			color LineColor = { 0 };
			LineColor.Background = COLOR_DEFAULT_BACKGROUND;
			LineColor.Foreground = COLOR_UNSELECTED_FOREGROUND_DIRECTORY;
			ColorSet(LineColor);
			ClearCurrentLine();
			printf("<empty>");
		}

		
		// NOTE(Felix): Display search string
		// TODO(Felix): Debug stuff, is pretty ugly and will probably hide an entry
		// if we're filling all rows
		if (FilterBuffer[0] != 0 ||
			ProgramState == PROGRAM_STATE_ENTER_SEARCH_FILTER_CASE_INSENSITIVE ||
			ProgramState == PROGRAM_STATE_ENTER_SEARCH_FILTER_CASE_SENSITIVE)
		{
			CursorMoveTo(ConsoleRows-1, 0);
			ColorResetToDefault();
			ClearCurrentLine();
			printf("Search term (%s): %s", 
			       (FilterIsCaseSensitive) ? "(Case sensitive)" : ("Case insensitive"),
				   FilterBuffer);
		}
		
		
		// NOTE(Felix): Get input (and/or catch resize of window)
		int InputCharacter = 0;
		{
			struct pollfd PollRequest = { 0 };
			PollRequest.fd = STDIN_FILENO;
			PollRequest.events = POLLIN;

			// NOTE(Felix): Wait for either
			//  - Input
			//  - Interrupt of any kind (including resizing of console)
			poll(&PollRequest, 1, -1);
			
			if (GLOBALUpdateConsoleDimensions)
			{
				// NOTE(Felix): Update Dimensions and force redraw
				ConsoleUpdateDimensions(&ConsoleRows, &ConsoleColumns);
				GLOBALUpdateConsoleDimensions = 0;
				if (IndexIsOffscreen(SelectedIndex, StartDrawIndex, ConsoleRows))
				{
					StartDrawIndex = CenterIndexByReturningStartDrawIndex(SelectedIndex, StartDrawIndex,
																		  CurrentDirectoryEntryCount, ConsoleRows);
				}
				continue;
			}
			
			read(STDIN_FILENO, &InputCharacter, sizeof(InputCharacter));
		}
		

		// NOTE(Felix): Input is dependant on program state
		switch (ProgramState)
		{
			// NOTE(Felix): Input characters are mapped to do different things
			case PROGRAM_STATE_BROWSING: {
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
							ClearFilter(FilterBuffer, &FilterBufferIndex);

							// NOTE(Felix): We want to automatically select the folder we just left
							char PreviousDirectoryStringBuffer[PATH_MAX] = { 0 };
							ReadCurrentDirectoryNameIntoBuffer(PreviousDirectoryStringBuffer, PathBuffer);
							LeaveDirectory(PathBuffer);
							DirectoryReadIntoBufferAndFilter(CurrentDirectoryEntriesBuffer, &CurrentDirectoryEntryCount, PathBuffer, 
															 FilterHiddenEntries, FilterBuffer, FilterIsCaseSensitive);
							SelectedIndex = DirectoryGetIndexFromName(CurrentDirectoryEntriesBuffer, CurrentDirectoryEntryCount, PreviousDirectoryStringBuffer);

							// NOTE(Felix): Center selection
							StartDrawIndex = CalculateStartDrawIndex((i32)CurrentDirectoryEntryCount, SelectedIndex, ConsoleRows);
						}
					} break;

					// NOTE(Felix): Open file or enter directory
					case 'l': {
						ClearFilter(FilterBuffer, &FilterBufferIndex);
						internal_directory_entry *Entry = &CurrentDirectoryEntriesBuffer[SelectedIndex];
						OpenFileOrEnterDirectory(Entry, 
												 CurrentDirectoryEntriesBuffer, &CurrentDirectoryEntryCount,
												 &SelectedIndex, &StartDrawIndex, ConsoleRows,
												 PathBuffer, FilterHiddenEntries,
												 FilterBuffer, FilterIsCaseSensitive);
					} break;

					// NOTE(Felix): Toggle hidden files 
					case 't': {
						FilterHiddenEntries = !FilterHiddenEntries;
						RefreshCurrentDirectory(CurrentDirectoryEntriesBuffer, &CurrentDirectoryEntryCount, &SelectedIndex, PathBuffer,
												FilterHiddenEntries, FilterBuffer, FilterIsCaseSensitive);
					} break;

					// NOTE(Felix): Force refresh
					case 'r': {
						RefreshCurrentDirectory(CurrentDirectoryEntriesBuffer, &CurrentDirectoryEntryCount, &SelectedIndex, PathBuffer,
												FilterHiddenEntries, FilterBuffer, FilterIsCaseSensitive);
					} break;

					// NOTE(Felix): Jump to top (first Directory)
					case 'd': {
						SelectedIndex = 0;
						StartDrawIndex = 0;
					} break;

					// NOTE(Felix): Jump to first file 
					case 'f': {
						if (CurrentDirectoryEntryCount > 0)
						{
							i32 FirstFileIndex = DirectoryGetFirstFileEntryIndex(CurrentDirectoryEntriesBuffer, CurrentDirectoryEntryCount);
							FirstFileIndex = CLAMP(FirstFileIndex, 0, (i32)CurrentDirectoryEntryCount-1);

							if (IndexIsOffscreen(FirstFileIndex, StartDrawIndex, ConsoleRows))
							{
								StartDrawIndex = CenterIndexByReturningStartDrawIndex(FirstFileIndex, StartDrawIndex,
																					  CurrentDirectoryEntryCount, ConsoleRows);
							}
							SelectedIndex = FirstFileIndex;
						}
					} break;

					// NOTE(Felix): Jump to end
					case 'e': {
						SelectedIndex = (i32)CurrentDirectoryEntryCount-1;

						// NOTE(Felix): Scroll if not all entries fit in the window
						StartDrawIndex = MAX((i32)CurrentDirectoryEntryCount - ConsoleRows, 0);
					} break;
					
					// NOTE(Felix): Try to jump to character given afterwards
					case 'g': {
						ProgramState = PROGRAM_STATE_AWAITING_JUMP_CHARACTER;
					} break;

					// NOTE(Felix): Filter case   sensitive
					case '/': {
 						ProgramState = PROGRAM_STATE_ENTER_SEARCH_FILTER_CASE_INSENSITIVE;
						ClearFilter(FilterBuffer, &FilterBufferIndex);
						DirectoryReadIntoBufferAndFilter(CurrentDirectoryEntriesBuffer, &CurrentDirectoryEntryCount,
														 PathBuffer, FilterHiddenEntries,
														 0, 0);
					} break;

					// NOTE(Felix): Filter case insensitive
					case '?': {
 						ProgramState = PROGRAM_STATE_ENTER_SEARCH_FILTER_CASE_SENSITIVE;
						ClearFilter(FilterBuffer, &FilterBufferIndex);
						DirectoryReadIntoBufferAndFilter(CurrentDirectoryEntriesBuffer, &CurrentDirectoryEntryCount,
														 PathBuffer, FilterHiddenEntries,
														 0, 0);
					} break;
					
					// NOTE(Felix): Reset filter
					case 27: { // ESC
						ClearFilter(FilterBuffer, &FilterBufferIndex);
						DirectoryReadIntoBufferAndFilter(CurrentDirectoryEntriesBuffer, &CurrentDirectoryEntryCount,
														 PathBuffer, FilterHiddenEntries,
														 0, 0);
					} break;
					
					// NOTE(Felix): Skip a page forward
					case 6: { // CTRL-F
						if ((i32)CurrentDirectoryEntryCount - StartDrawIndex < ConsoleRows)
						{
							// NOTE(Felix): Last index is on screen, just select that
							SelectedIndex = (i32)CurrentDirectoryEntryCount-1;
						}
						else if (StartDrawIndex + 2*ConsoleRows < (i32)CurrentDirectoryEntryCount)
						{
							// NOTE(Felix): Skipping a page does not display the last entry, so just
							// happily skip along
							SelectedIndex  += ConsoleRows;
							StartDrawIndex += ConsoleRows;
						}
						else 
						{
							// NOTE(Felix): Scrolling reveals the end of all entries
							StartDrawIndex = (i32)CurrentDirectoryEntryCount - ConsoleRows;
							SelectedIndex = MIN((i32)CurrentDirectoryEntryCount - 1, SelectedIndex + ConsoleRows);
						}
					} break;
					
					// NOTE(Felix): Skip a page backward
					case 2: { // CTRL-B
						if (StartDrawIndex == 0)
						{
							// NOTE(Felix): First index is on screen, just select that
							SelectedIndex = 0;
						}
						else if (StartDrawIndex - ConsoleRows > 0)
						{
							// NOTE(Felix): Skipping a page does not display first entry, so just
							// happily skip along
							SelectedIndex  -= ConsoleRows;
							StartDrawIndex -= ConsoleRows;
						}
						else
						{
							// NOTE(Felix): Scrolling reveals the start of all entries
							StartDrawIndex = 0;
							SelectedIndex = MAX(0, SelectedIndex - ConsoleRows);
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
			} break;
			

			// NOTE(Felix): Jump to first entry starting with InputCharacter
			case PROGRAM_STATE_AWAITING_JUMP_CHARACTER: {
				InputCharacter = CharToLowerIfIsLetter((char)InputCharacter);
				i32 IndexToJumpTo = -1;
				for (i32 Index = 0; Index < (i32)CurrentDirectoryEntryCount; ++Index)
				{
					char StartingCharacter = CharToLowerIfIsLetter(CurrentDirectoryEntriesBuffer[Index].Name[0]);
					if (StartingCharacter == InputCharacter)
					{
						IndexToJumpTo = Index;
						break;
					}
				}
				
				if (IndexToJumpTo >= 0)
				{
					if (IndexIsOffscreen(IndexToJumpTo, StartDrawIndex, ConsoleRows))
					{
						StartDrawIndex = CenterIndexByReturningStartDrawIndex(SelectedIndex, StartDrawIndex, 
																			  CurrentDirectoryEntryCount, ConsoleRows);
					}
					SelectedIndex = IndexToJumpTo;
				}
				
				ProgramState = PROGRAM_STATE_BROWSING;
			} break;

			
			case PROGRAM_STATE_ENTER_SEARCH_FILTER_CASE_SENSITIVE: {
				SearchFilterInputCharacter(CurrentDirectoryEntriesBuffer, &CurrentDirectoryEntryCount, 
										   &SelectedIndex, &StartDrawIndex, ConsoleRows,
										   PathBuffer, FilterHiddenEntries,
										   FilterBuffer, &FilterBufferIndex, FILTER_BUFFER_SIZE,
										   &ProgramState, InputCharacter, 1);
			} break;

			case PROGRAM_STATE_ENTER_SEARCH_FILTER_CASE_INSENSITIVE: {
				SearchFilterInputCharacter(CurrentDirectoryEntriesBuffer, &CurrentDirectoryEntryCount, 
										   &SelectedIndex, &StartDrawIndex, ConsoleRows,
										   PathBuffer, FilterHiddenEntries, 
										   FilterBuffer, &FilterBufferIndex, FILTER_BUFFER_SIZE,
										   &ProgramState, InputCharacter, 0);
			} break;
		}
	}

	// NOTE(Felix): Shutdown
	ConsoleCleanup();
	munmap(CurrentDirectoryEntriesBuffer, CurrentDirectoryEntriesBufferSize);
	return (0);
}
