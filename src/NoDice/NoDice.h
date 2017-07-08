#ifndef _NODICE_H
#define _NODICE_H

// PPU stuff
#define PPU_PORTAL_X	272
#define PPU_PORTAL_Y	44

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

//extern BITMAP *PPU_portal;
void ppu_init();
void ppu_set_BG_bank(unsigned char bank, unsigned char to0800);
void ppu_configure_for_level();
void ppu_draw_tile(int x, int y, unsigned char tile, unsigned char pal);
void ppu_draw(int x, int y, int w, int h);
int ppu_sprite_draw(unsigned char id, int x, int y);
void ppu_sprite_get_offset(unsigned char id, int *offset_x, int *offset_y, int *width, int *height);
void ppu_shutdown();


// GUI controls

enum EDIT_NOTEBOOK_PAGES
{
	ENPAGE_GENS,	// Regular level generators
	ENPAGE_OBJS,	// Regular level objects
	ENPAGE_STARTS,	// Regular level start positions
	ENPAGE_TILES,	// World map tiles
	ENPAGE_MOBJS,	// World map objects
	ENPAGE_LINKS,	// World map links
	ENPAGE_TOTAL
};

// Publicizes info about the current drawing surface to the virtual PPU
extern struct _gui_draw_info
{
	double tilehint_alpha;
	double zoom;
	void *context;	// Holds pointer to context for Cairo; typeless so it can be exposed
} gui_draw_info;

// Not going to expose Cairo surface, but reserved in case we need a "real" type
typedef void gui_surface_t;

void gui_boot(int argc, char *argv[]);
int gui_init();
void gui_loop();
void gui_display_6502_error(enum RUN6502_STOP_REASON reason);
void gui_display_message(int is_error, const char *err_str);
int gui_ask_question(const char *prompt);
gui_surface_t *gui_surface_create(int width, int height);
gui_surface_t *gui_surface_from_file(const char *file);
unsigned char *gui_surface_capture_data(gui_surface_t *surface, int *out_stride);
void gui_surface_release_data(gui_surface_t *surface);
void gui_surface_blit(gui_surface_t *draw, int source_x, int source_y, int dest_x, int dest_y, int width, int height);
void gui_surface_overlay(gui_surface_t *draw, int dest_x, int dest_y);
void gui_surface_destroy(gui_surface_t *surface);
void gui_update_for_generators();
void gui_refesh_for_level();
void gui_6502_timeout_start();
void gui_6052_timeout_end();
void gui_overlay_select_index(int index);
const char *gui_make_image_path(const struct NoDice_tileset *tileset, const struct NoDice_the_levels *level);
void gui_set_subtitle(const char *subtitle);
void gui_set_modepage(enum EDIT_NOTEBOOK_PAGES page);
void gui_disable_empty_objs(int is_empty);
void gui_reboot();

extern struct _gui_tilehints
{
	gui_surface_t *hint;	// Tile hint graphic
	int is_global;			// TRUE means this is from the global <tilehints /> collection, so no need to free/reload
} gui_tilehints[256];

// Edit stuff
void edit_do();
void edit_transform_sel_gen(int diff_row, int diff_col);
void edit_6502_timeout();
void edit_level_load(unsigned char tileset, const struct NoDice_the_levels *level);
int edit_level_save(int save_layout, int save_objects);
void edit_level_save_new_check(const char *tileset_path, const char *layoutfile, int *save_layout, const char *objectfile, int *save_objects);
int edit_level_add_labels(const struct NoDice_the_levels *level, int add_layout, int add_objects);
const struct NoDice_the_levels *edit_level_find(unsigned char tileset_id, unsigned short layout_addr, unsigned short objects_addr);
void edit_gen_remove(struct NoDice_the_level_generator *remove);
void edit_gen_translate(struct NoDice_the_level_generator *gen, int diff_row, int diff_col);
void edit_revert();
void edit_gen_bring_forward(struct NoDice_the_level_generator *gen);
void edit_gen_send_backward(struct NoDice_the_level_generator *gen);
void edit_gen_send_to_back(struct NoDice_the_level_generator *gen);
void edit_gen_bring_to_front(struct NoDice_the_level_generator *gen);
void edit_gen_insert_generator(const struct NoDice_generator *gen, int row, int col, const unsigned char *p);
void edit_gen_set_parameters(struct NoDice_the_level_generator *gen, const unsigned char *parameters);
void edit_header_change(struct NoDice_the_level_generator *selected_gen, const unsigned char *old_header);
void edit_startspot_alt_load();
void edit_startspot_alt_revert();
void edit_obj_translate(struct NoDice_the_level_object *obj, int diff_row, int diff_col);
int edit_obj_insert_object(const struct NoDice_objects *obj, int row, int col);
void edit_obj_remove(struct NoDice_the_level_object *obj);
void edit_map_obj_clear(struct NoDice_the_level_object *obj);
void edit_maptile_set(int row, int col, unsigned char tile);
unsigned char edit_maptile_get(int row, int col);
void edit_link_translate(struct NoDice_map_link *link, int diff_row, int diff_col);
int edit_link_insert_link(int row, int col);
void edit_link_remove(struct NoDice_map_link *link);
void edit_link_adjust(struct NoDice_map_link *updated_link, struct NoDice_map_link *old_link);

#endif
