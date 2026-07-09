
void generate_emx_frame(spriteframe_t &spr, int _frame)
{
	if (g_allargs.verbose)
		std::cout << "frame [" << _frame << "]" << std::endl << std::flush;

	// default line order
	std::vector<int> best_pattern;
	std::vector<int> best_invpattern;
	for (int f = 0; f < spr.h_; f++)
	{
		best_pattern.push_back(f);
	}

	static const int c_max_ems_components = 320 / 48;

	ubyte_t *emptylines = new ubyte_t[spr.h_];
	memset(emptylines, 0, spr.h_);

	ubyte_t *livelines = new ubyte_t[spr.h_];
	memset(livelines, 0, spr.h_);

	// find common optimized line order
	if (USE_COMMON_LINEORDER_OPTIMIZATION && (spr.h_ > 1))
	{
		if (g_allargs.verbose)
			std::cout << "optimizing common line order across all preshifts..." << std::endl << std::flush;

		int shiftcount = spr.psmask_frames_emx_.size();

		// make copy of data in terms of 3*y EM words
		uword_t *buffer = new uword_t[3 * spr.h_ * shiftcount * c_max_ems_components];
		memset(buffer, 0, sizeof(uword_t) * 3 * spr.h_ * shiftcount * c_max_ems_components);

		int max_ci = 0;
		{
			int si = 0;
			std::vector<preshiftlist_t>::iterator pmit = spr.psmask_frames_emx_.begin();
			for (; pmit != spr.psmask_frames_emx_.end(); pmit++, si++)
			{
				// components within a preshift
				int ci = 0;
				for (preshiftlist_t::iterator cit = pmit->begin(); cit != pmit->end(); cit++, ci++)
				{
					if (ci > max_ci)
						max_ci = ci;

					preshift_t &shift = *cit;

					static const int pagesize = (3 * spr.h_);
					int page = ((ci * shiftcount) + si) * pagesize;
					uword_t *sbuffer = &buffer[page];

					//				uword_t *sbuffer = buffer;

					for (int v = 0; v < spr.h_; v++)
					{
						sbuffer[(v * 3) + 0] = shift.data_[(v * shift.wordwidth_) + 0];

						if (shift.wordwidth_ == 2)
						{
							// note: em1,em3 used in 2-wide blits. em2 is skipped
							sbuffer[(v * 3) + 1] = 0;
							sbuffer[(v * 3) + 2] = shift.data_[(v * shift.wordwidth_) + 1];
						}
						else
							if (shift.wordwidth_ == 3)
							{
								sbuffer[(v * 3) + 1] = shift.data_[(v * shift.wordwidth_) + 1];
								sbuffer[(v * 3) + 2] = shift.data_[(v * shift.wordwidth_) + 2];
							}

						// track all non-empty lines in any preshift
						if (USE_EMPTYLINE_OPTIMIZATION)
						{
							if (sbuffer[(v * 3) + 0] || sbuffer[(v * 3) + 1] || sbuffer[(v * 3) + 2])
								livelines[v] = 1;
							else
								emptylines[v] = 1;
						}
						else
						{
							livelines[v] = 1;
							emptylines[v] = 0;
						}
					}
				}
			}
		}

		// force partially (& fully) empty lines to end of pattern
#if (0)
		{
			bool busy;
			do
			{
				busy = false;

				for (int v = 0; v < (spr.h_ - 1); v++)
				{
					int v0 = best_pattern[v + 0];
					int v1 = best_pattern[v + 1];

//					if (!livelines[v0] && livelines[v1])
					if (emptylines[v0] && !emptylines[v1])
					{
						best_pattern[v + 0] = v1;
						best_pattern[v + 1] = v0;
						busy = true;
						break;
					}
				}

			} while (busy);
		}
#endif

#if (1)
		// force fully empty lines to end of pattern
		{
			bool busy;
			do
			{
				busy = false;

				for (int v = 0; v < (spr.h_ - 1); v++)
				{
					int v0 = best_pattern[v + 0];
					int v1 = best_pattern[v + 1];

					if (!livelines[v0] && livelines[v1])
					{
						best_pattern[v + 0] = v1;
						best_pattern[v + 1] = v0;
						busy = true;
						break;
					}
				}

			} while (busy);
		}
#endif

		// find active region
		int live_line_count = best_pattern.size();
		for (int v = 0; v < best_pattern.size(); v++)
		{
			if (!livelines[best_pattern[v]])
			{
				live_line_count = v;
				break;
			}
		}
		int empty_line_count = spr.h_ - live_line_count;


		// initial sort of mask edges
		if (USE_INITIAL_EDGE_SORT)
		{
			std::vector<std::pair<ulong_t,int>> sorting;
			sorting.resize(live_line_count);
			for (int v = 0; v < live_line_count; v++)
			{
				auto &s = sorting[v];
				s.first = 0;
				s.second = best_pattern[v];
			}

			int full_src_wordwidth = ((spr.w_ + 15) & -16) >> 4;
			int src_pixwidth = full_src_wordwidth << 4;

			for (int v = 0; v < live_line_count; v++)
			{
				int dsty = v;// best_pattern[v];
				uword_t *line = &spr.mask_[(dsty * full_src_wordwidth)];

				int le = 0;
				int re = 0;
				int mle = 0;
				int mre = 0;

				for (int x = 0; x < src_pixwidth; x++)
				{
					uword_t m = ~(line[x >> 4]);
					if (m & (0x8000 >> (x & 0xF)))
					{
						le = x;

						for (int xx = x; xx < src_pixwidth; xx++)
						{
							uword_t m = ~(line[xx >> 4]);
							if (!(m & (0x8000 >> (xx & 0xF))))
							{
								mle = xx; 
								break;
							}
						}

						break;
					}
				}
				for (int x = src_pixwidth-1; x >= 0; x--)
				{
					uword_t m = ~(line[x >> 4]);
					if (m & (0x8000 >> (x & 0xF)))
					{
						re = x; 

						for (int xx = x; xx >= 0; xx--)
						{
							uword_t m = ~(line[xx >> 4]);
							if (!(m & (0x8000 >> (xx & 0xF))))
							{
								mre = xx; 
								break;
							}
						}

						break;
					}
				}

				sorting[v].first =
					((le & 0x1FF) << 23) | 
					((re & 0x1FF) << 14) | 
					((mle & 0x7F) << 7) | 
					((mre & 0x7F) << 0);

				//std::cout << "le:" << le << " re:" << re << std::endl;
			}
			std::sort(sorting.begin(), sorting.end());
			for (int vnew = 0; vnew < live_line_count; vnew++)
			{
				best_pattern[vnew] = sorting[vnew].second;
			}
		}

//		exit(1);




/*
		int partial_line_count = 0;
		for (int v = 0; v < best_pattern.size(); v++)
		{
			if (livelines[best_pattern[v]] && emptylines[best_pattern[v]])
				partial_line_count++;
		}
*/
		if (g_allargs.verbose)
		{
			printf("live lines = %d\n", live_line_count);
			//printf("partial lines = %d\n", partial_line_count);
			printf("empty lines = %d\n", empty_line_count);
		}

		// optimize the line order for NON EMPTY LINES to minimize changes to EM registers
		// the empty lines are already at the end of the sequence so optimization works on a shorter sequence (up to live_line_count)
	
		{
			// optimize line pattern by reordering
			
			if (!g_allargs.quickcut)
			{
				mersenne_twister twist;

#if (USE_NOISY_REORDERING)
				unsigned long noise_factor = (1 << (32-7));
#endif
				double initial_dbest_cost = -1;
				int initial_best_loadcost = -1;

				// begin with default line pattern (top-to-bottom)
				double dbest_cost = double(1e20);
				int best_loadcost = 1<<20;
				ulong_t last_improvement = 0;

				std::vector<int> trial_pattern(best_pattern);

				ulong_t darwin_limit = 1 << g_allargs.emxoptlevel;
				for (ulong_t trial = 0, depth = 0; depth < darwin_limit; trial++, depth++)
				{
					for (int r = best_pattern.size() - 1; r >= 0; r--)
						trial_pattern[r] = best_pattern[r];

					// select mutation rate
					//				int mutrate = ((twist.genS32() >> 4) % 4) + 1;

					// we want to measure improvement from initial sort, so trial 0
					// skips mutation
					if (trial > 0)
					{

						int mutrate = 1 + ((twist.genS32() >> 4) % 8);
						mutrate = 1 + ((mutrate * mutrate) >> 3);

						int choice1 = 0;
						int choice2 = 0;

						for (int mut = 0; mut < mutrate; mut++)
						{
							do
							{
								// select pair to exchange
								choice1 = (twist.genU32() >> 4) % live_line_count;
								choice2 = (twist.genU32() >> 4) % live_line_count;

								// don't reorder against self
							} while ((choice1 == choice2) ||
								// don't place an empty line as the first 
								// line to render, otherwise individual components won't skip initial colour data
								((choice2 == 0) && (0 == livelines[trial_pattern[choice1]])) ||
								((choice1 == 0) && (0 == livelines[trial_pattern[choice2]])));

							// reorder
							//						int tmp = trial_pattern[choice1];
							//						trial_pattern[choice1] = trial_pattern[choice2];
							//						trial_pattern[choice2] = tmp;

							// bias sorting in one direction
							if (choice1 > choice2)
							{
								choice1 ^= choice2;
								choice2 ^= choice1;
								choice1 ^= choice2;
							}

							//if (choice1 < choice2)
							{
								// A		A
								// B <c1	D \
															// C		B | o
								// D <c2	C / o+1  tmp
								// E		E

								// push choice2 int choice1, moving all downwards
								int tmp = trial_pattern[choice2];
								for (int o = choice2 - 1; o >= choice1; o--)
									trial_pattern[o + 1] = trial_pattern[o];
								trial_pattern[choice1] = tmp;
							}
							/*	
							else
							{
							// A		A
							// B <c2	C \ o-1
							// C		D | o
							// D <c1	B /      tmp
							// E		E

							// push choice2 into choice1, moving all upwards
							int tmp = trial_pattern[choice2];
							for (int o = choice2 + 1; o <= choice1; o++)
							trial_pattern[o - 1] = trial_pattern[o];
							trial_pattern[choice1] = tmp;

							// swap
							//							int tmp = trial_pattern[choice1];
							//							trial_pattern[choice1] = trial_pattern[choice2];
							//							trial_pattern[choice2] = tmp;
							}
							*/
						} // for mutrate

					} // trial > 0

					// estimate efficiency of new pattern in terms of deltas per line
					{
						//int totalcost = 0;
						double dtotalcost = 0;
						int loadcost = 0;

						int shiftcount = spr.psmask_frames_emx_.size();
						for (int si = 0; si < shiftcount; si++)
						{
							for (int ci = 0; ci <= max_ci; ci++)
							{
								static const int pagesize = (3 * spr.h_);
								int page = ((ci * shiftcount) + si) * pagesize;
								uword_t *sbuffer = &buffer[page];

								//						uword_t *sbuffer = &buffer[3 * spr.h_ * si * ci];
								//						uword_t *sbuffer = buffer;

								for (int v = 0; v < (spr.h_ - 1); v++)
								{
									int v0 = trial_pattern[v + 0];
									int v1 = trial_pattern[v + 1];
									int yp = v0 * 3;
									int yn = v1 * 3;
									int lc1 = (sbuffer[yp + 0] != sbuffer[yn + 0]) ? 1 : 0;
									int lc2 = (sbuffer[yp + 1] != sbuffer[yn + 1]) ? 1 : 0;
									int lc3 = (sbuffer[yp + 2] != sbuffer[yn + 2]) ? 1 : 0;

									int error = (lc1 + lc2 + lc3);
									loadcost += error;
									dtotalcost += error * 256.0;

									// incur a slight penalty for split longs
									if (lc1 == 1 && lc3 == 1 && lc2 == 0)
										dtotalcost += 1.0;

									// attempt to keep lines roughly in top-bottom order by including small cost for 'pointless' reordering
									// this provides more opportunity to use a fixed increment between lines (save space with register add vs lea)
									// notes: 
									// - some chance this could prevent a difficult-to-reach optimization via small incremental mutations
									// - perhaps a bit more likely to result in extra LEA at start, to reach first block of contiguous lines 

									{
										// cost is same for forward or reverse ordering - delta value will be the same even if instruction isn't
										int dycost = (v1 - v0);
										if (dycost < 0)
										{
											dycost = -dycost;
											//dycost++;
										}
										dycost--;	// no cost for adjacents

										double dypenalty = log(1.0 + dycost);
										if (v1 < v0)
											dypenalty *= 1.1;

//										if (dycost > 8)
//											dycost = 8;
//										totalcost += dycost;

										dtotalcost += dypenalty;
									}
								}
							}
						}

						// include benign changes (cost delta == 0) to assist Darwin...
						if ((dtotalcost <= dbest_cost)
#if (USE_NOISY_REORDERING)
							||
							// include random acceptance early in trials, with cutoff
							(/*(depth < (darwin_limit>>1))*/(trial < 10000) && (twist.genU32() < noise_factor))
#endif
						)
						{
							// reset the search depth on any 'real' improvement
							if (dtotalcost < dbest_cost)
							{
								last_improvement = trial;
								depth = 0;
	#if (USE_NOISY_REORDERING)
								//noise_factor >>= 1;
	#endif
							}

							dbest_cost = dtotalcost;
							best_loadcost = loadcost;
							//						best_pattern = trial_pattern;
							for (int r = best_pattern.size() - 1; r >= 0; r--)
								best_pattern[r] = trial_pattern[r];

						}

						if (initial_dbest_cost < 0)
						{
							initial_dbest_cost = dbest_cost;
							initial_best_loadcost = best_loadcost;
						}
					}

#if (USE_NOISY_REORDERING)
					// decay noise factor - allows scoring to gradually dominate over randomness with each iteration
					noise_factor = (unsigned long)(((unsigned long long)noise_factor * 0xfff00000) >> 32);
					//std::cout << "noise factor:" << std::hex << noise_factor << std::endl;
#endif
				}

				//if (g_allargs.verbose)
				{
					if (dbest_cost < initial_dbest_cost)
					{
						double improvement_pct = 100.0 * (dbest_cost / initial_dbest_cost);

						printf("reduction = %.2f%% in %d trials @ loadcost=%d (totalcost=%f)\n",
							improvement_pct,
							last_improvement,
							best_loadcost,
							dbest_cost
							);
						std::cout << std::flush;
					}
					else
					{
						printf("no improvement in %d trials @ loadcost=%d (totalcost=%f)\n",
							last_improvement,
							best_loadcost,
							dbest_cost
							);
						std::cout << std::flush;
					}
				}
			}

			if (g_allargs.verbose)
			{
				// output the best pattern

				printf("\nordered:\n");
				for (int v = 0; v < spr.h_; v++)
				{
					int dsty = v;
					printf("s:%02d ", dsty);
					for (int ci = 0; ci <= max_ci; ci++)
					{
						static const int pagesize = (3 * spr.h_);
						int page = ((ci * shiftcount) + 0) * pagesize;
						uword_t *sbuffer = &buffer[page];

						print_dataword(sbuffer[(dsty * 3) + 0]);
						print_dataword(sbuffer[(dsty * 3) + 1]);
						print_dataword(sbuffer[(dsty * 3) + 2]);
						printf("+");
					}
					if (livelines[dsty])
						printf(" L");
					printf("\n");
				}

				printf("\nreordered:\n");
				for (int v = 0; v < spr.h_; v++)
				{
					int dsty = best_pattern[v];
					printf("s:%02d ", dsty);
					for (int ci = 0; ci <= max_ci; ci++)
					{
						static const int pagesize = (3 * spr.h_);
						int page = ((ci * shiftcount) + 0) * pagesize;
						uword_t *sbuffer = &buffer[page];

						print_dataword(sbuffer[(dsty * 3) + 0]);
						print_dataword(sbuffer[(dsty * 3) + 1]);
						print_dataword(sbuffer[(dsty * 3) + 2]);
						printf("+");
					}
					if (livelines[dsty])
						printf(" L");
					printf("\n");
				}
			}
		}

		delete[] buffer; buffer = 0;
		//delete[] livelines; livelines = 0;
		//delete[] emptylines; emptylines = 0;
	}


	spr.linemap_opt_2_src = best_pattern;

	//exit(-1);

	{
		for (int v = 0; v < best_pattern.size(); v++)
		{
			for (int i = 0; i < best_pattern.size(); i++)
			{
				if (v == best_pattern[i])
				{
					best_invpattern.push_back(i);
					break;
				}
			}
		}
	}

	// generate metrics for each data word
	int pi = 0;
	for (std::vector<preshiftlist_t>::iterator pmit = spr.psmask_frames_emx_.begin(); pmit != spr.psmask_frames_emx_.end(); pmit++, pi++)
	{
		for (preshiftlist_t::iterator pit = pmit->begin(); pit != pmit->end(); pit++)
		{
			preshift_t &shift = *pit;

			if ((DEBUG_PRESHIFT >= 0) && (pi != DEBUG_PRESHIFT))
				continue;

			Context context
			(
				shift.wordwidth_, 
				shift.height_, 
				/*srccol_wordwidth*/((spr.w_ + 15) >> 4), 
				3,
				/*guardxs=*/g_allargs.emguardx,
				/*shared_mask=*/false
			);

			// output the source image pattern
			if (!g_allargs.quickcut)
			if (g_allargs.verbose)
			{
				printf("\n\nframe %d preshift %d preview:\n", _frame, pi);
				for (int v = 0; v < shift.height_; v++)
				{
					for (int h = 0; h < shift.wordwidth_; h++)
					{
						print_dataword(shift.data_[(v * shift.wordwidth_) + h]);
					}
					printf("\n");
				}
				printf("\n");
			}

	//		byte_t *changes = new byte_t[shift.planewords_ * shift.height_];
	//		memset(changes, 0, shift.planewords_ * shift.height_);
			byte_t *changes = new byte_t[3 * shift.height_];
			memset(changes, 0, 3 * shift.height_);

			// make copy of data in terms of 3*y EM words
			uword_t *buffer = new uword_t[3 * shift.height_];
			memset(buffer, 0, 2 * 3 * shift.height_);
			{
				for (int v = 0; v < shift.height_; v++)
				{
					buffer[(v * 3) + 0] = shift.data_[(v * shift.wordwidth_) + 0];

					if (shift.wordwidth_ == 2)
					{
						// note: em1,em3 used in 2-wide blits. em2 is skipped
						buffer[(v * 3) + 2] = shift.data_[(v * shift.wordwidth_) + 1];
					}
					else
					if (shift.wordwidth_ == 3)
					{
						buffer[(v * 3) + 1] = shift.data_[(v * shift.wordwidth_) + 1];
						buffer[(v * 3) + 2] = shift.data_[(v * shift.wordwidth_) + 2];
					}

					//for (int h = 0; h < shift.wordwidth_ && h < 3; h++)
					//{
					//	buffer[(v * 3) + h] = shift.data_[(v * shift.wordwidth_) + h];
					//}
				}
			}

	#if (0)
			if (!g_allargs.quickcut)
			if (USE_PERSHIFT_LINEORDER_OPTIMIZATION && (shift.height_ > 1))
			{
				best_pattern.clear();
				for (int f = 0; f < shift.height_; f++)
				{
					best_pattern.push_back(f);
				}

				printf("optimizing line order...\n");

				// optimize the line order to minimize changes to EM registers
				{
					mersenne_twister twist;

					// begin with default line pattern (top-to-bottom)
					int best_cost = 1 << 20;
					int best_loadcost = best_cost;
					int last_improvement = 0;

					// optimize line pattern by reordering
					for (int trial = 0; trial < (1 << DARWIN_DEPTH); trial++)
					{
						std::vector<int> trial_pattern(best_pattern);

						// select mutation rate
						int mutrate = ((twist.genS32() >> 4) % 6) + 1;

						// mutate
						for (int mut = 0; mut < mutrate; mut++)
						{
							// select pair to exchange
							int choice1 = (twist.genS32() >> 4) % trial_pattern.size();
							int choice2 = (twist.genS32() >> 4) % trial_pattern.size();
							while (choice1 == choice2)
								choice2 = (twist.genS32() >> 4) % trial_pattern.size();

							// reorder
							int tmp = trial_pattern[choice1];
							trial_pattern[choice1] = trial_pattern[choice2];
							trial_pattern[choice2] = tmp;
						}

						// estimate efficiency of new pattern in terms of deltas per line
						{
							int totalcost = 0;
							int loadcost = 0;

							for (int v = 0; v < (shift.height_ - 1); v++)
							{
								int v0 = trial_pattern[v + 0];
								int v1 = trial_pattern[v + 1];
								int yp = v0 * 3;
								int yn = v1 * 3;
								int lc1 = (buffer[yp + 0] != buffer[yn + 0]) ? 1 : 0;
								int lc2 = (buffer[yp + 1] != buffer[yn + 1]) ? 1 : 0;
								int lc3 = (buffer[yp + 2] != buffer[yn + 2]) ? 1 : 0;

								totalcost += (lc1 + lc2 + lc3) << 8;
								loadcost += lc1 + lc2 + lc3;

								// incur a slight penalty for split longs
								if (lc1==1 && lc3==1 && lc2==0)
									totalcost++;

								// attempt to keep lines roughly in top-bottom order by including small cost for 'pointless' reordering
								// this provides more opportunity to use a fixed increment between lines (save space with register add vs lea)
								// notes: 
								// - some chance this could prevent a difficult-to-reach optimization via small incremental mutations
								// - perhaps a bit more likely to result in extra LEA at start, to reach first block of contiguous lines 

								// cost is same for forward or reverse ordering - delta value will be the same even if instruction isn't
								int dycost = (v1 - v0);
								if (dycost < 0)
									dycost = -dycost;
								if (dycost > 8)
									dycost = 8;

								totalcost += dycost;
							}

							if (totalcost < best_cost)
							{
								best_cost = totalcost;
								best_loadcost = loadcost;
								best_pattern = trial_pattern;
								last_improvement = trial;
							}
						}
					}

					if (g_allargs.verbose)
					{
						printf("\nconverged or exhausted at %d trials @ loadcost=%d (totalcost=%d)\n", last_improvement, best_cost, best_loadcost);

						// output the best pattern
						for (int v = 0; v < shift.height_; v++)
						{
							int dsty = best_pattern[v];
							print_dataword(buffer[(dsty * 3) + 0]);
							print_dataword(buffer[(dsty * 3) + 1]);
							print_dataword(buffer[(dsty * 3) + 2]);
							printf("\n");
						}
					}
				}
			}
	#endif

			ubyte_t *component_livelines = new ubyte_t[shift.height_];
			memset(component_livelines, 0, shift.height_);

			// find nonempty lines
			for (int y = 0; y < shift.height_; y++)
			{
				int dsty = best_pattern[y];

				if (USE_EMPTYLINE_OPTIMIZATION)
				{
					uword_t *srcdata = &buffer[dsty * 3];

					// look for empty blits (all EMs == 0)
					u64 line = 0;
					for (int x = 0; x < 3; x++)
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
	
			// generate map for only changing EM registers 
			// (excluding first output line - this is handled as dirty registers to minimize initial setup if the first line is empty)

			// mark first line as dirty
			//int y = best_pattern[0] * 3;
			//changes[y + 0] = 1;
			//if (shift.wordwidth_ >= 2)
			//	changes[y + 2] = 1;
			//if (shift.wordwidth_ == 3)
			//	changes[y + 1] = 1;

			if (USE_DELTA_OPTIMIZATION)
			{
				uword_t lastchanges[3];
				bool dirtyline = true;

				for (int v = 0; v < shift.height_; v++)
				{
					int dsty = best_pattern[v];

					// generate deltas for all but 1st BUSY line
					int yn3 = dsty * 3;
					int yp3 = yn3;
					if (v > 0)
						yp3 = best_pattern[v - 1] * 3;

					// is this a real line?
					//uword_t m = buffer[yn + 0];
					//if (shift.wordwidth_ >= 2)
					//	m |= buffer[yn + 2];
					//if (shift.wordwidth_ == 3)
					//	m |= buffer[yn + 1];

					// generate deltas for only real lines
					//if (m)
					if (component_livelines[dsty])
					{
						if (1)
							changes[yn3 + 0] = (dirtyline || (lastchanges[0] != buffer[yn3 + 0])) ? 1 : 0;
						if (shift.wordwidth_ == 3)
							changes[yn3 + 1] = (dirtyline || (lastchanges[1] != buffer[yn3 + 1])) ? 1 : 0;
						if (shift.wordwidth_ >= 2)
							changes[yn3 + 2] = (dirtyline || (lastchanges[2] != buffer[yn3 + 2])) ? 1 : 0;

						// accept new data for non-empty lines
						lastchanges[0] = buffer[yn3 + 0];
						lastchanges[1] = buffer[yn3 + 1];
						lastchanges[2] = buffer[yn3 + 2];

						dirtyline = false;
					}
				}
			}
			else
			{
				for (int v = 0; v < shift.height_; v++)
				{
					int dsty = best_pattern[v];

					if (component_livelines[dsty])
					{
						int dsty3 = dsty * 3;
						changes[dsty3 + 0] = true;
						changes[dsty3 + 1] = (shift.wordwidth_ == 3);
						changes[dsty3 + 2] = (shift.wordwidth_ >= 2);
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

			for (int v = 0; v < shift.height_; v++)
			{
				int dsty3 = best_pattern[v] * 3;

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

			if (!g_allargs.quickcut)
				if (g_allargs.verbose)
					printf("\nsurviving datawords...\n");

			// reset dirty EM registers for this blit width
			//em_dirty[0] = true;
			//em_dirty[1] = (shift.wordwidth_ > 2);
			//em_dirty[2] = (shift.wordwidth_ > 1);

	//		int unique_datawords = 0; // each unique dataword has a uid so it can be reused
			{
				for (int v = 0; v < shift.height_; v++)
				{
					int dsty = best_pattern[v];

					if (!component_livelines[dsty])
						continue;

					uword_t *srcdata = &buffer[dsty * 3];
					byte_t *linechanges = &changes[dsty * 3];
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
									if (g_allargs.verbose)
										printf("----------------");
								continue;
							}

							line_changed = true;

							uword_t dataword = srcdata[x];
							emx_metric_t &m = context.metrics[dataword];

							if (!g_allargs.quickcut)
								if (g_allargs.verbose)
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
							active_lines.push_back(lineout(v, dsty));

						if (!g_allargs.quickcut)
							if (g_allargs.verbose)
								printf("\n");
					}
				}
			}

			if (!g_allargs.quickcut)
				if (g_allargs.verbose)
					printf("total surviving = %d\n", context.metrics.size());

			if (!g_allargs.quickcut)
				if (g_allargs.verbose)
					printf("\nscoring:\n");

			// globally score each data word according to metrics
			int assigned_registers = 0;
			for (metricmap::iterator mit = context.metrics.begin(); mit != context.metrics.end(); mit++)
			{
				const uword_t &dataword = mit->first;

				if (!g_allargs.quickcut)
					if (g_allargs.verbose)
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
					if (g_allargs.verbose)
						printf(" incidence_score: %.2f\n", fscore);
			}

			// globally score each data word according to its left-adjacency (upper word opportuntiy)
			for (metricmap::iterator mit = context.metrics.begin(); mit != context.metrics.end(); mit++)
			{
				const uword_t &dataword = mit->first;

				if (!g_allargs.quickcut)
					if (g_allargs.verbose)
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
					if (g_allargs.verbose)
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
							if (g_allargs.verbose)
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
							if (g_allargs.verbose)
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
									if (g_allargs.verbose)
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
											if (g_allargs.verbose)
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


			// preload most useful values

			//context

			// data dependencies
			// process all *changing words* on *changing lines* in *optimized line order*

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

			std::vector<storedependency> deps;
			std::vector<u64> lifespans;

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
			for (std::vector<lineout>::iterator lit = active_lines.begin(); lit != active_lines.end(); lit++, job++)
			{
				lineout &lo = *lit;
				int dsty = lo.dline;

				// only live lines in active_lines table

				uword_t *srcdata = &buffer[dsty * 3];
				byte_t *linechanges = &changes[dsty * 3];

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
				if (g_allargs.verbose)
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
			if (g_allargs.verbose)
			{
				std::vector<storedependency>::iterator dit = deps.begin();
				for (std::vector<u64>::iterator lit = lifespans.begin(); lit != lifespans.end(); lit++, dit++)
				{
					assert(dit != deps.end());
					print_data(*lit, /*unique_datawords*/assigned_registers); printf(" %s\n", (dit->type == 0) ? "R" : "U");
				}
			}


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

			bool NFSR = 
				(shift.wordwidth_ > 1) && // NFSR is meaningless for wordwidth==1
				(shift.wordsremaining_ == 0) && 
				(((spr.w_ + 15) >> 4) != (shift.wordwidth_ + shift.wordoffset_));

			bool FISR = (shift.wordoffset_ > 0);

				//false;// ((shift.wordwidth_ << 4) == ((spr.w_ + 15) & -16));
			context.blitter.skew = 
				(shift.shift_ & 15) | 
				(NFSR ? 0x40 : 0x00) | 
				(FISR ? 0x80 : 0x00);

			context.blitter.dyinc = (8 + 2) - (shift.wordwidth_ << 3);

			// SYINC = difference between source wordwidth & preshift wordwidth, + 2
			// note: compensate for NFSR!
			context.blitter.syinc = 
				(((((spr.w_ + 15) >> 4) - shift.wordwidth_) << 1) + 2) 
					+ (NFSR ? 2 : 0) 
					- (FISR ? 2 : 0);
		
			// (8 + 2) - (shift.wordwidth_ << 3);

			// gather metrics on LEA skip values, for loading into registers
			// must be done before sim reset!
			if (USE_DSTSKIP_REG_OPTIMIZATION)
			{
				int current_line = 0;

				for (int y = 0; y < shift.height_; y++)
				{
					int dsty = best_pattern[y];

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

			context.reset_sim(true);

			// reset dirty EM registers for this blit width
			//em_dirty[0] = true;
			//em_dirty[1] = (shift.wordwidth_ > 2);
			//em_dirty[2] = (shift.wordwidth_ > 1);

			// generate operations
			int current_line = 0;
			int reg_index = 0;
			int reg_index2 = 0;
			int reg_index3 = 0;
			{
	#if (USE_NOP_BINARY_VALIDATION)
				context.operations.push_back(new NoOp(context));
				context.operations.back()->execute();
	#endif

//				context.operations.push_back(new SYS_PageRestore(context));
//				context.operations.back()->execute();

				context.operations.push_back(new BLiT_Init(context));
				context.operations.back()->execute();
			
	//			context.operations.push_back(new Terminate(context));
	//			context.operations.back()->execute();

				// load fixed registers
				if (1)
				{
					for (std::vector<dataregister>::iterator it = context.registers.begin(); it != context.registers.end(); it++)
					{
						if (it->constant)
						{
							// use standard immediate load - let the optimizer reduce it if possible
							context.operations.push_back(new DR_LoadI32(context, it->index, it->H, it->L));

							it->constant = false;

							it->allocatedH = true;
							it->allocatedL = true;

							context.operations.back()->execute();
						}
					}

					// on next reset, registers will no longer be constant/preallocated
					context.setconstants = false;
				}
			

				int src_lines_skipped = 0;

				int curr_line_words = shift.wordwidth_;

				linemap lines;
				for (int y = 0; y < shift.height_; y++)
				{
					int dsty = best_pattern[y];

					uword_t *srcdata = &buffer[dsty * 3];
					byte_t *linechanges = &changes[dsty * 3];

					// bypass empty blits (all EMs == 0)
					//u64 line = 0;
					//for (int x = 0; x < 3; x++)
					//{
					//	line <<= 16;
					//	line |= srcdata[x];
					//}

					// determine true [offset,width] for this line only
/*
					int live_line_words = 0;
					switch (curr_line_words)
					{

					};

					for (int mi = 3-1; mi >= 0; mi--)
					{
						if (srcdata[mi] != 0)
							live_line_words++;
					}

					if ((live_line_words > 0) && (live_line_words != curr_line_words))
					{
						context.blitter.xcount = curr_line_words;
						context.operations.push_back(new BLiT_XMod(context, (live_line_words- curr_line_words)));
						curr_line_words = live_line_words;
					}
*/
					// is the blit going to output something?
					//if (line)
					if (component_livelines[dsty])
					{
						// line hop +/-
						int linejump = dsty - current_line;

						// all non-empty blits require a dst address update
						// todo: linewidth should be an argument
						if (linejump)
						{
							// caution: can't use LEA for jumps > +/-32k
							int addressjump = (linejump * context.dstlinewid);
							if (addressjump < 32768 && addressjump >= -32768)
							{
								if (USE_DSTSKIP_REG_OPTIMIZATION)
								{

									int reg_src = -1;
									DataSource ds;

									ds = context.sim_resolve_data16(uword_t(addressjump), reg_src, true);
									if (ds == DataSource_RegisterL)
									{
										// for +ve steps, look for +ve equivalent register and apply ADD operation
										// only allocates register if value does not pre-exist 
										context.operations.push_back(new DST_AddaR16(context, reg_src, context.SCRLINE_reg, 0, linejump));
									}
									else
									{
										// for -ve steps, look for +ve equivalent register and apply SUB operation
										// only allocates register if value does not pre-exist 
										ds = context.sim_resolve_data16(uword_t(-addressjump), reg_src, true);
										if (ds == DataSource_RegisterL)
										{
											context.operations.push_back(new DST_SubaR16(context, reg_src, context.SCRLINE_reg, 0, linejump));
										}
										else
										{
											// pointless allocation - fall back to LEA
											context.operations.push_back(new DST_Lea(context, context.SCRLINE_reg, context.SCRLINE_reg, 0, linejump));
										}
									}
								}
								else
								{
									context.operations.push_back(new DST_Lea(context, context.SCRLINE_reg, context.SCRLINE_reg, 0, linejump));
								}
							}
							else
							{
								context.operations.push_back(new DST_AddaIL(context, context.SCRLINE_reg, 0, linejump));
							}
							context.operations.back()->execute();
							if (src_lines_skipped > 0)
							{
								context.operations.push_back(new SRC_Skip(context, 0, src_lines_skipped));
								context.operations.back()->execute();
								src_lines_skipped = 0;
							}
						}

						// check for all 3 changing
						if ((linechanges[0]/* || em_dirty[0]*/) &&
							(linechanges[1]/* || em_dirty[1]*/) &&
							(linechanges[2]/* || em_dirty[2]*/))
						{
							int sumA = 0, sumB = 0;

							// test-resolve EM1:EM2 and EM2:EM3, looking for the cheapest solution
							DataSource ds12 = context.sim_resolve_data32(srcdata[0], srcdata[1], reg_index, reg_index2, false);
							DataSource ds3 = context.sim_resolve_data16(srcdata[2], reg_index3, false);

							sumA += DataSource_costs[ds12];
							sumA += DataSource_costs[ds3];

							DataSource ds23 = context.sim_resolve_data32(srcdata[1], srcdata[2], reg_index, reg_index2, false);
							DataSource ds1 = context.sim_resolve_data16(srcdata[0], reg_index3, false);

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
										if (g_allargs.verbose)
											printf("preferred long immediate with optimal value [$%04x%04x]\n", srcdata[1],srcdata[2]);
									}
									else
									{
										if (g_allargs.verbose)
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
										if (g_allargs.verbose)
											printf("preferred long register with optimal L value [$%04x%04x]\n", srcdata[1],srcdata[2]);
									}
									else
									{
										if (g_allargs.verbose)
											printf("preferred long register with optimal L value [$%04x%04x]\n", srcdata[0],srcdata[1]);
									}
								}
								else
								{
									// don't care
								}
							}

							if ((sumA < sumB) || (chosen == 0))
							{
								DataSource ds12 = context.sim_resolve_data32(srcdata[0], srcdata[1], reg_index, reg_index2, true);
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

								DataSource ds3 = context.sim_resolve_data16(srcdata[2], reg_index3, true);
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
								DataSource ds23 = context.sim_resolve_data32(srcdata[1], srcdata[2], reg_index, reg_index2, true);
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

								DataSource ds1 = context.sim_resolve_data16(srcdata[0], reg_index3, true);
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
						else
						if ((linechanges[0]/* || em_dirty[0]*/) && 
							(linechanges[1]/* || em_dirty[1]*/))
						{
	//						context.operations.push_back(new EM_MoveI32(context, 0, srcdata[0], srcdata[1]));
							DataSource ds = context.sim_resolve_data32(srcdata[0], srcdata[1], reg_index, reg_index2, true);
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
						else
						if ((linechanges[1]/* || em_dirty[1]*/) && 
							(linechanges[2]/* || em_dirty[2]*/))
						{
							// longword to EM2
	//						context.operations.push_back(new EM_MoveI32(context, 1, srcdata[1], srcdata[2]));
	//						context.operations.back()->execute();

							//context.sim_track_datarefs(srcdata[1]);
							//context.sim_track_datarefs(srcdata[2]);

	//						em_dirty[1] = em_dirty[2] = false;

							DataSource ds = context.sim_resolve_data32(srcdata[1], srcdata[2], reg_index, reg_index2, true);
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
									if (context.sim_resolve_data16(srcdata[x], reg_index, true) == DataSource_RegisterL)
									{
										context.operations.push_back(new EM_MoveR16(context, x, reg_index, EM_reg));
										context.operations.back()->execute();
									}
									else
									{
										context.operations.push_back(new EM_MoveI16(context, x, srcdata[x], EM_reg));
										context.operations.back()->execute();
									}

									//context.sim_track_datarefs(srcdata[x]);

									//em_dirty[x] = false;
								}
							}
						}

						// update blitter dest/ycount
						context.operations.push_back(new BLiT_SetYC(context, context.PLANES_reg));
						context.operations.back()->execute();
						context.operations.push_back(new BLiT_SetDSTADDR(context, context.SCRLINE_reg));
						context.operations.back()->execute();

						// execute the blit for this line
						context.operations.push_back(new BLiT_Go(context, context.BHOG_reg, y, dsty, 0));
						context.operations.back()->execute();

						// next linejump+blit will be relative to this one (and not an empty one)
						current_line = dsty;
					}
					else
					{
						src_lines_skipped++;
					}
				}
			}

			//int job = 0;
			//for (std::vector<dataregister>::iterator rit = regs.begin(); rit != regs.end(); rit++)
			//{
			//	dataregister &dr = *rit;
			//	if (job != dr.job)
			//	{
			//		// previous job complete - emit blit operations
			//		job = dr.job;

			//		operations.push_back(new BLiT_Go(context));
			//	}
			//}
			// emit final blit operations
			//operations.push_back(new BLiT_Go(context));
			// emit terminal
			context.operations.push_back(new Terminate(context));
			context.operations.back()->execute();

	#if (SIM_ALL_STAGES)
			run_sim(context, component_livelines, _frame, pi, shift, best_invpattern, /*generate=*/false);
	#endif
			// peephole passes

			bool changed = !g_allargs.quickcut;
			int xform_passes = 0;

	//		if (!g_allargs.quickcut)

			while (changed)
			{
				changed = false;

				// immediate loads to registers with register-derived values
	
				while (context.transform_pass())
				{
					changed = true;
					xform_passes++;
	#if (SIM_ALL_STAGES)
					run_sim(context, component_livelines, _frame, pi, shift, best_invpattern, /*generate=*/false);
	#endif
				}

				changed |= context.cleanup_pass();
	#if (SIM_ALL_STAGES)
				run_sim(context, component_livelines, _frame, pi, shift, best_invpattern, /*generate=*/false);
	#endif
				{
					while (context.promote_pass())
					{
						changed = true;
	#if (SIM_ALL_STAGES)
						run_sim(context, component_livelines, _frame, pi, shift, best_invpattern, /*generate=*/false);
	#endif
					}
				}
			}
			
#if 0
// not working yet - should probably be performed as fallback case for a failed transform of LEA
// so subsequent LEAs can take advantage of reused register load
			{
				while (context.promote_lea_pass())
				{
#if (SIM_ALL_STAGES)
					run_sim(context, component_livelines, _frame, pi, shift, best_invpattern, /*generate=*/false);
#endif
				}
			}
#endif

			//if (!g_allargs.quickcut)
			{
				context.reduce_pass();
	#if (SIM_ALL_STAGES)
				run_sim(context, component_livelines, _frame, pi, shift, best_invpattern, /*generate=*/false);
	#endif
			}


			// optimize instruction tail / blitter overlaps
			if (!g_allargs.quickcut)
				context.tail_pass();

			if (!g_allargs.quickcut && g_allargs.verbose)
				std::cout << "preshift " << pi << " codegen optimization transforms: " << xform_passes << std::endl;
		
			//
			run_sim(context, component_livelines, _frame, pi, shift, best_invpattern, /*generate=*/true);



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

			delete[] component_livelines; component_livelines = 0;
		} // components
	} // preshifts

	delete[] livelines; livelines = 0;
	// generate the generator



}


bool generate_emx_sequence()
{
	std::cout << "code-generating EMX frames: " << std::endl;

	// individual sprites follow
	spritelist_t::iterator it = g_spriteframes.begin();

	int f = 0;
	for (; it != g_spriteframes.end(); it++, f++)
	{
		if ((DEBUG_FRAME >= 0) && (f != DEBUG_FRAME))
			continue;
		generate_emx_frame(*it, f);
	}

	return true;
}
