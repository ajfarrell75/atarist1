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

#ifndef sounds_h_
#define sounds_h_

//-----------------------------------------------------------------------------

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

//-----------------------------------------------------------------------------

// 1 = force to 25khz (currently breaks ingame sfx, but will be used for titles)
#ifdef USE_HQ_AUDIO
#define dma_hq (1)
#else
#define dma_hq (0)
#endif

#define pbend_bits		(5)
#define pbend_steps		(1<<pbend_bits)

#define note_reference	(60)				// reference note (C4) against which instrument samples (22050Hz) were recorded (2x replay rate @ 11025Hz)
#define note_unitstep	(note_reference-12)	// note corresponding to unit sample increment ($00010000)
											// important: percussion and other 'fixed frequency' sounds recorded at 11025 and replayed with unit increment
#define note_synref		(note_unitstep+2)	// synth reference note (E3)

#define note_variable	(0)

#define note_c8			(60+48)
#define note_c7			(60+36)
#define note_c6			(60+24)
#define note_c5			(60+12)
#define note_c4			(60)
#define  note_as3		(60-2)
#define  note_a3		(60-3)
#define note_c3			(60-12)
#define note_c2			(60-24)
#define  note_a2		(note_a3-12)

#define note_var_c8		(-(note_reference-48))
#define  note_var_b8	(-(note_reference-47))
#define note_var_c7		(-(note_reference-36))
#define note_var_c6		(-(note_reference-24))
#define note_var_c5		(-(note_reference-12))
#define note_var_c4		(-(note_reference))
#define  note_var_as3	(-(note_reference+2))
#define  note_var_a3	(-(note_reference+3))
#define note_var_c3		(-(note_reference+12))
#define note_var_c2		(-(note_reference+24))

#define num_joinpoint_bits (5)
#define num_joinpoints (1<<num_joinpoint_bits)
#define join_scale (2)

#define num_jumppoint_bits (5)
#define num_jumppoints (1<<num_jumppoint_bits)
//#define jump_grain (8)

// can jump at resolution of 256 samples

#define env_timebase_shift (8+dma_hq)

#define adsrctrl_softrelease (-1)
#define adsrctrl_fastrelease (-2)
#define adsrctrl_symmrelease (-3)
#define adsrctrl_immediaterelease (-4)






enum EventPriority
{
	EP_High			=	0,
	EP_MedHigh		=	1,
	EP_Med			=	2,
	EP_MedLow		=	3,
	EP_Low			=	4,
	EP_Background	=	5,

	EP_Std			=	EP_Med
};


/*
enum MixMode
{
	MixMode_NONE,
	MixMode_PRIORITY_ANY,			// note gets priority in any free channel
	MixMode_PRIORITY_A,				// note gets priority in channel A when struck
	MixMode_PRIORITY_B,				// note gets priority in channel B when struck
	MixMode_PRIORITY_C,				// note gets priority in channel C when struck
	MixMode_BACKGROUND_A,			// background note in A
	MixMode_BACKGROUND_B,			// background note in B
	MixMode_BACKGROUND_C,			// background note in C
	MixMode_MPLEX_A,				// note will multiplex with others in A
	MixMode_MPLEX_B,				// note will multiplex with others in B
	MixMode_MPLEX_C,				// note will multiplex with others in C
};
*/

static u8 envseq_strings[] =
{
	12,										// [sustain] position in [sequence]
	0xff,									// waveform
											// [sequence] begins here...
	7,
	9,11,
	13,14,
	15,15,15,
	15,15,15,15,
	14,14,14,14,
	14,14,14,14,
	13,13,13,13,
	12,12,12,12,
	11,11,11,11,
	10,10,10,10,
	9,9,9,9,9,9,
	8,8,8,8,8,8,
	7,7,7,7,7,7,
	6,6,6,6,6,6,
	5,5,5,5,5,5,
	4,4,4,4,4,4,
//	3,3,3,3,3,3,
//	2,2,2,2,2,2,
//	1,1,1,1,1,1,
	0
};

// todo: move this table out of .h
static u8 envseq_piano[] =
{
	30,										// [sustain] position in [sequence]
	0xff,									// waveform
											// [sequence] begins here...
	7,
	10,
	13,
	15,15,15,15,
	15,15,15,15,
	14,14,14,14,
	14,14,14,14,
	13,13,13,13,
	12,12,12,12,
	11,11,11,11,
	10,10,10,10,

	9,9,9,9,9,9,
	8,8,8,8,8,8,
	7,7,7,7,7,7,
	6,6,6,6,6,6,
	5,5,5,5,5,5,
	4,4,4,4,4,4,
//	3,3,3,3,3,3,
//	2,2,2,2,2,2,
//	1,1,1,1,1,1,
	0
};

static u8 envseq_piano2[] =
{
	0xff,									// [sustain] position in [sequence]
	0xff,									// waveform
											// [sequence] begins here...
	15,15,15,15,
	14,14,14,14,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	12,12,12,12,
	12,12,12,12,
	12,12,12,12,
	11,11,11,11,
	11,11,11,11,
	11,11,11,11,
	11,11,11,11,
	10,10,10,10,
	10,10,10,10,
	10,10,10,10,
	10,10,10,10,

	9,9,9,9,9,9,
	9,9,9,9,9,9,
	9,9,9,9,9,9,
	9,9,9,9,9,9,
	8,8,8,8,8,8,
	8,8,8,8,8,8,
	8,8,8,8,8,8,
	8,8,8,8,8,8,
	7,7,7,7,7,7,
	7,7,7,7,7,7,
	7,7,7,7,7,7,
	7,7,7,7,7,7,
	6,6,6,6,6,6,
	6,6,6,6,6,6,
	6,6,6,6,6,6,
	6,6,6,6,6,6,
	5,5,5,5,5,5,
	5,5,5,5,5,5,
	5,5,5,5,5,5,
	5,5,5,5,5,5,
	4,4,4,4,4,4,
	4,4,4,4,4,4,
	4,4,4,4,4,4,
	4,4,4,4,4,4,
//	3,3,3,3,3,3,
//	2,2,2,2,2,2,
//	1,1,1,1,1,1,
	0
};

static u8 envseq_bass[] =
{
	16,										// [sustain] position in [sequence]
	0x08,									// waveform
											// [sequence] begins here...
//	0x07,0x0a,0x0c,0x0f,
	0x3f,0x3f,0x3f,0x3f,

	0x3f,0x3f,0x3f,0x3f,
	0x3f,0x3f,0x3f,0x3f,
	0x3f,0x3f,0x3f,0x1f,
/*
	0x10,0x30,0x10,0x30,
	0x10,0x30,0x10,0x30,
	0x10,0x30,0x10,0x30,
	0x10,0x30,0x10,0x30,
	0x10,0x30,0x10,0x30,
	0x10,0x30,0x10,0x30,
	0x10,0x30,0x10,0x30,
	0x10,0x30,0x10,0x30,
*/	

	/*
//	0x0f,
//	0xf,
//	0x30,
//	0x7,
//	0xf,
//	0xf,
//	0xf,
	0x30,
	0x30,
	0x30,
	0x30,
	0x30,
	0x30,
	0x30,
	0x30,

	0x10,


	0x9,
	0x8,
	0x7,
	0x6,
	0x5,
	0x4,
	0x3,
	0x2,
	0x1,

//	0x10,
//	0x10,
//	0x10,
//	0x10,
*/
	0
/*
	10,12,13,14,
	15,15,15,15,
	14,14,14,14,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	13,13,13,13,
	11,11,11,11,
	10,10,10,10,
	9,9,9,9,9,9,
	8,8,8,8,8,8,
	7,7,7,7,7,7,
	6,6,6,6,6,6,
	5,5,5,5,5,5,
	4,4,4,4,4,4,

	0
*/
};

// waveform init.
#define WBuzzReset		(0x40)	// just reset envelope
#define WSqreReset		(0x20)	// reset both envelope & squarewave
#define WBuzzSaw		(0x0a)
#define WBuzzTri		(0x08)

// envelope flags
#define EBuzzEnable		(0x10)	// enable buzz waveform
#define ESqreDisable	(0x20)	// disable square waveform (buzz only)

//static u8 envseq_bassphase_sqr_tri[] =
//{
//	0x08,
//	0x1f,
//	0
//};

//static u8 envseq_bassphase_sqr_saw[] =
//{
//	0x0a,
//	0x1f,
//	0
//};


static u8 envseq_bassphase_sqr_tri[] =
{
	0,										// [sustain] position in [sequence]
	WBuzzTri|WSqreReset,					// waveform
											// [sequence] begins here...
	0x0f | (EBuzzEnable/*|ESqreDisable*/),
	0
};

static u8 envseq_bassphase_sqr_saw[] =
{
	0,										// [sustain] position in [sequence]
	WBuzzSaw|WSqreReset,					// waveform
											// [sequence] begins here...
	0x0f | (EBuzzEnable/*|ESqreDisable*/),
	0
};


static u8 envseq_bassbuzz_tri[] =
{
	0,										// [sustain] position in [sequence]
	WBuzzTri,								// waveform
											// [sequence] begins here...
	0x0f | (EBuzzEnable|ESqreDisable),
	0
};

static u8 envseq_bassbuzz_saw[] =
{
	0,										// [sustain] position in [sequence]
	WBuzzSaw,								// waveform
											// [sequence] begins here...
	0x0f | (EBuzzEnable|ESqreDisable),
	0
};

static u8 envseq_fretphase_sqr_tri[] =
{
	6,										// [sustain] position in [sequence]
	WBuzzTri|WSqreReset|WBuzzReset,			// waveform
											// [sequence] begins here...
	0x0f | (EBuzzEnable|ESqreDisable),
	0x0f | (EBuzzEnable|ESqreDisable),
	0x0f | (EBuzzEnable|ESqreDisable),
	0x0f | (EBuzzEnable|ESqreDisable),
	0x0f | (EBuzzEnable|ESqreDisable),
	0x0f | (EBuzzEnable/*|ESqreDisable*/),
	0
};

static u8 envseq_fretphase_sqr_saw[] =
{
	6,										// [sustain] position in [sequence]
	WBuzzSaw|WSqreReset|WBuzzReset,			// waveform
											// [sequence] begins here...
	0x0f | (EBuzzEnable|ESqreDisable),
	0x0f | (EBuzzEnable|ESqreDisable),
	0x0f | (EBuzzEnable|ESqreDisable),
	0x0f | (EBuzzEnable|ESqreDisable),
	0x0f | (EBuzzEnable|ESqreDisable),
	0x0f | (EBuzzEnable/*|ESqreDisable*/),
	0
};


//static u8 envseq_bassbuzz[] =
//{
//	0x08,
//	0x3f,
//	0
//};

static u8 envseq_bassfret[] =
{
	6,										// [sustain] position in [sequence]
	0x08,									// waveform
											// [sequence] begins here...
	0x3f,0x3f,0x3f,0x3f,
	0x3f,0x1f,
	0
};


static u8 envseq_chime[] =
{
	0xff,			// [sustain] position in [sequence]
	0xff,			// waveform
					// [sequence] begins here...
	15,15,
	14,14,14,
	13,13,13,13,
	12,12,12,12,
	11,11,11,11,
	10,10,10,10,
	9,9,9,9,//9,9,
	8,8,8,//8,8,8,
	7,7,7,//7,7,7,
	6,6,6,//6,6,6,
	5,5,5,//5,5,5,
	4,4,4,//4,4,4,
//	3,3,3,3,3,3,
//	2,2,2,2,2,2,
//	1,1,1,1,1,1,
	0
};


// instrument definition
typedef struct yminstrument_s
{
//	MixMode mmode;					// mixing & prioritization control
	s16 prio;

	u8 *p_envseq;					// software envelope
	s16 env_rel;

	const char *p_samplename;
	u8 *p_wavalloc;
	s8 *p_wav;
	s16 samplelen;

} yminstrument_t;

// 
// Identifiers for MIDI instrument remapping
//

typedef enum
{
	gmi_AcousticGrandPiano,
	gmi_BrightAcousticPiano,
	gmi_ElectricGrandPiano,
	gmi_HonkytonkPiano,
	gmi_ElectricPiano1,
	gmi_ElectricPiano2,
	gmi_Harpsichord,
	gmi_Clavi,
	gmi_Celesta,
	gmi_Glockenspiel,
	gmi_MusicBox,
	gmi_Vibraphone,
	gmi_Marimba,
	gmi_Xylophone,
	gmi_TubularBells,
	gmi_Dulcimer,
	gmi_DrawbarOrgan,
	gmi_PercussiveOrgan,
	gmi_RockOrgan,
	gmi_ChurchOrgan,
	gmi_ReedOrgan,
	gmi_Accordion,
	gmi_Harmonica,
	gmi_TangoAccordion,
	gmi_AcousticGuitar_nylon,
	gmi_AcousticGuitar_steel,
	gmi_ElectricGuitar_jazz,
	gmi_ElectricGuitar_clean,
	gmi_ElectricGuitar_muted,
	gmi_OverdrivenGuitar,
	gmi_DistortionGuitar,
	gmi_Guitarharmonics,
	gmi_AcousticBass,
	gmi_ElectricBass_finger,
	gmi_ElectricBass_pick,
	gmi_FretlessBass,
	gmi_SlapBass1,
	gmi_SlapBass2,
	gmi_SynthBass1,
	gmi_SynthBass2,
	gmi_Violin,
	gmi_Viola,
	gmi_Cello,
	gmi_Contrabass,
	gmi_TremoloStrings,
	gmi_PizzicatoStrings,
	gmi_OrchestralHarp,
	gmi_Timpani,
	gmi_StringEnsemble1,
	gmi_StringEnsemble2,
	gmi_SynthStrings1,
	gmi_SynthStrings2,
	gmi_ChoirAahs,
	gmi_VoiceOohs,
	gmi_SynthVoice,
	gmi_OrchestraHit,
	gmi_Trumpet,
	gmi_Trombone,
	gmi_Tuba,
	gmi_MutedTrumpet,
	gmi_FrenchHorn,
	gmi_BrassSection,
	gmi_SynthBrass1,
	gmi_SynthBrass2,
	gmi_SopranoSax,
	gmi_AltoSax,
	gmi_TenorSax,
	gmi_BaritoneSax,
	gmi_Oboe,
	gmi_EnglishHorn,
	gmi_Bassoon,
	gmi_Clarinet,
	gmi_Piccolo,
	gmi_Flute,
	gmi_Recorder,
	gmi_PanFlute,
	gmi_BlownBottle,
	gmi_Shakuhachi,
	gmi_Whistle,
	gmi_Ocarina,
	gmi_Lead1_square,
	gmi_Lead2_sawtooth,
	gmi_Lead3_calliope,
	gmi_Lead4_chiff,
	gmi_Lead5_charang,
	gmi_Lead6_voice,
	gmi_Lead7_fifths,
	gmi_Lead8_bass_lead,
	gmi_Pad1_newage,
	gmi_Pad2_warm,
	gmi_Pad3_polysynth,
	gmi_Pad4_choir,
	gmi_Pad5_bowed,
	gmi_Pad6_metallic,
	gmi_Pad7_halo,
	gmi_Pad8_sweep,
	gmi_FX1_rain,
	gmi_FX2_soundtrack,
	gmi_FX3_crystal,
	gmi_FX4_atmosphere,
	gmi_FX5_brightness,
	gmi_FX6_goblins,
	gmi_FX7_echoes,
	gmi_FX8_scifi,
	gmi_Sitar,
	gmi_Banjo,
	gmi_Shamisen,
	gmi_Koto,
	gmi_Kalimba,
	gmi_Bagpipe,
	gmi_Fiddle,
	gmi_Shanai,
	gmi_TinkleBell,
	gmi_Agogo,
	gmi_SteelDrums,
	gmi_Woodblock,
	gmi_TaikoDrum,
	gmi_MelodicTom,
	gmi_SynthDrum,
	gmi_ReverseCymbal,
	gmi_GuitarFretNoise,
	gmi_BreathNoise,
	gmi_Seashore,
	gmi_BirdTweet,
	gmi_TelephoneRing,
	gmi_Helicopter,
	gmi_Applause,
	gmi_Gunshot,

	// 128...255
	gmi_PizzicatoStrings_C5,
	gmi_PizzicatoStrings_C6,
	//
	gmi_Oboe_C6,
	gmi_Clarinet_C5,
	//
	gmi_OverdrivenGuitar_C4,
	gmi_OverdrivenGuitar_C5,
	gmi_OverdrivenGuitar_C6,
	//
	gmi_DistortionGuitar_C3,
	gmi_DistortionGuitar_C4,
	gmi_DistortionGuitar_C5,
	//
	gmi_RockOrgan_C6,
	//
	gmi_AcousticGuitar_nylon_C5,
	//
	gmi_StringEnsemble2_C3,
	gmi_StringEnsemble2_C4,
	gmi_StringEnsemble2_C5,
	gmi_StringEnsemble2_C6,
	//
	gmi_SynthStrings_C5,
	//
	gmi_FretlessBass_C2,
	gmi_FretlessBass_C3,


	//
	gmi_FX2_,					// pitchbend fx used only by e2m2
	gmi_Evil_,
	gmi_Q2Bass_,
	gmi_DistortionGuitarChordA3D3_7,
	gmi_DistortionGuitarChordAs3F3_5,
	gmi_UNMAPPED_PERCUSSION,

	gmi_NUM_

} gmienum_t;

typedef enum
{
	gmp_high_q = (28-1),		//_28-1
	gmp_slap,
	gmp_scratch_push,
	gmp_scratch_pull,
	gmp_sticks,
	gmp_square_click,
	gmp_metronome_click,
	gmp_metronome_bell,
	//
	gmp_kick_drum_2,			//_36-1		BM:first
	gmp_kick_drum_1,
	gmp_side_stick,
	gmp_snare_drum_1,
	gmp_hand_clap,
	gmp_snare_drum_2,
	gmp_low_tom_2,
	gmp_closed_high_hat_exc1,
	gmp_low_tom_1,
	gmp_pedal_high_hat_exc1,
	gmp_mid_tom_2,
	gmp_open_high_hat_exc1,
	gmp_mid_tom_1,
	gmp_high_tom_2,
	gmp_crash_cymbal_1,
	gmp_high_tom_1,
	gmp_ride_cymbal_1,
	gmp_Chinese_cymbal,
	gmp_ride_bell,
	gmp_tambourine,
	gmp_splash_cymbal,
	gmp_cow_bell,
	gmp_crash_cymbal_2,
	gmp_vibra_slap,
	gmp_ride_cymbal_2,
	gmp_high_bongo,			//_61-1		BM:last
	//
	gmp_low_bongo,
	gmp_mute_high_conga,
	gmp_open_high_conga,
	gmp_low_conga,
	gmp_high_timbale,
	gmp_low_timbale,
	gmp_high_agogo,
	gmp_low_agogo,
	gmp_cabasa,
	gmp_maracas,
	gmp_short_high_whistle_exc2,
	gmp_long_low_whistle_exc2,
	gmp_short_guiro_exc3,
	gmp_long_guiro_exc3,
	gmp_claves,
	gmp_high_wood_block,
	gmp_low_wood_block,
	gmp_mute_cuica_exe4,
	gmp_open_cuica_exc4,
	gmp_mute_triangle_exc5,
	gmp_open_triangle_exc5,
	//
	gmp_shaker,
	gmp_jingle_bell,
	gmp_belltree,
	gmp_castanets,
	gmp_mute_surto_exc6,
	gmp_open_surto_exc6,

	gmp_NUM_

} gmpenum_t;


//
// Identifiers for all sfx in game.
//

typedef enum
{
    YMI_None,

	// SFX begin here....

	// instruments begin here...

    YMI_Piano,
    YMI_Piano2,
	//
    YMI_BassBuzzTri,
    YMI_BassBuzzSaw,
    YMI_BassPhaseTri,
    YMI_BassPhaseSaw,
    YMI_FretPhaseTri,
    YMI_FretPhaseSaw,
	//
    YMI_Harps,
    YMI_Strings,

	// MIDI percussion begins here...

	// these are not present, but within mapping window
		mperc_otria,
		mperc_mtria,
		//
		mperc_ocuic,
		mperc_mcuic,
		mperc_lwood,
		mperc_hwood,
		mperc_clave,
		mperc_lguir,
		mperc_sguir,
		mperc_lwhis,
		mperc_swhis,
		mperc_marac,
		mperc_cabas,
		mperc_loago,
		mperc_hiago,
		mperc_lotim,
		mperc_hitim,
		mperc_locon,
		mperc_ohico,
		mperc_mhico,
		mperc_lobon,
	// mapping continues
	mperc_hibon,
	mperc_rcym2,
	mperc_vib,
	mperc_ccym2,
	mperc_cbell,
	mperc_spcym,
	mperc_tambo,
	mperc_rbell,
	mperc_chcym,
	mperc_rcym1,
	mperc_hitom,
	mperc_ccym1,
	mperc_hmtom,
	mperc_lmtom,
	mperc_ohhat,
	mperc_lotom,
	mperc_phhat,
	mperc_hftom,
	mperc_chhat,
	mperc_lftom,
	mperc_esnar,
	mperc_clap,
	mperc_asnar,
	mperc_sstik,
	mperc_bass1,
	mperc_bass2,

	YMI_NUM
			
} sfxenum_t;

#define minst_first_ (YMI_Piano)
#define minst_last_ (mperc_otria-1)

#define mperc_first_ (mperc_otria)
#define mperc_last_ (mperc_bass2)

#define midi_mperc_start_ (gmp_kick_drum_2)	// Bass drum 2
#define midi_mperc_end_ (gmp_shaker)		// Shaker (first not present)

//-----------------------------------------------------------------------------

#pragma GCC diagnostic pop

//-----------------------------------------------------------------------------

#endif // sounds_h_

