// This file is part of GNOME Games. License: GPL-3.0+.

#include "linux-raw-gamepad.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gi18n-lib.h>
#include <libevdev/libevdev.h>
#include <stdlib.h>
#include <unistd.h>
#include "../raw-gamepad.h"
#include "../standard-gamepad-axis.h"
#include "../standard-gamepad-button.h"

#define GUID_DATA_LENGTH 8
#define GUID_STRING_LENGTH 32 // (GUID_DATA_LENGTH * sizeof (guint16))

struct _GamesLinuxRawGamepad {
  GObject parent_instance;

  gint fd;
  glong event_source_id;
  struct libevdev *device;
  guint8 key_map[KEY_MAX];
  guint8 abs_map[ABS_MAX];
  struct input_absinfo abs_info[ABS_MAX];
  gchar *guid;
};

static void games_raw_gamepad_interface_init (GamesRawGamepadInterface *interface);

G_DEFINE_TYPE_WITH_CODE (GamesLinuxRawGamepad, games_linux_raw_gamepad, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GAMES_TYPE_RAW_GAMEPAD,
                                                games_raw_gamepad_interface_init))

/* Private */

static gchar
guint16_get_hex (guint16 value,
                 guint8 nibble)
{
  static const gchar hex_to_ascii_map[] = "0123456789abcdef";

  g_assert (nibble < 4);

  return hex_to_ascii_map[((value >> (4 * nibble)) & 0xf)];
}

static gchar *
guint16s_to_hex_string (guint16 *data)
{
  gchar *result;
  gint data_i;
  gint result_i;
  guint16 element;

  result = g_malloc (GUID_STRING_LENGTH + 1);
  result[GUID_STRING_LENGTH] = '\0';
  for (data_i = 0, result_i = 0; data_i < GUID_DATA_LENGTH; data_i++) {
    element = data[data_i];
    result[result_i++] = guint16_get_hex (element, 1);
    result[result_i++] = guint16_get_hex (element, 0);
    result[result_i++] = guint16_get_hex (element, 3);
    result[result_i++] = guint16_get_hex (element, 2);
  }

  return result;
}

static gchar *
compute_guid_string (struct libevdev *device)
{
  guint16 guid_array[GUID_DATA_LENGTH] = { 0 };

  guid_array[0] = (guint16) GINT_TO_LE (libevdev_get_id_bustype (device));
  guid_array[1] = 0;
  guid_array[2] = (guint16) GINT_TO_LE (libevdev_get_id_vendor (device));
  guid_array[3] = 0;
  guid_array[4] = (guint16) GINT_TO_LE (libevdev_get_id_product (device));
  guid_array[5] = 0;
  guid_array[6] = (guint16) GINT_TO_LE (libevdev_get_id_version (device));
  guid_array[7] = 0;

  return guint16s_to_hex_string (guid_array);
}

static const gchar *
get_guid (GamesRawGamepad *base)
{
  GamesLinuxRawGamepad *self;

  self = GAMES_LINUX_RAW_GAMEPAD (base);

  if (self->guid == NULL)
    self->guid = compute_guid_string (self->device);

  return self->guid;
}

static struct input_absinfo *_abs_info_dup (struct input_absinfo *self);

static GamesStandardGamepadAxis
axis_to_standard_axis (gint code)
{
  switch (code) {
  case ABS_X:
    return GAMES_STANDARD_GAMEPAD_AXIS_LEFT_X;
  case ABS_Y:
    return GAMES_STANDARD_GAMEPAD_AXIS_LEFT_Y;
  case ABS_RX:
    return GAMES_STANDARD_GAMEPAD_AXIS_RIGHT_X;
  case ABS_RY:
    return GAMES_STANDARD_GAMEPAD_AXIS_RIGHT_Y;
  default:
    return GAMES_STANDARD_GAMEPAD_AXIS_UNKNOWN;
  }
}

static GamesStandardGamepadButton
button_to_standard_button (gint code)
{
  switch (code) {
  case BTN_A:
    return GAMES_STANDARD_GAMEPAD_BUTTON_A;
  case BTN_B:
    return GAMES_STANDARD_GAMEPAD_BUTTON_B;
  case BTN_X:
    return GAMES_STANDARD_GAMEPAD_BUTTON_Y;
  case BTN_Y:
    return GAMES_STANDARD_GAMEPAD_BUTTON_X;
  case BTN_TL:
    return GAMES_STANDARD_GAMEPAD_BUTTON_SHOULDER_L;
  case BTN_TR:
    return GAMES_STANDARD_GAMEPAD_BUTTON_SHOULDER_R;
  case BTN_TL2:
    return GAMES_STANDARD_GAMEPAD_BUTTON_TRIGGER_L;
  case BTN_TR2:
    return GAMES_STANDARD_GAMEPAD_BUTTON_TRIGGER_R;
  case BTN_SELECT:
    return GAMES_STANDARD_GAMEPAD_BUTTON_SELECT;
  case BTN_START:
    return GAMES_STANDARD_GAMEPAD_BUTTON_START;
  case BTN_MODE:
    return GAMES_STANDARD_GAMEPAD_BUTTON_HOME;
  case BTN_THUMBL:
    return GAMES_STANDARD_GAMEPAD_BUTTON_STICK_L;
  case BTN_THUMBR:
    return GAMES_STANDARD_GAMEPAD_BUTTON_STICK_R;
  default:
    return GAMES_STANDARD_GAMEPAD_BUTTON_UNKNOWN;
  }
}

static gdouble
centered_axis_value (struct input_absinfo *abs_info,
                     gint32                value)
{
  gint64 min_absolute;
  gint64 max_normalized;
  gint64 value_normalized;
  gint64 max_centered;
  gint64 value_centered;
  gint64 divisor;

  g_return_val_if_fail (abs_info != NULL, 0.0);

  min_absolute = llabs ((gint64) abs_info->minimum);

  max_normalized = ((gint64) abs_info->maximum) + min_absolute;
  value_normalized = ((gint64) value) + min_absolute;

  max_centered = max_normalized / 2;
  value_centered = (value_normalized - max_normalized) + max_centered;

  divisor = value_centered < 0 ? max_centered + 1 : max_centered;;

  return ((gdouble) value_centered) / ((gdouble) divisor);
}

static void
handle_evdev_event (GamesLinuxRawGamepad *self)
{
  struct input_event event = { 0 };
  gint code;
  guint8 axis;
  gdouble value;

  g_return_if_fail (self != NULL);

  if (libevdev_next_event (self->device, (guint) LIBEVDEV_READ_FLAG_NORMAL, &event) != 0)
    return;

  // FIXME Should not cast from uint to int? No need to store it?
  code = (gint) event.code;
  switch (event.type) {
  case EV_KEY:
    if ((code & BTN_GAMEPAD) == BTN_GAMEPAD)
      g_signal_emit_by_name ((GamesRawGamepad*) self,
                             "standard-button-event",
                             button_to_standard_button (code),
                             (gboolean) event.value);
    g_signal_emit_by_name ((GamesRawGamepad*) self,
                           "button-event",
                           (gint) self->key_map[code - BTN_MISC],
                           (gboolean) event.value);

    break;
  case EV_ABS:

    switch (code) {
      case ABS_HAT0X:
      case ABS_HAT0Y:
      case ABS_HAT1X:
      case ABS_HAT1Y:
      case ABS_HAT2X:
      case ABS_HAT2Y:
      case ABS_HAT3X:
      case ABS_HAT3Y:
        code = code - ABS_HAT0X;
        g_signal_emit_by_name ((GamesRawGamepad*) self,
                               "dpad-event",
                               code / 2,
                               code % 2,
                               (gint) event.value);

        return;
      case ABS_X:
      case ABS_Y:
      case ABS_RX:
      case ABS_RY:
        axis = self->abs_map[code];
        value = centered_axis_value (&self->abs_info[axis], event.value);
        g_signal_emit_by_name ((GamesRawGamepad*) self,
                               "standard-axis-event",
                               axis_to_standard_axis (code),
                               value);

        break;
    }

    axis = self->abs_map[code];
    value = centered_axis_value (&self->abs_info[axis], event.value);
    g_signal_emit_by_name ((GamesRawGamepad*) self, "axis-event", (gint) axis, value);

    break;
  }
}

static gboolean
poll_events (GIOChannel   *source,
             GIOCondition  condition,
             gpointer      data)
{
  GamesLinuxRawGamepad *self;

  self = GAMES_LINUX_RAW_GAMEPAD (data);

  g_return_val_if_fail (self != NULL, FALSE);

  while (libevdev_has_event_pending (self->device))
    handle_evdev_event (self);

  return TRUE;
}

static gboolean
has_key (struct libevdev *device,
         guint            code)
{
  return libevdev_has_event_code (device, (guint) EV_KEY, code);
}

static gboolean
has_abs (struct libevdev *device,
         guint            code)
{
  return libevdev_has_event_code (device, (guint) EV_ABS, code);
}

static gboolean
is_joystick (struct libevdev *device)
{
  gboolean has_joystick_axes_or_buttons;

  g_return_val_if_fail (device != NULL, FALSE);

  /* Same detection code as udev-builtin-input_id.c in systemd
   * joysticks don’t necessarily have buttons; e. g.
   * rudders/pedals are joystick-like, but buttonless; they have
   * other fancy axes. */
  has_joystick_axes_or_buttons =
    has_key (device, BTN_TRIGGER) ||
    has_key (device, BTN_A) ||
    has_key (device, BTN_1) ||
    has_abs (device, ABS_RX) ||
    has_abs (device, ABS_RY) ||
    has_abs (device, ABS_RZ) ||
    has_abs (device, ABS_THROTTLE) ||
    has_abs (device, ABS_RUDDER) ||
    has_abs (device, ABS_WHEEL) ||
    has_abs (device, ABS_GAS) ||
    has_abs (device, ABS_BRAKE);

  return has_joystick_axes_or_buttons;
}

static void
remove_event_source (GamesLinuxRawGamepad *self)
{
  g_return_if_fail (self != NULL);

  if (self->event_source_id < 0)
    return;

  g_source_remove ((guint) self->event_source_id);
  self->event_source_id = -1;
}

/* Public */

GamesLinuxRawGamepad *
games_linux_raw_gamepad_new (const gchar  *file_name,
                             GError      **error)
{
  GamesLinuxRawGamepad *self = NULL;
  GIOChannel *channel;
  gint buttons_number;
  gint axes_number;
  guint i;

  g_return_val_if_fail (file_name != NULL, NULL);

  self = g_object_new (GAMES_TYPE_LINUX_RAW_GAMEPAD, NULL);

  self->fd = open (file_name, O_RDONLY | O_NONBLOCK, (mode_t) 0);
  if (self->fd < 0) {
    g_set_error (error,
                 G_FILE_ERROR,
                 G_FILE_ERROR_FAILED,
                 _("Unable to open file “%s”: %s"),
                 file_name,
                 strerror (errno));
    g_object_unref (self);

    return NULL;
  }

  self->device = libevdev_new ();
  if (libevdev_set_fd (self->device, self->fd) < 0) {
    g_set_error (error,
                 G_FILE_ERROR,
                 G_FILE_ERROR_FAILED,
                 _("Evdev is unable to open “%s”: %s"),
                 file_name,
                 strerror (errno));
    g_object_unref (self);

    return NULL;
  }

  if (!is_joystick (self->device)) {
    g_set_error (error,
                 G_FILE_ERROR,
                 G_FILE_ERROR_NXIO,
                 "“%s” is not a joystick",
                 file_name);
    g_object_unref (self);

    return NULL;
  }

  self->event_source_id = -1;

  // Poll the events in the default main loop
  channel = g_io_channel_unix_new (self->fd);
  self->event_source_id = (glong) g_io_add_watch (channel, G_IO_IN, poll_events, self);
  buttons_number = 0;

  // Initialize dpads, buttons and axes
  for (i = BTN_JOYSTICK; i < KEY_MAX; i++)
    if (libevdev_has_event_code (self->device, (guint) EV_KEY, i)) {
      self->key_map[i - BTN_MISC] = (guint8) buttons_number;
      buttons_number++;
    }
  for (i = BTN_MISC; i < BTN_JOYSTICK; i++)
    if (libevdev_has_event_code (self->device, (guint) EV_KEY, i)) {
      self->key_map[i - BTN_MISC] = (guint8) buttons_number;
      buttons_number++;
    }

  // Get info about axes
  axes_number = 0;
  for (i = 0; i < ABS_MAX; i++) {
    // Skip dpads
    if (i == ABS_HAT0X) {
      i = ABS_HAT3Y;

      continue;
    }
    if (libevdev_has_event_code (self->device, (guint) EV_ABS, i)) {
      const struct input_absinfo *absinfo;

      absinfo = libevdev_get_abs_info (self->device, i);
      if (absinfo != NULL) {
        self->abs_map[i] = (guint8) axes_number;
        self->abs_info[axes_number] = *absinfo;
        axes_number++;
      }
    }
  }

  g_io_channel_unref (channel);

  return self;
}

/* Type */

static void
games_linux_raw_gamepad_finalize (GObject *object)
{
  GamesLinuxRawGamepad *self = GAMES_LINUX_RAW_GAMEPAD (object);

  close (self->fd);
  remove_event_source (self);
  if (self->device != NULL)
    libevdev_free (self->device);
  if (self->guid == NULL)
    g_free (self->guid);

  G_OBJECT_CLASS (games_linux_raw_gamepad_parent_class)->finalize (object);
}

static void
games_linux_raw_gamepad_class_init (GamesLinuxRawGamepadClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = games_linux_raw_gamepad_finalize;
}

static void
games_raw_gamepad_interface_init (GamesRawGamepadInterface *interface)
{
  interface->get_guid = get_guid;
}

static void
games_linux_raw_gamepad_init (GamesLinuxRawGamepad *self)
{
  self->event_source_id = -1;
}