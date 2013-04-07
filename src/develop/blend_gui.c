/*
    This file is part of darktable,
    copyright (c) 2009--2012 johannes hanika.
    copyright (c) 2011 Henrik Andersson.
    copyright (c) 2012 Ulrich Pegelow

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
#include "bauhaus/bauhaus.h"
#include "common/opencl.h"
#include "common/dtpthread.h"
#include "common/debug.h"
#include "control/control.h"
#include "develop/imageop.h"
#include "develop/develop.h"
#include "develop/blend.h"
#include "develop/tiling.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"
#include "gui/presets.h"
#include "dtgtk/button.h"
#include "dtgtk/icon.h"
#include "dtgtk/tristatebutton.h"
#include "dtgtk/slider.h"
#include "dtgtk/tristatebutton.h"
#include "dtgtk/gradientslider.h"
#include "dtgtk/label.h"

#include <strings.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <gmodule.h>
#include <xmmintrin.h>


#define CLAMP_RANGE(x,y,z)      (CLAMP(x,y,z))
#define LIGHTNESS               32767.0f

typedef enum _iop_gui_blendif_channel_t
{
  ch_L     = 0,
  ch_a     = 1,
  ch_b     = 2,
  ch_gray  = 0,
  ch_red   = 1,
  ch_green = 2,
  ch_blue  = 3,
  ch_max   = 4
}
_iop_gui_blendif_channel_t;



static const dt_iop_gui_blendif_colorstop_t _gradient_L[] =
{
  { 0.0f, { 0, 0, 0, 0 } },
  { 0.5f, { 0, LIGHTNESS/2, LIGHTNESS/2, LIGHTNESS/2 } },
  { 1.0f, { 0, LIGHTNESS, LIGHTNESS, LIGHTNESS } }
};

static const dt_iop_gui_blendif_colorstop_t _gradient_a[] =
{
  { 0.0f, { 0, 0, 0.34*LIGHTNESS*2, 0.27*LIGHTNESS*2 } },
  { 0.5f, { 0, LIGHTNESS, LIGHTNESS, LIGHTNESS } },
  { 1.0f, { 0, 0.53*LIGHTNESS*2, 0.08*LIGHTNESS*2, 0.28*LIGHTNESS*2 } }
};

static const dt_iop_gui_blendif_colorstop_t _gradient_b[] =
{
  { 0.0f, { 0, 0, 0.27*LIGHTNESS*2, 0.58*LIGHTNESS*2 } },
  { 0.5f, { 0, LIGHTNESS, LIGHTNESS, LIGHTNESS } },
  { 1.0f, { 0, 0.81*LIGHTNESS*2, 0.66*LIGHTNESS*2, 0 } }
};

static const dt_iop_gui_blendif_colorstop_t _gradient_gray[] =
{
  { 0.0f, { 0, 0, 0, 0 } },
  { 0.5f, { 0, LIGHTNESS/2, LIGHTNESS/2, LIGHTNESS/2 } },
  { 1.0f, { 0, LIGHTNESS, LIGHTNESS, LIGHTNESS } }
};

static const dt_iop_gui_blendif_colorstop_t _gradient_red[] =
{
  { 0.0f, { 0, 0, 0, 0 } },
  { 0.5f, { 0, LIGHTNESS/2, 0, 0 } },
  { 1.0f, { 0, LIGHTNESS, 0, 0 } }
};

static const dt_iop_gui_blendif_colorstop_t _gradient_green[] =
{
  { 0.0f, { 0, 0, 0, 0 } },
  { 0.5f, { 0, 0, LIGHTNESS/2, 0 } },
  { 1.0f, { 0, 0, LIGHTNESS, 0 } }
};

static const dt_iop_gui_blendif_colorstop_t _gradient_blue[] =
{
  { 0.0f, { 0, 0, 0, 0 } },
  { 0.5f, { 0, 0, 0, LIGHTNESS/2 } },
  { 1.0f, { 0, 0, 0, LIGHTNESS } }
};

static const dt_iop_gui_blendif_colorstop_t _gradient_chroma[] =
{
  { 0.0f, { 0, LIGHTNESS, LIGHTNESS, LIGHTNESS } },
  { 0.5f, { 0, LIGHTNESS, LIGHTNESS/2, LIGHTNESS } },
  { 1.0f, { 0, LIGHTNESS, 0, LIGHTNESS } }
};

static const dt_iop_gui_blendif_colorstop_t _gradient_hue[] =
{
  { 0.0f,   { 0, 1.00f*1.5f*LIGHTNESS, 0.68f*1.5f*LIGHTNESS, 0.78f*1.5f*LIGHTNESS } },
  { 0.166f, { 0, 0.95f*1.5f*LIGHTNESS, 0.73f*1.5f*LIGHTNESS, 0.56f*1.5f*LIGHTNESS } },
  { 0.333f, { 0, 0.71f*1.5f*LIGHTNESS, 0.81f*1.5f*LIGHTNESS, 0.55f*1.5f*LIGHTNESS } },
  { 0.500f, { 0, 0.45f*1.5f*LIGHTNESS, 0.85f*1.5f*LIGHTNESS, 0.77f*1.5f*LIGHTNESS } },
  { 0.666f, { 0, 0.49f*1.5f*LIGHTNESS, 0.82f*1.5f*LIGHTNESS, 1.00f*1.5f*LIGHTNESS } },
  { 0.833f, { 0, 0.82f*1.5f*LIGHTNESS, 0.74f*1.5f*LIGHTNESS, 1.00f*1.5f*LIGHTNESS } },
  { 1.0f,   { 0, 1.00f*1.5f*LIGHTNESS, 0.68f*1.5f*LIGHTNESS, 0.78f*1.5f*LIGHTNESS } }
};

static const dt_iop_gui_blendif_colorstop_t _gradient_HUE[] =
{
  { 0.0f,   { 0, LIGHTNESS, 0, 0 } },
  { 0.166f, { 0, LIGHTNESS, LIGHTNESS, 0 } },
  { 0.332f, { 0, 0, LIGHTNESS, 0 } },
  { 0.498f, { 0, 0, LIGHTNESS, LIGHTNESS } },
  { 0.664f, { 0, 0, 0, LIGHTNESS } },
  { 0.830f, { 0, LIGHTNESS, 0, LIGHTNESS } },
  { 1.0f,   { 0, LIGHTNESS, 0, 0 } }
};


static inline void
_RGB_2_HSL(const float *RGB, float *HSL)
{
  float H, S, L;

  float R = RGB[0];
  float G = RGB[1];
  float B = RGB[2];

  float var_Min = fminf(R, fminf(G, B));
  float var_Max = fmaxf(R, fmaxf(G, B));
  float del_Max = var_Max - var_Min;

  L = (var_Max + var_Min) / 2.0f;

  if (del_Max == 0.0f)
  {
    H = 0.0f;
    S = 0.0f;
  }
  else
  {
    if (L < 0.5f) S = del_Max / (var_Max + var_Min);
    else          S = del_Max / (2.0f - var_Max - var_Min);

    float del_R = (((var_Max - R) / 6.0f) + (del_Max / 2.0f)) / del_Max;
    float del_G = (((var_Max - G) / 6.0f) + (del_Max / 2.0f)) / del_Max;
    float del_B = (((var_Max - B) / 6.0f) + (del_Max / 2.0f)) / del_Max;

    if      (R == var_Max) H = del_B - del_G;
    else if (G == var_Max) H = (1.0f / 3.0f) + del_R - del_B;
    else if (B == var_Max) H = (2.0f / 3.0f) + del_G - del_R;
    else H = 0.0f;   // make GCC happy

    if (H < 0.0f) H += 1.0f;
    if (H > 1.0f) H -= 1.0f;
  }

  HSL[0] = H;
  HSL[1] = S;
  HSL[2] = L;
}


static inline void
_Lab_2_LCH(const float *Lab, float *LCH)
{
  float var_H = atan2f(Lab[2], Lab[1]);

  if (var_H > 0.0f) var_H = var_H / (2.0f*M_PI);
  else              var_H = 1.0f - fabs(var_H) / (2.0f*M_PI);

  LCH[0] = Lab[0];
  LCH[1] = sqrtf(Lab[1]*Lab[1] + Lab[2]*Lab[2]);
  LCH[2] = var_H;
}


static void
_blendif_scale(dt_iop_colorspace_type_t cst, const float *in, float *out)
{
  float temp[4];

  switch(cst)
  {
    case iop_cs_Lab:
      _Lab_2_LCH(in, temp);
      out[0] = CLAMP_RANGE(in[0] / 100.0f, 0.0f, 1.0f);
      out[1] = CLAMP_RANGE((in[1] + 128.0f)/256.0f, 0.0f, 1.0f);
      out[2] = CLAMP_RANGE((in[2] + 128.0f)/256.0f, 0.0f, 1.0f);
      out[3] = CLAMP_RANGE(temp[1] / (128.0f * sqrtf(2.0f)), 0.0f, 1.0f);
      out[4] = CLAMP_RANGE(temp[2], 0.0f, 1.0f);
      out[5] = out[6] = out[7] = -1;
      break;
    case iop_cs_rgb:
      _RGB_2_HSL(in, temp);
      out[0] = CLAMP_RANGE(0.3f*in[0] + 0.59f*in[1] + 0.11f*in[2], 0.0f, 1.0f);
      out[1] = CLAMP_RANGE(in[0], 0.0f, 1.0f);
      out[2] = CLAMP_RANGE(in[1], 0.0f, 1.0f);
      out[3] = CLAMP_RANGE(in[2], 0.0f, 1.0f);
      out[4] = CLAMP_RANGE(temp[0], 0.0f, 1.0f);
      out[5] = CLAMP_RANGE(temp[1], 0.0f, 1.0f);
      out[6] = CLAMP_RANGE(temp[2], 0.0f, 1.0f);
      out[7] = -1;
      break;
    default:
      out[0] = out[1] = out[2] = out[3] = out[4] = out[5] = out[6] = out[7] = -1.0f;
  }
}

static void
_blendif_cook(dt_iop_colorspace_type_t cst, const float *in, float *out)
{
  float temp[4];

  switch(cst)
  {
    case iop_cs_Lab:
      _Lab_2_LCH(in, temp);
      out[0] = in[0];
      out[1] = in[1];
      out[2] = in[2];
      out[3] = temp[1] / (128.0f * sqrtf(2.0f)) * 100.0f;
      out[4] = temp[2]*360.0f;
      out[5] = out[6] = out[7] = -1;
      break;
    case iop_cs_rgb:
      _RGB_2_HSL(in, temp);
      out[0] = (0.3f*in[0] + 0.59f*in[1] + 0.11f*in[2])*255.0f;
      out[1] = in[0]*255.0f;
      out[2] = in[1]*255.0f;
      out[3] = in[2]*255.0f;
      out[4] = temp[0]*360.0f;
      out[5] = temp[1]*100.0f;
      out[6] = temp[2]*100.0f;
      out[7] = -1;
      break;
    default:
      out[0] = out[1] = out[2] = out[3] = out[4] = out[5] = out[6] = out[7] = -1.0f;
  }
}



static void
_blendif_scale_print_L(float value, char *string, int n)
{
  snprintf(string, n, "%-4.0f", value*100.0f);
}

static void
_blendif_scale_print_ab(float value, char *string, int n)
{
  snprintf(string, n, "%-4.0f", value*256.0f-128.0f);
}

static void
_blendif_scale_print_rgb(float value, char *string, int n)
{
  snprintf(string, n, "%-4.0f", value*255.0f);
}

static void
_blendif_scale_print_hue(float value, char *string, int n)
{
  snprintf(string, n, "%-4.0f", value*360.0f);
}

static void
_blendif_scale_print_default(float value, char *string, int n)
{
  snprintf(string, n, "%-4.0f", value*100.0f);
}

static void
_blendop_mode_callback (GtkWidget *combo, dt_iop_gui_blend_data_t *data)
{
  data->module->blend_params->mode = data->modes[dt_bauhaus_combobox_get(data->blend_modes_combo)].mode;
  if(data->module->blend_params->mode != DEVELOP_BLEND_DISABLED)
  {
    gtk_widget_show(data->opacity_slider);
    if(data->blendif_support)
    {
      gtk_widget_show(data->blendif_enable);
      if(dt_bauhaus_combobox_get(data->blendif_enable))
        gtk_widget_show(GTK_WIDGET(data->blendif_box));
    }
  }
  else
  {
    gtk_widget_hide(data->opacity_slider);
    if(data->blendif_support)
    {
      /* switch off color picker if it was requested by blendif */
      if(data->module->request_color_pick < 0)
      {
        data->module->request_color_pick = 0;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->colorpicker), 0);
      }

      data->module->request_mask_display = 0;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->showmask), 0);
      data->module->suppress_mask = 0;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->suppress), 0);


      gtk_widget_hide(GTK_WIDGET(data->blendif_enable));
      gtk_widget_hide(GTK_WIDGET(data->blendif_box));
    }
  }
  dt_dev_add_history_item(darktable.develop, data->module, TRUE);
}

static void
_blendop_opacity_callback (GtkWidget *slider, dt_iop_gui_blend_data_t *data)
{
  data->module->blend_params->opacity = dt_bauhaus_slider_get(slider);
  dt_dev_add_history_item(darktable.develop, data->module, TRUE);
}

static void
_blendop_blendif_radius_callback (GtkWidget *slider, dt_iop_gui_blend_data_t *data)
{
  data->module->blend_params->radius = dt_bauhaus_slider_get(slider);
  dt_dev_add_history_item(darktable.develop, data->module, TRUE);
}

static void
_blendop_blendif_callback(GtkWidget *b, dt_iop_gui_blend_data_t *data)
{
  if(dt_bauhaus_combobox_get(b))
  {
    data->module->blend_params->blendif |= (1<<DEVELOP_BLENDIF_active);
    gtk_widget_show(GTK_WIDGET(data->blendif_box));
  }
  else
  {
    /* switch off color picker if it was requested by blendif */
    if(data->module->request_color_pick < 0)
    {
      data->module->request_color_pick = 0;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->colorpicker), 0);
    }

    data->module->request_mask_display = 0;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->showmask), 0);
    data->module->suppress_mask = 0;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->suppress), 0);

    gtk_widget_hide(GTK_WIDGET(data->blendif_box));
    data->module->blend_params->blendif &= ~(1<<DEVELOP_BLENDIF_active);
  }
  dt_dev_add_history_item(darktable.develop, data->module, TRUE);
}


static void
_blendop_blendif_upper_callback (GtkDarktableGradientSlider *slider, dt_iop_gui_blend_data_t *data)
{
  if(darktable.gui->reset) return;
  dt_develop_blend_params_t *bp = data->module->blend_params;

  int tab = data->tab;
  int ch = data->channels[tab][1];

  float *parameters = &(bp->blendif_parameters[4*ch]);

  for(int k=0; k < 4; k++)
    parameters[k] = dtgtk_gradient_slider_multivalue_get_value(slider, k);

  for(int k=0; k < 4 ; k++)
  {
    char text[256];
    (data->scale_print[tab])(parameters[k], text, 256);
    gtk_label_set_text(data->upper_label[k], text);
  }

  /** de-activate processing of this channel if maximum span is selected */
  if(parameters[1] == 0.0f && parameters[2] == 1.0f)
    bp->blendif &= ~(1<<ch);
  else
    bp->blendif |= (1<<ch);

  dt_dev_add_history_item(darktable.develop, data->module, TRUE);
}


static void
_blendop_blendif_lower_callback (GtkDarktableGradientSlider *slider, dt_iop_gui_blend_data_t *data)
{
  if(darktable.gui->reset) return;
  dt_develop_blend_params_t *bp = data->module->blend_params;

  int tab = data->tab;
  int ch = data->channels[tab][0];

  float *parameters = &(bp->blendif_parameters[4*ch]);

  for(int k=0; k < 4; k++)
    parameters[k] = dtgtk_gradient_slider_multivalue_get_value(slider, k);

  for(int k=0; k < 4 ; k++)
  {
    char text[256];
    (data->scale_print[tab])(parameters[k], text, 256);
    gtk_label_set_text(data->lower_label[k], text);
  }

  /** de-activate processing of this channel if maximum span is selected */
  if(parameters[1] == 0.0f && parameters[2] == 1.0f)
    bp->blendif &= ~(1<<ch);
  else
    bp->blendif |= (1<<ch);

  dt_dev_add_history_item(darktable.develop, data->module, TRUE);
}


static void
_blendop_blendif_polarity_callback(GtkToggleButton *togglebutton, dt_iop_gui_blend_data_t *data)
{
  int active = gtk_toggle_button_get_active(togglebutton);
  if(darktable.gui->reset) return;
  dt_develop_blend_params_t *bp = data->module->blend_params;

  int tab = data->tab;
  int ch = GTK_WIDGET(togglebutton) == data->lower_polarity ? data->channels[tab][0] : data->channels[tab][1];
  GtkDarktableGradientSlider *slider = GTK_WIDGET(togglebutton) == data->lower_polarity ? data->lower_slider : data->upper_slider;

  if(!active)
    bp->blendif |= (1<<(ch+16));
  else
    bp->blendif &= ~(1<<(ch+16));


  dtgtk_gradient_slider_multivalue_set_marker(slider, active ? GRADIENT_SLIDER_MARKER_LOWER_OPEN_BIG : GRADIENT_SLIDER_MARKER_UPPER_OPEN_BIG, 0);
  dtgtk_gradient_slider_multivalue_set_marker(slider, active ? GRADIENT_SLIDER_MARKER_UPPER_FILLED_BIG : GRADIENT_SLIDER_MARKER_LOWER_FILLED_BIG, 1);
  dtgtk_gradient_slider_multivalue_set_marker(slider, active ? GRADIENT_SLIDER_MARKER_UPPER_FILLED_BIG : GRADIENT_SLIDER_MARKER_LOWER_FILLED_BIG, 2);
  dtgtk_gradient_slider_multivalue_set_marker(slider, active ? GRADIENT_SLIDER_MARKER_LOWER_OPEN_BIG : GRADIENT_SLIDER_MARKER_UPPER_OPEN_BIG, 3);


  dt_dev_add_history_item(darktable.develop, data->module, TRUE);
}


static void
_blendop_blendif_tab_switch(GtkNotebook *notebook, GtkNotebookPage *notebook_page, guint page_num,dt_iop_gui_blend_data_t *data)
{
  data->tab = page_num;
  dt_iop_gui_update_blendif(data->module);
}


static void
_blendop_blendif_pick_toggled(GtkToggleButton *togglebutton, dt_iop_module_t *module)
{
  if(darktable.gui->reset) return;

  /* if module itself already requested color pick (positive value in request_color_pick),
     don't tamper with it. A module color picker takes precedence. */
  if(module->request_color_pick > 0)
  {
    gtk_toggle_button_set_active(togglebutton, 0);
    return;
  }

  /* we put a negative value into request_color_pick to later see if color picker was
     requested by blendif. A module color picker may overwrite this. This is fine, blendif
     will use the color picker data, but not deactivate it. */
  module->request_color_pick = (gtk_toggle_button_get_active(togglebutton) ? -1 : 0);

  /* set the area sample size */
  if (module->request_color_pick)
  {
    dt_lib_colorpicker_set_point(darktable.lib, 0.5, 0.5);
    dt_dev_reprocess_all(module->dev);
  }
  else
    dt_control_queue_redraw();

  if(module->off) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(module->off), 1);
  dt_iop_request_focus(module);
}

static void
_blendop_blendif_showmask_toggled(GtkToggleButton *togglebutton, dt_iop_module_t *module)
{
  module->request_mask_display = gtk_toggle_button_get_active(togglebutton);
  if(darktable.gui->reset) return;

  if(module->off) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(module->off), 1);
  dt_iop_request_focus(module);

  dt_dev_reprocess_all(module->dev);
}

static void
_blendop_blendif_suppress_toggled(GtkToggleButton *togglebutton, dt_iop_module_t *module)
{
  module->suppress_mask = gtk_toggle_button_get_active(togglebutton);
  if(darktable.gui->reset) return;

  if(module->off) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(module->off), 1);
  dt_iop_request_focus(module);

  dt_dev_reprocess_all(module->dev);
}

static void
_blendop_blendif_reset(GtkButton *button, dt_iop_module_t *module)
{
  module->blend_params->blendif = module->default_blendop_params->blendif | (1<<DEVELOP_BLENDIF_active);
  module->blend_params->radius = module->default_blendop_params->radius;
  memcpy(module->blend_params->blendif_parameters, module->default_blendop_params->blendif_parameters, 4*DEVELOP_BLENDIF_SIZE*sizeof(float));

  dt_iop_gui_update_blendif(module);
  dt_dev_add_history_item(darktable.develop, module, TRUE);
}


static gboolean
_blendop_blendif_expose(GtkWidget *widget, GdkEventExpose *event, dt_iop_module_t *module)
{
  if(darktable.gui->reset) return FALSE;

  dt_iop_gui_blend_data_t *data = module->blend_data;

  float picker_mean[8], picker_min[8], picker_max[8];
  float cooked[8];
  float *raw_mean, *raw_min, *raw_max;
  char text[256];
  GtkLabel *label;

  if(widget == GTK_WIDGET(data->lower_slider))
  {
    raw_mean = module->picked_color;
    raw_min = module->picked_color_min;
    raw_max = module->picked_color_max;
    label = data->lower_picker_label;
  }
  else
  {
    raw_mean = module->picked_output_color;
    raw_min = module->picked_output_color_min;
    raw_max = module->picked_output_color_max;
    label = data->upper_picker_label;
  }

  darktable.gui->reset = 1;
  if(module->request_color_pick)
  {
    _blendif_scale(data->csp, raw_mean, picker_mean);
    _blendif_scale(data->csp, raw_min, picker_min);
    _blendif_scale(data->csp, raw_max, picker_max);
    _blendif_cook(data->csp, raw_mean, cooked);


    if(data->channels[data->tab][0] >= 8) // min and max make no sense for HSL and LCh
      picker_min[data->tab] = picker_max[data->tab] = picker_mean[data->tab];

    snprintf(text, 256, "(%.1f)", cooked[data->tab]);

    dtgtk_gradient_slider_multivalue_set_picker_meanminmax(DTGTK_GRADIENT_SLIDER(widget), picker_mean[data->tab], picker_min[data->tab], picker_max[data->tab]);
    gtk_label_set_text(label, text);
  }
  else
  {
    dtgtk_gradient_slider_multivalue_set_picker(DTGTK_GRADIENT_SLIDER(widget), -1.0f);
    gtk_label_set_text(label, "");
  }

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->colorpicker), (module->request_color_pick < 0 ? 1 : 0));

  darktable.gui->reset = 0;

  return FALSE;
}


void
dt_iop_gui_update_blendif(dt_iop_module_t *module)
{
  dt_iop_gui_blend_data_t *data = module->blend_data;
  dt_develop_blend_params_t *bp = module->blend_params;
  dt_develop_blend_params_t *dp = module->default_blendop_params;

  if (!data || !data->blendif_support || !data->blendif_inited) return;

  int tab = data->tab;
  int in_ch = data->channels[tab][0];
  int out_ch = data->channels[tab][1];

  float *iparameters = &(bp->blendif_parameters[4*in_ch]);
  float *oparameters = &(bp->blendif_parameters[4*out_ch]);
  float *idefaults = &(dp->blendif_parameters[4*in_ch]);
  float *odefaults = &(dp->blendif_parameters[4*out_ch]);

  int ipolarity = !(bp->blendif & (1<<(in_ch+16)));
  int opolarity = !(bp->blendif & (1<<(out_ch+16)));
  char text[256];

  int reset = darktable.gui->reset;
  darktable.gui->reset = 1;

  dt_bauhaus_combobox_set(data->blendif_enable, (module->blend_params->blendif & (1<<DEVELOP_BLENDIF_active)) ? 1 : 0);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->lower_polarity), ipolarity);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->upper_polarity), opolarity);


  dtgtk_gradient_slider_multivalue_set_marker(data->lower_slider, ipolarity ? GRADIENT_SLIDER_MARKER_LOWER_OPEN_BIG : GRADIENT_SLIDER_MARKER_UPPER_OPEN_BIG, 0);
  dtgtk_gradient_slider_multivalue_set_marker(data->lower_slider, ipolarity ? GRADIENT_SLIDER_MARKER_UPPER_FILLED_BIG : GRADIENT_SLIDER_MARKER_LOWER_FILLED_BIG, 1);
  dtgtk_gradient_slider_multivalue_set_marker(data->lower_slider, ipolarity ? GRADIENT_SLIDER_MARKER_UPPER_FILLED_BIG : GRADIENT_SLIDER_MARKER_LOWER_FILLED_BIG, 2);
  dtgtk_gradient_slider_multivalue_set_marker(data->lower_slider, ipolarity ? GRADIENT_SLIDER_MARKER_LOWER_OPEN_BIG : GRADIENT_SLIDER_MARKER_UPPER_OPEN_BIG, 3);

  dtgtk_gradient_slider_multivalue_set_marker(data->upper_slider, opolarity ? GRADIENT_SLIDER_MARKER_LOWER_OPEN_BIG : GRADIENT_SLIDER_MARKER_UPPER_OPEN_BIG, 0);
  dtgtk_gradient_slider_multivalue_set_marker(data->upper_slider, opolarity ? GRADIENT_SLIDER_MARKER_UPPER_FILLED_BIG : GRADIENT_SLIDER_MARKER_LOWER_FILLED_BIG, 1);
  dtgtk_gradient_slider_multivalue_set_marker(data->upper_slider, opolarity ? GRADIENT_SLIDER_MARKER_UPPER_FILLED_BIG : GRADIENT_SLIDER_MARKER_LOWER_FILLED_BIG, 2);
  dtgtk_gradient_slider_multivalue_set_marker(data->upper_slider, opolarity ? GRADIENT_SLIDER_MARKER_LOWER_OPEN_BIG : GRADIENT_SLIDER_MARKER_UPPER_OPEN_BIG, 3);



  for(int k=0; k < 4; k++)
  {
    dtgtk_gradient_slider_multivalue_set_value(data->lower_slider, iparameters[k], k);
    dtgtk_gradient_slider_multivalue_set_value(data->upper_slider, oparameters[k], k);
    dtgtk_gradient_slider_multivalue_set_resetvalue(data->lower_slider, idefaults[k], k);
    dtgtk_gradient_slider_multivalue_set_resetvalue(data->upper_slider, odefaults[k], k);
  }

  for(int k=0; k < 4 ; k++)
  {
    (data->scale_print[tab])(iparameters[k], text, 256);
    gtk_label_set_text(data->lower_label[k], text);
    (data->scale_print[tab])(oparameters[k], text, 256);
    gtk_label_set_text(data->upper_label[k], text);
  }

  dtgtk_gradient_slider_multivalue_clear_stops(data->lower_slider);
  dtgtk_gradient_slider_multivalue_clear_stops(data->upper_slider);

  for(int k=0; k < data->numberstops[tab]; k++)
  {
    dtgtk_gradient_slider_multivalue_set_stop(data->lower_slider, (data->colorstops[tab])[k].stoppoint,
        (data->colorstops[tab])[k].color);
    dtgtk_gradient_slider_multivalue_set_stop(data->upper_slider, (data->colorstops[tab])[k].stoppoint,
        (data->colorstops[tab])[k].color);
  }

  dtgtk_gradient_slider_multivalue_set_increment(data->lower_slider, data->increments[tab]);
  dtgtk_gradient_slider_multivalue_set_increment(data->upper_slider, data->increments[tab]);

  dt_bauhaus_slider_set(data->radius_slider, bp->radius);

  darktable.gui->reset = reset;
}


/** get the sequence number (in combo box) of a blend mode */
int dt_iop_gui_blending_mode_seq(dt_iop_gui_blend_data_t *bd, int mode)
{
  int result = 0;
  for (int k=0; k < bd->number_modes; k++)
    if (bd->modes[k].mode == mode)
    {
      result = k;
      break;
    }

  return result;
}


void dt_iop_gui_init_blendif(GtkVBox *blendw, dt_iop_module_t *module)
{
  dt_iop_gui_blend_data_t *bd = (dt_iop_gui_blend_data_t*)module->blend_data;

  /* create and add blendif support if module supports it */
  if (bd->blendif_support)
  {
    char *Lab_labels[] = { "  L  ", "  a  ", "  b  ", " C ", " h " };
    char *Lab_tooltips[] = { _("sliders for L channel"), _("sliders for a channel"), _("sliders for b channel"), _("sliders for chroma channel (of LCh)"), _("sliders for hue channel (of LCh)") };
    char *rgb_labels[] = { _(" g "), _(" R "), _(" G "), _(" B "), _(" H "), _(" S "), _(" L ") };
    char *rgb_tooltips[] = { _("sliders for gray value"), _("sliders for red channel"), _("sliders for green channel"), _("sliders for blue channel"),
                             _("sliders for hue channel (of HSL)"), _("sliders for chroma channel (of HSL)"), _("sliders for value channel (of HSL)")
                           };

    char *ttinput = _("adjustment based on input received by this module:\n* range defined by upper markers: blend fully\n* range defined by lower markers: do not blend at all\n* range between adjacent upper/lower markers: blend gradually");

    char *ttoutput = _("adjustment based on unblended output of this module:\n* range defined by upper markers: blend fully\n* range defined by lower markers: do not blend at all\n* range between adjacent upper/lower markers: blend gradually");

    bd->tab = 0;

    int maxchannels = 0;
    char **labels = NULL;
    char **tooltips = NULL;

    switch(bd->csp)
    {
      case iop_cs_Lab:
        maxchannels = 5;
        labels = Lab_labels;
        tooltips = Lab_tooltips;
        bd->scale_print[0] = _blendif_scale_print_L;
        bd->scale_print[1] = _blendif_scale_print_ab;
        bd->scale_print[2] = _blendif_scale_print_ab;
        bd->scale_print[3] = _blendif_scale_print_default;
        bd->scale_print[4] = _blendif_scale_print_hue;
        bd->increments[0] = 1.0f/100.0f;
        bd->increments[1] = 1.0f/256.0f;
        bd->increments[2] = 1.0f/256.0f;
        bd->increments[3] = 1.0f/100.0f;
        bd->increments[4] = 1.0f/360.0f;
        bd->channels[0][0] = DEVELOP_BLENDIF_L_in;
        bd->channels[0][1] = DEVELOP_BLENDIF_L_out;
        bd->channels[1][0] = DEVELOP_BLENDIF_A_in;
        bd->channels[1][1] = DEVELOP_BLENDIF_A_out;
        bd->channels[2][0] = DEVELOP_BLENDIF_B_in;
        bd->channels[2][1] = DEVELOP_BLENDIF_B_out;
        bd->channels[3][0] = DEVELOP_BLENDIF_C_in;
        bd->channels[3][1] = DEVELOP_BLENDIF_C_out;
        bd->channels[4][0] = DEVELOP_BLENDIF_h_in;
        bd->channels[4][1] = DEVELOP_BLENDIF_h_out;
        bd->colorstops[0] = _gradient_L;
        bd->numberstops[0] = sizeof(_gradient_L)/sizeof(dt_iop_gui_blendif_colorstop_t);
        bd->colorstops[1] = _gradient_a;
        bd->numberstops[1] = sizeof(_gradient_a)/sizeof(dt_iop_gui_blendif_colorstop_t);
        bd->colorstops[2] = _gradient_b;
        bd->numberstops[2] = sizeof(_gradient_b)/sizeof(dt_iop_gui_blendif_colorstop_t);
        bd->colorstops[3] = _gradient_chroma;
        bd->numberstops[3] = sizeof(_gradient_chroma)/sizeof(dt_iop_gui_blendif_colorstop_t);
        bd->colorstops[4] = _gradient_hue;
        bd->numberstops[4] = sizeof(_gradient_hue)/sizeof(dt_iop_gui_blendif_colorstop_t);
        break;
      case iop_cs_rgb:
        maxchannels = 7;
        labels = rgb_labels;
        tooltips = rgb_tooltips;
        bd->scale_print[0] = _blendif_scale_print_rgb;
        bd->scale_print[1] = _blendif_scale_print_rgb;
        bd->scale_print[2] = _blendif_scale_print_rgb;
        bd->scale_print[3] = _blendif_scale_print_rgb;
        bd->scale_print[4] = _blendif_scale_print_hue;
        bd->scale_print[5] = _blendif_scale_print_default;
        bd->scale_print[6] = _blendif_scale_print_L;
        bd->increments[0] = 1.0f/255.0f;
        bd->increments[1] = 1.0f/255.0f;
        bd->increments[2] = 1.0f/255.0f;
        bd->increments[3] = 1.0f/255.0f;
        bd->increments[4] = 1.0f/360.0f;
        bd->increments[5] = 1.0f/100.0f;
        bd->increments[6] = 1.0f/100.0f;
        bd->channels[0][0] = DEVELOP_BLENDIF_GRAY_in;
        bd->channels[0][1] = DEVELOP_BLENDIF_GRAY_out;
        bd->channels[1][0] = DEVELOP_BLENDIF_RED_in;
        bd->channels[1][1] = DEVELOP_BLENDIF_RED_out;
        bd->channels[2][0] = DEVELOP_BLENDIF_GREEN_in;
        bd->channels[2][1] = DEVELOP_BLENDIF_GREEN_out;
        bd->channels[3][0] = DEVELOP_BLENDIF_BLUE_in;
        bd->channels[3][1] = DEVELOP_BLENDIF_BLUE_out;
        bd->channels[4][0] = DEVELOP_BLENDIF_H_in;
        bd->channels[4][1] = DEVELOP_BLENDIF_H_out;
        bd->channels[5][0] = DEVELOP_BLENDIF_S_in;
        bd->channels[5][1] = DEVELOP_BLENDIF_S_out;
        bd->channels[6][0] = DEVELOP_BLENDIF_l_in;
        bd->channels[6][1] = DEVELOP_BLENDIF_l_out;
        bd->colorstops[0] = _gradient_gray;
        bd->numberstops[0] = sizeof(_gradient_gray)/sizeof(dt_iop_gui_blendif_colorstop_t);
        bd->colorstops[1] = _gradient_red;
        bd->numberstops[1] = sizeof(_gradient_red)/sizeof(dt_iop_gui_blendif_colorstop_t);
        bd->colorstops[2] = _gradient_green;
        bd->numberstops[2] = sizeof(_gradient_green)/sizeof(dt_iop_gui_blendif_colorstop_t);
        bd->colorstops[3] = _gradient_blue;
        bd->numberstops[3] = sizeof(_gradient_blue)/sizeof(dt_iop_gui_blendif_colorstop_t);
        bd->colorstops[4] = _gradient_HUE;
        bd->numberstops[4] = sizeof(_gradient_HUE)/sizeof(dt_iop_gui_blendif_colorstop_t);
        bd->colorstops[5] = _gradient_chroma;
        bd->numberstops[5] = sizeof(_gradient_chroma)/sizeof(dt_iop_gui_blendif_colorstop_t);
        bd->colorstops[6] = _gradient_gray;
        bd->numberstops[6] = sizeof(_gradient_gray)/sizeof(dt_iop_gui_blendif_colorstop_t);
        break;
      default:
        assert(FALSE);		// blendif not supported for RAW, which is already catched upstream; we should not get here
    }


    bd->blendif_box = GTK_VBOX(gtk_vbox_new(FALSE,DT_GUI_IOP_MODULE_CONTROL_SPACING));
    GtkWidget *uplabel = gtk_hbox_new(FALSE,0);
    GtkWidget *lowlabel = gtk_hbox_new(FALSE,0);
    GtkWidget *upslider = gtk_hbox_new(FALSE,0);
    GtkWidget *lowslider = gtk_hbox_new(FALSE,0);
    GtkWidget *notebook = gtk_vbox_new(FALSE,0);
    GtkWidget *header = gtk_hbox_new(FALSE, 0);

    bd->blendif_enable = dt_bauhaus_combobox_new(module);
    dt_bauhaus_widget_set_label(bd->blendif_enable, _("blend"));
    dt_bauhaus_combobox_add(bd->blendif_enable, _("uniformly"));
    dt_bauhaus_combobox_add(bd->blendif_enable, _("only, if.."));
    dt_bauhaus_combobox_set(bd->blendif_enable, 0);


    bd->channel_tabs = GTK_NOTEBOOK(gtk_notebook_new());

    for(int ch=0; ch<maxchannels; ch++)
    {
      gtk_notebook_append_page(GTK_NOTEBOOK(bd->channel_tabs), GTK_WIDGET(gtk_hbox_new(FALSE,0)), gtk_label_new(labels[ch]));
      g_object_set(G_OBJECT(gtk_notebook_get_tab_label(bd->channel_tabs, gtk_notebook_get_nth_page(bd->channel_tabs, -1))), "tooltip-text", tooltips[ch], NULL);
    }

    gtk_widget_show_all(GTK_WIDGET(gtk_notebook_get_nth_page(bd->channel_tabs, bd->tab)));
    gtk_notebook_set_current_page(GTK_NOTEBOOK(bd->channel_tabs), bd->tab);
    g_object_set(G_OBJECT(bd->channel_tabs), "homogeneous", TRUE, (char *)NULL);
    gtk_notebook_set_scrollable(bd->channel_tabs, TRUE);

    gtk_box_pack_start(GTK_BOX(notebook), GTK_WIDGET(bd->channel_tabs), FALSE, FALSE, 0);

    bd->colorpicker = dtgtk_togglebutton_new(dtgtk_cairo_paint_colorpicker, CPF_STYLE_FLAT);
    g_object_set(G_OBJECT(bd->colorpicker), "tooltip-text", _("pick gui color from image"), (char *)NULL);

    bd->showmask = dtgtk_togglebutton_new(dtgtk_cairo_paint_showmask, CPF_STYLE_FLAT);
    g_object_set(G_OBJECT(bd->showmask), "tooltip-text", _("display mask"), (char *)NULL);

    GtkWidget *res = dtgtk_button_new(dtgtk_cairo_paint_reset, CPF_STYLE_FLAT);
    g_object_set(G_OBJECT(res), "tooltip-text", _("reset blend mask settings"), (char *)NULL);

    bd->suppress = dtgtk_togglebutton_new(dtgtk_cairo_paint_eye_toggle, CPF_STYLE_FLAT);
    g_object_set(G_OBJECT(bd->suppress), "tooltip-text", _("temporarily switch off blend mask. only for module in focus."), (char *)NULL);

    gtk_box_pack_start(GTK_BOX(header), GTK_WIDGET(notebook), TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(header), GTK_WIDGET(res), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(header), GTK_WIDGET(bd->colorpicker), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(header), GTK_WIDGET(bd->showmask), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(header), GTK_WIDGET(bd->suppress), FALSE, FALSE, 0);

    bd->lower_slider = DTGTK_GRADIENT_SLIDER_MULTIVALUE(dtgtk_gradient_slider_multivalue_new(4));
    bd->upper_slider = DTGTK_GRADIENT_SLIDER_MULTIVALUE(dtgtk_gradient_slider_multivalue_new(4));

    bd->lower_polarity = dtgtk_togglebutton_new(dtgtk_cairo_paint_plusminus, CPF_STYLE_FLAT|CPF_DO_NOT_USE_BORDER);
    g_object_set(G_OBJECT(bd->lower_polarity), "tooltip-text", _("toggle polarity. best seen by enabling 'display mask'"), (char *)NULL);

    bd->upper_polarity = dtgtk_togglebutton_new(dtgtk_cairo_paint_plusminus, CPF_STYLE_FLAT|CPF_DO_NOT_USE_BORDER);
    g_object_set(G_OBJECT(bd->upper_polarity), "tooltip-text", _("toggle polarity. best seen by enabling 'display mask'"), (char *)NULL);

    gtk_box_pack_start(GTK_BOX(upslider), GTK_WIDGET(bd->upper_slider), TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(upslider), GTK_WIDGET(bd->upper_polarity), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(lowslider), GTK_WIDGET(bd->lower_slider), TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(lowslider), GTK_WIDGET(bd->lower_polarity), FALSE, FALSE, 0);


    GtkWidget *output = gtk_label_new(_("output"));
    bd->upper_picker_label = GTK_LABEL(gtk_label_new(""));
    gtk_box_pack_start(GTK_BOX(uplabel), GTK_WIDGET(output), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(uplabel), GTK_WIDGET(bd->upper_picker_label), TRUE, TRUE, 0);
    for(int k=0; k < 4 ; k++)
    {
      bd->upper_label[k] = GTK_LABEL(gtk_label_new(NULL));
      gtk_label_set_width_chars(bd->upper_label[k], 5);
      gtk_box_pack_start(GTK_BOX(uplabel), GTK_WIDGET(bd->upper_label[k]), FALSE, FALSE, 0);
    }

    GtkWidget *input = gtk_label_new(_("input"));
    bd->lower_picker_label = GTK_LABEL(gtk_label_new(""));
    gtk_box_pack_start(GTK_BOX(lowlabel), GTK_WIDGET(input), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(lowlabel), GTK_WIDGET(bd->lower_picker_label), TRUE, TRUE, 0);
    for(int k=0; k < 4 ; k++)
    {
      bd->lower_label[k] = GTK_LABEL(gtk_label_new(NULL));
      gtk_label_set_width_chars(bd->lower_label[k], 5);
      gtk_box_pack_start(GTK_BOX(lowlabel), GTK_WIDGET(bd->lower_label[k]), FALSE, FALSE, 0);
    }

    bd->radius_slider = dt_bauhaus_slider_new_with_range(module, 0.0, 50.0, 0.1, 0.0, 1);
    dt_bauhaus_widget_set_label(bd->radius_slider, _("mask blur"));
    dt_bauhaus_slider_set_format(bd->radius_slider, "%.1f");

    gtk_object_set(GTK_OBJECT(bd->blendif_enable), "tooltip-text", _("enable conditional blending"), (char *)NULL);
    gtk_object_set(GTK_OBJECT(bd->lower_slider), "tooltip-text", _("double click to reset"), (char *)NULL);
    gtk_object_set(GTK_OBJECT(bd->upper_slider), "tooltip-text", _("double click to reset"), (char *)NULL);
    gtk_object_set(GTK_OBJECT(output), "tooltip-text", ttoutput, (char *)NULL);
    gtk_object_set(GTK_OBJECT(input), "tooltip-text", ttinput, (char *)NULL);
    gtk_object_set(GTK_OBJECT(bd->radius_slider), "tooltip-text", _("radius for gaussian blur of blend mask"), (char *)NULL);


    g_signal_connect (G_OBJECT (bd->lower_slider), "expose-event",
                      G_CALLBACK (_blendop_blendif_expose), module);

    g_signal_connect (G_OBJECT (bd->upper_slider), "expose-event",
                      G_CALLBACK (_blendop_blendif_expose), module);

    g_signal_connect (G_OBJECT (bd->blendif_enable), "value-changed",
                      G_CALLBACK (_blendop_blendif_callback), bd);

    g_signal_connect(G_OBJECT (bd->channel_tabs), "switch_page",
                     G_CALLBACK (_blendop_blendif_tab_switch), bd);

    g_signal_connect (G_OBJECT (bd->upper_slider), "value-changed",
                      G_CALLBACK (_blendop_blendif_upper_callback), bd);

    g_signal_connect (G_OBJECT (bd->radius_slider), "value-changed",
                      G_CALLBACK (_blendop_blendif_radius_callback), bd);

    g_signal_connect (G_OBJECT (bd->lower_slider), "value-changed",
                      G_CALLBACK (_blendop_blendif_lower_callback), bd);

    g_signal_connect (G_OBJECT(bd->colorpicker), "toggled",
                      G_CALLBACK (_blendop_blendif_pick_toggled), module);

    g_signal_connect (G_OBJECT(bd->showmask), "toggled",
                      G_CALLBACK (_blendop_blendif_showmask_toggled), module);

    g_signal_connect (G_OBJECT(res), "clicked",
                      G_CALLBACK (_blendop_blendif_reset), module);

    g_signal_connect (G_OBJECT(bd->suppress), "toggled",
                      G_CALLBACK (_blendop_blendif_suppress_toggled), module);

    g_signal_connect (G_OBJECT(bd->lower_polarity), "toggled",
                      G_CALLBACK (_blendop_blendif_polarity_callback), bd);

    g_signal_connect (G_OBJECT(bd->upper_polarity), "toggled",
                      G_CALLBACK (_blendop_blendif_polarity_callback), bd);


    gtk_box_pack_start(GTK_BOX(bd->blendif_box), GTK_WIDGET(header), TRUE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bd->blendif_box), GTK_WIDGET(uplabel), TRUE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bd->blendif_box), GTK_WIDGET(upslider), TRUE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bd->blendif_box), GTK_WIDGET(lowlabel), TRUE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bd->blendif_box), GTK_WIDGET(lowslider), TRUE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bd->blendif_box), GTK_WIDGET(bd->radius_slider), TRUE, FALSE, 0);

    gtk_box_pack_end(GTK_BOX(blendw), GTK_WIDGET(bd->blendif_box),TRUE,TRUE,0);
    gtk_box_pack_end(GTK_BOX(blendw), GTK_WIDGET(bd->blendif_enable),TRUE,TRUE,0);

    bd->blendif_inited = 1;
  }
}


void dt_iop_gui_cleanup_blending(dt_iop_module_t *module)
{
  if (!module->blend_data) return;

  memset(module->blend_data, 0, sizeof(dt_iop_gui_blend_data_t));

  g_free(module->blend_data);
  module->blend_data = NULL;
}


void dt_iop_gui_update_blending(dt_iop_module_t *module)
{
  dt_iop_gui_blend_data_t *bd = (dt_iop_gui_blend_data_t*)module->blend_data;

  if (!(module->flags() & IOP_FLAGS_SUPPORTS_BLENDING) || !bd || !bd->blend_inited) return;

  int reset = darktable.gui->reset;
  darktable.gui->reset = 1;

  dt_bauhaus_combobox_set(bd->blend_modes_combo, dt_iop_gui_blending_mode_seq(bd, module->blend_params->mode));
  dt_bauhaus_slider_set(bd->opacity_slider, module->blend_params->opacity);

  dt_iop_gui_update_blendif(module);


  /* now show hide controls as required */
  if(bd->modes[dt_bauhaus_combobox_get(bd->blend_modes_combo)].mode == DEVELOP_BLEND_DISABLED)
  {
    gtk_widget_hide(GTK_WIDGET(bd->opacity_slider));
    if(bd->blendif_support && bd->blendif_inited)
    {
      gtk_widget_hide(GTK_WIDGET(bd->blendif_box));
      gtk_widget_hide(GTK_WIDGET(bd->blendif_enable));
    }
  }
  else
  {
    gtk_widget_show(GTK_WIDGET(bd->opacity_slider));
    if(bd->blendif_support && bd->blendif_inited)
    {
      gtk_widget_show(GTK_WIDGET(bd->blendif_enable));
      if(dt_bauhaus_combobox_get(bd->blendif_enable) != 0)
        gtk_widget_show(GTK_WIDGET(bd->blendif_box));
      else
        gtk_widget_hide(GTK_WIDGET(bd->blendif_box));
    }
  }
  darktable.gui->reset = reset;
}


void dt_iop_gui_init_blending(GtkWidget *iopw, dt_iop_module_t *module)
{
  /* create and add blend mode if module supports it */
  if (module->flags()&IOP_FLAGS_SUPPORTS_BLENDING)
  {
    module->blend_data = g_malloc(sizeof(dt_iop_gui_blend_data_t));
    memset(module->blend_data, 0, sizeof(dt_iop_gui_blend_data_t));
    dt_iop_gui_blend_data_t *bd = (dt_iop_gui_blend_data_t*)module->blend_data;

    dt_iop_gui_blendop_modes_t modes[23]; /* number must fit exactly!!! */
    modes[0].mode  = DEVELOP_BLEND_DISABLED;
    modes[0].name  = _("off");
    modes[1].mode  = DEVELOP_BLEND_NORMAL;
    modes[1].name  = _("normal");
    modes[2].mode  = DEVELOP_BLEND_INVERSE;
    modes[2].name  = _("inverse");
    modes[3].mode  = DEVELOP_BLEND_LIGHTEN;
    modes[3].name  = _("lighten");
    modes[4].mode  = DEVELOP_BLEND_DARKEN;
    modes[4].name  = _("darken");
    modes[5].mode  = DEVELOP_BLEND_MULTIPLY;
    modes[5].name  = _("multiply");
    modes[6].mode  = DEVELOP_BLEND_AVERAGE;
    modes[6].name  = _("average");
    modes[7].mode  = DEVELOP_BLEND_ADD;
    modes[7].name  = _("addition");
    modes[8].mode  = DEVELOP_BLEND_SUBSTRACT;
    modes[8].name  = _("subtract");
    modes[9].mode  = DEVELOP_BLEND_DIFFERENCE;
    modes[9].name  = _("difference");
    modes[10].mode = DEVELOP_BLEND_SCREEN;
    modes[10].name = _("screen");
    modes[11].mode = DEVELOP_BLEND_OVERLAY;
    modes[11].name = _("overlay");
    modes[12].mode = DEVELOP_BLEND_SOFTLIGHT;
    modes[12].name = _("softlight");
    modes[13].mode = DEVELOP_BLEND_HARDLIGHT;
    modes[13].name = _("hardlight");
    modes[14].mode = DEVELOP_BLEND_VIVIDLIGHT;
    modes[14].name = _("vividlight");
    modes[15].mode = DEVELOP_BLEND_LINEARLIGHT;
    modes[15].name = _("linearlight");
    modes[16].mode = DEVELOP_BLEND_PINLIGHT;
    modes[16].name = _("pinlight");
    modes[17].mode = DEVELOP_BLEND_LIGHTNESS;
    modes[17].name = _("lightness");
    modes[18].mode = DEVELOP_BLEND_CHROMA;
    modes[18].name = _("chroma");
    modes[19].mode = DEVELOP_BLEND_HUE;
    modes[19].name = _("hue");
    modes[20].mode = DEVELOP_BLEND_COLOR;
    modes[20].name = _("color");
    modes[21].mode = DEVELOP_BLEND_COLORADJUST;
    modes[21].name = _("coloradjustment");
    modes[22].mode = DEVELOP_BLEND_UNBOUNDED;
    modes[22].name = _("unbounded");


    bd->number_modes = sizeof(modes) / sizeof(dt_iop_gui_blendop_modes_t);
    memcpy(bd->modes, modes, bd->number_modes * sizeof(dt_iop_gui_blendop_modes_t));
    bd->iopw = iopw;
    bd->module = module;
    bd->csp = dt_iop_module_colorspace(module);
    bd->blendif_support = (bd->csp == iop_cs_Lab || bd->csp == iop_cs_rgb);
    bd->blendif_box = NULL;

    bd->blend_modes_combo = dt_bauhaus_combobox_new(module);
    dt_bauhaus_widget_set_label(bd->blend_modes_combo, _("blend mode"));
    bd->opacity_slider = dt_bauhaus_slider_new_with_range(module, 0.0, 100.0, 1, 100.0, 0);
    dt_bauhaus_widget_set_label(bd->opacity_slider, _("opacity"));
    dt_bauhaus_slider_set_format(bd->opacity_slider, "%.0f%%");
    module->fusion_slider = bd->opacity_slider;


    for(int k = 0; k < bd->number_modes; k++)
      dt_bauhaus_combobox_add(bd->blend_modes_combo, bd->modes[k].name);

    dt_bauhaus_combobox_set(bd->blend_modes_combo, 0);

    gtk_object_set(GTK_OBJECT(bd->opacity_slider), "tooltip-text", _("set the opacity of the blending"), (char *)NULL);
    gtk_object_set(GTK_OBJECT(bd->blend_modes_combo), "tooltip-text", _("choose blending mode"), (char *)NULL);

    g_signal_connect (G_OBJECT (bd->opacity_slider), "value-changed",
                      G_CALLBACK (_blendop_opacity_callback), bd);
    g_signal_connect (G_OBJECT (bd->blend_modes_combo), "value-changed",
                      G_CALLBACK (_blendop_mode_callback), bd);

    gtk_box_pack_start(GTK_BOX(iopw), bd->blend_modes_combo, TRUE, TRUE,0);
    gtk_box_pack_start(GTK_BOX(iopw), bd->opacity_slider, TRUE, TRUE,0);

    if(bd->blendif_support)
    {
      dt_iop_gui_init_blendif(GTK_VBOX(iopw), module);
    }
    bd->blend_inited = 1;
    gtk_widget_queue_draw(GTK_WIDGET(iopw));
    dt_iop_gui_update_blending(module);
  }
}


// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
