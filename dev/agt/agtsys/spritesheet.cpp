//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2022
//======================================================================================================================
//	spritesheet loader
//----------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------------
//	system headers

// ---------------------------------------------------------------------------------------------------------------------
//	Our AGT headers

#include "common_cpp.h"

#include "compress.h"

#include "spritesheet.h"

// ---------------------------------------------------------------------------------------------------------------------

void spritesheet::purge()
{
	efree(alloc_); alloc_ = nullptr;
	data_ = nullptr;
}

spritesheet::~spritesheet()
{
	dprintf("destroy spritesheet\n");
	purge();
}

// todo: read 'preshift' flags from file format instead of relying on user
bool spritesheet::load(const char* const _filename, bool _preshift)
{
	bool success = false;

	// dump existing asset, if any persists
	purge();

	// load-and-unpack asset file to memory buffer
	u32 info = 0;
	u8* passet = load_asset
	(
		_filename,
		AssetFlags(
			af_load_unwrapped |
			af_size_prefix | af_size_exclusive	// agtcut prefixes BE size word, non-inclusive
		),
		&info // returns total size of file, including prefix, status flags
	);

	if (passet)
	{
		// separate packed flag from size
		u32 size = info & 0x7fffffff;
		int packed_flag = info ^ size;

		// allocation includes size prefix, keep separate
		alloc_ = passet;

		// skip prefix
		data_ = (sps_header*)(passet+4);

		// run crc only if not wrapped
		if (!packed_flag)
		{
			// skip prefix & crc
			u32 crc_words = (size-8)>>1;
			u32 actual_crc = gen_crc((u16*)(passet+8), crc_words);
			if (data_->crc != actual_crc)
			{
				eprintf("error: sprite crc mismatch: 0x%08x!=0x%08x (%d)\n",
					actual_crc, data_->crc, crc_words
				);

				AGT_HALT();
			}
		}

		if (data_->version == SPS_VERSION)
		{
			pprintf("created spritesheet:\n\t%dx%d @ %d bpl, %d frames\n", 
				data_->srcwidth, 
				data_->srcheight, 
				data_->planes, 
				data_->framecount);

			s16 frames = data_->framecount;

			// relocate frame index for convenient access
			// caution: porting hazard, aliasing hazard. but hey its an ST!
			ptrsize_t addr_base = (ptrsize_t)&(data_->frameindex[0]);

			for (s16 frame = 0; frame < frames; frame++)
			{
				ptrsize_t addr_offset = (ptrsize_t)(data_->frameindex[frame]);
				data_->frameindex[frame] = (spf_header*)(addr_base + addr_offset);

				u16 storageflags = data_->flags & 7;

				// detect [EMSPR/EMXSPR] preshifted mask data or code
				if ((storageflags == 5) || (storageflags == 7))
				{
					// get preshift sub-index base address
					ptrsize_t subindex_base = (ptrsize_t)&(data_->frameindex[frame]->framedata[0]);

					// relocate preshifted colour subframes [EMSPR/EMXSPR]
					{
						ptrsize_t addr_base = subindex_base;
						ptrsize_t *psubindex = (ptrsize_t*)addr_base;
						s16 index = 0;
						for (s16 preshift = 0; preshift < 1; preshift++)
						{
							for (s16 field = 0; field < 2; field++)
							{
								ptrsize_t addr_offset = psubindex[index];
							//pprintf("note: colourplanes:%x!\n", addr_offset);
							if (addr_offset > 0)
									psubindex[index] = addr_base + addr_offset;
								subindex_base += 4;
								index++;
							}
						}
					}

					// relocate preshifted colouropt subframes [EMXSPR/CODEGEN ONLY]
					if (storageflags == 7)
					{
						ptrsize_t addr_base = subindex_base;
						ptrsize_t *psubindex = (ptrsize_t*)addr_base;
						s16 index = 0;
						for (s16 preshift = 0; preshift < 1; preshift++)
						{
							for (s16 field = 0; field < 2; field++)
							{
								ptrsize_t addr_offset = psubindex[index];
								if (addr_offset > 0)
									psubindex[index] = addr_base + addr_offset;
							subindex_base += 4;
								index++;
							}
						}
					}

					// relocate preshifted mask subframes [EMSPR/EMXSPR]
					{
						ptrsize_t addr_base = subindex_base;
						ptrsize_t *psubindex = (ptrsize_t*)addr_base;
						for (s16 preshift = 0; preshift < 16; preshift++)
						{
							ptrsize_t addr_offset = psubindex[preshift];
							//pprintf("note: psmask:%x!\n", addr_offset);

							if (addr_offset > 0)
							{
								ptrsize_t addr_abs = addr_base + addr_offset;
								psubindex[preshift] = addr_abs;
							}
							subindex_base += 4;
						}
					}

					// relocate preshifted maskopt subframes [EMXSPR/CODEGEN ONLY]
					if (storageflags == 7)
					{
						ptrsize_t addr_base = subindex_base;
						ptrsize_t *psubindex = (ptrsize_t*)addr_base;
						for (s16 preshift = 0; preshift < 16; preshift++)
						{
							ptrsize_t addr_offset = psubindex[preshift];
							ptrsize_t addr_abs = addr_base + addr_offset;
							psubindex[preshift] = addr_abs;
							subindex_base += 4;
						}
					}
				} // storageflags
			} // frames
			success = true;
		}
		else // SPS_VERSION
		{
			eprintf("error: bad spritesheet version (%04x!=%04x)\n", (u16)data_->version, (u16)SPS_VERSION);
			eprintf("re-generate assets with latest 'agtcut'\n");

			purge();
			
			AGT_HALT();
		}
	}
	else
	{
		eprintf("error: opening spritesheet: [%s]\n", _filename);
	}

	return success;
}	
