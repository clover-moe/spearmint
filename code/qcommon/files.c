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
 * name:		files.c
 *
 * desc:		handle based filesystem for Quake III Arena 
 *
 * $Archive: /MissionPack/code/qcommon/files.c $
 *
 *****************************************************************************/


#include "q_shared.h"
#include "qcommon.h"
#include "unzip.h"

/*
=============================================================================

QUAKE3 FILESYSTEM

All of Quake's data access is through a hierarchical file system, but the contents of 
the file system can be transparently merged from several sources.

A "qpath" is a reference to game file data.  MAX_ZPATH is 256 characters, which must include
a terminating zero. "..", "\\", and ":" are explicitly illegal in qpaths to prevent any
references outside the quake directory system.

The "base path" is the path to the directory holding all the game directories and usually
the executable.  It defaults to ".", but can be overridden with a "+set fs_basepath c:\quake3"
command line to allow code debugging in a different directory.  Basepath cannot
be modified at all after startup.  Any files that are created (demos, screenshots,
etc) will be created relative to the base path, so base path should usually be writable.

The "home path" is the path used for all write access. On win32 systems we have "base path"
== "home path", but on *nix systems the base installation is usually readonly, and
"home path" points to ~/.q3a or similar

The user can also install custom mods and content in "home path", so it should be searched
along with "home path" and "cd path" for game content.


The "base game" is the directory under the paths where data comes from by default, and
can be either "baseq3" or "demoq3".

The "current game" may be the same as the base game, or it may be the name of another
directory under the paths that should be searched for files before looking in the base game.
This is the basis for addons.

Clients automatically set the game directory after receiving a gamestate from a server,
so only servers need to worry about +set fs_game.

No other directories outside of the base game and current game will ever be referenced by
filesystem functions.

To save disk space and speed loading, directory trees can be collapsed into zip files.
The files use a ".pk3" extension to prevent users from unzipping them accidentally, but
otherwise the are simply normal uncompressed zip files.  A game directory can have multiple
zip files of the form "pak0.pk3", "pak1.pk3", etc.  Zip files are searched in decending order
from the highest number to the lowest, and will always take precedence over the filesystem.
This allows a pk3 distributed as a patch to override all existing data.

Because we will have updated executables freely available online, there is no point to
trying to restrict demo / oem versions of the game with code changes.  Demo / oem versions
should be exactly the same executables as release versions, but with different data that
automatically restricts where game media can come from to prevent add-ons from working.

File search order: when FS_FOpenFileRead gets called it will go through the fs_searchpaths
structure and stop on the first successful hit. fs_searchpaths is built with successive
calls to FS_AddGameDirectory

Additionally, we search in several subdirectories:
current game is the current mode
base game is a variable to allow mods based on other mods
(such as baseq3 + missionpack content combination in a mod for instance)
BASEGAME is the hardcoded base game ("baseq3")

e.g. the qpath "sound/newstuff/test.wav" would be searched for in the following places:

home path + current game's zip files
home path + current game's directory
base path + current game's zip files
base path + current game's directory
cd path + current game's zip files
cd path + current game's directory

home path + base game's zip file
home path + base game's directory
base path + base game's zip file
base path + base game's directory
cd path + base game's zip file
cd path + base game's directory

home path + BASEGAME's zip file
home path + BASEGAME's directory
base path + BASEGAME's zip file
base path + BASEGAME's directory
cd path + BASEGAME's zip file
cd path + BASEGAME's directory

server download, to be written to home path + current game's directory


The filesystem can be safely shutdown and reinitialized with different
basedir / cddir / game combinations, but all other subsystems that rely on it
(sound, video) must also be forced to restart.

Because the same files are loaded by both the clip model (CM_) and renderer (TR_)
subsystems, a simple single-file caching scheme is used.  The CM_ subsystems will
load the file with a request to cache.  Only one file will be kept cached at a time,
so any models that are going to be referenced by both subsystems should alternate
between the CM_ load function and the ref load function.

TODO: A qpath that starts with a leading slash will always refer to the base game, even if another
game is currently active.  This allows character models, skins, and sounds to be downloaded
to a common directory no matter which game is active.

How to prevent downloading zip files?
Pass pk3 file names in systeminfo, and download before FS_Restart()?

Aborting a download disconnects the client from the server.

How to mark files as downloadable?  Commercial add-ons won't be downloadable.

Non-commercial downloads will want to download the entire zip file.
the game would have to be reset to actually read the zip in

Auto-update information

Path separators

Casing

  separate server gamedir and client gamedir, so if the user starts
  a local game after having connected to a network game, it won't stick
  with the network game.

  allow menu options for game selection?

Read / write config to floppy option.

Different version coexistance?

When building a pak file, make sure a q3config.cfg isn't present in it,
or configs will never get loaded from disk!

  todo:

  downloading (outside fs?)
  game directory passing and restarting

=============================================================================

*/

gameConfig_t com_gameConfig;

// every time a new pk3 file is built, this checksum must be updated.
// the easiest way to get it is to just run the game and see what it spits out

#define MAX_PAKSUMS 64

typedef struct {
	char gamename[MAX_QPATH];
	char pakname[MAX_QPATH];
	unsigned checksum;
	qboolean nodownload;
	qboolean optional;
} purePak_t;

purePak_t com_purePaks[MAX_PAKSUMS];

qboolean	fs_foundPaksums;
int			fs_numPaksums;

// if this is defined, the executable positively won't work with any paks other
// than the demo pak, even if productid is present.  This is only used for our
// last demo release to prevent the mac and linux users from using the demo
// executable with the production windows pak before the mac/linux products
// hit the shelves a little later
// NOW defined in build files
//#define PRE_RELEASE_TADEMO

#define MAX_ZPATH			256
#define	MAX_SEARCH_PATHS	4096
#define MAX_FILEHASH_SIZE	1024

typedef struct fileInPack_s {
	char					*name;		// name of the file
	unsigned long			pos;		// file info position in zip
	unsigned long			len;		// uncompress file size
	struct	fileInPack_s*	next;		// next file in the hash
} fileInPack_t;

typedef struct {
        char			pakPathname[MAX_OSPATH];	// c:\quake3\baseq3
	char			pakFilename[MAX_OSPATH];	// c:\quake3\baseq3\pak0.pk3
	char			pakBasename[MAX_OSPATH];	// pak0
	char			pakGamename[MAX_OSPATH];	// baseq3
	unzFile			handle;						// handle to zip file
	int				checksum;					// checksum of the zip
	int				numfiles;					// number of files in pk3
	qboolean			referenced;					// is pk3 referenced?
	pakType_t		pakType;						// is it a commercial pak?
	int				hashSize;					// hash table size (power of 2)
	fileInPack_t*	*hashTable;					// hash table
	fileInPack_t*	buildBuffer;				// buffer with the filenames etc.
} pack_t;

typedef struct {
	char		fullpath[MAX_OSPATH];		// c:\quake3\baseq3
											// c:\quake3\baseq3\mypak.pk3dir
											// c:\quake3 (with qpath "fonts")
	char		gamedir[MAX_OSPATH];	// baseq3 or blank for fonts
	char		qpath[MAX_OSPATH];		// restrict looking for files that starts with path (i.e., "fonts")
	qboolean	virtualDir; // if true, don't treat as a base game directory because it's a .pk3dir or something.
} directory_t;

typedef struct searchpath_s {
	struct searchpath_s *next;

	pack_t		*pack;		// only one of pack / dir will be non NULL
	directory_t	*dir;
} searchpath_t;

static	char		fs_gamedir[MAX_OSPATH];	// this will be a single file name with no separators
static	cvar_t		*fs_debug;
static	cvar_t		*fs_homepath;
#ifdef __APPLE__
// Also search the .app bundle for .pk3 files
static	cvar_t		*fs_apppath;
#endif
static	cvar_t		*fs_basepath;
static	cvar_t		*fs_cdpath;
static	cvar_t		*fs_gamedirvar;
static	searchpath_t	*fs_searchpaths;
static	searchpath_t	*fs_stashedPath = NULL;
static	int			fs_readCount;			// total bytes read
static	int			fs_loadCount;			// total files read
static	int			fs_loadStack;			// total files in memory
static	int			fs_packFiles = 0;		// total number of files in packs

typedef union qfile_gus {
	FILE*		o;
	unzFile		z;
} qfile_gut;

typedef struct qfile_us {
	qfile_gut	file;
	qboolean	unique;
} qfile_ut;

typedef struct {
	qfile_ut	handleFiles;
	qboolean	handleSync;
	int			fileSize;
	int			zipFilePos;
	int			zipFileLen;
	qboolean	zipFile;
	char		name[MAX_ZPATH];
} fileHandleData_t;

static fileHandleData_t	fsh[MAX_FILE_HANDLES];

// TTimo - https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=540
// wether we did a reorder on the current search path when joining the server
static qboolean fs_reordered;

// never load anything from pk3 files that are not present at the server when pure
static int		fs_numServerPaks = 0;
static int		fs_serverPaks[MAX_SEARCH_PATHS];				// checksums
static char		*fs_serverPakNames[MAX_SEARCH_PATHS];			// pk3 names

// only used for autodownload, to make sure the client has at least
// all the pk3 files that are referenced at the server side
static int		fs_numServerReferencedPaks;
static int		fs_serverReferencedPaks[MAX_SEARCH_PATHS];			// checksums
static char		*fs_serverReferencedPakNames[MAX_SEARCH_PATHS];		// pk3 names

// last valid game folder used
char lastValidBase[MAX_OSPATH];
char lastValidGame[MAX_OSPATH];

#ifdef FS_MISSING
FILE*		missingFiles = NULL;
#endif

/* C99 defines __func__ */
#if __STDC_VERSION__ < 199901L
#  if __GNUC__ >= 2 || _MSC_VER >= 1300
#    define __func__ __FUNCTION__
#  else
#    define __func__ "(unknown)"
#  endif
#endif

struct {
	unsigned int checksum;
	char *gamename;
	char *basename;
} commercialPaks[] = {
	// quake3
	{ 1042450890u, "demoq3", "pak0" },
	{ 4204185745u, "baseq3", "pak0" },
	{ 4193093146u, "baseq3", "pak1" },
	{ 2353701282u, "baseq3", "pak2" },
	{ 3321918099u, "baseq3", "pak3" },
	{ 2809125413u, "baseq3", "pak4" },
	{ 1185311901u, "baseq3", "pak5" },
	{ 750524939u,  "baseq3", "pak6" },
	{ 2842227859u, "baseq3", "pak7" },
	{ 3662040954u, "baseq3", "pak8" },

	// team arena
	{ 1185930068u, "tademo", "pak0" },
	{ 946490770u, "missionpack", "pak0" },
	{ 1414087181u,"missionpack", "pak1" },
	{ 409244605u, "missionpack", "pak2" },
	{ 648653547u, "missionpack", "pak3" },

	// rtcw
	{ 1731729369u, "demomain", "pak0" },
	{ 1787286276u, "main", "pak0" },
	{ 825182904u, "main", "mp_pak0" },
	{ 1872082070u, "main", "mp_pak1" },
	{ 220365352u, "main", "mp_pak2" },
	{ 1130325916u, "main", "mp_pak3" },
	{ 3272074268u, "main", "mp_pak4" },
	{ 3805724433u, "main", "mp_pak5" },
	{ 2334312393u, "main", "sp_pak1" },
	{ 1078227215u, "main", "sp_pak2" },
	{ 2334312393u, "main", "sp_pak3" },
	{ 1078227215u, "main", "sp_pak4" },

	// et
	{ 1627565872u, "etmain", "pak0" },
	{ 1587932567u,"etmain", "pak1" },
	{ 3477493040u, "etmain", "pak2" },
};

int numCommercialPaks = ARRAY_LEN( commercialPaks );

/*
==============
FS_Initialized
==============
*/

qboolean FS_Initialized( void ) {
	return (fs_searchpaths != NULL);
}

/*
=================
FS_PakIsPure
=================
*/
qboolean FS_PakIsPure( pack_t *pack ) {
	int i;

	if ( fs_numServerPaks ) {
		for ( i = 0 ; i < fs_numServerPaks ; i++ ) {
			// FIXME: also use hashed file names
			// NOTE TTimo: a pk3 with same checksum but different name would be validated too
			//   I don't see this as allowing for any exploit, it would only happen if the client does manips of its file names 'not a bug'
			if ( pack->checksum == fs_serverPaks[i] ) {
				return qtrue;		// on the aproved list
			}
		}
		return qfalse;	// not on the pure server pak list
	}
	return qtrue;
}


/*
=================
FS_LoadStack
return load stack
=================
*/
int FS_LoadStack( void )
{
	return fs_loadStack;
}

/*
================
return a hash value for the filename
================
*/
static long FS_HashFileName( const char *fname, int hashSize ) {
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
	hash &= (hashSize-1);
	return hash;
}

static fileHandle_t	FS_HandleForFile(void) {
	int		i;

	for ( i = 1 ; i < MAX_FILE_HANDLES ; i++ ) {
		if ( fsh[i].handleFiles.file.o == NULL ) {
			return i;
		}
	}
	Com_Error( ERR_DROP, "FS_HandleForFile: none free" );
	return 0;
}

static FILE	*FS_FileForHandle( fileHandle_t f ) {
	if ( f < 1 || f >= MAX_FILE_HANDLES ) {
		Com_Error( ERR_DROP, "FS_FileForHandle: out of range" );
	}
	if (fsh[f].zipFile == qtrue) {
		Com_Error( ERR_DROP, "FS_FileForHandle: can't get FILE on zip file" );
	}
	if ( ! fsh[f].handleFiles.file.o ) {
		Com_Error( ERR_DROP, "FS_FileForHandle: NULL" );
	}
	
	return fsh[f].handleFiles.file.o;
}

void	FS_ForceFlush( fileHandle_t f ) {
	FILE *file;

	file = FS_FileForHandle(f);
	setvbuf( file, NULL, _IONBF, 0 );
}

/*
================
FS_fplength
================
*/

long FS_fplength(FILE *h)
{
	long		pos;
	long		end;

	pos = ftell(h);
	fseek(h, 0, SEEK_END);
	end = ftell(h);
	fseek(h, pos, SEEK_SET);

	return end;
}

/*
================
FS_filelength

If this is called on a non-unique FILE (from a pak file),
it will return the size of the pak file, not the expected
size of the file.
================
*/
long FS_filelength(fileHandle_t f)
{
	FILE	*h;

	h = FS_FileForHandle(f);
	
	if(h == NULL)
		return -1;
	else
		return FS_fplength(h);
}

/*
====================
FS_ReplaceSeparators

Fix things up differently for win/unix/mac
====================
*/
static void FS_ReplaceSeparators( char *path ) {
	char	*s;
	qboolean lastCharWasSep = qfalse;

	for ( s = path ; *s ; s++ ) {
		if ( *s == '/' || *s == '\\' ) {
			if ( !lastCharWasSep ) {
				*s = PATH_SEP;
				lastCharWasSep = qtrue;
			} else {
				memmove (s, s + 1, strlen (s));
			}
		} else {
			lastCharWasSep = qfalse;
		}
	}
}

/*
===================
FS_BuildOSPath

Qpath may have either forward or backwards slashes
===================
*/
char *FS_BuildOSPath( const char *base, const char *game, const char *qpath ) {
	char	temp[MAX_OSPATH];
	static char ospath[2][MAX_OSPATH];
	static int toggle;
	
	toggle ^= 1;		// flip-flop to allow two returns without clash

	if ( game ) {
#if 0
		// ZTM: required for vanilla q3 which has fs_game "" for basegame
		if( !game[0] ) {
			game = fs_gamedir;
		}
#endif

		Com_sprintf( temp, sizeof(temp), "/%s/%s", game, qpath );
	} else {
		Com_sprintf( temp, sizeof(temp), "/%s", qpath );
	}

	FS_ReplaceSeparators( temp );
	Com_sprintf( ospath[toggle], sizeof( ospath[0] ), "%s%s", base, temp );
	
	return ospath[toggle];
}


/*
============
FS_CreatePath

Creates any directories needed to store the given filename
============
*/
qboolean FS_CreatePath (char *OSPath) {
	char	*ofs;
	char	path[MAX_OSPATH];
	
	// make absolutely sure that it can't back up the path
	// FIXME: is c: allowed???
	if ( strstr( OSPath, ".." ) || strstr( OSPath, "::" ) ) {
		Com_Printf( "WARNING: refusing to create relative path \"%s\"\n", OSPath );
		return qtrue;
	}

	Q_strncpyz( path, OSPath, sizeof( path ) );
	FS_ReplaceSeparators( path );

	// Skip creation of the root directory as it will always be there
	ofs = strchr( path, PATH_SEP );
	if ( ofs != NULL ) {
		ofs++;
	}

	for (; ofs != NULL && *ofs ; ofs++) {
		if (*ofs == PATH_SEP) {
			// create the directory
			*ofs = 0;
			if (!Sys_Mkdir (path)) {
				Com_Error( ERR_FATAL, "FS_CreatePath: failed to create path \"%s\"",
					path );
			}
			*ofs = PATH_SEP;
		}
	}

	return qfalse;
}

/*
=================
FS_CheckFilenameIsMutable

ERR_FATAL if trying to maniuplate a file with the platform library, QVM, or pk3 extension
=================
 */
static void FS_CheckFilenameIsMutable( const char *filename,
		const char *function )
{
	// Check if the filename ends with the library, QVM, or pk3 extension
	if( Sys_DllExtension( filename )
		|| COM_CompareExtension( filename, ".qvm" )
		|| COM_CompareExtension( filename, ".pk3" ) )
	{
		Com_Error( ERR_FATAL, "%s: Not allowed to manipulate '%s' due "
			"to %s extension", function, filename, COM_GetExtension( filename ) );
	}
}

/*
===========
FS_Remove

===========
*/
int FS_Remove( const char *osPath ) {
	FS_CheckFilenameIsMutable( osPath, __func__ );

	return remove( osPath ) != -1;
}

/*
===========
FS_HomeRemove

===========
*/
int FS_HomeRemove( const char *homePath ) {
	FS_CheckFilenameIsMutable( homePath, __func__ );

	return remove( FS_BuildOSPath( fs_homepath->string,
			fs_gamedir, homePath ) ) != -1;
}

/*
================
FS_FileInPathExists

Tests if path and file exists
================
*/
qboolean FS_FileInPathExists(const char *testpath)
{
	FILE *filep;

	filep = Sys_FOpen(testpath, "rb");
	
	if(filep)
	{
		fclose(filep);
		return qtrue;
	}
	
	return qfalse;
}

/*
================
FS_FileExists

Tests if the file exists in the current gamedir, this DOES NOT
search the paths.  This is to determine if opening a file to write
(which always goes into the current gamedir) will cause any overwrites.
NOTE TTimo: this goes with FS_FOpenFileWrite for opening the file afterwards
================
*/
qboolean FS_FileExists(const char *file)
{
	return FS_FileInPathExists(FS_BuildOSPath(fs_homepath->string, fs_gamedir, file));
}

/*
================
FS_SV_FileExists

Tests if the file exists 
================
*/
qboolean FS_SV_FileExists( const char *file )
{
	char *testpath;

	testpath = FS_BuildOSPath( fs_homepath->string, NULL, file );

	return FS_FileInPathExists(testpath);
}

/*
================
FS_SV_RW_FileExists

check base path too
================
*/
qboolean FS_SV_RW_FileExists(const char *file)
{
	char *testpath;

	testpath = FS_BuildOSPath( fs_homepath->string, NULL, file );

	if ( FS_FileInPathExists(testpath) )
		return qtrue;

	testpath = FS_BuildOSPath( fs_basepath->string, NULL, file );

	return FS_FileInPathExists(testpath);
}

/*
===========
FS_SV_FOpenFileWrite

===========
*/
fileHandle_t FS_SV_FOpenFileWrite( const char *filename ) {
	char *ospath;
	fileHandle_t	f;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	ospath = FS_BuildOSPath( fs_homepath->string, NULL, filename );

	f = FS_HandleForFile();
	fsh[f].zipFile = qfalse;

	if ( fs_debug->integer ) {
		Com_Printf( "FS_SV_FOpenFileWrite: %s\n", ospath );
	}

	FS_CheckFilenameIsMutable( ospath, __func__ );

	if( FS_CreatePath( ospath ) ) {
		return 0;
	}

	Com_DPrintf( "writing to: %s\n", ospath );
	fsh[f].handleFiles.file.o = Sys_FOpen( ospath, "wb" );

	Q_strncpyz( fsh[f].name, filename, sizeof( fsh[f].name ) );

	fsh[f].handleSync = qfalse;
	if (!fsh[f].handleFiles.file.o) {
		f = 0;
	}
	return f;
}

/*
===========
FS_SV_FOpenFileRead

Search for a file somewhere below the home path then base path
in that order
===========
*/
long FS_SV_FOpenFileRead(const char *filename, fileHandle_t *fp)
{
	char *ospath;
	fileHandle_t	f = 0;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	f = FS_HandleForFile();
	fsh[f].zipFile = qfalse;

	Q_strncpyz( fsh[f].name, filename, sizeof( fsh[f].name ) );

	// don't let sound stutter
	S_ClearSoundBuffer();

	// search homepath
	ospath = FS_BuildOSPath( fs_homepath->string, NULL, filename );

	if ( fs_debug->integer ) {
		Com_Printf( "FS_SV_FOpenFileRead (fs_homepath): %s\n", ospath );
	}

	fsh[f].handleFiles.file.o = Sys_FOpen( ospath, "rb" );
	fsh[f].handleSync = qfalse;
	if (!fsh[f].handleFiles.file.o)
	{
		// If fs_homepath == fs_basepath, don't bother
		if (Q_stricmp(fs_homepath->string,fs_basepath->string))
		{
			// search basepath
			ospath = FS_BuildOSPath( fs_basepath->string, NULL, filename );

			if ( fs_debug->integer )
			{
				Com_Printf( "FS_SV_FOpenFileRead (fs_basepath): %s\n", ospath );
			}

			fsh[f].handleFiles.file.o = Sys_FOpen( ospath, "rb" );
			fsh[f].handleSync = qfalse;
		}

		if (!fsh[f].handleFiles.file.o && fs_cdpath->string[0])
		{
			// search cd path
			ospath = FS_BuildOSPath( fs_cdpath->string, NULL, filename );

			if ( fs_debug->integer )
			{
				Com_Printf( "FS_SV_FOpenFileRead (fs_cdpath): %s\n", ospath );
			}

			fsh[f].handleFiles.file.o = Sys_FOpen( ospath, "rb" );
			fsh[f].handleSync = qfalse;
		}

		if (!fsh[f].handleFiles.file.o && fs_cdpath->string[0])
		{
			// search cd path
			ospath = FS_BuildOSPath( fs_cdpath->string, filename, "" );
			ospath[strlen(ospath)-1] = '\0';

			if ( fs_debug->integer )
			{
				Com_Printf( "FS_SV_FOpenFileRead (fs_cdpath): %s\n", ospath );
			}

			fsh[f].handleFiles.file.o = Sys_FOpen( ospath, "rb" );
			fsh[f].handleSync = qfalse;
		}

		if ( !fsh[f].handleFiles.file.o )
		{
			f = 0;
		}
	}

	*fp = f;
	if (f) {
		return FS_filelength(f);
	}

	return -1;
}


/*
===========
FS_System_FOpenFileRead

Open a file using a full OS path
===========
*/
long FS_System_FOpenFileRead(const char *ospath, fileHandle_t *fp)
{
	fileHandle_t	f = 0;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	f = FS_HandleForFile();
	fsh[f].zipFile = qfalse;

	Q_strncpyz( fsh[f].name, ospath, sizeof( fsh[f].name ) );

	// don't let sound stutter
	S_ClearSoundBuffer();

	if ( fs_debug->integer ) {
		Com_Printf( "FS_System_FOpenFileRead: %s\n", ospath );
	}

	fsh[f].handleFiles.file.o = Sys_FOpen( ospath, "rb" );
	fsh[f].handleSync = qfalse;

	if (!fsh[f].handleFiles.file.o) {
		f = 0;
	}

	*fp = f;
	if (f) {
		return FS_filelength(f);
	}

	return -1;
}


/*
===========
FS_SV_Rename

===========
*/
void FS_SV_Rename( const char *from, const char *to, qboolean safe ) {
	char			*from_ospath, *to_ospath;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( !from || !*from || !to || !*to || Q_stricmp( from, to ) == 0 ) {
		return;
	}

	// don't let sound stutter
	S_ClearSoundBuffer();

	from_ospath = FS_BuildOSPath( fs_homepath->string, NULL, from );
	to_ospath = FS_BuildOSPath( fs_homepath->string, NULL, to );

	if ( fs_debug->integer ) {
		Com_Printf( "FS_SV_Rename: %s --> %s\n", from_ospath, to_ospath );
	}

	if ( safe ) {
		FS_CheckFilenameIsMutable( to_ospath, __func__ );
	}

	rename(from_ospath, to_ospath);
}



/*
===========
FS_Rename

===========
*/
qboolean FS_Rename( const char *from, const char *to ) {
	char			*from_ospath, *to_ospath;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( !from || !*from || !to || !*to || Q_stricmp( from, to ) == 0 ) {
		return qfalse;
	}

	// don't let sound stutter
	S_ClearSoundBuffer();

	from_ospath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, from );
	to_ospath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, to );

	if ( fs_debug->integer ) {
		Com_Printf( "FS_Rename: %s --> %s\n", from_ospath, to_ospath );
	}

	FS_CheckFilenameIsMutable( to_ospath, __func__ );

	if( FS_CreatePath( to_ospath ) ) {
		return qfalse;
	}

	return rename(from_ospath, to_ospath) == 0;
}

/*
==============
FS_FCloseFile

If the FILE pointer is an open pak file, leave it open.

For some reason, other dll's can't just cal fclose()
on files returned by FS_FOpenFile...
==============
*/
void FS_FCloseFile( fileHandle_t f ) {
	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( f < 1 || f >= MAX_FILE_HANDLES ) {
		return;
	}

	if (fsh[f].zipFile == qtrue) {
		unzCloseCurrentFile( fsh[f].handleFiles.file.z );
		if ( fsh[f].handleFiles.unique ) {
			unzClose( fsh[f].handleFiles.file.z );
		}
		Com_Memset( &fsh[f], 0, sizeof( fsh[f] ) );
		return;
	}

	// we didn't find it as a pak, so close it as a unique file
	if (fsh[f].handleFiles.file.o) {
		fclose (fsh[f].handleFiles.file.o);
	}
	Com_Memset( &fsh[f], 0, sizeof( fsh[f] ) );
}

/*
===========
FS_FOpenFileWrite

===========
*/
fileHandle_t FS_FOpenFileWrite( const char *filename ) {
	char			*ospath;
	fileHandle_t	f;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	f = FS_HandleForFile();
	fsh[f].zipFile = qfalse;

	ospath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, filename );

	if ( fs_debug->integer ) {
		Com_Printf( "FS_FOpenFileWrite: %s\n", ospath );
	}

	FS_CheckFilenameIsMutable( ospath, __func__ );

	if( FS_CreatePath( ospath ) ) {
		return 0;
	}

	// enabling the following line causes a recursive function call loop
	// when running with +set logfile 1 +set developer 1
	//Com_DPrintf( "writing to: %s\n", ospath );
	fsh[f].handleFiles.file.o = Sys_FOpen( ospath, "wb" );

	Q_strncpyz( fsh[f].name, filename, sizeof( fsh[f].name ) );

	fsh[f].handleSync = qfalse;
	if (!fsh[f].handleFiles.file.o) {
		f = 0;
	}
	return f;
}

/*
===========
FS_FOpenFileAppend

===========
*/
fileHandle_t FS_FOpenFileAppend( const char *filename ) {
	char			*ospath;
	fileHandle_t	f;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	f = FS_HandleForFile();
	fsh[f].zipFile = qfalse;

	Q_strncpyz( fsh[f].name, filename, sizeof( fsh[f].name ) );

	// don't let sound stutter
	S_ClearSoundBuffer();

	ospath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, filename );

	if ( fs_debug->integer ) {
		Com_Printf( "FS_FOpenFileAppend: %s\n", ospath );
	}

	FS_CheckFilenameIsMutable( ospath, __func__ );

	if( FS_CreatePath( ospath ) ) {
		return 0;
	}

	fsh[f].handleFiles.file.o = Sys_FOpen( ospath, "ab" );
	fsh[f].handleSync = qfalse;
	if (!fsh[f].handleFiles.file.o) {
		f = 0;
	}
	return f;
}

/*
===========
FS_FCreateOpenPipeFile

===========
*/
fileHandle_t FS_FCreateOpenPipeFile( const char *filename ) {
	char	    		*ospath;
	FILE					*fifo;
	fileHandle_t	f;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	f = FS_HandleForFile();
	fsh[f].zipFile = qfalse;

	Q_strncpyz( fsh[f].name, filename, sizeof( fsh[f].name ) );

	// don't let sound stutter
	S_ClearSoundBuffer();

	ospath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, filename );

	if ( fs_debug->integer ) {
		Com_Printf( "FS_FCreateOpenPipeFile: %s\n", ospath );
	}

	FS_CheckFilenameIsMutable( ospath, __func__ );

	fifo = Sys_Mkfifo( ospath );
	if( fifo ) {
		fsh[f].handleFiles.file.o = fifo;
		fsh[f].handleSync = qfalse;
	}
	else
	{
		Com_Printf( S_COLOR_YELLOW "WARNING: Could not create new com_pipefile at %s. "
			"com_pipefile will not be used.\n", ospath );
		f = 0;
	}

	return f;
}

/*
===========
FS_FilenameCompare

Ignore case and seprator char distinctions
===========
*/
qboolean FS_FilenameCompare( const char *s1, const char *s2 ) {
	int		c1, c2;

	do {
		c1 = *s1++;
		c2 = *s2++;

		if (c1 >= 'a' && c1 <= 'z') {
			c1 -= ('a' - 'A');
		}
		if (c2 >= 'a' && c2 <= 'z') {
			c2 -= ('a' - 'A');
		}

		if ( c1 == '\\' || c1 == ':' ) {
			c1 = '/';
		}
		if ( c2 == '\\' || c2 == ':' ) {
			c2 = '/';
		}

		if (c1 != c2) {
			return qtrue;		// strings not equal
		}
	} while (c1);

	return qfalse;		// strings are equal
}

/*
===========
FS_IsExt

Return qtrue if ext matches file extension filename
===========
*/

qboolean FS_IsExt(const char *filename, const char *ext, int namelen)
{
	int extlen;

	extlen = strlen(ext);

	if(extlen > namelen)
		return qfalse;

	filename += namelen - extlen;

	return !Q_stricmp(filename, ext);
}

/*
===========
FS_IsDemoExt

Return qtrue if filename has a demo extension
===========
*/

qboolean FS_IsDemoExt(const char *filename, int namelen)
{
	char dotdemoext[MAX_QPATH];

	Com_sprintf( dotdemoext, sizeof (dotdemoext), ".%s", com_demoext->string );

	return FS_IsExt( filename, dotdemoext, namelen );
}

/*
===========
FS_ReadPathFromDir

Return qtrue if filename can be read from path
===========
*/

qboolean FS_ReadPathFromDir( directory_t *dir, const char *filename ) {
	char path[MAX_QPATH];
	int qpathLength;

	if ( !dir->qpath[0] )
		return qtrue;

	Q_strncpyz( path, filename, sizeof ( path ) );
	FS_ReplaceSeparators( path );

	qpathLength = strlen( dir->qpath );

	if ( !Q_stricmpn( dir->qpath, path, qpathLength )
		&& (   path[qpathLength] == '\0'
			|| path[qpathLength] == '/'
			|| path[qpathLength] == '\\' ) )
		return qtrue;

	return qfalse;
}

/*
===========
FS_FOpenFileReadDir

Tries opening file "filename" in searchpath "search"
Returns filesize and an open FILE pointer.
===========
*/
extern qboolean		com_fullyInitialized;

long FS_FOpenFileReadDir(const char *filename, searchpath_t *search, fileHandle_t *file, qboolean uniqueFILE, qboolean unpure)
{
	long			hash;
	pack_t		*pak;
	fileInPack_t	*pakFile;
	directory_t	*dir;
	char		*netpath;
	FILE		*filep;
	int			len;

	if(filename == NULL)
		Com_Error(ERR_FATAL, "FS_FOpenFileRead: NULL 'filename' parameter passed");

	// qpaths are not supposed to have a leading slash
	if(filename[0] == '/' || filename[0] == '\\')
		filename++;

	// make absolutely sure that it can't back up the path.
	// The searchpaths do guarantee that something will always
	// be prepended, so we don't need to worry about "c:" or "//limbo" 
	if(strstr(filename, ".." ) || strstr(filename, "::"))
	{
		if(file == NULL)
			return qfalse;

		*file = 0;
		return -1;
	}

	if(file == NULL)
	{
		// just wants to see if file is there

		// is the element a pak file?
		if(search->pack)
		{
			hash = FS_HashFileName(filename, search->pack->hashSize);

			if(search->pack->hashTable[hash])
			{
				// look through all the pak file elements
				pak = search->pack;
				pakFile = pak->hashTable[hash];

				do
				{
					// case and separator insensitive comparisons
					if(!FS_FilenameCompare(pakFile->name, filename))
					{
						// found it!
						if(pakFile->len)
							return pakFile->len;
						else
						{
							// It's not nice, but legacy code depends
							// on positive value if file exists no matter
							// what size
							return 1;
						}
					}

					pakFile = pakFile->next;
				} while(pakFile != NULL);
			}
		}
		else if(search->dir)
		{
			dir = search->dir;

			if ( !FS_ReadPathFromDir( dir, filename ) )
			{
				return 0;
			}

			netpath = FS_BuildOSPath(dir->fullpath, NULL, filename);
			filep = Sys_FOpen(netpath, "rb");

			if(filep)
			{
				len = FS_fplength(filep);
				fclose(filep);

				if(len)
					return len;
				else
					return 1;
			}
		}

		return 0;
	}

	*file = FS_HandleForFile();
	fsh[*file].handleFiles.unique = uniqueFILE;

	// is the element a pak file?
	if(search->pack)
	{
		hash = FS_HashFileName(filename, search->pack->hashSize);

		if(search->pack->hashTable[hash])
		{
			// disregard if it doesn't match one of the allowed pure pak files
			if(!unpure && !FS_PakIsPure(search->pack))
			{
				*file = 0;
				return -1;
			}

			// look through all the pak file elements
			pak = search->pack;
			pakFile = pak->hashTable[hash];

			do
			{
				// case and separator insensitive comparisons
				if(!FS_FilenameCompare(pakFile->name, filename))
				{
					// found it!

					// mark the pak as having been referenced
					// shaders, txt, arena files  by themselves do not count as a reference as 
					// these are loaded from all pk3s 
					// from every pk3 file.. 
					len = strlen(filename);

					if (!pak->referenced)
					{
						if(!FS_IsExt(filename, ".shader", len) &&
						   !FS_IsExt(filename, ".txt", len) &&
						   !FS_IsExt(filename, ".cfg", len) &&
						   !FS_IsExt(filename, ".config", len) &&
						   !FS_IsExt(filename, ".bot", len) &&
						   !FS_IsExt(filename, ".arena", len) &&
						   !FS_IsExt(filename, ".menu", len) &&
						   Q_stricmp(filename, "vm/" VM_PREFIX "game.qvm") != 0 &&
						   !strstr(filename, "levelshots"))
						{
							pak->referenced = qtrue;
						}
						else if ( Q_stricmp(filename, "default.cfg") == 0 ||
								 Q_stricmp(filename, GAMESETTINGS) == 0)
						{
							pak->referenced = qtrue;
						}
					}

					if(uniqueFILE)
					{
						// open a new file on the pakfile
						fsh[*file].handleFiles.file.z = unzOpen(pak->pakFilename);

						if(fsh[*file].handleFiles.file.z == NULL)
							Com_Error(ERR_FATAL, "Couldn't open %s", pak->pakFilename);
					}
					else
						fsh[*file].handleFiles.file.z = pak->handle;

					Q_strncpyz(fsh[*file].name, filename, sizeof(fsh[*file].name));
					fsh[*file].zipFile = qtrue;

					// set the file position in the zip file (also sets the current file info)
					unzSetOffset(fsh[*file].handleFiles.file.z, pakFile->pos);

					// open the file in the zip
					unzOpenCurrentFile(fsh[*file].handleFiles.file.z);
					fsh[*file].zipFilePos = pakFile->pos;
					fsh[*file].zipFileLen = pakFile->len;

					if(fs_debug->integer)
					{
						Com_Printf("FS_FOpenFileRead: %s (found in '%s')\n", 
								filename, pak->pakFilename);
					}

					return pakFile->len;
				}

				pakFile = pakFile->next;
			} while(pakFile != NULL);
		}
	}
	else if(search->dir)
	{
		// check a file in the directory tree

		// if we are running restricted, the only files we
		// will allow to come from the directory are .cfg files
		len = strlen(filename);
		// FIXME TTimo I'm not sure about the fs_numServerPaks test
		// if you are using FS_ReadFile to find out if a file exists,
		//   this test can make the search fail although the file is in the directory
		// I had the problem on https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=8
		// turned out I used FS_FileExists instead
		if(!unpure && fs_numServerPaks)
		{
			if(!FS_IsExt(filename, ".cfg", len) &&		// for config files
			   !FS_IsExt(filename, ".txt", len) &&		// menu files, bots.txt, arenas.txt
			   !FS_IsExt(filename, ".menu", len) &&		// menu files
			   !FS_IsExt(filename, ".game", len) &&		// menu files
			   !FS_IsExt(filename, ".dat", len) &&		// for journal files
			   strncmp(filename, "fonts", 5) != 0 &&
			   strncmp(filename, "music", 5) != 0 &&
			   !FS_IsDemoExt(filename, len))			// demos
			{
				*file = 0;
				return -1;
			}
		}

		dir = search->dir;

		if ( !FS_ReadPathFromDir( dir, filename ) )
		{
			*file = 0;
			return -1;
		}

		netpath = FS_BuildOSPath(dir->fullpath, NULL, filename);
		filep = Sys_FOpen(netpath, "rb");

		if (filep == NULL)
		{
			*file = 0;
			return -1;
		}

		Q_strncpyz(fsh[*file].name, filename, sizeof(fsh[*file].name));
		fsh[*file].zipFile = qfalse;

		if(fs_debug->integer)
		{
			Com_Printf("FS_FOpenFileRead: %s (found in '%s')\n", filename,
					dir->fullpath);
		}

		fsh[*file].handleFiles.file.o = filep;
		return FS_fplength(filep);
	}

	*file = 0;
	return -1;
}

/*
===========
FS_FOpenFileRead

Finds the file in the search path.
Returns filesize and an open FILE pointer.
Used for streaming data out of either a
separate file or a ZIP file.
===========
*/
long FS_FOpenFileRead(const char *filename, fileHandle_t *file, qboolean uniqueFILE)
{
	searchpath_t *search;
	long len;
	qboolean isLocalConfig;

	if(!fs_searchpaths)
		Com_Error(ERR_FATAL, "Filesystem call made without initialization");

	isLocalConfig = !strcmp(filename, "autoexec.cfg") || !strcmp(filename, Q3CONFIG_CFG);
	for(search = fs_searchpaths; search; search = search->next)
	{
		// autoexec.cfg and q3config.cfg can only be loaded outside of pk3 files.
		if (isLocalConfig && search->pack)
			continue;

		len = FS_FOpenFileReadDir(filename, search, file, uniqueFILE, qfalse);

		if(file == NULL)
		{
			if(len > 0)
				return len;
		}
		else
		{
			if(len >= 0 && *file)
				return len;
		}

	}
	
#ifdef FS_MISSING
	if(missingFiles)
		fprintf(missingFiles, "%s\n", filename);
#endif

	if(file)
	{
		*file = 0;
		return -1;
	}
	else
	{
		// When file is NULL, we're querying the existence of the file
		// If we've got here, it doesn't exist
		return 0;
	}
}

/*
=================
FS_FindVM

Find a suitable VM file in search path order.

In each searchpath try:
 - open DLL file if DLL loading enabled
 - open QVM file

Enable search for DLL by setting enableDll to FSVM_ENABLEDLL

write found DLL or QVM to "found" and return VMI_NATIVE if DLL, VMI_COMPILED if QVM
Return the searchpath in "startSearch".
=================
*/

int FS_FindVM(void **startSearch, char *found, int foundlen, const char *name, int enableDll)
{
	searchpath_t *search, *lastSearch;
	directory_t *dir;
	pack_t *pack;
	char dllName[MAX_OSPATH], qvmName[MAX_OSPATH];
	char *netpath;

	if(!fs_searchpaths)
		Com_Error(ERR_FATAL, "Filesystem call made without initialization");

	if(enableDll)
		Com_sprintf(dllName, sizeof(dllName), "%s_" ARCH_STRING DLL_EXT, name);

	Com_sprintf(qvmName, sizeof(qvmName), "vm/%s.qvm", name);

	lastSearch = *startSearch;
	if(*startSearch == NULL)
		search = fs_searchpaths;
	else
		search = lastSearch->next;

	while(search)
	{
		if(search->dir && FS_ReadPathFromDir( search->dir, dllName ) )
		{
			dir = search->dir;

			if(enableDll)
			{
				netpath = FS_BuildOSPath(dir->fullpath, NULL, dllName);

				if(FS_FileInPathExists(netpath))
				{
					Q_strncpyz(found, netpath, foundlen);
					*startSearch = search;

					return VMI_NATIVE;
				}
			}

			if(FS_FOpenFileReadDir(qvmName, search, NULL, qfalse, qtrue) > 0)
			{
				Com_sprintf( found, foundlen, "%s%c%s", search->dir->fullpath, PATH_SEP, qvmName );

				*startSearch = search;
				return VMI_COMPILED;
			}
		}
		else if(search->pack)
		{
			pack = search->pack;

			if(lastSearch && lastSearch->pack)
			{
				// make sure we only try loading one VM file per game dir
				// i.e. if VM from pak7.pk3 fails we won't try one from pak6.pk3

				if(!FS_FilenameCompare(lastSearch->pack->pakPathname, pack->pakPathname))
				{
					search = search->next;
					continue;
				}
			}

			if(FS_FOpenFileReadDir(qvmName, search, NULL, qfalse, qfalse) > 0)
			{
				Com_sprintf( found, foundlen, "%s%c%s", search->pack->pakFilename, PATH_SEP, qvmName );

				*startSearch = search;

				return VMI_COMPILED;
			}
		}

		search = search->next;
	}

	return -1;
}

/*
==============
FS_AllowDeletion
==============
*/
qboolean FS_AllowDeletion( char *filename ) {
	// for safety, only allow deletion from the save, profiles and demo directory
	if ( Q_strncmp( filename, "save/", 5 ) != 0 &&
		 Q_strncmp( filename, "profiles/", 9 ) != 0 &&
		 Q_strncmp( filename, "demos/", 6 ) != 0 ) {
		return qfalse;
	}

	return qtrue;
}

/*
==============
FS_DeleteDir
==============
*/
int FS_DeleteDir( char *dirname, qboolean nonEmpty, qboolean recursive ) {
	char    *ospath;
	char    **pFiles = NULL;
	int     i, nFiles = 0;
	char	fileName[MAX_QPATH];

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization\n" );
	}

	if ( !dirname || dirname[0] == 0 ) {
		return 0;
	}

	if ( !FS_AllowDeletion( dirname ) ) {
		return 0;
	}

	if ( recursive ) {
		ospath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, dirname );
		pFiles = Sys_ListFiles( ospath, "/", NULL, &nFiles, qfalse );
		for ( i = 0; i < nFiles; i++ ) {
			char temp[MAX_OSPATH];

			if ( !Q_stricmp( pFiles[i], ".." ) || !Q_stricmp( pFiles[i], "." ) ) {
				continue;
			}

			Com_sprintf( temp, sizeof( temp ), "%s/%s", dirname, pFiles[i] );

			if ( !FS_DeleteDir( temp, nonEmpty, recursive ) ) {
				return 0;
			}
		}
		Sys_FreeFileList( pFiles );
	}

	if ( nonEmpty ) {
		ospath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, dirname );
		pFiles = Sys_ListFiles( ospath, NULL, NULL, &nFiles, qfalse );
		for ( i = 0; i < nFiles; i++ ) {
			Com_sprintf( fileName, sizeof (fileName), "%s/%s", dirname, pFiles[i] );
			ospath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, fileName );

			if ( !FS_Remove( ospath ) ) {  // failure
				return 0;
			}
		}
		Sys_FreeFileList( pFiles );
	}

	ospath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, dirname );

	if ( Sys_Rmdir( ospath ) ) {
		return 1;
	}

	return 0;
}

/*
==============
FS_Delete
using fs_homepath for the file to remove
==============
*/
int FS_Delete( char *filename ) {
	char    *ospath;
	int     stat;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization\n" );
	}

	if ( !filename || !*filename ) {
		return 0;
	}

	if ( !FS_AllowDeletion( filename ) ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: Not allowed to delete %s\n", filename );
		return 0;
	}

	ospath = FS_BuildOSPath( fs_homepath->string, fs_gamedir, filename );

	if ( fs_debug->integer ) {
		Com_Printf( "FS_Delete: %s\n", ospath );
	}

	stat = Sys_StatFile( ospath );
	if ( stat == -1 ) {
		return 0;
	}

	if ( stat == 1 ) {
		return( FS_DeleteDir( filename, qtrue, qtrue ) );
	} else {
		if ( FS_Remove( ospath ) ) {  // success
			return 1;
		}
	}

	return 0;
}

/*
=================
FS_Read

Properly handles partial reads
=================
*/
int FS_Read( void *buffer, int len, fileHandle_t f ) {
	int		block, remaining;
	int		read;
	byte	*buf;
	int		tries;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( f < 1 || f >= MAX_FILE_HANDLES ) {
		return 0;
	}

	if ( !buffer || len < 1 ) {
		return 0;
	}

	buf = (byte *)buffer;
	fs_readCount += len;

	if (fsh[f].zipFile == qfalse) {
		remaining = len;
		tries = 0;
		while (remaining) {
			block = remaining;
			read = fread (buf, 1, block, fsh[f].handleFiles.file.o);
			if (read == 0) {
				// we might have been trying to read from a CD, which
				// sometimes returns a 0 read on windows
				if (!tries) {
					tries = 1;
				} else {
					return len-remaining;	//Com_Error (ERR_FATAL, "FS_Read: 0 bytes read");
				}
			}

			if (read == -1) {
				Com_Error (ERR_FATAL, "FS_Read: -1 bytes read");
			}

			remaining -= read;
			buf += read;
		}
		return len;
	} else {
		return unzReadCurrentFile(fsh[f].handleFiles.file.z, buffer, len);
	}
}

/*
=================
FS_Write

Properly handles partial writes
=================
*/
int FS_Write( const void *buffer, int len, fileHandle_t h ) {
	int		block, remaining;
	int		written;
	byte	*buf;
	int		tries;
	FILE	*f;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( h < 1 || h >= MAX_FILE_HANDLES ) {
		return 0;
	}

	if ( !buffer || len < 1 ) {
		return 0;
	}

	f = FS_FileForHandle(h);
	buf = (byte *)buffer;

	remaining = len;
	tries = 0;
	while (remaining) {
		block = remaining;
		written = fwrite (buf, 1, block, f);
		if (written == 0) {
			if (!tries) {
				tries = 1;
			} else {
				Com_Printf( "FS_Write: 0 bytes written\n" );
				return 0;
			}
		}

		if (written == -1) {
			Com_Printf( "FS_Write: -1 bytes written\n" );
			return 0;
		}

		remaining -= written;
		buf += written;
	}
	if ( fsh[h].handleSync ) {
		fflush( f );
	}
	return len;
}

void QDECL FS_Printf( fileHandle_t h, const char *fmt, ... ) {
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	va_start (argptr,fmt);
	Q_vsnprintf (msg, sizeof(msg), fmt, argptr);
	va_end (argptr);

	FS_Write(msg, strlen(msg), h);
}

#define PK3_SEEK_BUFFER_SIZE 65536

/*
=================
FS_Seek

Returns 0 on success and non-zero on failure
=================
*/
int FS_Seek( fileHandle_t f, long offset, int origin ) {
	int		_origin;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
		return -1;
	}

	if ( f < 1 || f >= MAX_FILE_HANDLES ) {
		return -1;
	}

	if (fsh[f].zipFile == qtrue) {
		//FIXME: this is really, really crappy
		//(but better than what was here before)
		byte	buffer[PK3_SEEK_BUFFER_SIZE];
		int		remainder;
		int		currentPosition = FS_FTell( f );

		// change negative offsets into FS_SEEK_SET
		if ( offset < 0 ) {
			switch( origin ) {
				case FS_SEEK_END:
					remainder = fsh[f].zipFileLen + offset;
					break;

				case FS_SEEK_CUR:
					remainder = currentPosition + offset;
					break;

				case FS_SEEK_SET:
				default:
					remainder = 0;
					break;
			}

			if ( remainder < 0 ) {
				remainder = 0;
			}

			origin = FS_SEEK_SET;
		} else {
			if ( origin == FS_SEEK_END ) {
				remainder = fsh[f].zipFileLen - currentPosition + offset;
			} else {
				remainder = offset;
			}
		}

		switch( origin ) {
			case FS_SEEK_SET:
				if ( remainder == currentPosition ) {
					return 0;
				}
				unzSetOffset(fsh[f].handleFiles.file.z, fsh[f].zipFilePos);
				unzOpenCurrentFile(fsh[f].handleFiles.file.z);
				//fallthrough

			case FS_SEEK_END:
			case FS_SEEK_CUR:
				while( remainder > PK3_SEEK_BUFFER_SIZE ) {
					FS_Read( buffer, PK3_SEEK_BUFFER_SIZE, f );
					remainder -= PK3_SEEK_BUFFER_SIZE;
				}
				FS_Read( buffer, remainder, f );
				return 0;

			default:
				Com_Error( ERR_FATAL, "Bad origin in FS_Seek" );
				return -1;
		}
	} else {
		FILE *file;
		file = FS_FileForHandle(f);
		switch( origin ) {
		case FS_SEEK_CUR:
			_origin = SEEK_CUR;
			break;
		case FS_SEEK_END:
			_origin = SEEK_END;
			break;
		case FS_SEEK_SET:
			_origin = SEEK_SET;
			break;
		default:
			Com_Error( ERR_FATAL, "Bad origin in FS_Seek" );
			break;
		}

		return fseek( file, offset, _origin );
	}
}


/*
======================================================================================

CONVENIENCE FUNCTIONS FOR ENTIRE FILES

======================================================================================
*/

/*
============
FS_ReadFileDir

Filename are relative to the quake search path
a null buffer will just return the file length without loading
If searchPath is non-NULL search only in that specific search path
============
*/
long FS_ReadFileDir(const char *qpath, void *searchPath, qboolean unpure, void **buffer)
{
	fileHandle_t	h;
	searchpath_t	*search;
	byte*			buf;
	qboolean		isConfig;
	long				len;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( !qpath || !qpath[0] ) {
		Com_Error( ERR_FATAL, "FS_ReadFile with empty name" );
	}

	buf = NULL;	// quiet compiler warning

	// if this is a .cfg file and we are playing back a journal, read
	// it from the journal file
	if ( strstr( qpath, ".cfg" ) ) {
		isConfig = qtrue;
		if ( com_journal && com_journal->integer == 2 ) {
			int		r;

			Com_DPrintf( "Loading %s from journal file.\n", qpath );
			r = FS_Read( &len, sizeof( len ), com_journalDataFile );
			if ( r != sizeof( len ) ) {
				if (buffer != NULL) *buffer = NULL;
				return -1;
			}
			// if the file didn't exist when the journal was created
			if (!len) {
				if (buffer == NULL) {
					return 1;			// hack for old journal files
				}
				*buffer = NULL;
				return -1;
			}
			if (buffer == NULL) {
				return len;
			}

			buf = Hunk_AllocateTempMemory(len+1);
			*buffer = buf;

			r = FS_Read( buf, len, com_journalDataFile );
			if ( r != len ) {
				Com_Error( ERR_FATAL, "Read from journalDataFile failed" );
			}

			fs_loadCount++;
			fs_loadStack++;

			// guarantee that it will have a trailing 0 for string operations
			buf[len] = 0;

			return len;
		}
	} else {
		isConfig = qfalse;
	}

	search = searchPath;

	if(search == NULL)
	{
		// look for it in the filesystem or pack files
		len = FS_FOpenFileRead(qpath, &h, qfalse);
	}
	else
	{
		// look for it in a specific search path only
		len = FS_FOpenFileReadDir(qpath, search, &h, qfalse, unpure);
	}

	if ( h == 0 ) {
		if ( buffer ) {
			*buffer = NULL;
		}
		// if we are journalling and it is a config file, write a zero to the journal file
		if ( isConfig && com_journal && com_journal->integer == 1 ) {
			Com_DPrintf( "Writing zero for %s to journal file.\n", qpath );
			len = 0;
			FS_Write( &len, sizeof( len ), com_journalDataFile );
			FS_Flush( com_journalDataFile );
		}
		return -1;
	}

	if ( !buffer ) {
		if ( isConfig && com_journal && com_journal->integer == 1 ) {
			Com_DPrintf( "Writing len for %s to journal file.\n", qpath );
			FS_Write( &len, sizeof( len ), com_journalDataFile );
			FS_Flush( com_journalDataFile );
		}
		FS_FCloseFile( h);
		return len;
	}

	fs_loadCount++;
	fs_loadStack++;

	buf = Hunk_AllocateTempMemory(len+1);
	*buffer = buf;

	FS_Read (buf, len, h);

	// guarantee that it will have a trailing 0 for string operations
	buf[len] = 0;
	FS_FCloseFile( h );

	// if we are journalling and it is a config file, write it to the journal file
	if ( isConfig && com_journal && com_journal->integer == 1 ) {
		Com_DPrintf( "Writing %s to journal file.\n", qpath );
		FS_Write( &len, sizeof( len ), com_journalDataFile );
		FS_Write( buf, len, com_journalDataFile );
		FS_Flush( com_journalDataFile );
	}
	return len;
}

/*
============
FS_ReadFileFromGameDir

Filename are relative to the quake search path
a null buffer will just return the file length without loading
============
*/
long FS_ReadFileFromGameDir(const char *qpath, void **buffer, const char *gameDir)
{
	searchpath_t *search;
	const char *dirname;
	long len;

	if(!fs_searchpaths)
		Com_Error(ERR_FATAL, "Filesystem call made without initialization");

	for(search = fs_searchpaths; search; search = search->next)
	{
		if(search->pack) {
			dirname = search->pack->pakGamename;
		} else {
			dirname = search->dir->gamedir;
		}

		if(Q_stricmp(gameDir, dirname)) {
			continue;
		}

		len = FS_ReadFileDir(qpath, search, qfalse, buffer);

		if(len >= 0) {
			return len;
		}
	}

	if (buffer) {
		*buffer = NULL;
	}
	return -1;
}

/*
============
FS_ReadFile

Filename are relative to the quake search path
a null buffer will just return the file length without loading
============
*/
long FS_ReadFile(const char *qpath, void **buffer)
{
	return FS_ReadFileDir(qpath, NULL, qfalse, buffer);
}

/*
=============
FS_FreeFile
=============
*/
void FS_FreeFile( void *buffer ) {
	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}
	if ( !buffer ) {
		Com_Error( ERR_FATAL, "FS_FreeFile( NULL )" );
	}
	fs_loadStack--;

	Hunk_FreeTempMemory( buffer );

	// if all of our temp files are free, clear all of our space
	if ( fs_loadStack == 0 ) {
		Hunk_ClearTempMemory();
	}
}

/*
============
FS_WriteFile

Filename are relative to the quake search path
============
*/
void FS_WriteFile( const char *qpath, const void *buffer, int size ) {
	fileHandle_t f;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( !qpath || !buffer ) {
		Com_Error( ERR_FATAL, "FS_WriteFile: NULL parameter" );
	}

	f = FS_FOpenFileWrite( qpath );
	if ( !f ) {
		Com_Printf( "Failed to open %s\n", qpath );
		return;
	}

	FS_Write( buffer, size, f );

	FS_FCloseFile( f );
}



/*
==========================================================================

ZIP FILE LOADING

==========================================================================
*/

/*
=================
FS_LoadZipFile

Creates a new pak_t in the search chain for the contents
of a zip file.
=================
*/
static pack_t *FS_LoadZipFile(const char *zipfile, const char *basename)
{
	fileInPack_t	*buildBuffer;
	pack_t			*pack;
	unzFile			uf;
	int				err;
	unz_global_info gi;
	char			filename_inzip[MAX_ZPATH];
	unz_file_info	file_info;
	int				i, len;
	long			hash;
	int				fs_numHeaderLongs;
	int				*fs_headerLongs;
	char			*namePtr;

	fs_numHeaderLongs = 0;

	uf = unzOpen(zipfile);
	err = unzGetGlobalInfo (uf,&gi);

	if (err != UNZ_OK)
		return NULL;

	len = 0;
	unzGoToFirstFile(uf);
	for (i = 0; i < gi.number_entry; i++)
	{
		err = unzGetCurrentFileInfo(uf, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0);
		if (err != UNZ_OK) {
			break;
		}
		len += strlen(filename_inzip) + 1;
		unzGoToNextFile(uf);
	}

	buildBuffer = Z_Malloc( (gi.number_entry * sizeof( fileInPack_t )) + len );
	namePtr = ((char *) buildBuffer) + gi.number_entry * sizeof( fileInPack_t );
	fs_headerLongs = Z_Malloc( gi.number_entry * sizeof(int) );

	// get the hash table size from the number of files in the zip
	// because lots of custom pk3 files have less than 32 or 64 files
	for (i = 1; i <= MAX_FILEHASH_SIZE; i <<= 1) {
		if (i > gi.number_entry) {
			break;
		}
	}

	pack = Z_Malloc( sizeof( pack_t ) + i * sizeof(fileInPack_t *) );
	pack->hashSize = i;
	pack->hashTable = (fileInPack_t **) (((char *) pack) + sizeof( pack_t ));
	for(i = 0; i < pack->hashSize; i++) {
		pack->hashTable[i] = NULL;
	}

	Q_strncpyz( pack->pakFilename, zipfile, sizeof( pack->pakFilename ) );
	Q_strncpyz( pack->pakBasename, basename, sizeof( pack->pakBasename ) );

	// strip .pk3 if needed
	if ( strlen( pack->pakBasename ) > 4 && !Q_stricmp( pack->pakBasename + strlen( pack->pakBasename ) - 4, ".pk3" ) ) {
		pack->pakBasename[strlen( pack->pakBasename ) - 4] = 0;
	}

	pack->handle = uf;
	pack->numfiles = gi.number_entry;
	unzGoToFirstFile(uf);

	for (i = 0; i < gi.number_entry; i++)
	{
		err = unzGetCurrentFileInfo(uf, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0);
		if (err != UNZ_OK) {
			break;
		}
		if (file_info.uncompressed_size > 0) {
			fs_headerLongs[fs_numHeaderLongs++] = LittleLong(file_info.uncompressed_size);
		}
		Q_strlwr( filename_inzip );
		hash = FS_HashFileName(filename_inzip, pack->hashSize);
		buildBuffer[i].name = namePtr;
		strcpy( buildBuffer[i].name, filename_inzip );
		namePtr += strlen(filename_inzip) + 1;
		// store the file position in the zip
		buildBuffer[i].pos = unzGetOffset(uf);
		buildBuffer[i].len = file_info.uncompressed_size;
		buildBuffer[i].next = pack->hashTable[hash];
		pack->hashTable[hash] = &buildBuffer[i];
		unzGoToNextFile(uf);
	}

	pack->checksum = Com_BlockChecksum( fs_headerLongs, sizeof(*fs_headerLongs) *  fs_numHeaderLongs );
	pack->checksum = LittleLong( pack->checksum );

	Z_Free(fs_headerLongs);

	pack->buildBuffer = buildBuffer;
	return pack;
}

/*
=================
FS_FreePak

Frees a pak structure and releases all associated resources
=================
*/

static void FS_FreePak(pack_t *thepak)
{
	unzClose(thepak->handle);
	Z_Free(thepak->buildBuffer);
	Z_Free(thepak);
}

/*
=================
FS_GetZipChecksum

Compares whether the given pak file matches a referenced checksum
=================
*/
qboolean FS_CompareZipChecksum(const char *zipfile)
{
	pack_t *thepak;
	int index, checksum;

	thepak = FS_LoadZipFile(zipfile, "");

	if(!thepak)
		return qfalse;

	checksum = thepak->checksum;
	FS_FreePak(thepak);

	for(index = 0; index < fs_numServerReferencedPaks; index++)
	{
		if(checksum == fs_serverReferencedPaks[index])
			return qtrue;
	}

	return qfalse;
}

/*
=================================================================================

DIRECTORY SCANNING FUNCTIONS

=================================================================================
*/

#define	MAX_FOUND_FILES	0x1000

static int FS_ReturnPath( const char *zname, char *zpath, int *depth ) {
	int len, at, newdep;

	newdep = 0;
	zpath[0] = 0;
	len = 0;
	at = 0;

	while(zname[at] != 0)
	{
		if (zname[at]=='/' || zname[at]=='\\') {
			len = at;
			newdep++;
		}
		at++;
	}
	strcpy(zpath, zname);
	zpath[len] = 0;
	*depth = newdep;

	return len;
}

/*
==================
FS_AddFileToList
==================
*/
static int FS_AddFileToList( char *name, char *list[MAX_FOUND_FILES], int nfiles ) {
	int		i, j, val;

	if ( nfiles == MAX_FOUND_FILES - 1 ) {
		return nfiles;
	}
	for ( i = 0 ; i < nfiles ; i++ ) {
		val = FS_PathCmp( name, list[i] );
		if ( !val ) {
			return nfiles;		// already in list
		}
		// insert into list
		else if ( val < 0 ) {
			for ( j = nfiles-1; j >= i; --j ) {
				list[j+1] = list[j];
			}

			list[i] = CopyString( name );
			nfiles++;

			return nfiles;
		}
	}
	list[nfiles] = CopyString( name );
	nfiles++;

	return nfiles;
}

/*
===============
FS_ListFilteredFiles

Returns a uniqued list of files that match the given criteria
from all search paths
===============
*/
char **FS_ListFilteredFiles( const char *path, const char *extension, char *filter, int *numfiles, qboolean allowNonPureFilesOnDisk ) {
	int				nfiles;
	char			**listCopy;
	char			*list[MAX_FOUND_FILES];
	searchpath_t	*search;
	int				i;
	int				pathLength;
	int				extensionLength;
	int				length, pathDepth, temp;
	pack_t			*pak;
	fileInPack_t	*buildBuffer;
	char			zpath[MAX_ZPATH];

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( !path ) {
		*numfiles = 0;
		return NULL;
	}
	if ( !extension ) {
		extension = "";
	}

	pathLength = strlen( path );
	if ( path[pathLength-1] == '\\' || path[pathLength-1] == '/' ) {
		pathLength--;
	}
	extensionLength = strlen( extension );
	nfiles = 0;
	FS_ReturnPath(path, zpath, &pathDepth);

	//
	// search through the path, one element at a time, adding to list
	//
	for (search = fs_searchpaths ; search ; search = search->next) {
		// is the element a pak file?
		if (search->pack) {

			//ZOID:  If we are pure, don't search for files on paks that
			// aren't on the pure list
			if ( !FS_PakIsPure(search->pack) ) {
				continue;
			}

			// look through all the pak file elements
			pak = search->pack;
			buildBuffer = pak->buildBuffer;
			for (i = 0; i < pak->numfiles; i++) {
				char	*name;
				int		zpathLen, depth;

				// check for directory match
				name = buildBuffer[i].name;
				//
				if (filter) {
					// case insensitive
					if (!Com_FilterPath( filter, name, qfalse ))
						continue;
					// unique the match
					nfiles = FS_AddFileToList( name, list, nfiles );
				}
				else {

					zpathLen = FS_ReturnPath(name, zpath, &depth);

					if ( (depth-pathDepth)>2 || pathLength > zpathLen || Q_stricmpn( name, path, pathLength ) ) {
						continue;
					}

					// check for extension match
					length = strlen( name );
					if ( length < extensionLength ) {
						continue;
					}

					if ( Q_stricmp( name + length - extensionLength, extension ) ) {
						continue;
					}
					// unique the match

					temp = pathLength;
					if (pathLength) {
						temp++; // include the '/'
					}
					nfiles = FS_AddFileToList( name + temp, list, nfiles );
				}
			}
		} else if (search->dir) { // scan for files in the filesystem
			char	*netpath;
			int		numSysFiles;
			char	**sysFiles;
			char	*name;

			// don't scan directories for files if we are pure or restricted
			if ( fs_numServerPaks && !allowNonPureFilesOnDisk ) {
				continue;
			} else {
				if ( !FS_ReadPathFromDir( search->dir, path ) ) {
					continue;
				}

				netpath = FS_BuildOSPath( search->dir->fullpath, NULL, path );
				sysFiles = Sys_ListFiles( netpath, extension, filter, &numSysFiles, qfalse );
				for ( i = 0 ; i < numSysFiles ; i++ ) {
					// unique the match
					name = sysFiles[i];
					nfiles = FS_AddFileToList( name, list, nfiles );
				}
				Sys_FreeFileList( sysFiles );
			}
		}
	}

	// return a copy of the list
	*numfiles = nfiles;

	if ( !nfiles ) {
		return NULL;
	}

	listCopy = Z_Malloc( ( nfiles + 1 ) * sizeof( *listCopy ) );
	for ( i = 0 ; i < nfiles ; i++ ) {
		listCopy[i] = list[i];
	}
	listCopy[i] = NULL;

	return listCopy;
}

/*
=================
FS_ListFiles
=================
*/
char **FS_ListFiles( const char *path, const char *extension, int *numfiles ) {
	return FS_ListFilteredFiles( path, extension, NULL, numfiles, qfalse );
}

/*
=================
FS_ListFilesEx

Create a list of files using multiple file extensions
=================
*/
char **FS_ListFilesEx( const char *path, const char **extensions, int numExts, int *numfiles, qboolean allowNonPureFilesOnDisk ) {
	int		nfiles;
	char	**listCopy;
	char	*list[MAX_FOUND_FILES];
	char	**sysFiles = NULL;
	int		numSysFiles;
	char	*name;
	int		i;
	int		j;

	nfiles = 0;

	for (i = 0; i < numExts; i++)
	{
		sysFiles = FS_ListFilteredFiles( path, extensions[i], NULL, &numSysFiles, allowNonPureFilesOnDisk );
		for ( j = 0 ; j < numSysFiles ; j++ ) {
			// unique the match
			name = sysFiles[j];
			nfiles = FS_AddFileToList( name, list, nfiles );
		}
		Sys_FreeFileList( sysFiles );
	}

	// return a copy of the list
	*numfiles = nfiles;

	if ( !nfiles ) {
		return NULL;
	}

	listCopy = Z_Malloc( ( nfiles + 1 ) * sizeof( *listCopy ) );
	for ( i = 0 ; i < nfiles ; i++ ) {
		listCopy[i] = list[i];
	}
	listCopy[i] = NULL;

	return listCopy;
}

/*
=================
FS_FreeFileList
=================
*/
void FS_FreeFileList( char **list ) {
	int		i;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( !list ) {
		return;
	}

	for ( i = 0 ; list[i] ; i++ ) {
		Z_Free( list[i] );
	}

	Z_Free( list );
}


/*
================
FS_GetFileList
================
*/
char **FS_GetFileList( const char *path, const char *extension, int *numfiles, qboolean allowNonPureFilesOnDisk ) {
	int		nFiles;
	char **pFiles = NULL;

	if ( !path ) {
		if ( numfiles ) {
			*numfiles = 0;
		}
		return NULL;
	}

	if ( !extension ) {
		extension = "";
	}

	if (Q_stricmp(extension, "$demos") == 0)
	{
		char dotdemoext[MAX_QPATH];
		Com_sprintf( dotdemoext, sizeof ( dotdemoext ), ".%s", com_demoext->string );
		pFiles = FS_ListFilteredFiles(path, dotdemoext, NULL, &nFiles, allowNonPureFilesOnDisk);
	}
	else if (Q_stricmp(extension, "$videos") == 0)
	{
		const char *extensions[] = { "RoQ", "roq" };
		int extNamesSize = ARRAY_LEN(extensions);
		pFiles = FS_ListFilesEx(path, extensions, extNamesSize, &nFiles, allowNonPureFilesOnDisk);
	}
	else if (Q_stricmp(extension, "$images") == 0)
	{
		const char *extensions[] = { "png", "tga", "jpg", "jpeg", "ftx", "dds", "pcx", "bmp" };
		int extNamesSize = ARRAY_LEN(extensions);
		pFiles = FS_ListFilesEx(path, extensions, extNamesSize, &nFiles, allowNonPureFilesOnDisk);
	}
	else if (Q_stricmp(extension, "$sounds") == 0)
	{
		const char *extensions[] = { "wav"
#ifdef USE_CODEC_VORBIS
			, "ogg"
#endif
#ifdef USE_CODEC_OPUS
			, "opus"
#endif
			};
		int extNamesSize = ARRAY_LEN(extensions);
		pFiles = FS_ListFilesEx(path, extensions, extNamesSize, &nFiles, allowNonPureFilesOnDisk);
	}
	else if (Q_stricmp(extension, "$fonts") == 0)
	{
		const char *extensions[] = { "ttf", "otf", "ttc", "otc", "fon" };
		int extNamesSize = ARRAY_LEN(extensions);
		pFiles = FS_ListFilesEx(path, extensions, extNamesSize, &nFiles, allowNonPureFilesOnDisk);
	}
	else if (Q_stricmp(extension, "$models") == 0)
	{
		const char *extensions[] = { "md3", "mdr", "mdc", "mds", "mdx", "mdm", "tan", "iqm" };
		int extNamesSize = ARRAY_LEN(extensions);
		pFiles = FS_ListFilesEx(path, extensions, extNamesSize, &nFiles, allowNonPureFilesOnDisk);
	}
	// Allow extension to be a list
	// Example "RoQ;roq;jpg;wav"
	else if (strstr(extension, ";"))
	{
		#define MAX_FILE_LIST_EXTS 8
		char buffer[MAX_QPATH];
		const char *extensions[MAX_FILE_LIST_EXTS];
		int numExts;
		int len;
		char *s1;
		char *s2;

		numExts = 0;

		Q_strncpyz(buffer, extension, MAX_QPATH);

		s1 = buffer;
		s2 = strchr(s1, ';');

		len = s2 - s1 + 1;
		if (len > 0)
		{
			extensions[numExts] = s1;
			numExts++;
		}

		while ( 1 )
		{
			s1 = s2;
			if (!s1) {
				break;
			}
			*s1 = 0; // Change ';' to '\0'
			s1++;

			s2 = strchr(s1, ';');
			if (s2) {
				len = s2 - s1 + 1;
			} else {
				len = strlen(s1) + 1;
			}

			if (len > 0)
			{
				extensions[numExts] = s1;
				numExts++;
				if (numExts == MAX_FILE_LIST_EXTS) {
					break;
				}
			}
		}

		pFiles = FS_ListFilesEx(path, extensions, numExts, &nFiles, allowNonPureFilesOnDisk);
		#undef MAX_FILE_LIST_EXTS
	}
	else
	{
		pFiles = FS_ListFilteredFiles(path, extension, NULL, &nFiles, allowNonPureFilesOnDisk);
	}

	if ( numfiles ) {
		*numfiles = nFiles;
	}
	return pFiles;
}

/*
================
FS_GetFileListBuffer
================
*/
int	FS_GetFileListBuffer( const char *path, const char *extension, char *listbuf, int bufsize ) {
	int		nFiles, i, nTotal, nLen;
	char **pFiles = NULL;

	if ( !path || !listbuf || bufsize < 1 ) {
		return 0;
	}

	if ( !extension ) {
		extension = "";
	}

	*listbuf = 0;
	nFiles = 0;
	nTotal = 0;

	if (Q_stricmp(path, "$modlist") == 0) {
		return FS_GetModList(listbuf, bufsize);
	}

	// ZTM: TODO: merging removing extension with general block as this list code is duplicate.
	if (Q_stricmp(extension, "$demos") == 0) {
		char dotdemoext[MAX_QPATH];
		int extLength;

		Com_sprintf( dotdemoext, sizeof ( dotdemoext ), ".%s", com_demoext->string );
		extLength = strlen(dotdemoext);
		pFiles = FS_ListFiles(path, dotdemoext, &nFiles);

		// strip extension from list items
		for (i =0; i < nFiles; i++) {
			nLen = strlen(pFiles[i]) + 1 - extLength;
			if (nTotal + nLen + 1 < bufsize) {
				Q_strncpyz(listbuf, pFiles[i], nLen);
				listbuf += nLen;
				nTotal += nLen;
			}
			else {
				nFiles = i;
				break;
			}
		}

		FS_FreeFileList(pFiles);
		return nFiles;
	}

	pFiles = FS_GetFileList( path, extension, &nFiles, qfalse );

	for (i =0; i < nFiles; i++) {
		nLen = strlen(pFiles[i]) + 1;
		if (nTotal + nLen + 1 < bufsize) {
			strcpy(listbuf, pFiles[i]);
			listbuf += nLen;
			nTotal += nLen;
		}
		else {
			nFiles = i;
			break;
		}
	}

	FS_FreeFileList(pFiles);

	return nFiles;
}

/*
=======================
Sys_ConcatenateFileLists

mkv: Naive implementation. Concatenates three lists into a
     new list, and frees the old lists from the heap.
bk001129 - from cvs1.17 (mkv)

FIXME TTimo those two should move to common.c next to Sys_ListFiles
=======================
 */
static unsigned int Sys_CountFileList(char **list)
{
	int i = 0;

	if (list)
	{
		while (*list)
		{
			list++;
			i++;
		}
	}
	return i;
}

static char** Sys_ConcatenateFileLists( char **list0, char **list1, char **list2 )
{
	int totalLength = 0;
	char** cat = NULL, **dst, **src;

	totalLength += Sys_CountFileList(list0);
	totalLength += Sys_CountFileList(list1);
	totalLength += Sys_CountFileList(list2);

	/* Create new list. */
	dst = cat = Z_Malloc( ( totalLength + 1 ) * sizeof( char* ) );

	/* Copy over lists. */
	if (list0)
	{
		for (src = list0; *src; src++, dst++)
			*dst = *src;
	}
	if (list1)
	{
		for (src = list1; *src; src++, dst++)
			*dst = *src;
	}
	if (list2)
	{
		for (src = list2; *src; src++, dst++)
			*dst = *src;
	}

	// Terminate the list
	*dst = NULL;

	// Free our old lists.
	// NOTE: not freeing their content, it's been merged in dst and still being used
	if (list0) Z_Free( list0 );
	if (list1) Z_Free( list1 );
	if (list2) Z_Free( list2 );

	return cat;
}

/*
================
FS_GetModDescription
================
*/
void FS_GetModDescription( const char *modDir, char *description, int descriptionLen ) {
	fileHandle_t	descHandle;
	char			descPath[MAX_QPATH];
	int				nDescLen;
	FILE			*file;

	Com_sprintf( descPath, sizeof ( descPath ), "%s%cdescription.txt", modDir, PATH_SEP );
	nDescLen = FS_SV_FOpenFileRead( descPath, &descHandle );

	if ( nDescLen > 0 ) {
		file = FS_FileForHandle(descHandle);
		Com_Memset( description, 0, descriptionLen );
		nDescLen = fread(description, 1, descriptionLen, file);
		if (nDescLen >= 0) {
			description[nDescLen] = '\0';
		}

		// Remove \n, \r\n, and \r
		if (nDescLen > 0 && description[nDescLen - 1] == '\n') {
			nDescLen--;
			description[nDescLen] = '\0';
		}
		if (nDescLen > 0 && description[nDescLen - 1] == '\r') {
			nDescLen--;
			description[nDescLen] = '\0';
		}
	} else {
		Q_strncpyz( description, modDir, descriptionLen );
	}

	if ( descHandle ) {
		FS_FCloseFile( descHandle );
	}
}

/*
================
FS_GetModList

Returns a list of mod directory names
A mod directory is a peer to baseq3 with a pk3 or pk3dir in it
The directories are searched in base path, cd path and home path
================
*/
int	FS_GetModList( char *listbuf, int bufsize ) {
	int i, j, k, pak;
	int nMods, nPotential;
	int nPaks, nDirs, nPakDirs;
	int nTotal, nLen, nDescLen;
	char **pFiles = NULL;
	char **pPaks = NULL;
	char **pDirs = NULL;
	char *name, *path;
	char description[MAX_OSPATH];

	int dummy;
	char **pFiles0 = NULL;
	char **pFiles1 = NULL;
	char **pFiles2 = NULL;
	qboolean bDrop = qfalse;

	*listbuf = 0;
	nMods = nTotal = 0;

	pFiles0 = Sys_ListFiles( fs_homepath->string, NULL, NULL, &dummy, qtrue );
	pFiles1 = Sys_ListFiles( fs_basepath->string, NULL, NULL, &dummy, qtrue );
	pFiles2 = Sys_ListFiles( fs_cdpath->string, NULL, NULL, &dummy, qtrue );
	// we searched for mods in the three paths
	// it is likely that we have duplicate names now, which we will cleanup below
	pFiles = Sys_ConcatenateFileLists( pFiles0, pFiles1, pFiles2 );

	nPotential = Sys_CountFileList(pFiles);

	for (i = 0; i < nPotential; i++) {
		name = pFiles[i];

		// NOTE: cleaner would involve more changes
		// ignore duplicate mod directories
		if (i != 0) {
			bDrop = qfalse;
			for (j = 0; j < i; j++) {
				if (Q_stricmp(pFiles[j], name) == 0) {
					// this one can be dropped
					bDrop = qtrue;
					break;
				}
			}
		}

		// we also drop "." and ".."
		if (bDrop || Q_stricmpn(name, ".", 1) == 0) {
			continue;
		}

		// in order to be a valid mod the directory must contain at least one .pk3 or .pk3dir
		// we didn't keep the information when we merged the directory names, as to what OS Path it was found under
		// so we will try each of them here
		path = FS_BuildOSPath( fs_homepath->string, name, "" );
		pFiles0 = Sys_ListFiles( path, ".pk3", NULL, &dummy, qfalse );

		path = FS_BuildOSPath( fs_basepath->string, name, "" );
		pFiles1 = Sys_ListFiles( path, ".pk3", NULL, &dummy, qfalse );

		path = FS_BuildOSPath( fs_cdpath->string, name, "" );
		pFiles2 = Sys_ListFiles( path, ".pk3", NULL, &dummy, qfalse );
		// we searched for paks in the three paths
		// it is possible that we have duplicate names now
		pPaks = Sys_ConcatenateFileLists( pFiles0, pFiles1, pFiles2 );

		nPaks = Sys_CountFileList( pPaks );

		// don't list the mod if only Spearmint patch pk3 files are found
		// example names: spearmint-patch-0.5.pk3, spearmint-baseq3-0.5.pk3
		for ( pak = 0; pak < nPaks; pak++ ) {
			if ( Q_stricmpn( pPaks[pak], "spearmint", 9 ) ) {
				// found a pk3 that isn't a Spearmint patch pk3
				break;
			}
		}

		if ( pak == nPaks ) {
			// all the paks are spearmint patches, treat as no paks.
			nPaks = 0;
		}

		Sys_FreeFileList( pPaks );

		nPakDirs = 0;

		// check for .pk3dir directories
		if ( nPaks == 0 ) {
			path = FS_BuildOSPath( fs_homepath->string, name, "" );
			pFiles0 = Sys_ListFiles( path, "/", NULL, &dummy, qfalse );

			path = FS_BuildOSPath( fs_basepath->string, name, "" );
			pFiles1 = Sys_ListFiles( path, "/", NULL, &dummy, qfalse );

			path = FS_BuildOSPath( fs_cdpath->string, name, "" );
			pFiles2 = Sys_ListFiles( path, "/", NULL, &dummy, qfalse );
			// we searched for paks in the three paths
			// it is possible that we have duplicate names now
			pDirs = Sys_ConcatenateFileLists( pFiles0, pFiles1, pFiles2 );

			nDirs = Sys_CountFileList( pDirs );

			// check if there is a directory ending with ".pk3dir"
			for (k = 0; k < nDirs; k++) {
				if (FS_IsExt(pDirs[k], ".pk3dir", strlen(pDirs[k]))) {
					nPakDirs = 1;
					break;
				}
			}

			Sys_FreeFileList( pDirs );
		}

		// if there was a game pk3 or .pk3dir, add mod to list
		if (nPaks > 0 || nPakDirs > 0) {
			nLen = strlen(name) + 1;
			// nLen is the length of the mod path
			// we need to see if there is a description available
			FS_GetModDescription( name, description, sizeof( description ) );
			nDescLen = strlen(description) + 1;

			if (nTotal + nLen + 1 + nDescLen + 1 < bufsize) {
				strcpy(listbuf, name);
				listbuf += nLen;
				strcpy(listbuf, description);
				listbuf += nDescLen;
				nTotal += nLen + nDescLen;
				nMods++;
			}
			else {
				Com_Printf( S_COLOR_YELLOW "WARNING: Ran out of space for mods in mod list (%d mods fit in the %d byte buffer)\n", nMods, bufsize );
				break;
			}
		}
	}
	Sys_FreeFileList( pFiles );

	return nMods;
}




//============================================================================

/*
================
FS_Dir_f
================
*/
void FS_Dir_f( void ) {
	char	*path;
	char	*extension;
	char	**dirnames;
	int		ndirs;
	int		i;

	if ( Cmd_Argc() < 2 || Cmd_Argc() > 3 ) {
		Com_Printf( "usage: dir <directory> [extension]\n" );
		return;
	}

	if ( Cmd_Argc() == 2 ) {
		path = Cmd_Argv( 1 );
		extension = "";
	} else {
		path = Cmd_Argv( 1 );
		extension = Cmd_Argv( 2 );
	}

	Com_Printf( "Directory of %s %s\n", path, extension );
	Com_Printf( "---------------\n" );

	dirnames = FS_GetFileList( path, extension, &ndirs, qfalse );

	for ( i = 0; i < ndirs; i++ ) {
		Com_Printf( "%s\n", dirnames[i] );
	}
	FS_FreeFileList( dirnames );
}

/*
===========
FS_ConvertPath
===========
*/
void FS_ConvertPath( char *s ) {
	while (*s) {
		if ( *s == '\\' || *s == ':' ) {
			*s = '/';
		}
		s++;
	}
}

/*
===========
FS_PathCmp

Ignore case and seprator char distinctions
===========
*/
int FS_PathCmp( const char *s1, const char *s2 ) {
	int		c1, c2;

	do {
		c1 = *s1++;
		c2 = *s2++;

		if (c1 >= 'a' && c1 <= 'z') {
			c1 -= ('a' - 'A');
		}
		if (c2 >= 'a' && c2 <= 'z') {
			c2 -= ('a' - 'A');
		}

		if ( c1 == '\\' || c1 == ':' ) {
			c1 = '/';
		}
		if ( c2 == '\\' || c2 == ':' ) {
			c2 = '/';
		}
		
		if (c1 < c2) {
			return -1;		// strings not equal
		}
		if (c1 > c2) {
			return 1;
		}
	} while (c1);
	
	return 0;		// strings are equal
}

/*
================
FS_NewDir_f
================
*/
void FS_NewDir_f( void ) {
	char	*filter;
	char	**dirnames;
	int		ndirs;
	int		i;

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "usage: fdir <filter>\n" );
		Com_Printf( "example: fdir *q3dm*.bsp\n");
		return;
	}

	filter = Cmd_Argv( 1 );

	Com_Printf( "---------------\n" );

	dirnames = FS_ListFilteredFiles( "", "", filter, &ndirs, qfalse );

	for ( i = 0; i < ndirs; i++ ) {
		FS_ConvertPath(dirnames[i]);
		Com_Printf( "%s\n", dirnames[i] );
	}
	Com_Printf( "%d files listed\n", ndirs );
	FS_FreeFileList( dirnames );
}

/*
============
FS_Path_f

============
*/
void FS_Path_f( void ) {
	searchpath_t	*s;
	int				i;

	Com_Printf ("Current search path:\n");
	for (s = fs_searchpaths; s; s = s->next) {
		if (s->pack) {
			Com_Printf ("%s (%i files)\n", s->pack->pakFilename, s->pack->numfiles);
			if ( fs_numServerPaks ) {
				if ( !FS_PakIsPure(s->pack) ) {
					Com_Printf( "    not on the pure list\n" );
				} else {
					Com_Printf( "    on the pure list\n" );
				}
			}
		} else if ( s->dir->qpath ) {
			Com_Printf ("%s%c%s\n", s->dir->fullpath, PATH_SEP, s->dir->qpath );
		} else {
			Com_Printf ("%s\n", s->dir->fullpath );
		}
	}


	Com_Printf( "\n" );
	for ( i = 1 ; i < MAX_FILE_HANDLES ; i++ ) {
		if ( fsh[i].handleFiles.file.o ) {
			Com_Printf( "handle %i: %s\n", i, fsh[i].name );
		}
	}
}

/*
============
FS_TouchFile_f
============
*/
void FS_TouchFile_f( void ) {
	fileHandle_t	f;

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: touchFile <file>\n" );
		return;
	}

	FS_FOpenFileRead( Cmd_Argv( 1 ), &f, qfalse );
	if ( f ) {
		FS_FCloseFile( f );
	}
}

/*
============
FS_Which
============
*/

qboolean FS_Which(const char *filename, void *searchPath)
{
	searchpath_t *search = searchPath;

	if(FS_FOpenFileReadDir(filename, search, NULL, qfalse, qfalse) > 0)
	{
		if(search->pack)
		{
			Com_Printf("File \"%s\" found in \"%s\"\n", filename, search->pack->pakFilename);
			return qtrue;
		}
		else if(search->dir)
		{
			Com_Printf( "File \"%s\" found at \"%s\"\n", filename, search->dir->fullpath);
			return qtrue;
		}
	}

	return qfalse;
}

/*
============
FS_Which_f
============
*/
void FS_Which_f( void ) {
	searchpath_t	*search;
	char		*filename;

	filename = Cmd_Argv(1);

	if ( !filename[0] ) {
		Com_Printf( "Usage: which <file>\n" );
		return;
	}

	// qpaths are not supposed to have a leading slash
	if ( filename[0] == '/' || filename[0] == '\\' ) {
		filename++;
	}

	// just wants to see if file is there
	for(search = fs_searchpaths; search; search = search->next)
	{
		if(FS_Which(filename, search))
			return;
	}

	Com_Printf("File not found: \"%s\"\n", filename);
}


//===========================================================================


static int QDECL paksort( const void *a, const void *b ) {
	char	*aa, *bb;

	aa = *(char **)a;
	bb = *(char **)b;

	return FS_PathCmp( aa, bb );
}

/*
================
FS_AddGameDirectory

Adds the directory to the head of the path,
then loads the zip headers
================
*/
void FS_AddGameDirectory( const char *path, const char *dir ) {
	searchpath_t	*sp;
	searchpath_t	*search;
	pack_t			*pak;
	char			curpath[MAX_OSPATH + 1], *pakfile;
	int				numfiles;
	char			**pakfiles;
	int				pakfilesi;
	char			**pakfilestmp;
	int				numdirs;
	char			**pakdirs;
	int				pakdirsi;
	char			**pakdirstmp;

	int				pakwhich;
	int				len;

	// find all pak files in this directory
	Q_strncpyz(curpath, FS_BuildOSPath(path, dir, ""), sizeof(curpath));
	curpath[strlen(curpath) - 1] = '\0';	// strip the trailing slash

	// Unique
	for ( sp = fs_searchpaths ; sp ; sp = sp->next ) {
		if ( sp->dir && !sp->dir->virtualDir && !Q_stricmp(sp->dir->fullpath, curpath)) {
			return;			// we've already got this one
		}
	}

	// Get .pk3 files
	pakfiles = Sys_ListFiles(curpath, ".pk3", NULL, &numfiles, qfalse);

	if ( pakfiles ) {
		qsort( pakfiles, numfiles, sizeof(char*), paksort );
	}

	if ( fs_numServerPaks ) {
		numdirs = 0;
		pakdirs = NULL;
	} else {
		// Get top level directories (we'll filter them later since the Sys_ListFiles filtering is terrible)
		pakdirs = Sys_ListFiles(curpath, "/", NULL, &numdirs, qfalse);

		qsort( pakdirs, numdirs, sizeof(char *), paksort );
	}

	pakfilesi = 0;
	pakdirsi = 0;

	while((pakfilesi < numfiles) || (pakdirsi < numdirs))
	{
		// Check if a pakfile or pakdir comes next
		if (pakfilesi >= numfiles) {
			// We've used all the pakfiles, it must be a pakdir.
			pakwhich = 0;
		}
		else if (pakdirsi >= numdirs) {
			// We've used all the pakdirs, it must be a pakfile.
			pakwhich = 1;
		}
		else {
			// Could be either, compare to see which name comes first
			// Need tmp variables for appropriate indirection for paksort()
			pakfilestmp = &pakfiles[pakfilesi];
			pakdirstmp = &pakdirs[pakdirsi];
			pakwhich = (paksort(pakfilestmp, pakdirstmp) < 0);
		}

		if (pakwhich) {
			// The next .pk3 file is before the next .pk3dir
			pakfile = FS_BuildOSPath(path, dir, pakfiles[pakfilesi]);
			if ((pak = FS_LoadZipFile(pakfile, pakfiles[pakfilesi])) == 0) {
				// This isn't a .pk3! Next!
				pakfilesi++;
				continue;
			}

			Q_strncpyz(pak->pakPathname, curpath, sizeof(pak->pakPathname));
			// store the game name for downloading
			Q_strncpyz(pak->pakGamename, dir, sizeof(pak->pakGamename));

			fs_packFiles += pak->numfiles;

			search = Z_Malloc(sizeof(searchpath_t));
			search->pack = pak;
			search->next = fs_searchpaths;
			fs_searchpaths = search;

			pakfilesi++;
		}
		else {
			// The next .pk3dir is before the next .pk3 file
			// But wait, this could be any directory, we're filtering to only ending with ".pk3dir" here.
			len = strlen(pakdirs[pakdirsi]);
			if (!FS_IsExt(pakdirs[pakdirsi], ".pk3dir", len)) {
				// This isn't a .pk3dir! Next!
				pakdirsi++;
				continue;
			}

			pakfile = FS_BuildOSPath(path, dir, pakdirs[pakdirsi]);

			// add the directory to the search path
			search = Z_Malloc(sizeof(searchpath_t));
			search->dir = Z_Malloc(sizeof(*search->dir));

			Q_strncpyz(search->dir->fullpath, pakfile, sizeof(search->dir->fullpath));	// c:\quake3\baseq3\mypak.pk3dir
			Q_strncpyz(search->dir->gamedir, dir, sizeof(search->dir->gamedir)); // baseq3
			search->dir->virtualDir = qtrue;

			search->next = fs_searchpaths;
			fs_searchpaths = search;

			pakdirsi++;
		}
	}

	// done
	Sys_FreeFileList( pakfiles );
	Sys_FreeFileList( pakdirs );

	//
	// add the directory to the search path
	//
	search = Z_Malloc (sizeof(searchpath_t));
	search->dir = Z_Malloc( sizeof( *search->dir ) );

	Q_strncpyz(search->dir->fullpath, curpath, sizeof(search->dir->fullpath));
	Q_strncpyz(search->dir->gamedir, dir, sizeof(search->dir->gamedir));
	search->dir->virtualDir = qfalse;

	search->next = fs_searchpaths;
	fs_searchpaths = search;
}

/*
================
FS_ReferencedPakType
================
*/
pakType_t FS_ReferencedPakType( const char *name, int checksum, qboolean *pInstalled ) {
	searchpath_t *sp;
	int i;
	char *slash;
	char gamedir[MAX_OSPATH];
	char pakname[MAX_OSPATH];

	for ( sp = fs_searchpaths ; sp ; sp = sp->next ) {
		if ( sp->pack && sp->pack->checksum == checksum ) {
			if ( pInstalled ) {
				*pInstalled = qtrue;
			}
			return sp->pack->pakType;
		}
	}

	// split name into gamedir and pakname
	Q_strncpyz( gamedir, name, sizeof ( gamedir ) );
	slash = strchr( gamedir, '/' );
	if ( !slash ) {
		slash = strchr( gamedir, '\\' );
	}
	if ( slash != NULL ) {
		*slash = '\0';

		Q_strncpyz( pakname, slash + 1, MIN( (int)(slash - name), sizeof ( gamedir ) ) );

		// used by client when downloading pk3s for a gamedir not in the current search path
		for ( sp = fs_searchpaths ; sp ; sp = sp->next ) {
			if ( sp->dir && !sp->dir->virtualDir && !Q_stricmp( sp->dir->gamedir, gamedir ) ) {
				pack_t *thepak;
				int zipchecksum;
				char *zippath;

				zippath = FS_BuildOSPath( sp->dir->fullpath, NULL, va( "%s.%08x.pk3", pakname, checksum ) );
				thepak = FS_LoadZipFile( zippath, "");

				if ( !thepak ) {
					zippath = FS_BuildOSPath( sp->dir->fullpath, NULL, va( "%s.pk3", pakname ) );
					thepak = FS_LoadZipFile( zippath, "");
				}

				if ( thepak ) {
					zipchecksum = thepak->checksum;
					FS_FreePak(thepak);

					if ( zipchecksum == checksum ) {
						if ( pInstalled ) {
							*pInstalled = qtrue;
						}
						// this probably isn't correct type for pak but client just needs to know we already have it
						return PAK_NO_DOWNLOAD;
					}
				}
			}
		}
	}

	if ( pInstalled ) {
		*pInstalled = qfalse;
	}

	// prevent client from trying to downloading commericial paks that aren't installed (ignore checksum)
	for ( i = 0; i < numCommercialPaks; ++i ) {
		if ( /*commercialPaks[i].checksum == checksum
			&&*/ Q_stricmp( name, va( "%s/%s", commercialPaks[i].gamename, commercialPaks[i].basename ) ) == 0 ) {
			return PAK_COMMERCIAL;
		}
	}

	// make client respect the local nodownload list (ignore checksum)
	for ( i = 0 ; i < fs_numPaksums ; i++ ) {
		if ( /*com_purePaks[i].checksum == checksum
			&&*/ Q_stricmp( name, va( "%s/%s", com_purePaks[i].gamename, com_purePaks[i].pakname ) ) == 0 ) {
			return PAK_NO_DOWNLOAD;
		}
	}

	return PAK_UNKNOWN;
}

/*
================
FS_CheckDirTraversal

Check whether the string contains stuff like "../" to prevent directory traversal bugs
and return qtrue if it does.
================
*/

qboolean FS_CheckDirTraversal(const char *checkdir)
{
	if(strstr(checkdir, "../") || strstr(checkdir, "..\\"))
		return qtrue;
	
	return qfalse;
}

/*
================
FS_InvalidGameDir

return true if path is a reference to current directory or directory traversal
or a sub-directory
================
*/
qboolean FS_InvalidGameDir( const char *gamedir ) {
	if ( !strcmp( gamedir, "." ) || !strcmp( gamedir, ".." )
		|| strchr( gamedir, '/' ) || strchr( gamedir, '\\' ) ) {
		return qtrue;
	}

	return qfalse;
}

/*
================
FS_ComparePaks

----------------
dlstring == qtrue

Returns a list of pak files that we should download from the server. They all get stored
in the current gamedir and an FS_Restart will be fired up after we download them all.

The string is the format:

@remotename@localname [repeat]

static int		fs_numServerReferencedPaks;
static int		fs_serverReferencedPaks[MAX_SEARCH_PATHS];
static char		*fs_serverReferencedPakNames[MAX_SEARCH_PATHS];

----------------
dlstring == qfalse

we are not interested in a download string format, we want something human-readable
(this is used for diagnostics while connecting to a pure server)

================
*/
qboolean FS_ComparePaks( char *neededpaks, int len, qboolean dlstring ) {
	pakType_t		pakType;
	qboolean		installed;
	char *origpos = neededpaks;
	int i;

	if (!fs_numServerReferencedPaks)
		return qfalse; // Server didn't send any pack information along

	*neededpaks = 0;

	for ( i = 0 ; i < fs_numServerReferencedPaks ; i++ )
	{
		// Ok, see if we have this pak file
		pakType = FS_ReferencedPakType( fs_serverReferencedPakNames[i], fs_serverReferencedPaks[ i ], &installed );

		if ( installed )
		{
			continue;
		}

		// never autodownload any of the id paks
		// if missing commericial pak, don't download any paks
		// if pak exists with wrong checksum, allow downloading
		// FIXME: what if they unzipped the paks? maybe don't download if missing default.cfg? not having a way to know "is this game installed?" sucks.
		if ( pakType == PAK_COMMERCIAL && dlstring )
		{
			if ( FS_SV_FileExists( va( "%s.pk3", fs_serverReferencedPakNames[i] ) ) )
			{
				// pak exists with wrong checksum, don't try to download it
				continue;
			}
			else
			{
				// don't download addons if the base game is missing.
				*neededpaks = 0;
				return qfalse;
			}
		}

		if ( pakType == PAK_NO_DOWNLOAD && dlstring ) {
			continue;
		}

		// Make sure the server cannot make us write to non-quake3 directories.
		if(FS_CheckDirTraversal(fs_serverReferencedPakNames[i]))
		{
			Com_Printf("WARNING: Invalid download name %s\n", fs_serverReferencedPakNames[i]);
			continue;
		}

		if ( fs_serverReferencedPakNames[i] && *fs_serverReferencedPakNames[i] ) { 
			// Don't got it

			if (dlstring)
			{
				// We need this to make sure we won't hit the end of the buffer or the server could
				// overwrite non-pk3 files on clients by writing so much crap into neededpaks that
				// Q_strcat cuts off the .pk3 extension.

				origpos += strlen(origpos);

				// Remote name
				Q_strcat( neededpaks, len, "@");
				Q_strcat( neededpaks, len, fs_serverReferencedPakNames[i] );
				Q_strcat( neededpaks, len, ".pk3" );

				// Local name
				Q_strcat( neededpaks, len, "@");
				// Do we have one with the same name?
				if ( FS_SV_FileExists( va( "%s.pk3", fs_serverReferencedPakNames[i] ) ) )
				{
					char st[MAX_ZPATH];
					// We already have one called this, we need to download it to another name
					// Make something up with the checksum in it
					Com_sprintf( st, sizeof( st ), "%s.%08x.pk3", fs_serverReferencedPakNames[i], fs_serverReferencedPaks[i] );
					Q_strcat( neededpaks, len, st );
				}
				else
				{
					Q_strcat( neededpaks, len, fs_serverReferencedPakNames[i] );
					Q_strcat( neededpaks, len, ".pk3" );
				}

				// Find out whether it might have overflowed the buffer and don't add this file to the
				// list if that is the case.
				if(strlen(origpos) + (origpos - neededpaks) >= len - 1)
				{
					*origpos = '\0';
					break;
				}
			}
			else
			{
				Q_strcat( neededpaks, len, fs_serverReferencedPakNames[i] );
				Q_strcat( neededpaks, len, ".pk3" );
				// Do we have one with the same name?
				if ( FS_SV_FileExists( va( "%s.pk3", fs_serverReferencedPakNames[i] ) ) )
				{
					Q_strcat( neededpaks, len, " (local file exists with wrong checksum)");
				}
				else if ( pakType == PAK_COMMERCIAL )
				{
					Q_strcat( neededpaks, len, " (commercial pak, downloading not allowed)");
				}
				else if ( pakType == PAK_NO_DOWNLOAD )
				{
					Q_strcat( neededpaks, len, " (downloading not allowed)");
				}
				Q_strcat( neededpaks, len, "\n");
			}
		}
	}

	if ( *neededpaks ) {
		return qtrue;
	}

	return qfalse; // We have them all
}

/*
================
FS_Shutdown

Frees all resources.
================
*/
void FS_Shutdown( qboolean closemfp ) {
	searchpath_t	*p, *next;
	int	i;

	for(i = 0; i < MAX_FILE_HANDLES; i++) {
		if (fsh[i].fileSize) {
			FS_FCloseFile(i);
		}
	}

	// free everything
	for(p = fs_searchpaths; p; p = next)
	{
		next = p->next;

		if(p->pack)
			FS_FreePak(p->pack);
		if (p->dir)
			Z_Free(p->dir);

		Z_Free(p);
	}

	// any FS_ calls will now be an error until reinitialized
	fs_searchpaths = NULL;

	Cmd_RemoveCommand( "path" );
	Cmd_RemoveCommand( "dir" );
	Cmd_RemoveCommand( "fdir" );
	Cmd_RemoveCommand( "touchFile" );
	Cmd_RemoveCommand( "which" );

#ifdef FS_MISSING
	if (closemfp) {
		fclose(missingFiles);
	}
#endif
}

/*
===================
FS_StashSearchPath
===================
*/
void FS_StashSearchPath(void) {
	if ( fs_stashedPath ) {
		Com_Error( ERR_FATAL, "FS_StashSearchPath cannot stash multiple times" );
	}
	fs_stashedPath = fs_searchpaths;
	fs_searchpaths = NULL;
}

/*
===================
FS_UnstashSearchPath

Add stashed search paths to head of current search paths
===================
*/
void FS_UnstashSearchPath(void) {
	searchpath_t *p;

	if ( !fs_stashedPath ) {
		Com_Error( ERR_FATAL, "FS_UnstashSearchPath has nothing to unstash" );
	}

	if ( fs_searchpaths ) {
		for (p = fs_stashedPath; p->next; p = p->next) {
		}

		p->next = fs_searchpaths;
	}

	fs_searchpaths = fs_stashedPath;
	fs_stashedPath = NULL;
}

/*
================
FS_ReorderPurePaks
NOTE TTimo: the reordering that happens here is not reflected in the cvars (\cvarlist *pak*)
  this can lead to misleading situations, see https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=540
================
*/
static void FS_ReorderPurePaks( void )
{
	searchpath_t *s;
	int i;
	searchpath_t **p_insert_index, // for linked list reordering
		**p_previous; // when doing the scan

	fs_reordered = qfalse;

	// only relevant when connected to pure server
	if ( !fs_numServerPaks )
		return;

	p_insert_index = &fs_searchpaths; // we insert in order at the beginning of the list
	for ( i = 0 ; i < fs_numServerPaks ; i++ ) {
		p_previous = p_insert_index; // track the pointer-to-current-item
		for (s = *p_insert_index; s; s = s->next) {
			// the part of the list before p_insert_index has been sorted already
			if (s->pack && fs_serverPaks[i] == s->pack->checksum) {
				fs_reordered = qtrue;
				// move this element to the insert list
				*p_previous = s->next;
				s->next = *p_insert_index;
				*p_insert_index = s;
				// increment insert list
				p_insert_index = &s->next;
				break; // iterate to next server pack
			}
			p_previous = &s->next;
		}
	}
}

/*
================
FS_PortableHomePath

If a settings directory exists in same directory as binary, return path to it.
================
*/
const char *FS_PortableHomePath(void) {
	static char settings[ MAX_OSPATH ];

	Com_sprintf( settings, sizeof( settings ), "%s%csettings", Sys_DefaultInstallPath(), PATH_SEP );

	if ( Sys_StatFile( settings ) != 1 ) {
		return NULL;
	}

	return settings;
}

/*
================
FS_DefaultBaseGame

Check list of base game overrides.
================
*/
static char defaultBaseGame[ MAX_OSPATH ] = { 0 };
const char *FS_DefaultBaseGame( void ) {
	char path[ MAX_OSPATH ];
	FILE *filep;

	// only check once
	if ( defaultBaseGame[0] ) {
		return defaultBaseGame;
	}

	// default to base game from compile time
	Q_strncpyz( defaultBaseGame, BASEGAME, sizeof ( defaultBaseGame ) );

	filep = NULL;

	// check for file with list of overrides
	if ( !filep && fs_homepath->string[0] ) {
		Com_sprintf( path, sizeof( path ), "%s%c%s", fs_homepath->string, PATH_SEP, MINT_GAMELIST );
		filep = Sys_FOpen( path, "rb" );
	}
#ifdef __APPLE__
	if ( !filep && fs_apppath->string[0] ) {
		Com_sprintf( path, sizeof( path ), "%s%c%s", fs_apppath->string, PATH_SEP, MINT_GAMELIST );
		filep = Sys_FOpen( path, "rb" );
	}
#endif
	if ( !filep && fs_basepath->string[0] ) {
		Com_sprintf( path, sizeof( path ), "%s%c%s", fs_basepath->string, PATH_SEP, MINT_GAMELIST );
		filep = Sys_FOpen( path, "rb" );
	}
	if ( !filep && fs_cdpath->string[0] ) {
		Com_sprintf( path, sizeof( path ), "%s%c%s", fs_cdpath->string, PATH_SEP, MINT_GAMELIST );
		filep = Sys_FOpen( path, "rb" );
	}

	// found file
	if ( filep ) {
		int			len = FS_fplength( filep );
		char		*buf, *text_p, *token;
		char		gamedir[MAX_OSPATH];
		char		file[MAX_OSPATH];
		qboolean	firstLine = qtrue;
		qboolean	foundGameDir = qfalse;

		buf = Hunk_AllocateTempMemory(len+1);
		buf[len] = 0;

		fread( buf, 1, len, filep );
		fclose( filep );

		text_p = buf;

		while ( 1 ) {
			if ( firstLine ) {
				firstLine = qfalse;
			} else {
				// skip the rest in case want to add additional options here later
				SkipRestOfLine( &text_p );
			}

			token = COM_Parse( &text_p );
			if ( !*token )
				break;

			if ( Q_stricmp( token, "tryGameDir" ) == 0 ) {
				token = COM_ParseExt( &text_p, qfalse );
				if ( !*token ) {
					Com_Printf( S_COLOR_YELLOW "WARNING: missing gamedir for 'tryGameDir' in %s\n", path );
					continue;
				}

				Q_strncpyz( gamedir, token, sizeof ( gamedir ) );

				if ( FS_CheckDirTraversal( gamedir ) ) {
					Com_Printf( S_COLOR_YELLOW "WARNING: invalid gamedir %s in %s\n", gamedir, path );
					continue;
				}

				token = COM_ParseExt( &text_p, qfalse );
				if ( !*token ) {
					Com_Printf( S_COLOR_YELLOW "WARNING: missing file for gamedir %s in %s\n", gamedir, path );
					continue;
				}

				Q_strncpyz( file, token, sizeof ( file ) );

				if ( FS_CheckDirTraversal( file ) ) {
					Com_Printf( S_COLOR_YELLOW "WARNING: invalid file %s in %s\n", file, path );
					continue;
				}

				// if already found game, just finish parsing the file for error checking.
				if ( foundGameDir ) {
					continue;
				}

				if ( ( fs_homepath->string[0] && FS_FileInPathExists( FS_BuildOSPath( fs_homepath->string, gamedir, file ) ) )
#ifdef __APPLE__
					|| ( fs_apppath->string[0] && FS_FileInPathExists( FS_BuildOSPath( fs_apppath->string, gamedir, file ) ) )
#endif
					|| ( fs_basepath->string[0] && FS_FileInPathExists( FS_BuildOSPath( fs_basepath->string, gamedir, file ) ) )
					|| ( fs_cdpath->string[0] && FS_FileInPathExists( FS_BuildOSPath( fs_cdpath->string, gamedir, file ) ) ) ) {
					Com_Printf( "Using '%s' as default game as specified in %s.\n", gamedir, path );
					Q_strncpyz( defaultBaseGame, gamedir, sizeof ( defaultBaseGame ) );
					foundGameDir = qtrue;
				}
			} else {
				Com_Printf( S_COLOR_YELLOW "WARNING: Unknown token '%s' in %s\n", token, path );
			}
		}

		Hunk_FreeTempMemory( buf );
	}

	return defaultBaseGame;
}

/*
===================
FS_LoadGameConfig

Load additional search paths, from current search path

Add gameDirs in forward search order

Example: A mod for Quake III: Team Arena would have
gameDirs[0]: missionpack
gameDirs[1]: baseq3
===================
*/
static qboolean FS_LoadGameConfig( gameConfig_t *config, const char *gameDir, qboolean mainGameDir ) {
	union {
		char *c;
		void *v;
	} buffer;
	int				len;
	char			*text_p, *token;
	char			path[MAX_QPATH];
	char			cvarName[256], cvarValue[256];
	qboolean		firstLine = qtrue;
#ifndef DEDICATED
	loadingScreen_t	*screen;

	// default to ioq3 defaultSound
	if ( mainGameDir ) {
		Q_strncpyz( config->defaultSound, "sound/feedback/hit.wav", sizeof (config->defaultSound) );
	}
#endif

	Q_strncpyz( path, GAMESETTINGS, sizeof (path) );
	len = FS_ReadFileFromGameDir( path, &buffer.v, gameDir );

	if ( len <= 0 ) {
		return qfalse;
	}

	fs_foundPaksums = qtrue;

	text_p = buffer.c;

	while ( 1 ) {
		if ( firstLine ) {
			firstLine = qfalse;
		} else {
			// skip the rest in case want to add additional options here later
			SkipRestOfLine( &text_p );
		}

		token = COM_Parse( &text_p );
		if ( !*token )
			break;

		if ( Q_stricmp( token, "paksums" ) == 0 ) {
			// XXX
			extern void FS_ParsePakChecksums( char **text, const char *path, const char *gameDir );

			FS_ParsePakChecksums( &text_p, path, gameDir );
			continue;
		}

		if ( !mainGameDir )
			continue;

		if ( Q_stricmp( token, "addGameDir" ) == 0 ) {
			token = COM_ParseExt( &text_p, qfalse );
			if ( !*token ) {
				Com_Printf( S_COLOR_YELLOW "WARNING: missing gamedir for 'addGameDir' in %s\n", path );
				continue;
			}

			if ( FS_CheckDirTraversal( token ) ) {
				Com_Printf( S_COLOR_YELLOW "WARNING: invalid gamedir %s in %s\n", token, path );
				continue;
			}

			if ( config->numGameDirs >= MAX_GAMEDIRS ) {
				Com_Printf( "WARNING: Excessive game directories in %s (max is %d)\n", path, MAX_GAMEDIRS );
			} else if ( Q_stricmp( fs_gamedirvar->string, token ) == 0 ) {
				Com_Printf( "WARNING: Ignoring gamedir %s listed in %s (it's the current fs_game)\n", token, path );
			} else {
				Q_strncpyz( config->gameDirs[config->numGameDirs], token, sizeof (config->gameDirs[0]) );
				config->numGameDirs++;
			}
		} else if ( Q_stricmp( token, "cvarDefault" ) == 0 ) {
			// cvar name
			token = COM_ParseExt( &text_p, qfalse );
			if ( !*token )
				continue;
			Q_strncpyz( cvarName, token, sizeof (cvarName) );

			// value
			token = COM_ParseExt( &text_p, qfalse );
			if ( !*token )
				continue;
			Q_strncpyz( cvarValue, token, sizeof (cvarValue) );

			Cvar_SetDefault( cvarName, cvarValue );
		} else if ( Q_stricmp( token, "defaultSound" ) == 0 ) {
#ifndef DEDICATED
			token = COM_ParseExt( &text_p, qfalse );
			if ( !*token )
				continue;

			Q_strncpyz( config->defaultSound, token, sizeof (config->defaultSound) );
#endif
		} else if ( Q_stricmp( token, "addLoadingScreen" ) == 0 ) {
#ifndef DEDICATED
			// Example: addLoadingScreen menuback ( 0 0 0 ) 1.33333
			token = COM_ParseExt( &text_p, qfalse );
			if ( !*token )
				continue;

			if ( config->numLoadingScreens >= MAX_LOADINGSCREENS ) {
				Com_Printf( "WARNING: Excessive loading screens in %s (max is %d)\n", path, MAX_LOADINGSCREENS );
				continue;
			}

			screen = &config->loadingScreens[ config->numLoadingScreens ];

			config->numLoadingScreens++;

			Q_strncpyz( screen->shaderName, token, sizeof (screen->shaderName) );

			Parse1DMatrix( &text_p, 3, screen->color );

			token = COM_ParseExt( &text_p, qfalse );
			if ( !*token ) {
				screen->aspect = 1;
				continue;
			}

			screen->aspect = atof( token );
#endif
		} else {
			Com_Printf( S_COLOR_YELLOW "WARNING: Unknown token '%s' in %s\n", token, path );
		}
	}

	FS_FreeFile( buffer.v );
	return qtrue;
}

/*
================
FS_AddQPathDirectory

Add a directory as a qpath.
For example, searching in c:\quake3\fonts (path C:\quake3, qpath fonts).
================
*/
void FS_AddQPathDirectory( const char *path, const char *qpath ) {
	searchpath_t *search, *sp;

	// Unique
	for ( sp = fs_searchpaths ; sp ; sp = sp->next ) {
		if ( sp->dir && sp->dir->virtualDir && !Q_stricmp(sp->dir->fullpath, path) && !Q_stricmp(sp->dir->qpath, qpath)) {
			return;			// we've already got this one
		}
	}

	//
	// add the directory to the search path
	//
	search = Z_Malloc (sizeof(searchpath_t));
	search->dir = Z_Malloc( sizeof( *search->dir ) );

	Q_strncpyz(search->dir->fullpath, path, sizeof(search->dir->fullpath));
	Q_strncpyz(search->dir->gamedir, "", sizeof(search->dir->gamedir));
	Q_strncpyz(search->dir->qpath, qpath, sizeof(search->dir->gamedir));
	search->dir->virtualDir = qtrue;

	search->next = fs_searchpaths;
	fs_searchpaths = search;
}

/*
================
FS_AddDirectory
================
*/
void FS_AddDirectory( const char *qpath ) {
	// add search path elements in reverse priority order
	if (fs_cdpath->string[0]) {
		FS_AddQPathDirectory( fs_cdpath->string, qpath );
	}
	if (fs_basepath->string[0]) {
		FS_AddQPathDirectory( fs_basepath->string, qpath );
	}

#ifdef __APPLE__
	// Make MacOSX also include the base path included with the .app bundle
	if (fs_apppath->string[0])
		FS_AddQPathDirectory( fs_apppath->string, qpath );
#endif

	// fs_homepath is somewhat particular to *nix systems, only add if relevant
	if (fs_homepath->string[0] && Q_stricmp(fs_homepath->string,fs_basepath->string)) {
		FS_AddQPathDirectory ( fs_homepath->string, qpath );
	}
}

/*
================
FS_AddGame
================
*/
static void FS_AddGame( const char *gameName ) {
	// add search path elements in reverse priority order
	if (fs_cdpath->string[0]) {
		FS_AddGameDirectory( fs_cdpath->string, gameName );
	}
	if (fs_basepath->string[0]) {
		FS_AddGameDirectory( fs_basepath->string, gameName );
	}

#ifdef __APPLE__
	// Make MacOSX also include the base path included with the .app bundle
	if (fs_apppath->string[0])
		FS_AddGameDirectory( fs_apppath->string, gameName );
#endif

	// fs_homepath is somewhat particular to *nix systems, only add if relevant
	if (fs_homepath->string[0] && Q_stricmp(fs_homepath->string,fs_basepath->string)) {
		FS_CreatePath ( fs_homepath->string );
		FS_AddGameDirectory ( fs_homepath->string, gameName );
	}

	FS_LoadGameConfig( &com_gameConfig, gameName, !Q_stricmp( gameName, fs_gamedirvar->string ) );
}

// XXX
void FS_ClearPakChecksums( void );
static void FS_CheckPaks( qboolean quiet );

/*
================
FS_Startup
================
*/
static void FS_Startup( qboolean quiet )
{
	const char	*homePath;
	char		description[MAX_QPATH];
	int			i;

	if (!quiet) {
		Com_Printf( "----- FS_Startup -----\n" );
	}

	fs_packFiles = 0;

	fs_debug = Cvar_Get( "fs_debug", "0", 0 );
	fs_cdpath = Cvar_Get ("fs_cdpath", "", CVAR_INIT|CVAR_PROTECTED );
	fs_basepath = Cvar_Get ("fs_basepath", Sys_DefaultInstallPath(), CVAR_INIT|CVAR_PROTECTED );
#ifdef __APPLE__
	fs_apppath = Cvar_Get ("fs_apppath", Sys_DefaultAppPath(), CVAR_INIT|CVAR_PROTECTED );
#endif
	homePath = FS_PortableHomePath();
	if (!homePath || !homePath[0]) {
		homePath = Sys_DefaultHomePath();
	}
	if (!homePath || !homePath[0]) {
		homePath = fs_basepath->string;
	}
	fs_homepath = Cvar_Get ("fs_homepath", homePath, CVAR_INIT|CVAR_PROTECTED );
	fs_gamedirvar = Cvar_Get ("fs_game", FS_DefaultBaseGame(), CVAR_INIT|CVAR_SYSTEMINFO|CVAR_SERVERINFO );

	if(!fs_gamedirvar->string[0])
		Cvar_ForceReset("fs_game");

	if (FS_InvalidGameDir(fs_gamedirvar->string)) {
		Com_Error( ERR_DROP, "Invalid fs_game '%s'", fs_gamedirvar->string );
	}

	FS_ClearPakChecksums();
	Com_Memset( &com_gameConfig, 0, sizeof (com_gameConfig) );

	FS_AddGame( fs_gamedirvar->string );

	if ( com_gameConfig.numGameDirs > 0 ) {
		// hide fs_game path
		FS_StashSearchPath();

		// add extra gamedirs in reverse order
		for ( i = com_gameConfig.numGameDirs-1; i >= 0; --i ) {
			FS_AddGame( com_gameConfig.gameDirs[i] );
		}

		// put fs_game at the head of list
		FS_UnstashSearchPath();
	}

	//
	// Add fonts at the end of the search path.
	//
	// hide game paths
	FS_StashSearchPath();
	// add top-level fonts directory
	FS_AddDirectory( "fonts" );
	// add game paths to beginning of list
	FS_UnstashSearchPath();

	Q_strncpyz( fs_gamedir, fs_gamedirvar->string, sizeof( fs_gamedir ) );

	FS_GetModDescription( fs_gamedir, description, sizeof ( description ) );
	Q_CleanStr( description );
	Cvar_Set( "com_productName", description );

	// add our commands
	Cmd_AddCommand ("path", FS_Path_f);
	Cmd_AddCommand ("dir", FS_Dir_f );
	Cmd_AddCommand ("fdir", FS_NewDir_f );
	Cmd_AddCommand ("touchFile", FS_TouchFile_f );
	Cmd_AddCommand ("which", FS_Which_f );

	FS_CheckPaks( quiet );

	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=506
	// reorder the pure pk3 files according to server order
	FS_ReorderPurePaks();

	fs_gamedirvar->modified = qfalse; // We just loaded, it's not modified

#ifdef FS_MISSING
	if (missingFiles == NULL) {
		missingFiles = Sys_FOpen( "\\missing.txt", "ab" );
	}
#endif

	if (!quiet) {
		// print the current search paths
		FS_Path_f();

		Com_Printf( "----------------------\n" );
		Com_Printf( "%d files in pk3 files\n", fs_packFiles );
	}
}

/*
===================
FS_DetectCommercialPaks

Do not remove!
===================
*/
void FS_DetectCommercialPaks( void ) {
	searchpath_t *path;
	int i;

	for( path = fs_searchpaths; path; path = path->next ) {
		if ( !path->pack )
			continue;

		for ( i = 0; i < numCommercialPaks; ++i ) {
			if ( /*path->pack->checksum == commercialPaks[i].checksum
				&& */Q_stricmp( path->pack->pakGamename, commercialPaks[i].gamename ) == 0
				&& Q_stricmp( path->pack->pakBasename, commercialPaks[i].basename ) == 0 ) {
				//Com_DPrintf("Found commercial pak: %s (original %s%c%s)\n", path->pack->pakFilename,
				//		   commercialPaks[i].gamename, PATH_SEP, commercialPaks[i].basename );
				path->pack->pakType = PAK_COMMERCIAL;
				break;
			}
		}

		if ( i == numCommercialPaks ) {
			path->pack->pakType = PAK_FREE;
		}
	}
}

/*
===================
FS_AddModToList
===================
*/
void FS_AddModToList( char *list, int listLen, const char *gamedir ) {
	char gameDescription[MAX_QPATH];

	FS_GetModDescription( gamedir, gameDescription, sizeof ( gameDescription ) );

	if ( !strstr( list, gameDescription ) ) {
		if ( strlen( list ) ) {
			Q_strcat( list, listLen, ", " );
		}

		Q_strcat( list, listLen, gameDescription );
	}
}

/*
===================
FS_ClearPakChecksums
===================
*/
void FS_ClearPakChecksums( void ) {
	fs_foundPaksums = qfalse;
	fs_numPaksums = 0;
}

/*
===================
FS_ParsePakChecksums

Load checksums for pk3 files in gamedir
===================
*/
void FS_ParsePakChecksums( char **text, const char *path, const char *gameDir ) {
	char			*token;

	token = COM_Parse( text );
	if ( *token != '{' ) {
		Com_Error( ERR_DROP, "Missing opening brace for paksums in %s\n", path );
	}

	while ( 1 ) {
		token = COM_Parse( text );
		if ( *token == '}' )
			break;
		if ( !*token )
			Com_Error( ERR_DROP, "Missing closing brace for paksums in %s", path );

		if ( fs_numPaksums >= MAX_PAKSUMS )
			Com_Error( ERR_DROP, "Too many paksums (max is %d) in %s", MAX_PAKSUMS, path );

		COM_StripExtension( token, com_purePaks[fs_numPaksums].pakname, sizeof ( com_purePaks[fs_numPaksums].pakname ) );

		// read checksum
		token = COM_ParseExt( text, qfalse );
		if ( !*token ) {
			Com_Printf( "'%s' missing checksum in %s\n", com_purePaks[fs_numPaksums].pakname, path );
			continue;
		}
		com_purePaks[fs_numPaksums].checksum = strtoul(token, NULL, 10);

		// read optional keywords
		com_purePaks[fs_numPaksums].nodownload = qfalse;
		com_purePaks[fs_numPaksums].optional = qfalse;
		while ( 1 ) {
			token = COM_ParseExt( text, qfalse );
			if ( !*token )
				break;
			if ( Q_stricmp( token, "nodownload" ) == 0 ) {
				com_purePaks[fs_numPaksums].nodownload = qtrue;
			} else if ( Q_stricmp( token, "optional" ) == 0 ) {
				com_purePaks[fs_numPaksums].optional = qtrue;
			} else {
				Com_Printf( "Unknown pak keyword '%s' in %s\n", token, path );
			}
		}

		Q_strncpyz( com_purePaks[fs_numPaksums].gamename, gameDir, sizeof ( com_purePaks[fs_numPaksums].gamename ) );
		fs_numPaksums++;
	}
}

/*
================
FS_PaksumsSortValue
================
*/
int FS_PaksumsSortValue( const searchpath_t *s ) {
	int pak;

	if ( s->pack ) {
		for ( pak = 0 ; pak < fs_numPaksums ; pak++ ) {
			// skip default paks for different game directories.
			if ( Q_stricmp( com_purePaks[pak].gamename, s->pack->pakGamename ) != 0 ) {
				continue;
			}

			// if pak has checksum or name of default pak use explicit sort order defined by
			// mint-game.settings.
			if ( com_purePaks[pak].checksum == s->pack->checksum
				|| Q_stricmp( com_purePaks[pak].pakname, s->pack->pakBasename ) == 0 ) {
				return pak;
			}
		}
	}

	// real-game directories are immovable, .pk3dir sort last like pk3s
	if ( s->dir && !s->dir->virtualDir ) {
		return -1;
	}

	// unknown pk3s are sorted after ones listed in mint-game.settings.
	return fs_numPaksums;
}

/*
================
FS_ReorderPaksumsPaks
================
*/
static void FS_ReorderPaksumsPaks( void )
{
	searchpath_t	*s, *tmp, *previous_s;
	qboolean		swapped;

	// only relevant when have paksums and
	// not relevant if connecting to pure server
	if ( !fs_numPaksums || fs_numServerPaks )
		return;

	while ( 1 ) {
		previous_s = NULL;
		swapped = qfalse;
		for ( s = fs_searchpaths; s && s->next; s = s->next ) {
			// check if s should be after s->next
			if ( s->pack && FS_PaksumsSortValue(s) < FS_PaksumsSortValue(s->next) ) {
				// swap order of s and s->next
				tmp = s->next->next;
				s->next->next = s;

				if ( previous_s ) {
					previous_s->next = s->next;
				}

				s->next = tmp;

				swapped = qtrue;
			}
			previous_s = s;
		}
		if (!swapped) {
			break;
		}
	}
}

/*
===================
FS_CheckPaks

Checks that default pk3s are present and their checksums are correct
===================
*/
static void FS_CheckPaks( qboolean quiet )
{
	searchpath_t	*path;
	qboolean		missingPak = qfalse;
	qboolean		invalidPak = qfalse;
	char			badGames[512];
	int				pak;

	badGames[0] = '\0';

	FS_DetectCommercialPaks();
	FS_ReorderPaksumsPaks();

	for (pak = 0; pak < fs_numPaksums; pak++) {
		for( path = fs_searchpaths; path; path = path->next ) {
			if ( path->pack && Q_stricmp( path->pack->pakGamename, com_purePaks[pak].gamename ) == 0
				&& Q_stricmp( path->pack->pakBasename, com_purePaks[pak].pakname ) == 0 ) {
				// reference all non-optional default paks
				if ( !com_purePaks[pak].optional ) {
					path->pack->referenced = qtrue;
				}

				// commercial paks cannot be downloaded either, don't change their type
				if ( path->pack->pakType != PAK_COMMERCIAL && com_purePaks[pak].nodownload ) {
					path->pack->pakType = PAK_NO_DOWNLOAD;
				}
				break;
			}
		}

		if ( !path ) {
			if ( com_purePaks[pak].optional ) {
				// ignore missing optional paks
				continue;
			}

			if ( !quiet ) {
				Com_Printf("\n\n"
						"**********************************************************************\n"
						"WARNING: %s/%s.pk3 is missing.\n"
						"**********************************************************************\n\n\n",
						com_purePaks[pak].gamename, com_purePaks[pak].pakname );
			}

			missingPak = qtrue;
		} else if( path->pack->checksum != com_purePaks[pak].checksum ) {
			if ( !quiet ) {
				Com_Printf("\n\n"
						"**********************************************************************\n"
						"WARNING: %s/%s.pk3 is present but its checksum (%u) is not correct.\n"
						"**********************************************************************\n\n\n",
						com_purePaks[pak].gamename, com_purePaks[pak].pakname, path->pack->checksum );
			}

			invalidPak = qtrue;
		} else {
			continue;
		}

		FS_AddModToList( badGames, sizeof ( badGames ), com_purePaks[pak].gamename );
	}

	if ( fs_numPaksums > 0 && !missingPak && !invalidPak ) {
		// have a pure list and pure pk3s
		Cvar_Set( "fs_pure", "1" );
	} else {
		char line1[256];
		char line2[256];

		// missing basegame pure list or pure pk3s
		Cvar_Set("fs_pure", "0");

		if ( quiet ) {
			return;
		}

		if ( !strlen ( badGames ) ) {
			FS_GetModDescription( fs_gamedir, badGames, sizeof ( badGames ) );
		}

		if ( FS_ReadFile( "default.cfg", NULL ) <= 0 ) {
			if ( CL_ConnectedToRemoteServer() ) {
				return;
			}

			if ( !quiet ) {
				// print the current search paths
				FS_Path_f();

				Com_Printf( "----------------------\n" );
				Com_Printf( "%d files in pk3 files\n", fs_packFiles );
			}

			// missing data files are more important than missing PAKSUMS
			Q_strncpyz( line1, "Unable to locate data files.", sizeof (line1) );
			Com_sprintf( line2, sizeof (line2), "You need to install %s in order to play", badGames );
			Com_Error( ERR_DROP, "%s %s", line1, line2 );
			return;
		}

		Com_sprintf( line2, sizeof (line2), "You need to reinstall %s in order to play on pure servers.", badGames );

		if ( !fs_foundPaksums ) {
			// no PAKSUMS files found in search paths
			Q_strncpyz( line1, "Missing file containing Pk3 checksums.", sizeof ( line1 ) );
			Com_sprintf( line2, sizeof (line2), "You need a %s%c%s file to enable pure mode.", fs_gamedir, PATH_SEP, GAMESETTINGS );
		} else if ( !fs_numPaksums ) {
			// only empty PAKSUMS files found, probably game under development or doesn't want pure mode
			Com_Printf( "No Pk3 checksums found, disabling pure mode.\n");
			return;
		} else if ( missingPak && invalidPak ) {
			Com_sprintf( line1, sizeof ( line1 ), "Default Pk3 %s missing, corrupt, or modified.", fs_numPaksums == 1 ? "file is" : "files are" );
		} else if ( invalidPak ) {
			Com_sprintf( line1, sizeof ( line1 ), "Default Pk3 %s corrupt or modified.", fs_numPaksums == 1 ? "file is" : "files are" );
		} else {
			Com_sprintf( line1, sizeof ( line1 ), "Missing default Pk3 %s.", fs_numPaksums == 1 ? "file" : "files" );
		}

		Com_Printf(S_COLOR_YELLOW "WARNING: %s\n%s\n", line1, line2);

#ifndef DEDICATED
		// the game's config hasn't been loaded yet so this needs to be set on command line.
		// the cvar is not reset when changing fs_game.
		{
			cvar_t *fs_pakWarningDialog = Cvar_Get( "fs_pakWarningDialog", "1", CVAR_INIT );

			if ( fs_pakWarningDialog->integer ) {
				Sys_Dialog( DT_WARNING, va("%s %s", line1, line2), "Warning" );
			}
		}
#endif
	}
}

/*
=====================
FS_LoadedPakChecksums

Returns a space separated string containing the checksums of all loaded pk3 files.
Servers with sv_pure set will get this string and pass it to clients.
=====================
*/
const char *FS_LoadedPakChecksums( void ) {
	static char	info[BIG_INFO_STRING];
	searchpath_t	*search;

	info[0] = 0;

	for ( search = fs_searchpaths ; search ; search = search->next ) {
		// is the element a pak file? 
		if ( !search->pack ) {
			continue;
		}

		Q_strcat( info, sizeof( info ), va("%i ", search->pack->checksum ) );
	}

	return info;
}

/*
=====================
FS_LoadedPakNames

Returns a space separated string containing the names of all loaded pk3 files.
Servers with sv_pure set will get this string and pass it to clients.
=====================
*/
const char *FS_LoadedPakNames( void ) {
	static char	info[BIG_INFO_STRING];
	searchpath_t	*search;

	info[0] = 0;

	for ( search = fs_searchpaths ; search ; search = search->next ) {
		// is the element a pak file?
		if ( !search->pack ) {
			continue;
		}

		if (*info) {
			Q_strcat(info, sizeof( info ), " " );
		}
		Q_strcat( info, sizeof( info ), search->pack->pakBasename );
	}

	return info;
}

/*
=====================
FS_ReferencedPakChecksums

Returns a space separated string containing the checksums of all referenced pk3 files.
The server will send this to the clients so they can check which files should be auto-downloaded. 
=====================
*/
const char *FS_ReferencedPakChecksums( void ) {
	static char	info[BIG_INFO_STRING];
	searchpath_t *search;

	info[0] = 0;


	for ( search = fs_searchpaths ; search ; search = search->next ) {
		// is the element a pak file?
		if ( search->pack ) {
			if (search->pack->referenced) {
				Q_strcat( info, sizeof( info ), va("%i ", search->pack->checksum ) );
			}
		}
	}

	return info;
}

/*
=====================
FS_ReferencedPakChecksum
=====================
*/
int FS_ReferencedPakChecksum( int n ) {
	searchpath_t	*search;
	int				i = 0;

	for ( search = fs_searchpaths ; search ; search = search->next ) {
		// is the element a pak file?
		if ( search->pack && search->pack->referenced) {
			if ( i == n ) {
				return search->pack->checksum;
			}
			i++;
		}
	}

	return 0;
}

/*
=====================
FS_ReferencedPakNames

Returns a space separated string containing the names of all referenced pk3 files.
The server will send this to the clients so they can check which files should be auto-downloaded. 
=====================
*/
const char *FS_ReferencedPakNames( void ) {
	static char	info[BIG_INFO_STRING];
	searchpath_t	*search;

	info[0] = 0;

	for ( search = fs_searchpaths ; search ; search = search->next ) {
		// is the element a pak file?
		if ( search->pack ) {
			if (search->pack->referenced) {
				if (*info) {
					Q_strcat(info, sizeof( info ), " " );
				}
				Q_strcat( info, sizeof( info ), search->pack->pakGamename );
				Q_strcat( info, sizeof( info ), "/" );
				Q_strcat( info, sizeof( info ), search->pack->pakBasename );
			}
		}
	}

	return info;
}

/*
=====================
FS_PureServerSetLoadedPaks

If the string is empty, all data sources will be allowed.
If not empty, only pk3 files that match one of the space
separated checksums will be checked for files, with the
exception of .cfg and .dat files.
=====================
*/
void FS_PureServerSetLoadedPaks( const char *pakSums, const char *pakNames ) {
	int		i, c, d;

	Cmd_TokenizeString( pakSums );

	c = Cmd_Argc();
	if ( c > MAX_SEARCH_PATHS ) {
		c = MAX_SEARCH_PATHS;
	}

	fs_numServerPaks = c;

	for ( i = 0 ; i < c ; i++ ) {
		fs_serverPaks[i] = atoi( Cmd_Argv( i ) );
	}

	if (fs_numServerPaks) {
		Com_DPrintf( "Connected to a pure server.\n" );
	}
	else
	{
		if (fs_reordered)
		{
			// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=540
			// force a restart to make sure the search order will be correct
			Com_DPrintf( "FS search reorder is required\n" );
			FS_Restart( qfalse );
			return;
		}
	}

	for ( i = 0 ; i < c ; i++ ) {
		if (fs_serverPakNames[i]) {
			Z_Free(fs_serverPakNames[i]);
		}
		fs_serverPakNames[i] = NULL;
	}
	if ( pakNames && *pakNames ) {
		Cmd_TokenizeString( pakNames );

		d = Cmd_Argc();
		if ( d > MAX_SEARCH_PATHS ) {
			d = MAX_SEARCH_PATHS;
		}

		for ( i = 0 ; i < d ; i++ ) {
			fs_serverPakNames[i] = CopyString( Cmd_Argv( i ) );
		}
	}
}

/*
=====================
FS_PureServerSetReferencedPaks

The checksums and names of the pk3 files referenced at the server
are sent to the client and stored here. The client will use these
checksums to see if any pk3 files need to be auto-downloaded. 
=====================
*/
void FS_PureServerSetReferencedPaks( const char *pakSums, const char *pakNames ) {
	int		i, c, d = 0;

	Cmd_TokenizeString( pakSums );

	c = Cmd_Argc();
	if ( c > MAX_SEARCH_PATHS ) {
		c = MAX_SEARCH_PATHS;
	}

	for ( i = 0 ; i < c ; i++ ) {
		fs_serverReferencedPaks[i] = atoi( Cmd_Argv( i ) );
	}

	for (i = 0 ; i < ARRAY_LEN(fs_serverReferencedPakNames); i++)
	{
		if(fs_serverReferencedPakNames[i])
			Z_Free(fs_serverReferencedPakNames[i]);

		fs_serverReferencedPakNames[i] = NULL;
	}

	if ( pakNames && *pakNames ) {
		Cmd_TokenizeString( pakNames );

		d = Cmd_Argc();

		if(d > c)
			d = c;

		for ( i = 0 ; i < d ; i++ ) {
			fs_serverReferencedPakNames[i] = CopyString( Cmd_Argv( i ) );
		}
	}

	// ensure that there are as many checksums as there are pak names.
	if(d < c)
		c = d;

	fs_numServerReferencedPaks = c;	
}

/*
================
FS_InitFilesystem

Called only at initial startup, not when the filesystem
is resetting due to a game change
================
*/
void FS_InitFilesystem( void ) {
	// allow command line parms to override our defaults
	// we have to specially handle this, because normal command
	// line variable sets don't happen until after the filesystem
	// has already been initialized
	Com_StartupVariable("fs_cdpath");
	Com_StartupVariable("fs_basepath");
	Com_StartupVariable("fs_homepath");
	Com_StartupVariable("fs_game");

	// try to start up normally
	FS_Startup(qfalse);

	// if we can't find default.cfg, assume that the paths are
	// busted and error out now, rather than getting an unreadable
	// graphics screen when the font fails to load
	if ( FS_ReadFile( "default.cfg", NULL ) <= 0 ) {
		Com_Error( ERR_FATAL, "Couldn't load default.cfg for %s", fs_gamedirvar->string );
	}
}

/*
================
FS_TryLastValidGame

Called by Com_Error and FS_Restart if failed during game directory change
================
*/
qboolean FS_TryLastValidGame( void ) {
	if (!lastValidBase[0]) {
		return qfalse;
	}

	FS_PureServerSetLoadedPaks("", "");

	Cvar_Set("fs_basepath", lastValidBase);
	Cvar_Set("fs_game", lastValidGame);
	lastValidBase[0] = '\0';
	lastValidGame[0] = '\0';

	FS_Restart(qtrue);

	// Clean out any user and VM created cvars
	Cvar_Restart(qtrue);
	Com_ExecuteCfg();

	return qtrue;
}


/*
================
FS_Restart
================
*/
void FS_Restart( qboolean gameDirChanged ) {

	// free anything we currently have loaded
	FS_Shutdown(qfalse);

	Cvar_ResetDefaultOverrides();

	// try to start up normally
	FS_Startup(!gameDirChanged);

	// if we can't find default.cfg, assume that the paths are
	// busted and error out now, rather than getting an unreadable
	// graphics screen when the font fails to load
	if ( FS_ReadFile( "default.cfg", NULL ) <= 0 ) {
		char gamedir[MAX_OSPATH];

		// save gamedir before it's changed by FS_TryLastValidGame
		Q_strncpyz( gamedir, fs_gamedirvar->string, sizeof ( gamedir ) );

		// this might happen when connecting to a pure server not using BASEGAME/pak0.pk3
		// (for instance a Team Arena demo server)
		if (FS_TryLastValidGame()) {
			// if connected to a remote server, try to download the files
			if ( CL_ConnectedToRemoteServer() ) {
				Com_Printf( S_COLOR_YELLOW "WARNING: Couldn't load default.cfg for %s, switched to last game to try to download missing files.\n", gamedir );
				CL_MissingDefaultCfg( gamedir );
			} else {
				Com_Error( ERR_DROP, "Couldn't load default.cfg for %s", gamedir );
			}
			return;
		}
		Com_Error( ERR_FATAL, "Couldn't load default.cfg for %s", gamedir );
	}

	if ( Q_stricmp(fs_gamedirvar->string, lastValidGame) ) {
		Sys_RemovePIDFile( lastValidGame );
		Sys_InitPIDFile( fs_gamedirvar->string );

		// skip the q3config.cfg if "safe" is on the command line
		// and only execute q3config.cfg if it exists in current fs_homepath + fs_gamedir
		if ( !Com_SafeMode() && FS_FileExists( Q3CONFIG_CFG ) ) {
			Cbuf_AddText ("exec " Q3CONFIG_CFG "\n");
		}
	}

#ifdef DEDICATED
	FS_GameValid();
#endif
}

/*
=================
FS_GameValid
=================
*/
void FS_GameValid( void ) {
	Q_strncpyz(lastValidBase, fs_basepath->string, sizeof(lastValidBase));
	Q_strncpyz(lastValidGame, fs_gamedirvar->string, sizeof(lastValidGame));

}

/*
=================
FS_ConditionalRestart

Restart if necessary
Return qtrue if restarting due to game directory changed, qfalse otherwise
=================
*/
qboolean FS_ConditionalRestart(qboolean disconnect)
{
	if(fs_gamedirvar->modified)
	{
		if(FS_FilenameCompare(lastValidGame, fs_gamedirvar->string) &&
				*lastValidGame && *fs_gamedirvar->string)
		{
			Com_GameRestart(disconnect);
			return qtrue;
		}
		else
			fs_gamedirvar->modified = qfalse;
	}

	if(fs_numServerPaks && !fs_reordered)
		FS_ReorderPurePaks();

	return qfalse;
}

/*
========================================================================================

Handle based file calls for virtual machines

========================================================================================
*/

int		FS_FOpenFileByMode( const char *qpath, fileHandle_t *f, fsMode_t mode ) {
	int		r;
	qboolean	sync;

	if ( !qpath || !*qpath ) {
		return -1;
	}

	sync = qfalse;

	switch( mode ) {
		case FS_READ:
			r = FS_FOpenFileRead( qpath, f, qtrue );
			break;
		case FS_WRITE:
			if (!f) {
				return -1;
			}
			*f = FS_FOpenFileWrite( qpath );
			r = 0;
			if (*f == 0) {
				r = -1;
			}
			break;
		case FS_APPEND_SYNC:
			sync = qtrue;
		case FS_APPEND:
			if (!f) {
				return -1;
			}
			*f = FS_FOpenFileAppend( qpath );
			r = 0;
			if (*f == 0) {
				r = -1;
			}
			break;
		default:
			Com_Error( ERR_FATAL, "FS_FOpenFileByMode: bad mode" );
			return -1;
	}

	if (!f) {
		return r;
	}

	if ( *f ) {
		fsh[*f].fileSize = r;
	}
	fsh[*f].handleSync = sync;

	return r;
}

int		FS_FTell( fileHandle_t f ) {
	int pos;

	if ( !fs_searchpaths ) {
		Com_Error( ERR_FATAL, "Filesystem call made without initialization" );
	}

	if ( f < 1 || f >= MAX_FILE_HANDLES ) {
		return -1;
	}

	if (fsh[f].zipFile == qtrue) {
		pos = unztell(fsh[f].handleFiles.file.z);
	} else {
		pos = ftell(fsh[f].handleFiles.file.o);
	}
	return pos;
}

void	FS_Flush( fileHandle_t f ) {
	fflush(fsh[f].handleFiles.file.o);
}

void	FS_FilenameCompletion( char **filenames, int nfiles,
		qboolean stripExt, void(*callback)(const char *s), qboolean allowNonPureFilesOnDisk ) {
	int		i;
	char	filename[ MAX_STRING_CHARS ];

	for( i = 0; i < nfiles; i++ ) {
		FS_ConvertPath( filenames[ i ] );
		Q_strncpyz( filename, filenames[ i ], MAX_STRING_CHARS );

		if( stripExt ) {
			COM_StripExtension(filename, filename, sizeof(filename));
		}

		callback( filename );
	}
}

const char *FS_GetCurrentGameDir(void)
{
	return fs_gamedirvar->string;
}
