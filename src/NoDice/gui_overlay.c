#include <string.h>
#include "NoDiceLib.h"
#include "NoDice.h"
#include "guictls.h"

// http://zetcode.com/tutorials/cairographicstutorial/customgtkwidget/

static void gtk_selectable_overlay_class_init(GtkSelectableOverlayClass *klass);
static void gtk_selectable_overlay_init(GtkSelectableOverlay *selectable_overlay);
static void gtk_selectable_overlay_size_request(GtkWidget *widget, GtkRequisition *requisition);
static void gtk_selectable_overlay_size_allocate(GtkWidget *widget, GtkAllocation *allocation);
static void gtk_selectable_overlay_realize(GtkWidget *widget);
static void gtk_selectable_overlay_paint_obj(GtkWidget *widget, struct NoDice_the_level_object *obj);
static void gtk_selectable_overlay_paint_link(GtkWidget *widget, struct NoDice_map_link *link);
static void gtk_selectable_overlay_paint_selection(GtkWidget *widget);
static void gtk_selectable_overlay_destroy(GtkObject *object);

static gboolean gtk_selectable_overlay_expose(GtkWidget *widget, GdkEventExpose *event);
static gboolean gtk_selectable_overlay_enter(GtkWidget *widget, GdkEventCrossing *event);
static gboolean gtk_selectable_overlay_leave(GtkWidget *widget, GdkEventCrossing *event);
static gboolean gtk_selectable_overlay_press(GtkWidget *widget, GdkEventButton *event);
static gboolean gtk_selectable_overlay_release(GtkWidget *widget, GdkEventButton *event);
static gboolean gtk_selectable_overlay_motion(GtkWidget *widget, GdkEventMotion *event);

GtkType gtk_selectable_overlay_get_type(void)
{
	static GtkType gtk_selectable_overlay_type = 0;

	if (!gtk_selectable_overlay_type)
	{
		static const GtkTypeInfo gtk_selectable_overlay_info =
		{
			"GtkSelectableOverlay",
			sizeof(GtkSelectableOverlay),
			sizeof(GtkSelectableOverlayClass),
			(GtkClassInitFunc) gtk_selectable_overlay_class_init,
			(GtkObjectInitFunc) gtk_selectable_overlay_init,
			NULL,
			NULL,
			(GtkClassInitFunc) NULL
		};

		gtk_selectable_overlay_type = gtk_type_unique(GTK_TYPE_WIDGET, &gtk_selectable_overlay_info);
	}


	return gtk_selectable_overlay_type;
}


GtkWidget *gtk_selectable_overlay_new()
{
	return GTK_WIDGET(gtk_type_new(gtk_selectable_overlay_get_type()));
}


static void gtk_selectable_overlay_class_init(GtkSelectableOverlayClass *klass)
{
	GtkWidgetClass *widget_class;
	GtkObjectClass *object_class;


	widget_class = (GtkWidgetClass *) klass;
	object_class = (GtkObjectClass *) klass;

	widget_class->realize = gtk_selectable_overlay_realize;
	widget_class->size_request = gtk_selectable_overlay_size_request;
	widget_class->size_allocate = gtk_selectable_overlay_size_allocate;
	widget_class->expose_event = gtk_selectable_overlay_expose;

	widget_class->enter_notify_event = gtk_selectable_overlay_enter;
	widget_class->leave_notify_event = gtk_selectable_overlay_leave;
	widget_class->button_press_event = gtk_selectable_overlay_press;
	widget_class->button_release_event = gtk_selectable_overlay_release;
	widget_class->motion_notify_event = gtk_selectable_overlay_motion;

	object_class->destroy = gtk_selectable_overlay_destroy;
}


static void gtk_selectable_overlay_init(GtkSelectableOverlay *selectable_overlay)
{
	selectable_overlay->selected = FALSE;
	selectable_overlay->select_handler = NULL;
	selectable_overlay->gen = NULL;
	selectable_overlay->obj = NULL;
	selectable_overlay->link = NULL;

	memset(&selectable_overlay->translation_data, 0, sizeof(selectable_overlay->translation_data));
}


static void gtk_selectable_overlay_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
	g_return_if_fail(widget != NULL);
	g_return_if_fail(GTK_IS_SELECTABLE_OVERLAY(widget));
	g_return_if_fail(requisition != NULL);

	requisition->width = 80;
	requisition->height = 100;
}


static void gtk_selectable_overlay_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
	g_return_if_fail(widget != NULL);
	g_return_if_fail(GTK_IS_SELECTABLE_OVERLAY(widget));
	g_return_if_fail(allocation != NULL);

	widget->allocation = *allocation;

	if (GTK_WIDGET_REALIZED(widget))
	{
		gdk_window_move_resize(
			widget->window,
			allocation->x, allocation->y,
			allocation->width, allocation->height
			);
	}
}


static void gtk_selectable_overlay_realize(GtkWidget *widget)
{
	GdkWindowAttr attributes;
	guint attributes_mask;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(GTK_IS_SELECTABLE_OVERLAY(widget));

	GTK_WIDGET_SET_FLAGS(widget, GTK_REALIZED);

	attributes.window_type = GDK_WINDOW_CHILD;
	attributes.x = widget->allocation.x;
	attributes.y = widget->allocation.y;
	attributes.width = 80;
	attributes.height = 100;

	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.event_mask = gtk_widget_get_events(widget) | GDK_EXPOSURE_MASK | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_BUTTON1_MOTION_MASK;

	attributes_mask = GDK_WA_X | GDK_WA_Y;

	widget->window = gdk_window_new(gtk_widget_get_parent_window (widget), &attributes, attributes_mask);

	gdk_window_set_user_data(widget->window, widget);

	widget->style = gtk_style_attach(widget->style, widget->window);
	gtk_style_set_background(widget->style, widget->window, GTK_STATE_NORMAL);
}


gboolean gui_PPU_portal_expose_event_callback(GtkWidget *widget, GdkEventExpose *event, gpointer data);
static gboolean gtk_selectable_overlay_expose(GtkWidget *widget, GdkEventExpose *event)
{
	GtkSelectableOverlay *overlay = GTK_SELECTABLE_OVERLAY(widget);
	GtkAllocation alloc;

	g_return_val_if_fail(widget != NULL, FALSE);
	g_return_val_if_fail(GTK_IS_SELECTABLE_OVERLAY(widget), FALSE);
	g_return_val_if_fail(event != NULL, FALSE);

	// Chaining a call to the virtual PPU to redraw this rectangle
	// since a GTK widget cannot have a transparent background and
	// Win32 cannot do compositing due to unimplemented RGBA

	// Getting the allocation of -this widget- so we can offset
	// the virtual PPU's redraw as if it came from its parent.
	gtk_widget_get_allocation(widget, &alloc);

	// If selected, use origin X/Y because it will keep the correct appearance while dragging
	if(overlay->selected)
	{
		alloc.x = overlay->translation_data.origin_x;
		alloc.y = overlay->translation_data.origin_y;
	}

	// Go ahead and redraw this rectangle!
	gui_PPU_portal_expose_event_callback(widget, event, &alloc);

	if(overlay->obj != NULL)
		gtk_selectable_overlay_paint_obj(widget, overlay->obj);
	else if(overlay->link != NULL)
		gtk_selectable_overlay_paint_link(widget, overlay->link);

	// Now we may draw as we please~
	if(overlay->selected)
		gtk_selectable_overlay_paint_selection(widget);

	return TRUE;
}


static void gtk_selectable_overlay_paint_obj(GtkWidget *widget, struct NoDice_the_level_object *obj)
{
	GtkRequisition req;
	GtkAllocation alloc;
	cairo_t *cr;

	gtk_widget_get_allocation(widget, &alloc);
	gtk_widget_size_request(widget, &req);

	cr = gdk_cairo_create(widget->window);

	gui_draw_info.context = (void *)cr;

	// Save pre-scale in case we don't have a sprite draw
	cairo_save(cr);

	cairo_scale(cr, gui_draw_info.zoom, gui_draw_info.zoom);

	// If no icon available, just draw rectangle with ID
	if(!ppu_sprite_draw(obj->id, 0, 0))
	{
		const struct NoDice_objects *this_obj = &NoDice_config.game.objects[obj->id] ;
		int tx = (int)((double)(TILESIZE/4) * gui_draw_info.zoom);
		int ty = (int)((double)(TILESIZE/2) * gui_draw_info.zoom);
		int tsize = (int)(6.0 * gui_draw_info.zoom);

		char numbuf[3];
		const char *printtext = numbuf;

		if(this_obj->special_options.options_list_count == 0)
			// Non-special no sprite object
			snprintf(numbuf, sizeof(numbuf), "%02X", obj->id);
		else
			printtext = this_obj->name;

		// Restore to pre-scale cairo
		cairo_restore(cr);

		// Inner fill rectangle
		cairo_set_source_rgb(cr, 0.75, 0.75, 0.0);
		cairo_rectangle(cr, 1, 1, req.width-1, req.height-1);
		cairo_fill(cr);

		// Outer line dash
		cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
		cairo_rectangle(cr, 0, 0, req.width, req.height);
		cairo_stroke(cr);

		cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, tsize);
		cairo_move_to(cr, tx, ty);

		if(this_obj->special_options.options_list_count != 0)
			// Special object rotate 90
			cairo_rotate(cr, 1.570796327);

		cairo_show_text(cr, printtext);
	}

	cairo_destroy(cr);
}


static void gtk_selectable_overlay_paint_link(GtkWidget *widget, struct NoDice_map_link *link)
{
	GtkRequisition req;
	GtkAllocation alloc;
	cairo_t *cr;

	gtk_widget_get_allocation(widget, &alloc);
	gtk_widget_size_request(widget, &req);

	cr = gdk_cairo_create(widget->window);

	gui_draw_info.context = (void *)cr;

	cairo_scale(cr, gui_draw_info.zoom, gui_draw_info.zoom);

	{

		// Inner fill rectangle
		cairo_set_source_rgba(cr, 0.75, 0.75, 0.0, 0.5);
		cairo_rectangle(cr, 1, 1, req.width-1, req.height-1);
		cairo_fill(cr);

		// Outer line dash
		cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
		cairo_rectangle(cr, 0, 0, req.width, req.height);
		cairo_stroke(cr);

	}

	cairo_destroy(cr);
}


static void gtk_selectable_overlay_paint_selection(GtkWidget *widget)
{
	static const double dashed[] = {2.0, 3.0};
	GtkRequisition req;
	GtkAllocation alloc;
	cairo_t *cr;

	gtk_widget_get_allocation(widget, &alloc);
	gtk_widget_size_request(widget, &req);

	cr = gdk_cairo_create(widget->window);

	// Inner fill rectangle
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.5);
	cairo_rectangle(cr, 1, 1, req.width-1, req.height-1);
	cairo_fill(cr);

	// Outer line dash
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	cairo_set_dash(cr, dashed, sizeof(dashed)/sizeof(double), 0);
	cairo_rectangle(cr, 0, 0, req.width, req.height);
	cairo_stroke(cr);

	cairo_destroy(cr);
}


static void gtk_selectable_overlay_destroy(GtkObject *object)
{
	GtkSelectableOverlay *selectable_overlay;
	GtkSelectableOverlayClass *klass;

	g_return_if_fail(object != NULL);
	g_return_if_fail(GTK_IS_SELECTABLE_OVERLAY(object));

	selectable_overlay = GTK_SELECTABLE_OVERLAY(object);

	klass = gtk_type_class(gtk_widget_get_type());

	if (GTK_OBJECT_CLASS(klass)->destroy)
	{
		(* GTK_OBJECT_CLASS(klass)->destroy) (object);
	}
}

static gboolean gtk_selectable_overlay_enter(GtkWidget *widget, GdkEventCrossing *event)
{
	return TRUE;
}

static gboolean gtk_selectable_overlay_leave(GtkWidget *widget, GdkEventCrossing *event)
{
	return TRUE;
}

static void gtk_selectable_overlay_release_foreach_callback(GtkWidget *widget, gpointer selected_widget)
{
	GtkWidget *the_selected_widget = (GtkWidget *)selected_widget;

	if(widget != the_selected_widget && GTK_IS_SELECTABLE_OVERLAY(widget))
	{
		GtkSelectableOverlay *selectable_overlay = GTK_SELECTABLE_OVERLAY(widget);
		if(selectable_overlay->selected)
		{
			selectable_overlay->selected = FALSE;

			if( (selectable_overlay->obj != NULL) && (NoDice_config.game.objects[selectable_overlay->obj->id].special_options.options_list_count != 0) )
				// Special object should be hidden
				gtk_widget_hide(GTK_WIDGET(selectable_overlay));
			else
				// Otherwise just redraw for de-selection
				gtk_widget_queue_draw(widget);
		}
	}
}

static gboolean gtk_selectable_overlay_press(GtkWidget *widget, GdkEventButton *event)
{
	GtkSelectableOverlay *selectable_overlay = GTK_SELECTABLE_OVERLAY(widget);
	GtkAllocation alloc;

	// Get widget allocation
	gtk_widget_get_allocation(widget, &alloc);

	// Only a single left click used
	if(event->type == GDK_BUTTON_PRESS)
	{
		if(event->button == 1)
		{
			// FIXME: Add fine-point tuning here

			// Find any other control selected and de-select it!!
			gtk_container_foreach(GTK_CONTAINER(widget->parent), gtk_selectable_overlay_release_foreach_callback, (gpointer)widget);

			selectable_overlay->translation_data.mouse_origin_x = event->x;
			selectable_overlay->translation_data.mouse_origin_y = event->y;
			selectable_overlay->translation_data.origin_x = alloc.x;
			selectable_overlay->translation_data.origin_y = alloc.y;

			gtk_selectable_overlay_select(selectable_overlay);
		}
		else if(event->button == 3)
		{
			GtkWidget *event_box;
			GdkEventButton event_adjusted;
			gboolean unused;

			// Pass adjusted right-click down to the parent
			// Allows generator-atop-generator insertion

			// Make a duplicate of the event
			memcpy(&event_adjusted, event, sizeof(GdkEventButton));

			// Need to add the allocation of this widget so the coordinates are parent-relative
			event_adjusted.x += alloc.x;
			event_adjusted.y += alloc.y;

			// Emit a button press event to the parent's parent (should be event box)
			g_return_val_if_fail(widget->parent != NULL, TRUE);
			g_return_val_if_fail(widget->parent->parent != NULL, TRUE);

			event_box = widget->parent->parent;

			g_return_val_if_fail(GTK_IS_EVENT_BOX(event_box), TRUE);

			g_signal_emit_by_name(event_box, "button-press-event", &event_adjusted, &unused);
		}

	}

	return TRUE;
}


static gboolean gtk_selectable_overlay_release(GtkWidget *widget, GdkEventButton *event)
{
	// Only handle the left button here
	if(event->type == GDK_BUTTON_RELEASE && event->button == 1)
	{
		GtkAllocation alloc;
		GtkSelectableOverlay *selectable_overlay = GTK_SELECTABLE_OVERLAY(widget);
		int diff_row, diff_col;

		gtk_widget_get_allocation(widget, &alloc);

		diff_row = (int)((double)(alloc.y - selectable_overlay->translation_data.origin_y) / gui_draw_info.zoom / TILESIZE);
		diff_col = (int)((double)(alloc.x - selectable_overlay->translation_data.origin_x) / gui_draw_info.zoom / TILESIZE);

		if(diff_row != 0 || diff_col != 0)
		{
			if(selectable_overlay->gen != NULL)
				edit_gen_translate(selectable_overlay->gen, diff_row, diff_col);
			else if(selectable_overlay->obj != NULL)
			{
				const struct NoDice_objects *this_obj = &NoDice_config.game.objects[selectable_overlay->obj->id];

				if(this_obj->special_options.options_list_count != 0)
					// Special object: Do not allow row changes (property window handles that)
					diff_row = 0;

				edit_obj_translate(selectable_overlay->obj, diff_row, diff_col);
			}
			else if(selectable_overlay->link != NULL)
			{
				edit_link_translate(selectable_overlay->link, diff_row, diff_col);
			}
		}
	}

	return TRUE;
}


static gboolean gtk_selectable_overlay_motion(GtkWidget *widget, GdkEventMotion *event)
{
	GtkAllocation alloc;
	GtkSelectableOverlay *selectable_overlay = GTK_SELECTABLE_OVERLAY(widget);
	int diff_x = (int)((event->x - selectable_overlay->translation_data.mouse_origin_x) / gui_draw_info.zoom / TILESIZE);
	int diff_y = (int)((event->y - selectable_overlay->translation_data.mouse_origin_y) / gui_draw_info.zoom / TILESIZE);

	if(diff_x != 0 || diff_y != 0)
	{
		gtk_widget_get_allocation(widget, &alloc);

		gtk_fixed_move(GTK_FIXED(widget->parent), widget, alloc.x + diff_x * TILESIZE * gui_draw_info.zoom, alloc.y + diff_y * TILESIZE * gui_draw_info.zoom);
	}

	return TRUE;
}


void gtk_selectable_overlay_select(GtkSelectableOverlay *selectable_overlay)
{
	// Set selected
	selectable_overlay->selected = TRUE;

	// Run callback
	if(selectable_overlay->select_handler != NULL)
		selectable_overlay->select_handler(selectable_overlay);

	// Need to redraw
	gtk_widget_queue_draw(GTK_WIDGET(selectable_overlay));
}
