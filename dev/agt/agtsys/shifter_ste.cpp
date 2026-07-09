//======================================================================================================================
// 	[A]tari [G]ame [T]ools / dml 2016
//======================================================================================================================
//	Shifter display interface: STE specific
//----------------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------------
//	system headers

// ---------------------------------------------------------------------------------------------------------------------
//	Our AGT headers


#include "common_cpp.h"

// ---------------------------------------------------------------------------------------------------------------------

#include "shifter.h"

// ---------------------------------------------------------------------------------------------------------------------

extern "C" void STE_ConfigureDisplayService();
extern "C" void STE_InstallDisplayService(int _mode);
extern "C" void STE_RemoveDisplayService();


// ---------------------------------------------------------------------------------------------------------------------

void shifter_ste::configure()
{
	scrollmask_ = 0xf;

	STE_ConfigureDisplayService();
}

void shifter_ste::init(STEDisplayMode _mode)
{	
	init_common();

	current_mode_ = _mode;

	// some built-in modes now implemented via Nickel
	switch (_mode)
	{
	// specific display configs
	case STLow200:
	case STLow272:
		break;

	// user-defined display config
	case Nickel:
		break;

	// built-in configs via Nickel
	case STLow240:
		{
			static nickel_op_t nickel_commands[] =
			{
				{ NE_PortSelect,	0+16,	NickelPort0	},
				{ NE_Stop,			240+16	}
			};

			load_nickel(nickel_commands);
		}
		break;
	case STLow256:
		{
			static nickel_op_t nickel_commands[] =
			{
				{ NE_PortSelect,	0+8,	NickelPort0	},
				{ NE_Stop,			256+8	}
			};

			load_nickel(nickel_commands);
		}
		break;
	}

#if !defined(AGT_DISABLE_DISPLAYSERVICE)
	STE_InstallDisplayService((int)_mode);
#endif
}

void shifter_ste::restore()
{
	restore_common();

#if !defined(AGT_DISABLE_DISPLAYSERVICE)
	STE_RemoveDisplayService();
#endif
}

// ---------------------------------------------------------------------------------------------------------------------

void shifter_ste::setcolour(s16 _buffer, s16 _field, s16 _ci, u16 _c, s16 _view)
{
	volatile shiftgroup& g = *(g_pgroups[_view][_buffer]);//g_groups[_buffer];
	volatile shiftfield& s = *(g.pshiftfields_[_field]);//g.shiftfields_[_field];	

	s.palette_[_ci] = _c;	
}

void shifter_ste::setpalette(s16 _buffer, s16 _field, u16* _colours, s16 _view)
{
	volatile shiftgroup& g = *(g_pgroups[_view][_buffer]);//g_groups[_buffer];
	volatile shiftfield& s = *(g.pshiftfields_[_field]);//g.shiftfields_[_field];	

	for (s16 c = 0; c < 16; ++c)
	{
		u16 col = _colours[c];	

		s.palette_[c] = col;	
	}
}

typedef struct nickel_ins_s
{
	void * psyncsmc;
	u8 sync8;
	u8 time8;
	void * prout;
} nickel_ins_t;

extern "C" void *g_a_nickel_port_cfg[2];
extern "C" void *g_a_nickel_border_cfg[2];
extern "C" void *g_a_nickel_stop_cfg[2];
extern "C" void *g_a_nickel_blank_cfg[2];
extern "C" void *g_a_nickel_end_cfg[2];

extern "C" void *g_a_nickel_codegen_area[2];
extern "C" nickel_ins_t *g_a_nickel_eventbase[2];
extern "C" void *g_a_displayport_maps[2];

extern "C" int *g_p_nickel_block_templates;

static int s_dummy_smc = 0;

//static const int timecritical_pad_tb = 3-1;

static s16 s_nickel_codegen_wordindex = 0;

static inline s16 nickel_get_ringbuffer_pos()
{
	s16 codegen_wordindex = s_nickel_codegen_wordindex;
	// ringbuffer!
	if (codegen_wordindex >= (2048-128))
		codegen_wordindex = 0;
	return codegen_wordindex;
}

static inline void nickel_set_ringbuffer_pos(s16 codegen_wordindex)
{
	s_nickel_codegen_wordindex = codegen_wordindex;
}

extern "C" volatile s16 g_nickel_read_idx;
extern "C" s16 g_nickel_write_idx;

void shifter_ste::load_nickel(nickel_op_t *_pevents, int _timing_resilience)
{
	s16 slot;
	while ((slot = ((g_nickel_write_idx + 1) & 1)) == g_nickel_read_idx)
	{
	}

	s16 timecritical_pad_tb = _timing_resilience;

	nickel_ins_t * pinstruction = g_a_nickel_eventbase[slot];
	u16* pcodegenbase = (u16*)((int)g_a_nickel_codegen_area[slot] & -256);
	s16* pportmap = (s16*)(g_a_displayport_maps[slot]);
	s16 portidx = 0;

	nickel_set_ringbuffer_pos(0);

	u8 last_line = 0;
	s16 border_opened = 0;

	//for (int e = 0; e < _nevents; e++)
	bool bcontinue = true;
	for (int e = 0; bcontinue ; e++)
	{
		nickel_op_t &revent = _pevents[e];

		// events crossing low border must insert border event first
		if ((revent.line > 229) && (border_opened == 0))
		{
			u8 absline = 229;

			u8 rel_line = absline - last_line;
			last_line = absline+1;
			pinstruction->sync8 = rel_line - (timecritical_pad_tb<<1);
			pinstruction->time8 = rel_line - (timecritical_pad_tb<<0);
			pinstruction->psyncsmc = g_a_nickel_border_cfg[0];
			pinstruction->prout = g_a_nickel_border_cfg[1];
			pinstruction++;
			//
			border_opened = 1;
		}

		switch (revent.type)
		{
		case NE_PortSelect:
			{
				// don't emit scanline event if too close to line 0
				// VBL sets up port 0 by default
				if (revent.line >= 4)
				{
					u8 rel_line = revent.line - last_line;
					last_line = revent.line;
					pinstruction->sync8 = rel_line - (timecritical_pad_tb<<1);
					pinstruction->time8 = rel_line - (timecritical_pad_tb<<0);
					pinstruction->psyncsmc = g_a_nickel_port_cfg[0];
					pinstruction->prout = g_a_nickel_port_cfg[1];
					pinstruction++;
				}
				// map port
				pportmap[portidx++] = revent.index<<2;
			}
			break;
		case NE_ColourBurst:
			{
				u8 rel_line = revent.line - last_line;
				last_line = revent.line;
				pinstruction->sync8 = rel_line - (timecritical_pad_tb<<1);
				pinstruction->time8 = rel_line - (timecritical_pad_tb<<0);
				pinstruction->psyncsmc = &s_dummy_smc;

				{
					s16 codegen_wordindex = nickel_get_ringbuffer_pos();

					// destination codegen address
					//u16* pcodegenbase = (u16*)((int)g_p_nickel_codegen_area & -256);
					u16 *pdst = pcodegenbase;//&pcodegenbase[codegen_wordindex];
					pinstruction->prout = &pdst[codegen_wordindex];

					u16 *pvalues = (u16*)(revent.pvalues);
					int *ptemplates = g_p_nickel_block_templates;

/*
00001000  7055                      13      moveq   #$55,d0
00001002  41F8 8240                 14      lea     $ffff8240,a0
00001006                            15      
00001006  4E71                      16      nop
00001008                            17      
00001008  30FC AAAA                 18      move.w  #$aaaa,(a0)+
0000100C                            19      
0000100C  20FC AAAABBBB             20      move.l  #$aaaabbbb,(a0)+
00001012                            21      
00001012  5A88                      22      addq.l  #5,a0
00001014                            23      
00001014  41E8 0600                 24      lea     $600(a0),a0
*/

					// block 0
					u16 *psrc = (u16*)*(ptemplates++);
					int size = *ptemplates++;
					qmemcpy(&pdst[codegen_wordindex], psrc, size);
					codegen_wordindex += size>>1;

					// pre-sync
					pdst[codegen_wordindex++] = 0x7000 | (rel_line - (timecritical_pad_tb<<1));
					pdst[codegen_wordindex++] = 0x41f8;
					pdst[codegen_wordindex++] = 0x8240 + (revent.index<<1);
					
					//  block 1
					psrc = (u16*)*(ptemplates++);
					size = *ptemplates++;
					qmemcpy(&pdst[codegen_wordindex], psrc, size);
					codegen_wordindex += size>>1;

					// nop
					pdst[codegen_wordindex++] = 0x4e71;
					pdst[codegen_wordindex++] = 0x4e71;

					// post-sync
					int pairs = revent.count >> 1;
					s16 v = 0;
					for (int c = 0; c < pairs; c++)
					{
						pdst[codegen_wordindex++] = 0x20fc;
						pdst[codegen_wordindex++] = pvalues[v++];
						pdst[codegen_wordindex++] = pvalues[v++];
					}
					if (revent.count & 1)
					{
						pdst[codegen_wordindex++] = 0x30fc;
						pdst[codegen_wordindex++] = pvalues[v++];
					}

					//  block 2
					psrc = (u16*)*(ptemplates++);
					size = *ptemplates++;
					qmemcpy(&pdst[codegen_wordindex], psrc, size);
					codegen_wordindex += size>>1;

					nickel_set_ringbuffer_pos(codegen_wordindex);
				}
				pinstruction++;
			}
			break;
		case NE_RegisterLoad:
			{
				u8 rel_line = revent.line - last_line;
				last_line = revent.line;
				pinstruction->sync8 = rel_line - (timecritical_pad_tb<<1);
				pinstruction->time8 = rel_line - (timecritical_pad_tb<<0);
				pinstruction->psyncsmc = &s_dummy_smc;

				{
					s16 codegen_wordindex = nickel_get_ringbuffer_pos();

					// destination codegen address
					//u16* pcodegenbase = (u16*)((int)g_p_nickel_codegen_area & -256);
					u16 *pdst = pcodegenbase;//&pcodegenbase[codegen_wordindex];
					pinstruction->prout = &pdst[codegen_wordindex];
					
					//pdst[codegen_wordindex++] = 0x60fe;

					u16 *pvalues = (u16*)(revent.pvalues);
					int *ptemplates = g_p_nickel_block_templates;

/*
00001000  21FC 00007777 8240        12      move.l  #$7777,$ffff8240.w

00001008  31FC 7777 8240            13      move.w  #$7777,$ffff8240.w

0000100E  11FC 0077 8240            14      move.b  #$77,$ffff8240.w
*/
					// block 0
					u16 *psrc = (u16*)*(ptemplates++);
					int size = *ptemplates++;
					qmemcpy(&pdst[codegen_wordindex], psrc, size);
					codegen_wordindex += size>>1;

					// pre-sync
					pdst[codegen_wordindex++] = 0x7000 | (rel_line - (timecritical_pad_tb<<1));
					
					//  block 1
					psrc = (u16*)*(ptemplates++);
					size = *ptemplates++;
					qmemcpy(&pdst[codegen_wordindex], psrc, size);
					codegen_wordindex += size>>1;

					// post-sync
					s16 v = 0;
					for (int c = 0; c < revent.count; c++)
					{
						u16 mode = pvalues[v++];	// 1,2,4
						if (mode == 1)
						{
							pdst[codegen_wordindex++] = 0x11fc;
							// value
							pdst[codegen_wordindex++] = pvalues[v++];
						}
						else
						if (mode == 2)
						{
							pdst[codegen_wordindex++] = 0x31fc;
							// value
							pdst[codegen_wordindex++] = pvalues[v++];
						}
						else
						if (mode == 4)
						{
							pdst[codegen_wordindex++] = 0x21fc;
							// value
							pdst[codegen_wordindex++] = pvalues[v++];
							pdst[codegen_wordindex++] = pvalues[v++];
						}
						// register
						pdst[codegen_wordindex++] = pvalues[v++];
					}

					//  block 2
					psrc = (u16*)*(ptemplates++);
					size = *ptemplates++;
					qmemcpy(&pdst[codegen_wordindex], psrc, size);
					codegen_wordindex += size>>1;

					nickel_set_ringbuffer_pos(codegen_wordindex);
				}
				pinstruction++;
			}
			break;
		case NE_Blank:
			{
				u8 rel_line = revent.line - last_line;
				last_line = revent.line;
				pinstruction->sync8 = rel_line - (timecritical_pad_tb<<1);
				pinstruction->time8 = rel_line - (timecritical_pad_tb<<0);
				pinstruction->psyncsmc = g_a_nickel_blank_cfg[0];
				pinstruction->prout = g_a_nickel_blank_cfg[1];
				pinstruction++;
			}
			break;
		case NE_Stop:
			{
				u8 rel_line = revent.line - last_line;
				last_line = revent.line;
				pinstruction->sync8 = rel_line - (timecritical_pad_tb<<1);
				pinstruction->time8 = rel_line - (timecritical_pad_tb<<0);
				pinstruction->psyncsmc = g_a_nickel_stop_cfg[0];
				pinstruction->prout = g_a_nickel_stop_cfg[1];
				pinstruction++;
			}
			bcontinue = false;
			break;
		case NE_End:
			{
				u8 rel_line = revent.line - last_line;
				last_line = revent.line;
				pinstruction->sync8 = rel_line - (timecritical_pad_tb<<1);
				pinstruction->time8 = rel_line - (timecritical_pad_tb<<0);
				pinstruction->psyncsmc = g_a_nickel_end_cfg[0];
				pinstruction->prout = g_a_nickel_end_cfg[1];
				pinstruction++;
			}
			bcontinue = false;
			break;
		default:
			{
			}
			break;
		};
	}

	// terminate displayport map
	pportmap[portidx++] = -1;

	// commit
	g_nickel_write_idx = slot;
}

// ---------------------------------------------------------------------------------------------------------------------
