//======================================================================================================================
//	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	YM music interface (BMZ soft MIDI)
//----------------------------------------------------------------------------------------------------------------------
//	******************************************************************************************************
//	******************************************************************************************************
//	todo: THIS IS A GIANT MESS. but most of this is now handled in the 68k module and is due for deletion
//	so just look away for now and some day it will look tidy
//	******************************************************************************************************
//	******************************************************************************************************
//----------------------------------------------------------------------------------------------------------------------

#ifndef music_h_
#define music_h_

//-----------------------------------------------------------------------------

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

//-----------------------------------------------------------------------------

// these do not change
#define max_midi_channels (16)
#define max_midi_notes (128)

//-----------------------------------------------------------------------------
// some globals used by replay engine

extern volatile s16 g_looped;

extern u16 bmm_volume; // current global volume for replay
extern u8* bmm_volumes; // currently configured volume translation table based on global volume
extern u8 bmm_volumetable[]; // all volume translation tables

extern u8* bmm_alloc;
extern u8* bmm_ptr; // points to current music stream in memory - one at a time
extern unsigned short bmmseq_size; // sequence size for music stream - defines end of stream in terms of events

extern s32 ev_target;
extern s32 ev_processed;

extern u8* g_bmmstream_pos; // current music stream position during replay
extern u8* bmmstream_startpos; // music stream restart position (== bmm_ptr)
extern u8* g_bmmstream_restartpos;
extern unsigned short g_bmmstream_eventcount; // music events remaining before next loop
extern unsigned short g_bmmstream_restarteventcount;
extern short g_bmmstream_loop; // nonzero = indicates stream should loop when complete

//-----------------------------------------------------------------------------
// tweaks and overrides for BMZ music available during AGT_MusicFixupCallback()

// updated by set_channel_routing(...)
extern "C" s8 g_bmm_channel_multiplex_map[];


// offset octave (e.g. +12 for octave +1) for each MIDI channel (0-15)
extern "C" s8 g_bmm_channel_octave_map[];

// YM instrument (YM_? to map to each GM instrument (0-128)
extern "C" u8 g_instrument_map[];

// force single GM instrument (0-127) on each MIDI channel (0-15), otherwise use programmed instruments
extern u8 channel_gmi_overrides[];
// select one GM instrument (0-127) on each MIDI channel (0-15) to override volume
extern u8 channel_gmi_volume_overrides[];
// volume to use (0-127) for channel (0-15) volume override
extern u8 channel_gmi_volume_override_values[];

// function ptr type for remapping channels at loadtime (hack alert!)
typedef int (*remap_ft)(s16 ch, u8 *i, u8 *n, u8 *v);

//-----------------------------------------------------------------------------

// load BMU/BMZ music track
void AGT_MusicLoad(const char* _name);

// start music track with or without looping
void AGT_MusicStart(int _looping);

// stop music track, wait for channel flush
void AGT_MusicStop();

// used to push individual note events from game code, for testing sounds
// todo: not yet implemented
void AGT_MusicPushEvent(s16 mc, u8 control, u8 instrument, u8 volume, u8 note);

// opportunity for game code to tweak instruments & channel routing on load
extern "C" void AGT_MusicFixupCallback(const char* const _name, remap_ft &remap_func);

//-----------------------------------------------------------------------------
// route each of the 16 MIDI channels to the YM hardware output channels.
// can specifies multiplexing group (0,1,2) or -1 for priority notes
// note: priority should be used sparingly as it can cancel any of the 
// other 3 channels and is polyphonic - best used for very short, fast 
// decay foreground instruments.

extern "C" void set_channel_routing(const s8 *_routing);

//-----------------------------------------------------------------------------

#pragma GCC diagnostic pop

//-----------------------------------------------------------------------------

#endif // music_h_

//-----------------------------------------------------------------------------
