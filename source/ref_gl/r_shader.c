/*
Copyright (C) 1999 Stephen C. Taylor
Copyright (C) 2002-2007 Victor Luchits

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

// r_shader.c

#include "r_local.h"
#include "../qalgo/hash.h"

#define SHADERS_HASH_SIZE	128
#define SHADERCACHE_HASH_SIZE	128

typedef struct
{
	const char *keyword;
	void ( *func )( shader_t *shader, shaderpass_t *pass, const char **ptr );
} shaderkey_t;

typedef struct shadercache_s
{
	char *name;
	char *buffer;
	char *filename;
	size_t offset;
	struct shadercache_s *hash_next;
} shadercache_t;

static shader_t r_shaders[MAX_SHADERS];

static shader_t r_shaders_hash_headnode[SHADERS_HASH_SIZE], *r_free_shaders;
static shadercache_t *shadercache_hash[SHADERCACHE_HASH_SIZE];

static deformv_t r_currentDeforms[MAX_SHADER_DEFORMVS];
static shaderpass_t r_currentPasses[MAX_SHADER_PASSES];
static float r_currentRGBgenArgs[MAX_SHADER_PASSES][3], r_currentAlphagenArgs[MAX_SHADER_PASSES][2];
static tcmod_t r_currentTcmods[MAX_SHADER_PASSES][MAX_SHADER_TCMODS];
static vec4_t r_currentTcGen[MAX_SHADER_PASSES][2];

static bool	r_shaderNoMipMaps;
static bool	r_shaderNoPicMip;
static bool r_shaderNoFiltering;
static bool	r_shaderNoCompress;
static int r_shaderMinMipSize;
static bool r_shaderHasLightmapPass;
static bool r_shaderHasAutosprite;
static int		r_shaderAllDetail;
static char		r_shaderDeformvKey[1024];

static image_t	*r_defaultImage;

static char *r_shaderTemplateBuf;

static char *r_shortShaderName;
static size_t r_shortShaderNameSize;

static bool Shader_Parsetok( shader_t *shader, shaderpass_t *pass, const shaderkey_t *keys, const char *token, const char **ptr );
static void Shader_MakeCache( const char *filename );
static unsigned int Shader_GetCache( const char *name, shadercache_t **cache );
#define R_FreePassCinematics(pass) if( (pass)->cin ) { R_FreeCinematic( (pass)->cin ); (pass)->cin = 0; }

//===========================================================================

static char *Shader_ParseString( const char **ptr )
{
	char *token;

	if( !ptr || !( *ptr ) )
		return "";
	if( !**ptr || **ptr == '}' )
		return "";

	token = COM_ParseExt( ptr, false );
	return Q_strlwr( token );
}

static float Shader_ParseFloat( const char **ptr )
{
	if( !ptr || !( *ptr ) )
		return 0;
	if( !**ptr || **ptr == '}' )
		return 0;

	return atof( COM_ParseExt( ptr, false ) );
}

static float Shader_ParseInt( const char **ptr )
{
	if( !ptr || !( *ptr ) )
		return 0;
	if( !**ptr || **ptr == '}' )
		return 0;

	return atoi( COM_ParseExt( ptr, false ) );
}

static void Shader_ParseVector( const char **ptr, float *v, unsigned int size )
{
	unsigned int i;
	char *token;
	bool bracket;

	if( !size )
		return;
	if( size == 1 )
	{
		Shader_ParseFloat( ptr );
		return;
	}

	token = Shader_ParseString( ptr );
	if( !strcmp( token, "(" ) )
	{
		bracket = true;
		token = Shader_ParseString( ptr );
	}
	else if( token[0] == '(' )
	{
		bracket = true;
		token = &token[1];
	}
	else
	{
		bracket = false;
	}

	v[0] = atof( token );
	for( i = 1; i < size-1; i++ )
		v[i] = Shader_ParseFloat( ptr );

	token = Shader_ParseString( ptr );
	if( !token[0] )
	{
		v[i] = 0;
	}
	else if( token[strlen( token )-1] == ')' )
	{
		token[strlen( token )-1] = 0;
		v[i] = atof( token );
	}
	else
	{
		v[i] = atof( token );
		if( bracket )
			Shader_ParseString( ptr );
	}
}

static void Shader_SkipLine( const char **ptr )
{
	while( ptr )
	{
		const char *token = COM_ParseExt( ptr, false );
		if( !token[0] )
			return;
	}
}

static void Shader_SkipBlock( const char **ptr )
{
	const char *tok;
	int brace_count;

	// Opening brace
	tok = COM_ParseExt( ptr, true );
	if( tok[0] != '{' )
		return;

	for( brace_count = 1; brace_count > 0; )
	{
		tok = COM_ParseExt( ptr, true );
		if( !tok[0] )
			return;
		if( tok[0] == '{' )
			brace_count++;
		else if( tok[0] == '}' )
			brace_count--;
	}
}

#define MAX_CONDITIONS		8
typedef enum { COP_LS, COP_LE, COP_EQ, COP_GR, COP_GE, COP_NE } conOp_t;
typedef enum { COP2_AND, COP2_OR } conOp2_t;
typedef struct { int operand; conOp_t op; bool negative; int val; conOp2_t logic; } shaderCon_t;

char *conOpStrings[] = { "<", "<=", "==", ">", ">=", "!=", NULL };
char *conOpStrings2[] = { "&&", "||", NULL };

static bool Shader_ParseConditions( const char **ptr, shader_t *shader )
{
	int i;
	char *tok;
	int numConditions;
	shaderCon_t conditions[MAX_CONDITIONS];
	bool result = false, val = false, skip, expectingOperator;
//	static const int falseCondition = 0;

	numConditions = 0;
	memset( conditions, 0, sizeof( conditions ) );

	skip = false;
	expectingOperator = false;
	while( 1 )
	{
		tok = Shader_ParseString( ptr );
		if( !tok[0] )
		{
			if( expectingOperator )
				numConditions++;
			break;
		}
		if( skip )
			continue;

		for( i = 0; conOpStrings[i]; i++ )
		{
			if( !strcmp( tok, conOpStrings[i] ) )
				break;
		}

		if( conOpStrings[i] )
		{
			if( !expectingOperator )
			{
				Com_Printf( S_COLOR_YELLOW "WARNING: Bad syntax in condition (shader %s)\n", shader->name );
				skip = true;
			}
			else
			{
				conditions[numConditions].op = i;
				expectingOperator = false;
			}
			continue;
		}

		for( i = 0; conOpStrings2[i]; i++ )
		{
			if( !strcmp( tok, conOpStrings2[i] ) )
				break;
		}

		if( conOpStrings2[i] )
		{
			if( !expectingOperator )
			{
				Com_Printf( S_COLOR_YELLOW "WARNING: Bad syntax in condition (shader %s)\n", shader->name );
				skip = true;
			}
			else
			{
				conditions[numConditions++].logic = i;
				if( numConditions == MAX_CONDITIONS )
					skip = true;
				else
					expectingOperator = false;
			}
			continue;
		}

		if( expectingOperator )
		{
			Com_Printf( S_COLOR_YELLOW "WARNING: Bad syntax in condition (shader %s)\n", shader->name );
			skip = true;
			continue;
		}

		if( !strcmp( tok, "!" ) )
		{
			conditions[numConditions].negative = !conditions[numConditions].negative;
			continue;
		}

		if( !conditions[numConditions].operand )
		{
			if( !Q_stricmp( tok, "maxTextureSize" ) )
				conditions[numConditions].operand = ( int  )glConfig.maxTextureSize;
			else if( !Q_stricmp( tok, "maxTextureCubemapSize" ) )
				conditions[numConditions].operand = ( int )glConfig.maxTextureCubemapSize;
			else if( !Q_stricmp( tok, "maxTextureUnits" ) )
				conditions[numConditions].operand = ( int )glConfig.maxTextureUnits;
			else if( !Q_stricmp( tok, "textureCubeMap" ) )
				conditions[numConditions].operand = ( int )glConfig.ext.texture_cube_map;
			else if( !Q_stricmp( tok, "GLSL" ) )
				conditions[numConditions].operand = 1;
			else if( !Q_stricmp( tok, "deluxeMaps" ) || !Q_stricmp( tok, "deluxe" ) )
				conditions[numConditions].operand = ( int )mapConfig.deluxeMappingEnabled;
			else if( !Q_stricmp( tok, "portalMaps" ) )
				conditions[numConditions].operand = ( int )r_portalmaps->integer;
			else
			{
//				Com_Printf( S_COLOR_YELLOW "WARNING: Unknown expression '%s' in shader %s\n", tok, shader->name );
//				skip = true;
//				conditions[numConditions].operand = ( int )falseCondition;
				conditions[numConditions].operand = atoi( tok );
			}

			conditions[numConditions].operand++;
			if( conditions[numConditions].operand < 0 )
				conditions[numConditions].operand = 0;

			if( !skip )
			{
				conditions[numConditions].op = COP_NE;
				expectingOperator = true;
			}
			continue;
		}

		if( !strcmp( tok, "false" ) )
			conditions[numConditions].val = 0;
		else if( !strcmp( tok, "true" ) )
			conditions[numConditions].val = 1;
		else
			conditions[numConditions].val = atoi( tok );
		expectingOperator = true;
	}

	if( skip )
		return false;

	if( !conditions[0].operand )
	{
		Com_Printf( S_COLOR_YELLOW "WARNING: Empty 'if' statement in shader %s\n", shader->name );
		return false;
	}


	for( i = 0; i < numConditions; i++ )
	{
		conditions[i].operand--;

		switch( conditions[i].op )
		{
		case COP_LS:
			val = ( conditions[i].operand < conditions[i].val );
			break;
		case COP_LE:
			val = ( conditions[i].operand <= conditions[i].val );
			break;
		case COP_EQ:
			val = ( conditions[i].operand == conditions[i].val );
			break;
		case COP_GR:
			val = ( conditions[i].operand > conditions[i].val );
			break;
		case COP_GE:
			val = ( conditions[i].operand >= conditions[i].val );
			break;
		case COP_NE:
			val = ( conditions[i].operand != conditions[i].val );
			break;
		default:
			break;
		}

		if( conditions[i].negative )
			val = !val;
		if( i )
		{
			switch( conditions[i-1].logic )
			{
			case COP2_AND:
				result = result && val;
				break;
			case COP2_OR:
				result = result || val;
				break;
			}
		}
		else
		{
			result = val;
		}
	}

	return result;
}

static bool Shader_SkipConditionBlock( const char **ptr )
{
	const char *tok;
	int condition_count;

	for( condition_count = 1; condition_count > 0; )
	{
		tok = COM_ParseExt( ptr, true );
		if( !tok[0] )
			return false;
		if( !Q_stricmp( tok, "if" ) )
			condition_count++;
		else if( !Q_stricmp( tok, "endif" ) )
			condition_count--;
// Vic: commented out for now
//		else if( !Q_stricmp( tok, "else" ) && (condition_count == 1) )
//			return true;
	}

	return true;
}

//===========================================================================

static void Shader_ParseSkySides( const char **ptr, image_t **images, int imagetags, bool underscore )
{
	int i, j;
	char *token;
	bool noskybox = false;

	token = Shader_ParseString( ptr );
	if( token[0] == '-' )
	{
		noskybox = true;
	}
	else
	{
		struct cubemapSufAndFlip
		{
			char *suf; int flags;
		} cubemapSides[2][6] = {
			{
				{ "rt", 0 },
				{ "bk", 0 },
				{ "lf", 0 },
				{ "ft", 0 },
				{ "up", 0 },
				{ "dn", 0 }
			},
			{
				{ "px", IT_FLIPDIAGONAL },
				{ "py", IT_FLIPY },
				{ "nx", IT_FLIPX|IT_FLIPY|IT_FLIPDIAGONAL },
				{ "ny", IT_FLIPX },
				{ "pz", IT_FLIPDIAGONAL },
				{ "nz", IT_FLIPDIAGONAL }
			}
		};

		for( i = 0; i < 2; i++ )
		{
			char suffix[6];

			memset( images, 0, sizeof( *images ) * 6 );

			for( j = 0; j < 6; j++ )
			{
				if( underscore )
					Q_strncpyz( suffix, "_", sizeof( suffix ) );
				else
					suffix[0] = '\0';
				Q_strncatz( suffix, cubemapSides[i][j].suf, sizeof( suffix ) );

				images[j] = R_FindImage( token, suffix, IT_SKYFLAGS|cubemapSides[i][j].flags, 1, imagetags );
				if( !images[j] )
					break;
			}

			if( j == 6 )
				break;
		}

		if( i == 2 )
			noskybox = true;
	}

	if( noskybox )
		memset( images, 0, sizeof( *images ) * 6 );
}

/**
 * Parses sky sides with their names specified in the shader itself.
 * To reuse already specified images without writing their name multiple times,
 * rt, bk, lf, ft, up and dn shortcuts can be used.
 *
 * @param ptr       parameters to parse
 * @param images    array to write the images to
 * @param imagetags image usage tags
 */
static void Shader_ParseCustomSkySides( const char **ptr, image_t **images, int imagetags )
{
	int i, j;
	char *token;
	int refs[6] = { 0 };
	const char *refnames[] = {
		"rt",
		"bk",
		"lf",
		"ft",
		"up",
		"dn"
	};

	memset( images, 0, sizeof( *images ) * 6 );

	for( i = 0; i < 6; i++ )
	{
		token = Shader_ParseString( ptr );
		for( j = 0; j < 6; j++ )
		{
			if( !Q_stricmp( token, refnames[j] ) )
			{
				refs[i] = j + 1;
				break;
			}
		}

		if( !refs[i] )
			images[i] = R_FindImage( token, NULL, IT_SKYFLAGS, 1, imagetags );
	}

	for( i = 0; i < 6; i++ )
	{
		if( refs[i] )
			images[i] = images[refs[i] - 1];

		if( !images[i] )
		{
			memset( images, 0, sizeof( *images ) * 6 );
			break;
		}
	}
}

static void Shader_ParseFunc( const char **ptr, shaderfunc_t *func )
{
	char *token;

	token = Shader_ParseString( ptr );
	if( !strcmp( token, "sin" ) )
		func->type = SHADER_FUNC_SIN;
	else if( !strcmp( token, "triangle" ) )
		func->type = SHADER_FUNC_TRIANGLE;
	else if( !strcmp( token, "square" ) )
		func->type = SHADER_FUNC_SQUARE;
	else if( !strcmp( token, "sawtooth" ) )
		func->type = SHADER_FUNC_SAWTOOTH;
	else if( !strcmp( token, "inversesawtooth" ) )
		func->type = SHADER_FUNC_INVERSESAWTOOTH;
	else if( !strcmp( token, "noise" ) )
		func->type = SHADER_FUNC_NOISE;
	else if( !strcmp( token, "distanceramp" ) )
		func->type = SHADER_FUNC_RAMP;

	func->args[0] = Shader_ParseFloat( ptr );
	func->args[1] = Shader_ParseFloat( ptr );
	func->args[2] = Shader_ParseFloat( ptr );
	func->args[3] = Shader_ParseFloat( ptr );
}

//===========================================================================

static int Shader_SetImageFlags( shader_t *shader )
{
	int flags = 0;

	if( shader->flags & SHADER_SKY )
		flags |= IT_SKY;
	if( r_shaderNoMipMaps )
		flags |= IT_NOMIPMAP;
	if( r_shaderNoPicMip )
		flags |= IT_NOPICMIP;
	if( r_shaderNoCompress )
		flags |= IT_NOCOMPRESS;
	if( r_shaderNoFiltering )
		flags |= IT_NOFILTERING;
	if( shader->type == SHADER_TYPE_2D 
		|| shader->type == SHADER_TYPE_2D_RAW
		|| shader->type == SHADER_TYPE_VIDEO )
		flags |= IT_SYNC;
	//if( r_shaderHasAutosprite )
	//	flags |= IT_CLAMP;

	return flags;
}

static image_t *Shader_FindImage( shader_t *shader, const char *name, int flags )
{
	image_t *image;

	if( !Q_stricmp( name, "$whiteimage" ) || !Q_stricmp( name, "*white" ) )
		return rsh.whiteTexture;
	if( !Q_stricmp( name, "$blackimage" ) || !Q_stricmp( name, "*black" ) )
		return rsh.blackTexture;
	if( !Q_stricmp( name, "$greyimage" ) || !Q_stricmp( name, "*grey" ) )
		return rsh.greyTexture;
	if( !Q_stricmp( name, "$blankbumpimage" ) || !Q_stricmp( name, "*blankbump" ) )
		return rsh.blankBumpTexture;
	if( !Q_stricmp( name, "$particleimage" ) || !Q_stricmp( name, "*particle" ) )
		return rsh.particleTexture;
	if( !Q_strnicmp( name, "*lm", 3 ) )
	{
		ri.Com_DPrintf( S_COLOR_YELLOW "WARNING: shader %s has a stage with explicit lightmap image\n", shader->name );
		return rsh.whiteTexture;
	}

	image = R_FindImage( name, NULL, flags, r_shaderMinMipSize, shader->imagetags );
	if( !image )
	{
		ri.Com_DPrintf( S_COLOR_YELLOW "WARNING: shader %s has a stage with no image: %s\n", shader->name, name );
		return r_defaultImage;
	}

	return image;
}

/****************** shader keyword functions ************************/

static void Shader_Cull( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	char *token;

	shader->flags &= ~( SHADER_CULL_FRONT|SHADER_CULL_BACK );

	token = Shader_ParseString( ptr );
	if( !strcmp( token, "disable" ) || !strcmp( token, "none" ) || !strcmp( token, "twosided" ) )
		;
	else if( !strcmp( token, "back" ) || !strcmp( token, "backside" ) || !strcmp( token, "backsided" ) )
		shader->flags |= SHADER_CULL_BACK;
	else
		shader->flags |= SHADER_CULL_FRONT;
}

static void Shader_NoMipMaps( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	r_shaderNoMipMaps = r_shaderNoPicMip = true;
	r_shaderMinMipSize = 1;
}

static void Shader_NoPicMip( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	r_shaderNoPicMip = true;
}

static void Shader_NoCompress( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	r_shaderNoCompress = true;
}

static void Shader_NoFiltering( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	r_shaderNoFiltering = true;
	shader->flags |= SHADER_NO_TEX_FILTERING;
}

static void Shader_SmallestMipMapSize( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	int size = Shader_ParseInt( ptr );
	if( glConfig.ext.texture_lod && !r_shaderNoMipMaps )
		r_shaderMinMipSize = max( size, 1 );
}

static void Shader_DeformVertexes( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	char tmp[128];
	char *token;
	deformv_t *deformv;
	shaderfunc_t *func;

	if( shader->numdeforms == MAX_SHADER_DEFORMVS )
	{
		Com_Printf( S_COLOR_YELLOW "WARNING: shader %s has too many deforms\n", shader->name );
		Shader_SkipLine( ptr );
		return;
	}

	deformv = &r_currentDeforms[shader->numdeforms];
	func = &deformv->func;

	token = Shader_ParseString( ptr );
	Q_strncatz( r_shaderDeformvKey, token, sizeof( r_shaderDeformvKey ) );

	if( !strcmp( token, "wave" ) )
	{
		deformv->type = DEFORMV_WAVE;
		deformv->args[0] = Shader_ParseFloat( ptr );
		Shader_ParseFunc( ptr, func );
		Q_strncatz( r_shaderDeformvKey, 
			va_r( tmp, sizeof( tmp ), "%g%i%g%g%g%g", 
			deformv->args[0], func->type, func->args[0], func->args[1], func->args[2], func->args[3] ), 
			sizeof( r_shaderDeformvKey ) );
		deformv->args[0] = deformv->args[0] ? 1.0f / deformv->args[0] : 100.0f;
	}
	else if( !strcmp( token, "bulge" ) )
	{
		deformv->type = DEFORMV_BULGE;
		Shader_ParseVector( ptr, deformv->args, 4 );
		Q_strncatz( r_shaderDeformvKey, 
			va_r( tmp, sizeof( tmp ), "%g%g%g%g", 
			deformv->args[0], deformv->args[1], deformv->args[2], deformv->args[3] ), 
			sizeof( r_shaderDeformvKey ) );
	}
	else if( !strcmp( token, "move" ) )
	{
		deformv->type = DEFORMV_MOVE;
		Shader_ParseVector( ptr, deformv->args, 3 );
		Shader_ParseFunc( ptr, &deformv->func );
		Q_strncatz( r_shaderDeformvKey, 
			va_r( tmp, sizeof( tmp ), "%g%g%g%i%g%g%g%g", 
			deformv->args[0], deformv->args[1], deformv->args[2],
			func->type, func->args[0], func->args[1], func->args[2], func->args[3] ), 
			sizeof( r_shaderDeformvKey ) );
	}
	else if( !strcmp( token, "autosprite" ) )
	{
		deformv->type = DEFORMV_AUTOSPRITE;
		shader->flags |= SHADER_AUTOSPRITE;
		r_shaderHasAutosprite = true;
	}
	else if( !strcmp( token, "autosprite2" ) )
	{
		deformv->type = DEFORMV_AUTOSPRITE2;
		shader->flags |= SHADER_AUTOSPRITE;
	}
	else if( !strcmp( token, "autoparticle" ) ) {
		deformv->type = DEFORMV_AUTOPARTICLE;
		shader->flags |= SHADER_AUTOSPRITE;
	}
	else
	{
		Shader_SkipLine( ptr );
		return;
	}

	shader->numdeforms++;
}

static void Shader_SkyParmsExt( shader_t *shader, shaderpass_t *pass, const char **ptr, bool custom, bool underscore )
{
	float skyheight;

	if( custom )
		Shader_ParseCustomSkySides( ptr, shader->skyboxImages, shader->imagetags );
	else
		Shader_ParseSkySides( ptr, shader->skyboxImages, shader->imagetags, underscore );

	skyheight = Shader_ParseFloat( ptr );
	if( !skyheight )
		skyheight = 512.0f;

	shader->skyHeight = skyheight;
	shader->flags |= SHADER_SKY;
}

static void Shader_SkyParms( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	Shader_SkyParmsExt( shader, pass, ptr, false, true );
}

static void Shader_SkyParms2( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	Shader_SkyParmsExt( shader, pass, ptr, false, false );
}

static void Shader_SkyParmsSides( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	Shader_SkyParmsExt( shader, pass, ptr, true, false );
}

static void Shader_FogParms( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	vec3_t color, fcolor;

	Shader_ParseVector( ptr, color, 3 );
	ColorNormalize( color, fcolor );

	shader->fog_color[0] = ( int )( fcolor[0] * 255.0f );
	shader->fog_color[1] = ( int )( fcolor[1] * 255.0f );
	shader->fog_color[2] = ( int )( fcolor[2] * 255.0f );
	shader->fog_color[3] = 255;
	shader->fog_dist = Shader_ParseFloat( ptr );
	if( shader->fog_dist <= 0.1f )
		shader->fog_dist = 128.0f;

	shader->fog_clearDist = Shader_ParseFloat( ptr );
	if( shader->fog_clearDist > shader->fog_dist - 128 )
		shader->fog_clearDist = shader->fog_dist - 128;
	if( shader->fog_clearDist <= 0.0f )
		shader->fog_clearDist = 0;
}

static void Shader_Sort( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	char *token;

	token = Shader_ParseString( ptr );
	if( !strcmp( token, "portal" ) )
		shader->sort = SHADER_SORT_PORTAL;
	else if( !strcmp( token, "sky" ) )
		shader->sort = SHADER_SORT_SKY;
	else if( !strcmp( token, "opaque" ) )
		shader->sort = SHADER_SORT_OPAQUE;
	else if( !strcmp( token, "banner" ) )
		shader->sort = SHADER_SORT_BANNER;
	else if( !strcmp( token, "underwater" ) )
		shader->sort = SHADER_SORT_UNDERWATER;
	else if( !strcmp( token, "additive" ) )
		shader->sort = SHADER_SORT_ADDITIVE;
	else if( !strcmp( token, "nearest" ) )
		shader->sort = SHADER_SORT_NEAREST;
	else
	{
		shader->sort = atoi( token );
		if( shader->sort > SHADER_SORT_NEAREST )
			shader->sort = SHADER_SORT_NEAREST;
	}
}

static void Shader_Portal( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	shader->flags |= SHADER_PORTAL;
	shader->sort = SHADER_SORT_PORTAL;
}

static void Shader_PolygonOffset( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	shader->flags |= SHADER_POLYGONOFFSET;
}

static void Shader_StencilTest( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	shader->flags |= SHADER_STENCILTEST;
}

static void Shader_EntityMergable( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	shader->flags |= SHADER_ENTITY_MERGABLE;
}

static void Shader_If( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	if( !Shader_ParseConditions( ptr, shader ) )
	{
		if( !Shader_SkipConditionBlock( ptr ) )
			Com_Printf( S_COLOR_YELLOW "WARNING: Mismatched if/endif pair in shader %s\n", shader->name );
	}
}

static void Shader_Endif( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
}

static void Shader_OffsetMappingScale( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	shader->offsetmappingScale = Shader_ParseFloat( ptr );
	if( shader->offsetmappingScale <= 0 )
		shader->offsetmappingScale = 0;
}

static void Shader_GlossIntensity( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	shader->glossIntensity = Shader_ParseFloat( ptr );
	if( shader->glossIntensity <= 0 )
		shader->glossIntensity = 0;
}

static void Shader_GlossExponent( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	shader->glossExponent = Shader_ParseFloat( ptr );
	if( shader->glossExponent <= 0 )
		shader->glossExponent = 0;
}

#define MAX_SHADER_TEMPLATE_ARGS	12
static void Shader_Template( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	int i;
	char *tmpl, *buf, *out, *token;
	char *pos, *before, *ptr2;
	const char *ptr_backup;
	char backup;
	char args[MAX_SHADER_TEMPLATE_ARGS][MAX_QPATH];
	shadercache_t *cache;
	int num_args;
	size_t length;

	token = Shader_ParseString( ptr );
	if( !*token )
	{
		Com_Printf( S_COLOR_YELLOW "WARNING: missing template arguments in shader %s\n", shader->name );
		Shader_SkipLine( ptr );
		return;
	}

	// search for template in cache
	tmpl = token;
	Shader_GetCache( tmpl, &cache );
	if( !cache )
	{
		Com_Printf( S_COLOR_YELLOW "WARNING: shader template %s not found in cache\n", tmpl );
		Shader_SkipLine( ptr );
		return;
	}

	// aha, found it

	// find total length
	buf = cache->buffer + cache->offset;
	ptr2 = buf;
	Shader_SkipBlock( (const char **)&ptr2 );
	length = ptr2 - buf;

	// replace the following char with a EOF
	backup = cache->buffer[ptr2 - cache->buffer];
	cache->buffer[ptr2 - cache->buffer] = '\0';

	// now count occurences of each argument in a template
	ptr_backup = *ptr;
	for( i = 1, num_args = 0; ; i++ )
	{
		char arg[8];
		size_t arg_count;

		token = Shader_ParseString( ptr );
		if( !*token ) {
			break;
		}

		if( num_args == MAX_SHADER_TEMPLATE_ARGS ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: shader template %s has too many arguments\n", tmpl );
			break;
		}

		Q_snprintfz( arg, sizeof( arg ), "$%i", i );
		arg_count = Q_strcount( buf, arg );
		length += arg_count * strlen( token );

		Q_strncpyz( args[num_args], token, sizeof( args[0] ) );
		num_args++;
	}

	// (re)allocate string buffer, if needed
	if( !r_shaderTemplateBuf )
		r_shaderTemplateBuf = R_Malloc( length + 1 );
	else
		r_shaderTemplateBuf = R_Realloc( r_shaderTemplateBuf, length + 1 );

	// start with an empty string
	out = r_shaderTemplateBuf;
	memset( out, 0, length + 1 );

	// now replace all occurences of placeholders
	pos = before = buf;
	*ptr = ptr_backup;
	while( (pos = strstr( pos, "$" )) != NULL )
	{
		int i, arg;

		// calculate the placeholder index
		for( i = 1, arg = 0; ; i++ )
		{
			if( *(pos+i) >= '1' && *(pos+i) <= '9' )
			{
				arg = arg * 10 + (int)(*(pos+i) - '0');
				continue;
			}
			break;
		}

		if( arg && arg <= num_args )
		{
			token = args[arg-1];

			// hack in EOF, then concat
			*pos = '\0';
			strcat( out, before );
			strcat( out, token );
			*pos = '$';

			pos += i;
			before = pos;
		}
		else
		{
			// treat a single '$' as a regular character
			pos += i;
		}
	}

	strcat( out, before );

	// skip the initial { and change the original pointer
	*ptr = r_shaderTemplateBuf;
	COM_ParseExt( ptr, true );

	// restore backup char
	cache->buffer[ptr2 - cache->buffer] = backup;
}

static void Shader_Skip( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	Shader_SkipLine( ptr );
}

static void Shader_SoftParticle( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	shader->flags |= SHADER_SOFT_PARTICLE;
}

static void Shader_ForceWorldOutlines( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	shader->flags |= SHADER_FORCE_OUTLINE_WORLD;
}

static const shaderkey_t shaderkeys[] =
{
	{ "cull", Shader_Cull },
	{ "skyparms", Shader_SkyParms },
	{ "skyparms2", Shader_SkyParms2 },
	{ "skyparmssides", Shader_SkyParmsSides },
	{ "fogparms", Shader_FogParms },
	{ "nomipmaps", Shader_NoMipMaps },
	{ "nopicmip", Shader_NoPicMip },
	{ "nocompress",	Shader_NoCompress },
	{ "nofiltering", Shader_NoFiltering },
	{ "smallestmipmapsize", Shader_SmallestMipMapSize },
	{ "polygonoffset", Shader_PolygonOffset },
	{ "stenciltest", Shader_StencilTest },
	{ "sort", Shader_Sort },
	{ "deformvertexes", Shader_DeformVertexes },
	{ "portal", Shader_Portal },
	{ "entitymergable", Shader_EntityMergable },
	{ "if",	Shader_If },
	{ "endif", Shader_Endif },
	{ "offsetmappingscale", Shader_OffsetMappingScale },
	{ "glossexponent", Shader_GlossExponent },
	{ "glossintensity", Shader_GlossIntensity },
	{ "template", Shader_Template },
	{ "skip", Shader_Skip },
	{ "softparticle", Shader_SoftParticle },
	{ "forceworldoutlines", Shader_ForceWorldOutlines },

	{ NULL,	NULL }
};

// ===============================================================

static void Shaderpass_LoadMaterial( image_t **normalmap, image_t **glossmap, image_t **decalmap, const char *name, int addFlags, int imagetags )
{
	image_t *images[3];

	// set defaults
	images[0] = images[1] = images[2] = NULL;

	// load normalmap image
	images[0] = R_FindImage( name, "_norm", (addFlags|IT_NORMALMAP), r_shaderMinMipSize, imagetags );

	// load glossmap image
	if( r_lighting_specular->integer ) {
		images[1] = R_FindImage( name, "_gloss", addFlags, r_shaderMinMipSize, imagetags );
	}

	images[2] = R_FindImage( name, "_decal", addFlags, r_shaderMinMipSize, imagetags );
	if( !images[2] ) {
		images[2] = R_FindImage( name, "_add", addFlags, r_shaderMinMipSize, imagetags );
	}

	*normalmap = images[0];
	*glossmap = images[1];
	*decalmap = images[2];
}

static void Shaderpass_MapExt( shader_t *shader, shaderpass_t *pass, int addFlags, const char **ptr )
{
	int flags;
	char *token;

	R_FreePassCinematics( pass );

	token = Shader_ParseString( ptr );
	if( token[0] == '$' )
	{
		token++;
		if( !strcmp( token, "lightmap" ) )
		{
			r_shaderHasLightmapPass = true;

			pass->tcgen = TC_GEN_LIGHTMAP;
			pass->flags = ( pass->flags & ~( SHADERPASS_PORTALMAP ) ) | SHADERPASS_LIGHTMAP;
			pass->anim_fps = 0;
			pass->images[0] = NULL;
			return;
		}
		else if( !strcmp( token, "portalmap" ) || !strcmp( token, "mirrormap" ) )
		{
			pass->tcgen = TC_GEN_PROJECTION;
			pass->flags = ( pass->flags & ~( SHADERPASS_LIGHTMAP ) ) | SHADERPASS_PORTALMAP;
			pass->anim_fps = 0;
			pass->images[0] = NULL;
			if( ( shader->flags & SHADER_PORTAL ) && ( shader->sort == SHADER_SORT_PORTAL ) )
				shader->sort = 0; // reset sorting so we can figure it out later. FIXME?
			shader->flags |= SHADER_PORTAL|( r_portalmaps->integer ? SHADER_PORTAL_CAPTURE : 0 );
			return;
		}
		else
		{
			token--;
		}
	}

	flags = Shader_SetImageFlags( shader ) | addFlags;
	pass->tcgen = TC_GEN_BASE;
	pass->flags &= ~( SHADERPASS_LIGHTMAP|SHADERPASS_PORTALMAP );
	pass->anim_fps = 0;
	pass->images[0] = Shader_FindImage( shader, token, flags );
}

static void Shaderpass_AnimMapExt( shader_t *shader, shaderpass_t *pass, int addFlags, const char **ptr )
{
	int flags;
	char *token;

	R_FreePassCinematics( pass );

	flags = Shader_SetImageFlags( shader ) | addFlags;

	pass->tcgen = TC_GEN_BASE;
	pass->flags &= ~( SHADERPASS_LIGHTMAP|SHADERPASS_PORTALMAP );
	pass->anim_fps = Shader_ParseFloat( ptr );
	pass->anim_numframes = 0;

	for(;; )
	{
		token = Shader_ParseString( ptr );
		if( !token[0] )
			break;
		if( pass->anim_numframes < MAX_SHADER_IMAGES )
			pass->images[pass->anim_numframes++] = Shader_FindImage( shader, token, flags );
	}

	if( pass->anim_numframes == 0 )
		pass->anim_fps = 0;
}

static void Shaderpass_CubeMapExt( shader_t *shader, shaderpass_t *pass, int addFlags, int tcgen, const char **ptr )
{
	int flags;
	char *token;

	R_FreePassCinematics( pass );

	token = Shader_ParseString( ptr );
	flags = Shader_SetImageFlags( shader ) | addFlags;
	pass->anim_fps = 0;
	pass->flags &= ~( SHADERPASS_LIGHTMAP|SHADERPASS_PORTALMAP );

	pass->images[0] = R_FindImage( token, NULL, flags|IT_CUBEMAP, r_shaderMinMipSize, shader->imagetags );
	if( pass->images[0] )
	{
		pass->tcgen = tcgen;
	}
	else
	{
		ri.Com_DPrintf( S_COLOR_YELLOW "Shader %s has a stage with no image: %s\n", shader->name, token );
		pass->images[0] = rsh.noTexture;
		pass->tcgen = TC_GEN_BASE;
	}
}

static void Shaderpass_Map( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	Shaderpass_MapExt( shader, pass, 0, ptr );
}

static void Shaderpass_ClampMap( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	Shaderpass_MapExt( shader, pass, IT_CLAMP, ptr );
}

static void Shaderpass_AnimMap( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	Shaderpass_AnimMapExt( shader, pass, 0, ptr );
}

static void Shaderpass_AnimClampMap( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	Shaderpass_AnimMapExt( shader, pass, IT_CLAMP, ptr );
}

static void Shaderpass_CubeMap( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	Shaderpass_CubeMapExt( shader, pass, IT_CLAMP, TC_GEN_REFLECTION, ptr );
}

static void Shaderpass_ShadeCubeMap( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	Shaderpass_CubeMapExt( shader, pass, IT_CLAMP, TC_GEN_REFLECTION_CELSHADE, ptr );
}

static void Shaderpass_SurroundMap( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	Shaderpass_CubeMapExt( shader, pass, IT_CLAMP, TC_GEN_SURROUND, ptr );
}

static void Shaderpass_VideoMap( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	char *token;

	R_FreePassCinematics( pass );

	token = Shader_ParseString( ptr );

	pass->cin = R_StartCinematic( token );
	pass->tcgen = TC_GEN_BASE;
	pass->anim_fps = 0;
	pass->flags &= ~(SHADERPASS_LIGHTMAP|SHADERPASS_PORTALMAP);
}

static void Shaderpass_Material( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	int i, flags;
	char *token;
	bool endl;

	R_FreePassCinematics( pass );

	flags = Shader_SetImageFlags( shader );
	token = Shader_ParseString( ptr );

	endl = token[0] == '\0';
	if( endl ) {
		// single-word syntax
		token = shader->name;
	}

	pass->images[0] = Shader_FindImage( shader, token, flags );
	if( !pass->images[0] )
	{
		ri.Com_DPrintf( S_COLOR_YELLOW "WARNING: failed to load base/diffuse image for material %s in shader %s.\n", token, shader->name );
		return;
	}

	pass->images[1] = pass->images[2] = pass->images[3] = NULL;

	pass->tcgen = TC_GEN_BASE;
	pass->flags &= ~( SHADERPASS_LIGHTMAP|SHADERPASS_PORTALMAP );
	if( pass->rgbgen.type == RGB_GEN_UNKNOWN )
		pass->rgbgen.type = RGB_GEN_IDENTITY;
	
	// I assume materials are only applied to lightmapped surfaces
	r_shaderHasLightmapPass = true;

	while( !endl )
	{
		token = Shader_ParseString( ptr );
		if( !*token )
			break;

		if( Q_isdigit( token ) )
		{
			continue;
		}
		else if( !pass->images[1] )
		{
			image_t *normalmap;
			normalmap = Shader_FindImage( shader, token, flags|IT_NORMALMAP );
			pass->program_type = GLSL_PROGRAM_TYPE_MATERIAL;
			pass->images[1] = normalmap;
		}
		else if( !pass->images[2] )
		{
			if( strcmp( token, "-" ) && r_lighting_specular->integer ) {
				pass->images[2] = Shader_FindImage( shader, token, flags );
			} else {
				// set gloss to rsh.blackTexture so we know we have already parsed the gloss image
				pass->images[2] = rsh.blackTexture;
			}
		}
		else
		{
			// parse decal images
			for( i = 3; i < 5; i++ ) {
				if( pass->images[i] ) {
					continue;
				}

				if( strcmp( token, "-" ) ) {
					pass->images[i] = Shader_FindImage( shader, token, flags );
				}
				else {
					pass->images[i] = rsh.whiteTexture;
				}
				break;
			}
		}
	}

	// black texture => no gloss, so don't waste time in the GLSL program
	if( pass->images[2] == rsh.blackTexture )
		pass->images[2] = NULL;

	for( i = 3; i < 5; i++ ) {
		if( pass->images[i] == rsh.whiteTexture )
			pass->images[i] = NULL;
	}

	if( pass->images[1] )
		return;

	// load default images
	pass->program_type = GLSL_PROGRAM_TYPE_MATERIAL;
	Shaderpass_LoadMaterial( &pass->images[1], &pass->images[2], &pass->images[3],
		pass->images[0]->name, flags, shader->imagetags );
}

static void Shaderpass_Distortion( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	int flags;
	char *token;

	if( !r_portalmaps->integer )
	{
		ri.Com_DPrintf( S_COLOR_YELLOW "WARNING: shader %s has a distortion stage, while GLSL is not supported\n", shader->name );
		Shader_SkipLine( ptr );
		return;
	}

	R_FreePassCinematics( pass );

	flags = Shader_SetImageFlags( shader );
	pass->flags &= ~( SHADERPASS_LIGHTMAP|SHADERPASS_PORTALMAP );
	pass->images[0] = pass->images[1] = NULL;

	while( 1 )
	{
		token = Shader_ParseString( ptr );
		if( !*token )
			break;

		if( Q_isdigit( token ) )
		{
			continue;
		}
		else if( !pass->images[0] )
		{
			pass->images[0] = Shader_FindImage( shader, token, flags );
			pass->program_type = GLSL_PROGRAM_TYPE_DISTORTION;
		}
		else
		{
			pass->images[1] = Shader_FindImage( shader, token, flags );
		}
	}

	if( pass->rgbgen.type == RGB_GEN_UNKNOWN )
	{
		pass->rgbgen.type = RGB_GEN_CONST;
		VectorClear( pass->rgbgen.args );
	}

	if( shader->sort == SHADER_SORT_PORTAL )
		shader->sort = 0; // reset sorting so we can figure it out later. FIXME?
	shader->flags |= SHADER_PORTAL|SHADER_PORTAL_CAPTURE|SHADER_PORTAL_CAPTURE2;
}

// <base> <celshade> [diffuse] [decal] [entitydecal] [stripes] [celllight]
static void Shaderpass_Celshade( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	int i;
	int flags;
	char *token;

	R_FreePassCinematics( pass );

	flags = Shader_SetImageFlags( shader );
	pass->tcgen = TC_GEN_BASE;
	pass->flags &= ~( SHADERPASS_LIGHTMAP|SHADERPASS_PORTALMAP );
	if( pass->rgbgen.type == RGB_GEN_UNKNOWN )
		pass->rgbgen.type = RGB_GEN_IDENTITY;

	pass->anim_fps = 0;
	memset( pass->images, 0, sizeof( pass->images ) );

	// at least two valid images are required: 'base' and 'celshade'
	for( i = 0; i < 2; i++ ) {
		token = Shader_ParseString( ptr );
		if( *token && strcmp( token, "-" ) )
			pass->images[i] = Shader_FindImage( shader, token, flags | (i ? IT_CLAMP|IT_CUBEMAP : 0) );
	}

	pass->program_type = GLSL_PROGRAM_TYPE_CELSHADE;

	// parse optional images: [diffuse] [decal] [entitydecal] [stripes] [celllight]
	for( i = 0; i < 5; i++ ) {
		token = Shader_ParseString( ptr );
		if( !*token ) {
			break;
		}
		if( strcmp( token, "-" ) ) {
			pass->images[i+2] = Shader_FindImage( shader, token, flags | (i == 4 ? IT_CLAMP|IT_CUBEMAP : 0) );
		}
	}
}

static void Shaderpass_RGBGen( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	char *token;
	bool wave = false;

	token = Shader_ParseString( ptr );
	if( !strcmp( token, "identitylighting" ) )
		pass->rgbgen.type = RGB_GEN_IDENTITY;
	else if( !strcmp( token, "identity" ) )
		pass->rgbgen.type = RGB_GEN_IDENTITY;
	else if( !strcmp( token, "wave" ) )
	{
		pass->rgbgen.type = RGB_GEN_WAVE;
		pass->rgbgen.args[0] = 1.0f;
		pass->rgbgen.args[1] = 1.0f;
		pass->rgbgen.args[2] = 1.0f;
		Shader_ParseFunc( ptr, &pass->rgbgen.func );
	}
	else if( ( wave = !strcmp( token, "colorwave" ) ? true : false ) )
	{
		pass->rgbgen.type = RGB_GEN_WAVE;
		Shader_ParseVector( ptr, pass->rgbgen.args, 3 );
		Shader_ParseFunc( ptr, &pass->rgbgen.func );
	}
	else if( !strcmp( token, "custom" ) || !strcmp( token, "teamcolor" )
		|| ( wave = !strcmp( token, "teamcolorwave" ) || !strcmp( token, "customcolorwave" ) ? true : false ) )
	{
		pass->rgbgen.type = RGB_GEN_CUSTOMWAVE;
		pass->rgbgen.args[0] = (int)Shader_ParseFloat( ptr );
		if( pass->rgbgen.args[0] < 0 || pass->rgbgen.args[0] >= NUM_CUSTOMCOLORS )
			pass->rgbgen.args[0] = 0;
		pass->rgbgen.func.type = SHADER_FUNC_NONE;
		if( wave )
			Shader_ParseFunc( ptr, &pass->rgbgen.func );
	}
	else if( !strcmp( token, "entity" ) || ( wave = !strcmp( token, "entitycolorwave" ) ? true : false ) )
	{
		pass->rgbgen.type = RGB_GEN_ENTITYWAVE;
		pass->rgbgen.func.type = SHADER_FUNC_NONE;
		if( wave )
		{
			Shader_ParseVector( ptr, pass->rgbgen.args, 3 );
			Shader_ParseFunc( ptr, &pass->rgbgen.func );
		}
	}
	else if( !strcmp( token, "oneminusentity" ) )
		pass->rgbgen.type = RGB_GEN_ONE_MINUS_ENTITY;
	else if( !strcmp( token, "vertex" ) )
		pass->rgbgen.type = RGB_GEN_VERTEX;
	else if( !strcmp( token, "oneminusvertex" ) )
		pass->rgbgen.type = RGB_GEN_ONE_MINUS_VERTEX;
	else if( !strcmp( token, "lightingdiffuse" ) ) {
		if( shader->type < SHADER_TYPE_DIFFUSE ) {
			pass->rgbgen.type = RGB_GEN_VERTEX;
		} else if( shader->type > SHADER_TYPE_DIFFUSE ) {
			pass->rgbgen.type = RGB_GEN_IDENTITY;
		} else {
			pass->rgbgen.type = RGB_GEN_LIGHTING_DIFFUSE;
		}
	}
	else if( !strcmp( token, "exactvertex" ) )
		pass->rgbgen.type = RGB_GEN_EXACT_VERTEX;
	else if( !strcmp( token, "const" ) || !strcmp( token, "constant" ) )
	{
		vec3_t color;

		pass->rgbgen.type = RGB_GEN_CONST;
		Shader_ParseVector( ptr, color, 3 );
		ColorNormalize( color, pass->rgbgen.args );
	}
}

static void Shaderpass_AlphaGen( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	char *token;
	float dist;

	token = Shader_ParseString( ptr );
	if( !strcmp( token, "vertex" ) )
		pass->alphagen.type = ALPHA_GEN_VERTEX;
	else if( !strcmp( token, "oneminusvertex" ) )
		pass->alphagen.type = ALPHA_GEN_ONE_MINUS_VERTEX;
	else if( !strcmp( token, "entity" ) )
		pass->alphagen.type = ALPHA_GEN_ENTITY;
	else if( !strcmp( token, "wave" ) )
	{
		pass->alphagen.type = ALPHA_GEN_WAVE;
		Shader_ParseFunc( ptr, &pass->alphagen.func );

		// treat custom distanceramp as portal
		if( pass->alphagen.func.type == SHADER_FUNC_RAMP && pass->alphagen.func.args[1] == 1 ) {
			dist = fabs( pass->alphagen.func.args[3] );
			shader->portalDistance = max( dist, shader->portalDistance );
		}
	}
	else if( !strcmp( token, "const" ) || !strcmp( token, "constant" ) )
	{
		pass->alphagen.type = ALPHA_GEN_CONST;
		pass->alphagen.args[0] = fabs( Shader_ParseFloat( ptr ) );
	}
	else if( !strcmp( token, "portal" ) )
	{
		dist = fabs( Shader_ParseFloat( ptr ) );
		if( !dist ) {
			dist = 256;
		}
		pass->alphagen.type = ALPHA_GEN_WAVE;
		pass->alphagen.func.type = SHADER_FUNC_RAMP;
		Vector4Set( pass->alphagen.func.args, 0, 1, 0, dist );

		shader->portalDistance = max( dist, shader->portalDistance );
	}
}

static inline int Shaderpass_SrcBlendBits( char *token )
{
	if( !strcmp( token, "gl_zero" ) )
		return GLSTATE_SRCBLEND_ZERO;
	if( !strcmp( token, "gl_one" ) )
		return GLSTATE_SRCBLEND_ONE;
	if( !strcmp( token, "gl_dst_color" ) )
		return GLSTATE_SRCBLEND_DST_COLOR;
	if( !strcmp( token, "gl_one_minus_dst_color" ) )
		return GLSTATE_SRCBLEND_ONE_MINUS_DST_COLOR;
	if( !strcmp( token, "gl_src_alpha" ) )
		return GLSTATE_SRCBLEND_SRC_ALPHA;
	if( !strcmp( token, "gl_one_minus_src_alpha" ) )
		return GLSTATE_SRCBLEND_ONE_MINUS_SRC_ALPHA;
	if( !strcmp( token, "gl_dst_alpha" ) )
		return GLSTATE_SRCBLEND_DST_ALPHA;
	if( !strcmp( token, "gl_one_minus_dst_alpha" ) )
		return GLSTATE_SRCBLEND_ONE_MINUS_DST_ALPHA;
	return GLSTATE_SRCBLEND_ONE;
}

static inline int Shaderpass_DstBlendBits( char *token )
{
	if( !strcmp( token, "gl_zero" ) )
		return GLSTATE_DSTBLEND_ZERO;
	if( !strcmp( token, "gl_one" ) )
		return GLSTATE_DSTBLEND_ONE;
	if( !strcmp( token, "gl_src_color" ) )
		return GLSTATE_DSTBLEND_SRC_COLOR;
	if( !strcmp( token, "gl_one_minus_src_color" ) )
		return GLSTATE_DSTBLEND_ONE_MINUS_SRC_COLOR;
	if( !strcmp( token, "gl_src_alpha" ) )
		return GLSTATE_DSTBLEND_SRC_ALPHA;
	if( !strcmp( token, "gl_one_minus_src_alpha" ) )
		return GLSTATE_DSTBLEND_ONE_MINUS_SRC_ALPHA;
	if( !strcmp( token, "gl_dst_alpha" ) )
		return GLSTATE_DSTBLEND_DST_ALPHA;
	if( !strcmp( token, "gl_one_minus_dst_alpha" ) )
		return GLSTATE_DSTBLEND_ONE_MINUS_DST_ALPHA;
	return GLSTATE_DSTBLEND_ONE;
}

static void Shaderpass_BlendFunc( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	char *token;

	token = Shader_ParseString( ptr );

	pass->flags &= ~GLSTATE_BLEND_MASK;
	if( !strcmp( token, "blend" ) )
		pass->flags |= GLSTATE_SRCBLEND_SRC_ALPHA|GLSTATE_DSTBLEND_ONE_MINUS_SRC_ALPHA;
	else if( !strcmp( token, "filter" ) )
		pass->flags |= GLSTATE_SRCBLEND_DST_COLOR|GLSTATE_DSTBLEND_ZERO;
	else if( !strcmp( token, "add" ) )
		pass->flags |= GLSTATE_SRCBLEND_ONE|GLSTATE_DSTBLEND_ONE;
	else
	{
		pass->flags |= Shaderpass_SrcBlendBits( token );
		pass->flags |= Shaderpass_DstBlendBits( Shader_ParseString( ptr ) );
	}
}

static void Shaderpass_AlphaFunc( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	char *token;

	token = Shader_ParseString( ptr );

	pass->flags &= ~(SHADERPASS_ALPHAFUNC);
	if( !strcmp( token, "gt0" ) )
		pass->flags |= SHADERPASS_AFUNC_GT0;
	else if( !strcmp( token, "lt128" ) )
		pass->flags |= SHADERPASS_AFUNC_LT128;
	else if( !strcmp( token, "ge128" ) )
		pass->flags |= SHADERPASS_AFUNC_GE128;
}

static void Shaderpass_DepthFunc( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	char *token;

	token = Shader_ParseString( ptr );

	pass->flags &= ~GLSTATE_DEPTHFUNC_EQ;
	if( !strcmp( token, "equal" ) )
		pass->flags |= GLSTATE_DEPTHFUNC_EQ;
}

static void Shaderpass_DepthWrite( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	pass->flags |= GLSTATE_DEPTHWRITE;
}

static void Shaderpass_TcMod( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	int i;
	tcmod_t *tcmod;
	char *token;

	if( pass->numtcmods == MAX_SHADER_TCMODS )
	{
		Com_Printf( S_COLOR_YELLOW "WARNING: shader %s has too many tcmods\n", shader->name );
		Shader_SkipLine( ptr );
		return;
	}

	tcmod = &pass->tcmods[pass->numtcmods];

	token = Shader_ParseString( ptr );
	if( !strcmp( token, "rotate" ) )
	{
		tcmod->args[0] = -Shader_ParseFloat( ptr ) / 360.0f;
		if( !tcmod->args[0] )
			return;
		tcmod->type = TC_MOD_ROTATE;
	}
	else if( !strcmp( token, "scale" ) )
	{
		Shader_ParseVector( ptr, tcmod->args, 2 );
		tcmod->type = TC_MOD_SCALE;
	}
	else if( !strcmp( token, "scroll" ) )
	{
		Shader_ParseVector( ptr, tcmod->args, 2 );
		tcmod->type = TC_MOD_SCROLL;
	}
	else if( !strcmp( token, "stretch" ) )
	{
		shaderfunc_t func;

		Shader_ParseFunc( ptr, &func );

		tcmod->args[0] = func.type;
		for( i = 1; i < 5; i++ )
			tcmod->args[i] = func.args[i-1];
		tcmod->type = TC_MOD_STRETCH;
	}
	else if( !strcmp( token, "transform" ) )
	{
		Shader_ParseVector( ptr, tcmod->args, 6 );
		tcmod->args[4] = tcmod->args[4] - floor( tcmod->args[4] );
		tcmod->args[5] = tcmod->args[5] - floor( tcmod->args[5] );
		tcmod->type = TC_MOD_TRANSFORM;
	}
	else if( !strcmp( token, "turb" ) )
	{
		Shader_ParseVector( ptr, tcmod->args, 4 );
		tcmod->type = TC_MOD_TURB;
	}
	else
	{
		Shader_SkipLine( ptr );
		return;
	}

	r_currentPasses[shader->numpasses].numtcmods++;
}

static void Shaderpass_TcGen( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	char *token;

	token = Shader_ParseString( ptr );
	if( !strcmp( token, "base" ) )
		pass->tcgen = TC_GEN_BASE;
	else if( !strcmp( token, "lightmap" ) )
		pass->tcgen = TC_GEN_LIGHTMAP;
	else if( !strcmp( token, "environment" ) )
		pass->tcgen = TC_GEN_ENVIRONMENT;
	else if( !strcmp( token, "vector" ) )
	{
		pass->tcgen = TC_GEN_VECTOR;
		Shader_ParseVector( ptr, &pass->tcgenVec[0], 4 );
		Shader_ParseVector( ptr, &pass->tcgenVec[4], 4 );
	}
	else if( !strcmp( token, "reflection" ) )
		pass->tcgen = TC_GEN_REFLECTION;
	else if( !strcmp( token, "celshade" ) )
		pass->tcgen = TC_GEN_REFLECTION_CELSHADE;
	else if( !strcmp( token, "surround" ) )
		pass->tcgen = TC_GEN_SURROUND;
}

static void Shaderpass_Detail( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	pass->flags |= SHADERPASS_DETAIL;
}

static void Shaderpass_Greyscale( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	pass->flags |= SHADERPASS_GREYSCALE;
}

static void Shaderpass_Skip( shader_t *shader, shaderpass_t *pass, const char **ptr )
{
	Shader_SkipLine( ptr );
}

static const shaderkey_t shaderpasskeys[] =
{
	{ "rgbgen", Shaderpass_RGBGen },
	{ "blendfunc", Shaderpass_BlendFunc },
	{ "depthfunc", Shaderpass_DepthFunc },
	{ "depthwrite",	Shaderpass_DepthWrite },
	{ "alphafunc", Shaderpass_AlphaFunc },
	{ "tcmod", Shaderpass_TcMod },
	{ "map", Shaderpass_Map },
	{ "animmap", Shaderpass_AnimMap },
	{ "cubemap", Shaderpass_CubeMap },
	{ "shadecubemap", Shaderpass_ShadeCubeMap },
	{ "surroundmap", Shaderpass_SurroundMap },
	{ "videomap", Shaderpass_VideoMap },
	{ "clampmap", Shaderpass_ClampMap },
	{ "animclampmap", Shaderpass_AnimClampMap },
	{ "material", Shaderpass_Material },
	{ "distortion",	Shaderpass_Distortion },
	{ "celshade", Shaderpass_Celshade },
	{ "tcgen", Shaderpass_TcGen },
	{ "alphagen", Shaderpass_AlphaGen },
	{ "detail", Shaderpass_Detail },
	{ "grayscale", Shaderpass_Greyscale },
	{ "greyscale", Shaderpass_Greyscale },
	{ "skip", Shaderpass_Skip },

	{ NULL,	NULL }
};

// ===============================================================

/*
* R_PrintShaderList
*/
void R_PrintShaderList( const char *pattern, bool (*filter)( const char *filter, const char *value) )
{
	int i;
	int numShaders;
	shader_t *shader;

	if( !pattern ) {
		pattern = "";
	}

	numShaders = 0;

	Com_Printf( "------------------\n" );
	for( i = 0, shader = r_shaders; i < MAX_SHADERS; i++, shader++ ) {
		if( !shader->name ) {
			continue;
		}
		if( filter && !filter( pattern, shader->name ) ) {
			continue;
		}
		
		Com_Printf( " %2i %2i: %s\n", shader->numpasses, shader->sort, shader->name );
		numShaders++;
	}
	Com_Printf( "%i shaders total\n", numShaders );
}

/*
* R_PrintShaderCache
*/
void R_PrintShaderCache( const char *name )
{
	char backup, *start;
	const char *ptr;
	shadercache_t *cache;

	Shader_GetCache( name, &cache );
	if( !cache )
	{
		Com_Printf( "Could not find shader %s in cache.\n", name );
		return;
	}

	start = cache->buffer + cache->offset;

	// temporarily hack in the zero-char
	ptr = start;
	Shader_SkipBlock( &ptr );
	backup = cache->buffer[ptr - cache->buffer];
	cache->buffer[ptr - cache->buffer] = '\0';

	Com_Printf( "Found in %s:\n\n", cache->filename );
	Com_Printf( S_COLOR_YELLOW "%s%s\n", name, start );

	cache->buffer[ptr - cache->buffer] = backup;
}

static void Shader_MakeCache( const char *filename )
{
	int size;
	unsigned int key;
	char *pathName = NULL;
	size_t pathNameSize;
	char *token, *buf, *temp = NULL;
	const char *ptr;
	shadercache_t *cache;
	uint8_t *cacheMemBuf;
	size_t cacheMemSize;

	pathNameSize = strlen( "scripts/" ) + strlen( filename ) + 1;
	pathName = R_Malloc( pathNameSize );
	assert( pathName );
	Q_snprintfz( pathName, pathNameSize, "scripts/%s", filename );

	Com_Printf( "...loading '%s'\n", pathName );

	size = R_LoadFile( pathName, ( void ** )&temp );
	if( !temp || size <= 0 )
		goto done;

	size = COM_Compress( temp );
	if( !size )
		goto done;

	buf = R_Malloc( size+1 );
	strcpy( buf, temp );
	R_FreeFile( temp );
	temp = NULL;

	// calculate buffer size to allocate our cache objects all at once (we may leak
	// insignificantly here because of duplicate entries)
	for( ptr = buf, cacheMemSize = 0; ptr; )
	{
		token = COM_ParseExt( &ptr, true );
		if( !token[0] )
			break;

		cacheMemSize += sizeof( shadercache_t ) + strlen( token ) + 1;
		Shader_SkipBlock( &ptr );
	}

	if( !cacheMemSize )
	{
		R_Free( buf );
		goto done;
	}

	cacheMemBuf = R_Malloc( cacheMemSize );
	memset( cacheMemBuf, 0, cacheMemSize );
	for( ptr = buf; ptr; )
	{
		token = COM_ParseExt( &ptr, true );
		if( !token[0] )
			break;

		token = Q_strlwr( token );
		key = Shader_GetCache( token, &cache );
		if( cache )
			goto set_path_and_offset;

		cache = ( shadercache_t * )cacheMemBuf; cacheMemBuf += sizeof( shadercache_t ) + strlen( token ) + 1;
		cache->hash_next = shadercache_hash[key];
		cache->name = ( char * )( (uint8_t *)cache + sizeof( shadercache_t ) );
		cache->filename = NULL;
		strcpy( cache->name, token );
		shadercache_hash[key] = cache;

set_path_and_offset:
		if( cache->filename )
			R_Free( cache->filename );
		cache->filename = R_CopyString( filename );
		cache->buffer = buf;
		cache->offset = ptr - buf;

		Shader_SkipBlock( &ptr );
	}

done:
	if( temp )
		R_FreeFile( temp );
	if( pathName )
		R_Free( pathName );
}

/*
* Shader_GetCache
*/
static unsigned int Shader_GetCache( const char *name, shadercache_t **cache )
{
	unsigned int key;
	shadercache_t *c;
	unsigned int len;

	*cache = NULL;

	len = strlen( name );
	key = COM_SuperFastHash( ( const uint8_t * )name, len, len ) % SHADERCACHE_HASH_SIZE;
	for( c = shadercache_hash[key]; c; c = c->hash_next )
	{
		if( !Q_stricmp( c->name, name ) )
		{
			*cache = c;
			return key;
		}
	}

	return key;
}

/*
* R_PrecacheShaders
*/
static void R_InitShadersCache( void )
{
	int d;
	int i, j, k, numfiles;
	int numfiles_total;
	const char *fileptr;
	char shaderPaths[1024];
	const char *dirs[3] = { "<scripts", ">scripts", "scripts" };

	r_shaderTemplateBuf = NULL;

	memset( shadercache_hash, 0, sizeof( shadercache_t * )*SHADERCACHE_HASH_SIZE );
	
	Com_Printf( "Initializing Shaders:\n" );

	numfiles_total = 0;
	for( d = 0; d < 3; d++ ) {
		if( d == 2 ) {
			// this is a fallback case for older bins that do not support the '<>' prefixes
			// since we got some files, the binary is sufficiently up to date
			if( numfiles_total )
				break;
		}

		// enumerate shaders
		numfiles = ri.FS_GetFileList( dirs[d], ".shader", NULL, 0, 0, 0 );
		numfiles_total += numfiles;

		// now load them all
		for( i = 0; i < numfiles; i += k ) {
			if( ( k = ri.FS_GetFileList( dirs[d], ".shader", shaderPaths, sizeof( shaderPaths ), i, numfiles )) == 0 ) {
				k = 1; // advance by one file
				continue;
			}

			fileptr = shaderPaths;
			for( j = 0; j < k; j++ ) {
				Shader_MakeCache( fileptr );

				fileptr += strlen( fileptr ) + 1;
				if( !*fileptr ) {
					break;
				}
			}
		}
	}

	if( !numfiles_total ) {
		ri.Com_Error( ERR_DROP, "Could not find any shaders!" );
	}

	Com_Printf( "--------------------------------------\n" );
}

/*
* R_InitShaders
*/
void R_InitShaders( void )
{
	int i;

	R_InitShadersCache();

	memset( r_shaders, 0, sizeof( r_shaders ) );

	// link shaders
	r_free_shaders = r_shaders;
	for( i = 0; i < SHADERS_HASH_SIZE; i++ ) {
		r_shaders_hash_headnode[i].prev = &r_shaders_hash_headnode[i];
		r_shaders_hash_headnode[i].next = &r_shaders_hash_headnode[i];
	}
	for( i = 0; i < MAX_SHADERS - 1; i++ ) {
		r_shaders[i].next = &r_shaders[i+1];
	}
}

/*
* R_FreeShader
*/
static void R_FreeShader( shader_t *shader )
{
	unsigned i;
	shaderpass_t *pass;

	if( shader->cin ) {
		for( i = 0, pass = shader->passes; i < shader->numpasses; i++, pass++ )
			R_FreePassCinematics( pass );
	}

	if( shader->deforms ) {
		R_Free( shader->deforms );
		shader->deforms = 0;
	}
	shader->numdeforms = 0;
	shader->deformsKey = NULL;
	R_Free( shader->passes );
	shader->passes = NULL;
	shader->numpasses = 0;
	shader->name = NULL;
	shader->flags = 0;
	shader->registrationSequence = 0;
}

/*
* R_UnlinkShader
*/
static void R_UnlinkShader( shader_t *shader )
{
	// remove from linked active list
	shader->prev->next = shader->next;
	shader->next->prev = shader->prev;

	// insert into linked free list
	shader->next = r_free_shaders;
	r_free_shaders = shader;
}

/*
* R_TouchShader
*/
void R_TouchShader( shader_t *s )
{
	unsigned i, j;
	unsigned imagetags;

	if( s->registrationSequence == rsh.registrationSequence ) {
		return;
	}

	s->registrationSequence = rsh.registrationSequence;

	imagetags = s->imagetags;

	// touch all images this shader references
	for( i = 0; i < s->numpasses; i++ ) {
		shaderpass_t *pass = s->passes + i;

		for( j = 0; j < MAX_SHADER_IMAGES; j++ ) {
			image_t *image = pass->images[j];
			if( image ) {
				R_TouchImage( image, imagetags );
			} else if( !pass->program_type ) {
				// only programs can have gaps in images
				break;
			}
		}

		if( pass->cin ) {
			R_TouchCinematic( pass->cin );
		}
	}

	if( s->flags & SHADER_SKY ) {
		// touch sky images for this shader
		for( i = 0; i < 6; i++ ) {
			image_t *image = s->skyboxImages[i];
			if( image ) {
				R_TouchImage( image, imagetags );
			}
		}
	}
}

/*
* R_FreeUnusedShadersByType
*/
void R_FreeUnusedShadersByType( const shaderType_e *types, unsigned int numTypes )
{
	int i;
	unsigned int type;
	shader_t *s;

	for( i = 0, s = r_shaders; i < MAX_SHADERS; i++, s++ ) {
		if( !s->name ) {
			// free shader
			continue;
		}
		if( s->registrationSequence == rsh.registrationSequence ) {
			// we need this shader
			continue;
		}

		if( numTypes ) {
			for( type = 0; type < numTypes; type++ ) {
				if( s->type == types[type] ) {
					break;
				}
			}
			if( type >= numTypes ) {
				// not in the type filter
				continue;
			}
		}

		R_FreeShader( s );

		R_UnlinkShader( s );
	}
}

/*
* R_FreeUnusedShaders
*/
void R_FreeUnusedShaders( void )
{
	R_FreeUnusedShadersByType( NULL, 0 );
}

/*
* R_ShutdownShaders
*/
void R_ShutdownShaders( void )
{
	int i;
	shader_t *s;

	for( i = 0, s = r_shaders; i < MAX_SHADERS; i++, s++ ) {
		if( !s->name ) {
			// free shader
			continue;
		}
		R_FreeShader( s );
	}

	R_Free( r_shaderTemplateBuf );
	R_Free( r_shortShaderName );

	r_shaderTemplateBuf = NULL;
	r_shortShaderName = NULL;
	r_shortShaderNameSize = 0;

	memset( shadercache_hash, 0, sizeof( shadercache_hash ) );
}

static void Shader_Readpass( shader_t *shader, const char **ptr )
{
	int n = shader->numpasses;
	int blendmask;
	const char *token;
	shaderpass_t *pass;

	if( n == MAX_SHADER_PASSES )
	{
		Com_Printf( S_COLOR_YELLOW "WARNING: shader %s has too many passes\n", shader->name );

		while( ptr )
		{	// skip
			token = COM_ParseExt( ptr, true );
			if( !token[0] || token[0] == '}' )
				break;
		}
		return;
	}

	// Set defaults
	pass = &r_currentPasses[n];
	memset( pass, 0, sizeof( shaderpass_t ) );
	pass->rgbgen.type = RGB_GEN_UNKNOWN;
	pass->rgbgen.args = r_currentRGBgenArgs[n];
	pass->alphagen.type = ALPHA_GEN_UNKNOWN;
	pass->alphagen.args = r_currentAlphagenArgs[n];
	pass->tcgenVec = r_currentTcGen[n][0];
	pass->tcgen = TC_GEN_BASE;
	pass->tcmods = r_currentTcmods[n];

	while( ptr )
	{
		token = COM_ParseExt( ptr, true );

		if( !token[0] )
			break;
		else if( token[0] == '}' )
			break;
		else if( Shader_Parsetok( shader, pass, shaderpasskeys, token, ptr ) )
			break;
	}

	blendmask = (pass->flags & GLSTATE_BLEND_MASK);

	if( (pass->flags & SHADERPASS_LIGHTMAP) && r_lighting_vertexlight->integer )
		return;

	// keep track of detail passes. if all passes are detail, the whole shader is also detail
	r_shaderAllDetail &= (pass->flags & SHADERPASS_DETAIL);

	if( pass->rgbgen.type == RGB_GEN_UNKNOWN )
	{
		pass->rgbgen.type = RGB_GEN_IDENTITY;
	}

	if( pass->alphagen.type == ALPHA_GEN_UNKNOWN )
	{
		if( pass->rgbgen.type == RGB_GEN_VERTEX || pass->rgbgen.type == RGB_GEN_EXACT_VERTEX )
			pass->alphagen.type = ALPHA_GEN_VERTEX;
		else
			pass->alphagen.type = ALPHA_GEN_IDENTITY;
	}

	if( blendmask == (GLSTATE_SRCBLEND_ONE|GLSTATE_DSTBLEND_ZERO) )
	{
		pass->flags &= ~blendmask;
	}

	shader->numpasses++;
}

static bool Shader_Parsetok( shader_t *shader, shaderpass_t *pass, const shaderkey_t *keys, const char *token, const char **ptr )
{
	const shaderkey_t *key;

	for( key = keys; key->keyword != NULL; key++ )
	{
		if( !Q_stricmp( token, key->keyword ) )
		{
			if( key->func )
				key->func( shader, pass, ptr );
			if( *ptr && **ptr == '}' )
			{
				*ptr = *ptr + 1;
				return true;
			}
			return false;
		}
	}

	Shader_SkipLine( ptr );

	return false;
}

static void Shader_SetVertexAttribs( shader_t *s )
{
	unsigned i;
	shaderpass_t *pass;

	s->vattribs |= VATTRIB_POSITION_BIT;

	if( s->flags & SHADER_SKY )
		s->vattribs |= VATTRIB_TEXCOORDS_BIT;

	for( i = 0; i < s->numdeforms; i++ )
	{
		switch( s->deforms[i].type )
		{
		case DEFORMV_BULGE:
			s->vattribs |= VATTRIB_TEXCOORDS_BIT;
		case DEFORMV_WAVE:
			s->vattribs |= VATTRIB_NORMAL_BIT;
			break;
		case DEFORMV_AUTOSPRITE:
		case DEFORMV_AUTOPARTICLE:
			s->vattribs |= VATTRIB_AUTOSPRITE_BIT;
			break;
		case DEFORMV_AUTOSPRITE2:
			s->vattribs |= VATTRIB_AUTOSPRITE_BIT;
			s->vattribs |= VATTRIB_AUTOSPRITE2_BIT;
			break;
		case DEFORMV_MOVE:
			break;
		default:
			break;
		}
	}

	for( i = 0, pass = s->passes; i < s->numpasses; i++, pass++ )
	{
		if ( pass->program_type == GLSL_PROGRAM_TYPE_MATERIAL )
			s->vattribs |= VATTRIB_NORMAL_BIT|VATTRIB_SVECTOR_BIT|VATTRIB_LMCOORDS0_BIT;
		else if( pass->program_type == GLSL_PROGRAM_TYPE_DISTORTION )
			s->vattribs |= VATTRIB_NORMAL_BIT|VATTRIB_SVECTOR_BIT;
		else if( pass->program_type == GLSL_PROGRAM_TYPE_CELSHADE )
			s->vattribs |= VATTRIB_TEXCOORDS_BIT|VATTRIB_NORMAL_BIT;

		switch( pass->rgbgen.type )
		{
		case RGB_GEN_LIGHTING_DIFFUSE:
			if( pass->program_type == GLSL_PROGRAM_TYPE_MATERIAL )
				s->vattribs = (s->vattribs | VATTRIB_COLOR0_BIT) & ~VATTRIB_LMCOORDS0_BIT;
			s->vattribs |= VATTRIB_NORMAL_BIT;
			break;
		case RGB_GEN_VERTEX:
		case RGB_GEN_ONE_MINUS_VERTEX:
		case RGB_GEN_EXACT_VERTEX:
			s->vattribs |= VATTRIB_COLOR0_BIT;
			break;
		case RGB_GEN_WAVE:
			if( pass->rgbgen.func.type == SHADER_FUNC_RAMP )
				s->vattribs |= VATTRIB_NORMAL_BIT;
			break;
		}

		switch( pass->alphagen.type )
		{
		case ALPHA_GEN_VERTEX:
		case ALPHA_GEN_ONE_MINUS_VERTEX:
			s->vattribs |= VATTRIB_COLOR0_BIT;
			break;
		case ALPHA_GEN_WAVE:
			if( pass->alphagen.func.type == SHADER_FUNC_RAMP )
				s->vattribs |= VATTRIB_NORMAL_BIT;
			break;
		}

		switch( pass->tcgen )
		{
		case TC_GEN_LIGHTMAP:
			s->vattribs |= VATTRIB_LMCOORDS0_BIT;
			break;
		case TC_GEN_ENVIRONMENT:
			s->vattribs |= VATTRIB_NORMAL_BIT;
			break;
		case TC_GEN_REFLECTION:
		case TC_GEN_REFLECTION_CELSHADE:
			s->vattribs |= VATTRIB_NORMAL_BIT;
			break;
		default:
			s->vattribs |= VATTRIB_TEXCOORDS_BIT;
			break;
		}
	}
}

static void Shader_Finish( shader_t *s )
{
	unsigned i;
	int opaque = -1;
	int blendmask;
	const char *oldname = s->name;
	size_t size = 0, bufferOffset = 0;

	shaderpass_t *pass;
	uint8_t *buffer;
	size_t deformvKeyLen;

	deformvKeyLen = strlen( r_shaderDeformvKey );

	if( !s->numpasses && !s->sort )
	{
		if( s->flags & SHADER_PORTAL )
			s->sort = SHADER_SORT_PORTAL;
		else
			s->sort = SHADER_SORT_ADDITIVE;
	}

	if( s->flags & SHADER_SKY )
		s->sort = SHADER_SORT_SKY;
	if( ( s->flags & SHADER_POLYGONOFFSET ) && !s->sort )
		s->sort = SHADER_SORT_DECAL;

	// fix up rgbgen's and blendmodes for lightmapped shaders and vertex lighting
	if (r_shaderHasLightmapPass && r_lighting_vertexlight->integer && ( s->type == SHADER_TYPE_DELUXEMAP || s->type == SHADER_TYPE_VERTEX ) )
	{
		for( i = 0, pass = r_currentPasses; i < s->numpasses; i++, pass++ )
		{
			blendmask = pass->flags & GLSTATE_BLEND_MASK;

			if( !blendmask || blendmask == (GLSTATE_SRCBLEND_DST_COLOR|GLSTATE_DSTBLEND_ZERO) || s->numpasses == 1 )
			{
				if( pass->rgbgen.type == RGB_GEN_IDENTITY ) {
					pass->rgbgen.type = RGB_GEN_VERTEX;
				}
				//if( pass->alphagen.type == ALPHA_GEN_IDENTITY )
				//	pass->alphagen.type = ALPHA_GEN_VERTEX;

				if( !(pass->flags & SHADERPASS_ALPHAFUNC) && (blendmask != (GLSTATE_SRCBLEND_SRC_ALPHA|GLSTATE_DSTBLEND_ONE_MINUS_SRC_ALPHA)) )
					pass->flags &= ~blendmask;
				break;
			}
		}
	}

	size = s->numpasses * sizeof( shaderpass_t );

	for( i = 0, pass = r_currentPasses; i < s->numpasses; i++, pass++ )
	{
		size = ALIGN( size, 16 );

		// rgbgen args
		if( pass->rgbgen.type == RGB_GEN_WAVE ||
			pass->rgbgen.type == RGB_GEN_CONST )
			size += sizeof( float ) * 4;
		else if( pass->rgbgen.type == RGB_GEN_CUSTOMWAVE )
			size += sizeof( float ) * 2;

		// alphagen args
		if( pass->alphagen.type == ALPHA_GEN_CONST )
			size += sizeof( float ) * 2;

		if( pass->tcgen == TC_GEN_VECTOR )
			size += sizeof( vec4_t ) * 2;

		size += pass->numtcmods * sizeof( tcmod_t );
	}
	
	size += strlen( oldname ) + 1;

	buffer = R_Malloc( size );
	bufferOffset = 0;

	s->passes = ( shaderpass_t * )(buffer + bufferOffset); bufferOffset += s->numpasses * sizeof( shaderpass_t );
	memcpy( s->passes, r_currentPasses, s->numpasses * sizeof( shaderpass_t ) );

	for( i = 0, pass = s->passes; i < s->numpasses; i++, pass++ )
	{
		bufferOffset = ALIGN( bufferOffset, 16 );

		if( pass->rgbgen.type == RGB_GEN_WAVE ||
			pass->rgbgen.type == RGB_GEN_CONST )
		{
			pass->rgbgen.args = ( float * )(buffer + bufferOffset); bufferOffset += sizeof( float ) * 4;
			memcpy( pass->rgbgen.args, r_currentPasses[i].rgbgen.args, sizeof( float ) * 3 );
		}
		else if( pass->rgbgen.type == RGB_GEN_CUSTOMWAVE )
		{
			pass->rgbgen.args = ( float * )(buffer + bufferOffset); bufferOffset += sizeof( float ) * 2;
			memcpy( pass->rgbgen.args, r_currentPasses[i].rgbgen.args, sizeof( float ) * 1 );
		}

		if( pass->alphagen.type == ALPHA_GEN_CONST )
		{
			pass->alphagen.args = ( float * )(buffer + bufferOffset); bufferOffset += sizeof( float ) * 2;
			memcpy( pass->alphagen.args, r_currentPasses[i].alphagen.args, sizeof( float ) * 2 );
		}

		if( pass->tcgen == TC_GEN_VECTOR )
		{
			pass->tcgenVec = ( vec_t * )(buffer + bufferOffset); bufferOffset += sizeof( vec4_t ) * 2;
			Vector4Copy( &r_currentPasses[i].tcgenVec[0], &pass->tcgenVec[0] );
			Vector4Copy( &r_currentPasses[i].tcgenVec[4], &pass->tcgenVec[4] );
		}

		if( pass->numtcmods )
		{
			pass->tcmods = ( tcmod_t * )(buffer + bufferOffset); bufferOffset += pass->numtcmods * sizeof( tcmod_t );
			memcpy( pass->tcmods, r_currentPasses[i].tcmods, pass->numtcmods * sizeof( tcmod_t ) );
		}
	}

	s->name = ( char * )(buffer + bufferOffset);
	strcpy( s->name, oldname );

	size = s->numdeforms * sizeof( deformv_t );
	size += deformvKeyLen + 1;

	buffer = R_Malloc( size );
	bufferOffset = 0;

	if( s->numdeforms )
	{
		s->deforms = ( deformv_t * )(buffer + bufferOffset); bufferOffset += s->numdeforms * sizeof( deformv_t );
		memcpy( s->deforms, r_currentDeforms, s->numdeforms * sizeof( deformv_t ) );
	}

	s->deformsKey = ( char * )(buffer + bufferOffset);
	memcpy( s->deformsKey, r_shaderDeformvKey, deformvKeyLen + 1 );

	if( s->flags & SHADER_AUTOSPRITE )
		s->flags &= ~( SHADER_CULL_FRONT|SHADER_CULL_BACK );
	if( s->numpasses && r_shaderAllDetail )
		s->flags |= SHADER_ALLDETAIL;

	for( i = 0, pass = s->passes; i < s->numpasses; i++, pass++ )
	{
		blendmask = pass->flags & GLSTATE_BLEND_MASK;

		if( opaque == -1 && !blendmask )
			opaque = i;

		if( !blendmask )
			pass->flags |= GLSTATE_DEPTHWRITE;
		if( pass->cin )
			s->cin = pass->cin;
		if( ( pass->flags & SHADERPASS_LIGHTMAP || 
			pass->program_type == GLSL_PROGRAM_TYPE_MATERIAL ) && ( s->type >= SHADER_TYPE_DELUXEMAP ) )
			s->flags |= SHADER_LIGHTMAP;
		if( pass->flags & GLSTATE_DEPTHWRITE )
		{
			if( s->flags & SHADER_SKY )
				pass->flags &= ~GLSTATE_DEPTHWRITE;
			else
				s->flags |= SHADER_DEPTHWRITE;
		}

		// disable r_drawflat for shaders with customizable color passes
		if( pass->rgbgen.type == RGB_GEN_CONST || pass->rgbgen.type == RGB_GEN_CUSTOMWAVE ||
			pass->rgbgen.type == RGB_GEN_ENTITYWAVE || pass->rgbgen.type == RGB_GEN_ONE_MINUS_ENTITY ) {
			s->flags |= SHADER_NODRAWFLAT;
		}
	}

	// all passes have blendfuncs
	if( opaque == -1 )
	{
		if( !s->sort )
		{
			if( s->flags & SHADER_DEPTHWRITE )
				s->sort = SHADER_SORT_ALPHATEST;
			else
				s->sort = SHADER_SORT_ADDITIVE;
		}
	}
	else
	{
		pass = s->passes + opaque;

		if( !s->sort )
		{
			if( pass->flags & SHADERPASS_ALPHAFUNC )
				s->sort = SHADER_SORT_ALPHATEST;
		}
	}

	if( !s->sort )
		s->sort = SHADER_SORT_OPAQUE;

	// disable r_drawflat for transparent shaders
	if( s->sort >= SHADER_SORT_UNDERWATER ) {
		s->flags |= SHADER_NODRAWFLAT;
	}

	Shader_SetVertexAttribs( s );
}

/*
* R_UploadCinematicShader
*/
void R_UploadCinematicShader( const shader_t *shader )
{
	unsigned j;
	const shaderpass_t *pass;

	// upload cinematics
	for( j = 0, pass = shader->passes; j < shader->numpasses; j++, pass++ )
	{
		if( pass->cin )
			R_UploadCinematic( pass->cin );
	}
}

/*
* R_ShaderCleanName
*/
static size_t R_ShaderCleanName( const char *name, char *shortname, size_t shortname_size )
{
	int i;
	size_t length = 0;
	size_t lastDot = 0;
	size_t lastSlash = 0;

	for( i = 0; name[i] && (name[i] == '/' || name[i] == '\\'); i++ );

	for( length = 0; name[i] && ( length < shortname_size-1 ); i++ )
	{
		if( name[i] == '.' )
			lastDot = length;
		if( name[i] == '\\' )
			shortname[length] = '/';
		else
			shortname[length] = tolower( name[i] );
		if( shortname[length] == '/' )
			lastSlash = length;
		length++;
	}

	if( !length )
		return 0;
	if( lastDot < lastSlash )
		lastDot = 0;
	if( lastDot )
		length = lastDot;
	shortname[length] = 0;

	return length;
}

/*
* R_LoadShaderReal
*/
static void R_LoadShaderReal( shader_t *s, const char *shortname, 
	size_t shortname_length, const char *longname, shaderType_e type, bool forceDefault )
{
	void *data;
	shadercache_t *cache;
	shaderpass_t *pass;
	image_t *materialImages[MAX_SHADER_IMAGES];

	s->name = ( char * )shortname; // HACK, will be copied over in Shader_Finish
	s->type = type;

	if( type >= SHADER_TYPE_BSP_MIN && type <= SHADER_TYPE_BSP_MAX )
		s->imagetags = IMAGE_TAG_WORLD;
	else
		s->imagetags = IMAGE_TAG_GENERIC;

	// set defaults
	s->flags = SHADER_CULL_FRONT;
	s->vattribs = 0;
	s->glossIntensity = 0;
	s->glossExponent = 0;
	s->offsetmappingScale = 1;

	r_shaderNoMipMaps =	false;
	r_shaderNoPicMip = false;
	r_shaderNoCompress = false;
	r_shaderNoFiltering = false;
	r_shaderMinMipSize = 1;
	r_shaderHasLightmapPass = false;
	r_shaderHasAutosprite = false;
	r_shaderAllDetail = SHADERPASS_DETAIL;
	r_shaderDeformvKey[0] = '\0';
	if( !r_defaultImage )
		r_defaultImage = rsh.noTexture;

	cache = NULL;
	if( !forceDefault )
		Shader_GetCache( shortname, &cache );

	if( cache ) {
		const char *text;
		const char *ptr, *token;

		// shader is in the shader scripts
		text = cache->buffer + cache->offset;
		ri.Com_DPrintf( "Loading shader %s from cache...\n", shortname );

		ptr = text;
		token = COM_ParseExt( &ptr, true );

		if( !ptr || token[0] != '{' ) {
			goto create_default;
		}

		while( ptr ) {
			token = COM_ParseExt( &ptr, true );

			if( !token[0] )
				break;
			else if( token[0] == '}' )
				break;
			else if( token[0] == '{' )
				Shader_Readpass( s, &ptr );
			else if( Shader_Parsetok( s, NULL, shaderkeys, token, &ptr ) )
				break;
		}

		Shader_Finish( s );
	}
	else
	{
create_default:
		// make default shader
		switch( type ) {
		case SHADER_TYPE_VERTEX:
			// vertex lighting
			data = R_Malloc( shortname_length + 1 + sizeof( shaderpass_t ) );
			s->flags = SHADER_DEPTHWRITE|SHADER_CULL_FRONT;
			s->vattribs = VATTRIB_POSITION_BIT|VATTRIB_TEXCOORDS_BIT|VATTRIB_COLOR0_BIT;
			s->sort = SHADER_SORT_OPAQUE;
			s->numpasses = 1;
			s->passes = ( shaderpass_t * )data;
			s->name = ( char * )( s->passes + 1 );
			strcpy( s->name, shortname );

			s->numpasses = 0;
			pass = &s->passes[s->numpasses++];
			pass->flags = GLSTATE_DEPTHWRITE;
			pass->tcgen = TC_GEN_BASE;
			pass->rgbgen.type = RGB_GEN_VERTEX;
			pass->alphagen.type = ALPHA_GEN_IDENTITY;
			pass->images[0] = Shader_FindImage( s, longname, 0 );
			break;
		case SHADER_TYPE_DELUXEMAP:
			// deluxemapping
			Shaderpass_LoadMaterial( &materialImages[0], &materialImages[1], &materialImages[2], shortname, 0, s->imagetags );

			data = R_Malloc( shortname_length + 1 + sizeof( shaderpass_t ) );
			s->flags = SHADER_DEPTHWRITE|SHADER_CULL_FRONT|SHADER_LIGHTMAP;
			s->vattribs = VATTRIB_POSITION_BIT|VATTRIB_TEXCOORDS_BIT|VATTRIB_LMCOORDS0_BIT|VATTRIB_NORMAL_BIT|VATTRIB_SVECTOR_BIT;
			s->sort = SHADER_SORT_OPAQUE;
			s->numpasses = 1;
			s->passes = ( shaderpass_t * )data;
			s->name = ( char * )( s->passes + 1 );
			strcpy( s->name, shortname );

			pass = &s->passes[0];
			pass->flags = GLSTATE_DEPTHWRITE;
			pass->tcgen = TC_GEN_BASE;
			pass->rgbgen.type = RGB_GEN_IDENTITY;
			pass->alphagen.type = ALPHA_GEN_IDENTITY;
			pass->program_type = GLSL_PROGRAM_TYPE_MATERIAL;
			pass->images[0] = Shader_FindImage( s, longname, 0 );
			pass->images[1] = materialImages[0]; // normalmap
			pass->images[2] = materialImages[1]; // glossmap
			pass->images[3] = materialImages[2]; // decalmap
			break;
		case SHADER_TYPE_CORONA:
			data = R_Malloc( shortname_length + 1 + sizeof( shaderpass_t ) * 1 );
			s->vattribs = VATTRIB_POSITION_BIT|VATTRIB_TEXCOORDS_BIT|VATTRIB_COLOR0_BIT;
			s->sort = SHADER_SORT_ADDITIVE;
			s->numpasses = 1;
			s->passes = ( shaderpass_t * )data;
			s->name = ( char * )( s->passes + 1 );
			s->flags = SHADER_SOFT_PARTICLE;
			strcpy( s->name, shortname );

			pass = &s->passes[0];
			pass->flags = GLSTATE_SRCBLEND_ONE|GLSTATE_DSTBLEND_ONE;
			pass->rgbgen.type = RGB_GEN_VERTEX;
			pass->alphagen.type = ALPHA_GEN_IDENTITY;
			pass->tcgen = TC_GEN_BASE;
			pass->images[0] = Shader_FindImage( s, longname, IT_SPECIAL );
			break;
		case SHADER_TYPE_DIFFUSE:
			// load material images
			Shaderpass_LoadMaterial( &materialImages[0], &materialImages[1], &materialImages[2], shortname, 0, s->imagetags );

			data = R_Malloc( shortname_length + 1 + sizeof( shaderpass_t ) );
			s->flags = SHADER_DEPTHWRITE|SHADER_CULL_FRONT;
			s->vattribs = VATTRIB_POSITION_BIT|VATTRIB_TEXCOORDS_BIT|VATTRIB_NORMAL_BIT;
			s->sort = SHADER_SORT_OPAQUE;
			s->numpasses = 1;
			s->passes = ( shaderpass_t * )data;
			s->name = ( char * )( s->passes + 1 );
			strcpy( s->name, shortname );

			pass = &s->passes[0];
			pass->flags = GLSTATE_DEPTHWRITE;
			pass->rgbgen.type = RGB_GEN_IDENTITY;
			pass->alphagen.type = ALPHA_GEN_IDENTITY;
			pass->tcgen = TC_GEN_BASE;
			pass->program_type = GLSL_PROGRAM_TYPE_MATERIAL;
			pass->images[0] = Shader_FindImage( s, longname, 0 );
			pass->images[1] = materialImages[0]; // normalmap
			pass->images[2] = materialImages[1]; // glossmap
			pass->images[3] = materialImages[2]; // decalmap
			s->vattribs |= VATTRIB_SVECTOR_BIT|VATTRIB_NORMAL_BIT;
			break;
		case SHADER_TYPE_2D:
		case SHADER_TYPE_2D_RAW:
		case SHADER_TYPE_VIDEO:
			data = R_Malloc( shortname_length + 1 + sizeof( shaderpass_t ) );

			s->flags = 0;
			s->vattribs = VATTRIB_POSITION_BIT|VATTRIB_TEXCOORDS_BIT|VATTRIB_COLOR0_BIT;
			s->sort = SHADER_SORT_ADDITIVE;
			s->numpasses = 1;
			s->passes = ( shaderpass_t * )data;
			s->name = ( char * )( s->passes + 1 );
			strcpy( s->name, shortname );

			pass = &s->passes[0];
			pass->flags = GLSTATE_SRCBLEND_SRC_ALPHA|GLSTATE_DSTBLEND_ONE_MINUS_SRC_ALPHA;
			pass->rgbgen.type = RGB_GEN_VERTEX;
			pass->alphagen.type = ALPHA_GEN_VERTEX;
			pass->tcgen = TC_GEN_BASE;
			if( type == SHADER_TYPE_VIDEO )
			{
				// we don't want "video/" to be there since it's hardcoded into cinematics
				if( !Q_strnicmp( shortname, "video/", 6 ) )
					pass->cin = R_StartCinematic( shortname+6 );
				else
					pass->cin = R_StartCinematic( shortname );
				s->cin = pass->cin;
				pass->images[0] = rsh.noTexture;
			} else if( type != SHADER_TYPE_2D_RAW ) {
				pass->images[0] = Shader_FindImage( s, longname, IT_SPECIAL|IT_SYNC );
			}
			break;
		case SHADER_TYPE_OPAQUE_ENV:
			// pad to 4 floats
			data = R_Malloc( ALIGN( sizeof( shaderpass_t ), 16 ) + 4 * sizeof( float ) + shortname_length + 1 );

			s->vattribs = VATTRIB_POSITION_BIT;
			s->sort = SHADER_SORT_OPAQUE;
			s->flags = SHADER_CULL_FRONT|SHADER_DEPTHWRITE;
			s->numpasses = 1;
			s->passes = ( shaderpass_t * )( data );
			s->passes[0].rgbgen.args = ( float * )((uint8_t *)data + ALIGN( sizeof( shaderpass_t ), 16 ));
			s->name = ( char * )( s->passes[0].rgbgen.args + 4 );
			strcpy( s->name, shortname );

			pass = &s->passes[0];
			pass->flags = GLSTATE_DEPTHWRITE;
			pass->rgbgen.type = RGB_GEN_ENVIRONMENT;
			VectorClear( pass->rgbgen.args );
			pass->alphagen.type = ALPHA_GEN_IDENTITY;
			pass->tcgen = TC_GEN_NONE;
			pass->images[0] = rsh.whiteTexture;
			break;
		case SHADER_TYPE_SKYBOX:
			data = R_Malloc( shortname_length + 1 + sizeof( shaderpass_t ) );

			s->vattribs = VATTRIB_POSITION_BIT|VATTRIB_TEXCOORDS_BIT;
			s->sort = SHADER_SORT_SKY;
			s->flags = SHADER_CULL_FRONT|SHADER_SKY;
			s->numpasses = 1;
			s->passes = ( shaderpass_t * )data;
			s->name = ( char * )( s->passes + 1 );
			strcpy( s->name, shortname );

			pass = &s->passes[0];
			pass->rgbgen.type = RGB_GEN_IDENTITY;
			pass->alphagen.type = ALPHA_GEN_IDENTITY;
			pass->tcgen = TC_GEN_BASE;
			pass->flags = SHADERPASS_SKYBOXSIDE;
			// the actual image will be picked at rendering time based on skyside number
			pass->images[0] = rsh.whiteTexture;
			break;
		case SHADER_TYPE_FOG:
			data = R_Malloc( shortname_length + 1 );

			s->vattribs = VATTRIB_POSITION_BIT|VATTRIB_TEXCOORDS_BIT;
			s->sort = SHADER_SORT_FOG;
			s->flags = SHADER_CULL_FRONT;
			s->numpasses = 0;
			s->name = ( char * )data;
			strcpy( s->name, shortname );
			break;
		default:
			break;
		}
	}

	s->registrationSequence = rsh.registrationSequence;
}

/*
* R_PackShaderOrder
*
* Sort opaque shaders by this value for better caching of GL/program state.
*/
unsigned R_PackShaderOrder( const shader_t *shader )
{
	int order;
	int program_type;
	const shaderpass_t *pass;

	if( !shader->numpasses )
		return 0;

	pass = &shader->passes[0];
	program_type = pass->program_type;

	if( program_type == GLSL_PROGRAM_TYPE_MATERIAL ) {
		// this is not a material shader in case all images are missing except for the defuse
		if( ( !pass->images[1] || pass->images[1]->missing || pass->images[1] == rsh.blankBumpTexture ) &&
			( !pass->images[2] || pass->images[2]->missing ) &&
			( !pass->images[3] || pass->images[3]->missing ) &&
			( !pass->images[4] || pass->images[4]->missing ) )
			program_type = GLSL_PROGRAM_TYPE_Q3A_SHADER;
	}

	// sort by base program type
	order = program_type & 0x1F;

	// check presence of gloss for materials
	if( program_type == GLSL_PROGRAM_TYPE_MATERIAL && pass->images[2] != NULL && !pass->images[2]->missing )
		order |= 0x20;

	return order;
}

/*
* R_ShaderById
*/
shader_t *R_ShaderById( unsigned int id )
{
	assert( id < MAX_SHADERS );
	if( id >= MAX_SHADERS ) {
		return NULL;
	}
	return r_shaders + id;
}

/*
* R_TouchShadersByName
*/
void R_TouchShadersByName( const char *name )
{
	unsigned int shortNameSize;
	char *shortName;
	unsigned int nameLength;
	unsigned int key;
	shader_t *hnode, *s;

	if( !name || !name[0] )
		return;

	shortNameSize = strlen( name ) + 1;
	shortName = alloca( shortNameSize );
	nameLength = R_ShaderCleanName( name, shortName, shortNameSize );
	if( !nameLength )
		return;

	key = COM_SuperFastHash( ( const uint8_t * )shortName, nameLength, nameLength ) % SHADERS_HASH_SIZE;
	hnode = &r_shaders_hash_headnode[key];
	for( s = hnode->next; s != hnode; s = s->next ) {
		if( !strcmp( s->name, shortName ) ) {
			R_TouchShader( s );
		}
	}
}

/*
* R_LoadShader
*/
shader_t *R_LoadShader( const char *name, shaderType_e type, bool forceDefault )
{
	unsigned int key, nameLength;
	char *shortname;
	shader_t *s;
	shader_t *hnode, *prev, *next;

	if( !name || !name[0] )
		return NULL;

	// alloc buffer to hold clean shader name
	nameLength = strlen( name );
	if( nameLength + 1 > r_shortShaderNameSize || r_shortShaderNameSize < MAX_QPATH ) {
		if( r_shortShaderName ) {
			R_Free( r_shortShaderName );
			r_shortShaderName = NULL;
		}
		r_shortShaderNameSize = max( nameLength + 1, MAX_QPATH );
		r_shortShaderName = R_Malloc( r_shortShaderNameSize );
	}

	shortname = r_shortShaderName;
	nameLength = R_ShaderCleanName( name, r_shortShaderName, r_shortShaderNameSize );
	if( !nameLength )
		return NULL;

	// test if already loaded
	key = COM_SuperFastHash( ( const uint8_t *)shortname, nameLength, nameLength ) % SHADERS_HASH_SIZE;
	hnode = &r_shaders_hash_headnode[key];

	// scan all instances of the same shader for exact match of the type
	for( s = hnode->next; s != hnode; s = s->next ) {
		if( strcmp( s->name, shortname ) ) {
			continue;
		}
		if( s->type == type ) {
			// exact match found
			R_TouchShader( s );
			return s;
		}
		if( ( type == SHADER_TYPE_2D ) && ( s->type == SHADER_TYPE_2D_RAW ) ) {
			// almost exact match:
			// alias SHADER_TYPE_2D_RAW to SHADER_TYPE_2D so the shader can be "touched" by name
			R_TouchShader( s );
			return s;
		}
	}

	if( !r_free_shaders ) {
		ri.Com_Error( ERR_FATAL, "R_LoadShader: Shader limit exceeded" );
	}

	s = r_free_shaders;
	r_free_shaders = s->next;

	prev = s->prev;
	next = s->next;
	memset( s, 0, sizeof( shader_t ) );
	s->next = next;
	s->prev = prev;
	s->id = s - r_shaders;
	R_LoadShaderReal( s, shortname, nameLength, name, type, forceDefault );

	// add to linked lists
	s->prev = hnode;
	s->next = hnode->next;
	s->next->prev = s;
	s->prev->next = s;

	return s;
}

/*
* R_RegisterPic
*/
shader_t *R_RegisterPic( const char *name )
{
	return R_LoadShader( name, SHADER_TYPE_2D, false );
}

/*
* R_RegisterRawPic_
*
* Registers default 2D shader with base image provided as raw data.
*/
shader_t *R_RegisterRawPic_( const char *name, int width, int height, uint8_t *data, int flags, int samples )
{
	shader_t *s;
	int type;

	type = SHADER_TYPE_2D_RAW;
	flags |= IT_SPECIAL;

	s = R_LoadShader( name, type, true );
	if( s ) {
		image_t *image;

		// unlink and delete the old image from memory, unless it's the default one
		image = s->passes[0].images[0];
		if( !image || image == rsh.noTexture ) {
			// try to load new image
			image = R_LoadImage( name, &data, width, height, flags, 1, IMAGE_TAG_GENERIC, samples );
			s->passes[0].images[0] = image;
		}
		else {
			// replace current texture data
			R_ReplaceImage( image, &data, width, height, image->flags, 1, image->samples );
		}
	}
	return s;
}

/*
* R_RegisterRawPic
*
* Registers default 2D shader with base image provided as raw color data.
*/
shader_t *R_RegisterRawPic( const char *name, int width, int height, uint8_t *data, int samples )
{
	return R_RegisterRawPic_( name, width, height, data, 0, samples );
}

/*
* R_RegisterRawAlphaMask
*
* Registers default alpha mask shader with base image provided as raw alpha values.
*/
shader_t *R_RegisterRawAlphaMask( const char *name, int width, int height, uint8_t *data )
{
	return R_RegisterRawPic_( name, width, height, data, IT_ALPHAMASK, 1 );
}

/*
* R_RegisterLevelshot
*/
shader_t *R_RegisterLevelshot( const char *name, shader_t *defaultShader, bool *matchesDefault )
{
	shader_t *shader;

	r_defaultImage = defaultShader ? defaultShader->passes[0].images[0] : NULL;
	shader = R_LoadShader( name, SHADER_TYPE_2D, true );

	if( matchesDefault )
		*matchesDefault = ((shader->passes[0].images[0] == r_defaultImage) ? true : false);

	r_defaultImage = NULL;

	return shader;
}

/*
* R_RegisterShader
*/
shader_t *R_RegisterShader( const char *name, shaderType_e type )
{
	return R_LoadShader( name, type, false );
}

/*
* R_RegisterSkin
*/
shader_t *R_RegisterSkin( const char *name )
{
	return R_LoadShader( name, SHADER_TYPE_DIFFUSE, false );
}

/*
* R_RegisterVideo
*/
shader_t *R_RegisterVideo( const char *name )
{
	return R_LoadShader( name, SHADER_TYPE_VIDEO, false );
}

/*
* R_RemapShader
*/
void R_RemapShader( const char *from, const char *to, int timeOffset )
{
}

/*
* R_GetShaderDimensions
*
* Returns dimensions for shader's base (taken from the first pass) image
*/
void R_GetShaderDimensions( const shader_t *shader, int *width, int *height )
{
	image_t *baseImage;

	assert( shader );
	if( !shader || !shader->numpasses ) {
		return;
	}

	baseImage = shader->passes[0].images[0];
	if( !baseImage ) {
		ri.Com_DPrintf( S_COLOR_YELLOW "R_GetShaderDimensions: shader %s is missing base image\n", shader->name );
		return;
	}

	if( width ) {
		*width = baseImage->width;
	}
	if( height ) {
		*height = baseImage->height;
	}
}

/*
* R_ReplaceRawSubPic
*
* Adds a new subimage to the specified raw pic.
* Must not be used to overwrite previously written areas when doing batched drawing.
*/
void R_ReplaceRawSubPic( shader_t *shader, int x, int y, int width, int height, uint8_t *data )
{
	image_t *baseImage;

	assert( shader );
	if( !shader )
		return;

	assert( shader->type == SHADER_TYPE_2D_RAW );
	if( shader->type != SHADER_TYPE_2D_RAW )
		return;

	baseImage = shader->passes[0].images[0];

	assert( ( ( x + width ) <= baseImage->upload_width ) && ( ( y + height ) <= baseImage->upload_height ) );
	if( ( ( x + width ) > baseImage->upload_width ) || ( ( y + height ) > baseImage->upload_height ) )
		return;

	R_ReplaceSubImage( baseImage, 0, x, y, &data, width, height );
}
