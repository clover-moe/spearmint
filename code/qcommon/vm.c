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
// vm.c -- virtual machine

/*


intermix code and data
symbol table

a dll has one imported function: VM_SystemCall
and one exported function: Perform


*/

#include "vm_local.h"

cvar_t	*vm_cgameHeapMegs;
cvar_t	*vm_gameHeapMegs;

vm_t	*currentVM = NULL;
vm_t	*lastVM    = NULL;
int		vm_debugLevel;

// used by Com_Error to get rid of running vm's before longjmp
static int forced_unload;

#define	MAX_VM		3
vm_t	vmTable[MAX_VM];


void VM_VmInfo_f( void );
void VM_VmProfile_f( void );



#if 0 // 64bit!
// converts a VM pointer to a C pointer and
// checks to make sure that the range is acceptable
void	*VM_VM2C( vmptr_t p, int length ) {
	return (void *)p;
}
#endif

void VM_Debug( int level ) {
	vm_debugLevel = level;
}

/*
==============
VM_Init
==============
*/
void VM_Init( void ) {
	Cvar_Get( "vm_cgame", "0", CVAR_ARCHIVE );
	Cvar_Get( "vm_game", "0", CVAR_ARCHIVE );

	vm_cgameHeapMegs = Cvar_Get( "vm_cgameHeapMegs", "2", CVAR_ARCHIVE );
	vm_gameHeapMegs = Cvar_Get( "vm_gameHeapMegs", "24", CVAR_ARCHIVE );
	Cvar_CheckRange( vm_cgameHeapMegs, 0, 128, qtrue );
	Cvar_CheckRange( vm_gameHeapMegs, 0, 128, qtrue );

	Cmd_AddCommand ("vmprofile", VM_VmProfile_f );
	Cmd_AddCommand ("vminfo", VM_VmInfo_f );

	Com_Memset( vmTable, 0, sizeof( vmTable ) );
}


/*
===============
VM_ValueToSymbol

Assumes a program counter value
===============
*/
const char *VM_ValueToSymbol( vm_t *vm, int value ) {
	vmSymbol_t	*sym;
	static char		text[MAX_TOKEN_CHARS];

	sym = vm->symbols;
	if ( !sym ) {
		return "NO SYMBOLS";
	}

	// find the symbol
	while ( sym->next && sym->next->symValue <= value ) {
		sym = sym->next;
	}

	if ( value == sym->symValue ) {
		return sym->symName;
	}

	Com_sprintf( text, sizeof( text ), "%s+%i", sym->symName, value - sym->symValue );

	return text;
}

/*
===============
VM_ValueToFunctionSymbol

For profiling, find the symbol behind this value
===============
*/
vmSymbol_t *VM_ValueToFunctionSymbol( vm_t *vm, int value ) {
	vmSymbol_t	*sym;
	static vmSymbol_t	nullSym;

	sym = vm->symbols;
	if ( !sym ) {
		return &nullSym;
	}

	while ( sym->next && sym->next->symValue <= value ) {
		sym = sym->next;
	}

	return sym;
}


/*
===============
VM_SymbolToValue
===============
*/
int VM_SymbolToValue( vm_t *vm, const char *symbol ) {
	vmSymbol_t	*sym;

	for ( sym = vm->symbols ; sym ; sym = sym->next ) {
		if ( !strcmp( symbol, sym->symName ) ) {
			return sym->symValue;
		}
	}
	return 0;
}


/*
=====================
VM_SymbolForCompiledPointer
=====================
*/
#if 0 // 64bit!
const char *VM_SymbolForCompiledPointer( vm_t *vm, void *code ) {
	int			i;

	if ( code < (void *)vm->codeBase ) {
		return "Before code block";
	}
	if ( code >= (void *)(vm->codeBase + vm->codeLength) ) {
		return "After code block";
	}

	// find which original instruction it is after
	for ( i = 0 ; i < vm->codeLength ; i++ ) {
		if ( (void *)vm->instructionPointers[i] > code ) {
			break;
		}
	}
	i--;

	// now look up the bytecode instruction pointer
	return VM_ValueToSymbol( vm, i );
}
#endif



/*
===============
ParseHex
===============
*/
int	ParseHex( const char *text ) {
	int		value;
	int		c;

	value = 0;
	while ( ( c = *text++ ) != 0 ) {
		if ( c >= '0' && c <= '9' ) {
			value = value * 16 + c - '0';
			continue;
		}
		if ( c >= 'a' && c <= 'f' ) {
			value = value * 16 + 10 + c - 'a';
			continue;
		}
		if ( c >= 'A' && c <= 'F' ) {
			value = value * 16 + 10 + c - 'A';
			continue;
		}
	}

	return value;
}

/*
===============
VM_LoadSymbols
===============
*/
void VM_LoadSymbols( vm_t *vm ) {
	union {
		char	*c;
		void	*v;
	} mapfile;
	char *text_p, *token;
	char	name[MAX_QPATH];
	char	symbols[MAX_QPATH];
	vmSymbol_t	**prev, *sym;
	int		count;
	int		value;
	int		chars;
	int		segment;
	int		numInstructions;

	// don't load symbols if not developer
	if ( !com_developer->integer ) {
		return;
	}

	COM_StripExtension(vm->name, name, sizeof(name));
	Com_sprintf( symbols, sizeof( symbols ), "vm/%s.map", name );
	FS_ReadFile( symbols, &mapfile.v );
	if ( !mapfile.c ) {
		Com_Printf( "Couldn't load symbol file: %s\n", symbols );
		return;
	}

	numInstructions = vm->instructionCount;

	// parse the symbols
	text_p = mapfile.c;
	prev = &vm->symbols;
	count = 0;

	while ( 1 ) {
		token = COM_Parse( &text_p );
		if ( !token[0] ) {
			break;
		}
		segment = ParseHex( token );
		if ( segment ) {
			COM_Parse( &text_p );
			COM_Parse( &text_p );
			continue;		// only load code segment values
		}

		token = COM_Parse( &text_p );
		if ( !token[0] ) {
			Com_Printf( "WARNING: incomplete line at end of file\n" );
			break;
		}
		value = ParseHex( token );

		token = COM_Parse( &text_p );
		if ( !token[0] ) {
			Com_Printf( "WARNING: incomplete line at end of file\n" );
			break;
		}
		chars = strlen( token );
		sym = Hunk_Alloc( sizeof( *sym ) + chars, h_high );
		*prev = sym;
		prev = &sym->next;
		sym->next = NULL;

		// convert value from an instruction number to a code offset
		if ( value >= 0 && value < numInstructions ) {
			value = vm->instructionPointers[value];
		}

		sym->symValue = value;
		Q_strncpyz( sym->symName, token, chars + 1 );

		count++;
	}

	vm->numSymbols = count;
	Com_Printf( "%i symbols parsed from %s\n", count, symbols );
	FS_FreeFile( mapfile.v );
}

/*
============
VM_DllSyscall

Dlls will call this directly

 rcg010206 The horror; the horror.

  The syscall mechanism relies on stack manipulation to get its args.
   This is likely due to C's inability to pass "..." parameters to
   a function in one clean chunk. On PowerPC Linux, these parameters
   are not necessarily passed on the stack, so while (&arg[0] == arg)
   is true, (&arg[1] == 2nd function parameter) is not necessarily
   accurate, as arg's value might have been stored to the stack or
   other piece of scratch memory to give it a valid address, but the
   next parameter might still be sitting in a register.

  Quake's syscall system also assumes that the stack grows downward,
   and that any needed types can be squeezed, safely, into a signed int.

  This hack below copies all needed values for an argument to a
   array in memory, so that Quake can get the correct values. This can
   also be used on systems where the stack grows upwards, as the
   presumably standard and safe stdargs.h macros are used.

  As for having enough space in a signed int for your datatypes, well,
   it might be better to wait for DOOM 3 before you start porting.  :)

  The original code, while probably still inherently dangerous, seems
   to work well enough for the platforms it already works on. Rather
   than add the performance hit for those platforms, the original code
   is still in use there.

  For speed, we just grab 15 arguments, and don't worry about exactly
   how many the syscall actually needs; the extra is thrown away.
 
============
*/
intptr_t QDECL VM_DllSyscall( intptr_t arg, ... ) {
#if !id386 || defined __clang__
  // rcg010206 - see commentary above
  intptr_t args[MAX_VMSYSCALL_ARGS];
  int i;
  va_list ap;
  
  args[0] = arg;
  
  va_start(ap, arg);
  for (i = 1; i < ARRAY_LEN (args); i++)
    args[i] = va_arg(ap, intptr_t);
  va_end(ap);
  
  return currentVM->systemCall( args );
#else // original id code
	return currentVM->systemCall( &arg );
#endif
}

/*
============
VM_QvmSyscall

QVM bytecode interpreters call this when QVM makes a system call
============
*/
intptr_t VM_QvmSyscall( intptr_t *args ) {
	switch (args[0]) {
	case TRAP_MEMSET:
		Com_Memset( VMA(1), args[2], args[3] );
		return args[1];
	case TRAP_MEMCPY:
		Com_Memcpy( VMA(1), VMA(2), args[3] );
		return args[1];
	case TRAP_STRNCPY:
#ifdef QVM_STRNCPY_OVERLAP
		// Handle overlapping src and dest as the C standard library may
		// not support it and some mods may do this.
		// (Native libraries and QVMs without QVM_STRNCPY_OVERLAP defined
		// report an error in Q_strncpyz() instead.)
		{
			char *dest = VMA(1);
			const char *src = VMA(2);
			unsigned int destsize = args[3];

			if ( dest < src + destsize && src < dest + destsize ) {
				const char *srcend = (const char *)memchr( src, '\0', destsize );
				unsigned int srclen = srcend ? (unsigned int)( srcend - src ) : destsize;

				if ( dest < src + srclen ) {
					memmove( dest, src, srclen );
					memset( dest + srclen, '\0', destsize - srclen );

					return args[1];
				}
			}

			strncpy( dest, src, destsize );
			return args[1];
		}
#else
		strncpy( VMA(1), VMA(2), args[3] );
		return args[1];
#endif
	case TRAP_SIN:
		return FloatAsInt( sin( VMF(1) ) );
	case TRAP_COS:
		return FloatAsInt( cos( VMF(1) ) );
	case TRAP_ATAN2:
		return FloatAsInt( atan2( VMF(1), VMF(2) ) );
	case TRAP_SQRT:
		return FloatAsInt( sqrt( VMF(1) ) );
	case TRAP_FLOOR:
		return FloatAsInt( floor( VMF(1) ) );
	case TRAP_CEIL:
		return FloatAsInt( ceil( VMF(1) ) );
	case TRAP_ACOS:
		return FloatAsInt( Q_acos( VMF(1) ) );
	case TRAP_ASIN:
		return FloatAsInt( Q_asin( VMF(1) ) );
	case TRAP_TAN:
		return FloatAsInt( tan( VMF(1) ) );
	case TRAP_ATAN:
		return FloatAsInt( atan( VMF(1) ) );
	case TRAP_POW:
		return FloatAsInt( pow( VMF(1), VMF(2) ) );
	case TRAP_EXP:
		return FloatAsInt( exp( VMF(1) ) );
	case TRAP_LOG:
		return FloatAsInt( log( VMF(1) ) );
	case TRAP_LOG10:
		return FloatAsInt( log10( VMF(1) ) );
	case TRAP_SYSCALL:
		return currentVM->systemCall( &args[1] );
	default:
		assert(0);
		Com_Error( ERR_DROP, "Unknown QVM-specific system call: %ld", (long int) args[0] );
	}
}

/*
=================
VM_LoadQVM

Load a .qvm file
=================
*/
vmHeader_t *VM_LoadQVM( vm_t *vm, qboolean alloc, qboolean unpure, int heapRequestedSize)
{
	int					dataLength;
	int					hunkLength;
	int					i;
	char				filename[MAX_QPATH];
	union {
		vmHeader_t	*h;
		void				*v;
	} header;

	// load the image
	Com_sprintf( filename, sizeof(filename), "vm/%s.qvm", vm->name );
	Com_DPrintf( "Loading vm file %s...\n", filename );

	FS_ReadFileDir(filename, vm->searchPath, unpure, &header.v);

	if ( !header.h ) {
		Com_Printf("Loading vm file %s failed.\n", filename);
		VM_Free( vm );

		Com_Printf(S_COLOR_YELLOW "Warning: Couldn't open VM file %s\n", filename);

		return NULL;
	}

	if (com_developer->integer) {
		// show where the qvm was loaded from
		FS_Which(filename, vm->searchPath);
	}

	if( LittleLong( header.h->vmMagic ) == VM_MAGIC_VER2_NEO ) {
		// byte swap the header
		for ( i = 0 ; i < sizeof( vmHeader_t ) / 4 ; i++ ) {
			((int *)header.h)[i] = LittleLong( ((int *)header.h)[i] );
		}

		// validate
		if ( header.h->jtrgLength < 0
			|| header.h->bssLength < 0
			|| header.h->dataLength < 0
			|| header.h->litLength < 0
			|| header.h->codeLength <= 0 )
		{
			VM_Free(vm);
			FS_FreeFile(header.v);
			
			Com_Printf(S_COLOR_YELLOW "Warning: %s has bad header\n", filename);
			return NULL;
		}
	} else if( LittleLong( header.h->vmMagic ) == VM_MAGIC || LittleLong( header.h->vmMagic ) == VM_MAGIC_VER2 ) {
		Com_Printf( S_COLOR_YELLOW "Warning: Ignoring unsupported legacy qvm: " );

		// show where the qvm was loaded from
		FS_Which(filename, vm->searchPath);

		VM_Free( vm );
		FS_FreeFile( header.v );

		return NULL;
	} else {
		VM_Free( vm );
		FS_FreeFile(header.v);

		Com_Printf(S_COLOR_YELLOW "Warning: %s does not have a recognisable "
				"magic number in its header\n", filename);
		return NULL;
	}

	// find size of the data
	dataLength = header.h->dataLength + header.h->litLength +
		header.h->bssLength;

	// reserve additional data for dynamic memory allocation via a system call
	hunkLength = dataLength + heapRequestedSize;

	// round up to next power of 2 so all data operations can
	// be mask protected, extra data is used for dynamic memory allocation
	for ( i = 0 ; hunkLength > ( 1 << i ) ; i++ ) {
	}
	hunkLength = 1 << i;

	if(alloc)
	{
		// allocate zero filled space for initialized and uninitialized data
		// leave some space beyond data mask so we can secure all mask operations
		vm->dataAlloc = hunkLength + 4;
		vm->dataBase = Hunk_Alloc(hunkLength, h_high);
		vm->dataMask = hunkLength - 1;

		// set up dynamic memory access
		if ( heapRequestedSize > 0 ) {
			vm->heapBase = vm->dataBase + dataLength;

			// the stack is implicitly at the end of the image
			vm->heapLength = hunkLength - PROGRAM_STACK_SIZE - dataLength;

			Z_VM_InitHeap( vm->zoneTag, vm->heapBase, vm->heapLength );
		} else {
			vm->heapBase = NULL;
			vm->heapLength = 0;
		}
	}
	else
	{
		// clear the data, but make sure we're not clearing more than allocated
		if(vm->dataAlloc != hunkLength + 4)
		{
			VM_Free(vm);
			FS_FreeFile(header.v);

			Com_Printf(S_COLOR_YELLOW "Warning: Data region size of %s not matching after "
					"VM_Restart()\n", filename);
			return NULL;
		}
		
		Com_Memset(vm->dataBase, 0, vm->dataAlloc);
	}

	// copy the intialized data
	Com_Memcpy( vm->dataBase, (byte *)header.h + header.h->dataOffset,
		header.h->dataLength + header.h->litLength );

	// byte swap the longs
	for ( i = 0 ; i < header.h->dataLength ; i += 4 ) {
		*(int *)(vm->dataBase + i) = LittleLong( *(int *)(vm->dataBase + i ) );
	}

	if(header.h->vmMagic == VM_MAGIC_VER2_NEO)
	{
		int previousNumJumpTableTargets = vm->numJumpTableTargets;

		header.h->jtrgLength &= ~0x03;

		vm->numJumpTableTargets = header.h->jtrgLength >> 2;
		Com_DPrintf("Loading %d jump table targets\n", vm->numJumpTableTargets);

		if(alloc)
		{
			vm->jumpTableTargets = Hunk_Alloc(header.h->jtrgLength, h_high);
		}
		else
		{
			if(vm->numJumpTableTargets != previousNumJumpTableTargets)
			{
				VM_Free(vm);
				FS_FreeFile(header.v);

				Com_Printf(S_COLOR_YELLOW "Warning: Jump table size of %s not matching after "
						"VM_Restart()\n", filename);
				return NULL;
			}

			Com_Memset(vm->jumpTableTargets, 0, header.h->jtrgLength);
		}

		Com_Memcpy(vm->jumpTableTargets, (byte *) header.h + header.h->dataOffset +
				header.h->dataLength + header.h->litLength, header.h->jtrgLength);

		// byte swap the longs
		for ( i = 0 ; i < header.h->jtrgLength ; i += 4 ) {
			*(int *)(vm->jumpTableTargets + i) = LittleLong( *(int *)(vm->jumpTableTargets + i ) );
		}
	}

	return header.h;
}

/*
=================
VM_Restart

Reload the data, but leave everything else in place
This allows a server to do a map_restart without changing memory allocation

We need to make sure that servers can access unpure QVMs (not contained in any pak)
even if the client is pure, so take "unpure" as argument.
=================
*/
vm_t *VM_Restart(vm_t *vm, qboolean unpure)
{
	vmHeader_t	*header;

	Com_DPrintf("VM_Restart()\n");

	if ( vm->dllHandle ) {
		Sys_UnloadDll( vm->dllHandle );

		vm->dllHandle = Sys_LoadGameDll( vm->filename, &vm->entryPoint, VM_DllSyscall );

		if ( !vm->dllHandle ) {
			Com_Error( ERR_DROP, "VM_Restart failed" );
			return NULL;
		}
	} else {
		// load the image
		if(!(header = VM_LoadQVM(vm, qfalse, unpure, vm->heapRequestedSize)))
		{
			Com_Error(ERR_DROP, "VM_Restart failed");
			return NULL;
		}

		// free the original file
		FS_FreeFile(header);
	}

	// clear the zone to a single free block
	Z_VM_InitHeap( vm->zoneTag, vm->heapBase, vm->heapLength );

	return vm;
}

/*
================
VM_Create

If image ends in .qvm it will be interpreted, otherwise
it will attempt to load as a system dll
================
*/
vm_t *VM_Create( const char *module, intptr_t (*systemCalls)(intptr_t *), 
				vmInterpret_t interpret, int zoneTag, int heapRequestedSize ) {
	vm_t		*vm;
	vmHeader_t	*header;
	int			i, remaining, retval;
	char filename[MAX_OSPATH];
	void *startSearch = NULL;

	if ( !module || !module[0] || !systemCalls ) {
		Com_Error( ERR_FATAL, "VM_Create: bad parms" );
	}

	remaining = Hunk_MemoryRemaining();

	// see if we already have the VM
	for ( i = 0 ; i < MAX_VM ; i++ ) {
		if (!Q_stricmp(vmTable[i].name, module)) {
			vm = &vmTable[i];
			return vm;
		}
	}

	// find a free vm
	for ( i = 0 ; i < MAX_VM ; i++ ) {
		if ( !vmTable[i].name[0] ) {
			break;
		}
	}

	if ( i == MAX_VM ) {
		Com_Error( ERR_FATAL, "VM_Create: no free vm_t" );
	}

	vm = &vmTable[i];

	Q_strncpyz(vm->name, module, sizeof(vm->name));
	vm->zoneTag = zoneTag;
	vm->heapRequestedSize = heapRequestedSize;

	do
	{
		retval = FS_FindVM(&startSearch, filename, sizeof(filename), module, (interpret == VMI_NATIVE));
		
		if(retval == VMI_NATIVE)
		{
			Com_DPrintf("Try loading dll file %s\n", filename);

			vm->dllHandle = Sys_LoadGameDll(filename, &vm->entryPoint, VM_DllSyscall);
			
			if(vm->dllHandle)
			{
				vm->systemCall = systemCalls;
				Q_strncpyz(vm->filename, filename, sizeof(vm->filename));
				vm->heapBase = heapRequestedSize > 0 ? Hunk_Alloc( heapRequestedSize, h_high ) : NULL;
				vm->heapLength = heapRequestedSize;
				Z_VM_InitHeap( vm->zoneTag, vm->heapBase, vm->heapLength );
				return vm;
			}
			
			Com_Printf("Failed loading dll, trying next\n");
		}
		else if(retval == VMI_COMPILED)
		{
			vm->searchPath = startSearch;
			Q_strncpyz(vm->filename, filename, sizeof(vm->filename));
			if((header = VM_LoadQVM(vm, qtrue, qtrue, heapRequestedSize)))
				break;

			// VM_Free overwrites the name on failed load
			Q_strncpyz(vm->name, module, sizeof(vm->name));
		}
	} while(retval >= 0);
	
	if(retval < 0)
		return NULL;

	vm->systemCall = systemCalls;

	// allocate space for the jump targets, which will be filled in by the compile/prep functions
	vm->instructionCount = header->instructionCount;
	vm->instructionPointers = Hunk_Alloc(vm->instructionCount * sizeof(*vm->instructionPointers), h_high);

	// copy or compile the instructions
	vm->codeLength = header->codeLength;

	vm->compiled = qfalse;

#ifdef NO_VM_COMPILED
	if(interpret >= VMI_COMPILED) {
		Com_Printf("Architecture doesn't have a bytecode compiler, using interpreter\n");
		interpret = VMI_BYTECODE;
	}
#else
	if(interpret != VMI_BYTECODE)
	{
		vm->compiled = qtrue;
		VM_Compile( vm, header );
	}
#endif
	// VM_Compile may have reset vm->compiled if compilation failed
	if (!vm->compiled)
	{
		VM_PrepareInterpreter( vm, header );
	}

	// free the original file
	FS_FreeFile( header );

	// load the map file
	VM_LoadSymbols( vm );

	// the stack is implicitly at the end of the image
	vm->programStack = vm->dataMask + 1;
	vm->stackBottom = vm->programStack - PROGRAM_STACK_SIZE;

	Com_DPrintf("%s loaded in %d bytes on the hunk\n", module, remaining - Hunk_MemoryRemaining());

	return vm;
}

/*
==============
VM_Free
==============
*/
void VM_Free( vm_t *vm ) {

	if(!vm) {
		return;
	}

	if ( vm->zoneTag ) {
		Z_VM_ShutdownHeap( vm->zoneTag );
	}

	if(vm->callLevel) {
		if(!forced_unload) {
			Com_Error( ERR_FATAL, "VM_Free(%s) on running vm", vm->name );
			return;
		} else {
			Com_Printf( "forcefully unloading %s vm\n", vm->name );
		}
	}

	if(vm->destroy)
		vm->destroy(vm);

	if ( vm->dllHandle ) {
		Sys_UnloadDll( vm->dllHandle );
#if 0	// now automatically freed by hunk
		if ( vm->heapBase ) {
			Z_Free( vm->heapBase );
		}
#endif
		Com_Memset( vm, 0, sizeof( *vm ) );
	}
#if 0	// now automatically freed by hunk
	if ( vm->codeBase ) {
		Z_Free( vm->codeBase );
	}
	if ( vm->dataBase ) {
		Z_Free( vm->dataBase );
	}
	if ( vm->instructionPointers ) {
		Z_Free( vm->instructionPointers );
	}
#endif
	Com_Memset( vm, 0, sizeof( *vm ) );

	currentVM = NULL;
	lastVM = NULL;
}

void VM_Clear(void) {
	int i;
	for (i=0;i<MAX_VM; i++) {
		VM_Free(&vmTable[i]);
	}
}

void VM_Forced_Unload_Start(void) {
	forced_unload = 1;
}

void VM_Forced_Unload_Done(void) {
	forced_unload = 0;
}

void *VM_ArgPtr( intptr_t intValue ) {
	if ( !intValue ) {
		return NULL;
	}
	// currentVM is missing on reconnect
	if ( currentVM==NULL )
	  return NULL;

	if ( currentVM->entryPoint ) {
		return (void *)(currentVM->dataBase + intValue);
	}
	else {
		return (void *)(currentVM->dataBase + (intValue & currentVM->dataMask));
	}
}

void *VM_ExplicitArgPtr( vm_t *vm, intptr_t intValue ) {
	if ( !intValue ) {
		return NULL;
	}

	// currentVM is missing on reconnect here as well?
	if ( currentVM==NULL )
	  return NULL;

	//
	if ( vm->entryPoint ) {
		return (void *)(vm->dataBase + intValue);
	}
	else {
		return (void *)(vm->dataBase + (intValue & vm->dataMask));
	}
}


/*
==============
VM_Call


Upon a system call, the stack will look like:

sp+32	parm1
sp+28	parm0
sp+24	return value
sp+20	return address
sp+16	local1
sp+14	local0
sp+12	arg1
sp+8	arg0
sp+4	return stack
sp		return address

An interpreted function will immediately execute
an OP_ENTER instruction, which will subtract space for
locals from sp
==============
*/

intptr_t QDECL VM_Call( vm_t *vm, int callnum, ... )
{
	vm_t	*oldVM;
	intptr_t r;
	int i;

	if(!vm || !vm->name[0])
		Com_Error(ERR_FATAL, "VM_Call with NULL vm (callnum is %d)", callnum);

	oldVM = currentVM;
	currentVM = vm;
	lastVM = vm;

	if ( vm_debugLevel ) {
	  Com_Printf( "VM_Call( %d )\n", callnum );
	}

	++vm->callLevel;
	// if we have a dll loaded, call it directly
	if ( vm->entryPoint ) {
		//rcg010207 -  see dissertation at top of VM_DllSyscall() in this file.
		int args[MAX_VMMAIN_ARGS-1];
		va_list ap;
		va_start(ap, callnum);
		for (i = 0; i < ARRAY_LEN(args); i++) {
			args[i] = va_arg(ap, int);
		}
		va_end(ap);

		r = vm->entryPoint( callnum,  args[0],  args[1],  args[2], args[3],
                            args[4],  args[5],  args[6], args[7],
                            args[8],  args[9], args[10], args[11]);
	} else {
#if ( id386 || idsparc ) && !defined __clang__ // calling convention doesn't need conversion in some cases
#ifndef NO_VM_COMPILED
		if ( vm->compiled )
			r = VM_CallCompiled( vm, (int*)&callnum );
		else
#endif
			r = VM_CallInterpreted( vm, (int*)&callnum );
#else
		struct {
			int callnum;
			int args[MAX_VMMAIN_ARGS-1];
		} a;
		va_list ap;

		a.callnum = callnum;
		va_start(ap, callnum);
		for (i = 0; i < ARRAY_LEN(a.args); i++) {
			a.args[i] = va_arg(ap, int);
		}
		va_end(ap);
#ifndef NO_VM_COMPILED
		if ( vm->compiled )
			r = VM_CallCompiled( vm, &a.callnum );
		else
#endif
			r = VM_CallInterpreted( vm, &a.callnum );
#endif
	}
	--vm->callLevel;

	if ( oldVM != NULL )
	  currentVM = oldVM;
	return r;
}

//=================================================================

vm_t *safeVM = NULL;

intptr_t VM_APISafeSystemCalls( intptr_t *args ) {
	Com_Printf("*** Unsafe VM API system call detected:\n");
	Com_Printf("*** The %s VM tried to make an API call during API setup process, since we\n", safeVM ? safeVM->name : "unknown");
	Com_Printf("*** haven't finished checking the API name/version we don't know what it should do.\n");
	Com_Error(ERR_FATAL, "Incompatible %s VM (tried to make an API system call before init)", safeVM ? safeVM->name : "unknown");
	return 0;
}

// Makes a VM_Call where the vm cannot make any system calls.
// If vm tries to make a system call, it errors.
// Probably only useful for getting api version.
intptr_t QDECL VM_SafeCall( vm_t *vm, int callnum )
{
	intptr_t	(*savedSystemCall)( intptr_t *parms );
	intptr_t	value;

	safeVM = vm;
	savedSystemCall = vm->systemCall;

	// Use API safe system call function.
	vm->systemCall = VM_APISafeSystemCalls;

	// Make the call.
	value = VM_Call( vm, callnum );

	// Restore systemCall pointer
	vm->systemCall = savedSystemCall;

	return value;
}

void VM_GetVersion( vm_t *vm, int nameCallNum, int versionCallNum, char *apiName, int apiNameSize, int *major, int *minor ) {
	const char			*apiNamePtr;
	unsigned int		version;
	int					i;

	apiNamePtr = VM_ExplicitArgPtr( vm, VM_SafeCall( vm, nameCallNum ) );
	version = VM_SafeCall( vm, versionCallNum );
	*major = (version >> 16) & 0xFFFF;
	*minor = version & 0xFFFF;

	// make sure API name is a graphic string and length < 64
	if ( apiNamePtr ) {
		for (i = 0; i < apiNameSize; i++) {
			if ( !apiNamePtr[i])
				break;
			if ( !isgraph( apiNamePtr[i]) ) {
				apiNamePtr = NULL;
				break;
			}
		}
		if ( i == 0 || i == apiNameSize )
			apiNamePtr = NULL;
	}
	if ( !apiNamePtr ) {
		apiNamePtr = "Unknown";
	}
	Q_strncpyz( apiName, apiNamePtr, apiNameSize );
}

//=================================================================

static int QDECL VM_ProfileSort( const void *a, const void *b ) {
	vmSymbol_t	*sa, *sb;

	sa = *(vmSymbol_t **)a;
	sb = *(vmSymbol_t **)b;

	if ( sa->profileCount < sb->profileCount ) {
		return -1;
	}
	if ( sa->profileCount > sb->profileCount ) {
		return 1;
	}
	return 0;
}

/*
==============
VM_VmProfile_f

==============
*/
void VM_VmProfile_f( void ) {
	vm_t		*vm;
	vmSymbol_t	**sorted, *sym;
	int			i;
	double		total;

	if ( !lastVM ) {
		return;
	}

	vm = lastVM;

	if ( !vm->numSymbols ) {
		return;
	}

	sorted = Z_Malloc( vm->numSymbols * sizeof( *sorted ) );
	sorted[0] = vm->symbols;
	total = sorted[0]->profileCount;
	for ( i = 1 ; i < vm->numSymbols ; i++ ) {
		sorted[i] = sorted[i-1]->next;
		total += sorted[i]->profileCount;
	}

	qsort( sorted, vm->numSymbols, sizeof( *sorted ), VM_ProfileSort );

	for ( i = 0 ; i < vm->numSymbols ; i++ ) {
		int		perc;

		sym = sorted[i];

		perc = 100 * (float) sym->profileCount / total;
		Com_Printf( "%2i%% %9i %s\n", perc, sym->profileCount, sym->symName );
		sym->profileCount = 0;
	}

	Com_Printf("    %9.0f total\n", total );

	Z_Free( sorted );
}

/*
==============
VM_VmInfo_f

==============
*/
void VM_VmInfo_f( void ) {
	vm_t	*vm;
	int		i;
	int		freeMemory;

	Com_Printf( "Registered virtual machines:\n" );
	for ( i = 0 ; i < MAX_VM ; i++ ) {
		vm = &vmTable[i];
		if ( !vm->name[0] ) {
			break;
		}

		Com_Printf( "%s : ", vm->name );
		if ( vm->dllHandle ) {
			Com_Printf( "native\n" );
		} else if ( vm->compiled ) {
			Com_Printf( "compiled on load\n" );
		} else {
			Com_Printf( "interpreted\n" );
		}

		Com_Printf( "    file name   : \"%s\"\n", vm->filename );

		if ( !vm->dllHandle ) {
			Com_Printf( "    code length : %7i\n", vm->codeLength );
			Com_Printf( "    table length: %7i\n", vm->instructionCount*4 );
			Com_Printf( "    data length : %7i\n", vm->dataMask + 1 );
		}

		freeMemory = Z_VM_HeapAvailable( vm->zoneTag );

		Com_Printf( "  dynamic memory:\n" );
		Com_Printf( "    total memory: %7i\n", vm->heapLength );
		Com_Printf( "    free memory : %7i\n", freeMemory );
		Com_Printf( "    used memory : %7i\n", vm->heapLength - freeMemory );
	}
}

/*
===============
VM_LogSyscalls

Insert calls to this while debugging the vm compiler
===============
*/
void VM_LogSyscalls( int *args ) {
	static	int		callnum;
	static	FILE	*f;

	if ( !f ) {
		f = Sys_FOpen("syscalls.log", "w" );
	}
	callnum++;
	fprintf(f, "%i: %p (%i) = %i %i %i %i\n", callnum, (void*)(args - (int *)currentVM->dataBase),
		args[0], args[1], args[2], args[3], args[4] );
}

/*
=================
VM_BlockCopy
Executes a block copy operation within currentVM data space
=================
*/

void VM_BlockCopy(unsigned int dest, unsigned int src, size_t n)
{
	unsigned int dataMask = currentVM->dataMask;

	if ((dest & dataMask) != dest
	|| (src & dataMask) != src
	|| ((dest + n) & dataMask) != dest + n
	|| ((src + n) & dataMask) != src + n)
	{
		Com_Error(ERR_DROP, "OP_BLOCK_COPY out of range!");
	}

	Com_Memcpy(currentVM->dataBase + dest, currentVM->dataBase + src, n);
}

/*
=================
VM_HeapMalloc
=================
*/
intptr_t VM_HeapMalloc( int size ) {
	byte *buf;

	if ( !currentVM->heapBase ) {
		Com_Error( ERR_DROP, "VM_HeapMalloc: Cannot allocate %d bytes for %s (no heap)", size, currentVM->name );
		return 0;
	}

	buf = Z_TagMalloc( size, currentVM->zoneTag );

	if ( !buf ) {
		Com_Error( ERR_DROP, "VM_HeapMalloc: Cannot allocate %d bytes for %s (heap full)", size, currentVM->name );
		return 0;
	}

	Com_Memset( buf, 0, size );

	return buf - currentVM->dataBase;
}

/*
=================
VM_HeapAvailable
=================
*/
int VM_HeapAvailable( void ) {
	if ( !currentVM->heapBase ) {
		return 0;
	}
	return Z_VM_HeapAvailable( currentVM->zoneTag );
}

/*
=================
VM_HeapFree
=================
*/
void VM_HeapFree( void *data ) {
	if ( !currentVM->heapBase ) {
		return;
	}
	if ( !data ) {
		Com_Error( ERR_DROP, "VM_HeapFree: NULL pointer from %s", currentVM->name );
	}
	Z_Free( data );
}
