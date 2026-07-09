//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	SPRITE graphics data / loader
//----------------------------------------------------------------------------------------------------------------------

#ifndef spritesheet_h_
#define spritesheet_h_

// ---------------------------------------------------------------------------------------------------------------------

#include "ealloc.h"
#include "compress.h"

// ---------------------------------------------------------------------------------------------------------------------

class spritesheet
{
public:

	// ---------------------------------------------------------------------------------------------------------------------
	// types

	static const int SPS_VERSION = 0x0207;

	// sprite frame header (one per sprite frame)
	struct spf_header
	{
		s16 w;						// frame width in pixels
		s16 h;						// frame height in pixels
		s16 xo;						// frame x offset (from source cutting position, if size was optimized)
		s16 yo;						// frame y offset (from source cutting position, if size was optimized)
		s16 omap;					// offset to optional occlusion maps
		s16 framedata[1];			// mask/colour data follows immediately when no preshifts present
									// OR...
									// pointers[#preshifts=1] to colourdata preshift frames
									// pointers[#preshifts=16] to maskdata preshift frames
	};


	// in-memory spritesheet header
	struct sps_header
	{
		// note: initial [filebytes] field is not part of in-memory header!
		u32 crc;					// file CRC, excluding size prefix & CRC field itself
		s16 version;				// file format version info
		s16 framecount;				// number of frames in spritesheet
		s16 planes;					// number of bitplanes in sprite
		u16 flags;					// misc. flags (mask format etc.)
		u8 preshift_range;			// preshift range = 2^preshift_range pixels (0..4 for 0->16 pixels)
		u8 preshift_step;			// preshift step = 2^preshift_step pixels (0..3 for 1->8 pixels)
		s16 srcwidth;				// source cutting width (before frame size optimization)
		s16 srcheight;				// source cutting height (before frame size optimization)
		spf_header *frameindex[1];	// sprite frame index follows (pointers to individual sprite frame headers)
	};

	// ---------------------------------------------------------------------------------------------------------------------
	// storage

	sps_header* data_;
	u8* alloc_;
	
	// ---------------------------------------------------------------------------------------------------------------------
	// internal stuff

private:

	void purge();

	// ---------------------------------------------------------------------------------------------------------------------
	// interface

public:

	spritesheet()
		: alloc_(nullptr),
		  data_(nullptr)
	{
	}

	~spritesheet();
	
	inline sps_header* get() const { return data_; }	
	inline s32 getwidth() const { return data_->srcwidth; }	
	inline s32 getheight() const { return data_->srcheight; }	
		
	// todo: read 'preshift' flags from file format instead of relying on user
	bool load(const char* const _filename, bool _preshift = true);
};

// ---------------------------------------------------------------------------------------------------------------------

#endif // spritesheet_h_
