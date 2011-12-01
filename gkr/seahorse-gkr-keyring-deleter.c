/* 
 * Seahorse
 * 
 * Copyright (C) 2011 Collabora Ltd.
 *
 * This program is free software; you can redistribute it and/or modify 
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *  
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *  
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Author: Stef Walter <stefw@collabora.co.uk>
 */

#include "config.h"

#include "seahorse-gkr-backend.h"
#include "seahorse-gkr-keyring-deleter.h"
#include "seahorse-gkr-operation.h"

#include "seahorse-delete-dialog.h"
#include "seahorse-object.h"

#include <gnome-keyring.h>

#include <glib/gi18n.h>

#define SEAHORSE_GKR_KEYRING_DELETER_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), SEAHORSE_TYPE_GKR_KEYRING_DELETER, SeahorseGkrKeyringDeleterClass))
#define SEAHORSE_IS_GKR_KEYRING_DELETER_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), SEAHORSE_TYPE_GKR_KEYRING_DELETER))
#define SEAHORSE_GKR_KEYRING_DELETER_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj), SEAHORSE_TYPE_GKR_KEYRING_DELETER, SeahorseGkrKeyringDeleterClass))

typedef struct _SeahorseGkrKeyringDeleterClass SeahorseGkrKeyringDeleterClass;

struct _SeahorseGkrKeyringDeleter {
	SeahorseDeleter parent;
	SeahorseGkrKeyring *keyring;
	GList *objects;
};

struct _SeahorseGkrKeyringDeleterClass {
	SeahorseDeleterClass parent_class;
};

G_DEFINE_TYPE (SeahorseGkrKeyringDeleter, seahorse_gkr_keyring_deleter, SEAHORSE_TYPE_DELETER);

static void
seahorse_gkr_keyring_deleter_init (SeahorseGkrKeyringDeleter *self)
{

}

static void
seahorse_gkr_keyring_deleter_finalize (GObject *obj)
{
	SeahorseGkrKeyringDeleter *self = SEAHORSE_GKR_KEYRING_DELETER (obj);

	g_object_unref (self->keyring);
	g_list_free (self->objects);

	G_OBJECT_CLASS (seahorse_gkr_keyring_deleter_parent_class)->finalize (obj);
}

static GtkDialog *
seahorse_gkr_keyring_deleter_create_confirm (SeahorseDeleter *deleter,
                                             GtkWindow *parent)
{
	SeahorseGkrKeyringDeleter *self = SEAHORSE_GKR_KEYRING_DELETER (deleter);
	GtkDialog *dialog;

	dialog = seahorse_delete_dialog_new (parent,
	                                     _("Are you sure you want to delete the password keyring '%s'?"),
	                                     seahorse_object_get_label (SEAHORSE_OBJECT (self->keyring)));

	seahorse_delete_dialog_set_check_label (SEAHORSE_DELETE_DIALOG (dialog), _("I understand that all items will be permanently deleted."));
	seahorse_delete_dialog_set_check_require (SEAHORSE_DELETE_DIALOG (dialog), TRUE);

	return dialog;
}

static GList *
seahorse_gkr_keyring_deleter_get_objects (SeahorseDeleter *deleter)
{
	SeahorseGkrKeyringDeleter *self = SEAHORSE_GKR_KEYRING_DELETER (deleter);
	return self->objects;
}

static gboolean
seahorse_gkr_keyring_deleter_add_object (SeahorseDeleter *deleter,
                                         GObject *object)
{
	SeahorseGkrKeyringDeleter *self = SEAHORSE_GKR_KEYRING_DELETER (deleter);
	if (self->keyring)
		return FALSE;
	if (!SEAHORSE_IS_GKR_KEYRING (object))
		return FALSE;
	self->keyring = g_object_ref (object);
	self->objects = g_list_append (self->objects, self->keyring);
	return TRUE;
}

typedef struct {
	GCancellable *cancellable;
	gpointer request;
	gulong cancelled_sig;
} DeleteClosure;

static void
delete_closure_free (gpointer data)
{
	DeleteClosure *closure = data;
	if (closure->cancellable && closure->cancelled_sig)
		g_signal_handler_disconnect (closure->cancellable,
		                             closure->cancelled_sig);
	g_clear_object (&closure->cancellable);
	g_assert (!closure->request);
	g_free (closure);
}

static void
on_delete_gkr_complete (GnomeKeyringResult result,
                        gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	DeleteClosure *closure = g_simple_async_result_get_op_res_gpointer (res);
	SeahorseGkrKeyringDeleter *self = SEAHORSE_GKR_KEYRING_DELETER (g_async_result_get_source_object (user_data));
	GError *error = NULL;

	closure->request = NULL;

	if (seahorse_gkr_propagate_error (result, &error))
		g_simple_async_result_take_error (res, error);
	else
		seahorse_gkr_backend_remove_keyring (NULL, self->keyring);

	g_simple_async_result_complete_in_idle (res);
	g_object_unref (self);
}

static void
on_delete_gkr_cancelled (GCancellable *cancellable,
                         gpointer user_data)
{
	DeleteClosure *closure = user_data;

	if (closure->request)
		gnome_keyring_cancel_request (closure->request);
}

static void
seahorse_gkr_keyring_deleter_delete_async (SeahorseDeleter *deleter,
                                           GCancellable *cancellable,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data)
{
	SeahorseGkrKeyringDeleter *self = SEAHORSE_GKR_KEYRING_DELETER (deleter);
	GSimpleAsyncResult *res;
	DeleteClosure *closure;
	const gchar *name;

	res = g_simple_async_result_new (G_OBJECT (self), callback, user_data,
	                                 seahorse_gkr_keyring_deleter_delete_async);
	closure = g_new0 (DeleteClosure, 1);
	closure->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
	g_simple_async_result_set_op_res_gpointer (res, closure, delete_closure_free);

	name = seahorse_gkr_keyring_get_name (self->keyring);
	closure->request = gnome_keyring_delete (name, on_delete_gkr_complete,
	                                         g_object_ref (res), g_object_unref);

	if (cancellable)
		closure->cancelled_sig = g_cancellable_connect (cancellable,
		                                                G_CALLBACK (on_delete_gkr_cancelled),
		                                                closure, NULL);

	g_object_unref (res);
}

static gboolean
seahorse_gkr_keyring_deleter_delete_finish (SeahorseDeleter *deleter,
                                            GAsyncResult *result,
                                            GError **error)
{
	SeahorseGkrKeyringDeleter *self = SEAHORSE_GKR_KEYRING_DELETER (deleter);

	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (self),
	                      seahorse_gkr_keyring_deleter_delete_async), FALSE);

	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	return TRUE;
}

static void
seahorse_gkr_keyring_deleter_class_init (SeahorseGkrKeyringDeleterClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	SeahorseDeleterClass *deleter_class = SEAHORSE_DELETER_CLASS (klass);

	gobject_class->finalize = seahorse_gkr_keyring_deleter_finalize;

	deleter_class->add_object = seahorse_gkr_keyring_deleter_add_object;
	deleter_class->create_confirm = seahorse_gkr_keyring_deleter_create_confirm;
	deleter_class->delete_async = seahorse_gkr_keyring_deleter_delete_async;
	deleter_class->delete_finish = seahorse_gkr_keyring_deleter_delete_finish;
	deleter_class->get_objects = seahorse_gkr_keyring_deleter_get_objects;
}

SeahorseDeleter *
seahorse_gkr_keyring_deleter_new (SeahorseGkrKeyring *keyring)
{
	SeahorseDeleter *deleter;

	deleter = g_object_new (SEAHORSE_TYPE_GKR_KEYRING_DELETER, NULL);
	if (!seahorse_deleter_add_object (deleter, G_OBJECT (keyring)))
		g_assert_not_reached ();

	return deleter;
}
