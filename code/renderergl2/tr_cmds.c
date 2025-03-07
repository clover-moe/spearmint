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

/*
=====================
R_PerformanceCounters
=====================
*/
void R_PerformanceCounters( void ) {
	if ( !r_speeds->integer ) {
		// clear the counters even if we aren't printing
		Com_Memset( &tr.pc, 0, sizeof( tr.pc ) );
		Com_Memset( &backEnd.pc, 0, sizeof( backEnd.pc ) );
		return;
	}

	if (r_speeds->integer == 1) {
		ri.Printf (PRINT_ALL, "%i/%i/%i shaders/batches/surfs %i leafs %i verts %i/%i tris %.2f mtex %.2f dc\n",
			backEnd.pc.c_shaders, backEnd.pc.c_surfBatches, backEnd.pc.c_surfaces, tr.pc.c_leafs, backEnd.pc.c_vertexes, 
			backEnd.pc.c_indexes/3, backEnd.pc.c_totalIndexes/3, 
			R_SumOfUsedImages()/(1000000.0f), backEnd.pc.c_overDraw / (float)(glConfig.vidWidth * glConfig.vidHeight) ); 
	} else if (r_speeds->integer == 2) {
		ri.Printf (PRINT_ALL, "(patch) %i sin %i sclip  %i sout %i bin %i bclip %i bout\n",
			tr.pc.c_sphere_cull_patch_in, tr.pc.c_sphere_cull_patch_clip, tr.pc.c_sphere_cull_patch_out, 
			tr.pc.c_box_cull_patch_in, tr.pc.c_box_cull_patch_clip, tr.pc.c_box_cull_patch_out );
		ri.Printf (PRINT_ALL, "(md3) %i sin %i sclip  %i sout %i bin %i bclip %i bout\n",
			tr.pc.c_sphere_cull_md3_in, tr.pc.c_sphere_cull_md3_clip, tr.pc.c_sphere_cull_md3_out, 
			tr.pc.c_box_cull_md3_in, tr.pc.c_box_cull_md3_clip, tr.pc.c_box_cull_md3_out );
	} else if (r_speeds->integer == 3) {
		ri.Printf (PRINT_ALL, "viewcluster: %i\n", tr.viewCluster );
	} else if (r_speeds->integer == 4) {
		if ( backEnd.pc.c_dlightVertexes ) {
			ri.Printf (PRINT_ALL, "dlight srf:%i  culled:%i  verts:%i  tris:%i\n", 
				tr.pc.c_dlightSurfaces, tr.pc.c_dlightSurfacesCulled,
				backEnd.pc.c_dlightVertexes, backEnd.pc.c_dlightIndexes / 3 );
		}
	} 
	else if (r_speeds->integer == 5 )
	{
		ri.Printf( PRINT_ALL, "zFar: %.0f\n", tr.viewParms.zFar );
	}
	else if (r_speeds->integer == 6 )
	{
		ri.Printf( PRINT_ALL, "flare adds:%i tests:%i renders:%i\n", 
			backEnd.pc.c_flareAdds, backEnd.pc.c_flareTests, backEnd.pc.c_flareRenders );
	}
	else if (r_speeds->integer == 7 )
	{
		ri.Printf( PRINT_ALL, "VAO draws: static %i dynamic %i\n",
			backEnd.pc.c_staticVaoDraws, backEnd.pc.c_dynamicVaoDraws);
		ri.Printf( PRINT_ALL, "GLSL binds: %i  draws: gen %i light %i fog %i dlight %i\n",
			backEnd.pc.c_glslShaderBinds, backEnd.pc.c_genericDraws, backEnd.pc.c_lightallDraws, backEnd.pc.c_fogDraws, backEnd.pc.c_dlightDraws);
	}

	Com_Memset( &tr.pc, 0, sizeof( tr.pc ) );
	Com_Memset( &backEnd.pc, 0, sizeof( backEnd.pc ) );
}


/*
====================
R_IssueRenderCommands
====================
*/
void R_IssueRenderCommands( qboolean runPerformanceCounters ) {
	renderCommandList_t	*cmdList;

	if ( refHeadless ) {
		return;
	}

	cmdList = &backEndData->commands;
	assert(cmdList);
	// add an end-of-list command
	*(int *)(cmdList->cmds + cmdList->used) = RC_END_OF_LIST;

	// clear it out, in case this is a sync and not a buffer flip
	cmdList->used = 0;

	if ( runPerformanceCounters ) {
		R_PerformanceCounters();
	}

	// actually start the commands going
	if ( !r_skipBackEnd->integer ) {
		// let it start on the new batch
		RB_ExecuteRenderCommands( cmdList->cmds );
	}
}


/*
====================
R_IssuePendingRenderCommands

Issue any pending commands and wait for them to complete.
====================
*/
void R_IssuePendingRenderCommands( void ) {
	if ( !tr.registered ) {
		return;
	}
	R_IssueRenderCommands( qfalse );
}

/*
============
R_GetCommandBufferReserved

make sure there is enough command space
============
*/
void *R_GetCommandBufferReserved( int bytes, int reservedBytes ) {
	renderCommandList_t	*cmdList;

	cmdList = &backEndData->commands;
	bytes = PAD(bytes, sizeof(void *));

	// always leave room for the end of list command
	if ( cmdList->used + bytes + sizeof( int ) + reservedBytes > MAX_RENDER_COMMANDS ) {
		if ( bytes > MAX_RENDER_COMMANDS - sizeof( int ) ) {
			ri.Error( ERR_FATAL, "R_GetCommandBuffer: bad size %i", bytes );
		}
		// if we run out of room, just start dropping commands
		return NULL;
	}

	cmdList->used += bytes;

	return cmdList->cmds + cmdList->used - bytes;
}

/*
=============
R_GetCommandBuffer

returns NULL if there is not enough space for important commands
=============
*/
void *R_GetCommandBuffer( int bytes ) {
	return R_GetCommandBufferReserved( bytes, PAD( sizeof( swapBuffersCommand_t ), sizeof(void *) ) );
}


/*
=============
R_AddDrawSurfCmd

=============
*/
void	R_AddDrawSurfCmd( drawSurf_t *drawSurfs, int numDrawSurfs ) {
	drawSurfsCommand_t	*cmd;

	cmd = R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_DRAW_SURFS;

	cmd->drawSurfs = drawSurfs;
	cmd->numDrawSurfs = numDrawSurfs;

	cmd->refdef = tr.refdef;
	cmd->viewParms = tr.viewParms;
}


/*
=============
R_AddCapShadowmapCmd

=============
*/
void	R_AddCapShadowmapCmd( int map, int cubeSide ) {
	capShadowmapCommand_t	*cmd;

	cmd = R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_CAPSHADOWMAP;

	cmd->map = map;
	cmd->cubeSide = cubeSide;
}


/*
=============
R_AddPostProcessCmd

=============
*/
void	R_AddPostProcessCmd( void ) {
	postProcessCommand_t	*cmd;

	cmd = R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_POSTPROCESS;

	cmd->refdef = tr.refdef;
	cmd->viewParms = tr.viewParms;
}

/*
=============
RE_SetColor

Passing NULL will set the color to white
=============
*/
void	RE_SetColor( const float *rgba ) {
	setColorCommand_t	*cmd;

  if ( !tr.registered ) {
    return;
  }
	cmd = R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_SET_COLOR;
	if ( !rgba ) {
		static float colorWhite[4] = { 1, 1, 1, 1 };

		rgba = colorWhite;
	}

	cmd->color[0] = rgba[0];
	cmd->color[1] = rgba[1];
	cmd->color[2] = rgba[2];
	cmd->color[3] = rgba[3];
}


/*
=============
R_ClipRegion
=============
*/
static qboolean R_ClipRegion ( float *x, float *y, float *w, float *h,
		float *s1, float *t1, float *s2, float *t2 ) {
	float left, top, right, bottom;
	float _s1, _t1, _s2, _t2;
	float clipLeft, clipTop, clipRight, clipBottom;

	if (tr.clipRegion[2] <= tr.clipRegion[0] ||
		tr.clipRegion[3] <= tr.clipRegion[1] ) {
		return qfalse;
	}

	left = *x;
	top = *y;
	right = *x + *w;
	bottom = *y + *h;

	_s1 = *s1;
	_t1 = *t1;
	_s2 = *s2;
	_t2 = *t2;

	clipLeft = tr.clipRegion[0];
	clipTop = tr.clipRegion[1];
	clipRight = tr.clipRegion[2];
	clipBottom = tr.clipRegion[3];

	// Completely clipped away
	if ( right <= clipLeft || left >= clipRight ||
		bottom <= clipTop || top >= clipBottom ) {
		return qtrue;
	}

	// Clip left edge
	if ( left < clipLeft ) {
		float f = ( clipLeft - left ) / ( right - left );
		*s1 = ( f * ( _s2 - _s1 ) ) + _s1;
		*x = clipLeft;
		*w -= ( clipLeft - left );
	}

	// Clip right edge
	if ( right > clipRight ) {
		float f = ( clipRight - right ) / ( left - right );
		*s2 = ( f * ( _s1 - _s2 ) ) + _s2;
		*w = clipRight - *x;
	}

	// Clip top edge
	if ( top < clipTop ) {
		float f = ( clipTop - top ) / ( bottom - top );
		*t1 = ( f * ( _t2 - _t1 ) ) + _t1;
		*y = clipTop;
		*h -= ( clipTop - top );
	}

	// Clip bottom edge
	if ( bottom > clipBottom ) {
		float f = ( clipBottom - bottom ) / ( top - bottom );
		*t2 = ( f * ( _t1 - _t2 ) ) + _t2;
		*h = clipBottom - *y;
	}

	return qfalse;
}

/*
=============
RE_SetClipRegion
=============
*/
void RE_SetClipRegion( const float *region ) {
	if ( region == NULL ) {
		Com_Memset( tr.clipRegion, 0, sizeof( vec4_t ) );
	} else {
		Vector4Copy( region, tr.clipRegion );
	}
}


/*
=============
RE_StretchPic
=============
*/
void RE_StretchPic ( float x, float y, float w, float h, 
					  float s1, float t1, float s2, float t2, qhandle_t hShader ) {
	stretchPicCommand_t	*cmd;

	if (!tr.registered) {
		return;
	}
	if (R_ClipRegion(&x, &y, &w, &h, &s1, &t1, &s2, &t2)) {
		return;
	}
	cmd = R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_STRETCH_PIC;
	cmd->shader = R_GetShaderByHandle( hShader );
	cmd->x = x;
	cmd->y = y;
	cmd->w = w;
	cmd->h = h;
	cmd->s1 = s1;
	cmd->t1 = t1;
	cmd->s2 = s2;
	cmd->t2 = t2;
}

/*
=============
RE_RotatedPic
=============
*/
void RE_RotatedPic( float x, float y, float w, float h,
					float s1, float t1, float s2, float t2, qhandle_t hShader, float angle ) {
	stretchPicCommand_t *cmd;

	if ( !tr.registered ) {
		return;
	}

	cmd = R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_ROTATED_PIC;
	cmd->shader = R_GetShaderByHandle( hShader );
	cmd->x = x;
	cmd->y = y;
	cmd->w = w;
	cmd->h = h;

	// fixup
	cmd->w /= 2;
	cmd->h /= 2;
	cmd->x += cmd->w;
	cmd->y += cmd->h;
	cmd->w = sqrt( ( cmd->w * cmd->w ) + ( cmd->h * cmd->h ) );
	cmd->h = cmd->w;

	cmd->angle = angle;
	cmd->s1 = s1;
	cmd->t1 = t1;
	cmd->s2 = s2;
	cmd->t2 = t2;
}

/*
==============
RE_StretchPicGradient
==============
*/
void RE_StretchPicGradient( float x, float y, float w, float h,
							float s1, float t1, float s2, float t2, qhandle_t hShader, const float *gradientColor ) {
	stretchPicCommand_t *cmd;

	if ( !tr.registered ) {
		return;
	}

	cmd = R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_STRETCH_PIC_GRADIENT;
	cmd->shader = R_GetShaderByHandle( hShader );
	cmd->x = x;
	cmd->y = y;
	cmd->w = w;
	cmd->h = h;
	cmd->s1 = s1;
	cmd->t1 = t1;
	cmd->s2 = s2;
	cmd->t2 = t2;

	if ( !gradientColor ) {
		static float colorWhite[4] = { 1, 1, 1, 1 };

		gradientColor = colorWhite;
	}

	cmd->gradientColor[0] = gradientColor[0] * 255;
	cmd->gradientColor[1] = gradientColor[1] * 255;
	cmd->gradientColor[2] = gradientColor[2] * 255;
	cmd->gradientColor[3] = gradientColor[3] * 255;
}

/*
=============
RE_2DPolyies
=============
*/
extern int r_numpolyverts;

void RE_2DPolyies( polyVert_t* verts, int numverts, qhandle_t hShader ) {
	poly2dCommand_t* cmd;

	if ( !tr.registered ) {
		return;
	}

	if ( r_numpolyverts + numverts > max_polyverts ) {
		return;
	}

	cmd = R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}

	cmd->commandId =    RC_2DPOLYS;
	cmd->verts =        &backEndData->polyVerts[r_numpolyverts];
	cmd->numverts =     numverts;
	memcpy( cmd->verts, verts, sizeof( polyVert_t ) * numverts );
	cmd->shader =       R_GetShaderByHandle( hShader );

	r_numpolyverts += numverts;
}

/*
====================
RE_GetGlobalFog
====================
*/
void RE_GetGlobalFog( fogType_t *type, vec3_t color, float *depthForOpaque, float *density, float *farClip ) {
	if (type) {
		*type = tr.globalFogType;
	}

	if (color) {
		VectorCopy( tr.globalFogColor, color );
	}

	if (depthForOpaque) {
		*depthForOpaque = tr.globalFogDepthForOpaque;
	}

	if (density) {
		*density = tr.globalFogDensity;
	}

	if (farClip) {
		*farClip = tr.globalFogFarClip;
	}
}

/*
====================
R_PointInBrush

ZTM: TODO: Use bmodel->orientation.axis ?
====================
*/
qboolean R_PointInBrush( const vec3_t point, const bmodel_t *bmodel ) {
	int		j;
	const float	*origin = bmodel->orientation.origin;
	const float	*mins = bmodel->bounds[ 0 ];
	const float	*maxs = bmodel->bounds[ 1 ];

	for ( j = 0; j < 3; j++ )
	{
		if ( origin[ j ] + mins[ j ] >= point[ j ] ) {
			break;
		}
		if ( origin[ j ] + maxs[ j ] <= point[ j ] ) {
			break;
		}
	}

	return ( j == 3 );

}

/*
====================
RE_GetViewFog
====================
*/
void RE_GetViewFog( const vec3_t origin, fogType_t *type, vec3_t color, float *depthForOpaque, float *density, float *farClip, qboolean inwater ) {
	fogType_t			fogType;
	vec3_t				fogColor;
	float				fogDepthForOpaque;
	float				fogDensity;
	float				fogFarClip;

	if ( inwater ) {
		fogType = tr.waterFogParms.fogType;
		VectorCopy( tr.waterFogParms.color, fogColor );
		fogDepthForOpaque = tr.waterFogParms.depthForOpaque;
		fogDensity = tr.waterFogParms.density;
		fogFarClip = tr.waterFogParms.farClip;
	} else {
		fogType = FT_NONE;
		VectorSet( fogColor, 0, 0, 0 );
		fogDepthForOpaque = 0;
		fogDensity = 0;
		fogFarClip = 0;
	}

	if ( tr.world && tr.world->numBModels > 1 ) {
		int					bmodelNum;
		int					surfaceNum;
		const bmodel_t		*bmodel;
		const msurface_t	*surface;

		// skip world (bmodel 0)
		for ( bmodelNum = 1, bmodel = &tr.world->bmodels[1]; bmodelNum < tr.world->numBModels; bmodelNum++, bmodel++ ) {

			if ( !R_PointInBrush( origin, bmodel ) )
				continue;

			for ( surfaceNum = 0, surface = tr.world->surfaces + bmodel->firstSurface; surfaceNum < bmodel->numSurfaces; surfaceNum++, surface++ ) {
				if ( !surface->shader ) {
					continue;
				}

				if ( surface->shader->viewFogParms.fogType != FT_NONE
						|| surface->shader->viewFogParms.color[0]
						|| surface->shader->viewFogParms.color[1]
						|| surface->shader->viewFogParms.color[2] ) {
					fogType = surface->shader->viewFogParms.fogType;
					VectorCopy( surface->shader->viewFogParms.color, fogColor );
					fogDepthForOpaque = surface->shader->viewFogParms.depthForOpaque;
					fogDensity = surface->shader->viewFogParms.density;
					fogFarClip = surface->shader->viewFogParms.farClip;
					break;
				}
			}

			if ( surfaceNum != bmodel->numSurfaces ) {
				break;
			}
		}
	}

	if (type) {
		*type = fogType;
	}

	if (color) {
		VectorCopy( fogColor, color );
	}

	if (depthForOpaque) {
		*depthForOpaque = fogDepthForOpaque;
	}

	if (density) {
		*density = fogDensity;
	}

	if (farClip) {
		*farClip = fogFarClip;
	}
}

#define MODE_RED_CYAN	1
#define MODE_RED_BLUE	2
#define MODE_RED_GREEN	3
#define MODE_GREEN_MAGENTA 4
#define MODE_MAX	MODE_GREEN_MAGENTA

void R_SetColorMode(GLboolean *rgba, stereoFrame_t stereoFrame, int colormode)
{
	rgba[0] = rgba[1] = rgba[2] = rgba[3] = GL_TRUE;
	
	if(colormode > MODE_MAX)
	{
		if(stereoFrame == STEREO_LEFT)
			stereoFrame = STEREO_RIGHT;
		else if(stereoFrame == STEREO_RIGHT)
			stereoFrame = STEREO_LEFT;
		
		colormode -= MODE_MAX;
	}
	
	if(colormode == MODE_GREEN_MAGENTA)
	{
		if(stereoFrame == STEREO_LEFT)
			rgba[0] = rgba[2] = GL_FALSE;
		else if(stereoFrame == STEREO_RIGHT)
			rgba[1] = GL_FALSE;
	}
	else
	{
		if(stereoFrame == STEREO_LEFT)
			rgba[1] = rgba[2] = GL_FALSE;
		else if(stereoFrame == STEREO_RIGHT)
		{
			rgba[0] = GL_FALSE;
		
			if(colormode == MODE_RED_BLUE)
				rgba[1] = GL_FALSE;
			else if(colormode == MODE_RED_GREEN)
				rgba[2] = GL_FALSE;
		}
	}
}


/*
====================
RE_BeginFrame

If running in stereo, RE_BeginFrame will be called twice
for each RE_EndFrame
====================
*/
void RE_BeginFrame( stereoFrame_t stereoFrame ) {
	drawBufferCommand_t	*cmd = NULL;
	colorMaskCommand_t *colcmd = NULL;

	if ( !tr.registered ) {
		return;
	}
	glState.finishCalled = qfalse;

	tr.frameCount++;
	tr.frameSceneNum = 0;

	//
	// do overdraw measurement
	//
	if ( r_measureOverdraw->integer )
	{
		if ( qglesMajorVersion >= 1 && !glRefConfig.readStencil )
		{
			ri.Printf( PRINT_WARNING, "OpenGL ES needs GL_NV_read_stencil to read stencil bits to measure overdraw\n" );
			ri.Cvar_Set( "r_measureOverdraw", "0" );
			r_measureOverdraw->modified = qfalse;
		}
		else if ( glConfig.stencilBits < 4 )
		{
			ri.Printf( PRINT_ALL, "Warning: not enough stencil bits to measure overdraw: %d\n", glConfig.stencilBits );
			ri.Cvar_Set( "r_measureOverdraw", "0" );
			r_measureOverdraw->modified = qfalse;
		}
		else if ( r_shadows->integer == 2 )
		{
			ri.Printf( PRINT_ALL, "Warning: stencil shadows and overdraw measurement are mutually exclusive\n" );
			ri.Cvar_Set( "r_measureOverdraw", "0" );
			r_measureOverdraw->modified = qfalse;
		}
		else
		{
			R_IssuePendingRenderCommands();
			qglEnable( GL_STENCIL_TEST );
			qglStencilMask( ~0U );
			qglClearStencil( 0U );
			qglStencilFunc( GL_ALWAYS, 0U, ~0U );
			qglStencilOp( GL_KEEP, GL_INCR, GL_INCR );
		}
		r_measureOverdraw->modified = qfalse;
	}
	else
	{
		// this is only reached if it was on and is now off
		if ( r_measureOverdraw->modified ) {
			R_IssuePendingRenderCommands();
			qglDisable( GL_STENCIL_TEST );
		}
		r_measureOverdraw->modified = qfalse;
	}

	//
	// texturemode stuff
	//
	if ( r_textureMode->modified ) {
		R_IssuePendingRenderCommands();
		GL_TextureMode( r_textureMode->string );
		r_textureMode->modified = qfalse;
	}

	//
	// gamma stuff
	//
	if ( r_gamma->modified ) {
		r_gamma->modified = qfalse;

		R_IssuePendingRenderCommands();
		R_SetColorMappings();
	}

	// check for errors
	if ( !r_ignoreGLErrors->integer )
	{
		int	err;

		R_IssuePendingRenderCommands();
		if ((err = qglGetError()) != GL_NO_ERROR)
			ri.Error(ERR_FATAL, "RE_BeginFrame() - glGetError() failed (0x%x)!", err);
	}

	if (glConfig.stereoEnabled) {
		if( !(cmd = R_GetCommandBuffer(sizeof(*cmd))) )
			return;
			
		cmd->commandId = RC_DRAW_BUFFER;
		
		if ( stereoFrame == STEREO_LEFT ) {
			cmd->buffer = (int)GL_BACK_LEFT;
		} else if ( stereoFrame == STEREO_RIGHT ) {
			cmd->buffer = (int)GL_BACK_RIGHT;
		} else {
			ri.Error( ERR_FATAL, "RE_BeginFrame: Stereo is enabled, but stereoFrame was %i", stereoFrame );
		}
	}
	else
	{
		if (qglesMajorVersion >= 1 && r_anaglyphMode->integer)
		{
			ri.Printf( PRINT_WARNING, "OpenGL ES does not support drawing to separate buffer for anaglyph mode\n" );
			ri.Cvar_Set( "r_anaglyphMode", "0" );
			r_anaglyphMode->modified = qfalse;
		}

		if(r_anaglyphMode->integer)
		{
			if(r_anaglyphMode->modified)
			{
				// clear both, front and backbuffer.
				qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
				backEnd.colorMask[0] = GL_FALSE;
				backEnd.colorMask[1] = GL_FALSE;
				backEnd.colorMask[2] = GL_FALSE;
				backEnd.colorMask[3] = GL_FALSE;
								
				if (glRefConfig.framebufferObject)
				{
					// clear all framebuffers
					if (tr.msaaResolveFbo)
					{
						FBO_Bind(tr.msaaResolveFbo);
						qglClear(GL_COLOR_BUFFER_BIT);
					}

					if (tr.renderFbo)
					{
						FBO_Bind(tr.renderFbo);
						qglClear(GL_COLOR_BUFFER_BIT);
					}

					FBO_Bind(NULL);
				}

				qglDrawBuffer(GL_FRONT);
				qglClear(GL_COLOR_BUFFER_BIT);
				qglDrawBuffer(GL_BACK);
				qglClear(GL_COLOR_BUFFER_BIT);

				r_anaglyphMode->modified = qfalse;
			}
			
			if(stereoFrame == STEREO_LEFT)
			{
				if( !(cmd = R_GetCommandBuffer(sizeof(*cmd))) )
					return;
				
				if( !(colcmd = R_GetCommandBuffer(sizeof(*colcmd))) )
					return;
			}
			else if(stereoFrame == STEREO_RIGHT)
			{
				clearDepthCommand_t *cldcmd;
				
				if( !(cldcmd = R_GetCommandBuffer(sizeof(*cldcmd))) )
					return;

				cldcmd->commandId = RC_CLEARDEPTH;

				if( !(colcmd = R_GetCommandBuffer(sizeof(*colcmd))) )
					return;
			}
			else
				ri.Error( ERR_FATAL, "RE_BeginFrame: Stereo is enabled, but stereoFrame was %i", stereoFrame );

			R_SetColorMode(colcmd->rgba, stereoFrame, r_anaglyphMode->integer);
			colcmd->commandId = RC_COLORMASK;
		}
		else
		{
			if(stereoFrame != STEREO_CENTER)
				ri.Error( ERR_FATAL, "RE_BeginFrame: Stereo is disabled, but stereoFrame was %i", stereoFrame );

			if( !(cmd = R_GetCommandBuffer(sizeof(*cmd))) )
				return;
		}

		if(cmd)
		{
			cmd->commandId = RC_DRAW_BUFFER;

			if(r_anaglyphMode->modified)
			{
				qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
				backEnd.colorMask[0] = 0;
				backEnd.colorMask[1] = 0;
				backEnd.colorMask[2] = 0;
				backEnd.colorMask[3] = 0;
				r_anaglyphMode->modified = qfalse;
			}

			if (!Q_stricmp(r_drawBuffer->string, "GL_FRONT"))
				cmd->buffer = (int)GL_FRONT;
			else
				cmd->buffer = (int)GL_BACK;
		}
	}
	
	tr.refdef.stereoFrame = stereoFrame;
}


/*
=============
RE_EndFrame

Returns the number of msec spent in the back end
=============
*/
void RE_EndFrame( int *frontEndMsec, int *backEndMsec ) {
	swapBuffersCommand_t	*cmd;

	if ( !tr.registered ) {
		return;
	}
	cmd = R_GetCommandBufferReserved( sizeof( *cmd ), 0 );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_SWAP_BUFFERS;

	R_IssueRenderCommands( qtrue );

	R_InitNextFrame();

	if ( frontEndMsec ) {
		*frontEndMsec = tr.frontEndMsec;
	}
	tr.frontEndMsec = 0;
	if ( backEndMsec ) {
		*backEndMsec = backEnd.pc.msec;
	}
	backEnd.pc.msec = 0;
}

/*
=============
RE_TakeVideoFrame
=============
*/
void RE_TakeVideoFrame( int width, int height,
		byte *captureBuffer, byte *encodeBuffer, qboolean motionJpeg )
{
	videoFrameCommand_t	*cmd;

	if( !tr.registered ) {
		return;
	}

	cmd = R_GetCommandBuffer( sizeof( *cmd ) );
	if( !cmd ) {
		return;
	}

	cmd->commandId = RC_VIDEOFRAME;

	cmd->width = width;
	cmd->height = height;
	cmd->captureBuffer = captureBuffer;
	cmd->encodeBuffer = encodeBuffer;
	cmd->motionJpeg = motionJpeg;
}
