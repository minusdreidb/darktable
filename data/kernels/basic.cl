/*
    This file is part of darktable,
    copyright (c) 2009--2012 johannes hanika.

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

const sampler_t sampleri =  CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST;
const sampler_t samplerf =  CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_LINEAR;

#ifndef M_PI
#define M_PI           3.14159265358979323846  // should be defined by the OpenCL compiler acc. to standard
#endif

int
FC(const int row, const int col, const unsigned int filters)
{
  return filters >> ((((row) << 1 & 14) + ((col) & 1)) << 1) & 3;
}


kernel void
whitebalance_1ui(read_only image2d_t in, write_only image2d_t out, const int width, const int height, global float *coeffs,
    const unsigned int filters, const int rx, const int ry)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);
  if(x >= width || y >= height) return;
  const uint4 pixel = read_imageui(in, sampleri, (int2)(x, y));
  write_imagef (out, (int2)(x, y), (float4)(pixel.x * coeffs[FC(ry+y, rx+x, filters)], 0.0f, 0.0f, 0.0f));
}

kernel void
whitebalance_1f(read_only image2d_t in, write_only image2d_t out, const int width, const int height, global float *coeffs,
    const unsigned int filters, const int rx, const int ry)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);
  if(x >= width || y >= height) return;
  const float pixel = read_imagef(in, sampleri, (int2)(x, y)).x;
  write_imagef (out, (int2)(x, y), (float4)(pixel * coeffs[FC(rx+y, ry+x, filters)], 0.0f, 0.0f, 0.0f));
}

kernel void
whitebalance_4f(read_only image2d_t in, write_only image2d_t out, const int width, const int height, global float *coeffs,
    const unsigned int filters, const int rx, const int ry)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);
  if(x >= width || y >= height) return;
  const float4 pixel = read_imagef(in, sampleri, (int2)(x, y));
  write_imagef (out, (int2)(x, y), (float4)(pixel.x * coeffs[0], pixel.y * coeffs[1], pixel.z * coeffs[2], pixel.w));
}

/* kernel for the exposure plugin. should work transparently with float4 and float image2d. */
kernel void
exposure (read_only image2d_t in, write_only image2d_t out, const int width, const int height, const float black, const float scale)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;
  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));
  pixel.xyz = (pixel.xyz - black)*scale;
  write_imagef (out, (int2)(x, y), pixel);
}

/* helpers for the highlights plugin: convert to lch. */

constant float xyz_rgb[9] = {  /* XYZ from RGB */
	  0.412453, 0.357580, 0.180423,
	  0.212671, 0.715160, 0.072169,
	  0.019334, 0.119193, 0.950227};
constant float rgb_xyz[9] = {  /* RGB from XYZ */
	  3.24048, -1.53715, -0.498536,
	  -0.969255, 1.87599, 0.0415559,
	  0.0556466, -0.204041, 1.05731};

void
rgb_to_lch (float *rgb, float *lch)
{
	float xyz[3], lab[3];
	xyz[0] = xyz[1] = xyz[2] = 0.0f;
	for (int c=0; c<3; c++)
		for (int cc=0; cc<3; cc++)
			xyz[cc] += xyz_rgb[3*cc+c] * rgb[c];
	for (int c=0; c<3; c++)
		xyz[c] = xyz[c] > 0.008856f ? native_powr(xyz[c], 1.0f/3.0f) : 7.787f*xyz[c] + 16.0f/116.0f;
	lab[0] = 116.0f * xyz[1] - 16.0f;
	lab[1] = 500.0f * (xyz[0] - xyz[1]);
	lab[2] = 200.0f * (xyz[1] - xyz[2]);

	lch[0] = lab[0];
	lch[1] = native_sqrt(lab[1]*lab[1]+lab[2]*lab[2]);
	lch[2] = atan2(lab[2], lab[1]);
}

// convert CIE-LCh to linear RGB
void
lch_to_rgb(float *lch, float *rgb)
{
	float xyz[3], lab[3];
	const float epsilon = 0.008856f, kappa = 903.3f;
	lab[0] = lch[0];
	lab[1] = lch[1] * native_cos(lch[2]);
	lab[2] = lch[1] * native_sin(lch[2]);
	xyz[1] = (lab[0]<=kappa*epsilon) ?
		(lab[0]/kappa) : (native_powr((lab[0]+16.0f)/116.0f, 3.0f));
	const float fy = (xyz[1]<=epsilon) ? ((kappa*xyz[1]+16.0f)/116.0f) : ((lab[0]+16.0f)/116.0f);
	const float fz = fy - lab[2]/200.0f;
	const float fx = lab[1]/500.0f + fy;
	xyz[2] = (native_powr(fz, 3.0f)<=epsilon) ? ((116.0f*fz-16.0f)/kappa) : (native_powr(fz, 3.0f));
	xyz[0] = (native_powr(fx, 3.0f)<=epsilon) ? ((116.0f*fx-16.0f)/kappa) : (native_powr(fx, 3.0f));

	for (int c=0; c<3; c++)
	{
		float tmpf = 0.0f;
		for (int cc=0; cc<3; cc++)
			tmpf += rgb_xyz[3*c+cc] * xyz[cc];
		rgb[c] = fmax(tmpf, 0.0f);
	}
}

/* kernel for the highlights plugin. */
kernel void
highlights (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
            const int mode, const float clip, const float blendL, const float blendC, const float blendh)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));
  float4 inc, lchi, lchc, lch;
  switch(mode)
  {
    case 1: // DT_IOP_HIGHLIGHTS_LCH
      inc.x = fmin(clip, pixel.x);
      inc.y = fmin(clip, pixel.y);
      inc.z = fmin(clip, pixel.z);
      rgb_to_lch((float *)&pixel, (float *)&lchi);
      rgb_to_lch((float *)&inc, (float *)&lchc);
      lch.x = lchc.x + blendL * (lchi.x - lchc.x);
      lch.y = lchc.y + blendC * (lchi.y - lchc.y);
      lch.z = lchc.z + blendh * (lchi.z - lchc.z);
      lch_to_rgb((float *)&lch, (float *)&pixel);
      break;
    default: // 0, DT_IOP_HIGHLIGHTS_CLIP
      pixel.x = fmin(clip, pixel.x);
      pixel.y = fmin(clip, pixel.y);
      pixel.z = fmin(clip, pixel.z);
      break;
  }
  write_imagef (out, (int2)(x, y), pixel);
}

float
lookup_unbounded(read_only image2d_t lut, const float x, global float *a)
{
  // in case the tone curve is marked as linear, return the fast
  // path to linear unbounded (does not clip x at 1)
  if(a[0] >= 0.0f)
  {
    if(x < 1.0f/a[0])
    {
      const int xi = clamp(x*65535.0f, 0.0f, 65535.0f);
      const int2 p = (int2)((xi & 0xff), (xi >> 8));
      return read_imagef(lut, sampleri, p).x;
    }
    else return a[1] * native_powr(x*a[0], a[2]);
  }
  else return x;
}

float
lookup(read_only image2d_t lut, const float x)
{
  int xi = clamp(x*65535.0f, 0.0f, 65535.0f);
  int2 p = (int2)((xi & 0xff), (xi >> 8));
  return read_imagef(lut, sampleri, p).x;
}

/* kernel for the basecurve plugin. */
kernel void
basecurve (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
           read_only image2d_t table, global float *a)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));
  // use lut or extrapolation:
  pixel.x = lookup_unbounded(table, pixel.x, a);
  pixel.y = lookup_unbounded(table, pixel.y, a);
  pixel.z = lookup_unbounded(table, pixel.z, a);
  write_imagef (out, (int2)(x, y), pixel);
}


void
XYZ_to_Lab(float *xyz, float *lab)
{
  xyz[0] *= (1.0f/0.9642f);
  xyz[2] *= (1.0f/0.8242f);
	for (int c=0; c<3; c++)
		xyz[c] = xyz[c] > 0.008856f ? native_powr(xyz[c], 1.0f/3.0f) : 7.787f*xyz[c] + 16.0f/116.0f;
	lab[0] = 116.0f * xyz[1] - 16.0f;
	lab[1] = 500.0f * (xyz[0] - xyz[1]);
	lab[2] = 200.0f * (xyz[1] - xyz[2]);
}

/* kernel for the plugin colorin */
kernel void
colorin (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
         global float *mat, read_only image2d_t lutr, read_only image2d_t lutg, read_only image2d_t lutb,
         const int map_blues,
         global float *a)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));

  float cam[3], XYZ[3], Lab[3];
  cam[0] = lookup_unbounded(lutr, pixel.x, a);
  cam[1] = lookup_unbounded(lutg, pixel.y, a+3);
  cam[2] = lookup_unbounded(lutb, pixel.z, a+6);


  const float YY = cam[0]+cam[1]+cam[2];
  if(map_blues && YY > 0.0f)
  {
    // manual gamut mapping. these values cause trouble when converting back from Lab to sRGB:
    const float zz = cam[2]/YY;
    // lower amount and higher bound_z make the effect smaller.
    // the effect is weakened the darker input values are, saturating at bound_Y
    const float bound_z = 0.5f, bound_Y = 0.8f;
    const float amount = 0.11f;
    if (zz > bound_z)
    {
      const float t = (zz - bound_z)/(1.0f-bound_z) * fmin(1.0f, YY/bound_Y);
      cam[1] += t*amount;
      cam[2] -= t*amount;
    }
  }
  // now convert camera to XYZ using the color matrix
  for(int j=0;j<3;j++)
  {
    XYZ[j] = 0.0f;
    for(int i=0;i<3;i++) XYZ[j] += mat[3*j+i] * cam[i];
  }
  XYZ_to_Lab(XYZ, (float *)&pixel);
  write_imagef (out, (int2)(x, y), pixel);
}

/* kernel for the tonecurve plugin version 2 */
kernel void
tonecurve (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
           read_only image2d_t table_L, read_only image2d_t table_a, read_only image2d_t table_b,
           const int autoscale_ab, global float *a)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));
  const float L_in = pixel.x/100.0f;
  // use lut or extrapolation:
  const float L = lookup_unbounded(table_L, L_in, a);
  if (autoscale_ab == 0)
  {
    const float a_in = (pixel.y + 128.0f) / 256.0f;
    const float b_in = (pixel.z + 128.0f) / 256.0f;
    pixel.y = lookup(table_a, a_in);
    pixel.z = lookup(table_b, b_in);
  }
  else if(pixel.x > 0.01f)
  {
    pixel.y *= L/pixel.x;
    pixel.z *= L/pixel.x;
  }
  else
  {
    pixel.y *= L/0.01f;
    pixel.z *= L/0.01f;
  }
  pixel.x = L;
  write_imagef (out, (int2)(x, y), pixel);
}


/* kernel for the colorcorrection plugin. */
__kernel void
colorcorrection (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
                 const float saturation, const float a_scale, const float a_base, 
                 const float b_scale, const float b_base)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));
  pixel.y = saturation*(pixel.y + pixel.x * a_scale + a_base);
  pixel.z = saturation*(pixel.z + pixel.x * b_scale + b_base);
  write_imagef (out, (int2)(x, y), pixel);
}


void
mul_mat_vec_2(const float4 m, const float2 *p, float2 *o)
{
  (*o).x = (*p).x*m.x + (*p).y*m.y;
  (*o).y = (*p).x*m.z + (*p).y*m.w;
}

void
backtransform(float2 *p, float2 *o, const float4 m, const float2 t)
{
  (*p).y /= (1.0f + (*p).x*t.x);
  (*p).x /= (1.0f + (*p).y*t.y);
  mul_mat_vec_2(m, p, o);
}



float
interpolation_func_bicubic(float t)
{
  float r;
  t = fabs(t);

  r = (t >= 2.0f) ? 0.0f : ((t > 1.0f) ? (0.5f*(t*(-t*t + 5.0f*t - 8.0f) + 4.0f)) : (0.5f*(t*(3.0f*t*t - 5.0f*t) + 2.0f)));

  return r;
}

#define DT_LANCZOS_EPSILON (1e-9f)

#if 0
float
interpolation_func_lanczos(float width, float t)
{
  float ta = fabs(t);

  float r = (ta > width) ? 0.0f : ((ta < DT_LANCZOS_EPSILON) ? 1.0f : width*native_sin(M_PI*t)*native_sin(M_PI*t/width)/(M_PI*M_PI*t*t));

  return r;
}
#else
float
sinf_fast(float t)
{
  const float a = 4/(M_PI*M_PI);
  const float p = 0.225f;

  t = a*t*(M_PI - fabs(t));

  return p*(t*fabs(t) - t) + t;
}

float
interpolation_func_lanczos(float width, float t)
{
  /* Compute a value for sinf(pi.t) in [-pi pi] for which the value will be
   * correct */
  int a = (int)t;
  float r = t - (float)a;

  // Compute the correct sign for sinf(pi.r)
  union { float f; unsigned int i; } sign;
  sign.i = ((a&1)<<31) | 0x3f800000;

  return (DT_LANCZOS_EPSILON + width*sign.f*sinf_fast(M_PI*r)*sinf_fast(M_PI*t/width))/(DT_LANCZOS_EPSILON + M_PI*M_PI*t*t);
}
#endif


/* kernel for clip&rotate: bilinear interpolation */
__kernel void
clip_rotate_bilinear(read_only image2d_t in, write_only image2d_t out, const int width, const int height, 
            const int in_width, const int in_height,
            const int2 roi_in, const int2 roi_out, const float scale_in, const float scale_out,
            const int flip, const float2 ci, const float2 t, const float2 k, const float4 mat)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float2 pi, po;
  
  pi.x = roi_out.x + scale_out * ci.x + x + 0.5f;
  pi.y = roi_out.y + scale_out * ci.y + y + 0.5f;

  pi.x -= flip ? t.y * scale_out : t.x * scale_out;
  pi.y -= flip ? t.x * scale_out : t.y * scale_out;

  pi /= scale_out;
  backtransform(&pi, &po, mat, k);
  po *= scale_in;

  po.x += t.x * scale_in;
  po.y += t.y * scale_in;

  po.x -= roi_in.x;
  po.y -= roi_in.y;

  const int ii = (int)po.x;
  const int jj = (int)po.y;

  float4 o;

  if (ii >=0 && jj >= 0 && ii <= in_width-2 && jj <= in_height-2)
    o = read_imagef(in, samplerf, po);
  else
    o = (float4)0.0f;

  write_imagef (out, (int2)(x, y), o);
}



/* kernel for clip&rotate: bicubic interpolation */
__kernel void
clip_rotate_bicubic(read_only image2d_t in, write_only image2d_t out, const int width, const int height, 
            const int in_width, const int in_height,
            const int2 roi_in, const int2 roi_out, const float scale_in, const float scale_out,
            const int flip, const float2 ci, const float2 t, const float2 k, const float4 mat)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  const int kwidth = 2;

  if(x >= width || y >= height) return;

  float2 pi, po;
  
  pi.x = roi_out.x + scale_out * ci.x + x + 0.5f;
  pi.y = roi_out.y + scale_out * ci.y + y + 0.5f;

  pi.x -= flip ? t.y * scale_out : t.x * scale_out;
  pi.y -= flip ? t.x * scale_out : t.y * scale_out;

  pi /= scale_out;
  backtransform(&pi, &po, mat, k);
  po *= scale_in;

  po.x += t.x * scale_in;
  po.y += t.y * scale_in;

  po.x -= roi_in.x;
  po.y -= roi_in.y;

  int tx = po.x;
  int ty = po.y;

  float4 pixel = (float4)0.0f;
  float weight = 0.0f;

  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    float wx = interpolation_func_bicubic((float)(tx + ii) - po.x);
    float wy = interpolation_func_bicubic((float)(ty + jj) - po.y);
    float w = wx * wy;

    pixel += read_imagef(in, sampleri, (int2)(tx + ii, ty + jj)) * w;
    weight += w;
  }

  write_imagef (out, (int2)(x, y), pixel / weight);
}


/* kernel for clip&rotate: lanczos2 interpolation */
__kernel void
clip_rotate_lanczos2(read_only image2d_t in, write_only image2d_t out, const int width, const int height, 
            const int in_width, const int in_height,
            const int2 roi_in, const int2 roi_out, const float scale_in, const float scale_out,
            const int flip, const float2 ci, const float2 t, const float2 k, const float4 mat)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  const int kwidth = 2;

  if(x >= width || y >= height) return;

  float2 pi, po;
  
  pi.x = roi_out.x + scale_out * ci.x + x + 0.5f;
  pi.y = roi_out.y + scale_out * ci.y + y + 0.5f;

  pi.x -= flip ? t.y * scale_out : t.x * scale_out;
  pi.y -= flip ? t.x * scale_out : t.y * scale_out;

  pi /= scale_out;
  backtransform(&pi, &po, mat, k);
  po *= scale_in;

  po.x += t.x * scale_in;
  po.y += t.y * scale_in;

  po.x -= roi_in.x;
  po.y -= roi_in.y;

  int tx = po.x;
  int ty = po.y;

  float4 pixel = (float4)0.0f;
  float weight = 0.0f;

  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    float wx = interpolation_func_lanczos(2, (float)(tx + ii) - po.x);
    float wy = interpolation_func_lanczos(2, (float)(ty + jj) - po.y);
    float w = wx * wy;

    pixel += read_imagef(in, sampleri, (int2)(tx + ii, ty + jj)) * w;
    weight += w;
  }

  write_imagef (out, (int2)(x, y), pixel / weight);
}



/* kernel for clip&rotate: lanczos3 interpolation */
__kernel void
clip_rotate_lanczos3(read_only image2d_t in, write_only image2d_t out, const int width, const int height, 
            const int in_width, const int in_height,
            const int2 roi_in, const int2 roi_out, const float scale_in, const float scale_out,
            const int flip, const float2 ci, const float2 t, const float2 k, const float4 mat)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  const int kwidth = 3;

  if(x >= width || y >= height) return;

  float2 pi, po;
  
  pi.x = roi_out.x + scale_out * ci.x + x + 0.5f;
  pi.y = roi_out.y + scale_out * ci.y + y + 0.5f;

  pi.x -= flip ? t.y * scale_out : t.x * scale_out;
  pi.y -= flip ? t.x * scale_out : t.y * scale_out;

  pi /= scale_out;
  backtransform(&pi, &po, mat, k);
  po *= scale_in;

  po.x += t.x * scale_in;
  po.y += t.y * scale_in;

  po.x -= roi_in.x;
  po.y -= roi_in.y;

  int tx = po.x;
  int ty = po.y;

  float4 pixel = (float4)0.0f;
  float weight = 0.0f;

  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    float wx = interpolation_func_lanczos(3, (float)(tx + ii) - po.x);
    float wy = interpolation_func_lanczos(3, (float)(ty + jj) - po.y);
    float w = wx * wy;

    pixel += read_imagef(in, sampleri, (int2)(tx + ii, ty + jj)) * w;
    weight += w;
  }

  write_imagef (out, (int2)(x, y), pixel / weight);
}


/* kernels for the lens plugin: bilinear interpolation */
kernel void
lens_distort_bilinear (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
               const int iwidth, const int iheight, const int roi_in_x, const int roi_in_y, global float *pi)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel;

  float rx, ry;
  const int piwidth = 2*3*width;
  global float *ppi = pi + mad24(y, piwidth, 2*3*x);

  rx = ppi[0] - roi_in_x;
  ry = ppi[1] - roi_in_y;
  pixel.x = (rx >= 0 && ry >= 0 && rx <= iwidth - 1 && ry <= iheight - 1) ? read_imagef(in, samplerf, (float2)(rx, ry)).x : 0.0f;

  rx = ppi[2] - roi_in_x;
  ry = ppi[3] - roi_in_y;
  pixel.y = (rx >= 0 && ry >= 0 && rx <= iwidth - 1 && ry <= iheight - 1) ? read_imagef(in, samplerf, (float2)(rx, ry)).y : 0.0f;
  pixel.w = (rx >= 0 && ry >= 0 && rx <= iwidth - 1 && ry <= iheight - 1) ? read_imagef(in, samplerf, (float2)(rx, ry)).w : 0.0f;

  rx = ppi[4] - roi_in_x;
  ry = ppi[5] - roi_in_y;
  pixel.z = (rx >= 0 && ry >= 0 && rx <= iwidth - 1 && ry <= iheight - 1) ? read_imagef(in, samplerf, (float2)(rx, ry)).z : 0.0f;

  write_imagef (out, (int2)(x, y), pixel); 
}



/* kernels for the lens plugin: bicubic interpolation */
kernel void
lens_distort_bicubic (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
                      const int iwidth, const int iheight, const int roi_in_x, const int roi_in_y, global float *pi)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  const int kwidth = 2;

  if(x >= width || y >= height) return;

  float4 pixel = (float4)0.0f;

  float rx, ry;
  int tx, ty;
  float sum, weight;
  float2 sum2;
  const int piwidth = 2*3*width;
  global float *ppi = pi + mad24(y, piwidth, 2*3*x);

  rx = ppi[0] - (float)roi_in_x;
  ry = ppi[1] - (float)roi_in_y;

  tx = rx;
  ty = ry;

  sum = 0.0f;
  weight = 0.0f;
  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    float wx = interpolation_func_bicubic((float)(tx + ii) - rx);
    float wy = interpolation_func_bicubic((float)(ty + jj) - ry);
    float w = wx * wy;

    sum += read_imagef(in, sampleri, (int2)(tx + ii, ty + jj)).x * w;
    weight += w;
  }
  pixel.x = (tx >= 0 && ty >= 0 && tx <= iwidth - 1 && ty <= iheight - 1) ? sum/weight : 0.0f;


  rx = ppi[2] - (float)roi_in_x;
  ry = ppi[3] - (float)roi_in_y;

  tx = rx;
  ty = ry;

  sum2 = (float2)0.0f;
  weight = 0.0f;
  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    float wx = interpolation_func_bicubic((float)(tx + ii) - rx);
    float wy = interpolation_func_bicubic((float)(ty + jj) - ry);
    float w = wx * wy;

    sum2 += read_imagef(in, sampleri, (int2)(tx + ii, ty + jj)).yw * w;
    weight += w;
  }
  pixel.yw = (tx >= 0 && ty >= 0 && tx <= iwidth - 1 && ty <= iheight - 1) ? sum2/weight : (float2)0.0f;


  rx = ppi[4] - (float)roi_in_x;
  ry = ppi[5] - (float)roi_in_y;

  tx = rx;
  ty = ry;

  sum = 0.0f;
  weight = 0.0f;
  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    float wx = interpolation_func_bicubic((float)(tx + ii) - rx);
    float wy = interpolation_func_bicubic((float)(ty + jj) - ry);
    float w = wx * wy;

    sum += read_imagef(in, sampleri, (int2)(tx + ii, ty + jj)).z * w;
    weight += w;
  }
  pixel.z = (tx >= 0 && ty >= 0 && tx <= iwidth - 1 && ty <= iheight - 1) ? sum/weight : 0.0f;

  write_imagef (out, (int2)(x, y), pixel); 
}


/* kernels for the lens plugin: lanczos2 interpolation */
kernel void
lens_distort_lanczos2 (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
                      const int iwidth, const int iheight, const int roi_in_x, const int roi_in_y, global float *pi)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  const int kwidth = 2;

  if(x >= width || y >= height) return;

  float4 pixel = (float4)0.0f;

  float rx, ry;
  int tx, ty;
  float sum, weight;
  float2 sum2;
  const int piwidth = 2*3*width;
  global float *ppi = pi + mad24(y, piwidth, 2*3*x);

  rx = ppi[0] - (float)roi_in_x;
  ry = ppi[1] - (float)roi_in_y;

  tx = rx;
  ty = ry;

  sum = 0.0f;
  weight = 0.0f;
  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    float wx = interpolation_func_lanczos(2, (float)(tx + ii) - rx);
    float wy = interpolation_func_lanczos(2, (float)(ty + jj) - ry);
    float w = wx * wy;

    sum += read_imagef(in, sampleri, (int2)(tx + ii, ty + jj)).x * w;
    weight += w;
  }
  pixel.x = (tx >= 0 && ty >= 0 && tx <= iwidth - 1 && ty <= iheight - 1) ? sum/weight : 0.0f;


  rx = ppi[2] - (float)roi_in_x;
  ry = ppi[3] - (float)roi_in_y;

  tx = rx;
  ty = ry;

  sum2 = (float2)0.0f;
  weight = 0.0f;
  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    float wx = interpolation_func_lanczos(2, (float)(tx + ii) - rx);
    float wy = interpolation_func_lanczos(2, (float)(ty + jj) - ry);
    float w = wx * wy;

    sum2 += read_imagef(in, sampleri, (int2)(tx + ii, ty + jj)).yw * w;
    weight += w;
  }
  pixel.yw = (tx >= 0 && ty >= 0 && tx <= iwidth - 1 && ty <= iheight - 1) ? sum2/weight : (float2)0.0f;


  rx = ppi[4] - (float)roi_in_x;
  ry = ppi[5] - (float)roi_in_y;

  tx = rx;
  ty = ry;

  sum = 0.0f;
  weight = 0.0f;
  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    float wx = interpolation_func_lanczos(2, (float)(tx + ii) - rx);
    float wy = interpolation_func_lanczos(2, (float)(ty + jj) - ry);
    float w = wx * wy;

    sum += read_imagef(in, sampleri, (int2)(tx + ii, ty + jj)).z * w;
    weight += w;
  }
  pixel.z = (tx >= 0 && ty >= 0 && tx <= iwidth - 1 && ty <= iheight - 1) ? sum/weight : 0.0f;

  write_imagef (out, (int2)(x, y), pixel); 
}


/* kernels for the lens plugin: lanczos3 interpolation */
kernel void
lens_distort_lanczos3 (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
                      const int iwidth, const int iheight, const int roi_in_x, const int roi_in_y, global float *pi)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  const int kwidth = 3;

  if(x >= width || y >= height) return;

  float4 pixel = (float4)0.0f;

  float rx, ry;
  int tx, ty;
  float sum, weight;
  float2 sum2;
  const int piwidth = 2*3*width;
  global float *ppi = pi + mad24(y, piwidth, 2*3*x);

  rx = ppi[0] - (float)roi_in_x;
  ry = ppi[1] - (float)roi_in_y;

  tx = rx;
  ty = ry;

  sum = 0.0f;
  weight = 0.0f;
  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    float wx = interpolation_func_lanczos(3, (float)(tx + ii) - rx);
    float wy = interpolation_func_lanczos(3, (float)(ty + jj) - ry);
    float w = wx * wy;

    sum += read_imagef(in, sampleri, (int2)(tx + ii, ty + jj)).x * w;
    weight += w;
  }
  pixel.x = (tx >= 0 && ty >= 0 && tx <= iwidth - 1 && ty <= iheight - 1) ? sum/weight : 0.0f;


  rx = ppi[2] - (float)roi_in_x;
  ry = ppi[3] - (float)roi_in_y;

  tx = rx;
  ty = ry;

  sum2 = (float2)0.0f;
  weight = 0.0f;
  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    float wx = interpolation_func_lanczos(3, (float)(tx + ii) - rx);
    float wy = interpolation_func_lanczos(3, (float)(ty + jj) - ry);
    float w = wx * wy;

    sum2 += read_imagef(in, sampleri, (int2)(tx + ii, ty + jj)).yw * w;
    weight += w;
  }
  pixel.yw = (tx >= 0 && ty >= 0 && tx <= iwidth - 1 && ty <= iheight - 1) ? sum2/weight : (float2)0.0f;


  rx = ppi[4] - (float)roi_in_x;
  ry = ppi[5] - (float)roi_in_y;

  tx = rx;
  ty = ry;

  sum = 0.0f;
  weight = 0.0f;
  for(int jj = 1 - kwidth; jj <= kwidth; jj++)
    for(int ii= 1 - kwidth; ii <= kwidth; ii++)
  {
    float wx = interpolation_func_lanczos(3, (float)(tx + ii) - rx);
    float wy = interpolation_func_lanczos(3, (float)(ty + jj) - ry);
    float w = wx * wy;

    sum += read_imagef(in, sampleri, (int2)(tx + ii, ty + jj)).z * w;
    weight += w;
  }
  pixel.z = (tx >= 0 && ty >= 0 && tx <= iwidth - 1 && ty <= iheight - 1) ? sum/weight : 0.0f;

  write_imagef (out, (int2)(x, y), pixel); 
}


kernel void
lens_vignette (read_only image2d_t in, write_only image2d_t out, const int width, const int height, global float4 *pi)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));
  float4 scale = pi[mad24(y, width, x)]/(float4)0.5f;

  pixel.xyz *= scale.xyz;

  write_imagef (out, (int2)(x, y), pixel); 
}



/* kernel for flip */
__kernel void
flip(read_only image2d_t in, write_only image2d_t out, const int width, const int height, const int orientation)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));

  int nx = (orientation & 4) ? y : x;
  int ny = (orientation & 4) ? x : y;
  int wd = (orientation & 4) ? height : width;
  int ht = (orientation & 4) ? width : height;
  nx = (orientation & 2) ? wd - nx - 1 : nx;
  ny = (orientation & 1) ? ht - ny - 1 : ny;

  write_imagef (out, (int2)(nx, ny), pixel);
}


/* we use this exp approximation to maintain full identity with cpu path */
float 
fast_expf(const float x)
{
  // meant for the range [-100.0f, 0.0f]. largest error ~ -0.06 at 0.0f.
  // will get _a_lot_ worse for x > 0.0f (9000 at 10.0f)..
  const int i1 = 0x3f800000u;
  // e^x, the comment would be 2^x
  const int i2 = 0x402DF854u;//0x40000000u;
  // const int k = CLAMPS(i1 + x * (i2 - i1), 0x0u, 0x7fffffffu);
  // without max clamping (doesn't work for large x, but is faster):
  const int k0 = i1 + x * (i2 - i1);
  const int k = k0 > 0 ? k0 : 0;
  const float f = *(const float *)&k;
  return f;
}


/* kernel for monochrome */
__kernel void
monochrome(read_only image2d_t in, write_only image2d_t out, const int width, const int height, 
           const float a, const float b, const float size)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));
  pixel.x *= fast_expf(-clamp(((pixel.y - a)*(pixel.y - a) + (pixel.z - b)*(pixel.z - b))/(2.0f * size), 0.0f, 1.0f));
  pixel.y = pixel.z = 0.0f;
  write_imagef (out, (int2)(x, y), pixel);
}


float
lab_f_inv(float x)
{
  const float epsilon = 0.206896551f; 
  const float kappa   = 24389.0f/27.0f;
  if(x > epsilon) return x*x*x;
  else return (116.0f*x - 16.0f)/kappa;
}

void
Lab_to_XYZ(float *Lab, float *XYZ)
{
  const float d50[3] = { 0.9642f, 1.0f, 0.8249f };
  const float fy = (Lab[0] + 16.0f)/116.0f;
  const float fx = Lab[1]/500.0f + fy;
  const float fz = fy - Lab[2]/200.0f;
  XYZ[0] = d50[0]*lab_f_inv(fx);
  XYZ[1] = d50[1]*lab_f_inv(fy);
  XYZ[2] = d50[2]*lab_f_inv(fz);
}

/* kernel for the plugin colorout, fast matrix + shaper path only */
kernel void
colorout (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
          global float *mat, read_only image2d_t lutr, read_only image2d_t lutg, read_only image2d_t lutb,
          global float *a)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));
  float XYZ[3], rgb[3];
  Lab_to_XYZ((float *)&pixel, XYZ);
  for(int i=0;i<3;i++)
  {
    rgb[i] = 0.0f;
    for(int j=0;j<3;j++) rgb[i] += mat[3*i+j]*XYZ[j];
  }
  pixel.x = lookup_unbounded(lutr, rgb[0], a);
  pixel.y = lookup_unbounded(lutg, rgb[1], a+3);
  pixel.z = lookup_unbounded(lutb, rgb[2], a+6);
  write_imagef (out, (int2)(x, y), pixel);
}


/* kernel for the levels plugin */
kernel void
levels (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
        read_only image2d_t lut, const float in_low, const float in_high, const float in_inv_gamma)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));
  const float L = pixel.x;
  const float L_in = pixel.x/100.0f;

  if(L_in <= in_low)
  {
    pixel.x = 0.0f;
  }
  else if(L_in >= in_high)
  {
    float percentage = (L_in - in_low) / (in_high - in_low);
    pixel.x = 100.0f * pow(percentage, in_inv_gamma);
  }
  else
  {
    float percentage = (L_in - in_low) / (in_high - in_low);
    pixel.x = lookup(lut, percentage);
  }

  if(L_in > 0.01f)
  {
    pixel.y *= pixel.x/L;
    pixel.z *= pixel.x/L;
  }
  else
  {
    pixel.y *= pixel.x;
    pixel.z *= pixel.x;
  }

  write_imagef (out, (int2)(x, y), pixel);
}


/* kernel for the colorzones plugin */
enum
{
  DT_IOP_COLORZONES_L = 0,
  DT_IOP_COLORZONES_C = 1,
  DT_IOP_COLORZONES_h = 2
};


kernel void
colorzones (read_only image2d_t in, write_only image2d_t out, const int width, const int height, const int channel,
            read_only image2d_t table_L, read_only image2d_t table_a, read_only image2d_t table_b)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));

  const float a = pixel.y;
  const float b = pixel.z;
  const float h = fmod(atan2(b, a) + 2.0f*M_PI, 2.0f*M_PI)/(2.0f*M_PI);
  const float C = sqrt(b*b + a*a);

  float select = 0.0f;
  float blend = 0.0f;

  switch(channel)
  {
    case DT_IOP_COLORZONES_L:
      select = fmin(1.0f, pixel.x/100.0f);
      break;
    case DT_IOP_COLORZONES_C:
      select = fmin(1.0f, C/128.0f);
      break;
    default:
    case DT_IOP_COLORZONES_h:
      select = h;
      blend = pow(1.0f - C/128.0f, 2.0f);
      break;
  }

  const float Lm = (blend * 0.5f + (1.0f-blend)*lookup(table_L, select)) - 0.5f;
  const float hm = (blend * 0.5f + (1.0f-blend)*lookup(table_b, select)) - 0.5f;
  blend *= blend; // saturation isn't as prone to artifacts:
  // const float Cm = 2.0 * (blend*.5f + (1.0f-blend)*lookup(d->lut[1], select));
  const float Cm = 2.0f * lookup(table_a, select);
  const float L = pixel.x * pow(2.0f, 4.0f*Lm);

  pixel.x = L;
  pixel.y = cos(2.0f*M_PI*(h + hm)) * Cm * C;
  pixel.z = sin(2.0f*M_PI*(h + hm)) * Cm * C;

  write_imagef (out, (int2)(x, y), pixel); 
}


/* kernel for the zonesystem plugin */
kernel void
zonesystem (read_only image2d_t in, write_only image2d_t out, const int width, const int height, const int size,
            global float *zonemap_offset, global float *zonemap_scale)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));

  const float rzscale = (float)(size-1)/100.0f;
  const int rz = clamp((int)(pixel.x*rzscale), 0, size-2);
  const float zs = ((rz > 0) ? (zonemap_offset[rz]/pixel.x) : 0) + zonemap_scale[rz];

  pixel.xyz *= zs;

  write_imagef (out, (int2)(x, y), pixel); 
}




/* kernel to fill an image with a color (for the borders plugin). */
kernel void
borders_fill (write_only image2d_t out, const int left, const int top, const int width, const int height, const float4 color)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x < left || y < top) return;
  if(x >= width + left || y >= height + top) return;

  write_imagef (out, (int2)(x, y), color);
}


/* kernel for the overexposed plugin. */
kernel void
overexposed (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
             const float lower, const float upper, const float4 lower_color, const float4 upper_color)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));

  if(pixel.x >= upper || pixel.y >= upper || pixel.z >= upper)
  {
    pixel.xyz = upper_color.xyz;
  }
  else if(pixel.x <= lower && pixel.y <= lower && pixel.z <= lower)
  {
    pixel.xyz = lower_color.xyz;
  }

  write_imagef (out, (int2)(x, y), pixel);
}


/* kernel for the lowlight plugin. */
kernel void
lowlight (read_only image2d_t in, write_only image2d_t out, const int width, const int height,
          const float4 XYZ_sw, read_only image2d_t lut)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  const float c = 0.5f;
  const float threshold = 0.01f;

  float4 XYZ;
  float V;
  float w;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));

  Lab_to_XYZ((float*)&pixel, (float *)&XYZ);

  // calculate scotopic luminance
  if (XYZ.x > threshold)
  {
    // normal flow
    V = XYZ.y * ( 1.33f * ( 1.0f + (XYZ.y+XYZ.z)/XYZ.x) - 1.68f );
  }
  else
  {
    // low red flow, avoids "snow" on dark noisy areas
    V = XYZ.y * ( 1.33f * ( 1.0f + (XYZ.y+XYZ.z)/threshold) - 1.68f );
  }

  // scale using empiric coefficient and fit inside limits
  V = clamp(c*V, 0.0f, 1.0f);

  // blending coefficient from curve
  w = lookup(lut, pixel.x/100.0f);

  XYZ = w * XYZ + (1.0f - w) * V * XYZ_sw;

  XYZ_to_Lab((float *)&XYZ, (float *)&pixel);

  write_imagef (out, (int2)(x, y), pixel);
}


