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

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#ifndef DEDICATED
#ifdef USE_LOCAL_HEADERS
#	include "SDL_version.h"
#else
#	include <SDL_version.h>
#endif

// Require a minimum version of SDL
#define MINSDL_MAJOR 2
#define MINSDL_MINOR 0
#if SDL_VERSION_ATLEAST( 2, 0, 5 )
#define MINSDL_PATCH 5
#elif SDL_VERSION_ATLEAST( 2, 0, 2 )
#define MINSDL_PATCH 2
#else
#define MINSDL_PATCH 0
#endif
#endif

// Console
void CON_Shutdown( void );
void CON_Init( void );
char *CON_Input( void );
void CON_Print( const char *message );

unsigned int CON_LogSize( void );
unsigned int CON_LogWrite( const char *in );
unsigned int CON_LogRead( char *out, unsigned int outSize );

#ifdef __APPLE__
char *Sys_StripAppBundle( char *pwd );
#endif

void Sys_GLimpSafeInit( void );
void Sys_GLimpInit( void );
void Sys_CommandLineInit( int *argcP, char ***argvP );
void Sys_PlatformInit( void );
void Sys_PlatformExit( void );
void Sys_SigHandler( int signal ) __attribute__ ((noreturn));
void Sys_ErrorDialog( const char *error );
void Sys_AnsiColorPrint( const char *msg );

int Sys_PID( void );
qboolean Sys_PIDIsRunning( int pid );

#ifdef PROTOCOL_HANDLER
char *Sys_ParseProtocolUri( const char *uri );
#endif
