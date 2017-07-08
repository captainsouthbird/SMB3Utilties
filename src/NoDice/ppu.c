#include <stdio.h>
#include "NoDiceLib.h"
#include "NoDice.h"

static gui_surface_t *PPU_BG_VROM[4];

// 256 possible object IDs, although unlikely most will ever be used
// 256 possible map object IDs, VERY unlikely you'd ever come close
static struct _PPU_SPR
{
	int offset_x, offset_y;	// Upper left/top offset
	int width, height;
	gui_surface_t *surface;	// Actual complete sprite
} 	PPU_SPR_objects[256] = { { 0, 0, 0, 0, NULL } },
	PPU_SPR_mobjects[256] = { { 0, 0, 0, 0, NULL } },
	*PPU_SPR = NULL;

// Based on http://nesdev.parodius.com/pal.txt
static const struct _nes_palette
{
	unsigned char r, g, b;
} nes_palette[64] =
{
	{ 117, 117,  117 }, {  39,  27,  143 }, {   0,   0,  171 }, {  71,   0,  159 },
	{ 143,   0,  119 }, { 171,   0,   19 }, { 167,   0,    0 }, { 127,  11,    0 },
	{  67,  47,    0 }, {   0,  71,    0 }, {   0,  81,    0 }, {   0,  63,   23 },
	{  27,  63,   95 }, {   0,   0,    0 }, {   0,   0,    0 }, {   0,   0,    0 },
	{ 188, 188,  188 }, {   0, 115,  239 }, {  35,  59,  239 }, { 131,   0,  243 },
	{ 191,   0,  191 }, { 231,   0,   91 }, { 219,  43,    0 }, { 203,  79,   15 },
	{ 139, 115,    0 }, {   0, 151,    0 }, {   0, 171,    0 }, {   0, 147,   59 },
	{   0, 131,  139 }, {   0,   0,    0 }, {   0,   0,    0 }, {   0,   0,    0 },
	{ 255, 255,  255 }, {  63, 191,  255 }, {  95, 151,  255 }, { 167, 139,  253 },
	{ 247, 123,  255 }, { 255, 119,  183 }, { 255, 119,   99 }, { 255, 155,   59 },
	{ 243, 191,   63 }, { 131, 211,   19 }, {  79, 223,   75 }, {  88, 248,  152 },
	{   0, 235,  219 }, {   0,   0,    0 }, {   0,   0,    0 }, {   0,   0,    0 },
	{ 255, 255,  255 }, { 171, 231,  255 }, { 199, 215,  255 }, { 215, 203,  255 },
	{ 255, 199,  255 }, { 255, 199,  219 }, { 255, 191,  179 }, { 255, 219,  171 },
	{ 255, 231,  163 }, { 227, 255,  163 }, { 171, 243,  191 }, { 179, 255,  207 },
	{ 159, 255,  243 }, {   0,   0,    0 }, {   0,   0,    0 }, {   0,   0,    0 },
};

// Pointers of currently selected BG colors
static const struct _nes_palette *nes_palette_current[32];


#ifdef LSB_FIRST
#define nes_pixel(surface_data,surface_offset,pal,alpha) { surface_data[surface_offset++] = (pal)->b; surface_data[surface_offset++] = (pal)->g; surface_data[surface_offset++] = (pal)->r; surface_data[surface_offset++] = (alpha); }
#else
#define nes_pixel(surface_data,surface_offset,pal,alpha) { surface_data[surface_offset++] = (alpha); surface_data[surface_offset++] = (pal)->r; surface_data[surface_offset++] = (pal)->g; surface_data[surface_offset++] = (pal)->b; }
#endif


static void ppu_sprite_calc_limits(struct NoDice_objects *this_obj, int *off_x, int *off_y, int *width, int *height)
{
	int i, min_x = INT_MAX, min_y = INT_MAX, max_x = INT_MIN, max_y = INT_MIN;

	for(i = 0; i < this_obj->total_sprites; i++)
	{
		struct NoDice_object_sprites *this_spr = &this_obj->sprites[i];

		// SMB3 uses 8x16 sprite segments
		int left = this_spr->x, top = this_spr->y;
		int right = this_spr->x + 8, bottom = this_spr->y + 16;

		if(left < min_x)	min_x = left;
		if(right > max_x)	max_x = right;
		if(top < min_y)	min_y = top;
		if(bottom > max_y)	max_y = bottom;
	}

	*off_x = min_x;
	*off_y = min_y;
	*width = max_x - min_x;
	*height = max_y - min_y;
}


static void ppu_init_objs(struct _PPU_SPR *ppu_spr_base, int is_map_objects)
{
	int i;

	for(i = 0; i < 256; i++)
	{
		struct NoDice_objects *this_obj = (!is_map_objects) ?
			&NoDice_config.game.regular_objects[i] :
			&NoDice_config.game.map_objects[i];

		struct _PPU_SPR *ppu_spr = &ppu_spr_base[i];

		if(this_obj->total_sprites > 0)
		{
			// Calculate the rectangle and offset for this sprite
			ppu_sprite_calc_limits(this_obj, &ppu_spr->offset_x, &ppu_spr->offset_y, &ppu_spr->width, &ppu_spr->height);

			// Create sprite
			ppu_spr->surface = gui_surface_create(ppu_spr->width, ppu_spr->height);
		}
	}
}


void ppu_init()
{
	int i;

	// Create linear space, enough for 256 tiles, 4 times
	// (different palettes, maybe there's a better way?)
	for(i = 0; i < 4; i++)
		PPU_BG_VROM[i] = gui_surface_create(8, 256*8);

	// Configure sprites/limits for objects and map objects
	ppu_init_objs(PPU_SPR_objects, 0);
	ppu_init_objs(PPU_SPR_mobjects, 1);
}


static void ppu_configure_for_level_sprites(struct NoDice_objects *this_obj)
{
	int i, id = this_obj - NoDice_config.game.objects;
	struct _PPU_SPR *spr = &PPU_SPR[id];

	if(spr->surface != NULL)
	{
		int surface_stride, surface_offset;
		int x, y;
		unsigned char *surface_data = gui_surface_capture_data(spr->surface, &surface_stride),
			*clear_ptr = surface_data;

		// First, need to clear the sprite surface completely
		for(y = 0; y < spr->height; y++)
		{
			for(x = 0; x < spr->width; x++)
				*clear_ptr = 0x00;
		}

		for(i = 0; i < this_obj->total_sprites; i++)
		{
			struct NoDice_object_sprites *this_spr = &this_obj->sprites[i];
			const unsigned char *VROM = NoDice_get_raw_CHR_bank(this_spr->bank) + ((int)this_spr->pattern * 64);

			int cbase = 16 + (this_spr->palette << 2);
			int hflip = (this_spr->flips & 1), vflip = (this_spr->flips & 2);

			surface_offset = ((this_spr->y - spr->offset_y) * surface_stride) + ((this_spr->x - spr->offset_x) * 4);

			// When v-flipped, jump to last row of sprite
			if(vflip)
				VROM += 8*15;

			for(y = 0; y < 16; y++)
			{
				// Horizontally flipped starts at end of row
				if(hflip)
					VROM += 8;

				for(x = 0; x < 8; x++)
				{
					unsigned char c = (!hflip) ? (*VROM++) : (*--VROM);

					if(c != 0)
					{
						// Get palette entry for this pixel
						const struct _nes_palette *pal = nes_palette_current[c + cbase];

						// Expand palette pixel into RGB
						nes_pixel(surface_data, surface_offset, pal, 255);
					}
					else
						surface_offset += 4;
				}

				// Next row
				if(hflip)
					VROM += 8;

				// If vertically flipped, move up one line
				if(vflip)
					VROM -= 16;

				// Hop the rest of the stride (if any)
				surface_offset += surface_stride - (8*4);
			}
		}

		// Release the surface
		gui_surface_release_data(spr->surface);
	}
}


// In MMC3, the BG bank setting actually uses two adjacent banks
// (that is, it swaps in 2KB / 128 tiles in "halves")
// This imitates that behavior accordingly...
//
//MMC3_2K_TO_PPU_0000	= %01000000	; 0
//MMC3_2K_TO_PPU_0800	= %01000001	; 1
//
// So if to0800 is non-zero, it adds to the "bottom half"
void ppu_set_BG_bank(unsigned char bank, unsigned char to0800)
{
	int row;
	const unsigned char *VROM_start = NoDice_get_raw_CHR_bank(bank), *VROM;
	int pal, cbase;

	// Generate all four palette copies (seems wasteful; better way?)
	for(pal = 0; pal < 4; pal++)
	{
		// Get surface data and stride
		gui_surface_t *surface = PPU_BG_VROM[pal];
		int surface_stride, surface_offset;
		unsigned char *surface_data = gui_surface_capture_data(surface, &surface_stride);

		// Set offset by value of to 08000
		surface_offset = to0800 ? (128 * 8 * surface_stride) : 0;

		VROM = VROM_start;
		cbase = pal << 2;

		// This actually goes through two banks, but works because it's linear
		// Except if, somehow, you specified the last bank...
		for(row = 0; row < 128*8; row++)
		{
			int x;

			// CHECKME: More efficient way??
			for(x = 0; x < 8; x++)
			{
				// Get palette entry for this pixel
				const struct _nes_palette *pal = nes_palette_current[(*VROM++) + cbase];

				// Expand palette pixel into RGB
				nes_pixel(surface_data, surface_offset, pal, 255);
			}

			// Hop the rest of the stride (if any)
			surface_offset += surface_stride - (8*4);
		}

		// Release the surface
		gui_surface_release_data(surface);
	}
}


void ppu_configure_for_level()
{
	int i;

	// Set BG/SPR palette
	for(i = 0; i < 16; i++)
	{
		nes_palette_current[i+0]  = &nes_palette[NoDice_the_level.bg_pal[i]];
		nes_palette_current[i+16] = &nes_palette[NoDice_the_level.spr_pal[i]];
	}

	// Set banks (loads VROM)
	ppu_set_BG_bank(NoDice_the_level.bg_page_1, 0);
	ppu_set_BG_bank(NoDice_the_level.bg_page_2, 1);

	// Set proper sprite set
	PPU_SPR = (NoDice_the_level.tileset->id > 0) ? PPU_SPR_objects : PPU_SPR_mobjects;
	NoDice_config.game.objects = (NoDice_the_level.tileset->id > 0) ? NoDice_config.game.regular_objects : NoDice_config.game.map_objects;

	// Set proper object set
	for(i = 0; i < 256; i++)
	{
		struct NoDice_objects *this_obj = &NoDice_config.game.objects[i];
		ppu_configure_for_level_sprites(this_obj);
	}
}



#define ppu_draw_pattern(x, y, pat, pal)	gui_surface_blit(PPU_BG_VROM[pal], 0, (pat) << 3, (x), (y), 8, 8)
void ppu_draw_tile(int x, int y, unsigned char tile, unsigned char pal)
{
	ppu_draw_pattern(x+0, y+0, NoDice_the_level.tile_layout[tile][0], pal);
	ppu_draw_pattern(x+0, y+8, NoDice_the_level.tile_layout[tile][1], pal);
	ppu_draw_pattern(x+8, y+0, NoDice_the_level.tile_layout[tile][2], pal);
	ppu_draw_pattern(x+8, y+8, NoDice_the_level.tile_layout[tile][3], pal);
}


void ppu_draw(int x, int y, int w, int h)
{
	int row, col;
	int row_end, col_end;
	int start_x, col_start;
	int max_row, max_col;

	unsigned char screen, col_ef;
	unsigned char pal, tile;
	unsigned short offset;

	// Shouldn't happen except at start
	if(NoDice_the_level.tiles == NULL)
		return;

	// Need to align x and y to tile grid!
	x &= ~(TILESIZE - 1);
	y &= ~(TILESIZE - 1);

	row = (y / TILESIZE) +
		// WORLD MAP HACK: Only the last 9 rows matter, so we offset to them
		((NoDice_the_level.tileset->id == 0) ? SCREEN_MAP_ROW_OFFSET : 0);

	col = x / TILESIZE;
	row_end = row + (h / TILESIZE) + 2;
	col_end = col + (w / TILESIZE) + 2;
	start_x = x;
	col_start = col;

	if(!NoDice_the_level.header.is_vert)
	{
		// Level / Worldmap
		max_row = (NoDice_the_level.tileset->id != 0) ? (SCREEN_BYTESIZE / TILESIZE) : (SCREEN_MAP_ROW_OFFSET + (SCREEN_BYTESIZE_M / TILESIZE));
		max_col = NoDice_the_level.header.total_screens * SCREEN_WIDTH;
	}
	else
	{
		max_row = (SCREEN_BYTESIZE_V / SCREEN_WIDTH) * NoDice_the_level.header.total_screens;
		max_col = SCREEN_WIDTH;
	}

	// If too low, just quit!
	if(row > max_row)
		return;

	// Cap off row_end
	if(row_end > max_row)
		row_end = max_row;

	// If too far, just quit!
	if(col > max_col)
		return;

	// Cap off end_col
	if(col_end > max_col)
		col_end = max_col;

	for( ; row < row_end; row++)
	{
		for( ; col < col_end; col++)
		{
			if(!NoDice_the_level.header.is_vert)
			{
				screen = col >> 4;
				col_ef = col & 0xF;
				offset = (screen * SCREEN_BYTESIZE) + (row * SCREEN_WIDTH) + col_ef;
			}
			else
			{
				screen = row / (SCREEN_BYTESIZE_V / SCREEN_WIDTH);	// Not required to figure offset
				col_ef = col & 0xF;
				offset = (row * SCREEN_WIDTH) + col_ef;
			}

			tile = NoDice_the_level.tiles[offset];
			pal = tile >> 6;	// Tile Quadrant defines palette select in SMB3

			ppu_draw_tile(x, y, tile, pal);

			// World map has no use for Tile Hints!
			if(gui_tilehints[tile].hint != NULL && NoDice_the_level.tileset->id != 0)
				gui_surface_overlay(gui_tilehints[tile].hint, x+0, y+0);

			x += 16;
		}

		y += 16;

		x = start_x;
		col = col_start;
	}

}


void ppu_sprite_get_offset(unsigned char id, int *offset_x, int *offset_y, int *width, int *height)
{
	const struct _PPU_SPR *spr = &PPU_SPR[id];

	if(spr->surface != NULL)
	{
		*offset_x = spr->offset_x;
		*offset_y = spr->offset_y;
		*width = spr->width;
		*height = spr->height;
	}
	else
	{
		*offset_x = 0;
		*offset_y = 0;
		*width = TILESIZE;
		*height = TILESIZE;
	}
}


int ppu_sprite_draw(unsigned char id, int x, int y)
{
	const struct _PPU_SPR *spr = &PPU_SPR[id];

	if(spr->surface != NULL)
	{
		gui_surface_blit(spr->surface, 0, 0, x, y, spr->width, spr->height);
		//gui_surface_blit(PPU_BG_VROM[0], 0, 0, x, y, 8, spr->height);
		return 1;
	}

	return 0;
}


void ppu_shutdown()
{
	int i;

	for(i = 0; i < 4; i++)
		gui_surface_destroy(PPU_BG_VROM[i]);

	for(i = 0; i < sizeof(PPU_SPR) / sizeof(struct _PPU_SPR); i++)
	{
		if(PPU_SPR[i].surface != NULL)
			gui_surface_destroy(PPU_SPR[i].surface);
		PPU_SPR[i].surface = NULL;
	}
}
