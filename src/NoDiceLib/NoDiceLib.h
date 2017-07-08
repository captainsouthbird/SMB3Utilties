#ifndef _NODICELIB_H
#define _NODICELIB_H

#include <limits.h>

#ifdef _MSC_VER
// MSVC compatibility fixes
#if _MSC_VER < 1900 //vs2015 already have this function
#define snprintf _snprintf
#endif
#define chdir _chdir
#define strcasecmp _stricmp
#define strdup _strdup
#define PATH_MAX 260	// FIXME: Ought to come from header
#endif

char *stristr(const char *String, const char *Pattern);

// File extensions off of "filebase" and other filesystem defines
#define EXT_ROM			".nes"	// Produced ROM file
#define EXT_SYMBOLS			".fns"	// "fns" symbol listing file
#define EXT_ASM			".asm"	// Game assembly source
#define SUBDIR_PRG			"PRG"
#define SUBDIR_LEVELS		SUBDIR_PRG "/levels"
#define SUBDIR_OBJECTS		SUBDIR_PRG "/objects"
#define SUBDIR_ICONS		"icons"
#define SUBDIR_ICON_LEVELS	SUBDIR_ICONS "/levels"
#define SUBDIR_ICON_TILES	SUBDIR_ICONS "/tiles"
#define SUBDIR_MAPS			SUBDIR_PRG "/maps/"

// Screen size assumptions (FIXME: this isn't checked to sync with source)
#define TILEMEM_BASE		0x6000	// Base of tile memory
#define TILEMEM_END			0x794F	// Absolute last address of tile memory
#define SCREEN_BYTESIZE		0x1B0	// Screen size in bytes, non-vertical level
#define SCREEN_BYTESIZE_V	0xF0		// Screen size in bytes, vertical level
#define SCREEN_WIDTH		16		// Vertical or non-vertical, a "screen" is 16 tiles wide
#define SCREEN_VHEIGHT		(SCREEN_BYTESIZE_V / SCREEN_WIDTH)	// Vertical screen height
#define SCREEN_COUNT		15		// Number of screens when not vertical
#define SCREEN_VCOUNT		16		// Number of screens when vertical
#define TILESIZE			16		// Pixels wide and tall of a single tile
#define SCREEN_BYTESIZE_M	0x90		// Screen size in bytes, world map (9 x 16)
#define SCREEN_MCOUNT		4		// Number of screens when world map
#define SCREEN_MAP_ROW_OFFSET	((SCREEN_BYTESIZE - SCREEN_BYTESIZE_M) / SCREEN_WIDTH - 1)	// Offset to beginning of map tiles

// Object assumptions (FIXME: this isn't checked to sync with source)
#define OBJ_BANK			6
#define OBJS_MAX			48		// Maximum number of objects total possible in a level
#define MOBJS_MAX			9		// Maximum number of (pre-defined) objects total possible on a map
#define MAP_OBJECT_BASE_ROW	2		// Map objects are two rows in, otherwise not useful
#define MAX_MAP_LINKS		256		// Ideal value is 16x9x4 = 576, but this saves a bit which helps pack a value in gui_popups.c
#define MAX_SPECIAL_TILES	64		// Ideal value is 256 of course, but this helps pack a value in gui_popups.c

// GEN_MAX_PARAMS: Maximum number of "parameters" a generator may have;
// Should definitely be >= 2.  GENTYPE_JCTSTART requires 2.  GENTYPE_VARIABLE
// may need 1 to 2.  For GENTYPE_VARIABLE, the first is always LL_ShapeDef's
// lower 4 bits, second is an additional byte that MAY be used by some of the
// variable generators that need a greater parameter range.
// This also determines the maximum <param /> blocks you can have in game.xml
#define GEN_MAX_PARAMS	2

// There are 5 level header bytes besides the pointers to next level/object
#define LEVEL_HEADER_COUNT	5

#define LEVEL_JCT_STARTS		16

// Types of generators (in level editor only, no bearing on the level format)
enum
{
	GENTYPE_JCTSTART,	// Junction start generator
	GENTYPE_VARIABLE,	// Variable-size generator
	GENTYPE_FIXED		// Fixed-size generator
};

// Types of build errors
enum BUILDERR
{
	BUILDERR_RETURNCODE,	// Check if assembler returns non-zero code
	BUILDERR_TEXTERROR,		// Check if assembler output includes word "error"

	BUILDERR_TOTAL
};

// Automatically loaded from XML when initialized
extern struct NoDice_configuration
{
	char original_dir[PATH_MAX];	// Maintain original CWD in case of configuration reload
	const char *game_dir;

	struct
	{
		char *_build_str;		// Pointer to split build string, just required for freeing later
		char **build_argv;		// Build executable (index 0) and additional parameters
		enum BUILDERR builderr;	// Error check style
	} buildinfo;

	const char *filebase;
	int core6502_timeout;
	unsigned short level_range_check_high;

	// Built from the game.xml
	struct NoDice_game
	{
		struct NoDice_options
		{
			const char *title;				// Game title
			const char *warpzone;			// Warp Zone layout label definition
			const char *object_set_bank;	// The bank ASM file which specifies 
		} options;

		struct NoDice_tilehint
		{
			unsigned char id;		// ID of the tile
			const char *overlay;	// Overlay graphic file
		} *tilehints;				// Tileset-agnostic (common) hints
		int tilehint_count;

		struct NoDice_tileset
		{
			int id;			// ID of tileset
			const char *name;		// Name of tileset
			const char *path;		// Relative file path component to access tileset
			const char *rootfile;	// Name of ASM file which defines the layout includes for this tileset; if null, use "path"
			const char *desc;		// Description of tileset

			struct NoDice_generator
			{
				unsigned char type;	// Type of generator
				unsigned char id;	// ID of generator
				const char *name;	// Name of generator
				const char *desc;	// Description of generator

				// This just provides descriptive labels for the parameters
				// of the generator and parameter ranges
				struct NoDice_generator_parameter
				{
					const char *name;	// Name of the parameter
					unsigned char min;	// Minimum value
					unsigned char max;	// Maximum value
				} parameters[GEN_MAX_PARAMS];

			}	*generators;		// Variable and Fixed-size block generators
			int 	gen_count;		// Count of generators

			struct NoDice_tilehint *tilehints;	// Tileset specific hints
			int tilehint_count;

			struct NoDice_the_levels
			{
				const char *name;		// Name of level
				const char *layoutfile;	// Assembly file for level layout
				const char *layoutlabel;	// Assembly label for level layout
				const char *objectfile;	// Assembly file for object layout
				const char *objectlabel;	// Assembly label for object layout
				const char *desc;		// Description of level
			}
				*levels;
			int levels_count;		// Count of levels


		} *tilesets;	// Tilesets in this game

		int tileset_count;	// Count of tilesets

		struct NoDice_headers
		{
			// Each header byte can be broken into one or more <options />
			struct NoDice_header_options
			{
				const char *id;		// Optional id value for referencing
				unsigned char mask;		// Mask of options list
				unsigned char shift;	// Shift value
				const char *display;	// Display value

				// Each <option /> block
				struct NoDice_option
				{
					const char *label;		// Assembler label for this option item
					const char *display;	// Display value for this option item
					unsigned char value;	// Value for this option
				} *options;
				int options_count;

				// Form: "id:val"
				const char *showif_id;		// Id to check
				unsigned char showif_val;	// Value to match
			} *options_list;
			int options_list_count;

		} 	headers[LEVEL_HEADER_COUNT];

		// Junction has some options
		struct NoDice_headers jct_options;

		struct NoDice_objects
		{
			const char *label;		// Label
			const char *name;		// Name of object
			const char *desc;		// Description

			struct NoDice_object_sprites
			{
				int x;
				int y;
				unsigned char bank;
				unsigned char pattern;
				unsigned char palette;
				unsigned char flips;	// bit 0: Hflip, 1: Vflip
			} *sprites;
			int total_sprites;

			struct NoDice_headers special_options;

		}	*objects,
			regular_objects[256], 	// Level objects
			map_objects[256];		// World Map objects

		struct NoDice_map_object_item
		{
			unsigned char id;	// ID of item
			const char *name;	// Name of item
		} *map_object_items;
		int total_map_object_items;

		struct NoDice_map_special_tile
		{
			unsigned char id;			// ID of tile to map
			unsigned char tileset;		// If "tilelayout" is specified, this provides the tileset to be used (otherwise ignored)

			// Spade and Toad houses can use one or the other layout values for something else
			struct NoDice_map_special_tile_override
			{
				struct NoDice_headers low;	// Low byte options
				struct NoDice_headers high;	// High byte options
			} override_tile, override_object;
		} *map_special_tiles;
		int total_map_special_tiles;
	} game;
} NoDice_config;



// Automatically populated when NoDice_load_level is called
extern struct NoDice_level
{
	struct NoDice_level_header
	{
		unsigned short
			alt_level_layout, 	// Alternate level layout address
			alt_level_objects;	// Alternate level object set address

		// Alternate tileset
		unsigned char alt_level_tileset;

		// The header option bytes
		unsigned char option[LEVEL_HEADER_COUNT];

		// Flag for whether level is vertical
		unsigned char is_vert;

		// Keep total screens
		unsigned char total_screens;

		// Capture vertical position; mostly used for creating thumbnail at this point
		unsigned short vert_scroll;

	} header;

	const struct NoDice_tileset *tileset;	// Tileset of this level
	const struct NoDice_the_levels *level;	// Definition of the level

	unsigned char
		bg_page_1, 	// First MMC3 BG page
		bg_page_2;	// Second MMC3 BG page
	unsigned char tile_layout[256][4];	// UL, LL, UR, LR; 8x8 patterns making up the 16x16 tile
	unsigned char bg_pal[16];	// NES color values making up the 4 BG palettes
	unsigned char spr_pal[16];	// NES color values making up the 4 SPR palettes
	unsigned char *tiles;		// Pointer to beginning of tile RAM

	// This defines a grid of tiles (stride depending on whether vertical or not)
	// where the index of the generator that produced the tile is stored, thus
	// the topmost generator of a particular tile has its index there.  This helps
	// to identify which tiles belong to what generator to simplify determining if
	// the mouse is actually on the generator and not an overlapped one or a gap.
	unsigned short tile_id_grid[TILEMEM_END - TILEMEM_BASE];

	struct NoDice_the_level_generator
	{
		unsigned short index;	// Internal index count for tile_id_grid
		unsigned char type;		// Type of generator

		unsigned char id;			// ID of generator
		unsigned short addr_start;	// Tile RAM address generator starts at
		unsigned short addr_min;		// Minimum tile RAM address used
		unsigned short addr_max;		// Maximum tile RAM address used
		unsigned short xs, ys;		// Pixel X/Y start
		unsigned short xe, ye;		// Pixel X/Y end
		unsigned char p[GEN_MAX_PARAMS];	// Parameters depending on generator; see #define GEN_MAX_PARAMS
		unsigned char size;			// Size of generator

		struct NoDice_the_level_generator *prev;		// Previous generator in level
		struct NoDice_the_level_generator *next;		// Next generator in level
	}	*generators;		// All generators

	unsigned char Level_JctXLHStart[LEVEL_JCT_STARTS], Level_JctYLHStart[LEVEL_JCT_STARTS];

	unsigned short addr_start, addr_end;

	unsigned char object_unknown;
	struct NoDice_the_level_object
	{
		unsigned char row;
		unsigned char col;
		unsigned char id;	// 0xFF is a terminator
	} objects[OBJS_MAX];
	int object_count;


	// World Map stuff only
	struct NoDice_map_link
	{
		unsigned char row_tileset;	// upper 4 bits is row, lower 4 bits is tileset
		unsigned char col_hi;		// upper 4 bits is column, lower 4 bits is screen
		unsigned short object_addr;	// Object address (except Spade/Toad panels)
		unsigned short layout_addr;	// Layout address
	} map_links[MAX_MAP_LINKS];	// Enough storage to link every tile on every screen
	int map_link_count;

	// Special storage for map object items only
	unsigned char map_object_items[MOBJS_MAX];

} NoDice_the_level;

enum MUSICTRACK
{
	MUSICTRACK_SQUARE2,
	MUSICTRACK_SQUARE1,
	MUSICTRACK_TRIANGLE,
	MUSICTRACK_NOISE,
	MUSICTRACK_DMC,

	MUSICTRACK_TOTAL
};


struct NoDice_music_context
{
	// Holds pointers and initialization info to all segments
	struct NoDice_music_segment
	{
		const unsigned char *segment_data;	// Data belonging to segment
		unsigned char track_starts[MUSICTRACK_TOTAL];	// Start offsets for all tracks
	} music_segments[256];		// 256 should be more than enough since the segment counter is only 8-bit
	unsigned char music_segment_count;

	unsigned char rest_table_base;		// Base index value into rest_table
	const unsigned char *rest_table;	// Rest lookup table

	// Tally data, probably not useful?
	unsigned char index_start;
	unsigned char index_end;
	unsigned char index_loop;
};


int NoDice_Init();
const char *NoDice_Error();
void NoDice_Shutdown();

enum RUN6502_STOP_REASON
{
	RUN6502_STOP_NOTSTOPPED,		// 6502 still running!
	RUN6502_STOP_END,			// Ended
	RUN6502_INIT_ERROR,			// Initialization error occurred!
	RUN6502_LEVEL_OORW_LOW,		// Level load out-of-range write low (beyond RAM A, below TILEMEM_BASE)
	RUN6502_LEVEL_OORW_HIGH,		// Level load out-of-range write high (greater than TILEMEM_END)
	RUN6502_GENERATOR_TOO_LARGE,	// A generator was loaded that has more parameters than GEN_MAX_PARAMS

	// Signals not set by the 6502 core, externally influenced
	RUN6502_TIMEOUT,			// Timeout hit (external source must make this happen)
	RUN6502_GENGENCOUNT_MISMATCH,	// Generator count unexpectedly changed
};
extern volatile enum RUN6502_STOP_REASON NoDice_Run6502_Stop;	// Set stop reason to halt execution

int NoDice_PRG_refresh();
const unsigned char *NoDice_get_raw_CHR_bank(unsigned char bank);
const unsigned char *NoDice_pack_level(int *size, int need_header);
void NoDice_load_level(unsigned char tileset, const char *level_layout, const char *object_layout);
void NoDice_load_level_by_addr(unsigned char tileset, unsigned short address, unsigned short object_address);
void NoDice_load_level_raw_data(const unsigned char *data, int size, int has_header);
unsigned short NoDice_get_addr_for_label(const char *label);
int NoDice_get_tilebank_free_space(unsigned char tileset);
const unsigned char *NoDice_get_rest_table();
int NoDice_get_music_context(struct NoDice_music_context *context, const char *header_index_name, const char *SEL_name, unsigned char music_index);
void NoDice_tile_test();
const char *NoDice_config_game_add_level_entry(unsigned char tileset, const char *name, const char *layoutfile, const char *layoutlabel, const char *objectfile, const char *objectlabel, const char *desc);

// Process execution
#define EXEC_BUF_LINE_LEN	512	// Length of a single line of output from the process execution buffer
int NoDice_exec_build(void (*buffer_callback)(const char *));	// FIXME: Probably not necessary to expose this
const char *NoDice_DoBuild();

#endif // _NODICELIB_H
