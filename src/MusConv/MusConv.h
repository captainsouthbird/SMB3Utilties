#ifndef _MUSCONV_H
#define _MUSCONV_H

int MusConv_Import(const char *input_midi_name, const char *output_asm_name);
int MusConv_Export(const char *midi_out_filename, const char *header_index_name, const char *SEL_name, unsigned char music_index);
int config_load();

extern struct _import_map
{
	unsigned char map;
} import_patch_map[128], import_noise_map[128], import_percussion_map[128];

#endif
