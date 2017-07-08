#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "NoDiceLib.h"
#include "internal.h"
#include "M6502/M6502.h"

#define LOW(address)	((byte)((address) & 0x00FF))
#define HIGH(address)	((byte)(((address) & 0xFF00) >> 8))
#define MAKE16(high, low)	((unsigned short)((high) << 8) | (low))

#define MEM_A_START		0x0000
#define MEM_A_END		0x07FF
#define MEM_B_START		0x6000
#define MEM_B_END		0x7FFF

#define PRG_A_START		0x8000
#define PRG_A_END		0x9FFF
#define PRG_B_START		0xA000
#define PRG_B_END		0xBFFF
#define PRG_C_START		0xC000
#define PRG_C_END		0xDFFF
#define PRG_D_START		0xE000
#define PRG_D_END		0xFFFF

// Scratch space for modified levels; situated at 0x4020-0x5FFF ("expansion ROM")
// Only available when is_loading_level is set, not part of stock SMB3
#define SCRATCH_START	0x4020
#define SCRATCH_END		0x5FFF

// Only MMC3 commands needed here
#define MMC3_8K_TO_PRG_C000	0x46
#define MMC3_8K_TO_PRG_A000	0x47

#define MMC3_COMMAND	0x8000
#define MMC3_PAGE  		0x8001	// page number to MMC3_COMMAND

#define MMC3_BANKSIZE	8192


// A few RAM variables that we need
#define Temp_Var15				Rd6502(_ram[TEMP_VAR15].address)	// General temporary, but used for capturing generators
#define Temp_Var16				Rd6502(_ram[TEMP_VAR16].address)	// General temporary, but used for capturing generators
#define Vert_Scroll_Hi			Rd6502(_ram[VERT_SCROLL_HI].address)	// Provides a "High" byte for vertically scrolling (only used during vertical levels!)
#define Level_Width				Rd6502(_ram[LEVEL_WIDTH].address)	// Width of current level, in screens
#define Level_LayPtr_AddrL		Rd6502(_ram[LEVEL_LAYPTR_ADDRL].address)	// Pointer to layout data to be loaded (low)
#define Level_LayPtr_AddrH		Rd6502(_ram[LEVEL_LAYPTR_ADDRH].address)	// Pointer to layout data to be loaded (high)
#define Map_Tile_AddrL			Rd6502(_ram[MAP_TILE_ADDRL].address)	// Low byte of tile address
#define Map_Tile_AddrH			Rd6502(_ram[MAP_TILE_ADDRH].address)	// High byte of tile address
#define Vert_Scroll				Rd6502(_ram[VERT_SCROLL].address)	// Vertical scroll of name table
#define Level_7Vertical			Rd6502(_ram[LEVEL_7VERTICAL].address)	// Set in World 7 vertical type levels
#define TileAddr_Off			Rd6502(_ram[TILEADDR_OFF].address)	// During level loading, specifies an offset into the current Mem_Tile_Addr setting
#define LL_ShapeDef				Rd6502(_ram[LL_SHAPEDEF].address)	// Used for capturing the generators
#define Level_Tileset			Rd6502(_ram[LEVEL_TILESET].address)	// Set current tileset / level style
#define PAGE_C000				Rd6502(_ram[MMC3PAGE_C000].address)	// Page to set PRG ROM C000 (PRGROM_Change_Both)
#define PAGE_A000				Rd6502(_ram[MMC3PAGE_A000].address)	// Page to set PRG ROM A000 (PRGROM_Change_Both)
#define World_Num				Rd6502(_ram[WORLD_NUM].address)	// Current world index (0-8, where 0 = World 1, 7 = World 8, 8 = World 9 / Warp Zone)
#define PalSel_Tile_Colors		Rd6502(_ram[PALSEL_TILE_COLORS].address)	// Stores value to index which tile color set to use when palette loading routine is called
#define PalSel_Obj_Colors		Rd6502(_ram[PALSEL_OBJ_COLORS].address)	// Stores value to index which object color set to use when palette loading routine is called
#define Map_Objects_Itm(index)	Rd6502(_ram[MAP_OBJECTS_ITM].address + (index))
#define Level_AltTileset		Rd6502(_ram[LEVEL_ALTTILESET].address)	// Pointer to level's "alternate" tileset (when you go into bonus pipe, etc.)
#define Level_BG_Page1_2		Rd6502(_ram[LEVEL_BG_PAGE1_2].address)	// Sets which bank the first and second page (2K / 64 8x8 tiles) of BG is using
#define Map_Objects_Y(index)	Rd6502(_ram[MAP_OBJECTS_Y].address + (index))
#define Map_Objects_XLo(index)	Rd6502(_ram[MAP_OBJECTS_XLO].address + (index))
#define Map_Objects_XHi(index)	Rd6502(_ram[MAP_OBJECTS_XHI].address + (index))
#define Map_Objects_ID(index)	Rd6502(_ram[MAP_OBJECTS_ID].address + (index))

#define MAP_LAYOUT_BANK		12	// Specifies bank to get layout/structure data; FIXME: Hardcoded

#define GEN_WIDTH(addr)		(((((addr) - TILEMEM_BASE) / SCREENSIZE * 16) + (((addr) - TILEMEM_BASE) & 0xF)) * TILESIZE)
#define GEN_HEIGHT(addr)		(((((addr) - TILEMEM_BASE) & 0xF0) >> 4) * TILESIZE)
#define GEN_WIDTH_V(addr)	((((addr) - TILEMEM_BASE) & 0xF) * TILESIZE)
#define GEN_HEIGHT_V(addr)	(((addr) - TILEMEM_BASE) / SCREENSIZE_V

typedef const unsigned char *rom_t;

static M6502 CPU_Context;
volatile enum RUN6502_STOP_REASON NoDice_Run6502_Stop = RUN6502_STOP_NOTSTOPPED;
static int PRG_size;
static unsigned short CHR_banks;

// Trap functions needed to extract level data
static unsigned short LoadLevel_StoreJctStart, LeveLoad_Generators, LeveLoad_FixedSizeGens;

// Holds the PRG and (decoded) CHR images
static rom_t _PRG = NULL, _CHR = NULL;

// Emulates MMC3 banking
static rom_t _PRG_A, _PRG_B, _PRG_C, _PRG_D;

// Scratch space for modified levels; see defs for SCRATCH_START/END
static unsigned char _PRG_FakeScratch[SCRATCH_END - SCRATCH_START + 1];

// NES RAM and MMC3 RAM
static unsigned char _RAM[ (MEM_B_END - MEM_B_START + 1) + (MEM_A_END - MEM_A_START + 1) ] = { 0 };

// Current MMC3 command
static unsigned char MMC3_Command = 0x00;


// ROM label resolver
static struct ROM_label
{
	char label[32];
	unsigned short address;
	struct ROM_label *next;
} *ROM_labels = NULL;


// The loaded level memory
struct NoDice_level NoDice_the_level = { { 0 } };

// Required stuff for level loading, not public
static byte is_loading_level = 0;	// Set to enable any of the following
static struct NoDice_the_level_generator *cur_gen = NULL, *prev_gen = NULL;
static unsigned short prev_gen_start_addr = 0x0000;	// Start address of generator; for determining size


// Calculate virtual x pixel position of specified video address (non-vertical)
static unsigned short vram_virtual_x(unsigned short addr)
{
	unsigned short x = 0;

	// Make relative address
	unsigned short offset = addr - TILEMEM_BASE;

	// Divide offset by size of a single horizontal screen; each screen is SCREEN_WIDTH tiles
	// Multiply by SCREEN_WIDTH to get the absolute virtual column
	x = (offset / SCREEN_BYTESIZE) * SCREEN_WIDTH;

	// Now add on the offset across the current screen
	x += (offset % SCREEN_WIDTH);

	// Finally, multiply to get the pixel position
	x *= TILESIZE;

	return x;
}

// Calculate virtual y pixel position of specified video address (non-vertical)
static unsigned short vram_virtual_y(unsigned short addr)
{
	unsigned short y = 0;

	// Make relative address
	unsigned short offset = addr - TILEMEM_BASE;

	// Get offset within screen and divide by width to get row
	y = ((offset % SCREEN_BYTESIZE) / SCREEN_WIDTH);

	// Finally, multiply to get the pixel position
	y *= TILESIZE;

	return y;
}

// Calculate virtual x pixel position of specified video address (vertical)
static unsigned short vram_virtual_x_v(unsigned short addr)
{
	unsigned short x = 0;

	// Make relative address
	unsigned short offset = addr - TILEMEM_BASE;

	// Column can always be determined by the lower nibble of the lower byte of the offset
	x = (offset & 0xF);

	// Finally, multiply to get the pixel position
	x *= TILESIZE;

	return x;
}

// Calculate virtual y pixel position of specified video address (vertical)
static unsigned short vram_virtual_y_v(unsigned short addr)
{
	unsigned short y = 0;

	// Make relative address
	unsigned short offset = addr - TILEMEM_BASE;

	// Divide offset by SCREEN_WIDTH to get the row
	y = (offset / SCREEN_WIDTH);

	// Finally, multiply to get the pixel position
	y *= TILESIZE;

	return y;
}


static void prev_gen_patch(int size_offset)
{
	if(prev_gen != NULL)
	{
		// Set size on previous generator
		prev_gen->size = MAKE16(Level_LayPtr_AddrH, Level_LayPtr_AddrL) - prev_gen_start_addr + size_offset;

		// Push to edge of tile
		prev_gen->xe += (TILESIZE - 1);
		prev_gen->ye += (TILESIZE - 1);

		prev_gen = NULL;
	}
}


void _rom_free_level_list()
{
	struct NoDice_the_level_generator *gen, *next;

	gen = NoDice_the_level.generators;
	while(gen != NULL)
	{
		next = gen->next;
		free(gen);
		gen = next;
	}
	NoDice_the_level.generators = NULL;

	cur_gen = NULL;
	prev_gen = NULL;
}


void Wr6502(register word Addr,register byte Value)
{
	if(is_loading_level)
	{
		// Determine if a generator is writing out of bounds

		// If writing in the address space used by the scratch below
		// the expanded RAM bank, you're probably out of order!
		if(Addr >= SCRATCH_START && Addr < MEM_B_START)
			NoDice_Run6502_Stop = RUN6502_LEVEL_OORW_LOW;

		// If writing beyond the sensible end of the tile grid,
		// mark it as out-of-range-high.  There actually are some
		// generator layouts that overwrite memory they shouldn't,
		// but for the sake of legacy compatibility, we're allowing
		// a user-defined acceptable overrun... ideally this would
		// not exceed TILEMEM_END, but oh well...
		else if(Addr > TILEMEM_END && Addr <= NoDice_config.level_range_check_high)
			NoDice_Run6502_Stop = RUN6502_LEVEL_OORW_HIGH;


		// This is assuming tile memory grid writes from generators,
		// but this might not be completely safe...
		if(Addr >= MEM_B_START && Addr <= MEM_B_END && prev_gen != NULL)
		{
			short x, y;

			// Update min/max ranges as needed
			if(Addr < prev_gen->addr_min)
				prev_gen->addr_min = Addr;
			if(Addr > prev_gen->addr_max)
				prev_gen->addr_max = Addr;

			if(prev_gen->type != GENTYPE_JCTSTART)
			{
				// Check if we've hit a new high or low x/y position
				x = (!Level_7Vertical) ? vram_virtual_x(Addr) : vram_virtual_x_v(Addr);
				y = (!Level_7Vertical) ? vram_virtual_y(Addr) : vram_virtual_y_v(Addr);

				if(x < prev_gen->xs)
					prev_gen->xs = x;
				if(x > prev_gen->xe)
					prev_gen->xe = x;

				if(y < prev_gen->ys)
					prev_gen->ys = y;
				if(y > prev_gen->ye)
					prev_gen->ye = y;
			}


			// Record generator index onto tile_id_grid (acceptable range only)
			// This will allow us to later identify what tiles actually belong
			// to this generator, a finer detection than just the rectangle.
			if(Addr <= TILEMEM_END)
				NoDice_the_level.tile_id_grid[Addr - TILEMEM_BASE] = prev_gen->index;
		}
	}

	if(Addr >= MEM_A_START && Addr <= MEM_A_END)
		_RAM[Addr - MEM_A_START] = Value;
	else if(Addr >= MEM_B_START && Addr <= MEM_B_END)
		_RAM[Addr - MEM_B_START + MEM_A_END + 1] = Value;
	else if(Addr == MMC3_COMMAND)
		MMC3_Command = Value;
	else if(Addr == MMC3_PAGE)
	{
		//printf("*** Page change %s to %i\n", (MMC3_Command == MMC3_8K_TO_PRG_A000) ? "A000" : "C000", Value );

		if(MMC3_Command == MMC3_8K_TO_PRG_A000)
			_PRG_B = &_PRG[Value * MMC3_BANKSIZE];
		else if(MMC3_Command == MMC3_8K_TO_PRG_C000)
			_PRG_C = &_PRG[Value * MMC3_BANKSIZE];
		else
			fprintf(stderr, "Warning: Unsupported MMC3 command %02X\n", MMC3_Command);

		MMC3_Command = 0;
	}
	else
		fprintf(stderr, "Warning: Out of range write %04X\n", Addr);
}

byte Rd6502(register word Addr)
{
	if(is_loading_level)
	{
		if((Addr == LeveLoad_Generators) || (Addr == LeveLoad_FixedSizeGens) || (Addr == LoadLevel_StoreJctStart))
		{
			// Allocate new generator
			struct NoDice_the_level_generator *g = (struct NoDice_the_level_generator *)malloc(sizeof(struct NoDice_the_level_generator));

			if(Addr == LeveLoad_Generators)
			{
				// Set type
				g->type = GENTYPE_VARIABLE;

				// To determine which generator we're executing, the format is:
				// The upper 3 bits of Temp_Var15 select a multiple of 15
				// The upper 4 bits of LL_ShapeDef hold an offset
				// Add those together and subtract 1
				g->id = ((Temp_Var15 >> 5) * 15) + (LL_ShapeDef >> 4) - 1;

				// p1 is a guarantee, lower 4 bits of LL_ShapeDef, value of 0-15
				g->p[0] = LL_ShapeDef & 0xF;

				// p2 MAY be used, next byte after the base generator was defined, but not always!
				g->p[1] = Rd6502(MAKE16(Level_LayPtr_AddrH, Level_LayPtr_AddrL));
			}
			else if(Addr == LeveLoad_FixedSizeGens)
			{
				// Set type
				g->type = GENTYPE_FIXED;

				// To determine which generator we're executing, the format is:
				// The upper 3 bits of Temp_Var15, shifted down 1
				// Add LL_ShapeDef
				g->id = ((Temp_Var15 & 0xE0) >> 1) + LL_ShapeDef;

				// No params (ever?)
				g->p[0] = 0;
				g->p[1] = 0;
			}
			else
			{
				// Set type
				g->type = GENTYPE_JCTSTART;

				// Junction start index (lower 4 bits of Temp_Var15) is the ID
				g->id = (Temp_Var15 & 0xF);

				// Temp_Var16 is stored in Level_JctYLHStart array
				g->p[0] = Temp_Var16;

				// LL_ShapeDef is stored in Level_JctXLHStart array
				g->p[1] = LL_ShapeDef;

				// FIXME: Do we need this too?
				NoDice_the_level.Level_JctYLHStart[g->id] = Temp_Var16;
				NoDice_the_level.Level_JctXLHStart[g->id] = LL_ShapeDef;
			}

			// Assign current address
			g->addr_start = MAKE16(Map_Tile_AddrH, Map_Tile_AddrL) + TileAddr_Off;

			// Terminate list
			g->next = NULL;

			if(cur_gen == NULL)
			{
				g->prev = NULL;
				NoDice_the_level.generators = g;
				cur_gen = g;

				// If there's no previous generator, we start on index zero
				g->index = 0;
			}
			else
			{
				// Patch in values for the previous generator
				// -3 for the same reason as the initial calculation;
				// we're already that far ahead!
				prev_gen_patch(-3);

				g->prev = cur_gen;
				cur_gen->next = g;
				cur_gen = cur_gen->next;

				// For all subsequent generators, the index is the previous index + 1
				g->index = g->prev->index + 1;
			}

			// Mark start of generator
			// NOTE: -3 because by the execution flow, by the time it has decided
			// which routine to call, it has already read in the 3 primary bytes
			// required by all generators, so the actual start is 3 bytes ago...
			prev_gen_start_addr = MAKE16(Level_LayPtr_AddrH, Level_LayPtr_AddrL) - 3;
			prev_gen = g;

			// Reset min/max finders
			prev_gen->addr_min = 0xFFFF;
			prev_gen->addr_max = 0x0000;
			prev_gen->xs = 0xFFFF;
			prev_gen->ys = 0xFFFF;
			prev_gen->xe = 0x0000;
			prev_gen->ye = 0x0000;
		}
	}	// is_loading_level

	// 0xFFFC, normally the "RESET" vector, will be used as the termination address
	if(Addr == 0xFFFC)
		NoDice_Run6502_Stop = RUN6502_STOP_END;			// Flag execution as terminated
	else if(Addr >= 0xFFFA)
		return (Addr & 1) ? 0xFF : 0xF9;	// Handle interrupt vectors with 0xFFF9 (mainly in case of BRK)
	else if(Addr == 0xFFF9)
		return 0x40;					// 0xFFF9 will just return RTI
	else if(Addr >= MEM_A_START && Addr <= MEM_A_END)
		return _RAM[Addr - MEM_A_START];
	else if(Addr >= MEM_B_START && Addr <= MEM_B_END)
		return _RAM[Addr - MEM_B_START + MEM_A_END + 1];
	else if(Addr >= SCRATCH_START && Addr <= SCRATCH_END && is_loading_level)
		// Scratch space for modified levels; see defs for SCRATCH_START/END
		return _PRG_FakeScratch[Addr - SCRATCH_START];
	else if(Addr >= PRG_A_START && Addr <= PRG_A_END)
		return _PRG_A[Addr - PRG_A_START];
	else if(Addr >= PRG_B_START && Addr <= PRG_B_END)
		return _PRG_B[Addr - PRG_B_START];
	else if(Addr >= PRG_C_START && Addr <= PRG_C_END)
		return _PRG_C[Addr - PRG_C_START];
	else if(Addr >= PRG_D_START && Addr <= PRG_D_END)
		return _PRG_D[Addr - PRG_D_START];
	else
		fprintf(stderr, "Warning: Out of range read %04X\n", Addr);

	return 0xFF;
}

byte Patch6502(register byte Op,register M6502 *R)
{
	// Illegal opcodes are illegal opcodes
	return 0;
}

byte Loop6502(register M6502 *R)
{
	// Run until we get a stop reason!
	return (NoDice_Run6502_Stop == RUN6502_STOP_NOTSTOPPED) ? INT_NONE : INT_QUIT;
}


static void rom_Reset6502(int ram_clear)
{
	// Set up default ROM banks -- _PRG_A and _PRG_D are always (for SMB3) the last two PRG banks
	// The middle are 00 and 01 until otherwise
	_PRG_A = &_PRG[PRG_size - (MMC3_BANKSIZE * 2)];
	_PRG_B = &_PRG[0];
	_PRG_C = &_PRG[MMC3_BANKSIZE];
	_PRG_D = &_PRG[PRG_size - (MMC3_BANKSIZE * 1)];

	// Clear RAM
	if(ram_clear)
		memset(_RAM, 0, sizeof(_RAM));

	// Reset
	Reset6502(&CPU_Context);

	// Set stack to RTS to a hardcoded address which will terminate the
	// execution; RTS goes to address+1, so 0xFFFB will return to
	// the address 0xFFFC, which would be the Reset vector...
	_RAM[MEM_A_START + 0x1FE] = 0xFB;	// Low
	_RAM[MEM_A_START + 0x1FF] = 0xFF;	// High
	CPU_Context.S = 0xFD;

	// Clear flag for next run
	NoDice_Run6502_Stop = RUN6502_STOP_NOTSTOPPED;

	// Not loading a level unless you say so!
	is_loading_level = 0;
}


static int _rom_load_symbols()
{
	struct ROM_label *label_cur = NULL, *label_next;
	FILE *rom;

	// Free old symbols, if any
	while(label_cur != NULL)
	{
		label_next = label_cur->next;
		free(label_cur);
		label_cur = label_next;
	}

	ROM_labels = NULL;
	label_cur = NULL;

	// Load FNS file
	sprintf(_buffer, "%s" EXT_SYMBOLS, NoDice_config.filebase);
	rom = fopen(_buffer, "r");

	if(rom == NULL)
	{
		snprintf(_error_msg, ERROR_MSG_LEN, "Failed to open binary %s", _buffer);
		return 0;
	}

	while(fgets(_buffer, BUFFER_LEN, rom) != NULL)
	{
		char label[32];
		unsigned int addr;

		// Remove any comments
		char *semi = strchr(_buffer, ';');
		if(semi != NULL)
			*semi = '\0';

		// Pull out the label and its assigned address
		if(sscanf(_buffer, "%31s = $%04X", label, &addr) == 2)
		{
			// If sscanf returned 2 fields parsed, we fill in...
			if(ROM_labels == NULL)
			{
				// First label...
				ROM_labels = (struct ROM_label *)malloc(sizeof(struct ROM_label));
				label_cur = ROM_labels;
			}
			else
			{
				// Subsequent labels...
				label_cur->next = (struct ROM_label *)malloc(sizeof(struct ROM_label));
				label_cur = label_cur->next;
			}

			// Terminate the label set thus far
			label_cur->next = NULL;

			// Copy in the label data
			strcpy(label_cur->label, label);
			label_cur->address = (unsigned short)addr;
		}
	}

	fclose(rom);

	return 1;
}


int _rom_load()
{
	FILE *rom;

	// Load NES file
	sprintf(_buffer, "%s" EXT_ROM, NoDice_config.filebase);
	rom = fopen(_buffer, "rb");

	if(rom == NULL)
	{
		snprintf(_error_msg, ERROR_MSG_LEN, "Failed to open binary %s", _buffer);
		return 0;
	}

	// Check for iNES header
	fread(_buffer, sizeof(char), 4, rom);
	if(strncmp(_buffer, "NES\x1A", 4))
	{
		snprintf(_error_msg, ERROR_MSG_LEN, "%s" EXT_ROM " does not have a valid iNES header", NoDice_config.filebase);
		return 0;
	}

	// Get number of 16KB PRG pages and multiply to get PRG size
	PRG_size = fgetc(rom) * 16384;

	// Get number of 8KB CHR pages and multiply to find number of 1KB CHR banks (MMC3 standard)
	CHR_banks = fgetc(rom) * 8;	// because 8KB of CHR means 8 MMC3 CHR banks

	// Skip rest of iNES header
	fseek(rom, 10, SEEK_CUR);

	// Allocate and read
	_PRG = (rom_t)malloc(PRG_size);
	fread((void *)_PRG, sizeof(char), PRG_size, rom);


	// The CHR memory is in a strange planar format unique to the NES.
	// Each tile is 8x8 pixels at 2bpp depth so:
	// In total each tile is (8px * 2bpp * 8) = 128 bits / 8 = 16 bytes
	// We'll decode it to regular 8bpp linear memory for ease of use...
	{
		int tile, row, chrpos = 0;
		char tile_buffer[16];	// Read in one 8x8 tile for decoding

		// Allocate enough space to store CHR as 8bpp
		// In 8bpp, each 8x8 occupies 8 * 8 * 8bpp = 512 bits / 8 = 64 bytes
		unsigned char *CHR = (unsigned char *)malloc((int)CHR_banks * 64 * 64);

		// Each bank is 1KB of PPU data, so given that each tile is 16
		// bytes, each bank holds 1024 / 16 = 64 tiles... so the total
		// tiles we're going to pull in is 64 * CHR_banks
		for(tile = 0; tile < 64 * CHR_banks; tile++)
		{
			// Read tile in whole
			fread(tile_buffer, sizeof(char), 16, rom);

			// Break down all 8 rows
			for(row = 0; row < 8; row++)
			{
				unsigned char a = tile_buffer[ row ];
				unsigned char b = tile_buffer[ row + 8];

				CHR[chrpos++] = ((a >> 7) & 1) | ((b >> 6) & 2);
				CHR[chrpos++] = ((a >> 6) & 1) | ((b >> 5) & 2);
				CHR[chrpos++] = ((a >> 5) & 1) | ((b >> 4) & 2);
				CHR[chrpos++] = ((a >> 4) & 1) | ((b >> 3) & 2);
				CHR[chrpos++] = ((a >> 3) & 1) | ((b >> 2) & 2);
				CHR[chrpos++] = ((a >> 2) & 1) | ((b >> 1) & 2);
				CHR[chrpos++] = ((a >> 1) & 1) | ((b     ) & 2);
				CHR[chrpos++] = ((a     ) & 1) | ((b << 1) & 2);
			}
		}

		// Couldn't assign the items under the readonly _CHR after all...
		_CHR = CHR;
	}

	// Close ROM
	fclose(rom);

	// Reset and prepare for new execution
	rom_Reset6502(1);

	// Load symbols
	if(!_rom_load_symbols())
		return 0;

	// Resolve RAM labels
	if(!_ram_resolve_labels())
		return 0;

	return 1;
}

void _rom_shutdown()
{
	struct ROM_label *label_cur = ROM_labels, *label_next;

	if(_PRG != NULL)
	{
		free((void *)_PRG);
		_PRG = NULL;
	}

	if(_CHR != NULL)
	{
		free((void *)_CHR);
		_CHR = NULL;
	}

	while(label_cur != NULL)
	{
		label_next = label_cur->next;
		free(label_cur);
		label_cur = label_next;
	}

	ROM_labels = NULL;
}


int NoDice_PRG_refresh()
{
	int PRG_size_check = PRG_size;
	FILE *rom;

	// Load NES file
	sprintf(_buffer, "%s" EXT_ROM, NoDice_config.filebase);
	rom = fopen(_buffer, "rb");

	if(rom == NULL)
	{
		snprintf(_error_msg, ERROR_MSG_LEN, "Failed to open binary %s", _buffer);
		return 0;
	}

	// Check for iNES header
	fread(_buffer, sizeof(char), 4, rom);
	if(strncmp(_buffer, "NES\x1A", 4))
	{
		snprintf(_error_msg, ERROR_MSG_LEN, "%s" EXT_ROM " does not have a valid iNES header", NoDice_config.filebase);
		return 0;
	}

	// Get number of 16KB PRG pages and multiply to get PRG size
	PRG_size = fgetc(rom) * 16384;

	// This is unlikely to happen unless you were screwing around...
	// We don't want to reallocate because it might break current
	// references in the editor, just reload PRG, so don't screw
	// with the header right now please!~
	if(PRG_size != PRG_size_check)
	{
		snprintf(_error_msg, ERROR_MSG_LEN, "Failed to refresh PRG cache because PRG changed size?!  Recommend shutting down and restarting NoDice if modifying ROM header!");
		return 0;
	}

	// This function will not load CHR
	fgetc(rom);

	// Skip rest of iNES header
	fseek(rom, 10, SEEK_CUR);

	// Re-read PRG
	fread((void *)_PRG, sizeof(char), PRG_size, rom);

	// Close ROM
	fclose(rom);


	// Need to reload symbols
	if(!_rom_load_symbols())
		return 0;

	// Resolve RAM labels
	if(!_ram_resolve_labels())
		return 0;

	return 1;
}


static const unsigned char *rom_make_ptr_for_addr(unsigned short Addr)
{
	if(Addr >= PRG_A_START && Addr <= PRG_A_END)
		return &_PRG_A[Addr - PRG_A_START];
	else if(Addr >= PRG_B_START && Addr <= PRG_B_END)
		return &_PRG_B[Addr - PRG_B_START];
	else if(Addr >= PRG_C_START && Addr <= PRG_C_END)
		return &_PRG_C[Addr - PRG_C_START];
	else if(Addr >= PRG_D_START && Addr <= PRG_D_END)
		return &_PRG_D[Addr - PRG_D_START];

	return NULL;
}


unsigned short NoDice_get_addr_for_label(const char *label)
{
	// FIXME: This should be redone as a sorted array so we can implement a binary search!!

	struct ROM_label *label_cur = ROM_labels;
	while(label_cur != NULL)
	{
		// If label found, return address
		if(!strcmp(label_cur->label, label))
			return label_cur->address;

		label_cur = label_cur->next;
	}

	snprintf(_error_msg, ERROR_MSG_LEN, "Failed to find label \"%s\" in FNS listing", label);

	// Label not found
	return 0xFFFF;
}


static void rom_MMC3_set_pages(unsigned char page_A000, unsigned char page_C000)
{
	Wr6502(_ram[MMC3PAGE_A000].address, page_A000);
	Wr6502(_ram[MMC3PAGE_C000].address, page_C000);

	// Execute page changes
	Wr6502(MMC3_COMMAND, MMC3_8K_TO_PRG_A000);
	Wr6502(MMC3_PAGE, PAGE_A000);

	Wr6502(MMC3_COMMAND, MMC3_8K_TO_PRG_C000);
	Wr6502(MMC3_PAGE, PAGE_C000);
}


void NoDice_load_level(unsigned char tileset, const char *level_layout, const char *object_layout)
{
	unsigned short address, object_address;

	if(tileset > 0)
	{
		if( (address = NoDice_get_addr_for_label(level_layout)) == 0xFFFF)
		{
			NoDice_Run6502_Stop = RUN6502_INIT_ERROR;
			return;
		}

		if( (object_address = NoDice_get_addr_for_label(object_layout)) == 0xFFFF)
		{
			NoDice_Run6502_Stop = RUN6502_INIT_ERROR;
			return;
		}
	}
	else
	{
		// These labels are not resolved in world map mode
		address = (unsigned short)strtoul(level_layout, NULL, 0) - 1;	// This actually specifies the world number (zero-based)
		object_address = 0;
	}

	NoDice_load_level_by_addr(tileset, address, object_address);
}


void NoDice_load_level_by_addr(unsigned char tileset, unsigned short address, unsigned short object_address)
{
	// The setup for loading a level layout looks like this:

	// 	LDY Level_Tileset
	//	LDA PAGE_C000_ByTileset,Y
	//	STA PAGE_C000
	//	LDA PAGE_A000_ByTileset,Y
	//	STA PAGE_A000
	//	JSR PRGROM_Change_Both2
	//
	//	JSR LevelLoad_ByTileset			; Load the level layout data!

	unsigned short
		PAGE_A000_ByTileset, PAGE_C000_ByTileset, LevelLoad_ByTileset,
		Level_BG_Pages1, Level_BG_Pages2, TileLayout_ByTileset,
		Palette_By_Tileset;

	unsigned short header_addr;
	int i;

	// Resolve labels
	if( (PAGE_A000_ByTileset = NoDice_get_addr_for_label("PAGE_A000_ByTileset")) == 0xFFFF)
	{
		NoDice_Run6502_Stop = RUN6502_INIT_ERROR;
		return;
	}

	if( (PAGE_C000_ByTileset = NoDice_get_addr_for_label("PAGE_C000_ByTileset")) == 0xFFFF)
	{
		NoDice_Run6502_Stop = RUN6502_INIT_ERROR;
		return;
	}


	if( (Level_BG_Pages1 = NoDice_get_addr_for_label("Level_BG_Pages1")) == 0xFFFF)
	{
		NoDice_Run6502_Stop = RUN6502_INIT_ERROR;
		return;
	}

	if( (Level_BG_Pages2 = NoDice_get_addr_for_label("Level_BG_Pages2")) == 0xFFFF)
	{
		NoDice_Run6502_Stop = RUN6502_INIT_ERROR;
		return;
	}

	if( (TileLayout_ByTileset = NoDice_get_addr_for_label("TileLayout_ByTileset")) == 0xFFFF)
	{
		NoDice_Run6502_Stop = RUN6502_INIT_ERROR;
		return;
	}

	if( (Palette_By_Tileset = NoDice_get_addr_for_label("Palette_By_Tileset")) == 0xFFFF)
	{
		NoDice_Run6502_Stop = RUN6502_INIT_ERROR;
		return;
	}


	if(tileset > 0)
	{
		if( (LevelLoad_ByTileset = NoDice_get_addr_for_label("LevelLoad_ByTileset")) == 0xFFFF)
		{
			NoDice_Run6502_Stop = RUN6502_INIT_ERROR;
			return;
		}

		if( (LoadLevel_StoreJctStart = NoDice_get_addr_for_label("LoadLevel_StoreJctStart")) == 0xFFFF)
		{
			NoDice_Run6502_Stop = RUN6502_INIT_ERROR;
			return;
		}

		if( (LeveLoad_Generators = NoDice_get_addr_for_label("LeveLoad_Generators")) == 0xFFFF)
		{
			NoDice_Run6502_Stop = RUN6502_INIT_ERROR;
			return;
		}

		if( (LeveLoad_FixedSizeGens = NoDice_get_addr_for_label("LeveLoad_FixedSizeGens")) == 0xFFFF)
		{
			NoDice_Run6502_Stop = RUN6502_INIT_ERROR;
			return;
		}


		// Reset and prepare for new execution (best to start clean!)
		rom_Reset6502(1);

		// We ARE loading a level!
		is_loading_level = 1;

		// If loaded a level previously, free the level generator lists
		_rom_free_level_list();

		// Configure for level load
		Wr6502(_ram[LEVEL_LAYPTR_ADDRL].address, LOW(address));
		Wr6502(_ram[LEVEL_LAYPTR_ADDRH].address, HIGH(address));
		Wr6502(_ram[LEVEL_TILESET].address, tileset);

		// Set up the pages and call an imitation of PRGROM_Change_Both2
		rom_MMC3_set_pages(Rd6502(PAGE_A000_ByTileset + Level_Tileset), Rd6502(PAGE_C000_ByTileset + Level_Tileset));

		// Before we run the emulation, let's capture header data...
		header_addr = MAKE16(Level_LayPtr_AddrH, Level_LayPtr_AddrL);
		NoDice_the_level.header.alt_level_layout =
				MAKE16(Rd6502(header_addr+1), Rd6502(header_addr+0));
		NoDice_the_level.header.alt_level_objects =
				MAKE16(Rd6502(header_addr+3), Rd6502(header_addr+2));

		for(i = 0; i < LEVEL_HEADER_COUNT; i++)
			NoDice_the_level.header.option[i] = Rd6502(header_addr+4+i);

		// Set all jct starts to 0xFF
		for(i = 0; i < LEVEL_JCT_STARTS; i++)
		{
			NoDice_the_level.Level_JctXLHStart[i] = 0xFF;
			NoDice_the_level.Level_JctYLHStart[i] = 0xFF;
		}

		// Force PC to the LevelLoad_ByTileset subroutine
		CPU_Context.PC.W = LevelLoad_ByTileset;
		Run6502(&CPU_Context);

		// The last generator doesn't get a chance to be patched, so we'll do it now
		prev_gen_patch(0);
	}
	else
	{
		unsigned short Map_Reload_with_Completions, Map_Init;

		// World Map only (Tileset 0)
		// Loads the grid tiles
		if( (Map_Reload_with_Completions = NoDice_get_addr_for_label("Map_Reload_with_Completions")) == 0xFFFF)
		{
			NoDice_Run6502_Stop = RUN6502_INIT_ERROR;
			return;
		}

		if( (Map_Init = NoDice_get_addr_for_label("Map_Init")) == 0xFFFF)
		{
			NoDice_Run6502_Stop = RUN6502_INIT_ERROR;
			return;
		}

		// Not considered a "level" (i.e. no generators)
		is_loading_level = 0;


		/////////////////////////////////////////////////////////////////////
		// Load world map objects
		/////////////////////////////////////////////////////////////////////

		// Reset and prepare for new execution (best to start clean!)
		rom_Reset6502(1);

		// World_Num is the value held in "address"
		Wr6502(_ram[WORLD_NUM].address, (unsigned char)address);

		// Set up the pages and call an imitation of PRGROM_Change_Both2
		rom_MMC3_set_pages(Rd6502(PAGE_A000_ByTileset + Level_Tileset), Rd6502(PAGE_C000_ByTileset + Level_Tileset));

		// Force PC to the Map_Reload_with_Completions subroutine
		CPU_Context.PC.W = Map_Init;
		Run6502(&CPU_Context);


		/////////////////////////////////////////////////////////////////////
		// Load world map tiles
		/////////////////////////////////////////////////////////////////////

		// Reset and prepare for new execution (best to start clean!)
		rom_Reset6502(0);

		// Set up the pages and call an imitation of PRGROM_Change_Both2
		rom_MMC3_set_pages(MAP_LAYOUT_BANK, PAGE_C000);	// FIXME: Hardcoded

		// Force PC to the Map_Reload_with_Completions subroutine
		CPU_Context.PC.W = Map_Reload_with_Completions;
		Run6502(&CPU_Context);

	}

	// When we get here, the level was HOPEFULLY decompressed successfully!
	// Now to populate the "NoDice_the_level" structure...

	{
		int i;

		// Find the tileset that matches the tileset ID of this level
		for(i = 0; i < NoDice_config.game.tileset_count; i++)
		{
			if(NoDice_config.game.tilesets[i].id == tileset)
			{
				NoDice_the_level.tileset = &NoDice_config.game.tilesets[i];
				break;
			}
		}
	}

	if(tileset > 0)
	{
		// Regular level has lots of info
		NoDice_the_level.header.alt_level_tileset = Level_AltTileset;
		NoDice_the_level.header.is_vert = Level_7Vertical;
		NoDice_the_level.header.total_screens = Level_Width + 1;
		NoDice_the_level.header.vert_scroll = (!NoDice_the_level.header.is_vert) ?
			Vert_Scroll : (Vert_Scroll_Hi * SCREEN_BYTESIZE_V / SCREEN_WIDTH * TILESIZE);
		NoDice_the_level.bg_page_1 = Rd6502(Level_BG_Pages1 + Level_BG_Page1_2);
		NoDice_the_level.bg_page_2 = Rd6502(Level_BG_Pages2 + Level_BG_Page1_2);
	}
	else
	{
		int screens = 0;
		unsigned short map_data_start;

		// To count the screens available, we need to quickly run
		// through the (very simple) world map data; it is just
		// raw terminated by 0xFF...

		// Generate the layout label name
		snprintf(_buffer, BUFFER_LEN, "W%i_Map_Layout", World_Num+1);

		// Attempt to get the start of map data... if we fail,
		// we'll just assume 4 screens, but that could be
		// dangerous to an editor...!!
		if( (map_data_start = NoDice_get_addr_for_label(_buffer)) != 0xFFFF)
		{
			int byte_count = 0;

			// Found it!  Now to run through it until we hit 0xFF...
			// Or 4 map screens... in case the data's bad...

			while(Rd6502(map_data_start++) != 0xFF)
				byte_count++;

			// Okay, end of loop, divide byte_count by map screen size
			// to get the number of screens...
			screens = byte_count / SCREEN_BYTESIZE_M;
		}

		// Failed to find map or came up short; assume 4 screens
		if(screens == 0)
			screens = 4;

		// World map has some assumptions
		NoDice_the_level.header.alt_level_tileset = 0;
		NoDice_the_level.header.is_vert = 0;
		NoDice_the_level.header.total_screens = screens;
		NoDice_the_level.header.vert_scroll = Vert_Scroll;
		NoDice_the_level.bg_page_1 = 20;
		NoDice_the_level.bg_page_2 = 22;
	}

	NoDice_the_level.tiles = &_RAM[0x6000 - MEM_B_START + MEM_A_END + 1];

	// Copy in the four quarters of tiles into the array...
	{
		unsigned short layoutAddr = MAKE16(
					Rd6502(TileLayout_ByTileset + (tileset * 2) + 1),
					Rd6502(TileLayout_ByTileset + (tileset * 2) + 0));
		int tile, quarter;
		for(quarter = 0; quarter < 4; quarter++)
		{
			for(tile = 0; tile < 256; tile++)
				// The layouts are stored as 4 contiguous 256 byte arrays
				NoDice_the_level.tile_layout[tile][quarter] = Rd6502(layoutAddr++);
		}
	}


	// Copy in the BG/SPR palette values -- requires PRG027 to be loaded in!
	rom_MMC3_set_pages(27, PAGE_C000);
	{
		// First get offset to palette table unique to this Tileset
		unsigned short pal_base_addr = MAKE16(
					Rd6502(Palette_By_Tileset + (tileset * 2) + 1),
					Rd6502(Palette_By_Tileset + (tileset * 2) + 0));

		// Then read colors as offset by PalSel_Tile_Colors
		int c, base = PalSel_Tile_Colors * 16;
		for(c = 0; c < 16; c++)
			NoDice_the_level.bg_pal[c] = Rd6502(pal_base_addr + base + c);

		base = PalSel_Obj_Colors * 16;
		for(c = 0; c < 16; c++)
			NoDice_the_level.spr_pal[c] = Rd6502(pal_base_addr + base + c);
	}


	// Tasks for regular level (not world map) only...
	if(tileset > 0)
	{
		NoDice_the_level.addr_start = address;
		NoDice_the_level.addr_end = MAKE16(Level_LayPtr_AddrH, Level_LayPtr_AddrL);

		/*
		{
			const char *names[3] = { "LevelJct", "Variable", "Fixed   "};
			struct NoDice_the_level_generator *cur;
			FILE *f = fopen("dump.txt", "w");

			for(cur = NoDice_the_level.generators; cur != NULL; cur = cur->next)
			{
				fprintf(f, "%s\tid = %02i\taddr = %04X/%04X/%04X %i, %i to %i, %i\tsize = %i\tp1 = $%02X\tp2 = $%02X\n", names[cur->type], cur->id, cur->addr_start, cur->addr_min, cur->addr_max, cur->xs, cur->ys, cur->xe, cur->ye, cur->size, cur->p[0], cur->p[1]);
			}

			fclose(f);
		}
		*/

		// If not the empty object set...
		if(object_address != 0xFFFF)
		{
			is_loading_level = 0;

			// Set pages to load object data
			rom_MMC3_set_pages(PAGE_A000, OBJ_BANK);

			// Grab and hold the mysterious unknown-apparent-no-purpose byte
			NoDice_the_level.object_unknown = Rd6502(object_address++);

			NoDice_the_level.object_count = 0;
			for(i = 0; (i < OBJS_MAX*3) && (Rd6502(object_address + i + 0) != 0xFF); i+=3)
			{
				struct NoDice_the_level_object *object = &NoDice_the_level.objects[NoDice_the_level.object_count++];

				object->id = Rd6502(object_address + i + 0);
				object->col = Rd6502(object_address + i + 1);
				object->row = Rd6502(object_address + i + 2);
			}
		}
	}
	else
	{
		// World Map
		int i;
		unsigned short Wx_ByRowType, Wx_ByScrCol, Wx_ObjSets, Wx_LevelLayout;

		// Copy in map objects
		NoDice_the_level.object_count = 0;

		// Warp Zone bypass does not load objects
		// FIXME: This check isn't the same as the others (i.e. as string)
		if((address + 1) != strtoul(NoDice_config.game.options.warpzone, NULL, 0))
		{
			for(i = 0; i < MOBJS_MAX; i++)
			{
				unsigned short x = (Map_Objects_XHi(i) << 8) | Map_Objects_XLo(i);
				struct NoDice_the_level_object *object = &NoDice_the_level.objects[NoDice_the_level.object_count++];

				// Technically map objects have full pixel placement possibility,
				// but this is never used, and for now this is more compatible
				// with the existing editor structure.
				object->id = Map_Objects_ID(i);
				object->col = x / TILESIZE;
				object->row = (Map_Objects_Y(i) / TILESIZE) - MAP_OBJECT_BASE_ROW;	// Slight adjustment since there's unused vertical space

				//printf("%i %i %i\n", object->id, object->row, object->col);

				// Copy in map object items
				NoDice_the_level.map_object_items[i] = Map_Objects_Itm(i);
			}
		}

		// Unfortunately the map links aren't copied to RAM anywhere
		// and there's no real way to check limits on it so some
		// assumptions will have to be employed to make them up...

		// In this case, we'll assume the count comes between the
		// labels Wx_ByRowType and Wx_ByScrCol

		snprintf(_buffer, BUFFER_LEN, "W%i_ByRowType", World_Num + 1);
		if( (Wx_ByRowType = NoDice_get_addr_for_label(_buffer)) == 0xFFFF)
		{
			NoDice_Run6502_Stop = RUN6502_INIT_ERROR;
			return;
		}

		snprintf(_buffer, BUFFER_LEN, "W%i_ByScrCol", World_Num + 1);
		if( (Wx_ByScrCol = NoDice_get_addr_for_label(_buffer)) == 0xFFFF)
		{
			NoDice_Run6502_Stop = RUN6502_INIT_ERROR;
			return;
		}

		snprintf(_buffer, BUFFER_LEN, "W%i_ObjSets", World_Num + 1);
		if( (Wx_ObjSets = NoDice_get_addr_for_label(_buffer)) == 0xFFFF)
		{
			NoDice_Run6502_Stop = RUN6502_INIT_ERROR;
			return;
		}

		snprintf(_buffer, BUFFER_LEN, "W%i_LevelLayout", World_Num + 1);
		if( (Wx_LevelLayout = NoDice_get_addr_for_label(_buffer)) == 0xFFFF)
		{
			NoDice_Run6502_Stop = RUN6502_INIT_ERROR;
			return;
		}

		// Get total map links
		NoDice_the_level.map_link_count = Wx_ByScrCol - Wx_ByRowType;

		// Set up the pages and call an imitation of PRGROM_Change_Both2
		rom_MMC3_set_pages(MAP_LAYOUT_BANK, PAGE_C000);	// FIXME: Hardcoded

		// Read them in...
		for(i = 0; i < NoDice_the_level.map_link_count; i++)
		{
			struct NoDice_map_link *link = &NoDice_the_level.map_links[i];

			// Single byte
			link->row_tileset = Rd6502(Wx_ByRowType + i);
			link->col_hi = Rd6502(Wx_ByScrCol + i);

			// Double byte
			link->object_addr = MAKE16(Rd6502(Wx_ObjSets + (i * 2 + 1)), Rd6502(Wx_ObjSets + (i * 2 + 0)));
			link->layout_addr = MAKE16(Rd6502(Wx_LevelLayout + (i * 2 + 1)), Rd6502(Wx_LevelLayout + (i * 2 + 0)));
		}
	}
}


// Splits a video address into Temp_Var15/16 components
static void vaddr_to_level(unsigned short addr, unsigned char *t15, unsigned char *t16)
{
	unsigned short inter_screen_offset;
	int is_vert = NoDice_the_level.header.is_vert;

	// Make "addr" relative
	addr -= MEM_B_START;

	// Calculate inter-screen offset
	inter_screen_offset = !is_vert ? (addr % SCREEN_BYTESIZE) : (addr % SCREEN_BYTESIZE_V);

	// Clear t15/16
	*t15 = 0;
	*t16 = 0;

	// Upper 4 bits of Temp_Var16 are screen-select
	*t16 |= ((!is_vert ? (addr / SCREEN_BYTESIZE) : (addr / SCREEN_BYTESIZE_V)) & 0x0F) << 4;

	// Bit 4 (0x10) of Temp_Var15 specifies "lower half" of screen (0x100 - 0x1B0) for non-vertical
	if(!is_vert)	// only non-vertical levels have this issue
		*t15 |= (inter_screen_offset >= 0x100) ? 0x10 : 0x00;

	// Lower 4 bits of Temp_Var15 are the upper 4 bits of the inter-screen offset
	*t15 |= ((inter_screen_offset >> 4) & 0x0F);

	// Lower 4 bits of Temp_Var16 are the lower 4 bits of the inter-screen offset
	*t16 |= (inter_screen_offset & 0x0F);
}


static void rom_pack_level_header(unsigned char **ptr)
{
	int i;

	// Header goes in pretty straight
	*(*ptr)++ = LOW(NoDice_the_level.header.alt_level_layout);
	*(*ptr)++ = HIGH(NoDice_the_level.header.alt_level_layout);
	*(*ptr)++ = LOW(NoDice_the_level.header.alt_level_objects);
	*(*ptr)++ = HIGH(NoDice_the_level.header.alt_level_objects);

	for(i = 0; i < LEVEL_HEADER_COUNT; i++)
		*(*ptr)++ = NoDice_the_level.header.option[i];
}


// Packs level data into raw SMB3 standard form -> _PRG_FakeScratch
const unsigned char *NoDice_pack_level(int *size, int need_header)
{
	unsigned char t15, t16;
	struct NoDice_the_level_generator *gen = NoDice_the_level.generators;
	unsigned char *ptr = _PRG_FakeScratch;

	// If you want the SMB3 engine to load it, you absolutely need the header!
	// If this is just for the sake of an undo layer, you don't!
	if(need_header)
		rom_pack_level_header(&ptr);

	// Now to repack the generators...
	while(gen != NULL)
	{
		// Temp_Var15
		// Temp_Var16
		// LL_ShapeDef

		// Write each generator in correct form...
		switch(gen->type)
		{
		case GENTYPE_JCTSTART:

			// Upper 3 bits of Temp_Var15 are ALL set... i.e. Temp_Var15 = 111x xxxx
			// The lower 4 bits specify an index into the junction starts
			t15 = 0xE0 | (gen->id & 0xF);
			*ptr++ = t15;

			// The junction start itself follows, Y then X
			*ptr++ = NoDice_the_level.Level_JctYLHStart[gen->id & 0xF];
			*ptr++ = NoDice_the_level.Level_JctXLHStart[gen->id & 0xF];

			break;

		case GENTYPE_VARIABLE:

			// Decompose address into encoded form
			vaddr_to_level(gen->addr_start, &t15, &t16);

			// Upper 3 bits of Temp_Var15 select a multiple of 15 for the generator
			t15 |= ((gen->id / 15) & 0x07) << 5;

			// Commit Temp_Var15 and Temp_Var16
			*ptr++ = t15;
			*ptr++ = t16;

			// The upper 4 bits of LL_ShapeDef must NOT be zero, or else that specifies
			// a fixed-size generator; the loading code in SMB3 compensates for that by
			// requiring LL_ShapeDef > 0, hence why a subtraction of 1 is required and
			// also why it's a multiple of 15, not 16!
			// The lower 4 bits of LL_ShapeDef are the first parameter (range 0 to 15)
			*ptr++ =
				(((gen->id % 15) + 1) & 0x0F) << 4		// Upper 4 bits
				| (gen->p[0] & 0x0F);				// Lower 4 bits


			// If this generator is occupying more bytes than we can tolerate,
			// abort the execution now...
			if(gen->size >= 3+GEN_MAX_PARAMS)
			{
				NoDice_Run6502_Stop = RUN6502_GENERATOR_TOO_LARGE;
				break;
			}

			// Some generators may have additional parameters; commit them now!
			// Stock SMB3 only ever had one additional parameter when it came up, but
			// I'm trying to write this so that you could potentially have more ...
			for(t15 = 3; t15 < gen->size; t15++)
				*ptr++ = gen->p[t15 - 2];

			break;

		case GENTYPE_FIXED:

			// Decompose address into encoded form
			vaddr_to_level(gen->addr_start, &t15, &t16);

			// Upper 3 bits of Temp_Var15 are bits 4-6 of the generator ID
			t15 |= ((gen->id & 0x70) << 1);

			// Commit Temp_Var15 and Temp_Var16
			*ptr++ = t15;
			*ptr++ = t16;

			// Upper 4 bits of LL_ShapeDef are always zero (that's what
			// officially differentiates a variable-size from a fixed-size
			// generator) and the lower 4 bits area the lower 4 bits of
			// the generator ID...
			*ptr++ = (gen->id & 0x0F);

			break;
		}

		gen = gen->next;
	}

	// Terminator!
	*ptr++ = 0xFF;

	// Return size, if you want it
	if(size != NULL)
		*size = (int)(ptr - _PRG_FakeScratch);

	/*
	{
		unsigned char *cur = _PRG_FakeScratch + 9;
		int c = 0;
		FILE *f = fopen("dump.txt", "w");

		fputs("\t.byte ", f);

		while(cur != ptr)
		{
			fprintf(f, "$%02X", *cur++);

			if(++c == 16)
			{
				fputs("\n\t.byte ", f);
				c = 0;
			}
			else
				fputs(", ", f);
		}

		fclose(f);
	}
	*/

	return _PRG_FakeScratch;
}


// Reload the level; if undo_data is NULL, it uses current level data,
// otherwise it loads the feed from "undo_data" plus a header from the
// currently loaded level; this is mainly for use in an undo system.
void NoDice_load_level_raw_data(const unsigned char *data, int size, int has_header)
{
	if(data == NULL)
		// Pack the level, include the header, don't care about size
		NoDice_pack_level(NULL, 1);
	else
	{
		unsigned char *ptr = _PRG_FakeScratch;

		// We have raw data...

		// Do we need to supply the header?
		if(!has_header)
			rom_pack_level_header(&ptr);

		// Push the data in!
		while(size-- > 0)
			*ptr++ = *data++;

		/*
		{
			unsigned char *cur = _PRG_FakeScratch + 9;
			int c = 0;
			FILE *f = fopen("dump.txt", "w");

			fputs("\t.byte ", f);

			while(cur != ptr)
			{
				fprintf(f, "$%02X", *cur++);

				if(++c == 16)
				{
					fputs("\n\t.byte ", f);
					c = 0;
				}
				else
					fputs(", ", f);
			}

			fclose(f);
		}
		*/
	}

	// Reload level from scratch area
	NoDice_load_level_by_addr(NoDice_the_level.tileset->id, SCRATCH_START, 0xFFFF);
}


const unsigned char *NoDice_get_raw_CHR_bank(unsigned char bank)
{
	// If you pick an out of range bank, wrap to nearest valid bank
	if(bank >= CHR_banks)
		bank %= CHR_banks;

	// Each bank contains 64 tiles, and each tile is 64 bytes in our
	// decoded form, so return...
	return &_CHR[(int)bank * 64 * 64];
}


int NoDice_get_tilebank_free_space(unsigned char tileset)
{
	unsigned char bank_for_tileset;
	unsigned short PAGE_A000_ByTileset;
	static rom_t _PRG_tileset;
	int bank_offset;

	// Get bank for tileset
	if( (PAGE_A000_ByTileset = NoDice_get_addr_for_label("PAGE_A000_ByTileset")) == 0xFFFF)
		return 0;

	bank_for_tileset = Rd6502(PAGE_A000_ByTileset + Level_Tileset);

	// Point to end of tileset bank
	_PRG_tileset = &_PRG[bank_for_tileset * MMC3_BANKSIZE];

	// "Free space" in a bank is filled with 0xFFs
	// End of a level is 0xFF

	// So the best thing to do is start at the end and work backwards
	// to determine where the last 0xFF is to get an idea how many
	// bytes are "free" in the bank for this tileset...
	for(bank_offset = MMC3_BANKSIZE-1; bank_offset >= 0; bank_offset--)
	{
		if(_PRG_tileset[bank_offset] != 0xFF)
			break;
	}

	// Based on the value of bank_offset, we can determine the bytes free
	// Note that the offset is on the first non-0xFF since the end of the
	// bank, so really we want to be two steps forward.  One step to get
	// to the actual last 0xFF and one more to move passed that, assuming
	// that byte to be the tail end of the last level of the bank.
	return MMC3_BANKSIZE - (bank_offset + 2);
}


const unsigned char *NoDice_get_rest_table()
{
	unsigned short Music_RestH_LUT;

	if( (Music_RestH_LUT = NoDice_get_addr_for_label("Music_RestH_LUT")) == 0xFFFF)
	{
		snprintf(_error_msg, ERROR_MSG_LEN, "Could not find label Music_RestH_LUT");
		return NULL;
	}

	// Get the Music_RestH_LUT assignment out of the way...
	return rom_make_ptr_for_addr(Music_RestH_LUT);
}


int NoDice_get_music_context(struct NoDice_music_context *context, const char *header_index_name, const char *SEL_name, unsigned char music_index)
{
	unsigned char segment;
	unsigned short Music_Starts, Music_Ends, Music_Loops;
	unsigned short Music_IndexOffs, Music_Headers;

	// Start/End/Loop index (into Music_xxx_IndexOffs) values per music_index:
	// Music_xxx_Starts
	// Music_xxx_Ends
	// Music_xxx_Loops

	// Headers:
	// Music_xxx_IndexOffs	-- defines index into Music_xxx_Headers for given Start/End/Loop index
	// Music_xxx_Headers

	// Music spans page 28 and 29
	rom_MMC3_set_pages(28, 29);	// FIXME: Hardcoded, should probably allow others

	snprintf(_buffer, BUFFER_LEN, "Music_%s_IndexOffs", header_index_name);
	if( (Music_IndexOffs = NoDice_get_addr_for_label(_buffer)) == 0xFFFF)
	{
		snprintf(_error_msg, ERROR_MSG_LEN, "Could not find label %s", _buffer);
		return 0;
	}

	snprintf(_buffer, BUFFER_LEN, "Music_%s_Headers", header_index_name);
	if( (Music_Headers = NoDice_get_addr_for_label(_buffer)) == 0xFFFF)
	{
		snprintf(_error_msg, ERROR_MSG_LEN, "Could not find label %s", _buffer);
		return 0;
	}

	if( (context->rest_table = NoDice_get_rest_table()) == NULL )
		return 0;

	// Starts/Ends/Loops CAN be missing, this indicates a music set
	// of only single segments per index (i.e. Set 1) which will
	// imply that the start, end, and loop all equal music_index
	if(SEL_name != NULL)
	{
		snprintf(_buffer, BUFFER_LEN, "Music_%s_Starts", SEL_name);
		Music_Starts = NoDice_get_addr_for_label(_buffer);

		snprintf(_buffer, BUFFER_LEN, "Music_%s_Ends", SEL_name);
		Music_Ends = NoDice_get_addr_for_label(_buffer);

		snprintf(_buffer, BUFFER_LEN, "Music_%s_Loops", SEL_name);
		Music_Loops = NoDice_get_addr_for_label(_buffer);

		// Long music
		context->index_start = Rd6502(Music_Starts + music_index);
		context->index_end = Rd6502(Music_Ends + music_index);
		context->index_loop = Rd6502(Music_Loops + music_index);
	}
	else
	{
		// Short (single segment) music
		context->index_start = music_index;
		context->index_end = music_index;
		context->index_loop = music_index;
	}

	// Set count of segments
	context->music_segment_count = context->index_end - context->index_start + 1;

	// Load all segments into context...
	for(segment = context->index_start; segment <= context->index_end; segment++)
	{
		struct NoDice_music_segment *this_segment = &context->music_segments[segment - context->index_start];

		   // 0: Music_RestH_Base value (always divisible by $10; base part of index into PRG031's Music_RestH_LUT)
		   // 1-2: Address of music segment data (all tracks this segment, offsets to follow, except implied Square 2 zero)
		   // 3: Triangle track starting offset ($00 means disabled)
		   // 4: Square 1 track starting offset (cannot be disabled)
		   // 5: Noise track starting offset ($00 means disabled)
		   // 6: DCM track starting offset ($00 means disabled)

		// Get offset to header
		unsigned char header_offset = Rd6502(Music_IndexOffs + segment);

		// Load segment header data
		context->rest_table_base = Rd6502(Music_Headers + header_offset + 0);

		this_segment->segment_data = rom_make_ptr_for_addr(
			MAKE16(
				Rd6502(Music_Headers + header_offset + 2),
				Rd6502(Music_Headers + header_offset + 1)
			) );

		this_segment->track_starts[MUSICTRACK_SQUARE1] = Rd6502(Music_Headers + header_offset + 4);
		this_segment->track_starts[MUSICTRACK_SQUARE2] = 0x00;	// Always implied
		this_segment->track_starts[MUSICTRACK_TRIANGLE] = Rd6502(Music_Headers + header_offset + 3);
		this_segment->track_starts[MUSICTRACK_NOISE] = Rd6502(Music_Headers + header_offset + 5);
		this_segment->track_starts[MUSICTRACK_DMC] = Rd6502(Music_Headers + header_offset + 6);
	}

	return 1;
}


// FIXME: Remove this later
void NoDice_tile_test()
{
	word i;
	byte t = 0;
	for(i = MEM_B_START; i < MEM_B_START + 16*16; i++)
	{
		Wr6502(i, t++);
	}
}
