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

#include <string.h>
#include "playlist.h"

#define PLAYLIST_MOVIE_SUFFIX ".bml"
#define PRIO_QUEUE_PREFIX "_"

void ptr_array_randomize( GPtrArray *a, guint from, guint to ){
	gpointer swap;
	guint i, j;
	if( to >= a->len ){
		to = a->len - 1;
	}
	for( i = from; i <= to; ++i ){
		j = g_random_int_range( from, to+1 );
		swap = g_ptr_array_index( a, j );
		g_ptr_array_index( a, j ) = g_ptr_array_index( a, i );
		g_ptr_array_index( a, i ) = swap;
	}
}

/* Sort function for sorting GPtrArrays of strings */
static gint strcmp2(gconstpointer a, gconstpointer b)
{
	const char *aa = *(char **) a;
	const char *bb = *(char **) b;

	return strcmp(aa, bb);
}

void playlist_reorder( Playlist *p ){
	if( p->random->len > 0 ){
		ptr_array_randomize( p->random, p->randomPos, p->random->len-1 );
	}
	g_ptr_array_sort( p->sorted, strcmp2 );
}

GPtrArray* choose_queue( Playlist *p, const gchar *file ){
	if( g_ascii_isdigit( file[0] )){
		return p->sorted;
	}
	if( g_str_has_prefix( file, PRIO_QUEUE_PREFIX )){
		return p->prio;
	}
	return p->random;
}

void playlist_add( Playlist *p, const gchar *file ){
	GPtrArray *queue = choose_queue( p, file );
	g_ptr_array_add( queue, g_strdup( file ));
}

void playlist_remove( Playlist *p, const gchar *file ){
	guint i;
	GPtrArray *queue = choose_queue( p, file );
	for( i = 0; i < queue->len; ++i ){
		if( strcmp( file, g_ptr_array_index( queue, i )) == 0 ){
			g_free( g_ptr_array_index( queue, i ));
			g_ptr_array_remove_index( queue, i );
		}
	}
}

void string_array_free( GPtrArray *p ){
	guint i;
	for( i = 0; i < p->len; ++i ){
		g_free( g_ptr_array_index( p, i ));
	}
	g_ptr_array_free( p, TRUE );
}

void playlist_update( Playlist *p ){
	const gchar *file;
	gboolean playlist_changed = FALSE;
	GPtrArray *contents = g_ptr_array_new();

	/* Read current directory content */
	g_dir_rewind( p->dir );
	while( file = g_dir_read_name( p->dir ) ){
		if( g_str_has_suffix( file, PLAYLIST_MOVIE_SUFFIX ) == TRUE ) {
			g_ptr_array_add( contents, g_strdup( file ) );
		}
	}
	g_ptr_array_sort( contents, strcmp2 );

	/* Compare directory content with last time */
	if( p->files->len == 0 && contents->len > 0 ){
		string_array_free( p->files );
		p->files = contents;
		guint i;
		for( i = 0; i < contents->len; ++i ){
			playlist_add( p, g_ptr_array_index( p->files, i ));
		}
		playlist_changed = TRUE;
	} else if( contents->len == 0 && p->files->len > 0 ){
		guint i;
		for( i = 0; i < p->files->len; ++i ){
			playlist_remove( p, g_ptr_array_index( p->files, i ));
		}
		string_array_free( p->files );
		p->files = contents;
		playlist_changed = TRUE;
	} else if( contents->len == 0 && p->files->len == 0 ){
	} else {
		/* Put the last known directory content (p->files) next to the
		 * current state (contents) in two alphanumerically sorted
		 * columns. Run both down from top to bottom.
		 */
		guint c = 0;
		guint f = 0;
		int cmp;
		while( c < contents->len || f < p->files->len ) {
			if( c >= contents->len ){
				cmp = 1;
			} else if( f >= p->files->len ){
				cmp = -1;
			} else {
				cmp = strcmp( g_ptr_array_index( contents, c ),
						g_ptr_array_index( p->files, f ));
			}
			if( cmp < 0 ){
				playlist_add( p, g_ptr_array_index( contents, c ));
				++c;
				playlist_changed = TRUE;
			} else if( cmp > 0 ){
				playlist_remove( p, g_ptr_array_index( p->files, f ));
				++f;
				playlist_changed = TRUE;
			} else {
				++c;
				++f;
			}
		}
		string_array_free( p->files );
		p->files = contents;
	}
	if( playlist_changed ){
		playlist_reorder( p );
	}
}

Playlist* playlist_new( const gchar *path, GError **error ) {
	Playlist *p = g_slice_new( Playlist );

	p->dir = g_dir_open( path, 0, error );
	if( !p->dir ){
		playlist_free( p );
		return NULL;
	}

	p->files = g_ptr_array_new();
	p->random = g_ptr_array_new();
	p->sorted = g_ptr_array_new();
	p->prio = g_ptr_array_new();
	p->randomPos = p->sortedPos = p->prioPos = 0;

	playlist_update( p );

	return p;
}

void playlist_free( Playlist *p ){
	if( p->dir ){
		g_dir_close( p->dir );
	}
	string_array_free( p->files );
	string_array_free( p->random );
	string_array_free( p->sorted );
	string_array_free( p->prio );
	g_slice_free( Playlist, p );
	p = NULL;
}

const gchar* playlist_next( Playlist *p ){
	playlist_update( p );

	/* Play prio queue first */
	if( p->prioPos < p->prio->len ){
		++p->prioPos;
		return g_ptr_array_index( p->prio, p->prioPos-1 );
	}
	/* Empty prio queue when played once */
	if( p->prio->len > 0 && p->prioPos >= p->prio->len ){
		string_array_free( p->prio );
		p->prio = g_ptr_array_new();
		p->prioPos = 0;
	}

	/* Anything to play at all? */
	if( p->prio->len <= 0 && p->sorted->len <= 0 && p->random->len <= 0 ){
		return NULL;
	}

	/* All queues played -> reorder */
	if( p->sortedPos >= p->sorted->len &&
			p->randomPos >= p->random->len ){
		p->randomPos = 0;
		p->sortedPos = 0;
		playlist_reorder( p );
	}

	/*  Play sorted and random queues fairly */
	if( p->sortedPos < p->sorted->len && ( p->random->len <= 0 ||
			(p->sortedPos+1) * p->random->len <
			(p->randomPos+1) * p->sorted->len )
	){
		++p->sortedPos;
		return g_ptr_array_index( p->sorted, p->sortedPos-1 );
	}
	if( p->randomPos < p->random->len ){
		++p->randomPos;
		return g_ptr_array_index( p->random, p->randomPos-1 );
	}
}

