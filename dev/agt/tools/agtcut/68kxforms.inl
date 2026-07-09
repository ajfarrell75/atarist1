//---------------------------------------------------------------------------------------------------------------------
//	[A]tari [G]ame [T]ools / dml 2016
//---------------------------------------------------------------------------------------------------------------------
//	68k register-load transformations
//	Notes:
//	- These find non-obvious ways to produce expected constants from free register data while respecting data 
//	  lifespan, address registers and locked/constant registers
//---------------------------------------------------------------------------------------------------------------------

bool Context::safe_rename_H(std::list<Operation*>::iterator curr, std::vector<dataregister>::iterator it_treg, int _rold)
{
	return
	(
		((*curr)->alltagsH[_rold] < 0) ||
		(it_treg->stableH && registers[_rold].stableH && (it_treg->H == registers[_rold].H))
	);
}

bool Context::safe_rename_H(std::list<Operation*>::iterator curr, uword_t _dataH, int _rold)
{
	return
	(
		((*curr)->alltagsH[_rold] < 0) ||
		(registers[_rold].stableH && (_dataH == registers[_rold].H))
	);
}

bool Context::try_reg_transforms(std::list<Operation*>::iterator &curr, std::vector<dataregister>::iterator it_treg, OpSize _opsize, int _rold, uword_t _dataH, uword_t _dataL, int& r_limit, bool _2nd)
{
	// don't transform constant registers
	//if (registers[_rold].constant/* && (it_treg->index != _rold)*/)
	//	return false;

	// don't rename to/from locked registers - but they can still be transformed
	if ((registers[_rold].locked || it_treg->locked)	
		&& (it_treg->index != _rold))
		return false;

	// can't transform variable registers
	if (registers[_rold].variable || it_treg->variable)
		return false;

	// don't rename An -> Dn
	if ((registers[_rold].regclass == RegClass_Address) && 
		(it_treg->regclass != RegClass_Address))
		return false;

	uword_t data16 = _dataL;
	ulong_t data32 = (ulong_t(_dataH) << 16) | _dataL;

	uword_t treg16 = it_treg->L;
	ulong_t treg32 = (ulong_t(it_treg->H) << 16) | it_treg->L;

	int rnew = it_treg->index;

	// can't reassign to a register with a pre-existing lifespan which overlaps with the lifespan of the current job
	// note: register can't collide with itself
	if (!tag_collision(curr, _rold, rnew, _opsize) &&
	// avoid transforming constant registers
		!(it_treg->constant)
	)
	{
		// ensures upper word of target register is stable and correct when replacing 32bit loads
		//bool equalH = ((_opsize == OpSize_16) || 
		//				(it_treg->stableH && (it_treg->H == _dataH)));

		// rnew must have unused H, or OpSize_16 + rnew.H == rold.H

		Operation * poper = *curr;

		bool equalH = false;

		if (_opsize == OpSize_16)
		{
			equalH =
				(poper->alltagsH[_rold] < 0) ||			// any new H will suffice if old H is not a dependency
				(
					(it_treg->stableH) &&				// rnew.H has stable value
					(it_treg->H == registers[_rold].H)	// rnew.H == rold.H		
				)
			;
		}
		else
		{
			equalH = 
				(it_treg->stableH) &&			// rnew.H has stable value
				(it_treg->H == _dataH)			// rnew.H == new_dataH			
				;
		}

		// ensure mid byte of target register is correct when performing low-byte tests
		bool equalM = ((it_treg->L & 0xFF00) == (_dataL & 0xFF00));

		// ensures lower word is valid for 16bit loads
		bool valid_treg16 = (it_treg->stableL);

		// ensures upper word is valid for 32bit loads
		bool valid_treg32 = (_opsize == OpSize_32) && it_treg->stableH;


		if (r_limit >= 4)
		{

			// OPPORTUNITY: (CLR.B Rn == EXPECTED)
			if ((it_treg->regclass == RegClass_Data) && valid_treg16 && equalH &&
				(data16 == (treg16 & 0xFF00))
			)
			{
				if (g_allargs.debug)
					if (_opsize == OpSize_16)
					{
						printf("transform: move.w #$%04x,%s -> clr.b %s [$%04x&$FF00]\n",
							data16,
							registers[_rold].name.c_str(),
							it_treg->name.c_str(),
							treg16
							);
					}
					else
					{
						printf("transform: move.l #$%08x,%s -> clr.b %s [$%08x&$FFFFFF00]\n",
							data32,
							registers[_rold].name.c_str(),
							it_treg->name.c_str(),
							treg32
							);
					}

				register_rename(curr, _rold, rnew);
				curr = operations.erase(curr);
				curr = operations.insert(curr, new DR_Clr8(*this, rnew));
				r_limit -= (*curr)->cost;
				return true;
			}


			// OPPORTUNITY: (NOT.W Rn == EXPECTED)
			if ((it_treg->regclass == RegClass_Data) && valid_treg16 && equalH &&
				(data16 == ~(treg16))
			)
			{
				if (g_allargs.debug)
					if (_opsize == OpSize_16)
					{
						printf("transform: move.w #$%04x,%s -> not.w %s [~$%04x]\n",
							data16,
							registers[_rold].name.c_str(),
							it_treg->name.c_str(),
							treg16
							);
					}
					else
					{
						printf("transform: move.l #$%08x,%s -> not.w %s [$%04x][~$%04x]\n",
							data32,
							registers[_rold].name.c_str(),
							it_treg->name.c_str(),
							it_treg->H, it_treg->L
							);
					}

				register_rename(curr, _rold, rnew);
				curr = operations.erase(curr);
				curr = operations.insert(curr, new DR_Not16(*this, rnew));
				r_limit -= (*curr)->cost;
				return true;
			}

			// OPPORTUNITY: (NOT.B Rn == EXPECTED)
			if ((it_treg->regclass == RegClass_Data) && valid_treg16 && equalH &&
				(data16 == (treg16 ^ 0x00FF))
			)
			{
				if (g_allargs.debug)
					if (_opsize == OpSize_16)
					{
						printf("transform: move.w #$%04x,%s -> not.b %s [$%02x][~$%02x]\n",
							data16,
							registers[_rold].name.c_str(),
							it_treg->name.c_str(),
							treg16 >> 8, treg16 & 0xFF
							);
					}
					else
					{
						printf("transform: move.l #$%08x,%s -> not.b %s [$%06x][~$%02x]\n",
							data32,
							registers[_rold].name.c_str(),
							it_treg->name.c_str(),
							treg32 >> 8, treg32 & 0xFF
							);
					}

				register_rename(curr, _rold, rnew);
				curr = operations.erase(curr);
				curr = operations.insert(curr, new DR_Not8(*this, rnew));
				r_limit -= (*curr)->cost;
				return true;
			}

			// OPPORTUNITY: (NEG.W Rn == EXPECTED)
			if ((it_treg->regclass == RegClass_Data) && valid_treg16 && equalH &&
				(word_t(data16) == -(word_t(treg16)))
			)
			{
				if (g_allargs.debug)
					if (_opsize == OpSize_16)
					{
						printf("transform: move.w #$%04x,%s -> neg.w %s [-$%04x]\n",
							data16,
							registers[_rold].name.c_str(),
							it_treg->name.c_str(),
							treg16
							);
					}
					else
					{
						printf("transform: move.l #$%08x,%s -> neg.w %s [$%04x][-$%04x]\n",
							data32,
							registers[_rold].name.c_str(),
							it_treg->name.c_str(),
							it_treg->H, it_treg->L
							);
					}

				register_rename(curr, _rold, rnew);
				curr = operations.erase(curr);
				curr = operations.insert(curr, new DR_Neg16(*this, rnew));
				r_limit -= (*curr)->cost;
				return true;
			}

			// OPPORTUNITY: (NEG.B Rn == EXPECTED)
			if ((it_treg->regclass == RegClass_Data) && valid_treg16 && equalH && equalM &&
				(byte_t(data16) == -byte_t(treg16))
			)
			{
				if (g_allargs.debug)
					if (_opsize == OpSize_16)
					{
						printf("transform: move.w #$%04x,%s -> neg.b %s [$%02x][-$%02x]\n",
							data16,
							registers[_rold].name.c_str(),
							it_treg->name.c_str(),
							treg16 >> 8, treg16 & 0xFF
							);
					}
					else
					{
						printf("transform: move.l #$%08x,%s -> neg.b %s [$%06x][-$%02x]\n",
							data32,
							registers[_rold].name.c_str(),
							it_treg->name.c_str(),
							treg32 >> 8, treg32 & 0xFF
							);
					}

				register_rename(curr, _rold, rnew);
				curr = operations.erase(curr);
				curr = operations.insert(curr, new DR_Neg8(*this, rnew));
				r_limit -= (*curr)->cost;
				return true;
			}

			// OPPORTUNITY: (TAS.B Rn == EXPECTED)
			if ((it_treg->regclass == RegClass_Data) && valid_treg16 && equalH &&
				(data16 == (treg16 | 0x0080))
			)
			{
				if (g_allargs.debug)
					if (_opsize == OpSize_16)
					{
						printf("transform: move.w #$%04x,%s -> tas.b %s [$%04x|$80]\n",
							data16,
							registers[_rold].name.c_str(),
							it_treg->name.c_str(),
							treg16
							);
					}
					else
					{
						printf("transform: move.l #$%08x,%s -> tas.b %s [$%08x|$80]\n",
							data32,
							registers[_rold].name.c_str(),
							it_treg->name.c_str(),
							treg32
							);
					}

				register_rename(curr, _rold, rnew);
				curr = operations.erase(curr);
				curr = operations.insert(curr, new DR_TasR8(*this, rnew));
				r_limit -= (*curr)->cost;
				return true;
			}

			// OPPORTUNITY: (EXT.W Rn == EXPECTED)
			if ((it_treg->regclass == RegClass_Data) && valid_treg16 && equalH &&
				(word_t(data16) == word_t(byte_t(treg16)))
			)
			{
				if (g_allargs.debug)
					if (_opsize == OpSize_16)
					{
						printf("transform: move.w #$%04x,%s -> ext.w %s[$%04x] [$SS%02x]\n",
							data16,
							registers[_rold].name.c_str(),
							it_treg->name.c_str(),
							treg16 & 0xFFFF,
							treg16 & 0xFF
							);
					}
					else
					{
						printf("transform: move.l #$%08x,%s -> ext.w %s[$%04x] [$%04x][$SS%02x]\n",
							data32,
							registers[_rold].name.c_str(),
							it_treg->name.c_str(),
							treg32 & 0xFFFF,
							treg32 >> 16,
							treg32 & 0xFF
							);
					}

				register_rename(curr, _rold, rnew);
				curr = operations.erase(curr);
				curr = operations.insert(curr, new DR_ExtR16(*this, rnew));
				r_limit -= (*curr)->cost;
				return true;
			}


			if (_opsize == OpSize_16)
			{
				// note: looking to replace a 16bit load, but SWAP is a 32bit operation
				// OPPORTUNITY: (SWAP Rx.W == EXPECTED)

//				if ((it_treg->regclass == RegClass_Data) && it_treg->stableH &&
//					(data16 == it_treg->H)
//				)
//				{

				if ((it_treg->regclass == RegClass_Data) && 
					it_treg->stableH && (it_treg->H == data16) &&
					// don't perform swap if it modifies a high word dependency
					((*curr)->alltagsH[_rold] < 0)
					// don't permit rename if it affects a high word dependency - unless high words are stable and equal
					//safe_rename_H(curr, it_treg, _rold)
				)
				{

				if (g_allargs.debug)
					printf("transform: move.w #$%04x,%s -> swap %s [$%04x<:>$%04x]\n",
						data16,
						registers[_rold].name.c_str(),
						it_treg->name.c_str(),
						it_treg->H, it_treg->L
						);

					register_rename(curr, _rold, rnew);
					curr = operations.erase(curr);
					curr = operations.insert(curr, new DR_SwapR32(*this, rnew));
					r_limit -= (*curr)->cost;
					return true;
				}
			}
		
			if (_opsize == OpSize_32)
			{
				// OPPORTUNITY: (EXT.L Rn.L == EXPECTED)
				if ((it_treg->regclass == RegClass_Data) && valid_treg16 &&
					(long_t(data32) == long_t(word_t(treg16)))
				)
				{
					if (g_allargs.debug)
					printf("transform: move.l #$%08x,%s -> ext.l %s [$SSSS][$%04x]\n",
						data32,
						registers[_rold].name.c_str(),
						it_treg->name.c_str(),
						treg16
						);

					register_rename(curr, _rold, rnew);
					curr = operations.erase(curr);
					curr = operations.insert(curr, new DR_ExtR32(*this, rnew));
					r_limit -= (*curr)->cost;
					return true;
				}

				// OPPORTUNITY: (SWAP Rx.L == EXPECTED)
				if ((it_treg->regclass == RegClass_Data) && valid_treg32 &&
					(_dataL == it_treg->H) &&
					(_dataH == it_treg->L)
				)
				{
					if (g_allargs.debug)
					printf("transform: move.l #$%08x,%s -> swap %s [$%04x<:>$%04x]\n",
						data32,
						registers[_rold].name.c_str(),
						it_treg->name.c_str(),
						it_treg->H, it_treg->L
						);

					register_rename(curr, _rold, rnew);
					curr = operations.erase(curr);
					curr = operations.insert(curr, new DR_SwapR32(*this, rnew));
					r_limit -= (*curr)->cost;
					return true;
				}
			}

			// now check sets and ranges (more expensive to find)
			{
	#if (USE_XFORM_ADDSUBQ)
				for (word_t i = -8; i <= 8; i++)
				{
					if (i == 0)
						continue;

					// OPPORTUNITY: (ADDQ/SUBQ.W #IMM,Rn == EXPECTED)
					// note: avoid side effects on address reg upper word
					if ((it_treg->regclass == RegClass_Data) && valid_treg16 && equalH &&
						(data16 == uword_t(i + word_t(treg16)))
					)
					{
						if (i > 0)
						{
							if (g_allargs.debug)
								if (_opsize == OpSize_16)
								{
									printf("transform: move.w #$%04x,%s -> addq.w #%d,%s [$%04x+%d]\n",
										data16,
										registers[_rold].name.c_str(),
										i,
										it_treg->name.c_str(),
										treg16,
										i);
								}
								else
								{
									printf("transform: move.l #$%08x,%s -> addq.w #%d,%s [$%04x][$%04x+%d]\n",
										data32,
										registers[_rold].name.c_str(),
										i,
										it_treg->name.c_str(),
										it_treg->H, it_treg->L,
										i);
								}
						}
						else
						{
							if (g_allargs.debug)
								if (_opsize == OpSize_16)
								{
									printf("transform: move.w #$%04x,%s -> subq.w #%d,%s [$%04x-%d]\n",
										data16,
										registers[_rold].name.c_str(),
										-i,
										it_treg->name.c_str(),
										treg16,
										-i);
								}
								else
								{
									printf("transform: move.l #$%08x,%s -> subq.w #%d,%s [$%04x][$%04x-%d]\n",
										data32,
										registers[_rold].name.c_str(),
										-i,
										it_treg->name.c_str(),
										it_treg->H, it_treg->L,
										-i);
								}
						}

						register_rename(curr, _rold, rnew);
						curr = operations.erase(curr);
						curr = operations.insert(curr, new DR_AddSubQ16(*this, i, rnew));
						r_limit -= (*curr)->cost;
						return true;
					}

					// OPPORTUNITY: (ADDQ/SUBQ.B #IMM,Rn == EXPECTED)
					if ((it_treg->regclass == RegClass_Data) && valid_treg16 && equalH &&
						data16 ==
						(
							(treg16 & 0xFF00) |
							(uword_t(byte_t(i) + byte_t(treg16 & 0x00FF)) & 0x00FF)
						)
					)
					{
						if (i > 0)
						{
							if (g_allargs.debug)
								if (_opsize == OpSize_16)
								{
									printf("transform: move.w #$%04x,%s -> addq.b #%d,%s [$%02x][$%02x+%d]\n",
										data16,
										registers[_rold].name.c_str(),
										i,
										it_treg->name.c_str(),
										treg16 >> 8, treg16 & 0xFF,
										i);
								}
								else
								{
									printf("transform: move.l #$%08x,%s -> addq.b #%d,%s [$%06x][$%02x+%d]\n",
										data32,
										registers[_rold].name.c_str(),
										i,
										it_treg->name.c_str(),
										treg32 >> 8, treg32 & 0xFF,
										i);
								}
						}
						else
						{
							if (g_allargs.debug)
								if (_opsize == OpSize_16)
								{
									printf("transform: move.w #$%04x,%s -> subq.b #%d,%s [$%02x][$%02x-%d]\n",
										data16,
										registers[_rold].name.c_str(),
										-i,
										it_treg->name.c_str(),
										treg16 >> 8, treg16 & 0xFF,
										-i);
								}
								else
								{
									printf("transform: move.l #$%08x,%s -> subq.b #%d,%s [$%06x][$%02x-%d]\n",
										data32,
										registers[_rold].name.c_str(),
										-i,
										it_treg->name.c_str(),
										treg32 >> 8, treg32 & 0xFF,
										-i);
								}
						}

						register_rename(curr, _rold, rnew);
						curr = operations.erase(curr);
						curr = operations.insert(curr, new DR_AddSubQ8(*this, i, rnew));
						r_limit -= (*curr)->cost;
						return true;
					}

					// OPPORTUNITY: (ADDQ/SUBQ.L #IMM,Rn.L == EXPECTED)
					if ((it_treg->regclass == RegClass_Data) && valid_treg32 &&
						(long_t(data32) == i + long_t(treg32))
					)
					{
						if (g_allargs.debug)
						if (i > 0)
						{
							printf("transform: move.l #$%08x,%s -> addq.l #%d,%s [$%08x+%d]\n",
								data32,
								registers[_rold].name.c_str(),
								i,
								it_treg->name.c_str(),
								treg32,
								i);
						}
						else
						{
							printf("transform: move.l #$%08x,%s -> subq.l #%d,%s [$%08x-%d]\n",
								data32,
								registers[_rold].name.c_str(),
								-i,
								it_treg->name.c_str(),
								treg32,
								-i);
						}

						register_rename(curr, _rold, rnew);
						curr = operations.erase(curr);
						curr = operations.insert(curr, new DR_AddSubQ32(*this, i, rnew));
						r_limit -= (*curr)->cost;
						return true;
					}

				}
	#endif //USE_XFORM_ADDSUBQ

				// now check all register-register transformations
				for (int rs = 0; rs < registers.size(); rs++)
				{
					// don't use source registers with unstable data
					if (!registers[rs].stableL)
						continue;

					uword_t tsrc16 = registers[rs].L;
					ulong_t tsrc32 = (ulong_t(registers[rs].H) << 16) | tsrc16;

					bool valid_tsrc32 = (_opsize == OpSize_32) && registers[rs].stableH;

					bool tsdata = (registers[rs].regclass == RegClass_Data);

					// OPPORTUNITY: (MOVE.B Rx,Ry == EXPECTED)
					if ((it_treg->regclass == RegClass_Data) && tsdata && 
						valid_treg16 && equalH && equalM &&
						(byte_t(data16) == byte_t(tsrc16))
					)
					{
						if (g_allargs.debug)
							if (_opsize == OpSize_16)
							{
								printf("transform: move.w #$%04x,%s -> move.b %s,%s [$%02x][$%02x<-$%02x]\n",
									data16,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									treg16 >> 8, treg16 & 0xFF,
									tsrc16 & 0xFF
									);
							}
							else
							{
								printf("transform: move.l #$%08x,%s -> move.b %s,%s [$%06x][$%02x<-$%02x]\n",
									data32,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									treg32 >> 8, treg32 & 0xFF,
									tsrc32 & 0xFF
									);
							}

						register_rename(curr, _rold, rnew);
						curr = operations.erase(curr);
						curr = operations.insert(curr, new DR_MoveR8(*this, rs, rnew));
						r_limit -= (*curr)->cost;
						return true;
					}

					// OPPORTUNITY: (MOVE.W Rx,Ry.L == EXPECTED)
					if ((it_treg->regclass == RegClass_Data) &&
						valid_treg32 && equalH &&
						(data16 == tsrc16)
					)
					{
						if (g_allargs.debug)
						printf("transform: move.l #$%08x,%s -> move.w %s,%s [$%04x][$%04x<-$%04x]\n",
							data32,
							registers[_rold].name.c_str(),
							registers[rs].name.c_str(),
							it_treg->name.c_str(),
							treg32 >> 16, treg16,
							tsrc16
							);

						register_rename(curr, _rold, rnew);
						curr = operations.erase(curr);
						curr = operations.insert(curr, new DR_MoveR16(*this, rs, rnew));
						r_limit -= (*curr)->cost;
						return true;
					}

					// OPPORTUNITY: (ADD.B Rx,Ry.W == EXPECTED)
					if ((it_treg->regclass == RegClass_Data) && tsdata && 
						valid_treg16 && equalH && equalM &&
						(byte_t(data16) == byte_t(byte_t(treg16) + byte_t(tsrc16)))
					)
					{
						if (g_allargs.debug)
							if (_opsize == OpSize_16)
							{
								printf("transform: move.w #$%04x,%s -> add.b %s,%s [$%02x][$%02x+$%02x]\n",
									data16,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									treg16 >> 8, treg16 & 0xFF,
									tsrc16 & 0xFF
									);
							}
							else
							{
								printf("transform: move.l #$%08x,%s -> add.b %s,%s [$%06x][$%02x+$%02x]\n",
									data32,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									treg32 >> 8, treg32 & 0xFF,
									tsrc32 & 0xFF
									);
							}

						register_rename(curr, _rold, rnew);
						curr = operations.erase(curr);
						curr = operations.insert(curr, new DR_AddR8(*this, rs, rnew));
						r_limit -= (*curr)->cost;
						return true;
					}

					// OPPORTUNITY: (ADD.W Rx,Ry.W == EXPECTED)
					// note: avoid side effects on address reg upper word
					if ((it_treg->regclass == RegClass_Data) &&
						valid_treg16 && equalH &&
						(word_t(data16) == word_t(word_t(treg16) + word_t(tsrc16)))
					)
					{
						if (g_allargs.debug)
							if (_opsize == OpSize_16)
							{
								printf("transform: move.w #$%04x,%s -> add.w %s,%s [$%04x+$%04x]\n",
									data16,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									treg16,
									tsrc16
									);
							}
							else
							{
								printf("transform: move.l #$%8x,%s -> add.w %s,%s [$%04x+$%04x]\n",
									data32,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									treg16,
									tsrc16
									);
							}

						register_rename(curr, _rold, rnew);
						curr = operations.erase(curr);
						curr = operations.insert(curr, new DR_AddR16(*this, rs, rnew));
						r_limit -= (*curr)->cost;
						return true;
					}

					// OPPORTUNITY: (SUB.B Rx,Ry.W == EXPECTED)
					if ((it_treg->regclass == RegClass_Data) && tsdata &&
						valid_treg16 && equalH && equalM &&
						(byte_t(data16) == byte_t(byte_t(treg16) - byte_t(tsrc16)))
					)
					{
						if (g_allargs.debug)
							if (_opsize == OpSize_16)
							{
								printf("transform: move.w #$%04x,%s -> sub.b %s,%s [$%02x][$%02x-$%02x]\n",
									data16,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									treg16 >> 8, treg16 & 0xFF,
									tsrc16 & 0xFF
									);
							}
							else
							{
								printf("transform: move.l #$%08x,%s -> sub.b %s,%s [$%06x][$%02x-$%02x]\n",
									data32,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									treg32 >> 8, treg32 & 0xFF,
									tsrc32 & 0xFF
									);
							}

						register_rename(curr, _rold, rnew);
						curr = operations.erase(curr);
						curr = operations.insert(curr, new DR_SubR8(*this, rs, rnew));
						r_limit -= (*curr)->cost;
						return true;
					}

					// OPPORTUNITY: (SUB.W Rx,Ry.W == EXPECTED)
					// note: avoid side effects on address reg upper word
					if ((it_treg->regclass == RegClass_Data) &&
						valid_treg16 && equalH &&
						(word_t(data16) == word_t(word_t(treg16) - word_t(tsrc16)))
					)
					{
						if (g_allargs.debug)
							if (_opsize == OpSize_16)
							{
								printf("transform: move.w #$%04x,%s -> sub.w %s,%s [$%04x-$%04x]\n",
									data16,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									treg16,
									tsrc16
									);
							}
							else
							{
								printf("transform: move.l #$%08x,%s -> sub.w %s,%s [$%04x-$%04x]\n",
									data32,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									treg16,
									tsrc16
									);
							}

						register_rename(curr, _rold, rnew);
						curr = operations.erase(curr);
						curr = operations.insert(curr, new DR_SubR16(*this, rs, rnew));
						r_limit -= (*curr)->cost;
						return true;
					}

					// OPPORTUNITY: (AND.B Rx,Ry == EXPECTED)
					if ((it_treg->regclass == RegClass_Data) && tsdata && 
						valid_treg16 && equalH && equalM &&
						(ubyte_t(data16) == (ubyte_t(treg16) & ubyte_t(tsrc16)))
					)
					{
						if (g_allargs.debug)
							if (_opsize == OpSize_16)
							{
								printf("transform: move.w #$%04x,%s -> and.b %s,%s [$%02x][$%02x^$%02x]\n",
									data16,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									treg16 >> 8, treg16 & 0xFF,
									tsrc16 & 0xFF
									);
							}
							else
							{
								printf("transform: move.l #$%08x,%s -> and.b %s,%s [$%06x][$%02x^$%02x]\n",
									data32,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									treg32 >> 8, treg32 & 0xFF,
									tsrc32 & 0xFF
									);
							}

						register_rename(curr, _rold, rnew);
						curr = operations.erase(curr);
						curr = operations.insert(curr, new DR_AndR8(*this, rs, rnew));
						r_limit -= (*curr)->cost;
						return true;
					}

					// OPPORTUNITY: (AND.W Rx,Ry == EXPECTED)
					if ((it_treg->regclass == RegClass_Data) && tsdata &&
						valid_treg16 && equalH &&
						(data16 == (treg16 & tsrc16))
					)
					{
						if (g_allargs.debug)
							if (_opsize == OpSize_16)
							{
								printf("transform: move.w #$%04x,%s -> and.w %s,%s [$%04x&$%04x]\n",
									data16,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									treg16,
									tsrc16
									);
							}
							else
							{
								printf("transform: move.l #$%08x,%s -> and.w %s,%s [$%04x&$%04x]\n",
									data32,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									treg16,
									tsrc16
									);
							}

						register_rename(curr, _rold, rnew);
						curr = operations.erase(curr);
						curr = operations.insert(curr, new DR_AndR16(*this, rs, rnew));
						r_limit -= (*curr)->cost;
						return true;
					}

					// OPPORTUNITY: (OR.B Rx,Ry == EXPECTED)
					if ((it_treg->regclass == RegClass_Data) && tsdata &&
						valid_treg16 && equalH && equalM &&
						(ubyte_t(data16) == (ubyte_t(treg16) | ubyte_t(tsrc16)))
					)
					{
						if (g_allargs.debug)
							if (_opsize == OpSize_16)
							{
								printf("transform: move.w #$%04x,%s -> or.b %s,%s [$%02x][$%02x^$%02x]\n",
									data16,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									treg16 >> 16, treg16 & 0xFF,
									tsrc16 & 0xFF
									);
							}
							else
							{
								printf("transform: move.l #$%08x,%s -> or.b %s,%s [$%06x][$%02x^$%02x]\n",
									data32,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									treg32 >> 16, treg32 & 0xFF,
									tsrc32 & 0xFF
									);
							}

						register_rename(curr, _rold, rnew);
						curr = operations.erase(curr);
						curr = operations.insert(curr, new DR_OrR8(*this, rs, rnew));
						r_limit -= (*curr)->cost;
						return true;
					}

					// OPPORTUNITY: (OR.W Rx,Ry == EXPECTED)
					if ((it_treg->regclass == RegClass_Data) && tsdata &&
						valid_treg16 && equalH &&
						(data16 == (treg16 | tsrc16))
					)
					{
						if (g_allargs.debug)
							if (_opsize == OpSize_16)
							{
								printf("transform: move.w #$%04x,%s -> or.w %s,%s [$%04x|$%04x]\n",
									data16,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									it_treg->L,
									registers[rs].L
									);
							}
							else
							{
								printf("transform: move.l #$%08x,%s -> or.w %s,%s [$%04x|$%04x]\n",
									data32,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									it_treg->L,
									registers[rs].L
									);
							}

						register_rename(curr, _rold, rnew);
						curr = operations.erase(curr);
						curr = operations.insert(curr, new DR_OrR16(*this, rs, rnew));
						r_limit -= (*curr)->cost;
						return true;
					}

					// OPPORTUNITY: (EOR.B Rx,Ry == EXPECTED)
					if ((it_treg->regclass == RegClass_Data) && tsdata &&
						valid_treg16 && equalH && equalM &&
						(ubyte_t(data16) == (ubyte_t(treg16) ^ ubyte_t(tsrc16)))
					)
					{
						if (g_allargs.debug)
							if (_opsize == OpSize_16)
							{
								printf("transform: move.w #$%04x,%s -> eor.b %s,%s [$%02x][$%02x^$%02x]\n",
									data16,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									treg16 >> 16, treg16 & 0xFF,
									tsrc16 & 0xFF
									);
							}
							else
							{
								printf("transform: move.l #$%08x,%s -> eor.b %s,%s [$%06x][$%02x^$%02x]\n",
									data32,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									treg32 >> 16, treg32 & 0xFF,
									tsrc32 & 0xFF
									);
							}

						register_rename(curr, _rold, rnew);
						curr = operations.erase(curr);
						curr = operations.insert(curr, new DR_XorR8(*this, rs, rnew));
						r_limit -= (*curr)->cost;
						return true;
					}

					// OPPORTUNITY: (EOR.W Rx,Ry == EXPECTED)
					if ((it_treg->regclass == RegClass_Data) && tsdata &&
						valid_treg16 && equalH &&
						(data16 == (treg16 ^ tsrc16))
					)
					{
						if (g_allargs.debug)
							if (_opsize == OpSize_16)
							{
								printf("transform: move.w #$%04x,%s -> eor.w %s,%s [$%04x^$%04x]\n",
									data16,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									treg16,
									tsrc16
									);
							}
							else
							{
								printf("transform: move.l #$%08x,%s -> eor.w %s,%s [$%04x^$%04x]\n",
									data32,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									treg16,
									tsrc16
									);
							}

						register_rename(curr, _rold, rnew);
						curr = operations.erase(curr);
						curr = operations.insert(curr, new DR_XorR16(*this, rs, rnew));
						r_limit -= (*curr)->cost;
						return true;
					}

				}

			} // if (r_limit >= 4)


			if (r_limit >= 8)
			{

				// OPPORTUNITY: (NOT.L Rn.L == EXPECTED)
				if ((it_treg->regclass == RegClass_Data) &&
					valid_treg32 &&
					(data32 == ~(treg32))
				)
				{
					if (g_allargs.debug)
					printf("transform: move.l #$%08x,%s -> not.l %s [~$%08x]\n",
						data32,
						registers[_rold].name.c_str(),
						it_treg->name.c_str(),
						treg32
						);

					register_rename(curr, _rold, rnew);
					curr = operations.erase(curr);
					curr = operations.insert(curr, new DR_Not32(*this, rnew));
					r_limit -= (*curr)->cost;
					return true;
				}

				// OPPORTUNITY: (NEG.L Rn.L == EXPECTED)
				if ((it_treg->regclass == RegClass_Data) && 
					valid_treg32 &&
					(long_t(data32) == -(long_t(treg32)))
				)
				{
					if (g_allargs.debug)
					printf("transform: move.l #$%08x,%s -> neg.l %s [~$%08x]\n",
						data32,
						registers[_rold].name.c_str(),
						it_treg->name.c_str(),
						treg32
						);

					register_rename(curr, _rold, rnew);
					curr = operations.erase(curr);
					curr = operations.insert(curr, new DR_Neg32(*this, rnew));
					r_limit -= (*curr)->cost;
					return true;
				}

				// OPPORTUNITY: (ST Rn == EXPECTED)
				if ((it_treg->regclass == RegClass_Data) &&
					valid_treg16 && equalH &&
					(data16 == (treg16 | 0x00FF))
				)
				{
					if (g_allargs.debug)
						if (_opsize == OpSize_16)
						{
							printf("transform: move.w #$%04x,%s -> st %s [$%04x|$FF]\n",
								data16,
								registers[_rold].name.c_str(),
								it_treg->name.c_str(),
								treg16
								);
						}
						else
						{
							printf("transform: move.l #$%08x,%s -> st %s [$%08x|$FF]\n",
								data32,
								registers[_rold].name.c_str(),
								it_treg->name.c_str(),
								treg32
								);
						}

					register_rename(curr, _rold, rnew);
					curr = operations.erase(curr);
					curr = operations.insert(curr, new DR_SccR8(*this, true, rnew));
					r_limit -= (*curr)->cost;
					return true;
				}

				// now check all register-register transformations
				for (int rs = 0; rs < registers.size(); rs++)
				{
					if (!registers[rs].stableL)
						continue;

					uword_t tsrc16 = registers[rs].L;
					ulong_t tsrc32 = (ulong_t(registers[rs].H) << 16) | tsrc16;

					bool valid_tsrc32 = (_opsize == OpSize_32) && registers[rs].stableH;

					// OPPORTUNITY: (ADD.L Rx,Ry.L == EXPECTED)
					if (valid_treg32 && valid_tsrc32 &&
						(long_t(data32) == long_t(long_t(treg32) + long_t(tsrc32)))
					)
					{
						if (g_allargs.debug)
						printf("transform: move.l #$%08x,%s -> add.l %s,%s [$%08x+$%08x]\n",
							data32,
							registers[_rold].name.c_str(),
							registers[rs].name.c_str(),
							it_treg->name.c_str(),
							treg32,
							tsrc32
							);

						register_rename(curr, _rold, rnew);
						curr = operations.erase(curr);
						curr = operations.insert(curr, new DR_AddR32(*this, rs, rnew));
						r_limit -= (*curr)->cost;
						return true;
					}

					// OPPORTUNITY: (SUB.L Rx,Ry.L == EXPECTED)
					if (valid_treg32 && valid_tsrc32 &&
						(long_t(data32) == long_t(long_t(treg32) - long_t(tsrc32)))
					)
					{
						if (g_allargs.debug)
						printf("transform: move.l #$%08x,%s -> sub.l %s,%s [$%08x+$%08x]\n",
							data32,
							registers[_rold].name.c_str(),
							registers[rs].name.c_str(),
							it_treg->name.c_str(),
							treg32,
							tsrc32
							);

						register_rename(curr, _rold, rnew);
						curr = operations.erase(curr);
						curr = operations.insert(curr, new DR_SubR32(*this, rs, rnew));
						r_limit -= (*curr)->cost;
						return true;
					}

					// OPPORTUNITY: (AND.L Rx,Ry.L == EXPECTED)
					if ((it_treg->regclass == RegClass_Data) && (registers[rs].regclass == RegClass_Data) &&
						valid_treg32 && valid_tsrc32 &&
						(data32 == (treg32 & tsrc32))
					)
					{
						if (g_allargs.debug)
						printf("transform: move.l #$%08x,%s -> and.l %s,%s [$%08x&$%08x]\n",
							data32,
							registers[_rold].name.c_str(),
							registers[rs].name.c_str(),
							it_treg->name.c_str(),
							treg32,
							tsrc32
							);

						register_rename(curr, _rold, rnew);
						curr = operations.erase(curr);
						curr = operations.insert(curr, new DR_AndR32(*this, rs, rnew));
						r_limit -= (*curr)->cost;
						return true;
					}

					// OPPORTUNITY: (OR.L Rx,Ry.L == EXPECTED)
					if ((it_treg->regclass == RegClass_Data) && (registers[rs].regclass == RegClass_Data) &&
						valid_treg32 && valid_tsrc32 &&
						(data32 == (treg32 | tsrc32))
					)
					{
						if (g_allargs.debug)
						printf("transform: move.l #$%08x,%s -> or.l %s,%s [$%08x|$%08x]\n",
							data32,
							registers[_rold].name.c_str(),
							registers[rs].name.c_str(),
							it_treg->name.c_str(),
							treg32,
							tsrc32
							);

						register_rename(curr, _rold, rnew);
						curr = operations.erase(curr);
						curr = operations.insert(curr, new DR_OrR32(*this, rs, rnew));
						r_limit -= (*curr)->cost;
						return true;
					}

					// OPPORTUNITY: (EOR.L Rx,Ry.L == EXPECTED)
					if ((it_treg->regclass == RegClass_Data) && (registers[rs].regclass == RegClass_Data) &&
						valid_treg32 && valid_tsrc32 &&
						(data32 == (treg32 ^ tsrc32))
					)
					{
						if (g_allargs.debug)
						printf("transform: move.l #$%08x,%s -> eor.l %s,%s [$%08x^$%08x]\n",
							data32,
							registers[_rold].name.c_str(),
							registers[rs].name.c_str(),
							it_treg->name.c_str(),
							treg32,
							tsrc32
							);

						register_rename(curr, _rold, rnew);
						curr = operations.erase(curr);
						curr = operations.insert(curr, new DR_XorR32(*this, rs, rnew));
						r_limit -= (*curr)->cost;
						return true;
					}

					if (_opsize == OpSize_16)
					{
						// OPPORTUNITY: (BCHG Rx,Ry.W == EXPECTED)
						// note: looking to replace a 16bit load, but BCHG is a 32bit operation so we filter a bit
						if ((it_treg->regclass == RegClass_Data) && (registers[rs].regclass == RegClass_Data) &&
							// try register has valid data
							valid_treg16 &&
							// filter out side effects of BCHG on H word
							((registers[rs].L & 31) < 16) &&
							// desired data matches BCHG output
							(data16 == (it_treg->L ^ (1UL << (registers[rs].L & 31)))) &&
							// don't permit rename if it affects a high word dependency - unless high words are stable and equal
							safe_rename_H(curr, it_treg, _rold)
						)
//						if ((it_treg->regclass == RegClass_Data) && (registers[rs].regclass == RegClass_Data) &&
//							valid_treg16 &&
//							((registers[rs].L & 31) < 16) && // filter out side effects on H word
//							(data16 == (it_treg->L ^ (1 << (registers[rs].L & 31))))
//							)
						{
							if (g_allargs.debug)
							printf("transform: move.w #$%04x,%s -> bchg %s,%s [$%04x^$%04x]\n",
								data16,
								registers[_rold].name.c_str(),
								registers[rs].name.c_str(),
								it_treg->name.c_str(),
								it_treg->L,
								(uword_t)(1UL << (registers[rs].L & 31))
								);

							register_rename(curr, _rold, rnew);
							curr = operations.erase(curr);
							// note: we know this bit is in the lower word, so we can specify word opsize
							curr = operations.insert(curr, new DR_BchgR(*this, OpSize_16, rs, rnew));
							r_limit -= (*curr)->cost;
							return true;
						}
					}
					else
					{
						// OPPORTUNITY: (BCHG Rx,Ry.L == EXPECTED)
						if ((it_treg->regclass == RegClass_Data) && (registers[rs].regclass == RegClass_Data) &&
							valid_treg32 &&
							(data32 == (((ulong_t(it_treg->H) << 16) | it_treg->L) ^ (1UL << (registers[rs].L & 31))))
						)
						{
							if (g_allargs.debug)
							printf("transform: move.l #$%04x%04x,%s -> bchg %s,%s [$%04x%04x^$%08x]\n",
								_dataH, _dataL,
								registers[_rold].name.c_str(),
								registers[rs].name.c_str(),
								it_treg->name.c_str(),
								it_treg->H, it_treg->L,
								(uword_t)(1UL << (registers[rs].L & 31))
								);

							register_rename(curr, _rold, rnew);
							curr = operations.erase(curr);
							curr = operations.insert(curr, new DR_BchgR(*this, OpSize_32, rs, rnew));
							r_limit -= (*curr)->cost;
							return true;
						}
					}
				}

				// allow 8c compound operations for 32bit loads only
				if (!_2nd && (_opsize == OpSize_32) && (it_treg->regclass == RegClass_Data))
				{
					// no-op + ?
					if (equalH && valid_treg32)
					{
						//uword_t saveL = it_treg->L;

						//it_treg->L &= 0xFF00;

						if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
						{
							//it_treg->L = saveL;

							if (g_allargs.debug)
								printf("preamble:  none %s\n",
									it_treg->name.c_str()
									);

							//curr = operations.insert(curr, new DR_Clr8(*this, rnew));
							//r_limit -= (*curr)->cost;
							return true;
						}

						//it_treg->L = saveL;
					}


					// CLR.B + ?
					if (equalH && valid_treg32)
					{
						uword_t saveL = it_treg->L;

						it_treg->L &= 0xFF00;

						if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
						{
							it_treg->L = saveL;

							if (g_allargs.debug)
								printf("preamble:  clr.b %s\n",
									it_treg->name.c_str()
									);

							curr = operations.insert(curr, new DR_Clr8(*this, rnew));
							r_limit -= (*curr)->cost;
							return true;
						}

						it_treg->L = saveL;
					}

					// CLR.W + ?
					if (equalH && valid_treg32)
					{
						uword_t saveL = it_treg->L;

						it_treg->L = 0;

						if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
						{
							it_treg->L = saveL;

							if (g_allargs.debug)
								printf("preamble:  clr.w %s\n",
									it_treg->name.c_str()
									);

							curr = operations.insert(curr, new DR_Clr16(*this, rnew));
							r_limit -= (*curr)->cost;
							return true;
						}

						it_treg->L = saveL;
					}

					// EXT.W + ?
					if (equalH && valid_treg32)
					{
						uword_t saveL = it_treg->L;

						it_treg->L = uword_t(word_t(byte_t(it_treg->L)));

						if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
						{
							it_treg->L = saveL;

							if (g_allargs.debug)
								printf("preamble:  ext.w %s\n",
									it_treg->name.c_str()
									);

							curr = operations.insert(curr, new DR_ExtR16(*this, rnew));
							r_limit -= (*curr)->cost;
							return true;
						}

						it_treg->L = saveL;
					}

					// EXT.L + ?
					if (valid_treg16)
					{
						uword_t saveH = it_treg->H;
						uword_t saveL = it_treg->L;
						bool saveSH = it_treg->stableH;

						ulong_t v = ulong_t(saveH) << 16 | saveL;
						v = ulong_t(long_t(word_t(v)));

						it_treg->H = v >> 16;
						it_treg->L = v & 0xFFFF;

						if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
						{
							it_treg->H = saveH;
							it_treg->L = saveL;
							it_treg->stableH = saveSH;

							if (g_allargs.debug)
								printf("preamble:  ext.l %s\n",
									it_treg->name.c_str()
									);

							curr = operations.insert(curr, new DR_ExtR32(*this, rnew));
							r_limit -= (*curr)->cost;
							return true;
						}

						it_treg->H = saveH;
						it_treg->L = saveL;
						it_treg->stableH = saveSH;
					}

					// SWAP + ?
					if ((_opsize == OpSize_32) && valid_treg32)
					{
						uword_t saveH = it_treg->H;
						uword_t saveL = it_treg->L;
						bool saveSH = it_treg->stableH;
						bool saveSL = it_treg->stableL;

						it_treg->L = saveH;
						it_treg->H = saveL;
						it_treg->stableL = saveSH;
						it_treg->stableH = saveSL;

						if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
						{
							it_treg->H = saveH;
							it_treg->L = saveL;
							it_treg->stableH = saveSH;
							it_treg->stableL = saveSL;

							if (g_allargs.debug)
								printf("preamble:  swap %s\n",
									it_treg->name.c_str()
									);

							curr = operations.insert(curr, new DR_SwapR32(*this, rnew));
							r_limit -= (*curr)->cost;
							return true;
						}

						it_treg->H = saveH;
						it_treg->L = saveL;
						it_treg->stableH = saveSH;
						it_treg->stableL = saveSL;
					}

					// TAS.B + ?
					if (equalH && valid_treg32)
					{
						uword_t save = it_treg->L;

						it_treg->L = it_treg->L | 0x0080;

						if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
						{
							it_treg->L = save;

							if (g_allargs.debug)
								printf("preamble:  tas.b %s\n",
									it_treg->name.c_str()
									);

							curr = operations.insert(curr, new DR_TasR8(*this, rnew));
							r_limit -= (*curr)->cost;
							return true;
						}

						it_treg->L = save;
					}

					// NOT.B + ?
					if (equalH && valid_treg32)
					{
						uword_t save = it_treg->L;

						it_treg->L = it_treg->L ^ 0xFF;

						if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
						{
							it_treg->L = save;

							if (g_allargs.debug)
								printf("preamble:  not.b %s\n",
									it_treg->name.c_str()
									);

							curr = operations.insert(curr, new DR_Not8(*this, rnew));
							r_limit -= (*curr)->cost;
							return true;
						}

						it_treg->L = save;
					}

					// NOT.W + ?
					if (equalH && valid_treg32)
					{
						uword_t save = it_treg->L;

						it_treg->L = it_treg->L ^ 0xFFFF;

						if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
						{
							it_treg->L = save;

							if (g_allargs.debug)
								printf("preamble:  not.w %s\n",
									it_treg->name.c_str()
									);

							curr = operations.insert(curr, new DR_Not16(*this, rnew));
							r_limit -= (*curr)->cost;
							return true;
						}

						it_treg->L = save;
					}

					// NEG.B + ?
					if (equalH && valid_treg32)
					{
						uword_t save = it_treg->L;

						it_treg->L = (it_treg->L & 0xFF00) | (-byte_t(it_treg->L) & 0xFF);

						if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
						{
							it_treg->L = save;

							if (g_allargs.debug)
								printf("preamble:  neg.b %s\n",
									it_treg->name.c_str()
									);

							curr = operations.insert(curr, new DR_Neg8(*this, rnew));
							r_limit -= (*curr)->cost;
							return true;
						}

						it_treg->L = save;
					}
					// NEG.W + ?
					if (equalH && valid_treg32)
					{
						uword_t save = it_treg->L;

						it_treg->L = -word_t(it_treg->L);

						if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
						{
							it_treg->L = save;

							if (g_allargs.debug)
								printf("preamble:  neg.w %s\n",
									it_treg->name.c_str()
									);

							curr = operations.insert(curr, new DR_Neg16(*this, rnew));
							r_limit -= (*curr)->cost;
							return true;
						}

						it_treg->L = save;
					}

					// immediate ranges
					if (equalH && valid_treg32)
					{
						for (int imm = -8; imm <= 8; imm++)
						{
							if (imm == 0)
								continue;

							{
								uword_t save = it_treg->L;

								it_treg->L = uword_t(word_t(it_treg->L) + word_t(imm));

								if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
								{
									it_treg->L = save;

									if (imm > 0)
									{
										if (g_allargs.debug)
											printf("preamble:  addq.w #%d,%s\n",
												imm,
												it_treg->name.c_str()
												);
									}
									else
									{
										if (g_allargs.debug)
											printf("preamble:  subq.w #%d,%s\n",
												-imm,
												it_treg->name.c_str()
												);
									}

									curr = operations.insert(curr, new DR_AddSubQ16(*this, imm, rnew));
									r_limit -= (*curr)->cost;
									return true;
								}

								it_treg->L = save;
							}


							{
								uword_t saveH = it_treg->H;
								uword_t saveL = it_treg->L;

								ulong_t v = ulong_t(saveH) << 16 | saveL;
								v = ulong_t(long_t(v) + imm);

								it_treg->H = v >> 16;
								it_treg->L = v & 0xFFFF;

								if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
								{
									it_treg->H = saveH;
									it_treg->L = saveL;

									if (imm > 0)
									{
										if (g_allargs.debug)
											printf("preamble:  addq.l #%d,%s\n",
												imm,
												it_treg->name.c_str()
												);
									}
									else
									{
										if (g_allargs.debug)
											printf("preamble:  subq.l #%d,%s\n",
												-imm,
												it_treg->name.c_str()
												);
									}

									curr = operations.insert(curr, new DR_AddSubQ32(*this, imm, rnew));
									r_limit -= (*curr)->cost;
									return true;
								}

								it_treg->H = saveH;
								it_treg->L = saveL;
							}

						}
					}

					// now check all register-register transformations
					for (int rs = 0; rs < registers.size(); rs++)
					{
						if (!registers[rs].stableL)
							continue;

						uword_t tsrc16 = registers[rs].L;
						ulong_t tsrc32 = (ulong_t(registers[rs].H) << 16) | tsrc16;

						bool valid_tsrc32 = (_opsize == OpSize_32) && registers[rs].stableH;

						bool tsdata = (registers[rs].regclass == RegClass_Data);

						// MOVE.B Rx,Ry + ?
						if (valid_treg32 && tsdata)
						{
							uword_t save = it_treg->L;

							it_treg->L = (it_treg->L & 0xFF00) | (tsrc16 & 0xFF);

							if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
							{
								it_treg->L = save;

								if (g_allargs.debug)
									printf("preamble:  move.b %s,%s\n",
										registers[rs].name.c_str(),
										it_treg->name.c_str()
										);

								curr = operations.insert(curr, new DR_MoveR8(*this, rs, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}

							it_treg->L = save;
						}

						// MOVE.W Rx,Ry + ?
						if ((_opsize == OpSize_32) && valid_treg32)
						{
							it_treg->push();

							it_treg->L = tsrc16;

							//if (_dataH == 0x0001 && _dataL == 0x8000)
							//{
							//	__asm int 3;
							//}

							if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
							{
								it_treg->pop();

								if (g_allargs.debug)
									printf("preamble:  move.w %s,%s\n",
										registers[rs].name.c_str(),
										it_treg->name.c_str()
										);

								curr = operations.insert(curr, new DR_MoveR16(*this, rs, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}

							it_treg->pop();
						}

						// MOVE.L Rx,Ry + ?
						if ((_opsize == OpSize_32) && valid_tsrc32)
						{
							uword_t saveH = it_treg->H;
							uword_t saveL = it_treg->L;
							bool saveSH = it_treg->stableH;
							bool saveSL = it_treg->stableL;

							it_treg->H = registers[rs].H;
							it_treg->L = registers[rs].L;
							it_treg->stableH = true;
							it_treg->stableL = true;

							if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
							{
								it_treg->H = saveH;
								it_treg->L = saveL;
								it_treg->stableH = saveSH;
								it_treg->stableL = saveSL;

								if (g_allargs.debug)
									printf("preamble:  move.l %s,%s\n",
										registers[rs].name.c_str(),
										it_treg->name.c_str()
										);

								curr = operations.insert(curr, new DR_MoveR32(*this, rs, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}

							it_treg->H = saveH;
							it_treg->L = saveL;
							it_treg->stableH = saveSH;
							it_treg->stableL = saveSL;
						}

						// ADD.B Rx,Ry + ?
						if (valid_treg32 && tsdata)
						{
							uword_t save = it_treg->L;

							it_treg->L = (it_treg->L & 0xFF00) | (uword_t(it_treg->L + tsrc16) & 0xFF);

							if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
							{
								it_treg->L = save;

								if (g_allargs.debug)
									printf("preamble:  add.b %s,%s\n",
										registers[rs].name.c_str(),
										it_treg->name.c_str()
										);

								curr = operations.insert(curr, new DR_AddR8(*this, rs, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}

							it_treg->L = save;
						}

						// ADD.W Rx,Ry + ?
						if (valid_treg32)
						{
							uword_t save = it_treg->L;

							it_treg->L = uword_t(it_treg->L + tsrc16);

							if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
							{
								it_treg->L = save;

								if (g_allargs.debug)
									printf("preamble:  add.w %s,%s\n",
										registers[rs].name.c_str(),
										it_treg->name.c_str()
										);

								curr = operations.insert(curr, new DR_AddR16(*this, rs, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}

							it_treg->L = save;
						}

						// SUB.B Rx,Ry + ?
						if (valid_treg32 && tsdata)
						{
							uword_t save = it_treg->L;

							it_treg->L = (it_treg->L & 0xFF00) | ((byte_t(it_treg->L) - byte_t(tsrc16)) & 0xFF);

							if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
							{
								it_treg->L = save;

								if (g_allargs.debug)
									printf("preamble:  sub.b %s,%s\n",
										registers[rs].name.c_str(),
										it_treg->name.c_str()
										);

								curr = operations.insert(curr, new DR_SubR8(*this, rs, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}

							it_treg->L = save;
						}

						// SUB.W Rx,Ry + ?
						if (valid_treg32)
						{
							uword_t save = it_treg->L;

							it_treg->L = uword_t(word_t(it_treg->L) - word_t(tsrc16));

							if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
							{
								it_treg->L = save;

								if (g_allargs.debug)
									printf("preamble:  sub.w %s,%s\n",
										registers[rs].name.c_str(),
										it_treg->name.c_str()
										);

								curr = operations.insert(curr, new DR_SubR16(*this, rs, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}

							it_treg->L = save;
						}

						// AND.B Rx,Ry + ?
						if (valid_treg32 && tsdata)
						{
							uword_t save = it_treg->L;

							it_treg->L = (it_treg->L & 0xFF00) | ((it_treg->L & tsrc16) & 0xFF);

							if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
							{
								it_treg->L = save;

								if (g_allargs.debug)
									printf("preamble:  and.b %s,%s\n",
										registers[rs].name.c_str(),
										it_treg->name.c_str()
										);

								curr = operations.insert(curr, new DR_AndR8(*this, rs, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}

							it_treg->L = save;
						}

						// AND.W Rx,Ry + ?
						if (valid_treg32 && tsdata)
						{
							uword_t save = it_treg->L;

							it_treg->L = it_treg->L & tsrc16;

							if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
							{
								it_treg->L = save;

								if (g_allargs.debug)
									printf("preamble:  and.w %s,%s\n",
										registers[rs].name.c_str(),
										it_treg->name.c_str()
										);

								curr = operations.insert(curr, new DR_AndR16(*this, rs, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}

							it_treg->L = save;
						}

						// OR.W Rx,Ry + ?
						if (valid_treg32 && tsdata)
						{
							uword_t save = it_treg->L;

							it_treg->L = it_treg->L | tsrc16;

							if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
							{
								it_treg->L = save;

								if (g_allargs.debug)
									printf("preamble:  or.w %s,%s\n",
										registers[rs].name.c_str(),
										it_treg->name.c_str()
										);

								curr = operations.insert(curr, new DR_OrR16(*this, rs, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}

							it_treg->L = save;
						}

						// OR.B Rx,Ry + ?
						if (valid_treg32 && tsdata)
						{
							uword_t save = it_treg->L;

							it_treg->L = (it_treg->L & 0xFF00) | ((it_treg->L | tsrc16) & 0xFF);

							if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
							{
								it_treg->L = save;

								if (g_allargs.debug)
									printf("preamble:  or.b %s,%s\n",
										registers[rs].name.c_str(),
										it_treg->name.c_str()
										);

								curr = operations.insert(curr, new DR_OrR8(*this, rs, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}

							it_treg->L = save;
						}

						// EOR.W Rx,Ry + ?
						if (valid_treg32 && tsdata)
						{
							uword_t save = it_treg->L;

							it_treg->L = it_treg->L ^ tsrc16;

							if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
							{
								it_treg->L = save;

								if (g_allargs.debug)
									printf("preamble:  eor.w %s,%s\n",
										registers[rs].name.c_str(),
										it_treg->name.c_str()
										);

								curr = operations.insert(curr, new DR_XorR16(*this, rs, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}

							it_treg->L = save;
						}

						// EOR.B Rx,Ry + ?
						if (valid_treg32 && tsdata)
						{
							uword_t save = it_treg->L;

							it_treg->L = (it_treg->L & 0xFF00) | ((it_treg->L ^ tsrc16) & 0xFF);

							if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
							{
								it_treg->L = save;

								if (g_allargs.debug)
									printf("preamble:  eor.b %s,%s\n",
										registers[rs].name.c_str(),
										it_treg->name.c_str()
										);

								curr = operations.insert(curr, new DR_XorR8(*this, rs, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}

							it_treg->L = save;
						}
					}
				}

			} // (r_limit >= 8)


			// OPPORTUNITY: OPA Rp,Rs; OPB Rs,Rt
			if (r_limit >= 8)
			{
				// allow 8c compound operations for 32bit loads only
				if (!_2nd && (_opsize == OpSize_32) && (it_treg->regclass == RegClass_Data))
				{
					// iterate Rp
					for (std::vector<dataregister>::iterator it_preg = registers.begin(); it_preg != registers.end(); it_preg++)
					{
						// Rp is pure source, must have stable value
						if (!it_preg->stableL)
							continue;

						int rp = it_preg->index;

						uword_t preg16 = it_preg->L;
						ulong_t preg32 = (ulong_t(it_preg->H) << 16) | it_preg->L;

						bool valid_preg32 = (_opsize == OpSize_32) && it_preg->stableH;

						bool preg_isdata = (it_preg->regclass == RegClass_Data);

						// iterate Rs
						for (std::vector<dataregister>::iterator it_sreg = registers.begin(); it_sreg != registers.end(); it_sreg++)
						{
							// don't modify Rs if constant
							if (it_sreg->locked)
								continue;

							int rs = it_sreg->index;

							// don't modify Rs if involved in another operation
							if (tags_busy(curr, rs/*, OpSize_32*/))
								continue;

							uword_t sreg16 = it_sreg->L;
							ulong_t sreg32 = (ulong_t(it_sreg->H) << 16) | it_sreg->L;

//							bool valid_sreg16 = it_sreg->stableL;
							bool valid_sreg32 = (_opsize == OpSize_32) && it_sreg->stableH;

							bool sreg_isdata = (it_sreg->regclass == RegClass_Data);


							// MOVE.B Rp,Rs + OPB Rs,Rt
							if (preg_isdata && sreg_isdata && valid_sreg32)
							{
								it_sreg->push();

								it_sreg->L = (it_sreg->L & 0xFF00) | (preg16 & 0xFF);

								if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
								{
									it_sreg->pop();

									if (g_allargs.debug)
										printf("preamble:  move.b %s,%s\n",
											it_preg->name.c_str(),
											it_sreg->name.c_str()
											);

									curr = operations.insert(curr, new DR_MoveR8(*this, rp, rs));
									r_limit -= (*curr)->cost;
									return true;
								}

								it_sreg->pop();
							}

							// MOVE.W Rp,Rs + OPB Rs,Rt
							if (sreg_isdata && valid_sreg32) // preamble must be 4c to count so no addr reg word dest
							{
								it_sreg->push();

								it_sreg->L = preg16;

								if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
								{
									it_sreg->pop();

									if (g_allargs.debug)
										printf("preamble:  move.w %s,%s\n",
											it_preg->name.c_str(),
											it_sreg->name.c_str()
											);

									curr = operations.insert(curr, new DR_MoveR16(*this, rp, rs));
									r_limit -= (*curr)->cost;
									return true;
								}

								it_sreg->pop();
							}

							// MOVE.L Rp,Rs + OPB Rs,Rt
							if (sreg_isdata && valid_preg32)
							{
								it_sreg->push();

								it_sreg->H = it_preg->H;
								it_sreg->L = it_preg->L;
								it_sreg->stableH = true;
								it_sreg->stableL = true;

								if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
								{
									it_sreg->pop();

									if (g_allargs.debug)
										printf("preamble:  move.l %s,%s\n",
											it_preg->name.c_str(),
											it_sreg->name.c_str()
											);

									curr = operations.insert(curr, new DR_MoveR32(*this, rp, rs));
									r_limit -= (*curr)->cost;
									return true;
								}

								it_sreg->pop();
							}
							
							// ADD.B Rp,Rs + OPB Rs,Rt
							if (preg_isdata && sreg_isdata && valid_sreg32)
							{
								it_sreg->push();

								it_sreg->L = (it_sreg->L & 0xFF00) | (uword_t(word_t(it_sreg->L) + word_t(preg16)) & 0x00FF);

								if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
								{
									it_sreg->pop();

									if (g_allargs.debug)
										printf("preamble:  add.b %s,%s\n",
											it_preg->name.c_str(),
											it_sreg->name.c_str()
											);

									curr = operations.insert(curr, new DR_AddR8(*this, rp, rs));
									r_limit -= (*curr)->cost;
									return true;
								}

								it_sreg->pop();
							}

							// ADD.W Rp,Rs + OPB Rs,Rt
							if (sreg_isdata && valid_sreg32)
							{
								it_sreg->push();

								it_sreg->L = uword_t(word_t(it_sreg->L) + word_t(preg16));

								if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
								{
									it_sreg->pop();

									if (g_allargs.debug)
										printf("preamble:  add.w %s,%s\n",
											it_preg->name.c_str(),
											it_sreg->name.c_str()
											);

									curr = operations.insert(curr, new DR_AddR16(*this, rp, rs));
									r_limit -= (*curr)->cost;
									return true;
								}

								it_sreg->pop();
							}

							// SUB.B Rp,Rs + OPB Rs,Rt
							if (preg_isdata && sreg_isdata && valid_sreg32)
							{
								it_sreg->push();

								it_sreg->L = (it_sreg->L & 0xFF00) | ((byte_t(it_sreg->L) - byte_t(preg16)) & 0x00FF);

								if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
								{
									it_sreg->pop();

									if (g_allargs.debug)
										printf("preamble:  sub.b %s,%s\n",
											it_preg->name.c_str(),
											it_sreg->name.c_str()
											);

									curr = operations.insert(curr, new DR_SubR8(*this, rp, rs));
									r_limit -= (*curr)->cost;
									return true;
								}

								it_sreg->pop();
							}

							// SUB.W Rp,Rs + OPB Rs,Rt
							if (sreg_isdata && valid_sreg32)
							{
								it_sreg->push();

								it_sreg->L = uword_t(word_t(it_sreg->L) - word_t(preg16));

								if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
								{
									it_sreg->pop();

									if (g_allargs.debug)
										printf("preamble:  sub.w %s,%s\n",
											it_preg->name.c_str(),
											it_sreg->name.c_str()
											);

									curr = operations.insert(curr, new DR_SubR16(*this, rp, rs));
									r_limit -= (*curr)->cost;
									return true;
								}

								it_sreg->pop();
							}


							// AND.B Rp,Rs + OPB Rs,Rt
							if (preg_isdata && sreg_isdata && valid_sreg32)
							{
								it_sreg->push();

								it_sreg->L = (it_sreg->L & 0xFF00) | ((it_sreg->L & preg16) & 0x00FF);

								if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
								{
									it_sreg->pop();

									if (g_allargs.debug)
										printf("preamble:  and.b %s,%s\n",
											it_preg->name.c_str(),
											it_sreg->name.c_str()
											);

									curr = operations.insert(curr, new DR_AndR8(*this, rp, rs));
									r_limit -= (*curr)->cost;
									return true;
								}

								it_sreg->pop();
							}

							// AND.W Rp,Rs + OPB Rs,Rt
							if (preg_isdata && sreg_isdata && valid_sreg32)
							{
								it_sreg->push();

								it_sreg->L = it_sreg->L & preg16;

								if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
								{
									it_sreg->pop();

									if (g_allargs.debug)
										printf("preamble:  and.w %s,%s\n",
											it_preg->name.c_str(),
											it_sreg->name.c_str()
											);

									curr = operations.insert(curr, new DR_AndR16(*this, rp, rs));
									r_limit -= (*curr)->cost;
									return true;
								}

								it_sreg->pop();
							}

							// OR.B Rp,Rs + OPB Rs,Rt
							if (preg_isdata && sreg_isdata && valid_sreg32)
							{
								it_sreg->push();

								it_sreg->L = (it_sreg->L & 0xFF00) | ((it_sreg->L | preg16) & 0x00FF);

								if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
								{
									it_sreg->pop();

									if (g_allargs.debug)
										printf("preamble:  or.b %s,%s\n",
											it_preg->name.c_str(),
											it_sreg->name.c_str()
											);

									curr = operations.insert(curr, new DR_OrR8(*this, rp, rs));
									r_limit -= (*curr)->cost;
									return true;
								}

								it_sreg->pop();
							}

							// OR.W Rp,Rs + OPB Rs,Rt
							if (preg_isdata && sreg_isdata && valid_sreg32)
							{
								it_sreg->push();

								it_sreg->L = it_sreg->L | preg16;

								if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
								{
									it_sreg->pop();

									if (g_allargs.debug)
										printf("preamble:  or.w %s,%s\n",
											it_preg->name.c_str(),
											it_sreg->name.c_str()
											);

									curr = operations.insert(curr, new DR_OrR16(*this, rp, rs));
									r_limit -= (*curr)->cost;
									return true;
								}

								it_sreg->pop();
							}

							// EOR.B Rp,Rs + OPB Rs,Rt
							if (preg_isdata && sreg_isdata && valid_sreg32)
							{
								it_sreg->push();

								it_sreg->L = (it_sreg->L & 0xFF00) | ((it_sreg->L ^ preg16) & 0x00FF);

								if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
								{
									it_sreg->pop();

									if (g_allargs.debug)
										printf("preamble:  eor.b %s,%s\n",
											it_preg->name.c_str(),
											it_sreg->name.c_str()
											);

									curr = operations.insert(curr, new DR_XorR8(*this, rp, rs));
									r_limit -= (*curr)->cost;
									return true;
								}

								it_sreg->pop();
							}

							// EOR.W Rp,Rs + OPB Rs,Rt
							if (preg_isdata && sreg_isdata && valid_sreg32)
							{
								it_sreg->push();

								it_sreg->L = it_sreg->L ^ preg16;

								if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, r_limit, true))
								{
									it_sreg->pop();

									if (g_allargs.debug)
										printf("preamble:  eor.w %s,%s\n",
											it_preg->name.c_str(),
											it_sreg->name.c_str()
											);

									curr = operations.insert(curr, new DR_XorR16(*this, rp, rs));
									r_limit -= (*curr)->cost;
									return true;
								}

								it_sreg->pop();
							}
						}
					}
				}

			} // (r_limit >= 8)


			if (r_limit >= 12)
			{
				// OPPORTUNITY: (MOVE.W #IMM,Rn.L == EXPECTED)
				if ((it_treg->regclass == RegClass_Data) &&
					valid_treg32 &&
					(_dataH == it_treg->H)
				)
				{
					if (g_allargs.debug)
						printf("transform: move.l #$%08x,%s -> move.w #$%04x,%s [$%04x:%04x]\n",
							data32,
							registers[_rold].name.c_str(),
							_dataL,
							it_treg->name.c_str(),
							it_treg->H, _dataL
							);

					register_rename(curr, _rold, rnew);
					curr = operations.erase(curr);
					curr = operations.insert(curr, new DR_LoadI16(*this, rnew, _dataL));
					r_limit -= (*curr)->cost;
					return true;
				}

				// OPPORTUNITY: (LSL/LSR/ASL/ASR.W #N,Rx.W == EXPECTED)
				if ((it_treg->regclass == RegClass_Data) && equalH)
				{
					for (word_t i = 1; i <= 8; i++)
					{
						if (data16 == (it_treg->L << i))
						{
							if (_opsize == OpSize_16)
							{
								if (safe_rename_H(curr, it_treg, _rold))
								{
									if (g_allargs.debug)
										printf("transform: move.w #$%04x,%s -> lsl.w #%d,%s [$%04x<<%d]\n",
										data16,
										registers[_rold].name.c_str(),
										i,
										it_treg->name.c_str(),
										it_treg->L,
										i);

									register_rename(curr, _rold, rnew);
									curr = operations.erase(curr);
									curr = operations.insert(curr, new DR_LSI16(*this, -i, rnew));
									r_limit -= (*curr)->cost;
									return true;
								}
							}
							else //(_opsize == OpSize_32)
							{
								if (g_allargs.debug)
									printf("transform: move.l #$%04x%04x,%s -> lsl.w #%d,%s [$%04x<<%d]\n",
									_dataH, _dataL,
									registers[_rold].name.c_str(),
									i,
									it_treg->name.c_str(),
									it_treg->L,
									i);

								register_rename(curr, _rold, rnew);
								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_LSI16(*this, -i, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}
						}

						if (data16 == (it_treg->L >> i))
						{
							if (_opsize == OpSize_16)
							{
								if (safe_rename_H(curr, it_treg, _rold))
								{
									if (g_allargs.debug)
										printf("transform: move.w #$%04x,%s -> lsr.w #%d,%s [$%04x>>%d]\n",
										data16,
										registers[_rold].name.c_str(),
										i,
										it_treg->name.c_str(),
										it_treg->L,
										i);

									register_rename(curr, _rold, rnew);
									curr = operations.erase(curr);
									curr = operations.insert(curr, new DR_LSI16(*this, i, rnew));
									r_limit -= (*curr)->cost;
									return true;
								}
							}
							else // (_opsize == OpSize_32)
							{
								if (g_allargs.debug)
									printf("transform: move.l #$%04x%04x,%s -> lsr.w #%d,%s [$%04x>>%d]\n",
										_dataH, _dataL,
										registers[_rold].name.c_str(),
										i,
										it_treg->name.c_str(),
										it_treg->L,
										i);

								register_rename(curr, _rold, rnew);
								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_LSI16(*this, i, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}
						}

						if (word_t(data16) == word_t(word_t(it_treg->L) << i))
						{
							if (_opsize == OpSize_16)
							{
								if (safe_rename_H(curr, it_treg, _rold))
								{
									if (g_allargs.debug)
										printf("transform: move.w #$%04x,%s -> asl.w #%d,%s [$%04x<<%d]\n",
											data16,
											registers[_rold].name.c_str(),
											i,
											it_treg->name.c_str(),
											it_treg->L,
											i);

									register_rename(curr, _rold, rnew);
									curr = operations.erase(curr);
									curr = operations.insert(curr, new DR_ASI16(*this, -i, rnew));
									r_limit -= (*curr)->cost;
									return true;
								}
							}
							else
							{
								if (g_allargs.debug)
									printf("transform: move.l #$%04x%04x,%s -> asl.w #%d,%s [$%04x<<%d]\n",
										_dataH, _dataL,
										registers[_rold].name.c_str(),
										i,
										it_treg->name.c_str(),
										it_treg->L,
										i);

								register_rename(curr, _rold, rnew);
								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_ASI16(*this, -i, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}
						}

						if (word_t(data16) == word_t(word_t(it_treg->L) >> i))
						{
							if (_opsize == OpSize_16)
							{
								if (safe_rename_H(curr, it_treg, _rold))
								{
									if (g_allargs.debug)
										printf("transform: move.w #$%04x,%s -> asr.w #%d,%s [$%04x>>%d]\n",
										data16,
										registers[_rold].name.c_str(),
										i,
										it_treg->name.c_str(),
										it_treg->L,
										i);

									register_rename(curr, _rold, rnew);
									curr = operations.erase(curr);
									curr = operations.insert(curr, new DR_ASI16(*this, i, rnew));
									r_limit -= (*curr)->cost;
									return true;
								}
							}
							else
							{
								if (g_allargs.debug)
									printf("transform: move.l #$%04x%04x,%s -> asr.w #%d,%s [$%04x>>%d]\n",
										_dataH, _dataL,
										registers[_rold].name.c_str(),
										i,
										it_treg->name.c_str(),
										it_treg->L,
										i);

								register_rename(curr, _rold, rnew);
								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_ASI16(*this, i, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}
						}

						if (data16 == lofl16(it_treg->L, i))
						{
							if (_opsize == OpSize_16)
							{
								if (safe_rename_H(curr, it_treg, _rold))
								{
									if (g_allargs.debug)
										printf("transform: move.w #$%04x,%s -> rol.w #%d,%s [$%04x<-<%d]\n",
										data16,
										registers[_rold].name.c_str(),
										i,
										it_treg->name.c_str(),
										it_treg->L,
										i);

									register_rename(curr, _rold, rnew);
									curr = operations.erase(curr);
									curr = operations.insert(curr, new DR_ROI16(*this, -i, rnew));
									r_limit -= (*curr)->cost;
									return true;
								}
							}
							else
							{
								if (g_allargs.debug)
									printf("transform: move.l #$%04x%04x,%s -> rol.w #%d,%s [$%04x<-<%d]\n",
										_dataH, _dataL,
										registers[_rold].name.c_str(),
										i,
										it_treg->name.c_str(),
										it_treg->L,
										i);

								register_rename(curr, _rold, rnew);
								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_ROI16(*this, -i, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}

						}

						if (data16 == rofl16(it_treg->L, i))
						{
							if (_opsize == OpSize_16)
							{
								if (safe_rename_H(curr, it_treg, _rold))
								{
									if (g_allargs.debug)
										printf("transform: move.w #$%04x,%s -> ror.w #%d,%s [$%04x>->%d]\n",
										data16,
										registers[_rold].name.c_str(),
										i,
										it_treg->name.c_str(),
										it_treg->L,
										i);

									register_rename(curr, _rold, rnew);
									curr = operations.erase(curr);
									curr = operations.insert(curr, new DR_ROI16(*this, i, rnew));
									r_limit -= (*curr)->cost;
									return true;
								}
							}
							else
							{
								if (g_allargs.debug)
								printf("transform: move.l #$%04x%04x,%s -> ror.w #%d,%s [$%04x>->%d]\n",
									_dataH, _dataL,
									registers[_rold].name.c_str(),
									i,
									it_treg->name.c_str(),
									it_treg->L,
									i);

								register_rename(curr, _rold, rnew);
								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_ROI16(*this, i, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}
						}
					}
				}

				// OPPORTUNITY: (LSR/ROL.L #N,Rx.W == EXPECTED)
				if ((_opsize == OpSize_16) &&
					(it_treg->regclass == RegClass_Data) && valid_treg32 &&
					safe_rename_H(curr, it_treg, _rold))
				{
					for (word_t i = 1; i <= 8; i++)
					{						
						if (data16 == uword_t(treg32 >> i))
						{
							if (g_allargs.debug)
							printf("transform: move.w #$%04x,%s -> lsr.l #%d,%s [$%08x>>%d]\n",
								data16,
								registers[_rold].name.c_str(),
								i,
								it_treg->name.c_str(),
								treg32,
								i);

							register_rename(curr, _rold, rnew);
							curr = operations.erase(curr);
							curr = operations.insert(curr, new DR_LSI32(*this, i, rnew));
							r_limit -= (*curr)->cost;
							return true;
						}

						if (data32 == lofl32(treg32, i))
						{
							if (g_allargs.debug)
							printf("transform: move.w #$%04x,%s -> rol.l #%d,%s [$%08x<-<%d]\n",
								data16,
								registers[_rold].name.c_str(),
								i,
								it_treg->name.c_str(),
								treg32,
								i);

							register_rename(curr, _rold, rnew);
							curr = operations.erase(curr);
							curr = operations.insert(curr, new DR_ROI32(*this, -i, rnew));
							r_limit -= (*curr)->cost;
							return true;
						}
					}
				}

				// OPPORTUNITY: (LSL/LSR/ASL/ASR.L #N,Rx.L == EXPECTED)
				if ((it_treg->regclass == RegClass_Data) && valid_treg32)
				{
					for (word_t i = 1; i <= 8; i++)
					{
						ulong_t reg32 = (ulong_t(it_treg->H) << 16) | it_treg->L;
						
						if (data32 == (reg32 << i))
						{
							if (g_allargs.debug)
							printf("transform: move.l #$%04x%04x,%s -> lsl.l #%d,%s [$%08x<<%d]\n",
								_dataH, _dataL,
								registers[_rold].name.c_str(),
								i,
								it_treg->name.c_str(),
								reg32,
								i);

							register_rename(curr, _rold, rnew);
							curr = operations.erase(curr);
							curr = operations.insert(curr, new DR_LSI32(*this, -i, rnew));
							r_limit -= (*curr)->cost;
							return true;
						}

						if (data32 == (reg32 >> i))
						{
							if (g_allargs.debug)
							printf("transform: move.l #$%04x%04x,%s -> lsr.l #%d,%s [$%08x>>%d]\n",
								_dataH, _dataL,
								registers[_rold].name.c_str(),
								i,
								it_treg->name.c_str(),
								reg32,
								i);

							register_rename(curr, _rold, rnew);
							curr = operations.erase(curr);
							curr = operations.insert(curr, new DR_LSI32(*this, i, rnew));
							r_limit -= (*curr)->cost;
							return true;
						}

						if (long_t(data32) == (long_t(reg32) << i))
						{
							if (g_allargs.debug)
							printf("transform: move.l #$%04x%04x,%s -> asl.l #%d,%s [$%08x<<%d]\n",
								_dataH, _dataL,
								registers[_rold].name.c_str(),
								i,
								it_treg->name.c_str(),
								reg32,
								i);

							register_rename(curr, _rold, rnew);
							curr = operations.erase(curr);
							curr = operations.insert(curr, new DR_ASI32(*this, -i, rnew));
							r_limit -= (*curr)->cost;
							return true;
						}

						if (long_t(data32) == (long_t(reg32) >> i))
						{
							if (g_allargs.debug)
							printf("transform: move.l #$%04x%04x,%s -> asr.l #%d,%s [$%08x>>%d]\n",
								_dataH, _dataL,
								registers[_rold].name.c_str(),
								i,
								it_treg->name.c_str(),
								reg32,
								i);

							register_rename(curr, _rold, rnew);
							curr = operations.erase(curr);
							curr = operations.insert(curr, new DR_ASI32(*this, i, rnew));
							r_limit -= (*curr)->cost;
							return true;
						}

						if (data32 == lofl32(reg32, i))
						{
							if (g_allargs.debug)
							printf("transform: move.l #$%04x%04x,%s -> rol.l #%d,%s [$%08x<-<%d]\n",
								_dataH, _dataL,
								registers[_rold].name.c_str(),
								i,
								it_treg->name.c_str(),
								reg32,
								i);

							register_rename(curr, _rold, rnew);
							curr = operations.erase(curr);
							curr = operations.insert(curr, new DR_ROI32(*this, -i, rnew));
							r_limit -= (*curr)->cost;
							return true;
						}

						if (data32 == rofl32(reg32, i))
						{
							if (g_allargs.debug)
							printf("transform: move.l #$%04x%04x,%s -> ror.l #%d,%s [$%08x>->%d]\n",
								_dataH, _dataL,
								registers[_rold].name.c_str(),
								i,
								it_treg->name.c_str(),
								reg32,
								i);

							register_rename(curr, _rold, rnew);
							curr = operations.erase(curr);
							curr = operations.insert(curr, new DR_ROI32(*this, i, rnew));
							r_limit -= (*curr)->cost;
							return true;
						}
					}
				}

				// now check all register-register transformations
				for (int rs = 0; rs < registers.size(); rs++)
				{
					if (!registers[rs].stableL)
						continue;

					uword_t tsrc16 = registers[rs].L;
					ulong_t tsrc32 = (ulong_t(registers[rs].H) << 16) | tsrc16;

					bool valid_tsrc32 = (_opsize == OpSize_32) && registers[rs].stableH;
					
					bool tsdata = (registers[rs].regclass == RegClass_Data);

					if (tsdata)
					{
						if (_opsize == OpSize_16)
						{
							// OPPORTUNITY: (MULU.W Rx,Ry.W == EXPECTED)
							// note: looking to replace a 16bit load, but MULU.W is a 32bit operation with unavoidable destruction of upper word

							ulong_t result = it_treg->L * registers[rs].L;

							uword_t result_hw = uword_t(result >> 16);
							uword_t result_lw = uword_t(result);

							if ((it_treg->regclass == RegClass_Data) && (registers[rs].regclass == RegClass_Data) &&
								(data16 == result_lw) && 
								// don't perform moveq if it modifies a high word dependency
								//(((*curr)->alltagsH[_rold] < 0) || (registers[_rold].H == result_hw)))
								// don't permit rename if it affects a high word dependency - unless high words are stable and equal
								safe_rename_H(curr, result_hw, _rold))
							{

//							if ((it_treg->regclass == RegClass_Data) && (registers[rs].regclass == RegClass_Data) &&
//								(data16 == (result & 0x0000FFFF))
//								)
//							{
								if (g_allargs.debug)
								printf("transform: move.w #$%04x,%s -> mulu.w %s,%s [$%04x*$%04x]\n",
									data16,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									it_treg->L,
									registers[rs].L
									);

								register_rename(curr, _rold, rnew);
								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_MuluR16(*this, rs, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}
						}
						else
						{
							// OPPORTUNITY: (MULU.W Rx,Ry.L == EXPECTED)

							ulong_t result = it_treg->L * registers[rs].L;

							if ((it_treg->regclass == RegClass_Data) && (registers[rs].regclass == RegClass_Data) &&
								(data32 == result)
								)
							{
								if (g_allargs.debug)
								printf("transform: move.l #$%04x%04x,%s -> mulu.w %s,%s [$%04x*$%04x]\n",
									_dataH, _dataL,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									it_treg->L,
									registers[rs].L
									);

								register_rename(curr, _rold, rnew);
								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_MuluR16(*this, rs, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}
						}

						if (_opsize == OpSize_16)
						{
							// OPPORTUNITY: (MULS.W Rx,Ry.W == EXPECTED)
							// note: looking to replace a 16bit load, but MULS.W is a 32bit operation with unavoidable destruction of upper word

							long_t result = word_t(it_treg->L) * word_t(registers[rs].L);

							uword_t result_hw = uword_t(result >> 16);
							uword_t result_lw = uword_t(result);

							if ((it_treg->regclass == RegClass_Data) && (registers[rs].regclass == RegClass_Data) &&
								(data16 == result_lw) && 
								// don't perform moveq if it modifies a high word dependency
								//(((*curr)->alltagsH[_rold] < 0) || (registers[_rold].H == result_hw)))
								// don't permit rename if it affects a high word dependency - unless high words are stable and equal
								safe_rename_H(curr, result_hw, _rold))
							{

//							if ((it_treg->regclass == RegClass_Data) && (registers[rs].regclass == RegClass_Data) &&
//								(data16 == uword_t(result & 0x0000FFFF))
//								)
//							{
								if (g_allargs.debug)
								printf("transform: move.w #$%04x,%s -> muls.w %s,%s [$%04x*$%04x]\n",
									data16,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									it_treg->L,
									registers[rs].L
									);

								register_rename(curr, _rold, rnew);
								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_MulsR16(*this, rs, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}
						}
						else
						{
							// OPPORTUNITY: (MULS.W Rx,Ry.L == EXPECTED)

							long_t result = word_t(it_treg->L) * word_t(registers[rs].L);

							if ((it_treg->regclass == RegClass_Data) && (registers[rs].regclass == RegClass_Data) &&
								(data32 == ulong_t(result))
								)
							{
								if (g_allargs.debug)
								printf("transform: move.l #$%04x%04x,%s -> muls.w %s,%s [$%04x*$%04x]\n",
									_dataH, _dataL,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									it_treg->name.c_str(),
									it_treg->L,
									registers[rs].L
									);

								register_rename(curr, _rold, rnew);
								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_MulsR16(*this, rs, rnew));
								r_limit -= (*curr)->cost;
								return true;
							}
						}
					}
				}

			}  // (r_limit >= 12)

		}
	}

	return false;
}

bool Context::try_12c_init_transforms(std::list<Operation*>::iterator &curr, OpSize _opsize, int _rold, uword_t _dataH, uword_t _dataL)
{
	// can't transform variable registers
	if (registers[_rold].variable)
		return false;

	uword_t data16 = _dataL;
	ulong_t data32 = (ulong_t(_dataH) << 16) | _dataL;

	// initialize existing register

#if (USE_MOVEQ_INITS)
	if (_opsize == OpSize_32)
	{
		// OPPORTUNITY: (MOVEQ.L #N,Rn.L/XXX Rn.L == EXPECTED)
		if ((registers[_rold].regclass == RegClass_Data)
		)
		{
			for (int imm = -128; imm < 128; imm++)
			{

				{
					ulong_t uimm =  ulong_t(imm);
					uword_t uimmU = uimm >> 16;

					// now check all register-register transformations
					for (int rs = 0; rs < registers.size(); rs++)
					{
						if (!registers[rs].stableL)
							continue;

						{
							ulong_t v = ulong_t(imm) ^ (ulong_t(1) << (registers[rs].L & 31));

							// OPPORTUNITY: (BCHG Rx,Ry.L == EXPECTED)
							if ((registers[rs].regclass == RegClass_Data) &&
								(data32 == v)
							)
							{
								if (g_allargs.debug)
								printf("transform: move.l #$%04x%04x,%s -> moveq.l #%d,%s + bchg %s,%s [$%08x^$%08x]\n",
									_dataH, _dataL,
									registers[_rold].name.c_str(),
									imm,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									registers[_rold].name.c_str(),
									imm,
									(ulong_t(1) << (registers[rs].L & 31))
									);

								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_BchgR(*this, OpSize_32, rs, _rold));
								curr = operations.insert(curr, new DR_MoveQ32(*this, imm, _rold));
								return true;
							}
						}

						{
							ulong_t v = uword_t(imm) * registers[rs].L;

							// OPPORTUNITY: (MULU.W Rx,Ry.L == EXPECTED)
							if ((registers[rs].regclass == RegClass_Data) &&
								(data32 == v)
							)
							{
								if (g_allargs.debug)
								printf("transform: move.l #$%04x%04x,%s -> moveq.l #%d,%s + mulu.w %s,%s [$%08x*$%04x]\n",
									_dataH, _dataL,
									registers[_rold].name.c_str(),
									imm,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									registers[_rold].name.c_str(),
									imm,
									registers[rs].L
									);

								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_MuluR16(*this, rs, _rold));
								curr = operations.insert(curr, new DR_MoveQ32(*this, imm, _rold));
								return true;
							}
						}

						{
							ulong_t v = ulong_t(word_t(imm) * word_t(registers[rs].L));

							// OPPORTUNITY: (MULS.W Rx,Ry.L == EXPECTED)
							if ((registers[rs].regclass == RegClass_Data) &&
								(data32 == v)
							)
							{
								if (g_allargs.debug)
								printf("transform: move.l #$%04x%04x,%s -> moveq.l #%d,%s + muls.w %s,%s [$%08x*$%04x]\n",
									_dataH, _dataL,
									registers[_rold].name.c_str(),
									imm,
									registers[_rold].name.c_str(),
									registers[rs].name.c_str(),
									registers[_rold].name.c_str(),
									imm,
									registers[rs].L
									);

								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_MulsR16(*this, rs, _rold));
								curr = operations.insert(curr, new DR_MoveQ32(*this, imm, _rold));
								return true;
							}
						}
					}
				}

				{
					for (word_t i = 1; i <= 8; i++)
					{
						{
							ulong_t v = (ulong_t(imm) & 0xFFFF0000) | (uword_t(imm & 0xFFFF) << i);

							if (data32 == v)
							{
								if (g_allargs.debug)
								printf("transform: move.l #$%04x%04x,%s -> moveq #%d,%s + lsl.w #%d,%s [$%04x<<%d]\n",
									_dataH, _dataL,
									registers[_rold].name.c_str(),
									imm,
									registers[_rold].name.c_str(),
									i,
									registers[_rold].name.c_str(),
									uword_t(imm),
									i);

								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_LSI16(*this, -i, _rold));
								curr = operations.insert(curr, new DR_MoveQ32(*this, imm, _rold));
								return true;
							}
						}

						{
							ulong_t v = (ulong_t(imm) & 0xFFFF0000) | (uword_t(imm & 0xFFFF) >> i);

							if (data32 == v)
							{
								if (g_allargs.debug)
								printf("transform: move.l #$%04x%04x,%s -> moveq #%d,%s + lsr.w #%d,%s [$%04x>>%d]\n",
									_dataH, _dataL,
									registers[_rold].name.c_str(),
									imm,
									registers[_rold].name.c_str(),
									i,
									registers[_rold].name.c_str(),
									uword_t(imm),
									i);

								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_LSI16(*this, i, _rold));
								curr = operations.insert(curr, new DR_MoveQ32(*this, imm, _rold));
								return true;
							}
						}

						{
							ulong_t v = (ulong_t(imm) & 0xFFFF0000) | (word_t(imm & 0xFFFF) << i);

							if (data32 == v)
							{
								if (g_allargs.debug)
								printf("transform: move.l #$%04x%04x,%s -> moveq #%d,%s + asl.w #%d,%s [$%04x<<%d]\n",
									_dataH, _dataL,
									registers[_rold].name.c_str(),
									imm,
									registers[_rold].name.c_str(),
									i,
									registers[_rold].name.c_str(),
									uword_t(imm),
									i);

								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_ASI16(*this, -i, _rold));
								curr = operations.insert(curr, new DR_MoveQ32(*this, imm, _rold));
								return true;
							}
						}

						{
							ulong_t v = (ulong_t(imm) & 0xFFFF0000) | (word_t(imm & 0xFFFF) >> i);

							if (data32 == v)
							{
								if (g_allargs.debug)
								printf("transform: move.l #$%04x%04x,%s -> moveq #%d,%s + asr.w #%d,%s [$%04x>>%d]\n",
									_dataH, _dataL,
									registers[_rold].name.c_str(),
									imm,
									registers[_rold].name.c_str(),
									i,
									registers[_rold].name.c_str(),
									uword_t(imm),
									i);

								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_ASI16(*this, i, _rold));
								curr = operations.insert(curr, new DR_MoveQ32(*this, imm, _rold));
								return true;
							}
						}

						{
							ulong_t v = (ulong_t(imm) & 0xFFFF0000) | (_rotl16(word_t(imm & 0xFFFF), i));

							if (data32 == v)
							{
								if (g_allargs.debug)
								printf("transform: move.l #$%04x%04x,%s -> moveq #%d,%s + rol.w #%d,%s [$%04x<-<%d]\n",
									_dataH, _dataL,
									registers[_rold].name.c_str(),
									imm,
									registers[_rold].name.c_str(),
									i,
									registers[_rold].name.c_str(),
									uword_t(imm),
									i);

								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_ROI16(*this, -i, _rold));
								curr = operations.insert(curr, new DR_MoveQ32(*this, imm, _rold));
								return true;
							}
						}

						{
							ulong_t v = (ulong_t(imm) & 0xFFFF0000) | (_rotr16(word_t(imm & 0xFFFF), i));

							if (data32 == v)
							{
								if (g_allargs.debug)
								printf("transform: move.l #$%04x%04x,%s -> moveq #%d,%s + ror.w #%d,%s [$%04x>->%d]\n",
									_dataH, _dataL,
									registers[_rold].name.c_str(),
									imm,
									registers[_rold].name.c_str(),
									i,
									registers[_rold].name.c_str(),
									uword_t(imm),
									i);

								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_ROI16(*this, i, _rold));
								curr = operations.insert(curr, new DR_MoveQ32(*this, imm, _rold));
								return true;
							}
						}

						{
							ulong_t v = (ulong_t(imm) << i);

							if (data32 == v)
							{
								if (g_allargs.debug)
								printf("transform: move.l #$%04x%04x,%s -> moveq #%d,%s + lsl.l #%d,%s [$%08x<<%d]\n",
									_dataH, _dataL,
									registers[_rold].name.c_str(),
									imm,
									registers[_rold].name.c_str(),
									i,
									registers[_rold].name.c_str(),
									imm,
									i);

								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_LSI32(*this, -i, _rold));
								curr = operations.insert(curr, new DR_MoveQ32(*this, imm, _rold));
								return true;
							}
						}

						{
							ulong_t v = (ulong_t(imm) >> i);

							if (data32 == v)
							{
								if (g_allargs.debug)
								printf("transform: move.l #$%04x%04x,%s -> moveq #%d,%s + lsr.l #%d,%s [$%08x>>%d]\n",
									_dataH, _dataL,
									registers[_rold].name.c_str(),
									imm,
									registers[_rold].name.c_str(),
									i,
									registers[_rold].name.c_str(),
									imm,
									i);

								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_LSI32(*this, i, _rold));
								curr = operations.insert(curr, new DR_MoveQ32(*this, imm, _rold));
								return true;
							}
						}

						{
							ulong_t v = (long_t(imm) << i);

							if (data32 == v)
							{
								if (g_allargs.debug)
								printf("transform: move.l #$%04x%04x,%s -> moveq #%d,%s + asl.l #%d,%s [$%08x<<%d]\n",
									_dataH, _dataL,
									registers[_rold].name.c_str(),
									imm,
									registers[_rold].name.c_str(),
									i,
									registers[_rold].name.c_str(),
									imm,
									i);

								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_ASI32(*this, -i, _rold));
								curr = operations.insert(curr, new DR_MoveQ32(*this, imm, _rold));
								return true;
							}
						}

						{
							ulong_t v = (long_t(imm) >> i);

							if (data32 == v)
							{
								if (g_allargs.debug)
								printf("transform: move.l #$%04x%04x,%s -> moveq #%d,%s + asr.l #%d,%s [$%08x>>%d]\n",
									_dataH, _dataL,
									registers[_rold].name.c_str(),
									imm,
									registers[_rold].name.c_str(),
									i,
									registers[_rold].name.c_str(),
									imm,
									i);

								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_ASI32(*this, i, _rold));
								curr = operations.insert(curr, new DR_MoveQ32(*this, imm, _rold));
								return true;
							}
						}

						{
							ulong_t v = _rotl(imm, i);

							if (data32 == v)
							{
								if (g_allargs.debug)
								printf("transform: move.l #$%04x%04x,%s -> moveq #%d,%s + rol.l #%d,%s [$%08x<-<%d]\n",
									_dataH, _dataL,
									registers[_rold].name.c_str(),
									imm,
									registers[_rold].name.c_str(),
									i,
									registers[_rold].name.c_str(),
									imm,
									i);

								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_ROI32(*this, -i, _rold));
								curr = operations.insert(curr, new DR_MoveQ32(*this, imm, _rold));
								return true;
							}
						}

						{
							ulong_t v = _rotr(imm, i);

							if (data32 == v)
							{
								if (g_allargs.debug)
								printf("transform: move.l #$%04x%04x,%s -> moveq #%d,%s + ror.l #%d,%s [$%08x>->%d]\n",
									_dataH, _dataL,
									registers[_rold].name.c_str(),
									imm,
									registers[_rold].name.c_str(),
									i,
									registers[_rold].name.c_str(),
									imm,
									i);

								curr = operations.erase(curr);
								curr = operations.insert(curr, new DR_ROI32(*this, i, _rold));
								curr = operations.insert(curr, new DR_MoveQ32(*this, imm, _rold));
								return true;
							}
						}
					}
				}
			}
		}
	}
#endif

	return false;
}

bool Context::try_8c_init_transforms(std::list<Operation*>::iterator &curr, OpSize _opsize, int _rold, uword_t _dataH, uword_t _dataL)
{
	// can't transform variable registers
	if (registers[_rold].variable)
		return false;

	std::vector<dataregister>::iterator it_treg = registers.begin() + _rold;

	uword_t data16 = _dataL;
	ulong_t data32 = (ulong_t(_dataH) << 16) | _dataL;

	// initialize existing register

	if (_opsize == OpSize_32)
	{
		// OPPORTUNITY: compound MOVEQ.L + ???
		if ((_opsize == OpSize_32) && (it_treg->regclass == RegClass_Data))
		{
			// MOVEQ.L + ?
			for (int imm = -128; imm < 128; imm++)
			{
				it_treg->push();

				it_treg->H = word_t(imm >> 16);
				it_treg->L = word_t(imm);
				it_treg->stableH = true;
				it_treg->stableL = true;

				int limit = 4;
				if (try_reg_transforms(curr, it_treg, _opsize, _rold, _dataH, _dataL, limit, true))
				{
					it_treg->pop();

					if (g_allargs.debug)
					printf("preamble:  moveq.l #%d,%s\n",
						imm,
						it_treg->name.c_str()
						);

					curr = operations.insert(curr, new DR_MoveQ32(*this, imm, _rold));
					return true;
				}

				it_treg->pop();
			}
		}
	}

	return false;
}

bool Context::try_4c_init_transforms(std::list<Operation*>::iterator &curr, OpSize _opsize, int _rold, uword_t _dataH, uword_t _dataL)
{
	// can't transform variable registers
	if (registers[_rold].variable)
		return false;

	std::vector<dataregister>::iterator it_treg = registers.begin() + _rold;

	uword_t data16 = _dataL;
	ulong_t data32 = (ulong_t(_dataH) << 16) | _dataL;

	// initialize existing register

	if (_opsize == OpSize_16)
	{
		// prefer SWAP to CLR/MOVEQ because it moves data around to help other transforms
		// note: looking to replace a 16bit load, but SWAP is a 32bit operation
		// OPPORTUNITY: (SWAP Rx.W == EXPECTED)

		if ((registers[_rold].regclass == RegClass_Data) && 
			registers[_rold].stableH && (registers[_rold].H == data16) &&
			// don't perform swap if it modifies a high word dependency
			((*curr)->alltagsH[_rold] < 0)
		)
		{
			if (g_allargs.debug)
			printf("transform: move.w #$%04x,%s -> swap %s [$%04x<:>$%04x]\n",
				data16,
				registers[_rold].name.c_str(),
				registers[_rold].name.c_str(),
				registers[_rold].H, registers[_rold].L
				);

			curr = operations.erase(curr);
			curr = operations.insert(curr, new DR_SwapR32(*this, _rold));
			return true;
		}

		// OPPORTUNITY: (MOVEQ.L #N,Rn.W == EXPECTED)
		// note: prefer MOVEQ to CLR when H word is not yet initialized
		if ((registers[_rold].regclass == RegClass_Data) &&
			!(registers[_rold].stableH)
		)
		{
//#if (USE_MOVEQ_INITS)
			for (int imm = -128; imm < 128; imm++)
			{
//				if (data16 == uword_t(imm))
				uword_t imm_hw = uword_t(imm >> 16);
				uword_t imm_lw = uword_t(imm);

				if ((data16 == imm_lw) && 
					// don't perform moveq if it modifies a high word dependency
					//(/*!registers[_rold].stableH*/((*curr)->alltagsH[_rold] < 0) || (registers[_rold].H == imm_hw)))
					// don't permit rename if it affects a high word dependency - unless high words are stable and equal
					safe_rename_H(curr, imm_hw, _rold))
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
					return true;
				}
			}
//#endif
		}

		// OPPORTUNITY: (CLR.W Rn == EXPECTED)
		if ((registers[_rold].regclass == RegClass_Data) &&
			(data16 == 0)
		)
		{
			if (g_allargs.debug)
			printf("transform: move.w #$0000,%s -> clr.w %s [$%04x]\n",
				registers[_rold].name.c_str(),
				registers[_rold].name.c_str(),
				registers[_rold].L
				);

			// replace operation with equivalent CLR.W
			curr = operations.erase(curr);
			curr = operations.insert(curr, new DR_Clr16(*this, _rold));
			return true;
		}
	}
	else // (_opsize == OpSize_32)
	{
//#if (USE_MOVEQ_INITS)
		// OPPORTUNITY: (MOVEQ.L #N,Rn.L/XXX Rn.L == EXPECTED)
		if ((registers[_rold].regclass == RegClass_Data)
		)
		{
			for (int imm = -128; imm < 128; imm++)
			{
				// OPPORTUNITY: (MOVEQ.L #N,Rn.L == EXPECTED)
				if (data32 == ulong_t(imm))
				{
					if (g_allargs.debug)
					printf("transform: move.l #$%08x,%s -> moveq.l #%d,%s [$%04x%04x]\n",
						data32,
						registers[_rold].name.c_str(),
						imm,
						registers[_rold].name.c_str(),
						registers[_rold].H, registers[_rold].L
						);

					curr = operations.erase(curr);
					curr = operations.insert(curr, new DR_MoveQ32(*this, imm, _rold));
					return true;
				}
			}
		}
//#endif
	}

	return false;
}

//---------------------------------------------------------------------------------------------------------------------
