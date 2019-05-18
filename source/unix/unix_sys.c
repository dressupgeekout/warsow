/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

/* we need __APPLE__ here because __MACOSX__ is defined in ../game/q_shared.h from ../qcommon/qcommon.h
which defines HAVE_STRCASECMP if SDL.h isn't called first, causing a bunch of warnings
FIXME:  This will be remidied once a native Mac port is complete
*/
#if defined ( __APPLE__ ) && !defined ( DEDICATED_ONLY )
#include <SDL.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <signal.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <errno.h>
#include <locale.h>

#include "../qcommon/qcommon.h"
#include "glob.h"

#if defined(DEDICATED_ONLY)

cvar_t *nostdout;
bool nostdout_backup_val = false;

unsigned sys_frame_time;

uid_t saved_euid;

// =======================================================================
// General routines
// =======================================================================

#ifndef DEDICATED_ONLY
extern void CL_Shutdown( void );
#endif

static void sigusr_handler( int sig )
{
	if( sig == SIGUSR1 )
	{
		Com_DeferConsoleLogReopen();
	}
	return;
}

static void signal_handler( int sig )
{
	static int try = 0;

	switch( try++ )
	{
	case 0:
		if( sig == SIGINT || sig == SIGTERM )
		{
			printf( "Received signal %d, exiting...\n", sig );
		}
		else
		{
			fprintf( stderr, "Received signal %d\n", sig );
		}
		Com_DeferQuit();
		break;
	case 1:
		printf( "Received signal %d, exiting...\n", sig );
		_exit( 1 );
		break;
	default:
		_exit( 2 );
		break;
	}
}

static void catchsig( int sig, void (*handler)(int) )
{
	struct sigaction new_action, old_action;
	new_action.sa_handler = handler;
	sigemptyset( &new_action.sa_mask );
	new_action.sa_flags = SA_RESTART;
	sigaction( sig, &new_action, &old_action );
}

static void InitSig( void )
{
	catchsig( SIGHUP, signal_handler );
	catchsig( SIGQUIT, signal_handler );
	catchsig( SIGILL, signal_handler );
	catchsig( SIGTRAP, signal_handler );
	catchsig( SIGIOT, signal_handler );
	catchsig( SIGBUS, signal_handler );
	catchsig( SIGFPE, signal_handler );
	catchsig( SIGSEGV, signal_handler );
	catchsig( SIGTERM, signal_handler );
	catchsig( SIGINT, signal_handler );
	catchsig( SIGPIPE, SIG_IGN );
	catchsig( SIGUSR1, sigusr_handler );
}

/*
* Sys_Quit
*/
void Sys_Quit( void )
{
	// Qcommon_Shutdown is going destroy the cvar, so backup its value now
	// and invalidate the pointer
	nostdout_backup_val = (nostdout && nostdout->integer ? true : false);
	nostdout = NULL;

	fcntl( 0, F_SETFL, fcntl( 0, F_GETFL, 0 ) & ~O_NONBLOCK );

	Qcommon_Shutdown();

	exit( 0 );
}

/*
* Sys_Init
*/
void Sys_Init( void )
{
}

/*
* Sys_InitDynvars
*/
void Sys_InitDynvars( void )
{
}

/*
* Sys_Error
*/
void Sys_Error( const char *format, ... )
{
	va_list argptr;
	char string[1024];

	// change stdin to non blocking
	fcntl( 0, F_SETFL, fcntl( 0, F_GETFL, 0 ) & ~O_NONBLOCK );

	va_start( argptr, format );
	Q_vsnprintfz( string, sizeof( string ), format, argptr );
	va_end( argptr );

	fprintf( stderr, "Error: %s\n", string );

	_exit( 1 );
}

/*
* Sys_Sleep
*/
void Sys_Sleep( unsigned int millis )
{
	usleep( millis * 1000 );
}

/*
* Sys_GetSymbol
*/
#ifdef SYS_SYMBOL
void *Sys_GetSymbol( const char *moduleName, const char *symbolName )
{
	// FIXME: Does not work on Debian64 for unknown reasons (dlsym always returns NULL)
	void *const module = dlopen( moduleName, RTLD_NOW );
	if( module )
	{
		void *const symbol = dlsym( module, symbolName );
		dlclose( module );
		return symbol;
	}
	else
		return NULL;
}
#endif // SYS_SYMBOL

//===============================================================================

/*
* Sys_AppActivate
*/
void Sys_AppActivate( void )
{
}

/*
* Sys_SendKeyEvents
*/
void Sys_SendKeyEvents( void )
{
	// grab frame time
	sys_frame_time = Sys_Milliseconds();
}

#endif // defined(DEDICATED_ONLY)

#ifndef __APPLE__
/*
* Sys_OpenURLInBrowser for Linux-based systems.
* OSX systems have their own function defined
* in mac_sys.m.
*/

bool Sys_IsBrowserAvailable( void )
{
	return true;
}

void Sys_OpenURLInBrowser( const char *url )
{
    int r;

    r = system( va( "xdg-open \"%s\"", url ) );
    if( r == 0 ) {
		// FIXME: XIconifyWindow does minimize the window, however
		// it seems that FocusIn even which follows grabs the input afterwards
		// XIconifyWindow( x11display.dpy, x11display.win, x11display.scr );
    }
}
#endif

/*
* Sys_GetCurrentProcessId
*/
int Sys_GetCurrentProcessId( void )
{
	return getpid();
}

/*
* Sys_GetPreferredLanguage
*/
const char *Sys_GetPreferredLanguage( void )
{
	static char lang[10];
	const char *locale;
	char *p;

	setlocale( LC_ALL, "" );
	locale = setlocale( LC_ALL, NULL );

	Q_strncpyz( lang, locale, sizeof( lang ) ); 

	setlocale( LC_ALL, "C" );

	p = strchr( lang, '.' );
	if( p ) { *p = '\0'; }

	if( !lang[0] ) {
		return APP_DEFAULT_LANGUAGE;
	}
	if( !Q_stricmp( lang, "C" ) ) {
		return APP_DEFAULT_LANGUAGE;
	}
	return Q_strlwr( lang );
}

#if defined(DEDICATED_ONLY)

/*
* Sys_AcquireWakeLock
*/
void *Sys_AcquireWakeLock( void )
{
	return NULL;
}

/*
* Sys_ReleaseWakeLock
*/
void Sys_ReleaseWakeLock( void *wl )
{
}

/*****************************************************************************/

int main( int argc, char **argv )
{
	unsigned int oldtime, newtime, time;

	InitSig();

#if defined ( __MACOSX__ ) && !defined (DEDICATED_ONLY)
	char resourcesPath[MAXPATHLEN];
	CFURLGetFileSystemRepresentation(CFBundleCopyResourcesDirectoryURL(CFBundleGetMainBundle()), 1, (UInt8 *)resourcesPath, MAXPATHLEN);
	chdir(resourcesPath);
	
	SDL_Init( SDL_INIT_VIDEO );
#endif

	Qcommon_Init( argc, argv );

	fcntl( 0, F_SETFL, fcntl( 0, F_GETFL, 0 ) | O_NONBLOCK );

	nostdout = Cvar_Get( "nostdout", "0", 0 );
	if( !nostdout->integer )
	{
		fcntl( 0, F_SETFL, fcntl( 0, F_GETFL, 0 ) | O_NONBLOCK );
	}

	oldtime = Sys_Milliseconds();
	while( true )
	{
		// find time spent rendering last frame
		do
		{
			newtime = Sys_Milliseconds();
			time = newtime - oldtime;
			if( time > 0 )
				break;
#ifdef PUTCPU2SLEEP
			Sys_Sleep( 0 );
#endif
		}
		while( 1 );
		oldtime = newtime;

		Qcommon_Frame( time );
	}
#if defined ( __MACOSX__ ) && !defined (DEDICATED_ONLY)
	SDL_Quit();
#endif
}

#endif // defined(DEDICATED_ONLY)
