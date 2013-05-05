/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) 2013 Kevin Shanahan

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <assert.h>
#include <stdint.h>

#include "textures.h"
#include "vid.h"
#include "zone.h"

/* --------------------------------------------------------------------------*/
/* Texture Format Transformations                                            */
/* --------------------------------------------------------------------------*/

qtexture32_t *
QTexture32_Alloc(int width, int height)
{
    const int memsize = offsetof(qtexture32_t, pixels[width * height]);
    qtexture32_t *texture = Hunk_Alloc(memsize);

    if (texture) {
	texture->width = width;
	texture->height = height;
    }

    return texture;
}

void
QTexture32_8to32(const byte *in, int width, int height, int stride,
		 qboolean alpha, qtexture32_t *out)
{
    qpixel32_t *pixel = out->pixels;
    int x, y;

    if (alpha) {
	/* index 0 is a transparent colour */
	for (y = 0; y < height; y++) {
	    for (x = 0; x < width; x++, in++, pixel++)
		pixel->rgba = (*in) ? d_8to24table[*in] : 0;
	    in += stride - width;
	}
    } else {
	for (y = 0; y < height; y++) {
	    for (x = 0; x < width; x++, in++, pixel++)
		pixel->rgba = d_8to24table[*in];
	    in += stride - width;
	}
    }
}

/*
================
QTexture32_Stretch
TODO - should probably be doing bilinear filtering or something
================
*/
void
QTexture32_Stretch(const qtexture32_t *in, qtexture32_t *out)
{
    int i, j;
    const qpixel32_t *inrow;
    qpixel32_t *outrow;
    unsigned frac, fracstep;

    assert(!(out->width & 3));

    fracstep = in->width * 0x10000 / out->width;
    outrow = out->pixels;
    for (i = 0; i < out->height; i++, outrow += out->width) {
	inrow = in->pixels + in->width * (i * in->height / out->height);
	frac = fracstep >> 1;
	for (j = 0; j < out->width; j += 4) {
	    outrow[j] = inrow[frac >> 16];
	    frac += fracstep;
	    outrow[j + 1] = inrow[frac >> 16];
	    frac += fracstep;
	    outrow[j + 2] = inrow[frac >> 16];
	    frac += fracstep;
	    outrow[j + 3] = inrow[frac >> 16];
	    frac += fracstep;
	}
    }
}

/* --------------------------------------------------------------------------*/
/* Mipmaps - Handle all variations of even/odd dimensions                    */
/* --------------------------------------------------------------------------*/

static void
QTexture32_MipMap_1D_Even(qpixel32_t *pixels, int length)
{
    const byte *in;
    byte *out;
    int i;

    in = out = (byte *)pixels;

    length >>= 1;
    for (i = 0; i < length; i++, out += 4, in += 8) {
	out[0] = ((int)in[0] + in[4]) >> 1;
	out[1] = ((int)in[1] + in[5]) >> 1;
	out[2] = ((int)in[2] + in[6]) >> 1;
	out[3] = ((int)in[3] + in[7]) >> 1;
    }
}

static void
QTexture32_MipMap_1D_Odd(qpixel32_t *pixels, int length)
{
    const int inlength = length;
    const byte *in;
    byte *out;
    int i;

    in = out = (byte *)pixels;

    length >>= 1;

    const float w1 = (float)inlength / length;
    for (i = 0; i < length; i++, out += 4, in += 8) {
	const float w0 = (float)(i - length) / inlength;
	const float w2 = (float)(i + 1) / inlength;

	out[0] = w0 * in[0] + w1 * in[4] + w2 * in[8];
	out[1] = w0 * in[1] + w1 * in[5] + w2 * in[9];
	out[2] = w0 * in[2] + w1 * in[6] + w2 * in[10];
	out[3] = w0 * in[3] + w1 * in[7] + w2 * in[11];
    }
}

/*
================
QTexture32_MipMap_EvenEven

Simple 2x2 box filter for textures with even width/height
================
*/
static void
QTexture32_MipMap_EvenEven(qpixel32_t *pixels, int width, int height)
{
    int i, j;
    byte *in, *out;

    in = out = (byte *)pixels;

    width <<= 2;
    height >>= 1;
    for (i = 0; i < height; i++, in += width) {
	for (j = 0; j < width; j += 8, out += 4, in += 8) {
	    out[0] = ((int)in[0] + in[4] + in[width + 0] + in[width + 4]) >> 2;
	    out[1] = ((int)in[1] + in[5] + in[width + 1] + in[width + 5]) >> 2;
	    out[2] = ((int)in[2] + in[6] + in[width + 2] + in[width + 6]) >> 2;
	    out[3] = ((int)in[3] + in[7] + in[width + 3] + in[width + 7]) >> 2;
	}
    }
}


/*
================
QTexture32_MipMap_OddOdd

With two odd dimensions we have a polyphase box filter in two
dimensions, taking weighted samples from a 3x3 square in the original
texture.
================
*/
static void
QTexture32_MipMap_OddOdd(qpixel32_t *pixels, int width, int height)
{
    const int inwidth = width;
    const int inheight = height;
    const byte *in;
    byte *out;
    int x, y;

    in = out = (byte *)pixels;

    width >>= 1;
    height >>= 1;

    /*
     * Take weighted samples from a 3x3 square on the original texture.
     * Weights for the centre pixel work out to be constant.
     */
    const float wy1 = (float)height / inheight;
    const float wx1 = (float)width / inwidth;

    for (y = 0; y < height; y++, in += inwidth << 2) {
	const float wy0 = (float)(height - y) / inheight;
	const float wy2 = (float)(1 + y) / inheight;

	for (x = 0; x < width; x ++, in += 8, out += 4) {
	    const float wx0 = (float)(width - x) / inwidth;
	    const float wx2 = (float)(1 + x) / inwidth;

	    /* Set up input row pointers to make things read easier below */
	    const byte *r0 = in;
	    const byte *r1 = in + (inwidth << 2);
	    const byte *r2 = in + (inwidth << 3);

	    out[0] =
		wx0 * wy0 * r0[0] + wx1 * wy0 * r0[4] + wx2 * wy0 * r0[8] +
		wx0 * wy1 * r1[0] + wx1 * wy1 * r1[4] + wx2 * wy1 * r1[8] +
		wx0 * wy2 * r2[0] + wx1 * wy2 * r2[4] + wx2 * wy2 * r2[8];
	    out[1] =
		wx0 * wy0 * r0[1] + wx1 * wy0 * r0[5] + wx2 * wy0 * r0[9] +
		wx0 * wy1 * r1[1] + wx1 * wy1 * r1[5] + wx2 * wy1 * r1[9] +
		wx0 * wy2 * r2[1] + wx1 * wy2 * r2[5] + wx2 * wy2 * r2[9];
	    out[2] =
		wx0 * wy0 * r0[2] + wx1 * wy0 * r0[6] + wx2 * wy0 * r0[10] +
		wx0 * wy1 * r1[2] + wx1 * wy1 * r1[6] + wx2 * wy1 * r1[10] +
		wx0 * wy2 * r2[2] + wx1 * wy2 * r2[6] + wx2 * wy2 * r2[10];
	    out[3] =
		wx0 * wy0 * r0[3] + wx1 * wy0 * r0[7] + wx2 * wy0 * r0[11] +
		wx0 * wy1 * r1[3] + wx1 * wy1 * r1[7] + wx2 * wy1 * r1[11] +
		wx0 * wy2 * r2[3] + wx1 * wy2 * r2[7] + wx2 * wy2 * r2[11];
	}
    }
}

/*
================
QTexture32_MipMap_OddEven

Handle odd width, even height
================
*/
static void
QTexture32_MipMap_OddEven(qpixel32_t *pixels, int width, int height)
{
    const int inwidth = width;
    const byte *in;
    byte *out;
    int x, y;

    in = out = (byte *)pixels;

    width >>= 1;
    height >>= 1;

    /*
     * Take weighted samples from a 3x2 square on the original texture.
     * Weights for the centre pixels are constant.
     */
    const float wx1 = (float)width / inwidth;
    for (y = 0; y < height; y++, in += inwidth << 2) {
	for (x = 0; x < width; x ++, in += 8, out += 4) {
	    const float wx0 = (float)(width - x) / inwidth;
	    const float wx2 = (float)(1 + x) / inwidth;

	    /* Set up input row pointers to make things read easier below */
	    const byte *r0 = in;
	    const byte *r1 = in + (inwidth << 2);

	    out[0] = 0.5 * (wx0 * r0[0] + wx1 * r0[4] + wx2 * r0[8] +
			    wx0 * r1[0] + wx1 * r1[4] + wx2 * r1[8]);
	    out[1] = 0.5 * (wx0 * r0[1] + wx1 * r0[5] + wx2 * r0[9] +
			    wx0 * r1[1] + wx1 * r1[5] + wx2 * r1[9]);
	    out[2] = 0.5 * (wx0 * r0[2] + wx1 * r0[6] + wx2 * r0[10] +
			    wx0 * r1[2] + wx1 * r1[6] + wx2 * r1[10]);
	    out[3] = 0.5 * (wx0 * r0[3] + wx1 * r0[7] + wx2 * r0[11] +
			    wx0 * r1[3] + wx1 * r1[7] + wx2 * r1[11]);
	}
    }
}

/*
================
QTexture32_MipMap_EvenOdd

Handle even width, odd height
================
*/
static void
QTexture32_MipMap_EvenOdd(qpixel32_t *pixels, int width, int height)
{
    const int inwidth = width;
    const int inheight = height;
    const byte *in;
    byte *out;
    int x, y;

    in = out = (byte *)pixels;

    width >>= 1;
    height >>= 1;

    /*
     * Take weighted samples from a 2x3 square on the original texture.
     * Weights for the centre pixels are constant.
     */
    const float wy1 = (float)height / inheight;
    for (y = 0; y < height; y++, in += inwidth << 2) {
	const float wy0 = (float)(height - y) / inheight;
	const float wy2 = (float)(1 + y) / inheight;

	for (x = 0; x < width; x++, in += 8, out += 4) {

	    /* Set up input row pointers to make things read easier below */
	    const byte *r0 = in;
	    const byte *r1 = in + (inwidth << 2);
	    const byte *r2 = in + (inwidth << 3);

	    out[0] = 0.5 * (wy0 * ((int)r0[0] + r0[4]) +
			    wy1 * ((int)r1[0] + r1[4]) +
			    wy2 * ((int)r2[0] + r2[4]));
	    out[1] = 0.5 * (wy0 * ((int)r0[1] + r0[5]) +
			    wy1 * ((int)r1[1] + r1[5]) +
			    wy2 * ((int)r2[1] + r2[5]));
	    out[2] = 0.5 * (wy0 * ((int)r0[2] + r0[6]) +
			    wy1 * ((int)r1[2] + r1[6]) +
			    wy2 * ((int)r2[2] + r2[6]));
	    out[3] = 0.5 * (wy0 * ((int)r0[3] + r0[7]) +
			    wy1 * ((int)r1[3] + r1[7]) +
			    wy2 * ((int)r2[3] + r2[7]));
	}
    }
}

/*
================
QTexture32_MipMap

Check texture dimensions and call the approriate specialized mipmap function
================
*/
void
QTexture32_MipMap(qtexture32_t *in)
{
    assert(in->width > 1 || in->height > 1);

    if (in->width == 1) {
	if (in->height & 1)
	    QTexture32_MipMap_1D_Odd(in->pixels, in->height);
	else
	    QTexture32_MipMap_1D_Even(in->pixels, in->height);

	in->height >>= 1;
	return;
    }

    if (in->height == 1) {
	if (in->width & 1)
	    QTexture32_MipMap_1D_Odd(in->pixels, in->width);
	else
	    QTexture32_MipMap_1D_Even(in->pixels, in->width);

	in->width >>= 1;
	return;
    }

    if (in->width & 1) {
	if (in->height & 1)
	    QTexture32_MipMap_OddOdd(in->pixels, in->width, in->height);
	else
	    QTexture32_MipMap_OddEven(in->pixels, in->width, in->height);
    } else if (in->height & 1) {
	QTexture32_MipMap_EvenOdd(in->pixels, in->width, in->height);
    } else {
	QTexture32_MipMap_EvenEven(in->pixels, in->width, in->height);
    }

    in->width >>= 1;
    in->height >>= 1;
}
