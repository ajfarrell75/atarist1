//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	playfield world map data / loader
//----------------------------------------------------------------------------------------------------------------------

#ifndef worldmap_h_
#define worldmap_h_

#include "ealloc.h"
#include "../3rdparty/tinycsv.h"
#include "fileio.h"

// ---------------------------------------------------------------------------------------------------------------------

enum MapFormat
{
	MapFormat_PLAYFIELD,
	MapFormat_32,
	MapFormat_16,
	MapFormat_8
};

enum HScrollMode
{
	HScrollMode_NONE,
	HScrollMode_MARGIN,		// note: not yet supported
	HScrollMode_LOOPBACK,	// note: likely to be deprecated - scanwalk is cheaper
	HScrollMode_SCANWALK
};

enum VScrollMode
{
	VScrollMode_NONE,
	VScrollMode_MARGIN,		// note: not yet supported
	VScrollMode_LOOPBACK
};


class agt_tilepos
{
public:

	agt_tilepos(int _x, int _y)
		: x(s16(_x)), y(s16(_y))
	{
	}

	s16 x;
	s16 y;
};

class worldmap
{
public:

	// ---------------------------------------------------------------------------------------------------------------------
	// types

	struct ccm_header
	{
		s8 flags;
		s8 tilestep;
		s16 width;
		s16 _unused_;
		s16 height;
	};

	// ---------------------------------------------------------------------------------------------------------------------
	// storage

	ccm_header header_;

	s32* map_;
	s32* mapalloc_;
	s16 guardx_;
	s16 guardy_;
	s16 hvsf_;
	s16 padrows_pre_;
	s16 padrows_post_;

	// ---------------------------------------------------------------------------------------------------------------------
	// internal stuff

private:

	void purge()
	{
		delete[] mapalloc_; mapalloc_ = map_ = 0;
		header_.tilestep = 0;
		header_.flags = 0;
		header_.width = 0;
		header_.height = 0;
	}

	s16 row_datawords(MapFormat _format, s16 _rowsize);

	s32 allocate(MapFormat _format);

	// specific step to prepare map for playfield tile engine. not required for collision maps etc.
	void prepare_playfield(s32 _mapspace);

	// ---------------------------------------------------------------------------------------------------------------------
	// interface

public:

	worldmap
	(
		int _guardx = AGT_CONFIG_SYS_GUARDX, 
		int _guardy = AGT_CONFIG_SYS_GUARDY,
		HScrollMode _hsm = HScrollMode_SCANWALK, 
		VScrollMode _vsm = VScrollMode_LOOPBACK
	)
		: map_(0), 
		  mapalloc_(0),
		  //
		  guardx_(0), guardy_(0),
		  hvsf_(((_hsm != HScrollMode_NONE) ? 1 : 0) | ((_vsm != VScrollMode_NONE) ? 2 : 0))
	{
	}

	~worldmap();
	
	inline s32* get() const { return map_; }	
	inline s32* get32() const { return map_; }	
	inline s16* get16() const { return (s16*)map_; }	
	inline s8* get8() const { return (s8*)map_; }	
	inline s16 getwidth() const { return header_.width; }	
	inline s16 getheight() const { return header_.height; }	
	inline s16 gettilestep() const { return (s16)header_.tilestep; }	
			
	bool create(s16 _w, s16 _h, int _tilestep, bool _overlay, int _fill_index);

	bool load_ccm(const char* const _filename);

	bool load_csv(const char* const _filename, s16 _w, s16 _h, MapFormat _format);
};

// ---------------------------------------------------------------------------------------------------------------------

#endif // worldmap_h_
