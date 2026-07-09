
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <ctype.h>

//#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>		// for uintxx_t

#include <string>

#include "../common/common.h"
#include "../common/bendian.h"

typedef struct wrapheader_s
{
	// full header excluded from data_size

	uint32_t prefix;			// 'wrap'
	uint32_t wrapcode;			// 4-char code for wrapping packer type
	uint32_t data_size;			// size of compressed data (on disk size minus this header)
	uint32_t unpacked_size;		// size of unpacked data
	uint32_t pcrc;				// crc of packed data (check for device read errors)
	uint32_t ucrc;				// crc of unpacked data (check for packer/decompressor faults)

} wrapheader_t;

static uint32_t gen_crc_lite(uint16_t *_data, uint32_t _words)
{
	// perform hash
	uint32_t CRC = 0;

	do
	{
		uint16_t data16 = agt_ntohs(*_data++);		// BE data representation on target

		// xor
		CRC ^= (uint32_t)data16;

		// ror 7
		CRC = (CRC >> 7) | (CRC << ((sizeof(CRC)<<3)-7));

	} while ((--_words) != 0);

	return CRC;
}

static const uint32_t c_FNVOBase = 2166136261UL;

// generate 32bit CRC (FNV hash) based on 16 bit words
static uint32_t gen_crc(uint16_t *_data, uint32_t _words, uint32_t hash = c_FNVOBase)
{
	// perform hash
	// note: 32bit CRC position (and anything before it) will not be included
	uint32_t CRC = 0;
	{
		static const uint32_t c_FNVPrime = 16777619;

		do
		{
			uint16_t data16 = agt_ntohs(*_data++);		// BE data representation on target

			// xor
			hash ^= data16;

			// mul c
			hash *= c_FNVPrime;

			// ror 7
			hash = (hash >> 7) | (hash << ((sizeof(hash)<<3)-7));

		} while ((--_words) != 0);

		CRC = hash;
	}

	return CRC;
}

int main(int argc, char * argv[]) 
{
	struct stat filestatus;

	printf("PackWrap: compressed asset wrapper for AGT : dml/2022\n");

	if (argc < 5)
	{
		printf("usage: packwrap <origfile> <packedfile> <wrappedfile> <wrapcode>\n");  
		printf("wrapcodes:\n");
		printf(" wrap/none:   32bit CRC only\n");
		printf(" lz77:        LZ77\n");
		printf(" lzs1:        LZSA-1\n");
		printf(" lzs2/lzsa:   LZSA-2\n");
		printf(" pk2e/nrve:   NRV2e\n");
		printf(" arj4:        ARJ -m4\n");
		printf(" arj7:        ARJ -m7\n");		
		return EXIT_SUCCESS;
	}

	const char *name_orig = argv[1];
	const char *name_packed = argv[2];
	const char *name_wrapped = argv[3];
	const char *wrapping_code = argv[4];

	if ((0 == name_orig)||
		(0 == name_packed)||
	    (0 == name_wrapped)||
		(0 == wrapping_code)) 
	{
		printf("error: bad command inputs\n");  
		exit(EXIT_FAILURE);
	}

	if (strlen(wrapping_code) != 4)
	{
		printf("error: bad wrapping code, must be 4-char sequence\n");  
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
	uint32_t orig_filesize = filestatus.st_size;

	// perform quick CRC on the original data before packing (packer safety check)
	uint32_t orig_crc = 0;
	{
		FILE *origfile = fopen(name_orig, "rb");
		uint8_t *orig_buffer = new uint8_t[orig_filesize];
		fread(orig_buffer, 1, orig_filesize, origfile);
		fclose(origfile);

		orig_crc = gen_crc_lite((uint16_t*)orig_buffer, orig_filesize>>1);

		delete[] orig_buffer;
	}


	// size of packed file
	filestatus.st_size = 0;
	stat(name_packed, &filestatus);
	if (!filestatus.st_size)
	{
		printf("error: could not stat packed file: %s\n", name_packed);  
		exit(EXIT_FAILURE);
	}
	uint32_t in_filesize = filestatus.st_size;

	printf("original size: %d\n", orig_filesize);
	printf("packed size: %d\n", in_filesize);
	printf("wrapping code: %s\n", wrapping_code);

	// open files



	FILE *infile = fopen(name_packed, "rb");
	if (infile == NULL)
	{
		printf("error: failed to open packed file: %s\n", name_packed);  
		exit(EXIT_FAILURE);
	}

	uint8_t *buffer = new uint8_t[in_filesize + sizeof(wrapheader_t)];

	if (!buffer)
	{
		printf("error: failed to allocate memory\n");  
		exit(EXIT_FAILURE);
	}

	uint8_t *bufferpos = buffer;

	uint32_t wrapcode = 
		((uint32_t)wrapping_code[0] << 24) |
		((uint32_t)wrapping_code[1] << 16) |
		((uint32_t)wrapping_code[2] <<  8) |
		((uint32_t)wrapping_code[3] <<  0);

	wrapheader_t *header = (wrapheader_t*)bufferpos;

	// create wrapper with BE encoding for 68k
	header->prefix =		agt_htonl('wrap');
	header->wrapcode =		agt_htonl(wrapcode);
	header->data_size =		agt_htonl(in_filesize);
	header->unpacked_size =	agt_htonl(orig_filesize);

	bufferpos += sizeof(wrapheader_t);

	fread(bufferpos, 1, in_filesize, infile);
	fclose(infile);

	// calculate simple CRC
	uint32_t packed_crc = gen_crc((uint16_t*)bufferpos, in_filesize >> 1);

	header->pcrc = agt_htonl(packed_crc);
	header->ucrc = agt_htonl(orig_crc);

	FILE *outfile = fopen(name_wrapped, "wb");
	if (outfile == NULL)
	{
		printf("error: failed to write wrapped file: %s\n", name_wrapped);  
		exit(EXIT_FAILURE);
	}

	// write out wrapped file
	// [wrap][code][size_packed][size_unpacked][rawdata]
	fwrite(buffer, 1, (in_filesize + sizeof(wrapheader_t)), outfile);
	fclose(outfile);

	delete[] buffer;

	return EXIT_SUCCESS;
}
