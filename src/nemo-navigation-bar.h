#pragma once

#include <gtk/gtk.h>

#define NEMO_TYPE_NAVIGATION_BAR (nemo_navigation_bar_get_type ())

G_DECLARE_FINAL_TYPE (NemoNavigationBar, nemo_navigation_bar, NEMO, NAVIGATION_BAR, GtkBox)

NemoNavigationBar *nemo_navigation_bar_new (void);

GtkWidget *nemo_navigation_bar_get_path_bar            (NemoNavigationBar *self);
GtkWidget *nemo_navigation_bar_get_location_bar        (NemoNavigationBar *self);
void       nemo_navigation_bar_set_show_location_entry (NemoNavigationBar *self,
                                                        gboolean           show_location_entry);
