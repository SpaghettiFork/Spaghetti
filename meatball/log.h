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
#ifndef _MB_LOG_H
#define _MB_LOG_H

enum MBLogType
{
	MB_LOG_NONE,
	MB_LOG_ERROR,
	MB_LOG_WARNING,
	MB_LOG_INFO,
	MB_LOG_DEBUG
};

extern void MBLog(enum MBLogType, const char*, ...);

#endif // _MB_LOG_H
