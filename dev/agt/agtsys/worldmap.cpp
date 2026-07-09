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

#include "system.h"

#include "compress.h"

#include "worldmap.h"

// ---------------------------------------------------------------------------------------------------------------------

worldmap::~worldmap()
{
	dprintf("destroy map\n");
	purge();
}

bool worldmap::create(s16 _w, s16 _h, int _tilestep, bool _overlay, int _fill_index)
{
	purge();

	bool success = false;

	header_.flags = _overlay ? 8 : 0;
	header_.tilestep = _tilestep;
	header_.width = _w;
	header_.height = _h;

	s32 mapspace = allocate(MapFormat_PLAYFIELD);

	if (mapspace)
	{
		pprintf("created map: %dx%d tiles\n", (int)header_.width, (int)header_.height);

		for (int i = 0; i < mapspace; i++)
			map_[i] = _fill_index;

		prepare_playfield(mapspace);

		success = true;
	}

	return success;
}

bool worldmap::load_ccm(const char* const _filename)
{
	purge();

	bool success = false;

	// load-and-unpack asset file to memory buffer
	u8* passet = load_asset
	(
		_filename,
		AssetFlags(af_none) // don't proceed to load the file if the header is unwrapped
	);

	s32 mapspace = 0;

	if (passet)
	{
		// loaded as wrapped asset - transfer data to map

		const ccm_header *psrcheader = (ccm_header*)passet;
		qmemcpy(&header_, psrcheader, sizeof(ccm_header));

		mapspace = allocate(MapFormat_PLAYFIELD);

		++psrcheader;

//		s32 *psrcmap = (s32*)(((u8*)passet) + sizeof(ccm_header));
		s32 *psrcmap = (s32*)psrcheader;

		qmemcpy(map_, psrcmap, mapspace * sizeof(s32));

		efree(passet);
	}
	else
	{
		// not wrapped - read directly, in steps

		FILE* h = fopen(_filename, "rb");
		if (h != 0)
		{
			// read the header
			fread(&header_, 1, sizeof(ccm_header), h);

			// older format was emitted little-endian, so we just detect it
			// and reformat if needed
			// todo: deprecate the old format, remove the swappage
			//bool le = (header_.width & 0xFFFF0000);
			//if (le)
			//{
			//	endianswap32((u32*)&header_.width, 1); 
			//	endianswap32((u32*)&header_.height, 1); 
			//}

			mapspace = allocate(MapFormat_PLAYFIELD);
									
			// read main part of the file
			fread(map_, 1, mapspace * sizeof(s32), h);
			// done
			fclose(h);

			// older format was emitted little-endian, so this is retained
			//if (le)
			//	endianswap32((u32*)map_, mapspace); 
		}
	}

	if (mapspace)
	{
		pprintf("created ccm map: %dx%d tiles\n", (int)header_.width, (int)header_.height);
		prepare_playfield(mapspace);
		success = true;
	}
	else
	{
		eprintf("error: opening ccm map: [%s]\n", _filename);
	}

	return success;
}

bool worldmap::load_csv(const char* const _filename, s16 _w, s16 _h, MapFormat _format)
{
	purge();

	bool success = false;

	//size_t rawsize = fsize((char*)_filename);
	//if (rawsize)
	{
		reg8(a4) = 1;

		FILE* fh = fopen(_filename, "rb");
		if (fh != 0)
		{
			size_t rawsize = 0;
			fseek(fh, 0, SEEK_END);
			rawsize = ftell(fh);
			fseek(fh, 0, SEEK_SET);

			header_.width = _w;
			header_.height = _h;

			s32 mapspace = allocate(_format);
	
			char *rawdata = nullptr;
			if (mapspace)
			{
				pprintf("created csv map: %dx%d tiles\n", (int)header_.width, (int)header_.height);

				rawdata = (char*)talloc(rawsize);
				fread(rawdata, 1, rawsize, fh);
			}

			// done
			fclose(fh);

			reg8(a4) = 0;

			if (mapspace)
			{
				int i, j;

				// alloc row scratch area
				char *scratch = (char*)talloc(32768);

				// parse in rows
				char **rows = NULL;
				csv_getcols(rawdata, "\n", &rows);

				s32 *pmap32 = map_;
				s16 *pmap16 = (s16*)map_;
				s8 *pmap8 = (s8*)map_;

				for (i = 0; rows[i]; i++) 
				{
					// parse in columns from each row
					char **columnX = (char**)scratch;//NULL;
					csv_getcols(rows[i], ",", &columnX);

					switch (_format)
					{
					default:
					case MapFormat_PLAYFIELD:
					case MapFormat_32:
						for (j = 0; columnX[j]; j++) *pmap32++ = atoi(columnX[j]);
						break;
					case MapFormat_16:
						for (j = 0; columnX[j]; j++) *pmap16++ = atoi(columnX[j]);
						break;
					case MapFormat_8:
						for (j = 0; columnX[j]; j++) *pmap8++ = atoi(columnX[j]);
						break;
					};
				}

				// free rows
				efree(rows);

				// free row scratch area
				efree(scratch);
			}

			efree(rawdata);

			if (mapspace)
			{
				if (_format == MapFormat_PLAYFIELD)
					prepare_playfield(mapspace);

				success = true;
			}
		}
		else
		{
			reg8(a4) = 0;
			eprintf("error: opening csv map: [%s] @ %dx%d tiles\n", _filename, _w, _h);
		}
	}
	//else
	//{
	//	eprintf("error: sizing csv map: [%s] @ %dx%d tiles\n", _filename, _w, _h);
	//}

	return success;
}	

s16 worldmap::row_datawords(MapFormat _format, s16 _rowsize)
{
	s16 datawords = _rowsize;

	switch (_format)
	{
	default:
	case MapFormat_PLAYFIELD:
	case MapFormat_32:
		break;
	case MapFormat_16:
		datawords = (datawords+1) >> 1;
		break;
	case MapFormat_8:
		datawords = (datawords+3) >> 2;
		break;
	};

	return datawords;
}

s32 worldmap::allocate(MapFormat _format)
{
	int tilestep = header_.tilestep;
	if (tilestep == 0)
		tilestep = 4;

	// allocate space (+overrun padding)
	s16 rowsize = row_datawords(_format, header_.width);

	s16 alloc_height = header_.height;
	padrows_pre_ = 0;

	// special case for playfields - overrun padding required before/after active window
	if (_format == MapFormat_PLAYFIELD)
	{
		// if vscroll specified...
		if (hvsf_ & 2)
		{
			// scroll safety margin (tilesize*2)
			padrows_pre_ += 2;
			// add guardband
			padrows_pre_ += (guardy_ + (1<<tilestep)-1) >> tilestep;
			// can't pad by more than the size of the map itself
			padrows_pre_ = xmin(padrows_pre_, header_.height);
		}
		else
		{
			// +1 rows enough for hscroll padding
			padrows_pre_ += 1;
		}
		//
		alloc_height += padrows_pre_;
		//
		// world map area
		//
		// viewport copy
		if (hvsf_ & 2)
		{
			switch (tilestep)
			{
			case 3:
				padrows_post_ += (272 + guardy_) >> 3;
				break;
			case 4:
				padrows_post_ += (272 + guardy_) >> 4;
				break;
			default:
				// error
				eprintf("error: unsupported tilesize\n");
				break;
			}
		}
		else
		{
			// +2 rows enough for hscroll padding
			padrows_post_ += 2;
		}

		// can't pad by more than the size of the map itself
		padrows_post_ = xmin(padrows_post_, header_.height);

		alloc_height += padrows_post_;
	}

	// alloc space for map, pre + post padding
	s32 allocspace = rowsize * alloc_height;
	mapalloc_ = new s32[allocspace];

	// map begins after pre padding
	map_ = mapalloc_ + (rowsize * padrows_pre_);

	// return just the loaded map size, without pre/post padding
	s32 mapspace = rowsize * header_.height;

	// failure to alloc should return error
	if (!mapalloc_)
		mapspace = 0;

	return mapspace;
}

void worldmap::prepare_playfield(s32 _mapspace)
{
	s16 rowsize = header_.width;

	int tilestep = header_.tilestep;
	if (tilestep == 0)
		tilestep = 4;

	int overlay = (header_.flags >> 3) & 1;

	int preshift = (tilestep + tilestep + 2) - 3 + overlay;

	// we now preshift for 16x16 or 8x8 tiles since other bits are unused (so far - can be masked later)
	for (s32 m = 0; m < _mapspace; m++)
	{
		map_[m] <<= preshift;
	}

	// fill rows before active map area, to catch map indexing overruns when scroll position is
	// near 0 or guardband is active
	{
		s16 rs = header_.height - padrows_pre_;
		s16 rowcount = padrows_pre_;
		qmemcpy(&mapalloc_[0], &map_[rowsize * rs], rowcount*rowsize*sizeof(s32));
	}

	// fill additional rows with copy of map data, to catch map indexing overruns when 
	// VScroll is enabled but map is smaller than virtual page size + vscroll margin
	{
		s16 rd = header_.height;
		s16 rowcount = padrows_post_;
		qmemcpy(&map_[rowsize * rd], &map_[0], rowcount*rowsize*sizeof(s32));
	}

	//qmemcpy(&map_[_mapspace], &map_[0], _mapspace*sizeof(s32));
}

// ---------------------------------------------------------------------------------------------------------------------
