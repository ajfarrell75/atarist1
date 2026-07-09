//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2022
//======================================================================================================================
//	planes-to-nibbles & c2p conversion
//----------------------------------------------------------------------------------------------------------------------

#ifndef tools_p42n_h_
#define tools_p42n_h_

#if !defined(tools_common_h_)
#error "requires include: common/common.h"
#endif

//----------------------------------------------------------------------------------------------------------------------

template<bool BE>
static void rotate_p42n(uint16_t *pdata)
{
	uint16_t p0 = pdata[0];
	uint16_t p1 = pdata[1];
	uint16_t p2 = pdata[2];
	uint16_t p3 = pdata[3];

	if (BE)
	{
		p0 = agt_ntohs(p0);
		p1 = agt_ntohs(p1);
		p2 = agt_ntohs(p2);
		p3 = agt_ntohs(p3);
	}

	uint8_t n = 0;
	uint8_t b = 0;
	uint16_t w = 0;

	for (int o = 0; o < 4; ++o)
	{
		for (int wi = 0; wi < 2; ++wi)
		{
			for (int bi = 0; bi < 2; ++bi)
			{
				n <<= 1; n |= ((p3 >> 15) & 1);
				n <<= 1; n |= ((p2 >> 15) & 1);
				n <<= 1; n |= ((p1 >> 15) & 1);
				n <<= 1; n |= ((p0 >> 15) & 1);

				b <<= 4;
				b |= n;

				p3 <<= 1;
				p2 <<= 1;
				p1 <<= 1;
				p0 <<= 1;
			}

			w <<= 8; w |= b;
		}
		if (BE)
        {
			w = agt_htons(w);
        }
		pdata[o] = w;
	}
}

static void rotate_p42n(uint8_t *pdata)
{
	uint8_t p0 = pdata[0];
	uint8_t p1 = pdata[1];
	uint8_t p2 = pdata[2];
	uint8_t p3 = pdata[3];

	uint8_t n = 0;
	uint8_t b = 0;
	uint8_t w = 0;

	for (int o = 0; o < 4; ++o)
	{
        for (int bi = 0; bi < 2; ++bi)
        {
            n <<= 1; n |= ((p3 >> 7) & 1);
            n <<= 1; n |= ((p2 >> 7) & 1);
            n <<= 1; n |= ((p1 >> 7) & 1);
            n <<= 1; n |= ((p0 >> 7) & 1);

            b <<= 4;
            b |= n;

            p3 <<= 1;
            p2 <<= 1;
            p1 <<= 1;
            p0 <<= 1;
        }

		pdata[o] = b;
	}
}

// --------------------------------------------------------------------
//	chunky (8bit) to N-planar conversion
// --------------------------------------------------------------------

static void c2pw
(
	ubyte_t* _csrc, uword_t* _pdst, 
	int _w, int _h,
	int _sstride, int _dstride,
	int _nplanes
)
{	
	// v line loop
	for (int yc = 0; yc < _h; ++yc)
	{
		ubyte_t* src = &_csrc[yc * _sstride];
		uword_t* dst = &_pdst[yc * _dstride];
		
		// h word loop
		for (int xc = (_w >> 4) - 1; xc >= 0; --xc)
		{
			// h pixel loop				
			for (int xbc = 16-1; xbc >= 0; --xbc)
			{
				ubyte_t val = *(src++);
					
				// plane loop
				uword_t* o = dst;
				for (int p = _nplanes - 1; p >= 0; --p)
				{
					*(o) = (*(o) << 1) | (val & 1); val >>= 1; o++;
				}
			}
				
			// advance per word
			dst += _nplanes;
		}
	}
}	

static void c2pb
(
	ubyte_t* _csrc, ubyte_t* _pdst, 
	int _w, int _h,
	int _sstride, int _dstride,
	int _nplanes
)
{	
	// v line loop
	for (int yc = 0; yc < _h; ++yc)
	{
		ubyte_t* src = &_csrc[yc * _sstride];
		ubyte_t* dst = &_pdst[yc * _dstride];
		
		// h word loop
		for (int xc = (_w >> 3) - 1; xc >= 0; --xc)
		{
			// h pixel loop				
			for (int xbc = 8-1; xbc >= 0; --xbc)
			{
				ubyte_t val = *(src++);
					
				// plane loop
				ubyte_t* o = dst;
				for (int p = _nplanes - 1; p >= 0; --p)
				{
					*(o) = (*(o) << 1) | (val & 1); val >>= 1; o++;
				}
			}
				
			// advance per word
			dst += _nplanes;
		}
	}
}	

//----------------------------------------------------------------------------------------------------------------------
#endif // tools_p42n_h_
//----------------------------------------------------------------------------------------------------------------------
