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
#include "nemo-actions.h"

struct _NemoNavigationBar
{
    GtkFrame parent_instance;

    GtkActionGroup *action_group;

    NemoPathIndicator *path_indicator;
    NemoPathBar *path_bar;
    GtkWidget *location_bar;
    GtkWidget *stack;

    gboolean show_location_entry;
};

enum
{
    PROP_0,
    PROP_ACTION_GROUP,
    N_PROPERTIES
};

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

G_DEFINE_TYPE (NemoNavigationBar, nemo_navigation_bar, GTK_TYPE_FRAME)

static void
nemo_navigation_bar_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    NemoNavigationBar *self = NEMO_NAVIGATION_BAR (object);

    switch (prop_id) {
        case PROP_ACTION_GROUP:
            self->action_group = g_value_dup_object (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
nemo_navigation_bar_constructed (GObject *object)
{
    NemoNavigationBar *self = NEMO_NAVIGATION_BAR (object);
    GtkWidget *main_box;
    GtkAction *action;

    G_OBJECT_CLASS (nemo_navigation_bar_parent_class)->constructed (object);

    gtk_style_context_add_class (gtk_widget_get_style_context GTK_WIDGET (self), "nemo-navigation-bar");

    main_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add (GTK_CONTAINER (self), main_box);

    self->path_indicator = nemo_path_indicator_new ();
    action = gtk_action_group_get_action (self->action_group, NEMO_ACTION_TOGGLE_LOCATION);
    gtk_activatable_set_related_action (GTK_ACTIVATABLE (self->path_indicator), action);
    gtk_box_pack_start (GTK_BOX (main_box), GTK_WIDGET (self->path_indicator), FALSE, FALSE, 0);

    self->stack = gtk_stack_new ();
    gtk_stack_set_transition_type (GTK_STACK (self->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration (GTK_STACK (self->stack), 150);
    gtk_box_pack_start (GTK_BOX (main_box), self->stack, TRUE, TRUE, 0);

    self->path_bar = nemo_path_bar_new (self->path_indicator);
    gtk_stack_add_named (GTK_STACK (self->stack), GTK_WIDGET (self->path_bar), "path_bar");

    self->location_bar = nemo_location_bar_new ();
    gtk_stack_add_named (GTK_STACK (self->stack), GTK_WIDGET (self->location_bar), "location_bar");
}

static void
nemo_navigation_bar_dispose (GObject *object)
{
    NemoNavigationBar *self = NEMO_NAVIGATION_BAR (object);

    g_clear_object (&self->action_group);

    G_OBJECT_CLASS (nemo_navigation_bar_parent_class)->dispose (object);
}

static void
nemo_navigation_bar_init (NemoNavigationBar *self)
{
}

static void
nemo_navigation_bar_class_init (NemoNavigationBarClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = nemo_navigation_bar_set_property;
    object_class->constructed = nemo_navigation_bar_constructed;
    object_class->dispose = nemo_navigation_bar_dispose;

    obj_properties[PROP_ACTION_GROUP] =
        g_param_spec_object ("action-group",
                             "Action group",
                             "The action group to get actions from",
                             GTK_TYPE_ACTION_GROUP,
                             G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPERTIES, obj_properties);
}

NemoNavigationBar *
nemo_navigation_bar_new (GtkActionGroup *action_group)
{
    return g_object_new (NEMO_TYPE_NAVIGATION_BAR,
                         "action-group", action_group,
                         NULL);
}

GtkWidget *
nemo_navigation_bar_get_path_bar (NemoNavigationBar *self)
{
    return GTK_WIDGET (self->path_bar);
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
