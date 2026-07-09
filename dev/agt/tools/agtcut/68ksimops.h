//---------------------------------------------------------------------------------------------------------------------
//	[A]tari [G]ame [T]ools / dml 2016
//---------------------------------------------------------------------------------------------------------------------
//	68k sim operations
//---------------------------------------------------------------------------------------------------------------------

class Operation
{
public:

	Operation
	(
		Context &_ctx, 
		OpKind _kind, 
		OpSize _opsize,
		OpSource _opsrc,
		OpSource _opdst,
		OpWrite _opwr,
		int _cost,
		int _tail,
		int _words
	)
		: ctx(_ctx),
		  kind(_kind),
		  opsize(_opsize),
		  opsrc(_opsrc),
		  opdst(_opdst),
		  opwr(_opwr),
		  cost(_cost),
		  tail(_tail),
		  words(_words),
		  sreg(-1),
		  dreg(-1),
		  areg(-1),
		  stagH(-1),
		  stagL(-1),
		  dtagH(-1),
		  dtagL(-1),
		  atag(-1),
		  hidden(false)
	{
	}

	virtual void onExecute() = 0;
	virtual void onEmit() { };

	virtual std::string comment_pre() { return std::string();  };
	virtual std::string comment_post() { return std::string(); };

	void execute()
	{
		onExecute();

		if (!hidden)
		{
			ctx.cycles += cost;
			ctx.words += words;
		}

		// mark parts of registers which become stable for reading/modifying/combining as a result of a replace-write
		if (opdst == OpSource_Register)
		{
			if (opwr == OpWrite_Replace)
			{
				ctx.registers[dreg].stableL = true;
				//ctx.registers[dreg].constant = false;
			}

			if (opsize == OpSize_32)
			{
				if (opwr == OpWrite_Replace || opwr == OpWrite_ReplaceH || opwr == OpWrite_ReplaceHCombineL)
				{
					ctx.registers[dreg].stableH = true;
					//ctx.registers[dreg].constant = false;
				}
			}
		}
	}

	void emit()
	{
		std::string c_pre = comment_pre();
		if (c_pre.length() > 1)
			ctx.genout << "; " << c_pre << std::endl;

		if (!hidden)
		{
			ctx.buf[0] = 0;
			onEmit();
			if (g_allargs.debug)
				printf("%s", ctx.buf);
			ctx.genout << ctx.buf;
		}

		std::string c_post = comment_post();
		if (c_post.length() > 1)
			ctx.genout << "; " << c_post << std::endl;
	}

	std::string describe()
	{
		ctx.buf[0] = 0;
		onEmit();
		return std::string(ctx.buf);
	}

	static char s_buf[1024];

	int alltagsH[max_registers];
	int alltagsL[max_registers];

	Context &ctx;
	OpKind kind;
	OpSize opsize;
	OpSource opsrc;
	OpSource opdst;
	OpWrite opwr;

	int sreg;
	int dreg;
	int areg;

	int atag;
	int stagH;
	int stagL;
	int dtagH;
	int dtagL;

	int cost;
	int tail;
	int words;

	bool hidden;
};

// --------------------------------------------------------------------

class NoOp : public Operation
{
public:

	NoOp(Context &_ctx)
		: Operation(_ctx, OpKind_Nop, OpSize_None, OpSource_None, OpSource_None, OpWrite_None,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
	}

	std::string comment_pre() {
		return "---";
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " nop\t;%d\n",
			cost
		);
	}

	virtual void onExecute()
	{
	}
};

class EM_MoveI16 : public Operation
{
public:

	EM_MoveI16(Context &_ctx, int _em, uword_t _data, int _areg)
		: Operation(_ctx, OpKind_StoreI16, OpSize_16, OpSource_Immediate, OpSource_Memory, OpWrite_Replace,
		    /*cycles=*/12, 
			/*tail=*/0, 
			/*words=*/2
		  ),
		  em(_em),
		  data(_data)
	{
		assert(_areg < 256);
		areg = _areg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " move.w #$%04x,%s\t;%d [EM:%d]\n", 
			data, 
			ctx.BLiTEM_names[em].c_str(), 
			cost,
			em+1
		);
	}

	virtual void onExecute()
	{
		ctx.blitter.em[em] = data;// ctx.registers[iLHS].L;
		ctx.sim_track_datarefs(data);
	}

	int em;
	uword_t data;
};

class EM_MoveI32 : public Operation
{
public:

	EM_MoveI32(Context &_ctx, int _em, uword_t _dataH, uword_t _dataL, int _areg)
		: Operation(_ctx, OpKind_StoreI32, OpSize_32, OpSource_Immediate, OpSource_Memory, OpWrite_Replace,
		    /*cycles=*/20, 
			/*tail=*/0, 
			/*words=*/3
		  ),
		  em(_em),
		  dataH(_dataH),
		  dataL(_dataL)
	{
		assert(_areg < 256);
		areg = _areg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " move.l #$%04x%04x,%s\t;%d [EM:%d:%d]\n", 
			dataH, 
			dataL, 
			ctx.BLiTEM_names[em].c_str(), 
			cost,
			em+1,em+2
		);
	}

	virtual void onExecute()
	{
		ctx.blitter.em[em + 0] = dataH;// ctx.registers[iLHS].L;
		ctx.blitter.em[em + 1] = dataL;// ctx.registers[iLHS].L;
		ctx.sim_track_datarefs(dataH, dataL);
	}

	int em;
	uword_t dataH;
	uword_t dataL;
};

class EM_MoveR16 : public Operation
{
public:

	EM_MoveR16(Context &_ctx, int _em, int _reg, int _areg)
		: Operation(_ctx, OpKind_StoreR16, OpSize_16, OpSource_Register, OpSource_Memory, OpWrite_Replace,
		    /*cycles=*/8, 
			/*tail=*/0, 
			/*words=*/1
		  ),
		  em(_em)
	{
		assert(_reg < 256);
		sreg = _reg;
		assert(_areg < 256);
		areg = _areg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " move.w %s,%s\t\t;%d [EM:%d] ($%04x)\n", 
			ctx.registers[sreg].name.c_str(), 
			ctx.BLiTEM_names[em].c_str(), 
			cost,
			em+1,
			ctx.registers[sreg].L);
	}

	virtual void onExecute()
	{
		ctx.blitter.em[em] = ctx.registers[sreg].L;
		ctx.sim_track_datarefs(ctx.registers[sreg].L);
	}

	int em;
	uword_t data;
};

class EM_MoveR32 : public Operation
{
public:

	EM_MoveR32(Context &_ctx, int _em, int _reg, int _areg)
		: Operation(_ctx, OpKind_StoreR32, OpSize_32, OpSource_Register, OpSource_Memory, OpWrite_Replace,
		    /*cycles=*/12, 
			/*tail=*/0, 
			/*words=*/1
		  ),
		  em(_em)
	{
		assert(_reg < 256);
		sreg = _reg;
		assert(_areg < 256);
		areg = _areg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " move.l %s,%s\t\t;%d [EM:%d:%d] ($%04x%04x)\n", 
			ctx.registers[sreg].name.c_str(), 
			ctx.BLiTEM_names[em].c_str(), 
			cost,
			em+1,em+2,
			ctx.registers[sreg].H, ctx.registers[sreg].L
		);
	}

	virtual void onExecute()
	{
		ctx.blitter.em[em + 0] = ctx.registers[sreg].H;
		ctx.blitter.em[em + 1] = ctx.registers[sreg].L;
		ctx.sim_track_datarefs(ctx.registers[sreg].H, ctx.registers[sreg].L);
	}

	int em;
};

// --------------------------------------------------------------------

class EM_MoveMPI16 : public Operation
{
public:

	EM_MoveMPI16(Context &_ctx, int _em, int _reg, int _offset, int _areg)
		: Operation(_ctx, OpKind_StoreMPI16, OpSize_16, OpSource_Memory, OpSource_Memory, OpWrite_Replace,
		    /*cycles=*/12, 
			/*tail=*/0, 
			/*words=*/1
		  ),
		  em(_em),
		  offset(_offset)
	{
		assert(_reg < 256);
		sreg = _reg;
		assert(_areg < 256);
		areg = _areg;

		if (offset)
			words++;
	}

	virtual void onEmit()
	{
		if (offset)
		{
			sprintf(ctx.buf, " move.w (%s)+,%d(%s)\t\t;%d [EM:%d]\n",
				ctx.registers[sreg].name.c_str(),
				offset,
				ctx.registers[areg].name.c_str(),
				//ctx.BLiTEM_names[em].c_str(),
				cost,
				em + 1
			);
		}
		else
		{
			sprintf(ctx.buf, " move.w (%s)+,(%s)\t\t;%d [EM:%d]\n",
				ctx.registers[sreg].name.c_str(),
				ctx.registers[areg].name.c_str(),
				//ctx.BLiTEM_names[em].c_str(),
				cost,
				em + 1
			);
		}
	}

	virtual void onExecute()
	{
		if (ctx.shared_mask_word_pos < ctx.shared_mask_words.size())
			ctx.blitter.em[em] = ctx.shared_mask_words[ctx.shared_mask_word_pos++];
	}

	int em;
	int offset;
};

class EM_MoveMPI32 : public Operation
{
public:

	EM_MoveMPI32(Context &_ctx, int _em, int _reg, int _offset, int _areg)
		: Operation(_ctx, OpKind_StoreMPI32, OpSize_32, OpSource_Memory, OpSource_Memory, OpWrite_Replace,
		    /*cycles=*/20, 
			/*tail=*/0, 
			/*words=*/1
		  ),
		  em(_em), 
		  offset(_offset)
	{
		assert(_reg < 256);
		sreg = _reg;
		assert(_areg < 256);
		areg = _areg;

		if (offset)
			words++;
	}

	virtual void onEmit()
	{
		if (offset)
		{
			sprintf(ctx.buf, " move.l (%s)+,%d(%s)\t\t;%d [EM:%d:%d]\n",
				ctx.registers[sreg].name.c_str(),
				offset,
				ctx.registers[areg].name.c_str(),
				//ctx.BLiTEM_names[em].c_str(),
				cost,
				em + 1, em + 2
			);
		}
		else
		{
			sprintf(ctx.buf, " move.l (%s)+,(%s)\t\t;%d [EM:%d:%d]\n",
				ctx.registers[sreg].name.c_str(),
				ctx.registers[areg].name.c_str(),
				//ctx.BLiTEM_names[em].c_str(),
				cost,
				em + 1, em + 2
			);
		}
	}

	virtual void onExecute()
	{
		if (ctx.shared_mask_word_pos < ctx.shared_mask_words.size())
			ctx.blitter.em[em + 0] = ctx.shared_mask_words[ctx.shared_mask_word_pos++];
		if (ctx.shared_mask_word_pos < ctx.shared_mask_words.size())
			ctx.blitter.em[em + 1] = ctx.shared_mask_words[ctx.shared_mask_word_pos++];
	}

	int em;
	int offset;
};

// --------------------------------------------------------------------

class AR_Lea16 : public Operation
{
public:

	AR_Lea16(Context &_ctx, int _reg, uword_t _data)
		: Operation(_ctx, OpKind_Lea16, OpSize_32, OpSource_Immediate, OpSource_Register, OpWrite_Replace,
		    /*cycles=*/8, 
			/*tail=*/2, 
			/*words=*/2
		  ),
		  data(_data)
	{
		dreg = _reg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " lea $%04x.w,%s\t;%d\n", 
			//uword_t(word_t(data)>>15), 
			data, 
			ctx.registers[dreg].name.c_str(), 
			cost
		);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].H = uword_t(word_t(data) >> 15);
		ctx.registers[dreg].L = data;
		ctx.registers[dreg].allocatedH = true;
		ctx.registers[dreg].allocatedL = true;
	}

	uword_t data;
};

class AR_Lea32 : public Operation
{
public:

	AR_Lea32(Context &_ctx, int _reg, uword_t _dataH, uword_t _dataL)
		: Operation(_ctx, OpKind_Lea32, OpSize_32, OpSource_Immediate, OpSource_Register, OpWrite_Replace,
		    /*cycles=*/12, 
			/*tail=*/3, 
			/*words=*/3
		  ),
		  dataH(_dataH),
		  dataL(_dataL)
	{
		dreg = _reg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " lea $%04x%04x,%s\t;%d\n", 
			dataH, dataL, 
			ctx.registers[dreg].name.c_str(), 
			cost
		);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].H = dataH;
		ctx.registers[dreg].L = dataL;
		ctx.registers[dreg].allocatedH = true;
		ctx.registers[dreg].allocatedL = true;
	}

	uword_t dataH;
	uword_t dataL;
};

// --------------------------------------------------------------------

class DR_LoadI16 : public Operation
{
public:

	DR_LoadI16(Context &_ctx, int _reg, uword_t _data)
		: Operation(_ctx, OpKind_LoadI16, OpSize_16, OpSource_Immediate, OpSource_Register, OpWrite_Replace,
		    /*cycles=*/8, 
			/*tail=*/2, 
			/*words=*/2
		  ),
		  data(_data)
	{
		dreg = _reg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " move.w #$%04x,%s\t;%d\n", 
			data, 
			ctx.registers[dreg].name.c_str(), 
			cost
		);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].L = data;// ctx.registers[iLHS].L;
		ctx.registers[dreg].allocatedL = true;

		ctx.sim_track_datarefs(data);
	}

	uword_t data;
};

class DR_Clr8 : public Operation
{
public:

	DR_Clr8(Context &_ctx, int _reg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_None, OpSource_Register, OpWrite_Modify,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		dreg = _reg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " clr.b %s\t;%d [$%04x]\n", 
			ctx.registers[dreg].name.c_str(), 
			cost,
			ctx.registers[dreg].L
		);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].L &= 0xFF00;
	}
};

class DR_Clr16 : public Operation
{
public:

	DR_Clr16(Context &_ctx, int _reg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_None, OpSource_Register, OpWrite_Replace,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		dreg = _reg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " clr.w %s\t;%d\n", 
			ctx.registers[dreg].name.c_str(), 
			cost
		);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].L = 0;
		ctx.registers[dreg].allocatedL = true;
	}
};

class DR_MoveQ32 : public Operation
{
public:

	DR_MoveQ32(Context &_ctx, int _imm, int _reg)
		: Operation(_ctx, OpKind_Other, OpSize_32, OpSource_None, OpSource_Register, OpWrite_Replace,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  ),
		  imm(_imm)
	{
		dreg = _reg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " moveq.l #%d,%s\t;%d\n", 
			imm,
			ctx.registers[dreg].name.c_str(), 
			cost
		);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].H = ulong_t(imm) >> 16;
		ctx.registers[dreg].L = ulong_t(imm) & 0xFFFF;
		ctx.registers[dreg].allocatedH = true;
		ctx.registers[dreg].allocatedL = true;
	}

	int imm;
};

class DR_Not32 : public Operation
{
public:

	DR_Not32(Context &_ctx, int _reg)
		: Operation(_ctx, OpKind_Other, OpSize_32, OpSource_None, OpSource_Register, OpWrite_Modify,
		    /*cycles=*/8, 
			/*tail=*/4, 
			/*words=*/1
		  )
	{
		sreg = dreg = _reg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " not.l %s\t;%d [$%04x%04x]\n", 
			ctx.registers[dreg].name.c_str(), 
			cost,
			ctx.registers[dreg].H, ctx.registers[dreg].L
		);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].H = ~ctx.registers[dreg].H;
		ctx.registers[dreg].L = ~ctx.registers[dreg].L;
	}
};

class DR_Not16 : public Operation
{
public:

	DR_Not16(Context &_ctx, int _reg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_None, OpSource_Register, OpWrite_Modify,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = dreg = _reg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " not.w %s\t;%d [$%04x]\n", 
			ctx.registers[dreg].name.c_str(), 
			cost,
			ctx.registers[dreg].L
		);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].L = ~ctx.registers[dreg].L;
	}
};

class DR_Not8 : public Operation
{
public:

	DR_Not8(Context &_ctx, int _reg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_None, OpSource_Register, OpWrite_Modify,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = dreg = _reg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " not.b %s\t;%d [$%04x]\n", 
			ctx.registers[dreg].name.c_str(), 
			cost,
			ctx.registers[dreg].L
		);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].L = ctx.registers[dreg].L ^ 0x00FF;
	}
};

class DR_Neg32 : public Operation
{
public:

	DR_Neg32(Context &_ctx, int _reg)
		: Operation(_ctx, OpKind_Other, OpSize_32, OpSource_None, OpSource_Register, OpWrite_Modify,
		    /*cycles=*/8, 
			/*tail=*/4, 
			/*words=*/1
		  )
	{
		sreg = dreg = _reg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " neg.l %s\t;%d [$%04x%04x]\n", 
			ctx.registers[dreg].name.c_str(), 
			cost,
			ctx.registers[dreg].H, ctx.registers[dreg].L
		);
	}

	virtual void onExecute()
	{
		long_t v = long_t(ctx.registers[dreg].H << 16) | ctx.registers[dreg].L;
		v = -v;
		ctx.registers[dreg].H = uword_t(ulong_t(v >> 16) & 0x0000FFFF);
		ctx.registers[dreg].L = uword_t(ulong_t(v) & 0x0000FFFF);
	}
};

class DR_Neg16 : public Operation
{
public:

	DR_Neg16(Context &_ctx, int _reg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_None, OpSource_Register, OpWrite_Modify,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = dreg = _reg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " neg.w %s\t;%d [$%04x]\n", 
			ctx.registers[dreg].name.c_str(), 
			cost,
			ctx.registers[dreg].L
		);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].L = uword_t(-(word_t(ctx.registers[dreg].L)));
	}
};

class DR_Neg8 : public Operation
{
public:

	DR_Neg8(Context &_ctx, int _reg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_None, OpSource_Register, OpWrite_Modify,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = dreg = _reg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " neg.b %s\t;%d [$%04x]\n", 
			ctx.registers[dreg].name.c_str(), 
			cost,
			ctx.registers[dreg].L
		);
	}

	virtual void onExecute()
	{
		byte_t v = byte_t(ctx.registers[dreg].L & 0x00FF);
		v = -v;
		ctx.registers[dreg].L = (ctx.registers[dreg].L & 0xFF00) | ubyte_t(v);
	}
};

class DR_AddSubQ32 : public Operation
{
public:

	DR_AddSubQ32(Context &_ctx, int _imm, int _reg)
		: Operation(_ctx, OpKind_Other, OpSize_32, OpSource_Immediate, OpSource_Register, OpWrite_Modify,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  ),
		  imm(_imm)
	{
		sreg = dreg = _reg;
	}

	virtual void onEmit()
	{
		if (imm > 0)
		{
			sprintf(ctx.buf, " addq.l #%d,%s\t;%d [$%04x%04x]\n",
				imm,
				ctx.registers[dreg].name.c_str(),
				cost,
				ctx.registers[dreg].H, ctx.registers[dreg].L
				);
		}
		else
		{
			sprintf(ctx.buf, " subq.l #%d,%s\t;%d [$%04x%04x]\n",
				-imm,
				ctx.registers[dreg].name.c_str(),
				cost,
				ctx.registers[dreg].H, ctx.registers[dreg].L
				);
		}
	}

	virtual void onExecute()
	{
		long_t v = long_t(ctx.registers[dreg].H << 16) | ctx.registers[dreg].L;
		v = v + imm;

		ctx.registers[dreg].H = uword_t(ulong_t(v) >> 16);
		ctx.registers[dreg].L = uword_t(ulong_t(v) & 0xFFFF);;
	}

	int imm;
};

class DR_AddSubQ16 : public Operation
{
public:

	DR_AddSubQ16(Context &_ctx, int _imm, int _reg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_Immediate, OpSource_Register, OpWrite_Modify,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  ),
		  imm(_imm)
	{
		sreg = dreg = _reg;
	}

	virtual void onEmit()
	{
		if (imm > 0)
		{
			sprintf(ctx.buf, " addq.w #%d,%s\t;%d [$%04x]\n",
				imm,
				ctx.registers[dreg].name.c_str(),
				cost,
				ctx.registers[dreg].L
				);
		}
		else
		{
			sprintf(ctx.buf, " subq.w #%d,%s\t;%d [$%04x]\n",
				-imm,
				ctx.registers[dreg].name.c_str(),
				cost,
				ctx.registers[dreg].L
				);
		}
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].L = uword_t(word_t(ctx.registers[dreg].L) + word_t(imm));
	}

	int imm;
};

class DR_AddSubQ8 : public Operation
{
public:

	DR_AddSubQ8(Context &_ctx, int _imm, int _reg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_Immediate, OpSource_Register, OpWrite_Modify,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  ),
		  imm(_imm)
	{
		sreg = dreg = _reg;
	}

	virtual void onEmit()
	{
		if (imm > 0)
		{
			sprintf(ctx.buf, " addq.b #%d,%s\t;%d [$%04x]\n",
				imm,
				ctx.registers[dreg].name.c_str(),
				cost,
				ctx.registers[dreg].L
				);
		}
		else
		{
			sprintf(ctx.buf, " subq.b #%d,%s\t;%d [$%04x]\n",
				-imm,
				ctx.registers[dreg].name.c_str(),
				cost,
				ctx.registers[dreg].L
				);
		}
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].L = (ctx.registers[dreg].L & 0xFF00) | 
								(uword_t(ubyte_t(byte_t(ctx.registers[dreg].L & 0x00FF) + byte_t(imm))) & 0x00FF);
	}

	int imm;
};

class DR_LSI16 : public Operation
{
public:

	DR_LSI16(Context &_ctx, int _imm, int _reg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_Immediate, OpSource_Register, OpWrite_Modify,
		    /*cycles=*/12, 
			/*tail=*/8, 
			/*words=*/1
		  ),
		  imm(_imm)
	{
		sreg = dreg = _reg;
	}

	virtual void onEmit()
	{
		if (imm > 0)
		{
			sprintf(ctx.buf, " lsr.w #%d,%s\t;%d [$%04x]\n",
				imm,
				ctx.registers[dreg].name.c_str(),
				cost,
				ctx.registers[dreg].L
				);
		}
		else
		{
			sprintf(ctx.buf, " lsl.w #%d,%s\t;%d [$%04x]\n",
				-imm,
				ctx.registers[dreg].name.c_str(),
				cost,
				ctx.registers[dreg].L
				);
		}
	}

	virtual void onExecute()
	{
		if (imm < 0)
			ctx.registers[dreg].L = ctx.registers[dreg].L << (-imm);
		else
			ctx.registers[dreg].L = ctx.registers[dreg].L >> imm;
	}

	int imm;
};

class DR_LSI32 : public Operation
{
public:

	DR_LSI32(Context &_ctx, int _imm, int _reg)
		: Operation(_ctx, OpKind_Other, OpSize_32, OpSource_Immediate, OpSource_Register, OpWrite_Modify,
		    /*cycles=*/12, 
			/*tail=*/8, 
			/*words=*/1
		  ),
		  imm(_imm)
	{
		sreg = dreg = _reg;
	}

	virtual void onEmit()
	{
		if (imm > 0)
		{
			sprintf(ctx.buf, " lsr.l #%d,%s\t;%d [$%04x%04x]\n",
				imm,
				ctx.registers[dreg].name.c_str(),
				cost,
				ctx.registers[dreg].H,
				ctx.registers[dreg].L
				);
		}
		else
		{
			sprintf(ctx.buf, " lsl.l #%d,%s\t;%d [$%04x%04x]\n",
				-imm,
				ctx.registers[dreg].name.c_str(),
				cost,
				ctx.registers[dreg].H,
				ctx.registers[dreg].L
				);
		}
	}

	virtual void onExecute()
	{
		ulong_t data32 = (ulong_t(ctx.registers[dreg].H) << 16) | ctx.registers[dreg].L;

		if (imm < 0)
			data32 = data32 << (-imm);
		else
			data32 = data32 >> (imm);

		ctx.registers[dreg].H = data32 >> 16;
		ctx.registers[dreg].L = data32 & 0xFFFF;
	}

	int imm;
};

class DR_ASI16 : public Operation
{
public:

	DR_ASI16(Context &_ctx, int _imm, int _reg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_Immediate, OpSource_Register, OpWrite_Modify,
		    /*cycles=*/12, 
			/*tail=*/8, 
			/*words=*/1
		  ),
		  imm(_imm)
	{
		sreg = dreg = _reg;
	}

	virtual void onEmit()
	{
		if (imm > 0)
		{
			sprintf(ctx.buf, " asr.w #%d,%s\t;%d [$%04x]\n",
				imm,
				ctx.registers[dreg].name.c_str(),
				cost,
				ctx.registers[dreg].L
				);
		}
		else
		{
			sprintf(ctx.buf, " asl.w #%d,%s\t;%d [$%04x]\n",
				-imm,
				ctx.registers[dreg].name.c_str(),
				cost,
				ctx.registers[dreg].L
				);
		}
	}

	virtual void onExecute()
	{
		if (imm < 0)
			ctx.registers[dreg].L = uword_t(word_t(ctx.registers[dreg].L) << (-imm));
		else
			ctx.registers[dreg].L = uword_t(word_t(ctx.registers[dreg].L) >> imm);
	}

	int imm;
};

class DR_ASI32 : public Operation
{
public:

	DR_ASI32(Context &_ctx, int _imm, int _reg)
		: Operation(_ctx, OpKind_Other, OpSize_32, OpSource_Immediate, OpSource_Register, OpWrite_Modify,
		    /*cycles=*/12, 
			/*tail=*/8, 
			/*words=*/1
		  ),
		  imm(_imm)
	{
		sreg = dreg = _reg;
	}

	virtual void onEmit()
	{
		if (imm > 0)
		{
			sprintf(ctx.buf, " asr.l #%d,%s\t;%d [$%04x%04x]\n",
				imm,
				ctx.registers[dreg].name.c_str(),
				cost,
				ctx.registers[dreg].H,
				ctx.registers[dreg].L
				);
		}
		else
		{
			sprintf(ctx.buf, " asl.l #%d,%s\t;%d [$%04x%04x]\n",
				-imm,
				ctx.registers[dreg].name.c_str(),
				cost,
				ctx.registers[dreg].H,
				ctx.registers[dreg].L
				);
		}
	}

	virtual void onExecute()
	{
		long_t data32 = long_t((ulong_t(ctx.registers[dreg].H) << 16) | ctx.registers[dreg].L);

		if (imm < 0)
			data32 = data32 << (-imm);
		else
			data32 = data32 >> (imm);

		ctx.registers[dreg].H = long_t(data32) >> 16;
		ctx.registers[dreg].L = long_t(data32) & 0xFFFF;
	}

	int imm;
};

class DR_ROI16 : public Operation
{
public:

	DR_ROI16(Context &_ctx, int _imm, int _reg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_Immediate, OpSource_Register, OpWrite_Modify,
		    /*cycles=*/12, 
			/*tail=*/8, 
			/*words=*/1
		  ),
		  imm(_imm)
	{
		sreg = dreg = _reg;
	}

	virtual void onEmit()
	{
		if (imm > 0)
		{
			sprintf(ctx.buf, " ror.w #%d,%s\t;%d [$%04x]\n",
				imm,
				ctx.registers[dreg].name.c_str(),
				cost,
				ctx.registers[dreg].L
				);
		}
		else
		{
			sprintf(ctx.buf, " rol.w #%d,%s\t;%d [$%04x]\n",
				-imm,
				ctx.registers[dreg].name.c_str(),
				cost,
				ctx.registers[dreg].L
				);
		}
	}

	virtual void onExecute()
	{
		if (imm < 0)
			ctx.registers[dreg].L = lofl16(ctx.registers[dreg].L, (-imm));
		else
			ctx.registers[dreg].L = rofl16(ctx.registers[dreg].L, imm);
	}

	int imm;
};

class DR_ROI32 : public Operation
{
public:

	DR_ROI32(Context &_ctx, int _imm, int _reg)
		: Operation(_ctx, OpKind_Other, OpSize_32, OpSource_Immediate, OpSource_Register, OpWrite_Modify,
		    /*cycles=*/12, 
			/*tail=*/8, 
			/*words=*/1
		  ),
		  imm(_imm)
	{
		sreg = dreg = _reg;
	}

	virtual void onEmit()
	{
		if (imm > 0)
		{
			sprintf(ctx.buf, " ror.l #%d,%s\t;%d [$%04x%04x]\n",
				imm,
				ctx.registers[dreg].name.c_str(),
				cost,
				ctx.registers[dreg].H,
				ctx.registers[dreg].L
				);
		}
		else
		{
			sprintf(ctx.buf, " rol.l #%d,%s\t;%d [$%04x%04x]\n",
				-imm,
				ctx.registers[dreg].name.c_str(),
				cost,
				ctx.registers[dreg].H,
				ctx.registers[dreg].L
				);
		}
	}

	virtual void onExecute()
	{
		ulong_t data32 = (ulong_t(ctx.registers[dreg].H) << 16) | ctx.registers[dreg].L;

		if (imm < 0)
			data32 = lofl32(data32, (-imm));
		else
			data32 = rofl32(data32, (imm));

		ctx.registers[dreg].H = data32 >> 16;
		ctx.registers[dreg].L = data32 & 0xFFFF;
	}

	int imm;
};


class DR_MoveR8 : public Operation
{
public:

	DR_MoveR8(Context &_ctx, int _sreg, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_Register, OpSource_Register, OpWrite_Combine,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " move.b %s,%s\t;%d [$%04x]\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		uword_t d = ctx.registers[dreg].L;
		uword_t s = ctx.registers[sreg].L;

		uword_t v = (d & 0xFF00) | ubyte_t(s & 0xFF);

		ctx.registers[dreg].L = v;
	}
};

class DR_MoveR16 : public Operation
{
public:

	DR_MoveR16(Context &_ctx, int _sreg, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_Register, OpSource_Register, OpWrite_Replace,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " move.w %s,%s\t;%d [$%04x]\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].L = ctx.registers[sreg].L;
	}
};

class DR_MoveR32 : public Operation
{
public:

	DR_MoveR32(Context &_ctx, int _sreg, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_32, OpSource_Register, OpSource_Register, OpWrite_Replace,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " move.l %s,%s\t;%d [$%04x%04x]\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].H,
			ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].H = ctx.registers[sreg].H;
		ctx.registers[dreg].L = ctx.registers[sreg].L;
	}
};

class DR_AddR32 : public Operation
{
public:

	DR_AddR32(Context &_ctx, int _sreg, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_32, OpSource_Register, OpSource_Register, OpWrite_Combine,
		    /*cycles=*/8, 
			/*tail=*/4, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " add.l %s,%s\t;%d [$%04x%04x]\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].H, ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		long_t d = (ulong_t(ctx.registers[dreg].H) << 16) | ctx.registers[dreg].L;
		long_t s = (ulong_t(ctx.registers[sreg].H) << 16) | ctx.registers[sreg].L;
		long_t v = d + s;

		ctx.registers[dreg].H = ulong_t(v) >> 16;
		ctx.registers[dreg].L = ulong_t(v) & 0xFFFF;
	}
};

class DR_AddR16 : public Operation
{
public:

	DR_AddR16(Context &_ctx, int _sreg, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_Register, OpSource_Register, OpWrite_Combine,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " add.w %s,%s\t;%d [$%04x]\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].L = uword_t(word_t(ctx.registers[dreg].L) + word_t(ctx.registers[sreg].L));
	}
};

class DR_AddR8 : public Operation
{
public:

	DR_AddR8(Context &_ctx, int _sreg, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_Register, OpSource_Register, OpWrite_Combine,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " add.b %s,%s\t;%d [$%04x]\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		uword_t d = ctx.registers[dreg].L;
		uword_t s = ctx.registers[sreg].L;

		uword_t v = (d & 0xFF00) | ubyte_t(byte_t(d & 0xFF) + byte_t(s & 0xFF));

		ctx.registers[dreg].L = v;
	}
};

class DR_SubR32 : public Operation
{
public:

	DR_SubR32(Context &_ctx, int _sreg, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_32, OpSource_Register, OpSource_Register, OpWrite_Combine,
		    /*cycles=*/8, 
			/*tail=*/4, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " sub.l %s,%s\t;%d [$%04x%04x]\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].H, ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		long_t d = (ulong_t(ctx.registers[dreg].H) << 16) | ctx.registers[dreg].L;
		long_t s = (ulong_t(ctx.registers[sreg].H) << 16) | ctx.registers[sreg].L;
		long_t v = d - s;

		ctx.registers[dreg].H = ulong_t(v) >> 16;
		ctx.registers[dreg].L = ulong_t(v) & 0xFFFF;
	}
};

class DR_SubR16 : public Operation
{
public:

	DR_SubR16(Context &_ctx, int _sreg, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_Register, OpSource_Register, OpWrite_Combine,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " sub.w %s,%s\t;%d [$%04x]\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].L = uword_t(word_t(ctx.registers[dreg].L) - word_t(ctx.registers[sreg].L));
	}
};

class DR_SubR8 : public Operation
{
public:

	DR_SubR8(Context &_ctx, int _sreg, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_Register, OpSource_Register, OpWrite_Combine,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " sub.b %s,%s\t;%d [$%04x]\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		uword_t d = ctx.registers[dreg].L;
		uword_t s = ctx.registers[sreg].L;

		uword_t v = (d & 0xFF00) | ubyte_t(byte_t(d & 0xFF) - byte_t(s & 0xFF));

		ctx.registers[dreg].L = v;
	}
};

class DR_AndR32 : public Operation
{
public:

	DR_AndR32(Context &_ctx, int _sreg, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_32, OpSource_Register, OpSource_Register, OpWrite_Combine,
		    /*cycles=*/8, 
			/*tail=*/4, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " and.l %s,%s\t;%d [$%04x%04x]\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].H, ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		ulong_t d = (ulong_t(ctx.registers[dreg].H) << 16) | ctx.registers[dreg].L;
		ulong_t s = (ulong_t(ctx.registers[sreg].H) << 16) | ctx.registers[sreg].L;
		ulong_t v = d & s;

		ctx.registers[dreg].H = ulong_t(v) >> 16;
		ctx.registers[dreg].L = ulong_t(v) & 0xFFFF;
	}
};

class DR_AndR16 : public Operation
{
public:

	DR_AndR16(Context &_ctx, int _sreg, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_Register, OpSource_Register, OpWrite_Combine,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " and.w %s,%s\t;%d [$%04x]\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].L = ctx.registers[dreg].L & ctx.registers[sreg].L;
	}
};

class DR_AndR8 : public Operation
{
public:

	DR_AndR8(Context &_ctx, int _sreg, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_Register, OpSource_Register, OpWrite_Combine,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " and.b %s,%s\t;%d [$%04x]\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		uword_t d = ctx.registers[dreg].L;
		uword_t s = ctx.registers[sreg].L;

		uword_t v = (d & 0xFF00) | ((d & s) & 0xFF);

		ctx.registers[dreg].L = v;
	}
};

class DR_OrR32 : public Operation
{
public:

	DR_OrR32(Context &_ctx, int _sreg, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_32, OpSource_Register, OpSource_Register, OpWrite_Combine,
		    /*cycles=*/8, 
			/*tail=*/4, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " or.l %s,%s\t;%d [$%04x%04x]\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].H, ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		ulong_t d = (ulong_t(ctx.registers[dreg].H) << 16) | ctx.registers[dreg].L;
		ulong_t s = (ulong_t(ctx.registers[sreg].H) << 16) | ctx.registers[sreg].L;
		ulong_t v = d | s;

		ctx.registers[dreg].H = ulong_t(v) >> 16;
		ctx.registers[dreg].L = ulong_t(v) & 0xFFFF;
	}
};

class DR_OrR16 : public Operation
{
public:

	DR_OrR16(Context &_ctx, int _sreg, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_Register, OpSource_Register, OpWrite_Combine,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " or.w %s,%s\t;%d [$%04x]\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].L = ctx.registers[dreg].L | ctx.registers[sreg].L;
	}
};

class DR_OrR8 : public Operation
{
public:

	DR_OrR8(Context &_ctx, int _sreg, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_Register, OpSource_Register, OpWrite_Combine,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " or.b %s,%s\t;%d [$%04x]\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		uword_t d = ctx.registers[dreg].L;
		uword_t s = ctx.registers[sreg].L;

		uword_t v = (d & 0xFF00) | ((d | s) & 0xFF);

		ctx.registers[dreg].L = v;
	}
};

class DR_XorR32 : public Operation
{
public:

	DR_XorR32(Context &_ctx, int _sreg, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_32, OpSource_Register, OpSource_Register, OpWrite_Combine,
		    /*cycles=*/8, 
			/*tail=*/4, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " eor.l %s,%s\t;%d [$%04x%04x]\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].H, ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		ulong_t d = (ulong_t(ctx.registers[dreg].H) << 16) | ctx.registers[dreg].L;
		ulong_t s = (ulong_t(ctx.registers[sreg].H) << 16) | ctx.registers[sreg].L;
		ulong_t v = d ^ s;

		ctx.registers[dreg].H = ulong_t(v) >> 16;
		ctx.registers[dreg].L = ulong_t(v) & 0xFFFF;
	}
};

class DR_XorR16 : public Operation
{
public:

	DR_XorR16(Context &_ctx, int _sreg, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_Register, OpSource_Register, OpWrite_Combine,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " eor.w %s,%s\t;%d [$%04x]\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].L = ctx.registers[dreg].L ^ ctx.registers[sreg].L;
	}
};

class DR_XorR8 : public Operation
{
public:

	DR_XorR8(Context &_ctx, int _sreg, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_Register, OpSource_Register, OpWrite_Combine,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " eor.b %s,%s\t;%d [$%04x]\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		uword_t d = ctx.registers[dreg].L;
		uword_t s = ctx.registers[sreg].L;

		uword_t v = (d & 0xFF00) | ((d ^ s) & 0xFF);

		ctx.registers[dreg].L = v;
	}
};

class DR_SccR8 : public Operation
{
public:

	DR_SccR8(Context &_ctx, bool _cond, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_None, OpSource_Register, OpWrite_Modify,
		    /*cycles=*/8, 
			/*tail=*/4, 
			/*words=*/1
		  ),
		  cond(_cond)
	{
		sreg = dreg = _dreg;
	}

	virtual void onEmit()
	{
		if (cond)
		{
			sprintf(ctx.buf, " st %s\t;%d [$%04x]\n",
				ctx.registers[dreg].name.c_str(),
				cost,
				ctx.registers[dreg].L
				);
		}
		else
		{
			sprintf(ctx.buf, " sf %s\t;%d [$%04x]\n",
				ctx.registers[dreg].name.c_str(),
				cost,
				ctx.registers[dreg].L
				);
		}
	}

	virtual void onExecute()
	{
		if (cond)
			ctx.registers[dreg].L = ctx.registers[dreg].L | 0x00FF;
		else
			ctx.registers[dreg].L = ctx.registers[dreg].L & 0xFF00;
	}

	bool cond;
};

class DR_TasR8 : public Operation
{
public:

	DR_TasR8(Context &_ctx, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_None, OpSource_Register, OpWrite_Modify,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " tas.b %s\t;%d [$%04x]\n",
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].L = ctx.registers[dreg].L | 0x0080;
	}
};

class DR_ExtR16 : public Operation
{
public:

	DR_ExtR16(Context &_ctx, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_None, OpSource_Register, OpWrite_Modify,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " ext.w %s\t;%d [$%04x]\n",
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].L = uword_t(word_t(byte_t(ctx.registers[dreg].L)));
	}
};

class DR_ExtR32 : public Operation
{
public:

	DR_ExtR32(Context &_ctx, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_32, OpSource_None, OpSource_Register, OpWrite_ReplaceH,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " ext.l %s\t;%d [$%04x%04x]\n",
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].H, ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		ulong_t v = ulong_t(long_t(word_t(ctx.registers[dreg].L)));

		ctx.registers[dreg].H = ulong_t(v) >> 16;
		ctx.registers[dreg].L = ulong_t(v) & 0xFFFF;
	}
};
class DR_SwapR32 : public Operation
{
public:

	DR_SwapR32(Context &_ctx, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_32, OpSource_None, OpSource_Register, OpWrite_Modify,
		    /*cycles=*/4, 
			/*tail=*/0, 
			/*words=*/1
		  )
		// note: swap acts like a replace since for each word, the new value doesn't depend on the previous value
		// however it is not injected if the word to be used (usually upper) is not stable in the first place
	{
		sreg = dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " swap %s\t;%d [$%04x%04x]\n",
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].H, ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		uword_t tmp = ctx.registers[dreg].L;
		ctx.registers[dreg].L = ctx.registers[dreg].H;
		ctx.registers[dreg].H = tmp;

		// special handling of stale state
		bool sH = ctx.registers[dreg].stableH;
		ctx.registers[dreg].stableH = ctx.registers[dreg].stableL;
		ctx.registers[dreg].stableL = sH;
	}
};

class DR_BchgR : public Operation
{
public:

	DR_BchgR(Context &_ctx, OpSize _opsize, int _sreg, int _dreg)
		: Operation(_ctx, OpKind_Other, _opsize, OpSource_Register, OpSource_Register, OpWrite_Combine,
		    /*cycles=*/8, 
			/*tail=*/4, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " bchg %s,%s\t;%d [$%04x%04x]\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].H, ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		int shift = ctx.registers[sreg].L & 31;
		ulong_t bit = 1UL << shift;
		ctx.registers[dreg].H ^= ((bit & 0xFFFF0000) >> 16);
		ctx.registers[dreg].L ^= ((bit & 0x0000FFFF) >> 0);
	}
};

class DR_MuluR16 : public Operation
{
public:

	DR_MuluR16(Context &_ctx, int _sreg, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_Register, OpSource_Register, OpWrite_ReplaceHCombineL,
		    /*cycles=*/70, 
			/*tail=*/64, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " mulu.w %s,%s\t;%d [$%04x%04x]\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].H, ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		ulong_t result = ctx.registers[dreg].L * ctx.registers[sreg].L;
		ctx.registers[dreg].H = result >> 16;
		ctx.registers[dreg].L = result & 0xFFFF;
	}
};

class DR_MulsR16 : public Operation
{
public:

	DR_MulsR16(Context &_ctx, int _sreg, int _dreg)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_Register, OpSource_Register, OpWrite_ReplaceHCombineL,
		    /*cycles=*/70, 
			/*tail=*/64, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " muls.w %s,%s\t;%d [$%04x%04x]\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			cost,
			ctx.registers[dreg].H, ctx.registers[dreg].L
			);
	}

	virtual void onExecute()
	{
		long_t result = word_t(ctx.registers[dreg].L) * word_t(ctx.registers[sreg].L);
		ctx.registers[dreg].H = uword_t(ulong_t(result) >> 16);
		ctx.registers[dreg].L = uword_t(ulong_t(result) & 0x0000FFFF);
	}
};

class DR_EorI16 : public Operation
{
public:

	DR_EorI16(Context &_ctx, int _dreg, uword_t _data)
		: Operation(_ctx, OpKind_Other, OpSize_16, OpSource_Immediate, OpSource_Register, OpWrite_Combine,
		    /*cycles=*/8, 
			/*tail=*/0, 
			/*words=*/2
		  ),
		  data(_data)
	{
		sreg = _dreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " eor.w #$%04x,%s\t;%d\n",
			data,
			ctx.registers[dreg].name.c_str(),
			cost
			);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].L ^= data;
	}

	uword_t data;
};

class DR_LoadI32 : public Operation
{
public:

	DR_LoadI32(Context &_ctx, int _reg, uword_t _dataH, uword_t _dataL)
		: Operation(_ctx, OpKind_LoadI32, OpSize_32, OpSource_Immediate, OpSource_Register, OpWrite_Replace,
		    /*cycles=*/12, 
			/*tail=*/0, 
			/*words=*/3
		  ),
		  dataH(_dataH),
		  dataL(_dataL)
	{
		dreg = _reg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " move.l #$%04x%04x,%s\t;%d\n", dataH, dataL, ctx.registers[dreg].name.c_str(), cost);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].H = dataH;
		ctx.registers[dreg].L = dataL;
		ctx.registers[dreg].allocatedH = true;
		ctx.registers[dreg].allocatedL = true;
	}

	uword_t dataH;
	uword_t dataL;
};


class BLiT_Defs : public Operation
{
public:

	BLiT_Defs(Context &_ctx)
		: Operation(_ctx, OpKind_BLITInit, OpSize_None, OpSource_None, OpSource_None, OpWrite_None,
		    /*cycles=*/0, 
			/*tail=*/0, 
			/*words=*/0
		  )
	{
	}

	virtual void onEmit()
	{
		char buf[1024];
		ctx.buf[0] = 0;

		sprintf(buf, "BLiTSXI = $ffff8a20;\t2\n"); strcat(ctx.buf, buf);
		sprintf(buf, "BLiTSYI = $ffff8a22;\t2\n"); strcat(ctx.buf, buf);
		sprintf(buf, "BLiTSRC = $ffff8a24;\t4\n"); strcat(ctx.buf, buf);

		sprintf(buf, "BLiTEM1 = $ffff8a28;\t2\n"); strcat(ctx.buf, buf);
		sprintf(buf, "BLiTEM2 = $ffff8a2a;\t2\n"); strcat(ctx.buf, buf);
		sprintf(buf, "BLiTEM3 = $ffff8a2c;\t2\n"); strcat(ctx.buf, buf);

		sprintf(buf, "BLiTDXI = $ffff8a2e;\t2\n"); strcat(ctx.buf, buf);
		sprintf(buf, "BLiTDYI = $ffff8a30;\t2\n"); strcat(ctx.buf, buf);
		sprintf(buf, "BLiTDST = $ffff8a32;\t4\n"); strcat(ctx.buf, buf);

		sprintf(buf, "BLiTXC = $ffff8a36;\t2\n"); strcat(ctx.buf, buf);
		sprintf(buf, "BLiTYC = $ffff8a38;\t2\n"); strcat(ctx.buf, buf);

		sprintf(buf, "BLiTHOP = $ffff8a3a;\t1\n"); strcat(ctx.buf, buf);
		sprintf(buf, "BLiTLOP = $ffff8a3b;\t1\n"); strcat(ctx.buf, buf);

		sprintf(buf, "BLiTCTRL = $ffff8a3c;\t1\n"); strcat(ctx.buf, buf);
		sprintf(buf, "BLiTSKEW = $ffff8a3d;\t1\n\n"); strcat(ctx.buf, buf);
	}

	virtual void onExecute()
	{
	}
};


class BLiT_Init : public Operation
{
public:

	BLiT_Init(Context &_ctx)
		: Operation(_ctx, OpKind_BLITInit, OpSize_None, OpSource_None, OpSource_None, OpWrite_None,
		    /*cycles=*/16*3, 
			/*tail=*/0, 
			/*words=*/3*3
		  )
	{
	}

	virtual void onEmit()
	{
		char buf[1024];
		sprintf(ctx.buf,	" move.w #$%04x,%s\t;%d\n", ctx.blitter.dyinc, ctx.BLiTDYI_name.c_str(), 16);
		sprintf(buf,		" move.w #$%04x,%s\t;%d\n", ctx.blitter.syinc, ctx.BLiTSYI_name.c_str(), 16);
		strcat(ctx.buf, buf);
		sprintf(buf,		" move.w #$%04x,%s\t;%d\n", ctx.blitter.xcount, ctx.BLiTXC_name.c_str(), 16);
		strcat(ctx.buf, buf);
//		sprintf(buf,		" move.b #$%02x,%s\t;%d\n", ctx.blitter.skew, ctx.BLiTSKEW_name.c_str(), 16);
//		strcat(ctx.buf, buf);
	}

	virtual void onExecute()
	{
	}
};

class BLiT_XCMod : public Operation
{
public:

	BLiT_XCMod(Context &_ctx, int _changew, int _absw)
		: Operation(_ctx, OpKind_BLITXCMod, OpSize_None, OpSource_None, OpSource_None, OpWrite_None,
		    /*cycles=*/20, 
			/*tail=*/0, 
			/*words=*/3
		  ),
		  changew_(_changew),
		  absw_(_absw)
	{
		if (abs(changew_) <= 8)
		{
			// addq.w/subq.w
			cost = 16;
			words = 2;
		}
		else
		{
			// move.w #
			cost = 16;
			words = 3;
		}
	}

	virtual std::string comment_pre()
	{
		return "BLiT_XCMod";
	}

	virtual void onEmit()
	{
		if (changew_ > 0)
		{
			if (changew_ <= 8)
			{
				sprintf(ctx.buf, " addq.w #%d,%s\t;%d (XC:+%d)\n",
					changew_,
					ctx.BLiTXC_name.c_str(),
					cost,
					changew_
				);
			}
			else
			{
				sprintf(ctx.buf, " move.w #%d,%s\t;%d (XC:%d)\n",
					absw_,
					ctx.BLiTXC_name.c_str(),
					cost,
					absw_
				);
			}
		}
		else
		if (changew_ < 0)
		{
			if (changew_ >= -8)
			{
				sprintf(ctx.buf, " subq.w #%d,%s\t;%d (XC:-%d)\n",
					-changew_,
					ctx.BLiTXC_name.c_str(),
					cost,
					-changew_
				);
			}
			else
			{
				sprintf(ctx.buf, " move.w #%d,%s\t;%d (XC:%d)\n",
					absw_,
					ctx.BLiTXC_name.c_str(),
					cost,
					absw_
				);
			}
		}
	}

	virtual void onExecute()
	{
		ctx.blitter.xcount_adj += changew_;
	}

	int changew_;
	int absw_;
};

class BLiT_DYIMod : public Operation
{
public:

	BLiT_DYIMod(Context &_ctx, int _changew, int _absw)
		: Operation(_ctx, OpKind_BLITDYIMod, OpSize_None, OpSource_None, OpSource_None, OpWrite_None,
		    /*cycles=*/20, 
			/*tail=*/0, 
			/*words=*/3
		  ),
		  changew_(_changew),
		  absw_(_absw)
	{
		if (abs(changew_) <= 1)
		{
			// addq.w/subq.w
			cost = 16;
			words = 2;
		}
		else
		{
			// move.w #
			cost = 16;
			words = 3;
		}
	}

	std::string comment_pre() { return "BLiT_DYIMod"; }

	virtual void onEmit()
	{
		if (changew_ > 0)
		{
			if (changew_ <= 1)
			{
				sprintf(ctx.buf, " addq.w #%d,%s\t;%d (DYI:+%d)\n",
					changew_ * 8,
					ctx.BLiTDYI_name.c_str(),
					cost,
					changew_
				);
			}
			else
			{
				sprintf(ctx.buf, " move.w #%d,%s\t;%d (DYI:%d)\n",
					absw_,
					ctx.BLiTDYI_name.c_str(),
					cost,
					absw_
				);
			}
		}
		else
		if (changew_ < 0)
		{
			if (changew_ >= -1)
			{
				sprintf(ctx.buf, " subq.w #%d,%s\t;%d (DYI:-%d)\n",
					-changew_ * 8,
					ctx.BLiTDYI_name.c_str(),
					cost,
					-changew_
				);
			}
			else
			{
				sprintf(ctx.buf, " move.w #%d,%s\t;%d (DYI:%d)\n",
					absw_,
					ctx.BLiTDYI_name.c_str(),
					cost,
					absw_
				);
			}
		}
	}

	virtual void onExecute()
	{
	}

	int changew_;
	int absw_;
};

class BLiT_SYIMod : public Operation
{
public:

	BLiT_SYIMod(Context &_ctx, int _changew, int _absw)
		: Operation(_ctx, OpKind_BLITSYIMod, OpSize_None, OpSource_None, OpSource_None, OpWrite_None,
		    /*cycles=*/20, 
			/*tail=*/0, 
			/*words=*/3
		  ),
		  changew_(_changew),
		  absw_(_absw)
	{
		if (abs(changew_) <= 4)
		{
			// addq.w/subq.w
			cost = 16;
			words = 2;
		}
		else
		{
			// move.w #
			cost = 16;
			words = 3;
		}
	}

	std::string comment_pre() { return "BLiT_SYIMod"; }

	virtual void onEmit()
	{
		if (changew_ > 0)
		{
			if (changew_ <= 4)
			{
				sprintf(ctx.buf, " addq.w #%d,%s\t;%d (SYI:+%d)\n",
					changew_ * 2,
					ctx.BLiTSYI_name.c_str(),
					cost,
					changew_
				);
			}
			else
			{
				sprintf(ctx.buf, " move.w #%d,%s\t;%d (SYI:%d)\n",
					absw_,
					ctx.BLiTSYI_name.c_str(),
					cost,
					absw_
				);
			}
		}
		else
		if (changew_ < 0)
		{
			if (changew_ >= -4)
			{
				sprintf(ctx.buf, " subq.w #%d,%s\t;%d (SYI:-%d)\n",
					-changew_ * 2,
					ctx.BLiTSYI_name.c_str(),
					cost,
					-changew_
				);
			}
			else
			{
				sprintf(ctx.buf, " move.w #%d,%s\t;%d (SYI:%d)\n",
					absw_,
					ctx.BLiTSYI_name.c_str(),
					cost,
					absw_
				);
			}
		}
	}

	virtual void onExecute()
	{
	}

	int changew_;
	int absw_;
};

class DR_SetSkew16 : public Operation
{
public:

	DR_SetSkew16(Context &_ctx, u8 _skew)
		: Operation(_ctx, OpKind_SetSkew, OpSize_16, OpSource_Register, OpSource_Register, OpWrite_Replace,
		    /*cycles=*/8, 
			/*tail=*/0, 
			/*words=*/2
		  ),
		  skew_(_skew),
		  data_(0xc000 | _skew)
	{
		dreg = _ctx.BHOG_reg;
	}

	std::string comment_pre() { return "DR_SetSkew16"; }

	virtual void onEmit()
	{
/*
		sprintf(ctx.buf, " move.b #%d,%s\t;%d (SKEW:%02x)\n",
			skew_, 
			ctx.BLiTSKEW_name.c_str(), 
			cost, 
			skew_
		);
*/
		sprintf(ctx.buf, " move.w #$%x,%s\t;%d (SKEW:%02x)\n",
			data_,
			ctx.registers[dreg].name.c_str(),
			cost,
			skew_
		);
	}

	virtual void onExecute()
	{
		ctx.registers[dreg].L = data_;
	}

	u16 data_;
	u8 skew_;
};

class SYS_PageRestore : public Operation
{
public:

	SYS_PageRestore(Context &_ctx)
		: Operation(_ctx, OpKind_SYSPageRestore, OpSize_None, OpSource_None, OpSource_None, OpWrite_None,
		    /*cycles=*/0, 
			/*tail=*/0, 
			/*words=*/-1
		  )
	{
	}

	virtual void onEmit()
	{
		char buf[1024];
		sprintf(ctx.buf,	" move.w #$%04x,(a5)+\t; dstskip\n", ctx.dstlinewid - (ctx.blitter.xcount << 3));
		sprintf(buf,		" move.w #$%04x,(a5)+\t; dstww2\n", ctx.blitter.xcount * 2);
		strcat(ctx.buf, buf);
		sprintf(buf,		" move.w d4,(a5)+\t; lines\n");
		strcat(ctx.buf, buf);
		sprintf(buf,		" move.l d5,(a5)+\t; record link\n");
		strcat(ctx.buf, buf);
		sprintf(buf,		" move.l a5,usp\t; save ctx\n");
		strcat(ctx.buf, buf);
	}

	virtual void onExecute()
	{
	}
};


class BLiT_SetYC : public Operation
{
public:

	BLiT_SetYC(Context &_ctx, int _sreg)
		: Operation(_ctx, OpKind_BLITSetup, OpSize_16, OpSource_Register, OpSource_Memory, OpWrite_None,
		    /*cycles=*/8, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
	}

	virtual void onEmit()
	{
		sprintf
		(
			ctx.buf,	
			" move.w %s,%s\t;%d\n", 
			ctx.registers[sreg].name.c_str(), 
			ctx.BLiTYC_name.c_str(), 
			cost
		);
	}

	virtual void onExecute()
	{
	}
};

class BLiT_SetDSTADDR : public Operation
{
public:

	BLiT_SetDSTADDR(Context &_ctx, int _sreg)
		: Operation(_ctx, OpKind_BLITSetup, OpSize_32, OpSource_Register, OpSource_Memory, OpWrite_None,
		    /*cycles=*/12, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
		sreg = _sreg;
	}

	virtual void onEmit()
	{
		sprintf
		(
			ctx.buf,
			" move.l %s,%s\t;%d\n",
			ctx.registers[sreg].name.c_str(),
			ctx.BLiTDST_name.c_str(),
			cost
		);
	}

	virtual void onExecute()
	{
		ctx.dstaddr = ctx.memaddr;
	}
};

#if (0)
class BLiT_Go : public Operation
{
public:

	BLiT_Go(Context &_ctx, int _sreg, int _sline, int _dline, int _order)
		: Operation(_ctx, OpKind_BLITGo, OpSize_None, OpSource_None, OpSource_None, OpWrite_None,
		    /*cycles=*/8, 
			/*tail=*/0, 
			/*words=*/1
		  ),
		  sline(_sline),
		  dline(_dline),
		  order(_order)
	{
		sreg = _sreg;
	}

	virtual std::string comment_post()
	{
		return "</BLiT>";
	}

	virtual void onEmit()
	{
		sprintf
		(
			ctx.buf,
			" move.b %s,%s\t;%d BLiT(s=%d,d=%d,o=%d)\n",
			ctx.registers[sreg].name.c_str(), 
			ctx.BLiTCTRL_name.c_str(), 
			cost,
			sline, dline, order
		);
	}

	virtual void onExecute()
	{
		//if ((order == 0) || (order == 43))
		//	__debugbreak();

		int width = ctx.blitter.xcount + ctx.blitter.xcount_adj;

		ctx.memory[ctx.dstaddr + 0] = ctx.blitter.em[0];

		if (width == 2)
		{
			ctx.memory[ctx.dstaddr + 1] = ctx.blitter.em[2];
		}
		else
		if (width == 3)
		{
			ctx.memory[ctx.dstaddr + 1] = ctx.blitter.em[1];
			ctx.memory[ctx.dstaddr + 2] = ctx.blitter.em[2];
		}
		else 
		if (width > 3)
		{
			// multi-word case
			int m = 1;
			for (; m < (width - 1); m++)
				ctx.memory[ctx.dstaddr + m] = ctx.blitter.em[1];
			ctx.memory[ctx.dstaddr + m] = ctx.blitter.em[2];
		}

	}

	int sline;
	int dline;
	int order;
};
#endif

class BLiT_GoSkew : public Operation
{
public:

	BLiT_GoSkew(Context &_ctx, int _sreg, int _sline, int _dline, int _order, int _buscycles)
		: Operation(_ctx, OpKind_BLITGoSkew, OpSize_None, OpSource_None, OpSource_None, OpWrite_None,
		    /*cycles=*/8, 
			/*tail=*/0, 
			/*words=*/1
		  ),
		  sline(_sline),
		  dline(_dline),
		  order(_order),
		  buscycles(_buscycles)
	{
		sreg = _sreg;
	}

	virtual std::string comment_post()
	{
		return "</BLiT>";
	}

	virtual void onEmit()
	{
		sprintf
		(
			ctx.buf,
			" move.w %s,%s\t;%d BLiT(s=%d,d=%d,o=%d,bc=%d)\n",
			ctx.registers[sreg].name.c_str(),
			ctx.BLiTCTRL_name.c_str(),
			cost,
			sline, dline, order, sim_buscycles*4
		);
	}

	virtual void onExecute()
	{
		//if ((order == 0) || (order == 43))
		//	__debugbreak();
		sim_buscycles = 0;

		int width = ctx.blitter.xcount + ctx.blitter.xcount_adj;

		//print_dataword(ctx.blitter.em[0]); putchar(':');
		ctx.memory[ctx.dstaddr + 0] = ctx.blitter.em[0];
		sim_buscycles += 3 - ((ctx.blitter.em[0] == 0xFFFF) ? 1 : 0);


		if (width == 2)
		{
			//print_dataword(ctx.blitter.em[2]); putchar(':');
			ctx.memory[ctx.dstaddr + 1] = ctx.blitter.em[2];
			sim_buscycles += 3 - ((ctx.blitter.em[2] == 0xFFFF) ? 1 : 0);
		}
		else
			if (width == 3)
			{
				//print_dataword(ctx.blitter.em[1]); putchar(':');
				//print_dataword(ctx.blitter.em[2]); putchar(':');
				ctx.memory[ctx.dstaddr + 1] = ctx.blitter.em[1];
				ctx.memory[ctx.dstaddr + 2] = ctx.blitter.em[2];
				sim_buscycles += 3 - ((ctx.blitter.em[1] == 0xFFFF) ? 1 : 0);
				sim_buscycles += 3 - ((ctx.blitter.em[2] == 0xFFFF) ? 1 : 0);
			}
			else
				if (width > 3)
				{
					// multi-word case
					int m = 1;
					for (; m < (width - 1); m++)
					{
						//print_dataword(ctx.blitter.em[1]); putchar(':');
						ctx.memory[ctx.dstaddr + m] = ctx.blitter.em[1];
						sim_buscycles += 3 - ((ctx.blitter.em[1] == 0xFFFF) ? 1 : 0);
					}
					//print_dataword(ctx.blitter.em[2]); putchar(':');
					ctx.memory[ctx.dstaddr + m] = ctx.blitter.em[2];
					sim_buscycles += 3 - ((ctx.blitter.em[2] == 0xFFFF) ? 1 : 0);
				}

		//putchar('\n');

		sim_buscycles += (ctx.blitter.skew & 0x80) ? 1 : 0; // FISR
		sim_buscycles -= (ctx.blitter.skew & 0x40) ? 1 : 0; // NFSR

		sim_buscycles *= 4; // YC=4 (for 4 planes)
	}

	int sline;
	int dline;
	int order;
	int buscycles;		// bus cycles expected when span was recorded

	int sim_buscycles;	// bus cycles actually counted when simulated
};

class SRC_Skip : public Operation
{
public:

	SRC_Skip(Context &_ctx, int _worddelta, int _linedelta)
		: Operation(_ctx, OpKind_SRCSkip, OpSize_None, OpSource_None, OpSource_None, OpWrite_None,
		    /*cycles=*/32, 
			/*tail=*/0, 
			/*words=*/4
		  ),		

		  worddelta(_worddelta),
		  linedelta(_linedelta)
	{
		int offset = delta(ctx.srccolww);
		if (abs(offset) <= 8)
		{
			cost = 24;
			words = 2;
		}
		else
		if ((ctx.SKIPTEMP_reg >= 0) && (abs(offset) < 128))
		{
			cost = 24;
			words = 3;
		}
	}

	virtual std::string comment_pre()
	{
		return "SRC_Skip";
	}

	int delta(int srccolww)
	{
		return (linedelta * (srccolww * 8)) + (worddelta * 2);
	}

	virtual void onEmit()
	{
		int offset = delta(ctx.srccolww);
		if (offset < 0)
		{
			if (offset >= -8)
			{
				sprintf
				(
					ctx.buf,
					" subq.l #%d,%s\t;%d (sxi:%d,syi:%d)\n",
					-offset,
					ctx.BLiTSRC_name.c_str(),
					cost,
					worddelta, linedelta
				);
			}
			else
			if ((ctx.SKIPTEMP_reg >= 0) && (offset > -128))
			{
				sprintf
					(
					ctx.buf,
					" moveq #%d,%s\t;\n sub.l %s,%s\t;%d (sxi:%d,syi:%d)\n",
					-offset,
					ctx.registers[ctx.SKIPTEMP_reg].name.c_str(),
					ctx.registers[ctx.SKIPTEMP_reg].name.c_str(),
					ctx.BLiTSRC_name.c_str(),
					cost,
					worddelta, linedelta
				);
			}
			else
			{
				sprintf
				(
					ctx.buf,
					" sub.l #%d,%s\t;%d (sxi:%d,syi:%d)\n",
					-offset,
					ctx.BLiTSRC_name.c_str(),
					cost,
					worddelta, linedelta
				);
			}
		}
		else
		{
			if (offset <= 8)
			{
				sprintf
				(
					ctx.buf,
					" addq.l #%d,%s\t;%d (sxi:%d,syi:%d)\n",
					offset,
					ctx.BLiTSRC_name.c_str(),
					cost,
					worddelta, linedelta
				);
			}
			else
			if ((ctx.SKIPTEMP_reg >= 0) && (offset < 128))
			{
				sprintf
					(
					ctx.buf,
					" moveq #%d,%s\t;\n add.l %s,%s\t;%d (sxi:%d,syi:%d)\n",
					offset,
					ctx.registers[ctx.SKIPTEMP_reg].name.c_str(),
					ctx.registers[ctx.SKIPTEMP_reg].name.c_str(),
					ctx.BLiTSRC_name.c_str(),
					cost,
					worddelta, linedelta
				);
			}
			else
			{
				sprintf
				(
					ctx.buf,
					" add.l #%d,%s\t;%d (sxi:%d,syi:%d)\n",
					offset,
					ctx.BLiTSRC_name.c_str(),
					cost,
					worddelta, linedelta
				);
			}
		}
	}

	virtual void onExecute()
	{
/*		int dl = delta(ctx.srccolww);
		uword_t dH = u32(dl) >> 16;
		uword_t dL = u32(dl) & 0xFFFF;
		ctx.sim_track_datarefs(dH, dL);
*/	}

	int worddelta;
	int linedelta;
};

class DST_Lea : public Operation
{
public:

	DST_Lea(Context &_ctx, int _sreg, int _dreg, int _worddelta, int _linedelta)
		: Operation(_ctx, OpKind_DSTLea, OpSize_None, OpSource_None, OpSource_None, OpWrite_None,
			/*cycles=*/8, 
			/*tail=*/2, 
			/*words=*/2
		  ),		
		  linedelta(_linedelta),
		  worddelta(_worddelta)
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	int delta(int _dlw) 
	{
		return (linedelta * _dlw) + (worddelta * 8);
	}

	virtual void onEmit()
	{
		int offset = delta(ctx.dstlinewid);

		sprintf
		(
			ctx.buf, 
			" lea %d(%s),%s\t;%d (x:%d,y:%d)\n", 
			offset, 
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(),
			//ctx.DSTADDR_regname.c_str(), 
			//ctx.DSTADDR_regname.c_str(), 
			cost,
			worddelta, 
			linedelta
		);
	}

	int sim_delta() 
	{
		return (linedelta * ctx.dstww) + worddelta;
	}

	virtual void onExecute()
	{
		ctx.memaddr += sim_delta();

		ctx.sim_track_datarefs(uword_t(delta(ctx.dstlinewid)));
	}

	int linedelta;
	int worddelta;
};

class DST_AddaIL : public Operation
{
public:

	DST_AddaIL(Context &_ctx, int _dreg, int _worddelta, int _linedelta)
		: Operation(_ctx, OpKind_DSTAddaIL, OpSize_None, OpSource_None, OpSource_None, OpWrite_None,			
			/*cycles=*/16, 
			/*tail=*/0, 
			/*words=*/3
		  ),		

		  worddelta(_worddelta),
		  linedelta(_linedelta)
	{
		sreg = _dreg;
		dreg = _dreg;
	}

	virtual std::string comment_pre()
	{
		return "DST_AddaIL";
	}

	int delta(int _dlw)
	{
		return (linedelta * _dlw) + (worddelta * 8);
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " adda.l #%d,%s\t;%d (x:%d,y:%d)\n", 
			delta(ctx.dstlinewid), 
			ctx.registers[dreg].name.c_str(), 
			cost,
			worddelta, linedelta
		);
	}

	int sim_delta() 
	{
		return (linedelta * ctx.dstww) + worddelta;
	}

	virtual void onExecute()
	{
		ctx.memaddr += sim_delta();

		int dl = delta(ctx.dstlinewid);
		uword_t dH = u32(dl) >> 16;
		uword_t dL = u32(dl) & 0xFFFF;
		ctx.sim_track_datarefs(dH,dL);
	}

	int worddelta;
	int linedelta;
};

class DST_AddaR16 : public Operation
{
public:

	DST_AddaR16(Context &_ctx, int _sreg, int _dreg, int _worddelta, int _linedelta)
		: Operation(_ctx, OpKind_DSTAddaR16, OpSize_16, OpSource_Register, OpSource_Register, OpWrite_Combine,
		    /*cycles=*/8, 
			/*tail=*/4, 
			/*words=*/1
		  ),		

		  worddelta(_worddelta),
		  linedelta(_linedelta)
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " adda.w %s,%s\t;%d (x:%d,y:%d)\n", 
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(), 
			cost,
			worddelta,
			linedelta
			);
	}

	int sim_delta() 
	{
		return (linedelta * ctx.dstww) + worddelta;
	}

	virtual void onExecute()
	{
		//ctx.memaddr += word_t((word_t(ctx.registers[sreg].L) / ctx.dstlinewid) * ctx.dstww);
		ctx.memaddr += sim_delta();
	}

	int worddelta;
	int linedelta;
};

class DST_SubaR16 : public Operation
{
public:

	DST_SubaR16(Context &_ctx, int _sreg, int _dreg, int _worddelta, int _linedelta)
		: Operation(_ctx, OpKind_DSTSubaR16, OpSize_16, OpSource_Register, OpSource_Register, OpWrite_Combine,
			/*cycles=*/8, 
			/*tail=*/4, 
			/*words=*/1
		  ),		
		  worddelta(_worddelta),
		  linedelta(_linedelta)
	{
		sreg = _sreg;
		dreg = _dreg;
	}

	virtual void onEmit()
	{
		sprintf(ctx.buf, " suba.w %s,%s\t;%d (x:%d,y:%d)\n",
			ctx.registers[sreg].name.c_str(),
			ctx.registers[dreg].name.c_str(), 
			cost,
			worddelta,
			linedelta
			);
	}

	// note: simulator provides signed word/line hops, so result may be -ve, so do not use SUB
	int sim_delta()
	{
		return (linedelta * ctx.dstww) + worddelta;
	}

	virtual void onExecute()
	{
		//ctx.memaddr -= word_t((word_t(ctx.registers[sreg].L) / ctx.dstlinewid) * ctx.dstww);

		// note: operation is SUB, but simulator provides signed word/line hops, so we ADD here
		ctx.memaddr += sim_delta();
	}

	int worddelta;
	int linedelta;
};

class Terminate : public Operation
{
public:

	Terminate(Context &_ctx)
		: Operation(_ctx, OpKind_Terminate, OpSize_None, OpSource_None, OpSource_None, OpWrite_None, 
			/*cycles=*/16, 
			/*tail=*/0, 
			/*words=*/1
		  )
	{
	}

	virtual void onEmit()
	{
//		sprintf(ctx.buf, " tst.w $1.w;\nrts;\n");
		sprintf(ctx.buf, " rts\n");
	}

	virtual void onExecute()
	{
	}
};

char Operation::s_buf[1024];


