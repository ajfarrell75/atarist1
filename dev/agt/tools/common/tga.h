//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2022
//======================================================================================================================
// TARGA .tga writing
//----------------------------------------------------------------------------------------------------------------------

#ifndef tools_tga_h_
#define tools_tga_h_

#if !defined(tools_common_h_)
#error "requires include: common/common.h"
#endif

//----------------------------------------------------------------------------------------------------------------------

#if !defined(_MSC_VER)
__attribute__((noinline))
#endif
static void emit_tga(const char* const _name, RGB *data, int _w, int _h)
{
	// emit trivial targa format, out of sheer lazyness
	FILE *fptr = fopen(_name, "wb");
	if (fptr)
	{
		putc(0,fptr);
		putc(0,fptr);
		putc(2,fptr);                         /* uncompressed RGB */
		putc(0,fptr); putc(0,fptr);
		putc(0,fptr); putc(0,fptr);
		putc(0,fptr);
		putc(0,fptr); putc(0,fptr);           /* X origin */
		putc(0,fptr); putc(0,fptr);           /* y origin */
		putc((_w & 0x00FF),fptr);
		putc((_w & 0xFF00) / 256,fptr);
		putc((_h & 0x00FF),fptr);
		putc((_h & 0xFF00) / 256,fptr);
		putc(24,fptr);                        /* 24 bit bitmap */
		putc(0,fptr);

		for (int y = _h-1; y >= 0; y--)
		{
			for (int x = 0; x < _w; x++)
			{
				const RGB &rgbpan = data[(y * _w) + x];

				putc(rgbpan.blue,		fptr);
				putc(rgbpan.green,		fptr);
				putc(rgbpan.red,		fptr);
			}
		}

		fclose(fptr);
	}
	else
	{
		std::cerr << "warning: could not create review TGA file: " << _name << std::endl;
	}
}

//----------------------------------------------------------------------------------------------------------------------
#endif // tools_tga_h_
//----------------------------------------------------------------------------------------------------------------------
