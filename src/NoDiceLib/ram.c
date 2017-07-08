#include <stdio.h>
#include "NoDiceLib.h"
#include "internal.h"

// These are all resolved at runtime to the addresses as provided by the FNS
struct SMB3_RAM _ram[SMB3RAM_TOTAL] =
{
	{ "Temp_Var15", 0 },	//  General temporary, but used for capturing generators
	{ "Temp_Var16", 0 },	//  General temporary, but used for capturing generators
	{ "Vert_Scroll_Hi", 0 },	//  Provides a "High" byte for vertically scrolling (only used during vertical levels!)
	{ "Level_Width", 0 },	//  Width of current level, in screens
	{ "Level_LayPtr_AddrL", 0 },	//  Pointer to layout data to be loaded (low)
	{ "Level_LayPtr_AddrH", 0 },	//  Pointer to layout data to be loaded (high)
	{ "Map_Tile_AddrL", 0 },	//  Low byte of tile address
	{ "Map_Tile_AddrH", 0 },	//  High byte of tile address
	{ "Vert_Scroll", 0 },	//  Vertical scroll of name table
	{ "Level_7Vertical", 0 },	//  Set in World 7 vertical type levels
	{ "TileAddr_Off", 0 },	//  During level loading, specifies an offset into the current Mem_Tile_Addr setting
	{ "LL_ShapeDef", 0 },	//  Used for capturing the generators
	{ "Level_Tileset", 0 },	//  Set current tileset / level style
	{ "PAGE_C000", 0 },	//  Page to set PRG ROM C000 (PRGROM_Change_Both)
	{ "PAGE_A000", 0 },	//  Page to set PRG ROM A000 (PRGROM_Change_Both)
	{ "World_Num", 0 },	//  Current world index (0-8, where 0 = World 1, 7 = World 8, 8 = World 9 / Warp Zone)
	{ "PalSel_Tile_Colors", 0 },	//  Stores value to index which tile color set to use when palette loading routine is called
	{ "PalSel_Obj_Colors", 0 },	//  Stores value to index which object color set to use when palette loading routine is called
	{ "Map_Objects_Itm", 0 },	//
	{ "Level_AltTileset", 0 },	//  Pointer to level's "alternate" tileset (when you go into bonus pipe, etc.)
	{ "Level_BG_Page1_2", 0 },	//  Sets which bank the first and second page (2K / 64 8x8 tiles) of BG is using
	{ "Map_Objects_Y", 0 },	//
	{ "Map_Objects_XLo", 0 },	//
	{ "Map_Objects_XHi", 0 },	//
	{ "Map_Objects_IDs", 0 },	//
};


int _ram_resolve_labels()
{
	int i;
	for(i = 0; i < SMB3RAM_TOTAL; i++)
	{
		unsigned short addr = NoDice_get_addr_for_label(_ram[i].varname);
		if(addr == 0xFFFF)
		{
			// Failed to resolve RAM label...
			snprintf(_error_msg, ERROR_MSG_LEN, "Failed to resolve required RAM label %s", _ram[i].varname);

			return 0;
		}

		// Resolved!
		_ram[i].address = addr;
	}

	return 1;
}
