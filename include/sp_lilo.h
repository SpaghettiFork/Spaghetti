/**
 * Spaghetti Display Server
 * Copyright (C) 2026  SpaghettiFork
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
#ifndef _SP_LILO_H_
#define _SP_LILO_H_

#include <misc.h>

typedef struct LILO
{
	void **data;
	size_t size;
	size_t capacity;
} LILO;

inline Bool
create_lilo(LILO* lilo, size_t capacity)
{
	if (lilo) {
		lilo->data = (void **)malloc(capacity * sizeof(void *));
		lilo->size = 0;
		lilo->capacity = capacity;
		return (lilo->data != NULL);
	} else {
		return FALSE;
	}
}

inline Bool
add_lilo(LILO *lilo, void *item)
{
	if (lilo->size < lilo->capacity)
	{
		lilo->data[lilo->size++] = item;
		return TRUE;
	} else {
		return FALSE;
	}
}

inline Bool
remove_lilo(LILO *lilo, void *item)
{
	for (size_t i = 0; i < lilo->size; i++)
	{
		if (lilo->data[i] == item)
		{
			for (size_t j = i; j < lilo->size - 1; j++)
			{
				lilo->data[j] = lilo->data[j + 1];
			}
			lilo->size--;
			return TRUE;
		}
	}
	return FALSE;
}

inline void
destroy_lilo(LILO* lilo)
{
	if (lilo && lilo->data) {
		free(lilo->data);
		lilo->data = NULL;
		lilo->size = 0;
		lilo->capacity = 0;
	}
}

inline Bool
contains_lilo(const LILO *lilo, void *item)
{
	for (size_t i = 0; i < lilo->size; i++)
	{
		if (lilo->data[i] == item) {
			return TRUE;
		}
	}

	return FALSE;
}

#define LILO_FOREACH_BACK_TO_FRONT(lilo, item) \
	for (size_t lilo_idx = (lilo)->size; lilo_idx; lilo_idx--) \
		if ((item = (lilo)->data[lilo_idx - 1]))

#define LILO_FOREACH_FRONT_TO_BACK(lilo, item) \
	for (size_t lilo_idx = 0; lilo_idx < (lilo)->size; lilo_idx++) \
		if ((item = (lilo)->data[lilo_idx]))

#endif // _SP_LILO_H_