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

#ifdef ENABLE_AGT_AUDIO

// 1 = expect .bmu files packed with p2e wrapper
// 0 = expect .bmz raw files
#define AGT_CONFIG_PACKED_BMU (1)

// ---------------------------------------------------------------------------------------------------------------------
//	system headers

// ---------------------------------------------------------------------------------------------------------------------
//	Our AGT headers

#include "../common_cpp.h"

#include "../ealloc.h"
#include "../compress.h"

#include "sound.h"
#include "music.h"

//----------------------------------------------------------------------------------------------------------------------

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

//----------------------------------------------------------------------------------------------------------------------

// force flush of active channels
extern volatile s8 g_flushchannels;

extern volatile s8 g_soundtick;

// force single GM instrument on each MIDI channel, otherwise use programmed instruments
u8 channel_gmi_overrides[16] __attribute__((aligned(2)));
// select one GM instrument on each MIDI channel to override volume
u8 channel_gmi_volume_overrides[16] __attribute__((aligned(2)));
// volume to use for instrument volume override
u8 channel_gmi_volume_override_values[16] __attribute__((aligned(2)));

//----------------------------------------------------------------------------------------------------------------------
// Start BMZ music track

void AGT_MusicStart(int _looping)
{
	ev_target = 0;
	ev_processed = 0;

	g_bmmstream_pos = bmmstream_startpos;
	g_bmmstream_eventcount = g_bmmstream_restarteventcount;
	g_bmmstream_loop = _looping;
	g_looped = 0;
}

//----------------------------------------------------------------------------------------------------------------------
// Stop BMZ music track, wait for channel flush

void AGT_MusicStop()
{
	// stop replay
	ev_target = 0;
	ev_processed = 0;

	g_bmmstream_eventcount = 0;
	g_bmmstream_pos = 0;

	// remove lingering channels
//	api_channel_cleanup();
	++g_flushchannels;
	
	s8 st = g_soundtick;
	while (st == g_soundtick) { }
	st = g_soundtick;
	while (st == g_soundtick) { }

}

//----------------------------------------------------------------------------------------------------------------------
// Load BMZ music track, prepare to play

void AGT_MusicLoad(const char* _name)
{

    int i;
	char name[128];
	char loadname[128];

	u8 gmi_map[gmi_NUM_] __attribute__((aligned(2)));

	FILE* fh;

	remap_ft remap_func = 0;

	AGT_MusicStop();

	g_serverlocks++;

	// here we get a last chance to manipulate source material in ways that
	// would be effort-intensive to tune/adjust from the editing side
	// this mainly includes instrument remapping & octave shifting

	for (s16 ci = 0; ci < 16; ci++)
	{
		channel_gmi_overrides[ci] = 0;
	}

	for (s16 ci = 0; ci < 16; ci++)
	{
		channel_gmi_volume_overrides[ci] = 0xff;
		channel_gmi_volume_override_values[ci] = 0;

		// reset channel controler volumes
		g_bmm_channel_controller_volume[ci] = (max_vol_states-1);
		g_bmm_channel_controller_pbend[ci] = 0;
	}

	{
		s16 i;
		for (i = 0; i < 256; ++i)
			g_instrument_map[i] = YMI_Piano;	// missing instrument indicator
	}

	for (s16 i = 0; i < gmi_NUM_; ++i)
		gmi_map[i] = i;					// missing instrument indicator
 

	for (s16 i = 0; i < 16; i++)
		g_bmm_channel_octave_map[i] = 0;

	// user gets a chance to remap & route instruments, perform channel tweaks before replay

	AGT_MusicFixupCallback(_name, remap_func);



	strncpy(loadname, "music\\", 7);		// isolation dir for all music
	strcat(loadname, _name);				// subpath/name for music file

	strncpy(name, _name, sizeof(name));
		
	pprintf("loading track: %s\n", loadname);

	// load-and-unpack asset file to memory buffer
	u32 info = 0;
	u8* passet = load_asset
	(
		loadname,
		AssetFlags(
			af_load_unwrapped |
			af_size_prefix | af_size_lendian | af_size_exclusive	// prefixed LE size word, non-inclusive
		),
		&info // returns total size of file, including prefix, status flags
	);

	if (!passet)
	{
		eprintf("error: failed to load track: %s\n", loadname);
		AGT_HALT();
	}
	else
	{
		// separate packed flag from size
		u32 size = info & 0x7fffffff;
		int packed_flag = info ^ size;

		u8* last_bmm_alloc = bmm_alloc;
		bmm_alloc = passet;

		bmm_ptr = passet + 4;

		efree(last_bmm_alloc);
	}

/*
	const int s = sizeof(name);
	strncpy(name, _name, s);
	s16 d = 0;
	for (; (d < s) && name[d] && (name[d] != '.'); d++) { }
	name[d] = 0;

	strncpy(loadname, "music\\", 7);		// isolation dir for all music
	strcat(loadname, name);				// subpath/name for music file
	strcat(name, ".bmz");				// name for scanning/fixup
	
#if (AGT_CONFIG_PACKED_BMU)
	strcat(loadname, ".bmu");
#else
	strcat(loadname, ".bmz");
#endif

	fh = fopen(loadname, "rb");
	if (fh == 0)
	{
#if (AGT_CONFIG_PACKED_BMU)
		eprintf("error: failed to load BMU: %s\n", loadname);
#else
		eprintf("error: failed to load BMZ: %s\n", loadname);
#endif
	}
	else
*/

	{
/*
		u8* old_ptr = bmm_ptr;
		bmm_ptr = 0;

		// read filesize
		u32 datasize = 0;
		fread(&datasize, 1, 4, fh);
		endianswap32(&datasize, 1);


		bmm_ptr = (u8*)ealloc(datasize);
		fread(bmm_ptr, 1, datasize, fh);
		fclose(fh);

		reg8(a4) = 0;

		// check for packed indicator
		if (*((u32*)bmm_ptr) == 'pk2e')
		{
			// skip 'pk2e' ident
			u8* data_ptr = bmm_ptr; data_ptr += 4;

			// fetch size of unpacked data
			datasize = *((u32*)data_ptr); data_ptr += 4;
			endianswap32(&datasize, 1);

			// true destination when unpacked
			u8 *unpack_ptr = (u8*)ealloc(datasize+256);
			// unpack
			qmemclr(unpack_ptr, datasize+256);

			DePack2e_68k(data_ptr, unpack_ptr);

			// remove file header
			qmemcpy(unpack_ptr, unpack_ptr+4, datasize);

			// release file buffer
			efree(bmm_ptr);

			// data is here
			bmm_ptr = unpack_ptr;
		}
*/

		// convert intel nonsense to bigendian
		{
			u16 s;
			int *pdata32, *pdata32out;
			char *pdata8, *pdata8out;
			unsigned short *pdata16, *pdata16out, *pdata16reset, *pappendduration;
			bool real_event = false;
			
			pdata16 = (unsigned short*)bmm_ptr;
			pappendduration = 0;

			u16 tmp = (u16)(*pdata16++);
			endianswap16(&tmp, 1);

			pdata16out = (unsigned short*)pdata16;

			bmmseq_size = g_bmmstream_restarteventcount = tmp;
			g_bmmstream_pos = bmmstream_startpos = g_bmmstream_restartpos = (u8*)pdata16;
	
//			printf("sequence length: %d events\n", (int)bmmseq_size);

			for (s = 0; s < bmmseq_size; ++s)
			{
				u8 chn,c,i,v,n;

				pdata16reset = pdata16out;

				// duration
//				*pdata16 = SHORT(*pdata16); pdata16++;

				unsigned short duration = *pdata16++;
				endianswap16((word_t*)&duration, 1);
				*pdata16out++ = duration;
	
				pdata8 = (char*)pdata16;
				pdata8out = (char*)pdata16out;

				// control, instrument, volume, note
				c = pdata8[0];
				i = pdata8[1];
				v = pdata8[2];
				n = pdata8[3];

				chn = (c >> 4) & 0xf;

				if (channel_gmi_overrides[chn] != 0)
					i = channel_gmi_overrides[chn];

				bool keepevent =
					// kill events from unmapped channels
					(g_bmm_channel_multiplex_map[chn] < 8)
					// kill controller events from percussion channel
					&& (!((chn == (10-1)) && (c & 8)))
					// kill lone velocity changes on percussion channel
					&& (!((chn == (10-1)) && (c == 1)))
					// kill notes mapped to YMI_None
					&& (g_instrument_map[i] != YMI_None);


				if ((chn == (10-1)) && keepevent)
				{
					// map unavailable percussion sounds to first sound,
					// to prevent corrupt playback
					if ((n >= midi_mperc_end_) || 
						(n < midi_mperc_start_) ||
						((n <= 79) && (n >= 61))	// unmapped region
					)
					{
						keepevent = false;

						wprintf("warning: audiodevice: track uses unmapped percussion [gmi=%d+1]\n", n);
					}
				}

				real_event = real_event || (!(c & 8));

				// collapswe long waits at the start of faulty midi tracks
				if ((!real_event) && (duration > 20))
				{
					*pdata16reset = duration = 20;
				}

				if (keepevent)
				{
//					while ((c & 0x7) == 3) { }

					if (c & 8)
					{
						// controller change
						dprintf("audiodevice: bmmload: controller change: chn[%d] ctrl[%d] value[%d]\n", 
							(int)chn, (int)n, (int)v);

					}
					else
					{
						// note update

	//drum: 0394 -> [036][kick drum 1]
	//drum: 0148 -> [040][snare drum 2]
	//drum: 0412 -> [042][closed high hat exc1]
	//drum: 0028 -> [046][open high hat exc1]
	//drum: 0044 -> [051][ride cymbal 1]
	//drum: 0045 -> [059][ride cymbal 2]
	//drum: 0006 -> [069][cabasa]
	//drum: 0006 -> [075][claves]
	//drum: 0004 -> [080][mute triangle exc5]
	//drum: 0001 -> [081][open triangle exc5]
	//drum: 0006 -> [082][shaker]
	//drum: 0008 -> [085][castanets]


					if (remap_func != 0)
					{
						if (0 == remap_func(chn, &i, &n, &v))
						{
							n = 0;
							v = 0;
						}
					}
					// translate source instruments to available instruments using
					// temporary GM instrument map
					else
					{
						i = gmi_map[i];
					}

					if (channel_gmi_volume_overrides[chn] == i)
						v = channel_gmi_volume_override_values[chn];

	//				printf("chn[%d] volume: %d\n", (int)chn, (int)v);

					}

//					if (g_bmm_channel_multiplex_map[chn] >= 8)
//						while (1) { }

					pdata8out[0] = c;
					pdata8out[1] = i;
					pdata8out[2] = v;
					pdata8out[3] = n;
				
					pdata8out += 4;
					pdata16out = (unsigned short*)pdata8out;

					// track duration of last emitted event so we can account for dropped events
					pappendduration = pdata16reset;
				}
				else
				{
					// removed an event
					g_bmmstream_restarteventcount--;

					// account for lost time, if any
					if (pappendduration)
						*pappendduration += duration;

					// don't advance output ptr
					pdata16out = pdata16reset;
				}

				pdata8 += 4;
				pdata16 = (unsigned short*)pdata8;

			}
		}
		dprintf("loaded track: %s\n", name);

		bmmseq_size = g_bmmstream_restarteventcount;

		if (0)
		{
			u8 chn,c,i,v,n;

			reg32(5c0) = (int)bmm_ptr;

			u8 *data = bmm_ptr;
			data += 2;

			u16 count = bmmseq_size;
			while (count-- > 0)
			{
				u16 d = *(unsigned short*)data; data += 2;

				c = *data++;
				i = *data++;
				v = *data++;
				n = *data++;
			
				chn = (c >> 4) & 0xf;

				if (g_bmm_channel_multiplex_map[chn] >= 8)
				{
					// signal error in multiplexing map
					while (1) { reg16(ffff8240) = ~reg16(ffff8240); }
				}
			}

			reg32(5c4) = (int)data;
		}

		//efree(old_ptr);

	}
	--g_serverlocks;
}

//-----------------------------------------------------------------------------
// stub in case audio not imported to a given project, but audio still linked

#if !defined(ENABLE_AGT_AUDIO)
void AGT_MusicFixupCallback(const char* const _name, remap_ft &remap_func)
{
}
#endif // defined(ENABLE_AGT_AUDIO)

#pragma GCC diagnostic pop

#endif // ENABLE_AGT_AUDIO