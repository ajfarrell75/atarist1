//---------------------------------------------------------------------------------------------------------------------
//	[A]tari [G]ame [T]ools / dml 2016
//---------------------------------------------------------------------------------------------------------------------
//	EMX/EMH blitter sprite generator
//	Notes:
//	- EMX sprites uses code generation for each preshift (0-15) of a sprite frame, where a preshift consists of
//	  delta-compressed blitter endmask updates using the minimum number of data loads & stores
//	- EMH sprites also use code generation for preshifts but 1 program is shared between all preshifts for a given
//	  animation frame. endmask data is stored unique per preshift but is a sparse set compared with EMS.
//	- EMX sprites can optionally embed the sprite-restore records needed to erase sprites after each update
//	- requires RMAC assembler V1.4.12 (or better) to post-process the frames
//---------------------------------------------------------------------------------------------------------------------

#define DEBUG_REGISTER_TAGS (0)
#define DEBUG_REGISTER_TAGS2 (0)

#define DEBUG_REGISTER_STATES (0)
#define SIM_ALL_STAGES (0) // 1= don't just simulate final generated code. simulate after every pass, checking for errors (slow!)
#define DEBUG_FRAME (-1)
#define DEBUG_PRESHIFT (-1)

// 20 tiles / 320 pix
// +1 for hscroll margin
// +2 for speculative update margin

#define DISPLAY_LINEWIDTH (8*(20+1))	// +1 for finescroll

enum RegClass
{
	RegClass_Data,
	RegClass_Address
};

class dataregister
{
public:

	dataregister
	(
		const char* _name, 
		int _index, 
		RegClass _regclass, 
		bool _alloc, 
		bool _constant, 
		bool _locked,
		bool _external,
		ulong_t _init
	)
		: name(_name),
		  index(_index),
		  regclass(_regclass),
		  allocatedH(_alloc),
		  allocatedL(_alloc),
		  constant(_constant),
		  locked(_locked),
		  external(_external),
		  variable(false),
		  H(_init >> 16),
		  L(_init & 0xFFFF),
		  stableH(_alloc),
		  stableL(_alloc),
		  sspos(0)
	{
	}

	typedef struct state_s
	{
		bool stableH;
		bool stableL;
		uword_t H;
		uword_t L;

	} state_t;


	state_t statestack[4];
	int sspos;

	void push()
	{
		state_t &s = statestack[sspos++];
		s.L = L;
		s.H = H;
		s.stableL = stableL;
		s.stableH = stableH;
	}

	void pop()
	{
		const state_t &s = statestack[--sspos];
		L = s.L;
		H = s.H;
		stableL = s.stableL;
		stableH = s.stableH;
	}

	std::string name;
	int index;
	RegClass regclass;
	bool allocatedH;
	bool allocatedL;
	bool constant;
	bool locked;
	bool external;
	bool variable;
	bool stableH;
	bool stableL;
	uword_t H;
	uword_t L;
};

//---------------------------------------------------------------------------------------------------------------------

class BLiT
{
public:

	BLiT()
		: xcount(0),
		  xcount_adj(0),
		  dyinc(0),
		  syinc(0),
		  skew(0)
	{
		em[0] = 0;
		em[1] = 0;
		em[2] = 0;
	}

	uword_t em[3];

	uword_t xcount;
	word_t xcount_adj;
	uword_t dyinc;
	uword_t syinc;
	ubyte_t skew;
};

//---------------------------------------------------------------------------------------------------------------------

typedef std::map<uword_t, int> relationtable;

class emx_metric_t
{
public:

	emx_metric_t()
		: incidence(0),
		  references(0),
		  name(-1),
		  //incidence_em0(0),
		  //incidence_em1(0),
		  //incidence_em2(0),
		  //
		  fincidence_score(0.0f),
		  fadjacency_score(0.0f)
	{
	}

	int incidence;
	int references;
	int name;
	//int incidence_em0;
	//int incidence_em1;
	//int incidence_em2;
	relationtable line_adjacencies;
	relationtable left_adjacencies;
	relationtable left_references;

	float fincidence_score;
	float fadjacency_score;

};

typedef std::map<uword_t, emx_metric_t> metricmap;

static const int max_virtual_registers = 100;
static const int max_registers = max_virtual_registers+28;

class Operation;

int DataSource_costs[] =
{
	(0<<8),
	(8<<8),
	(12<<8),
	(12<<8),
	(16<<8),
	(20<<8),
	(20<<8)+1					// small bias against long immediate because it's harder to peephole these vs short immediates
};

enum DataSource
{
	DataSource_None,
	DataSource_RegisterL,		// 8c
	DataSource_RegisterHL,		// 12c
	DataSource_Immediate16,		// 12c
	DataSource_RegisterLL,		// 16c
	DataSource_MixedIR,			// 20c
	DataSource_Immediate32,		// 20+1c
};

class preload
{
public:

	preload(uword_t _l)
		: H(0), L(_l), 
		  validH(false),
		  validL(true)
	{
	}

	uword_t H;
	uword_t L;
	bool validH;
	bool validL;
};

static inline int long_cost(uword_t _h, uword_t _l)
{
	// moveq
	long_t sA = (long_t(_h) << 16) | _l;
	if ((sA >= -128) && (sA < 128))
		return 4;

	// moveq/swap
	long_t sB = (long_t(_l) << 16) | _h;
	if ((sB >= -128) && (sB < 128))
		return 8;

	// todo: moveq+rotate(hidden)

	return 12;
}

//---------------------------------------------------------------------------------------------------------------------

enum OpKind
{
	OpKind_Other,
	//
	// generic ops
	//
	OpKind_Nop,
	//
	OpKind_LoadI16,
	OpKind_LoadI32,
	//
	OpKind_Lea16,
	OpKind_Lea32,
	//
	// blitter ops
	//
	OpKind_StoreI16,
	OpKind_StoreI32,
	OpKind_StoreR16,
	OpKind_StoreR32,
	OpKind_StoreMPI16,
	OpKind_StoreMPI32,
	//
	OpKind_DSTAddaR16,
	OpKind_DSTSubaR16,
	OpKind_DSTLea,
	OpKind_DSTAddaIL,
	//
	OpKind_SetSkew,
	//
	OpKind_SYSPageRestore,
	//
	OpKind_BLITInit,
	OpKind_BLITXCMod,
	OpKind_BLITSYIMod,
	OpKind_BLITDYIMod,
	OpKind_BLITSetup,
	OpKind_BLITGo,
	OpKind_BLITGoSkew,
	OpKind_SRCSkip,
	//
	OpKind_Terminate
};

static const char* const as_str[] =
{
	"OpKind_Other",
	//
	// generic ops
	//
	"OpKind_Nop",
	//
	"OpKind_LoadI16",
	"OpKind_LoadI32",
	//
	"OpKind_Lea16",
	"OpKind_Lea32",
	//
	// blitter ops
	//
	"OpKind_StoreI16",
	"OpKind_StoreI32",
	"OpKind_StoreR16",
	"OpKind_StoreR32",
	"OpKind_StoreMPI16",
	"OpKind_StoreMPI32",
	//
	"OpKind_DSTAddaR16",
	"OpKind_DSTSubaR16",
	"OpKind_DSTLea",
	"OpKind_DSTAddaIL",
	//
	"OpKind_SetSkew",
	//
	"OpKind_SYSPageRestore",
	//
	"OpKind_BLITInit",
	"OpKind_BLITXCMod",
	"OpKind_BLITSYIMod",
	"OpKind_BLITDYIMod",
	"OpKind_BLITSetup",
	"OpKind_BLITGo",
	"OpKind_BLITGoSkew",
	"OpKind_SRCSkip",
	//
	"OpKind_Terminate"
};

enum OpWrite
{
	OpWrite_None,
	OpWrite_Replace,
	OpWrite_Modify,
	OpWrite_ReplaceH,				// upper word replaced/extended
	OpWrite_ReplaceHCombineL,		// upper word replaced/extended, lower word combined from another source
	OpWrite_Combine,
};

enum OpSource
{
	OpSource_None,
	OpSource_Immediate,
	OpSource_Register,
	OpSource_Memory,
};

enum OpSize
{
	OpSize_None,
//	OpSize_8,
	OpSize_16,
	OpSize_32,
};

//---------------------------------------------------------------------------------------------------------------------

class Context
{
public:

	void reset_registers()
	{
		int r = 0;

		registers.clear();

		int metrics_words_allocated = 0;

		// sort datawords by incidence

		std::map<int, std::list<metricmap::iterator>> sort_metrics;
		std::map<int, metricmap::iterator> ranked_metrics;
		for (auto it = metrics.begin(); it != metrics.end(); it++)
		{
			sort_metrics[it->second.incidence].push_back(it);
		}

		int ip = 0;
		for (auto & in : sort_metrics)
		{
			for (auto & eit : in.second)
			{
				ranked_metrics[--ip] = eit;
			}
		}

/*		for (auto & rm : ranked_metrics)
		{
			std::cout << "metrics for word:0x" << std::hex << rm.second->first << " incidence:" << std::dec << rm.second->second.incidence << std::endl;
		}
*/
		//

		if (0 && PREFER_TRANSFORMS)
		{
			char name[] = "r00";

			for (int v = 0; v < max_virtual_registers; v++)
			{
				name[1] = char((v / 10) + '0');
				name[2] = char((v % 10) + '0');

				registers.push_back(dataregister(name, r++, RegClass_Data, false, false, false, false, 0));
			}
		}

		if (1 || !PREFER_TRANSFORMS)
		{
			// a0: BLiT_YC
			// a1: scrline
			// a2: BLiT_EM1
			// a3: BLiT_DST
			// a4: BLiT_EM3 (?)
			// a5: BLiT_CTRL
			// a6: BLiT_EM2 (?)

			if (shared_mask_)
			{
				// force dstlinewid into d0.w to optimize dst skips
				registers.push_back(dataregister("d0", r++, RegClass_Data, setconstants, setconstants, true, false, dstlinewid));
			}
			else
			{
				if ((srcww >= 3) && USE_DSTSKIP_REG_FIXED_PRELOAD)
				{
					// force dstlinewid into d0.w to optimize dst skips
					registers.push_back(dataregister("d0", r++, RegClass_Data, setconstants, setconstants, true, false, 0xC0000000 | dstlinewid));
				}
				else
				{
					// release d0 for general transforms
					registers.push_back(dataregister("d0", r++, RegClass_Data, false, false, false, false, 0));
				}
			}

			if (shared_mask_)
			{
				SKIPTEMP_reg = r;
				registers.push_back(dataregister("d3", r++, RegClass_Data, false, false, false, false, 0));
				registers[SKIPTEMP_reg].variable = true;
			}
			else
			{
				registers.push_back(dataregister("d3", r++, RegClass_Data, false, false, false, false, 0));
			}

			registers.push_back(dataregister("d4", r++, RegClass_Data, false, false, false, false, 0));
			registers.push_back(dataregister("d5", r++, RegClass_Data, false, false, false, false, 0));
			registers.push_back(dataregister("d6", r++, RegClass_Data, false, false, false, false, 0));
			registers.push_back(dataregister("d7", r++, RegClass_Data, false, false, false, false, 0));

/*			for (int p = 0; (p < registers.size()) && (p < preloads.size()); p++)
			{
				if (preloads[p].validL)
				{
					registers[p].stableL = true;
					registers[p].allocatedL = true;
					registers[p].L = preloads[p].L;
				}
				if (preloads[p].validH)
				{
					registers[p].stableH = true;
					registers[p].allocatedH = true;
					registers[p].H = preloads[p].H;
				}
			}
*/
		}

		// todo: different ideal register pattern & usage for 1/2/3-wide cases

		SCRLINE_reg = r;
		registers.push_back(dataregister("a1", r++, RegClass_Address, false, false, false, false, 0));
		registers[SCRLINE_reg].variable = true;

		DSTADDR_reg = r;
		registers.push_back(dataregister("a3", r++, RegClass_Address,	true/*setconstants*/, true/*setconstants*/, true, true,	0xffff8a32));
		YC_reg = r;
		registers.push_back(dataregister("a0", r++, RegClass_Address,	true/*setconstants*/, true/*setconstants*/, true, true,	0x00ff8a38));
		CTRL_reg = r;
		registers.push_back(dataregister("a5", r++, RegClass_Address,	true/*setconstants*/, true/*setconstants*/, true, true,	0xffff8a3c));

		// em1 addr always needed
		EM1_reg = r;
		registers.push_back(dataregister("a2", r++, RegClass_Address,	true/*setconstants*/, true/*setconstants*/, true, true,	0x00ff8a28));	// em1
	
		// em2 not used unless 3 words wide
		EM2_reg = 999;
		// em3 not used unless 2-3 words wide
		EM3_reg = 999;


		if (shared_mask_)
		{
			// not enough registers to allocate a6 for EM2 while also fetching masks from RAM
			// in shared-mask mode, we allocate it as the mask source buffer
			SM_reg = r;
			registers.push_back(dataregister("a6", r++, RegClass_Address, false, false, false, false, 0));	// em2
			registers[SM_reg].variable = true;
		}
		else
		{

			// em2,em3 addr depend on sprite width - free up registers if possible
			if (srcww >= 3)
			{
				EM2_reg = r;
				registers.push_back(dataregister("a6", r++, RegClass_Address, setconstants, setconstants, true, false, 0xffff8a2a));	// em2
			}
			else
				if (1)
				{
					long_t data = dstlinewid;

					if (USE_DSTSKIP_REG_FIXED_PRELOAD_INCIDENCE)
					{
						int i = 0;
						for (auto rmit = ranked_metrics.begin(); rmit != ranked_metrics.end(); rmit++, i++)
						{
							if (metrics_words_allocated == i)
							{
								if (rmit->second->second.incidence > 2)
								{
									//std::cout << "preload high incidence word " << i << ":0x" << std::hex << rmit->second->first << " incidence:" << std::dec << rmit->second->second.incidence << std::endl;
									data = (long_t)(word_t)rmit->second->first;
									metrics_words_allocated++;
								}
								break;
							}
						}
						/*
										for (auto it = metrics.begin(); it != metrics.end(); it++, i++)
										{
											if (metrics_words_allocated == i)
											{
												if (it->second.incidence > 2)
												{
													std::cout << "preload high incidence word " << i << ":0x" << std::hex << it->first << " incidence:" << std::dec << it->second.incidence << std::endl;
													data = (long_t)(word_t)(it->first);
													metrics_words_allocated++;
												}
												break;
											}
										}
						*/
					}

					registers.push_back(dataregister("a6", r++, RegClass_Address, setconstants, setconstants, true, false, (ulong_t)data));
				}
				else
				{
					// leave a6 free
					registers.push_back(dataregister("a6", r++, RegClass_Address, false, false, false, false, 0));
				}
		}

		if (srcww > 1)
		{
			EM3_reg = r;
			registers.push_back(dataregister("a4", r++, RegClass_Address, true/*setconstants*/, true/*setconstants*/, true,	true,	0x00ff8a2c));	// em3
		}
		else
		{
			// leave a4 free?
			long_t data = 0;
			bool alloc = false;

			if (USE_DSTSKIP_REG_FIXED_PRELOAD_INCIDENCE)
			{
				int i = 0;
//				for (auto it = metrics.begin(); it != metrics.end(); it++, i++)
//				for (auto & rm : ranked_metrics)
				for (auto rmit = ranked_metrics.begin(); rmit != ranked_metrics.end(); rmit++, i++)
				{
					if (metrics_words_allocated == i)
					{
						if (rmit->second->second.incidence > 2)
						{
							//std::cout << "preload high incidence word " << i << ":0x" << std::hex << rmit->second->first << " incidence:" << std::dec << rmit->second->second.incidence << std::endl;
							data = (long_t)(word_t)rmit->second->first;
							metrics_words_allocated++;

							alloc = true;
						}
						break;
					}
				}
			}

			if (alloc)
				registers.push_back(dataregister("a4", r++, RegClass_Address, setconstants, setconstants, true, false, data));	// em3
			else
				registers.push_back(dataregister("a4", r++, RegClass_Address, false, false, false, false, 0));
		}


		BHOG_reg = r;
//		registers.push_back(dataregister("d1", r++, RegClass_Data,		setconstants, setconstants, true,		0x00ffffc0));
		registers.push_back(dataregister("d1", r++, RegClass_Data,		setconstants, setconstants, true, false,	0x00ffc000));

		PLANES_reg = r;
		registers.push_back(dataregister("d2", r++, RegClass_Data,		true/*setconstants*/, true/*setconstants*/, true, true,		0xFF000004));

//		registers.push_back(dataregister("sr", r++, true, true, 0x00002300));
	}

	Context(int _srcww, int _srch, int _srccolww, int _dstww, int _guardxs, bool _shared_mask)
		: srcww(_srcww), 
		  srch(_srch),
		  srccolww(_srccolww),
		  emaddr(0),
		  memaddr(0),
		  dstaddr(0),
		  cycles(0),
		  dstlinewid(0),
		  genout(genstr),
		  verbose(false),
		  setconstants(true),
		  SCRLINE_reg(0),
		  PLANES_reg(0),
		  BHOG_reg(0),
		  guardwords((_guardxs + (16-1)) >> 4),
		  shared_mask_(_shared_mask)
	{
		if (_dstww < 3)
			dstww = 3;
		else
			dstww = _dstww;

		printf("context allocated %d x %d words\n", srch, dstww);

		memory = new uword_t[dstww * srch];
		memset(memory, 0xAA, dstww * srch * sizeof(uword_t));

//		memset(preloads, 0, sizeof(preloads));

//		blitter.xcount = srcww;

		dstlinewid = DISPLAY_LINEWIDTH + (8 * guardwords);

		//BLiTDST_name = "BLiTDST.w";
		//BLiTCTRL_name = "BLiTCTRL.w";
		//BLiTYC_name = "BLiTYC.w";
		BLiTDST_name = "(a3)";
		BLiTCTRL_name = "(a5)";
		BLiTYC_name = "(a0)";

//		BLiTEM_names.push_back("BLiTEM1.w");
//		BLiTEM_names.push_back("BLiTEM2.w");
//		BLiTEM_names.push_back("BLiTEM3.w");
		BLiTEM_names.push_back("(a2)");
		BLiTEM_names.push_back("(a6)");
		BLiTEM_names.push_back("(a4)");
//		BLiTEM1_name = "BLiTEM1.w";
//		BLiTEM2_name = "BLiTEM2.w";
//		BLiTEM3_name = "BLiTEM3.w";
		//BLiTEM1_name = "(a2)";
		//BLiTEM2_name = "(a6)";
		//BLiTEM3_name = "(a4)";

		BLiTSYI_name = "BLiTSYI.w";
		BLiTDYI_name = "BLiTDYI.w";
		BLiTXC_name = "BLiTXC.w";
		BLiTSKEW_name = "BLiTSKEW.w";
		BLiTSRC_name = "BLiTSRC.w";
		DSTADDR_regname = "a1";

		BLiTCTRL_go_srcname = "d1";	//#$c0;
		BLiTYC_planes_srcname = "d2";	//#4;

		SKIPTEMP_reg = -1;

		// free: d0,d3,d4,d5,d6,d7

//		reset_registers();

		//{
		//	char *name = "r00";
		//	for (int v = 0; v < max_virtual_registers; v++)
		//	{
		//		registers.push_back(dataregister(name, r++));

		//		name[1] = (v / 10) + '0';
		//		name[2] = (v % 10) + '0';
		//	}
		//}
	}

	~Context()
	{
		delete[] memory;
	}

	void reset_sim(bool _verbose)
	{
		verbose = _verbose;
		memset(memory, 0xAA, dstww * srch * sizeof(uword_t));
		emaddr = 0;
		memaddr = 0;
		dstaddr = 0;
		cycles = 0;
		words = 0;
		blitter.xcount = srcww;
		blitter.xcount_adj = 0;
		reset_registers();
		metrics_sim = metrics;
		shared_mask_word_pos = 0;
	}

	struct program_metrics
	{
		int words;
		int cycles;
		int spans;
		int em_mods;
		int em_modcycles;
		int src_skips;
		int src_skipcycles;
		int dst_skips;
		int dst_skipcycles;
		int group_mods;
		int group_modcycles;
		int skew_mods;
		int skew_modcycles;
		int main_cycles;
		int blit_expbuscycles;
		int blit_simbuscycles;
		int hidden_tailcycles;
	};

	int cycle_sum(program_metrics &r_program_metrics);

	void dump_tags();
	void register_rename(std::list<Operation*>::iterator curr, int _rold, int _rnew);

	DataSource sim_resolve_data16(uword_t _data, int &r_reg_index, bool _generate_program, bool _allow_statechanges);
	DataSource sim_resolve_data32(uword_t _dataH, uword_t _dataL, int &r_regH_index, int &r_regL_index, bool _generate_program, bool _allow_statechanges);

	void sim_track_datarefs(uword_t _data)
	{
		emx_metric_t& m = metrics_sim[_data];
		m.references--;
		if (m.references == 0)
		{
//			m.incidence = 0;

			// this data is no longer required - free up whatever register(s) are assigned with it
			// look for existent value in active registers
			for (std::vector<dataregister>::iterator it = registers.begin(); it != registers.end(); it++)
			{
				if (!(it->locked))
				{
					if ((it->allocatedL) && (it->L == _data))
					{
						//if (verbose)
						//	printf("> released value [$%04x] from register [%s]L\n", _data, it->name.c_str());
						it->allocatedL = false;
					}

					if ((it->allocatedH) && (it->H == _data))
					{
						//if (verbose)
						//	printf("> released value [$%04x] from register [%s]H\n", _data, it->name.c_str());
						it->allocatedH = false;
					}
				}
			}
		}
	}

	void sim_track_datarefs(uword_t _dataH, uword_t _dataL)
	{
		emx_metric_t& m = metrics_sim[_dataH];
		m.left_references[_dataL]--;

		sim_track_datarefs(_dataH);
		sim_track_datarefs(_dataL);
	}

	bool try_transforms(std::list<Operation*>::iterator &curr, OpSize _opsize, int _rold, uword_t _dataH, uword_t _dataL);

	bool try_4c_init_transforms(std::list<Operation*>::iterator &curr, OpSize _opsize, int _rold, uword_t _dataH, uword_t _dataL);
	bool try_8c_init_transforms(std::list<Operation*>::iterator &curr, OpSize _opsize, int _rold, uword_t _dataH, uword_t _dataL);
	bool try_12c_init_transforms(std::list<Operation*>::iterator &curr, OpSize _opsize, int _rold, uword_t _dataH, uword_t _dataL);

	bool try_reg_transforms(std::list<Operation*>::iterator &curr, std::vector<dataregister>::iterator it, OpSize _opsize, int _rold, uword_t _dataH, uword_t _dataL, int &r_limit, bool _2nd = false);
//	bool try_8c_reg_transforms(std::list<Operation*>::iterator &curr, std::vector<dataregister>::iterator it, OpSize _opsize, int _rold, uword_t _dataH, uword_t _dataL, bool _2nd = false);

	bool safe_rename_H(std::list<Operation*>::iterator curr, std::vector<dataregister>::iterator it_treg, int _rold);
	bool safe_rename_H(std::list<Operation*>::iterator curr, uword_t dataH, int _rold);

	bool transform_pass();
	bool cleanup_pass();
	bool reduce_pass();
	bool promote_pass();
	bool promote_lea_pass();
	void tail_pass();

	void register_sweep();

	bool tag_collision(std::list<Operation*>::iterator _start, int _rold, int _rnew, OpSize _sz);
	bool tags_busy(std::list<Operation*>::iterator _start, int _r);
	int register_count_srcrefs(std::list<Operation*>::iterator _start, int _reg, OpSize _sz);
	std::list<Operation*>::iterator register_last_written(std::list<Operation*>::iterator _start, int _reg, OpSize _sz);

	void assemble(ubyte_t *&r_p68kbin, size_t &r_size68kbin, std::string &_tmpfile)
	{
//		ubyte_t *p68kbin = 0;
		std::string tmp_lst = _tmpfile + ".lst";
		std::string tmp_bin = _tmpfile + ".bin";
		std::string tmp_src = _tmpfile + ".s";
	
		//__debugbreak();

		const std::string& str = genout.str();
		std::ofstream cfile;
		cfile.open(tmp_src);  
		cfile << str;  
		cfile.close();
		
		// execute assembler, turning our temporary .s into binary 68k
		//		ShellExecute(NULL, "open", "rmac.exe", "-n -o tmp68k.bin tmp68k.s", NULL, SW_HIDE);
		{
			// aout format
			std::string tmpargs = "-fa +oall ";

			// emit .lst files only when specified
			if (g_allargs.mplist)
			{
				tmpargs += "-l";
				tmpargs += tmp_lst;
				tmpargs += " ";
			}

			// output binary 68k
			tmpargs += "-o ";
			tmpargs += tmp_bin;

			// input temporary src file
			tmpargs += " ";
			tmpargs += tmp_src;

			std::string rmacpath = c_rmac_filename;

#if defined(_MSC_VER)
//#error
			if (g_allargs.rmacpath.length() > 1)
				rmacpath = g_allargs.rmacpath + '\\' + c_rmac_filename;


			// todo: replace this with exec() below
			
			SHELLEXECUTEINFO ShExecInfo = { 0 };
			ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
			ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
			ShExecInfo.hwnd = NULL;
			ShExecInfo.lpVerb = NULL;
			ShExecInfo.lpFile = rmacpath.c_str();
			ShExecInfo.lpParameters = tmpargs.c_str(); // "-fa -ltmp68k.lst -o tmp68k.bin tmp68k.s";
			ShExecInfo.lpDirectory = NULL;
			ShExecInfo.nShow = SW_HIDE;
			ShExecInfo.hInstApp = NULL;
			ShellExecuteEx(&ShExecInfo);
			WaitForSingleObject(ShExecInfo.hProcess, INFINITE);

			DWORD exitcode = 0;
			GetExitCodeProcess(ShExecInfo.hProcess, &exitcode);

			if (exitcode != 0)
			{
				std::cerr << "error assembling intermediate code using " << rmacpath << std::endl << std::flush;
				getchar();

				exit(-1);
			}

			//DeleteFileA(tmp_src.c_str()/*"tmp68k.s"*/);
#else
			if (g_allargs.rmacpath.length() > 1)
				rmacpath = g_allargs.rmacpath + '/' + c_rmac_filename;

			std::string command = rmacpath + " " + tmpargs;
			
			std::string result_str;
			bool success = exec(command, result_str);
			if (!success)
			{
				std::cerr << "error assembling intermediate code using " << command << std::endl << std::flush;
				getchar();

				exit(-1);
			}
#endif
			
		}


        
		// read code back in
		{
			FILE *h = fopen(tmp_bin.c_str()/*"tmp68k.bin"*/, "rb");
			if (h)
			{
				fseek(h, 0, SEEK_END);
				r_size68kbin = ftell(h);
				fseek(h, 0, SEEK_SET);

				if (r_size68kbin <= 32)
				{
					fclose(h);

					std::cerr << "error sizing binary code!" << std::endl << std::flush;
					getchar();
					exit(-1);
				}

				r_p68kbin = new ubyte_t[r_size68kbin];
				if (r_size68kbin != fread(r_p68kbin, 1, r_size68kbin, h))
				{
					fclose(h);

					std::cerr << "error reading binary code!" << std::endl << std::flush;
					getchar();
					exit(-1);
				}

#if (USE_NOP_BINARY_VALIDATION)
				if ((r_p68kbin[32+0] != 0x4e) ||
					(r_p68kbin[32+1] != 0x71))
				{
					std::cerr << "error validating binary code begin!" << std::endl;
					getchar();
					exit(-1);

					fclose(h);
				}
#endif

				size_t oldsize = r_size68kbin;
				while (
					r_size68kbin &&
					(
						(r_p68kbin[r_size68kbin - 2] != 0x4e) ||
						(r_p68kbin[r_size68kbin - 1] != 0x75)
					)
				)
				{
					r_size68kbin--;
				}

				if (g_allargs.verbose)
					if (oldsize != r_size68kbin)
						std::cout << "trimmed binary code by " << (oldsize-r_size68kbin) << " bytes" << std::endl;


				if (r_size68kbin == 0)
				{
					fclose(h);

					std::cerr << "error validating binary code end!" << std::endl << std::flush;
					getchar();
					exit(-1);
				}

				fclose(h);

#if defined(_MSC_VER)
				DeleteFileA(tmp_bin.c_str()/*"tmp68k.bin"*/);
#endif
            }
			else
			{
				std::cerr << "error opening binary code!" << std::endl << std::flush;
				getchar();
				exit(-1);
			}
		}
	}

	std::string genstr;
	std::stringstream genout;
	bool verbose;

	std::vector<preload> preloads;

	int srcww, dstww;
	int srccolww;
	int srch;
	int dstlinewid;
	int guardwords;

	metricmap metrics;
	metricmap metrics_sim;

	std::vector<u16> shared_mask_words;
	int shared_mask_word_pos;

	std::vector<dataregister> registers;

	std::list<Operation*> operations;

	std::string BLiTSRC_name;
	std::string BLiTDST_name;
	std::string BLiTCTRL_name;
//	std::string BLiTEM1_name;
//	std::string BLiTEM2_name;
//	std::string BLiTEM3_name;
	std::string BLiTYC_name;
	std::string BLiTSYI_name;
	std::string BLiTDYI_name;
	std::string BLiTXC_name;
	std::string BLiTSKEW_name;

	std::vector<std::string> BLiTEM_names;

	bool shared_mask_;

	int SKIPTEMP_reg;
	int SCRLINE_reg;
	int PLANES_reg;
	int BHOG_reg;
	int DSTADDR_reg;
	int YC_reg;
	int CTRL_reg;
	int EM1_reg;
	int EM2_reg;
	int EM3_reg;
	int SM_reg;
	std::string DSTADDR_regname;
	std::string BLiTCTRL_go_srcname;
	std::string BLiTYC_planes_srcname;


	// blitter
	BLiT blitter;
	int emaddr;

	// framebuffer
	uword_t *memory;
	int memaddr;
	int dstaddr;

//	wordpair_t registers[16];
	int cycles;
	int words;

	bool setconstants;

	char buf[1024];
};


#include "68ksimops.h"


// metrics: 
//	total incidence
//	incidence at EM0,EM1,EM2
//	line adjacency vs all others
//	left-adjacency vs all others

typedef std::map<u64, int> linemap;


//static void trylineseq(const std::vector<int> &_stack_free, const std::vector<int> &_stack, std::vector<int> &_stack_best)
//{
//	int freesize = _stack_free.size();
//	if (freesize == 0)
//		return;
//
//	std::vector<int> stack;
//	std::vector<int> stack_free;
//
//	for (int n = 0; n < freesize; n++)
//	{
//		// try all positions as next place
//		stack = _stack;
//		stack.push_back(_stack_free[n]);
//
//		stack_free = _stack_free;
//		stack_free[n] = _stack_free[freesize-1];
//		stack_free.pop_back();
//
//		trylineseq(stack_free, stack, _stack_best);
//	}
//}

DataSource Context::sim_resolve_data16(uword_t _data, int &r_reg_index, bool _generate_program, bool _allow_statechanges)
{
	// 1) look for existent value in active registers (allocated or otherwise)
	for (std::vector<dataregister>::iterator it = registers.begin(); it != registers.end(); it++)
	{
		if (!(it->variable) &&
			(it->stableL) &&
			(it->L == _data))
		{
			if (_generate_program)
			{
				if (g_allargs.debug)
					printf("> referenced value [$%04x] in register [%s]L\n", _data, it->name.c_str());
			}
			r_reg_index = it->index;
			return DataSource_RegisterL;
		}
	}

	if (_allow_statechanges)
	{

		if (1 && PREFER_TRANSFORMS)
		{
			int i = 0;
			for (std::vector<dataregister>::iterator it = registers.begin(); it != registers.end(); it++, i++)
			{
				if (!(it->locked) && !(it->variable))
				{
					if (!(it->stableL))
					{
						if (_generate_program)
						{
							if (g_allargs.debug)
								printf("> assigned value [$%04x] to register [%s]L\n", _data, it->name.c_str());
							operations.push_back(new DR_LoadI16(*this, it->index, _data));

							it->allocatedL = true;
							it->L = _data;

							operations.back()->execute();
						}

						r_reg_index = it->index;
						return DataSource_RegisterL;
					}
				}
			}
		}


		// 2) assign value to unallocated register if sufficient reuse expected
		if (metrics_sim[_data].references >= 2)
		{
			int best_refcount = 1 << 30;
			int best_reg = -1;
			int i = 0;
			for (std::vector<dataregister>::iterator it = registers.begin(); it != registers.end(); it++, i++)
			{
				if (!(it->locked) && !(it->variable))
				{
					if (!(it->allocatedL))
					{
						if (_generate_program)
						{
							if (g_allargs.debug)
								printf("> assigned value [$%04x] to register [%s]L\n", _data, it->name.c_str());
							operations.push_back(new DR_LoadI16(*this, it->index, _data));

							it->allocatedL = true;
							it->L = _data;

							operations.back()->execute();
						}

						r_reg_index = it->index;
						return DataSource_RegisterL;
					}
					else
					{
						// meanwhile, look for the best still-active register to steal
						const emx_metric_t &m = metrics_sim[it->L];
						if (m.references < best_refcount)
						{
							best_refcount = m.references;
							best_reg = i;
						}
					}
				}
			}

			// 3) steal allocated register, if estimated saving makes it worthwhile
			// factor +1 cost for reload
			if ((best_reg >= 0) &&
				(metrics_sim[_data].references > (best_refcount + 1)))
			{
				dataregister &r = registers[best_reg];
				if (_generate_program)
				{
					if (g_allargs.debug)
						printf("> replaced value [$%04x] with [$%04x] at register [%s]L\n", r.L, _data, r.name.c_str());
					//				r.L = _data;
					// allows future choices to acknowledge the reuse incidence looks different from now on
					//				metrics[_data].incidence = metrics[_data].references;

					operations.push_back(new DR_LoadI16(*this, best_reg, _data));

					r.L = _data;

					operations.back()->execute();
				}
				r_reg_index = r.index;

				//if (PREFER_TRANSFORMS)
				//	assert(false);

				return DataSource_RegisterL;
			}
		}
	}

	// single-use, or last resort - just load immediate data
	if (_generate_program)
		if (g_allargs.debug)
			printf("> resolved value [$%04x] as immediate\n", _data);

	return DataSource_Immediate16;
}

DataSource Context::sim_resolve_data32(uword_t _dataH, uword_t _dataL, int &r_regH_index, int &r_regL_index, bool _generate_program, bool _allow_statechanges)
{
	// 1) look for existent value in active registers (allocated or otherwise)
	for (std::vector<dataregister>::iterator it = registers.begin(); it != registers.end(); it++)
	{
		if (!(it->variable) &&
			(it->stableH) &&
			(it->H == _dataH) && 
			(it->L == _dataL)
		)
		{
			if (_generate_program)
			{
				if (g_allargs.debug)
					printf("> referenced value [$%04x%04x] in register [%s]HL\n", _dataH, _dataL, it->name.c_str());
			}
			r_regH_index = it->index;
			return DataSource_RegisterHL;
		}
	}

	if (_allow_statechanges)
	{
		if (1 && PREFER_TRANSFORMS)
		{
			int i = 0;
			for (std::vector<dataregister>::iterator it = registers.begin(); it != registers.end(); it++, i++)
			{
				// don't touch constant registers
				if (!(it->locked) && !(it->variable))
				{
					// don't touch allocated fields
					if (!(it->stableH) &&
						!(it->stableL))
					{
						if (_generate_program)
						{
							if (g_allargs.debug)
								printf("> assigned value [$%04x%04x] to register [%s]HL\n", _dataH, _dataL, it->name.c_str());

							operations.push_back(new DR_LoadI32(*this, it->index, _dataH, _dataL));

							it->allocatedH = true;
							it->allocatedL = true;
							it->H = _dataH;
							it->L = _dataL;

							operations.back()->execute();
						}

						r_regH_index = it->index;
						return DataSource_RegisterHL;
					}
				}
			}
		}

		// 2) assign value to unallocated register if sufficient reuse expected or if value is cheap to load
		if (
			((metrics_sim[_dataH].left_references[_dataL] >= 2) ||
			(metrics_sim[_dataL].references >= 2)) ||
			(long_cost(_dataH, _dataL) <= 8)
			)
		{
			int best_refcount = 1 << 30;
			int best_reg = -1;
			int i = 0;
			for (std::vector<dataregister>::iterator it = registers.begin(); it != registers.end(); it++, i++)
			{
				// don't touch constant registers
				if (!(it->locked) && !(it->variable))
				{
					// don't touch allocated fields
					if (!(it->allocatedH) &&
						!(it->allocatedL))
					{
						if (_generate_program)
						{
							if (g_allargs.debug)
								printf("> assigned value [$%04x%04x] to register [%s]HL\n", _dataH, _dataL, it->name.c_str());

							operations.push_back(new DR_LoadI32(*this, it->index, _dataH, _dataL));

							it->allocatedH = true;
							it->allocatedL = true;
							it->H = _dataH;
							it->L = _dataL;

							operations.back()->execute();
						}

						r_regH_index = it->index;
						return DataSource_RegisterHL;
					}
					else
					{
						// meanwhile, look for the best still-active register to steal
						//emx_metric_t &m = metrics[it->L];
						//if (m.references < best_refcount)
						//{
						//	best_refcount = m.references;
						//	best_reg = i;
						//}
					}
				}
			}

			// 3) steal allocated register, if estimated saving makes it worthwhile
			// factor +1 cost for reload
			//if (best_reg >= 0 && metrics[_data].references > (best_refcount+1))
			//{
			//	dataregister &r = registers[best_reg];
			//	printf("> replaced value [$%04x] with [$%04x] at register [%s]L\n", r.L, _data, r.name.c_str());
			//	r.L = _data;
			//	// allows future choices to acknowledge the reuse incidence looks different from now on
			//	metrics[_data].incidence = metrics[_data].references;
			//	r_reg_index = r.index;
			//	return DataSource_RegisterL;			
			//}
		}
	}

	// look for mixed combination (high immediate + low reg) for cases with nontrivial high word
	// since immediate words are easier to peephole than arbitrary immediate longs
	for (std::vector<dataregister>::iterator it = registers.begin(); it != registers.end(); it++)
	{
		if ((it->H != 0x0000) && 
			(it->H != 0xFFFF))
		{
			if (!(it->variable) &&
				(it->stableL) && 
				(it->L == _dataL))
			{
				if (_generate_program)
				{
					if (g_allargs.debug)
						printf("> referenced value [$%04x] in register [%s]L\n", _dataL, it->name.c_str());
				}
				r_regL_index = it->index;
				return DataSource_MixedIR;
			}
		}
	}

	// single-use, or last resort - just load immediate data
	if (_generate_program)
		if (g_allargs.debug)
			printf("> resolved value [$%04x%04x] as immediate\n", _dataH, _dataL);

	return DataSource_Immediate32;
}

void Context::dump_tags()
{
	//register_sweep();

	for (std::list<Operation*>::iterator it = operations.begin(); it != operations.end(); it++)
	{
		Operation *poper = *it;

		printf("tags:");
		for (int r = 0; r < registers.size() && r < 6; r++)
		{
			printf(" %s[%02x:%02x](%08x)", 
				registers[r].name.c_str(), 
				ubyte_t(poper->alltagsH[r]), ubyte_t(poper->alltagsL[r]),
				(((ulong_t)registers[r].H << 16) | registers[r].L)
			);
		}
		printf("  %s", poper->describe().c_str());
	}
}

void Context::register_rename(std::list<Operation*>::iterator _start, int _regold, int _regnew)
{
	if (_regold == _regnew)
		return;

	Operation* poper = *_start;

	// caution: low and high words of same register can have different tags
	// also, low and high words of same register may independently live or dead (-1)

	int tagoldH = -1;
	int tagoldL = -1;
	if (_regold == poper->dreg) // give dreg priority over sreg for sake of single-register outputs (dreg = 'dreg)
	{
		tagoldH = poper->dtagH;
		tagoldL = poper->dtagL;
	}
	else
	if (_regold == poper->sreg)
	{
		tagoldH = poper->stagH;
		tagoldL = poper->stagL;
	}
	else
	if (_regold == poper->areg)
	{
		tagoldH = poper->atag;
		tagoldL = poper->atag;
	}

	// can only rename if some part is live
	if ((tagoldL < 0) && (tagoldH < 0))
		return;

	//dump_tags();

	for (std::list<Operation*>::iterator it = _start/*operations.begin()*/; it != operations.end(); it++)
	{
		Operation *poper = *it; 

		if ((poper->opdst == OpSource_Register) &&
			(
				((poper->dtagL >= 0 ) && ((poper->dtagL == tagoldL) || (poper->dtagL == tagoldH))) ||
				((poper->dtagH >= 0 ) && (/*(poper->dtagH == tagoldL) ||*/ (poper->dtagH == tagoldH)))
			)
		)
		{
			if (g_allargs.debug)
				printf("replacing D:%s with %s\n", registers[poper->dreg].name.c_str(), registers[_regnew].name.c_str());

			if ((poper->opsrc == OpSource_None) && (poper->sreg == poper->dreg))
			{
				poper->sreg = _regnew;
			}

			poper->dreg = _regnew;
		}

		if ((poper->opsrc == OpSource_Register) &&
			(
				((poper->stagL >= 0 ) && ((poper->stagL == tagoldL) || (poper->stagL == tagoldH))) ||
				((poper->stagH >= 0 ) && (/*(poper->stagH == tagoldL) ||*/ (poper->stagH == tagoldH)))
			)
		)
		{
			if (g_allargs.debug)
				printf("replacing S:%s with %s\n", registers[poper->sreg].name.c_str(), registers[_regnew].name.c_str());
			poper->sreg = _regnew;
		}


		if ((poper->atag >= 0) && (poper->atag == tagoldL))
		{
			if (g_allargs.debug)
				printf("replacing A:%s with %s\n", registers[poper->areg].name.c_str(), registers[_regnew].name.c_str());
			poper->areg = _regnew;
		}
	}

	//register_sweep();

	//dump_tags();
}

void Context::register_sweep()
{
	static std::vector<std::pair<int,int> > s_tags;
	s_tags.clear();
	s_tags.resize(registers.size(), std::pair<int,int>(-1,-1));
	int newtag = 0;

#if (DEBUG_REGISTER_TAGS)
	printf("\nregister sweep...\n");
#endif

	for (std::list<Operation*>::reverse_iterator rit = operations.rbegin(); rit != operations.rend(); rit++)
	{
		Operation *poper = *rit;

		int replacedH = -1;
		int replacedL = -1;

		// data source
		if (poper->sreg >= 0)
		{
			assert(poper->sreg < s_tags.size());

			// if not yet tagged - generate new tag
			std::pair<int,int> &optag = s_tags[poper->sreg];

/*			23
			23
			23 16
			22
			22 32
			01
			01
			01 16
			00
			00
*/
			if (optag.first < 0)
			{
				optag.first = newtag++;
			}

			if (poper->opsize == OpSize_32)
			{
				if (optag.second < 0)
					optag.second = optag.first;
			}

			poper->stagH = optag.second;
			poper->stagL = optag.first;

#if (DEBUG_REGISTER_TAGS)
			if (poper->opsize == OpSize_32)
			printf("referenced: %s (%02d:%02d) [32] : %s\n", 
				registers[poper->sreg].name.c_str(), 
				poper->stagH, 
				poper->stagL,
				poper->describe().c_str()
				);
			else
			printf("referenced: %s (%02d:%02d) [16] : %s\n", 
				registers[poper->sreg].name.c_str(), 
				poper->stagH, 
				poper->stagL,
				poper->describe().c_str()
				);
#endif
		}

		// address source
		if (poper->areg >= 0)
		{
//			printf("areg: %d\n", poper->areg);
			assert(poper->areg < s_tags.size());

			// if not yet tagged - generate new tag
			std::pair<int,int> &optag = s_tags[poper->areg];

			if (optag.first < 0)
			{
				optag.first = newtag++;
				optag.second = optag.first;
			}

			poper->atag = optag.first;

#if (DEBUG_REGISTER_TAGS)
			printf("referenced: %s (%02d) : %s", 
				registers[poper->areg].name.c_str(), 
				poper->atag,
				poper->describe().c_str()
				);
#endif
		}

		// destination as source
		if ((poper->dreg >= 0) && 
			((poper->opwr == OpWrite_Modify) || 
			 (poper->opwr == OpWrite_Combine) ||
			 (poper->opwr == OpWrite_Replace) || 
			 (poper->opwr == OpWrite_ReplaceH) || 
			 (poper->opwr == OpWrite_ReplaceHCombineL))
		)
		{
			assert(poper->dreg < s_tags.size());

			// if not yet tagged - generate new tag
			std::pair<int,int> &optag = s_tags[poper->dreg];

			if (optag.first < 0)
			{
				optag.first = newtag++;
			}

				if (poper->opsize == OpSize_32)
				{
					if (optag.second < 0)
						optag.second = optag.first;
				}

			poper->dtagH = optag.second;
			poper->dtagL = optag.first;

#if (DEBUG_REGISTER_TAGS)
			if (poper->opwr == OpWrite_Combine)
			printf("combined: %s (%02d:%02d) : %s", 
				registers[poper->dreg].name.c_str(), 
				poper->dtagH, 
				poper->dtagL,
				poper->describe().c_str()
				);
			else
			if (poper->opwr == OpWrite_Replace && poper->opsize == OpSize_32)
			printf("replaced: %s (%02d:%02d) [32] : %s", 
				registers[poper->dreg].name.c_str(), 
				poper->dtagH, 
				poper->dtagL,
				poper->describe().c_str()
				);
			else
			if (poper->opwr == OpWrite_Replace && poper->opsize == OpSize_16)
			printf("replaced: %s (%02d:%02d) [16] : %s", 
				registers[poper->dreg].name.c_str(), 
				poper->dtagH, 
				poper->dtagL,
				poper->describe().c_str()
				);
			else
			if (poper->opwr == OpWrite_ReplaceHCombineL)
			printf("repHcmbL: %s (%02d:%02d) : %s", 
				registers[poper->dreg].name.c_str(), 
				poper->dtagH, 
				poper->dtagL,
				poper->describe().c_str()
				);
			else
			if (poper->opsize == OpSize_32)
			printf("modified: %s (%02d:%02d) [32] : %s", 
				registers[poper->dreg].name.c_str(), 
				poper->dtagH, 
				poper->dtagL,
				poper->describe().c_str()
				);
			else
			printf("modified: %s (%02d:%02d) [16] : %s", 
				registers[poper->dreg].name.c_str(), 
				poper->dtagH, 
				poper->dtagL,
				poper->describe().c_str()
				);
#endif
		}

		//// destination replaced (rename)
		//if ((poper->dreg >= 0) && 
		//	((poper->opwr == OpWrite_Replace) || 
		//	(poper->opwr == OpWrite_ReplaceHCombineL))
		//	)
		//{
		//	// if not yet tagged - generate new tag
		//	std::pair<int,int> &optag = tags[poper->dreg];

		//	if (optag.first < 0)
		//	{
		//		optag.first = newtag++;
		//		if (poper->opsize == OpSize_32)
		//			optag.second = optag.first;
		//	}

		//	poper->dtagH = optag.second;
		//	poper->dtagL = optag.first;

		//	//printf("replaced: %s (%d:%d)\n", 
		//	//	registers[poper->dreg].name.c_str(), 
		//	//	poper->dtagH, 
		//	//	poper->dtagL
		//	//	);

		//	// lower word only replaced if a simple replace op (e.g. not a sign-extending combine, where only upper word is replaced)
		//	if (poper->opwr != OpWrite_ReplaceHCombineL)
		//		replacedL = poper->dreg;

		//	if (poper->opsize == OpSize_32)
		//		replacedH = poper->dreg;
		//}

		// lower word only replaced if a simple replace op (e.g. not a sign-extending combine, where only upper word is replaced)
		if (poper->opwr == OpWrite_Replace)
			replacedL = poper->dreg;

		if (poper->opsize == OpSize_32)
			if (poper->opwr == OpWrite_Replace || poper->opwr == OpWrite_ReplaceH || poper->opwr == OpWrite_ReplaceHCombineL)
				replacedH = poper->dreg;

		int r = 0;
		for (std::vector<std::pair<int,int> >::iterator it = s_tags.begin(); it != s_tags.end(); it++, r++)
		{
			std::pair<int,int> &tag = *it;

			// save full tag pattern for this state in operation to help with renaming
			assert(r < sizeof(poper->alltagsH));
			poper->alltagsH[r] = tag.second;
			poper->alltagsL[r] = tag.first;

#if (DEBUG_REGISTER_TAGS2)
			if (tag.first >= 0)
				printf("%02d:%02d ", tag.second, tag.first);
			else
				printf("--:-- ");
#endif
		}

		// reset tags for replaced register BEFORE replace op (running backwards)
		if (replacedL >= 0)
		{
			assert(replacedL < s_tags.size());
			s_tags[replacedL].first = -1;
		}
		if (replacedH >= 0)
		{
			assert(replacedH < s_tags.size());
			s_tags[replacedH].second = -1;
		}

#if (DEBUG_REGISTER_TAGS)
		printf("\n");
#endif
	}
}

bool Context::tags_busy(std::list<Operation*>::iterator _exepos, int _r)
{
	// detect existing dependency on either part of register _r
	Operation *poper = *_exepos;

	assert(_r < sizeof(poper->alltagsL));

	if (poper->alltagsL[_r] >= 0)
		return true;

	if (poper->alltagsH[_r] >= 0)
		return true;

	return false;
}

bool Context::tag_collision(std::list<Operation*>::iterator _start, int _rold, int _rnew, OpSize _sz)
{
	// reg can't collide with itself
	if (_rnew == _rold)
		return false;

	assert(_rold >= 0);
	assert(_rnew >= 0);

	// detecttags lifespan tag conflicts between two registers
	for (std::list<Operation*>::iterator it = _start; it != operations.end(); it++)
	{
		Operation *poper = *it;
		assert(_rnew < sizeof(poper->alltagsL));
		assert(_rold < sizeof(poper->alltagsL));

		// check if upper word of target register is busy.
		if (poper->alltagsH[_rnew] >= 0)
		{
			// note: if busy, it may still be renamed but *only* if upper words already match!
			return true; // collision
		}

		// if lower word of target is busy, can't rename
		if (poper->alltagsL[_rnew] >= 0)
			return true; // collision

/*
		assert(_rold < sizeof(poper->alltagsL));
		if (poper->alltagsL[_rold] >= 0)
		{
			assert(_rnew < sizeof(poper->alltagsL));
			if (poper->alltagsL[_rnew] >= 0)
				return true;
		}

		if (_sz == OpSize_32)
		{
			assert(_rold < sizeof(poper->alltagsH));
			if (poper->alltagsH[_rold] >= 0)
			{
				assert(_rnew < sizeof(poper->alltagsH));
				if (poper->alltagsH[_rnew] >= 0)
					return true;
			}
		}
*/
	}

	return false;
}

#define USE_XFORM_ADDSUBQ (1)

#include "68kxforms.inl"



bool Context::try_transforms(std::list<Operation*>::iterator &curr, OpSize _opsize, int _rold, uword_t _dataH, uword_t _dataL)
{
	// can't transform variable registers
	if (registers[_rold].variable)
		return false;

	uword_t data16 = _dataL;
	ulong_t data32 = (ulong_t(_dataH) << 16) | _dataL;

	// try cheapest form of init (CLR/MOVEQ)
	if (try_4c_init_transforms(curr, _opsize, _rold, _dataH, _dataL))
		goto terminate;

	// look for cheapest transforms of free register values
	{
		// start with self
		std::vector<dataregister>::iterator itself = registers.begin() + _rold;

		int limit = 4;
		if (try_reg_transforms(curr, itself, _opsize, _rold, _dataH, _dataL, limit))
			goto terminate;

		for (std::vector<dataregister>::iterator it = registers.begin(); it != registers.end(); it++)
		{
			if (it == itself)
				continue;

			int limit = 4;
			if (try_reg_transforms(curr, it, _opsize, _rold, _dataH, _dataL, limit))
				goto terminate;
		}
	}

	// try MOVEQ for 16bit loads to registers pre-initialized H
//#if (USE_MOVEQ_INITS)
	if (_opsize == OpSize_16)
	{
		// OPPORTUNITY: (MOVEQ.L #N,Rn.W == EXPECTED)
		if ((registers[_rold].regclass == RegClass_Data)
		)
		{
			for (int imm = -128; imm < 128; imm++)
			{
//				if (data16 == uword_t(imm))
//				{
				uword_t imm_hw = uword_t(imm >> 16);
				uword_t imm_lw = uword_t(imm);

				if ((data16 == imm_lw) && 
					// don't perform moveq if it modifies a high word dependency
					(/*!registers[_rold].stableH*/((*curr)->alltagsH[_rold] < 0) || (registers[_rold].H == imm_hw)))
				{

					if (g_allargs.debug)
						printf("transform: move.w #$%04x,%s -> moveq.l #%d,%s [$%04x:%04x]\n",
							data16,
							registers[_rold].name.c_str(),
							imm,
							registers[_rold].name.c_str(),
							registers[_rold].H,
							registers[_rold].L
							);

					curr = operations.erase(curr);
					curr = operations.insert(curr, new DR_MoveQ32(*this, imm, _rold));
					goto terminate;
				}
			}
		}
	}
//#endif

	// look for compound replacements for MOVE.L #IMM,Rx
	if (try_8c_init_transforms(curr, _opsize, _rold, _dataH, _dataL))
		goto terminate;

	// look for more expensive transforms of free register values
	{
		// start with self
		std::vector<dataregister>::iterator itself = registers.begin() + _rold;

		int limit = 8;
		if (try_reg_transforms(curr, itself, _opsize, _rold, _dataH, _dataL, limit))
			goto terminate;

		for (std::vector<dataregister>::iterator it = registers.begin(); it != registers.end(); it++)
		{
			if (it == itself)
				continue;

			int limit = 8;
			if (try_reg_transforms(curr, it, _opsize, _rold, _dataH, _dataL, limit))
				goto terminate;
		}
	}

	// look for more expensive transforms of free register values
	if (ALLOW_BTAIL_OPTIMIZATIONS)
	{
		// start with self
		std::vector<dataregister>::iterator itself = registers.begin() + _rold;

		int limit = 12;
		if (try_reg_transforms(curr, itself, _opsize, _rold, _dataH, _dataL, limit))
			goto terminate;

		for (std::vector<dataregister>::iterator it = registers.begin(); it != registers.end(); it++)
		{
			if (it == itself)
				continue;

			int limit = 12;
			if (try_reg_transforms(curr, it, _opsize, _rold, _dataH, _dataL, limit))
				goto terminate;
		}
	}

	// look for most expensive transforms of free register values
	if (ALLOW_BTAIL_OPTIMIZATIONS && (srcww > 2))
	{
		// start with self
		std::vector<dataregister>::iterator itself = registers.begin() + _rold;

		int limit = 100;
		if (try_reg_transforms(curr, itself, _opsize, _rold, _dataH, _dataL, limit))
			goto terminate;

		for (std::vector<dataregister>::iterator it = registers.begin(); it != registers.end(); it++)
		{
			if (it == itself)
				continue;

			int limit = 100;
			if (try_reg_transforms(curr, it, _opsize, _rold, _dataH, _dataL, limit))
				goto terminate;
		}
	}

	// try more expensive replacements for MOVE.L #IMM,Rx
	//if (try_12c_init_transforms(curr, _opsize, _rold, _dataH, _dataL))
	//	goto terminate;

	return false;

terminate:;

	return true;
}

bool Context::transform_pass()
{
	reset_sim(true);

	register_sweep();

	//dump_tags();

	if (!g_allargs.quickcut)
		if (g_allargs.verbose)
			printf("transform pass: replace word #immediate register loads with derived register values\n");

	for (std::list<Operation*>::iterator it = operations.begin(); it != operations.end();)
	{
		std::list<Operation*>::iterator curr = it++;
		Operation *pop = *curr;

		if (!pop->hidden)
		{
			if (pop->kind == OpKind_LoadI16)
			{
				DR_LoadI16 *pactual = reinterpret_cast<DR_LoadI16*>(pop);
				int rold = pactual->dreg;

				if (try_transforms(curr, OpSize_16, rold, 0, pactual->data))
					return true;
			}
			else
				if (pop->kind == OpKind_LoadI32)
				{
					DR_LoadI32 *pactual = reinterpret_cast<DR_LoadI32*>(pop);
					int rold = pactual->dreg;

					//if ((pactual->dataH == 0) && (pactual->dataL == 0xe0))
					//{
					//	int a = 0;
					//}

					if (try_transforms(curr, OpSize_32, rold, pactual->dataH, pactual->dataL))
						return true;
				}
		}

		// catch up with execution from last insert point
		while (curr != it)
		{
			register_sweep();
			(*curr)->execute(); 
			curr++;
		}
	}

	return false;
}


bool Context::cleanup_pass()
{
	reset_sim(true);

	register_sweep();

	bool changed = false;

	if (!g_allargs.quickcut)
		if (g_allargs.verbose)
			printf("cleanup pass: replace #immediate EM stores with modified register values\n");

	for (std::list<Operation*>::iterator it = operations.begin(); it != operations.end();)
	{
		std::list<Operation*>::iterator curr = it++;
		Operation *pop = *curr;

		if (pop->hidden)
			goto terminate;

		if (pop->kind == OpKind_DSTLea)
		{
			DST_Lea *pactual = reinterpret_cast<DST_Lea*>(pop);

			for (std::vector<dataregister>::iterator it = registers.begin(); it != registers.end(); it++)
			{
				int rnew = it->index;

				if (it->stableL &&
					(word_t(pactual->delta(dstlinewid)) == word_t(it->L)))
				{
					if (g_allargs.debug)
						printf("transform: lea.w %d(%s),%s -> adda.w %s,%s [$%04x]\n",
							pactual->delta(dstlinewid),
							registers[SCRLINE_reg].name.c_str(), registers[SCRLINE_reg].name.c_str(),
							registers[rnew].name.c_str(),
							registers[SCRLINE_reg].name.c_str(),
							registers[rnew].L
						);
					int wordjump = pactual->worddelta;
					int linejump = pactual->linedelta;
					curr = operations.erase(curr);
					curr = operations.insert(curr, new DST_AddaR16(*this, rnew, SCRLINE_reg, wordjump, linejump));
					changed = true;
					goto terminate;
				}

				if (it->stableL &&
					(word_t(pactual->delta(dstlinewid)) == -word_t(it->L)))
				{
					if (g_allargs.debug)
						printf("transform: lea.w %d(%s),%s -> suba.w %s,%s [$%04x]\n",
							pactual->delta(dstlinewid),
							registers[SCRLINE_reg].name.c_str(), registers[SCRLINE_reg].name.c_str(),
							registers[rnew].name.c_str(),
							registers[SCRLINE_reg].name.c_str(),
							registers[rnew].L
							);
					int wordjump = pactual->worddelta;
					int linejump = pactual->linedelta;
					curr = operations.erase(curr);
					curr = operations.insert(curr, new DST_SubaR16(*this, rnew, SCRLINE_reg, wordjump, linejump));
					changed = true;
					goto terminate;
				}
			}
		}

		if (pop->kind == OpKind_StoreI16)
		{
			EM_MoveI16 *pactual = reinterpret_cast<EM_MoveI16*>(pop);
			int emold = pactual->em;
			int arold = pactual->areg;

			// now look for transforms of free register values
			{
				for (std::vector<dataregister>::iterator it = registers.begin(); it != registers.end(); it++)
				{
					int rnew = it->index;

					if (registers[rnew].stableL &&
						(pactual->data == (it->L)))
					{
						if (g_allargs.debug)
							printf("transform: move.w #$%04x,%s -> move.w %s,%s [$%04x]\n", 
								pactual->data, 
								BLiTEM_names[emold].c_str(),
								registers[rnew].name.c_str(),
								BLiTEM_names[emold].c_str(),
								registers[rnew].L
							);
						curr = operations.erase(curr);
						curr = operations.insert(curr, new EM_MoveR16(*this, emold, rnew, arold));
						changed = true;
						goto terminate;
					}
				}
			}
		}

		if (pop->kind == OpKind_StoreI32)
		{
			EM_MoveI32 *pactual = reinterpret_cast<EM_MoveI32*>(pop);
			int emold = pactual->em;
			int arold = pactual->areg;

			// now look for transforms of free register values
			{
				for (std::vector<dataregister>::iterator it = registers.begin(); it != registers.end(); it++)
				{
					int rnew = it->index;

					if (registers[rnew].stableL && (pactual->dataL == (it->L)) &&
						registers[rnew].stableH && (pactual->dataH == (it->H))
					)
					{
						if (g_allargs.debug)
							printf("transform: move.l #$%04x%04x,%s -> move.l %s,%s [$%04x%04x]\n", 
								pactual->dataH, pactual->dataL, 
								BLiTEM_names[emold].c_str(),
								registers[rnew].name.c_str(),
								BLiTEM_names[emold].c_str(),
								registers[rnew].H,
								registers[rnew].L
							);
						curr = operations.erase(curr);
						assert(arold < 256);
						curr = operations.insert(curr, new EM_MoveR32(*this, emold, rnew, arold));
						changed = true;
						goto terminate;
					}
				}
			}
		}

		// convert long address loads to lea
		if (pop->kind == OpKind_LoadI32)
		{
			DR_LoadI32 *pactual = reinterpret_cast<DR_LoadI32*>(pop);

			if (registers[pactual->dreg].regclass == RegClass_Address)
			{
				if ((registers[pactual->dreg].H == 0) && (!(registers[pactual->dreg].L & 0x8000)))
				{
					// short +ve
					curr = operations.erase(curr);
					curr = operations.insert(curr, new AR_Lea16(*this, pactual->dreg, registers[pactual->dreg].L));
					changed = true;
					goto terminate;
				}
				else
				if ((registers[pactual->dreg].H == 0xFFFF) && (registers[pactual->dreg].L & 0x8000))
				{
					// short -ve
					curr = operations.erase(curr);
					curr = operations.insert(curr, new AR_Lea16(*this, pactual->dreg, registers[pactual->dreg].L));
					changed = true;
					goto terminate;
				}
				else
				{
					curr = operations.erase(curr);
					curr = operations.insert(curr, new AR_Lea32(*this, pactual->dreg, registers[pactual->dreg].H, registers[pactual->dreg].L));
					changed = true;
					goto terminate;
				}
			}
		}


terminate:

		// catch up with execution from last insert point
		while (curr != it)
		{
			register_sweep();
			(*curr)->execute(); 
			curr++;
		}
	}

	return changed;
}

std::list<Operation*>::iterator Context::register_last_written(std::list<Operation*>::iterator _start, int _reg, OpSize _sz)
{
	// get tag for register used at point of interest
	Operation *popfirst = *_start;
	int tagH = popfirst->alltagsH[_reg];
	int tagL = popfirst->alltagsL[_reg];

	std::list<Operation*>::iterator itlast = _start;

	// find last write to this tag as dest
	for (std::list<Operation*>::iterator it = operations.begin(); it != operations.end(); it++)
	{
		Operation *pop = *it;

		// look for writes
		if ((pop->opdst == OpSource_Register) &&
			(pop->opwr != OpWrite_None)
			)
		{
			bool matchH = (tagH >= 0) && ((pop->alltagsH[pop->dreg] == tagH) || (pop->alltagsL[pop->dreg] == tagH));
			bool matchL = (tagL >= 0) && ((pop->alltagsH[pop->dreg] == tagL) || (pop->alltagsL[pop->dreg] == tagL));

			// only care about tagL
			if (_sz == OpSize_32)
			{
				if (matchH || matchL)
				{
					itlast = it;
				}
			}
			else
			{
				if (matchL)
				{
					itlast = it;
				}
			}

/*			bool matchH = (pop->alltagsH[pop->dreg] == tagH) && (tagH >= 0) && (pop->alltagsH[pop->dreg] >= 0);
			bool matchL = (pop->alltagsL[pop->dreg] == tagL) && (tagL >= 0) && (pop->alltagsL[pop->dreg] >= 0);

			// only care about current tag
			if (_sz == OpSize_32)
			{
				if (matchH && matchL)
				{
					itlast = it;
				}
			}
			else
			{
				if (matchL)
				{
					itlast = it;
				}
			}
*/
		}
	}

	return itlast;
}

int Context::register_count_srcrefs(std::list<Operation*>::iterator _start, int _reg, OpSize _sz)
{
	// get tag for register used at point of interest
	Operation *popfirst = *_start;
	int tagH = popfirst->alltagsH[_reg];
	int tagL = popfirst->alltagsL[_reg];
	int refs = 0;

/*	00
	00
	00
	00
	23
	23
	23 <-
	22
	 1
	 1
*/

	// count references to this tag as a source
	for (std::list<Operation*>::iterator it = operations.begin(); it != operations.end(); it++)
	{
		Operation *pop = *it;

		if (_sz == OpSize_32)
		{
			if (pop->opsrc == OpSource_Register)
			{
				bool matchH = (pop->alltagsH[pop->sreg] == tagH) && (tagH >= 0) && (pop->alltagsH[pop->sreg] >= 0);
				bool matchL = (pop->alltagsL[pop->sreg] == tagL) && (tagL >= 0) && (pop->alltagsL[pop->sreg] >= 0);

//				if (matchH && matchL)
//					refs++;
				if (matchH || matchL)
					refs++;
				//if (matchH)
				//	refs++;
				//if (matchL)
				//	refs++;
			}
			if (pop->areg >= 0)
			{
				bool match = (pop->alltagsL[pop->areg] == tagL) && (tagL >= 0) && (pop->alltagsL[pop->areg] >= 0);

				if (match)
					refs++;
			}
			if (pop->opdst == OpSource_Register)
			{
				bool matchH = (pop->alltagsH[pop->dreg] == tagH) && (tagH >= 0) && (pop->alltagsH[pop->dreg] >= 0);
				bool matchL = (pop->alltagsL[pop->dreg] == tagL) && (tagL >= 0) && (pop->alltagsL[pop->dreg] >= 0);

				if ((pop->opwr == OpWrite_Modify) || (pop->opwr == OpWrite_Combine))
				{
					if (matchH || matchL)
						refs++;
					//if (matchH && matchL)
					//	refs++;
				}
			}
		}
		else
		{
			if (pop->opsrc == OpSource_Register)
			{
				bool matchL = (pop->alltagsL[pop->sreg] == tagL) && (tagL >= 0) && (pop->alltagsL[pop->sreg] >= 0);

				if (matchL)
					refs++;
			}
			if (pop->areg >= 0)
			{
				bool match = (pop->alltagsL[pop->areg] == tagL) && (tagL >= 0) && (pop->alltagsL[pop->areg] >= 0);

				if (match)
					refs++;
			}
			if (pop->opdst == OpSource_Register)
			{
				bool matchL = (pop->alltagsL[pop->dreg] == tagL) && (tagL >= 0) && (pop->alltagsL[pop->dreg] >= 0);

				if ((pop->opwr == OpWrite_Modify) || (pop->opwr == OpWrite_Combine) || (pop->opwr == OpWrite_ReplaceHCombineL))
					if (matchL)
						refs++;
			}
		}
	}

	return refs;
}

bool Context::reduce_pass()
{
	reset_sim(true);

	register_sweep();

	bool changed = false;

	if (!g_allargs.quickcut)
		if (g_allargs.verbose)
			printf("load reduction pass: replace redundant register load/stores with immediate stores\n");

	for (std::list<Operation*>::iterator it = operations.begin(); it != operations.end();)
	{
		std::list<Operation*>::iterator curr = it++;
		Operation *pop = *curr;

		int reg = -1;
		int em = -1;

		if (pop->kind == OpKind_StoreR16)
		{
			EM_MoveR16 *pactual = reinterpret_cast<EM_MoveR16*>(pop);
			reg = pactual->sreg;
			em = pactual->em;
		}

		if (pop->kind == OpKind_StoreR32)
		{
			EM_MoveR32 *pactual = reinterpret_cast<EM_MoveR32*>(pop);
			reg = pactual->sreg;
			em = pactual->em;
		}

		if (reg >= 0)
		{
			// this is a 16 or 32bit EM store from a register
			// now check to see if this register is used as a source elsewhere
			int refs = register_count_srcrefs(curr, reg, pop->opsize);
			if (!(registers[reg].locked) && (refs == 1))
			{
//				printf("possible reduce opportunity: reg %d [$%04x%04x]\n", reg, registers[reg].H, registers[reg].L);

				// this register is only referenced as a source once - here
				// now determine *where* the register was written, to see if it was an immediate load
				std::list<Operation*>::iterator itwrite = register_last_written(curr, reg, pop->opsize);
				Operation* pwrite = *itwrite;

				// ensure the write fully accounts for the read, so it may be safely dissolved
				if ((pwrite->kind == OpKind_LoadI32) || 
					((pwrite->kind == OpKind_LoadI16) && (pop->kind == OpKind_StoreR16))
				)
				{
					if (!g_allargs.quickcut)
						if (g_allargs.debug)
							printf("reduce opportunity: reg %d [$%04x%04x]\n", reg, registers[reg].H, registers[reg].L);

					int arold = pwrite->areg;

					// remove load
					operations.erase(itwrite);

					// replace store
					if (pop->kind == OpKind_StoreR16)
					{
						curr = operations.erase(curr);
						curr = operations.insert(curr, new EM_MoveI16(*this, em, registers[reg].L, arold));
						changed = true;
					}
					else
					{
						curr = operations.erase(curr);
						curr = operations.insert(curr, new EM_MoveI32(*this, em, registers[reg].H, registers[reg].L, arold));
						changed = true;
					}
				}
			}
		}

//terminate:

		// catch up with execution from last insert point
		while (curr != it)
		{
			register_sweep();
			(*curr)->execute(); 
			curr++;
		}
	}

	return changed;
}

bool Context::promote_pass()
{
	reset_sim(true);

	register_sweep();

	bool changed = false;

	if (!g_allargs.quickcut)
		if (g_allargs.verbose)
			printf("promotion pass: replace immediate stores with load/store via free register\n");

	for (std::list<Operation*>::iterator it = operations.begin(); it != operations.end() && !changed;)
	{
		std::list<Operation*>::iterator curr = it++;
		Operation *pop = *curr;

		uword_t iH = -1;
		uword_t iL = -1;
		int em = -1;
		int arold = -1;

		if (pop->kind == OpKind_StoreI16)
		{
			EM_MoveI16 *pactual = reinterpret_cast<EM_MoveI16*>(pop);
			iL = pactual->data;
			em = pactual->em;
			arold = pactual->areg;
		}

		if (pop->kind == OpKind_StoreI32)
		{
			EM_MoveI32 *pactual = reinterpret_cast<EM_MoveI32*>(pop);
			iH = pactual->dataH;
			iL = pactual->dataL;
			em = pactual->em;
			arold = pactual->areg;
		}

		if (em >= 0)
		{
			// look for register with no burdens
			int freereg = -1;
			for (int r = 0; r < registers.size(); r++)
			{
				if (!(registers[r].locked) && !(registers[r].variable))
				{
					if (
						(pop->alltagsL[r] < 0) &&
						// incidence of replacement must be higher than incidence of existing
						//(metrics_sim[iL].references > metrics_sim[registers[r].L].references)
						1//(metrics_sim[iL].incidence > metrics_sim[registers[r].L].incidence)
					)
					{
						if (pop->kind == OpKind_StoreI16)
						{
							freereg = r;
							break;
						}
						else
						{
							// don't promote long stores unless upper data is trivial
							if ((pop->alltagsH[r] < 0) && ((iH == 0) || (iH == 0xFFFF)))
							{
								freereg = r;
								break;
							}
						}
					}
				}
			}

			if (freereg >= 0)
			{
				{
					//				printf("possible reduce opportunity: reg %d [$%04x%04x]\n", reg, registers[reg].H, registers[reg].L);

					{
						// replace store
						if (pop->kind == OpKind_StoreI16)
						{
							if (!g_allargs.quickcut)
								if (g_allargs.debug)
									printf("promote opportunity: reg %d [$%04x]\n", freereg, iL);

							curr = operations.erase(curr);
							curr = operations.insert(curr, new EM_MoveR16(*this, em, freereg, arold));
							curr = operations.insert(curr, new DR_LoadI16(*this, freereg, iL));
							changed = true;
						}
						else
						{
							if (!g_allargs.quickcut)
								if (g_allargs.debug)
									printf("promote opportunity: reg %d [$%04x%04x]\n", freereg, iH, iL);

							curr = operations.erase(curr);
							curr = operations.insert(curr, new EM_MoveR32(*this, em, freereg, arold));
							curr = operations.insert(curr, new DR_LoadI32(*this, freereg, iH, iL));
							changed = true;
						}
					}
				}
			}
		}


//terminate:

		// catch up with execution from last insert point
		while (curr != it)
		{
			register_sweep();
			(*curr)->execute(); 
			curr++;
		}
	}

	return changed;
}

bool Context::promote_lea_pass()
{
	reset_sim(true);

	register_sweep();

	bool changed = false;

	if (!g_allargs.quickcut)
		if (g_allargs.verbose)
			printf("promotion pass: replace lea dst skips with add/sub via free register\n");

	for (std::list<Operation*>::iterator it = operations.begin(); it != operations.end() && !changed;)
	{
		std::list<Operation*>::iterator curr = it++;
		Operation *pop = *curr;

		if (pop->kind == OpKind_DSTLea)
		{
			DST_Lea *pactual = reinterpret_cast<DST_Lea*>(pop);
			int delta = pactual->delta(dstlinewid);
			int linejump = pactual->linedelta;
			int wordjump = pactual->worddelta;
			int rold = pactual->dreg;

			// look for register with no burdens
			int freereg = -1;
			for (int r = 0; r < registers.size(); r++)
			{
				if (!(registers[r].locked) && !(registers[r].variable))
				{
					if (pop->alltagsL[r] < 0)
					{
						freereg = r;
						break;
					}
				}
			}

			if (freereg > 0)
			{
				{
					{
						// replace lea
						{
							if (!g_allargs.quickcut)
								if (g_allargs.debug)
									printf("lea promote opportunity: reg %d [$%04x] (x:%d,y:%d)\n", freereg, delta, wordjump, linejump);

							curr = operations.erase(curr);
							curr = operations.insert(curr, new DST_AddaR16(*this, freereg, SCRLINE_reg, wordjump, linejump));
							curr = operations.insert(curr, new DR_LoadI16(*this, freereg, delta));
							changed = true;
						}					
					}
				}
			}
		}


//terminate:

		// catch up with execution from last insert point
		while (curr != it)
		{
			register_sweep();
			(*curr)->execute(); 
			curr++;
		}
	}

	return changed;
}

void Context::tail_pass()
{
	reset_sim(true);
	
	register_sweep();

	if (!g_allargs.quickcut)
		if (g_allargs.verbose)
			printf("tail optimization pass: move BLiT operations to hide long tails...\n");

	// start with last operation, walk backwards using forward iterator in order to fork sub-search
	std::list<Operation*>::iterator it = operations.end(); it--;
	std::list<Operation*>::iterator itstop = operations.begin();

	while (it != itstop)
	{
		std::list<Operation*>::iterator curr = it--;
		Operation *pop = *curr;

		// encountered blit operation
		if (pop->kind == OpKind_BLITGoSkew)
		{
			BLiT_GoSkew *pactual = reinterpret_cast<BLiT_GoSkew*>(pop);

			// self
			//std::list<Operation*>::iterator best_tailop = curr;

			// scan from next op onwards
			std::list<Operation*>::iterator itscan = curr; 
			if (itscan != operations.end())
				itscan++;

			// first op can be a barrier, but should not cause a change if chosen
			std::list<Operation*>::iterator first = itscan;
			std::list<Operation*>::iterator best_tailop = first;
			int best_tailval = (*best_tailop)->tail;

			//int best_tailval = (*itscan)->tail;
			for (; itscan != operations.end(); itscan++)
			{
				Operation *ptryop = *itscan;

				OpKind kind = ptryop->kind;

				int trywords = ptryop->words;

				// special ops with nontrivial size/implementation, we don't reorder across these
				// note: covers most of the specific tests below, but we check them anyway
//				if (trywords <= 0)
//					break;

				// don't reorder blit across modification of d1 (BLiT_GoSkew source register)
				if (ptryop->dreg == BHOG_reg)
					break;

				// observe routine end
				if (kind == OpKind_Terminate)
					break;

				// don't reorder past skew reg changes (in d1) since this is our source for BLITGo
				if (kind == OpKind_SetSkew)
					break;

				// can't reorder blit beyond HW register stores
				if (kind == OpKind_StoreI16)
					break;
				if (kind == OpKind_StoreI32)
					break;
				if (kind == OpKind_StoreR16)
					break;
				if (kind == OpKind_StoreR32)
					break;
				if (kind == OpKind_StoreMPI16)
					break;
				if (kind == OpKind_StoreMPI32)
					break;

				if (kind == OpKind_BLITInit)
					break;
				if (kind == OpKind_BLITXCMod)
					break;
				if (kind == OpKind_BLITSYIMod)
					break;
				if (kind == OpKind_BLITDYIMod)
					break;
				if (kind == OpKind_BLITSetup)
					break;
				if (kind == OpKind_BLITGo)
					break;
				if (kind == OpKind_BLITGoSkew)
					break;
				if (kind == OpKind_SRCSkip)
					break;
				if (kind == OpKind_SYSPageRestore)
					break;

				//printf("kind: %s\n", as_str[kind]);

				int trytail = ptryop->tail;

				if (
					((trywords == 1) && (trytail > best_tailval)) 
				)
				{
					best_tailval = trytail;
					best_tailop = itscan;
				}
			}

			// if we found a good position and it is not the first position, move the blit
			if ((best_tailop != curr) && 
				(best_tailop != first))
			{
				if (g_allargs.debug)
				{
					printf("relocating blit to hide tail=%d cycles [op:%s]\n", 
						best_tailval, 
						(*best_tailop)->describe().c_str()
					);
				}
				operations.erase(curr);
				operations.insert(best_tailop, pop);
			}
		}
	};

//terminate:;
}

void run_sim
(
	Context &context, 
	ubyte_t *livelines, 
	int _frame, 
	int pi, preshift_t &shift, 
	std::vector<int> &linemap_src_2_opt,
	bool _generate
)
{
	context.reset_sim(false);
	for (std::list<Operation*>::iterator it = context.operations.begin(); it != context.operations.end(); it++)
	{
		Operation *poper = *it;

		poper->execute();
		if (_generate)
			poper->emit();

#if (DEBUG_REGISTER_STATES)
		printf("tags:");
		for (int r = 0; r < context.registers.size() && r < 6; r++)
		{
			printf(" %s[%02x:%02x](%08x)", 
				context.registers[r].name.c_str(), 
				ubyte_t(poper->alltagsH[r]), ubyte_t(poper->alltagsL[r]),
				(((ulong_t)context.registers[r].H << 16) | context.registers[r].L)
			);
		}
		printf("  %s", poper->describe().c_str());
#endif
	}

//	context.dump_tags();

	// debug the blit
	//		if (g_allargs.debug)
	{
		if (!g_allargs.quickcut)
			if (g_allargs.debug)
				printf("debug simulation... %dx%d words (%dx%d buffer)\n", 
					context.srcww, context.srch,
					context.dstww, context.srch
				);

		int errors = 0;
		for (int v = 0; v < context.srch; v++)
		{
			// detect empty lines
			//uword_t m = 0;
			//for (int h = 0; h < shift.wordwidth_; h++)
			//	m |= shift.data_[(v * shift.wordwidth_) + h];

			//ubyte_t m = livelines[v];

			//for (int h = 0; h < shift.wordwidth_; h++)
			for (int h = 0; h < shift.wordwidth_; h++)
			{
				uword_t data = context.memory[(v * context.dstww) + h];
				uword_t expected = shift.data_[(v * shift.wordwidth_) + h];
				bool error = false;

				if (/*m &&*/
					(data != expected) &&
					// tolerate unwritten words for empty source
					!((data == 0xaaaa) && (expected == 0))
					)
				{
					errors++;
					error = true;
				}

				if (!g_allargs.quickcut)
					if (g_allargs.debug)
					{
						//if (m)
						{
							print_dataword(expected); printf(":"); print_dataword(data); 
							if (error)
								printf("< ");
							else
								printf("  ");
						}
						/*else
						{
							print_dataword(expected); printf(":----------------  ");
						}*/
					}
			}
			if (!g_allargs.quickcut)
				if (g_allargs.debug)
					printf(" optsrc=%d\n", linemap_src_2_opt[v]);
		}

		if (errors > 0)
		{
			std::cerr << "*************************************************" << std::endl;
			std::cerr << "error: simulation failed on frame " << _frame << " preshift " << pi << std::endl;
			std::cerr << "this is a bug in agtcut! contact the dml with your datas..." << std::endl;
			std::cerr << "quickfix: play with -emxopt <level>, or switch to EMS format for this file" << std::endl;
			printf("press any key...\n");
			getchar();
			exit(1);
		}
	}
}

static int word_le(uword_t w)
{
	int le = 0;
	while ((w & 0x8000) == 0)
	{
		w <<= 1;
		le++;
	}
	return le;
}

static int word_re(uword_t w)
{
	int re = 0;
	while ((w & 0x8000) != 0)
	{
		w <<= 1;
		re++;
	}
	return re;
}

int Context::cycle_sum(Context::program_metrics &r_program_metrics)
{
	int words = 0;
	int cycles = 0;
	int spans = 0;
	int em_mods = 0;
	int em_modcycles = 0;
	int src_skips = 0;
	int src_skipcycles = 0;
	int dst_skips = 0;
	int dst_skipcycles = 0;
	int hidden_tailcycles = 0;
	int group_mods = 0;
	int group_modcycles = 0;
	int skew_mods = 0;
	int skew_modcycles = 0;
	int blit_expbuscycles = 0;
	int blit_simbuscycles = 0;
	int main_cycles = 0;

	int last_tail = 0;

	int lastoper_isgroupmod = 0;

	for (std::list<Operation*>::iterator it = operations.begin(); it != operations.end(); it++)
	{
		Operation *poper = *it;

		// don't count the cost of non-emitted setup, used only by the sim
		if (poper->hidden)
		{
			//poper->onEmit();
			//printf("hidden: %s\n", buf);

			continue;
		}

		if ((poper->kind == OpKind_BLITXCMod) || 
			(poper->kind == OpKind_BLITDYIMod) || 
			(poper->kind == OpKind_BLITSYIMod))
		{
			if (!lastoper_isgroupmod)
				group_mods++;
			lastoper_isgroupmod = 1;

			group_modcycles += poper->cost;
		}
		else
		{
			lastoper_isgroupmod = 0;

			if (poper->kind == OpKind_SRCSkip)
			{
				src_skips++;
				src_skipcycles += poper->cost;
			}
			else
			if ((poper->kind == OpKind_DSTLea) || 
				(poper->kind == OpKind_DSTAddaIL))
			{
				dst_skips++;
				dst_skipcycles += poper->cost - 8;

				// all cycles from span-constant operations (every line has a dstaddr update cost of 8+ cycles)
				main_cycles += poper->cost - 8;
			}
			else
			if ((poper->kind == OpKind_StoreI16) || 
				(poper->kind == OpKind_StoreI32) ||
				(poper->kind == OpKind_StoreR16) ||
				(poper->kind == OpKind_StoreR32) ||
				(poper->kind == OpKind_StoreMPI16) ||
				(poper->kind == OpKind_StoreMPI32))
			{
				em_mods++;
				em_modcycles += poper->cost;
			}
			else
			if (poper->kind == OpKind_SetSkew)
			{
				skew_mods++;
				skew_modcycles += poper->cost;
			}
			else
			if (poper->kind == OpKind_BLITGoSkew)
			{
				spans++;
				// includes cost for 4 bitplanes
				blit_expbuscycles += reinterpret_cast<BLiT_GoSkew*>(poper)->buscycles;
				blit_simbuscycles += reinterpret_cast<BLiT_GoSkew*>(poper)->sim_buscycles;
				// all cycles from span-constant operations
				main_cycles += poper->cost;
			}
			else
			{
				// all cycles from span-constant operations
				main_cycles += poper->cost;
			}
		}

		// sum cycles
		cycles += poper->cost;

		// sum words
		words += poper->words;

		// only interested in tails overlapping blitter starts
		int blitter_tail = (poper->kind == OpKind_BLITGoSkew) ? last_tail : 0;

		// only interested in tail time for 1-word ops
		last_tail = (poper->words == 1) ? poper->tail : 0;

		// account for tails
		cycles -= blitter_tail;
		hidden_tailcycles += blitter_tail;
	}

	r_program_metrics.words = words;
	r_program_metrics.cycles = cycles;
	r_program_metrics.spans = spans;
	r_program_metrics.em_mods = em_mods;
	r_program_metrics.em_modcycles = em_modcycles;
	r_program_metrics.src_skips = src_skips;
	r_program_metrics.src_skipcycles = src_skipcycles;
	r_program_metrics.dst_skips = dst_skips;
	r_program_metrics.dst_skipcycles = dst_skipcycles;
	r_program_metrics.hidden_tailcycles = hidden_tailcycles;
	r_program_metrics.group_mods = group_mods;
	r_program_metrics.group_modcycles = group_modcycles;
	r_program_metrics.skew_mods = skew_mods;
	r_program_metrics.skew_modcycles = skew_modcycles;
	r_program_metrics.blit_expbuscycles = blit_expbuscycles;
	r_program_metrics.blit_simbuscycles = blit_simbuscycles;
	r_program_metrics.main_cycles = main_cycles;

	return cycles;
}

//---------------------------------------------------------------------------------------------------------------------

void generate_mprog_emdata_metrics
(
	Context &context, 
	const std::vector<constrained_wordspan*> &stacked_spans,
	ubyte_t *changes,
	uword_t *buffer,
	std::vector<constrained_wordspan*> &active_spans
)
{
	if (!g_allargs.quickcut)
		if (g_allargs.verbose)
			printf("\nsurviving datawords...\n");

	// reset dirty EM registers for this blit width
	//em_dirty[0] = true;
	//em_dirty[1] = (shift.wordwidth_ > 2);
	//em_dirty[2] = (shift.wordwidth_ > 1);

	//		int unique_datawords = 0; // each unique dataword has a uid so it can be reused
	{
		for (auto pspan : stacked_spans)
		{
			int v = pspan->draw_order;
			int dsty = v;

			uword_t *srcdata = &buffer[dsty * 3];
			ubyte_t *linechanges = &changes[dsty * 3];
			// generate metrics for nonempty, unique lines only - lines where 1 or more EM registers must change
			if (linechanges[0] || linechanges[1] || linechanges[2])
			{
				// gather metrics for datawords on this line
				bool line_changed = false;
				for (int x = 0; x < 3; x++)
				{
					if (!linechanges[x])
					{
						if (!g_allargs.quickcut)
							if (g_allargs.debug)
								printf("----------------");
						continue;
					}

					line_changed = true;

					uword_t dataword = srcdata[x];
					emx_metric_t &m = context.metrics[dataword];

					if (!g_allargs.quickcut)
						if (g_allargs.debug)
							print_dataword(dataword);

					//if (m.name < 0)
					//	m.name = unique_datawords++;

					// incidence of this word
					m.incidence++;
					m.references++;

					//// incidence at each EM position
					//if (x == 0)
					//	m.incidence_em0++;
					//else
					//	if (x == 1)
					//		m.incidence_em1++;
					//	else
					//		if (x == 2)
					//			m.incidence_em2++;

					// incidence of this word relative to other words
					for (int lna = 0; lna < 3; lna++)
					{
						if ((x != lna) && (linechanges[lna]))
						{
							uword_t lnaword = srcdata[lna];

							// relative to all other words referenced on same line
							m.line_adjacencies[lnaword]++;

							// relative to other words used immediately to the right
							if (x == lna - 1)
							{
								m.left_adjacencies[lnaword]++;
								m.left_references[lnaword]++;
							}
						}
					}
				}

				// track this as a live line which changes
				if (line_changed)
					active_spans.push_back(pspan);
				//	active_lines.push_back(lineout(v, dsty));

				if (!g_allargs.quickcut)
					if (g_allargs.debug)
						printf("\n");
			}
		}
	}

	if (!g_allargs.quickcut)
		if (g_allargs.verbose)
			printf("total surviving = %d\n", context.metrics.size());
}

/*
void generate_emx2_map_span_sources
(
	spriteframe_t &spr,
	std::vector<constrained_wordspan*> &stacked_spans,
	int _shift
)
{
	int full_src_wordwidth = ((spr.w_ + 15) & -16) >> 4;

	for (auto pspan : stacked_spans)
	{
		int sww = pspan->ww;
		int sws = pspan->ws;

		if (_shift > 0)
			sww++;

		if (pspan->cxs)
		{
			sww++;
			sws--;
		}

		if (pspan->cxe)
			sww--;

		pspan->sww = sww;
		pspan->sws = sws;

		pspan->span_src_wos = (full_src_wordwidth * pspan->line_opt) + pspan->sws;
	}
}
*/

//---------------------------------------------------------------------------------------------------------------------

void generate_mprog_extract_deltas
(
	std::vector<constrained_wordspan*> &stacked_spans, 
	ubyte_t *changes, 
	uword_t *buffer
)
{
	// generate map for only changing EM registers 
	// (excluding first output line - this is handled as dirty registers to minimize initial setup if the first line is empty)

	if (USE_DELTA_OPTIMIZATION)
	{
		uword_t lastchanges[3] = { 0,0,0 };
		//bool dirtyline = true;

		//printf("shift wordwidth: %d", shift.wordwidth_);

		for (auto pspan : stacked_spans)
		{
			int v = pspan->draw_order;
			int v3 = v * 3;

			if (lastchanges[0] != buffer[v3 + 0])
			{
				lastchanges[0] = buffer[v3 + 0];

				changes[v3 + 0] = 1;
			}

			if (pspan->ww >= 3)
			{
				if (lastchanges[1] != buffer[v3 + 1])
				{
					lastchanges[1] = buffer[v3 + 1];

					changes[v3 + 1] = 1;
				}
			}

			if (pspan->ww >= 2)
			{
				if (lastchanges[2] != buffer[v3 + 2])
				{
					lastchanges[2] = buffer[v3 + 2];

					changes[v3 + 2] = 1;
				}
			}
			/*
			//int dsty = v;
			//int dsty = linemap_opt_2_src[v];

			// generate deltas for all but 1st BUSY line
			int yn3 = v * 3;
			int yp3 = yn3;
			if (v > 0)
				yp3 = (v - 1) * 3;// linemap_opt_2_src[v - 1] * 3;

			// generate deltas for only real lines
			{
				if (1)
				{
					changes[yn3 + 0] = (dirtyline || (lastchanges[0] != buffer[yn3 + 0])) ? 1 : 0;
					lastchanges[0] = buffer[yn3 + 0];
				}

				if (pspan->ww >= 3)
				{
					changes[yn3 + 1] = (dirtyline || (lastchanges[1] != buffer[yn3 + 1])) ? 1 : 0;
					lastchanges[1] = buffer[yn3 + 1];
				}
				if (pspan->ww >= 2)
				{
					changes[yn3 + 2] = (dirtyline || (lastchanges[2] != buffer[yn3 + 2])) ? 1 : 0;
					lastchanges[2] = buffer[yn3 + 2];
				}

				dirtyline = false;
			}
*/
		}
	}
	else
	{
		int v = -1;
		for (auto pspan : stacked_spans)
		{
			v++;
			int dsty = v;
			//int dsty = linemap_opt_2_src[v];

			//if (component_livelines[dsty])
			{
				int dsty3 = dsty * 3;
				changes[dsty3 + 0] = true;
				changes[dsty3 + 1] = (pspan->ww >= 3);
				changes[dsty3 + 2] = (pspan->ww >= 2);
			}
		}
	}


	// by this point, changes[y:x] marks all dirty EM registers

	//bool em_dirty[3];

	if (!g_allargs.quickcut)
		if (g_allargs.verbose)
			printf("deltas...\n");

	// reset dirty EM registers for this blit width
	//em_dirty[0] = true;
	//em_dirty[1] = (shift.wordwidth_ > 2);
	//em_dirty[2] = (shift.wordwidth_ > 1);

	//			for (int v = 0; v < shift.height_; v++)
	//			{
	//				int dsty3 = linemap_opt_2_src[v] * 3;

	int v = -1;
	for (auto pspan : stacked_spans)
	{
		v++;
		int dsty3 = v * 3;

		if (changes[dsty3 + 0]/* || em_dirty[0]*/)
		{
			//em_dirty[0] = false;
			if (!g_allargs.quickcut)
				if (g_allargs.verbose)
					print_dataword(buffer[dsty3 + 0]);
		}
		else
		{
			if (!g_allargs.quickcut)
				if (g_allargs.verbose)
				{
					if (buffer[dsty3 + 0] == 0)
						printf("////////////////");
					else
						printf("................");
				}
		}

		if (changes[dsty3 + 1]/* || em_dirty[1]*/)
		{
			//em_dirty[1] = false;
			if (!g_allargs.quickcut)
				if (g_allargs.verbose)
					print_dataword(buffer[dsty3 + 1]);
		}
		else
		{
			if (!g_allargs.quickcut)
				if (g_allargs.verbose)
				{
					if (buffer[dsty3 + 1] == 0)
						printf("////////////////");
					else
						printf("................");
				}
		}


		if (changes[dsty3 + 2]/* || em_dirty[2]*/)
		{
			//em_dirty[2] = false;
			if (!g_allargs.quickcut)
				if (g_allargs.verbose)
					print_dataword(buffer[dsty3 + 2]);
		}
		else
		{
			if (!g_allargs.quickcut)
				if (g_allargs.verbose)
				{
					if (buffer[dsty3 + 2] == 0)
						printf("////////////////");
					else
						printf("................");
				}
		}

		if (!g_allargs.quickcut)
			if (g_allargs.verbose)
				printf("\n");
	}
}

//---------------------------------------------------------------------------------------------------------------------

class storedependency
{
public:
	storedependency(int _name, int _pos, int _job, int _type, uword_t _H, uword_t _L)
		: name(_name), pos(_pos), job(_job), type(_type), H(_H), L(_L)
	{
	}

	int name;
	int pos;
	int job;
	int type;
	uword_t H;
	uword_t L;
};


void generate_mprog_store_dependencies
(
	Context &context, 
	ubyte_t *changes,
	uword_t *buffer,
	int assigned_registers,
	const std::vector<constrained_wordspan*> &active_spans,
	std::vector<storedependency>& deps, 
	std::vector<u64>& lifespans
)
{
	// todo: determine max regs available for final code
	int max_regs = 4;

	// 1)
	// put each piece of data in a new register in order of need, assuming an infinute number of free 
	// registers (to begin with).
	// note that in worst case, several changes may involve the same data at spaced intervals desite
	// minimization of deltas - because multiple words may change at once in any line. 
	// while such data probably can survive in a register throughout its needed lifespan, it may need 
	// to be reallocated if registers become exhausted - so we do a full register rename and reduce.

	// reset dirty EM registers for this blit width
	//em_dirty[0] = true;
	//em_dirty[1] = (shift.wordwidth_ > 2);
	//em_dirty[2] = (shift.wordwidth_ > 1);

	std::vector<int> blits;

	int stage = 0;
	//		int regcount = 0;
	u64 lifebits = 0;
	int job = 0;
	//			for (std::vector<lineout>::iterator lit = active_lines.begin(); lit != active_lines.end(); lit++, job++)
	for (auto pspan : active_spans)
	{
		//				lineout &lo = *lit;
		//				int dsty = lo.dline;
		int dsty = pspan->draw_order;

		// only live lines in active_lines table

		uword_t *srcdata = &buffer[dsty * 3];
		ubyte_t *linechanges = &changes[dsty * 3];

		blits.push_back(deps.size());

		for (int x = 0; x < 3; x++)
		{
			if (!(linechanges[x]/*||em_dirty[x]*/))
				continue;

			uword_t dataword = srcdata[x];

			emx_metric_t &m = context.metrics[dataword];

			// resource type: 0 = reused (register), 1 = single-use (#immediate/transform)
			//				int regname = -1;
			int type = 1;
			if (m.incidence > 1)
			{
				type = 0;
				//					regname = regcount++;
			}

			// todo: load H word with best candidate for each L data word (if any)
			deps.push_back(storedependency(m.name, stage, job, type, 0, dataword));
			if (m.name >= 0)
				lifebits |= (u64(1) << m.name);
			lifespans.push_back(lifebits);
			stage++;

			//em_dirty[x] = false;
		}
	}

	// 2)
	// trim lifespans for each piece of data
	u64 cancelbits = 0;
	std::vector<u64>::reverse_iterator lit = lifespans.rbegin();
	for (std::vector<storedependency>::reverse_iterator rit = deps.rbegin(); rit != deps.rend(); rit++, lit++)
	{
		storedependency &dr = *rit;
		u64 &lifebits = *lit;

		if (dr.name >= 0)
		{
			// register name for this data
			u64 activebit = u64(1) << dr.name;

			// prevent cancelling of this data's lifespan from here backwards
			cancelbits |= activebit;
		}

		// cancel lifespans for data not yet encountered
		lifebits &= cancelbits;
	}

	if (!g_allargs.quickcut)
		if (g_allargs.debug)
			printf("\n\ninitial data lifespans:\n");
	//for (std::vector<storedependency>::iterator rit = deps.begin(); rit != deps.end(); rit++)
	//{
	//	storedependency &dr = *rit;
	//	if (dr.type == 0)
	//		printf("R");
	//	else
	//	if (dr.type == 1)
	//		printf("I");
	//}
	//printf("\n");

	// report lifespan status for each state for each execution stage
	if (!g_allargs.quickcut)
		if (g_allargs.debug)
		{
			std::vector<storedependency>::iterator dit = deps.begin();
			for (std::vector<u64>::iterator lit = lifespans.begin(); lit != lifespans.end(); lit++, dit++)
			{
				assert(dit != deps.end());
				print_data(*lit, /*unique_datawords*/assigned_registers); printf(" %s\n", (dit->type == 0) ? "R" : "U");
			}
		}
}

//---------------------------------------------------------------------------------------------------------------------

int generate_mprog_emdata_scoring(Context &context)
{
	if (!g_allargs.quickcut)
		if (g_allargs.debug)
			printf("\nscoring:\n");

	// globally score each data word according to metrics
	int assigned_registers = 0;
	for (metricmap::iterator mit = context.metrics.begin(); mit != context.metrics.end(); mit++)
	{
		const uword_t &dataword = mit->first;

		if (!g_allargs.quickcut)
			if (g_allargs.debug)
				print_dataword(dataword);

		emx_metric_t &m = mit->second;

		if ((m.name < 0) && (m.incidence > 1))
		{
			m.name = assigned_registers++;
		}

		float fscore = log(1.0 + (float)m.incidence);

		float sharing = 0.0f;
		for (relationtable::iterator rit = m.line_adjacencies.begin(); rit != m.line_adjacencies.end(); rit++)
			sharing += (float)rit->second;

		// score never modulates to 0 - can only increase with sharing
		fscore *= (1.0f + log(1.0 + sharing));

		m.fincidence_score = fscore;

		if (!g_allargs.quickcut)
			if (g_allargs.debug)
				printf(" incidence_score: %.2f\n", fscore);
	}

	// globally score each data word according to its left-adjacency (upper word opportuntiy)
	for (metricmap::iterator mit = context.metrics.begin(); mit != context.metrics.end(); mit++)
	{
		const uword_t &dataword = mit->first;

		if (!g_allargs.quickcut)
			if (g_allargs.debug)
				print_dataword(dataword);

		emx_metric_t &m = mit->second;

		float fscore = m.fincidence_score;

		float fadj = 0.0f;
		for (relationtable::iterator rit = m.left_adjacencies.begin(); rit != m.left_adjacencies.end(); rit++)
			fadj += (float)rit->second;

		// score can modulate to 0 if this dataword never appears on the left of any other
		fscore *= (0.0f + log(1.0 + fadj));

		m.fadjacency_score = fscore;

		if (!g_allargs.quickcut)
			if (g_allargs.debug)
				printf(" adjacency_score: %.2f\n", fscore);
	}

	// find the most useful values

	typedef std::map<float, std::list<metricmap::iterator> > usemap;

	usemap usemap_lw;
	usemap usemap_hw;

	for (metricmap::iterator mit = context.metrics.begin(); mit != context.metrics.end(); mit++)
	{
		usemap_lw[mit->second.fincidence_score].push_back(mit);
		usemap_hw[mit->second.fadjacency_score].push_back(mit);
	}

	for (usemap::reverse_iterator uit = usemap_lw.rbegin(); uit != usemap_lw.rend(); uit++)
	{
		for (std::list<metricmap::iterator>::iterator lit = uit->second.begin(); lit != uit->second.end(); lit++)
		{
			const uword_t &dataword = (*lit)->first;

			emx_metric_t &m = context.metrics[dataword];

			if (m.incidence >= 2)
			{
				if (!g_allargs.quickcut)
					if (g_allargs.debug)
						printf("L preload: [$%04x] iscore: %.2f\n", dataword, uit->first);
				context.preloads.push_back(preload(dataword));
			}
		}
	}

	for (usemap::reverse_iterator uit = usemap_hw.rbegin(); uit != usemap_hw.rend(); uit++)
	{
		float h_score = uit->first;

		for (std::list<metricmap::iterator>::iterator lit = uit->second.begin(); lit != uit->second.end(); lit++)
		{
			const uword_t &dataword = (*lit)->first;

			// metrics for this word relative to others
			emx_metric_t &m = context.metrics[dataword];

			//				if (m.incidence > 1)
			{
				if (!g_allargs.quickcut)
					if (g_allargs.debug)
						printf("H preload: [$%04x] ascore: %.2f\n", dataword, h_score);


				// sort adjacencies with indirection

				typedef std::map<int, std::list<relationtable::iterator> > hitmap;
				hitmap hits;

				for (relationtable::iterator relit = m.left_adjacencies.begin(); relit != m.left_adjacencies.end(); relit++)
				{
					int h = relit->second;
					hits[h].push_back(relit);
				}

				// scan through the most promising adjacents
				for (hitmap::reverse_iterator hit = hits.rbegin(); hit != hits.rend(); hit++)
				{
					std::list<relationtable::iterator> &competing = hit->second;

					while (!competing.empty())
					{
						relationtable::iterator relit = competing.back();
						competing.pop_back();

						uword_t adjacent_low = relit->first;

						if (!g_allargs.quickcut)
							if (g_allargs.debug)
								printf("candidate adjacent low: [$%04x] hits: %d\n", adjacent_low, relit->second);

						// try to locate it in the existing pool of L preloads
						// if we find a match, bind it and break

						for (std::vector<preload>::iterator pit = context.preloads.begin(); pit != context.preloads.end(); pit++)
						{
							if (pit->validL && (pit->L == adjacent_low))
							{
								pit->H = dataword;
								pit->validH = true;

								if (!g_allargs.quickcut)
									if (g_allargs.debug)
										printf("bound preload [$%04x]:[$%04x]\n", dataword, adjacent_low);

								goto break_bind;
							}
						}
					}
				}
			break_bind:;
			}
		}
	}

	return assigned_registers;
}

//---------------------------------------------------------------------------------------------------------------------

void generate_emx_emh_metaprogram
(
	spriteframe_t &spr, int _frame, 
	preshift_t &shift, int pi, 
	std::vector<constrained_wordspan*> stacked_spans,
	bool _shared_mask,
	std::vector<u16> &shared_mask_words
)
{
	// generate metrics for each data word
					
	size_t spancount = stacked_spans.size();

	if (g_allargs.verbose)
	{
		printf("\n\nframe %d spans %d preview:\n", _frame, pi);
		for (auto pspan : stacked_spans)
		{
			print_dataword(pspan->em[0]);
			print_dataword(pspan->em[1]);
			print_dataword(pspan->em[2]);
			printf("\n");
		}
		printf("\n");
	}

	Context context
	(
		/*masksrcww=*/shift.wordwidth_, 
		/*masksrch=*/shift.height_, 
		/*srccol_wordwidth*/((spr.w_ + 15) >> 4), 
		/*simdstww=*/shift.wordwidth_, 
		/*guardxs=*/g_allargs.emguardx,
		_shared_mask
	);

	context.shared_mask_words = shift.shared_mask_words;


	ubyte_t *changes = new ubyte_t[3 * spancount/*shift.height_*/];
	memset(changes, 0, 3 * spancount/*shift.height_*/);

	// make copy of data in terms of 3*y EM words
	uword_t *buffer = new uword_t[3 * spancount/*shift.height_*/];
	memset(buffer, 0, 2 * 3 * spancount/*shift.height_*/);
	{
		for (auto pspan : stacked_spans)
		{
			int v = pspan->draw_order;
			buffer[(v * 3) + 0] = pspan->em[0];
			buffer[(v * 3) + 1] = pspan->em[1];
			buffer[(v * 3) + 2] = pspan->em[2];
		}
	}

	ubyte_t *component_livelines = nullptr;// new ubyte_t[shift.height_];
	//memset(component_livelines, 0, shift.height_);

	// find nonempty lines
/*	for (int y = 0; y < shift.height_; y++)
	{
		int dsty = spr.linemap_src_2_opt[y];

		if (USE_EMPTYLINE_OPTIMIZATION)
		{
			uword_t *srcdata = &buffer[dsty * 3];

			// look for empty blits (all EMs == 0)
			u64 line = 0;
			for (int x = 0; x < shift.wordwidth_; x++)
			{
				line <<= 16;
				line |= srcdata[x];
			}

			component_livelines[dsty] = (line != 0);
		}
		else
		{
			component_livelines[dsty] = 1;
		}
	}
*/
	generate_mprog_extract_deltas(stacked_spans, changes, buffer);


/*

	class lineout
	{
	public:

		lineout(int s, int d)
			: sline(s), dline(d)
		{
		}

		int sline;
		int dline;
	};

	std::vector<lineout> active_lines;
*/

	std::vector<constrained_wordspan*> active_spans;
	generate_mprog_emdata_metrics(context, stacked_spans, changes, buffer, active_spans);

	int assigned_registers = generate_mprog_emdata_scoring(context);


	// preload most useful values

	//context

	// data dependencies
	// process all *changing words* on *changing lines* in *optimized line order*


	std::vector<storedependency> deps;
	std::vector<u64> lifespans;

	generate_mprog_store_dependencies
	(
		context, 
		changes,
		buffer,
		assigned_registers,
		active_spans,
		deps, 
		lifespans
	);



#if 0

	// look for ways to derive unique values without loads, by transforming values with expired lifespan
			
	//for (std::vector<storedependency>::iterator dit = deps.begin(); dit != deps.end(); dit++)
	//{
	//	storedependency &dep = *dit;

	//	if (dep.type == 1)
	//	{
	//		// this dependency is isolated and expensive to load, so look for different ways to derive it from spent values
	//	}
	//}

	//for (std::vector<storedependency>::iterator dit = deps.begin(); dit != deps.end(); dit++)
	//{
	//	storedependency &dep = *dit;

	//	if (dep.type == 0)
	//	{
	//		// this value gets reused so place it in registers


	//	}
	//}


	// 3)
	// optimize assignments to fit available resources
	// - finish with no more than max_registers unique names
	// - avoid interrupting long lifespans (or lifespans of high incidence data)
	// - don't mess with immediate types
	// - minimize number of load events



	//std::vector<int>::reverse_iterator lit = lifespans.rbegin();
	//for (std::vector<dataregister>::reverse_iterator rit = regs.rbegin(); rit != regs.rend(); rit++, lit++)
	//{
	//	dataregister &dr = *rit;
	//	int &lifebits = *lit;

	//	// register name for this data
	//	int activebit = 1 << dr.name;

	//	// prevent cancelling of this data's lifespan from here backwards
	//	cancelbits |= activebit;

	//	// cancel lifespans for data not yet encountered
	//	lifebits &= cancelbits;
	//}

	// todo: enable dynamic NFSR with single or final component

	// SKEW + NFSR, DYINC
//		context.blitter.skew = (shift.shift_ & 15) | (((shift.wordwidth_ << 4) == ((spr.w_ + 15) & -16)) ? 0x00 : 0x40);
#endif


	// (8 + 2) - (shift.wordwidth_ << 3);
#if 0
	// gather metrics on LEA skip values, for loading into registers
	// must be done before sim reset!
	if (USE_DSTSKIP_REG_OPTIMIZATION)
	{
		int current_line = 0;

		for (int y = 0; y < shift.height_; y++)
		{
			int dsty = linemap_opt_2_src[y];

			// is the blit going to output something?
			if (component_livelines[dsty])
			{
				// line hop +/-
				int linejump = dsty - current_line;

				// all non-empty blits require a dst address update
				// todo: linewidth should be an argument
				if (linejump)
				{
					int addressjump = (linejump * context.dstlinewid);
					if (addressjump < 32768 && addressjump >= -32768)
					{
						// caution: don't abs() -32768 case as it has will have no 16bit representation
						int reduced_dataword = addressjump;
						if (reduced_dataword > -32768)
							reduced_dataword = abs(reduced_dataword);
									
						uword_t metrics_dataword = (uword_t)reduced_dataword;

						context.metrics[metrics_dataword].incidence++;
						context.metrics[metrics_dataword].references++;
					}
				}

				current_line = dsty;
			}
		}
	}
#endif

	// analyse dst skips

	// analyse src skips

	// 
	bool allow_interline_cpu_statechanges = !_shared_mask;

	for (int mode = 0; mode < 2; mode++)
	{
		bool analyse_constants = (mode == 0);
		bool build_metaprogram = (mode == 1);

		//__debugbreak();
		context.reset_sim(true);

		if (build_metaprogram)
		{
			shift.scan_states.clear();
			shift.scan_states.resize(spr.h_);
		}

		// reset dirty EM registers for this blit width
		//em_dirty[0] = true;
		//em_dirty[1] = (shift.wordwidth_ > 2);
		//em_dirty[2] = (shift.wordwidth_ > 1);

		bool NFSR = false;
		//(shift.wordwidth_ > 1) && // NFSR is meaningless for wordwidth==1
		//(shift.wordsremaining_ == 0) &&
		//(((spr.w_ + 15) >> 4) != (shift.wordwidth_ + shift.wordoffset_));

		bool FISR = false;// (shift.wordoffset_ > 0);

		// EMH: suppress shift field, loaded by harness (init=shift)
		// EMX: encode shift field, loaded by first generated scan (init=0)
		u16 current_skew = _shared_mask ? (shift.shift_ & 15) : 0;

		/*			u8 current_skew =
						//false;// ((shift.wordwidth_ << 4) == ((spr.w_ + 15) & -16));
						(shift.shift_ & 15) |
						(NFSR ? 0x40 : 0x00) |
						(FISR ? 0x80 : 0x00);
		*/
		s16 current_skew_syinc_adj =
			(NFSR ? 1 : 0) -
			(FISR ? 1 : 0);

		s16 current_skew_syoff_adj =
			0 -
			(FISR ? 1 : 0);

		// SYINC = difference between source wordwidth & preshift wordwidth, + 2
		// note: compensate for NFSR!
		s16 current_syinc =
			(((((spr.w_ + 15) >> 4) - shift.wordwidth_) << 1) + 2)
			- (current_skew_syinc_adj * 2);
		//				+ (NFSR ? 2 : 0)
		//				- (FISR ? 2 : 0);

		s16 current_dyinc = (8 + 2) - (shift.wordwidth_ << 3);

		context.blitter.skew = current_skew;
		context.blitter.syinc = current_syinc;
		context.blitter.dyinc = current_dyinc;

		// generate operations
		int current_dline = 0;
		int current_dline_wordoffset = 0;
		int current_sline = 0;
		int current_sline_wordoffset = 0;
		int current_ww = context.blitter.xcount;
		int current_ws = 0;
		int current_src = 0;
		//int current_mprog_words = 0;

		int reg_index = 0;
		int reg_index2 = 0;
		int reg_index3 = 0;
		{

			if (build_metaprogram)
			{

#if (USE_NOP_BINARY_VALIDATION)
				context.operations.push_back(new NoOp(context));
				context.operations.back()->execute();
#endif

				//context.operations.push_back(new SYS_PageRestore(context));
				//context.operations.back()->execute();

				context.operations.push_back(new BLiT_Defs(context));
				context.operations.back()->execute();

				// emitting init in shared mode causes program offset verify to fail
				if (!_shared_mask)
				{
					context.operations.push_back(new BLiT_Init(context));
					context.operations.back()->execute();
				}

				// load fixed registers
				{
					for (std::vector<dataregister>::iterator it = context.registers.begin(); it != context.registers.end(); it++)
					{
						if (it->constant)
						{
							// use standard immediate load - let the optimizer reduce it if possible
							auto op = new DR_LoadI32(context, it->index, it->H, it->L);
							// we don't emit these as part of codegen, but assume they are prepared by the caller
							op->hidden = _shared_mask || it->external;

							context.operations.push_back(op);
							it->constant = false;

							it->allocatedH = true;
							it->allocatedL = true;

							context.operations.back()->execute();
						}
					}

					// on next reset, registers will no longer be constant/preallocated
					context.setconstants = false;
				}
			}


			//				int src_lines_skipped = 0;

			//				int curr_line_words = shift.wordwidth_;

			//				linemap lines;
			//				for (int y = 0; y < shift.height_; y++)
			//				{
			//					int dsty = linemap_opt_2_src[y];

			int last_y = -1;
			int firstspan = 1;

			for (auto pspan : stacked_spans)
			{
				//__debugbreak();

				int span_draw_order = pspan->draw_order;
				int span_orig_sline = pspan->line_orig;
				int span_source_sline = pspan->line_opt;
				int span_dline = span_orig_sline;
				int span_syi_change = 0;

				int dsty = span_draw_order;
				uword_t *srcdata = &buffer[dsty * 3];
				ubyte_t *linechanges = &changes[dsty * 3];


				bool newline = (span_dline != last_y);
				last_y = span_dline;

				// record EM state at the beginning of each new scanline
				if (build_metaprogram)
				{
					if (newline)
					{
						auto & state = shift.scan_states[span_dline];

						state.allocated = true;

						state.dst_line_offset = current_dline;
						state.dst_word_offset = current_dline_wordoffset;
						state.srccol_offset = current_src;
						//state.emdata_offset
						state.mprog_offset = context.words;// current_mprog_words;

						auto & blitstate = state.blitstate;

						blitstate.SKEW = (u8)current_skew;
						blitstate.XC = current_ww;
						blitstate.SYI = current_syinc;
						blitstate.DYI = current_dyinc;

						//blitstate.EM1 = srcdata[0];
						//blitstate.EM2 = srcdata[1];
						//blitstate.EM3 = srcdata[2];

						//context.operations.push_back(new NoOp(context));
						//context.operations.back()->execute();

						if (firstspan)
						{
							firstspan = 0;
							shift.first_scan_state = state;
						}
					}
				}

				// FISR is set when span begins with colour data from previous a word 
				bool span_FISR = pspan->cxs;

				// NFSR can be set on FINAL spans where scrolled width exceeds defined source colour width
				bool span_NFSR = pspan->cxe;
				//						(shift.wordwidth_ > 1) && // NFSR is meaningless for wordwidth==1
				//						(shift.wordsremaining_ == 0) &&
				//						(((spr.w_ + 15) >> 4) != (shift.wordwidth_ + shift.wordoffset_));

				u8 span_skew =
					(shift.shift_ & 15) |
					(span_NFSR ? 0x40 : 0x00) |
					(span_FISR ? 0x80 : 0x00);

				// emit SKEW changes
				{
					u8 span_skew_change = current_skew ^ span_skew;

					if (span_skew_change)
					{
						u16 data = span_skew_change;//0xc000 | (span_skew & 0xCF);

						if (build_metaprogram)
						{
							//context.operations.push_back(new DR_SetSkew16(context, span_skew));
//							context.operations.push_back(new DR_LoadI16(context, context.BHOG_reg, data));
								
							if ((span_skew_change == 0x80) && !(current_skew & 0x80))
								context.operations.push_back(new DR_TasR8(context, context.BHOG_reg));
							else
								context.operations.push_back(new DR_EorI16(context, context.BHOG_reg, data));

							context.operations.back()->execute();
						}

						if (analyse_constants)
						{
							uword_t metrics_dataword = data;

							context.metrics[metrics_dataword].incidence++;
							context.metrics[metrics_dataword].references++;
						}
					}
					current_skew = span_skew;
				}



				// determine true [offset,width] for this line only
				//{

					// destination hop +/-
					int dst_linejump = span_dline - current_dline;
					int dst_wordjump = pspan->ws - current_dline_wordoffset;

					//__debugbreak();

					if (dst_linejump || dst_wordjump)
					{
						//printf("wordjump: %d\n", dst_wordjump);

						// caution: can't use LEA for jumps > +/-32k
						int addressjump = (dst_linejump * context.dstlinewid) + (dst_wordjump * 8);
						if (addressjump < 32768 && addressjump >= -32768)
						{
							if (build_metaprogram)
							{
								if (USE_DSTSKIP_REG_OPTIMIZATION)
								{
									int reg_src = -1;
									DataSource ds;

									ds = context.sim_resolve_data16(uword_t(addressjump), reg_src, true, allow_interline_cpu_statechanges);
									if (ds == DataSource_RegisterL)
									{
										// for +ve steps, look for +ve equivalent register and apply ADD operation
										// only allocates register if value does not pre-exist 
										context.operations.push_back(new DST_AddaR16(context, reg_src, context.SCRLINE_reg, dst_wordjump, dst_linejump));
									}
									else
									{
										// for -ve steps, look for +ve equivalent register and apply SUB operation
										// only allocates register if value does not pre-exist 
										ds = context.sim_resolve_data16(uword_t(-addressjump), reg_src, true, allow_interline_cpu_statechanges);
										if (ds == DataSource_RegisterL)
										{
											context.operations.push_back(new DST_SubaR16(context, reg_src, context.SCRLINE_reg, dst_wordjump, dst_linejump));
										}
										else
										{
											// pointless allocation - fall back to LEA
											context.operations.push_back(new DST_Lea(context, context.SCRLINE_reg, context.SCRLINE_reg, dst_wordjump, dst_linejump));
										}
									}
								}
								else
								{
									context.operations.push_back(new DST_Lea(context, context.SCRLINE_reg, context.SCRLINE_reg, dst_wordjump, dst_linejump));
								}
								context.operations.back()->execute();
							}

							if (analyse_constants)
							{
								// caution: don't abs() -32768 case as it has will have no 16bit representation
								int reduced_dataword = addressjump;
								if (reduced_dataword > -32768)
									reduced_dataword = abs(reduced_dataword);

								uword_t metrics_dataword = (uword_t)reduced_dataword;

								context.metrics[metrics_dataword].incidence++;
								context.metrics[metrics_dataword].references++;
							}
						}
						else
						{
							if (build_metaprogram)
							{
								context.operations.push_back(new DST_AddaIL(context, context.SCRLINE_reg, dst_wordjump, dst_linejump));
								context.operations.back()->execute();
							}

							if (analyse_constants)
							{
								uword_t dH = u32(addressjump) >> 16;
								uword_t dL = u32(addressjump) & 0xFFFF;

								context.metrics[dH].incidence++;
								context.metrics[dH].references++;
								context.metrics[dL].incidence++;
								context.metrics[dL].references++;
							}
						}

						current_dline = span_dline;
						current_dline_wordoffset = pspan->ws;
					}

					if (build_metaprogram)
					{

						// check for all 3 changing
						if ((linechanges[0]/* || em_dirty[0]*/) &&
							(linechanges[1]/* || em_dirty[1]*/) &&
							(linechanges[2]/* || em_dirty[2]*/))
						{
							if (_shared_mask)
							{
								shared_mask_words.push_back(srcdata[0]);
								shared_mask_words.push_back(srcdata[1]);
								shared_mask_words.push_back(srcdata[2]);
								pspan->reload[0] = true;
								pspan->reload[1] = true;
								pspan->reload[2] = true;

								context.operations.push_back(new EM_MoveMPI32(context, 0, context.SM_reg, 0, context.EM1_reg));
								context.operations.back()->execute();
								context.operations.push_back(new EM_MoveMPI16(context, 2, context.SM_reg, 0, context.EM3_reg));
								context.operations.back()->execute();
							}
							else
							{

								int sumA = 0, sumB = 0;

								// test-resolve EM1:EM2 and EM2:EM3, looking for the cheapest solution
								DataSource ds12 = context.sim_resolve_data32(srcdata[0], srcdata[1], reg_index, reg_index2, false, allow_interline_cpu_statechanges);
								DataSource ds3 = context.sim_resolve_data16(srcdata[2], reg_index3, false, allow_interline_cpu_statechanges);

								sumA += DataSource_costs[ds12];
								sumA += DataSource_costs[ds3];

								DataSource ds23 = context.sim_resolve_data32(srcdata[1], srcdata[2], reg_index, reg_index2, false, allow_interline_cpu_statechanges);
								DataSource ds1 = context.sim_resolve_data16(srcdata[0], reg_index3, false, allow_interline_cpu_statechanges);

								sumB += DataSource_costs[ds23];
								sumB += DataSource_costs[ds1];

								int chosen = -1;
								if (sumA == sumB)
								{
									chosen = 0;

									// competing longs on same line
									if (ds12 == DataSource_Immediate32)
									{
										// competing immediates - pick immediate more likely to be optimized
										if (long_cost(srcdata[1], srcdata[2]) < long_cost(srcdata[0], srcdata[1]))
										{
											chosen = 1;
											if (g_allargs.debug)
												printf("preferred long immediate with optimal value [$%04x%04x]\n", srcdata[1], srcdata[2]);
										}
										else
										{
											if (g_allargs.debug)
												printf("preferred long immediate with optimal value [$%04x%04x]\n", srcdata[0], srcdata[1]);
										}
									}
									else
										if (ds12 == DataSource_MixedIR ||
											ds12 == DataSource_RegisterHL)
										{
											// prefer long with higher incidence L
											if (context.metrics_sim[srcdata[2]].incidence < context.metrics_sim[srcdata[1]].incidence)
											{
												chosen = 1;
												if (g_allargs.debug)
													printf("preferred long register with optimal L value [$%04x%04x]\n", srcdata[1], srcdata[2]);
											}
											else
											{
												if (g_allargs.debug)
													printf("preferred long register with optimal L value [$%04x%04x]\n", srcdata[0], srcdata[1]);
											}
										}
										else
										{
											// don't care
										}
								}

								if ((sumA < sumB) || (chosen == 0))
								{
									DataSource ds12 = context.sim_resolve_data32(srcdata[0], srcdata[1], reg_index, reg_index2, true, allow_interline_cpu_statechanges);
									if (ds12 == DataSource_RegisterHL)
									{
										context.operations.push_back(new EM_MoveR32(context, 0, reg_index, context.EM1_reg));
										context.operations.back()->execute();
									}
									else
										if (ds12 == DataSource_RegisterLL)
										{
											context.operations.push_back(new EM_MoveR16(context, 0, reg_index, context.EM1_reg));
											context.operations.back()->execute();
											context.operations.push_back(new EM_MoveR16(context, 1, reg_index2, context.EM2_reg));
											context.operations.back()->execute();
										}
										else
											if (ds12 == DataSource_MixedIR)
											{
												context.operations.push_back(new EM_MoveI16(context, 0, srcdata[0], context.EM1_reg));
												context.operations.back()->execute();
												context.operations.push_back(new EM_MoveR16(context, 1, reg_index2, context.EM2_reg));
												context.operations.back()->execute();
											}
											else
											{
												context.operations.push_back(new EM_MoveI32(context, 0, srcdata[0], srcdata[1], context.EM1_reg));
												context.operations.back()->execute();
											}

									DataSource ds3 = context.sim_resolve_data16(srcdata[2], reg_index3, true, allow_interline_cpu_statechanges);
									if (ds3 == DataSource_RegisterL)
									{
										context.operations.push_back(new EM_MoveR16(context, 2, reg_index3, context.EM3_reg));
										context.operations.back()->execute();
									}
									else
									{
										context.operations.push_back(new EM_MoveI16(context, 2, srcdata[2], context.EM3_reg));
										context.operations.back()->execute();
									}
								}
								else
									if ((sumA > sumB) || (chosen == 1))
									{
										DataSource ds23 = context.sim_resolve_data32(srcdata[1], srcdata[2], reg_index, reg_index2, true, allow_interline_cpu_statechanges);
										if (ds23 == DataSource_RegisterHL)
										{
											context.operations.push_back(new EM_MoveR32(context, 1, reg_index, context.EM2_reg));
											context.operations.back()->execute();
										}
										else
											if (ds23 == DataSource_RegisterLL)
											{
												context.operations.push_back(new EM_MoveR16(context, 1, reg_index, context.EM2_reg));
												context.operations.back()->execute();
												context.operations.push_back(new EM_MoveR16(context, 2, reg_index2, context.EM3_reg));
												context.operations.back()->execute();
											}
											else
												if (ds23 == DataSource_MixedIR)
												{
													context.operations.push_back(new EM_MoveI16(context, 1, srcdata[1], context.EM2_reg));
													context.operations.back()->execute();
													context.operations.push_back(new EM_MoveR16(context, 2, reg_index2, context.EM3_reg));
													context.operations.back()->execute();
												}
												else
													context.operations.push_back(new EM_MoveI32(context, 1, srcdata[1], srcdata[2], context.EM2_reg));

										DataSource ds1 = context.sim_resolve_data16(srcdata[0], reg_index3, true, allow_interline_cpu_statechanges);
										if (ds1 == DataSource_RegisterL)
										{
											context.operations.push_back(new EM_MoveR16(context, 0, reg_index3, context.EM1_reg));
											context.operations.back()->execute();
										}
										else
										{
											context.operations.push_back(new EM_MoveI16(context, 0, srcdata[0], context.EM1_reg));
											context.operations.back()->execute();
										}
									}

								//context.sim_track_datarefs(srcdata[0]);
								//context.sim_track_datarefs(srcdata[1]);
								//context.sim_track_datarefs(srcdata[2]);

								//em_dirty[0] = em_dirty[1] = em_dirty[2] = false;
							}
						}
						else
						if ((linechanges[0]/* || em_dirty[0]*/) &&
							(linechanges[1]/* || em_dirty[1]*/))
						{
							if (_shared_mask)
							{
								shared_mask_words.push_back(srcdata[0]);
								shared_mask_words.push_back(srcdata[1]);
								pspan->reload[0] = true;
								pspan->reload[1] = true;

								context.operations.push_back(new EM_MoveMPI32(context, 0, context.SM_reg, 0, context.EM1_reg));
								context.operations.back()->execute();
							}
							else
							{

								//						context.operations.push_back(new EM_MoveI32(context, 0, srcdata[0], srcdata[1]));
								DataSource ds = context.sim_resolve_data32(srcdata[0], srcdata[1], reg_index, reg_index2, true, allow_interline_cpu_statechanges);
								if (ds == DataSource_RegisterHL)
								{
									context.operations.push_back(new EM_MoveR32(context, 0, reg_index, context.EM1_reg));
									context.operations.back()->execute();
								}
								else
									if (ds == DataSource_RegisterLL)
									{
										context.operations.push_back(new EM_MoveR16(context, 0, reg_index, context.EM1_reg));
										context.operations.back()->execute();
										context.operations.push_back(new EM_MoveR16(context, 1, reg_index2, context.EM2_reg));
										context.operations.back()->execute();
									}
									else
									{
										context.operations.push_back(new EM_MoveI32(context, 0, srcdata[0], srcdata[1], context.EM1_reg));
										context.operations.back()->execute();
									}

								//context.sim_track_datarefs(srcdata[0]);
								//context.sim_track_datarefs(srcdata[1]);

								//em_dirty[0] = em_dirty[1] = false;

		//						if (linechanges[2] || em_dirty[2])
		//						{
		////							context.operations.push_back(new EM_MoveI16(context, 2, srcdata[2]));
		//							if (context.resolve_data16(srcdata[2], reg_index, true) == DataSource_RegisterL)
		//								context.operations.push_back(new EM_MoveR16(context, 2, reg_index));
		//							else
		//								context.operations.push_back(new EM_MoveI16(context, 2, srcdata[2]));
		//
		//							context.track_datarefs(srcdata[2]);
		//
		//							em_dirty[2] = false;
		//						}
							}
						}
						else
						if ((linechanges[1]/* || em_dirty[1]*/) &&
							(linechanges[2]/* || em_dirty[2]*/))
						{
							if (_shared_mask)
							{
								shared_mask_words.push_back(srcdata[1]);
								shared_mask_words.push_back(srcdata[2]);
								pspan->reload[1] = true;
								pspan->reload[2] = true;

								context.operations.push_back(new EM_MoveMPI32(context, 1, context.SM_reg, 2, context.EM1_reg));
								context.operations.back()->execute();
							}
							else
							{
								// longword to EM2
		//						context.operations.push_back(new EM_MoveI32(context, 1, srcdata[1], srcdata[2]));
		//						context.operations.back()->execute();

								//context.sim_track_datarefs(srcdata[1]);
								//context.sim_track_datarefs(srcdata[2]);

		//						em_dirty[1] = em_dirty[2] = false;

								DataSource ds = context.sim_resolve_data32(srcdata[1], srcdata[2], reg_index, reg_index2, true, allow_interline_cpu_statechanges);
								if (ds == DataSource_RegisterHL)
								{
									context.operations.push_back(new EM_MoveR32(context, 1, reg_index, context.EM2_reg));
									context.operations.back()->execute();
								}
								else
									if (ds == DataSource_RegisterLL)
									{
										context.operations.push_back(new EM_MoveR16(context, 1, reg_index, context.EM2_reg));
										context.operations.back()->execute();
										context.operations.push_back(new EM_MoveR16(context, 2, reg_index2, context.EM3_reg));
										context.operations.back()->execute();
									}
									else
									{
										context.operations.push_back(new EM_MoveI32(context, 1, srcdata[1], srcdata[2], context.EM2_reg));
										context.operations.back()->execute();
									}

								//context.sim_track_datarefs(srcdata[0]);
								//context.sim_track_datarefs(srcdata[1]);

								//em_dirty[1] = em_dirty[2] = false;
							}
						}
						else
						{
							// words only

							// this line involves at least one write to EM1/EM2/EM3
							for (int x = 0; x < 3; x++)
							{
								int EM_reg = context.EM1_reg;
								if (x == 1)
									EM_reg = context.EM2_reg;
								else
									if (x == 2)
										EM_reg = context.EM3_reg;

								if (linechanges[x]/* || em_dirty[x]*/)
								{
									if (_shared_mask)
									{
										shared_mask_words.push_back(srcdata[x]);
										pspan->reload[x] = true;

										if (x == 1)
										{
											// 2(a2)
											context.operations.push_back(new EM_MoveMPI16(context, 1, context.SM_reg, 2, context.EM1_reg));
											context.operations.back()->execute();
										}
										else
										{
											// (a2)/(a4)
											context.operations.push_back(new EM_MoveMPI16(context, x, context.SM_reg, 0, EM_reg));
											context.operations.back()->execute();
										}
									}
									else
									{
										if (context.sim_resolve_data16(srcdata[x], reg_index, true, allow_interline_cpu_statechanges) == DataSource_RegisterL)
										{
											context.operations.push_back(new EM_MoveR16(context, x, reg_index, EM_reg));
											context.operations.back()->execute();
										}
										else
										{
											context.operations.push_back(new EM_MoveI16(context, x, srcdata[x], EM_reg));
											context.operations.back()->execute();
										}
									}

									//context.sim_track_datarefs(srcdata[x]);

									//em_dirty[x] = false;
								}
							}
						}
					}


					// SYINC = difference between source wordwidth & preshift wordwidth, + 2
					// note: compensate for NFSR/FISR!
					s16 span_skew_syinc_adj =
						(span_NFSR ? 1 : 0) -
						(span_FISR ? 1 : 0);

					s16 span_skew_syoff_adj =
						0 -
						(span_FISR ? 1 : 0);

					int span_ww_change = pspan->ww - current_ww;

					if (span_ww_change)
					{
						if (build_metaprogram)
						{
							context.operations.push_back(new BLiT_XCMod(context, /*rel=*/span_ww_change, /*abs=*/pspan->ww));
							context.operations.back()->execute();
							context.operations.push_back(new BLiT_DYIMod(context, /*rel=*/-span_ww_change, /*abs=*/(current_dyinc - span_ww_change*8)));
							context.operations.back()->execute();
						}

						span_syi_change -= span_ww_change;

						current_dyinc -= span_ww_change*8;
						current_ww += span_ww_change;
					}

					// emit SYI changes
					{
						// incorporate SKEW FISR/NFSR SYI changes
						s16 span_skew_syinc_change = span_skew_syinc_adj - current_skew_syinc_adj;
						current_skew_syinc_adj = span_skew_syinc_adj;
						span_syi_change += span_skew_syinc_change;

						if (span_syi_change)
						{
							if (build_metaprogram)
							{
								context.operations.push_back(new BLiT_SYIMod(context, /*rel=*/span_syi_change, /*abs=*/(current_syinc + span_syi_change*2)));
								context.operations.back()->execute();
							}

							current_syinc += span_syi_change*2;
						}
					}

					{
						// source hop +/-
						int src_linejump = span_source_sline - current_sline;
						current_sline = span_source_sline;

						int src_wordjump = pspan->ws - current_sline_wordoffset;
						current_sline_wordoffset = pspan->ws;

						// incorporate FISR adjust
						src_wordjump += span_skew_syoff_adj - current_skew_syoff_adj;
						current_skew_syoff_adj = span_skew_syoff_adj;

						if (src_linejump || src_wordjump)
						{
							int addressjump = (src_linejump * (context.srccolww * 8)) + (src_wordjump * 2);

							if (build_metaprogram)
							{
								//int reg_src = -1;
								//ds = context.sim_resolve_data16(uword_t(addressjump), reg_src, true);
								//if (ds == DataSource_RegisterL)

								// todo: source long constant as register (but no other combination)
								context.operations.push_back(new SRC_Skip(context, src_wordjump, src_linejump));
								context.operations.back()->execute();
							}

							if (analyse_constants)
							{
							}

							//src_lines_skipped += src_linejump;
							current_src += addressjump;
						}
					}


					if (build_metaprogram)
					{
						// update blitter dest/ycount
						context.operations.push_back(new BLiT_SetYC(context, context.PLANES_reg));
						context.operations.back()->execute();
						context.operations.push_back(new BLiT_SetDSTADDR(context, context.SCRLINE_reg));
						context.operations.back()->execute();

						// execute the blit for this line, multiplies span buscycle cost by bitplane count (YC)
						context.operations.push_back
						(
							new BLiT_GoSkew
							(
								context, context.BHOG_reg, 
								span_orig_sline, span_dline, span_draw_order, 
								/*buscycles*/pspan->buscycle_count * /*planes*/4
							)
						);
						context.operations.back()->execute();
					}


					current_src += /*planes=*/4 * ((((current_ww - 1) - current_skew_syinc_adj) * /*sxinc=*/2) + current_syinc);

					// SYI accounts for this automatically, except for real skips
					current_sline++;

					// next linejump+blit will be relative to this one (and not an empty one)
					//span_dline = dsty;
				//}
				//else
				//{
				//	src_lines_skipped++;
				//}
			}
		}

		if (build_metaprogram)
		{
			// emit terminal
			shift.mprog_final = context.words;

			context.operations.push_back(new Terminate(context));
			context.operations.back()->execute();
		}
	}

	if (shared_mask_words.size() != shift.shared_mask_words.size())
	{
		printf("error: metaprogram mask sequence (%d) != analysed mask sequence (%d)\n", shared_mask_words.size(), shift.shared_mask_words.size());
		//exit(1);

		for (int m = 0; (m < shared_mask_words.size()) || (m < shift.shared_mask_words.size()); m++)
		{
			if (m < shared_mask_words.size())
				print_dataword(shared_mask_words[m]);
			printf(":");
			if (m < shift.shared_mask_words.size())
				print_dataword(shift.shared_mask_words[m]);
			printf("\n");
		}
	}

//	int program_size = 0;
	
	Context::program_metrics metrics;

	context.cycle_sum(metrics);
	if (g_allargs.verbose)
		printf("metaprogram [built] cycles:%d, words:%d\n", metrics.cycles, metrics.words);

#if (SIM_ALL_STAGES)
	run_sim(context, component_livelines, _frame, pi, shift, linemap_src_2_opt, /*generate=*/false);
#endif
	// peephole passes

	bool changed = !g_allargs.quickcut && !_shared_mask;

	int xform_passes = 0;

//		if (!g_allargs.quickcut)

#if 1
	while (changed)
	{
		changed = false;

		// immediate loads to registers with register-derived values
	
		while (context.transform_pass())
		{
			changed = true;
			xform_passes++;
#if (SIM_ALL_STAGES)
			run_sim(context, component_livelines, _frame, pi, shift, linemap_src_2_opt, /*generate=*/false);
#endif
		}

		changed |= context.cleanup_pass();
#if (SIM_ALL_STAGES)
		run_sim(context, component_livelines, _frame, pi, shift, linemap_src_2_opt, /*generate=*/false);
#endif
		{
			while (context.promote_pass())
			{
				changed = true;
#if (SIM_ALL_STAGES)
				run_sim(context, component_livelines, _frame, pi, shift, linemap_src_2_opt, /*generate=*/false);
#endif
			}
		}
	}

	context.cycle_sum(metrics);
	if (g_allargs.verbose)
		printf("metaprogram [xforms] cycles:%d, words:%d\n", metrics.cycles, metrics.words);

#if 0
// not working yet - should probably be performed as fallback case for a failed transform of LEA
// so subsequent LEAs can take advantage of reused register load
	{
		while (context.promote_lea_pass())
		{
#if (SIM_ALL_STAGES)
			run_sim(context, component_livelines, _frame, pi, shift, linemap_src_2_opt, /*generate=*/false);
#endif
		}
	}
#endif

	//if (!g_allargs.quickcut)
	if (!_shared_mask)
	{
		context.reduce_pass();
#if (SIM_ALL_STAGES)
		run_sim(context, component_livelines, _frame, pi, shift, linemap_src_2_opt, /*generate=*/false);
#endif
	}

	context.cycle_sum(metrics);
	if (g_allargs.verbose)
		printf("metaprogram [reduced] cycles:%d, words:%d\n", metrics.cycles, metrics.words);

	// optimize instruction tail / blitter overlaps
	if (ALLOW_BTAIL_OPTIMIZATIONS && !g_allargs.quickcut)
	{
		if (!_shared_mask)
		{
			context.tail_pass();

			context.cycle_sum(metrics);
			if (g_allargs.verbose)
				printf("metaprogram [tails] cycles:%d, words:%d\n", metrics.cycles, metrics.words);
		}
	}
#endif

	if (!g_allargs.quickcut && g_allargs.verbose)
		std::cout << "preshift " << pi << " codegen optimization transforms: " << xform_passes << std::endl;

	context.cycle_sum(metrics);
	printf("\nmetaprogram [final]:\n");
	printf("> all-cycles:%d\n", metrics.cycles + metrics.blit_simbuscycles*4);
	printf("> spans:%d, blit-cycles:%d, exp-blit-cycles:%d, hidden-tailcycles:%d\n", metrics.spans, metrics.blit_simbuscycles*4, metrics.blit_expbuscycles*4, metrics.hidden_tailcycles);
	printf("> prog-words:%d, prog-cycles:%d, prog-nonmod-cycles:%d\n", metrics.words, metrics.cycles, metrics.main_cycles);
	printf(">  em-mods:%d, em-modcycles:%d \n", metrics.em_mods, metrics.em_modcycles);
	printf(">  dst-skips:%d, dst-skipcycles:%d \n", metrics.dst_skips, metrics.dst_skipcycles);
	printf(">  src-skips:%d, src-skipcycles:%d \n", metrics.src_skips, metrics.src_skipcycles);
	printf(">  group-mods:%d, group-modcyles:%d \n", metrics.group_mods, metrics.group_modcycles);
	printf(">  skew-mods:%d, skew-modcyles:%d \n", metrics.skew_mods, metrics.skew_modcycles);
	printf("\n");

	//

	std::vector<int> *linemap;
	if (_shared_mask)
		linemap = &spr.ems_.linemap_src_2_opt;
	else
		linemap = &spr.emx_.linemap_src_2_opt;


	run_sim(context, component_livelines, _frame, pi, shift, *linemap/*spr.linemap_src_2_opt*/, /*generate=*/true);


	ubyte_t *p68kbin = 0;
	size_t size68kbin = 0;
		
	context.assemble(p68kbin, size68kbin, g_allargs.mactmp);

	if (p68kbin)
	{
		shift.p68kbin = p68kbin;
		shift.size68kbin = size68kbin;
	}
	else
	{
		std::cerr << "error generating 68k binary code!" << std::endl;
		getchar();
		exit(1);
	}

	delete[] changes; changes = 0;
	delete[] buffer; buffer = 0;

	//delete[] component_livelines; component_livelines = 0;
//#endif


//	delete[] livelines; livelines = 0;
	// generate the generator
}

//---------------------------------------------------------------------------------------------------------------------
