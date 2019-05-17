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

#include <zlib.h>

#define qzcompress compress
#define qzcompress2 compress2
#define qzuncompress uncompress
#define qzinflateInit2 inflateInit2
#define qzinflate inflate
#define qzinflateEnd inflateEnd
#define qzinflateReset inflateReset
#define qgzopen gzopen
#define qgzseek gzseek
#define qgztell gztell
#define qgzread gzread
#define qgzwrite gzwrite
#define qgzclose gzclose
#define qgzeof gzeof
#define qgzflush gzflush
#define qgzsetparams gzsetparams
#define qgzbuffer gzbuffer

void Com_LoadCompressionLibraries( void );
void Com_UnloadCompressionLibraries( void );
