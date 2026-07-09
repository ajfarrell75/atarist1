//======================================================================================================================
//	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
// 	entity management systems
//----------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------------
//	system headers

// ---------------------------------------------------------------------------------------------------------------------
//	Our AGT headers

#include "common_cpp.h"

#include "system.h"

#include "spritesheet.h"
#include "slabsheet.h"
#include "spritelib.h"

#include "entity.h"

//----------------------------------------------------------------------------------------------------------------------

extern "C" void EntityTick_68k();
extern "C" void EntitySpatial_68k();

#if defined(AGT_CONFIG_DEBUG_CONSOLE_)
extern "C" void AGT_Sys_ConsoleRefresh();
#endif

//----------------------------------------------------------------------------------------------------------------------

template<int c_size>
inline void copy(void *const _dst, void *const _src)
{
	int remaining = c_size;

	void *src = _src;
	void *dst = _dst;

	if (remaining & -4)
	{
		s32 *src32 = (s32*)src;
		s32 *dst32 = (s32*)dst;

		while (remaining & -4)
		{
//			*dst32++ = *src32++;

			__asm __volatile__
			(
				"move.l (%0)+,(%1)+;"
				: "+a"(src32), "+a"(dst32)
				:
				:
			);

			remaining -= 4;
		}

		src = src32;
		dst = dst32;
	}

	if (remaining & -2)
	{
		s16 *src16 = (s16*)src;
		s16 *dst16 = (s16*)dst;

		while (remaining & -2)
		{
//			*dst16++ = *src16++;

			__asm __volatile__
			(
				"move.w (%0)+,(%1)+;"
				: "+a"(src16), "+a"(dst16)
				:
				:
			);

			remaining -= 2;
		}

		src = src16;
		dst = dst16;
	}

	if (remaining)
	{
		s8 *src8 = (s8*)src;
		s8 *dst8 = (s8*)dst;

		while (remaining)
		{
//			*dst8++ = *src8++;

			__asm __volatile__
			(
				"move.b (%0)+,(%1)+;"
				: "+a"(src8), "+a"(dst8)
				:
				:
			);

			remaining--;
		}

		src = src8;
		dst = dst8;
	}
}

// ---------------------------------------------------------------------------------------------------------------------

// dummy tick function, does nothing
static void nulltick(entity_t *) { } 

// ---------------------------------------------------------------------------------------------------------------------

s16 g_debugdraw = 0;

static s16 s_spawnrev = 0;

static entitybase_t s_entity_tick_beforehead;
static entitybase_t s_entity_tick_head;
static entitybase_t s_entity_tick_tail;
static entitybase_t s_entity_tick_aftertail;
entitybase_t *gp_entity_tick_tail = &s_entity_tick_tail;
entitybase_t *gp_entity_tick_head = &s_entity_tick_head;
entitybase_t *gp_entity_tick_aftertail = &s_entity_tick_aftertail;

#if defined(ENABLE_SPATIAL_X)
static entitybase_t s_entity_x_head;
static entitybase_t s_entity_x_tail;
static entitybase_t s_entity_x_aftertail;
entitybase_t *gp_entity_x_head = &s_entity_x_head;
entitybase_t *gp_entity_x_aftertail = &s_entity_x_aftertail;
#endif

#if defined(ENABLE_SPATIAL_Y)
static entitybase_t s_entity_y_head;
static entitybase_t s_entity_y_tail;
static entitybase_t s_entity_y_aftertail;
entitybase_t *gp_entity_y_head = &s_entity_y_head;
entitybase_t *gp_entity_y_aftertail = &s_entity_y_aftertail;
#endif

s16 s_entity_remove_count = 0;
entitybase_t *s_entity_removes[c_entity_max_removes];

s16 s_entity_link_count = 0;
entitybase_t *s_entity_links[c_entity_max_links];

entitybase_t *entities_freelist[c_max_entities];
s16 entities_freelist_count = 0;

#if defined(AGT_ENTITY_USER_EXTENSION)
extern entity_t entities[c_max_entities];
#else
entity_t entities[c_max_entities];
#endif

#if defined(AGT_CONFIG_ENTITY_HISTOGRAM)
// required only for debugging entity counts & lifespans
static s16 entity_histogram[256];
#endif

static s16 s_max_entities = 0;

extern "C" { entitybase_t *g_pe_viewport = nullptr; };

// ---------------------------------------------------------------------------------------------------------------------

static int s_entity_size = 0;
static char * s_pentity_raw_pool = 0;

#if defined(AGT_ENTITY_USER_EXTENSION)
void EntitySetup(int _numtypes, int _entity_size)
#else
void EntitySetup(int _numtypes)
#endif
{
	s_max_entities = _numtypes;

#if defined(AGT_ENTITY_USER_EXTENSION)
	// dynamically reallocate entity pool according to user-defined entity_t footprint
	if ((s_entity_size != _entity_size) || (!s_pentity_raw_pool))
	{
		if (s_pentity_raw_pool)
		{
			efree(s_pentity_raw_pool); 
			s_pentity_raw_pool = nullptr;
			s_entity_size = 0;
		}
		s_pentity_raw_pool = (char*)ealloc(_entity_size * c_max_entities);
		s_entity_size = _entity_size;
	}
#endif

	// initialze entity freelist
	{
		entitybase_t **freelist = entities_freelist;

		u32 prng = 19273;
		for (s16 i = 0; i < c_max_entities; i++)
		{
#if defined(AGT_ENTITY_USER_EXTENSION)
			entitybase_t *pcurr = (entitybase_t*)(&s_pentity_raw_pool[i * s_entity_size]);
#else
			entitybase_t *pcurr = &entities[i];
#endif
			pcurr->id = (s16)(prng >> 5);
			prng += 21533;
			prng *= 32971;
			prng ^= 95862;

			pcurr->rev_self = s_spawnrev-1;

#if defined(AGT_CONFIG_SAFETY_CHECKS)
			pcurr->pprev_tick = 0;
			pcurr->pnext_tick = 0;
#if defined(ENABLE_SPATIAL_X)
			pcurr->pprev_x = 0;
			pcurr->pnext_x = 0;
#endif
#if defined(ENABLE_SPATIAL_Y)
			pcurr->pprev_y = 0;
			pcurr->pnext_y = 0;
#endif
#endif
			*freelist++ = pcurr;
		}
	}

#if defined(AGT_CONFIG_ENTITY_HISTOGRAM)
	qmemclr(entity_histogram, sizeof(entity_histogram));
#endif

	entities_freelist_count = c_max_entities;
	s_entity_remove_count = 0;

	// list beforehead shelves priorities < 0
	s_entity_tick_beforehead.tick_priority = -256;
	s_entity_tick_beforehead.pprev_tick = NULL;
	s_entity_tick_beforehead.pnext_tick = &s_entity_tick_head;
	s_entity_tick_beforehead.fntick = &nulltick;

	// list head should always sort before real entities
	s_entity_tick_head.tick_priority = -1;
	s_entity_tick_head.pprev_tick = &s_entity_tick_beforehead;
	s_entity_tick_head.pnext_tick = &s_entity_tick_tail;
	s_entity_tick_head.fntick = &nulltick;
	
	// list tail should always sort after real entities
	s_entity_tick_tail.tick_priority = 0x7fff;
	s_entity_tick_tail.pprev_tick = &s_entity_tick_head;
	s_entity_tick_tail.pnext_tick = &s_entity_tick_aftertail;
	s_entity_tick_tail.fntick = &nulltick;

	// this sentinel forces a sort 'event', to allow us to escape processing without checking for the list end constantly
	s_entity_tick_aftertail.tick_priority = -1;
	s_entity_tick_aftertail.pnext_tick = NULL; //&s_entity_tick_sentinel;
	s_entity_tick_aftertail.pprev_tick = &s_entity_tick_tail;
	s_entity_tick_aftertail.fntick = &nulltick;

#if defined(ENABLE_SPATIAL_X)
	// list head should always sort before real entities
	s_entity_x_head.rx = 0x8000;
	s_entity_x_head.pprev_x = NULL;
	s_entity_x_head.pnext_x = &s_entity_x_tail;

	// list tail should always sort after real entities
	s_entity_x_tail.rx = 0x7fff;
	s_entity_x_tail.pprev_x = &s_entity_x_head;
	s_entity_x_tail.pnext_x = &s_entity_x_aftertail;

	// this sentinel forces a sort 'event', to allow us to escape processing
	s_entity_x_aftertail.rx = 0x8000;
	s_entity_x_aftertail.pnext_x = NULL; //&s_entity_tick_sentinel;
	s_entity_x_aftertail.pprev_x = &s_entity_x_tail;
#endif

#if defined(ENABLE_SPATIAL_Y)
	// list head should always sort before real entities
	s_entity_y_head.ry = 0x8000;
	s_entity_y_head.pprev_y = NULL;
	s_entity_y_head.pnext_y = &s_entity_y_tail;

	// list tail should always sort after real entities
	s_entity_y_tail.ry = 0x7fff;
	s_entity_y_tail.pprev_y = &s_entity_y_head;
	s_entity_y_tail.pnext_y = &s_entity_y_aftertail;

	// this sentinel forces a sort 'event', to allow us to escape processing
	s_entity_y_aftertail.ry = 0x8000;
	s_entity_y_aftertail.pnext_y = NULL; //&s_entity_tick_sentinel;
	s_entity_y_aftertail.pprev_y = &s_entity_y_tail;
#endif
}

// ---------------------------------------------------------------------------------------------------------------------
//	link entity into the list of instances to be ticked (i.e. those which can change state on each game cycle)

static inline void EntityLink_Tick_(entitybase_t *_pent, entitybase_t *_pnear)
{
	entitybase_t *pcurr = _pent;

	// find the insert position
	//entity_t *psort = _near ? _near : &s_entity_tick_tail;
	//while (psort->tick_priority > pcurr->tick_priority)
	//	psort = psort->pprev_tick;

	entitybase_t *psort = _pnear;
	if (!psort || !(psort->f_self & EntityFlag_TICKED))
	{
		psort = &s_entity_tick_tail;
		goto tsort_left;
	}
	else
	if (psort->tick_priority < pcurr->tick_priority)
	{
		while (psort->pnext_tick->tick_priority < pcurr->tick_priority)
			psort = psort->pnext_tick;
		goto tsort_done;
	}
tsort_left:
	while (psort->tick_priority > pcurr->tick_priority)
		psort = psort->pprev_tick;
tsort_done:


	// insert
	entitybase_t *psortnext = psort->pnext_tick;
	pcurr->pnext_tick = psortnext;
	pcurr->pprev_tick = psort;
	psortnext->pprev_tick = pcurr;
	psort->pnext_tick = pcurr;	
}

void EntityLink_Tick(entitybase_t *_pent, entitybase_t *_pnear)
{
	EntityLink_Tick_(_pent, _pnear);
}

// ---------------------------------------------------------------------------------------------------------------------
//	link entity into the 2D spatial world (i.e. those which have a position and may move or subject to proximity tests)

static inline void EntityLink2D_(entitybase_t *_pent, entitybase_t *_pnear)
{
	entitybase_t *pcurr = _pent;

#if defined(ENABLE_SPATIAL_X)
	{
		// find the insert position
		//entity_t *psort = _near ? _near : &s_entity_x_tail;
		//while (psort->rx > pcurr->rx)
		//	psort = psort->pprev_x;

		entitybase_t *psort = _pnear;
		if (!psort || !(psort->f_self & EntityFlag_SPATIAL))
		{
			psort = &s_entity_x_tail;
			goto xsort_left;
		}
		else
		if (psort->rx < pcurr->rx)
		{
			while (psort->pnext_x->rx < pcurr->rx)
				psort = psort->pnext_x;
			goto xsort_done;
		}
xsort_left:
		while (psort->rx > pcurr->rx)
			psort = psort->pprev_x;
xsort_done:

		// insert
		entitybase_t *psortnext = psort->pnext_x;
		pcurr->pnext_x = psortnext;
		pcurr->pprev_x = psort;
		psortnext->pprev_x = pcurr;
		psort->pnext_x = pcurr;	
	}
#endif

#if defined(ENABLE_SPATIAL_Y)
	{
		// find the insert position
//		entity_t *psort = _near ? _near : &s_entity_y_tail;
//		while (psort->ry > pcurr->ry)
//			psort = psort->pprev_y;

		entitybase_t *psort = _pnear;
		if (!psort || !(psort->f_self & EntityFlag_SPATIAL))
		{
			psort = &s_entity_y_tail;
			goto ysort_left;
		}
		else
		if (psort->ry < pcurr->ry)
		{
			while (psort->pnext_y->ry < pcurr->ry)
				psort = psort->pnext_y;
			goto ysort_done;
		}
ysort_left:
		while (psort->ry > pcurr->ry)
			psort = psort->pprev_y;
ysort_done:

		// insert
		entitybase_t *psortnext = psort->pnext_y;
		pcurr->pnext_y = psortnext;
		pcurr->pprev_y = psort;
		psortnext->pprev_y = pcurr;
		psort->pnext_y = pcurr;		
	}
#endif
}

void EntityLink2D(entitybase_t *_pent, entitybase_t *_pnear)
{
	EntityLink2D_(_pent, _pnear);
}

// ---------------------------------------------------------------------------------------------------------------------
//	link entity into all relevant lists

static void EntityLink_(entitybase_t *_pent, entitybase_t *_pnear)
{
	if (likely(_pent->f_self & EntityFlag_TICKED))
		EntityLink_Tick_(_pent, _pnear);

	if (likely(_pent->f_self & EntityFlag_SPATIAL))
		EntityLink2D_(_pent, _pnear);
}

void EntityLink(entitybase_t *_pent, entitybase_t *_pnear)
{
	EntityLink_(_pent, _pnear);
}

static void EntityDumpHistogram_()
{
#if defined(AGT_CONFIG_ENTITY_HISTOGRAM)
	static s16 s_duty = 0;
	if (--s_duty >= 0)
		return;
	s_duty = 32;

	g_lock_consolerefresh++;
	entprintf("histogram: %d\n", (c_max_entities-entities_freelist_count));
	for (s16 i = 0; i < s_max_entities; i++)
		entprintf("%d ", entity_histogram[i]);
	g_lock_consolerefresh--;
	entprintf("\n");
#endif
}

static agt_inline void EntityExecuteRemove_(entitybase_t *_pent)
{
	// unlink from all systems
	EntityUnLink(_pent);
	// notify any interested objects that entity identity has changed
	_pent->rev_self--; 
	// place it back in the free pool
	entities_freelist[entities_freelist_count++] = _pent;

#if defined(AGT_CONFIG_ENTITY_TRACKING)
	g_lock_consolerefresh++;
	entprintf("EntityRemove type(%d):\n", (int)_pent->type);
	g_lock_consolerefresh--;
#if defined(AGT_CONFIG_ENTITY_HISTOGRAM)
	entity_histogram[_pent->type]--;
	EntityDumpHistogram_();
#endif
#endif
}

void EntityExecuteAllRemoves()
{
	AUTOPRF(ent_links);

	if (s_entity_remove_count > 0)
	{
		entitybase_t **ppremoves = s_entity_removes;
		s16 p = s_entity_remove_count-1;
		do {
			EntityExecuteRemove_(*ppremoves++);
		} while (--p != -1);
	}

	s_entity_remove_count = 0;

#if defined(AGT_CONFIG_DEBUG_CONSOLE_)
	AGT_Sys_ConsoleRefresh();
#endif
}

void EntityExecuteAllLinks()
{
	AUTOPRF(ent_links);

	if (s_entity_link_count > 0)
	{
		entitybase_t **pplinks = s_entity_links;

		s16 c = s_entity_link_count>>1;
		s16 p = c-1;

		do {
			entitybase_t *pe = *pplinks++;
			entitybase_t *pp = *pplinks++;
			EntityLink_(pe, pp);
		} while (--p != -1);

		s_entity_link_count = 0;
	}
}

// ---------------------------------------------------------------------------------------------------------------------

#if (0)
static void EntitySpatial()
{
#if defined(ENABLE_SPATIAL_X)
	{
		//// first entity is pointed by list head
		//entity_t *pcurr = s_entity_x_head.pnext_x;

		//// one-time check for empty list (head->[nothing]->tail)
		//if (pcurr == &s_entity_x_tail)
		//	return;

		//// iterate until we hit the sentinel
		//s16 maxx = s_entity_x_tail.rx;
		//while (pcurr->rx < maxx)
		//{
		//	EntitySort2D(pcurr);

		//	pcurr = pcurr->pnext_x;
		//}

		entitybase_t *pnext, *pprev, *pwe;
		entitybase_t *pcurr = s_entity_x_head.pnext_x;

		while (1)
		{
nextent:
			if (pcurr->rx < pcurr->pprev_x->rx)
				goto pushback;
			pcurr = pcurr->pnext_x;
								
			goto nextent;		
			
pushback:
			// first chance to terminate
			if (pcurr == &s_entity_x_aftertail)
				return;
				
			// push it back to keep it sorted		
			pprev = pcurr->pprev_x;
			pnext = pcurr->pnext_x;

			// pull the edge out of the edge list
			pnext->pprev_x = pprev;
			pprev->pnext_x = pnext;

			// find out where the edge goes in the edge list
			pwe = pprev->pprev_x;

			while (pwe->rx > pcurr->rx)
				pwe = pwe->pprev_x;

			// put the edge back into the edge list
			pcurr->pnext_x = pwe->pnext_x;
			pcurr->pprev_x = pwe;
			pcurr->pnext_x->pprev_x = pcurr;
			pwe->pnext_x = pcurr;

			// advance
			pcurr = pnext;

			// last chance to terminate
			//if (pcurr == &s_entity_x_tail)
			//	return;
		}
	}
#endif

#if defined(ENABLE_SPATIAL_Y)
	{
		//// first entity is pointed by list head
		//entity_t *pcurr = s_entity_y_head.pnext_y;

		//// one-time check for empty list (head->[nothing]->tail)
		//if (pcurr == &s_entity_y_tail)
		//	return;

		//// iterate until we hit the sentinel
		//s16 maxy = s_entity_y_tail.ry;
		//while (pcurr->ry < maxy)
		//{
		//	EntitySort2D(pcurr);

		//	pcurr = pcurr->pnext_y;
		//}


		entitybase_t *pnext, *pprev, *pwe;
		entitybase_t *pcurr = s_entity_y_head.pnext_y;

		while (1)
		{
nextent:
			if (pcurr->ry < pcurr->pprev_y->ry)
				goto pushback;
			pcurr = pcurr->pnext_y;
								
			goto nextent;		
			
pushback:
			// first chance to terminate
			if (pcurr == &s_entity_y_aftertail)
				return;
				
			// push it back to keep it sorted		
			pprev = pcurr->pprev_y;
			pnext = pcurr->pnext_y;

			// pull the edge out of the edge list
			pnext->pprev_y = pprev;
			pprev->pnext_y = pnext;

			// find out where the edge goes in the edge list
			pwe = pprev->pprev_y;

			while (pwe->ry > pcurr->ry)
				pwe = pwe->pprev_y;

			// put the edge back into the edge list
			pcurr->pnext_y = pwe->pnext_y;
			pcurr->pprev_y = pwe;
			pcurr->pnext_y->pprev_y = pcurr;
			pwe->pnext_y = pcurr;

			// advance
			pcurr = pnext;

			// last chance to terminate
			//if (pcurr == &s_entity_y_tail)
			//	return;
		}
	}
#endif
}
#endif // 0

void EntityTickAll()
{
	AUTOPRF(ent_tick);

#if (0)
	// first entity is pointed by list head
	entitybase_t *pcurr = s_entity_tick_head.pnext_tick;

	// one-time check for empty list (head->[nothing]->tail)
	if (pcurr == &s_entity_tick_tail)
		goto complete;

	// iterate until we hit the sentinel
	while (1)
	{

#if defined(AGT_CONFIG_DYNAMIC_TICKPRIO)
next_entity:
#endif
		entitybase_t &curr = *pcurr;

		// perform the instance-specific tick
		// note: this is initially type-specific but instances can change their tick at runtime
		if (curr.fntick)
			curr.fntick(pcurr);

		// deal with any changes in tick/draw priority
		entitybase_t *pprev = curr.pprev_tick;
		if (curr.tick_priority < pprev->tick_priority)
		{
			// we arranged the termination test to occur in the [unlikely] codepath. amortize!
			if (pcurr == &s_entity_tick_aftertail)
				goto complete;

#if defined(AGT_CONFIG_DYNAMIC_TICKPRIO)
			entitybase_t *pnext = curr.pnext_tick;
			//entity_t *pprev = curr.pprev_tick;

			// unlink
			pnext->pprev_tick = pprev;
			pprev->pnext_tick = pnext;

			// find the re-insert position
			entitybase_t *psort = pcurr->pprev_tick->pprev_tick;
			s16 pos = curr.tick_priority;
			while (psort->tick_priority > pos)
				psort = psort->pprev_tick;

			// re-insert
			entitybase_t *psortnext = psort->pnext_tick;
			curr.pnext_tick = psortnext;
			curr.pprev_tick = psort;
			psortnext->pprev_tick = pcurr;
			psort->pnext_tick = pcurr;

			// the object's world sort position may be invalid so correct that here
			if (curr.f_self & EntityFlag_SPATIAL)
				EntitySort2D(pcurr);

			// next object
			pcurr = curr.pnext_tick;

			// opportunity to escape
			if (pcurr = &s_entity_tick_tail)
				goto complete;

			goto next_entity;
#endif
		}

		// deprecated - single optimised pass favoured over piecewise testing. majority of objects get ticked.
		// the object's world sort position may be invalid so correct that here
		//if (curr.f_self & EntityFlag_SPATIAL)
		//	EntitySort2D(pcurr);

		// the [likely] codepath just advances to the next entity ASAP (forever)
		pcurr = curr.pnext_tick;
	}

complete:
#endif // 0

	EntityTick_68k();

	// spatial coherence update pass
	EntitySpatial_68k();

	return;
}

// ---------------------------------------------------------------------------------------------------------------------
// draw all entities to workbuffer, using entity_t::drawtype

#if (0)
void EntityDrawVisibleXM(u16 *_pcurrent_workbuffer, s16 _snapx, s16 _snapy)
{

	AGT_BLiT_EMSprInit();

	s32 *plinetable = 0;

	// first entity is pointed by viewport left edge
	entitybase_t *pcurr = g_pe_viewport->pnext_x;

	s16 snapy = _snapy;

	// continue rendering on the major scrolling axis until we pass the viewport margin
	// note: needs conservative margin on the left or things disappear early. achieved by artificially moving the left edge of the viewport.
	// todo: assumes x-axis major for now. should be configurable.
	s16 pxmax = g_pe_viewport->rx + 320 + (32*2);

#if (1)
	while (likely(pcurr->rx < pxmax))
	{
		entitybase_t &e = *pcurr;

		// draw things using the appropriate thing drawer
		// note: we don't really want to use C-style callbacks as the overhead is terrible. 
		// needs to be turned into 68k anyway so keep it simple for now.
		if (e.drawtype == EntityDraw_SPRITE)
		{
			// todo: since we're using spatial coherence, we know when we don't need left-clipping on at least
			// the major scrolling axis. we could switch methods at this level to save on cliptests...
			AGT_BLiT_EMSprDrawClipped
			(
				/*sprite=*/(spritesheet::sps_header*)pcurr->passet, 
				/*framebuffer=*/_pcurrent_workbuffer,
				plinetable,
				/*anim_frame=*/e.frame,
				/*position=*/e.rx, e.ry - snapy
			);
		}

		pcurr = e.pnext_x;
	}
#endif




#if (0)

switch_sprite:


next_sprite:

	AGT_BLiT_EMSprInit();

	if (likely(pcurr->rx < pxmax))
	{

		// draw things using the appropriate thing drawer
		// note: we don't really want to use C-style callbacks as the overhead is terrible. 
		// needs to be turned into 68k anyway so keep it simple for now.
		if (pcurr->drawtype != EntityDraw_SPRITE)
			goto evaluate;

		{
#if (1)
			entitybase_t &e = *pcurr;

			// todo: since we're using spatial coherence, we know when we don't need left-clipping on at least
			// the major scrolling axis. we could switch methods at this level to save on cliptests...
			AGT_BLiT_EMSprDrawClipped
			(
				/*sprite=*/(spritesheet::sps_header*)e.passet, 
				/*framebuffer=*/_pcurrent_workbuffer,
				/*anim_frame=*/e.frame,
				/*position=*/e.rx, e.ry - snapy
			);
#endif
		}

		pcurr = pcurr->pnext_x;
		goto next_sprite;
	}

	goto done;

switch_slab:;

next_slab:
	if (likely(pcurr->rx < pxmax))
	{
		// draw things using the appropriate thing drawer
		// note: we don't really want to use C-style callbacks as the overhead is terrible. 
		// needs to be turned into 68k anyway so keep it simple for now.
		if (pcurr->drawtype != EntityDraw_SLAB)
			goto evaluate;

		{
#if (1)
			entitybase_t &e = *pcurr;

			AGT_BLiT_SlabDraw
//			STE_SlabNoClipGeneral4BPL
			(
				/*slab=*/(slabsheet::sls_header*)e.passet,
				/*framebuffer=*/_pcurrent_workbuffer,
				/*anim_frame=*/e.frame,
				/*position=*/e.rx, e.ry - snapy
			);
#endif
		}

		pcurr = pcurr->pnext_x;
		goto next_slab;
	}

	goto done;

evaluate:
	if (pcurr->drawtype == EntityDraw_SLAB)
		goto switch_slab;
	else
	if (pcurr->drawtype == EntityDraw_SPRITE)
		goto switch_sprite;

done:;

#endif


#if (0)
	do
	{

	while(likely((pcurr->rx < pxmax) && (pcurr->drawtype == EntityDraw_SPRITE)))
	{
		AGT_BLiT_EMSprInit();

		{
#if (1)
			entitybase_t &e = *pcurr;

			// todo: since we're using spatial coherence, we know when we don't need left-clipping on at least
			// the major scrolling axis. we could switch methods at this level to save on cliptests...
			AGT_BLiT_EMSprDrawClipped
			(
				/*sprite=*/(spritesheet::sps_header*)e.passet, 
				/*framebuffer=*/_pcurrent_workbuffer,
				/*anim_frame=*/e.frame,
				/*position=*/e.rx, e.ry - snapy
			);
#endif
		}

		pcurr = pcurr->pnext_x;
	}

	if (likely(pcurr->rx >= pxmax))
		return;

	while(likely((pcurr->rx < pxmax) && (pcurr->drawtype == EntityDraw_SLAB)))
	{
//		AGT_BLiT_EMSprInit();

		{
#if (1)
			entitybase_t &e = *pcurr;

			AGT_BLiT_SlabDraw
//			STE_SlabNoClipGeneral4BPL
			(
				/*slab=*/(slabsheet::sls_header*)e.passet,
				/*framebuffer=*/_pcurrent_workbuffer,
				/*anim_frame=*/e.frame,
				/*position=*/e.rx, e.ry - snapy
			);
#endif
		}

		pcurr = pcurr->pnext_x;
	}

	} while (likely(pcurr->rx < pxmax));
#endif
}
#endif // 0

// ---------------------------------------------------------------------------------------------------------------------
// process interactions

#if (0)
void EntityInteractVisibleXM()
{
	// first entity is pointed by viewport left edge
	entitybase_t *pcurr = g_pe_viewport->pnext_x;

	// todo: assumes x-axis major for now. should be configurable.
	s16 pxmax = g_pe_viewport->rx + 320 + (32*2);
	while (likely(pcurr->rx < pxmax))
	{
		entitybase_t &e = *pcurr;

		// if we encounter a vis-trigger entity, link it into the tickchain and change its tick mode
		// todo: could scan inwards from scroll leading edge?
		if (unlikely(e.f_self & EntityFlag_VISTRIG))
		{
			e.f_self = (e.f_self & ~EntityFlag_VISTRIG) | EntityFlag_TICKED;
			EntityLink_Tick(pcurr, g_pe_viewport->pnext_x);			
		}

		s16 hmax = e.rx + e.sx;
		s16 vmax = e.ry + e.sy;

		// process overlapping entities until we pass the right edge of the candidate
		entitybase_t *petest = e.pnext_x;
		while (petest->rx < hmax)
		{
			// process any likely interactions in the overlap rectangle
			if ((petest->ry < vmax) && ((petest->ry + petest->sy) > e.ry))
			{
				// interaction between these two objects is possible, but still
				// unclassified and unordered. we can meet a pair in any order and will only
				// meet the pair once per collision pass, so we process both interactions at once
				// this works out more efficient than processing each interaction individually

				// todo: use the sprite frame information to make this more accurate. no clean
				// way to do this yet but pulling context out of the sprite asset after tick
				// using an adaptor seems like a good idea.

				if (unlikely((e.f_self | petest->f_self) & EntityFlags_KILL))
				{
					// don't interact recently-killed objects
					// todo: turn this into a debug error - it indicates a problem with interaction flags vs missing collide functions
				}
				else
				{
					// process self:other interaction
					if (e.fncollide && (e.f_self & petest->f_interactswith))
						e.fncollide(pcurr, petest);

					// process reciprocal interaction
					if (petest->fncollide && (petest->f_self & e.f_interactswith))
						petest->fncollide(petest, pcurr);
				}
			}

			petest = petest->pnext_x;
		}

		pcurr = e.pnext_x;
	}
}
#endif // 0

// ---------------------------------------------------------------------------------------------------------------------
// add new entity to world, based on a known type, without any context except coordinates
// note: caller can still modify the entity state once created

entity_t * EntitySpawn(EntityType _etype, s16 px, s16 py)
{
	entitybase_t *palloc = 0;

	// can only alloc if we have some spare
	if (likely(entities_freelist_count > 0))
	{
		// allocate object
		palloc = entities_freelist[--entities_freelist_count];

		// initialize from static definition
		// todo: can reduce this by having dynamic instance point at static fields in the dictionary, but
		// then we have to indirect to reach the definition every time in every AI so it's not worth
		// the trouble unless the cost of copying the full definition is quite big. doing so would also
		// remove the flexibility of being able to change those fields for a single instance without upsetting
		// the other instances.
		entitydef_t *pdef = &entity_dictionary[_etype];
		palloc->type = _etype;
		palloc->rev_self = s_spawnrev++;
//		qmemcpy(&(palloc->fntick), pdef, sizeof(entitydef_t));

		//__asm __volatile__
		//(
		//".x%=:;"
		//	"bra.s .x%=;"
		//	:
		//	:
		//	:
		//);

//		memcpy(&(palloc->fntick), pdef, sizeof(entitydef_t));
		copy<sizeof(entitydef_t)>(&(palloc->fntick), pdef);

		// assign coordinates
		palloc->rx = px;
		palloc->ry = py;

		// link entity into world, without context
		//EntityLink(palloc, NULL);
		EntityScheduleLink(palloc, NULL);

#if defined(AGT_CONFIG_ENTITY_TRACKING)
		g_lock_consolerefresh++;
		entprintf("EntitySpawn type(%d):\n", (int)_etype);
		g_lock_consolerefresh--;
#if defined(AGT_CONFIG_ENTITY_HISTOGRAM)
		entity_histogram[_etype]++;
		EntityDumpHistogram_();
#endif
#endif	
	}

	return reinterpret_cast<entity_t*>(palloc);
}

// ---------------------------------------------------------------------------------------------------------------------
// add new entity to world, based on a known type, based on parent context (points at parent, steals parent coords)
// note: caller can still modify the entity state once created

entity_t * EntitySpawnFrom(EntityType _etype, entbaseptr_t _pparent)
{
	entitybase_t *palloc = 0;

	// can only alloc if we have some spare
	if (likely(entities_freelist_count > 0))
	{
#if defined(AGT_CONFIG_SAFETY_CHECKS)
		if (_pparent->f_self & EntityFlags_KILL)
			wprintf("warning: EntitySpawnFrom creates type(%d) from killed type(%d)\n", (int)_etype, (int)_pparent->type);
#endif

		// allocate object
		palloc = entities_freelist[--entities_freelist_count];

		// initialize from static definition
		// todo: can reduce this by having dynamic instance point at static fields in the dictionary, but
		// then we have to indirect to reach the definition every time in every AI so it's not worth
		// the trouble unless the cost of copying the full definition is quite big. doing so would also
		// remove the flexibility of being able to change those fields for a single instance without upsetting
		// the other instances.
		entitydef_t *pdef = &entity_dictionary[_etype];
		palloc->type = _etype;
		palloc->rev_self = s_spawnrev++;
		palloc->rev_parent = _pparent->rev_self;
//		qmemcpy(&(palloc->fntick), pdef, sizeof(entitydef_t));
		copy<sizeof(entitydef_t)>(&(palloc->fntick), pdef);

		// link to parent
		(entitybase_t*&)(palloc->pparent) = _pparent;
		// steal coordinates
		palloc->rx = _pparent->rx + _pparent->ox - palloc->ox;
		palloc->ry = _pparent->ry + _pparent->oy - palloc->oy;

		// link entity into world near its parent (much cheaper than a full search-insert on the world)
//		EntityLink(palloc, _pparent);
		EntityScheduleLink(palloc, _pparent);

#if defined(AGT_CONFIG_ENTITY_TRACKING)
		g_lock_consolerefresh++;
		entprintf("EntitySpawnFrom type(%d->%d):\n", (int)_pparent->type, (int)_etype);
		g_lock_consolerefresh--;
#if defined(AGT_CONFIG_ENTITY_HISTOGRAM)
		entity_histogram[_etype]++;
		EntityDumpHistogram_();
#endif
#endif	
	}

	return reinterpret_cast<entity_t*>(palloc);
}

entity_t * EntitySpawnRel(EntityType _etype, entbaseptr_t _pparent, s16 _ox, s16 _oy)
{
	entitybase_t *palloc = 0;

	// can only alloc if we have some spare
	if (likely(entities_freelist_count > 0))
	{
#if defined(AGT_CONFIG_SAFETY_CHECKS)
		if (_pparent->f_self & EntityFlags_KILL)
			wprintf("warning: EntitySpawnRel creates type(%d) from killed type(%d)\n", (int)_etype, (int)_pparent->type);
#endif

		// allocate object
		palloc = entities_freelist[--entities_freelist_count];

		// initialize from static definition
		// todo: can reduce this by having dynamic instance point at static fields in the dictionary, but
		// then we have to indirect to reach the definition every time in every AI so it's not worth
		// the trouble unless the cost of copying the full definition is quite big. doing so would also
		// remove the flexibility of being able to change those fields for a single instance without upsetting
		// the other instances.
		entitydef_t *pdef = &entity_dictionary[_etype];
		palloc->type = _etype;
		palloc->rev_self = s_spawnrev++;
		palloc->rev_parent = _pparent->rev_self;

		//__asm __volatile__
		//(
		//".x%=:;"
		//	"bra.s .x%=;"
		//	:
		//	:
		//	:
		//);

//		memcpy(&(palloc->fntick), pdef, sizeof(entitydef_t));
		copy<sizeof(entitydef_t)>(&(palloc->fntick), pdef);

//		qmemcpy(&(palloc->fntick), pdef, sizeof(entitydef_t));

		// link to parent
		palloc->pparent = (entity_t*)_pparent;
		// assign coords
		palloc->rx = _pparent->rx + _pparent->ox - palloc->ox + _ox;
		palloc->ry = _pparent->ry + _pparent->oy - palloc->oy + _oy;

		// link entity into world near its parent (much cheaper than a full search-insert on the world)
//		EntityLink(palloc, _pparent);
		EntityScheduleLink(palloc, _pparent);

#if defined(AGT_CONFIG_ENTITY_TRACKING)
		g_lock_consolerefresh++;
		entprintf("EntitySpawnRel type(%d->%d):\n", (int)_pparent->type, (int)_etype);
		g_lock_consolerefresh--;
#if defined(AGT_CONFIG_ENTITY_HISTOGRAM)
		entity_histogram[_etype]++;
		EntityDumpHistogram_();
#endif
#endif	
	}

	return reinterpret_cast<entity_t*>(palloc);
}

entity_t * EntitySpawnNear(EntityType _etype, entbaseptr_t _pparent, s16 _ox, s16 _oy)
{
	entitybase_t *palloc = 0;

	// can only alloc if we have some spare
	if (likely(entities_freelist_count > 0))
	{
#if defined(AGT_CONFIG_SAFETY_CHECKS)
		if (_pparent->f_self & EntityFlags_KILL)
			wprintf("warning: EntitySpawnNear creates type(%d) from killed type(%d)\n", (int)_etype, (int)_pparent->type);
#endif

		// allocate object
		palloc = entities_freelist[--entities_freelist_count];

		// initialize from static definition
		// todo: can reduce this by having dynamic instance point at static fields in the dictionary, but
		// then we have to indirect to reach the definition every time in every AI so it's not worth
		// the trouble unless the cost of copying the full definition is quite big. doing so would also
		// remove the flexibility of being able to change those fields for a single instance without upsetting
		// the other instances.
		entitydef_t *pdef = &entity_dictionary[_etype];
		palloc->type = _etype;
		palloc->rev_self = s_spawnrev++;
		palloc->rev_parent = _pparent->rev_self;

//		qmemcpy(&(palloc->fntick), pdef, sizeof(entitydef_t));
		copy<sizeof(entitydef_t)>(&(palloc->fntick), pdef);

		// link to parent
		palloc->pparent = (entity_t*)_pparent;
		// assign coords
		palloc->rx =  _ox - palloc->ox;
		palloc->ry =  _oy - palloc->oy;

		// link entity into world near its parent (much cheaper than a full search-insert on the world)
//		EntityLink(palloc, _pparent);
		EntityScheduleLink(palloc, _pparent);

#if defined(AGT_CONFIG_ENTITY_TRACKING)
		g_lock_consolerefresh++;
		entprintf("EntitySpawnNear type(%d->%d):\n", (int)_pparent->type, (int)_etype);
		g_lock_consolerefresh--;
#if defined(AGT_CONFIG_ENTITY_HISTOGRAM)
		entity_histogram[_etype]++;
		EntityDumpHistogram_();
#endif
#endif	
	}

	return reinterpret_cast<entity_t*>(palloc);
}

entity_t * EntitySpawnImmediate(EntityType _etype, entbaseptr_t _pparent, s16 _ox, s16 _oy)
{
	entitybase_t *palloc = 0;

	// can only alloc if we have some spare
	if (likely(entities_freelist_count > 0))
	{
#if defined(AGT_CONFIG_SAFETY_CHECKS)
		if (_pparent->f_self & EntityFlags_KILL)
			wprintf("warning: EntitySpawnImmediate creates type(%d) from killed type(%d)\n", (int)_etype, (int)_pparent->type);
#endif

		// allocate object
		palloc = entities_freelist[--entities_freelist_count];

		// initialize from static definition
		// todo: can reduce this by having dynamic instance point at static fields in the dictionary, but
		// then we have to indirect to reach the definition every time in every AI so it's not worth
		// the trouble unless the cost of copying the full definition is quite big. doing so would also
		// remove the flexibility of being able to change those fields for a single instance without upsetting
		// the other instances.
		entitydef_t *pdef = &entity_dictionary[_etype];
		palloc->type = _etype;
		palloc->rev_self = s_spawnrev++;
		palloc->rev_parent = _pparent->rev_self;
//		qmemcpy(&(palloc->fntick), pdef, sizeof(entitydef_t));
		copy<sizeof(entitydef_t)>(&(palloc->fntick), pdef);

		// link to parent
		palloc->pparent = (entity_t*)_pparent;
		// assign coords
		palloc->rx = _pparent->rx + _pparent->ox - palloc->ox + _ox;
		palloc->ry = _pparent->ry + _pparent->oy - palloc->oy + _oy;

		// link entity into world near its parent (much cheaper than a full search-insert on the world)
		EntityLink_(palloc, _pparent);

#if defined(AGT_CONFIG_ENTITY_TRACKING)
		g_lock_consolerefresh++;
		entprintf("EntitySpawnImmediate type(%d->%d):\n", (int)_pparent->type, (int)_etype);
		g_lock_consolerefresh--;
#if defined(AGT_CONFIG_ENTITY_HISTOGRAM)
		entity_histogram[_etype]++;
		EntityDumpHistogram_();
#endif
#endif	
	}

	return reinterpret_cast<entity_t*>(palloc);
}

// ---------------------------------------------------------------------------------------------------------------------
// create entity, based on a known type, without context and without linking the entity into services
// important: to spawn the object, caller must set coordinates and manually link entity afterwards!
// e.g.
//	pentity->rx = x;
//	pentity->ry = y;
//	EntityLink(pentity, pnear/NULL);

entity_t * EntityCreateUnlinked(EntityType _etype)
{
	entitybase_t *palloc = 0;

	// can only alloc if we have some spare
	if (likely(entities_freelist_count > 0))
	{
		// allocate object
		palloc = entities_freelist[--entities_freelist_count];

		// initialize from static definition
		// todo: can reduce this by having dynamic instance point at static fields in the dictionary, but
		// then we have to indirect to reach the definition every time in every AI so it's not worth
		// the trouble unless the cost of copying the full definition is quite big. doing so would also
		// remove the flexibility of being able to change those fields for a single instance without upsetting
		// the other instances.
		entitydef_t *pdef = &entity_dictionary[_etype];
		palloc->type = _etype;
		palloc->rev_self = s_spawnrev++;
//		qmemcpy(&(palloc->fntick), pdef, sizeof(entitydef_t));
		copy<sizeof(entitydef_t)>(&(palloc->fntick), pdef);
	}

	return reinterpret_cast<entity_t*>(palloc);
}

// ---------------------------------------------------------------------------------------------------------------------
//	associate [sprite] asset with entity, extract dimensions etc.

void EntityDefAsset_Sprite(EntityType _etype, spritesheet *_passet)
{
	spritesheet::sps_header *pspr = _passet->get();
	entity_dictionary[_etype].passet = pspr;
	entity_dictionary[_etype].sx = pspr->srcwidth;
	entity_dictionary[_etype].sy = pspr->srcheight;
	// todo: origin should also be stored/retrieved via spritesheet
	entity_dictionary[_etype].ox = pspr->srcwidth>>1;
	entity_dictionary[_etype].oy = pspr->srcheight>>1;
}

void EntityDefAsset_Hidden(EntityType _etype, spritesheet *_passet)
{
	spritesheet::sps_header *pspr = _passet->get();
	entity_dictionary[_etype].phiddenasset = pspr;
}

void EntityDefAsset_Slab(EntityType _etype, sls_pair *_passet)
{
	slabsheet::sls_header *pslb = _passet->psls_draw_;
	entity_dictionary[_etype].passet = _passet;
	entity_dictionary[_etype].sx = pslb->srcwidth;
	entity_dictionary[_etype].sy = pslb->srcheight;
	// todo: origin should also be stored/retrieved via spritesheet
	entity_dictionary[_etype].ox = pslb->srcwidth>>1;
	entity_dictionary[_etype].oy = pslb->srcheight>>1;
}

void EntityDefAsset_SpriteProxy(EntityType _etype, sps_pair *_passet)
{
	spritesheet::sps_header *pspr = _passet->psps_draw_;
	entity_dictionary[_etype].passet = _passet;
	entity_dictionary[_etype].sx = pspr->srcwidth;
	entity_dictionary[_etype].sy = pspr->srcheight;
	// todo: origin should also be stored/retrieved via spritesheet
	entity_dictionary[_etype].ox = pspr->srcwidth>>1;
	entity_dictionary[_etype].oy = pspr->srcheight>>1;
}


// ---------------------------------------------------------------------------------------------------------------------

#if defined(AGT_CONFIG_SAFETY_CHECKS)

extern "C" void AGT_Exception_IMSFrame(s16 _frame, spritesheet::sps_header *h)
{
	entitybase_t * pcurr = (entitybase_t*)(reg32(A8));

	eprintf("error: IMS frame fault\n")
	eprintf("ent:%d type:%d frame:%d >= fmax:%d\n", (unsigned int)pcurr->id, (int)pcurr->type, (int)_frame, (int)h->framecount);
	xassert(false);
	while(1) { }
}

extern "C" void AGT_Exception_EMSFrame(s16 _frame, spritesheet::sps_header *h)
{
	entitybase_t * pcurr = (entitybase_t*)(reg32(A8));

	eprintf("error: EMS frame fault\n")
	eprintf("ent:%d type:%d frame:%d >= fmax:%d\n", (unsigned int)pcurr->id, (int)pcurr->type, (int)_frame, (int)h->framecount);
	xassert(false);
	while(1) { }
}

extern "C" void AGT_Exception_EMHFrame(s16 _frame, spritesheet::sps_header *h)
{
	entitybase_t * pcurr = (entitybase_t*)(reg32(A8));

	eprintf("error: EMH frame fault\n")
	eprintf("ent:%d type:%d frame:%d >= fmax:%d\n", (unsigned int)pcurr->id, (int)pcurr->type, (int)_frame, (int)h->framecount);
	xassert(false);
	while(1) { }
}

extern "C" void AGT_Exception_EMXFrame(s16 _frame, spritesheet::sps_header *h)
{
	entitybase_t * pcurr = (entitybase_t*)(reg32(A8));

	eprintf("error: EMX frame fault\n")
	eprintf("ent:%d type:%d frame:%d >= fmax:%d\n", (unsigned int)pcurr->id, (int)pcurr->type, (int)_frame, (int)h->framecount);
	xassert(false);
	while(1) { }
}

extern "C" void AGT_Exception_SLSFrame(s16 _frame, slabsheet::sls_header *h)
{
	entitybase_t * pcurr = (entitybase_t*)(reg32(A8));

	eprintf("error: Slab (SLS) frame fault\n")
	eprintf("ent:%d type:%d frame:%d >= fmax:%d\n", (unsigned int)pcurr->id, (int)pcurr->type, (int)_frame, (int)h->framecount);
	xassert(false);
	while(1) { }
}

extern "C" void AGT_Exception_SLRFrame(s16 _frame, slabsheet::sls_header *h)
{
	entitybase_t * pcurr = (entitybase_t*)(reg32(A8));

	eprintf("error: SlabRestore (SLR) frame fault\n")
	eprintf("ent:%d type:%d frame:%d >= fmax:%d\n", (unsigned int)pcurr->id, (int)pcurr->type, (int)_frame, (int)h->framecount);
	xassert(false);
	while(1) { }
}

#endif // defined(AGT_CONFIG_SAFETY_CHECKS)

// ---------------------------------------------------------------------------------------------------------------------
