/**
 * Spaghetti Display Server
 * Copyright (C) 2025  SpaghettiFork
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _MINI_VEC_H_
#define _MINI_VEC_H_

#include <stddef.h>
#include <stdlib.h>

typedef struct
{
	int *array;
	size_t used;
	size_t size;
	size_t obj_size;
} mini_vector;

inline void mini_vector_init(mini_vector *a, size_t obj_size, size_t initialSize)
{
	a->array = malloc(initialSize * obj_size);
	a->used = 0;
	a->size = initialSize;
	a->obj_size = obj_size;
}

inline void insert_mini_vector(mini_vector *a, int element)
{
	if (a->used == a->size)
	{
		a->size *= 2;
		a->array = realloc(a->array, a->size * a->obj_size);
	}

	a->array[a->used++] = element;
}

inline void free_mini_vector(mini_vector *a)
{
	free(a->array);
	a->array = NULL;
	a->used = a->size = 0;
}

#endif // _MINI_VEC_H_
