//---------------------------------------------------------------------------------------------------------------------
//	[A]tari [G]ame [T]ools / dml 2016
//---------------------------------------------------------------------------------------------------------------------
//	AGTCut - graphics cutting tool
//---------------------------------------------------------------------------------------------------------------------

// --------------------------------------------------------------------
//	platform stuff - todo: OSX/Linux porting!
// --------------------------------------------------------------------

#define PREFER_TRANSFORMS (1) // hacks for testing
#define USE_MOVEQ_INITS (0) // hacks for testing
#define USE_EMX2
//#define USE_EMS2 // WIP, not working

#if defined(_MSC_VER)
//#error **** ALL TOOL BUILDS SHOULD NOW BE MADE WITH MINGW NOT VC++ ****
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdint.h>
#endif

#include <stdlib.h>
#include <stdio.h>

#include <string>
#include <iostream>
#include <sstream>
#include <fstream>

// --------------------------------------------------------------------
//	STL
// --------------------------------------------------------------------

#include <list>
#include <vector>
#include <map>

// --------------------------------------------------------------------
//	stb_image! nice :)
// --------------------------------------------------------------------

#define STB_IMAGE_IMPLEMENTATION
#include "../3rdparty/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../3rdparty/stb_image_write.h"
#include "../3rdparty/ThreadPool.h"

#include "../common/common.h"
#include "../common/bendian.h"
#include "../common/p42n.h"
#include "../common/rnd.h"
#include "../common/csvreader.h"
#include "../common/exec.h"
#include "../common/tga.h"

#include "../../agtsys/tables/tiny6x6fnt.c"

// --------------------------------------------------------------------
//	constants
// --------------------------------------------------------------------

// tool version
static const char* const g_version_string = "v0.475b";

// SPS file format version
static const int SPS_VERSION = 0x0207;

static const int SLS_VERSION = 0x1202;

#if defined(_MSC_VER)
static const char* const c_rmac_filename = "rmac.exe";
#else
static const char* const c_rmac_filename = "rmac";
#endif

// this allowed EMH to be y-clipped providing the lineorder is preserved - but results in bad performance and excess storage costs.
// disabling this makes EMH useful versus both EMS and EMX foramts so long as GUARDY is used for y-clipping cases
static const int USE_EMH_CLIPTABLE = 0;

//static const int USE_LINEDUPE_OPTIMIZATION = 1;
static const int USE_DELTA_OPTIMIZATION = 1;

// single/common optimized line ordering shared by all preshifts of a given frame
static const int USE_COMMON_LINEORDER_OPTIMIZATION = 1;

// USE_EMPTYLINE_OPTIMIZATION can *only* work properly with USE_COMMON_LINEORDER_OPTIMIZATION for sprites which can be clipped,
// since there's no code provision for skipping of source data lines and all lines get emitted for indexed EMS clipping case.
// the USE_COMMON_LINEORDER_OPTIMIZATION phase will move any empty lines to the end of the render to avert the need to skip them.
static const int USE_EMPTYLINE_OPTIMIZATION = 1;

// organize sprite frame lines by edge coherence before optimizing
static const int USE_INITIAL_EDGE_SORT = 1;

// forces dstlinewid (dest skip) into a free register depending on sprite width, otherwise registers are used for EM ptrs
// this will be inefficient if most common dest skip is not a single scanline e.g. hatched masks!
static const int USE_DSTSKIP_REG_FIXED_PRELOAD = 0;

// dynamically allocate/steal registers to map the most frequently used dest skip(s)
static const int USE_DSTSKIP_REG_OPTIMIZATION = 1;
// fill spare EM registers (a4, a6) with popular constants
static const int USE_DSTSKIP_REG_FIXED_PRELOAD_INCIDENCE = 1;

// this only makes sense if blitter is started with RMW op - which is currently not the case, is started with move.w d1,(an)
static const int ALLOW_BTAIL_OPTIMIZATIONS = 0;

#define USE_NOISY_REORDERING (0)

// per-preshift line ordering - implies per-preshift colourdata ordering as well (too much bloat)
//static const int USE_PERSHIFT_LINEORDER_OPTIMIZATION = 0;

//static const int DARWIN_DEPTH = 20;
static const int DEFAULT_EMX_OPTLEVEL = 1;

#define USE_NOP_BINARY_VALIDATION (0)

#define BMP_TYPICAL_ROW_ORDER (1)		// 0 = -ve height, +ve ordering (easier to parse back in)
										// 1 = +ve height, -ve ordering (more common)

// --------------------------------------------------------------------

class hasher
{
public:

	template<class T>
	static u32 hash(T *source, size_t count)
	{
		static const u32 c_FNVPrime = 16777619;
		static const u32 c_FNVOBase = 2166136261;

		u32 hash = c_FNVOBase;

		for (size_t p = 0; p < count; ++p)
		{
			u32 data = *source++;
			hash ^= data;
			hash *= c_FNVPrime;
			hash = (hash >> 7) | (hash << (32 - 7));
		}

		return hash;
	}
};

// --------------------------------------------------------------------
//	local types
// --------------------------------------------------------------------

typedef struct spritemapping_s
{
	int to;
	int from;
	std::string flags;

	int x;
	int y;

} spritemapping_t;

typedef std::map<int, spritemapping_t> spritemap_t;

static const u8 c_tile_mask_keycol = 128;
static const u8 c_default_keycol = 255;

// --------------------------------------------------------------------
//	some ...globals (?!)
// --------------------------------------------------------------------

static int s_progress = 0;
static int g_maxplanes = 4;

static ubyte_t *g_colrbuffer8 = 0;
static uword_t *g_colrbufferbpl = 0;

static ubyte_t *g_colrbuffer80 = 0;
static uword_t *g_colrbufferbpl0 = 0;
static ubyte_t *g_colrbuffer81 = 0;
static uword_t *g_colrbufferbpl1 = 0;

static ubyte_t *g_maskbuffer8 = 0;
static uword_t *g_maskbufferbpl = 0;

static ubyte_t *g_workbuffer8 = 0;
static uword_t *g_workbufferbpl = 0;

// --------------------------------------------------------------------
// SimpleOpt... because life is too short
// --------------------------------------------------------------------

#if defined(_MSC_VER)
# include <windows.h>
# include <tchar.h>
#else
# define TCHAR		char		// yuck
# define _T(x)		x
#endif

#include "../3rdparty/SimpleOpt.h"
#if defined(_MSC_VER)
#include "../3rdparty/SimpleGlob.h"
#endif

// --------------------------------------------------------------------
//	misc. small helper functions
// --------------------------------------------------------------------


static void plot6x6char(RGB *_buf, int _bufw, int _c, int _x, int _y)
{
	u8 *fnt = &tiny6x6fnt[_c * 6];
	RGB *out = &_buf[(_y * _bufw) + _x];

	for (int l = 0; l < 6; l++)
	{
		u8 b = *fnt++;

		for (int p = 0; p < 6; p++)
		{
			s8 v = (b & 0x80); v >>= 7;
			b <<= 1;
			out[p].red		|= v;
			out[p].green	|= v;
			out[p].blue		|= v;
		}

		out += _bufw;
	}
}

static void plot6x6text(RGB *_buf, int _bufw, char *_str, int _x, int _y)
{
	int c;
	while (0 != (c = *_str++))
	{
		plot6x6char(_buf, _bufw, c, _x, _y);
		_x += 5;
	}
}

static void plot6x6hex3(RGB *_buf, int _bufw, int _v, int _x, int _y)
{
	char numbuf[4] = { 0, 0, 0 };
	int n;

	for (int i = 0; i < 3; i++)
	{
		if (_v == 0)
		{
			n = ' ';
		}
		else
		{
			n = (_v & 0xF); _v >>= 4;

			if (n < 10)
				n += '0';
			else
				n += 'A' - 10;
		}

		numbuf[2-i] = n;
	}

	plot6x6text(_buf, _bufw, numbuf, _x, _y);
}

// --------------------------------------------------------------------
//	interface to engine tileset reader, for access to palettes
// --------------------------------------------------------------------

// todo: move reader/writer out of tool generally!
//#include "tileset.h"

// --------------------------------------------------------------------
//	commandline handling
// --------------------------------------------------------------------

// define the ID values to indentify the option
enum
{ 
	OPT_HELP, 
	OPT_VERBOSE, 
	OPT_QUIET, 
	OPT_DEBUG,
	OPT_MPLIST,
	OPT_WAIT_KEY, 
	// general
	OPT_BITPLANES,
	OPT_KEYCOL,
	OPT_KEYRGB,
	OPT_KEYGUARD,
	OPT_KEYTOL,
	OPT_ALPHARGB,
	OPT_ALPHATOL,
	OPT_DFALPHA,
//	OPT_AUTOCUTCOL,
	OPT_AUTOCUTRGB,
//	OPT_HITCOL,
	OPT_HITRGB,
	OPT_SOURCE,
	OPT_OUTFILE,
	OPT_RMACPATH,
	OPT_BUILDPATH,
	OPT_TILETAGS,
	OPT_PALETTE,
	OPT_COLRENORM,
	OPT_CUTMODE,
	OPT_CCFIELDS,
	OPT_CCREMAP,
	OPT_NOREMAP,
	OPT_FORCEREMAP,
	// sprites
	OPT_EMXOPT,
	OPT_EMX_NO_NFSR,
	OPT_EMC,
	OPT_OCM,
	OPT_NOCLIP,
	OPT_NOCLIPX,
	OPT_NOCLIPY,
	OPT_EMGUARDX,
	OPT_SPRITEGUIDE,
	OPT_QUICKCUT,
	OPT_VIS,
	OPT_HIDDEN,
	OPT_PREVIEW,
	OPT_GENMASK,
	OPT_NOGENMASK,
	OPT_MASKLAYOUT,
	OPT_PRESHIFTSTEP,
	OPT_SPROPT,
	OPT_SPRXP,
	OPT_SPRYP,
	OPT_SPRXS,
	OPT_SPRYS,
	OPT_SPRXI,
	OPT_SPRYI,
	OPT_SPRXC,
	OPT_SPRYC,
	OPT_SPRCOUNT,
	OPT_SPRMAP,
//	OPT_SLAB,
	// tiles
	OPT_STITCHMAPS,
	OPT_MAPRECT,
	OPT_TILESIZE,
	OPT_TOLERANCE,
	OPT_TRANSLAYER,
	OPT_OUTMODE
	//
};

CSimpleOpt::SOption g_Options[] = 
{
	{ OPT_HELP, 		_T("-?"),     			SO_NONE		},
	{ OPT_HELP, 		_T("-h"),     			SO_NONE		},
	{ OPT_HELP, 		_T("--help"), 			SO_NONE		},
	//
	{ OPT_VERBOSE, 		_T("-v"),     			SO_NONE		},
	{ OPT_VERBOSE,		_T("--verbose"), 		SO_NONE		},
	//
	{ OPT_QUIET, 		_T("-q"),     			SO_NONE		},
	{ OPT_QUIET,		_T("--quiet"), 			SO_NONE		},
	//
	{ OPT_DEBUG, 		_T("-d"),     			SO_NONE		},
	{ OPT_DEBUG,		_T("--debug"), 			SO_NONE		},
	//
	{ OPT_MPLIST, 		_T("-mpl"),     		SO_NONE		},
	{ OPT_MPLIST,		_T("--mproglist"), 		SO_NONE		},
	//
	{ OPT_WAIT_KEY,		_T("-wk"),    			SO_NONE		},
	{ OPT_WAIT_KEY,		_T("--wait-key"), 		SO_NONE		},
	//
	{ OPT_PREVIEW,		_T("-prv"),     		SO_NONE		},
	{ OPT_PREVIEW,		_T("--preview"), 		SO_NONE		},
	//
	{ OPT_GENMASK,		_T("-gm"),	 			SO_NONE		},
	{ OPT_GENMASK,		_T("--gen-mask"),		SO_NONE		},
	//
	{ OPT_NOGENMASK,	_T("-nogm"),	 		SO_NONE		},
	{ OPT_NOGENMASK,	_T("--no-gen-mask"),	SO_NONE		},
	//
	{ OPT_MASKLAYOUT,	_T("-ml"),	 			SO_REQ_SEP	},
	{ OPT_MASKLAYOUT,	_T("--mask-layout"),	SO_REQ_SEP	},
	//
	{ OPT_PRESHIFTSTEP,	_T("-pss"),	 			SO_REQ_SEP	},
	{ OPT_PRESHIFTSTEP,	_T("--preshift-step"),	SO_REQ_SEP	},
	//
	{ OPT_EMXOPT,		_T("-emxopt"),	 		SO_REQ_SEP	},
	{ OPT_EMXOPT,		_T("--emx-opt"),		SO_REQ_SEP	},
	//
	{ OPT_EMX_NO_NFSR,	_T("-emxnon"), 			SO_NONE		},
	{ OPT_EMX_NO_NFSR,	_T("--emx-no-nfsr"),	SO_NONE		},
	//
	{ OPT_EMGUARDX,		_T("-emgx"),			SO_REQ_SEP	},
	{ OPT_EMGUARDX,		_T("--emguardx"),		SO_REQ_SEP	},
	//
	{ OPT_EMC,			_T("-emc"),	 			SO_NONE		},
	{ OPT_EMC,			_T("--emc"),			SO_NONE		},
	//
	{ OPT_OCM,			_T("-ocm"),	 			SO_NONE		},
	{ OPT_OCM,			_T("--occlusion-map"),	SO_NONE		},
	//
	{ OPT_NOCLIPX,		_T("-ncx"),	 			SO_NONE		},
	{ OPT_NOCLIPX,		_T("--no-clipx"),		SO_NONE		},
	//
	{ OPT_NOCLIPY,		_T("-ncy"),	 			SO_NONE		},
	{ OPT_NOCLIPY,		_T("--no-clipy"),		SO_NONE		},
	//
	{ OPT_NOCLIP,		_T("-nc"),	 			SO_NONE		},
	{ OPT_NOCLIP,		_T("--no-clip"),		SO_NONE		},

	//
	{ OPT_KEYGUARD,		_T("-kg"),	 			SO_NONE		},
	{ OPT_KEYGUARD,		_T("--key-guard"),		SO_NONE		},
	//
	{ OPT_KEYTOL,		_T("-kt"),	 			SO_REQ_SEP	},
	{ OPT_KEYTOL,		_T("--key-tol"),		SO_REQ_SEP	},

	//
	{ OPT_KEYCOL,		_T("-key"),	 			SO_REQ_SEP	},
	{ OPT_KEYCOL,		_T("--key"),			SO_REQ_SEP	},
	//
	{ OPT_KEYRGB,		_T("-keyrgb"), 			SO_REQ_SEP	},
	{ OPT_KEYRGB,		_T("--key-rgb"),		SO_REQ_SEP	},

	//
	{ OPT_ALPHARGB,		_T("-alphargb"), 		SO_REQ_SEP	},
	{ OPT_ALPHARGB,		_T("--alpha-rgb"),		SO_REQ_SEP	},
	//
	{ OPT_ALPHATOL,		_T("-at"),	 			SO_REQ_SEP	},
	{ OPT_ALPHATOL,		_T("--alpha-tol"),		SO_REQ_SEP	},
	//
	{ OPT_DFALPHA,		_T("-dfa"),	 			SO_NONE		},
	{ OPT_DFALPHA,		_T("--df-alpha"),		SO_NONE		},

	//
//	{ OPT_AUTOCUTCOL,	_T("-ackey"),	 		SO_REQ_SEP	},
//	{ OPT_AUTOCUTCOL,	_T("--ackey"),			SO_REQ_SEP	},
	//
	{ OPT_AUTOCUTRGB,	_T("-ackeyrgb"),	 	SO_REQ_SEP	},
	{ OPT_AUTOCUTRGB,	_T("--ackey-rgb"),		SO_REQ_SEP	},
	
	//
//	{ OPT_HITCOL,		_T("-hitkey"),	 		SO_REQ_SEP	},
//	{ OPT_HITCOL,		_T("--hitkey"),			SO_REQ_SEP	},
	//
	{ OPT_HITRGB,		_T("-hitkeyrgb"), 		SO_REQ_SEP	},
	{ OPT_HITRGB,		_T("--hitkey-rgb"),		SO_REQ_SEP	},

	//
	{ OPT_SOURCE,		_T("-s"),	 			SO_REQ_SEP	},
	{ OPT_SOURCE,		_T("--src"), 			SO_REQ_SEP	},
	//
	{ OPT_CUTMODE,		_T("-cm"),	 			SO_REQ_SEP	},
	{ OPT_CUTMODE,		_T("--cutmode"), 		SO_REQ_SEP	},
	//
	{ OPT_CCFIELDS,		_T("-ccf"),	 			SO_REQ_SEP	},
	{ OPT_CCFIELDS,		_T("--ccfields"), 		SO_REQ_SEP	},
	//
	{ OPT_CCREMAP,		_T("-ccr"),	 			SO_REQ_SEP	},
	{ OPT_CCREMAP,		_T("-cc-remap"),	 	SO_REQ_SEP	},
	//
	{ OPT_NOREMAP,		_T("-nr"),	 			SO_NONE		},
	{ OPT_NOREMAP,		_T("--no-remap"),		SO_NONE		},
	//
	{ OPT_FORCEREMAP,	_T("-fr"),	 			SO_NONE		},
	{ OPT_FORCEREMAP,	_T("--force-remap"),	SO_NONE		},
	//
	{ OPT_SPRITEGUIDE,	_T("-sg"),	 			SO_REQ_SEP	},
	{ OPT_SPRITEGUIDE,	_T("--sprguide"), 		SO_REQ_SEP	},
	//
	{ OPT_MAPRECT,		_T("-mr"),	 			SO_REQ_SEP	},
	{ OPT_MAPRECT,		_T("--maprect"), 		SO_REQ_SEP	},
	//
	{ OPT_QUICKCUT,		_T("-qc"),	 			SO_NONE	},
	{ OPT_QUICKCUT,		_T("--quickcut"), 		SO_NONE	},
	//
	{ OPT_VIS,			_T("-vis"),	 			SO_NONE	},
	{ OPT_VIS,			_T("--visual"), 		SO_NONE	},
	//
	{ OPT_SPROPT,		_T("-sopt"), 			SO_REQ_SEP	},
	{ OPT_SPROPT,		_T("--soptimize"),		SO_REQ_SEP	},
	{ OPT_SPROPT,		_T("--soptimise"),		SO_REQ_SEP	},
	//
	{ OPT_HIDDEN,		_T("-hidden"),		 	SO_NONE	},
	{ OPT_HIDDEN,		_T("--hidden"), 		SO_NONE	},
	//
	{ OPT_SPRXP,		_T("-sxp"),	 			SO_REQ_SEP	},
	{ OPT_SPRXP,		_T("--sprxpos"),		SO_REQ_SEP	},
	//
	{ OPT_SPRYP,		_T("-syp"),	 			SO_REQ_SEP	},
	{ OPT_SPRYP,		_T("--sprypos"),		SO_REQ_SEP	},
	//
	{ OPT_SPRXS,		_T("-sxs"),	 			SO_REQ_SEP	},
	{ OPT_SPRXS,		_T("--sprxsize"),		SO_REQ_SEP	},
	//
	{ OPT_SPRYS,		_T("-sys"),	 			SO_REQ_SEP	},
	{ OPT_SPRYS,		_T("--sprysize"),		SO_REQ_SEP	},
	//
	{ OPT_SPRXI,		_T("-sxi"),	 			SO_REQ_SEP	},
	{ OPT_SPRXI,		_T("--sprxinc"),		SO_REQ_SEP	},
	//
	{ OPT_SPRYI,		_T("-syi"),	 			SO_REQ_SEP	},
	{ OPT_SPRYI,		_T("--spryinc"),		SO_REQ_SEP	},
	//
	{ OPT_SPRXC,		_T("-sxc"),	 			SO_REQ_SEP	},
	{ OPT_SPRXC,		_T("--sprxcount"),		SO_REQ_SEP	},
	//
	{ OPT_SPRYC,		_T("-syc"),	 			SO_REQ_SEP	},
	{ OPT_SPRYC,		_T("--sprycount"),		SO_REQ_SEP	},
	//
	{ OPT_SPRCOUNT,		_T("-sc"),	 			SO_REQ_SEP	},
	{ OPT_SPRCOUNT,		_T("--sprcount"),		SO_REQ_SEP	},
	//
	{ OPT_SPRMAP,		_T("-sm"),	 			SO_REQ_SEP	},
	{ OPT_SPRMAP,		_T("--sprmap"),			SO_REQ_SEP	},
	//
	//{ OPT_SLAB,			_T("-sl"),	 			SO_NONE		},
	//{ OPT_SLAB,			_T("--slab"),			SO_NONE		},
	//
	{ OPT_STITCHMAPS,	_T("-stm"),	 			SO_NONE	},
	{ OPT_STITCHMAPS,	_T("--stitch-maps"),	SO_NONE	},
	//
	{ OPT_TILESIZE,		_T("-ts"),	 			SO_REQ_SEP	},
	{ OPT_TILESIZE,		_T("--tilesize"),		SO_REQ_SEP	},
	//
	{ OPT_TOLERANCE,	_T("-t"),	 			SO_REQ_SEP	},
	{ OPT_TOLERANCE,	_T("--tol"), 			SO_REQ_SEP	},
	//
	{ OPT_TRANSLAYER,	_T("-layer"),			SO_NONE		},
	{ OPT_TRANSLAYER,	_T("--layer"),			SO_NONE		},
	//
	{ OPT_OUTMODE,		_T("-om"),	 			SO_REQ_SEP	},
	{ OPT_OUTMODE,		_T("--outmode"), 		SO_REQ_SEP	},
	//
	{ OPT_OUTFILE,		_T("-o"),	 			SO_REQ_SEP	},
	{ OPT_OUTFILE,		_T("--outfile"),		SO_REQ_SEP	},
	//
	{ OPT_RMACPATH,		_T("-rmac"), 			SO_REQ_SEP	},
	{ OPT_RMACPATH,		_T("--rmac"),			SO_REQ_SEP	},
	//
	{ OPT_BUILDPATH,	_T("-bld"), 			SO_REQ_SEP	},
	{ OPT_BUILDPATH,	_T("--build"),			SO_REQ_SEP	},
	//
	{ OPT_TILETAGS,		_T("-ttags"), 			SO_REQ_SEP	},
	{ OPT_TILETAGS,		_T("--tiletags"),		SO_REQ_SEP	},
	//
	{ OPT_PALETTE,		_T("-p"),	 			SO_REQ_SEP	},
	{ OPT_PALETTE,		_T("--pal"),	 		SO_REQ_SEP	},
	//
	{ OPT_COLRENORM,	_T("-crn"),	 			SO_REQ_SEP	},
	{ OPT_COLRENORM,	_T("--colrenorm"), 		SO_REQ_SEP	},
	//
	{ OPT_BITPLANES,	_T("-bp"),	 			SO_REQ_SEP	},
	{ OPT_BITPLANES,	_T("--bitplanes"), 		SO_REQ_SEP	},
	//
	SO_END_OF_OPTIONS
};

static void usage(int level)
{
	if (level >= 1) 
	{
		//std::cout << g_version_string << std::endl;
		std::cout << "built on " << __DATE__ << std::endl;
	}
		
	std::cout << "usage:" << std::endl;
	std::cout << " atgcut [opts] -s infile" << std::endl;
	std::cout << std::endl;
	std::cout << " -?, -h, --help           > show help/usage" << std::endl;
	std::cout << " -v, --verbose            > more output" << std::endl;
	std::cout << " -q, --quiet              > less output" << std::endl;
	std::cout << " -d, --debug              > all output, includes EMX/EMH metaprogram gen & listing" << std::endl;
	std::cout << " -mpl, --mproglist        > emit EMX/EMH metaprogram listing" << std::endl;
	std::cout << " -wk, --wait-key          > wait key on exit" << std::endl;
	std::cout << std::endl;
	std::cout << "general:" << std::endl;
	std::cout << " -s, --src                > source image containing tiles/sprites/slabs:" << std::endl;
	std::cout << "                          > pre-mapped: png,bmp (i.e. use indexed 4/8bit colour w/no remapping)" << std::endl;
	std::cout << "                          > unmapped:   png,bmp,jpg,psd,tga,gif,hdr,pic,ppm/pgm/pnm" << std::endl;
	std::cout << " -o, --outfile            > output filename" << std::endl;
	std::cout << " -cm, --cutmode           > cutting mode: tiles,imspr,emspr,emxspr,slabs,slabrestore" << std::endl;
	std::cout << " -ccf, --ccfields         > cryptochrome fields: 1 or 2 (for dual-field gfx)" << std::endl;
	std::cout << " -p, --pal                > fixed palette to use: pi1,pal(riff),bmp,png" << std::endl;
	std::cout << " -crn, --colrenorm        > renormalise quantised source RGB to 8bit RGB: 1-255" << std::endl;
	std::cout << " -bp, --bitplanes         > #bitplanes to generate: 1-8" << std::endl;
	std::cout << " -key, --key              > colour index assumed to be transarent: 0-255" << std::endl;
	std::cout << " -keyrgb, --key-rgb       > rgb value assumed to be transarent: <RR:GG:BB> in hex" << std::endl;
	std::cout << " -kg, --key-guard         > guard against reducing colours to key colour" << std::endl;
	std::cout << " -kt, --key-tol           > key guard tolerance: 0.0+ (default: 0.01)" << std::endl;
	std::cout << " -ccr, --cc-remap         > cryptochrome colour field remapping: both,high,low,match" << std::endl;
	std::cout << " -nr, --no-remap          > prevent remapping of 8bit images (to 4bit), use first 16 colours only" << std::endl;
	std::cout << " -fr, --force-remap       > force remapping of 8bit images (to 4bit) so --key-rgb is usable" << std::endl;
	std::cout << " -vis, --visual           > provide feedback/visualisation of cut assets as .tga file" << std::endl;
	std::cout << std::endl;
	std::cout << "sprite cutting mode:" << std::endl;
	std::cout << " -sxp, --sprxpos          > first frame xpos: 0+" << std::endl;
	std::cout << " -syp, --sprypos          > first frame ypos: 0+" << std::endl;
	std::cout << " -sxs, --sprxsize         > all frames xsize: 1+" << std::endl;
	std::cout << " -sys, --sprysize         > all frames ysize: 1+" << std::endl;
	std::cout << " -sxi, --sprxinc          > x step for next frame in row: 0+" << std::endl;
	std::cout << " -syi, --spryinc          > y step for next row: 0+" << std::endl;
	std::cout << " -sxc, --sprxcount        > frames per row: 1+" << std::endl;
	std::cout << " -syc, --sprycount        > frames per column: 1+" << std::endl;
	std::cout << " -sc, --sprcount          > total frames to cut, if stopping early: 0+, where 0=ignore" << std::endl;
	std::cout << " -sm, --sprmap            > map sprite frame from another: -sm new:existing:flipflags" << std::endl;
	std::cout << " -gm, --gen-mask          > generate 1bpl mask data along with colour data" << std::endl;
	std::cout << " -nogm, --no-gen-mask     > don't generate 1bpl mask data - only colour data (STF)" << std::endl;
	std::cout << " -ml, --mask-layout       > mask layout strategy: interleaved,planar,preshift" << std::endl;
	std::cout << " -sopt, --soptimize       > optimize away unused pixels: on/off (compensated w/offsets)" << std::endl;
	std::cout << " -qc, --quickcut          > improve cutting speed (reduce optimization) for quicker test/debug turnaround" << std::endl;
	std::cout << " -prv, --preview          > show ASCII preview of extracted colours + masking" << std::endl;
	std::cout << " -sg, --sprguide          > sprite guide file (ASCII)" << std::endl;
	std::cout << " -hidden, --hidden        > hidden resource (e.g. collision/undraw) - don't store any pixel data" << std::endl;
	std::cout << " -alphargb, --alpha-rgb   > rgb value for 50% transparency: <RR:GG:BB> in hex" << std::endl;
	std::cout << " -at, --alpha-tol         > rgb tolerance for 50% transparency: 0.0+" << std::endl;
	std::cout << " -ackeyrgb, --ackey-rgb   > rgb value for 100% transparency: <RR:GG:BB> in hex" << std::endl;
	std::cout << " -hitkeyrgb, --hitkey-rgb > rgb value used to express hidden hitboxes: <RR:GG:BB> in hex" << std::endl;
	std::cout << " -emxopt, --emx-opt       > optimization level for EMX mask delta elimination" << std::endl;
	std::cout << " -emgx, --emguardx        > configure x-guardband width for EMX sprites (default:32)" << std::endl;
	std::cout << " -emc, --emc              > allow compound (large >32 wide) EMS/EMX sprites to be cut" << std::endl;
	std::cout << " -nc, --no-clip           > indicate sprite do *not* require clipping (on y axis), reduces EMX storage size" << std::endl;
	std::cout << std::endl;
	std::cout << "map & tile cutting mode:" << std::endl;
	std::cout << " -ts, --tilesize          > tile size: 8,16,32" << std::endl;
	std::cout << " -ttags, --tiletags       > list of tile indices to tag (CSV file - one hex value per row)" << std::endl;
	std::cout << " -t, --tol                > tile matching tolerance: 0.0+" << std::endl;
	std::cout << " -mr, --maprect           > map rectangle to extract (-mr x:y:w:h ..or.. -mr x:y)" << std::endl;
	std::cout << " -layer, --layer          > cut map as transparent layer" << std::endl;
	std::cout << " -stm, --stitch-maps      > import multiple, numbered source images, stack vertically" << std::endl;
	std::cout << " -om, --outmode           > tile library output mode: direct,degas,rbplus" << std::endl;
	std::cout << std::endl;
	std::cout << "press any key..." << std::endl << std::flush;
		
	getchar();	
	exit(1);
}

// --------------------------------------------------------------------

void update_file_crc(FILE* hout, size_t crc_offset)
{
	u16 wv;
	u32 lv;

	// reset file position
	fseek(hout, crc_offset+4, SEEK_SET);

	// perform hash
	// note: 32bit CRC position (and anything before it) will not be included
	u32 CRC = 0;
	size_t words = 0;
	{
		static const u32 c_FNVPrime = 16777619;
		static const u32 c_FNVOBase = 2166136261;

		u32 hash = c_FNVOBase;

		size_t read = 0;
		do
		{
			// words were formatted BE
			read = fread(&wv, 1, 2, hout);

			if (read == 2)
			{
				// must be restored to native LE for hashing
				u16 data = endianswap16(wv);

				hash ^= data;
				hash *= c_FNVPrime;

				hash = (hash >> 7) | (hash << (32-7));

				words++;
			}

		} while (read == 2);

		CRC = hash;
	}

	printf("CRC=%08x w=%d\n", CRC, words);

	fseek(hout, crc_offset, SEEK_SET);
	// convert to BE
	lv = endianswap32(CRC);
	fwrite(&lv, 1, 4, hout);
}

// --------------------------------------------------------------------
//	general argument handling
// --------------------------------------------------------------------


enum CCRemap
{
	CCRemap_BOTH,				// high/low field dither, alternating per field
	CCRemap_HIGH,				// high field first (low field second, if ccf=2)
	CCRemap_LOW,				// low field first (high field second, if ccf=2)
	CCRemap_MATCH,				// closest match available (same for all fields)
};

enum Outmode
{
	Outmode_DIRECT,
	Outmode_DEGAS,
	Outmode_RBPLUS
};

enum Cutmode
{
	Cutmode_TILES,
	Cutmode_SPRITES,			// standard sprites (software w/o mask, or blitter w/ mask)
	Cutmode_EMSPR,				// EM blitter sprites (preshifted maskdata)
	Cutmode_EMXSPR,				// EM eXtreme blitter sprites (preshifted codegen, immediate sparse maskdata)
	Cutmode_EMHSPR,				// EM hybrid blitter sprites (preshift-shared codegen, preshifted sparse maskdata)
	Cutmode_SLABS,
	Cutmode_SLABRESTORE,
};

enum Masklayout
{
	Masklayout_INTERLEAVED,
	Masklayout_PLANAR,
	Masklayout_PRESHIFT,
	Masklayout_IMPLIED,
};

// sprite guide cutting event, extracted from .sgd text file
class guideevent
{
public:

	guideevent(int _count, std::string _flags, int _xp, int _yp, int _xs, int _ys, int _xi, int _yi)
		: count(_count), flags(_flags), xp(_xp), yp(_yp), xs(_xs), ys(_ys), xi(_xi), yi(_yi)
	{
	}

	int count;
	std::string flags;
	int xp, yp;
	int xs, ys;
	int xi, yi;
};

class Arguments
{
public:

	Arguments()
		: verbose(false),
		  quiet(false),
		  debug(false),
		  mplist(false),
		  waitkey(false),
		  spriteoptimize(true),
		  preview(false),
		  genmask(true),
		  keyguard(false),
		  quickcut(false),
		  visual(false),
		  hidden(false),
		  keyrgb_specified(false),
		  alphargb_specified(false),
		  hitkeyrgb_specified(false),
		  autocutkeyrgb_specified(false),
		  dualfield_alpha(false),
		  colrenorm(255),
		  tolerance(0.0f),
		  keytol(0.01f),
		  //sourcefile(""),
		  //outfile(""),
		  //tiletags(""),
		  palettefile("palette.pi1"),
		  //spriteguidefile(""),
		  ccfields(1),
		  ccremap(CCRemap_BOTH),
		  masklayout(Masklayout_INTERLEAVED),
		  cutmode(Cutmode_TILES),
		  outmode(Outmode_DEGAS),
		  stitchmaps(false),
		  translayer(false),
		  noremap(false),
		  forceremap(false),
		  masklayout_lock(false),
		  compound_slabs(true),
		  planes(4),
		  tilesize(16),
		  emxoptlevel(DEFAULT_EMX_OPTLEVEL),
		  emxopttrials(4),
		  emguardx(32),
		  preshift_step(1),
		  preshift_scale(0),
		  emc(false),
		  ocm(false),
		  noclipx(false),
		  noclipy(false),
		  dualfield(false),
		  no_nfsr(false),
		  keycol(c_default_keycol),
		  autocutkeycol(256),
		  hitkeycol(256),
		  cut_xpos(0),
		  cut_ypos(0),
		  cut_xsize(16),
		  cut_ysize(16),
		  cut_xinc(0),
		  cut_yinc(0),
		  cut_xcount(0),
		  cut_ycount(0),
		  cut_count(0),
		  maprect_xpos(0),
		  maprect_ypos(0),
		  maprect_xsize(0x7fffffff),
		  maprect_ysize(0x7fffffff),
		  //
		  use_solutionpair_reduction_(false),
		  distributed_dualfield_(false)
	{
		keyrgb.red = 255;
		keyrgb.green = 0;
		keyrgb.blue = 255;
		autocutkeyrgb.red = 0;
		autocutkeyrgb.green = 255;
		autocutkeyrgb.blue = 0;
		hitkeyrgb.red = 0;
		hitkeyrgb.green = 255;
		hitkeyrgb.blue = 255;
		alphargb.red = 0;
		alphargb.green = 0;
		alphargb.blue = 0;
	}

	bool verbose;
	bool quiet;
	bool debug;
	bool mplist;
	bool waitkey;
	bool spriteoptimize;
	bool preview;
	bool genmask;
	bool keyguard;
	bool quickcut;
	bool visual;
	bool hidden;
	bool keyrgb_specified;
	bool alphargb_specified;
	bool hitkeyrgb_specified;
	bool autocutkeyrgb_specified;
	bool dualfield_alpha;
	bool translayer;
	bool stitchmaps;
	bool compound_slabs;
	bool emc;
	bool ocm;
	bool noclipx;
	bool noclipy;
	bool dualfield;
	bool no_nfsr;

	ubyte_t keycol;
	iRGB keyrgb;
	uword_t hitkeycol;
	iRGB hitkeyrgb;
	uword_t autocutkeycol;
	iRGB autocutkeyrgb;
	iRGB alphargb;
	int colrenorm;
	int cut_xpos;
	int cut_ypos;
	int cut_xsize;
	int cut_ysize;
	int cut_xinc;
	int cut_yinc;
	int cut_xcount;
	int cut_ycount;
	int cut_count;
	int maprect_xpos;
	int maprect_ypos;
	int maprect_xsize;
	int maprect_ysize;
	int emxoptlevel;
	int emxopttrials;
	int emguardx;
	int preshift_step;
	int preshift_scale;

	float tolerance;
	float keytol;
	float alphatol;
	std::string sourcefile;
	std::string outfile;
	std::string tiletags;
	std::string rmacpath;
	std::string buildpath;
	int ccfields;
	std::string spriteguidefile;
	Cutmode cutmode;
	Outmode outmode;
	Masklayout masklayout;
	bool masklayout_lock;
	CCRemap ccremap;
	std::string palettefile;
	int planes;
	int tilesize;
	std::string invokehash;
	std::string mactmp;
	bool noremap;
	bool forceremap;

	std::list<guideevent> guideeventseq;
	// sources actually specified on commandline (1 or more)
	std::vector<std::string> explicit_sources;
	// all sources, after glob expansion. this is what gets used if multiple sources were specified.
	std::vector<std::string> sources;

	spritemap_t spritemappings;

	// internal: not set via commandline
	bool use_solutionpair_reduction_;
	bool distributed_dualfield_;
};

Arguments g_allargs;

static ulong_t strqhash(ulong_t _h, char* _str)
{
	ulong_t h = _h;

	int p = 0;
	ubyte_t c = 0;
	while ((c = _str[p]))
	{
		h -= rofl32(h, 7);
		h *= 0x12967ad1;
		h += rofl32(h, 17);
		h = (h + 0x3869b5a2) * 0x72b1d643;
		p++;
	}

	return h;
}

static void GetArguments(int _argc, char **_argv, Arguments& _args)
{
    CSimpleOpt args(_argc, _argv, g_Options);
	std::string s_outfile;

	// create a hash of all arguments e.g. for tempfile specialization in parallel builds
	// this is more stable/repeatable for debugging vs using time or PID or some other flaky source
	{
		char buf[16];
		ulong_t h = 0x194bca85;
		for (int a = 0; a < _argc; a++)
		{
			if (_argv[a])
				h = strqhash(h, _argv[a]);
		}
		sprintf(buf, "%08x", h);
		_args.invokehash = buf;
	}


    // while there are arguments left to process
    while (args.Next()) 
    {
		if (args.LastError() == SO_SUCCESS) 
		{
			if (args.OptionId() == OPT_HELP) 
			{
				usage(1);
			}
			else if (args.OptionId() == OPT_VERBOSE) 
			{
				_args.verbose = true;
			}
			else if (args.OptionId() == OPT_QUIET) 
			{
				_args.quiet = true;
			}
			else if (args.OptionId() == OPT_MPLIST) 
			{
				_args.mplist = true;
			}
			else if (args.OptionId() == OPT_DEBUG) 
			{
				_args.debug = true;
				_args.verbose = true;
				_args.mplist = true;
				_args.quiet = false;
			}
			else if (args.OptionId() == OPT_WAIT_KEY) 
			{
				_args.waitkey = true;
			}			
			else if (args.OptionId() == OPT_PREVIEW) 
			{
				_args.preview = true;
			}
			else if (args.OptionId() == OPT_GENMASK) 
			{
				_args.genmask = true;
			}
			else if (args.OptionId() == OPT_NOGENMASK) 
			{
				_args.genmask = false;
			}	
			else if (args.OptionId() == OPT_KEYGUARD) 
			{
				_args.keyguard = true;
			}	
			//
			else if (args.OptionId() == OPT_SOURCE)
			{
				_args.sourcefile = args.OptionArg();
				// add explicit files to sources
				_args.explicit_sources.push_back(_args.sourcefile);
			}
			else if (args.OptionId() == OPT_OUTFILE)
			{
				_args.outfile = args.OptionArg();
			}
			else if (args.OptionId() == OPT_RMACPATH)
			{
				_args.rmacpath = args.OptionArg();
			}
			else if (args.OptionId() == OPT_BUILDPATH)
			{
				_args.buildpath = args.OptionArg();
			}
			else if (args.OptionId() == OPT_TILETAGS)
			{
				_args.tiletags = args.OptionArg();
			}
			else if (args.OptionId() == OPT_TOLERANCE)
			{
				std::string s_arg = args.OptionArg();
				float farg = (float)atof(s_arg.c_str());
				if (farg < 0.0f || farg > 1e4f)
				{
					std::cerr << "error: invalid arg for tile matching tolerance: " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
				_args.tolerance = farg;	
			}
			else if (args.OptionId() == OPT_KEYTOL)
			{
				std::string s_arg = args.OptionArg();
				float farg = (float)atof(s_arg.c_str());
				if (farg < 0.0f || farg > 1e4f)
				{
					std::cerr << "error: invalid arg for key guard tolerance: " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
				_args.keytol = farg;	
			}
			else if (args.OptionId() == OPT_ALPHATOL)
			{
				std::string s_arg = args.OptionArg();
				float farg = (float)atof(s_arg.c_str());
				if (farg < 0.0f || farg > 1e4f)
				{
					std::cerr << "error: invalid arg for alpha key tolerance: " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
				_args.alphatol = farg;	
			}
			else if (args.OptionId() == OPT_DFALPHA) 
			{
				_args.dualfield_alpha = true;
			}			
			else if (args.OptionId() == OPT_CUTMODE)
			{
				std::string s_arg = args.OptionArg();

				if (s_arg.find("tiles") != std::string::npos)
				{
					_args.cutmode = Cutmode_TILES;
				}
				else
				if ((s_arg.find("imspr") != std::string::npos) || 
					(s_arg.find("sprites") != std::string::npos))
				{
					_args.cutmode = Cutmode_SPRITES;
				}
				else
				if (s_arg.find("emspr") != std::string::npos)
				{
					_args.cutmode = Cutmode_EMSPR;
					_args.masklayout = Masklayout_PRESHIFT;
					_args.masklayout_lock = true;
					_args.genmask = true;
				}
				else
				if (s_arg.find("emxspr") != std::string::npos)
				{
					_args.cutmode = Cutmode_EMXSPR;
					_args.masklayout = Masklayout_PRESHIFT;
					_args.masklayout_lock = true;
					_args.genmask = true;
				}
				else
				if (s_arg.find("emhspr") != std::string::npos)
				{
					_args.cutmode = Cutmode_EMHSPR;
					_args.masklayout = Masklayout_PRESHIFT;
					_args.masklayout_lock = true;
					_args.genmask = true;
				}
				else
				if (s_arg.find("slabs") != std::string::npos)
				{
					_args.cutmode = Cutmode_SLABS;
					_args.masklayout = Masklayout_IMPLIED;
					_args.masklayout_lock = true;
				}
				else
				if (s_arg.find("slabrestore") != std::string::npos)
				{
					_args.cutmode = Cutmode_SLABRESTORE;
					_args.masklayout = Masklayout_IMPLIED;
					_args.masklayout_lock = true;
				}
				else
				{
					std::cerr << "error: invalid arg for cutmode (tiles/imspr/emspr/emxspr/slabs/slabrestore): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
			}
			else if (args.OptionId() == OPT_OUTMODE)
			{
				std::string s_arg = args.OptionArg();

				if (s_arg.find("degas") != std::string::npos)
				{
					_args.outmode = Outmode_DEGAS;
					g_maxplanes = 4;
				}
				else
				if (s_arg.find("direct") != std::string::npos)
				{
					_args.outmode = Outmode_DIRECT;
					g_maxplanes = 8;
				}
				else
				if (s_arg.find("rbplus") != std::string::npos)
				{
					_args.outmode = Outmode_RBPLUS;
					g_maxplanes = 8;
				}
				else
				{
					std::cerr << "error: invalid arg for outmode (degas/direct/rbplus): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
			}
			else if (args.OptionId() == OPT_STITCHMAPS)
			{
				_args.stitchmaps = true;
			}
			else if (args.OptionId() == OPT_TRANSLAYER)
			{
				_args.translayer = true;
			}
			else if (args.OptionId() == OPT_MASKLAYOUT)
			{
				std::string s_arg = args.OptionArg();

				// cutmode can dictate mask layout for some modes
				if (!_args.masklayout_lock)
				{
					if (s_arg.find("planar") != std::string::npos)
					{
						_args.masklayout = Masklayout_PLANAR;
					}
					else
					if (s_arg.find("interleaved") != std::string::npos)
					{
						_args.masklayout = Masklayout_INTERLEAVED;
					}
					else
					if (s_arg.find("preshift") != std::string::npos)
					{
						_args.masklayout = Masklayout_PRESHIFT;
					}
					else
					{
						std::cerr << "error: invalid arg for masklayout (planar/interleaved/preshift): " << s_arg << std::endl;
						std::cout << "press any key..." << std::endl << std::flush;
						getchar();
						exit(1);
					}
				}
			}
			else if (args.OptionId() == OPT_QUICKCUT)
			{
				_args.quickcut = true;
			}
			else if (args.OptionId() == OPT_VIS)
			{
				_args.visual = true;
			}
			else if (args.OptionId() == OPT_HIDDEN)
			{
				_args.hidden = true;
			}
			else if (args.OptionId() == OPT_SPROPT)
			{
				std::string s_arg = args.OptionArg();

				if (s_arg.find("on") != std::string::npos)
				{
					_args.spriteoptimize = true;
				}
				else
				if (s_arg.find("off") != std::string::npos)
				{
					_args.spriteoptimize = false;
				}
				else
				{
					std::cerr << "error: invalid arg for sprite-optimize (on/off): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
			}
			else if (args.OptionId() == OPT_PALETTE)
			{
				_args.palettefile = args.OptionArg();
			}
			else if (args.OptionId() == OPT_SPRITEGUIDE)
			{
				_args.spriteguidefile = args.OptionArg();
			}
			else if (args.OptionId() == OPT_COLRENORM)
			{
				std::string s_arg = args.OptionArg();
				int iarg = atoi(s_arg.c_str());
				if (iarg < 1 || iarg > 255)
				{
					std::cerr << "error: invalid arg for col renorm (max=255): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
				_args.colrenorm = iarg;	
			}
			else if (args.OptionId() == OPT_EMXOPT)
			{
				std::string s_arg = args.OptionArg();
				int trials = 4;
				int iters = 0;
				if (s_arg.find(",") == std::string::npos)
				{
					// single argument (iters)
					iters = atoi(s_arg.c_str());
				}
				else
				{
					// x,y pair (iters:trials)
					size_t split = s_arg.find(",");
					std::string x = s_arg.substr(0,split);
					std::string y = s_arg.substr(split+1, std::string::npos);
					iters = atoi(x.c_str());
					trials = atoi(y.c_str());

					if (trials <= 0 || trials > 255)
					{
						std::cerr << "error: invalid arg for emxopt trials <iters,trials> (0 < trials < 256): " << s_arg << std::endl;
						std::cout << "press any key..." << std::endl << std::flush;
						getchar();
						exit(1);
					}
				}

				if (iters < 0 || iters > 31)
				{
					std::cerr << "error: invalid arg for emxopt iters (max=31): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
				_args.emxoptlevel = iters;	
				_args.emxopttrials = trials;	
			}
			else if (args.OptionId() == OPT_PRESHIFTSTEP)
			{
				std::string s_arg = args.OptionArg();
				int iarg = atoi(s_arg.c_str());
				if 
				(
					!(
						iarg == 1 || 
						iarg == 2 ||
						iarg == 4 ||
						iarg == 8 ||
						iarg == 16
					)
				)
				{
					std::cerr << "error: invalid arg for preshift step (1/2/4/8/16): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
				_args.preshift_step = iarg;	
				_args.preshift_scale = bceil(_args.preshift_step);
				std::cout << "preshift step=" << _args.preshift_step << " scale=" << _args.preshift_scale << std::endl;
			}
			else if (args.OptionId() == OPT_EMGUARDX)
			{
				std::string s_arg = args.OptionArg();
				int iarg = atoi(s_arg.c_str());
				if (iarg < 0 || iarg > 256)
				{
					std::cerr << "error: invalid arg for emguardx (0-256): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
				_args.emguardx = iarg;	
			}
			else if (args.OptionId() == OPT_EMC)
			{
				_args.emc = true;
			}
			else if (args.OptionId() == OPT_EMX_NO_NFSR)
			{
				_args.no_nfsr = true;
			}
			else if (args.OptionId() == OPT_OCM)
			{
				_args.ocm = true;
			}
			else if (args.OptionId() == OPT_NOCLIPX)
			{
				_args.noclipx = true;
			}
			else if (args.OptionId() == OPT_NOCLIPY)
			{
				_args.noclipy = true;
			}
			else if (args.OptionId() == OPT_NOCLIP)
			{
				_args.noclipx = true;
				_args.noclipy = true;
			}
			else if (args.OptionId() == OPT_KEYCOL)
			{
				std::string s_arg = args.OptionArg();
				int iarg = atoi(s_arg.c_str());
				if (iarg < 0 || iarg > 255)
				{
					std::cerr << "error: invalid arg for key colour index (max=255): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
				_args.keycol = iarg;	
			}
			else if (args.OptionId() == OPT_KEYRGB)
			{
				_args.keyrgb_specified = true;

				std::string s_arg = args.OptionArg();
				
				int r, g, b;
				sscanf(s_arg.c_str(), "%x:%x:%x", &r, &g, &b);

				if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255)
				{
					std::cerr << "error: invalid arg for key rgb (hex RR:GG:BB): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}

				printf("key colour from rgb: r(%x):g(%x):b(%x):\n", r, g, b);

				_args.keyrgb.red = r;
				_args.keyrgb.green = g;
				_args.keyrgb.blue = b;
			}

			else if (args.OptionId() == OPT_ALPHARGB)
			{
				_args.alphargb_specified = true;

				std::string s_arg = args.OptionArg();
				
				int r, g, b;
				sscanf(s_arg.c_str(), "%x:%x:%x", &r, &g, &b);

				if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255)
				{
					std::cerr << "error: invalid arg for alpha rgb (hex RR:GG:BB): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}

				printf("alpha key colour from rgb: r(%x):g(%x):b(%x):\n", r, g, b);

				_args.alphargb.red = r;
				_args.alphargb.green = g;
				_args.alphargb.blue = b;
			}
			else if (args.OptionId() == OPT_AUTOCUTRGB)
			{
				_args.autocutkeyrgb_specified = true;

				std::string s_arg = args.OptionArg();
				
				int r, g, b;
				sscanf(s_arg.c_str(), "%x:%x:%x", &r, &g, &b);

				if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255)
				{
					std::cerr << "error: invalid arg for autocut key rgb (hex RR:GG:BB): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}

				printf("autocut key colour from rgb: r(%x):g(%x):b(%x):\n", r, g, b);

				_args.autocutkeyrgb.red = r;
				_args.autocutkeyrgb.green = g;
				_args.autocutkeyrgb.blue = b;
			}

			else if (args.OptionId() == OPT_HITRGB)
			{
				_args.hitkeyrgb_specified = true;

				std::string s_arg = args.OptionArg();
				
				int r, g, b;
				sscanf(s_arg.c_str(), "%x:%x:%x", &r, &g, &b);

				if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255)
				{
					std::cerr << "error: invalid arg for hitkey rgb (hex RR:GG:BB): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}

				printf("hitkey colour from rgb: r(%x):g(%x):b(%x):\n", r, g, b);

				_args.hitkeyrgb.red = r;
				_args.hitkeyrgb.green = g;
				_args.hitkeyrgb.blue = b;
			}

			else if (args.OptionId() == OPT_TILESIZE)
			{
				std::string s_arg = args.OptionArg();
				int iarg = atoi(s_arg.c_str());
				if ((iarg != 8) && (iarg != 16) && (iarg != 32))
				{
					std::cerr << "error: invalid arg for tile size (8/16/32): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
				_args.tilesize = iarg;	
			}
			else if (args.OptionId() == OPT_CCFIELDS)
			{
				std::string s_arg = args.OptionArg();
				int iarg = atoi(s_arg.c_str());
				if ((iarg != 1) && (iarg != 2))
				{
					std::cerr << "error: invalid arg for ccfields (1/2): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
				_args.ccfields = iarg;	
				_args.dualfield = _args.ccfields > 1;
			}
			else if (args.OptionId() == OPT_CCREMAP)
			{
				std::string s_arg = args.OptionArg();

				if (s_arg.find("both") != std::string::npos)
				{
					_args.ccremap = CCRemap_BOTH;
				}
				else
				if (s_arg.find("high") != std::string::npos)
				{
					_args.ccremap = CCRemap_HIGH;
				}
				else
				if (s_arg.find("low") != std::string::npos)
				{
					_args.ccremap = CCRemap_LOW;
				}
				else
				if (s_arg.find("match") != std::string::npos)
				{
					_args.ccremap = CCRemap_MATCH;
				}
				else
				{
					std::cerr << "error: invalid arg for cryptochrome dualfield remap (both/high/low/match): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
			}
			else if (args.OptionId() == OPT_NOREMAP)
			{
				_args.noremap = true;
			}
			else if (args.OptionId() == OPT_FORCEREMAP)
			{
				_args.forceremap = true;
			}
			else if (args.OptionId() == OPT_MAPRECT)
			{
				std::string s_arg = args.OptionArg();
				
				// try extracting all 4 fields of cutting rectangle
				int mx = 0, my = 0, mw = 0x7fffffff, mh = 0x7fffffff;
				int count = sscanf(s_arg.c_str(), "%d:%d:%d:%d", &mx, &my, &mw, &mh);
				if (count < 4)
				{
					// otherwise try just x,y
					mw = 0x7fffffff;
					mh = 0x7fffffff;
					count = sscanf(s_arg.c_str(), "%d:%d", &mx, &my);
				}

				if ((count < 2) ||
					(mx < 0) || 
					(my < 0) ||
					(mw < 0) ||
					(mh < 0))
				{
					std::cerr << "error: invalid arg for map cutting rectangle: " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}

				if (count == 2)
					printf("cutting map sub-rectangle from tilemap (dimensions in tiles): x(%d):y(%d)\n",
						mx, my);
				else
					printf("cutting map sub-rectangle from tilemap (dimensions in tiles): x(%d):y(%d):w(%d):h(%d)\n",
						mx, my, mw, mh);
			

				_args.maprect_xpos = mx;
				_args.maprect_ypos = my;
				_args.maprect_xsize = mw;
				_args.maprect_ysize = mh;
			}
			else if (args.OptionId() == OPT_BITPLANES)
			{
				std::string s_arg = args.OptionArg();
				int iarg = atoi(s_arg.c_str());
				if (iarg < 0 || iarg > g_maxplanes)
				{
					std::cerr << "error: invalid arg for bitplanes/bpp: " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
				_args.planes = iarg;	
			}
			else if (args.OptionId() == OPT_SPRXP)
			{
				std::string s_arg = args.OptionArg();
				int iarg = atoi(s_arg.c_str());
				if (iarg < 0 || iarg > 4096)
				{
					std::cerr << "error: invalid arg for sprite cutting position (max=4096): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
				_args.cut_xpos = iarg;	
			}
			else if (args.OptionId() == OPT_SPRYP)
			{
				std::string s_arg = args.OptionArg();
				int iarg = atoi(s_arg.c_str());
				if (iarg < 0 || iarg > 4096)
				{
					std::cerr << "error: invalid arg for sprite cutting position (max=4096): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
				_args.cut_ypos = iarg;	
			}
			else if (args.OptionId() == OPT_SPRXS)
			{
				std::string s_arg = args.OptionArg();
				int iarg = atoi(s_arg.c_str());
				if (iarg < 1 || iarg > 4096)
				{
					std::cerr << "error: invalid arg for sprite cutting size (max=4096): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
				if ((_args.cutmode == Cutmode_SPRITES) && (iarg > 128))
				{
					printf("warning: sprite cutting width %s exceeds max. of 128 for AGT sprite system!\n", s_arg.c_str());
				}
				else
				if ((_args.cutmode == Cutmode_SLABS || _args.cutmode == Cutmode_SLABRESTORE) && (iarg > 256))
				{
					printf("warning: sprite cutting width %s exceeds max. of 256 for AGT slab system!\n", s_arg.c_str());
				}
				_args.cut_xsize = iarg;	
			}
			else if (args.OptionId() == OPT_SPRYS)
			{
				std::string s_arg = args.OptionArg();
				int iarg = atoi(s_arg.c_str());
				if (iarg < 1 || iarg > 4096)
				{
					std::cerr << "error: invalid arg for sprite cutting size (max=4096): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
				_args.cut_ysize = iarg;	
			}
			else if (args.OptionId() == OPT_SPRXI)
			{
				std::string s_arg = args.OptionArg();
				int iarg = atoi(s_arg.c_str());
				if (iarg < 0 || iarg > 4096)
				{
					std::cerr << "error: invalid arg for sprite cutting increment/step (max=4096): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
				_args.cut_xinc = iarg;	
			}
			else if (args.OptionId() == OPT_SPRYI)
			{
				std::string s_arg = args.OptionArg();
				int iarg = atoi(s_arg.c_str());
				if (iarg < 0 || iarg > 4096)
				{
					std::cerr << "error: invalid arg for sprite cutting increment/step (max=4096): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
				_args.cut_yinc = iarg;	
			}
			else if (args.OptionId() == OPT_SPRXC)
			{
				std::string s_arg = args.OptionArg();
				int iarg = atoi(s_arg.c_str());
				if (iarg < 1 || iarg > 1024)
				{
					std::cerr << "error: invalid arg for sprite cutting x-count (max=1024): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
				_args.cut_xcount = iarg;	
			}
			else if (args.OptionId() == OPT_SPRYC)
			{
				std::string s_arg = args.OptionArg();
				int iarg = atoi(s_arg.c_str());
				if (iarg < 1 || iarg > 1024)
				{
					std::cerr << "error: invalid arg for sprite cutting y-count (max=1024): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
				_args.cut_ycount = iarg;	
			}
			else if (args.OptionId() == OPT_SPRCOUNT)
			{
				std::string s_arg = args.OptionArg();
				int iarg = atoi(s_arg.c_str());
				if (iarg < 1 || iarg > 1024)
				{
					std::cerr << "error: invalid arg for sprite cutting count (max=1024): " << s_arg << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();
					exit(1);
				}
				_args.cut_count = iarg;	
			}
			else if (args.OptionId() == OPT_SPRMAP)
			{
				std::string s_arg = args.OptionArg();

				char flipflags[256];
				int to, from;
				sscanf(s_arg.c_str(), "%d:%d:%s", &to, &from, flipflags);

				// record each individual mapping for processing later
				{
					spritemapping_t sm;
					sm.to = to;
					sm.from = from;
					sm.flags = flipflags;
					sm.x = sm.y = 0;
					g_allargs.spritemappings[to] = sm;
				}
			}
		}
		else 
		{
			std::cerr << "error: invalid argument: " << args.OptionText() << std::endl;
			std::cout << "press any key..." << std::endl << std::flush;
			getchar();
			exit(1);
		}
    }

	{
		if (!_args.buildpath.empty())
			_args.mactmp = _args.buildpath + "/";
		else
			_args.mactmp = "";
		
		_args.mactmp += "tmp68k_";
		_args.mactmp += _args.invokehash;
	}

	// certain modes expect globbing
	if (_args.stitchmaps)
	{
		// multiple sources, single output

		// can't stitch from a glob if no outfile was specified
		if (_args.outfile.length() == 0)
		{
			std::cerr << "error: must specify -o output filename stub if using multiple source images" << std::endl;
			std::cout << "press any key..." << std::endl << std::flush;
			getchar();
			exit(1);
		}

		// process input paths
		
		// only expand glob if 1 source specified, otherwise treat each source as distinct
		if (_args.explicit_sources.size() > 1)
		{
			std::cout << "multiple sources specified..." << std::endl;

			_args.sources = _args.explicit_sources;
		}
		else
		{
			std::cout << "single source specified - expanding glob/wildcards..." << std::endl;

#if defined(_MSC_VER)
			CSimpleGlob glob(SG_GLOB_NOCHECK);
			if (SG_SUCCESS != glob.Add(_args.sourcefile.c_str()))
			{
				std::cerr << "error: failed to expand wildcard glob" << std::endl;
				std::cout << "press any key..." << std::endl << std::flush;
				getchar();
				exit(1);
			}

			// check infile/outfile specification rules  
			if (glob.FileCount() == 0)
			{
				std::cerr << "error: no input file specified!" << std::endl;
				std::cout << "press any key..." << std::endl << std::flush;
				getchar();
				exit(1);
			}
			else
			{
				// expand input filespec to individual files
				for (int n = 0; n < glob.FileCount(); ++n)
				{
					std::string namein(glob.File(n));

					printf("expanded glob: %s\n", namein.c_str());

					// in case only one file globbed, use normal mode - otherwise record multiple sources
					if (glob.FileCount() > 1)
						_args.sources.push_back(namein);
					if (n == 0)
						_args.sourcefile = namein;
				}
			}
#else
			_args.sources.push_back(_args.sourcefile);
#endif
		}
	}
	else
	{
		// multiple sources, multiple outputs

		// no glob expansion - explicit sources only
		if (_args.explicit_sources.size() > 1)
		{
			std::cout << "multiple sources specified..." << std::endl;

			_args.sources = _args.explicit_sources;
		}
	}

	if (_args.sourcefile.length() == 0)
	{
		usage(0);
		exit(1);
	}

	if (g_allargs.ccfields > 1)
	{
		if (g_allargs.palettefile.find(".ccs") == std::string::npos)
		{
			std::cerr << "error: dual-field remapping requires a [-p <name>.ccs] colourmap source" << std::endl;
			exit(-1);
		}
	}
	
	// trounce verbosity in quickcut mode
	//if (g_allargs.quickcut)
	//	g_allargs.verbose = false;

	if (
		(_args.cutmode == Cutmode_EMXSPR) ||
		(_args.cutmode == Cutmode_EMHSPR)
	)
	{
		if (_args.emc)
			std::cout << "note: EMX/EMH support arbitrary-width sprites without -emc" << std::endl;
		else
			_args.emc = true;
	}

	if (_args.cutmode == Cutmode_SLABRESTORE)
	{
		_args.compound_slabs = false;
	}

	if (g_allargs.hidden)
	{
		if (g_allargs.cutmode != Cutmode_SPRITES)
		{
			std::cerr << "can only apply -hidden optimization in imspr cutmode. (emspr/emxspr can still be used for hidden assets but will always contain pixel data)." << std::endl;
			std::cout << "press any key..." << std::endl << std::flush;
			getchar();
			exit(1);
		}
	}

	if (g_allargs.cutmode == Cutmode_EMSPR)
	{
		if (g_allargs.emc)
		{
			// no x clipping supported in compound EMS
			// does remain possible for <=32-wide EMS
			g_allargs.noclipx = true;
		}
	}
	else
	if (g_allargs.cutmode == Cutmode_EMXSPR ||
		g_allargs.cutmode == Cutmode_EMHSPR)
	{
		// no x-clipping supported at all in EMX/EMH
		g_allargs.noclipx = true;
	}

	if (g_allargs.dualfield_alpha)
	{
		if (g_allargs.cutmode == Cutmode_EMSPR || 
			g_allargs.cutmode == Cutmode_EMXSPR ||
			g_allargs.cutmode == Cutmode_EMHSPR)
		{
			if ((g_allargs.ccfields == 1) &&
				(g_allargs.alphargb_specified))
			{
				// emit 2 distinct sprite frames for each frame cut
				g_allargs.distributed_dualfield_ = true;
			}
			else
			{
				std::cerr << "dual-field alpha (--df-alpha) requires combined settings (--alpha-rgb <RGB> --ccfields 1), producing 2x as many frames with single colour field each" << std::endl;
				std::cout << "press any key..." << std::endl << std::flush;
				getchar();
				exit(1);
			}
		}
		else
		{
			std::cerr << "dual-field alpha only supported for EMS/EMX sprites" << std::endl;
			std::cout << "press any key..." << std::endl << std::flush;
			getchar();
			exit(1);
		}
	}


	// adjust counts to make sense for common scenarios with terse commandlines

	if (g_allargs.cut_count > 0)
	{
		// if count cap has been set, decide if this is a grid, row or column cutting sequence
		if (g_allargs.cut_xcount && g_allargs.cut_ycount)
		{
			// grid. leave things alone.
		}
		else
		{
			// either row, or column, or single, or error
			if (g_allargs.cut_xinc == 0 && g_allargs.cut_yinc == 0)
			{
				// single cut. leave things alone
			}
			if (g_allargs.cut_yinc == 0)
			{
				// row
				g_allargs.cut_xcount = 1024;
			}
			else
			if (g_allargs.cut_xinc == 0)
			{
				// column
				g_allargs.cut_ycount = 1024;
			}
		}
	}
	else
	{
		// otherwise make sure each grid axis has count of at least 1
		// this works for grids, single rows and single columns
		if (g_allargs.cut_xcount == 0)
			g_allargs.cut_xcount = 1;
		if (g_allargs.cut_ycount == 0)
			g_allargs.cut_ycount = 1;
	}

	// if a spriteguide file was specified, read it
	if (g_allargs.spriteguidefile.length() > 0)
	{
		std::cout << "processing sprite guide file: " << g_allargs.spriteguidefile.c_str() << std::endl;
		std::ifstream sprguide(g_allargs.spriteguidefile.c_str());
		if (!(sprguide.is_open() && sprguide.good()))
		{
			std::cerr << "error: unable to locate or read specified spriteguide file!" << std::endl;
			std::cout << "press any key..." << std::endl << std::flush;
			getchar();
			exit(1);
		}

		// [size]
		int cutxs = 16;
		int cutys = 16;
		// [step]
		int cutxi = 0;
		int cutyi = 0;
		// [flags]
		std::string cutflags;
		// [xbase:right]
		int cutxbase = 0;
		// [ybase:bottom]
		int cutybase = 0;
		// [cut]
		int cutcount = 1;
		int cutxp = 0;
		int cutyp = 0;
		int cutsofar = 0;
		int cmdmode = -1;

		// parse CSV
		for (CSVIterator loop(sprguide); loop != CSVIterator(); ++loop)
		{
			const CSVRow& r = (*loop);
			size_t s = r.size();
			if (s > 0)
			{
				std::string inspect = r[0];

				if ((inspect.length() == 0) ||
					(inspect[0] == ';'))
				{
					// skip comments & empty lines
				}
				else
				if (inspect[0] == '[')
				{
					// command
					if (inspect.find("[size]") == 0)
					{
						cmdmode = 0;
					}
					else
					if (inspect.find("[step]") == 0)
					{
						cmdmode = 1;
					}
					else
					if (inspect.find("[flags]") == 0)
					{
						cmdmode = 2;
					}
					else
					if (inspect.find("[cut]") == 0)
					{
						cmdmode = 3;
					}
					else
					if (inspect.find("[ybase:bottom]") == 0)
					{
						cutybase = 1;
						// no args
					}
					else
					if (inspect.find("[xbase:right]") == 0)
					{
						cutxbase = 1;
						// no args
					}
					else
					{
						std::cerr << "error: unknown spriteguide command: " << inspect.c_str() << std::endl;
						std::cout << "press any key..." << std::endl << std::flush;
						getchar();
						exit(1);
					}
				}
				else
				{
					int column_value = 0;
					int column_index = 0;
					std::string colval;
					char *pdummy = 0;

					switch (cmdmode)
					{
					case 0:
						{
							// size
							colval = r[column_index++];
							std::transform(colval.begin(), colval.end(), colval.begin(), sanitise_csv_integer);
							column_value = strtol(colval.c_str(), &pdummy, 10);
							cutxs = column_value;
							//
							colval = r[column_index++];
							std::transform(colval.begin(), colval.end(), colval.begin(), sanitise_csv_integer);
							column_value = strtol(colval.c_str(), &pdummy, 10);
							cutys = column_value;

							cmdmode = -1;
						}
						break;
					case 1:
						{
							// step
							colval = r[column_index++];
							std::transform(colval.begin(), colval.end(), colval.begin(), sanitise_csv_integer);
							column_value = strtol(colval.c_str(), &pdummy, 10);
							cutxi = column_value;
							//
							colval = r[column_index++];
							std::transform(colval.begin(), colval.end(), colval.begin(), sanitise_csv_integer);
							column_value = strtol(colval.c_str(), &pdummy, 10);
							cutyi = column_value;

							cmdmode = -1;
						}
						break;
					case 2:
						{
							// flags
							colval = r[column_index++];
							cutflags = colval;

							cmdmode = -1;
						}
						break;
					case 3:
						{
							// count
							colval = r[column_index++];
							std::transform(colval.begin(), colval.end(), colval.begin(), sanitise_csv_integer);
							column_value = strtol(colval.c_str(), &pdummy, 10);
							cutcount = column_value;
							// xpos
							colval = r[column_index++];
							std::transform(colval.begin(), colval.end(), colval.begin(), sanitise_csv_integer);
							column_value = strtol(colval.c_str(), &pdummy, 10);
							cutxp = column_value;
							// ypos
							colval = r[column_index++];
							std::transform(colval.begin(), colval.end(), colval.begin(), sanitise_csv_integer);
							column_value = strtol(colval.c_str(), &pdummy, 10);
							cutyp = column_value;

							if (cutxbase)
								cutxp -= (cutxs - 1);

							if (cutybase)
								cutyp -= (cutys - 1);

							// create extra mappings for frames with flags
							if (cutflags.length() > 0)
							{
								int mapend = cutsofar + cutcount;
								int mappos = cutsofar;
								while (mappos < mapend)
								{
									spritemapping_t &mapping = g_allargs.spritemappings[mappos];
									mapping.flags = cutflags;
									// no remapping of frames, just applying flags
									// also, no need to modify x/y position when not remapping frames
									mapping.from = mappos;
									mapping.to = mappos;
									mappos++;
								}
							}

							// record cutting event
							g_allargs.guideeventseq.push_back(guideevent(cutcount, cutflags, cutxp, cutyp, cutxs, cutys, cutxi, cutyi));

							std::cout << "recorded cutting event:" <<
								" count=" << cutcount << 
								" xp=" << cutxp <<
								" yp=" << cutyp <<
								" xs=" << cutxs <<
								" ys=" << cutys <<
								" xi=" << cutxi <<
								" yi=" << cutyi <<
								" fl=" << cutflags.c_str() <<
								std::endl;

							// reset flags
							cutflags = "";

							cutsofar += cutcount;

							cmdmode = -1;
						}
					default:
						break;
					};
				}
			}
		}
	}
	else
	{
		// may still be using autocut mode - but we'll build sprite guides after loading & processing the source image
		// into the required colour format etc.
	}
}

// --------------------------------------------------------------------
//	palette reading: 24bit .PAL
// --------------------------------------------------------------------

static bool read_pal24(const char* _fname, RGB *&r_ppal, unsigned short& r_numcolours, unsigned short& r_numuniquecolors)
{
	// we only need this here, so why pollute?
	typedef struct bgra_s
	{
		ubyte_t red;
		ubyte_t green;
		ubyte_t blue;
		ubyte_t alpha;
	} bgra_t;

	FILE *hpal = fopen(_fname, "rb");
	if (hpal)
	{
		std::cout << "reading fixed palette source [" << _fname << "]" << std::endl;

		// yeah, i know...
		fseek(hpal, 4 + 4 + 4 + 4 + 4 + 2, SEEK_SET);

		short numcolors = 0;
		fread(&numcolors, 2, 1, hpal);

		r_numcolours = numcolors;
		if (r_numcolours == 0)
		{
			fclose(hpal);
			return false;
		}

		{
			// read in the colors
			bgra_t *temp = new bgra_t[numcolors];
			fread(temp, 1, 4 * numcolors, hpal);
			fclose(hpal);

			// convert whatever colours we were able to read into local format
			r_ppal = new RGB[/*numcolors*/256];
			for (int i = 0; i < numcolors; i++)
			{
				r_ppal[i].blue = temp[i].blue;
				r_ppal[i].green = temp[i].green;
				r_ppal[i].red = temp[i].red;
			}

			delete[] temp;
		}

		// count distinct colours (in case of duplicates)
		int u = r_numcolours;
		for (int o = 0; o < r_numcolours; o++)
		{
			for (int i = o + 1; i < r_numcolours; i++)
			{
				if ((r_ppal[o].red == r_ppal[i].red) &&
					(r_ppal[o].green == r_ppal[i].green) &&
					(r_ppal[o].blue == r_ppal[i].blue))
					{
						u--; break;
					}
			}
		}
		r_numuniquecolors = u;

		return true;
	}

	// not recognized/not found
	std::cerr << "warning: can't open this palette file: " << _fname << std::endl;
	return false;
}

// --------------------------------------------------------------------
//	palette reading: 12bit .cct tileset files
// --------------------------------------------------------------------

static bool read_pal_cct(const char* _fname, RGB *&r_ppal, unsigned short& r_numcolours, unsigned short& r_numuniquecolours)
{
	FILE *hpal = fopen(_fname, "rb");
	if (hpal)
	{
		std::cout << "reading fixed palette source [" << _fname << "]" << std::endl;

		r_numcolours = 16;

		// read in the colors
		uword_t palette[16];
		//fread(palette, 1, 2, hpal);
		fread(palette, 1, 32, hpal);
		fclose(hpal);

		// convert whatever colours we were able to read into local format
		r_ppal = new RGB[256/*16*/];

		for (int i = 0; i < 16; i++)
		{
			// reformat from bigendian for motorola->intel
			uword_t ste_col = endianswap16(palette[i]);

			word_t ste_r = (ste_col >> 8) & 0xF;
			word_t ste_g = (ste_col >> 4) & 0xF;
			word_t ste_b = (ste_col >> 0) & 0xF;

			// reformat STE swizzled 4:4:4 format into 24bit 8:8:8
			word_t r = ((ste_r & 0x7) << 1) | ((ste_r >> 3) & 1);
			word_t g = ((ste_g & 0x7) << 1) | ((ste_g >> 3) & 1);
			word_t b = ((ste_b & 0x7) << 1) | ((ste_b >> 3) & 1);

			r_ppal[i].red   = r << 4;
			r_ppal[i].green = g << 4;
			r_ppal[i].blue  = b << 4;
		}

		// count distinct colours (in case of duplicates)
		int u = r_numcolours;
		for (int o = 0; o < r_numcolours; o++)
		{
			for (int i = o + 1; i < r_numcolours; i++)
			{
				if ((r_ppal[o].red == r_ppal[i].red) &&
					(r_ppal[o].green == r_ppal[i].green) &&
					(r_ppal[o].blue == r_ppal[i].blue))
					{
						u--; break;
					}
			}
		}
		r_numuniquecolours = u;

		return true;
	}

	// not recognized/not found
	std::cerr << "warning: can't open this image: [" << _fname << "]" << std::endl;
	return false;
}


// --------------------------------------------------------------------
//	palette reading: 18bit->4:4 solutionpair from PhotoChrome
// --------------------------------------------------------------------

u8 *g_rgb_solutionpair_map = NULL;
u8 g_solutionpairs[256];

static bool read_pal_ccs(const char* _fname, RGB *&r_ppal, unsigned short& r_numcolours, unsigned short& r_numuniquecolours)
{
	std::string file_ccs(_fname);		// name of solutionpair file
	std::string file_ccr(_fname);		// name of RGB->solutionpair reductionmap file
	std::string file_ccp(_fname);		// name of palette file

	replace_extension(file_ccr, "ccr");
	replace_extension(file_ccp, "ccp");

	FILE *hpal = NULL;
	FILE *hsol = NULL;
	FILE *hred = NULL;

	hpal = fopen(file_ccp.c_str(), "rb");
	if (hpal)
	{
		hsol = fopen(file_ccs.c_str(), "rb");
		if (hsol)
		{
			hred = fopen(file_ccr.c_str(), "rb");
			if (hred)
			{
				std::cout << "reading fixed palette fileset [" << file_ccs.c_str() << "] [" << file_ccr.c_str() << "] [" << file_ccp.c_str() << "]" << std::endl;

				r_numcolours = 16;

				// read in the colors
				uword_t palette[16];
				//fread(palette, 1, 2, hpal);
				fread(palette, 1, sizeof(palette), hpal);
				fclose(hpal); hpal = NULL;

				// convert whatever colours we were able to read into local format
				r_ppal = new RGB[256/*16*/];

				for (int i = 0; i < 16; i++)
				{
					// reformat from bigendian for motorola->intel
					uword_t ste_col = endianswap16(palette[i]);

					word_t ste_r = (ste_col >> 8) & 0xF;
					word_t ste_g = (ste_col >> 4) & 0xF;
					word_t ste_b = (ste_col >> 0) & 0xF;

					// reformat STE swizzled 4:4:4 format into 24bit 8:8:8
					word_t r = ((ste_r & 0x7) << 1) | ((ste_r >> 3) & 1);
					word_t g = ((ste_g & 0x7) << 1) | ((ste_g >> 3) & 1);
					word_t b = ((ste_b & 0x7) << 1) | ((ste_b >> 3) & 1);

					r_ppal[i].red = r << 4;
					r_ppal[i].green = g << 4;
					r_ppal[i].blue = b << 4;
				}

				// count distinct colours (in case of duplicates)
				int u = r_numcolours;
				for (int o = 0; o < r_numcolours; o++)
				{
					for (int i = o + 1; i < r_numcolours; i++)
					{
						if ((r_ppal[o].red == r_ppal[i].red) &&
							(r_ppal[o].green == r_ppal[i].green) &&
							(r_ppal[o].blue == r_ppal[i].blue))
						{
							u--; break;
						}
					}
				}
				r_numuniquecolours = u;

				// read solutionpairs (256 8bit records)
				fread(g_solutionpairs, 1, 256, hsol);
				fclose(hsol); hsol = NULL;

				// read RGB->solutionpair reductionmap (64x64x64 = 256kb table)
				delete[] g_rgb_solutionpair_map;
				g_rgb_solutionpair_map = new u8[64*64*64];
				fread(g_rgb_solutionpair_map, 1, (64*64*64), hred);
				fclose(hred); hred = NULL;

				return true;
			}
		}
	}

	if (hsol)
		fclose(hsol);
	if (hpal)
		fclose(hpal);

	// not recognized/not found
	std::cerr << "warning: can't open complete fileset: " << _fname << std::endl;
	return false;
}

// --------------------------------------------------------------------
//	palette reading: 12bit .PI1
// --------------------------------------------------------------------

static bool read_pal_pi1(const char* _fname, RGB *&r_ppal, unsigned short& r_numcolours, unsigned short& r_numuniquecolours)
{
	FILE *hpal = fopen(_fname, "rb");
	if (hpal)
	{
		std::cout << "reading fixed palette source [" << _fname << "]" << std::endl;

		r_numcolours = 16;

		// read in the colors
		uword_t palette[16];
		fread(palette, 1, 2, hpal);
		fread(palette, 1, 32, hpal);
		fclose(hpal);

		// convert whatever colours we were able to read into local format
		r_ppal = new RGB[256/*16*/];

		for (int i = 0; i < 16; i++)
		{
			// reformat from bigendian for motorola->intel
			uword_t ste_col = endianswap16(palette[i]);

			word_t ste_r = (ste_col >> 8) & 0xF;
			word_t ste_g = (ste_col >> 4) & 0xF;
			word_t ste_b = (ste_col >> 0) & 0xF;

			// reformat STE swizzled 4:4:4 format into 24bit 8:8:8
			word_t r = ((ste_r & 0x7) << 1) | ((ste_r >> 3) & 1);
			word_t g = ((ste_g & 0x7) << 1) | ((ste_g >> 3) & 1);
			word_t b = ((ste_b & 0x7) << 1) | ((ste_b >> 3) & 1);

			r_ppal[i].red   = r << 4;
			r_ppal[i].green = g << 4;
			r_ppal[i].blue  = b << 4;
		}

		// count distinct colours (in case of duplicates)
		int u = r_numcolours;
		for (int o = 0; o < r_numcolours; o++)
		{
			for (int i = o + 1; i < r_numcolours; i++)
			{
				if ((r_ppal[o].red == r_ppal[i].red) &&
					(r_ppal[o].green == r_ppal[i].green) &&
					(r_ppal[o].blue == r_ppal[i].blue))
					{
						u--; break;
					}
			}
		}
		r_numuniquecolours = u;

		return true;
	}

	// not recognized/not found
	std::cerr << "warning: can't open this image: [" << _fname << "]" << std::endl;
	return false;
}

// --------------------------------------------------------------------
//	wrappers for stb_image loader
// --------------------------------------------------------------------


// --------------------------------------------------------------------
//	load image, recovering indexed colour & palette if available
// --------------------------------------------------------------------

bool image_load(const char* filename, RGB *&r_psourcergb, unsigned char *&r_psourcecol, RGB *&r_ppalettergb, ulong_t &r_w, ulong_t &r_h, ulong_t &r_d)
{
	int stbx,stby,stbn;
	unsigned char *stbdata = stbi_load(filename, &stbx, &stby, &stbn, 3);

	if (stbdata)
	{
		int imagesize = stbx*stby;
		r_psourcergb = new RGB[imagesize];
		unsigned char* data = stbdata;
		for (int p = 0; p < imagesize; p++)
		{
			r_psourcergb[p].red = *data++;
			r_psourcergb[p].green = *data++;
			r_psourcergb[p].blue = *data++;
		}
		STBI_FREE(stbdata);

		if (g_stbi_colours)
		{
			r_ppalettergb = new RGB[256];
			for (int p = 0; p < g_stbi_colours; p++)
			{
				r_ppalettergb[p].red = g_stbi_palette[p][0];
				r_ppalettergb[p].green = g_stbi_palette[p][1];
				r_ppalettergb[p].blue = g_stbi_palette[p][2];
			}
		}

		r_psourcecol = g_stbi_colourmap;
		r_w = stbx;
		r_h = stby;
		r_d = g_stbi_bits;

		return true;
	}

	// not recognized/not found
	std::cerr << "warning: can't open this image" << std::endl;
	return false;
}

// --------------------------------------------------------------------
//	load image, recovering ONLY the palette if available
// --------------------------------------------------------------------

bool palette_load(const char* filename, RGB *&r_ppal, unsigned short &r_numcolours, unsigned short &r_numuniquecolours)
{
	int stbx,stby,stbn;
	unsigned char *stbdata = stbi_load(filename, &stbx, &stby, &stbn, 3);

	if (stbdata)
	{
		// don't need pixel data
		STBI_FREE(stbdata);

		if (g_stbi_colours)
		{
			r_ppal = new RGB[256];
			for (int p = 0; p < g_stbi_colours; p++)
			{
				r_ppal[p].red = g_stbi_palette[p][0];
				r_ppal[p].green = g_stbi_palette[p][1];
				r_ppal[p].blue = g_stbi_palette[p][2];
			}
			
			r_numcolours = g_stbi_colours;

			// count distinct colours (in case of duplicates)
			int u = r_numcolours;
			for (int o = 0; o < r_numcolours; o++)
			{
				for (int i = o + 1; i < r_numcolours; i++)
				{
					if ((r_ppal[o].red == r_ppal[i].red) &&
						(r_ppal[o].green == r_ppal[i].green) &&
						(r_ppal[o].blue == r_ppal[i].blue))
					{
						u--; break;
					}
				}
			}
			r_numuniquecolours = u;

			return true;
		}

		// not indexed colour - no palette
		std::cerr << "warning: non-indexed colour image or unsupported image type - no palette available" << std::endl;
		return false;
	}

	// not recognized/not found
	std::cerr << "warning: can't open this image" << std::endl;
	return false;
}

// --------------------------------------------------------------------
//	tile dictionary stuff
// --------------------------------------------------------------------

typedef std::list<RGB*> tile24dictionary_t;
typedef std::list<ubyte_t*> tile8dictionary_t;
typedef std::list<uword_t*> tileplanardictionary_t;

tile24dictionary_t s_tile24dictionary;
tile8dictionary_t s_tile8dictionary[2];
tileplanardictionary_t s_tileplanardictionary[2];

// extract pixels from 8bit indexed tile data
static inline void extract_tile8(ubyte_t *_pdsttile, ubyte_t *_psrctile, ulong_t _sourcew)
{
	int tilesize = g_allargs.tilesize;
	for (int py = 0; py < tilesize; py++)
	{
//		printf("\n");
		for (int px = 0; px < tilesize; px++)
		{
//			printf("%02x ", _psrctile[(py * _sourcew) + px]);
			_pdsttile[(py * tilesize) + px] = _psrctile[(py * _sourcew) + px];
		}
	}
}

// extract pixels from 24bit TC tile data (for error comparison)
static inline void extract_tile24(RGB *_pdsttile, RGB *_psrctile, ulong_t _sourcew)
{
	int tilesize = g_allargs.tilesize;
	for (int py = 0; py < tilesize; py++)
	{
//		printf("\n");
		for (int px = 0; px < tilesize; px++)
		{
//			printf("%02x ", _psrctile[(py * _sourcew) + px].green);
			_pdsttile[(py * tilesize) + px] = _psrctile[(py * _sourcew) + px];
		}
	}
}

// compare two 24bit tiles for similarity
static inline float compare_tile24(RGB *_pdsttile, RGB *_psrctile, ulong_t _sourcew)
{
	int tilesize = g_allargs.tilesize;

	static const float c_n = 1.0f / 255.0f;

	float errsum = 0.0f;

	for (int py = 0; py < tilesize; py++)
	{
		for (int px = 0; px < tilesize; px++)
		{
			const RGB &spix = _psrctile[(py * _sourcew) + px];
			const RGB &dpix = _pdsttile[(py * tilesize) + px];

			float dr = float(spix.red   - dpix.red) * c_n;
			float dg = float(spix.green - dpix.green) * c_n;
			float db = float(spix.blue  - dpix.blue) * c_n;

			float err = (dr * dr) + (dg * dg) + (db * db);
//			float err = pow(fabs(dr), 0.5) + pow(fabs(dg), 0.5) + pow(fabs(db), 0.5);
//			float err = fabs(dr) + fabs(dg) + fabs(db);

			errsum += err;
		}
	}

	return errsum;
}

// --------------------------------------------------------------------
//	add tile to dictionary if it appears to be unique
// --------------------------------------------------------------------

static int process_tile
(
	RGB *_psourcergb, 
	ubyte_t *_psourcecol, 
	ulong_t _sourcew, 
	int _tx, int _ty
)
{
	int mapindex = -1;

	// compare this tile with all tiles already in dictionary

	// address source tile in terms of tile coords
	RGB *psrctile24 = &_psourcergb[(_sourcew * (_ty * g_allargs.tilesize)) + (_tx * g_allargs.tilesize)];

	// configure nearest match to worst case before search
	tile24dictionary_t::iterator best_it24 = s_tile24dictionary.end();
	tile8dictionary_t::iterator best_it8 = s_tile8dictionary[0].end();
	float best_error = g_allargs.tolerance+(1e-7f);
	int best_index = -1;
	bool match = false;

	// run comparison against all tiles
	tile24dictionary_t::iterator it24 = s_tile24dictionary.begin();
	tile8dictionary_t::iterator it8 = s_tile8dictionary[0].begin();
	int index = 0;
	for (; it24 != s_tile24dictionary.end(); it24++, it8++, index++)
	{
		RGB *pdict24 = *it24;
		float match_error = compare_tile24(pdict24, psrctile24, _sourcew);

		// if this dictionary tile yields a closer match, track it
		if (match_error < best_error)
		{
			match = true;
			best_error = match_error;
			best_it24 = it24;
			best_it8 = it8;
			best_index = index;
		}
	}

	if (match)
	{
		// if match was found, consider it reused
		//		std::cout << "reused tile with match error @ " << best_error << std::endl;
		mapindex = best_index;
	}
	else
	{
		mapindex = (int)s_tile8dictionary[0].size();

		// if no match was found, store tile as unique
		RGB *pnewtile24 = new RGB[64 * 64];
		ubyte_t *pnewtile8 = new ubyte_t[64 * 64];
		ubyte_t *psrctile8 = &_psourcecol[(_sourcew * (_ty * g_allargs.tilesize)) + (_tx * g_allargs.tilesize)];

		extract_tile24(pnewtile24, psrctile24, _sourcew);
		extract_tile8(pnewtile8, psrctile8, _sourcew);

		//		std::cout << "recorded unique tile " << s_tile8dictionary.size() << " @ " << _tx << "x" << _ty << " with match error > " << best_error << std::endl;
		s_tile24dictionary.push_back(pnewtile24);
		s_tile8dictionary[0].push_back(pnewtile8);
	}

	return mapindex;
}

static int process_tile2
(
	RGB *_psourcergb, 
	ubyte_t *_psourcecol0, ubyte_t *_psourcecol1,
	ulong_t _sourcew, 
	int _tx, int _ty
)
{
	int mapindex = -1;

	// compare this tile with all tiles already in dictionary

	// address source tile in terms of tile coords
	RGB *psrctile24 = &_psourcergb[(_sourcew * (_ty * g_allargs.tilesize)) + (_tx * g_allargs.tilesize)];

	// configure nearest match to worst case before search
	tile24dictionary_t::iterator best_it24 = s_tile24dictionary.end();
	//tile8dictionary_t::iterator best_it80 = s_tile8dictionary[0].end();
	//tile8dictionary_t::iterator best_it81 = s_tile8dictionary[1].end();
	float best_error = g_allargs.tolerance+(1e-7f);
	int best_index = -1;
	bool match = false;

	// run comparison against all tiles
	tile24dictionary_t::iterator it24 = s_tile24dictionary.begin();
	//tile8dictionary_t::iterator it80 = s_tile8dictionary[0].begin();
	//tile8dictionary_t::iterator it81 = s_tile8dictionary[1].begin();
	int index = 0;
	for (; it24 != s_tile24dictionary.end(); it24++, /*it80++, it81++,*/ index++)
	{
		RGB *pdict24 = *it24;
		float match_error = compare_tile24(pdict24, psrctile24, _sourcew);

		// if this dictionary tile yields a closer match, track it
		if (match_error < best_error)
		{
			match = true;
			best_error = match_error;
			best_it24 = it24;
			//best_it80 = it80;
			//best_it81 = it81;
			best_index = index;
		}
	}

	if (match)
	{
		// if match was found, consider it reused
		//		std::cout << "reused tile with match error @ " << best_error << std::endl;
		mapindex = best_index;
	}
	else
	{
		mapindex = (int)s_tile24dictionary.size();

		// if no match was found, store tile as unique
		RGB *pnewtile24 = new RGB[64 * 64];

		ubyte_t *pnewtile80 = new ubyte_t[64 * 64];
		ubyte_t *psrctile80 = &_psourcecol0[(_sourcew * (_ty * g_allargs.tilesize)) + (_tx * g_allargs.tilesize)];

		ubyte_t *pnewtile81 = new ubyte_t[64 * 64];
		ubyte_t *psrctile81 = &_psourcecol1[(_sourcew * (_ty * g_allargs.tilesize)) + (_tx * g_allargs.tilesize)];

		extract_tile24(pnewtile24, psrctile24, _sourcew);
		extract_tile8(pnewtile80, psrctile80, _sourcew);
		extract_tile8(pnewtile81, psrctile81, _sourcew);

		//		std::cout << "recorded unique tile " << s_tile8dictionary.size() << " @ " << _tx << "x" << _ty << " with match error > " << best_error << std::endl;
		s_tile24dictionary.push_back(pnewtile24);
		s_tile8dictionary[0].push_back(pnewtile80);
		s_tile8dictionary[1].push_back(pnewtile81);
	}

	return mapindex;
}

// --------------------------------------------------------------------
//	process all map tiles from source image
// --------------------------------------------------------------------

/*
static void process_tilemap
(
	long_t *&r_ptilesrcmap, int &r_tilesrcw, int &r_tilesrch,
	long_t *&r_ptilemap, int &r_tilemapw, int &r_tilemaph, 
	RGB *_psourcergb, ubyte_t *_psourcecol, ulong_t _sourcew, ulong_t _sourceh
)
{
	s_tile24dictionary.clear();
	s_tile8dictionary[0].clear();

	// find source dimensions in terms of tiles
	int tilesrcw = (_sourcew / g_allargs.tilesize);
	int tilesrch = (_sourceh / g_allargs.tilesize);
	r_tilesrcw = tilesrcw;
	r_tilesrch = tilesrch;
	r_ptilesrcmap = new long_t[tilesrcw*tilesrch];

	long_t *psrcmap = r_ptilesrcmap;

	// find map dimensions in terms of tiles
	int mapw = xmin(tilesrcw - g_allargs.maprect_xpos, g_allargs.maprect_xsize);
	int maph = xmin(tilesrch - g_allargs.maprect_ypos, g_allargs.maprect_ysize);
	r_tilemapw = mapw;
	r_tilemaph = maph;
	r_ptilemap = new long_t[mapw*maph];

	long_t *ptilemap = r_ptilemap;

	// iterate over source tiles, left-to-right, top-to-bottom
	for (int ty = 0; ty < tilesrch; ty++)
	{
		for (int tx = 0; tx < tilesrcw; tx++)
		{
			int tilemapindex = process_tile(_psourcergb, _psourcecol, _sourcew, tx, ty);

			// update map only within cutting rectangle
			int mx = tx - g_allargs.maprect_xpos;
			int my = ty - g_allargs.maprect_ypos;
			if ((mx >= 0 && my >= 0) && (mx < g_allargs.maprect_xsize && my < g_allargs.maprect_ysize))
			{
				ptilemap[(my * mapw) + mx] = tilemapindex;
			}
			// update src map always (for --visual)
			psrcmap[(ty*tilesrcw)+tx] = tilemapindex;

			if ((s_progress & 0x1F) == 0)
			{
				switch ((s_progress >> 5) & 3)
				{
					case 0: printf("|\r"); break;
					case 1: printf("/\r"); break;
					case 2: printf("-\r"); break;
					case 3: printf("\\\r"); break;
				}
			}
			s_progress++;
		}
	}

	int uniquetiles = s_tile8dictionary[0].size();
}
*/

static void process_tilemap2
(
	long_t *&r_ptilesrcmap, int &r_tilesrcw, int &r_tilesrch,
	long_t *&r_ptilemap, int &r_tilemapw, int &r_tilemaph, 
	RGB *_psourcergb, 
	ubyte_t *_psourcecol0, ubyte_t *_psourcecol1,
	ulong_t _sourcew, ulong_t _sourceh,
	int _fields
)
{
	// find source dimensions in terms of tiles
	int tilesrcw = (_sourcew / g_allargs.tilesize);
	int tilesrch = (_sourceh / g_allargs.tilesize);
	r_tilesrcw = tilesrcw;
	r_tilesrch = tilesrch;
	r_ptilesrcmap = new long_t[tilesrcw*tilesrch];

	long_t *psrcmap = r_ptilesrcmap;

	// find map dimensions in terms of tiles
	int mapw = xmin(tilesrcw - g_allargs.maprect_xpos, g_allargs.maprect_xsize);
	int maph = xmin(tilesrch - g_allargs.maprect_ypos, g_allargs.maprect_ysize);
	r_tilemapw = mapw;
	r_tilemaph = maph;
	r_ptilemap = new long_t[mapw*maph];

	long_t *ptilemap = r_ptilemap;

	// iterate over source tiles, left-to-right, top-to-bottom
	for (int ty = 0; ty < tilesrch; ty++)
	{
		for (int tx = 0; tx < tilesrcw; tx++)
		{
			int tilemapindex;

			if (_fields == 2)
			{
				tilemapindex = process_tile2
				(
					_psourcergb,
					_psourcecol0, _psourcecol1,
					_sourcew,
					tx, ty
				);
			}
			else
			{
				tilemapindex = process_tile
				(
					_psourcergb,
					_psourcecol0,
					_sourcew,
					tx, ty
				);
			}

			// update map only within cutting rectangle
			int mx = tx - g_allargs.maprect_xpos;
			int my = ty - g_allargs.maprect_ypos;
			if ((mx >= 0 && my >= 0) && (mx < g_allargs.maprect_xsize && my < g_allargs.maprect_ysize))
			{
				ptilemap[(my * mapw) + mx] = tilemapindex;
			}
			// update src map always (for --visual)
			psrcmap[(ty*tilesrcw)+tx] = tilemapindex;

			if ((s_progress & 0x1F) == 0)
			{
				switch ((s_progress >> 5) & 3)
				{
					case 0: printf("|\r"); break;
					case 1: printf("/\r"); break;
					case 2: printf("-\r"); break;
					case 3: printf("\\\r"); break;
				}
			}
			s_progress++;
		}
	}

	int uniquetiles = s_tile24dictionary.size();
}

// --------------------------------------------------------------------
//	convert 8bit tiles to 8-plane for export (later chop to N planes)
// --------------------------------------------------------------------

static void remap_dictionary_tiles(tile8dictionary_t &_in, tileplanardictionary_t &_out)
{
	std::cout << "converting " << _in.size() << " 8-bit tiles into planar format..." << std::endl;

	tile8dictionary_t::iterator it8 = _in.begin();
	for (; it8 != _in.end(); it8++)
	{
		ubyte_t *ptile8 = *it8;
/*
		for (int y = 0; y < 16; y++)
		{
			printf("\n");
			for (int x = 0; x < 16; x++)
			{
				printf("%02x ", ptile8[(y*16)+x]);
			}
		}
*/
		// convert mask colour (if present) to index 128, encoding 8th plane only
		for (int x = 0; x < g_allargs.tilesize * g_allargs.tilesize; x++)
		{
			ubyte_t c = ptile8[x];
			if (g_allargs.keycol == c)
				c = c_tile_mask_keycol;
			ptile8[x] = c;
		}

		// operate on 8 planes for intermediate steps, then save only required planes later
		int planes = 8;
		uword_t *ptileplanar = 0;

		// handle 8x8 a bit differently so we can use word-based c2p on it
		if (g_allargs.tilesize == 8)
		{
			// emit 8x8 to 16x8 buffer for c2p
			int bytes_per_dst_line = planes;

			int bytes_per_tile = g_allargs.tilesize*bytes_per_dst_line;
			ptileplanar = new uword_t[bytes_per_tile>>1];
			memset(ptileplanar, 0, bytes_per_tile);

			// convert chunk 8-bit tiles into 8-planar format...
			c2pb
			(
				/*src=*/ptile8, /*dst*/(ubyte_t*)ptileplanar,
				/*w=*/g_allargs.tilesize, /*h=*/g_allargs.tilesize,
				/*bytes_per_src_line=*/g_allargs.tilesize, /*bytes_per_dst_line=*/bytes_per_dst_line,
				/*planes=*/planes
			);
		}
		else
		{
			// all other sizes go to normal buffer
			int words_per_dst_line = (planes * (g_allargs.tilesize >> 4));

			int words_per_tile = g_allargs.tilesize*words_per_dst_line;
			ptileplanar = new uword_t[words_per_tile];
			memset(ptileplanar, 0, words_per_tile<<1);

			// convert chunk 8-bit tiles into 8-planar format
			c2pw
			(
				/*src=*/ptile8, /*dst*/ptileplanar,
				/*w=*/g_allargs.tilesize, /*h=*/g_allargs.tilesize,
				/*bytes_per_src_line=*/g_allargs.tilesize, /*words_per_dst_line=*/words_per_dst_line,
				/*planes=*/planes
			);
		}

		_out.push_back(ptileplanar);
	}

	std::cout << "...done" << std::endl;
}

// --------------------------------------------------------------------
//	emit .PI1 from 4-plane buffer & palette data
// --------------------------------------------------------------------

static void emit_pi1(std::string outfile, uword_t *buffer4plane, RGB *_ppal24, bool _fromwords, int pi1_index)
{
	// t<N><outfile>.pi1
	std::string fname("t");
	fname += ('0' + pi1_index);
	fname.append(outfile.c_str());
	replace_extension(fname, "pi1");

	FILE *hout = fopen(fname.c_str(), "wb");
	if (hout)
	{
		uword_t palette4bit[16] = { 0x000, 0xfff, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777, 0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee };

		for (int i = 0; i < 16; i++)
		{
			word_t r = word_t(_ppal24[i].red);
			word_t g = word_t(_ppal24[i].green);
			word_t b = word_t(_ppal24[i].blue);

			// reformat 24bit 8:8:8 into STE swizzled 4:4:4 format
			word_t ste_r = (r >> 5) | (((r >> 4) & 1) << 3);
			word_t ste_g = (g >> 5) | (((g >> 4) & 1) << 3);
			word_t ste_b = (b >> 5) | (((b >> 4) & 1) << 3);

			uword_t ste_col = uword_t((ste_r << 8) | (ste_g << 4) | ste_b);

			// reformat to bigendian for intel->motorola
			palette4bit[i] = endianswap16(ste_col);
		}

		// reformat image to bigendian for intel->motorola
		if (_fromwords)
			for (int i = 0; i < 16000; i++)
			{
				buffer4plane[i] = endianswap16(buffer4plane[i]);
			}

		// write header
		unsigned short v = 0;
		fwrite(&v, 1, 2, hout);

		// write palette
		fwrite(palette4bit, 1, 32, hout);

		// write buffer
		fwrite(buffer4plane, 1, 32000, hout);
		fclose(hout);

		std::cout << "emitted tilegroup [" << fname.c_str() << "]" << std::endl;
	}
}

// --------------------------------------------------------------------
//	colour reduction (fixed palette)
// --------------------------------------------------------------------

static ubyte_t s_colours_matched[256];
int s_num_colours_matched = 0;

static bool s_reducer_keycol_exact = true;

static inline ubyte_t match_colour(const RGB &rgb, RGB *_ppal, int _ncols)
{
	static const float c_n = 1.0f / 255.0f;

	float fr = (float(rgb.red) * g_allargs.colrenorm) / 255.0f;
	float fg = (float(rgb.green) * g_allargs.colrenorm) / 255.0f;
	float fb = (float(rgb.blue) * g_allargs.colrenorm) / 255.0f;

	//if (fr > 250.0f && fg > 250.0f && fb > 250.0f)
	//{
	//	__asm int 3;
	//}

	// default to keycol
	int best_hit = g_allargs.keycol;

	u8 kr = g_allargs.keyrgb.red;
	u8 kg = g_allargs.keyrgb.green;
	u8 kb = g_allargs.keyrgb.blue;

	if ((rgb.red == kr) &&
		(rgb.green == kg) &&
		(rgb.blue == kb))
	{
		// map keycol, even if it's outside indexed colour range (it won't be emitted as graphics anyway)
	}
	else
	{
		float best_err = 1e7;
		for (int i = 0; i < _ncols; i++)
		{
			const RGB &pal = _ppal[i];

			float pr = float(pal.red /*+ 8*/);
			float pg = float(pal.green /*+ 8*/);
			float pb = float(pal.blue /*+ 8*/);

			// YUV error - meh
			//float dr = (fr - pr) * c_n * 0.299;
			//float dg = (fg - pg) * c_n * 0.587;
			//float db = (fb - pb) * c_n * 0.114;

			float dr = (fr - pr) * c_n;
			float dg = (fg - pg) * c_n;
			float db = (fb - pb) * c_n;

			float err = (dr * dr) + (dg * dg) + (db * db);

			// in exact mode, don't remap anything to keycol index unless its an exact colour match
			if (!g_allargs.keyguard ||
				(i != g_allargs.keycol) ||
				(err < g_allargs.keytol))
			{
				if (err < best_err)
				{
					best_err = err;
					best_hit = i;
				}
			}
		}
	}

	if (s_colours_matched[best_hit] == 0)
	{
		s_colours_matched[best_hit] = 1;
		s_num_colours_matched++;
	}

	return (ubyte_t)best_hit;
}

// --------------------------------------------------------------------
//	reduce TC image to indexed colour using fixed palette
// --------------------------------------------------------------------

void review_palettes(RGB *pspal, RGB *pdpal, int _sourced)
{
	memset(s_colours_matched, 0, 256);
	if (_sourced == 0)
		_sourced = 24;

	int ncols = 1 << _sourced;

	for (int c = 0; c < (1 << _sourced); c++)
	{
		if (pspal && pdpal && (c < 16))
		{
			int m = match_colour(pspal[c], pdpal, ncols);

			// index-index remapping case
			printf("src: [%03d] $%02x:$%02x:$%02x  mapto: [%02d] $%02x:$%02x:$%02x  target: [%02d] $%02x:$%02x:$%02x\n",
				c,
				(int)pspal[c].red, (int)pspal[c].green, (int)pspal[c].blue,
				m,
				(int)pdpal[m].red, (int)pdpal[m].green, (int)pdpal[m].blue,
				c,
				(int)pdpal[c].red, (int)pdpal[c].green, (int)pdpal[c].blue
				);
		}
		else
		if (pdpal && (c < 16))
		{
			// 24bit reduction case
			printf("src: target: [%02d] $%02x:$%02x:$%02x\n",
				c,
				(int)pdpal[c].red, (int)pdpal[c].green, (int)pdpal[c].blue
				);
		}
		else
		if (pspal)
		{
			// direct index translation case
			printf("src: [%03d] $%02x:$%02x:$%02x\n",
				c,
				(int)pspal[c].red, (int)pspal[c].green, (int)pspal[c].blue
				);
		}
	}
}

static bool near_alpha(const RGB &rgb)
{
	static const float c_n = 1.0f / 255.0f;

	if (!g_allargs.alphargb_specified)
		return false;

	u8 kr = g_allargs.alphargb.red;
	u8 kg = g_allargs.alphargb.green;
	u8 kb = g_allargs.alphargb.blue;

	float akr = float(kr);
	float akg = float(kg);
	float akb = float(kb);

	float pr = float(rgb.red);
	float pg = float(rgb.green);
	float pb = float(rgb.blue);

	float dr = (akr - pr) * c_n;
	float dg = (akg - pg) * c_n;
	float db = (akb - pb) * c_n;

	float err = (dr * dr) + (dg * dg) + (db * db);

	return (err <= g_allargs.alphatol);
}

static bool near_alpha(int i, RGB *_ppal)
{
	const RGB &rgb = _ppal[i];

	return near_alpha(rgb);
}


// if matched colour is within alphakeytol of alphargb, map to keycol according to (x^y)&1

static int alpha_dodge(int i, int x, int y, int field, RGB *_ppal)
{
	if (near_alpha(i, _ppal))
	{
		int v = ((x^y)^field) & 1;
		if (v) 
			i = g_allargs.keycol;
	}

	return i;
}

static void reduce_image24(RGB *_psourcergb, ubyte_t *_psrccol, ubyte_t *_pdstcol0, ubyte_t *_pdstcol1, ulong_t _sourcew, ulong_t _sourceh, ulong_t _sourced, RGB *&r_ppal)
{
	unsigned short r_numcolours = 0;
	unsigned short r_numuniquecolours = 0;
	bool success = false;

	RGB* ppal = 0;

	// Degas as container for a palette
	if (g_allargs.palettefile.find(".pi1") != std::string::npos)
		success = read_pal_pi1(g_allargs.palettefile.c_str(), ppal, r_numcolours, r_numuniquecolours);
	else
	// RIFF format palettes
	if (g_allargs.palettefile.find(".pal") != std::string::npos)
		success = read_pal24(g_allargs.palettefile.c_str(), ppal, r_numcolours, r_numuniquecolours);
	else
	// PhotoChrome-generated .cct tile libraries carry the palette as the first part of the header
	if (g_allargs.palettefile.find(".cct") != std::string::npos)
		success = read_pal_cct(g_allargs.palettefile.c_str(), ppal, r_numcolours, r_numuniquecolours);
	else
	// a lone .ccp is raw palette data, same as header for .cct, so just reuse that reader for now
	if (g_allargs.palettefile.find(".ccp") != std::string::npos)
		success = read_pal_cct(g_allargs.palettefile.c_str(), ppal, r_numcolours, r_numuniquecolours);
	else
	{
		// The .ccs format infers the existence of: .ccs (solutionpairs) + .ccr (RGB->solutionpair reduction map) + .ccp (palette)
		// which all get emitted together from PhotoChrome v6 when using '-ccmode 3' to generate palettes
		// This format also enables a different remapping method in order to apply complementary field dithering.
		if (g_allargs.palettefile.find(".ccs") != std::string::npos)
		{
			success = read_pal_ccs(g_allargs.palettefile.c_str(), ppal, r_numcolours, r_numuniquecolours);
			g_allargs.use_solutionpair_reduction_ = true;
		}
		else
		{
			std::cout << " unrecognized palette format [" << g_allargs.palettefile.c_str() << "] so trying image handlers..." << std::endl;
			success = palette_load(g_allargs.palettefile.c_str(), ppal, r_numcolours, r_numuniquecolours);
			if (!success)
				std::cerr << "warning: this image format can't be used to supply palettes currently" << std::endl;
		}
	}

	if (success)
	{
		// review palettes if source image is indexed
		if ((_sourced > 0) && (_sourced < 16) && 
			!g_allargs.quickcut && g_allargs.verbose)
		{
			std::cout << "summary of source->desintation palettes for indexed source image being remapped" << std::endl;
			review_palettes(r_ppal, ppal, _sourced);
		}

		// expand palette up to 256 indices for safe indexing with 'dead' colours (e.g. keycol)


		RGB *extpal = new RGB[256];
		memset(extpal, 0, sizeof(RGB)*256);
		memcpy(extpal, ppal, sizeof(RGB)*r_numcolours);

		// adopt extended palette
		delete[] ppal;
		r_ppal = ppal = extpal;

		std::cout << " size of palette: " << r_numcolours << std::endl;
		std::cout << " unique 24bit colours available in palette: " << r_numuniquecolours << std::endl;
		if (r_numuniquecolours > (1 << g_allargs.planes))
			std::cout << " warning: target palette contains more colours than " << g_allargs.planes << " bitplanes can encode!" << std::endl;

		int num_unique = 16;

		if (g_allargs.use_solutionpair_reduction_)
		{
			// use PhotoChrome-generated complementary solution pairs for extra win!
			// todo: assume single-field for now, add dual-field later
//			int field = 0;

			{
				RGB *psrc = _psourcergb;
				ubyte_t *pdst0 = _pdstcol0;
				ubyte_t *pdst1 = _pdstcol1;
				for (int py = 0; py < (int)_sourceh; py++)
				{
					for (int px = 0; px < (int)_sourcew; px++)
					{
						RGB &rgb = *psrc++;

						u8 r8 = rgb.red;
						u8 g8 = rgb.green;
						u8 b8 = rgb.blue;

						u8 solution0 = g_allargs.keycol;
						u8 solution1 = g_allargs.keycol;

						if ((r8 == g_allargs.keyrgb.red) &&
							(g8 == g_allargs.keyrgb.green) &&
							(b8 == g_allargs.keyrgb.blue))
						{
							// default to keycol
						}
						else
						{
							// otherwise use the solution pair (usually a dither)
							int rgb666 = ((b8 >> 2) << 12) | ((g8 >> 2) << 6) | (r8 >> 2);

							u8 solution = g_rgb_solutionpair_map[rgb666];
							u8 solutionpair = g_solutionpairs[solution];

							solution0 = (solutionpair >> 4) & 0xF;
							solution1 = (solutionpair >> 0) & 0xF;
						}

						if (g_allargs.ccremap == CCRemap_LOW)
						{
							// primary field gets low intensity component, secondary field gets high intensity
							*pdst0++ = solution0;
							if (pdst1)
								*pdst1++ = solution1;
						}
						else
						if (g_allargs.ccremap == CCRemap_HIGH)
						{
							// primary field gets high intensity component, secondary field gets low intensity
							*pdst0++ = solution1;
							if (pdst1)
								*pdst1++ = solution0;
						}
						else
						if (g_allargs.ccremap == CCRemap_MATCH)
						{
							// both fields share closest match
							ubyte_t m = match_colour(rgb, extpal, r_numcolours);

							*pdst0++ = m;
							if (pdst1)
								*pdst1++ = m;
						}
						else
						{
							// CCRemap_BOTH
							int field = 0;
						
/*							if ((g_allargs.ccfields == 1) &&
								g_allargs.alphargb_specified)
							{
								// special case: spread dual-field over 2 distinct sprite frames, instead of 2 colour fields within 1 frame
								// so we keep the fields separate, allowing the sprite cutter to do the dithering itself since it knows the frame index.

								if (near_alpha(rgb))
								{
									// both fields share a nearest match, since alpha dither will be used on this colour!
									ubyte_t m = match_colour(rgb, ppal, r_numcolours);

									*pdst0++ = m;
									if (pdst1)
										*pdst1++ = m;
								}
								else
								{
									// primary field gets low intensity component, secondary field gets high intensity
									*pdst0++ = solution0;
									if (pdst1)
										*pdst1++ = solution1;
								}
							}
							else
*/							{
								if (near_alpha(rgb))
								{
									// both fields share a nearest match, since alpha dither will be used on this colour!
									ubyte_t m = match_colour(rgb, extpal, r_numcolours);

									*pdst0++ = m;
									if (pdst1)
										*pdst1++ = m;
								}
								else
								{
									// both fields get alternating, complementary pattern of high/low
									*pdst0++ = (((px ^ py) ^ field ^ 0) & 1) ? solution0 : solution1;
									if (pdst1)
										*pdst1++ = (((px ^ py) ^ field ^ 1) & 1) ? solution0 : solution1;
								}
							}
						}
					}
				}
			}
		}
		else
		{
			// use average-joe palette, with mundane nearest-match thingy. zzzzz...

			memset(s_colours_matched, 0, 256);
			num_unique = 0;
			{
				char unique[256];
				memset(unique, 0, 256);
				RGB *psrc = _psourcergb;
				ubyte_t *pdst = _pdstcol0;
				for (int py = 0; py < (int)_sourceh; py++)
				{
					for (int px = 0; px < (int)_sourcew; px++)
					{
						ubyte_t m = match_colour(*psrc++, r_ppal, r_numcolours);

						*pdst++ = m;
						if (unique[m] == 0)
						{
							unique[m] = 1;
							num_unique++;
						}
					}
				}
			}
		}

		std::cout << " indexed colours used in final image: " << num_unique << std::endl;

		if (((_sourced == 0) || (_sourced >= 16)) && 
			!g_allargs.quickcut && !g_allargs.quiet)
		{
			std::cout << "summary of destination palette for " << ((_sourced > 0) ? _sourced : 24) << "-bit source image being remapped" << std::endl;
			review_palettes(NULL, r_ppal, _sourced);
		}

	}
	else
	{
		std::cerr << "error: could not read input palette from palette file or image" << std::endl;
		std::cout << "press any key..." << std::endl << std::flush;
		getchar();	
		exit(1);
	}
	std::cout << "...done" << std::endl;
}

// --------------------------------------------------------------------
//	emit map data file indicating where tiles go in original image
// --------------------------------------------------------------------

static void emit_tilemap_ccm(std::string outfile, long_t *_ptilemap, int _tilemapw, int _tilemaph, int _tilestep)
{
	long_t v;
	word_t vw;
	byte_t vb;

	replace_extension(outfile, "ccm");

	int mapentries = _tilemapw * _tilemaph;
	long_t *be_tilemap = new long_t[mapentries];
//	memcpy(be_tilemap, _ptilemap, mapentries * sizeof(long_t));

	for (int i = 0; i < mapentries; i++)
		be_tilemap[i] = endianswap32(_ptilemap[i]);

	FILE *hmap = fopen(outfile.c_str(), "wb");

	// flags
	// interpretation:
	// %00001000
	//  1=overlay
	//  0=normal
	vb = 0;
	vb |= g_allargs.translayer ? 8 : 0;
	fwrite(&vb, 1, sizeof(vb), hmap);
	// tilestep
	vb = (byte_t)_tilestep;
	fwrite(&vb, 1, sizeof(vb), hmap);
	// width
	vw = endianswap16((s16)_tilemapw);
	fwrite(&vw, 1, sizeof(vw), hmap);
	// unused
	vw = 0;
	fwrite(&vw, 1, sizeof(vw), hmap);
	// height
	vw = endianswap16((s16)_tilemaph);
	fwrite(&vw, 1, sizeof(vw), hmap);

	fwrite(be_tilemap, sizeof(long_t), mapentries, hmap);

	fclose(hmap);

	delete[] be_tilemap;

	std::cout << "emitted tilemap [" << outfile.c_str() << "]" << std::endl;
}

// --------------------------------------------------------------------
//	emit tile library as sequence of Degas .PI1 images
// --------------------------------------------------------------------

// emit tiles to a series of 320x200 .PI1 images
static void emit_tiles_degas(std::string outfile, RGB *_ppalettergb, bool _mask, int _ccf)
{
	uword_t *buffer4plane = new uword_t[16000];
	memset(buffer4plane, 0, 32000);

	int pi1_index = 0;
	int otx = 0;
	int oty = 0;
	// emit PI1 images representing dictionary
	//while (!s_tileplanardictionary.empty())
	for (tileplanardictionary_t::iterator it = s_tileplanardictionary[_ccf].begin(); 
		it != s_tileplanardictionary[_ccf].end(); it++)
	{
		// copy 4 least significant planes to PI1 buffer
		uword_t *pin = *it;// s_tileplanardictionary.front();
		// find output tile address in 4-plane output buffer
		uword_t *pout = &buffer4plane[((otx & -16)>>(4-2)) + (oty * (320>>(4-2)))];

		// special handling of 8x8 tiles. note this produces motorola/bigendian ordering. other sizes produce intel words.
		if (g_allargs.tilesize == 8)
		{
			ubyte_t *pinb = (ubyte_t*)pin;
			for (int ty = 0; ty < g_allargs.tilesize; ty++)
			{
				ubyte_t *poutb = (ubyte_t *)pout;
				{
					if (otx & 8)
					{
						if (_mask)
						{
							// record mask only
							poutb[0 + 1] = pinb[7];
							poutb[2 + 1] = pinb[7];
							poutb[4 + 1] = pinb[7];
							poutb[6 + 1] = pinb[7];
							pinb += 4;
						}
						else
						{

							// record 4 planes
							poutb[0+1] = *pinb++;
							poutb[2+1] = *pinb++;
							poutb[4+1] = *pinb++;
							poutb[6+1] = *pinb++;
						}
					}
					else
					{
						if (_mask)
						{
							// record mask only
							poutb[0 + 0] = pinb[7];
							poutb[2 + 0] = pinb[7];
							poutb[4 + 0] = pinb[7];
							poutb[6 + 0] = pinb[7];
							pinb += 4;
						}
						else
						{
							// record 4 planes
							poutb[0+0] = *pinb++;
							poutb[2+0] = *pinb++;
							poutb[4+0] = *pinb++;
							poutb[6+0] = *pinb++;
						}
					}
					// skip 4 planes of 8-plane tile
					pinb += 4;
				}
				// next line of PI1
				pout += (320 >> (4-2));
			}
		}
		else
		{
			for (int ty = 0; ty < g_allargs.tilesize; ty++)
			{
				uword_t *poutw = pout;
				for (int tx = 0; tx < (g_allargs.tilesize >> 4); tx++)
				{
					if (_mask)
					{
						*poutw++ = pin[7];
						*poutw++ = pin[7];
						*poutw++ = pin[7];
						*poutw++ = pin[7];
						pin += 8;
					}
					else
					{
						// record 4 planes
						*poutw++ = *pin++;
						*poutw++ = *pin++;
						*poutw++ = *pin++;
						*poutw++ = *pin++;
						// skip 4 planes of 8-plane tile
						pin += 4;
					}
				}
				// next line of PI1
				pout += (320 >> (4-2));
			}
		}

		otx += g_allargs.tilesize;
		if ((otx + g_allargs.tilesize) > 320)
		{
			otx = 0;
			oty += g_allargs.tilesize;
			if ((oty + g_allargs.tilesize) > 200)
			{
				oty = 0;

				// flush image
				emit_pi1(outfile, buffer4plane, _ppalettergb, /*fromwords=*/(g_allargs.tilesize!=8), pi1_index++);
				memset(buffer4plane, 0, 32000);
			}
		}

		//s_tileplanardictionary.pop_front();
	}

	// if some tiles are still pending, flush remainder
	if (otx > 0 || oty > 0)
		emit_pi1(outfile, buffer4plane, _ppalettergb, /*fromwords=*/(g_allargs.tilesize!=8), pi1_index++);

	delete[] buffer4plane;
}

// --------------------------------------------------------------------
//	emit tile library data file
// --------------------------------------------------------------------


static void emit_tiles_cct(std::string outfile, RGB *_ppal24, int _ccf)
{
//	std::string filename("tiles.cct");

	// construct output filename: [infile.ccm]

	//std::string outfile;
	//if (g_allargs.outfile.length() > 0)
	//{
	//	outfile = g_allargs.outfile;
	//}
	//else
	//{
	//	outfile = g_allargs.sourcefile;
	//}

	replace_extension(outfile, "cct");

	FILE* h = fopen(outfile.c_str(), "wb+");
	if (h)
	{
		// write filesize
		//{
		//	ulong_t datasize = 0;
		//	fwrite(&datasize, 1, sizeof(datasize), h);
		//}

		// write CRC
		{
			ulong_t CRC = 0;
			fwrite(&CRC, 1, sizeof(CRC), h);
		}

		// write palette (16*2)
		{
			uword_t palette4bit[16];

			for (int i = 0; i < 16; i++)
			{
				word_t r = word_t(_ppal24[i].red);
				word_t g = word_t(_ppal24[i].green);
				word_t b = word_t(_ppal24[i].blue);

				// reformat 24bit 8:8:8 into STE swizzled 4:4:4 format
				word_t ste_r = (r >> 5) | (((r >> 4) & 1) << 3);
				word_t ste_g = (g >> 5) | (((g >> 4) & 1) << 3);
				word_t ste_b = (b >> 5) | (((b >> 4) & 1) << 3);

				uword_t ste_col = uword_t((ste_r << 8) | (ste_g << 4) | ste_b);

				// reformat to bigendian for intel->motorola
				palette4bit[i] = endianswap16(ste_col);
			}

			fwrite(palette4bit, 1, sizeof(palette4bit), h);
		}

		// only convert to nibbles if this is 4-plane, base layer
		bool nibble_formatted = (g_allargs.planes == 4) && (!g_allargs.translayer);

		// write flags (1)
		{
			// interpretation:
			// %00000111
			//  0=16x16 (legacy)
			//  3=8x8
			//  4=16x16
			//  5=32x32
			// %00001000
			//  1=overlay
			//  0=normal
			// %00010000
			//  1=nibble-formatted
			//  0=planes

			ubyte_t flags = 0;
			flags |= bceil<ubyte_t>((ubyte_t)g_allargs.tilesize);
			flags |= g_allargs.translayer ? (1<<3) : 0;
			flags |= nibble_formatted ? (1<<4) : 0;	// encode as nibbles only in 4pln mode
			fwrite(&flags, 1, sizeof(flags), h);
		}

		// write #bitplanes (1)
		{
			ubyte_t count = g_allargs.planes;
			fwrite(&count, 1, sizeof(count), h);
		}

		// write tile count (2)
		{
			word_t count = s_tileplanardictionary[_ccf].size();
			count = endianswap16(count);
			fwrite(&count, 1, sizeof(count), h);
		}

		// write tiles (n)
		{
//			uword_t tilespace[32*(8*(32>>4))]; // worst: 32x32x8pl(*2mask)
			uword_t tilespace[(32*32*8*2)>>3]; // worst: 32x32x8pl(*2mask)

			int tileplanes = g_allargs.planes;


			for (tileplanardictionary_t::iterator it = s_tileplanardictionary[_ccf].begin(); it != s_tileplanardictionary[_ccf].end(); it++)
			{

				// extract planes
				if (g_allargs.tilesize == 8)
				{
					// 8x8 mode encoded differently for movep
					int tilebytes = tileplanes * g_allargs.tilesize * (g_allargs.tilesize>>3);
					if (g_allargs.translayer)
						tilebytes <<= 1;

					uword_t *tilew = *it;
					ubyte_t *tileb = (ubyte_t *)tilew;
					ubyte_t *ptilespaceb = (ubyte_t *)tilespace;
					for (int y = 0; y < 8; y++)
					{
						ubyte_t *ptileoutb = ptilespaceb;
						ubyte_t *tilesrc = tileb;
						for (int p = 0; p < tileplanes; p++)
							*ptilespaceb++ = *tilesrc++;
						
						// convert 4-plane x 8 pixels to nibbles n0...n7
						if (nibble_formatted)
							rotate_p42n(ptileoutb);

						if (g_allargs.translayer)
						{
							// emit exactly 4 bytes of mask (and.l)
							for (int p = 0; p < 4; p++)
								*ptilespaceb++ = tileb[7];
						}

						tileb += 8;
					}

					fwrite(tilespace, 1, tilebytes, h);
				}
				else
				if (g_allargs.tilesize == 16)
				{
					// 16x16
					int tilewords = tileplanes * g_allargs.tilesize * (g_allargs.tilesize>>4);
					if (g_allargs.translayer)
						tilewords <<= 1;

					uword_t *tile = *it;
					uword_t *ptilespace = tilespace;
					for (int y = 0; y < 16; y++)
					{
						if (g_allargs.translayer)
						{
							// emit exactly 2 words of mask (and.l)
							for (int p = 0; p < 2; p++)
								*ptilespace++ = tile[7];
						}

						uword_t *ptileout = ptilespace;
						uword_t *tilesrc = tile;
						for (int p = 0; p < tileplanes; p++)
							*ptilespace++ = *tilesrc++;

						// convert 4-plane x 16 pixels to nibbles n0...n15
						if (nibble_formatted)
							rotate_p42n</*BE=*/false>(ptileout);

						tile += 8;
					}

					// reformat image to bigendian for intel->motorola
					for (int i = 0; i < tilewords; i++)
						tilespace[i] = endianswap16(tilespace[i]);

					fwrite(tilespace, 1, (tilewords * 2), h);
				}
				else
				{
					std::cerr << "error: direct mode supports 16x16 or 8x8 tiles only!" << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();	
					exit(1);
				}
			}
		}

		// last, generate CRC
		update_file_crc(h, /*crc_offset=*/0);

		fclose(h);

		std::cout << "emitted tiles [" << outfile.c_str() << "]" << std::endl;
	}
}

// --------------------------------------------------------------------
//	emit map data file indicating where tiles go in original image
// --------------------------------------------------------------------

static void emit_tilemap_16bitraw(std::string outfile, long_t *_ptilemap, int _tilemapw, int _tilemaph)
{
	long_t v;

	replace_extension(outfile, "bin");

	FILE *hmap = fopen(outfile.c_str(), "wb");
	if (hmap)
	{
		int mapentries = _tilemapw * _tilemaph;
		for (int i = 0; i < mapentries; i++)
		{
			uword_t tile_index_mot = endianswap16(uword_t(_ptilemap[i]));
			fwrite(&tile_index_mot, 1, sizeof(tile_index_mot), hmap);
		}

		fclose(hmap);
	}

	std::cout << "emitted tilemap [" << outfile.c_str() << "]" << std::endl;
}

// --------------------------------------------------------------------
//	emit tile library data file for RBasic+ (Jaguar)
// --------------------------------------------------------------------

#pragma pack(push,2) // it's a file format :-/
struct bmp_header
{
	// file
	uword_t	file_sig;
	ulong_t	file_size;
	uword_t	file_rsv1;
	uword_t	file_rsv2;
	ulong_t file_offset_pixeldata;
	// dib
	ulong_t dib_size_header;
	ulong_t dib_w;
	ulong_t dib_signed_h;
	uword_t dib_planes;
	uword_t dib_bpp;
	ulong_t dib_format;
	ulong_t dib_rawsize;
	ulong_t dib_print_xres;
	ulong_t dib_print_yres;
	ulong_t dib_ncolours;
	ulong_t dib_icolours;
	// pixeldata
};
#pragma pack(pop)

static void emit_tiles_rbplus(std::string outfile, RGB *_ppal24)
{
	// construct output filename: [infile.ccm]
	//std::string outfile;
	//if (g_allargs.outfile.length() > 0)
	//{
	//	outfile = g_allargs.outfile;
	//}
	//else
	//{
	//	outfile = g_allargs.sourcefile;
	//}

	replace_extension(outfile, "bmp");

	FILE* h = fopen(outfile.c_str(), "wb");
	if (h)
	{
		int tracked_filesize = 0;
		ulong_t zero = 0;

		int tilesize = g_allargs.tilesize;
		// create header (16 x (tiles*count) x nBPP)

		int pixeldatasize = (tilesize * tilesize) * s_tile8dictionary[0].size();
		if (g_allargs.planes <= 4)
			pixeldatasize >>= 1;

		bmp_header bmph;
		bmph.file_sig = 'MB';
		bmph.file_size = 0; // patch later
		bmph.file_rsv1 = 0;
		bmph.file_rsv2 = 0;
		bmph.file_offset_pixeldata = 0; // patch later
		bmph.dib_size_header = 40;
		bmph.dib_w = tilesize;
#if (BMP_TYPICAL_ROW_ORDER)
		bmph.dib_signed_h = (tilesize * s_tile8dictionary[0].size());
#else
		bmph.dib_signed_h = -(tilesize * s_tile8dictionary[0].size());
#endif
		bmph.dib_planes = 1;
		bmph.dib_bpp = (g_allargs.planes <= 4) ? 4 : 8;
		bmph.dib_format = 0;
		bmph.dib_rawsize = pixeldatasize;
		bmph.dib_print_xres = tilesize;
		bmph.dib_print_yres = (tilesize * s_tile8dictionary[0].size());
		bmph.dib_ncolours = (g_allargs.planes <= 4) ? 16 : 256;
		bmph.dib_icolours = bmph.dib_ncolours;
		// palette follows - no padding required
		// image pixels follow - with padding

		// write header
		fwrite(&bmph, 1, sizeof(bmph), h); tracked_filesize += sizeof(bmph);

		// write palette (16*BGRA)
		{
			for (int i = 0; i < bmph.dib_ncolours; i++)
			{
				ubyte_t r = ubyte_t(_ppal24[i].red);
				ubyte_t g = ubyte_t(_ppal24[i].green);
				ubyte_t b = ubyte_t(_ppal24[i].blue);

				fwrite(&b, 1, 1, h);
				fwrite(&g, 1, 1, h);
				fwrite(&r, 1, 1, h);
				fwrite(&zero, 1, 1, h);

				tracked_filesize += 4;
			}
		}

		// align the pixeldata
		int aligned_filepos = (tracked_filesize + 3) & ~3;
		int padding = aligned_filepos - tracked_filesize;
		tracked_filesize += padding;
		while (padding--)
		{
			fwrite(&zero, 1, 1, h);
		}

		int offset_pixeldata = tracked_filesize;

		// write tiles (n)
		{
			int tileplanes = g_allargs.planes;
			int tilepixels = tilesize * tilesize;
			ubyte_t tilespace[64];	// expected worst case is 32

#if (BMP_TYPICAL_ROW_ORDER)
			// so we need to emit the tiles in reverse. what a crock...
			for (tile8dictionary_t::reverse_iterator it = s_tile8dictionary[0].rbegin(); it != s_tile8dictionary[0].rend(); it++)
#else
			for (tile8dictionary_t::iterator it = s_tile8dictionary[0].begin(); it != s_tile8dictionary[0].end(); it++)
#endif
			{
				ubyte_t *tile = *it;

#if (BMP_TYPICAL_ROW_ORDER)
				for (int r = tilesize-1; r >= 0; r--)
#else
				for (int r = 0; r < tilesize; r++)
#endif
				{
					// access row
					ubyte_t *tilerow = &tile[r * tilesize];

					// emit a row at a time, so we can BMP-flip the row order :(

					// extract pixels
					if (tileplanes <= 4)
					{
						// pack as nibbles
						for (int a = 0, p = 0; a < (tilesize >> 1); ++a, p += 2)
						{
							ubyte_t pixel = (tilerow[p + 0] << 4) | (tilerow[p + 1] << 0);
							tilespace[a] = pixel;
						}

						fwrite(tilespace, 1, tilesize >> 1, h); tracked_filesize += tilesize >> 1;
					}
					else
					{
						// already in correct 8bit chunky format
						fwrite(tilerow, 1, tilesize, h); tracked_filesize += tilesize;
					}
				}
			}
		}

		// patch filesize
		fseek(h, 2, 0);
		fwrite(&tracked_filesize, 1, 4, h);

		// patch pixeldata offset
		fseek(h, 10, 0);
		fwrite(&offset_pixeldata, 1, 4, h);

		std::cout << "emitted tiles [" << outfile.c_str() << "]" << std::endl;

		fclose(h);
	}
}

// --------------------------------------------------------------------
//	map & tile cutting mode
// --------------------------------------------------------------------

bool stitch_sources
(
	RGB *&m_psourcergb,
	RGB *&m_ppalettergb,
	ubyte_t *&m_psourcecol,
	ulong_t &sourcew,
	ulong_t &sourceh,
	ulong_t &sourced
)
{
	// pull in multiple source images, generate single joined image in memory
	// todo: ideally we clean up the old images after join \o/

	// todo: this is crap code - try harder later
	static const int MAX_STITCH_SOURCES = 64;

	RGB *t_psourcergb[MAX_STITCH_SOURCES];;
	ubyte_t *t_psourcecol[MAX_STITCH_SOURCES];
	RGB *t_ppalettergb[MAX_STITCH_SOURCES];
	ulong_t t_sourcew[MAX_STITCH_SOURCES];
	ulong_t t_sourceh[MAX_STITCH_SOURCES];
	ulong_t t_sourced[MAX_STITCH_SOURCES];

	bool success = true;
	int sidx = 0;
	for (std::vector<std::string>::iterator it = g_allargs.sources.begin(); (it != g_allargs.sources.end()) && success; it++, sidx++)
	{
		// load images as separate objects

		t_psourcergb[sidx] = 0;
		t_psourcecol[sidx] = 0;
		t_ppalettergb[sidx] = 0;
		t_sourcew[sidx] = 0;
		t_sourceh[sidx] = 0;
		t_sourced[sidx] = 0;

		std::string source = *it;

		success &= image_load
			(
			source.c_str(),
			t_psourcergb[sidx],
			t_psourcecol[sidx],
			t_ppalettergb[sidx],
			t_sourcew[sidx],
			t_sourceh[sidx],
			t_sourced[sidx]
			);
		if (success)
			std::cout << "read stitching source [" << g_allargs.sourcefile.c_str() << "] with colour depth " << t_sourced[sidx] << " and dimensions " << t_sourcew[sidx] << " x " << t_sourceh[sidx] << std::endl;

		// source consistency check, in case someone saves a file with the wrong width, depth etc.
		if ((sidx > 0) &&
			((t_sourcew[sidx] != t_sourcew[sidx - 1]) ||
			(t_sourced[sidx] != t_sourced[sidx - 1]) ||
			(!!(t_psourcecol[sidx]) != !!(t_psourcecol[sidx - 1])) ||
			(!!(t_ppalettergb[sidx]) != !!(t_ppalettergb[sidx - 1]))
			)
			)
		{
			std::cerr << "error: multiple source files are inconsistent in width or colour depth!" << std::endl;
			std::cout << "press any key..." << std::endl << std::flush;
			getchar();
			exit(1);
		}

		// additional palette consistency check!
		if ((sidx > 0) &&
			(t_ppalettergb[0]) &&
			(t_sourced[0] > 0) &&
			(t_sourced[0] <= 8)
			)
		{
			int ncols = 1 << t_sourced[0];
			bool different = false;

			RGB *cmpa = t_ppalettergb[sidx];
			RGB *cmpb = t_ppalettergb[sidx - 1];

			for (int c = 0; (c < ncols) & !different; c++)
			{
				if ((cmpa[c].red != cmpb[c].red) ||
					(cmpa[c].green != cmpb[c].green) ||
					(cmpa[c].blue != cmpb[c].blue))
					different = true;
			}

			if (different)
			{
				std::cerr << "error: multiple source files are inconsistent in palette values!" << std::endl;
				std::cout << "press any key..." << std::endl << std::flush;
				getchar();
				exit(1);
			}
		}

		// sum height for joined image
		sourceh += t_sourceh[sidx];
	}

	if (success)
	{
		std::cout << "read multiple source images for stitching" << std::endl;

		// join the images together

		sourcew = t_sourcew[0];
		sourced = t_sourced[0];

		// copy palette from first image
		if (t_ppalettergb[0])
		{
			int ncols = 1 << t_sourced[0];
			m_ppalettergb = new RGB[256];
			memset(m_ppalettergb, 0, sizeof(RGB) * 256);
			memcpy(m_ppalettergb, t_ppalettergb[0], sizeof(RGB) * ncols);
		}

		// create space for joined colourmap, if present
		if (t_psourcecol[0])
			m_psourcecol = new ubyte_t[sourcew*sourceh];

		// create space for raw RGB data
		if (t_psourcergb[0])
			m_psourcergb = new RGB[sourcew*sourceh];

		// transfer pixels to joined image
		for (int i = 0, yo = 0; i < sidx; i++)
		{
			int ytilerow = yo >> bceil(g_allargs.tilesize);
			std::cout << "joining source [" << i << "] starting @ tile row [" << ytilerow << "]" << std::endl;

			// note: certain pixel buffers are optional so check them before copying data
			ubyte_t *scol = t_psourcecol[i];
			RGB *srgb = t_psourcergb[i];
			for (int sy = 0; sy < t_sourceh[i]; sy++)
			{
				for (int sx = 0; sx < sourcew; sx++)
				{
					if (m_psourcecol)
						m_psourcecol[sx + ((sy + yo) * sourcew)] = scol[sx + (sy * sourcew)];
					if (m_psourcergb)
						m_psourcergb[sx + ((sy + yo) * sourcew)] = srgb[sx + (sy * sourcew)];
				}
			}

			// advance output vertically
			yo += t_sourceh[i];
		}

		std::cout << "joined " << sidx << " images into single map for extraction" << std::endl;
	}
	else
	{
		std::cerr << "error: failed to read one or more source images for stitching!" << std::endl;
		std::cout << "press any key..." << std::endl << std::flush;
		getchar();
		exit(1);
	}

	return success;
}

void emit_tilemap_diagnostics
(
	std::string outfile,
	RGB *m_psourcergb,
	RGB *m_ppalettergb,
	ulong_t sourcew,
	ulong_t sourceh,
	long_t *psrcmap,
	int tilesrcw,
	int tilesrch
)
{
	int tilesize = g_allargs.tilesize;

	// only emit mapvis review file if requested
	if (g_allargs.visual)
	{
		// rebuild source from tiles
		for (int yy = 0; yy < tilesrch; yy++)
		{
			for (int xx = 0; xx < tilesrcw; xx++)
			{
				RGB *pdst = &m_psourcergb[((yy * tilesize) * sourcew) + (xx * tilesize)];

				//int index = -1;
				//
				//int mx = xx - g_allargs.maprect_xpos;
				//int my = yy - g_allargs.maprect_ypos;
				//if ((mx >= 0 && my >= 0) && (mx < g_allargs.maprect_xsize && my < g_allargs.maprect_ysize))
				//{
				//	index = ptilemap[(my * tilemapw) + mx];
				//}

				int index = psrcmap[(yy*tilesrcw)+xx];

				bool inside = false;
				int mx = xx - g_allargs.maprect_xpos;
				int my = yy - g_allargs.maprect_ypos;
				if ((mx >= 0 && my >= 0) && 
					(mx < g_allargs.maprect_xsize && my < g_allargs.maprect_ysize))
				{
					inside = true;
				}

				// unfortunately we're storing the tiles in a list - better change this
				tile8dictionary_t::iterator it8 = s_tile8dictionary[0].begin();
				for (int i = 0; i < index; i++, it8++);

				if (inside)
				{
					// valid index, part of the map - fill with tile
					u8 *psrc = *it8;
					for (int py = 0; py < tilesize; py++)
					{
						for (int px = 0; px < tilesize; px++)
						{
							u8 c = psrc[(py * tilesize) + px];
							pdst[(py * sourcew) + px] = m_ppalettergb[c];
						}
					}
				}
				else
				{
					// not part of the map - darken pixels
					u8 *psrc = *it8;
					for (int py = 0; py < tilesize; py++)
					{
						for (int px = 0; px < tilesize; px++)
						{
							u8 c = psrc[(py * tilesize) + px];
							RGB v = m_ppalettergb[c];
							v.red >>= 1;
							v.green >>= 1;
							v.blue >>= 1;
							pdst[(py * sourcew) + px] = v;
						}
					}
				}
			}
		}

		std::string reviewfile(outfile);
		remove_extension(reviewfile);
		reviewfile.append("_mapvis.tga");

		emit_tga(reviewfile.c_str(), m_psourcergb, sourcew, sourceh);
		std::cout << "emitted map visualisation [" << reviewfile.c_str() << "]" << std::endl;
	}

	// only emit map index review file if requested
	if (g_allargs.visual)
	{
		// rebuild source from tiles
		for (int yy = 0; yy < tilesrch; yy++)
		{
			for (int xx = 0; xx < tilesrcw; xx++)
			{
				RGB *pdst = &m_psourcergb[((yy * tilesize) * sourcew) + (xx * tilesize)];

				//int index = -1;

				//int mx = xx - g_allargs.maprect_xpos;
				//int my = yy - g_allargs.maprect_ypos;
				//if ((mx >= 0 && my >= 0) && (mx < g_allargs.maprect_xsize && my < g_allargs.maprect_ysize))
				//{
				//	index = ptilemap[(my * tilemapw) + mx];
				//}

				int index = psrcmap[(yy*tilesrcw)+xx];

				bool inside = false;
				int mx = xx - g_allargs.maprect_xpos;
				int my = yy - g_allargs.maprect_ypos;
				if ((mx >= 0 && my >= 0) && 
					(mx < g_allargs.maprect_xsize && my < g_allargs.maprect_ysize))
				{
					inside = true;
				}

				// unfortunately we're storing the tiles in a list - better change this
				tile24dictionary_t::iterator it24 = s_tile24dictionary.begin();
				for (int i = 0; i < index; i++, it24++);

				if (inside)
				{
					// valid index, part of the map - fill with tile
					RGB *psrc = *it24;
					for (int py = 0; py < tilesize; py++)
					{
						for (int px = 0; px < tilesize; px++)
						{
							pdst[(py * sourcew) + px] = psrc[(py * tilesize) + px];

							plot6x6hex3(pdst, sourcew, index, 1, 1);
						}
					}
				}
				else
				{
					// not part of the map - darken pixels
					RGB *psrc = *it24;
					for (int py = 0; py < tilesize; py++)
					{
						for (int px = 0; px < tilesize; px++)
						{
							RGB v = psrc[(py * tilesize) + px];
							v.red >>= 1;
							v.green >>= 1;
							v.blue >>= 1;
							pdst[(py * sourcew) + px] = v;

							plot6x6hex3(pdst, sourcew, index, 1, 1);
						}
					}
				}
			}
		}

		std::string reviewfile(outfile);
		remove_extension(reviewfile);
		reviewfile.append("_index.tga");

		emit_tga(reviewfile.c_str(), m_psourcergb, sourcew, sourceh);
		std::cout << "emitted map index visualisation [" << reviewfile.c_str() << "]" << std::endl;
	}

	// only emit tags review if requested
	if (g_allargs.tiletags.length() > 0)
	{
		std::ifstream tiletags(g_allargs.tiletags.c_str());
		if (!(tiletags.is_open() && tiletags.good()))
		{
			std::cerr << "error: unable to locate or read specified tiletags CSV file!" << std::endl;
			std::cout << "press any key..." << std::endl << std::flush;
			getchar();	
			exit(1);
		}

		// create maskindex from marked indices
		const int numtiles = s_tile24dictionary.size();
		u8 *pmaskindex = new u8[numtiles];
		memset(pmaskindex, 0, numtiles);

		// parse CSV
		for (CSVIterator loop(tiletags); loop != CSVIterator(); ++loop)
		{
			const CSVRow& r = (*loop);
			size_t s = r.size();
			if (s > 0)
			{
				std::string inspect = r[0];
				if (inspect[0] == ';')
				{
					// skip comment
				}
				else
				{
					int column_value = 0;
					int column_index = 0;

					std::cout << "reading tiletags: ";
		
					std::string colval = r[column_index++];
					std::transform(colval.begin(), colval.end(), colval.begin(), sanitise_csv_integer);
					char *pend = 0;
					column_value = strtol(colval.c_str(), &pend, 16);
					printf("index: %s\n", colval.c_str());
					if ((column_value >= 0) && (column_value <= numtiles))
					{	
						// tag this tile
						pmaskindex[column_value] = 1;
						std::cout << "tagged tile index [" << column_value << "]" << std::endl;
					}
					else
					{
						std::cerr << "error: invalid tile index field " << column_value << " in tiletags CSV file!" << std::endl;
						std::cout << "press any key..." << std::endl << std::flush;
						getchar();
						exit(1);
					}
				}
			}
		}

		// rebuild source from tiles
		for (int yy = 0; yy < tilesrch; yy++)
		{
			for (int xx = 0; xx < tilesrcw; xx++)
			{
				RGB *pdst = &m_psourcergb[((yy * tilesize) * sourcew) + (xx * tilesize)];

				//int index = -1;

				//int mx = xx - g_allargs.maprect_xpos;
				//int my = yy - g_allargs.maprect_ypos;
				//if ((mx >= 0 && my >= 0) && (mx < g_allargs.maprect_xsize && my < g_allargs.maprect_ysize))
				//{
				//	index = ptilemap[(my * tilemapw) + mx];
				//}

				int index = psrcmap[(yy*tilesrcw)+xx];

				bool inside = false;
				int mx = xx - g_allargs.maprect_xpos;
				int my = yy - g_allargs.maprect_ypos;
				if ((mx >= 0 && my >= 0) && 
					(mx < g_allargs.maprect_xsize && my < g_allargs.maprect_ysize))
				{
					inside = true;
				}

				if (inside)
				{
					if (pmaskindex[index])
					{
						for (int py = 0; py < tilesize; py++)
						{
							for (int px = 0; px < tilesize; px++)
							{
								pdst[(py * sourcew) + px].red = ~(pdst[(py * sourcew) + px].red >> 1);
								pdst[(py * sourcew) + px].green = ~(pdst[(py * sourcew) + px].green >> 1);
								pdst[(py * sourcew) + px].blue = ~(pdst[(py * sourcew) + px].blue >> 1);
							}
						}
					}
				}
			}
		}

		delete[] pmaskindex;

		{
			std::string reviewfile(outfile);
			remove_extension(reviewfile);
			reviewfile.append("_tags.tga");

			emit_tga(reviewfile.c_str(), m_psourcergb, sourcew, sourceh);
			std::cout << "emitted map tags visualisation [" << reviewfile.c_str() << "]" << std::endl;
		}

	}
}


bool do_tilemap_single()
{
	ulong_t sourcew = 0;
	ulong_t sourceh = 0;
	ulong_t sourced = 0;
	RGB *m_psourcergb = 0;
	RGB *m_ppalettergb = 0;
	ubyte_t *m_psourcecol = 0;
	ubyte_t *m_psourcecol2 = 0;
	ubyte_t *m_poriginalsrccol = 0;

	long_t *psrcmap = 0;
	int tilesrcw = 0;
	int tilesrch = 0;
	long_t *ptilemap = 0;
	int tilemapw = 0;
	int tilemaph = 0;

	std::cout << "map / tile cutting mode (single map output)..." << std::endl;

	bool success = false;

	if ((g_allargs.sources.size() > 1) && g_allargs.stitchmaps)
	{
		success = stitch_sources
		(
			m_psourcergb,
			m_ppalettergb,
			m_psourcecol,
			sourcew,
			sourceh,
			sourced
		);
	}
	else
	{
		// single source image - just load it and use it
		success = image_load(g_allargs.sourcefile.c_str(), m_psourcergb, m_psourcecol, m_ppalettergb, sourcew, sourceh, sourced);
		if (success)
			std::cout << "read [" << g_allargs.sourcefile.c_str() << "] with colour depth " << sourced << " and dimensions " << sourcew << " x " << sourceh << std::endl;
	}

	if (success)
	{
		//if (1)
		{
			//std::cout << "read [" << g_allargs.sourcefile.c_str() << "] with colour depth " << sourced << " and dimensions " << sourcew << " x " << sourceh << std::endl;

			if ((sourcew & (g_allargs.tilesize - 1)) || (sourceh & (g_allargs.tilesize - 1)))
			{
				std::cerr << "error: source image is not a multiple of " << g_allargs.tilesize << " pixels on both axes - check the map!" << std::endl;
				std::cout << "press any key..." << std::endl << std::flush;
				getchar();	
				exit(1);
			}

			if (m_psourcecol)
			{
				bool doremap = true;

				if (sourced > g_allargs.planes)
				{
					if (g_allargs.noremap)
					{
						std::cout << "warning: source image is indexed and > " << g_allargs.planes << " index bits - will use first 16 colours only (--no-remap)" << std::endl;
						doremap = false;
					}
					else
					{
						std::cout << "warning: source image is indexed and > " << g_allargs.planes << " index bits - will be remapped to external palette" << std::endl;
					}
				}
				else
				{
					if (g_allargs.forceremap)
					{
						std::cout << "warning: source image is indexed and <= " << g_allargs.planes << " index bits - but will remap anyway (--force-remap)" << std::endl;
					}
					else
					{
						std::cout << "warning: source image is indexed and <= " << g_allargs.planes << " index bits - will pass through without remapping" << std::endl;
						doremap = false;
					}
				}

				if (doremap)
				{
					m_poriginalsrccol = m_psourcecol;
					m_psourcecol = 0;
				}
				else
				{
					// check that indexed colours are now in range for bitplanes specified
					int maxcolidx = 1 << g_allargs.planes;
					int highest = 0;
					for (size_t p = 0; p < sourcew*sourceh; p++)
					{
						if ((m_psourcecol[p] >= maxcolidx) && (m_psourcecol[p] != g_allargs.keycol))
						{
							highest = xmax(highest, m_psourcecol[p]);
							m_psourcecol[p] = g_allargs.keycol;
//							break;
						}
					}

					if (highest)
					{
						std::cout << "warning: --no-remap was specified, but source colour indices exceed target bitplane count!" << std::endl;
						std::cout << "highest usable index = " << maxcolidx - 1 << ", highest index encountered = " << highest << std::endl;
						std::cout << "all illegal colours have been converted to --key-colour..." << std::endl;
					}
				}

/*
				int maxcolidx = 1 << g_allargs.planes;
				// check that indexed colours are in range for bitplanes specified
				for (size_t p = 0; p < sourcew*sourceh; p++)
				{
					if (m_psourcecol[p] >= maxcolidx)
					{
						std::cout << "warning: source image is indexed, but values exceed target bitplane count: remapping..." << std::endl;
						// ignore colourmap
						m_poriginalsrccol = m_psourcecol;
						m_psourcecol = 0;
						break;
					}
				}

				std::cout << "adopting palette and colourmap provided by source image..." << std::endl;
*/
			}

			if (m_psourcecol)
			{
				std::cout << "using palette and colourmap provided by source image..." << std::endl;

				if (!g_allargs.quickcut && !g_allargs.quiet)
				{
					// review palette
					std::cout << "summary of source palette for indexed source image being remapped" << std::endl;
					review_palettes(m_ppalettergb, NULL, sourced);
				}
			}
			else
			{
				std::cout << "reducing " << ((sourced > 0) ? sourced : 24) << "-bit image to external fixed palette..." << std::endl;
				m_psourcecol = new ubyte_t[sourcew * sourceh]; // (ubyte_t*)malloc(sourcew * sourceh);
				if (g_allargs.ccfields == 2)
					m_psourcecol2 = new ubyte_t[sourcew * sourceh]; // (ubyte_t*)malloc(sourcew * sourceh);
				// reduce source image to indexed colour with fixed palette
				reduce_image24(m_psourcergb, 0, m_psourcecol, m_psourcecol2, sourcew, sourceh, sourced, m_ppalettergb);
			}

			s_tileplanardictionary[0].clear();
			s_tileplanardictionary[1].clear();

			s_tile24dictionary.clear();
			s_tile8dictionary[0].clear();
			s_tile8dictionary[1].clear();

			std::cout << "extracting unique tiles..." << std::endl;

			process_tilemap2
			(
				psrcmap, 
				tilesrcw, tilesrch, 
				ptilemap, 
				tilemapw, tilemaph, 
				m_psourcergb, 
				m_psourcecol, m_psourcecol2,
				sourcew, sourceh,
				g_allargs.ccfields
			);

			// done with sourcecol
			if (m_psourcecol)
				delete[] m_psourcecol;
			m_psourcecol = nullptr;
			if (m_psourcecol2)
				delete[] m_psourcecol2;
			m_psourcecol2 = nullptr;

			if (ptilemap)
			{
				// if no -o <outfile> was specified, adopt the source directory and name, just changing the extension
				std::string outfile = g_allargs.sourcefile;

				// for single sources, map adopts -o <outfile> filename, just changing the extension
				if (g_allargs.outfile.length() > 0)
					outfile = g_allargs.outfile;

				remove_extension(outfile);

				if (g_allargs.outmode == Outmode_RBPLUS)
				{
					emit_tilemap_16bitraw(outfile.c_str(), ptilemap, tilemapw, tilemaph);
				}
				else
				{
					int tilestep = bceil<int>(g_allargs.tilesize);
					emit_tilemap_ccm(outfile.c_str(), ptilemap, tilemapw, tilemaph, tilestep);
				}

				// emit tilemap review files
				emit_tilemap_diagnostics
				(
					outfile,
					m_psourcergb,
					m_ppalettergb,
					sourcew,
					sourceh,
					psrcmap,
					tilesrcw,
					tilesrch
				);
			}
/*
			// construct output filename: [infile.map]
			std::string outfile;
			if (g_allargs.outfile.length() > 0)
			{
				outfile = g_allargs.outfile;
			}
			else
			{
				outfile = g_allargs.sourcefile;
			}

			// emit tilemap review files
			emit_tilemap_diagnostics
			(
				outfile,
				m_psourcergb,
				m_ppalettergb,
				sourcew,
				sourceh,
				psrcmap,
				tilesrcw,
				tilesrch
			);
*/
			// done with sourcergb
			if (m_psourcergb)
				delete[] m_psourcergb;
			m_psourcergb = nullptr;

			// all tiles processed
			std::cout << "...done" << std::endl;


			// calculate compressed footprint
			float footprint = (float)s_tile24dictionary.size() / float(tilesrcw*tilesrch);
			std::cout << "final dictionary contains " << s_tile24dictionary.size() << " unique tiles with footprint @ " << footprint << " of original image." << std::endl;

			{
				if (footprint > 0.5f)
					std::cout << " tile reuse is terrible - source image was rendered in crayon by a womble" << std::endl;
				if (footprint > 0.3f)
					std::cout << " tile reuse is poor - check tile alignment at top left and lower right of source image" << std::endl;
				else
				if (footprint > 0.2f)
					std::cout << " tile reuse is fair - but carefully recheck tile alignment to be sure?" << std::endl;
				else
				if (footprint > 0.1f)
					std::cout << " tile reuse seems ok but keep an eye on things..." << std::endl;
				else
					std::cout << " tile reuse is good! > l33t mapping 5ki11z <" << std::endl;
			}

			for (int ccf = 0; ccf < g_allargs.ccfields; ccf++)
			{
				// convert 8bit indexed tiles into 8-plane graphics
				remap_dictionary_tiles(s_tile8dictionary[ccf], s_tileplanardictionary[ccf]);

				// construct output filename: [infile]
				std::string outfile;
				if (g_allargs.outfile.length() > 0)
				{
					outfile = g_allargs.outfile;
				}
				else
				{
					outfile = g_allargs.sourcefile;
				}

				remove_extension(outfile);
				if (g_allargs.ccfields == 2)
				{
					if (ccf == 0)
						outfile.append("0");
					else
						outfile.append("1");
				}

				if (g_allargs.outmode == Outmode_DEGAS)
				{
					// emit tiles to a series of 320x200 .PI1 images
					emit_tiles_degas(outfile, m_ppalettergb, false, ccf);
					if (g_allargs.translayer)
					{
						std::string outfilem(outfile);
						outfilem.append("-");
						emit_tiles_degas(outfilem, m_ppalettergb, true, ccf);
					}
				}
				else
				if (g_allargs.outmode == Outmode_DIRECT)
				{
					emit_tiles_cct(outfile, m_ppalettergb, ccf);
				}
				else
				if (g_allargs.outmode == Outmode_RBPLUS)
				{
					emit_tiles_rbplus(outfile, m_ppalettergb);
				}

			} // ccf loop

			// palette shared across all stages, released last
			if (m_ppalettergb)
				delete[] m_ppalettergb;
			m_ppalettergb = nullptr;
		}
		//else
		//{
		//	std::cerr << "warning: could not decode [" << g_allargs.sourcefile.c_str() << "] source image" << std::endl;
		//}
	}
	else
	{
		std::cerr << "warning: could not open [" << g_allargs.sourcefile.c_str() << "] source file" << std::endl;
	}

	return true;
}

bool do_tilemap_multi()
{
	RGB *m_ppalettergb = 0;
	int tilesrcw = 0;
	int tilesrch = 0;

	std::cout << "map / tile cutting mode (multiple maps, shared tileset)..." << std::endl;

	s_tileplanardictionary[0].clear();
	s_tileplanardictionary[1].clear();

	s_tile24dictionary.clear();
	s_tile8dictionary[0].clear();
	s_tile8dictionary[1].clear();

	for (auto & source : g_allargs.sources)
	{
		ulong_t sourcew = 0;
		ulong_t sourceh = 0;
		ulong_t sourced = 0;
		RGB *m_psourcergb = 0;
		ubyte_t *m_psourcecol = 0;
		ubyte_t *m_psourcecol2 = 0;
		ubyte_t *m_poriginalsrccol = 0;

		// read next source image
		bool success = image_load(source.c_str(), m_psourcergb, m_psourcecol, m_ppalettergb, sourcew, sourceh, sourced);

		if (success)
		{
			std::cout << "read [" << source.c_str() << "] with colour depth " << sourced << " and dimensions " << sourcew << " x " << sourceh << std::endl;

			if ((sourcew & (g_allargs.tilesize - 1)) || (sourceh & (g_allargs.tilesize - 1)))
			{
				std::cerr << "error: source image is not a multiple of " << g_allargs.tilesize << " pixels on both axes - check the map!" << std::endl;
				std::cout << "press any key..." << std::endl << std::flush;
				getchar();
				exit(1);
			}

			if (m_psourcecol)
			{
				bool doremap = true;

				if (sourced > g_allargs.planes)
				{
					if (g_allargs.noremap)
					{
						std::cout << "warning: source image is indexed and > " << g_allargs.planes << " index bits - will use first 16 colours only (--no-remap)" << std::endl;
						doremap = false;
					}
					else
					{
						std::cout << "warning: source image is indexed and > " << g_allargs.planes << " index bits - will be remapped to external palette" << std::endl;
					}
				}
				else
				{
					if (g_allargs.forceremap)
					{
						std::cout << "warning: source image is indexed and <= " << g_allargs.planes << " index bits - but will remap anyway (--force-remap)" << std::endl;
					}
					else
					{
						std::cout << "warning: source image is indexed and <= " << g_allargs.planes << " index bits - will pass through without remapping" << std::endl;
						doremap = false;
					}
				}

				if (doremap)
				{
					m_poriginalsrccol = m_psourcecol;
					m_psourcecol = 0;
				}
				else
				{
					// check that indexed colours are now in range for bitplanes specified
					int maxcolidx = 1 << g_allargs.planes;
					int highest = 0;
					for (size_t p = 0; p < sourcew*sourceh; p++)
					{
						if ((m_psourcecol[p] >= maxcolidx) && (m_psourcecol[p] != g_allargs.keycol))
						{
							highest = xmax(highest, m_psourcecol[p]);
							m_psourcecol[p] = g_allargs.keycol;
							//							break;
						}
					}

					if (highest)
					{
						std::cout << "warning: --no-remap was specified, but source colour indices exceed target bitplane count!" << std::endl;
						std::cout << "highest usable index = " << maxcolidx - 1 << ", highest index encountered = " << highest << std::endl;
						std::cout << "all illegal colours have been converted to --key-colour..." << std::endl;
					}
				}
			}

			if (m_psourcecol || (g_allargs.palettefile.length() == 0))
			{
				std::cerr << "error: can't process multiple distinct tilemaps without remapping to a shared palette! use --force-remap with --pal external palette!" << std::endl;
				std::cout << "press any key..." << std::endl << std::flush;
				getchar();
				exit(1);
			}
			else
			{
				std::cout << "reducing " << ((sourced > 0) ? sourced : 24) << "-bit image to external fixed palette..." << std::endl;
				m_psourcecol = new ubyte_t[sourcew * sourceh]; // (ubyte_t*)malloc(sourcew * sourceh);
				if (g_allargs.ccfields == 2)
					m_psourcecol2 = new ubyte_t[sourcew * sourceh]; // (ubyte_t*)malloc(sourcew * sourceh);
				// reduce source image to indexed colour with fixed palette
				reduce_image24(m_psourcergb, 0, m_psourcecol, m_psourcecol2, sourcew, sourceh, sourced, m_ppalettergb);
			}


			std::cout << "extracting unique tiles..." << std::endl;

			long_t *psrcmap = 0;
			long_t *ptilemap = 0;
			int tilemapw = 0;
			int tilemaph = 0;

			process_tilemap2
			(
				psrcmap,
				tilesrcw, tilesrch,
				ptilemap,
				tilemapw, tilemaph,
				m_psourcergb,
				m_psourcecol, m_psourcecol2,
				sourcew, sourceh,
				g_allargs.ccfields
			);

			// done with sourcecol
			if (m_psourcecol)
				delete[] m_psourcecol;
			m_psourcecol = nullptr;
			if (m_psourcecol2)
				delete[] m_psourcecol2;
			m_psourcecol2 = nullptr;

			if (ptilemap)
			{
				// if no -o <outfile> was specified, adopt the source directory and name, just changing the extension
				std::string outfile = source;

				if (g_allargs.sources.size() == 1)
				{
					// for single sources, map adopts -o <outfile> filename, just changing the extension
					if (g_allargs.outfile.length() > 0)
						outfile = g_allargs.outfile;
				}
				else
				{
					// for multiple sources, each map adopts a source filename but still uses -o <outdir> location

					if (g_allargs.outfile.length() > 0)
					{
						// construct the output filename: [outpath]/[sourcename].ccm
							
						size_t stub_pos = outfile.find_last_of("\\/");
						if (stub_pos != std::string::npos)
							outfile = outfile.substr(stub_pos + 1, std::string::npos);

						std::string outdir;
						size_t dir_pos = g_allargs.outfile.find_last_of("\\/");
						if (dir_pos != std::string::npos)
							outdir = g_allargs.outfile.substr(0, dir_pos) + "/";

						outfile = outdir + outfile;
					}
				}

				// construct output filename: [name].ccm
				remove_extension(outfile);

				if (g_allargs.outmode == Outmode_RBPLUS)
				{
					emit_tilemap_16bitraw(outfile.c_str(), ptilemap, tilemapw, tilemaph);
				}
				else
				{
					int tilestep = bceil<int>(g_allargs.tilesize);
					emit_tilemap_ccm(outfile.c_str(), ptilemap, tilemapw, tilemaph, tilestep);
				}

				// emit tilemap review files
				emit_tilemap_diagnostics
				(
					outfile,
					m_psourcergb,
					m_ppalettergb,
					sourcew,
					sourceh,
					psrcmap,
					tilesrcw,
					tilesrch
				);
			}

			/*
			// construct distinct output filename: [infile]
			std::string outfile = source;

			// emit tilemap review files
			emit_tilemap_diagnostics
			(
				outfile,
				m_psourcergb,
				m_ppalettergb,
				sourcew,
				sourceh,
				psrcmap,
				tilesrcw,
				tilesrch
			);
			*/

			// done with sourcergb
			if (m_psourcergb)
				delete[] m_psourcergb;
			m_psourcergb = nullptr;

		} // success
		else
		{
			std::cerr << "warning: could not open [" << source.c_str() << "] source file" << std::endl;
		}

	} // for (sources)

	// all tiles discovered
	std::cout << "...done" << std::endl;

	// calculate compressed footprint
	float footprint = (float)s_tile24dictionary.size() / float(tilesrcw*tilesrch);
	std::cout << "final dictionary contains " << s_tile24dictionary.size() << " unique tiles with footprint @ " << footprint << " of original image." << std::endl;

	{
		if (footprint > 0.5f)
			std::cout << " tile reuse is terrible - source image was rendered in crayon by a womble" << std::endl;
		if (footprint > 0.3f)
			std::cout << " tile reuse is poor - check tile alignment at top left and lower right of source image" << std::endl;
		else
			if (footprint > 0.2f)
				std::cout << " tile reuse is fair - but carefully recheck tile alignment to be sure?" << std::endl;
			else
				if (footprint > 0.1f)
					std::cout << " tile reuse seems ok but keep an eye on things..." << std::endl;
				else
					std::cout << " tile reuse is good! > l33t mapping 5ki11z <" << std::endl;
	}


	for (int ccf = 0; ccf < g_allargs.ccfields; ccf++)
	{
		// convert 8bit indexed tiles into 8-plane graphics
		remap_dictionary_tiles(s_tile8dictionary[ccf], s_tileplanardictionary[ccf]);

		// construct output filename: [infile]
		std::string outfile;
		if (g_allargs.outfile.length() > 0)
		{
			outfile = g_allargs.outfile;
		}
		else
		{
			// use first source as root name
			outfile = g_allargs.sources.front();
		}

		remove_extension(outfile);
		if (g_allargs.ccfields == 2)
		{
			if (ccf == 0)
				outfile.append("0");
			else
				outfile.append("1");
		}

		if (g_allargs.outmode == Outmode_DEGAS)
		{
			// emit tiles to a series of 320x200 .PI1 images
			emit_tiles_degas(outfile, m_ppalettergb, false, ccf);
			if (g_allargs.translayer)
			{
				std::string outfilem(outfile);
				outfilem.append("-");
				emit_tiles_degas(outfilem, m_ppalettergb, true, ccf);
			}
		}
		else
		if (g_allargs.outmode == Outmode_DIRECT)
		{
			emit_tiles_cct(outfile, m_ppalettergb, ccf);
		}
		else
		if (g_allargs.outmode == Outmode_RBPLUS)
		{
			emit_tiles_rbplus(outfile, m_ppalettergb);
		}

	} // ccf loop

	// palette shared across all stages, released last
	if (m_ppalettergb)
		delete[] m_ppalettergb;
	m_ppalettergb = nullptr;

	return true;
}

// --------------------------------------------------------------------
//	sprite cutting storage
// --------------------------------------------------------------------

// todo: tidy this up!

static int g_sprite_bpwidth = 0;
static int g_sprite_width = 0;
static int g_sprite_height = 0;
static int g_spritesource_w = 0;
static int g_spritesource_h = 0;


class om_descriptor
{
public:

	om_descriptor()
		: tdata(nullptr), odata(nullptr),
		  y1(0), y2(0)
	{
	}

	uword_t *tdata;
	uword_t *odata;
	s16 y1;
	s16 y2;

	void release()
	{
		delete[] tdata; tdata = nullptr;
		delete[] odata; odata = nullptr;
	}
};

class constrained_wordspan
{
public:

	constrained_wordspan() = default;
	constrained_wordspan(const constrained_wordspan&) = default;

	constrained_wordspan
	(
		int _ws, 
		int _ww, 
		bool _cxs, 
		bool _cxe, 
		int _line_orig,
		int _line_aww, 
		u16 *_pmasks, 
		int _buscycle_count
	)
		: ws(_ws), 
		  ww(_ww), 
		  cxs(_cxs), 
		  cxe(_cxe), 
		  line_orig(_line_orig),
		  line_active_wordwidth(_line_aww),
		  buscycle_count(_buscycle_count),
		  //
		  draw_order(0),
		  sww(0),
		  sws(0),
		  span_src_wos(0),
		  //
		  group(0)
	{
		reload[0] = false;
		reload[1] = false;
		reload[2] = false;

		switch (_ww)
		{
		case 0:
			printf("error!\n"); exit(1);
			break;
		case 1:
			{
				em[0] = _pmasks[0];
				em[1] = 0;
				em[2] = 0;
			}
			//printf("em1:%04x [em2:%04x em3:%04x]\n", em[0], em[1], em[2]);
			break;
		case 2:
			{
				em[0] = _pmasks[0];
				em[1] = 0;
				if (_pmasks[1])
				{
					em[2] = _pmasks[1];
				}
				else
				{
					em[2] = _pmasks[0];
				}
			}
			//printf("em1:%04x [em2:%04x] em3:%04x\n", em[0], em[1], em[2]);
			break;
		default:
			{
				em[0] = _pmasks[0];
				if (_pmasks[2])
				{
					// all 3 masks allocated
					em[1] = _pmasks[1];
					em[2] = _pmasks[2];
				}
				else
				if (_pmasks[1])
				{
					// special case: span ended with 2 words allocated, so expand
					em[1] = _pmasks[1];
					em[2] = _pmasks[1];
				}
				else
				{
					// special case: span ended with 1 word allocated, so expand
					em[1] = _pmasks[0];
					em[2] = _pmasks[0];
				}
				//printf("em1:%04x em2:%04x em3:%04x\n", em[0], em[1], em[2]);
			}
			break;
		};
	}

	// source fields
//	int fxs;		// fine x start
//	int fxe;		// fine x end
	int ws;			// word start
	int ww;			// word width
	bool cxs;		// cropped x start (+FISR)
	bool cxe;		// cropped x end   (-NFSR)

	int line_active_wordwidth;	// source line active width
	int line_orig;	// original line index
	u16 em[3];		// endmasks
	bool reload[3];
	int buscycle_count;	// bus cycles required

	int sww;
	int sws;
	int span_src_wos;
	int line_opt;	// order-optimized line index
	int draw_order;
	int group;
};

typedef std::list<constrained_wordspan> span_hseq_t;

class span_analysis
{
public:

	std::vector<span_hseq_t> span_vpattern_;
};

class blitstate_t
{
public:

	blitstate_t()
		: SYI(0), DYI(0), SKEW(0), XC(0),
		  EM1(0), EM2(0), EM3(0)
	{
	}

	bool alias_of(const blitstate_t &_o) const
	{
		return
		(
			(SYI == _o.SYI) &&
			(DYI == _o.DYI) &&
			(SKEW == _o.SKEW) &&
			(XC == _o.XC) &&
			(EM1 == _o.EM1) &&
			(EM2 == _o.EM2) &&
			(EM3 == _o.EM3)
		);
	}

	u8 SYI;
	u8 DYI;
	u8 SKEW; // d1
	u8 XC;
	u16 EM1;
	u16 EM2;
	u16 EM3;
};

class scanstate_t
{
public:

	scanstate_t()
		: allocated(false),
		  emdata_offset(0),
		  srccol_offset(0),
		  mprog_offset(0),
		  lines_remaining(0),
		  dst_line_offset(0),
		  dst_word_offset(0)
	{
	}

	bool alias_of(const scanstate_t &_o) const
	{
		return
		(
			(emdata_offset == _o.emdata_offset) &&
			(srccol_offset == _o.srccol_offset) &&
			(mprog_offset == _o.mprog_offset) &&
			(dst_line_offset == _o.dst_line_offset) &&
			(dst_word_offset == _o.dst_word_offset) &&
			(blitstate.alias_of(_o.blitstate))
		);
	}

	bool allocated;

	u16 emdata_offset;
	u16 srccol_offset;
	u16 mprog_offset;
	u16 lines_remaining;
	u8 dst_line_offset;
	u8 dst_word_offset;

	blitstate_t blitstate;
};

class preshift_t
{
public:

	word_t shift_;
	word_t planes_;
	word_t height_;
	word_t wordwidth_;
	word_t wordoffset_;
	word_t wordsremaining_;
	word_t planewords_;
	uword_t *data_;
	ubyte_t *p68kbin;
	size_t size68kbin;

	span_analysis sa;

	std::vector<u16> shared_mask_words;
	std::vector<scanstate_t> scan_states;
	scanstate_t first_scan_state;
	std::map<int, bool> occupied_lines;
	std::vector < constrained_wordspan* > stacked_spans;
	int mprog_final;
	int file_offset;

	preshift_t
	(
		int _shift, 
		int _planes, 
		int _height, 
		int _wordwidth, 
		int _wordoffset, 
		int _wordsremaining, 
		uword_t *_data
	)
		: shift_(_shift),
		  planes_(_planes),
		  height_(_height),
		  wordwidth_(_wordwidth),
		  wordoffset_(_wordoffset),
		  wordsremaining_(_wordsremaining),
		  data_(_data),
		  p68kbin(nullptr),
		  size68kbin(0),
		  mprog_final(0),
		  file_offset(0)
	{
		planewords_ = _height * _wordwidth;
	}

	~preshift_t()
	{
		// do not delete here - no refcounting on allocations happening within STL container
	}

	void release()
	{
		delete[] data_; data_ = 0;
	}
};

typedef std::list<std::shared_ptr<preshift_t>> preshiftlist_t;

static void print_dataword(uword_t _data)
{
	uword_t v = _data;
	for (int i = 0; i < 16; i++)
	{
		if (v & 0x8000)
			putchar('1');
		else
			putchar('0');

		v <<= 1;
	}
}

static void print_datalong(ulong_t _data)
{
	ulong_t v = _data;
	for (int i = 0; i < 32; i++)
	{
		if (v & 0x80000000)
			putchar('1');
		else
			putchar('0');

		v <<= 1;
	}
}

static void print_data(u64 _data, int _count)
{
	u64 v = _data;
	u64 mask = (u64)1 << (_count - 1);
	for (int i = 0; i < _count; i++)
	{
		if (v & mask)
			putchar('1');
		else
			putchar('0');

		v <<= 1;
	}
}

class spriteframe_t
{
public:

	word_t w_, h_;
	word_t xo_, yo_;
	uword_t *mask_;
	uword_t *colr0_;
	uword_t *colr1_;

	// specific to EMS2
	ubyte_t *ems2_p68kbin_;
	size_t ems2_size68kbin_;

	std::vector<int> active_starts;
	std::vector<int> active_widths;

	preshiftlist_t pscolr_frames_;

	std::vector<om_descriptor> occlusion_maps_;

	struct generated_data_t
	{
		std::vector<preshiftlist_t> psmask_frames_;
		std::vector<int> linemap_opt_2_src;
		std::vector<int> linemap_src_2_opt;
	};

	generated_data_t emx_;
	generated_data_t ems_;


	spriteframe_t(int _w, int _h, int _xo, int _yo, uword_t *_mask, uword_t *_colr0, uword_t *_colr1)
		: w_(_w),
		  h_(_h),
		  xo_(_xo),
		  yo_(_yo),
		  mask_(_mask),
		  colr0_(_colr0),
		  colr1_(_colr1),
		  ems2_p68kbin_(nullptr),
		  ems2_size68kbin_(0)
	{
	}

	~spriteframe_t()
	{
		// do not delete here - no refcounting on allocations happening within STL container
	}

	static const int c_max_occlusion_ys = 15;
	static const int c_num_om_xshifts = 4;
	static const int c_num_om_yshifts = 4;

	void generate_occlusion_maps()
	{
		occlusion_maps_.clear();
		occlusion_maps_.resize(c_num_om_xshifts * c_num_om_yshifts);

		int full_src_wordwidth = ((w_ + 15) & -16) >> 4;

		// process multiple 'preshifts', to regain some accuracy for tile coverage at 
		// fine x,y positions. otherwise the occlusion masks are way too conservative
		// and we don't gain much occlusion.
		for (int ysi = 0, yshift = 0; yshift < 16; ysi++, yshift += (16 / c_num_om_yshifts))
		{
			for (int xsi = 0, xshift = 0; xshift < 16; xsi++, xshift += (16 / c_num_om_xshifts))
			{
				// occlusion mask for this x,y preshift
				om_descriptor &om_desc = occlusion_maps_[xsi + (ysi*c_num_om_yshifts)];

				// allocate occlusion maps for each distinct x:y preshift pattern
				uword_t *occlusion_t = new uword_t[c_max_occlusion_ys];
				memset(occlusion_t, 0x00, sizeof(uword_t) * c_max_occlusion_ys);
				uword_t *occlusion_o = new uword_t[c_max_occlusion_ys];
				memset(occlusion_o, 0xFF, sizeof(uword_t) * c_max_occlusion_ys);

				om_desc.tdata = occlusion_t;
				om_desc.odata = occlusion_o;

				// process for all possible y offsets into this y-preshift, since we are not
				// generating preshifts for all possible fine-y positions (more ram).
				for (int yoff = 0; yoff < (16 / c_num_om_yshifts); yoff++)
				{
					for (int xoff = 0; xoff < (16 / c_num_om_xshifts); xoff++)
					{
						// process every line of sprite mask
						for (int y = 0; y < 240 + 16; y++)
						{
							int yy = (y - yshift) - yoff;

							int oc_linepos = (y >> 4);

							for (int x = 0; x < 240 + 16; x++)
							{
								int xx = (x - xshift) - xoff;

								// don't read mask pixels outwith source
								int bit = 1;
								if ((xx >= 0 && xx < w_) && (yy >= 0 && yy < h_))
								{
									uword_t *pline = &mask_[(yy * full_src_wordwidth)];
									uword_t word = pline[xx >> 4];
									bit = (word & (0x8000 >> (xx & 15)));
								}


								int oc_bitpos = (x >> 4);

								if (0 == bit)
									// set bit for any live pixel in this source grid tile
									occlusion_t[oc_linepos] |= (0x8000 >> oc_bitpos);
								else
									// clear bit for any dead pixel in this source grid tile
									occlusion_o[oc_linepos] &= ~(0x8000 >> oc_bitpos);

							}
						}
					}
				}

				
/*				printf("occlusion_t:\n");
				for (int d = 0; d < c_max_occlusion_ys; d++)
				{
					print_dataword(occlusion_t[d]); printf("\n");
				}
				printf("occlusion_o:\n");
				for (int d = 0; d < c_max_occlusion_ys; d++)
				{
					print_dataword(occlusion_o[d]); printf("\n");
				}
*/				

				// find first/last active line for each
				//int miny = -1;
				int maxy = -1;
				for (int n = 0; n < c_max_occlusion_ys; n++)
				{
					//if ((miny == 0) && (occlusion_t[n] != 0))
					//	miny = n;
					if ((maxy < n) && (occlusion_t[n] != 0))
						maxy = n;
					//if ((miny == 0) && (occlusion_o[n] != 0))
					//	miny = n;
					if ((maxy < n) && (occlusion_o[n] != 0))
						maxy = n;
				}

				// assume sprite begins at line 0 if properly fitted by cutting mechanism
				om_desc.y1 = 0;// xmax(miny, 0);
				om_desc.y2 = maxy + 1;

				//printf("%d->%d\n", om_desc.y1, om_desc.y2);
			}
		}
	}

	void generate_mask_preshifts(spriteframe_t::generated_data_t &_gen, int _range, int _step, int _max_dst_wordwidth)
	{
		int src_wordwidth = ((w_ + 15) & -16) >> 4;

		int preshift_index = 0;
		for (int s = 0; s < _range; s += _step, preshift_index++)
		{
//			printf("\n");

			int full_dst_wordwidth = (((w_ + s) + 15) & -16) >> 4;
			int full_src_wordwidth = ((w_ + 15) & -16) >> 4;

			//bool NFSR = (full_dst_wordwidth != full_src_wordwidth);

			int remaining_dst_wordwidth = full_dst_wordwidth;
			int consumed_dst_wordwidth = 0;
			
			int component = 0;
			while (remaining_dst_wordwidth > 0)
			{
				int dst_wordwidth = remaining_dst_wordwidth;

				// limit to 3 for EMS/EMX
				if (_max_dst_wordwidth > 0)
					if (dst_wordwidth > _max_dst_wordwidth)
						dst_wordwidth = _max_dst_wordwidth;

				remaining_dst_wordwidth -= dst_wordwidth;

				int preshiftwords = h_ * dst_wordwidth;

				//			printf("shift: %d\n", s);
				//			printf("dst_wordwidth: %d\n", dst_wordwidth);
				//			printf("preshiftwords: %d\n", preshiftwords);

				uword_t *preshift = new uword_t[preshiftwords];
				//memset(preshift, 0, 2 * preshiftwords);
				uword_t *pout = preshift;
				
				for (int y = 0; y < h_; y++)
				{
					//				printf("\n");
					
					// preload shift register
					uword_t *pin = &mask_[(y * full_src_wordwidth) + consumed_dst_wordwidth];
					u32 shiftreg = 0;
						if (consumed_dst_wordwidth > 0)
							shiftreg = ~pin[-1]; // FISR

					for (int dw = 0; dw < dst_wordwidth; dw++)
					{
						//					printf(" sr:%08x", (u32)shiftreg);

						shiftreg <<= 16;

						// fetch source while data remains
						if ((dw + consumed_dst_wordwidth) < full_src_wordwidth)
							shiftreg |= u32(u16(~(*pin++))); // NFSR

						uword_t m = ((uword_t)(shiftreg >> s));

						//					printf(" m:%04x", (u32)m);

						*pout++ = m;
					}
				}

				// todo: components created based on iterative extraction!
				if (preshift_index >= _gen.psmask_frames_.size())
					_gen.psmask_frames_.resize(preshift_index + 1);

				_gen.psmask_frames_[preshift_index].emplace_back
				(
					std::make_shared<preshift_t>
					(
						s, 1, h_,
						dst_wordwidth, consumed_dst_wordwidth, remaining_dst_wordwidth,
						preshift
					)
				);

				if (g_allargs.verbose)
					std::cout << " mask preshift index " << preshift_index << " step " << s << " component " << component << " @ wordwidth " << dst_wordwidth << std::endl;

				consumed_dst_wordwidth += dst_wordwidth;
				component++;
			}
		}
	}

	void release()
	{
		delete[] mask_; mask_ = 0;
		delete[] colr0_; colr0_ = 0;
		delete[] colr1_; colr1_ = 0;
	}
};

typedef std::list<spriteframe_t> spritelist_t;

// todo: tidy this up!

spritelist_t g_spriteframes;

int g_sprite_framewidth;
int g_sprite_frameheight;
int g_sprite_framebpwidth;

// --------------------------------------------------------------------
//	prepare sprite cutting sequence
// --------------------------------------------------------------------

void init_sprite_sequence(int _xs, int _ys, int _sw, int _sh)
{
	for (spritelist_t::iterator it = g_spriteframes.begin(); it != g_spriteframes.end(); it++)
		it->release();

	g_spriteframes.clear();

	g_spritesource_w = _sw;
	g_spritesource_h = _sh;

	// sprite width in bitplane words
	g_sprite_bpwidth = (_xs + (16-1)) & -16;
	g_sprite_width = _xs;
	g_sprite_height = _ys;

	// allocate sprite buffer for chunky processing (default to keycol)
	delete[] g_colrbuffer80;
	g_colrbuffer80 = new ubyte_t[g_sprite_bpwidth * g_sprite_height];
	memset(g_colrbuffer80, g_allargs.keycol, g_sprite_bpwidth * g_sprite_height);
	delete[] g_colrbuffer81;
	g_colrbuffer81 = new ubyte_t[g_sprite_bpwidth * g_sprite_height];
	memset(g_colrbuffer81, g_allargs.keycol, g_sprite_bpwidth * g_sprite_height);

	// allocate sprite buffer for bitplane processing
	delete[] g_colrbufferbpl0;
	g_colrbufferbpl0 = new uword_t[(g_sprite_bpwidth * g_sprite_height) >> 1];
	memset(g_colrbufferbpl0, 0, g_sprite_bpwidth * g_sprite_height);
	delete[] g_colrbufferbpl1;
	g_colrbufferbpl1 = new uword_t[(g_sprite_bpwidth * g_sprite_height) >> 1];
	memset(g_colrbufferbpl1, 0, g_sprite_bpwidth * g_sprite_height);


	// allocate mask buffer for chunky processing (default to transparency)
	delete[] g_maskbuffer8;
	g_maskbuffer8 = new ubyte_t[g_sprite_bpwidth * g_sprite_height];
	memset(g_maskbuffer8, 0xFF, g_sprite_bpwidth * g_sprite_height);

	// allocate mask buffer for bitplane processing
	delete[] g_maskbufferbpl;
	g_maskbufferbpl = new uword_t[(g_sprite_bpwidth * g_sprite_height) >> 1];
	memset(g_maskbufferbpl, 0, g_sprite_bpwidth * g_sprite_height);
}

// --------------------------------------------------------------------
//	cut a single frame from source image
// --------------------------------------------------------------------

void add_sprite_frame
(
	ubyte_t* _psource80, ubyte_t* _psource81, 
	ubyte_t* _pframemask, 
	int _x, int _y, 
	bool _xflip, bool _yflip, 
	int _hs, int _vs, 
	RGB* _ppal, 
	RGB* _pvisualrgb,
	int _sourcew, int _sourceh
)
{
	//std::cout << "1" << std::endl << std::flush;

	memset(g_colrbuffer80, g_allargs.keycol, g_sprite_bpwidth * g_sprite_height);
	memset(g_colrbuffer81, g_allargs.keycol, g_sprite_bpwidth * g_sprite_height);
	memset(g_maskbuffer8, 0xFF, g_sprite_bpwidth * g_sprite_height);

	//std::cout << "2" << std::endl << std::flush;

	//_pframemask = 0;

	int frame = g_spriteframes.size() + 1;

	// special case:
	// single-field colour, dual-field alpha with dual-field palette (-ccr both)
	// in this case, alternating colour dither is distributed over 2 distinct animation frames vs 2 embedded colour fields
	if (
		g_allargs.distributed_dualfield_ &&
		g_allargs.use_solutionpair_reduction_ &&
		// don't alternate fields if a specific field (or colour-match) has been specified
		(g_allargs.ccremap == CCRemap_BOTH)
	)
	{
		if (_psource81 == nullptr)
		{
			std::cerr << "error!" << std::endl;
			exit(-1);
		}

		if (frame & 1)
			std::swap(_psource80, _psource81);
	}

	//std::cout << "3" << std::endl << std::flush;


	int cxmin = 0;
	int cymin = 0;
	int cxmax = g_sprite_width;
	int cymax = g_sprite_height;

	// cutting may read source either x-flipped, y-flipped, both or neither so we configure loops here
	int pxs = _xflip ? (g_sprite_width-1) : 0;
	int pys = _yflip ? (g_sprite_height-1) : 0;
	int pxi = _xflip ? -1 : 1;
	int pyi = _yflip ? -1 : 1;

	//std::cout << "pxs:" << pxs << " pys:" << pys << " pxi:" << pxi << " pyi:" << pyi << std::endl << std::flush;

	if (g_allargs.visual && _pvisualrgb)
	{
		for (int lpy = 0, py = pys; lpy < g_sprite_height; lpy++, py += pyi)
		{
			for (int lpx = 0, px = pxs; lpx < g_sprite_width; lpx++, px += pxi)
			{
				int frameidx = g_spriteframes.size();

				int srcx = (px + _x);
				int srcy = (py + _y);

				//std::cout << "f:" << frameidx << " xs:" << srcx << " ys:" << srcy << std::endl << std::flush;

				ubyte_t c = g_allargs.keycol;
				if ((srcx >= 0 && srcy >= 0) && (srcx < _sourcew && srcy < _sourceh)) 
				{
					c = _psource80[(srcy * g_spritesource_w) + srcx];

					if ((_pframemask == nullptr) || (_pframemask[(srcy * g_spritesource_w) + srcx] == frame))
						;
					else
						c = g_allargs.keycol;
				}

				RGB &pix = _pvisualrgb[(frameidx * (g_sprite_width*g_sprite_height)) + (lpy*g_sprite_width) + lpx];
				
				c = alpha_dodge(c, srcx, srcy, (frame & 1), _ppal);

				if (c == g_allargs.keycol)
				{	
					pix.red = g_allargs.keyrgb.red;
					pix.green = g_allargs.keyrgb.green;
					pix.blue = g_allargs.keyrgb.blue;
				}
				else
				{
					pix = _ppal[c];
				}
			}
		}
	}

	//std::cout << "4" << std::endl << std::flush;

	if (g_allargs.preview)
	{
		printf("\n");
		for (int lpy = 0, py = pys; lpy < g_sprite_height; lpy++, py += pyi)
		{
			printf("\t|");
			for (int lpx = 0, px = pxs; lpx < g_sprite_width; lpx++, px += pxi)
			{
				int srcx = (px + _x);
				int srcy = (py + _y);

				ubyte_t c = g_allargs.keycol;
				if ((srcx >= 0 && srcy >= 0) && (srcx < _sourcew && srcy < _sourceh))
				{
					c = _psource80[(srcy * g_spritesource_w) + srcx];

					if ((_pframemask == nullptr) || (_pframemask[(srcy * g_spritesource_w) + srcx] == frame))
						;
					else
						c = g_allargs.keycol;
				}
				//c = alpha_dodge(c, (px + _x), (py + _y), (frame & 1), _ppal);

				if (c == g_allargs.keycol)
					printf(" ");
				else
				{
					c = alpha_dodge(c, srcx, srcy, (frame & 1), _ppal);
					if (c == g_allargs.keycol)
						printf("~");
					else
						printf("%x", (int)(c & 0xF));
				}
			}
			printf("|");
			if (_psource81/*g_allargs.ccfields > 1*/)
			{
				for (int lpx = 0, px = pxs; lpx < g_sprite_width; lpx++, px += pxi)
				{
					int srcx = (px + _x);
					int srcy = (py + _y);

					ubyte_t c = g_allargs.keycol;
					if ((srcx >= 0 && srcy >= 0) && (srcx < _sourcew && srcy < _sourceh))
					{
						c = _psource81[(srcy * g_spritesource_w) + srcx];

						if ((_pframemask == nullptr) || (_pframemask[(srcy * g_spritesource_w) + srcx] == frame))
							;
						else
							c = g_allargs.keycol;
					}

					if (c == g_allargs.keycol)
						printf(" ");
					else
					{
						c = alpha_dodge(c, srcx, srcy, (frame & 1), _ppal);
						if (c == g_allargs.keycol)
							printf("~");
						else
							printf("%x", (int)(c & 0xF));
					}
				}
				printf("|");
			}
			printf("\n");
		}
		printf("\n");
	}

	if (g_allargs.spriteoptimize)
	{
		cxmin = g_sprite_width;
		cymin = g_sprite_height;
		cxmax = 0;
		cymax = 0;

		// optimize cutting area

		for (int lpy = 0, py = pys; lpy < g_sprite_height; lpy++, py += pyi)
		{
			for (int lpx = 0, px = pxs; lpx < g_sprite_width; lpx++, px += pxi)
			{
				int srcx = (px + _x);
				int srcy = (py + _y);

				ubyte_t c = g_allargs.keycol;
				if ((srcx >= 0 && srcy >= 0) && (srcx < _sourcew && srcy < _sourceh))
				{
					c = _psource80[(srcy * g_spritesource_w) + srcx];

					if ((_pframemask == nullptr) || (_pframemask[(srcy * g_spritesource_w) + srcx] == frame))
						;
					else
						c = g_allargs.keycol;
				}

				if (c != g_allargs.keycol)
				{
					// opaque pixel - adjust bounds
					cxmin = xmin(cxmin, px);
					cxmax = xmax(cxmax, px + 1);
					cymin = xmin(cymin, py);
					cymax = xmax(cymax, py + 1);
				}
			}
		}
	}

	// capture at least one pixel per frame
	if ((cxmax < cxmin) || (cymax < cymin))
	{
		cxmin = 0;
		cxmax = 1;
		cymin = 0;
		cymax = 1;
	}

	// if preshift step applied, all anim frames should agree on x-snap
	if (
		(g_allargs.cutmode == Cutmode_EMSPR) ||
		(g_allargs.cutmode == Cutmode_EMXSPR) ||
		(g_allargs.cutmode == Cutmode_EMHSPR)
	)
	{
		if (g_allargs.preshift_step > 1)
		{
			cxmin = (cxmin & -g_allargs.preshift_step);
			//cxmax = (cxmax + (g_allargs.preshift_step - 1)) & -g_allargs.preshift_step;
		}
	}

	g_sprite_framewidth = cxmax - cxmin;
	g_sprite_frameheight = cymax - cymin;
	g_sprite_framebpwidth = (g_sprite_framewidth + (16-1)) & -16;

	// guard against active sprite source widths > 32 pixels for EMS/EMX formats

	if (g_allargs.cutmode == Cutmode_EMSPR)
	{
		if (1 && (g_sprite_framebpwidth >> 4) > 2)
		{
			if (g_allargs.emc)
			{
				std::cout << "note: using compound mode - this frame has a max source width of " << g_sprite_bpwidth
					<< std::endl;
			}
			else
			{
				std::cerr << "error: max source width for standard EMS/EMX is 32 pixels. for wide sprites enable compound mode (-emc). this frame has a max source width of " << g_sprite_bpwidth
					<< std::endl;
				std::cerr << "press any key..." << std::endl << std::flush;
				getchar();
				exit(1);
			}
		}
	}

	// cut chunky sprite to chunky colrbuffer & maskbuffer

	for (int psy = cymin, py = 0; psy < cymax; psy++, py++)
	{
		//int psyr = _yflip ? ((g_sprite_height - 1) - psy) : psy;
		int psyr = _yflip ? ((cymax - 1) - py) : (cymin + py);

		for (int psx = cxmin, px = 0; psx < cxmax; psx++, px++)
		{
			//int psxr = _xflip ? ((g_sprite_width - 1) - psx) : psx;
			int psxr = _xflip ? ((cxmax - 1) - px) : (cxmin + px);

			ubyte_t c0 = _psource80[((psyr + _y) * g_spritesource_w) + (psxr + _x)];
			ubyte_t c1 = c0;
			if (_psource81)
				c1 = _psource81[((psyr + _y) * g_spritesource_w) + (psxr + _x)];

			if ((_pframemask == nullptr) || (_pframemask[((psyr + _y) * g_spritesource_w) + (psxr + _x)] == frame))
				;
			else
			{
				c0 = c1 = g_allargs.keycol;
			}

			c0 = alpha_dodge(c0, (psxr + _x), (psyr + _y), (frame & 1), _ppal);
			//c1 = alpha_dodge(c1, (psxr + _x), (psyr + _y), 1, _ppal);

			// colour buffer receives actual colour, or 0 for keycol (so two colours can potentially map to 0)
			g_colrbuffer80[(py * g_sprite_framebpwidth) + px] = (c0 == g_allargs.keycol) ? 0x00 : c0;
			if (_psource81)
				g_colrbuffer81[(py * g_sprite_framebpwidth) + px] = (c1 == g_allargs.keycol) ? 0x00 : c1;

			// mask buffer receives 0 for colour, or 1 for keycol
			g_maskbuffer8[(py * g_sprite_framebpwidth) + px] = (c0 == g_allargs.keycol) ? 0xFF : 0x00;
		}
	}

	// chunky buffer -> planar buffer

	// colour component
	c2pw
	(
		/*src=*/g_colrbuffer80, /*dst*/g_colrbufferbpl0,
		/*w=*/g_sprite_framebpwidth, /*h=*/g_sprite_frameheight,
		/*bytes_per_src_line=*/g_sprite_framebpwidth, /*words_per_dst_line=*/g_sprite_framebpwidth>>1,
		/*planes=*/8	//g_allargs.planes
	);

	if (_psource81)
	c2pw
	(
		/*src=*/g_colrbuffer81, /*dst*/g_colrbufferbpl1,
		/*w=*/g_sprite_framebpwidth, /*h=*/g_sprite_frameheight,
		/*bytes_per_src_line=*/g_sprite_framebpwidth, /*words_per_dst_line=*/g_sprite_framebpwidth>>1,
		/*planes=*/8	//g_allargs.planes
	);

	// mask component
	c2pw
	(
		/*src=*/g_maskbuffer8, /*dst*/g_maskbufferbpl,
		/*w=*/g_sprite_framebpwidth, /*h=*/g_sprite_frameheight,
		/*bytes_per_src_line=*/g_sprite_framebpwidth, /*words_per_dst_line=*/g_sprite_framebpwidth>>1,
		/*planes=*/8	//g_allargs.planes
	);

	// generate the sprite data

	size_t spriteplane_wordsize = (g_sprite_framebpwidth >> 4) * g_sprite_frameheight;

	uword_t *maskdata = new uword_t[spriteplane_wordsize * 1];
	uword_t *colrdata0 = new uword_t[spriteplane_wordsize * g_allargs.planes];
	uword_t *colrdata1 = nullptr;
	if (_psource81)
		colrdata1 = new uword_t[spriteplane_wordsize * g_allargs.planes];

	// copy mask data - just need one plane of the 8 we converted!
	for (size_t v = 0; v < spriteplane_wordsize; v++)
	{
		maskdata[v] = g_maskbufferbpl[v << 3];
	}

	// copy plane data - need N planes of the 8 we converted
	{
		int s = 0;
		int d = 0;
		for (size_t v = 0; v < spriteplane_wordsize; v++, s += 8, d += g_allargs.planes)
		{
			for (int p = 0; p < g_allargs.planes; p++)
			{
				colrdata0[d + p] = g_colrbufferbpl0[s + p];
				if (colrdata1)
					colrdata1[d + p] = g_colrbufferbpl1[s + p];
			}
		}
	}

	std::cout << "created sprite frame " << g_spriteframes.size() << " from offset [" << cxmin << ", " << cymin << "] size [" << g_sprite_framewidth << " x " << g_sprite_frameheight << "]" << std::endl;
	g_spriteframes.push_back
	(
		spriteframe_t
		(
			g_sprite_framewidth, 
			g_sprite_frameheight, 
			cxmin, cymin, 
			maskdata, 
			colrdata0, colrdata1
		)
	);

	// optionally generate preshifts for mask or colour data

	// limit the max destination width of preshifted masks to 3 words for EMS/EMX
/*	int max_dst_wordwidth = 0;
	if (
		(g_allargs.cutmode == Cutmode_EMSPR)
//#if !defined(USE_EMX2)
		||
		(g_allargs.cutmode == Cutmode_EMXSPR)
//#endif
	)
	{
		max_dst_wordwidth = 3;
	}
*/
	auto & spr = g_spriteframes.back();

	if (g_allargs.genmask && (g_allargs.masklayout == Masklayout_PRESHIFT))
	{
		if (g_allargs.verbose)
			std::cout << "preshifting mask..." << std::endl;

		switch (g_allargs.cutmode)
		{
		case Cutmode_SPRITES:
		case Cutmode_SLABS:
		case Cutmode_SLABRESTORE:
		default:
			break;		
		case Cutmode_EMSPR:
			{
				// generate component count based on max wordwidth per component
				spr.generate_mask_preshifts(spr.ems_, 16, g_allargs.preshift_step, /*max_dst_wordwidth=*/3);
			}
			break;
		case Cutmode_EMXSPR:
			{
				// embedded EMS, for y-clipping. multiple components.
				spr.generate_mask_preshifts(spr.ems_, 16, g_allargs.preshift_step, /*max_dst_wordwidth=*/3);
				// generate single component
				spr.generate_mask_preshifts(spr.emx_, 16, g_allargs.preshift_step, 0);
			}
			break;
		case Cutmode_EMHSPR:
			{
				// generate single component for now
				spr.generate_mask_preshifts(spr.ems_, 16, g_allargs.preshift_step, 0);
			}
			break;
		};
	}
		
	if (g_allargs.emc && g_allargs.ocm)
		spr.generate_occlusion_maps();

}

// --------------------------------------------------------------------

#include "emxgen.inl"

#include "emcommon.inl"
//#include "emximpl_v1.inl"
#include "emximpl_v2.inl"
#include "emhimpl.inl"

// --------------------------------------------------------------------
//	emit .sps spritesheet sequence file
// --------------------------------------------------------------------

bool emit_sprite_sequence(bool _storepixels)
{
	std::string fname;
	if (g_allargs.outfile.length() > 0)
	{
		fname = g_allargs.outfile;
	}
	else
	{
		fname = g_allargs.sourcefile;
		replace_extension(fname, "sps");
	}

	// generate the right extension for sprite types - because there are several sprite formats confusion is never far away
	if (g_allargs.cutmode == Cutmode_EMSPR)
		replace_extension(fname, "ems");
	else
	if (g_allargs.cutmode == Cutmode_EMXSPR)
		replace_extension(fname, "emx");
	else
	if (g_allargs.cutmode == Cutmode_EMHSPR)
		replace_extension(fname, "emh");


	FILE *hout = fopen(fname.c_str(), "wb+");
	if (hout)
	{
		word_t wb;
		word_t wv;
		long_t lv;
		int tracked_filesize = 0;

		std::vector<int> sprite_index;

		// 4: size of file remaining (patched before closing)
		lv = 0;
		fwrite(&lv, 1, 4, hout);

		// 4: file CRC (generated & patched before closing)
		lv = 0;
		fwrite(&lv, 1, 4, hout); tracked_filesize += 4;

		// 2: version info
		wv = endianswap16(SPS_VERSION);
		fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

		// 2: number of sprite frames in sequence
		wv = endianswap16((uword_t)g_spriteframes.size());
		fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

		// 2: number of bitplanes in sprite data
		wv = endianswap16(g_allargs.planes);
		fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

		// 2: flags
		// 0 0 0 0 0 0 0 A D O C Y X L L M
		// M	mask present (1bpl mask data before colour data)
		// LL	mask layout mode (00=interleaved, 01=planar, 10=preshift, 11=codegen)
		// X	suppress x clipping
		// Y	suppress y clipping
		// C	compound sprite (more than 1 component expected, no x-clip supported)
		// O	occlusion maps
		// D	dual-field colour data
		// A	dual-field alpha (distributed dual-field)

		wv = 0;
		wv |= (g_allargs.genmask) ? 1 : 0;
		wv |= (g_allargs.distributed_dualfield_) ? (1<<8) : 0;

		if (g_allargs.cutmode == Cutmode_EMXSPR)
			wv |= 3 << 1;
		else
		if ((g_allargs.cutmode == Cutmode_EMSPR) || (g_allargs.cutmode == Cutmode_EMHSPR))
			wv |= 2 << 1;
		else
		if (g_allargs.masklayout == Masklayout_PLANAR)
			wv |= 1 << 1;

		if (g_allargs.noclipx)
			wv |= 1 << 3;
		if (g_allargs.noclipy)
			wv |= 1 << 4;

		if (g_allargs.emc)
			wv |= 1 << 5;

		if (!(g_spriteframes.back().occlusion_maps_.empty()))
			wv |= 1 << 6;

		if (g_allargs.dualfield)
			wv |= 1 << 7;

//		wv |= (g_allargs.genmask && (g_allargs.masklayout == Masklayout_PLANAR)) ? 2 : 0;

		wv = endianswap16((uword_t)wv);
		fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

		// 1: preshift range (TOOD: change purpose of this field to offset)
		wb = 0;
		fwrite(&wb, 1, 1, hout); tracked_filesize += 1;
		// 1: preshift step
		wb = g_allargs.preshift_step;
		fwrite(&wb, 1, 1, hout); tracked_filesize += 1;

		// 2: source width
		wv = endianswap16((uword_t)g_sprite_width);
		fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
		// 2: source height
		wv = endianswap16((uword_t)g_sprite_height);
		fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

		std::cout << "bound xs: " << std::dec << g_sprite_width << std::endl;
		std::cout << "bound ys: " << std::dec << g_sprite_height << std::endl;

		// create sprite frame index (patched before closing)
		int sprite_index_fpos = tracked_filesize;
		lv = 0xBBBBBBBB;
		for (spritelist_t::iterator it = g_spriteframes.begin(); it != g_spriteframes.end(); it++)
		{
			fwrite(&lv, 1, 4, hout); tracked_filesize += 4;
		}

		//std::map<int, int> mprog_fpos_remap;
		std::map<u32, int> mprog_hash_map;

		// individual sprite frames follow
		int si = 0;
		for (spritelist_t::iterator it = g_spriteframes.begin(); it != g_spriteframes.end(); it++, si++)
		{
			printf("frame: %03d xo:%03d yo:%03d xs:%03d ys:%02d\n", si, it->xo_, it->yo_, it->w_, it->h_);

			// record file-offset to sprite in sprite index, for patching later
			sprite_index.push_back(tracked_filesize);

			// 2: sprite frame pixel width
			wv = endianswap16(it->w_);
			fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
			// 2: sprite frame pixel height
			wv = endianswap16(it->h_);
			fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

			// 2: sprite frame xoff
			wv = endianswap16(it->xo_);
			fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
			// 2: sprite frame yoff
			wv = endianswap16(it->yo_);
			fwrite(&wv, 1, 2, hout); tracked_filesize += 2;


			// 2: occlusion map offset
			int omap_base_fpos = tracked_filesize;
			wv = 0;
			fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

			// 2: number of components in frame
			//wv = endianswap16(components);
			//fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

			if (_storepixels)
			{
				// width of plane in words
				int wordwidth = (it->w_ + (16 - 1)) >> 4;
				// size of plane in words
				int planewords = wordwidth * it->h_;

				// if preshifting is specified, additional indirection table used for preshifted sub-frames
				if (g_allargs.masklayout == Masklayout_PRESHIFT)
				{
					// spf_em_
					// spf_emx_

					// dual-field colour, two pointers
					std::vector<std::pair<int, int>> pscolr_index;
					std::vector<std::pair<int, int>> pscolropt_index;

					// preshifting is present so we need additional indirection for preshifted sub-frames
					std::map<std::pair<int, int>, int> psmask_component_index;
					std::map<std::pair<int, int>, int> psmaskopt_component_index;

					std::vector<std::list<int>> psmask_component_nextlinks;
					std::vector<std::list<int>> psmaskopt_component_nextlinks;

					/*
										// write N word offsets for occlusion x-preshifts
										int occlusion_preshift_index_fpos = tracked_filesize;
										for (std::vector<preshiftlist_t>::iterator itpm = it->psmask_frames_.begin(); itpm != it->psmask_frames_.end(); itpm++)
										{
										wv = 0;
										fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
										}
										*/
					// write offsets for plain colourdata (no preshifts yet)
					int colr_preshift_index_fpos = tracked_filesize;
					lv = 0;
					fwrite(&lv, 1, 4, hout); tracked_filesize += 4;
					fwrite(&lv, 1, 4, hout); tracked_filesize += 4;

					int colropt_preshift_index_fpos = tracked_filesize;
					if (g_allargs.cutmode == Cutmode_EMXSPR)
					{
						// write offsets for optimized colourdata (no preshifts yet)
						lv = 0;
						fwrite(&lv, 1, 4, hout); tracked_filesize += 4;
						fwrite(&lv, 1, 4, hout); tracked_filesize += 4;
					}

					// write N offsets for plain mask preshifts
					int mask_preshift_index_fpos = tracked_filesize;
					// write preshift set for number of required EMS sections (1 for 32-wide, 2 for 80-wide, 3 for 128-wide) 
					//std::cout << "patch mask index..." << std::endl << std::flush;
					//for (std::vector<preshiftlist_t>::iterator itpm = it->psmask_frames_.begin(); itpm != it->psmask_frames_.end(); itpm++)
					for (int s = 0; s < 16; s++)
					{
						lv = 0;
						fwrite(&lv, 1, 4, hout); tracked_filesize += 4;
					}

					int maskopt_preshift_index_fpos = tracked_filesize;
					if (g_allargs.cutmode == Cutmode_EMXSPR)
					{
						//std::cout << "output maskopt index..." << std::endl << std::flush;
						// write N offsets for optimized mask preshifts
						//for (std::vector<preshiftlist_t>::iterator itpm = it->psmask_frames_.begin(); itpm != it->psmask_frames_.end(); itpm++)
						for (int s = 0; s < 16; s++)
						{
							lv = 0xBBBBBBBB;
							fwrite(&lv, 1, 4, hout); tracked_filesize += 4;
						}
					}

					//---------------------------------------------------------------------------------------
					// occlusion maps

					if (!it->occlusion_maps_.empty())
					{
						// emit index - offsets from sprite frame to each occlusion map
						int omap_index_fpos = tracked_filesize;
						for (auto ocit = it->occlusion_maps_.begin(); ocit != it->occlusion_maps_.end(); ocit++)
						{
							wv = 0;
							fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
						}
						// emit occlusion maps
						std::vector<int> omap_data_index;
						for (auto ocit = it->occlusion_maps_.begin(); ocit != it->occlusion_maps_.end(); ocit++)
						{
							omap_data_index.push_back(tracked_filesize);
							// ocm height (lines)
							s16 lines = ocit->y2;
							wv = endianswap16(lines);
							fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

							for (int l = 0; l < lines; l++)
							{
								wv = endianswap16(ocit->tdata[l]);	// (T)
								fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
								wv = endianswap16(ocit->odata[l]);	// (O)
								fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
							}
						}

						{
							size_t save = ftell(hout);

							// patch omap index base offset
							{
								fseek(hout, omap_base_fpos + 4, 0);
								s32 offset = omap_index_fpos - omap_base_fpos;
								assert((offset < 65536) && "offset too large for occlusion map data");
								wv = s16(offset);
								wv = endianswap16(wv);
								fwrite(&wv, 1, 2, hout);
							}

							// patch omap index
							fseek(hout, omap_index_fpos + 4, 0);
							for (std::vector<int>::iterator it = omap_data_index.begin(); it != omap_data_index.end(); it++)
							{
								wv = s16((*it) - omap_index_fpos);
								wv = endianswap16(wv);
								fwrite(&wv, 1, 2, hout);
							}

							fseek(hout, save, 0);
						}
					}

					//---------------------------------------------------------------------------------------
					// EMH shared codegen

					//#if defined(USE_EMS2)
					std::map<ubyte_t*, int> mprog_fpos_map;

					if (g_allargs.cutmode == Cutmode_EMHSPR)
					{
						int preshift_index = 0;
						for (std::vector<preshiftlist_t>::iterator itpm = it->ems_.psmask_frames_.begin(); itpm != it->ems_.psmask_frames_.end(); itpm++, preshift_index++)
						{
							int component = 0;
							for (preshiftlist_t::iterator itc = itpm->begin(); itc != itpm->end(); itc++, component++)
							{
								auto & spshift = *itc;
								auto & shift = *spshift;

								if (shift.size68kbin)
								{
									// check if we've handled this binary yet (first in a series of identical preshifts?)
									auto look = mprog_fpos_map.find(shift.p68kbin);
									if (look == mprog_fpos_map.end())
									{
										// this is a new binary, although it may still duplicate another animation frame

										// ensure binary ends with RTS
										ubyte_t *binend = (ubyte_t*)(shift.p68kbin + (shift.size68kbin - 2));
										assert(binend[0] == 0x4e);
										assert(binend[1] == 0x75);

										// collapse identical codegen instances (optimization for animated sheets)
										u32 hash32 = hasher::hash<uword_t>((uword_t*)(shift.p68kbin+32), (shift.size68kbin-32)>>1);
										auto hashlook = mprog_hash_map.find(hash32);
										if (hashlook == mprog_hash_map.end())
										{
											// unique instance, not seen before: record in file, record position

											int filepos = tracked_filesize;
											mprog_hash_map[hash32] = filepos;

											fwrite((shift.p68kbin + 32), 1, (shift.size68kbin - 32), hout);

											// track position for this reference
											mprog_fpos_map[shift.p68kbin] = filepos;
											//mprog_fpos_remap[filepos] = filepos;

											tracked_filesize += (shift.size68kbin - 32);

											printf("output m68k shared metaprogram H[%08x] for EMH preshift %d component %d\n", hash32, preshift_index, component);
										}
										else
										{
											int remapped_filepos = hashlook->second;

											// duplicate instance, despite being a distinct reference - remap it
											mprog_fpos_map[shift.p68kbin] = remapped_filepos;

											printf("folded m68k shared metaprogram H[%08x] for EMH preshift %d component %d\n", hash32, preshift_index, component);
										}


										//mprog_fpos_map[h68kbin] = std::pair<int, preshift_t*>(tracked_filesize, &shift);

										//shift.file_offset = tracked_filesize;
									}
									else
									{
/*
										int real_fileoffset = look->second.first;
										preshift_t *preal_instance = look->second.second;

										// map duplicate to real instance
										shift.p68kbin = preal_instance->p68kbin;
										shift.size68kbin = preal_instance->size68kbin;
										shift.file_offset = preal_instance->file_offset;
*/
										// duplicate reference
									}
								}
								else
								{
									printf("error: no m68k code generated for EMH preshift %d component %d\n", preshift_index, component);
									exit(1);
								}
							}
						}
					}

/*
					int ems2_sharedmask_codegen_fpos = 0;
					if (g_allargs.cutmode == Cutmode_EMHSPR)
					{
						// spf_emh_psm_
						if (it->ems2_size68kbin_)
						{
							ems2_sharedmask_codegen_fpos = tracked_filesize;

							fwrite((it->ems2_p68kbin_ + 32), 1, (it->ems2_size68kbin_ - 32), hout);
							tracked_filesize += (it->ems2_size68kbin_ - 32);
						}
						else
						{
							printf("error: no m68k code generated for EMS2\n");
							exit(1);
						}
					}
*/
//#endif
					//---------------------------------------------------------------------------------------
					// plain colour 

					if ((g_allargs.cutmode == Cutmode_EMSPR) || 
						(g_allargs.cutmode == Cutmode_EMHSPR) || 
						// if EMX, still embed EMS when any kind of clipping is needed (default)
						// todo: for compound EMX, can assume [noclipx]
						!(g_allargs.noclipx && g_allargs.noclipy))
					{
						//std::cout << "output colour..." << std::endl << std::flush;
						// track colour preshifts
						// todo: assumes only one for now
						int pscolor0_pos = 0;
						int pscolor1_pos = 0;
						for (int n = 0; n < g_allargs.ccfields; n++)
						{
							if (n == 0)
								pscolor0_pos = tracked_filesize;
							else
								pscolor1_pos = tracked_filesize;

						// write word-width (will be 1, 2 or 3 in this mode for STE/F030 blitter endmasks)
						wv = endianswap16(wordwidth);
						fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

						// write colour words for N lines, lines(planes(words))) order
						for (int y = 0; y < it->h_; y++)
						{
							// reorder colour data if necessary
							int realy = y;
							//						if (g_allargs.cutmode == Cutmode_EMXSPR)
							//							realy = it->lineorder[y];

							if (g_allargs.cutmode == Cutmode_EMHSPR)
								realy = it->ems_.linemap_opt_2_src[y];

							for (int p = 0; p < g_allargs.planes; p++)
							{
								for (int x = 0; x < wordwidth; x++)
								{
										int offset = (((realy*wordwidth) + x)*g_allargs.planes) + p;
										if (n ==0)
											wv = endianswap16(it->colr0_[offset]);
										else
											wv = endianswap16(it->colr1_[offset]);

									fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
								}
							}
						}
					}

						pscolr_index.push_back(std::pair<int,int>(pscolor0_pos,pscolor1_pos));
					}

					// optimized colour
					if (g_allargs.cutmode == Cutmode_EMXSPR)
					{
						//std::cout << "output colouropt..." << std::endl << std::flush;
						// track colour preshifts
						// todo: assumes only one for now
						int pscoloropt0_pos = 0;
						int pscoloropt1_pos = 0;
						for (int n = 0; n < g_allargs.ccfields; n++)
						{
							if (n == 0)
								pscoloropt0_pos = tracked_filesize;
							else
								pscoloropt1_pos = tracked_filesize;

						// write word-width for this preshift (will be 1, 2 or 3 in this mode for STE/F030 blitter endmasks)
						//					wv = endianswap16(wordwidth);
						//					fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

						// write colour words for N lines, lines(planes(words))) order
						for (int y = 0; y < it->emx_.linemap_opt_2_src.size(); y++)
						{
							// reorder colour data if necessary
							int realy = y;
							//if (g_allargs.cutmode == Cutmode_EMXSPR)
							realy = it->emx_.linemap_opt_2_src[y];

							for (int p = 0; p < g_allargs.planes; p++)
							{
								for (int x = 0; x < wordwidth; x++)
								{
										int offset = (((realy*wordwidth) + x)*g_allargs.planes) + p;
										if (n == 0)
											wv = endianswap16(it->colr0_[offset]);
										else
											wv = endianswap16(it->colr1_[offset]);

									fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
								}
							}
						}
					}

						pscolropt_index.push_back(std::pair<int,int>(pscoloropt0_pos,pscoloropt1_pos));
					}

					//for (int o = 0; o < planewords; o++)
					//{
					//	// write colour words for N planes, interleaved
					//	for (int p = 0; p < g_allargs.planes; p++)
					//	{
					//		wv = endianswap16(it->colr_[(o*g_allargs.planes) + p]);
					//		fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
					//	}
					//}

					// plain mask preshifts 
					if ((g_allargs.cutmode == Cutmode_EMSPR) ||
						(g_allargs.cutmode == Cutmode_EMHSPR) ||
						// if EMX, still embed EMS when any kind of clipping is needed (default)
						// todo: for compound EMX, can ignore [noclipx]
						!(g_allargs.noclipx && g_allargs.noclipy))
					{
						//std::cout << "output mask..." << std::endl << std::flush;

						// track mask preshifts
						psmask_component_nextlinks.resize(it->ems_.psmask_frames_.size());
						int preshift_index = 0;
						for (std::vector<preshiftlist_t>::iterator itpm = it->ems_.psmask_frames_.begin(); itpm != it->ems_.psmask_frames_.end(); itpm++, preshift_index++)
						{
							int component = 0;
							for (preshiftlist_t::iterator itc = itpm->begin(); itc != itpm->end(); itc++, component++)
							{
								auto & spshift = *itc;

								psmask_component_index[std::pair<int, int>(preshift_index, component)] = tracked_filesize;
								int psmask_component_fpos = tracked_filesize;
								// spf_psm_
								// spf_emh_psm_

								// write word-width for this preshift (will be 1, 2 or 3 in this mode for STE/F030 blitter endmasks)
								wv = endianswap16(spshift->wordwidth_);
								fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

								if (g_allargs.cutmode == Cutmode_EMHSPR)
								{
									// component wordoffset
									wv = endianswap16(spshift->wordoffset_);
									fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

									// dest line skip for pagerestore
									int dstskip = ((20+1)+((g_allargs.emguardx+(16-1))>>4) - spshift->wordwidth_) << 3;
									wv = endianswap16(dstskip);
									fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

									// dest wordsize for pagerestore
									int dstwords = spshift->wordwidth_ << 2;
									wv = endianswap16(dstwords);
									fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

//#if defined(USE_EMS2)
									//const auto & ss = *(spshift->scan_states.begin());
//									int orig_y = it->ems_.linemap_opt_2_src[0];
//									const auto & ss = spshift->scan_states[orig_y];
//									const blitstate_t & bs = ss.blitstate;
//									assert(ss.allocated);

									const auto & ss = spshift->first_scan_state;
									const blitstate_t & bs = ss.blitstate;
									assert(ss.allocated);

									if (g_allargs.verbose)
										printf("\n");

									if (g_allargs.verbose)
										printf("mpo:%04x\n", (int)ss.mprog_offset);
									if (g_allargs.verbose)
										printf("dlo:%04x\n", (int)ss.dst_line_offset);
									if (g_allargs.verbose)
										printf("edo:%04x\n", (int)ss.emdata_offset);
									if (g_allargs.verbose)
										printf("sco:%04x\n", (int)ss.srccol_offset);

									assert(ss.mprog_offset == 0);
									assert(ss.emdata_offset == 0);
									assert(ss.srccol_offset == 0);

									// BLiTSKEW
									wv = u16(bs.SKEW);
									if (g_allargs.verbose)
										printf("BLiTSKEW:%04x\n", (int)wv);
									wv = endianswap16(wv);
									fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

									// BLiTSYI
									wv = u16(bs.SYI);
									if (g_allargs.verbose)
										printf("BLiTSYI:%04x\n", (int)wv);
									wv = endianswap16(wv);
									fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

									// BLiTDYI
									wv = u16(s16((s8)bs.DYI));
									if (g_allargs.verbose)
										printf("BLiTDYI:%04x\n", (u32)wv);
									wv = endianswap16(wv);
									fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

									// BLiTXC
									wv = u16(bs.XC);
									if (g_allargs.verbose)
										printf("BLiTXC:%04x\n", (int)wv);
									wv = endianswap16(wv);
									fwrite(&wv, 1, 2, hout); tracked_filesize += 2;								
//#else
								}
								else
								if ((g_allargs.cutmode == Cutmode_EMSPR) ||
									(g_allargs.cutmode == Cutmode_EMXSPR))
								{
									// blitter-specific fields (for EMS or y-clipped EMX)

									bool NFSR =
										(spshift->wordwidth_ > 1) && // NFSR is meaningless for wordwidth==1
										(spshift->wordsremaining_ == 0) &&
										(((it->w_ + 15) >> 4) != (spshift->wordwidth_ + spshift->wordoffset_));

									bool FISR = (spshift->wordoffset_ > 0);

									uword_t skew =
										(spshift->shift_ & 15) |
										(NFSR ? 0x40 : 0x00) |
										(FISR ? 0x80 : 0x00);

									word_t dyinc = (8 + 2) - (spshift->wordwidth_ << 3);

									// SYINC = difference between source wordwidth & preshift wordwidth, + 2
									// note: compensate for NFSR!
									word_t syinc =
										(((((it->w_ + 15) >> 4) - spshift->wordwidth_) << 1) + 2)
										+ (NFSR ? 2 : 0)
										- (FISR ? 2 : 0);

									// BLiTSKEW
									wv = endianswap16(skew);
									fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
									// BLiTSYI
									wv = endianswap16(syinc);
									fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
									// BLiTDYI
									wv = endianswap16(dyinc);
									fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
									// wordoffset
									wv = endianswap16(spshift->wordoffset_);
									fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
								}

								int psmask_component_emdata_offset = tracked_filesize;
								if (g_allargs.cutmode == Cutmode_EMHSPR)
								{
//#if defined(USE_EMS2)
									// spf_emh_psm_
									// spf_emh_psm_emdata
									// offset.w to emdata pool for this preshift
									wv = 0;
									fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

									// spf_emh_psm_code
									// offset.l to shared m68k mprog
									//int psmask_component_codegen_offset = tracked_filesize;
									// code already emitted, position known
									//lv = int(ems2_sharedmask_codegen_fpos - psmask_component_fpos);
									int ems2_sharedmask_codegen_fpos = mprog_fpos_map[spshift->p68kbin];

									// an additional remap can collapse indentical instances by their hash (for animation frames producing same codegen)
									//ems2_sharedmask_codegen_fpos = mprog_fpos_remap[ems2_sharedmask_codegen_fpos];

									lv = int(ems2_sharedmask_codegen_fpos - psmask_component_fpos);
									lv = endianswap32(lv);
									fwrite(&lv, 1, 4, hout); tracked_filesize += 4;
//#endif
								}

								//psmask_component_nextlinks[ems_component].push_back(tracked_filesize);
								psmask_component_nextlinks[preshift_index].push_back(tracked_filesize);
								lv = 0;
								fwrite(&lv, 1, 4, hout); tracked_filesize += 4;

								// psf_psm2_ end 
								// ---------------------------------------------------------------------------------------

								if (g_allargs.cutmode == Cutmode_EMHSPR)
								{
									// this defeats the point of EMH, making it bigger than EMX! y-clipping has been abandoned in favour of GUARDY
									if (USE_EMH_CLIPTABLE && !g_allargs.noclipy)
									{
										// ---------------------------------------------------------------------------------------
										// clip table (follows spf_emh_psm_)

										std::map<int, int> scan_index;
										std::map<int, int> remapping;
										int compressed_tide = 0;

										// clipdata index table (16 bits per scan)
										for (int so = 0; so < spshift->scan_states.size(); so++)
										{
											const auto & rsso = spshift->scan_states[so];

											int remapped_index = so;

											// find first match for all reusable fields
											for (int si = 0; si < so; si++)
											{
												// find first match for all reusable fields
												const auto & rssi = spshift->scan_states[si];

												if (rssi.alias_of(rsso))
												{
													remapped_index = si;
													break;
												}
											}

											int compressed_index = 0;

											if (remapped_index == so)
											{
												// distinct - encode new compressed index
												compressed_index = compressed_tide++;
												remapping[so] = compressed_index;
											}
											else
											{
												// reused/compressed
												compressed_index = remapping[remapped_index];
											}

											scan_index[so] = compressed_index;

											static const int c_indexentry_size = 2;
											static const int c_record_size = 18;

											int data_offset =
												(spshift->scan_states.size() * c_indexentry_size) +
												(compressed_index * c_record_size);

											if (g_allargs.verbose)
												printf("scan index: %d->%d o:%d\n", so, compressed_index, data_offset);

											wv = endianswap16(u16(data_offset));
											fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
										}

										//for (const auto & ss : spshift->scan_states)
										for (auto & me : remapping)
										{
											int so = me.first;
											const  auto & ss = spshift->scan_states[so];

											const blitstate_t & bs = ss.blitstate;
											/*
												u16 emdata_offset;
												u16 srccol_offset;
												u16 mprog_offset;
												u16 lines_remaining;
												u8 dst_line_offset;
												u8 dst_word_offset;
												u8 SYI;
												u8 DYI;
												u8 SKEW; // d1
												u8 XC;
												u16 EM1;
												u16 EM2;
												u16 EM3;
												*/
												// cpu
											wv = endianswap16(ss.emdata_offset);
											fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
											wv = endianswap16(ss.srccol_offset);
											fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
											wv = endianswap16(ss.mprog_offset);
											fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
											//wv = endianswap16(ss.lines_remaining);
											//fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
											fwrite(&ss.dst_line_offset, 1, 1, hout); tracked_filesize += 1;
											fwrite(&ss.dst_word_offset, 1, 1, hout); tracked_filesize += 1;
											// blitter
											fwrite(&bs.SYI, 1, 1, hout); tracked_filesize += 1;
											fwrite(&bs.DYI, 1, 1, hout); tracked_filesize += 1;
											fwrite(&bs.SKEW, 1, 1, hout); tracked_filesize += 1;
											fwrite(&bs.XC, 1, 1, hout); tracked_filesize += 1;
											wv = endianswap16(bs.EM1);
											fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
											wv = endianswap16(bs.EM2);
											fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
											wv = endianswap16(bs.EM3);
											fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
										}
									} // USE_EMH_CLIPTABLE

									// ---------------------------------------------------------------------------------------
									// EM data
									int psmask_component_emdata_fpos = tracked_filesize;
									for (auto & w : spshift->shared_mask_words)
									{
										if (g_allargs.verbose)
										{
											print_dataword(w);
											printf("\n");
										}
										wv = endianswap16(w);
										fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
									}

									// immediately patch offset to EM data
									{
										size_t save = ftell(hout);
										fseek(hout, psmask_component_emdata_offset + 4, 0);
										{
											// offset to EM data from this component
											wv = int(psmask_component_emdata_fpos - psmask_component_fpos);
											wv = endianswap16(wv);
											fwrite(&wv, 1, 2, hout);
										}
										fseek(hout, save, 0);
									}

								}
								else
								{

									//if (g_allargs.cutmode == Cutmode_EMXSPR)
									//{
									//	fwrite(fname.c_str(), 1, 32, hout); tracked_filesize += 32;

									//	// write 68k binary code for this preshift
									//	fwrite((itm->p68kbin + 32), 1, (itm->size68kbin - 32), hout);
									//	tracked_filesize += (itm->size68kbin - 32);
									//}
									//else
									{
										int wordcounter = 0;

										// write whole mask preshift
										//std::cout << "mask:" << std::endl;
										for (int o = 0; o < spshift->planewords_; o++)
										{
											// write colour words for single mask plane
											uword_t data = spshift->data_[o];

											if (0)
												if (component > 0)
												{
													for (int x = 0; x < 16; x++)
													{
														if (data & 0x8000)
															std::cout << "1";
														else
															std::cout << "0";
														data <<= 1;
													}
													std::cout << " ";
													wordcounter++;
													if (wordcounter == spshift->wordwidth_)
													{
														std::cout << std::endl;
														wordcounter = 0;
													}
												}

											wv = endianswap16(spshift->data_[o]);
											fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
										}
									}
								}
//#endif

							}
						}
					}

					// track mask preshifts
					if (g_allargs.cutmode == Cutmode_EMXSPR)
					{
						//std::cout << "output maskopt..." << std::endl << std::flush;

						psmaskopt_component_nextlinks.resize(it->emx_.psmask_frames_.size());
						int preshift_index = 0;
						for (std::vector<preshiftlist_t>::iterator itpm = it->emx_.psmask_frames_.begin(); itpm != it->emx_.psmask_frames_.end(); itpm++, preshift_index++)
						{
							int component = 0;
							for (preshiftlist_t::iterator itc = itpm->begin(); itc != itpm->end(); itc++, component++)
							{
								// spf_emx_psm_
								auto & spshift = *itc;

								//psmaskopt_component_index[emx_component].push_back(tracked_filesize);
								psmaskopt_component_index[std::pair<int,int>(preshift_index,component)] = tracked_filesize;

								// write word-width for this preshift (will be 1, 2 or 3 in this mode for STE/F030 blitter endmasks)
								wv = endianswap16(spshift->wordwidth_);
								fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

								// component wordoffset
								wv = endianswap16(spshift->wordoffset_);
								fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

								// dest line skip for pagerestore
								int dstskip = ((20+1)+((g_allargs.emguardx+(16-1))>>4) - spshift->wordwidth_) << 3;
								wv = endianswap16(dstskip);
								fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

								// dest wordsize for pagerestore
								int dstwords = spshift->wordwidth_ << 2;
								wv = endianswap16(dstwords);
								fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

								//psmaskopt_component_nextlinks[emx_component].push_back(tracked_filesize);
								psmaskopt_component_nextlinks[preshift_index].push_back(tracked_filesize);
								lv = 0;
								fwrite(&lv, 1, 4, hout); tracked_filesize += 4;

								//					if (g_allargs.cutmode == Cutmode_EMXSPR)
								{
									//fwrite(fname.c_str(), 1, 32, hout); tracked_filesize += 32;

									// write 68k binary code for this preshift
									fwrite((spshift->p68kbin + 32), 1, (spshift->size68kbin - 32), hout);
									tracked_filesize += (spshift->size68kbin - 32);
								}
								//else
								//{
								//	// write whole mask preshift
								//	for (int o = 0; o < itm->planewords_; o++)
								//	{
								//		// write colour words for single mask plane
								//		wv = endianswap16(itm->data_[o]);
								//		fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
								//	}
								//}
							}
						}
					}

					// patch colour preshift index
					if ((g_allargs.cutmode == Cutmode_EMSPR) ||
						(g_allargs.cutmode == Cutmode_EMHSPR) ||
						// if EMX, still embed EMS when any kind of clipping is needed (default)
						// todo: for compound EMX, can ignore [noclipx]
						!(g_allargs.noclipx && g_allargs.noclipy))
					{
						lv = 0;
						size_t save = ftell(hout);
						fseek(hout, colr_preshift_index_fpos + 4, 0);
						for (std::vector<std::pair<int,int>>::iterator it = pscolr_index.begin(); it != pscolr_index.end(); it++)
						{
							// 1st field
							//std::cout << "patching 1st field pscolor" << std::endl;
							lv = it->first - colr_preshift_index_fpos;
							lv = endianswap32(lv);
							fwrite(&lv, 1, 4, hout);

							// 2nd field is duplicate reference of 1st, if no dual-field
							if (it->second != 0)
							{
								//std::cout << "patching 2nd field pscolor" << std::endl;
								lv = it->second - colr_preshift_index_fpos;
								lv = endianswap32(lv);
							}
							fwrite(&lv, 1, 4, hout);
						}
						fseek(hout, save, 0);
					}

					// patch colouropt preshift index
					if (g_allargs.cutmode == Cutmode_EMXSPR)
					{
						lv = 0;
						size_t save = ftell(hout);
						fseek(hout, colropt_preshift_index_fpos + 4, 0);
						for (std::vector<std::pair<int,int>>::iterator it = pscolropt_index.begin(); it != pscolropt_index.end(); it++)
						{
							// 1st field
							//std::cout << "patching 1st field pscoloropt" << std::endl;
							lv = it->first - colropt_preshift_index_fpos;
							lv = endianswap32(lv);
							fwrite(&lv, 1, 4, hout);

							// 2nd field is duplicate reference of 1st, if no dual-field
							if (it->second != 0)
							{
								//std::cout << "patching 2nd field pscoloropt" << std::endl;
								lv = it->second - colropt_preshift_index_fpos;
								lv = endianswap32(lv);
							}
							fwrite(&lv, 1, 4, hout);
						}
						fseek(hout, save, 0);
					}

					// patch mask preshift index
					if ((g_allargs.cutmode == Cutmode_EMSPR) ||
						(g_allargs.cutmode == Cutmode_EMHSPR) ||
						// if EMX, still embed EMS when any kind of clipping is needed (default)
						// todo: for compound EMX, can ignore [noclipx]
						!(g_allargs.noclipx && g_allargs.noclipy))
					{
						lv = 0;
						size_t save = ftell(hout);
						fseek(hout, mask_preshift_index_fpos + 4, 0);
						for (std::map<std::pair<int,int>, int>::iterator it = psmask_component_index.begin(); it != psmask_component_index.end(); it++)
						{
							int preshift_index = it->first.first;
							int component = it->first.second;
							if (component == 0)
							{
								lv = (it->second) - mask_preshift_index_fpos;
								lv = endianswap32(lv);

								// duplicate references for preshift steps > 1. index should always have 16 entries.
								for (int s = 0; s < g_allargs.preshift_step; s++)
								{
									int preshift_entry = (preshift_index << g_allargs.preshift_scale) + s;
									fseek(hout, mask_preshift_index_fpos + (preshift_entry << 2) + 4, 0);
									fwrite(&lv, 1, 4, hout);
								}
							}
						}

						// links between components for each preshift
						int preshift_index = 0;
						for (std::vector<std::list<int>>::iterator pit = psmask_component_nextlinks.begin(); pit != psmask_component_nextlinks.end(); pit++, preshift_index++)
						{
							int component = 0;
							for (std::list<int>::iterator cmplnkit = pit->begin(); cmplnkit != pit->end(); cmplnkit++, component++)
							{
								int patch_offset = *cmplnkit;
								auto linkfrom = psmask_component_index.find(std::pair<int,int>(preshift_index, component));
								assert(linkfrom != psmask_component_index.end());
								auto linkto = psmask_component_index.find(std::pair<int,int>(preshift_index, component+1));
								if (linkto != psmask_component_index.end())
								{
									int patch_reference = linkto->second;
									int patch_ref_base = linkfrom->second;
									fseek(hout, patch_offset + 4, 0);
									lv = patch_reference - patch_ref_base;
									lv = endianswap32(lv);
									fwrite(&lv, 1, 4, hout);
								}
							}
						}
						fseek(hout, save, 0);
					}

					// patch maskopt preshift index
					if (g_allargs.cutmode == Cutmode_EMXSPR)
					{
						//std::cout << "patch maskopt index..." << std::endl << std::flush;
						lv = 0;
						size_t save = ftell(hout);
						fseek(hout, maskopt_preshift_index_fpos + 4, 0);

						for (std::map<std::pair<int,int>, int>::iterator it = psmaskopt_component_index.begin(); it != psmaskopt_component_index.end(); it++)
						{
							int preshift_index = it->first.first;
							int component = it->first.second;
							if (component == 0)
							{
								lv = (it->second) - maskopt_preshift_index_fpos;
								lv = endianswap32(lv);
								// duplicate references for preshift steps > 1. index should always have 16 entries.
								for (int s = 0; s < g_allargs.preshift_step; s++)
								{
									int preshift_entry = (preshift_index << g_allargs.preshift_scale) + s;
									fseek(hout, maskopt_preshift_index_fpos + (preshift_entry << 2) + 4, 0);
									fwrite(&lv, 1, 4, hout);
								}
							}
						}

						//std::cout << "patch maskopt links..." << std::endl << std::flush;
						int preshift_index = 0;
						for (std::vector<std::list<int>>::iterator pit = psmaskopt_component_nextlinks.begin(); pit != psmaskopt_component_nextlinks.end(); pit++, preshift_index++)
						{
							int component = 0;
							for (std::list<int>::iterator cmplnkit = pit->begin(); cmplnkit != pit->end(); cmplnkit++, component++)
							{
								//printf("psi:%d cmp:%d\n", preshift_index, component);

								int patch_offset = *cmplnkit;
								auto linkfrom = psmaskopt_component_index.find(std::pair<int,int>(preshift_index, component));
								assert(linkfrom != psmaskopt_component_index.end());
								auto linkto = psmaskopt_component_index.find(std::pair<int,int>(preshift_index, component+1));
								if (linkto != psmaskopt_component_index.end())
								{
									int patch_reference = linkto->second;
									int patch_ref_base = linkfrom->second;
									fseek(hout, patch_offset + 4, 0);
									lv = patch_reference - patch_ref_base;
									lv = endianswap32(lv);
									fwrite(&lv, 1, 4, hout);
								}
							}
						}
						fseek(hout, save, 0);
						//std::cout << "patch maskopt done..." << std::endl << std::flush;
					}

				}
				else // !preshift
				{
					// immediate data follows - no preshifting present

					// write mask,colr[n] words

					// in planar-mask mode, we emit the entire mask before the colour data planes
					if (g_allargs.genmask && (g_allargs.masklayout == Masklayout_PLANAR))
					{
						for (int o = 0; o < planewords; o++)
						{
							// write mask plane data
							wv = endianswap16(it->mask_[o]);
							fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
						}
					}

					for (int o = 0; o < planewords; o++)
					{
						if (g_allargs.genmask && (g_allargs.masklayout == Masklayout_INTERLEAVED))
						{
							// write interleaved mask word BEFORE each group of colour plane words
							wv = endianswap16(it->mask_[o]);
							fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
						}

						// write colour words for N planes
						for (int p = 0; p < g_allargs.planes; p++)
						{
							wv = endianswap16(it->colr0_[(o*g_allargs.planes) + p]);
							fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
						}
					}
				} // preshift
			} // storepixels
		} // spriteframes

		// patch sprite index
		lv = 0;
		fseek(hout, sprite_index_fpos+4, SEEK_SET);
		for (std::vector<int>::iterator it = sprite_index.begin(); it != sprite_index.end(); it++)
		{
			lv = (*it) - sprite_index_fpos;
			lv = endianswap32(lv);
			fwrite(&lv, 1, 4, hout);
		}

		// patch filesize
		fseek(hout, 0, SEEK_SET);
		lv = endianswap32(tracked_filesize);
		fwrite(&lv, 1, 4, hout);

		// last, generate CRC
		update_file_crc(hout, /*crc_offset=*/4);
		
		fclose(hout);
	}

	std::cout << "emitted sprite sequence [" << fname.c_str() << "]" << std::endl;

	return true;
}

// --------------------------------------------------------------------
//	slab mode
// --------------------------------------------------------------------

class slabspan_t
{
public:

	word_t xo_;
	word_t xs_;
	word_t dxw_;
	word_t y_;
	word_t dy_;
	word_t ys_;
	uword_t *pspan_;

	slabspan_t(int _xo, int _xs, int _dxw, int _y, int _dy, int _ys, uword_t *_pspan)
		: xo_(_xo),
		  xs_(_xs),
		  dxw_(_dxw),
		  y_(_y),
		  dy_(_dy),
		  ys_(_ys),
		  pspan_(_pspan)
	{
	}
};

class slabframe_t
{
public:

	word_t w_, h_;
	word_t xo_, yo_;
	uword_t *colr_;
	slabframe_t *nextcmp_;

	slabframe_t(int _w, int _h, int _xo, int _yo, uword_t *_colr)
		: w_(_w),
		  h_(_h),
		  xo_(_xo),
		  yo_(_yo),
		  colr_(_colr),
		  nextcmp_(nullptr)
	{
	}

	~slabframe_t()
	{
		// do not delete here - no refcounting on allocations happening within STL container
	}

	void release()
	{
		delete[] colr_; colr_ = 0;
		if (nextcmp_)
			nextcmp_->release();
	}

	std::list<slabspan_t> spans_;
};

typedef std::list<slabframe_t> slablist_t;

// todo: tidy this up!

slablist_t g_slabframes;

// --------------------------------------------------------------------
//	slab mode
// --------------------------------------------------------------------

void init_slab_sequence(int _xs, int _ys, int _sw, int _sh)
{
	for (slablist_t::iterator it = g_slabframes.begin(); it != g_slabframes.end(); it++)
		it->release();

	g_slabframes.clear();

	g_spritesource_w = _sw;
	g_spritesource_h = _sh;

	// worst-case width in bitplane words
	g_sprite_bpwidth = (_xs + (16-1)) & -16;
	g_sprite_width = _xs;
	g_sprite_height = _ys;


	// allocate sprite buffer for chunky processing (default to keycol)
	delete[] g_colrbuffer8;
	g_colrbuffer8 = new ubyte_t[g_sprite_bpwidth * g_sprite_height];
	memset(g_colrbuffer8, g_allargs.keycol, g_sprite_bpwidth * g_sprite_height);

	// allocate sprite buffer for bitplane processing
	delete[] g_colrbufferbpl;
	g_colrbufferbpl = new uword_t[(g_sprite_bpwidth * g_sprite_height) >> 1];
	memset(g_colrbufferbpl, 0, g_sprite_bpwidth * g_sprite_height);

	// allocate sprite buffer for chunky processing (default to keycol)
	delete[] g_workbuffer8;
	g_workbuffer8 = new ubyte_t[g_sprite_bpwidth * g_sprite_height];
	memset(g_workbuffer8, g_allargs.keycol, g_sprite_bpwidth * g_sprite_height);

	// allocate sprite buffer for bitplane processing
	delete[] g_workbufferbpl;
	g_workbufferbpl = new uword_t[(g_sprite_bpwidth * g_sprite_height) >> 1];
	memset(g_workbufferbpl, 0, g_sprite_bpwidth * g_sprite_height);
}

void extract_spanblock(slabframe_t &slab, int _spanblock_firstx, int _spanblock_lastx, int _spanblock_firsty, int _spanblock_lasty, int _spanblock_gapy, int cxmax)
{
	int spanblock_lines = _spanblock_lasty - _spanblock_firsty;

	assert(spanblock_lines > 0);

	// left-justify span to remove leading keycolour pixels
	for (int ey = _spanblock_firsty; ey < _spanblock_lasty; ey++)
	{
		int dstx = 0;
		for (int px = _spanblock_firstx; px <= _spanblock_lastx; px++, dstx++)
		{
			// extract pixel
			ubyte_t c = g_colrbuffer8[(ey * g_sprite_framebpwidth) + px];
			// erase pixel with keycol
			g_colrbuffer8[(ey * g_sprite_framebpwidth) + px] = g_allargs.keycol;
			// transfer to workbuffer, left-justified
			g_workbuffer8[(ey * g_sprite_framebpwidth) + dstx] = c;
		}

		// fill remainder of workbuffer spanblock with 0
		for (; dstx < cxmax; dstx++)
		{
			g_workbuffer8[(ey * g_sprite_framebpwidth) + dstx] = 0;
		}
	}

	// chunky -> planar
	c2pw
	(
		/*src=*/&g_workbuffer8[_spanblock_firsty * g_sprite_framebpwidth], /*dst*/&g_workbufferbpl[(_spanblock_firsty * g_sprite_framebpwidth) >> 1],
		/*w=*/g_sprite_framebpwidth, /*h=*/spanblock_lines,
		/*bytes_per_src_line=*/g_sprite_framebpwidth, /*words_per_dst_line=*/g_sprite_framebpwidth >> 1,
		/*planes=*/8
	);


	int spanlength = (_spanblock_lastx - _spanblock_firstx) + 1;
	//std::cerr << "slen: " << spanlength << std::endl;
	int spanlength16 = (spanlength + (16 - 1)) & -16;
	int spanlengthwords = spanlength16 >> 4;

	// space for spanblock lines @ 8bpl
	uword_t *spanwords = new uword_t[(spanlengthwords * 8) * spanblock_lines];

	// copy 8-plane span lines into slab span container
	for (int ey = _spanblock_firsty, dsty = 0; ey < _spanblock_lasty; ey++, dsty++)
	{
		memcpy(&spanwords[(spanlengthwords * 8) * dsty], &g_workbufferbpl[(ey * g_sprite_framebpwidth) >> 1], spanlength16);
	}

	// store slab span
	slab.spans_.push_back
	(
		slabspan_t
		(
			_spanblock_firstx, spanlength, spanlengthwords, 
			_spanblock_firsty, _spanblock_gapy, spanblock_lines, 
			spanwords
		)
	);
}


void add_slab_frame(ubyte_t* _psource8, ubyte_t* _pframemask, int _x, int _y)
{
	memset(g_colrbuffer8, g_allargs.keycol, g_sprite_bpwidth * g_sprite_height);

	int frame = g_slabframes.size() + 1;

	int cxmin = 0;
	int cymin = 0;
	int cxmax = g_sprite_width;
	int cymax = g_sprite_height;

	if (g_allargs.preview)
	{
		printf("\nsource:\n");
		for (int py = 0; py < g_sprite_height; py++)
		{
			printf("\t");
			for (int px = 0; px < g_sprite_width; px++)
			{
				ubyte_t c = _psource8[((py + _y) * g_spritesource_w) + (px + _x)];

				if ((_pframemask == nullptr) || (_pframemask[((py + _y) * g_spritesource_w) + (px + _x)] == frame))
					;
				else
					c = g_allargs.keycol;

				if (c == g_allargs.keycol)
					printf(" ");
				else
					printf("%x", (int)(c & 0xF));
			}
			printf("\n");
		}
		printf("\n");
	}

	//if (g_allargs.spriteoptimize)
	// always optimize the slab region 
	{
		cxmin = g_sprite_width;
		cymin = g_sprite_height;
		cxmax = 0;
		cymax = 0;

		// optimize cutting area

		for (int py = 0; py < g_sprite_height; py++)
		{
			for (int px = 0; px < g_sprite_width; px++)
			{
				ubyte_t c = _psource8[((py + _y) * g_spritesource_w) + (px + _x)];

				if ((_pframemask == nullptr) || (_pframemask[((py + _y) * g_spritesource_w) + (px + _x)] == frame))
					;
				else
					c = g_allargs.keycol;

				if (c != g_allargs.keycol)
				{
					// opaque pixel - adjust bounds
					cxmin = xmin(cxmin, px);
					cxmax = xmax(cxmax, px + 1);
					cymin = xmin(cymin, py);
					cymax = xmax(cymax, py + 1);
				}
			}
		}
	}

	// capture at least one pixel per frame
	if ((cxmax < cxmin) || (cymax < cymin))
	{
		cxmin = 0;
		cxmax = 0;
		cymin = 0;
		cymax = 0;

		std::cout << "warning: empty slab frame!" << std::endl;
	}

	g_sprite_framewidth = cxmax - cxmin;
	g_sprite_frameheight = cymax - cymin;

	// this gets calculated again for each individual span
	g_sprite_framebpwidth = (g_sprite_framewidth + (16-1)) & -16;

	// cut chunky sprite to chunky colrbuffer & maskbuffer

	for (int psy = cymin, py = 0; psy < cymax; psy++, py++)
	{
		for (int psx = cxmin, px = 0; psx < cxmax; psx++, px++)
		{
			ubyte_t c = _psource8[((psy + _y) * g_spritesource_w) + (psx + _x)];

			if ((_pframemask == nullptr) || (_pframemask[((py + _y) * g_spritesource_w) + (px + _x)] == frame))
				;
			else
				c = g_allargs.keycol;

			// colour buffer receives actual colour, or 0 for keycol (so two colours can potentially map to 0)
			g_colrbuffer8[(py * g_sprite_framebpwidth) + px] = c;// (c == g_allargs.keycol) ? 0x00 : c;
		}
	}

	slabframe_t slabframe_head
	(
		g_sprite_framewidth, 
		g_sprite_frameheight, 
		cxmin, cymin, 
		nullptr
	);
	slabframe_t *pframecomponent = &slabframe_head;

	bool components_remaining = true;
	while (components_remaining)
	{
		int spanblock_firstx = -1;
		int spanblock_lastx = -2;
		int spanblock_firsty = 0;
		int spanblock_lasty = 0;
		int prevlasty = 0;

		// process individual spans
		for (int psy = cymin, py = 0; psy < cymax; psy++, py++)
		{
			// find begin/end of solid part of span
			int firstx = 0;
			for (int psx = cxmin; psx < cxmax; psx++, firstx++)
			{
				ubyte_t c = g_colrbuffer8[(py * g_sprite_framebpwidth) + firstx];
				if (c != g_allargs.keycol) break;
			}

			int lastx;
			// if cutting compound slabs, extract piecewise from the left side, interrupted by keycol
			if (g_allargs.compound_slabs)
			{
				lastx = firstx;
				for (int psx = lastx; psx < cxmax; psx++, lastx++)
				{
					ubyte_t c = g_colrbuffer8[(py * g_sprite_framebpwidth) + lastx];
					if (c == g_allargs.keycol) break;
				}
				lastx--;
			}
			else
				// otherwise extract everything as a single component, between left and rightmost edge
			{
				lastx = (cxmax-cxmin) - 1;
				//for (int psx = cxmin; psx < cxmax; psx++, lastx--)
				for (; lastx >= 0; lastx--)
				{
					ubyte_t c = g_colrbuffer8[(py * g_sprite_framebpwidth) + lastx];
					if (c != g_allargs.keycol) break;
				}
			}

			// terminate spanblock if ends change
			if ((spanblock_firstx != firstx) || (spanblock_lastx != lastx))
			{
				if (spanblock_lastx >= spanblock_firstx)
				{
					// completion of non-empty spanblock

					printf("fx:%d lx:%d xs:%d\n", spanblock_firstx, spanblock_lastx, (spanblock_lastx - spanblock_firstx) + 1);
					// extract spanblock, left-justified to workbuffer and erase original (for multiple extractions)
					extract_spanblock
					(
						*pframecomponent, 
						spanblock_firstx, spanblock_lastx, 
						spanblock_firsty, spanblock_lasty, 
						(spanblock_firsty - prevlasty), 
						cxmax
					);
					prevlasty = spanblock_lasty;
				}
				else
				{
					// completion of empty block
				}

				// reset run
				spanblock_firstx = firstx;
				spanblock_lastx = lastx;
				spanblock_firsty = py;
			}
			else
			{
				// continue run
			}

			spanblock_lasty++;
		}

		if (spanblock_lastx >= spanblock_firstx)
		{
			// final spanblock (if any)

			// extract spanblock, left-justified to workbuffer and erase original (for multiple extractions)
			extract_spanblock
			(
				*pframecomponent, 
				spanblock_firstx, spanblock_lastx, 
				spanblock_firsty, spanblock_lasty, 
				(spanblock_firsty - prevlasty), 
				cxmax
			);
		}

		if (g_allargs.preview)
		{
			printf("\nreconstruction:\n");
			//

			int xx = pframecomponent->xo_;
			int yy = pframecomponent->yo_;
			for (auto & s : pframecomponent->spans_)
			{
				// optional lineskip
				for (int py = yy; py < (yy + s.dy_); py++)
				{
					printf("\t");
					for (int t = 0; t < g_sprite_framewidth; t++)
						printf("-");
					printf("\n");
				}
				yy += s.dy_;

				// body
				for (int py = yy; py < (yy + s.ys_); py++)
				{
					int xxo = xx + s.xo_;
					printf("\t");
					int t = 0;
					for (; t < xx; t++)
						printf("~");
					for (; t < xxo; t++)
						printf(".");

					int px = xxo;
					for (; px < (xxo + s.xs_); px++)
					{
						ubyte_t c = _psource8[((py + _y) * g_spritesource_w) + (px + _x)];

						//printf("x");
						if (c == g_allargs.keycol)
							printf("*");
						else
							printf("%x", (int)(c & 0xF));
					}
					for (int t = px; t < g_sprite_framewidth; t++)
						printf(".");

					printf("\n");
				}

				yy += s.ys_;
			}
			printf("\n<<<\n");
		}

		if (g_allargs.preview)
		{
			printf("\nsource updated:\n");
			for (int y = cymin; (y < cymax); y++)
			{
				for (int x = cxmin; x < cxmax; x++)
				{
					ubyte_t c = g_colrbuffer8[(y * g_sprite_framebpwidth) + x];

					if (c == g_allargs.keycol)
						printf(" ");
					else
						printf("%x", (int)(c & 0x0F));
				}
				printf("\n");
			}
			printf("\n");
		}

		components_remaining = false;

		if (g_allargs.compound_slabs)
		{
			// look for any un-cut pixels in g_colrbuffer8
			for (int y = cymin; (y < cymax) && !components_remaining; y++)
			{
				for (int x = cxmin; x < cxmax; x++)
				{
					ubyte_t c = g_colrbuffer8[(y * g_sprite_framebpwidth) + x];
					if (c != g_allargs.keycol)
					{
						components_remaining = true;
						std::cout << "slab components remaining - performing another cut pass..." << std::endl;
						//getchar();


						int live_height = -1;

						// find live height of optimized frame area
						// we don't want the last spanblock to extend beyond the last live line since dy skips are before a spanblock, not after
						for (int py = (cymax-cymin)-1; (py >= 0) && (live_height < 0); py--)
						{
							for (int px = 0; px < (cxmax-cxmin); px++)
							{
								ubyte_t c = g_colrbuffer8[(py * g_sprite_framebpwidth) + px];
								if (c != g_allargs.keycol)
								{
									live_height = py+1;
									break;
								}
							}
						}

						int cymax = cymin + live_height;

						g_sprite_frameheight = cymax - cymin;


						// continue chain of components
						slabframe_t *pnextcmp = new slabframe_t
						(
							g_sprite_framewidth, 
							g_sprite_frameheight, 
							cxmin, cymin, 
							nullptr
						);
						pframecomponent->nextcmp_ = pnextcmp;
						pframecomponent = pnextcmp;

						break;
					}
				}
			}
		}

//		components_remaining = false;
	}

	std::cout << "created slab frame " << g_slabframes.size() << " from offset [" << cxmin << ", " << cymin << "] size [" << g_sprite_framewidth << " x " << g_sprite_frameheight << "]" << std::endl;
	g_slabframes.push_back(slabframe_head);
}

static inline ubyte_t advance_block_encoding(ubyte_t cc, ubyte_t k)
{
	cc = (cc + 1) & 255;
	if (cc == k)
		cc = (cc + 1) & 255;
	cc |= 0x80; // avoid input colour range (which we can expect to be very limited, for SLR input shapes)
	return cc;
}

void add_slabrestore_frame(ubyte_t* _psource8, ubyte_t* _pframemask, int _x, int _y)
{
	// preprocess slab to produce fewer, less precise spanblock regions for accelerated clearing

	int frame = g_slabframes.size() + 1;

	int cxmin = 0;
	int cymin = 0;
	int cxmax = g_sprite_width;
	int cymax = g_sprite_height;

	if (1)
	{
		cxmin = g_sprite_width;
		cymin = g_sprite_height;
		cxmax = 0;
		cymax = 0;

		// optimize cutting area

		for (int py = 0; py < g_sprite_height; py++)
		{
			for (int px = 0; px < g_sprite_width; px++)
			{
				ubyte_t c = _psource8[((py + _y) * g_spritesource_w) + (px + _x)];

				if ((c != g_allargs.keycol) && 
					((_pframemask == nullptr) || (_pframemask[((py + _y) * g_spritesource_w) + (px + _x)] == frame))
					)
				{
					// opaque pixel - adjust bounds
					cxmin = xmin(cxmin, px);
					cxmax = xmax(cxmax, px + 1);
					cymin = xmin(cymin, py);
					cymax = xmax(cymax, py + 1);
				}
			}
		}
	}

	if ((cxmax < cxmin) || (cymax < cymin))
	{
		cxmin = 0;
		cxmax = 0;
		cymin = 0;
		cymax = 0;

		std::cout << "warning: empty slabrestore frame!" << std::endl;

		// now process the quantised frame normally, for an empty frame
		add_slab_frame(_psource8, _pframemask, _x, _y);
	}
	else // !degenerate
	{
		// transfer pixel block to work area for preprocessing

		int workarea_w = (g_sprite_width + 15) & -16;
		int workarea_h = g_sprite_height;

		ubyte_t * workarea = new ubyte_t[workarea_w * workarea_h];
		memset(workarea, g_allargs.keycol, workarea_w * workarea_h);

		for (int py = 0; py < g_sprite_height; py++)
		{
			// find first solid pixel
			int px = 0;
			for (; px < g_sprite_width; px++)
			{
				ubyte_t c = _psource8[((py + _y) * g_spritesource_w) + (px + _x)];
				
				if ((_pframemask == nullptr) || (_pframemask[((py + _y) * g_spritesource_w) + (px + _x)] == frame))
					;
				else
					c = g_allargs.keycol;

				workarea[((py + 0) * workarea_w) + (px + 0)] = c;
			}
		}

		// set workarea as temporary source
		int tmp_spritesource_w = g_spritesource_w;

		g_spritesource_w = workarea_w;
		_psource8 = workarea;
		_x = 0;
		_y = 0;

		// 1) quantise spans to 16 pixel (planeword) boundaries
		{
			for (int py = 0; py < g_sprite_height; py++)
			{
				// find first solid pixel
				int px = 0;
				for (; px < g_sprite_width; px++)
				{
					ubyte_t c = _psource8[((py + _y) * g_spritesource_w) + (px + _x)];

					if (c != g_allargs.keycol)
						break;
				}

				// round towards left
				int left = px & -16;

				// find last solid pixel
				for (px = g_sprite_width - 1; px >= 0; px--)
				{
					ubyte_t c = _psource8[((py + _y) * g_spritesource_w) + (px + _x)];

					if (c != g_allargs.keycol)
						break;
				}

				// last pixel exclusive
				px++;

				// round towards right
				int right = (px + 15) & -16;

				// clamp
				if (right > g_spritesource_w)
					right = g_spritesource_w;

				// choose random colours for debugging the regions
				//			int cc = py & 15;
				//			if (cc == g_allargs.keycol)
				//				cc = 15 - cc;

				ubyte_t cc = g_allargs.keycol + 1;

				// replace colour for quantised region
				px = left;
				for (; px < right; px++)
				{
					// final colour doesn't matter, so long as its not keycol
					_psource8[((py + _y) * g_spritesource_w) + (px + _x)] = cc;
				}
			}
		}

		// 2) reduce fragmentation of quantised regions
		{
			ubyte_t k = g_allargs.keycol;
			ubyte_t s = k + 1;
			ubyte_t b = s + 1;
			// process columns, not rows
			for (int px = 0; px < g_sprite_width; px += 16)
			{
				// single column
				for (int py = 0; py < g_sprite_height; py++)
				{
					// for each word, look at the above/below flanking word pattern and blur

					// this whole sequence is crap, but it'll do

					int pym2 = xmax(py - 2, 0);
					int pym1 = xmax(py - 1, 0);
					int pyp1 = xmin(py + 1, g_sprite_height - 1);
					int pyp2 = xmin(py + 2, g_sprite_height - 1);

					ubyte_t cm2 = _psource8[((pym2 + _y) * g_spritesource_w) + (px + _x)];
					ubyte_t cm1 = _psource8[((pym1 + _y) * g_spritesource_w) + (px + _x)];
					ubyte_t c00 = _psource8[((py + _y) * g_spritesource_w) + (px + _x)];
					ubyte_t cp1 = _psource8[((pyp1 + _y) * g_spritesource_w) + (px + _x)];
					ubyte_t cp2 = _psource8[((pyp2 + _y) * g_spritesource_w) + (px + _x)];

					if (c00 == k)
					{
						if ((cm1 == s && cp2 == s) ||
							(cm2 == s && cp1 == s) ||
							(cm1 == s && cp1 == s))
						{
							for (int x = px; x < px + 16; x++)
							{
								_psource8[((py + _y) * g_spritesource_w) + (x + _x)] = b;
							}
						}
					}
				}
			}
		}

		std::vector<std::pair<int, int> > fragments;
		fragments.resize(g_sprite_height);
		std::vector<int> blocksize;
		blocksize.resize(g_sprite_height);

		//printf("merge spanblocks...(key:%d)\n", g_allargs.keycol);

		// 4) merge single-span blocks with neighbour blocks
		//    most of the overhead is coming from these alone
		bool changed = true;
		int srep = 0;
		ubyte_t k = g_allargs.keycol;
//		ubyte_t cc = k | 0x80;
		ubyte_t cc = k;

		//	for (int p = 0; p < 3; p++)
		while (changed)
		{
			changed = false;

			// 4a) recolour/encode by spanblock fragmentation
			{
				cc = advance_block_encoding(cc, k);

				//ubyte_t k = g_allargs.keycol;
				//ubyte_t cc = (k + 1) & 255;
				/*cc = (cc + 1) & 255;
				if (cc == k)
					cc = (cc + 1) & 255;
				cc |= 0x80;
				*/

				if (g_allargs.verbose)
					printf("spanblock recolour/encode pass %d / cc = %02x:%02x...\n", srep++, (u32)k, (u32)cc);

				int lastleft = -1;
				int lastright = -1;
				int spancount = 1;
				int last_block = 0;

				for (int py = 0; py < g_sprite_height; py++)
				{
					// find first solid pixel
					int pxl = 0;
					for (; pxl < g_sprite_width; pxl++)
					{
						ubyte_t c = _psource8[((py + _y) * g_spritesource_w) + (pxl + _x)];

						if (c != g_allargs.keycol)
							break;
					}

					// find last solid pixel
					int pxr = g_sprite_width - 1;
					for (; pxr >= 0; pxr--)
					{
						ubyte_t c = _psource8[((py + _y) * g_spritesource_w) + (pxr + _x)];

						if (c != g_allargs.keycol)
							break;
					}

					int left = 0;
					int right = 0;

					if (pxl > pxr)
					{
						//printf("warning: degenerate/empty span!\n");
						pxr = pxl = 0;
					}
					else
					{
						// round towards left
						left = pxl & -16;

						// last pixel exclusive
						pxr++;
						// round towards right
						right = (pxr + 15) & -16;

						// clamp
						if (right > g_spritesource_w)
							right = g_spritesource_w;

						//printf("ok: good span - %d->%d\n", left, right);
					}

					// record fragment left/right coords
					fragments[py].first = left;
					fragments[py].second = right;

					/*				if (left == right)
									{
									printf("warning: degenerate word width!\n");
									//					exit(-1);
									}
									*/

					if ((left != lastleft) ||
						(right != lastright))
					{

						// change colour
						auto oldcc = cc;
						cc = advance_block_encoding(cc, k);

						if (g_allargs.verbose)
							printf("new spanblock %d->%d (y:%d->%d), re-encode %02x->%02x\n", 
								left, right, 
								last_block, py, 
								(u32)oldcc, (u32)cc
							);

						/*
						cc = (cc + 1) & 255;
						if (cc == k)
							cc = (cc + 1) & 255;
						cc |= 0x80;
						*/

						lastleft = left;
						lastright = right;

						for (int s = last_block; s < py; s++)
						{
							blocksize[s] = spancount;
							if (g_allargs.verbose)
								printf("spancount[%d]: %d\n", s, spancount);
						}

						spancount = 1;
						last_block = py;
					}
					else
					{
						// accumulate unchanging spans
						spancount++;
					}

					// replace colour for quantised region
					if (right > left)
					{
						if (g_allargs.verbose)
							printf("recolour x:%d y:%d ww:%d pw:%d cc:%02x\n", left, py, (right - left), (pxr - pxl), (u32)cc);
						int wpx = left;
						for (; wpx < right; wpx++)
						{
							// final colour doesn't matter, so long as its not keycol
							_psource8[((py + _y) * g_spritesource_w) + (wpx + _x)] = cc;
						}
					}
				}
			}

			//ubyte_t k = g_allargs.keycol;
			//ubyte_t cc = (k + 1) & 255;

			cc = advance_block_encoding(cc, k);

			for (int py = 0; py < g_sprite_height; py++)
			{


				//			continue;

				// 4b) merge spans with neighbours 

				int pym2 = xmax(py - 2, 0);
				int pym1 = xmax(py - 1, 0);
				int pyp1 = xmin(py + 1, g_sprite_height - 1);
				int pyp2 = xmin(py + 2, g_sprite_height - 1);

				if (
					(blocksize[py] > 0 &&
					 blocksize[py] < 3)
					/*
									(
									//difference with m1
									((pym1 == py) ||
									(fragments[pym1].first  != fragments[py].first) ||
									(fragments[pym1].second != fragments[py].second))
									&&
									//difference with p1
									((pyp1 == py) ||
									(fragments[pyp1].first  != fragments[py].first) ||
									(fragments[pyp1].second != fragments[py].second))
									)
									||
									(
									//difference with m2
									((pym2 == py) ||
									(fragments[pym2].first  != fragments[py].first) ||
									(fragments[pym2].second != fragments[py].second))
									&&
									//difference with p1
									((pyp1 == py) ||
									(fragments[pyp1].first  != fragments[py].first) ||
									(fragments[pyp1].second != fragments[py].second))
									)
									||
									(
									//difference with m1
									((pym1 == py) ||
									(fragments[pym1].first  != fragments[py].first) ||
									(fragments[pym1].second != fragments[py].second))
									&&
									//difference with p2
									((pyp2 == py) ||
									(fragments[pyp2].first  != fragments[py].first) ||
									(fragments[pyp2].second != fragments[py].second))
									)
									*/
									)
				{
					// this is a single-span or 2-span block

					// check possibility of merging with upper neighbour
					// pym1		--XXXXXXXXX- 
					// py		----XXXXXXX-
					// pyp1     ------------
					bool mu =
						(py != pym1) &&
						(fragments[py].first != fragments[py].second) &&
						(fragments[pym1].first != fragments[pym1].second) &&
						(fragments[py].first >= fragments[pym1].first) &&
						(fragments[py].second <= fragments[pym1].second);

					// check possibility of merging with lower neighbour
					// pym1		------------
					// py		----XXXXXXX-
					// pyp1     --XXXXXXXXX-
					bool ml =
						(py != pyp1) &&
						(fragments[py].first != fragments[py].second) &&
						(fragments[pyp1].first != fragments[pyp1].second) &&
						(fragments[py].first >= fragments[pyp1].first) &&
						(fragments[py].second <= fragments[pyp1].second);

					// find merge error with each neighbour
					int erru = mu ? ((fragments[py].first - fragments[pym1].first) + (fragments[pym1].second - fragments[py].second)) : 0x7fffffff;
					int errl = ml ? ((fragments[py].first - fragments[pyp1].first) + (fragments[pyp1].second - fragments[py].second)) : 0x7fffffff;

					// ignore pre-merged cases
					if (erru == 0) erru = 0x7fffffff;
					if (errl == 0) errl = 0x7fffffff;

					// find merge error with lower neighbour
					if (erru < errl)
					{
						// lower neighbour preferred
						if (erru < 0x7fffffff)
						{
							if (g_allargs.verbose)
								printf("merge scan up: (%d->%d)->(%d->%d)\n",
									fragments[py].first, fragments[py].second,
									fragments[pym1].first, fragments[pym1].second
								);

							ubyte_t col = _psource8[((py + _y) * g_spritesource_w) + (fragments[py].first + _x)];

							int px = fragments[pym1].first;
							for (; px < fragments[pym1].second; px++)
							{
								_psource8[((py + _y) * g_spritesource_w) + (px + _x)] = col;
								changed = true;
							}
							break;
						}
					}
					else
					{
						// upper neighbour preferred
						if (errl < 0x7fffffff)
						{
							if (g_allargs.verbose)
								printf("merge scan down: (%d->%d)->(%d->%d)\n",
									fragments[py].first, fragments[py].second,
									fragments[pyp1].first, fragments[pyp1].second
								);

							ubyte_t col = _psource8[((py + _y) * g_spritesource_w) + (fragments[py].first + _x)];

							int px = fragments[pyp1].first;
							for (; px < fragments[pyp1].second; px++)
							{
								_psource8[((py + _y) * g_spritesource_w) + (px + _x)] = col;
								changed = true;
							}
							break;
						}
					}
				}

			} // py loop
		}

		// now process the quantised frame normally, but from the proxy work area
		add_slab_frame(_psource8, nullptr, _x, _y);

		// release the work area
		delete[] workarea;

		// restore source stride/config
		g_spritesource_w = tmp_spritesource_w;

	} // !degenerate
}

bool emit_slab_sequence(bool _storepixels)
{
	if (1)
	{
		std::cout << "outputting..." << std::endl;

		int s = 0;
		for (slablist_t::iterator fit = g_slabframes.begin(); fit != g_slabframes.end(); fit++, s++)
		{
			slabframe_t & slabframe = *fit;
			slabframe_t * pslabcomponent = &slabframe;

			int c = 0;
			while (pslabcomponent)
			{
				std::cout << " slab[" << s << "," << c << "]" << std::endl;
/*				for (std::list<slabspan_t>::iterator sit = pslabcomponent->spans_.begin(); sit != pslabcomponent->spans_.end(); sit++)
				{
					std::cout << "  spanblock[" << sit->ys_ << "]: dy:" << sit->dy_ << " y:" << sit->y_ << " xo:" << sit->xo_ << " xs:" << sit->xs_ << std::endl;
				}
*/				pslabcomponent = pslabcomponent->nextcmp_;
				c++;
			}
		}
	}

	std::string outfile;
	if (g_allargs.outfile.length() > 0)
	{
		outfile = g_allargs.outfile;
	}
	else
	{
		outfile = g_allargs.sourcefile;
	}

	if (g_allargs.cutmode == Cutmode_SLABS)
		replace_extension(outfile, "sls");
	else
	if (g_allargs.cutmode == Cutmode_SLABRESTORE)
		replace_extension(outfile, "slr");

	FILE *hout = fopen(outfile.c_str(), "wb+");
	if (hout)
	{
		word_t wb;
		word_t wv;
		long_t lv;
		int tracked_filesize = 0;

		std::vector<int> slab_index;
		std::vector<int> slab_nextcmp_chainlinks;
		std::vector<int> slab_nextcmp_chain;

		// 4: size of file remaining (patched before closing)
		lv = 0;
		fwrite(&lv, 1, 4, hout);

		// 4: file CRC (generated & patched before closing)
		lv = 0;
		fwrite(&lv, 1, 4, hout); tracked_filesize += 4;

		// 2: version info
		wv = endianswap16(SLS_VERSION);
		fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

		// 2: number of slab frames in sequence
		wv = endianswap16((uword_t)g_slabframes.size());
		fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

		// 2: number of bitplanes in slab data
		wv = endianswap16(g_allargs.planes);
		fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

		// 2: flags
		wv = 0;
		wv = endianswap16((uword_t)wv);
		fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

		// 1: preshift range
		wb = 0;
		fwrite(&wb, 1, 1, hout); tracked_filesize += 1;
		// 1: preshift step
		wb = 0;
		fwrite(&wb, 1, 1, hout); tracked_filesize += 1;

		// 2: source width
		wv = endianswap16((uword_t)g_sprite_width);
		fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
		// 2: source height
		wv = endianswap16((uword_t)g_sprite_height);
		fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

		// create slab index (patched before closing)
		int slab_index_fpos = tracked_filesize;
		lv = 0xBBBBBBBB;
		for (slablist_t::iterator it = g_slabframes.begin(); it != g_slabframes.end(); it++)
		{
			fwrite(&lv, 1, sizeof(lv), hout); tracked_filesize += sizeof(lv);
		}

		// individual slabs follow
		int idx = 0;
		for (slablist_t::iterator frit = g_slabframes.begin(); frit != g_slabframes.end(); frit++, idx++)
		{
			slabframe_t &slabframe = *frit;

			std::cout << "slabframe: " << idx << std::endl;

			int cmpidx = 0;
			slabframe_t *pcomponent = &slabframe;
		
			// record file-offset to first (head) slab in slab index, for patching later
			slab_index.push_back(tracked_filesize);

			while (pcomponent)
			{
				std::cout << " component: " << cmpidx << std::endl;

				std::vector<int> spanblock_index;
				std::vector<int> spanblock_clipper;
				std::vector<int> clipspandata_ptrs;
				std::vector<int> spandata_ptrs;
				std::vector<int> spandata_index;

				// 2: slab frame pixel width
				std::cout << "  pw: " << pcomponent->w_ << std::endl;
				wv = endianswap16(pcomponent->w_);
				fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
				// 2: slab frame pixel height
				std::cout << "  ph: " << pcomponent->h_ << std::endl;
				wv = endianswap16(pcomponent->h_);
				fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

				// 2: slab frame xoff
				wv = endianswap16(pcomponent->xo_);
				std::cout << "  xo: " << pcomponent->xo_ << std::endl;
				fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
				// 2: slab frame yoff
				wv = endianswap16(pcomponent->yo_);
				std::cout << "  yo: " << pcomponent->yo_ << std::endl;
				fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

				// 2: slab spanblocks
				wv = endianswap16(s16(pcomponent->spans_.size()));
				std::cout << "  spanblocks: " << s16(pcomponent->spans_.size()) << std::endl;
				fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

				// 4: pointer to next component, if present

				if (1)
				{
					if (pcomponent->nextcmp_)
					{
						slab_nextcmp_chainlinks.push_back(tracked_filesize);
						std::cout << "  nextcmp: " << (cmpidx + 1) << std::endl;
					}
					else
						std::cout << "  nextcmp: " << 0 << std::endl;

					lv = 0;//endianswap32(nextcmp);
					fwrite(&lv, 1, 4, hout); tracked_filesize += 4;
				}

				//// pointer to spanblock index (patched after writing)
				//int spanblock_indexptr_fpos = tracked_filesize;
				//lv = 0xEEEEEEEE;
				//fwrite(&lv, 1, sizeof(lv), hout); tracked_filesize += sizeof(lv);

				int indextables_fpos = tracked_filesize;

				// create clipper index (patched after writing)
				int spanblock_clipper_fpos = tracked_filesize;
				wv = 0xAAAA;
				//			lv = 0xDDDDDDDD;
				std::cout << "  clipper height: " << pcomponent->h_ << std::endl;
				for (int i = 0; i < pcomponent->h_; i++)
				{
					// 4x 16bit fields per line of slab frame

					// offset to spanblock reference for this line
					fwrite(&wv, 1, sizeof(wv), hout); tracked_filesize += sizeof(wv);

					// remaining spanblocks for this top yc -1
					fwrite(&wv, 1, sizeof(wv), hout); tracked_filesize += sizeof(wv);

					// pointer to spandata for top yc
					clipspandata_ptrs.push_back(tracked_filesize);
					//				fwrite(&lv, 1, sizeof(lv), hout); tracked_filesize += sizeof(lv);
					fwrite(&wv, 1, sizeof(wv), hout); tracked_filesize += sizeof(wv);

					// remaining spanblocks for this bottom yc -1
					fwrite(&wv, 1, sizeof(wv), hout); tracked_filesize += sizeof(wv);
				}

				// create spanblock index (patched after writing)
				int spanblock_index_fpos = tracked_filesize;
				wv = 0xBBBB;
				for (int i = 0; i < pcomponent->spans_.size(); i++)
				{
					// one 16bit offset per line of frame
					fwrite(&wv, 1, sizeof(wv), hout); tracked_filesize += sizeof(wv);

					//	cy		sbi		sb
					//	0		A		A
					//	1		A		-
					//	2		A		-
					//	3		B		B
					//	4		B		-
					//	5		C		C
					//	6		D		D
					//	7		D		-
				}

				// write out spanblock records
				//			int mapped_line = 0;
				int last_spanblock = -1;
				int last_spanblock_y = 0;
				for (std::list<slabspan_t>::iterator spanit = pcomponent->spans_.begin(); spanit != pcomponent->spans_.end(); spanit++)
				{
					spanblock_index.push_back(tracked_filesize);

					// fill clipper for all lines up to start of new spanblock (if any)
					// note: take care over initial y skip, which would otherwise fill -1 for last spanblock
					int last_valid_spanblock = (last_spanblock >= 0) ? last_spanblock : 0;
					for (; last_spanblock_y < (spanit->y_ - spanit->dy_); last_spanblock_y++)
					{
						spanblock_clipper.push_back(last_valid_spanblock);
					}

					last_spanblock++;

					// 2: spanblock dy (skipped lines)
					wv = endianswap16(spanit->dy_);
					fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
					// 2: spanblock xo
					wv = endianswap16(spanit->xo_);
					fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
					// 2: spanblock xs (<<4 to preshift for blitter lookups)
					wv = endianswap16(u16(spanit->xs_) << 4);
					fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
					// 2: spanblock dxw
					wv = endianswap16(spanit->dxw_);
					fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
					// 2: spanblock y (for skipped then nonskipped lines of spanblock)
					wv = endianswap16(spanit->y_);
					fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
					// 2: spanblock nonskipped lines (ys-1)
					wv = endianswap16(spanit->ys_ - 1);
					fwrite(&wv, 1, 2, hout); tracked_filesize += 2;

					// 4: spandata ptr
					spandata_ptrs.push_back(tracked_filesize);
					lv = 0xAAAAAAAA;
					fwrite(&lv, 1, sizeof(lv), hout); tracked_filesize += sizeof(lv);
				}

				// now write pixel data separately, and contiguously
				for (std::list<slabspan_t>::iterator spanit = pcomponent->spans_.begin(); spanit != pcomponent->spans_.end(); spanit++)
				{
					spandata_index.push_back(tracked_filesize);

					if (_storepixels)
					{

						// output spanblock lines
						for (int l = 0; l < spanit->ys_; l++)
						{

							//if (0) // interleaved planewords (CPU)
							//{
							//	// output span plane words for a line
							//	for (int o = 0; o < spanit->dxw_; o++)
							//	{
							//		// write colour words for N planes
							//		for (int p = 0; p < g_allargs.planes; p++)
							//		{
							//			wv = endianswap16(spanit->pspan_[(((l*spanit->dxw_) + o) * 8) + p]);
							//			fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
							//		}
							//	}
							//}
							//else // plane-at-a-time scans (blitter)
							{
								// write colour words for N planes
								for (int p = 0; p < g_allargs.planes; p++)
								{
									// output span plane words for a line
									for (int o = 0; o < spanit->dxw_; o++)
									{
										wv = endianswap16(spanit->pspan_[(((l*spanit->dxw_) + o) * 8) + p]);
										fwrite(&wv, 1, 2, hout); tracked_filesize += 2;
									}
								}
							}
						}
					}
				}

				// fill clipper for all lines up to end of last spanblock
				// note: take care over initial y skip, which would otherwise fill -1 for last spanblock
				int last_valid_spanblock = (last_spanblock >= 0) ? last_spanblock : 0;
				for (; last_spanblock_y < pcomponent->h_; last_spanblock_y++)
				{
					spanblock_clipper.push_back(last_valid_spanblock);
				}

				//			assert(mapped_line == it->h_);

				/*			while (mapped_line < it->h_)
							{
							spanblock_index.push_back(-1);
							mapped_line++;
							}
							*/

				// save seek			
				//int tmp_pos = ftell(hout);

				std::vector<word_t> clipspandata_offsets;
				// patch spanblock record data pointers
				int yi = 0;
				for (std::vector<int>::iterator cspit = clipspandata_ptrs.begin(); cspit != clipspandata_ptrs.end(); cspit++, yi++)
				{
					int ptr_off = (*cspit);
					fseek(hout, ptr_off + 4, SEEK_SET);
					// find spanblock used by this y
					int pi = spanblock_clipper[yi];
					// antireloc spandata ptr

					wv = (word_t)(spandata_index[pi] - indextables_fpos);
					clipspandata_offsets.push_back(wv);

					//				printf("clipper[%d]:[%08x] %08x\n", yi, ptr_off, lv);
					wv = endianswap16(wv);
					fwrite(&wv, 1, sizeof(wv), hout);

					//				lv = spandata_index[pi] - indextables_fpos;
					////				printf("clipper[%d]:[%08x] %08x\n", yi, ptr_off, lv);
					//				lv = endianswap32(lv);
					//				fwrite(&lv, 1, sizeof(lv), hout);
				}

				// patch spanblock record data pointers
				int pi = 0;
				for (std::vector<int>::iterator srpit = spandata_ptrs.begin(); srpit != spandata_ptrs.end(); srpit++, pi++)
				{
					int ptr_off = (*srpit);
					fseek(hout, ptr_off + 4, SEEK_SET);
					// antireloc next spandata ptr
					lv = spandata_index[pi] - indextables_fpos;
					//				printf("record[%d]: %08x\n", pi, lv);
					lv = endianswap32(lv);
					fwrite(&lv, 1, sizeof(lv), hout);
				}

				// patch clipper index
				fseek(hout, spanblock_clipper_fpos + 4, SEEK_SET);
				int yc = 0;
				for (std::vector<int>::iterator cit = spanblock_clipper.begin(); cit != spanblock_clipper.end(); cit++, yc++)
				{
					// spanblock clipper addresses spanblocks, but based on scanline indices
					// the clipper array contains spanblock indices counting from 0 and we translate 
					// these into offset from the spanblock index so the drawing routine starts on 
					// the correct spanblock index entry for any initial yclip

					int clipfirst = (*cit);

					wv = (spanblock_index_fpos + (clipfirst * sizeof(word_t))) - indextables_fpos;
					wv = endianswap16(wv);
					fwrite(&wv, 1, sizeof(wv), hout);

					// remaining spanblocks for top yc, assuming this was the first
					int remaining_sb = spanblock_index.size() - clipfirst;

					wv = remaining_sb;
					wv = endianswap16(wv);
					fwrite(&wv, 1, sizeof(wv), hout);

					// skip offset, already patched
					fseek(hout, 2, SEEK_CUR);

					// sum spanblocks until (y2 >= yc)
					{
						// ybot recedes as yc advances
						int ybot = pcomponent->h_ - yc;

						int sbcount = 0;
						for (std::list<slabspan_t>::iterator blkit = pcomponent->spans_.begin(); blkit != pcomponent->spans_.end(); blkit++)
						{
							int spanblock_y2 = (blkit->y_ /*+ blkit->dy_*/ + blkit->ys_);
							if (spanblock_y2 >= ybot)
								break;
							sbcount++;
						}
/*
						printf("clipper[%d]: i=%d, rt=%d, o=%d, rb=%d\n", 
							yc, 
							clipfirst, 
							remaining_sb,
							(int)clipspandata_offsets[yc],
							sbcount
							);
*/
						// remaining spanblocks for bottom yc -1
						wv = sbcount;
						wv = endianswap16(wv);
						fwrite(&wv, 1, sizeof(wv), hout);
					}
				}

				// patch spanblock index
				fseek(hout, spanblock_index_fpos + 4, SEEK_SET);
				for (std::vector<int>::iterator spanit = spanblock_index.begin(); spanit != spanblock_index.end(); spanit++)
				{
					wv = (*spanit) - indextables_fpos;// spanblock_index_fpos;
					wv = endianswap16(wv);
					fwrite(&wv, 1, sizeof(wv), hout);
				}

				// restore seek
				fseek(hout, 0, SEEK_END);

				// next component within frame (if any)
				pcomponent = pcomponent->nextcmp_;
				if (pcomponent)
					slab_nextcmp_chain.push_back(tracked_filesize);
					
				cmpidx++;

			} // frame components

		} // slabframes

		// patch slab index
		// todo: must also patch any non-null nextcmp* offsets
		lv = 0;
		size_t index_table_fpos = slab_index_fpos;
		fseek(hout, index_table_fpos+4, SEEK_SET);
		{
			int idx = 0;
			std::cout << std::hex;
			for (std::vector<int>::iterator it = slab_index.begin(); it != slab_index.end(); it++, idx++)
			{
				lv = (*it) - slab_index_fpos;
				//std::cout << "patch slab index [" << idx << "] O=0x" << (size_t)lv << " @ 0x" << index_table_fpos << std::endl;
				lv = endianswap32(lv);
				fwrite(&lv, 1, sizeof(lv), hout);
				index_table_fpos += sizeof(lv);
			}
		}

		// patch slab nextcmp chain index
		if (1)
		{
			int idx = 0;
			std::cout << std::hex;
			std::vector<int>::iterator lit = slab_nextcmp_chainlinks.begin();
			std::vector<int>::iterator it = slab_nextcmp_chain.begin();
			for (; lit != slab_nextcmp_chainlinks.end(); lit++, it++, idx++)
			{
				size_t link_fpos = (*lit);
				fseek(hout, link_fpos+4, SEEK_SET);
				size_t cmp_fpos = (*it);
				lv = cmp_fpos - slab_index_fpos;
				//std::cout << "patch nextcmp link [" << idx << "] O=0x" << (size_t)lv << " @ 0x" << link_fpos << std::endl;
				lv = endianswap32(lv);
				fwrite(&lv, 1, sizeof(lv), hout);
			}
		}

		// patch filesize
		fseek(hout, 0, SEEK_SET);
		lv = endianswap32(tracked_filesize);
		fwrite(&lv, 1, 4, hout);

		// last, generate CRC
		update_file_crc(hout, /*crc_offset=*/4);
		
		fclose(hout);
	}

	std::cout << "emitted slab sequence [" << outfile.c_str() << "]" << std::endl;

	return true;
}

// --------------------------------------------------------------------
//	sheet mode
// --------------------------------------------------------------------

static float rgb_error(const RGB& a, const RGB& b)
{
	static const float rn = 1.0f / 255.0f;

	float ar = float(a.red);
	float ag = float(a.green);
	float ab = float(a.blue);

	float br = float(b.red);
	float bg = float(b.green);
	float bb = float(b.blue);

	float dr = (ar - br) * rn;
	float dg = (ag - bg) * rn;
	float db = (ab - bb) * rn;

	float err = (dr * dr) + (dg * dg) + (db * db);

	//printf("err: %.6f\n", err);
	return err;
}

static float rgb_error(const RGB& a, const iRGB& b)
{
	RGB bb;
	bb.red = (unsigned int)b.red;
	bb.green = (unsigned int)b.green;
	bb.blue = (unsigned int)b.blue;
	return rgb_error(a, bb);
}

bool do_sheet()
{
	ulong_t sourcew = 0;
	ulong_t sourceh = 0;
	ulong_t sourced = 0;
	RGB *m_psourcergb = 0;
	RGB *m_ppalettergb = 0;
	ubyte_t *m_psourcecol0 = 0;
	ubyte_t *m_psourcecol1 = 0;
	ubyte_t *m_poriginalsrccol = 0;
	ubyte_t *m_framemask = 0;
	RGB *pvisualrgb = 0;
	int visualw = 0, visualh = 0;

	if (g_allargs.cutmode == Cutmode_SPRITES)
		std::cout << "spritesheet cutting mode..." << std::endl;
	else
	if (g_allargs.cutmode == Cutmode_SLABS)
		std::cout << "slab-sheet cutting mode..." << std::endl;
	else
	if (g_allargs.cutmode == Cutmode_SLABRESTORE)
		std::cout << "slab-sheet region restore mode..." << std::endl;

	{
		if (image_load(g_allargs.sourcefile.c_str(), m_psourcergb, m_psourcecol0, m_ppalettergb, sourcew, sourceh, sourced))
		{
			std::cout << "read [" << g_allargs.sourcefile.c_str() << "] with colour depth " << sourced << " and dimensions " << sourcew << " x " << sourceh << std::endl;

			bool doremap = true;

			if (m_psourcecol0)
			{
				if (sourced > g_allargs.planes)
				{
					if (g_allargs.noremap)
					{
						std::cout << "warning: source image is indexed and > " << g_allargs.planes << " index bits - will use first 16 colours only (--no-remap)" << std::endl;
						doremap = false;
					}
					else
					{
						std::cout << "warning: source image is indexed and > " << g_allargs.planes << " index bits - will be remapped to external palette" << std::endl;
					}
				}
				else
				{
					if (g_allargs.forceremap)
					{
						std::cout << "warning: source image is indexed and <= " << g_allargs.planes << " index bits - but will remap anyway (--force-remap)" << std::endl;
					}
					else
					{
						std::cout << "warning: source image is indexed and <= " << g_allargs.planes << " index bits - will pass through without remapping" << std::endl;
						doremap = false;
					}
				}

				if (doremap)
				{
					m_poriginalsrccol = m_psourcecol0;
					m_psourcecol0 = 0;
				}
				else
				{
					// check that indexed colours are now in range for bitplanes specified
					int maxcolidx = 1 << g_allargs.planes;
					int highest = 0;
					for (size_t p = 0; p < sourcew*sourceh; p++)
					{
						if ((m_psourcecol0[p] >= maxcolidx) && (m_psourcecol0[p] != g_allargs.keycol))
						{
							highest = xmax(highest, m_psourcecol0[p]);
							m_psourcecol0[p] = g_allargs.keycol;
//							break;
						}
					}

					if (highest)
					{
						std::cout << "warning: --no-remap was specified, but source colour indices exceed target bitplane count!" << std::endl;
						std::cout << "highest usable index = " << maxcolidx - 1 << ", highest index encountered = " << highest << std::endl;
						std::cout << "all illegal colours have been converted to --key-colour..." << std::endl;
					}
				}

/*
				int maxcolidx = 1 << g_allargs.planes;

				// check that indexed colours are in range for bitplanes specified
				for (size_t p = 0; p < sourcew*sourceh; p++)
				{
					if (m_psourcecol[p] >= maxcolidx)
					{
						std::cout << "warning: source image is indexed, but values exceed target bitplane count: remapping..." << std::endl;
						// ignore colourmap
						m_poriginalsrccol = m_psourcecol;
						m_psourcecol = 0;
						break;
					}
				}
*/
			}

			if (m_psourcecol0)
			{
				if (g_allargs.keyrgb_specified || 
					g_allargs.alphargb_specified || 
					g_allargs.hitkeyrgb_specified || 
					g_allargs.autocutkeyrgb_specified )
				{
					std::cerr << "error: can't use -keyrgb,-alphargb,-hitkeyrgb,-autocutkeyrgb on non-remapped spritesheet images" << std::endl 
						<< "can use -key/-hitkey <col> instead of -keyrgb." << std::endl;
					std::cout << "press any key..." << std::endl << std::flush;
					getchar();	
					exit(1);
				}

				std::cout << "using palette and colourmap provided by source image..." << std::endl;
			}
			else
			{
				// special case: generate both colour fields for benefit of dual-field mask used with distributed dual-field
				bool force_two_colour_fields =
					!g_allargs.dualfield &&
					(
						g_allargs.distributed_dualfield_ &&
						(g_allargs.ccremap == CCRemap_BOTH)
					);

				std::cout << "reducing " << ((sourced > 0) ? sourced : 24) << "-bit image to external fixed palette..." << std::endl;
				m_psourcecol0 = new ubyte_t[sourcew * sourceh]; //(ubyte_t*)malloc(sourcew * sourceh);
				m_psourcecol1 = nullptr;
				if (g_allargs.dualfield || force_two_colour_fields)
					m_psourcecol1 = new ubyte_t[sourcew * sourceh]; //(ubyte_t*)malloc(sourcew * sourceh);

				//if (m_psourcecol1 == nullptr)
				//	std::cerr << "error error!" << std::endl;

				// reduce source image to indexed colour with fixed palette
				reduce_image24(m_psourcergb, m_poriginalsrccol, m_psourcecol0, m_psourcecol1, sourcew, sourceh, sourced, m_ppalettergb);

				if (force_two_colour_fields && !g_allargs.use_solutionpair_reduction_)
				{
					std::cout << "dropping 2nd colour field..." << std::endl;

					// apparently we were not supplied a .ccs for this single-field sprite so 2nd field can be dumped after all
					delete[] m_psourcecol1;
					m_psourcecol1 = nullptr;
				}
			}

			// now generate autocut guides from source image



			if (g_allargs.autocutkeyrgb_specified)
			{
				std::cout << "autocut..." << std::endl;

				std::list<std::pair<int, int>> hotspots;

				// autocut mode - guide script is internally generated from crosshairs and reserved colours

				byte_t *pguidesh = new byte_t[sourcew * sourceh];
				memset(pguidesh, 0, sizeof(byte_t) * sourcew * sourceh);
				byte_t *pguidesv = new byte_t[sourcew * sourceh];
				memset(pguidesv, 0, sizeof(byte_t) * sourcew * sourceh);

				float t = g_allargs.keytol;

				// vertical sweep for horizontal guides
				for (int y = 0; y < sourceh; y++)
				{
					for (int x = 0; x < sourcew-1; x++)
					{
						RGB *pcol0 = &m_psourcergb[(y * sourcew) + x];
						RGB *pcol1 = &m_psourcergb[(y * sourcew) + x + 1];

						if ((rgb_error(*pcol0, g_allargs.autocutkeyrgb) < t) && (rgb_error(*pcol0, *pcol1) < 1e-6))
						{
							// horizontal crosshair pixel
							//std::cout << "h @ " << x << "," << y << std::endl;

							// transfer crosshair to guide buffer
							for (int xx = x; xx >= 0; xx--)
							{
								RGB *pcol = &m_psourcergb[(y * sourcew) + xx];
								if (rgb_error(*pcol, g_allargs.keyrgb) < t)
									break;

								// mark guide
								pguidesh[(y * sourcew) + xx] = 1;
							}
							int xe = x + 1;
							for (; xe < sourcew; xe++)
							{
								RGB *pcol = &m_psourcergb[(y * sourcew) + xe];
								if (rgb_error(*pcol, g_allargs.keyrgb) < t)
									break;

								// mark guide
								pguidesh[(y * sourcew) + xe] = 1;
							}

							// continue scan beyond end of crosshair
							x = xe - 1;
						}
					}
				}

				// horizontal sweep for vertical guides
				for (int x = 0; x < sourcew; x++)
				{
					for (int y = 0; y < sourceh-1; y++)
					{
						RGB *pcol0 = &m_psourcergb[(y * sourcew) + x];
						RGB *pcol1 = &m_psourcergb[((y + 1) * sourcew) + x];

						if ((rgb_error(*pcol0, g_allargs.autocutkeyrgb) < t) && (rgb_error(*pcol0, *pcol1) < 1e-6))
						{
							// vertical crosshair pixel
							//std::cout << "v @ " << x << "," << y << std::endl;

							// transfer crosshair to guide buffer
							for (int yy = y; yy >= 0; yy--)
							{
								RGB *pcol = &m_psourcergb[(yy * sourcew) + x];
								if (rgb_error(*pcol, g_allargs.keyrgb) < t)
									break;

								// mark guide
								pguidesv[(yy * sourcew) + x] = 1;
							}
							int ye = y + 1;
							for (; ye < sourceh; ye++)
							{
								RGB *pcol = &m_psourcergb[(ye * sourcew) + x];
								if (rgb_error(*pcol, g_allargs.keyrgb) < t)
									break;

								// mark guide
								pguidesv[(ye * sourcew) + x] = 1;
							}

							// continue scan beyond end of crosshair
							y = ye - 1;
						}
					}
				}

				// find crosshair origins
				for (int y = 0; y < sourceh; y++)
				{
					for (int x = 0; x < sourcew; x++)
					{
						byte_t c = 
							pguidesh[(y*sourcew) + x] & 
							pguidesv[(y*sourcew) + x];

						if (c)
						{
							//std::cout << "hotspot @ " << x << "," << y << std::endl;
							hotspots.push_back(std::pair<int, int>(x, y));
						}
					}
				}
				delete[] pguidesv; pguidesv = 0;
				delete[] pguidesh; pguidesh = 0;

				std::vector<std::pair<int, int>> sorted_hotspots;

				// formally organize hotspots left-right, top-bottom
				while (!hotspots.empty())
				{
					// find lowest x,y
					// find bounding box for remaining entries
					// find nearest to 0,0
					int x1 = 0x7ffff;
					int y1 = 0x7ffff;
					int x2 = 0;
					int y2 = 0;

					for (auto &h : hotspots)
					{
						if (h.first < x1)
							x1 = h.first;
						if (h.first > x2)
							x2 = h.first;
						if (h.second < y1)
							y1 = h.second;
						if (h.second > y2)
							y2 = h.second;
					}

					auto mi = hotspots.begin();
					{
						//int mx = 0x7ffff;
						//int my = 0x7ffff;
						int d = 0x7fffffff;
						for (auto hi = hotspots.begin(); hi != hotspots.end(); hi++)
						{
//							if ((hi->first < mx) && (hi->second < my))
							int dx = (hi->first - x1);
							int dy = (hi->second - y1);
							int dd = ((dx * dx) + (dy * dy));
							if (dd < d)
							{
								mi = hi;
								d = dd;
							}
						}
					}

					auto last_hotspot = *mi;
					sorted_hotspots.push_back(last_hotspot);
					hotspots.erase(mi);

					bool ok = true;
					while (!hotspots.empty() && ok)
					{
						// find next x within y tolerance
						auto mi = hotspots.begin();
						{
							int mx = 0x7ffff;
							int my = 0x7ffff;
							for (auto hi = hotspots.begin(); hi != hotspots.end(); hi++)
							{
								if ((hi->first < mx) && 
									(hi->first > last_hotspot.first) && 
									(abs(hi->second - last_hotspot.second) < 32))
								{
									mi = hi;
									mx = hi->first;
									my = hi->second;
								}
							}

							if (mx == 0x7ffff)
								ok = false;
						}

						if (ok)
						{
							last_hotspot = *mi;
							sorted_hotspots.push_back(last_hotspot);
							hotspots.erase(mi);
						}
					}

				}

				for (auto & h : sorted_hotspots)
					std::cout << "hotspot @ " << h.first << "," << h.second << std::endl;

				// find physical bounds of each individual sprite from hotspots

				class rect
				{
				public:

					rect(int _x1, int _y1, int _x2, int _y2) 
						: x1(_x1), y1(_y1), x2(_x2), y2(_y2)
					{ 
					}

					int x1, y1;
					int x2, y2;
				};

				std::vector<rect> bounds;

				m_framemask = new ubyte_t[sourcew*sourceh];
				memset(m_framemask, 0, sourcew*sourceh);
				int index = 0;
				for (auto & h : sorted_hotspots)
				{
					index++;

					// flood fill outwards 
					static std::vector<std::pair<int, int>> stack;
					stack.clear();
					stack.push_back(h);

					int rx1 = 0x7fffff;
					int rx2 = 0;
					int ry1 = 0x7fffff;
					int ry2 = 0;

					while (!stack.empty())
					{
						auto p = stack.back();
						stack.pop_back();


						// don't include key colour and don't sample it
						RGB *pcol = &m_psourcergb[(p.second * sourcew) + p.first];
						if (rgb_error(*pcol, g_allargs.keyrgb) < t)
						{
							// don't sample again, don't include pixel
							m_framemask[p.first + (p.second * sourcew)] = 255;
							// don't follow key pixels
							continue;
						}
						if (rgb_error(*pcol, g_allargs.autocutkeyrgb) < t)
						{
							// don't sample again, don't include pixel - but do follow crosshair to contact sprite
							m_framemask[p.first + (p.second * sourcew)] = 255;
						}
						else
						{
							// don't sample again - mark active pixel with frame index, follow active pixels
							m_framemask[p.first + (p.second * sourcew)] = index;

							if (p.first < rx1)
								rx1 = p.first;
							if (p.first > rx2)
								rx2 = p.first;

							if (p.second < ry1)
								ry1 = p.second;
							if (p.second > ry2)
								ry2 = p.second;
						}

						if (p.first > 0)
							if (0 == m_framemask[p.first - 1 + ((p.second + 0) * sourcew)])
								stack.push_back(std::pair<int, int>(p.first - 1, p.second + 0));
						if (p.first < sourcew-1)
							if (0 == m_framemask[p.first + 1 + ((p.second + 0) * sourcew)])
								stack.push_back(std::pair<int, int>(p.first + 1, p.second + 0));

						if (p.second > 0)
							if (0 == m_framemask[p.first + 0 + ((p.second - 1) * sourcew)])
								stack.push_back(std::pair<int, int>(p.first + 0, p.second - 1));
						if (p.second < sourceh-1)
							if (0 == m_framemask[p.first + 0 + ((p.second + 1) * sourcew)])
								stack.push_back(std::pair<int, int>(p.first + 0, p.second + 1));
					}

					rx2++;
					ry2++;
					std::cout << "frame bound: " << rx1 << "," << ry1 << "..." << rx2 << "," << ry2 << std::endl;
					bounds.push_back(rect(rx1,ry1,rx2,ry2));

				}

				// find common bound for all sprites

				int cx1 = 0;
				int cy1 = 0;
				int cx2 = 0;
				int cy2 = 0;
				int f = 0;
				for (auto & h : sorted_hotspots)
				{
					auto & b = bounds[f++];

					int dx1 = b.x1 - h.first;
					if (dx1 < cx1)
						cx1 = dx1;

					int dy1 = b.y1 - h.second;
					if (dy1 < cy1)
						cy1 = dy1;

					int dx2 = b.x2 - h.first;
					if (dx2 > cx2)
						cx2 = dx2;

					int dy2 = b.y2 - h.second;
					if (dy2 > cy2)
						cy2 = dy2;
				}

				std::cout << "sprite bounds: " << cx1 << "," << cy1 << "..." << cx2 << "," << cy2 << std::endl;

				// generate cutting guides

				for (auto & h : sorted_hotspots)
				{
					int x1 = h.first + cx1;
					int y1 = h.second + cy1;
					g_allargs.guideeventseq.push_back
					(
						guideevent
						(
							/*cutcount=*/1, 
							/*cutflags=*/"", 
							x1, y1,
							cx2-cx1, cy2-cy1,
							0, 0
						)
					);
				}

			}

			// sprite cutting loop
			{
				// either cut a rectangular grid of frames based on ycount,xcount
				// or, if specified, stop cutting when a total count is reached instead
				int cuts;
				if (g_allargs.cut_count)
					cuts = g_allargs.cut_count;
				else
					cuts = g_allargs.cut_xcount * g_allargs.cut_ycount;

				// configure expected cut count for allocating visual feedback image
				int total_expected_cuts = cuts;
				// if using a spriteguide file, sum up frames from the cutting events
				if (!g_allargs.guideeventseq.empty())
				{
					total_expected_cuts = 0;
					for (std::list<guideevent>::iterator git = g_allargs.guideeventseq.begin();
						git != g_allargs.guideeventseq.end();
						git++)
					{
						total_expected_cuts += git->count;
					}
				}

				// allocate double space for distributed transparency support
				if (g_allargs.distributed_dualfield_)
					total_expected_cuts <<= 1;

				// configure default cut from command args

				int cutting_source_xs = g_allargs.cut_xsize;
				int cutting_source_ys = g_allargs.cut_ysize;

				int cutting_source_x = g_allargs.cut_xpos;
				int cutting_source_y = g_allargs.cut_ypos;

				int total_cuts = 0;

				do
				{
					// if cutting from a guide file, override command settings for cutting cursor
					// and repeat the cut while guide events remain unprocessed
					if (!g_allargs.guideeventseq.empty())
					{
						guideevent &ge = g_allargs.guideeventseq.front();

						cutting_source_x = ge.xp;
						cutting_source_y = ge.yp;
						g_allargs.cut_xpos = ge.xp;
						g_allargs.cut_ypos = ge.yp;

						cutting_source_xs = ge.xs;
						cutting_source_ys = ge.ys;
						g_allargs.cut_xsize = ge.xs;
						g_allargs.cut_ysize = ge.ys;

						g_allargs.cut_xinc = ge.xi;
						g_allargs.cut_yinc = ge.yi;

						// cutting N frames from static position
						if ((ge.xi == 0) && (ge.yi == 0))
						{
							g_allargs.cut_xcount = ge.count;
							g_allargs.cut_ycount = 1;
						}
						else
						// row cut
						if ((ge.xi != 0) && (ge.yi == 0))
						{
							g_allargs.cut_xcount = ge.count;
							g_allargs.cut_ycount = 1;
						}
						else
						// column cut
						if ((ge.xi == 0) && (ge.yi != 0))
						{
							g_allargs.cut_xcount = 1;
							g_allargs.cut_ycount = ge.count;
						}
						else
						// everything else is an error
						{
							std::cerr << "error: cutting sequences in spriteguide files must be row or column, not diagonal!" << std::endl;
							std::cout << "press any key..." << std::endl << std::flush;
							getchar();	
							exit(1);
						}
	
						// configure cut count based on new args
						cuts = g_allargs.cut_xcount * g_allargs.cut_ycount;

						// this event has been dealt with - remove it
						g_allargs.guideeventseq.pop_front();
					}

					// can only have one sprite (outer) frame size per file, so we initialise
					// the sequence on the first cut and ignore any subsequent size changes
					static bool bfirstcut = true;
					if (bfirstcut)
					{
						bfirstcut = false;

						std::cout << "sprite dimensions [" << cutting_source_xs << ", " << cutting_source_ys << "]" << std::endl;

						if (g_allargs.cutmode == Cutmode_SPRITES ||
							g_allargs.cutmode == Cutmode_EMSPR ||
							g_allargs.cutmode == Cutmode_EMXSPR ||
							g_allargs.cutmode == Cutmode_EMHSPR)
						{
							init_sprite_sequence(cutting_source_xs, cutting_source_ys, sourcew, sourceh);

							// opportunity to create visual feedback for cutting jobs
							// note: sprites only
							if (g_allargs.visual && !pvisualrgb)
							{
								visualw = cutting_source_xs;
								visualh = cutting_source_ys * total_expected_cuts;
								pvisualrgb = new RGB[visualw * visualh];
								memset(pvisualrgb, 0, visualw * visualh * sizeof(RGB));
							}
						}
						else
							if (g_allargs.cutmode == Cutmode_SLABS || g_allargs.cutmode == Cutmode_SLABRESTORE)
							{
								init_slab_sequence(cutting_source_xs, cutting_source_ys, sourcew, sourceh);
							}
					}


					// when 50% transparency is specified, record 2 sprite frames per source frame (one per mask field)
					//if (g_allargs.alphargb_specified)
					//	cuts <<= 1;

					for (int event_cuts = 0, xc = 0; event_cuts < cuts; event_cuts++, total_cuts++)
					{
						int cx = cutting_source_x;
						int cy = cutting_source_y;
						bool xflip = false;
						bool yflip = false;
						int hs = 0;
						int vs = 0;

						int real_cuts = total_cuts;
						//if (g_allargs.alphargb_specified)
						//	real_cuts >>= 1;

						if (g_allargs.verbose)
							std::cout << "real_cuts: " << real_cuts << std::endl;

						spritemap_t::iterator found = g_allargs.spritemappings.find(real_cuts);
						if (found != g_allargs.spritemappings.end())
						{
							// this frame is mapped from another
							int from = found->second.from;

							// locate cut record for 'from' frame
							const spritemapping_t &other = g_allargs.spritemappings[from];

							if (from != real_cuts)
							{
								// move cutting position to that of 'from' frame, if they are different frames
								cx = other.x;
								cy = other.y;
								std::cout << "from: " << cx << " " << cy << std::endl;
							}

							// check for X/Y flip-flags
							if (found->second.flags.find("x") != std::string::npos)
								xflip = true;

							if (found->second.flags.find("y") != std::string::npos)
								yflip = true;

							// ...and scroll adjust L/R/U/D
							if (found->second.flags.find("l") != std::string::npos)
								hs = -1;
							else
								if (found->second.flags.find("r") != std::string::npos)
									hs = 1;

							if (found->second.flags.find("u") != std::string::npos)
								vs = -1;
							else
								if (found->second.flags.find("d") != std::string::npos)
									vs = 1;
						}

						if (g_allargs.cutmode == Cutmode_SPRITES ||
							g_allargs.cutmode == Cutmode_EMSPR ||
							g_allargs.cutmode == Cutmode_EMXSPR ||
							g_allargs.cutmode == Cutmode_EMHSPR)
						{
							std::cout << "cutting sprite frame " << total_cuts << " from position [" << cutting_source_x << ", " << cutting_source_y << "]" << std::endl;

							add_sprite_frame(m_psourcecol0, m_psourcecol1, m_framemask, cx, cy, xflip, yflip, hs, vs, m_ppalettergb, pvisualrgb, sourcew, sourceh);
							if (g_allargs.distributed_dualfield_)
								add_sprite_frame(m_psourcecol0, m_psourcecol1, m_framemask, cx, cy, xflip, yflip, hs, vs, m_ppalettergb, pvisualrgb, sourcew, sourceh);
						}
						else
							if (g_allargs.cutmode == Cutmode_SLABS)
							{
								std::cout << "cutting slab frame " << total_cuts << " from position [" << cutting_source_x << ", " << cutting_source_y << "]" << std::endl;
								memset(g_colrbuffer8, g_allargs.keycol, g_sprite_bpwidth * g_sprite_height);
								add_slab_frame(m_psourcecol0, m_framemask, cx, cy);
							}
							else
								if (g_allargs.cutmode == Cutmode_SLABRESTORE)
								{
									std::cout << "cutting slabrestore frame " << total_cuts << " from position [" << cutting_source_x << ", " << cutting_source_y << "]" << std::endl;
									memset(g_colrbuffer8, g_allargs.keycol, g_sprite_bpwidth * g_sprite_height);
									add_slabrestore_frame(m_psourcecol0, m_framemask, cx, cy);
								}

						// store mappings for each frame, so future frames can refer to these
						// mappings in order to re-cut earlier frames with optional flipping
						g_allargs.spritemappings[real_cuts].x = cx;
						g_allargs.spritemappings[real_cuts].y = cy;

						// in dual-mask mode, advance only every 2nd cut
						//if (g_allargs.alphargb_specified && ((total_cuts & 1) == 0))
						//	continue;

						cutting_source_x += g_allargs.cut_xinc;
						xc++;

						if (xc >= g_allargs.cut_xcount)
						{
							// reset row when cut_xcount expires
							xc = 0;
							cutting_source_x = g_allargs.cut_xpos;
							cutting_source_y += g_allargs.cut_yinc;
						}

					}

					// continue while guide events remain (if there were any guide events)
				}
				while (!g_allargs.guideeventseq.empty());

				std::cout << "...done" << std::endl;
			}

			// create sprite datafile
			
			switch (g_allargs.cutmode)
			{
			//
			case Cutmode_EMXSPR:
				// code-generated preshifted blitter EM sprites
#if defined(USE_EMX2)
				generate_emx2_sequence();
#else
				generate_emx_sequence();
#endif
//#if defined(USE_EMS2)
//				generate_emh_sequence();
//#endif
				std::cout << "outputting data..." << std::endl;
				emit_sprite_sequence(!g_allargs.hidden);
				break;

			default:
			case Cutmode_EMSPR:
				std::cout << "outputting data..." << std::endl;
				emit_sprite_sequence(!g_allargs.hidden);
				break;

			case Cutmode_EMHSPR:
//#if defined(USE_EMS2)
				generate_emh_sequence();
//#endif
				std::cout << "outputting data..." << std::endl;
				emit_sprite_sequence(!g_allargs.hidden);
				break;

			case Cutmode_SPRITES:
				// non-code-generated sprite formats
				std::cout << "outputting data..." << std::endl;
				emit_sprite_sequence(!g_allargs.hidden);
				break;

			//
			case Cutmode_SLABS:
				std::cout << "outputting data..." << std::endl;
				emit_slab_sequence(true/*!g_allargs.hidden*/);
				break;
			case Cutmode_SLABRESTORE: // for now, we don't have a use case for drawing this except maybe to debug it
				std::cout << "outputting data..." << std::endl;
				emit_slab_sequence(false/*!g_allargs.hidden*/);
				break;
			}
			std::cout << "...done" << std::endl;
		}

		if (g_allargs.visual && pvisualrgb)
		{
			std::string reviewfile(g_allargs.outfile);
			remove_extension(reviewfile);
			reviewfile.append("_vis.tga");

			emit_tga(reviewfile.c_str(), pvisualrgb, visualw, visualh);
			std::cout << "emitted spritesheet visualisation [" << reviewfile.c_str() << "]" << std::endl;
			delete[] pvisualrgb; pvisualrgb = 0;
		}

	}

	return true;
}

// --------------------------------------------------------------------
//	main program entrypoint
// --------------------------------------------------------------------

int main(int argc, char* argv[])
{
	std::cout << "[A]tari [G]ame [T]ools: playfield/sprite/slab cutter " << g_version_string << " / dml" << std::endl;

	std::string argv_str(argv[0]);
	std::string agtcut_basepath = argv_str.substr(0, argv_str.find_last_of("\\/"));

	g_allargs.rmacpath = agtcut_basepath;

	GetArguments(argc, argv, g_allargs);

	// degas mode supports 8x8, 16x16, 32x32...
	// rb+ mode supports 16x16, 32x32...
	// direct mode supports 8x8, 16x16 only

	if (g_allargs.cutmode == Cutmode_TILES)
	{
		switch (g_allargs.outmode)
		{
		case Outmode_DEGAS:
			// degas mode supports all sizes
			break;
		case Outmode_RBPLUS:
			if (g_allargs.tilesize < 16)
			{
				std::cerr << "error: rbplus mode only supports tilesize 16x16 or larger" << std::endl;
				std::cout << "press any key..." << std::endl << std::flush;
				getchar();	
				exit(1);
			}
			break;
		case Outmode_DIRECT:
			if (g_allargs.tilesize > 16)
			{
				std::cerr << "error: direct mode only supports tilesizes 8x8 or 16x16." << std::endl;
				std::cout << "press any key..." << std::endl << std::flush;
				getchar();	
				exit(1);
			}
			break;
		}
	}

	if (g_allargs.cutmode == Cutmode_TILES)
	{
		if (g_allargs.stitchmaps || (g_allargs.explicit_sources.size() == 1))
		{
			// one or more (stitched) source maps, single output tilemap + tileset
			do_tilemap_single();
		}
		else
		{
			// multiple source maps, multiple output tilemaps, single shared tileset
			do_tilemap_multi();
		}
	}
	else
	if (g_allargs.cutmode == Cutmode_SPRITES ||
		g_allargs.cutmode == Cutmode_EMSPR ||
		g_allargs.cutmode == Cutmode_EMXSPR ||
		g_allargs.cutmode == Cutmode_EMHSPR ||
		g_allargs.cutmode == Cutmode_SLABS ||
		g_allargs.cutmode == Cutmode_SLABRESTORE)
	{
		do_sheet();
	}
	else
	{
		// error
		std::cerr << "error: unsupported cutmode!" << std::endl;
		std::cout << "press any key..." << std::endl << std::flush;
		getchar();	
		exit(1);
	}
//	xassert(false);
}

