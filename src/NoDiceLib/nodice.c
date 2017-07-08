#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef WIN32
#include <unistd.h>
#else
#include <direct.h>
#endif

#include "NoDiceLib.h"
#include "internal.h"
#include "M6502/M6502.h"

char _error_msg[ERROR_MSG_LEN];
char _buffer[BUFFER_LEN];

static int exec_buffer_pos = 0;
static void exec_buffer_callback(const char *next_line)
{
	// The buffer space is limited, and hopefully the user never has to see it,
	// but we need to be able to give them a hint if the assembly fails...

	// Make sure we haven't hit the end of the buffer
	if(exec_buffer_pos < (ERROR_MSG_LEN-1) )
	{
		// Number of characters for next line of output
		int len = strlen(next_line), max = (ERROR_MSG_LEN-1) - exec_buffer_pos;

		if(max > 0)
		{
			// Use strncat to limit number of characters we copy up to
			// what's remaining in the buffer
			strncat(_error_msg, next_line, max);

			// Advance position
			exec_buffer_pos += len;
		}
	}
}

const char *NoDice_DoBuild()
{
	// Start with beginning of message, assuming failure
	// If there's no failure the user never sees this!
	strcpy(_error_msg, "ROM ASSEMBLY FAILED; beginning of output:\n");
	exec_buffer_pos = strlen(_error_msg);

	if(!NoDice_exec_build(exec_buffer_callback))
		return _error_msg;
	else
		return NULL;
}


static int verify_generated_files()
{
	struct stat stat_buf;
	time_t asm_mtime;	// Assembly file modified time field

	// Get the timestamp for the ASM; this is to be sure we generated a new FNS and NES file
	snprintf(_buffer, BUFFER_LEN, "%s" EXT_ASM, NoDice_config.filebase);
	if(stat(_buffer, &stat_buf) == -1)
	{
		snprintf(_error_msg, ERROR_MSG_LEN, "Unable to stat %s", _buffer);
		return 0;
	}

	// Hold on to the modified time field
	asm_mtime = stat_buf.st_mtime;

	// Verify presense and timestamp of FNS
	snprintf(_buffer, BUFFER_LEN, "%s" EXT_SYMBOLS, NoDice_config.filebase);
	if(stat(_buffer, &stat_buf) == -1)
	{
		snprintf(_error_msg, ERROR_MSG_LEN, "Initial test build FAILED:\nUnable to stat %s (symbol listing file not created?)", _buffer);
		return 0;
	}

	if(stat_buf.st_mtime < asm_mtime)
	{
		snprintf(_error_msg, ERROR_MSG_LEN, "Initial test build FAILED:\n%s is older than %s" EXT_ASM " (symbol listing file not generated?)", _buffer, NoDice_config.filebase);
		return 0;
	}

	// Verify presense and timestamp of ROM
	snprintf(_buffer, BUFFER_LEN, "%s" EXT_ROM, NoDice_config.filebase);
	if(stat(_buffer, &stat_buf) == -1)
	{
		snprintf(_error_msg, ERROR_MSG_LEN, "Initial test build FAILED:\nUnable to stat %s (ROM file not created?)", _buffer);
		return 0;
	}

	if(stat_buf.st_mtime < asm_mtime)
	{
		snprintf(_error_msg, ERROR_MSG_LEN, "Initial test build FAILED:\n%s is older than %s" EXT_ASM " (ROM file not generated?)", _buffer, NoDice_config.filebase);
		return 0;
	}

	return 1;
}


int NoDice_Init()
{
	if(!_config_init())
		return 0;

	// Attempt to run a build to test the configuration before we proceed
	if(NoDice_DoBuild() != NULL)
		return 0;

	// If build succeeded, make sure we have new FNS and NES files
	if(!verify_generated_files())
		return 0;

	if(!_rom_load())
		return 0;

	return 1;
}

const char *NoDice_Error()
{
	return _error_msg;
}

void NoDice_Shutdown()
{
	_rom_shutdown();
	_rom_free_level_list();
	_config_shutdown();
}

