/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// cmodel.c -- model loading

#include "qcommon.h"
#include "cm_local.h"
#include "../qalgo/md5.h"

static bool	cm_initialized = false;

static mempool_t *cmap_mempool;

static cvar_t *cm_noAreas;
cvar_t *cm_noCurves;

void CM_LoadQ3BrushModel( cmodel_state_t *cms, void *parent, void *buffer, bspFormatDesc_t *format );

static const modelFormatDescr_t cm_supportedformats[] =
{
	// Q3-alike .bsp models
	{ "*", 4, q3BSPFormats, 0, ( const modelLoader_t )CM_LoadQ3BrushModel },

	// trailing NULL
	{ NULL,	0, NULL, 0, NULL }
};

/*
===============================================================================

PATCH LOADING

===============================================================================
*/

/*
* CM_Clear
*/
static void CM_Clear( cmodel_state_t *cms )
{
	int i;

	if( cms->map_shaderrefs )
	{
		Mem_Free( cms->map_shaderrefs[0].name );
		Mem_Free( cms->map_shaderrefs );
		cms->map_shaderrefs = NULL;
		cms->numshaderrefs = 0;
	}

	if( cms->map_faces )
	{
		for( i = 0; i < cms->numfaces; i++ )
			Mem_Free( cms->map_faces[i].facets );
		Mem_Free( cms->map_faces );
		cms->map_faces = NULL;
		cms->numfaces = 0;
	}

	if( cms->map_cmodels != &cms->map_cmodel_empty )
	{
		for( i = 0; i < cms->numcmodels; i++ )
		{
			Mem_Free( cms->map_cmodels[i].markfaces );
			Mem_Free( cms->map_cmodels[i].markbrushes );
		}
		Mem_Free( cms->map_cmodels );
		cms->map_cmodels = &cms->map_cmodel_empty;
		cms->numcmodels = 0;
	}

	if( cms->map_nodes )
	{
		Mem_Free( cms->map_nodes );
		cms->map_nodes = NULL;
		cms->numnodes = 0;
	}

	if( cms->map_markfaces )
	{
		Mem_Free( cms->map_markfaces );
		cms->map_markfaces = NULL;
		cms->nummarkfaces = 0;
	}

	if( cms->map_leafs != &cms->map_leaf_empty )
	{
		Mem_Free( cms->map_leafs );
		cms->map_leafs = &cms->map_leaf_empty;
		cms->numleafs = 0;
	}

	if( cms->map_areas != &cms->map_area_empty )
	{
		Mem_Free( cms->map_areas );
		cms->map_areas = &cms->map_area_empty;
		cms->numareas = 0;
	}

	if( cms->map_areaportals )
	{
		Mem_Free( cms->map_areaportals );
		cms->map_areaportals = NULL;
	}

	if( cms->map_planes )
	{
		Mem_Free( cms->map_planes );
		cms->map_planes = NULL;
		cms->numplanes = 0;
	}

	if( cms->map_markbrushes )
	{
		Mem_Free( cms->map_markbrushes );
		cms->map_markbrushes = NULL;
		cms->nummarkbrushes = 0;
	}

	if( cms->map_brushsides )
	{
		Mem_Free( cms->map_brushsides );
		cms->map_brushsides = NULL;
		cms->numbrushsides = 0;
	}

	if( cms->map_brushes )
	{
		Mem_Free( cms->map_brushes );
		cms->map_brushes = NULL;
		cms->numbrushes = 0;
	}

	if( cms->map_pvs )
	{
		Mem_Free( cms->map_pvs );
		cms->map_pvs = NULL;
	}

	if( cms->map_entitystring != &cms->map_entitystring_empty )
	{
		Mem_Free( cms->map_entitystring );
		cms->map_entitystring = &cms->map_entitystring_empty;
	}

	cms->map_name[0] = 0;

	ClearBounds( cms->world_mins, cms->world_maxs );

	cms->CM_TransformedBoxTrace = NULL;
	cms->CM_TransformedPointContents = NULL;
	cms->CM_RoundUpToHullSize = NULL;
}

/*
===============================================================================

MAP LOADING

===============================================================================
*/

/*
* CM_LoadMap
* Loads in the map and all submodels
* 
*  for spawning a server with no map at all, call like this:
*  CM_LoadMap( "", false, &checksum );	// no real map
*/
cmodel_t *CM_LoadMap( cmodel_state_t *cms, const char *name, bool clientload, unsigned *checksum )
{
	int length;
	unsigned *buf;
	char *header;
	const modelFormatDescr_t *descr;
	bspFormatDesc_t *bspFormat = NULL;

	assert( cms );
	assert( name && strlen( name ) < MAX_CONFIGSTRING_CHARS );
	assert( checksum );

	if( name && !strcmp( cms->map_name, name ) && ( clientload || !Cvar_Value( "flushmap" ) ) )
	{
		*checksum = cms->checksum;

		if( !clientload )
		{
			memset( cms->map_areaportals, 0, cms->numareas * cms->numareas * sizeof( *cms->map_areaportals ) );
			CM_FloodAreaConnections( cms );
		}

		return cms->map_cmodels; // still have the right version
	}

	CM_Clear( cms );

	if( !name || !name[0] )
	{
		cms->numleafs = 1;
		cms->numcmodels = 2;
		*checksum = 0;
		return cms->map_cmodels;    // cinematic servers won't have anything at all
	}

	//
	// load the file
	//
	length = FS_LoadFile( name, ( void ** )&buf, NULL, 0 );
	if( !buf )
		Com_Error( ERR_DROP, "Couldn't load %s", name );

	cms->checksum = md5_digest32( ( const uint8_t * )buf, length );
	*checksum = cms->checksum;

	// call the apropriate loader
	descr = Q_FindFormatDescriptor( cm_supportedformats, ( const uint8_t * )buf, (const bspFormatDesc_t **)&bspFormat );
	if( !descr )
		Com_Error( ERR_DROP, "CM_LoadMap: unknown fileid for %s", name );

	if( !bspFormat )
		Com_Error( ERR_DROP, "CM_LoadMap: %s: unknown bsp format" );

	// copy header into temp variable to be saveed in a cvar
	header = Mem_TempMalloc( descr->headerLen + 1 );
	memcpy( header, buf, descr->headerLen );
	header[descr->headerLen] = '\0';

	// store map format description in cvars
	Cvar_ForceSet( "cm_mapHeader", header );
	Cvar_ForceSet( "cm_mapVersion", va( "%i", LittleLong( *((int *)((uint8_t *)buf + descr->headerLen)) ) ) );

	Mem_TempFree( header );

	descr->loader( cms, NULL, buf, bspFormat );

	CM_InitBoxHull( cms );
	CM_InitOctagonHull( cms );

	if( cms->numareas )
	{
		cms->map_areas = Mem_Alloc( cms->mempool, cms->numareas * sizeof( *cms->map_areas ) );
		cms->map_areaportals = Mem_Alloc( cms->mempool, cms->numareas * cms->numareas * sizeof( *cms->map_areaportals ) );

		memset( cms->map_areaportals, 0, cms->numareas * cms->numareas * sizeof( *cms->map_areaportals ) );
		CM_FloodAreaConnections( cms );
	}

	memset( cms->nullrow, 255, MAX_CM_LEAFS / 8 );

	Q_strncpyz( cms->map_name, name, sizeof( cms->map_name ) );

	return cms->map_cmodels;
}

/*
* CM_LoadMapMessage
*/
char *CM_LoadMapMessage( char *name, char *message, int size )
{
	int file, len;
	uint8_t h_v[8];
	char *data, *entitystring;
	lump_t l;
	bool isworld;
	char key[MAX_KEY], value[MAX_VALUE], token[MAX_TOKEN_CHARS];
	size_t keyLength;
	const modelFormatDescr_t *descr;
	const bspFormatDesc_t *bspFormat = NULL;

	*message = '\0';

	len = FS_FOpenFile( name, &file, FS_READ );
	if( !file || len < 1 )
	{
		if( file )
			FS_FCloseFile( file );
		return message;
	}

	FS_Read( h_v, sizeof( h_v ), file );
	descr = Q_FindFormatDescriptor( cm_supportedformats, h_v, &bspFormat );
	if( !descr )
	{
		Com_Printf( "CM_LoadMapMessage: %s: unknown bsp format\n", name );
		FS_FCloseFile( file );
		return message;
	}

	FS_Seek( file, descr->headerLen + sizeof( int ) + sizeof( lump_t ) * bspFormat->entityLumpNum, FS_SEEK_SET );

	FS_Read( &l, sizeof( l ), file );
	l.fileofs = LittleLong( l.fileofs );
	l.filelen = LittleLong( l.filelen );

	if( !l.filelen )
	{
		FS_FCloseFile( file );
		return message;
	}

	FS_Seek( file, l.fileofs, FS_SEEK_SET );

	entitystring = Mem_TempMalloc( l.filelen + 1 );
	FS_Read( entitystring, l.filelen, file );
	entitystring[l.filelen] = '\0';

	FS_FCloseFile( file );

	for( data = entitystring; ( COM_Parse_r( token, sizeof( token ), &data ) ) && token[0] == '{'; )
	{
		isworld = false;
		*message = '\0';

		while( 1 )
		{
			COM_Parse_r( token, sizeof( token ), &data );
			if( !token[0] || token[0] == '}' )
				break; // end of entity

			Q_strncpyz( key, token, sizeof( key ) );
			// remove trailing spaces
			keyLength = strlen( key );
			while( keyLength && key[keyLength - 1] == ' ' )
				keyLength--;
			key[keyLength] = '\0';

			COM_Parse_r( token, sizeof( token ), &data );
			if( !token[0] )
				break; // error

			Q_strncpyz( value, token, sizeof( value ) );

			// now that we have the key pair worked out...
			if( !strcmp( key, "classname" ) )
			{
				isworld = strcmp( value, "worldspawn" ) == 0;
				if( *message )
					break;
			}
			else if( !strcmp( key, "message" ) )
			{
				Q_strncpyz( message, token, size );
				if( isworld )
					break;
			}
		}

		if( isworld )
			break;
	}

	Mem_Free( entitystring );

	return message;
}

/*
* CM_InlineModel
*/
cmodel_t *CM_InlineModel( cmodel_state_t *cms, int num )
{
	if( num < 0 || num >= cms->numcmodels )
		Com_Error( ERR_DROP, "CM_InlineModel: bad number %i (%i)", num, cms->numcmodels );
	return &cms->map_cmodels[num];
}

/*
* CM_NumInlineModels
*/
int CM_NumInlineModels( cmodel_state_t *cms )
{
	return cms->numcmodels;
}

/*
* CM_InlineModelBounds
*/
void CM_InlineModelBounds( cmodel_state_t *cms, cmodel_t *cmodel, vec3_t mins, vec3_t maxs )
{
	if( cmodel == cms->map_cmodels )
	{
		VectorCopy( cms->world_mins, mins );
		VectorCopy( cms->world_maxs, maxs );
	}
	else
	{
		VectorCopy( cmodel->mins, mins );
		VectorCopy( cmodel->maxs, maxs );
	}
}

/*
* CM_ShaderrefName
*/
const char *CM_ShaderrefName( cmodel_state_t *cms, int ref )
{
	if( ref < 0 || ref >= cms->numshaderrefs )
		return NULL;
	return cms->map_shaderrefs[ref].name;
}

/*
* CM_EntityStringLen
*/
int CM_EntityStringLen( cmodel_state_t *cms )
{
	return cms->numentitychars;
}

/*
* CM_EntityString
*/
char *CM_EntityString( cmodel_state_t *cms )
{
	return cms->map_entitystring;
}

/*
* CM_LeafCluster
*/
int CM_LeafCluster( cmodel_state_t *cms, int leafnum )
{
	if( leafnum < 0 || leafnum >= cms->numleafs )
		Com_Error( ERR_DROP, "CM_LeafCluster: bad number" );
	return cms->map_leafs[leafnum].cluster;
}

/*
* CM_LeafArea
*/
int CM_LeafArea( cmodel_state_t *cms, int leafnum )
{
	if( leafnum < 0 || leafnum >= cms->numleafs )
		Com_Error( ERR_DROP, "CM_LeafArea: bad number" );
	return cms->map_leafs[leafnum].area;
}

/*
===============================================================================

PVS

===============================================================================
*/

/*
* CM_ClusterRowSize
*/
int CM_ClusterRowSize( cmodel_state_t *cms )
{
	return cms->map_pvs ? cms->map_pvs->rowsize : MAX_CM_LEAFS / 8;
}

/*
* CM_ClusterRowLongs
*/
static int CM_ClusterRowLongs( cmodel_state_t *cms )
{
	return cms->map_pvs ? (cms->map_pvs->rowsize + 3) / 4 : MAX_CM_LEAFS / 32;
}

/*
* CM_NumClusters
*/
int CM_NumClusters( cmodel_state_t *cms )
{
	return cms->map_pvs ? cms->map_pvs->numclusters : 0;
}

/*
* CM_PVSData
*/
dvis_t *CM_PVSData( cmodel_state_t *cms )
{
	return cms->map_pvs;
}

/*
* CM_ClusterVS
*/
static inline uint8_t *CM_ClusterVS( int cluster, dvis_t *vis, uint8_t *nullrow )
{
	if( cluster == -1 || !vis )
		return nullrow;
	return ( uint8_t * )vis->data + cluster * vis->rowsize;
}

/*
* CM_ClusterPVS
*/
static inline uint8_t *CM_ClusterPVS( cmodel_state_t *cms, int cluster )
{
	return CM_ClusterVS( cluster, cms->map_pvs, cms->nullrow );
}

/*
* CM_NumAreas
*/
int CM_NumAreas( cmodel_state_t *cms )
{
	return cms->numareas;
}

/*
* CM_AreaRowSize
*/
int CM_AreaRowSize( cmodel_state_t *cms )
{
	return (cms->numareas + 7) / 8;
}

/*
===============================================================================

AREAPORTALS

===============================================================================
*/

/*
* CM_FloodArea_r
*/
static void CM_FloodArea_r( cmodel_state_t *cms, int areanum, int floodnum )
{
	int i;
	carea_t	*area;
	int *p;

	area = &cms->map_areas[areanum];
	if( area->floodvalid == cms->floodvalid )
	{
		if( area->floodnum == floodnum )
			return;
		Com_Error( ERR_DROP, "FloodArea_r: reflooded" );
	}

	area->floodnum = floodnum;
	area->floodvalid = cms->floodvalid;
	p = cms->map_areaportals + areanum * cms->numareas;
	for( i = 0; i < cms->numareas; i++ )
	{
		if( p[i] > 0 )
			CM_FloodArea_r( cms, i, floodnum );
	}
}

/*
* CM_FloodAreaConnections
*/
void CM_FloodAreaConnections( cmodel_state_t *cms )
{
	int i;
	int floodnum;

	// all current floods are now invalid
	cms->floodvalid++;
	floodnum = 0;
	for( i = 0; i < cms->numareas; i++ )
	{
		if( cms->map_areas[i].floodvalid == cms->floodvalid )
			continue; // already flooded into
		floodnum++;
		CM_FloodArea_r( cms, i, floodnum );
	}
}

/*
* CM_SetAreaPortalState
*/
void CM_SetAreaPortalState( cmodel_state_t *cms, int area1, int area2, bool open )
{
	int row1, row2;

	if( area1 == area2 )
		return;
	if( area1 < 0 || area2 < 0 )
		return;

	row1 = area1 * cms->numareas + area2;
	row2 = area2 * cms->numareas + area1;
	if( open ) {
		cms->map_areaportals[row1]++;
		cms->map_areaportals[row2]++;
	} else {
		if( cms->map_areaportals[row1] > 0 )
			cms->map_areaportals[row1]--;
		if( cms->map_areaportals[row2] > 0 )
			cms->map_areaportals[row2]--;
	}

	CM_FloodAreaConnections( cms );
}

/*
* CM_AreasConnected
*/
bool CM_AreasConnected( cmodel_state_t *cms, int area1, int area2 )
{
	if( cm_noAreas->integer )
		return true;
	if( cms->cmap_bspFormat->flags & BSP_NOAREAS )
		return true;

	if( area1 == area2 )
		return true;
	if( area1 < 0 || area2 < 0 )
		return true;

	if( area1 >= cms->numareas || area2 >= cms->numareas )
		Com_Error( ERR_DROP, "CM_AreasConnected: area >= numareas" );

	if( cms->map_areas[area1].floodnum == cms->map_areas[area2].floodnum )
		return true;
	return false;
}

/*
* CM_MergeAreaBits
*/
static int CM_MergeAreaBits( cmodel_state_t *cms, uint8_t *buffer, int area )
{
	int i;

	if( area < 0 )
		return CM_AreaRowSize( cms );

	for( i = 0; i < cms->numareas; i++ )
	{
		if( CM_AreasConnected( cms, i, area ) )
			buffer[i>>3] |= 1 << ( i&7 );
	}

	return CM_AreaRowSize( cms );
}

/*
* CM_WriteAreaBits
*/
int CM_WriteAreaBits( cmodel_state_t *cms, uint8_t *buffer )
{
	int i;
	int rowsize, bytes;

	rowsize = CM_AreaRowSize( cms );
	bytes = rowsize * cms->numareas;

	if( cm_noAreas->integer || cms->cmap_bspFormat->flags & BSP_NOAREAS )
	{
		// for debugging, send everything
		memset( buffer, 255, bytes );
	}
	else
	{
		uint8_t *row;

		memset( buffer, 0, bytes );

		for( i = 0; i < cms->numareas; i++ )
		{
			row = buffer + i * rowsize;
			CM_MergeAreaBits( cms, row, i );
		}
	}

	return bytes;
}

/*
* CM_ReadAreaBits
*/
void CM_ReadAreaBits( cmodel_state_t *cms, uint8_t *buffer )
{
	int i, j;
	int rowsize;

	memset( cms->map_areaportals, 0, cms->numareas * cms->numareas * sizeof( *cms->map_areaportals ) );

	rowsize = CM_AreaRowSize( cms );
	for( i = 0; i < cms->numareas; i++ )
	{
		uint8_t *row;

		row = buffer + i * rowsize;
		for( j = 0; j < cms->numareas; j++ )
		{
			if( row[j>>3] & (1<<(j&7)) )
				cms->map_areaportals[i * cms->numareas + j] = 1;
		}
	}

	CM_FloodAreaConnections( cms );
}

/*
* CM_WritePortalState
* Writes the portal state to a savegame file
*/
void CM_WritePortalState( cmodel_state_t *cms, int file )
{
	int i, j, t;

	for( i = 0; i < cms->numareas; i++ )
	{
		for( j = 0; j < cms->numareas; j++ )
		{
			t = LittleLong( cms->map_areaportals[i * cms->numareas + j] );
			FS_Write( &t, sizeof( t ), file );
		}
	}
}

/*
* CM_ReadPortalState
* Reads the portal state from a savegame file
* and recalculates the area connections
*/
void CM_ReadPortalState( cmodel_state_t *cms, int file )
{
	int i;

	FS_Read( &cms->map_areaportals, cms->numareas * cms->numareas * sizeof( *cms->map_areaportals ), file );

	for( i = 0; i < cms->numareas * cms->numareas; i++ )
		cms->map_areaportals[i] = LittleLong( cms->map_areaportals[i] );

	CM_FloodAreaConnections( cms );
}

/*
* CM_HeadnodeVisible
* Returns true if any leaf under headnode has a cluster that
* is potentially visible
*/
bool CM_HeadnodeVisible( cmodel_state_t *cms, int nodenum, uint8_t *visbits )
{
	int cluster;
	cnode_t	*node;

	while( nodenum >= 0 )
	{
		node = &cms->map_nodes[nodenum];
		if( CM_HeadnodeVisible( cms, node->children[0], visbits ) )
			return true;
		nodenum = node->children[1];
	}

	cluster = cms->map_leafs[-1 - nodenum].cluster;
	if( cluster == -1 )
		return false;
	if( visbits[cluster>>3] & ( 1<<( cluster&7 ) ) )
		return true;
	return false;
}


/*
* CM_MergePVS
* Merge PVS at origin into out
*/
void CM_MergePVS( cmodel_state_t *cms, vec3_t org, uint8_t *out )
{
	int leafs[128];
	int i, j, count;
	int longs;
	uint8_t *src;
	vec3_t mins, maxs;

	for( i = 0; i < 3; i++ )
	{
		mins[i] = org[i] - 9;
		maxs[i] = org[i] + 9;
	}

	count = CM_BoxLeafnums( cms, mins, maxs, leafs, sizeof( leafs )/sizeof( int ), NULL );
	if( count < 1 )
		Com_Error( ERR_FATAL, "CM_MergePVS: count < 1" );
	longs = CM_ClusterRowLongs( cms );

	// convert leafs to clusters
	for( i = 0; i < count; i++ )
		leafs[i] = CM_LeafCluster( cms, leafs[i] );

	// or in all the other leaf bits
	for( i = 0; i < count; i++ )
	{
		for( j = 0; j < i; j++ )
			if( leafs[i] == leafs[j] )
				break;
		if( j != i )
			continue; // already have the cluster we want
		src = CM_ClusterPVS( cms, leafs[i] );
		for( j = 0; j < longs; j++ )
			( (int *)out )[j] |= ( (int *)src )[j];
	}
}

/*
* CM_MergeVisSets
*/
int CM_MergeVisSets( cmodel_state_t *cms, vec3_t org, uint8_t *pvs, uint8_t *areabits )
{
	int area;

	assert( pvs || areabits );

	if( pvs )
		CM_MergePVS( cms, org, pvs );

	area = CM_PointLeafnum( cms, org );

	area = CM_LeafArea( cms, area );
	if( areabits && area > -1 )
		CM_MergeAreaBits( cms, areabits, area );

	return CM_AreaRowSize( cms ); // areabytes
}

/*
* CM_InPVS
*
* Also checks portalareas so that doors block sight
*/
bool CM_InPVS( cmodel_state_t *cms, const vec3_t p1, const vec3_t p2 )
{
	int leafnum;
	int cluster;
	int area1, area2;
	uint8_t *mask;

	leafnum = CM_PointLeafnum( cms, p1 );
	cluster = CM_LeafCluster( cms, leafnum );
	area1 = CM_LeafArea( cms, leafnum );
	mask = CM_ClusterPVS( cms, cluster );

	leafnum = CM_PointLeafnum( cms, p2 );
	cluster = CM_LeafCluster( cms, leafnum );
	area2 = CM_LeafArea( cms, leafnum );

	if( ( !( mask[cluster>>3] & ( 1<<( cluster&7 ) ) ) ) )
		return false;

	if( !CM_AreasConnected( cms, area1, area2 ) )
		return false; // a door blocks sight

	return true;
}

/*
* CM_New
*/
cmodel_state_t *CM_New( void *mempool )
{
	cmodel_state_t *cms;
	mempool_t *cms_mempool;

	cms_mempool = (mempool ? (mempool_t *)mempool : cmap_mempool);
	cms = Mem_Alloc( cms_mempool, sizeof( cmodel_state_t ) );

	cms->mempool = cms_mempool;
	cms->map_cmodels = &cms->map_cmodel_empty;
	cms->map_leafs = &cms->map_leaf_empty;
	cms->map_areas = &cms->map_area_empty;
	cms->map_entitystring = &cms->map_entitystring_empty;

	return cms;
}

/*
* CM_Free
*/
static void CM_Free( cmodel_state_t *cms )
{
	CM_Clear( cms );

	Mem_Free( cms );
}

/*
* CM_AddReference
*/
void CM_AddReference( cmodel_state_t *cms )
{
	if( !cms ) {
		return;
	}
	cms->refcount++;
}

/*
* CM_ReleaseReference
*/
void CM_ReleaseReference( cmodel_state_t *cms )
{
	if( !cms ) {
		return;
	}

	cms->refcount--;
	if( cms->refcount > 0 ) {
		return;
	}

	CM_Free( cms );
}

/*
* CM_Init
*/
void CM_Init( void )
{
	assert( !cm_initialized );

	cmap_mempool = Mem_AllocPool( NULL, "Collision Map" );

	cm_noAreas =	    Cvar_Get( "cm_noAreas", "0", CVAR_CHEAT );
	cm_noCurves =	    Cvar_Get( "cm_noCurves", "0", CVAR_CHEAT );

	cm_initialized = true;
}

/*
* CM_Shutdown
*/
void CM_Shutdown( void )
{
	if( !cm_initialized )
		return;

	Mem_FreePool( &cmap_mempool );

	cm_initialized = false;
}
