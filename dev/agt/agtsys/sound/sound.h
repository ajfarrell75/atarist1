//======================================================================================================================
//	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	YM sound interface
//----------------------------------------------------------------------------------------------------------------------
//	******************************************************************************************************
//	******************************************************************************************************
//	todo: THIS IS A GIANT MESS. but most of this is now handled in the 68k module and is due for deletion
//	so just look away for now and some day it will look tidy
//	******************************************************************************************************
//	******************************************************************************************************
//----------------------------------------------------------------------------------------------------------------------

#ifndef sound_h_
#define sound_h_

//-----------------------------------------------------------------------------

#include "sounds.h"

//-----------------------------------------------------------------------------

#define dma_channels (2)					// hardware output side is always stereo
typedef u32 osample_t;						// hardware output sample 'footprint' fixed at 2 shorts or 4 bytes
#define dma_bytes_per_channel (2)
#define dma_bytes_per_sample (dma_bytes_per_channel * dma_channels)

// volume-adjusted samples are mono or stereo depending on config (todo: make this dynamic @ runtime)
#ifdef ENABLE_AUDIOPANNING
typedef u32 vsample_t;
#else
typedef u16 vsample_t;
#endif

//-----------------------------------------------------------------------------

// this sets the DMA buffer size in terms of samples and also sets the
// buffer filling rate by implication (larger buffer = fewer interrupts, less setup code + greater sound latency)
#define samples_per_frame (5)//(256*4)

// approx milliseconds per DMA frame, used to schedule MIDI events within buffer
#define ms_per_frame (5) //(/*84*/((samples_per_frame*1000)+999)/DMA_SAMPLE_RATE)

#define sampledata_per_frame (samples_per_frame * dma_channels)
#define dma_bytes_per_frame (samples_per_frame * dma_bytes_per_sample)

// configure degree of decoupling between read/write sections of DMA ringbuffer (>2 only useful for debugging)
// must be 2^n 
#define max_frames (4)
#define dma_frame_isolation (max_frames/2)

// maximum number of pitchbend-affected channels which can be mixed in a single pass. 
// more will need multiple passes (e.g. 5 channels would require 3+2)
#define max_combined_pbend_channels (3)

//-----------------------------------------------------------------------------

#define max_pan_stateshift (0)				// panning disabled (mono output)
#define max_vol_stateshift (5)				// can afford some extra volume bits as a trade


#define max_pan_states (1<<max_pan_stateshift)
#define max_vol_states (1<<max_vol_stateshift)

// indicates pan value for 'centered' sound, equal volume at left/right
#define pan_centre ((max_pan_states/2)+0)

//-----------------------------------------------------------------------------

// somewhat arbitrary upper limit on the number of active virtual channels incl. music and sfx together
#define max_virtual_channels 48//((max_midi_channels+3)*2)		// midi + sfx

// max number of channels which might be killed during a single audio frame
#define max_toremove_channels (max_virtual_channels)

// min physical channels outputting to DMA buffer at any time - typically 1, but can be set equal to
// max_physical_channels to assist with performance testing or debugging of a specific channel count
#define min_physical_channels (3)

// max physical channels which can be mixed at any time - this can be raised by only at disproportionate
// CPU cost (by applying more than one pass to the mix buffer) since 6 is the maximum number of sources 
// supported by a single loop using the current impl... note that pitchbend-affected channels can't be
// sourced from the resampling cache and must use a special mixer - either one or more of the 6 channels
// sourced in a single pass, or by applying a separate pass later.
#ifdef USE_RESAMPLING_CACHE
#ifdef USE_EXTENDED_AUDIO
 #define max_physical_channels (3)
#else
 #define max_physical_channels (3)
#endif
#else
#ifdef USE_EXTENDED_AUDIO
 #define max_physical_channels (3)
#else
 #define max_physical_channels (3)
#endif
#endif
#define max_physical_channels_renorm (16) // safe for up to 8 channels
#define boost_amp (1)


#define max_resampling_channels (6)

#define max_ym_channels (3)

//-----------------------------------------------------------------------------

// sets the maximum number of events which can be fed to the SFX or music player from background code
// between each audio frame. excessive events will be dropped/ignored. 
// essentially the size of the event ringbuffer.
#define max_events_per_frame (16)

// number of channel handles reserved for SFX specifically
#define max_sfx_handles (256)

// sfx handles count from 0-255 inclusive. MIDI channels count from 256 onwards
// and map directly to channel:note index combinations (+256 offset). This allows
// a channel & note state to be obtained from its handle during replay in order to
// deal with polyphony logic etc.
#define max_handles (max_sfx_handles+(max_midi_notes*max_midi_channels))

//-----------------------------------------------------------------------------

// indicates status of any virtual channel
#define status_active (0) // indicates channel is active with a source or at least emitting silence
#define status_complete (1) // indicates the channel has been stopped by player, but not yet processed for killing
#define status_inactive (-1) // indicates the channel has already been placed on the kill list and 'in limbo' / offlimits

//-----------------------------------------------------------------------------
/*
typedef struct channel_phys_state_s
{
	s32 sl_op_samples;			// sample emits remaining at current frequency
	volatile u32 ul_op_abspos;	// resampled output position (CAUTION: asm mixer updates this, invisible to compiler)

} channel_phys_state_t;
*/

typedef struct virtual_channel_s
{
	// priority list
	struct virtual_channel_s* p_anext;
	struct virtual_channel_s* p_aprev;

	// subframe state - this can change within a frame for any channel
//	channel_phys_state_t phys;

	//
	u8 *penv;
	u8 *psustain;
//	s32 tenvr;
//	s32 tenva;


	// volume control
	u8 cvol;	// channel controller: volume (changed by bmm stream, affects multiple notes)
	s8 vol;		// note volume (changed by bmm stream)
	s8 evol;	// note envelope volume (changed by replay engine)

	// note data
	u8 note;
	
	//	s8 pan;
//	u8 egrain;

	// flags
	s8 status_;
//	s8 loopcount;
	u8 noteon;
	s8 chn;
//	s8 ovol;
	u8 setbuzz;

	s16 vibpos;
	s16 vibmag;

	s16 pbend;
	s16 finenote;

	s16 ymi;
	s16 handle;
	s16 ymch;
	s16 ymch_last;

	// private state

	s8 mplex_count;
	s8 mplex_cycle;
	s16 mplex_map;


	// active list - linked when playing, audible or otherwise (note-on)
	struct virtual_channel_s* p_uoanext;
	struct virtual_channel_s* p_uoaprev;

	// multiplexer/proxy list sentinel
	struct virtual_channel_s* p_mplex_sentinel;

	// yield list - linked when released, but allowed to continue playing (note-off)
//	struct virtual_channel_s* p_yprev;
//	struct virtual_channel_s* p_ynext;

} virtual_channel_t;

//-----------------------------------------------------------------------------

extern "C" void api_channel_cleanup();

extern "C" volatile char g_serverlocks;

extern u8 g_bmm_channel_controller_volume[];
extern s16 g_bmm_channel_controller_pbend[];

extern s16 g_strobe;
extern s16 s_stroberate;

//-----------------------------------------------------------------------------

// open the sound system, initialise hardware state
void AGT_SoundOpen();

// shut down sound system & clean up hardware state
void AGT_SoundClose();

//-----------------------------------------------------------------------------

#endif // sound_h_

