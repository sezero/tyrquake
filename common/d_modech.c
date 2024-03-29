/*
Copyright (C) 1996-1997 Id Software, Inc.

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
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/
// d_modech.c: called when mode has just changed

#include "quakedef.h"
#include "d_local.h"
#include "render.h"
#include "sys.h"

int d_vrectx, d_vrecty, d_vrectright_particle, d_vrectbottom_particle;

int d_y_aspect_shift, d_pix_min, d_pix_max, d_pix_shift;

int d_scantable[MAXHEIGHT];
short *zspantable[MAXHEIGHT];

/*
================
D_ViewChanged
================
*/
void
D_ViewChanged(void)
{
    scale_for_mip = xscale;
    if (yscale > xscale)
	scale_for_mip = yscale;

    d_zrowbytes = vid.width * 2;
    d_zwidth = vid.width;

    d_pix_min = r_refdef.vrect.width * r_particle_scale.value / 320;
    if (d_pix_min < 1)
	d_pix_min = 1;

    d_pix_max = (int)((float)r_refdef.vrect.width / (320.0 / 4.0) * r_particle_scale.value + 0.5);
    d_pix_shift = 8 - (int)((float)r_refdef.vrect.width / 320.0 + 0.5);
    if (d_pix_max < 1)
	d_pix_max = 1;
    if (d_pix_shift < 0)
        d_pix_shift = 0;

    if (pixelAspect > 1.4)
	d_y_aspect_shift = 1;
    else
	d_y_aspect_shift = 0;

    d_vrectx = r_refdef.vrect.x;
    d_vrecty = r_refdef.vrect.y;
    d_vrectright_particle = r_refdef.vrectright - d_pix_max;
    d_vrectbottom_particle =
	r_refdef.vrectbottom - (d_pix_max << d_y_aspect_shift);

    {
	int i;

	for (i = 0; i < vid.height; i++) {
	    d_scantable[i] = i * vid.rowbytes;
	    zspantable[i] = d_pzbuffer + i * d_zwidth;
	}
    }
}
