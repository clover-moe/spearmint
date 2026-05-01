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

#ifdef USE_LOCAL_HEADERS
#	include "SDL.h"
#else
#	include <SDL.h>
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "../client/client.h"
#include "../sys/sys_local.h"

#if !SDL_VERSION_ATLEAST(2, 0, 17)
#define KMOD_SCROLL KMOD_RESERVED
#endif

static cvar_t *in_keyboardDebug     = NULL;

static SDL_Joystick *stick[CL_MAX_SPLITVIEW] = {NULL};

static qboolean mouseAvailable = qfalse;
static qboolean mouseActive = qfalse;

static cvar_t *in_mouse             = NULL;
static cvar_t *in_nograb;

static cvar_t *in_joystick[CL_MAX_SPLITVIEW] 			= {NULL};
static cvar_t *in_joystickThreshold[CL_MAX_SPLITVIEW]	= {NULL};
static cvar_t *in_joystickNo[CL_MAX_SPLITVIEW]			= {NULL};

static int in_eventTime = 0;

static SDL_Window *SDL_window = NULL;

#define CTRL(a) ((a)-'a'+1)

/*
===============
IN_PrintKey
===============
*/
static void IN_PrintKey( const SDL_Keysym *keysym, keyNum_t key, qboolean down )
{
	if( down )
		Com_Printf( "+ " );
	else
		Com_Printf( "  " );

	Com_Printf( "Scancode: 0x%02x(%s) Sym: 0x%02x(%s)",
			keysym->scancode, SDL_GetScancodeName( keysym->scancode ),
			keysym->sym, SDL_GetKeyName( keysym->sym ) );

	if( keysym->mod & KMOD_LSHIFT )   Com_Printf( " KMOD_LSHIFT" );
	if( keysym->mod & KMOD_RSHIFT )   Com_Printf( " KMOD_RSHIFT" );
	if( keysym->mod & KMOD_LCTRL )    Com_Printf( " KMOD_LCTRL" );
	if( keysym->mod & KMOD_RCTRL )    Com_Printf( " KMOD_RCTRL" );
	if( keysym->mod & KMOD_LALT )     Com_Printf( " KMOD_LALT" );
	if( keysym->mod & KMOD_RALT )     Com_Printf( " KMOD_RALT" );
	if( keysym->mod & KMOD_LGUI )     Com_Printf( " KMOD_LGUI" );
	if( keysym->mod & KMOD_RGUI )     Com_Printf( " KMOD_RGUI" );
	if( keysym->mod & KMOD_NUM )      Com_Printf( " KMOD_NUM" );
	if( keysym->mod & KMOD_CAPS )     Com_Printf( " KMOD_CAPS" );
	if( keysym->mod & KMOD_MODE )     Com_Printf( " KMOD_MODE" );
	if( keysym->mod & KMOD_SCROLL )   Com_Printf( " KMOD_SCROLL" );

	Com_Printf( " Q:0x%02x(%s)\n", key, Key_KeynumToString( key ) );
}

#define MAX_CONSOLE_KEYS 16

/*
===============
IN_IsConsoleKey

TODO: If the SDL_Scancode situation improves, use it instead of
      both of these methods
===============
*/
static qboolean IN_IsConsoleKey( keyNum_t key, int character )
{
	typedef struct consoleKey_s
	{
		enum
		{
			QUAKE_KEY,
			CHARACTER
		} type;

		union
		{
			keyNum_t key;
			int character;
		} u;
	} consoleKey_t;

	static consoleKey_t consoleKeys[ MAX_CONSOLE_KEYS ];
	static int numConsoleKeys = 0;
	int i;

	// Only parse the variable when it changes
	if( cl_consoleKeys->modified )
	{
		char *text_p, *token;

		cl_consoleKeys->modified = qfalse;
		text_p = cl_consoleKeys->string;
		numConsoleKeys = 0;

		while( numConsoleKeys < MAX_CONSOLE_KEYS )
		{
			consoleKey_t *c;
			int charCode = 0;
			int keynums[KEYNUMS_PER_STRING];

			token = COM_Parse( &text_p );
			if( !token[ 0 ] )
				break;

			charCode = Com_HexStrToInt( token );

			if( charCode > 0 )
			{
				c = &consoleKeys[ numConsoleKeys ];
				c->type = CHARACTER;
				c->u.character = charCode;
				numConsoleKeys++;
			}
			else if( Key_StringToKeynum( token, keynums ) )
			{
				for ( i = 0; i < KEYNUMS_PER_STRING; i++ )
				{
					if ( keynums[i] == -1 )
						break;
	
					c = &consoleKeys[ numConsoleKeys ];
					c->type = QUAKE_KEY;
					c->u.key = keynums[i];
					numConsoleKeys++;

					if ( numConsoleKeys == MAX_CONSOLE_KEYS )
						break;
				}
			}
		}
	}

	// If the character is the same as the key, prefer the character
	if( key == character )
		key = 0;

	for( i = 0; i < numConsoleKeys; i++ )
	{
		consoleKey_t *c = &consoleKeys[ i ];

		switch( c->type )
		{
			case QUAKE_KEY:
				if( key && c->u.key == key )
					return qtrue;
				break;

			case CHARACTER:
				if( c->u.character == character )
					return qtrue;
				break;
		}
	}

	return qfalse;
}

/*
===============
IN_TranslateSDLToQ3Key
===============
*/
static keyNum_t IN_TranslateSDLToQ3Key( SDL_Keysym *keysym, qboolean down )
{
	keyNum_t key = 0;

	if( keysym->scancode >= SDL_SCANCODE_1 && keysym->scancode <= SDL_SCANCODE_0 )
	{
		// Always map the number keys as such even if they actually map
		// to other characters (eg, "1" is "&" on an AZERTY keyboard).
		// This is required for SDL before 2.0.6, except on Windows
		// which already had this behavior.
		if( keysym->scancode == SDL_SCANCODE_0 )
			key = '0';
		else
			key = '1' + keysym->scancode - SDL_SCANCODE_1;
	}
	else if( keysym->sym >= SDLK_SPACE && keysym->sym < SDLK_DELETE )
	{
		// These happen to match the ASCII chars
		key = (int)keysym->sym;
	}
	else
	{
		switch( keysym->sym )
		{
			case SDLK_PAGEUP:       key = K_PGUP;          break;
			case SDLK_KP_9:         key = K_KP_PGUP;       break;
			case SDLK_PAGEDOWN:     key = K_PGDN;          break;
			case SDLK_KP_3:         key = K_KP_PGDN;       break;
			case SDLK_KP_7:         key = K_KP_HOME;       break;
			case SDLK_HOME:         key = K_HOME;          break;
			case SDLK_KP_1:         key = K_KP_END;        break;
			case SDLK_END:          key = K_END;           break;
			case SDLK_KP_4:         key = K_KP_LEFTARROW;  break;
			case SDLK_LEFT:         key = K_LEFTARROW;     break;
			case SDLK_KP_6:         key = K_KP_RIGHTARROW; break;
			case SDLK_RIGHT:        key = K_RIGHTARROW;    break;
			case SDLK_KP_2:         key = K_KP_DOWNARROW;  break;
			case SDLK_DOWN:         key = K_DOWNARROW;     break;
			case SDLK_KP_8:         key = K_KP_UPARROW;    break;
			case SDLK_UP:           key = K_UPARROW;       break;
			case SDLK_ESCAPE:       key = K_ESCAPE;        break;
			case SDLK_KP_ENTER:     key = K_KP_ENTER;      break;
			case SDLK_RETURN:       key = K_ENTER;         break;
			case SDLK_TAB:          key = K_TAB;           break;
			case SDLK_F1:           key = K_F1;            break;
			case SDLK_F2:           key = K_F2;            break;
			case SDLK_F3:           key = K_F3;            break;
			case SDLK_F4:           key = K_F4;            break;
			case SDLK_F5:           key = K_F5;            break;
			case SDLK_F6:           key = K_F6;            break;
			case SDLK_F7:           key = K_F7;            break;
			case SDLK_F8:           key = K_F8;            break;
			case SDLK_F9:           key = K_F9;            break;
			case SDLK_F10:          key = K_F10;           break;
			case SDLK_F11:          key = K_F11;           break;
			case SDLK_F12:          key = K_F12;           break;
			case SDLK_F13:          key = K_F13;           break;
			case SDLK_F14:          key = K_F14;           break;
			case SDLK_F15:          key = K_F15;           break;

			case SDLK_BACKSPACE:    key = K_BACKSPACE;     break;
			case SDLK_KP_PERIOD:    key = K_KP_DEL;        break;
			case SDLK_DELETE:       key = K_DEL;           break;
			case SDLK_PAUSE:        key = K_PAUSE;         break;

			case SDLK_LSHIFT:       key = K_LEFTSHIFT;     break;
			case SDLK_RSHIFT:       key = K_RIGHTSHIFT;    break;

			case SDLK_LCTRL:        key = K_LEFTCTRL;      break;
			case SDLK_RCTRL:        key = K_RIGHTCTRL;     break;

#ifdef __APPLE__

			case SDLK_LGUI:         key = K_LEFTCOMMAND;   break;
			case SDLK_RGUI:         key = K_RIGHTCOMMAND;  break;
#else

			case SDLK_LGUI:         key = K_LEFTSUPER;     break;
			case SDLK_RGUI:         key = K_RIGHTSUPER;    break;
#endif

			case SDLK_LALT:         key = K_LEFTALT;       break;
			case SDLK_RALT:         key = K_RIGHTALT;      break;

			case SDLK_KP_5:         key = K_KP_5;          break;
			case SDLK_INSERT:       key = K_INS;           break;
			case SDLK_KP_0:         key = K_KP_INS;        break;
			case SDLK_KP_MULTIPLY:  key = K_KP_STAR;       break;
			case SDLK_KP_PLUS:      key = K_KP_PLUS;       break;
			case SDLK_KP_MINUS:     key = K_KP_MINUS;      break;
			case SDLK_KP_DIVIDE:    key = K_KP_SLASH;      break;

			case SDLK_MODE:         key = K_MODE;          break;
			case SDLK_HELP:         key = K_HELP;          break;
			case SDLK_PRINTSCREEN:  key = K_PRINT;         break;
			case SDLK_SYSREQ:       key = K_SYSREQ;        break;
			case SDLK_MENU:         key = K_MENU;          break;
			case SDLK_APPLICATION:	key = K_MENU;          break;
			case SDLK_POWER:        key = K_POWER;         break;
			case SDLK_UNDO:         key = K_UNDO;          break;
			case SDLK_SCROLLLOCK:   key = K_SCROLLOCK;     break;
			case SDLK_NUMLOCKCLEAR: key = K_KP_NUMLOCK;    break;
			case SDLK_CAPSLOCK:     key = K_CAPSLOCK;      break;

			default:
				if( !( keysym->sym & SDLK_SCANCODE_MASK ) && keysym->scancode <= 255 )
				{
					// Map Unicode characters to 255 world keys using the key's scan code.
					// TODO: Maybe create a map of scancode to quake key at start up and on
					// key map change; allocate world key numbers as needed similar
					// to SDL 1.2.
					key = K_WORLD_0 + (int)keysym->scancode;
				}
				break;
		}
	}

	if( in_keyboardDebug->integer )
		IN_PrintKey( keysym, key, down );

	if( IN_IsConsoleKey( key, 0 ) )
	{
		// Console keys can't be bound or generate characters
		key = K_CONSOLE;
	}

	return key;
}

/*
===============
IN_GobbleMotionEvents
===============
*/
static void IN_GobbleMotionEvents( void )
{
	SDL_Event dummy[ 1 ];
	int val = 0;

	// Gobble any mouse motion events
	SDL_PumpEvents( );
	while( ( val = SDL_PeepEvents( dummy, 1, SDL_GETEVENT,
		SDL_MOUSEMOTION, SDL_MOUSEMOTION ) ) > 0 ) { }

	if ( val < 0 )
		Com_Printf( "IN_GobbleMotionEvents failed: %s\n", SDL_GetError( ) );
}

/*
===============
IN_GetUIMousePosition
===============
*/
static void IN_GetUIMousePosition( int localPlayerNum, int *x, int *y )
{
	if( cgvm )
	{
		int pos = VM_Call( cgvm, CG_MOUSE_POSITION, localPlayerNum );
		*x = Com_Clamp(0, cls.glconfig.vidWidth - 1, pos & 0xFFFF);
		*y = Com_Clamp(0, cls.glconfig.vidHeight - 1, ( pos >> 16 ) & 0xFFFF);
	}
	else
	{
		*x = cls.glconfig.vidWidth / 2;
		*y = cls.glconfig.vidHeight / 2;
	}
}

/*
===============
IN_SetUIMousePosition
===============
*/
static void IN_SetUIMousePosition( int localPlayerNum, int x, int y )
{
	if( cgvm )
	{
		VM_Call( cgvm, CG_SET_MOUSE_POSITION, localPlayerNum, x, y );
	}
}

/*
===============
IN_ActivateMouse
===============
*/
static void IN_ActivateMouse( qboolean isFullscreen )
{
	if (!mouseAvailable || !SDL_WasInit( SDL_INIT_VIDEO ) )
		return;

	if( !mouseActive )
	{
		SDL_SetRelativeMouseMode( SDL_TRUE );
		SDL_SetWindowGrab( SDL_window, SDL_TRUE );

		IN_GobbleMotionEvents( );
	}

	// in_nograb makes no sense in fullscreen mode
	if( !isFullscreen )
	{
		if( in_nograb->modified || !mouseActive )
		{
			if( in_nograb->integer ) {
				SDL_SetRelativeMouseMode( SDL_FALSE );
				SDL_SetWindowGrab( SDL_window, SDL_FALSE );
			} else {
				SDL_SetRelativeMouseMode( SDL_TRUE );
				SDL_SetWindowGrab( SDL_window, SDL_TRUE );
			}

			in_nograb->modified = qfalse;
		}
	}

	mouseActive = qtrue;
}

/*
===============
IN_DeactivateMouse
===============
*/
static void IN_DeactivateMouse( qboolean isFullscreen, qboolean showSystemCursor )
{
	if( !SDL_WasInit( SDL_INIT_VIDEO ) )
		return;

	// Show the cursor when the mouse is disabled and not drawing UI cursor,
	// but not when fullscreen
	if( isFullscreen )
		SDL_ShowCursor( SDL_FALSE );
	else
		SDL_ShowCursor( showSystemCursor );

	if( !mouseAvailable )
		return;

	if( mouseActive )
	{
		IN_GobbleMotionEvents( );

		SDL_SetWindowGrab( SDL_window, SDL_FALSE );
		SDL_SetRelativeMouseMode( SDL_FALSE );

		// Don't warp the mouse unless the cursor is within the window
		if( SDL_GetWindowFlags( SDL_window ) & SDL_WINDOW_MOUSE_FOCUS )
		{
			int x, y;
			IN_GetUIMousePosition( 0, &x, &y );
			SDL_WarpMouseInWindow( SDL_window, x, y );
		}

		mouseActive = qfalse;
	}
}

/*
===============
IN_SetGameControllerDefaults
===============
*/
static qboolean IN_SetGameControllerDefaults( int localPlayerNum, int joystickNum ) {
	struct {
		int sdlAxis;
		int negKey;
		int posKey;
	} axisRemap[] = {
		{ SDL_CONTROLLER_AXIS_LEFTX, K_JOY_LEFTSTICK_LEFT, K_JOY_LEFTSTICK_RIGHT },
		{ SDL_CONTROLLER_AXIS_LEFTY, K_JOY_LEFTSTICK_UP, K_JOY_LEFTSTICK_DOWN },
		{ SDL_CONTROLLER_AXIS_RIGHTX, K_JOY_RIGHTSTICK_LEFT, K_JOY_RIGHTSTICK_RIGHT },
		{ SDL_CONTROLLER_AXIS_RIGHTY, K_JOY_RIGHTSTICK_UP, K_JOY_RIGHTSTICK_DOWN },
		{ SDL_CONTROLLER_AXIS_TRIGGERLEFT, -1, K_JOY_LEFTTRIGGER },
		{ SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -1, K_JOY_RIGHTTRIGGER }
	};
	struct {
		int sdlButton;
		int key;
	} buttonRemap[] = {
		{ SDL_CONTROLLER_BUTTON_A, K_JOY_A },
		{ SDL_CONTROLLER_BUTTON_B, K_JOY_B },
		{ SDL_CONTROLLER_BUTTON_X, K_JOY_X },
		{ SDL_CONTROLLER_BUTTON_Y, K_JOY_Y },
		{ SDL_CONTROLLER_BUTTON_BACK, K_JOY_BACK },
		{ SDL_CONTROLLER_BUTTON_GUIDE, K_JOY_GUIDE },
		{ SDL_CONTROLLER_BUTTON_START, K_JOY_START },
		{ SDL_CONTROLLER_BUTTON_LEFTSTICK, K_JOY_LEFTSTICK },
		{ SDL_CONTROLLER_BUTTON_RIGHTSTICK, K_JOY_RIGHTSTICK },
		{ SDL_CONTROLLER_BUTTON_LEFTSHOULDER, K_JOY_LEFTSHOULDER },
		{ SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, K_JOY_RIGHTSHOULDER },
		{ SDL_CONTROLLER_BUTTON_DPAD_UP, K_JOY_DPAD_UP },
		{ SDL_CONTROLLER_BUTTON_DPAD_DOWN, K_JOY_DPAD_DOWN },
		{ SDL_CONTROLLER_BUTTON_DPAD_LEFT, K_JOY_DPAD_LEFT },
		{ SDL_CONTROLLER_BUTTON_DPAD_RIGHT, K_JOY_DPAD_RIGHT },
#if SDL_VERSION_ATLEAST( 2, 0, 14 )
		{ SDL_CONTROLLER_BUTTON_MISC1, K_JOY_MISC1 },
		{ SDL_CONTROLLER_BUTTON_PADDLE1, K_JOY_PADDLE1 },
		{ SDL_CONTROLLER_BUTTON_PADDLE2, K_JOY_PADDLE2 },
		{ SDL_CONTROLLER_BUTTON_PADDLE3, K_JOY_PADDLE3 },
		{ SDL_CONTROLLER_BUTTON_PADDLE4, K_JOY_PADDLE4 },
		{ SDL_CONTROLLER_BUTTON_TOUCHPAD, K_JOY_TOUCHPAD }
#endif
	};

	SDL_GameControllerButtonBind bind;
	int b;
	joyevent_t event;
	SDL_GameController *controller;

	controller = SDL_GameControllerOpen( joystickNum );

	if ( !controller ) {
		return qfalse;
	}

	for ( b = 0; b < ARRAY_LEN( axisRemap ); b++ ) {
		bind = SDL_GameControllerGetBindForAxis( controller, axisRemap[b].sdlAxis );

		if ( bind.bindType == SDL_CONTROLLER_BINDTYPE_AXIS ) {
			event.type = JOYEVENT_AXIS;
			event.value.axis.num = bind.value.axis;
			event.value.axis.sign = 1;
			CL_SetKeyForJoyEvent( localPlayerNum, &event, axisRemap[b].posKey );

			if ( axisRemap[b].negKey != -1 ) {
				event.value.axis.sign = -1;
				CL_SetKeyForJoyEvent( localPlayerNum, &event, axisRemap[b].negKey );
			}
			continue;
		}

		switch ( bind.bindType ) {
			case SDL_CONTROLLER_BINDTYPE_BUTTON:
				event.type = JOYEVENT_BUTTON;
				event.value.button = bind.value.button;
				break;
			case SDL_CONTROLLER_BINDTYPE_HAT:
				event.type = JOYEVENT_HAT;
				event.value.hat.num = bind.value.hat.hat;

				switch( bind.value.hat.hat_mask )
				{
					case SDL_HAT_UP:
						event.value.hat.mask = HAT_UP;
						break;
					case SDL_HAT_RIGHT:
						event.value.hat.mask = HAT_RIGHT;
						break;
					case SDL_HAT_DOWN:
						event.value.hat.mask = HAT_DOWN;
						break;
					case SDL_HAT_LEFT:
						event.value.hat.mask = HAT_LEFT;
						break;
					case SDL_HAT_RIGHTUP:
						event.value.hat.mask = HAT_RIGHTUP;
						break;
					case SDL_HAT_RIGHTDOWN:
						event.value.hat.mask = HAT_RIGHTDOWN;
						break;
					case SDL_HAT_LEFTUP:
						event.value.hat.mask = HAT_LEFTUP;
						break;
					case SDL_HAT_LEFTDOWN:
						event.value.hat.mask = HAT_LEFTDOWN;
						break;
					case SDL_HAT_CENTERED:
					default:
						event.value.hat.mask = HAT_CENTERED;
						break;
				}
				break;
			case SDL_CONTROLLER_BINDTYPE_NONE:
			default:
				continue;
		}

		// buttons and hats only handle positive axis key (works for triggers, but not left/right sticks)
		CL_SetKeyForJoyEvent( localPlayerNum, &event, axisRemap[b].posKey );
	}

	for ( b = 0; b < ARRAY_LEN( buttonRemap ); b++ ) {
		bind = SDL_GameControllerGetBindForButton( controller, buttonRemap[b].sdlButton );
		switch ( bind.bindType ) {
			case SDL_CONTROLLER_BINDTYPE_BUTTON:
				event.type = JOYEVENT_BUTTON;
				event.value.button = bind.value.button;
				break;
			case SDL_CONTROLLER_BINDTYPE_HAT:
				event.type = JOYEVENT_HAT;
				event.value.hat.num = bind.value.hat.hat;

				switch( bind.value.hat.hat_mask )
				{
					case SDL_HAT_UP:
						event.value.hat.mask = HAT_UP;
						break;
					case SDL_HAT_RIGHT:
						event.value.hat.mask = HAT_RIGHT;
						break;
					case SDL_HAT_DOWN:
						event.value.hat.mask = HAT_DOWN;
						break;
					case SDL_HAT_LEFT:
						event.value.hat.mask = HAT_LEFT;
						break;
					case SDL_HAT_RIGHTUP:
						event.value.hat.mask = HAT_RIGHTUP;
						break;
					case SDL_HAT_RIGHTDOWN:
						event.value.hat.mask = HAT_RIGHTDOWN;
						break;
					case SDL_HAT_LEFTUP:
						event.value.hat.mask = HAT_LEFTUP;
						break;
					case SDL_HAT_LEFTDOWN:
						event.value.hat.mask = HAT_LEFTDOWN;
						break;
					case SDL_HAT_CENTERED:
					default:
						event.value.hat.mask = HAT_CENTERED;
						break;
				}
				break;
			case SDL_CONTROLLER_BINDTYPE_AXIS:
				event.type = JOYEVENT_AXIS;
				event.value.axis.num = bind.value.axis;
				event.value.axis.sign = 1;
				// no negative axis event.
				break;
			case SDL_CONTROLLER_BINDTYPE_NONE:
			default:
				continue;
		}

		CL_SetKeyForJoyEvent( localPlayerNum, &event, buttonRemap[b].key );
	}

	// don't archive these
	CL_SetAutoJoyRemap( localPlayerNum );

	SDL_GameControllerClose( controller );
	return qtrue;
}


struct
{
	qboolean buttons[MAX_JOYSTICK_BUTTONS];
	int oldaaxes[MAX_JOYSTICK_AXIS];
	Uint8 oldhats[MAX_JOYSTICK_HATS];
} stick_state[CL_MAX_SPLITVIEW];


/*
===============
IN_InitJoystick
===============
*/
static void IN_InitJoystick( void )
{
	int i = 0, j;
	int total = 0;
	char buf[16384] = "";
	qboolean joyEnabled = qfalse;
	char guid[64];

	for (i = 0; i < CL_MAX_SPLITVIEW; i++) {
		if (stick[i] != NULL) {
			CL_CloseJoystickRemap( i );
			SDL_JoystickClose(stick[i]);
		}

		stick[i] = NULL;
		memset(&stick_state[i], '\0', sizeof (stick_state[i]));

		if( in_joystick[i]->integer ) {
			joyEnabled = qtrue;
		}
	}

	// SDL 2.0.4 requires SDL_INIT_JOYSTICK to be initialized separately from
	// SDL_INIT_GAMECONTROLLER for SDL_JoystickOpen() to work correctly,
	// despite https://wiki.libsdl.org/SDL_Init (retrieved 2016-08-16)
	// indicating SDL_INIT_JOYSTICK should be initialized automatically.
	if (!SDL_WasInit(SDL_INIT_JOYSTICK))
	{
		Com_DPrintf("Calling SDL_Init(SDL_INIT_JOYSTICK)...\n");
		if (SDL_Init(SDL_INIT_JOYSTICK) != 0)
		{
			Com_DPrintf("SDL_Init(SDL_INIT_JOYSTICK) failed: %s\n", SDL_GetError());
			return;
		}
		Com_DPrintf("SDL_Init(SDL_INIT_JOYSTICK) passed.\n");
	}

	total = SDL_NumJoysticks();
	Com_DPrintf("%d possible joysticks\n", total);

	// Print list and build cvar to allow ui to select joystick.
	for (i = 0; i < total; i++)
	{
		Q_strcat(buf, sizeof(buf), SDL_JoystickNameForIndex(i));
		Q_strcat(buf, sizeof(buf), "\n");
	}

	Cvar_Get( "in_availableJoysticks", "", CVAR_ROM );

	// Update cvar on in_restart or controller add/remove.
	Cvar_Set( "in_availableJoysticks", buf );

	if ( !joyEnabled ) {
		Com_DPrintf( "Joystick is not active.\n" );
		return;
	}

	if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER))
	{
		Com_DPrintf("Calling SDL_Init(SDL_INIT_GAMECONTROLLER)...\n");
		if (SDL_Init(SDL_INIT_GAMECONTROLLER) == -1)
		{
			Com_DPrintf("SDL_Init(SDL_INIT_GAMECONTROLLER) failed: %s\n", SDL_GetError());
		}
		else
		{
			Com_DPrintf("SDL_Init(SDL_INIT_GAMECONTROLLER) passed.\n");

#if SDL_VERSION_ATLEAST( 2, 0, 2 )
			{
				static qboolean firstTime = qtrue;
				const char *homePath = Cvar_VariableString("fs_homepath");
				const char *basePath = Cvar_VariableString("fs_basepath");
				char *testpath;
				int numMappings;

				// check if mappings file is in home path, else try basepath
				testpath = FS_BuildOSPath( homePath, "gamecontrollerdb.txt", "");
				testpath[strlen(testpath)-1] = '\0';

				if ( !FS_FileInPathExists(testpath) ) {
					testpath = FS_BuildOSPath( basePath, "gamecontrollerdb.txt", "");
					testpath[strlen(testpath)-1] = '\0';
				}

				numMappings = SDL_GameControllerAddMappingsFromFile(testpath);
				if ( numMappings < 0 ) {
					Com_DPrintf("WARNING: Couldn't load gamecontrollerdb.txt\n" );
				} else if ( firstTime || com_developer->integer ) {
					firstTime = qfalse;
					Com_Printf("Loaded %d controller mappings from %s\n", numMappings, testpath );
				}
			}
#endif
		}
	}

	for (i = 0; i < CL_MAX_SPLITVIEW; i++) {
		if( !in_joystick[i]->integer ) {
			continue;
		}

		in_joystickNo[i] = Cvar_Get( Com_LocalPlayerCvarName(i, "in_joystickNo"), "0", CVAR_ARCHIVE );
		if( in_joystickNo[i]->integer < 0 || in_joystickNo[i]->integer >= total )
			Cvar_Set( Com_LocalPlayerCvarName(i, "in_joystickNo"), "0" );

		// don't allow multiple players to use the same joystick
		for (j = 0; j < i; j++) {
			if ( in_joystick[j]->integer && in_joystickNo[j]->integer == in_joystickNo[i]->integer ) {
				break;
			}
		}

		if ( j != i ) {
			Com_DPrintf( "Joystick for player %d already in use by player %d.\n", i+1, j+1 );
			continue;
		}

		stick[i] = SDL_JoystickOpen( in_joystickNo[i]->integer );

		if (stick[i] == NULL) {
			Com_DPrintf( "Opening joystick for player %d failed.\n", i+1 );
			continue;
		}

		SDL_JoystickGetGUIDString( SDL_JoystickGetGUID(stick[i]), guid, sizeof ( guid ) );

		Com_DPrintf( "Joystick %d opened for player %d\n", in_joystickNo[i]->integer, i+1 );
		Com_DPrintf( "Name:       %s\n", SDL_JoystickNameForIndex(in_joystickNo[i]->integer) );
		Com_DPrintf( "GUID:       %s\n", guid );
		Com_DPrintf( "Axes:       %d\n", SDL_JoystickNumAxes(stick[i]) );
		Com_DPrintf( "Hats:       %d\n", SDL_JoystickNumHats(stick[i]) );
		Com_DPrintf( "Buttons:    %d\n", SDL_JoystickNumButtons(stick[i]) );
		Com_DPrintf( "Balls:      %d\n", SDL_JoystickNumBalls(stick[i]) );

		if ( !CL_OpenJoystickRemap( i, SDL_JoystickNameForIndex(in_joystickNo[i]->integer), guid ) ) {
			if ( !IN_SetGameControllerDefaults( i, in_joystickNo[i]->integer ) ) {
				Com_Printf( "No joystick remap found for %s (GUID: %s)\n",
						SDL_JoystickNameForIndex(in_joystickNo[i]->integer), guid );
			}
		}
	}

	if ( SDL_WasInit( SDL_INIT_GAMECONTROLLER ) )
	{
		SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
	}

	SDL_JoystickEventState(SDL_QUERY);
}

/*
===============
IN_ShutdownJoystick
===============
*/
static void IN_ShutdownJoystick( void )
{
	int		i;

	if ( SDL_WasInit( SDL_INIT_GAMECONTROLLER ) )
	{
		SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
	}

	if ( !SDL_WasInit( SDL_INIT_JOYSTICK ) )
		return;

	for (i = 0; i < CL_MAX_SPLITVIEW; i++) {
		if (stick[i]) {
			CL_CloseJoystickRemap( i );
			SDL_JoystickClose(stick[i]);
			stick[i] = NULL;
		}
	}

	SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
}

/*
===============
IN_JoyMove
===============
*/
static void IN_JoyMove( void )
{
	int total = 0;
	int i = 0;
	int joy;

	for (joy = 0; joy < CL_MAX_SPLITVIEW; joy++) {
		if (stick[joy]) {
			break;
		}
	}

	if (joy == CL_MAX_SPLITVIEW) {
		return;
	}

	SDL_JoystickUpdate();

	for (joy = 0; joy < CL_MAX_SPLITVIEW; joy++) {
		// update the ball state.
		total = SDL_JoystickNumBalls(stick[joy]);
		if (total > 0)
		{
			int balldx = 0;
			int balldy = 0;
			for (i = 0; i < total; i++)
			{
				int dx = 0;
				int dy = 0;
				SDL_JoystickGetBall(stick[joy], i, &dx, &dy);
				balldx += dx;
				balldy += dy;
			}
			if (balldx || balldy)
			{
				// !!! FIXME: is this good for stick balls, or just mice?
				// Scale like the mouse input...
				if (abs(balldx) > 1)
					balldx *= 2;
				if (abs(balldy) > 1)
					balldy *= 2;
				Com_QueueEvent( in_eventTime, SE_MOUSE + joy, balldx, balldy, 0, NULL );
			}
		}

		// now query the stick buttons...
		total = SDL_JoystickNumButtons(stick[joy]);
		if (total > 0)
		{
			if (total > ARRAY_LEN(stick_state[joy].buttons))
				total = ARRAY_LEN(stick_state[joy].buttons);
			for (i = 0; i < total; i++)
			{
				qboolean pressed = (SDL_JoystickGetButton(stick[joy], i) != 0);
				if (pressed != stick_state[joy].buttons[i])
				{
					Com_QueueEvent( in_eventTime, SE_JOYSTICK_BUTTON+joy, i, pressed, 0, NULL );
					stick_state[joy].buttons[i] = pressed;
				}
			}
		}

		// look at the hats...
		total = SDL_JoystickNumHats(stick[joy]);
		if (total > 0)
		{
			if (total > ARRAY_LEN(stick_state[joy].oldhats))
				total = ARRAY_LEN(stick_state[joy].oldhats);
			for (i = 0; i < total; i++)
			{
				Uint8 state = SDL_JoystickGetHat(stick[joy], i), value;
				if ( state != stick_state[joy].oldhats[i] )
				{
					switch( state )
					{
						case SDL_HAT_UP:
							value = HAT_UP;
							break;
						case SDL_HAT_RIGHT:
							value = HAT_RIGHT;
							break;
						case SDL_HAT_DOWN:
							value = HAT_DOWN;
							break;
						case SDL_HAT_LEFT:
							value = HAT_LEFT;
							break;
						case SDL_HAT_RIGHTUP:
							value = HAT_RIGHTUP;
							break;
						case SDL_HAT_RIGHTDOWN:
							value = HAT_RIGHTDOWN;
							break;
						case SDL_HAT_LEFTUP:
							value = HAT_LEFTUP;
							break;
						case SDL_HAT_LEFTDOWN:
							value = HAT_LEFTDOWN;
							break;
						case SDL_HAT_CENTERED:
						default:
							value = HAT_CENTERED;
							break;
					}

					Com_QueueEvent( in_eventTime, SE_JOYSTICK_HAT+joy, i, value, 0, NULL );
					stick_state[joy].oldhats[i] = state;
				}
			}
		}

		// finally, look at the axes...
		total = SDL_JoystickNumAxes(stick[joy]);
		if (total > 0)
		{
			if (total > MAX_JOYSTICK_AXIS) total = MAX_JOYSTICK_AXIS;
			for (i = 0; i < total; i++)
			{
				Sint16 axis = SDL_JoystickGetAxis(stick[joy], i);
				float f = ( (float) abs(axis) ) / 32767.0f;
				
				if( f < in_joystickThreshold[joy]->value ) axis = 0;

				if ( axis != stick_state[joy].oldaaxes[i] )
				{
					Com_QueueEvent( in_eventTime, SE_JOYSTICK_AXIS + joy, i, axis, 0, NULL );
					stick_state[joy].oldaaxes[i] = axis;
				}
			}
		}
	}
}

/*
===============
IN_ProcessEvents
===============
*/
static void IN_ProcessEvents( void )
{
	SDL_Event e;
	keyNum_t key = 0;
	static keyNum_t lastKeyDown = 0;

	if( !SDL_WasInit( SDL_INIT_VIDEO ) )
			return;

	while( SDL_PollEvent( &e ) )
	{
		switch( e.type )
		{
			case SDL_KEYDOWN:
				if ( e.key.repeat && Key_GetRepeat( ) == qfalse )
					break;

				if( ( key = IN_TranslateSDLToQ3Key( &e.key.keysym, qtrue ) ) )
					Com_QueueEvent( in_eventTime, SE_KEY, key, qtrue, 0, NULL );

				if( key == K_BACKSPACE )
					Com_QueueEvent( in_eventTime, SE_CHAR, CTRL('h'), 0, 0, NULL );
				else if( ( keys[K_LEFTCTRL].down || keys[K_RIGHTCTRL].down ) && key >= 'a' && key <= 'z' )
					Com_QueueEvent( in_eventTime, SE_CHAR, CTRL(key), 0, 0, NULL );

				lastKeyDown = key;
				break;

			case SDL_KEYUP:
				if( ( key = IN_TranslateSDLToQ3Key( &e.key.keysym, qfalse ) ) )
					Com_QueueEvent( in_eventTime, SE_KEY, key, qfalse, 0, NULL );

				lastKeyDown = 0;
				break;

			case SDL_TEXTINPUT:
				if( lastKeyDown != K_CONSOLE )
				{
					char *c = e.text.text;

					// Quick and dirty UTF-8 to UTF-32 conversion
					while( *c )
					{
						int utf32 = 0;

						if( ( *c & 0x80 ) == 0 )
							utf32 = *c++;
						else if( ( *c & 0xE0 ) == 0xC0 ) // 110x xxxx
						{
							utf32 |= ( *c++ & 0x1F ) << 6;
							utf32 |= ( *c++ & 0x3F );
						}
						else if( ( *c & 0xF0 ) == 0xE0 ) // 1110 xxxx
						{
							utf32 |= ( *c++ & 0x0F ) << 12;
							utf32 |= ( *c++ & 0x3F ) << 6;
							utf32 |= ( *c++ & 0x3F );
						}
						else if( ( *c & 0xF8 ) == 0xF0 ) // 1111 0xxx
						{
							utf32 |= ( *c++ & 0x07 ) << 18;
							utf32 |= ( *c++ & 0x3F ) << 12;
							utf32 |= ( *c++ & 0x3F ) << 6;
							utf32 |= ( *c++ & 0x3F );
						}
						else
						{
							Com_DPrintf( "Unrecognised UTF-8 lead byte: 0x%x\n", (unsigned int)*c );
							c++;
						}

						if( utf32 != 0 )
						{
							if( IN_IsConsoleKey( 0, utf32 ) )
							{
								Com_QueueEvent( in_eventTime, SE_KEY, K_CONSOLE, qtrue, 0, NULL );
								Com_QueueEvent( in_eventTime, SE_KEY, K_CONSOLE, qfalse, 0, NULL );
							}
							else
								Com_QueueEvent( in_eventTime, SE_CHAR, utf32, 0, 0, NULL );
						}
          }
        }
				break;

			case SDL_MOUSEMOTION:
				if( mouseActive )
				{
					if( !e.motion.xrel && !e.motion.yrel )
						break;
					Com_QueueEvent( in_eventTime, SE_MOUSE, e.motion.xrel, e.motion.yrel, 0, NULL );
				}
				break;

			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				{
					int b;
					switch( e.button.button )
					{
						case SDL_BUTTON_LEFT:   b = K_MOUSE1;     break;
						case SDL_BUTTON_MIDDLE: b = K_MOUSE3;     break;
						case SDL_BUTTON_RIGHT:  b = K_MOUSE2;     break;
						case SDL_BUTTON_X1:     b = K_MOUSE4;     break;
						case SDL_BUTTON_X2:     b = K_MOUSE5;     break;

						// ZTM: HACK around incorrect SDL2 code
						// SDL X11 passes the button value directly from X11 instead of remapping to SDL_BUTTON_*
						// Other than that, SDL does not use buttons aside from the above 5, so.. ignore how this would affect AUX
						case 8:                 b = K_MOUSE4;     break;
						case 9:                 b = K_MOUSE5;     break;

						default:                b = K_AUX1 + ( e.button.button - SDL_BUTTON_X2 + 1 ) % 16; break;
					}
					Com_QueueEvent( in_eventTime, SE_KEY, b,
						( e.type == SDL_MOUSEBUTTONDOWN ? qtrue : qfalse ), 0, NULL );
				}
				break;

			case SDL_MOUSEWHEEL:
				if( e.wheel.y > 0 )
				{
					Com_QueueEvent( in_eventTime, SE_KEY, K_MWHEELUP, qtrue, 0, NULL );
					Com_QueueEvent( in_eventTime, SE_KEY, K_MWHEELUP, qfalse, 0, NULL );
				}
				else if( e.wheel.y < 0 )
				{
					Com_QueueEvent( in_eventTime, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL );
					Com_QueueEvent( in_eventTime, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL );
				}

				if( e.wheel.x > 0 )
				{
					Com_QueueEvent( in_eventTime, SE_KEY, K_MWHEELRIGHT, qtrue, 0, NULL );
					Com_QueueEvent( in_eventTime, SE_KEY, K_MWHEELRIGHT, qfalse, 0, NULL );
				}
				else if( e.wheel.x < 0 )
				{
					Com_QueueEvent( in_eventTime, SE_KEY, K_MWHEELLEFT, qtrue, 0, NULL );
					Com_QueueEvent( in_eventTime, SE_KEY, K_MWHEELLEFT, qfalse, 0, NULL );
				}
				break;

			case SDL_CONTROLLERDEVICEADDED:
			case SDL_CONTROLLERDEVICEREMOVED:
#if 0 // ZTM: TODO
				if (in_joystick->integer)
					IN_InitJoystick();
#endif
				break;

			case SDL_QUIT:
				Cbuf_ExecuteText(EXEC_NOW, "quit Closed window\n");
				break;

			case SDL_WINDOWEVENT:
				switch( e.window.event )
				{
					case SDL_WINDOWEVENT_RESIZED:
						{
							int width, height;

							width = e.window.data1;
							height = e.window.data2;

							// check if size actually changed
							if( cls.glconfig.vidWidth == width && cls.glconfig.vidHeight == height )
							{
								break;
							}

							Cvar_SetValue( "r_customwidth", width );
							Cvar_SetValue( "r_customheight", height );
							Cvar_Set( "r_mode", "-1" );

							re.ResizeWindow( width, height );
						}
						break;

					case SDL_WINDOWEVENT_MINIMIZED:    Cvar_SetValue( "com_minimized", 1 ); break;
					case SDL_WINDOWEVENT_RESTORED:
					case SDL_WINDOWEVENT_MAXIMIZED:    Cvar_SetValue( "com_minimized", 0 ); break;
					case SDL_WINDOWEVENT_FOCUS_LOST:   Cvar_SetValue( "com_unfocused", 1 ); break;
					case SDL_WINDOWEVENT_FOCUS_GAINED: Cvar_SetValue( "com_unfocused", 0 ); break;
				}
				break;

			case SDL_DROPFILE:
				{
					char *filename = e.drop.file;

#if defined(PROTOCOL_HANDLER) && defined(__APPLE__)
					// Handle macOS open URL event. URL protocol scheme must be set in Info.plist.
					if( !Q_strncmp( filename, PROTOCOL_HANDLER ":", strlen( PROTOCOL_HANDLER ":" ) ) )
					{
						char *protocolCommand = Sys_ParseProtocolUri( filename );

						if( protocolCommand )
						{
							Cbuf_ExecuteText( EXEC_APPEND, va( "%s\n", protocolCommand ) );
							free( protocolCommand );
						}
					}
					else
#endif
					// ZTM: TODO: Open file and check for DEMO_MAGIC so it work with any demo extension?
					if ( FS_IsDemoExt( filename, strlen( filename ) ) ) {
						CL_PlayDemo( filename );
					} else {
						Com_Printf( "Ignoring unknown file: %s\n", filename );
					}

					SDL_free( filename );
				}
				break;

			default:
				break;
		}
	}
}

/*
===============
IN_Frame
===============
*/
void IN_Frame( void )
{
	qboolean loading;
	qboolean mouseGrab;
	qboolean systemCursor;
	int x, y;

	IN_JoyMove( );

	// If not DISCONNECTED (main menu) or ACTIVE (in game), we're loading
	loading = ( clc.state != CA_DISCONNECTED && clc.state != CA_ACTIVE );
	mouseGrab = ( Mouse_GetState( 0 ) & MOUSE_CLIENT );
	systemCursor = ( Mouse_GetState( 0 ) & MOUSE_SYSTEMCURSOR );

	if( !cls.glconfig.isFullscreen && !mouseGrab && systemCursor )
	{
		// Loading in windowed mode
		IN_DeactivateMouse( cls.glconfig.isFullscreen, qtrue );
	}
	else if( !cls.glconfig.isFullscreen && loading )
	{
		// Loading in windowed mode
		IN_DeactivateMouse( cls.glconfig.isFullscreen, qtrue );
	}
	else if( !cls.glconfig.isFullscreen && !mouseGrab )
	{
		// Showing cursor in windowed mode
		IN_DeactivateMouse( cls.glconfig.isFullscreen, qfalse );
	}
	else if( !( SDL_GetWindowFlags( SDL_window ) & SDL_WINDOW_INPUT_FOCUS ) )
	{
		// Window not got focus
		IN_DeactivateMouse( cls.glconfig.isFullscreen, qtrue );
	}
	else
		IN_ActivateMouse( cls.glconfig.isFullscreen );

	IN_ProcessEvents( );

	// Set event time for next frame to earliest possible time an event could happen
	in_eventTime = Sys_Milliseconds( );

	if( !mouseActive )
	{
		SDL_GetMouseState( &x, &y );
		IN_SetUIMousePosition( 0, x, y );
	}
}

/*
===============
IN_Init
===============
*/
void IN_Init( void *windowData )
{
	int appState;
	int i;

	if( !SDL_WasInit( SDL_INIT_VIDEO ) )
	{
		Com_Error( ERR_FATAL, "IN_Init called before SDL_Init( SDL_INIT_VIDEO )" );
		return;
	}

	SDL_window = (SDL_Window *)windowData;

	Com_DPrintf( "\n------- Input Initialization -------\n" );

	in_keyboardDebug = Cvar_Get( "in_keyboardDebug", "0", CVAR_ARCHIVE );

	// mouse variables
	in_mouse = Cvar_Get( "in_mouse", "1", CVAR_ARCHIVE );
	in_nograb = Cvar_Get( "in_nograb", "0", CVAR_ARCHIVE );

	for (i = 0; i < CL_MAX_SPLITVIEW; i++) {
		in_joystick[i] = Cvar_Get( Com_LocalPlayerCvarName(i, "in_joystick"), "0", CVAR_ARCHIVE|CVAR_LATCH );
		in_joystickThreshold[i] = Cvar_Get( Com_LocalPlayerCvarName(i, "in_joystickThreshold"), "0.15", CVAR_ARCHIVE );
		Cvar_CheckRange(in_joystickThreshold[i], 0, 0.9f, qfalse);
	}

	SDL_EventState( SDL_DROPFILE, SDL_ENABLE );

	SDL_StartTextInput( );

	mouseAvailable = ( in_mouse->value != 0 );
	IN_DeactivateMouse( Cvar_VariableIntegerValue( "r_fullscreen" ) != 0, qtrue );

	appState = SDL_GetWindowFlags( SDL_window );
	Cvar_SetValue( "com_unfocused",	!( appState & SDL_WINDOW_INPUT_FOCUS ) );
	Cvar_SetValue( "com_minimized", appState & SDL_WINDOW_MINIMIZED );

	IN_InitJoystick( );
	Com_DPrintf( "------------------------------------\n" );
}

/*
===============
IN_Shutdown
===============
*/
void IN_Shutdown( void )
{
	SDL_StopTextInput( );

	IN_DeactivateMouse( Cvar_VariableIntegerValue( "r_fullscreen" ) != 0, qtrue );
	mouseAvailable = qfalse;

	IN_ShutdownJoystick( );

	SDL_window = NULL;
}

/*
===============
IN_Restart
===============
*/
void IN_Restart( void )
{
	IN_ShutdownJoystick( );
	IN_Init( SDL_window );
}
