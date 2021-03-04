#pragma once

// NOTE(Felix): Controls:
// 'j'   - Move down
// 'k'   - Move up
// 'h'   - Leave directory
// 'l'   - Enter directory / open file
// 't'   - Toggle hidden files / directories
// 'r'   - Refresh contents of current folder
// 'd'   - Jump to top / first directory
// 'f'   - Jump to first file
// 'e'   - Jump to end
// 'g'   - Jump to first entry starting with the following character (case insensitive)
// '/'   - Search (case insensitive)
// '?'   - Search (case sensitive)
// 'C-f' - Move a page forward
// 'C-b' - Move a page backward
// 'C-w' - Clear but continue search
// 'esc' - Clear search and enter browsing mode
// 'q'   - Quit
// TODO(Felix): 'x' for 7z unzip?

#define SCROLL_OFF 5

global_variable file_type_config GLOBALFileTypeConfig[] = {
	// 
	// File-Ending   Path to program   IsConsoleApplication
	{ "",           "/bin/nvim",               1            }, // Default

	{ ".pdf",       "/bin/zathura",            0            },
	{ ".djvu",      "/bin/zathura",            0            },

	{ ".png",       "/bin/feh",                0            },
	{ ".jpeg",      "/bin/feh",                0            },
	{ ".jpg",       "/bin/feh",                0            },
	{ ".gif",       "/bin/feh",                0            },

	{ ".mp4",       "/bin/mpv",                0            },
	{ ".mkv",       "/bin/mpv",                0            },
	{ ".avi",       "/bin/mpv",                0            },
	{ ".mp3",       "/bin/mpv",                0            },
	{ ".flac",      "/bin/mpv",                0            },
	{ ".ogg",       "/bin/mpv",                0            },
};
