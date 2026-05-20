/*
===========================================================================
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.

This file is part of Spearmint Source Code.

Spearmint Source Code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 3 of the License,
or (at your option) any later version.

Spearmint Source Code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Spearmint Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, Spearmint Source Code is also subject to certain additional terms.
You should have received a copy of these additional terms immediately following
the terms and conditions of the GNU General Public License.  If not, please
request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional
terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc.,
Suite 120, Rockville, Maryland 20850 USA.
===========================================================================
*/

#ifdef DEDICATED
void *Sys_LoadLibrary( const char *f );
void Sys_UnloadLibrary( void *h );
void *Sys_LoadFunction( void *h, const char *fn );
const char *Sys_LibraryError( void );
#else
#	ifdef USE_LOCAL_HEADERS
#		include "SDL.h"
#		include "SDL_loadso.h"
#	else
#		include <SDL.h>
#		include <SDL_loadso.h>
#	endif
#	define Sys_LoadLibrary(f) SDL_LoadObject(f)
#	define Sys_UnloadLibrary(h) SDL_UnloadObject(h)
#	define Sys_LoadFunction(h,fn) SDL_LoadFunction(h,fn)
#	define Sys_LibraryError() SDL_GetError()
#endif

void * QDECL Sys_LoadDll(const char *name, qboolean useSystemLib);
