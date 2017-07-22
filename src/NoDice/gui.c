#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include "NoDiceLib.h"
#include "NoDice.h"
#include "guictls.h"


static struct _gui_parameter_widgets
{
	GtkWidget *label;
	GtkWidget *spin;
} gui_parameter_widgets[GEN_MAX_PARAMS];


const char *edit_notebook_page_names[ENPAGE_TOTAL] = { "Gens", "Objs", "Starts", "Tiles", "Objs", "Links" };

const char *gui_start_widgets_button_by_state[] = { "Place Start", "Apply" };
const char *gui_start_widgets_desc_by_state[] = { "Select screen section that Player will junction from (highlighted in green) and click 'Place Start'", "Use right mouse button to horizontally place the start position and controls above to configure it.\n\nVertical start on 'vertical' levels will either start on absolute top or absolute bottom." };
static struct _gui_start_widgets
{
	enum EDIT_NOTEBOOK_PAGES edit_notebook_page;
	unsigned char state;
	unsigned char screen;		// Currently selected screen
	unsigned char screen_edit;	// Screen last edited (persists after level swapping)
	unsigned char jct_data;		// Contains the "Level_JctYLHStart" component during editing
	const struct NoDice_tileset *revert_tileset;
	unsigned short jct_x, jct_y;	// Coordinates for display of jucntion (jct_x is real, jct_y is calculated)
	GtkWidget *screen_list;
	GtkWidget *button_active;
	GtkWidget *button_cancel;
	GtkWidget *property_vbox;
	GtkWidget *textbox_desc;
} gui_start_widgets = { ENPAGE_GENS, 0 };


struct _gui_tilehints gui_tilehints[256] = { { 0 } };	// Tile hints

static struct _map_tile_buttons
{
	int col_size;	// Remember last column size to see if we need to reconfigure
	GtkWidget *buttons[255];	// Buttons for map tiles
} map_tile_buttons;

// Publicizes info about the current drawing surface to the virtual PPU
struct _gui_draw_info gui_draw_info = { 0.75, 2.0 };
GtkWidget *gui_main_window = NULL;
GtkWidget *gui_status_bar = NULL;
static GtkWidget *gui_menu;
static GtkWidget *gui_generator_listbox = NULL;
static GtkWidget *gui_objects_listbox = NULL;
static GtkWidget *gui_objects_special_listbox = NULL;
static GtkWidget *gui_map_objects_listbox = NULL;
static GtkWidget *gui_fixed_view = NULL;
static GtkWidget *gui_notebook = NULL;
static GtkWidget *gui_right_pane = NULL;
static GList *gui_start_pos_props_context = NULL;
static int gui_bank_free_space = 0, gui_level_base_size = 0, gui_level_new_size = 0;

static struct NoDice_the_level_generator *gui_selected_gen = NULL;	// Selected generator in level
static struct NoDice_generator *gui_selected_insert_gen = NULL;		// Selected generator for insertion
static struct NoDice_the_level_object *gui_selected_obj = NULL;		// Selected object in level
static struct NoDice_map_link *gui_selected_link = NULL;			// Selected map link on world map
static struct NoDice_objects *gui_selected_insert_obj = NULL;		// Selected object for insertion
static unsigned char gui_selected_map_tile = 0;					// Selected map tile

static char path_buffer[PATH_MAX];

#ifndef _WIN32

#if GLIB_MAJOR_VERSION <= 2 && GLIB_MINOR_VERSION < 32

static GCond *run_6502_timeout_thread_cond;
static GThread *run_6502_timeout_thread_thread;
static volatile gboolean run_6502_timeout_thread_cond_ack = FALSE;

// Used to run a timeout in case the 6502 freezes
static gpointer run_6502_timeout_thread(gpointer optr)
{
	GTimeVal time;

	// Required for g_cond_timed_wait, not used
	GMutex *unused_mutex = g_mutex_new();

	// Sleep until we hit timeout or the condition is signalled
	g_get_current_time(&time);
	g_time_val_add(&time, 1000 * NoDice_config.core6502_timeout);

	// g_cond_timed_wait returns FALSE if it timed out
	// If condition is signalled however (i.e. 6502 core
	// completed in a timely manner), then we don't do anything.
	if(g_cond_timed_wait(run_6502_timeout_thread_cond, unused_mutex, &time) == FALSE)
		// Timeout occurred; assume 6502 is frozen!
		NoDice_Run6502_Stop = RUN6502_TIMEOUT;
	else
		// Acknowledge condition (if that's how we exited)
		run_6502_timeout_thread_cond_ack = TRUE;

	// Free unused mutex
	g_mutex_free(unused_mutex);

	return NULL;
}


void gui_6502_timeout_start()
{
	// Install a timer to check for lockups of the 6502 core
	run_6502_timeout_thread_cond_ack = FALSE;
	run_6502_timeout_thread_cond = g_cond_new();
	run_6502_timeout_thread_thread = g_thread_create(run_6502_timeout_thread, NULL, TRUE, NULL);
}


void gui_6052_timeout_end()
{
	// If we didn't time out, send the signal
	if(NoDice_Run6502_Stop != RUN6502_TIMEOUT)
	{
		// Repeatedly hit the condition in case it gets missed; this can
		// happen if the 6502 core runs faster than the setup for g_cond_timed_wait
		while(!run_6502_timeout_thread_cond_ack)
		{
			g_cond_signal(run_6502_timeout_thread_cond);
			g_thread_yield();
		}
	}

	// Cleanup timeout thread
	g_thread_join(run_6502_timeout_thread_thread);
	g_cond_free(run_6502_timeout_thread_cond);
}

#else		// End old GLIB < 2.32

static GCond run_6502_timeout_thread_cond;
static GThread *run_6502_timeout_thread_thread;
static volatile gboolean run_6502_timeout_thread_cond_ack = FALSE;

// Used to run a timeout in case the 6502 freezes
static gpointer run_6502_timeout_thread(gpointer optr)
{
	gint64 time;

	// Required for g_cond_timed_wait, not used
	GMutex unused_mutex;
	g_mutex_init(&unused_mutex);
	
	g_mutex_lock (&unused_mutex);

	// Sleep until we hit timeout or the condition is signalled
	time = g_get_monotonic_time() + NoDice_config.core6502_timeout * G_TIME_SPAN_SECOND;

	// g_cond_timed_wait returns FALSE if it timed out
	// If condition is signalled however (i.e. 6502 core
	// completed in a timely manner), then we don't do anything.
	if(g_cond_wait_until(&run_6502_timeout_thread_cond, &unused_mutex, time) == FALSE)
		// Timeout occurred; assume 6502 is frozen!
		NoDice_Run6502_Stop = RUN6502_TIMEOUT;
	else
		// Acknowledge condition (if that's how we exited)
		run_6502_timeout_thread_cond_ack = TRUE;

	g_mutex_unlock (&unused_mutex);
		
	return NULL;
}


void gui_6502_timeout_start()
{
	// Install a timer to check for lockups of the 6502 core
	run_6502_timeout_thread_cond_ack = FALSE;
	g_cond_init(&run_6502_timeout_thread_cond);
	run_6502_timeout_thread_thread = g_thread_new("6502_timeout_thread", run_6502_timeout_thread, NULL);
}


void gui_6052_timeout_end()
{
	// If we didn't time out, send the signal
	if(NoDice_Run6502_Stop != RUN6502_TIMEOUT)
	{
		// Repeatedly hit the condition in case it gets missed; this can
		// happen if the 6502 core runs faster than the setup for g_cond_timed_wait
		while(!run_6502_timeout_thread_cond_ack)
		{
			g_cond_signal(&run_6502_timeout_thread_cond);
			g_thread_yield();
		}
	}

	// Cleanup timeout thread
	g_thread_join(run_6502_timeout_thread_thread);
}

#endif		// End newer GLIB

#else	// End Linux

	// For some reason I haven't determined yet, the above precise
	// timeout loop doesn't work under Windows (it just deadlocks)
	// so here's another, less precise approach.  This will work
	// as long as the user does not save twice faster than the
	// timeout value; otherwise there will be overlapping threads
	// (since there's no way to force the thread to terminate early.)
static gpointer run_6502_timeout_thread(gpointer optr)
{
	// Windows version: Just sleep for time specified.  If we come
	// back and the CPU is still running, call it a timeout.
	// Otherwise, do nothing...
	g_usleep(NoDice_config.core6502_timeout);

	if(NoDice_Run6502_Stop == RUN6502_STOP_END)
		NoDice_Run6502_Stop = RUN6502_TIMEOUT;
}

void gui_6502_timeout_start()
{
	// Start the sleeper thread; if it wakes up before the CPU
	// stops, then we call it a timeout!
	g_thread_create(run_6502_timeout_thread, NULL, TRUE, NULL);
}

void gui_6052_timeout_end()
{
	// Nothing
}

#endif

static GtkWidget *menu_find_item(GtkWidget *menu, const char *path)
{
	GList *list;

	// Skip beginning slash
	if(*path == '/')
		path++;

	// See if we match on this layer...
	list = GTK_MENU_SHELL(menu)->children;
	while(list != NULL)
	{
		GtkMenuItem *item = GTK_MENU_ITEM(list->data);
		const char *item_name;
		int len;
		const char *c = strchr(path, '/');

		if(GTK_IS_IMAGE_MENU_ITEM(item))
			item_name = gtk_label_get_label(GTK_LABEL(gtk_bin_get_child(GTK_BIN(item))));
		else
			item_name = gtk_menu_item_get_label(item);

		if(item_name != NULL)
		{
			if(c == NULL)
				// Length of last path component
				len = strlen(path);
			else
				// Length of current path component
				len = c - path;

			//printf("%s vs %s\n", path, item_name);

			if(!strncmp(path, item_name, len))
			{
				// If we haven't hit the last path, recurse
				if(c != NULL)
				{
					GtkWidget *sec = gtk_menu_item_get_submenu(GTK_MENU_ITEM(list->data));
					if(sec != NULL)
						return menu_find_item(GTK_WIDGET(sec), c+1);
				}
				else
					// Winner
					return GTK_WIDGET(item);
			}
		}

		list = list->next;
	}

	return NULL;
}


static void menu_edit_undo(GtkWidget *w, gpointer data)
{
	edit_revert();

	// Requires a redraw for map tiles
	if(gui_start_widgets.edit_notebook_page == ENPAGE_TILES)
		gtk_widget_queue_draw(gui_fixed_view);
}

static void menu_edit_delete(GtkWidget *w, gpointer data)
{
	if(gui_selected_gen != NULL)
	{
		edit_gen_remove(gui_selected_gen);
		gui_selected_gen = NULL;
	}
	else if(gui_selected_obj != NULL)
	{
		if(NoDice_the_level.tileset->id == 0)
		{
			// World map actually just clears the object, not deletes it
			int index = (int)(gui_selected_obj - NoDice_the_level.objects);

			// We don't really "delete" a map object so much as reset it to an off-screen empty
			edit_map_obj_clear(&NoDice_the_level.objects[index]);
		}
		else
		{
			edit_obj_remove(gui_selected_obj);
			gui_selected_obj = NULL;
		}
	}
	else if(gui_selected_link != NULL)
	{
		edit_link_remove(gui_selected_link);
		gui_selected_link = NULL;
	}
}


static void menu_edit_arrange(GtkWidget *w, gpointer data)
{
	if(gui_selected_gen != NULL)
	{
		long arrange_op = (long)data;

		if(arrange_op == 0)			// Bring to Front
		{
			edit_gen_bring_to_front(gui_selected_gen);
		}
		else if(arrange_op == 1)		// Bring Forward
		{
			edit_gen_bring_forward(gui_selected_gen);
		}
		else if(arrange_op == 2)		// Send Backward
		{
			edit_gen_send_backward(gui_selected_gen);
		}
		else if(arrange_op == 3)		// Send to Back
		{
			edit_gen_send_to_back(gui_selected_gen);
		}
	}
}


static void gui_fixed_calc_size()
{
	int w, h;

	if(NoDice_the_level.tiles == NULL)
	{
		// No level loaded, no size!
		w = 0;
		h = 0;
	}
	else if(NoDice_the_level.tileset->id == 0)
	{
		// World map
		w = (int)(
			// Number of screens times number of columns times pixel size of tiles
			(double)(NoDice_the_level.header.total_screens * SCREEN_WIDTH * TILESIZE) * gui_draw_info.zoom);
		h = (int)(
			// Number of rows times pixel size of tiles
			(double)((SCREEN_BYTESIZE_M / SCREEN_WIDTH) * TILESIZE) * gui_draw_info.zoom);
	}
	else if(!NoDice_the_level.header.is_vert)
	{
		// Non-vertical level
		w = (int)(
			// Number of screens times number of columns times pixel size of tiles
			(double)(NoDice_the_level.header.total_screens * SCREEN_WIDTH * TILESIZE) * gui_draw_info.zoom);
		h = (int)(
			// Number of rows times pixel size of tiles
			(double)(SCREEN_BYTESIZE / SCREEN_WIDTH * TILESIZE) * gui_draw_info.zoom);
	}
	else
	{
		// Vertical level
		w = (int)(
			// Number of columns times pixel size of tiles
			(double)(SCREEN_WIDTH * TILESIZE) * gui_draw_info.zoom);
		h = (int)(
			// Number of rows times pixel size of tiles times screens
			(double)((NoDice_the_level.header.total_screens + 1) * TILESIZE * SCREEN_BYTESIZE_V / SCREEN_WIDTH) * gui_draw_info.zoom);
	}

	gtk_widget_set_size_request(gui_fixed_view, w, h);
}


static void menu_edit_properties(GtkWidget *w, gpointer data)
{
	gui_level_properties(gui_selected_gen);
	gui_fixed_calc_size();
}


static void gui_gen_overlay_calc_allocation(const struct NoDice_the_level_generator *gen, GtkAllocation *allocation)
{
	allocation->x = (int)((double)gen->xs * gui_draw_info.zoom);
	allocation->y = (int)((double)gen->ys * gui_draw_info.zoom);
	allocation->width = (int)((double)(gen->xe - gen->xs + 1) * gui_draw_info.zoom);
	allocation->height = (int)((double)(gen->ye - gen->ys + 1) * gui_draw_info.zoom);
}


static void gui_obj_overlay_calc_allocation(const struct NoDice_the_level_object *object, GtkAllocation *allocation)
{
	const struct NoDice_objects *this_obj = &NoDice_config.game.objects[object->id];
	int x = (object->col * TILESIZE);
	int y = (object->row * TILESIZE);
	int off_x, off_y, w, h;

	if(this_obj->special_options.options_list_count == 0)
		// Non-special objects get size based on their sprite
		ppu_sprite_get_offset(object->id, &off_x, &off_y, &w, &h);
	else
	{
		// Special objects are as tall as the view area
		y = 0;
		off_x = 0;
		off_y = 0;
		w = TILESIZE;

		if(!NoDice_the_level.header.is_vert)
			h = (SCREEN_BYTESIZE / SCREEN_WIDTH) * TILESIZE;
		else
			h = SCREEN_VHEIGHT * SCREEN_VCOUNT * TILESIZE;
	}

	x += off_x;
	y += off_y;

	allocation->x = (int)((double)x * gui_draw_info.zoom);
	allocation->y = (int)((double)y * gui_draw_info.zoom);
	allocation->width = (int)((double)w * gui_draw_info.zoom);
	allocation->height = (int)((double)h * gui_draw_info.zoom);
}


static void gui_link_overlay_calc_allocation(const struct NoDice_map_link *link, GtkAllocation *allocation)
{
	int x = (int)link->col_hi * TILESIZE;
	int y = (((link->row_tileset & 0xF0) >> 4) - MAP_OBJECT_BASE_ROW) * TILESIZE;

	allocation->x = (int)((double)x * gui_draw_info.zoom);
	allocation->y = (int)((double)y * gui_draw_info.zoom);
	allocation->width = (int)((double)TILESIZE * gui_draw_info.zoom);
	allocation->height = (int)((double)TILESIZE * gui_draw_info.zoom);
}



static void gui_menu_view_zoom_foreach_callback(GtkWidget *widget, gpointer unused)
{
	GtkAllocation overlay_alloc;
	GtkSelectableOverlay *selectable_overlay = GTK_SELECTABLE_OVERLAY(widget);

	// Calculate new allocation
	if(selectable_overlay->gen != NULL)
		gui_gen_overlay_calc_allocation(selectable_overlay->gen, &overlay_alloc);
	else if(selectable_overlay->obj != NULL)
		gui_obj_overlay_calc_allocation(selectable_overlay->obj, &overlay_alloc);

	// Move the widget to the correct adjusted position
	gtk_fixed_move(GTK_FIXED(widget->parent), widget, overlay_alloc.x, overlay_alloc.y);
	gtk_widget_set_size_request(widget, overlay_alloc.width, overlay_alloc.height);

	if(selectable_overlay->selected)
	{
		selectable_overlay->translation_data.origin_x = overlay_alloc.x;
		selectable_overlay->translation_data.origin_y = overlay_alloc.y;
	}
}


static int gui_level_calc_size()
{
	int total = 0;
	const struct NoDice_the_level_generator *gen = NoDice_the_level.generators;

	while(gen != NULL)
	{
		total += gen->size;
		gen = gen->next;
	}

	return total;
}


static void gui_statusbar_update()
{
	int actual_size;

	if(NoDice_the_level.tiles == NULL || gui_start_widgets.state > 0)
		return;

	gui_level_new_size = gui_level_calc_size();
	actual_size = gui_bank_free_space - (gui_level_new_size - gui_level_base_size);

	if(NoDice_the_level.tileset->id > 0)
		snprintf(path_buffer, PATH_MAX, "Objects: %i / %i   Bank free space: %i bytes", NoDice_the_level.object_count, OBJS_MAX, actual_size);
	else
		snprintf(path_buffer, PATH_MAX, "Objects: %i / %i   Map links: %i / %i", NoDice_the_level.object_count, OBJS_MAX, NoDice_the_level.map_link_count, MAX_MAP_LINKS);

	gtk_label_set_text(GTK_LABEL(GTK_STATUSBAR(gui_status_bar)->label), path_buffer);

	// If bank space dips below zero, you're out of memory!
	if((actual_size < 0) && (gui_level_base_size != 0))
	{
		gui_display_message(TRUE, "Reverting edit -- there is not enough free space in the bank to add this generator");
		edit_revert();
	}
}


static void menu_view_zoom(GtkWidget *widget, gpointer callback_data)
{
	// Set new zoom
	gui_draw_info.zoom = (double)((long)callback_data) / 100.0;

	// Need to redo coordinates and width/height of the overlay widgets
	gtk_container_foreach(GTK_CONTAINER(gui_fixed_view), gui_menu_view_zoom_foreach_callback, NULL);

	// Set proper size of the fixed view
	gui_fixed_calc_size();
}


static void menu_view_hint_select( gpointer   callback_data,
                            guint      callback_action,
                            GtkWidget *menu_item )
{
	if(GTK_CHECK_MENU_ITEM(menu_item)->active)
	{
		long radio_btn = (long)callback_action;
		double tile_hint_trans[3] = { 0.0, 0.75, 1.0 };
		gui_draw_info.tilehint_alpha = tile_hint_trans[radio_btn];

		gtk_widget_queue_draw(GTK_WIDGET(gui_fixed_view));
	}
}


static void gui_do_load_complete_actions();
static void menu_file_open(GtkWidget *widget, gpointer callback_data)
{
	unsigned char tileset;
	const struct NoDice_the_levels *level;

	gui_level_base_size = 0;

	if(gui_open_level_popup(0, &tileset, &level))
	{
		// Load new level and prepare UI
		edit_level_load(tileset, level);

		// Level loaded; fix up UI...
		gui_do_load_complete_actions();
	}
}

static void menu_file_save(GtkWidget *widget, gpointer callback_data)
{
	// If the Alt level gets moved after saving (i.e. it is in the same bank
	// at a later address than this level) then the layout/object addresses
	// will be invalid and need to be re-determined.  So we need to determine
	// ahead of time what level is currently out alternate (if we can) so we
	// can fix it on the other end...
	struct NoDice_level_header *hdr = &NoDice_the_level.header;
	const struct NoDice_the_levels *current_alt = edit_level_find(hdr->alt_level_tileset, hdr->alt_level_layout, hdr->alt_level_objects);

	if(edit_level_save(TRUE, TRUE))
	{
		// The following creates and saves the thumbnail!
		unsigned short vert_max = (!NoDice_the_level.header.is_vert) ?
			(SCREEN_BYTESIZE / SCREEN_WIDTH * TILESIZE - 256) :
			(SCREEN_BYTESIZE_V * SCREEN_VCOUNT / SCREEN_WIDTH * TILESIZE - 256);
		unsigned short vert_pos = NoDice_the_level.header.vert_scroll;
		cairo_surface_t *cs = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 128, 128);
		cairo_t *cr = cairo_create(cs);

		gui_draw_info.context = (void *)cr;

		//cairo_pattern_set_filter(cairo_get_source (cr), CAIRO_FILTER_NEAREST);

		if(vert_pos > vert_max)
			vert_pos = vert_max;

		cairo_translate(cr, 0, -vert_pos/2);

		// Set zoom factor
		cairo_scale(cr, 0.5, 0.5);

		ppu_draw(0, vert_pos, 256, 256);

		cairo_surface_write_to_png(cs, gui_make_image_path(NoDice_the_level.tileset, NoDice_the_level.level));

		// Done!
		cairo_destroy(cr);
		cairo_surface_destroy(cs);

		//////////////

		// To finish up what we started, we must now re-evalulate the alternate
		// level so we can fix the reference to it...
		if(current_alt != NULL)
		{
			// Re-resolve the layout and object addresses!
			hdr->alt_level_layout = NoDice_get_addr_for_label(current_alt->layoutlabel);
			hdr->alt_level_objects = NoDice_get_addr_for_label(current_alt->objectlabel);
		}
	}
}


static void menu_file_tiletest(GtkWidget *widget, gpointer callback_data)
{
	NoDice_tile_test();
	gtk_widget_queue_draw(GTK_WIDGET(gui_fixed_view));
}


static void menu_file_new(GtkWidget *widget, gpointer callback_data)
{
	if(gui_new_level_popup())
	{
		// Level loaded; fix up UI...
		gui_do_load_complete_actions();
	}
}


/* Our menu, an array of GtkItemFactoryEntry structures that defines each menu item */
static GtkItemFactoryEntry menu_items[] = {
  { "/_File",         NULL,         NULL,           0, "<Branch>" },
  { "/File/_New",     "<CTRL>N", menu_file_new,    0, "<StockItem>", GTK_STOCK_NEW },
  { "/File/_Open Level from ROM...",    "<CTRL>O", menu_file_open,    0, "<StockItem>", GTK_STOCK_OPEN },
  { "/File/_Save Level to ROM",    "<CTRL>S", menu_file_save,    0, "<StockItem>", GTK_STOCK_SAVE },
  { "/File/sep1",     NULL,         NULL,           0, "<Separator>" },
  { "/File/Test Tiles", NULL,	menu_file_tiletest,	0, "<Item>" },
  { "/File/sep1",     NULL,         NULL,           0, "<Separator>" },
  { "/File/_Quit",    "<CTRL>Q", gtk_main_quit, 0, "<StockItem>", GTK_STOCK_QUIT },
  { "/_Edit",         NULL,         NULL,           0, "<Branch>" },
  { "/Edit/_Undo",    "<CTRL>Z",    menu_edit_undo, 0, "<StockItem>", GTK_STOCK_UNDO },
  { "/Edit/_Delete",  "Delete",     menu_edit_delete,0, "<StockItem>", GTK_STOCK_DELETE },
  { "/Edit/Arrange/_Bring to Front",  "<CTRL>Prior", menu_edit_arrange,0, "<StockItem>", GTK_STOCK_GOTO_LAST },
  { "/Edit/Arrange/Bring _Forward",  "Prior",        menu_edit_arrange,1, "<StockItem>", GTK_STOCK_GO_FORWARD },
  { "/Edit/Arrange/Send Back_ward",  "Next",         menu_edit_arrange,2, "<StockItem>", GTK_STOCK_GO_BACK },
  { "/Edit/Arrange/_Send to Back",  "<CTRL>Next",    menu_edit_arrange,3, "<StockItem>", GTK_STOCK_GOTO_FIRST },
  { "/Edit/Level _Properties",  NULL,     menu_edit_properties,0, "<StockItem>", GTK_STOCK_PROPERTIES },
  { "/_View",         NULL,         NULL,           0, "<Branch>" },
  { "/View/Zoom/50%",  NULL,         menu_view_zoom,   50, "<Item>" },
  { "/View/Zoom/100%", NULL,         menu_view_zoom,  100, "<Item>" },
  { "/View/Zoom/200%", NULL,         menu_view_zoom,  200, "<Item>" },
  { "/View/Zoom/400%", NULL,         menu_view_zoom,  400, "<Item>" },
  { "/View/Tile Hints/None",  NULL,		menu_view_hint_select, 0, "<RadioItem>" },
  { "/View/Tile Hints/Translucent",  NULL,	menu_view_hint_select, 1, "/View/Tile Hints/None" },
  { "/View/Tile Hints/Opaque",  NULL,		menu_view_hint_select, 2, "/View/Tile Hints/None" },
};

static gint nmenu_items = sizeof (menu_items) / sizeof (menu_items[0]);

/* Returns a menubar widget made from the above menu */
static GtkWidget *get_menubar_menu( GtkWidget  *window )
{
	GtkItemFactory *item_factory;
	GtkAccelGroup *accel_group;

	/* Make an accelerator group (shortcut keys) */
	accel_group = gtk_accel_group_new ();

	/* Make an ItemFactory (that makes a menubar) */
	item_factory = gtk_item_factory_new (GTK_TYPE_MENU_BAR, "<main>",
							    accel_group);

	/* This function generates the menu items. Pass the item factory,
	the number of items in the array, the array itself, and any
	callback data for the the menu items. */
	gtk_item_factory_create_items (item_factory, nmenu_items, menu_items, NULL);

	/* Attach the new accelerator group to the window. */
	gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);

	/* Finally, return the actual menu bar created by the item factory. */
	return gtk_item_factory_get_widget (item_factory, "<main>");
}


/* Same again but return an option menu */
GtkWidget *get_option_menu( void )
{
   GtkItemFactory *item_factory;
   GtkWidget *option_menu;

   /* Same again, not bothering with the accelerators */
   item_factory = gtk_item_factory_new (GTK_TYPE_OPTION_MENU, "<main>",
                                        NULL);
   gtk_item_factory_create_items (item_factory, nmenu_items, menu_items, NULL);
   option_menu = gtk_item_factory_get_widget (item_factory, "<main>");

   return option_menu;
}


/////////////////////////////////////////////////////////////////////////////////////

gui_surface_t *gui_surface_create(int width, int height)
{
	return (gui_surface_t *)cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
}

gui_surface_t *gui_surface_from_file(const char *file)
{
	return (gui_surface_t *)cairo_image_surface_create_from_png(file);
}

unsigned char *gui_surface_capture_data(gui_surface_t *surface, int *out_stride)
{
	unsigned char *data;

	cairo_surface_t *cs = (cairo_surface_t *)surface;

	// flush to ensure all writing to the image was done
	cairo_surface_flush (cs);

	// Get the data and stride
	data = cairo_image_surface_get_data(cs);
	*out_stride = cairo_image_surface_get_stride(cs);

	return data;
}


void gui_surface_release_data(gui_surface_t *surface)
{
	// mark the image dirty so Cairo clears its caches.
	cairo_surface_mark_dirty (surface);
}


void gui_surface_destroy(gui_surface_t *surface)
{
	cairo_surface_destroy(surface);
}


static void cairo_blit(cairo_t *cr, cairo_surface_t *image, int source_x, int source_y, int dest_x, int dest_y, int width, int height, double alpha)
{
	cairo_set_source_surface(cr, image, dest_x - source_x, dest_y - source_y);
	cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_NEAREST);
	cairo_rectangle(cr, dest_x, dest_y, width, height);
	cairo_set_line_width(cr, 1);

	if(alpha == 1.0)
	{
		cairo_fill(cr);
	}
	else
	{
		cairo_paint_with_alpha(cr, alpha);
	}
}


void gui_surface_blit(gui_surface_t *draw, int source_x, int source_y, int dest_x, int dest_y, int width, int height)
{
	cairo_t *context = (cairo_t *)gui_draw_info.context;
	cairo_blit(context, draw, source_x, source_y, dest_x, dest_y, width, height, 1.0);
}


void gui_surface_overlay(gui_surface_t *draw, int dest_x, int dest_y)
{
	cairo_t *context = (cairo_t *)gui_draw_info.context;
	cairo_surface_t *surface = (cairo_surface_t *)draw;
	cairo_blit(context, draw, 0, 0, dest_x, dest_y, cairo_image_surface_get_width(surface), cairo_image_surface_get_height(surface), gui_draw_info.tilehint_alpha);
}

// NOTE: Exposed to guictls.c as a way to get around the fact that GTK Widgets
// simply cannot have a transparent background and compositing doesn't work
// on Win32 due to lack of an RGBA colormap etc...
gboolean gui_PPU_portal_expose_event_callback (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
	GtkAllocation offset = { 0, 0 };
	double zoom = gui_draw_info.zoom;
	cairo_t *cr;

	cr = gdk_cairo_create(widget->window);

	//cairo_rectangle(cr, event->area.x, event->area.y, event->area.width, event->area.height);
	//cairo_clip(cr);

	if(data != NULL)
	{
		GtkAllocation *alloc = (GtkAllocation *)data;
		offset.x = alloc->x;
		offset.y = alloc->y;

		cairo_translate(cr, -offset.x, -offset.y);
	}

	// Publicize info about drawing surface
	gui_draw_info.context = (void *)cr;

	// Set zoom factor
	cairo_scale(cr, zoom, zoom);

	// PPU to draw tiles in the update region
	{
		// Calculate virtual coordinates to match the scaling
		int rx = (int)((double)(event->area.x + offset.x) / zoom);
		int ry = (int)((double)(event->area.y + offset.y) / zoom);
		int rw = (int)((double)event->area.width / zoom);
		int rh = (int)((double)event->area.height / zoom);

		ppu_draw(rx, ry, rw, rh);
	}

	// If we're in start spot mode, draw over the screen space
	if((gui_start_widgets.edit_notebook_page == ENPAGE_STARTS))
	{
		int rx, ry, rw, rh;

		if(!NoDice_the_level.header.is_vert)
		{
			ry = 0;
			rh = (SCREEN_BYTESIZE / SCREEN_WIDTH * TILESIZE);

			if(gui_start_widgets.state == 0)
			{
				rx = (SCREEN_WIDTH * TILESIZE * gui_start_widgets.screen);
				rw = (SCREEN_WIDTH * TILESIZE);
			}
			else
			{
				rx = gui_start_widgets.jct_x;
				rw = TILESIZE;
			}
		}
		else
		{
			if(gui_start_widgets.state == 0)
			{
				rx = 0;
				ry = (SCREEN_VHEIGHT * TILESIZE * gui_start_widgets.screen);
				rw = (SCREEN_WIDTH * TILESIZE);
				rh = (SCREEN_VHEIGHT * TILESIZE);
			}
			else
			{
				rx = gui_start_widgets.jct_x & 0xF0;
				ry = 0;
				rw = TILESIZE;
				rh = (SCREEN_VHEIGHT * TILESIZE) * SCREEN_VCOUNT;
			}
		}

		cairo_set_source_rgba(cr, 0.0, 1.0, 0.0, 0.5);
		cairo_rectangle(cr, rx, ry, rw, rh);
		cairo_fill(cr);

		// Draw the start spot marker
		if(gui_start_widgets.state == 1)
		{
			ry = gui_start_widgets.jct_y;
			rh = TILESIZE;

			// In vertical levels, the upper bits actually set to the bottom of the level
			if(NoDice_the_level.header.is_vert && (ry & 0xFF00))
			{
				ry += (NoDice_the_level.header.total_screens - 1) * SCREEN_VHEIGHT * TILESIZE;
			}

			cairo_set_source_rgba(cr, 1.0, 1.0, 0.0, 0.75);
			cairo_rectangle(cr, rx, ry, rw, rh);
			cairo_fill(cr);
		}
	}

	// Done!
	cairo_destroy(cr);

	return TRUE;
}


static void gui_update_for_generators_foreach_callback(GtkWidget *widget, gpointer unused)
{
	gtk_widget_destroy(widget);
}


static void gui_select_handler(const GtkSelectableOverlay *selectable_overlay)
{
	int i;

	if(selectable_overlay->gen != NULL)
	{
		const struct NoDice_generator *tileset_gen = NULL;

		gui_selected_gen = NULL;

		// Need to find this generator so we can select it (and in addition configures the UI)
		for(i = 0; i < NoDice_the_level.tileset->gen_count; i++)
		{
			tileset_gen = &NoDice_the_level.tileset->generators[i];
			if( (tileset_gen->id == selectable_overlay->gen->id) && (tileset_gen->type == selectable_overlay->gen->type) )
			{
				int p;

				gui_listbox_set_index(gui_generator_listbox, i);

				// Need to set the spin controls!
				for(p = 0; p <= selectable_overlay->gen->size-3; p++)
				{
					gtk_spin_button_set_value(GTK_SPIN_BUTTON(gui_parameter_widgets[p].spin), selectable_overlay->gen->p[p]);
				}

				break;
			}
		}

		gui_selected_gen = selectable_overlay->gen;
	}
	else if(selectable_overlay->obj != NULL)
	{
		const struct NoDice_objects *this_obj = &NoDice_config.game.objects[selectable_overlay->obj->id];

		gui_selected_obj = selectable_overlay->obj;

		// Only select the non-special objects
		if(this_obj->special_options.options_list_count == 0)
			gui_listbox_set_index(gui_objects_listbox, gui_selected_obj->id);
	}
	else if(selectable_overlay->link != NULL)
	{
		gui_selected_link = selectable_overlay->link;
	}
}


enum SPECOBJ_OPS
{
	SPECOBJ_PROP,
	SPECOBJ_ADD,
	SPECOBJ_DELETE
};


static void gui_select_map_object_handler(const GtkSelectableOverlay *selectable_overlay)
{
	int index = selectable_overlay->obj - NoDice_the_level.objects;

	gui_selected_obj = selectable_overlay->obj;

	// Set map object in listbox
	if(gui_listbox_get_index(gui_map_objects_listbox) != index)
		gui_listbox_set_index(gui_map_objects_listbox, index);
}


static gboolean gui_map_object_selchange(GtkTreeView *treeview, gpointer user_data)
{
	int obj_index = gui_listbox_get_index_by_view(treeview);

	// Breaks potential infinite recursion
	gui_overlay_select_index(obj_index);

	return TRUE;
}


static void gui_map_object_properties(GtkButton *button, gpointer user_data)
{
	enum SPECOBJ_OPS op = GPOINTER_TO_INT(user_data);
	int index;

	// If getting properties or deleting, use the selected index
	if(op == SPECOBJ_PROP || op == SPECOBJ_DELETE)
	{
		index = gui_listbox_get_index(gui_map_objects_listbox);

		if(index == -1)
		{
			gui_display_message(TRUE, "Must select a Map Object first");
			return;
		}
	}

	if(op != SPECOBJ_DELETE)
	{
		struct NoDice_the_level_object object_backup, *object_to_edit;

		object_to_edit = &NoDice_the_level.objects[index];
		memcpy(&object_backup, object_to_edit, sizeof(struct NoDice_the_level_object));

		// Modify object
		if(gui_map_obj_properties(object_to_edit))
		{
			gui_update_for_generators();
		}
		else
			// Restore backup
			memcpy(object_to_edit, &object_backup, sizeof(struct NoDice_the_level_object));
	}
	else
	{
		// We don't really "delete" a map object so much as reset it to an off-screen empty
		edit_map_obj_clear(&NoDice_the_level.objects[index]);
	}
}


static void gui_load_tile_hint(const struct NoDice_tilehint *hint, int is_global)
{
	gui_surface_t *surface = NULL;
	unsigned char id = hint->id;

	snprintf(path_buffer, sizeof(path_buffer), SUBDIR_ICON_TILES "/%s", hint->overlay);
	if((surface = gui_surface_from_file(path_buffer)) != NULL)
	{
		gui_tilehints[id].hint = surface;
		gui_tilehints[id].is_global = is_global;
	}
	else
	{
		snprintf(path_buffer, sizeof(path_buffer), "Failed to load tile hint icon " SUBDIR_ICON_TILES "/%s", hint->overlay);
		gui_display_message(TRUE, path_buffer);
	}
}


static GtkSelectableOverlay *gui_overlay_add(GtkAllocation *overlay_alloc)
{
	GtkWidget *overlay = gtk_selectable_overlay_new();
	GtkSelectableOverlay *selectable_overlay = GTK_SELECTABLE_OVERLAY(overlay);

	gtk_fixed_put(GTK_FIXED(gui_fixed_view), overlay, overlay_alloc->x, overlay_alloc->y);
	gtk_widget_set_size_request(overlay, overlay_alloc->width, overlay_alloc->height);
	selectable_overlay->translation_data.origin_x = overlay_alloc->x;
	selectable_overlay->translation_data.origin_y = overlay_alloc->y;

	return selectable_overlay;
}


void gui_update_for_generators()
{
	// enable_for_load is true as long as a level is loaded
	// Used to disable some controls that shouldn't be used
	// if there's no level loaded (at startup)
	gboolean enable_for_load = (NoDice_the_level.tiles != NULL);

	// Destroy all child widgets from the fixed container
	gtk_container_foreach(GTK_CONTAINER(gui_fixed_view), gui_update_for_generators_foreach_callback, NULL);
	gui_selected_gen = NULL;

	{
		gboolean is_world_map = enable_for_load && NoDice_the_level.tileset->id == 0;
		gboolean is_level = enable_for_load && NoDice_the_level.tileset->id != 0;

		gtk_widget_set_visible(gtk_notebook_get_nth_page(GTK_NOTEBOOK(gui_notebook), ENPAGE_GENS), is_level);
		gtk_widget_set_visible(gtk_notebook_get_nth_page(GTK_NOTEBOOK(gui_notebook), ENPAGE_OBJS), is_level);
		gtk_widget_set_visible(gtk_notebook_get_nth_page(GTK_NOTEBOOK(gui_notebook), ENPAGE_STARTS), is_level);

		gtk_widget_set_visible(gtk_notebook_get_nth_page(GTK_NOTEBOOK(gui_notebook), ENPAGE_TILES), is_world_map);
		gtk_widget_set_visible(gtk_notebook_get_nth_page(GTK_NOTEBOOK(gui_notebook), ENPAGE_MOBJS), is_world_map);
		gtk_widget_set_visible(gtk_notebook_get_nth_page(GTK_NOTEBOOK(gui_notebook), ENPAGE_LINKS), is_world_map);
	}

	if(gui_start_widgets.edit_notebook_page == ENPAGE_GENS)
	{
		struct NoDice_the_level_generator *cur = NoDice_the_level.generators;

		// Destroy all child widgets from the fixed container
		gtk_container_foreach(GTK_CONTAINER(gui_fixed_view), gui_update_for_generators_foreach_callback, NULL);

		// Set proper size of the fixed view
		gui_fixed_calc_size();

		// Set default screen list
		if(gui_start_widgets.screen == 0)
			// FIXME: Hackish, sidestep bug from already being no screen 0
			gui_combobox_simple_set_selected(gui_start_widgets.screen_list, 1);

		gui_combobox_simple_set_selected(gui_start_widgets.screen_list, 0);

		while(cur != NULL)
		{
			if(cur->type != GENTYPE_JCTSTART)
			{
				GtkAllocation overlay_alloc;
				GtkSelectableOverlay *selectable_overlay;

				gui_gen_overlay_calc_allocation(cur, &overlay_alloc);

				selectable_overlay = gui_overlay_add(&overlay_alloc);
				selectable_overlay->gen = cur;
				selectable_overlay->select_handler = gui_select_handler;

				gtk_widget_show(GTK_WIDGET(selectable_overlay));
			}

			cur = cur->next;
		}
	}
	else if(gui_start_widgets.edit_notebook_page == ENPAGE_OBJS)
	{
		int i;
		GtkListStore *model;

		model = gui_listbox_get_disconnected_list(gui_objects_special_listbox);

		gtk_list_store_clear(model);

		for(i = 0; i < NoDice_the_level.object_count; i++)
		{
			GtkAllocation overlay_alloc;
			GtkSelectableOverlay *selectable_overlay;
			struct NoDice_the_level_object *object = &NoDice_the_level.objects[i];
			const struct NoDice_objects *this_obj = &NoDice_config.game.objects[object->id];
			int is_special = (this_obj->special_options.options_list_count != 0);

			if(is_special)
				// Add all special objects to the special object list
				gui_listbox_additem(model, i, this_obj->name);

			gui_obj_overlay_calc_allocation(object, &overlay_alloc);

			selectable_overlay = gui_overlay_add(&overlay_alloc);
			selectable_overlay->obj = object;
			selectable_overlay->select_handler = gui_select_handler;

			gtk_widget_set_visible(GTK_WIDGET(selectable_overlay), !is_special);
		}

		gui_listbox_reconnect_list(gui_objects_special_listbox, model);

	}
	else if(gui_start_widgets.edit_notebook_page == ENPAGE_TILES)
	{
		// Destroy all child widgets from the fixed container
		gtk_container_foreach(GTK_CONTAINER(gui_fixed_view), gui_update_for_generators_foreach_callback, NULL);

		// Set proper size of the fixed view
		gui_fixed_calc_size();
	}
	else if(gui_start_widgets.edit_notebook_page == ENPAGE_MOBJS)
	{
		int i;
		GtkListStore *model;

		model = gui_listbox_get_disconnected_list(gui_map_objects_listbox);

		gtk_list_store_clear(model);

		for(i = 0; i < NoDice_the_level.object_count; i++)
		{
			GtkAllocation overlay_alloc;
			GtkSelectableOverlay *selectable_overlay;
			struct NoDice_the_level_object *object = &NoDice_the_level.objects[i];
			const struct NoDice_objects *this_obj = &NoDice_config.game.objects[object->id];

			// Generate list item slot
			snprintf(path_buffer, PATH_MAX, "Slot %i/%i: %s", i+1, NoDice_the_level.object_count, this_obj->name);

			// Add all special objects to the special object list
			gui_listbox_additem(model, i, path_buffer);

			gui_obj_overlay_calc_allocation(object, &overlay_alloc);

			selectable_overlay = gui_overlay_add(&overlay_alloc);
			selectable_overlay->obj = object;
			selectable_overlay->select_handler = gui_select_map_object_handler;

			gtk_widget_set_visible(GTK_WIDGET(selectable_overlay), TRUE);
		}

		gui_listbox_reconnect_list(gui_map_objects_listbox, model);

	}
	else if(gui_start_widgets.edit_notebook_page == ENPAGE_LINKS)
	{
		int i;

		for(i = 0; i < NoDice_the_level.map_link_count; i++)
		{
			GtkAllocation overlay_alloc;
			GtkSelectableOverlay *selectable_overlay;
			struct NoDice_map_link *link = &NoDice_the_level.map_links[i];

			gui_link_overlay_calc_allocation(link, &overlay_alloc);

			selectable_overlay = gui_overlay_add(&overlay_alloc);
			selectable_overlay->link = link;
			selectable_overlay->select_handler = gui_select_handler;

			gtk_widget_set_visible(GTK_WIDGET(selectable_overlay), TRUE);
		}
	}


	// Disable things by context...
	gtk_widget_set_sensitive(gui_notebook, enable_for_load);
	gtk_widget_set_sensitive(menu_find_item(gui_menu, "/_Edit"), enable_for_load);
	gtk_widget_set_sensitive(menu_find_item(gui_menu, "/_View"), enable_for_load);
	gtk_widget_set_sensitive(menu_find_item(gui_menu, "/_File/_Save Level to ROM"), enable_for_load);

	if(enable_for_load)
		// World map has no use for the Tile Hints
		gtk_widget_set_sensitive(menu_find_item(gui_menu, "/_View/Tile Hints"), (NoDice_the_level.tileset->id != 0));

	gui_statusbar_update();

	gtk_widget_queue_draw(gui_fixed_view);
}


// Prepares UI for new level so we have proper generators etc.
void gui_refesh_for_level()
{
	GtkListStore *model = gui_listbox_get_disconnected_list(gui_generator_listbox);
	int i;

	// Clear out any specific tile hints
	for(i = 0; i < sizeof(gui_tilehints) / sizeof(struct _gui_tilehints); i++)
	{
		struct _gui_tilehints *tilehint = &gui_tilehints[i];

		// If hint was loaded and not a global one, delete it
		if(tilehint->hint != NULL && !tilehint->is_global)
		{
			gui_surface_destroy(tilehint->hint);
			tilehint->hint = NULL;
		}
	}

	// Load specific hints
	for(i = 0; i < NoDice_the_level.tileset->tilehint_count; i++)
		gui_load_tile_hint(&NoDice_the_level.tileset->tilehints[i], FALSE);

	// Clear all list items
	gtk_list_store_clear(model);

	// Add all generators
	for(i = 0; i < NoDice_the_level.tileset->gen_count; i++)
	{
		struct NoDice_generator *gen_cur = &NoDice_the_level.tileset->generators[i];

		gui_listbox_additem(model, i, gen_cur->name);
	}

	gui_listbox_reconnect_list(gui_generator_listbox, model);

	// Add in the generator overlay widgets
	gui_update_for_generators();
}


static void gui_overlay_select_index_foreach_callback(GtkWidget *widget, gpointer index_ptr)
{
	int index = *(int *)index_ptr;
	GtkSelectableOverlay *selectable_overlay = GTK_SELECTABLE_OVERLAY(widget);

	// This is the one that SHOULD be selected!
	if(selectable_overlay->gen != NULL && selectable_overlay->gen->index == index)
	{
		gtk_selectable_overlay_select(selectable_overlay);
		gui_selected_gen = selectable_overlay->gen;
	}
	else if(selectable_overlay->obj != NULL && (selectable_overlay->obj - NoDice_the_level.objects) == index)
	{
		const struct NoDice_objects *this_obj = &NoDice_config.game.objects[selectable_overlay->obj->id];

		gtk_selectable_overlay_select(selectable_overlay);

		if(this_obj->special_options.options_list_count == 0)
			// Non-special objects can be arbitrarily inserted
			gui_selected_obj = selectable_overlay->obj;
		else
			// Special objects just become visible
			gtk_widget_show(GTK_WIDGET(selectable_overlay));
 	}
	else if(selectable_overlay->selected == TRUE)
	{
		// This one should NOT be selected!
		selectable_overlay->selected = FALSE;

		if( (selectable_overlay->obj != NULL) && (NoDice_config.game.objects[selectable_overlay->obj->id].special_options.options_list_count != 0) )
			// Special object should be hidden
			gtk_widget_hide(GTK_WIDGET(selectable_overlay));
		else
			// Otherwise, draw the overlay (update for de-selection)
			gtk_widget_queue_draw(widget);
	}
}


void gui_overlay_select_index(int index)
{
	gui_selected_gen = NULL;

	// Find the generator that matches index and select it!
	gtk_container_foreach(GTK_CONTAINER(gui_fixed_view), gui_overlay_select_index_foreach_callback, &index);
}


static gboolean gui_fixed_view_button_hander(GtkWidget *widget, GdkEventButton *event)
{
	if(event->type == GDK_BUTTON_PRESS)
	{
		// Insert generator or object here!
		int 	row = (int)((double)event->y / gui_draw_info.zoom / TILESIZE),
			col = (int)((double)event->x / gui_draw_info.zoom / TILESIZE);

		// If user clicked left button, and we're seeing it, that means
		// they clicked outside of any generator/object, so deselect!!
		if(event->button == 1)
		{
			if(gui_start_widgets.edit_notebook_page == ENPAGE_TILES)
			{
				// Map tiles only...
				gui_selected_map_tile = edit_maptile_get(row, col);

				// Set the selected tile
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(map_tile_buttons.buttons[gui_selected_map_tile]), TRUE);
			}
			else
			{
				gui_overlay_select_index(-1);	// An impossible index will deselect and not select anything!
			}
		}
		// If user clicked right button, insert generator/object
		else if(event->button == 3)
		{
			// Generator insert mode
			if(gui_start_widgets.edit_notebook_page == ENPAGE_GENS)
			{
				if(gui_selected_insert_gen != NULL)
				{
					int i;
					unsigned char parameters[GEN_MAX_PARAMS];

					for(i = 0; i < GEN_MAX_PARAMS; i++)
						parameters[i] = (unsigned char)gtk_spin_button_get_value(GTK_SPIN_BUTTON(gui_parameter_widgets[i].spin));

					edit_gen_insert_generator(gui_selected_insert_gen, row, col, parameters);
				}
			}
			else if(gui_start_widgets.edit_notebook_page == ENPAGE_OBJS)
			{
				if(gui_selected_insert_obj != NULL)
				{
					if(!edit_obj_insert_object(gui_selected_insert_obj, row, col))
					{
						gui_display_message(TRUE, "You cannot insert any more objects -- maximum for this object set has been reached!");
					}
				}
			}			// Start spot horizontal set mode
			else if(gui_start_widgets.edit_notebook_page == ENPAGE_STARTS)
			{
				gui_start_widgets.jct_x = col * TILESIZE;
				gtk_widget_queue_draw(gui_fixed_view);
			}
			else if(gui_start_widgets.edit_notebook_page == ENPAGE_TILES)
			{
				edit_maptile_set(row, col, gui_selected_map_tile);
				gtk_widget_queue_draw(gui_fixed_view);
			}
			else if(gui_start_widgets.edit_notebook_page == ENPAGE_MOBJS)
			{
				if(gui_selected_obj != NULL)
				{
					int index = gui_listbox_get_index(gui_map_objects_listbox);

					int row_diff = row - gui_selected_obj->row;
					int col_diff = col - gui_selected_obj->col;
					edit_obj_translate(gui_selected_obj, row_diff, col_diff);

					gui_listbox_set_index(gui_map_objects_listbox, index);
				}
			}
			else if(gui_start_widgets.edit_notebook_page == ENPAGE_LINKS)
			{
				if(!edit_link_insert_link(row, col))
				{
					gui_display_message(TRUE, "You cannot insert any more links -- maximum has been reached!");
				}
			}
		}
	}

	return TRUE;
}


static gboolean gui_generator_selchange(GtkTreeView *treeview, gpointer user_data)
{
	int i;
	int gen_index = gui_listbox_get_index_by_view(treeview);

	// Set the selected insertable generator
	gui_selected_insert_gen = &NoDice_the_level.tileset->generators[gen_index];

	// Update UI
	for(i = 0; i < GEN_MAX_PARAMS; i++)
	{
		const struct NoDice_generator_parameter *p = &gui_selected_insert_gen->parameters[i];
		struct _gui_parameter_widgets *ui = &gui_parameter_widgets[i];

		if(p->name != NULL)
		{
			gtk_widget_show(ui->spin);

			gtk_label_set(GTK_LABEL(ui->label), p->name);
			gtk_spin_button_set_range(GTK_SPIN_BUTTON(ui->spin), p->min, p->max);
		}
		else
		{
			gtk_widget_hide(ui->spin);

			gtk_label_set(GTK_LABEL(ui->label), "N/A");
		}
	}

	// Deselect everything because we don't support changing the generator
	if(gui_selected_gen != NULL)
	{
		if(gui_selected_insert_gen->id != gui_selected_gen->id || gui_selected_insert_gen->type != gui_selected_gen->type)
			gui_overlay_select_index(-1);	// An impossible index will deselect and not select anything!
	}

	return TRUE;
}


static gboolean gui_object_selchange(GtkTreeView *treeview, gpointer user_data)
{
	int obj_index = gui_listbox_get_index_by_view(treeview);

	if(obj_index >= 0)
	{
		// Set the selected insertable object
		gui_selected_insert_obj = &NoDice_config.game.objects[obj_index];

		// Deselect everything because we don't support changing the object
		if(gui_selected_obj != NULL)
		{
			if( (gui_selected_insert_obj - NoDice_config.game.objects) != gui_selected_obj->id )
				gui_overlay_select_index(-1);	// An impossible index will deselect and not select anything!
		}
	}

	return TRUE;
}


static gboolean gui_special_object_selchange(GtkTreeView *treeview, gpointer user_data)
{
	int obj_index = gui_listbox_get_index_by_view(treeview);

	gui_overlay_select_index(obj_index);

	return TRUE;
}


static gboolean gui_notebook_selected(GtkNotebook *notebook, gpointer arg1, guint arg2, gpointer user_data)
{
	gui_start_widgets.edit_notebook_page = arg2;

	// No "delete" in start spot mode or world map tiles edit mode
	gtk_widget_set_sensitive(menu_find_item(gui_menu, "/_Edit/_Delete"), (arg2 != ENPAGE_STARTS && arg2 != ENPAGE_TILES));

	// No arrangement for objects since they implicitly sort anyway
	gtk_widget_set_sensitive(menu_find_item(gui_menu, "/_Edit/Arrange"), (arg2 == ENPAGE_GENS));

	// No level properties for world map
	gtk_widget_set_sensitive(menu_find_item(gui_menu, "/_Edit/Level _Properties"), (arg2 != ENPAGE_TILES) && (arg2 != ENPAGE_MOBJS));


	gui_update_for_generators();

	return TRUE;
}


static void gui_parameter_spin_val_change(GtkSpinButton *spinbutton, gpointer user_data)
{
	// If we have a selected generator, then changing the spin controls manipulates it!
	if(gui_selected_gen != NULL)
	{
		int i;
		unsigned char parameters[GEN_MAX_PARAMS];
		long index = (long)user_data;

		for(i = 0; i < GEN_MAX_PARAMS; i++)
		{
			if(i != index)
				parameters[i] = gui_selected_gen->p[i];
			else
				parameters[i] = (unsigned char)gtk_spin_button_get_value(GTK_SPIN_BUTTON(gui_parameter_widgets[i].spin));
		}

		edit_gen_set_parameters(gui_selected_gen, parameters);
	}
}


const char *gui_make_image_path(const struct NoDice_tileset *tileset, const struct NoDice_the_levels *level)
{
	int i;

	// Build a filename out of tileset and level/object labels
	snprintf(path_buffer, sizeof(path_buffer)-5, SUBDIR_ICON_LEVELS "/%s_%s_%s", tileset->name, level->layoutlabel, level->objectlabel);

	for(i=strlen(SUBDIR_ICON_LEVELS)+1; path_buffer[i] != '\0'; i++)
	{
		if(!isdigit(path_buffer[i]) && !isalpha(path_buffer[i]))
			path_buffer[i] = '_';
	}

	strcat(path_buffer, ".png");

	return path_buffer;
}


static void gui_starts_properties_foreach_callback(GtkWidget *widget, gpointer relative_index_ptr)
{
	int *relative_index = (int *)relative_index_ptr;
	unsigned char jct_y = NoDice_the_level.Level_JctYLHStart[gui_combobox_simple_get_index(gui_start_widgets.screen_list)];

	// Assuming hbox containing a label on left and a control on right
	if(GTK_IS_HBOX(widget))
	{
		GList *box_children = gtk_container_get_children(GTK_CONTAINER(widget));
		GtkWidget *right_ctl = GTK_WIDGET(box_children->next->data);
		const struct NoDice_header_options *jct_option = &NoDice_config.game.jct_options.options_list[*relative_index];

		if(GTK_IS_CHECK_BUTTON(right_ctl))
		{
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(right_ctl), jct_y & jct_option->mask);

			(*relative_index)++;
		}
		else if(GTK_IS_COMBO_BOX(right_ctl))
		{
			unsigned char relative_value = (jct_y & jct_option->mask) >> jct_option->shift;

			gui_combobox_simple_set_selected(right_ctl, relative_value);

			(*relative_index)++;
		}

		g_list_free(box_children);
	}
}


static void gui_screen_list_change(GtkComboBox *widget, gpointer user_data)
{
	int relative_index = 0;

	gui_start_widgets.screen = gui_combobox_simple_get_index(GTK_WIDGET(widget));
	gtk_widget_queue_draw(GTK_WIDGET(gui_fixed_view));

	gtk_container_foreach(GTK_CONTAINER(gui_start_widgets.property_vbox), gui_starts_properties_foreach_callback, &relative_index);
}


static void gui_starts_button_next(GtkButton *button, gpointer user_data)
{
	long is_cancel = (long)user_data;
	GtkTextIter start, end;
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gui_start_widgets.textbox_desc));

	if(gui_start_widgets.state == 0)
	{
		int screen = gui_combobox_simple_get_index(gui_start_widgets.screen_list);
		unsigned char jct_c = NoDice_the_level.Level_JctXLHStart[screen];

		// Record the junction X start
		gui_start_widgets.jct_x = (jct_c & 0xF0) | ((jct_c & 0x0F) << 8);

		// Set the "data" value
		gui_start_widgets.jct_data = NoDice_the_level.Level_JctYLHStart[screen];

		// Backup the tileset and screen we're editing
		gui_start_widgets.revert_tileset = NoDice_the_level.tileset;
		gui_start_widgets.screen_edit = (unsigned char)screen;

		// Need to load the alternate level
		edit_startspot_alt_load();
		gui_fixed_calc_size();
		gtk_widget_queue_draw(GTK_WIDGET(gui_fixed_view));

		// Hide the screen selector
		gtk_widget_hide(gui_start_widgets.screen_list);

		// Enable cancel button
		gtk_widget_set_sensitive(gui_start_widgets.button_cancel, TRUE);

		// Set new action button text
		gtk_button_set_label(GTK_BUTTON(gui_start_widgets.button_active), gui_start_widgets_button_by_state[1]);

		// Show the start spot controls
		gtk_widget_show(gui_start_widgets.property_vbox);

		// Disable menus that could be troublemakers right now!
		gtk_widget_set_sensitive(menu_find_item(gui_menu, "/_File"), FALSE);
		gtk_widget_set_sensitive(menu_find_item(gui_menu, "/_Edit"), FALSE);

		gui_start_widgets.state = 1;
	}
	else if(gui_start_widgets.state == 1)
	{
		unsigned char jct_data = gui_start_widgets.jct_data;

		// Hackish: is_cancel == -1 means we're just creating this area (gui_create_right_pane),
		// so don't actually try to undo things... that will mess us up when creating a new
		// level and we're rebooting the right pane
		if(is_cancel != -1)
		{
			// A little hackish, but the tileset must be put back here
			NoDice_the_level.tileset = gui_start_widgets.revert_tileset;

			// Revert to level
			edit_startspot_alt_revert();
			gui_fixed_calc_size();
			gtk_widget_queue_draw(GTK_WIDGET(gui_fixed_view));
		}

		// Only actually apply anything if not cancelling
		if(!is_cancel)
		{
			unsigned char screen = gui_start_widgets.screen_edit;
			unsigned char parameters[2];
			struct NoDice_the_level_generator *gen = NoDice_the_level.generators;

			NoDice_the_level.Level_JctXLHStart[screen] = (gui_start_widgets.jct_x & 0xF0) | ((gui_start_widgets.jct_x & 0xF00) >> 8);
			NoDice_the_level.Level_JctYLHStart[screen] = jct_data;
			parameters[0] = NoDice_the_level.Level_JctYLHStart[screen];
			parameters[1] = NoDice_the_level.Level_JctXLHStart[screen];

			// See if we already have a junction generator by this ID
			while(gen != NULL)
			{
				// Found it!
				if(gen->type == GENTYPE_JCTSTART && gen->id == screen)
				{
					edit_gen_set_parameters(gen, parameters);
					break;
				}

				gen = gen->next;
			}

			if(gen == NULL)
			{
				// We do not have a junction generator with this ID,
				// so we have to add it...
				struct NoDice_generator new_jct_gen;
				new_jct_gen.type = GENTYPE_JCTSTART;
				new_jct_gen.id = screen;

				edit_gen_insert_generator(&new_jct_gen, 0, 0, parameters);
			}
		}

		// Show the screen selector
		gtk_widget_show(gui_start_widgets.screen_list);

		// Disable cancel button
		gtk_widget_set_sensitive(gui_start_widgets.button_cancel, FALSE);

		// Set new action button text
		gtk_button_set_label(GTK_BUTTON(gui_start_widgets.button_active), gui_start_widgets_button_by_state[0]);

		// Hide the start spot controls
		gtk_widget_hide(gui_start_widgets.property_vbox);

		// Re-enable menus
		gtk_widget_set_sensitive(menu_find_item(gui_menu, "/_File"), TRUE);
		gtk_widget_set_sensitive(menu_find_item(gui_menu, "/_Edit"), TRUE);

		gui_start_widgets.state = 0;
		gui_statusbar_update();
	}

	gtk_text_buffer_get_start_iter(buffer, &start);
	gtk_text_buffer_get_end_iter(buffer, &end);
	gtk_text_buffer_delete(buffer, &start, &end);

	gtk_text_buffer_insert_at_cursor(buffer, gui_start_widgets_desc_by_state[gui_start_widgets.state], strlen(gui_start_widgets_desc_by_state[gui_start_widgets.state]));
}


static void gui_starts_properties_option_list_change(GtkComboBox *widget, gpointer user_data)
{
	int i;
	int header_ref_tag = (int)(long)user_data;
	int option_list_index = (header_ref_tag & 0x0000FFFF);
	const struct NoDice_header_options *option_list = &NoDice_config.game.jct_options.options_list[option_list_index];
	int relative_value = gui_combobox_simple_get_index(GTK_WIDGET(widget));
	unsigned char *jct_y = &gui_start_widgets.jct_data;
	const char *label = NULL;

	// Need the specific label for the Y position (display only)
	for(i = 0; i < option_list->options_count; i++)
	{
		if(option_list->options[i].value == relative_value)
		{
			label = option_list->options[i].label;
			break;
		}
	}

	// Mask out the old value in the junction byte
	*jct_y &= ~option_list->mask;

	// Set the new value
	*jct_y |= (relative_value << option_list->shift) & option_list->mask;

	// Updates display to show where Player emerges
	if(label != NULL && label[0] == 'Y')
	{
		gui_start_widgets.jct_y = strtoul(&label[1], NULL, 16);
		gtk_widget_queue_draw(GTK_WIDGET(gui_fixed_view));
	}
}


static void gui_starts_properties_option_toggle_change(GtkToggleButton *togglebutton, gpointer user_data)
{
	int header_ref_tag = (int)(long)user_data;
	int option_list_index = (header_ref_tag & 0x0000FFFF);
	const struct NoDice_header_options *option_list = &NoDice_config.game.jct_options.options_list[option_list_index];
	unsigned char *jct_y = &gui_start_widgets.jct_data;

	if(!gtk_toggle_button_get_active(togglebutton))
		// Mask out the old value in the junction byte
		*jct_y &= ~option_list->mask;
	else
		// Set the new value
		*jct_y |= option_list->mask;
}


static void gui_starts_properties_option_spin_change(GtkSpinButton *spinbutton, gpointer user_data)
{
	int header_ref_tag = (int)(long)user_data;
	int option_list_index = (header_ref_tag & 0x0000FFFF);
	const struct NoDice_header_options *option_list = &NoDice_config.game.jct_options.options_list[option_list_index];
	int relative_value = (int)gtk_spin_button_get_value(spinbutton);
	unsigned char *jct_y = &gui_start_widgets.jct_data;
	const char *label = NULL;

	// Mask out the old value in the junction byte
	*jct_y &= ~option_list->mask;

	// Set the new value
	*jct_y |= (relative_value << option_list->shift) & option_list->mask;

	if(label != NULL && label[0] == 'Y')
	{
		gui_start_widgets.jct_y = strtoul(&label[1], NULL, 16);
		gtk_widget_queue_draw(GTK_WIDGET(gui_fixed_view));
	}
}


static gint gui_object_list_sort(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data)
{
	int result;

	GValue value_a = { 0 }, value_b = { 0 };

	gtk_tree_model_get_value(model, a, 0, &value_a);
	gtk_tree_model_get_value(model, b, 0, &value_b);

	result = strcmp((gchar *)value_a.data->v_pointer, (gchar *)value_b.data->v_pointer);

	g_value_unset(&value_a);
	g_value_unset(&value_b);

	return result;
}


static void gui_special_objs_properties(GtkButton *button, gpointer user_data)
{
	enum SPECOBJ_OPS op = GPOINTER_TO_INT(user_data);
	int index;

	// If getting properties or deleting, use the selected index
	if(op == SPECOBJ_PROP || op == SPECOBJ_DELETE)
	{
		index = gui_listbox_get_index(gui_objects_special_listbox);

		if(index == -1)
		{
			gui_display_message(TRUE, "Must select a Special Object first");
			return;
		}
	}

	if(op != SPECOBJ_DELETE)
	{
		// If editing properties or adding anew...

		struct NoDice_the_level_object object_backup, *object_to_edit;

		if(op == SPECOBJ_ADD)
		{
			// Adding new...
			index = NoDice_the_level.object_count;

			if(index == OBJS_MAX)
			{
				gui_display_message(TRUE, "You cannot insert any more objects -- maximum for this object set has been reached!");
				return;
			}

			object_to_edit = &NoDice_the_level.objects[index];
			object_to_edit->id = 0;
		}
		else
		{
			object_to_edit = &NoDice_the_level.objects[index];
			memcpy(&object_backup, object_to_edit, sizeof(struct NoDice_the_level_object));
		}

		// Add/Modify object
		if(gui_special_obj_properties(object_to_edit))
		{
			if(op == SPECOBJ_ADD)
				edit_obj_insert_object(&NoDice_config.game.objects[object_to_edit->id], object_to_edit->row, 0);
			else
			{
				int row_diff = object_to_edit->row - object_backup.row;
				int col_diff = object_to_edit->col - object_backup.col;

				// Restore backup
				memcpy(object_to_edit, &object_backup, sizeof(struct NoDice_the_level_object));

				// Force a translation to update the object with proper undo
				edit_obj_translate(object_to_edit, row_diff, col_diff);
			}
		}
		else
			// Restore backup
			memcpy(object_to_edit, &object_backup, sizeof(struct NoDice_the_level_object));
	}
	else
	{
		edit_obj_remove(&NoDice_the_level.objects[index]);
	}
}


static void gui_map_tiles_size_change(GtkWidget *widget, GdkRectangle *allocation, gpointer user_data)
{
	GtkFixed *fixed = GTK_FIXED(user_data);
	int tile_size = TILESIZE * 2;
	int col_span = allocation->width / tile_size;
	int col, tile = 0, x = 0, y = 0;

	if(col_span != map_tile_buttons.col_size)
	{
		map_tile_buttons.col_size = col_span;

		while(tile < 255)
		{
			x = 0;

			for(col = 0; col < col_span; col++)
			{
				gtk_fixed_move(fixed, map_tile_buttons.buttons[tile++], x, y);

				if(tile == 255)
					return;

				x += tile_size;
			}

			y += tile_size;
		}
	}
}


static gboolean gui_map_tiles_expose_event_callback (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
	int zoom = 2;
	int tile = GPOINTER_TO_INT(data), tile_size = (TILESIZE * zoom);

	cairo_t *cr;

	cr = gdk_cairo_create(widget->window);
	gui_draw_info.context = (void *)cr;

	cairo_save(cr);

	cairo_translate(cr, event->area.x & ~(tile_size-1), event->area.y & ~(tile_size-1));
	cairo_scale(cr, zoom, zoom);

	ppu_draw_tile(0, 0, tile, tile/64);

	cairo_restore(cr);

	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
	{
		cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.75);
		cairo_rectangle(cr, event->area.x, event->area.y, event->area.width, event->area.height);
		cairo_fill(cr);
	}

	cairo_destroy(cr);

	return TRUE;
}


static void gui_map_tiles_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	int tile = GPOINTER_TO_INT(user_data);

	if(gtk_toggle_button_get_active(togglebutton))
		gui_selected_map_tile = (unsigned char)tile;
}


static void gui_map_link_properties_clicked(GtkButton *button, gpointer user_data)
{
	if(gui_selected_link != NULL)
	{
		struct NoDice_map_link map_link_backup;

		// Back up in case of cancel
		memcpy(&map_link_backup, gui_selected_link, sizeof(struct NoDice_map_link));

		if(gui_map_link_properties(gui_selected_link))
			edit_link_adjust(gui_selected_link, &map_link_backup);
		else
			// Restore on cancel
			memcpy(gui_selected_link, &map_link_backup, sizeof(struct NoDice_map_link));
	}
	else
		gui_display_message(TRUE, "You must select a map link first!");
}


void gui_boot(int argc, char *argv[])
{
	gdk_threads_init();

	// Initialize GTK+
	gtk_init (&argc, &argv);
}


static void gui_create_right_pane(GtkWidget *vbox)
{
	// Generators/Objects
	gui_notebook = gtk_notebook_new();
	gtk_box_pack_start(GTK_BOX(vbox), gui_notebook, TRUE, TRUE, 0);

	// Generators tab
	{
		GtkWidget *vbox_gens = gtk_vbox_new(FALSE, 0);
		GtkWidget *list_frame = gtk_frame_new(NULL);

		// Parameters
		{
			int i;

			GtkWidget *table = gtk_table_new(1 + GEN_MAX_PARAMS, 2, TRUE);

			gtk_table_attach(GTK_TABLE(table), gtk_label_new("Parameters"), 0, 2, 0, 1, 0, 0, 0, 0);

			for(i=0; i<GEN_MAX_PARAMS; i++)
			{
				gui_parameter_widgets[i].label = gtk_label_new("N/A");
				gtk_table_attach_defaults(GTK_TABLE(table), gui_parameter_widgets[i].label, 0, 1, 1+i, 2+i);

				gui_parameter_widgets[i].spin = gtk_spin_button_new_with_range(0, (i == 0) ? 15 : 255, 1);
				gtk_table_attach_defaults(GTK_TABLE(table), gui_parameter_widgets[i].spin, 1, 2, 1+i, 2+i);

				g_signal_connect (G_OBJECT (gui_parameter_widgets[i].spin), "value-changed", G_CALLBACK (gui_parameter_spin_val_change), GINT_TO_POINTER(i));
			}


			gtk_box_pack_start(GTK_BOX(vbox_gens), table, FALSE, FALSE, 0);
		}

		// The list of generators
		gui_generator_listbox = gui_listbox_new(G_CALLBACK(gui_generator_selchange), NULL);

		// Added to frame
		gtk_container_add (GTK_CONTAINER (list_frame), gui_generator_listbox);

		// Frame added to vbox
		gtk_box_pack_start(GTK_BOX(vbox_gens), list_frame, TRUE, TRUE, 5);

		gtk_notebook_append_page(GTK_NOTEBOOK(gui_notebook), vbox_gens, gtk_label_new(edit_notebook_page_names[ENPAGE_GENS]));
	}

	// Objects tab
	{
		int i;
		GtkWidget *vbox_objs = gtk_vbox_new(FALSE, 0);
		GtkWidget *list_frame;
		GtkListStore *model;

		// Frame
		list_frame = gtk_frame_new(NULL);

		// The list of objects
		gui_objects_listbox = gui_listbox_new(G_CALLBACK(gui_object_selchange), NULL);

		// Added to frame
		gtk_container_add (GTK_CONTAINER (list_frame), gui_objects_listbox);

		// Frame added to vbox
		gtk_box_pack_start(GTK_BOX(vbox_objs), list_frame, TRUE, TRUE, 5);


		model = gui_listbox_get_disconnected_list(gui_objects_listbox);

		// Add all non-special objects
		for(i = 0; i < 256; i++)
		{
			struct NoDice_objects *this_obj = &NoDice_config.game.regular_objects[i];

			if(this_obj->name != NULL && this_obj->special_options.options_list_count == 0)
				gui_listbox_additem(model, i, this_obj->name);
		}

		{
			GtkTreeSortable *sortable = GTK_TREE_SORTABLE(model);
			gtk_tree_sortable_set_sort_func(sortable, 0, gui_object_list_sort, NULL, NULL);
			gtk_tree_sortable_set_sort_column_id(sortable, 0, GTK_SORT_ASCENDING);

		}

		gui_listbox_reconnect_list(gui_objects_listbox, model);

		///////////////////////////////////////////////

		gtk_box_pack_start(GTK_BOX(vbox_objs), gtk_label_new("Special Objects"), FALSE, FALSE, 2);

		{
			GtkWidget *hbox_buttons = gtk_hbox_new(FALSE, 0);
			GtkWidget *button;

			button = gtk_button_new_from_stock(GTK_STOCK_PROPERTIES);
			g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(gui_special_objs_properties), GINT_TO_POINTER(SPECOBJ_PROP));
			gtk_box_pack_start(GTK_BOX(hbox_buttons), button, FALSE, FALSE, 2);

			button = gtk_button_new_from_stock(GTK_STOCK_ADD);
			g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(gui_special_objs_properties), GINT_TO_POINTER(SPECOBJ_ADD));
			gtk_box_pack_start(GTK_BOX(hbox_buttons), button, FALSE, FALSE, 2);

			button = gtk_button_new_from_stock(GTK_STOCK_DELETE);
			g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(gui_special_objs_properties), GINT_TO_POINTER(SPECOBJ_DELETE));
			gtk_box_pack_start(GTK_BOX(hbox_buttons), button, FALSE, FALSE, 2);

			// HBox added to vbox
			gtk_box_pack_start(GTK_BOX(vbox_objs), hbox_buttons, FALSE, FALSE, 2);
		}

		///////////////////////////////////////////////


		// Frame
		list_frame = gtk_frame_new(NULL);

		// The list of special objects
		gui_objects_special_listbox = gui_listbox_new(G_CALLBACK(gui_special_object_selchange), NULL);

		// Added to frame
		gtk_container_add (GTK_CONTAINER (list_frame), gui_objects_special_listbox);

		// Frame added to vbox
		gtk_box_pack_start(GTK_BOX(vbox_objs), list_frame, TRUE, TRUE, 5);


		gtk_notebook_append_page(GTK_NOTEBOOK(gui_notebook), vbox_objs, gtk_label_new(edit_notebook_page_names[ENPAGE_OBJS]));
	}

	// Start Positions tab
	{
		int i;
		char buffer[24];

		GtkWidget *vbox = gtk_vbox_new(FALSE, 6);

		gui_start_widgets.screen_list = gui_combobox_simple_new();
		for(i=0; i<16; i++)
		{
			snprintf(buffer, sizeof(buffer), "Screen %i", i+1);
			gui_combobox_simple_add_item(gui_start_widgets.screen_list, i, buffer);
		}
		g_signal_connect(gui_start_widgets.screen_list, "changed", G_CALLBACK(gui_screen_list_change), NULL);
		gtk_box_pack_start(GTK_BOX(vbox), gui_start_widgets.screen_list, FALSE, FALSE, 0);


		gui_start_widgets.button_active = gtk_button_new();
		g_signal_connect(G_OBJECT(gui_start_widgets.button_active), "clicked", G_CALLBACK(gui_starts_button_next), GINT_TO_POINTER(FALSE));
		gtk_box_pack_start(GTK_BOX(vbox), gui_start_widgets.button_active, FALSE, FALSE, 0);


		gui_start_widgets.button_cancel = gtk_button_new_with_label("Cancel");
		g_signal_connect(G_OBJECT(gui_start_widgets.button_cancel), "clicked", G_CALLBACK(gui_starts_button_next), GINT_TO_POINTER(TRUE));
		gtk_widget_set_sensitive(gui_start_widgets.button_cancel, FALSE);
		gtk_box_pack_start(GTK_BOX(vbox), gui_start_widgets.button_cancel, FALSE, FALSE, 0);

		gui_start_widgets.property_vbox = gtk_vbox_new(FALSE, 6);
		gui_generate_option_controls(&gui_start_pos_props_context, gui_start_widgets.property_vbox, 0, 0, &NoDice_config.game.jct_options, G_CALLBACK(gui_starts_properties_option_list_change), G_CALLBACK(gui_starts_properties_option_toggle_change), G_CALLBACK(gui_starts_properties_option_spin_change));
		gtk_box_pack_start(GTK_BOX(vbox), gui_start_widgets.property_vbox, FALSE, FALSE, 0);

		gui_start_widgets.textbox_desc = gtk_text_view_new();
		gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(gui_start_widgets.textbox_desc), GTK_WRAP_WORD_CHAR);
		gtk_text_view_set_editable(GTK_TEXT_VIEW(gui_start_widgets.textbox_desc), FALSE);
		gtk_box_pack_start(GTK_BOX(vbox), gui_start_widgets.textbox_desc, TRUE, TRUE, 0);

		// Forcing reset to button and directions are set correctly
		gui_start_widgets.state = 1;
		gui_starts_button_next(GTK_BUTTON(gui_start_widgets.button_cancel), GINT_TO_POINTER(-1));

		gtk_notebook_append_page(GTK_NOTEBOOK(gui_notebook), vbox, gtk_label_new(edit_notebook_page_names[ENPAGE_STARTS]));
	}

	// World map tiles
	{
		int tile;
		GtkWidget *fixed = gtk_fixed_new();
		GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);

		for(tile = 0; tile < 255; tile++)
		{
			if(tile == 0)
				map_tile_buttons.buttons[0] = gtk_radio_button_new(NULL);
			else
				map_tile_buttons.buttons[tile] = gtk_radio_button_new_from_widget(GTK_RADIO_BUTTON(map_tile_buttons.buttons[tile - 1]));

			gtk_widget_set_size_request(map_tile_buttons.buttons[tile], TILESIZE*2, TILESIZE*2);

			g_signal_connect(G_OBJECT(map_tile_buttons.buttons[tile]), "toggled", G_CALLBACK(gui_map_tiles_toggled), GINT_TO_POINTER(tile));
			g_signal_connect(G_OBJECT(map_tile_buttons.buttons[tile]), "expose_event", G_CALLBACK (gui_map_tiles_expose_event_callback), GINT_TO_POINTER(tile));

			// These will be positioned on the resize
			gtk_fixed_put(GTK_FIXED(fixed), map_tile_buttons.buttons[tile], 0, 0);
		}

		gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled_window), fixed);

		//gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

		g_signal_connect(scrolled_window, "size-allocate", G_CALLBACK(gui_map_tiles_size_change), fixed);

		gtk_notebook_append_page(GTK_NOTEBOOK(gui_notebook), scrolled_window, gtk_label_new(edit_notebook_page_names[ENPAGE_TILES]));
	}

	// World map objects
	{
		GtkWidget *vbox = gtk_vbox_new(FALSE, 6);

		// Frame
		GtkWidget *list_frame = gtk_frame_new(NULL);

		// The list of map objects
		gui_map_objects_listbox = gui_listbox_new(G_CALLBACK(gui_map_object_selchange), NULL);

		// Added to frame
		gtk_container_add (GTK_CONTAINER (list_frame), gui_map_objects_listbox);

		// Frame added to vbox
		gtk_box_pack_start(GTK_BOX(vbox), list_frame, TRUE, TRUE, 5);

		{
			GtkWidget *hbox_buttons = gtk_hbox_new(FALSE, 0);
			GtkWidget *button;

			button = gtk_button_new_from_stock(GTK_STOCK_PROPERTIES);
			g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(gui_map_object_properties), GINT_TO_POINTER(SPECOBJ_PROP));
			gtk_box_pack_start(GTK_BOX(hbox_buttons), button, FALSE, FALSE, 2);

			button = gtk_button_new_from_stock(GTK_STOCK_DELETE);
			g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(gui_map_object_properties), GINT_TO_POINTER(SPECOBJ_DELETE));
			gtk_box_pack_start(GTK_BOX(hbox_buttons), button, FALSE, FALSE, 2);

			// HBox added to vbox
			gtk_box_pack_start(GTK_BOX(vbox), hbox_buttons, FALSE, FALSE, 2);
		}

		gtk_notebook_append_page(GTK_NOTEBOOK(gui_notebook), vbox, gtk_label_new(edit_notebook_page_names[ENPAGE_MOBJS]));
	}

	// World map links
	{
		GtkWidget *vbox = gtk_vbox_new(FALSE, 6);
		GtkWidget *button;

		button = gtk_button_new_from_stock(GTK_STOCK_PROPERTIES);
		g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(gui_map_link_properties_clicked), NULL);
		gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 2);

		gtk_notebook_append_page(GTK_NOTEBOOK(gui_notebook), vbox, gtk_label_new(edit_notebook_page_names[ENPAGE_LINKS]));
	}

	g_signal_connect(gui_notebook, "switch-page", G_CALLBACK(gui_notebook_selected), NULL);
}


int gui_init()
{
#if GLIB_MAJOR_VERSION <= 2 && GLIB_MINOR_VERSION < 32
	// Initialize threading
	g_thread_init(NULL);
#endif

	// Create the main window
	gui_main_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(gui_main_window), 800, 600);
	gtk_window_set_title (GTK_WINDOW (gui_main_window), "NoDice");
	gtk_window_set_position (GTK_WINDOW (gui_main_window), GTK_WIN_POS_CENTER);
	gtk_widget_realize (gui_main_window);
	g_signal_connect (gui_main_window, "destroy", gtk_main_quit, NULL);

	// Menu power v-box
	{
		GtkWidget *vbox = gtk_vbox_new (FALSE, 1);
		gtk_container_add (GTK_CONTAINER (gui_main_window), vbox);

		// Top
		{
			GtkCheckMenuItem *menu_item;

			// Add the menu
			gui_menu = get_menubar_menu(gui_main_window);
			gtk_box_pack_start(GTK_BOX(vbox), gui_menu, FALSE, TRUE, 0);

			// Select the translucent tile hint
			menu_item = GTK_CHECK_MENU_ITEM(menu_find_item(gui_menu, "/_View/Tile Hints/Translucent"));
			gtk_check_menu_item_set_active(menu_item, TRUE);
		}

		// Mid
		{
			// H-Pane
			GtkWidget *hpane = gtk_hpaned_new();
			gtk_box_pack_start(GTK_BOX(vbox), hpane, TRUE, TRUE, 0);

			// Left pane
			{
				GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
				GtkWidget *event_box = gtk_event_box_new();

				// Event box should be on top
				gtk_event_box_set_above_child(GTK_EVENT_BOX(event_box), TRUE);

				gui_fixed_view = gtk_fixed_new();
				gtk_container_add(GTK_CONTAINER(event_box), gui_fixed_view);
				gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled_window), event_box);

				g_signal_connect (G_OBJECT (gui_fixed_view), "expose_event", G_CALLBACK (gui_PPU_portal_expose_event_callback), NULL);

				// We don't want the generator controls to be tabbed into
				gtk_container_set_focus_chain(GTK_CONTAINER(gui_fixed_view), NULL);

				g_signal_connect (event_box, "button-press-event", G_CALLBACK(gui_fixed_view_button_hander), NULL);

				gtk_paned_pack1(GTK_PANED (hpane), scrolled_window, TRUE, TRUE);
			}

			// Right pane
			gui_right_pane = gtk_vbox_new(FALSE, 6);
			gui_create_right_pane(gui_right_pane);
			gtk_paned_pack2(GTK_PANED (hpane), gui_right_pane, TRUE, TRUE);

			// FIXME: Should make this a fraction of the allocation
			gtk_paned_set_position(GTK_PANED(hpane), 600);
		}

		// Bottom
		{
			gui_status_bar = gtk_statusbar_new();
			gtk_box_pack_start(GTK_BOX(vbox), gui_status_bar, FALSE, TRUE, 0);
		}
	}

	// Show everything!!
	gtk_widget_show_all (gui_main_window);

	// Except the start spot controls
	gtk_widget_hide(gui_start_widgets.property_vbox);

	// And hide any Show-Ifs as appropriate
	gui_update_option_controls(gui_start_pos_props_context);

	// Preload global tile hints
	{
		int i;
		for(i=0; i < NoDice_config.game.tilehint_count; i++)
			gui_load_tile_hint(&NoDice_config.game.tilehints[i], TRUE);
	}


	return 1;
}


// Pulled this out into its own function to support the "New" popup
static void gui_do_load_complete_actions()
{
		gui_refesh_for_level();
		ppu_configure_for_level();
		gui_set_subtitle(NoDice_the_level.level->name);

		// If not cancelled, set back to generators/map tiles...
		gtk_notebook_set_current_page(GTK_NOTEBOOK(gui_notebook), (NoDice_the_level.tileset->id != 0) ? ENPAGE_GENS : ENPAGE_TILES);

		// Determine bytes free in bank
		gui_bank_free_space = NoDice_get_tilebank_free_space(NoDice_the_level.tileset->id);
		gui_level_base_size = gui_level_calc_size();
		gui_statusbar_update();
}


void gui_set_subtitle(const char *subtitle)
{
	snprintf(path_buffer, PATH_MAX, "NoDice - %s", subtitle);
	gtk_window_set_title (GTK_WINDOW (gui_main_window), path_buffer);
}


void gui_set_modepage(enum EDIT_NOTEBOOK_PAGES page)
{
	gtk_notebook_set_current_page(GTK_NOTEBOOK(gui_notebook), page);
}


void gui_disable_empty_objs(int is_empty)
{
	if(NoDice_the_level.tileset->id != 0)
	{
		if(is_empty)
		{
			gui_display_message(FALSE, "Level is using empty object set; object editing is disabled.  To add objects to this level, change to a new object set.");
			gtk_widget_set_sensitive(gtk_notebook_get_nth_page(GTK_NOTEBOOK(gui_notebook), ENPAGE_OBJS), FALSE);
		}
		else
			gtk_widget_set_sensitive(gtk_notebook_get_nth_page(GTK_NOTEBOOK(gui_notebook), ENPAGE_OBJS), TRUE);
	}
	else
	{
		// Disable object editing on warp zone map
		if(!strcmp(NoDice_the_level.level->layoutlabel, NoDice_config.game.options.warpzone))
		{
			gui_display_message(FALSE, "This is the defined Warp Zone map; object editing is disabled.  This setting is defined in game.xml.\n\nAlso, all map links only set destination worlds.");
			gtk_widget_set_sensitive(gtk_notebook_get_nth_page(GTK_NOTEBOOK(gui_notebook), ENPAGE_MOBJS), FALSE);
		}
		else
			gtk_widget_set_sensitive(gtk_notebook_get_nth_page(GTK_NOTEBOOK(gui_notebook), ENPAGE_MOBJS), TRUE);
	}
}

// May go away?
void gui_loop()
{
	gtk_main ();

	g_list_free_full(gui_start_pos_props_context, (GDestroyNotify)free);
}


// Compatibility with older GLib versions
#if !GLIB_CHECK_VERSION(2,28,0)
void g_list_free_full(GList *list, GDestroyNotify  free_func)
{
	g_list_foreach (list, (GFunc) free_func, NULL);
	g_list_free (list);
}
#endif


void gui_reboot()
{
	gtk_container_foreach(GTK_CONTAINER(gui_right_pane), gui_update_for_generators_foreach_callback, NULL);

	gui_create_right_pane(gui_right_pane);

	gtk_widget_show_all(gui_right_pane);
}
