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

#ifdef ENABLE_AGT_AUDIO

// generate .dat tables for YM volume, frequency and vibrato (requires MiNTlib CRT / MINIMAL_LINK=no)
//#define MAKE_TABLES

static const int pbend_renorm = 12;	//2

// ---------------------------------------------------------------------------------------------------------------------
//	system headers

#if defined(MAKE_TABLES)
#include <mint/sysbind.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <math.h>
#endif

// ---------------------------------------------------------------------------------------------------------------------
//	Our AGT headers

#include "../common_cpp.h"

#include "../ealloc.h"

#include "../system.h"

#include "../fileio.h"

#include "sound.h"
#include "music.h"

//-----------------------------------------------------------------------------

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

//-----------------------------------------------------------------------------

extern "C" void STE_WriteLMC1992(int data);

volatile s16 g_looped = 0;

//-----------------------------------------------------------------------------
// inline implementations for high performance code

extern yminstrument_t g_yminstrument_dict[YMI_NUM];

extern "C" yminstrument_t *g_yminstrument_index[];

void calibrate_envsequences();

//-----------------------------------------------------------------------------

//s16 g_ym_buzztable[128*pbend_steps];

#if defined(MAKE_TABLES)
//
////s16 g_ym_freqtable[128*pbend_steps];
//extern "C" s16 g_ym_freqtable[128*pbend_steps];
//s16 g_ym_vibtable[1<<(6+7)];
//s8 g_combine_ctrlvol_notevel[1<<(max_vol_stateshift+7)];
//s8 g_combine_adsrenv_mixvol[1<<(2+4+7)];
s8 loglintable[128+1];
//extern "C" s8 g_sampletranslation[256];
//
//#else
#endif

extern "C" s16 g_ym_freqtable[128*pbend_steps];
extern "C" s16 g_ym_vibtable[1<<(6+7)];
extern "C" s8 g_combine_ctrlvol_notevel[1<<(max_vol_stateshift+7)];
extern "C" s8 g_combine_adsrenv_mixvol[1<<(2+4+7)];
extern "C" s8 g_sampletranslation[256];

//#endif

//static s16 real_physical_channels = 0;

static s16 event_head = 0;
static volatile s16 event_tail = 0;
s16 g_channel_freepool_count = 0;

static int isrunning = 0;
static int isopen = 0;

static void device_forcestart(void);
static void device_forcestop(void);

static virtual_channel_t event_queue[max_events_per_frame];
virtual_channel_t *g_channel_freepool[max_virtual_channels];
static virtual_channel_t channel_pool[max_virtual_channels];

//extern "C" volatile char g_serverlocks;
volatile char g_flushchannels = 0;

static int device_open(void);
static int device_start(void);
static void device_stop(void);
static void device_close(void);

extern "C" u8 g_instrument_map[];

extern "C" int g_sampleB;
extern "C" int g_sampleE;

extern "C" s8 g_samplePrio;

//u8 bmm_volumetable[16*max_vol_states];
//u8* bmm_volumes = 0;

//u8 g_bmm_channel_controller_volume[16];
//s16 g_bmm_channel_controller_pbend[16];

extern "C" s8 g_bmm_channel_multiplex_map[];

void set_channel_routing(const s8 *_routing)
{
	// careful here: byte buffers might not be word-aligned! avoid optimised memcpy ops.
	for (s16 i = 0; i < 16; i++)
		g_bmm_channel_multiplex_map[i] = _routing[i];
//	qmemcpy(g_bmm_channel_multiplex_map, _routing, 16);
}

static u32 s_timestamp = 0;

#define TESTLEN (5*32)

//-----------------------------------------------------------------------------

char *load_sample(int id, void* own);

extern "C" virtual_channel_t * g_active_channels[];	// sfx dynamic + midi fixed
static s16 s_handle_pool = 0;


void AGT_SoundClose(void)
{
	if (isopen)
	{
		if (isrunning)
		{
			device_stop();
		}

		device_close();
	}
}


void AGT_SoundOpen(void)
{
	// Initialize external data (all sounds) at start, keep static.
	dprintf( "AGT_SoundOpen: begin...\n");

	if (device_open())
	{
		device_start();
	}

	// Finished initialization.
	dprintf("AGT_SoundOpen: done - sound module ready\n");
}


PRFINLINE void load_silence(virtual_channel_t *curr)
{
	curr->note = 0; curr->pbend = 0;
	/*curr->vibpos = 0; */curr->vibmag = 0;
	curr->vol = curr->evol = 0;
	curr->noteon = false;
//	curr->phys.ul_op_abspos = 0;
//	curr->tenvr = 0;
	curr->penv = 0;
	curr->psustain = 0;
//	curr->egrain = env_timebase_shift+4;
}

s32 ev_processed = 0;
s32 ev_target = 0;

u8* bmm_alloc = 0;
u8* bmm_ptr = 0;
u8* bmmstream_startpos = 0;
u8* g_bmmstream_restartpos = 0;
u8* g_bmmstream_pos = 0;
unsigned short bmmseq_size = 0;
unsigned short g_bmmstream_eventcount = 0;
unsigned short g_bmmstream_restarteventcount = 0;
short g_bmmstream_loop = 0;






void assign_channel(virtual_channel_t * __restrict _virt_channel, virtual_channel_t * __restrict _event_data)
{
	s16 handle;
	u16 note;
//	u32 active_length;

	_virt_channel->chn = -1;

	// translate note pitch to step
	note = _event_data->note;

	_virt_channel->note = note;


	_virt_channel->pbend = 0;

//	_virt_channel->phys.ul_op_abspos = 0;


	_virt_channel->penv = 0;
//	_virt_channel->tenvr = 0;

//	_virt_channel->phys.source.length = _event_data->phys.source.length;
//	_virt_channel->phys.source.looplen = _event_data->phys.source.looplen;



//	if (_virt_channel->phys.source.inc != 0)
/*	if (note != 0)
	{
		_virt_channel->phys.sl_op_samples = TESTLEN;
	}
	else
	{
		_virt_channel->phys.sl_op_samples = 0x7fffffff;
	}
*/

//	printf("assign: vol = %08x\n", (int)_event_data->vol);

//	_virt_channel->phys.vlut = (u32)(&volume_luts[(_event_data->pan << 8) | (_event_data->vol << (8+max_pan_stateshift))]);

//	_virt_channel->pan = _event_data->pan;
	_virt_channel->vol = _event_data->vol;
	_virt_channel->evol = 15;
	_virt_channel->cvol = (max_vol_states-1);	// does not change for duration of sound

//	_virt_channel->loopcount = _event_data->loopcount;


//	_virt_channel->sfxid = _event_data->sfxid;
	handle = _event_data->handle; _virt_channel->handle = handle;

	_virt_channel->status_ = status_active;

	// map active channel for tracking
	// WARNING: this is 'server side' event occurring within interrupt context
	// client side needs to lock the event processing to read and use this info...
	g_active_channels[handle] = _virt_channel;

//	printf("assigned:\n");

}



//static virtual_channel_t virtual_channels[max_virtual_channels];

extern "C" virtual_channel_t g_front_prio;
extern "C" virtual_channel_t g_back_prio;
extern "C" virtual_channel_t g_front_uoactive;
virtual_channel_t g_back_uoactive;

//static virtual_channel_t *unordered_active[max_virtual_channels];
//static s16 num_unordered_active = 0;

//static virtual_channel_t front_yield;
//static virtual_channel_t back_yield;

virtual_channel_t g_multiplex_sentinels[3];
virtual_channel_t g_multiplex_proxies[3];

extern "C" virtual_channel_t *g_multiplex_proxy_index[];
extern "C" virtual_channel_t *g_multiplex_sentinel_index[];

extern "C" virtual_channel_t* g_active_physical[];


//A	p[v1,mA[v0,v5,v7]]
//B	p[v1,v2,mB[]]
//C	p[v1,v2,v3,v4,mC[]]

//extern "C" s32 dmaframesize;
extern "C" void audio_mux_frame();

static void device_forcestart(void)
{
	if (isrunning)
	{
	}
}



static void device_forcestop(void)
{
	if (isrunning)
	{
	}
}

extern "C" int YMSYS_Init();

static int device_start(void)
{
	if (isopen && !isrunning)
	{

		dprintf("audiodevice:: start...\n");

		YMSYS_Init();

		isrunning = true;
		device_forcestart();
	}

	return isrunning;
}

static void device_stop(void)
{
	if (isopen && isrunning)
	{
		dprintf("audiodevice:: stop\n");

		device_forcestop();
		isrunning = false;
	}
}

static void init_channel(virtual_channel_t* channel, int _i)
{
	channel->p_aprev = channel->p_anext = 0;
	channel->p_uoaprev = channel->p_uoanext = 0;

	channel->note = 0;
	channel->penv = 0;
	channel->psustain = 0;
	channel->vol = channel->evol = channel->cvol = 0;
	channel->vibpos = (_i << 2) & 255;

	channel->status_ = status_inactive;
	channel->noteon = false;
	channel->chn = -1;
	channel->ymi = 0;
	channel->handle = -1;
	channel->ymch = channel->ymch_last = -1;

	channel->p_mplex_sentinel = 0;
	channel->mplex_count = 0;
	channel->mplex_cycle = 0;
}

extern "C" void fixup_procedures_68k();

static int device_open(void)
{
	if (!isopen)
	{

		dprintf("audiodevice:: open\n");

		fixup_procedures_68k();

		calibrate_envsequences();

		// these should never change
		{
			s16 i;
			for (i = 0; i < 256; ++i)
				g_instrument_map[i] = YMI_Piano;	// missing instrument indicator
		}

		// init channel priority list
		{
			init_channel(&g_front_prio, 0);
			g_front_prio.p_aprev = 0;
			g_front_prio.p_anext = &g_back_prio;

			init_channel(&g_back_prio, 0);
			g_back_prio.p_aprev = &g_front_prio;
			g_back_prio.p_anext = 0;
		}

		// init channel unordered active list
		{
			init_channel(&g_front_uoactive, 0);
			g_front_uoactive.p_uoaprev = 0;
			g_front_uoactive.p_uoanext = &g_back_uoactive;

			init_channel(&g_back_uoactive, 0);
			g_back_uoactive.p_uoaprev = &g_front_uoactive;
			g_back_uoactive.p_uoanext = 0;
		}

		// init multiplex sentinels for proxy channels
		for (s16 mc = 0; mc < 3; mc++)
		{
			virtual_channel_t *mplex_proxy = &g_multiplex_proxies[mc];
			virtual_channel_t *mplex_sent = &g_multiplex_sentinels[mc];

			g_multiplex_proxy_index[mc] = mplex_proxy;
			g_multiplex_sentinel_index[mc] = mplex_sent;

			init_channel(mplex_proxy, mc);
			init_channel(mplex_sent, mc);
			mplex_proxy->status_ = status_active;

			mplex_proxy->p_mplex_sentinel = mplex_sent;
			mplex_sent->p_anext = mplex_sent;
			mplex_sent->p_aprev = mplex_sent;
		}



		// init channel yield list
/*		{
			init_channel_silent(&front_yield);

			front_yield.p_aprev = 0;
			front_yield.p_anext = 0;
			front_yield.p_yprev = 0;
			front_yield.p_ynext = &back_yield;

			init_channel_silent(&back_yield);

			back_yield.p_aprev = 0;
			back_yield.p_anext = 0;
			back_yield.p_yprev = &front_yield;
			back_yield.p_ynext = 0;
		}
*/
		// init free channel pool
		{
			s16 f;
			for (f = 0; f < max_virtual_channels; ++f)
			{
				virtual_channel_t *channel;
				channel = &channel_pool[f];
				g_channel_freepool[f] = channel;
				g_channel_freepool_count += 4;

				init_channel(channel, f);
			}
		}

		// init active channels
		{
			s16 f;
			for (f = 0; f < max_handles; ++f)
			{
				g_active_channels[f] = 0;
			}
		}

		{
			s16 ch;
			for (ch = 0; ch < 16; ++ch)
			{
				// reset channel controler volumes
				g_bmm_channel_controller_volume[ch] = (max_vol_states-1);
				g_bmm_channel_controller_pbend[ch] = 0;
			}
		}

		{
			for (s16 p = 0; p < max_physical_channels; p++)
			{
				g_active_physical[p] = 0;
			}
		}

		dprintf("device:: build notetable...\n");

/*		for (int n = 0; n < 256; ++n)
		{
			float fm =  pow(2.0f, float(n-69) / 12.0f) * 440.0f;	
			float tonePeriod = 125000.0 / fm;
			int fn = (int)tonePeriod;
			notetable[n] = fn;			
		}
*/

#if defined(MAKE_TABLES)
		for (s16 m = 0; m < 32; ++m)
		{
			const float f_magrange = 16;

			float f_mag = (float)m / 32.0;

			for (s16 t = 0; t < 128; ++t)
			{
				float f_t_rad = ((float)t * 2.0 * 3.1415927) / 128.0;
				float f_p = sin(f_t_rad) * f_mag * f_magrange;
				int fn = (int)floor(f_p + 0.5);
				g_ym_vibtable[(m << 7) | t] = (s16)fn;	
			}

			fsave("ymvib.dat", (char*)g_ym_vibtable, sizeof(g_ym_vibtable));
		}
#endif

#if defined(MAKE_TABLES)
		{
			for (s16 n = 0; n < (128*pbend_steps); ++n)
			{
				float fm =  pow(2.0, float(n-(69*pbend_steps)) / (12.0 * pbend_steps)) * 440.0;	
				float tonePeriod = 125000.0 / fm;
				int fn = (int)floor(tonePeriod + 0.5);
				g_ym_freqtable[n] = (s16)fn;			

	//			freq_table[n] = fn;
	//			float sample_period = ((261.6255653006*11025.0)/22050.0)/fn; //25033.0 / fn;
	//			float rsample_period = 1.0 / sample_period;
	////			float sample_period = 25033.0 / fn;
	//			period_table[n] = sample_period;
	//			rperiod_table[n] = rsample_period;
			}
			fsave("ymfrq.dat", (char*)g_ym_freqtable, sizeof(g_ym_freqtable));
		}
#endif

#if defined(MAKE_TABLES)
		{
			for (s16 ll = 0; ll <= 128; ll++)
			{
				float fl = (float)ll / 128.0f;
				//float fv = pow(fl, 0.6f);
//				float fv = log(fl + 1.0f) / 0.6931471805599453;
				float fv = pow(fl, 0.33f);
				if (fv > 1.0f) 
					fv = 1.0f;
				loglintable[ll] = (int)floor(fv * 127.999f);
			}
		}

		{
			for (s16 cv = 0; cv < max_vol_states; cv++)
			{
				for (s16 nv = 0; nv < (1<<7); nv++)
				{
					s16 v = ((nv+1) * (cv+1)) >> ((max_vol_stateshift+7)-7);
					if (v > 128)
						v = 128;

					v = loglintable[v];

					g_combine_ctrlvol_notevel[(cv<<7) | nv] = (s8)v;
				}
			}
			fsave("ymv1.dat", (char*)g_combine_ctrlvol_notevel, sizeof(g_combine_ctrlvol_notevel));
		}

		{
			for (s16 ev = 0; ev < (1<<(2+4)); ev++)
			{
				s16 mag_ev = ev & 0x0f;
				s16 flg_ev = ev & 0x30;

				for (s16 mv = 0; mv < (1<<7); mv++)
				{
//					s16 v = (((mag_ev+1) * (mv+1)) >> ((4+7)-4));

					s16 v = ((mag_ev+1) * (mv+0));

					// log compensation
//					v = (loglintable[v >> ((4+7)-7)] + 3) >> (7 - 4);
/*					
					v = (1<<(4+7)) - v;

					s32 vnl = (v*v) >> (4+7);
					v = vnl; 

					v = (1<<(4+7)) - v;

*/
					// shift into HW range
					v = (v + 63) >> ((4+7)-4);

					// clamp HW volume level
//					v--;
//					if (v < 0) 
//						v = 0;
					if (v > 15) 
						v = 15;

					// recombine flags
					v |= flg_ev;

//					v = 0;

					g_combine_adsrenv_mixvol[(ev<<7) | mv] = (s8)v;
				}
			}
			fsave("ymv2.dat", (char*)g_combine_adsrenv_mixvol, sizeof(g_combine_adsrenv_mixvol));
		}
#endif

#if defined(MAKE_TABLES)
		{
			for (s16 u = 0; u < 256; u++)
			{
				s32 s = u - 128;
				float fl = ((float)s) / 128.0f;
				float fv;

				if (fl >= 0.0f)
				{
					fv = pow(fl, 0.7f);
					if (fv > 0.99999f) 
						fv = 0.99999f;
				}
				else
				{
					fv = -pow(-fl, 0.7f);
					if (fv < 1.0f) 
						fv < 1.0f;
				}

				g_sampletranslation[u] = (s8)(s32)floor(fv * 128.0f);
			}
			fsave("ymst.dat", (char*)g_sampletranslation, sizeof(g_sampletranslation));
		}
#endif

#if (0)
		{
			for (s16 n = 0; n < (128*pbend_steps); ++n)
			{
				static const s16 o_offset = 0;
				static const s16 o_modulo = 10;
				static const s16 o_shift = 0;
				s16 nmod = tmod16((n - (12*o_offset*pbend_steps)), (12*o_modulo*pbend_steps)) + (12*(o_offset+o_shift)*pbend_steps);

				float fm =  pow(2.0, float(nmod-(69*pbend_steps)) / (12.0 * pbend_steps)) * 440.0;	
				float tonePeriod = 125000.0 / fm;
				int fn = (int)floor(tonePeriod + 0.5);
				g_ym_buzztable[n] = (s16)fn;			
			}
	//		fsave("ymfrq.dat", (char*)g_ym_freqtable, sizeof(g_ym_freqtable));
		}
#endif

		// pull in sound samples for percussion etc.
		{
			for (int ymi = 0; ymi < YMI_NUM; ymi++)
			{
				g_yminstrument_index[ymi] = &g_yminstrument_dict[ymi];

				load_sample(ymi, NULL);
			}
		}

//s8 volume_table_1[1<<(max_vol_stateshift+max_vol_stateshift)];
//s8 volume_table_2[1<<(6+7)];

		reg8(FFFF8800) = 7;
		reg8(FFFF8802) = binary(00111000);
		reg8(FFFF8800) = 0xD;
		reg8(FFFF8802) = binary(00001100);		// inv-trsi
//		reg8(FFFF8802) = binary(00001010);		// saw

		reg8(FFFF8800) = 8;
		reg8(FFFF8802) = 16;

		reg8(ffff8921) = 0x81;

		// fiddle with the STE's LMC1992 via microwire - not that it helps much
		// Q: why does the DMA+YM2149-12db mode not seem to work?
		if (0 && bSTE)
		{
			// set master volume
			STE_WriteLMC1992((3<<6) | 30);	// slightly reduce master vol.
			// set left vol
			STE_WriteLMC1992((5<<6) | 20);	// max left panning
			// set right vol
			STE_WriteLMC1992((4<<6) | 20);	// max right panning
			// set treble
			STE_WriteLMC1992((2<<6) | 6);	// treble +db
			// set bass
			STE_WriteLMC1992((1<<6) | 6);	// bass +db
			// set mixer
			STE_WriteLMC1992((0<<6) | 1);	// YM+DMA (nothing else works or us useful!)
		}

		isopen = true;
	}

	return isopen;
}

static void device_close(void)
{
	if (isopen)
	{
		dprintf("audiodevice:: close\n");

		isopen = false;
	}
}

//-----------------------------------------------------------------------------
// insert/remove/prioritize channels on frame event

//typedef struct channel_removal_s
//{
//	virtual_channel_t *prev;
//	virtual_channel_t *curr;
//} channel_removal_t;

extern "C" virtual_channel_t * g_remove_channels[];
extern "C" s16 g_remove_channel_count;

s16 g_bmmstate_duration = 0;

s16 g_bmm_pending_samples = 0;

//


//-----------------------------------------------------------------------------


typedef struct bmmevent_s
{
	s16 mc;
	u8 control;
	u8 instrument;
	u8 volume;
	u8 note;

} bmmevent_t;

static bmmevent_t bmm_event_queue[16];
volatile s16 bmm_event_head = 0;
volatile s16 bmm_event_tail = 0;

void AGT_MusicPushEvent(s16 mc, u8 control, u8 instrument, u8 volume, u8 note)
{
	s16 sema, l_event_head;
	bmmevent_t* newevent;

	sema = (bmm_event_tail - 1) & (16-1);
	l_event_head = bmm_event_head;
	if (l_event_head != sema)
	{
		newevent = &bmm_event_queue[l_event_head];
		newevent->mc = mc;
		newevent->control = control;
		newevent->instrument = instrument;
		newevent->volume = volume;
		newevent->note = note;

		// event is committed here
		bmm_event_head = (l_event_head + 1) & (16-1);
	}
}


extern "C" s8 g_bmm_channel_octave_map[];

extern "C" void assign_bmm_channel_68k(virtual_channel_t *_virt_channel, s16 _bmmchannel, s16 _mpmap, u8 _instrument, u8 _volume, u8 _note/*, s32 _step*/);

agt_outline void assign_bmm_channel(virtual_channel_t *_virt_channel, s16 _bmmchannel, s16 _mpmap, u8 _instrument, u8 _volume, u8 _note/*, s32 _step*/)
{
	//u32 active_length;
	//s32 step;
	s16 note, pbnote;//, pitchcode;
	s16 ymi;

	// midi channel #
	_virt_channel->chn = _bmmchannel;		
	_virt_channel->mplex_map = _mpmap;

	// special handling of channel #10 (percussion)
	//if (_bmmchannel == (10-1))
	//{
	//	// for percussion, ymi is based on translated note
	//	ymi = mperc_last_ - (_note - midi_mperc_start_);

	//	printf("percussion ymi: %d\n", (int)ymi);

	//	// lookup instrument
	//	yminstrument_t *pins = &s_yminstrument_dict[ymi];

	//	// trigger sample
	//	if (pins->p_wav)
	//	{
	//		g_sampleB = (int)(pins->p_wav + 4);
	//		g_sampleE = g_sampleB + *(int*)(pins->p_wav);
	//	}
	//
	//	return;
	//}
	//else
	{
		// for instruments, use the map
		ymi = g_instrument_map[_instrument];
	}

//	if (_bmmchannel == (1-1))
//	if (_bmmchannel == (2-1))
//	if (_bmmchannel == (3-1))
//	if (_bmmchannel == (4-1))
//	if (_bmmchannel == (5-1))
//	if (_bmmchannel == (6-1))
//	if (_bmmchannel == (8-1))
//	if (_bmmchannel == (9-1))
//	if (_bmmchannel == (7-1))
//	if (_bmmchannel == (11-1))
//		ymi = YMI_Bass;
//	else
//		ymi = YMI_Piano;

	_virt_channel->ymi = ymi;

	// determine fixed or variable pitch instrument
	note = (s16)_note;

	//pitchcode = psfxinfo->pitch;
	//if (pitchcode > 0)
	//{
	//	// absolute pitch - fixed frequency e.g. most percussion, some sfx
	//	note = pitchcode;
	//}
	//else if (pitchcode < 0)
	{
		// variable pitch, relative to specified reference note
		//note = (note-note_reference)-pitchcode;

		// if a sample's reference rate puts it beyond the available
		// note range, push it back as a last resort
		if (note <= 0)
			note += 12;
	}
	
	if ((note == 0) && (g_bmm_channel_controller_pbend[_bmmchannel] != 0))
		eprintf("error: audiodevice: nonzero pbend on zero note\n");

	pbnote = ((s16)note << pbend_bits) + g_bmm_channel_controller_pbend[_bmmchannel];

	if (pbnote < 0 || pbnote >= (128*pbend_steps))
		eprintf("error: audiodevice: pbnote out of range(1): pb=%d n=%d pbn=%d\n",
			(int)g_bmm_channel_controller_pbend[_bmmchannel], (int)note, (int)pbnote);

	// lookup instrument
	yminstrument_t *pins = &g_yminstrument_dict[ymi];

	// important: original note is assigned, but adjusted note is used to obtain step
	// this allows polyphony but will consume more channel resources during replay
	_virt_channel->note = note;
	_virt_channel->pbend = g_bmm_channel_controller_pbend[_bmmchannel];


//	_virt_channel->phys.ul_op_abspos = 0;

	// configure

	_virt_channel->penv = pins->p_envseq;

	_virt_channel->psustain = 0;
	_virt_channel->evol = 15;			// envelope volume

	if (_virt_channel->penv)
	{
		if (pins->env_rel >= 0)
			_virt_channel->psustain = &pins->p_envseq[pins->env_rel];
	
		_virt_channel->evol = *(_virt_channel->penv);
	}

	_virt_channel->vol = _volume;		// note volume (velocity)
	_virt_channel->cvol = max_vol_states-1;			// controller volume

	_virt_channel->vibmag = 0;

	//if (_virt_channel->note != 0)
	//{
	//	_virt_channel->phys.sl_op_samples = TESTLEN;
	//}
	//else
	//{
	//	_virt_channel->phys.sl_op_samples= 0x7fffffff;
	//}

//	printf("assign: inc = %08x\n", _virt_channel->phys.source.inc);
/*
	printf("assign: base = %08x\n", _virt_channel->phys.source.base);
	printf("assign: pos = %08x\n", _virt_channel->phys.source.pos);
	printf("assign: inc = %08x\n", _virt_channel->phys.source.inc);
	printf("assign: alen = %08x\n", _virt_channel->phys.source.alen);
	printf("assign: spos = %08x\n", _virt_channel->phys.source.spos);
	printf("assign: slen = %08x\n", _virt_channel->phys.source.slen);
	printf("assign: dlen = %08x\n", _virt_channel->phys.source.dlen);
	printf("assign: samples = %08x\n", _virt_channel->phys.samples);
	printf("assign: loop = %d\n", (int)_virt_channel->loopcount);
*/
//	printf("assign: vol = %08x\n", (int)_event_data->vol);

//	_virt_channel->pan = _bmmchannel & (max_pan_states-1); 

//		_virt_channel->pan = 0;


//	_virt_channel->vol = _volume;

//	printf("bmm volume: %d\n", (int)_volume);

//	_virt_channel->pan = 3;
//	_virt_channel->vol = 3;

//	_virt_channel->phys.vlut = (u32)(&volume_luts[(_virt_channel->pan << 8) | (_virt_channel->vol << (8+max_pan_stateshift))]);


//	_virt_channel->priority = 0x0fff;
//	_virt_channel->priority_decay = 0x7fff;

	//_virt_channel->restart.base = _event_data->restart.base;
	//_virt_channel->restart.pos = _event_data->restart.pos;
	//_virt_channel->restart.inc = _event_data->restart.inc;
	//_virt_channel->restart.alen = _event_data->restart.alen;
	//_virt_channel->restart.spos = _event_data->restart.spos;
	//_virt_channel->restart.slen = _event_data->restart.slen;
	//_virt_channel->restart.dlen = _event_data->restart.dlen;

//	_virt_channel->restart.length = _event_data->restart.length;
//	_virt_channel->restart.looplen = _event_data->restart.looplen;

/*
	printf("assign: rbase = %08x\n", _virt_channel->restart.base);
	printf("assign: rpos = %08x\n", _virt_channel->restart.pos);
	printf("assign: rinc = %08x\n", _virt_channel->restart.inc);
	printf("assign: rlength = %08x\n", _virt_channel->restart.length);
*/
//	_virt_channel->loopcount = _event_data->loopcount;

//	_virt_channel->sfxid = sfxid;

	_virt_channel->status_ = status_active;

//	printf("assigned:\n");

}


extern "C" int bmm_event_feeder_68k();


// forced inline because it has only one callsite, but is relatively
// infrequent, subject to an unlikely() condition
PRFINLINE int bmm_event_feeder(void)
{
	virtual_channel_t *curr, *channel;
	//s16 _dbg_counter_ = 0;

//	*** todo *** predict points during mux where frame events are expected next! in most cases 1 or 2 mux parts should be enough

	s16 looped = 0;
	g_bmmstate_duration = 0;

	// inject midi stream
//	if (g_bmmstream_eventcount)
	while(
		(!looped) &&
		(g_bmmstate_duration == 0)
	)
	{
		//u32* pbmm32;
		u16* pbmm16;
		u8* pbmm8;
		s16 mc;

		pbmm16 = (u16*)g_bmmstream_pos;

		// duration of this state before fetching next state
		g_bmmstate_duration = (*pbmm16++);

//		printf("pending: %d\n", g_bmm_pending_samples);

		pbmm8 = (u8*)pbmm16;

		{
			u8 control, instrument, volume, note;
//			s32 step;

			// check each stream channel for state change
			control = *pbmm8++;

			// todo: combine at convertor side
			mc = (control & 0xf0) >> 4; control &= 0x0f;

			// todo: probably better to remap at convertor side?
			instrument = *pbmm8++;

			// todo: preprocess in convertor?
			volume = *pbmm8++;

			note = *pbmm8++;

//			printf("note vol: %d\n", (int)volume);

			s8 mpmap = g_bmm_channel_multiplex_map[mc];

//			if (g_bmm_channel_multiplex_map[mc] >= 8)
			if (mpmap >= 8)
				goto next;

//			if ((mc != (7-1)) && (mc != (10-1)))
//				goto next;

//			if (mc == 10-1)
//				goto next;
//			if (mc == 15-1)
//				goto next;
//			if (mc == 12-1)
//				goto next;
//			if (mc == 13-1)
//				goto next;

//			if (mc == 1-1)
//				goto next;
//			if (mc == 2-1)
//				goto next;
//			if (mc == 3-1)
//				goto next;
//			if (mc == 4-1)
//				goto next;
//			if (mc == 5-1)
//				goto next;
//			if (mc == 6-1)
//				goto next;

			if (control & 8)
			{
				// controller change
				if (note == 0)
				{
					// channel volume controller
					dprintf("audiodevice: channel volume control: chn[%d] vol[%02x]\n", 
						(int)mc, (int)volume);

//					volume = 127;
					g_bmm_channel_controller_volume[mc] = (volume/*+1*/)>>(7-max_vol_stateshift);
				}
				else
				if (note == 1)
				{
					// channel pitchbend
					// normalize pitchbend data to +/- one full note, maximum ((+/-2.0 * 12)/2)
					g_bmm_channel_controller_pbend[mc] = (((s16)(volume) - 64) * (pbend_renorm>>1));
//					bmm_channel_controller_pbend[mc] = (((s16)(volume) - 64) >> 0);
//					wprintf("audiodevice: pitchbend chn[%d] b[%02x]\n", 
//						(int)mc, (int)bmm_channel_controller_pbend[mc]);
				}
				else
				{
//					eprintf("audiodevice: error - unknown channel controller: chn[%d] ctrl[%d]\n", 
//						(int)mc, (int)note);
				}

				goto next;
			}

			// shift volume into native range
//			volume >>= (8-(max_vol_stateshift+1));
			// translate volume using master volume control
//			volume = bmm_volumes[volume];


			if (mc == (10-1))
			{
				// for percussion, ymi is based on translated note
				int ymi = mperc_last_ - (note - midi_mperc_start_);

//				if ((g_sampleB == 0) || 
//					((note != gmp_closed_high_hat_exc1) && 
//					 (note != gmp_open_high_hat_exc1) &&
//					 (note != gmp_ride_cymbal_1) &&
//					 (note != gmp_ride_cymbal_2) ))

				// lookup instrument
				yminstrument_t *pins = &g_yminstrument_dict[ymi];
				if (!pins->p_wav)
					eprintf("error: missing drum: %d [%s]\n", ymi, pins->p_samplename);

				if (control & 4)
				{
					if (/*(g_sampleE != 0) && */(pins->prio <= g_samplePrio))
					{
						//g_sampleE = 0;
						g_samplePrio = EP_Background;
					}
				}
				if (control & 2)
				{
					if (pins->p_wav)
					{

						if ((g_sampleE == 0) || (pins->prio <= g_samplePrio))
						{

//							printf("drum: %d [%s]\n", ymi, pins->p_samplename);

						// trigger sample
		//					int size = *(int*)(pins->p_wav);
							int size = pins->samplelen;

							g_sampleB = ((int)(pins->p_wav) + 1) & -2;
							g_sampleE = (g_sampleB + size) & -2;
							g_samplePrio = pins->prio;

							//printf("percussion ymi: %d @ %d\n", (int)ymi, size);
						}
					}
				}
				//else
				//if (control & 4)
				//{
				//	if ((g_sampleE != 0) && (pins->prio <= g_samplePrio))
				//	{
				//		g_sampleE = 0;
				//		g_samplePrio = EP_Background;
				//	}
				//}

				
				goto next;
			}

//			goto next;

			// drop channels below volume threshold, to make
			// more opportunities for SFX
			if ((control & 2) && 
				((note == 0) /*||
				(volume < (max_vol_states>>4))*/))
				goto next;

			if (1)
			{
/*				s16 inote = (s16)note;

				if (inote <= 0)
				{
					eprintf("audiodevice: error - note (%d) invalid\n", (int)inote);
					goto next;
				}
				if (inote >= 128)
				{
					eprintf("audiodevice: error - note (%d) out of range\n", (int)inote);
					goto next;
				}

				inote += g_bmm_channel_octave_map[mc];

				if (inote <= 0)
				{
					eprintf("audiodevice: error - octave-adjusted note (%d) out of range\n", (int)inote);
					inote += 12;
				}
				if (inote >= 128)
				{
					eprintf("audiodevice: error - octave-adjusted note (%d) out of range\n", (int)inote);
					inote -= 12;
				}

				note = (u8)inote;
*/
				note += g_bmm_channel_octave_map[mc];
			}

			// todo: can unwrap this as preprocess step. important part is to
			// make sure the period table and inverse period table are almost
			// reciprocal i.e. same degree of rounding error.
			// todo: for the sake of pitch bending etc, probably better to
			// perform the step lookup much later - e.g. just before resampling
			
			// access note period table (.16 normalized)
//			unsigned short *steplut = &step_tab[((unsigned short)note)<<1];
//			u16 smnt = steplut[0];
//			u16 sexp = steplut[1];
//
//			// recover denormalized 16.16 step
//			step = (s32)( ((u32)smnt << 16) >> sexp );

//			step = get_note_step(note);

//	********* notes: 
//	decide - looping to preserve frac part or not? may cause drift as loops accumulate... bad
//	midi stream timing is wrong - note positions seem to be quantized or drift around?
//	try new mixer using resampling cache
//	- resample in blocks equal to DMA page size, so maximum of one fracture per fill
//	- crop block size prediction to min(cacheblock_remainder, nearest_event) but iterate microevents inside subframe block to avoid inserting frame events
//	- some waste at end of last cache block for any source sample
//	- might permit sample filtering??? only if quick enough, not likely
//	- one hash lookup/refilter per physical channel per microevent

//			if (mc != (6-1))
//				goto next;

//				continue;
/*
			switch (mc)
			{
			case (1-1):		// harpsichord 1

//				case (2-1):		// harpsichord chord C4
//				case (3-1):		// harpsichord chord C3
//				case (4-1):		// distguitar chord AD,A#F

			case (5-1):		// sawwave
			case (6-1):		// cello
			case (7-1):		// contrabass

//				case (8-1):		// sound
//				case (9-1):		// sound

			case (10-1):	// percussion
			case (13-1):	// * harpsichord chord
				break;

			case (11-1):	// * distguitar chord AD 
			case (12-1):	// * distguitar chord A#F
//				printf("step: %08x\n", step);
				step = 1<<16;
				break;

			default:
				continue;
			};
*/

/*
			switch (mc)
			{
			//case (1-1):
			//case (2-1):
			//case (3-1):
			//case (4-1):
			//case (5-1):
			//case (6-1):
			//case (7-1):

			case (10-1):	// percussion
				break;

			default:
				continue;
			};
*/


			// de-tune for polyphonic phase
//			step += (((bmmstate_seektime >> 5) + mc) & 0x1f) << 4;


			dprintf("audiodevice: bmm: event ch:%d d:%03x ct:%02x i:%02x v:%02x n:%02x\n", 
				(int)mc,
				(int)g_bmmstate_duration,
				(int)control,
				(int)instrument,
				(int)volume,
				(int)note);

			//if ((control == 3) && (volume > 0))
			//printf("audiodevice: bmm: event ch:%d d:%03x ct:%02x i:%02x v:%02x n:%02x\n", 
			//	(int)mc,
			//	(int)g_bmmstate_duration,
			//	(int)control,
			//	(int)instrument,
			//	(int)volume,
			//	(int)note);

			// change state of active channel
			curr = g_active_channels[max_sfx_handles+(((u16)mc<<7)|note)];

			//if ((control & 6) == 6)
			//	printf("note on/off: c:%d n:%d\n", (int)mc, (int)note);

			if (curr && (curr->status_ == status_active)) // handle mapping should never be -1/pending since bmm stream is fed from within frame context
			{
				// channel tracking indicates this channel & note are busy, although
				// the note may not be held ('noteon') and could just be a previously 
				// released note which hasn't completed its audio cycle

				if (control & 4)
				{
					// note-off

					if (curr->status_ != 0)
						eprintf("error: audiodevice: bmm: faulty note-off h:%04x ch:%d n:%02x status:%d\n", 
							(int)curr->handle, (int)mc, (int)note, (int)curr->status_); 

					if (!curr->noteon)
					{
						wprintf("warning: audiodevice: bmm: multiple note-off h:%04x ch:%d n:%02x\n", 
							(int)curr->handle, (int)mc, (int)note); 
					}
					else
					{

						dprintf("audiodevice: bmm: note-off h:%04x ch:%d n:%02x\n", 
							(int)curr->handle, (int)mc, (int)note); 

						//printf("noteoff: ch:%d n:%02x\n", (int)mc, (int)note);
						curr->noteon = false;

						// priority to minimum (if not multiplexed - priority is shared for multiplexed channels)
						if (mpmap < 0)
						{
							// unlink
							curr->p_aprev->p_anext = curr->p_anext;
							curr->p_anext->p_aprev = curr->p_aprev;

							// link event at end of channel list (deprioritise)
							curr->p_aprev = g_back_prio.p_aprev;
							curr->p_anext = &g_back_prio;
							g_back_prio.p_aprev->p_anext = curr;
							g_back_prio.p_aprev = curr;

							// position in unordered active list remains unchanged
						}

					}
				}
				else if (control & 2)
				{
					// note-on

					if (curr->status_ != 0)
						eprintf("error: audiodevice: bmm: faulty note-on h:%04x ch:%d n:%02x status:%d\n", 
							(int)curr->handle, (int)mc, (int)note, (int)curr->status_); 

					dprintf("audiodevice: bmm: note-on (reassigned) h:%04x ch:%d n:%02x\n", 
						(int)curr->handle, (int)mc, (int)note); 

					curr->noteon = true;

					// note: handle does not change
//					assign_bmm_channel(curr, mc, mpmap, instrument, volume, note/*, step*/);
					assign_bmm_channel_68k(curr, mc, mpmap, instrument, volume, note/*, step*/);

					// priority to maximum (even if multiplexed - priority is shared for multiplexed channels)
					if (mpmap >= 0)
					{
						// multiplexed channel - sort the proxy, not the channel
						virtual_channel_t *proxy = &g_multiplex_proxies[mpmap];
				
						// unlink
						proxy->p_aprev->p_anext = proxy->p_anext;
						proxy->p_anext->p_aprev = proxy->p_aprev;

						// link event at start of channel list (reprioritise)
						proxy->p_anext = g_front_prio.p_anext;
						proxy->p_aprev = &g_front_prio;
						g_front_prio.p_anext->p_aprev = proxy;
						g_front_prio.p_anext = proxy;
					}
					else
					{
						// unlink
						curr->p_aprev->p_anext = curr->p_anext;
						curr->p_anext->p_aprev = curr->p_aprev;

						// link event at start of channel list (reprioritise)
						curr->p_anext = g_front_prio.p_anext;
						curr->p_aprev = &g_front_prio;
						g_front_prio.p_anext->p_aprev = curr;
						g_front_prio.p_anext = curr;

						// status in unordered active list remains unchanged
					}
				}
				else if (control == 1)
				{
					// velocity

					if (curr->status_ != 0)
						eprintf("error: audiodevice: bmm: faulty vol h:%04x ch:%d n:%02x status:%d\n", 
							(int)curr->handle, (int)mc, (int)note, (int)curr->status_); 

					// adjust channel state (volume, note)
					curr->vol = volume;
//					curr->note = note; // pitchbend approximated by this hack
				}
				else
				{
					// ignore - do nothing
				}
			}
			else //!curr
			{
				// channel/note does not exist yet

				if (curr && (curr->status_ == 0))
					eprintf("error: audiodevice: bmm: faulty status h:%04x ch:%d n:%02x status:%d\n", 
						(int)curr->handle, (int)mc, (int)note, (int)curr->status_); 

				if (control & 4)
				{
					// note-off

					//if (!curr)
					//	printf("audiodevice: error - tried to stop vchannel with dead handle ch:%d n:%02x\n", (int)mc, (int)note);
					//else
						dprintf("audiodevice: error - tried to note-off dead channel ch:%d n:%02x\n", (int)mc, (int)note);
				}

				if (control & 2)
				{
					// note-on

					// start channel

//					printf("noteon: ch:%d n:%02x\n", (int)mc, (int)note);

					// steal 'released' channel if possible 
					// (reduces load, increases probability of hearing a sound)
/*
					if (back_yield.p_yprev->p_yprev)
					{
//						printf(" unlink!\n");

						s16 handle;

						channel = back_yield.p_yprev;
						channel->noteon = true;


						// unlink
						channel->p_yprev->p_ynext = channel->p_ynext;
						channel->p_ynext->p_yprev = channel->p_yprev;
						channel->p_yprev = channel->p_ynext = 0;

						// unmap existing handle
						if (channel->handle >= 0)
						{
							active_channels[channel->handle] = 0;
							printf("audiodevice: bmm: stealing h:%04x\n", (int)channel->handle);
						}

						assign_bmm_channel(channel, mc, instrument, volume, note, step);

						// map new handle
						handle = max_sfx_handles + (((u16)mc << 7) | note); 
						dprintf("audiodevice: bmm: note-on (steal) h:%04x<-h:%04x ch:%d n:%02x\n", 
							(int)channel->handle, (int)handle, (int)mc, (int)note); 
						channel->handle = handle;
						active_channels[handle] = channel;
					}
					else
*/

#if (0)

					if (curr && !(curr->noteon)/*(curr->status_ == status_complete)*/)
					{
						// reactivate completed channel

						s16 handle = curr->handle;

						curr->noteon = true;

						// unmap existing handle
//						if (curr->handle >= 0)
//						{
//							active_channels[channel->handle] = 0;
							printf("audiodevice: bmm: warning - stealing h:%04x\n", (int)handle);
//						}

						assign_bmm_channel(curr, mc, mpmap, instrument, volume, note/*, step*/);


						// priority to maximum (even if multiplexed - priority is shared for multiplexed channels)
						if (mpmap >= 0)
						{
							// multiplexed channel - sort the proxy, not the channel
							virtual_channel_t *proxy = &g_multiplex_proxies[mpmap];
					
							// unlink
							proxy->p_aprev->p_anext = proxy->p_anext;
							proxy->p_anext->p_aprev = proxy->p_aprev;

							// link event at start of channel list (reprioritise)
							proxy->p_anext = g_front_prio.p_anext;
							proxy->p_aprev = &g_front_prio;
							g_front_prio.p_anext->p_aprev = proxy;
							g_front_prio.p_anext = proxy;
						}
						else
						{
							// unlink
							curr->p_aprev->p_anext = curr->p_anext;
							curr->p_anext->p_aprev = curr->p_aprev;

							// link event at start of channel list (reprioritise)
							curr->p_anext = g_front_prio.p_anext;
							curr->p_aprev = &g_front_prio;
							g_front_prio.p_anext->p_aprev = curr;
							g_front_prio.p_anext = curr;

							// status in unordered active list remains unchanged
						}


						// map new handle
						//handle = max_sfx_handles + (((u16)mc << 7) | note); 
						dprintf("audiodevice: bmm: note-on (steal) h:%04x<-h:%04x ch:%d n:%02x\n", 
							(int)channel->handle, (int)handle, (int)mc, (int)note); 
						//channel->handle = handle;
						//active_channels[handle] = channel;
					}
					else // (!curr) || (curr && (curr->status_ == status_inactive))

#endif

					{
						// must create new channel

						// limited number of virtual channels available
						if (g_channel_freepool_count > 0)
						{
							s16 handle;

							// allocate channel
							channel = g_channel_freepool[--g_channel_freepool_count];
							channel->noteon = true;


							// set channel from event data
//							assign_bmm_channel(channel, mc, mpmap, instrument, volume, note/*, step*/);
							assign_bmm_channel_68k(channel, mc, mpmap, instrument, volume, note/*, step*/);

							// assign handle to channel
							handle = max_sfx_handles + (((u16)mc << 7) | note); 
							channel->handle = handle;

							dprintf("audiodevice: bmm: note-on (alloc) h:%04x ch:%d n:%02x\n", 
								(int)channel->handle, (int)mc, (int)note); 

							g_active_channels[handle] = channel;

	//						printf("activate c:%d n:%d\n", (int)mc, (int)note);

							// todo: find first non-sfx channel to insert after


							// link event into channel priority list or relevant multiplexing chain
							{
								s16 mplex = mpmap;
								if (mplex >= 0)
								{
									// multiplexed channel - add to assigned multiplexing chain
									virtual_channel_t *pproxy = &g_multiplex_proxies[mplex];
									virtual_channel_t *psent = pproxy->p_mplex_sentinel;

									// add proxy to priority list
									if (psent->mplex_count++ == 0)
									{
										virtual_channel_t *phead = &g_front_prio;
										virtual_channel_t *pheadnext = phead->p_anext;
										pproxy->p_aprev = phead;
										pproxy->p_anext = pheadnext;
										pheadnext->p_aprev = pproxy;
										phead->p_anext = pproxy;
									}
//									psent->mplex_count++;

									// add channel to multiplexing chain
									virtual_channel_t *psentnext = psent->p_anext;
									channel->p_aprev = psent;
									channel->p_anext = psentnext;
									psentnext->p_aprev = channel;
									psent->p_anext = channel;
								}
								else
								{
									// normal channel - add to priority list
									virtual_channel_t *phead = &g_front_prio;
									virtual_channel_t *pheadnext = phead->p_anext;
									channel->p_aprev = phead;
									channel->p_anext = pheadnext;
									pheadnext->p_aprev = channel;
									phead->p_anext = channel;
								}

							}

							//// link event into channel priority list
							//channel->p_anext = front_prio.p_anext;
							//channel->p_aprev = &front_prio;
							//front_prio.p_anext->p_aprev = channel;
							//front_prio.p_anext = channel;

							// link event into unordered active list
							virtual_channel_t *phead = &g_front_uoactive;
							virtual_channel_t *pheadnext = phead->p_uoanext;
							channel->p_uoaprev = phead;
							channel->p_uoanext = pheadnext;
							pheadnext->p_uoaprev = channel;
							phead->p_uoanext = channel;
						}
						else
						{
							eprintf("error: audiodevice: bmm: failed to acquire vchannel\n");
						}

					}
				}
			}
		}

next:
		g_bmmstream_pos = (u8*)pbmm8;

		// reset position when all events accounted for
		if (--g_bmmstream_eventcount == 0)
		{

			//{
			//	virtual_channel_t *pcurr, *pnext;

			//	// update all virtual channels as if they are all playing
			//	// note: one of these may yield an event, predicted by logic higher up

			//	pcurr = front_prio.p_anext;
			//	pnext = pcurr->p_anext;
			//	while (pcurr && pnext)
			//	{
			//		// only update channels which are active - other channels will not be
			//		// output to mixer and don't need stepped

			//		if (pcurr->noteon || pcurr->status_ == status_active)
			//		printf("audiodevice: bmm: status h:%d ns:%d s:%d\n", 
			//			(int)pcurr->handle, (int)pcurr->noteon, (int)pcurr->status_); 

			//		// next vchannel
			//		pcurr = pnext;
			//		pnext = pcurr->p_anext;
			//	}
			//}

			if (g_bmmstream_loop)
			{
				g_bmmstream_eventcount = g_bmmstream_restarteventcount;//bmmseq_size;
				g_bmmstream_pos = g_bmmstream_restartpos;
			}

//			printf("bmm: loop!\n");
			g_looped = looped = 1;
		}
	} // !g_bmmstream_eventcount

	g_bmm_pending_samples = ((u16)g_bmmstate_duration * (u16)samples_per_frame) / (u16)ms_per_frame;
//	printf("audiodevice: bmm_event_feeder: pending=%d, msdur=%d\n", 
//		(int)g_bmm_pending_samples, (int)g_bmmstate_duration);

	return g_bmmstate_duration;
}

// forced inline, because it's only called once per audio frame
PRFINLINE int sfx_event_feeder(void)
{
	virtual_channel_t *newevent, *channel;
	s16 _dbg_counter_ = 0;
	int change = 0;

	// safely deal with insertions from API
	// interrupt-safe queue coupled via atomic [event_head] index
	{
		s16 l_event_tail = event_tail;
		while (l_event_tail != event_head)
		{
			newevent = &event_queue[l_event_tail];

			// limited number of virtual channels available
			if (g_channel_freepool_count > 0)
			{
				// allocate channel
				channel = g_channel_freepool[--g_channel_freepool_count];

				// set channel from event data
				assign_channel(channel, newevent);

				// link event into channel priority list (not multiplexed)
				channel->p_aprev = &g_front_prio;
				channel->p_anext = g_front_prio.p_anext;
				g_front_prio.p_anext->p_aprev = channel;
				g_front_prio.p_anext = channel;

				// link event into channel unordered active list
				channel->p_uoaprev = &g_front_uoactive;
				channel->p_uoanext = g_front_uoactive.p_uoanext;
				g_front_uoactive.p_uoanext->p_uoaprev = channel;
				g_front_uoactive.p_uoanext = channel;

				change = 1;
			}

			l_event_tail = (l_event_tail + 1) & (max_events_per_frame-1);

			if (_dbg_counter_++ > 100)
			{ eprintf("error: audiodevice: sfx: frame_event insert infinite loop\n"); _dbg_counter_ = 0; }
		}
		// release queue here
		event_tail = l_event_tail;
	}

	return change;
}

s16 g_strobe = 0;
s16 s_stroberate = 3;

//virtual_channel_t *g_ymselected[max_ym_channels];
extern "C" virtual_channel_t *g_ymselected[];

PRFINLINE void output()
{
	if (1)
	{
		virtual_channel_t *curr;

		// todo: move all of this into 68k bit
		//if (g_sampleE > g_sampleB)
		//{
		//	// stop playback
		//	reg8(ffff8901) = 0x00;
		//	reg8(ffff8921) = 0x81;

		//	reg8(ffff8903) = (g_sampleB >> 16) & 0xFF;
		//	reg8(ffff8905) = (g_sampleB >> 8) & 0xFF;
		//	reg8(ffff8907) =  g_sampleB & 0xFF;

		//	reg8(ffff890f) = (g_sampleE >> 16) & 0xFF;
		//	reg8(ffff8911) = (g_sampleE >> 8) & 0xFF;
		//	reg8(ffff8913) =  g_sampleE & 0xFF;

		//	// start playback
		//	reg8(ffff8901) = 0x01;

		//	g_sampleB = 0;
		//	g_sampleE = 0;
		//}



		if (0)
		{

		// output data
		if (0)
		{
			curr = g_ymselected[0];
			if (curr)
			{
				s32 fn = g_ym_freqtable[((s16)(curr->note) << pbend_bits) + curr->pbend];
				//u8 vel = curr->evol;

				s32 en = fn >> 3;

//				en &= 0xFFF5;

		//		fn += 1;

				//reg32(FFFF8800) = (0 << 24) | ((fn & 0xFF) << 8);
				//reg32(FFFF8800) = (1 << 24) | ((fn >> 8) << 8);
				//reg32(FFFF8800) = (8 << 24) | (vel << 8);

				//reg8(FFFF8800) = 0;
				//reg8(FFFF8802) = 0;
				//reg8(FFFF8800) = 1;
				//reg8(FFFF8802) = 0;

				reg8(FFFF8800) = 0;
				reg8(FFFF8802) = fn & 0xFF;
				reg8(FFFF8800) = 1;
				reg8(FFFF8802) = fn >> 8;
//				reg8(FFFF8800) = 8;
//				reg8(FFFF8802) = 16 | vel;//binary(00001111);

				reg8(FFFF8800) = 0xB;
				reg8(FFFF8802) = en & 0xFF;
				reg8(FFFF8800) = 0xC;
				reg8(FFFF8802) = en >> 8;
			}
		}

		if (1)
		{
			curr = g_ymselected[1];
			if (curr)
			{
				s32 fn = g_ym_freqtable[((s16)(curr->note-12) << pbend_bits) + curr->pbend];
				u8 vel = curr->evol;

				reg8(FFFF8800) = 2;
				reg8(FFFF8802) = fn & 0xFF;
				reg8(FFFF8800) = 3;
				reg8(FFFF8802) = fn >> 8;
				reg8(FFFF8800) = 9;
				reg8(FFFF8802) = vel;//binary(00001111);
			}
		}

		if (1)
		{
			curr = g_ymselected[2];
			if (curr)
			{
				s32 fn = g_ym_freqtable[((s16)(curr->note-12) << pbend_bits) + curr->pbend];
				u8 vel = curr->evol;

				fn -= 1;
				reg8(FFFF8800) = 4;
				reg8(FFFF8802) = fn & 0xFF;
				reg8(FFFF8800) = 5;
				reg8(FFFF8802) = fn >> 8;
				reg8(FFFF8800) = 10;
				reg8(FFFF8802) = vel;//binary(00001111);
			}
		}

		}
#if (0)
		// rotate multiplexing sources
		if (g_strobe-- < 0)
		{
			g_strobe = s_stroberate;

			for (s16 mpch = 0; mpch < max_ym_channels; mpch++)
			{
				virtual_channel_t *psent = &g_multiplex_sentinels[mpch];
				if (psent->mplex_count > 1)
				{
					// move first in chain to back
					curr = psent->p_anext;
/*
					// unlink
					virtual_channel_t *pprev = curr->p_aprev;
					virtual_channel_t *pnext = curr->p_anext;
					pprev->p_anext = pnext;
					pnext->p_aprev = pprev;

					// relink at back
					virtual_channel_t *psentprev = psent->p_aprev;
					curr->p_anext = psent;
					curr->p_aprev = psentprev;
					psent->p_aprev = curr;
					psentprev->p_anext = curr;
*/
//					s8 thresh = (curr->ovol&0xf) - 3;
					bool n = curr->noteon;
//					s8 chn = curr->chn;

retry:
					{
						// unlink
						virtual_channel_t *pprev = curr->p_aprev;
						virtual_channel_t *pnext = curr->p_anext;
						pprev->p_anext = pnext;
						pnext->p_aprev = pprev;

						// relink at back
						virtual_channel_t *psentprev = psent->p_aprev;
						curr->p_anext = psent;
						curr->p_aprev = psentprev;
						psent->p_aprev = curr;
						psentprev->p_anext = curr;
					}

					curr = psent->p_anext;
//					if (curr->status_ != status_active)
//						while (1) { }

//					if ((curr->ovol&0xf) < thresh)
					if (n && !curr->noteon)
						goto retry;


				}
			}
		}
#endif
	}
}


//-----------------------------------------------------------------------------
// map selected channels to 'sticky' YM hardware channels
PRFINLINE void map_channels()
{
	if (1)
	{
		virtual_channel_t *curr;
		virtual_channel_t *prioritystack[max_ym_channels+1];
		virtual_channel_t **stacked = prioritystack; 

		s16 pc;

		// initialize all YM channels as unmapped / silent

		g_ymselected[0] = 0;
		g_ymselected[1] = 0;
		g_ymselected[2] = 0;

		// stack terminator
		*stacked++ = (virtual_channel_t *)(0);

		// make selection for every YM channel
		for (pc = 0; pc < max_ym_channels; ++pc)
		{
			curr = g_active_physical[pc];

			// not all channels necessarily filled
			if (likely(curr))
			{
				// indirect to first multiplexed channel if possible
				virtual_channel_t *psent = curr->p_mplex_sentinel;
				if (psent)
				{
					// multiplexed - and tied to a specific physical channel
					// (should always be valid, if in priority list!)
					curr = psent->p_anext;
				//	// physical channel to be used
				//	s16 mplex = g_bmm_channel_multiplex_map[curr->chn];
				//	g_ymselected[mplex] = curr;
				//}
				//else 
				//{
				//	*stacked++ = curr;
				}

				s16 sticky = curr->ymch_last;
				if (sticky >= 0)
				{
					curr->ymch = sticky;
					g_ymselected[sticky] = curr;
				}
				else
				{
					*stacked++ = curr;
				}
			}
		}

		// fill remaining free channels with prioritized sources
		{
			virtual_channel_t **pfill = g_ymselected; 
			s16 c = 0;
			while (0 != (curr = *(--stacked)))
			{
				// locate first free channel
				while (0 != *pfill) { pfill++; c++; }
				// claim it
				*pfill++ = curr; 
				curr->ymch = c++;
				// ...repeat until stack empty
			}
		}
	}
}


// processed every frame, for envelope & fx controllers

PRFINLINE void frame_event(void)
{
	virtual_channel_t *curr, *next, *newevent, *channel;
	s16 _dbg_counter_ = 0;

//	s16 num_physical_channels = 0;
	s32 min_samples = 0x7fffffff;




	// caution: removal list must be processed on same frame as updates for removal
	// otherwise priority management may occur on next visit and mess up recorded prev/curr
	// so we just make the whole update stage a critical section to save flagging
	// channels to dodge priority management
	//if (!g_serverlocks)
	{


	//s16 _dbg_counter_ = 0;





#if (0)

	curr = g_front_uoactive.p_uoanext;
	next = curr->p_uoanext;
	while (next)
	{
		if (curr->status_ == status_active)
		{
			// advance envelope
			if (curr->penv)
			{
				u8 *penv = curr->penv;
				if (penv == curr->psustain)
				{
					// [S]ustain (no change)

//					s16 mplex = g_bmm_channel_multiplex_map[curr->chn];
					s16 mplex = curr->mplex_map;

					// hack! slide vibrato magnitude upwards as note is held
					// todo: should come from instrument definition / envelope data (if not a distinct MIDI controller)
					//if (mplex < 0)
						if (curr->vibmag < ((1<<(7+1))*24))
							curr->vibmag += (1<<(7+1));
				}
				else
				{
					u8 evol = *(penv++);
					// AD/R
					curr->evol = evol;

					if (evol > 0)
					{
						curr->penv = penv;

						if (curr->vibmag > ((1<<(7+1))*0))
							curr->vibmag -= (1<<(7+1));
					}
					else
					{
						// envelope completed: terminate note
						curr->noteon = false;
						curr->status_ = status_complete;
						curr->penv = 0;
					}
				}
			}

			s16 v  = (s16)curr->vol;
			s16 cv = (s16)curr->cvol;

			// combine note volume with channel controller volume
			s16 mixvol = g_combine_ctrlvol_notevel[(cv << 7) | v];

			// combine envelope volume with channel volume
			curr->ovol = g_combine_adsrenv_mixvol[((s16)curr->evol << 7) | mixvol];

		}

		// detach from last YM HW channel
		curr->ymch_last = curr->ymch;
		curr->ymch = -1;

		// next vchannel
		curr = next;
		next = curr->p_uoanext;
	}
#endif







	//-----------------------------------------------------------------------------
	// process completed channels, placing them on recycle chain

	if (0)
	{
	curr = g_front_uoactive.p_uoanext;
	next = curr->p_uoanext;
	while (next)
	{
		if (unlikely(curr->status_ == status_complete))
		{
			//if (!g_serverlocks)
			{
			dprintf("audiodevice: frame: completed h:%04x\n", (int)curr->handle);

			if (g_active_channels[curr->handle] == 0)
			{
				eprintf("error: audiodevice: channel handle h:%04x already retired\n", (int)curr->handle);

//				{
//				int x;
//				for (x = 0; x < g_remove_channel_count; ++x)
//					if (remove_channels[x] == curr)
//						eprintf("audiodevice: error - channel already listed for removal\n");
//				}
			}

			if (curr->noteon)
				eprintf("error: audiodevice: channel retired with note-on status\n");

/*			s16 h = curr->handle;
			if (h < 0)
				printf("audiodevice: frame: error - tried to retire vchannel with bad h:%04x\n", (int)h);
			else
			{
				// unmap channel from handle which may be used by client side
				if (active_channels[h] == 0)
					printf("audiodevice: frame: error - tried to retire vchannel with unmapped h:%04x\n", (int)h);

				dprintf("audiodevice: frame: unlinking h:%04x\n", (int)h);
				active_channels[h] = 0;
			}
*/
			curr->status_ = status_inactive;
//			curr->noteon = false;
//			curr->ymch = -1;

			g_remove_channels[g_remove_channel_count++] = curr;


		//	s16 h = curr->handle;
		//	if (h < 0)
		//		printf("audiodevice: frame: error - tried to retire vchannel with bad h:%04x\n", (int)h);

		//	if (h >= 0)
		//	{
		//		// unmap channel from handle which may be used by client side
		//		if (active_channels[h] == 0)
		//			printf("audiodevice: frame: error - tried to retire vchannel with unmapped h:%04x\n", (int)h);

		////			if (curr->handle >= max_sfx_handles)
		////				printf("auto retiring c:%d n:%d\n", (((int)curr->handle-max_sfx_handles)>>7), (((int)curr->handle-max_sfx_handles)&127));

		//		dprintf("audiodevice: frame: unlinking h:%04x\n", (int)h);
		//		active_channels[h] = 0;
		//		//active_sfxid[curr->sfxid]--;
		//	}


//			curr->p_anext->p_aprev = curr->p_aprev;
//			curr->p_aprev->p_anext = curr->p_anext;

			if (g_remove_channel_count > max_toremove_channels)
				eprintf("error: audiodevice: channel could not be queued for removal\n");

			} // (!g_serverlocks)
		}
		else
		{
		}

//		// next in chain
//		curr = next;

		// next vchannel
		curr = next;
		next = curr->p_uoanext;

	}
	}

	//-----------------------------------------------------------------------------
	// select highest priority channels for mapping to YM hardware

	if (0)
	{
		virtual_channel_t *pcurr, *pnext;
		s16 num_physical_channels = 0;

		pcurr = g_front_prio.p_anext;
		pnext = pcurr->p_anext;
		while (pnext)
		{
			if (likely(pcurr->status_ == status_active))
			{
				// list priority order determines physical mapping
				if (num_physical_channels < max_physical_channels)
					g_active_physical[num_physical_channels++] = pcurr;
			}

			// next vchannel
			pcurr = pnext;
			pnext = pcurr->p_anext;
		}

		// if fewer virtual channels than physically mapped, fill the remainder with a dummy source

		while (unlikely(num_physical_channels < min_physical_channels))
			g_active_physical[num_physical_channels++] = 0;

	}

	} // g_serverlocks
}

//-----------------------------------------------------------------------------
// remove completed channels - only needed once per frame

PRFINLINE void frame_cleanup()
{
	// deal with removals
	s16 _dbg_counter_ = 0;

	if (!g_serverlocks)
	{
		// can't process removals if client has locked state
		s16 toremove = g_remove_channel_count;
		while (toremove > 0)
		{
			s16 h;
			virtual_channel_t* curr;

			curr = g_remove_channels[--toremove];

//			curr->status_ = status;

			h = curr->handle;
			if (h >= 0)
			{
				// unmap channel from handle which may be used by client side
				if (unlikely(g_active_channels[h] != curr))
				{
					wprintf("warning: audiodevice: frame: retiring channel with orphaned h:%04x c:%08x\n", (int)h, (int)g_active_channels[h]);
				}
				else
				{
					dprintf("audiodevice: frame: unlinking h:%04x\n", (int)h);
					g_active_channels[h] = 0;
				}
			}

			// restore to freepool
			g_channel_freepool[g_channel_freepool_count++] = curr;

			// if we're deleting a multiplexed channel, maintain the proxy
			s16 mplex = curr->mplex_map;//g_bmm_channel_multiplex_map[curr->chn];
			if (mplex >= 0)
			{
				// unlink channel from proxy multiplexing chain
//				curr->p_anext->p_aprev = curr->p_aprev;
//				curr->p_aprev->p_anext = curr->p_anext;

				virtual_channel_t *proxy = &g_multiplex_proxies[mplex];

				// if proxy chain is depleted, remove proxy from priority list
				proxy->p_mplex_sentinel->mplex_count--;
				if (proxy->p_mplex_sentinel->mplex_count == 0)
				{
					// unlink proxy from priority list
					proxy->p_anext->p_aprev = proxy->p_aprev;
					proxy->p_aprev->p_anext = proxy->p_anext;
				}
			}
//			else
//			{
//				// unlink channel from priority list
//				curr->p_anext->p_aprev = curr->p_aprev;
//				curr->p_aprev->p_anext = curr->p_anext;
//			}

			// unlink channel from priority or mplex list
			curr->p_anext->p_aprev = curr->p_aprev;
			curr->p_aprev->p_anext = curr->p_anext;

			// unlink channel from unordered active list
			curr->p_uoanext->p_uoaprev = curr->p_uoaprev;
			curr->p_uoaprev->p_uoanext = curr->p_uoanext;

/*			if (curr->p_ynext)
			{
				// unlink from yield list
				curr->p_ynext->p_yprev = curr->p_yprev;
				curr->p_yprev->p_ynext = curr->p_ynext;
				curr->p_ynext = curr->p_yprev = 0;
			}
*/
			if (_dbg_counter_++ > 100)
			{ eprintf("error: audiodevice: frame_event removal infinite loop\n"); _dbg_counter_ = 0; }
		}

		g_remove_channel_count = 0;

	} // (!g_serverlocks)
}

extern "C" void api_channel_cleanup()
{
	virtual_channel_t *curr, *next;

	g_serverlocks++;

	//eprintf("*** LOOP LOOP LOOP ***\n");
	dprintf("num vch free: %d\n", g_channel_freepool_count);

	curr = g_front_uoactive.p_uoanext;
	while (curr && curr->p_uoanext)
	{
		// prefetch next, ahead of sort
		next = curr->p_uoanext;

		//printf("vch_penv: %x\n", curr->penv);
		//printf("vch_psustain: %x\n", curr->psustain);
		//printf("vch_cvol: %x\n", curr->cvol);
		//printf("vch_vol: %x\n", curr->vol);
		//printf("vch_evol: %x\n", curr->evol);
		//printf("vch_note: %x\n", curr->note);
		//printf("vch_status: %x\n", curr->status_);
		//printf("vch_noteon: %x\n", curr->noteon);
		//printf("vch_chn: %x\n", curr->chn);
		//printf("vch_setbuzz: %x\n", curr->setbuzz);
		//printf("vch_vibpos: %x\n", curr->vibpos);
		//printf("vch_vibmag: %x\n", curr->vibmag);
		//printf("vch_pbend: %x\n", curr->pbend);
		//printf("vch_finenote: %x\n", curr->finenote);
		//printf("vch_ymi: %x\n", curr->ymi);
		//printf("vch_handle: %x\n", curr->handle);
		//printf("vch_ymch: %x\n", curr->ymch);
		//printf("vch_ymch_last: %x\n", curr->ymch_last);
		//printf("vch_mplex_count: %x\n", curr->mplex_count);
		//printf("vch_mplex_cycle: %x\n", curr->mplex_cycle);
		//printf("vch_mplex_map: %x\n", curr->mplex_map);
		//printf("vch_mplex_sentinel: %x\n", curr->p_mplex_sentinel);

		// retire all music channels
		if (
			(curr->status_ == status_active) /*&& 
			(curr->handle >= max_sfx_handles)*/ &&
			// do not remove multiplexer proxies directly,
			// this will occur when mplex_count is decremented
			(curr->mplex_count == 0) 
		)
		{
//			load_silence(curr);

			if (curr->noteon)
			{
				curr->noteon = false;	// should never loop with any notes on, if BMZ is valid
				eprintf("error: audiodevice: track completed with note on : chn[%d]\n", curr->chn);
			}

//			curr->loopcount = 0;
			//curr->phys.sl_op_samples = 0;
//			curr->status_ = status_complete;

			s16 c = g_remove_channel_count>>2;
			g_remove_channels[c] = curr;
			g_remove_channel_count += 4;
			curr->status_ = status_inactive;
		}

		curr = next;
	}

	g_serverlocks--;
}

//-----------------------------------------------------------------------------
// update/advance sample sources for all channels, active and virtual

/*
PRFINLINE void subframe_step(s32 _stepsize)
{
	virtual_channel_t *pcurr, *pnext;

	// update all virtual channels as if they are all playing
	// note: one of these may yield an event, predicted by logic higher up

	pcurr = g_front_uoactive.p_uoanext;
	pnext = pcurr->p_uoanext;
	while (pcurr && pnext)
	{
		// probably has no effect - but since we do update pcurr within this scope,
		// we can at least hint that object behind curr is isolated and not aliased
		// via pnext, etc.
		virtual_channel_t * __restrict curr = pcurr;

		// only update channels which are active - other channels will not be
		// output to mixer and don't need stepped
		if (curr->status_ == status_active)
		{
			// consume output samples to current position, used to predict upcoming events
			curr->phys.sl_op_samples -= _stepsize;
			curr->phys.ul_op_abspos  += _stepsize;
		}

		// next vchannel
		pcurr = pnext;
		pnext = pcurr->p_uoanext;
	}
}
*/

PRFINLINE void subframe_event()
{
	virtual_channel_t *pcurr, *pnext;
	s16 _dbg_counter_ = 0;

	// update all virtual channels as if they are all playing
	// note: one of these may yield an event, if it represents
	// the nearest event of all channels and is scheduled within
	// the current audio frame

	pcurr = g_front_uoactive.p_uoanext;
	pnext = pcurr->p_uoanext;
	while (likely(pcurr && pnext))
	{
		// probably has no effect - but since we do update pcurr within this scope,
		// we can at least hint that object behind curr is isolated and not aliased
		// via pnext, etc.
		virtual_channel_t * __restrict curr = pcurr;

		if (likely(curr->status_ == status_active))
		{
			s32 active_length;


			s16 ch;


			// determine if this is a BMM channel or SFX. BMM channels are affected by controllers
//			ch = curr->handle - max_sfx_handles;
			ch = curr->chn;
			if (ch >= 0)
			{
				s16 pbend, pbnote;
				u8 cvol;
				// this is a BMM channel
//				ch >>= 7;

				pbend = g_bmm_channel_controller_pbend[ch];
				if (curr->pbend != pbend)
				{
					s32 step, old_step, old_step2;

					if (curr->note != 0)
					{

						if ((curr->note == 0) && (pbend != 0))
							eprintf("error: audiodevice: nonzero pbend on zero note\n");

						pbnote = (curr->note << pbend_bits) + pbend;

						if (pbnote < 0 || pbnote >= 128*pbend_steps)
							eprintf("error: audiodevice: pbnote out of range(3): pb=%d n=%d pbn=%d\n",
							(int)pbend, (int)curr->note, (int)pbnote);
					}

					curr->pbend = pbend;
				}

				cvol = g_bmm_channel_controller_volume[ch];
				if (curr->cvol != cvol)
				{
					curr->cvol = cvol;
				}
			}

			curr->finenote = ((s16)curr->note << pbend_bits) + curr->pbend;


			// note: [phys.source.pos/base] now reflect DMA buffer contents muxed so far

//			if (unlikely(curr->phys.sl_op_samples <= 0))
			if (curr->noteon == false)
			{
				if (curr->penv)
				{
					// unlock sustain: ADS->R
					curr->psustain = 0;
				}
				else
				{
					// stop playback immediately
					curr->status_ = status_complete;
				}

				// truncate note
				//curr->noteon = false;
			}

		} // status_active

		if (_dbg_counter_++ > 100)
		{ eprintf("error: audiodevice: subframe_event infinite loop\n"); _dbg_counter_ = 0; }

		// next vchannel
		pcurr = pnext;
		pnext = pcurr->p_uoanext;
	}



}

//-----------------------------------------------------------------------------
// generate a sample block which contributes to a complete sample frame

//s16 s_pass = 0x7fff;

/*
PRFINLINE s32 subframe_block(s32 _completed, s32 _target)
{
	s32 blocksize;
	s16 n;

	// find next channel to yield an event, number of output samples before that event
	blocksize = _target - _completed;
	{
		virtual_channel_t* curr;
		s16 pc;


		for (pc = 0; pc < real_physical_channels; ++pc)
		{
			s16 ch;

			curr = active_physical[pc];

			// determine if this is a BMM channel or SFX. BMM channels are affected by controllers
//			ch = curr->handle - max_sfx_handles;
			ch = curr->chn;
			if (ch >= 0)
			{
				s16 pbend, pbnote;
				u8 cvol;
				// this is a BMM channel
//				ch >>= 7;

				cvol = g_bmm_channel_controller_volume[ch];
				if (curr->cvol != cvol)
				{
					curr->cvol = cvol;
				}
			}

			if (curr->phys.sl_op_samples <= 0)
				eprintf("audiodevice: error - channel %d has %d active samples!\n", 
					(int)pc, curr->phys.sl_op_samples); 

			if (curr->phys.sl_op_samples < blocksize)
				blocksize = curr->phys.sl_op_samples;
		}
		if (blocksize <= 0)
			eprintf("audiodevice: error - found active channel with no samples pending\n"); 
	}

	subframe_step(blocksize);

	// return blocksize for block processing loop
	return blocksize;
}
*/

//-----------------------------------------------------------------------------
//	perform deferred mixing
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// generate a complete sample frame



extern s16 audioframe_collisions;

extern "C"
{

int subframe_event_68k();


void audio_mux_frame(void)
{
	s32 muxed = 0;
	s32 muxlimit = 0;
	s16 frameevent;
	int change = 0;

	bool eventdirty = false;

//	reg16(ffff8240) = 0x700;

//	if (!g_serverlocks)
//	{

//	s_pass++;

	if (audioframe_collisions)
	{ eprintf("error: audiodevice: mux timecritical overrun!\n"); audioframe_collisions = 0; }

//	output();

	if (g_flushchannels)
	{
		g_flushchannels = 0;
		api_channel_cleanup();
	}

//	reg16(ffff8240) = 0x770;

	dprintf("audiodevice: audio_mux_frame [begin]\n");

//	change += sfx_event_feeder();

	// this pulls 'manually pushed' events from the game logic instead of
	// a music track/stream - used to implement instrument test UI
	//change += BM_A_BMMPullEvents();

	// perform subframe if no music events pending
//	if (change && !(g_bmmstream_eventcount && (g_bmm_pending_samples <= 0)))
//		subframe_event();

//	reg16(ffff8240) = 0x777;

//	if (g_bmmstream_eventcount)
//	{


	if (g_bmmstream_eventcount)
	{

		bmm_event_feeder_68k();
		eventdirty = true;

/*
		ev_target += samples_per_frame;

		while (g_bmmstream_eventcount && (ev_processed < ev_target))
		{
			s32 duration = bmm_event_feeder_68k();
			if ((duration == 0) && g_bmmstream_loop)
			{
				api_channel_cleanup();
				dprintf("song: loop!\n");

				duration = bmm_event_feeder_68k();
			}

			ev_processed += duration;

			eventdirty = true;
		}
*/
	}


#if (0)

	// render ~5ms time block until complete
	while (g_bmmstream_eventcount && (muxed < samples_per_frame))
	{
		s32 bmm_fragment, delta;

		if (/*g_bmmstream_eventcount && */(g_bmm_pending_samples <= 0))
		{
			dprintf("audiodevice: audio_mux_frame [bmm event]\n");
//			while (unlikely(bmm_event_feeder() == 0) && g_bmmstream_loop)
			while (unlikely(bmm_event_feeder_68k() == 0) && g_bmmstream_loop)
			{
				api_channel_cleanup();
				dprintf("song: loop!\n");
			}

			// at least one event occurred
			// may affect: channels, controllers
			eventdirty = true;
		}

		// find samples pending (fragment) until next bmm event
		// if no events pending, just schedule a full frame (samples_per_frame)
		bmm_fragment = g_bmmstream_eventcount ? 
			g_bmm_pending_samples : samples_per_frame;

		// fragment must be clipped against remainder of frame
		delta = samples_per_frame - (muxed + bmm_fragment);
		if (delta < 0)
			bmm_fragment += delta;

		//printf("audiodevice: audio_mux_frame [block=%d pending=%d]\n", block, g_bmm_pending_samples);

		// account for pending bmm samples
		g_bmm_pending_samples -= bmm_fragment;

		muxed += bmm_fragment;

	}

#endif

//	}

//	if (eventdirty)
//		subframe_event_68k();
//		subframe_event();

//	reg16(ffff8240) = 0x077;

//	frame_event();

//	reg16(ffff8240) = 0x707;

	// remove completed channels
//	frame_cleanup();

//	reg16(ffff8240) = 0x007;

//	map_channels();
//	} // (!g_serverlocks)
}



}

//-----------------------------------------------------------------------------

//static s_gamma[256];

#if (1)
char *load_sample(int id, void* own)
{
//	lumpname_t *l = &g_lumpnames[id];
//	char* filename = l->filename;

	yminstrument_t *pymi = &g_yminstrument_dict[id];
	const char* filename = pymi->p_samplename;

	if (!filename)
		return NULL;

//	printf("statting wav: %s [%d]\n", filename, id);

	size_t bytes = fsize(filename);
	if (bytes)
	{
		//printf("trying wav: %s\n", filename);
		FILE* fp = fopen(filename, "rb");
		if (fp)
		{
			pprintf("opened: [%d] %s @ %d\n", ((mperc_last_-id)+midi_mperc_start_), filename, bytes);
			pymi->p_wavalloc = (u8*)ealloc(bytes+64);
			fread(pymi->p_wavalloc, 1, bytes, fp);
			fclose(fp); fp = 0;

			// find 'data' marker

			int stry = 256;
			char *src = (char*)(pymi->p_wavalloc);
			while  (((src[0] != 'd') ||
					 (src[1] != 'a') ||
					 (src[2] != 't') ||
					 (src[3] != 'a')) &&
					(stry > 0))
			{
				src++;
				stry--;
			}
			if (stry == 0)
			{
				wprintf("warning: could not locate wav 'data'\n");
				return 0;
			}
			
			//printf("stuff: %s\n", src);


			// skip data marker
			src += 4;

			// get size field (little endian)

			long_t len = 0;
			len |= *src++;
			len <<= 8;
			len |= *src++;
			len <<= 8;
			len |= *src++;
			len <<= 8;
			len |= *src++;

			endianswap32((long_t*)&len, 1);

//			printf("wav length: %08x\n", len);


		
//			while(1) { }

			// start of data (header = size)
//			pymi->p_wav = (s8*)src;

			// advance to data
//			src += 4;
			pymi->p_wav = (s8*)src;

			// copy/convert data
			s16 slen = (s16)len;
			for (s16 p = 0; p < slen; ++p)
			{
				*src = g_sampletranslation[*((u8*)src)];
				src++;
				//*src = (*src) + 128; src++;
			}

			// pad with zeroes
			len += 64;
			for (s16 p = 0; p < 64; ++p)
			{
				*src++ = 0;
			}

			// trim size to leave only 2 zeroes

			src--;
			while (((*src) == 0) && (len > 0))
			{
				len--;
				src--;
			}

			// include two padding zeroes in sample record
			len += 2;

			pymi->samplelen = len;

			// store adjusted size in sample header, for use by player
//			*(int*)(pymi->p_wav) = size;

			//printf("\n");
			//for (s16 x = 0; x < 8; x++)
			//{
			//	printf(" %d", (int)pymi->p_wav[x]);
			//}

			return (char*)(pymi->p_wav);
		}
	}

	wprintf("warning: could not open wav: %s\n", filename);

	return 0;
}
#endif

// todo: move this table out of .h
yminstrument_t g_yminstrument_dict[YMI_NUM] =
{
	{ EP_Std,		0,1+0,							0,0,0,0		},
	//
	{ EP_Std,		envseq_piano,1+30,				0,0,0,0		},
	{ EP_Std,		envseq_piano2,-1,				0,0,0,0		},
	//
	{ EP_Std,		envseq_bassbuzz_tri,1+0,		0,0,0,0		},
	{ EP_Std,		envseq_bassbuzz_saw,1+0,		0,0,0,0		},
	{ EP_Std,		envseq_bassphase_sqr_tri,1+0,	0,0,0,0		},
	{ EP_Std,		envseq_bassphase_sqr_saw,1+0,	0,0,0,0		},
	//
	{ EP_Std,		envseq_fretphase_sqr_tri,1+6,	0,0,0,0		},
	{ EP_Std,		envseq_bassfret,1+6,			0,0,0,0		},
	//
	{ EP_Std,		envseq_chime,-1,				0,0,0,0		},
	{ EP_Std,		envseq_strings,1+8,				0,0,0,0		},

	// not loaded, but mapped/indexed anyway due to lack of translation table for percussion
	{  EP_Med,		0,0,				"sounds\\potria.wav",0,0,0 },	//	mperc_otria
	{  EP_Med,		0,0,				"sounds\\pctria.wav",0,0,0 },	//	mperc_mtria,
	//
	{  EP_Med,		0,0,				"sounds\\pocuic.wav",0,0,0 },	//	mperc_ocuic,
	{  EP_Med,		0,0,				"sounds\\pmcuic.wav",0,0,0 },	//	mperc_mcuic,
	{  EP_Med,		0,0,				"sounds\\plwood.wav",0,0,0 },	//	mperc_lwood,
	{  EP_Med,		0,0,				"sounds\\phwood.wav",0,0,0 },	//	mperc_hwood,
	{  EP_Med,		0,0,				"sounds\\pclave.wav",0,0,0 },	//	mperc_clave,
	{  EP_Med,		0,0,				"sounds\\plguir.wav",0,0,0 },	//	mperc_lguir,
	{  EP_Med,		0,0,				"sounds\\psguir.wav",0,0,0 },	//	mperc_sguir,
	{  EP_Med,		0,0,				"sounds\\plwhis.wav",0,0,0 },	//	mperc_lwhis,
	{  EP_Med,		0,0,				"sounds\\pswhis.wav",0,0,0 },	//	mperc_swhis,
	{  EP_Med,		0,0,				"sounds\\pmarac.wav",0,0,0 },	//	mperc_marac,
	{  EP_Med,		0,0,				"sounds\\pcabas.wav",0,0,0 },	//	mperc_cabas,
	{  EP_Med,		0,0,				"sounds\\ploago.wav",0,0,0 },	//	mperc_loago,
	{  EP_Med,		0,0,				"sounds\\phiago.wav",0,0,0 },	//	mperc_hiago,
	{  EP_Med,		0,0,				"sounds\\plotim.wav",0,0,0 },	//	mperc_lotim,
	{  EP_Med,		0,0,				"sounds\\phitim.wav",0,0,0 },	//	mperc_hitim,
	{  EP_Med,		0,0,				"sounds\\plocon.wav",0,0,0 },	//	mperc_locon,
	{  EP_Med,		0,0,				"sounds\\pohico.wav",0,0,0 },	//	mperc_ohico,
	{  EP_Med,		0,0,				"sounds\\pmhico.wav",0,0,0 },	//	mperc_mhico,
	{  EP_Med,		0,0,				"sounds\\plobon.wav",0,0,0 },	//	mperc_lobon,
	//
	{  EP_Med,		0,0,				"sounds\\phibon.wav",0,0,0 },
	{  EP_MedLow,	0,0,				"sounds\\prcym2.wav",0,0,0 },
	{  EP_Med,		0,0,				"sounds\\pvib.wav",  0,0,0 },
	{  EP_MedLow,	0,0,				"sounds\\pccym2.wav",0,0,0 },
	{  EP_Med,		0,0,				"sounds\\pcbell.wav",0,0,0 },
	{  EP_MedLow,	0,0,				"sounds\\pspcym.wav",0,0,0 },
	{  EP_Med,		0,0,				"sounds\\ptambo.wav",0,0,0 },
	{  EP_Med,		0,0,				"sounds\\prbell.wav",0,0,0 },
	{  EP_MedLow,	0,0,				"sounds\\pchcym.wav",0,0,0 },
	{  EP_MedLow,	0,0,				"sounds\\prcym1.wav",0,0,0 },
	{  EP_MedHigh,	0,0,				"sounds\\phitom.wav",0,0,0 },
	{  EP_MedLow,	0,0,				"sounds\\pccym1.wav",0,0,0 },
	{  EP_MedHigh,	0,0,				"sounds\\phmtom.wav",0,0,0 },
	{  EP_MedHigh,	0,0,				"sounds\\plmtom.wav",0,0,0 },
	{  EP_Low,		0,0,				"sounds\\pohhat.wav",0,0,0 },
	{  EP_MedHigh,	0,0,				"sounds\\plotom.wav",0,0,0 },
	{  EP_Low,		0,0,				"sounds\\pphhat.wav",0,0,0 },
	{  EP_MedHigh,	0,0,				"sounds\\phftom.wav",0,0,0 },
	{  EP_Low,		0,0,				"sounds\\pchhat.wav",0,0,0 },
	{  EP_MedHigh,	0,0,				"sounds\\plftom.wav",0,0,0 },
	{  EP_High,		0,0,				"sounds\\pesnar.wav",0,0,0 },
	{  EP_Med,		0,0,				"sounds\\pclap.wav", 0,0,0 },
	{  EP_High,		0,0,				"sounds\\pasnar.wav",0,0,0 },
	{  EP_Med,		0,0,				"sounds\\psstik.wav",0,0,0 },
	{  EP_MedHigh,	0,0,				"sounds\\pbass1.wav",0,0,0 },
	{  EP_MedHigh,	0,0,				"sounds\\pbass2.wav",0,0,0 }
};

u8 *envseq_table[] =
{
	envseq_strings,
	envseq_piano,
	envseq_piano2,
	envseq_bass,
	envseq_bassphase_sqr_tri,
	envseq_bassphase_sqr_saw,
	envseq_bassbuzz_tri,
	envseq_bassbuzz_saw,
	envseq_fretphase_sqr_tri,
	envseq_fretphase_sqr_saw,
	envseq_bassfret,
	envseq_chime,
};

static const int num_envseq = sizeof(envseq_table) / sizeof(u8*);

void calibrate_envsequences()
{
	if (1)
	for (s16 e = 0; e < num_envseq; e++)
	{
		u8* penv = envseq_table[e];

		u8 *psustain = penv;
		u8 i_sustain = *penv++;
		u8 i_waveform = *penv++;

		// find seq length
		u8 *pscan = penv;
		s16 length = 0;
		while (*pscan++)
			length++;

		// cap sustain position in case of accidents
		if ((i_sustain != 0xff) && (i_sustain > length))
			i_sustain = length;

		// recalibrate for timestep
		s16 spos = 0;
		s16 dlen = 0;
		while (spos < length)
		{
			penv[dlen] = penv[spos];
			spos += 4;
			dlen += 1;
		}
		// terminate
		penv[dlen] = 0;

		// recalibrate sustain position in sequence, if not -1
		if (i_sustain != 0xff)
		{
			s16 d_sustain = i_sustain / 4;

			// sustain present - cap to last position in sequence
			if (d_sustain > dlen)
				d_sustain = dlen;

			// copy original sustain value to new sustain pos
			penv[d_sustain] = penv[i_sustain];

			// rewrite position
			*psustain = d_sustain;
		}
	}

	for (s16 ymi = 0; ymi < YMI_NUM; ymi++)
	{
		// move sustain index from start of env sequence to dictionary
		yminstrument_t &ins = g_yminstrument_dict[ymi];

		// start of original description
		u8 *penv = ins.p_envseq;

		// fetch sustain index (or -1)
		s16 sustainpos = (s16)(s8)(*penv++);

		// adjust start - point to actual env sequence
		ins.p_envseq = penv;

		if (sustainpos >= 0)
			ins.env_rel = sustainpos+1;
		else
			ins.env_rel = -1;
	}
}

//-----------------------------------------------------------------------------

extern u32 raster_callback_ptr;

//-----------------------------------------------------------------------------

#pragma GCC diagnostic pop

#endif // ENABLE_AGT_AUDIO