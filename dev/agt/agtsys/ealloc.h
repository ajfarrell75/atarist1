//======================================================================================================================
//	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
// 	memory interface - thin wrapper around malloc()
//	note: internals soon to be replaced with advanced allocator
//----------------------------------------------------------------------------------------------------------------------

#ifndef ealloc_h_
#define ealloc_h_

//----------------------------------------------------------------------------------------------------------------------

// allocate space
void* ealloc(int _s);

// free space
void efree(void* _p);

// allocate temporary space (can be flushed)
void* talloc(int _s);

// push temporary marker
void tpush();

// pop temporary marker - flushes all temporary space since last tpush()
void tpop();

//----------------------------------------------------------------------------------------------------------------------

// define memory buffer to use for scratch allocs (e.g. display backbuffer or other work area)
// note: required before using tpush()/tpop()/talloc()
void M_InitScratch(void * _base, int _size);

// reset all temporary space - equivalent to executing tpop() for all existing tpush()'s
void M_ResetScratch();

//----------------------------------------------------------------------------------------------------------------------

// running sum of all allocated space so far
extern int g_tracked_allocation_;

//----------------------------------------------------------------------------------------------------------------------

#endif // ealloc_h_