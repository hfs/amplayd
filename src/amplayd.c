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
#include <errno.h>
#include <libdaemon/daemon.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "config.h"
#include "playlist.h"

#define AMPLAYD_NAME "amplayd"
#define AMPLAYD_DEVICE "/dev/am_usb"
#define AMPLAYD_PLAYLISTDIR "/var/spool/blinken"

static void daemonize( char* name ){
	pid_t pid;

	daemon_pid_file_ident = daemon_log_ident = daemon_ident_from_argv0(name);

	if ( ( pid = daemon_pid_file_is_running() ) >= 0 ) {
		daemon_log( LOG_ERR, "%s is already running as PID %u.\n", AMPLAYD_NAME, pid);
		exit( EXIT_FAILURE );
	}

	if ( ( pid = daemon_fork() ) < 0 ) {
		daemon_log( LOG_ERR, "Fork failed.\n");
		exit( EXIT_FAILURE );
	} else if ( pid ) { // parent
		exit( EXIT_SUCCESS );
	} else { // daemon
		if ( daemon_pid_file_create() < 0 ) {
			daemon_log( LOG_ERR, "Could not create PID file (%s).\n", strerror(errno));
			exit( EXIT_FAILURE );
		}
	}
}

static void usage( const char* name ){
	g_printerr( "%s - Daemon to play movies on an ARCADEmini\n", AMPLAYD_NAME );
	g_printerr( "Usage: %s [OPTIONS] <filename>\n", name );
	g_printerr( "\n" );
	g_printerr( "Options:\n" );
	g_printerr( "\t-d\t\tDebug mode (non-daemon mode).\n" );
	g_printerr( "\t-h\t\tPrints this help information.\n" );
	g_printerr( "\t-V\t\tPrints the daemons version.\n" );
	g_printerr( "\n" );
}

static void version(){
	g_printerr( "%s version %d.%d.%d\n", AMPLAYD_NAME, VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH );
}

void signal_handler(int sig) {
	switch( sig ){
		case SIGTERM:
		case SIGINT:
		case SIGQUIT:
			daemon_pid_file_remove();
			exit( EXIT_SUCCESS );
	}
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
	int c;
	char daemonized = 1;

	while ( ( c = getopt( argc, argv, "dhV" ) ) != -1 ) {
		switch (c) {
			case 'd':
				daemonized = 0;
				break;
			case 'h':
				usage( argv[0] );
				exit( EXIT_SUCCESS );
				break;
			case 'V':
				version();
				exit( EXIT_SUCCESS );
		}
	}

	if ( daemonized ) {
		daemonize( argv[0] );
		signal(SIGPIPE, SIG_IGN);
		signal(SIGTERM, signal_handler);
		signal(SIGINT, signal_handler);
		signal(SIGQUIT, signal_handler);
	}

	b_init();

	playlist = playlist_new( AMPLAYD_PLAYLISTDIR, &error );
	if( !playlist ){
		g_printerr( "Error loading playlist directory '%s': %s\n", AMPLAYD_PLAYLISTDIR, error->message );
		g_clear_error( &error );
		exit( EXIT_FAILURE );
	}
	GIOChannel *am_dev = g_io_channel_new_file( AMPLAYD_DEVICE, "w", &error );
	if( !am_dev ){
		g_printerr( "Error opening device '%s': %s\n",
        AMPLAYD_DEVICE,
				error->message );
		g_clear_error( &error );
		playlist_free( playlist );
	  exit( EXIT_FAILURE );
	}
	if( g_io_channel_set_encoding( am_dev, NULL, &error ) != G_IO_STATUS_NORMAL ){
		g_printerr( "Can't enable binary mode: %s\n", error->message );
		g_clear_error( &error );
		playlist_free( playlist );
		exit( EXIT_FAILURE );
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
	exit( EXIT_FAILURE );
}
