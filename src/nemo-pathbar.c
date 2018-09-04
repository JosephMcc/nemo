/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* nemo-pathbar.c
 * Copyright (C) 2004  Red Hat, Inc.,  Jonathan Blandford <jrb@gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
 * Boston, MA 02110-1335, USA.
 */


#include <config.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <math.h>

#include "nemo-pathbar.h"

#include <eel/eel-vfs-extensions.h>

#include <libnemo-private/nemo-file.h>
#include <libnemo-private/nemo-file-utilities.h>
#include <libnemo-private/nemo-global-preferences.h>
#include <libnemo-private/nemo-icon-names.h>
#include <libnemo-private/nemo-icon-dnd.h>

#include "nemo-window.h"
#include "nemo-window-private.h"
#include "nemo-window-slot.h"
#include "nemo-window-slot-dnd.h"

enum {
    PATH_CLICKED,
    PATH_SET,
    LAST_SIGNAL
};

enum {
    PROP_0,
    PROP_INDICATOR,
    N_PROPERTIES
};

typedef enum {
    NORMAL_BUTTON,
    ROOT_BUTTON,
    HOME_BUTTON,
    RECENT_BUTTON,
    TRASH_BUTTON,
    NETWORK_BUTTON,
    DESKTOP_BUTTON,
    MOUNT_BUTTON,
    XDG_BUTTON
} ButtonType;

#define BUTTON_DATA(x) ((ButtonData *)(x))

static guint path_bar_signals [LAST_SIGNAL] = { 0 };

static gboolean desktop_is_home;

#define NEMO_PATH_BAR_ICON_SIZE 16
#define NEMO_PATH_BAR_BUTTON_MAX_WIDTH 250

/*
 * Content of pathbar->button_list:
 *       <- next                      previous ->
 * ---------------------------------------------------------------------
 * | /   |   home  |   user      | downloads    | folder   | sub folder
 * ---------------------------------------------------------------------
 *  last             fake_root                              button_list
 *                                scrolled_root_button
 */

typedef struct {
    GtkWidget *button;
    ButtonType type;
    char *dir_name;
    GFile *path;
    NemoFile *file;
    unsigned int file_changed_signal_id;

    gchar *mount_icon_name;

    GtkWidget *image;
    GtkWidget *label;
    GtkWidget *bold_label;
    GtkWidget *container;
    guint ignore_changes : 1;
    guint is_root : 1;
} ButtonData;

struct _NemoPathBar {
	GtkContainer parent_instance;

	GdkWindow *event_window;

	GFile *root_path;
	GFile *home_path;
	GFile *desktop_path;

	/** XDG Dirs */
	GFile *xdg_documents_path;
	GFile *xdg_download_path;
	GFile *xdg_music_path;
	GFile *xdg_pictures_path;
	GFile *xdg_public_path;
	GFile *xdg_templates_path;
	GFile *xdg_videos_path;

    NemoPathIndicator *indicator;

	GFile *current_path;
	gpointer current_button_data;

	GList *button_list;
	guint settings_signal_id;
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

G_DEFINE_TYPE (NemoPathBar, nemo_path_bar, GTK_TYPE_CONTAINER);

static GFile* get_xdg_dir               (GUserDirectory dir);
static void     nemo_path_bar_check_icon_theme         (NemoPathBar *self);
static void     nemo_path_bar_update_button_appearance (NemoPathBar *self,
                                                        ButtonData  *button_data);
static void     nemo_path_bar_update_button_state      (NemoPathBar *self,
                                                        ButtonData  *button_data,
                                gboolean         current_dir);
static void     nemo_path_bar_update_path              (NemoPathBar *self,
                                GFile           *file_path);

static void
nemo_path_bar_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
    NemoPathBar *self = NEMO_PATH_BAR (object);

    switch (prop_id)
    {
        case PROP_INDICATOR:
            nemo_path_bar_set_indicator (self, g_value_get_object (value));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
nemo_path_bar_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
    NemoPathBar *self = NEMO_PATH_BAR (object);

    switch (prop_id)
    {
        case PROP_INDICATOR:
            g_value_set_object (value, self->indicator);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
update_button_types (NemoPathBar *self)
{
    GList *list;
    GFile *path = NULL;

    for (list = self->button_list; list; list = list->next) {
        ButtonData *button_data;
        button_data = BUTTON_DATA (list->data);
        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button_data->button))) {
            path = g_object_ref (button_data->path);
            break;
        }
    }
    if (path != NULL) {
        nemo_path_bar_update_path (self, path);
        g_object_unref (path);
    }
}


static void
desktop_location_changed_callback (gpointer user_data)
{
    NemoPathBar *self;

    self = NEMO_PATH_BAR (user_data);

    g_object_unref (self->desktop_path);
    g_object_unref (self->home_path);
    self->desktop_path = nemo_get_desktop_location ();
    self->home_path = g_file_new_for_path (g_get_home_dir ());
    desktop_is_home = g_file_equal (self->home_path, self->desktop_path);

    update_button_types (self);
}

/**
 * Utility function. Return a GFile for the "special directory" if it exists, or NULL
 * Ripped from nemo-file.c (nemo_file_is_user_special_directory) and slightly modified
 */
static GFile*
get_xdg_dir (GUserDirectory dir) {

    const gchar *special_dir;

    special_dir = g_get_user_special_dir (dir);

    if (special_dir) {
        return g_file_new_for_path (special_dir);
    } else {
        return NULL;
    }

}

static void
nemo_path_bar_init (NemoPathBar *self)
{
    char *p;

    gtk_widget_set_has_window (GTK_WIDGET (self), FALSE);
    gtk_widget_set_redraw_on_allocate (GTK_WIDGET (self), FALSE);

    // gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (path_bar)),
    //                              GTK_STYLE_CLASS_LINKED);
    // gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (path_bar)),
    //                              "path-bar");

    p = nemo_get_desktop_directory ();
    self->desktop_path = g_file_new_for_path (p);
    g_free (p);
    self->home_path = g_file_new_for_path (g_get_home_dir ());
    self->root_path = g_file_new_for_path ("/");
    self->xdg_documents_path = get_xdg_dir (G_USER_DIRECTORY_DOCUMENTS);
    self->xdg_download_path = get_xdg_dir (G_USER_DIRECTORY_DOWNLOAD);
    self->xdg_music_path = get_xdg_dir (G_USER_DIRECTORY_MUSIC);
    self->xdg_pictures_path = get_xdg_dir (G_USER_DIRECTORY_PICTURES);
    self->xdg_public_path = get_xdg_dir (G_USER_DIRECTORY_PUBLIC_SHARE);
    self->xdg_templates_path = get_xdg_dir (G_USER_DIRECTORY_TEMPLATES);
    self->xdg_videos_path = get_xdg_dir (G_USER_DIRECTORY_VIDEOS);

    desktop_is_home = g_file_equal (self->home_path, self->desktop_path);

    g_signal_connect_swapped (nemo_preferences, "changed::" NEMO_PREFERENCES_DESKTOP_IS_HOME_DIR,
                  G_CALLBACK(desktop_location_changed_callback),
                  self);
}

static void
nemo_path_bar_finalize (GObject *object)
{
    NemoPathBar *self;

    self = NEMO_PATH_BAR (object);

    g_list_free (self->button_list);
    g_clear_object (&self->xdg_documents_path);
    g_clear_object (&self->xdg_download_path);
    g_clear_object (&self->xdg_music_path);
    g_clear_object (&self->xdg_pictures_path);
    g_clear_object (&self->xdg_public_path);
    g_clear_object (&self->xdg_templates_path);
    g_clear_object (&self->xdg_videos_path);

    g_signal_handlers_disconnect_by_func (nemo_preferences,
                          desktop_location_changed_callback,
                          self);

    G_OBJECT_CLASS (nemo_path_bar_parent_class)->finalize (object);
}

/* Removes the settings signal handler.  It's safe to call multiple times */
static void
remove_settings_signal (NemoPathBar *self,
            GdkScreen  *screen)
{
    if (self->settings_signal_id) {
          GtkSettings *settings;

         settings = gtk_settings_get_for_screen (screen);
         g_signal_handler_disconnect (settings,
                            self->settings_signal_id);
        self->settings_signal_id = 0;
    }
}

static void
nemo_path_bar_dispose (GObject *object)
{
    remove_settings_signal (NEMO_PATH_BAR (object), gtk_widget_get_screen (GTK_WIDGET (object)));

    G_OBJECT_CLASS (nemo_path_bar_parent_class)->dispose (object);
}

/* We always want to request the same size for the label, whether
 * or not the contents are bold
 */
static void
set_label_size_request (ButtonData *button_data)
{
    gint width, height;
    GtkRequisition nat_req, bold_req;

    if (button_data->label == NULL) {
        return;
    }

    gtk_widget_get_preferred_size (button_data->label, NULL, &nat_req);
    gtk_widget_get_preferred_size (button_data->bold_label, &bold_req, NULL);

    width = MAX (nat_req.width, bold_req.width);
    width = MIN (width, NEMO_PATH_BAR_BUTTON_MAX_WIDTH);
    height = MAX (nat_req.height, bold_req.height);

    gtk_widget_set_size_request (button_data->label, width, height);
}

/* Size requisition:
 *
 * Ideally, our size is determined by another widget, and we are just filling
 * available space.
 */
static void
nemo_path_bar_get_preferred_width (GtkWidget *widget,
                       gint      *minimum,
                       gint      *natural)
{
    ButtonData *button_data;
    NemoPathBar *self;
    GList *list;
    gint child_height;
    gint height;
    gint child_min, child_nat;

    self = NEMO_PATH_BAR (widget);

    *minimum = *natural = 0;
    height = 0;

    for (list = self->button_list; list; list = list->next) {
        button_data = BUTTON_DATA (list->data);
        set_label_size_request (button_data);

        gtk_widget_get_preferred_width (button_data->container, &child_min, &child_nat);
        gtk_widget_get_preferred_height (button_data->container, &child_height, NULL);
        height = MAX (height, child_height);

        if (button_data->type == NORMAL_BUTTON) {
            /* Use 2*Height as button width because of ellipsized labels */
            child_min = MAX (child_min, child_height * 2);
            child_nat = MAX (child_min, child_height * 2);
        }

        *minimum = MAX (*minimum, child_min);
        *natural = *natural + child_nat;
    }
}

static void
nemo_path_bar_get_preferred_height (GtkWidget *widget,
                    gint      *minimum,
                    gint      *natural)
{
    ButtonData *button_data;
    NemoPathBar *self;
    GList *list;
    gint child_min, child_nat;

    self = NEMO_PATH_BAR (widget);

    *minimum = *natural = 0;

    for (list = self->button_list; list; list = list->next) {
        button_data = BUTTON_DATA (list->data);
        set_label_size_request (button_data);

        gtk_widget_get_preferred_height (button_data->container, &child_min, &child_nat);

        *minimum = MAX (*minimum, child_min);
        *natural = MAX (*natural, child_nat);
    }
}

static void
nemo_path_bar_unmap (GtkWidget *widget)
{
    gdk_window_hide (NEMO_PATH_BAR (widget)->event_window);

    GTK_WIDGET_CLASS (nemo_path_bar_parent_class)->unmap (widget);
}

static void
nemo_path_bar_map (GtkWidget *widget)
{
    gdk_window_show (NEMO_PATH_BAR (widget)->event_window);

    GTK_WIDGET_CLASS (nemo_path_bar_parent_class)->map (widget);
}

/* This is a tad complicated */
static void
nemo_path_bar_size_allocate (GtkWidget     *widget,
                             GtkAllocation *allocation)
{
    GtkWidget *child;
    NemoPathBar *self;
    GtkTextDirection direction;
    GtkAllocation child_allocation;
    GList *list, *first_button;
    gint width;
    gint largest_width;
    GtkRequisition child_requisition;

    self = NEMO_PATH_BAR (widget);

    gtk_widget_set_allocation (widget, allocation);

    if (gtk_widget_get_realized (widget)) {
        gdk_window_move_resize (self->event_window,
                    allocation->x, allocation->y,
                    allocation->width, allocation->height);
    }

    /* No path is set so we don't have to allocate anything. */
    if (self->button_list == NULL) {
        return;
    }
    direction = gtk_widget_get_direction (widget);

    width = 0;

    gtk_widget_get_preferred_size (BUTTON_DATA (self->button_list->data)->container,
                       &child_requisition, NULL);
    width += child_requisition.width;

    for (list = self->button_list->next; list; list = list->next) {
        child = BUTTON_DATA (list->data)->button;
        gtk_widget_get_preferred_size (child, &child_requisition, NULL);
        width += child_requisition.width;
    }

    if (width <= allocation->width) {
        first_button = g_list_last (self->button_list);
    } else {
        gboolean reached_end;
        reached_end = FALSE;

        first_button = self->button_list;

        /* To see how much space we have, and how many buttons we can display.
         * We start at the first button, count forward until hit the new
         * button, then count backwards.
         */

        /* Count down the path chain towards the end. */
        gtk_widget_get_preferred_size (BUTTON_DATA (first_button->data)->container,
                                       &child_requisition, NULL);
        width = child_requisition.width;
        list = first_button->prev;
        while (list && !reached_end) {
            child = BUTTON_DATA (list->data)->container;
            gtk_widget_get_preferred_size (child, &child_requisition, NULL);

            if (width + child_requisition.width > allocation->width) {
                reached_end = TRUE;
            } else {
                width += child_requisition.width;
            }
            list = list->prev;
        }

        /* Finally, we walk up, seeing how many of the previous buttons we can add*/
        while (first_button->next && !reached_end) {
            child = BUTTON_DATA (first_button->next->data)->button;
            gtk_widget_get_preferred_size (child, &child_requisition, NULL);

            if (width + child_requisition.width > allocation->width) {
                reached_end = TRUE;
            } else {
                width += child_requisition.width;
                first_button = first_button->next;
            }
        }
    }

    /* Now, we allocate space to the buttons */
    child_allocation.y = allocation->y;
    child_allocation.height = allocation->height;

    if (direction == GTK_TEXT_DIR_RTL) {
        child_allocation.x = allocation->x + allocation->width;
    } else {
        child_allocation.x = allocation->x;
    }

    /* Determine the largest possible allocation size */
    largest_width = allocation->width;
    for (list = first_button; list; list = list->prev) {
        child = BUTTON_DATA (list->data)->container;
        gtk_widget_get_preferred_size (child, &child_requisition, NULL);

        child_allocation.width = MIN (child_requisition.width, largest_width);
        if (direction == GTK_TEXT_DIR_RTL) {
            child_allocation.x -= child_allocation.width;
        }
    	/* Check to see if we've don't have any more space to allocate buttons */

        gtk_widget_set_child_visible (child, TRUE);
        gtk_widget_size_allocate (child, &child_allocation);

        if (direction == GTK_TEXT_DIR_LTR) {
            child_allocation.x += child_allocation.width;
        }
    }
    /* Now we go hide all the widgets that don't fit */
    while (list) {
        child = BUTTON_DATA (list->data)->container;
        gtk_widget_set_child_visible (child, FALSE);
        list = list->prev;
    }

    for (list = first_button->next; list; list = list->next) {
        child = BUTTON_DATA (list->data)->container;
        gtk_widget_set_child_visible (child, FALSE);
    }
}

static void
nemo_path_bar_style_updated (GtkWidget *widget)
{
    GTK_WIDGET_CLASS (nemo_path_bar_parent_class)->style_updated (widget);

    nemo_path_bar_check_icon_theme (NEMO_PATH_BAR (widget));
}

static void
nemo_path_bar_screen_changed (GtkWidget *widget,
                      GdkScreen *previous_screen)
{
    if (GTK_WIDGET_CLASS (nemo_path_bar_parent_class)->screen_changed) {
        GTK_WIDGET_CLASS (nemo_path_bar_parent_class)->screen_changed (widget, previous_screen);
    }
        /* We might nave a new settings, so we remove the old one */
    if (previous_screen) {
        remove_settings_signal (NEMO_PATH_BAR (widget), previous_screen);
    }
    nemo_path_bar_check_icon_theme (NEMO_PATH_BAR (widget));
}

static void
nemo_path_bar_realize (GtkWidget *widget)
{
    NemoPathBar *self;
    GtkAllocation allocation;
    GdkWindow *window;
    GdkWindowAttr attributes;
    gint attributes_mask;

    gtk_widget_set_realized (widget, TRUE);

    self = NEMO_PATH_BAR (widget);
    window = gtk_widget_get_parent_window (widget);
    gtk_widget_set_window (widget, window);
    g_object_ref (window);

    gtk_widget_get_allocation (widget, &allocation);

    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.x = allocation.x;
    attributes.y = allocation.y;
    attributes.width = allocation.width;
    attributes.height = allocation.height;
    attributes.wclass = GDK_INPUT_ONLY;
    attributes.event_mask = gtk_widget_get_events (widget);
    attributes.event_mask |=
        GDK_BUTTON_PRESS_MASK |
        GDK_BUTTON_RELEASE_MASK;
    attributes_mask = GDK_WA_X | GDK_WA_Y;

    self->event_window = gdk_window_new (gtk_widget_get_parent_window (widget),
                         &attributes, attributes_mask);
    gdk_window_set_user_data (self->event_window, widget);
}

static void
nemo_path_bar_unrealize (GtkWidget *widget)
{
    NemoPathBar *self;

    self = NEMO_PATH_BAR (widget);

    gdk_window_set_user_data (self->event_window, NULL);
    gdk_window_destroy (self->event_window);
    self->event_window = NULL;

    GTK_WIDGET_CLASS (nemo_path_bar_parent_class)->unrealize (widget);
}

static void
nemo_path_bar_add (GtkContainer *container,
               GtkWidget    *widget)
{
    gtk_widget_set_parent (widget, GTK_WIDGET (container));
}

static void
nemo_path_bar_remove_1 (GtkContainer *container,
                    GtkWidget    *widget)
{
    gboolean was_visible = gtk_widget_get_visible (widget);
    gtk_widget_unparent (widget);
    if (was_visible) {
        gtk_widget_queue_resize (GTK_WIDGET (container));
    }
}

static void
nemo_path_bar_remove (GtkContainer *container,
                  GtkWidget    *widget)
{
    NemoPathBar *self;
    GList *children;

    self = NEMO_PATH_BAR (container);

    children = self->button_list;
    while (children != NULL) {
        if (widget == BUTTON_DATA (children->data)->container) {
          nemo_path_bar_remove_1 (container, widget);
            self->button_list = g_list_remove_link (self->button_list, children);
            g_list_free_1 (children);
            return;
        }
        children = children->next;
    }
}

static void
nemo_path_bar_forall (GtkContainer *container,
                  gboolean      include_internals,
                  GtkCallback   callback,
                  gpointer      callback_data)
{
    NemoPathBar *self;
    GList *children;

    g_return_if_fail (callback != NULL);
    self = NEMO_PATH_BAR (container);

    children = self->button_list;
    while (children != NULL) {
        GtkWidget *child;
        child = BUTTON_DATA (children->data)->container;
        children = children->next;
        (* callback) (child, callback_data);
    }
}

static GtkWidgetPath *
nemo_path_bar_get_path_for_child (GtkContainer *container,
                    GtkWidget *child)
{
    NemoPathBar *self = NEMO_PATH_BAR (container);
    GtkWidgetPath *path;

    path = gtk_widget_path_copy (gtk_widget_get_path (GTK_WIDGET (self)));

    if (gtk_widget_get_visible (child) && gtk_widget_get_child_visible (child)) {
        GtkWidgetPath *sibling_path;
        GList *visible_children;
        GList *l;
        int pos;

        /* 1. Build the list of visible children, in visually left-to-right order
         * (i.e. independently of the widget's direction).  Note that our
         * button_list is stored in innermost-to-outermost path order!
         */

        visible_children = NULL;

        for (l = self->button_list; l; l = l->next) {
            ButtonData *data = l->data;

            if (gtk_widget_get_visible (data->container) &&
                gtk_widget_get_child_visible (data->container)) {
                visible_children = g_list_prepend (visible_children, data->container);
            }
        }

        if (gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL) {
            visible_children = g_list_reverse (visible_children);
        }

        /* 2. Find the index of the child within that list */

        pos = 0;

        for (l = visible_children; l; l = l->next) {
            GtkWidget *button = l->data;

            if (button == child) {
                break;
            }

            pos++;
        }

        /* 3. Build the path */

        sibling_path = gtk_widget_path_new ();

        for (l = visible_children; l; l = l->next) {
            gtk_widget_path_append_for_widget (sibling_path, l->data);
        }

        gtk_widget_path_append_with_siblings (path, sibling_path, pos);

        g_list_free (visible_children);
        gtk_widget_path_unref (sibling_path);
    } else {
        gtk_widget_path_append_for_widget (path, child);
    }

    return path;
}

static void
nemo_path_bar_class_init (NemoPathBarClass *path_bar_class)
{
    GObjectClass *gobject_class;
    GtkWidgetClass *widget_class;
    GtkContainerClass *container_class;

    gobject_class = (GObjectClass *) path_bar_class;
    widget_class = (GtkWidgetClass *) path_bar_class;
    container_class = (GtkContainerClass *) path_bar_class;

    gobject_class->finalize = nemo_path_bar_finalize;
    gobject_class->dispose = nemo_path_bar_dispose;
    gobject_class->set_property = nemo_path_bar_set_property;
    gobject_class->get_property = nemo_path_bar_get_property;

    widget_class->get_preferred_height = nemo_path_bar_get_preferred_height;
    widget_class->get_preferred_width = nemo_path_bar_get_preferred_width;
    widget_class->realize = nemo_path_bar_realize;
    widget_class->unrealize = nemo_path_bar_unrealize;
    widget_class->unmap = nemo_path_bar_unmap;
    widget_class->map = nemo_path_bar_map;
    widget_class->size_allocate = nemo_path_bar_size_allocate;
    widget_class->style_updated = nemo_path_bar_style_updated;
    widget_class->screen_changed = nemo_path_bar_screen_changed;

    container_class->add = nemo_path_bar_add;
    container_class->forall = nemo_path_bar_forall;
    container_class->remove = nemo_path_bar_remove;
    container_class->get_path_for_child = nemo_path_bar_get_path_for_child;

    path_bar_signals [PATH_CLICKED] =
        g_signal_new ("path-clicked",
        G_OBJECT_CLASS_TYPE (path_bar_class),
        G_SIGNAL_RUN_FIRST,
        0,
        NULL, NULL,
        g_cclosure_marshal_VOID__OBJECT,
        G_TYPE_NONE, 1,
        G_TYPE_FILE);
    path_bar_signals [PATH_SET] =
        g_signal_new ("path-set",
        G_OBJECT_CLASS_TYPE (path_bar_class),
        G_SIGNAL_RUN_FIRST,
        0,
        NULL, NULL,
        g_cclosure_marshal_VOID__OBJECT,
        G_TYPE_NONE, 1,
        G_TYPE_FILE);

    obj_properties[PROP_INDICATOR] =
        g_param_spec_object ("indicator",
                             "Indicator",
                             "Indicator associated with the pathbar",
                             NEMO_TYPE_PATH_INDICATOR,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (gobject_class, N_PROPERTIES, obj_properties);

     gtk_container_class_handle_border_width (container_class);
}

/* Changes the icons wherever it is needed */
static void
reload_icons (NemoPathBar *self)
{
    GList *list;

    for (list = self->button_list; list; list = list->next) {
        ButtonData *button_data;
        button_data = BUTTON_DATA (list->data);
        if (button_data->type != NORMAL_BUTTON || button_data->is_root) {
            nemo_path_bar_update_button_appearance (self, button_data);
        }
    }
}

/* Callback used when a GtkSettings value changes */
static void
settings_notify_cb (GObject    *object,
            GParamSpec *pspec,
            NemoPathBar *self)
{
    const char *name;

    name = g_param_spec_get_name (pspec);

     if (! strcmp (name, "gtk-icon-theme-name") || ! strcmp (name, "gtk-icon-sizes")) {
        reload_icons (self);
    }
}

static void
nemo_path_bar_check_icon_theme (NemoPathBar *self)
{
    GtkSettings *settings;

    if (self->settings_signal_id) {
        return;
    }

    settings = gtk_settings_get_for_screen (gtk_widget_get_screen (GTK_WIDGET (self)));
    self->settings_signal_id = g_signal_connect (settings, "notify", G_CALLBACK (settings_notify_cb), self);

    reload_icons (self);
}

/* Public functions and their helpers */
NemoPathBar *
nemo_path_bar_new (NemoPathIndicator *indicator)
{
    return g_object_new (NEMO_TYPE_PATH_BAR,
                         "indicator", indicator,
                         NULL);
}

void
nemo_path_bar_clear_buttons (NemoPathBar *self)
{
    while (self->button_list != NULL) {
        gtk_container_remove (GTK_CONTAINER (self), BUTTON_DATA (self->button_list->data)->container);
    }
}

static void
button_clicked_cb (GtkWidget *button,
           gpointer   data)
{
    ButtonData *button_data;
    NemoPathBar *self;
    GList *button_list;

    button_data = BUTTON_DATA (data);
    if (button_data->ignore_changes) {
        return;
    }

    self = NEMO_PATH_BAR (gtk_widget_get_parent (gtk_widget_get_parent (button)));

    button_list = g_list_find (self->button_list, button_data);
    g_assert (button_list != NULL);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);

    g_signal_emit (self, path_bar_signals [PATH_CLICKED], 0, button_data->path);
}

static void
button_data_free (ButtonData *button_data)
{
    g_object_unref (button_data->path);
    g_free (button_data->dir_name);

    g_clear_pointer (&button_data->mount_icon_name, g_free);

    if (button_data->file != NULL) {
        g_signal_handler_disconnect (button_data->file,
                         button_data->file_changed_signal_id);
        nemo_file_monitor_remove (button_data->file, button_data);
        nemo_file_unref (button_data->file);
    }

    g_free (button_data);
}

static const char *
get_dir_name (ButtonData *button_data)
{
    switch (button_data->type) {
        case DESKTOP_BUTTON:
        {
            return _("Desktop");
        }
        case ROOT_BUTTON:
        {
            return _("File System");
        }
        case HOME_BUTTON:
        {
            return _("Home");
        }
        default:
        {
            return button_data->dir_name;
        }
    }
}

static void
nemo_path_bar_update_button_appearance (NemoPathBar *self,
                                        ButtonData  *button_data)
{
    gchar *icon_name;

    const gchar *dir_name = get_dir_name (button_data);

    if (button_data->label != NULL) {
        char *markup;

        markup = g_markup_printf_escaped ("<b>%s</b>", dir_name);

        if (gtk_label_get_use_markup (GTK_LABEL (button_data->label))) {
            gtk_label_set_markup (GTK_LABEL (button_data->label), markup);
        } else {
            gtk_label_set_text (GTK_LABEL (button_data->label), dir_name);
        }

        gtk_label_set_markup (GTK_LABEL (button_data->bold_label), markup);
        g_free (markup);
    }

    icon_name = NULL;

    switch (button_data->type) {
        case ROOT_BUTTON:
            icon_name = g_strdup (NEMO_ICON_SYMBOLIC_FILESYSTEM);
            break;
        case HOME_BUTTON:
        case DESKTOP_BUTTON:
        case RECENT_BUTTON:
        case TRASH_BUTTON:
        case NETWORK_BUTTON:
        case XDG_BUTTON:
            icon_name = nemo_file_get_control_icon_name (button_data->file);
            break;
        case NORMAL_BUTTON:
            if (button_data->is_root) {
                icon_name = nemo_file_get_control_icon_name (button_data->file);
                break;
            }
        case MOUNT_BUTTON:
            if (button_data->mount_icon_name) {
                icon_name = g_strdup (button_data->mount_icon_name);
                break;
            }
        default:
            icon_name = NULL;
        }

        if (icon_name != NULL) {
            nemo_path_indicator_set_icon (self->indicator, icon_name);
        }

        g_free (icon_name);
}

static void
nemo_path_bar_update_button_state (NemoPathBar *self,
                                   ButtonData  *button_data,
                                   gboolean    current_dir)
{
    if (button_data->label != NULL) {
        gtk_label_set_label (GTK_LABEL (button_data->label), NULL);
        gtk_label_set_label (GTK_LABEL (button_data->bold_label), NULL);
        gtk_label_set_use_markup (GTK_LABEL (button_data->label), current_dir);
    }

    nemo_path_bar_update_button_appearance (self, button_data);

    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button_data->button)) != current_dir) {
        button_data->ignore_changes = TRUE;
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button_data->button), current_dir);
        button_data->ignore_changes = FALSE;
    }
}

static gboolean
setup_file_path_mounted_mount (GFile *location, ButtonData *button_data)
{
    GVolumeMonitor *volume_monitor;
    GList *mounts, *l;
    GMount *mount;
    gboolean result;
    GFile *root, *default_location;

    result = FALSE;
    volume_monitor = g_volume_monitor_get ();
    mounts = g_volume_monitor_get_mounts (volume_monitor);
    for (l = mounts; l != NULL; l = l->next) {
        mount = l->data;
        if (g_mount_is_shadowed (mount)) {
            continue;
        }
        if (result) {
            continue;
        }
        root = g_mount_get_root (mount);
        if (g_file_equal (location, root)) {
            result = TRUE;
            /* set mount specific details in button_data */
            if (button_data) {
                button_data->mount_icon_name = nemo_get_mount_icon_name (mount);
                button_data->dir_name = g_mount_get_name (mount);
                button_data->type = MOUNT_BUTTON;
            }
            g_object_unref (root);
            break;
        }
        default_location = g_mount_get_default_location (mount);
        if (!g_file_equal (default_location, root) &&
            g_file_equal (location, default_location)) {
            result = TRUE;
            /* set mount specific details in button_data */
            if (button_data) {
                button_data->mount_icon_name = nemo_get_mount_icon_name (mount);
            }
            g_object_unref (default_location);
            g_object_unref (root);
            break;
        }
        g_object_unref (default_location);
        g_object_unref (root);
    }
    g_list_free_full (mounts, g_object_unref);
    return result;
}

static void
setup_button_type (ButtonData       *button_data,
           NemoPathBar  *self,
           GFile *location)
{
    g_autofree char *uri = NULL;

    if (nemo_is_root_directory (location)) {
        button_data->type = ROOT_BUTTON;
    } else if (nemo_is_home_directory (location)) {
        button_data->type = HOME_BUTTON;
        button_data->is_root = TRUE;
    } else if (nemo_is_desktop_directory (location)) {
        if (!desktop_is_home) {
            button_data->type = DESKTOP_BUTTON;
        } else {
            button_data->type = NORMAL_BUTTON;
        }
    } else if (nemo_is_recent_directory (location)) {
        button_data->type = RECENT_BUTTON;
        button_data->is_root = TRUE;
    } else if (self->xdg_documents_path != NULL && g_file_equal (location, self->xdg_documents_path)) {
        button_data->type = XDG_BUTTON;
        button_data->is_root = TRUE;
    } else if (self->xdg_download_path != NULL && g_file_equal (location, self->xdg_download_path)) {
        button_data->type = XDG_BUTTON;
        button_data->is_root = TRUE;
    } else if (self->xdg_music_path != NULL && g_file_equal (location, self->xdg_music_path)) {
        button_data->type = XDG_BUTTON;
        button_data->is_root = TRUE;
    } else if (self->xdg_pictures_path != NULL && g_file_equal (location, self->xdg_pictures_path)) {
        button_data->type = XDG_BUTTON;
        button_data->is_root = TRUE;
    } else if (self->xdg_templates_path != NULL && g_file_equal (location, self->xdg_templates_path)) {
        button_data->type = XDG_BUTTON;
        button_data->is_root = TRUE;
    } else if (self->xdg_videos_path != NULL && g_file_equal (location, self->xdg_videos_path)) {
        button_data->type = XDG_BUTTON;
        button_data->is_root = TRUE;
    } else if (self->xdg_public_path != NULL && g_file_equal (location, self->xdg_public_path)) {
        button_data->type = XDG_BUTTON;
        button_data->is_root = TRUE;
    } else if (setup_file_path_mounted_mount (location, button_data)) {
        /* already setup */
    } else if (strcmp ((uri = g_file_get_uri (location)), "trash:///") == 0) {
        button_data->type = TRASH_BUTTON;
        button_data->is_root = TRUE;
    } else if (strcmp (uri, "network:///") == 0) {
        button_data->type = NETWORK_BUTTON;
        button_data->is_root = TRUE;
    } else {
        button_data->type = NORMAL_BUTTON;
    }
}

static void
button_drag_data_get_cb (GtkWidget          *widget,
             GdkDragContext     *context,
             GtkSelectionData   *selection_data,
             guint               info,
             guint               time_,
             gpointer            user_data)
{
    ButtonData *button_data;
    char *uri_list[2];
    char *tmp;

    button_data = user_data;

    uri_list[0] = g_file_get_uri (button_data->path);
    uri_list[1] = NULL;

    if (info == NEMO_ICON_DND_GNOME_ICON_LIST) {
        tmp = g_strdup_printf ("%s\r\n", uri_list[0]);
        gtk_selection_data_set (selection_data, gtk_selection_data_get_target (selection_data),
                    8, (const guchar *) tmp, strlen (tmp));
        g_free (tmp);
    } else if (info == NEMO_ICON_DND_URI_LIST) {
        gtk_selection_data_set_uris (selection_data, uri_list);
    }

    g_free (uri_list[0]);
}

static void
setup_button_drag_source (ButtonData *button_data)
{
    GtkTargetList *target_list;
    const GtkTargetEntry targets[] = {
        { (char *)NEMO_ICON_DND_GNOME_ICON_LIST_TYPE, 0, NEMO_ICON_DND_GNOME_ICON_LIST }
    };

        gtk_drag_source_set (button_data->button,
                     GDK_BUTTON1_MASK |
                 GDK_BUTTON2_MASK,
                     NULL, 0,
                 GDK_ACTION_MOVE |
                 GDK_ACTION_COPY |
                 GDK_ACTION_LINK |
                 GDK_ACTION_ASK);

    target_list = gtk_target_list_new (targets, G_N_ELEMENTS (targets));
    gtk_target_list_add_uri_targets (target_list, NEMO_ICON_DND_URI_LIST);
    gtk_drag_source_set_target_list (button_data->button, target_list);
    gtk_target_list_unref (target_list);

    g_signal_connect (button_data->button, "drag-data-get",
                G_CALLBACK (button_drag_data_get_cb),
                button_data);
}

static void
button_data_file_changed (NemoFile *file,
              ButtonData *button_data)
{
    GFile *location, *current_location, *parent, *button_parent;
    ButtonData *current_button_data;
    char *display_name;
    NemoPathBar *self;
    gboolean renamed, child;

    self = (NemoPathBar *) gtk_widget_get_ancestor (button_data->button,
                                NEMO_TYPE_PATH_BAR);
    if (self == NULL) {
        return;
    }

    g_assert (self->current_path != NULL);
    g_assert (self->current_button_data != NULL);

    current_button_data = self->current_button_data;

    location = nemo_file_get_location (file);
    if (!g_file_equal (button_data->path, location)) {
        parent = g_file_get_parent (location);
        button_parent = g_file_get_parent (button_data->path);

        renamed = (parent != NULL && button_parent != NULL) &&
               g_file_equal (parent, button_parent);

        if (parent != NULL) {
            g_object_unref (parent);
        }
        if (button_parent != NULL) {
            g_object_unref (button_parent);
        }

        if (renamed) {
            button_data->path = g_object_ref (location);
        } else {
            /* the file has been moved.
             * If it was below the currently displayed location, remove it.
             * If it was not below the currently displayed location, update the path bar
             */
            child = g_file_has_prefix (button_data->path,
                           self->current_path);

            if (child) {
                /* moved file inside current path hierarchy */
                g_object_unref (location);
                location = g_file_get_parent (button_data->path);
                current_location = g_object_ref (self->current_path);
            } else {
                /* moved current path, or file outside current path hierarchy.
                 * Update path bar to new locations.
                 */
                current_location = nemo_file_get_location (current_button_data->file);
            }

                nemo_path_bar_update_path (self, location);
                nemo_path_bar_set_path (self, current_location);
            g_object_unref (location);
            g_object_unref (current_location);
            return;
        }
    } else if (nemo_file_is_gone (file)) {
        gint idx, position;

        /* if the current or a parent location are gone, don't do anything, as the view
         * will get the event too and call us back.
         */
        current_location = nemo_file_get_location (current_button_data->file);

        if (g_file_has_prefix (location, current_location)) {
            /* remove this and the following buttons */
            position = g_list_position (self->button_list,
                    g_list_find (self->button_list, button_data));

            if (position != -1) {
                for (idx = 0; idx <= position; idx++) {
                    gtk_container_remove (GTK_CONTAINER (self),
                                  BUTTON_DATA (self->button_list->data)->container);
                }
            }
        }

        g_object_unref (current_location);
        g_object_unref (location);
        return;
    }
    g_object_unref (location);

    /* MOUNTs use the GMount as the name, so don't update for those */
    if (button_data->type != MOUNT_BUTTON) {
        display_name = nemo_file_get_display_name (file);
        if (g_strcmp0 (display_name, button_data->dir_name) != 0) {
            g_free (button_data->dir_name);
            button_data->dir_name = g_strdup (display_name);
        }

        g_free (display_name);
    }
    nemo_path_bar_update_button_appearance (self, button_data);
}

static ButtonData *
make_directory_button (NemoPathBar  *self,
               NemoFile     *file,
               gboolean          current_dir)
{
    GFile *path;
    GtkWidget *child;
    ButtonData *button_data;
    gchar *uri;

    path = nemo_file_get_location (file);
    child = NULL;

        /* Is it a special button? */
    button_data = g_new0 (ButtonData, 1);

    setup_button_type (button_data, self, path);
    button_data->button = gtk_toggle_button_new ();
    // gtk_style_context_add_class (gtk_widget_get_style_context (button_data->button), "text-button");
    gtk_button_set_focus_on_click (GTK_BUTTON (button_data->button), FALSE);
    /* TODO update button type when xdg directories change */

    // button_data->image = gtk_image_new ();
    // button_data->arrow_icon = gtk_image_new_from_icon_name ("pan-end-symbolic", GTK_ICON_SIZE_MENU);

    switch (button_data->type) {
        case ROOT_BUTTON:
        case HOME_BUTTON:
        case DESKTOP_BUTTON:
        case RECENT_BUTTON:
        case TRASH_BUTTON:
        case NETWORK_BUTTON:
        case MOUNT_BUTTON:
        case XDG_BUTTON:
        {
            // GtkWidget *separator_icon;

            // separator_label = gtk_label_new (G_DIR_SEPARATOR_S);
            // gtk_style_context_add_class (gtk_widget_get_style_context (separator_label), "dim-label");
            // separator_icon = gtk_image_new_from_icon_name ("pan-end-symbolic", GTK_ICON_SIZE_MENU);
            // gtk_style_context_add_class (gtk_widget_get_style_context (separator_icon), "arrow-icon");
            button_data->label = gtk_label_new (NULL);
            child = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
            button_data->container = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
            // gtk_box_pack_start (GTK_BOX (button_data->container), separator_icon, FALSE, FALSE, 0);
            gtk_box_pack_start (GTK_BOX (button_data->container), button_data->button, FALSE, FALSE,  0);
            gtk_box_pack_start (GTK_BOX (child), button_data->label, FALSE, FALSE, 0);
        }
        break;
        case NORMAL_BUTTON:
        default:
        {
            GtkWidget *separator_icon;

            // separator_label = gtk_label_new (G_DIR_SEPARATOR_S);
            // gtk_style_context_add_class (gtk_widget_get_style_context (separator_label), "dim-label");
            separator_icon = gtk_image_new_from_icon_name ("nemo-path-next-symbolic", GTK_ICON_SIZE_MENU);
            gtk_style_context_add_class (gtk_widget_get_style_context (separator_icon), "arrow-icon");
            button_data->label = gtk_label_new (NULL);
            child = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
            button_data->container = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
            gtk_box_pack_start (GTK_BOX (button_data->container), separator_icon, FALSE, FALSE, 0);
            gtk_box_pack_start (GTK_BOX (button_data->container), button_data->button, FALSE, FALSE,  0);
            gtk_box_pack_start (GTK_BOX (child), button_data->label, FALSE, FALSE, 0);
        }
        break;
    }
    if (button_data->label != NULL) {
        gtk_label_set_ellipsize (GTK_LABEL (button_data->label), PANGO_ELLIPSIZE_MIDDLE);
        gtk_label_set_single_line_mode (GTK_LABEL (button_data->label), TRUE);

        button_data->bold_label = gtk_label_new (NULL);
        gtk_widget_set_no_show_all (button_data->bold_label, TRUE);
        gtk_label_set_single_line_mode (GTK_LABEL (button_data->bold_label), TRUE);
        gtk_box_pack_start (GTK_BOX (child), button_data->bold_label, FALSE, FALSE, 0);
    }

    if (button_data->path == NULL) {
            button_data->path = g_object_ref (path);
    }
    if (button_data->dir_name == NULL) {
        button_data->dir_name = nemo_file_get_display_name (file);
    }
    if (button_data->file == NULL) {
        button_data->file = nemo_file_ref (file);
        nemo_file_monitor_add (button_data->file, button_data,
                       NEMO_FILE_ATTRIBUTES_FOR_ICON);
        button_data->file_changed_signal_id =
            g_signal_connect (button_data->file, "changed",
                      G_CALLBACK (button_data_file_changed),
                      button_data);
    }

    gtk_container_add (GTK_CONTAINER (button_data->button), child);
    gtk_widget_show_all (button_data->container);

    nemo_path_bar_update_button_state (self, button_data, current_dir);

    g_signal_connect (button_data->button, "clicked", G_CALLBACK (button_clicked_cb), button_data);
    g_object_weak_ref (G_OBJECT (button_data->button), (GWeakNotify) button_data_free, button_data);

    uri = g_file_get_uri (path);

    if (!eel_uri_is_search (uri)) {
        setup_button_drag_source (button_data);
    }

    g_clear_pointer (&uri, g_free);

    nemo_drag_slot_proxy_init (button_data->button, button_data->file, NULL);

    g_object_unref (path);

    return button_data;
}

// static gboolean
// nemo_path_bar_check_parent_path (NemoPathBar *path_bar,
//                      GFile *location,
//                      ButtonData **current_button_data)
// {
//     GList *list;
//     ButtonData *button_data, *current_data;
//     gboolean is_active;

//     current_data = NULL;

//     for (list = self->button_list; list; list = list->next) {
//         button_data = list->data;
//         if (g_file_equal (location, button_data->path)) {
//             current_data = button_data;
//             is_active = TRUE;

//             if (!gtk_widget_get_child_visible (current_data->container)) {
//                 self->scrolled_root_button = list;
//                 gtk_widget_queue_resize (GTK_WIDGET (path_bar));
//             }
//         } else {
//             is_active = FALSE;
//         }

//         nemo_path_bar_update_button_state (button_data, is_active);
//     }

//     return (current_data != NULL);
// }

static void
nemo_path_bar_update_path (NemoPathBar *self,
                           GFile       *file_path)
{
    NemoFile *file;
    gboolean first_directory;
    GList *new_buttons, *l;
    ButtonData *button_data;

    g_return_if_fail (NEMO_IS_PATH_BAR (self));
    g_return_if_fail (file_path != NULL);

    first_directory = TRUE;
    new_buttons = NULL;

    file = nemo_file_get (file_path);

    while (file != NULL) {
        NemoFile *parent_file;

        parent_file = nemo_file_get_parent (file);
        button_data = make_directory_button (self, file, first_directory);
        nemo_file_unref (file);

        if (first_directory) {
            first_directory = FALSE;
        }

        new_buttons = g_list_prepend (new_buttons, button_data);

        if (parent_file != NULL && button_data->is_root) {
            nemo_file_unref (parent_file);
            break;
        }

        file = parent_file;
    }

    nemo_path_bar_clear_buttons (self);
    self->button_list = g_list_reverse (new_buttons);

    for (l = self->button_list; l; l = l->next) {
        GtkWidget *container;
        container = BUTTON_DATA (l->data)->container;
        gtk_container_add (GTK_CONTAINER (self), container);
    }

    g_signal_emit (self, path_bar_signals [PATH_SET], 0, file_path);
}

void
nemo_path_bar_set_path (NemoPathBar *self,
                        GFile       *file_path)
{
    ButtonData *button_data;

    g_return_if_fail (NEMO_IS_PATH_BAR (self));
    g_return_if_fail (file_path != NULL);

        /* Check whether the new path is already present in the pathbar as buttons.
         * This could be a parent directory or a previous selected subdirectory. */
    // if (!nemo_path_bar_check_parent_path (path_bar, file_path, &button_data)) {
    //     nemo_path_bar_update_path (path_bar, file_path);
    //     button_data = g_list_nth_data (self->button_list, 0);
    // }
    nemo_path_bar_update_path (self, file_path);
    button_data = g_list_nth_data (self->button_list, 0);

    if (self->current_path != NULL) {
        g_object_unref (self->current_path);
    }

    self->current_path = g_object_ref (file_path);
    self->current_button_data = button_data;
}

GFile *
nemo_path_bar_get_path_for_button (NemoPathBar *self,
                       GtkWidget       *button)
{
    GList *list;

    g_return_val_if_fail (NEMO_IS_PATH_BAR (self), NULL);
    g_return_val_if_fail (GTK_IS_BUTTON (button), NULL);

    for (list = self->button_list; list; list = list->next) {
        ButtonData *button_data;
        button_data = BUTTON_DATA (list->data);
        if (button_data->button == button) {
            return g_object_ref (button_data->path);
        }
    }

    return NULL;
}

void
nemo_path_bar_set_indicator (NemoPathBar       *self,
                             NemoPathIndicator *indicator)
{
    g_return_if_fail (NEMO_IS_PATH_BAR (self));
    g_return_if_fail (NEMO_IS_PATH_INDICATOR (indicator) || indicator == NULL);

    if (self->indicator == indicator) {
        return;
    }

    self->indicator = indicator;
}
