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

#include "tr_local.h"

int			r_firstSceneDrawSurf;

int			r_numdlights;
int			r_firstSceneDlight;

int			r_numcoronas;
int			r_firstSceneCorona;

int			r_numskins;
int			r_numskinsurfaces;

int			r_numentities;
int			r_firstSceneEntity;

int			r_numpolys;
int			r_firstScenePoly;

int			r_numpolyverts;

int			r_numpolybuffers;
int			r_firstScenePolybuffer;


/*
====================
R_InitNextFrame

====================
*/
void R_InitNextFrame( void ) {
	backEndData->commands.used = 0;

	r_firstSceneDrawSurf = 0;

	r_numdlights = 0;
	r_firstSceneDlight = 0;

	r_numcoronas = 0;
	r_firstSceneCorona = 0;

	r_numskins = 0;
	r_numskinsurfaces = 0;

	r_numentities = 0;
	r_firstSceneEntity = 0;

	r_numpolys = 0;
	r_firstScenePoly = 0;

	r_numpolyverts = 0;

	r_numpolybuffers = 0;
	r_firstScenePolybuffer = 0;
}


/*
====================
RE_ClearScene

====================
*/
void RE_ClearScene( void ) {
	int i;

	if ( tr.world ) {
		for ( i = 0; i < tr.world->numBModels; i++ ) {
			tr.world->bmodels[ i ].entityNum = -1;
		}
	}

	r_firstSceneDlight = r_numdlights;
	r_firstSceneCorona = r_numcoronas;
	r_firstSceneEntity = r_numentities;
	r_firstScenePoly = r_numpolys;
	r_firstScenePolybuffer = r_numpolybuffers;
}

/*
=====================
RE_AddSkinToFrame

=====================
*/
qhandle_t RE_AddSkinToFrame( int numSurfaces, const qhandle_t *surfaces ) {
	int		i, j;

	if ( !tr.registered ) {
		return 0;
	}
	if ( numSurfaces <= 0 ) {
		return 0;
	}

	// validate surfaces (should only fail if there is a bug in cgame)
	for ( i = 0; i < numSurfaces; i++ ) {
		if ( surfaces[i] < 0 || surfaces[i] >= tr.numSkinSurfaces ) {
			ri.Printf(PRINT_DEVELOPER, "RE_AddSkinToFrame: Dropping skin, surface index out of range\n");
			return 0;
		}
	}

	// check if skin was already added this frame
	for ( i = 0; i < r_numskins; i++ ) {
		if ( backEndData->skins[i].numSurfaces != numSurfaces )
			continue;

		for ( j = 0; j < numSurfaces; j++ ) {
			if ( backEndData->skins[i].surfaces[j] != surfaces[j] ) {
				break;
			}
		}

		if ( j == numSurfaces ) {
			return i+1;
		}
	}

	if ( r_numskins >= MAX_SKINS ) {
		ri.Printf(PRINT_DEVELOPER, "RE_AddSkinToFrame: Dropping skin, reached MAX_SKINS\n");
		return 0;
	}
	if ( r_numskinsurfaces + numSurfaces > MAX_SKINSURFACES ) {
		ri.Printf(PRINT_DEVELOPER, "RE_AddSkinToFrame: Dropping skin, reached MAX_SKINSURFACES\n");
		return 0;
	}

	// create new skin
	backEndData->skins[r_numskins].surfaces = &backEndData->skinSurfaces[r_numskinsurfaces];
	backEndData->skins[r_numskins].numSurfaces = numSurfaces;

	for ( i = 0; i < numSurfaces; i++ ) {
		backEndData->skinSurfaces[r_numskinsurfaces+i] = surfaces[i];
	}

	r_numskinsurfaces += numSurfaces;
	r_numskins++;

	return r_numskins;
}

/*
===========================================================================

DISCRETE POLYS

===========================================================================
*/

/*
=================
R_PolygonFogNum

See if a polygon chain is inside a fog volume
=================
*/
int R_PolyFogNum( srfPoly_t *poly ) {
	int				i;
	vec3_t			mins, maxs;

	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) {
		return 0;
	}

	VectorCopy( poly->verts[0].xyz, mins );
	VectorCopy( poly->verts[0].xyz, maxs );
	for ( i = 1 ; i < poly->numVerts ; i++ ) {
		AddPointToBounds( poly->verts[i].xyz, mins, maxs );
	}

	return R_BoundsFogNum( &tr.refdef, mins, maxs );
}

/*
=====================
R_RefEntityForBmodelNum
=====================
*/
int R_RefEntityForBmodelNum( int bmodelNum ) {
	if ( tr.world && bmodelNum > 0 && bmodelNum < tr.world->numBModels ) {
		return tr.world->bmodels[ bmodelNum ].entityNum;
	}

	return REFENTITYNUM_WORLD;
}

/*
=====================
R_AddPolygonSurfaces

Adds all the scene's polys into this view's drawsurf list
=====================
*/
void R_AddPolygonSurfaces( void ) {
	int			i;
	shader_t	*sh;
	srfPoly_t	*poly;

	for ( i = 0, poly = tr.refdef.polys; i < tr.refdef.numPolys ; i++, poly++ ) {
		tr.currentEntityNum = R_RefEntityForBmodelNum( poly->bmodelNum );

		if ( tr.currentEntityNum == -1 )
			continue;

		tr.shiftedEntityNum = tr.currentEntityNum << QSORT_REFENTITYNUM_SHIFT;
		sh = R_GetShaderByHandle( poly->hShader );
		R_AddEntDrawSurf( NULL, ( void * )poly, sh, R_PolyFogNum( poly ), qfalse, poly->sortLevel, qfalse, 0 /*cubeMap*/  );
	}
}

/*
=====================
RE_AddPolyToScene

=====================
*/
void RE_AddPolyToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts, int numPolys, int bmodelNum, int sortLevel ) {
	srfPoly_t	*poly;
	int			j;

	if ( !tr.registered ) {
		return;
	}

	if ( !hShader ) {
		// This isn't a useful warning, and an hShader of zero isn't a null shader, it's
		// the default shader.
		//ri.Printf( PRINT_WARNING, "WARNING: RE_AddPolyToScene: NULL poly shader\n");
		//return;
	}

	for ( j = 0; j < numPolys; j++ ) {
		if ( r_numpolyverts + numVerts > max_polyverts || r_numpolys >= max_polys ) {
      /*
      NOTE TTimo this was initially a PRINT_WARNING
      but it happens a lot with high fighting scenes and particles
      since we don't plan on changing the const and making for room for those effects
      simply cut this message to developer only
      */
			ri.Printf( PRINT_DEVELOPER, "WARNING: RE_AddPolyToScene: r_max_polys or r_max_polyverts reached\n");
			return;
		}

		poly = &backEndData->polys[r_numpolys];
		poly->surfaceType = SF_POLY;
		poly->bmodelNum = bmodelNum;
		poly->sortLevel = sortLevel;
		poly->hShader = hShader;
		poly->numVerts = numVerts;
		poly->verts = &backEndData->polyVerts[r_numpolyverts];
		
		Com_Memcpy( poly->verts, &verts[numVerts*j], numVerts * sizeof( *verts ) );

		r_numpolys++;
		r_numpolyverts += numVerts;
	}
}


/*
=================
R_PolyBufferFogNum

See if a polygon buffer is inside a fog volume
=================
*/
int R_PolyBufferFogNum( srfPolyBuffer_t *pPolySurf ) {
	int				i;
	vec3_t			mins, maxs;
	polyBuffer_t	*pPolyBuffer;

	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) {
		return 0;
	}

	pPolyBuffer = pPolySurf->pPolyBuffer;

	VectorCopy( pPolyBuffer->xyz[0], mins );
	VectorCopy( pPolyBuffer->xyz[0], maxs );
	for ( i = 1 ; i < pPolyBuffer->numVerts ; i++ ) {
		AddPointToBounds( pPolyBuffer->xyz[i], mins, maxs );
	}

	return R_BoundsFogNum( &tr.refdef, mins, maxs );
}

/*
=====================
R_AddPolygonSurfaces

Adds all the scene's polys into this view's drawsurf list
=====================
*/
void R_AddPolygonBufferSurfaces( void ) {
	int i;
	shader_t        *sh;
	srfPolyBuffer_t *polybuffer;

	tr.currentEntityNum = REFENTITYNUM_WORLD;
	tr.shiftedEntityNum = tr.currentEntityNum << QSORT_REFENTITYNUM_SHIFT;

	for ( i = 0, polybuffer = tr.refdef.polybuffers; i < tr.refdef.numPolyBuffers ; i++, polybuffer++ ) {
		sh = R_GetShaderByHandle( polybuffer->pPolyBuffer->shader );

		R_AddDrawSurf( ( void * )polybuffer, sh, R_PolyBufferFogNum( polybuffer ), qfalse, qfalse, 0 /*cubeMap*/  );
	}
}

/*
=====================
RE_AddPolyBufferToScene

=====================
*/
void RE_AddPolyBufferToScene( polyBuffer_t* pPolyBuffer ) {
	srfPolyBuffer_t*    pPolySurf;

	if ( r_numpolybuffers >= max_polybuffers ) {
		ri.Printf( PRINT_DEVELOPER, "WARNING: RE_AddPolyBufferToScene: r_maxpolybuffers reached\n");
		return;
	}

	pPolySurf = &backEndData->polybuffers[r_numpolybuffers];
	r_numpolybuffers++;

	pPolySurf->surfaceType = SF_POLYBUFFER;
	pPolySurf->pPolyBuffer = pPolyBuffer;
}

//=================================================================================


/*
=====================
RE_AddRefEntityToScene

=====================
*/
void RE_AddRefEntityToScene( const refEntity_t *ent, int entBufSize, int numVerts, const polyVert_t *verts, int numPolys ) {
	vec3_t cross;

	if ( !tr.registered ) {
		return;
	}
	if ( r_numentities >= MAX_REFENTITIES ) {
		ri.Printf(PRINT_DEVELOPER, "RE_AddRefEntityToScene: Dropping refEntity, reached MAX_REFENTITIES\n");
		return;
	}
	if ( Q_isnan(ent->origin[0]) || Q_isnan(ent->origin[1]) || Q_isnan(ent->origin[2]) ) {
		static qboolean firstTime = qtrue;
		if (firstTime) {
			firstTime = qfalse;
			ri.Printf( PRINT_WARNING, "RE_AddRefEntityToScene passed a refEntity which has an origin with a NaN component\n");
		}
		return;
	}
	if ( (int)ent->reType < 0 || ent->reType >= RT_MAX_REF_ENTITY_TYPE ) {
		ri.Error( ERR_DROP, "RE_AddRefEntityToScene: bad reType %i", ent->reType );
	}

	if ( ent->reType == RT_POLY_GLOBAL || ent->reType == RT_POLY_LOCAL ) {
		int totalVerts = numVerts * numPolys;

		if ( !verts || numVerts <= 0 || numPolys <= 0 ) {
			ri.Printf( PRINT_WARNING, "WARNING: RE_AddRefEntityToScene: RT_POLY without poly info\n");
			return;
		}

		if ( r_numpolyverts + totalVerts > max_polyverts ) {
			ri.Printf( PRINT_DEVELOPER, "WARNING: RE_AddRefEntityToScene: r_max_polyverts reached\n");
			return;
		}

		backEndData->entities[r_numentities].numPolys = numPolys;
		backEndData->entities[r_numentities].numVerts = numVerts;
		backEndData->entities[r_numentities].verts = &backEndData->polyVerts[r_numpolyverts];

		Com_Memcpy( backEndData->entities[r_numentities].verts, verts, totalVerts * sizeof( *verts ) );

		r_numpolyverts += totalVerts;
	}

	Com_Memcpy2( &backEndData->entities[r_numentities].e, sizeof ( refEntity_t ), ent, entBufSize );
	backEndData->entities[r_numentities].lightingCalculated = qfalse;

	CrossProduct(ent->axis[0], ent->axis[1], cross);
	backEndData->entities[r_numentities].mirrored = (DotProduct(ent->axis[2], cross) < 0.f);

	// store bmodel refEntityNums
	if ( tr.world && ent->reType == RT_MODEL ) {
		model_t *model = R_GetModelByHandle( ent->hModel );

		if ( model && model->type == MOD_BRUSH ) {
			model->bmodel->entityNum = r_numentities - r_firstSceneEntity;
		}
	}

	r_numentities++;
}


/*
=====================
RE_AddDynamicLightToScene

=====================
*/
void RE_AddDynamicLightToScene( const vec3_t org, float radius, float intensity, float r, float g, float b, int flags, qhandle_t hShader ) {
	dlight_t	*dl;

	// early out
	if ( !tr.registered || r_numdlights >= MAX_DLIGHTS || radius <= 0 || intensity <= 0 ) {
		return;
	}

	// allow forcing some dlights under all circumstances
	if ( r_dynamiclight->integer == 0 && !( flags & REF_FORCE_DLIGHT ) ) {
		return;
	}

	// set up a new dlight
	dl = &backEndData->dlights[ r_numdlights++ ];
	VectorCopy( org, dl->origin );
	VectorCopy( org, dl->transformed );
	dl->radius = radius;
	dl->radiusInverseCubed = ( 1.0 / dl->radius );
	dl->radiusInverseCubed = dl->radiusInverseCubed * dl->radiusInverseCubed * dl->radiusInverseCubed;
	dl->intensity = intensity;
	dl->color[ 0 ] = r;
	dl->color[ 1 ] = g;
	dl->color[ 2 ] = b;
	dl->flags = flags;
	if ( hShader ) {
		dl->dlshader = R_GetShaderByHandle( hShader );
	} else {
		dl->dlshader = NULL;
	}

	if(r_greyscale->integer)
	{
		float scale;

		scale = LUMA(dl->color[0], dl->color[1], dl->color[2]);
		dl->color[0] = dl->color[1] = dl->color[2] = scale;
	}
	else if(r_greyscale->value)
	{
		float scale;

		scale = LUMA(dl->color[0], dl->color[1], dl->color[2]);
		dl->color[0] = LERP(dl->color[0], scale, r_greyscale->value);
		dl->color[1] = LERP(dl->color[1], scale, r_greyscale->value);
		dl->color[2] = LERP(dl->color[2], scale, r_greyscale->value);
	}
}

/*
=====================
RE_AddLightToScene

=====================
*/
void RE_AddLightToScene( const vec3_t org, float radius, float intensity, float r, float g, float b, qhandle_t hShader ) {
	RE_AddDynamicLightToScene( org, radius, intensity, r, g, b, REF_GRID_DLIGHT | REF_SURFACE_DLIGHT | REF_FLARE_DLIGHT, hShader );
}

/*
=====================
RE_AddAdditiveLightToScene

=====================
*/
void RE_AddAdditiveLightToScene( const vec3_t org, float radius, float intensity, float r, float g, float b ) {
	RE_AddDynamicLightToScene( org, radius, intensity, r, g, b, REF_GRID_DLIGHT | REF_SURFACE_DLIGHT | REF_FLARE_DLIGHT | REF_ADDITIVE_DLIGHT, 0 );
}

/*
=====================
RE_AddVertexLightToScene

=====================
*/
void RE_AddVertexLightToScene( const vec3_t org, float radius, float intensity, float r, float g, float b ) {
	RE_AddDynamicLightToScene( org, radius, intensity, r, g, b, REF_GRID_DLIGHT | REF_SURFACE_DLIGHT | REF_FLARE_DLIGHT | REF_VERTEX_DLIGHT, 0 );
}

/*
=====================
RE_AddJuniorLightToScene

=====================
*/
void RE_AddJuniorLightToScene( const vec3_t org, float radius, float intensity, float r, float g, float b ) {
	RE_AddDynamicLightToScene( org, radius, intensity, r, g, b, REF_GRID_DLIGHT, 0 );
}

/*
=====================
RE_AddDirectedLightToScene

=====================
*/
void RE_AddDirectedLightToScene( const vec3_t normal, float intensity, float r, float g, float b ) {
	RE_AddDynamicLightToScene( normal, 256, intensity, r, g, b, REF_GRID_DLIGHT | REF_SURFACE_DLIGHT | REF_DIRECTED_DLIGHT, 0 );
}


/*
==============
RE_AddCoronaToScene
==============
*/
void RE_AddCoronaToScene( const vec3_t org, float r, float g, float b, float scale, int id, qboolean visible, qhandle_t hShader ) {
	corona_t    *cor;

	if ( !tr.registered ) {
		return;
	}
	if ( r_numcoronas >= MAX_CORONAS ) {
		return;
	}

	cor = &backEndData->coronas[r_numcoronas++];
	VectorCopy( org, cor->origin );
	cor->color[0] = r;
	cor->color[1] = g;
	cor->color[2] = b;
	cor->scale = scale;
	cor->id = id;
	cor->visible = visible;
	cor->shader = R_GetShaderByHandle( hShader );
}


void RE_BeginScene(const refdef_t *fd)
{
	Com_Memcpy( tr.refdef.text, fd->text, sizeof( tr.refdef.text ) );

	tr.refdef.x = fd->x;
	tr.refdef.y = fd->y;
	tr.refdef.width = fd->width;
	tr.refdef.height = fd->height;
	tr.refdef.fov_x = fd->fov_x;
	tr.refdef.fov_y = fd->fov_y;

	if ( fd->weapon_fov_x > 0 && fd->weapon_fov_y > 0 ) {
		tr.refdef.weapon_fov_x = fd->weapon_fov_x;
		tr.refdef.weapon_fov_y = fd->weapon_fov_y;
	} else {
		tr.refdef.weapon_fov_x = fd->fov_x;
		tr.refdef.weapon_fov_y = fd->fov_y;
	}

	VectorCopy( fd->vieworg, tr.refdef.vieworg );
	VectorCopy( fd->viewaxis[0], tr.refdef.viewaxis[0] );
	VectorCopy( fd->viewaxis[1], tr.refdef.viewaxis[1] );
	VectorCopy( fd->viewaxis[2], tr.refdef.viewaxis[2] );

	tr.refdef.time = fd->time;
	tr.refdef.rdflags = fd->rdflags;

	tr.refdef.skyAlpha = fd->skyAlpha;

	tr.refdef.fogType = fd->fogType;
	if ( tr.refdef.fogType < FT_NONE || tr.refdef.fogType >= FT_MAX_FOG_TYPE ) {
		tr.refdef.fogType = FT_NONE;
	}

	tr.refdef.fogColor[0] = fd->fogColor[0];
	tr.refdef.fogColor[1] = fd->fogColor[1];
	tr.refdef.fogColor[2] = fd->fogColor[2];

	tr.refdef.fogColorInt = ColorBytes4( tr.refdef.fogColor[0],
									  tr.refdef.fogColor[1],
									  tr.refdef.fogColor[2], 1.0 );

	tr.refdef.fogDensity = fd->fogDensity;

	if ( r_zfar->value ) {
		tr.refdef.fogDepthForOpaque = r_zfar->value < 1 ? 1 : r_zfar->value;
	} else {
		tr.refdef.fogDepthForOpaque = fd->fogDepthForOpaque < 1 ? 1 : fd->fogDepthForOpaque;
	}
	tr.refdef.fogTcScale = R_FogTcScale( tr.refdef.fogType, tr.refdef.fogDepthForOpaque, tr.refdef.fogDensity );

	tr.refdef.maxFarClip = fd->farClip;

	// copy the areamask data over and note if it has changed, which
	// will force a reset of the visible leafs even if the view hasn't moved
	tr.refdef.areamaskModified = qfalse;
	if ( ! (tr.refdef.rdflags & RDF_NOWORLDMODEL) ) {
		int		areaDiff;
		int		i;

		// compare the area bits
		areaDiff = 0;
		for (i = 0 ; i < MAX_MAP_AREA_BYTES/4 ; i++) {
			areaDiff |= ((int *)tr.refdef.areamask)[i] ^ ((int *)fd->areamask)[i];
			((int *)tr.refdef.areamask)[i] = ((int *)fd->areamask)[i];
		}

		if ( areaDiff ) {
			// a door just opened or something
			tr.refdef.areamaskModified = qtrue;
		}
	}

	tr.refdef.sunDir[3] = 0.0f;
	tr.refdef.sunCol[3] = 1.0f;
	tr.refdef.sunAmbCol[3] = 1.0f;

	VectorCopy(tr.sunDirection, tr.refdef.sunDir);
	if ( (tr.refdef.rdflags & RDF_NOWORLDMODEL) || !(r_depthPrepass->value) ){
		VectorSet(tr.refdef.sunCol, 0, 0, 0);
		VectorSet(tr.refdef.sunAmbCol, 0, 0, 0);
	}
	else
	{
		float scale = (1 << r_mapOverBrightBits->integer) / 255.0f;

		if (r_forceSun->integer)
			VectorScale(tr.sunLight, scale * r_forceSunLightScale->value, tr.refdef.sunCol);
		else
			VectorScale(tr.sunLight, scale, tr.refdef.sunCol);

		if (r_sunlightMode->integer == 1)
		{
			tr.refdef.sunAmbCol[0] =
			tr.refdef.sunAmbCol[1] =
			tr.refdef.sunAmbCol[2] = r_forceSun->integer ? r_forceSunAmbientScale->value : tr.sunShadowScale;
		}
		else
		{
			if (r_forceSun->integer)
				VectorScale(tr.sunLight, scale * r_forceSunAmbientScale->value, tr.refdef.sunAmbCol);
			else
				VectorScale(tr.sunLight, scale * tr.sunShadowScale, tr.refdef.sunAmbCol);
		}
	}

	if (r_forceAutoExposure->integer)
	{
		tr.refdef.autoExposureMinMax[0] = r_forceAutoExposureMin->value;
		tr.refdef.autoExposureMinMax[1] = r_forceAutoExposureMax->value;
	}
	else
	{
		tr.refdef.autoExposureMinMax[0] = tr.autoExposureMinMax[0];
		tr.refdef.autoExposureMinMax[1] = tr.autoExposureMinMax[1];
	}

	if (r_forceToneMap->integer)
	{
		tr.refdef.toneMinAvgMaxLinear[0] = pow(2, r_forceToneMapMin->value);
		tr.refdef.toneMinAvgMaxLinear[1] = pow(2, r_forceToneMapAvg->value);
		tr.refdef.toneMinAvgMaxLinear[2] = pow(2, r_forceToneMapMax->value);
	}
	else
	{
		tr.refdef.toneMinAvgMaxLinear[0] = pow(2, tr.toneMinAvgMaxLevel[0]);
		tr.refdef.toneMinAvgMaxLinear[1] = pow(2, tr.toneMinAvgMaxLevel[1]);
		tr.refdef.toneMinAvgMaxLinear[2] = pow(2, tr.toneMinAvgMaxLevel[2]);
	}

	tr.refdef.blurFactor = fd->blurFactor;

	if (fd->rdflags & RDF_SUNLIGHT)
	{
		VectorCopy(fd->sunDir,    tr.refdef.sunDir);
		VectorCopy(fd->sunCol,    tr.refdef.sunCol);
		VectorCopy(fd->sunAmbCol, tr.refdef.sunAmbCol);
	}

	// derived info

	tr.refdef.floatTime = tr.refdef.time * 0.001;

	tr.refdef.numDrawSurfs = r_firstSceneDrawSurf;
	tr.refdef.drawSurfs = backEndData->drawSurfs;

	tr.refdef.numSkins = r_numskins;
	tr.refdef.skins = backEndData->skins;

	tr.refdef.num_entities = r_numentities - r_firstSceneEntity;
	tr.refdef.entities = &backEndData->entities[r_firstSceneEntity];

	tr.refdef.num_dlights = r_numdlights - r_firstSceneDlight;
	tr.refdef.dlights = &backEndData->dlights[r_firstSceneDlight];
	tr.refdef.dlightBits = 0;

	tr.refdef.num_coronas = r_numcoronas - r_firstSceneCorona;
	tr.refdef.coronas = &backEndData->coronas[r_firstSceneCorona];

	tr.refdef.numPolys = r_numpolys - r_firstScenePoly;
	tr.refdef.polys = &backEndData->polys[r_firstScenePoly];

	tr.refdef.numPolyBuffers = r_numpolybuffers - r_firstScenePolybuffer;
	tr.refdef.polybuffers = &backEndData->polybuffers[r_firstScenePolybuffer];

	tr.refdef.num_pshadows = 0;
	tr.refdef.pshadows = &backEndData->pshadows[0];

	// a single frame may have multiple scenes draw inside it --
	// a 3D game view, 3D status bar renderings, 3D menus, etc.
	// They need to be distinguished by the light flare code, because
	// the visibility state for a given surface may be different in
	// each scene / view.
	tr.frameSceneNum++;
	tr.sceneCount++;
}


void RE_EndScene(void)
{
	// the next scene rendered in this frame will tack on after this one
	r_firstSceneDrawSurf = tr.refdef.numDrawSurfs;
	r_firstSceneEntity = r_numentities;
	r_firstSceneDlight = r_numdlights;
	r_firstScenePoly = r_numpolys;
	r_firstScenePolybuffer = r_numpolybuffers;
}

/*
@@@@@@@@@@@@@@@@@@@@@
RE_RenderScene

Draw a 3D view into a part of the window, then return
to 2D drawing.

Rendering a scene may require multiple views to be rendered
to handle mirrors,
@@@@@@@@@@@@@@@@@@@@@
*/
void RE_RenderScene( const refdef_t *vmRefDef, int vmRefDefSize ) {
	refdef_t		fd;
	viewParms_t		parms;
	int				startTime;

	if ( !tr.registered ) {
		return;
	}
	GLimp_LogComment( "====== RE_RenderScene =====\n" );

	if ( r_norefresh->integer ) {
		return;
	}

	startTime = ri.Milliseconds();

	Com_Memcpy2( &fd, sizeof ( refdef_t ), vmRefDef, vmRefDefSize );

	if (!tr.world && !( fd.rdflags & RDF_NOWORLDMODEL ) ) {
		ri.Error (ERR_DROP, "R_RenderScene: NULL worldmodel");
	}

	RE_BeginScene(&fd);

	// SmileTheory: playing with shadow mapping
	if (!( fd.rdflags & RDF_NOWORLDMODEL ) && tr.refdef.num_dlights && r_dlightMode->integer >= 2)
	{
		R_RenderDlightCubemaps(&fd);
	}

	/* playing with more shadows */
	if(glRefConfig.framebufferObject && !( fd.rdflags & RDF_NOWORLDMODEL ) && r_shadows->integer == 4)
	{
		R_RenderPshadowMaps(&fd);
	}

	// playing with even more shadows
	if(glRefConfig.framebufferObject && r_sunlightMode->integer && !( fd.rdflags & RDF_NOWORLDMODEL ) && (r_forceSun->integer || tr.sunShadows))
	{
		if (r_shadowCascadeZFar->integer != 0)
		{
			R_RenderSunShadowMaps(&fd, 0);
			R_RenderSunShadowMaps(&fd, 1);
			R_RenderSunShadowMaps(&fd, 2);
		}
		else
		{
			Mat4Zero(tr.refdef.sunShadowMvp[0]);
			Mat4Zero(tr.refdef.sunShadowMvp[1]);
			Mat4Zero(tr.refdef.sunShadowMvp[2]);
		}

		// only rerender last cascade if sun has changed position
		if (r_forceSun->integer == 2 || !VectorCompare(tr.refdef.sunDir, tr.lastCascadeSunDirection))
		{
			VectorCopy(tr.refdef.sunDir, tr.lastCascadeSunDirection);
			R_RenderSunShadowMaps(&fd, 3);
			Mat4Copy(tr.refdef.sunShadowMvp[3], tr.lastCascadeSunMvp);
		}
		else
		{
			Mat4Copy(tr.lastCascadeSunMvp, tr.refdef.sunShadowMvp[3]);
		}
	}

	// playing with cube maps
	// this is where dynamic cubemaps would be rendered
	if (0) //(glRefConfig.framebufferObject && !( fd.rdflags & RDF_NOWORLDMODEL ))
	{
		int i, j;

		for (i = 0; i < tr.numCubemaps; i++)
		{
			for (j = 0; j < 6; j++)
			{
				R_RenderCubemapSide(i, j, qtrue);
			}
		}
	}

	// setup view parms for the initial view
	//
	// set up viewport
	// The refdef takes 0-at-the-top y coordinates, so
	// convert to GL's 0-at-the-bottom space
	//
	Com_Memset( &parms, 0, sizeof( parms ) );
	parms.viewportX = tr.refdef.x;
	parms.viewportY = glConfig.vidHeight - ( tr.refdef.y + tr.refdef.height );
	parms.viewportWidth = tr.refdef.width;
	parms.viewportHeight = tr.refdef.height;
	parms.isPortal = qfalse;

	parms.fovX = tr.refdef.fov_x;
	parms.fovY = tr.refdef.fov_y;

	parms.weaponFovX = tr.refdef.weapon_fov_x;
	parms.weaponFovY = tr.refdef.weapon_fov_y;
	
	parms.stereoFrame = tr.refdef.stereoFrame;

	VectorCopy( fd.vieworg, parms.or.origin );
	VectorCopy( fd.viewaxis[0], parms.or.axis[0] );
	VectorCopy( fd.viewaxis[1], parms.or.axis[1] );
	VectorCopy( fd.viewaxis[2], parms.or.axis[2] );

	VectorCopy( fd.vieworg, parms.pvsOrigin );

	if(!( fd.rdflags & RDF_NOWORLDMODEL ) && r_depthPrepass->value && ((r_forceSun->integer) || tr.sunShadows))
	{
		parms.flags = VPF_USESUNLIGHT;
	}

	R_RenderView( &parms );

	if(!( fd.rdflags & RDF_NOWORLDMODEL ))
		R_AddPostProcessCmd();

	RE_EndScene();

	tr.frontEndMsec += ri.Milliseconds() - startTime;
}
