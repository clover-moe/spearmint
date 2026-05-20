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

// tr_shader.c -- this file deals with the parsing and definition of shaders

static char *s_shaderText;

// the shader is parsed into these global variables, then copied into
// dynamically allocated memory if it is valid.
static	shaderStage_t	stages[MAX_SHADER_STAGES];		
static	shader_t		shader;
static	texModInfo_t	texMods[MAX_SHADER_STAGES][NUM_TEXTURE_BUNDLES][TR_MAX_TEXMODS];
static	int				shader_realLightmapIndex;
static	image_t			*imageAnimations[MAX_SHADER_STAGES][NUM_TEXTURE_BUNDLES][MAX_IMAGE_ANIMATIONS];
static	imgFlags_t		shader_picmipFlag;
static	qboolean		shader_novlcollapse;
static	qboolean		shader_allowCompress;
static	qboolean		stage_ignore;

// these are here because they are only referenced while parsing a shader
static char implicitMap[ MAX_QPATH ];
static unsigned implicitStateBits;
static cullType_t implicitCullType;
static char aliasShader[ MAX_QPATH ] = { 0 };

#define FILE_HASH_SIZE		1024
static	shader_t*		hashTable[FILE_HASH_SIZE];

#define MAX_SHADERTEXT_HASH		2048
static char **shaderTextHashTable[MAX_SHADERTEXT_HASH];

static void ClearShaderStage( int num );

/*
================
return a hash value for the filename
================
*/
#ifdef __GNUCC__
  #warning TODO: check if long is ok here 
#endif
static long generateHashValue( const char *fname, const int size ) {
	int		i;
	long	hash;
	char	letter;

	hash = 0;
	i = 0;
	while (fname[i] != '\0') {
		letter = tolower(fname[i]);
		if (letter =='.') break;				// don't include extension
		if (letter =='\\') letter = '/';		// damn path names
		if (letter == PATH_SEP) letter = '/';		// damn path names
		hash+=(long)(letter)*(i+119);
		i++;
	}
	hash = (hash ^ (hash >> 10) ^ (hash >> 20));
	hash &= (size-1);
	return hash;
}

void R_RemapShader(const char *shaderName, const char *newShaderName, const char *timeOffset) {
	char		strippedName[MAX_QPATH];
	int			hash;
	shader_t	*sh, *sh2;
	qhandle_t	h;

	sh = R_FindShaderByName( shaderName );
	if (sh == NULL || sh == tr.defaultShader) {
		h = RE_RegisterShaderEx(shaderName, 0, qtrue);
		sh = R_GetShaderByHandle(h);
	}
	if (sh == NULL || sh == tr.defaultShader) {
		ri.Printf( PRINT_WARNING, "WARNING: R_RemapShader: shader %s not found\n", shaderName );
		return;
	}

	sh2 = R_FindShaderByName( newShaderName );
	if (sh2 == NULL || sh2 == tr.defaultShader) {
		h = RE_RegisterShaderEx(newShaderName, 0, qtrue);
		sh2 = R_GetShaderByHandle(h);
	}

	if (sh2 == NULL || sh2 == tr.defaultShader) {
		ri.Printf( PRINT_WARNING, "WARNING: R_RemapShader: new shader %s not found\n", newShaderName );
		return;
	}

	// remap all the shaders with the given name
	// even tho they might have different lightmaps
	COM_StripExtension(shaderName, strippedName, sizeof(strippedName));
	hash = generateHashValue(strippedName, FILE_HASH_SIZE);
	for (sh = hashTable[hash]; sh; sh = sh->next) {
		if (Q_stricmp(sh->name, strippedName) == 0) {
			if (sh != sh2) {
				sh->remappedShader = sh2;
			} else {
				sh->remappedShader = NULL;
			}
		}
	}
	if (timeOffset) {
		sh2->timeOffset = atof(timeOffset);
	}
}

/*
==============
R_DiffuseColorGen

It's implied that AGEN_SKIP can be used with any returned color gen.

Returns diffuse color gen for lightmap index.
==============
*/
static ID_INLINE colorGen_t R_DiffuseColorGen( int lightmapIndex ) {
	colorGen_t rgbGen;

	// lighting diffuse only works on entities, not 2D or world
	if ( lightmapIndex == LIGHTMAP_NONE ) {
		rgbGen = CGEN_LIGHTING_DIFFUSE;
	} else if ( lightmapIndex == LIGHTMAP_2D ) {
		rgbGen = CGEN_VERTEX;
	} else {
		// world; LIGHTMAP_BY_VERTEX, LIGHTMAP_WHITEIMAGE, or a real lightmap
		// already overbright shifted, so use exact
		rgbGen = CGEN_EXACT_VERTEX;
	}

	return rgbGen;
}

/*
==============
RE_SetSurfaceShader

Set shader for given world surface
==============
*/
void RE_SetSurfaceShader( int surfaceNum, const char *name ) {
	msurface_t	*surf;
	qboolean wasCustom, nowCustom;

	// remove the plus one offset
	surfaceNum--;

	if ( !tr.world || surfaceNum < 0 || surfaceNum >= tr.world->numsurfaces ) {
		return;
	}

	surf = &tr.world->surfaces[surfaceNum];

	wasCustom = ( surf->shader != surf->originalShader );

	if ( !name ) {
		surf->shader = surf->originalShader;
	} else if ( surf->originalShader ) {
		surf->shader = R_FindShader( name, surf->originalShader->lightmapIndex, MIP_RAW_IMAGE );
	} else {
		surf->shader = R_FindShader( name, LIGHTMAP_NONE, MIP_RAW_IMAGE );
	}

	nowCustom = ( surf->shader != surf->originalShader );

	if ( wasCustom != nowCustom ) {
		int i, j;
		mnode_t *leaf;
		int surfNum1;

		// update leaf to know if merged surfaces can be used
		for (i = 0; i < tr.world->numnodes - tr.world->numDecisionNodes; i++) {
			leaf = tr.world->nodes + tr.world->numDecisionNodes + i;

			for (j = 0; j < leaf->nummarksurfaces; j++) {
				surfNum1 = *(tr.world->marksurfaces + leaf->firstmarksurface + j);

				if ( surfNum1 == surfaceNum ) {
					if ( nowCustom ) {
						leaf->numCustomShaders++;
					} else {
						leaf->numCustomShaders--;
					}
					break;
				}
			}
		}
	}
}

/*
==============
RE_GetSurfaceShader

return a shader index for a given world surface

lightmapIndex=LIGHTMAP_NONE will create a new shader that is a copy of the one found
on the model, without the lighmap stage, if the shader has a lightmap stage

lightmapIndex=LIGHTMAP_2D will create shader for 2D UI/HUD usage.

lightmapIndex >= 0 will use the default lightmap for the surface.
==============
*/
qhandle_t RE_GetSurfaceShader( int surfaceNum, int lightmapIndex ) {
	msurface_t	*surf;
	shader_t	*shd;
	int			i;

	// remove the plus one offset
	surfaceNum--;

	if ( !tr.world || surfaceNum < 0 || surfaceNum >= tr.world->numsurfaces ) {
		return 0;
	}

	surf = &tr.world->surfaces[surfaceNum];

	// RF, check for null shader (can happen on func_explosive's with botclips attached)
	if ( !surf->shader ) {
		return 0;
	}

	if ( lightmapIndex < 0 && surf->shader->lightmapIndex != lightmapIndex ) {
		shd = R_FindShader( surf->shader->name, lightmapIndex, MIP_RAW_IMAGE );

		// ZTM: FIXME: I'm not sure forcing lighting diffuse is good idea...
		//             at least allow const and waveform
		for ( i = 0 ; i < MAX_SHADER_STAGES ; i++ ) {
			if ( !shd->stages[i] ) {
				break;
			}

			if ( shd->stages[i]->rgbGen != CGEN_CONST && shd->stages[i]->rgbGen != CGEN_WAVEFORM
					&& shd->stages[i]->rgbGen != CGEN_COLOR_WAVEFORM ) {
				shd->stages[i]->rgbGen = R_DiffuseColorGen( lightmapIndex );
			}

			if ( lightmapIndex == LIGHTMAP_2D && shd->stages[i]->alphaGen != AGEN_CONST && shd->stages[i]->alphaGen != AGEN_WAVEFORM ) {
				shd->stages[i]->alphaGen = AGEN_IDENTITY;
			}
		}
	} else {
		shd = surf->shader;
	}

	return shd->index;
}

/*
==============
RE_GetShaderFromModel

return a shader index for a given model's surface
'lightmapIndex' set to LIGHTMAP_NONE will create a new shader that is a copy of the one found
on the model, without the lighmap stage, if the shader has a lightmap stage

NOTE: only works for bmodels right now.  Could modify for other models (md3's etc.)
==============
*/
qhandle_t RE_GetShaderFromModel( qhandle_t hModel, int surfnum, int lightmapIndex ) {
	model_t		*model;
	bmodel_t	*bmodel;

	model = R_GetModelByHandle( hModel );

	if ( model ) {
		bmodel = model->bmodel;
		if ( bmodel && bmodel->firstSurface ) {
			if ( surfnum < 0 || surfnum >= bmodel->numSurfaces ) {
				surfnum = 0;
			}

			return RE_GetSurfaceShader( bmodel->firstSurface + surfnum + 1, lightmapIndex );
		}
	}

	return 0;
}

/*
==============
RE_GetShaderName
==============
*/
void RE_GetShaderName( qhandle_t hShader, char *buffer, int bufferSize ) {
	shader_t	*shader;

	shader = R_GetShaderByHandle( hShader );

	Q_strncpyz( buffer, shader->name, bufferSize );
}

/*
===============
ParseIf

textures/name/etc
{
  mipmaps // etc
if novertexlight
  ... shader stages
endif
if vertexlight
  ... shader stage
endif
}

FAKK2 keywords: 0, 1,  mtex, and no_mtex
Alice keywords: shaderlod <float-value>
EF2 keywords: [no]vertexlight, [no]detail
(Alice and EF2 use some FAKK2 keywords as well.)

New: Support "if cvar" and "if !cvar"

===============
*/
static qboolean ParseIf( char **text ) {
	char	*token;
	char	*var;
	qboolean wantValue;
	int		value;

	token = COM_ParseExt( text, qfalse );
	if ( !token[0] ) {
		ri.Printf( PRINT_WARNING, "WARNING: missing parm for 'if' keyword in shader '%s'\n", shader.name );
		return qfalse;
	}

	if ( !Q_stricmp( token, "1" ) ) {
		return qtrue;
	}
	else if ( !Q_stricmp( token, "0" ) ) {
		// always skip
	}
	// shaderlod <float-value>
	else if ( !Q_stricmp( token, "shaderlod" ) ) {
		token = COM_ParseExt( text, qfalse );
		if ( !token[0] ) {
			ri.Printf( PRINT_WARNING, "WARNING: missing lod value after 'if shaderlod' in shader '%s'\n", shader.name );
			return qtrue;
		}

		if ( r_shaderlod->value > atof( token ) ) {
			return qtrue;
		}
	} else {
		// support no and ! prefix
		if ( !Q_stricmpn( token, "no", 2 ) ) {
			wantValue = qfalse;
			var = token+2;
		}
		else if ( !Q_stricmpn( token, "!", 1 ) ) {
			wantValue = qfalse;
			var = token+1;
		}
		else {
			wantValue = qtrue;
			var = token;
		}

		if ( !Q_stricmp( var, "mtex" ) || !Q_stricmp( token, "no_mtex" ) ) {
			value = ( qglActiveTextureARB != NULL );
		}
		else if ( !Q_stricmp( var, "vertexlight" ) ) {
			value = r_vertexLight->integer;
		}
		else if ( !Q_stricmp( var, "detail" ) ) {
			value = r_detailTextures->integer;
		}
		else {
			// allow checking any cvar (defaults to 0 value if cvar does not exist)
			value = ri.Cvar_VariableIntegerValue( var );
		}

		if ( !wantValue && !value ) {
			return qtrue;
		}
		else if ( wantValue && value ) {
			return qtrue;
		}
	}

	return qfalse;
}

#define MAX_IF_RECURSION 10
static qboolean dealtWithElse[MAX_IF_RECURSION];
static qboolean SkipIfBlock( char **text, int *ifIndent, int braketLevel ) {
	char	*token;
	int		indent;
	int		initialIfIndent;

	indent = braketLevel;

	initialIfIndent = *ifIndent;

	// Skip tokens inside of if-block
	while ( 1 ) {
		token = COM_ParseExt( text, qtrue );
		if ( !token[0] ) {
			ri.Printf( PRINT_WARNING, "WARNING: no concluding '}' in shader %s\n", shader.name );
			return qfalse;
		}

		if ( token[0] == '}' && indent == 1 ) {
			ri.Printf( PRINT_WARNING, "WARNING: no concluding 'endif' in shader %s\n", shader.name );
			return qfalse;
		}

		if ( token[0] == '}' ) {
			indent--;
		}
		else if ( token[0] == '{' ) {
			indent++;
		}
		else if ( !Q_stricmp( token, "if" ) || !Q_stricmp( token, "#if" ) ) {
			*ifIndent = *ifIndent + 1;
			if ( *ifIndent >= MAX_IF_RECURSION ) {
				*ifIndent = 0;
				ri.Printf( PRINT_WARNING, "WARNING: 'if' recursion level goes too high (max %d) shader %s\n", MAX_IF_RECURSION, shader.name );
				return 0;
			}
			dealtWithElse[ *ifIndent ] = qfalse;
		}
		else if ( !Q_stricmp( token, "else" ) || !Q_stricmp( token, "#else" ) ) {
			if ( dealtWithElse[ *ifIndent ] ) {
				ri.Printf( PRINT_WARNING, "WARNING: Unexpected 'else' in shader %s\n", shader.name );
				return qfalse;
			}
			dealtWithElse[ *ifIndent ] = qtrue;

			if ( *ifIndent == initialIfIndent ) {
				break;
			}
		}
		else if ( !Q_stricmp( token, "endif" ) || !Q_stricmp( token, "#endif" ) ) {
			*ifIndent = *ifIndent - 1;

			if ( *ifIndent == initialIfIndent - 1 ) {
				break;
			}
		}
	}

	return qtrue;
}

/*
===============
ParseIfEndif
===============
*/
static int ParseIfEndif( char **text, char *token, int *ifIndent, int braketLevel ) {

	if ( !Q_stricmp( token, "if" ) || !Q_stricmp( token, "#if" ) )
	{
		*ifIndent = *ifIndent + 1;
		if ( *ifIndent >= MAX_IF_RECURSION ) {
			*ifIndent = 0;
			ri.Printf( PRINT_WARNING, "WARNING: 'if' recursion level goes too high (max %d) shader %s\n", MAX_IF_RECURSION, shader.name );
			return 0;
		}
		dealtWithElse[ *ifIndent ] = qfalse;

		// if the 'if' is false, skip block
		if ( !ParseIf( text ) && !SkipIfBlock( text, ifIndent, braketLevel ) ) {
			return 0;
		}
		return 1;
	}
	// we only get an else here through error or the 'if' was true
	else if ( !Q_stricmp( token, "else" ) || !Q_stricmp( token, "#else" ) )
	{
		if ( *ifIndent < 1 ) {
			*ifIndent = 0;
			ri.Printf( PRINT_WARNING, "WARNING: 'else' without 'if' in shader %s\n", shader.name );
			return 0;
		}
		if ( dealtWithElse[ *ifIndent ] ) {
			*ifIndent = 0;
			ri.Printf( PRINT_WARNING, "WARNING: Unexpected 'else' in shader %s\n", shader.name );
			return 0;
		}
		dealtWithElse[ *ifIndent ] = qtrue;

		// skip block
		if ( !SkipIfBlock( text, ifIndent, braketLevel ) ) {
			return 0;
		}
		return 1;
	}
	else if ( !Q_stricmp( token, "endif" ) || !Q_stricmp( token, "#endif" ) )
	{
		*ifIndent = *ifIndent - 1;
		if ( *ifIndent < 0 ) {
			*ifIndent = 0;
			ri.Printf( PRINT_WARNING, "WARNING: 'endif' with no 'if' in shader %s\n", shader.name );
			return 0;
		}
		return 1;
	}

	return -1; // not our token
}

/*
===============
ParseOptionalToken
===============
*/
static qboolean ParseOptionalToken( char **text, const char *expected ) {
	char *oldText = *text;
	char *token;

	token = COM_ParseExt( text, qfalse );
	if ( strcmp( token, expected ) ) {
		*text = oldText;
		return qfalse;
	}

	return qtrue;
}

/*
===============
ParseVector
===============
*/
static qboolean ParseVector( char **text, int count, float *v ) {
	qboolean openParen, closeParen;
	char	*token, *end;
	int		i;

	// FIXME: spaces are currently required after parens, should change parseext...
	openParen = ParseOptionalToken( text, "(" );

	for ( i = 0 ; i < count ; i++ ) {
		token = COM_ParseExt( text, qfalse );
		if ( !token[0] ) {
			ri.Printf( PRINT_WARNING, "WARNING: missing vector element in shader '%s'\n", shader.name );
			return qfalse;
		}
		v[i] = strtod( token, &end );
		if ( *end != '\0' ) {
			ri.Printf( PRINT_WARNING, "WARNING: vector element '%s' in shader '%s' is not a number\n", token, shader.name );
		}
	}

	closeParen = ParseOptionalToken( text, ")" );
	if ( closeParen != openParen ) {
		ri.Printf( PRINT_WARNING, "WARNING: missing parenthesis in shader '%s'\n", shader.name );
		return qfalse;
	}

	return qtrue;
}


/*
===============
NameToAFunc
===============
*/
static unsigned NameToAFunc( const char *funcname )
{	
	if ( !Q_stricmp( funcname, "GT0" ) )
	{
		return GLS_ATEST_GT_0;
	}
	else if ( !Q_stricmp( funcname, "LT128" ) )
	{
		return GLS_ATEST_LT_80;
	}
	else if ( !Q_stricmp( funcname, "GE128" ) )
	{
		return GLS_ATEST_GE_80;
	}
	else if ( !Q_stricmp( funcname, "GE192" ) )
	{
		return GLS_ATEST_GE_C0;
	}

	ri.Printf( PRINT_WARNING, "WARNING: invalid alphaFunc name '%s' in shader '%s'\n", funcname, shader.name );
	return 0;
}


/*
===============
NameToSrcBlendMode
===============
*/
static int NameToSrcBlendMode( const char *name )
{
	if ( !Q_stricmp( name, "GL_ONE" ) )
	{
		return GLS_SRCBLEND_ONE;
	}
	else if ( !Q_stricmp( name, "GL_ZERO" ) )
	{
		return GLS_SRCBLEND_ZERO;
	}
	else if ( !Q_stricmp( name, "GL_DST_COLOR" ) )
	{
		return GLS_SRCBLEND_DST_COLOR;
	}
	else if ( !Q_stricmp( name, "GL_ONE_MINUS_DST_COLOR" ) )
	{
		return GLS_SRCBLEND_ONE_MINUS_DST_COLOR;
	}
	else if ( !Q_stricmp( name, "GL_SRC_ALPHA" ) )
	{
		return GLS_SRCBLEND_SRC_ALPHA;
	}
	else if ( !Q_stricmp( name, "GL_ONE_MINUS_SRC_ALPHA" ) )
	{
		return GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA;
	}
	else if ( !Q_stricmp( name, "GL_DST_ALPHA" ) )
	{
		if (r_ignoreDstAlpha->integer)
			return GLS_SRCBLEND_ONE;

		return GLS_SRCBLEND_DST_ALPHA;
	}
	else if ( !Q_stricmp( name, "GL_ONE_MINUS_DST_ALPHA" ) )
	{
		if (r_ignoreDstAlpha->integer)
			return GLS_SRCBLEND_ZERO;

		return GLS_SRCBLEND_ONE_MINUS_DST_ALPHA;
	}
	else if ( !Q_stricmp( name, "GL_SRC_ALPHA_SATURATE" ) )
	{
		return GLS_SRCBLEND_ALPHA_SATURATE;
	}

	ri.Printf( PRINT_WARNING, "WARNING: unknown blend mode '%s' in shader '%s', substituting GL_ONE\n", name, shader.name );
	return GLS_SRCBLEND_ONE;
}

/*
===============
NameToDstBlendMode
===============
*/
static int NameToDstBlendMode( const char *name )
{
	if ( !Q_stricmp( name, "GL_ONE" ) )
	{
		return GLS_DSTBLEND_ONE;
	}
	else if ( !Q_stricmp( name, "GL_ZERO" ) )
	{
		return GLS_DSTBLEND_ZERO;
	}
	else if ( !Q_stricmp( name, "GL_SRC_ALPHA" ) )
	{
		return GLS_DSTBLEND_SRC_ALPHA;
	}
	else if ( !Q_stricmp( name, "GL_ONE_MINUS_SRC_ALPHA" ) )
	{
		return GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
	}
	else if ( !Q_stricmp( name, "GL_DST_ALPHA" ) )
	{
		if (r_ignoreDstAlpha->integer)
			return GLS_DSTBLEND_ONE;

		return GLS_DSTBLEND_DST_ALPHA;
	}
	else if ( !Q_stricmp( name, "GL_ONE_MINUS_DST_ALPHA" ) )
	{
		if (r_ignoreDstAlpha->integer)
			return GLS_DSTBLEND_ZERO;

		return GLS_DSTBLEND_ONE_MINUS_DST_ALPHA;
	}
	else if ( !Q_stricmp( name, "GL_SRC_COLOR" ) )
	{
		return GLS_DSTBLEND_SRC_COLOR;
	}
	else if ( !Q_stricmp( name, "GL_ONE_MINUS_SRC_COLOR" ) )
	{
		return GLS_DSTBLEND_ONE_MINUS_SRC_COLOR;
	}

	ri.Printf( PRINT_WARNING, "WARNING: unknown blend mode '%s' in shader '%s', substituting GL_ONE\n", name, shader.name );
	return GLS_DSTBLEND_ONE;
}

/*
===============
NameToGenFunc
===============
*/
static genFunc_t NameToGenFunc( const char *funcname )
{
	if ( !Q_stricmp( funcname, "sin" ) )
	{
		return GF_SIN;
	}
	else if ( !Q_stricmp( funcname, "square" ) )
	{
		return GF_SQUARE;
	}
	else if ( !Q_stricmp( funcname, "triangle" ) )
	{
		return GF_TRIANGLE;
	}
	else if ( !Q_stricmp( funcname, "sawtooth" ) )
	{
		return GF_SAWTOOTH;
	}
	else if ( !Q_stricmp( funcname, "inversesawtooth" ) )
	{
		return GF_INVERSE_SAWTOOTH;
	}
	else if ( !Q_stricmp( funcname, "noise" ) )
	{
		return GF_NOISE;
	}
	else if ( !Q_stricmp( funcname, "random" ) )
	{
		return GF_RANDOM;
	}

	ri.Printf( PRINT_WARNING, "WARNING: invalid genfunc name '%s' in shader '%s'\n", funcname, shader.name );
	return GF_SIN;
}


/*
===================
ParseWaveForm
===================
*/
static void ParseWaveForm( char **text, waveForm_t *wave )
{
	char *token;

	token = COM_ParseExt( text, qfalse );
	if ( token[0] == 0 )
	{
		ri.Printf( PRINT_WARNING, "WARNING: missing waveform parm in shader '%s'\n", shader.name );
		return;
	}
	wave->func = NameToGenFunc( token );

	// BASE, AMP, PHASE, FREQ
	token = COM_ParseExt( text, qfalse );
	if ( token[0] == 0 )
	{
		ri.Printf( PRINT_WARNING, "WARNING: missing waveform parm in shader '%s'\n", shader.name );
		return;
	}
	wave->base = atof( token );

	token = COM_ParseExt( text, qfalse );
	if ( token[0] == 0 )
	{
		ri.Printf( PRINT_WARNING, "WARNING: missing waveform parm in shader '%s'\n", shader.name );
		return;
	}
	wave->amplitude = atof( token );

	token = COM_ParseExt( text, qfalse );
	if ( token[0] == 0 )
	{
		ri.Printf( PRINT_WARNING, "WARNING: missing waveform parm in shader '%s'\n", shader.name );
		return;
	}
	wave->phase = atof( token );

	token = COM_ParseExt( text, qfalse );
	if ( token[0] == 0 )
	{
		ri.Printf( PRINT_WARNING, "WARNING: missing waveform parm in shader '%s'\n", shader.name );
		return;
	}
	wave->frequency = atof( token );
}


/*
===================
ParseTexMod
===================
*/
static void ParseTexMod( char *_text, textureBundle_t *bundle )
{
	const char *token;
	char **text = &_text;
	texModInfo_t *tmi;

	if ( bundle->numTexMods == TR_MAX_TEXMODS ) {
		ri.Error( ERR_DROP, "ERROR: too many tcMod stages in shader '%s'", shader.name );
		return;
	}

	tmi = &bundle->texMods[bundle->numTexMods];
	bundle->numTexMods++;

	token = COM_ParseExt( text, qfalse );

	//
	// turb
	//
	if ( !Q_stricmp( token, "turb" ) )
	{
		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing tcMod turb parms in shader '%s'\n", shader.name );
			return;
		}
		tmi->wave.base = atof( token );
		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing tcMod turb in shader '%s'\n", shader.name );
			return;
		}
		tmi->wave.amplitude = atof( token );
		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing tcMod turb in shader '%s'\n", shader.name );
			return;
		}
		tmi->wave.phase = atof( token );
		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing tcMod turb in shader '%s'\n", shader.name );
			return;
		}
		tmi->wave.frequency = atof( token );

		tmi->type = TMOD_TURBULENT;
	}
	//
	// scale
	//
	else if ( !Q_stricmp( token, "scale" ) )
	{
		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing scale parms in shader '%s'\n", shader.name );
			return;
		}
		tmi->scale[0] = atof( token );

		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing scale parms in shader '%s'\n", shader.name );
			return;
		}
		tmi->scale[1] = atof( token );
		tmi->type = TMOD_SCALE;
	}
	//
	// scroll
	//
	else if ( !Q_stricmp( token, "scroll" ) )
	{
		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing scale scroll parms in shader '%s'\n", shader.name );
			return;
		}
		tmi->scroll[0] = atof( token );
		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing scale scroll parms in shader '%s'\n", shader.name );
			return;
		}
		tmi->scroll[1] = atof( token );
		tmi->type = TMOD_SCROLL;
	}
	//
	// stretch
	//
	else if ( !Q_stricmp( token, "stretch" ) )
	{
		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing stretch parms in shader '%s'\n", shader.name );
			return;
		}
		tmi->wave.func = NameToGenFunc( token );

		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing stretch parms in shader '%s'\n", shader.name );
			return;
		}
		tmi->wave.base = atof( token );

		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing stretch parms in shader '%s'\n", shader.name );
			return;
		}
		tmi->wave.amplitude = atof( token );

		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing stretch parms in shader '%s'\n", shader.name );
			return;
		}
		tmi->wave.phase = atof( token );

		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing stretch parms in shader '%s'\n", shader.name );
			return;
		}
		tmi->wave.frequency = atof( token );
		
		tmi->type = TMOD_STRETCH;
	}
	//
	// transform
	//
	else if ( !Q_stricmp( token, "transform" ) )
	{
		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing transform parms in shader '%s'\n", shader.name );
			return;
		}
		tmi->matrix[0][0] = atof( token );

		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing transform parms in shader '%s'\n", shader.name );
			return;
		}
		tmi->matrix[0][1] = atof( token );

		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing transform parms in shader '%s'\n", shader.name );
			return;
		}
		tmi->matrix[1][0] = atof( token );

		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing transform parms in shader '%s'\n", shader.name );
			return;
		}
		tmi->matrix[1][1] = atof( token );

		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing transform parms in shader '%s'\n", shader.name );
			return;
		}
		tmi->translate[0] = atof( token );

		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing transform parms in shader '%s'\n", shader.name );
			return;
		}
		tmi->translate[1] = atof( token );

		tmi->type = TMOD_TRANSFORM;
	}
	//
	// rotate
	//
	else if ( !Q_stricmp( token, "rotate" ) )
	{
		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing tcMod rotate parms in shader '%s'\n", shader.name );
			return;
		}
		tmi->rotateSpeed = atof( token );
		tmi->type = TMOD_ROTATE;
	}
	//
	// entityTranslate
	//
	else if ( !Q_stricmp( token, "entityTranslate" ) )
	{
		tmi->type = TMOD_ENTITY_TRANSLATE;
	}
	//
	// offset
	//
	else if ( !Q_stricmp( token, "offset" ) )
	{
		tmi->matrix[0][0] = 1;
		tmi->matrix[0][1] = 0;
		tmi->matrix[1][0] = 0;
		tmi->matrix[1][1] = 1;

		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing tcMod offset parms in shader '%s'\n", shader.name );
			return;
		}
		tmi->translate[0] = atof( token );

		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing tcMod offset parms in shader '%s'\n", shader.name );
			return;
		}
		tmi->translate[1] = atof( token );

		tmi->type = TMOD_TRANSFORM;
	}
	//
	// swap
	//
	else if ( !Q_stricmp( token, "swap" ) )
	{
		// swap S and T
		tmi->matrix[0][0] = 0;
		tmi->matrix[0][1] = 1;
		tmi->matrix[1][0] = 1;
		tmi->matrix[1][1] = 0;
		tmi->translate[0] = 0;
		tmi->translate[1] = 0;

		tmi->type = TMOD_TRANSFORM;
	}
	else
	{
		ri.Printf( PRINT_WARNING, "WARNING: unknown tcMod '%s' in shader '%s'\n", token, shader.name );
	}
}


/*
===================
ParseStage
===================
*/
static qboolean ParseStage( shaderStage_t *stage, char **text, int *ifIndent )
{
	char keyword[64];
	char *token;
	int depthMaskBits = GLS_DEPTHMASK_TRUE, blendSrcBits = 0, blendDstBits = 0, atestBits = 0, depthFuncBits = 0, depthTestBits = 0;
	qboolean depthMaskExplicit = qfalse;
	qboolean skipRestOfLine = qfalse;
	qboolean clampMap = qfalse;
	qboolean clampTexCoords = qfalse;
	qboolean stage_noMipMaps = shader.noMipMaps;
	qboolean stage_noPicMip = shader.noPicMip;
	int stage_picmipFlag = shader_picmipFlag;
	int currentBundle = 0;
	textureBundle_t *bundle = &stage->bundle[0];
	char imageNames[2/*MAX_TEXTURE_BUNDLES*/][MAX_IMAGE_ANIMATIONS][MAX_QPATH];
	int numImageNames[2/*MAX_TEXTURE_BUNDLES*/] = {0};
	int i;

	stage->active = qtrue;

	while ( 1 )
	{
		// skip unused data for known parameters, but not the initial {
		if ( skipRestOfLine ) {
			SkipRestOfLineUntilBrace( text );
		}
		skipRestOfLine = qtrue;

		token = COM_ParseExt( text, qtrue );
		if ( !token[0] )
		{
			ri.Printf( PRINT_WARNING, "WARNING: no matching '}' found\n" );
			return qfalse;
		}

		Q_strncpyz( keyword, token, sizeof ( keyword ) );

		switch ( ParseIfEndif( text, token, ifIndent, 2 ) )
		{
			case 0:
				return qfalse;
			case 1:
				continue;
			default:
				break;
		}

		if ( token[0] == '}' )
		{
			break;
		}

		//
		// check special case for map16/map32/mapcomp/mapnocomp (compression enabled)
		//
		if ( !Q_stricmp( token, "map16" ) ) {    // only use this texture if 16 bit color depth
			if ( glConfig.colorBits <= 16 ) {
				token = "map";   // use this map
			} else {
				COM_ParseExt( text, qfalse );   // ignore the map
				continue;
			}
		} else if ( !Q_stricmp( token, "map32" ) )    { // only use this texture if more than 16 bit color depth
			if ( glConfig.colorBits > 16 ) {
				token = "map";   // use this map
			} else {
				COM_ParseExt( text, qfalse );   // ignore the map
				continue;
			}
		} else if ( !Q_stricmp( token, "mapcomp" ) )    { // only use this texture if compression is enabled
			if ( glConfig.textureCompression != TC_NONE ) {
				token = "map";   // use this map
			} else {
				COM_ParseExt( text, qfalse );   // ignore the map
				continue;
			}
		} else if ( !Q_stricmp( token, "mapnocomp" ) )    { // only use this texture if compression is not available or disabled
			if ( glConfig.textureCompression == TC_NONE ) {
				token = "map";   // use this map
			} else {
				COM_ParseExt( text, qfalse );   // ignore the map
				continue;
			}
		} else if ( !Q_stricmp( token, "animmapcomp" ) )    { // only use this texture if compression is enabled
			if ( glConfig.textureCompression != TC_NONE ) {
				token = "animmap";   // use this map
			} else {
				while ( token[0] ) {
					token = COM_ParseExt( text, qfalse );   // ignore the map
				}
				continue;
			}
		} else if ( !Q_stricmp( token, "animmapnocomp" ) )    { // only use this texture if compression is not available or disabled
			if ( glConfig.textureCompression == TC_NONE ) {
				token = "animmap";   // use this map
			} else {
				while ( token[0] ) {
					token = COM_ParseExt( text, qfalse );   // ignore the map
				}
				continue;
			}
		}

		//
		// map <name>
		//
		if ( !Q_stricmp( token, "map" ) )
		{
			token = COM_ParseExt( text, qfalse );
			if ( !token[0] )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing parameter for '%s' keyword in shader '%s'\n", keyword, shader.name );
				return qfalse;
			}

			if ( !Q_stricmp( token, "$whiteimage" ) || !Q_stricmp( token, "*white" ) ) {
				bundle->image[0] = tr.whiteImage;
				continue;
			}
			else if ( !Q_stricmp( token, "$dlight" ) ) {
				bundle->image[0] = tr.dlightImage;
				continue;
			}
			else if ( !Q_stricmp( token, "$lightmap" ) )
			{
				bundle->isLightmap = qtrue;
				if ( shader.lightmapIndex < 0 || !tr.lightmaps ) {
					bundle->image[0] = tr.whiteImage;
				} else {
					bundle->image[0] = tr.lightmaps[shader.lightmapIndex];
				}
				continue;
			}
			else if ( !Q_stricmp( token, "$deluxemap" ) )
			{
				if (!tr.worldDeluxeMapping)
				{
					ri.Printf( PRINT_WARNING, "WARNING: shader '%s' wants a deluxe map in a map compiled without them\n", shader.name );
					return qfalse;
				}

				bundle->isLightmap = qtrue;
				if ( shader.lightmapIndex < 0 ) {
					bundle->image[0] = tr.whiteImage;
				} else {
					bundle->image[0] = tr.deluxemaps[shader.lightmapIndex];
				}
				continue;
			}
			else
			{
				Q_strncpyz( imageNames[currentBundle][0], token, sizeof( imageNames[currentBundle][0] ) );
				numImageNames[currentBundle] = 1;
				clampMap = qfalse;
			}
		}
		//
		// clampmap <name>
		//
		else if ( !Q_stricmp( token, "clampmap" ) )
		{
			token = COM_ParseExt( text, qfalse );
			if ( !token[0] )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing parameter for 'clampmap' keyword in shader '%s'\n", shader.name );
				return qfalse;
			}

			Q_strncpyz( imageNames[currentBundle][0], token, sizeof( imageNames[currentBundle][0] ) );
			numImageNames[currentBundle] = 1;
			clampMap = qtrue;
		}
		//
		// clampTexCoords
		//
		else if ( !Q_stricmp( token, "clampTexCoords" ) )
		{
			clampTexCoords = qtrue;
		}
		//
		// lightmap <name>
		//
		else if ( !Q_stricmp( token, "lightmap" ) ) {
			token = COM_ParseExt( text, qfalse );
			if ( !token[0] ) {
				ri.Printf( PRINT_WARNING, "WARNING: missing parameter for 'lightmap' keyword in shader '%s'\n", shader.name );
				return qfalse;
			}

			if ( !Q_stricmp( token, "$whiteimage" ) || !Q_stricmp( token, "*white" ) ) {
				bundle->image[0] = tr.whiteImage;
				continue;
			}
			else if ( !Q_stricmp( token, "$dlight" ) ) {
				bundle->image[0] = tr.dlightImage;
				continue;
			}
			else if ( !Q_stricmp( token, "$lightmap" ) ) {
				bundle->isLightmap = qtrue;
				if ( shader.lightmapIndex < 0 || !tr.lightmaps ) {
					bundle->image[0] = tr.whiteImage;
				} else {
					bundle->image[0] = tr.lightmaps[shader.lightmapIndex];
				}
				continue;
			} else {
				bundle->image[0] = R_FindImageFile( token, IMGTYPE_COLORALPHA,
						IMGFLAG_LIGHTMAP | IMGFLAG_NOLIGHTSCALE | IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE );
				if ( !bundle->image[0] ) {
					ri.Printf( PRINT_WARNING, "WARNING: R_FindImageFile could not find '%s' in shader '%s'\n", token, shader.name );
					return qfalse;
				}
				bundle->isLightmap = qtrue;
			}
		}
		//
		// animMap <frequency> <image1> .... <imageN>
		//
		else if ( !Q_stricmp( token, "animMap" ) || !Q_stricmp( token, "clampAnimMap" ) || !Q_stricmp( token, "oneshotAnimMap" ) || !Q_stricmp( token, "oneshotClampAnimMap" ) )
		{
			int	totalImages = 0;

			bundle->loopingImageAnim = qtrue;
			clampMap = qfalse;

			if (!Q_stricmp( token, "clampAnimMap" )) {
				clampMap = qtrue;
			}
			else if (!Q_stricmp( token, "oneshotAnimMap" )) {
				bundle->loopingImageAnim = qfalse;
			}
			else if (!Q_stricmp( token, "oneshotClampAnimMap" )) {
				clampMap = qtrue;
				bundle->loopingImageAnim = qfalse;
			}

			token = COM_ParseExt( text, qfalse );
			if ( !token[0] )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing parameter for '%s' keyword in shader '%s'\n", keyword, shader.name );
				return qfalse;
			}
			bundle->imageAnimationSpeed = atof( token );

			numImageNames[currentBundle] = 0;

			// parse up to MAX_IMAGE_ANIMATIONS animations
			while ( 1 ) {
				int		num;

				token = COM_ParseExt( text, qfalse );
				if ( !token[0] ) {
					break;
				}
				num = numImageNames[currentBundle];
				if ( num < MAX_IMAGE_ANIMATIONS ) {
					Q_strncpyz( imageNames[currentBundle][num], token, sizeof( imageNames[currentBundle][0] ) );
					numImageNames[currentBundle]++;
				}
				totalImages++;
			}

			if ( totalImages > MAX_IMAGE_ANIMATIONS ) {
				ri.Printf( PRINT_WARNING, "WARNING: ignoring excess images for '%s' (found %d, max is %d) in shader '%s'\n",
						keyword, totalImages, MAX_IMAGE_ANIMATIONS, shader.name );
			}
		}
		else if ( !Q_stricmp( token, "videoMap" ) )
		{
			token = COM_ParseExt( text, qfalse );
			if ( !token[0] )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing parameter for 'videoMap' keyword in shader '%s'\n", shader.name );
				return qfalse;
			}
			bundle->videoMapHandle = ri.CIN_PlayCinematic( token, 0, 0, 256, 256, (CIN_loop | CIN_silent | CIN_shader));
			if (bundle->videoMapHandle != -1) {
				bundle->isVideoMap = qtrue;
				bundle->image[0] = tr.scratchImage[bundle->videoMapHandle];
			} else {
				ri.Printf( PRINT_WARNING, "WARNING: could not load '%s' for 'videoMap' keyword in shader '%s'\n", token, shader.name );
			}
		}
		//
		// no mip maps
		//
		else if ( !Q_stricmp( token, "nomipmaps" ) || ( !Q_stricmp( token,"nomipmap" ) ) )
		{
			stage_noMipMaps = qtrue;
			stage_noPicMip = qtrue;
			continue;
		}
		//
		// no picmip adjustment
		//
		else if ( !Q_stricmp( token, "nopicmip" ) )
		{
			stage_noPicMip = qtrue;
			continue;
		}
		//
		// character picmip adjustment
		//
		else if ( !Q_stricmp( token, "picmip2" ) ) {
			stage_picmipFlag = IMGFLAG_PICMIP2;
			continue;
		}
		//
		// alphafunc <func>
		//
		else if ( !Q_stricmp( token, "alphaFunc" ) )
		{
			token = COM_ParseExt( text, qfalse );
			if ( !token[0] )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing parameter for 'alphaFunc' keyword in shader '%s'\n", shader.name );
				return qfalse;
			}

			atestBits = NameToAFunc( token );
		}
		//
		// alphaTest <func> <value>
		//
		else if ( !Q_stricmp( token, "alphaTest" ) )
		{
			float fvalue;

			token = COM_ParseExt( text, qfalse );
			if ( !token[0] )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing alpha func for 'alphaTest' keyword in shader '%s'\n", shader.name );
				return qfalse;
			}

			if ( !Q_stricmp( token, "greaterEqual" ) )
			{
				atestBits = GLS_ATEST_GREATEREQUAL;
			}
			else if ( !Q_stricmp( token, "greater" ) )
			{
				atestBits = GLS_ATEST_GREATER;
			}
			else if ( !Q_stricmp( token, "less" ) )
			{
				atestBits = GLS_ATEST_LESS;
			}
			else if ( !Q_stricmp( token, "lessEqual" ) )
			{
				atestBits = GLS_ATEST_LESSEQUAL;
			}
			else if ( !Q_stricmp( token, "equal" ) )
			{
				atestBits = GLS_ATEST_EQUAL;
			}
			else if ( !Q_stricmp( token, "notEqual" ) )
			{
				atestBits = GLS_ATEST_NOTEQUAL;
			}
			else
			{
				ri.Printf( PRINT_WARNING, "WARNING: invalid alpha func name '%s' for 'alphaTest' in shader '%s'\n", token, shader.name );
				return qfalse;
			}

			token = COM_ParseExt( text, qfalse );
			if ( !token[0] )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing alpha reference value for 'alphaTest' keyword in shader '%s'\n", shader.name );
				return qfalse;
			}

			fvalue = atof( token );

			if ( fvalue < 0.0f || fvalue > 1.0f )
			{
				ri.Printf( PRINT_WARNING, "WARNING: invalid alpha reference value '%g' for 'alphaTest' in shader '%s'\n", fvalue, shader.name );
				return qfalse;
			}

			atestBits |= (unsigned)( fvalue * 100 ) << GLS_ATEST_REF_SHIFT;
		}
		//
		// depthFunc <func>
		//
		else if ( !Q_stricmp( token, "depthfunc" ) )
		{
			token = COM_ParseExt( text, qfalse );

			if ( !token[0] )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing parameter for 'depthfunc' keyword in shader '%s'\n", shader.name );
				return qfalse;
			}

			if ( !Q_stricmp( token, "lequal" ) )
			{
				depthFuncBits = 0;
			}
			else if ( !Q_stricmp( token, "disable" ) )
			{
				depthFuncBits = 0;
			}
			else if ( !Q_stricmp( token, "equal" ) )
			{
				depthFuncBits = GLS_DEPTHFUNC_EQUAL;
			}
			else
			{
				ri.Printf( PRINT_WARNING, "WARNING: unknown depthfunc '%s' in shader '%s'\n", token, shader.name );
				continue;
			}
		}
		//
		// detail
		//
		else if ( !Q_stricmp( token, "detail" ) )
		{
			stage->isDetail = qtrue;

			// ditch this stage if it's detail and detail textures are disabled
			if ( !r_detailTextures->integer ) {
				stage_ignore = qtrue;
			} else if ( r_detailTextures->integer >= 2 ) {
				stage_noPicMip = qtrue;
			}
		}
		//
		// fog
		//
		else if ( !Q_stricmp( token, "fog" ) ) {
			token = COM_ParseExt( text, qfalse );
			if ( token[0] == 0 ) {
				ri.Printf( PRINT_WARNING, "WARNING: missing parm for fog in shader '%s'\n", shader.name );
				continue;
			}
			if ( !Q_stricmp( token, "on" ) ) {
				stage->isFogged = qtrue;
			} else {
				stage->isFogged = qfalse;
			}
		}
		//
		// blendfunc <srcFactor> <dstFactor>
		// or blendfunc <add|filter|blend>
		//
		else if ( !Q_stricmp( token, "blendfunc" ) )
		{
			token = COM_ParseExt( text, qfalse );
			if ( token[0] == 0 )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing parm for blendFunc in shader '%s'\n", shader.name );
				continue;
			}
			// check for "simple" blends first
			if ( !Q_stricmp( token, "add" ) ) {
				blendSrcBits = GLS_SRCBLEND_ONE;
				blendDstBits = GLS_DSTBLEND_ONE;
			} else if ( !Q_stricmp( token, "filter" ) ) {
				blendSrcBits = GLS_SRCBLEND_DST_COLOR;
				blendDstBits = GLS_DSTBLEND_ZERO;
			} else if ( !Q_stricmp( token, "blend" ) ) {
				blendSrcBits = GLS_SRCBLEND_SRC_ALPHA;
				blendDstBits = GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
			} else {
				// complex double blends
				blendSrcBits = NameToSrcBlendMode( token );

				token = COM_ParseExt( text, qfalse );
				if ( token[0] == 0 )
				{
					ri.Printf( PRINT_WARNING, "WARNING: missing parm for blendFunc in shader '%s'\n", shader.name );
					continue;
				}
				blendDstBits = NameToDstBlendMode( token );
			}

			// clear depth mask for blended surfaces
			if ( !depthMaskExplicit )
			{
				depthMaskBits = 0;
			}
		}
		//
		// stage <type>
		//
		else if(!Q_stricmp(token, "stage"))
		{
			token = COM_ParseExt(text, qfalse);
			if(token[0] == 0)
			{
				ri.Printf(PRINT_WARNING, "WARNING: missing parameters for stage in shader '%s'\n", shader.name);
				continue;
			}

			if(!Q_stricmp(token, "diffuseMap"))
			{
				stage->type = ST_DIFFUSEMAP;
			}
			else if(!Q_stricmp(token, "normalMap") || !Q_stricmp(token, "bumpMap"))
			{
				stage->type = ST_NORMALMAP;
				VectorSet4(stage->normalScale, r_baseNormalX->value, r_baseNormalY->value, 1.0f, r_baseParallax->value);
			}
			else if(!Q_stricmp(token, "normalParallaxMap") || !Q_stricmp(token, "bumpParallaxMap"))
			{
				if (r_parallaxMapping->integer)
					stage->type = ST_NORMALPARALLAXMAP;
				else
					stage->type = ST_NORMALMAP;
				VectorSet4(stage->normalScale, r_baseNormalX->value, r_baseNormalY->value, 1.0f, r_baseParallax->value);
			}
			else if(!Q_stricmp(token, "specularMap"))
			{
				stage->type = ST_SPECULARMAP;
				VectorSet4(stage->specularScale, 1.0f, 1.0f, 1.0f, 1.0f);
			}
			else
			{
				ri.Printf(PRINT_WARNING, "WARNING: unknown stage parameter '%s' in shader '%s'\n", token, shader.name);
				continue;
			}
		}
		//
		// specularReflectance <value>
		//
		else if (!Q_stricmp(token, "specularreflectance"))
		{
			token = COM_ParseExt(text, qfalse);
			if ( token[0] == 0 )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing parameter for specular reflectance in shader '%s'\n", shader.name );
				continue;
			}

			if (r_pbr->integer)
			{
				// interpret specularReflectance < 0.5 as nonmetal
				stage->specularScale[1] = (atof(token) < 0.5f) ? 0.0f : 1.0f;
			}
			else
			{
				stage->specularScale[0] =
				stage->specularScale[1] =
				stage->specularScale[2] = atof( token );
			}
		}
		//
		// specularExponent <value>
		//
		else if (!Q_stricmp(token, "specularexponent"))
		{
			float exponent;

			token = COM_ParseExt(text, qfalse);
			if ( token[0] == 0 )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing parameter for specular exponent in shader '%s'\n", shader.name );
				continue;
			}

			exponent = atof( token );

			if (r_pbr->integer)
				stage->specularScale[0] = 1.0f - powf(2.0f / (exponent + 2.0), 0.25);
			else
			{
				// Change shininess to gloss
				// Assumes max exponent of 8190 and min of 0, must change here if altered in lightall_fp.glsl
				exponent = CLAMP(exponent, 0.0f, 8190.0f);
				stage->specularScale[3] = (log2f(exponent + 2.0f) - 1.0f) / 12.0f;
			}
		}
		//
		// gloss <value>
		//
		else if (!Q_stricmp(token, "gloss"))
		{
			float gloss;

			token = COM_ParseExt(text, qfalse);
			if ( token[0] == 0 )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing parameter for gloss in shader '%s'\n", shader.name );
				continue;
			}

			gloss = atof(token);

			if (r_pbr->integer)
				stage->specularScale[0] = 1.0f - exp2f(-3.0f * gloss);
			else
				stage->specularScale[3] = gloss;
		}
		//
		// roughness <value>
		//
		else if (!Q_stricmp(token, "roughness"))
		{
			float roughness;

			token = COM_ParseExt(text, qfalse);
			if (token[0] == 0)
			{
				ri.Printf(PRINT_WARNING, "WARNING: missing parameter for roughness in shader '%s'\n", shader.name);
				continue;
			}

			roughness = atof(token);

			if (r_pbr->integer)
				stage->specularScale[0] = 1.0 - roughness;
			else
			{
				if (roughness >= 0.125)
					stage->specularScale[3] = log2f(1.0f / roughness) / 3.0f;
				else
					stage->specularScale[3] = 1.0f;
			}
		}
		//
		// parallaxDepth <value>
		//
		else if (!Q_stricmp(token, "parallaxdepth"))
		{
			token = COM_ParseExt(text, qfalse);
			if ( token[0] == 0 )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing parameter for parallaxDepth in shader '%s'\n", shader.name );
				continue;
			}

			stage->normalScale[3] = atof( token );
		}
		//
		// normalScale <xy>
		// or normalScale <x> <y>
		// or normalScale <x> <y> <height>
		//
		else if (!Q_stricmp(token, "normalscale"))
		{
			token = COM_ParseExt(text, qfalse);
			if ( token[0] == 0 )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing parameter for normalScale in shader '%s'\n", shader.name );
				continue;
			}

			stage->normalScale[0] = atof( token );

			token = COM_ParseExt(text, qfalse);
			if ( token[0] == 0 )
			{
				// one value, applies to X/Y
				stage->normalScale[1] = stage->normalScale[0];
				continue;
			}

			stage->normalScale[1] = atof( token );

			token = COM_ParseExt(text, qfalse);
			if ( token[0] == 0 )
			{
				// two values, no height
				continue;
			}

			stage->normalScale[3] = atof( token );
		}
		//
		// specularScale <rgb> <gloss>
		// or specularScale <metallic> <smoothness> with r_pbr 1
		// or specularScale <r> <g> <b>
		// or specularScale <r> <g> <b> <gloss>
		//
		else if (!Q_stricmp(token, "specularscale"))
		{
			token = COM_ParseExt(text, qfalse);
			if ( token[0] == 0 )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing parameter for specularScale in shader '%s'\n", shader.name );
				continue;
			}

			stage->specularScale[0] = atof( token );

			token = COM_ParseExt(text, qfalse);
			if ( token[0] == 0 )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing parameter for specularScale in shader '%s'\n", shader.name );
				continue;
			}

			stage->specularScale[1] = atof( token );

			token = COM_ParseExt(text, qfalse);
			if ( token[0] == 0 )
			{
				if (r_pbr->integer)
				{
					// two values, metallic then smoothness
					float smoothness = stage->specularScale[1];
					stage->specularScale[1] = (stage->specularScale[0] < 0.5f) ? 0.0f : 1.0f;
					stage->specularScale[0] = smoothness;
				}
				else
				{
					// two values, rgb then gloss
					stage->specularScale[3] = stage->specularScale[1];
					stage->specularScale[1] =
					stage->specularScale[2] = stage->specularScale[0];
				}
				continue;
			}

			stage->specularScale[2] = atof( token );

			token = COM_ParseExt(text, qfalse);
			if ( token[0] == 0 )
			{
				// three values, rgb
				continue;
			}

			stage->specularScale[3] = atof( token );

		}
		//
		// rgbGen
		//
		else if ( !Q_stricmp( token, "rgbGen" ) )
		{
			token = COM_ParseExt( text, qfalse );
			if ( token[0] == 0 )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing parameters for rgbGen in shader '%s'\n", shader.name );
				continue;
			}

			if ( !Q_stricmp( token, "wave" ) )
			{
				ParseWaveForm( text, &stage->rgbWave );
				stage->rgbGen = CGEN_WAVEFORM;
			}
			else if ( !Q_stricmp( token, "colorwave" ) )
			{
				vec3_t	color;

				ParseVector( text, 3, color );
				stage->constantColor[0] = 255 * color[0];
				stage->constantColor[1] = 255 * color[1];
				stage->constantColor[2] = 255 * color[2];

				ParseWaveForm( text, &stage->rgbWave );

				stage->rgbGen = CGEN_COLOR_WAVEFORM;
			}
			else if ( !Q_stricmp( token, "const" ) || !Q_stricmp( token, "constant" ) )
			{
				vec3_t	color;

				VectorClear( color );

				ParseVector( text, 3, color );
				stage->constantColor[0] = 255 * color[0];
				stage->constantColor[1] = 255 * color[1];
				stage->constantColor[2] = 255 * color[2];

				stage->rgbGen = CGEN_CONST;
			}
			else if ( !Q_stricmp( token, "identity" ) )
			{
				stage->rgbGen = CGEN_IDENTITY;
			}
			else if ( !Q_stricmp( token, "identityLighting" ) )
			{
				stage->rgbGen = CGEN_IDENTITY_LIGHTING;
			}
			else if ( !Q_stricmp( token, "entity" ) || !Q_stricmp( token, "fromEntity" ) )
			{
				stage->rgbGen = CGEN_ENTITY;
			}
			else if ( !Q_stricmp( token, "oneMinusEntity" ) )
			{
				stage->rgbGen = CGEN_ONE_MINUS_ENTITY;
			}
			else if ( !Q_stricmp( token, "vertex" ) || !Q_stricmp( token, "fromClient" ) )
			{
				stage->rgbGen = CGEN_VERTEX;
				if ( stage->alphaGen == 0 ) {
					stage->alphaGen = AGEN_VERTEX;
				}
			}
			else if ( !Q_stricmp( token, "exactVertex" ) )
			{
				stage->rgbGen = CGEN_EXACT_VERTEX;
			}
			else if ( !Q_stricmp( token, "vertexLit" ) )
			{
				stage->rgbGen = CGEN_VERTEX_LIT;
				if ( stage->alphaGen == 0 ) {
					stage->alphaGen = AGEN_VERTEX;
				}
			}
			else if ( !Q_stricmp( token, "exactVertexLit" ) )
			{
				stage->rgbGen = CGEN_EXACT_VERTEX_LIT;
			}
			else if ( !Q_stricmp( token, "lightingDiffuse" ) )
			{
				stage->rgbGen = R_DiffuseColorGen( shader.lightmapIndex );
			}
			else if ( !Q_stricmp( token, "lightingDiffuseEntity" ) )
			{
				// lighting diffuse only works on entities, not 2D or world
				if ( shader.lightmapIndex == LIGHTMAP_NONE ) {
					stage->rgbGen = CGEN_LIGHTING_DIFFUSE_ENTITY;
				} else {
					// use vertex color fallback
					stage->rgbGen = R_DiffuseColorGen( shader.lightmapIndex );
				}
			}
			else if ( !Q_stricmp( token, "oneMinusVertex" ) )
			{
				stage->rgbGen = CGEN_ONE_MINUS_VERTEX;
			}
			else
			{
				ri.Printf( PRINT_WARNING, "WARNING: unknown rgbGen parameter '%s' in shader '%s'\n", token, shader.name );
				continue;
			}
		}
		//
		// alphaGen 
		//
		else if ( !Q_stricmp( token, "alphaGen" ) )
		{
			token = COM_ParseExt( text, qfalse );
			if ( token[0] == 0 )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing parameters for alphaGen in shader '%s'\n", shader.name );
				continue;
			}

			if ( !Q_stricmp( token, "wave" ) )
			{
				ParseWaveForm( text, &stage->alphaWave );
				stage->alphaGen = AGEN_WAVEFORM;
			}
			else if ( !Q_stricmp( token, "const" ) || !Q_stricmp( token, "constant" ) )
			{
				token = COM_ParseExt( text, qfalse );
				stage->constantColor[3] = 255 * atof( token );
				stage->alphaGen = AGEN_CONST;
			}
			else if ( !Q_stricmp( token, "identity" ) )
			{
				stage->alphaGen = AGEN_IDENTITY;
			}
			else if ( !Q_stricmp( token, "entity" ) || !Q_stricmp( token, "fromEntity" ) )
			{
				stage->alphaGen = AGEN_ENTITY;
			}
			else if ( !Q_stricmp( token, "oneMinusEntity" ) )
			{
				stage->alphaGen = AGEN_ONE_MINUS_ENTITY;
			}
			else if ( !Q_stricmp( token, "vertex" ) || !Q_stricmp( token, "fromClient" ) )
			{
				stage->alphaGen = AGEN_VERTEX;
			}
			else if ( !Q_stricmp( token, "lightingSpecular" ) )
			{
				stage->alphaGen = AGEN_LIGHTING_SPECULAR;
			}
			else if ( !Q_stricmp( token, "oneMinusVertex" ) )
			{
				stage->alphaGen = AGEN_ONE_MINUS_VERTEX;
			}
			else if ( !Q_stricmp( token, "portal" ) )
			{
				stage->alphaGen = AGEN_PORTAL;
				token = COM_ParseExt( text, qfalse );
				if ( token[0] == 0 )
				{
					shader.portalRange = 256;
					ri.Printf( PRINT_WARNING, "WARNING: missing range parameter for alphaGen portal in shader '%s', defaulting to 256\n", shader.name );
				}
				else
				{
					shader.portalRange = atof( token );
				}
			}
			else if ( !Q_stricmp( token, "skyAlpha" ) )
			{
				stage->alphaGen = AGEN_SKY_ALPHA;
			}
			else if ( !Q_stricmp( token, "oneMinusSkyAlpha" ) )
			{
				stage->alphaGen = AGEN_ONE_MINUS_SKY_ALPHA;
			}
			else if ( !Q_stricmp( token, "normalzfade" ) )
			{
				stage->alphaGen = AGEN_NORMALZFADE;
				token = COM_ParseExt( text, qfalse );
				if ( token[0] ) {
					stage->constantColor[3] = 255 * atof( token );
				} else {
					stage->constantColor[3] = 255;
				}

				token = COM_ParseExt( text, qfalse );
				if ( token[0] ) {
					stage->zFadeBounds[0] = atof( token );    // lower range
					token = COM_ParseExt( text, qfalse );
					stage->zFadeBounds[1] = atof( token );    // upper range
				} else {
					stage->zFadeBounds[0] = -1.0;   // lower range
					stage->zFadeBounds[1] =  1.0;   // upper range
				}
			}
			else
			{
				ri.Printf( PRINT_WARNING, "WARNING: unknown alphaGen parameter '%s' in shader '%s'\n", token, shader.name );
				continue;
			}
		}
		//
		// tcGen <function>
		//
		else if ( !Q_stricmp(token, "texgen") || !Q_stricmp( token, "tcGen" ) ) 
		{
			token = COM_ParseExt( text, qfalse );
			if ( token[0] == 0 )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing %s parm in shader '%s'\n", keyword, shader.name );
				continue;
			}

			if ( !Q_stricmp( token, "environment" ) )
			{
				bundle->tcGen = TCGEN_ENVIRONMENT_MAPPED;
			}
			else if ( !Q_stricmp( token, "cel" ) )
			{
				bundle->tcGen = TCGEN_ENVIRONMENT_CELSHADE_MAPPED;
			}
			else if ( !Q_stricmp( token, "lightmap" ) )
			{
				bundle->tcGen = TCGEN_LIGHTMAP;
			}
			else if ( !Q_stricmp( token, "texture" ) || !Q_stricmp( token, "base" ) )
			{
				bundle->tcGen = TCGEN_TEXTURE;
			}
			else if ( !Q_stricmp( token, "vector" ) )
			{
				ParseVector( text, 3, bundle->tcGenVectors[0] );
				ParseVector( text, 3, bundle->tcGenVectors[1] );

				bundle->tcGen = TCGEN_VECTOR;
			}
			else 
			{
				ri.Printf( PRINT_WARNING, "WARNING: unknown %s parameter '%s' in shader '%s'\n", keyword, token, shader.name );
			}
		}
		//
		// tcMod <type> <...>
		//
		else if ( !Q_stricmp( token, "tcMod" ) )
		{
			char buffer[1024] = "";

			while ( 1 )
			{
				token = COM_ParseExt( text, qfalse );
				if ( token[0] == 0 )
					break;
				Q_strcat( buffer, sizeof (buffer), token );
				Q_strcat( buffer, sizeof (buffer), " " );
			}

			ParseTexMod( buffer, bundle );

			continue;
		}
		//
		// depthmask
		//
		else if ( !Q_stricmp( token, "depthwrite" ) || !Q_stricmp( token, "depthmask" ) )
		{
			depthMaskBits = GLS_DEPTHMASK_TRUE;
			depthMaskExplicit = qtrue;

			continue;
		}
		//
		// noDepthTest
		//
		else if ( !Q_stricmp( token, "noDepthTest" ) )
		{
			depthTestBits = GLS_DEPTHTEST_DISABLE;
			continue;
		}
		//
		// depthTest disable
		//
		else if ( !Q_stricmp( token, "depthTest" ) )
		{
			token = COM_ParseExt( text, qfalse );
			if ( token[0] == 0 )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing depthTest parameter in shader '%s'\n", shader.name );
				continue;
			}

			if ( !Q_stricmp( token, "disable" ) )
			{
				depthTestBits = GLS_DEPTHTEST_DISABLE;
			}
			else
			{
				depthTestBits = 0;
				ri.Printf( PRINT_WARNING, "WARNING: unknown depthTest parameter '%s' in shader '%s'\n", token, shader.name );
			}
			continue;
		}
		//
		// nextbundle
		//
		else if ( !Q_stricmp( token, "nextbundle" ) )
		{
			if ( !qglActiveTextureARB ) {
				ri.Printf( PRINT_WARNING, "WARNING: nextbundle keyword in shader '%s' but multitexture is not available\n", shader.name );
				return qfalse;
			}
			// ZTM: Note: Rend2 has more magic bundles so hard code to 2.
			if ( currentBundle+1 >= 2/*NUM_TEXTURE_BUNDLES*/ ) {
				ri.Printf( PRINT_WARNING, "WARNING: too many 'nextbundle' keywords in shader '%s' (max %d bundles)\n", shader.name, 2/*NUM_TEXTURE_BUNDLES*/ );
				return qfalse;
			}
			currentBundle++;
			bundle = &stage->bundle[currentBundle];
			continue;
		}
		//
		// surfaceSprites (and settings)
		//
		else if ( !Q_stricmp( token, "surfaceSprites" )
				|| !Q_stricmp( token, "ssFadeMax" )
				|| !Q_stricmp( token, "ssFadescale" )
				|| !Q_stricmp( token, "ssVariance" )
				|| !Q_stricmp( token, "ssWind" )
				|| !Q_stricmp( token, "ssWindidle" )
				|| !Q_stricmp( token, "ssFXWeather" )
				|| !Q_stricmp( token, "ssFXAlphaRange" )
				|| !Q_stricmp( token, "ssFXDuration" )
				|| !Q_stricmp( token, "ssFXGrow" )
				|| !Q_stricmp( token, "ssHangDown" )
				|| !Q_stricmp( token, "ssFaceup" )
				|| !Q_stricmp( token, "ssFaceflat" )
				|| !Q_stricmp( token, "ssGore" )
				|| !Q_stricmp( token, "ssGoreCentral" )
				|| !Q_stricmp( token, "ssSpurtflat" )
				|| !Q_stricmp( token, "ssSpurt" )
				|| !Q_stricmp( token, "ssNoOffset" )
				)
		{
			ri.Printf( PRINT_WARNING, "WARNING: %s keyword is unsupported, used by '%s'\n", token, shader.name );
			stage_ignore = qtrue;
			continue;
		}
		//
		// glow
		//
		else if ( !Q_stricmp( token, "glow" ) )
		{
			ri.Printf( PRINT_WARNING, "WARNING: glow keyword is unsupported, used by '%s'\n", shader.name );
			continue;
		}
		else
		{
			ri.Printf( PRINT_WARNING, "WARNING: unknown parameter '%s' in shader '%s'\n", token, shader.name );
			return qfalse;
		}
	}

	//
	// load images
	//
	for ( currentBundle = 0; currentBundle < 2; currentBundle++ )
	{
		int num;

		num = numImageNames[currentBundle];
		bundle = &stage->bundle[currentBundle];

		for ( i = 0; i < num; i++ )
		{
			imgType_t type = IMGTYPE_COLORALPHA;
			imgFlags_t flags = IMGFLAG_NONE;

			// don't load the images if the stage isn't going to be used
			if ( stage_ignore ) {
				bundle->image[i] = tr.defaultImage;
				continue;
			}

			if (!stage_noMipMaps)
				flags |= IMGFLAG_MIPMAP;

			if (!stage_noPicMip)
				flags |= stage_picmipFlag;

			if (!shader_allowCompress)
				flags |= IMGFLAG_NO_COMPRESSION;

			if (clampMap || clampTexCoords)
				flags |= IMGFLAG_CLAMPTOEDGE;

			if (stage->type == ST_NORMALMAP || stage->type == ST_NORMALPARALLAXMAP)
			{
				type = IMGTYPE_NORMAL;
				flags |= IMGFLAG_NOLIGHTSCALE;

				if (stage->type == ST_NORMALPARALLAXMAP)
					type = IMGTYPE_NORMALHEIGHT;
			}
			else
			{
				if (r_genNormalMaps->integer)
					flags |= IMGFLAG_GENNORMALMAP;
			}

			token = imageNames[currentBundle][i];
			bundle->image[i] = R_FindImageFile( token, type, flags );

			if ( !bundle->image[i] )
			{
				ri.Printf( PRINT_WARNING, "WARNING: R_FindImageFile could not find '%s' in shader '%s'\n", token, shader.name );
				return qfalse;
			}
		}

		bundle->numImageAnimations = ( num > 1 ) ? num : 0;
	}


	//
	// handle multiple bundles
	//
	if ( stage->bundle[1].image[0] ) {
		// ZTM: Note: In FAKK stages with nextbundle don't use lightmap if r_lightmap 1, suggesting this swap isn't done.
		//				though I'm not sure what it effects besides r_lightmap. I don't think it matter.
		// make sure that lightmaps are in bundle 1 for 3dfx
		if ( stage->bundle[0].isLightmap )
		{
			textureBundle_t tmpBundle;

			tmpBundle = stage->bundle[0];
			stage->bundle[0] = stage->bundle[1];
			stage->bundle[1] = tmpBundle;
		}

		// ZTM: My guess based on multitexture collapse. I saw a shader with nextbundle and blendfunc GL_ONE GL_ONE so probably uses GL_ADD. Should try to check if GL_DECAL is used? 
		// GL_ADD is a separate extension
		if ( blendSrcBits == GLS_SRCBLEND_ONE && blendDstBits == GLS_DSTBLEND_ONE && glConfig.textureEnvAddAvailable ) {
			stage->multitextureEnv = GL_ADD;
		} else {
			stage->multitextureEnv = GL_MODULATE;
		}
	}

	//
	// if cgen isn't explicitly specified, use either identity or identitylighting
	//
	if ( stage->rgbGen == CGEN_BAD ) {
		if ( blendSrcBits == 0 ||
			blendSrcBits == GLS_SRCBLEND_ONE || 
			blendSrcBits == GLS_SRCBLEND_SRC_ALPHA ) {
			stage->rgbGen = CGEN_IDENTITY_LIGHTING;
		} else {
			stage->rgbGen = CGEN_IDENTITY;
		}
	}

	//
	// pre-retail Quake 3 / FAKK colorizies 2D identity and unspecified rgbGen
	//
	if ( r_colorize2DIdentity->integer && shader.lightmapIndex == LIGHTMAP_2D ) {
		if ( stage->rgbGen == CGEN_IDENTITY || stage->rgbGen == CGEN_IDENTITY_LIGHTING ) {
			stage->rgbGen = CGEN_VERTEX;
		}
	}

	//
	// if shader stage references a lightmap but no lightmap is present
	// and shader doesn't specify nolightmap (or also has pointlight),
	// use diffuse cgen
	//
	if (((stage->bundle[0].isLightmap && stage->bundle[0].image[0] == tr.whiteImage ) ||
		( stage->bundle[1].isLightmap && stage->bundle[1].image[0] == tr.whiteImage ) )
		&& shader.lightmapIndex < 0 )
	{
		if ( !( shader.surfaceParms & SURF_NOLIGHTMAP ) || ( shader.surfaceParms & SURF_POINTLIGHT ) ) {
			if ( r_missingLightmapUseDiffuseLighting->integer ) {
				// surfaceParm pointlight, vertex-approximated surfaces, non-lightmapped misc_models, or not a world surface
				ri.Printf( PRINT_DEVELOPER, "WARNING: shader '%s' has lightmap stage but no lightmap, using diffuse lighting.\n", shader.name );
				stage->bundle[0].isLightmap = qfalse;
				stage->bundle[1].isLightmap = qfalse;
				stage->rgbGen = R_DiffuseColorGen( shader.lightmapIndex );
			} else {
				ri.Printf( PRINT_DEVELOPER, "WARNING: shader '%s' has lightmap stage but no lightmap, using white image.\n", shader.name );
			}
		} else {
			ri.Printf( PRINT_DEVELOPER, "WARNING: shader '%s' has lightmap stage but specifies no lightmap, using white image.\n", shader.name );
		}
	}

	//
	// implicitly assume that a GL_ONE GL_ZERO blend mask disables blending
	//
	if ( ( blendSrcBits == GLS_SRCBLEND_ONE ) && 
		 ( blendDstBits == GLS_DSTBLEND_ZERO ) )
	{
		blendDstBits = blendSrcBits = 0;
		depthMaskBits = GLS_DEPTHMASK_TRUE;
	}

	// decide which agens we can skip
	if ( stage->alphaGen == AGEN_IDENTITY ) {
		if ( stage->rgbGen == CGEN_IDENTITY
			|| stage->rgbGen == CGEN_LIGHTING_DIFFUSE ) {
			stage->alphaGen = AGEN_SKIP;
		}
	}

	//
	// compute state bits
	//
	stage->stateBits = depthMaskBits | 
		               blendSrcBits | blendDstBits | 
					   atestBits | 
					   depthFuncBits |
					   depthTestBits;

	return qtrue;
}

/*
===============
ParseDeform

deformVertexes wave <spread> <waveform> <base> <amplitude> <phase> <frequency>
deformVertexes normal <frequency> <amplitude>
deformVertexes move <vector> <waveform> <base> <amplitude> <phase> <frequency>
deformVertexes bulge <bulgeWidth> <bulgeHeight> <bulgeSpeed>
deformVertexes projectionShadow
deformVertexes autoSprite
deformVertexes autoSprite2
deformVertexes text[0-7]
===============
*/
static void ParseDeform( char **text ) {
	char	*token;
	deformStage_t	*ds;

	token = COM_ParseExt( text, qfalse );
	if ( token[0] == 0 )
	{
		ri.Printf( PRINT_WARNING, "WARNING: missing deform parm in shader '%s'\n", shader.name );
		return;
	}

	if ( shader.numDeforms == MAX_SHADER_DEFORMS ) {
		ri.Printf( PRINT_WARNING, "WARNING: MAX_SHADER_DEFORMS in '%s'\n", shader.name );
		return;
	}

	ds = &shader.deforms[ shader.numDeforms ];
	shader.numDeforms++;

	if ( !Q_stricmp( token, "projectionShadow" ) ) {
		ds->deformation = DEFORM_PROJECTION_SHADOW;
		return;
	}

	if ( !Q_stricmp( token, "autosprite" ) ) {
		ds->deformation = DEFORM_AUTOSPRITE;
		return;
	}

	if ( !Q_stricmp( token, "autosprite2" ) ) {
		ds->deformation = DEFORM_AUTOSPRITE2;
		return;
	}

	if ( !Q_stricmpn( token, "text", 4 ) ) {
		int		n;
		
		n = token[4] - '0';
		if ( n < 0 || n > 7 ) {
			n = 0;
		}
		ds->deformation = DEFORM_TEXT0 + n;
		return;
	}

	if ( !Q_stricmp( token, "bulge" ) )	{
		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing deformVertexes bulge parm in shader '%s'\n", shader.name );
			return;
		}
		ds->bulgeWidth = atof( token );

		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing deformVertexes bulge parm in shader '%s'\n", shader.name );
			return;
		}
		ds->bulgeHeight = atof( token );

		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing deformVertexes bulge parm in shader '%s'\n", shader.name );
			return;
		}
		ds->bulgeSpeed = atof( token );

		ds->deformation = DEFORM_BULGE;
		return;
	}

	if ( !Q_stricmp( token, "wave" ) )
	{
		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing deformVertexes parm in shader '%s'\n", shader.name );
			return;
		}

		if ( atof( token ) != 0 )
		{
			ds->deformationSpread = 1.0f / atof( token );
		}
		else
		{
			ds->deformationSpread = 100.0f;
			ri.Printf( PRINT_WARNING, "WARNING: illegal div value of 0 in deformVertexes command for shader '%s'\n", shader.name );
		}

		ParseWaveForm( text, &ds->deformationWave );
		ds->deformation = DEFORM_WAVE;
		return;
	}

	if ( !Q_stricmp( token, "normal" ) )
	{
		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing deformVertexes parm in shader '%s'\n", shader.name );
			return;
		}
		ds->deformationWave.amplitude = atof( token );

		token = COM_ParseExt( text, qfalse );
		if ( token[0] == 0 )
		{
			ri.Printf( PRINT_WARNING, "WARNING: missing deformVertexes parm in shader '%s'\n", shader.name );
			return;
		}
		ds->deformationWave.frequency = atof( token );

		ds->deformation = DEFORM_NORMALS;
		return;
	}

	if ( !Q_stricmp( token, "move" ) ) {
		int		i;

		for ( i = 0 ; i < 3 ; i++ ) {
			token = COM_ParseExt( text, qfalse );
			if ( token[0] == 0 ) {
				ri.Printf( PRINT_WARNING, "WARNING: missing deformVertexes parm in shader '%s'\n", shader.name );
				return;
			}
			ds->moveVector[i] = atof( token );
		}

		ParseWaveForm( text, &ds->deformationWave );
		ds->deformation = DEFORM_MOVE;
		return;
	}

	ri.Printf( PRINT_WARNING, "WARNING: unknown deformVertexes subtype '%s' found in shader '%s'\n", token, shader.name );
}


/*
===============
ParseSkyParms

skyParms <outerbox> <cloudheight> <innerbox>
===============
*/
static void ParseSkyParms( char **text ) {
	char		*token;
	static char	*suf[6] = {"rt", "bk", "lf", "ft", "up", "dn"};
	char		pathname[MAX_QPATH];
	int			i;
	imgFlags_t imgFlags = IMGFLAG_MIPMAP | shader_picmipFlag;

	if (!shader_allowCompress)
		imgFlags |= IMGFLAG_NO_COMPRESSION;

	// outerbox
	token = COM_ParseExt( text, qfalse );
	if ( token[0] == 0 ) {
		ri.Printf( PRINT_WARNING, "WARNING: 'skyParms' missing parameter in shader '%s'\n", shader.name );
		return;
	}
	if ( strcmp( token, "-" ) ) {
		for (i=0 ; i<6 ; i++) {
			Com_sprintf( pathname, sizeof(pathname), "%s_%s.tga"
				, token, suf[i] );
			shader.sky.outerbox[i] = R_FindImageFile( ( char * ) pathname, IMGTYPE_COLORALPHA, imgFlags | IMGFLAG_CLAMPTOEDGE );

			if ( !shader.sky.outerbox[i] ) {
				shader.sky.outerbox[i] = tr.defaultImage;
			}
		}
	}

	// cloudheight
	token = COM_ParseExt( text, qfalse );
	if ( token[0] == 0 ) {
		ri.Printf( PRINT_WARNING, "WARNING: 'skyParms' missing parameter in shader '%s'\n", shader.name );
		return;
	}
	shader.sky.cloudHeight = atof( token );
	if ( !shader.sky.cloudHeight ) {
		shader.sky.cloudHeight = 512;
	}
	R_InitSkyTexCoords( shader.sky.cloudHeight );


	// innerbox
	token = COM_ParseExt( text, qfalse );
	if ( token[0] == 0 ) {
		ri.Printf( PRINT_WARNING, "WARNING: 'skyParms' missing parameter in shader '%s'\n", shader.name );
		return;
	}
	if ( strcmp( token, "-" ) ) {
		for (i=0 ; i<6 ; i++) {
			Com_sprintf( pathname, sizeof(pathname), "%s_%s.tga"
				, token, suf[i] );
			shader.sky.innerbox[i] = R_FindImageFile( ( char * ) pathname, IMGTYPE_COLORALPHA, imgFlags );
			if ( !shader.sky.innerbox[i] ) {
				shader.sky.innerbox[i] = tr.defaultImage;
			}
		}
	}

	shader.isSky = qtrue;
}


/*
=================
ParseSort
=================
*/
void ParseSort( char **text ) {
	char	*token, *end;

	token = COM_ParseExt( text, qfalse );
	if ( token[0] == 0 ) {
		ri.Printf( PRINT_WARNING, "WARNING: missing sort parameter in shader '%s'\n", shader.name );
		return;
	}

	if ( !Q_stricmp( token, "portal" ) ) {
		shader.sort = SS_PORTAL;
	} else if ( !Q_stricmp( token, "sky" ) ) {
		shader.sort = SS_ENVIRONMENT;
	} else if ( !Q_stricmp( token, "opaque" ) ) {
		shader.sort = SS_OPAQUE;
	}else if ( !Q_stricmp( token, "decal" ) ) {
		shader.sort = SS_DECAL;
	} else if ( !Q_stricmp( token, "seeThrough" ) ) {
		shader.sort = SS_SEE_THROUGH;
	} else if ( !Q_stricmp( token, "banner" ) ) {
		shader.sort = SS_BANNER;
	} else if ( !Q_stricmp( token, "additive" ) ) {
		shader.sort = SS_BLEND1;
	} else if ( !Q_stricmp( token, "nearest" ) ) {
		shader.sort = SS_NEAREST;
	} else if ( !Q_stricmp( token, "underwater" ) ) {
		shader.sort = SS_UNDERWATER;
	} else {
		shader.sort = strtof( token, &end );

		if ( end == token ) {
			// Unknown text keyword. Fallback to automatic sort value.
			ri.Printf( PRINT_WARNING, "WARNING: unknown sort parameter '%s' in shader '%s'\n", token, shader.name );
			shader.sort = 0;
		} else if ( shader.sort == 0 ) {
			// Automatically calculate sort value. This is the default and shouldn't be specified.
		} else if ( floor( shader.sort ) < 1 || floor( shader.sort ) >= SS_MAX_SORT ) {
			ri.Printf( PRINT_WARNING, "WARNING: sort parameter %f in shader '%s' is out of range (min 1, max %d)\n", shader.sort, shader.name, SS_MAX_SORT - 1 );
			shader.sort = Com_Clamp( 1, SS_MAX_SORT - 1, shader.sort );
		}
	}
}



typedef struct {
	char	*name;
	int		surfaceParms;
} infoParm_t;

infoParm_t	infoParms[] = {
	{"fog",			SURF_FOG		},	// carves surfaces entering
	{"sky",			SURF_SKY		},	// emit light from an environment map
	{"noimpact",	SURF_NOIMPACT	},	// don't make impact explosions or marks
	{"nomarks",		SURF_NOMARKS	},	// don't make impact marks, but still explode
	{"pointlight",	SURF_POINTLIGHT	},	// sample lighting at vertexes
	{"nolightmap",	SURF_NOLIGHTMAP	},	// don't generate a lightmap
	{"nodlight",	SURF_NODLIGHT	},	// don't ever add dynamic lights
};


/*
===============
ParseSurfaceParm

surfaceparm <name>
===============
*/
static void ParseSurfaceParm( char **text ) {
	char	*token;
	int		numInfoParms = ARRAY_LEN( infoParms );
	int		i;

	token = COM_ParseExt( text, qfalse );
	for ( i = 0 ; i < numInfoParms ; i++ ) {
		if ( !Q_stricmp( token, infoParms[i].name ) ) {
			shader.surfaceParms |= infoParms[i].surfaceParms;
			break;
		}
	}
}

/*
=================
ParseShader

The current text pointer is at the explicit text definition of the
shader.  Parse it into the global shader variable.  Later functions
will optimize it.
=================
*/
static qboolean ParseShader( char **text )
{
	char *token;
	int s;
	int ifIndent = 0;
	qboolean skipRestOfLine = qfalse;

	s = 0;

	token = COM_ParseExt( text, qtrue );
	if ( token[0] != '{' )
	{
		ri.Printf( PRINT_WARNING, "WARNING: expecting '{', found '%s' instead in shader '%s'\n", token, shader.name );
		return qfalse;
	}

	while ( 1 )
	{
		if ( skipRestOfLine ) {
			// skip unused data for known directives
			SkipRestOfLineUntilBrace( text );
		}
		skipRestOfLine = qtrue;

		token = COM_ParseExt( text, qtrue );
		if ( !token[0] )
		{
			ri.Printf( PRINT_WARNING, "WARNING: no concluding '}' in shader %s\n", shader.name );
			return qfalse;
		}

		switch ( ParseIfEndif( text, token, &ifIndent, 1 ) )
		{
			case 0:
				return qfalse;
			case 1:
				continue;
			default:
				break;
		}

		// end of shader definition
		if ( token[0] == '}' )
		{
			break;
		}
		// stage definition
		else if ( token[0] == '{' )
		{
			if ( s >= MAX_SHADER_STAGES ) {
				ri.Printf( PRINT_WARNING, "WARNING: too many stages in shader %s (max is %i)\n", shader.name, MAX_SHADER_STAGES );
				return qfalse;
			}

			stage_ignore = qfalse;

			if ( !ParseStage( &stages[s], text, &ifIndent ) )
			{
				return qfalse;
			}

			if ( stage_ignore ) {
				ClearShaderStage( s );
			} else {
				stages[s].active = qtrue;
				s++;
			}

			// Don't skip data after the }
			skipRestOfLine = qfalse;

			continue;
		}
		// skip stuff that only the QuakeEdRadient needs
		else if ( !Q_stricmpn( token, "qer", 3 ) ) {
			continue;
		}
		// sun parms
		else if ( !Q_stricmp( token, "sun" ) || !Q_stricmp( token, "q3map_sun" ) || !Q_stricmp( token, "q3map_sunExt" ) || !Q_stricmp( token, "q3gl2_sun" ) ) {
			float	a, b;
			qboolean isGL2Sun = qfalse;

			if (!Q_stricmp( token, "q3gl2_sun" ) && r_sunShadows->integer )
			{
				isGL2Sun = qtrue;
				tr.sunShadows = qtrue;
			}

			token = COM_ParseExt( text, qfalse );
			tr.sunLight[0] = atof( token );
			token = COM_ParseExt( text, qfalse );
			tr.sunLight[1] = atof( token );
			token = COM_ParseExt( text, qfalse );
			tr.sunLight[2] = atof( token );
			
			VectorNormalize( tr.sunLight );

			token = COM_ParseExt( text, qfalse );
			a = atof( token );
			VectorScale( tr.sunLight, a, tr.sunLight);

			token = COM_ParseExt( text, qfalse );
			a = atof( token );
			a = a / 180 * M_PI;

			token = COM_ParseExt( text, qfalse );
			b = atof( token );
			b = b / 180 * M_PI;

			tr.sunDirection[0] = cos( a ) * cos( b );
			tr.sunDirection[1] = sin( a ) * cos( b );
			tr.sunDirection[2] = sin( b );

			if (isGL2Sun)
			{
				token = COM_ParseExt( text, qfalse );
				tr.sunShadowScale = atof(token);

				// parse twice, since older shaders may include mapLightScale before sunShadowScale
				if (token[0]) {
					token = COM_ParseExt( text, qfalse );
					if (token[0]) {
						tr.sunShadowScale = atof(token);
					}
				}
			}
			continue;
		}
		// tonemap parms
		else if ( !Q_stricmp( token, "q3gl2_tonemap" ) ) {
			token = COM_ParseExt( text, qfalse );
			tr.toneMinAvgMaxLevel[0] = atof( token );
			token = COM_ParseExt( text, qfalse );
			tr.toneMinAvgMaxLevel[1] = atof( token );
			token = COM_ParseExt( text, qfalse );
			tr.toneMinAvgMaxLevel[2] = atof( token );

			token = COM_ParseExt( text, qfalse );
			tr.autoExposureMinMax[0] = atof( token );
			token = COM_ParseExt( text, qfalse );
			tr.autoExposureMinMax[1] = atof( token );
			continue;
		}
		else if ( !Q_stricmp( token, "deformVertexes" ) ) {
			ParseDeform( text );
			continue;
		}
		else if ( !Q_stricmp( token, "tesssize" ) ) {
			continue;
		}
		else if ( !Q_stricmp( token, "clampTime" ) ) {
			token = COM_ParseExt( text, qfalse );
			if (token[0]) {
				shader.clampTime = atof(token);
			}
		}
		// skip stuff that only the q3map needs
		else if ( !Q_stricmpn( token, "q3map", 5 ) ) {
			continue;
		}
		// fogOnly
		// Some Q3 shaders have it, but not ones used by the final game.
		// FAKK and Alice do use it though. FAKK shader manual say it must be used with surfaceParm fog.
		else if ( !Q_stricmp( token, "fogOnly" ) ) {
			continue;
		}
		// surfaceLight is an alias for q3map_surfacelight (for FAKK at least)
		else if ( !Q_stricmp( token, "surfaceLight" ) ) {
			continue;
		}
		// surfaceColor <red> <green> <blue>
		// used by FAKK map compiler, an alternative to q3map_lightimage
		// FAKK doesn't use parentheses, but it would have them in Q3-style syntax...
		else if ( !Q_stricmp( token, "surfaceColor" ) ) {
			continue;
		}
		// surfaceDensity <density> // used by FAKK mapcompiler for lightmap density
		else if ( !Q_stricmp( token, "surfaceDensity" ) ) {
			continue;
		}
		// subdivisions <integer> // used by FAKK mapcompiler for patch draw surface's subdivisions
		else if ( !Q_stricmp( token, "subdivisions" ) ) {
			continue;
		}
		// lightcolor ( <red> <green> <blue> )
		// ZTM: I think it's used by SoF2 map compiler as an alternative to q3map_lightimage
		// Might also be used by SoF2's RMG for ambient color (if shader is sky).
		else if ( !Q_stricmp(token, "lightcolor") ) {
			continue;
		}
		// skip stuff that only q3map or the server needs
		else if ( !Q_stricmp( token, "surfaceParm" ) ) {
			ParseSurfaceParm( text );
			continue;
		}
		// force 32 bit
		else if ( !Q_stricmp( token, "force32bit" ) ) {
			// ZTM: FIXME: Actually support force32bit
			continue;
		}
		// no mip maps
		else if ( !Q_stricmp( token, "nomipmaps" ) || ( !Q_stricmp( token,"nomipmap" ) )  )
		{
			shader.noMipMaps = qtrue;
			shader.noPicMip = qtrue;
			continue;
		}
		// no picmip adjustment
		else if ( !Q_stricmp( token, "nopicmip" ) )
		{
			shader.noPicMip = qtrue;
			continue;
		}
		// character picmip adjustment
		else if ( !Q_stricmp( token, "picmip2" ) ) {
			shader_picmipFlag = IMGFLAG_PICMIP2;
			continue;
		}
		// polygonOffset
		else if ( !Q_stricmp( token, "polygonOffset" ) )
		{
			shader.polygonOffset = qtrue;
			continue;
		}
		// entityMergable, allowing sprite surfaces from multiple entities
		// to be merged into one batch.  This is a savings for smoke
		// puffs and blood, but can't be used for anything where the
		// shader calcs (not the surface function) reference the entity color or scroll
		else if ( !Q_stricmp( token, "entityMergable" ) )
		{
			shader.entityMergable = qtrue;
			continue;
		}
		// spriteScale <float>
		else if ( !Q_stricmp( token, "spriteScale" ) )
		{
			token = COM_ParseExt( text, qfalse );
			if ( token[0] == 0 )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing parm for 'spriteScale' keyword in shader '%s'\n", shader.name );
				continue;
			}

			shader.spriteScale = atof( token );
		}
		// spriteGen <parallel|parallel_upright|parallel_oriented|oriented>
		else if ( !Q_stricmp( token, "spriteGen" ) )
		{
			token = COM_ParseExt( text, qfalse );
			if ( token[0] == 0 )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing parm for 'spriteGen' keyword in shader '%s'\n", shader.name );
				continue;
			}

			if ( !Q_stricmp( token, "parallel" ) ) {
				shader.spriteGen = SG_PARALLEL;
			} else if ( !Q_stricmp( token, "parallel_upright" ) ) {
				shader.spriteGen = SG_PARALLEL_UPRIGHT;
			} else if ( !Q_stricmp( token, "parallel_oriented" ) ) {
				shader.spriteGen = SG_PARALLEL_ORIENTED;
			} else if ( !Q_stricmp( token, "oriented" ) ) {
				shader.spriteGen = SG_ORIENTED;
			} else {
				ri.Printf( PRINT_WARNING, "WARNING: invalid spriteGen parm '%s' in shader '%s'\n", token, shader.name );
				continue;
			}
		}
		// sunShader <shader> [scale]
		else if ( !Q_stricmp( token, "sunShader" ) ) {
			token = COM_ParseExt( text, qfalse );
			if ( !token[0] ) {
				ri.Printf( PRINT_WARNING, "WARNING: '%s' missing shader name for 'sunShader'\n", shader.name );
				continue;
			}

			Q_strncpyz( tr.sunShaderName, token, sizeof ( tr.sunShaderName ) );

			token = COM_ParseExt( text, qfalse );
			if ( !token[0] ) {
				tr.sunShaderScale = 1.0f;
				continue;
			}

			tr.sunShaderScale = atof( token );

			if ( tr.sunShaderScale <= 0 ) {
				ri.Printf( PRINT_WARNING, "WARNING: '%s' scale for 'sunShader' must be more than 0, using scale 1.0 instead.\n", shader.name );
				tr.sunShaderScale = 1.0f;
			}
			continue;
		}
		// lightgridmulamb <scale>  ambient multiplier for lightgrid
		else if ( !Q_stricmp( token, "lightgridmulamb" ) )
		{
			float scale;

			token = COM_ParseExt( text, qfalse );
			if ( !token[0] ) {
				ri.Printf( PRINT_WARNING, "WARNING: missing parm for 'lightgridmulamb' keyword in shader '%s'\n", shader.name );
				continue;
			}

			scale = atof( token );
			if ( scale > 0 ) {
				tr.lightGridMulAmbient = scale;
			}
		}
		// lightgridmuldir <scale>  directional multiplier for lightgrid
		else if ( !Q_stricmp( token, "lightgridmuldir" ) )
		{
			float scale;

			token = COM_ParseExt( text, qfalse );
			if ( !token[0] ) {
				ri.Printf( PRINT_WARNING, "WARNING: missing parm for 'lightgridmuldir' keyword in shader '%s'\n", shader.name );
				continue;
			}

			scale = atof( token );
			if ( scale > 0 ) {
				tr.lightGridMulDirected = scale;
			}
		}
		// fogParms ( <red> <green> <blue> ) <depthForOpaque> [fog type]
		else if ( !Q_stricmp( token, "fogParms" ) )
		{
			if ( !ParseVector( text, 3, shader.fogParms.color ) ) {
				return qfalse;
			}

			if ( r_greyscale->integer )
			{
				float luminance;

				luminance = LUMA( shader.fogParms.color[0], shader.fogParms.color[1], shader.fogParms.color[2] );
				VectorSet( shader.fogParms.color, luminance, luminance, luminance );
			}
			else if ( r_greyscale->value )
			{
				float luminance;

				luminance = LUMA( shader.fogParms.color[0], shader.fogParms.color[1], shader.fogParms.color[2] );
				shader.fogParms.color[0] = LERP( shader.fogParms.color[0], luminance, r_greyscale->value );
				shader.fogParms.color[1] = LERP( shader.fogParms.color[1], luminance, r_greyscale->value );
				shader.fogParms.color[2] = LERP( shader.fogParms.color[2], luminance, r_greyscale->value );
			}

			token = COM_ParseExt( text, qfalse );
			if ( !token[0] ) {
				ri.Printf( PRINT_WARNING, "WARNING: missing parm for 'fogParms' keyword in shader '%s'\n", shader.name );
				continue;
			}

			shader.fogParms.depthForOpaque = atof( token );

			// note: there could not be a token, or token might be old gradient directions
			token = COM_ParseExt( text, qfalse );
			if ( !*token )
				token = r_defaultFogParmsType->string;

			if ( !Q_stricmp( token, "linear" ) ) {
				shader.fogParms.fogType = FT_LINEAR;
				shader.fogParms.density = DEFAULT_FOG_LINEAR_DENSITY;
				shader.fogParms.farClip = shader.fogParms.depthForOpaque;
			} else {
				if ( Q_stricmp( token, "exp" ) && !Q_isanumber( token ) )
					ri.Printf( PRINT_WARNING, "WARNING: unknown fog type '%s' for 'fogParms' keyword in shader '%s'\n", token, shader.name );

				shader.fogParms.fogType = FT_EXP;
				shader.fogParms.density = DEFAULT_FOG_EXP_DENSITY;
				shader.fogParms.farClip = 0;
			}

			// note: skips any old gradient directions
			continue;
		}
		// portal
		else if ( !Q_stricmp(token, "portal") )
		{
			shader.sort = SS_PORTAL;
			shader.isPortal = qtrue;
			continue;
		}
		// portalsky
		else if ( !Q_stricmp(token, "portalsky") )
		{
			// ZTM: FIXME: Not entirely sure what portalsky is suppose to do.
			continue;
		}
		// skyparms <cloudheight> <outerbox> <innerbox>
		else if ( !Q_stricmp( token, "skyparms" ) )
		{
			ParseSkyParms( text );
			continue;
		}
		// skyfogvars ( <red> <green> <blue> ) <density>
		else if ( !Q_stricmp( token, "skyfogvars" ) ) {
			vec3_t fogColor;
			float fogDensity;

			if ( !ParseVector( text, 3, fogColor ) ) {
				return qfalse;
			}
			token = COM_ParseExt( text, qfalse );

			if ( !token[0] ) {
				ri.Printf( PRINT_WARNING, "WARNING: missing density value for skyfogvars\n" );
				continue;
			}

			fogDensity = atof( token );

			if ( fogDensity > 1 ) {
				ri.Printf( PRINT_WARNING, "WARNING: last value for skyfogvars is 'density' which needs to be 0.0-1.0\n" );
				continue;
			}

			tr.skyFogType = FT_EXP;

			// sets how big sky box size is too, not just fog tcscale
			tr.skyFogDepthForOpaque = DEFAULT_FOG_EXP_DEPTH_FOR_OPAQUE;

			tr.skyFogColorInt = ColorBytes4 ( fogColor[0],
										  fogColor[1],
										  fogColor[2], 1.0 );

			tr.skyFogTcScale = R_FogTcScale( tr.skyFogType, tr.skyFogDepthForOpaque, fogDensity );
			continue;
		}
		// waterfogvars ( <red> <green> <blue> ) [density <= 1 or depthForOpaque > 1]
		else if ( !Q_stricmp( token, "waterfogvars" ) ) {
			vec3_t waterColor;
			float fogvar;

			if ( !ParseVector( text, 3, waterColor ) ) {
				return qfalse;
			}
			token = COM_ParseExt( text, qfalse );

			if ( !token[0] ) {
				ri.Printf( PRINT_WARNING, "WARNING: missing density/distance value for waterfogvars\n" );
				continue;
			}

			fogvar = atof( token );

			if ( fogvar == 0 ) {
				// Specifies "use the map values for everything except the fog color"
				tr.waterFogParms.fogType = FT_NONE;

				if ( waterColor[0] == 0 && waterColor[1] == 0 && waterColor[2] == 0 ) {
					// Color must be non-zero.
					waterColor[0] = waterColor[1] = waterColor[2] = 0.00001;
				}
			} else if ( fogvar > 1 ) {
				tr.waterFogParms.fogType = FT_LINEAR;
				tr.waterFogParms.depthForOpaque = fogvar;
				tr.waterFogParms.density = DEFAULT_FOG_LINEAR_DENSITY;
				tr.waterFogParms.farClip = fogvar;
			} else {
				tr.waterFogParms.fogType = FT_EXP;
				tr.waterFogParms.density = fogvar;
				tr.waterFogParms.depthForOpaque = DEFAULT_FOG_EXP_DEPTH_FOR_OPAQUE;
				tr.waterFogParms.farClip = 0;
			}

			VectorCopy( waterColor, tr.waterFogParms.color );
		}
		// viewfogvars ( <red> <green> <blue> ) [density <= 1 or depthForOpaque > 1]
		else if ( !Q_stricmp( token, "viewfogvars" ) ) {
			vec3_t viewColor;
			float fogvar;

			if ( !ParseVector( text, 3, viewColor ) ) {
				return qfalse;
			}
			token = COM_ParseExt( text, qfalse );

			if ( !token[0] ) {
				ri.Printf( PRINT_WARNING, "WARNING: missing density/distance value for viewfogvars\n" );
				continue;
			}

			fogvar = atof( token );

			if ( fogvar == 0 ) {
				// Specifies "use the map values for everything except the fog color"
				shader.viewFogParms.fogType = FT_NONE;

				if ( viewColor[0] == 0 && viewColor[1] == 0 && viewColor[2] == 0 ) {
					// Color must be non-zero.
					viewColor[0] = viewColor[1] = viewColor[2] = 0.00001;
				}
			} else if ( fogvar > 1 ) {
				shader.viewFogParms.fogType = FT_LINEAR;
				shader.viewFogParms.depthForOpaque = fogvar;
				shader.viewFogParms.density = DEFAULT_FOG_LINEAR_DENSITY;
			} else {
				shader.viewFogParms.fogType = FT_EXP;
				shader.viewFogParms.density = fogvar;
				shader.viewFogParms.depthForOpaque = DEFAULT_FOG_EXP_DEPTH_FOR_OPAQUE;
			}

			shader.viewFogParms.farClip = 0;

			VectorCopy( viewColor, shader.viewFogParms.color );
			continue;
		}
		// fogvars ( <red> <green> <blue> ) [density <= 1 or depthForOpaque > 1]
		else if ( !Q_stricmp( token, "fogvars" ) ) {
			vec3_t fogColor;
			float fogvar;

			if ( !ParseVector( text, 3, fogColor ) ) {
				return qfalse;
			}

			token = COM_ParseExt( text, qfalse );
			if ( !token[0] ) {
				ri.Printf( PRINT_WARNING, "WARNING: missing density value for the fog\n" );
				continue;
			}

			// fogFar > 1 means the shader is setting the farclip, < 1 means setting
			// density (so old maps or maps that just need softening fog don't have to care about farclip)
			fogvar = atof( token );

			if ( fogvar > 1 ) {
				tr.globalFogType = FT_LINEAR;
				tr.globalFogDepthForOpaque = fogvar;
				tr.globalFogDensity = DEFAULT_FOG_LINEAR_DENSITY;
				tr.globalFogFarClip = fogvar;
			} else {
				tr.globalFogType = FT_EXP;
				tr.globalFogDensity = fogvar;
				tr.globalFogDepthForOpaque = DEFAULT_FOG_EXP_DEPTH_FOR_OPAQUE;
				tr.globalFogFarClip = 0;
			}

			VectorCopy( fogColor, tr.globalFogColor );
			continue;
		}
		// nofog, allow disabling fog for some shaders
		else if ( !Q_stricmp( token, "nofog" ) ) {
			shader.noFog = qtrue;
			continue;
		}
		// allow each shader to permit compression if available
		else if ( !Q_stricmp( token, "allowcompress" ) ) {
			shader_allowCompress = qtrue;
			continue;
		} else if ( !Q_stricmp( token, "nocompress" ) || !Q_stricmp( token, "notc" ) ) {
			shader_allowCompress = qfalse;
			continue;
		}
		// light <value> determines flaring in q3map, not needed here
		else if ( !Q_stricmp(token, "light") ) 
		{
			COM_ParseExt( text, qfalse );
			continue;
		}
		// cull <face>
		else if ( !Q_stricmp( token, "cull") ) 
		{
			token = COM_ParseExt( text, qfalse );
			if ( token[0] == 0 )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing cull parms in shader '%s'\n", shader.name );
				continue;
			}

			if ( !Q_stricmp( token, "none" ) || !Q_stricmp( token, "twosided" ) || !Q_stricmp( token, "disable" ) )
			{
				shader.cullType = CT_TWO_SIDED;
			}
			else if ( !Q_stricmp( token, "back" ) || !Q_stricmp( token, "backside" ) || !Q_stricmp( token, "backsided" ) )
			{
				shader.cullType = CT_BACK_SIDED;
			}
			else if ( !Q_stricmp( token, "front" ) || !Q_stricmp( token, "frontside" ) || !Q_stricmp( token, "frontsided" ) )
			{
				shader.cullType = CT_FRONT_SIDED;
			}
			else
			{
				ri.Printf( PRINT_WARNING, "WARNING: invalid cull parm '%s' in shader '%s'\n", token, shader.name );
			}
			continue;
		}
		// distancecull <opaque distance> <transparent distance> <alpha threshold>
		else if ( !Q_stricmp( token, "distancecull" ) ) {
			int i;

			for ( i = 0; i < 3; i++ )
			{
				token = COM_ParseExt( text, qfalse );
				if ( token[ 0 ] == 0 ) {
					ri.Printf( PRINT_WARNING, "WARNING: missing distancecull parms in shader '%s'\n", shader.name );
				} else {
					shader.distanceCull[ i ] = atof( token );
				}
			}

			if ( shader.distanceCull[ 1 ] - shader.distanceCull[ 0 ] > 0 ) {
				// distanceCull[ 3 ] is an optimization
				shader.distanceCull[ 3 ] = 1.0 / ( shader.distanceCull[ 1 ] - shader.distanceCull[ 0 ] );
			} else {
				shader.distanceCull[ 0 ] = 0;
				shader.distanceCull[ 1 ] = 0;
				shader.distanceCull[ 2 ] = 0;
				shader.distanceCull[ 3 ] = 0;
			}
			continue;
		}
		// sort
		else if ( !Q_stricmp( token, "sort" ) )
		{
			ParseSort( text );
			continue;
		}
		// implicit default mapping to eliminate redundant/incorrect explicit shader stages
		else if ( !Q_stricmpn( token, "implicit", 8 ) ) {
			// set implicit mapping state
			if ( !Q_stricmp( token, "implicitBlend" ) ) {
				implicitStateBits = GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
				implicitCullType = CT_TWO_SIDED;
			} else if ( !Q_stricmp( token, "implicitMask" ) )     {
				implicitStateBits = GLS_DEPTHMASK_TRUE | GLS_ATEST_GE_80;
				implicitCullType = CT_TWO_SIDED;
			} else    // "implicitMap"
			{
				implicitStateBits = GLS_DEPTHMASK_TRUE;
				implicitCullType = CT_FRONT_SIDED;
			}

			// get image
			token = COM_ParseExt( text, qfalse );
			if ( token[ 0 ] != '\0' ) {
				Q_strncpyz( implicitMap, token, sizeof( implicitMap ) );
			} else {
				implicitMap[ 0 ] = '-';
				implicitMap[ 1 ] = '\0';
			}

			continue;
		}
		// aliasShader <shader>
		else if ( !Q_stricmp( token, "aliasShader" ) ) {
			token = COM_ParseExt( text, qfalse );
			if ( token[0] == 0 )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing shader for 'aliasShader' keyword in shader '%s'\n", shader.name );
				continue;
			}

			if ( r_aliasShaders->integer && Q_stricmp( token, shader.name ) ) {
				Q_strncpyz( aliasShader, token, sizeof ( aliasShader ) );
				return qtrue;
			}
		}
		// damageShader <shader> <health>
		else if ( !Q_stricmp( token, "damageShader" ) ) {
			token = COM_ParseExt( text, qfalse );
			if ( token[0] == 0 )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing shader for 'damageShader' keyword in shader '%s'\n", shader.name );
				continue;
			}

			ri.Printf( PRINT_WARNING, "WARNING: damageShader keyword is unsupported, '%s' in '%s'\n", token, shader.name );
		}
		// hitLocation <image>
		else if ( !Q_stricmp( token, "hitLocation" ) ) {
			token = COM_ParseExt( text, qfalse );
			if ( token[0] == 0 )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing image for 'hitLocation' keyword in shader '%s'\n", shader.name );
				continue;
			}

			ri.Printf( PRINT_WARNING, "WARNING: hitLocation keyword is unsupported, '%s' in '%s'\n", token, shader.name );
		}
		// hitMaterial <image>
		else if ( !Q_stricmp( token, "hitMaterial" ) ) {
			token = COM_ParseExt( text, qfalse );
			if ( token[0] == 0 )
			{
				ri.Printf( PRINT_WARNING, "WARNING: missing image for 'hitMaterial' keyword in shader '%s'\n", shader.name );
				continue;
			}

			ri.Printf( PRINT_WARNING, "WARNING: hitMaterial keyword is unsupported, '%s' in '%s'\n", token, shader.name );
		}
		// novlcollapse
		else if ( !Q_stricmp( token, "novlcollapse" ) ) {
			shader_novlcollapse = qtrue;
			continue;
		}
		// unknown directive
		else
		{
			ri.Printf( PRINT_WARNING, "WARNING: unknown general shader parameter '%s' in '%s'\n", token, shader.name );
			return qfalse;
		}
	}

	if ( ifIndent > 0 ) {
		ri.Printf( PRINT_WARNING, "WARNING: no concluding 'endif' in shader %s\n", shader.name );
		return qfalse;
	}

	//
	// ignore shaders that don't have any stages, unless it is a sky, fog, or have implicit mapping
	//
	if ( s == 0 && !shader.isSky && !( shader.surfaceParms & SURF_FOG ) && implicitMap[ 0 ] == '\0' ) {
		return qfalse;
	}

	shader.explicitlyDefined = qtrue;

	return qtrue;
}

/*
========================================================================================

SHADER OPTIMIZATION AND FOGGING

========================================================================================
*/

/*
===================
ComputeStageIteratorFunc

See if we can use on of the simple fastpath stage functions,
otherwise set to the generic stage function
===================
*/
static void ComputeStageIteratorFunc( void )
{
	shader.optimalStageIteratorFunc = RB_StageIteratorGeneric;

	//
	// see if this should go into the sky path
	//
	if ( shader.isSky )
	{
		shader.optimalStageIteratorFunc = RB_StageIteratorSky;
		return;
	}
}

/*
===================
ComputeVertexAttribs

Check which vertex attributes we only need, so we
don't need to submit/copy all of them.
===================
*/
static void ComputeVertexAttribs(void)
{
	int i, stage;

	// dlights always need ATTR_NORMAL
	shader.vertexAttribs = ATTR_POSITION | ATTR_NORMAL;

	// portals always need normals, for SurfIsOffscreen()
	if (shader.isPortal)
	{
		shader.vertexAttribs |= ATTR_NORMAL;
	}

	if (shader.defaultShader)
	{
		shader.vertexAttribs |= ATTR_TEXCOORD;
		return;
	}

	if(shader.numDeforms)
	{
		for ( i = 0; i < shader.numDeforms; i++)
		{
			deformStage_t  *ds = &shader.deforms[i];

			switch (ds->deformation)
			{
				case DEFORM_BULGE:
					shader.vertexAttribs |= ATTR_NORMAL | ATTR_TEXCOORD;
					break;

				case DEFORM_AUTOSPRITE:
					shader.vertexAttribs |= ATTR_NORMAL | ATTR_COLOR;
					break;

				case DEFORM_WAVE:
				case DEFORM_NORMALS:
				case DEFORM_TEXT0:
				case DEFORM_TEXT1:
				case DEFORM_TEXT2:
				case DEFORM_TEXT3:
				case DEFORM_TEXT4:
				case DEFORM_TEXT5:
				case DEFORM_TEXT6:
				case DEFORM_TEXT7:
					shader.vertexAttribs |= ATTR_NORMAL;
					break;

				default:
				case DEFORM_NONE:
				case DEFORM_MOVE:
				case DEFORM_PROJECTION_SHADOW:
				case DEFORM_AUTOSPRITE2:
					break;
			}
		}
	}

	for ( stage = 0; stage < MAX_SHADER_STAGES; stage++ )
	{
		shaderStage_t *pStage = &stages[stage];

		if ( !pStage->active ) 
		{
			break;
		}

		if (pStage->glslShaderGroup == tr.lightallShader)
		{
			shader.vertexAttribs |= ATTR_NORMAL;

			if ((pStage->glslShaderIndex & LIGHTDEF_LIGHTTYPE_MASK) && !(r_normalMapping->integer == 0 && r_specularMapping->integer == 0))
			{
				shader.vertexAttribs |= ATTR_TANGENT;
			}

			switch (pStage->glslShaderIndex & LIGHTDEF_LIGHTTYPE_MASK)
			{
				case LIGHTDEF_USE_LIGHTMAP:
				case LIGHTDEF_USE_LIGHT_VERTEX:
					shader.vertexAttribs |= ATTR_LIGHTDIRECTION;
					break;
				default:
					break;
			}
		}

		for (i = 0; i < NUM_TEXTURE_BUNDLES; i++)
		{
			if ( pStage->bundle[i].image[0] == 0 )
			{
				continue;
			}

			switch(pStage->bundle[i].tcGen)
			{
				case TCGEN_TEXTURE:
					shader.vertexAttribs |= ATTR_TEXCOORD;
					break;
				case TCGEN_LIGHTMAP:
					shader.vertexAttribs |= ATTR_LIGHTCOORD;
					break;
				case TCGEN_ENVIRONMENT_MAPPED:
					shader.vertexAttribs |= ATTR_NORMAL;
					break;

				default:
					break;
			}
		}

		switch(pStage->rgbGen)
		{
			case CGEN_EXACT_VERTEX:
			case CGEN_VERTEX:
			case CGEN_EXACT_VERTEX_LIT:
			case CGEN_VERTEX_LIT:
			case CGEN_ONE_MINUS_VERTEX:
				shader.vertexAttribs |= ATTR_COLOR;
				break;

			case CGEN_LIGHTING_DIFFUSE:
			case CGEN_LIGHTING_DIFFUSE_ENTITY:
				shader.vertexAttribs |= ATTR_NORMAL;
				break;

			default:
				break;
		}

		switch(pStage->alphaGen)
		{
			case AGEN_LIGHTING_SPECULAR:
				shader.vertexAttribs |= ATTR_NORMAL;
				break;

			case AGEN_VERTEX:
			case AGEN_ONE_MINUS_VERTEX:
				shader.vertexAttribs |= ATTR_COLOR;
				break;

			default:
				break;
		}
	}
}

typedef struct {
	int		blendA;
	int		blendB;

	int		multitextureEnv;
	int		multitextureBlend;
} collapse_t;

static collapse_t	collapse[] = {
	{ 0, GLS_DSTBLEND_SRC_COLOR | GLS_SRCBLEND_ZERO,	
		GL_MODULATE, 0 },

	{ 0, GLS_DSTBLEND_ZERO | GLS_SRCBLEND_DST_COLOR,
		GL_MODULATE, 0 },

	{ GLS_DSTBLEND_ZERO | GLS_SRCBLEND_DST_COLOR, GLS_DSTBLEND_ZERO | GLS_SRCBLEND_DST_COLOR,
		GL_MODULATE, GLS_DSTBLEND_ZERO | GLS_SRCBLEND_DST_COLOR },

	{ GLS_DSTBLEND_SRC_COLOR | GLS_SRCBLEND_ZERO, GLS_DSTBLEND_ZERO | GLS_SRCBLEND_DST_COLOR,
		GL_MODULATE, GLS_DSTBLEND_ZERO | GLS_SRCBLEND_DST_COLOR },

	{ GLS_DSTBLEND_ZERO | GLS_SRCBLEND_DST_COLOR, GLS_DSTBLEND_SRC_COLOR | GLS_SRCBLEND_ZERO,
		GL_MODULATE, GLS_DSTBLEND_ZERO | GLS_SRCBLEND_DST_COLOR },

	{ GLS_DSTBLEND_SRC_COLOR | GLS_SRCBLEND_ZERO, GLS_DSTBLEND_SRC_COLOR | GLS_SRCBLEND_ZERO,
		GL_MODULATE, GLS_DSTBLEND_ZERO | GLS_SRCBLEND_DST_COLOR },

	{ 0, GLS_DSTBLEND_ONE | GLS_SRCBLEND_ONE,
		GL_ADD, 0 },

	{ GLS_DSTBLEND_ONE | GLS_SRCBLEND_ONE, GLS_DSTBLEND_ONE | GLS_SRCBLEND_ONE,
		GL_ADD, GLS_DSTBLEND_ONE | GLS_SRCBLEND_ONE },
#if 0
	{ 0, GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_SRCBLEND_SRC_ALPHA,
		GL_DECAL, 0 },
#endif
	{ -1 }
};

/*
================
CollapseMultitexture

Attempt to combine two stages into a single multitexture stage
FIXME: I think modulated add + modulated add collapses incorrectly
=================
*/
static qboolean CollapseMultitexture( void ) {
	int abits, bbits;
	int i;
	textureBundle_t tmpBundle;

	if ( !qglActiveTextureARB ) {
		return qfalse;
	}

	// make sure both stages are active
	if ( !stages[0].active || !stages[1].active ) {
		return qfalse;
	}

	// make sure not already using multitexture
	if ( stages[0].multitextureEnv || stages[1].multitextureEnv ) {
		return qfalse;
	}

	abits = stages[0].stateBits;
	bbits = stages[1].stateBits;

	// make sure that both stages have identical state other than blend modes
	if ( ( abits & ~( GLS_DSTBLEND_BITS | GLS_SRCBLEND_BITS | GLS_DEPTHMASK_TRUE ) ) !=
		( bbits & ~( GLS_DSTBLEND_BITS | GLS_SRCBLEND_BITS | GLS_DEPTHMASK_TRUE ) ) ) {
		return qfalse;
	}

	abits &= ( GLS_DSTBLEND_BITS | GLS_SRCBLEND_BITS );
	bbits &= ( GLS_DSTBLEND_BITS | GLS_SRCBLEND_BITS );

	// search for a valid multitexture blend function
	for ( i = 0; collapse[i].blendA != -1 ; i++ ) {
		if ( abits == collapse[i].blendA
			&& bbits == collapse[i].blendB ) {
			break;
		}
	}

	// nothing found
	if ( collapse[i].blendA == -1 ) {
		return qfalse;
	}

	// GL_ADD is a separate extension
	if ( collapse[i].multitextureEnv == GL_ADD && !glConfig.textureEnvAddAvailable ) {
		return qfalse;
	}

	// make sure waveforms have identical parameters
	if ( ( stages[0].rgbGen != stages[1].rgbGen ) ||
		( stages[0].alphaGen != stages[1].alphaGen ) )  {
		return qfalse;
	}

	// an add collapse can only have identity colors
	if ( collapse[i].multitextureEnv == GL_ADD && stages[0].rgbGen != CGEN_IDENTITY ) {
		return qfalse;
	}

	if ( stages[0].rgbGen == CGEN_WAVEFORM || stages[0].rgbGen == CGEN_COLOR_WAVEFORM )
	{
		if ( memcmp( &stages[0].rgbWave,
					 &stages[1].rgbWave,
					 sizeof( stages[0].rgbWave ) ) )
		{
			return qfalse;
		}
	}
	if ( stages[0].alphaGen == AGEN_WAVEFORM )
	{
		if ( memcmp( &stages[0].alphaWave,
					 &stages[1].alphaWave,
					 sizeof( stages[0].alphaWave ) ) )
		{
			return qfalse;
		}
	}


	// make sure that lightmaps are in bundle 1 for 3dfx
	if ( stages[0].bundle[0].isLightmap )
	{
		tmpBundle = stages[0].bundle[0];
		stages[0].bundle[0] = stages[1].bundle[0];
		stages[0].bundle[1] = tmpBundle;
	}
	else
	{
		stages[0].bundle[1] = stages[1].bundle[0];
	}

	// set the new blend state bits
	stages[0].multitextureEnv = collapse[i].multitextureEnv;
	stages[0].stateBits &= ~( GLS_DSTBLEND_BITS | GLS_SRCBLEND_BITS );
	stages[0].stateBits |= collapse[i].multitextureBlend;

	//
	// move down subsequent shaders
	//
	memmove( &stages[1], &stages[2], sizeof( stages[0] ) * ( MAX_SHADER_STAGES - 2 ) );
	Com_Memset( &stages[MAX_SHADER_STAGES-1], 0, sizeof( stages[0] ) );

	return qtrue;
}

static void CopyBundle( const textureBundle_t *from, textureBundle_t *to ) {
	image_t **origImagePtr = to->image;
	int i, numImages;

	*to = *from;

	to->image = origImagePtr;
	numImages = MAX( to->numImageAnimations, 1 );
	for ( i = 0; i < numImages; i++ ) {
		to->image[i] = from->image[i];
	}
}

static void CollapseStagesToLightall(shaderStage_t *diffuse, 
	shaderStage_t *normal, shaderStage_t *specular, shaderStage_t *lightmap, 
	qboolean useLightVector, qboolean useLightVertex, qboolean parallax, qboolean tcgen)
{
	int defs = 0;

	//ri.Printf(PRINT_ALL, "shader %s has diffuse %s", shader.name, diffuse->bundle[0].image[0]->imgName);

	// reuse diffuse, mark others inactive
	diffuse->type = ST_GLSL;
	diffuse->multitextureEnv = 0;

	if (lightmap)
	{
		//ri.Printf(PRINT_ALL, ", lightmap");
		CopyBundle( &lightmap->bundle[0], &diffuse->bundle[TB_LIGHTMAP] );
		defs |= LIGHTDEF_USE_LIGHTMAP;
	}
	else if (diffuse->bundle[1].isLightmap)
	{
		// shader using nextbundle, lightmap already setup
		//ri.Printf(PRINT_ALL, ", lightmap");
		defs |= LIGHTDEF_USE_LIGHTMAP;
	}
	else if (useLightVector)
	{
		defs |= LIGHTDEF_USE_LIGHT_VECTOR;
	}
	else if (useLightVertex)
	{
		defs |= LIGHTDEF_USE_LIGHT_VERTEX;
	}

	if (r_deluxeMapping->integer && tr.worldDeluxeMapping && lightmap && shader.lightmapIndex >= 0)
	{
		//ri.Printf(PRINT_ALL, ", deluxemap");
		CopyBundle( &lightmap->bundle[0], &diffuse->bundle[TB_DELUXEMAP] );
		diffuse->bundle[TB_DELUXEMAP].image[0] = tr.deluxemaps[shader.lightmapIndex];
	}
	// ZTM: FIXME: handle shader using nextbundle with deluxmapping?

	if (r_normalMapping->integer)
	{
		image_t *diffuseImg;
		if (normal)
		{
			//ri.Printf(PRINT_ALL, ", normalmap %s", normal->bundle[0].image[0]->imgName);
			CopyBundle( &normal->bundle[0], &diffuse->bundle[TB_NORMALMAP] );
			if (parallax && r_parallaxMapping->integer)
				defs |= LIGHTDEF_USE_PARALLAXMAP;

			VectorCopy4(normal->normalScale, diffuse->normalScale);
		}
		else if ((lightmap || useLightVector || useLightVertex) && (diffuseImg = diffuse->bundle[TB_DIFFUSEMAP].image[0]))
		{
			char normalName[MAX_QPATH];
			image_t *normalImg;
			imgFlags_t normalFlags = (diffuseImg->flags & ~IMGFLAG_GENNORMALMAP) | IMGFLAG_NOLIGHTSCALE;

			// ZTM: FIXME: check shader_allowCompress? I don't think it's always valid here though.

			// try a normalheight image first
			COM_StripExtension(diffuseImg->imgName, normalName, MAX_QPATH);
			Q_strcat(normalName, MAX_QPATH, "_nh");

			normalImg = R_FindImageFile(normalName, IMGTYPE_NORMALHEIGHT, normalFlags);

			if (normalImg)
			{
				parallax = qtrue;
			}
			else
			{
				// try a normal image ("_n" suffix)
				normalName[strlen(normalName) - 1] = '\0';
				normalImg = R_FindImageFile(normalName, IMGTYPE_NORMAL, normalFlags);
			}

			if (normalImg)
			{
				CopyBundle( &diffuse->bundle[0], &diffuse->bundle[TB_NORMALMAP] );
				diffuse->bundle[TB_NORMALMAP].image[0] = normalImg;

				if (parallax && r_parallaxMapping->integer)
					defs |= LIGHTDEF_USE_PARALLAXMAP;

				VectorSet4(diffuse->normalScale, r_baseNormalX->value, r_baseNormalY->value, 1.0f, r_baseParallax->value);
			}
		}
	}

	if (r_specularMapping->integer)
	{
		image_t *diffuseImg;
		if (specular)
		{
			//ri.Printf(PRINT_ALL, ", specularmap %s", specular->bundle[0].image[0]->imgName);
			CopyBundle( &specular->bundle[0], &diffuse->bundle[TB_SPECULARMAP] );
			VectorCopy4(specular->specularScale, diffuse->specularScale);
		}
		else if ((lightmap || useLightVector || useLightVertex) && (diffuseImg = diffuse->bundle[TB_DIFFUSEMAP].image[0]))
		{
			char specularName[MAX_QPATH];
			image_t *specularImg;
			imgFlags_t specularFlags = (diffuseImg->flags & ~IMGFLAG_GENNORMALMAP) | IMGFLAG_NOLIGHTSCALE;

			COM_StripExtension(diffuseImg->imgName, specularName, MAX_QPATH);
			Q_strcat(specularName, MAX_QPATH, "_s");

			specularImg = R_FindImageFile(specularName, IMGTYPE_COLORALPHA, specularFlags);

			if (specularImg)
			{
				diffuse->bundle[TB_SPECULARMAP] = diffuse->bundle[0];
				diffuse->bundle[TB_SPECULARMAP].numImageAnimations = 0;
				diffuse->bundle[TB_SPECULARMAP].image[0] = specularImg;

				VectorSet4(diffuse->specularScale, 1.0f, 1.0f, 1.0f, 1.0f);
			}
		}
	}

	if (tcgen || diffuse->bundle[0].numTexMods)
	{
		defs |= LIGHTDEF_USE_TCGEN_AND_TCMOD;
	}

	//ri.Printf(PRINT_ALL, ".\n");

	diffuse->glslShaderGroup = tr.lightallShader;
	diffuse->glslShaderIndex = defs;
}


static int CollapseStagesToGLSL(void)
{
	int i, j, numStages;
	qboolean skip = qfalse;

	// skip shaders with deforms
	if (shader.numDeforms != 0)
	{
		skip = qtrue;
	}

	if (!skip)
	{
		// if 2+ stages and first stage is lightmap, switch them
		// this makes it easier for the later bits to process
		if (stages[0].active && stages[0].bundle[0].tcGen == TCGEN_LIGHTMAP && stages[1].active)
		{
			int blendBits = stages[1].stateBits & ( GLS_DSTBLEND_BITS | GLS_SRCBLEND_BITS );

			if (blendBits == (GLS_DSTBLEND_SRC_COLOR | GLS_SRCBLEND_ZERO)
				|| blendBits == (GLS_DSTBLEND_ZERO | GLS_SRCBLEND_DST_COLOR))
			{
				int stateBits0 = stages[0].stateBits;
				int stateBits1 = stages[1].stateBits;
				shaderStage_t swapStage;

				swapStage = stages[0];
				stages[0] = stages[1];
				stages[1] = swapStage;

				stages[0].stateBits = stateBits0;
				stages[1].stateBits = stateBits1;
			}
		}
	}

	if (!skip)
	{
		// scan for shaders that aren't supported
		for (i = 0; i < MAX_SHADER_STAGES; i++)
		{
			shaderStage_t *pStage = &stages[i];

			if (!pStage->active)
				continue;

			if (pStage->adjustColorsForFog)
			{
				skip = qtrue;
				break;
			}

			if (pStage->multitextureEnv)
			{
				// ZTM: TODO: add more checks here? tcmod, tcgen, etc?
				if ( pStage->multitextureEnv != GL_MODULATE
					|| !pStage->bundle[1].isLightmap )
				{
					skip = qtrue;
					break;
				}
			}

			if (pStage->bundle[0].tcGen == TCGEN_LIGHTMAP)
			{
				int blendBits = pStage->stateBits & ( GLS_DSTBLEND_BITS | GLS_SRCBLEND_BITS );
				
				if (blendBits != (GLS_DSTBLEND_SRC_COLOR | GLS_SRCBLEND_ZERO)
					&& blendBits != (GLS_DSTBLEND_ZERO | GLS_SRCBLEND_DST_COLOR))
				{
					skip = qtrue;
					break;
				}
			}

			switch(pStage->bundle[0].tcGen)
			{
				case TCGEN_TEXTURE:
				case TCGEN_LIGHTMAP:
				case TCGEN_ENVIRONMENT_MAPPED:
				case TCGEN_ENVIRONMENT_CELSHADE_MAPPED:
				case TCGEN_VECTOR:
					break;
				default:
					skip = qtrue;
					break;
			}

			switch(pStage->alphaGen)
			{
				case AGEN_LIGHTING_SPECULAR:
				case AGEN_PORTAL:
				case AGEN_NORMALZFADE:
					skip = qtrue;
					break;
				default:
					break;
			}

			switch(pStage->rgbGen)
			{
				case CGEN_LIGHTING_DIFFUSE_ENTITY:
					skip = qtrue;
					break;
				default:
					break;
			}
		}
	}

	if (!skip)
	{
		qboolean usedLightmap = qfalse;

		for (i = 0; i < MAX_SHADER_STAGES; i++)
		{
			shaderStage_t *pStage = &stages[i];
			shaderStage_t *diffuse, *normal, *specular, *lightmap;
			qboolean parallax, tcgen, diffuselit, vertexlit;

			if (!pStage->active)
				continue;

			// skip normal and specular maps
			if (pStage->type != ST_COLORMAP)
				continue;

			// skip lightmaps
			if (pStage->bundle[0].tcGen == TCGEN_LIGHTMAP)
				continue;

			diffuse  = pStage;
			normal   = NULL;
			parallax = qfalse;
			specular = NULL;
			lightmap = NULL;

			// we have a diffuse map, find matching normal, specular, and lightmap
			for (j = i + 1; j < MAX_SHADER_STAGES; j++)
			{
				shaderStage_t *pStage2 = &stages[j];

				if (!pStage2->active)
					continue;

				switch(pStage2->type)
				{
					case ST_NORMALMAP:
						if (!normal)
						{
							normal = pStage2;
						}
						break;

					case ST_NORMALPARALLAXMAP:
						if (!normal)
						{
							normal = pStage2;
							parallax = qtrue;
						}
						break;

					case ST_SPECULARMAP:
						if (!specular)
						{
							specular = pStage2;
						}
						break;

					case ST_COLORMAP:
						if (pStage2->bundle[0].tcGen == TCGEN_LIGHTMAP)
						{
							int blendBits = pStage->stateBits & ( GLS_DSTBLEND_BITS | GLS_SRCBLEND_BITS );

							// Only add lightmap to blendfunc filter stage if it's the first time lightmap is used
							// otherwise it will cause the shader to be darkened by the lightmap multiple times.
							if (!usedLightmap || (blendBits != (GLS_DSTBLEND_SRC_COLOR | GLS_SRCBLEND_ZERO)
								&& blendBits != (GLS_DSTBLEND_ZERO | GLS_SRCBLEND_DST_COLOR)))
							{
								lightmap = pStage2;
								usedLightmap = qtrue;
							}
						}
						break;

					default:
						break;
				}
			}

			tcgen = qfalse;
			if (diffuse->bundle[0].tcGen == TCGEN_ENVIRONMENT_MAPPED
			    || diffuse->bundle[0].tcGen == TCGEN_ENVIRONMENT_CELSHADE_MAPPED
			    || diffuse->bundle[0].tcGen == TCGEN_LIGHTMAP
			    || diffuse->bundle[0].tcGen == TCGEN_VECTOR)
			{
				tcgen = qtrue;
			}

			diffuselit = qfalse;
			if (diffuse->rgbGen == CGEN_LIGHTING_DIFFUSE)
			{
				diffuselit = qtrue;
			}

			vertexlit = qfalse;
			if (diffuse->rgbGen == CGEN_VERTEX_LIT || diffuse->rgbGen == CGEN_EXACT_VERTEX_LIT)
			{
				vertexlit = qtrue;
			}

			CollapseStagesToLightall(diffuse, normal, specular, lightmap, diffuselit, vertexlit, parallax, tcgen);
		}

		// deactivate lightmap stages
		for (i = 0; i < MAX_SHADER_STAGES; i++)
		{
			shaderStage_t *pStage = &stages[i];

			if (!pStage->active)
				continue;

			if (pStage->bundle[0].tcGen == TCGEN_LIGHTMAP)
			{
				pStage->active = qfalse;
			}
		}
	}

	// deactivate normal and specular stages
	for (i = 0; i < MAX_SHADER_STAGES; i++)
	{
		shaderStage_t *pStage = &stages[i];

		if (!pStage->active)
			continue;

		if (pStage->type == ST_NORMALMAP)
		{
			pStage->active = qfalse;
		}

		if (pStage->type == ST_NORMALPARALLAXMAP)
		{
			pStage->active = qfalse;
		}

		if (pStage->type == ST_SPECULARMAP)
		{
			pStage->active = qfalse;
		}			
	}

	// remove inactive stages
	numStages = 0;
	for (i = 0; i < MAX_SHADER_STAGES; i++)
	{
		if (!stages[i].active)
			continue;

		if (i == numStages)
		{
			numStages++;
			continue;
		}

		stages[numStages] = stages[i];
		stages[i].active = qfalse;
		numStages++;
	}

	if (numStages == i && i >= 2 && CollapseMultitexture())
		numStages--;

	// convert any remaining lightmap stages with no blending or blendfunc filter
	// to a lighting pass with a white texture
	// only do this with r_sunlightMode non-zero, as it's only for correct shadows.
	if (r_sunlightMode->integer && shader.numDeforms == 0)
	{
		for (i = 0; i < MAX_SHADER_STAGES; i++)
		{
			shaderStage_t *pStage = &stages[i];
			int blendBits;

			if (!pStage->active)
				continue;

			if (pStage->adjustColorsForFog)
				continue;

			// ZTM: FIXME? probably need special handling for 'nextbundle' multitexture here
			if (pStage->multitextureEnv)
				continue;

			switch(pStage->alphaGen)
			{
				case AGEN_LIGHTING_SPECULAR:
				case AGEN_PORTAL:
					continue;
				default:
					break;
			}

			if (pStage->bundle[TB_DIFFUSEMAP].tcGen != TCGEN_LIGHTMAP)
				continue;

			blendBits = pStage->stateBits & (GLS_DSTBLEND_BITS | GLS_SRCBLEND_BITS);

			if (blendBits != 0 &&
				blendBits != (GLS_DSTBLEND_SRC_COLOR | GLS_SRCBLEND_ZERO) &&
				blendBits != (GLS_DSTBLEND_ZERO | GLS_SRCBLEND_DST_COLOR)) {
				continue;
			}

			pStage->glslShaderGroup = tr.lightallShader;
			pStage->glslShaderIndex = LIGHTDEF_USE_LIGHTMAP;
			CopyBundle( &pStage->bundle[TB_DIFFUSEMAP], &pStage->bundle[TB_LIGHTMAP] );
			pStage->bundle[TB_DIFFUSEMAP].image[0] = tr.whiteImage;
			pStage->bundle[TB_DIFFUSEMAP].isLightmap = qfalse;
			pStage->bundle[TB_DIFFUSEMAP].tcGen = TCGEN_TEXTURE;
		}
	}

	// convert any remaining lightingdiffuse stages to a lighting pass
	if (shader.numDeforms == 0)
	{
		for (i = 0; i < MAX_SHADER_STAGES; i++)
		{
			shaderStage_t *pStage = &stages[i];

			if (!pStage->active)
				continue;

			if (pStage->adjustColorsForFog)
				continue;

			if (pStage->multitextureEnv)
				continue;

			switch(pStage->alphaGen)
			{
				case AGEN_LIGHTING_SPECULAR:
				case AGEN_PORTAL:
					continue;
				default:
					break;
			}

			if (pStage->rgbGen == CGEN_LIGHTING_DIFFUSE)
			{
				pStage->glslShaderGroup = tr.lightallShader;
				pStage->glslShaderIndex = LIGHTDEF_USE_LIGHT_VECTOR;

				if (pStage->bundle[0].tcGen != TCGEN_TEXTURE || pStage->bundle[0].numTexMods != 0)
					pStage->glslShaderIndex |= LIGHTDEF_USE_TCGEN_AND_TCMOD;
			}
		}
	}

	return numStages;
}

/*
=============

FixRenderCommandList
https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=493
Arnout: this is a nasty issue. Shaders can be registered after drawsurfaces are generated
but before the frame is rendered. This will, for the duration of one frame, cause drawsurfaces
to be rendered with bad shaders. To fix this, need to go through all render commands and fix
sortedIndex.
ZTM: Spearmint keeps shaderIndex in drawSurf_t so this function only fixes the sort order,
not the actual shader
==============
*/
static void FixRenderCommandList( int newSortOrder ) {
	renderCommandList_t	*cmdList = &backEndData->commands;

	if( cmdList ) {
		const void *curCmd = cmdList->cmds;

		while ( 1 ) {
			curCmd = PADP(curCmd, sizeof(void *));

			switch ( *(const int *)curCmd ) {
			case RC_SET_COLOR:
				{
				const setColorCommand_t *sc_cmd = (const setColorCommand_t *)curCmd;
				curCmd = (const void *)(sc_cmd + 1);
				break;
				}
			case RC_STRETCH_PIC:
			case RC_ROTATED_PIC:
			case RC_STRETCH_PIC_GRADIENT:
				{
				const stretchPicCommand_t *sp_cmd = (const stretchPicCommand_t *)curCmd;
				curCmd = (const void *)(sp_cmd + 1);
				break;
				}
			case RC_2DPOLYS:
				{
				const poly2dCommand_t *sp_cmd = (const poly2dCommand_t *)curCmd;
				curCmd = (const void *)( sp_cmd + 1 );
				break;
				}
			case RC_DRAW_SURFS:
				{
				int i;
				drawSurf_t	*drawSurf;
				shader_t	*shader;
				int			sortOrder;
				int			fogNum;
				int			entityNum;
				int			dlightMap;
				int         pshadowMap;
				const drawSurfsCommand_t *ds_cmd =  (const drawSurfsCommand_t *)curCmd;

				for( i = 0, drawSurf = ds_cmd->drawSurfs; i < ds_cmd->numDrawSurfs; i++, drawSurf++ ) {
					R_DecomposeSort(drawSurf, &shader, &sortOrder, &entityNum, &fogNum, &dlightMap, &pshadowMap);
					if( sortOrder >= newSortOrder ) {
						sortOrder++;
						R_ComposeSort(drawSurf, shader->sortedIndex, sortOrder, (entityNum << QSORT_REFENTITYNUM_SHIFT),
										fogNum, dlightMap, pshadowMap);
					}
				}
				curCmd = (const void *)(ds_cmd + 1);
				break;
				}
			case RC_DRAW_BUFFER:
				{
				const drawBufferCommand_t *db_cmd = (const drawBufferCommand_t *)curCmd;
				curCmd = (const void *)(db_cmd + 1);
				break;
				}
			case RC_SWAP_BUFFERS:
				{
				const swapBuffersCommand_t *sb_cmd = (const swapBuffersCommand_t *)curCmd;
				curCmd = (const void *)(sb_cmd + 1);
				break;
				}
			case RC_END_OF_LIST:
			default:
				return;
			}
		}
	}
}

/*
==============
SortNewShader

Positions the most recently created shader in the tr.sortedShaders[]
array so that the shader->sort key is sorted reletive to the other
shaders.

Sets shader->sortedIndex
==============
*/
static void SortNewShader( void ) {
	int		i;
	int		newSortOrder;
	float	sort;
	shader_t	*newShader;

	newShader = tr.shaders[ tr.numShaders - 1 ];
	sort = newShader->sort;

	for ( i = tr.numShaders - 2 ; i >= 0 ; i-- ) {
		if ( tr.sortedShaders[ i ]->sort <= sort ) {
			break;
		}
		tr.sortedShaders[i+1] = tr.sortedShaders[i];
		tr.sortedShaders[i+1]->sortedIndex++;
	}

	// Arnout: fix rendercommandlist
	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=493
	newSortOrder = 1024 * ( sort - 1 ) + newShader->sortedIndex;
	FixRenderCommandList( newSortOrder+1 );

	newShader->sortedIndex = i+1;
	tr.sortedShaders[i+1] = newShader;
}


/*
====================
GeneratePermanentShader
====================
*/
static shader_t *GeneratePermanentShader( void ) {
	shader_t	*newShader;
	int			i, b;
	int			size, hash;

	if ( tr.numShaders == MAX_SHADERS ) {
		ri.Printf( PRINT_WARNING, "WARNING: GeneratePermanentShader - MAX_SHADERS hit\n");
		return tr.defaultShader;
	}

	newShader = ri.Hunk_Alloc( sizeof( shader_t ), h_low );

	*newShader = shader;

	tr.shaders[ tr.numShaders ] = newShader;
	newShader->index = tr.numShaders;
	
	tr.sortedShaders[ tr.numShaders ] = newShader;
	newShader->sortedIndex = tr.numShaders;

	tr.numShaders++;

	tr.numShadersSort[ (int)floor( newShader->sort ) ]++;

	for ( i = 0 ; i < newShader->numUnfoggedPasses ; i++ ) {
		if ( !stages[i].active ) {
			break;
		}
		newShader->stages[i] = ri.Hunk_Alloc( sizeof( stages[i] ), h_low );
		*newShader->stages[i] = stages[i];

		for ( b = 0 ; b < NUM_TEXTURE_BUNDLES ; b++ ) {
			size = newShader->stages[i]->bundle[b].numTexMods * sizeof( texModInfo_t );
			newShader->stages[i]->bundle[b].texMods = ri.Hunk_Alloc( size, h_low );
			Com_Memcpy( newShader->stages[i]->bundle[b].texMods, stages[i].bundle[b].texMods, size );

			if ( newShader->stages[i]->bundle[b].numImageAnimations )
				size = newShader->stages[i]->bundle[b].numImageAnimations * sizeof( image_t * );
			else
				size = sizeof( image_t * );

			newShader->stages[i]->bundle[b].image = ri.Hunk_Alloc( size, h_low );
			Com_Memcpy( newShader->stages[i]->bundle[b].image, stages[i].bundle[b].image, size );
		}
	}

	SortNewShader();

	hash = generateHashValue(newShader->name, FILE_HASH_SIZE);
	newShader->next = hashTable[hash];
	hashTable[hash] = newShader;

	return newShader;
}

/*
=================
VertexLightingCollapse

If vertex lighting is enabled, only render a single
pass, trying to guess which is the correct one to best approximate
what it is supposed to look like.
=================
*/
static void VertexLightingCollapse( void ) {
	int		stage;
	shaderStage_t	*bestStage;
	int		bestImageRank;
	int		rank;

	// if we aren't opaque, just use the first pass
	if ( shader.sort == SS_OPAQUE ) {

		// pick the best texture for the single pass
		bestStage = &stages[0];
		bestImageRank = -999999;

		for ( stage = 0; stage < MAX_SHADER_STAGES; stage++ ) {
			shaderStage_t *pStage = &stages[stage];

			if ( !pStage->active ) {
				break;
			}
			rank = 0;

			if ( pStage->bundle[0].isLightmap ) {
				rank -= 100;
			}
			if ( pStage->bundle[0].tcGen != TCGEN_TEXTURE ) {
				rank -= 5;
			}
			if ( pStage->bundle[0].numTexMods ) {
				rank -= 5;
			}
			if ( pStage->rgbGen != CGEN_IDENTITY && pStage->rgbGen != CGEN_IDENTITY_LIGHTING ) {
				rank -= 3;
			}

			if ( rank > bestImageRank  ) {
				bestImageRank = rank;
				bestStage = pStage;
			}
		}

		stages[0].bundle[0] = bestStage->bundle[0];
		stages[0].stateBits &= ~( GLS_DSTBLEND_BITS | GLS_SRCBLEND_BITS );
		stages[0].stateBits |= GLS_DEPTHMASK_TRUE;
		stages[0].rgbGen = R_DiffuseColorGen( shader.lightmapIndex );
		stages[0].alphaGen = AGEN_SKIP;		
	} else {
		// don't use a lightmap (tesla coils)
		if ( stages[0].bundle[0].isLightmap ) {
			stages[0] = stages[1];
		}

		// if we were in a cross-fade cgen, hack it to normal
		if ( stages[0].rgbGen == CGEN_ONE_MINUS_ENTITY || stages[1].rgbGen == CGEN_ONE_MINUS_ENTITY ) {
			stages[0].rgbGen = CGEN_IDENTITY_LIGHTING;
		}
		if ( ( stages[0].rgbGen == CGEN_WAVEFORM && stages[0].rgbWave.func == GF_SAWTOOTH )
			&& ( stages[1].rgbGen == CGEN_WAVEFORM && stages[1].rgbWave.func == GF_INVERSE_SAWTOOTH ) ) {
			stages[0].rgbGen = CGEN_IDENTITY_LIGHTING;
		}
		if ( ( stages[0].rgbGen == CGEN_WAVEFORM && stages[0].rgbWave.func == GF_INVERSE_SAWTOOTH )
			&& ( stages[1].rgbGen == CGEN_WAVEFORM && stages[1].rgbWave.func == GF_SAWTOOTH ) ) {
			stages[0].rgbGen = CGEN_IDENTITY_LIGHTING;
		}
	}

	for ( stage = 1; stage < MAX_SHADER_STAGES; stage++ ) {
		shaderStage_t *pStage = &stages[stage];

		if ( !pStage->active ) {
			break;
		}

		Com_Memset( pStage, 0, sizeof( *pStage ) );
	}
}


/*
====================
SetImplicitShaderStages
sets a shader's stages to one of several defaults
====================
*/
static void SetImplicitShaderStages( image_t *image ) {
	// set implicit cull type
	if ( implicitCullType && !shader.cullType ) {
		shader.cullType = implicitCullType;
	}

	// set shader stages
	switch ( shader.lightmapIndex )
	{
		// dynamic colors at vertexes
	case LIGHTMAP_NONE:
		stages[ 0 ].bundle[ 0 ].image[ 0 ] = image;
		stages[ 0 ].active = qtrue;
		stages[ 0 ].rgbGen = CGEN_LIGHTING_DIFFUSE;
		stages[ 0 ].alphaGen = AGEN_SKIP;
		stages[ 0 ].stateBits = implicitStateBits;
		break;

		// gui elements (note state bits are overridden)
	case LIGHTMAP_2D:
		stages[ 0 ].bundle[ 0 ].image[ 0 ] = image;
		stages[ 0 ].active = qtrue;
		stages[ 0 ].rgbGen = CGEN_VERTEX;
		stages[ 0 ].alphaGen = AGEN_SKIP;
		stages[ 0 ].stateBits = GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
		break;

		// fullbright is disabled per atvi request
	case LIGHTMAP_WHITEIMAGE:

		// explicit colors at vertexes
	case LIGHTMAP_BY_VERTEX:
		stages[ 0 ].bundle[ 0 ].image[ 0 ] = image;
		stages[ 0 ].active = qtrue;
		stages[ 0 ].rgbGen = CGEN_EXACT_VERTEX;
		stages[ 0 ].alphaGen = AGEN_SKIP;
		stages[ 0 ].stateBits = implicitStateBits;
		break;

		// use lightmap pass
	default:
		// masked or blended implicit shaders need texture first
		if ( implicitStateBits & ( GLS_ATEST_BITS | GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) {
			stages[ 0 ].bundle[ 0 ].image[ 0 ] = image;
			stages[ 0 ].active = qtrue;
			stages[ 0 ].rgbGen = CGEN_IDENTITY;
			stages[ 0 ].stateBits = implicitStateBits;

			stages[ 1 ].bundle[ 0 ].image[ 0 ] = tr.lightmaps[ shader.lightmapIndex ];
			stages[ 1 ].bundle[ 0 ].isLightmap = qtrue;
			stages[ 1 ].active = qtrue;
			stages[ 1 ].rgbGen = CGEN_IDENTITY;
			stages[ 1 ].stateBits = GLS_DEFAULT | GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO | GLS_DEPTHFUNC_EQUAL;
		}
		// otherwise do standard lightmap + texture
		else
		{
			stages[ 0 ].bundle[ 0 ].image[ 0 ] = tr.lightmaps[ shader.lightmapIndex ];
			stages[ 0 ].bundle[ 0 ].isLightmap = qtrue;
			stages[ 0 ].active = qtrue;
			stages[ 0 ].rgbGen = CGEN_IDENTITY;
			stages[ 0 ].stateBits = GLS_DEFAULT;

			stages[ 1 ].bundle[ 0 ].image[ 0 ] = image;
			stages[ 1 ].active = qtrue;
			stages[ 1 ].rgbGen = CGEN_IDENTITY;
			stages[ 1 ].stateBits = GLS_DEFAULT | GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO;
		}
		break;
	}

	#if 0
	if ( shader.lightmapIndex == LIGHTMAP_NONE ) {
		// dynamic colors at vertexes
		stages[0].bundle[0].image[0] = image;
		stages[0].active = qtrue;
		stages[0].rgbGen = CGEN_LIGHTING_DIFFUSE;
		stages[0].stateBits = GLS_DEFAULT;
	} else if ( shader.lightmapIndex == LIGHTMAP_BY_VERTEX ) {
		// explicit colors at vertexes
		stages[0].bundle[0].image[0] = image;
		stages[0].active = qtrue;
		stages[0].rgbGen = CGEN_EXACT_VERTEX;
		stages[0].alphaGen = AGEN_SKIP;
		stages[0].stateBits = GLS_DEFAULT;
	} else if ( shader.lightmapIndex == LIGHTMAP_2D ) {
		// GUI elements
		stages[0].bundle[0].image[0] = image;
		stages[0].active = qtrue;
		stages[0].rgbGen = CGEN_VERTEX;
		stages[0].alphaGen = AGEN_VERTEX;
		stages[0].stateBits = GLS_DEPTHTEST_DISABLE |
							  GLS_SRCBLEND_SRC_ALPHA |
							  GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
	} else if ( shader.lightmapIndex == LIGHTMAP_WHITEIMAGE ) {
		// fullbright level
		stages[0].bundle[0].image[0] = tr.whiteImage;
		stages[0].active = qtrue;
		stages[0].rgbGen = CGEN_IDENTITY_LIGHTING;
		stages[0].stateBits = GLS_DEFAULT;

		stages[1].bundle[0].image[0] = image;
		stages[1].active = qtrue;
		stages[1].rgbGen = CGEN_IDENTITY;
		stages[1].stateBits |= GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO;
	} else {
		// two pass lightmap
		stages[0].bundle[0].image[0] = tr.lightmaps[shader.lightmapIndex];
		stages[0].bundle[0].isLightmap = qtrue;
		stages[0].active = qtrue;
		stages[0].rgbGen = CGEN_IDENTITY;       // lightmaps are scaled on creation for identitylight
		stages[0].stateBits = GLS_DEFAULT;

		stages[1].bundle[0].image[0] = image;
		stages[1].active = qtrue;
		stages[1].rgbGen = CGEN_IDENTITY;
		stages[1].stateBits |= GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO;
	}
	#endif
}




/*
=================
FixFatLightmapTexCoords

Handle edge cases of altering lightmap texcoords for fat lightmap atlas
=================
*/
static void FixFatLightmapTexCoords(void)
{
	texModInfo_t *tmi;
	int lightmapnum;
	int stage;
	int size;
	int i;

	if ( !r_mergeLightmaps->integer || tr.fatLightmapCols <= 0) {
		return;
	}

	if ( shader.lightmapIndex < 0 ) {
		// no internal lightmap, texcoords were not modified
		return;
	}

	lightmapnum = shader_realLightmapIndex;

	if (tr.worldDeluxeMapping)
		lightmapnum >>= 1;

	lightmapnum %= (tr.fatLightmapCols * tr.fatLightmapRows);

	for ( stage = 0; stage < MAX_SHADER_STAGES; stage++ ) {
		shaderStage_t *pStage = &stages[stage];

		if ( !pStage->active ) {
			break;
		}

		if ( pStage->bundle[0].isLightmap ) {
			// fix tcMod transform for internal lightmaps, it may be used by q3map2 lightstyles
			if ( pStage->bundle[0].tcGen == TCGEN_LIGHTMAP ) {
				for ( i = 0; i < pStage->bundle[0].numTexMods; i++ ) {
					tmi = &pStage->bundle[0].texMods[i];

					if ( tmi->type == TMOD_TRANSFORM ) {
						tmi->translate[0] /= (float)tr.fatLightmapCols;
						tmi->translate[1] /= (float)tr.fatLightmapRows;
					}
				}
			}

			// fix tcGen environment for internal lightmaps to be limited to the sub-image of the atlas
			// this is done last so other tcMods are applied first in the 0.0 to 1.0 space
			if ( pStage->bundle[0].tcGen == TCGEN_ENVIRONMENT_MAPPED ) {
				if ( pStage->bundle[0].numTexMods == TR_MAX_TEXMODS ) {
					ri.Printf( PRINT_DEVELOPER, "WARNING: too many tcmods to fix lightmap texcoords for r_mergeLightmaps in shader '%s'", shader.name );
				} else {
					tmi = &pStage->bundle[0].texMods[pStage->bundle[0].numTexMods];
					pStage->bundle[0].numTexMods++;

					tmi->matrix[0][0] = 1.0f / tr.fatLightmapCols;
					tmi->matrix[0][1] = 0;
					tmi->matrix[1][0] = 0;
					tmi->matrix[1][1] = 1.0f / tr.fatLightmapRows;

					tmi->translate[0] = ( lightmapnum % tr.fatLightmapCols ) / (float)tr.fatLightmapCols;
					tmi->translate[1] = ( lightmapnum / tr.fatLightmapCols ) / (float)tr.fatLightmapRows;

					tmi->type = TMOD_TRANSFORM;
				}
			}
		}
		// add a tcMod transform for external lightmaps to convert back to the original texcoords
		else if ( pStage->bundle[0].tcGen == TCGEN_LIGHTMAP ) {
			if ( pStage->bundle[0].numTexMods == TR_MAX_TEXMODS ) {
				ri.Printf( PRINT_DEVELOPER, "WARNING: too many tcmods to fix lightmap texcoords for r_mergeLightmaps in shader '%s'", shader.name );
			} else {
				size = pStage->bundle[0].numTexMods * sizeof( texModInfo_t );

				if ( size ) {
					memmove( &pStage->bundle[0].texMods[1], &pStage->bundle[0].texMods[0], size );
				}

				tmi = &pStage->bundle[0].texMods[0];
				pStage->bundle[0].numTexMods++;

				tmi->matrix[0][0] = tr.fatLightmapCols;
				tmi->matrix[0][1] = 0;
				tmi->matrix[1][0] = 0;
				tmi->matrix[1][1] = tr.fatLightmapRows;

				tmi->translate[0] = -( lightmapnum % tr.fatLightmapCols );
				tmi->translate[1] = -( lightmapnum / tr.fatLightmapCols );

				tmi->type = TMOD_TRANSFORM;
			}
		}
	}
}

/*
===============
InitShader
===============
*/
static void InitShaderEx( const char *name, int lightmapIndex, int realLightmapIndex ) {
	int i, b;

	// clear the global shader
	Com_Memset( &shader, 0, sizeof( shader ) );
	Com_Memset( &stages, 0, sizeof( stages ) );

	Q_strncpyz( shader.name, name, sizeof( shader.name ) );
	shader.lightmapIndex = lightmapIndex;
	shader_realLightmapIndex = realLightmapIndex;

	shader_picmipFlag = IMGFLAG_PICMIP;
	shader_novlcollapse = qfalse;

	if ( r_ext_compressed_textures->integer == 2 ) {
		// if the shader hasn't specifically asked for it, don't allow compression
		shader_allowCompress = qfalse;
	} else {
		shader_allowCompress = qtrue;
	}

	// default to no implicit mappings
	implicitMap[ 0 ] = '\0';
	implicitStateBits = GLS_DEFAULT;
	implicitCullType = CT_FRONT_SIDED;
	aliasShader[ 0 ] = '\0';

	for ( i = 0 ; i < MAX_SHADER_STAGES ; i++ ) {
		for ( b = 0; b < NUM_TEXTURE_BUNDLES; b++ ) {
			stages[i].bundle[b].texMods = texMods[i][b];
			stages[i].bundle[b].image = imageAnimations[i][b];
			stages[i].bundle[b].image[0] = NULL;
		}

		// default normal/specular
		VectorSet4(stages[i].normalScale, 0.0f, 0.0f, 0.0f, 0.0f);
		if (r_pbr->integer)
		{
			stages[i].specularScale[0] = r_baseGloss->value;
		}
		else
		{
			stages[i].specularScale[0] =
			stages[i].specularScale[1] =
			stages[i].specularScale[2] = r_baseSpecular->value;
			stages[i].specularScale[3] = r_baseGloss->value;
		}
	}
}

static void InitShader( const char *name, int lightmapIndex ) {
	InitShaderEx( name, lightmapIndex, lightmapIndex );
}

/*
===============
ClearShaderStage
===============
*/
static void ClearShaderStage( int num ) {
	int b;

	Com_Memset( &stages[num], 0, sizeof( stages[0] ) );

	for ( b = 0; b < NUM_TEXTURE_BUNDLES; b++ ) {
		stages[num].bundle[b].texMods = texMods[num][b];
		stages[num].bundle[b].image = imageAnimations[num][b];
		stages[num].bundle[b].image[0] = NULL;
	}
}

/*
=========================
FinishShader

Returns a freshly allocated shader with all the needed info
from the current global working shader
=========================
*/
static shader_t *FinishShader( void ) {
	int stage;
	int bundle;
	qboolean		hasLightmapStage;
	qboolean		vertexLightmap;
	qboolean		hasOpaqueStage;

	hasLightmapStage = qfalse;
	vertexLightmap = qfalse;
	hasOpaqueStage = qfalse;

	//
	// set sky stuff appropriate
	//
	if ( shader.isSky ) {
		shader.sort = SS_ENVIRONMENT;
	}

	//
	// set polygon offset
	//
	if ( shader.polygonOffset && !shader.sort ) {
		shader.sort = SS_DECAL;
	}

	//
	// set default sprite scale
	//
	if ( shader.spriteScale == 0 ) {
		shader.spriteScale = 1.0f;
	}

	//
	// set appropriate stage information
	//
	for ( stage = 0; stage < MAX_SHADER_STAGES; ) {
		shaderStage_t *pStage = &stages[stage];
		qboolean isLightmap;

		if ( !pStage->active ) {
			break;
		}

		if ( ( pStage->stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == 0 ) {
			hasOpaqueStage = qtrue;
		}

    // check for a missing texture
		if ( !pStage->bundle[0].image[0] ) {
			ri.Printf( PRINT_WARNING, "Shader %s has a stage with no image\n", shader.name );
			pStage->active = qfalse;
			stage++;
			continue;
		}

		//
		// default texture coordinate generation
		//
		isLightmap = qtrue;
		for ( bundle = 0; bundle < NUM_TEXTURE_BUNDLES; bundle++ ) {
			textureBundle_t *pBundle = &pStage->bundle[bundle];

			if ( !pBundle->image[0] )
				break;

			if ( bundle != TB_COLORMAP && bundle != TB_LIGHTMAP )
				continue;

			if ( pBundle->isLightmap ) {
				if ( pBundle->tcGen == TCGEN_BAD ) {
					pBundle->tcGen = TCGEN_LIGHTMAP;
				}
				hasLightmapStage = qtrue;
			} else {
				if ( pBundle->tcGen == TCGEN_BAD ) {
					pBundle->tcGen = TCGEN_TEXTURE;
				}

				// contains a non-lightmap bundle
				isLightmap = qfalse;
			}
		}


    // not a true lightmap but we want to leave existing 
    // behaviour in place and not print out a warning
    //if (pStage->rgbGen == CGEN_VERTEX) {
    //  vertexLightmap = qtrue;
    //}



		//
		// determine sort order and fog color adjustment
		//
		if ( ( pStage->stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) )
			 && !isLightmap && !hasOpaqueStage ) {
			int blendSrcBits = pStage->stateBits & GLS_SRCBLEND_BITS;
			int blendDstBits = pStage->stateBits & GLS_DSTBLEND_BITS;

			// fog color adjustment only works for blend modes that have a contribution
			// that aproaches 0 as the modulate values aproach 0 --
			// GL_ONE, GL_ONE
			// GL_ZERO, GL_ONE_MINUS_SRC_COLOR
			// GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA

			// modulate, additive
			if ( ( ( blendSrcBits == GLS_SRCBLEND_ONE ) && ( blendDstBits == GLS_DSTBLEND_ONE ) ) ||
				( ( blendSrcBits == GLS_SRCBLEND_ZERO ) && ( blendDstBits == GLS_DSTBLEND_ONE_MINUS_SRC_COLOR ) ) ) {
				pStage->adjustColorsForFog = ACFF_MODULATE_RGB;
			}
			// strict blend
			else if ( ( blendSrcBits == GLS_SRCBLEND_SRC_ALPHA ) && ( blendDstBits == GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA ) )
			{
				pStage->adjustColorsForFog = ACFF_MODULATE_ALPHA;
			}
			// premultiplied alpha
			else if ( ( blendSrcBits == GLS_SRCBLEND_ONE ) && ( blendDstBits == GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA ) )
			{
				pStage->adjustColorsForFog = ACFF_MODULATE_RGBA;
			} else {
				// we can't adjust this one correctly, so it won't be exactly correct in fog
			}

			// don't screw with sort order if this is a portal or environment
			if ( !shader.sort ) {
				// see through item, like a grill or grate
				if ( pStage->stateBits & GLS_DEPTHMASK_TRUE ) {
					shader.sort = SS_SEE_THROUGH;
				} else {
					shader.sort = SS_BLEND0;
				}
			}
		}
		
		stage++;
	}

	// there are times when you will need to manually apply a sort to
	// opaque alpha tested shaders that have later blend passes
	if ( !shader.sort ) {
		shader.sort = SS_OPAQUE;
	}

	//
	if ( hasOpaqueStage && shader.sort <= SS_SEE_THROUGH ) {
		shader.fogPass = FP_EQUAL;
	} else if ( shader.surfaceParms & SURF_FOG ) {
		shader.fogPass = FP_LE;
	}

	//
	// if we are in r_vertexLight mode, never use a lightmap texture
	//
	if ( stage > 1 && r_vertexLight->integer && hasLightmapStage && !shader_novlcollapse ) {
		VertexLightingCollapse();
		hasLightmapStage = qfalse;
	}

	FixFatLightmapTexCoords();

	//
	// look for multitexture potential
	//
	stage = CollapseStagesToGLSL();

	if ( shader.lightmapIndex >= 0 && !hasLightmapStage ) {
		if (vertexLightmap) {
			ri.Printf( PRINT_DEVELOPER, "WARNING: shader '%s' has VERTEX forced lightmap!\n", shader.name );
		} else {
			ri.Printf( PRINT_DEVELOPER, "WARNING: shader '%s' has lightmap but no lightmap stage!\n", shader.name );
			// Don't set this, it will just add duplicate shaders to the hash
  			//shader.lightmapIndex = LIGHTMAP_NONE;
		}
	}


	//
	// compute number of passes
	//
	shader.numUnfoggedPasses = stage;

	// fogonly shaders don't have any normal passes
	if (stage == 0 && !shader.isSky)
		shader.sort = SS_FOG;

	// determine which stage iterator function is appropriate
	ComputeStageIteratorFunc();

	// determine which vertex attributes this shader needs
	ComputeVertexAttribs();

	return GeneratePermanentShader();
}

//========================================================================================

/*
====================
FindShaderInShaderText

Scans the combined text description of all the shader files for
the given shader name.

return NULL if not found

If found, it will return a valid shader
=====================
*/
static char *FindShaderInShaderText( const char *shadername ) {

	char *token, *p;

	int i, hash;

	hash = generateHashValue(shadername, MAX_SHADERTEXT_HASH);

	if(shaderTextHashTable[hash])
	{
		for (i = 0; shaderTextHashTable[hash][i]; i++)
		{
			p = shaderTextHashTable[hash][i];
			token = COM_ParseExt(&p, qtrue);
		
			if(!Q_stricmp(token, shadername))
				return p;
		}
	}

	p = s_shaderText;

	if ( !p ) {
		return NULL;
	}

	// look for label
	while ( 1 ) {
		token = COM_ParseExt( &p, qtrue );
		if ( token[0] == 0 ) {
			break;
		}

		if ( !Q_stricmp( token, shadername ) ) {
			return p;
		}
		else {
			// skip the definition
			SkipBracedSection( &p, 0 );
		}
	}

	return NULL;
}


/*
==================
R_FindShaderByName

Will always return a valid shader, but it might be the
default shader if the real one can't be found.
==================
*/
shader_t *R_FindShaderByName( const char *name ) {
	char		strippedName[MAX_QPATH];
	int			hash;
	shader_t	*sh;

	if ( (name==NULL) || (name[0] == 0) ) {
		return tr.defaultShader;
	}

	COM_StripExtension(name, strippedName, sizeof(strippedName));

	hash = generateHashValue(strippedName, FILE_HASH_SIZE);

	//
	// see if the shader is already loaded
	//
	for (sh=hashTable[hash]; sh; sh=sh->next) {
		// NOTE: if there was no shader or image available with the name strippedName
		// then a default shader is created with lightmapIndex == LIGHTMAP_NONE, so we
		// have to check all default shaders otherwise for every call to R_FindShader
		// with that same strippedName a new default shader is created.
		if (Q_stricmp(sh->name, strippedName) == 0) {
			// match found
			return sh;
		}
	}

	return tr.defaultShader;
}

/*
===============
R_FindLightmap - ydnar
given a (potentially erroneous) lightmap index, attempts to load
an external lightmap image and/or sets the index to a valid number
===============
*/

#define EXTERNAL_LIGHTMAP   "lm_%04d.tga"    // THIS MUST BE IN SYNC WITH Q3MAP2

void R_FindLightmap( int *lightmapIndex ) {
	image_t     *image;
	char fileName[ MAX_QPATH ];

	// don't fool with bogus lightmap indexes
	if ( *lightmapIndex < 0 ) {
		return;
	}

	// does this lightmap already exist?
	if ( *lightmapIndex < tr.numLightmaps && tr.lightmaps[ *lightmapIndex ] != NULL ) {
		return;
	}

	// bail if no world dir
	if ( tr.worldDir == NULL ) {
		*lightmapIndex = LIGHTMAP_BY_VERTEX;
		return;
	}

	// bail if no free slot
	if ( *lightmapIndex >= tr.maxLightmaps ) {
		*lightmapIndex = LIGHTMAP_BY_VERTEX;
		return;
	}

	R_IssuePendingRenderCommands();

	// attempt to load an external lightmap
	Com_sprintf( fileName, sizeof (fileName), "%s/" EXTERNAL_LIGHTMAP, tr.worldDir, *lightmapIndex );
	image = R_FindImageFile( fileName, IMGTYPE_COLORALPHA, IMGFLAG_LIGHTMAP | IMGFLAG_NOLIGHTSCALE | IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE );
	if ( image == NULL ) {
		*lightmapIndex = LIGHTMAP_BY_VERTEX;
		return;
	}

	// add it to the lightmap list
	if ( *lightmapIndex >= tr.numLightmaps ) {
		tr.numLightmaps = *lightmapIndex + 1;
	}
	tr.lightmaps[ *lightmapIndex ] = image;
}



/*
===============
R_FindShader

Will always return a valid shader, but it might be the
default shader if the real one can't be found.

In the interest of not requiring an explicit shader text entry to
be defined for every single image used in the game, three default
shader behaviors can be auto-created for any image:

If lightmapIndex == LIGHTMAP_NONE, then the image will have
dynamic diffuse lighting applied to it, as appropriate for most
entity skin surfaces.

If lightmapIndex == LIGHTMAP_2D, then the image will be used
for 2D rendering unless an explicit shader is found

If lightmapIndex == LIGHTMAP_BY_VERTEX, then the image will use
the vertex rgba modulate values, as appropriate for misc_model
pre-lit surfaces.

Other lightmapIndex values will have a lightmap stage created
and src*dest blending applied with the texture, as appropriate for
most world construction surfaces.

===============
*/
shader_t *R_FindShader( const char *name, int lightmapIndex, imgFlags_t rawImageFlags ) {
	return R_FindShaderEx( name, lightmapIndex, rawImageFlags, lightmapIndex );
}

shader_t *R_FindShaderEx( const char *name, int lightmapIndex, imgFlags_t rawImageFlags, int realLightmapIndex ) {
	char		strippedName[MAX_QPATH];
	char		fileName[MAX_QPATH];
	int			hash;
	char		*shaderText;
	image_t		*image;
	shader_t	*sh;

	if ( refHeadless ) {
		return tr.defaultShader;
	}

	if ( name[0] == 0 ) {
		return tr.defaultShader;
	}

	// ydnar: validate lightmap index
	R_FindLightmap( &lightmapIndex );

	COM_StripExtension(name, strippedName, sizeof(strippedName));

	hash = generateHashValue(strippedName, FILE_HASH_SIZE);

	//
	// see if the shader is already loaded
	//
	for (sh = hashTable[hash]; sh; sh = sh->next) {
		// NOTE: if there was no shader or image available with the name strippedName
		// then a default shader is created with lightmapIndex == LIGHTMAP_NONE, so we
		// have to check all default shaders otherwise for every call to R_FindShader
		// with that same strippedName a new default shader is created.
		if ( (sh->lightmapIndex == lightmapIndex || sh->defaultShader) &&
		     !Q_stricmp(sh->name, strippedName)) {
			// match found
			return sh;
		}
	}

	InitShaderEx( strippedName, lightmapIndex, realLightmapIndex );

	//
	// attempt to define shader from an explicit parameter file
	//
	shaderText = FindShaderInShaderText( strippedName );
	if ( shaderText ) {
		// enable this when building a pak file to get a global list
		// of all explicit shaders
		if ( r_printShaders->integer ) {
			ri.Printf( PRINT_ALL, "*SHADER* %s\n", name );
		}

		if ( !ParseShader( &shaderText ) ) {
			// had errors, so use default shader
			shader.defaultShader = qtrue;
			sh = FinishShader();
			return sh;
		}

		if ( aliasShader[ 0 ] != '\0' ) {
			char storedAlias[ MAX_QPATH ];

			Q_strncpyz( storedAlias, aliasShader, sizeof ( storedAlias ) );

			return R_FindShader( storedAlias, lightmapIndex, rawImageFlags );
		}

		// allow implicit mappings
		if ( implicitMap[ 0 ] == '\0' ) {
			sh = FinishShader();
			return sh;
		}
	}


	// allow implicit mapping ('-' = use shader name)
	if ( implicitMap[ 0 ] == '\0' || implicitMap[ 0 ] == '-' ) {
		Q_strncpyz( fileName, name, sizeof( fileName ) );
	} else {
		Q_strncpyz( fileName, implicitMap, sizeof( fileName ) );
	}

	// implicit shaders were breaking nopicmip/nomipmaps
	shader.noMipMaps = !( rawImageFlags & IMGFLAG_MIPMAP );
	shader.noPicMip = !( rawImageFlags & IMGFLAG_PICMIP );

	//
	// if not defined in the in-memory shader descriptions,
	// look for a single supported image file
	//
	{
		imgFlags_t flags;

		flags = IMGFLAG_NONE;

		// allow implicit shaders to use picmip2
		if ( rawImageFlags & IMGFLAG_PICMIP ) {
			flags |= shader_picmipFlag;
			rawImageFlags &= ~IMGFLAG_PICMIP;
		}

		flags |= rawImageFlags;

		if (rawImageFlags & IMGFLAG_MIPMAP)
		{
			if (r_genNormalMaps->integer)
				flags |= IMGFLAG_GENNORMALMAP;
		}

		if (!shader_allowCompress)
			flags |= IMGFLAG_NO_COMPRESSION;

		image = R_FindImageFile( fileName, IMGTYPE_COLORALPHA, flags );
		if ( !image ) {
			ri.Printf( PRINT_DEVELOPER, "Couldn't find image file for shader %s\n", name );
			shader.defaultShader = qtrue;
			return FinishShader();
		}
	}

	// set default stages
	SetImplicitShaderStages( image );

	return FinishShader();
}


qhandle_t RE_RegisterShaderFromImage(const char *name, int lightmapIndex, image_t *image, qboolean mipRawImage) {
	int			hash;
	shader_t	*sh;

	hash = generateHashValue(name, FILE_HASH_SIZE);

	// probably not necessary since this function
	// only gets called from tr_font.c with lightmapIndex == LIGHTMAP_2D
	// but better safe than sorry.
	if ( lightmapIndex >= tr.numLightmaps ) {
		lightmapIndex = LIGHTMAP_WHITEIMAGE;
	}

	//
	// see if the shader is already loaded
	//
	for (sh=hashTable[hash]; sh; sh=sh->next) {
		// NOTE: if there was no shader or image available with the name strippedName
		// then a default shader is created with lightmapIndex == LIGHTMAP_NONE, so we
		// have to check all default shaders otherwise for every call to R_FindShader
		// with that same strippedName a new default shader is created.
		if ( (sh->lightmapIndex == lightmapIndex || sh->defaultShader) &&
			// index by name
			!Q_stricmp(sh->name, name)) {
			// match found
			return sh->index;
		}
	}

	InitShader( name, lightmapIndex );

	// set default stages
	SetImplicitShaderStages( image );

	sh = FinishShader();
  return sh->index; 
}


/* 
====================
RE_RegisterShaderEx

This is the exported shader entry point for the rest of the system
It will always return an index that will be valid.
====================
*/
qhandle_t RE_RegisterShaderEx( const char *name, int lightmapIndex, qboolean mipRawImage ) {
	shader_t	*sh;

	if ( strlen( name ) >= MAX_QPATH ) {
		ri.Printf( PRINT_ALL, "Shader name exceeds MAX_QPATH\n" );
		return 0;
	}

	sh = R_FindShader( name, lightmapIndex, mipRawImage ? ( IMGFLAG_MIPMAP | IMGFLAG_PICMIP ) : IMGFLAG_CLAMPTOEDGE );

	// we want to return 0 if the shader failed to
	// load for some reason, but R_FindShader should
	// still keep a name allocated for it, so if
	// something calls RE_RegisterShader again with
	// the same name, we don't try looking for it again
	if ( sh->defaultShader ) {
		return 0;
	}

	return sh->index;
}


/* 
====================
RE_RegisterShader

This is the exported shader entry point for the rest of the system
It will always return an index that will be valid.

This should really only be used for explicit shaders, because there is no
way to ask for different implicit lighting modes (vertex, lightmap, etc)
====================
*/
qhandle_t RE_RegisterShader( const char *name ) {
	return RE_RegisterShaderEx( name, LIGHTMAP_2D, qtrue );
}


/*
====================
RE_RegisterShaderNoMip

For menu graphics that should never be picmiped and not have mipmaps
====================
*/
qhandle_t RE_RegisterShaderNoMip( const char *name ) {
	return RE_RegisterShaderEx( name, LIGHTMAP_2D, qfalse );
}

/*
====================
RE_RegisterShaderNoPicMip

For menu graphics that should never be picmiped but will have mipmaps
====================
*/
qhandle_t RE_RegisterShaderNoPicMip( const char *name ) {
	shader_t	*sh;

	if ( strlen( name ) >= MAX_QPATH ) {
		ri.Printf( PRINT_ALL, "Shader name exceeds MAX_QPATH\n" );
		return 0;
	}

	sh = R_FindShader( name, LIGHTMAP_2D, IMGFLAG_MIPMAP | IMGFLAG_CLAMPTOEDGE );

	// we want to return 0 if the shader failed to
	// load for some reason, but R_FindShader should
	// still keep a name allocated for it, so if
	// something calls RE_RegisterShader again with
	// the same name, we don't try looking for it again
	if ( sh->defaultShader ) {
		return 0;
	}

	return sh->index;
}

/*
====================
R_GetShaderByHandle

When a handle is passed in by another module, this range checks
it and returns a valid (possibly default) shader_t to be used internally.
====================
*/
shader_t *R_GetShaderByHandle( qhandle_t hShader ) {
	if ( hShader < 0 ) {
	  ri.Printf( PRINT_WARNING, "R_GetShaderByHandle: out of range hShader '%d'\n", hShader );
		return tr.defaultShader;
	}
	if ( hShader >= tr.numShaders ) {
		ri.Printf( PRINT_WARNING, "R_GetShaderByHandle: out of range hShader '%d'\n", hShader );
		return tr.defaultShader;
	}
	return tr.shaders[hShader];
}

/*
===============
R_ShaderList_f

Dump information on all valid shaders to the console
A second parameter will cause it to print in sorted order
===============
*/
void	R_ShaderList_f (void) {
	int			i;
	int			count;
	shader_t	*shader;

	ri.Printf (PRINT_ALL, "-----------------------\n");

	count = 0;
	for ( i = 0 ; i < tr.numShaders ; i++ ) {
		if ( ri.Cmd_Argc() > 1 ) {
			shader = tr.sortedShaders[i];
		} else {
			shader = tr.shaders[i];
		}

		ri.Printf( PRINT_ALL, "%i ", shader->numUnfoggedPasses );

		if (shader->lightmapIndex >= 0 ) {
			ri.Printf (PRINT_ALL, "L ");
		} else {
			ri.Printf (PRINT_ALL, "  ");
		}
		// ZTM: TODO? Report MT if any stage uses multitexture?
		if ( shader->stages[0] && shader->stages[0]->multitextureEnv == GL_ADD ) {
			ri.Printf( PRINT_ALL, "MT(a) " );
		} else if ( shader->stages[0] && shader->stages[0]->multitextureEnv == GL_MODULATE ) {
			ri.Printf( PRINT_ALL, "MT(m) " );
		} else if ( shader->stages[0] && shader->stages[0]->multitextureEnv == GL_DECAL ) {
			ri.Printf( PRINT_ALL, "MT(d) " );
		} else {
			ri.Printf( PRINT_ALL, "      " );
		}
		if ( shader->explicitlyDefined ) {
			ri.Printf( PRINT_ALL, "E " );
		} else {
			ri.Printf( PRINT_ALL, "  " );
		}

		if ( shader->optimalStageIteratorFunc == RB_StageIteratorGeneric ) {
			ri.Printf( PRINT_ALL, "gen " );
		} else if ( shader->optimalStageIteratorFunc == RB_StageIteratorSky ) {
			ri.Printf( PRINT_ALL, "sky " );
		} else {
			ri.Printf( PRINT_ALL, "    " );
		}

		if ( shader->defaultShader ) {
			ri.Printf (PRINT_ALL,  ": %s (DEFAULTED)\n", shader->name);
		} else {
			ri.Printf (PRINT_ALL,  ": %s\n", shader->name);
		}
		count++;
	}
	ri.Printf (PRINT_ALL, "%i total shaders\n", count);
	ri.Printf (PRINT_ALL, "------------------\n");
}

/*
====================
ScanAndLoadShaderFiles

Finds and loads all .shader files, combining them into
a single large text block that can be scanned for shader names
=====================
*/
#define	MAX_SHADER_FILES	4096
static void ScanAndLoadShaderFiles( void )
{
	char **shaderFiles;
	char *buffers[MAX_SHADER_FILES] = {NULL};
	char *p;
	int numShaderFiles;
	int i;
	char *oldp, *token, *hashMem, *textEnd;
	int shaderTextHashTableSizes[MAX_SHADERTEXT_HASH], hash, size;
	char shaderName[MAX_QPATH];
	int shaderLine;

	long sum = 0, summand;
	// scan for shader files
	shaderFiles = ri.FS_ListFiles( r_shadersDirectory->string, ".shader", &numShaderFiles );

	if ( !shaderFiles || !numShaderFiles )
	{
		ri.Printf( PRINT_WARNING, "WARNING: no shader files found\n" );
		return;
	}

	if ( numShaderFiles > MAX_SHADER_FILES ) {
		numShaderFiles = MAX_SHADER_FILES;
	}

	// load and parse shader files
	for ( i = 0; i < numShaderFiles; i++ )
	{
		char filename[MAX_QPATH];

		// look for a .mtr file first
		{
			char *ext;
			Com_sprintf( filename, sizeof( filename ), "%s/%s", r_shadersDirectory->string, shaderFiles[i] );
			if ( (ext = strrchr(filename, '.')) )
			{
				strcpy(ext, ".mtr");
			}

			if ( ri.FS_ReadFile( filename, NULL ) <= 0 )
			{
				Com_sprintf( filename, sizeof( filename ), "%s/%s", r_shadersDirectory->string, shaderFiles[i] );
			}
		}
		
		ri.Printf( PRINT_DEVELOPER, "...loading '%s'\n", filename );
		summand = ri.FS_ReadFile( filename, (void **)&buffers[i] );
		
		if ( !buffers[i] )
			ri.Error( ERR_DROP, "Couldn't load %s", filename );
		
		// Do a simple check on the shader structure in that file to make sure one bad shader file cannot fuck up all other shaders.
		p = buffers[i];
		COM_BeginParseSession(filename);
		while(1)
		{
			token = COM_ParseExt(&p, qtrue);
			
			if(!*token)
				break;

			Q_strncpyz(shaderName, token, sizeof(shaderName));
			shaderLine = COM_GetCurrentParseLine();

			token = COM_ParseExt(&p, qtrue);
			if(token[0] != '{' || token[1] != '\0')
			{
				ri.Printf(PRINT_WARNING, "WARNING: Ignoring shader file %s. Shader \"%s\" on line %d missing opening brace",
							filename, shaderName, shaderLine);
				if (token[0])
				{
					ri.Printf(PRINT_WARNING, " (found \"%s\" on line %d)", token, COM_GetCurrentParseLine());
				}
				ri.Printf(PRINT_WARNING, ".\n");
				ri.FS_FreeFile(buffers[i]);
				buffers[i] = NULL;
				break;
			}

			if(!SkipBracedSection(&p, 1))
			{
				ri.Printf(PRINT_WARNING, "WARNING: Ignoring shader file %s. Shader \"%s\" on line %d missing closing brace.\n",
							filename, shaderName, shaderLine);
				ri.FS_FreeFile(buffers[i]);
				buffers[i] = NULL;
				break;
			}
		}
			
		
		if (buffers[i])
			sum += summand;		
	}

	// build single large buffer
	s_shaderText = ri.Hunk_Alloc( sum + numShaderFiles*2, h_low );
	s_shaderText[ 0 ] = '\0';
	textEnd = s_shaderText;
 
	// free in reverse order, so the temp files are all dumped
	for ( i = numShaderFiles - 1; i >= 0 ; i-- )
	{
		if ( !buffers[i] )
			continue;

		strcat( textEnd, buffers[i] );
		strcat( textEnd, "\n" );
		textEnd += strlen( textEnd );
		ri.FS_FreeFile( buffers[i] );
	}

	COM_Compress( s_shaderText );

	// free up memory
	ri.FS_FreeFileList( shaderFiles );

	Com_Memset(shaderTextHashTableSizes, 0, sizeof(shaderTextHashTableSizes));
	size = 0;

	p = s_shaderText;
	// look for shader names
	while ( 1 ) {
		token = COM_ParseExt( &p, qtrue );
		if ( token[0] == 0 ) {
			break;
		}

		hash = generateHashValue(token, MAX_SHADERTEXT_HASH);
		shaderTextHashTableSizes[hash]++;
		size++;
		SkipBracedSection(&p, 0);
	}

	size += MAX_SHADERTEXT_HASH;

	hashMem = ri.Hunk_Alloc( size * sizeof(char *), h_low );

	for (i = 0; i < MAX_SHADERTEXT_HASH; i++) {
		shaderTextHashTable[i] = (char **) hashMem;
		hashMem = ((char *) hashMem) + ((shaderTextHashTableSizes[i] + 1) * sizeof(char *));
	}

	Com_Memset(shaderTextHashTableSizes, 0, sizeof(shaderTextHashTableSizes));

	p = s_shaderText;
	// look for shader names
	while ( 1 ) {
		oldp = p;
		token = COM_ParseExt( &p, qtrue );
		if ( token[0] == 0 ) {
			break;
		}

		hash = generateHashValue(token, MAX_SHADERTEXT_HASH);
		shaderTextHashTable[hash][shaderTextHashTableSizes[hash]++] = oldp;

		SkipBracedSection(&p, 0);
	}

	return;

}


/*
====================
CreateInternalShaders
====================
*/
static void CreateInternalShaders( void ) {
	tr.numShaders = 0;

	// init the default shader
	InitShader( "<default>", LIGHTMAP_NONE );
	stages[0].bundle[0].image[0] = tr.defaultImage;
	stages[0].active = qtrue;
	stages[0].stateBits = GLS_DEFAULT;
	tr.defaultShader = FinishShader();

	// used for skins for disable surfaces
	Q_strncpyz( shader.name, "nodraw", sizeof( shader.name ) );
	tr.nodrawShader = FinishShader();

	// shadow shader is just a marker
	Q_strncpyz( shader.name, "<stencil shadow>", sizeof( shader.name ) );
	shader.sort = SS_STENCIL_SHADOW;
	tr.shadowShader = FinishShader();
}

static void CreateExternalShaders( void ) {
	tr.projectionShadowShader = R_FindShader( "projectionShadow", LIGHTMAP_NONE, MIP_RAW_IMAGE );
	tr.flareShader = R_FindShader( "flareShader", LIGHTMAP_NONE, MIP_RAW_IMAGE );
	tr.sunShader = NULL;

	tr.sunFlareShader = R_FindShader( "gfx/2d/sunflare", LIGHTMAP_NONE, MIP_RAW_IMAGE );

	// HACK: if sunflare is missing, make one using the flare image or dlight image
	if (tr.sunFlareShader->defaultShader)
	{
		image_t *image;

		if (!tr.flareShader->defaultShader && tr.flareShader->stages[0] && tr.flareShader->stages[0]->bundle[0].image[0])
			image = tr.flareShader->stages[0]->bundle[0].image[0];
		else
			image = tr.dlightImage;

		InitShader( "gfx/2d/sunflare", LIGHTMAP_NONE );
		stages[0].bundle[0].image[0] = image;
		stages[0].active = qtrue;
		stages[0].stateBits = GLS_DEFAULT;
		tr.sunFlareShader = FinishShader();
	}

}

/*
==================
R_InitShaders
==================
*/
void R_InitShaders( void ) {
	ri.Printf(PRINT_DEVELOPER, "Initializing Shaders\n");

	Com_Memset(hashTable, 0, sizeof(hashTable));

	CreateInternalShaders();

	ScanAndLoadShaderFiles();

	CreateExternalShaders();
}

/*
==================
R_InitExternalShaders
==================
*/
void R_InitExternalShaders( void ) {
	if ( !tr.sunShaderName[0] ) {
		Q_strncpyz( tr.sunShaderName, "sun", sizeof ( tr.sunShaderName ) );
	}

	tr.sunShader = R_FindShader( tr.sunShaderName, LIGHTMAP_NONE, MIP_RAW_IMAGE );
}
