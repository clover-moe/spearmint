               ,-----------------------------------------------.
               |                                               |
            _________                                 _        __
           /   _____/_____  ____   __ _______  _____ (_) _____/  |
           \_____  \ |  _ \/ __ \ /  \\_  __ \/     \| |/    \   __\
           /        \| |_)   ___// () \|  | '/  | |  \ |  ||  \  |
          /_________/|  __/\____/__/\__\__|  |__|_|__/_|__||__/__|
               |     |_|                                       |
               `--- https://github.com/clover-moe/spearmint ---'

**Spearmint** has two main goals;

  1. provide a flexible engine for creating new games and mods.
  2. support features from (and running) various id Tech 3-based games.

Some of the major Spearmint features are:

  * Four player splitscreen
  * Moved a lot of code from server and client to Game and CGame VMs;
    * including console, chat input overlay, `usercmd_t` creation, and a lot of bot AI code.
  * Allow games to modify `entityState_t` and `playerState_t` without changing the engine.
  * Extended API for Game and CGame/UI, includes some of the features from;
    * RTCW, WolfET, Tremulous, Turtle Arena, and World of Padman
  * Merged UI VM into CGame VM, easier to modify as a whole / more flexible.
  * Additional shader keywords.

Some of the major ioquake3 features currently implemented are:

  * SDL 2 backend
  * OpenAL sound API support (multiple speaker support and better sound
    quality)
  * Full x86_64 support on Linux
  * VoIP support, both in-game and external support through Mumble.
  * MinGW compilation support on Windows and cross compilation support on Linux
  * AVI video capture of demos
  * Much improved console autocompletion
  * Persistent console history
  * Colorized terminal output
  * Optional Ogg Vorbis support
  * Much improved QVM tools
  * Support for various esoteric operating systems
  * cl_guid support
  * HTTP/FTP download redirection (using cURL)
  * Multiuser support on Windows systems (user specific game data
    is stored in "%APPDATA%\Spearmint")
  * PNG support
  * Web support via Emscripten
  * Many, many bug fixes

The map editor and associated compiling tools are not included. We suggest you
use a modern copy from http://icculus.org/gtkradiant/.

The original id software readme that accompanied the Q3 source release has been
renamed to id-readme.txt so as to prevent confusion. Please refer to the
website for updated status.

# Game Code

The Quake 3 game, cgame, and ui code and QVM compiler are not included. If you wish to play Quake 3, you'll need to get
and build https://github.com/clover-moe/mint-arena/ too.

# Engine Compilation and installation

For *nix
  1. Change to the directory containing this readme.
  2. Run 'make'.

For Windows
  1. Install mingw-w64
  2. Change to the directory containing this readme.
  3. Run 'make'.

For macOS, building a Universal Binary (macOS 10.5 to 10.8, x86_64, x86, ppc)
  1. Install MacOSX SDK packages from XCode.  For maximum compatibility,
     install MacOSX10.5.sdk and MacOSX10.6.sdk.
  2. Change to the directory containing this README file.
  3. Run './make-macosx-ub.sh'
  4. Copy the resulting ioquake3.app in /build/release-darwin-universal
     to your /Applications/ioquake3 folder.

For macOS, building a Universal Binary 2 (macOS 10.9+, arm64, x86_64)
  1. Install MacOSX SDK packages from XCode.  Building for arm64 requires
     MacOSX11.sdk or later.
  2. Change to the directory containing this README file.
  3. Run './make-macosx-ub2.sh'
  4. Copy the resulting ioquake3.app in /build/release-darwin-universal2
     to your /Applications/ioquake3 folder.

For Web, building with Emscripten
  1. Follow the installation instructions for the Emscripten SDK including
     setting up the environment with emsdk_env.
  2. Run `emmake make debug` (or release).
  3. Copy or symlink your baseq3 pk3 files into the `build/debug-emscripten-wasm32/baseq3`
     directory so they can be loaded at run-time. Only game files listed in
     `client-config.json` will be loaded.
  4. Start a web server serving this directory. `python3 -m http.server`
     is an easy default that you may already have installed.
  5. Open `http://localhost:8000/build/debug-emscripten-wasm32/ioquake3.html`
     in a web browser. Open the developer console to see errors and warnings.
  6. Debugging the C code is possible using a Chrome extension. For details
     see https://developer.chrome.com/blog/wasm-debugging-2020

Installation, for *nix
  1. Set the COPYDIR variable in the shell to be where you installed Quake 3
     to. By default it will be /usr/local/games/quake3 if you haven't set it.
     This is the path as used by the original Linux Q3 installer and subsequent
     point releases.
  2. Run 'make copyfiles'.

It is also possible to cross compile for Windows under *nix using MinGW. Your
distribution may have mingw32 packages available. On debian/Ubuntu, you need to
install 'mingw-w64'. Thereafter cross compiling is simply a case running
'PLATFORM=mingw32 ARCH=x86 make' in place of 'make'. ARCH may also be set to
x86_64.

The following variables may be set, either on the command line or in
Makefile.local:

```
  DEPEND_MAKEFILE      - set to 0 to disable rebuilding all targets when
                         the Makefile or Makefile.local is changed
  CFLAGS               - use this for custom CFLAGS
  V                    - set to show cc command line when building
  DEFAULT_BASEDIR      - extra path to search for baseq3 and such
  BUILD_SERVER         - build the 'spearmint-server' server binary
  BUILD_CLIENT         - build the 'spearmint' client binary
  SERVERBIN            - rename 'spearmint-server' server binary
  CLIENTBIN            - rename 'spearmint' client binary
  RENDERER_PREFIX      - rename 'renderer_' renderer binaries
  USE_RENDERER_DLOPEN  - build and use the renderer in a library
  BUILD_RENDERER_OPENGL1 build the opengl1 client / renderer library
  BUILD_RENDERER_OPENGL2 build the opengl2 client / renderer library
  USE_OPENAL           - use OpenAL where available
  USE_OPENAL_DLOPEN    - link with OpenAL at runtime
  USE_CURL             - use libcurl for http/ftp download support
  USE_CURL_DLOPEN      - link with libcurl at runtime
  USE_CODEC_MP3        - enable MP3 support
  USE_CODEC_VORBIS     - enable Ogg Vorbis support
  USE_CODEC_OPUS       - enable Ogg Opus support
  USE_MUMBLE           - enable Mumble support
  USE_VOIP             - enable built-in VoIP support
  USE_FREETYPE         - enable FreeType support for rendering fonts
  USE_INTERNAL_LIBS    - build internal libraries instead of dynamically
                         linking against system libraries; this just sets
                         the default for USE_INTERNAL_ZLIB etc.
                         and USE_LOCAL_HEADERS
  USE_INTERNAL_FREETYPE - build and link against internal FreeType library
  USE_INTERNAL_ZLIB    - build and link against internal zlib
  USE_INTERNAL_JPEG    - build and link against internal JPEG library
  USE_INTERNAL_OGG     - build and link against internal ogg library
  USE_INTERNAL_OPUS    - build and link against internal opus/opusfile libraries
  USE_INTERNAL_VORBIS  - build and link against internal Vorbis library
  USE_LOCAL_HEADERS    - use headers local to ioq3 instead of system ones
  DEBUG_CFLAGS         - C compiler flags to use for building debug version
  COPYDIR              - the target installation directory
  TEMPDIR              - specify user defined directory for temp files
  EMSCRIPTEN_PRELOAD_FILE - set to 1 to package 'baseq3' (BASEGAME) directory
                            containing pk3s and loose files as a single
                            .data file that is loaded instead of listing
                            individual files in client-config.json
```

The defaults for these variables differ depending on the target platform.


# OpenGL ES support

The opengl2 renderer (the default) supports OpenGL ES 2+. Though there
are many missing features and the performance may not be sufficient for
embedded System-on-a-Chip and mobile platforms.

The opengl1 renderer does not have OpenGL ES support.

The opengl2 renderer will try both OpenGL and OpenGL ES APIs to find one that
works. The `r_preferOpenGLES` cvar controls which API to try first.
Set it to -1 for auto (default), 0 for OpenGL, and 1 for OpenGL ES. It should be
set using command line arguments:

    spearmint +set cl_renderer opengl2 +set r_preferOpenGLES 1


# Console

## New cvars

```
  cl_autoRecordDemo                 - record a new demo on each map change
  cl_aviFrameRate                   - the framerate to use when capturing video
  cl_aviMotionJpeg                  - use the mjpeg codec when capturing video
  cl_guidServerUniq                 - makes cl_guid unique for each server
  cl_cURLLib                        - filename of cURL library to load
  cl_consoleKeys                    - space delimited list of key names or
                                      characters that toggle the console
  cl_mouseAccelStyle                - Set to 1 for QuakeLive mouse acceleration
                                      behaviour, 0 for standard q3
  cl_mouseAccelOffset               - Tuning the acceleration curve, see below

  s_useOpenAL                       - use the OpenAL sound backend if available
  s_alPrecache                      - cache OpenAL sounds before use
  s_alGain                          - the value of AL_GAIN for each source
  s_alSources                       - the total number of sources (memory) to
                                      allocate
  s_alDopplerFactor                 - the value passed to alDopplerFactor
  s_alDopplerSpeed                  - the value passed to alDopplerVelocity
  s_alMinDistance                   - the value of AL_REFERENCE_DISTANCE for
                                      each source
  s_alMaxDistance                   - the maximum distance before sounds start
                                      to become inaudible.
  s_alRolloff                       - the value of AL_ROLLOFF_FACTOR for each
                                      source
  s_alGraceDistance                 - after having passed MaxDistance, length
                                      until sounds are completely inaudible
  s_alDriver                        - which OpenAL library to use
  s_alDevice                        - which OpenAL device to use
  s_alAvailableDevices              - list of available OpenAL devices
  s_alInputDevice                   - which OpenAL input device to use
  s_alAvailableInputDevices         - list of available OpenAL input devices
  s_sdlBits                         - SDL bit resolution
  s_sdlSpeed                        - SDL sample rate
  s_sdlChannels                     - SDL number of channels
  s_sdlDevSamps                     - SDL DMA buffer size override
  s_sdlMixSamps                     - SDL mix buffer size override
  s_backend                         - read only, indicates the current sound
                                      backend
  s_muteWhenMinimized               - mute sound when minimized
  s_muteWhenUnfocused               - mute sound when window is unfocused
  sv_dlRate                         - bandwidth allotted to PK3 file downloads
                                      via UDP, in kbyte/s

  com_ansiColor                     - enable use of ANSI escape codes in the tty
  com_altivec                       - enable use of altivec on PowerPC systems
  com_homepath                      - Specify name that is to be appended to the
                                      home path
  com_legacyprotocol                - Specify protocol version number for
                                      legacy Quake3 1.32c protocol, see
                                      "Network protocols" section below
                                      (startup only)
  com_maxfpsUnfocused               - Maximum frames per second when unfocused
  com_maxfpsMinimized               - Maximum frames per second when minimized
  com_busyWait                      - Will use a busy loop to wait for rendering
                                      next frame when set to non-zero value
  com_pipefile                      - Specify filename to create a named pipe
                                      through which other processes can control
                                      the server while it is running.
                                      Nonfunctional on Windows.
  com_gamename                      - Gamename sent to master server in
                                      getservers[Ext] query and infoResponse
                                      "gamename" infostring value. Also used
                                      for filtering local network games.
  com_protocol                      - Specify protocol version number for
                                      current ioquake3 protocol, see
                                      "Network protocols" section below
                                      (startup only)

  in_joystickNo                     - select which joystick to use
  in_availableJoysticks             - list of available Joysticks
  in_keyboardDebug                  - print keyboard debug info

  sv_dlURL                          - the base of the HTTP or FTP site that
                                      holds custom pk3 files for your server
  sv_banFile                        - Name of the file that is used for storing
                                      the server bans
  sv_public                         - controls infomation sent to master server
                                      and game server browsers
                                       1: send info to masters and game browsers
                                       0: send info to game browsers
                                      -1: don't send any info
                                      -2: don't send any info or allow joining

  net_ip6                           - IPv6 address to bind to
  net_port6                         - port to bind to using the ipv6 address
  net_enabled                       - enable networking, bitmask. Add up
                                      number for option to enable it:
                                      enable ipv4 networking:    1
                                      enable ipv6 networking:    2
                                      prioritise ipv6 over ipv4: 4
                                      disable multicast support: 8
  net_mcast6addr                    - multicast address to use for scanning for
                                      ipv6 servers on the local network
  net_mcastiface                    - outgoing interface to use for scan

  r_allowResize                     - make window resizable
  r_ext_texture_filter_anisotropic  - anisotropic texture filtering
  r_zProj                           - distance of observer camera to projection
                                      plane in quake3 standard units
  r_greyscale                       - desaturate textures, useful for anaglyph,
                                      supports values in the range of 0 to 1
  r_stereoEnabled                   - enable stereo rendering for techniques
                                      like shutter glasses (untested)
  r_anaglyphMode                    - Enable rendering of anaglyph images
                                      red-cyan glasses:    1
                                      red-blue:            2
                                      red-green:           3
                                      green-magenta:       4
                                      To swap the colors for left and right eye
                                      just add 4 to the value for the wanted
                                      color combination. For red-blue and
                                      red-green you probably want to enable
                                      r_greyscale
  r_stereoSeparation                - Control eye separation. Resulting
                                      separation is r_zProj divided by this
                                      value in quake3 standard units.
                                      See also
                                      http://wiki.ioquake3.org/Stereo_Rendering
                                      for more information
  r_marksOnTriangleMeshes           - Support impact marks on md3 models, MOD
                                      developers should increase the mark
                                      triangle limits in cg_marks.c if they
                                      intend to use this.
  r_sdlDriver                       - read only, indicates the SDL driver
                                      backend being used
  r_noborder                        - Remove window decoration from window
                                      managers, like borders and titlebar.
  r_screenshotJpegQuality           - Controls quality of jpeg screenshots
                                      captured using screenshotJPEG
  r_aviMotionJpegQuality            - Controls quality of video capture when
                                      cl_aviMotionJpeg is enabled
  r_mode -2                         - This new video mode automatically uses the
                                      desktop resolution.
```

New in Spearmint
```
  r_zfar                            - sets z far for testing global fog
                                      distances, requires cheats to be enabled
  r_forceSunScale                   - override sunShader scale when more than 0

  con_autoclear                     - causes the console input to be cleared
                                      when opening/closing the console (this is
                                      the default behavior in Quake 3, RTCW, and
                                      ET)
  con_autochat                      - causes console input without a slash at
                                      the to beginning to be sent as chat
                                      messages

  cg_atmosphericEffects             - 0.0 to 1.0 scales the amount of rain and
                                      snow. set to 0 to disable rain/snow.
  cg_teamDmLeadAnnouncements        - Set to 0 to disable the team lead change
                                      announcements in Team Deathmatch (quake3
                                      1.32 had no lead announcements in TDM).
  cg_consoleLatency                 - controls how long messages stay in cgame
                                      notify area in miliseconds (default 3000)
  cg_drawShaderInfo                 - draw world shader name/image on HUD that
                                      that player's crosshair is pointing at
                                      (must enable cm_betterSurfaceNums!)
  cg_fovAspectAdjust                - apply fov correction for non-4:3 viewports
                                      it's especially useful for splitscreen
  cg_fadeExplosions                 - fade out explosions instead of
                                      shrinking radius
  cg_coronas                        - 0: disable drawing corona entities
                                      1: draw coronas if in front of camera
                                         and within dist set by cg_coronafardist
                                      2: only check if solid object between
                                         corona and camera
                                      3: just pass to renderer, may draw through
                                         walls
  cg_coronafardist                  - max draw distance for coronas, if
                                      cg_coronas is 1

  vm_minQvmHunkKB                   - minimum memory pool size for each QVM's
                                      trap_Alloc. DLLs use shared memory pool
                                      and are limited by com_hunkMegs.

  cm_betterSurfaceNums              - try to reconnect collision and draw
                                      surface numbers. must be enabled for
                                      trap_R_GetSurfaceShader and
                                      trap_R_SetSurfaceShader to work correctly

  r_shaderlod                       - used for if-endifs in shaders
                                      "if shaderlod 0.45" .. "endif"
                                       check if r_shaderlod > 0.45
  r_forceWindowIcon32               - use low resolution 32x32 window icon,
                                      windowicon32.ext instead of windowicon.ext

  g_playerCapsule                   - use capsule instead of box for player
                                      collision. each player has it set at spawn
                                      allowing mix of capsule and box in game

  ui_stretch                        - stretch menus to fill wide/narrow screens

  bot_report                        - show bot info on HUD when following bot
                                      in spectator mode
  bot_shownodechanges               - show bot AI node state changes, useful
                                      for debugging bot behavior
  bot_showteamgoals                 - show bot team goal changes, useful for
                                      debugging bot teamplay behavior
```

## New commands

```
  video [filename]        - start video capture (use with demo command)
  stopvideo               - stop video capture
  stopmusic               - stop background music
  minimize                - Minimize the game and show desktop
  togglemenu              - causes escape key event for opening/closing menu, or
                            going to a previous menu. works in binds, even in UI

  print                   - print out the contents of a cvar
  unset                   - unset a user created cvar

  banaddr <range>         - ban an ip address range from joining a game on this
                            server, valid <range> is either playernum or CIDR
                            notation address range.
  exceptaddr <range>      - exempt an ip address range from a ban.
  bandel <range>          - delete ban (either range or ban number)
  exceptdel <range>       - delete exception (either range or exception number)
  listbans                - list all currently active bans and exceptions
  rehashbans              - reload the banlist from serverbans.dat
  flushbans               - delete all bans

  net_restart             - restart network subsystem to change latched settings
  game_restart <fs_game>  - Switch to another mod

  which <filename/path>   - print out the path on disk to a loaded item

  execq <filename>        - quiet exec command, doesn't print "execing file.cfg"

  kicknum <client number> - kick a client off the server using client number
  kickall                 - kick all clients off the server
  kickbots                - kick all bots off the server

  tell <client num> <msg> - send message to a single client (new to server)

  cvar_modified [filter]  - list modified cvars, can filter results (such as "r*"
                            for renderer cvars) like cvarlist which lists all cvars

  addbot random           - the bot name "random" now selects a random bot
```

New in Spearmint
```
  levelshot <size>        - spearmint allows setting levelshot size, example
                            `levelshot 512'. defaults to 128.

  botreport               - shows what bots are doing, must set
                            bot_report 1
```

## Removed cvars
```
  r_customPixelAspect     - it had no effect
  com_basegame            - replaced manually setting basegame dirs with loading
                            them from mint-game.settings
  fs_basegame             - see above
  vm_ui                   - ui VM has been merged into cgame VM
```

## Removed commands
```
  clientkick <clientkick> - replaced with kicknum command
  kick all                - replaced with kickall command
  kick allbots            - replaced with kickbots command
```


# README for Users

## Using shared libraries instead of qvm

To force Spearmint to use shared libraries instead of qvms run it with the following
parameters: `+set sv_pure 0 +set vm_cgame 0 +set vm_game 0`

## Using Demo Data Files

Copy demoq3/pak0.pk3 from the demo installer to your baseq3 directory. The
qvm files in this pak0.pk3 will not work, so you have to use the native
shared libraries or qvms from this project. To use the new qvms, they must be
put into a pk3 file. A pk3 file is just a zip file, so any compression tool
that can create such files will work. The shared libraries should already be
in the correct place. Use the instructions above to use them.

Please bear in mind that you will not be able to play online using the demo
data, nor is it something that we like to spend much time maintaining or
supporting.

## Help! Ioquake3 won't give me an fps of X anymore when setting com_maxfps!

Ioquake3 now uses the select() system call to wait for the rendering of the
next frame when com_maxfps was hit. This will improve your CPU load
considerably in these cases. However, not all systems may support a
granularity for its timing functions that is required to perform this waiting
correctly. For instance, ioquake3 tells select() to wait 2 milliseconds, but
really it can only wait for a multiple of 5ms, i.e. 5, 10, 15, 20... ms.
In this case you can always revert back to the old behaviour by setting the
cvar com_busyWait to 1.

## Using HTTP/FTP Download Support (Server)

You can enable redirected downloads on your server even if it's not
an ioquake3 server.  You simply need to use the 'sets' command to put
the sv_dlURL cvar into your SERVERINFO string and ensure sv_allowDownloads
is set to 1

sv_dlURL is the base of the URL that contains your custom .pk3 files
the client will append both fs_game and the filename to the end of
this value.  For example, if you have sv_dlURL set to
`"http://ioquake3.org"`, fs_game is `"baseq3"`, and the client is
missing `"test.pk3"`, it will attempt to download from the URL
`"http://ioquake3.org/baseq3/test.pk3"`

sv_allowDownload's value is now a bitmask made up of the following
flags:

  * 1 - ENABLE
  * 4 - do not use UDP downloads
  * 8 - do not ask the client to disconnect when using HTTP/FTP

Server operators who are concerned about potential "leeching" from their
HTTP servers from other ioquake3 servers can make use of the HTTP_REFERER
that ioquake3 sets which is `"ioQ3://{SERVER_IP}:{SERVER_PORT}"`.  For,
example, Apache's mod_rewrite can restrict access based on HTTP_REFERER.

On a sidenote, downloading via UDP has been improved and yields higher data
rates now. You can configure the maximum bandwidth for UDP downloads via the
cvar sv_dlRate. Due to system-specific limits the download rate is capped
at about 1 Mbyte/s per client, so curl downloading may still be faster.

## Using HTTP/FTP Download Support (Client)

Simply setting cl_allowDownload to 1 will enable HTTP/FTP downloads
assuming ioquake3 was compiled with USE_CURL=1 (the default).
like sv_allowDownload, cl_allowDownload also uses a bitmask value
supporting the following flags:

  * 1 - ENABLE
  * 2 - do not use HTTP/FTP downloads
  * 4 - do not use UDP downloads

When ioquake3 is built with USE_CURL_DLOPEN=1 (default on some platforms),
it will use the value of the cvar cl_cURLLib as the filename of the cURL
library to dynamically load.

## Multiuser Support on Windows systems
On Windows, all user specific files such as autogenerated configuration,
demos, videos, screenshots, and autodownloaded pk3s are now saved in a
directory specific to the user who is running ioquake3.

On NT-based such as Windows XP, this is usually a directory named:

    C:\Documents and Settings\%USERNAME%\Application Data\Spearmint\

Windows 95, Windows 98, and Windows ME will use a directory like:

    C:\Windows\Application Data\Spearmint

in single-user mode, or:

    C:\Windows\Profiles\%USERNAME%\Application Data\Spearmint

if multiple logins have been enabled.

In order to access this directory more easily, the installer may create a
Shortcut which has its target set to:

    %APPDATA%\Spearmint\

This Shortcut would work for all users on the system regardless of the
locale settings.  Unfortunately, this environment variable is only
present on Windows NT based systems.

You can revert to the old single-user behaviour by setting the fs_homepath
cvar to the directory where Spearmint is installed.  For example:

    spearmint.exe +set fs_homepath "c:\spearmint"

Note that this cvar MUST be set as a command line parameter.

## SDL Keyboard Differences

ioquake3 clients have different keyboard behaviour compared to the original
Quake3 clients.

  * SDL > 1.2.9 does not support disabling dead key recognition. In order to
      send dead key characters (e.g. ~, ', `, and ^), you must key a Space (or
      sometimes the same character again) after the character to send it on
      many international keyboard layouts.

  * The SDL client supports many more keys than the original Quake3 client.
      For example the keys: "Windows", "SysReq", "ScrollLock", and "Break".
      For non-US keyboards, all of the so called "World" keys are now supported
      as well as F13, F14, F15, and the country-specific mode/meta keys.

On many international layouts the default console toggle keys are also dead
keys, meaning that dropping the console potentially results in
unintentionally initiating the keying of a dead key. Furthermore SDL 1.2's
dead key support is broken by design and Q3 doesn't support non-ASCII text
entry, so the chances are you won't get the correct character anyway.

If you use such a keyboard layout, you can set the cvar cl_consoleKeys. This
is a space delimited list of key names that will toggle the console. The key
names are the usual Q3 names e.g. "~", "`", "c", "BACKSPACE", "PAUSE",
"WINDOWS" etc. It's also possible to use ASCII characters, by hexadecimal
number. Some example values for cl_consoleKeys:

    "~ ` 0x7e 0x60"           Toggle on ~ or ` (the default)
    "WINDOWS"                 Toggle on the Windows key
    "c"                       Toggle on the c key
    "0x43"                    Toggle on the C character (Shift-c)
    "PAUSE F1 PGUP"           Toggle on the Pause, F1 or Page Up keys

Note that when you elect a set of console keys or characters, they cannot
then be used for binding, nor will they generate characters when entering
text. Also, in addition to the nominated console keys, Shift-ESC is hard
coded to always toggle the console.

## QuakeLive mouse acceleration
(patch and this text written by TTimo from id)

I've been using an experimental mouse acceleration code for a while, and
decided to make it available to everyone. Don't be too worried if you don't
understand the explanations below, this is mostly intended for advanced
players:
To enable it, set cl_mouseAccelStyle 1 (0 is the default/legacy behavior)

New style is controlled with 3 cvars:

sensitivity
cl_mouseAccel
cl_mouseAccelOffset

The old code (cl_mouseAccelStyle 0) can be difficult to calibrate because if
you have a base sensitivity setup, as soon as you set a non zero acceleration
your base sensitivity at low speeds will change as well. The other problem
with style 0 is that you are stuck on a square (power of two) acceleration
curve.

The new code tries to solve both problems:

Once you setup your sensitivity to feel comfortable and accurate enough for
low mouse deltas with no acceleration (cl_mouseAccel 0), you can start
increasing cl_mouseAccel and tweaking cl_mouseAccelOffset to get the
amplification you want for high deltas with little effect on low mouse deltas.

cl_mouseAccel is a power value. Should be >= 1, 2 will be the same power curve
as style 0. The higher the value, the faster the amplification grows with the
mouse delta.

cl_mouseAccelOffset sets how much base mouse delta will be doubled by
acceleration. The closer to zero you bring it, the more acceleration will
happen at low speeds. This is also very useful if you are changing to a new
mouse with higher dpi, if you go from 500 to 1000 dpi, you can divide your
cl_mouseAccelOffset by two to keep the same overall 'feel' (you will likely
gain in precision when you do that, but that is not related to mouse
acceleration).

Mouse acceleration is tricky to configure, and when you do you'll have to
re-learn your aiming. But you will find that it's very much forth it in the
long run.

If you try the new acceleration code and start using it, I'd be very
interested by your feedback.


# README for Developers

## pk3dir

Spearmint has a useful new feature for mappers. Paths in a game directory with
the extension ".pk3dir" are treated like pk3 files. This means you can keep
all files specific to your map in one directory tree and easily zip this
folder for distribution.

## Creating standalone games

Have you finished the daunting task of removing all dependencies on the Q3
game data? You probably now want to give your users the opportunity to play
the game without owning a copy of Q3.

However, before you start compiling your own version of Spearmint, you have to
ask yourself: Have we changed or will we need to change anything of importance
in the engine? If your answer to this question is "no", you should probably
just use Spearmint.

If you really changed parts that would make vanilla Spearmint incompatible with
your mod, we have included another way to conveniently build a stand-alone
binary. Just run make. Don't forget to edit the PRODUCT_NAME and subsequent
 #defines in qcommon/q_shared.h with information appropriate for your project.

## Standalone game licensing

While a lot of work has been put into Spearmint that you can benefit from free
of charge, it does not mean that you have no obligations to fulfill. Please be
aware that as soon as you start distributing your game with an engine based on
our sources we expect you to fully comply with the requirements as stated in
the GPL. That includes making sources and modifications you made to the
Spearmint engine as well as the game-code used to compile the .qvm files for
the game logic freely available to everyone. Furthermore, note that the "QIIIA
Game Source License" prohibits distribution of mods that are intended to
operate on a version of Q3 not sanctioned by id software:

    "with this Agreement, ID grants to you the non-exclusive and limited right
    to distribute copies of the Software ... for operation only with the full
    version of the software game QUAKE III ARENA"

This means that if you're creating a standalone game, you cannot use said
license on any portion of the product. As the only other license this code has
been released under is the GPL, this is the only option.

This does NOT mean that you cannot market this game commercially. The GPL does
not prohibit commercial exploitation and all assets (e.g. textures, sounds,
maps) created by yourself are your property and can be sold like every other
game you find in stores.

## cl_guid Support

cl_guid is a cvar which is part of the client's USERINFO string.  Its value
is a 32 character string made up of [a-f] and [0-9] characters.  This
value is pseudo-unique for every player.  Id's Quake 3 Arena client also
sets cl_guid, but only if Punkbuster is enabled on the client.

If cl_guidServerUniq is non-zero (the default), then this value is also
pseudo-unique for each server a client connects to (based on IP:PORT of
the server).

The purpose of cl_guid is to add an identifier for each player on
a server.  This value can be reset by the client at any time so it's not
useful for blocking access.  However, it can have at least two uses in
your mod's game code:

  1. improve logging to allow statistical tools to index players by more
       than just name
  2. granting some weak admin rights to players without requiring passwords

## PNG support

ioquake3 supports the use of PNG (Portable Network Graphic) images as
textures. It should be noted that the use of such images in a map will
result in missing placeholder textures where the map is used with the id
Quake 3 client or earlier versions of ioquake3.

Recent versions of GtkRadiant and q3map2 support PNG images without
modification. However GtkRadiant is not aware that PNG textures are supported
by ioquake3. To change this behaviour open the file 'q3.game' in the 'games'
directory of the GtkRadiant base directory with an editor and change the
line:

    texturetypes="tga jpg"

to

    texturetypes="tga jpg png"

Restart GtkRadiant and PNG textures are now available.

## Building with MinGW for pre Windows XP

IPv6 support requires a header named "wspiapi.h" to abstract away from
differences in earlier versions of Windows' IPv6 stack. There is no MinGW
equivalent of this header and the Microsoft version is obviously not
redistributable, so in its absence we're forced to require Windows XP.
However if this header is acquired separately and placed in the qcommon/
directory, this restriction is lifted.


# Contributing

Please submit all patches as a GitHub pull request.

The focus for Spearmint is to develop a stable base suitable for further
development and provide players with the same Quake 3 game play experience
they've had for years.


# Credits

Spearmint Maintainers

  * Zack Middleton <zturtleman@gmail.com>

ioquake3 Maintainers

  * James Canete <use.less01@gmail.com>
  * Ludwig Nussel <ludwig.nussel@suse.de>
  * Thilo Schulz <arny@ats.s.bawue.de>
  * Tim Angus <tim@ngus.net>
  * Tony J. White <tjw@tjw.org>
  * Jack Slater <jack@ioquake.org>
  * Zack Middleton <zturtleman@gmail.com>

Significant contributions from

  * Ryan C. Gordon <icculus@icculus.org>
  * Andreas Kohn <andreas@syndrom23.de>
  * Joerg Dietrich <Dietrich_Joerg@t-online.de>
  * Stuart Dalton <badcdev@gmail.com>
  * Vincent S. Cojot <vincent at cojot dot name>
  * optical <alex@rigbo.se>
  * Aaron Gyes <floam@aaron.gy>


