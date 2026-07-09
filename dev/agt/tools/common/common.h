//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2022
//======================================================================================================================
//	common macros, definitions, tools & helper code
//----------------------------------------------------------------------------------------------------------------------

#ifndef tools_common_h_
#define tools_common_h_

//----------------------------------------------------------------------------------------------------------------------
//  debugging
//----------------------------------------------------------------------------------------------------------------------

// TODO: aarch64
#define xassert(_x_) \
{ \
	if (!(_x_)) \
	{ \
		__asm { int 3 }; \
	}; \
}

//----------------------------------------------------------------------------------------------------------------------

// TODO: should be size_t - but check this isn't used in file header creation before fixing
typedef unsigned int datasize_t;

typedef int32_t long_t;
typedef int16_t word_t;
typedef int8_t byte_t;

typedef uint32_t ulong_t;
typedef uint16_t uword_t;
typedef uint8_t ubyte_t;

// alternates, for working with engine types
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

typedef uint64_t u64;

//----------------------------------------------------------------------------------------------------------------------

// meh: this arrived with BMP and lingers on...

typedef struct RGB
{
    ubyte_t blue;
    ubyte_t green;
    ubyte_t red;
} RGB;

typedef struct iRGB
{
    int blue;
    int green;
    int red;
} iRGB;

//----------------------------------------------------------------------------------------------------------------------
//  value transforms
//----------------------------------------------------------------------------------------------------------------------

// note: legacy - prefer htons
static inline uword_t endianswap16(const uword_t _v)
{
	return ((_v & 0xFF00) >> 8) | ((_v & 0x00FF) << 8);
}

// note: legacy - prefer htonl
static inline ulong_t endianswap32(const ulong_t _v)
{
	return  ((_v & 0xFF000000) >> 24) |
			((_v & 0x00FF0000) >> 8) |
			((_v & 0x0000FF00) << 8) |
			((_v & 0x000000FF) << 24);
}


static inline int xmin(int _x, int _y)
{
	return (_x < _y) ? _x : _y;
}

static inline int xmax(int _x, int _y)
{
	return (_x > _y) ? _x : _y;
}


ulong_t rofl32(ulong_t _v, size_t _bits)
{
    return (_v >> _bits) | (_v << ((sizeof(_v) << 3) - _bits));
}

ulong_t lofl32(ulong_t _v, size_t _bits)
{
    return (_v << _bits) | (_v >> ((sizeof(_v) << 3) - _bits));
}

uword_t rofl16(uword_t _v, size_t _bits)
{
    return (_v >> _bits) | (_v << ((sizeof(_v) << 3) - _bits));
}

uword_t lofl16(uword_t _v, size_t _bits)
{
    return (_v << _bits) | (_v >> ((sizeof(_v) << 3) - _bits));
}


template<class T>
inline const T bceil(const T _v)
{
	T t = 0;

	while ((1 << t) < _v)
	{
		++t;
	}

	return (t);	
}

//----------------------------------------------------------------------------------------------------------------------
//  file string operations
//----------------------------------------------------------------------------------------------------------------------

static void replace_extension(std::string &_fname, const char* const _ext)
{
	size_t dot = _fname.find_last_of(".");
	if (dot != std::string::npos)
	{
		_fname = _fname.substr(0, dot+1);
		_fname.append(_ext);
	}
	else
	{
		_fname.append(".");
		_fname.append(_ext);
	}
}

static void remove_extension(std::string &_fname)
{
	size_t dot = _fname.find_last_of(".");
	if (dot != std::string::npos)
		_fname = _fname.substr(0, dot);
}

inline char sanitise_csv_integer(char ch)
{
	unsigned char uc = (unsigned char)ch;
	if ((uc >= '0' && uc <= '9') ||
		(uc >= 'a' && uc <= 'f') ||
		(uc >= 'A' && uc <= 'F') ||
		(uc == '-') ||
		(uc == 'x'))
		return ch;

	return ' ';
}

//----------------------------------------------------------------------------------------------------------------------
#endif // tools_common_h_
//----------------------------------------------------------------------------------------------------------------------
