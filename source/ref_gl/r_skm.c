 /*
Copyright (C) 2002-2011 Victor Luchits

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

// r_skm.c: skeletal animation model format

#include "r_local.h"
#include "iqm.h"

// typedefs
typedef struct iqmheader iqmheader_t;
typedef struct iqmvertexarray iqmvertexarray_t;
typedef struct iqmjoint iqmjoint_t;
typedef struct iqmpose iqmpose_t;
typedef struct iqmmesh iqmmesh_t;
typedef struct iqmbounds iqmbounds_t;

/*
==============================================================================

IQM MODELS

==============================================================================
*/

/*
* Mod_SkeletalBuildStaticVBOForMesh
* 
* Builds a static vertex buffer object for given skeletal model mesh
*/
static void Mod_SkeletalBuildStaticVBOForMesh( mskmesh_t *mesh )
{
	mesh_t skmmesh;
	vattribmask_t vattribs;
	
	vattribs = VATTRIB_POSITION_BIT | VATTRIB_TEXCOORDS_BIT | VATTRIB_NORMAL_BIT | VATTRIB_SVECTOR_BIT;
	if( glConfig.maxGLSLBones > 0 ) {
		vattribs |= VATTRIB_BONES_BITS;
	}
	if( mesh->skin.shader ) {
		vattribs |= mesh->skin.shader->vattribs;
	}

	mesh->vbo = R_CreateMeshVBO( ( void * )mesh, 
		mesh->numverts, mesh->numtris * 3, 0, vattribs, VBO_TAG_MODEL, vattribs );

	if( !mesh->vbo ) {
		return;
	}

	memset( &skmmesh, 0, sizeof( skmmesh ) );

	skmmesh.elems = mesh->elems;
	skmmesh.numElems = mesh->numtris * 3;
	skmmesh.numVerts = mesh->numverts;

	skmmesh.xyzArray = mesh->xyzArray;
	skmmesh.stArray = mesh->stArray;
	skmmesh.normalsArray = mesh->normalsArray;
	skmmesh.sVectorsArray = mesh->sVectorsArray;

	skmmesh.blendIndices = mesh->blendIndices;
	skmmesh.blendWeights = mesh->blendWeights;

	R_UploadVBOVertexData( mesh->vbo, 0, vattribs, &skmmesh ); 
	R_UploadVBOElemData( mesh->vbo, 0, 0, &skmmesh );
}

/*
* Mod_TouchSkeletalModel
*/
static void Mod_TouchSkeletalModel( model_t *mod )
{
	unsigned int i;
	mskmesh_t *mesh;
	mskskin_t *skin;
	mskmodel_t *skmodel = ( mskmodel_t * )mod->extradata;

	mod->registrationSequence = rsh.registrationSequence;

	for( i = 0, mesh = skmodel->meshes; i < skmodel->nummeshes; i++, mesh++ ) {
		// register needed skins and images
		skin = &mesh->skin;
		if( skin->shader ) {
			R_TouchShader( skin->shader );
		}
		if( mesh->vbo ) {
			R_TouchMeshVBO( mesh->vbo );
		}
	}
}

/*
* Mod_SkeletalModel_AddBlend
* 
* If there's only one influencing bone, return its index early.
* Otherwise lookup identical blending combination.
*/
static int Mod_SkeletalModel_AddBlend( mskmodel_t *model, const mskblend_t *newblend )
{
	unsigned int i, j;
	mskblend_t t;
	mskblend_t *blends;

	t = *newblend;

	// sort influences in descending order	
	for( i = 0; i < SKM_MAX_WEIGHTS; i++ ) {
		for( j = i + 1; j < SKM_MAX_WEIGHTS; j++ ) {
			if( t.weights[i] < t.weights[j] ) {
				uint8_t bi, bw;
				bi = t.indices[i];
				bw = t.weights[i];
				t.indices[i] = t.indices[j];
				t.weights[i] = t.weights[j];
				t.indices[j] = bi;
				t.weights[j] = bw;
			}
		}
	}

	if( !t.weights[1] ) {
		return t.indices[0];
	}

	for( i = 0, blends = model->blends; i < model->numblends; i++, blends++ ) {
		if( !memcmp( blends, &t, sizeof( mskblend_t ) ) ) {
			return model->numbones + i;
		}
	}

	model->numblends++;
	memcpy( blends, &t, sizeof( mskblend_t ) );

	return model->numbones + i;
}

/*
* Mod_LoadSkeletalModel
*/
void Mod_LoadSkeletalModel( model_t *mod, const model_t *parent, void *buffer, bspFormatDesc_t *unused )
{
	unsigned int i, j, k;
	size_t filesize;
	uint8_t *pbase;
	size_t memsize;
	uint8_t *pmem;
	iqmheader_t *header;
	char *texts;
	iqmvertexarray_t *vas, va;
	iqmjoint_t *joints, joint;
	bonepose_t *baseposes;
	iqmpose_t *poses, pose;
	unsigned short *framedata;
	const int *inelems;
	elem_t *outelems;
	iqmmesh_t *inmeshes, inmesh;
	iqmbounds_t *inbounds, inbound;
	float *vposition, *vtexcoord, *vnormal, *vtangent;
	uint8_t *vblendindices_byte, *vblendweights_byte;
	int *vblendindexes_int;
	float *vblendweights_float;
	mskmodel_t *poutmodel;

	baseposes = NULL;
	header = ( iqmheader_t * )buffer;

	// check IQM magic
	if( memcmp( header->magic, "INTERQUAKEMODEL", 16 ) ) {
		ri.Com_Printf( S_COLOR_RED "ERROR: %s is not an Inter-Quake Model\n", mod->name );
		goto error;
	}

	// check header version
	header->version = LittleLong( header->version );
	if( header->version != IQM_VERSION ) {
		ri.Com_Printf( S_COLOR_RED "ERROR: %s has wrong type number (%i should be %i)\n", mod->name, header->version, IQM_VERSION );
		goto error;
	}

	// byteswap header
#define H_SWAP(s) (header->s = LittleLong( header->s ))
	H_SWAP( filesize );
	H_SWAP( flags );
	H_SWAP( num_text );
	H_SWAP( ofs_text );
	H_SWAP( num_meshes );
	H_SWAP( ofs_meshes );
	H_SWAP( num_vertexarrays );
	H_SWAP( num_vertexes );
	H_SWAP( ofs_vertexarrays );
	H_SWAP( num_triangles );
	H_SWAP( ofs_triangles );
	H_SWAP( ofs_adjacency );
	H_SWAP( num_joints );
	H_SWAP( ofs_joints );
	H_SWAP( num_poses );
	H_SWAP( ofs_poses );
	H_SWAP( num_anims );
	H_SWAP( ofs_anims );
	H_SWAP( num_frames );
	H_SWAP( num_framechannels );
	H_SWAP( ofs_frames );
	H_SWAP( ofs_bounds );
	H_SWAP( num_comment );
	H_SWAP( ofs_comment );
	H_SWAP( num_extensions );
	H_SWAP( ofs_extensions );
#undef H_SWAP

	if( header->num_triangles < 1 || header->num_vertexes < 3 || header->num_vertexarrays < 1 || header->num_meshes < 1 ) {
		ri.Com_Printf( S_COLOR_RED "ERROR: %s has no geometry\n", mod->name );
		goto error;
	}
	if( header->num_vertexes >= USHRT_MAX ) {
		ri.Com_Printf( S_COLOR_RED "ERROR: %s has too many vertices\n", mod->name );
		goto error;
	}
	if( header->num_frames < 1 || header->num_anims < 1 ) {
		ri.Com_Printf( S_COLOR_RED "ERROR: %s has no animations\n", mod->name );
		goto error;
	}
	if( header->num_joints != header->num_poses ) {
		ri.Com_Printf( S_COLOR_RED "ERROR: %s has an invalid number of poses: %i vs %i\n", mod->name, header->num_joints, header->num_poses );
		goto error;
	}
	if( !header->ofs_bounds ) {
		ri.Com_Printf( S_COLOR_RED "ERROR: %s has no frame bounds\n", mod->name );
		goto error;
	}

	pbase = ( uint8_t * )buffer;
	filesize = header->filesize;

	// check data offsets against the filesize
	if( header->ofs_text + header->num_text > filesize
		|| header->ofs_vertexarrays + header->num_vertexarrays * sizeof( iqmvertexarray_t ) > filesize
		|| header->ofs_joints + header->num_joints * sizeof( iqmjoint_t ) > filesize
		|| header->ofs_frames + header->num_frames * header->num_framechannels * sizeof( unsigned short ) > filesize
		|| header->ofs_triangles + header->num_triangles * sizeof( int[3] ) > filesize
		|| header->ofs_meshes + header->num_meshes * sizeof( iqmmesh_t ) > filesize
		|| header->ofs_bounds + header->num_frames * sizeof( iqmbounds_t ) > filesize
		) {
		ri.Com_Printf( S_COLOR_RED "ERROR: %s has invalid size or offset information\n", mod->name );
		goto error;
	}

	poutmodel = mod->extradata = Mod_Malloc( mod, sizeof( *poutmodel ) );


	// load text
	texts = Mod_Malloc( mod, header->num_text + 1 );
	if( header->ofs_text ) {
		memcpy( texts, (const char *)(pbase + header->ofs_text), header->num_text );
	}
	texts[header->ofs_text] = '\0';


	// load vertex arrays
	vposition = NULL;
	vtexcoord = NULL;
	vnormal = NULL;
	vtangent = NULL;
	vblendindices_byte = NULL;
	vblendindexes_int = NULL;
	vblendweights_byte = NULL;
	vblendweights_float = NULL;

	vas = ( iqmvertexarray_t * )( pbase + header->ofs_vertexarrays );
	for( i = 0; i < header->num_vertexarrays; i++ ) {
		size_t vsize;

		memcpy( &va, &vas[i], sizeof( iqmvertexarray_t ) );

		va.type = LittleLong( va.type );
		va.flags = LittleLong( va.flags );
		va.format = LittleLong( va.format );
		va.size = LittleLong( va.size );
		va.offset = LittleLong( va.offset );

		vsize = header->num_vertexes*va.size;
		switch( va.format ) { 
			case IQM_FLOAT:
				vsize *= sizeof( float );
				break;
			case IQM_INT:
			case IQM_UINT:
				vsize *= sizeof( int );
				break;
			case IQM_BYTE:
			case IQM_UBYTE:
				vsize *= sizeof( unsigned char );
				break;
			default:
				continue;
		}

		if( va.offset + vsize > filesize ) {
			continue;
		}

		switch( va.type ) {
			case IQM_POSITION:
				if( va.format == IQM_FLOAT && va.size == 3 ) {
					vposition = ( float * )( pbase + va.offset );
				}
				break;
			case IQM_TEXCOORD:
				if( va.format == IQM_FLOAT && va.size == 2 ) {
					vtexcoord = ( float * )( pbase + va.offset );
				}
				break;
			case IQM_NORMAL:
				if( va.format == IQM_FLOAT && va.size == 3 ) {
					vnormal = ( float * )( pbase + va.offset );
				}
				break;
			case IQM_TANGENT:
				if( va.format == IQM_FLOAT && va.size == 4 ) {
					vtangent = ( float * )( pbase + va.offset );
				}
				break;
			case IQM_BLENDINDEXES:
				if( va.size != SKM_MAX_WEIGHTS )
					break;
				if( va.format == IQM_BYTE || va.format == IQM_UBYTE ) {
					vblendindices_byte = ( uint8_t * )( pbase + va.offset );
				}
				else if( va.format == IQM_INT || va.format == IQM_UINT ) {
					vblendindexes_int = ( int * )( pbase + va.offset );
				}
				break;
			case IQM_BLENDWEIGHTS:
				if( va.size != SKM_MAX_WEIGHTS )
					break;
				if( va.format == IQM_UBYTE ) {
					vblendweights_byte = ( uint8_t * )( pbase + va.offset );
				}
				else if( va.format == IQM_FLOAT ) {
					vblendweights_float = ( float * )( pbase + va.offset );
				}
				break;
			default:
				break;
		}
	}

	if( !vposition || !vtexcoord 
		|| !(vblendindices_byte || vblendindexes_int) 
		|| !(vblendweights_byte || vblendweights_float) ) {
		ri.Com_Printf( S_COLOR_RED "ERROR: %s is missing vertex array data\n", mod->name );
		goto error;
	}

	// load joints
	memsize = 0;
	memsize += sizeof( bonepose_t ) * header->num_joints;
	pmem = Mod_Malloc( mod, memsize );

	baseposes = ( void * )pmem; pmem += sizeof( *baseposes );

	memsize = 0;
	memsize += sizeof( mskbone_t ) * header->num_joints;
	memsize += sizeof( bonepose_t ) * header->num_joints;
	pmem = Mod_Malloc( mod, memsize );

	poutmodel->numbones = header->num_joints;
	poutmodel->bones = ( void * )pmem; pmem += sizeof( *poutmodel->bones ) * poutmodel->numbones;
	poutmodel->invbaseposes = ( void * )pmem; pmem += sizeof( *poutmodel->invbaseposes ) * poutmodel->numbones;

	joints = ( iqmjoint_t * )( pbase + header->ofs_joints );
	for( i = 0; i < poutmodel->numbones; i++ ) {
		memcpy( &joint, &joints[i], sizeof( iqmjoint_t ) );

		joint.name = LittleLong( joint.name );
		joint.parent = LittleLong( joint.parent );

		for( j = 0; j < 3; j++ ) {
			joint.translate[j] = LittleFloat( joint.translate[j] );
			joint.rotate[j] = LittleFloat( joint.rotate[j] );
			joint.scale[j] = LittleFloat( joint.scale[j] );
		}

		if( joints[i].parent >= (int)i ) {
			ri.Com_Printf( S_COLOR_RED "ERROR: %s bone[%i].parent(%i) >= %i\n", mod->name, i, joint.parent, i );
			goto error;
		}

		poutmodel->bones[i].name = texts + joint.name;
		poutmodel->bones[i].parent = joint.parent;

		DualQuat_FromQuat3AndVector( joint.rotate, joint.translate, baseposes[i].dualquat );

		// scale is unused

		// reconstruct invserse bone pose

		if( joint.parent >= 0 )
		{
			bonepose_t bp, *pbp;
			bp = baseposes[i];
			pbp = &baseposes[joint.parent];

			DualQuat_Multiply( pbp->dualquat, bp.dualquat, baseposes[i].dualquat );
		}

		DualQuat_Copy( baseposes[i].dualquat, poutmodel->invbaseposes[i].dualquat );
		DualQuat_Invert( poutmodel->invbaseposes[i].dualquat );
	}


	// load frames
	poses = ( iqmpose_t * )( pbase + header->ofs_poses );
	for( i = 0; i < header->num_poses; i++ ) {
		memcpy( &pose, &poses[i], sizeof( iqmpose_t ) );

		pose.parent = LittleLong( pose.parent );
		pose.mask = LittleLong( pose.mask );

		for( j = 0; j < 10; j++ ) {
			pose.channeloffset[j] = LittleFloat( pose.channeloffset[j] );
			pose.channelscale[j] = LittleFloat( pose.channelscale[j] );
		}

		memcpy( &poses[i], &pose, sizeof( iqmpose_t ) );
	}

	memsize = 0;
	memsize += sizeof( mskframe_t ) * header->num_frames;
	memsize += sizeof( bonepose_t ) * header->num_joints * header->num_frames;
	pmem = Mod_Malloc( mod, memsize );

	poutmodel->numframes = header->num_frames;
	poutmodel->frames = ( mskframe_t * )pmem; pmem += sizeof( mskframe_t ) * poutmodel->numframes;

	framedata = ( unsigned short * )( pbase + header->ofs_frames );
	for( i = 0; i < header->num_frames; i++ ) {
		bonepose_t *pbp;
		unsigned short fd[7], *pfd;
		unsigned int fdsize;
		vec3_t translate;
		quat_t rotate;

		poutmodel->frames[i].boneposes = ( bonepose_t * )pmem; pmem += sizeof( bonepose_t ) * poutmodel->numbones;

		for( j = 0, pbp = poutmodel->frames[i].boneposes; j < header->num_poses; j++, pbp++ ) {
			memcpy( &pose, &poses[j], sizeof( iqmpose_t ) );

			fdsize = 0;
			for( k = 0; k < 7; k++ ) {
				fdsize += (pose.mask >> k) & 1;
			}
			memcpy( fd, framedata, sizeof( unsigned short ) * fdsize );
			for( k = 0; k < fdsize; k++ ) {
				fd[k] = LittleShort( fd[k] );
			}
			pfd = fd;
			framedata += fdsize;

			translate[0] = pose.channeloffset[0]; if( pose.mask & 0x01 ) translate[0] += *(pfd++) * pose.channelscale[0];
			translate[1] = pose.channeloffset[1]; if( pose.mask & 0x02 ) translate[1] += *(pfd++) * pose.channelscale[1];
			translate[2] = pose.channeloffset[2]; if( pose.mask & 0x04 ) translate[2] += *(pfd++) * pose.channelscale[2];

			rotate[0] = pose.channeloffset[3]; if( pose.mask & 0x08 ) rotate[0] += *(pfd++) * pose.channelscale[3];
			rotate[1] = pose.channeloffset[4]; if( pose.mask & 0x10 ) rotate[1] += *(pfd++) * pose.channelscale[4];
			rotate[2] = pose.channeloffset[5]; if( pose.mask & 0x20 ) rotate[2] += *(pfd++) * pose.channelscale[5];
			rotate[3] = pose.channeloffset[6]; if( pose.mask & 0x40 ) rotate[3] += *(pfd++) * pose.channelscale[6];
			if( rotate[3] > 0 ) {
				Vector4Inverse( rotate );
			}
			Vector4Normalize( rotate );

			// scale is unused
			if( pose.mask & 0x80  ) framedata++;
			if( pose.mask & 0x100 ) framedata++;
			if( pose.mask & 0x200 ) framedata++;

			DualQuat_FromQuatAndVector( rotate, translate, pbp->dualquat );
		}
	}


	// load triangles
	memsize = 0;
	memsize += sizeof( *outelems ) * header->num_triangles * 3;
	pmem = Mod_Malloc( mod, memsize );

	poutmodel->numtris = header->num_triangles;
	poutmodel->elems = ( elem_t * )pmem; pmem += sizeof( *outelems ) * header->num_triangles * 3;

	inelems = ( const int * )(pbase + header->ofs_triangles);
	outelems = poutmodel->elems;

	for( i = 0; i < header->num_triangles; i++ ) {
		int e[3];
		memcpy( e, inelems, sizeof( int ) * 3 );
		for( j = 0; j < 3; j++ ) {
			outelems[j] = LittleLong( e[j] );
		}
		inelems += 3;
		outelems += 3;
	}


	// load vertices
	memsize = 0;
	memsize += sizeof( *poutmodel->sVectorsArray ) * header->num_vertexes;	// 16-bytes aligned
	memsize += sizeof( *poutmodel->xyzArray ) * header->num_vertexes;
	memsize += sizeof( *poutmodel->normalsArray ) * header->num_vertexes;
	memsize += sizeof( *poutmodel->stArray ) * header->num_vertexes;
	memsize += sizeof( *poutmodel->blendWeights ) * header->num_vertexes * SKM_MAX_WEIGHTS;
	memsize += sizeof( *poutmodel->blendIndices ) * header->num_vertexes * SKM_MAX_WEIGHTS;
	pmem = Mod_Malloc( mod, memsize );

	poutmodel->numverts = header->num_vertexes;

	// S-vectors
	poutmodel->sVectorsArray = ( vec4_t * )pmem; pmem += sizeof( *poutmodel->sVectorsArray ) * header->num_vertexes;

	if( vtangent ) {
		memcpy( poutmodel->sVectorsArray, vtangent, sizeof( vec4_t ) * header->num_vertexes );
		for( i = 0; i < header->num_vertexes; i++ ) {
			for( j = 0; j < 4; j++ ) {
				poutmodel->sVectorsArray[i][j] = LittleFloat( poutmodel->sVectorsArray[i][j] );
			}
		}
	}

	// XYZ positions
	poutmodel->xyzArray = ( vec4_t * )pmem; pmem += sizeof( *poutmodel->xyzArray ) * header->num_vertexes;
	for( i = 0; i < header->num_vertexes; i++ ) {
		memcpy( poutmodel->xyzArray[i], vposition, sizeof( vec3_t ) );
		for( j = 0; j < 3; j++ ) {
			poutmodel->xyzArray[i][j] = LittleFloat( poutmodel->xyzArray[i][j] );
		}
		poutmodel->xyzArray[i][3] = 1.0f;
		vposition += 3;
	}

	// normals
	poutmodel->normalsArray = ( vec4_t * )pmem; pmem += sizeof( *poutmodel->normalsArray ) * header->num_vertexes;
	for( i = 0; i < header->num_vertexes; i++ ) {
		memcpy( poutmodel->normalsArray[i], vnormal, sizeof( vec3_t ) );
		for( j = 0; j < 3; j++ ) {
			poutmodel->normalsArray[i][j] = LittleFloat( poutmodel->normalsArray[i][j] );
		}
		poutmodel->normalsArray[i][3] = 0.0f;
		vnormal += 3;
	}

	// texture coordinates
	poutmodel->stArray = ( vec2_t * )pmem; pmem += sizeof( *poutmodel->stArray ) * header->num_vertexes;
	memcpy( poutmodel->stArray, vtexcoord, sizeof( vec2_t ) * header->num_vertexes );
	for( i = 0; i < header->num_vertexes; i++ ) {
		for( j = 0; j < 2; j++ ) {
			poutmodel->stArray[i][j] = LittleFloat( poutmodel->stArray[i][j] );
		}
		vtexcoord += 2;
	}

	if( !vtangent ) {
		// if the loaded file is missing precomputed S-vectors, compute them now
		R_BuildTangentVectors( poutmodel->numverts, poutmodel->xyzArray, poutmodel->normalsArray, poutmodel->stArray, 
			poutmodel->numtris, poutmodel->elems, poutmodel->sVectorsArray );
	}

	// blend indices
	poutmodel->blendIndices = ( uint8_t * )pmem; pmem += sizeof( *poutmodel->blendIndices ) * header->num_vertexes * SKM_MAX_WEIGHTS;
	if( vblendindices_byte ) {
		memcpy( poutmodel->blendIndices, vblendindices_byte, sizeof( uint8_t ) * header->num_vertexes * SKM_MAX_WEIGHTS );
	} else if( vblendindexes_int ) {
		int bi[SKM_MAX_WEIGHTS];
		uint8_t *pbi = poutmodel->blendIndices;
		for( j = 0; j < header->num_vertexes; j++ ) {
			memcpy( bi, &vblendindexes_int[j * SKM_MAX_WEIGHTS], sizeof( int ) * SKM_MAX_WEIGHTS );
			for( k = 0; k < SKM_MAX_WEIGHTS; k++ ) {
				*( pbi++ ) = LittleLong( bi[k] );
			}
		}
	}

	// blend weights
	poutmodel->blendWeights = ( uint8_t * )pmem; pmem += sizeof( *poutmodel->blendWeights ) * header->num_vertexes * SKM_MAX_WEIGHTS;
	if( vblendweights_byte ) {
		memcpy( poutmodel->blendWeights, vblendweights_byte, sizeof( uint8_t ) * header->num_vertexes * SKM_MAX_WEIGHTS );
	}
	else if( vblendweights_float ) {
		float bw[SKM_MAX_WEIGHTS];
		uint8_t *pbw = poutmodel->blendWeights;
		for( j = 0; j < header->num_vertexes; j++ ) {
			memcpy( bw, &vblendweights_float[j * SKM_MAX_WEIGHTS], sizeof( float ) * SKM_MAX_WEIGHTS );
			for( k = 0; k < SKM_MAX_WEIGHTS; k++ ) {
				*( pbw++ ) = LittleFloat( bw[k] ) * 255.0f;
			}
		}
	}


	// blends
	memsize = 0;
	memsize += poutmodel->numverts * ( sizeof( mskblend_t ) + sizeof( unsigned int ) );
	pmem = Mod_Malloc( mod, memsize );

	poutmodel->numblends = 0;
	poutmodel->blends = ( mskblend_t * )pmem; pmem += sizeof( *poutmodel->blends ) * poutmodel->numverts;
	poutmodel->vertexBlends = ( unsigned int * )pmem;

	vblendindices_byte = poutmodel->blendIndices;
	vblendweights_byte = poutmodel->blendWeights;

	for( i = 0; i < poutmodel->numverts; i++ ) {
		mskblend_t blend;

		for( j = 0; j < SKM_MAX_WEIGHTS; j++ ) {
			blend.indices[j] = vblendindices_byte[j];
			blend.weights[j] = vblendweights_byte[j];
		}

		poutmodel->vertexBlends[i] = Mod_SkeletalModel_AddBlend( poutmodel, &blend );

		vblendindices_byte += SKM_MAX_WEIGHTS;
		vblendweights_byte += SKM_MAX_WEIGHTS;
	}

	// meshes
	memsize = 0;
	memsize += sizeof( mskmesh_t ) * header->num_meshes;
	memsize += sizeof( drawSurfaceSkeletal_t ) * header->num_meshes;
	pmem = Mod_Malloc( mod, memsize );

	poutmodel->nummeshes = header->num_meshes;
	poutmodel->meshes = ( mskmesh_t * )pmem; pmem += sizeof( *poutmodel->meshes ) * header->num_meshes;

	inmeshes = ( iqmmesh_t * )(pbase + header->ofs_meshes);
	for( i = 0; i < header->num_meshes; i++ ) {
		memcpy( &inmesh, &inmeshes[i], sizeof( iqmmesh_t ) );

		inmesh.name = LittleLong( inmesh.name );
		inmesh.material = LittleLong( inmesh.material );
		inmesh.first_vertex = LittleLong( inmesh.first_vertex );
		inmesh.num_vertexes = LittleLong( inmesh.num_vertexes );
		inmesh.first_triangle = LittleLong( inmesh.first_triangle );
		inmesh.num_triangles = LittleLong( inmesh.num_triangles );

		poutmodel->meshes[i].name = texts + inmesh.name;
		Mod_StripLODSuffix( poutmodel->meshes[i].name );

		poutmodel->meshes[i].skin.name = texts + inmesh.material;
		poutmodel->meshes[i].skin.shader = R_RegisterSkin( poutmodel->meshes[i].skin.name );

		poutmodel->meshes[i].elems = poutmodel->elems + inmesh.first_triangle * 3;
		poutmodel->meshes[i].numtris = inmesh.num_triangles;

		poutmodel->meshes[i].numverts = inmesh.num_vertexes;
		poutmodel->meshes[i].xyzArray = poutmodel->xyzArray + inmesh.first_vertex;
		poutmodel->meshes[i].normalsArray = poutmodel->normalsArray + inmesh.first_vertex;
		poutmodel->meshes[i].stArray = poutmodel->stArray + inmesh.first_vertex;
		poutmodel->meshes[i].sVectorsArray = poutmodel->sVectorsArray + inmesh.first_vertex;

		poutmodel->meshes[i].blendIndices = poutmodel->blendIndices + inmesh.first_vertex * SKM_MAX_WEIGHTS;
		poutmodel->meshes[i].blendWeights = poutmodel->blendWeights + inmesh.first_vertex * SKM_MAX_WEIGHTS;

		poutmodel->meshes[i].vertexBlends = poutmodel->vertexBlends + inmesh.first_vertex;

		// elements are always offset to start vertex 0 for each mesh
		outelems = poutmodel->meshes[i].elems;
		for( j = 0; j < poutmodel->meshes[i].numtris; j++ ) {
			outelems[0] -= inmesh.first_vertex;
			outelems[1] -= inmesh.first_vertex;
			outelems[2] -= inmesh.first_vertex;
			outelems += 3;
		}

		poutmodel->meshes[i].maxWeights = 1;

		vblendweights_byte = poutmodel->meshes[i].blendWeights;
		for( j = 0; j < poutmodel->meshes[i].numverts; j++ ) {
			for( k = 1; k < SKM_MAX_WEIGHTS && vblendweights_byte[k]; k++ );

			if( k > poutmodel->meshes[i].maxWeights ) {
				poutmodel->meshes[i].maxWeights = k;
				if( k == SKM_MAX_WEIGHTS ) {
					break;
				}
			}
			vblendweights_byte += SKM_MAX_WEIGHTS;
		}
	}

	// creating a VBO only makes sense if GLSL is present and the number of bones
	// we can handle on the GPU is sufficient
	// (created after the skins because skin loading may wait for GL commands to finish)
	if( poutmodel->numbones <= glConfig.maxGLSLBones ) {
		for( i = 0; i < header->num_meshes; i++ ) {
			// build a static vertex buffer object for this mesh
			Mod_SkeletalBuildStaticVBOForMesh( &poutmodel->meshes[i] );
		}
	}

	poutmodel->drawSurfs = ( drawSurfaceSkeletal_t * )pmem; pmem += sizeof( *poutmodel->drawSurfs ) * header->num_meshes;
	for( i = 0; i < header->num_meshes; i++ ) {
		poutmodel->drawSurfs[i].type = ST_SKELETAL;
		poutmodel->drawSurfs[i].model = mod;
		poutmodel->drawSurfs[i].mesh = poutmodel->meshes + i;
	}

	// bounds
	ClearBounds( mod->mins, mod->maxs );

	inbounds = ( iqmbounds_t * )(pbase + header->ofs_bounds);
	for( i = 0; i < header->num_frames; i++ ) {
		memcpy( &inbound, &inbounds[i], sizeof( iqmbounds_t ) );

		for( j = 0; j < 3; j++ ) {
			inbound.bbmin[j] = LittleFloat( inbound.bbmin[j] );
			inbound.bbmax[j] = LittleFloat( inbound.bbmax[j] );
		}
		inbound.radius = LittleFloat( inbound.radius );
		inbound.xyradius = LittleFloat( inbound.xyradius );

		VectorCopy( inbound.bbmin, poutmodel->frames[i].mins );
		VectorCopy( inbound.bbmax, poutmodel->frames[i].maxs );
		poutmodel->frames[i].radius = inbound.radius;

		AddPointToBounds( poutmodel->frames[i].mins, mod->mins, mod->maxs );
		AddPointToBounds( poutmodel->frames[i].maxs, mod->mins, mod->maxs );
	}

	mod->radius = RadiusFromBounds( mod->mins, mod->maxs );
	mod->type = mod_skeletal;
	mod->registrationSequence = rsh.registrationSequence;
	mod->touch = &Mod_TouchSkeletalModel;

	R_Free( baseposes );
	return;

error:
	if( baseposes ) {
		R_Free( baseposes );
	}
	mod->type = mod_bad;
}

/*
* R_SkeletalGetNumBones
*/
int R_SkeletalGetNumBones( const model_t *mod, int *numFrames )
{
	mskmodel_t *skmodel;

	if( !mod || mod->type != mod_skeletal )
		return 0;

	skmodel = ( mskmodel_t * )mod->extradata;
	if( numFrames )
		*numFrames = skmodel->numframes;
	return skmodel->numbones;
}

/*
* R_SkeletalGetBoneInfo
*/
int R_SkeletalGetBoneInfo( const model_t *mod, int bonenum, char *name, size_t name_size, int *flags )
{
	const mskbone_t *bone;
	const mskmodel_t *skmodel;

	if( !mod || mod->type != mod_skeletal )
		return 0;

	skmodel = ( mskmodel_t * )mod->extradata;
	if( (unsigned int)bonenum >= (int)skmodel->numbones )
		ri.Com_Error( ERR_DROP, "R_SkeletalGetBone: bad bone number" );

	bone = &skmodel->bones[bonenum];
	if( name && name_size )
		Q_strncpyz( name, bone->name, name_size );
	if( flags )
		*flags = bone->flags;
	return bone->parent;
}

/*
* R_SkeletalGetBonePose
*/
void R_SkeletalGetBonePose( const model_t *mod, int bonenum, int frame, bonepose_t *bonepose )
{
	const mskmodel_t *skmodel;

	if( !mod || mod->type != mod_skeletal )
		return;

	skmodel = ( mskmodel_t * )mod->extradata;
	if( bonenum < 0 || bonenum >= (int)skmodel->numbones )
		ri.Com_Error( ERR_DROP, "R_SkeletalGetBonePose: bad bone number" );
	if( frame < 0 || frame >= (int)skmodel->numframes )
		ri.Com_Error( ERR_DROP, "R_SkeletalGetBonePose: bad frame number" );

	if( bonepose )
		*bonepose = skmodel->frames[frame].boneposes[bonenum];
}

/*
* R_SkeletalModelLOD
*/
static model_t *R_SkeletalModelLOD( const entity_t *e )
{
	int lod;

	if( !e->model->numlods || ( e->flags & RF_FORCENOLOD ) )
		return e->model;

	lod = R_LODForSphere( e->origin, e->model->radius );

	if( lod < 1 )
		return e->model;
	return e->model->lods[min( lod, e->model->numlods )-1];
}

/*
* R_SkeletalModelLerpBBox
*/
static float R_SkeletalModelLerpBBox( const entity_t *e, const model_t *mod, vec3_t mins, vec3_t maxs )
{
	int i;
	int frame = e->frame, oldframe = e->oldframe;
	mskframe_t *pframe, *poldframe;
	float *thismins, *oldmins, *thismaxs, *oldmaxs;
	mskmodel_t *skmodel = ( mskmodel_t * )mod->extradata;

	if( !skmodel->nummeshes )
	{
		ClearBounds( mins, maxs );
		return 0;
	}

	if( frame < 0 || frame >= (int)skmodel->numframes )
	{
#ifndef PUBLIC_BUILD
		ri.Com_DPrintf( "R_SkeletalModelLerpBBox %s: no such frame %i\n", mod->name, frame );
#endif
		frame = 0;
	}
	if( oldframe < 0 || oldframe >= (int)skmodel->numframes )
	{
#ifndef PUBLIC_BUILD
		ri.Com_DPrintf( "R_SkeletalModelLerpBBox %s: no such oldframe %i\n", mod->name, oldframe );
#endif
		oldframe = 0;
	}

	frame = oldframe = 0;

	pframe = skmodel->frames + frame;
	poldframe = skmodel->frames + oldframe;

	// compute axially aligned mins and maxs
	if( pframe == poldframe )
	{
		VectorCopy( pframe->mins, mins );
		VectorCopy( pframe->maxs, maxs );
		if( e->scale == 1 ) {
			return pframe->radius;
		}
	}
	else
	{
		thismins = pframe->mins;
		thismaxs = pframe->maxs;

		oldmins = poldframe->mins;
		oldmaxs = poldframe->maxs;

		for( i = 0; i < 3; i++ )
		{
			mins[i] = min( thismins[i], oldmins[i] );
			maxs[i] = max( thismaxs[i], oldmaxs[i] );
		}
	}

	VectorScale( mins, e->scale, mins );
	VectorScale( maxs, e->scale, maxs );
	return RadiusFromBounds( mins, maxs );
}

//=======================================================================

typedef struct skmcacheentry_s
{
	size_t size;
	uint8_t *data;
	struct skmcacheentry_s *next;
} skmcacheentry_t;

mempool_t *r_skmcachepool;

static skmcacheentry_t *r_skmcache_head;	// actual entries are linked to this
static skmcacheentry_t *r_skmcache_free;	// actual entries are linked to this
static skmcacheentry_t *r_skmcachekeys[MAX_REF_ENTITIES*(MOD_MAX_LODS+1)];		// entities linked to cache entries

#define R_SKMCacheAlloc(size) R_MallocExt(r_skmcachepool, (size), 16, 1)

/*
* R_InitSkeletalCache
*/
void R_InitSkeletalCache( void )
{
	r_skmcachepool = R_AllocPool( r_mempool, "SKM Cache" );

	r_skmcache_head = NULL;
	r_skmcache_free = NULL;
}

/*
* R_GetSkeletalCache
*/
static uint8_t *R_GetSkeletalCache( int entNum, int lodNum )
{
	skmcacheentry_t *cache;
	
	cache = r_skmcachekeys[entNum*(MOD_MAX_LODS+1) + lodNum];
	if( !cache ) {
		return NULL;
	}
	return cache->data;
}

/*
* R_AllocSkeletalDataCache
* 
* Allocates or reuses a memory chunk and links it to entity+LOD num pair. The chunk
* is then linked to other chunks allocated in the same frame. At the end of the frame
* all of the entries in the "allocation" list are moved to the "free" list, to be reused in the 
* later function calls.
*/
static uint8_t *R_AllocSkeletalDataCache( int entNum, int lodNum, size_t size )
{
	size_t best_size;
	skmcacheentry_t *cache, *prev;
	skmcacheentry_t *best_prev, *best;

	best = NULL;
	best_prev = NULL;
	best_size = 0;

	assert( !r_skmcachekeys[entNum * (MOD_MAX_LODS+1) + lodNum] );

	// scan the list of free cache entries to see if there's a suitable candidate
	prev = NULL;
	cache = r_skmcache_free;
	while( cache ) {
		if( cache->size >= size ) {
			// keep track of the cache entry with the minimal overhead
			if( !best || cache->size < best_size ) {
				best_size = cache->size;
				best = cache;
				best_prev = prev;
			}
		}

		// return early if we find a perfect fit
		if( cache->size == size ) {
			break;
		}

		prev = cache;
		cache = cache->next;
	}

	// no suitable entries found, allocate
	if( !best ) {
		best = R_SKMCacheAlloc( sizeof( *best ) );
		best->data = R_SKMCacheAlloc( size );
		best->size = size;
		best_prev = NULL;
	}

	assert( best->size >= size );

	// unlink this cache entry from the current list
	if( best_prev ) {
		best_prev->next = best->next;
	}
	if( best == r_skmcache_free ) {
		r_skmcache_free = best->next;
	}

	// and link it to the allocation list
	best->next = r_skmcache_head;
	r_skmcache_head = best;
	r_skmcachekeys[entNum * (MOD_MAX_LODS+1) + lodNum] = best;

	return best->data;
}

/*
* R_ClearSkeletalCache
* 
* Remove entries from the "allocation" list to the "free" list.
* FIXME: this can probably be optimized a bit better.
*/
void R_ClearSkeletalCache( void )
{
	skmcacheentry_t *next, *cache;

	cache = r_skmcache_head;
	while( cache ) {
		next = cache->next;

		cache->next = r_skmcache_free;
		r_skmcache_free = cache;

		cache = next;
	}
	r_skmcache_head = NULL;

	memset( r_skmcachekeys, 0, sizeof( r_skmcachekeys ) );
}

/*
* R_ShutdownSkeletalCache
*/
void R_ShutdownSkeletalCache( void )
{
	if( !r_skmcachepool )
		return;

	R_FreePool( &r_skmcachepool );

	r_skmcache_head = NULL;
	r_skmcache_free = NULL;
}

//=======================================================================

// set the FP precision to fast
#if defined ( _WIN32 ) && ( _MSC_VER >= 1400 ) && defined( NDEBUG )
# pragma float_control(except, off, push)
# pragma float_control(precise, off, push)
# pragma fp_contract(on)		// this line is needed on Itanium processors
#endif

/*
* R_SkeletalBlendPoses
*/
static void R_SkeletalBlendPoses( unsigned int numblends, mskblend_t *blends, unsigned int numbones, mat4_t *relbonepose )
{
	unsigned int i, j, k;
	float *pose;
	mskblend_t *blend;

	for( i = 0, j = numbones, blend = blends; i < numblends; i++, j++, blend++ ) {
		float *b, f;

		pose = relbonepose[j];

		b = relbonepose[blend->indices[0]];
		f = blend->weights[0] * (1.0 / 255.0);

		pose[ 0] = f * b[ 0]; pose[ 1] = f * b[ 1]; pose[ 2] = f * b[ 2];
		pose[ 4] = f * b[ 4]; pose[ 5] = f * b[ 5]; pose[ 6] = f * b[ 6];
		pose[ 8] = f * b[ 8]; pose[ 9] = f * b[ 9]; pose[10] = f * b[10];
		pose[12] = f * b[12]; pose[13] = f * b[13]; pose[14] = f * b[14];

		for( k = 1; k < SKM_MAX_WEIGHTS && blend->weights[k]; k++ ) {
			b = relbonepose[blend->indices[k]];
			f = blend->weights[k] * (1.0 / 255.0);

			pose[ 0] += f * b[ 0]; pose[ 1] += f * b[ 1]; pose[ 2] += f * b[ 2];
			pose[ 4] += f * b[ 4]; pose[ 5] += f * b[ 5]; pose[ 6] += f * b[ 6];
			pose[ 8] += f * b[ 8]; pose[ 9] += f * b[ 9]; pose[10] += f * b[10];
			pose[12] += f * b[12]; pose[13] += f * b[13]; pose[14] += f * b[14];
		}
	}
}

/*
* R_SkeletalTransformVerts
*/
static void R_SkeletalTransformVerts( int numverts, const unsigned int *blends, mat4_t *relbonepose, const vec_t *v, vec_t *ov )
{
	const float *pose;

	for( ; numverts; numverts--, v += 4, ov += 4, blends++ ) {
		pose = relbonepose[*blends];

		ov[0] = v[0] * pose[0] + v[1] * pose[4] + v[2] * pose[ 8] + pose[12];
		ov[1] = v[0] * pose[1] + v[1] * pose[5] + v[2] * pose[ 9] + pose[13];
		ov[2] = v[0] * pose[2] + v[1] * pose[6] + v[2] * pose[10] + pose[14];
		ov[3] = 1;
	}
}

/*
* R_SkeletalTransformNormals
*/
static void R_SkeletalTransformNormals( int numverts, const unsigned int *blends, mat4_t *relbonepose, const vec_t *v, vec_t *ov )
{
	const float *pose;

	for( ; numverts; numverts--, v += 4, ov += 4, blends++ ) {
		pose = relbonepose[*blends];

		ov[0] = v[0] * pose[0] + v[1] * pose[4] + v[2] * pose[ 8];
		ov[1] = v[0] * pose[1] + v[1] * pose[5] + v[2] * pose[ 9];
		ov[2] = v[0] * pose[2] + v[1] * pose[6] + v[2] * pose[10];
		ov[3] = 0;
	}
}

/*
* R_SkeletalTransformNormalsAndSVecs
*/
static void R_SkeletalTransformNormalsAndSVecs( int numverts, const unsigned int *blends, mat4_t *relbonepose, const vec_t *v, vec_t *ov, const vec_t *sv, vec_t *osv )
{
	const float *pose;

	for( ; numverts; numverts--, v += 4, ov += 4, sv += 4, osv += 4, blends++ ) {
		pose = relbonepose[*blends];

		ov[0] = v[0] * pose[0] + v[1] * pose[4] + v[2] * pose[ 8];
		ov[1] = v[0] * pose[1] + v[1] * pose[5] + v[2] * pose[ 9];
		ov[2] = v[0] * pose[2] + v[1] * pose[6] + v[2] * pose[10];
		ov[3] = 0;

		osv[0] = sv[0] * pose[0] + sv[1] * pose[4] + sv[2] * pose[ 8];
		osv[1] = sv[0] * pose[1] + sv[1] * pose[5] + sv[2] * pose[ 9];
		osv[2] = sv[0] * pose[2] + sv[1] * pose[6] + sv[2] * pose[10];
		osv[3] = sv[3];
	}
}

// set the FP precision back to whatever value it was
#if defined ( _WIN32 ) && ( _MSC_VER >= 1400 ) && defined( NDEBUG )
# pragma float_control(pop)
# pragma float_control(pop)
# pragma fp_contract(off)	// this line is needed on Itanium processors
#endif

//=======================================================================

/*
* R_DrawSkeletalSurf
*/
void R_DrawSkeletalSurf( const entity_t *e, const shader_t *shader, const mfog_t *fog, const portalSurface_t *portalSurface, unsigned int shadowBits, drawSurfaceSkeletal_t *drawSurf )
{
	unsigned int i, j;
	int framenum = e->frame;
	int oldframenum = e->oldframe;
	float backlerp = e->backlerp;
	float frontlerp = 1.0 - backlerp;
	bonepose_t tempbonepose[256];
	const bonepose_t *bp, *oldbp, *bonepose, *oldbonepose, *lerpedbonepose;
	bonepose_t *out, tp;
	mskbone_t *bone;
	mat4_t *bonePoseRelativeMat;
	dualquat_t *bonePoseRelativeDQ;
	size_t bonePoseRelativeMatSize, bonePoseRelativeDQSize;
	const model_t *mod = drawSurf->model;
	const mskmodel_t *skmodel = ( const mskmodel_t * )mod->extradata;
	const mskmesh_t *skmesh = drawSurf->mesh;
	bool hardwareTransform = skmesh->vbo != NULL && glConfig.maxGLSLBones > 0 ? true : false;
	vattribmask_t vattribs;

	bonePoseRelativeMat = NULL;
	bonePoseRelativeDQ = NULL;

	bp = e->boneposes;
	oldbp = e->oldboneposes;

	// not sure if it's really needed
	if( bp == skmodel->frames[0].boneposes )
	{
		bp = NULL;
		framenum = oldframenum = 0;
	}

	// choose boneposes for lerping
	if( bp )
	{
		if( !oldbp )
			oldbp = bp;
	}
	else
	{
		if( ( framenum >= (int)skmodel->numframes ) || ( framenum < 0 ) )
		{
#ifndef PUBLIC_BUILD
			ri.Com_DPrintf( "R_DrawBonesFrameLerp %s: no such frame %d\n", mod->name, framenum );
#endif
			framenum = 0;
		}
		if( ( oldframenum >= (int)skmodel->numframes ) || ( oldframenum < 0 ) )
		{
#ifndef PUBLIC_BUILD
			ri.Com_DPrintf( "R_DrawBonesFrameLerp %s: no such oldframe %d\n", mod->name, oldframenum );
#endif
			oldframenum = 0;
		}

		bp = skmodel->frames[framenum].boneposes;
		oldbp = skmodel->frames[oldframenum].boneposes;
	}

	if( bp == oldbp && !framenum && skmesh->vbo != NULL ) {
		// fastpath: render static frame 0 as is
		RB_BindVBO( skmesh->vbo->index, GL_TRIANGLES );

		RB_DrawElements( 0, skmesh->numverts, 0, skmesh->numtris * 3, 
			0, skmesh->numverts, 0, skmesh->numtris * 3 );

		return;
	}

	// see what vertex attribs backend needs
	vattribs = RB_GetVertexAttribs();

	// cache size
	bonePoseRelativeMatSize = sizeof( mat4_t ) * (skmodel->numbones + skmodel->numblends);
	bonePoseRelativeDQSize = sizeof( dualquat_t ) * skmodel->numbones;

	// fetch bones tranforms from cache (both matrices and dual quaternions)
	bonePoseRelativeDQ = ( dualquat_t * )R_GetSkeletalCache( R_ENT2NUM( e ), mod->lodnum );
	if( bonePoseRelativeDQ ) {
		bonePoseRelativeMat = ( mat4_t * )(( uint8_t * )bonePoseRelativeDQ + bonePoseRelativeDQSize);
	}
	else {
		// lerp boneposes and store results in cache

		lerpedbonepose = tempbonepose;
		if( bp == oldbp || frontlerp == 1 )
		{
			if( e->boneposes )
			{
				// assume that parent transforms have already been applied
				lerpedbonepose = bp;
			}
			else
			{
				for( i = 0; i < skmodel->numbones; i++ )
				{
					j = i;
					out = tempbonepose + j;
					bonepose = bp + j;
					bone = skmodel->bones + j;

					if( bone->parent >= 0 ) {
						DualQuat_Multiply( tempbonepose[bone->parent].dualquat, bonepose->dualquat, out->dualquat );
					}
					else {
						DualQuat_Copy( bonepose->dualquat, out->dualquat );
					}
				}
			}
		}
		else
		{
			if( e->boneposes )
			{
				// lerp, assume that parent transforms have already been applied
				for( i = 0, out = tempbonepose, bonepose = bp, oldbonepose = oldbp, bone = skmodel->bones; i < skmodel->numbones; i++, out++, bonepose++, oldbonepose++, bone++ )
				{
					DualQuat_Lerp( oldbonepose->dualquat, bonepose->dualquat, frontlerp, out->dualquat );
				}
			}
			else
			{
				// lerp and transform
				for( i = 0; i < skmodel->numbones; i++ )
				{
					j = i;
					out = tempbonepose + j;
					bonepose = bp + j;
					oldbonepose = oldbp + j;
					bone = skmodel->bones + j;

					DualQuat_Lerp( oldbonepose->dualquat, bonepose->dualquat, frontlerp, out->dualquat );

					if( bone->parent >= 0 ) {
						DualQuat_Copy( out->dualquat, tp.dualquat );
						DualQuat_Multiply( tempbonepose[bone->parent].dualquat, tp.dualquat, out->dualquat );
					}
				}
			}
		}

		bonePoseRelativeDQ = ( dualquat_t * )R_AllocSkeletalDataCache( R_ENT2NUM( e ), mod->lodnum, 
			bonePoseRelativeDQSize + bonePoseRelativeMatSize );

		// generate dual quaternions for all bones
		for( i = 0; i < skmodel->numbones; i++ ) {
			DualQuat_Multiply( lerpedbonepose[i].dualquat, skmodel->invbaseposes[i].dualquat, bonePoseRelativeDQ[i] );
			DualQuat_Normalize( bonePoseRelativeDQ[i] );
		}

		// CPU transforms
		if( !hardwareTransform ) {
			bonePoseRelativeMat = ( mat4_t * )(( uint8_t * )bonePoseRelativeDQ + bonePoseRelativeDQSize);

			// generate matrices for all bones
			for( i = 0; i < skmodel->numbones; i++ ) {
				Matrix4_FromDualQuaternion( bonePoseRelativeDQ[i], bonePoseRelativeMat[i] );
			}

			// generate matrices for all blend combinations
			R_SkeletalBlendPoses( skmodel->numblends, skmodel->blends, skmodel->numbones, bonePoseRelativeMat );
		}
	}

	if( hardwareTransform )
	{
		RB_BindVBO( skmesh->vbo->index, GL_TRIANGLES );
		RB_SetBonesData( skmodel->numbones, bonePoseRelativeDQ, skmesh->maxWeights );
		RB_DrawElements( 0, skmesh->numverts, 0, skmesh->numtris * 3, 
			0, skmesh->numverts, 0, skmesh->numtris * 3 );
	}
	else
	{
		mesh_t dynamicMesh;

		memset( &dynamicMesh, 0, sizeof( dynamicMesh ) );

		dynamicMesh.elems = skmesh->elems;
		dynamicMesh.numElems = skmesh->numtris * 3;
		dynamicMesh.numVerts = skmesh->numverts;

		R_GetTransformBufferForMesh( &dynamicMesh, true,
			( vattribs & ( VATTRIB_NORMAL_BIT|VATTRIB_SVECTOR_BIT ) ) ? true : false,
			( vattribs & VATTRIB_SVECTOR_BIT ) ? true : false );

		R_SkeletalTransformVerts( skmesh->numverts, skmesh->vertexBlends, bonePoseRelativeMat,
			( vec_t * )skmesh->xyzArray[0], ( vec_t * )( dynamicMesh.xyzArray ) );

		if( vattribs & VATTRIB_SVECTOR_BIT ) {
			R_SkeletalTransformNormalsAndSVecs( skmesh->numverts, skmesh->vertexBlends, bonePoseRelativeMat,
				( vec_t * )skmesh->normalsArray[0], ( vec_t * )( dynamicMesh.normalsArray ),
				( vec_t * )skmesh->sVectorsArray[0], ( vec_t * )( dynamicMesh.sVectorsArray ) );
		} else if( vattribs & VATTRIB_NORMAL_BIT ) {
			R_SkeletalTransformNormals( skmesh->numverts, skmesh->vertexBlends, bonePoseRelativeMat,
				( vec_t * )skmesh->normalsArray[0], ( vec_t * )( dynamicMesh.normalsArray ) );
		}

		dynamicMesh.stArray = skmesh->stArray;

		RB_AddDynamicMesh( e, shader, fog, portalSurface, shadowBits, &dynamicMesh, GL_TRIANGLES, 0.0f, 0.0f );

		RB_FlushDynamicMeshes();
	}
}

/*
* R_SkeletalModelBBox
*/
float R_SkeletalModelBBox( const entity_t *e, vec3_t mins, vec3_t maxs )
{
	model_t	*mod;

	mod = R_SkeletalModelLOD( e );
	if( !mod )
		return 0;

	return R_SkeletalModelLerpBBox( e, mod, mins, maxs );
}

/*
* R_SkeletalModelFrameBounds
*/
void R_SkeletalModelFrameBounds( const model_t *mod, int frame, vec3_t mins, vec3_t maxs )
{
	mskframe_t *pframe;
	mskmodel_t *skmodel = ( mskmodel_t * )mod->extradata;

	if( !skmodel->nummeshes )
	{
		ClearBounds( mins, maxs );
		return;
	}

	if( ( frame >= (int)skmodel->numframes ) || ( frame < 0 ) )
	{
#ifndef PUBLIC_BUILD
		ri.Com_DPrintf( "R_SkeletalModelFrameBounds %s: no such frame %d\n", mod->name, frame );
#endif
		ClearBounds( mins, maxs );
		return;
	}

	pframe = skmodel->frames + frame;
	VectorCopy( pframe->mins, mins );
	VectorCopy( pframe->maxs, maxs );
}

/*
* R_AddSkeletalModelToDrawList
*/
bool R_AddSkeletalModelToDrawList( const entity_t *e )
{
	int i;
	const mfog_t *fog;
	const model_t *mod;
	const shader_t *shader;
	const mskmesh_t *mesh;
	const mskmodel_t *skmodel;
	vec3_t mins, maxs;
	float radius;
	float distance;
	int clipped;

	mod = R_SkeletalModelLOD( e );
	if( !( skmodel = ( ( mskmodel_t * )mod->extradata ) ) || !skmodel->nummeshes )
		return false;

	radius = R_SkeletalModelLerpBBox( e, mod, mins, maxs );
	clipped = R_CullModelEntity( e, mins, maxs, radius, true, true );
	if( clipped )
		return false;

	// never render weapon models or non-occluders into shadowmaps
	if( rn.renderFlags & RF_SHADOWMAPVIEW ) {
		if( e->renderfx & RF_WEAPONMODEL ) {
			return true;
		}
		if( rsc.entShadowGroups[R_ENT2NUM(e)] != rn.shadowGroup->id ) {
			return true;
		}
	}

	// make sure weapon model is always closest to the viewer
	if( e->renderfx & RF_WEAPONMODEL ) {
		distance = 0;
	}
	else {
		distance = Distance( e->origin, rn.viewOrigin ) + 1;
	}

	fog = R_FogForSphere( e->origin, radius );
#if 0
	if( !( e->flags & RF_WEAPONMODEL ) && fog )
	{
		R_SkeletalModelLerpBBox( e, mod );
		if( R_CompletelyFogged( fog, e->origin, skm_radius ) )
			return false;
	}
#endif

	for( i = 0, mesh = skmodel->meshes; i < (int)skmodel->nummeshes; i++, mesh++ )
	{
		shader = NULL;
		if( e->customSkin ) {
			shader = R_FindShaderForSkinFile( e->customSkin, mesh->name );
		} else if( e->customShader ) {
			shader = e->customShader;
		} else {
			shader = mesh->skin.shader;
		}

		if( shader ) {
			R_AddSurfToDrawList( rn.meshlist, e, fog, shader, distance, 0, NULL, skmodel->drawSurfs + i );
		}
	}

	return true;
}
