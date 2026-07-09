//======================================================================================================================
//	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
// 	common header (c++)
//----------------------------------------------------------------------------------------------------------------------

#ifndef common_h_
#define common_h_

//----------------------------------------------------------------------------------------------------------------------
// 	common includes, types, helper functions and other misc. stuff
//----------------------------------------------------------------------------------------------------------------------

#include <string>
#include <vector>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <math.h>

#include "../3rdparty/binarylit.h"
#include "rnd.h"

// --------------------------------------------------------------------
//	types & portability stuff

typedef unsigned char byte_t;
typedef unsigned short word_t;
typedef unsigned long long_t;
typedef signed long datasize_t;
typedef signed long ptrsize_t;

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
typedef signed char s8;
typedef signed short s16;
typedef signed long s32;

// --------------------------------------------------------------------

// just in case tolower is not in the host env headers...
inline char mytolower(char in)
{
  if(in<='Z' && in>='A')
    return in-('Z'-'z');
  return in;
} 

inline char sanitise_csv_arg(char ch)
{
	unsigned char uc = (unsigned char)ch;
	if ((uc >= '0' && uc <= '9') ||
		(uc == '-') ||
		(uc == 'x'))
		return ch;

	return ' ';
}

// --------------------------------------------------------------------

template<class T>
inline T xmax(T a, T b) { return a > b ? a : b; }
template<class T>
inline T xmin(T a, T b) { return a < b ? a : b; }

template<class T>
inline T xclamp(T v, T a, T b) { return xmin(xmax(v, a), b); }

template<class T>
inline T xabs(T x) { return x >= T(0) ? x : -x; }

template <class T>
inline T xsign(const T& _v) { return (_v > T(0)) ? T(1) : ((_v < T(0)) ? T(-1) : T(0)); }

// sign-safe modulo (a % b)
// note: (0 < b < 2^<bits>)
template<class T> inline T tmod(T a, T b)
{
	T r = a % b;
	if(r < T(0))
		r += xabs(b);
	return r;
}

// sign-safe 16bit modulo (a % b)
// note: (0 < b < 2^16)
static inline s16 tmod16(s32 a, s16 b)
{
/*
	s16 r = a % b;
	if(r < 0)
		r += xabs(b);
	return r;
*/
	__asm__
	(
"										\
		divs.w %1,%0;					\
		swap %0;						\
		tst.w %0;						\
		bpl.s .nn1%=;					\
		tst.w %1;						\
		bpl.s .nn2%=;					\
		neg.w %1;						\
.nn2%=:;								\
		add.w %1,%0;					\
.nn1%=:;								\
"
		: "+d" (a), "+d" (b)
	    :
		: "cc"
	);

	return a;
}

// fast 16bit sign(x)
static inline s16 tsign16(s16 _v)
{
	// -10 = -1
	// 0   = 0
	// 10  = 1

	s16 o;

	__asm__
	(
"										\
		move.w %0,%1;					\
		add.w %1,%1;					\
		subx.w %1,%1;					\
		neg.w %0;						\
		addx.w %1,%1;					\
"
		: "+d" (_v), "=d" (o)
	    :
		: "cc"
	);

	return o;
}

static inline u32 mulu_32_32(u32 _a, u32 _b)
{
//	P = A0*B0
//	Q = (A0*B1) << 16
//	R = (A1*B0) << 16

	u32 c,d,e;

	__asm__
	(
"										\
		move.w %0,%2;					\
		move.w %0,%4;					\
										\
		mulu.w %1,%2;					\
										\
		swap   %0;						\
		mulu.w %1,%0;					\
										\
		swap   %1;						\
		mulu.w %4,%1;					\
										\
		add.l  %1,%0;					\
		swap   %0;						\
		clr.w  %0;						\
										\
		add.l  %2,%0;					\
"
		: "+d"(_a), "+d"(_b), "+d"(c), "+d"(d), "+d"(e)
	    : 
		: "cc"
	);

	return _a;
}

// --------------------------------------------------------------------
// bit hacks

inline int msb(unsigned long v) 
{
	static const int pos[32] = 
	{
		0,  1,  28, 2,  29, 14, 24, 3,
		30, 22, 20, 15, 25, 17, 4,  8, 
		31, 27, 13, 23, 21, 19, 16, 7, 
		26, 12, 18, 6,  11, 5,  10, 9
	};
	
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v = (v >> 1) + 1;
	
	return pos[(v * 0x077CB531UL) >> 27];
}

inline unsigned long nlpo2(unsigned long x)
{
        x |= (x >> 1);
        x |= (x >> 2);
        x |= (x >> 4);
        x |= (x >> 8);
        x |= (x >> 16);
        return (x+1);
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

inline unsigned long reverse(unsigned long x)
{
    x = ((x >> 1)  & 0x55555555u) | ((x & 0x55555555u) << 1);
    x = ((x >> 2)  & 0x33333333u) | ((x & 0x33333333u) << 2);
    x = ((x >> 4)  & 0x0f0f0f0fu) | ((x & 0x0f0f0f0fu) << 4);
    x = ((x >> 8)  & 0x00ff00ffu) | ((x & 0x00ff00ffu) << 8);
    x = ((x >> 16) & 0x0000ffffu) | ((x & 0x0000ffffu) << 16);
    return x;
}

//------------------------------------------------------------------------*
// read or write a bigendian word in a host- and alignment-insensitive way
// meant to be safe and portable, not necessarily quick.

inline word_t read_be_word(void* _addr)
{
	return ((word_t)(((byte_t*)_addr)[0]) << 8) | 
		   ((word_t)(((byte_t*)_addr)[1]));
}	

inline void write_be_word(void* _addr, word_t _val)
{
	((byte_t*)_addr)[0] = (_val >> 8);
	((byte_t*)_addr)[1] = (_val & 0xFF);
}	

// byte & word versions redundant, but make for less confusing source code

inline byte_t read_byte(void* _addr)
{
	return *((byte_t*)_addr);
}	

inline void write_byte(void* _addr, byte_t _val)
{
	*((byte_t*)_addr) = _val;
}

inline word_t read_word(void* _addr)
{
	return *((word_t*)_addr);
}	

inline void write_word(void* _addr, word_t _val)
{
	*((word_t*)_addr) = _val;
}	

// copies native words in source buffer to BE words in destination buffer
// note: inefficient, but it's only used for emitting raw image data to file
inline void copy_be_words(word_t* _dst, word_t* _src, datasize_t _size)
{
	for (int i = 0; i < _size; ++i)
		write_be_word(_dst++, read_word(_src++));
}	

inline void endianswap32(long_t* _data, datasize_t _num)
{
	for (datasize_t i = 0; i < _num; ++i)
	{
		long_t v = _data[i];

		long_t vs = 
			((v & 0x000000FF) << 24) | 
			((v & 0x0000FF00) << 8) | 
			((v & 0x00FF0000) >> 8) | 
			((v & 0xFF000000) >> 24)
		;
		
		_data[i] = vs;
	}
}

inline void endianswap16(word_t* _data, datasize_t _num)
{
	for (datasize_t i = 0; i < _num; ++i)
	{
		word_t v = _data[i];

		word_t vs = 
			((v & 0x00FF) << 8) | 
			((v & 0xFF00) >> 8)
		;
		
		_data[i] = vs;
	}
}

// --------------------------------------------------------------------

// HW register access
#define reg8(_a_) *(volatile u8*)(0x##_a_)
#define reg16(_a_) *(volatile u16*)(0x##_a_)
#define reg32(_a_) *(volatile u32*)(0x##_a_)

// compiler optimization hints for flow control logic
#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

// caution: cheaper for pointers in memory, expensive for those already in registers
#define ptr_null(_p_) (!(*(short*)(&_p_)))

// ---------------------------------------------------------------------------------------------------------------------

#ifdef PROFILING
// in profiling builds, disable inlining to get accurate code identification
#define PRFINLINE __attribute__((noinline))
#else
// in optimized builds, enforce inlining
#define PRFINLINE static inline __attribute__((always_inline))
#endif

// force inlining for small functions
#define agt_inline inline __attribute__((always_inline))

// inhibit inlining for big functions (or for easier debugging)
#define agt_outline __attribute__((noinline))

#define agt_used __attribute__((used))
#define agt_unused __attribute__((unused))

#define agt_hotcode __attribute__((hot))
#define agt_coldcode __attribute__((cold))
#define agt_argnonnull __attribute__((nonnull))
#define agt_retnonnull __attribute__((returns_nonnull))
#define agt_noreturn __attribute__((noreturn))
#define agt_pure __attribute__((pure))
#define agt_malloc __attribute__((malloc))
#define agt_cconstexpr __attribute__((const))
#define agt_flatten __attribute__((agt_flatten))
#define agt_optnone __attribute__((optimize("O0"))
#define agt_assumealigned(_a_) __attribute__((assume_aligned(_a_))

// ---------------------------------------------------------------------------------------------------------------------
//	quick alternatives to common mem ops
// ---------------------------------------------------------------------------------------------------------------------

extern "C" void* qmemcpy(void *dest, const void *src, size_t num);
extern "C" void* qmemset(void *dest, int value, size_t num);
extern "C" int qmemclr(void *dest, size_t num);
extern "C" int qmemset00(void *dest, size_t num);
extern "C" int qmemsetff(void *dest, size_t num);

extern "C" int agtprintf(const char *pFormat, ...);
extern "C" int agtprintf_coded(const char *pFormat, ...);

//#define printf agtprintf

//#define eprintf //printf
//#define wprintf //printf
//#define dprintf //printf

// enable these for printf/natfeats debugging

#if defined(AGT_DEBUG_BUILD)
#define AGT_PRINT_LEVEL_ (2)
#else
#if defined(AGT_TESTING_BUILD)
#define AGT_PRINT_LEVEL_ (1)
#else
#define AGT_PRINT_LEVEL_ (0)
#endif
#endif

#if defined(AGT_CONFIG_QUIET)
#define AGT_CONFIG_ALLOW_PRINT (0)
#else
#define AGT_CONFIG_ALLOW_PRINT (1)
#endif

extern "C" u8 g_concode;

// std printf - always emitted
#define printf(...) { if ((1) && AGT_CONFIG_ALLOW_PRINT) { agtprintf(__VA_ARGS__) ; } }

// error printf (debug/testing builds)
#define eprintf(...) { if ((AGT_PRINT_LEVEL_>=0)&& AGT_CONFIG_ALLOW_PRINT) { g_concode = 1; agtprintf_coded(__VA_ARGS__) ; } }
// warning printf (debug/testing builds)
#define wprintf(...) { if ((AGT_PRINT_LEVEL_>=1)&& AGT_CONFIG_ALLOW_PRINT) { g_concode = 3; agtprintf_coded(__VA_ARGS__) ; } }
// debug printf (debug builds - detailed info only)
#define dprintf(...) { if ((AGT_PRINT_LEVEL_>=2)&& AGT_CONFIG_ALLOW_PRINT) { g_concode = 2; agtprintf_coded(__VA_ARGS__) ; } }

// progress printf (debug/testing builds - loading/parsing or any significant envents)
#define pprintf(...) { if ((AGT_PRINT_LEVEL_>=0)&& AGT_CONFIG_ALLOW_PRINT) { agtprintf(__VA_ARGS__) ; } }
// entity printf (entity manager detail)
#define entprintf(...) { if ((AGT_PRINT_LEVEL_>=1)&& AGT_CONFIG_ALLOW_PRINT) { g_concode = 6; agtprintf_coded(__VA_ARGS__) ; } }
// mapmod printf (mapmod/multimod detail)
#define mmprintf(...) { if ((0) && AGT_CONFIG_ALLOW_PRINT) { agtprintf(__VA_ARGS__) ; } }

// --------------------------------------------------------------------

// just inline stuff - don't bother with heuristics & guesswork
#define FORCEINLINE inline __attribute__((always_inline))

// inhibit tranlation of normal code into memcpy/memset calls - e.g. within inner loops
#define NO_INTRINSIC_MEMXXX __attribute__((optimize("-fno-tree-loop-distribute-patterns")))

#define AGT_STRINGIFY(x) #x
#define AGT_TOSTRING(x) AGT_STRINGIFY(x)
#define AGT_AT __FILE__ ":" AGT_TOSTRING(__LINE__)

#define AGT_DBG_HALT(_c_) { while (1) { reg16(ffff8240) ^= _c_; }  }
#define AGT_HALT() { while (1) { ; }  }

extern "C" void SYS_Assert();

// fancy assert which invokes vector $1C with a message param
#define xassert(_e_) { if (!(_e_)) { const char* const __msg = "assert @ " AGT_AT; __asm__ __volatile__ ("move.l %0,-(%%sp); jsr _SYS_Assert;" : : "p"(__msg) : "cc"); } };

// --------------------------------------------------------------------

#endif // common_h_
