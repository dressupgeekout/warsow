#include <SDL.h>
#include "../client/client.h"

#if defined( _WIN32 )
#include "../win32/resource.h"
#endif

#if defined( __APPLE__ )
#include <CoreFoundation/CoreFoundation.h>
#include <sys/param.h>
#endif

unsigned sys_frame_time;

void Sys_InitTime( void );

void Sys_Sleep( unsigned int millis )
{
	SDL_Delay( millis );
}

void Sys_Error( const char *format, ... )
{
	va_list argptr;
	char msg[1024];

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, APPLICATION, msg, NULL );

	exit( 1 );
}

/*
* Sys_Init
*/
void Sys_Init( void )
{
	Sys_InitTime();
}

/*
* Sys_InitDynvars
*/
void Sys_InitDynvars( void )
{
}

/*
* Sys_Quit
*/
void Sys_Quit( void )
{
	Qcommon_Shutdown();

	exit( 0 );
}

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

char *Sys_ConsoleInput( void )
{
	return NULL;
}

void Sys_ConsoleOutput( char *string )
{
}

/*****************************************************************************/

int main( int argc, char **argv )
{
	unsigned int oldtime, newtime, time;

#if defined( __APPLE__ ) && !defined( DEDICATED_ONLY )
	char resourcesPath[MAXPATHLEN];
	CFURLGetFileSystemRepresentation( CFBundleCopyResourcesDirectoryURL( CFBundleGetMainBundle() ), 1, (UInt8 *)resourcesPath, MAXPATHLEN );
	chdir( resourcesPath );
#endif

#if defined( __WIN32__ )
#if defined( _DEBUG )
	SDL_SetHint( SDL_HINT_ALLOW_TOPMOST, "0" );
#endif
#endif

	SDL_Init( SDL_INIT_VIDEO );
	
	Qcommon_Init( argc, argv );

	oldtime = Sys_Milliseconds();
	while( true ) {
		// find time spent rendering last frame
		do {
			newtime = Sys_Milliseconds();
			time = newtime - oldtime;
			if( time > 0 )
				break;
			Sys_Sleep( 0 );
		} while( 1 );
		oldtime = newtime;

		Qcommon_Frame( time );
	}

	SDL_Quit();
}
