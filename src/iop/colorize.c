/*
    This file is part of darktable,
    copyright (c) 2011 Henrik Andersson.

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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "bauhaus/bauhaus.h"
#include "common/colorspaces.h"
#include "common/opencl.h"
#include "develop/develop.h"
#include "develop/imageop.h"
#include "control/control.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"

#include <gtk/gtk.h>
#include <inttypes.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <string.h>

DT_MODULE(1)

typedef struct dt_iop_colorize_params_t
{
  float hue;
  float saturation;
  float source_lightness_mix;
  float lightness;
}
dt_iop_colorize_params_t;

typedef struct dt_iop_colorize_gui_data_t
{
  GtkWidget *scale1,*scale2;       					//  lightness, source_lightnessmix
  GtkWidget *gslider1,*gslider2;		//hue, saturation
}
dt_iop_colorize_gui_data_t;

typedef struct dt_iop_colorize_data_t
{
  float hue;
  float saturation;
  float source_lightness_mix;
  float lightness;
}
dt_iop_colorize_data_t;

typedef struct dt_iop_colorize_global_data_t
{
  int kernel_colorize;
}
dt_iop_colorize_global_data_t;


const char *name()
{
  return _("colorize");
}

int flags()
{
  return IOP_FLAGS_INCLUDE_IN_STYLES | IOP_FLAGS_SUPPORTS_BLENDING | IOP_FLAGS_ALLOW_TILING;
}

int
groups ()
{
  return IOP_GROUP_EFFECT;
}

void init_key_accels(dt_iop_module_so_t *self)
{
  dt_accel_register_slider_iop(self, FALSE, NC_("accel", "lightness"));
  dt_accel_register_slider_iop(self, FALSE, NC_("accel", "source mix"));
}

void connect_key_accels(dt_iop_module_t *self)
{
  dt_iop_colorize_gui_data_t *g = (dt_iop_colorize_gui_data_t*)self->gui_data;

  dt_accel_connect_slider_iop(self, "lightness", GTK_WIDGET(g->scale1));
  dt_accel_connect_slider_iop(self, "source mix", GTK_WIDGET(g->scale2));
}

void process (struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, void *ivoid, void *ovoid, const dt_iop_roi_t *roi_in, const dt_iop_roi_t *roi_out)
{
  float *in, *out;
  dt_iop_colorize_data_t *d = (dt_iop_colorize_data_t *)piece->data;
  const int ch = piece->colors;

  /* create Lab */
  float rgb[3]= {0}, XYZ[3]= {0}, Lab[3]= {0};
  hsl2rgb(rgb,d->hue, d->saturation, d->lightness/100.0);

  XYZ[0] = (rgb[0] * 0.5767309) + (rgb[1] * 0.1855540) + (rgb[2] * 0.1881852);
  XYZ[1] = (rgb[0] * 0.2973769) + (rgb[1] * 0.6273491) + (rgb[2] * 0.0752741);
  XYZ[2] = (rgb[0] * 0.0270343) + (rgb[1] * 0.0706872) + (rgb[2] * 0.9911085);

  dt_XYZ_to_Lab(XYZ,Lab);


  /* a/b components */
  const float L = Lab[0];
  const float a = Lab[1];
  const float b = Lab[2];

  const float mix = d->source_lightness_mix/100.0;

#ifdef _OPENMP
  #pragma omp parallel for default(none) shared(ivoid,ovoid,roi_out) private(in,out) schedule(static)
#endif
  for(int k=0; k<roi_out->height; k++)
  {
    float lmix=(mix*100.0)/2.0;
    int stride = ch*roi_out->width;

    in = (float *)ivoid+(k*stride);
    out = (float *)ovoid+(k*stride);

    for(int l=0; l < stride; l+=ch)
    {
      out[l+0] = L-lmix + in[l+0]*mix;
      out[l+1] = a;
      out[l+2] = b;
      out[l+3] = in[l+3];
    }
  }
}

#ifdef HAVE_OPENCL
int
process_cl (struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, cl_mem dev_in, cl_mem dev_out, const dt_iop_roi_t *roi_in, const dt_iop_roi_t *roi_out)
{
  dt_iop_colorize_data_t *data = (dt_iop_colorize_data_t *)piece->data;
  dt_iop_colorize_global_data_t *gd = (dt_iop_colorize_global_data_t *)self->data;

  cl_int err = -999;
  const int devid = piece->pipe->devid;
  const int width = roi_in->width;
  const int height = roi_in->height;

  /* create Lab */
  float rgb[3]= {0}, XYZ[3]= {0}, Lab[3]= {0};
  hsl2rgb(rgb,data->hue, data->saturation, data->lightness/100.0);

  XYZ[0] = (rgb[0] * 0.5767309) + (rgb[1] * 0.1855540) + (rgb[2] * 0.1881852);
  XYZ[1] = (rgb[0] * 0.2973769) + (rgb[1] * 0.6273491) + (rgb[2] * 0.0752741);
  XYZ[2] = (rgb[0] * 0.0270343) + (rgb[1] * 0.0706872) + (rgb[2] * 0.9911085);

  dt_XYZ_to_Lab(XYZ,Lab);


  /* a/b components */
  const float L = Lab[0];
  const float a = Lab[1];
  const float b = Lab[2];
  const float mix = data->source_lightness_mix/100.0f;

  size_t sizes[] = { ROUNDUPWD(width), ROUNDUPHT(height), 1};

  dt_opencl_set_kernel_arg(devid, gd->kernel_colorize, 0, sizeof(cl_mem), (void *)&dev_in);
  dt_opencl_set_kernel_arg(devid, gd->kernel_colorize, 1, sizeof(cl_mem), (void *)&dev_out);
  dt_opencl_set_kernel_arg(devid, gd->kernel_colorize, 2, sizeof(int), (void *)&width);
  dt_opencl_set_kernel_arg(devid, gd->kernel_colorize, 3, sizeof(int), (void *)&height);
  dt_opencl_set_kernel_arg(devid, gd->kernel_colorize, 4, sizeof(float), (void *)&mix);
  dt_opencl_set_kernel_arg(devid, gd->kernel_colorize, 5, sizeof(float), (void *)&L);
  dt_opencl_set_kernel_arg(devid, gd->kernel_colorize, 6, sizeof(float), (void *)&a);
  dt_opencl_set_kernel_arg(devid, gd->kernel_colorize, 7, sizeof(float), (void *)&b);
  err = dt_opencl_enqueue_kernel_2d(devid, gd->kernel_colorize, sizes);
  if(err != CL_SUCCESS) goto error;
  return TRUE;

error:
  dt_print(DT_DEBUG_OPENCL, "[opencl_colorize] couldn't enqueue kernel! %d\n", err);
  return FALSE;
}
#endif

void init_global(dt_iop_module_so_t *module)
{
  const int program = 8; // extended.cl, from programs.conf
  dt_iop_colorize_global_data_t *gd = (dt_iop_colorize_global_data_t *)malloc(sizeof(dt_iop_colorize_global_data_t));
  module->data = gd;
  gd->kernel_colorize = dt_opencl_create_kernel(program, "colorize");
}

void cleanup_global(dt_iop_module_so_t *module)
{
  dt_iop_colorize_global_data_t *gd = (dt_iop_colorize_global_data_t *)module->data;
  dt_opencl_free_kernel(gd->kernel_colorize);
  free(module->data);
  module->data = NULL;
}


static void
lightness_callback (GtkWidget *slider, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_colorize_params_t *p = (dt_iop_colorize_params_t *)self->params;
  p->lightness = dt_bauhaus_slider_get(slider);
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void
source_lightness_mix_callback (GtkWidget *slider, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_colorize_params_t *p = (dt_iop_colorize_params_t *)self->params;
  p->source_lightness_mix = dt_bauhaus_slider_get(slider);
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void
hue_callback(GtkWidget *slider, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_colorize_params_t *p = (dt_iop_colorize_params_t *)self->params;
  dt_iop_colorize_gui_data_t *g = (dt_iop_colorize_gui_data_t *)self->gui_data;

  const float hue = dt_bauhaus_slider_get(g->gslider1);
  //fprintf(stderr," hue: %f, saturation: %f\n",hue,dtgtk_gradient_slider_get_value(g->gslider2));
  float saturation = 1.0f;
  float color[3];
  hsl2rgb(color, hue, saturation, 0.5f);

  dt_bauhaus_slider_set_stop(g->gslider2, 1.0f, color[0], color[1], color[2]);  // Update saturation end color

  gtk_widget_draw(GTK_WIDGET(g->gslider2),NULL);

  p->hue = hue;
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void
saturation_callback(GtkWidget *slider, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_colorize_params_t *p = (dt_iop_colorize_params_t *)self->params;

  p->saturation = dt_bauhaus_slider_get(slider);
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

#if 0 // TODO: could bind those to quad callback, if needed.
static void
colorpick_button_callback(GtkButton *button,gpointer user_data)
{
  GtkColorSelectionDialog  *csd=(GtkColorSelectionDialog  *)user_data;
  gtk_dialog_response(GTK_DIALOG(csd),(GTK_WIDGET(button)==csd->ok_button)?GTK_RESPONSE_ACCEPT:0);
}

static void
colorpick_callback (GtkDarktableButton *button, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_colorize_gui_data_t *g = (dt_iop_colorize_gui_data_t *)self->gui_data;
  if(self->dt->gui->reset) return;
  dt_iop_colorize_params_t *p = (dt_iop_colorize_params_t *)self->params;

  GtkColorSelectionDialog  *csd = GTK_COLOR_SELECTION_DIALOG(gtk_color_selection_dialog_new(_("select tone color")));
  g_signal_connect (G_OBJECT (csd->ok_button), "clicked",
                    G_CALLBACK (colorpick_button_callback), csd);
  g_signal_connect (G_OBJECT (csd->cancel_button), "clicked",
                    G_CALLBACK (colorpick_button_callback), csd);

  GtkColorSelection *cs = GTK_COLOR_SELECTION(gtk_color_selection_dialog_get_color_selection(csd));
  GdkColor c;
  float color[3],h,s,l;

  h = p->hue;
  s = p->saturation;
  l=0.5;
  hsl2rgb(color,h,s,l);

  c.red= 65535 * color[0];
  c.green= 65535 * color[1];
  c.blue= 65535 * color[2];

  gtk_color_selection_set_current_color(cs,&c);

  if(gtk_dialog_run(GTK_DIALOG(csd))==GTK_RESPONSE_ACCEPT)
  {
    gtk_color_selection_get_current_color(cs,&c);
    color[0]=c.red/65535.0;
    color[1]=c.green/65535.0;
    color[2]=c.blue/65535.0;
    rgb2hsl(color,&h,&s,&l);
    l=0.5;
    hsl2rgb(color,h,s,l);

    dtgtk_gradient_slider_set_value(g->gslider1, h);
    dtgtk_gradient_slider_set_value(g->gslider2, s);
  }

  gtk_widget_destroy(GTK_WIDGET(csd));
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}
#endif

void commit_params (struct dt_iop_module_t *self, dt_iop_params_t *p1, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_colorize_params_t *p = (dt_iop_colorize_params_t *)p1;
#ifdef HAVE_GEGL
  fprintf(stderr, "[splittoning] TODO: implement gegl version!\n");
  // pull in new params to gegl
#else
  dt_iop_colorize_data_t *d = (dt_iop_colorize_data_t *)piece->data;
  d->hue = p->hue;
  d->saturation = p->saturation;
  d->lightness = p->lightness;
  d->source_lightness_mix = p->source_lightness_mix;
#endif
}

void init_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
#ifdef HAVE_GEGL
  // create part of the gegl pipeline
  piece->data = NULL;
#else
  piece->data = malloc(sizeof(dt_iop_colorize_data_t));
  memset(piece->data,0,sizeof(dt_iop_colorize_data_t));
  self->commit_params(self, self->default_params, pipe, piece);
#endif
}

void cleanup_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
#ifdef HAVE_GEGL
  // clean up everything again.
  (void)gegl_node_remove_child(pipe->gegl, piece->input);
  // no free necessary, no data is alloc'ed
#else
  free(piece->data);
#endif
}

void gui_update(struct dt_iop_module_t *self)
{
  dt_iop_module_t *module = (dt_iop_module_t *)self;
  dt_iop_colorize_gui_data_t *g = (dt_iop_colorize_gui_data_t *)self->gui_data;
  dt_iop_colorize_params_t *p = (dt_iop_colorize_params_t *)module->params;

  dt_bauhaus_slider_set(g->gslider1, p->hue);
  dt_bauhaus_slider_set(g->gslider2, p->saturation);
  dt_bauhaus_slider_set(g->scale1, p->lightness);
  dt_bauhaus_slider_set(g->scale2, p->source_lightness_mix);

#if 0 // could update quad drawing color here
  float color[3];
  hsl2rgb(color,p->hue,p->saturation,0.5);

  GdkColor c;
  c.red=color[0]*65535.0;
  c.green=color[1]*65535.0;
  c.blue=color[2]*65535.0;

  gtk_widget_modify_fg(GTK_WIDGET(g->colorpick1),GTK_STATE_NORMAL,&c);
#endif
}

void init(dt_iop_module_t *module)
{
  module->params = malloc(sizeof(dt_iop_colorize_params_t));
  module->default_params = malloc(sizeof(dt_iop_colorize_params_t));
  module->default_enabled = 0;
  module->priority = 400; // module order created by iop_dependencies.py, do not edit!
  module->params_size = sizeof(dt_iop_colorize_params_t);
  module->gui_data = NULL;
  dt_iop_colorize_params_t tmp = (dt_iop_colorize_params_t)
  {
    0, 0.5, 50, 50
  };
  memcpy(module->params, &tmp, sizeof(dt_iop_colorize_params_t));
  memcpy(module->default_params, &tmp, sizeof(dt_iop_colorize_params_t));
}

void cleanup(dt_iop_module_t *module)
{
  free(module->gui_data);
  module->gui_data = NULL;
  free(module->params);
  module->params = NULL;
}

void gui_init(struct dt_iop_module_t *self)
{
  self->gui_data = malloc(sizeof(dt_iop_colorize_gui_data_t));
  dt_iop_colorize_gui_data_t *g = (dt_iop_colorize_gui_data_t *)self->gui_data;
  dt_iop_colorize_params_t *p = (dt_iop_colorize_params_t *)self->params;

  self->widget = gtk_vbox_new(FALSE, DT_BAUHAUS_SPACE);

  /* hue slider */
  g->gslider1 = dt_bauhaus_slider_new_with_range(self, 0.0f, 1.0f, 0.01f, 0.0f, 2);
  dt_bauhaus_slider_set_stop(g->gslider1, 0.0f, 1.0f, 0.0f, 0.0f);
  // dt_bauhaus_slider_set_format(g->gslider1, "");
  dt_bauhaus_widget_set_label(g->gslider1, _("hue"));
  dt_bauhaus_slider_set_stop(g->gslider1, 0.166f, 1.0f, 1.0f, 0.0f);
  dt_bauhaus_slider_set_stop(g->gslider1, 0.322f, 0.0f, 1.0f, 0.0f);
  dt_bauhaus_slider_set_stop(g->gslider1, 0.498f, 0.0f, 1.0f, 1.0f);
  dt_bauhaus_slider_set_stop(g->gslider1, 0.664f, 0.0f, 0.0f, 1.0f);
  dt_bauhaus_slider_set_stop(g->gslider1, 0.830f, 1.0f, 0.0f, 1.0f);
  dt_bauhaus_slider_set_stop(g->gslider1, 1.0f, 1.0f, 0.0f, 0.0f);
  g_object_set(G_OBJECT(g->gslider1), "tooltip-text", _("select the hue tone"), (char *)NULL);

  gtk_box_pack_start(GTK_BOX(self->widget), g->gslider1, TRUE, TRUE, 0);

  /* saturation slider */
  g->gslider2 = dt_bauhaus_slider_new_with_range(self, 0.0f, 1.0f, 0.01f, 0.0f, 2);
  // dt_bauhaus_slider_set_format(g->gslider2, "");
  dt_bauhaus_widget_set_label(g->gslider2, _("saturation"));
  dt_bauhaus_slider_set_stop(g->gslider2, 0.0f, 1.0f, 1.0f, 1.0f);
  dt_bauhaus_slider_set_stop(g->gslider2, 1.0f, 1.0f, 1.0f, 1.0f);
  g_object_set(G_OBJECT(g->gslider2), "tooltip-text", _("select the saturation shadow tone"), (char *)NULL);

  gtk_box_pack_start(GTK_BOX(self->widget), g->gslider2, TRUE, TRUE, 0);

  // Additional paramters
  g->scale1 = dt_bauhaus_slider_new_with_range(self, 0.0, 100.0, 0.1, p->lightness*100.0, 2);
  dt_bauhaus_slider_set_format(g->scale1, "%.2f%%");
  dt_bauhaus_widget_set_label(g->scale1, _("lightness"));
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(g->scale1), TRUE, TRUE, 0);

  g->scale2 = dt_bauhaus_slider_new_with_range(self, 0.0, 100.0, 0.1, p->source_lightness_mix, 2);
  dt_bauhaus_slider_set_format(g->scale2, "%.2f%%");
  dt_bauhaus_widget_set_label(g->scale2, _("source mix"));
  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(g->scale2), TRUE, TRUE, 0);


  g_object_set(G_OBJECT(g->scale1), "tooltip-text", _("lightness of color"), (char *)NULL);
  g_object_set(G_OBJECT(g->scale2), "tooltip-text", _("mix value of source lightness"), (char *)NULL);

  g_signal_connect (G_OBJECT (g->gslider1), "value-changed",
                    G_CALLBACK (hue_callback), self);
  g_signal_connect (G_OBJECT (g->gslider2), "value-changed",
                    G_CALLBACK (saturation_callback), self);
  g_signal_connect (G_OBJECT (g->scale1), "value-changed",
                    G_CALLBACK (lightness_callback), self);
  g_signal_connect (G_OBJECT (g->scale2), "value-changed",
                    G_CALLBACK (source_lightness_mix_callback), self);
}

void gui_cleanup(struct dt_iop_module_t *self)
{
  free(self->gui_data);
  self->gui_data = NULL;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
