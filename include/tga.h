/*
Copyright (C) 2025 Kevin Shanahan

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

#ifndef TGA_H
#define TGA_H

#include <stdint.h>

#include "qpic.h"

#ifdef GLQUAKE
qpic32_t *TGA_LoadHunkFile(const char *filename, const char *hunkname);
#endif

struct tga_hunkfile {
    void *data;
    uint32_t size;
};

struct tga_hunkfile TGA_CreateHunkFile8 (const uint8_t *data, int32_t width, int32_t height, int32_t stride);
struct tga_hunkfile TGA_CreateHunkFile24(const uint8_t *data, int32_t width, int32_t height, int32_t stride);

#endif // TGA_H
