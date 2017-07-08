#ifndef GUICTLS_H_INCLUDED
#define GUICTLS_H_INCLUDED

#include <gtk/gtk.h>
#include <cairo.h>

G_BEGIN_DECLS

#define GTK_SELECTABLE_OVERLAY(obj) GTK_CHECK_CAST(obj, gtk_selectable_overlay_get_type (), GtkSelectableOverlay)
#define GTK_SELECTABLE_OVERLAY_CLASS(klass) GTK_CHECK_CLASS_CAST(klass, gtk_selectable_overlay_get_type(), GtkSelectableOverlayClass)
#define GTK_IS_SELECTABLE_OVERLAY(obj) GTK_CHECK_TYPE(obj, gtk_selectable_overlay_get_type())


typedef struct _GtkSelectableOverlay GtkSelectableOverlay;
typedef struct _GtkSelectableOverlayClass GtkSelectableOverlayClass;


struct _GtkSelectableOverlay
{
	GtkWidget widget;
	void (* select_handler)(const GtkSelectableOverlay *selectable_overlay);

	struct NoDice_the_level_generator *gen;		// The generator this represents (if applicable)
	struct NoDice_the_level_object *obj;		// The object this represents (if applicable)
	struct NoDice_map_link *link;				// The map link this represents (if applicable)

	gboolean selected;						// This item is selected

	struct _translation_data
	{
		double mouse_origin_x, mouse_origin_y;		// Origin of mouse when dragging began
		int origin_x, origin_y;					// Origin X/Y
	} translation_data;		// Data used to translate a generator
};

struct _GtkSelectableOverlayClass {
  GtkWidgetClass parent_class;
};


GtkType gtk_selectable_overlay_get_type(void);
GtkWidget *gtk_selectable_overlay_new();
void gtk_selectable_overlay_select(GtkSelectableOverlay *selectable_overlay);

G_END_DECLS


// Pseudo-listbox
GtkWidget *gui_listbox_new(GCallback sel_change_callback, gpointer user_data);
void gui_listbox_additem(GtkListStore *list_store, int index, const char *item);
GtkListStore *gui_listbox_get_disconnected_list(GtkWidget *listbox);
void gui_listbox_reconnect_list(GtkWidget *listbox, GtkListStore *model);
int gui_listbox_get_index_by_view(GtkTreeView *view);
int gui_listbox_get_index(GtkWidget *listbox);
void gui_listbox_set_index(GtkWidget *listbox, int index);
void gui_listbox_set_first(GtkWidget *listbox);

// Simple combobox
GtkWidget *gui_combobox_simple_new();
void gui_combobox_simple_add_item(GtkWidget *widget, int index, const char *text);
void gui_combobox_simple_clear_items(GtkWidget *widget);
void gui_combobox_simple_set_selected(GtkWidget *widget, int index);
int gui_combobox_simple_get_index(GtkWidget *widget);

// Popups
enum OPEN_LEVEL_POPUP
{
	OLP_FORBROWSE = 1,			// Browsing for level, not intending to load
	OLP_NIBBLE_TILESETS_ONLY = 2,	// Only list levels that fit in a tileset 4-bits wide
};

int gui_new_level_popup();
int gui_open_level_popup(enum OPEN_LEVEL_POPUP popup_options, unsigned char *tileset, const struct NoDice_the_levels **level);
void gui_level_properties(struct NoDice_the_level_generator *selected_gen);
void gui_generate_option_controls(GList **context, GtkWidget *vbox, unsigned short header_byte, unsigned char header_val, const struct NoDice_headers *header, GCallback list_change, GCallback toggle_change, GCallback spin_change);
void gui_update_option_controls(GList *context);
int gui_special_obj_properties(struct NoDice_the_level_object *object);
int gui_map_obj_properties(struct NoDice_the_level_object *object);
int gui_map_link_properties(struct NoDice_map_link *link);

// Compatibility with older GLib versions
#if !GLIB_CHECK_VERSION(2,28,0)
void g_list_free_full(GList *list, GDestroyNotify  free_func);
#endif

#endif // GUICTLS_H_INCLUDED
