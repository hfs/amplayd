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

#define AMPLAYD_NAME "amplayd"
#define AMPLAYD_DEVICE "/dev/am_usb"
#define AMPLAYD_PLAYLISTDIR "/var/spool/blinken"

enum {
	DAEMON_SUCCESS = 1
};

static void daemonize( char* name ){
	pid_t pid;

	/* process' name */
	daemon_pid_file_ident = daemon_log_ident = daemon_ident_from_argv0(name);
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
			daemon_log( LOG_ERR, "Could not create PID file (%s).\n", strerror(errno));
			exit( EXIT_FAILURE );
		}
	}
}

static void usage( const char* name ){
	g_printerr( "%s - Daemon to play movies on an ARCADEmini\n", AMPLAYD_NAME );
	g_printerr( "Usage: %s [OPTIONS]\n", name );
	g_printerr( "\n" );
	g_printerr( "Options:\n" );
	g_printerr( "    -u --user user  Drop privileges to the specified user.\n" );
	g_printerr( "    -d --no-daemon  Debug mode (non-daemon mode).\n" );
	g_printerr( "    -h --help       Prints this help information.\n" );
	g_printerr( "    -V --version    Prints the daemons version.\n" );
	g_printerr( "\n" );
}

static void version(){
	g_printerr( "%s version %d.%d.%d\n", AMPLAYD_NAME, VERSION_MAJOR,
			VERSION_MINOR, VERSION_PATCH );
}

void signal_handler(int sig) {
	switch( sig ){
		case SIGTERM:
		case SIGINT:
		case SIGQUIT:
			if (daemon_pid_file_remove() == -1) {
				daemon_log(LOG_ERR, "Could not remove pid file (%s).\n",
						strerror(errno));
			}
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
	int o;
	struct passwd * user = NULL;
	gboolean daemonized = TRUE;

	/* Command line options */
	static const struct option long_options[] = {
		{ "help",      no_argument,       NULL, 'h' },
		{ "version",   no_argument,       NULL, 'V' },
		{ "no-daemon", no_argument,       NULL, 'd' },
		{ "user",      required_argument, NULL, 'u' },
		{ NULL, 0, NULL, 0}
	};

	while (( o = getopt_long( argc, argv, "dhu:V", long_options, NULL )) >= 0 ){
		switch (o) {
			case 'd':
				daemonized = FALSE;
				break;
			case 'h':
				usage( argv[0] );
				exit( EXIT_SUCCESS );
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

	if( daemonized ){
		daemonize( argv[0] );
		signal( SIGPIPE, SIG_IGN );
		signal( SIGTERM, signal_handler );
		signal( SIGINT, signal_handler );
		signal( SIGQUIT, signal_handler );
	}

	/* drop privileges */
	if( user != NULL ){
		if( chown( daemon_pid_file_proc(), user->pw_uid, user->pw_gid) != 0 ) {
			daemon_log( LOG_ERR,
					"Couldn't chown '%s' to '%s' uid=%d gid=%d\n",
					daemon_pid_file_proc(), user->pw_name,
					user->pw_uid, user->pw_gid );
		}
		if( setuid( user->pw_uid ) != 0 || setgid( user->pw_gid ) != 0 ){
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
	playlist = playlist_new( AMPLAYD_PLAYLISTDIR, &error );
	if( !playlist ){
		daemon_log(LOG_ERR, "Error loading playlist directory '%s': %s\n", 
				AMPLAYD_PLAYLISTDIR, error->message );
		g_clear_error( &error );
		exit( EXIT_FAILURE );
	}
	GIOChannel *am_dev = g_io_channel_new_file( AMPLAYD_DEVICE, "r+", &error );
	if( !am_dev ){
		daemon_log(LOG_ERR, "Error opening device '%s': %s\n",
				AMPLAYD_DEVICE, error->message );
		g_clear_error( &error );
		playlist_free( playlist );
		exit( EXIT_FAILURE );
	}
	if( g_io_channel_set_encoding( am_dev, NULL, &error ) != G_IO_STATUS_NORMAL ){
		daemon_log(LOG_ERR, "Can't enable binary mode: %s\n", error->message );
		g_clear_error( &error );
		playlist_free( playlist );
		exit( EXIT_FAILURE );
	}

	/* Ok, looks good */
	if( daemonized ){
		daemon_retval_send( DAEMON_SUCCESS );
	}

	/* Main Loop */
	gboolean empty_playlist_error = FALSE;
	while( TRUE ){
		file = playlist_next( playlist );
		if( !file ){
			if( !empty_playlist_error ){
				daemon_log( LOG_ERR, "Playlist is empty. Please "
						"check " AMPLAYD_PLAYLISTDIR "\n" );
				empty_playlist_error = TRUE;
			}
			sleep( 10 );
			continue;
		}
		else {
			empty_playlist_error = FALSE;
		}
		file = g_strconcat( AMPLAYD_PLAYLISTDIR "/", file, NULL );
		movie = b_movie_new_from_file( file, FALSE, &error );
		if( !movie ){
			daemon_log(LOG_ERR, "Error loading '%s': %s\n",
					file, error->message );
			g_clear_error( &error );
			continue;
		}

		play_movie( am_dev, movie, &error );
		/* TODO Check return value
		 * Decide if this makes sense: Check if the device is gone and
		 * reopen if necessary.
		 */
	}
	exit( EXIT_FAILURE );
}
