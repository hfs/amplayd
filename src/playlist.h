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

/* Playlist
 *
 * A playlist is controlled by the files in one directory. It is divided into
 * three different qeues, in which entries are sorted depending on their
 * filename. All files must end in '.bml'.
 *
 * priority: All files in this queue are played first, but only once. Filenames
 * must begin with an underscore '_'.
 *
 * sorted: All files in this queue are played in alphanumerical order.
 * Filenames must begin with an digit (0--9).
 *
 * random: All other files are played at random.
 *
 * Priority files are served first. After that files from the sorted and random
 * queues are served intermixed and fairly distributed. After all files have
 * been played, the order of the random queue is reshuffled.
 */
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

/* playlist_new() creates a new Playlist or returns NULL in case of an error.
 * The playlist should be freed with playlist_free() after use.
 */
Playlist* playlist_new( const gchar* path, GError **error );

/* Returns the next file in the playlist or NULL if the playlist is empty. The
 * directory is checked for changes each time before a filename is returned,
 * and changes take immediate effect.
 */
const gchar* playlist_next();

/* Frees all memory allocated for the playlist and the file names.
 */
void playlist_free( Playlist* p );

#endif
