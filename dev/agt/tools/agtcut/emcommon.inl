//---------------------------------------------------------------------------------------------------------------------
//	[A]tari [G]ame [T]ools / dml 2022
//---------------------------------------------------------------------------------------------------------------------
//	common code, shared between EMX/EMH/EMS generators
//---------------------------------------------------------------------------------------------------------------------

static const int c_max_buscycles_per_plane = 256 / 4;

#include <thread>

//---------------------------------------------------------------------------------------------------------------------

void dump_ordered_pattern
(
	const spriteframe_t &spr, 
	int max_group,
	int shiftcount,
	const uword_t *buffer,
	const ubyte_t *livelines
)
{
	// output the initial pattern
	
	int pagesize = (3 * spr.h_);

	for (int v = 0; v < spr.h_; v++)
	{
		int dsty = v;
		printf("s:%02d ", dsty);
		for (int group = 0; group <= max_group; group++)
		{
			int page = ((group * shiftcount) + 0) * pagesize;
			const uword_t *sbuffer = &buffer[page];

			print_dataword(sbuffer[(dsty * 3) + 0]);
			print_dataword(sbuffer[(dsty * 3) + 1]);
			print_dataword(sbuffer[(dsty * 3) + 2]);
			printf("+");
		}
		if (livelines[dsty])
			printf(" L");
		printf("\n");
	}

	fflush(stdout);
}

void dump_reordered_pattern
(
	const spriteframe_t &spr, 
	const std::vector<int> &ordering,
	int ordered_lines,
	int max_group,
	int shiftcount,
	const uword_t *buffer,
	const ubyte_t *livelines
)
{
	int pagesize = (3 * spr.h_);
	for (int v = 0; v < ordered_lines; v++)
	{
		int srcy = ordering[v];
		printf("s:%02d ", srcy);
		for (int group = 0; group <= max_group; group++)
		{
			int page = ((group * shiftcount) + 0) * pagesize;
			const uword_t *sbuffer = &buffer[page];

			print_dataword(sbuffer[(srcy * 3) + 0]);
			print_dataword(sbuffer[(srcy * 3) + 1]);
			print_dataword(sbuffer[(srcy * 3) + 2]);
			printf("+");
		}
		if (livelines[srcy])
			printf(" L");
		printf("\n");
	}
	fflush(stdout);
}

void generate_emx_emh_span_pattern_audit(const spriteframe_t &_spr, const preshift_t &_shift)
{
	// preview

	printf("\n");

	auto &sa = _shift.sa;

	// all lines in a pattern
	int v = 0;
	for (auto &ln : sa.span_vpattern_)
	{
		int wx = 0;

		printf("AW:%d ", _spr.active_widths[v++]);

		// all spans in a line
		for (auto &span : ln)
		{
			// offset
			{
				for (; wx < span.ws; wx++)
				{
					printf(".");
					printf(" ");
					for (int p = 0; p < 16; p++)
						printf("-");
					printf(" ");
				}
			}

			// words
			if (span.ww == 0)
			{
				printf("error!\n"); exit(1);
			}

			bool span_start = true;
			int lasti = span.ww - 1;
			for (int wi = 0; wi <= lasti; wi++, wx++)
			{
				u16 maskword = span.em[1];

				if (wi == 0)
					 maskword = span.em[0];
				else
				if (wi == lasti)
					 maskword = span.em[2];

				if (span_start)
					printf("%d", span.ww);
				else
					printf(".");

				if (span.cxs && (wi == 0))
					printf("!");
				else
					printf(":");

				if (span_start)
					span_start = false;

				for (int p = 0; p < 16; p++)
				{
					if (maskword & 0x8000)
						printf("1");
					else
						printf("0");

					maskword <<= 1;
				}

				if (span.cxe && (wi == lasti))
					printf("!");
				else
					printf(":");
			}

		}
		printf("\n");
	}

	printf("...\n");

}

//---------------------------------------------------------------------------------------------------------------------

void generate_emx_emh_active_linewidths(spriteframe_t &spr, spriteframe_t::generated_data_t &gen)
{
	spr.active_starts.clear();
	spr.active_starts.resize(spr.h_, 0);
	spr.active_widths.clear();
	spr.active_widths.resize(spr.h_, 0);

	// process only preshift[0] to establish source colour active widths
	{
		std::vector<preshiftlist_t>::iterator pmit = gen.psmask_frames_.begin();

		// compute active mask widths
		{
			preshiftlist_t::iterator cit = pmit->begin();
			auto & spshift = *cit;
			preshift_t &shift = *spshift;

			for (int v = 0; v < spr.h_; v++)
			{
				// find start
				int w = 0;
				int start = -1;
				while (w < shift.wordwidth_)
				{
					// fetch next word
					u16 *pline = &shift.data_[(v * shift.wordwidth_)];
					u16 word = pline[w];
					int word_pos = w++;

					if (word != 0)
					{
						start = word_pos;
						break;
					}
				}

				// find end
				w = shift.wordwidth_ - 1;
				int end = -1;
				while (w >= 0)
				{
					// fetch next word
					u16 *pline = &shift.data_[(v * shift.wordwidth_)];
					u16 word = pline[w];
					int word_pos = w--;

					if (word != 0)
					{
						end = word_pos;
						break;
					}
				}

				if ((start >= 0) && (end >= 0))
				{
					spr.active_starts[v] = start;
					spr.active_widths[v] = (end + 1) - start;
				}
			}
		}
	}
}

//---------------------------------------------------------------------------------------------------------------------

static void set_default_lineorder
(
	const spriteframe_t &spr,
	std::vector<int> &linemap_opt_2_src
)
{
	if (g_allargs.verbose)
		std::cout << "set default line order..." << std::endl << std::flush;

	linemap_opt_2_src.clear();
	linemap_opt_2_src.reserve(spr.h_);
	for (int f = 0; f < spr.h_; f++)
	{
		linemap_opt_2_src.push_back(f);
	}
}

static void lineorder_reverse_mapping
(
	const spriteframe_t &spr,
	const std::vector<int> &linemap_opt_2_src,
	std::vector<int> &linemap_src_2_opt
)
{
	if (g_allargs.verbose)
		std::cout << "reverse-map source to optimized line order..." << std::endl << std::flush;

	linemap_src_2_opt.clear();
	linemap_src_2_opt.resize(spr.h_, 0);
	for (int v = 0; v < spr.h_; v++)
	{
		for (int i = 0; i < linemap_opt_2_src.size(); i++)
		{
			if (v == linemap_opt_2_src[i])
			{
				linemap_src_2_opt[v] = i;
				break;
			}
		}
	}
}

// optimize the line order for NON EMPTY LINES to minimize changes to EM registers
// the empty lines are already at the end of the sequence so optimization works on a shorter sequence (up to live_line_count)	

struct anneal_in
{
	const std::vector<int> &linemap_opt_2_src;
	const spriteframe_t &spr;
	const spriteframe_t::generated_data_t &gen;
	const preshift_t * pshared_shift;
	int max_group;
	int shiftcount;
	const uword_t *buffer;
	//const uword_t *sgbuffer;
	const ubyte_t *livelines;
	int live_line_count;
};

struct anneal_result
{
	std::vector<int> linemap_opt_2_src;
	double dbest_cost;
	int best_loadcost;
};

struct anneal_costs
{
	double total;
	double lineadj;
	int loads;
	int pairbonus;
};

double measure_psm_fragmentation
(
	const std::vector<int> &trial_pattern,
	const spriteframe_t::generated_data_t &gen
)
{
	double fragcost = 0.0;

	int live_line_count = trial_pattern.size();

	int si = 0;
	auto pmit = gen.psmask_frames_.begin();
	for (; pmit != gen.psmask_frames_.end(); pmit++, si++)
	{
		auto cit = pmit->begin();
		{
			auto & spshift = *cit;
			auto &shift = *spshift;

			// iterate word columns
			int w = 0;
			while (w < shift.wordwidth_)
			{
				// iterate lines
				for (int ov = 1; ov < live_line_count; ++ov)
				{
					int opi = ov - 1;
					int oni = ov - 0;

					int spi = trial_pattern[opi];
					int sni = trial_pattern[oni];

					// access maskline
					u16 *plinep = &shift.data_[(spi * shift.wordwidth_)];
					u16 *plinen = &shift.data_[(sni * shift.wordwidth_)];

					// fetch words
					u16 wordp = plinep[w];
					u16 wordn = plinen[w];

					//if (w == 0)
					//{ print_dataword(wordp); putchar(':'); }

					if (wordp == wordn)
					{
						// no cost for transition between equal masks (or empty masks)
					}
					else 
					if ((wordp == 0) || (wordn == 0)) 
					{
						// special cost for transition to/from empty mask (no mask state change but potentially a srcskip or new group)
						fragcost += 1.0;
					}
				}

				// next word column
				++w;
			}

		}
	}

	return fragcost;
}

double measure_psm_fragmentation
(
	const std::vector<int> &trial_pattern,
	const preshift_t * pshared_shift,
	int shiftcount
)
{
	double fragcost = 0.0;

	int live_line_count = trial_pattern.size();

	{		
		auto &shift = *pshared_shift;
		// iterate word columns
		int w = 0;
		while (w < shift.wordwidth_)
		{
			// iterate lines
			for (int ov = 1; ov < live_line_count; ++ov)
			{
				int opi = ov - 1;
				int oni = ov - 0;

				int spi = trial_pattern[opi];
				int sni = trial_pattern[oni];

				// access maskline
				u16 *plinep = &shift.data_[(spi * shift.wordwidth_)];
				u16 *plinen = &shift.data_[(sni * shift.wordwidth_)];

				// fetch words
				u16 wordp = plinep[w];
				u16 wordn = plinen[w];

				//if (w == 0)
				//{ print_dataword(wordp); putchar(':'); }

				if (wordp == wordn)
				{
					// no cost for transition between equal masks (or empty masks)
				}
				else 
				if ((wordp == 0) || (wordn == 0)) 
				{
					// special cost for transition to/from empty mask (no mask state change but potentially a srcskip or new group)
					fragcost += 1.0;
				}
			}

			// next word column
			++w;
		}

	}

	return fragcost * shiftcount;
}

void measure
(
	double &dtotalcost, int &loadcost,
	//
	const std::vector<int> &trial_pattern,
	const spriteframe_t &spr,
	const spriteframe_t::generated_data_t &gen,
	const preshift_t * __restrict pshared_shift,
	int max_group, int shiftcount,
	const uword_t * __restrict buffer, //const uword_t * __restrict sgbuffer,
	const ubyte_t *__restrict livelines, int live_line_count
)
{
	//int totalcost = 0;
	
	//double srcadjcost = 0.0;
	double dstlineadjcost = 0.0;

	int loadcount = 0;
	int loadpairs = 0;

	int pagesize = (3 * spr.h_);

	//int shiftcount = gen.psmask_frames_.size();
	for (int si = 0; si < shiftcount; si++)
	{
		for (int group = 0; group <= max_group; group++)
		{
			int page = ((group * shiftcount) + si) * pagesize;
			const uword_t *sbuffer = &buffer[page];

			//int gpage = ((group * shiftcount) + si) * spr.h_;
			//const uword_t *ssgbuffer = &sgbuffer[gpage];


			//						uword_t *sbuffer = &buffer[3 * spr.h_ * si * ci];
			//						uword_t *sbuffer = buffer;
			for (int v = 0; v < (live_line_count - 1); v++)
			{
				int v0 = trial_pattern[v + 0];
				int v1 = trial_pattern[v + 1];


				int yp = v0 * 3;
				int yn = v1 * 3;

				word_t wp0 = sbuffer[yp + 0];
				word_t wp1 = sbuffer[yp + 1];
				word_t wp2 = sbuffer[yp + 2];
				
				word_t wn0 = sbuffer[yn + 0];
				word_t wn1 = sbuffer[yn + 1];
				word_t wn2 = sbuffer[yn + 2];

				// get deltas
				int lc1 = (wp0 != wn0) ? 1 : 0;
				int lc2 = (wp1 != wn1) ? 1 : 0;
				int lc3 = (wp2 != wn2) ? 1 : 0;


				// load costs

				int loadseq = (lc1 + lc2 + lc3);
				loadcount += loadseq;

				if ((lc1 & lc2) || (lc2 & lc3))
					loadpairs++;
				
				// dst skip costs

				//dstlineadjcost *= 0.9;

				int dstlineskips = (v1 - v0);
				if (dstlineskips < 0)
					dstlineskips = -dstlineskips;
				if (dstlineskips > 0)
					dstlineskips--;	// no cost for adjacents

				// higher penality for larger jumps, reduces src skips caused by line reordering
				dstlineadjcost += sqrt(1.0 + dstlineskips);
/*
				if (dstlineskips > 128)
					dstlineadjcost += 3.0;
				else
				if (dstlineskips > 2)
					dstlineadjcost += 2.0;
				else
				if (dstlineskips > 0)
					dstlineadjcost += 1.0;
*/
				// src skip costs

/*				word_t gp = ssgbuffer[yp];
				word_t gn = ssgbuffer[yn];

				if (gp != gn)
				{
					// group switch penalty?
					//if ((gp != 0) && (gn != 0))
					{
						groupswitchcost += 1.0;
					}
				}
*/
//				word_t gp = ssgbuffer[yp];
//				word_t gn = ssgbuffer[yn];

//				word_t gp = wp0; //| wp1 | wp2;
//				word_t gn = wn0; //| wn1 | wn2;

				//printf("s:%d g:%d l:%d ", si, group, v); print_dataword(gp); putchar(':'); print_dataword(gn); putchar('\n');
				//printf("s:%d g:%d l:%d ", si, group, v); print_dataword(wp0); putchar(':'); print_dataword(wp1); putchar(':'); print_dataword(wp2); putchar('\n');

				//if ((gn != 0) && (gp == 0))
//				if (gp == 0)
//					exit(1);//srcadjcost += 1000.0;

#if 0
				int error = (lc1 + lc2 + lc3);
				loadcost += error;
				dtotalcost += error * 256.0;

				// incur penalty for split longs
				//if (lc1 == 1 && lc3 == 1 && lc2 == 0)
				//	dtotalcost += 32.0;

				// small bonus for combined longs
				if ((lc1 & lc2) | (lc2 & lc3))
					dtotalcost -= 16.0;

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

/*
					// adjacent lines cost nothing
					// skip 1  lines = cost 1
					// skip 2+ lines = cost 2
					// skip 3+ lines = cost 3
					// gives a chance to optimize for short skips, reusing register values. otherwise constant cost for any skip.

					// 32768/208 = approx 157 lines for signed 16bit hop
					if (dycost > 128)
						dycost = 4;
					else
					if (dycost > 3)
						dycost = 3;
					
					dtotalcost += dycost;
*/

					double dypenalty = sqrt(1.0 + dycost); //log(1.0 + dycost);
					if (v1 < v0)
						dypenalty += 4;
//						dypenalty *= 1.1;

//										if (dycost > 8)
//											dycost = 8;
//										totalcost += dycost;

					dtotalcost += dypenalty;
				}
#endif
			}
		}
	}

	loadcost = loadcount - loadpairs;

	// dst line skips primarily a memory overhead, not cycle overhead
	if (g_allargs.cutmode == Cutmode_EMXSPR)
		dstlineadjcost *= 0.5;

	double fragcost = 0.0;

	if (pshared_shift)
		fragcost = measure_psm_fragmentation(trial_pattern, pshared_shift, shiftcount);
	else
		fragcost = measure_psm_fragmentation(trial_pattern, gen);

	dtotalcost = 
		((double)loadcount * 8.0) - ((double)loadpairs * 4.0)
		+ (dstlineadjcost * 1.0)
		+ (fragcost * 10.0);
}

void anneal_lineorder
(
	anneal_result &result,
	//
	uint32_t seed,
	//
	const std::vector<int> &linemap_opt_2_src,
	const spriteframe_t &spr,
	const spriteframe_t::generated_data_t &gen,
	const preshift_t * __restrict pshared_shift,
	int max_group, int shiftcount,
	const uword_t * __restrict buffer, //const uword_t * __restrict sgbuffer,
	const ubyte_t *__restrict livelines, int live_line_count
)
{
	// optimize line pattern by reordering
	std::vector<int> best_pattern(linemap_opt_2_src);


	double initial_dbest_cost;
	int initial_best_loadcost;

	measure
	(
		initial_dbest_cost, initial_best_loadcost,
		//
		best_pattern,
		spr,
		gen,
		pshared_shift,
		max_group, shiftcount,
		buffer, //sgbuffer,
		livelines, live_line_count
	);

	// begin with default line pattern (top-to-bottom)
	double dbest_cost = initial_dbest_cost;//double(1e20);
	int best_loadcost = initial_best_loadcost;//1<<20;

	mersenne_twister twist(seed);

#if (USE_NOISY_REORDERING)
	unsigned long noise_factor = (1 << (32-7));
#endif
	//double initial_dbest_cost = -1;
	//int initial_best_loadcost = -1;

	ulong_t last_improvement = 0;

	std::vector<int> trial_pattern(best_pattern);


	int sh = ((live_line_count + 2) / 3);
	if (g_allargs.emxoptlevel > 1)
		sh = g_allargs.emxoptlevel;

	if (g_allargs.verbose || (g_allargs.emxoptlevel > 0))
	{
		printf("[%s] anneal pass begin @ 2^%d iters:\n", g_allargs.outfile.c_str(), sh); 
		fflush(stdout);
	}

	ulong_t darwin_limit = 1UL << sh;
	
	ulong_t trial = 0;
	ulong_t depth = 0;

	for (; depth < darwin_limit; trial++, depth++)
	{
		double alpha = 0.5 + (0.5 * (1.0 - ((double)depth / (double)(darwin_limit-1))));

		for (int r = best_pattern.size() - 1; r >= 0; r--)
			trial_pattern[r] = best_pattern[r];

		// select mutation rate
		//				int mutrate = ((twist.genS32() >> 4) % 4) + 1;

		// we want to measure improvement from initial sort, so trial 0
		// skips mutation
		if (trial > 0)
		{
			static const float c_max_portion = 1.0f / 2.0f;

			float funitrand = twist.genFloat();
			float fproportion = funitrand * funitrand;

			int mutrate = ceil(0.001f + ((float)live_line_count * c_max_portion * fproportion));

			//int mutrate = 1 + ((twist.genS32() >> 4) % 8);
			//mutrate = 1 + ((mutrate * mutrate) >> 3);

			//mutrate = ceil((float)alpha * mutrate);

			int choice1 = 0;
			int choice2 = 0;

			for (int mut = 0; mut < mutrate; mut++)
			{
				//printf("roll...\n");

				do
				{
					{
						// select pair to exchange
						choice1 = (twist.genU32() >> 11) % live_line_count;
						choice2 = (twist.genU32() >> 11) % live_line_count;
					}

				} while (choice1 == choice2);

					// don't reorder against self
/*								} while ((choice1 == choice2) ||
					// don't place an empty line as the first 
					// line to render, otherwise individual components won't skip initial colour data
					((choice2 == 0) && (0 == livelines[trial_pattern[choice1]])) ||
					((choice1 == 0) && (0 == livelines[trial_pattern[choice2]])));
*/
				// there should be no empty lines within pattern line mutate choices
				//assert(!((choice2 == 0) && (0 == livelines[trial_pattern[choice1]])));
				//assert(!((choice1 == 0) && (0 == livelines[trial_pattern[choice2]])));
				assert(0 != livelines[trial_pattern[choice1]]);
				assert(0 != livelines[trial_pattern[choice2]]);

				//printf("choice1:%d choice2:%d\n", choice1, choice2);

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
					/*
					// A		A
					// B <c1	D \
												// C		B | o
					// D <c2	C / o+1  tmp
					// E		E
					*/

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
			double dtotalcost;
			int loadcost;

			measure
			(
				dtotalcost, loadcost,
				//
				trial_pattern,
				spr,
				gen,
				pshared_shift,
				max_group, shiftcount,
				buffer, //sgbuffer,
				livelines, live_line_count
			);

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
				//if ((dtotalcost + 1e-5) < dbest_cost)
				if ((dbest_cost - dtotalcost) > 0.001)
				{
					//printf("anneal reset! depth=%d b=%f t=%f\n", depth, dtotalcost, dbest_cost);

					last_improvement = trial;

					if (g_allargs.verbose)
						if (initial_dbest_cost > 0)
						{
							double reduction = dbest_cost / initial_dbest_cost;
							printf("reduction = %.3f%% in %d trials @ loadcost=%d (totalcost=%f)\n",
								reduction * 100.0,
								last_improvement,
								best_loadcost,
								dbest_cost
								);
							fflush(stdout);
						}

					depth = 0;
#if (USE_NOISY_REORDERING)
					//noise_factor >>= 1;
#endif
				}
//ERROOORR
//# ERROR
//#error
				dbest_cost = dtotalcost;
				best_loadcost = loadcost;
				//						best_pattern = trial_pattern;
				for (int r = best_pattern.size() - 1; r >= 0; r--)
					best_pattern[r] = trial_pattern[r];

			}

			//if (initial_dbest_cost < 0)
			//{
			//	initial_dbest_cost = dbest_cost;
			//	initial_best_loadcost = best_loadcost;
			//}
		}

#if (USE_NOISY_REORDERING)
		// decay noise factor - allows scoring to gradually dominate over randomness with each iteration
		noise_factor = (unsigned long)(((unsigned long long)noise_factor * 0xfff00000) >> 32);
		//std::cout << "noise factor:" << std::hex << noise_factor << std::endl;
#endif
		static int s_pass = 0;
		//printf("anneal pass %d\n", ++s_pass);

	}

	{
		double reduction = dbest_cost / initial_dbest_cost;

		if (reduction < 0.99999)
		{
			printf("worker annealed = %.2f%% @ loadcost=%d (totalcost=%f)\n",
				reduction * 100.0,
				best_loadcost,
				dbest_cost
				);
			std::cout << std::flush;
		}
	}

/*
	if (g_allargs.verbose || g_allargs.emxoptlevel)
	{
		double reduction = dbest_cost / initial_dbest_cost;

		if (reduction < 0.99999)
		{
			printf("reduction = %.2f%% in %d trials @ loadcost=%d (totalcost=%f)\n",
				reduction * 100.0,
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
*/

	result.linemap_opt_2_src = best_pattern;
	result.dbest_cost = dbest_cost;
	result.best_loadcost = best_loadcost;
}

void anneal_worker(uint32_t _seed, const anneal_in* _in, anneal_result *_result)
{
	anneal_lineorder
	(
		*_result, 
		//
		_seed,
		//
		_in->linemap_opt_2_src, 
		_in->spr, _in->gen,
		_in->pshared_shift,
		_in->max_group, _in->shiftcount,
		_in->buffer, //_in->sgbuffer,
		_in->livelines, _in->live_line_count
	);
}

void generate_emx_emh_common_lineorder
(
	spriteframe_t &spr,
	spriteframe_t::generated_data_t &gen,
	std::vector<int> &linemap_opt_2_src, 
	std::vector<int> &linemap_src_2_opt, 
	bool _optimize,
	preshift_t *pshared_shift
)
{
	static const int c_max_line_spans = 16;// 320 / 48;

	// default line order

	set_default_lineorder(spr, linemap_opt_2_src);

	//

	ubyte_t *emptylines = new ubyte_t[spr.h_];
	memset(emptylines, 1, spr.h_);

	ubyte_t *livelines = new ubyte_t[spr.h_];
	memset(livelines, 0, spr.h_);

	// find common optimized line order
	if (_optimize &&  USE_COMMON_LINEORDER_OPTIMIZATION && (spr.h_ > 2))
	{
		if (g_allargs.verbose)
			std::cout << "optimizing common line order across all preshifts..." << std::endl << std::flush;

		int pagesize = (3 * spr.h_);

		int shiftcount = gen.psmask_frames_.size();
		//if (pshared_shift)
		//	shiftcount = 1;

		int groups = c_max_line_spans;

		// make copy of data in terms of 3*y EM words
		uword_t *buffer = new uword_t[pagesize * shiftcount * groups];
		memset(buffer, 0, sizeof(uword_t) * pagesize * shiftcount * groups);

		// spangroup association buffer
		//uword_t *sgbuffer = new uword_t[spr.h_ * shiftcount * groups];
		//memset(sgbuffer, 0, sizeof(uword_t) * spr.h_ * shiftcount * groups);

		//std::map<int, int> groupmap;

		//int max_ci = -1;
		int max_group = -1;

		if (0) //(pshared_shift)
		{
			assert(false); // deprecated for now

			preshift_t &shift = *pshared_shift;

			//int si = 0;
			for (int v = 0; v < spr.h_; v++)
			{
				span_hseq_t &spanlist = shift.sa.span_vpattern_[v];

				int group = -1;
				for (auto & span : spanlist)
				{
					//printf("span: ws=%d\n", span.ws);

					group = span.group;
					assert(group >= 0);
					if (group > max_group)
						max_group = group;
/*
					auto look = groupmap.find(span.ws);
					if (look != groupmap.end())
					{
						group = look->second;

						//printf("reused x=%d, ci=%d\n", span.ws, ci);
					}
					else
					{
						group = ++max_group;

						if (g_allargs.verbose)
							printf("allocated x=%d, g=%d\n", span.ws, group);

						groupmap[span.ws] = group;

						if (max_group >= c_max_line_spans)
						{
							printf("error - too many span offsets (%d)\n", max_group);
							exit(1);
						}
					}
*/
					/*
												ci++;
												if (ci > max_ci)
													max_ci = ci;
					*/
					//constrained_wordspan & span = e;

					// record group ID in sgbuffer to detect group switches during line reordering
					//int gpage = ((group * shiftcount) + 0) * spr.h_;
					//uword_t *ssgbuffer = &sgbuffer[gpage];
					//ssgbuffer[v] = 1+group; // nonzero = span present


					int page = ((group * shiftcount) + 0) * pagesize;
					uword_t *sbuffer = &buffer[page];

					sbuffer[(v * 3) + 0] = span.em[0];
					sbuffer[(v * 3) + 1] = span.em[1];
					sbuffer[(v * 3) + 2] = span.em[2];

					// track all non-empty lines in any preshift
					if (USE_EMPTYLINE_OPTIMIZATION)
					{
						if (sbuffer[(v * 3) + 0] || sbuffer[(v * 3) + 1] || sbuffer[(v * 3) + 2])
						{
							livelines[v] = 1;
							emptylines[v] = 0;
						}
					}
					else
					{
						livelines[v] = 1;
						emptylines[v] = 0;
					}
				}
			}

		}
		else
		{
			int si = 0;
			std::vector<preshiftlist_t>::iterator pmit = gen.psmask_frames_.begin();
			for (; pmit != gen.psmask_frames_.end(); pmit++, si++)
			{
				// components within a preshift
				//int ci = 0;
				//				for (preshiftlist_t::iterator cit = pmit->begin(); cit != pmit->end(); cit++, ci++)
				preshiftlist_t::iterator cit = pmit->begin();
				{
					auto & spshift = *cit;

					// select span pattern from EMH shared or EMX unique preshift
					preshift_t *pshift = pshared_shift;
					if (!pshift)
						pshift = spshift.get();

					auto &shift = *pshift;

					for (int v = 0; v < spr.h_; v++)
					{
						span_hseq_t &spanlist = shift.sa.span_vpattern_[v];

						int group = -1;
						for (auto & span : spanlist)
						{
							//printf("span: ws=%d\n", span.ws);
							group = span.group;
							assert(group >= 0);
							if (group > max_group)
								max_group = group;
/*
							auto look = groupmap.find(span.ws);
							if (look != groupmap.end())
							{
								group = look->second;

								//printf("reused x=%d, ci=%d\n", span.ws, ci);
							}
							else
							{
								group = ++max_group;

								if (g_allargs.verbose)
									printf("allocated x=%d, g=%d\n", span.ws, group);

								groupmap[span.ws] = group;

								if (max_group >= c_max_line_spans)
								{
									printf("error - too many span offsets (%d)\n", max_group);
									exit(1);
								}
							}
*/							
							
/*
							ci++;
							if (ci > max_ci)
								max_ci = ci;
*/
							//constrained_wordspan & span = e;

							// record group ID in sgbuffer to detect group switches during line reordering
							//int gpage = ((group * shiftcount) + si) * spr.h_;
							//uword_t *ssgbuffer = &sgbuffer[gpage];
							//ssgbuffer[v] = 1; // nonzero = span present


							int page = ((group * shiftcount) + si) * pagesize;
							uword_t *sbuffer = &buffer[page];

							sbuffer[(v * 3) + 0] = span.em[0];
							sbuffer[(v * 3) + 1] = span.em[1];
							sbuffer[(v * 3) + 2] = span.em[2];

							// track all non-empty lines in any preshift
							if (USE_EMPTYLINE_OPTIMIZATION)
							{
								if (sbuffer[(v * 3) + 0] || sbuffer[(v * 3) + 1] || sbuffer[(v * 3) + 2])
								{
									livelines[v] = 1;
									emptylines[v] = 0;
								}
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
		}

		if (max_group < 0)
		{
			printf("error: bad max_group\n");
			exit(1);
		}


		// force fully empty lines to end of pattern
		{
			if (g_allargs.verbose)
				std::cout << "forcing out empty lines..." << std::endl << std::flush;
			
			bool busy;
			do
			{
				busy = false;

				for (int v = 0; v < (spr.h_ - 1); v++)
				{
					int v0 = linemap_opt_2_src[v + 0];
					int v1 = linemap_opt_2_src[v + 1];

					if (!livelines[v0] && livelines[v1])
					{
						linemap_opt_2_src[v + 0] = v1;
						linemap_opt_2_src[v + 1] = v0;
						busy = true;
						break;
					}
				}

			} while (busy);
		}

		if (g_allargs.verbose)
			std::cout << "find active region..." << std::endl << std::flush;

		// find active region
		int live_line_count = linemap_opt_2_src.size();
		for (int v = 0; v < linemap_opt_2_src.size(); v++)
		{
			// find first empty line after reordering
			int srcy = linemap_opt_2_src[v];
			if (!livelines[srcy])
			{
				live_line_count = v;
				break;
			}
		}

		// represent only live lines
		linemap_opt_2_src.resize(live_line_count);

		int empty_line_count = spr.h_ - live_line_count;

		if (g_allargs.verbose)
			std::cout << "found " <<
			live_line_count << " live lines, " <<
			empty_line_count << " empty lines" << 
			std::endl << std::flush;


		if (g_allargs.verbose)
		{
			printf("\nordered:\n");
			dump_ordered_pattern
			(
				spr, 
				max_group, shiftcount,
				buffer, livelines
			);
		}


		// initial sort of mask edges
		if (USE_INITIAL_EDGE_SORT && (g_allargs.emxoptlevel > 0) && (live_line_count > 2))
		{
			if (g_allargs.verbose)
				std::cout << "initial edge sort for > 2 live scans..." << std::endl << std::flush;

			std::vector<std::pair<ulong_t, int>> sorting;
			sorting.resize(live_line_count);
			for (int v = 0; v < live_line_count; v++)
			{
				auto &s = sorting[v];
				s.first = 0;
				s.second = linemap_opt_2_src[v];
			}

			int full_src_wordwidth = ((spr.w_ + 15) & -16) >> 4;
			int src_pixwidth = full_src_wordwidth << 4;

			for (int v = 0; v < live_line_count; v++)
			{
				int ordering = v;
				int orig_y = linemap_opt_2_src[ordering];
				uword_t *line = &spr.mask_[(orig_y * full_src_wordwidth)];

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
				for (int x = src_pixwidth - 1; x >= 0; x--)
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
				linemap_opt_2_src[vnew] = sorting[vnew].second;
			}

		if (g_allargs.verbose)
		{
			printf("\nleading edge ordering:\n");

			dump_reordered_pattern
			(
				spr, 
				linemap_opt_2_src, linemap_opt_2_src.size(),
					max_group, shiftcount,
				buffer, livelines
			);
		}
		}


		// can anneal-optimize ordering here


		if (!g_allargs.quickcut && (g_allargs.emxoptlevel > 0) && (live_line_count > 2))
		{
			printf("anneal line ordering for > 2 live scans...\n"); fflush(stdout);


			// measure the cost before annealing, for reference
			double initial_dbest_cost;
			int initial_best_loadcost;

			measure
			(
				initial_dbest_cost, initial_best_loadcost,
				//
				linemap_opt_2_src,
				spr,
				gen,
				pshared_shift,
				max_group, shiftcount,
				buffer, //sgbuffer,
				livelines, live_line_count
			);

			// define common anneal inputs
			anneal_in in
			{
				linemap_opt_2_src, 
				spr, gen,
				pshared_shift,
				max_group, shiftcount,
				buffer, //sgbuffer, 
				livelines, live_line_count
			};

#define USE_THREADPOOL
#if defined(USE_THREADPOOL)
			size_t concurrency = (std::thread::hardware_concurrency() * 3)/4;
			size_t num_tasks = g_allargs.emxopttrials;
			std::vector<std::shared_ptr<anneal_result>> results;
			{
				ThreadPool worker_pool(concurrency);
				mersenne_twister seed_twist;
				for (int t = 0; t < num_tasks; ++t)
				{
					uint32_t seed = seed_twist.genU32();
					auto result = std::make_shared<anneal_result>();
					results.push_back(result);
					worker_pool.enqueue(anneal_worker, seed, &in, result.get());
				}
				// threads joined on pool destruction, all results ready
			}
			// get the best result from all workers
			int best_worker = 0;
			double dbest_cost = 1e20;
			for (int r = results.size() - 1; r >= 0; --r)
			{
				auto &result = results[r];
				if (result->dbest_cost < dbest_cost)
				{
					best_worker = r;
					dbest_cost = result->dbest_cost;
				}
			}

			auto winning_result = *(results[best_worker].get());
			results.clear();
#else
			// define task and result of each task
			struct anneal_task
			{
				std::shared_ptr<std::thread> worker;
				std::shared_ptr<anneal_result> result;
			};

			// create the tasks
			static const size_t num_tasks = 6;
			std::vector<anneal_task> tasks;
			tasks.reserve(num_tasks);
			mersenne_twister seed_twist;
			for (int t = 0; t < num_tasks; ++t)
			{
				anneal_task task;

				uint32_t seed = seed_twist.genU32();
				task.result = std::make_shared<anneal_result>();
				task.worker = std::make_shared<std::thread>(anneal_worker, seed, &in, task.result.get());
				tasks.push_back(task);
			}

			// wait for tasks to complete
			for (auto & task : tasks)
				task.worker->join();

			// get the best result from all workers
			int best_worker = 0;
			double dbest_cost = 1e20;
			for (int t = tasks.size() - 1; t >= 0; --t)
			{
				anneal_task &task = tasks[t];
				if (task.result->dbest_cost < dbest_cost)
				{
					best_worker = t;
					dbest_cost = task.result->dbest_cost;
				}
			}

			auto &winner = tasks[best_worker];
			auto winning_result = *winner.result;
			tasks.clear();
#endif

			linemap_opt_2_src = winning_result.linemap_opt_2_src;

			if (g_allargs.verbose)
			{
				printf("\nannealed ordering:\n");

				dump_reordered_pattern
				(
					spr, 
					linemap_opt_2_src, linemap_opt_2_src.size(),
					max_group, shiftcount,
					buffer, livelines
				);
			}
			// annealing is expensive - always report unless quiet
			if (!g_allargs.quiet)
			{
				double reduction = winning_result.dbest_cost / initial_dbest_cost;

				if (reduction < 0.99999)
				{
					printf("reduction = %.2f%% @ loadcost=%d (totalcost=%f)\n",
						reduction * 100.0,
						winning_result.best_loadcost,
						winning_result.dbest_cost
						);
					std::cout << std::flush;
				}
				else
				{
					printf("no improvement @ loadcost=%d (totalcost=%f)\n",
						winning_result.best_loadcost,
						winning_result.dbest_cost
						);
					std::cout << std::flush;
				}
				fflush(stdout);
			}

			//delete[] buffer; buffer = 0;
			//delete[] livelines; livelines = 0;
			//delete[] emptylines; emptylines = 0;
		}





		// show reordering
		if (!g_allargs.quiet)
		{
			printf("\nannealed:\n");

			int full_src_wordwidth = ((spr.w_ + 15) & -16) >> 4;
			int src_pixwidth = full_src_wordwidth << 4;

			for (int v = 0; v < live_line_count; v++)
			{
				int order = v;
				int orig_y = linemap_opt_2_src[order];
				uword_t *line = &spr.mask_[(orig_y * full_src_wordwidth)];

				for (int w = 0; w < full_src_wordwidth; w++)
				{
					u16 maskword = ~line[w];

					printf(":");
					if (maskword == 0)
					{
						printf("                ");
					}
					else
					{
						for (int p = 0; p < 16; p++)
						{
							if (maskword & 0x8000)
								printf("1");
							else
								printf("0");

							maskword <<= 1;
						}
					}
					printf(":");
				}
				printf("\n");
			}
			fflush(stdout);
		}

		lineorder_reverse_mapping(spr, linemap_opt_2_src, linemap_src_2_opt);
/*
		linemap_src_2_opt.clear();
		linemap_src_2_opt.resize(spr.h_, 0);
		for (int v = 0; v < spr.h_; v++)
		{
			for (int i = 0; i < linemap_opt_2_src.size(); i++)
			{
				if (v == linemap_opt_2_src[i])
				{
					linemap_src_2_opt[v] = i;
					break;
				}
			}
		}
*/
		// reconstruct order
		if (!g_allargs.quiet)
		{
			printf("\nreconstruction:\n");
			int full_src_wordwidth = ((spr.w_ + 15) & -16) >> 4;
			int src_pixwidth = full_src_wordwidth << 4;

			for (int v = 0; v < spr.h_; v++)
			{
				int orig_y = v;

				if (orig_y >= linemap_src_2_opt.size())
				{
					printf(" ...\n");
				}
				else
				{
					int order = linemap_src_2_opt[orig_y];

					int src_y = linemap_opt_2_src[order];

					uword_t *line = &spr.mask_[(src_y * full_src_wordwidth)];

					for (int w = 0; w < full_src_wordwidth; w++)
					{
						u16 maskword = ~line[w];

						printf(":");
						if (maskword == 0)
						{
							printf("                ");
						}
						else
						{
							for (int p = 0; p < 16; p++)
							{
								if (maskword & 0x8000)
									printf("1");
								else
									printf("0");

								maskword <<= 1;
							}
						}
						printf(":");
					}
				}
				printf("\n");
			}

			fflush(stdout);
		}

		//

		if (g_allargs.verbose)
		{
			// output the best pattern
					
			const int pagesize = (3 * spr.h_);

			printf("\nordered:\n");

			dump_ordered_pattern
			(
				spr, 
				max_group, shiftcount,
				buffer, livelines
			);

/*			for (int v = 0; v < spr.h_; v++)
			{
				int ordered_srcy = v;
				printf("s:%02d ", ordered_srcy);
				for (int ci = 0; ci <= max_ci; ci++)
				{
					int page = ((ci * shiftcount) + 0) * pagesize;
					uword_t *sbuffer = &buffer[page];

					print_dataword(sbuffer[(ordered_srcy * 3) + 0]);
					print_dataword(sbuffer[(ordered_srcy * 3) + 1]);
					print_dataword(sbuffer[(ordered_srcy * 3) + 2]);
					printf("+");
				}
				if (livelines[ordered_srcy])
					printf(" L");
				printf("\n");
			}
*/
			printf("\nfinal-reordered:\n");

			dump_reordered_pattern
			(
				spr,
				linemap_opt_2_src, linemap_opt_2_src.size(),
				max_group, shiftcount,
				buffer, livelines
			);
/*
			for (int v = 0; v < linemap_opt_2_src.size(); v++)
			{
				int reordered_srcy = linemap_opt_2_src[v];
				printf("s:%02d ", reordered_srcy);
				for (int ci = 0; ci <= max_ci; ci++)
				{
					int page = ((ci * shiftcount) + 0) * pagesize;
					uword_t *sbuffer = &buffer[page];

					print_dataword(sbuffer[(reordered_srcy * 3) + 0]);
					print_dataword(sbuffer[(reordered_srcy * 3) + 1]);
					print_dataword(sbuffer[(reordered_srcy * 3) + 2]);
					printf("+");
				}
				if (livelines[reordered_srcy])
					printf(" L");
				printf("\n");
			}
*/
			fflush(stdout);
		}

		printf("...\n"); fflush(stdout);

		delete[] buffer; buffer = 0;
	}
	else
	{
		printf("skip line reordering for 2-scan frame...\n"); fflush(stdout);

		lineorder_reverse_mapping(spr, linemap_opt_2_src, linemap_src_2_opt);
	}
//	exit(1);
}

//---------------------------------------------------------------------------------------------------------------------

void output_span_diagnostic(int pw, int ph, std::vector<constrained_wordspan*> &_stacked_spans, const std::string _name)
{
	int dbw = ((pw + 15) & -16) + 16;;
	int dbh = ph;
	auto data = new RGB[dbw * dbh];
	memset(data, 0, sizeof(RGB) * (dbw * dbh));

	for (constrained_wordspan *pspan : _stacked_spans)
	{
		auto & span = *pspan;

		uint8_t r = ((span.group+1) * 1923987498324787 >> 4) & 255;
		uint8_t g = ((span.group+1) * 1923987498324787 >> 12) & 255;
		uint8_t b = ((span.group+1) * 1923987498324787 >> 20) & 255;

		for (int x = (span.ws<<4); x < (span.ws + span.ww) << 4; ++x)
		{
			RGB & p = data[(dbw * span.line_orig) + x];
			p.red = r;
			p.green = g;
			p.blue = b;
		}
	}

	stbi_write_png(_name.c_str(), dbw, dbh, 3, data, 0);

	delete[] data;
}

//---------------------------------------------------------------------------------------------------------------------
