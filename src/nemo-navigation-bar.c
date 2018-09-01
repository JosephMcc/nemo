/*
 * Nemo
 *
 * Copyright (C) 2018, Linux Mint
 *
 * Nemo is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Nemo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, MA 02110-1335, USA.
 *
 * Author: Joseph Mccullar
 *
 */

#include <config.h>

#include "nemo-navigation-bar.h"
#include "nemo-location-bar.h"
#include "nemo-pathbar.h"
#include "nemo-path-indicator.h"

struct _NemoNavigationBar
{
    GtkFrame parent_instance;

    NemoPathIndicator *path_indicator;
    GtkWidget *path_bar;
    GtkWidget *location_bar;
    GtkWidget *stack;

    gboolean show_location_entry;
};

G_DEFINE_TYPE (NemoNavigationBar, nemo_navigation_bar, GTK_TYPE_FRAME)

static void
nemo_navigation_bar_init (NemoNavigationBar *self)
{
    GtkWidget *main_box;

    gtk_style_context_add_class (gtk_widget_get_style_context GTK_WIDGET (self), "nemo-navigation-bar");

    main_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add (GTK_CONTAINER (self), main_box);

    self->path_indicator = nemo_path_indicator_new ();
    gtk_box_pack_start (GTK_BOX (main_box), GTK_WIDGET (self->path_indicator), FALSE, FALSE, 0);

    self->stack = gtk_stack_new ();
    gtk_stack_set_transition_type (GTK_STACK (self->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration (GTK_STACK (self->stack), 150);
    gtk_box_pack_start (GTK_BOX (main_box), self->stack, TRUE, TRUE, 0);

    self->path_bar = g_object_new (NEMO_TYPE_PATH_BAR, NULL);
    gtk_stack_add_named (GTK_STACK (self->stack), GTK_WIDGET (self->path_bar), "path_bar");

    self->location_bar = nemo_location_bar_new ();
    gtk_stack_add_named (GTK_STACK (self->stack), GTK_WIDGET (self->location_bar), "location_bar");

    // gtk_widget_show_all (main_box);
}

static void
nemo_navigation_bar_class_init (NemoNavigationBarClass *klass)
{
}

NemoNavigationBar *
nemo_navigation_bar_new ()
{
    return g_object_new (NEMO_TYPE_NAVIGATION_BAR, NULL);
}

GtkWidget *
nemo_navigation_bar_get_path_bar (NemoNavigationBar *self)
{
    return self->path_bar;
}

GtkWidget *
nemo_navigation_bar_get_location_bar (NemoNavigationBar *self)
{
    return self->location_bar;
}

void
nemo_navigation_bar_set_show_location_entry (NemoNavigationBar *self,
                                             gboolean           show_location_entry)
{
    if (show_location_entry) {
        gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "location_bar");
    } else {
        gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "path_bar");
    }
}
