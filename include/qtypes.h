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

#ifndef QTYPES_H
#define QTYPES_H

typedef unsigned char byte;

typedef int qboolean;
#undef true
#undef false
#if defined __STDC_VERSION__ && (__STDC_VERSION__ >= 199901L)
#include <stdbool.h>
#else
enum {
    false = 0,
    true  = 1
};
#endif

#ifndef offsetof
#define offsetof(type, member)  __builtin_offsetof (type, member)
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:        the pointer to the member.
 * @type:       the type of the container struct this is embedded in.
 * @member:     the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({                      \
	typeof( ((type *)0)->member ) *__mptr = (ptr);          \
        (type *)( (char *)__mptr - offsetof(type,member) );})

#define const_container_of(ptr, type, member) ({                \
	const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (const type *)( (const char *)__mptr - offsetof(type,member) );})

#endif /* QTYPES_H */
