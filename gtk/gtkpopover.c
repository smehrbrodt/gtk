/* GTK - The GIMP Toolkit
 * Copyright © 2013 Carlos Garnacho <carlosg@gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:gtkpopover
 * @Short_description: Context dependent bubbles
 * @Title: GtkPopover
 *
 * GtkPopover is a bubble-like context window, primarily meant to
 * provide context-dependent information or options. Popovers are
 * attached to a widget, passed at construction time on gtk_popover_new(),
 * or updated afterwards through gtk_popover_set_relative_to(), by
 * default they will point to the whole widget area, although this
 * behavior can be changed through gtk_popover_set_pointing_to().
 *
 * The position of a popover relative to the widget it is attached to
 * can also be changed through gtk_popover_set_position().
 *
 * By default, #GtkPopover performs a GTK+ grab, in order to ensure
 * input events get redirected to it while it is shown, and also so
 * the popover is dismissed in the expected situations (clicks outside
 * the popover, or the Esc key being pressed). If no such modal behavior
 * is desired on a popover, gtk_popover_set_modal() may be called on it
 * to tweak its behavior.
 *
 * ## GtkPopover as menu replacement
 *
 * GtkPopover is often used to replace menus. To facilitate this, it
 * supports being populated from a #GMenuModel, using
 * gtk_popover_new_from_model(). In addition to all the regular menu
 * model features, this function supports rendering sections in the
 * model in a more compact form, as a row of icon buttons instead of
 * menu items.
 *
 * To use this rendering, set the ”display-hint” attribute of the
 * section to ”horizontal-buttons” and set the icons of your items
 * with the ”verb-icon” attribute.
 *
 * |[
 * <section>
 *   <attribute name="display-hint">horizontal-buttons</attribute>
 *   <item>
 *     <attribute name="label">Cut</attribute>
 *     <attribute name="action">app.cut</attribute>
 *     <attribute name="verb-icon">edit-cut-symbolic</attribute>
 *   </item>
 *   <item>
 *     <attribute name="label">Copy</attribute>
 *     <attribute name="action">app.copy</attribute>
 *     <attribute name="verb-icon">edit-copy-symbolic</attribute>
 *   </item>
 *   <item>
 *     <attribute name="label">Paste</attribute>
 *     <attribute name="action">app.paste</attribute>
 *     <attribute name="verb-icon">edit-paste-symbolic</attribute>
 *   </item>
 * </section>
 * ]|
 *
 * # CSS nodes
 *
 * |[<!-- language="plain" -->
 * popover
 * ├── arrow
 * ╰── contents.background[.menu]
 *     ╰── <child>
 * ]|
 *
 * The contents child node always gets the .background style class and it
 * gets the .menu style class if the popover is menu-like (e.g. #GtkPopoverMenu
 * or created using gtk_popover_new_from_model().
 *
 * Particular uses of GtkPopover, such as touch selection popups
 * or magnifiers in #GtkEntry or #GtkTextView get style classes
 * like .touch-selection or .magnifier to differentiate from
 * plain popovers.
 *
 * When styling a popover directly, the popover node should usually not have any
 * background.
 *
 * Note that, in order to accomplish appropriate arrow visuals, #GtkPopover uses
 * custom drawing for the arrow node. This makes it possible for the arrow to change
 * its shape dynamically, but it also limits the possibilities of styling it using CSS.
 * In particular, the arrow gets drawn over the content node's border so they look
 * like one shape, which means that the border-width of the content node and the arrow
 * node should be the same. The arrow also does not support any border shape other than
 * solid, no border-radius, only one border width (border-bottom-width is used) and no box-shadow.
 */

#include "config.h"
#include <gdk/gdk.h>
#include "gtkpopover.h"
#include "gtkpopoverprivate.h"
#include "gtktypebuiltins.h"
#include "gtkmain.h"
#include "gtkwindowprivate.h"
#include "gtkscrollable.h"
#include "gtkadjustment.h"
#include "gtkprivate.h"
#include "gtkintl.h"
#include "gtklabel.h"
#include "gtkbox.h"
#include "gtkbutton.h"
#include "gtkcontainerprivate.h"
#include "gtkseparator.h"
#include "gtkmodelbutton.h"
#include "gtkwidgetprivate.h"
#include "gtkactionmuxerprivate.h"
#include "gtkmenutracker.h"
#include "gtkstack.h"
#include "gtksizegroup.h"
#include "a11y/gtkpopoveraccessible.h"
#include "gtkmenusectionbox.h"
#include "gtkroundedboxprivate.h"
#include "gtkstylecontextprivate.h"
#include "gtkprogresstrackerprivate.h"
#include "gtksettingsprivate.h"
#include "gtkgizmoprivate.h"
#include "gtkcssnodeprivate.h"

#include "gsk/gskroundedrectprivate.h"

#ifdef GDK_WINDOWING_WAYLAND
#include "wayland/gdkwayland.h"
#endif

#define TAIL_GAP_WIDTH  24
#define TAIL_HEIGHT     12
#define TRANSITION_DIFF 20
#define TRANSITION_DURATION 150 * 1000

#define POS_IS_VERTICAL(p) ((p) == GTK_POS_TOP || (p) == GTK_POS_BOTTOM)

enum {
  PROP_RELATIVE_TO = 1,
  PROP_POINTING_TO,
  PROP_POSITION,
  PROP_MODAL,
  PROP_CONSTRAIN_TO,
  NUM_PROPERTIES
};

enum {
  CLOSED,
  N_SIGNALS
};

enum {
  STATE_SHOWING,
  STATE_SHOWN,
  STATE_HIDING,
  STATE_HIDDEN
};

struct _GtkPopoverPrivate
{
  GtkWidget *widget;
  GtkWidget *contents_widget;
  GtkCssNode *arrow_node;
  GtkWindow *window;
  GtkWidget *prev_focus_widget;
  GtkWidget *default_widget;
  GtkWidget *prev_default;
  GtkScrollable *parent_scrollable;
  GtkAdjustment *vadj;
  GtkAdjustment *hadj;
  GdkRectangle pointing_to;
  GtkPopoverConstraint constraint;
  GtkProgressTracker tracker;
  guint prev_focus_unmap_id;
  guint hierarchy_changed_id;
  guint size_allocate_id;
  guint unmap_id;
  guint scrollable_notify_id;
  guint grab_notify_id;
  guint state_changed_id;
  guint has_pointing_to    : 1;
  guint preferred_position : 2;
  guint final_position     : 2;
  guint current_position   : 2;
  guint modal              : 1;
  guint button_pressed     : 1;
  guint grab_notify_blocked : 1;
  guint state               : 2;
  guint visible             : 1;
  guint first_frame_skipped : 1;
  gint transition_diff;
  guint tick_id;

  gint tip_x;
  gint tip_y;
};

static GParamSpec *properties[NUM_PROPERTIES];
static GQuark quark_widget_popovers = 0;
static guint signals[N_SIGNALS] = { 0 };

static void gtk_popover_update_relative_to (GtkPopover *popover,
                                            GtkWidget  *relative_to);
static void gtk_popover_set_state          (GtkPopover *popover,
                                            guint       state);
static void gtk_popover_apply_modality     (GtkPopover *popover,
                                            gboolean    modal);

static void gtk_popover_set_scrollable_full (GtkPopover    *popover,
                                             GtkScrollable *scrollable);

G_DEFINE_TYPE_WITH_PRIVATE (GtkPopover, gtk_popover, GTK_TYPE_BIN)


static void
measure_contents (GtkGizmo       *gizmo,
                  GtkOrientation  orientation,
                  int             for_size,
                  int            *minimum,
                  int            *natural,
                  int            *minimum_baseline,
                  int            *natural_baseline)
{
  GtkPopover *popover = GTK_POPOVER (gtk_widget_get_parent (GTK_WIDGET (gizmo)));
  GtkWidget *child = gtk_bin_get_child (GTK_BIN (popover));

  if (child)
    gtk_widget_measure (child, orientation, for_size,
                        minimum, natural,
                        minimum_baseline, natural_baseline);
}

static void
allocate_contents (GtkGizmo            *gizmo,
                   const GtkAllocation *allocation,
                   int                  baseline,
                   GtkAllocation       *out_clip)
{
  GtkPopover *popover = GTK_POPOVER (gtk_widget_get_parent (GTK_WIDGET (gizmo)));
  GtkWidget *child = gtk_bin_get_child (GTK_BIN (popover));

  if (child)
    gtk_widget_size_allocate (child, allocation, -1, out_clip);
}

static void
node_style_changed_cb (GtkCssNode        *node,
                       GtkCssStyleChange *change,
                       GtkWidget         *widget)
{
  if (gtk_css_style_change_affects (change, GTK_CSS_AFFECTS_SIZE | GTK_CSS_AFFECTS_CLIP))
    gtk_widget_queue_resize (widget);
  else
    gtk_widget_queue_draw (widget);
}

static void
gtk_popover_init (GtkPopover *popover)
{
  GtkWidget *widget;
  GtkStyleContext *context;

  widget = GTK_WIDGET (popover);
  gtk_widget_set_has_window (widget, TRUE);
  popover->priv = gtk_popover_get_instance_private (popover);
  popover->priv->modal = TRUE;
  popover->priv->tick_id = 0;
  popover->priv->state = STATE_HIDDEN;
  popover->priv->visible = FALSE;
  popover->priv->preferred_position = GTK_POS_TOP;
  popover->priv->constraint = GTK_POPOVER_CONSTRAINT_WINDOW;

  popover->priv->arrow_node = gtk_css_node_new ();
  gtk_css_node_set_name (popover->priv->arrow_node, I_("arrow"));
  gtk_css_node_set_parent (popover->priv->arrow_node, gtk_widget_get_css_node (widget));
  gtk_css_node_set_state (popover->priv->arrow_node,
                          gtk_css_node_get_state (gtk_widget_get_css_node (widget)));
  g_signal_connect_object (popover->priv->arrow_node, "style-changed",
                           G_CALLBACK (node_style_changed_cb), popover, 0);
  g_object_unref (popover->priv->arrow_node);

  popover->priv->contents_widget = gtk_gizmo_new ("contents",
                                                  measure_contents,
                                                  allocate_contents,
                                                  NULL);

  gtk_widget_set_parent (popover->priv->contents_widget, widget);

  context = gtk_widget_get_style_context (popover->priv->contents_widget);
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_BACKGROUND);
}

static void
gtk_popover_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  switch (prop_id)
    {
    case PROP_RELATIVE_TO:
      gtk_popover_set_relative_to (GTK_POPOVER (object),
                                   g_value_get_object (value));
      break;
    case PROP_POINTING_TO:
      gtk_popover_set_pointing_to (GTK_POPOVER (object),
                                   g_value_get_boxed (value));
      break;
    case PROP_POSITION:
      gtk_popover_set_position (GTK_POPOVER (object),
                                g_value_get_enum (value));
      break;
    case PROP_MODAL:
      gtk_popover_set_modal (GTK_POPOVER (object),
                             g_value_get_boolean (value));
      break;
    case PROP_CONSTRAIN_TO:
      gtk_popover_set_constrain_to (GTK_POPOVER (object),
                                    g_value_get_enum (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtk_popover_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  GtkPopoverPrivate *priv = GTK_POPOVER (object)->priv;

  switch (prop_id)
    {
    case PROP_RELATIVE_TO:
      g_value_set_object (value, priv->widget);
      break;
    case PROP_POINTING_TO:
      g_value_set_boxed (value, &priv->pointing_to);
      break;
    case PROP_POSITION:
      g_value_set_enum (value, priv->preferred_position);
      break;
    case PROP_MODAL:
      g_value_set_boolean (value, priv->modal);
      break;
    case PROP_CONSTRAIN_TO:
      g_value_set_enum (value, priv->constraint);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static gboolean
transitions_enabled (GtkPopover *popover)
{
  return gtk_settings_get_enable_animations (gtk_widget_get_settings (GTK_WIDGET (popover)));
}

static void
gtk_popover_hide_internal (GtkPopover *popover)
{
  GtkPopoverPrivate *priv = gtk_popover_get_instance_private (popover);
  GtkWidget *widget = GTK_WIDGET (popover);

  if (!priv->visible)
    return;

  priv->visible = FALSE;
  g_signal_emit (widget, signals[CLOSED], 0);

  if (priv->modal)
    gtk_popover_apply_modality (popover, FALSE);

  if (gtk_widget_get_realized (widget))
    {
      cairo_region_t *region = cairo_region_create ();
      gdk_window_input_shape_combine_region (gtk_widget_get_window (widget),
                                             region, 0, 0);
      cairo_region_destroy (region);
    }
}

static void
gtk_popover_finalize (GObject *object)
{
  GtkPopover *popover = GTK_POPOVER (object);
  GtkPopoverPrivate *priv = popover->priv;

  if (priv->widget)
    gtk_popover_update_relative_to (popover, NULL);

  G_OBJECT_CLASS (gtk_popover_parent_class)->finalize (object);
}

static void
popover_unset_prev_focus (GtkPopover *popover)
{
  GtkPopoverPrivate *priv = popover->priv;

  if (!priv->prev_focus_widget)
    return;

  if (priv->prev_focus_unmap_id)
    {
      g_signal_handler_disconnect (priv->prev_focus_widget,
                                   priv->prev_focus_unmap_id);
      priv->prev_focus_unmap_id = 0;
    }

  g_clear_object (&priv->prev_focus_widget);
}

static void
gtk_popover_dispose (GObject *object)
{
  GtkPopover *popover = GTK_POPOVER (object);
  GtkPopoverPrivate *priv = popover->priv;

  if (priv->modal)
    gtk_popover_apply_modality (popover, FALSE);

  if (priv->window)
    {
      g_signal_handlers_disconnect_by_data (priv->window, popover);
      _gtk_window_remove_popover (priv->window, GTK_WIDGET (object));
    }

  priv->window = NULL;

  if (priv->widget)
    gtk_popover_update_relative_to (popover, NULL);

  popover_unset_prev_focus (popover);

  g_clear_object (&priv->default_widget);

  if (priv->contents_widget)
    {
      GtkWidget *child = gtk_bin_get_child (GTK_BIN (popover));

      if (child)
        {
          /* Parent is content_widget! */
          gtk_widget_unparent (child);
          _gtk_bin_set_child (GTK_BIN (popover), NULL);
        }

      gtk_widget_unparent (priv->contents_widget);
      priv->contents_widget = NULL;
    }

  G_OBJECT_CLASS (gtk_popover_parent_class)->dispose (object);
}

static void
gtk_popover_realize (GtkWidget *widget)
{
  GtkAllocation allocation;
  GdkWindow *window;

  gtk_widget_get_window_allocation (widget, &allocation);

  /* We want to use subsurfaces for popovers, so they can extend outside
   * the main window, but for that, we first need to have clean subsurface
   * support that works with GSK.
   */
#if 0
  if (GDK_IS_WAYLAND_DISPLAY (gtk_widget_get_display (widget)))
    {
      GtkWidget *toplevel = gtk_widget_get_toplevel (widget);

      g_assert (GTK_IS_WINDOW (toplevel));

      window = gdk_wayland_window_new_subsurface (gtk_widget_get_display (toplevel),
                                                  &allocation);

      gdk_window_set_transient_for (window,
                                    gtk_widget_get_window (toplevel));
    }
  else
#endif
    {
      window = gdk_window_new_child (gtk_widget_get_parent_window (widget),
                                     &allocation);
    }

  gtk_widget_set_window (widget, window);
  gtk_widget_register_window (widget, window);
  gtk_widget_set_realized (widget, TRUE);
}

static void
window_active_changed (GtkWindow  *window,
                       GParamSpec *pspec,
                       GtkPopover *popover)
{
  GtkPopoverPrivate *priv = gtk_popover_get_instance_private (popover);

  if (!priv->modal ||
      !gtk_widget_is_drawable (GTK_WIDGET (popover)))
    return;

  if (gtk_window_is_active (window))
    {
      /* Regain the grab when the window is focused */
      GtkWidget *focus;

      gtk_grab_add (GTK_WIDGET (popover));

      focus = gtk_window_get_focus (window);

      if (focus == NULL || !gtk_widget_is_ancestor (focus, GTK_WIDGET (popover)))
        gtk_widget_grab_focus (GTK_WIDGET (popover));

      if (priv->grab_notify_blocked)
        g_signal_handler_unblock (priv->widget, priv->grab_notify_id);

      priv->grab_notify_blocked = FALSE;
    }
  else
    {
      /* Temporarily remove the grab when unfocused */
      g_signal_handler_block (priv->widget, priv->grab_notify_id);
      gtk_grab_remove (GTK_WIDGET (popover));

      priv->grab_notify_blocked = TRUE;
    }
}

static void
window_set_focus (GtkWindow  *window,
                  GtkWidget  *widget,
                  GtkPopover *popover)
{
  GtkPopoverPrivate *priv = gtk_popover_get_instance_private (popover);

  if (!priv->modal || !widget || !gtk_widget_is_drawable (GTK_WIDGET (popover)))
    return;

  widget = gtk_widget_get_ancestor (widget, GTK_TYPE_POPOVER);
  while (widget != NULL)
    {
      if (widget == GTK_WIDGET (popover))
        return;

      widget = gtk_popover_get_relative_to (GTK_POPOVER (widget));
      if (widget == NULL)
        break;
      widget = gtk_widget_get_ancestor (widget, GTK_TYPE_POPOVER);
    }

  popover_unset_prev_focus (popover);
  gtk_widget_hide (GTK_WIDGET (popover));
}

static void
prev_focus_unmap_cb (GtkWidget  *widget,
                     GtkPopover *popover)
{
  popover_unset_prev_focus (popover);
}

static void
gtk_popover_apply_modality (GtkPopover *popover,
                            gboolean    modal)
{
  GtkPopoverPrivate *priv = popover->priv;

  if (!priv->window)
    return;

  if (modal)
    {
      GtkWidget *prev_focus;

      prev_focus = gtk_window_get_focus (priv->window);
      priv->prev_focus_widget = prev_focus;
      if (priv->prev_focus_widget)
        {
          priv->prev_focus_unmap_id =
            g_signal_connect (prev_focus, "unmap",
                              G_CALLBACK (prev_focus_unmap_cb), popover);
          g_object_ref (prev_focus);
        }
      gtk_grab_add (GTK_WIDGET (popover));
      gtk_window_set_focus (priv->window, NULL);
      gtk_widget_grab_focus (GTK_WIDGET (popover));

      g_signal_connect (priv->window, "notify::is-active",
                        G_CALLBACK (window_active_changed), popover);
      g_signal_connect (priv->window, "set-focus",
                        G_CALLBACK (window_set_focus), popover);
    }
  else
    {
      g_signal_handlers_disconnect_by_data (priv->window, popover);
      if (priv->prev_focus_widget == GTK_WIDGET (priv->window))
        priv->prev_focus_unmap_id = 0;
      gtk_grab_remove (GTK_WIDGET (popover));

      /* Let prev_focus_widget regain focus */
      if (priv->prev_focus_widget &&
          gtk_widget_is_drawable (priv->prev_focus_widget))
        {
           if (GTK_IS_ENTRY (priv->prev_focus_widget))
             gtk_entry_grab_focus_without_selecting (GTK_ENTRY (priv->prev_focus_widget));
           else
             gtk_widget_grab_focus (priv->prev_focus_widget);
        }
      else if (priv->window)
        gtk_widget_grab_focus (GTK_WIDGET (priv->window));

      popover_unset_prev_focus (popover);
    }
}

static gboolean
show_animate_cb (GtkWidget     *widget,
                 GdkFrameClock *frame_clock,
                 gpointer       user_data)
{
  GtkPopover *popover = GTK_POPOVER (widget);
  GtkPopoverPrivate *priv = gtk_popover_get_instance_private (popover);
  gdouble t;

  if (priv->first_frame_skipped)
    gtk_progress_tracker_advance_frame (&priv->tracker,
                                        gdk_frame_clock_get_frame_time (frame_clock));
  else
    priv->first_frame_skipped = TRUE;

  t = gtk_progress_tracker_get_ease_out_cubic (&priv->tracker, FALSE);

  if (priv->state == STATE_SHOWING)
    {
      priv->transition_diff = TRANSITION_DIFF - (TRANSITION_DIFF * t);
      gtk_widget_set_opacity (widget, t);
    }
  else if (priv->state == STATE_HIDING)
    {
      priv->transition_diff = -TRANSITION_DIFF * t;
      gtk_widget_set_opacity (widget, 1.0 - t);
    }

  gtk_popover_update_position (popover);
  gtk_widget_queue_allocate (GTK_WIDGET (priv->window));

  if (gtk_progress_tracker_get_state (&priv->tracker) == GTK_PROGRESS_STATE_AFTER)
    {
      if (priv->state == STATE_SHOWING)
        {
          gtk_popover_set_state (popover, STATE_SHOWN);

          if (!priv->visible)
            gtk_popover_set_state (popover, STATE_HIDING);
        }
      else
        {
          gtk_widget_hide (widget);
        }

      priv->tick_id = 0;
      return G_SOURCE_REMOVE;
    }
  else
    return G_SOURCE_CONTINUE;
}

static void
gtk_popover_stop_transition (GtkPopover *popover)
{
  GtkPopoverPrivate *priv = popover->priv;

  if (priv->tick_id != 0)
    {
      gtk_widget_remove_tick_callback (GTK_WIDGET (popover), priv->tick_id);
      priv->tick_id = 0;
    }
}

static void
gtk_popover_start_transition (GtkPopover *popover)
{
  GtkPopoverPrivate *priv = popover->priv;

  if (priv->tick_id != 0)
    return;

  priv->first_frame_skipped = FALSE;
  gtk_progress_tracker_start (&priv->tracker, TRANSITION_DURATION, 0, 1.0);
  priv->tick_id = gtk_widget_add_tick_callback (GTK_WIDGET (popover),
                                                show_animate_cb,
                                                popover, NULL);
}

static void
gtk_popover_set_state (GtkPopover *popover,
                       guint       state)
{
  GtkPopoverPrivate *priv = popover->priv;

  if (!transitions_enabled (popover) ||
      !gtk_widget_get_realized (GTK_WIDGET (popover)))
    {
      if (state == STATE_SHOWING)
        state = STATE_SHOWN;
      else if (state == STATE_HIDING)
        state = STATE_HIDDEN;
    }

  priv->state = state;

  if (state == STATE_SHOWING || state == STATE_HIDING)
    gtk_popover_start_transition (popover);
  else
    {
      gtk_popover_stop_transition (popover);

      gtk_widget_set_visible (GTK_WIDGET (popover), state == STATE_SHOWN);
    }
}

static void
gtk_popover_map (GtkWidget *widget)
{
  GtkPopoverPrivate *priv = GTK_POPOVER (widget)->priv;

  priv->prev_default = gtk_window_get_default_widget (priv->window);
  if (priv->prev_default)
    g_object_ref (priv->prev_default);

  GTK_WIDGET_CLASS (gtk_popover_parent_class)->map (widget);

  gdk_window_show (gtk_widget_get_window (widget));
  gtk_popover_update_position (GTK_POPOVER (widget));

  gtk_window_set_default (priv->window, priv->default_widget);
}

static void
gtk_popover_unmap (GtkWidget *widget)
{
  GtkPopoverPrivate *priv = GTK_POPOVER (widget)->priv;

  priv->button_pressed = FALSE;

  gdk_window_hide (gtk_widget_get_window (widget));
  GTK_WIDGET_CLASS (gtk_popover_parent_class)->unmap (widget);

  if (gtk_window_get_default_widget (priv->window) == priv->default_widget)
    gtk_window_set_default (priv->window, priv->prev_default);
  g_clear_object (&priv->prev_default);
}

static GtkPositionType
get_effective_position (GtkPopover      *popover,
                        GtkPositionType  pos)
{
  if (_gtk_widget_get_direction (GTK_WIDGET (popover)) == GTK_TEXT_DIR_RTL)
    {
      if (pos == GTK_POS_LEFT)
        pos = GTK_POS_RIGHT;
      else if (pos == GTK_POS_RIGHT)
        pos = GTK_POS_LEFT;
    }

  return pos;
}

static void
get_margin (GtkWidget *widget,
            GtkBorder *border)
{
  GtkStyleContext *context;

  context = gtk_widget_get_style_context (widget);
  gtk_style_context_get_margin (context,
                                border);
}

static void
gtk_popover_get_gap_coords (GtkPopover      *popover,
                            gint            *initial_x_out,
                            gint            *initial_y_out,
                            gint            *tip_x_out,
                            gint            *tip_y_out,
                            gint            *final_x_out,
                            gint            *final_y_out)
{
  GtkWidget *widget = GTK_WIDGET (popover);
  GtkPopoverPrivate *priv = popover->priv;
  GdkRectangle rect = { 0 };
  gint base, tip, tip_pos;
  gint initial_x, initial_y;
  gint tip_x, tip_y;
  gint final_x, final_y;
  GtkPositionType pos;
  gint border_radius;
  GtkStyleContext *context;
  GtkBorder border;
  int popover_width, popover_height;

  gtk_popover_get_pointing_to (popover, &rect);
  popover_width = gtk_widget_get_width (widget);
  popover_height = gtk_widget_get_height (widget);

#ifdef GDK_WINDOWING_WAYLAND
  if (GDK_IS_WAYLAND_DISPLAY (gtk_widget_get_display (widget)))
    {
      gint win_x, win_y;

      gtk_widget_translate_coordinates (priv->widget, GTK_WIDGET (priv->window),
                                        rect.x, rect.y, &rect.x, &rect.y);
      gdk_window_get_origin (gtk_widget_get_window (GTK_WIDGET (popover)),
                             &win_x, &win_y);
      rect.x -= win_x;
      rect.y -= win_y;
    }
  else
#endif
    gtk_widget_translate_coordinates (priv->widget, widget,
                                      rect.x, rect.y, &rect.x, &rect.y);

  context = gtk_widget_get_style_context (priv->contents_widget);
  gtk_style_context_get_border (context, &border);

  pos = get_effective_position (popover, priv->final_position);

  gtk_style_context_get_border (context, &border);
  gtk_style_context_get (context,
                         GTK_STYLE_PROPERTY_BORDER_RADIUS, &border_radius,
                         NULL);

  if (pos == GTK_POS_BOTTOM || pos == GTK_POS_RIGHT)
    {
      tip = 0;
      base = tip + TAIL_HEIGHT + border.top;
    }
  else if (pos == GTK_POS_TOP)
    {
      base = popover_height - border.bottom - TAIL_HEIGHT;
      tip = base + TAIL_HEIGHT;
    }
  else if (pos == GTK_POS_LEFT)
    {
      base = popover_width - border.right - TAIL_HEIGHT;
      tip = base + TAIL_HEIGHT;
    }
  else
    g_assert_not_reached ();

  if (POS_IS_VERTICAL (pos))
    {
      tip_pos = rect.x + (rect.width / 2);
      initial_x = CLAMP (tip_pos - TAIL_GAP_WIDTH / 2,
                         border_radius,
                         popover_width - TAIL_GAP_WIDTH - border_radius);
      initial_y = base;

      tip_x = CLAMP (tip_pos, 0, popover_width);
      tip_y = tip;

      final_x = CLAMP (tip_pos + TAIL_GAP_WIDTH / 2,
                       border_radius + TAIL_GAP_WIDTH,
                       popover_width - border_radius);
      final_y = base;
    }
  else
    {
      tip_pos = rect.y + (rect.height / 2);

      initial_x = base;
      initial_y = CLAMP (tip_pos - TAIL_GAP_WIDTH / 2,
                         border_radius,
                         popover_height - TAIL_GAP_WIDTH - border_radius);

      tip_x = tip;
      tip_y = CLAMP (tip_pos, 0, popover_height);

      final_x = base;
      final_y = CLAMP (tip_pos + TAIL_GAP_WIDTH / 2,
                       border_radius + TAIL_GAP_WIDTH,
                       popover_height - border_radius);
    }

  *initial_x_out = initial_x;
  *initial_y_out = initial_y;

  *tip_x_out = tip_x;
  *tip_y_out = tip_y;

  *final_x_out = final_x;
  *final_y_out = final_y;
}

static void
gtk_popover_get_rect_for_size (GtkPopover   *popover,
                               int           popover_width,
                               int           popover_height,
                               GdkRectangle *rect)
{
  GtkWidget *widget = GTK_WIDGET (popover);
  int x, y, w, h;
  GtkBorder margin;

  get_margin (widget, &margin);

  x = 0;
  y = 0;
  w = popover_width;
  h = popover_height;

  x += MAX (TAIL_HEIGHT, margin.left);
  y += MAX (TAIL_HEIGHT, margin.top);
  w -= x + MAX (TAIL_HEIGHT, margin.right);
  h -= y + MAX (TAIL_HEIGHT, margin.bottom);

  rect->x = x;
  rect->y = y;
  rect->width = w;
  rect->height = h;
}

static void
gtk_popover_get_rect_coords (GtkPopover *popover,
                             int        *x_out,
                             int        *y_out,
                             int        *w_out,
                             int        *h_out)
{
  GtkWidget *widget = GTK_WIDGET (popover);
  GdkRectangle rect;
  GtkAllocation allocation;

  gtk_widget_get_allocation (widget, &allocation);
  gtk_popover_get_rect_for_size (popover, allocation.width, allocation.height, &rect);

  *x_out = rect.x;
  *y_out = rect.y;
  *w_out = rect.width;
  *h_out = rect.height;
}

static void
gtk_popover_apply_tail_path (GtkPopover *popover,
                             cairo_t    *cr)
{
  gint initial_x, initial_y;
  gint tip_x, tip_y;
  gint final_x, final_y;
  GtkStyleContext *context;
  GtkBorder border;

  if (!popover->priv->widget)
    return;

  context = gtk_widget_get_style_context (popover->priv->contents_widget);
  gtk_style_context_get_border (context, &border);

  cairo_set_line_width (cr, 1);
  gtk_popover_get_gap_coords (popover,
                              &initial_x, &initial_y,
                              &tip_x, &tip_y,
                              &final_x, &final_y);

  cairo_move_to (cr, initial_x, initial_y);
  cairo_line_to (cr, tip_x, tip_y);
  cairo_line_to (cr, final_x, final_y);
}

static void
gtk_popover_fill_border_path (GtkPopover *popover,
                              cairo_t    *cr)
{
  GtkWidget *widget = GTK_WIDGET (popover);
  GtkAllocation allocation;
  GtkStyleContext *context;
  int x, y, w, h;
  GskRoundedRect box;

  context = gtk_widget_get_style_context (widget);
  gtk_widget_get_allocation (widget, &allocation);

  cairo_set_source_rgba (cr, 0, 0, 0, 1);

  gtk_popover_apply_tail_path (popover, cr);
  cairo_close_path (cr);
  cairo_fill (cr);

  gtk_popover_get_rect_coords (popover, &x, &y, &w, &h);

  gtk_rounded_boxes_init_for_style (&box,
                                    NULL, NULL,
                                    gtk_style_context_lookup_style (context),
                                    x, y, w, h);
  gsk_rounded_rect_path (&box, cr);
  cairo_fill (cr);
}

static void
gtk_popover_update_shape (GtkPopover *popover)
{
  GtkWidget *widget = GTK_WIDGET (popover);
  cairo_surface_t *surface;
  cairo_region_t *region;
  GdkWindow *win;
  cairo_t *cr;

#ifdef GDK_WINDOWING_WAYLAND
  if (GDK_IS_WAYLAND_DISPLAY (gtk_widget_get_display (widget)))
    return;
#endif

  win = gtk_widget_get_window (widget);
  surface =
    gdk_window_create_similar_surface (win,
                                       CAIRO_CONTENT_COLOR_ALPHA,
                                       gdk_window_get_width (win),
                                       gdk_window_get_height (win));

  cr = cairo_create (surface);
  gtk_popover_fill_border_path (popover, cr);
  cairo_destroy (cr);

  region = gdk_cairo_region_create_from_surface (surface);
  cairo_surface_destroy (surface);

  gtk_widget_shape_combine_region (widget, region);
  cairo_region_destroy (region);

  gdk_window_set_child_shapes (gtk_widget_get_window (widget));
}

static void
_gtk_popover_update_child_visible (GtkPopover *popover)
{
  GtkPopoverPrivate *priv = popover->priv;
  GtkWidget *widget = GTK_WIDGET (popover);
  GdkRectangle rect;
  GtkAllocation allocation;
  GtkWidget *parent;

  if (!priv->parent_scrollable)
    {
      gtk_widget_set_child_visible (widget, TRUE);
      return;
    }

  parent = gtk_widget_get_parent (GTK_WIDGET (priv->parent_scrollable));
  rect = priv->pointing_to;

  gtk_widget_translate_coordinates (priv->widget, parent,
                                    rect.x, rect.y, &rect.x, &rect.y);

  gtk_widget_get_allocation (GTK_WIDGET (parent), &allocation);

  if (rect.x + rect.width < 0 || rect.x > allocation.width ||
      rect.y + rect.height < 0 || rect.y > allocation.height)
    gtk_widget_set_child_visible (widget, FALSE);
  else
    gtk_widget_set_child_visible (widget, TRUE);
}

static GtkPositionType
opposite_position (GtkPositionType pos)
{
  switch (pos)
    {
    default:
    case GTK_POS_LEFT: return GTK_POS_RIGHT;
    case GTK_POS_RIGHT: return GTK_POS_LEFT;
    case GTK_POS_TOP: return GTK_POS_BOTTOM;
    case GTK_POS_BOTTOM: return GTK_POS_TOP;
    }
}

void
gtk_popover_update_position (GtkPopover *popover)
{
  GtkPopoverPrivate *priv = popover->priv;
  GtkWidget *widget = GTK_WIDGET (popover);
  GtkAllocation window_alloc;
  GtkBorder window_shadow;
  GdkRectangle rect = { 0 };
  GtkRequisition req;
  GtkPositionType pos;
  gint overshoot[4];
  gint i, j;
  gint best;

  if (!priv->window)
    return;

  gtk_widget_get_preferred_size (widget, NULL, &req);
  gtk_widget_get_allocation (GTK_WIDGET (priv->window), &window_alloc);
  _gtk_window_get_shadow_width (priv->window, &window_shadow);
  priv->final_position = priv->preferred_position;

  gtk_popover_get_pointing_to (popover, &rect);
  gtk_widget_translate_coordinates (priv->widget, GTK_WIDGET (priv->window),
                                    rect.x, rect.y, &rect.x, &rect.y);

  pos = get_effective_position (popover, priv->preferred_position);

  overshoot[GTK_POS_TOP] = req.height - rect.y + window_shadow.top;
  overshoot[GTK_POS_BOTTOM] = rect.y + rect.height + req.height - window_alloc.height
                              + window_shadow.bottom;
  overshoot[GTK_POS_LEFT] = req.width - rect.x + window_shadow.left;
  overshoot[GTK_POS_RIGHT] = rect.x + rect.width + req.width - window_alloc.width
                             + window_shadow.right;

#ifdef GDK_WINDOWING_WAYLAND
  if (GDK_IS_WAYLAND_DISPLAY (gtk_widget_get_display (widget)) &&
      priv->constraint == GTK_POPOVER_CONSTRAINT_NONE)
    {
      priv->final_position = priv->preferred_position;
    }
  else
#endif
  if (overshoot[pos] <= 0)
    {
      priv->final_position = priv->preferred_position;
    }
  else if (overshoot[opposite_position (pos)] <= 0)
    {
      priv->final_position = opposite_position (priv->preferred_position);
    }
  else
    {
      best = G_MAXINT;
      pos = 0;
      for (i = 0; i < 4; i++)
        {
          j = get_effective_position (popover, i);
          if (overshoot[j] < best)
            {
              pos = i;
              best = overshoot[j];
            }
        }
      priv->final_position = pos;
    }

  switch (priv->final_position)
    {
    case GTK_POS_TOP:
      rect.y += priv->transition_diff;
      break;
    case GTK_POS_BOTTOM:
      rect.y -= priv->transition_diff;
      break;
    case GTK_POS_LEFT:
      rect.x += priv->transition_diff;
      break;
    case GTK_POS_RIGHT:
      rect.x -= priv->transition_diff;
      break;
    default:
      break;
    }

  _gtk_window_set_popover_position (priv->window, widget,
                                    priv->final_position, &rect);

  if (priv->final_position != priv->current_position)
    {
      if (gtk_widget_is_drawable (widget))
        gtk_popover_update_shape (popover);

      priv->current_position = priv->final_position;
    }

  _gtk_popover_update_child_visible (popover);
}

GtkWidget *
gtk_popover_get_contents_widget (GtkPopover *popover)
{
  GtkPopoverPrivate *priv = gtk_popover_get_instance_private (popover);

  return priv->contents_widget;
}

static void
gtk_popover_snapshot (GtkWidget   *widget,
                      GtkSnapshot *snapshot)
{
  GtkPopover *popover = GTK_POPOVER (widget);
  GtkPopoverPrivate *priv = gtk_popover_get_instance_private (popover);
  GtkStyleContext *context;
  GtkBorder border;
  graphene_rect_t bounds;
  cairo_t *cr;

  /* Draw the child first so we can draw the arrow (partially) over it */
  gtk_widget_snapshot_child (widget, priv->contents_widget, snapshot);

  graphene_rect_init (&bounds,
                      0, 0,
                      gtk_widget_get_width (widget),
                      gtk_widget_get_height (widget));
  cr = gtk_snapshot_append_cairo (snapshot,
                                  &bounds,
                                  "Popover");

  /* Clip to the arrow shape */
  cairo_save (cr);

  gtk_popover_apply_tail_path (popover, cr);

  cairo_clip (cr);

  context = gtk_widget_get_style_context (widget);
  gtk_style_context_save_to_node (context, priv->arrow_node);
  gtk_style_context_get_border (context, &border);

  /* Render the arrow background */
  gtk_render_background (context, cr,
                         0, 0,
                         gtk_widget_get_width (widget),
                         gtk_widget_get_height (widget));

  /* Render the border of the arrow tip */
  if (border.bottom > 0)
    {
      GdkRGBA border_color;
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      gtk_style_context_get_border_color (context, &border_color);
G_GNUC_END_IGNORE_DEPRECATIONS

      gtk_popover_apply_tail_path (popover, cr);
      gdk_cairo_set_source_rgba (cr, &border_color);

      cairo_set_line_width (cr, border.bottom + 1);
      cairo_stroke (cr);
    }

  cairo_restore (cr);
  /* We're done */
  cairo_destroy (cr);

  gtk_style_context_restore (context);
}

static gint
get_border_radius (GtkWidget *widget)
{
  GtkStyleContext *context;
  gint border_radius;

  context = gtk_widget_get_style_context (widget);
  gtk_style_context_get (context,
                         GTK_STYLE_PROPERTY_BORDER_RADIUS, &border_radius,
                         NULL);
  return border_radius;
}

static gint
get_minimal_size (GtkPopover     *popover,
                  GtkOrientation  orientation)
{
  GtkPopoverPrivate *priv = popover->priv;
  GtkPositionType pos;
  gint minimal_size;

  minimal_size = 2 * get_border_radius (GTK_WIDGET (popover));
  pos = get_effective_position (popover, priv->preferred_position);

  if ((orientation == GTK_ORIENTATION_HORIZONTAL && POS_IS_VERTICAL (pos)) ||
      (orientation == GTK_ORIENTATION_VERTICAL && !POS_IS_VERTICAL (pos)))
    minimal_size += TAIL_GAP_WIDTH;

  return minimal_size;
}

static void
gtk_popover_measure (GtkWidget      *widget,
                     GtkOrientation  orientation,
                     int             for_size,
                     int            *minimum,
                     int            *natural,
                     int            *minimum_baseline,
                     int            *natural_baseline)
{
  GtkPopover *popover = GTK_POPOVER (widget);
  GtkPopoverPrivate *priv = gtk_popover_get_instance_private (popover);
  GtkWidget *child = gtk_bin_get_child (GTK_BIN (widget));
  int minimal_size;

  *minimum = 0;
  *natural = 0;

  if (child != NULL)
    {
      if (for_size >= 0)
        for_size -= TAIL_HEIGHT;

      gtk_widget_measure (priv->contents_widget, orientation, for_size, minimum, natural, NULL, NULL);
    }

  minimal_size = get_minimal_size (popover, orientation);
  *minimum = MAX (*minimum, minimal_size);
  *natural = MAX (*natural, minimal_size);

  *minimum += TAIL_HEIGHT;
  *natural += TAIL_HEIGHT;
}

static void
gtk_popover_size_allocate (GtkWidget           *widget,
                           const GtkAllocation *allocation,
                           int                  baseline,
                           GtkAllocation       *out_clip)
{
  GtkPopover *popover = GTK_POPOVER (widget);
  GtkPopoverPrivate *priv = gtk_popover_get_instance_private (popover);
  GtkAllocation child_alloc = *allocation;

  /* Note that we in measure() we add TAIL_HEIGHT in both directions, regardless
   * of the popovers position. This is to ensure that we get enough space
   * even priv->current_position changes between measure and size-allocate.
   */
  child_alloc.height -= TAIL_HEIGHT;
  child_alloc.width -= TAIL_HEIGHT;

  switch (priv->current_position)
    {
    case GTK_POS_TOP:
      child_alloc.x += TAIL_HEIGHT / 2;
      break;
    case GTK_POS_BOTTOM:
      child_alloc.x += TAIL_HEIGHT / 2;
      child_alloc.y += TAIL_HEIGHT;
      break;
    case GTK_POS_LEFT:
      child_alloc.y += TAIL_HEIGHT / 2;
      break;
    case GTK_POS_RIGHT:
      child_alloc.x += TAIL_HEIGHT;
      child_alloc.y += TAIL_HEIGHT / 2;
      break;
    default:
      break;
    }

  gtk_widget_size_allocate (priv->contents_widget, &child_alloc, -1, out_clip);

  if (gtk_widget_get_realized (widget))
    {
      GtkAllocation a;
      gtk_widget_get_window_allocation (widget, &a);
      gdk_window_move_resize (gtk_widget_get_window (widget),
                              a.x, a.y, a.width, a.height);
      gtk_popover_update_shape (popover);
    }
}

static gboolean
gtk_popover_event (GtkWidget *widget,
                   GdkEvent  *event)
{
  GtkPopover *popover = GTK_POPOVER (widget);

  if (gdk_event_get_event_type (event) == GDK_BUTTON_PRESS)
    popover->priv->button_pressed = TRUE;
  else if (gdk_event_get_event_type (event) == GDK_BUTTON_RELEASE)
    {
      GtkAllocation child_alloc;
      GtkWidget *child;
      gdouble x, y;

      child = gtk_bin_get_child (GTK_BIN (widget));

      if (!popover->priv->button_pressed ||
          !gdk_event_get_coords (event, &x, &y))
        return GDK_EVENT_PROPAGATE;

      gtk_widget_get_allocation (child, &child_alloc);

      if (x < child_alloc.x ||
          x > child_alloc.x + child_alloc.width ||
          y < child_alloc.y ||
          y > child_alloc.y + child_alloc.height)
        gtk_popover_popdown (popover);
    }

  return GDK_EVENT_PROPAGATE;
}

static gboolean
gtk_popover_key_press (GtkWidget   *widget,
                       GdkEventKey *event)
{
  GtkWidget *toplevel, *focus;
  guint keyval;

  if (!gdk_event_get_keyval ((GdkEvent *) event, &keyval))
    return GDK_EVENT_PROPAGATE;

  if (keyval == GDK_KEY_Escape)
    {
      gtk_popover_popdown (GTK_POPOVER (widget));
      return GDK_EVENT_STOP;
    }

  if (!GTK_POPOVER (widget)->priv->modal)
    return GDK_EVENT_PROPAGATE;

  toplevel = gtk_widget_get_toplevel (widget);

  if (GTK_IS_WINDOW (toplevel))
    {
      focus = gtk_window_get_focus (GTK_WINDOW (toplevel));

      if (focus && gtk_widget_is_ancestor (focus, widget))
        return gtk_widget_event (focus, (GdkEvent*) event);
    }

  return GDK_EVENT_PROPAGATE;
}

static void
gtk_popover_grab_focus (GtkWidget *widget)
{
  GtkPopoverPrivate *priv = GTK_POPOVER (widget)->priv;
  GtkWidget *child;

  if (!priv->visible)
    return;

  /* Focus the first natural child */
  child = gtk_bin_get_child (GTK_BIN (widget));

  if (child)
    gtk_widget_child_focus (child, GTK_DIR_TAB_FORWARD);
}

static gboolean
gtk_popover_focus (GtkWidget        *widget,
                   GtkDirectionType  direction)
{
  GtkPopover *popover = GTK_POPOVER (widget);
  GtkPopoverPrivate *priv = popover->priv;

  if (!priv->visible)
    return FALSE;

  if (!GTK_WIDGET_CLASS (gtk_popover_parent_class)->focus (widget, direction))
    {
      GtkWidget *focus;

      focus = gtk_window_get_focus (popover->priv->window);
      focus = gtk_widget_get_parent (focus);

      /* Unset focus child through children, so it is next stepped from
       * scratch.
       */
      while (focus && focus != widget)
        {
          gtk_widget_set_focus_child (focus, NULL);
          focus = gtk_widget_get_parent (focus);
        }

      return gtk_widget_child_focus (gtk_bin_get_child (GTK_BIN (widget)),
                                     direction);
    }

  return TRUE;
}

static void
gtk_popover_show (GtkWidget *widget)
{
  GtkPopoverPrivate *priv = GTK_POPOVER (widget)->priv;

  if (priv->window)
    _gtk_window_raise_popover (priv->window, widget);

  priv->visible = TRUE;

  GTK_WIDGET_CLASS (gtk_popover_parent_class)->show (widget);

  if (priv->modal)
    gtk_popover_apply_modality (GTK_POPOVER (widget), TRUE);

  priv->state = STATE_SHOWN;

  if (gtk_widget_get_realized (widget))
    gdk_window_input_shape_combine_region (gtk_widget_get_window (widget),
                                           NULL, 0, 0);
}

static void
gtk_popover_hide (GtkWidget *widget)
{
  GtkPopoverPrivate *priv = GTK_POPOVER (widget)->priv;

  gtk_popover_hide_internal (GTK_POPOVER (widget));

  gtk_popover_stop_transition (GTK_POPOVER (widget));
  priv->state = STATE_HIDDEN;
  priv->transition_diff = 0;
  gtk_progress_tracker_finish (&priv->tracker);
  gtk_widget_set_opacity (widget, 1.0);


  GTK_WIDGET_CLASS (gtk_popover_parent_class)->hide (widget);
}

static void
gtk_popover_add (GtkContainer *container,
                 GtkWidget    *child)
{
  GtkPopover *popover = GTK_POPOVER (container);
  GtkPopoverPrivate *priv = gtk_popover_get_instance_private (popover);

  _gtk_bin_set_child (GTK_BIN (popover), child);

  gtk_widget_set_parent (child, priv->contents_widget);
}

static void
gtk_popover_remove (GtkContainer *container,
                    GtkWidget    *child)
{
  GtkPopover *popover = GTK_POPOVER (container);

  _gtk_bin_set_child (GTK_BIN (popover), NULL);
  gtk_widget_unparent (child);
}

static void
gtk_popover_state_flags_changed (GtkWidget     *widget,
                                 GtkStateFlags  previous_state)
{
  GtkPopover *popover = GTK_POPOVER (widget);
  GtkPopoverPrivate *priv = gtk_popover_get_instance_private (popover);
  GtkStateFlags state;

  state = gtk_widget_get_state_flags (widget);
  gtk_css_node_set_state (priv->arrow_node, state);

  GTK_WIDGET_CLASS (gtk_popover_parent_class)->state_flags_changed (widget, previous_state);
}

static void
gtk_popover_class_init (GtkPopoverClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);

  object_class->set_property = gtk_popover_set_property;
  object_class->get_property = gtk_popover_get_property;
  object_class->finalize = gtk_popover_finalize;
  object_class->dispose = gtk_popover_dispose;

  widget_class->realize = gtk_popover_realize;
  widget_class->map = gtk_popover_map;
  widget_class->unmap = gtk_popover_unmap;
  widget_class->measure = gtk_popover_measure;
  widget_class->size_allocate = gtk_popover_size_allocate;
  widget_class->snapshot = gtk_popover_snapshot;
  widget_class->event = gtk_popover_event;
  widget_class->key_press_event = gtk_popover_key_press;
  widget_class->grab_focus = gtk_popover_grab_focus;
  widget_class->focus = gtk_popover_focus;
  widget_class->show = gtk_popover_show;
  widget_class->hide = gtk_popover_hide;
  widget_class->state_flags_changed = gtk_popover_state_flags_changed;

  container_class->add = gtk_popover_add;
  container_class->remove = gtk_popover_remove;

  /**
   * GtkPopover:relative-to:
   *
   * Sets the attached widget.
   */
  properties[PROP_RELATIVE_TO] =
      g_param_spec_object ("relative-to",
                           P_("Relative to"),
                           P_("Widget the bubble window points to"),
                           GTK_TYPE_WIDGET,
                           GTK_PARAM_READWRITE);

  /**
   * GtkPopover:pointing-to:
   *
   * Marks a specific rectangle to be pointed.
   */
  properties[PROP_POINTING_TO] =
      g_param_spec_boxed ("pointing-to",
                          P_("Pointing to"),
                          P_("Rectangle the bubble window points to"),
                          GDK_TYPE_RECTANGLE,
                          GTK_PARAM_READWRITE);

  /**
   * GtkPopover:position
   *
   * Sets the preferred position of the popover.
   */
  properties[PROP_POSITION] =
      g_param_spec_enum ("position",
                         P_("Position"),
                         P_("Position to place the bubble window"),
                         GTK_TYPE_POSITION_TYPE, GTK_POS_TOP,
                         GTK_PARAM_READWRITE|G_PARAM_EXPLICIT_NOTIFY);

  /**
   * GtkPopover:modal
   *
   * Sets whether the popover is modal (so other elements in the window do not
   * receive input while the popover is visible).
   */
  properties[PROP_MODAL] =
      g_param_spec_boolean ("modal",
                            P_("Modal"),
                            P_("Whether the popover is modal"),
                            TRUE,
                            GTK_PARAM_READWRITE|G_PARAM_EXPLICIT_NOTIFY);

  /**
   * GtkPopover:constrain-to:
   *
   * Sets a constraint for the popover position.
   */
  properties[PROP_CONSTRAIN_TO] =
      g_param_spec_enum ("constrain-to",
                         P_("Constraint"),
                         P_("Constraint for the popover position"),
                         GTK_TYPE_POPOVER_CONSTRAINT, GTK_POPOVER_CONSTRAINT_WINDOW,
                         GTK_PARAM_READWRITE|G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, NUM_PROPERTIES, properties);

  signals[CLOSED] =
    g_signal_new (I_("closed"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GtkPopoverClass, closed),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  quark_widget_popovers = g_quark_from_static_string ("gtk-quark-widget-popovers");
  gtk_widget_class_set_accessible_type (widget_class, GTK_TYPE_POPOVER_ACCESSIBLE);
  gtk_widget_class_set_css_name (widget_class, I_("popover"));
}

static void
gtk_popover_update_scrollable (GtkPopover *popover)
{
  GtkPopoverPrivate *priv = popover->priv;
  GtkScrollable *scrollable;

  scrollable = GTK_SCROLLABLE (gtk_widget_get_ancestor (priv->widget,
                                                        GTK_TYPE_SCROLLABLE));
  gtk_popover_set_scrollable_full (popover, scrollable);
}

static void
_gtk_popover_parent_hierarchy_changed (GtkWidget  *widget,
                                       GtkWidget  *previous_toplevel,
                                       GtkPopover *popover)
{
  GtkPopoverPrivate *priv = popover->priv;
  GtkWindow *new_window;

  new_window = GTK_WINDOW (gtk_widget_get_ancestor (widget, GTK_TYPE_WINDOW));

  if (priv->window == new_window)
    return;

  g_object_ref (popover);

  if (gtk_widget_has_grab (GTK_WIDGET (popover)))
    gtk_popover_apply_modality (popover, FALSE);

  if (priv->window)
    _gtk_window_remove_popover (priv->window, GTK_WIDGET (popover));

  if (priv->parent_scrollable)
    gtk_popover_set_scrollable_full (popover, NULL);

  priv->window = new_window;

  if (new_window)
    {
      _gtk_window_add_popover (new_window, GTK_WIDGET (popover), priv->widget, TRUE);
      gtk_popover_update_scrollable (popover);
      gtk_popover_update_position (popover);
    }

  if (gtk_widget_is_visible (GTK_WIDGET (popover)))
    gtk_widget_queue_resize (GTK_WIDGET (popover));

  g_object_unref (popover);
}

static void
_popover_propagate_state (GtkPopover    *popover,
                          GtkStateFlags  state,
                          GtkStateFlags  old_state,
                          GtkStateFlags  flag)
{
  if ((state & flag) != (old_state & flag))
    {
      if ((state & flag) == flag)
        gtk_widget_set_state_flags (GTK_WIDGET (popover), flag, FALSE);
      else
        gtk_widget_unset_state_flags (GTK_WIDGET (popover), flag);
    }
}

static void
_gtk_popover_parent_state_changed (GtkWidget     *widget,
                                   GtkStateFlags  old_state,
                                   GtkPopover    *popover)
{
  guint state;

  state = gtk_widget_get_state_flags (widget);
  _popover_propagate_state (popover, state, old_state,
                            GTK_STATE_FLAG_INSENSITIVE);
  _popover_propagate_state (popover, state, old_state,
                            GTK_STATE_FLAG_BACKDROP);
}

static void
_gtk_popover_parent_grab_notify (GtkWidget  *widget,
                                 gboolean    was_shadowed,
                                 GtkPopover *popover)
{
  GtkPopoverPrivate *priv = popover->priv;

  if (priv->modal &&
      gtk_widget_is_visible (GTK_WIDGET (popover)) &&
      !gtk_widget_has_grab (GTK_WIDGET (popover)))
    {
      GtkWidget *grab_widget;

      grab_widget = gtk_grab_get_current ();

      if (!grab_widget || !GTK_IS_POPOVER (grab_widget))
        gtk_popover_popdown (popover);
    }
}

static void
_gtk_popover_parent_unmap (GtkWidget *widget,
                           GtkPopover *popover)
{
  GtkPopoverPrivate *priv = popover->priv;

  if (priv->state == STATE_SHOWING)
    priv->visible = FALSE;
  else if (priv->state == STATE_SHOWN)
    gtk_popover_set_state (popover, STATE_HIDING);
}

static void
_gtk_popover_parent_size_allocate (GtkWidget           *widget,
                                   const GtkAllocation *allocation,
                                   int                  baseline,
                                   GtkAllocation       *out_clip,
                                   GtkPopover          *popover)
{
  gtk_popover_update_position (popover);
}

static void
_unmanage_popover (GObject *object)
{
  gtk_popover_update_relative_to (GTK_POPOVER (object), NULL);
  g_object_unref (object);
}

static void
widget_manage_popover (GtkWidget  *widget,
                       GtkPopover *popover)
{
  GHashTable *popovers;

  popovers = g_object_get_qdata (G_OBJECT (widget), quark_widget_popovers);

  if (G_UNLIKELY (!popovers))
    {
      popovers = g_hash_table_new_full (NULL, NULL,
                                        (GDestroyNotify) _unmanage_popover, NULL);
      g_object_set_qdata_full (G_OBJECT (widget),
                               quark_widget_popovers, popovers,
                               (GDestroyNotify) g_hash_table_unref);
    }

  g_hash_table_add (popovers, g_object_ref_sink (popover));
}

static void
widget_unmanage_popover (GtkWidget  *widget,
                         GtkPopover *popover)
{
  GHashTable *popovers;

  popovers = g_object_get_qdata (G_OBJECT (widget), quark_widget_popovers);

  if (G_UNLIKELY (!popovers))
    return;

  g_hash_table_remove (popovers, popover);
}

static void
adjustment_changed_cb (GtkAdjustment *adjustment,
                       GtkPopover    *popover)
{
  gtk_popover_update_position (popover);
}

static void
_gtk_popover_set_scrollable (GtkPopover    *popover,
                             GtkScrollable *scrollable)
{
  GtkPopoverPrivate *priv = popover->priv;

  if (priv->parent_scrollable)
    {
      if (priv->vadj)
        {
          g_signal_handlers_disconnect_by_data (priv->vadj, popover);
          g_object_unref (priv->vadj);
          priv->vadj = NULL;
        }

      if (priv->hadj)
        {
          g_signal_handlers_disconnect_by_data (priv->hadj, popover);
          g_object_unref (priv->hadj);
          priv->hadj = NULL;
        }

      g_object_unref (priv->parent_scrollable);
    }

  priv->parent_scrollable = scrollable;

  if (scrollable)
    {
      g_object_ref (scrollable);
      priv->vadj = gtk_scrollable_get_vadjustment (scrollable);
      priv->hadj = gtk_scrollable_get_hadjustment (scrollable);

      if (priv->vadj)
        {
          g_object_ref (priv->vadj);
          g_signal_connect (priv->vadj, "changed",
                            G_CALLBACK (adjustment_changed_cb), popover);
          g_signal_connect (priv->vadj, "value-changed",
                            G_CALLBACK (adjustment_changed_cb), popover);
        }

      if (priv->hadj)
        {
          g_object_ref (priv->hadj);
          g_signal_connect (priv->hadj, "changed",
                            G_CALLBACK (adjustment_changed_cb), popover);
          g_signal_connect (priv->hadj, "value-changed",
                            G_CALLBACK (adjustment_changed_cb), popover);
        }
    }
}

static void
scrollable_notify_cb (GObject    *object,
                      GParamSpec *pspec,
                      GtkPopover *popover)
{
  if (pspec->value_type == GTK_TYPE_ADJUSTMENT)
    _gtk_popover_set_scrollable (popover, GTK_SCROLLABLE (object));
}

static void
gtk_popover_set_scrollable_full (GtkPopover    *popover,
                                 GtkScrollable *scrollable)
{
  GtkPopoverPrivate *priv = popover->priv;

  if (priv->scrollable_notify_id != 0 &&
      g_signal_handler_is_connected (priv->parent_scrollable, priv->scrollable_notify_id))
    {
      g_signal_handler_disconnect (priv->parent_scrollable, priv->scrollable_notify_id);
      priv->scrollable_notify_id = 0;
    }

  _gtk_popover_set_scrollable (popover, scrollable);

  if (scrollable)
    {
      priv->scrollable_notify_id =
        g_signal_connect (priv->parent_scrollable, "notify",
                          G_CALLBACK (scrollable_notify_cb), popover);
    }
}

static void
gtk_popover_update_relative_to (GtkPopover *popover,
                                GtkWidget  *relative_to)
{
  GtkPopoverPrivate *priv = popover->priv;

  if (priv->widget == relative_to)
    return;

  g_object_ref (popover);

  if (priv->window)
    {
      _gtk_window_remove_popover (priv->window, GTK_WIDGET (popover));
      priv->window = NULL;
    }

  popover_unset_prev_focus (popover);

  if (priv->widget)
    {
      if (g_signal_handler_is_connected (priv->widget, priv->hierarchy_changed_id))
        g_signal_handler_disconnect (priv->widget, priv->hierarchy_changed_id);
      if (g_signal_handler_is_connected (priv->widget, priv->size_allocate_id))
        g_signal_handler_disconnect (priv->widget, priv->size_allocate_id);
      if (g_signal_handler_is_connected (priv->widget, priv->unmap_id))
        g_signal_handler_disconnect (priv->widget, priv->unmap_id);
      if (g_signal_handler_is_connected (priv->widget, priv->state_changed_id))
        g_signal_handler_disconnect (priv->widget, priv->state_changed_id);
      if (g_signal_handler_is_connected (priv->widget, priv->grab_notify_id))
        g_signal_handler_disconnect (priv->widget, priv->grab_notify_id);

      widget_unmanage_popover (priv->widget, popover);
    }

  if (priv->parent_scrollable)
    gtk_popover_set_scrollable_full (popover, NULL);

  priv->widget = relative_to;
  g_object_notify_by_pspec (G_OBJECT (popover), properties[PROP_RELATIVE_TO]);

  if (priv->widget)
    {
      priv->window =
        GTK_WINDOW (gtk_widget_get_ancestor (priv->widget, GTK_TYPE_WINDOW));

      priv->hierarchy_changed_id =
        g_signal_connect (priv->widget, "hierarchy-changed",
                          G_CALLBACK (_gtk_popover_parent_hierarchy_changed),
                          popover);
      priv->size_allocate_id =
        g_signal_connect (priv->widget, "size-allocate",
                          G_CALLBACK (_gtk_popover_parent_size_allocate),
                          popover);
      priv->unmap_id =
        g_signal_connect (priv->widget, "unmap",
                          G_CALLBACK (_gtk_popover_parent_unmap),
                          popover);
      priv->state_changed_id =
        g_signal_connect (priv->widget, "state-flags-changed",
                          G_CALLBACK (_gtk_popover_parent_state_changed),
                          popover);
      priv->grab_notify_id =
        g_signal_connect (priv->widget, "grab-notify",
                          G_CALLBACK (_gtk_popover_parent_grab_notify),
                          popover);

      /* Give ownership of the popover to widget */
      widget_manage_popover (priv->widget, popover);
    }

  if (priv->window)
    _gtk_window_add_popover (priv->window, GTK_WIDGET (popover), priv->widget, TRUE);

  if (priv->widget)
    gtk_popover_update_scrollable (popover);

  _gtk_widget_update_parent_muxer (GTK_WIDGET (popover));
  g_object_unref (popover);
}

static void
gtk_popover_update_pointing_to (GtkPopover         *popover,
                                const GdkRectangle *pointing_to)
{
  GtkPopoverPrivate *priv = popover->priv;

  if (pointing_to)
    {
      priv->pointing_to = *pointing_to;
      priv->has_pointing_to = TRUE;
    }
  else
    priv->has_pointing_to = FALSE;

  g_object_notify_by_pspec (G_OBJECT (popover), properties[PROP_POINTING_TO]);
}

static void
gtk_popover_update_preferred_position (GtkPopover      *popover,
                                       GtkPositionType  position)
{
  if (popover->priv->preferred_position == position)
    return;

  popover->priv->preferred_position = position;
  g_object_notify_by_pspec (G_OBJECT (popover), properties[PROP_POSITION]);
}

/**
 * gtk_popover_new:
 * @relative_to: (allow-none): #GtkWidget the popover is related to
 *
 * Creates a new popover to point to @relative_to
 *
 * Returns: a new #GtkPopover
 **/
GtkWidget *
gtk_popover_new (GtkWidget *relative_to)
{
  g_return_val_if_fail (relative_to == NULL || GTK_IS_WIDGET (relative_to), NULL);

  return g_object_new (GTK_TYPE_POPOVER,
                       "relative-to", relative_to,
                       NULL);
}

/**
 * gtk_popover_set_relative_to:
 * @popover: a #GtkPopover
 * @relative_to: (allow-none): a #GtkWidget
 *
 * Sets a new widget to be attached to @popover. If @popover is
 * visible, the position will be updated.
 *
 * Note: the ownership of popovers is always given to their @relative_to
 * widget, so if @relative_to is set to %NULL on an attached @popover, it
 * will be detached from its previous widget, and consequently destroyed
 * unless extra references are kept.
 **/
void
gtk_popover_set_relative_to (GtkPopover *popover,
                             GtkWidget  *relative_to)
{
  g_return_if_fail (GTK_IS_POPOVER (popover));
  g_return_if_fail (relative_to == NULL || GTK_IS_WIDGET (relative_to));

  gtk_popover_update_relative_to (popover, relative_to);

  if (relative_to)
    gtk_popover_update_position (popover);
}

/**
 * gtk_popover_get_relative_to:
 * @popover: a #GtkPopover
 *
 * Returns the widget @popover is currently attached to
 *
 * Returns: (transfer none): a #GtkWidget
 **/
GtkWidget *
gtk_popover_get_relative_to (GtkPopover *popover)
{
  g_return_val_if_fail (GTK_IS_POPOVER (popover), NULL);

  return popover->priv->widget;
}

/**
 * gtk_popover_set_pointing_to:
 * @popover: a #GtkPopover
 * @rect: rectangle to point to
 *
 * Sets the rectangle that @popover will point to, in the
 * coordinate space of the widget @popover is attached to,
 * see gtk_popover_set_relative_to().
 **/
void
gtk_popover_set_pointing_to (GtkPopover         *popover,
                             const GdkRectangle *rect)
{
  g_return_if_fail (GTK_IS_POPOVER (popover));
  g_return_if_fail (rect != NULL);

  gtk_popover_update_pointing_to (popover, rect);
  gtk_popover_update_position (popover);
}

/**
 * gtk_popover_get_pointing_to:
 * @popover: a #GtkPopover
 * @rect: (out): location to store the rectangle
 *
 * If a rectangle to point to has been set, this function will
 * return %TRUE and fill in @rect with such rectangle, otherwise
 * it will return %FALSE and fill in @rect with the attached
 * widget coordinates.
 *
 * Returns: %TRUE if a rectangle to point to was set.
 **/
gboolean
gtk_popover_get_pointing_to (GtkPopover   *popover,
                             GdkRectangle *rect)
{
  GtkPopoverPrivate *priv = gtk_popover_get_instance_private (popover);

  g_return_val_if_fail (GTK_IS_POPOVER (popover), FALSE);
  g_return_val_if_fail (rect != NULL, FALSE);

  if (priv->has_pointing_to)
    *rect = priv->pointing_to;
   else if (priv->widget)
    gtk_widget_get_own_allocation (priv->widget, rect);

  return priv->has_pointing_to;
}

/**
 * gtk_popover_set_position:
 * @popover: a #GtkPopover
 * @position: preferred popover position
 *
 * Sets the preferred position for @popover to appear. If the @popover
 * is currently visible, it will be immediately updated.
 *
 * This preference will be respected where possible, although
 * on lack of space (eg. if close to the window edges), the
 * #GtkPopover may choose to appear on the opposite side
 **/
void
gtk_popover_set_position (GtkPopover      *popover,
                          GtkPositionType  position)
{
  g_return_if_fail (GTK_IS_POPOVER (popover));
  g_return_if_fail (position >= GTK_POS_LEFT && position <= GTK_POS_BOTTOM);

  gtk_popover_update_preferred_position (popover, position);
  gtk_popover_update_position (popover);
}

/**
 * gtk_popover_get_position:
 * @popover: a #GtkPopover
 *
 * Returns the preferred position of @popover.
 *
 * Returns: The preferred position.
 **/
GtkPositionType
gtk_popover_get_position (GtkPopover *popover)
{
  g_return_val_if_fail (GTK_IS_POPOVER (popover), GTK_POS_TOP);

  return popover->priv->preferred_position;
}

/**
 * gtk_popover_set_modal:
 * @popover: a #GtkPopover
 * @modal: #TRUE to make popover claim all input within the toplevel
 *
 * Sets whether @popover is modal, a modal popover will grab all input
 * within the toplevel and grab the keyboard focus on it when being
 * displayed. Clicking outside the popover area or pressing Esc will
 * dismiss the popover and ungrab input.
 **/
void
gtk_popover_set_modal (GtkPopover *popover,
                       gboolean    modal)
{
  GtkPopoverPrivate *priv = gtk_popover_get_instance_private (popover);

  g_return_if_fail (GTK_IS_POPOVER (popover));

  modal = modal != FALSE;

  if (priv->modal == modal)
    return;

  priv->modal = modal;

  if (gtk_widget_is_visible (GTK_WIDGET (popover)))
    gtk_popover_apply_modality (popover, priv->modal);

  g_object_notify_by_pspec (G_OBJECT (popover), properties[PROP_MODAL]);
}

/**
 * gtk_popover_get_modal:
 * @popover: a #GtkPopover
 *
 * Returns whether the popover is modal, see gtk_popover_set_modal to
 * see the implications of this.
 *
 * Returns: #TRUE if @popover is modal
 **/
gboolean
gtk_popover_get_modal (GtkPopover *popover)
{
  g_return_val_if_fail (GTK_IS_POPOVER (popover), FALSE);

  return popover->priv->modal;
}

static void
back_to_main (GtkWidget *popover)
{
  GtkWidget *stack;

  stack = gtk_bin_get_child (GTK_BIN (popover));
  gtk_stack_set_visible_child_name (GTK_STACK (stack), "main");
}

/**
 * gtk_popover_bind_model:
 * @popover: a #GtkPopover
 * @model: (allow-none): the #GMenuModel to bind to or %NULL to remove
 *   binding
 * @action_namespace: (allow-none): the namespace for actions in @model
 *
 * Establishes a binding between a #GtkPopover and a #GMenuModel.
 *
 * The contents of @popover are removed and then refilled with menu items
 * according to @model.  When @model changes, @popover is updated.
 * Calling this function twice on @popover with different @model will
 * cause the first binding to be replaced with a binding to the new
 * model. If @model is %NULL then any previous binding is undone and
 * all children are removed.
 *
 * If @action_namespace is non-%NULL then the effect is as if all
 * actions mentioned in the @model have their names prefixed with the
 * namespace, plus a dot.  For example, if the action “quit” is
 * mentioned and @action_namespace is “app” then the effective action
 * name is “app.quit”.
 *
 * This function uses #GtkActionable to define the action name and
 * target values on the created menu items.  If you want to use an
 * action group other than “app” and “win”, or if you want to use a
 * #GtkMenuShell outside of a #GtkApplicationWindow, then you will need
 * to attach your own action group to the widget hierarchy using
 * gtk_widget_insert_action_group().  As an example, if you created a
 * group with a “quit” action and inserted it with the name “mygroup”
 * then you would use the action name “mygroup.quit” in your
 * #GMenuModel.
 */
void
gtk_popover_bind_model (GtkPopover  *popover,
                        GMenuModel  *model,
                        const gchar *action_namespace)
{
  GtkWidget *child;
  GtkWidget *stack;
  GtkStyleContext *style_context;

  g_return_if_fail (GTK_IS_POPOVER (popover));
  g_return_if_fail (model == NULL || G_IS_MENU_MODEL (model));

  child = gtk_bin_get_child (GTK_BIN (popover));
  if (child)
    gtk_widget_destroy (child);

  style_context = gtk_widget_get_style_context (popover->priv->contents_widget);

  if (model)
    {
      stack = gtk_stack_new ();
      gtk_stack_set_vhomogeneous (GTK_STACK (stack), FALSE);
      gtk_stack_set_transition_type (GTK_STACK (stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
      gtk_stack_set_interpolate_size (GTK_STACK (stack), TRUE);
      gtk_container_add (GTK_CONTAINER (popover), stack);

      gtk_menu_section_box_new_toplevel (GTK_STACK (stack),
                                         model,
                                         action_namespace,
                                         popover);
      gtk_stack_set_visible_child_name (GTK_STACK (stack), "main");

      g_signal_connect (popover, "unmap", G_CALLBACK (back_to_main), NULL);
      g_signal_connect (popover, "map", G_CALLBACK (back_to_main), NULL);

      gtk_style_context_add_class (style_context, GTK_STYLE_CLASS_MENU);
    }
  else
    {
      gtk_style_context_remove_class (style_context, GTK_STYLE_CLASS_MENU);
    }
}

/**
 * gtk_popover_new_from_model:
 * @relative_to: (allow-none): #GtkWidget the popover is related to
 * @model: a #GMenuModel
 *
 * Creates a #GtkPopover and populates it according to
 * @model. The popover is pointed to the @relative_to widget.
 *
 * The created buttons are connected to actions found in the
 * #GtkApplicationWindow to which the popover belongs - typically
 * by means of being attached to a widget that is contained within
 * the #GtkApplicationWindows widget hierarchy.
 *
 * Actions can also be added using gtk_widget_insert_action_group()
 * on the menus attach widget or on any of its parent widgets.
 *
 * Returns: the new #GtkPopover
 */
GtkWidget *
gtk_popover_new_from_model (GtkWidget  *relative_to,
                            GMenuModel *model)
{
  GtkWidget *popover;

  g_return_val_if_fail (relative_to == NULL || GTK_IS_WIDGET (relative_to), NULL);
  g_return_val_if_fail (G_IS_MENU_MODEL (model), NULL);

  popover = gtk_popover_new (relative_to);
  gtk_popover_bind_model (GTK_POPOVER (popover), model, NULL);

  return popover;
}

/**
 * gtk_popover_set_default_widget:
 * @popover: a #GtkPopover
 * @widget: (allow-none): the new default widget, or %NULL
 *
 * Sets the widget that should be set as default widget while
 * the popover is shown (see gtk_window_set_default()). #GtkPopover
 * remembers the previous default widget and reestablishes it
 * when the popover is dismissed.
 */
void
gtk_popover_set_default_widget (GtkPopover *popover,
                                GtkWidget  *widget)
{
  GtkPopoverPrivate *priv = gtk_popover_get_instance_private (popover);

  g_return_if_fail (GTK_IS_POPOVER (popover));
  g_return_if_fail (widget == NULL || gtk_widget_get_can_default (widget));

  if (priv->default_widget == widget)
    return;

  if (priv->default_widget)
    g_object_unref (priv->default_widget);

  priv->default_widget = widget;

  if (priv->default_widget)
    g_object_ref (priv->default_widget);

  if (gtk_widget_get_mapped (GTK_WIDGET (popover)))
    gtk_window_set_default (priv->window, priv->default_widget);
}

/**
 * gtk_popover_get_default_widget:
 * @popover: a #GtkPopover
 *
 * Gets the widget that should be set as the default while
 * the popover is shown.
 *
 * Returns: (nullable) (transfer none): the default widget,
 * or %NULL if there is none
 */
GtkWidget *
gtk_popover_get_default_widget (GtkPopover *popover)
{
  GtkPopoverPrivate *priv = gtk_popover_get_instance_private (popover);

  g_return_val_if_fail (GTK_IS_POPOVER (popover), NULL);

  return priv->default_widget;
}

/**
 * gtk_popover_set_constrain_to:
 * @popover: a #GtkPopover
 * @constraint: the new constraint
 *
 * Sets a constraint for positioning this popover.
 *
 * Note that not all platforms support placing popovers freely,
 * and may already impose constraints.
 */
void
gtk_popover_set_constrain_to (GtkPopover           *popover,
                              GtkPopoverConstraint  constraint)
{
  GtkPopoverPrivate *priv = gtk_popover_get_instance_private (popover);

  g_return_if_fail (GTK_IS_POPOVER (popover));

  if (priv->constraint == constraint)
    return;

  priv->constraint = constraint;
  gtk_popover_update_position (popover);

  g_object_notify_by_pspec (G_OBJECT (popover), properties[PROP_CONSTRAIN_TO]);
}

/**
 * gtk_popover_get_constrain_to:
 * @popover: a #GtkPopover
 *
 * Returns the constraint for placing this popover.
 * See gtk_popover_set_constrain_to().
 *
 * Returns: the constraint for placing this popover.
 */
GtkPopoverConstraint
gtk_popover_get_constrain_to (GtkPopover *popover)
{
  GtkPopoverPrivate *priv = gtk_popover_get_instance_private (popover);

  g_return_val_if_fail (GTK_IS_POPOVER (popover), GTK_POPOVER_CONSTRAINT_WINDOW);

  return priv->constraint;
}

/**
 * gtk_popover_popup:
 * @popover: a #GtkPopover
 *
 * Pops @popover up. This is different than a gtk_widget_show() call
 * in that it shows the popover with a transition. If you want to show
 * the popover without a transition, use gtk_widget_show().
 */
void
gtk_popover_popup (GtkPopover *popover)
{
  GtkPopoverPrivate *priv = gtk_popover_get_instance_private (popover);

  g_return_if_fail (GTK_IS_POPOVER (popover));

  if (priv->state == STATE_SHOWING ||
      priv->state == STATE_SHOWN)
    return;

  gtk_widget_show (GTK_WIDGET (popover));

  if (transitions_enabled (popover))
    gtk_popover_set_state (popover, STATE_SHOWING);
}

/**
 * gtk_popover_popdown:
 * @popover: a #GtkPopover
 *
 * Pops @popover down.This is different than a gtk_widget_hide() call
 * in that it shows the popover with a transition. If you want to hide
 * the popover without a transition, use gtk_widget_hide().
 */
void
gtk_popover_popdown (GtkPopover *popover)
{
  GtkPopoverPrivate *priv = gtk_popover_get_instance_private (popover);

  g_return_if_fail (GTK_IS_POPOVER (popover));

  if (priv->state == STATE_HIDING ||
      priv->state == STATE_HIDDEN)
    return;


  if (!transitions_enabled (popover))
    gtk_widget_hide (GTK_WIDGET (popover));
  else
    gtk_popover_set_state (popover, STATE_HIDING);

  gtk_popover_hide_internal (popover);
}
