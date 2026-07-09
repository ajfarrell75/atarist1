//======================================================================================================================
//	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
// 	memory interface
//----------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------------
//	system headers

#include <mint/sysbind.h>

#include <memory.h>

// ---------------------------------------------------------------------------------------------------------------------
//	Our AGT headers

#include "common_cpp.h"

#include "ealloc.h"

// ---------------------------------------------------------------------------------------------------------------------

int g_tracked_allocation_ = 0;
int g_tracked_tallocation_ = 0;
int s_tempstack[8];
int s_tempstackpos = 0;
int g_temphead = 0;
int g_tempstart = 0;
int g_tempend = 0;

#if defined(USE_TINY_CRT)

extern "C"
{

	// ----------------------------------------------------------------------------------------
	// proxy for std malloc(), routed through our ealloc system

	void * malloc ( size_t size )
	{
		dprintf("::malloc!\n");
		return (void*)ealloc( size );
	}

	// ----------------------------------------------------------------------------------------
	// proxy for std calloc(), routed through our ealloc system

	void * calloc ( size_t num, size_t size )
	{
		dprintf("::calloc!\n");
		// todo: long multiply
		char *ptr = (char*) ealloc( size*num );
		// todo: long multiply
		qmemclr(ptr, size*num);
		return ptr;
	}

	// ----------------------------------------------------------------------------------------
	// proxy for std free(), routed through our ealloc system

	void free ( void * ptr )
	{
		dprintf("::free!\n");
		if ( ptr )
		{
	//		Mfree ( ptr );
			efree(ptr);
		}
	}

	// ----------------------------------------------------------------------------------------
	// proxy for std realloc(), routed through our ealloc system

	void * realloc ( void * ptr, size_t size )
	{
		dprintf("::realloc!\n");
		char *newptr = (char*) ealloc( size );
		qmemcpy ( newptr, ptr, size );
	//	Mfree ( ptr );
		efree(ptr);
		return newptr;
	}

}

#endif

// ----------------------------------------------------------------------------------------

void* operator new(size_t _size)
{
//	printf("::new!\n");
	return ealloc(_size);
}

// ----------------------------------------------------------------------------------------

void* operator new[](size_t _size)
{
//	printf("::new[]!\n");
	return ealloc(_size);
}

// ----------------------------------------------------------------------------------------

void operator delete(void* _ptr)
{
//	printf("::delete!\n");
	efree(_ptr);
}

// ----------------------------------------------------------------------------------------

void operator delete[](void* _ptr)
{
//	printf("::delete[]!\n");
	efree(_ptr);
}

// ---------------------------------------------------------------------------------------------------------------------
// TODO: sized delete for C++14

void operator delete(void*, std::size_t)
{
}

void operator delete[](void*, std::size_t)
{
}

// ---------------------------------------------------------------------------------------------------------------------

void* ealloc(int _s)
{
	int v, va, as, *pva;

	as = _s + ((4*2) + 4);

#if defined(USE_TINY_CRT)
	v = (int)Malloc(as);
#else
	v = (int)malloc(as);
#endif

	if (!v)
	{
		eprintf("error: ealloc(%d) failure!\n", as);
		while (1) { }
		return 0;
	}

	g_tracked_allocation_ += as;
	

	va = (v + ((4*2) + 4)) & ~3;
	pva = (int*)va;
	*(pva-1) = v;
	*(pva-2) = as;

#if defined(USE_MEMTRACE)
	printf("ealloc: +[%d] = [%d] tracked [OA=%x, IA=%x, [OA=%x,IS=%x]]\n", 
		as, 
		g_tracked_allocation_, 
		v, 
		(int)pva,
		*(pva-1),
		*(pva-2)
	);
#endif

//	qmemclr(pva, _s);

	return (void*)pva;
}

// push scratch head state - allows scratch allocs to be nested
void tpush()
{
	s_tempstack[s_tempstackpos++] = g_temphead;
	s_tempstack[s_tempstackpos++] = g_tracked_tallocation_;
}

// pop scratch head state - deallocs any scratch allocs since last push
void tpop()
{
	g_tracked_tallocation_ = s_tempstack[--s_tempstackpos];
	g_temphead = s_tempstack[--s_tempstackpos];
}

void* talloc(int _s)
{
	int v, va, as, *pva;

	as = _s + ((4*2) + 4);

	if (as > (g_tempend - g_temphead))
	{
		// not enough space in scratch area - alloc properly
		wprintf("warning: scratch overflow: [%d] > [%d]\n", 
			as, (g_tempend - g_temphead));
		return ealloc(_s);
	}
	else
	{
		// scratch area has space - create block there & move head to account for it
		g_tracked_tallocation_ += as;

		v = g_temphead;
		g_temphead += as;
		
#if defined(USE_MEMTRACE)
		printf("talloc: +[%d] = [%d] tide [%d] tracked\n", 
			as, 
			(g_temphead - g_tempstart), 
			g_tracked_tallocation_);
#endif

		// use same block format as heap blocks, for common dealloc
		va = (v + ((4*2) + 4)) & ~3;
		pva = (int*)va;
		*(pva-1) = v;
		*(pva-2) = -as;

		qmemclr(pva, _s);

		return (void*)pva;
	}
}

void efree(void* _p)
{
	int v, as;

	if (!_p)
		return;

	// retrieve original size/type from block header
	as = *(((int*)_p) - 2);

	if (as > 0)
	{
		// block to be freed was on the heap - dealloc properly
		g_tracked_allocation_ -= as;

		v = *(((int*)_p) - 1);

#if defined(USE_TINY_CRT)
		Mfree((void*)v);
#else
		free((void*)v);
#endif

#if defined(USE_MEMTRACE)
		printf("ealloc: -[%d] = [%d] tracked\n", as, g_tracked_allocation_);
#endif
	}
	else
	{
		// block to be freed was in temporary/scratch area so we only need
		// to account for tracked total and reset tide if total depletes
		g_tracked_tallocation_ += as;
#if defined(USE_MEMTRACE)
		printf("talloc: -[%d] = [%d] tide [%d] tracked\n", 
			-as, 
			(g_temphead - g_tempstart),
			g_tracked_tallocation_);
#endif
		// can't move head until depleted due to unpredictable dealloc order
		if (0 == g_tracked_tallocation_)
		{
			g_temphead = g_tempstart;
#if defined(USE_MEMTRACE)
			printf("talloc: tide autoreset...\n");
#endif
		}
	}
}

void M_InitScratch(void * _base, int _size)
{
	g_tempstart = g_temphead = (int)_base;
	g_tempend = g_tempstart + _size;
	qmemclr((void*)g_tempstart, (g_tempend - g_tempstart));
}

void M_ResetScratch()
{
//	qmemclr(g_tempstart, (g_tempend - g_tempstart));
	g_tempstart = g_temphead = g_tempend = 0;
}
