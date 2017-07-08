#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "NoDiceLib.h"
#include "MusConv.h"

// MIDI PHUN:
//
// At a timebase of 120 and a tempo of 120,
// 120 ticks represent 0.5 seconds of playback.
// B = 120, T = 120, L = 0.5s @ 120 ticks
//
// At a timebase of 240 and a tempo of 120,
// 240 ticks represent 0.5 seconds of playback.
// B = 240, T = 120, L = 0.5s @ 240 ticks
//
// At a timebase of 120 and a tempo of 240,
// 120 ticks represent 0.25 seconds of playback.
// B = 120, T = 240, L = 0.25s @ 120 ticks
//
// SO:	Timebase (divisions) represents how many ticks it takes 
//		to achieve 500,000ms (0.5s) of playback normally.
//
//		Tempo is a divisor of that function.  The actual length
//		of time achieved is (120 / Tempo) * 500,000ms
//
// For my 1/60th of a second general synchro, first you calculate the
// "time-per-tick", which is:
// TPT = (120 / Tempo * 0.5s) / Timebase
//
// TPT now represents how much time is spent on 1 tick.  With default
// values of 120 base and 120 tempo:
// TPT = (120 / 120 * 0.5s) / 120 = ~0.004167s per MIDI tick.
//
// Or perhaps with 240 tempo (etc.):
// TPT = (120 / 240 * 0.5s) / 120 = ~0.002083s per MIDI tick.
//
// So for any given tick amount, you simply multiply by TPT to 
// figure out its location in playback in seconds.
// Finally, using that seconds value, figure out how many 1/60th
// ticks are in that value!  That returns the relative integer
// position so desired by our playback system.  It's easy:
//
// ticks = 60 * (MIDI_ticks * TPT);
//
// Since if TPT = 0.004167 and the current specified MIDI_ticks are
// 120, you get:
//
// ticks = 60 * (120 * 0.004167) = 30 (rounded)
//
// UPDATE:	Sorta what I said, except I'm adding up the 1/60 ticks now
//			instead of just calculating them from the MIDI ticks
//			since that's better with tempo changes

static short readBE16(FILE *in)
{
	unsigned short s16;
	fread(&s16, sizeof(short), 1, in);

#ifdef LSB_FIRST
	return ((s16 & 0xFF00) >> 8) | (s16 << 8);
#else
	return s16;
#endif
}

static int readBE32(FILE *in)
{
	unsigned int s32;
	fread(&s32, sizeof(int), 1, in);

#ifdef LSB_FIRST
	return	((s32 & 0xFF000000) >> 24) |
			((s32 & 0x00FF0000) >> 8 ) |
			((s32 & 0x0000FF00) << 8 ) |
			((s32 & 0x000000FF) << 24);
#else
	return s32;
#endif
}



unsigned int ReadVarLen(FILE *infile)
{
	int value;
	unsigned char c;

	if ( (value = fgetc(infile)) & 0x80 )
	{
		value &= 0x7F;
		do
		{
			value = (value << 7) + ((c = fgetc(infile)) & 0x7F);
		} while (c & 0x80);
	}

	return(value);
}


static struct MTHD_CHUNK
{
   // Here's the 8 byte header that all chunks must have 
   char           ID[4];  // This will be 'M','T','h','d' 
   unsigned int  Length; // This will be 6 

   // Here are the 6 bytes 
   unsigned short Format;
   unsigned short NumTracks;
   unsigned short Division;
} mthd;

// This list tracks tempo changes
#define MAX_TEMPO_CHANGES	120		// *shrug*
static struct
{
	unsigned int tempo_wait;
	unsigned short tempo;
} TempoList[MAX_TEMPO_CHANGES];
unsigned int current_tempo = 0;

enum MUSICEVENT
{
	EVENT_NOTEON,
	EVENT_NOTEOFF,
	EVENT_PROGRAMCHANGE,
	EVENT_LOOP		// Special "LOOP" SysEx
};

static struct music_event_list
{
	unsigned int time;		// Literal time of event
	enum MUSICEVENT event;	// Event type
	unsigned char data;		// Event data (if needed)
	struct music_event_list *next;
}	*head_music_tracks[MUSICTRACK_TOTAL],
	*cur_track_event[MUSICTRACK_TOTAL] = { NULL };


static void music_event_list_add(enum MUSICTRACK track, unsigned int time, enum MUSICEVENT event, unsigned char data)
{
	struct music_event_list *music_event = (struct music_event_list *)malloc(sizeof(struct music_event_list));

	music_event->time = time;
	music_event->event = event;
	music_event->data = data;
	music_event->next = NULL;

	if(cur_track_event[track] != NULL)
		cur_track_event[track]->next = music_event;
	else
		head_music_tracks[track] = music_event;

	cur_track_event[track] = music_event;
}


static struct music_segment
{
	// Segment data/size
	unsigned char segment_data[256];	// Data belonging to segment
	unsigned short segment_data_size;	// Actual size of data ("short" so we can acknowledge size 256)

	// Track data
	struct music_segment_track
	{
		unsigned char track_start;		// Start offsets for this tracks
		unsigned int track_end_time;	// Termination time of this track
	} tracks[MUSICTRACK_TOTAL];

	// Start time for this segment entirely
	unsigned int start_time;			// The time when this segment begins!
	//unsigned int end_time;				// The time when this segment ends!
} music_segments[256] = { 0 };		// 256 should be more than enough since the segment counter is only 8-bit


static void segment_add_data(struct music_segment *segment, unsigned char data)
{
	if(segment->segment_data_size != 256)
		// Still room left...
		segment->segment_data[segment->segment_data_size++] = data;
	else
	{
		// Out of segment space; panic!!
		fprintf(stderr, "Segment %i is too large; increase partitions of your MIDI file!\n", segment - music_segments);
		exit(1);	// FIXME: Ought to be setjmp type deal
	}
}


// This will return the index into the rest table lookup row that 
// most closely matches the specified target_value, or 0xFF if there
// simply is no value small enough.  This should never happen if we
// employ a "good" row, like 0x40 in stock SMB3, which contains a
// value of 1, meaning even the smallest sliver should get an index!
static unsigned char rest_get_nearest_index(const unsigned char *rest_table_base_ptr, unsigned char target_value)
{
	unsigned char rest_index = 0;
	unsigned char closest_index = 0xFF, closest_diff = 0xFF;
	
	// Search for which of the rests in this "row" of the rest table
	// comes the closest (without overflowing) the desired value...
	for(rest_index = 0; rest_index < 16; rest_index++)
	{
		unsigned char table_val = rest_table_base_ptr[rest_index];

		// If we get lucky and find a perfect match, just use that!
		if(table_val == target_value)
		{
			closest_index = rest_index;
			break;
		}
		else if(table_val < target_value)
		{
			// Check if this one is nearest the target...
			unsigned char this_diff = target_value - table_val;

			// If this the smallest gap we've had ...
			if(this_diff < closest_diff)
			{
				// ... we'll possibly use this!
				closest_index = rest_index;
				closest_diff = this_diff;
			}
		}
	}

	return closest_index;
}


static void music_pad_rests(struct music_segment *segment, const unsigned char *rest_table_base_ptr, unsigned char cur_patch, unsigned char rest_length, int is_noise)
{
	unsigned char rest_note = !is_noise ? 0x7E : 0x01;

	// Perform a pattern of $Px, $7E to pad out a rest length
	while(rest_length > 0)
	{
		unsigned char nearest_index = rest_get_nearest_index(rest_table_base_ptr, rest_length);
		
		// With a "good" row in the rest table, this should never happen
		if(nearest_index == 0xFF)
		{
			fprintf(stderr, "WARNING: Unable to satisfy rest length of %i\n", rest_length);
			break;
		}

		// Deduct the nearest length
		rest_length -= rest_table_base_ptr[nearest_index];

		// Add the $Px, $7E to the stream
		segment_add_data(segment, (cur_patch & 0xF0) | nearest_index);
		segment_add_data(segment, rest_note);
	}
}


// This version of music_pad_rests uses the largest rest value available
// and "hacks away" at the desired period until it is <= 0.  This is
// acceptable for non-Square 2 tracks to be "run to the end" of their
// segment since Square 2's termination byte is the final word!
static void music_pad_rests_for_segment_end(struct music_segment *segment, const unsigned char *rest_table_base_ptr, unsigned char cur_patch, int rest_length, int is_noise)
{
	unsigned char rest_note = !is_noise ? 0x7E : 0x01;
	int i;

	// Find the largest wait value
	unsigned char largest_wait = 0x00, largest_wait_index;
	for(i = 0; i < 16; i++)
	{
		if(rest_table_base_ptr[i] > largest_wait)
		{
			largest_wait = rest_table_base_ptr[i];
			largest_wait_index = i;
		}
	}

	// Okay, now use that wait until we've exhausted rest_length
	while(rest_length > 0)
	{
		// Add the $Px, $7E to the stream
		segment_add_data(segment, (cur_patch & 0xF0) | largest_wait_index);
		segment_add_data(segment, rest_note);

		// Deduct this from rest_length
		rest_length -= largest_wait;
	}
}


int MusConv_Import(const char *input_midi_name, const char *output_asm_name)
{
	const unsigned char *rest_table;	// SMB3's limited timing system lookup table
	int segment_total = 1;				// Assuming there's always one segment
	unsigned short stop_time = 0xFFFF;	// Stop SysEx time (if any); default is not to stop early

	int track;
	FILE *midi = fopen(input_midi_name, "rb");

	unsigned int tempo;
	float TPT;  // See "MIDI PHUN" description at top

	if( (rest_table = NoDice_get_rest_table()) == NULL)
	{
		fprintf(stderr, NoDice_Error());
		return 0;
	}

	if(!config_load())
		return 0;

	if(midi == NULL)
	{
		fprintf(stderr, "Failed to open %s for reading\n", input_midi_name);
		return 0;
	}

	fread(mthd.ID, sizeof(char), 4, midi);
	mthd.Length = readBE32(midi);
	mthd.Format = readBE16(midi);
	mthd.NumTracks = readBE16(midi);
	mthd.Division = readBE16(midi);

	if(mthd.Format == 0)
	{
		fprintf(stderr, "Only MIDI format 1 supported!\n");
		return 0;
	}

	printf("MIDI: %d tracks\n",mthd.NumTracks);

	// Using specific mapping of track to channel
	if(mthd.NumTracks > MUSICTRACK_TOTAL+1)
	{
		fprintf(stderr, "Maximum tracks exceeded (%i/%i -- including Track 0)\n", mthd.NumTracks, MUSICTRACK_TOTAL+1);
		return 0;
	}

	tempo = 120;
	TPT = (120.0f / (float)tempo * 0.5f) / (float)mthd.Division;
	TempoList[0].tempo_wait = 0xFFFFFFFF; // just in case

	for(track=0; track < mthd.NumTracks; track++)
	{
		int len, track_end;
		char chunk_buf[4];

		unsigned int notes_played_this_track = 0;
		unsigned char track_volume = 127;
		unsigned int ongoing_time_total = 0;
		unsigned short actual_1_60th_ticks = 0;
		double ongoing_time_total_float = 0.0f;

		unsigned char midi_event, midi_channel;
		unsigned char byte1, byte2;
		unsigned char active_event = 0;
		unsigned char process_event = 0;

		fread(chunk_buf,1,4,midi);
		if (strncmp(chunk_buf, "MTrk", 4))
		{
			printf("Expected MTrk, found %.4s, aborting!",chunk_buf);
			return 0;
		}


		// Read each track...
		len = readBE32(midi);

		if(len >= 1000000)
		{
			printf("Something rediculous.  Aborting.\n");
			return 0;
		}

		// Calculate track end
		track_end = ftell(midi) + len;

		while(ftell(midi) < track_end)
		{
			// Get that funky variable length whats-it
			len = ReadVarLen(midi);

			ongoing_time_total += len; // In case an event doesn't actually get used

			// For all tracks above 0, make sure to apply tempo changes!
			if(track > 0)
			{
				// Is it time for a tempo change?
				if(ongoing_time_total >= TempoList[current_tempo].tempo_wait)
				{	
					// Assign new tempo
					tempo = TempoList[current_tempo].tempo;

					// Calculate new time-per-tick
					// (120 / tempo * 0.5s) / timebase
					TPT = (120.0f / (float)tempo * 0.5f) / (float)mthd.Division;

				
					// Ready for possible next tempo...
					current_tempo++;
				}
			}

			ongoing_time_total_float += (len * TPT);
			actual_1_60th_ticks = (unsigned short)(60.0f * ongoing_time_total_float);

			// Support of "STOP" SysEx
			if(actual_1_60th_ticks >= stop_time)
				goto TrackEnd;

			midi_event = fgetc(midi);

			// Events:
			// 0x80 - 0x8F = Note-off
			// 0x90 - 0x9F = Note-on
			// 0xA0 - 0xAF = Polyphonic key aftertouch
			// 0xB0 - 0xBF = Control-change
			// 0xC0 - 0xCF = Program-change
			// 0xD0 - 0xDF = Channel-aftertouch
			// 0xE0 - 0xEF = Pitch bend change
			// 0xF0 - 0xFF = System (specifically, F0 is Sysex, FF is a meta event...)

			/*
			Note On		NoteOn/Channel	Note Number	Velocity (loudness)
					90	0 - 127		0 - 127

			Note Off	NoteOff/Channel	Note Number	Velocity
					80	0 - 127		0

			Program Change	PChange/Channel	Program Number	--------------
					C0	0 - 127		

			Controller	Ctrl/Channel	Controller Num	Control Value
					B0	0 - 127		0 - 127

			Pitch Bend	P.Bend/Channel	P. Bend Amount	P. Bend Amount
					E0	0 - 127		0 - 127
			*/

			if(midi_event & 0x80)
			{
				// This is a MIDI event...
				if(midi_event >= 0x80 && midi_event <= 0xEF)
				{
					midi_channel = midi_event & 0x0F;
					midi_event &= 0xF0; 
				}

				// If this is an event, set as new active event and event we will process
				active_event = midi_event;
				process_event = midi_event;
			}
			else
			{
				// This is a data value, so put the byte back and we'll pick it up 
				// in event processing...
				fseek(midi, -1, SEEK_CUR);
				process_event = active_event;
			}

			switch(process_event)
			{
			case 0x80: // NOTE OFF
				// This event is actually a stupid one...
				// Only because it carries a dud byte (where volume is in note on, it's 0 here)
				byte1 = fgetc(midi);
				byte2 = fgetc(midi);

				if(track == 0)
				{
					printf("Warning: No note events supported in track 0!\n");
					break;
				}
				else if((track - 1) == MUSICTRACK_NOISE)
				{
					// Remap noise "note"
					// If result is zero, note will be dropped
					if(import_noise_map[byte1].map == 0)
					{
						// No need to say it twice
						//printf("WARNING: Dropping note %i on Noise track due to mapping\n", byte1);
						break;
					}
					else
						byte1 = import_noise_map[byte1].map;
				}
				else if((track - 1) == MUSICTRACK_DMC)
				{
					// Remap percussion "note"
					// If result is zero, note will be dropped
					if(import_percussion_map[byte1].map == 0)
					{
						// No need to say it twice
						//printf("WARNING: Dropping note %i on DCM track due to mapping\n", byte1);
						break;
					}
					else
						byte1 = import_percussion_map[byte1].map;
				}

				// Do a note-off only if this is the last note-off to all playing
				// notes.  (Actually, this is a kludge, and won't work if some 
				// huge note plays over top of everything else.  But if you let that
				// happen, shame on you.  =P)
Handle_NoteOff:;
				if(notes_played_this_track == 1)
				{
					music_event_list_add(track - 1, actual_1_60th_ticks, EVENT_NOTEOFF, 0);
				}

				notes_played_this_track--;
			
				break;

			case 0x90: // NOTE ON (0x90 - 0x9F)
				byte1 = fgetc(midi);
				byte2 = fgetc(midi);

				if(track == 0)
				{
					printf("Warning: No note events supported in track 0!\n");
					break;
				}
				else if((track - 1) == MUSICTRACK_NOISE)
				{
					// Remap noise "note"
					// If result is zero, note will be dropped
					if(import_noise_map[byte1].map == 0)
					{
						printf("WARNING: Dropping note %i on Noise track due to mapping\n", byte1);
						break;
					}
					else
						byte1 = import_noise_map[byte1].map;
				}
				else if((track - 1) == MUSICTRACK_DMC)
				{
					// Remap percussion "note"
					// If result is zero, note will be dropped
					if(import_percussion_map[byte1].map == 0)
					{
						printf("WARNING: Dropping note %i on DCM track due to mapping\n", byte1);
						break;
					}
					else
						byte1 = import_percussion_map[byte1].map;
				}

				if(byte2 > 0)
				{
					music_event_list_add(track - 1, actual_1_60th_ticks, EVENT_NOTEON, byte1);
				
					notes_played_this_track++;
				}
				else	// 0 volume Note-On's are some sequencer's Note-Off's
					goto Handle_NoteOff;

				//event_print("%i NOTE ON note = %i  volume = %i\n", actual_1_60th_ticks, byte1, byte2);

				break;

			case 0xB0: // CONTROLLER
				byte1 = fgetc(midi);
				byte2 = fgetc(midi);

				/*
				switch(byte1)
				{
				case 0x07: // Volume
					track_volume = byte2;

					event_print("%i: VOLUME CHANGE: %i\n", actual_1_60th_ticks, track_volume);

					break;
				}
				*/

				break;

			case 0xC0: // PROGRAM CHANGE
				byte1 = fgetc(midi);

				if((track - 1) != MUSICTRACK_SQUARE1 && (track - 1) != MUSICTRACK_SQUARE2)
				{
					printf("Warning: Program changes supported only on track 1 (Square 2) and track 2 (Square 1)!\n");
					break;
				}

				// Do remapping
				byte1 = import_patch_map[byte1].map;

				music_event_list_add(track - 1, actual_1_60th_ticks, EVENT_PROGRAMCHANGE, byte1);

				break;

			case 0xE0: // PITCH BEND (May want this...)
				byte1 = fgetc(midi);
				byte2 = fgetc(midi);

				//event_print("%i: PITCH BEND\n", actual_1_60th_ticks);

				break;

			case 0xF0: // SysEx
				{
					char buffer[7];

					// There's only one SysEx we care about right now
					// and that's the SEGEND custom SysEx to define
					// when a segment split should occur; also, must
					// occur on Track 0!

					// Length of SysEx
					len = ReadVarLen(midi); 

					// SysEx we care about is S,E,G,E,N,D,0xF7 (<-- SysEx terminator)
					if(len == 7)
					{
						// Need to read this in to make sure it's our SysEx!
						fread(buffer, sizeof(char), len, midi);

						if(track == 0)
						{
							if(!strncmp(buffer, "SEGEND", 6))
							{
								// This is ours!  Record when this new segment starts
								music_segments[segment_total++].start_time = actual_1_60th_ticks;
							}
						}
						else
							printf("SEGEND SysEx only works in Track 0\n");
					}
					else if(len == 5)
					{
						// Need to read this in to make sure it's our SysEx!
						fread(buffer, sizeof(char), len, midi);

						if(!strncmp(buffer, "STOP", 4))
						{
							if(track == 0)
							{
								// This is ours!  Record when the stop occurs
								if(stop_time == 0xFFFF)
								{
									printf("WARNING: STOP SysEx detected; will stop processing at %f\n", ongoing_time_total_float);
									stop_time = actual_1_60th_ticks;
								}
								else
									printf("WARNING: Multiple STOP SysExs; only first one is to take effect\n");
							}
							else
								printf("WARNING: STOP SysEx only works in Track 0\n");
						}
						else if(!strncmp(buffer, "LOOP", 4))
						{
							if((track - 1) == MUSICTRACK_NOISE || (track - 1) == MUSICTRACK_DMC )
							{
								music_event_list_add(track - 1, actual_1_60th_ticks, EVENT_LOOP, 0);
							}
							else
								printf("WARNING: LOOP SysEx only applies to Noise and DCM tracks; ignoring on track %i.\n", track);
						}
					}
					else
					{
						// I don't CARE what the data is, so let's jump it!
						fseek(midi, len, SEEK_CUR);
					}

					break;
				}

			case 0xFF: // Meta event

				// Type of SysEx
				byte1 = fgetc(midi);

				switch(byte1)
				{
				case 0xFF:
				case 0x2F:	// End of track!

TrackEnd:;
					// Reset tempo table
					current_tempo = 0;

					// Set segment-after-last start time to end of track,
					// if this is the greatest time thus far, so we have
					// a proper finale end time for the last real segment
					if(music_segments[segment_total].start_time < actual_1_60th_ticks)
						music_segments[segment_total].start_time = actual_1_60th_ticks;

					break;

				case 0x51: // Tempo change
					fgetc(midi); // Always 3 (length of tempo value)

					// This is just to GET the tempo value
					byte1 = fgetc(midi);
					tempo = (byte1 * 0x10000L);

					byte1 = fgetc(midi);
					tempo += (byte1 * 0x100);

					byte1 = fgetc(midi);
					tempo += (byte1);

					// Calculate relative MIDI tempo
					tempo = 60000000 / tempo;

					// Calculate new time-per-tick
					// (120 / tempo * 0.5s) / timebase
					TPT = (120.0f / (float)tempo * 0.5f) / (float)mthd.Division;

					// Store this tempo change into the change list
					if(track != 0)
						printf("Warning: Tempo change outside of track 0 not supported!\n");
					else
					{
						TempoList[current_tempo].tempo = tempo;
						TempoList[current_tempo].tempo_wait = ongoing_time_total;

						current_tempo++;

						// just to prevent a false tempo from ever being applied
						TempoList[current_tempo].tempo_wait = 0xFFFFFFFF;
					}

					//event_print("%i: TEMPO %i\n", actual_1_60th_ticks, tempo);

					break;
				default:
					{
						len = ReadVarLen(midi); // Length

						// I don't CARE what the data is, so let's jump it!
						fseek(midi, len, SEEK_CUR);
					}
					break;
				}

				break;
			default:
				byte1 = fgetc(midi);
				byte2 = fgetc(midi);

				printf("Error: Don't know how to handle event %02X (%02X, %02X) on track %d!\n",midi_event,byte1,byte2,track);
				return 0;
			}

			// Support of "STOP" SysEx
			if(actual_1_60th_ticks >= stop_time)
				break;		
		}

		// Make sure we're in the right spot
		fseek(midi, track_end, SEEK_SET);
	}

	fclose(midi);


	// Post-processing
	{
		const unsigned char rest_table_base = 0x40;	// FIXME: A good one, but should be configurable
		const unsigned char *rest_table_base_ptr = rest_table + rest_table_base;
		unsigned char cur_patch_per_track[MUSICTRACK_TOTAL] = { 0x80, 0x80, 0x80, 0x80, 0x80 };	// Really only matters for Square 1 and 2

		int i;

		FILE *out = fopen(output_asm_name, "w");

		if(out == NULL)
		{
			fprintf(stderr, "Failed to open %s for writing\n", output_asm_name);
			return 0;
		}

		// Reinitialize the cur_track_event pointers; these will be reused
		// as position holders for the tracks...
		for(i = 0; i < MUSICTRACK_TOTAL; i++)
			cur_track_event[i] = head_music_tracks[i];

		// Now we take the simplified events and restructure them into SMB3
		// compatible format data...
		for(i = 0; i < segment_total; i++)
		{
			struct music_segment *segment = &music_segments[i];
			unsigned int last_segment_end = (i > 0) ?
				music_segments[i].start_time : 0;

			// Sets whether a track played anything this time around,
			// thus "enabled"; unfortunately by SMB3 design, both
			// Squares must always be enabled...
			unsigned char track_en[MUSICTRACK_TOTAL] = { 1, 1 };

			// Process all events that fit -this- segment
			for(track = 0; track < MUSICTRACK_TOTAL; track++)
			{
				int loop_trigger = 0;	// When set, loop event occurred, so run out segment
				unsigned char rest_note = (track != MUSICTRACK_NOISE) ? 0x7E : 0x01;	// proper "rest" "note"
				unsigned char cur_patch = cur_patch_per_track[track];
				
				// Time when last recorded event occurred; resume from previous
				// segment, if any...
				unsigned int time_last_event = last_segment_end;	

				// Starts at beginning or resumes where left off
				struct music_event_list *events = cur_track_event[track];


				// Big thing to remember in SMB3 is that there's really only
				// two types of bytes (in regular music flow); the patch/delay 
				// byte and the Note On/Off byte... so basically we're living
				// from "Note On to Note On", and whatever distance we need
				// to get to the next one...

				// The other thing is that the bytes that come before Note On
				// specify how long that Note On is going to be!

				// Process as many events as we can until ...
				while( (events != NULL) &&	// ... we run out ...
						(events->time < music_segments[i + 1].start_time) // ... or hit a segment split
						)
				{
					// Run to segment end (or out of events)
					if(loop_trigger)
					{
						events = events->next;
						continue;
					}

					// Good time to mark track as enabled since it's doing something
					track_en[track] = 1;

					// If we hit a loop, just write the loop byte and run to the 
					// segement end (ignore further events)
					if(events->event == EVENT_LOOP)
					{
						// Pad to loop byte
						music_pad_rests(segment, rest_table_base_ptr, cur_patch, events->time - time_last_event, track == MUSICTRACK_NOISE);

						// Loop byte (MIDI pre-processing prevents this from 
						// being on a bad track, so no worries?)
						segment_add_data(segment, 0x00);

						// Loop trigger; no more event processing this segment!
						loop_trigger = 1;
						continue;
					}
					// Program Changes we'll just absorb into cur_patch
					else if(events->event == EVENT_PROGRAMCHANGE)
					{
						if(track == MUSICTRACK_SQUARE1 || track == MUSICTRACK_SQUARE2)
						{
							cur_patch = ((events->data << 4) & 0x70) | 0x80;
						}
						else
							// No other tracks support patch changes
							cur_patch = 0x80;

						cur_patch_per_track[track] = cur_patch;
					}
					else if(events->event == EVENT_NOTEON)
					{
						struct music_event_list *next_event = events->next;

						// Must pad rests until we meet the current Note On
						music_pad_rests(segment, rest_table_base_ptr, cur_patch, events->time - time_last_event, track == MUSICTRACK_NOISE);

						// Make sure the next event we're considering is s note event
						// Although there probably won't be spurious program changes
						// in the middle of a note, this will handle that situation
						while(next_event != NULL && 
							(next_event->event != EVENT_NOTEOFF && next_event->event != EVENT_NOTEON) )
							next_event = next_event->next;

						if(next_event != NULL)
						{
							short adjusted_note = events->data;

							// Look forward to next event to get delay delta
							// "delta" specifies how long the Note On
							// is going to be
							int delta = next_event->time - events->time;

							// Get nearest match value for this note; unfortunately,
							// due to limited accuracy, this may get cut short...
							unsigned char nearest_index = rest_get_nearest_index(rest_table_base_ptr, delta);

							// For non-Noise/DCM tracks, range limit notes!
							if(track < MUSICTRACK_NOISE)
							{
								// Notes in SMB3 are double size and based at 36 ahead
								// from MIDI, so we adjust it now... "short" catches range
								adjusted_note = (events->data - 36) * 2;

								if(adjusted_note < 0x01)
									adjusted_note = 0x01;
								else if(adjusted_note > 0x7D)
									adjusted_note = 0x7D;
							}
							else
							{
								// Can't really tell the max note from here,
								// but we can stop it from being 0x00
								// which indicates loop on these tracks
								if(adjusted_note < 0x01)
									adjusted_note = 0x01;
							}

							// Add $Px and Note On
							segment_add_data(segment, (cur_patch & 0xF0) | nearest_index);
							segment_add_data(segment, (unsigned char)adjusted_note);

							time_last_event = events->time + rest_table_base_ptr[nearest_index];
						}
					}

					// Next event...
					events = events->next;
				}

				// END OF TRACK 

				// Mark what event we stopped on
				cur_track_event[track] = events;

				// This time should be at the end of the last Note On
				// event that occurred on this track.  Note that this
				// will be used for "cleanup" of segments later as the
				// stop point of this segment.  This means that you
				// will cause a problem if you have a note that straddles
				// the segment split SysEx on any track.
				// So don't do that ... :P
				segment->tracks[track].track_end_time = time_last_event;

				// This also tells us where to put the start position for
				// the next track (though may change when we "clean up" later)
				if(track < MUSICTRACK_DMC)
					segment->tracks[track + 1].track_start = (unsigned char)segment->segment_data_size;
			}


			/// END OF SEGMENT

			// Finally, we must tidy up our segment, which means padding
			// tracks that don't line up at the end and adding the terminator
			// to Square Channel 2.
			{
				unsigned char temp_seg_buffer[256];
				unsigned int end_time;

				// Use following segment for the end time; this works even
				// for the last segment 
				end_time = music_segments[i + 1].start_time;

				// Next, we must go through all tracks and pad them so that
				// they meet up with this time (since SMB3 does not provide
				// termination on tracks other than the segment-termination
				// byte on Square 2.)
				for(track = 0; track < MUSICTRACK_TOTAL; track++)
				{
					// If a non-square track didn't play anything, it is
					// disabled, and thus gets a magic offset of zero
					if(!track_en[track])
					{
						// This is a non-square track with nothing on it!
						segment->tracks[track].track_start = 0;
					}
					else
					{
						// The trick is, segment data is just one big blob, but
						// we need to insert these delay items... so get the
						// current end of the data and we know how to move things 
						// around once the inserting is done
						unsigned char current_end = (unsigned char)segment->segment_data_size;
						unsigned char new_end;

						// For all tracks except the one who had the "end_time"...
						if(segment->tracks[track].track_end_time != end_time)
						{
							// Get amount of time we must delay
							int time_diff = end_time - segment->tracks[track].track_end_time;

							// For all tracks besides Square 2, we can use an imprecise
							// method of delay that just uses the largest value until
							// we meet the minimum; this saves on bytes and is perfectly
							// valid because Square 2 is the final say in when the segment
							// has come to an end...
							if(track != MUSICTRACK_SQUARE2)
							{
								int pad_ok = 1;

								// Do not pad rests on percussion tracks that used the loop byte
								if(track == MUSICTRACK_NOISE || track == MUSICTRACK_DMC)
								{
									pad_ok = segment->segment_data[segment->segment_data_size - 1] != 0x00;
								}

								if(pad_ok)
									music_pad_rests_for_segment_end(segment, rest_table_base_ptr, 0x80, time_diff, track == MUSICTRACK_NOISE);
							}
							else
								// Square 2 must be precise, however, since it determines
								// exactly when the segment has ended...
								music_pad_rests(segment, rest_table_base_ptr, 0x80, time_diff, track == MUSICTRACK_NOISE);
						}

						// Now's a good time to terminate the segment!
						if(track == MUSICTRACK_SQUARE2)
							segment_add_data(segment, 0x00);

						// Reorganize the data so it is correct
						new_end = (unsigned char)segment->segment_data_size;

						// Obviously don't do anything if no new data was added
						// Also no need to move anything if we're on the DMC
						// track since that is already the final data...
						if( (current_end < new_end) && (track < MUSICTRACK_DMC) )
						{
							int future_tracks;

							// Calculate copy size and get next track's starting index
							unsigned char copy_size = new_end - current_end;
							unsigned char next_track_start = segment->tracks[track + 1].track_start;

							// Copy the newly appended data into the temporary buffer
							memcpy(temp_seg_buffer, &segment->segment_data[current_end], copy_size);

							// Move data starting at the next track over
							memmove(&segment->segment_data[next_track_start + copy_size],
								&segment->segment_data[next_track_start], 
								current_end - next_track_start);

							// Reinsert the new data back at the end of the track we're modifying
							memcpy(&segment->segment_data[next_track_start], temp_seg_buffer, copy_size);

							// Finally, the future tracks must have their starts shifted over
							for(future_tracks = track + 1; future_tracks < MUSICTRACK_TOTAL; future_tracks++)
							{
								segment->tracks[future_tracks].track_start += copy_size;
							}
						}
					}
				}
			}

		}


		// Write out Segment data
		fputs("; Place these segment data blocks where space is available in the bank\n\n", out);
		{
			for(i = 0; i < segment_total; i++)
			{
				int data_byte, col = 0;
				struct music_segment *segment = &music_segments[i];

				fprintf(out, "MXXSegData%02X:\n", i);

				for(data_byte = 0; data_byte < segment->segment_data_size; data_byte++)
				{
					if(col == 16)
					{
						fputs("\n", out);
						col = 0;
					}

					if(col == 0)
						fprintf(out, "\t.byte ");

					fprintf(out, "$%02X", segment->segment_data[data_byte]);
					col++;

					if( (data_byte < (segment->segment_data_size - 1)) && (col < 16) )
						fputs(", ", out);
				}

				fputs("\n\n", out);
			}
		}


		// Write out segment headers
		fputs("; Place these segment headers in the Music_xxx_Headers section\n\n", out);

		for(i = 0; i < segment_total; i++)
		{
			struct music_segment *seg = &music_segments[i];

			fprintf(out, "\t\tMusSeg $%02X, MXXSegData%02X, $%02X, $%02X, $%02X, $%02X\n", 
				rest_table_base, 
				i, 
				seg->tracks[MUSICTRACK_TRIANGLE].track_start,
				seg->tracks[MUSICTRACK_SQUARE1].track_start, 
				seg->tracks[MUSICTRACK_NOISE].track_start,
				seg->tracks[MUSICTRACK_DMC].track_start);
		}

		fclose(out);
	}

	// Free all event allocations
	for(track = 0; track < MUSICTRACK_TOTAL; track++)
	{
		struct music_event_list *events = head_music_tracks[track];

		while(events != NULL)
		{
			struct music_event_list *next_event = events->next;
			free(events);
			events = next_event;
		}
	}

	return 1;
}
