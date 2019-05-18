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
// sys_win.h

#include "../qcommon/qcommon.h"
#include "winquake.h"
#include "resource.h"
#include <errno.h>
#include <float.h>
#include <fcntl.h>
#include <stdio.h>
#include <io.h>
#include <conio.h>
#include <limits.h>

#include "../win32/conproc.h"

#if !defined(DEDICATED_ONLY)
QF_DLL_EXPORT DWORD NvOptimusEnablement = 0x00000001;
QF_DLL_EXPORT int AmdPowerXpressRequestHighPerformance = 1;
#endif

#if defined(DEDICATED_ONLY)

int starttime;
int ActiveApp;
int Minimized;
int AppFocused;

HANDLE hinput, houtput;

unsigned sys_msg_time;
unsigned sys_frame_time;

#define	MAX_NUM_ARGVS	128
int argc;
char *argv[MAX_NUM_ARGVS];

// dynvar forward declarations
static dynvar_get_status_t Sys_GetAffinity_f( void **affinity );
static dynvar_set_status_t Sys_SetAffinity_f( void *affinity );

void Sys_InitTimeDynvar( void );
void Sys_InitTime( void );

void Sys_InitThreads( void );

/*
===============================================================================

SYSTEM IO

===============================================================================
*/

void Sys_Error( const char *format, ... )
{
	va_list	argptr;
	char msg[1024];

	va_start( argptr, format );
	Q_vsnprintfz( msg, sizeof( msg ), format, argptr );
	va_end( argptr );

	MessageBox( NULL, msg, "Error", 0 /* MB_OK */ );

	// shut down QHOST hooks if necessary
	DeinitConProc();

	exit( 1 );
}

void Sys_Quit( void )
{
	timeEndPeriod( 1 );

	SV_Shutdown( "Server quit\n" );
	CL_Shutdown();

	if( dedicated && dedicated->integer )
		FreeConsole();

	// shut down QHOST hooks if necessary
	DeinitConProc();

	Qcommon_Shutdown();

	exit( 0 );
}

//================================================================

void Sys_Sleep( unsigned int millis )
{
	Sleep( millis );
}

//===============================================================================

/*
* Sys_Init
*/
void Sys_Init( void )
{
	timeBeginPeriod( 1 );

	Sys_InitTime();

	Sys_InitThreads();

	if( dedicated->integer )
	{
		SetPriorityClass( GetCurrentProcess(), HIGH_PRIORITY_CLASS );

		if( !AllocConsole() )
			Sys_Error( "Couldn't create dedicated server console" );
		hinput = GetStdHandle( STD_INPUT_HANDLE );
		houtput = GetStdHandle( STD_OUTPUT_HANDLE );

		// let QHOST hook in
		InitConProc( argc, argv );
	}
}

/*
* Sys_InitDynvars
*/
void Sys_InitDynvars( void )
{
	char *dummyStr;
	dynvar_t *affinity_var;

	affinity_var = Dynvar_Create( "sys_affinity", true, Sys_GetAffinity_f, Sys_SetAffinity_f );
	assert( affinity_var );
	Dynvar_GetValue( affinity_var, (void **)&dummyStr );
	assert( dummyStr );
	Dynvar_SetValue( affinity_var, dummyStr );

	Sys_InitTimeDynvar();
}

/*
* myTranslateMessage
* A wrapper around TranslateMessage to avoid garbage if the toggleconsole
* key happens to be a dead key (like in the German layout)
*/
#ifdef DEDICATED_ONLY
#define myTranslateMessage(msg) TranslateMessage(msg)
#else
int IN_MapKey( int key );
bool Key_IsNonPrintable( int key );
static BOOL myTranslateMessage (MSG *msg)
{
	if (msg->message == WM_KEYDOWN) {
		if (Key_IsNonPrintable(IN_MapKey(msg->lParam)))
			return TRUE;
		else
			return TranslateMessage(msg);
	}
	return TranslateMessage(msg);
}
#endif

/*
* Sys_SendKeyEvents
* 
* Send Key_Event calls
*/
void Sys_SendKeyEvents( void )
{
	MSG msg;

	while( PeekMessageW( &msg, NULL, 0, 0, PM_NOREMOVE ) )
	{
		if( !GetMessageW( &msg, NULL, 0, 0 ) )
			Sys_Quit();
		sys_msg_time = msg.time;
		myTranslateMessage( &msg );
		DispatchMessageW( &msg );
	}

	// grab frame time
	sys_frame_time = timeGetTime(); // FIXME: should this be at start?
}

#endif // defined(DEDICATED_ONLY)

/*
* Sys_IsBrowserAvailable
*/
bool Sys_IsBrowserAvailable( void )
{
	return true;
}

/*
* Sys_OpenURLInBrowser
*/
void Sys_OpenURLInBrowser( const char *url )
{
	ShellExecute( NULL, "open", url, NULL, NULL, SW_SHOWNORMAL );
}

/*
* Sys_GetCurrentProcessId
*/
int Sys_GetCurrentProcessId( void )
{
	return GetCurrentProcessId();
}

/*
* Sys_GetPreferredLanguage
* Get the preferred language through the MUI API. Works on Vista and newer.
*/
const char *Sys_GetPreferredLanguage( void )
{
	typedef BOOL (WINAPI *GetUserPreferredUILanguages_t)(DWORD, PULONG, PWSTR, PULONG);
	BOOL hr;
	ULONG numLanguages = 0;
	DWORD cchLanguagesBuffer = 0;
	HINSTANCE kernel32Dll;
	GetUserPreferredUILanguages_t GetUserPreferredUILanguages_f;
	static char lang[10];

// mingw doesn't define this
#ifndef MUI_LANGUAGE_NAME
# define MUI_LANGUAGE_NAME 0x8
#endif

	lang[0] = '\0';

	kernel32Dll = LoadLibrary( "kernel32.dll" );

	hr = FALSE;
	GetUserPreferredUILanguages_f = (void *)GetProcAddress( kernel32Dll, "GetUserPreferredUILanguages" );
	if( GetUserPreferredUILanguages_f ) {
		hr = GetUserPreferredUILanguages_f( MUI_LANGUAGE_NAME, &numLanguages, NULL, &cchLanguagesBuffer );
	}

	if( hr ) {
		WCHAR *pwszLanguagesBuffer;
		
		pwszLanguagesBuffer = Q_malloc( sizeof( WCHAR ) * cchLanguagesBuffer );
		hr = GetUserPreferredUILanguages_f( MUI_LANGUAGE_NAME, &numLanguages, pwszLanguagesBuffer, &cchLanguagesBuffer );

		if( hr ) {
			char *p;

			WideCharToMultiByte( CP_ACP, 0, pwszLanguagesBuffer, cchLanguagesBuffer, lang, sizeof(lang), NULL, NULL );
			lang[sizeof(lang)-1] = '\0';

			p = strchr( lang, '-' );
			if( p ) { *p = '_'; }
		}

		Q_free( pwszLanguagesBuffer );	
	}

	FreeLibrary( kernel32Dll );

	if( !lang[0] ) {
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

/*
* Sys_AppActivate
*/
void Sys_AppActivate( void )
{
#ifndef DEDICATED_ONLY
	ShowWindow( cl_hwnd, SW_RESTORE );
	SetForegroundWindow( cl_hwnd );
#endif
}

//========================================================================

/*
* ParseCommandLine
*/
static void ParseCommandLine( LPSTR lpCmdLine )
{
	argc = 1;
	argv[0] = "exe";

	while( *lpCmdLine && ( argc < MAX_NUM_ARGVS ) )
	{
		while( *lpCmdLine && ( *lpCmdLine <= 32 || *lpCmdLine > 126 ) )
			lpCmdLine++;

		if( *lpCmdLine )
		{
			char quote = ( ( '"' == *lpCmdLine || '\'' == *lpCmdLine ) ? *lpCmdLine++ : 0 );

			argv[argc++] = lpCmdLine;
			if( quote )
			{
				while( *lpCmdLine && *lpCmdLine != quote && *lpCmdLine >= 32 && *lpCmdLine <= 126 )
					lpCmdLine++;
			}
			else
			{
				while( *lpCmdLine && *lpCmdLine > 32 && *lpCmdLine <= 126 )
					lpCmdLine++;
			}

			if( *lpCmdLine )
				*lpCmdLine++ = 0;
		}
	}
}

static dynvar_get_status_t Sys_GetAffinity_f( void **affinity )
{
	static bool affinityAutoSet = false;
	static char affinityString[33];
	DWORD_PTR procAffinity, sysAffinity;
	HANDLE proc = GetCurrentProcess();

	if( GetProcessAffinityMask( proc, &procAffinity, &sysAffinity ) )
	{
		SYSTEM_INFO sysInfo;
		DWORD i;

		CloseHandle( proc );

		assert( affinity );

		GetSystemInfo( &sysInfo );
		for( i = 0; i < sysInfo.dwNumberOfProcessors && i < 33; ++i )
		{
			affinityString[i] = '0' + ( ( procAffinity & sysAffinity ) & 1 );
			procAffinity >>= 1;
			sysAffinity >>= 1;
		}
		affinityString[i] = '\0';

		if( !affinityAutoSet )
		{
#if 0
			// set the affinity string to something like 0001
			const char *lastBit = strrchr( affinityString, '1' );
			if( lastBit )
			{   // Vic: FIXME??
				for( i = 0; i < (DWORD)( lastBit - affinityString ); i++ )
					affinityString[i] = '0';
			}
#endif
			affinityAutoSet = true;
		}

		*affinity = affinityString;
		return DYNVAR_GET_OK;
	}

	CloseHandle( proc );
	*affinity = NULL;
	return DYNVAR_GET_TRANSIENT;
}

static dynvar_set_status_t Sys_SetAffinity_f( void *affinity )
{
	dynvar_set_status_t result = DYNVAR_SET_INVALID;
	SYSTEM_INFO sysInfo;
	DWORD_PTR procAffinity = 0, i;
	HANDLE proc = GetCurrentProcess();
	char minValid[33], maxValid[33];
	const size_t len = strlen( (char *) affinity );

	// create range of valid values for error printing
	GetSystemInfo( &sysInfo );
	for( i = 0; i < sysInfo.dwNumberOfProcessors; ++i )
	{
		minValid[i] = '0';
		maxValid[i] = '1';
	}
	minValid[i] = '\0';
	maxValid[i] = '\0';

	if( len == sysInfo.dwNumberOfProcessors )
	{
		// string is of valid length, parse in reverse direction
		const char *c;
		for( c = ( (char *) affinity ) + len - 1; c >= (char *) affinity; --c )
		{
			// parse binary digit
			procAffinity <<= 1;
			switch( *c )
			{
			case '0':
				// nothing to do
				break;
			case '1':
				// at least one digit must be 1
				result = DYNVAR_SET_OK;
				procAffinity |= 1;
				break;
			default:
				// invalid character found
				result = DYNVAR_SET_INVALID;
				goto abort;
			}
		}

		SetProcessAffinityMask( proc, procAffinity );
		//if (len > 1)
		//SetPriorityClass(proc, HIGH_PRIORITY_CLASS);
	}

abort:
	if( result != DYNVAR_SET_OK )
		Com_Printf( "\"sys_affinity\" must be a non-zero bitmask between \"%s\" and \"%s\".\n", minValid, maxValid );

	CloseHandle( proc );
	return result;
}

/*
* WinMain
*/
HINSTANCE global_hInstance;
int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow )
{
	MSG msg;
	unsigned int oldtime, newtime, time;

	/* previous instances do not exist in Win32 */
	if( hPrevInstance )
		return 0;

	global_hInstance = hInstance;

	ParseCommandLine( lpCmdLine );

	Qcommon_Init( argc, argv );

	oldtime = Sys_Milliseconds();

	/* main window message loop */
	while( 1 )
	{
		// if at a full screen console, don't update unless needed
		if( Minimized || ( dedicated && dedicated->integer ) )
		{
			Sleep( 1 );
		}

		while( PeekMessageW( &msg, NULL, 0, 0, PM_NOREMOVE ) )
		{
			if( !GetMessageW( &msg, NULL, 0, 0 ) )
				Com_Quit();
			sys_msg_time = msg.time;
			myTranslateMessage( &msg );
			DispatchMessageW( &msg );
		}

		do
		{
			newtime = Sys_Milliseconds();
			time = newtime - oldtime; // no warp problem as unsigned
			if( time > 0 )
				break;
			Sys_Sleep( 0 );
		}
		while( 1 );
		//Com_Printf ("time:%5.2u - %5.2u = %5.2u\n", newtime, oldtime, time);
		oldtime = newtime;

		// do as q3 (use the default floating point precision)
		//	_controlfp( ~( _EM_ZERODIVIDE /*| _EM_INVALID*/ ), _MCW_EM );
		//_controlfp( _PC_24, _MCW_PC );
		Qcommon_Frame( time );
	}

	// never gets here
	return TRUE;
}

#endif // defined(DEDICATED_ONLY)
