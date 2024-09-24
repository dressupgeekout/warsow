/*
Copyright (C) 2015 Victor Luchits

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
#include "compression.h"

static void *zLibrary;

/*
* ZLib_UnloadLibrary
*/
void ZLib_UnloadLibrary( void )
{
	zLibrary = NULL;
}

/*
* ZLib_LoadLibrary
*/
void ZLib_LoadLibrary( void )
{
	ZLib_UnloadLibrary();

	zLibrary = (void *)1;
}

/*
* Com_LoadCompressionLibraries
*/
void Com_LoadCompressionLibraries( void )
{
	ZLib_LoadLibrary();
}

/*
* Com_UnloadCompressionLibraries
*/
void Com_UnloadCompressionLibraries( void )
{
	ZLib_UnloadLibrary();
}
