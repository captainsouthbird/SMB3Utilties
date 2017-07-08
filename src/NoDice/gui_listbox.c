#include <gtk/gtk.h>
#include "NoDiceLib.h"
#include "NoDice.h"

/////////////////////////////////////////////////////////////////////////////////////
// Methods to support a pseudo "listbox" in GTK+ since it is otherwise pretty ugly :)
/////////////////////////////////////////////////////////////////////////////////////
GtkWidget *gui_listbox_new(GCallback sel_change_callback, gpointer user_data)
{
	GtkWidget *scroll_window;
	GtkWidget *list_view;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column1;
	GtkAdjustment *vscroll;
	GtkAdjustment *hscroll;
	GtkListStore *list_store;

	// Create a list store to hold a displayed string and internal index
	list_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);

	// create a scrollable container
	scroll_window = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_window),
							 GTK_POLICY_AUTOMATIC,
							 GTK_POLICY_ALWAYS);


	// create a tree view object
	list_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(list_store));
	gtk_container_add(GTK_CONTAINER(scroll_window), list_view);


	// set the renderer for the two columns we have and append the
	// columns to the tree view
	renderer = gtk_cell_renderer_text_new ();

	column1 = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW (list_view), column1);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW (list_view), FALSE);


	// Set vertical adjustment for the tree view
	vscroll = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll_window));
	gtk_tree_view_set_vadjustment(GTK_TREE_VIEW (list_view), vscroll);

	// Set vertical adjustment for the tree view
	hscroll = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scroll_window));
	gtk_tree_view_set_hadjustment(GTK_TREE_VIEW (list_view), hscroll);

	if(sel_change_callback != NULL)
		g_signal_connect (list_view, "cursor-changed", G_CALLBACK(sel_change_callback), user_data);

	return scroll_window;
}


void gui_listbox_additem(GtkListStore *list_store, int index, const char *item)
{
	GtkTreeIter list_iterator;
	gtk_list_store_append(GTK_LIST_STORE(list_store), &list_iterator);
	gtk_list_store_set(GTK_LIST_STORE(list_store), &list_iterator, 0, item, 1, index, -1);
}


// Returns the ListStore belonging to the virtual ListBox, disconnected so
// we can add a large number of items without excessive redraw
GtkListStore *gui_listbox_get_disconnected_list(GtkWidget *listbox)
{
	GtkWidget *view = NULL;
	GtkListStore *model;

	// Get the actual view out of the scrolled_window
	view = gtk_bin_get_child(GTK_BIN(listbox));

	// Hopefully we have returned with the tree view... now get the model
	// (which, in our virtual listbox, is a List Store)
	model = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(view)));

	// Make sure the model stays with us after the tree view unrefs it
	g_object_ref(model);

	// Detach model from view
	gtk_tree_view_set_model(GTK_TREE_VIEW(view), NULL);

	// Return the list store
	return model;
}


// Used to reconnect the list after done modifying it via gui_listbox_get_disconnected_list
void gui_listbox_reconnect_list(GtkWidget *listbox, GtkListStore *model)
{
	GtkWidget *view = NULL;

	// Get the actual view out of the scrolled_window
	view = gtk_bin_get_child(GTK_BIN(listbox));

	gtk_tree_view_set_model(GTK_TREE_VIEW(view), GTK_TREE_MODEL(model)); // Re-attach model to view

	g_object_unref(model);
}


int gui_listbox_get_index_by_view(GtkTreeView *view)
{
	int result;

	GtkTreeSelection *select = gtk_tree_view_get_selection(view);
	GtkTreeModel *model;
	GtkTreeIter iter;
	GValue value = { 0 };

	if(gtk_tree_selection_get_selected(select, &model, &iter))
	{
		gtk_tree_model_get_value(model, &iter, 1, &value);

		result = value.data->v_int;

		g_value_unset(&value);
	}
	else
		result = -1;

	return result;
}


int gui_listbox_get_index(GtkWidget *listbox)
{
	GtkWidget *view = NULL;

	// Get the actual view out of the scrolled_window
	view = gtk_bin_get_child(GTK_BIN(listbox));

	return gui_listbox_get_index_by_view(GTK_TREE_VIEW(view));
}


struct _gui_listbox_set_index_foreach_callback_data
{
	int target_index;
	GtkTreeView *view;
};

static gboolean gui_listbox_set_index_foreach_callback(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
	struct _gui_listbox_set_index_foreach_callback_data *tree_data = (struct _gui_listbox_set_index_foreach_callback_data *)data;
	int this_index;

	GValue value = { 0 };
	gtk_tree_model_get_value(model, iter, 1, &value);
	this_index = value.data->v_int;
	g_value_unset(&value);

	// If we've found the index we're looking for...
	if(this_index == tree_data->target_index)
	{
		// Then select it!
		GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_data->view);
		gtk_tree_selection_select_iter(selection, iter);

		// And we're done!
		return TRUE;
	}

	return FALSE;
}


static void gui_listbox_get_view_and_model(GtkWidget *listbox, GtkTreeView **view, GtkTreeModel **model)
{
	// Get the actual view out of the scrolled_window
	*view = GTK_TREE_VIEW(gtk_bin_get_child(GTK_BIN(listbox)));

	// Get the model
	*model = gtk_tree_view_get_model(*view);
}


static void gui_listbox_set_selection(GtkWidget *listbox, GtkTreeView *view, GtkTreeModel *model)
{
	// For appearance, we should scroll the list to this point
	GtkTreeIter iter;
	gboolean ret;	// Unused return signal

	gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view), &model, &iter);

	// Scroll to cell
	gtk_tree_view_scroll_to_cell(view, gtk_tree_model_get_path (model, &iter), NULL, FALSE, 0, 0);

	// Need to emit the change signal
	g_signal_emit_by_name(GTK_WIDGET(view), "cursor-changed", NULL, &ret);
}


void gui_listbox_set_first(GtkWidget *listbox)
{
	GtkTreeView *view;
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkTreeIter iter;


	// Get the view and model
	gui_listbox_get_view_and_model(listbox, &view, &model);

	// Get selection object
	selection = gtk_tree_view_get_selection(view);

	// Get iter
	gtk_tree_model_get_iter(model, &iter, gtk_tree_path_new_from_string ("0"));

	// Set selection
	gtk_tree_selection_select_iter(selection, &iter);

	// Set selection
	gui_listbox_set_selection(listbox, view, model);
}


void gui_listbox_set_index(GtkWidget *listbox, int index)
{
	GtkTreeView *view;
	GtkTreeModel *model;
	struct _gui_listbox_set_index_foreach_callback_data gui_listbox_set_index_foreach_callback_data;

	// Get the view and model
	gui_listbox_get_view_and_model(listbox, &view, &model);

	// Passed in data struct
	gui_listbox_set_index_foreach_callback_data.target_index = index;
	gui_listbox_set_index_foreach_callback_data.view = GTK_TREE_VIEW(view);

	// Walk tree until we find our item
	gtk_tree_model_foreach(model, gui_listbox_set_index_foreach_callback, (gpointer)&gui_listbox_set_index_foreach_callback_data);

	// Set selection
	gui_listbox_set_selection(listbox, view, model);
}
