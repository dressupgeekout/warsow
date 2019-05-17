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
// snd_loc.h -- private sound functions

// it's in qcommon.h too, but we don't include it for modules
typedef struct { char *name; void **funcPointer; } dllfunc_t;

#include "../gameshared/q_arch.h"
#include "../gameshared/q_math.h"
#include "../gameshared/q_shared.h"
#include "../gameshared/q_cvar.h"

#include "../client/snd_public.h"
#include "snd_syscalls.h"

// !!! if this is changed, the asm code must change !!!
typedef struct
{
	int left;
	int right;
} portable_samplepair_t;

typedef struct
{
	unsigned int length;
	unsigned int loopstart;
	unsigned int speed;              // not needed, because converted on load?
	unsigned short channels;
	unsigned short width;
	uint8_t data[1];          // variable sized
} sfxcache_t;

typedef struct sfx_s
{
	char name[MAX_QPATH];
	int registration_sequence;
	bool isUrl;
	sfxcache_t *cache;
} sfx_t;

typedef struct
{
	sfx_t *sfx;
	int entnum;
	float volume;
	float attenuation;
} loopsfx_t;

typedef struct
{
	int entnum;
	float volume;
	float attenuation;
	int left_volume, right_volume;
	volatile unsigned int rawend;
	portable_samplepair_t rawsamples[1];
} rawsound_t;

// a playsound_t will be generated by each call to S_StartSound,
// when the mixer reaches playsound->begin, the playsound will
// be assigned to a channel
typedef struct playsound_s
{
	struct playsound_s *prev, *next;
	sfx_t *sfx;
	float volume;
	float attenuation;
	int entnum;
	int entchannel;
	bool fixed_origin;  // use origin field instead of entnum's origin
	vec3_t origin;
	unsigned begin;         // begin on this sample
} playsound_t;

typedef struct
{
	unsigned short channels;
	unsigned int samples;                    // mono samples in buffer
	unsigned int submission_chunk;           // don't mix less than this #
	unsigned int samplepos;                  // in mono samples
	unsigned int samplebits;
	unsigned int speed;
	float msec_per_sample;
	uint8_t *buffer;
} dma_t;

typedef struct
{
	sfx_t *sfx;             // sfx number
	int leftvol;            // 0-255 volume
	int rightvol;           // 0-255 volume
	unsigned int end;       // end time in global paintsamples
	unsigned int pos;       // sample position in sfx
	int entnum;             // to allow overriding a specific sound
	int entchannel;         //
	vec3_t origin;          // only use if fixed_origin is set
	vec_t dist_mult;        // distance multiplier (attenuation/clipK)
	int master_vol;         // 0-255 master volume
	bool fixed_origin;  // use origin instead of fetching entnum's origin
	bool autosound;     // from an entity->sound, cleared each frame
	int	lpf_lcoeff;			// lowpass filters coefficient (0-0xffff) for both ears
	int lpf_rcoeff;
	int lpf_history[4];		// lowpass IIR chain 2-pole, 2-chan = 4 samples
	unsigned int ldelay;	// invidual ear delay offset for both channels
	unsigned int rdelay;
	rawsound_t *rawsamples;	// got no static sfx, read samples directly
} channel_t;

typedef struct
{
	int rate;
	short width;
	short channels;
	int loopstart;
	int samples;
	int dataofs;            // chunk starts this many bytes from file start
} wavinfo_t;

typedef struct bgTrack_s
{
	char *filename;
	bool ignore;
	int file;
	wavinfo_t info;
	bool isUrl;
	bool loop;
	bool muteOnPause;

	void *vorbisFile;
	bool ( *open )( struct bgTrack_s *track, bool *delay );
	int ( *read )( struct bgTrack_s *track, void *ptr, size_t size );
	int ( *seek )( struct bgTrack_s *track, int pos );
	void ( *close )( struct bgTrack_s *track );

	struct bgTrack_s *next; // the next track to be played, the looping part aways points to itself
	struct bgTrack_s *prev; // previous track in the playlist
	struct bgTrack_s *anext; // allocation linked list
} bgTrack_t;

typedef struct
{
	vec3_t origin;
	vec3_t velocity;
} entity_spatialization_t;

int S_API( void );
void S_Error( const char *format, ... );
void S_Activate( bool active );

void *S_BackgroundUpdateProc( void *param );

void S_ClearPaintBuffer( void );

// music cinema

void S_StartBackgroundTrack( const char *intro, const char *loop, int mode );
void S_StopBackgroundTrack( void );
void S_PauseBackgroundTrack( void );
void S_LockBackgroundTrack( bool lock );
void S_PrevBackgroundTrack( void );
void S_NextBackgroundTrack( void );
void S_UpdateBackgroundTrack( void );

void S_RawSamples2( unsigned int samples, unsigned int rate, unsigned short width, 
	unsigned short channels, const uint8_t *data, int snd_vol );
unsigned int S_GetRawSamplesLength( void );
unsigned int S_GetPositionedRawSamplesLength( int entnum );

/*
====================================================================

SYSTEM SPECIFIC FUNCTIONS

====================================================================
*/

// initializes cycling through a DMA buffer and returns information on it
bool SNDDMA_Init( void *hwnd, bool verbose );

// gets the current DMA position
int	SNDDMA_GetDMAPos( void );

// shutdown the DMA xfer.
void	SNDDMA_Shutdown( bool verbose );

void	SNDDMA_BeginPainting( void );

void	SNDDMA_Submit( void );

void	SNDOGG_Init( bool verbose );
void	SNDOGG_Shutdown( bool verbose );
bool SNDOGG_OpenTrack( bgTrack_t *track, bool *delay );
sfxcache_t *SNDOGG_Load( sfx_t *s );

//====================================================================

// during registration it is possible to have more sounds
// than could actually be referenced during gameplay,
// because we don't want to free anything until we are
// sure we won't need it.
#define	    MAX_SFX	512
extern sfx_t known_sfx[MAX_SFX];
extern int num_sfx;

#define	MAX_CHANNELS		128
extern channel_t channels[MAX_CHANNELS];

extern volatile unsigned int paintedtime;
extern dma_t dma;
extern playsound_t s_pendingplays;

extern bool s_active;

#define	MAX_RAW_SAMPLES	16384

#define	MAX_RAW_SOUNDS 16
extern rawsound_t *raw_sounds[MAX_RAW_SOUNDS];

extern cvar_t *developer;

extern cvar_t *s_volume;
extern cvar_t *s_musicvolume;
extern cvar_t *s_nosound;
extern cvar_t *s_khz;
extern cvar_t *s_show;
extern cvar_t *s_mixahead;
extern cvar_t *s_testsound;
extern cvar_t *s_swapstereo;
extern cvar_t *s_pseudoAcoustics;
extern cvar_t *s_separationDelay;
extern cvar_t *s_globalfocus;

extern struct mempool_s *soundpool;

#define S_MemAlloc( pool, size ) trap_MemAlloc( pool, size, __FILE__, __LINE__ )
#define S_MemFree( mem ) trap_MemFree( mem, __FILE__, __LINE__ )
#define S_MemAllocPool( name ) trap_MemAllocPool( name, __FILE__, __LINE__ )
#define S_MemFreePool( pool ) trap_MemFreePool( pool, __FILE__, __LINE__ )
#define S_MemEmptyPool( pool ) trap_MemEmptyPool( pool, __FILE__, __LINE__ )

#define S_Malloc( size ) S_MemAlloc( soundpool, size )
#define S_Free( data ) S_MemFree( data )

wavinfo_t GetWavinfo( const char *name, uint8_t *wav, int wavlength );
unsigned int ResampleSfx( unsigned int numsamples, unsigned int speed, unsigned short channels, unsigned short width, const uint8_t *data, uint8_t *outdata, char *name );

void S_InitScaletable( void );

sfxcache_t *S_LoadSound( sfx_t *s );

void S_IssuePlaysound( playsound_t *ps );

int S_PaintChannels( unsigned int endtime, int dumpfile, float gain );

//====================================================================

/*
* Exported functions
*/
bool SF_Init( void *hwnd, int maxEntities, bool verbose );
void SF_Shutdown( bool verbose );
void SF_EndRegistration( void );
void SF_BeginRegistration( void );
sfx_t *SF_RegisterSound( const char *name );
void SF_StartBackgroundTrack( const char *intro, const char *loop, int mode );
void SF_StopBackgroundTrack( void );
void SF_LockBackgroundTrack( bool lock );
void SF_StopAllSounds( bool clear, bool stopMusic );
void SF_PrevBackgroundTrack( void );
void SF_NextBackgroundTrack( void );
void SF_PauseBackgroundTrack( void );
void SF_Activate( bool active );
void SF_BeginAviDemo( void );
void SF_StopAviDemo( void );
void SF_SetAttenuationModel( int model, float maxdistance, float refdistance );
void SF_SetEntitySpatialization( int entnum, const vec3_t origin, const vec3_t velocity );
void SF_SetAttenuationModel( int model, float maxdistance, float refdistance );
void SF_StartFixedSound( sfx_t *sfx, const vec3_t origin, int channel, float fvol, float attenuation );
void SF_StartRelativeSound( sfx_t *sfx, int entnum, int channel, float fvol, float attenuation );
void SF_StartGlobalSound( sfx_t *sfx, int channel, float fvol );
void SF_StartLocalSound( const char *sound );
void SF_Clear( void );
void SF_AddLoopSound( sfx_t *sfx, int entnum, float fvol, float attenuation );
void SF_Update( const vec3_t origin, const vec3_t velocity, const mat3_t axis, bool avidump );
void SF_RawSamples( unsigned int samples, unsigned int rate, unsigned short width, 
	unsigned short channels, const uint8_t *data, bool music );
void SF_PositionedRawSamples( int entnum, float fvol, float attenuation, 
	unsigned int samples, unsigned int rate, 
	unsigned short width, unsigned short channels, const uint8_t *data );

//====================================================================

// Lowpass code ripped from OpenAL software implementation
static inline float S_LowpassCoeff(float gain, float cw )
{
	float a = 0.0;
	float g = gain;

	g = max( g, 0.01 );
	if( g < 0.9999f )
		a = (1.0 - g*cw - sqrt(2.0*g*(1.0-cw) - g*g*(1.0-cw*cw))) / (1.0 - g);

	return a;
}

static inline float S_LowpassCW(float freq, float samplerate)
{
	return cos( 2.0 * M_PI * freq / samplerate );
}

static inline int S_Lowpass2pole( int sample, int *history, int coeff )
{
	int output = sample;
	output += (( history[0] - output ) * coeff) >> 16;
	history[0] = output;
	output += (( history[1] - output ) * coeff) >> 16;
	history[1] = output;
	return output;
}
