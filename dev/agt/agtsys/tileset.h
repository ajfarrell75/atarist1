//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	playfield tile library data / loader
//----------------------------------------------------------------------------------------------------------------------

#ifndef tileset_h_
#define tileset_h_

// ---------------------------------------------------------------------------------------------------------------------

#include "ealloc.h"
#include "compress.h"

// ---------------------------------------------------------------------------------------------------------------------

class tileset
{
public:

	// ---------------------------------------------------------------------------------------------------------------------
	// types

	struct cct_header
	{
		u32 crc;			// crc for whole file
		u16 palette[16];
		u8 flags;
		u8 bitplanes;
		u16 tilecount;
	};

	// ---------------------------------------------------------------------------------------------------------------------
	// storage

	u8* tiles_alloc_;
	u16* tiles_;
	s16 numtiles_;
	s16 tilesize_;
	s16 bitplanes_;
	u16 palette_[16];

	// ---------------------------------------------------------------------------------------------------------------------
	// internal stuff

private:

	void purge();

	// ---------------------------------------------------------------------------------------------------------------------
	// interface

public:

	tileset()
		: tiles_alloc_(nullptr),
		  tiles_(nullptr),
		  numtiles_(0),
		  bitplanes_(0),
		  tilesize_(0)
	{
	}

	~tileset();
	
	// access tiledata
	inline const u16* get() const { return tiles_; }	
	inline const u8* get8() const { return (u8*)tiles_; }	
	// access tiledata for single tile
	//inline const u16* get(s16 _t) const { return &tiles_[_t << (7-1)]; }	// 16x16 tiles
	//inline const u8* get8(s16 _t) const { return (u8*)(&tiles_[_t << (5-1)]); }	// 8x8 tiles
	
	// access individual palette entries
	inline u16 getcol(s16 _c) { return palette_[_c]; }
		
	s16 interpret_header(const cct_header& header);

	// load the tileset
	bool load_cct(const char* const _filename);
};

// ---------------------------------------------------------------------------------------------------------------------

#endif // tileset_h_
