#pragma once

#include <gtk/gtk.h>

#define NEMO_TYPE_PATH_INDICATOR (nemo_path_indicator_get_type ())

G_DECLARE_FINAL_TYPE (NemoPathIndicator, nemo_path_indicator, NEMO, PATH_INDICATOR, GtkButton)

NemoPathIndicator *nemo_path_indicator_new (void);

void nemo_path_indicator_set_icon (NemoPathIndicator *self,
                                   const gchar       *icon_name);
