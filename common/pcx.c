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
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "common.h"
#include "pcx.h"
#include "qtypes.h"
#include "zone.h"

void
SwapPCX(pcx_t *pcx)
{
    pcx->xmax = LittleShort(pcx->xmax);
    pcx->ymax = LittleShort(pcx->ymax);
    pcx->hres = LittleShort(pcx->hres);
    pcx->vres = LittleShort(pcx->vres);
    pcx->bytes_per_line = LittleShort(pcx->bytes_per_line);
    pcx->palette_type = LittleShort(pcx->palette_type);
}


/*
==============
WritePCXfile
==============
*/
void
WritePCXfile(const char *filename, const byte *data, int width, int height,
	     int rowbytes, const byte *palette, qboolean upload)
{
    int i, j, length;
    pcx_t *pcx;
    byte *pack;

    pcx = Hunk_TempAlloc(width * height * 2 + 1000);

    pcx->manufacturer = 0x0a;	// PCX id
    pcx->version = 5;		// 256 color
    pcx->encoding = 1;		// uncompressed
    pcx->bits_per_pixel = 8;	// 256 color
    pcx->xmin = 0;
    pcx->ymin = 0;
    pcx->xmax = width - 1;
    pcx->ymax = height - 1;
    pcx->hres = width;
    pcx->vres = height;
    memset(pcx->palette, 0, sizeof(pcx->palette));
    pcx->color_planes = 1;	// chunky image
    pcx->bytes_per_line = width;
    pcx->palette_type = 1;	// not a grey scale
    memset(pcx->filler, 0, sizeof(pcx->filler));

    SwapPCX(pcx);

    // pack the image
    pack = &pcx->data;

#ifdef GLQUAKE
    // The GL buffer addressing is bottom to top?
    data += rowbytes * (height - 1);
    for (i = 0; i < height; i++) {
	for (j = 0; j < width; j++) {
	    if ((*data & 0xc0) != 0xc0) {
		*pack++ = *data++;
	    } else {
		*pack++ = 0xc1;
		*pack++ = *data++;
	    }
	}
	data += rowbytes - width;
	data -= rowbytes * 2;
    }
#else
    for (i = 0; i < height; i++) {
	for (j = 0; j < width; j++) {
	    if ((*data & 0xc0) != 0xc0) {
		*pack++ = *data++;
	    } else {
		*pack++ = 0xc1;
		*pack++ = *data++;
	    }
	}
	data += rowbytes - width;
    }
#endif

    // write the palette
    *pack++ = 0x0c;		// palette ID byte
    for (i = 0; i < 768; i++)
	*pack++ = *palette++;

    // write output file
    length = pack - (byte *)pcx;

#ifdef QW_HACK
    if (upload) {
	CL_StartUpload((byte *)pcx, length);
	return;
    }
#endif

    COM_WriteFile(filename, pcx, length);
}
