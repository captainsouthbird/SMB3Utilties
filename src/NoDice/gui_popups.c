#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include "NoDiceLib.h"
#include "NoDice.h"
#include "guictls.h"

extern GtkWidget *gui_main_window;

void gui_display_6502_error(enum RUN6502_STOP_REASON reason)
{
	GtkWidget *dialog;
	const char *s3 = "";
	const char *revertReasons[] =
	{
		// Core triggered
		"Error??  6502 apparently still running!",	// RUN6502_STOP_NOTSTOPPED (shouldn't be used)
		"No apparent error",					// RUN6502_STOP_END (good day, shouldn't be used either)
		"Internal error",						// RUN6502_INIT_ERROR
		"Generator wrote out of tile memory bounds (low, top/left)",	// RUN6502_LEVEL_OORW_LOW
		"Generator wrote out of tile memory bounds (high, bottom/right)",	// RUN6502_LEVEL_OORW_HIGH
		"A generator appeared to be too large",		// RUN6502_GENERATOR_TOO_LARGE

		// Externally triggered
		"6502 core appears to have frozen (NOTE: May have to change config \"coretimeout\")",	// RUN6502_TIMEOUT
		"Did not return with the expected number of generators",	// RUN6502_GENGENCOUNT_MISMATCH
	};

	if(reason == RUN6502_INIT_ERROR)
		s3 = NoDice_Error();

	dialog = gtk_message_dialog_new (GTK_WINDOW(gui_main_window),
							   GTK_DIALOG_DESTROY_WITH_PARENT,
							   GTK_MESSAGE_ERROR,
							   GTK_BUTTONS_CLOSE,
							   "EDIT REVERTED: %s %s",
							   revertReasons[reason], s3);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy(GTK_WIDGET(dialog));
}


void gui_display_message(int is_error, const char *err_str)
{
	GtkWidget *dialog = gtk_message_dialog_new (GTK_WINDOW(gui_main_window),
							   GTK_DIALOG_DESTROY_WITH_PARENT,
							   is_error ? GTK_MESSAGE_ERROR : GTK_MESSAGE_INFO,
							   GTK_BUTTONS_CLOSE,
							   "%s", err_str);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy(GTK_WIDGET(dialog));
}


int gui_ask_question(const char *prompt)
{
	int result;

	GtkWidget *dialog = gtk_message_dialog_new (GTK_WINDOW(gui_main_window),
							   GTK_DIALOG_DESTROY_WITH_PARENT,
							   GTK_MESSAGE_QUESTION,
							   GTK_BUTTONS_YES_NO,
							   "%s", prompt);
	result = gtk_dialog_run (GTK_DIALOG (dialog));

	gtk_widget_destroy(GTK_WIDGET(dialog));

	return result == GTK_RESPONSE_YES;
}


// Widgets that require updates when the selected level changes
struct _gui_open_level_popup_desc_widgets
{
	GtkWidget *textbox_desc;		// Level description text box
	GtkWidget *label_tileset;	// Tileset
	GtkWidget *label_llayout;	// Level layout
	GtkWidget *label_olayout;	// Object layout
	GtkWidget *image_preview;	// Preview image

	unsigned char selected_level_tileset;
	const struct NoDice_the_levels *selected_level;
};

static gint gui_open_level_popup_sort(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data)
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


static void gui_open_level_popup_filter_change(GtkComboBox *widget, gpointer user_data)
{
	GtkWidget *level_list = (GtkWidget *)user_data;
	GtkListStore *store = gui_listbox_get_disconnected_list(level_list);
	int current, end, index = gui_combobox_simple_get_index(GTK_WIDGET(widget));

	// Clear all list items
	gtk_list_store_clear(store);

	if(index < 0)
	{
		current = 0;
		end = NoDice_config.game.tileset_count - 1;
	}
	else
	{
		current = index;
		end = index;
	}

	for( ; current <= end; current++)
	{
		const struct NoDice_tileset *t = &NoDice_config.game.tilesets[current];
		int i, base = (current << 16);

		if((index != -2) || (t->id <= 15))
		{
			for(i = 0; i < t->levels_count; i++)
				gui_listbox_additem(store, base | i, t->levels[i].name);
		}
	}

	{
		GtkTreeSortable *sortable = GTK_TREE_SORTABLE(store);

		gtk_tree_sortable_set_sort_func(sortable, 0, gui_open_level_popup_sort, NULL, NULL);

		gtk_tree_sortable_set_sort_column_id(sortable, 0, GTK_SORT_ASCENDING);
	}

	gui_listbox_reconnect_list(level_list, store);


	if(NoDice_the_level.tiles == NULL)
		gui_listbox_set_first(level_list);
	else
	{
		int tileset_index = NoDice_the_level.tileset - NoDice_config.game.tilesets;
		int level_index = NoDice_the_level.level - NoDice_the_level.tileset->levels;

		gui_listbox_set_index(level_list, (tileset_index << 16) | level_index);
	}

	if(gui_listbox_get_index(level_list) == -1)
		gui_listbox_set_first(level_list);
}


static gboolean gui_open_level_popup_change_level(GtkTreeView *treeview, gpointer user_data)
{
	struct _gui_open_level_popup_desc_widgets *desc_widgets = (struct _gui_open_level_popup_desc_widgets *)user_data;
	GtkTextIter start, end;
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(desc_widgets->textbox_desc));
	int index = gui_listbox_get_index_by_view(treeview);

	gtk_text_buffer_get_start_iter(buffer, &start);
	gtk_text_buffer_get_end_iter(buffer, &end);
	gtk_text_buffer_delete(buffer, &start, &end);

	if(index >= 0)
	{
		int tileset = index >> 16;
		int level = index & 0xFFFF;
		const struct NoDice_tileset *the_tileset = &NoDice_config.game.tilesets[tileset];
		const struct NoDice_the_levels *the_level = &the_tileset->levels[level];

		gtk_text_buffer_insert_at_cursor(buffer, the_level->desc, strlen(the_level->desc));

		gtk_label_set_text(GTK_LABEL(desc_widgets->label_tileset), the_tileset->name);
		gtk_label_set_text(GTK_LABEL(desc_widgets->label_llayout), the_level->layoutlabel);
		gtk_label_set_text(GTK_LABEL(desc_widgets->label_olayout), the_level->objectlabel);

		gtk_image_set_from_file(GTK_IMAGE(desc_widgets->image_preview), gui_make_image_path(the_tileset, the_level));

		desc_widgets->selected_level_tileset = the_tileset->id;
		desc_widgets->selected_level = the_level;
	}

	return TRUE;
}


int gui_open_level_popup(enum OPEN_LEVEL_POPUP popup_options, unsigned char *tileset, const struct NoDice_the_levels **level)
{
	int i, result = TRUE;
	GtkWidget *level_filter;
	GtkWidget *level_list;
	struct _gui_open_level_popup_desc_widgets gui_open_level_popup_desc_widgets;
	GtkAllocation alloc;

	GtkWidget *popup = gtk_dialog_new_with_buttons((popup_options & OLP_FORBROWSE) ? "Browse for Level" : "Open Level from ROM", GTK_WINDOW(gui_main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
          GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
		GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
		NULL);

	gui_open_level_popup_desc_widgets.textbox_desc = gtk_text_view_new();

	gtk_widget_get_allocation(GTK_WIDGET(gui_main_window), &alloc);
	gtk_widget_set_size_request(popup, (int)((double)alloc.width * 0.90), (int)((double)alloc.height * 0.90));

	// Tileset filter
	{
		level_filter = gui_combobox_simple_new();

		// Append the "<ALL>" filter
		// There's a -1 for "ALL" all
		// And there's a -2 for "ALL" in nibble mode (tilesets <= 15)
		gui_combobox_simple_add_item(level_filter, (popup_options & OLP_NIBBLE_TILESETS_ONLY) ? -2 : -1, "<ALL>");

		for(i=0; i < NoDice_config.game.tileset_count; i++)
		{
			struct NoDice_tileset *tileset = &NoDice_config.game.tilesets[i];

			// Filter out tilesets above 15 if OLP_NIBBLE_TILESETS_ONLY is set
			if( !(popup_options & OLP_NIBBLE_TILESETS_ONLY) || (tileset->id <= 15) )
				gui_combobox_simple_add_item(level_filter, i, tileset->name);
		}

		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), level_filter, FALSE, FALSE, 5);
	}

	{
		GtkWidget *hbox = gtk_hbox_new(FALSE, 0);

		// Left - List of levels
		{
			GtkWidget *frame = gtk_frame_new("Levels");

			gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(gui_open_level_popup_desc_widgets.textbox_desc), GTK_WRAP_WORD_CHAR);
			gtk_text_view_set_editable(GTK_TEXT_VIEW(gui_open_level_popup_desc_widgets.textbox_desc), FALSE);

			level_list = gui_listbox_new(G_CALLBACK(gui_open_level_popup_change_level), &gui_open_level_popup_desc_widgets);

			gtk_container_add (GTK_CONTAINER (frame), level_list);

			gtk_box_pack_start(GTK_BOX(hbox), frame, TRUE, TRUE, 5);
		}

		// Right - Info at a glance
		{
			GtkWidget *frame = gtk_frame_new("Level info");
			GtkWidget *vbox = gtk_vbox_new(FALSE, 0);

			gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("Tileset:"), FALSE, FALSE, 0);
			gui_open_level_popup_desc_widgets.label_tileset = gtk_label_new(NULL);
			gtk_box_pack_start(GTK_BOX(vbox), gui_open_level_popup_desc_widgets.label_tileset, FALSE, FALSE, 0);

			gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("Level Layout:"), FALSE, FALSE, 0);
			gui_open_level_popup_desc_widgets.label_llayout = gtk_label_new(NULL);
			gtk_box_pack_start(GTK_BOX(vbox), gui_open_level_popup_desc_widgets.label_llayout, FALSE, FALSE, 0);

			gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("Object Layout:"), FALSE, FALSE, 0);
			gui_open_level_popup_desc_widgets.label_olayout = gtk_label_new(NULL);
			gtk_box_pack_start(GTK_BOX(vbox), gui_open_level_popup_desc_widgets.label_olayout, FALSE, FALSE, 0);

			gui_open_level_popup_desc_widgets.image_preview = gtk_image_new();
			gtk_widget_set_size_request(gui_open_level_popup_desc_widgets.image_preview, 128, 128);
			gtk_box_pack_start(GTK_BOX(vbox), gui_open_level_popup_desc_widgets.image_preview, FALSE, FALSE, 0);

			gtk_container_add (GTK_CONTAINER (frame), vbox);
			gtk_box_pack_start(GTK_BOX(hbox), frame, FALSE, FALSE, 5);
		}

		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), hbox, TRUE, TRUE, 5);
	}

	// Connect change signal
	g_signal_connect(level_filter, "changed", G_CALLBACK(gui_open_level_popup_filter_change), (gpointer)level_list);

	// Select <ALL> by default
	gui_combobox_simple_set_selected(level_filter, (popup_options & OLP_NIBBLE_TILESETS_ONLY) ? -2 : -1);

	// Description box
	{
		GtkWidget *frame = gtk_frame_new("Description");

		gtk_container_add (GTK_CONTAINER (frame), gui_open_level_popup_desc_widgets.textbox_desc);

		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), frame, TRUE, TRUE, 5);
	}

	// Make sure all widgets show up
	gtk_widget_show_all(popup);

	// Run dialog...
	if(gtk_dialog_run (GTK_DIALOG (popup)) == GTK_RESPONSE_ACCEPT)
	{
		*tileset = gui_open_level_popup_desc_widgets.selected_level_tileset;
		*level = gui_open_level_popup_desc_widgets.selected_level;
	}
	else
		result = FALSE;

	gtk_widget_destroy (popup);

	return result;
}


static void gui_level_properties_option_list_change(GtkComboBox *widget, gpointer user_data)
{
	int header_ref_tag = GPOINTER_TO_INT(user_data);
	unsigned short header_byte = (header_ref_tag & 0xFFFF0000) >> 16;
	int option_list_index = (header_ref_tag & 0x0000FFFF);
	const struct NoDice_header_options *option_list = &NoDice_config.game.headers[header_byte].options_list[option_list_index];
	int relative_value = gui_combobox_simple_get_index(GTK_WIDGET(widget));

	// Mask out the old value in the header
	NoDice_the_level.header.option[header_byte] &= ~option_list->mask;

	// Set the new value
	NoDice_the_level.header.option[header_byte] |= (relative_value << option_list->shift) & option_list->mask;
}


static void gui_level_properties_option_toggle_change(GtkToggleButton *togglebutton, gpointer user_data)
{
	int header_ref_tag = GPOINTER_TO_INT(user_data);
	unsigned short header_byte = (header_ref_tag & 0xFFFF0000) >> 16;
	int option_list_index = (header_ref_tag & 0x0000FFFF);
	const struct NoDice_header_options *option_list = &NoDice_config.game.headers[header_byte].options_list[option_list_index];

	if(!gtk_toggle_button_get_active(togglebutton))
		// Mask out the old value in the header
		NoDice_the_level.header.option[header_byte] &= ~option_list->mask;
	else
		// Set the new value
		NoDice_the_level.header.option[header_byte] |= option_list->mask;
}


static void gui_level_properties_option_spin_change(GtkSpinButton *spinbutton, gpointer user_data)
{
	int header_ref_tag = GPOINTER_TO_INT(user_data);
	unsigned short header_byte = (header_ref_tag & 0xFFFF0000) >> 16;
	int option_list_index = (header_ref_tag & 0x0000FFFF);
	const struct NoDice_header_options *option_list = &NoDice_config.game.headers[header_byte].options_list[option_list_index];
	int relative_value = (int)gtk_spin_button_get_value(spinbutton);

	// Mask out the old value in the header
	NoDice_the_level.header.option[header_byte] &= ~option_list->mask;

	// Set the new value
	NoDice_the_level.header.option[header_byte] |= (relative_value << option_list->shift) & option_list->mask;
}


static void gui_level_properties_browse_click(GtkButton *button, gpointer user_data)
{
	GtkWidget *level_label = GTK_WIDGET(user_data);
	unsigned char tileset;
	const struct NoDice_the_levels *level;

	const struct NoDice_the_levels *backup_level_level = NoDice_the_level.level;
	const struct NoDice_tileset *backup_level_tileset = NoDice_the_level.tileset;

	// Temporarily switch out pointer so gui_open_level_popup shows the map link's level, not loaded level
	NoDice_the_level.level = edit_level_find(NoDice_the_level.header.alt_level_tileset, NoDice_the_level.header.alt_level_layout, NoDice_the_level.header.alt_level_objects);
	NoDice_the_level.tileset = &NoDice_config.game.tilesets[NoDice_the_level.header.alt_level_tileset];

	if(gui_open_level_popup(OLP_FORBROWSE | OLP_NIBBLE_TILESETS_ONLY, &tileset, &level))
	{
		int i;
		char buffer[256];
		const struct NoDice_tileset *the_tileset = NULL;

		for(i = 0; i < NoDice_config.game.tileset_count; i++)
		{
			if(NoDice_config.game.tilesets[i].id == tileset)
				the_tileset = &NoDice_config.game.tilesets[i];
		}

		NoDice_the_level.header.alt_level_layout = NoDice_get_addr_for_label(level->layoutlabel);
		NoDice_the_level.header.alt_level_objects = NoDice_get_addr_for_label(level->objectlabel);

		gtk_label_set(GTK_LABEL(level_label), level->name);

		snprintf(buffer, sizeof(buffer), "You MUST now change the 'Tileset of Alternate Level' in the property window to 'Tileset %i (%s)' or the game will crash!", the_tileset->id, the_tileset->name);
		gui_display_message(FALSE, buffer);
	}

	// Restore level pointer
	NoDice_the_level.level = backup_level_level;
	NoDice_the_level.tileset = backup_level_tileset;
}


void gui_level_properties(struct NoDice_the_level_generator *selected_gen)
{
	GList *property_context = NULL;
	GtkAllocation alloc;
	unsigned char option_backup[LEVEL_HEADER_COUNT];

	GtkWidget *popup = gtk_dialog_new_with_buttons("Level Properties", GTK_WINDOW(gui_main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
          GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
		GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
		NULL);

	gtk_widget_get_allocation(GTK_WIDGET(gui_main_window), &alloc);
	gtk_widget_set_size_request(popup, (int)((double)alloc.width * 0.90), (int)((double)alloc.height * 0.90));

	{
		int i;
		GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
		GtkWidget *vbox = gtk_vbox_new(FALSE, 6);

		const struct NoDice_the_levels *alt_level = edit_level_find(NoDice_the_level.header.alt_level_tileset, NoDice_the_level.header.alt_level_layout, NoDice_the_level.header.alt_level_objects);
		GtkWidget	*alt_level_hbox = gtk_hbox_new(FALSE, 6),
				*alt_level_browse = gtk_button_new_with_label("Select Alt Level"),
				*alt_level_label = gtk_label_new( (alt_level != NULL) ? alt_level->name : "<Undefined alternate level>" );
		g_signal_connect(G_OBJECT(alt_level_browse), "clicked", G_CALLBACK(gui_level_properties_browse_click), (gpointer)alt_level_label);
		gtk_box_pack_start(GTK_BOX(alt_level_hbox), gtk_label_new("Alternate Level:"), FALSE, FALSE, 5);
		gtk_box_pack_start(GTK_BOX(alt_level_hbox), alt_level_label, FALSE, FALSE, 5);
		gtk_box_pack_start(GTK_BOX(alt_level_hbox), alt_level_browse, FALSE, FALSE, 5);


		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), alt_level_hbox, FALSE, FALSE, 5);

		for(i=0; i<LEVEL_HEADER_COUNT; i++)
		{
			const struct NoDice_headers *header = &NoDice_config.game.headers[i];

			gui_generate_option_controls(&property_context, vbox, i, NoDice_the_level.header.option[i], header, G_CALLBACK(gui_level_properties_option_list_change), G_CALLBACK(gui_level_properties_option_toggle_change), G_CALLBACK(gui_level_properties_option_spin_change));
		}

		gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled_window), vbox);
		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), scrolled_window, TRUE, TRUE, 5);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
								 GTK_POLICY_AUTOMATIC,
								 GTK_POLICY_ALWAYS);
	}

	// Make sure all widgets show up
	gtk_widget_show_all(popup);

	// But hide any Show-Ifs as appropriate
	gui_update_option_controls(property_context);

	// Backup current header options in case user cancels
	memcpy(option_backup, NoDice_the_level.header.option, LEVEL_HEADER_COUNT);

	// Run dialog...
	if(gtk_dialog_run (GTK_DIALOG (popup)) == GTK_RESPONSE_ACCEPT)
	{
		// Reloading level... good luck!
		edit_header_change(selected_gen, option_backup);
	}
	else
		// User cancelled, restore backup
		memcpy(NoDice_the_level.header.option, option_backup, LEVEL_HEADER_COUNT);

	gtk_widget_destroy (popup);

	g_list_free_full(property_context, (GDestroyNotify)free);
}


static void gui_special_obj_properties_option_list_change(GtkComboBox *widget, gpointer user_data)
{
	int header_ref_tag = GPOINTER_TO_INT(user_data);
	unsigned short header_byte = (header_ref_tag & 0xFFFF0000) >> 16;
	int option_list_index = (header_ref_tag & 0x0000FFFF);
	int relative_value = gui_combobox_simple_get_index(GTK_WIDGET(widget));

	// Get the specific object (header_byte) and whether it is the
	// row or col we're effecting (bit 31 of header_ref_tag)
	struct NoDice_the_level_object *object = &NoDice_the_level.objects[header_byte];

	// Get the options related to this object
	const struct NoDice_header_options *option_list = &NoDice_config.game.objects[object->id].special_options.options_list[option_list_index];

	// Mask out the old value in the header
	object->row &= ~option_list->mask;

	// Set the new value
	object->row |= (relative_value << option_list->shift) & option_list->mask;
}


static void gui_special_obj_properties_option_toggle_change(GtkToggleButton *togglebutton, gpointer user_data)
{
	int header_ref_tag = GPOINTER_TO_INT(user_data);
	unsigned short header_byte = (header_ref_tag & 0xFFFF0000) >> 16;
	int option_list_index = (header_ref_tag & 0x0000FFFF);

	// Get the specific object (header_byte) and whether it is the
	// row or col we're effecting (bit 31 of header_ref_tag)
	struct NoDice_the_level_object *object = &NoDice_the_level.objects[header_byte];

	// Get the options related to this object
	const struct NoDice_header_options *option_list = &NoDice_config.game.objects[object->id].special_options.options_list[option_list_index];

	if(!gtk_toggle_button_get_active(togglebutton))
		// Mask out the old value in the header
		//NoDice_the_level.objects[header_byte]
		object->row &= ~option_list->mask;
	else
		// Set the new value
		object->row |= option_list->mask;
}


static void gui_special_obj_properties_option_spin_change(GtkSpinButton *spinbutton, gpointer user_data)
{
	int header_ref_tag = GPOINTER_TO_INT(user_data);
	unsigned short header_byte = (header_ref_tag & 0xFFFF0000) >> 16;
	int option_list_index = (header_ref_tag & 0x0000FFFF);
	int relative_value = (int)gtk_spin_button_get_value(spinbutton);

	// Get the specific object (header_byte) and whether it is the
	// row or col we're effecting (bit 31 of header_ref_tag)
	struct NoDice_the_level_object *object = &NoDice_the_level.objects[header_byte];

	// Get the options related to this object
	const struct NoDice_header_options *option_list = &NoDice_config.game.objects[object->id].special_options.options_list[option_list_index];

	// Mask out the old value in the header
	object->row &= ~option_list->mask;

	// Set the new value
	object->row |= (relative_value << option_list->shift) & option_list->mask;
}


struct gui_special_obj_properties_data
{
	int object_index;
	struct NoDice_the_level_object *object;
	GtkWidget *vbox;
	GList *property_context;
};


static void gui_special_obj_properties_id_change_foreach_callback(GtkWidget *widget, gpointer unused)
{
	gtk_widget_destroy(widget);
}


static void gui_special_obj_properties_id_change(GtkComboBox *widget, gpointer user_data)
{
	int object_id = gui_combobox_simple_get_index(GTK_WIDGET(widget));
	struct gui_special_obj_properties_data *data = (struct gui_special_obj_properties_data *)user_data;
	struct NoDice_the_level_object *object = &NoDice_the_level.objects[data->object_index];
	const struct NoDice_objects *this_obj = &NoDice_config.game.objects[object_id];

	// Update object ID
	data->object->id = object_id;

	// Destroy all child widgets from the fixed container
	gtk_container_foreach(GTK_CONTAINER(data->vbox), gui_special_obj_properties_id_change_foreach_callback, NULL);

	// Destroy old property box context
	g_list_free_full(data->property_context, (GDestroyNotify)free);
	data->property_context = NULL;

	// Create new controls fitting this special object
	gui_generate_option_controls(&data->property_context, data->vbox, (unsigned short)data->object_index, object->row, &this_obj->special_options, G_CALLBACK(gui_special_obj_properties_option_list_change), G_CALLBACK(gui_special_obj_properties_option_toggle_change), G_CALLBACK(gui_special_obj_properties_option_spin_change));

	// Show all controls...
	gtk_widget_show_all(data->vbox);

	// But hide any Show-Ifs as appropriate
	gui_update_option_controls(data->property_context);
}


int gui_special_obj_properties(struct NoDice_the_level_object *object)
{
	GtkAllocation alloc;
	GtkWidget *special_objs_list = gui_combobox_simple_new();
	int result;
	struct gui_special_obj_properties_data data;

	GtkWidget *popup = gtk_dialog_new_with_buttons("Special Object Properties", GTK_WINDOW(gui_main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
          GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
		GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
		NULL);

	data.property_context = NULL;

	gtk_widget_get_allocation(GTK_WIDGET(gui_main_window), &alloc);
	gtk_widget_set_size_request(popup, (int)((double)alloc.width * 0.75), (int)((double)alloc.width * 0.35));

	{
		int i, sel_index = -1;
		GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
		GtkWidget *vbox = gtk_vbox_new(FALSE, 6);

		// Add all available special objects
		for(i = 0; i < 256; i++)
		{
			struct NoDice_objects *this_obj = &NoDice_config.game.objects[i];

			if(this_obj->name != NULL && (this_obj->special_options.options_list_count != 0))
			{
				gui_combobox_simple_add_item(special_objs_list, i, this_obj->name);

				// In cases where this is a new special object, we need the first valid index
				if(sel_index == -1)
					sel_index = i;

				else if(object->id == i)
					sel_index = i;
			}
		}

		// Configure the data object
		data.object = object;
		data.object_index = (int)(object - NoDice_the_level.objects);
		data.vbox = vbox;

		// Connect change signal
		g_signal_connect(special_objs_list, "changed", G_CALLBACK(gui_special_obj_properties_id_change), (gpointer)&data);

		// Select the first or current object
		gui_combobox_simple_set_selected(special_objs_list, sel_index);


		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), special_objs_list, FALSE, FALSE, 5);

		gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled_window), vbox);
		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), scrolled_window, TRUE, TRUE, 5);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
								 GTK_POLICY_AUTOMATIC,
								 GTK_POLICY_ALWAYS);
	}

	// Make sure all widgets show up
	gtk_widget_show_all(popup);

	// But hide any Show-Ifs as appropriate
	gui_update_option_controls(data.property_context);

	// Run dialog...
	result = (gtk_dialog_run (GTK_DIALOG (popup)) == GTK_RESPONSE_ACCEPT);

	gtk_widget_destroy (popup);

	g_list_free_full(data.property_context, (GDestroyNotify)free);

	return result;
}


static void gui_map_obj_properties_id_change(GtkComboBox *widget, gpointer user_data)
{
	struct NoDice_the_level_object *the_object = (struct NoDice_the_level_object *)user_data;
	int object_index = gui_combobox_simple_get_index(GTK_WIDGET(widget));

	the_object->id = object_index;
}


static void gui_map_obj_properties_item_change(GtkComboBox *widget, gpointer user_data)
{
	unsigned char *the_item = (unsigned char *)user_data;
	*the_item = gui_combobox_simple_get_index(GTK_WIDGET(widget));
}


int gui_map_obj_properties(struct NoDice_the_level_object *object)
{
	GtkAllocation alloc;
	GtkWidget *map_objs_list = gui_combobox_simple_new();
	GtkWidget *map_items_list = gui_combobox_simple_new();
	int result;

	GtkWidget *popup = gtk_dialog_new_with_buttons("Change Map Object", GTK_WINDOW(gui_main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
          GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
		GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
		NULL);

	gtk_widget_get_allocation(GTK_WIDGET(gui_main_window), &alloc);
	//gtk_widget_set_size_request(popup, (int)((double)alloc.width * 0.75), (int)((double)alloc.width * 0.35));

	{
		int i, sel_index = -1, obj_index = object - NoDice_the_level.objects;

		// Add all available map objects
		for(i = 0; i < 256; i++)
		{
			struct NoDice_objects *this_obj = &NoDice_config.game.map_objects[i];

			if(this_obj->name != NULL)
			{
				gui_combobox_simple_add_item(map_objs_list, i, this_obj->name);

				// In cases where this is a new special object, we need the first valid index
				if(sel_index == -1)
					sel_index = i;

				else if(object->id == i)
					sel_index = i;
			}
		}


		// Select the current object
		gui_combobox_simple_set_selected(map_objs_list, sel_index);

		// Connect change signal
		g_signal_connect(map_objs_list, "changed", G_CALLBACK(gui_map_obj_properties_id_change), object);

		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), map_objs_list, FALSE, FALSE, 5);


		// Add all available map items
		for(i = 0; i < NoDice_config.game.total_map_object_items; i++)
		{
			struct NoDice_map_object_item *item = &NoDice_config.game.map_object_items[i];
			gui_combobox_simple_add_item(map_items_list, item->id, item->name);
		}

		// Select the current object
		gui_combobox_simple_set_selected(map_items_list, NoDice_the_level.map_object_items[obj_index]);

		// Connect change signal
		g_signal_connect(map_items_list, "changed", G_CALLBACK(gui_map_obj_properties_item_change), &NoDice_the_level.map_object_items[obj_index]);

		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), map_items_list, FALSE, FALSE, 5);
	}

	// Make sure all widgets show up
	gtk_widget_show_all(popup);


	// Run dialog...
	result = (gtk_dialog_run (GTK_DIALOG (popup)) == GTK_RESPONSE_ACCEPT);

	gtk_widget_destroy (popup);

	return result;
}



// Option panel for special-tile map links is a little complicated
// because there's no 1:1 relationship...
//
// Need to pack index of map link, low/high
// Also link back to the original special tile def
// This is what makes up the defined limits for MAX_MAP_LINKS and
// MAX_SPECIAL_TILES, basically from digging myself into a hole...
enum OVERRIDE_TYPE
{
	// Map Link Index Override Tile / Object
	MLIO_TILEL = 0,
	MLIO_TILEH = 1,
	MLIO_OBJL = 2,
	MLIO_OBJH = 3
};

#define MAPLINKIDX_PACK(ovrtype, special_tile_def_index, map_link_index) (((ovrtype) << 14) | (((special_tile_def_index) & 0x003F) << 8) | ((map_link_index) & 0x00FF))

#define MAPLINK_UNPACK_OVRTYPE(x) 		(((x) & 0xC000) >> 14)
#define MAPLINK_UNPACK_SPECTILE_IDX(x)	(((x) & 0x3F00) >> 8)
#define MAPLINK_UNPACK_MAPLINK_IDX(x)	(((x) & 0x00FF) >> 0)

static const struct NoDice_header_options *opt_list_for_map_link(int option_list_index, unsigned short maplink_spectile, unsigned short maplink_ovrtype)
{
	const struct NoDice_map_special_tile *spec_tile = &NoDice_config.game.map_special_tiles[maplink_spectile];

	switch(maplink_ovrtype)
	{
	case MLIO_TILEL:
		return &spec_tile->override_tile.low.options_list[option_list_index];

	case MLIO_TILEH:
		return &spec_tile->override_tile.high.options_list[option_list_index];

	case MLIO_OBJL:
		return &spec_tile->override_object.low.options_list[option_list_index];

	case MLIO_OBJH:
		return &spec_tile->override_object.high.options_list[option_list_index];
	}

	// Should never get here
	return NULL;
}


static void map_link_change_by_opt_list(const struct NoDice_header_options *option_list, unsigned short maplink_ovrtype, unsigned short maplink_index, int relative_value)
{
	int is_high = ((maplink_ovrtype == MLIO_TILEH) || (maplink_ovrtype == MLIO_OBJH));
	unsigned short *addr;
	unsigned char this_val;

	if( (maplink_ovrtype == MLIO_TILEL) || (maplink_ovrtype == MLIO_TILEH) )
		addr = &NoDice_the_level.map_links[maplink_index].layout_addr;
	else
		addr = &NoDice_the_level.map_links[maplink_index].object_addr;

	this_val = is_high ? ((*addr) >> 8) : ((*addr) & 0x00FF);

	// Mask out the old value in the header
	this_val &= ~option_list->mask;

	// Set the new value
	this_val |= (relative_value << option_list->shift) & option_list->mask;

	if(is_high)
		// High
		*addr = ((*addr) & 0x00FF) | (this_val << 8);
	else
		// Low
		*addr = ((*addr) & 0xFF00) | this_val;
}


static void gui_map_links_option_list_change(GtkComboBox *widget, gpointer user_data)
{
	// header_byte selects the option set
	// option_list_index selects the specific option
	int header_ref_tag = GPOINTER_TO_INT(user_data);
	unsigned short header_byte = (header_ref_tag & 0xFFFF0000) >> 16;
	int option_list_index = (header_ref_tag & 0x0000FFFF);

	unsigned short	maplink_ovrtype = MAPLINK_UNPACK_OVRTYPE(header_byte),
				maplink_spectile = MAPLINK_UNPACK_SPECTILE_IDX(header_byte),
				maplink_index = MAPLINK_UNPACK_MAPLINK_IDX(header_byte);

	const struct NoDice_header_options *option_list =
		opt_list_for_map_link(option_list_index, maplink_spectile, maplink_ovrtype);

	int relative_value = gui_combobox_simple_get_index(GTK_WIDGET(widget));

	map_link_change_by_opt_list(option_list, maplink_ovrtype, maplink_index, relative_value);
}


static void gui_map_links_option_toggle_change(GtkToggleButton *togglebutton, gpointer user_data)
{
	int header_ref_tag = GPOINTER_TO_INT(user_data);
	unsigned short header_byte = (header_ref_tag & 0xFFFF0000) >> 16;
	int option_list_index = (header_ref_tag & 0x0000FFFF);

	unsigned short	maplink_ovrtype = MAPLINK_UNPACK_OVRTYPE(header_byte),
				maplink_spectile = MAPLINK_UNPACK_SPECTILE_IDX(header_byte),
				maplink_index = MAPLINK_UNPACK_MAPLINK_IDX(header_byte);

	const struct NoDice_header_options *option_list =
		opt_list_for_map_link(option_list_index, maplink_spectile, maplink_ovrtype);

	int relative_value = gtk_toggle_button_get_active(togglebutton);

	map_link_change_by_opt_list(option_list, maplink_ovrtype, maplink_index, relative_value);
}


static void gui_map_links_option_spin_change(GtkSpinButton *spinbutton, gpointer user_data)
{
	int header_ref_tag = GPOINTER_TO_INT(user_data);
	unsigned short header_byte = (header_ref_tag & 0xFFFF0000) >> 16;
	int option_list_index = (header_ref_tag & 0x0000FFFF);

	unsigned short	maplink_ovrtype = MAPLINK_UNPACK_OVRTYPE(header_byte),
				maplink_spectile = MAPLINK_UNPACK_SPECTILE_IDX(header_byte),
				maplink_index = MAPLINK_UNPACK_MAPLINK_IDX(header_byte);

	const struct NoDice_header_options *option_list =
		opt_list_for_map_link(option_list_index, maplink_spectile, maplink_ovrtype);


	int relative_value = (int)gtk_spin_button_get_value(spinbutton);

	map_link_change_by_opt_list(option_list, maplink_ovrtype, maplink_index, relative_value);
}


struct _gui_map_links_browse_click_data
{
	GtkWidget *level_label;
	struct NoDice_map_link *map_link;
	int is_special;
};


static void gui_map_links_browse_click(GtkButton *button, gpointer user_data)
{
	struct _gui_map_links_browse_click_data *data = (struct _gui_map_links_browse_click_data *)user_data;
	unsigned char tileset;
	const struct NoDice_the_levels *level;

	unsigned char browse_tileset = data->map_link->row_tileset & 0x0F;
	const struct NoDice_the_levels *backup_level_level = NoDice_the_level.level;
	const struct NoDice_tileset *backup_level_tileset = NoDice_the_level.tileset;

	// Temporarily switch out pointer so gui_open_level_popup shows the map link's level, not loaded level
	NoDice_the_level.level = edit_level_find(browse_tileset, data->map_link->layout_addr, data->map_link->object_addr);
	NoDice_the_level.tileset = &NoDice_config.game.tilesets[browse_tileset];

	if(gui_open_level_popup(OLP_FORBROWSE | OLP_NIBBLE_TILESETS_ONLY, &tileset, &level))
	{
		int i;
		const struct NoDice_tileset *the_tileset = NULL;

		for(i = 0; i < NoDice_config.game.tileset_count; i++)
		{
			if(NoDice_config.game.tilesets[i].id == tileset)
				the_tileset = &NoDice_config.game.tilesets[i];
		}

		data->map_link->row_tileset = (data->map_link->row_tileset & 0xF0) | the_tileset->id;
		data->map_link->layout_addr = NoDice_get_addr_for_label(level->layoutlabel);

		// Only set the object address if this is not a special tile
		if(!data->is_special)
			data->map_link->object_addr = NoDice_get_addr_for_label(level->objectlabel);

		gtk_label_set(GTK_LABEL(data->level_label), level->name);
	}

	// Restore level pointer
	NoDice_the_level.level = backup_level_level;
	NoDice_the_level.tileset = backup_level_tileset;
}


static void gui_map_warpzone_spin_val_change(GtkSpinButton *spinbutton, gpointer user_data)
{
	struct NoDice_map_link *link = (struct NoDice_map_link *)user_data;

	link->row_tileset = (link->row_tileset & 0xF0) | (((int)gtk_spin_button_get_value(spinbutton) - 1) & 0x0F);
}


int gui_map_link_properties(struct NoDice_map_link *link)
{
	int i, result;

	// Get the tile that this map link is sitting on to handle special tiles
	unsigned char tile_id = edit_maptile_get( ((link->row_tileset & 0xF0) >> 4) - MAP_OBJECT_BASE_ROW, link->col_hi);
	struct NoDice_map_special_tile *special_tile = NULL;

	GList *property_context = NULL;
	GtkAllocation alloc;

	GtkWidget *popup = gtk_dialog_new_with_buttons("Map Link Properties", GTK_WINDOW(gui_main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
          GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
		GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
		NULL);

	gtk_widget_get_allocation(GTK_WIDGET(gui_main_window), &alloc);
	gtk_widget_set_size_request(popup, (int)((double)alloc.width * 0.80), (int)((double)alloc.height * 0.40));

	// Warp Zone links only specify worlds
	if(strcmp(NoDice_the_level.level->layoutlabel, NoDice_config.game.options.warpzone))
	{
		// Check if this is one of the special tiles; if so, set up special properties
		for(i = 0; i < NoDice_config.game.total_map_special_tiles; i++)
		{
			struct NoDice_map_special_tile *this_special_tile = &NoDice_config.game.map_special_tiles[i];
			if(this_special_tile->id == tile_id)
			{
				special_tile = this_special_tile;
				break;
			}
		}

		{
			GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
			GtkWidget *vbox = gtk_vbox_new(FALSE, 6);

			// Only show "Target Level:" if it is not overridden
			if(	(special_tile == NULL) || (
				(special_tile->override_tile.low.options_list_count == 0) &&
				(special_tile->override_tile.high.options_list_count == 0) )
				)
			{
				const struct NoDice_the_levels *target_level = edit_level_find(link->row_tileset & 0x0F, link->layout_addr, (special_tile == NULL) ? link->object_addr : 0xFFFF);
				GtkWidget	*target_level_hbox = gtk_hbox_new(FALSE, 6),
						*target_level_browse = gtk_button_new_with_label("Select Target Level"),
						*target_level_label = gtk_label_new( (target_level != NULL) ? target_level->name : "<Undefined target level>" );
				struct _gui_map_links_browse_click_data gui_map_links_browse_click_data = { target_level_label, link, special_tile != NULL };

				g_signal_connect(G_OBJECT(target_level_browse), "clicked", G_CALLBACK(gui_map_links_browse_click), (gpointer)&gui_map_links_browse_click_data);
				gtk_box_pack_start(GTK_BOX(target_level_hbox), gtk_label_new("Target Level:"), FALSE, FALSE, 5);
				gtk_box_pack_start(GTK_BOX(target_level_hbox), target_level_label, FALSE, FALSE, 5);
				gtk_box_pack_start(GTK_BOX(target_level_hbox), target_level_browse, FALSE, FALSE, 5);

				gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), target_level_hbox, FALSE, FALSE, 5);
			}
			else
			{
				// If there is no "Target Level" because the "tilelayout" is specified,
				// force the "tileset" value to whatever the config specified it to be...
				link->row_tileset = (link->row_tileset & 0xF0) | (special_tile->tileset & 0x0F);
			}

			if(special_tile != NULL)
			{
				int index = link - NoDice_the_level.map_links;

				if(special_tile->override_tile.low.options_list_count > 0)
					gui_generate_option_controls(&property_context, vbox, MAPLINKIDX_PACK(MLIO_TILEL, i, index), (link->layout_addr & 0x00FF), &special_tile->override_tile.low, G_CALLBACK(gui_map_links_option_list_change), G_CALLBACK(gui_map_links_option_toggle_change), G_CALLBACK(gui_map_links_option_spin_change));

				if(special_tile->override_tile.high.options_list_count > 0)
					gui_generate_option_controls(&property_context, vbox, MAPLINKIDX_PACK(MLIO_TILEH, i, index), (link->layout_addr & 0xFF00) >> 8, &special_tile->override_tile.high, G_CALLBACK(gui_map_links_option_list_change), G_CALLBACK(gui_map_links_option_toggle_change), G_CALLBACK(gui_map_links_option_spin_change));

				if(special_tile->override_object.low.options_list_count > 0)
					gui_generate_option_controls(&property_context, vbox, MAPLINKIDX_PACK(MLIO_OBJL, i, index), (link->object_addr & 0x00FF), &special_tile->override_object.low, G_CALLBACK(gui_map_links_option_list_change), G_CALLBACK(gui_map_links_option_toggle_change), G_CALLBACK(gui_map_links_option_spin_change));

				if(special_tile->override_object.high.options_list_count > 0)
					gui_generate_option_controls(&property_context, vbox, MAPLINKIDX_PACK(MLIO_OBJH, i, index), (link->object_addr & 0xFF00) >> 8, &special_tile->override_object.high, G_CALLBACK(gui_map_links_option_list_change), G_CALLBACK(gui_map_links_option_toggle_change), G_CALLBACK(gui_map_links_option_spin_change));

				gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled_window), vbox);
				gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), scrolled_window, TRUE, TRUE, 5);
				gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
										 GTK_POLICY_AUTOMATIC,
										 GTK_POLICY_ALWAYS);
			}
		}
	}
	else
	{
		GtkWidget *hbox = gtk_hbox_new(FALSE, 0);
		GtkWidget *world_spin;

		// Label
		gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("Destination World:"), FALSE, FALSE, 5);

		// Warp Zone links only specify the destination world as the "tileset"
		world_spin = gtk_spin_button_new_with_range(1, 16, 1);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(world_spin), (link->row_tileset & 0x0F) + 1);
		g_signal_connect (G_OBJECT (world_spin), "value-changed", G_CALLBACK (gui_map_warpzone_spin_val_change), (gpointer)link);
		gtk_box_pack_start(GTK_BOX(hbox), world_spin, FALSE, FALSE, 5);

		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), hbox, FALSE, FALSE, 5);
	}

	// Make sure all widgets show up
	gtk_widget_show_all(popup);

	// But hide any Show-Ifs as appropriate
	gui_update_option_controls(property_context);

	result = (gtk_dialog_run (GTK_DIALOG (popup)) == GTK_RESPONSE_ACCEPT);

	gtk_widget_destroy (popup);

	g_list_free_full(property_context, (GDestroyNotify)free);

	return result;
}


// The two comboboxes, so we can change their contents by tileset
static struct _gui_new_level_tileset_change_data
{
	GtkWidget *layouts;	// simple combobox holding layouts
	GtkWidget *objects;	// simple combobox holding objects
} layout_object_cbs;


// Provides the two related entry blanks for setting and enabling/disabling
static struct _gui_new_level_existing_lo_change_data
{
	GtkWidget *filename;	// File to include for data
	GtkWidget *labelname;	// Label to define for data

	const struct label_file_pair *pairs;
} level_layout, level_object;


// Label and file pairs for selection and setting of the entry blanks
static struct label_file_pair
{
	const char *label;		// Fill in the blank
	const char *file;		// Fill in the blank
	char label_file[128];	// For display purposes
} *layout_pairs = NULL, *object_pairs = NULL;


static void gui_new_level_tileset_change(GtkComboBox *widget, gpointer user_data)
{
	int tset = gui_combobox_simple_get_index(GTK_WIDGET(widget));
	const struct NoDice_tileset *tileset = &NoDice_config.game.tilesets[tset];

	int level;

	// Clear any old layouts
	gui_combobox_simple_clear_items(layout_object_cbs.layouts);
	gui_combobox_simple_clear_items(layout_object_cbs.objects);

	if(layout_pairs != NULL)
		free(layout_pairs);

	if(object_pairs != NULL)
		free(object_pairs);

	// Allocate enough pairs to support this
	layout_pairs = (struct label_file_pair *)malloc(sizeof(struct label_file_pair) * tileset->levels_count);
	level_layout.pairs = layout_pairs;
	object_pairs = (struct label_file_pair *)malloc(sizeof(struct label_file_pair) * tileset->levels_count);
	level_object.pairs = object_pairs;

	// Add "-- NEW --" default
	gui_combobox_simple_add_item(layout_object_cbs.layouts, -1, "-- NEW --");
	gui_combobox_simple_set_selected(layout_object_cbs.layouts, -1);
	gui_combobox_simple_add_item(layout_object_cbs.objects, -1, "-- NEW --");
	gui_combobox_simple_set_selected(layout_object_cbs.objects, -1);

	// Generate pairs...
	for(level = 0; level < tileset->levels_count; level++)
	{
		const struct NoDice_the_levels *this_level = &tileset->levels[level];
		struct label_file_pair *lfp_layout = &layout_pairs[level];
		struct label_file_pair *lfp_object = &object_pairs[level];

		// For this pair...
		lfp_layout->label = this_level->layoutlabel;
		lfp_layout->file = this_level->layoutfile;
		snprintf(lfp_layout->label_file, sizeof(lfp_layout->label_file)-1, "%s / %s (%s)", lfp_layout->label, lfp_layout->file, this_level->name);

		lfp_object->label = this_level->objectlabel;
		lfp_object->file = this_level->objectfile;
		snprintf(lfp_object->label_file, sizeof(lfp_object->label_file)-1, "%s / %s (%s)", lfp_object->label, lfp_object->file, this_level->name);

		// Add to combobox as well
		gui_combobox_simple_add_item(layout_object_cbs.layouts, level, lfp_layout->label_file);
		gui_combobox_simple_add_item(layout_object_cbs.objects, level, lfp_object->label_file);
	}
}


static void gui_new_level_existing_lo_change(GtkComboBox *widget, gpointer user_data)
{
	const struct _gui_new_level_existing_lo_change_data *data = (const struct _gui_new_level_existing_lo_change_data *)user_data;
	const struct label_file_pair *pairs = data->pairs;

	// Basically this is to just disable the entry fields if you select an existing layout/object set
	int index = gui_combobox_simple_get_index(GTK_WIDGET(widget));

	gtk_widget_set_sensitive(data->filename, index == -1);
	gtk_widget_set_sensitive(data->labelname, index == -1);

	if(index >= 0)
	{
		gtk_entry_set_text(GTK_ENTRY(data->filename), pairs[index].file); 
		gtk_entry_set_text(GTK_ENTRY(data->labelname), pairs[index].label); 
	}
}


static int gui_new_level_validate_name(const char *label, const char *name, int for_label)
{
	char buffer[256];
	int i, name_ok = 1;

	// Must contain SOMETHING
	if(strlen(name) < 1)
	{
		snprintf(buffer, sizeof(buffer), "%s must be specified", label);
		gui_display_message(TRUE, buffer);

		name_ok = FALSE;
	}
	else
	{
		for(i = 0; name[i] != '\0'; i++)
		{
			// For labels: Do not allow starting with a digit character
			if(i == 0 && for_label && (name[i] >= '0' && name[i] <= '9'))
			{
				snprintf(buffer, sizeof(buffer), "%s cannot start with a digit (0 to 9)", label);
				gui_display_message(TRUE, buffer);

				name_ok = FALSE;
				break;
			}
			else
			{
				// Check if valid characters are used
				name_ok = 
					(name[i] >= '0' && name[i] <= '9') ||
					(name[i] >= 'A' && name[i] <= 'Z') ||
					(name[i] >= 'a' && name[i] <= 'z') ||
					(name[i] == '_');

				if(!name_ok)
				{
					if(for_label)
					{
						snprintf(buffer, sizeof(buffer), "%s contains invalid characters; you may only use alphanumeric characters and underscores", label);
						gui_display_message(TRUE, buffer);
						break;
					}
					else
					{
						int j;

						// Filenames allow a few more... list characters we really can't allow
						const char invalid_fchars[] = "\\ / * ? \" < > |   ";	// NOTE: Space [and space] at the end; spaces can be bad in filenames!

						// Assume we're actually GOOD until we're not
						name_ok = TRUE;

						// Deliberately spaced for display purposes, so j += 2
						for(j = 0; (invalid_fchars[j] != '\0') && name_ok; j += 2)
						{
							if(name[i] == invalid_fchars[j])
								name_ok = FALSE;
						}

						if(!name_ok)
						{
							snprintf(buffer, sizeof(buffer), "%s contains invalid characters; you cannot use spaces or any of the following:\n%s", label, invalid_fchars);
							gui_display_message(TRUE, buffer);
							break;
						}
					}
				}
			}

		}
	}

	return name_ok;
}


// Check to see if a label exists in the given ASM file
// Returns 1 if found, 0 if not found, -1 if error
static int gui_new_level_check_label_exists(const char *filename, const char *label)
{
	int label_found = -1;
	char buffer[256];
	FILE *f = fopen(filename, "r");

	if(f != NULL)
	{
		// At least there wasn't an error...
		label_found = 0;

		// Valid ASM labels only exist in the beginning of the line
		// They also must start with A-Za-z
		// Otherwise we ignore this line
		// May end in a colon or whitespace
		while(!feof(f))
		{
			// Get this line
			fgets(buffer, sizeof(buffer), f);

			// Line must START A-Za-z
			if( (buffer[0] >= 'A' && buffer[0] <= 'Z') || (buffer[0] >= 'a' && buffer[0] <= 'z') )
			{
				// Cut off line at whitespace, colon, or newline
				const char *label_check = strtok(buffer, ": \t\n");

				// Now check!
				if(strcmp(label, label_check) == 0)
				{
					// Found a matching label in this file...
					label_found = 1;
					break;
				}
			}
		}

		fclose(f);
	}
	else
	{
		snprintf(buffer, sizeof(buffer), "Verification check failed; couldn't open %s", filename);
		gui_display_message(TRUE, buffer);
	}

	return label_found;
}


// Returns a level by name from configuration
static const struct NoDice_the_levels *gui_new_level_get_level_by_name(const char *name)
{
	const struct NoDice_the_levels *the_level = NULL;

	int i;
	for(i = 0; i < NoDice_config.game.tileset_count && !the_level; i++)
	{
		int t;
		const struct NoDice_tileset *tileset = &NoDice_config.game.tilesets[i];
		for(t = 0; t < tileset->levels_count && !the_level; t++)
		{
			const struct NoDice_the_levels *level = &tileset->levels[t];
			if(!strcmp(level->name, name))
				the_level = level;
		}
	}

	return the_level;
}


// Check to see if a level name is already in use
static int gui_new_level_check_name_exists(const char *name)
{
	return gui_new_level_get_level_by_name(name) != NULL;
}


int gui_new_level_popup()
{
	int tset;
	GtkWidget *cb_tilesets = gui_combobox_simple_new();
	GtkWidget	*level_name_entry = gtk_entry_new_with_max_length(250), 
				*level_desc = gtk_entry_new();

	int result = 0;

	GtkWidget *popup = gtk_dialog_new_with_buttons("New Level", GTK_WINDOW(gui_main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
          GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
		GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
		NULL);


	//gtk_widget_get_allocation(GTK_WIDGET(gui_main_window), &alloc);
	//gtk_widget_set_size_request(popup, (int)((double)alloc.width * 0.75), (int)((double)alloc.width * 0.35));

	// Layout container...
	{
		GtkWidget *table = gtk_table_new(9, 2, FALSE);

		// Friendly warning
		GtkWidget *warning_label = gtk_label_new(NULL);
		gtk_label_set_markup(GTK_LABEL(warning_label), "<b>** WARNING **</b> This will close the level you are currently editing!");
		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), warning_label, FALSE, FALSE, 5);

		warning_label = gtk_label_new(NULL);
		gtk_label_set_markup(GTK_LABEL(warning_label), "<b>Cancel and save</b> if you have any edits you want to keep!");
		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), warning_label, FALSE, FALSE, 5);

		warning_label = gtk_label_new(NULL);
		gtk_label_set_markup(GTK_LABEL(warning_label), "NOTE: This currently does <b>not support world maps</b>");
		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), warning_label, FALSE, FALSE, 5);

		// LEVEL NAME
		gtk_table_attach_defaults(GTK_TABLE(table), gtk_label_new("Level Name:"), 0, 1, 0, 1);
		gtk_table_attach_defaults(GTK_TABLE(table), level_name_entry, 1, 2, 0, 1);

		// LEVEL DESCRIPTION
		gtk_table_attach_defaults(GTK_TABLE(table), gtk_label_new("Level Description:"), 0, 1, 1, 2);
		gtk_table_attach_defaults(GTK_TABLE(table), level_desc, 1, 2, 1, 2);

		// LEVEL TILESET
		gtk_table_attach_defaults(GTK_TABLE(table), gtk_label_new("Level Tileset:"), 0, 1, 2, 3);

		// These need to be allocated for gui_new_level_tileset_change
		layout_object_cbs.layouts = gui_combobox_simple_new();
		layout_object_cbs.objects = gui_combobox_simple_new();

		// Connect change signal
		g_signal_connect(cb_tilesets, "changed", G_CALLBACK(gui_new_level_tileset_change), NULL);

		{
			gboolean cb_tilesets_is_set = FALSE;
			for(tset = 0; tset < NoDice_config.game.tileset_count; tset++)
			{
				const struct NoDice_tileset *tileset = &NoDice_config.game.tilesets[tset];

				// NOTE: Not supporting tileset = 0 (World Map)
				if(tileset->id != 0)
				{
					gui_combobox_simple_add_item(cb_tilesets, tset, tileset->name);

					if(!cb_tilesets_is_set)
					{
						gui_combobox_simple_set_selected(cb_tilesets, tset);
						cb_tilesets_is_set = TRUE;
					}
				}
			}
		}

		gtk_table_attach_defaults(GTK_TABLE(table), cb_tilesets, 1, 2, 2, 3);
		gtk_table_set_row_spacing(GTK_TABLE(table), 2, 20);

		// LEVEL LAYOUT
		level_layout.filename = gtk_entry_new_with_max_length(32);
		level_layout.labelname = gtk_entry_new_with_max_length(20); 

		// Connect change signal
		g_signal_connect(layout_object_cbs.layouts, "changed", G_CALLBACK(gui_new_level_existing_lo_change), (gpointer)&level_layout);

		gtk_table_attach_defaults(GTK_TABLE(table), gtk_label_new("Existing Layout:"), 0, 1, 3, 4);
		gtk_table_attach_defaults(GTK_TABLE(table), layout_object_cbs.layouts, 1, 2, 3, 4);
		gtk_table_attach_defaults(GTK_TABLE(table), gtk_label_new("Layout Label:"), 0, 1, 4, 5);
		gtk_table_attach_defaults(GTK_TABLE(table), level_layout.labelname, 1, 2, 4, 5);
		gtk_table_attach_defaults(GTK_TABLE(table), gtk_label_new("Layout Filename:"), 0, 1, 5, 6);
		gtk_table_attach_defaults(GTK_TABLE(table), level_layout.filename, 1, 2, 5, 6);
		gtk_table_set_row_spacing(GTK_TABLE(table), 5, 20);

		// LEVEL OBJECTS
		level_object.filename = gtk_entry_new_with_max_length(32);
		level_object.labelname = gtk_entry_new_with_max_length(20);

		// Connect change signal
		g_signal_connect(layout_object_cbs.objects, "changed", G_CALLBACK(gui_new_level_existing_lo_change), (gpointer)&level_object);

		gtk_table_attach_defaults(GTK_TABLE(table), gtk_label_new("Existing Objects:"), 0, 1, 6, 7);
		gtk_table_attach_defaults(GTK_TABLE(table), layout_object_cbs.objects, 1, 2, 6, 7);
		gtk_table_attach_defaults(GTK_TABLE(table), gtk_label_new("Object Label:"), 0, 1, 7, 8);
		gtk_table_attach_defaults(GTK_TABLE(table), level_object.labelname, 1, 2, 7, 8);
		gtk_table_attach_defaults(GTK_TABLE(table), gtk_label_new("Object Filename:"), 0, 1, 8, 9);
		gtk_table_attach_defaults(GTK_TABLE(table), level_object.filename, 1, 2, 8, 9);

		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), table, FALSE, FALSE, 5);

		/*
Level Name: 		[Descriptive Name]
Description (Optional):	[Description]
Tileset:		[  |V]

Layout:			[  |V]	Default to "--New--"; otherwise disables below  
Layout Label:		[    ]
Layout Filename:	[    ]

Objects:		[  |V]	Default to "--New--"; otherwise disables below
Object Label:		[    ]
Object Filename:	[    ]
		*/
	}

	// Make sure all widgets show up
	gtk_widget_show_all(popup);


	// Run dialog...
	do
	{
		result = (gtk_dialog_run (GTK_DIALOG (popup)) == GTK_RESPONSE_ACCEPT);

		// If user pressed cancel, quit!
		if(!result)
			break;

		// Otherwise, let's check some things...
		else
		{
			char buffer[512];
			const char *level_name = gtk_entry_get_text(GTK_ENTRY(level_name_entry));
			const char *layout_label = gtk_entry_get_text(GTK_ENTRY(level_layout.labelname));
			const char *layout_file = gtk_entry_get_text(GTK_ENTRY(level_layout.filename));
			const char *object_label = gtk_entry_get_text(GTK_ENTRY(level_object.labelname));
			const char *object_file = gtk_entry_get_text(GTK_ENTRY(level_object.filename));

			// Name of level MUST be specified
			if(strlen(level_name) < 1)
			{
				snprintf(buffer, sizeof(buffer), "Name of level must be specified");
				gui_display_message(TRUE, buffer);

				// FAIL
				result = FALSE;
				continue;
			}
			else if(gui_new_level_check_name_exists(level_name))
			{
				snprintf(buffer, sizeof(buffer), "Level name '%s' has already been defined, please use a different name", level_name);
				gui_display_message(TRUE, buffer);

				// FAIL
				result = FALSE;
				continue;
			}

			// Validate the other entries...
			result = 
				gui_new_level_validate_name("Layout label name", layout_label, TRUE) &&
				gui_new_level_validate_name("Layout file name", layout_file, FALSE) &&
				gui_new_level_validate_name("Object label name", object_label, TRUE) &&
				gui_new_level_validate_name("Object file name", object_file, FALSE);

			if(result && !strcmp(layout_label, object_label))
			{
				snprintf(buffer, sizeof(buffer), "Layout label and object label can NOT be the same!");
				gui_display_message(TRUE, buffer);

				// FAIL
				result = FALSE;
				continue;
			}


			// If the names are valid...
			if(result)
			{
				// This will store flags based on what the user has done...
				int reused_layout = 0, reused_object = 0;

				int existence_check;
				int tset = gui_combobox_simple_get_index(cb_tilesets);
				const struct NoDice_tileset *tileset = &NoDice_config.game.tilesets[tset];
				
				const char *layout_collection_file = tileset->rootfile;
				const char *object_collection_file = NoDice_config.game.options.object_set_bank;

				// Check if label currently exists in THIS tileset; this situation is
				// okay, it's valid as re-use of an existing layout.
				snprintf(buffer, sizeof(buffer), SUBDIR_LEVELS "/%s" EXT_ASM, layout_collection_file);
				existence_check = gui_new_level_check_label_exists(buffer, layout_label);
				if(existence_check == 1)
				{
					snprintf(buffer, sizeof(buffer), "NOTE: Layout Label %s already exists in this tileset; this is not a problem, but the layout will be shared with whatever other levels also use this layout.\n\nAlso note that the entered Layout Filename will be ignored (since it is already defined by this label.)\n\nDo you want to continue?", layout_label);

					// Give user a chance to cancel in case of mistake
					if(!gui_ask_question(buffer))
						result = FALSE;
					else
						// Don't re-create the layout file!!
						reused_layout = 1;
				}
				else if(existence_check == -1)
				{
					gui_display_message(TRUE, "Failed the layout label check due to a file error.  Cannot safely create the new level!");
					result = FALSE;
				}
				else
				{
					// Otherwise, check if label specified exists in a different tileset;
					// this situation is NOT okay, it would result in SMB3 crashing!

					// We'll use NoDice_get_addr_for_label to attempt to resolve the label; 
					// if we get anything besides 0xFFFF (not found) back, we can assume
					// that the label exists in another tileset or in whatever way is not
					// valid to be used here!
					if(NoDice_get_addr_for_label(layout_label) != 0xFFFF)
					{
						snprintf(buffer, sizeof(buffer), "Label %s cannot be used in this tileset because it is defined in another bank (e.g. in an incompatible tileset); enter a different name.", layout_label);
						gui_display_message(TRUE, buffer);
						result = FALSE;
					}
				}


				if(result)
				{
					// Check if label currently exists in the object collection; this situation is
					// okay, it's valid as re-use of an existing object layout.
					snprintf(buffer, sizeof(buffer), SUBDIR_PRG "/%s" EXT_ASM, object_collection_file);
					existence_check = gui_new_level_check_label_exists(buffer, object_label);
					if(existence_check == 1)
					{
						snprintf(buffer, sizeof(buffer), "NOTE: Object Label %s already exists; this is not a problem, but the object layout will be shared with whatever other levels also use this object layout.\n\nAlso note that the entered Object Filename will be ignored (since it is already defined by this label.)\n\nDo you want to continue?", object_label);

						// Give user a chance to cancel in case of mistake
						if(!gui_ask_question(buffer))
							result = FALSE;
						else
							// Don't re-create the object file!!
							reused_object = 1;
					}
					else if(existence_check == -1)
					{
						gui_display_message(TRUE, "Failed the object label check due to a file error.  Cannot safely create the new level!");
						result = FALSE;
					}
					else
					{
						// Otherwise, check if label specified exists elsewhere;
						// this situation is NOT okay, SMB3 only has one bank!

						// We'll use NoDice_get_addr_for_label to attempt to resolve the label; 
						// if we get anything besides 0xFFFF (not found) back, we can assume
						// that the label exists in elsewhere and is not valid.
						if(NoDice_get_addr_for_label(object_label) != 0xFFFF)
						{
							snprintf(buffer, sizeof(buffer), "Label %s cannot be used since it is defined outside the object bank; enter a different name.", object_label);
							gui_display_message(TRUE, buffer);
							result = FALSE;
						}
					}
				}

				if(result)
				{
					int save_layout = !reused_layout, save_objects = !reused_object;

					// Make sure user isn't accidentally overwriting something they didn't mean to
					edit_level_save_new_check(tileset->path, layout_file, &save_layout, object_file, &save_objects);

					// If edit_level_save_new_check reported the layout file exists, alert user about this!
					if(!reused_layout && !save_layout)
					{
						snprintf(buffer, sizeof(buffer), "NOTE: Layout file %s already exists; this is not a problem, but the layout data will be shared with whatever other levels also use this object layout file.\n\nDo you want to continue?", layout_file);

						// Give user a chance to cancel in case of mistake
						if(!gui_ask_question(buffer))
							result = FALSE;
					}

					if(result)
					{
						// If edit_level_save_new_check reported the object file exists, alert user about this!
						if(!reused_object && !save_objects)
						{
							snprintf(buffer, sizeof(buffer), "NOTE: Object layout file %s already exists; this is not a problem, but the object layout data will be shared with whatever other levels also use this object layout file.\n\nDo you want to continue?", object_file);

							// Give user a chance to cancel in case of mistake
							if(!gui_ask_question(buffer))
								result = FALSE;
						}
					}
				}

				// Layout and Object labels are verified OK!  Now comes the dangerous part!
				if(result)
				{
					const char *error = NoDice_config_game_add_level_entry(
						(unsigned char)tset, 
						level_name, 
						layout_file, layout_label, object_file, object_label, 
						gtk_entry_get_text(GTK_ENTRY(level_desc)));

					// Need to re-point this
					tileset = &NoDice_config.game.tilesets[tset];

					// Associating minimal info..
					NoDice_the_level.tileset = tileset;

					// If error occurred, display it!
					if(error != NULL)
					{
						gui_display_message(TRUE, error);
						result = FALSE;
					}
					// No error...
					else
					{
						// Get the level back out of configuration
						const struct NoDice_the_levels *new_level = gui_new_level_get_level_by_name(level_name);

						NoDice_the_level.tileset = tileset;

						// NoDice_the_level has now been minimally configured with new level
						// thanks to the efforts of NoDice_config_game_add_level_entry...

						// Non-reuse labels MUST be added to their respective files
						if(edit_level_add_labels(new_level, !reused_layout, !reused_object))
						{
							// So now we should call the save routine so the files get created
							// as needed...
							if(edit_level_save(!reused_layout, !reused_object))
							{
								// Now load level and we should be in sync!
								edit_level_load(tileset->id, new_level);

								gui_reboot();

								gui_update_for_generators();
							}
						}
						else
						{
							gui_display_message(TRUE, "Failed to create new labels!");
							result = FALSE;
						}
					}
				}

			}
		}
	} while(!result);

	gtk_widget_destroy (popup);

	// Clean up...
	free(layout_pairs);
	layout_pairs = NULL;

	free(object_pairs);
	object_pairs = NULL;

	return result;
}
