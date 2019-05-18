/*
Copyright (C) 2008 Chasseur de bots

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

#include "qcommon.h"
#include "sys_library.h"

/*
* Com_UnloadLibrary
*/
void Com_UnloadLibrary( void **lib )
{
	if( lib && *lib )
	{
		if( !Sys_Library_Close( *lib ) )
			Com_Error( ERR_FATAL, "Sys_CloseLibrary failed" );
		*lib = NULL;
	}
}

/*
* Com_LoadLibraryExt
*/
void *Com_LoadLibraryExt( const char *name, dllfunc_t *funcs, bool sys )
{
	void *lib;
	dllfunc_t *func;
	const char *fullname;

	if( !name || !name[0] || !funcs )
		return NULL;

	Com_DPrintf( "LoadLibrary (%s)\n", name );

	if( sys )
		fullname = name;
	else
		fullname = Sys_Library_GetFullName( name );
	if( !fullname )
	{
		Com_DPrintf( "LoadLibrary (%s):(Not found)\n", name );
		return NULL;
	}

	lib = Sys_Library_Open( fullname );
	if( !lib )
	{
		if( !sys )
			Com_Printf( "LoadLibrary (%s):(%s)\n", fullname, Sys_Library_ErrorString() );
		return NULL;
	}

	for( func = funcs; func->name; func++ )
	{
		*( func->funcPointer ) = Sys_Library_ProcAddress( lib, func->name );

		if( !( *( func->funcPointer ) ) )
		{
			Com_UnloadLibrary( &lib );
			if( sys )
				return NULL;
			Com_Error( ERR_FATAL, "%s: Sys_GetProcAddress failed for %s", fullname, func->name );
		}
	}

	return lib;
}

/*
* Com_LoadSysLibrary
*/
void *Com_LoadSysLibrary( const char *name, dllfunc_t *funcs )
{
	char *names;
	size_t names_size;
	char *s, *saveptr;
	void *lib = NULL;

	names_size = strlen( name ) + 1;
	names = Q_malloc( names_size );
	memcpy( names, name, names_size );

	s = strtok_r( names, "|", &saveptr );
	while( s != NULL ) {
		lib = Com_LoadLibraryExt( s, funcs, true );
		if( lib ) {
			Com_Printf( "Loaded %s\n", s );
			break;
		}
		s = strtok_r( NULL, "|", &saveptr );
	}

	free( names );
	return lib;
}

/*
* Com_LoadLibrary
*/
void *Com_LoadLibrary( const char *name, dllfunc_t *funcs )
{
	return Com_LoadLibraryExt( name, funcs, false );
}

/*
* Com_LibraryProcAddress
*/
void *Com_LibraryProcAddress( void *lib, const char *name )
{
	return Sys_Library_ProcAddress( lib, name );
}

//==============================================

typedef struct gamelib_s gamelib_t;

struct gamelib_s
{
	void *lib;
	char *fullname;
	gamelib_t *next;
};

static gamelib_t *gamelibs = NULL;

/*
* Com_UnloadGameLibrary
*/
void Com_UnloadGameLibrary( void **handle )
{
	gamelib_t *gamelib, *iter, *prev;

	assert( handle );

	if( !handle )
		return;

	gamelib = (gamelib_t *)*handle;
	if( !gamelib )
		return;

	// remove it from the linked list
	if( gamelib == gamelibs )
	{
		gamelibs = gamelib->next;
	}
	else
	{
		prev = NULL;
		iter = gamelibs;
		while( iter )
		{
			if( iter == gamelib )
			{
				assert( prev );
				prev->next = iter->next;
			}
			prev = iter;
			iter = iter->next;
		}
	}

	// close lib, if not statically linked
	if( gamelib->lib )
	{
		if( !Sys_Library_Close( gamelib->lib ) )
			Com_Error( ERR_FATAL, "Sys_CloseLibrary failed" );
		gamelib->lib = NULL;
	}

	// remove tempfile if it's not used by other instances
	if( gamelib->fullname )
	{
		iter = gamelibs;
		while( iter )
		{
			if( !strcmp( gamelib->fullname, iter->fullname ) )
				break;
			iter = iter->next;
		}

		if( !iter )
		{
			char *p;

			FS_RemoveAbsoluteFile( gamelib->fullname );
			p = strrchr( gamelib->fullname, '/' );
			if( p )
			{
				*p = '\0';
				FS_RemoveAbsoluteDirectory( gamelib->fullname );
			}
		}

		Mem_ZoneFree( gamelib->fullname );
	}

	Mem_ZoneFree( gamelib );

	*handle = NULL;
}

/*
* Com_LoadGameLibraryManifest
*
* Convert the { "key" "value" } notation into infostring
*/
static void Com_LoadGameLibraryManifest( const char *libname, char *manifest )
{
	const char *data;

	data = FS_FileManifest( libname );
	if( data )
	{
		char key[MAX_INFO_KEY], value[MAX_INFO_VALUE], *token;

		for( ; ( token = COM_Parse( &data ) ) && token[0] == '{'; )
		{
			while( 1 )
			{
				token = COM_Parse( &data );
				if( !token[0] )
					break; // error
				if( token[0] == '}' )
					break; // end of entity

				Q_strncpyz( key, token, sizeof( key ) );
				while( key[strlen( key )-1] == ' ' )  // remove trailing spaces
					key[strlen( key )-1] = 0;

				token = COM_Parse( &data );
				if( !token[0] )
					break; // error

				Q_strncpyz( value, token, sizeof( value ) );

				if( !Info_SetValueForKey( manifest, key, value ) )
				{
					Com_Printf( S_COLOR_YELLOW "Invalid manifest key/value pair: %s/%s\n", key, value );
					continue;
				}
			}
		}
	}
}

/*
* Com_LoadGameLibrary
*/
void *Com_LoadGameLibrary( const char *basename, const char *apifuncname, void **handle, void *parms, bool pure, char *manifest )
{
	static int randomizer = 0; // random part of tempmodules dir, always the same for one launch of Warsow
	static int64_t randomizer_time;
	const char *temppath;
	char *tempname, *libname;
	int libname_size;
	void *( *APIfunc )(void *);
	gamelib_t *gamelib;

	*handle = 0;

	if( !randomizer )
	{
		randomizer_time = time( NULL );
		srand( randomizer_time );
		randomizer = brandom( 1, 9999 );
	}

	gamelib = ( gamelib_t* )Mem_ZoneMalloc( sizeof( gamelib_t ) );
	gamelib->lib = NULL;
	gamelib->fullname = NULL;

	libname_size = strlen( LIB_PREFIX ) + strlen( basename ) + strlen( LIB_SUFFIX ) + 1;
	libname = ( char* )Mem_TempMalloc( libname_size );
	Q_snprintfz( libname, libname_size, LIB_PREFIX "%s" LIB_SUFFIX, basename );

	// it exists?
	if( FS_FOpenFile( libname, NULL, FS_READ ) == -1 )
	{
		Com_Printf( "LoadLibrary (%s):(File not found)\n", libname );
		Mem_TempFree( libname );
		Mem_ZoneFree( gamelib );
		return NULL;
	}

	temppath = Sys_Library_GetGameLibPath( libname, randomizer_time, randomizer );
	tempname = ( char * )Mem_ZoneMalloc( strlen( temppath ) + 1 );
	strcpy( tempname, temppath );

	if( FS_FOpenFile( tempname, NULL, FS_READ ) == -1 )
	{
		if( !FS_ExtractFile( libname, tempname ) )
		{
			Com_Printf( "LoadLibrary (%s):(FS_ExtractFile failed)\n", libname );
			Mem_TempFree( libname );
			Mem_ZoneFree( tempname );
			Mem_ZoneFree( gamelib );
			return NULL;
		}
	}

	gamelib->fullname = COM_SanitizeFilePath( tempname );
	gamelib->lib = Sys_Library_Open( gamelib->fullname );
	gamelib->next = gamelibs;
	gamelibs = gamelib;

	if( !( gamelib->lib ) )
	{
		Com_Printf( "LoadLibrary (%s):(%s)\n", tempname, Sys_Library_ErrorString() );
		Mem_TempFree( libname );
		Com_UnloadGameLibrary( (void **)&gamelib );
		return NULL;
	}

	APIfunc = ( void* ( * )( void* ) )Sys_Library_ProcAddress( gamelib->lib, apifuncname );
	if( !APIfunc )
	{
		Com_Printf( "LoadLibrary (%s):(%s)\n", tempname, Sys_Library_ErrorString() );
		Mem_TempFree( libname );
		Com_UnloadGameLibrary( (void **)&gamelib );
		return NULL;
	}

	*handle = gamelib;

	if( manifest )
		Com_LoadGameLibraryManifest( libname, manifest );

	Mem_TempFree( libname );

	return APIfunc( parms );
}
