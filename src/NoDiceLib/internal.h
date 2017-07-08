#ifndef _INTERNAL_H
#define _INTERNAL_H

#define ERROR_MSG_LEN	8192
#define BUFFER_LEN		4096


// RAM identifiers
enum SMB3RAM
{
	TEMP_VAR15,
	TEMP_VAR16,
	VERT_SCROLL_HI,
	LEVEL_WIDTH,
	LEVEL_LAYPTR_ADDRL,
	LEVEL_LAYPTR_ADDRH,
	MAP_TILE_ADDRL,
	MAP_TILE_ADDRH,
	VERT_SCROLL,
	LEVEL_7VERTICAL,
	TILEADDR_OFF,
	LL_SHAPEDEF,
	LEVEL_TILESET,
	MMC3PAGE_C000,
	MMC3PAGE_A000,
	WORLD_NUM,
	PALSEL_TILE_COLORS,
	PALSEL_OBJ_COLORS,
	MAP_OBJECTS_ITM,
	LEVEL_ALTTILESET,
	LEVEL_BG_PAGE1_2,
	MAP_OBJECTS_Y,
	MAP_OBJECTS_XLO,
	MAP_OBJECTS_XHI,
	MAP_OBJECTS_ID,

	SMB3RAM_TOTAL
};
extern struct SMB3_RAM 
{
	const char *varname;		// Name of variable (from source, to be resolved by FNS)
	unsigned short address;		// Address of variable (to be resolved by FNS)
} _ram[SMB3RAM_TOTAL];


extern char _error_msg[ERROR_MSG_LEN];
extern char _buffer[BUFFER_LEN];

int _config_init();
void _config_shutdown();

int _rom_load();
void _rom_free_level_list();
void _rom_shutdown();
int _ram_resolve_labels();

#endif // _INTERNAL_H
