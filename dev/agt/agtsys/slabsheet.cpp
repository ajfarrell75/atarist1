//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2022
//======================================================================================================================
//	slabsheet loader
//----------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------------
//	system headers

// ---------------------------------------------------------------------------------------------------------------------
//	Our AGT headers

#include "common_cpp.h"

#include "compress.h"

#include "spritesheet.h"
#include "slabsheet.h"

// ---------------------------------------------------------------------------------------------------------------------

void slabsheet::purge()
{
	efree(alloc_); alloc_ = nullptr;
	data_ = nullptr;
}

slabsheet::~slabsheet()
{
	dprintf("destroy slabsheet\n");
	purge();
}

bool slabsheet::load_sls(const char* const _filename)
{
	bool success = false;

	purge();

	// load, or load-and-unpack asset file to memory buffer
	u32 info = 0;
	u8* passet = load_asset
	(
		_filename,
		AssetFlags(
			af_load_unwrapped |
			af_size_prefix | af_size_exclusive	// agtcut prefixes BE size word, non-inclusive
		),
		&info
	);

	if (passet)
	{
		// separate packed flag from size
		u32 size = info & 0x7fffffff;
		int packed_flag = info ^ size;

		// allocation includes size prefix, keep separate
		alloc_ = passet;
		data_ = (sls_header*)(passet+4);

		// run crc only if not wrapped
		if (!packed_flag)
		{
			// skip prefix & crc
			u32 crc_words = (size-8)>>1;
			u32 actual_crc = gen_crc((u16*)(passet+8), crc_words);
			if (data_->crc != actual_crc)
			{
				eprintf("error: slab crc mismatch: 0x%08x!=0x%08x (%d)\n",
					actual_crc, data_->crc, crc_words
				);
				AGT_HALT();
			}
		}

		if (data_->version == SLS_VERSION)
		{
			pprintf("created slabsheet:\n\t%dx%d @ %d bpl, %d frames\n", 
				data_->srcwidth, 
				data_->srcheight, 
				data_->planes, 
				data_->framecount);

			s16 frames = data_->framecount;

			// relocate frame index for convenient access
			// caution: porting hazard, aliasing hazard. but hey its an ST!
			ptrsize_t addr_base = (ptrsize_t)&(data_->frameindex[0]);

			for (s16 f = 0; f < frames; f++)
			{
				// relocate frame index
				ptrsize_t addr_offset = (ptrsize_t)(data_->frameindex[f]);
				slf_header *pbh = (slf_header*)(addr_base + addr_offset);
				data_->frameindex[f] = pbh;

				// may be more than one component within this frame
				do
				{
					ptrsize_t index_base = (ptrsize_t)&(pbh->indexbase[0]);

					// clipper and spanblock indices remain as offsets to save space
					// but pixeldata references are pointers so they need relocation

/*						// clipper table continues from indexbase
					slc_header *pclipper = (slc_header*)(&(pbh->indexbase[0]));

					for (s16 yc = 0; yc < pbh->h; yc++)
					{
						printf("clipper[%d]: o=%d rt=%d spd=%d rb=%d\n", 
							(int)yc, 
							(int)pclipper[yc].offset, 
							(int)pclipper[yc].remaining_t, 
							(int)pclipper[yc].spdoff,
							(int)pclipper[yc].remaining_b 
							);
					}
*/
					// spanblock index follows clipper table, which is (8 bytes * height) in size
					u16 *pspanblocks = (u16*)&(pbh->indexbase[sizeof(slc_header) * pbh->h]);

					for (s16 sb = 0; sb < pbh->spanblocks; sb++)
					{
						// note: pixeldata offsets are in terms of bytes, not words - meant for 68k lookups.
						ssb_header *pssb = (ssb_header*)(&pbh->indexbase[pspanblocks[sb]]);

						//printf("spanrecord[%d]: %08x\n", (int)sb, pssb->spandata);

						// relocate pixel data vs index base, so each spanblock record points at its own pixels
						// note: this is used mainly by the x-clipper to find the source offset in the spanblock
						pssb->spandata += index_base;		
					}

					slf_header *next_pbh = nullptr;
#if (1)
					// terminate, unless another component follows
					ptrsize_t nextcmp_addroffset = (ptrsize_t)pbh->nextcmp;

					if (nextcmp_addroffset)
					{
						//printf("next off: %x\n", nextcmp_addroffset);
						// relocate, if not null
						next_pbh = (slf_header*)(nextcmp_addroffset + addr_base);
						//printf("next pbh: %x\n", (ptrsize_t)next_pbh);
						pbh->nextcmp = (s32)next_pbh;
					}
#endif
					pbh = next_pbh;

				} while (pbh != 0);

				//while (1) { }

			}
			success = true;
		}
		else
		{
			eprintf("error: bad slabsheet version (%04x!=%04x)\n", (u16)data_->version, (u16)SLS_VERSION);
			eprintf("re-generate assets with latest 'agtcut'\n");

			purge();

			AGT_HALT();
		}
	}
	else
	{
		eprintf("error: opening slabsheet: [%s]\n", _filename);
	}

	return success;
}	
