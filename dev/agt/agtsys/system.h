//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	system-level config, save/restore
//----------------------------------------------------------------------------------------------------------------------

#ifndef system_h_
#define system_h_

// ---------------------------------------------------------------------------------------------------------------------

#if !defined(AGT_CONFIG_SYS_GUARDX)
#define AGT_CONFIG_SYS_GUARDX (32)
#endif

#if !defined(AGT_CONFIG_SYS_GUARDY)
#define AGT_CONFIG_SYS_GUARDY (32)
#endif

// ---------------------------------------------------------------------------------------------------------------------
//	Include shared console code if any of the supported consoles is enabled

#if defined(AGT_CONFIG_DEBUG_CONSOLE_NATIVE) || defined(AGT_CONFIG_DEBUG_CONSOLE_UDEV)
#define AGT_CONFIG_DEBUG_CONSOLE_
#endif

// ---------------------------------------------------------------------------------------------------------------------

class machine
{
	bool open_;

public:

	machine()
		: open_(false)
	{
	}

	~machine()
	{
	}

	// publish system-specific metrics (e.g. machine type) which might be needed by other services
	void configure();

	// save full machine state
	void save();
	
	// take ownership over HW vectors & interrupt state
	// each individual service must then configure any interrupts needed
	void claim();

	// restore machine state for clean exit
	void restore();
};

// ---------------------------------------------------------------------------------------------------------------------
// debugging helpers

static inline void halt() { while (1) { } }

// ---------------------------------------------------------------------------------------------------------------------
// A nonzero value signals the display is locked & owned by AGT, preventing any C printfs from reaching the screen. 
// note: NATFEATS debugging continues to work in this mode!

extern "C" short g_displaylocked;
extern "C" short g_lock_consolerefresh;

// ---------------------------------------------------------------------------------------------------------------------
// Hatari NATFEATS interface was detected if nonzero

extern "C" short g_hnf_ok;

// ---------------------------------------------------------------------------------------------------------------------
// machine detection

extern "C" u8 bSTF;
extern "C" u8 bSTE;
extern "C" u8 bTT;
extern "C" u8 bMegaSTE;
extern "C" u8 bFalcon030;

extern "C" s32 STE_GetDisplayTimer(void);
extern "C" s32 STE_WaitDisplayTimerStart(void);

// ---------------------------------------------------------------------------------------------------------------------
// Hatari NATFEATS interface for 'remote' console/printf debugging without touching ST's display

#if defined(AGT_CONFIG_HATARI_DEBUG)
extern "C" 
{
	#include "3rdparty/natfeats.h"
}
#endif

// ---------------------------------------------------------------------------------------------------------------------
// console

enum AGTConsole
{
	AGTConsole_OFF = 0,
	AGTConsole_MINIMISED = 1,
	AGTConsole_MAXIMISED = 2
};

extern "C" void AGT_Sys_ConsoleMode(AGTConsole _mode);

// ---------------------------------------------------------------------------------------------------------------------
// builtin profiler

extern "C" void AGT_Sys_ProfilerInit();
extern "C" void AGT_Sys_ProfilerReset();
extern "C" void AGT_Sys_ProfilerDoReport(int _num);

agt_inline void prf_push(int _ctx)
{
#if defined(AGT_CONFIG_PROFILER)
	int tmp;
	__asm__ __volatile__ (
		"ori.w	#0x0700,%%sr;" \
		"move.l	0x44.w,%0;" \
		"move.w	0x40.w,-(%0);" \
		"move.w	%1,0x40.w;" \
		"move.l	%0,0x44.w;" \
		"andi.w	#0xfbff,%%sr;"
		: "=a"(tmp)
		: "X"(_ctx)
		: "cc"
	);
#endif
}

agt_inline void prf_set(int _ctx)
{
#if defined(AGT_CONFIG_PROFILER)
	__asm__ __volatile__ (
		"move.w	%0,0x40.w;" \
		:
		: "X"(_ctx)
		: "cc"
	);
#endif
}

agt_inline void prf_pop()
{
#if defined(AGT_CONFIG_PROFILER)
	int tmp;
	__asm__ __volatile__ (
		"ori.w	#0x0700,%%sr;" \
		"move.l	0x44.w,%0;" \
		"move.w	(%0)+,0x40.w;" \
		"move.l	%0,0x44.w;" \
		"andi.w	#0xfbff,%%sr;"
		: "=a"(tmp)
		:
		: "cc"
	);
#endif
}

enum : int
{
	//
	// start of system-defined profiling tokens
	//
	prf_undefined,
	prf_profiling,
	prf_mainloop,
	prf_idle,
	//
	prf_arena_misc,
	prf_arena_bgrestore,
	prf_arena_fill,
	//
	prf_ent_tick,
	prf_ent_spatial,
	prf_ent_interact,
	prf_ent_links,
	prf_ent_draw,
	//
	// end of system-defined profiling tokens
	//
	prf_USER_ 
	//
	// start of user-defined profiling tokens
};

#if defined(AGT_CONFIG_PROFILER)

// profiler auto-context object. profiles current C scope until scope closes or function returns.
template<int _ctx>
class autoprf
{
public:

	agt_inline autoprf() { prf_push(_ctx); }
	agt_inline ~autoprf() { prf_pop(); }
};

// macro wrapper for profiler auto-context
#define AUTOPRF(id) autoprf<prf_##id> _prf;

typedef const char* const strptr_t;

// individual profiling context definition
#define prf_context_def(str, uid) \
 str, (strptr_t)uid, 

// start of profiling context definitions
// (+register system-defined profiling contexts)
#define prf_context_begin \
extern "C" { \
__attribute__((used)) strptr_t g_prf_context_refs[] = { \
prf_context_def("undefined...",prf_undefined) \
prf_context_def("-profiling- ",prf_profiling) \
prf_context_def("main:loop",prf_mainloop) \
prf_context_def("main:idle",prf_idle) \
prf_context_def("arena:misc",prf_arena_misc) \
prf_context_def("arena:bgrestore",prf_arena_bgrestore) \
prf_context_def("arena:fill",prf_arena_fill) \
prf_context_def("ent:tick",prf_ent_tick) \
prf_context_def("ent:spatial",prf_ent_spatial) \
prf_context_def("ent:interact",prf_ent_interact) \
prf_context_def("ent:links",prf_ent_links) \
prf_context_def("ent:draw",prf_ent_draw)

// end of profiling context definitions
#define prf_context_end(n) \
}; \
__attribute__((used)) s32 gp_prf_context_refs = (s32)g_prf_context_refs; \
__attribute__((used)) s32 g_prf_context_num = (n); \
};

#else // defined(AGT_CONFIG_PROFILER)

#define AUTOPRF(id)
#define prf_context_begin
#define prf_context_def(str, uid)
#define prf_context_end(n)

#endif // defined(AGT_CONFIG_PROFILER)

// ---------------------------------------------------------------------------------------------------------------------
// supervisor/user & USP/SSP management stuff for MiNTlib-specific builds. 'minimal/tiny crt' builds don't require this

#if !defined(USE_TINY_CRT)
#define S_SUPER_SSP(_sz_) \
static const int ssp_size = _sz_>>2; \
u32 s_new_ssp[ssp_size]; \
u32 s_old_ssp; \
u32 s_old_usp;
#else
#define S_SUPER_SSP(_sz_)
#endif

#if !defined(USE_TINY_CRT)
#define S_SUPER \
	__asm__ __volatile__ ("move.l	%%sp,%0;" \
		: "=m"(s_old_usp) \
		: \
		: "cc" \
	); \
	s_old_ssp = Super(&s_new_ssp[ssp_size-64]);
#else
#define S_SUPER
#endif

#if !defined(USE_TINY_CRT)
#define S_USER \
	dprintf("usermode...\n"); \
	Super(s_old_ssp); \
	__asm__ __volatile__ ("move.l	%0,%%sp;" \
		: \
		: "m"(s_old_usp) \
		: "cc" \
	);
#else
#define S_USER
#endif

// ---------------------------------------------------------------------------------------------------------------------
// defines program entrypoint main(argc,argv) and executes AGT_EntryPoint in supervisor mode
// ---------------------------------------------------------------------------------------------------------------------
// notes:
// ---------------------------------------------------------------------------------------------------------------------
// - USE_TINY_CRT employs AGT's own thin CRT which selects supervisor mode early, so the super
//   switch is omitted from this stage when using this mode. it is retained for compatibility
//   with LIBC variants like MiNTlib.
// - Supexec() is preferred because Super() changes the active stack pointer in the current
//   context,  which in turn messes up the compiled C code's stack frame when combined with
//   -fomit-frame-pointer.

extern "C" int AGT_EntryPointPre();
extern "C" int AGT_EntryPointPost();
extern "C" int AGT_EntryPoint();

#if !defined(USE_TINY_CRT)
#define AGT_MAIN								\
void AGT_SuperStart()							\
{												\
	__asm__ __volatile__						\
	(											\
		"										\
		move.l		%0,%%a0;					\
		move.l		%%a0,%%d0;					\
		subq.l		#4,%%d0;					\
		and.w		#-16,%%d0;					\
		move.l		%%d0,%%a0;					\
		move.l		%%sp,-(%%a0);				\
		move.l		%%usp,%%a1;					\
		move.l		%%a1,-(%%a0);				\
		move.l		%%a0,%%sp;					\
		movem.l		%%d1-%%d7/%%a2-%%a6,-(%%sp);	\
		jsr			(%2);							\
		movem.l		(%%sp)+,%%d1-%%d7/%%a2-%%a6;	\
		movem.l		%%d1-%%d7/%%a2-%%a6,-(%%sp);	\
		jsr			(%1);							\
		movem.l		(%%sp)+,%%d1-%%d7/%%a2-%%a6;	\
		movem.l		%%d1-%%d7/%%a2-%%a6,-(%%sp);	\
		jsr			(%3);							\
		movem.l		(%%sp)+,%%d1-%%d7/%%a2-%%a6;	\
		move.l		(%%sp)+,%%a0;				\
		move.l		%%a0,%%usp;					\
		move.l		(%%sp)+,%%sp;				\
		"										\
		:										\
		: "p"(&s_new_ssp[ssp_size-16]), "a"(&AGT_EntryPoint), "a"(&AGT_EntryPointPre), "a"(&AGT_EntryPointPost) \
		: "%%d0", "%%a0", "%%a1", "cc"			\
	);											\
}												\
int main(int argc, char **argv)					\
{												\
	Supexec(&AGT_SuperStart, 0,0,0,0,0);		\
	return 0;									\
}
#else // !defined(USE_TINY_CRT)
#define AGT_MAIN								\
int main(int argc, char **argv)					\
{												\
	AGT_EntryPointPre();						\
	int r = AGT_EntryPoint();					\
	AGT_EntryPointPost();						\
	return r;									\
}
#endif // !defined(USE_TINY_CRT)

// ---------------------------------------------------------------------------------------------------------------------
// access to timing rasters from C code

#ifdef TIMING_RASTERS_MAIN
#define TIMING_MAIN(_rgb_) { reg16(00000064) = _rgb_; reg16(ffff8240) = _rgb_; }
#else
#define TIMING_MAIN(_rgb_)
#endif		

#ifdef TIMING_RASTERS
#define TIMING(_rgb_) { reg16(00000064) = _rgb_; reg16(ffff8240) = _rgb_; }
#else
#define TIMING(_rgb_)
#endif		

#define EXPRESS32(_v_) \
	__asm__ __volatile__ ("move.l	%0,%%d7; illegal;" \
		: \
		: "g"(_v_) \
		: "%%d7", "cc" \
	);

#define EXPRESS16(_v_) \
	__asm__ __volatile__ ("move.w	%0,%%d7; illegal;" \
		: \
		: "g"(_v_) \
		: "%%d7", "cc" \
	);

#define EXPRESS8(_v_) \
	__asm__ __volatile__ ("move.b	%0,%%d7; illegal;" \
		: \
		: "g"(_v_) \
		: "%%d7", "cc" \
	);

// ---------------------------------------------------------------------------------------------------------------------
// estimate of program end address, for memory tracking

extern int end;

// ---------------------------------------------------------------------------------------------------------------------

#endif // system_h_
