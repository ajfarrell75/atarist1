//======================================================================================================================
//	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
// 	compression interface
//----------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------------
//	system headers

// ---------------------------------------------------------------------------------------------------------------------
//	Our AGT headers

#include "common_cpp.h"

// basic memory interface
#include "ealloc.h"
#include "compress.h"

// ---------------------------------------------------------------------------------------------------------------------

class assetcache_L1
{
public:
	assetcache_L1()
	{
		s_assetcache_sentinel_begin_.pprev = nullptr;
		s_assetcache_sentinel_begin_.pnext = &s_assetcache_sentinel_end_;
		s_assetcache_sentinel_end_.pprev = &s_assetcache_sentinel_begin_;
		s_assetcache_sentinel_end_.pnext = nullptr;
	}

	static asset_cachelink s_assetcache_sentinel_begin_;
	static asset_cachelink s_assetcache_sentinel_end_;
};

class assetcache_L2
{
public:
	assetcache_L2()
	{
		s_assetcache_sentinel_begin_.pprev = nullptr;
		s_assetcache_sentinel_begin_.pnext = &s_assetcache_sentinel_end_;
		s_assetcache_sentinel_end_.pprev = &s_assetcache_sentinel_begin_;
		s_assetcache_sentinel_end_.pnext = nullptr;
	}

	static asset_cachelink s_assetcache_sentinel_begin_;
	static asset_cachelink s_assetcache_sentinel_end_;
};

// L1 cache: disk-to-ram (loading)
asset_cachelink assetcache_L1::s_assetcache_sentinel_begin_;
asset_cachelink assetcache_L1::s_assetcache_sentinel_end_;
assetcache_L1 s_assetcache_L1;

// L2 cache: ram-to-asset (decompression)
asset_cachelink assetcache_L2::s_assetcache_sentinel_begin_;
asset_cachelink assetcache_L2::s_assetcache_sentinel_end_;
assetcache_L2 s_assetcache_L2;

// ---------------------------------------------------------------------------------------------------------------------

u32 g_decompression_ticks = 0;

static void decompressor
(
	u32 _wrapcode, 
	u32 _packed_size, u8 * __restrict _packed, 
	u32 _unpacked_size, u8 * __restrict _unpacked
)
{
	u32 time = reg32(4BA);

	switch (_wrapcode)
	{
	case 'none':
	case 'wrap':
		// direct copy
		qmemcpy(_unpacked, _packed, _unpacked_size);
		break;
	case 'lz77':
		// unpack with lz77.s
#if defined(AGT_CONFIG_PACKER_ANY) || defined(AGT_CONFIG_PACKER_LZ77)
		DeLz77_68k(_packed, _unpacked);
#else
		wprintf("warning: define AGT_CONFIG_PACKER_LZ77 (or _ANY) in Makefile for lz77 mode");
#endif
		break;
	case 'zx0_':
		// unpack with unzx0.s
#if defined(AGT_CONFIG_PACKER_ANY) || defined(AGT_CONFIG_PACKER_ZX0)
		DeZx0_68k(_packed, _unpacked);
#else
		wprintf("warning: define AGT_CONFIG_PACKER_ZX0 (or _ANY) in Makefile for zx0 modes");
#endif
		break;
	case 'lzs1':
	case 'lzs2':
	case 'lzsa':
		// unpack with lzsa.s
#if defined(AGT_CONFIG_PACKER_ANY) || defined(AGT_CONFIG_PACKER_LZSA)
		DeLzsa_68k(_packed, _unpacked);
#else
		wprintf("warning: define AGT_CONFIG_PACKER_LZSA (or _ANY) in Makefile for lzsa/lzs1,lzs2 modes");
#endif
		break;
	case 'pk2e':
	case 'nrve':
		// unpack with pack2e.s
#if defined(AGT_CONFIG_PACKER_ANY) || defined(AGT_CONFIG_PACKER_PK2E)
		DePack2e_68k(_packed, _unpacked);
#else
		wprintf("warning: define AGT_CONFIG_PACKER_PK2E (or _ANY) in Makefile for pack2e/nrv2e mode");
#endif
		break;
	case 'arj4':
		// arjbeta -m4
#if defined(AGT_CONFIG_PACKER_ANY) || defined(AGT_CONFIG_PACKER_ARJ4)
		DeArj4_68k(_packed, _unpacked);
#else
		wprintf("warning: define AGT_CONFIG_PACKER_ARJ4 (or _ANY) in Makefile for arj-m4 mode");
#endif
		break;
	case 'arj7':
		// arjbeta -m7
#if defined(AGT_CONFIG_PACKER_ANY) || defined(AGT_CONFIG_PACKER_ARJ7)
		DeArj7_68k(_packed, _unpacked);
#else
		wprintf("warning: define AGT_CONFIG_PACKER_ARJ7 (or _ANY) in Makefile for arj-m7 mode");
#endif
		break;
	default:
		eprintf("error: unknown packing code!");
		break;
	}

	g_decompression_ticks += reg32(4BA) - time;
}

// ---------------------------------------------------------------------------------------------------------------------

#if 0
bool place_asset
(
	const char *const __restrict _name, 
	AssetFlags _flags, 
	u8 * __restrict _place, 
	u32 * __restrict _psize, 
	u32 _smax
)
{
	reg8(a4) = 1;

	FILE *fh = fopen(_name, "rb");
	if (fh == 0)
	{
		reg8(a4) = 0;

		eprintf("error: failed to place asset: %s\n", _name);
	}
	else
	{
		wrapheader_t header;
		fread(&header, 1, sizeof(header), fh);
		
		if (header.prefix == 'wrap')
		{
			// wrapped asset
			u32 packed_size = header.data_size;
			u32 unpacked_size = header.unpacked_size;

			if (_smax && (unpacked_size > _smax))
			{
				fclose(fh);
				reg8(a4) = 0;
				return false;
			}

			// can always use scratch for temporary packed data, if available
			u8 *temp_ptr = (u8*)talloc(packed_size + 2);

			u8 *zpad = &temp_ptr[packed_size];	// +2 safety padding in case of unpacker bugs!
			*zpad++ = 0;
			*zpad++ = 0;

			fread(temp_ptr, 1, packed_size, fh);
			fclose(fh);

			reg8(a4) = 0;

#if !defined(AGT_CONFIG_NO_WRAPCRC)
			u32 packed_crc = gen_crc((u16*)temp_ptr, packed_size>>1);

			if (header.pcrc != packed_crc)
			{
				eprintf("error: crc mismatch on file read!");
				efree(temp_ptr);
				if (_psize)
					_psize[0] = 0x80000000; // indicate file was packed
				return false;
			}
#endif

			decompressor(header.wrapcode, packed_size, temp_ptr, unpacked_size, _place);

#if !defined(AGT_CONFIG_NO_UNWRAPCRC)
			u32 unpacked_crc = gen_crc_lite((u16*)_place, unpacked_size>>1);
			if (header.ucrc != unpacked_crc)
			{
				eprintf("error: crc mismatch on unpacked data!");
				efree(temp_ptr);
				if (_psize)
					_psize[0] = 0x80000000; // indicate file was packed
				return nullptr;
			}
#endif
			efree(temp_ptr);

			if (_psize)
			{
				if (unpacked_size)
					unpacked_size |= 0x80000000;	// indicate this was packed
				_psize[0] = unpacked_size;
			}

			return true;
		}
		else
		{
			u32 total = 0;

			// unwrapped asset
			if (_flags & af_size_prefix)
			{
				// raw with size prefix - fetch & correct if necessary
				u32 remaining = header.prefix;
				if (_flags & af_size_lendian)
					endianswap32(&remaining, 1);

				if (_flags & af_size_exclusive)
					remaining += 4;

				total = remaining;

				if (_smax && (total > _smax))
				{
					fclose(fh);
					reg8(a4) = 0;
					return false;
				}

				u8 *writepos = _place;

				// transfer pre-read portion to dest, including prefix
				u8 *readpos = (u8*)(&header);
				qmemcpy(writepos, readpos, sizeof(header));
				writepos += sizeof(header);
				remaining -= sizeof(header);
				

				// read remainder in-place
				fread(writepos, 1, remaining, fh);
				fclose(fh);

				reg8(a4) = 0;

				if (_psize)
					_psize[0] = total;

				return true;
			}
			else
			{
				// if raw asset has no size prefix we need to find the size directly
				fseek(fh, 0, SEEK_END);
				total = ftell(fh);
				fseek(fh, 0, SEEK_SET);

				if (total)
				{
					// placement would overrun
					if (_smax && (total > _smax))
					{
						fclose(fh);
						reg8(a4) = 0;
						return false;
					}

					// read file from beginning
					fread(_place, 1, total, fh);
				}
	
				fclose(fh);
		
				reg8(a4) = 0;

				if (_psize)
					_psize[0] = total;

				return true;
			}
		}

/*
		// read filesize
		u32 datasize = 0;
		fread(&datasize, 1, 4, fh);
		endianswap32(&datasize, 1);

		u8 *temp_ptr = (u8*)ealloc(datasize);
		fread(temp_ptr, 1, datasize, fh);
		fclose(fh);
l
		reg8(a4) = 0;

		// check for packed indicator
		if (*((u32*)temp_ptr) == 'pk2e')
		{
			// skip 'pk2e' ident
			u8* data_ptr = temp_ptr; data_ptr += 4;

			// fetch size of unpacked data
			datasize = *((u32*)data_ptr); data_ptr += 4;
			endianswap32(&datasize, 1);

			// unpack
			qmemclr(_place, datasize);
			DePack2e_68k(data_ptr, _place);

			// release file buffer
			efree(temp_ptr);

			return true;
		}
		else
		{
			eprintf("error: p2e incorrect header: %s\n", _name);
		}
*/
	}

	return false;
}
#endif // 0


u8 * load_asset
(
	const char *const __restrict _name, 
	AssetFlags _flags, 
	u32 * __restrict _psize, 
	u32 _smax
)
{
	reg8(a4) = 1;

	FILE *fh = fopen(_name, "rb");
	if (fh == 0)
	{
		reg8(a4) = 0;

		eprintf("error: failed to open asset: %s\n", _name);

		return nullptr;
	}
	else
	{
		wrapheader_t header;
		fread(&header, 1, sizeof(header), fh);
		
		if (header.prefix == 'wrap')
		{
			// wrapped asset
			u32 packed_size = header.data_size;
			u32 unpacked_size = header.unpacked_size;

			dprintf("loading wrapped: [%s]:%d\n", _name, packed_size);

			if (_smax && (unpacked_size > _smax))
			{
				eprintf("error: asset too large: [%s]:%d\n", _name, unpacked_size);

				fclose(fh);
				reg8(a4) = 0;
				return nullptr;
			}

			u8 *temp_ptr = (u8*)talloc(packed_size + 2);

			u8 *zpad = &temp_ptr[packed_size];	// +2 safety padding in case of unpacker bugs!
			*zpad++ = 0;
			*zpad++ = 0;

			fread(temp_ptr, 1, packed_size, fh);
			fclose(fh);

			reg8(a4) = 0;

#if !defined(AGT_CONFIG_NO_WRAPCRC)
			u32 packed_crc = gen_crc((u16*)temp_ptr, packed_size>>1);

			if (header.pcrc != packed_crc)
			{
				eprintf("error: crc mismatch on file read!");
				efree(temp_ptr);
				if (_psize)
					_psize[0] = 0x80000000; // indicate file was packed
				return nullptr;
			}
#endif
			u8 *unpacked = nullptr;
			if (_flags & af_asset_scratch)
				unpacked = (u8*)talloc(unpacked_size);
			else
				unpacked = (u8*)ealloc(unpacked_size);

			if (unpacked)
			{
				decompressor(header.wrapcode, packed_size, temp_ptr, unpacked_size, unpacked);

#if !defined(AGT_CONFIG_NO_UNWRAPCRC)
				u32 unpacked_crc = gen_crc_lite((u16*)unpacked, unpacked_size>>1);
				if (header.ucrc != unpacked_crc)
				{
					eprintf("error: crc mismatch on unpacked data!");
					efree(unpacked);
					efree(temp_ptr);
					if (_psize)
						_psize[0] = 0x80000000; // indicate file was packed

					AGT_HALT();

					return nullptr;
				}
#endif
			}

			efree(temp_ptr);

			if (_psize)
			{
				if (unpacked_size)
					unpacked_size |= 0x80000000;	// indicate this was packed
				_psize[0] = unpacked_size;
			}
			
			return unpacked;
		}
		else
		{
			u32 total = 0;

			if (_flags & af_load_unwrapped)
			{
				// unwrapped asset
				if (_flags & af_size_prefix)
				{
					// raw with size prefix - fetch & correct if necessary
					total = header.prefix;
					if (_flags & af_size_lendian)
						endianswap32(&total, 1);

					if (_flags & af_size_exclusive)
						total += 4;
						
					dprintf("loading prefixed: [%s]:%d\n", _name, total);

					u32 remaining = total;

					if (_smax && (total > _smax))
					{
						eprintf("error: asset too large: [%s]:%d\n", _name, total);

						fclose(fh);
						reg8(a4) = 0;
						return nullptr;
					}

					u8 *unpacked = nullptr;
					if (_flags & af_asset_scratch)
						unpacked = (u8*)talloc(total);
					else
						unpacked = (u8*)ealloc(total);

					if (unpacked)
					{
						u8 *writepos = unpacked;

						// transfer pre-read portion to dest, including prefix
						u8 *readpos = (u8*)(&header);
						qmemcpy(writepos, readpos, sizeof(header));
						writepos += sizeof(header);
						remaining -= sizeof(header);
		
						// read remainder in-place
						fread(writepos, 1, remaining, fh);
					}

					fclose(fh);

					reg8(a4) = 0;

					if (_psize)
						_psize[0] = total;

					return unpacked;
				}
				else
				{
					dprintf("loading raw: [%s]\n", _name);

					// if raw asset has no size prefix we need to find the size directly
					fseek(fh, 0, SEEK_END);
					total = ftell(fh);
					fseek(fh, 0, SEEK_SET);

					//printf("filesize = %d\n", total);

					u8 *unpacked = nullptr;

					if (total)
					{
						// placement would overrun
						if (_smax && (total > _smax))
						{
							eprintf("error: asset too large: [%s]:%d\n", _name, total);

							fclose(fh);
							reg8(a4) = 0;
							return nullptr;
						}

						if (_flags & af_asset_scratch)
							unpacked = (u8*)talloc(total);
						else
							unpacked = (u8*)ealloc(total);

						if (unpacked)
						{
							// read file from beginning
							fread(unpacked, 1, total, fh);
						}
					}
		
					fclose(fh);
			
					reg8(a4) = 0;

					if (_psize)
						_psize[0] = total;

					return unpacked;
				}
			}
			else
			{
				// unwrapped, don't load but indicate exists

				dprintf("special handling: [%s]\n", _name);

				fclose(fh);
				reg8(a4) = 0;

				if (_psize)
				{
					_psize[0] = 0x40000000;
				}

				return nullptr;
			}
		}
	}

	// unreachable
	return nullptr;
}


/*
u8* unwrap_asset
(
	const char *const _name,	// filename
	bool _load_raw,				// load raw version anyway, if not p2e wrapped (otherwise return -1 so caller can attempt load)
	bool _raw_size_prefix,		// raw version has size prefix, avoids need to derive filesize by other means
	u32 *_psize
)
{
	reg8(a4) = 1;

	FILE *fh = fopen(_name, "rb");
	if (fh == 0)
	{
		reg8(a4) = 0;

		printf("error: p2e unwrap fail: %s\n", _name);
		
		return nullptr;
	}
	else
	{
		// read p2e header (8 bytes)
		u32 p2eh[2];
		fread(p2eh, 1, 8, fh);
		u32 rawsize = p2eh[0];

		endianswap32(&p2eh[0], 1);

		// check for p2e wrapping signature
		if ((p2eh[1] == 'pk2e') && (p2eh[0] < (1<<21)))
		{
			// p2e-wrapped asset

			// reset
			fseek(fh, 4, 0);

			u32 packedsize = p2eh[0];

			u8 *temp_ptr = (u8*)ealloc(packedsize);
			fread(temp_ptr, 1, packedsize, fh);
			fclose(fh);

			reg8(a4) = 0;

			// skip 'pk2e' ident
			u8* data_ptr = temp_ptr; data_ptr += 4;

			// fetch size of unpacked data
			u32 datasize = *((u32*)data_ptr); data_ptr += 4;
			endianswap32(&datasize, 1);

			u8* place = (u8*)ealloc(datasize);
			if (place)
			{
				if (_psize)
				{
					*_psize = datasize;
				}

				// unpack
				qmemclr(place, datasize);
				DePack2e_68k(data_ptr, place);

				// release file buffer
				efree(temp_ptr);

				return place;
			}
			else
			{
				printf("error: p2e unwrap failed allocate space: %s\n", _name);

				// release file buffer
				efree(temp_ptr);

				return nullptr;
			}
		}
		else
		{
			if (_load_raw)
			{
				// plain asset, no p2e wrapping
				// can be loaded as contiguous blob

				if (_raw_size_prefix)
				{
					// we know the size from the prefix/header
					rawsize += 4;
				}
				else
				{
					// if raw asset has no size prefix we need to find the size directly
					fseek(fh, 0, SEEK_END);
					rawsize = ftell(fh);
				}

				if (_psize)
				{
					*_psize = rawsize;
				}

				// reset
				fseek(fh, 0, 0);

				// allocate space
				u8* passet = (u8*)ealloc(rawsize);

				// read main part of file
				fread(passet, 1, rawsize, fh);

				fclose(fh);

				reg8(a4) = 0;

				return passet;
			}
			else
			{
				reg8(a4) = 0;

				// signal caller that file is raw but can't be loaded as contiguous block
				return (u8*)-1;
			}
		}
	}
				
	reg8(a4) = 0;

	return nullptr;
}
*/

static inline u32 ror(u32 _a, const u32 _c)
{
	__asm__
	(
"										\
		ror.l %1,%0;					\
"
		: "+d"(_a)
	    : "i"(_c)
		: "cc"
	);

	return _a;
}

u32 gen_crc_lite(u16 *_data, u32 _words)
{
	// perform hash
	u32 CRC = 0;

	do
	{
		u16 data16 = *_data++;

		// xor
		CRC ^= (u32)data16;

		// ror 7
		CRC = ror(CRC, 7);

	} while ((--_words) != 0);

	return CRC;
}

u32 gen_crc(u16 *_data, u32 _words, u32 hash)
{
	// perform hash
	u32 CRC = 0;
	{
		static const u32 c_FNVPrime = 16777619;

		do
		{
			u16 data = *_data++;

			// xor
			hash ^= data;

			// mul
			// todo: can bake constant version for better code
			hash = mulu_32_32(hash, c_FNVPrime);
			//hash *= c_FNVPrime; // todo: use 68k mulu

			// ror 7
			hash = ror(hash, 7);

		} while ((--_words) != 0);

		CRC = hash;
	}

	return CRC;
}

// ---------------------------------------------------------------------------------------------------------------------
//	convert [8 nibble-pairs] to [4 plane-words]

agt_outline void rotate_n2p4(u16 *data, s32 words)
{
	__asm__
	(
		"move.l		%0,%%a0;"
		"move.l		%0,%%a1;"
		"move.l		%1,%%d0;"
		"subq.w		#1,%%d0"
		";"
		"llp%=:;"
		"moveq		#4-1,%%d6;"
		";"
		"wlp%=:;"
		"move.w		(%%a0)+,%%d7;"
		"moveq		#4-1,%%d5;"
		";"
		"nlp%=:;"
		"add.w		%%d7,%%d7;"
		"addx.w		%%d1,%%d1;"
		"add.w		%%d7,%%d7;"
		"addx.w		%%d2,%%d2;"
		"add.w		%%d7,%%d7;"
		"addx.w		%%d3,%%d3;"
		"add.w		%%d7,%%d7;"
		"addx.w		%%d4,%%d4;"
		"dbf		%%d5,nlp%=;"
		"dbf		%%d6,wlp%=;"
		"move.w		%%d4,(%%a1)+;"
		"move.w		%%d3,(%%a1)+;"
		"move.w		%%d2,(%%a1)+;"
		"move.w		%%d1,(%%a1)+;"
		"dbf		%%d0,llp%=;"
		";"
		:
		: "g"(data), "g"(words>>2)
		: "%%d0", "%%d1", "%%d2", "%%d3", "%%d4", "%%d5", "%%d6", "%%d7", "%%a0", "%%a1" 
	);
}

// ---------------------------------------------------------------------------------------------------------------------
