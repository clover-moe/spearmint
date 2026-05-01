/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// cl_cgame.c  -- client system interaction with client game

#include "client.h"

#include "../botlib/botlib.h"

#ifdef USE_MUMBLE
#include "libmumblelink.h"
#endif

extern	botlib_export_t	*botlib_export;

extern qboolean loadCamera(const char *name);
extern void startCamera(int time);
extern qboolean getCameraInfo(int time, vec3_t *origin, vec3_t *angles);

#ifdef USE_FLEXIBLE_DISPLAY
static float cl_lastx, cl_lasty, cl_lastw, cl_lasth;
static float cl_originx, cl_originy, cl_originw, cl_originh;
static qboolean cl_lastUpperRight;
static qboolean cl_enteredScoreboard;
static qboolean cl_enteredStatusBar;
static qboolean cl_enteredLowerLeft;
static qboolean cl_ignoreLowerLeft;
static qboolean cl_drewLagometer;
#endif

/*
====================
CL_GetGameState
====================
*/
void CL_GetGameState( gameState_t *gs ) {
	*gs = cl.gameState;
}

/*
====================
CL_GetGlconfig
====================
*/
void CL_GetGlconfig( glconfig_t *glconfig ) {
	*glconfig = cls.glconfig;

#ifdef USE_FLEXIBLE_DISPLAY
	if ( cl_flexibleDisplay->integer ) {
		glconfig->vidWidth = 640;
		glconfig->vidHeight = 480;
	}
#endif
}

#ifdef USE_FLEXIBLE_DISPLAY
/*
====================
CL_AdjustFromCGame

This is only called if cl_flexibleDisplay is enabled.
====================
*/
void CL_AdjustFromCGame( float *x, float *y, float *w, float *h ) {
	if ( *y == SCREEN_HEIGHT - 48 && *x == SCREEN_WIDTH - 48 && *w == 48 && *h == 48 ) {
		// always treat osp lagometer as new draw (should be fine for baseq3 as well)
		cl_drewLagometer = qtrue;
		cl_originx = *x;
		cl_originy = *y;
	}
	// detect drawing something connected to the previous draw
	// so that text string, etc are not split apart
	else if ( *y == cl_lasty && *x >= cl_lastx ) {
		// detects Q3 text
		// detects Q3 scoreboard headers
	} else if ( *y + *h > cl_lasty && *y < cl_lasty + cl_lasth && *x >= cl_lastx - 2 && *x <= cl_lastx + cl_lastw + 32 ) {
		// detects Team Arena text
		// (this would also detect Q3 text)
	} else if (  // *y >= 380 - 22 &&
		*y + *h >= cl_originy && *y < cl_originy + cl_originh
		&& *x + *w >= cl_originx && *x < cl_originx + cl_originw ) {
		// detects differing y / size for drawing weapon select marker (for scroll wheel) after weapon icons.
		// detects same y for cg_drawTeamOverlay 2 and 3 drawing weapon icon after drawing text before and after it.
	} else {
		// not part of previous draw
		cl_originx = *x;
		cl_originy = *y;
	}

	cl_originw = *x + *w - cl_originx;
	cl_originh = *y + *h - cl_originy;

	int placement;

	if ( cl_viewmode->integer <= 3 ) {
		placement = SCR_VERT_CENTER | SCR_HOR_CENTER;
	} else {
		placement = SCR_VERT_STRETCH | SCR_HOR_STRETCH;
	}

	if ( clc.state == CA_ACTIVE && cl_viewmode->integer == 3 ) {
		qboolean upperRight = qfalse;
		const char *gamedir;

		gamedir = Cvar_VariableString( "fs_game" );

		// place the status bar at the bottom of the screen
		placement = SCR_VERT_BOTTOM | SCR_HOR_CENTER;

		// The scoreboard is drawn after the HUD.
		if ( *y >= 60 && *y <= 69/*86*/ && cl_originx >= 8 && cl_originx < 320 ) {
			// baseq3 scoreboard text or column header (center print is lower than this)
			cl_enteredScoreboard = qtrue;
		}

		// tournament names (A vs B) draw at y=70

		// osp draws initial connect message here but it's not after the HUD
		if ( *y >= 87 && *y <= 144 && cl_originx >= 8 && cl_originx < 320 && strcmp( gamedir, "osp" ) != 0 ) {
			// baseq3 center print text (this is drawn instead of scoreboard)
			// proball scoreboard
			cl_enteredScoreboard = qtrue;
		}

		if ( *y < 380 - 22 - TINYCHAR_HEIGHT * 8 && cl_originx >= 8 && *w > SCREEN_WIDTH / 2 && *h > 16 ) {
			// Team Arena scoreboard
			// if it's above the statusbar and area for cg_drawTeamOverlay 3
			// and over half the screen width,  it's probably the scoreboard background.
			// (baseq3 draws various text, bot icon, and head model before this though)
			cl_enteredScoreboard = qtrue;
		}

		// drew in left of status bar, or more likely drew 3D head model at y=60
		if ( ( *y >= SCREEN_HEIGHT - 48 && *x < 320 )
			|| ( *y >= SCREEN_HEIGHT - 60 && *x >= 256 && *x < 320 ) ) {
			cl_enteredStatusBar = qtrue;
		}

		if ( 320 >= cl_originx && 320 <= cl_originx + cl_originw
		  && 240 >= cl_originy && 240 <= cl_originy + cl_originh
		  && *x == cl_originx && *y == cl_originy ) {
			// center the crosshair (only matters for narrow screens)
			placement = SCR_VERT_CENTER | SCR_HOR_CENTER;
		}
		// these are very game-specific
		else if ( cl_conXOffset->integer && cl_originx + cl_originw <= cl_conXOffset->integer
		       && cl_originy + cl_originh <= cl_conXOffset->integer ) {
			// Team Arena voicechat head
			placement = SCR_VERT_TOP | SCR_HOR_LEFT;
		} else if ( *y >= 240 - 16 && *y < 240 + 64 && ( cl_originy > 253 || cl_originx > 128 )
		         && strcmp( gamedir, "defrag" ) == 0 ) {
			// df_hud_cgaz 1
			placement = SCR_VERT_CENTER | SCR_HOR_CENTER;
		} else if ( !cl_enteredScoreboard && (
		    /* cg_drawTeamOverlay 1; additional powerups are drawn to left of SCREEN_WIDTH - 312 afterward
		    NOTE: osp draws team overlay at y=0_after fps, timer, and things not in the upper right. :( */
		    ( ( cl_originx >= SCREEN_WIDTH - 312 || ( cl_lastUpperRight && *x + *w == cl_lastx && *y == cl_lasty ) )
		                && cl_originy + cl_originh <= TINYCHAR_HEIGHT * 8 )
		    /* cg_drawSnapshot 1; originy == 0 for Q3 font but allow higher in case a mod uses a Team Arena font. */
		    || ( cl_originx > 0 && cl_originy <= BIGCHAR_HEIGHT && cl_originy + cl_originh <= BIGCHAR_HEIGHT + 4 )
#if 0 // This has too many false positives, such as warmup and tournament names.
		    /* cg_drawSnapshot 1 with cg_drawTeamOverlay 1 */
		    || ( cl_lastUpperRight && cl_originx > 0 && cl_originy + cl_originh <= TINYCHAR_HEIGHT * 8 + BIGCHAR_HEIGHT + 4 )
#endif
		    /* Other elements; cg_drawFPS, cg_drawTimer, cg_drawAttacker */
		    || ( cl_originx >= SCREEN_WIDTH - 128 && cl_originy <= BIGCHAR_HEIGHT )
		    || ( cl_lastUpperRight && cl_originx >= 320 && cl_originy + cl_originh <= 240 ) ) ) {
			// top-right HUD information
			//
			// NOTE: max cl_originy + cl_originh in baseq3/missionpack is 186.
			//
			// cg_drawSnapshot text can be very wide and may be far left on the screen.
			// Don't bother checking x because nothing should draw in the upper left
			// due to notify text aside background at x == 0 for cg_viewsize 50
			// and Team Arena voicechat head.
			//
			placement = SCR_VERT_TOP | SCR_HOR_RIGHT;
			upperRight = qtrue;
		} else if ( !cl_enteredScoreboard && ( ( cl_originx <= 64 && ( cl_originx <= 8 || cl_originy > 128 ) )
		        || ( cl_enteredLowerLeft && cl_originx <= 128 && cl_originy >= 240 ) )
		    && cl_originy + cl_originh <= SCREEN_HEIGHT - 48
		    /* && cl_originy >= SCREEN_HEIGHT - 48 * 2 - TINYCHAR_HEIGHT * 8 */ ) {
			// CG_DrawLowerLeft()
			// Item pickup (CG_DrawReward) in baseq3
			// teamoverlay and team chat
			// Showing scoreboard hides status bar but still need to offset baseq3 reward
			// osp needs to check larger area after cl_enteredLowerLeft so URL in MOTD is detected

			// Hack to detect Team Arena's team gametype player info area (y=367).
			// baseq3 team chat is at y=420, reward and team overlay are at y=SCREEN_HEIGHT - 48
			// osp draws reward at y=410 and MOTD is higher.
			if ( !cl_enteredLowerLeft && cl_originy == 367 ) {
				cl_ignoreLowerLeft = qtrue;
			}

			if ( !cl_ignoreLowerLeft ) {
				placement = SCR_VERT_BOTTOM | SCR_HOR_LEFT;
			}
			cl_enteredLowerLeft = qtrue;
		} else if ( !cl_enteredScoreboard && cl_originx >= SCREEN_WIDTH - 312 && ( cl_originy > 128 || cl_originx > 320 )
		    && ( !cl_enteredStatusBar || cl_originy + cl_originh <= SCREEN_HEIGHT - 48 ||
		       ( cl_drewLagometer && cl_originx >= SCREEN_WIDTH - 128 && cl_originy >= SCREEN_HEIGHT - 48 ) ) ) {
			// CG_DrawLowerRight()
			// Scores, powerups in baseq3
			// Powerups for Team Arena draw before status bar and decend into it with 5+ powerups.
			// osp draws lagometer with scores to the left of it.
			// This also detects Team Arena g_gametype 4's location message but it's ignored here and left centered.
			if ( !cl_ignoreLowerLeft ) {
				placement = SCR_VERT_BOTTOM | SCR_HOR_RIGHT;
			}
		} else if ( cl_originy + cl_originh < 380 - 22 ) {
			// above weapon select is centered vertically
			// this mainly for the center print
			placement = SCR_VERT_CENTER | SCR_HOR_CENTER;
		}

		cl_lastUpperRight = upperRight;
	} else if ( clc.state < CA_ACTIVE && cl_viewmode->integer == 3 ) {
		// TODO?: Add a cvar to enable this?
		if ( 0 && *x == 0 && *y == 0 && *w == SCREEN_WIDTH && *h == SCREEN_HEIGHT ) {
			// stretch loading levelshot
			placement = SCR_VERT_STRETCH | SCR_HOR_STRETCH;
		}
	}

	cl_lastx = *x;
	cl_lasty = *y;
	cl_lastw = *w;
	cl_lasth = *h;

	SCR_SetScreenPlacement( placement );
	SCR_AdjustFrom640( x, y, w, h );
}
#endif


/*
====================
CL_GetUserCmd
====================
*/
qboolean CL_GetUserCmd( int cmdNumber, usercmd_t *ucmd ) {
	// cmds[cmdNumber] is the last properly generated command

	// can't return anything that we haven't created yet
	if ( cmdNumber > cl.cmdNumber ) {
		Com_Error( ERR_DROP, "CL_GetUserCmd: %i >= %i", cmdNumber, cl.cmdNumber );
	}

	// the usercmd has been overwritten in the wrapping
	// buffer because it is too far out of date
	if ( cmdNumber <= cl.cmdNumber - CMD_BACKUP ) {
		return qfalse;
	}

	*ucmd = cl.cmds[ cmdNumber & CMD_MASK ];

	return qtrue;
}

int CL_GetCurrentCmdNumber( void ) {
	return cl.cmdNumber;
}


/*
====================
CL_GetParseEntityState
====================
*/
qboolean	CL_GetParseEntityState( int parseEntityNumber, entityState_t *state ) {
	// can't return anything that hasn't been parsed yet
	if ( parseEntityNumber >= cl.parseEntitiesNum ) {
		Com_Error( ERR_DROP, "CL_GetParseEntityState: %i >= %i",
			parseEntityNumber, cl.parseEntitiesNum );
	}

	// can't return anything that has been overwritten in the circular buffer
	if ( parseEntityNumber <= cl.parseEntitiesNum - MAX_PARSE_ENTITIES ) {
		return qfalse;
	}

	*state = cl.parseEntities[ parseEntityNumber & ( MAX_PARSE_ENTITIES - 1 ) ];
	return qtrue;
}

/*
====================
CL_GetCurrentSnapshotNumber
====================
*/
void	CL_GetCurrentSnapshotNumber( int *snapshotNumber, int *serverTime ) {
	*snapshotNumber = cl.snap.messageNum;
	*serverTime = cl.snap.serverTime;
}

/*
====================
CL_GetSnapshot
====================
*/
qboolean	CL_GetSnapshot( int snapshotNumber, snapshot_t *snapshot ) {
	clSnapshot_t	*clSnap;
	int				i, count;

	if ( snapshotNumber > cl.snap.messageNum ) {
		Com_Error( ERR_DROP, "CL_GetSnapshot: snapshotNumber > cl.snapshot.messageNum" );
	}

	// if the frame has fallen out of the circular buffer, we can't return it
	if ( cl.snap.messageNum - snapshotNumber >= PACKET_BACKUP ) {
		return qfalse;
	}

	// if the frame is not valid, we can't return it
	clSnap = &cl.snapshots[snapshotNumber & PACKET_MASK];
	if ( !clSnap->valid ) {
		return qfalse;
	}

	// if the entities in the frame have fallen out of their
	// circular buffer, we can't return it
	if ( cl.parseEntitiesNum - clSnap->parseEntitiesNum >= MAX_PARSE_ENTITIES ) {
		return qfalse;
	}

	// write the snapshot
	snapshot->snapFlags = clSnap->snapFlags;
	snapshot->serverCommandSequence = clSnap->serverCommandNum;
	snapshot->ping = clSnap->ping;
	snapshot->serverTime = clSnap->serverTime;
	Com_Memcpy( snapshot->areamask, clSnap->areamask, sizeof( snapshot->areamask ) );
	snapshot->ps = clSnap->ps;
	count = clSnap->numEntities;
	if ( count > MAX_ENTITIES_IN_SNAPSHOT ) {
		Com_DPrintf( "CL_GetSnapshot: truncated %i entities to %i\n", count, MAX_ENTITIES_IN_SNAPSHOT );
		count = MAX_ENTITIES_IN_SNAPSHOT;
	}
	snapshot->numEntities = count;
	for ( i = 0 ; i < count ; i++ ) {
		snapshot->entities[i] = 
			cl.parseEntities[ ( clSnap->parseEntitiesNum + i ) & (MAX_PARSE_ENTITIES-1) ];
	}

	// FIXME: configstring changes and server commands!!!

	return qtrue;
}

/*
=====================
CL_SetUserCmdValue
=====================
*/
void CL_SetUserCmdValue( int userCmdValue, float sensitivityScale ) {
	cl.cgameUserCmdValue = userCmdValue;
	cl.cgameSensitivity = sensitivityScale;
}

/*
=====================
CL_AddCgameCommand
=====================
*/
void CL_AddCgameCommand( const char *cmdName ) {
	Cmd_AddCommand( cmdName, NULL );
}


/*
=====================
CL_ConfigstringModified
=====================
*/
void CL_ConfigstringModified( void ) {
	char		*old, *s;
	int			i, index;
	char		*dup;
	gameState_t	oldGs;
	int			len;

	index = atoi( Cmd_Argv(1) );
	if ( index < 0 || index >= MAX_CONFIGSTRINGS ) {
		Com_Error( ERR_DROP, "CL_ConfigstringModified: bad index %i", index );
	}
	// get everything after "cs <num>"
	s = Cmd_ArgsFrom(2);

	old = cl.gameState.stringData + cl.gameState.stringOffsets[ index ];
	if ( !strcmp( old, s ) ) {
		return;		// unchanged
	}

	// build the new gameState_t
	oldGs = cl.gameState;

	Com_Memset( &cl.gameState, 0, sizeof( cl.gameState ) );

	// leave the first 0 for uninitialized strings
	cl.gameState.dataCount = 1;
		
	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		if ( i == index ) {
			dup = s;
		} else {
			dup = oldGs.stringData + oldGs.stringOffsets[ i ];
		}
		if ( !dup[0] ) {
			continue;		// leave with the default empty string
		}

		len = strlen( dup );

		if ( len + 1 + cl.gameState.dataCount > MAX_GAMESTATE_CHARS ) {
			Com_Error( ERR_DROP, "MAX_GAMESTATE_CHARS exceeded" );
		}

		// append it to the gameState string buffer
		cl.gameState.stringOffsets[ i ] = cl.gameState.dataCount;
		Com_Memcpy( cl.gameState.stringData + cl.gameState.dataCount, dup, len + 1 );
		cl.gameState.dataCount += len + 1;
	}

	if ( index == CS_SYSTEMINFO ) {
		// parse serverId and other cvars
		CL_SystemInfoChanged();
	} else if ( index == CS_SERVERINFO ) {
		CL_ServerInfoChanged();
	}

}


/*
===================
CL_GetServerCommand

Set up argc/argv for the given command
===================
*/
qboolean CL_GetServerCommand( int serverCommandNumber ) {
	char	*s;
	char	*cmd;
	static char bigConfigString[BIG_INFO_STRING];
	int argc;

	// if we have irretrievably lost a reliable command, drop the connection
	if ( serverCommandNumber <= clc.serverCommandSequence - MAX_RELIABLE_COMMANDS ) {
		// when a demo record was started after the client got a whole bunch of
		// reliable commands then the client never got those first reliable commands
		if ( clc.demoplaying )
			return qfalse;
		Com_Error( ERR_DROP, "CL_GetServerCommand: a reliable command was cycled out" );
		return qfalse;
	}

	if ( serverCommandNumber > clc.serverCommandSequence ) {
		Com_Error( ERR_DROP, "CL_GetServerCommand: requested a command not received" );
		return qfalse;
	}

	s = clc.serverCommands[ serverCommandNumber & ( MAX_RELIABLE_COMMANDS - 1 ) ];
	clc.lastExecutedServerCommand = serverCommandNumber;

	Com_DPrintf( "serverCommand: %i : %s\n", serverCommandNumber, s );

rescan:
	Cmd_TokenizeString( s );
	cmd = Cmd_Argv(0);
	argc = Cmd_Argc();

	if ( !strcmp( cmd, "disconnect" ) ) {
		// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=552
		// allow server to indicate why they were disconnected
		if ( argc >= 2 )
			Com_Error( ERR_SERVERDISCONNECT, "Server disconnected - %s", Cmd_Argv( 1 ) );
		else
			Com_Error( ERR_SERVERDISCONNECT, "Server disconnected" );
	}

	if ( !strcmp( cmd, "bcs0" ) ) {
		Com_sprintf( bigConfigString, BIG_INFO_STRING, "cs %s \"%s", Cmd_Argv(1), Cmd_Argv(2) );
		return qfalse;
	}

	if ( !strcmp( cmd, "bcs1" ) ) {
		s = Cmd_Argv(2);
		if( strlen(bigConfigString) + strlen(s) >= BIG_INFO_STRING ) {
			Com_Error( ERR_DROP, "bcs exceeded BIG_INFO_STRING" );
		}
		strcat( bigConfigString, s );
		return qfalse;
	}

	if ( !strcmp( cmd, "bcs2" ) ) {
		s = Cmd_Argv(2);
		if( strlen(bigConfigString) + strlen(s) + 1 >= BIG_INFO_STRING ) {
			Com_Error( ERR_DROP, "bcs exceeded BIG_INFO_STRING" );
		}
		strcat( bigConfigString, s );
		strcat( bigConfigString, "\"" );
		s = bigConfigString;
		goto rescan;
	}

	if ( !strcmp( cmd, "cs" ) ) {
		CL_ConfigstringModified();
		// reparse the string, because CL_ConfigstringModified may have done another Cmd_TokenizeString()
		Cmd_TokenizeString( s );
		return qtrue;
	}

	if ( !strcmp( cmd, "map_restart" ) ) {
		// clear notify lines and outgoing commands before passing
		// the restart to the cgame
		Con_ClearNotify();
		// reparse the string, because Con_ClearNotify() may have done another Cmd_TokenizeString()
		Cmd_TokenizeString( s );
		Com_Memset( cl.cmds, 0, sizeof( cl.cmds ) );
		return qtrue;
	}

	// the clientLevelShot command is used during development
	// to generate 128*128 screenshots from the intermission
	// point of levels for the menu system to use
	// we pass it along to the cgame to make appropriate adjustments,
	// but we also clear the console and notify lines here
	if ( !strcmp( cmd, "clientLevelShot" ) ) {
		// don't do it if we aren't running the server locally,
		// otherwise malicious remote servers could overwrite
		// the existing thumbnails
		if ( !com_sv_running->integer ) {
			return qfalse;
		}
		// close the console
		Con_Close();
		// take a special screenshot next frame
		Cbuf_AddText( "wait ; wait ; wait ; wait ; screenshot levelshot\n" );
		return qtrue;
	}

	// we may want to put a "connect to other server" command here

	// cgame can now act on the command
	return qtrue;
}


/*
====================
CL_CM_LoadMap

Just adds default parameters that cgame doesn't need to know about
====================
*/
void CL_CM_LoadMap( const char *mapname ) {
	int		checksum;

	CM_LoadMap( mapname, qtrue, &checksum );
}

/*
====================
CL_ShutdownCGame

====================
*/
void CL_ShutdownCGame( void ) {
	Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_CGAME );
	cls.cgameStarted = qfalse;
	if ( !cgvm ) {
		return;
	}
	VM_Call( cgvm, CG_SHUTDOWN );
	VM_Free( cgvm );
	cgvm = NULL;
}

static int	FloatAsInt( float f ) {
	floatint_t fi;
	fi.f = f;
	return fi.i;
}

/*
====================
CL_CgameSystemCalls

The cgame module is making a system call
====================
*/
intptr_t CL_CgameSystemCalls( intptr_t *args ) {
	switch( args[0] ) {
	case CG_PRINT:
		Com_Printf( "%s", (const char*)VMA(1) );
		return 0;
	case CG_ERROR:
		Com_Error( ERR_DROP, "%s", (const char*)VMA(1) );
		return 0;
	case CG_MILLISECONDS:
		return Sys_Milliseconds();
	case CG_CVAR_REGISTER:
		Cvar_Register( VMA(1), VMA(2), VMA(3), args[4] ); 
		return 0;
	case CG_CVAR_UPDATE:
		Cvar_Update( VMA(1) );
		return 0;
	case CG_CVAR_SET:
		Cvar_SetSafe( VMA(1), VMA(2) );
		return 0;
	case CG_CVAR_VARIABLESTRINGBUFFER:
		Cvar_VariableStringBuffer( VMA(1), VMA(2), args[3] );
		return 0;
	case CG_ARGC:
		return Cmd_Argc();
	case CG_ARGV:
		Cmd_ArgvBuffer( args[1], VMA(2), args[3] );
		return 0;
	case CG_ARGS:
		Cmd_ArgsBuffer( VMA(1), args[2] );
		return 0;
	case CG_FS_FOPENFILE:
		return FS_FOpenFileByMode( VMA(1), VMA(2), args[3] );
	case CG_FS_READ:
		FS_Read( VMA(1), args[2], args[3] );
		return 0;
	case CG_FS_WRITE:
		FS_Write( VMA(1), args[2], args[3] );
		return 0;
	case CG_FS_FCLOSEFILE:
		FS_FCloseFile( args[1] );
		return 0;
	case CG_FS_SEEK:
		return FS_Seek( args[1], args[2], args[3] );
	case CG_SENDCONSOLECOMMAND:
		Cbuf_AddText( VMA(1) );
		return 0;
	case CG_ADDCOMMAND:
		CL_AddCgameCommand( VMA(1) );
		return 0;
	case CG_REMOVECOMMAND:
		Cmd_RemoveCommandSafe( VMA(1) );
		return 0;
	case CG_SENDCLIENTCOMMAND:
		CL_AddReliableCommand(VMA(1), qfalse);
		return 0;
	case CG_UPDATESCREEN:
		// this is used during lengthy level loading, so pump message loop
//		Com_EventLoop();	// FIXME: if a server restarts here, BAD THINGS HAPPEN!
// We can't call Com_EventLoop here, a restart will crash and this _does_ happen
// if there is a map change while we are downloading at pk3.
// ZOID
		SCR_UpdateScreen();
		return 0;
	case CG_CM_LOADMAP:
		CL_CM_LoadMap( VMA(1) );
		return 0;
	case CG_CM_NUMINLINEMODELS:
		return CM_NumInlineModels();
	case CG_CM_INLINEMODEL:
		return CM_InlineModel( args[1] );
	case CG_CM_TEMPBOXMODEL:
		return CM_TempBoxModel( VMA(1), VMA(2), /*int capsule*/ qfalse );
	case CG_CM_TEMPCAPSULEMODEL:
		return CM_TempBoxModel( VMA(1), VMA(2), /*int capsule*/ qtrue );
	case CG_CM_POINTCONTENTS:
		return CM_PointContents( VMA(1), args[2] );
	case CG_CM_TRANSFORMEDPOINTCONTENTS:
		return CM_TransformedPointContents( VMA(1), args[2], VMA(3), VMA(4) );
	case CG_CM_BOXTRACE:
		CM_BoxTrace( VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], /*int capsule*/ qfalse );
		return 0;
	case CG_CM_CAPSULETRACE:
		CM_BoxTrace( VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], /*int capsule*/ qtrue );
		return 0;
	case CG_CM_TRANSFORMEDBOXTRACE:
		CM_TransformedBoxTrace( VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], VMA(8), VMA(9), /*int capsule*/ qfalse );
		return 0;
	case CG_CM_TRANSFORMEDCAPSULETRACE:
		CM_TransformedBoxTrace( VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], VMA(8), VMA(9), /*int capsule*/ qtrue );
		return 0;
	case CG_CM_MARKFRAGMENTS:
		return re.MarkFragments( args[1], VMA(2), VMA(3), args[4], VMA(5), args[6], VMA(7) );
	case CG_S_STARTSOUND:
		S_StartSound( VMA(1), args[2], args[3], args[4] );
		return 0;
	case CG_S_STARTLOCALSOUND:
		S_StartLocalSound( args[1], args[2] );
		return 0;
	case CG_S_CLEARLOOPINGSOUNDS:
		S_ClearLoopingSounds(args[1]);
		return 0;
	case CG_S_ADDLOOPINGSOUND:
		S_AddLoopingSound( args[1], VMA(2), VMA(3), args[4] );
		return 0;
	case CG_S_ADDREALLOOPINGSOUND:
		S_AddRealLoopingSound( args[1], VMA(2), VMA(3), args[4] );
		return 0;
	case CG_S_STOPLOOPINGSOUND:
		S_StopLoopingSound( args[1] );
		return 0;
	case CG_S_UPDATEENTITYPOSITION:
		S_UpdateEntityPosition( args[1], VMA(2) );
		return 0;
	case CG_S_RESPATIALIZE:
		S_Respatialize( args[1], VMA(2), VMA(3), args[4] );
		return 0;
	case CG_S_REGISTERSOUND:
		return S_RegisterSound( VMA(1), args[2] );
	case CG_S_STARTBACKGROUNDTRACK:
		S_StartBackgroundTrack( VMA(1), VMA(2) );
		return 0;
	case CG_R_LOADWORLDMAP:
		re.LoadWorld( VMA(1) );
		return 0; 
	case CG_R_REGISTERMODEL:
		return re.RegisterModel( VMA(1) );
	case CG_R_REGISTERSKIN:
		return re.RegisterSkin( VMA(1) );
	case CG_R_REGISTERSHADER:
		return re.RegisterShader( VMA(1) );
	case CG_R_REGISTERSHADERNOMIP:
		return re.RegisterShaderNoMip( VMA(1) );
	case CG_R_REGISTERFONT:
		re.RegisterFont( VMA(1), args[2], VMA(3));
		return 0;
	case CG_R_CLEARSCENE:
		re.ClearScene();
		return 0;
	case CG_R_ADDREFENTITYTOSCENE:
		re.AddRefEntityToScene( VMA(1) );
		return 0;
	case CG_R_ADDPOLYTOSCENE:
		re.AddPolyToScene( args[1], args[2], VMA(3), 1 );
		return 0;
	case CG_R_ADDPOLYSTOSCENE:
		re.AddPolyToScene( args[1], args[2], VMA(3), args[4] );
		return 0;
	case CG_R_LIGHTFORPOINT:
		return re.LightForPoint( VMA(1), VMA(2), VMA(3), VMA(4) );
	case CG_R_ADDLIGHTTOSCENE:
		re.AddLightToScene( VMA(1), VMF(2), VMF(3), VMF(4), VMF(5) );
		return 0;
	case CG_R_ADDADDITIVELIGHTTOSCENE:
		re.AddAdditiveLightToScene( VMA(1), VMF(2), VMF(3), VMF(4), VMF(5) );
		return 0;
	case CG_R_RENDERSCENE:
#ifdef USE_FLEXIBLE_DISPLAY
		if ( cl_flexibleDisplay->integer ) {
			refdef_t fd;
			float x, y, width, height;

			memcpy( &fd, VMA(1), sizeof( refdef_t ) );

			if ( cl_viewmode->integer >= 2 && fd.x == 0 && fd.y == 0 && fd.width == 640 && fd.height == 480 ) {
				float expected_fov_y, water_fov_y;

				// find underwater view fov_y offset
				x = 640 / tan( fd.fov_x / 360 * M_PI );
				expected_fov_y = atan2( 480, x );
				expected_fov_y = expected_fov_y * 360 / M_PI;

				water_fov_y = expected_fov_y - fd.fov_y;

				// make the world scene fill the window
				fd.width = cls.glconfig.vidWidth;
				fd.height = cls.glconfig.vidHeight;

				if ( cl_viewmode->integer == 5 || ( clc.dmflags & DF_FIXED_FOV ) ) {
					// stretch fov
					// this is common for 4:3 video mode on a widescreen monitor.
				} else if ( cl_viewmode->integer <= 4 ) {
					// apply underwater effect after fov change
					// this doesn't matter much but it matches setting cg_fov manually
					// without cl_flexibleDisplay
					fd.fov_x -= water_fov_y;

					// Based on LordHavoc's code for Darkplaces
					// http://www.quakeworld.nu/forum/topic/53/what-does-your-qw-look-like/page/30
					const float baseAspect = 0.75f; // 3/4
					const float aspect = (float)fd.width/(float)fd.height;
					const float desiredFov = fd.fov_x;

					fd.fov_x = atan( tan( desiredFov*M_PI / 360.0f ) * baseAspect*aspect )*360.0f / M_PI;

					fd.fov_x += water_fov_y;
				} else {
					// use vert- to match original Quake 3 widescreen
					// find vert- fov_y
					x = fd.width / tan( fd.fov_x / 360 * M_PI );
					fd.fov_y = atan2( fd.height, x );
					fd.fov_y = fd.fov_y * 360 / M_PI;

					// restore underwater effect
					fd.fov_y -= water_fov_y;
				}
			} else {
				x = fd.x;
				y = fd.y;
				width = fd.width;
				height = fd.height;

				CL_AdjustFromCGame( &x, &y, &width, &height );

				fd.x = (int)x;
				fd.y = (int)y;
				fd.width = (int)(width + 0.5f);
				fd.height = (int)(height + 0.5f);
			}

			re.RenderScene( &fd );
			return 0;
		}
#endif
		re.RenderScene( VMA(1) );
		return 0;
	case CG_R_SETCOLOR:
		re.SetColor( VMA(1) );
		return 0;
	case CG_R_DRAWSTRETCHPIC:
#ifdef USE_FLEXIBLE_DISPLAY
		if ( cl_flexibleDisplay->integer ) {
			float x = VMF(1);
			float y = VMF(2);
			float width = VMF(3);
			float height = VMF(4);
			float s0 = VMF(5);
			float t0 = VMF(6);
			float s1 = VMF(7);
			float t1 = VMF(8);

			// Clamp bounds so it doesn't run off the virtual 4:3 screen.
			// The UI only clamps cl_viewmode <= 2 because the original q3_ui
			// was centered and the menu cursor was visible out-of-bounds in
			// widescreen.
			// CGame q3_ui scoreboard draws highlight for local player running
			// off screen. This wasn't original visible as CGame was always
			// stretched so clamp bounds in cl_viewmode 3 as well.
			if ( cl_viewmode->integer <= 3 ) {
				float value;

				if ( x + width > SCREEN_WIDTH ) {
					value = SCREEN_WIDTH - x;
					if ( value < 0 ) {
						value = 0;
					}
					s1 = ( s1 - s0 ) * ( value / width ) + s0;
					width = value;
				}
				// clamp other sides as well
				if ( y + height > SCREEN_HEIGHT ) {
					value = SCREEN_HEIGHT - y;
					if ( value < 0 ) {
						value = 0;
					}
					t1 = ( t1 - t0 ) * ( value / height ) + t0;
					height = value;
				}
				if ( x < 0 ) {
					value = width + x;
					if ( value < 0 ) {
						value = 0;
					}
					s0 = ( s0 - s1 ) * ( value / width ) + s1;
					width = value;
					x = 0;
				}
				if ( y < 0 ) {
					value = height + y;
					if ( value < 0 ) {
						value = 0;
					}
					t0 = ( t0 - t1 ) * ( value / height ) + t1;
					height = value;
					y = 0;
				}

				if ( width == 0 || height == 0 ) {
					return 0;
				}
			}


			CL_AdjustFromCGame( &x, &y, &width, &height );

			re.DrawStretchPic( x, y, width, height, s0, t0, s1, t1, args[9] );
			return 0;
		}
#endif
		re.DrawStretchPic( VMF(1), VMF(2), VMF(3), VMF(4), VMF(5), VMF(6), VMF(7), VMF(8), args[9] );
		return 0;
	case CG_R_MODELBOUNDS:
		re.ModelBounds( args[1], VMA(2), VMA(3) );
		return 0;
	case CG_R_LERPTAG:
		return re.LerpTag( VMA(1), args[2], args[3], args[4], VMF(5), VMA(6) );
	case CG_GETGLCONFIG:
		CL_GetGlconfig( VMA(1) );
		return 0;
	case CG_GETGAMESTATE:
		CL_GetGameState( VMA(1) );
		return 0;
	case CG_GETCURRENTSNAPSHOTNUMBER:
		CL_GetCurrentSnapshotNumber( VMA(1), VMA(2) );
		return 0;
	case CG_GETSNAPSHOT:
		return CL_GetSnapshot( args[1], VMA(2) );
	case CG_GETSERVERCOMMAND:
		return CL_GetServerCommand( args[1] );
	case CG_GETCURRENTCMDNUMBER:
		return CL_GetCurrentCmdNumber();
	case CG_GETUSERCMD:
		return CL_GetUserCmd( args[1], VMA(2) );
	case CG_SETUSERCMDVALUE:
		CL_SetUserCmdValue( args[1], VMF(2) );
		return 0;
	case CG_MEMORY_REMAINING:
		return Hunk_MemoryRemaining();
  case CG_KEY_ISDOWN:
		return Key_IsDown( args[1] );
  case CG_KEY_GETCATCHER:
		return Key_GetCatcher();
  case CG_KEY_SETCATCHER:
		// Don't allow the cgame module to close the console
		Key_SetCatcher( args[1] | ( Key_GetCatcher( ) & KEYCATCH_CONSOLE ) );
    return 0;
  case CG_KEY_GETKEY:
		return Key_GetKey( VMA(1) );



	case CG_MEMSET:
		Com_Memset( VMA(1), args[2], args[3] );
		return 0;
	case CG_MEMCPY:
		Com_Memcpy( VMA(1), VMA(2), args[3] );
		return 0;
	case CG_STRNCPY:
		strncpy( VMA(1), VMA(2), args[3] );
		return args[1];
	case CG_SIN:
		return FloatAsInt( sin( VMF(1) ) );
	case CG_COS:
		return FloatAsInt( cos( VMF(1) ) );
	case CG_ATAN2:
		return FloatAsInt( atan2( VMF(1), VMF(2) ) );
	case CG_SQRT:
		return FloatAsInt( sqrt( VMF(1) ) );
	case CG_FLOOR:
		return FloatAsInt( floor( VMF(1) ) );
	case CG_CEIL:
		return FloatAsInt( ceil( VMF(1) ) );
	case CG_ACOS:
		return FloatAsInt( Q_acos( VMF(1) ) );

	case CG_PC_ADD_GLOBAL_DEFINE:
		return botlib_export->PC_AddGlobalDefine( VMA(1) );
	case CG_PC_LOAD_SOURCE:
		return botlib_export->PC_LoadSourceHandle( VMA(1) );
	case CG_PC_FREE_SOURCE:
		return botlib_export->PC_FreeSourceHandle( args[1] );
	case CG_PC_READ_TOKEN:
		return botlib_export->PC_ReadTokenHandle( args[1], VMA(2) );
	case CG_PC_SOURCE_FILE_AND_LINE:
		return botlib_export->PC_SourceFileAndLine( args[1], VMA(2), VMA(3) );

	case CG_S_STOPBACKGROUNDTRACK:
		S_StopBackgroundTrack();
		return 0;

	case CG_REAL_TIME:
		return Com_RealTime( VMA(1) );
	case CG_SNAPVECTOR:
		Q_SnapVector(VMA(1));
		return 0;

	case CG_CIN_PLAYCINEMATIC:
#ifdef USE_FLEXIBLE_DISPLAY
	  // NOTE: position is offset by cl_flexibleDisplay
#endif
	  return CIN_PlayCinematic(VMA(1), args[2], args[3], args[4], args[5], args[6], CIN_CGAME);

	case CG_CIN_STOPCINEMATIC:
	  return CIN_StopCinematic(args[1]);

	case CG_CIN_RUNCINEMATIC:
	  return CIN_RunCinematic(args[1]);

	case CG_CIN_DRAWCINEMATIC:
	  CIN_DrawCinematic(args[1]);
	  return 0;

	case CG_CIN_SETEXTENTS:
#ifdef USE_FLEXIBLE_DISPLAY
	  // NOTE: position is offset by cl_flexibleDisplay
#endif
	  CIN_SetExtents(args[1], args[2], args[3], args[4], args[5], CIN_CGAME);
	  return 0;

	case CG_R_REMAP_SHADER:
		re.RemapShader( VMA(1), VMA(2), VMA(3) );
		return 0;

/*
	case CG_LOADCAMERA:
		return loadCamera(VMA(1));

	case CG_STARTCAMERA:
		startCamera(args[1]);
		return 0;

	case CG_GETCAMERAINFO:
		return getCameraInfo(args[1], VMA(2), VMA(3));
*/
	case CG_GET_ENTITY_TOKEN:
		return re.GetEntityToken( VMA(1), args[2] );
	case CG_R_INPVS:
		return re.inPVS( VMA(1), VMA(2) );

	default:
	        assert(0);
		Com_Error( ERR_DROP, "Bad cgame system trap: %ld", (long int) args[0] );
	}
	return 0;
}


/*
====================
CL_InitCGame

Should only be called by CL_StartHunkUsers
====================
*/
void CL_InitCGame( void ) {
	const char			*info;
	const char			*mapname;
	int					t1, t2;
	vmInterpret_t		interpret;

	t1 = Sys_Milliseconds();

	// put away the console
	Con_Close();

	// find the current mapname
	info = cl.gameState.stringData + cl.gameState.stringOffsets[ CS_SERVERINFO ];
	mapname = Info_ValueForKey( info, "mapname" );
	Com_sprintf( cl.mapname, sizeof( cl.mapname ), "maps/%s.bsp", mapname );

	// load the dll or bytecode
	interpret = Cvar_VariableValue("vm_cgame");
	if(cl_connectedToPureServer)
	{
		// if sv_pure is set we only allow qvms to be loaded
		if(interpret != VMI_COMPILED && interpret != VMI_BYTECODE)
			interpret = VMI_COMPILED;
	}

	cgvm = VM_Create( "cgame", CL_CgameSystemCalls, interpret );
	if ( !cgvm ) {
		Com_Error( ERR_DROP, "VM_Create on cgame failed" );
	}
	clc.state = CA_LOADING;

	// init for this gamestate
	// use the lastExecutedServerCommand instead of the serverCommandSequence
	// otherwise server commands sent just before a gamestate are dropped
	VM_Call( cgvm, CG_INIT, clc.serverMessageSequence, clc.lastExecutedServerCommand, clc.clientNum );

	// reset any CVAR_CHEAT cvars registered by cgame
	if ( !clc.demoplaying && !cl_connectedToCheatServer )
		Cvar_SetCheatState();

	// we will send a usercmd this frame, which
	// will cause the server to send us the first snapshot
	clc.state = CA_PRIMED;

	t2 = Sys_Milliseconds();

	Com_Printf( "CL_InitCGame: %5.2f seconds\n", (t2-t1)/1000.0 );

	// have the renderer touch all its images, so they are present
	// on the card even if the driver does deferred loading
	re.EndRegistration();

	// make sure everything is paged in
	if (!Sys_LowPhysicalMemory()) {
		Com_TouchMemory();
	}

	// clear anything that got printed
	Con_ClearNotify ();
}


/*
====================
CL_GameCommand

See if the current console command is claimed by the cgame
====================
*/
qboolean CL_GameCommand( void ) {
	if ( !cgvm ) {
		return qfalse;
	}

	return VM_Call( cgvm, CG_CONSOLE_COMMAND );
}



/*
=====================
CL_CGameRendering
=====================
*/
void CL_CGameRendering( stereoFrame_t stereo ) {
#ifdef USE_FLEXIBLE_DISPLAY
	cl_lastx = cl_lasty = -9000;
	cl_lastw = cl_lasth = 0;
	cl_originx = cl_originy = -9000;
	cl_originw = cl_originh = 0;
	cl_lastUpperRight = qfalse;
	cl_enteredScoreboard = qfalse;
	cl_enteredStatusBar = qfalse;
	cl_enteredLowerLeft = qfalse;
	cl_ignoreLowerLeft = qfalse;
	cl_drewLagometer = qfalse;
#endif

	VM_Call( cgvm, CG_DRAW_ACTIVE_FRAME, cl.serverTime, stereo, clc.demoplaying );
	VM_Debug( 0 );
}


/*
=================
CL_AdjustTimeDelta

Adjust the clients view of server time.

We attempt to have cl.serverTime exactly equal the server's view
of time plus the timeNudge, but with variable latencies over
the internet it will often need to drift a bit to match conditions.

Our ideal time would be to have the adjusted time approach, but not pass,
the very latest snapshot.

Adjustments are only made when a new snapshot arrives with a rational
latency, which keeps the adjustment process framerate independent and
prevents massive overadjustment during times of significant packet loss
or bursted delayed packets.
=================
*/

#define	RESET_TIME	500

void CL_AdjustTimeDelta( void ) {
	int		newDelta;
	int		deltaDelta;

	cl.newSnapshots = qfalse;

	// the delta never drifts when replaying a demo
	if ( clc.demoplaying ) {
		return;
	}

	newDelta = cl.snap.serverTime - cls.realtime;
	deltaDelta = abs( newDelta - cl.serverTimeDelta );

	if ( deltaDelta > RESET_TIME ) {
		cl.serverTimeDelta = newDelta;
		cl.oldServerTime = cl.snap.serverTime;	// FIXME: is this a problem for cgame?
		cl.serverTime = cl.snap.serverTime;
		if ( cl_showTimeDelta->integer ) {
			Com_Printf( "<RESET> " );
		}
	} else if ( deltaDelta > 100 ) {
		// fast adjust, cut the difference in half
		if ( cl_showTimeDelta->integer ) {
			Com_Printf( "<FAST> " );
		}
		cl.serverTimeDelta = ( cl.serverTimeDelta + newDelta ) >> 1;
	} else {
		// slow drift adjust, only move 1 or 2 msec

		// if any of the frames between this and the previous snapshot
		// had to be extrapolated, nudge our sense of time back a little
		// the granularity of +1 / -2 is too high for timescale modified frametimes
		if ( com_timescale->value == 0 || com_timescale->value == 1 ) {
			if ( cl.extrapolatedSnapshot ) {
				cl.extrapolatedSnapshot = qfalse;
				cl.serverTimeDelta -= 2;
			} else {
				// otherwise, move our sense of time forward to minimize total latency
				cl.serverTimeDelta++;
			}
		}
	}

	if ( cl_showTimeDelta->integer ) {
		Com_Printf( "%i ", cl.serverTimeDelta );
	}
}


/*
==================
CL_FirstSnapshot
==================
*/
void CL_FirstSnapshot( void ) {
	// ignore snapshots that don't have entities
	if ( cl.snap.snapFlags & SNAPFLAG_NOT_ACTIVE ) {
		return;
	}
	clc.state = CA_ACTIVE;

	// set the timedelta so we are exactly on this first frame
	cl.serverTimeDelta = cl.snap.serverTime - cls.realtime;
	cl.oldServerTime = cl.snap.serverTime;

	clc.timeDemoBaseTime = cl.snap.serverTime;

	// if this is the first frame of active play,
	// execute the contents of activeAction now
	// this is to allow scripting a timedemo to start right
	// after loading
	if ( cl_activeAction->string[0] ) {
		Cbuf_AddText( cl_activeAction->string );
		Cvar_Set( "activeAction", "" );
	}

#ifdef USE_MUMBLE
	if ((cl_useMumble->integer) && !mumble_islinked()) {
		int ret = mumble_link(CLIENT_WINDOW_TITLE);
		Com_Printf("Mumble: Linking to Mumble application %s\n", ret==0?"ok":"failed");
	}
#endif

#ifdef USE_VOIP
	if (!clc.voipCodecInitialized) {
		int i;
		int error;

		clc.opusEncoder = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &error);

		if ( error ) {
			Com_DPrintf("VoIP: Error opus_encoder_create %d\n", error);
			return;
		}

		for (i = 0; i < MAX_CLIENTS; i++) {
			clc.opusDecoder[i] = opus_decoder_create(48000, 1, &error);
			if ( error ) {
				Com_DPrintf("VoIP: Error opus_decoder_create(%d) %d\n", i, error);
				return;
			}
			clc.voipIgnore[i] = qfalse;
			clc.voipGain[i] = 1.0f;
		}
		clc.voipCodecInitialized = qtrue;
		clc.voipMuteAll = qfalse;
		Cmd_AddCommand ("voip", CL_Voip_f);
		Cvar_Set("cl_voipSendTarget", "spatial");
		Com_Memset(clc.voipTargets, ~0, sizeof(clc.voipTargets));
	}
#endif
}

/*
==================
CL_SetCGameTime
==================
*/
void CL_SetCGameTime( void ) {
	// getting a valid frame message ends the connection process
	if ( clc.state != CA_ACTIVE ) {
		if ( clc.state != CA_PRIMED ) {
			return;
		}
		if ( clc.demoplaying ) {
			// we shouldn't get the first snapshot on the same frame
			// as the gamestate, because it causes a bad time skip
			if ( !clc.firstDemoFrameSkipped ) {
				clc.firstDemoFrameSkipped = qtrue;
				return;
			}
			CL_ReadDemoMessage();
		}
		if ( cl.newSnapshots ) {
			cl.newSnapshots = qfalse;
			CL_FirstSnapshot();
		}
		if ( clc.state != CA_ACTIVE ) {
			return;
		}
	}	

	// if we have gotten to this point, cl.snap is guaranteed to be valid
	if ( !cl.snap.valid ) {
		Com_Error( ERR_DROP, "CL_SetCGameTime: !cl.snap.valid" );
	}

	// allow pause in single player
	if ( sv_paused->integer && CL_CheckPaused() && com_sv_running->integer ) {
		// paused
		return;
	}

	if ( cl.snap.serverTime < cl.oldFrameServerTime ) {
		Com_Error( ERR_DROP, "cl.snap.serverTime < cl.oldFrameServerTime" );
	}
	cl.oldFrameServerTime = cl.snap.serverTime;


	// get our current view of time

	if ( clc.demoplaying && cl_freezeDemo->integer ) {
		// cl_freezeDemo is used to lock a demo in place for single frame advances

	} else {
		// cl_timeNudge is a user adjustable cvar that allows more
		// or less latency to be added in the interest of better 
		// smoothness or better responsiveness.
		int tn;
		
		tn = cl_timeNudge->integer;
		if (tn<-30) {
			tn = -30;
		} else if (tn>30) {
			tn = 30;
		}

		cl.serverTime = cls.realtime + cl.serverTimeDelta - tn;

		// guarantee that time will never flow backwards, even if
		// serverTimeDelta made an adjustment or cl_timeNudge was changed
		if ( cl.serverTime < cl.oldServerTime ) {
			cl.serverTime = cl.oldServerTime;
		}
		cl.oldServerTime = cl.serverTime;

		// note if we are almost past the latest frame (without timeNudge),
		// so we will try and adjust back a bit when the next snapshot arrives
		if ( cls.realtime + cl.serverTimeDelta >= cl.snap.serverTime - 5 ) {
			cl.extrapolatedSnapshot = qtrue;
		}
	}

	// if we have gotten new snapshots, drift serverTimeDelta
	// don't do this every frame, or a period of packet loss would
	// make a huge adjustment
	if ( cl.newSnapshots ) {
		CL_AdjustTimeDelta();
	}

	if ( !clc.demoplaying ) {
		return;
	}

	// if we are playing a demo back, we can just keep reading
	// messages from the demo file until the cgame definitely
	// has valid snapshots to interpolate between

	// a timedemo will always use a deterministic set of time samples
	// no matter what speed machine it is run on,
	// while a normal demo may have different time samples
	// each time it is played back
	if ( cl_timedemo->integer ) {
		int now = Sys_Milliseconds( );
		int frameDuration;

		if (!clc.timeDemoStart) {
			clc.timeDemoStart = clc.timeDemoLastFrame = now;
			clc.timeDemoMinDuration = INT_MAX;
			clc.timeDemoMaxDuration = 0;
		}

		frameDuration = now - clc.timeDemoLastFrame;
		clc.timeDemoLastFrame = now;

		// Ignore the first measurement as it'll always be 0
		if( clc.timeDemoFrames > 0 )
		{
			if( frameDuration > clc.timeDemoMaxDuration )
				clc.timeDemoMaxDuration = frameDuration;

			if( frameDuration < clc.timeDemoMinDuration )
				clc.timeDemoMinDuration = frameDuration;

			// 255 ms = about 4fps
			if( frameDuration > UCHAR_MAX )
				frameDuration = UCHAR_MAX;

			clc.timeDemoDurations[ ( clc.timeDemoFrames - 1 ) %
				MAX_TIMEDEMO_DURATIONS ] = frameDuration;
		}

		clc.timeDemoFrames++;
		cl.serverTime = clc.timeDemoBaseTime + clc.timeDemoFrames * 50;
	}

	while ( cl.serverTime >= cl.snap.serverTime ) {
		// feed another messag, which should change
		// the contents of cl.snap
		CL_ReadDemoMessage();
		if ( clc.state != CA_ACTIVE ) {
			return;		// end of demo
		}
	}

}



