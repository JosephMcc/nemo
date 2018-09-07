/* nemo-pathbar.h
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
 *
 * 
 */

#ifndef NEMO_PATHBAR_H
#define NEMO_PATHBAR_H

#include <gtk/gtk.h>
#include <gio/gio.h>

#include "nemo-path-indicator.h"

#define NEMO_TYPE_PATH_BAR (nemo_path_bar_get_type ())
G_DECLARE_FINAL_TYPE (NemoPathBar, nemo_path_bar, NEMO, PATH_BAR, GtkContainer)

NemoPathBar *nemo_path_bar_new                 (NemoPathIndicator *indicator);
void         nemo_path_bar_set_path            (NemoPathBar *path_bar,
                                                GFile       *file);
GFile       *nemo_path_bar_get_path_for_button (NemoPathBar *path_bar,
                                                GtkWidget   *button);
void         nemo_path_bar_clear_buttons       (NemoPathBar *path_bar);

#endif /* NEMO_PATHBAR_H */
