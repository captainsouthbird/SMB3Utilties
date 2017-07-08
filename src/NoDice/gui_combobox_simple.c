#include <gtk/gtk.h>
#include "NoDiceLib.h"
#include "NoDice.h"
#include "guictls.h"

// GTK 2.24 actually provides something like this, but I don't want to depend on that for now

GtkWidget *gui_combobox_simple_new()
{
	GtkWidget *combo;
	GtkListStore *combo_list;
	GtkCellRenderer *cell;

	// Create list store
	combo_list = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);

	// Create the tileset filter combobox
	combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(combo_list));

	// Unref so it destroys with combobox
	g_object_unref(G_OBJECT(combo_list));

	// Create cell renderer
	cell = gtk_cell_renderer_text_new();

	// Pack it to the combo box
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), cell, TRUE);

	// Connect renderer to data source
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), cell, "text", 0, NULL);

	return combo;
}


void gui_combobox_simple_add_item(GtkWidget *widget, int index, const char *text)
{
	GtkListStore *list_store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(widget)));
	GtkTreeIter iter;

	gtk_list_store_append(list_store, &iter);
	gtk_list_store_set(list_store, &iter, 0, text, 1, index, -1);
}


void gui_combobox_simple_clear_items(GtkWidget *widget)
{
	GtkListStore *list_store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(widget)));

	gtk_list_store_clear(list_store);
}


struct _gui_combobox_simple_set_selected_foreach_callback_data
{
	int target_index;
	GtkComboBox *combo;
};

static gboolean gui_combobox_simple_set_selected_foreach_callback(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
	struct _gui_combobox_simple_set_selected_foreach_callback_data *tree_data = (struct _gui_combobox_simple_set_selected_foreach_callback_data *)data;
	int this_index;

	GValue value = { 0 };
	gtk_tree_model_get_value(model, iter, 1, &value);
	this_index = value.data->v_int;
	g_value_unset(&value);

	// If we've found the index we're looking for...
	if(this_index == tree_data->target_index)
	{
		// Then select it!
		gtk_combo_box_set_active_iter(GTK_COMBO_BOX(tree_data->combo), iter);

		// And we're done!
		return TRUE;
	}

	return FALSE;
}


void gui_combobox_simple_set_selected(GtkWidget *widget, int index)
{
	GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(widget));
	struct _gui_combobox_simple_set_selected_foreach_callback_data gui_combobox_simple_set_selected_foreach_callback_data;

	// Passed in data struct
	gui_combobox_simple_set_selected_foreach_callback_data.target_index = index;
	gui_combobox_simple_set_selected_foreach_callback_data.combo = GTK_COMBO_BOX(widget);

	// Walk tree until we find our item
	gtk_tree_model_foreach(model, gui_combobox_simple_set_selected_foreach_callback, (gpointer)&gui_combobox_simple_set_selected_foreach_callback_data);
}


int gui_combobox_simple_get_index(GtkWidget *widget)
{
	int result;

	GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(widget));
	GtkTreeIter iter;
	GValue value = { 0 };

	gtk_combo_box_get_active_iter(GTK_COMBO_BOX(widget), &iter);
	gtk_tree_model_get_value(model, &iter, 1, &value);

	result = value.data->v_int;

	g_value_unset(&value);

	return result;
}
