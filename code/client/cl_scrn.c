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
// cl_scrn.c -- master for refresh, status bar, console, chat, notify, etc

#include "client.h"

qboolean	scr_initialized;		// ready to draw
#ifdef USE_FLEXIBLE_DISPLAY
int			scr_placement;
float		scr_nativeScale = 1.0f;
#endif

cvar_t		*cl_timegraph;
cvar_t		*cl_debuggraph;
cvar_t		*cl_graphheight;
cvar_t		*cl_graphscale;
cvar_t		*cl_graphshift;

#ifdef USE_FLEXIBLE_DISPLAY
/*
================
SCR_SetScreenPlacement
================
*/
void SCR_SetScreenPlacement( int placement ) {
	scr_placement = placement;
}

/*
================
SCR_SetNativeScale
================
*/
void SCR_SetNativeScale( float scale ) {
	scr_nativeScale = scale;
}

/*
================
SCR_AdjustFrom640

Adjusted for resolution and screen aspect ratio
================
*/
void SCR_AdjustFrom640( float *x, float *y, float *w, float *h ) {
	int placement = scr_placement;

	if ( ( placement & SCR_HOR_MASK ) == SCR_HOR_STRETCH ) {
		// scale for screen sizes (not aspect correct in wide screen)
		if ( w != NULL ) {
			*w *= cls.screenXScaleStretch;
		}
		if ( x != NULL ) {
			*x *= cls.screenXScaleStretch;
		}
	} else {
		// scale for screen sizes
		if ( placement & SCR_HOR_NATIVE ) {
			if ( w != NULL ) {
				*w *= scr_nativeScale;
			}

			if ( x != NULL ) {
				*x *= scr_nativeScale;
			}
		} else {
			if ( w != NULL ) {
				*w *= cls.screenXScale;
			}

			if ( x != NULL ) {
				*x *= cls.screenXScale;
			}
		}

		if ( x != NULL ) {
			if ( ( placement & SCR_HOR_MASK ) == SCR_HOR_CENTER ) {
				*x += cls.screenXBias;
			} else if ( ( placement & SCR_HOR_MASK ) == SCR_HOR_RIGHT ) {
				*x += cls.screenXBias*2;
			}
		}
	}

	if ( ( placement & SCR_VERT_MASK ) == SCR_VERT_STRETCH ) {
		if ( h != NULL ) {
			*h *= cls.screenYScaleStretch;
		}
		if ( y != NULL ) {
			*y *= cls.screenYScaleStretch;
		}
	} else {
		if ( placement & SCR_VERT_NATIVE ) {
			if ( h != NULL ) {
				*h *= scr_nativeScale;
			}

			if ( y != NULL ) {
				*y *= scr_nativeScale;
			}
		} else {
			if ( h != NULL ) {
				*h *= cls.screenYScale;
			}

			if ( y != NULL ) {
				*y *= cls.screenYScale;
			}
		}

		if ( y != NULL ) {
			if ( ( placement & SCR_VERT_MASK ) == SCR_VERT_CENTER ) {
				*y += cls.screenYBias;
			} else if ( ( placement & SCR_VERT_MASK ) == SCR_VERT_BOTTOM ) {
				*y += cls.screenYBias*2;
			}
		}
	}
}

/*
================
SCR_AdjustTo640

Adjusted for resolution and screen aspect ratio
================
*/
void SCR_AdjustTo640( float *x, float *y, float *w, float *h ) {
	int placement = scr_placement;

	if ( ( placement & SCR_HOR_MASK ) == SCR_HOR_STRETCH ) {
		// scale for screen sizes (not aspect correct in wide screen)
		if ( w != NULL ) {
			*w /= cls.screenXScaleStretch;
		}
		if ( x != NULL ) {
			*x /= cls.screenXScaleStretch;
		}
	} else {
		if ( x != NULL ) {
			if ( ( placement & SCR_HOR_MASK ) == SCR_HOR_CENTER ) {
				*x -= cls.screenXBias;
			} else if ( ( placement & SCR_HOR_MASK ) == SCR_HOR_RIGHT ) {
				*x -= cls.screenXBias*2;
			}
		}

		// scale for screen sizes
		if ( placement & SCR_HOR_NATIVE ) {
			if ( w != NULL ) {
				*w /= scr_nativeScale;
			}

			if ( x != NULL ) {
				*x /= scr_nativeScale;
			}
		} else {
			if ( w != NULL ) {
				*w /= cls.screenXScale;
			}

			if ( x != NULL ) {
				*x /= cls.screenXScale;
			}
		}
	}

	if ( ( placement & SCR_VERT_MASK ) == SCR_VERT_STRETCH ) {
		if ( h != NULL ) {
			*h /= cls.screenYScaleStretch;
		}
		if ( y != NULL ) {
			*y /= cls.screenYScaleStretch;
		}
	} else {
		if ( y != NULL ) {
			if ( ( placement & SCR_VERT_MASK ) == SCR_VERT_CENTER ) {
				*y -= cls.screenYBias;
			} else if ( ( placement & SCR_VERT_MASK ) == SCR_VERT_BOTTOM ) {
				*y -= cls.screenYBias*2;
			}
		}

		if ( placement & SCR_HOR_NATIVE ) {
			if ( h != NULL ) {
				*h /= scr_nativeScale;
			}

			if ( y != NULL ) {
				*y /= scr_nativeScale;
			}
		} else {
			if ( h != NULL ) {
				*h /= cls.screenYScale;
			}

			if ( y != NULL ) {
				*y /= cls.screenYScale;
			}
		}
	}
}
#endif



/*
===============================================================================

DEBUG GRAPH

===============================================================================
*/

static	int			current;
static	float		values[1024];

/*
==============
SCR_DebugGraph
==============
*/
void SCR_DebugGraph (float value)
{
	values[current] = value;
	current = (current + 1) % ARRAY_LEN(values);
}

/*
==============
SCR_DrawDebugGraph
==============
*/
void SCR_DrawDebugGraph (void)
{
	int		a, x, y, w, i, h;
	float	v;

	//
	// draw the graph
	//
	w = cls.glconfig.vidWidth;
	x = 0;
	y = cls.glconfig.vidHeight;
	re.SetColor( g_color_table[0] );
	re.DrawStretchPic(x, y - cl_graphheight->integer, 
		w, cl_graphheight->integer, 0, 0, 0, 0, cls.whiteShader );
	re.SetColor( NULL );

	for (a=0 ; a<w ; a++)
	{
		i = (ARRAY_LEN(values)+current-1-(a % ARRAY_LEN(values))) % ARRAY_LEN(values);
		v = values[i];
		v = v * cl_graphscale->integer + cl_graphshift->integer;
		
		if (v < 0)
			v += cl_graphheight->integer * (1+(int)(-v / cl_graphheight->integer));
		h = (int)v % cl_graphheight->integer;
		re.DrawStretchPic( x+w-1-a, y - h, 1, h, 0, 0, 0, 0, cls.whiteShader );
	}
}

//=============================================================================

/*
==================
SCR_Init
==================
*/
void SCR_Init( void ) {
	cl_timegraph = Cvar_Get ("timegraph", "0", CVAR_CHEAT);
	cl_debuggraph = Cvar_Get ("debuggraph", "0", CVAR_CHEAT);
	cl_graphheight = Cvar_Get ("graphheight", "32", CVAR_CHEAT);
	cl_graphscale = Cvar_Get ("graphscale", "1", CVAR_CHEAT);
	cl_graphshift = Cvar_Get ("graphshift", "0", CVAR_CHEAT);

	scr_initialized = qtrue;
}


//=======================================================

/*
==================
SCR_DrawScreenField

This will be called twice if rendering in stereo mode
==================
*/
void SCR_DrawScreenField( stereoFrame_t stereoFrame ) {
	re.BeginFrame( stereoFrame );

#ifdef USE_FLEXIBLE_DISPLAY
	// wide aspect ratio screens need to have the sides cleared
	// unless they are displaying game renderings
	if ( cl_flexibleDisplay->integer && ( clc.state != CA_ACTIVE || cl_viewmode->integer == 1 ) ) {
		if ( cls.screenXBias || cls.screenYBias ) {
			int left = cls.screenXBias + 0.5f;
			int right = cls.glconfig.vidWidth - left;
			int top = cls.screenYBias + 0.5f;
			int bottom = cls.glconfig.vidHeight - top;

			re.SetColor( g_color_table[0] );

			if ( cls.screenXBias ) {
				// clear left
				re.DrawStretchPic( 0, 0, left, cls.glconfig.vidHeight, 0, 0, 0, 0, cls.whiteShader );

				// clear right
				re.DrawStretchPic( right, 0, cls.glconfig.vidWidth - right, cls.glconfig.vidHeight, 0, 0, 0, 0, cls.whiteShader );
			}

			if ( cls.screenYBias ) {
				// clear top
				re.DrawStretchPic( left, 0, right - left, top, 0, 0, 0, 0, cls.whiteShader );

				// clear bottom
				re.DrawStretchPic( left, bottom, right - left, cls.glconfig.vidHeight - bottom, 0, 0, 0, 0, cls.whiteShader );
			}

			re.SetColor( NULL );
		}
	}
#endif

	// draw UI and/or game scene
	CL_CGameRendering(stereoFrame);

	// debug graph can be drawn on top of anything
	if ( cl_debuggraph->integer || cl_timegraph->integer ) {
		SCR_DrawDebugGraph ();
	}
}

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.
==================
*/
void SCR_UpdateScreen( void ) {
	static int	recursive;

	if ( !scr_initialized ) {
		return;				// not initialized yet
	}

	if ( ++recursive > 2 ) {
		Com_Error( ERR_FATAL, "SCR_UpdateScreen: recursively called" );
	}
	recursive = 1;

	// If there is no VM, there are also no rendering commands issued. Stop the renderer in
	// that case.
	if( cgvm || com_dedicated->integer )
	{
		// XXX
		int in_anaglyphMode = Cvar_VariableIntegerValue("r_anaglyphMode");
		// if running in stereo, we need to draw the frame twice
		if ( cls.glconfig.stereoEnabled || in_anaglyphMode) {
			SCR_DrawScreenField( STEREO_LEFT );
			SCR_DrawScreenField( STEREO_RIGHT );
		} else {
			SCR_DrawScreenField( STEREO_CENTER );
		}

		if ( com_speeds->integer ) {
			re.EndFrame( &time_frontend, &time_backend );
		} else {
			re.EndFrame( NULL, NULL );
		}
	}
	
	recursive = 0;
}

