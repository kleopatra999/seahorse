/*
 * Seahorse
 *
 * Copyright (C) 2003 Jacob Perkins
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <gnome.h>
#include <eel/eel.h>

#include "seahorse-key-store.h"

enum {
	PROP_0,
	PROP_KEY_SOURCE,
    PROP_MODE,
    PROP_FILTER
};

static const gchar* col_ids[] = {
    KEY_STORE_BASE_IDS
};

static const GType col_types[] = {
    KEY_STORE_BASE_TYPES
};

struct _SeahorseKeyStorePriv {    
    GHashTable              *rows;
    
    GtkTreeModelFilter      *filter;
    GtkTreeModelSort        *sort;
    
    SeahorseKeyStoreMode    filter_mode;
    gchar*                  filter_text;
    guint                   filter_stag;
};

/* Internal data stored at 0 in the tree store in order to keep track
 * of the location, key-store and key.
 */
typedef struct
{
	SeahorseKeyStore	*skstore;
	GtkTreeRowReference	*ref;
	SeahorseKey		*skey;
} SeahorseKeyRow;

static void	seahorse_key_store_class_init		(SeahorseKeyStoreClass	*klass);
static GObject* seahorse_key_store_constructor  (GType type, guint n_props, 
                                             GObjectConstructParam* props);
static void seahorse_key_store_dispose          (GObject       *gobject);
static void	seahorse_key_store_finalize		    (GObject       *gobject);

static void	seahorse_key_store_set_property		(GObject		*gobject,
							 guint			prop_id,
							 const GValue		*value,
							 GParamSpec		*pspec);
static void	seahorse_key_store_get_property		(GObject		*gobject,
							 guint			prop_id,
							 GValue			*value,
							 GParamSpec		*pspec);
/* Virtual methods */
static void	seahorse_key_store_append_key   (SeahorseKeyStore   *skstore,
                                             SeahorseKey        *skey,
	                                         GtkTreeIter        *iter);
static void	seahorse_key_store_set          (SeahorseKeyStore   *skstore,
                                             SeahorseKey        *skey,
                							 GtkTreeIter        *iter);
static void	seahorse_key_store_remove_iter  (SeahorseKeyStore   *skstore,
                                             GtkTreeIter        *iter);
static void	seahorse_key_store_changed      (SeahorseKeyStore   *skstore,
                                             SeahorseKey        *skey,
                                             GtkTreeIter        *iter,
                                             SeahorseKeyChange  change);

/* Key source signals */
static void seahorse_key_store_key_added    (SeahorseKeySource  *sksrc,
                                             SeahorseKey        *skey,
                                             SeahorseKeyStore   *skstore);
static void seahorse_key_store_key_removed  (SeahorseKeySource  *sksrc,
                                             SeahorseKey        *skey,
                                             SeahorseKeyStore   *skstore);
                             
/* Key signals */
static void	seahorse_key_store_key_changed		(SeahorseKey		*skey,
							 SeahorseKeyChange	change,
							 SeahorseKeyRow		*skrow);
/* Key Row methods */
static void	seahorse_key_row_new			(SeahorseKeyStore	*skstore,
							 GtkTreeIter		*iter,
							 SeahorseKey		*skey);
static void	seahorse_key_row_remove			(SeahorseKeyRow		*skrow);

static void seahorse_key_row_free           (SeahorseKeyRow     *skrow);

/* Filter row method */
static gboolean filter_callback              (GtkTreeModel *model,
                                             GtkTreeIter *iter,
                                             gpointer data);
                                             
static GtkTreeStoreClass	*parent_class	= NULL;

GType
seahorse_key_store_get_type (void)
{
	static GType key_store_type = 0;
	
	if (!key_store_type) {
		static const GTypeInfo key_store_info =
		{
			sizeof (SeahorseKeyStoreClass),
			NULL, NULL,
			(GClassInitFunc) seahorse_key_store_class_init,
			NULL, NULL,
			sizeof (SeahorseKeyStore),
			0, NULL
		};
		
		key_store_type = g_type_register_static (GTK_TYPE_TREE_STORE,
			"SeahorseKeyStore", &key_store_info, 0);
	}
	
	return key_store_type;
}

static void
seahorse_key_store_class_init (SeahorseKeyStoreClass *klass)
{
	GObjectClass *gobject_class;
	
	parent_class = g_type_class_peek_parent (klass);
	gobject_class = G_OBJECT_CLASS (klass);
	
    gobject_class->constructor = seahorse_key_store_constructor;
    gobject_class->finalize = seahorse_key_store_finalize;
    gobject_class->dispose = seahorse_key_store_dispose;
	gobject_class->set_property = seahorse_key_store_set_property;
	gobject_class->get_property = seahorse_key_store_get_property;
	
	klass->append = seahorse_key_store_append_key;
	klass->set = seahorse_key_store_set;
	klass->remove = seahorse_key_store_remove_iter;
	klass->changed = seahorse_key_store_changed;
  
  	/* Class defaults. Derived classes should override */
    klass->use_check = FALSE;
    klass->n_columns = KEY_STORE_NCOLS;
    klass->col_types = col_types;
    klass->col_ids = col_ids;
	
	g_object_class_install_property (gobject_class, PROP_KEY_SOURCE,
		g_param_spec_object ("key-source", "Seahorse Key Source",
				     "Current Seahorse Key Source to use",
				     SEAHORSE_TYPE_KEY_SOURCE, G_PARAM_READWRITE));
                    
    g_object_class_install_property (gobject_class, PROP_MODE,
        g_param_spec_uint ("mode", "Key Store Mode",
                     "Key store mode controls which keys to display",
                     0, KEY_STORE_MODE_FILTERED, KEY_STORE_MODE_ALL, 
                     G_PARAM_READWRITE));

    g_object_class_install_property (gobject_class, PROP_FILTER,
        g_param_spec_string ("filter", "Key Store Filter",
                     "Key store filter for when in filtered mode",
                     "", G_PARAM_READWRITE));

}

static GObject*  
seahorse_key_store_constructor (GType type, guint n_props, GObjectConstructParam* props)
{
    GObject* obj = G_OBJECT_CLASS (parent_class)->constructor (type, n_props, props);
    SeahorseKeyStore* skstore = SEAHORSE_KEY_STORE (obj);

    /* init private vars */
    skstore->priv = g_new0 (SeahorseKeyStorePriv, 1);
    skstore->priv->rows = g_hash_table_new_full (g_direct_hash, g_direct_equal, 
                                       NULL, (GDestroyNotify)seahorse_key_row_free);
 
    /* Setup the store */
    guint cols = SEAHORSE_KEY_STORE_GET_CLASS (skstore)->n_columns;
    GType* types = (GType*)SEAHORSE_KEY_STORE_GET_CLASS (skstore)->col_types;
    gtk_tree_store_set_column_types (GTK_TREE_STORE (obj), cols, types);
    
    /* Setup the sort and filter */
    skstore->priv->filter = GTK_TREE_MODEL_FILTER (gtk_tree_model_filter_new (GTK_TREE_MODEL (obj), NULL));
    gtk_tree_model_filter_set_visible_func (skstore->priv->filter, filter_callback, skstore, NULL);
    skstore->priv->sort = GTK_TREE_MODEL_SORT (gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (skstore->priv->filter)));
    
    return obj;
}

/* dispose of all our internal references */
static void
seahorse_key_store_dispose (GObject *gobject)
{
    SeahorseKeyStore *skstore;
    
    /*
     * Note that after this executes the rest of the object should
     * still work without a segfault. This basically nullifies the 
     * object, but doesn't free it.
     * 
     * This function should also be able to run multiple times.
     */

    skstore = SEAHORSE_KEY_STORE (gobject);  

    if (skstore->sksrc) {
        g_signal_handlers_disconnect_by_func (skstore->sksrc, 
                                seahorse_key_store_key_added, skstore);
        g_signal_handlers_disconnect_by_func (skstore->sksrc, 
                                seahorse_key_store_key_removed, skstore);
        g_object_unref (skstore->sksrc);        
        skstore->sksrc = NULL;
    }
    
    G_OBJECT_CLASS (parent_class)->dispose (gobject);
}
    
static void
seahorse_key_store_finalize (GObject *gobject)
{
	SeahorseKeyStore *skstore;
	
	skstore = SEAHORSE_KEY_STORE (gobject);
    g_assert (skstore->sksrc == NULL);
    	
    /* These were allocated in the constructor */
    g_object_unref (skstore->priv->sort);
    g_object_unref (skstore->priv->filter);
     
    /* Allocated in property setter */
    g_free (skstore->priv->filter_text); 
	
    /* The row cache */
    g_hash_table_destroy (skstore->priv->rows);
    
	G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

/* Refilter the tree */
static gboolean
refilter_now(SeahorseKeyStore* skstore)
{
    gtk_tree_model_filter_refilter (skstore->priv->filter);
    skstore->priv->filter_stag = 0;
    return FALSE;
}

/* Refilter the tree after a timeout has passed */
static void
refilter_later(SeahorseKeyStore* skstore)
{
    if (skstore->priv->filter_stag != 0)
        g_source_remove (skstore->priv->filter_stag);
    skstore->priv->filter_stag = g_timeout_add (200, (GSourceFunc)refilter_now, skstore);
}

static void
seahorse_key_store_set_property (GObject *gobject, guint prop_id,
				 const GValue *value, GParamSpec *pspec)
{
	SeahorseKeyStore *skstore;
    const gchar* t;
	
	skstore = SEAHORSE_KEY_STORE (gobject);
	
	switch (prop_id) {
		/* Connects to key source signals */
		case PROP_KEY_SOURCE:
            g_return_if_fail (skstore->sksrc == NULL);
			skstore->sksrc = g_value_get_object (value);
			g_object_ref (skstore->sksrc);
            g_signal_connect_after (skstore->sksrc, "added",
                G_CALLBACK (seahorse_key_store_key_added), skstore);
            g_signal_connect_after (skstore->sksrc, "removed",
                G_CALLBACK (seahorse_key_store_key_removed), skstore);
			break;
			
		/* The filtering mode */
        case PROP_MODE:
            if (skstore->priv->filter_mode != g_value_get_uint (value)) {
                skstore->priv->filter_mode = g_value_get_uint (value);
                refilter_later (skstore);
            }
            break;
            
         /* The filter text */
        case PROP_FILTER:
            t = g_value_get_string (value);
            
            /* 
             * If we're not in filtered mode and there is text OR
             * we're in filtered mode (regardless of text or not)
             * then update the filter
             */
            if ((skstore->priv->filter_mode != KEY_STORE_MODE_FILTERED && t && t[0]) ||
                (skstore->priv->filter_mode == KEY_STORE_MODE_FILTERED)) {
                skstore->priv->filter_mode = KEY_STORE_MODE_FILTERED;
                g_free (skstore->priv->filter_text);
				/* We always use lower case text (see filter_callback) */
                skstore->priv->filter_text = g_utf8_strdown (t, -1);
                refilter_later (skstore);
            }
            break;
		default:
			break;
	}
}

static void
seahorse_key_store_get_property (GObject *gobject, guint prop_id,
				 GValue *value, GParamSpec *pspec)
{
	SeahorseKeyStore *skstore;
	
	skstore = SEAHORSE_KEY_STORE (gobject);
	
	switch (prop_id) {
		case PROP_KEY_SOURCE:
			g_value_set_object (value, skstore->sksrc);
			break;
			
		/* The filtering mode */
        case PROP_MODE:
            g_value_set_uint (value, skstore->priv->filter_mode);
            break;
        
        /* The filter text. Note that we act as if we don't have any 
         * filter text when not in filtering mode */
        case PROP_FILTER:
            g_value_set_string (value, 
                skstore->priv->filter_mode == KEY_STORE_MODE_FILTERED ? skstore->priv->filter_text : "");
            break;
            
		default:
			break;
	}
}

/* Sets attributes then appends a new #SeahorseKeyRow */
static void
seahorse_key_store_append_key (SeahorseKeyStore *skstore, SeahorseKey *skey, 
                                    GtkTreeIter *iter)
{
	SEAHORSE_KEY_STORE_GET_CLASS (skstore)->set (skstore, skey, iter);
	seahorse_key_row_new (skstore, iter, skey);
}

/* Sets Name and KeyID */
static void
seahorse_key_store_set (SeahorseKeyStore *skstore, SeahorseKey *skey, 
                                    GtkTreeIter *iter)
{
    gchar *userid = seahorse_key_get_userid (skey, 0);
	gtk_tree_store_set (GTK_TREE_STORE (skstore), iter,
        KEY_STORE_CHECK, FALSE,
		KEY_STORE_NAME, userid,
		KEY_STORE_KEYID, seahorse_key_get_keyid (skey, 0), -1);
    g_free (userid);
}

/* Removes row at @iter from store */
static void
seahorse_key_store_remove_iter (SeahorseKeyStore *skstore, GtkTreeIter *iter)
{
	gtk_tree_store_remove (GTK_TREE_STORE(skstore), iter);
}

/* Refreshes key if uids have changed */
static void
seahorse_key_store_changed (SeahorseKeyStore *skstore, SeahorseKey *skey, 
                                GtkTreeIter *iter, SeahorseKeyChange change)
{
	switch (change) {
        case SKEY_CHANGE_ALL:
		case SKEY_CHANGE_UIDS:
			SEAHORSE_KEY_STORE_GET_CLASS (skstore)->set (skstore, skey, iter);
			break;
		default:
			break;
	}
}

/* Appends @skey */
static void
seahorse_key_store_key_added (SeahorseKeySource *sksrc, SeahorseKey *skey, SeahorseKeyStore *skstore)
{
	GtkTreeIter iter;
	
	SEAHORSE_KEY_STORE_GET_CLASS (skstore)->append (skstore, skey, &iter);
}

/* Removes @skrow */
static void
seahorse_key_store_key_removed (SeahorseKeySource *sksrc, SeahorseKey *skey, SeahorseKeyStore *skstore)
{
    SeahorseKeyRow *skrow;
    
    skrow = (SeahorseKeyRow*)g_hash_table_lookup (skstore->priv->rows, skey);
	seahorse_key_row_remove (skrow);
}

/* Gets location of @skey, then calls virtual changed() */
static void
seahorse_key_store_key_changed (SeahorseKey *skey, SeahorseKeyChange change, SeahorseKeyRow *skrow)
{
	GtkTreePath *path;
	GtkTreeIter iter;

	path = gtk_tree_row_reference_get_path (skrow->ref);
	g_return_if_fail (gtk_tree_model_get_iter (GTK_TREE_MODEL (skrow->skstore), &iter, path));
	SEAHORSE_KEY_STORE_GET_CLASS (skrow->skstore)->changed (skrow->skstore, skey, &iter, change);
    gtk_tree_path_free (path);
}

/* Update the sort order for a column */
static void
set_sort_to (SeahorseKeyStore *skstore, const gchar *name)
{
    GtkTreeSortable* sort;
    gint i, id = -1;
    GtkSortType ord = GTK_SORT_ASCENDING;
    const gchar* n;
    
    /* Prefix with a minus means descending */
    if (name[0] == '-') {
        ord = GTK_SORT_DESCENDING;
        name++;
    }
    
    /* Prefix with a plus means ascending */
    else if (name[0] == '+') {
        ord = GTK_SORT_ASCENDING;
        name++;
    }
    
    /* Find the column id */
    for (i = SEAHORSE_KEY_STORE_GET_CLASS (skstore)->n_columns - 1; i >= 0 ; i--) {
        n = SEAHORSE_KEY_STORE_GET_CLASS (skstore)->col_ids[i];
        if (n && g_ascii_strcasecmp (name, n) == 0) {
            id = i;
            break;
        }
    }
    
    if (id != -1) {
        sort = GTK_TREE_SORTABLE (skstore->priv->sort);
        gtk_tree_sortable_set_sort_column_id (sort, id, ord);
    }
}

/* Called when the column sort is changed */
static void
sort_changed(GtkTreeSortable *sort, gpointer user_data)
{
    gint id;
    GtkSortType ord;
    SeahorseKeyStore *skstore;
    SeahorseKeyStoreClass *klass;
    const gchar* t;
    gchar* x;
    
    skstore = SEAHORSE_KEY_STORE (user_data);
    klass = SEAHORSE_KEY_STORE_GET_CLASS (skstore);
    
    if (!klass->gconf_sort_key)
        return;
        
    /* We have a sort so save it */
    if (gtk_tree_sortable_get_sort_column_id (sort, &id, &ord)) {
        if (id >= 0 && id < klass->n_columns) {
            t = klass->col_ids[id];
            if (t != NULL) {
                x = g_strconcat (ord == GTK_SORT_DESCENDING ? "-" : "", t, NULL);
                eel_gconf_set_string (klass->gconf_sort_key, x);
                g_free (x);
            }
        }
    }
    
    /* No sort so save blank */
    else {
        eel_gconf_set_string (klass->gconf_sort_key, "");
    }
}

/* Called when a checkbox is toggled, toggle the value */
static void
check_toggled(GtkCellRendererToggle *cellrenderertoggle, gchar *path, gpointer user_data)
{
    SeahorseKeyStore *skstore = SEAHORSE_KEY_STORE (user_data);
    GtkTreeModel* fmodel = GTK_TREE_MODEL (skstore->priv->sort);
    gboolean prev = FALSE;
    GtkTreeIter iter;
    GtkTreeIter child;
    GValue v;

    memset (&v, 0, sizeof(v));
    g_return_if_fail (path != NULL);
    g_return_if_fail (gtk_tree_model_get_iter_from_string (fmodel, &iter, path));
    
    /* We get notified in filtered coordinates, we have to convert those to base */
    seahorse_key_store_get_base_iter (skstore, &child, &iter);
 
    gtk_tree_model_get_value (GTK_TREE_MODEL (skstore), &child, KEY_STORE_CHECK, &v);
    if(G_VALUE_TYPE (&v) == G_TYPE_BOOLEAN)
        prev = g_value_get_boolean (&v);
    g_value_unset (&v);    
    
    gtk_tree_store_set (GTK_TREE_STORE (skstore), &child, KEY_STORE_CHECK, prev ? FALSE : TRUE, -1);
}

/* Creates a new #SeahorseKeyRow for listening to key signals */
static void
seahorse_key_row_new (SeahorseKeyStore *skstore, GtkTreeIter *iter, SeahorseKey *skey)
{
	SeahorseKeyRow *skrow;
	GtkTreePath *path;

	skrow = g_new0 (SeahorseKeyRow, 1);
	skrow->skstore = skstore;

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (skstore), iter);
	skrow->ref = gtk_tree_row_reference_new (GTK_TREE_MODEL (skstore), path);
    gtk_tree_path_free (path);
    
	skrow->skey = skey;
	g_object_ref (skey);
	g_signal_connect_after (skrow->skey, "changed",
		G_CALLBACK (seahorse_key_store_key_changed), skrow);

    /* Put it in our row cache */
    g_hash_table_replace (skstore->priv->rows, skey, skrow);

    gtk_tree_store_set (GTK_TREE_STORE (skstore), iter, KEY_STORE_DATA, skrow, -1);
}

static void
seahorse_key_row_free (SeahorseKeyRow *skrow)
{
    /* Unref key */
    g_signal_handlers_disconnect_by_func (skrow->skey,
             seahorse_key_store_key_changed, skrow);
    g_object_unref (skrow->skey);
  
    gtk_tree_row_reference_free (skrow->ref);
   
    g_free (skrow);
}    

/* Calls virtual remove() for @skrow's location, disconnects and unrefs
 * key, then frees itself.
 */
static void
seahorse_key_row_remove (SeahorseKeyRow *skrow)
{
	GtkTreeIter iter;
	GtkTreePath *path;

	path = gtk_tree_row_reference_get_path (skrow->ref);
	if (gtk_tree_model_get_iter (GTK_TREE_MODEL (skrow->skstore), &iter, path))
		SEAHORSE_KEY_STORE_GET_CLASS (skrow->skstore)->remove (skrow->skstore, &iter);
	gtk_tree_path_free (path);

    /* This also frees the skrow */
    g_return_if_fail(g_hash_table_remove (skrow->skstore->priv->rows, skrow->skey));
}

/**
 * seahorse_key_store_init:
 * @skstore: #SeahorseKeyStore to initialize
 * @view: #GtkTreeView that will show @skstore
 *
 * Initializes @skstore with default columns and embeds in @view.
 * This must be called after creating a new #SeahorseKeyStore.
 **/
void
seahorse_key_store_init (SeahorseKeyStore *skstore, GtkTreeView *view)
{
	GtkTreeViewColumn *col;

    seahorse_key_store_populate (skstore);

    /* The sorted model is the top level model */   
    g_assert (GTK_IS_TREE_MODEL (skstore->priv->sort));
    gtk_tree_view_set_model (view, GTK_TREE_MODEL (skstore->priv->sort));
 
 	/* When using checks we add a check column */
    if (SEAHORSE_KEY_STORE_GET_CLASS (skstore)->use_check) {
        GtkCellRenderer *renderer;
        renderer = gtk_cell_renderer_toggle_new ();
        g_signal_connect (renderer, "toggled", G_CALLBACK (check_toggled), skstore);
        col = gtk_tree_view_column_new_with_attributes ("", renderer, "active", KEY_STORE_CHECK, NULL);
        gtk_tree_view_column_set_resizable (col, FALSE);
        gtk_tree_view_append_column (view, col);
    }
      
	col = seahorse_key_store_append_column (view, _("Name"), KEY_STORE_NAME);
	gtk_tree_view_column_set_sort_column_id (col, KEY_STORE_NAME);
	
	seahorse_key_store_append_column (view, _("Key ID"), KEY_STORE_KEYID);

    if (SEAHORSE_KEY_STORE_GET_CLASS (skstore)->gconf_sort_key) {
        /* Also watch for sort-changed on the store */
        g_signal_connect (skstore->priv->sort, "sort-column-changed", G_CALLBACK (sort_changed), skstore);
    }
}

/**
 * seahorse_key_store_destroy:
 * @skstore: #SeahorseKeyStore to destroy
 *
 * Empties @skstore in order to unref any keys it contains.
 * Also unrefs @skstore.  Call this method if @skstore does not exist
 * for the life of the program.
 **/
void
seahorse_key_store_destroy (SeahorseKeyStore *skstore)
{
	SeahorseKeyRow *skrow;
	GtkTreeIter iter;
	
	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (skstore), &iter)) {
		do {
			gtk_tree_model_get (GTK_TREE_MODEL (skstore), &iter, KEY_STORE_DATA, &skrow, -1);
			seahorse_key_row_remove (skrow);
		} while (GTK_IS_TREE_MODEL (skstore) &&
			 gtk_tree_model_get_iter_first (GTK_TREE_MODEL (skstore), &iter));
	}
	g_object_unref (skstore);
}

/**
 * seahorse_key_store_populate:
 * @skstore: #SeahorseKeyStore to populate with #SeahorseKeys
 *
 * A new #SeahorseKeyStore is initially empty. Call this method to populate
 * the #SeahorseKeyStore with #SeahorseKeys.
 **/
void
seahorse_key_store_populate (SeahorseKeyStore *skstore)
{
	GList *keys, *list = NULL;
	SeahorseKey *skey;
	guint count = 1;
	gdouble length;
	GtkTreeIter iter;
	
    g_return_if_fail (SEAHORSE_IS_KEY_STORE (skstore));
    g_return_if_fail (SEAHORSE_IS_KEY_SOURCE (skstore->sksrc));
	
    /* Don't precipitate a load */
    if (seahorse_key_source_get_count (skstore->sksrc, FALSE) > 0) {

    	keys = list = seahorse_key_source_get_keys (skstore->sksrc, FALSE);
    	length = g_list_length (list);
	
    	while (list != NULL && (skey = list->data) != NULL) {
	       	SEAHORSE_KEY_STORE_GET_CLASS (skstore)->append (skstore, skey, &iter);
    		list = g_list_next (list);
    		count++;
    	}
     
        g_list_free (keys);
    }
}

/* Try to find our key store given a tree model */
static SeahorseKeyStore* 
key_store_from_model (GtkTreeModel *model)
{
	/* Sort models are what's set on the tree */
    if (GTK_IS_TREE_MODEL_SORT (model)) {
        model = gtk_tree_model_sort_get_model (GTK_TREE_MODEL_SORT (model));
        model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (model));
    }
    
    if (SEAHORSE_IS_KEY_STORE (model))
        return SEAHORSE_KEY_STORE (model);
    
    g_assert_not_reached ();
    return NULL;
}

/* Given an iterator find the associated key */
static SeahorseKey*
key_from_iterator (GtkTreeModel* model, GtkTreeIter* iter)
{
    GtkTreeIter parent, i;
    SeahorseKeyRow *skrow;
    
    /* Convert to base iter if necessary */
    if (!SEAHORSE_IS_KEY_STORE (model)) {
        SeahorseKeyStore* skstore = key_store_from_model(model);
        seahorse_key_store_get_base_iter (skstore, &i, iter);
        
        iter = &i;
        model = GTK_TREE_MODEL (skstore);
    }
    
    gtk_tree_model_get (model, 
        gtk_tree_model_iter_parent (model, &parent, iter) ? &parent : iter, 
        KEY_STORE_DATA, &skrow, -1);    
    
   return skrow->skey;
}


     
/**
 * seahorse_key_store_get_key_from_path:
 * @view: #GtkTreeView containing @path
 * @path: #GtkTreePath containing a #SeahorseKey
 *
 * Gets the #SeahorseKey at @path in @view.
 *
 * Returns: The #SeahorseKey at @path in @view
 **/
SeahorseKey*
seahorse_key_store_get_key_from_path (GtkTreeView *view, GtkTreePath *path)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	
	g_return_val_if_fail (GTK_IS_TREE_VIEW (view), NULL);
	g_return_val_if_fail (path != NULL, NULL);
	
	model = gtk_tree_view_get_model (view);
	g_return_val_if_fail (gtk_tree_model_get_iter (model, &iter, path), NULL);
    return key_from_iterator (model, &iter);
}

/**
 * seahorse_key_store_append_column:
 * @view: #GtkTreeView to append column to
 * @name: Title of new column
 * @index: Index of new column
 *
 * Creates a new #GtkTreeViewColumn with @name as the title,
 * and appends it to @view at @index.
 *
 * Returns: The created column
 **/
GtkTreeViewColumn*
seahorse_key_store_append_column (GtkTreeView *view, const gchar *label, const gint index)
{
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	SeahorseKeyStore *skstore;
    const gchar *key;
    gchar *sort;
    
	g_return_val_if_fail (GTK_IS_TREE_VIEW (view), NULL);

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (label, renderer, "text", index, NULL);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_append_column (view, column);
	
    skstore = key_store_from_model (gtk_tree_view_get_model(view));
    key = SEAHORSE_KEY_STORE_GET_CLASS (skstore)->gconf_sort_key;
    
    /* Update sort order in case the sorted column was added */
    if (key && (sort = eel_gconf_get_string (key)) != NULL) {
        set_sort_to (skstore, sort);
        g_free (sort);
    }  
    
	return column;
}

/**
 * seahorse_key_store_get_selected_keys:
 * @view: #GtkTreeView with selection
 *
 * Gets a list of the selected #SeahorseKeys.
 *
 * Returns: The selected #SeahorseKeys
 **/
GList*
seahorse_key_store_get_selected_keys (GtkTreeView *view)
{
	GList *keys = NULL;
    SeahorseKeyStore* skstore;
    
    g_return_val_if_fail (GTK_IS_TREE_VIEW (view), NULL);
    skstore = key_store_from_model (gtk_tree_view_get_model (view));
    
    if (SEAHORSE_KEY_STORE_GET_CLASS (skstore)->use_check) {
        GtkTreeModel* model = GTK_TREE_MODEL (skstore);
        GtkTreeIter iter;
        gboolean check;
            
        if (gtk_tree_model_get_iter_first (model, &iter)) {
            do {
                check = FALSE;
                gtk_tree_model_get (model, &iter, KEY_STORE_CHECK, &check, -1); 
                if (check)
                    keys = g_list_append (keys, key_from_iterator (model, &iter));
            } while (gtk_tree_model_iter_next (model, &iter));
        }
    }
    
    /* Fall back if none checked, or not using checks */
    if (keys == NULL) {
    	GList *list, *paths = NULL;
        GtkTreeSelection *selection;	
        
    	selection = gtk_tree_view_get_selection (view);
    	paths = gtk_tree_selection_get_selected_rows (selection, NULL);
	
    	/* make key list */
	    for (list = paths; list != NULL; list = g_list_next (list))
		    keys = g_list_append (keys, seahorse_key_store_get_key_from_path (view, list->data));
            
        /* free selected paths */
        g_list_foreach (paths, (GFunc)gtk_tree_path_free, NULL);
        g_list_free (paths);
    }
	 
    return keys;
}

/**
 * seahorse_key_store_get_selected_key:
 * @view: #GtkTreeView with selection
 *
 * Sugar method for getting the selected #SeahorseKey from @view.
 *
 * Returns: The selected #SeahorseKey, or NULL there are no selections or
 * more than one selection.
 **/
SeahorseKey*
seahorse_key_store_get_selected_key (GtkTreeView *view)
{
	GList *list = NULL;
	SeahorseKey *skey;
	
	g_return_val_if_fail (GTK_IS_TREE_VIEW (view), NULL);
	
	/* get selected keys */
	list = seahorse_key_store_get_selected_keys (view);
	g_return_val_if_fail (list != NULL, NULL);
	g_return_val_if_fail (g_list_length (list) == 1, NULL);
	
	/* get first key, free list */
	skey = list->data;
	g_list_free (list);
	
	return skey;
}

/* Search through row for text */
static gboolean
row_contains_filtered_text (GtkTreeModel* model, GtkTreeIter* iter, const gchar* text)
{
    gchar* name = NULL;
    gchar* id = NULL;
    gchar* t;
    gboolean ret = FALSE;
    
    /* Empty search text results in a match */
    if (!text || !text[0])
        return TRUE;
    
    /* Note that we only search the name and id */
    gtk_tree_model_get (model, iter, KEY_STORE_NAME, &name, KEY_STORE_KEYID, &id, -1);
    
    if(name) {
        t = g_utf8_strdown (name, -1);
        if (strstr (t, text))
            ret = TRUE;
        g_free (t);
    }
    
    if (!ret && id) {
        t = g_utf8_strdown (id, -1);
        if (strstr (t, text))
            ret = TRUE;
        g_free (t);
    }
    
    g_free (name);
    g_free (id);
    return ret;
}

/* Called to filter each row */
static gboolean 
filter_callback (GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    SeahorseKeyStore* skstore = SEAHORSE_KEY_STORE(data);
    GtkTreeIter child;
    gboolean ret = FALSE;
    
    /* Check the row requested */
    switch (skstore->priv->filter_mode)
    {
    case KEY_STORE_MODE_FILTERED:
        ret = row_contains_filtered_text (model, iter, skstore->priv->filter_text);
        break;
        
    case KEY_STORE_MODE_SELECTED:
        if (SEAHORSE_KEY_STORE_GET_CLASS (skstore)->use_check) {
            gboolean check = FALSE;
            gtk_tree_model_get (model, iter, KEY_STORE_CHECK, &check, -1); 
            ret = check;
        }
        break;
        
    case KEY_STORE_MODE_ALL:
        ret = TRUE;
        break;
        
    default:
        g_assert_not_reached ();
        break;
    };
    
    /* If current is not being shown, double check with children */
    if (!ret && gtk_tree_model_iter_children (model, &child, iter)) {
        do {
            ret = filter_callback (model, &child, data);
        } while (!ret && gtk_tree_model_iter_next (model, &child));
    }

    return ret;        
}

/* Given a treeview iter, get the base store iterator */
void 
seahorse_key_store_get_base_iter(SeahorseKeyStore* skstore, GtkTreeIter* base_iter, 
                                        const GtkTreeIter* iter)
{
    GtkTreeIter i;
    
    g_return_if_fail (SEAHORSE_IS_KEY_STORE (skstore));
    g_assert (skstore->priv->sort && skstore->priv->filter);
    
    gtk_tree_model_sort_convert_iter_to_child_iter (skstore->priv->sort, &i, (GtkTreeIter*)iter);
    gtk_tree_model_filter_convert_iter_to_child_iter (skstore->priv->filter, base_iter, &i);
}

