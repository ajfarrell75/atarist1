
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <ctype.h>

//#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>		// for uintxx_t

#include "../common/common.h"
#include "../common/bendian.h"
#include "../common/p42n.h"

// for access to PCS header etc.
#include "../../3rdparty/binarylit.h"
#include "../../agtsys/pcs.h"


// ---------------------------------------------------------------------------------------------------------------------

uint16_t *pcs416_image_fields[4];
uint16_t *pcs416_bpal_fields[4];
uint16_t *pcs416_cpal_fields[4];

// ---------------------------------------------------------------------------------------------------------------------

void format_field_bitmap(uint16_t *&r_psrcpos, int nlines, uint16_t *pdst)
{
	static const int c_display_offsetwords = (c_display_offset / 2);
	static const int c_display_linewords = (c_display_linebytes / 2);

	for (int w = 0; w < c_display_offsetwords; ++w)
		*pdst++ = 0;

	for (int line = 0; line < nlines; ++line)
	{
		uint16_t *pline = pdst;
		pdst += c_display_linewords;
		for (int w = 0; w < (416/16); ++w)
		{
			*pline++ = *r_psrcpos++;
			*pline++ = *r_psrcpos++;
			*pline++ = *r_psrcpos++;
			*pline++ = *r_psrcpos++;
		}
	}
}

void format_field_palette(uint16_t *&r_psrcpos, int nlines, uint16_t *bch, uint16_t *cch)
{
	for (int c = 0; c < c_display_cch_linewords; ++c)
		*cch++ = 0;

	for (int line = 0; line < nlines; ++line)
	{
		// 3x blitter palettes per line (14 cols * 3)
		for (int p = 0; p < 3; ++p)
		{
			for (int w = 0; w < 14; ++w)
				*bch++ = *r_psrcpos++;

			// 14 active colours per palette
			r_psrcpos += 2; // +2=16
		}

		// 1x cpu palette per line (10 cols)

		// skip 6 cols
		r_psrcpos += (2 * 3);

		// copy remaining 10 
		for (int w = 0; w < 10; ++w)
			*cch++ = *r_psrcpos++;
	}
}

// reformat for display
void STE_PCSV6_FormatPCS416x272
(
	int nlines,
	int palette_fields,
	int bitmap_fields,
	uint16_t *imagedata,
	//
	uint16_t **image_fields,
	uint16_t **bpal_fields,
	uint16_t **cpal_fields
)
{
	uint16_t *r_psrcpos = imagedata;

	if ((bitmap_fields == 1) && (palette_fields > 1))
	{
		// shared-bitmap mode
		format_field_bitmap( r_psrcpos, nlines, image_fields[0]);
		format_field_palette(r_psrcpos, nlines, bpal_fields[0], cpal_fields[0]);
		format_field_palette(r_psrcpos, nlines, bpal_fields[1], cpal_fields[1]);
	}
	else
	{
		switch (bitmap_fields)
		{
		default:
		case 1:
			format_field_bitmap( r_psrcpos, nlines, image_fields[0]);
			format_field_palette(r_psrcpos, nlines, bpal_fields[0], cpal_fields[0]);
			break;
		case 2:
			format_field_bitmap( r_psrcpos, nlines, image_fields[1]);
			format_field_palette(r_psrcpos, nlines, bpal_fields[0], cpal_fields[0]);
			format_field_bitmap( r_psrcpos, nlines, image_fields[0]);
			format_field_palette(r_psrcpos, nlines, bpal_fields[1], cpal_fields[1]);
			break;
		case 3:
			format_field_bitmap( r_psrcpos, nlines, image_fields[2]);
			format_field_palette(r_psrcpos, nlines, bpal_fields[0], cpal_fields[0]);
			format_field_bitmap( r_psrcpos, nlines, image_fields[0]);
			format_field_palette(r_psrcpos, nlines, bpal_fields[1], cpal_fields[1]);
			format_field_bitmap( r_psrcpos, nlines, image_fields[1]);
			format_field_palette(r_psrcpos, nlines, bpal_fields[2], cpal_fields[2]);
			break;
		case 4:
			format_field_bitmap( r_psrcpos, nlines, image_fields[3]);
			format_field_palette(r_psrcpos, nlines, bpal_fields[0], cpal_fields[0]);
			format_field_bitmap( r_psrcpos, nlines, image_fields[0]);
			format_field_palette(r_psrcpos, nlines, bpal_fields[1], cpal_fields[1]);
			format_field_bitmap( r_psrcpos, nlines, image_fields[1]);
			format_field_palette(r_psrcpos, nlines, bpal_fields[2], cpal_fields[2]);
			format_field_bitmap( r_psrcpos, nlines, image_fields[2]);
			format_field_palette(r_psrcpos, nlines, bpal_fields[3], cpal_fields[3]);
			break;
		}
	}
}


size_t convert
(
	int nlines,
	int palette_fields,
	int bitmap_fields,
	uint16_t *indata,
	uint16_t *outdata
)
{
	size_t accumulated_words = 0;


	//size_t stored_bitmap_sz = nlines * (26 * 8); 
	//size_t stored_palette_sz = nlines * (2 * 16 * 4); 

	//
	{
		size_t display_bitmap_fieldsz = 
			(nlines * c_display_linebytes) +
			c_display_offset; 
		
		int b = 0;
		for (; b < bitmap_fields; b++)
		{
			//u32 base = (u32)ealloc(display_bitmap_fieldsz + 256);
			//u32 aligned_base = (base + 255) & ~255;
			//pcs416_image_fields[b] = (u16*)aligned_base;

			pcs416_image_fields[b] = (uint16_t*)outdata;
			outdata += display_bitmap_fieldsz >> 1;
			memset(pcs416_image_fields[b], 0, display_bitmap_fieldsz);

			accumulated_words += display_bitmap_fieldsz >> 1;
		}
		for (; b < 4; b++)
			pcs416_image_fields[b] = nullptr;
	}

	//

	{
		size_t display_bch_fieldsz =
			(nlines * c_display_bch_linewords * 2);

		size_t display_cch_fieldsz =
			((nlines + 1) * c_display_cch_linewords * 2);

		int p = 0;
		for (; p < palette_fields; p++)
		{
			pcs416_bpal_fields[p] = outdata; outdata += display_bch_fieldsz>>1; //(u16*)ealloc(display_bch_fieldsz);
			pcs416_cpal_fields[p] = outdata; outdata += display_cch_fieldsz>>1; //(u16*)ealloc(display_cch_fieldsz);
			memset(pcs416_bpal_fields[p], 0, display_bch_fieldsz);
			memset(pcs416_cpal_fields[p], 0, display_cch_fieldsz);

			accumulated_words += (display_bch_fieldsz + display_cch_fieldsz) >> 1;
		}
		for (; p < 4; p++)
		{
			pcs416_bpal_fields[p] = nullptr;
			pcs416_cpal_fields[p] = nullptr;
		}
	}

	// reformat fields for display
	STE_PCSV6_FormatPCS416x272
	(
		nlines,
		palette_fields,
		bitmap_fields,
		indata,
		&pcs416_image_fields[0],
		&pcs416_bpal_fields[0],
		&pcs416_cpal_fields[0]
	);

	return accumulated_words * 2;
}

int main(int argc, char * argv[]) 
{
	struct stat filestatus;

	printf("agtpcs: PCS image display formatter for AGT : dml/2022\n");

	if (argc < 3)
	{
		printf("usage: agtpcs <infile> <outfile>\n");  
		return EXIT_SUCCESS;
	}

	const char *name_orig = argv[1];
	const char *name_out = argv[2];

	if ((0 == name_orig)||
		(0 == name_out)) 
	{
		printf("error: bad command inputs\n");  
		exit(EXIT_FAILURE);
	}

	// size of original file
	filestatus.st_size = 0;
	stat(name_orig, &filestatus);
	if (!filestatus.st_size)
	{
		printf("error: could not stat original file: %s\n", name_orig);  
		exit(EXIT_FAILURE);
	}
	uint32_t in_filesize = filestatus.st_size;

	// open files

	FILE *infile = fopen(name_orig, "rb");
	if (infile == NULL)
	{
		printf("error: failed to open input file: %s\n", name_orig);  
		exit(EXIT_FAILURE);
	}

	uint8_t *indata = new uint8_t[in_filesize];

	if (!indata)
	{
		printf("error: failed to allocate memory\n");  
		exit(EXIT_FAILURE);
	}

	fread(indata, 1, in_filesize, infile);
	fclose(infile);


	// intepret header

	bool ok = true;

	const PCS_header * pheader = (PCS_header*)indata;
	const PCS_header & header = *pheader;

	int width = ntohs(header.PCS_width);

	if (width != 416)
	{
		printf("error: PCSv6 asset has unsupported pixel width [%d]!=[416]", width);
		ok = false;
	}

	int nlines = ntohs(header.PCS_height);
	if (nlines < 272)
	{
		printf("error: PCSv6 asset has to few lines [%d]<[272]", nlines);
		ok = false;
	}

	int format_type = (header.PCS_format_flags & mPCS_format_type) >> oPCS_format_type;
	if (format_type != 1)
	{
		printf("error: PCSv6 asset must be type #1 [%d]", format_type);
		ok = false;
	}

	if (!ok)
	{
		delete[] indata;
		exit(EXIT_FAILURE);
	}

	// intepret storage
	int palette_fields = 1;
	int bitmap_fields = 1;
	if (header.PCS_format_flags & 0x80)
	{
		// multiple fields (1-4) encoded as 2 bits (0-3)
		bitmap_fields = palette_fields =
			1 + ((header.PCS_format_flags & mPCS_format_fields) >> oPCS_format_fields);

		if (bitmap_fields == 1)
		{
			// special case, shared-bitmap mode (multiple palettes, 1 bitmap)
			palette_fields = 2;
		}
	}

	printf("loaded PCS: w=%d, h=%d, p-fields=%d, b-fields=%d\n",
		width, nlines, palette_fields, bitmap_fields
	);

	// access image
	++pheader;
	uint16_t *imagedata = (uint16_t*)pheader;
	

	// prepare output for writing

	uint8_t *outbuffer = new uint8_t[in_filesize * 2];
	uint8_t *outbufferpos = outbuffer;
	size_t out_filesize = 0;

	acsheader_t *outheader = (acsheader_t*)outbufferpos;

	// create wrapper with BE encoding for 68k
	outheader->code =		htonl('p6t1');			// pcsv6 type #1
	outheader->w =			htons(width);
	outheader->h =			htons(nlines);
	outheader->pf =			htons(palette_fields);
	outheader->bf =			htons(bitmap_fields);

	++outheader;
	outbufferpos = (uint8_t*)outheader;

	out_filesize += sizeof(acsheader_t);

	// convert data

	out_filesize += convert
	(
		nlines,
		palette_fields,
		bitmap_fields,
		imagedata,
		(uint16_t*)outbufferpos
	);

	// finalise size prefix
	outheader->size =		htonl(out_filesize-4);

	// output converted data

	FILE *outfile = fopen(name_out, "wb");
	if (outfile == NULL)
	{
		printf("error: failed to write wrapped file: %s\n", name_out);  
		exit(EXIT_FAILURE);
	}

	fwrite(outbuffer, 1, out_filesize, outfile);
	fclose(outfile);

	delete[] indata;
	delete[] outbuffer;

	printf("original size: %d\n", in_filesize);
	printf("final size: %d\n", out_filesize);

	return EXIT_SUCCESS;
}
