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

#include <blib/blib.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "config.h"
#include "playlist.h"

#define AMPLAYD_DEVICE "/dev/am_usb"
#define AMPLAYD_PLAYLISTDIR "/var/spool/blinken"

static void usage( const char* name ){
	g_printerr( "amplayd - Daemon to play movies on an ARCADEmini\n" );
	g_printerr( "Version %d.%d.%d\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH );
	g_printerr( "Usage: %s <filename>\n", name );
	g_printerr( "\n" );
}

gboolean play_movie( GIOChannel *output, BMovie *movie, GError **error ){
	gsize size;
	gsize p_size;
	GList *list;
	GIOStatus status;
	struct timespec t;

	BPacket *packet = b_packet_new( movie->width, movie->height,
			movie->channels, movie->maxval, &size );
	p_size = b_packet_size( packet );
	g_return_val_if_fail( packet, FALSE );
	b_packet_hton( packet );

	for( list = movie->frames; list; list = list->next ){
		gsize bytes_written = 0;
		BMovieFrame *frame = list->data;
		memcpy( packet->data, frame->data, size );
		status = g_io_channel_write_chars( output, (guchar *) packet,
				p_size , &bytes_written, error );
		g_return_val_if_fail( status == G_IO_STATUS_NORMAL, FALSE );
		status = g_io_channel_flush( output, error );
		t.tv_sec = frame->duration/1000;
		t.tv_nsec = frame->duration%1000 * 1000000;
		nanosleep( &t, NULL );
	}

	return TRUE;
}

int main( int argc, char *argv[] ){
	BMovie *movie = NULL;
	GError *error = NULL;
	Playlist *playlist = NULL;
	const gchar *file = NULL;
	GIOStatus status;

	b_init();

	playlist = playlist_new( AMPLAYD_PLAYLISTDIR, &error );
	if( !playlist ){
		g_printerr( "Error loading playlist directory '"
				AMPLAYD_PLAYLISTDIR "': %s\n", error->message );
		g_clear_error( &error );
		return EXIT_FAILURE;
	}
	GIOChannel *am_dev = g_io_channel_new_file( AMPLAYD_DEVICE, "w", &error );
	if( !am_dev ){
		g_printerr( "Error opening device '" AMPLAYD_DEVICE "': %s\n",
				error->message );
		g_clear_error( &error );
		playlist_free( playlist );
		return EXIT_FAILURE;
	}
	if( g_io_channel_set_encoding( am_dev, NULL, &error ) != G_IO_STATUS_NORMAL ){
		g_printerr( "Can't enable binary mode? %s\n", error->message );
		g_clear_error( &error );
		playlist_free( playlist );
		return EXIT_FAILURE;
	}


	while( TRUE ){
		g_debug( "next: " );
		file = g_strconcat( AMPLAYD_PLAYLISTDIR "/", playlist_next( playlist ),
				NULL );
		g_debug( "%s\n", file );
		if( !file ){
			break;
		}
		movie = b_movie_new_from_file( file, FALSE, &error );
		if( !movie ){
			g_printerr( "Error loading '%s': %s\n", file, error->message );
			g_clear_error( &error );
			continue;
		}

		play_movie( am_dev, movie, &error );
	}
}
