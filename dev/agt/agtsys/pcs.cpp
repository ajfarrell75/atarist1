//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	PhotoChrome splash screen interface
//----------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------------
//	system headers

#include <mint/sysbind.h>

// ---------------------------------------------------------------------------------------------------------------------
//	Our AGT headers

#include "common_cpp.h"

// basic memory interface
#include "ealloc.h"
#include "compress.h"

// machine stuff
#include "system.h"

#include "pcs.h"

// ---------------------------------------------------------------------------------------------------------------------

extern "C" void STE_PCSV6_begin_();
extern "C" void STE_PCSV6_end_();
extern "C" int STE_PCSV6_Setup416x272(int num_fields, int num_lines, u16 **pimg_fieldseq, u16 **pbpal_fieldseq, u16 **pcpal_fieldseq, int scrolldelay);
extern "C" int STE_PCSV6_Show416x272();

// ---------------------------------------------------------------------------------------------------------------------

u16 *pcs416_image_fields[4];
u16 *pcs416_bpal_fields[4];
u16 *pcs416_cpal_fields[4];

// ---------------------------------------------------------------------------------------------------------------------

void STE_ShowAGI416(char *name, int scrolldelay)
{
	// pull in the asset and unpack it if necessary
	u8 * asset = load_asset
	(
		name,
		AssetFlags
		(
			afi_load_unwrapped |
			afi_asset_scratch 
			|
			(
				// .agi format a size prefix even if not wrapped
				afi_size_prefix |
				afi_size_exclusive
			)
		)
	);
	
	if (asset)
	{
		agiheader_t *pheader = (agiheader_t*)asset;

		if (pheader->code == 'p6t1')
		{
			s16 nlines = pheader->h;
			s16 palette_fields = pheader->pf;
			s16 bitmap_fields = pheader->bf;

			printf("loaded PCS: w=%d, h=%d, p-fields=%d, b-fields=%d\n", pheader->w, pheader->h, pheader->pf, pheader->bf);

			++pheader;
			u16 *imagedata = (u16*)pheader;
			--pheader;

			u16 *ptr = imagedata;

			s16 b = 0;
			for (; b < bitmap_fields; b++)
			{
				static const int bfield_words = (160+(224*nlines))/2;
				pcs416_image_fields[b] = ptr;	ptr += bfield_words;

				rotate_n2p4(pcs416_image_fields[b], bfield_words);
			}
			for (; b < 4; b++)
				pcs416_image_fields[b] = pcs416_image_fields[0];


			s16 p = 0;
			for (; p < palette_fields; p++)
			{
				pcs416_bpal_fields[p] = ptr;	ptr += ((14*3)*nlines);
				pcs416_cpal_fields[p] = ptr;	ptr += (10*(1+nlines));
			}

			// pass the information to the PCSv6 viewer
			STE_PCSV6_Setup416x272
			(
				/*fields=*/palette_fields, 
				/*lines=*/nlines, 
				pcs416_image_fields,  
				pcs416_bpal_fields, 
				pcs416_cpal_fields,
				scrolldelay
			);

			// invoke the viewer, which hogs the machine until SPACEBAR is hit
			STE_PCSV6_Show416x272();
		}

		// release asset space
		efree(asset);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
