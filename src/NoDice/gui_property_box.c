#include <string.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include "NoDiceLib.h"
#include "NoDice.h"
#include "guictls.h"

struct gui_generate_option_controls_context
{
	GtkWidget *widget;				// Widget in property box
	const char *widget_signal_name;	// Signal name to emit (for initial visibility of Show-If controls)
	guint widget_signal_data;		// Signal data to pass on emission (for initial visibility of Show-If controls)
	GtkWidget *label;				// Label in property box
	const struct NoDice_header_options *option_list;	// Option related to this control
};


struct showif_handler_foreach_callback_data
{
	// Control that fired the showif handler
	GtkWidget *changed_control;		// The control which changed and we're checking show-if controls against
	const struct NoDice_header_options *changed_control_option;	// Option belonging to control that changed
};

// Used to initialize showif_handler_foreach_callback_data option to control
static void showif_handler_count_foreach_callback(gpointer data, gpointer user_data)
{
	// Context: The current property box control item we're checking against
	// to see if this is the context belonging to the control that changed
	struct gui_generate_option_controls_context *context = (struct gui_generate_option_controls_context *)data;

	// showif_foreach_data: current item of the entire property box being checked
	// to see if this one represents the control that changed
	struct showif_handler_foreach_callback_data *showif_foreach_data = (struct showif_handler_foreach_callback_data *)user_data;

	// If this is the widget we're looking for, specify the found option
	if(showif_foreach_data->changed_control == context->widget)
		showif_foreach_data->changed_control_option = context->option_list;
}


static void showif_handler_foreach_callback(gpointer data, gpointer user_data)
{
	// Context: The current property box control item we're checking against
	// to see if this is the context belonging to the control that changed
	struct gui_generate_option_controls_context *context = (struct gui_generate_option_controls_context *)data;

	// showif_foreach_data: control that changed and related option set
	struct showif_handler_foreach_callback_data *showif_foreach_data = (struct showif_handler_foreach_callback_data *)user_data;

	// Don't check check_control against itself, and only proceed
	// if this control is referencing the check control's ID
	if(	(context->widget != showif_foreach_data->changed_control) &&
		(context->option_list->showif_id != NULL) &&
		(!strcasecmp(showif_foreach_data->changed_control_option->id, context->option_list->showif_id)) )
	{
		gboolean vis_current, vis_target;

		// At this point, we know that "check_control" references the id of
		// "changed_control", now we just need to see if the value matches;
		// if so, we show the control, otherwise we must hide it...
		GtkWidget *changed_control = showif_foreach_data->changed_control;

		// Get value of control we're checking
		unsigned char value = 0;

		if(GTK_IS_COMBO_BOX(changed_control))
			value = (unsigned char)gui_combobox_simple_get_index(changed_control);
		else if(GTK_IS_SPIN_BUTTON(changed_control))
			value = (unsigned char)gtk_spin_button_get_value(GTK_SPIN_BUTTON(changed_control));
		else if(GTK_IS_TOGGLE_BUTTON(changed_control))
			value = (unsigned char)gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(changed_control)) ? 1 : 0;

		vis_current = gtk_widget_get_visible(context->widget);
		vis_target = (value == context->option_list->showif_val);

		// Only do anything if visibility is changing
		if(vis_current != vis_target)
		{
			GtkWidget *target_widget = context->widget;

			gtk_widget_set_visible(context->label, vis_target);
			gtk_widget_set_visible(target_widget, vis_target);

			// If control is becoming visible but previously was not,
			// set to minimum value because we can't be sure if the
			// current value in the mask is valid on switch and
			// probably wouldn't be useful even if it was...
			if(vis_target)
			{
				if(GTK_IS_COMBO_BOX(target_widget))
					gui_combobox_simple_set_selected(target_widget, context->option_list->options[0].value);
				else if(GTK_IS_SPIN_BUTTON(target_widget))
					gtk_spin_button_set_value(GTK_SPIN_BUTTON(target_widget), 0);
				else if(GTK_IS_TOGGLE_BUTTON(target_widget))
					gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(target_widget), 0);

				// Force change signal just in case the above doesn't actually change it
				g_signal_emit_by_name(target_widget, context->widget_signal_name, context->widget_signal_data);
			}
		}
	}
}


static void showif_handler(GtkWidget *widget, GList *context)
{
	struct showif_handler_foreach_callback_data showif_data = { widget, NULL };

	// Get the option list of this control
	g_list_foreach(context, showif_handler_count_foreach_callback, &showif_data);

	if( (showif_data.changed_control_option != NULL) && (showif_data.changed_control_option->id != NULL) )
	{
		// Go through all controls to find out if this control should be shown or not
		g_list_foreach(context, showif_handler_foreach_callback, &showif_data);
	}
}


// The Show-If handlers for each supported control type
static void showif_handler_list(GtkComboBox *widget, gpointer user_data)
{
	showif_handler(GTK_WIDGET(widget), (GList *)user_data);
}

static void showif_handler_spin(GtkSpinButton *widget, gpointer user_data)
{
	showif_handler(GTK_WIDGET(widget), (GList *)user_data);
}

static void showif_handler_toggle(GtkToggleButton *widget, gpointer user_data)
{
	showif_handler(GTK_WIDGET(widget), (GList *)user_data);
}


void gui_generate_option_controls(GList **context, GtkWidget *vbox, unsigned short header_byte, unsigned char header_val, const struct NoDice_headers *header, GCallback list_change, GCallback toggle_change, GCallback spin_change)
{
	int j;

	for(j=0; j<header->options_list_count; j++)
	{
		GtkWidget *option_hbox = gtk_hbox_new(FALSE, 6);
		int k;
		const struct NoDice_header_options *option_list = &header->options_list[j];
		GtkWidget *label = gtk_label_new(option_list->display);
		struct gui_generate_option_controls_context *context_obj = (struct gui_generate_option_controls_context *)malloc(sizeof(struct gui_generate_option_controls_context));
		unsigned int header_ref_tag;

		// Context list provides a way to linearly access the properties even if they
		// come from multiple sections (e.g. the multiple header bytes) enabling the
		// "Show-If" functionality to work
		context_obj->option_list = option_list;
		context_obj->label = label;
		*context = g_list_insert(*context, (gpointer)context_obj, -1);

		// header_ref_tag provides a way back to the correct header
		// byte and option we're modifying when hooking up the signal
		header_ref_tag = (header_byte << 16) | (j & 0xFFFF);

		if((option_list->options_count == 0) && (option_list->display != NULL))
		{
			GtkWidget *option_spin = gtk_spin_button_new_with_range(0, (option_list->mask >> option_list->shift), 1);

			// Get index value relative to the option
			unsigned char relative_value = (header_val & option_list->mask) >> option_list->shift;

			// Create display label and pack
			gtk_box_pack_start(GTK_BOX(option_hbox), label, FALSE, FALSE, 5);

			// Pack checkbox
			gtk_box_pack_start(GTK_BOX(option_hbox), option_spin, FALSE, FALSE, 5);

			// Determine if this bit is set in the level
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(option_spin), relative_value);

			// Context configuration
			context_obj->widget = option_spin;
			context_obj->widget_signal_name = "value-changed";
			context_obj->widget_signal_data = header_ref_tag;

			// Show-If handler
			g_signal_connect (G_OBJECT (option_spin), context_obj->widget_signal_name, G_CALLBACK(showif_handler_spin), (gpointer)*context);

			if(spin_change != NULL)
				// Connect change signal
				g_signal_connect (G_OBJECT (option_spin), context_obj->widget_signal_name, G_CALLBACK(spin_change), GINT_TO_POINTER(context_obj->widget_signal_data));

		}
		else if((option_list->options_count == 1) && (option_list->display != NULL))
		{
			// Boolean flag; use a checkbox rather than a list
			GtkWidget *option_checkbox = gtk_check_button_new();

			// Create display label and pack
			gtk_box_pack_start(GTK_BOX(option_hbox), label, FALSE, FALSE, 5);

			// Pack checkbox
			gtk_box_pack_start(GTK_BOX(option_hbox), option_checkbox, FALSE, FALSE, 5);

			// Determine if this bit is set in the level
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(option_checkbox), header_val & option_list->mask);

			// Context configuration
			context_obj->widget = option_checkbox;
			context_obj->widget_signal_name = "toggled";
			context_obj->widget_signal_data = header_ref_tag;

			// Show-If handler
			g_signal_connect(option_checkbox, context_obj->widget_signal_name, G_CALLBACK(showif_handler_toggle), (gpointer)*context);

			if(toggle_change != NULL)
				// Connect change signal
				g_signal_connect(option_checkbox, context_obj->widget_signal_name, G_CALLBACK(toggle_change), GINT_TO_POINTER(context_obj->widget_signal_data));
		}
		else if(option_list->options_count > 1)
		{
			int selected_index = -1;

			// Get index value relative to the option
			unsigned char relative_value = (header_val & option_list->mask) >> option_list->shift;

			// Multiple items, make a combobox
			GtkWidget *option_combobox;

			// Create display label and pack
			gtk_box_pack_start(GTK_BOX(option_hbox), label, FALSE, FALSE, 5);

			// Create combobox and pack
			option_combobox = gui_combobox_simple_new();
			gtk_box_pack_start(GTK_BOX(option_hbox), option_combobox, FALSE, FALSE, 5);

			context_obj->widget = option_combobox;

			// Add all option values
			for(k=0; k<option_list->options_count; k++)
			{
				const struct NoDice_option *option = &option_list->options[k];

				// Add value
				gui_combobox_simple_add_item(option_combobox, option->value, option->display);

				// If this is the currently selected property value, hang on to it!
				if(option->value == relative_value)
					selected_index = option->value;
			}

			// Set proper selection
			gui_combobox_simple_set_selected(option_combobox, selected_index);

			// Context configuration
			context_obj->widget = option_combobox;
			context_obj->widget_signal_name = "changed";
			context_obj->widget_signal_data = header_ref_tag;

			// Show-If handler
			g_signal_connect(option_combobox, context_obj->widget_signal_name, G_CALLBACK(showif_handler_list), (gpointer)*context);

			if(list_change != NULL)
				// Connect change signal
				g_signal_connect(option_combobox, context_obj->widget_signal_name, G_CALLBACK(list_change), GINT_TO_POINTER(context_obj->widget_signal_data));
		}

		if(option_hbox != NULL)
			gtk_box_pack_start(GTK_BOX(vbox), option_hbox, FALSE, FALSE, 5);
	}
}


// foreach callback to fire all change signals
static void gui_generate_option_controls_emit_foreach_callback(gpointer data, gpointer user_data)
{
	struct gui_generate_option_controls_context *context = (struct gui_generate_option_controls_context *)data;

	// Emit the correct change signal on all controls to cause Show-Ifs to display properly initially
	// ID must be non-null or else it couldn't possibly cause Show-If changes anyway...
	if(context->option_list->id != NULL)
	{
		g_signal_emit_by_name(context->widget, context->widget_signal_name, context->widget_signal_data);
	}
}


void gui_update_option_controls(GList *context)
{
	g_list_foreach(context, gui_generate_option_controls_emit_foreach_callback, NULL);
}
