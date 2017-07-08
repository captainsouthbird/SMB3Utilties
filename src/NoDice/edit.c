#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "NoDiceLib.h"
#include "NoDice.h"

#define UNDO_STACK_LIMIT	100	// FIXME: User-configurable!

enum UNDOMODE
{
	UNDOMODE_GENS_NOHEADER,		// Mark generator undo, headerless
	UNDOMODE_GENS_WITHHEADER,	// Mark generator undo, with header
	UNDOMODE_OBJECTS,			// Mark object undo
	UNDOMODE_MAPTILE,			// Mark map tile undo
	UNDOMODE_LINK				// Mark link undo
};

static void undo_mark(enum UNDOMODE undo_mode);
static void undo_revert();

static const struct NoDice_the_levels *edit_cur_level;
static char path_buffer[PATH_MAX];

// This should be WAY MORE than enough; nesasm only allows labels up to something
// like 32 characters, and so even if you had 8 individual flags of 32 characters
// separated by " | " (35), max required is 35 * 8 = 280 characters
#define OPTION_STRING_LEN	512

static int option_string_add(char *opt_str, const char *addition, int *opt_str_pos)
{
	// The buffer space is limited...
	// Make sure we haven't hit the end of the buffer
	if(*opt_str_pos < (OPTION_STRING_LEN-1) )
	{
		int i;
		const char *additions[2] = { addition, " | " };

		for(i = 0; i < 2; i ++)
		{
			// Number of characters for next line of output
			int len = strlen(additions[i]), max = (OPTION_STRING_LEN-1) - *opt_str_pos;

			if(max > 0)
			{
				// Use strncat to limit number of characters we copy up to
				// what's remaining in the buffer
				strncat(opt_str, additions[i], max);

				// Advance position
				*opt_str_pos += len;
			}
			else
				// Not enough room!
				return 0;
		}

		return 1;
	}

	// Not enough room!!
	return 0;
}


const struct NoDice_the_levels *edit_level_find(unsigned char tileset_id, unsigned short layout_addr, unsigned short objects_addr)
{
	int i;
	const struct NoDice_the_levels *the_level = NULL;

	// FIXME: This process is kind of complicated; wonder if it could be done better?

	// Find the correct tileset that matches the alternate
	for(i = 0; i < NoDice_config.game.tileset_count; i++)
	{
		const struct NoDice_tileset *tileset = &NoDice_config.game.tilesets[i];
		if(tileset->id == tileset_id)
		{
			int j;

			// Found the tileset!  Hopefully we can find the level within...
			for(j = 0; j < tileset->levels_count; j++)
			{
				unsigned short addr;
				const struct NoDice_the_levels *level = &tileset->levels[j];

				// This part gets a little ugly; we need to resolve the labels
				// to addresses so we can see if these are the right labels to
				// apply here...
				addr = NoDice_get_addr_for_label(level->layoutlabel);

				// Check if the layout label is right; if not, we won't bother with
				// the object label...
				if(addr == layout_addr)
				{
					// Special object drop override used for special tile map links ONLY
					if(objects_addr != 0xFFFF)
						addr = NoDice_get_addr_for_label(level->objectlabel);

					if(addr == objects_addr || objects_addr == 0xFFFF)
					{
						// We found it!!
						the_level = level;

						// We're done!
						break;
					}
				}
			}
		}
	}

	return the_level;
}


static const char *edit_level_save_make_option_string(int header_index)
{
	int i, opt_str_pos = 0;
	static char opt_str[OPTION_STRING_LEN];
	const struct NoDice_headers *header = &NoDice_config.game.headers[header_index];
	unsigned char header_val = NoDice_the_level.header.option[header_index];

	// Blank out the string
	opt_str[0] = '\0';

	// Go through each option defined by the header_index to
	// determine the bit field constants...
	for(i = 0; i < header->options_list_count; i++)
	{
		int j;

		// This <options /> block...
		const struct NoDice_header_options *option_list = &header->options_list[i];

		// Special "flag" mode recognition -- if there is only a single
		// <option /> in the <options /> block, this is a flag, so we only
		// toggle it...
		if((option_list->options_count == 1) && (option_list->options[0].label != NULL))
		{
			// But only do anything if it's set, otherwise forget it
			if(header_val & option_list->mask)
			{
				// Bit set; add the mask, but abort if we run out of room (shouldn't happen, hopefully!!)
				if(!option_string_add(opt_str, option_list->options[0].label, &opt_str_pos))
					return NULL;
			}
		}
		else if(option_list->options_count > 1)
		{
			int resolved = 0;	// Flag set if label is resolved
			char alt_val[11];	// Small buffer in case we must enter value manually (enough for "(0-255 << 0-7)")

			// Get index value relative to the option
			unsigned char relative_value = (header_val & option_list->mask) >> option_list->shift;

			// Locate this value in the option list (hopefully!)
			for(j = 0; j < option_list->options_count; j++)
			{
				const struct NoDice_option *opt = &option_list->options[j];

				// If there's a defined label and the value matches the relative
				// value, then we have our label!
				if((opt->label != NULL) && (opt->value == relative_value))
				{
					// Push in the flag value if we can...
					if(!option_string_add(opt_str, opt->label, &opt_str_pos))
						return NULL;

					// We found it!
					resolved = 1;

					// Done!
					break;
				}
			}

			// We don't have a flag for this!  Just put in a raw value for now...
			if(!resolved)
			{
				snprintf(alt_val, sizeof(alt_val), "(%i << %i)", relative_value, option_list->shift);
				if(!option_string_add(opt_str, alt_val, &opt_str_pos))
					return NULL;
			}
		}
	}

	// Unlikely, but if we wound up with absolutely nothing, just return a zero
	if(strlen(opt_str) == 0)
		strcpy(opt_str, "0");
	else
	{
		int len = strlen(opt_str);

		// Trim off trailing " | ", if present
		if(opt_str[len-3] == ' ' && opt_str[len-2] == '|' && opt_str[len-1] == ' ')
			opt_str[len-3] = '\0';
	}

	return opt_str;
}


int edit_level_save(int save_layout, int save_objects)
{
	const char *autogen_warning = "; WARNING: Autogenerated file!  Do not put extra data here; editor will not preserve it!\n";
	const char *build_err;
	const struct NoDice_the_levels *level_alternate;
	const unsigned char *level_data;
	int i, size, col = 0;
	FILE *asm_file;

	if(NoDice_the_level.tileset->id > 0)
	{
		// Save layout only if required (for creating new-but-reusing levels)
		if(save_layout)
		{
			// The actual act of "saving" the level requires that first we rewrite
			// the asm file belonging to the level itself...
			snprintf(path_buffer, PATH_MAX, SUBDIR_LEVELS "/%s/%s" EXT_ASM, NoDice_the_level.tileset->path, edit_cur_level->layoutfile);

			asm_file = fopen(path_buffer, "w");

			if(!asm_file)
			{
				char error_buf[512];

				snprintf(error_buf, sizeof(error_buf), "edit_level_save: Failed to write %s: %s", path_buffer, strerror(errno));
				gui_display_message(1, error_buf);
				return 0;
			}

			fputs(autogen_warning, asm_file);

			level_alternate = edit_level_find(NoDice_the_level.header.alt_level_tileset, NoDice_the_level.header.alt_level_layout, NoDice_the_level.header.alt_level_objects);
			if(level_alternate != NULL)
			{
				// Found the alternate!  Use labels (preferred!)
				fprintf(asm_file,
						"\t.word %s\t; Alternate level layout\n"
						"\t.word %s\t; Alternate object layout\n",

					level_alternate->layoutlabel,
					level_alternate->objectlabel
						);
			}
			else
			{
				// Didn't find it, use addresses (not flexible!)
				fprintf(asm_file,
						"\t.word $%04X\t; Alternate level layout\n"
						"\t.word $%04X\t; Alternate object layout\n",

					NoDice_the_level.header.alt_level_layout,
					NoDice_the_level.header.alt_level_objects
						);
			}

			// Go through each header and add the header bytes
			for(i = 0; i < LEVEL_HEADER_COUNT; i++)
			{
				const char *next_header = edit_level_save_make_option_string(i);
				if(next_header != NULL)
					fprintf(asm_file, "\t.byte %s\n", next_header);
				else
				{
					// This really should NEVER happen!!
					gui_display_message(1, "INTERNAL ERROR: A generated header line was too long!");
					fclose(asm_file);
					return 0;
				}
			}

			// Pack the level
			level_data = NoDice_pack_level(&size, 0);

			// Write it out, byte-for-byte, 16 bytes per row
			for(i = 0; i < size; i++)
			{
				if(col == 0)
					fprintf(asm_file, "\n\t.byte ");

				fprintf(asm_file, "$%02X", level_data[i]);

				if(++col == 16)
					col = 0;
				else if(i < (size-1))
					fprintf(asm_file, ", ");
			}

			fclose(asm_file);
		}


		// Save objects only if required (for creating new-but-reusing levels)
		if(save_objects)
		{
			// Also need to rewrite the object file
			snprintf(path_buffer, PATH_MAX, SUBDIR_OBJECTS "/%s" EXT_ASM, edit_cur_level->objectfile);

			asm_file = fopen(path_buffer, "w");

			if(!asm_file)
			{
				char error_buf[512];

				snprintf(error_buf, sizeof(error_buf), "edit_level_save: Failed to write %s: %s", path_buffer, strerror(errno));
				gui_display_message(1, error_buf);
				return 0;
			}

			fputs(autogen_warning, asm_file);

			fprintf(asm_file, "\t.byte $%02X\t; Unknown purpose\n\n", NoDice_the_level.object_unknown);

			for(i = 0; i < NoDice_the_level.object_count; i++)
			{
				const struct NoDice_the_level_object *obj = &NoDice_the_level.objects[i];
				const char *label = NoDice_config.game.regular_objects[obj->id].label;
				fputs("\t.byte ", asm_file);

				// Label expansion
				if(label != NULL)
					fprintf(asm_file, "%s, ", label);
				else
					fprintf(asm_file, "$%02X, ", obj->id);	// Fallback to raw ID

				fprintf(asm_file, "$%02X, $%02X\n", obj->col, obj->row);
			}

			fputs("\t.byte $FF\n", asm_file);

			fclose(asm_file);
		}
	}
	else
	{
		enum OBJFILES
		{
			OF_ID,
			OF_ITEM,
			OF_X,
			OF_XHI,
			OF_Y,

			OF_TOTAL
		};
		const char *objfiles[OF_TOTAL] = { "", "I", "X", "H", "Y" };	// IDs, Items, XHi

		// World map has a lot more pieces
		int screen;

		////////////////////////////////////////////////////////////////////
		// MAP TILE LAYOUT DATA
		////////////////////////////////////////////////////////////////////
		snprintf(path_buffer, PATH_MAX, SUBDIR_MAPS "/%sL" EXT_ASM, edit_cur_level->layoutfile);

		asm_file = fopen(path_buffer, "w");

		if(!asm_file)
		{
			char error_buf[512];

			snprintf(error_buf, sizeof(error_buf), "edit_level_save: Failed to write %s: %s", path_buffer, strerror(errno));
			gui_display_message(1, error_buf);
			return 0;
		}

		fputs(autogen_warning, asm_file);

		for(screen = 0; screen < NoDice_the_level.header.total_screens; screen++)
		{
			int screen_offset = (SCREEN_BYTESIZE * screen) + (SCREEN_MAP_ROW_OFFSET * TILESIZE);

			// Write it out, byte-for-byte, 16 bytes per row
			for(i = 0; i < SCREEN_BYTESIZE_M; i++)
			{
				if(col == 0)
					fprintf(asm_file, "\n\t.byte ");

				fprintf(asm_file, "$%02X", NoDice_the_level.tiles[screen_offset + i]);

				if(++col == 16)
					col = 0;
				else if(i < (SCREEN_BYTESIZE_M-1))
					fprintf(asm_file, ", ");
			}

			// spacer
			fputc('\n', asm_file);
		}

		// Terminator
		fputs("\n\t.byte $FF\n", asm_file);

		fclose(asm_file);


		////////////////////////////////////////////////////////////////////
		// MAP OBJECT LAYOUT DATA
		////////////////////////////////////////////////////////////////////
		if(strcmp(edit_cur_level->layoutlabel, NoDice_config.game.options.warpzone))
		{
			for(i = 0; i < OF_TOTAL; i++)
			{
				int j;

				snprintf(path_buffer, PATH_MAX, SUBDIR_MAPS "/%s%s" EXT_ASM, edit_cur_level->objectfile, objfiles[i]);

				asm_file = fopen(path_buffer, "w");

				if(!asm_file)
				{
					char error_buf[512];

					snprintf(error_buf, sizeof(error_buf), "edit_level_save: Failed to write %s: %s", path_buffer, strerror(errno));
					gui_display_message(1, error_buf);
					return 0;
				}

				fputs(autogen_warning, asm_file);

				fputs("\t.byte ", asm_file);

				for(j = 0; j < MOBJS_MAX; j++)
				{
					struct NoDice_the_level_object *object = &NoDice_the_level.objects[j];
					unsigned char next_byte;

					if(i == OF_ID)
						next_byte = object->id;
					else if(i == OF_ITEM)
						next_byte = NoDice_the_level.map_object_items[j];
					else if((i == OF_X) || (i == OF_XHI))
					{
						unsigned short x = ((unsigned short)object->col) * TILESIZE;
						next_byte = (i == OF_X) ? (x & 0x00FF) : ((x & 0xFF00) >> 8);
					}
					else if(i == OF_Y)
						next_byte = ((object->row + MAP_OBJECT_BASE_ROW) * TILESIZE);

					if((i != OF_ID) || NoDice_config.game.map_objects[next_byte].label == NULL)
						fprintf(asm_file, "$%02X", next_byte);
					else
						// Object ID label expansion
						fprintf(asm_file, "%s", NoDice_config.game.map_objects[next_byte].label);

					if(j < (MOBJS_MAX-1))
						fputs(", ", asm_file);
				}

				fclose(asm_file);
			}
		}


		////////////////////////////////////////////////////////////////////
		// MAP LINKS
		////////////////////////////////////////////////////////////////////
		snprintf(path_buffer, PATH_MAX, SUBDIR_MAPS "/%sS" EXT_ASM, edit_cur_level->layoutfile);

		asm_file = fopen(path_buffer, "w");

		if(!asm_file)
		{
			char error_buf[512];

			snprintf(error_buf, sizeof(error_buf), "edit_level_save: Failed to write %s: %s", path_buffer, strerror(errno));
			gui_display_message(1, error_buf);
			return 0;
		}

		fputs(autogen_warning, asm_file);


		// Store lookup index labels
		fprintf(asm_file, "W%s_InitIndex:\t.byte $00, ", edit_cur_level->layoutlabel);
		for(i = 2; i <= 4; i++)
		{
			fprintf(asm_file, "(W%s_ByRowType_S%i - W%s_ByRowType)", edit_cur_level->layoutlabel, i, edit_cur_level->layoutlabel);

			if(i < 4)
				fputs(", ", asm_file);
		}
		fputc('\n', asm_file);


		// Begin storing screens
		screen = 0;

		{
			enum LABELS
			{
				LBL_BYROWTYPE,
				LBL_BYSCRCOL,
				LBL_OBJSETS,
				LBL_LEVELLAYOUT,

				LBL_TOTAL
			};
			const char *labels[LBL_TOTAL] = { "ByRowType", "ByScrCol", "ObjSets", "LevelLayout" };

			for(col = 0; col < LBL_TOTAL; col++)
			{
				if(	(col == LBL_BYROWTYPE) ||
					(col == LBL_BYSCRCOL) ||
					strcmp(edit_cur_level->layoutlabel, NoDice_config.game.options.warpzone))
				{
					const char *data_type = ((col == LBL_BYROWTYPE) || (col == LBL_BYSCRCOL)) ? "byte" : "word";

					// Print label
					fprintf(asm_file, "W%s_%s:\t.%s ", edit_cur_level->layoutlabel, labels[col], data_type);

					for(i = 0; i < NoDice_the_level.map_link_count; i++)
					{
						struct NoDice_map_link *link = &NoDice_the_level.map_links[i];
						int this_screen = (link->col_hi & 0xF0) >> 4;

						// Insert new labels on screen-change
						if(screen != this_screen)
						{
							fprintf(asm_file, "\nW%s_%s_S%i:\t.%s ", edit_cur_level->layoutlabel, labels[col], this_screen + 1, data_type);
							screen = this_screen;
						}
						else if(i > 0)
							fputs(", ", asm_file);

						if(col == LBL_BYROWTYPE)
							fprintf(asm_file, "$%02X", link->row_tileset);
						else if(col == LBL_BYSCRCOL)
							fprintf(asm_file, "$%02X", link->col_hi);
						else if( (col == LBL_OBJSETS) || (col == LBL_LEVELLAYOUT) )
						{
							// Non-warp zone...

							int tile_check, is_special = 0;

							// Get tileset of link
							unsigned char tileset = link->row_tileset & 0x0F;

							// Get tile under link (to check if this is a "special" tile)
							unsigned char tile_id = edit_maptile_get( ((link->row_tileset & 0xF0) >> 4) - MAP_OBJECT_BASE_ROW, link->col_hi);

							// Need to find the level to insert the label
							const struct NoDice_the_levels *level;

							// Find out if this is a "special" tile...
							for(tile_check = 0; tile_check < NoDice_config.game.total_map_special_tiles; tile_check++)
							{
								if(NoDice_config.game.map_special_tiles[tile_check].id == tile_id)
								{
									is_special = 1;
									break;
								}
							}

							// Get appropriate level
							if(!is_special || (col == LBL_LEVELLAYOUT))
								level = edit_level_find(tileset, link->layout_addr, !is_special ? link->object_addr : 0xFFFF);

							// Write appropriate label or value
							if(col == LBL_LEVELLAYOUT)
							{
								if(level != NULL)
									// Layout label is always printed
									fprintf(asm_file, "%s", level->layoutlabel);
								else
									fprintf(asm_file, "$%04X", link->layout_addr);
							}
							else
							{
								// Object label is printed only if not special
								if(!is_special && level != NULL)
									fprintf(asm_file, "%s", level->objectlabel);
								else
									fprintf(asm_file, "$%04X", link->object_addr);
							}
						}

					}

					fputc('\n', asm_file);
				}
				else
				{
					// Warp zone writes nothing for object sets / layouts
					fprintf(asm_file, "W%s_%s:\n", edit_cur_level->layoutlabel, labels[col]);
					screen = 0;
				}

				// Write out missing labels, if any
				for(screen++ ; screen < 4; screen++)
					fprintf(asm_file, "W%s_%s_S%i:\n", edit_cur_level->layoutlabel, labels[col], screen + 1);

				// Begin storing screens
				screen = 0;
			}
		}

		fclose(asm_file);
	}



	// Now ... initiate the build!!
	build_err = NoDice_DoBuild();

	if(build_err != NULL)
	{
		gui_display_message(1, build_err);
		return 0;
	}
	else
	{
		if(!NoDice_PRG_refresh())
			gui_display_message(0, NoDice_Error());
		else
			gui_display_message(0, "Save and build complete!");
	}

	return 1;
}


void edit_level_save_new_check(const char *tileset_path, const char *layoutfile, int *save_layout, const char *objectfile, int *save_objects)
{
	struct stat unused;

	// Does a layout already exist?
	if(*save_layout)
	{
		snprintf(path_buffer, PATH_MAX, SUBDIR_LEVELS "/%s/%s" EXT_ASM, tileset_path, layoutfile);

		// If file is found, do NOT actually save layout
		if(stat(path_buffer, &unused) == 0)
			*save_layout = 0;
	}

	// Does an object layout already exist?
	if(*save_objects)
	{
		snprintf(path_buffer, PATH_MAX, SUBDIR_OBJECTS "/%s" EXT_ASM, objectfile);

		// If file is found, do NOT actually save layout
		if(stat(path_buffer, &unused) == 0)
			*save_objects = 0;
	}
}


int edit_level_add_labels(const struct NoDice_the_levels *level, int add_layout, int add_objects)
{
	FILE *asm_file;

	// HACK: Need edit_cur_level to be set appropriately for save function to work
	// There's really no better time to set it... this is out last chance...
	edit_cur_level = level;

	if(NoDice_the_level.tileset->id > 0)
	{
		// Add new layout label if needed
		if(add_layout)
		{
			snprintf(path_buffer, PATH_MAX, SUBDIR_LEVELS "/%s" EXT_ASM, NoDice_the_level.tileset->rootfile);

			asm_file = fopen(path_buffer, "a");

			if(!asm_file)
			{
				char error_buf[512];

				snprintf(error_buf, sizeof(error_buf), "edit_level_add_labels: Failed to write %s: %s", path_buffer, strerror(errno));
				gui_display_message(1, error_buf);
				return 0;
			}

			fprintf(asm_file, "%s:\t.include \"" SUBDIR_LEVELS "/%s/%s\"\t; %s\n", 
				level->layoutlabel,
				NoDice_the_level.tileset->path,
				level->layoutfile,
				level->name
				);

			fclose(asm_file);
		}


		// Save objects only if required (for creating new-but-reusing levels)
		if(add_objects)
		{
			// Also need to rewrite the object file
			snprintf(path_buffer, PATH_MAX, SUBDIR_PRG "/%s" EXT_ASM, NoDice_config.game.options.object_set_bank);

			asm_file = fopen(path_buffer, "a");

			if(!asm_file)
			{
				char error_buf[512];

				snprintf(error_buf, sizeof(error_buf), "edit_level_add_labels: Failed to write %s: %s", path_buffer, strerror(errno));
				gui_display_message(1, error_buf);
				return 0;
			}

			fprintf(asm_file, "%s:\t.include \"" SUBDIR_OBJECTS "/%s\"\t; %s\n", 
				level->objectlabel,
				level->objectfile,
				level->name
				);

			fclose(asm_file);
		}
	}
	else
	{
		gui_display_message(TRUE, "FIXME: NOT IMPLEMENTED FOR WORLD MAPS");
		return 0;
	}

	return 1;
}


static unsigned char *map_tile_calc_ptr(int row, int col)
{
	// SMB3 is organized in "screens" made up of SCREEN_WIDTH tiles
	// Maps start at SCREEN_MAP_ROW_OFFSET
	return &NoDice_the_level.tiles[
		(SCREEN_MAP_ROW_OFFSET * TILESIZE) + 			// starting offset
		((col / SCREEN_WIDTH) * SCREEN_BYTESIZE) +	// jump by screen
		(row * SCREEN_WIDTH) +		// offset to specific row
		(col % SCREEN_WIDTH)];		// offset to specific column
}


static struct edit_undo
{
	unsigned char *layout_data;
	int layout_data_size;
	enum UNDOMODE undo_mode;
} undo_stack[UNDO_STACK_LIMIT];
static int
	undo_stack_pos = 0, 	// Position within stack
	undo_stack_bottom = 0,	// Current bottom of stack; moves circularly as needed
	undo_stack_total = 0;	// Total items in undo stack


// Add copy of level to undo buffer
static void undo_mark(enum UNDOMODE undo_mode)
{
	struct edit_undo *undo = &undo_stack[undo_stack_pos];
	const unsigned char *layout_data;

	// If undo stack is maxed out, we move the bottom up
	if(undo_stack_total == UNDO_STACK_LIMIT)
	{
		// Free components from old bottom
		free(undo_stack[undo_stack_bottom].layout_data);

		undo_stack_bottom = (undo_stack_bottom + 1) % UNDO_STACK_LIMIT;
	}
	else
		undo_stack_total++;

	// Advance position in the undo stack
	undo_stack_pos = (undo_stack_pos + 1) % UNDO_STACK_LIMIT;

	if(undo_mode == UNDOMODE_OBJECTS)
	{
		int i;

		undo->layout_data_size = 3 * NoDice_the_level.object_count;
		undo->layout_data = (unsigned char *)malloc(sizeof(unsigned char) * undo->layout_data_size);

		for(i = 0; i < NoDice_the_level.object_count; i++)
		{
			const struct NoDice_the_level_object *obj = &NoDice_the_level.objects[i];
			undo->layout_data[(i * 3) + 0] = obj->row;
			undo->layout_data[(i * 3) + 1] = obj->col;
			undo->layout_data[(i * 3) + 2] = obj->id;
		}
	}
	else if(undo_mode == UNDOMODE_MAPTILE)
	{
		// Work is done in a special function
	}
	else if(undo_mode == UNDOMODE_LINK)
	{
		undo->layout_data_size = sizeof(struct NoDice_map_link) * NoDice_the_level.map_link_count;
		undo->layout_data = malloc(undo->layout_data_size);

		memcpy(undo->layout_data, NoDice_the_level.map_links, undo->layout_data_size);
	}
	else
	{
		int has_header = (undo_mode == UNDOMODE_GENS_WITHHEADER);

		// Copy layout data onto stack (optional header)
		layout_data = NoDice_pack_level(&undo->layout_data_size, has_header);

		// Allocate and copy data
		undo->layout_data = (unsigned char *)malloc(sizeof(unsigned char) * undo->layout_data_size);
		memcpy(undo->layout_data, layout_data, undo->layout_data_size);
	}

	undo->undo_mode = undo_mode;
}


static void undo_mark_map_tile(int row, int col, unsigned char tile)
{
	struct edit_undo *undo = &undo_stack[undo_stack_pos];
	struct NoDice_the_level_object *map_tile = (struct NoDice_the_level_object *)malloc(sizeof(struct NoDice_the_level_object));

	map_tile->row = row;
	map_tile->col = col;
	map_tile->id = tile;

	undo->layout_data_size = sizeof(struct NoDice_the_level_object);
	undo->layout_data = (unsigned char *)map_tile;

	undo_mark(UNDOMODE_MAPTILE);
}

// Pop an action and revert the change
static void undo_revert()
{
	// Make sure we're not at the bottom of the stack already
	if(undo_stack_total > 0)
	{
		struct edit_undo *undo;

		// Reverse position in the undo stack
		undo_stack_pos = (undo_stack_pos == 0) ? (UNDO_STACK_LIMIT - 1) : (undo_stack_pos - 1);

		// One less total
		undo_stack_total--;

		// Get this action...
		undo = &undo_stack[undo_stack_pos];

		if(undo->undo_mode == UNDOMODE_OBJECTS)
		{
			int i;

			// Set back to object mode
			gui_set_modepage((NoDice_the_level.tileset->id > 0) ? ENPAGE_OBJS : ENPAGE_MOBJS);

			NoDice_the_level.object_count = undo->layout_data_size / 3;

			for(i = 0; i < NoDice_the_level.object_count; i++)
			{
				struct NoDice_the_level_object *obj = &NoDice_the_level.objects[i];
				obj->row = undo->layout_data[(i * 3) + 0];
				obj->col = undo->layout_data[(i * 3) + 1];
				obj->id  = undo->layout_data[(i * 3) + 2];
			}

			gui_update_for_generators();
		}
		else if(undo->undo_mode == UNDOMODE_MAPTILE)
		{
			struct NoDice_the_level_object *map_tile = (struct NoDice_the_level_object *)undo->layout_data;
			unsigned char *edit_tile = map_tile_calc_ptr(map_tile->row, map_tile->col);

			// Restore tile
			*edit_tile = map_tile->id;

			// Set back to map tile mode
			gui_set_modepage(ENPAGE_TILES);
		}
		else if(undo->undo_mode == UNDOMODE_LINK)
		{
			// Set back to link mode
			gui_set_modepage(ENPAGE_LINKS);

			NoDice_the_level.map_link_count = undo->layout_data_size / sizeof(struct NoDice_map_link);
			memcpy(NoDice_the_level.map_links, undo->layout_data, undo->layout_data_size);

			gui_update_for_generators();
		}
		else
		{
			int has_header = undo->undo_mode == UNDOMODE_GENS_WITHHEADER;

			// Set back to generator mode
			gui_set_modepage(ENPAGE_GENS);

			// Going to reload based on undo stack!
			NoDice_load_level_raw_data(undo->layout_data, undo->layout_data_size, has_header);

			// If undo changed header, have to reset the virtual PPU
			if(has_header)
				ppu_configure_for_level();
		}

		// Free the memory used by this undo
		if(undo->undo_mode != UNDOMODE_MAPTILE)
			free(undo->layout_data);
	}
}


// Remove specified generator from list (NOTE: Does not free it!!)
static void level_gen_remove(struct NoDice_the_level_generator *remove)
{
	// If the to-be-removed element is the first, then simply replace the first...
	if(NoDice_the_level.generators == remove)
	{
		// The next element is the new first element
		NoDice_the_level.generators = remove->next;

		// The new first element needs to have no previous!
		if(NoDice_the_level.generators != NULL)
			NoDice_the_level.generators->prev = NULL;
	}
	else
	{
		// Non-first element general removal
		remove->prev->next = remove->next;

		if(remove->next != NULL)
			remove->next->prev = remove->prev;
	}
}


// Inserts the specified generator after "insert_after"
// If "insert_after" is NULL, inserts as first element
static void level_gen_insert(struct NoDice_the_level_generator *insert_after, struct NoDice_the_level_generator *insert)
{
	if(insert_after == NULL)
	{
		// As long as an item exists, needs to acknowledge "insert" as its predecessor
		if(NoDice_the_level.generators != NULL)
		{
			NoDice_the_level.generators->prev = insert;
			insert->next = NoDice_the_level.generators;
		}
		else
			// New item in empty list
			insert->next = NULL;

		// New first element
		insert->prev = NULL;
		NoDice_the_level.generators = insert;
	}
	else
	{
		// Non-first element general insert
		insert->next = insert_after->next;

		if(insert_after->next != NULL)
			insert_after->next->prev = insert;

		insert->prev = insert_after;
		insert_after->next = insert;
	}
}


static int level_gen_count()
{
	int total = 0;
	struct NoDice_the_level_generator *cur = NoDice_the_level.generators;

	while(cur != NULL)
	{
		total++;
		cur = cur->next;
	}

	return total;
}


void edit_level_load(unsigned char tileset, const struct NoDice_the_levels *level)
{
	// Remember level we're looking at
	edit_cur_level = level;

	// For best accuracy, this must come immediately before the level reload!
	gui_6502_timeout_start();

	// Load level
	NoDice_load_level(tileset, level->layoutlabel, level->objectlabel);
	NoDice_the_level.level = level;

	// Cleanup timer
	gui_6052_timeout_end();

	// If an error occurred, display it!
	if(NoDice_Run6502_Stop != RUN6502_STOP_END)
		gui_display_6502_error(NoDice_Run6502_Stop);

	// Unwind the undo stack
	while(undo_stack_total > 0)
	{
		struct edit_undo *undo;

		// Reverse position in the undo stack
		undo_stack_pos = (undo_stack_pos == 0) ? (UNDO_STACK_LIMIT - 1) : (undo_stack_pos - 1);

		// One less total
		undo_stack_total--;

		// Get this action...
		undo = &undo_stack[undo_stack_pos];

		// Free the memory used by this undo
		if(undo->undo_mode != UNDOMODE_MAPTILE)
			free(undo->layout_data);
	}

	// Disable objects and warn if empty object set
	gui_disable_empty_objs(!strcmp(level->objectlabel, "Empty_ObjLayout"));

	// Initial undo mark
	undo_mark(UNDOMODE_GENS_NOHEADER);
}


static void level_reload(int expected_generator_count)
{
	// For best accuracy, this must come immediately before the level reload!
	gui_6502_timeout_start();

	// Reload level
	NoDice_load_level_raw_data(NULL, 0, FALSE);

	// Cleanup timer
	gui_6052_timeout_end();

	// Make sure no 6502 errors were reported...
	if(NoDice_Run6502_Stop == RUN6502_STOP_END)
	{
		int actual_count = level_gen_count();

		// Verify that the generator count did not change!
		if(expected_generator_count != actual_count)
			// FIXME: Kind of ugly, it doesn't need to be a 6502 stop signal
			// because we're already stopped at this point
			NoDice_Run6502_Stop = RUN6502_GENGENCOUNT_MISMATCH;
	}

	if(NoDice_Run6502_Stop != RUN6502_STOP_END)
	{
		// 6502 core terminated prematurely for some reason; tell user why
		gui_display_6502_error(NoDice_Run6502_Stop);

		// ... then revert the changes!
		undo_revert();
	}

	// Update GUI with new generators
	gui_update_for_generators();
}


// Reloads level data with active generator set and verifies that
// the generator count afterward is what is expected.  This deals
// with a type of corruption that may cause additional "fake" generators
// to appear (may be solved by other range checks, but safe!)
static void level_reload_with_selection(int expected_generator_count, int old_sel_index)
{
	// Reload level
	level_reload(expected_generator_count);

	// Restore selection
	gui_overlay_select_index(old_sel_index);
}

void edit_gen_remove(struct NoDice_the_level_generator *gen)
{
	// Add an undo mark (no header)
	undo_mark(UNDOMODE_GENS_NOHEADER);

	// Remove from level's generators
	level_gen_remove(gen);

	// Free this one!
	free(gen);

	// Reload level
	level_reload(level_gen_count());
}


void edit_revert()
{
	undo_revert();

	// Update GUI with new generators
	gui_update_for_generators();
}


void edit_gen_translate(struct NoDice_the_level_generator *gen, int diff_row, int diff_col)
{
	int cur_row, cur_col, target_row, target_col;

	// Set an undo mark
	undo_mark(UNDOMODE_GENS_NOHEADER);

	cur_row = gen->ys / TILESIZE;
	cur_col = gen->xs / TILESIZE;

	target_row = cur_row + diff_row;
	target_col = cur_col + diff_col;

	if(!(NoDice_the_level.header.is_vert))
	{
		int target_screen = target_col / SCREEN_WIDTH;

		// Vertical transformation is pretty simple...
		if( target_row >= 0 && target_row < (SCREEN_BYTESIZE / SCREEN_WIDTH) )
			gen->addr_start += diff_row * SCREEN_WIDTH;

		// Horizontal transformation needs to pay attention to the screen!
		if(target_screen >= 0 && target_screen <= SCREEN_COUNT)
		{
			// Get the difference in screens, if any
			int screen_diff = (target_col / SCREEN_WIDTH) - (cur_col / SCREEN_WIDTH);

			if(screen_diff != 0)
				gen->addr_start += (SCREEN_BYTESIZE * screen_diff) - (screen_diff * SCREEN_WIDTH) + diff_col;
			else
				gen->addr_start += diff_col;
		}
	}
	else
	{
		// Things are much easier in vertical levels because the memory is linear!
		if(target_row >= 0 && target_row < (SCREEN_BYTESIZE_V / SCREEN_WIDTH) * SCREEN_COUNT )
			gen->addr_start += diff_row * SCREEN_WIDTH;

		if(target_col >= 0 && target_col <= 15)
			gen->addr_start += diff_col;
	}

	// Reload level
	level_reload_with_selection(level_gen_count(), gen->index);
}


void edit_gen_send_backward(struct NoDice_the_level_generator *gen)
{
	// Make sure generator is not already all the way back
	if(gen->prev != NULL)
	{
		// Hold pointer to element before selected's previous,
		// where we need to reinsert it to move it back one
		struct NoDice_the_level_generator *sel_prev_prev = gen->prev->prev;

		// Add an undo mark (no header)
		undo_mark(UNDOMODE_GENS_NOHEADER);

		// Push back!

		// Step 1: Remove selected generator
		level_gen_remove(gen);

		// Step 2: Insert selected generator back into list
		level_gen_insert(sel_prev_prev, gen);

		// Reload level
		level_reload_with_selection(level_gen_count(), gen->index-1);
	}
}


void edit_gen_bring_forward(struct NoDice_the_level_generator *gen)
{
	// Make sure generator is not already all the way forward
	if(gen->next != NULL)
	{
		// Hold pointer to element after selected, where we
		// need  to reinsert it after to move it forward one
		struct NoDice_the_level_generator *sel_next = gen->next;

		// Add an undo mark (no header)
		undo_mark(UNDOMODE_GENS_NOHEADER);

		// Push forward!

		// Step 1: Remove selected generator
		level_gen_remove(gen);

		// Step 2: Insert selected generator back into list
		level_gen_insert(sel_next, gen);

		// Reload level
		level_reload_with_selection(level_gen_count(), gen->index+1);
	}
}


void edit_gen_send_to_back(struct NoDice_the_level_generator *gen)
{
	// This becomes the new first element of the chain;
	// make sure it isn't already!
	if(gen->prev != NULL)
	{
		// Add an undo mark (no header)
		undo_mark(UNDOMODE_GENS_NOHEADER);

		// Step 1: Remove selected generator
		level_gen_remove(gen);

		// Step 2: Insert selected generator back into list
		level_gen_insert(NULL, gen);

		// Reload level
		level_reload_with_selection(level_gen_count(), 0);
	}
}


void edit_gen_bring_to_front(struct NoDice_the_level_generator *gen)
{
	int index = 0;

	// This becomes the new last element of the chain;
	// make sure it isn't already
	if(gen->next != NULL)
	{
		struct NoDice_the_level_generator *tail = NoDice_the_level.generators;

		if(tail != NULL)
		{
			// Need to find the tail first...
			while(tail->next != NULL)
			{
				tail = tail->next;
				index++;
			}
		}

		// Add an undo mark (no header)
		undo_mark(UNDOMODE_GENS_NOHEADER);

		// Step 1: Remove selected generator
		level_gen_remove(gen);

		// Step 2: Insert selected generator back into list
		level_gen_insert(tail, gen);

		// Reload level
		level_reload_with_selection(level_gen_count(), index);
	}
}


void edit_gen_insert_generator(const struct NoDice_generator *gen, int row, int col, const unsigned char *p)
{
	int index = 0;
	struct NoDice_the_level_generator *tail = NoDice_the_level.generators,
		*new_gen = (struct NoDice_the_level_generator *)malloc(sizeof(struct NoDice_the_level_generator));

	if(tail != NULL)
	{
		// Need to find the tail first...
		while(tail->next != NULL)
		{
			tail = tail->next;
			index++;
		}
	}

	// Mark undo
	undo_mark(UNDOMODE_GENS_NOHEADER);

	if(gen->type != GENTYPE_JCTSTART)
	{
		if(!(NoDice_the_level.header.is_vert))
		{
			new_gen->addr_start = (col / SCREEN_WIDTH * SCREEN_BYTESIZE) + (row * SCREEN_WIDTH) + (col % SCREEN_WIDTH);
		}
		else
		{
			new_gen->addr_start = (row * SCREEN_WIDTH) + col;
		}

		// Address moved to RAM placement
		new_gen->addr_start += TILEMEM_BASE;
	}


	new_gen->type = gen->type;
	new_gen->id = gen->id;

	if(new_gen->type == GENTYPE_VARIABLE)
	{
		int i;

		// Should be 3+
		new_gen->size = 2;

		// NOTE: The size of the generator is determined by the defined parameters
		// Always define all of the parameters!!
		for(i = 0; i < GEN_MAX_PARAMS && gen->parameters[i].name != NULL; i++)
		{
			new_gen->p[i] = p[i];
			new_gen->size++;
		}
	}
	else
		new_gen->size = 3;

	// Insert it in at the end
	level_gen_insert(tail, new_gen);

	// Reload level
	level_reload_with_selection(level_gen_count(), index+1);
}


void edit_gen_set_parameters(struct NoDice_the_level_generator *gen, const unsigned char *parameters)
{
	int i;

	// Add an undo mark (no header)
	undo_mark(UNDOMODE_GENS_NOHEADER);

	// Update parameters
	for(i=0; i<GEN_MAX_PARAMS; i++)
		gen->p[i] = parameters[i];

	// Reload level
	level_reload_with_selection(level_gen_count(), gen->index);
}


void edit_header_change(struct NoDice_the_level_generator *selected_gen, const unsigned char *old_header)
{
	unsigned char new_header[LEVEL_HEADER_COUNT];

	// Backup current header
	memcpy(new_header, NoDice_the_level.header.option, LEVEL_HEADER_COUNT);

	// Restore old header (so we set undo_mark correctly)
	memcpy(NoDice_the_level.header.option, old_header, LEVEL_HEADER_COUNT);

	// Add an undo mark (with header)
	undo_mark(UNDOMODE_GENS_WITHHEADER);

	// Put back the new header
	memcpy(NoDice_the_level.header.option, new_header, LEVEL_HEADER_COUNT);


	// Reload level
	if(selected_gen != NULL)
		level_reload_with_selection(level_gen_count(), selected_gen->index);
	else
		level_reload(level_gen_count());

	ppu_configure_for_level();
	gui_update_for_generators();
}


void edit_startspot_alt_load()
{
	// Add an undo mark (with header)
	undo_mark(UNDOMODE_GENS_WITHHEADER);

	// And objects!
	undo_mark(UNDOMODE_OBJECTS);

	// For best accuracy, this must come immediately before the level reload!
	gui_6502_timeout_start();

	// Load level
	NoDice_load_level_by_addr(NoDice_the_level.header.alt_level_tileset, NoDice_the_level.header.alt_level_layout, NoDice_the_level.header.alt_level_objects);

	// Cleanup timer
	gui_6052_timeout_end();

	// If an error occurred, display it!
	if(NoDice_Run6502_Stop != RUN6502_STOP_END)
		gui_display_6502_error(NoDice_Run6502_Stop);

	ppu_configure_for_level();
}


void edit_startspot_alt_revert()
{
	// Revert to level
	undo_revert();	// revert objects
	undo_revert();	// revert generators
}


static int sort_objs_compare(const void *a, const void *b)
{
	const struct NoDice_the_level_object *oa = (struct NoDice_the_level_object *)a;
	const struct NoDice_the_level_object *ob = (struct NoDice_the_level_object *)b;

	if(!NoDice_the_level.header.is_vert)
		return oa->col - ob->col;
	else
		return oa->row - ob->row;
}


// In SMB3, objects must be sorted forward by their 
// columns (non-vertical) or rows (vertical)
static void edit_obj_sort()
{
	qsort(NoDice_the_level.objects, NoDice_the_level.object_count, sizeof(struct NoDice_the_level_object), sort_objs_compare);
	gui_update_for_generators();
}


void edit_obj_translate(struct NoDice_the_level_object *obj, int diff_row, int diff_col)
{
	int target_row = obj->row + diff_row, target_col = obj->col + diff_col;

	if(target_row >= 0 && target_col >= 0)
	{
		// Mark undo
		undo_mark(UNDOMODE_OBJECTS);

		obj->row = target_row;
		obj->col = target_col;

		// World Map (tileset 0) does not sort objects
		if(NoDice_the_level.tileset->id != 0)
			edit_obj_sort();
		else
			gui_update_for_generators();
	}
}


int edit_obj_insert_object(const struct NoDice_objects *obj, int row, int col)
{
	int max = (NoDice_the_level.tileset->id > 0) ? OBJS_MAX : MOBJS_MAX;

	if(NoDice_the_level.object_count < max)
	{
		// Adding object to end, but it could be sorted to another position
		struct NoDice_the_level_object *new_obj = &NoDice_the_level.objects[NoDice_the_level.object_count++];

		// Mark undo
		undo_mark(UNDOMODE_OBJECTS);

		// Configure the object
		new_obj->id = obj - ((NoDice_the_level.tileset->id > 0) ? NoDice_config.game.objects : NoDice_config.game.map_objects);
		new_obj->row = row;
		new_obj->col = col;

		edit_obj_sort();

		return 1;
	}

	return 0;
}


void edit_obj_remove(struct NoDice_the_level_object *obj)
{
	int i, index = obj - NoDice_the_level.objects;

	// Mark undo
	undo_mark(UNDOMODE_OBJECTS);

	// Move all objects back
	for(i = index+1; i < NoDice_the_level.object_count; i++)
		memcpy(&NoDice_the_level.objects[i-1], &NoDice_the_level.objects[i], sizeof(struct NoDice_the_level_object));

	// One less object
	NoDice_the_level.object_count--;

	gui_update_for_generators();
}


void edit_map_obj_clear(struct NoDice_the_level_object *obj)
{
	// Mark undo
	undo_mark(UNDOMODE_OBJECTS);

	obj->row = -2;		// -2 to compensate dead space
	obj->col = 0;
	obj->id = 0;

	// World Map (tileset 0) does not sort objects
	gui_update_for_generators();
}


void edit_maptile_set(int row, int col, unsigned char tile)
{
	unsigned char *edit_tile = map_tile_calc_ptr(row, col);

	// Mark undo
	undo_mark_map_tile(row, col, *edit_tile);

	// Change tile
	*edit_tile = tile;
}


unsigned char edit_maptile_get(int row, int col)
{
	return *map_tile_calc_ptr(row, col);
}


static int sort_links_compare(const void *a, const void *b)
{
	const struct NoDice_map_link *oa = (struct NoDice_map_link *)a;
	const struct NoDice_map_link *ob = (struct NoDice_map_link *)b;

	// Sorting for map links is done:
	//	Column high bits (screen) first
	//	Then row
	//	Then column low bits
	// Simplest thing to do is combine the individual parts
	// so we can sort numerically as a unit value...
	unsigned short row_col_a = ((int)(oa->col_hi & 0xF0) << 4) | (oa->row_tileset & 0xF0) | (oa->col_hi & 0x0F);
	unsigned short row_col_b = ((int)(ob->col_hi & 0xF0) << 4) | (ob->row_tileset & 0xF0) | (ob->col_hi & 0x0F);

	return row_col_a - row_col_b;
}


// In SMB3, links must be sorted by row first then column
static void edit_link_sort()
{
	qsort(NoDice_the_level.map_links, NoDice_the_level.map_link_count, sizeof(struct NoDice_map_link), sort_links_compare);
	gui_update_for_generators();
}


void edit_link_translate(struct NoDice_map_link *link, int diff_row, int diff_col)
{
	int target_row = ((link->row_tileset & 0xF0) >> 4) + diff_row, target_col = link->col_hi + diff_col;

	if(target_row >= 0 && target_col >= 0)
	{
		// Mark undo
		undo_mark(UNDOMODE_LINK);

		link->row_tileset = (link->row_tileset & 0x0F) | (target_row << 4);
		link->col_hi = target_col;

		edit_link_sort();
	}
}


int edit_link_insert_link(int row, int col)
{
	if(NoDice_the_level.map_link_count < MAX_MAP_LINKS)
	{
		// Adding linkect to end, but it could be sorted to another position
		struct NoDice_map_link *new_link = &NoDice_the_level.map_links[NoDice_the_level.map_link_count++];

		// Mark undo
		undo_mark(UNDOMODE_LINK);

		// Clear link memory
		memset(new_link, 0, sizeof(struct NoDice_map_link));

		// Configure the link
		new_link->row_tileset = (row + MAP_OBJECT_BASE_ROW) << 4;
		new_link->col_hi = col;

		edit_link_sort();

		return 1;
	}

	return 0;
}


void edit_link_remove(struct NoDice_map_link *link)
{
	int i, index = link - NoDice_the_level.map_links;

	// Mark undo
	undo_mark(UNDOMODE_LINK);

	// Move all link back
	for(i = index+1; i < NoDice_the_level.map_link_count; i++)
		memcpy(&NoDice_the_level.map_links[i-1], &NoDice_the_level.map_links[i], sizeof(struct NoDice_map_link));

	// One less link
	NoDice_the_level.map_link_count--;

	gui_update_for_generators();
}


void edit_link_adjust(struct NoDice_map_link *updated_link, struct NoDice_map_link *old_link)
{
	struct NoDice_map_link updated_link_copy;

	// Copy target link
	memcpy(&updated_link_copy, updated_link, sizeof(struct NoDice_map_link));

	// Restore old link for undo mark
	memcpy(updated_link, old_link, sizeof(struct NoDice_map_link));

	// Mark undo
	undo_mark(UNDOMODE_LINK);

	// Update the link
	memcpy(updated_link, &updated_link_copy, sizeof(struct NoDice_map_link));
}
