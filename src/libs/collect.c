/*
    This file is part of darktable,
    copyright (c) 2009--2011 johannes hanika.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "common/darktable.h"
#include "common/film.h"
#include "common/collection.h"
#include "common/debug.h"
#include "control/conf.h"
#include "control/control.h"
#include "control/jobs.h"
#include "gui/gtk.h"
#include "dtgtk/button.h"
#include "libs/lib.h"
#include "common/metadata.h"
#include "common/utility.h"
#include "libs/collect.h"
#include "views/view.h"

DT_MODULE(1)

#define MAX_RULES 10

/* Folders code starts here
 TODO: Clean it */

static void _lib_folders_update_collection(const gchar *filmroll);


//DT_MODULE(1)

typedef struct dt_lib_collect_rule_t
{
  long int num;
  GtkWidget *hbox;
  GtkComboBox *combo;
  GtkWidget *text;
  GtkWidget *button;
}
dt_lib_collect_rule_t;

typedef struct dt_lib_collect_t
{
  dt_lib_collect_rule_t rule[MAX_RULES];
  int active_rule;
  GtkTreeView *view;
  GtkTreeModel *treemodel;
  GtkTreeModel *listmodel;
  GtkScrolledWindow *scrolledwindow;
  
  GPtrArray *buttons;
  GPtrArray *trees;

  GtkBox *box;

  struct dt_lib_collect_params_t *params;
}
dt_lib_collect_t;

#define PARAM_STRING_SIZE 256  // FIXME: is this enough !?
typedef struct dt_lib_collect_params_t
{
  uint32_t rules;
  struct {
    uint32_t item:16;
    uint32_t mode:16;
    char string[PARAM_STRING_SIZE];
  } rule[MAX_RULES];
} dt_lib_collect_params_t;

typedef enum dt_lib_collect_cols_t
{
  DT_LIB_COLLECT_COL_TEXT=0,
  DT_LIB_COLLECT_COL_ID,
  DT_LIB_COLLECT_COL_TOOLTIP,
  DT_LIB_COLLECT_COL_PATH,
  DT_LIB_COLLECT_COL_COUNT,
  DT_LIB_COLLECT_NUM_COLS
}
dt_lib_collect_cols_t;

typedef struct dt_lib_folders_t
{
  /* data */
  GtkTreeStore *store;
  GList *mounts;

  /* gui */
  GVolumeMonitor *gv_monitor;
  GtkBox *box_tree;

  GPtrArray *buttons;
  GPtrArray *trees;
}
dt_lib_folders_t;

typedef struct _image_t
{
  int id;
  int filmid;
  gchar *path;
  gchar *filename;
  int exists;
}
_image_t;

static void
row_activated (GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *col, dt_lib_collect_t *d);

/* callback for drag and drop */
/*static void _lib_keywords_drag_data_received_callback(GtkWidget *w,
					  GdkDragContext *dctx,
					  guint x,
					  guint y,
					  GtkSelectionData *data,
					  guint info,
					  guint time,
					  gpointer user_data);
*/
/* set the data for drag and drop, eg the treeview path of drag source */
/*static void _lib_keywords_drag_data_get_callback(GtkWidget *w,
						 GdkDragContext *dctx,
						 GtkSelectionData *data,
						 guint info,
						 guint time,
						 gpointer user_data);
*/
/* add keyword to collection rules */
/*static void _lib_keywords_add_collection_rule(GtkTreeView *view, GtkTreePath *tp,
					      GtkTreeViewColumn *tvc, gpointer user_data);
*/

void _sync_list(gpointer *data, gpointer *user_data)
{
  _image_t *img = (_image_t *)data;
  
  if(img->exists == 0)
  {
    //remove filie
    dt_image_remove(img->id);
    return;
  }

  if(img->id == -1)
  {
    //add file
    gchar *fullpath = NULL;
    fullpath = dt_util_dstrcat(fullpath, "%s/%s", img->path, img->filename);
    /* TODO: Check if JPEGs are set to be ignored */
    dt_image_import(img->filmid, fullpath, 1);
    g_free(fullpath);
    return;
  }
}

void view_popup_menu_onSync (GtkWidget *menuitem, gpointer userdata)
{
  GtkTreeView *treeview = GTK_TREE_VIEW(userdata);
  GtkTreeSelection *selection;
  GtkTreeIter iter;
  GtkTreeModel *model;
  gchar *tree_path = NULL;
  gchar *query = NULL;
  sqlite3_stmt *stmt, *stmt2;
  GList *filelist = NULL;
  int count_new = 0;
  int count_found = 0;
  
  model = gtk_tree_view_get_model(treeview);
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
  gtk_tree_selection_get_selected(selection, &model, &iter);
  gtk_tree_model_get(model, &iter, DT_LIB_COLLECT_COL_PATH, &tree_path, -1);

  query = dt_util_dstrcat(query, "select id,folder from film_rolls where folder like '%s%%'", tree_path);
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), query, -1, &stmt, NULL);
  g_free(query);
  query = NULL;

  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    int film_id;
    gchar *path;
    GDir *dir;
    GError *error;

    film_id = sqlite3_column_int(stmt, 0);
    path = (gchar *) sqlite3_column_text(stmt, 1);

    /* Añadir nombre de fichero... por ahora sólo tenemos folders! 
       Tenemos que hacer un query por los ficheros, no por los filmrolls,
       el fichero tiene el id del filmroll, tomar el nombre del id (ver
       si hay ya helper function)*/
    query = dt_util_dstrcat(query, "select filename,id from images where film_id=%d", film_id);

    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), query, -1, &stmt2, NULL);
    g_free(query);

    while (sqlite3_step(stmt2) == SQLITE_ROW)
    {
      _image_t *img = malloc(sizeof(_image_t));

      img->id = sqlite3_column_int(stmt, 1);
      img->filmid = film_id;
      img->path = path;
      img->filename = g_strdup((gchar *)sqlite3_column_text(stmt2, 0));
      img->exists = 0;

      filelist = g_list_prepend (filelist, (gpointer *)img);
      g_free(img->filename);
      g_free(img);
    }

    dir = g_dir_open(path, 0, &error);
    /* TODO: check here for error output */

    gboolean found = 0;
    
    /* TODO: what happens if there are new subdirs? */
    const gchar *name = g_dir_read_name(dir);
    while (name != NULL)
    {
      for (int i=0; i<g_list_length(filelist); i++)
      {
        _image_t *tmp;
        tmp = g_list_nth_data(filelist, i);
        if(!g_strcmp0(tmp->filename, name))
        {
          // Should we check the path as well ??
          tmp->exists = 1;
          found = 1;
          count_found++;
          break;
        }
      }

      if (!found)
      {
        /* TODO: Check if file is supported.
         * If it is JPEG check if we should import it */
        _image_t *new = malloc(sizeof(_image_t));
        new->id = -1;
        new->path = g_strdup(path);
        new->filename = g_strdup(name);
        new->exists = 1;

        filelist = g_list_append(filelist, (gpointer *)new);

        count_new++;
      }

      name = g_dir_read_name(dir);
    }
  }
 
  /* Call now the foreach function that gives the total data */
  int count_missing = g_list_length(filelist) - count_new - count_found;

  /* Produce the dialog */
  GtkWidget *win = dt_ui_main_window(darktable.gui->ui);
  GtkWidget *dialog = gtk_message_dialog_new (GTK_WINDOW(win),
                                          GTK_DIALOG_DESTROY_WITH_PARENT,
                                          GTK_MESSAGE_QUESTION,
                                          GTK_BUTTONS_YES_NO,
                                          "_(There are %d new images and %d deleted images. Do you want to sync this folder?)", count_new, 
                                          count_missing);

  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
  {
    /* TODO: Get dialog returned options so we can choose only adding or deleting*/

    /* Proceed with sync */
    for (int j=0; j < g_list_length(filelist); j++)
    {
      _image_t *img;
      img = (_image_t *)g_list_nth_data(filelist, j);
      if (img->id == -1)
      {
        /* This is a new image */
        gchar *filename = NULL;
        filename = dt_util_dstrcat(filename, "%s/%s", img->path, img->filename);

        if(dt_image_import(img->filmid, filename, 0))
          dt_control_queue_redraw_center();           //TODO: Set ignore JPEGs according to prefs.
      }
      else if (img->id != -1 && img->exists == 0)
      {
        dt_image_remove(img->id);
      }

    }

  }
  gtk_widget_destroy (dialog);

}

void view_popup_menu_onSearchFilmroll (GtkWidget *menuitem, gpointer userdata)
{
  GtkTreeView *treeview = GTK_TREE_VIEW(userdata);
  GtkWidget *win = dt_ui_main_window(darktable.gui->ui);
  GtkWidget *filechooser;

  GtkTreeSelection *selection;
  GtkTreeIter iter, child;
  GtkTreeModel *model;

  gchar *tree_path = NULL;
  gchar *new_path = NULL;

  filechooser = gtk_file_chooser_dialog_new (_("search filmroll"),
                         GTK_WINDOW (win),
                         GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                         GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                         (char *)NULL);

  gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(filechooser), FALSE);

  model = gtk_tree_view_get_model(treeview);
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
  gtk_tree_selection_get_selected(selection, &model, &iter);
  child = iter;
  gtk_tree_model_iter_parent(model, &iter, &child);
  gtk_tree_model_get(model, &child, DT_LIB_COLLECT_COL_PATH, &tree_path, -1);

  if(tree_path != NULL)
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER (filechooser), tree_path);
  else
    goto error;

  // run the dialog
  if (gtk_dialog_run (GTK_DIALOG (filechooser)) == GTK_RESPONSE_ACCEPT)
  {
    gint id = -1;
    sqlite3_stmt *stmt;
    gchar *query = NULL;

    /* If we want to allow the user to just select the folder, we have to use 
     * gtk_file_chooser_get_uri() instead. THe code should be adjusted then,
     * as it returns a file:/// URI and with utf8 characters escaped.
     */
    new_path = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER (filechooser));
    if (new_path)
    {
      gchar *old = NULL;
      query = dt_util_dstrcat(query, "select id,folder from film_rolls where folder like '%s%%'", tree_path);
      DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), query, -1, &stmt, NULL);
      g_free(query);

      while (sqlite3_step(stmt) == SQLITE_ROW)
      {
        id = sqlite3_column_int(stmt, 0);
        old = (gchar *) sqlite3_column_text(stmt, 1);
        
        query = NULL;
        query = dt_util_dstrcat(query, "update film_rolls set folder=?1 where id=?2");

        gchar trailing[1024];
        gchar final[1024];

        if (g_strcmp0(old, tree_path))
        {
          g_snprintf(trailing, 1024, "%s", old + strlen(tree_path)+1);
          g_snprintf(final, 1024, "%s/%s", new_path, trailing);
        }
        else
        {
          g_snprintf(final, 1024, "%s", new_path);
        }

        sqlite3_stmt *stmt2;
        DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), query, -1, &stmt2, NULL);
        DT_DEBUG_SQLITE3_BIND_TEXT(stmt2, 1, final, strlen(final), SQLITE_STATIC);
        DT_DEBUG_SQLITE3_BIND_INT(stmt2, 2, id);
        sqlite3_step(stmt2);
        sqlite3_finalize(stmt2);
      }
      g_free(query);

      /* reset filter to display all images, otherwise view may remain empty */
      dt_view_filter_reset_to_show_all(darktable.view_manager);

      /* update collection to view missing filmroll */
      _lib_folders_update_collection(new_path);

      dt_control_signal_raise(darktable.signals, DT_SIGNAL_FILMROLLS_CHANGED);
    }
    else
      goto error;
  }
  g_free(tree_path);
  g_free(new_path);
  gtk_widget_destroy (filechooser);
  return;

error:
  /* Something wrong happened */
  gtk_widget_destroy (filechooser);
  dt_control_log(_("Problem selecting new path for the filmroll in %s"), tree_path);

  g_free(tree_path);
  g_free(new_path); 
}

void view_popup_menu_onRemove (GtkWidget *menuitem, gpointer userdata)
{
  GtkTreeView *treeview = GTK_TREE_VIEW(userdata);

  GtkTreeSelection *selection;
  GtkTreeIter iter;
  GtkTreeModel *model;

  gchar *filmroll_path = NULL;
  gchar *fullq = NULL;
  
  /* Get info about the filmroll (or parent) selected */
  model = gtk_tree_view_get_model(treeview);
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
  gtk_tree_selection_get_selected(selection, &model, &iter);
  gtk_tree_model_get(model, &iter, DT_LIB_COLLECT_COL_PATH, &filmroll_path, -1);

  /* Clean selected images, and add to the table those which are going to be deleted */
  DT_DEBUG_SQLITE3_EXEC(dt_database_get(darktable.db), "delete from selected_images", NULL, NULL, NULL);
 
  fullq = dt_util_dstrcat(fullq, "insert into selected_images select id from images where film_id  in (select id from film_rolls where folder like '%s%%')", filmroll_path);
  DT_DEBUG_SQLITE3_EXEC(dt_database_get(darktable.db), fullq, NULL, NULL, NULL);

  dt_control_remove_images();
}

void
view_popup_menu (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{
  GtkWidget *menu, *menuitem;

  menu = gtk_menu_new();

  menuitem = gtk_menu_item_new_with_label(_("search filmroll..."));
  g_signal_connect(menuitem, "activate",
                   (GCallback) view_popup_menu_onSearchFilmroll, treeview);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

  /* FIXME: give some functionality */
  menuitem = gtk_menu_item_new_with_label(_("sync..."));
  g_signal_connect(menuitem, "activate",
                   (GCallback) view_popup_menu_onSync, treeview);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

  menuitem = gtk_menu_item_new_with_label(_("remove..."));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
  g_signal_connect(menuitem, "activate",
                   (GCallback) view_popup_menu_onRemove, treeview);

  gtk_widget_show_all(menu);

  /* Note: event can be NULL here when called from view_onPopupMenu;
   *  gdk_event_get_time() accepts a NULL argument */
  gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
                 (event != NULL) ? event->button : 0,
                 gdk_event_get_time((GdkEvent*)event));
}

gboolean
view_onButtonPressed (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{
  /* single click with the right mouse button? */
  if (event->type == GDK_BUTTON_PRESS  &&  event->button == 3)
  {
    GtkTreeSelection *selection;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));

    /* Note: gtk_tree_selection_count_selected_rows() does not
     *   exist in gtk+-2.0, only in gtk+ >= v2.2 ! */
    if (gtk_tree_selection_count_selected_rows(selection)  <= 1)
    {
       GtkTreePath *path;

       /* Get tree path for row that was clicked */
       if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(treeview),
                                         (gint) event->x,
                                         (gint) event->y,
                                         &path, NULL, NULL, NULL))
       {
         gtk_tree_selection_unselect_all(selection);
         gtk_tree_selection_select_path(selection, path);
         gtk_tree_path_free(path);
       }
    }
    view_popup_menu(treeview, event, userdata);

    return TRUE; /* we handled this */
  }
  return FALSE; /* we did not handle this */
}

gboolean
view_onPopupMenu (GtkWidget *treeview, gpointer userdata)
{
  view_popup_menu(treeview, NULL, userdata);

  return TRUE; /* we handled this */
}

static int
_count_images(const char *path)
{
  sqlite3_stmt *stmt = NULL;
  gchar query[1024] = {0};
  int count = 0;

  snprintf(query, 1024, "select count(id) from images where film_id in (select id from film_rolls where folder like '%s%%')", path);
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), query, -1, &stmt, NULL);
  if (sqlite3_step(stmt) == SQLITE_ROW)
    count = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);

  return count;
}

static gboolean
_filmroll_is_present(const gchar *path)
{
  return g_file_test(path, G_FILE_TEST_IS_DIR);
}

static void
_show_filmroll_present(GtkTreeViewColumn *column,
                  GtkCellRenderer   *renderer,
                  GtkTreeModel      *model,
                  GtkTreeIter       *iter,
                  gpointer          user_data)
{
  gchar *path, *pch;
  gtk_tree_model_get(model, iter, DT_LIB_COLLECT_COL_PATH, &path, -1);
  gtk_tree_model_get(model, iter, DT_LIB_COLLECT_COL_TEXT, &pch, -1);

  g_object_set(renderer, "text", pch, NULL);
  g_object_set(renderer, "strikethrough", TRUE, NULL);

  if (!_filmroll_is_present(path))
    g_object_set(renderer, "strikethrough-set", TRUE, NULL);
  else
    g_object_set(renderer, "strikethrough-set", FALSE, NULL);
}


static GtkTreeStore *
_folder_tree ()
{
  /* intialize the tree store */
  char query[1024];
  sqlite3_stmt *stmt;
  snprintf(query, 1024, "select * from film_rolls");
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), query, -1, &stmt, NULL);
  GtkTreeStore *store = gtk_tree_store_new(DT_LIB_COLLECT_NUM_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INT);

  // initialize the model with the paths

  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    if(strchr((const char *)sqlite3_column_text(stmt, 2),'/')==0)
    {
      // Do nothing here
    }
    else
    {
      int level = 0;
      char *value;
      GtkTreeIter current, iter;
      GtkTreePath *root;
      char *path = g_strdup((char *)sqlite3_column_text(stmt, 2));
      char *pch = strtok((char *)sqlite3_column_text(stmt, 2),"/");
      char *external = g_strdup((char *)sqlite3_column_text(stmt, 3));

      if (external == NULL)
        external = g_strdup("Local");

      gboolean found=FALSE;

      root = gtk_tree_path_new_first();
      gtk_tree_model_get_iter (GTK_TREE_MODEL(store), &iter, root);

      int children = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store),NULL);
      for (int k=0;k<children;k++)
      {
        if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(store), &iter, NULL, k))
        {
          gtk_tree_model_get (GTK_TREE_MODEL(store), &iter, 0, &value, -1);

          if (strcmp(value, external)==0)
          {
            found = TRUE;
            current = iter;
            break;
          }
	      }
      }

      if (!found)
      {
        gtk_tree_store_insert(store, &iter, NULL, 0);
        gtk_tree_store_set(store, &iter, 0, external, -1);
        current = iter;
      }

      level=1;
      while (pch != NULL)
      {
        found = FALSE;
        int children = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store),level>0?&current:NULL);
        /* find child with name, if not found create and continue */
        for (int k=0;k<children;k++)
        {
          if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(store), &iter, level>0?&current:NULL, k))
          {
            gtk_tree_model_get (GTK_TREE_MODEL(store), &iter, 0, &value, -1);

            if (strcmp(value, pch)==0)
            {
              current = iter;
              found = TRUE;
              break;
            }
          }
        }

        /* lets add new path and assign current */
        if (!found)
        {
          const char *pth = g_strndup (path, strstr(path, pch)-path);
          const char *pth2 = g_strconcat(pth, pch, NULL);

          int count = _count_images(pth2);
          gtk_tree_store_insert(store, &iter, level>0?&current:NULL,0);
          gtk_tree_store_set(store, &iter, DT_LIB_COLLECT_COL_TEXT, pch, DT_LIB_COLLECT_COL_PATH, pth2, DT_LIB_COLLECT_COL_COUNT, count, -1);
          current = iter;
        }

        level++;
        pch = strtok(NULL, "/");
      }
    }
  }

  return store;
}

static GtkTreeModel *
_create_filtered_model (GtkTreeModel *model, GtkTreeIter iter)
{
  GtkTreeModel *filter = NULL;
  GtkTreePath *path;
  GtkTreeIter child;

  /* Filter level */
  while (gtk_tree_model_iter_has_child(model, &iter))
  {
    if (gtk_tree_model_iter_n_children(model, &iter) == 1)
    {
      gtk_tree_model_iter_children(model, &child, &iter);

      if (gtk_tree_model_iter_n_children(model, &child) != 0)
        iter = child;
      else
        break;
    }
    else
      break;
	}

  path = gtk_tree_model_get_path (model, &iter);

  /* Create filter and set virtual root */
  filter = gtk_tree_model_filter_new (model, path);
  gtk_tree_path_free (path);

  return filter;
}

static GtkTreeView *
_create_treeview_display (GtkTreeModel *model)
{
  GtkTreeView *tree;
  GtkCellRenderer *renderer, *renderer2;
  GtkTreeViewColumn *col1, *col2;

  tree = GTK_TREE_VIEW(gtk_tree_view_new ());

  /* Create columns */
  col1 = gtk_tree_view_column_new();
  gtk_tree_view_append_column(tree, col1);
  gtk_tree_view_column_set_sizing(col1, GTK_TREE_VIEW_COLUMN_FIXED);
  gtk_tree_view_column_set_fixed_width (col1, 230);
  gtk_tree_view_column_set_max_width (col1, 230);
  
  renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_column_pack_start(col1, renderer, TRUE);
  gtk_tree_view_column_add_attribute(col1, renderer, "text", DT_LIB_COLLECT_COL_TEXT);
  
  gtk_tree_view_column_set_cell_data_func(col1, renderer, _show_filmroll_present, NULL, NULL);

  col2 = gtk_tree_view_column_new();
  gtk_tree_view_append_column(tree,col2);
  
  renderer2 = gtk_cell_renderer_text_new();
  gtk_tree_view_column_pack_start(col2, renderer2, TRUE);
  gtk_tree_view_column_add_attribute(col2, renderer2, "text", DT_LIB_COLLECT_COL_COUNT);
  
  gtk_tree_view_set_model(tree, GTK_TREE_MODEL(model));
  
  gtk_tree_view_set_headers_visible(tree, FALSE);

  /* free store, treeview has its own storage now */
  g_object_unref(model);

  return tree;
}

static void _lib_folders_update_collection(const gchar *filmroll)
{

  gchar *complete_query = NULL;

  complete_query = dt_util_dstrcat(complete_query, "film_id in (select id from film_rolls where folder like '%s%%')", filmroll);

  dt_conf_set_string("plugins/lighttable/where_ext_query", complete_query);
  dt_conf_set_bool("plugins/lighttable/alt_query", 1);

  dt_collection_update_query(darktable.collection);

  g_free(complete_query);

  // remove from selected images where not in this query.
  sqlite3_stmt *stmt = NULL;
  const gchar *cquery = dt_collection_get_query(darktable.collection);
  complete_query = NULL;
  if(cquery && cquery[0] != '\0')
  {
    complete_query = dt_util_dstrcat(complete_query, "delete from selected_images where imgid not in (%s)", cquery);
    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), complete_query, -1, &stmt, NULL);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, 0);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, -1);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    /* free allocated strings */
    g_free(complete_query);
  }

  /* raise signal of collection change, only if this is an orginal */
  if (!darktable.collection->clone)
    dt_control_signal_raise(darktable.signals, DT_SIGNAL_COLLECTION_CHANGED);
}


#if 0
static void mount_changed (GVolumeMonitor *volume_monitor, GMount *mount, gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  dt_lib_folders_t *d = (dt_lib_folders_t *)self->data;

  d->mounts = g_volume_monitor_get_mounts(d->gv_monitor);
  _draw_tree_gui(self);
}
#endif

void destroy_widget (gpointer data)
{
  GtkWidget *widget = (GtkWidget *)data;

  gtk_widget_destroy(widget);
}

#if 0

void gui_init(dt_lib_module_t *self)
{
  /* initialize ui widgets */
  dt_lib_folders_t *d = (dt_lib_folders_t *)g_malloc(sizeof(dt_lib_folders_t));
  memset(d,0,sizeof(dt_lib_folders_t));
  self->data = (void *)d;
  self->widget = gtk_vbox_new(FALSE, 5);

  dt_control_signal_connect(darktable.signals, 
			    DT_SIGNAL_FILMROLLS_CHANGED,
			    G_CALLBACK(collection_updated),
			    self);

  d->box_tree = GTK_BOX(gtk_vbox_new(FALSE,5));

  /* set the monitor */
  d->gv_monitor = g_volume_monitor_get ();

//  g_signal_connect(G_OBJECT(d->gv_monitor), "mount-added", G_CALLBACK(mount_changed), self);
//  g_signal_connect(G_OBJECT(d->gv_monitor), "mount-removed", G_CALLBACK(mount_changed), self);
//  g_signal_connect(G_OBJECT(d->gv_monitor), "mount-changed", G_CALLBACK(mount_changed), self);

  d->mounts = g_volume_monitor_get_mounts(d->gv_monitor);
  
  _lib_folders_gui_update(self);

  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(d->box_tree), TRUE, TRUE, 0);
}

void gui_cleanup(dt_lib_module_t *self)
{
  dt_lib_folders_t *d = (dt_lib_folders_t*)self->data;

  dt_control_signal_disconnect(darktable.signals, G_CALLBACK(collection_updated), self);

  /* cleanup mem */
  g_ptr_array_free(d->buttons, TRUE);
  g_ptr_array_free(d->trees, TRUE);

  /* TODO: Cleanup gtktreestore and gtktreemodel all arounf the code */
  g_free(self->data);
  self->data = NULL;
}
#endif

/* Folders module code ends here
 * *********************************************************************************
*/


static void _lib_collect_gui_update (dt_lib_module_t *d);


const char*
name ()
{
  return _("collect images");
}


void init_presets(dt_lib_module_t *self)
{
}

/* Update the params struct with active ruleset */
static void _lib_collect_update_params(dt_lib_collect_t *d) {
  /* reset params */
  dt_lib_collect_params_t *p = d->params;
  memset(p,0,sizeof(dt_lib_collect_params_t));

  /* for each active rule set update params */
  const int active = CLAMP(dt_conf_get_int("plugins/lighttable/collect/num_rules") - 1, 0, (MAX_RULES-1));
  char confname[200];
  for (int i=0; i<=active; i++) {
    /* get item */
    snprintf(confname, 200, "plugins/lighttable/collect/item%1d", i);
    p->rule[i].item = dt_conf_get_int(confname);
    
    /* get mode */
    snprintf(confname, 200, "plugins/lighttable/collect/mode%1d", i);
    p->rule[i].mode = dt_conf_get_int(confname);

    /* get string */
    snprintf(confname, 200, "plugins/lighttable/collect/string%1d", i);
    gchar* string = dt_conf_get_string(confname);
    if (string != NULL) {
      snprintf(p->rule[i].string,PARAM_STRING_SIZE,"%s", string);
      g_free(string);
    }

    fprintf(stderr,"[%i] %d,%d,%s\n",i, p->rule[i].item, p->rule[i].mode,  p->rule[i].string);
  }
  
  p->rules = active+1;

}

void *get_params(dt_lib_module_t *self, int *size)
{
  _lib_collect_update_params(self->data);

  /* allocate a copy of params to return, freed by caller */
  *size = sizeof(dt_lib_collect_params_t);
  void *p = malloc(*size);
  memcpy(p,((dt_lib_collect_t *)self->data)->params,*size);
  return p;
}

int set_params(dt_lib_module_t *self, const void *params, int size)
{
  /* update conf settings from params */
  dt_lib_collect_params_t *p = (dt_lib_collect_params_t *)params;
  char confname[200];
  
  for (int i=0; i<p->rules; i++) {
    /* set item */
    snprintf(confname, 200, "plugins/lighttable/collect/item%1d", i);
    dt_conf_set_int(confname, p->rule[i].item);
    
    /* set mode */
    snprintf(confname, 200, "plugins/lighttable/collect/mode%1d", i);
    dt_conf_set_int(confname, p->rule[i].mode);
    
    /* set string */
    snprintf(confname, 200, "plugins/lighttable/collect/string%1d", i);
    dt_conf_set_string(confname, p->rule[i].string);
  }

  /* set number of rules */
  snprintf(confname, 200, "plugins/lighttable/collect/num_rules");
  dt_conf_set_int(confname, p->rules);

  /* update ui */
  _lib_collect_gui_update(self);

  /* update view */
  dt_conf_set_bool("plugins/lighttable/alt_query", 0);
  dt_collection_update_query(darktable.collection);

  return 0;
}


uint32_t views()
{
  return DT_VIEW_LIGHTTABLE;
}

uint32_t container()
{
  return DT_UI_CONTAINER_PANEL_LEFT_CENTER;
}

static dt_lib_collect_t*
get_collect (dt_lib_collect_rule_t *r)
{
  dt_lib_collect_t *d = (dt_lib_collect_t *)(((char *)r) - r->num*sizeof(dt_lib_collect_rule_t));
  return d;
}

static gboolean
changed_callback (GtkEntry *entry, dt_lib_collect_rule_t *dr)
{
  // update related list
  dt_lib_collect_t *d = get_collect(dr);
  sqlite3_stmt *stmt;
  GtkTreeIter iter;
  

  GtkWidget *button;
  GtkTreeView *tree;
  GtkTreeView *view;
  GtkTreeModel *listmodel;
  GtkTreeModel *treemodel;

  gtk_widget_hide(GTK_WIDGET(d->box));
  gtk_widget_hide(GTK_WIDGET(d->scrolledwindow));

  view = d->view;
  listmodel = d->listmodel;
  g_object_ref(listmodel);
  gtk_tree_view_set_model(GTK_TREE_VIEW(view), NULL);
  gtk_list_store_clear(GTK_LIST_STORE(listmodel));
  
  /* We have already inited the GUI once, clean around */
  if (d->buttons != NULL)
  {
    for (int i=0; i<d->buttons->len; i++)
    {
      button = GTK_WIDGET(g_ptr_array_index (d->buttons, i));
      g_ptr_array_free(d->buttons, TRUE);
    }
    d->buttons = NULL;
  }
   
  if (d->trees != NULL)
  {
    for (int i=0; i<d->trees->len; i++)
    {
      tree = GTK_TREE_VIEW(g_ptr_array_index (d->trees, i));
      g_ptr_array_free(d->trees, TRUE);
    }
    d->trees = NULL;
  }

  
  char query[1024];
  int property = gtk_combo_box_get_active(dr->combo);
  const gchar *text = gtk_entry_get_text(GTK_ENTRY(dr->text));
  gchar *escaped_text = dt_util_str_replace(text, "'", "''");
  char confname[200];
  snprintf(confname, 200, "plugins/lighttable/collect/string%1ld", dr->num);
  dt_conf_set_string (confname, text);
  snprintf(confname, 200, "plugins/lighttable/collect/item%1ld", dr->num);
  dt_conf_set_int (confname, property);

  switch(property)
  {
    case 0: // film roll
      goto filmroll;
      break;
    case 1: // camera
      snprintf(query, 1024, "select distinct maker || ' ' || model as model, 1 from images where maker || ' ' || model like '%%%s%%' order by model", escaped_text);
      break;
    case 2: // tag
      snprintf(query, 1024, "SELECT distinct name, id FROM tags WHERE name LIKE '%%%s%%' ORDER BY UPPER(name)", escaped_text);
      break;
    case 4: // History, 2 hardcoded alternatives
      gtk_list_store_append(GTK_LIST_STORE(listmodel), &iter);
      gtk_list_store_set (GTK_LIST_STORE(listmodel), &iter,
                          DT_LIB_COLLECT_COL_TEXT,_("altered"),
                          DT_LIB_COLLECT_COL_ID, 0,
                          DT_LIB_COLLECT_COL_TOOLTIP,_("altered"),
                          -1);
      gtk_list_store_append(GTK_LIST_STORE(listmodel), &iter);
      gtk_list_store_set (GTK_LIST_STORE(listmodel), &iter,
                          DT_LIB_COLLECT_COL_TEXT,_("not altered"),
                          DT_LIB_COLLECT_COL_ID, 1,
                          DT_LIB_COLLECT_COL_TOOLTIP,_("not altered"),
                          -1);
      goto entry_key_press_exit;
      break;

    case 5: // colorlabels
      gtk_list_store_append(GTK_LIST_STORE(listmodel), &iter);
      gtk_list_store_set (GTK_LIST_STORE(listmodel), &iter,
                          DT_LIB_COLLECT_COL_TEXT,_("red"),
                          DT_LIB_COLLECT_COL_ID, 0,
                          DT_LIB_COLLECT_COL_TOOLTIP, _("red"),
                          -1);
      gtk_list_store_append(GTK_LIST_STORE(listmodel), &iter);
      gtk_list_store_set (GTK_LIST_STORE(listmodel), &iter,
                          DT_LIB_COLLECT_COL_TEXT,_("yellow"),
                          DT_LIB_COLLECT_COL_ID, 1,
                          DT_LIB_COLLECT_COL_TOOLTIP, _("yellow"),
                          -1);
      gtk_list_store_append(GTK_LIST_STORE(listmodel), &iter);
      gtk_list_store_set (GTK_LIST_STORE(listmodel), &iter,
                          DT_LIB_COLLECT_COL_TEXT,_("green"),
                          DT_LIB_COLLECT_COL_ID, 2,
                          DT_LIB_COLLECT_COL_TOOLTIP, _("green"),
                          -1);
      gtk_list_store_append(GTK_LIST_STORE(listmodel), &iter);
      gtk_list_store_set (GTK_LIST_STORE(listmodel), &iter,
                          DT_LIB_COLLECT_COL_TEXT,_("blue"),
                          DT_LIB_COLLECT_COL_ID, 3,
                          DT_LIB_COLLECT_COL_TOOLTIP, _("blue"),
                          -1);
      gtk_list_store_append(GTK_LIST_STORE(listmodel), &iter);
      gtk_list_store_set (GTK_LIST_STORE(listmodel), &iter,
                          DT_LIB_COLLECT_COL_TEXT,_("purple"),
                          DT_LIB_COLLECT_COL_ID, 4,
                          DT_LIB_COLLECT_COL_TOOLTIP, _("purple"),
                          -1);
      goto entry_key_press_exit;
      break;

      // TODO: Add empty string for metadata?
      // TODO: Autogenerate this code?
    case 6: // title
      snprintf(query, 1024, "select distinct value, 1 from meta_data where key = %d and value like '%%%s%%' order by value",
               DT_METADATA_XMP_DC_TITLE, escaped_text);
      break;
    case 7: // description
      snprintf(query, 1024, "select distinct value, 1 from meta_data where key = %d and value like '%%%s%%' order by value",
               DT_METADATA_XMP_DC_DESCRIPTION, escaped_text);
      break;
    case 8: // creator
      snprintf(query, 1024, "select distinct value, 1 from meta_data where key = %d and value like '%%%s%%' order by value",
               DT_METADATA_XMP_DC_CREATOR, escaped_text);
      break;
    case 9: // publisher
      snprintf(query, 1024, "select distinct value, 1 from meta_data where key = %d and value like '%%%s%%' order by value",
               DT_METADATA_XMP_DC_PUBLISHER, escaped_text);
      break;
    case 10: // rights
      snprintf(query, 1024, "select distinct value, 1 from meta_data where key = %d and value like '%%%s%%'order by value ",
               DT_METADATA_XMP_DC_RIGHTS, escaped_text);
      break;
    case 11: // lens
      snprintf(query, 1024, "select distinct lens, 1 from images where lens like '%%%s%%' order by lens", escaped_text);
      break;
    case 12: // iso
      snprintf(query, 1024, "select distinct cast(iso as integer) as iso, 1 from images where iso like '%%%s%%' order by iso", escaped_text);
      break;
    case 13: // aperature
      snprintf(query, 1024, "select distinct round(aperture,1) as aperture, 1 from images where aperture like '%%%s%%' order by aperture", escaped_text);
      break;
    case 14: // filename
      snprintf(query, 1024, "select distinct filename, 1 from images where filename like '%%%s%%' order by filename", escaped_text);
      break;

    default: // case 3: // day
      snprintf(query, 1024, "SELECT DISTINCT datetime_taken, 1 FROM images WHERE datetime_taken LIKE '%%%s%%' ORDER BY datetime_taken DESC", escaped_text);
      break;
  }
  g_free(escaped_text);
  
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), query, -1, &stmt, NULL);
  while(sqlite3_step(stmt) == SQLITE_ROW)
  {
    gtk_list_store_append(GTK_LIST_STORE(listmodel), &iter);
    const char *folder = (const char*)sqlite3_column_text(stmt, 0);
    if(property == 0) // film roll
    {
      folder = dt_image_film_roll_name(folder);
    }
    gchar *value =  (gchar *)sqlite3_column_text(stmt, 0);
    gchar *escaped_text = g_markup_escape_text(value, strlen(value));
    gtk_list_store_set (GTK_LIST_STORE(listmodel), &iter,
                        DT_LIB_COLLECT_COL_TEXT, folder,
                        DT_LIB_COLLECT_COL_ID, sqlite3_column_int(stmt, 1),
                        DT_LIB_COLLECT_COL_TOOLTIP, escaped_text,
                        DT_LIB_COLLECT_COL_PATH, value,
                        -1);
  }
  sqlite3_finalize(stmt);

  goto entry_key_press_exit;

filmroll:
  /* TODO: Only create a new tree if something has changed
   * This will allow to cache the node, and not collapse the tree */
  d->treemodel = GTK_TREE_MODEL(_folder_tree());
  treemodel = d->treemodel;

  /* set the UI */
  GtkTreeModel *model2;
  
  GtkTreePath *root = gtk_tree_path_new_first();
  gtk_tree_model_get_iter (GTK_TREE_MODEL(treemodel), &iter, root);

  int children = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(treemodel), NULL);
  d->buttons = g_ptr_array_sized_new(children);
  g_ptr_array_set_free_func (d->buttons, destroy_widget);

  d->trees = g_ptr_array_sized_new(children);
  g_ptr_array_set_free_func (d->trees, destroy_widget);

  for (int i=0; i<children; i++)
  {
    GValue value;
    memset(&value,0,sizeof(GValue));
    gtk_tree_model_iter_nth_child (GTK_TREE_MODEL(treemodel), &iter, NULL, i);

  	gtk_tree_model_get_value (GTK_TREE_MODEL(treemodel), &iter, 0, &value);
    
    gchar *mount_name = g_value_dup_string(&value);

    if (g_strcmp0(mount_name, "Local")==0)
    {
      /* Add a button for local filesystem, to keep UI consistency */
      button = gtk_button_new_with_label (_("Local HDD"));
    }
    else
    {
      button = gtk_button_new_with_label (mount_name);
    }
    g_ptr_array_add(d->buttons, (gpointer) button);
    gtk_container_add(GTK_CONTAINER(d->box), GTK_WIDGET(button));
    
    model2 = _create_filtered_model(GTK_TREE_MODEL(treemodel), iter);
    tree = _create_treeview_display(GTK_TREE_MODEL(model2));
    g_ptr_array_add(d->trees, (gpointer) tree);
    gtk_container_add(GTK_CONTAINER(d->box), GTK_WIDGET(tree));

    gtk_tree_view_set_headers_visible(tree, FALSE);

    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(view), GTK_SELECTION_SINGLE);
    
    g_signal_connect(G_OBJECT (tree), "row-activated", G_CALLBACK (row_activated), d);
    g_signal_connect(G_OBJECT (tree), "button-press-event", G_CALLBACK (view_onButtonPressed), NULL);
    g_signal_connect(G_OBJECT (tree), "popup-menu", G_CALLBACK (view_onPopupMenu), NULL);
    
    g_value_unset(&value);
    //g_free(mount_name);
  }
  
  gtk_widget_show_all(GTK_WIDGET(d->box));
  g_object_unref(listmodel);

  return FALSE;

entry_key_press_exit:
  gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(view), DT_LIB_COLLECT_COL_TOOLTIP);
  gtk_tree_view_set_model(GTK_TREE_VIEW(view), listmodel);
  g_signal_connect(G_OBJECT (view), "row-activated", G_CALLBACK (row_activated), d);
  gtk_widget_show_all(GTK_WIDGET(d->scrolledwindow));
  g_object_unref(listmodel);
  return FALSE;
}

static void
_lib_collect_gui_update (dt_lib_module_t *self)
{
  dt_lib_collect_t *d = (dt_lib_collect_t *)self->data;

  const int old = darktable.gui->reset;
  darktable.gui->reset = 1;
  const int active = CLAMP(dt_conf_get_int("plugins/lighttable/collect/num_rules") - 1, 0, (MAX_RULES-1));
  char confname[200];
  for(int i=0; i<MAX_RULES; i++)
  {
    gtk_widget_set_no_show_all(d->rule[i].hbox, TRUE);
    gtk_widget_set_visible(d->rule[i].hbox, FALSE);
  }
  for(int i=0; i<=active; i++)
  {
    gtk_widget_set_no_show_all(d->rule[i].hbox, FALSE);
    gtk_widget_set_visible(d->rule[i].hbox, TRUE);
    gtk_widget_show_all(d->rule[i].hbox);
    snprintf(confname, 200, "plugins/lighttable/collect/item%1d", i);
    gtk_combo_box_set_active(GTK_COMBO_BOX(d->rule[i].combo), dt_conf_get_int(confname));
    snprintf(confname, 200, "plugins/lighttable/collect/string%1d", i);
    gchar *text = dt_conf_get_string(confname);
    if(text)
    {
      gtk_entry_set_text(GTK_ENTRY(d->rule[i].text), text);
      g_free(text);
    }

    GtkDarktableButton *button = DTGTK_BUTTON(d->rule[i].button);
    if(i == MAX_RULES - 1)
    {
      // only clear
      button->icon = dtgtk_cairo_paint_cancel;
      g_object_set(G_OBJECT(button), "tooltip-text", _("clear this rule"), (char *)NULL);
    }
    else if(i == active)
    {
      button->icon = dtgtk_cairo_paint_dropdown;
      g_object_set(G_OBJECT(button), "tooltip-text", _("clear this rule or add new rules"), (char *)NULL);
    }
    else
    {
      snprintf(confname, 200, "plugins/lighttable/collect/mode%1d", i+1);
      const int mode = dt_conf_get_int(confname);
      if(mode == DT_LIB_COLLECT_MODE_AND)     button->icon = dtgtk_cairo_paint_and;
      if(mode == DT_LIB_COLLECT_MODE_OR)      button->icon = dtgtk_cairo_paint_or;
      if(mode == DT_LIB_COLLECT_MODE_AND_NOT) button->icon = dtgtk_cairo_paint_andnot;
      g_object_set(G_OBJECT(button), "tooltip-text", _("clear this rule"), (char *)NULL);
    }
  }

  // update list of proposals
  changed_callback(NULL, d->rule + d->active_rule);
  darktable.gui->reset = old;
}

void
gui_reset (dt_lib_module_t *self)
{
  dt_conf_set_int("plugins/lighttable/collect/num_rules", 1);
  dt_conf_set_int("plugins/lighttable/collect/item0", 0);
  dt_conf_set_string("plugins/lighttable/collect/string0", "%");
  dt_collection_set_query_flags(darktable.collection,COLLECTION_QUERY_FULL);
  dt_conf_set_bool("plugins/lighttable/alt_query", 0);
  dt_collection_update_query(darktable.collection);
}

static void
combo_changed (GtkComboBox *combo, dt_lib_collect_rule_t *d)
{
  if(darktable.gui->reset) return;
  gtk_entry_set_text(GTK_ENTRY(d->text), "");
  dt_lib_collect_t *c = get_collect(d);
  c->active_rule = d->num;
  changed_callback(NULL, d);
  dt_conf_set_bool("plugins/lighttable/alt_query", 0);
  dt_collection_update_query(darktable.collection);
}

static void
row_activated (GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *col, dt_lib_collect_t *d)
{
  GtkTreeIter iter;
  GtkTreeModel *model = NULL;
  GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  if(!gtk_tree_selection_get_selected(selection, &model, &iter)) return;
  gchar *text;
  const int active = d->active_rule;
  const int item = gtk_combo_box_get_active(GTK_COMBO_BOX(d->rule[active].combo));
  if(item == 0) // get full path for film rolls:
    gtk_tree_model_get (model, &iter, DT_LIB_COLLECT_COL_PATH, &text, -1);
  else
    gtk_tree_model_get (model, &iter, DT_LIB_COLLECT_COL_TEXT, &text, -1);
  gtk_entry_set_text(GTK_ENTRY(d->rule[active].text), text);
  g_free(text);
  changed_callback(NULL, d->rule + active);
  dt_conf_set_bool("plugins/lighttable/alt_query", 0);
  dt_collection_update_query(darktable.collection);
  dt_control_queue_redraw_center();
}

static void
entry_activated (GtkWidget *entry, dt_lib_collect_rule_t *d)
{
  changed_callback(NULL, d);
  dt_lib_collect_t *c = get_collect(d);
  GtkTreeView *view = c->view;
  GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
  gint rows = gtk_tree_model_iter_n_children(model, NULL);
  if(rows == 1)
  {
    GtkTreeIter iter;
    if(gtk_tree_model_get_iter_first(model, &iter))
    {
      gchar *text;
      const int item = gtk_combo_box_get_active(GTK_COMBO_BOX(d->combo));
      if(item == 0) // get full path for film rolls:
        gtk_tree_model_get (model, &iter, DT_LIB_COLLECT_COL_PATH, &text, -1);
      else
        gtk_tree_model_get (model, &iter, DT_LIB_COLLECT_COL_TEXT, &text, -1);
      gtk_entry_set_text(GTK_ENTRY(d->text), text);
      g_free(text);
      changed_callback(NULL, d);
    }
  }
  dt_conf_set_bool("plugins/lighttable/alt_query", 0);
  dt_collection_update_query(darktable.collection);
}

int
position ()
{
  return 400;
}

static void
entry_focus_in_callback (GtkWidget *w, GdkEventFocus *event, dt_lib_collect_rule_t *d)
{
  dt_lib_collect_t *c = get_collect(d);
  c->active_rule = d->num;
  changed_callback(NULL, c->rule + c->active_rule);
}

#if 0
static void
focus_in_callback (GtkWidget *w, GdkEventFocus *event, dt_lib_module_t *self)
{
  GtkWidget *win = darktable.gui->widgets.main_window;
  GtkEntry *entry = GTK_ENTRY(self->text);
  GtkTreeView *view;
  int count = 1 + count_film_rolls(gtk_entry_get_text(entry));
  int ht = get_font_height(view, "Dreggn");
  const int size = MAX(2*ht, MIN(win->allocation.height/2, count*ht));
  gtk_widget_set_size_request(view, -1, size);
}

static void
hide_callback (GObject    *object,
               GParamSpec *param_spec,
               GtkWidget *view)
{
  GtkExpander *expander;
  expander = GTK_EXPANDER (object);
  if (!gtk_expander_get_expanded (expander))
    gtk_widget_set_size_request(view, -1, -1);
}
#endif

static void
menuitem_and (GtkMenuItem *menuitem, dt_lib_collect_rule_t *d)
{
  // add next row with and operator
  const int active = CLAMP(dt_conf_get_int("plugins/lighttable/collect/num_rules"), 1, MAX_RULES);
  if(active < 10)
  {
    char confname[200];
    snprintf(confname, 200, "plugins/lighttable/collect/mode%1d", active);
    dt_conf_set_int(confname, DT_LIB_COLLECT_MODE_AND);
    snprintf(confname, 200, "plugins/lighttable/collect/string%1d", active);
    dt_conf_set_string(confname, "");
    dt_conf_set_int("plugins/lighttable/collect/num_rules", active+1);
    dt_lib_collect_t *c = get_collect(d);
    c->active_rule = active;
  }
  dt_conf_set_bool("plugins/lighttable/alt_query", 0);
  dt_collection_update_query(darktable.collection);
}

static void
menuitem_or (GtkMenuItem *menuitem, dt_lib_collect_rule_t *d)
{
  // add next row with or operator
  const int active = CLAMP(dt_conf_get_int("plugins/lighttable/collect/num_rules"), 1, MAX_RULES);
  if(active < 10)
  {
    char confname[200];
    snprintf(confname, 200, "plugins/lighttable/collect/mode%1d", active);
    dt_conf_set_int(confname, DT_LIB_COLLECT_MODE_OR);
    snprintf(confname, 200, "plugins/lighttable/collect/string%1d", active);
    dt_conf_set_string(confname, "");
    dt_conf_set_int("plugins/lighttable/collect/num_rules", active+1);
  }
  dt_conf_set_bool("plugins/lighttable/alt_query", 0);
  dt_collection_update_query(darktable.collection);
}

static void
menuitem_and_not (GtkMenuItem *menuitem, dt_lib_collect_rule_t *d)
{
  // add next row with and not operator
  const int active = CLAMP(dt_conf_get_int("plugins/lighttable/collect/num_rules"), 1, MAX_RULES);
  if(active < 10)
  {
    char confname[200];
    snprintf(confname, 200, "plugins/lighttable/collect/mode%1d", active);
    dt_conf_set_int(confname, DT_LIB_COLLECT_MODE_AND_NOT);
    snprintf(confname, 200, "plugins/lighttable/collect/string%1d", active);
    dt_conf_set_string(confname, "");
    dt_conf_set_int("plugins/lighttable/collect/num_rules", active+1);
  }
  dt_conf_set_bool("plugins/lighttable/alt_query", 0);
  dt_collection_update_query(darktable.collection);
}

static void
menuitem_change_and (GtkMenuItem *menuitem, dt_lib_collect_rule_t *d)
{
  // add next row with and operator
  const int num = d->num + 1;
  if(num < 10 && num > 0)
  {
    char confname[200];
    snprintf(confname, 200, "plugins/lighttable/collect/mode%1d", num);
    dt_conf_set_int(confname, DT_LIB_COLLECT_MODE_AND);
  }
  dt_conf_set_bool("plugins/lighttable/alt_query", 0);
  dt_collection_update_query(darktable.collection);
}

static void
menuitem_change_or (GtkMenuItem *menuitem, dt_lib_collect_rule_t *d)
{
  // add next row with or operator
  const int num = d->num + 1;
  if(num < 10 && num > 0)
  {
    char confname[200];
    snprintf(confname, 200, "plugins/lighttable/collect/mode%1d", num);
    dt_conf_set_int(confname, DT_LIB_COLLECT_MODE_OR);
  }
  dt_conf_set_bool("plugins/lighttable/alt_query", 0);
  dt_collection_update_query(darktable.collection);
}

static void
menuitem_change_and_not (GtkMenuItem *menuitem, dt_lib_collect_rule_t *d)
{
  // add next row with and not operator
  const int num = d->num + 1;
  if(num < 10 && num > 0)
  {
    char confname[200];
    snprintf(confname, 200, "plugins/lighttable/collect/mode%1d", num);
    dt_conf_set_int(confname, DT_LIB_COLLECT_MODE_AND_NOT);
  }
  dt_conf_set_bool("plugins/lighttable/alt_query", 0);
  dt_collection_update_query(darktable.collection);
}

static void
collection_updated(gpointer instance,gpointer self)
{
  _lib_collect_gui_update((dt_lib_module_t *)self);
}


static void
menuitem_clear (GtkMenuItem *menuitem, dt_lib_collect_rule_t *d)
{
  // remove this row, or if 1st, clear text entry box
  const int active = CLAMP(dt_conf_get_int("plugins/lighttable/collect/num_rules"), 1, MAX_RULES);
  dt_lib_collect_t *c = get_collect(d);
  if(active > 1)
  {
    dt_conf_set_int("plugins/lighttable/collect/num_rules", active-1);
    if(c->active_rule >= active-1) c->active_rule = active - 2;
  }
  else
  {
    dt_conf_set_int("plugins/lighttable/collect/mode0", DT_LIB_COLLECT_MODE_AND);
    dt_conf_set_int("plugins/lighttable/collect/item0", 0);
    dt_conf_set_string("plugins/lighttable/collect/string0", "");
  }
  // move up all still active rules by one.
  for(int i=d->num; i<MAX_RULES-1; i++)
  {
    char confname[200];
    snprintf(confname, 200, "plugins/lighttable/collect/mode%1d", i+1);
    const int mode = dt_conf_get_int(confname);
    snprintf(confname, 200, "plugins/lighttable/collect/item%1d", i+1);
    const int item = dt_conf_get_int(confname);
    snprintf(confname, 200, "plugins/lighttable/collect/string%1d", i+1);
    gchar *string = dt_conf_get_string(confname);
    if(string)
    {
      snprintf(confname, 200, "plugins/lighttable/collect/mode%1d", i);
      dt_conf_set_int(confname, mode);
      snprintf(confname, 200, "plugins/lighttable/collect/item%1d", i);
      dt_conf_set_int(confname, item);
      snprintf(confname, 200, "plugins/lighttable/collect/string%1d", i);
      dt_conf_set_string(confname, string);
      g_free(string);
    }
  }
  dt_conf_set_bool("plugins/lighttable/alt_query", 0);
  dt_collection_update_query(darktable.collection);
}

static gboolean
popup_button_callback(GtkWidget *widget, GdkEventButton *event, dt_lib_collect_rule_t *d)
{
  if(event->button != 1) return FALSE;

  GtkWidget *menu = gtk_menu_new();
  GtkWidget *mi;
  const int active = CLAMP(dt_conf_get_int("plugins/lighttable/collect/num_rules"), 1, MAX_RULES);

  mi = gtk_menu_item_new_with_label(_("clear this rule"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
  g_signal_connect(G_OBJECT(mi), "activate", G_CALLBACK(menuitem_clear), d);

  if(d->num == active - 1)
  {
    mi = gtk_menu_item_new_with_label(_("narrow down search"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    g_signal_connect(G_OBJECT(mi), "activate", G_CALLBACK(menuitem_and), d);

    mi = gtk_menu_item_new_with_label(_("add more images"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    g_signal_connect(G_OBJECT(mi), "activate", G_CALLBACK(menuitem_or), d);

    mi = gtk_menu_item_new_with_label(_("exclude images"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    g_signal_connect(G_OBJECT(mi), "activate", G_CALLBACK(menuitem_and_not), d);
  }
  else if(d->num < active - 1)
  {
    mi = gtk_menu_item_new_with_label(_("change to: and"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    g_signal_connect(G_OBJECT(mi), "activate", G_CALLBACK(menuitem_change_and), d);

    mi = gtk_menu_item_new_with_label(_("change to: or"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    g_signal_connect(G_OBJECT(mi), "activate", G_CALLBACK(menuitem_change_or), d);

    mi = gtk_menu_item_new_with_label(_("change to: except"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    g_signal_connect(G_OBJECT(mi), "activate", G_CALLBACK(menuitem_change_and_not), d);
  }

  gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, event->button, event->time);
  gtk_widget_show_all(menu);

  return TRUE;
}

void
gui_init (dt_lib_module_t *self)
{
  dt_lib_collect_t *d = (dt_lib_collect_t *)malloc(sizeof(dt_lib_collect_t));

  memset(d, 0, sizeof(dt_lib_collect_t));

  self->data = (void *)d;
  self->widget = gtk_vbox_new(FALSE, 5);
  gtk_widget_set_size_request(self->widget, 100, -1);
  d->active_rule = 0;
  d->params = (dt_lib_collect_params_t*)malloc(sizeof(dt_lib_collect_params_t));

  dt_control_signal_connect(darktable.signals, 
			    DT_SIGNAL_COLLECTION_CHANGED,
			    G_CALLBACK(collection_updated),
			    self);
  
  GtkBox *box;
  GtkWidget *w;

  for(int i=0; i<MAX_RULES; i++)
  {
    d->rule[i].num = i;
    box = GTK_BOX(gtk_hbox_new(FALSE, 5));
    d->rule[i].hbox = GTK_WIDGET(box);
    gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(box), TRUE, TRUE, 0);
    w = gtk_combo_box_new_text();
    d->rule[i].combo = GTK_COMBO_BOX(w);
    for(int k=0; k<dt_lib_collect_string_cnt; k++)
      gtk_combo_box_append_text(GTK_COMBO_BOX(w), _(dt_lib_collect_string[k]));
    g_signal_connect(G_OBJECT(w), "changed", G_CALLBACK(combo_changed), d->rule + i);
    gtk_box_pack_start(box, w, FALSE, FALSE, 0);
    w = gtk_entry_new();
    dt_gui_key_accel_block_on_focus(w);
    d->rule[i].text = w;
    gtk_widget_add_events(w, GDK_FOCUS_CHANGE_MASK);
    g_signal_connect(G_OBJECT(w), "focus-in-event", G_CALLBACK(entry_focus_in_callback), d->rule + i);

    /* xgettext:no-c-format */
    g_object_set(G_OBJECT(w), "tooltip-text", _("type your query, use `%' as wildcard"), (char *)NULL);
    gtk_widget_add_events(w, GDK_KEY_PRESS_MASK);
    g_signal_connect(G_OBJECT(w), "changed", G_CALLBACK(changed_callback), d->rule + i);
    g_signal_connect(G_OBJECT(w), "activate", G_CALLBACK(entry_activated), d->rule + i);
    gtk_box_pack_start(box, w, TRUE, TRUE, 0);
    w = dtgtk_button_new(dtgtk_cairo_paint_presets, CPF_STYLE_FLAT|CPF_DO_NOT_USE_BORDER);
    d->rule[i].button = w;
    gtk_widget_set_events(w, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(G_OBJECT(w), "button-press-event", G_CALLBACK(popup_button_callback), d->rule + i);
    gtk_box_pack_start(box, w, FALSE, FALSE, 0);
    gtk_widget_set_size_request(w, 13, 13);
  }

  GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
  d->scrolledwindow = GTK_SCROLLED_WINDOW(sw);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  GtkTreeView  *view = GTK_TREE_VIEW(gtk_tree_view_new());
  d->view = view;
  gtk_tree_view_set_headers_visible(view, FALSE);
  gtk_widget_set_size_request(GTK_WIDGET(view), -1, 300);
  gtk_container_add(GTK_CONTAINER(sw), GTK_WIDGET(view));
  gtk_widget_hide(GTK_WIDGET(view));
  
  GtkTreeViewColumn *col = gtk_tree_view_column_new();
  gtk_tree_view_append_column(view, col);
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
  gtk_tree_view_column_pack_start(col, renderer, TRUE);
  gtk_tree_view_column_add_attribute(col, renderer, "text", DT_LIB_COLLECT_COL_TEXT);

  GtkTreeModel *listmodel = GTK_TREE_MODEL(gtk_list_store_new(DT_LIB_COLLECT_NUM_COLS, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT));
  d->listmodel = listmodel;
  
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(sw), TRUE, TRUE, 0);
  gtk_widget_hide(sw);


  GtkWidget *vbox = gtk_vbox_new(FALSE, 5);
  d->box = GTK_BOX(vbox);
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(d->box), TRUE, TRUE, 0);
  gtk_widget_hide(vbox);

  d->buttons = NULL;
  d->trees = NULL;

  /* setup proxy */
  darktable.view_manager->proxy.module_collect.module = self;
  darktable.view_manager->proxy.module_collect.update = _lib_collect_gui_update;

  _lib_collect_gui_update(self);
}

void
gui_cleanup (dt_lib_module_t *self)
{
  dt_control_signal_disconnect(darktable.signals, G_CALLBACK(collection_updated), self);
  darktable.view_manager->proxy.module_collect.module = NULL;
  free(((dt_lib_collect_t*)self->data)->params);
  free(self->data);
  self->data = NULL;
}

#undef MAX_RULES
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
