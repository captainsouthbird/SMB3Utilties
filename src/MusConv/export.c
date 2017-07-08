#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "NoDiceLib.h"
#include "MusConv.h"

#define NODICE_MUSICMIDI_TIMESCALE	8
#define MAX_MIDI_EVENTS		8192

struct smb3_track
{
	// Running data
	const unsigned char *data_ptr;	// Pointer to segment data
	unsigned char cur_note;			// Current note playing (for note off)
	unsigned char cur_hold;			// Current rest value

	// Playing data
	unsigned char hold_remain;		// Rest time remaining
	unsigned short time_total;		// Time accumulating (not delta like MIDI uses)

	struct smb3_midi_event
	{
		unsigned int time;
		unsigned char event;
		unsigned char bytes[16];
		unsigned char byte_len;
	} midi_events[MAX_MIDI_EVENTS];
	struct smb3_midi_event *midi_event;

} smb3_tracks[MUSICTRACK_TOTAL] = { NULL };


static void writeBE16(FILE *out, short s16)
{
#ifdef LSB_FIRST
	s16 = ((s16 & 0xFF00) >> 8) | (s16 << 8);
#endif

	fwrite(&s16, sizeof(short), 1, out);
}

static void writeBE32(FILE *out, int s32)
{
#ifdef LSB_FIRST
	s32	=	((s32 & 0xFF000000) >> 24) |
			((s32 & 0x00FF0000) >> 8 ) |
			((s32 & 0x0000FF00) << 8 ) |
			((s32 & 0x000000FF) << 24);
#endif

	fwrite(&s32, sizeof(int), 1, out);
}


void WriteVarLen(FILE *outfile, unsigned int value)
{
	register unsigned int buffer;
	buffer = value & 0x7F;

	while ( (value >>= 7) )
	{
		buffer <<= 8;
		buffer |= ((value & 0x7F) | 0x80);
	}

	while (1)
	{
		fputc(buffer,outfile);
		if (buffer & 0x80)
			buffer >>= 8;
		else
			break;
	}
}



static void midi_write_setup(struct smb3_track *track, unsigned char *midi_channel)
{
	int cur_track = track - smb3_tracks;
	int cur_event = track->midi_event - track->midi_events;

	// Be percussive on the percussive tracks
	*midi_channel = (cur_track < MUSICTRACK_NOISE) ? cur_track : 9;

	if(cur_event >= MAX_MIDI_EVENTS)
	{
		printf("Out of MIDI event space!!");
		exit(1);
	}
}


static void midi_write_program_change(struct smb3_track *track, unsigned char program_change)
{
	unsigned char midi_channel;
	midi_write_setup(track, &midi_channel);

	//printf("[%i %i] PROGRAM CHANGE %02X\n", midi_channel, track->time_total, program_change);

	// Program change
	track->midi_event->time = track->time_total;
	track->midi_event->event = 0xC0 | midi_channel;
	track->midi_event->bytes[0] = program_change;
	track->midi_event->byte_len = 1;
	track->midi_event++;
}


static void midi_write_note_off(struct smb3_track *track)
{
	unsigned char midi_channel;
	midi_write_setup(track, &midi_channel);

	//printf("[%i %i] NOTE OFF %02X\n", midi_channel, track->time_total, track->cur_note);

	track->midi_event->time = track->time_total;
	track->midi_event->event = 0x80 | midi_channel;
	track->midi_event->bytes[0] = track->cur_note;
	track->midi_event->bytes[1] = 0x00;
	track->midi_event->byte_len = 2;
	track->midi_event++;

	track->cur_note = 0;
}


static void midi_write_note_on(struct smb3_track *track, unsigned char note)
{
	unsigned char midi_channel;
	midi_write_setup(track, &midi_channel);

	//printf("[%i %i] NOTE ON %02X\n", midi_channel, track->time_total, note);

	track->midi_event->time = track->time_total;
	track->midi_event->event = 0x90 | midi_channel;
	track->midi_event->bytes[0] = note;
	track->midi_event->bytes[1] = 0x7F;	// volume
	track->midi_event->byte_len = 2;
	track->midi_event++;

	track->cur_note = note;
}

static void midi_write_sysex_track0(FILE *midi, unsigned int time, const char *sysex)
{
	static unsigned int prev_time = 0;

	// Write delta time
	WriteVarLen(midi, (time - prev_time) * NODICE_MUSICMIDI_TIMESCALE);
	prev_time = time;

	// SysEx
	fputc(0xF0, midi);
	
	// Size
	WriteVarLen(midi, strlen(sysex) + 1);	// +1 includes terminator

	// SysEx itself
	fputs(sysex, midi);

	// Terminator
	fputc(0xF7, midi);
}


static void midi_write_sysex(struct smb3_track *track, const char *sysex)
{
	int sysex_len = (int)strlen(sysex);

	if(sysex_len > sizeof(track->midi_event->bytes) - 2)
	{
		printf("WARNING: midi_write_sysex input size too large; ignoring\n");
		return;
	}

	// SysEx
	track->midi_event->time = track->time_total;
	track->midi_event->event = 0xF0;
	
	// Size
	track->midi_event->bytes[0] = sysex_len;

	// SysEx
	memcpy(&track->midi_event->bytes[1], sysex, sysex_len);

	// Terminator
	track->midi_event->bytes[sysex_len+2] = 0xF7;

	track->midi_event->byte_len = sysex_len + 1;
	track->midi_event++;
}


int MusConv_Export(const char *midi_out_filename, const char *header_index_name, const char *SEL_name, unsigned char music_index)
{
	int cur_track;
	int length_start, length_end;
	struct NoDice_music_context music_context;
	int cur_segment;
	FILE *out = fopen(midi_out_filename, "w");

	if(out == NULL)
	{
		fprintf(stderr, "Failed to open %s for writing\n", midi_out_filename);
		return 0;
	}

	fputs("MThd", out);
	writeBE32(out, 6);

	writeBE16(out, 1);		// MIDI Format 1
	writeBE16(out, MUSICTRACK_TOTAL+1);		// Number of tracks
	writeBE16(out, 120);	// Timebase

	// Track 0: Write tempo event for 240
	fputs("MTrk", out);

	// Length placeholder
	length_start = ftell(out);
	writeBE32(out, 0);

	// Port metaevent
	WriteVarLen(out, 0);
	fputc(0xFF, out);
	fputc(0x21, out);
	fputc(0x01, out);
	fputc(0x00, out);

	// Tempo
	WriteVarLen(out, 0);
	fputc(0xFF, out);
	fputc(0x51, out);
	fputc(0x03, out);	// Length of tempo value
	fputc(0x03, out);
	fputc(0xD0, out);
	fputc(0x90, out);	// 0x03D090 = 250000 ... 60000000 / 250000 = 240
	

	// With timebase 120 and tempo 240, there's 480 ticks a second
	// Each event holds a delta to the next (not time)

	if(!NoDice_get_music_context(&music_context, header_index_name, SEL_name, music_index))
	{
		fprintf(stderr, "Failed to get music context: %s\n", NoDice_Error());
		return 0;
	}

	// Add initial events
	for(cur_track = 0; cur_track < MUSICTRACK_TOTAL; cur_track++)
	{
		struct smb3_track *track = &smb3_tracks[cur_track];
		unsigned char program_change = 
			(cur_track == MUSICTRACK_SQUARE1 || cur_track == MUSICTRACK_SQUARE2) ? 80 :
			(cur_track == MUSICTRACK_TRIANGLE) ? 18 :
			0;

		// Be percussive on the percussive tracks
		unsigned char midi_channel = (cur_track < MUSICTRACK_NOISE) ? cur_track : 9;

		track->midi_event = track->midi_events;

		// Port metaevent
		track->midi_event->time = 0;
		track->midi_event->event = 0xFF;
		track->midi_event->bytes[0] = 0x21;
		track->midi_event->bytes[1] = 0x01;
		track->midi_event->bytes[2] = 0x00;
		track->midi_event->byte_len = 3;
		track->midi_event++;

		// Program change
		midi_write_program_change(track, program_change);
	}

	for(cur_segment = 0; cur_segment < music_context.music_segment_count; cur_segment++)
	{
		int loop_trigger = 0;	// Set when LOOP event is written so we only do it once per segment
		int is_playing = 1;
		struct NoDice_music_segment *segment = &music_context.music_segments[cur_segment];

		// Initialize track pointers for new segment
		for(cur_track = 0; cur_track < MUSICTRACK_TOTAL; cur_track++)
		{
			struct smb3_track *track = &smb3_tracks[cur_track];
			track->data_ptr = segment->segment_data + segment->track_starts[cur_track];

			// SMB3 music engine always re-initializes all holds to 1
			// (they are immediately decremented) on every segment change
			track->hold_remain = 1;

			// Sync track times to SQUARE2 because they should always all start together
			if(cur_track != MUSICTRACK_SQUARE2)
			{
				// This actually is not unacceptable
				//if(track->time_total > smb3_tracks[MUSICTRACK_SQUARE2].time_total)
				//	printf("WARNING: Track %i got ahead of Square 2 on segment change!  (%i vs %i)\n", cur_track, track->time_total, smb3_tracks[MUSICTRACK_SQUARE2].time_total);

				track->time_total = smb3_tracks[MUSICTRACK_SQUARE2].time_total;
			}
			else if(cur_segment > 0)
			{
				// Since Square Track 2 is the timing dictator, we'll use its
				// time to place the SEGEND SysEx...

				// Write segment end SysEx
				midi_write_sysex_track0(out, smb3_tracks[MUSICTRACK_SQUARE2].time_total, "SEGEND");
			}

		}

		while(is_playing)
		{
			for(cur_track = 0; cur_track < MUSICTRACK_TOTAL; cur_track++)
			{
				struct smb3_track *track = &smb3_tracks[cur_track];
				unsigned char byte;
				
				// Be percussive on the percussive tracks
				unsigned char midi_channel = (cur_track < MUSICTRACK_NOISE) ? cur_track : 9;

				// SMB3 music engine also does immediate decrement, so we imitate that
				if(--track->hold_remain > 0)
					continue;

GetAnotherByte:;
				byte = *track->data_ptr++;

				if(byte & 0x80)
				{
					if( (byte == 0xFF) && ((cur_track == MUSICTRACK_SQUARE1) || (cur_track == MUSICTRACK_SQUARE2)) )
					{
						// If an 0xFF appears prior to a Note On, it is considered invalid,
						// so do nothing here but go fetch another byte...
						goto GetAnotherByte;
					}
					else
					{
						if( (cur_track == MUSICTRACK_SQUARE1) || (cur_track == MUSICTRACK_SQUARE2)) 
						{
							// Patch
							unsigned char patch_to_midi[8] =
							{
								80,		// Patch 0: Long square
								0,		// Patch 1: Short "piano" (?) World 1 sound
								13,		// Patch 2: Short square
								50,		// Patch 3: Really short square
								40,		// Patch 4: Wavey sound (string-like, water sound)
								6,		// Patch 5: Short "harpshichord" (similar to World 1)
								0,		// Patch 6: Same as Patch 1
								45,		// Patch 7: Pizzicato string sound ("plucking")
							};

							// Program change
							midi_write_program_change(track, patch_to_midi[ (byte & 0x70) >> 4 ]);
						}

						// Load new hold value
						track->cur_hold = music_context.rest_table[music_context.rest_table_base + (byte & 0xF)];
					}

					byte = *track->data_ptr++;
				}


Handle7bit:;
				// Note always follows a bit 7 type
				if(byte == 0x00)
				{
					if(cur_track == MUSICTRACK_SQUARE1)
					{
						// Ramp effect

						// This is considered atomic with the Note On, so fetch another byte
						goto GetAnotherByte;
					}
					else if(cur_track == MUSICTRACK_SQUARE2)
					{
						// End of segment
						is_playing = 0;
					}
					else if(cur_track == MUSICTRACK_TRIANGLE)
					{
						// Triangle stops on 0x00
						midi_write_note_off(track);
					}
					else
					{
						// Noise and DPCM just restart
						track->data_ptr = music_context.music_segments[cur_segment].segment_data + music_context.music_segments[cur_segment].track_starts[cur_track];
						
						// Write LOOP SysEx (only once per segment)
						if(!loop_trigger)
						{
							midi_write_sysex(track, "LOOP");
							loop_trigger = 1;
						}

						// Jump back to get another byte
						goto GetAnotherByte;
					}
				}
				else
				{
					// Noise uses 0x01 as Note Off, so we have to handle that...
					if( (byte <= 0x7D) && ((cur_track != MUSICTRACK_NOISE) || byte != 0x01) )
					{
						if(track->cur_note != 0x00)
						{
							// Note off before new note on
							midi_write_note_off(track);
						}

						if(cur_track < MUSICTRACK_NOISE)
						{
							// Notes are actually double-value because it was
							// convenient for a frequency lookup table, so we
							// must divide by 2 to get it to fit in MIDI scale
							byte = 36 + byte / 2;
						}
						else if(cur_track == MUSICTRACK_NOISE)
						{
							unsigned char noise_note = byte & 0x07;

							// Noise "note" translation table...
							// FIXME: XML-ify
							unsigned char note_trans[8] = 
							{
								 0,  0,	// $00-$01 Not used (first is loop, second is note off)
								69, 69,	// $02-$03
								33, 33,	// $04-$05
								70, 70	// $06-$07
							};

							byte = note_trans[noise_note];
						}
						else if(cur_track == MUSICTRACK_DMC)
						{
							// DCM "note" translation table...
							// FIXME: XML-ify
							unsigned char note_trans[16] =
							{
								24+35, 24+40, 24+38, 24+40, 24+36, 24+37, 24+39, 24+41, 24+42, 24+12, 24+11, 24+32, 24+4, 24+21, 24+19, 24+17
							};

							byte = note_trans[(byte & 0x0F) - 1];
						}

						// Note on
						midi_write_note_on(track, byte);
					}
					else
					{
						// Rest / Note-Off
						if(track->cur_note != 0x00)
						{
							// Note off 
							midi_write_note_off(track);
						}
					}

					// On Square Wave tracks, an 0xFF value may follow the Note On
					// which specifies a bend effect; if it is present, we must
					// retrieve it immediately because it is considered atomic
					// with the Note On byte...
					if( ((cur_track == MUSICTRACK_SQUARE1) || (cur_track == MUSICTRACK_SQUARE2)) && *track->data_ptr == 0xFF)
					{
						// Based on the SMB3 music engine, the byte which follows
						// should only be an $00-$7F value... so we must jump to
						// that handler...
						byte = *track->data_ptr++;

						goto Handle7bit;
					}

					// Reload hold_remain
					track->hold_remain = track->cur_hold;
					track->time_total += track->cur_hold;
				}
			}
		}
	}


	// Terminator for MIDI Track 0
	WriteVarLen(out, 0);
	fputc(0xFF, out);
	fputc(0x2F, out);
	fputc(0x00, out);

	// Track length for MIDI Track 0
	length_end = ftell(out);
	fseek(out, length_start, SEEK_SET);
	writeBE32(out, length_end - length_start - 4);
	fseek(out, length_end, SEEK_SET);


	// Terminate everything and write out the tracks
	for(cur_track = 0; cur_track < MUSICTRACK_TOTAL; cur_track++)
	{
		int prev_event_time = 0;
		struct smb3_track *track = &smb3_tracks[cur_track];
		int midi_event_total;

		// Be percussive on the percussive tracks
		unsigned char midi_channel = (cur_track < MUSICTRACK_NOISE) ? cur_track : 9;

		// Terminator
		track->midi_event->time = 0;
		track->midi_event->event = 0xFF;
		track->midi_event->bytes[0] = 0x2F;
		track->midi_event->bytes[1] = 0x00;
		track->midi_event->byte_len = 2;
		track->midi_event++;

		// Calculate total midi events
		midi_event_total = (int)(track->midi_event - track->midi_events);

		// Reset midi event pointer
		track->midi_event = track->midi_events;

		// Track header
		fputs("MTrk", out);

		length_start = ftell(out);

		// Placeholder for length
		writeBE32(out, 0);

		while(--midi_event_total > 0)
		{
			int bytes, delta_time;

			int time = (track->midi_event->time * NODICE_MUSICMIDI_TIMESCALE);

			// MIDI uses delta times
			delta_time = time - prev_event_time;

			prev_event_time = time;

			if(delta_time < 0)
			{
				printf("WARNING: Track %i produced negative delta: %i\n", cur_track, delta_time);
				delta_time = 0;
			}

			WriteVarLen(out, delta_time);

			// Write event
			fputc(track->midi_event->event, out);

			// Write event bytes
			for(bytes = 0; bytes < track->midi_event->byte_len; bytes++)
				fputc(track->midi_event->bytes[bytes], out);

			track->midi_event++;
		}

		length_end = ftell(out);
		fseek(out, length_start, SEEK_SET);
		writeBE32(out, length_end - length_start - 4);
		fseek(out, length_end, SEEK_SET);
	}


	fclose(out);

	return 1;
}
