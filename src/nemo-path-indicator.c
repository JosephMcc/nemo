#include "nemo-path-indicator.h"

struct _NemoPathIndicator
{
    GtkButton parent_instance;

    GtkWidget *icon;
};

G_DEFINE_TYPE (NemoPathIndicator, nemo_path_indicator, GTK_TYPE_BUTTON)

static void
nemo_path_indicator_init (NemoPathIndicator *self)
{
    GtkWidget *box;
    GtkWidget *arrow_icon;

    box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add (GTK_CONTAINER (self), box);

    self->icon = gtk_image_new_from_icon_name ("go-home-symbolic", GTK_ICON_SIZE_MENU);
    gtk_widget_set_margin_start (self->icon, 6);
    gtk_box_pack_start (GTK_BOX (box), self->icon, FALSE, FALSE, 0);

    arrow_icon = gtk_image_new_from_icon_name ("nemo-path-next-symbolic", GTK_ICON_SIZE_MENU);
    gtk_style_context_add_class (gtk_widget_get_style_context (arrow_icon), "arrow-icon");
    gtk_box_pack_start (GTK_BOX (box), arrow_icon, FALSE, FALSE, 0);

    gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)), "nemo-path-indicator");
}

static void
nemo_path_indicator_class_init (NemoPathIndicatorClass *klass)
{
}

NemoPathIndicator *
nemo_path_indicator_new ()
{
    return g_object_new (NEMO_TYPE_PATH_INDICATOR, NULL);
}

void
nemo_path_indicator_set_icon (NemoPathIndicator *self,
                              const gchar       *icon_name)
{
    g_return_if_fail (NEMO_IS_PATH_INDICATOR (self));

    gtk_image_set_from_icon_name (GTK_IMAGE (self->icon), icon_name, GTK_ICON_SIZE_MENU);
}
