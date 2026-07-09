//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	SLAB graphics data / loader
//----------------------------------------------------------------------------------------------------------------------

#ifndef slabsheet_h_
#define slabsheet_h_

// ---------------------------------------------------------------------------------------------------------------------

#include "ealloc.h"
#include "compress.h"

// ---------------------------------------------------------------------------------------------------------------------

class slabsheet
{
public:

	// ---------------------------------------------------------------------------------------------------------------------
	// types

	static const int SLS_VERSION = 0x1202;

	// spanblock clipper header
	struct slc_header
	{
		s16 offset;					// poffset from indexbase to first spanblock record for this yc
		s16	remaining_t;			// remaining spanblocks to draw for this top yc
		s16 spdoff;					// offset from indexbase to spanblock pixel data
		s16	remaining_b;			// remaining spanblocks to draw for this bottom yc
	};

	// spanblock header
	struct ssb_header
	{
		s16 dy;						// skipped lines since last spanblock (for gaps)
		s16	xo;						// pixel xoffset from frame left edge
		s16	xs;						// pixel xsize of span
		s16 dxw;					// word xsize of span ((xs+15)&-16)
		s16 y;						// first y of spanblock
		s16 ys;						// ysize-1 of spanblock (0 for single span)
		s32 spandata;				// pointer to actual span pixels, left-justified & contiguous
	};

	// slab frame header (one per slab frame)
	struct slf_header
	{
		s16 w;						// frame width in pixels
		s16 h;						// frame height in pixels
		s16 xo;						// frame x offset (from source cutting position, if size was optimized)
		s16 yo;						// frame y offset (from source cutting position, if size was optimized)
		s16 spanblocks;				// number of spanblocks
		s32 nextcmp;				// pointer to next slab component (if more than one required to complete this frame)
		s8 indexbase[8];			// variable sized index & data follows 
									// ([h]eight * 8 bytes) clipping index to spanblock ssb_headers
									// followed by (spanblocks * 2 bytes) condensed index to unique spanblock ssb_headers
									// note: each index holds offsets from indexbase[]
	};

	// in-memory slabsheet header
	struct sls_header
	{
		// note: initial [filebytes] field is not part of in-memory header!
		u32 crc;					// file crc, minus this word and size prefix
		s16 version;				// file format version info
		s16 framecount;				// number of frames in spritesheet
		s16 planes;					// number of bitplanes in sprite
		u16 flags;					// misc. flags (mask format etc.)
		u8 preshift_range;			// preshift range = 2^preshift_range pixels (0..4 for 0->16 pixels)
		u8 preshift_step;			// preshift step = 2^preshift_step pixels (0..3 for 1->8 pixels)
		s16 srcwidth;				// source cutting width (before frame size optimization)
		s16 srcheight;				// source cutting height (before frame size optimization)
		slf_header *frameindex[1];	// sprite frame index follows (pointers to individual sprite frame headers)
	};

	// ---------------------------------------------------------------------------------------------------------------------
	// storage

	sls_header* data_;
	u8* alloc_;
	
	// ---------------------------------------------------------------------------------------------------------------------
	// internal stuff

private:

	void purge();

	// ---------------------------------------------------------------------------------------------------------------------
	// interface

public:

	slabsheet()
		: alloc_(nullptr),
		  data_(nullptr)
	{
	}

	~slabsheet();

	inline sls_header* get() const { return data_; }	
	inline s32 getwidth() const { return data_->srcwidth; }	
	inline s32 getheight() const { return data_->srcheight; }	

	bool load_sls(const char* const _filename);
};

// ---------------------------------------------------------------------------------------------------------------------
// slab pair structure allows detailed draw shape and simplified restore shape to be assigned as single asset

class sls_pair
{
public:

	slabsheet::sls_header *psls_draw_;
	slabsheet::sls_header *psls_restore_;

	sls_pair()
		: psls_draw_(0),
		  psls_restore_(0)
	{
	}

	void assign(slabsheet::sls_header *_draw, slabsheet::sls_header *_restore)
	{
		psls_draw_ = _draw;
		psls_restore_ = _restore;
	}
};

// ---------------------------------------------------------------------------------------------------------------------

class sps_pair
{
public:

	spritesheet::sps_header *psps_draw_;
	slabsheet::sls_header *psls_restore_;

	sps_pair()
		: psps_draw_(0),
		  psls_restore_(0)
	{
	}

	void assign(spritesheet::sps_header *_draw, slabsheet::sls_header *_restore)
	{
		psps_draw_ = _draw;
		psls_restore_ = _restore;
	}
};

// ---------------------------------------------------------------------------------------------------------------------

#endif // slabsheet_h_
