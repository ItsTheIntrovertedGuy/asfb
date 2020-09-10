#pragma once

#define SCROLL_OFF 5

global_variable file_type_config GLOBALFileTypeConfig[] = {
	// File-Ending
	//              Path to program
	//                                 IsConsoleApplication
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
};
