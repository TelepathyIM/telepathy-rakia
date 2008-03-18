/*
 * event-target.c - Implementation for TpsipEventTarget interface
 * Copyright (C) 2008 Nokia Corporation
 *   @author Mikhail Zabaluev <mikhail.zabaluev@nokia.com>
 *
 * This work is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This work is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this work; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "event-target.h"
#include "signals-marshal.h"

/* Define to the highest known nua_event_e enumeration member */
#define TPSIP_NUA_EVENT_LAST nua_i_register

/* Mapping of the event enumeration to signal detail quarks */
static GQuark event_quarks[TPSIP_NUA_EVENT_LAST + 1] = {0};

/* Signals */
enum {
  SIG_NUA_EVENT,
  NUM_SIGNALS
};
static guint signals[NUM_SIGNALS] = {0};

static TpsipEventTarget * tpsip_event_target_gone_instance ();

static void
tpsip_event_target_base_init (gpointer klass)
{
  static gboolean initialized = FALSE;
  gint i;

  if (!initialized)
    {
      initialized = TRUE;

      /**
       * TpsipEventTarget::nua-event:
       * @instance: an object implementing #TpsipEventTarget that emitted the signal
       * @event: Pointer to the event data structure
       * @tags: Tag list containing dynamically typed information about the event
       *
       * Emitted by the NUA event handler for an object bound
       * to a NUA operation handle.  
       * Returns: a handler returns TRUE to indicate that further handling
       * of the signal should cease.
       */
      signals[SIG_NUA_EVENT] =
        g_signal_new ("nua-event",
            G_TYPE_FROM_CLASS (klass),
            G_SIGNAL_RUN_LAST|G_SIGNAL_DETAILED,
            0 /* G_STRUCT_OFFSET (TpsipEventTargetInterface, nua_event) */,
            g_signal_accumulator_true_handled,
            NULL,
            _tpsip_marshal_BOOLEAN__POINTER_POINTER,
            G_TYPE_BOOLEAN,
            2,
            G_TYPE_POINTER,
            G_TYPE_POINTER);

      for (i = 0; i <= TPSIP_NUA_EVENT_LAST; i++)
        event_quarks[i] =
            g_quark_from_static_string (nua_event_name ((nua_event_t) i));
    }
}

GType
tpsip_event_target_get_type (void)
{
  static GType type = 0;

  if (G_UNLIKELY (type == 0))
    {
      static const GTypeInfo info = {
        sizeof (TpsipEventTargetInterface),
        tpsip_event_target_base_init, /* base_init */
        NULL, /* base_finalize */
        NULL, /* class_init */
        NULL, /* class_finalize */
        NULL, /* class_data */
        0,
        0, /* n_preallocs */
        NULL /* instance_init */
      };

      type = g_type_register_static (G_TYPE_INTERFACE,
          "TpsipEventTargetInterface", &info, 0);
    }

  return type;
}

static void
tpsip_event_target_retire_nua_handle (nua_handle_t *nh)
{
  static TpsipEventTarget *target_gone = NULL;

  if (G_UNLIKELY (target_gone == NULL))
    target_gone = tpsip_event_target_gone_instance ();

  nua_handle_bind (nh, target_gone);
  nua_handle_unref (nh);
}

static void
_tpsip_event_target_finalized (gpointer data, GObject *former_obj)
{
  tpsip_event_target_retire_nua_handle ((nua_handle_t *) data);
}

/**
 * tpsip_event_target_attach:
 * @nh: The NUA handle
 * @obj: an object implementing #TpsipEventTarget
 *
 * Attach an event target object to the NUA handle using nua_handle_bind().
 * The reference count of the NUA handle is incremented.
 * When the attached object is finalized, the reference count of the NUA handle
 * is decremented, and the handle is bound to a special end-of-life event
 * handler for debugging purposes.
 */
void
tpsip_event_target_attach (nua_handle_t *nh, GObject *obj)
{
  g_assert (nh != NULL);

  nua_handle_bind (nh, TPSIP_EVENT_TARGET (obj));
  nua_handle_ref (nh);

  g_object_weak_ref (obj, _tpsip_event_target_finalized, nh);
}

/**
 * tpsip_event_target_detach:
 * @nh: The NUA handle
 *
 * Detach the event target object previously attached to the NUA handle
 * using tpsip_event_target_attach().
 * The reference count of the NUA handle is decremented, and the handle
 * is bound to a special end-of-life event handler for debugging purposes.
 */
void
tpsip_event_target_detach (nua_handle_t *nh)
{
  GObject *obj;

  g_assert (nh != NULL);

  obj = G_OBJECT (nua_handle_magic (nh));
  g_object_weak_unref (obj, _tpsip_event_target_finalized, nh);

  tpsip_event_target_retire_nua_handle (nh);
}

/**
 * tpsip_event_target_emit_nua_event:
 * @instance: The object implementing this interface
 * @event: Pointer to the event data structure
 * @tags: Tag list containing dynamically typed information about the event 
 *
 * Emit the signal #TpsipEventTarget::nua-event, detailed with the event name,
 * on an instance of a class implementing this interface.
 * This function is normally called by the NUA callback.
 * Returns: TRUE if a signal handler handled the event and returned TRUE. 
 */
gboolean
tpsip_event_target_emit_nua_event (gpointer             instance,
                                   const TpsipNuaEvent *ev,
                                   tagi_t               tags[])
{
  gboolean retval = FALSE;
  gint nua_event;
  GQuark detail;

  g_assert (TPSIP_IS_EVENT_TARGET (instance));

  nua_event = ev->nua_event;

  detail = G_LIKELY (nua_event >= 0 && nua_event <= TPSIP_NUA_EVENT_LAST)
           ? event_quarks[nua_event]
           : g_quark_from_static_string (nua_event_name (nua_event));

  g_signal_emit (instance,
                 signals[SIG_NUA_EVENT],
                 detail,
                 ev,
                 tags,
                 &retval);

  return retval;
}

/* TpsipEventTargetGone:
 * a special private implementation of TpsipEventTarget for a singleton
 * catch-all object to associate with handles that have been detached from
 * other event targets.
 */
typedef struct _TpsipEventTargetGone TpsipEventTargetGone;
typedef struct _TpsipEventTargetGoneClass TpsipEventTargetGoneClass;

struct _TpsipEventTargetGone {
  GObject parent;
};

struct _TpsipEventTargetGoneClass {
  GObjectClass parent_class;
};

static gboolean
tpsip_late_nua_event_cb (TpsipEventTargetGone *self,
                         const TpsipNuaEvent  *event,
                         tagi_t                tags[],
                         gpointer              foo)
{
  g_message ("%s received for the retired handle %p: %03d %s",
             nua_event_name (event->nua_event),
             event->nua_handle,
             event->status,
             event->text);
  return TRUE;
}

static void
tpsip_event_target_gone_iface_init (gpointer g_iface, gpointer iface_data)
{
}

G_DEFINE_TYPE_WITH_CODE (TpsipEventTargetGone, tpsip_event_target_gone,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TPSIP_TYPE_EVENT_TARGET, tpsip_event_target_gone_iface_init))

static void
tpsip_event_target_gone_class_init (TpsipEventTargetGoneClass *klass)
{
}

static void
tpsip_event_target_gone_init (TpsipEventTargetGone *self)
{
  g_signal_connect (self, "nua-event",
                    G_CALLBACK (tpsip_late_nua_event_cb),
                    NULL);
}

static gpointer
_tpsip_event_target_gone_new_instance (gpointer foo)
{
  return g_object_new (tpsip_event_target_gone_get_type (), NULL);
}

static TpsipEventTarget *
tpsip_event_target_gone_instance ()
{
  static GOnce init_gone_once = G_ONCE_INIT;

  g_once (&init_gone_once, _tpsip_event_target_gone_new_instance, NULL);

  return TPSIP_EVENT_TARGET (init_gone_once.retval);
}
