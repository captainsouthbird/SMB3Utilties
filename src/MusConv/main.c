#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "NoDiceLib.h"
#include "MusConv.h"


static void usage()
{
	fprintf(stderr,
		"MusConv (import) [MIDI] [ASM]\n"
		"\n"
		"[MIDI]: Need a MIDI file input, which should be laid out like this:\n"
		"Track 0: Conductor track, limited events (see documentation)\n"
		"Track 1: Square Channel 2\n"
		"Track 2: Square Channel 1\n"
		"Track 3: Triangle Channel\n"
		"Track 4: Noise Channel (limited sounds, MIDI channel 10)\n"
		"Track 5: DCM Channel (limited sounds, MIDI channel 10)\n"
		"\n"
		"[ASM]: Output file to hold ASM file data; Some assembly required :)\n"
		"\n"

		// Export feature is broken and incomplete.  We don't talk about.
		/*
		"----------------------------------------------------------------------\n"
		"\n"
		"MusConv (export) [MIDI] [Header/Index Name] [Start/End/Loop Name] [Index]\n"
		"\n"
		"[MIDI]: Output MIDI name\n"
		"\n"
		"[Header/Index Name]: Name that fits Music_[xxx]_Headers/IndexOffs\n"
		"  e.g. \"Set1_Set2A\" or \"Set2B\"\n"
		"\n"
		"[Start/End/Loop Name]: Name that fits labels Music_[xxx]_Starts/End/Loops\n"
		"  e.g. \"Set2A\" or \"Set2B\"\n"
		"  NOTE: Can be omitted, will then only dump a single segment as specified\n"
		"        by [Index]; useful for Set 1 songs that do not have Start/End/Loop\n"
		"\n"
		*/

		"NOTE: TREAT THIS AS ALPHA SOFTWARE.  THIS SOFTWARE IS BUGGY AND DOESN'T\n"
		"PERFORM WELL WHEN THINGS AREN'T PERFECT.  Sorry about that.\n"
		"Who wants to write a beautiful GUI version of this idea with active\n"
		"previews etc.?  :)\n"
		"\n"
		);
}


int main(int argc, char *argv[])
{

	// Import:
	// "MusConv [MIDI] [ASM]"

	// Export:
	// "MusConv [MIDI] [Header/Index Name] [Start/End/Loop Name] [Index]"

	if(argc < 3)
	{
		//fprintf(stderr, "Mode indeterminate\n");
		usage();
	}
	else
	{
		// Initialize NoDice to get ROM data...
		if(!NoDice_Init())
		{
			fprintf(stderr, "Initialization failure: %s\n", NoDice_Error());

			NoDice_Shutdown();

			return 1;
		}

		if(argc == 3)	// Two parameters, need import mode
		{
			if(!MusConv_Import(argv[1], argv[2]))
				usage();
		}
		else if(argc > 3)
		{
			const char *SEL_name = NULL;
			unsigned char music_index = 0;

			// We know we have at least three args...
			if(argc == 5)	// 4 args, we have a SEL_name
				SEL_name = argv[3];

			// Music index is always last in any case
			music_index = (unsigned char)strtoul(argv[argc - 1], NULL, 0);

			if(!MusConv_Export(argv[1], argv[2], SEL_name, music_index))
				usage();
		}

		NoDice_Shutdown();
	}

	return 0;
}
