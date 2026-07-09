//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	playfield tile library data / loader
//----------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------------
//	system headers

// ---------------------------------------------------------------------------------------------------------------------
//	Our AGT headers

#include "common_cpp.h"

#include "compress.h"

#include "tileset.h"

// ---------------------------------------------------------------------------------------------------------------------

tileset::~tileset()
{
	dprintf("destroy tileset\n");
	purge();
}

void tileset::purge()
{
	//delete[] tiles_alloc_; tiles_alloc_ = 0;
	efree(tiles_alloc_); tiles_alloc_ = nullptr;
	tiles_ = nullptr;
	numtiles_ = 0;
}

s16 tileset::interpret_header(const cct_header& header)
{
	// determine bitdepth
	bitplanes_ = header.bitplanes;
	if (bitplanes_ == 0)
		bitplanes_ = 4;

	// determine tilesize
	tilesize_ = 1 << (header.flags & 7);
	if (tilesize_ == 1)
		tilesize_ = 16;

	// palette stored with tiles for convenience and debugging
	const u16* cols = header.palette;
	for (s16 c = 0; c < 16; ++c)
		palette_[c] = cols[c];
	
	// calculate words per tile
	const s16 tilelinewords = (tilesize_ * bitplanes_) >> (3+1);
	s16 tilewords = tilelinewords * tilesize_;

	// tiles with mask consume 2x the space (which may include padding in 16x16 case)
	if (header.flags & (1<<3))
		tilewords <<= 1;
	
	// calculate words for complete tileset
	numtiles_ = header.tilecount;

	return tilewords;
}

bool tileset::load_cct(const char* const _filename)
{
	bool success = false;

	// dump existing asset, if any persists
	purge();

	// load-and-unpack asset file to memory buffer
	u32 info = 0;
	u8* passet = load_asset
	(
		_filename,
		(
			af_load_unwrapped	// cct has no size prefix
		),
		&info
	);

	if (passet)
	{
		// separate packed flag from size
		u32 size = info & 0x7fffffff;
		int packed_flag = info ^ size;

		// note: packed assets don't support FASTPAGE_TILES mode (<= 512 tiles) without further reallocation
		tiles_alloc_ = passet;
		tiles_ = (u16*)(passet + sizeof(cct_header));

		// interpret file header
		cct_header &header = *(cct_header*)tiles_alloc_;			

		if (!packed_flag)
		{
			u32 actual_crc = gen_crc((u16*)(passet+4), (size-4)>>1);
			if (header.crc != actual_crc)
			{
				printf("error: tileset crc mismatch: 0x%08x!=0x%08x\n",
					actual_crc, header.crc
				);

				AGT_HALT();
			}
		}

		s16 tilewords = interpret_header(header);

		// calculate words for complete tileset
		numtiles_ = header.tilecount;
		s32 tilesetwords = tilewords * numtiles_;

		// stored as nibbles? convert back to bitplanes
		if (header.flags & (1<<4))
		{
			pprintf("converting tileset from nibbles\n");

			if (tilesize_ == 8)
			{
				int tilescans = numtiles_ * 8;

				u8 *pdata = (u8*)tiles_;
				for (int s = 0; s < tilescans; s++)
				{
					// 4 bytes per tile scan
					rotate_n2p4(pdata);
					pdata += 4;
				}
			}
			else
			if (tilesize_ == 16)
			{
				int tilescans = numtiles_ * 16;

				u16 *pdata = (u16*)tiles_;
				for (int s = 0; s < tilescans; s++)
				{
					// 4 words per tile scan
					rotate_n2p4(pdata);
					pdata += 4;
				}
			}
		}

		pprintf("created tileset: %d tiles @ %dx%d in %dp\n", 
			(int)numtiles_, (int)tilesize_, (int)tilesize_, (int)header.bitplanes);
		success = true;
	}
	else
	{
		eprintf("error: loading tileset: [%s]\n", _filename);
	}

	return success;
}

