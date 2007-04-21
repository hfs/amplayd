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
#include <getopt.h>
#include <libdaemon/daemon.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "config.h"
#include "playlist.h"

enum {
	DAEMON_SUCCESS = 1
};

static gboolean RUNNING;

static void usage( const char* name ){
	g_printerr( "%s - Daemon to play movies on an ARCADEmini\n", AMPLAYD_NAME );
	g_printerr( "Usage: %s [OPTIONS]\n", name );
	g_printerr( "\n" );
	g_printerr( "Options:\n" );
	g_printerr( "    -u --user user      Drop privileges to the specified user.\n" );
	g_printerr( "    -d --device device  Device of the ARCADEmini (default: " AMPLAYD_DEVICE ")\n" );
	g_printerr( "    -s --spool-dir dir  Playlist directory (default: " AMPLAYD_PLAYLISTDIR ")\n" );
	g_printerr( "    -f --foreground     Debug mode (run in foreground).\n" );
	g_printerr( "    -h --help           Prints this help information.\n" );
	g_printerr( "    -V --version        Prints the daemons version.\n" );
	g_printerr( "\n" );
}

static void version(){
	g_printerr( "%s version %d.%d.%d\n", AMPLAYD_NAME, VERSION_MAJOR,
			VERSION_MINOR, VERSION_PATCH );
}

static void daemonize( char* name ){
	pid_t pid;

	/* process' name */
	daemon_pid_file_ident = daemon_log_ident = daemon_ident_from_argv0( name );
	daemon_log_use = DAEMON_LOG_AUTO;

	if(( pid = daemon_pid_file_is_running() ) >= 0 ){
		daemon_log( LOG_ERR, "%s is already running as PID %u.\n",
				AMPLAYD_NAME, pid );
		exit( EXIT_FAILURE );
	}

	daemon_retval_init();

	/* fork(), detach from stdin/out/err, set new group, set working dir */
	if(( pid = daemon_fork() ) < 0 ){
		daemon_log( LOG_ERR, "Fork failed.\n" );
		exit( EXIT_FAILURE );
	} else if ( pid ) { // parent
		if( daemon_retval_wait(1) < 0 ){
			g_printerr( "Daemon couldn't start.\n"
					"Please check syslog for error messages.\n" );
			exit( EXIT_FAILURE );
		}
		exit( EXIT_SUCCESS );
	} else { // daemon
		if ( daemon_pid_file_create() < 0 ) {
			daemon_log( LOG_ERR, "Could not create PID file (%s).\n",
					strerror( errno ));
			exit( EXIT_FAILURE );
		}
	}
}

void signal_handler( int sig ){
	switch( sig ){
		case SIGTERM:
		case SIGINT:
		case SIGQUIT:
			RUNNING = FALSE;
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
	g_return_val_if_fail( packet, FALSE );
	p_size = b_packet_size( packet );
	b_packet_hton( packet );

	for( list = movie->frames; list && RUNNING; list = list->next ){
		gsize bytes_written = 0;
		BMovieFrame *frame = list->data;
		memcpy( packet->data, frame->data, size );
		status = g_io_channel_write_chars( output, (guchar *) packet,
				p_size , &bytes_written, error );
		g_return_val_if_fail( status == G_IO_STATUS_NORMAL, FALSE );
		status = g_io_channel_flush( output, error );
		g_return_val_if_fail( status == G_IO_STATUS_NORMAL, FALSE );
		t.tv_sec = frame->duration/1000;
		t.tv_nsec = frame->duration%1000 * 1000000;
		nanosleep( &t, NULL );
	}

	g_free( packet );
	return TRUE;
}

void io_channel_open_wait( GIOChannel** channel, gchar* filename, GError** error ){
	gboolean channel_open_error = FALSE;
	if( *channel != NULL ){
		g_io_channel_shutdown( *channel, TRUE, error );
		g_io_channel_unref( *channel );
		*channel = NULL;
	}
	while( *channel == NULL ){
		*channel = g_io_channel_new_file( filename, "r+", error );
		if( *channel != NULL ){
			break;
		} else if( !( (*error)->code == G_FILE_ERROR_NOENT ||
					(*error)->code == G_FILE_ERROR_NXIO )){
			break;
		}
		/* Display error message only once */
		if( !channel_open_error ){
			daemon_log(LOG_ERR, "Error opening device '%s': %s\n"
					"Retrying...",
					filename, (*error)->message );
			channel_open_error = TRUE;
		}
		g_clear_error( error );
		sleep( 10 );
	}
	if( g_io_channel_set_encoding( *channel, NULL, error )
			!= G_IO_STATUS_NORMAL ){
		daemon_log(LOG_ERR, "Can't enable binary mode: %s\n",
				(*error)->message );
		g_io_channel_shutdown( *channel, TRUE, NULL );
		*channel = NULL;
	}
	return;
}

int main( int argc, char *argv[] ){
	GError *error = NULL;
	BMovie *movie = NULL;
	Playlist *playlist = NULL;
	gchar *device = NULL;
	gchar *spool_dir = NULL;
	const gchar *filename = NULL;
	gchar *filepath = NULL;
	GIOStatus status;
	int o = -1;
	struct passwd *user = NULL;
	gboolean daemonized = TRUE;
	RUNNING = TRUE;

	/* Command line options */
	static const struct option long_options[] = {
		{ "help",       no_argument,       NULL, 'h' },
		{ "version",    no_argument,       NULL, 'V' },
		{ "foreground", no_argument,       NULL, 'f' },
		{ "user",       required_argument, NULL, 'u' },
		{ "device",     required_argument, NULL, 'd' },
		{ "spool-dir",  required_argument, NULL, 's' },
		{ NULL, 0, NULL, 0}
	};

	while (( o = getopt_long( argc, argv, "d:fhs:u:V", long_options, NULL )) >= 0 ){
		switch (o) {
			case 'd':
				device = g_strdup( optarg );
				break;
			case 'f':
				daemonized = FALSE;
				break;
			case 'h':
				usage( argv[0] );
				exit( EXIT_SUCCESS );
				break;
			case 's':
				spool_dir = g_strdup( optarg );
				break;
			case 'u':
				errno = 0;
				if(( user = getpwnam(optarg)) == NULL ){
					g_printerr( "Could not find user '%s' %s\n",
							optarg,
							( errno == 0 ) ? "" : strerror( errno ) );
					exit( EXIT_FAILURE );
				}
				break;
			case 'V':
				version();
				exit( EXIT_SUCCESS );
				break;
			default:
				usage( argv[0] );
				exit( EXIT_FAILURE );
				break;
		}
	}

	if( !device ){
		device = AMPLAYD_DEVICE;
	}
	if( !spool_dir ){
		spool_dir = AMPLAYD_PLAYLISTDIR;
	}
	if( daemonized ){
		daemonize( argv[0] );
	}

	/* drop privileges */
	if( user != NULL ){
		if( chown( daemon_pid_file_proc(), user->pw_uid, user->pw_gid) != 0 ) {
			daemon_log( LOG_ERR,
					"Couldn't chown '%s' to '%s' uid=%d gid=%d\n",
					daemon_pid_file_proc(), user->pw_name,
					user->pw_uid, user->pw_gid );
		}
		if( setgid( user->pw_gid ) != 0 || setuid( user->pw_uid ) != 0 ){
			daemon_log( LOG_ERR, "Could not set uid '%i', gid '%i': %s\n",
					user->pw_uid, user->pw_gid, strerror( errno ));
			exit( EXIT_FAILURE );
		} else {
			daemon_log( LOG_INFO, "Dropping privileges to uid '%i' (%s)\n",
					user->pw_uid, user->pw_name );
		}
	}

	b_init();

	/* Open playlist and output device */
	GIOChannel *am_dev = NULL;
	io_channel_open_wait( &am_dev, device, &error );
	if( !am_dev ){
		daemon_log(LOG_ERR, "Error opening device '%s': %s\n",
				device, error->message );
		g_clear_error( &error );
		exit( EXIT_FAILURE );
	}
	playlist = playlist_new( spool_dir, &error );
	if( !playlist ){
		daemon_log(LOG_ERR, "Error loading playlist directory '%s': %s\n", 
				spool_dir, error->message );
		g_clear_error( &error );
		g_io_channel_shutdown( am_dev, TRUE, NULL );
		exit( EXIT_FAILURE );
	}

	signal( SIGPIPE, SIG_IGN );
	signal( SIGTERM, signal_handler );
	signal( SIGINT, signal_handler );
	signal( SIGQUIT, signal_handler );

	/* Ok, looks good */
	if( daemonized ){
		daemon_retval_send( DAEMON_SUCCESS );
	}

	/* Main Loop */
	gboolean empty_playlist_error = FALSE;
	while( RUNNING ){
		filename = playlist_next( playlist );
		if( !filename ){
			if( !empty_playlist_error ){
				daemon_log( LOG_ERR, "Playlist is empty. Please "
						"check %s\n", spool_dir );
				empty_playlist_error = TRUE;
			}
			sleep( 10 );
			continue;
		}
		else {
			empty_playlist_error = FALSE;
		}
		filepath = g_strconcat( spool_dir, "/", filename, NULL );
		movie = b_movie_new_from_file( filepath, FALSE, &error );
		if( !movie ){
			daemon_log( LOG_ERR, "Error loading '%s': %s\n",
					filepath, error->message );
			g_clear_error( &error );
			continue;
		}

		if( ! play_movie( am_dev, movie, &error )){
			if( error ){
				daemon_log( LOG_ERR, "Error playing '%s': %s\n",
						filepath, error->message );
				g_clear_error( &error );
			}
			/* Try to reopen the device */
			io_channel_open_wait( &am_dev, device, &error );
			if( !am_dev ){
				daemon_log(LOG_ERR, "Error opening device '%s': %s\n",
						device, error->message );
				g_clear_error( &error );
				exit( EXIT_FAILURE );
			}
		}
		g_object_unref( movie );
		g_free( filepath );
	}

	/* Cleanup */
	playlist_free( playlist );
	if( daemonized ){
		if (daemon_pid_file_remove() == -1) {
			daemon_log(LOG_ERR, "Could not remove pid file (%s).\n",
					strerror(errno));
		}
	}
	g_io_channel_shutdown( am_dev, TRUE, NULL );
	g_io_channel_unref( am_dev );
	return EXIT_SUCCESS;
}
