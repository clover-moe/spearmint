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

/*****************************************************************************
 * name:		be_interface.c
 *
 * desc:		bot library interface
 *
 * $Archive: /MissionPack/code/botlib/be_interface.c $
 *
 *****************************************************************************/

#include "../qcommon/q_shared.h"
#include "l_log.h"
#include "l_libvar.h"
#include "l_memory.h"
#include "aasfile.h"
#include "botlib.h"
#include "be_aas.h"
#include "be_aas_funcs.h"
#include "be_aas_def.h"
#include "be_interface.h"

//library globals in a structure
botlib_globals_t botlibglobals;

botlib_export_t be_botlib_export;
botlib_import_t botimport;
//
int botDeveloper;
//qtrue if the library is setup
int botlibsetup = qfalse;

//===========================================================================
//
// several functions used by the exported functions
//
//===========================================================================

//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
qboolean ValidClientNumber(int num, char *str)
{
	if (num < 0 || num > botlibglobals.maxclients)
	{
		//weird: the disabled stuff results in a crash
		botimport.Print(PRT_ERROR, "%s: invalid client number %d, [0, %d]\n",
										str, num, botlibglobals.maxclients);
		return qfalse;
	} //end if
	return qtrue;
} //end of the function BotValidateClientNumber
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
qboolean ValidEntityNumber(int num, char *str)
{
	if (num < 0 || num > botlibglobals.maxentities)
	{
		botimport.Print(PRT_ERROR, "%s: invalid entity number %d, [0, %d]\n",
										str, num, botlibglobals.maxentities);
		return qfalse;
	} //end if
	return qtrue;
} //end of the function BotValidateClientNumber
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
qboolean BotLibSetup(char *str)
{
	if (!botlibglobals.botlibsetup)
	{
		botimport.Print(PRT_ERROR, "%s: bot library used before being setup\n", str);
		return qfalse;
	} //end if
	return qtrue;
} //end of the function BotLibSetup

//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
int Export_BotLibSetup(void)
{
	int		errnum;
	
	botDeveloper = LibVarGetValue("bot_developer");
 	memset( &botlibglobals, 0, sizeof(botlibglobals) );
	//initialize byte swapping (litte endian etc.)
//	Swap_Init();

	if(botDeveloper)
	{
		Log_Open("botlib.log");
	}

	botimport.Print(PRT_DEVELOPER, "------- BotLib Initialization -------\n");

	botlibglobals.maxclients = (int) LibVarValue("maxclients", "128");
	botlibglobals.maxentities = (int) LibVarValue("maxentities", "1024");

	errnum = AAS_Setup();			//be_aas_main.c
	if (errnum != BLERR_NOERROR) return errnum;

	botlibsetup = qtrue;
	botlibglobals.botlibsetup = qtrue;

	return BLERR_NOERROR;
} //end of the function Export_BotLibSetup
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
int Export_BotLibShutdown(void)
{
	if (!BotLibSetup("BotLibShutdown")) return BLERR_LIBRARYNOTSETUP;
#ifndef DEMO
	//DumpFileCRCs();
#endif //DEMO
	//shud down aas
	AAS_Shutdown();
	//free all libvars
	LibVarDeAllocAll();
	//clear debug polygons and release handles
	AAS_ClearShownPolygons();
	AAS_ClearShownDebugLines();

	//dump all allocated memory
//	DumpMemory();
#ifdef DEBUG
	PrintMemoryLabels();
#endif
	//shut down library log file
	Log_Shutdown();
	//
	botlibsetup = qfalse;
	botlibglobals.botlibsetup = qfalse;
	//
	return BLERR_NOERROR;
} //end of the function Export_BotLibShutdown
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
int Export_BotLibVarSet(const char *var_name, const char *value)
{
	LibVarSet(var_name, value);
	return BLERR_NOERROR;
} //end of the function Export_BotLibVarSet
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
int Export_BotLibVarGet(const char *var_name, char *value, int size)
{
	char *varvalue;

	varvalue = LibVarGetString(var_name);
	Q_strncpyz(value, varvalue, size);
	return BLERR_NOERROR;
} //end of the function Export_BotLibVarGet
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
int Export_BotLibStartFrame(float time)
{
	if (!BotLibSetup("BotStartFrame")) return BLERR_LIBRARYNOTSETUP;
	return AAS_StartFrame(time);
} //end of the function Export_BotLibStartFrame
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
int Export_BotLibLoadMap(const char *mapname)
{
#ifdef DEBUG
	int starttime = botimport.MilliSeconds();
#endif
	int errnum;

	if (!BotLibSetup("BotLoadMap")) return BLERR_LIBRARYNOTSETUP;
	//
	botimport.Print(PRT_DEVELOPER, "------------ Map Loading ------------\n");
	//startup AAS for the current map, model and sound index
	errnum = AAS_LoadMap(mapname);
	if (errnum != BLERR_NOERROR) return errnum;
	//
	botimport.Print(PRT_DEVELOPER, "-------------------------------------\n");
#ifdef DEBUG
	botimport.Print(PRT_DEVELOPER, "map loaded in %d msec\n", botimport.MilliSeconds() - starttime);
#endif
	//
	return BLERR_NOERROR;
} //end of the function Export_BotLibLoadMap
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
int Export_BotLibUpdateEntity(int ent, bot_entitystate_t *state)
{
	if (!BotLibSetup("BotUpdateEntity")) return BLERR_LIBRARYNOTSETUP;
	if (!ValidEntityNumber(ent, "BotUpdateEntity")) return BLERR_INVALIDENTITYNUMBER;

	return AAS_UpdateEntity(ent, state);
} //end of the function Export_BotLibUpdateEntity
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void Export_AAS_TracePlayerBBox(struct aas_trace_s *trace, vec3_t start, vec3_t end, int presencetype, int passent, int contentmask)
{
	aas_trace_t tr;
	if ( !trace ) return;
	tr = AAS_TracePlayerBBox(start, end, presencetype, passent, contentmask);
	Com_Memcpy(trace, &tr, sizeof (aas_trace_t));
} //end of the function Export_AAS_TracePlayerBBox


/*
============
Init_AAS_Export
============
*/
static void Init_AAS_Export( aas_export_t *aas ) {
	//--------------------------------------------
	// be_aas_main.c
	//--------------------------------------------
	aas->AAS_Loaded = AAS_Loaded;
	aas->AAS_Initialized = AAS_Initialized;
	aas->AAS_PresenceTypeBoundingBox = AAS_PresenceTypeBoundingBox;
	aas->AAS_Time = AAS_Time;
	//--------------------------------------------
	// be_aas_debug.c
	//--------------------------------------------
	aas->AAS_ClearShownDebugLines = AAS_ClearShownDebugLines;
	aas->AAS_ClearShownPolygons = AAS_ClearShownPolygons;
	aas->AAS_DebugLine = AAS_DebugLine;
	aas->AAS_PermanentLine = AAS_PermanentLine;
	aas->AAS_DrawPermanentCross = AAS_DrawPermanentCross;
	aas->AAS_DrawPlaneCross = AAS_DrawPlaneCross;
	aas->AAS_ShowBoundingBox = AAS_ShowBoundingBox;
	aas->AAS_ShowFace = AAS_ShowFace;
	aas->AAS_ShowArea = AAS_ShowArea;
	aas->AAS_ShowAreaPolygons = AAS_ShowAreaPolygons;
	aas->AAS_DrawCross = AAS_DrawCross;
	aas->AAS_PrintTravelType = AAS_PrintTravelType;
	aas->AAS_DrawArrow = AAS_DrawArrow;
	aas->AAS_ShowReachability = AAS_ShowReachability;
	aas->AAS_ShowReachableAreas = AAS_ShowReachableAreas;
	aas->AAS_FloodAreas = AAS_FloodAreas;
	//--------------------------------------------
	// be_aas_sample.c
	//--------------------------------------------
	aas->AAS_PointAreaNum = AAS_PointAreaNum;
	aas->AAS_PointReachabilityAreaIndex = AAS_PointReachabilityAreaIndex;
	aas->AAS_TracePlayerBBox = Export_AAS_TracePlayerBBox;
	aas->AAS_TraceAreas = AAS_TraceAreas;
	aas->AAS_BBoxAreas = AAS_BBoxAreas;
	aas->AAS_AreaInfo = AAS_AreaInfo;
	//--------------------------------------------
	// be_aas_bspq3.c
	//--------------------------------------------
	aas->AAS_PointContents = AAS_PointContents;
	aas->AAS_NextBSPEntity = AAS_NextBSPEntity;
	aas->AAS_ValueForBSPEpairKey = AAS_ValueForBSPEpairKey;
	aas->AAS_VectorForBSPEpairKey = AAS_VectorForBSPEpairKey;
	aas->AAS_FloatForBSPEpairKey = AAS_FloatForBSPEpairKey;
	aas->AAS_IntForBSPEpairKey = AAS_IntForBSPEpairKey;
	//--------------------------------------------
	// be_aas_reach.c
	//--------------------------------------------
	aas->AAS_AreaReachability = AAS_AreaReachability;
	aas->AAS_BestReachableArea = AAS_BestReachableArea;
	aas->AAS_BestReachableFromJumpPadArea = AAS_BestReachableFromJumpPadArea;
	aas->AAS_NextModelReachability = AAS_NextModelReachability;
	aas->AAS_AreaGroundFaceArea = AAS_AreaGroundFaceArea;
	aas->AAS_AreaCrouch = AAS_AreaCrouch;
	aas->AAS_AreaSwim = AAS_AreaSwim;
	aas->AAS_AreaLiquid = AAS_AreaLiquid;
	aas->AAS_AreaLava = AAS_AreaLava;
	aas->AAS_AreaSlime = AAS_AreaSlime;
	aas->AAS_AreaGrounded = AAS_AreaGrounded;
	aas->AAS_AreaLadder = AAS_AreaLadder;
	aas->AAS_AreaJumpPad = AAS_AreaJumpPad;
	aas->AAS_AreaDoNotEnter = AAS_AreaDoNotEnter;
	//--------------------------------------------
	// be_aas_route.c
	//--------------------------------------------
	aas->AAS_TravelFlagForType = AAS_TravelFlagForType;
	aas->AAS_AreaContentsTravelFlags = AAS_AreaContentsTravelFlags;
	aas->AAS_NextAreaReachability = AAS_NextAreaReachability;
	aas->AAS_ReachabilityFromNum = AAS_ReachabilityFromNum;
	aas->AAS_RandomGoalArea = AAS_RandomGoalArea;
	aas->AAS_EnableRoutingArea = AAS_EnableRoutingArea;
	aas->AAS_AreaTravelTime = AAS_AreaTravelTime;
	aas->AAS_AreaTravelTimeToGoalArea = AAS_AreaTravelTimeToGoalArea;
	aas->AAS_PredictRoute = AAS_PredictRoute;
	//--------------------------------------------
	// be_aas_altroute.c
	//--------------------------------------------
	aas->AAS_AlternativeRouteGoals = AAS_AlternativeRouteGoals;
	//--------------------------------------------
	// be_aas_move.c
	//--------------------------------------------
	aas->AAS_PredictPlayerMovement = AAS_PredictPlayerMovement;
	aas->AAS_OnGround = AAS_OnGround;
	aas->AAS_Swimming = AAS_Swimming;
	aas->AAS_JumpReachRunStart = AAS_JumpReachRunStart;
	aas->AAS_AgainstLadder = AAS_AgainstLadder;
	aas->AAS_HorizontalVelocityForJump = AAS_HorizontalVelocityForJump;
	aas->AAS_DropToFloor = AAS_DropToFloor;
}


/*
============
GetBotLibAPI
============
*/
botlib_export_t *GetBotLibAPI(int apiVersion, botlib_import_t *import) {
	assert(import);
	botimport = *import;
	assert(botimport.Print);

	Com_Memset( &be_botlib_export, 0, sizeof( be_botlib_export ) );

	if ( apiVersion != BOTLIB_API_VERSION ) {
		botimport.Print( PRT_ERROR, "Mismatched BOTLIB_API_VERSION: expected %i, got %i\n", BOTLIB_API_VERSION, apiVersion );
		return NULL;
	}

	Init_AAS_Export(&be_botlib_export.aas);

	be_botlib_export.BotLibSetup = Export_BotLibSetup;
	be_botlib_export.BotLibShutdown = Export_BotLibShutdown;
	be_botlib_export.BotLibVarSet = Export_BotLibVarSet;
	be_botlib_export.BotLibVarGet = Export_BotLibVarGet;

	be_botlib_export.BotLibStartFrame = Export_BotLibStartFrame;
	be_botlib_export.BotLibLoadMap = Export_BotLibLoadMap;
	be_botlib_export.BotLibUpdateEntity = Export_BotLibUpdateEntity;

	return &be_botlib_export;
}
