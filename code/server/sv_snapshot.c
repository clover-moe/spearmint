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

#include "server.h"

/*
=============
SV_SnapshotEntity

Get pointer to beginning of entity state.
=============
*/
sharedEntityState_t *SV_SnapshotEntity( int num ) {
	return DA_ElementPointer( svs.snapshotEntities, num % svs.numSnapshotEntities );
}

/*
=============
SV_SnapshotPlayer

Get pointer to beginning of player state.
=============
*/
sharedPlayerState_t *SV_SnapshotPlayer( clientSnapshot_t *snap, int num ) {
	return DA_ElementPointer( snap->playerStates, num );
}

/*
=============================================================================

Delta encode a client frame onto the network channel

A normal server packet will look like:

4	sequence number (high bit set if an oversize fragment)
<optional reliable commands>
1	svc_snapshot
4	last client reliable command
4	serverTime
1	lastframe for delta compression
1	snapFlags
1	areaBytes
<areabytes>
1   number of player states
8	localPlayerIndex and playerNum for each of MAX_SPLITVIEW
<playerstates>
<packetentities>

=============================================================================
*/

/*
=============
SV_EmitPacketEntities

Writes a delta update of an entityState_t list to the message.
=============
*/
static void SV_EmitPacketEntities( clientSnapshot_t *from, clientSnapshot_t *to, msg_t *msg ) {
	sharedEntityState_t	*oldent, *newent;
	int		oldindex, newindex;
	int		oldnum, newnum;
	int		from_num_entities;

	// generate the delta update
	if ( !from ) {
		from_num_entities = 0;
	} else {
		from_num_entities = from->num_entities;
	}

	newent = NULL;
	oldent = NULL;
	newindex = 0;
	oldindex = 0;
	while ( newindex < to->num_entities || oldindex < from_num_entities ) {
		if ( newindex >= to->num_entities ) {
			newnum = 9999;
		} else {
			newent = SV_SnapshotEntity( to->first_entity + newindex );
			newnum = newent->number;
		}

		if ( oldindex >= from_num_entities ) {
			oldnum = 9999;
		} else {
			oldent = SV_SnapshotEntity( from->first_entity + oldindex );
			oldnum = oldent->number;
		}

		if ( newnum == oldnum ) {
			// delta update from old position
			// because the force parm is qfalse, this will not result
			// in any bytes being emitted if the entity has not changed at all
			MSG_WriteDeltaEntity (msg, oldent, newent, qfalse );
			oldindex++;
			newindex++;
			continue;
		}

		if ( newnum < oldnum ) {
			// this is a new entity, send it from the baseline
			MSG_WriteDeltaEntity (msg, DA_ElementPointer( sv.svEntitiesBaseline, newnum ), newent, qtrue );
			newindex++;
			continue;
		}

		if ( newnum > oldnum ) {
			// the old entity isn't present in the new message
			MSG_WriteDeltaEntity (msg, oldent, NULL, qtrue );
			oldindex++;
			continue;
		}
	}

	MSG_WriteBits( msg, (MAX_GENTITIES-1), GENTITYNUM_BITS );	// end of packetentities
}



/*
==================
SV_WriteSnapshotToClient
==================
*/
static void SV_WriteSnapshotToClient( client_t *client, msg_t *msg ) {
	clientSnapshot_t	*frame, *oldframe;
	int					lastframe;
	int					i;
	int					snapFlags;

	// this is the snapshot we are creating
	frame = &client->frames[ client->netchan.outgoingSequence & PACKET_MASK ];

	// snapshot wasn't ever built
	if ( !frame->playerStates.pointer ) {
		return;
	}

	// try to use a previous frame as the source for delta compressing the snapshot
	if ( client->deltaMessage <= 0 || client->state != CS_ACTIVE ) {
		// client is asking for a retransmit
		oldframe = NULL;
		lastframe = 0;
	} else if ( client->netchan.outgoingSequence - client->deltaMessage 
		>= (PACKET_BACKUP - 3) ) {
		// client hasn't gotten a good message through in a long time
		Com_DPrintf ("%s: Delta request from out of date packet.\n", SV_ClientName( client ));
		oldframe = NULL;
		lastframe = 0;
	} else {
		// we have a valid snapshot to delta from
		oldframe = &client->frames[ client->deltaMessage & PACKET_MASK ];
		lastframe = client->netchan.outgoingSequence - client->deltaMessage;

		// the snapshot's entities may still have rolled off the buffer, though
		if ( oldframe->first_entity <= svs.nextSnapshotEntities - svs.numSnapshotEntities ) {
			Com_DPrintf ("%s: Delta request from out of date entities.\n", SV_ClientName( client ));
			oldframe = NULL;
			lastframe = 0;
		}
	}

	MSG_WriteByte (msg, svc_snapshot);

	// NOTE, MRE: now sent at the start of every message from server to client
	// let the client know which reliable clientCommands we have received
	//MSG_WriteLong( msg, client->lastClientCommand );

	// send over the current server time so the client can drift
	// its view of time to try to match
	if( client->oldServerTime ) {
		// The server has not yet got an acknowledgement of the
		// new gamestate from this client, so continue to send it
		// a time as if the server has not restarted. Note from
		// the client's perspective this time is strictly speaking
		// incorrect, but since it'll be busy loading a map at
		// the time it doesn't really matter.
		MSG_WriteLong (msg, sv.time + client->oldServerTime);
	} else {
		MSG_WriteLong (msg, sv.time);
	}

	// what we are delta'ing from
	MSG_WriteByte (msg, lastframe);

	snapFlags = svs.snapFlagServerBit;
	if ( client->rateDelayed ) {
		snapFlags |= SNAPFLAG_RATE_DELAYED;
	}
	if ( client->state != CS_ACTIVE ) {
		snapFlags |= SNAPFLAG_NOT_ACTIVE;
	}

	MSG_WriteByte (msg, snapFlags);

	// send playerstates
	if (frame->numPSs > MAX_SPLITVIEW) {
		Com_DPrintf(S_COLOR_YELLOW "Warning: Almost sent numPSs as %d (max=%d)\n", frame->numPSs, MAX_SPLITVIEW);
		frame->numPSs = MAX_SPLITVIEW;
	}

	// send number of playerstates and local player indexes
	MSG_WriteByte (msg, frame->numPSs);
	for (i = 0; i < MAX_SPLITVIEW; i++) {
		MSG_WriteByte (msg, frame->localPlayerIndex[i]);
		MSG_WriteByte (msg, frame->playerNums[i]);

		// send over the areabits
		MSG_WriteByte (msg, frame->areabytes[i]);
		MSG_WriteData (msg, frame->areabits[i], frame->areabytes[i]);
	}

	for (i = 0; i < MAX_SPLITVIEW; i++) {
		if (frame->localPlayerIndex[i] == -1) {
			continue;
		}

		// delta encode the playerstate
		if ( oldframe && oldframe->localPlayerIndex[i] != -1) {
			MSG_WriteDeltaPlayerstate( msg, SV_SnapshotPlayer(oldframe, oldframe->localPlayerIndex[i]),
											SV_SnapshotPlayer(frame, frame->localPlayerIndex[i]) );
		} else {
			MSG_WriteDeltaPlayerstate( msg, NULL, SV_SnapshotPlayer(frame, frame->localPlayerIndex[i]) );
		}
	}

	// delta encode the entities
	SV_EmitPacketEntities (oldframe, frame, msg);

	// padding for rate debugging
	if ( sv_padPackets->integer ) {
		for ( i = 0 ; i < sv_padPackets->integer ; i++ ) {
			MSG_WriteByte (msg, svc_nop);
		}
	}
}


/*
==================
SV_UpdateServerCommandsToClient

(re)send all server commands the client hasn't acknowledged yet
==================
*/
void SV_UpdateServerCommandsToClient( client_t *client, msg_t *msg ) {
	int		i;

	// write any unacknowledged serverCommands
	for ( i = client->reliableAcknowledge + 1 ; i <= client->reliableSequence ; i++ ) {
		MSG_WriteByte( msg, svc_serverCommand );
		MSG_WriteLong( msg, i );
		MSG_WriteString( msg, client->reliableCommands[ i & (MAX_RELIABLE_COMMANDS-1) ] );
	}
	client->reliableSent = client->reliableSequence;
}

/*
==================
SV_WriteBaselineToClient
==================
*/
void SV_WriteBaselineToClient( client_t *client, msg_t *msg ) {
	sharedEntityState_t	*base;
	int					start;

	if ( !client->needBaseline ) {
		return;
	}

	client->needBaseline = qfalse;

	// write the baselines
	for ( start = 0 ; start < MAX_GENTITIES; start++ ) {
		base = (sharedEntityState_t *)DA_ElementPointer( sv.svEntitiesBaseline, start );
		if ( !base->number ) {
			continue;
		}
		MSG_WriteByte( msg, svc_baseline );
		MSG_WriteDeltaEntity( msg, NULL, base, qtrue );
	}
}

/*
=============================================================================

Build a client snapshot structure

=============================================================================
*/

typedef struct {
	int		numSnapshotEntities;
	int		maxSnapshotEntities;
	int		snapshotEntities[MAX_SNAPSHOT_ENTITIES * MAX_SPLITVIEW];	
} snapshotEntityNumbers_t;

/*
=======================
SV_QsortEntityNumbers
=======================
*/
static int QDECL SV_QsortEntityNumbers( const void *a, const void *b ) {
	int	*ea, *eb;

	ea = (int *)a;
	eb = (int *)b;

	if ( *ea == *eb ) {
		Com_Error( ERR_DROP, "SV_QsortEntityStates: duplicated entity" );
	}

	if ( *ea < *eb ) {
		return -1;
	}

	return 1;
}


/*
===============
SV_AddEntToSnapshot
===============
*/
static void SV_AddEntToSnapshot( clientSnapshot_t *frame, svEntity_t *svEnt, sharedEntity_t *gEnt, snapshotEntityNumbers_t *eNums ) {
	int i;

	// if we have already added this entity to this snapshot, don't add again
	if ( svEnt->snapshotCounter == sv.snapshotCounter ) {
		return;
	}
	svEnt->snapshotCounter = sv.snapshotCounter;

	// if we are full, silently discard entities
	if ( eNums->numSnapshotEntities == eNums->maxSnapshotEntities ) {
		return;
	}

	// check if game wants to send entity to one of these clients
	for (i = 0; i < frame->numPSs; i++) {
		if ( (qboolean)VM_Call( gvm, GAME_SNAPSHOT_CALLBACK, gEnt->s.number, SV_SnapshotPlayer( frame, i )->playerNum ) ) {
			break;
		}
	}

	if (i == frame->numPSs) {
		return;
	}

	eNums->snapshotEntities[ eNums->numSnapshotEntities ] = gEnt->s.number;
	eNums->numSnapshotEntities++;
}

/*
===============
SV_AddEntitiesVisibleFromPoint
===============
*/
static void SV_AddEntitiesVisibleFromPoint( int psIndex, int playerNum, vec3_t origin, clientSnapshot_t *frame, 
									snapshotEntityNumbers_t *eNums, qboolean portal ) {
	int		e, i;
	sharedEntity_t *ent;
	svEntity_t	*svEnt;
	int		l;
	int		clientarea, clientcluster;
	int		leafnum;
	byte	*clientpvs;
	byte	*bitvector;

	// during an error shutdown message we may need to transmit
	// the shutdown message after the server has shutdown, so
	// specfically check for it
	if ( !sv.state ) {
		return;
	}

	leafnum = CM_PointLeafnum (origin);
	clientarea = CM_LeafArea (leafnum);
	clientcluster = CM_LeafCluster (leafnum);

	// calculate the visible areas
	frame->areabytes[psIndex] = CM_WriteAreaBits( frame->areabits[psIndex], clientarea );

	clientpvs = CM_ClusterPVS (clientcluster);

	for ( e = 0 ; e < sv.num_entities ; e++ ) {
		ent = SV_GentityNum(e);

		// never send entities that aren't linked in
		if ( !ent->r.linked ) {
			continue;
		}

		if (ent->s.number != e) {
			Com_DPrintf ("FIXING ENT->S.NUMBER!!!\n");
			ent->s.number = e;
		}

		// entities can be flagged to explicitly not be sent to the client
		if ( ent->r.svFlags & SVF_NOCLIENT ) {
			continue;
		}

		// entities can be flagged to be sent to a given mask of clients
		if ( ent->r.svFlags & SVF_PLAYERMASK ) {
			if ( !Com_ClientListContains( &ent->r.sendPlayers, playerNum ) )
				continue;
		}

		svEnt = SV_SvEntityForGentity( ent );

		// don't double add an entity through portals
		if ( svEnt->snapshotCounter == sv.snapshotCounter ) {
			continue;
		}

		// limit based on distance
		if ( ent->r.cullDistance ) {
			vec3_t dir;
			VectorSubtract(ent->s.origin, origin, dir);
			if ( VectorLengthSquared(dir) > (float) ent->r.cullDistance * ent->r.cullDistance ) {
				continue;
			}
		}

		// broadcast entities are always sent
		if ( ent->r.svFlags & SVF_BROADCAST ) {
			SV_AddEntToSnapshot( frame, svEnt, ent, eNums );
			continue;
		}

		// ignore if not touching a PV leaf
		// check area
		if ( !CM_AreasConnected( clientarea, svEnt->areanum ) ) {
			// doors can legally straddle two areas, so
			// we may need to check another one
			if ( !CM_AreasConnected( clientarea, svEnt->areanum2 ) ) {
				continue;		// blocked by a door
			}
		}

		bitvector = clientpvs;

		// check individual leafs
		if ( !svEnt->numClusters ) {
			continue;
		}
		l = 0;
		for ( i=0 ; i < svEnt->numClusters ; i++ ) {
			l = svEnt->clusternums[i];
			if ( bitvector[l >> 3] & (1 << (l&7) ) ) {
				break;
			}
		}

		// if we haven't found it to be visible,
		// check overflow clusters that coudln't be stored
		if ( i == svEnt->numClusters ) {
			if ( svEnt->lastCluster ) {
				for ( ; l <= svEnt->lastCluster ; l++ ) {
					if ( bitvector[l >> 3] & (1 << (l&7) ) ) {
						break;
					}
				}
				if ( l == svEnt->lastCluster ) {
					continue;	// not visible
				}
			} else {
				continue;
			}
		}

		// visibility dummies
		if ( ent->r.svFlags & SVF_VISDUMMY ) {
			sharedEntity_t *ment = NULL;

			// find master
			ment = SV_GentityNum( ent->r.visDummyNum );

			if ( ment ) {
				svEntity_t *master = NULL;
				master = SV_SvEntityForGentity( ment );

				if ( master->snapshotCounter == sv.snapshotCounter || !ment->r.linked ) {
					continue;
				}

				SV_AddEntToSnapshot( frame, master, ment, eNums );
			}

			// master needs to be added, but not this dummy ent
			continue;
		} else if ( ent->r.svFlags & SVF_VISDUMMY_MULTIPLE ) {
			int h;
			sharedEntity_t *ment = NULL;
			svEntity_t *master = NULL;

			for ( h = 0; h < sv.num_entities; h++ ) {
				ment = SV_GentityNum( h );

				if ( ment == ent ) {
					continue;
				}

				if ( ment ) {
					master = SV_SvEntityForGentity( ment );
				} else {
					continue;
				}

				if ( !ment->r.linked ) {
					continue;
				}

				if ( ment->s.number != h ) {
					Com_DPrintf( "FIXING vis dummy multiple ment->S.NUMBER!!!\n" );
					ment->s.number = h;
				}

				if ( ment->r.svFlags & SVF_NOCLIENT ) {
					continue;
				}

				if ( master->snapshotCounter == sv.snapshotCounter ) {
					continue;
				}

				if ( ment->r.visDummyNum == ent->s.number ) {
					SV_AddEntToSnapshot( frame, master, ment, eNums );
				}
			}

			// masters need to be added, but not this dummy ent
			continue;
		}

		// add it
		SV_AddEntToSnapshot( frame, svEnt, ent, eNums );

		// if it's a portal entity, add everything visible from its camera position
		if ( ent->r.svFlags & SVF_PORTAL ) {
			if ( ent->r.portalCullDistance ) {
				vec3_t dir;
				VectorSubtract(ent->s.origin, origin, dir);
				if ( VectorLengthSquared(dir) > (float) ent->r.portalCullDistance * ent->r.portalCullDistance ) {
					continue;
				}
			}
			SV_AddEntitiesVisibleFromPoint( psIndex, playerNum, ent->s.origin2, frame, eNums, qtrue );
		}

	}
}

/*
=============
SV_BuildClientSnapshot

Decides which entities are going to be visible to the client, and
copies off the playerstate and areabits.

This properly handles multiple recursive portals, but the render
currently doesn't.
=============
*/
static void SV_BuildClientSnapshot( client_t *client ) {
	vec3_t						org;
	clientSnapshot_t			*frame, *oldframe;
	snapshotEntityNumbers_t		entityNumbers;
	int							i, j;
	int							psIndex;
	sharedEntityState_t			*state;
	svEntity_t					*svEnt;
	int							playerNum;
	sharedPlayerState_t			*ps;
	int							offset;

	// bump the counter used to prevent double adding
	sv.snapshotCounter++;

	// this is the frame we are creating
	frame = &client->frames[ client->netchan.outgoingSequence & PACKET_MASK ];

	// clear everything in this snapshot
	entityNumbers.numSnapshotEntities = 0;
	Com_Memset( frame->areabits, 0, sizeof( frame->areabits ) );

  // https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=62
	frame->num_entities = 0;
	
	if ( client->state == CS_ZOMBIE ) {
		return;
	}

	// allocate player states for frame if needed
	if ( !frame->playerStates.pointer ) {
		DA_Init( &frame->playerStates, MAX_SPLITVIEW, sv.gamePlayerStateSize, qtrue );
	} else {
		DA_Clear( &frame->playerStates );
	}

	// grab the current player states
	for (i = 0, frame->numPSs = 0; i < MAX_SPLITVIEW; i++) {
		if ( !client->localPlayers[i] || !client->localPlayers[i]->gentity ) {
			frame->localPlayerIndex[i] = -1;
			frame->playerNums[i] = -1;
			continue;
		}
		frame->playerNums[i] = client->localPlayers[i] - svs.players;
		ps = SV_GamePlayerNum( frame->playerNums[i] );
		DA_SetElement( &frame->playerStates, frame->numPSs, ps );
		frame->localPlayerIndex[i] = frame->numPSs;
		frame->numPSs++;
	}

	if ( !frame->numPSs ) {
		return;
	}

	// never send player's own entity, because it can
	// be regenerated from the playerstate
	for (i = 0; i < frame->numPSs; i++) {
		playerNum = SV_SnapshotPlayer(frame, i)->playerNum;
		if ( playerNum < 0 || playerNum >= MAX_GENTITIES ) {
			Com_Error( ERR_DROP, "SV_SvEntityForGentity: bad gEnt" );
		}
		svEnt = &sv.svEntities[ playerNum ];

		svEnt->snapshotCounter = sv.snapshotCounter;
	}

	// Now that local players have been marked as no send, add visible entities.
	for (i = 0; i < frame->numPSs; i++) {
		// find the player's viewpoint
		VectorCopy( SV_SnapshotPlayer(frame, i)->origin, org );
		org[2] += SV_SnapshotPlayer(frame, i)->viewheight;

		// allow MAX_SNAPSHOT_ENTITIES to be added for this view point
		entityNumbers.maxSnapshotEntities = entityNumbers.numSnapshotEntities + MAX_SNAPSHOT_ENTITIES;

		// add all the entities directly visible to the eye, which
		// may include portal entities that merge other viewpoints
		SV_AddEntitiesVisibleFromPoint( i, SV_SnapshotPlayer(frame, i)->playerNum, org, frame, &entityNumbers, qfalse );
	}

	// if there were portals visible, there may be out of order entities
	// in the list which will need to be resorted for the delta compression
	// to work correctly.  This also catches the error condition
	// of an entity being included twice.
	qsort( entityNumbers.snapshotEntities, entityNumbers.numSnapshotEntities, 
		sizeof( entityNumbers.snapshotEntities[0] ), SV_QsortEntityNumbers );

	// now that all viewpoint's areabits have been OR'd together, invert
	// all of them to make it a mask vector, which is what the renderer wants
	for ( psIndex = 0; psIndex < frame->numPSs; psIndex++ ) {
		for ( i = 0 ; i < MAX_MAP_AREA_BYTES/4 ; i++ ) {
			((int *)frame->areabits[psIndex])[i] = ((int *)frame->areabits[psIndex])[i] ^ -1;
		}
	}

	// avoid wrapping snapshot entities index to negative while keeping the same modulo value
	// and lower existing first_entity for detecting rolling off the buffer
	if ( svs.nextSnapshotEntities >= 0x7FFFFFFE - entityNumbers.numSnapshotEntities ) {
		Com_DPrintf( "Updating snapshot entities to avoid numSnapshotEntities wrapping.\n" );

		offset = ( ( svs.nextSnapshotEntities / svs.numSnapshotEntities ) - 2 ) * svs.numSnapshotEntities;

		for ( i = 0; i < sv_maxclients->integer; i++ ) {
			oldframe = svs.clients[i].frames;

			for ( j = 0; j < PACKET_BACKUP; j++, oldframe++ ) {
				if ( oldframe->first_entity <= svs.nextSnapshotEntities - svs.numSnapshotEntities ) {
					oldframe->first_entity %= svs.numSnapshotEntities;
				} else {
					oldframe->first_entity -= offset;
				}
			}
		}

		svs.nextSnapshotEntities -= offset;
	}

	// copy the entity states out
	frame->num_entities = 0;
	frame->first_entity = svs.nextSnapshotEntities;
	for ( i = 0 ; i < entityNumbers.numSnapshotEntities ; i++ ) {
		state = SV_GameEntityStateNum( entityNumbers.snapshotEntities[i] );
		DA_SetElement( &svs.snapshotEntities, svs.nextSnapshotEntities % svs.numSnapshotEntities, state );
		svs.nextSnapshotEntities++;
		// this should never hit
		if ( svs.nextSnapshotEntities >= 0x7FFFFFFE ) {
			Com_Error(ERR_FATAL, "svs.nextSnapshotEntities wrapped");
		}
		frame->num_entities++;
	}
}

#ifdef USE_VOIP
/*
==================
SV_WriteVoipToClient

Check to see if there is any VoIP queued for a client, and send if there is.
==================
*/
static void SV_WriteVoipToClient(client_t *cl, msg_t *msg)
{
	int totalbytes = 0;
	int i;
	voipServerPacket_t *packet;

	if(cl->queuedVoipPackets)
	{
		// Write as many VoIP packets as we reasonably can...
		for(i = 0; i < cl->queuedVoipPackets; i++)
		{
			packet = cl->voipPacket[(i + cl->queuedVoipIndex) % ARRAY_LEN(cl->voipPacket)];

			if(!*cl->downloadName)
			{
        			totalbytes += packet->len;
	        		if (totalbytes > (msg->maxsize - msg->cursize) / 2)
		        		break;

        			MSG_WriteByte(msg, svc_voipOpus);
        			MSG_WriteShort(msg, packet->sender);
	        		MSG_WriteByte(msg, (byte) packet->generation);
		        	MSG_WriteLong(msg, packet->sequence);
		        	MSG_WriteByte(msg, packet->frames);
        			MSG_WriteShort(msg, packet->len);
        			MSG_WriteBits(msg, packet->flags, VOIP_FLAGCNT);
	        		MSG_WriteData(msg, packet->data, packet->len);
                        }

			Z_Free(packet);
		}

		cl->queuedVoipPackets -= i;
		cl->queuedVoipIndex += i;
		cl->queuedVoipIndex %= ARRAY_LEN(cl->voipPacket);
	}
}
#endif

/*
=======================
SV_SendMessageToClient

Called by SV_SendClientSnapshot and SV_SendClientGameState
=======================
*/
void SV_SendMessageToClient(msg_t *msg, client_t *client)
{
	// record information about the message
	client->frames[client->netchan.outgoingSequence & PACKET_MASK].messageSize = msg->cursize;
	client->frames[client->netchan.outgoingSequence & PACKET_MASK].messageSent = svs.time;
	client->frames[client->netchan.outgoingSequence & PACKET_MASK].messageAcked = -1;

	// send the datagram
	SV_Netchan_Transmit(client, msg);
}


/*
=======================
SV_SendClientSnapshot

Also called by SV_FinalMessage

=======================
*/
void SV_SendClientSnapshot( client_t *client ) {
	byte		msg_buf[MAX_MSGLEN];
	msg_t		msg;

	// build the snapshot
	SV_BuildClientSnapshot( client );

	// bots need to have their snapshots build, but
	// the query them directly without needing to be sent
	if ( client->netchan.remoteAddress.type == NA_BOT ) {
		return;
	}

	MSG_Init (&msg, msg_buf, sizeof(msg_buf));
	msg.allowoverflow = qtrue;

	// NOTE, MRE: all server->client messages now acknowledge
	// let the client know which reliable clientCommands we have received
	MSG_WriteLong( &msg, client->lastClientCommand );

	// (re)send any reliable server commands
	SV_UpdateServerCommandsToClient( client, &msg );

	// if client is awaiting gamestate (or downloading a pk3), hold off sending snapshot as it
	// can't be loaded until after cgame is loaded.
	// must send snapshot to kicked (zombie) clients for them to process disconnect.
	if ( client->state != CS_ACTIVE && client->state != CS_ZOMBIE ) {
		client->needBaseline = qtrue;
	} else {
		// entities delta baseline
		SV_WriteBaselineToClient( client, &msg );

		// send over all the relevant entityState_t
		// and playerState_t
		SV_WriteSnapshotToClient( client, &msg );
	}

#ifdef USE_VOIP
	SV_WriteVoipToClient( client, &msg );
#endif

	// check for overflow
	if ( msg.overflowed ) {
		Com_Printf ("WARNING: msg overflowed for %s\n", SV_ClientName( client ));
		MSG_Clear (&msg);
	}

	SV_SendMessageToClient( &msg, client );
}


/*
=======================
SV_SendClientMessages
=======================
*/
void SV_SendClientMessages(void)
{
	int		i;
	client_t	*c;

	// send a message to each connected client
	for(i=0; i < sv_maxclients->integer; i++)
	{
		c = &svs.clients[i];
		
		if(!c->state)
			continue;		// not connected

		if(svs.time - c->lastSnapshotTime < c->snapshotMsec * com_timescale->value)
			continue;		// It's not time yet

		if(*c->downloadName)
			continue;		// Client is downloading, don't send snapshots

		if(c->netchan.unsentFragments || c->netchan_start_queue)
		{
			c->rateDelayed = qtrue;
			continue;		// Drop this snapshot if the packet queue is still full or delta compression will break
		}

		if(!(c->netchan.remoteAddress.type == NA_LOOPBACK ||
		     (sv_lanForceRate->integer && Sys_IsLANAddress(c->netchan.remoteAddress))))
		{
			// rate control for clients not on LAN 
			if(SV_RateMsec(c) > 0)
			{
				// Not enough time since last packet passed through the line
				c->rateDelayed = qtrue;
				continue;
			}
		}

		// generate and send a new message
		SV_SendClientSnapshot(c);
		c->lastSnapshotTime = svs.time;
		c->rateDelayed = qfalse;
	}
}
