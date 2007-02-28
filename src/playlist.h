/*
 *  amplayd -- Daemon to play movies on an ARCADEmini
 *  Copyright (C) 2007  Hermann Schwarting <hfs@gmx.de>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA  02110-1301, USA
 */

#ifndef _PLAYLIST_H_
#define _PLAYLIST_H_


#include <glib.h>
#include "config.h"

typedef struct _Playlist {
	GPtrArray *files;
	GPtrArray *random;
	guint randomPos;
	GPtrArray *sorted;
	guint sortedPos;
	GPtrArray *prio;
	guint prioPos;
	GDir *dir;
} Playlist;

Playlist* playlist_new( const gchar* path, GError **error );
const gchar* playlist_next();
void playlist_free( Playlist* p );

#endif
