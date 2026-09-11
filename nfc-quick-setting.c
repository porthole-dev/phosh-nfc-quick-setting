/*
 * Copyright (C) 2026 Giuseppe Maggio
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nfc-quick-setting.h"

#include <glib/gi18n.h>

#define NEARD_BUS_NAME      "org.neard"
#define NEARD_ADAPTER_IFACE "org.neard.Adapter"

/**
 * PhoshNfcQuickSetting:
 *
 * A quick setting that turns the NFC radio on and off.
 *
 * The radio is `org.neard.Adapter`'s `Powered` property, which is the same
 * switch Settings shows under Privacy & Security. neard's bus policy allows
 * the user at the console to write it, so this needs no helper and no polkit
 * agent.
 *
 * The tile hides itself where there is no adapter, which is nearly every
 * machine. That mirrors the Settings page, which hides its row for the same
 * reason: a dead control on every desktop is worse than no control.
 */
struct _PhoshNfcQuickSetting {
  PhoshQuickSetting   parent;

  PhoshStatusIcon    *info;
  PhoshStatusPage    *status_page;
  GtkWidget          *placeholder;

  GCancellable       *cancellable;
  GDBusObjectManager *manager;
  GDBusProxy         *adapter;

  /* What the user last asked for, and whether we are still asking neard for
   * it. Powering an NFC controller up is not instant, so the tile follows the
   * request and the radio catches up. */
  gboolean            pending;
  gboolean            target;
  gboolean            requested;
  gboolean            retried;
};

G_DEFINE_TYPE (PhoshNfcQuickSetting, phosh_nfc_quick_setting, PHOSH_TYPE_QUICK_SETTING);


static gboolean
get_powered (PhoshNfcQuickSetting *self)
{
  g_autoptr (GVariant) value = NULL;

  if (self->adapter == NULL)
    return FALSE;

  value = g_dbus_proxy_get_cached_property (self->adapter, "Powered");

  return value != NULL &&
         g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN) &&
         g_variant_get_boolean (value);
}


static gboolean
get_polling (PhoshNfcQuickSetting *self)
{
  g_autoptr (GVariant) value = NULL;

  if (self->adapter == NULL)
    return FALSE;

  value = g_dbus_proxy_get_cached_property (self->adapter, "Polling");

  return value != NULL &&
         g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN) &&
         g_variant_get_boolean (value);
}


static void
update_state (PhoshNfcQuickSetting *self)
{
  gtk_widget_set_visible (GTK_WIDGET (self), self->adapter != NULL);

  /* While a request is in flight the tile shows what was asked for. Taking the
   * radio's word for it here would snap the tile back to the old state for as
   * long as the hardware takes, which reads as a tile that ignores you. */
  if (!self->pending)
    phosh_quick_setting_set_active (PHOSH_QUICK_SETTING (self), get_powered (self));
}


static gboolean
transform_to_icon_name (GBinding *binding, const GValue *from, GValue *to, gpointer data)
{
  g_value_set_string (to, g_value_get_boolean (from) ? "nfc-quick-setting-symbolic"
                                                     : "nfc-disabled-quick-setting-symbolic");
  return TRUE;
}


static gboolean
transform_to_label (GBinding *binding, const GValue *from, GValue *to, gpointer data)
{
  g_value_set_string (to, g_value_get_boolean (from) ? _("NFC On") : _("NFC Off"));
  return TRUE;
}


static void apply_target (PhoshNfcQuickSetting *self);


static void
on_set_powered_ready (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr (GVariant) reply = NULL;
  g_autoptr (GError) error = NULL;
  PhoshNfcQuickSetting *self;

  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    return;

  self = PHOSH_NFC_QUICK_SETTING (user_data);
  self->pending = FALSE;

  if (reply == NULL) {
    /* neard refuses to power down while a poll loop is running, and the
     * Polling we looked at came from a cache that anything else on the system
     * can have made stale a moment earlier. Rather than leave the press
     * swallowed, stop the loop and ask once more. */
    if (!self->requested && !self->retried) {
      self->retried = TRUE;
      apply_target (self);
      return;
    }

    g_warning ("Failed to switch the NFC radio: %s", error->message);
    update_state (self);
    return;
  }

  /* Taps that arrived while this one was in flight were folded into `target`
   * rather than queued, so at most one more request follows however hard the
   * tile was pressed. */
  if (self->target != self->requested) {
    apply_target (self);
    return;
  }

  /* The write succeeded, so the radio IS what was asked for. Re-reading the
   * cached property here is what swallowed presses: this reply and the
   * PropertiesChanged carrying the new value have no ordering between them,
   * so the cache still says the old thing often enough to be seen as the tile
   * snapping back. */
  phosh_quick_setting_set_active (PHOSH_QUICK_SETTING (self), self->requested);
}


static void
set_powered (PhoshNfcQuickSetting *self, gboolean powered)
{
  self->requested = powered;

  g_dbus_connection_call (g_dbus_proxy_get_connection (self->adapter),
                          NEARD_BUS_NAME,
                          g_dbus_proxy_get_object_path (self->adapter),
                          "org.freedesktop.DBus.Properties",
                          "Set",
                          g_variant_new ("(ssv)", NEARD_ADAPTER_IFACE, "Powered",
                                         g_variant_new_boolean (powered)),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          self->cancellable,
                          on_set_powered_ready,
                          self);
}


static void
on_poll_stopped (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr (GVariant) reply = NULL;
  g_autoptr (GError) error = NULL;
  PhoshNfcQuickSetting *self;

  reply = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), result, &error);
  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    return;

  self = PHOSH_NFC_QUICK_SETTING (user_data);

  /* The result does not matter: neard answers "Not polling" when the loop had
   * already stopped, and the radio is what we came for either way. Apply the
   * target rather than a literal FALSE, since it may have flipped while the
   * loop was stopping. */
  set_powered (self, self->target);
}


static void
apply_target (PhoshNfcQuickSetting *self)
{
  self->pending = TRUE;

  /* neard refuses to power the adapter down while a poll loop is running, so
   * an app left scanning would make this tile look broken: it answers
   * org.neard.Error.Failed and the radio stays on. Take the loop down first. */
  if (!self->target && (self->retried || get_polling (self))) {
    g_dbus_proxy_call (self->adapter,
                       "StopPollLoop",
                       NULL,
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       self->cancellable,
                       on_poll_stopped,
                       self);
    return;
  }

  set_powered (self, self->target);
}


static void
on_clicked (PhoshNfcQuickSetting *self)
{
  if (self->adapter == NULL)
    return;

  /* Toggle what is on screen, not what the radio last reported: pressed twice
   * quickly, the second press has to see the first one. */
  self->target = !phosh_quick_setting_get_active (PHOSH_QUICK_SETTING (self));
  self->retried = FALSE;
  phosh_quick_setting_set_active (PHOSH_QUICK_SETTING (self), self->target);

  /* One request at a time. A press during an in-flight one only moves the
   * target, and whoever finishes reconciles. */
  if (self->pending)
    return;

  apply_target (self);
}


static int
cmp_object_path (gconstpointer a, gconstpointer b)
{
  return g_strcmp0 (g_dbus_object_get_object_path ((GDBusObject *) a),
                    g_dbus_object_get_object_path ((GDBusObject *) b));
}


/* Sorted, so a machine with two adapters picks the same one twice. Do not
 * assume nfc0: adapters are numbered and the number is neard's. */
static void
find_adapter (PhoshNfcQuickSetting *self)
{
  g_autoptr (GDBusProxy) adapter = NULL;
  GList *objects;

  objects = g_list_sort (g_dbus_object_manager_get_objects (self->manager), cmp_object_path);

  for (GList *l = objects; l != NULL; l = l->next) {
    if (adapter == NULL)
      adapter = G_DBUS_PROXY (g_dbus_object_get_interface (l->data, NEARD_ADAPTER_IFACE));
  }

  g_list_free_full (objects, g_object_unref);

  if (self->adapter == adapter) {
    update_state (self);
    return;
  }

  g_set_object (&self->adapter, adapter);
  update_state (self);
}


static void
on_manager_ready (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr (GError) error = NULL;
  GDBusObjectManager *manager;
  PhoshNfcQuickSetting *self;

  manager = g_dbus_object_manager_client_new_for_bus_finish (result, &error);
  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    return;

  self = PHOSH_NFC_QUICK_SETTING (user_data);

  if (manager == NULL) {
    g_warning ("Failed to watch neard: %s", error->message);
    return;
  }

  self->manager = manager;

  g_signal_connect_swapped (manager, "object-added", G_CALLBACK (find_adapter), self);
  g_signal_connect_swapped (manager, "object-removed", G_CALLBACK (find_adapter), self);
  /* A property change cannot add or remove an adapter, and during a scan
   * these arrive constantly as tags and records come and go. Walking and
   * sorting the whole object tree for each one is work for nothing. */
  g_signal_connect_swapped (manager, "interface-proxy-properties-changed",
                            G_CALLBACK (update_state), self);

  find_adapter (self);
}


static void
phosh_nfc_quick_setting_finalize (GObject *object)
{
  PhoshNfcQuickSetting *self = PHOSH_NFC_QUICK_SETTING (object);

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->adapter);
  g_clear_object (&self->manager);

  G_OBJECT_CLASS (phosh_nfc_quick_setting_parent_class)->finalize (object);
}


static void
phosh_nfc_quick_setting_class_init (PhoshNfcQuickSettingClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = phosh_nfc_quick_setting_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/nfc-quick-setting/qs.ui");
  gtk_widget_class_bind_template_child (widget_class, PhoshNfcQuickSetting, info);
  gtk_widget_class_bind_template_child (widget_class, PhoshNfcQuickSetting, status_page);
  gtk_widget_class_bind_template_child (widget_class, PhoshNfcQuickSetting, placeholder);
  gtk_widget_class_bind_template_callback (widget_class, on_clicked);
}


static void
phosh_nfc_quick_setting_init (PhoshNfcQuickSetting *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->cancellable = g_cancellable_new ();

  g_object_bind_property_full (self, "active", self->info, "icon-name", G_BINDING_SYNC_CREATE,
                               transform_to_icon_name, NULL, NULL, NULL);
  g_object_bind_property_full (self, "active", self->info, "info", G_BINDING_SYNC_CREATE,
                               transform_to_label, NULL, NULL, NULL);
  g_object_bind_property_full (self, "active", self->placeholder, "icon-name",
                               G_BINDING_SYNC_CREATE,
                               transform_to_icon_name, NULL, NULL, NULL);
  g_object_bind_property_full (self, "active", self->placeholder, "title", G_BINDING_SYNC_CREATE,
                               transform_to_label, NULL, NULL, NULL);

  /* Until neard answers there is nothing to offer, so show nothing. */
  gtk_widget_set_visible (GTK_WIDGET (self), FALSE);

  g_dbus_object_manager_client_new_for_bus (G_BUS_TYPE_SYSTEM,
                                            G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                                            NEARD_BUS_NAME,
                                            "/",
                                            NULL, NULL, NULL,
                                            self->cancellable,
                                            on_manager_ready,
                                            self);
}
