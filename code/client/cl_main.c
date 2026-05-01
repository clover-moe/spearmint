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
// cl_main.c  -- client main loop

#include "client.h"
#include <limits.h>
#include <stddef.h>

#include "../sys/sys_local.h"

#ifdef USE_MUMBLE
#include "libmumblelink.h"
#endif

#ifdef USE_MUMBLE
cvar_t	*cl_useMumble;
cvar_t	*cl_mumbleScale;
#endif

#ifdef USE_VOIP
cvar_t	*cl_voipUseVAD;
cvar_t	*cl_voipVADThreshold;
cvar_t	*cl_voipSend;
cvar_t	*cl_voipSendTarget;
cvar_t	*cl_voipGainDuringCapture;
cvar_t	*cl_voipCaptureMult;
cvar_t	*cl_voipProtocol;
cvar_t	*cl_voip;
#endif

cvar_t	*cl_nodelta;

cvar_t	*cl_noprint;
#ifdef UPDATE_SERVER_NAME
cvar_t	*cl_motd;
#endif

cvar_t	*rcon_client_password;
cvar_t	*rconAddress;

cvar_t	*cl_timeout;
cvar_t	*cl_maxpackets;
cvar_t	*cl_packetdup;
cvar_t	*cl_timeNudge;
cvar_t	*cl_showTimeDelta;
cvar_t	*cl_freezeDemo;

cvar_t	*cl_shownet;
cvar_t	*cl_showSend;
cvar_t	*cl_timedemo;
cvar_t	*cl_timedemoLog;
cvar_t	*cl_autoRecordDemo;
cvar_t	*cl_aviFrameRate;
cvar_t	*cl_aviMotionJpeg;
cvar_t	*cl_forceavidemo;

cvar_t	*cl_sensitivity;

cvar_t	*cl_mouseAccel;
cvar_t	*cl_mouseAccelOffset;
cvar_t	*cl_mouseAccelStyle;
cvar_t	*cl_showMouseRate;

cvar_t	*m_filter;

cvar_t	*cl_activeAction;

cvar_t	*cl_motdString;

cvar_t	*cl_allowDownload;
cvar_t	*cl_inGameVideo;

cvar_t	*cl_serverStatusResendTime;

cvar_t	*cl_lanForcePackets;

cvar_t	*cl_guidServerUniq;

cvar_t	*cl_consoleKeys;

cvar_t	*cl_loadingScreenIndex;

cvar_t	*cl_rate;

#ifdef USE_FLEXIBLE_DISPLAY
cvar_t	*cl_viewsize;
cvar_t	*cl_viewmode;
cvar_t	*cl_flexibleDisplay;
#endif

clientActive_t		cl;
clientConnection_t	clc;
clientStatic_t		cls;
vm_t				*cgvm;

char				cl_reconnectArgs[MAX_OSPATH];
char				cl_oldGame[MAX_QPATH];
qboolean			cl_oldGameSet;

ping_t	cl_pinglist[MAX_PINGREQUESTS];

typedef struct serverStatus_s
{
	char string[BIG_INFO_STRING];
	netadr_t address;
	int time, startTime;
	qboolean pending;
	qboolean print;
	qboolean retrieved;
} serverStatus_t;

serverStatus_t cl_serverStatusList[MAX_SERVERSTATUSREQUESTS];

#if defined __USEA3D && defined __A3D_GEOM
	void hA3Dg_ExportRenderGeom (refexport_t *incoming_re);
#endif

static int noGameRestart = qfalse;

extern void SV_BotFrame( int time );
void CL_CheckForResend( void );
void CL_ShowIP_f(void);
void CL_ServerStatus_f(void);
void CL_ServerStatusResponse( netadr_t from, msg_t *msg );

const int cl_userinfoFlags[CL_MAX_SPLITVIEW] = {
		CVAR_USERINFO,
#if CL_MAX_SPLITVIEW > 1
		CVAR_USERINFO2,
#endif
#if CL_MAX_SPLITVIEW > 2
		CVAR_USERINFO3,
#endif
#if CL_MAX_SPLITVIEW > 3
		CVAR_USERINFO4
#endif
		};

#ifdef USE_MUMBLE
static
void CL_UpdateMumble(void)
{
	vec3_t pos, forward, up;
	float scale = cl_mumbleScale->value;
	float tmp;
	sharedPlayerState_t *ps;
	
	if(!cl_useMumble->integer)
		return;

	ps = (sharedPlayerState_t *)DA_ElementPointer( cl.snap.playerStates, 0 );

	// !!! FIXME: not sure if this is even close to correct.
	AngleVectors( ps->viewangles, forward, NULL, up);

	pos[0] = ps->origin[0] * scale;
	pos[1] = ps->origin[2] * scale;
	pos[2] = ps->origin[1] * scale;

	tmp = forward[1];
	forward[1] = forward[2];
	forward[2] = tmp;

	tmp = up[1];
	up[1] = up[2];
	up[2] = tmp;

	if(cl_useMumble->integer > 1) {
		fprintf(stderr, "%f %f %f, %f %f %f, %f %f %f\n",
			pos[0], pos[1], pos[2],
			forward[0], forward[1], forward[2],
			up[0], up[1], up[2]);
	}

	mumble_update_coordinates(pos, forward, up);
}
#endif


#ifdef USE_VOIP
static
void CL_UpdateVoipIgnore(const char *idstr, qboolean ignore)
{
	if ((*idstr >= '0') && (*idstr <= '9')) {
		const int id = atoi(idstr);
		if ((id >= 0) && (id < MAX_CLIENTS)) {
			clc.voipIgnore[id] = ignore;
			CL_AddReliableCommand(va("voip %s %d",
			                         ignore ? "ignore" : "unignore", id), qfalse);
			Com_Printf("VoIP: %s ignoring player #%d\n",
			            ignore ? "Now" : "No longer", id);
			return;
		}
	}
	Com_Printf("VoIP: invalid player ID#\n");
}

static
void CL_UpdateVoipGain(const char *idstr, float gain)
{
	if ((*idstr >= '0') && (*idstr <= '9')) {
		const int id = atoi(idstr);
		if (gain < 0.0f)
			gain = 0.0f;
		if ((id >= 0) && (id < MAX_CLIENTS)) {
			clc.voipGain[id] = gain;
			Com_Printf("VoIP: player #%d gain now set to %f\n", id, gain);
		}
	}
}

void CL_Voip_f( void )
{
	const char *cmd = Cmd_Argv(1);
	const char *reason = NULL;

	if (clc.state != CA_ACTIVE)
		reason = "Not connected to a server";
	else if (!clc.voipCodecInitialized)
		reason = "Voip codec not initialized";
	else if (!clc.voipEnabled)
		reason = "Server doesn't support VoIP";
	else if (!clc.demoplaying && Com_GameIsSinglePlayer())
		reason = "running in single-player mode";

	if (reason != NULL) {
		Com_Printf("VoIP: command ignored: %s\n", reason);
		return;
	}

	if (strcmp(cmd, "ignore") == 0) {
		CL_UpdateVoipIgnore(Cmd_Argv(2), qtrue);
	} else if (strcmp(cmd, "unignore") == 0) {
		CL_UpdateVoipIgnore(Cmd_Argv(2), qfalse);
	} else if (strcmp(cmd, "gain") == 0) {
		if (Cmd_Argc() > 3) {
			CL_UpdateVoipGain(Cmd_Argv(2), atof(Cmd_Argv(3)));
		} else if (Q_isanumber(Cmd_Argv(2))) {
			int id = atoi(Cmd_Argv(2));
			if (id >= 0 && id < MAX_CLIENTS) {
				Com_Printf("VoIP: current gain for player #%d "
					"is %f\n", id, clc.voipGain[id]);
			} else {
				Com_Printf("VoIP: invalid player ID#\n");
			}
		} else {
			Com_Printf("usage: voip gain <playerID#> [value]\n");
		}
	} else if (strcmp(cmd, "muteall") == 0) {
		Com_Printf("VoIP: muting incoming voice\n");
		CL_AddReliableCommand("voip muteall", qfalse);
		clc.voipMuteAll = qtrue;
	} else if (strcmp(cmd, "unmuteall") == 0) {
		Com_Printf("VoIP: unmuting incoming voice\n");
		CL_AddReliableCommand("voip unmuteall", qfalse);
		clc.voipMuteAll = qfalse;
	} else {
		Com_Printf("usage: voip [un]ignore <playerID#>\n"
		           "       voip [un]muteall\n"
		           "       voip gain <playerID#> [value]\n");
	}
}


static
void CL_VoipNewGeneration(void)
{
	// don't have a zero generation so new clients won't match, and don't
	//  wrap to negative so MSG_ReadLong() doesn't "fail."
	clc.voipOutgoingGeneration++;
	if (clc.voipOutgoingGeneration <= 0)
		clc.voipOutgoingGeneration = 1;
	clc.voipPower[clc.playerNums[0]] = 0.0f;
	clc.voipOutgoingSequence = 0;

	opus_encoder_ctl(clc.opusEncoder, OPUS_RESET_STATE);
}

/*
===============
CL_VoipParseTargets

sets clc.voipTargets according to cl_voipSendTarget
Generally we don't want who's listening to change during a transmission,
so this is only called when the key is first pressed
===============
*/
void CL_VoipParseTargets(void)
{
	const char *target = cl_voipSendTarget->string;
	const char *vmStr;
	char *end;
	int val;

	Com_Memset(clc.voipTargets, 0, sizeof(clc.voipTargets));
	clc.voipFlags &= ~VOIP_SPATIAL;

	while(target)
	{
		while(*target == ',' || *target == ' ')
			target++;

		if(!*target)
			break;
		
		if(isdigit(*target))
		{
			val = strtol(target, &end, 10);
			target = end;
		}
		else
		{
			if(!Q_stricmpn(target, "all", 3))
			{
				Com_Memset(clc.voipTargets, ~0, sizeof(clc.voipTargets));
				return;
			}
			if(!Q_stricmpn(target, "spatial", 7))
			{
				clc.voipFlags |= VOIP_SPATIAL;
				target += 7;
				continue;
			}
			else
			{
				// ask cgame for playerNums based on this token
				Cmd_TokenizeString( target );
				vmStr = VM_ExplicitArgPtr( cgvm, VM_Call( cgvm, CG_VOIP_STRING, 0 ) );

				while (vmStr)
				{
					while(*vmStr == ',' || *vmStr == ' ')
						vmStr++;

					if(!*vmStr || !isdigit(*vmStr))
						break;

					val = strtol(vmStr, &end, 10);
					vmStr = end;

					if(val < 0 || val >= MAX_CLIENTS)
						continue;

					clc.voipTargets[val / 8] |= 1 << (val % 8);
				}

				while(*target && *target != ',' && *target != ' ')
					target++;

				continue;
			}
		}

		if(val < 0 || val >= MAX_CLIENTS)
		{
			Com_Printf(S_COLOR_YELLOW "WARNING: VoIP "
				   "target %d is not a valid player "
				   "number\n", val);

			continue;
		}

		clc.voipTargets[val / 8] |= 1 << (val % 8);
	}
}

/*
===============
CL_CaptureVoip

Record more audio from the hardware if required and encode it into Opus
 data for later transmission.
===============
*/
static
void CL_CaptureVoip(void)
{
	const float audioMult = cl_voipCaptureMult->value;
	const qboolean useVad = (cl_voipUseVAD->integer != 0);
	qboolean initialFrame = qfalse;
	qboolean finalFrame = qfalse;

#if USE_MUMBLE
	// if we're using Mumble, don't try to handle VoIP transmission ourselves.
	if (cl_useMumble->integer)
		return;
#endif

	// If your data rate is too low, you'll get Connection Interrupted warnings
	//  when VoIP packets arrive, even if you have a broadband connection.
	//  This might work on rates lower than 25000, but for safety's sake, we'll
	//  just demand it. Who doesn't have at least a DSL line now, anyhow? If
	//  you don't, you don't need VoIP.  :)
	if (cl_voip->modified || cl_rate->modified) {
		if ((cl_voip->integer) && (cl_rate->integer < 25000)) {
			Com_Printf(S_COLOR_YELLOW "Your network rate is too slow for VoIP.\n");
			Com_Printf("Set 'Data Rate' to 'LAN/Cable/xDSL' in 'Setup/System/Network'.\n");
			Com_Printf("Until then, VoIP is disabled.\n");
			Cvar_Set("cl_voip", "0");
		}
		Cvar_Set("cl_voipProtocol", cl_voip->integer ? "opus" : "");
		cl_voip->modified = qfalse;
		cl_rate->modified = qfalse;
	}

	if (!clc.voipCodecInitialized)
		return;  // just in case this gets called at a bad time.

	if (clc.voipOutgoingDataSize > 0)
		return;  // packet is pending transmission, don't record more yet.

	if (cl_voipUseVAD->modified) {
		Cvar_Set("cl_voipSend", (useVad) ? "1" : "0");
		cl_voipUseVAD->modified = qfalse;
	}

	if ((useVad) && (!cl_voipSend->integer))
		Cvar_Set("cl_voipSend", "1");  // lots of things reset this.

	if (cl_voipSend->modified) {
		qboolean dontCapture = qfalse;
		if (clc.state != CA_ACTIVE)
			dontCapture = qtrue;  // not connected to a server.
		else if (!clc.voipEnabled)
			dontCapture = qtrue;  // server doesn't support VoIP.
		else if (clc.demoplaying)
			dontCapture = qtrue;  // playing back a demo.
		else if ( cl_voip->integer == 0 )
			dontCapture = qtrue;  // client has VoIP support disabled.
		else if ( audioMult == 0.0f )
			dontCapture = qtrue;  // basically silenced incoming audio.
		else if ( clc.playerNums[0] == -1 )
			dontCapture = qtrue;

		cl_voipSend->modified = qfalse;

		if(dontCapture)
		{
			Cvar_Set("cl_voipSend", "0");
			return;
		}

		if (cl_voipSend->integer) {
			initialFrame = qtrue;
		} else {
			finalFrame = qtrue;
		}
	}

	// try to get more audio data from the sound card...

	if (initialFrame) {
		S_MasterGain(Com_Clamp(0.0f, 1.0f, cl_voipGainDuringCapture->value));
		S_StartCapture();
		CL_VoipNewGeneration();
		CL_VoipParseTargets();
	}

	if ((cl_voipSend->integer) || (finalFrame)) { // user wants to capture audio?
		int samples = S_AvailableCaptureSamples();
		const int packetSamples = (finalFrame) ? VOIP_MAX_FRAME_SAMPLES : VOIP_MAX_PACKET_SAMPLES;

		// enough data buffered in audio hardware to process yet?
		if (samples >= packetSamples) {
			// audio capture is always MONO16.
			static int16_t sampbuffer[VOIP_MAX_PACKET_SAMPLES];
			float voipPower = 0.0f;
			int voipFrames;
			int i, bytes;

			if (samples > VOIP_MAX_PACKET_SAMPLES)
				samples = VOIP_MAX_PACKET_SAMPLES;

			// !!! FIXME: maybe separate recording from encoding, so voipPower
			// !!! FIXME:  updates faster than 4Hz?

			samples -= samples % VOIP_MAX_FRAME_SAMPLES;
			if (samples != 120 && samples != 240 && samples != 480 && samples != 960 && samples != 1920 && samples != 2880 ) {
				Com_Printf("Voip: bad number of samples %d\n", samples);
				return;
			}
			voipFrames = samples / VOIP_MAX_FRAME_SAMPLES;

			S_Capture(samples, (byte *) sampbuffer);  // grab from audio card.

			// check the "power" of this packet...
			for (i = 0; i < samples; i++) {
				float flsamp = (float) sampbuffer[i];
				float s;

				sampbuffer[i] = (int16_t) ((flsamp) * audioMult);

				// calculate power after scaling sample data
				flsamp = (float) sampbuffer[i];
				s = fabs(flsamp);
				voipPower += s * s;
			}

			// encode raw audio samples into Opus data...
			bytes = opus_encode(clc.opusEncoder, sampbuffer, samples,
									(unsigned char *) clc.voipOutgoingData,
									sizeof (clc.voipOutgoingData));
			if ( bytes <= 0 ) {
				Com_DPrintf("VoIP: Error encoding %d samples\n", samples);
				bytes = 0;
			}

			clc.voipPower[clc.playerNums[0]] = (voipPower / (32768.0f * 32768.0f *
			                 ((float) samples))) * 100.0f;

			if ((useVad) && (clc.voipPower[clc.playerNums[0]] < cl_voipVADThreshold->value)) {
				CL_VoipNewGeneration();  // no "talk" for at least 1/4 second.
			} else {
				clc.voipOutgoingDataSize = bytes;
				clc.voipOutgoingDataFrames = voipFrames;

				Com_DPrintf("VoIP: Send %d frames, %d bytes, %f power\n",
				            voipFrames, bytes, clc.voipPower[clc.playerNums[0]]);

				clc.voipLastPacketTime[clc.playerNums[0]] = cl.serverTime;

				#if 0
				static FILE *encio = NULL;
				if (encio == NULL) encio = fopen("voip-outgoing-encoded.bin", "wb");
				if (encio != NULL) { fwrite(clc.voipOutgoingData, bytes, 1, encio); fflush(encio); }
				static FILE *decio = NULL;
				if (decio == NULL) decio = fopen("voip-outgoing-decoded.bin", "wb");
				if (decio != NULL) { fwrite(sampbuffer, voipFrames * VOIP_MAX_FRAME_SAMPLES * 2, 1, decio); fflush(decio); }
				#endif
			}
		}
	}

	// User requested we stop recording, and we've now processed the last of
	//  any previously-buffered data. Pause the capture device, etc.
	if (finalFrame) {
		S_StopCapture();
		S_MasterGain(1.0f);
		clc.voipPower[clc.playerNums[0]] = 0.0f;  // force this value so it doesn't linger.
	}
}

// Cgame and UI access functions for VoIP information
int CL_GetVoipTime( int playerNum ) {
	if ( playerNum < 0  || playerNum >= ARRAY_LEN( clc.voipPower ) ) {
		return 0.0f;
	}

	// make sure server is running
	if ( clc.state != CA_ACTIVE ) {
		return 0;
	}

	return clc.voipLastPacketTime[playerNum];
}

float CL_GetVoipPower( int playerNum ) {
	if ( playerNum < 0  || playerNum >= ARRAY_LEN( clc.voipPower ) ) {
		return 0.0f;
	}

	// make sure server is running
	if ( clc.state != CA_ACTIVE )
		return 0.0f;

	// clc.voipPower is always the power of the last voip snapshot, never cleared.
	if ( !clc.voipLastPacketTime[playerNum] || clc.voipLastPacketTime[playerNum] < cl.serverTime - 250 )
		return 0.0f;

	return clc.voipPower[playerNum];
}

float CL_GetVoipGain( int playerNum ) {
	if ( playerNum < 0  || playerNum >= ARRAY_LEN( clc.voipGain ) ) {
		return 0.0f;
	}

	// make sure server is running
	if ( clc.state != CA_ACTIVE )
		return 0.0f;

	return clc.voipGain[playerNum];
}

qboolean CL_GetVoipMutePlayer( int playerNum ) {
	if ( playerNum < 0  || playerNum >= ARRAY_LEN( clc.voipIgnore ) ) {
		return qfalse;
	}

	// make sure server is running
	if ( clc.state != CA_ACTIVE )
		return qfalse;

	return clc.voipIgnore[playerNum];
}

qboolean CL_GetVoipMuteAll( void ) {
	// make sure server is running
	if ( clc.state != CA_ACTIVE )
		return qfalse;

	return clc.voipMuteAll;
}
#endif

/*
=======================================================================

CLIENT RELIABLE COMMAND COMMUNICATION

=======================================================================
*/

/*
======================
CL_AddReliableCommand

The given command will be transmitted to the server, and is guaranteed to
not have future usercmd_t executed before it is executed
======================
*/
void CL_AddReliableCommand(const char *cmd, qboolean isDisconnectCmd)
{
	int unacknowledged = clc.reliableSequence - clc.reliableAcknowledge;
	
	// if we would be losing an old command that hasn't been acknowledged,
	// we must drop the connection
	// also leave one slot open for the disconnect command in this case.
	
	if ((isDisconnectCmd && unacknowledged > MAX_RELIABLE_COMMANDS) ||
	    (!isDisconnectCmd && unacknowledged >= MAX_RELIABLE_COMMANDS))
	{
		if(com_errorEntered)
			return;
		else
			Com_Error(ERR_DROP, "Client command overflow");
	}

	Q_strncpyz(clc.reliableCommands[++clc.reliableSequence & (MAX_RELIABLE_COMMANDS - 1)],
		   cmd, sizeof(*clc.reliableCommands));
}

/*
=======================================================================

CLIENT SIDE DEMO RECORDING

=======================================================================
*/

/*
====================
CL_WriteDemoMessage

Dumps the current net message, prefixed by the length
====================
*/

void CL_WriteDemoMessage ( msg_t *msg, int headerBytes ) {
	int		len, swlen;

	// write the packet sequence
	len = clc.serverMessageSequence;
	swlen = LittleLong( len );
	FS_Write (&swlen, 4, clc.demofile);
	// skip the packet sequencing information
	len = msg->cursize - headerBytes;
	swlen = LittleLong(len);
	FS_Write (&swlen, 4, clc.demofile);
	FS_Write ( msg->data + headerBytes, len, clc.demofile );
}


/*
====================
CL_StopRecording_f

stop recording a demo
====================
*/
void CL_StopRecord_f( void ) {
	int		len;

	if ( !clc.demorecording ) {
		Com_Printf ("Not recording a demo.\n");
		return;
	}

	// finish up
	len = -1;
	FS_Write (&len, 4, clc.demofile);
	FS_Write (&len, 4, clc.demofile);

	// update end time in header
	if ( FS_Seek (clc.demofile, offsetof( demoHeader_t, endTime ), FS_SEEK_SET) == 0 ) {
		demoHeader_t	header;
		qtime_t			now;
		int				recordEndTime;

		Com_RealTime( &now );
		recordEndTime = Sys_Milliseconds();

		Com_sprintf( header.endTime, sizeof(header.endTime), "%04d-%02d-%02d %02d:%02d:%02d",
						1900 + now.tm_year,
						1 + now.tm_mon,
						now.tm_mday,
						now.tm_hour,
						now.tm_min,
						now.tm_sec );

		header.runTime = LittleLong( recordEndTime - clc.demoRecordStartTime );

		FS_Write (header.endTime, sizeof(header.endTime), clc.demofile);
		FS_Write (&header.runTime, sizeof(header.runTime), clc.demofile);
	}

	FS_FCloseFile (clc.demofile);
	clc.demofile = 0;
	clc.demorecording = qfalse;
	Com_Printf ("Stopped demo.\n");
}

/* 
================== 
CL_DemoFilename
================== 
*/  
void CL_DemoFilename( int number, char *fileName, int fileNameSize ) {
	int		a,b,c,d;

	if(number < 0 || number > 9999)
		number = 9999;

	a = number / 1000;
	number -= a*1000;
	b = number / 100;
	number -= b*100;
	c = number / 10;
	number -= c*10;
	d = number;

	Com_sprintf( fileName, fileNameSize, "demo%i%i%i%i"
		, a, b, c, d );
}

/*
====================
CL_Record_f

record <demoname>

Begins recording a demo from the current position
====================
*/
static char		demoName[MAX_QPATH];	// compiler bug workaround
void CL_Record_f( void ) {
	char		name[MAX_OSPATH];
	byte		bufData[MAX_MSGLEN];
	msg_t	buf;
	int			i;
	int			len;
	sharedEntityState_t	*ent;
	char		*s;
	demoHeader_t	header;
	qtime_t			now;

	if ( Cmd_Argc() > 2 ) {
		Com_Printf ("record <demoname>\n");
		return;
	}

	if ( clc.demorecording ) {
		Com_Printf ("Already recording.\n");
		return;
	}

	if ( clc.state != CA_ACTIVE ) {
		Com_Printf ("You must be in a level to record.\n");
		return;
	}

  // sync 0 doesn't prevent recording, so not forcing it off .. everyone does g_sync 1 ; record ; g_sync 0 ..
	if ( NET_IsLocalAddress( clc.serverAddress ) && !Cvar_VariableValue( "g_synchronousClients" ) ) {
		Com_Printf (S_COLOR_YELLOW "WARNING: You should set 'g_synchronousClients 1' for smoother demo recording\n");
	}

	if ( Cmd_Argc() == 2 ) {
		s = Cmd_Argv(1);
		Q_strncpyz( demoName, s, sizeof( demoName ) );
		Com_sprintf(name, sizeof(name), "demos/%s.%s", demoName, com_demoext->string);
	} else {
		int		number;

		// scan for a free demo name
		for ( number = 0 ; number <= 9999 ; number++ ) {
			CL_DemoFilename( number, demoName, sizeof( demoName ) );
			Com_sprintf(name, sizeof(name), "demos/%s.%s", demoName, com_demoext->string);

			if (!FS_FileExists(name))
				break;	// file doesn't exist
		}
	}

	// open the demo file

	Com_Printf ("recording to %s.\n", name);
	clc.demofile = FS_FOpenFileWrite( name );
	if ( !clc.demofile ) {
		Com_Printf ("ERROR: couldn't open.\n");
		return;
	}
	clc.demorecording = qtrue;

	Q_strncpyz( clc.demoName, demoName, sizeof( clc.demoName ) );

	// don't start saving messages until a non-delta compressed message is received
	clc.demowaiting = qtrue;

	Com_RealTime( &now );
	clc.demoRecordStartTime = Sys_Milliseconds();

	// setup demo header
	Com_Memset( &header, 0, sizeof ( header ) );
	Com_Memcpy( header.magic, DEMO_MAGIC, sizeof ( header.magic ) );
	header.headerSize = LittleLong( sizeof( header ) );

#ifdef LEGACY_PROTOCOL
	if(clc.compat)
		header.protocol = LittleLong( com_legacyprotocol->integer );
	else
#endif
		header.protocol = LittleLong( com_protocol->integer );

	Com_sprintf( header.startTime, sizeof(header.startTime), "%04d-%02d-%02d %02d:%02d:%02d",
					1900 + now.tm_year,
					1 + now.tm_mon,
					now.tm_mday,
					now.tm_hour,
					now.tm_min,
					now.tm_sec );
	Com_Memset( header.endTime, 0, sizeof (header.endTime) );

	// write demo header
	FS_Write (&header, sizeof(header), clc.demofile);

	// write out the gamestate message
	MSG_Init (&buf, bufData, sizeof(bufData));
	MSG_Bitstream(&buf);

	// NOTE, MRE: all server->client messages now acknowledge
	MSG_WriteLong( &buf, clc.reliableSequence );

	MSG_WriteByte (&buf, svc_gamestate);
	MSG_WriteLong (&buf, clc.serverCommandSequence );

	// configstrings
	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		if ( !cl.gameState.stringOffsets[i] ) {
			continue;
		}
		s = cl.gameState.stringData + cl.gameState.stringOffsets[i];
		MSG_WriteByte (&buf, svc_configstring);
		MSG_WriteShort (&buf, i);
		MSG_WriteBigString (&buf, s);
	}

	MSG_WriteByte( &buf, svc_EOF );

	for ( i = 0; i < MAX_SPLITVIEW; i++ ) {
		MSG_WriteLong(&buf, clc.playerNums[i]);
	}

	// finished writing the gamestate stuff

	// write initial baselines
	for ( i = 0; i < MAX_GENTITIES ; i++ ) {
		ent = (sharedEntityState_t *)DA_ElementPointer( cl.entityBaselines, i );
		if ( !ent->number ) {
			continue;
		}
		MSG_WriteByte (&buf, svc_baseline);		
		MSG_WriteDeltaEntity (&buf, NULL, ent, qtrue );
	}

	// finished writing the client packet
	MSG_WriteByte( &buf, svc_EOF );

	// write it to the demo file
	len = LittleLong( clc.serverMessageSequence - 1 );
	FS_Write (&len, 4, clc.demofile);

	len = LittleLong (buf.cursize);
	FS_Write (&len, 4, clc.demofile);
	FS_Write (buf.data, buf.cursize, clc.demofile);

	// the rest of the demo file will be copied from net messages
}

/*
=======================================================================

CLIENT SIDE DEMO PLAYBACK

=======================================================================
*/

/*
=================
CL_DemoFrameDurationSDev
=================
*/
static float CL_DemoFrameDurationSDev( void )
{
	int i;
	int numFrames;
	float mean = 0.0f;
	float variance = 0.0f;

	if( ( clc.timeDemoFrames - 1 ) > MAX_TIMEDEMO_DURATIONS )
		numFrames = MAX_TIMEDEMO_DURATIONS;
	else
		numFrames = clc.timeDemoFrames - 1;

	for( i = 0; i < numFrames; i++ )
		mean += clc.timeDemoDurations[ i ];
	mean /= numFrames;

	for( i = 0; i < numFrames; i++ )
	{
		float x = clc.timeDemoDurations[ i ];

		variance += ( ( x - mean ) * ( x - mean ) );
	}
	variance /= numFrames;

	return sqrt( variance );
}

/*
=================
CL_DemoCompleted
=================
*/
void CL_DemoCompleted( void )
{
	char buffer[ MAX_STRING_CHARS ];

	if( cl_timedemo && cl_timedemo->integer )
	{
		int	time;
		
		time = Sys_Milliseconds() - clc.timeDemoStart;
		if( time > 0 )
		{
			// Millisecond times are frame durations:
			// minimum/average/maximum/std deviation
			Com_sprintf( buffer, sizeof( buffer ),
					"%i frames %3.1f seconds %3.1f fps %d.0/%.1f/%d.0/%.1f ms\n",
					clc.timeDemoFrames,
					time/1000.0,
					clc.timeDemoFrames*1000.0 / time,
					clc.timeDemoMinDuration,
					time / (float)clc.timeDemoFrames,
					clc.timeDemoMaxDuration,
					CL_DemoFrameDurationSDev( ) );
			Com_Printf( "%s", buffer );

			// Write a log of all the frame durations
			if( cl_timedemoLog && strlen( cl_timedemoLog->string ) > 0 )
			{
				int i;
				int numFrames;
				fileHandle_t f;

				if( ( clc.timeDemoFrames - 1 ) > MAX_TIMEDEMO_DURATIONS )
					numFrames = MAX_TIMEDEMO_DURATIONS;
				else
					numFrames = clc.timeDemoFrames - 1;

				f = FS_FOpenFileWrite( cl_timedemoLog->string );
				if( f )
				{
					FS_Printf( f, "# %s", buffer );

					for( i = 0; i < numFrames; i++ )
						FS_Printf( f, "%d\n", clc.timeDemoDurations[ i ] );

					FS_FCloseFile( f );
					Com_Printf( "%s written\n", cl_timedemoLog->string );
				}
				else
				{
					Com_Printf( "Couldn't open %s for writing\n",
							cl_timedemoLog->string );
				}
			}
		}
	}

	Cbuf_AddText( "disconnect\n" );
	CL_NextDemo();
	Cbuf_Execute();
}

/*
=================
CL_ReadDemoMessage
=================
*/
void CL_ReadDemoMessage( void ) {
	int			r;
	msg_t		buf;
	byte		bufData[ MAX_MSGLEN ];
	int			s;

	if ( !clc.demofile ) {
		CL_DemoCompleted ();
		return;
	}

	// get the sequence number
	r = FS_Read( &s, 4, clc.demofile);
	if ( r != 4 ) {
		CL_DemoCompleted ();
		return;
	}
	clc.serverMessageSequence = LittleLong( s );

	// init the message
	MSG_Init( &buf, bufData, sizeof( bufData ) );

	// get the length
	r = FS_Read (&buf.cursize, 4, clc.demofile);
	if ( r != 4 ) {
		CL_DemoCompleted ();
		return;
	}
	buf.cursize = LittleLong( buf.cursize );
	if ( buf.cursize == -1 ) {
		CL_DemoCompleted ();
		return;
	}
	if ( buf.cursize > buf.maxsize ) {
		Com_Error (ERR_DROP, "CL_ReadDemoMessage: demoMsglen > MAX_MSGLEN");
	}
	r = FS_Read( buf.data, buf.cursize, clc.demofile );
	if ( r != buf.cursize ) {
		Com_Printf( "Demo file was truncated.\n");
		CL_DemoCompleted ();
		return;
	}

	clc.lastPacketTime = cls.realtime;
	buf.readcount = 0;
	CL_ParseServerMessage( &buf );
}

/*
====================
CL_ValidDemoFile

if demoName is an absolute path checks OS path, otherwise checks game filesystem path.

if returns true, header looks ok and can probably play it (assuming correct cgame and pk3s).
if returns false, can't play it.
if returns false and length == 0, either file not found or it's size is 0...
if returns false and protocol > 0, it's a unsupported protocol.
====================
*/
qboolean CL_ValidDemoFile( const char *demoName, int *pProtocol, int *pLength, fileHandle_t *pHandle, char *pStartTime, char *pEndTime, int *pRunTime ) {
	demoHeader_t	header;
	char			name[MAX_OSPATH];
	char			dotdemoext[MAX_QPATH];
	int				r, i;
	int				length;
	int				protocol;
	int				headerSize;
	fileHandle_t	f;

	if ( pProtocol )
		*pProtocol = 0;
	if ( pLength )
		*pLength = 0;
	if ( pHandle )
		*pHandle = 0;
	if ( pStartTime )
		*pStartTime = '\0';
	if ( pEndTime )
		*pEndTime = '\0';
	if ( pRunTime )
		*pRunTime = 0;

	// try OS path if has demo extension
	if ( Sys_PathIsAbsolute( demoName ) ) {
		length = FS_System_FOpenFileRead( demoName, &f );
	} else {
		// try game filesystem path
		Com_sprintf( name, sizeof(name), "demos/%s", demoName );

		// add demo extension if none was specified
		Com_sprintf( dotdemoext, sizeof(dotdemoext), ".%s", com_demoext->string );
		COM_DefaultExtension( name, sizeof(name), dotdemoext );

		length = FS_FOpenFileRead( name, &f, qtrue );
	}

	if ( !f ) {
		return qfalse;
	}

	if ( pLength ) {
		*pLength = length;
	}

	r = FS_Read( &header, sizeof (header), f);
	if ( r != sizeof (header) || memcmp( header.magic, DEMO_MAGIC, sizeof(header.magic) ) != 0 ) {
		FS_FCloseFile( f );
		return qfalse;
	}

	headerSize = LittleLong( header.headerSize );

	// must meet minimum header size
	if ( headerSize < offsetof( demoHeader_t, protocol ) + sizeof (int) ) {
		FS_FCloseFile( f );
		return qfalse;
	}

	// optional data
	if ( pStartTime && headerSize > offsetof( demoHeader_t, startTime )  ) {
		Q_strncpyz( pStartTime, header.startTime, sizeof (header.startTime) );
	}
	if ( pEndTime && headerSize > offsetof( demoHeader_t, endTime )  ) {
		Q_strncpyz( pEndTime, header.endTime, sizeof (header.endTime) );
	}
	if ( pRunTime && headerSize > offsetof( demoHeader_t, runTime ) ) {
		*pRunTime = LittleLong( header.runTime );
	}

	// verify protocol
	protocol = LittleLong( header.protocol );

	if ( pProtocol ) {
		*pProtocol = protocol;
	}

	for(i = 0; demo_protocols[i]; i++)
	{
		if(demo_protocols[i] == protocol)
			break;
	}

	if(demo_protocols[i] || protocol == com_protocol->integer
#ifdef LEGACY_PROTOCOL
	   || protocol == com_legacyprotocol->integer
#endif
	  )
	{
		// valid demo protocol
	} else {
		FS_FCloseFile( f );
		return qfalse;
	}

	if ( pHandle ) {
		// skip to end of header so can start reading the demo packets
		if ( FS_Seek ( f, headerSize, FS_SEEK_SET ) != 0 ) {
			FS_FCloseFile( f );
			return qfalse;
		}

		*pHandle = f;
	} else {
		FS_FCloseFile( f );
	}

	return qtrue;
}

/*
====================
CL_CompleteDemoName
====================
*/
static void CL_CompleteDemoName( char *args, int argNum )
{
	if( argNum == 2 )
	{
		char dotdemoext[MAX_QPATH];

		Com_sprintf( dotdemoext, sizeof (dotdemoext), ".%s", com_demoext->string );

		Field_CompleteFilename( "demos", dotdemoext, qtrue, qtrue );
	}
}

/*
====================
CL_PlayDemo

play a demo, fullpath
====================
*/
void CL_PlayDemo( const char *demoName ) {
	int			protocol;
	char		startTime[20];
	char		endTime[20];
	int			runTime;

	// make sure a local server is killed
	// 2 means don't force disconnect of local client
	Cvar_Set( "sv_killserver", "2" );

	if ( clc.state != CA_DISCONNECTED ) {
		CL_Disconnect( qtrue );
	}

	// open the demo file
	if ( !CL_ValidDemoFile( demoName, &protocol, &clc.demoLength, &clc.demofile, startTime, endTime, &runTime ) ) {
		if ( clc.demoLength <= 0 ) {
			Com_Printf( "Couldn't open demo '%s'\n", demoName );
		} else if ( protocol > 0 ) {
			Com_Printf( "Demo '%s' uses unsupported protocol %d\n", demoName, protocol );
		} else {
			Com_Printf( "Invalid demo header in '%s'\n", demoName );
		}
		return;
	}

	Com_Printf( "Loading demo '%s' recorded from %s to %s (%d seconds)\n", demoName, startTime, endTime, runTime / 1000 );

	Q_strncpyz( clc.demoName, demoName, sizeof( clc.demoName ) );

	Con_Close();

	clc.state = CA_CONNECTED;
	clc.demoplaying = qtrue;
	Q_strncpyz( clc.servername, Sys_Basename( clc.demoName ), sizeof( clc.servername ) );
	COM_StripExtension( clc.servername, clc.servername, sizeof( clc.servername ) );

	SCR_UpdateScreen();

#ifdef LEGACY_PROTOCOL
	if(protocol <= com_legacyprotocol->integer)
		clc.compat = qtrue;
	else
		clc.compat = qfalse;
#endif

	// read demo messages until connected
	while ( clc.state >= CA_CONNECTED && clc.state < CA_PRIMED ) {
		CL_ReadDemoMessage();
	}
	// don't get the first snapshot this frame, to prevent the long
	// time from the gamestate load from messing causing a time skip
	clc.firstDemoFrameSkipped = qfalse;
}


/*
====================
CL_PlayDemo_f

demo <demoname>

====================
*/
void CL_PlayDemo_f( void ) {
	char	demoName[MAX_OSPATH];

	if (Cmd_Argc() != 2) {
		Com_Printf ("demo <demoname>\n");
		return;
	}

	Q_strncpyz( demoName, Cmd_Argv(1), sizeof (demoName) );

	CL_PlayDemo( demoName );
}


/*
====================
CL_StartDemoLoop

Closing the main menu will restart the demo loop
====================
*/
void CL_StartDemoLoop( void ) {
	// start the demo loop again
	Cbuf_AddText ("d1\n");
}

/*
==================
CL_NextDemo

Called when a demo or cinematic finishes
If the "nextdemo" cvar is set, that command will be issued
==================
*/
void CL_NextDemo( void ) {
	char	v[MAX_STRING_CHARS];

	Q_strncpyz( v, Cvar_VariableString ("nextdemo"), sizeof(v) );
	v[MAX_STRING_CHARS-1] = 0;
	Com_DPrintf("CL_NextDemo: %s\n", v );
	if (!v[0]) {
		return;
	}

	Cvar_Set ("nextdemo","");
	Cbuf_AddText (v);
	Cbuf_AddText ("\n");
	Cbuf_Execute();
}

/*
==================
CL_DemoState

Returns the current state of the demo system
==================
*/
demoState_t CL_DemoState( void ) {
	if( clc.demoplaying ) {
		return DS_PLAYBACK;
	} else if( clc.demorecording ) {
		return DS_RECORDING;
	} else {
		return DS_NONE;
	}
}

/*
==================
CL_DemoPos

Returns the current position of the demo
==================
*/
int CL_DemoPos( void ) {
	if( clc.demoplaying || clc.demorecording ) {
		return FS_FTell( clc.demofile );
	} else {
		return 0;
	}
}

/*
==================
CL_DemoLength

Returns the length of the playing demo
==================
*/
int CL_DemoLength( void ) {
	if( clc.demoplaying ) {
		return clc.demoLength;
	} else {
		return 0;
	}
}

/*
==================
CL_DemoName

Returns the name of the demo
==================
*/
void CL_DemoName( char *buffer, int size ) {
	if( clc.demoplaying || clc.demorecording ) {
		Q_strncpyz( buffer, clc.demoName, size );
	} else if( size >= 1 ) {
		buffer[ 0 ] = '\0';
	}
}


//======================================================================

/*
=====================
CL_ShutdownAll
=====================
*/
void CL_ShutdownAll(qboolean shutdownRef)
{
	if(CL_VideoRecording())
		CL_CloseAVI();

	if(clc.demorecording)
		CL_StopRecord_f();

#ifdef USE_CURL
	CL_cURL_Shutdown();
#endif
	// clear sounds
	S_DisableSounds();
	// shutdown CGame
	CL_ShutdownCGame();

	// shutdown the renderer
	if(shutdownRef)
		Com_ShutdownRef();
	else if(re.Shutdown)
		re.Shutdown(qfalse);		// don't destroy window or context

	cls.cgameStarted = qfalse;
	cls.rendererStarted = qfalse;
	cls.soundRegistered = qfalse;
}

/*
=================
CL_ClearMemory

Called by Com_GameRestart
=================
*/
void CL_ClearMemory(qboolean shutdownRef)
{
	// shutdown all the client stuff
	CL_ShutdownAll(shutdownRef);

	// if not running a server clear the whole hunk
	if ( !com_sv_running || !com_sv_running->integer ) {
		// clear the whole hunk
		Hunk_Clear();
		// clear collision map data
		CM_ClearMap();
		// clear net fields
		MSG_ShutdownNetFields();
	}
	else {
		// clear all the client data on the hunk
		Hunk_ClearToMark();
	}
}

/*
=================
CL_FlushMemory

Called by CL_MapLoading, CL_Connect_f, CL_PlayDemo_f, and CL_ParseGamestate the only
ways a client gets into a game
Also called by Com_Error
=================
*/
void CL_FlushMemory(void)
{
	CL_ClearMemory(qfalse);
	CL_StartHunkUsers(qfalse);
}

/*
=================
CL_SetChallenging

Set state to challenging
=================
*/
void CL_SetChallenging( void ) {
	if ( clc.state == CA_CHALLENGING ) {
		return;
	}

	clc.state = CA_CHALLENGING;
	clc.desiredPlayerBits = Com_Clamp(1, (1<<CL_MAX_SPLITVIEW)-1, Cvar_VariableIntegerValue("cl_localPlayers"));

	// Reset the desired local players bits (must set each time before joining)
	Cvar_Set("cl_localPlayers", "1");
}

/*
=====================
CL_MapLoading

A local server is starting to load a map, so update the
screen to let the user know about it, then dump all client
memory on the hunk from cgame and renderer
=====================
*/
void CL_MapLoading( void ) {
	if ( com_dedicated->integer ) {
		clc.state = CA_DISCONNECTED;
		return;
	}

	if ( !com_cl_running->integer ) {
		return;
	}

	Con_Close();

	// if we are already connected to the local host, stay connected
	if ( clc.state >= CA_CONNECTED && !Q_stricmp( clc.servername, "localhost" ) ) {
		clc.state = CA_CONNECTED;		// so the connect screen is drawn
		Com_Memset( cls.updateInfoString, 0, sizeof( cls.updateInfoString ) );
		Com_Memset( clc.serverMessage, 0, sizeof( clc.serverMessage ) );
		Com_Memset( &cl.gameState, 0, sizeof( cl.gameState ) );
		clc.lastPacketSentTime = -9999;
		SCR_UpdateScreen();
	} else {
		// clear nextmap so the cinematic shutdown doesn't execute it
		Cvar_Set( "nextmap", "" );
		CL_Disconnect( qtrue );
		Q_strncpyz( clc.servername, "localhost", sizeof(clc.servername) );
		CL_SetChallenging();		// so the connect screen is drawn
		SCR_UpdateScreen();
		clc.connectTime = -RETRANSMIT_TIMEOUT;
		NET_StringToAdr( clc.servername, &clc.serverAddress, NA_UNSPEC);
		// we don't need a challenge on the localhost

		CL_CheckForResend();
	}
}

/*
=====================
CL_ClearState

Called before parsing a gamestate
=====================
*/
void CL_ClearState (void) {
	int index;

//	S_StopAllSounds();

	// free client structure
	for (index = 0; index < PACKET_BACKUP; index++) {
		DA_Free( &cl.snapshots[index].playerStates );
		cl.snapshots[index].valid = qfalse;
	}

	DA_Free( &cl.tempSnapshotPS );

	DA_Free( &cl.entityBaselines );
	DA_Free( &cl.parseEntities );

	Com_Memset( &cl, 0, sizeof( cl ) );
}

/*
=====================
CL_InitConnection
=====================
*/
void CL_InitConnection (qboolean clear) {
	int i;

	if (clear) {
		// wipe the client connection
		Com_Memset( &clc, 0, sizeof( clc ) );
	}

	clc.state = CA_DISCONNECTED;	// no longer CA_UNINITIALIZED

	for (i = 0; i < MAX_SPLITVIEW; i++) {
		clc.playerNums[i] = -1;
	}
}

/*
====================
CL_UpdateGUID

update cl_guid using QKEY_FILE and optional prefix
====================
*/
static void CL_UpdateGUID( const char *prefix, int prefix_len )
{
	fileHandle_t f;
	int len;

	len = FS_SV_FOpenFileRead( QKEY_FILE, &f );
	FS_FCloseFile( f );

	if( len != QKEY_SIZE ) 
		Cvar_Set( "cl_guid", "" );
	else
		Cvar_Set( "cl_guid", Com_MD5File( QKEY_FILE, QKEY_SIZE,
			prefix, prefix_len ) );
}

static void CL_OldGame(void)
{
	if(cl_oldGameSet)
	{
		// change back to previous fs_game
		cl_oldGameSet = qfalse;
		Cvar_Set("fs_game", cl_oldGame);
		FS_ConditionalRestart(qfalse);
	}
}

/*
=====================
CL_Disconnect

Called when a connection, demo, or cinematic is being terminated.
Goes from a connected state to either a menu state or a console state
Sends a disconnect message to the server
This is also called on Com_Error and Com_Quit, so it shouldn't cause any errors
=====================
*/
void CL_Disconnect( qboolean showMainMenu ) {
	if ( !com_cl_running || !com_cl_running->integer ) {
		return;
	}

	if ( clc.demorecording ) {
		CL_StopRecord_f ();
	}

	if (clc.download) {
		FS_FCloseFile( clc.download );
		clc.download = 0;
	}
	*clc.downloadTempName = *clc.downloadName = 0;
	Cvar_Set( "cl_downloadName", "" );

#ifdef USE_MUMBLE
	if (cl_useMumble->integer && mumble_islinked()) {
		Com_Printf("Mumble: Unlinking from Mumble application\n");
		mumble_unlink();
	}
#endif

#ifdef USE_VOIP
	if (cl_voipSend->integer) {
		int tmp = cl_voipUseVAD->integer;
		cl_voipUseVAD->integer = 0;  // disable this for a moment.
		clc.voipOutgoingDataSize = 0;  // dump any pending VoIP transmission.
		Cvar_Set("cl_voipSend", "0");
		CL_CaptureVoip();  // clean up any state...
		cl_voipUseVAD->integer = tmp;
	}

	if (clc.voipCodecInitialized) {
		int i;
		opus_encoder_destroy(clc.opusEncoder);
		for (i = 0; i < MAX_CLIENTS; i++) {
			opus_decoder_destroy(clc.opusDecoder[i]);
		}
		clc.voipCodecInitialized = qfalse;
	}
	Cmd_RemoveCommand ("voip");
#endif

	if ( clc.demofile ) {
		FS_FCloseFile( clc.demofile );
		clc.demofile = 0;
	}

	if ( cgvm && showMainMenu ) {
		CL_ShowMainMenu();
	}

	S_ClearSoundBuffer();

	// send a disconnect message to the server
	// send it a few times in case one is dropped
	if ( clc.state >= CA_CONNECTED ) {
		CL_AddReliableCommand("disconnect", qtrue);
		CL_WritePacket();
		CL_WritePacket();
		CL_WritePacket();
	}
	
	// Remove pure paks
	FS_PureServerSetLoadedPaks("", "");
	FS_PureServerSetReferencedPaks( "", "" );
	
	CL_ClearState ();
	CL_InitConnection (qtrue);

	// allow cheats locally
	Cvar_Set( "sv_cheats", "1" );

#ifdef USE_VOIP
	// not connected to voip server anymore.
	clc.voipEnabled = qfalse;
#endif

	// Stop recording any video
	if( clc.demoplaying && CL_VideoRecording( ) ) {
		// Finish rendering current frame
		SCR_UpdateScreen( );
		CL_CloseAVI( );
	}

	CL_UpdateGUID( NULL, 0 );

	if(!noGameRestart)
		CL_OldGame();
	else
		noGameRestart = qfalse;
}

/*
===================
CL_RequestMotd

===================
*/
void CL_RequestMotd( void ) {
#ifdef UPDATE_SERVER_NAME
	char		info[MAX_INFO_STRING];

	if ( !cl_motd->integer ) {
		return;
	}
	Com_Printf( "Resolving %s\n", UPDATE_SERVER_NAME );
	if ( !NET_StringToAdr( UPDATE_SERVER_NAME, &cls.updateServer, NA_IP ) ) {
		Com_Printf( "Couldn't resolve address\n" );
		return;
	}
	cls.updateServer.port = BigShort( PORT_UPDATE );
	Com_Printf( "%s resolved to %i.%i.%i.%i:%i\n", UPDATE_SERVER_NAME,
		cls.updateServer.ip[0], cls.updateServer.ip[1],
		cls.updateServer.ip[2], cls.updateServer.ip[3],
		BigShort( cls.updateServer.port ) );
	
	info[0] = 0;

	Com_sprintf( cls.updateChallenge, sizeof( cls.updateChallenge ), "%i", (int)((((unsigned int)rand() << 16) ^ (unsigned int)rand()) ^ Com_Milliseconds()));

	Info_SetValueForKey( info, "challenge", cls.updateChallenge );
	Info_SetValueForKey( info, "renderer", cls.glconfig.renderer_string );
	Info_SetValueForKey( info, "version", com_version->string );

	NET_OutOfBandPrint( NS_CLIENT, cls.updateServer, "getmotd \"%s\"\n", info );
#endif
}

/*
======================================================================

CONSOLE COMMANDS

======================================================================
*/

/*
==================
CL_ForwardToServer_f
==================
*/
void CL_ForwardToServer_f( void ) {
	if ( clc.state != CA_ACTIVE || clc.demoplaying ) {
		Com_Printf ("Not connected to a server.\n");
		return;
	}
	
	// don't forward the first argument
	if ( Cmd_Argc() > 1 ) {
		CL_AddReliableCommand(Cmd_Args(), qfalse);
	}
}

#if CL_MAX_SPLITVIEW > 1
/*
==================
CL_DropIn
==================
*/
void CL_DropIn( int localPlayerNum ) {
	if ( clc.state != CA_ACTIVE || clc.demoplaying ) {
		Com_Printf ("Not connected to a server.\n");
		return;
	}

	CL_AddReliableCommand(va("dropin%d \"%s\"", localPlayerNum+1, Cvar_InfoString( cl_userinfoFlags[localPlayerNum] )), qfalse);
}

/*
==================
CL_DropOut
==================
*/
void CL_DropOut( int localPlayerNum ) {
	if ( clc.state != CA_ACTIVE || clc.demoplaying ) {
		Com_Printf ("Not connected to a server.\n");
		return;
	}

	CL_AddReliableCommand(va("dropout%d", localPlayerNum+1), qfalse);
}

void CL_DropIn_f( void ) {
	CL_DropIn(0);
}

void CL_DropOut_f( void ) {
	CL_DropOut(0);
}

void CL_2DropIn_f( void ) {
	CL_DropIn(1);
}

void CL_2DropOut_f( void ) {
	CL_DropOut(1);
}
#endif

#if CL_MAX_SPLITVIEW > 2
void CL_3DropIn_f( void ) {
	CL_DropIn(2);
}

void CL_3DropOut_f( void ) {
	CL_DropOut(2);
}
#endif

#if CL_MAX_SPLITVIEW > 3
void CL_4DropIn_f( void ) {
	CL_DropIn(3);
}

void CL_4DropOut_f( void ) {
	CL_DropOut(3);
}
#endif

/*
==================
CL_Disconnect_f
==================
*/
void CL_Disconnect_f( void ) {
	Cvar_Set("ui_singlePlayerActive", "0");
	if ( clc.state != CA_DISCONNECTED ) {
		Com_Error (ERR_DISCONNECT, "Disconnected from server");
	}
}


/*
================
CL_Reconnect_f

================
*/
void CL_Reconnect_f( void ) {
	if ( !strlen( cl_reconnectArgs ) )
		return;
	Cvar_Set("ui_singlePlayerActive", "0");
	Cbuf_AddText( va("connect %s\n", cl_reconnectArgs ) );
}

/*
================
CL_Connect_f

================
*/
void CL_Connect_f( void ) {
	char	server[MAX_OSPATH];
	const char	*serverString;
	int argc = Cmd_Argc();
	netadrtype_t family = NA_UNSPEC;

	if ( argc != 2 && argc != 3 ) {
		Com_Printf( "usage: connect [-4|-6] server\n");
		return;	
	}
	
	if(argc == 2)
		Q_strncpyz( server, Cmd_Argv(1), sizeof( server ) );
	else
	{
		if(!strcmp(Cmd_Argv(1), "-4"))
			family = NA_IP;
		else if(!strcmp(Cmd_Argv(1), "-6"))
			family = NA_IP6;
		else
			Com_Printf( "warning: only -4 or -6 as address type understood.\n");
		
		Q_strncpyz( server, Cmd_Argv(2), sizeof( server ) );
	}

	// save arguments for reconnect
	Q_strncpyz( cl_reconnectArgs, Cmd_Args(), sizeof( cl_reconnectArgs ) );

	Cvar_Set("ui_singlePlayerActive", "0");

	// fire a message off to the motd server
	CL_RequestMotd();

	// clear any previous "server full" type messages
	clc.serverMessage[0] = 0;

	if ( com_sv_running->integer && !strcmp( server, "localhost" ) ) {
		// if running a local server, kill it
		SV_Shutdown( "Server quit" );
	}

	// make sure a local server is killed
	Cvar_Set( "sv_killserver", "1" );
	SV_Frame( 0 );

	noGameRestart = qtrue;
	CL_Disconnect( qtrue );
	Con_Close();

	Q_strncpyz( clc.servername, server, sizeof(clc.servername) );

	if (!NET_StringToAdr(clc.servername, &clc.serverAddress, family) ) {
		Com_Printf ("Bad server address\n");
		clc.state = CA_DISCONNECTED;
		return;
	}
	if (clc.serverAddress.port == 0) {
		clc.serverAddress.port = BigShort( PORT_SERVER );
	}

	serverString = NET_AdrToStringwPort(clc.serverAddress);

	Com_Printf( "%s resolved to %s\n", clc.servername, serverString);

	if( cl_guidServerUniq->integer )
		CL_UpdateGUID( serverString, strlen( serverString ) );
	else
		CL_UpdateGUID( NULL, 0 );

	// if we aren't playing on a lan, send challenge to prevent connection hijacking
	if(NET_IsLocalAddress(clc.serverAddress))
		CL_SetChallenging();
	else
	{
		clc.state = CA_CONNECTING;
		
		// Set a client challenge number that ideally is mirrored back by the server.
		clc.challenge = (((unsigned int)rand() << 16) ^ (unsigned int)rand()) ^ Com_Milliseconds();
	}

	clc.connectTime = -99999;	// CL_CheckForResend() will fire immediately
	clc.connectPacketCount = 0;

	// server connection string
	Cvar_Set( "cl_currentServerAddress", server );
}

#define MAX_RCON_MESSAGE 1024

/*
==================
CL_CompleteRcon
==================
*/
static void CL_CompleteRcon( char *args, int argNum )
{
	if( argNum == 2 )
	{
		// Skip "rcon "
		char *p = Com_SkipTokens( args, 1, " " );

		if( p > args )
			Field_CompleteCommand( p, qtrue, qtrue );
	}
}

/*
=====================
CL_Rcon_f

  Send the rest of the command line over as
  an unconnected command.
=====================
*/
void CL_Rcon_f( void ) {
	char	message[MAX_RCON_MESSAGE];
	netadr_t	to;

	if ( !rcon_client_password->string[0] ) {
		Com_Printf ("You must set 'rconpassword' before\n"
					"issuing an rcon command.\n");
		return;
	}

	message[0] = -1;
	message[1] = -1;
	message[2] = -1;
	message[3] = -1;
	message[4] = 0;

	Q_strcat (message, MAX_RCON_MESSAGE, "rcon ");

	Q_strcat (message, MAX_RCON_MESSAGE, rcon_client_password->string);
	Q_strcat (message, MAX_RCON_MESSAGE, " ");

	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=543
	Q_strcat (message, MAX_RCON_MESSAGE, Cmd_Cmd()+5);

	if ( clc.state >= CA_CONNECTED ) {
		to = clc.netchan.remoteAddress;
	} else {
		if (!strlen(rconAddress->string)) {
			Com_Printf ("You must either be connected,\n"
						"or set the 'rconAddress' cvar\n"
						"to issue rcon commands\n");

			return;
		}
		NET_StringToAdr (rconAddress->string, &to, NA_UNSPEC);
		if (to.port == 0) {
			to.port = BigShort (PORT_SERVER);
		}
	}
	
	NET_SendPacket (NS_CLIENT, strlen(message)+1, message, to);
	cls.rconAddress = to;
}

/*
=================
CL_Vid_Restart_f

Restart the video subsystem

we also have to reload CGame because the renderer
doesn't know what graphics to reload
=================
*/
void CL_Vid_Restart_f( void ) {

	// Settings may have changed so stop recording now
	if( CL_VideoRecording( ) ) {
		CL_CloseAVI( );
	}

	if(clc.demorecording)
		CL_StopRecord_f();

	// don't let them loop during the restart
	S_StopAllSounds();

	if(!FS_ConditionalRestart(qtrue))
	{
		// if not running a server clear the whole hunk
		if(com_sv_running->integer)
		{
			// clear all the client data on the hunk
			Hunk_ClearToMark();
		}
		else
		{
			// clear the whole hunk
			Hunk_Clear();
		}
	
		// shutdown the CGame
		CL_ShutdownCGame();
		// shutdown the renderer and clear the renderer interface
		Com_ShutdownRef();

		cls.rendererStarted = qfalse;
		cls.cgameStarted = qfalse;
		cls.soundRegistered = qfalse;

		cls.drawnLoadingScreen = qfalse;

		// unpause so the cgame definitely gets a snapshot and renders a frame
		Cvar_Set("cl_paused", "0");

		// initialize the renderer interface
		CL_InitRef();

		// startup all the client stuff
		CL_StartHunkUsers(qfalse);

		// let server game re-register models
		if (com_sv_running->integer) {
			// XXX
			extern void SV_GameVidRestart(void);
			SV_GameVidRestart();
		}
	}
}

/*
=================
CL_Snd_Restart

Restart the sound subsystem
=================
*/
void CL_Snd_Shutdown(void)
{
	S_Shutdown();
	cls.soundStarted = qfalse;
}

/*
=================
CL_Snd_Restart_f

Restart the sound subsystem
The cgame and game must also be forced to restart because
handles will be invalid
=================
*/
void CL_Snd_Restart_f(void)
{
	CL_Snd_Shutdown();
	// sound will be reinitialized by vid_restart
	CL_Vid_Restart_f();
}


/*
==================
CL_PK3List_f
==================
*/
void CL_OpenedPK3List_f( void ) {
	Com_Printf("Opened PK3 Names: %s\n", FS_LoadedPakNames());
}

/*
==================
CL_PureList_f
==================
*/
void CL_ReferencedPK3List_f( void ) {
	Com_Printf("Referenced PK3 Names: %s\n", FS_ReferencedPakNames());
}

/*
==================
CL_Configstrings_f
==================
*/
void CL_Configstrings_f( void ) {
	int		i;
	int		ofs;

	if ( clc.state != CA_ACTIVE ) {
		Com_Printf( "Not connected to a server.\n");
		return;
	}

	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		ofs = cl.gameState.stringOffsets[ i ];
		if ( !ofs ) {
			continue;
		}
		Com_Printf( "%4i: %s\n", i, cl.gameState.stringData + ofs );
	}
}

/*
==============
CL_Clientinfo_f
==============
*/
void CL_Clientinfo_f( void ) {
	Com_Printf( "--------- Client Information ---------\n" );
	Com_Printf( "state: %i\n", clc.state );
	Com_Printf( "Server: %s\n", clc.servername );
	Com_Printf ("User info settings:\n");
	Info_Print( Cvar_InfoString( CVAR_USERINFO ) );
	Com_Printf( "--------------------------------------\n" );
}


//====================================================================

/*
=================
CL_DownloadsComplete

Called when all downloading has been completed
=================
*/
void CL_DownloadsComplete( void ) {

#ifdef USE_CURL
	// if we downloaded with cURL
	if(clc.cURLUsed) { 
		clc.cURLUsed = qfalse;
		CL_cURL_Shutdown();
		if( clc.cURLDisconnected ) {
			if(clc.downloadRestart) {
				clc.downloadRestart = qfalse;
				clc.missingDefaultCfg[0] = '\0';

				FS_Restart( qfalse ); // We possibly downloaded a pak, restart the file system to load it
				clc.fsRestarted = qtrue;

				// still missing default.cfg after downloading files
				if ( clc.missingDefaultCfg[0] ) {
					Com_Error( ERR_DROP, "Couldn't load default.cfg for %s", clc.missingDefaultCfg );
				}
			}
			clc.cURLDisconnected = qfalse;
			CL_Reconnect_f();
			return;
		}
	}
#endif

	// if we downloaded files we need to restart the file system
	if (clc.downloadRestart) {
		clc.downloadRestart = qfalse;
		clc.missingDefaultCfg[0] = '\0';

		FS_Restart( qfalse ); // We possibly downloaded a pak, restart the file system to load it
		clc.fsRestarted = qtrue;

		// still missing default.cfg after downloading files
		if ( clc.missingDefaultCfg[0] ) {
			Com_Error( ERR_DROP, "Couldn't load default.cfg for %s", clc.missingDefaultCfg );
		}

		// inform the server so we get new gamestate info
		CL_AddReliableCommand("donedl", qfalse);

		// by sending the donedl command we request a new gamestate
		// so we don't want to load stuff yet
		return;
	}

	// must restart filesystem at connect to reload mint-game.settings
	if ( !clc.fsRestarted && !com_sv_running->integer ) {
		FS_Restart( qfalse );
		clc.fsRestarted = qtrue;
	}

	if ( clc.missingDefaultCfg[0] ) {
		Com_Error( ERR_DROP, "Couldn't load default.cfg for %s", clc.missingDefaultCfg );
	}

	// let the client game init and load data
	clc.state = CA_LOADING;

	// flush client memory and start loading stuff
	// this will also (re)load the cgame vm
	// if this is a local client then only the client part of the hunk
	// will be cleared, note that this is done after the hunk mark has been set
	CL_FlushMemory();

	CL_WritePacket();
	CL_WritePacket();
	CL_WritePacket();
}

/*
=================
CL_BeginDownload

Requests a file to download from the server.  Stores it in the current
game directory.
=================
*/
void CL_BeginDownload( const char *localName, const char *remoteName ) {

	Com_DPrintf("***** CL_BeginDownload *****\n"
				"Localname: %s\n"
				"Remotename: %s\n"
				"****************************\n", localName, remoteName);

	Q_strncpyz ( clc.downloadName, localName, sizeof(clc.downloadName) );
	Com_sprintf( clc.downloadTempName, sizeof(clc.downloadTempName), "%s.tmp", localName );

	// Set so UI gets access to it
	Cvar_Set( "cl_downloadName", remoteName );
	Cvar_Set( "cl_downloadSize", "0" );
	Cvar_Set( "cl_downloadCount", "0" );
	Cvar_SetValue( "cl_downloadTime", cls.realtime );

	clc.downloadBlock = 0; // Starting new file
	clc.downloadCount = 0;

	CL_AddReliableCommand(va("download %s", remoteName), qfalse);
}

/*
=================
CL_NextDownload

A download completed or failed
=================
*/
void CL_NextDownload(void)
{
	char *s;
	char *remoteName, *localName;
	qboolean useCURL = qfalse;

	// A download has finished, check whether this matches a referenced checksum
	if(*clc.downloadName)
	{
		char *zippath = FS_BuildOSPath(Cvar_VariableString("fs_homepath"), clc.downloadName, "");
		zippath[strlen(zippath)-1] = '\0';

		if(!FS_CompareZipChecksum(zippath))
			Com_Error(ERR_DROP, "Incorrect checksum for file: %s", clc.downloadName);
	}

	*clc.downloadTempName = *clc.downloadName = 0;
	Cvar_Set("cl_downloadName", "");

	// We are looking to start a download here
	if (*clc.downloadList) {
		s = clc.downloadList;

		// format is:
		//  @remotename@localname@remotename@localname, etc.

		if (*s == '@')
			s++;
		remoteName = s;
		
		if ( (s = strchr(s, '@')) == NULL ) {
			CL_DownloadsComplete();
			return;
		}

		*s++ = 0;
		localName = s;
		if ( (s = strchr(s, '@')) != NULL )
			*s++ = 0;
		else
			s = localName + strlen(localName); // point at the nul byte
#ifdef USE_CURL
		if(!(cl_allowDownload->integer & DLF_NO_REDIRECT)) {
			if(clc.sv_allowDownload & DLF_NO_REDIRECT) {
				Com_Printf("WARNING: server does not "
					"allow download redirection "
					"(sv_allowDownload is %d)\n",
					clc.sv_allowDownload);
			}
			else if(!*clc.sv_dlURL) {
				Com_Printf("WARNING: server allows "
					"download redirection, but does not "
					"have sv_dlURL set\n");
			}
			else if(!CL_cURL_Init()) {
				Com_Printf("WARNING: could not load "
					"cURL library\n");
			}
			else {
				CL_cURL_BeginDownload(localName, va("%s/%s",
					clc.sv_dlURL, remoteName));
				useCURL = qtrue;
			}
		}
		else if(!(clc.sv_allowDownload & DLF_NO_REDIRECT)) {
			Com_Printf("WARNING: server allows download "
				"redirection, but it disabled by client "
				"configuration (cl_allowDownload is %d)\n",
				cl_allowDownload->integer);
		}
#endif /* USE_CURL */
		if(!useCURL) {
			if((cl_allowDownload->integer & DLF_NO_UDP)) {
				Com_Error(ERR_DROP, "UDP Downloads are "
					"disabled on your client. "
					"(cl_allowDownload is %d)",
					cl_allowDownload->integer);
				return;	
			}
			else {
				CL_BeginDownload( localName, remoteName );
			}
		}
		clc.downloadRestart = qtrue;

		// move over the rest
		memmove( clc.downloadList, s, strlen(s) + 1);

		return;
	}

	CL_DownloadsComplete();
}

/*
=================
CL_InitDownloads

After receiving a valid game state, we valid the cgame and local zip files here
and determine if we need to download them
=================
*/
void CL_InitDownloads(void) {
	char missingfiles[1024];

	// it's possible that some referenced files on the server are missing
	if ( FS_ComparePaks( missingfiles, sizeof( missingfiles ), qfalse ) )
	{
		// NOTE TTimo I would rather have that printed as a modal message box
		//   but at this point while joining the game we don't know wether we will successfully join or not
		Com_Printf( "\nWARNING: You are missing some files referenced by the server:\n%s", missingfiles );

		if ( !(cl_allowDownload->integer & DLF_ENABLE) )
		{
			Com_Printf( "You might not be able to join the game\n"
						"Go to the setting menu to turn on autodownload, or get the file elsewhere\n\n" );
		}
		else
		{
			Com_Printf( "You might not be able to join the game\n\n" );
		}
	}

	if ( (cl_allowDownload->integer & DLF_ENABLE) && FS_ComparePaks( clc.downloadList, sizeof( clc.downloadList ) , qtrue ) ) {

		Com_Printf("Need paks: %s\n", clc.downloadList );

		if ( *clc.downloadList ) {
			// if autodownloading is not enabled on the server
			clc.state = CA_CONNECTED;

			*clc.downloadTempName = *clc.downloadName = 0;
			Cvar_Set( "cl_downloadName", "" );

			CL_NextDownload();
			return;
		}

	}
		
	CL_DownloadsComplete();
}

/*
=================
CL_MissingDefaultCfg

Client connected to a remote server and when changing fs_game found
that it was missing default.cfg.
=================
*/
void CL_MissingDefaultCfg( const char *gamedir ) {
	Q_strncpyz( clc.missingDefaultCfg, gamedir, sizeof ( clc.missingDefaultCfg ) );
}

/*
=================
CL_CheckForResend

Resend a connect message if the last one has timed out
=================
*/
void CL_CheckForResend( void ) {
	int		port, i;
	int		protocol;
	char	info[MAX_INFO_STRING];
	char	data[(MAX_INFO_STRING+3)*CL_MAX_SPLITVIEW + 7];

	// don't send anything if playing back a demo
	if ( clc.demoplaying ) {
		return;
	}

	// resend if we haven't gotten a reply yet
	if ( clc.state != CA_CONNECTING && clc.state != CA_CHALLENGING ) {
		return;
	}

	if ( cls.realtime - clc.connectTime < RETRANSMIT_TIMEOUT ) {
		return;
	}

	clc.connectTime = cls.realtime;	// for retransmit requests
	clc.connectPacketCount++;


	switch ( clc.state ) {
	case CA_CONNECTING:
		// The challenge request shall be followed by a client challenge so no malicious server can hijack this connection.
		// Add the gamename so the server knows we're running the correct game or can reject the client
		// with a meaningful message
		Com_sprintf(data, sizeof(data), "getchallenge %d %s", clc.challenge, com_gamename->string);

		NET_OutOfBandPrint(NS_CLIENT, clc.serverAddress, "%s", data);
		break;
		
	case CA_CHALLENGING:
		// sending back the challenge
		port = Cvar_VariableValue ("net_qport");

#ifdef LEGACY_PROTOCOL
		if(com_legacyprotocol->integer == com_protocol->integer)
			clc.compat = qtrue;

		if(clc.compat)
			protocol = com_legacyprotocol->integer;
		else
#endif
			protocol = com_protocol->integer;
		
		Q_strncpyz( data, "connect", sizeof( data ) );

		for ( i = 0; i < CL_MAX_SPLITVIEW; i++ ) {
			if ( !( clc.desiredPlayerBits & ( 1 << i ) ) ) {
				// dummy string so server knows which local player the
				// following info strings are for
				Q_strcat( data, sizeof( data ), " \"\"" );
				continue;
			}

			Q_strncpyz( info, Cvar_InfoString( cl_userinfoFlags[i] ), sizeof( info ) );

			Info_SetValueForKey( info, "protocol", va( "%i", protocol ) );
			Info_SetValueForKey( info, "qport", va( "%i", port ) );
			Info_SetValueForKey( info, "challenge", va( "%i", clc.challenge ) );

			Q_strcat( data, sizeof( data ), va( " \"%s\"", info ) );
		}

		NET_OutOfBandData( NS_CLIENT, clc.serverAddress, (byte *) data, strlen ( data ) );

		// the most current userinfo has been sent, so watch for any
		// newer changes to userinfo variables
		cvar_modifiedFlags &= ~CVAR_USERINFO_ALL;
		break;

	default:
		Com_Error( ERR_FATAL, "CL_CheckForResend: bad clc.state" );
	}
}

/*
===================
CL_MotdPacket

===================
*/
void CL_MotdPacket( netadr_t from ) {
#ifdef UPDATE_SERVER_NAME
	char	*challenge;
	char	*info;

	// if not from our server, ignore it
	if ( !NET_CompareAdr( from, cls.updateServer ) ) {
		return;
	}

	info = Cmd_Argv(1);

	// check challenge
	challenge = Info_ValueForKey( info, "challenge" );
	if ( strcmp( challenge, cls.updateChallenge ) ) {
		return;
	}

	challenge = Info_ValueForKey( info, "motd" );

	Q_strncpyz( cls.updateInfoString, info, sizeof( cls.updateInfoString ) );
	Cvar_Set( "cl_motdString", challenge );
#endif
}

/*
===================
CL_InitServerInfo
===================
*/
void CL_InitServerInfo( serverInfo_t *server, netadr_t *address ) {
	server->adr = *address;
	server->clients = 0;
	server->hostName[0] = '\0';
	server->mapName[0] = '\0';
	server->maxClients = 0;
	server->maxPing = 0;
	server->minPing = 0;
	server->ping = -1;
	server->game[0] = '\0';
	server->gameType[0] = '\0';
	server->netType = 0;
	server->g_humanplayers = 0;
	server->g_needpass = 0;
}

#define MAX_SERVERSPERPACKET	256

/*
===================
CL_ServersResponsePacket
===================
*/
void CL_ServersResponsePacket( const netadr_t* from, msg_t *msg, qboolean extended ) {
	int				i, j, count, total;
	netadr_t addresses[MAX_SERVERSPERPACKET];
	int				numservers;
	byte*			buffptr;
	byte*			buffend;
	
	Com_Printf("CL_ServersResponsePacket from %s\n", NET_AdrToStringwPort(*from));

	if (cls.numglobalservers == -1) {
		// state to detect lack of servers or lack of response
		cls.numglobalservers = 0;
		cls.numGlobalServerAddresses = 0;
	}

	// parse through server response string
	numservers = 0;
	buffptr    = msg->data;
	buffend    = buffptr + msg->cursize;

	// advance to initial token
	do
	{
		if(*buffptr == '\\' || (extended && *buffptr == '/'))
			break;
		
		buffptr++;
	} while (buffptr < buffend);

	while (buffptr + 1 < buffend)
	{
		// IPv4 address
		if (*buffptr == '\\')
		{
			buffptr++;

			if (buffend - buffptr < sizeof(addresses[numservers].ip) + sizeof(addresses[numservers].port) + 1)
				break;

			for(i = 0; i < sizeof(addresses[numservers].ip); i++)
				addresses[numservers].ip[i] = *buffptr++;

			addresses[numservers].type = NA_IP;
		}
		// IPv6 address, if it's an extended response
		else if (extended && *buffptr == '/')
		{
			buffptr++;

			if (buffend - buffptr < sizeof(addresses[numservers].ip6) + sizeof(addresses[numservers].port) + 1)
				break;
			
			for(i = 0; i < sizeof(addresses[numservers].ip6); i++)
				addresses[numservers].ip6[i] = *buffptr++;
			
			addresses[numservers].type = NA_IP6;
			addresses[numservers].scope_id = from->scope_id;
		}
		else
			// syntax error!
			break;
			
		// parse out port
		addresses[numservers].port = (*buffptr++) << 8;
		addresses[numservers].port += *buffptr++;
		addresses[numservers].port = BigShort( addresses[numservers].port );

		// syntax check
		if (*buffptr != '\\' && *buffptr != '/')
			break;
	
		numservers++;
		if (numservers >= MAX_SERVERSPERPACKET)
			break;
	}

	count = cls.numglobalservers;

	for (i = 0; i < numservers && count < MAX_GLOBAL_SERVERS; i++) {
		// build net address
		serverInfo_t *server = &cls.globalServers[count];

		// Tequila: It's possible to have sent many master server requests. Then
		// we may receive many times the same addresses from the master server.
		// We just avoid to add a server if it is still in the global servers list.
		for (j = 0; j < count; j++)
		{
			if (NET_CompareAdr(cls.globalServers[j].adr, addresses[i]))
				break;
		}

		if (j < count)
			continue;

		CL_InitServerInfo( server, &addresses[i] );
		// advance to next slot
		count++;
	}

	// if getting the global list
	if ( count >= MAX_GLOBAL_SERVERS && cls.numGlobalServerAddresses < MAX_GLOBAL_SERVERS )
	{
		// if we couldn't store the servers in the main list anymore
		for (; i < numservers && cls.numGlobalServerAddresses < MAX_GLOBAL_SERVERS; i++)
		{
			// just store the addresses in an additional list
			cls.globalServerAddresses[cls.numGlobalServerAddresses++] = addresses[i];
		}
	}

	cls.numglobalservers = count;
	total = count + cls.numGlobalServerAddresses;

	Com_Printf("%d servers parsed (total %d)\n", numservers, total);
}

/*
=================
CL_ConnectionlessPacket

Responses to broadcasts, etc
=================
*/
void CL_ConnectionlessPacket( netadr_t from, msg_t *msg ) {
	char	*s;
	char	*c;
	int challenge = 0;

	MSG_BeginReadingOOB( msg );
	MSG_ReadLong( msg );	// skip the -1

	s = MSG_ReadStringLine( msg );

	Cmd_TokenizeString( s );

	c = Cmd_Argv(0);

	Com_DPrintf ("CL packet %s: %s\n", NET_AdrToStringwPort(from), c);

	// challenge from the server we are connecting to
	if (!Q_stricmp(c, "challengeResponse"))
	{
		char *strver;
		int ver;
	
		if (clc.state != CA_CONNECTING)
		{
			Com_DPrintf("Unwanted challenge response received. Ignored.\n");
			return;
		}
		
		c = Cmd_Argv(2);
		if(*c)
			challenge = atoi(c);

		strver = Cmd_Argv(3);
		if(*strver)
		{
			ver = atoi(strver);
			
			if(ver != com_protocol->integer)
			{
#ifdef LEGACY_PROTOCOL
				if(com_legacyprotocol->integer > 0)
				{
					// Server is ioq3 but has a different protocol than we do.
					// Fall back to legacy protocol.
					clc.compat = qtrue;

					Com_Printf(S_COLOR_YELLOW "Warning: Server reports protocol version %d, "
						   "we have %d. Trying legacy protocol %d.\n",
						   ver, com_protocol->integer, com_legacyprotocol->integer);
				}
				else
#endif
				{
					Com_Printf(S_COLOR_YELLOW "Warning: Server reports protocol version %d, we have %d. "
						   "Trying anyways.\n", ver, com_protocol->integer);
				}
			}
		}
#ifdef LEGACY_PROTOCOL
		else
			clc.compat = qtrue;
#endif

		if(!*c || challenge != clc.challenge)
		{
			Com_Printf("Bad challenge for challengeResponse. Ignored.\n");
			return;
		}

		// start sending challenge response instead of challenge request packets
		clc.challenge = atoi(Cmd_Argv(1));
		CL_SetChallenging();
		clc.connectPacketCount = 0;
		clc.connectTime = -99999;

		// take this address as the new server address.  This allows
		// a server proxy to hand off connections to multiple servers
		clc.serverAddress = from;
		Com_DPrintf ("challengeResponse: %d\n", clc.challenge);
		return;
	}

	// server connection
	if ( !Q_stricmp(c, "connectResponse") ) {
		if ( clc.state >= CA_CONNECTED ) {
			Com_Printf ("Dup connect received. Ignored.\n");
			return;
		}
		if ( clc.state != CA_CHALLENGING ) {
			Com_Printf ("connectResponse packet while not connecting. Ignored.\n");
			return;
		}
		if ( !NET_CompareAdr( from, clc.serverAddress ) ) {
			Com_Printf( "connectResponse from wrong address. Ignored.\n" );
			return;
		}

		c = Cmd_Argv(1);

		if(*c)
			challenge = atoi(c);
		else
		{
			Com_Printf("Bad connectResponse received. Ignored.\n");
			return;
		}
		
		if(challenge != clc.challenge)
		{
			Com_Printf("ConnectResponse with bad challenge received. Ignored.\n");
			return;
		}

#ifdef LEGACY_PROTOCOL
		Netchan_Setup(NS_CLIENT, &clc.netchan, from, Cvar_VariableValue("net_qport"),
			      clc.challenge, clc.compat);
#else
		Netchan_Setup(NS_CLIENT, &clc.netchan, from, Cvar_VariableValue("net_qport"),
			      clc.challenge, qfalse);
#endif

		clc.state = CA_CONNECTED;
		clc.lastPacketSentTime = -9999;		// send first packet immediately
		return;
	}

	// server responding to an info broadcast
	if ( !Q_stricmp(c, "infoResponse") ) {
		CL_ServerInfoPacket( from, msg );
		return;
	}

	// server responding to a get playerlist
	if ( !Q_stricmp(c, "statusResponse") ) {
		CL_ServerStatusResponse( from, msg );
		return;
	}

	// echo request from server
	if ( !Q_stricmp(c, "echo") ) {
		// NOTE: we may have to add exceptions for auth and update servers
		if ( NET_CompareAdr( from, clc.serverAddress ) || NET_CompareAdr( from, cls.rconAddress ) ) {
			NET_OutOfBandPrint( NS_CLIENT, from, "%s", Cmd_Argv(1) );
		}
		return;
	}

	// global MOTD from id
	if ( !Q_stricmp(c, "motd") ) {
		CL_MotdPacket( from );
		return;
	}

	// echo request from server
	if ( !Q_stricmp(c, "print") ) {
		// NOTE: we may have to add exceptions for auth and update servers
		if ( NET_CompareAdr( from, clc.serverAddress ) || NET_CompareAdr( from, cls.rconAddress ) ) {
			s = MSG_ReadString( msg );

			Q_strncpyz( clc.serverMessage, s, sizeof( clc.serverMessage ) );
			Com_Printf( "%s", s );
		}
		return;
	}

	// list of servers sent back by a master server (classic)
	if ( !Q_strncmp(c, "getserversResponse", 18) ) {
		CL_ServersResponsePacket( &from, msg, qfalse );
		return;
	}

	// list of servers sent back by a master server (extended)
	if ( !Q_strncmp(c, "getserversExtResponse", 21) ) {
		CL_ServersResponsePacket( &from, msg, qtrue );
		return;
	}

	Com_DPrintf ("Unknown connectionless packet command.\n");
}


/*
=================
CL_PacketEvent

A packet has arrived from the main event loop
=================
*/
void CL_PacketEvent( netadr_t from, msg_t *msg ) {
	int		headerBytes;

	clc.lastPacketTime = cls.realtime;

	if ( msg->cursize >= 4 && *(int *)msg->data == -1 ) {
		CL_ConnectionlessPacket( from, msg );
		return;
	}

	if ( clc.state < CA_CONNECTED ) {
		return;		// can't be a valid sequenced packet
	}

	if ( msg->cursize < 4 ) {
		Com_Printf ("%s: Runt packet\n", NET_AdrToStringwPort( from ));
		return;
	}

	//
	// packet from server
	//
	if ( !NET_CompareAdr( from, clc.netchan.remoteAddress ) ) {
		Com_DPrintf ("%s:sequenced packet without connection\n"
			, NET_AdrToStringwPort( from ) );
		// FIXME: send a client disconnect?
		return;
	}

	if (!CL_Netchan_Process( &clc.netchan, msg) ) {
		return;		// out of order, duplicated, etc
	}

	// the header is different lengths for reliable and unreliable messages
	headerBytes = msg->readcount;

	// track the last message received so it can be returned in 
	// client messages, allowing the server to detect a dropped
	// gamestate
	clc.serverMessageSequence = LittleLong( *(int *)msg->data );

	clc.lastPacketTime = cls.realtime;
	CL_ParseServerMessage( msg );

	//
	// we don't know if it is ok to save a demo message until
	// after we have parsed the frame
	//
	if ( clc.demorecording && !clc.demowaiting ) {
		CL_WriteDemoMessage( msg, headerBytes );
	}
}

/*
==================
CL_CheckTimeout

==================
*/
void CL_CheckTimeout( void ) {
	//
	// check timeout
	//
	if ( ( !CL_CheckPaused() || !sv_paused->integer ) 
		&& clc.state >= CA_CONNECTED && clc.state != CA_CINEMATIC
	    && cls.realtime - clc.lastPacketTime > cl_timeout->value*1000) {
		if (++cl.timeoutcount > 5) {	// timeoutcount saves debugger
			Com_Printf ("\nServer connection timed out.\n");
			CL_Disconnect( qtrue );
			return;
		}
	} else {
		cl.timeoutcount = 0;
	}
}

/*
==================
CL_CheckPaused
Check whether client has been paused.
==================
*/
qboolean CL_CheckPaused(void)
{
	// if cl_paused->modified is set, the cvar has only been changed in
	// this frame. Keep paused in this frame to ensure the server doesn't
	// lag behind.
	if(cl_paused->integer || cl_paused->modified)
		return qtrue;
	
	return qfalse;
}

//============================================================================

/*
==================
CL_CheckUserinfo

==================
*/
void CL_CheckUserinfo( void ) {
	int i;

	// don't add reliable commands when not yet connected
	if(clc.state < CA_CONNECTED)
		return;

	// don't overflow the reliable command buffer when paused
	if(CL_CheckPaused())
		return;

	// send a reliable userinfo update if needed
	for(i = 0; i < CL_MAX_SPLITVIEW; i++) {
		if(cvar_modifiedFlags & cl_userinfoFlags[i]) {
			cvar_modifiedFlags &= ~cl_userinfoFlags[i];
			CL_AddReliableCommand(va("userinfo%d \"%s\"", i+1, Cvar_InfoString( cl_userinfoFlags[i] ) ), qfalse);
		}
	}
}

/*
==================
CL_Frame

==================
*/
void CL_Frame ( int msec ) {

	if ( !com_cl_running->integer ) {
		return;
	}

#ifdef USE_CURL
	if(clc.downloadCURLM) {
		CL_cURL_PerformDownload();
		// we can't process frames normally when in disconnected
		// download mode since the cgame vm expects clc.state to be
		// CA_CONNECTED
		if(clc.cURLDisconnected) {
			cls.realFrametime = msec;
			cls.frametime = msec;
			cls.realtime += cls.frametime;
			SCR_UpdateScreen();
			S_Update();
			cls.framecount++;
			return;
		}
	}
#endif

	// if recording an avi, lock to a fixed fps
	if ( CL_VideoRecording( ) && cl_aviFrameRate->integer && msec) {
		// save the current screen
		if ( !clc.demoplaying || clc.state == CA_ACTIVE || cl_forceavidemo->integer ) {
			float fps = MIN(cl_aviFrameRate->value * com_timescale->value, 1000.0f);
			float frameDuration = MAX(1000.0f / fps, 1.0f) + clc.aviVideoFrameRemainder;

			CL_TakeVideoFrame( );

			msec = (int)frameDuration;
			clc.aviVideoFrameRemainder = frameDuration - msec;
		}
	}
	
	if( cl_autoRecordDemo->integer ) {
		if( clc.state == CA_ACTIVE && !clc.demorecording && !clc.demoplaying ) {
			// If not recording a demo, and we should be, start one
			qtime_t	now;
			char		*nowString;
			char		*p;
			char		mapName[ MAX_QPATH ];
			char		serverName[ MAX_OSPATH ];

			Com_RealTime( &now );
			nowString = va( "%04d%02d%02d%02d%02d%02d",
					1900 + now.tm_year,
					1 + now.tm_mon,
					now.tm_mday,
					now.tm_hour,
					now.tm_min,
					now.tm_sec );

			Q_strncpyz( serverName, clc.servername, MAX_OSPATH );
			// Replace the ":" in the address as it is not a valid
			// file name character
			p = strstr( serverName, ":" );
			if( p ) {
				*p = '.';
			}

			Q_strncpyz( mapName, COM_SkipPath( cl.mapname ), sizeof( cl.mapname ) );
			COM_StripExtension(mapName, mapName, sizeof(mapName));

			Cbuf_ExecuteText( EXEC_NOW,
					va( "record %s-%s-%s", nowString, serverName, mapName ) );
		}
		else if( clc.state != CA_ACTIVE && clc.demorecording ) {
			// Recording, but not CA_ACTIVE, so stop recording
			CL_StopRecord_f( );
		}
	}

	// save the msec before checking pause
	cls.realFrametime = msec;

	// decide the simulation time
	cls.frametime = msec;

	cls.realtime += cls.frametime;

	if ( cl_timegraph->integer ) {
		SCR_DebugGraph ( cls.realFrametime * 0.25 );
	}

	// see if we need to update any userinfo
	CL_CheckUserinfo();

	// if we haven't gotten a packet in a long time,
	// drop the connection
	CL_CheckTimeout();

	// send intentions now
	CL_SendCmd();

	// resend a connection request if necessary
	CL_CheckForResend();

	// decide on the serverTime to render
	CL_SetCGameTime();

	// update the screen
	SCR_UpdateScreen();

	// update audio
	S_Update();

#ifdef USE_VOIP
	CL_CaptureVoip();
#endif

#ifdef USE_MUMBLE
	CL_UpdateMumble();
#endif

	cls.framecount++;
}


//============================================================================

/*
==========
CL_DrawCenteredPic

Draw shader at specified aspect scale to fit entirely on screen.
==========
*/
void CL_DrawCenteredPic( qhandle_t hShader, float aspect )
{
	float x, y, w, h;

	if ( cls.glconfig.vidWidth > cls.glconfig.vidHeight * aspect ) {
		// wide screen
		w = cls.glconfig.vidHeight * aspect;
		h = cls.glconfig.vidHeight;

		x = 0.5f * ( cls.glconfig.vidWidth - w );
		y = 0;
	} else {
		// narrow screen
		w = cls.glconfig.vidWidth;
		h = cls.glconfig.vidWidth / aspect;

		x = 0;
		y = 0.5f * ( cls.glconfig.vidHeight - h );
	}

	re.DrawStretchPic( x, y, w, h, 0, 0, 1, 1, hShader );
}

/*
============
CL_DrawLoadingScreenFrame
============
*/
void CL_DrawLoadingScreenFrame( stereoFrame_t stereoFrame, qhandle_t hShader, vec4_t color, float aspect )
{
	re.BeginFrame( stereoFrame );

	re.SetColor( color );
	re.DrawStretchPic( 0, 0, cls.glconfig.vidWidth, cls.glconfig.vidHeight, 0, 0, 0, 0, cls.whiteShader );
	re.SetColor( NULL );

	CL_DrawCenteredPic( hShader, aspect );
}

/*
============
CL_DrawLoadingScreen
============
*/
void CL_DrawLoadingScreen( void ) {
	int screenNum;
	loadingScreen_t	*screen;
	qhandle_t hShader;
	vec4_t color;

	if ( com_gameConfig.numLoadingScreens <= 0 ) {
		return;
	}

	if ( cl_loadingScreenIndex->integer >= 0 ) {
		screenNum = cl_loadingScreenIndex->integer % com_gameConfig.numLoadingScreens;
	} else {
		screenNum = 0;
	}

	Cvar_SetValue( "cl_loadingScreenIndex", screenNum + 1 );

	screen = &com_gameConfig.loadingScreens[screenNum];

	hShader = re.RegisterShaderNoMip( screen->shaderName );

	VectorCopy( screen->color, color );
	color[3] = 1.0f;

	// if running in stereo, we need to draw the frame twice
	if ( cls.glconfig.stereoEnabled || Cvar_VariableIntegerValue( "r_anaglyphMode" ) ) {
		CL_DrawLoadingScreenFrame( STEREO_LEFT, hShader, color, screen->aspect );
		CL_DrawLoadingScreenFrame( STEREO_RIGHT, hShader, color, screen->aspect );
	} else {
		CL_DrawLoadingScreenFrame( STEREO_CENTER, hShader, color, screen->aspect );
	}

	if ( com_speeds->integer ) {
		re.EndFrame( &time_frontend, &time_backend );
	} else {
		re.EndFrame( NULL, NULL );
	}
}

#ifdef USE_FLEXIBLE_DISPLAY
/*
============
CL_WindowResized
============
*/
void CL_WindowResized( void ) {
	//
	// Scaling factors and offsets for SCR_AdjustFrom640()
	//
	cls.screenXScaleStretch = cls.glconfig.vidWidth * (1.0/640);
	cls.screenYScaleStretch = cls.glconfig.vidHeight * (1.0/480);

	if ( cls.glconfig.vidWidth * 480 > cls.glconfig.vidHeight * 640 ) {
		cls.screenXScale = cls.screenXScaleStretch;
		cls.screenYScale = cls.screenYScaleStretch;
		// wide screen
		cls.screenXBias = 0.5 * ( cls.glconfig.vidWidth - ( cls.glconfig.vidHeight * (640/(float)480) ) );
		cls.screenXScale = cls.screenYScale;
		// no narrow screen
		cls.screenYBias = 0;
	} else {
		cls.screenXScale = cls.screenXScaleStretch;
		cls.screenYScale = cls.screenYScaleStretch;
		// narrow screen
		cls.screenYBias = 0.5 * ( cls.glconfig.vidHeight - ( cls.glconfig.vidWidth * (480/(float)640) ) );
		cls.screenYScale = cls.screenXScale;
		// no wide screen
		cls.screenXBias = 0;
	}
}
#endif

/*
============
CL_InitRenderer
============
*/
void CL_InitRenderer( void ) {
	// this sets up the renderer and calls R_Init
	re.BeginRegistration( &cls.glconfig );

	cls.whiteShader = re.RegisterShader( "white" );

#ifdef USE_FLEXIBLE_DISPLAY
	// update latched value at vid_restart
	cl_flexibleDisplay = Cvar_Get ("cl_flexibleDisplay", "1", CVAR_ARCHIVE | CVAR_LATCH);

	CL_WindowResized();
#endif

	// draw loading screen when the game is starting up
	if (!cls.drawnLoadingScreen) {
		CL_DrawLoadingScreen();
		cls.drawnLoadingScreen = qtrue;
	}
}

/*
============
CL_GlconfigChanged
============
*/
void CL_GlconfigChanged( const glconfig_t *glconfig ) {
	cls.glconfig = *glconfig;
	CL_UpdateGlconfig();

#ifdef USE_FLEXIBLE_DISPLAY
	CL_WindowResized();
#endif
}

/*
============================
CL_StartHunkUsers

After the server has cleared the hunk, these will need to be restarted
This is the only place that any of these functions are called from
============================
*/
void CL_StartHunkUsers( qboolean rendererOnly ) {
	if (!com_cl_running) {
		return;
	}

	if ( !com_cl_running->integer ) {
		return;
	}

	if ( !cls.rendererStarted ) {
		cls.rendererStarted = qtrue;
		CL_InitRenderer();
	}

	if ( rendererOnly ) {
		return;
	}

	if ( !cls.soundStarted ) {
		cls.soundStarted = qtrue;
		S_Init();
	}

	if ( !cls.soundRegistered ) {
		cls.soundRegistered = qtrue;
		S_BeginRegistration();
	}

	if ( !cls.cgameStarted ) {
		cls.cgameStarted = qtrue;
		CL_InitCGame();
	}
}

/*
============
CL_MaxSplitView
============
*/
int CL_MaxSplitView(void) {
	return CL_MAX_SPLITVIEW;
}

int CL_Ref_CIN_PlayCinematic( const char *arg, int x, int y, int w, int h, int systemBits ) {
	return CIN_PlayCinematic( arg, x, y, w, h, systemBits, CIN_CLIENT );
}

/*
============
CL_InitRef
============
*/
void CL_InitRef( void ) {
	refimport_t ri;

	Com_Memset( &ri, 0, sizeof ( refimport_t ) );

	// cinematic stuff

	ri.CIN_UploadCinematic = CIN_UploadCinematic;
	ri.CIN_PlayCinematic = CL_Ref_CIN_PlayCinematic;
	ri.CIN_RunCinematic = CIN_RunCinematic;
  
	ri.CL_WriteAVIVideoFrame = CL_WriteAVIVideoFrame;
	ri.CL_MaxSplitView = CL_MaxSplitView;
	ri.CL_GetMapTitle = CL_GetMapTitle;
	ri.CL_GetLocalPlayerLocation = CL_GetLocalPlayerLocation;
	ri.CL_GlconfigChanged = CL_GlconfigChanged;
	ri.zlib_compress = compress;
	ri.zlib_crc32 = crc32;

	ri.IN_Init = IN_Init;
	ri.IN_Shutdown = IN_Shutdown;
	ri.IN_Restart = IN_Restart;

	ri.Sys_GLimpSafeInit = Sys_GLimpSafeInit;
	ri.Sys_GLimpInit = Sys_GLimpInit;

	Com_InitRef(&ri);

#if defined __USEA3D && defined __A3D_GEOM
	hA3Dg_ExportRenderGeom (&re);
#endif

	// unpause so the cgame definitely gets a snapshot and renders a frame
	Cvar_Set( "cl_paused", "0" );
}


//===========================================================================================


/*
===============
CL_Video_f

video
video [filename]
===============
*/
void CL_Video_f( void )
{
  char  filename[ MAX_OSPATH ];
  int   i, last;

  if( Cmd_Argc( ) == 2 )
  {
    // explicit filename
    Com_sprintf( filename, MAX_OSPATH, "videos/%s.avi", Cmd_Argv( 1 ) );
  }
  else
  {
    // scan for a free filename
    for( i = 0; i <= 9999; i++ )
    {
      int a, b, c, d;

      last = i;

      a = last / 1000;
      last -= a * 1000;
      b = last / 100;
      last -= b * 100;
      c = last / 10;
      last -= c * 10;
      d = last;

      Com_sprintf( filename, MAX_OSPATH, "videos/video%d%d%d%d.avi",
          a, b, c, d );

      if( !FS_FileExists( filename ) )
        break; // file doesn't exist
    }

    if( i > 9999 )
    {
      Com_Printf( S_COLOR_RED "ERROR: no free file names to create video\n" );
      return;
    }
  }

  CL_OpenAVIForWriting( filename );
}

/*
===============
CL_StopVideo_f
===============
*/
void CL_StopVideo_f( void )
{
  CL_CloseAVI( );
}

/*
===============
CL_GenerateQKey

test to see if a valid QKEY_FILE exists.  If one does not, try to generate
it by filling it with 2048 bytes of random data.
===============
*/
static void CL_GenerateQKey(void)
{
	int len = 0;
	unsigned char buff[ QKEY_SIZE ];
	fileHandle_t f;

	len = FS_SV_FOpenFileRead( QKEY_FILE, &f );
	FS_FCloseFile( f );
	if( len == QKEY_SIZE ) {
		Com_Printf( "QKEY found.\n" );
		return;
	}
	else {
		if( len > 0 ) {
			Com_Printf( "QKEY file size != %d, regenerating\n",
				QKEY_SIZE );
		}

		Com_Printf( "QKEY building random string\n" );
		Com_RandomBytes( buff, sizeof(buff) );

		f = FS_SV_FOpenFileWrite( QKEY_FILE );
		if( !f ) {
			Com_Printf( "QKEY could not open %s for write\n",
				QKEY_FILE );
			return;
		}
		FS_Write( buff, sizeof(buff), f );
		FS_FCloseFile( f );
		Com_Printf( "QKEY generated\n" );
	}
} 

#ifdef USE_FLEXIBLE_DISPLAY
/*
=================
CL_PrintViewMode
=================
*/
static void CL_PrintViewMode( void ) {
	switch ( cl_viewmode->integer ) {
		case 1:
			Com_Printf( "View-mode set to original 4:3 view/HUD\n" );
			break;
		case 2:
			Com_Printf( "View-mode set to expanded view and 4:3 HUD\n" );
			break;
		case 3:
			Com_Printf( "View-mode set to expanded view/HUD\n" );
			break;
		case 4:
			Com_Printf( "View-mode set to expanded view and stretched HUD\n" );
			break;
		case 5:
			// 640x480 video mode in fullscreen on a widescreen monitor is
			// usually stretched to fill the whole screen.
			Com_Printf( "View-mode set to original stretched view/HUD\n" );
			break;
		case 6:
			Com_Printf( "View-mode set to original widescreen view/HUD\n" );
			break;
		default:
			break;
	}
}

extern void CL_GameCommand( void );

/*
=================
CL_SizeDown_f
=================
*/
static void CL_SizeUp_f (void) {
	if ( cl_flexibleDisplay->integer ) {
		if ( cl_viewsize->integer >= 100 ) {
			if ( cl_viewmode->integer >= 6 ) {
				return;
			}

			Cvar_Set(cl_viewmode->name, va("%i",(int)(cl_viewmode->integer+1)));
			CL_PrintViewMode();
		} else {
			Cvar_Set("cg_viewsize", va("%i",(int)(cl_viewsize->integer+10)));
		}
	} else {
		// run command in cgame
		if ( com_cl_running && com_cl_running->integer ) {
			CL_GameCommand();
		}
	}
}

/*
=================
CL_SizeDown_f
=================
*/
static void CL_SizeDown_f (void) {
	if ( cl_flexibleDisplay->integer ) {
		if ( cl_viewmode->integer > 1 ) {
			Cvar_Set(cl_viewmode->name, va("%i",(int)(cl_viewmode->integer-1)));
			CL_PrintViewMode();
		} else {
			Cvar_Set("cg_viewsize", va("%i",(int)(cl_viewsize->integer-10)));
		}
	} else {
		// run command in cgame
		if ( com_cl_running && com_cl_running->integer ) {
			CL_GameCommand();
		}
	}
}
#endif

/*
====================
CL_Init
====================
*/
void CL_Init( void ) {
	Com_Printf( "----- Client Initialization -----\n" );

	Con_Init ();

	if(!com_fullyInitialized)
	{
		CL_ClearState();
		CL_InitConnection(qfalse);
		cl_oldGameSet = qfalse;
		cls.drawnLoadingScreen = qfalse;
	}

	cls.realtime = 0;

	CL_InitInput ();

	//
	// register our variables
	//
	cl_noprint = Cvar_Get( "cl_noprint", "0", 0 );
#ifdef UPDATE_SERVER_NAME
	cl_motd = Cvar_Get ("cl_motd", "1", 0);
#endif

	cl_timeout = Cvar_Get ("cl_timeout", "200", 0);

	cl_timeNudge = Cvar_Get ("cl_timeNudge", "0", CVAR_TEMP );
	Cvar_CheckRange( cl_timeNudge, -30, 30, qtrue );
	cl_shownet = Cvar_Get ("cl_shownet", "0", CVAR_TEMP );
	cl_showSend = Cvar_Get ("cl_showSend", "0", CVAR_TEMP );
	cl_showTimeDelta = Cvar_Get ("cl_showTimeDelta", "0", CVAR_TEMP );
	cl_freezeDemo = Cvar_Get ("cl_freezeDemo", "0", CVAR_TEMP );
	rcon_client_password = Cvar_Get ("rconPassword", "", CVAR_TEMP );
	cl_activeAction = Cvar_Get( "activeAction", "", CVAR_TEMP );

	cl_timedemo = Cvar_Get ("timedemo", "0", 0);
	cl_timedemoLog = Cvar_Get ("cl_timedemoLog", "", CVAR_ARCHIVE);
	cl_autoRecordDemo = Cvar_Get ("cl_autoRecordDemo", "0", CVAR_ARCHIVE);
	cl_aviFrameRate = Cvar_Get ("cl_aviFrameRate", "25", CVAR_ARCHIVE);
	cl_aviMotionJpeg = Cvar_Get ("cl_aviMotionJpeg", "1", CVAR_ARCHIVE);
	cl_forceavidemo = Cvar_Get ("cl_forceavidemo", "0", 0);

	rconAddress = Cvar_Get ("rconAddress", "", 0);

	cl_maxpackets = Cvar_Get ("cl_maxpackets", "30", CVAR_ARCHIVE );
	Cvar_CheckRange( cl_maxpackets, 15, 125, qtrue );
	cl_packetdup = Cvar_Get ("cl_packetdup", "1", CVAR_ARCHIVE );

	cl_sensitivity = Cvar_Get ("sensitivity", "5", CVAR_ARCHIVE);
	cl_mouseAccel = Cvar_Get ("cl_mouseAccel", "0", CVAR_ARCHIVE);

	// 0: legacy mouse acceleration
	// 1: new implementation
	cl_mouseAccelStyle = Cvar_Get( "cl_mouseAccelStyle", "0", CVAR_ARCHIVE );
	// offset for the power function (for style 1, ignored otherwise)
	// this should be set to the max rate value
	cl_mouseAccelOffset = Cvar_Get( "cl_mouseAccelOffset", "5", CVAR_ARCHIVE );
	Cvar_CheckRange(cl_mouseAccelOffset, 0.001f, 50000.0f, qfalse);

	cl_showMouseRate = Cvar_Get ("cl_showmouserate", "0", 0);

	cl_allowDownload = Cvar_Get ("cl_allowDownload", "0", CVAR_ARCHIVE);
#ifdef USE_CURL_DLOPEN
	cl_cURLLib = Cvar_Get("cl_cURLLib", DEFAULT_CURL_LIB, CVAR_ARCHIVE | CVAR_PROTECTED);
#endif

#ifdef __APPLE__
	// In game video is REALLY slow in Mac OS X right now due to driver slowness
	cl_inGameVideo = Cvar_Get ("r_inGameVideo", "0", CVAR_ARCHIVE);
#else
	cl_inGameVideo = Cvar_Get ("r_inGameVideo", "1", CVAR_ARCHIVE);
#endif

	cl_serverStatusResendTime = Cvar_Get ("cl_serverStatusResendTime", "750", 0);

#ifdef __APPLE__
	// Input is jittery on OS X w/o this
	m_filter = Cvar_Get ("m_filter", "1", CVAR_ARCHIVE);
#else
	m_filter = Cvar_Get ("m_filter", "0", CVAR_ARCHIVE);
#endif

	cl_motdString = Cvar_Get( "cl_motdString", "", CVAR_ROM );

	Cvar_Get( "cl_maxPing", "800", CVAR_ARCHIVE );

	cl_lanForcePackets = Cvar_Get ("cl_lanForcePackets", "1", CVAR_ARCHIVE);

	cl_guidServerUniq = Cvar_Get ("cl_guidServerUniq", "1", CVAR_ARCHIVE);

	// ~ and `, as keys and characters
	cl_consoleKeys = Cvar_Get( "cl_consoleKeys", "~ ` 0x7e 0x60", CVAR_ARCHIVE);

	cl_loadingScreenIndex = Cvar_Get( "cl_loadingScreenIndex", "0", CVAR_ARCHIVE | CVAR_NORESTART );

	// select which local players (using bits) should join a server on connect
	Cvar_Get ("cl_localPlayers", "1", 0 );

	// userinfo
	cl_rate = Cvar_Get ("rate", "25000", CVAR_USERINFO_ALL | CVAR_ARCHIVE );
	Cvar_Get ("snaps", "", CVAR_USERINFO_ALL | CVAR_ARCHIVE );
	Cvar_Get ("cl_anonymous", "0", CVAR_USERINFO_ALL | CVAR_ARCHIVE );
	Cvar_Get ("password", "", CVAR_USERINFO_ALL);

#ifdef USE_MUMBLE
	cl_useMumble = Cvar_Get ("cl_useMumble", "0", CVAR_ARCHIVE | CVAR_LATCH);
	cl_mumbleScale = Cvar_Get ("cl_mumbleScale", "0.0254", CVAR_ARCHIVE);
#endif

#ifdef USE_VOIP
	cl_voipSend = Cvar_Get ("cl_voipSend", "0", 0);
	cl_voipSendTarget = Cvar_Get ("cl_voipSendTarget", "spatial", 0);
	cl_voipGainDuringCapture = Cvar_Get ("cl_voipGainDuringCapture", "0.2", CVAR_ARCHIVE);
	cl_voipCaptureMult = Cvar_Get ("cl_voipCaptureMult", "2.0", CVAR_ARCHIVE);
	cl_voipUseVAD = Cvar_Get ("cl_voipUseVAD", "0", CVAR_ARCHIVE);
	cl_voipVADThreshold = Cvar_Get ("cl_voipVADThreshold", "0.25", CVAR_ARCHIVE);

	cl_voip = Cvar_Get ("cl_voip", "0", CVAR_USERINFO_ALL | CVAR_ARCHIVE);
	Cvar_CheckRange( cl_voip, 0, 1, qtrue );
	cl_voipProtocol = Cvar_Get ("cl_voipProtocol", cl_voip->integer ? "opus" : "", CVAR_USERINFO | CVAR_ROM);
#endif

#ifdef USE_FLEXIBLE_DISPLAY
	cl_viewsize = Cvar_Get ("cg_viewsize", "100", CVAR_ARCHIVE );

	cl_flexibleDisplay = Cvar_Get ("cl_flexibleDisplay", "1", CVAR_ARCHIVE | CVAR_LATCH);
	cl_viewmode = cl_flexibleDisplay;
	Cvar_CheckRange( cl_flexibleDisplay, 0, 6, qtrue );
#endif

	//
	// register our commands
	//
	Cmd_AddCommand ("cmd", CL_ForwardToServer_f);
#if CL_MAX_SPLITVIEW > 1
	Cmd_AddCommand ("dropin", CL_DropIn_f);
	Cmd_AddCommand ("dropout", CL_DropOut_f);
	Cmd_AddCommand ("2dropin", CL_2DropIn_f);
	Cmd_AddCommand ("2dropout", CL_2DropOut_f);
#endif
#if CL_MAX_SPLITVIEW > 2
	Cmd_AddCommand ("3dropin", CL_3DropIn_f);
	Cmd_AddCommand ("3dropout", CL_3DropOut_f);
#endif
#if CL_MAX_SPLITVIEW > 3
	Cmd_AddCommand ("4dropin", CL_4DropIn_f);
	Cmd_AddCommand ("4dropout", CL_4DropOut_f);
#endif
	Cmd_AddCommand ("configstrings", CL_Configstrings_f);
	Cmd_AddCommand ("clientinfo", CL_Clientinfo_f);
	Cmd_AddCommand ("snd_restart", CL_Snd_Restart_f);
	Cmd_AddCommand ("vid_restart", CL_Vid_Restart_f);
	Cmd_AddCommand ("disconnect", CL_Disconnect_f);
	Cmd_AddCommand ("record", CL_Record_f);
	Cmd_AddCommand ("demo", CL_PlayDemo_f);
	Cmd_SetCommandCompletionFunc( "demo", CL_CompleteDemoName );
	Cmd_AddCommand ("stoprecord", CL_StopRecord_f);
	Cmd_AddCommand ("connect", CL_Connect_f);
	Cmd_AddCommand ("reconnect", CL_Reconnect_f);
	Cmd_AddCommand ("localservers", CL_LocalServers_f);
	Cmd_AddCommand ("globalservers", CL_GlobalServers_f);
	Cmd_AddCommand ("rcon", CL_Rcon_f);
	Cmd_SetCommandCompletionFunc( "rcon", CL_CompleteRcon );
	Cmd_AddCommand ("ping", CL_Ping_f );
	Cmd_AddCommand ("serverstatus", CL_ServerStatus_f );
	Cmd_AddCommand ("showip", CL_ShowIP_f );
	Cmd_AddCommand ("fs_openedList", CL_OpenedPK3List_f );
	Cmd_AddCommand ("fs_referencedList", CL_ReferencedPK3List_f );
	Cmd_AddCommand ("video", CL_Video_f );
	Cmd_AddCommand ("stopvideo", CL_StopVideo_f );
#ifdef USE_FLEXIBLE_DISPLAY
	Cmd_AddCommand ("sizeup", CL_SizeUp_f );
	Cmd_AddCommand ("sizedown", CL_SizeDown_f );
#endif

	CL_InitRef();

	SCR_Init ();

//	Cbuf_Execute ();

	Cvar_Set( "cl_running", "1" );

	CL_GenerateQKey();
	Cvar_Get( "cl_guid", "", CVAR_USERINFO | CVAR_ROM );
	CL_UpdateGUID( NULL, 0 );

	Com_Printf( "----- Client Initialization Complete -----\n" );
}


/*
===============
CL_Shutdown

===============
*/
void CL_Shutdown(char *finalmsg, qboolean disconnect, qboolean quit)
{
	static qboolean recursive = qfalse;
#if CL_MAX_SPLITVIEW > 1
	int i;
#endif
	
	// check whether the client is running at all.
	if(!(com_cl_running && com_cl_running->integer))
		return;
	
	Com_Printf( "----- Client Shutdown (%s) -----\n", finalmsg );

	if ( recursive ) {
		Com_Printf( "WARNING: Recursive shutdown\n" );
		return;
	}
	recursive = qtrue;

	noGameRestart = quit;

	if(disconnect)
		CL_Disconnect(qtrue);
	
	CL_ClearMemory(qtrue);
	CL_Snd_Shutdown();

	Cmd_RemoveCommand ("cmd");
#if CL_MAX_SPLITVIEW > 1
	for (i = 0; i < CL_MAX_SPLITVIEW; i++) {
		Cmd_RemoveCommand (Com_LocalPlayerCvarName(i, "dropout"));
		Cmd_RemoveCommand (Com_LocalPlayerCvarName(i, "dropin"));
	}
#endif
	Cmd_RemoveCommand ("configstrings");
	Cmd_RemoveCommand ("clientinfo");
	Cmd_RemoveCommand ("snd_restart");
	Cmd_RemoveCommand ("vid_restart");
	Cmd_RemoveCommand ("disconnect");
	Cmd_RemoveCommand ("record");
	Cmd_RemoveCommand ("demo");
	Cmd_RemoveCommand ("cinematic");
	Cmd_RemoveCommand ("stoprecord");
	Cmd_RemoveCommand ("connect");
	Cmd_RemoveCommand ("reconnect");
	Cmd_RemoveCommand ("localservers");
	Cmd_RemoveCommand ("globalservers");
	Cmd_RemoveCommand ("rcon");
	Cmd_RemoveCommand ("ping");
	Cmd_RemoveCommand ("serverstatus");
	Cmd_RemoveCommand ("showip");
	Cmd_RemoveCommand ("fs_openedList");
	Cmd_RemoveCommand ("fs_referencedList");
	Cmd_RemoveCommand ("video");
	Cmd_RemoveCommand ("stopvideo");

	CL_ShutdownInput();
	Con_Shutdown();

	Cvar_Set( "cl_running", "0" );

	recursive = qfalse;

	Com_Memset( &cls, 0, sizeof( cls ) );

	Com_Printf( "-----------------------\n" );

}

qboolean CL_ConnectedToRemoteServer( void ) {
	return ( com_sv_running && !com_sv_running->integer && clc.state >= CA_CONNECTED && !clc.demoplaying );
}

static void CL_SetServerInfo(serverInfo_t *server, const char *info, int ping) {
	if (server) {
		if (info) {
			server->clients = atoi(Info_ValueForKey(info, "clients"));
			Q_strncpyz(server->hostName,Info_ValueForKey(info, "hostname"), sizeof ( server->hostName ) );
			Q_strncpyz(server->mapName, Info_ValueForKey(info, "mapname"), sizeof ( server->mapName ) );
			server->maxClients = atoi(Info_ValueForKey(info, "sv_maxclients"));
			Q_strncpyz(server->game,Info_ValueForKey(info, "game"), sizeof ( server->game ) );
			Q_strncpyz(server->gameType, Info_ValueForKey(info, "gametype"), sizeof (server->gameType ) );
			server->netType = atoi(Info_ValueForKey(info, "nettype"));
			server->minPing = atoi(Info_ValueForKey(info, "minping"));
			server->maxPing = atoi(Info_ValueForKey(info, "maxping"));
			server->g_humanplayers = atoi(Info_ValueForKey(info, "g_humanplayers"));
			server->g_needpass = atoi(Info_ValueForKey(info, "g_needpass"));
		}
		server->ping = ping;
	}
}

static void CL_SetServerInfoByAddress(netadr_t from, const char *info, int ping) {
	int i;

	for (i = 0; i < MAX_OTHER_SERVERS; i++) {
		if (NET_CompareAdr(from, cls.localServers[i].adr)) {
			CL_SetServerInfo(&cls.localServers[i], info, ping);
		}
	}

	for (i = 0; i < MAX_GLOBAL_SERVERS; i++) {
		if (NET_CompareAdr(from, cls.globalServers[i].adr)) {
			CL_SetServerInfo(&cls.globalServers[i], info, ping);
		}
	}

	for (i = 0; i < MAX_OTHER_SERVERS; i++) {
		if (NET_CompareAdr(from, cls.favoriteServers[i].adr)) {
			CL_SetServerInfo(&cls.favoriteServers[i], info, ping);
		}
	}

}

/*
===================
CL_ServerInfoPacket
===================
*/
void CL_ServerInfoPacket( netadr_t from, msg_t *msg ) {
	int		i, type;
	char	info[MAX_INFO_STRING];
	char	*infoString;
	int		prot;
	char	*gamename;
	qboolean gameMismatch;

	infoString = MSG_ReadString( msg );

	// if this isn't the correct gamename, ignore it
	gamename = Info_ValueForKey( infoString, "gamename" );
	gameMismatch = !*gamename || strcmp(gamename, com_gamename->string) != 0;

	if (gameMismatch)
	{
		Com_DPrintf( "Game mismatch in info packet: %s\n", infoString );
		return;
	}

	// if this isn't the correct protocol version, ignore it
	prot = atoi( Info_ValueForKey( infoString, "protocol" ) );

	if(prot != com_protocol->integer
#ifdef LEGACY_PROTOCOL
	   && prot != com_legacyprotocol->integer
#endif
	  )
	{
		Com_DPrintf( "Different protocol info packet: %s\n", infoString );
		return;
	}

	// iterate servers waiting for ping response
	for (i=0; i<MAX_PINGREQUESTS; i++)
	{
		if ( cl_pinglist[i].adr.port && !cl_pinglist[i].time && NET_CompareAdr( from, cl_pinglist[i].adr ) )
		{
			// calc ping time
			cl_pinglist[i].time = Sys_Milliseconds() - cl_pinglist[i].start;
			Com_DPrintf( "ping time %dms from %s\n", cl_pinglist[i].time, NET_AdrToString( from ) );

			// save of info
			Q_strncpyz( cl_pinglist[i].info, infoString, sizeof( cl_pinglist[i].info ) );

			// tack on the net type
			// NOTE: make sure these types are in sync with the netnames strings in the UI
			switch (from.type)
			{
				case NA_BROADCAST:
				case NA_IP:
					type = 1;
					break;
				case NA_IP6:
					type = 2;
					break;
				default:
					type = 0;
					break;
			}
			Info_SetValueForKey( cl_pinglist[i].info, "nettype", va("%d", type) );
			CL_SetServerInfoByAddress(from, infoString, cl_pinglist[i].time);

			return;
		}
	}

	// if not just sent a local broadcast or pinging local servers
	if (cls.pingUpdateSource != AS_LOCAL) {
		return;
	}

	for ( i = 0 ; i < MAX_OTHER_SERVERS ; i++ ) {
		// empty slot
		if ( cls.localServers[i].adr.port == 0 ) {
			break;
		}

		// avoid duplicate
		if ( NET_CompareAdr( from, cls.localServers[i].adr ) ) {
			return;
		}
	}

	if ( i == MAX_OTHER_SERVERS ) {
		Com_DPrintf( "MAX_OTHER_SERVERS hit, dropping infoResponse\n" );
		return;
	}

	// add this to the list
	cls.numlocalservers = i+1;
	CL_InitServerInfo( &cls.localServers[i], &from );

	Q_strncpyz( info, MSG_ReadString( msg ), MAX_INFO_STRING );
	if (strlen(info)) {
		if (info[strlen(info)-1] != '\n') {
			Q_strcat(info, sizeof(info), "\n");
		}
		Com_Printf( "%s: %s", NET_AdrToStringwPort( from ), info );
	}
}

/*
===================
CL_GetServerStatus
===================
*/
serverStatus_t *CL_GetServerStatus( netadr_t from ) {
	int i, oldest, oldestTime;

	for (i = 0; i < MAX_SERVERSTATUSREQUESTS; i++) {
		if ( NET_CompareAdr( from, cl_serverStatusList[i].address ) ) {
			return &cl_serverStatusList[i];
		}
	}
	for (i = 0; i < MAX_SERVERSTATUSREQUESTS; i++) {
		if ( cl_serverStatusList[i].retrieved ) {
			return &cl_serverStatusList[i];
		}
	}
	oldest = -1;
	oldestTime = 0;
	for (i = 0; i < MAX_SERVERSTATUSREQUESTS; i++) {
		if (oldest == -1 || cl_serverStatusList[i].startTime < oldestTime) {
			oldest = i;
			oldestTime = cl_serverStatusList[i].startTime;
		}
	}
	return &cl_serverStatusList[oldest];
}

/*
===================
CL_ServerStatus
===================
*/
int CL_ServerStatus( char *serverAddress, char *serverStatusString, int maxLen ) {
	int i;
	netadr_t	to;
	serverStatus_t *serverStatus;

	// if no server address then reset all server status requests
	if ( !serverAddress ) {
		for (i = 0; i < MAX_SERVERSTATUSREQUESTS; i++) {
			cl_serverStatusList[i].address.port = 0;
			cl_serverStatusList[i].retrieved = qtrue;
		}
		return qfalse;
	}
	// get the address
	if ( !NET_StringToAdr( serverAddress, &to, NA_UNSPEC) ) {
		return qfalse;
	}
	serverStatus = CL_GetServerStatus( to );
	// if no server status string then reset the server status request for this address
	if ( !serverStatusString ) {
		serverStatus->retrieved = qtrue;
		return qfalse;
	}

	// if this server status request has the same address
	if ( NET_CompareAdr( to, serverStatus->address) ) {
		// if we received a response for this server status request
		if (!serverStatus->pending) {
			Q_strncpyz(serverStatusString, serverStatus->string, maxLen);
			serverStatus->retrieved = qtrue;
			serverStatus->startTime = 0;
			return qtrue;
		}
		// resend the request regularly
		else if ( serverStatus->startTime < Com_Milliseconds() - cl_serverStatusResendTime->integer ) {
			serverStatus->print = qfalse;
			serverStatus->pending = qtrue;
			serverStatus->retrieved = qfalse;
			serverStatus->time = 0;
			serverStatus->startTime = Com_Milliseconds();
			NET_OutOfBandPrint( NS_CLIENT, to, "getstatus" );
			return qfalse;
		}
	}
	// if retrieved
	else if ( serverStatus->retrieved ) {
		serverStatus->address = to;
		serverStatus->print = qfalse;
		serverStatus->pending = qtrue;
		serverStatus->retrieved = qfalse;
		serverStatus->startTime = Com_Milliseconds();
		serverStatus->time = 0;
		NET_OutOfBandPrint( NS_CLIENT, to, "getstatus" );
		return qfalse;
	}
	return qfalse;
}

/*
===================
CL_ServerStatusResponse
===================
*/
void CL_ServerStatusResponse( netadr_t from, msg_t *msg ) {
	char	*s;
	char	info[MAX_INFO_STRING];
	int		i, l, score, ping;
	int		len;
	serverStatus_t *serverStatus;

	serverStatus = NULL;
	for (i = 0; i < MAX_SERVERSTATUSREQUESTS; i++) {
		if ( NET_CompareAdr( from, cl_serverStatusList[i].address ) ) {
			serverStatus = &cl_serverStatusList[i];
			break;
		}
	}
	// if we didn't request this server status
	if (!serverStatus) {
		return;
	}

	s = MSG_ReadStringLine( msg );

	len = 0;
	Com_sprintf(&serverStatus->string[len], sizeof(serverStatus->string)-len, "%s", s);

	if (serverStatus->print) {
		Com_Printf("Server settings:\n");
		// print cvars
		while (*s) {
			for (i = 0; i < 2 && *s; i++) {
				if (*s == '\\')
					s++;
				l = 0;
				while (*s) {
					info[l++] = *s;
					if (l >= MAX_INFO_STRING-1)
						break;
					s++;
					if (*s == '\\') {
						break;
					}
				}
				info[l] = '\0';
				if (i) {
					Com_Printf("%s\n", info);
				}
				else {
					Com_Printf("%-24s", info);
				}
			}
		}
	}

	len = strlen(serverStatus->string);
	Com_sprintf(&serverStatus->string[len], sizeof(serverStatus->string)-len, "\\");

	if (serverStatus->print) {
		Com_Printf("\nPlayers:\n");
		Com_Printf("num: score: ping: name:\n");
	}
	for (i = 0, s = MSG_ReadStringLine( msg ); *s; s = MSG_ReadStringLine( msg ), i++) {

		len = strlen(serverStatus->string);
		Com_sprintf(&serverStatus->string[len], sizeof(serverStatus->string)-len, "\\%s", s);

		if (serverStatus->print) {
			score = ping = 0;
			sscanf(s, "%d %d", &score, &ping);
			s = strchr(s, ' ');
			if (s)
				s = strchr(s+1, ' ');
			if (s)
				s++;
			else
				s = "unknown";
			Com_Printf("%-2d   %-3d    %-3d   %s\n", i, score, ping, s );
		}
	}
	len = strlen(serverStatus->string);
	Com_sprintf(&serverStatus->string[len], sizeof(serverStatus->string)-len, "\\");

	serverStatus->time = Com_Milliseconds();
	serverStatus->address = from;
	serverStatus->pending = qfalse;
	if (serverStatus->print) {
		serverStatus->retrieved = qtrue;
	}
}

/*
==================
CL_LocalServers_f
==================
*/
void CL_LocalServers_f( void ) {
	char		*message;
	int			i, j;
	netadr_t	to;

	Com_Printf( "Scanning for servers on the local network...\n");

	// reset the list, waiting for response
	cls.numlocalservers = 0;
	cls.pingUpdateSource = AS_LOCAL;

	for (i = 0; i < MAX_OTHER_SERVERS; i++) {
		qboolean b = cls.localServers[i].visible;
		Com_Memset(&cls.localServers[i], 0, sizeof(cls.localServers[i]));
		cls.localServers[i].visible = b;
	}
	Com_Memset( &to, 0, sizeof( to ) );

	// The 'xxx' in the message is a challenge that will be echoed back
	// by the server.  We don't care about that here, but master servers
	// can use that to prevent spoofed server responses from invalid ip
	message = "\377\377\377\377getinfo xxx";

	// send each message twice in case one is dropped
	for ( i = 0 ; i < 2 ; i++ ) {
		// send a broadcast packet on each server port
		// we support multiple server ports so a single machine
		// can nicely run multiple servers
		for ( j = 0 ; j < NUM_SERVER_PORTS ; j++ ) {
			to.port = BigShort( (short)(PORT_SERVER + j) );

			to.type = NA_BROADCAST;
			NET_SendPacket( NS_CLIENT, strlen( message ), message, to );
			to.type = NA_MULTICAST6;
			NET_SendPacket( NS_CLIENT, strlen( message ), message, to );
		}
	}
}

/*
==================
CL_GlobalServers_f

Originally master 0 was Internet and master 1 was MPlayer.
ioquake3 2008; added support for requesting five separate master servers using 0-4.
ioquake3 2017; made master 0 fetch all master servers and 1-5 request a single master server.
==================
*/
void CL_GlobalServers_f( void ) {
	netadr_t	to;
	int			count, i, masterNum;
	char		command[1024], *masteraddress;
	
	if ((count = Cmd_Argc()) < 3 || (masterNum = atoi(Cmd_Argv(1))) < 0 || masterNum > MAX_MASTER_SERVERS)
	{
		Com_Printf("usage: globalservers <master# 0-%d> <protocol> [keywords]\n", MAX_MASTER_SERVERS);
		return;	
	}

	// request from all master servers
	if ( masterNum == 0 ) {
		int numAddress = 0;

		for ( i = 1; i <= MAX_MASTER_SERVERS; i++ ) {
			sprintf(command, "sv_master%d", i);
			masteraddress = Cvar_VariableString(command);

			if(!*masteraddress)
				continue;

			numAddress++;

			Com_sprintf(command, sizeof(command), "globalservers %d %s %s\n", i, Cmd_Argv(2), Cmd_ArgsFrom(3));
			Cbuf_AddText(command);
		}

		if ( !numAddress ) {
			Com_Printf( "CL_GlobalServers_f: Error: No master server addresses.\n");
		}
		return;
	}

	sprintf(command, "sv_master%d", masterNum);
	masteraddress = Cvar_VariableString(command);
	
	if(!*masteraddress)
	{
		Com_Printf( "CL_GlobalServers_f: Error: No master server address given.\n");
		return;	
	}

	// reset the list, waiting for response
	// -1 is used to distinguish a "no response"

	i = NET_StringToAdr(masteraddress, &to, NA_UNSPEC);
	
	if(!i)
	{
		Com_Printf( "CL_GlobalServers_f: Error: could not resolve address of master %s\n", masteraddress);
		return;	
	}
	else if(i == 2)
		to.port = BigShort(PORT_MASTER);

	Com_Printf("Requesting servers from %s (%s)...\n", masteraddress, NET_AdrToStringwPort(to));

	cls.numglobalservers = -1;
	cls.pingUpdateSource = AS_GLOBAL;

	// Use the extended query for IPv6 masters
	if (to.type == NA_IP6 || to.type == NA_MULTICAST6)
	{
		int v4enabled = Cvar_VariableIntegerValue("net_enabled") & NET_ENABLEV4;
		
		if(v4enabled)
		{
			Com_sprintf(command, sizeof(command), "getserversExt %s %s",
				com_gamename->string, Cmd_Argv(2));
		}
		else
		{
			Com_sprintf(command, sizeof(command), "getserversExt %s %s ipv6",
				com_gamename->string, Cmd_Argv(2));
		}
	}
	else
		Com_sprintf(command, sizeof(command), "getservers %s %s",
			com_gamename->string, Cmd_Argv(2));

	for (i=3; i < count; i++)
	{
		Q_strcat(command, sizeof(command), " ");
		Q_strcat(command, sizeof(command), Cmd_Argv(i));
	}

	NET_OutOfBandPrint( NS_SERVER, to, "%s", command );
}


/*
==================
CL_GetPing
==================
*/
void CL_GetPing( int n, char *buf, int buflen, int *pingtime )
{
	const char	*str;
	int		time;
	int		maxPing;

	if (n < 0 || n >= MAX_PINGREQUESTS || !cl_pinglist[n].adr.port)
	{
		// empty or invalid slot
		buf[0]    = '\0';
		*pingtime = 0;
		return;
	}

	str = NET_AdrToStringwPort( cl_pinglist[n].adr );
	Q_strncpyz( buf, str, buflen );

	time = cl_pinglist[n].time;
	if (!time)
	{
		// check for timeout
		time = Sys_Milliseconds() - cl_pinglist[n].start;
		maxPing = Cvar_VariableIntegerValue( "cl_maxPing" );
		if( maxPing < 100 ) {
			maxPing = 100;
		}
		if (time < maxPing)
		{
			// not timed out yet
			time = 0;
		}
	}

	CL_SetServerInfoByAddress(cl_pinglist[n].adr, cl_pinglist[n].info, cl_pinglist[n].time);

	*pingtime = time;
}

/*
==================
CL_GetPingInfo
==================
*/
void CL_GetPingInfo( int n, char *buf, int buflen )
{
	if (n < 0 || n >= MAX_PINGREQUESTS || !cl_pinglist[n].adr.port)
	{
		// empty or invalid slot
		if (buflen)
			buf[0] = '\0';
		return;
	}

	Q_strncpyz( buf, cl_pinglist[n].info, buflen );
}

/*
==================
CL_ClearPing
==================
*/
void CL_ClearPing( int n )
{
	if (n < 0 || n >= MAX_PINGREQUESTS)
		return;

	cl_pinglist[n].adr.port = 0;
}

/*
==================
CL_GetPingQueueCount
==================
*/
int CL_GetPingQueueCount( void )
{
	int		i;
	int		count;
	ping_t*	pingptr;

	count   = 0;
	pingptr = cl_pinglist;

	for (i=0; i<MAX_PINGREQUESTS; i++, pingptr++ ) {
		if (pingptr->adr.port) {
			count++;
		}
	}

	return (count);
}

/*
==================
CL_GetFreePing
==================
*/
ping_t* CL_GetFreePing( void )
{
	ping_t*	pingptr;
	ping_t*	best;	
	int		oldest;
	int		i;
	int		time;

	pingptr = cl_pinglist;
	for (i=0; i<MAX_PINGREQUESTS; i++, pingptr++ )
	{
		// find free ping slot
		if (pingptr->adr.port)
		{
			if (!pingptr->time)
			{
				if (Sys_Milliseconds() - pingptr->start < 500)
				{
					// still waiting for response
					continue;
				}
			}
			else if (pingptr->time < 500)
			{
				// results have not been queried
				continue;
			}
		}

		// clear it
		pingptr->adr.port = 0;
		return (pingptr);
	}

	// use oldest entry
	pingptr = cl_pinglist;
	best    = cl_pinglist;
	oldest  = INT_MIN;
	for (i=0; i<MAX_PINGREQUESTS; i++, pingptr++ )
	{
		// scan for oldest
		time = Sys_Milliseconds() - pingptr->start;
		if (time > oldest)
		{
			oldest = time;
			best   = pingptr;
		}
	}

	return (best);
}

/*
==================
CL_Ping_f
==================
*/
void CL_Ping_f( void ) {
	netadr_t	to;
	ping_t*		pingptr;
	char*		server;
	int			argc;
	netadrtype_t	family = NA_UNSPEC;

	argc = Cmd_Argc();

	if ( argc != 2 && argc != 3 ) {
		Com_Printf( "usage: ping [-4|-6] server\n");
		return;	
	}
	
	if(argc == 2)
		server = Cmd_Argv(1);
	else
	{
		if(!strcmp(Cmd_Argv(1), "-4"))
			family = NA_IP;
		else if(!strcmp(Cmd_Argv(1), "-6"))
			family = NA_IP6;
		else
			Com_Printf( "warning: only -4 or -6 as address type understood.\n");
		
		server = Cmd_Argv(2);
	}

	Com_Memset( &to, 0, sizeof(netadr_t) );

	if ( !NET_StringToAdr( server, &to, family ) ) {
		return;
	}

	pingptr = CL_GetFreePing();

	memcpy( &pingptr->adr, &to, sizeof (netadr_t) );
	pingptr->start = Sys_Milliseconds();
	pingptr->time  = 0;

	CL_SetServerInfoByAddress(pingptr->adr, NULL, 0);
		
	NET_OutOfBandPrint( NS_CLIENT, to, "getinfo xxx" );
}

/*
==================
CL_UpdateVisiblePings_f
==================
*/
qboolean CL_UpdateVisiblePings_f(int source) {
	int			slots, i;
	char		buff[MAX_STRING_CHARS];
	int			pingTime;
	int			max;
	qboolean status = qfalse;

	if (source < 0 || source >= AS_NUM_SOURCES) {
		return qfalse;
	}

	cls.pingUpdateSource = source;

	slots = CL_GetPingQueueCount();
	if (slots < MAX_PINGREQUESTS) {
		serverInfo_t *server = NULL;

		switch (source) {
			case AS_LOCAL :
				server = &cls.localServers[0];
				max = cls.numlocalservers;
			break;
			case AS_GLOBAL :
				server = &cls.globalServers[0];
				max = cls.numglobalservers;
			break;
			case AS_FAVORITES :
				server = &cls.favoriteServers[0];
				max = cls.numfavoriteservers;
			break;
			default:
				return qfalse;
		}
		for (i = 0; i < max; i++) {
			if (server[i].visible) {
				if (server[i].ping == -1) {
					int j;

					if (slots >= MAX_PINGREQUESTS) {
						break;
					}
					for (j = 0; j < MAX_PINGREQUESTS; j++) {
						if (!cl_pinglist[j].adr.port) {
							continue;
						}
						if (NET_CompareAdr( cl_pinglist[j].adr, server[i].adr)) {
							// already on the list
							break;
						}
					}
					if (j >= MAX_PINGREQUESTS) {
						status = qtrue;
						for (j = 0; j < MAX_PINGREQUESTS; j++) {
							if (!cl_pinglist[j].adr.port) {
								break;
							}
						}
						memcpy(&cl_pinglist[j].adr, &server[i].adr, sizeof(netadr_t));
						cl_pinglist[j].start = Sys_Milliseconds();
						cl_pinglist[j].time = 0;
						NET_OutOfBandPrint( NS_CLIENT, cl_pinglist[j].adr, "getinfo xxx" );
						slots++;
					}
				}
				// if the server has a ping higher than cl_maxPing or
				// the ping packet got lost
				else if (server[i].ping == 0) {
					// if we are updating global servers
					if (source == AS_GLOBAL) {
						//
						if ( cls.numGlobalServerAddresses > 0 ) {
							// overwrite this server with one from the additional global servers
							cls.numGlobalServerAddresses--;
							CL_InitServerInfo(&server[i], &cls.globalServerAddresses[cls.numGlobalServerAddresses]);
							// NOTE: the server[i].visible flag stays untouched
						}
					}
				}
			}
		}
	} 

	if (slots) {
		status = qtrue;
	}
	for (i = 0; i < MAX_PINGREQUESTS; i++) {
		if (!cl_pinglist[i].adr.port) {
			continue;
		}
		CL_GetPing( i, buff, MAX_STRING_CHARS, &pingTime );
		if (pingTime != 0) {
			CL_ClearPing(i);
			status = qtrue;
		}
	}

	return status;
}

/*
==================
CL_ServerStatus_f
==================
*/
void CL_ServerStatus_f(void) {
	netadr_t	to, *toptr = NULL;
	char		*server;
	serverStatus_t *serverStatus;
	int			argc;
	netadrtype_t	family = NA_UNSPEC;

	argc = Cmd_Argc();

	if ( argc != 2 && argc != 3 )
	{
		if (clc.state != CA_ACTIVE || clc.demoplaying)
		{
			Com_Printf ("Not connected to a server.\n");
			Com_Printf( "usage: serverstatus [-4|-6] server\n");
			return;
		}

		toptr = &clc.serverAddress;
	}
	
	if(!toptr)
	{
		Com_Memset( &to, 0, sizeof(netadr_t) );
	
		if(argc == 2)
			server = Cmd_Argv(1);
		else
		{
			if(!strcmp(Cmd_Argv(1), "-4"))
				family = NA_IP;
			else if(!strcmp(Cmd_Argv(1), "-6"))
				family = NA_IP6;
			else
				Com_Printf( "warning: only -4 or -6 as address type understood.\n");
		
			server = Cmd_Argv(2);
		}

		toptr = &to;
		if ( !NET_StringToAdr( server, toptr, family ) )
			return;
	}

	NET_OutOfBandPrint( NS_CLIENT, *toptr, "getstatus" );

	serverStatus = CL_GetServerStatus( *toptr );
	serverStatus->address = *toptr;
	serverStatus->print = qtrue;
	serverStatus->pending = qtrue;
}

/*
==================
CL_ShowIP_f
==================
*/
void CL_ShowIP_f(void) {
	Sys_ShowIP();
}

/*
=================
CL_GetMapTitle
=================
*/
void CL_GetMapTitle( char *buf, int bufLength ) {
	if ( !clc.mapTitle[0] ) {
		Q_strncpyz( buf, "Unknown", bufLength );
		return;
	}

	Q_strncpyz( buf, clc.mapTitle, bufLength );
}

/*
=================
CL_GetLocalPlayerLocation
=================
*/
qboolean CL_GetLocalPlayerLocation(char *buf, int bufLength, int localPlayerNum) {
	sharedPlayerState_t *ps;

	if (!cl.snap.valid || cl.snap.playerNums[localPlayerNum] == -1) {
		Q_strncpyz(buf, "Unknown", bufLength);
		return qfalse;
	}

	ps = DA_ElementPointer( cl.snap.playerStates, cl.snap.localPlayerIndex[localPlayerNum] );
	Com_sprintf(buf, bufLength, "X:%d Y:%d Z:%d A:%d", (int)ps->origin[0],
			(int)ps->origin[1], (int)ps->origin[2],
			(int)(ps->viewangles[YAW]+360)%360);
	return qtrue;
}
