/*
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
#ifndef R_PUBLIC_H
#define R_PUBLIC_H

#include "../cgame/ref.h"

#define REF_API_VERSION 21

struct mempool_s;
struct cinematics_s;

typedef struct qthread_s qthread_t;
typedef struct qmutex_s qmutex_t;
typedef struct qbufPipe_s qbufPipe_t;

//
// these are the functions exported by the refresh module
//
typedef struct
{
	// halts the application or drops to console
	void ( *Com_Error )( com_error_code_t code, const char *format, ... );

	// console messages
	void ( *Com_Printf )( const char *format, ... );
	void ( *Com_DPrintf )( const char *format, ... );

	// console variable interaction
	cvar_t *( *Cvar_Get )( const char *name, const char *value, int flags );
	cvar_t *( *Cvar_Set )( const char *name, const char *value );
	cvar_t *( *Cvar_ForceSet )( const char *name, const char *value );      // will return 0 0 if not found
	void ( *Cvar_SetValue )( const char *name, float value );
	float ( *Cvar_Value )( const char *name );
	const char *( *Cvar_String )( const char *name );

	int ( *Cmd_Argc )( void );
	char *( *Cmd_Argv )( int arg );
	char *( *Cmd_Args )( void );        // concatenation of all argv >= 1
	void ( *Cmd_AddCommand )( const char *name, void ( *cmd )( void ) );
	void ( *Cmd_RemoveCommand )( const char *cmd_name );
	void ( *Cmd_ExecuteText )( int exec_when, const char *text );
	void ( *Cmd_Execute )( void );
	void ( *Cmd_SetCompletionFunc )( const char *cmd_name, char **( *completion_func )( const char *partial ) );

	unsigned int ( *Sys_Milliseconds )( void );
	uint64_t ( *Sys_Microseconds )( void );
	void ( *Sys_Sleep )( unsigned int milliseconds );

	void *( *Com_LoadSysLibrary )( const char *name, dllfunc_t *funcs );
	void ( *Com_UnloadLibrary )( void **lib );
	void *( *Com_LibraryProcAddress )( void *lib, const char *name );

	int ( *FS_FOpenFile )( const char *filename, int *filenum, int mode );
	int ( *FS_FOpenAbsoluteFile )( const char *filename, int *filenum, int mode );
	int ( *FS_Read )( void *buffer, size_t len, int file );
	int ( *FS_Write )( const void *buffer, size_t len, int file );
	int ( *FS_Printf )( int file, const char *format, ... );
	int ( *FS_Tell )( int file );
	int ( *FS_Seek )( int file, int offset, int whence );
	int ( *FS_Eof )( int file );
	int ( *FS_Flush )( int file );
	void ( *FS_FCloseFile )( int file );
	bool ( *FS_RemoveFile )( const char *filename );
	int ( *FS_GetFileList )( const char *dir, const char *extension, char *buf, size_t bufsize, int start, int end );
	int ( *FS_GetGameDirectoryList )( char *buf, size_t bufsize );
	const char *( *FS_FirstExtension )( const char *filename, const char *extensions[], int num_extensions );
	bool ( *FS_MoveFile )( const char *src, const char *dst );
	bool ( *FS_IsUrl )( const char *url );
	time_t ( *FS_FileMTime )( const char *filename );
	bool ( *FS_RemoveDirectory )( const char *dirname );
	const char * ( *FS_GameDirectory )( void );
	const char * ( *FS_WriteDirectory )( void );
	const char * ( *FS_MediaDirectory )( fs_mediatype_t type );
	void ( *FS_AddFileToMedia )( const char *filename );

	struct cinematics_s *( *CIN_Open )( const char *name, unsigned int start_time, bool *yuv, float *framerate );
	bool ( *CIN_NeedNextFrame )( struct cinematics_s *cin, unsigned int curtime );
	uint8_t *( *CIN_ReadNextFrame )( struct cinematics_s *cin, int *width, int *height, 
		int *aspect_numerator, int *aspect_denominator, bool *redraw );
	ref_yuv_t *( *CIN_ReadNextFrameYUV )( struct cinematics_s *cin, int *width, int *height, 
		int *aspect_numerator, int *aspect_denominator, bool *redraw );
	void ( *CIN_Reset )( struct cinematics_s *cin, unsigned int cur_time );
	void ( *CIN_Close )( struct cinematics_s *cin );

	struct mempool_s *( *Mem_AllocPool )( struct mempool_s *parent, const char *name, const char *filename, int fileline );
	void ( *Mem_FreePool )( struct mempool_s **pool, const char *filename, int fileline );
	void ( *Mem_EmptyPool )( struct mempool_s *pool, const char *filename, int fileline );
	void *( *Mem_AllocExt )( struct mempool_s *pool, size_t size, size_t alignment, int z, const char *filename, int fileline );
	void ( *Mem_Free )( void *data, const char *filename, int fileline );
	void *( *Mem_Realloc )( void *data, size_t size, const char *filename, int fileline );
	size_t ( *Mem_PoolTotalSize )( struct mempool_s *pool );

	// multithreading
	struct qthread_s *( *Thread_Create )( void *(*routine) (void*), void *param );
	void ( *Thread_Join )( struct qthread_s *thread );
	void ( *Thread_Yield )( void );
	struct qmutex_s *( *Mutex_Create )( void );
	void ( *Mutex_Destroy )( struct qmutex_s **mutex );
	void ( *Mutex_Lock )( struct qmutex_s *mutex );
	void ( *Mutex_Unlock )( struct qmutex_s *mutex );

	qbufPipe_t *( *BufPipe_Create )( size_t bufSize, int flags );
	void ( *BufPipe_Destroy )( qbufPipe_t **pqueue );
	void ( *BufPipe_Finish )( qbufPipe_t *queue );
	void ( *BufPipe_WriteCmd )( qbufPipe_t *queue, const void *cmd, unsigned cmd_size );
	int ( *BufPipe_ReadCmds )( qbufPipe_t *queue, unsigned (**cmdHandlers)( const void * ) );
	void ( *BufPipe_Wait )( qbufPipe_t *queue, int (*read)( qbufPipe_t *, unsigned( ** )(const void *), bool ), 
		unsigned (**cmdHandlers)( const void * ), unsigned timeout_msec );
} ref_import_t;

typedef struct
{
	// if API is different, the dll cannot be used
	int			( *API )( void );

	rserr_t		( *Init )( const char *applicationName, const char *screenshotsPrefix, int startupColor,
					int iconResource, const int *iconXPM, void *hinstance, void *wndproc, void *parenthWnd, bool verbose );
	rserr_t		( *SetMode )( int x, int y, int width, int height, int displayFrequency, bool fullScreen, bool stereo );
	rserr_t		( *SetWindow )( void *hinstance, void *wndproc, void *parenthWnd );

	void		( *Shutdown )( bool verbose );

	// All data that will be used in a level should be
	// registered before rendering any frames to prevent disk hits,
	// but they can still be registered at a later time
	// if necessary.
	//
	// EndRegistration will free any remaining data that wasn't registered.
	// Any model_s, shader_s and skinfile_s pointers from before 
	// the BeginRegistration are no longer valid after EndRegistration.
	void		( *BeginRegistration )( void );
	void		( *EndRegistration )( void );

	void		( *ModelBounds )( const struct model_s *model, vec3_t mins, vec3_t maxs );
	void		( *ModelFrameBounds )( const struct model_s *model, int frame, vec3_t mins, vec3_t maxs );

	void		( *RegisterWorldModel )( const char *model, const dvis_t *pvsData );
	struct model_s *( *RegisterModel )( const char *name );
	struct shader_s *( *RegisterPic )( const char *name );
	struct shader_s *( *RegisterRawPic )( const char *name, int width, int height, uint8_t *data, int samples );
	struct shader_s *( *RegisterRawAlphaMask )( const char *name, int width, int height, uint8_t *data );
	struct shader_s *( *RegisterLevelshot )( const char *name, struct shader_s *defaultShader, bool *matchesDefault );
	struct shader_s *( *RegisterSkin )( const char *name );
	struct skinfile_s *( *RegisterSkinFile )( const char *name );
	struct shader_s *( *RegisterVideo )( const char *name );

	void		( *RemapShader )( const char *from, const char *to, int timeOffset );
	void		( *GetShaderDimensions )( const struct shader_s *shader, int *width, int *height );

	void		( *ReplaceRawSubPic )( struct shader_s *shader, int x, int y, int width, int height, uint8_t *data );

	void		( *ClearScene )( void );
	void		( *AddEntityToScene )( const entity_t *ent );
	void		( *AddLightToScene )( const vec3_t org, float intensity, float r, float g, float b );
	void		( *AddPolyToScene )( const poly_t *poly );
	void		( *AddLightStyleToScene )( int style, float r, float g, float b );
	void		( *RenderScene )( const refdef_t *fd );

	void		( *DrawStretchPic )( int x, int y, int w, int h, float s1, float t1, float s2, float t2, 
								 const float *color, const struct shader_s *shader );
	void		( *DrawRotatedStretchPic )( int x, int y, int w, int h, float s1, float t1, float s2, float t2, 
								 float angle, const vec4_t color, const struct shader_s *shader );

	// Passing NULL for data redraws last uploaded frame
	void		( *DrawStretchRaw )( int x, int y, int w, int h, int cols, int rows, 
									float s1, float t1, float s2, float t2, uint8_t *data );

	// Passing NULL for yuv redraws last uploaded frame
	void		( *DrawStretchRawYUV )( int x, int y, int w, int h, 
										float s1, float t1, float s2, float t2, ref_img_plane_t *yuv );

	void		( *DrawStretchPoly )( const poly_t *poly, float x_offset, float y_offset );
	void		( *Scissor )( int x, int y, int w, int h );
	void		( *GetScissor )( int *x, int *y, int *w, int *h );
	void		( *ResetScissor )( void );

	void		( *SetCustomColor )( int num, int r, int g, int b );
	void		( *LightForOrigin )( const vec3_t origin, vec3_t dir, vec4_t ambient, vec4_t diffuse, float radius );

	bool	( *LerpTag )( orientation_t *orient, const struct model_s *mod, int oldframe, int frame, float lerpfrac,
						  const char *name );

	int			( *SkeletalGetNumBones )( const struct model_s *mod, int *numFrames );
	int			( *SkeletalGetBoneInfo )( const struct model_s *mod, int bone, char *name, size_t name_size, int *flags );
	void		( *SkeletalGetBonePose )( const struct model_s *mod, int bone, int frame, bonepose_t *bonepose );

	int			( *GetClippedFragments )( const vec3_t origin, float radius, vec3_t axis[3], int maxfverts, vec4_t *fverts, 
									  int maxfragments, fragment_t *fragments );

	struct shader_s * ( *GetShaderForOrigin )( const vec3_t origin );
	struct cinematics_s * ( *GetShaderCinematic )( struct shader_s *shader );

	void		( *TransformVectorToScreen )( const refdef_t *rd, const vec3_t in, vec2_t out );

	// Should only be used as a hint - the renderer may keep drawing or not drawing to the window for a few frames when this changes
	bool		( *RenderingEnabled )( void );

	void		( *BeginFrame )( float cameraSeparation, bool forceClear, bool forceVsync );
	void		( *EndFrame )( void );
	const char *( *GetSpeedsMessage )( char *out, size_t size );
	int			( *GetAverageFramerate )( void );

	void		( *BeginAviDemo )( void );
	void		( *WriteAviFrame )( int frame, bool scissor );
	void		( *StopAviDemo )( void );

	void		( *AppActivate )( bool active, bool destroy );
} ref_export_t;

typedef ref_export_t *(*GetRefAPI_t)(const ref_import_t *imports);

#ifdef REF_HARD_LINKED
#ifdef __cplusplus
extern "C" {
#endif
	ref_export_t *GetRefAPI( ref_import_t *import );
#ifdef __cplusplus
}
#endif
#endif

#endif // R_PUBLIC_H
