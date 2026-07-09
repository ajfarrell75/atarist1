//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	PhotoChrome splash screen interface
//----------------------------------------------------------------------------------------------------------------------

#ifndef pcs_h_
#define pcs_h_

//----------------------------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------*
// header is a bit nasty, being a bag of endian-sensitive words and bits

enum PCS_format_flags
{
	mPCS_format_multiframe = 		binary( 10000011 ),		// image has more than one display frame
		oPCS_format_multiframe = 	0,

	mPCS_format_extensions = 		binary( 01111100 ),		// mask for all extension flags (00000, 11111, 10000 codes are reserved ...for compatibility & extension)
		oPCS_format_extensions = 	2,
	mPCS_format_type =				binary( 00011100 ),		// 3-bit code indicates format type
		oPCS_format_type =			2,
	mPCS_format_fields = 			binary( 01100000 ),		// 2-bits encode unique field count
		oPCS_format_fields = 		5,
		
	mPCS_format_xor_palette = 		binary( 00000010 ),		// ((this_bit ^ multiframe_bit) != 0) = palette >=1 stored as deltas
		oPCS_format_xor_palette = 	1,
	mPCS_format_xor_bitmap = 		binary( 00000001 ), 	// ((this_bit ^ multiframe_bit) != 0) = bitmap >=1 stored as deltas
		oPCS_format_xor_bitmap = 	0,
};

enum PCS_display_flags
{
	mPCS_display_colourbits = 		binary( 11000000 ),		// 11 = 4bit(STE), 00 = 3bit(STF), 01,10 = reserved
		oPCS_display_colourbits = 	6,
	mPCS_display_freqcode = 		binary( 00000001 ),		// 1 = mode *requires* 50hz, otherwise *requires* 60hz
		oPCS_display_freqcode = 	0,
};

typedef struct PCS_header_s
{
	unsigned short PCS_width;		// warning - endian-sensitive on m68k<->x86!
	unsigned short PCS_height;		//

	// warning - GCC 4.6.3 on m68k/mint does not seem to allow structs of 1 byte... they get padded
	// so we express the bitfields contiguously. 

	// storage flags	
	unsigned char PCS_format_flags;

	// hardware flags
	unsigned char PCS_display_flags;
		
} __attribute__((packed)) PCS_header; 

//----------------------------------------------------------------------------------------------------------------------

typedef struct agiheader_s
{
	uint32_t size;
	uint32_t code;
	uint16_t w;
	uint16_t h;
    uint16_t flags;
	uint8_t pf;
	uint8_t bf;
} __attribute__((packed)) agiheader_t;

//----------------------------------------------------------------------------------------------------------------------

static const int c_display_linebytes = 224;
static const int c_display_offset = 160;
static const int c_display_bch_linewords = (14 * 3);
static const int c_display_cch_linewords = 10;

//----------------------------------------------------------------------------------------------------------------------
// display STE 416x??? PCS v6 display format

void STE_ShowAGI416(char *name, int scrolldelay);

// ---------------------------------------------------------------------------------------------------------------------

#endif // pcs_h_
