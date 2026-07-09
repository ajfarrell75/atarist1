//---------------------------------------------------------------------------------------------------------------------
//	[A]tari [G]ame [T]ools / dml 2022
//---------------------------------------------------------------------------------------------------------------------
//	generator specifically for EMH sprites
//---------------------------------------------------------------------------------------------------------------------

void generate_emh_mask_sequence
(
	spriteframe_t &spr, 
	preshift_t &shift, 
	std::vector<constrained_wordspan*> stacked_spans, 
	uword_t *psharedmask,
	int sharedmask_wordwidth
)
{
	u16 last_sm1 = 0;
	u16 last_sm2 = 0;
	u16 last_sm3 = 0;

	for (auto pspan : stacked_spans)
	{
		int orig_y = spr.ems_.linemap_opt_2_src[pspan->line_opt];

		u16 *psourceline = &shift.data_[(orig_y * shift.wordwidth_)];
		u16 *psharedline = &psharedmask[(orig_y * sharedmask_wordwidth)];

		u16 em1 = 0;
		u16 em2 = 0;
		u16 em3 = 0;

		u16 sm1 = 0;
		u16 sm2 = 0;
		u16 sm3 = 0;

		int o1 = pspan->ws;
		int o2 = pspan->ws + 1;
		int o3 = (pspan->ws + pspan->ww) - 1;

		sm1 = psharedline[o1];
		em1 = psourceline[o1];

		if (sm1 != last_sm1)
		{
			shift.shared_mask_words.push_back(em1);
			last_sm1 = sm1;
		}

		if (pspan->ww >= 3)
		{
			sm2 = psharedline[o2];
			if (o2 < shift.wordwidth_)
				em2 = psourceline[o2];

			if (sm2 != last_sm2)
			{
				shift.shared_mask_words.push_back(em2);
				last_sm2 = sm2;
			}
		}

		if (pspan->ww >= 2)
		{
			sm3 = psharedline[o3];
			if (o3 < shift.wordwidth_)
				em3 = psourceline[o3];

			if (sm3 != last_sm3)
			{
				shift.shared_mask_words.push_back(em3);
				last_sm3 = sm3;
			}
		}
	}

	printf("mask seq len: %d\n", shift.shared_mask_words.size());
}

void generate_emh_shared_shift_analysis
(
	const spriteframe_t &_spr,
	preshift_t &_shared_shift,
	int _max_shift,
	bool _single_span_scans
)
{
	bool allow_NFSR = !g_allargs.no_nfsr;

	int full_src_wordwidth = ((_spr.w_ + 15) & -16) >> 4;
	int src_pixwidth = full_src_wordwidth << 4;

	// extract patterns of spans according to individual span constraints

	auto &sa = _shared_shift.sa;
	sa.span_vpattern_.resize(_spr.h_);

	for (int v = 0; v < _spr.h_; v++)
	{
		auto &span_hseq = sa.span_vpattern_[v];

		int accumulated_bcycles = 0;
		int accumulated_words = 0;
		int nonzero_words = 0;
		int span_pos = -1;
		u16 last_word = 0;

		u16 em[3] = { 0, 0, 0 };

		int w = 0;
		bool FISR = false;

		while (w < _shared_shift.wordwidth_)
		{
			// fetch next word
			u16 *psharedline = &_shared_shift.data_[(v * _shared_shift.wordwidth_)];
			u16 *psourceline = &_spr.mask_[(v * full_src_wordwidth)];

			u16 word = psharedline[w];

			int word_pos = w++;

			// detect start of new span
			if ((span_pos < 0) && (word != 0))
			{
				span_pos = word_pos;

				if (span_pos > 0)
				{
					// TODO: fix this to work with shared mask encoding
					//
					// check reference to shifted previous data
					u16 mword = ~(psourceline[span_pos - 1]);
					u32 shiftbuffer = u32(mword) << 16;

					FISR = ((u16(shiftbuffer >> _max_shift) & 0x0000FFFF) != 0);
				}
			}

			if (word == 0)
			{
				// absorb zero masks, but emit pending span

				if (nonzero_words > 0)
				{
					//					printf("zero-term:\n");
					//					printf("nonzero words:%d  acc words:%d\n", nonzero_words, accumulated_words);

					// TODO: fix this to work with shared mask encoding
					//
					// check reference to current data (not shifted from previous)
					bool NFSR =
						allow_NFSR &&
						(accumulated_words > 1) &&
						(psourceline[span_pos + accumulated_words - 1] == 0xFFFF);

					// gap of at least 4*3=12 bus cycles, terminate span here
					span_hseq.emplace_back
						(
						constrained_wordspan
						(
						/*ws=*/span_pos,
						/*ww=*/accumulated_words,
						/*cxs=*/FISR,
						/*cxe=*/NFSR,
						/*sline=*/v,
						/*sline_aw=*/_spr.active_widths[v],
						/*pem=*/em,
						/*bccount=*/accumulated_bcycles + (FISR ? 1 : 0) - (NFSR ? 1 : 0)
						)
						);
					em[0] = em[1] = em[2] = 0;

					span_pos = -1;
					accumulated_words = 0;
					nonzero_words = 0;
					accumulated_bcycles = 0;
					FISR = false;

					last_word = 0;
				}
			}
			else
			{
				// count bus cycles
				int word_bcycles = 2;	// +srcread +dstwrite
				if (word != 0xFFFF)
					word_bcycles++;		// +dstread

				if ((accumulated_bcycles + word_bcycles + (FISR ? 1 : 0)) > c_max_buscycles_per_plane)
				{
					//					printf("bus limit:\n");
					//					printf("nonzero words:%d  acc words:%d\n", nonzero_words, accumulated_words);

					//					// check reference to shifted previous data
					//					bool FISR =
					//						(span_pos > 0) &&
					//						(u16(0xFFFF0000UL >> _shift.shift_) & pline[span_pos]);

					// TODO: fix this to work with shared mask encoding
					//
					// check reference to current data (not shifted from previous)
					bool NFSR =
						allow_NFSR &&
						(accumulated_words > 1) &&
						(psourceline[span_pos + accumulated_words - 1] == 0xFFFF);

					// span constraints would be exceeded, emit span & continue
					span_hseq.emplace_back
						(
						constrained_wordspan
						(
						/*ws=*/span_pos,
						/*ww=*/accumulated_words,
						/*cxs=*/FISR,
						/*cxe=*/NFSR,
						/*sline=*/v,
						/*sline_aw=*/_spr.active_widths[v],
						/*pem=*/em,
						/*bccount=*/accumulated_bcycles + (FISR ? 1 : 0) - (NFSR ? 1 : 0)
						)
						);
					em[0] = em[1] = em[2] = 0;

					span_pos = -1;
					accumulated_words = 0;
					nonzero_words = 0;
					accumulated_bcycles = 0;
					FISR = false;

					last_word = 0;
				}

				// record changes in mask state, up to 3
				// force commit of new mask word (EM2) after first (EM1)
				if ((last_word != word) || (nonzero_words == 1))
				{
					last_word = word;
					em[nonzero_words++] = word;
				}

				accumulated_words++;
				accumulated_bcycles += word_bcycles;

				// check constraints
				if
					(
					// note: can reach end of line with width >2 on a fill, but not allocated EM3 yet!
					(w == _shared_shift.wordwidth_) ||	// end of line
					(nonzero_words == 3)		// exhausted EMs
					)
				{
					//					printf("eol:%d\n", (w == _shift.wordwidth_) ? 1 : 0);
					//					printf("nonzero words:%d  acc words:%d\n", nonzero_words, accumulated_words);

					// check reference to shifted previous data
					//					bool FISR =
					//						(span_pos > 0) &&
					//						(u16(0xFFFF0000UL >> _shift.shift_) & pline[span_pos]);

					// TODO: fix this to work with shared mask encoding
					//
					// check reference to current data (not shifted from previous)
					bool NFSR =
						allow_NFSR &&
						(accumulated_words > 1) &&
						(psourceline[span_pos + accumulated_words - 1] == 0xFFFF);

					// span constraints would be exceeded, emit span & continue
					span_hseq.emplace_back
						(
						constrained_wordspan
						(
						/*ws=*/span_pos,
						/*ww=*/accumulated_words,
						/*cxs=*/FISR,
						/*cxe=*/NFSR,
						/*sline=*/v,
						/*sline_aw=*/_spr.active_widths[v],
						/*pem=*/em,
						/*bccount=*/accumulated_bcycles + (FISR ? 1 : 0) - (NFSR ? 1 : 0)
						)
						);
					em[0] = em[1] = em[2] = 0;

					span_pos = -1;
					accumulated_words = 0;
					nonzero_words = 0;
					accumulated_bcycles = 0;
					FISR = false;

					last_word = 0;
				}
			}
		}

		if (_single_span_scans)
		{
			// strip out all but first span per scanline
			for (int v = 0; v < _spr.h_; v++)
			{
				auto &span_hseq = sa.span_vpattern_[v];
				if (!span_hseq.empty())
					span_hseq.resize(1);
			}
		}
	}
}


int generate_emh_enumerate_components
(
	int emh_layout,
	preshift_t &shift
)
{
	int num_components_required = 1;

	switch (emh_layout)
	{
	case 0:
		// horizontal-sequential organized
		{
			auto &sa = shift.sa;
			for (auto & spanlist : sa.span_vpattern_)
			{
				if (spanlist.size() > num_components_required)
					num_components_required = spanlist.size();
			}
		}
		break;

	case 1:
		// width-organized
		{
			auto &sa = shift.sa;

			std::map<int, int> xmap;
			std::map<uint32_t, std::vector<constrained_wordspan*>> ordered_spans;

			int component_index = 0;

			for (auto & spanlist : sa.span_vpattern_)
			{
				int horizontal_index = 0;
				for (auto &span : spanlist)
				{
					auto look = xmap.find(span.ww);
					if (look == xmap.end())
					{
						horizontal_index = component_index++;
						xmap[span.ww] = horizontal_index;
					}
				}
			}

			num_components_required = xmap.size();
		}
		break;

	case 2:
		// single component
		break;
	};

	return num_components_required;
}

void generate_emh_order_spans
(
	int emh_layout,
	spriteframe_t &spr, 
	preshift_t &shift, int ci,
	std::vector<constrained_wordspan*> &_stacked_spans
)
{
	std::map<int, int> xmap;
	std::map<uint32_t, std::vector<constrained_wordspan*>> ordered_spans;
	
	int component_count = 0;

	switch (emh_layout)
	{
	case 0:
		// horizontal-sequential organized
		{
			for (auto & spanlist : shift.sa.span_vpattern_)
			{
				int component_output_index = 0;
				for (auto &span : spanlist)
				{
					if (component_output_index == ci)
					{
						//auto line_opt = spr.ems_.linemap_src_2_opt[span.line_orig];
						//assert(line_opt == span.line_opt);
						//span.line_opt = line_opt;

						// span index has priority so spans rendered vertically for edge coherence, never horizontally
						// otherwise, frame common lineorder prevails
						uint32_t ordering_key;

						// left->right spans within top-bottom ordered scanlines, grouped by width, largest first
						ordering_key =
							((256-span.ww) << 16) |
							(uint32_t(span.line_opt))
							;

						ordered_spans[ordering_key].push_back(&span);
						break;
					}

					component_output_index++;
				}
			}
		}
		break;

	case 1:
		// width-organized
		{
			for (auto & spanlist : shift.sa.span_vpattern_)
			{
				int component_output_index = 0;
				for (auto &span : spanlist)
				{
					auto look = xmap.find(span.ww);
					if (look == xmap.end())
					{
						component_output_index = component_count++;
						xmap[span.ww] = component_output_index;
					}
					else
						component_output_index = look->second;

					if (component_output_index == ci)
					{
						//span.line_opt = spr.ems_.linemap_src_2_opt[span.line_orig];

						// span index has priority so spans rendered vertically for edge coherence, never horizontally
						// otherwise, frame common lineorder prevails
						uint32_t ordering_key;

						// left->right spans within top-bottom ordered scanlines
						ordering_key =
							(uint32_t(span.line_opt))
							;

						ordered_spans[ordering_key].push_back(&span);
					}
				}
			}
		}
		break;

	case 2:
		// left-right, top-bottom single component
		{
			int max_group = -1;
			for (auto & spanlist : shift.sa.span_vpattern_)
			{
				int hgroup = 0;
				for (auto &span : spanlist)
				{
					int key, group;
					if (0)
					{
						group = key = hgroup;
					}
					else
					{
						key  = span.ws;
						auto look = xmap.find(key);
						if (look == xmap.end())
						{
							group = ++max_group;
							xmap[key] = group;
						}
						else
							group = look->second;
					}

					uint32_t ordering_key =
						(uint32_t(key) << (32 - 5)) |
						(uint32_t(32-span.ww) << (32 - 10)) |
						(uint32_t(span.cxs)&1 << (32 - 15)) |
						(uint32_t(span.cxe)&1 << (32 - 16)) |
						(uint32_t(span.line_opt) << 0)
						;
/*
					// span index has priority so spans rendered vertically for edge coherence, never horizontally
					// otherwise, frame common lineorder prevails
					uint32_t ordering_key;

					// left->right spans within top-bottom ordered scanlines
					ordering_key =
						(horizontal_index << 24) |
//							(uint32_t(span.line_opt) << 5) |
//							(uint32_t(span.ws))
						((255-span.ww) << 16) |
						(uint32_t(span.line_opt))
						;
*/
					span.group = group; // use this everywhere downstream

					ordered_spans[ordering_key].push_back(&span);

					++hgroup;
				}
			}
		}
		break;

	}

	int o = 0;
	for (auto group_it = ordered_spans.begin(); group_it != ordered_spans.end(); group_it++)
	{
		for (auto pspan : group_it->second)
		{
			pspan->draw_order = o++;
			_stacked_spans.push_back(pspan);
		}
	}

	// output image showing span pattern
	if (g_allargs.debug || g_allargs.visual)
	{
		std::string diagnostic_name(g_allargs.explicit_sources.front());
		remove_extension(diagnostic_name);
		diagnostic_name.append("_spans_emh.png");
		printf("writing span diagnostic: %s @ %dx%d\n", diagnostic_name.c_str(), spr.w_, spr.h_);
		output_span_diagnostic(spr.w_, spr.h_, _stacked_spans, diagnostic_name);
	}
}

//#include "emlineorder.inl"

void generate_emh_frame_flexible(spriteframe_t &spr, int _frame)
{
	if (g_allargs.verbose)
		std::cout << "frame [" << _frame << "]" << std::endl << std::flush;

/*
	// find an initial common lineorder which removes empty lines and minimises gaps between spans.
	generate_emx_emh_initial_lineorder
	(
		spr,
		spr.ems_
	);
*/

	// analyse all preshifts within this frame
	int sharedmask_wordwidth = 0;

	// find widest mask pattern actually used
	for (auto pmit = spr.ems_.psmask_frames_.begin(); pmit != spr.ems_.psmask_frames_.end(); pmit++)
	{
		preshiftlist_t::iterator cit = pmit->begin();
		auto & spshift = *cit;

		if (spshift->wordwidth_ > sharedmask_wordwidth)
			sharedmask_wordwidth = spshift->wordwidth_;
	}

	// allocate new shared mask for each axis
	size_t totalmaskwords = spr.h_ * sharedmask_wordwidth;

	uword_t * psharedmask_h = new uword_t[totalmaskwords];
	memset(psharedmask_h, 0, sizeof(uword_t) * totalmaskwords);

//	uword_t * psharedmask_v = new uword_t[totalmaskwords];
//	memset(psharedmask_v, 0, sizeof(uword_t) * totalmaskwords);

	int psi = 0;
	for (auto pmit = spr.ems_.psmask_frames_.begin(); pmit != spr.ems_.psmask_frames_.end(); pmit++, psi++)
	{
		// first component within a preshift
		preshiftlist_t::iterator cit = pmit->begin();
		{
			auto & spshift = *cit;
			preshift_t &shift = *spshift;

			// find all mask words which change relative to their predecessor, build a shared mask for encoding

			for (int v = 0; v < spr.h_; v++)
			{
				u16 last_word = 0;

				u16 *psourceline = &shift.data_[(v * shift.wordwidth_)];
				u16 *psharedline = &psharedmask_h[(v * sharedmask_wordwidth)];

				int w = 0;
				while (w < shift.wordwidth_)
				{
					// fetch next word
					int word_pos = w++;
					u16 word = psourceline[word_pos];

					// mark all nonzero words seen on this preshift
					if (word != 0)
						psharedline[word_pos] |= 0x4000;

					// mark any adjacent deltas seen on this preshift
					// including first nonzero word used
					if (word != last_word)
						psharedline[word_pos] |= 0x8000;

					last_word = word;
				}

			} // lines

		} // components

	} // preshifts

	// generate word pattern to drive production of a common mask
	for (int v = 0; v < spr.h_; v++)
	{
		u16 *psharedline = &psharedmask_h[(v * sharedmask_wordwidth)];

		u16 last_encoding = 0;

		int w = 0;
		while (w < sharedmask_wordwidth)
		{
			// fetch next word
			int word_pos = w++;
			u16 word = psharedline[word_pos];

			if (word & 0x4000)
			{
				// words which were seen to contain nonzero on any preshift, must be encoded
				if (word & 0x8000)
				{
					// words which saw at least one change are encoded uniquely
					word = ++last_encoding;
				}
				else
				{
					if (last_encoding == 0)
					{
						printf("error: bad EM pattern encoding\n");
						exit(1);
					}

					// words which did not see any changes and are always nonzero are encoded as repeats (usually a fill)
					word = last_encoding;
				}
			}
			else
			{
				// always-zero words remain zero words
				word = 0;
			}

			psharedline[word_pos] = word;
		}
	}

	// dump shared mask encoding
	if (g_allargs.verbose)
	{
		printf("shared mask encoding pass 1 (horizontal)...\n");
		for (int v = 0; v < spr.h_; v++)
		{
			u16 *psharedline = &psharedmask_h[(v * sharedmask_wordwidth)];

			int w = 0;
			while (w < sharedmask_wordwidth)
			{
				// fetch next word
				int word_pos = w++;
				u16 word = psharedline[word_pos];

				print_dataword(word);
				printf(":");
			}
			printf("\n");
		}
		printf("\n");
	}

	auto & spshiftref = *(spr.ems_.psmask_frames_.begin()->begin());
	preshift_t &ref = *spshiftref;

	int max_shift = 16 - g_allargs.preshift_step;

	// create new, common shift accommodating all preshifts
	preshift_t *pshared_shift = new
		preshift_t
		(
			/*shift=*/max_shift, // =0 inhibits NFSR
			/*planes=*/ref.planes_,
			/*height=*/ref.height_,
			/*wordwidth=*/sharedmask_wordwidth,
			/*wordoffset=*/ref.wordoffset_,
			/*wordsremaining=*/ref.wordsremaining_,
			/*data=*/psharedmask_h
		);

	preshift_t &shared_analysis_shift = *pshared_shift;

	// build span representation
	//generate_emx_preshift_analysis(spr, shared_shift, /*psi=*/0, /*single_span_scans=*/true);


#if (0)
	// analyse all preshifts within this frame
	{
		std::vector<preshiftlist_t>::iterator pmit = spr.ems_.psmask_frames_.begin();

		int psi = 0;
		for (; pmit != spr.ems_.psmask_frames_.end(); pmit++, psi++)
		{
			// first component within a preshift
			preshiftlist_t::iterator cit = pmit->begin();
			{
				auto & spshift = *cit;
				preshift_t &shift = *spshift;

				generate_emx_preshift_analysis(spr, shift, psi, /*single_span_scans=*/false);
				if (g_allargs.verbose)
					generate_emx_emh_span_pattern_audit(spr, shift);
			}
		}
	}
#endif


#define EMH_MULTICOMPONENT

	// generate all spans in one pass
#if defined(EMH_MULTICOMPONENT)
	generate_emh_shared_shift_analysis(spr, shared_analysis_shift, /*max_shift=*/max_shift, /*single_span_scans=*/false);
#else
	generate_emh_shared_shift_analysis(spr, shared_analysis_shift, /*max_shift=*/max_shift, /*single_span_scans=*/true);
#endif
	if (g_allargs.verbose)
		generate_emx_emh_span_pattern_audit(spr, shared_analysis_shift);

	// find common line order
	generate_emx_emh_common_lineorder
	(
		spr, 
		spr.ems_, 
		spr.ems_.linemap_opt_2_src, spr.ems_.linemap_src_2_opt, 
		/*optimize=*/!(USE_EMH_CLIPTABLE && !g_allargs.noclipy),
		/*shared_shift=*/&shared_analysis_shift
	);

	// order spans within each component
	//std::vector<constrained_wordspan*> &all_stacked_spans = shared_analysis_shift.stacked_spans;
	//generate_emx_emh_order_spans(spr, _frame, shared_analysis_shift, /*pi=*/0, all_stacked_spans, /*emh=*/true);

	// load line remapping into spans
	for (auto & spanlist : shared_analysis_shift.sa.span_vpattern_)
	{
		for (auto &span : spanlist)
		{
			span.line_opt = spr.ems_.linemap_src_2_opt[span.line_orig];
		}
	}

	int emh_layout = 2;

	// measure required component count
	int num_components_required = generate_emh_enumerate_components(emh_layout, shared_analysis_shift);

	printf("EMH requires %d components\n", num_components_required);

	// create per-component codegen proxy
	std::vector<std::shared_ptr<preshift_t>> component_proxy_shifts;
	{
		int num_components = 0;
		while (num_components++ < num_components_required)
		{
			auto spproxy = std::make_shared<preshift_t>(shared_analysis_shift);

			uword_t * psharedmask_v = new uword_t[totalmaskwords];
			memset(psharedmask_v, 0, sizeof(uword_t) * totalmaskwords);
			spproxy->data_ = psharedmask_v;

			component_proxy_shifts.emplace_back(spproxy);
			printf("added EMH component %d\n", num_components);
		}
	}


//#if defined(EMH_MULTICOMPONENT)
	// duplicate each preshift index into a component sequence
	psi = 0;
	for (auto pmit = spr.ems_.psmask_frames_.begin(); pmit != spr.ems_.psmask_frames_.end(); pmit++, psi++)
	{
		preshiftlist_t::iterator cit = pmit->begin();
		auto & spshift = *cit;

		// copy span analysis into first and all subsequent components
		spshift->sa = shared_analysis_shift.sa;

		int num_components = 1;
		while (num_components++ < num_components_required)
		{
			printf("duplicating preshift %d component %d...\n", psi, num_components);
			pmit->emplace_back
			(
				std::make_shared<preshift_t>(*spshift)
			);
		}
	}

	// filter spans into components
	int ci = 0;
	for (auto & spproxy : component_proxy_shifts)
	{
		spproxy->stacked_spans.clear();

		generate_emh_order_spans(emh_layout, spr, *spproxy, ci, spproxy->stacked_spans);

		printf("filtered %d spans into component %d\n", spproxy->stacked_spans.size(), ci);

		ci++;
	}
//#endif

	// split spans into minimum number of horizontal components with <=1 span per line
/*	std::list<std::shared_ptr<preshift_t>> shared_shift_components;
	generate_emx2_order_component_spans(spr, shared_shift, shared_shift_components);

	if (shared_shift_components.empty())
	{
		printf("error: extracted no EMH components!\n");
		exit(1);
	}

*/
/*
	std::shared_ptr<preshift_t> spcmp = shared_shift_components.front();
	std::vector<constrained_wordspan*> stacked_spans = spcmp->stacked_spans;
*/

	// ***HACK*** steal spans from 1st component
/*	std::vector<constrained_wordspan*> __stacked_spans;
	{
		auto pmit = spr.ems_.psmask_frames_.begin();
		preshiftlist_t::iterator cit = pmit->begin();
		auto & spshift = *cit;
		__stacked_spans = spshift->stacked_spans;
	}
*/
	//size_t spancount = all_stacked_spans.size();

	// assign EM index to source mask words according to their position in each span
//	psi = 0;
//	for (auto pmit = spr.ems_.psmask_frames_.begin(); pmit != spr.ems_.psmask_frames_.end(); pmit++, psi++)
	{
		// first component within a preshift
//		int ci = 0;
//		for (preshiftlist_t::iterator cit = pmit->begin(); cit != pmit->end(); cit++, ci++)
		for (auto & spproxy : component_proxy_shifts)
		{
//			std::cout << "proxy shift: " << std::endl;
//			auto & spshift = *cit;
//			preshift_t &shift = *spshift;

//			auto & spproxy = component_proxy_shifts[ci];
			preshift_t &proxy = *spproxy;

			for (auto pspan : proxy.stacked_spans)
			{
				//int orig_y = spr.ems_.linemap_opt_2_src[pspan->line_opt];
				//assert(orig_y == pspan->line_orig);
				//std::cout << "proxy shift span: opty=" << pspan->line_opt << " srcy=" << orig_y << std::endl;
				int orig_y = pspan->line_orig;

				u16 *psharedline = &proxy.data_[(orig_y * sharedmask_wordwidth)];

				int o1 = pspan->ws;
				int o2 = pspan->ws + 1;
				int o3 = (pspan->ws + pspan->ww) - 1;

				psharedline[o1] = 1;

				if (pspan->ww >= 2)
					psharedline[o3] = 3;

				if (pspan->ww >= 3)
				{
					for (int p = o2; p < o3; p++)
						psharedline[p] = 2;
				}
			}
		}
	}

	// analyse EM deltas between spans, in span-order.
	// update the common mask to indicate deltas seen within any preshift.
	// then re-generate the common mask to encode these changes.
	{
		psi = 0;
		for (auto pmit = spr.ems_.psmask_frames_.begin(); pmit != spr.ems_.psmask_frames_.end(); pmit++, psi++)
		{
			// first component within a preshift
			int ci = 0;
			for (preshiftlist_t::iterator cit = pmit->begin(); cit != pmit->end(); cit++, ci++)
			{
				auto & spshift = *cit;
				preshift_t &shift = *spshift;

				auto & spproxy = component_proxy_shifts[ci];
				preshift_t &proxy = *spproxy;

				shift.occupied_lines.clear();

				{
					u16 em_last[3] = { 0,0,0 };
					u16 em_mark[3] = { 0,0,0 };

					//for (auto pspan : all_stacked_spans)
					for (auto pspan : proxy.stacked_spans)
					{
						//int orig_y = spr.ems_.linemap_opt_2_src[pspan->line_opt];
						//assert(orig_y == pspan->line_orig);
						int orig_y = pspan->line_orig;

						shift.occupied_lines[orig_y] = true;

						u16 *psourceline = &shift.data_[(orig_y * shift.wordwidth_)];
						u16 *psharedline = &proxy.data_[(orig_y * sharedmask_wordwidth)];

						// load real EM values for this span, for this preshift (for those within h-window)
						u16 em1 = 0;
						u16 em2 = 0;
						u16 em3 = 0;

						int o1 = pspan->ws;
						int o2 = pspan->ws + 1;
						int o3 = (pspan->ws + pspan->ww) - 1;

						if (o1 < shift.wordwidth_)
							em1 = psourceline[o1];

						if (o2 < shift.wordwidth_)
							em2 = psourceline[o2];
							
						if (o3 < shift.wordwidth_)
							em3 = psourceline[o3];

						// signal state changes via EM index generations
	
						if ((0 == em_mark[0]++) || (em1 != em_last[0]))
						{
							// EM1 changed
							em_last[0] = em1;
							psharedline[o1] |= 0x8000;
						}

						if (pspan->ww >= 2)
						{
							if ((0 == em_mark[2]++) || (em3 != em_last[2]))
							{
								// EM3 changed
								em_last[2] = em3;
								psharedline[o3] |= 0x8000;
							}
						}

						if (pspan->ww >= 3)
						{
							if ((0 == em_mark[1]++) || (em2 != em_last[1]))
							{
								// EM2 changed
								em_last[1] = em2;
								// this can be a run, so mark all affected words
								for (int p = o2; p < o3; p++)
									psharedline[p] |= 0x8000;
							}
						}
					}
				}
			}
		}

		// reassign vertical changes as em generations
		{
			//psi = 0;
			//for (auto pmit = spr.ems_.psmask_frames_.begin(); pmit != spr.ems_.psmask_frames_.end(); pmit++, psi++)
			{
				// first component within a preshift
				//int ci = 0;
				//for (preshiftlist_t::iterator cit = pmit->begin(); cit != pmit->end(); cit++, ci++)
				for (auto & spproxy : component_proxy_shifts)
				{
					//auto & spshift = *cit;
					//preshift_t &shift = *spshift;

					//auto & spproxy = component_proxy_shifts[ci];
					preshift_t &proxy = *spproxy;

					u16 em_gens[3] = { 0, 0, 0 };

					//for (auto pspan : all_stacked_spans)
					for (auto pspan : proxy.stacked_spans)
					{
						//int orig_y = spr.ems_.linemap_opt_2_src[pspan->line_opt];
						//assert(orig_y == pspan->line_orig);
						int orig_y = pspan->line_orig;

						//u16 *psourceline = &shift.data_[(orig_y * shift.wordwidth_)];
						u16 *psharedline = &proxy.data_[(orig_y * sharedmask_wordwidth)];

						// load real EM values for this span, for this preshift (for those within h-window)
						u16 em1 = 0;
						u16 em2 = 0;
						u16 em3 = 0;

						int o1 = pspan->ws;
						int o2 = pspan->ws + 1;
						int o3 = (pspan->ws + pspan->ww) - 1;

						em1 = psharedline[o1];
						em2 = psharedline[o2];
						em3 = psharedline[o3];

						// signal state changes via EM index generations

						if (em1 & 0x8000)
						{
							// EM1 changed
							em_gens[0]++;
							psharedline[o1] |= em_gens[0] << 6;
						}

						if (pspan->ww >= 2)
						{
							if (em3 & 0x8000)
							{
								// EM3 changed
								em_gens[2]++;
								psharedline[o3] |= em_gens[2] << 6;
							}
						}

						if (pspan->ww >= 3)
						{
							if (em2 & 0x8000)
							{
								// EM2 changed
								em_gens[1]++;
								for (int p = o2; p < o3; p++)
									psharedline[p] |= em_gens[1] << 6;
							}
						}
					}
				}
			}
		}

	}



	// load new pseudo-masks back into spans for metaprogram gen / simulation
	{
		//psi = 0;
		//for (auto pmit = spr.ems_.psmask_frames_.begin(); pmit != spr.ems_.psmask_frames_.end(); pmit++, psi++)
		{
			// first component within a preshift
			//int ci = 0;
			//for (preshiftlist_t::iterator cit = pmit->begin(); cit != pmit->end(); cit++, ci++)
			for (auto & spproxy : component_proxy_shifts)
			{
				//auto & spshift = *cit;
				//preshift_t &shift = *spshift;

				//auto & spproxy = component_proxy_shifts[ci];
				preshift_t &proxy = *spproxy;

				//for (auto pspan : all_stacked_spans)
				for (auto pspan : proxy.stacked_spans)
				{
					//int v = pspan->draw_order;
					//int orig_y = spr.ems_.linemap_opt_2_src[pspan->line_opt];
					//assert(orig_y == pspan->line_orig);
					int orig_y = pspan->line_orig;

					u16 *psharedline = &proxy.data_[(orig_y * sharedmask_wordwidth)];

					int o1 = pspan->ws;
					int o2 = pspan->ws + 1;
					int o3 = (pspan->ws + pspan->ww) - 1;
					
					pspan->em[0] = psharedline[o1];
					if (pspan->ww >= 2)
						pspan->em[2] = psharedline[o3];
					if (pspan->ww >= 3)
						pspan->em[1] = psharedline[o2];
				}

				if (g_allargs.verbose)
				{
					printf("shared mask encoding pass 2 (vertical)...\n");
					for (int v = 0; v < spr.h_; v++)
					{
						u16 *psharedline = &proxy.data_[(v * sharedmask_wordwidth)];

						int w = 0;
						while (w < sharedmask_wordwidth)
						{
							// fetch next word
							int word_pos = w++;
							u16 word = psharedline[word_pos];

							print_dataword(word);
							printf(":");
						}
						printf("\n");
					}
					printf("\n");
				}

			}
		}
	}


	// generate mask sequence for each preshift
	psi = 0;
	for (auto pmit = spr.ems_.psmask_frames_.begin(); pmit != spr.ems_.psmask_frames_.end(); pmit++, psi++)
	{
		// first component within a preshift
		int ci = 0;
		for (preshiftlist_t::iterator cit = pmit->begin();  cit != pmit->end(); cit++, ci++)
		{
			auto & spshift = *cit;
			preshift_t &shift = *spshift;

			auto & spproxy = component_proxy_shifts[ci];
			preshift_t &proxy = *spproxy;

			shift.shared_mask_words.clear();
			generate_emh_mask_sequence(spr, shift, proxy.stacked_spans, proxy.data_, sharedmask_wordwidth);

			printf("generated preshift %d component %d mask sequence with %d entries\n", 
				psi, 
				ci, 
				shift.shared_mask_words.size()
			);
		}
	}


//	std::cout << std::flush;
//	exit(1);
	std::cout << "assign shared mask..." << std::endl << std::flush;


	delete[] shared_analysis_shift.data_;
/*	shared_analysis_shift.data_ = psharedmask_v;


	// assign shared shift pattern to codegen proxy shifts
	for (auto & spproxy : component_proxy_shifts)
	{
		spproxy->data_ = psharedmask_v;
	}

	std::cout << "assigned shared mask to proxy set" << std::endl << std::flush;
*/

	// generate mask sequence for each preshift
/*	psi = 0;
	for (auto pmit = spr.ems_.psmask_frames_.begin(); pmit != spr.ems_.psmask_frames_.end(); pmit++, psi++)
	{
		// strip away all but first component
		while (pmit->size() > 1)
		{
			pmit->erase(--(pmit->end()));
		}

		std::cout << "trimmed compontents" << std::endl << std::flush;
	}
	*/

	// first component within a preshift
	ci = 0;
	for (auto & spproxy : component_proxy_shifts)
	{
		//auto pmit = spr.ems_.psmask_frames_.begin();
		//preshiftlist_t::iterator cit = pmit->begin();
		//auto & spshift = *cit;
		//preshift_t &shift = *spshift;

//		auto & spproxy = component_proxy_shifts[ci];
		preshift_t &proxy = *spproxy;

		generate_emh_mask_sequence(spr, proxy, proxy.stacked_spans/*all_stacked_spans*/, proxy.data_, sharedmask_wordwidth);
		std::cout << "generated compontent mask seq" << std::endl << std::flush;

		std::vector<u16> shared_mask_words_gen;
		generate_emx_emh_metaprogram(spr, _frame, proxy, /*pi=*/0, proxy.stacked_spans, /*shared_mask=*/true, shared_mask_words_gen);
		std::cout << "generated compontent metaprogram" << std::endl << std::flush;

		if (ci == 0)
		{
			spr.ems2_p68kbin_ = proxy.p68kbin;
			spr.ems2_size68kbin_ = proxy.size68kbin;
		}

		ci++;
	}

	std::cout << "generated all compontent metaprograms" << std::endl << std::flush;


//	delete[] shared_shift.data_;
//	shared_shift.data_ = psharedmask_v;

#if  0

	// for simulator
	generate_emh_mask_sequence(spr, shared_shift, all_stacked_spans, psharedmask_v, sharedmask_wordwidth);


	std::vector<u16> shared_mask_words_gen;
	generate_emx_emh_metaprogram(spr, _frame, shared_shift, /*pi=*/0, all_stacked_spans, /*shared_mask=*/true, shared_mask_words_gen);

	spr.ems2_p68kbin_ = shared_shift.p68kbin;
	spr.ems2_size68kbin_ = shared_shift.size68kbin;

#endif

	// assign shared metaprogram to all preshifts

	{
		psi = 0;
		for (auto pmit = spr.ems_.psmask_frames_.begin(); pmit != spr.ems_.psmask_frames_.end(); pmit++, psi++)
		{
			int firstspan = 1;

			// first component within a preshift
			int ci = 0;
			for (preshiftlist_t::iterator cit = pmit->begin(); cit != pmit->end(); cit++, ci++)
			{
				auto & spshift = *cit;
				preshift_t &shift = *spshift;

				auto & spproxy = component_proxy_shifts[ci];
				preshift_t &proxy = *spproxy;

				printf("preshift %d component %d with %d spans\n", psi, ci, proxy.stacked_spans.size());

				shift.scan_states = proxy.scan_states;
				shift.first_scan_state = proxy.first_scan_state;

				shift.p68kbin = proxy.p68kbin;
				shift.size68kbin = proxy.size68kbin;

				// extract mask data

				shift.shared_mask_words.clear();

				if (1)
				{
					int last_y = -1;
					u16 em1 = 0;
					u16 em2 = 0;
					u16 em3 = 0;

					for (auto pspan : proxy.stacked_spans)
					{	
						//int scan_y = pspan->line_opt;
						//int orig_y = spr.ems_.linemap_opt_2_src[scan_y];
						//assert(orig_y == pspan->line_orig);
						int orig_y = pspan->line_orig;

						bool newline = (orig_y != last_y);
						last_y = orig_y;

						// record EM state at the beginning of each new scanline
						if (newline)
						{
							auto & state = shift.scan_states[orig_y];
							state.emdata_offset = shift.shared_mask_words.size();

							auto & blitstate = state.blitstate;
							blitstate.EM1 = em1;
							blitstate.EM2 = em2;
							blitstate.EM3 = em3;
							blitstate.SKEW = (blitstate.SKEW & 0xC0) | (shift.shift_ & 15);

							if (firstspan)
							{
								firstspan = 0;
								assert(shift.first_scan_state.mprog_offset == 0);

								auto & blitstate = shift.first_scan_state.blitstate;
								blitstate.SKEW = (blitstate.SKEW & 0xC0) | (shift.shift_ & 15);
							}
						}

						// load real EM values for this span, for this preshift (for those within h-window)

						u16 *psourceline = &shift.data_[(orig_y * shift.wordwidth_)];

						int o1 = pspan->ws;
						int o2 = pspan->ws + 1;
						int o3 = (pspan->ws + pspan->ww) - 1;

						if (pspan->reload[0])
						{
							em1 = 0;
							if (o1 < shift.wordwidth_)
								em1 = psourceline[o1];
							shift.shared_mask_words.push_back(em1);
						}

						if (pspan->reload[1])
						{
							em2 = 0;
							if (o2 < shift.wordwidth_)
								em2 = psourceline[o2];
							shift.shared_mask_words.push_back(em2);
						}

						if (pspan->reload[2])
						{
							em3 = 0;
							if (o3 < shift.wordwidth_)
								em3 = psourceline[o3];
							shift.shared_mask_words.push_back(em3);
						}
					}
				}


				// reverse pass, deal with empty lines
				if (USE_EMH_CLIPTABLE && !g_allargs.noclipy)
				{
//					int lines_remaining = 0;
//					int accumulated_offset = 0;
					int last_active = -1;
					for (int y = shift.scan_states.size() - 1; y >= 0; y--)
					{
						auto &ss = shift.scan_states[y];

						if (ss.allocated)
						{
							last_active = y;
						}
						else
						{
							if (last_active < 0)
							{
								// load terination state into trailing lines
								ss.mprog_offset = proxy.mprog_final;
							}
							else
							{
								// duplicate state
								const auto &ss_last = shift.scan_states[last_active];
								ss = ss_last;
								// but leave unallocated
								ss.allocated = false;
							}
						}

/*						if (shift.occupied_lines.find(y) != shift.occupied_lines.end())
						{
							lines_remaining++;
							accumulated_offset = 0;
						}
						else
						{
							accumulated_offset++;
						}

						ss.lines_remaining = lines_remaining;
						//ss.dst_line_offset = accumulated_offset;
*/
					}

					// dump the scan states
					if (1)
					{
						//u16* mprog = (u16*)(shift.p68kbin+32);
						//u16* mprog = (u16*)(spr.ems2_p68kbin_+32);
						u16* mprog = (u16*)(proxy.p68kbin + 32);

						//printf("mprog:%08x\n", (int)mprog);

						printf("\npreshift %d component %d with %d mask words...\n", psi, ci, shift.shared_mask_words.size());
						int i = 0;
						for (const auto & ss : shift.scan_states)
						{
							const auto & bs = ss.blitstate;

							if (g_allargs.verbose)
							{
								printf("scanstate[%03d]: %c dlo:%03d dwo:%02d lr:%03d emo:%04x mpo:%04x sco:%04x",
									i,
									ss.allocated ? 'A' : '-',
									(int)ss.dst_line_offset,
									(int)ss.dst_word_offset,
									(int)ss.lines_remaining,
									(int)ss.emdata_offset,
									(int)ss.mprog_offset,
									(int)ss.srccol_offset
								);

								printf(" EM1:");
								print_dataword(bs.EM1);
								printf(" EM2:");
								print_dataword(bs.EM2);
								printf(" EM3:");
								print_dataword(bs.EM3);

								printf(" SYI:%03d DYI:%03d XC:%02d SK:%02x",
									(int)(char)bs.SYI,
									(int)(char)bs.DYI,
									(int)bs.XC,
									(int)bs.SKEW
								);

								printf("\n");

								std::cout << std::flush;
							}

							// verify
							if ((i > 0) &&
								(ss.mprog_offset > 0) &&
								ss.allocated &&
								(mprog[ss.mprog_offset - 1] != 0x813a)
								)
							{
								printf("error: blit index verify failed: D:$%04x @ O:$%04x\n",
									(int)mprog[ss.mprog_offset - 1],
									ss.mprog_offset
								);
								exit(1);
							}

							i++;
						}
					}

				}
				else // (USE_EMH_CLIPTABLE && !g_allargs.noclipy)
				{
					// optimized lineorder, no clipping tables - all states configured as first scan to be drawn
					
					for (int orig_y = shift.scan_states.size() - 1; orig_y >= 0; orig_y--)
					{
						shift.scan_states[orig_y] = shift.first_scan_state;
					}
					
				} // (USE_EMH_CLIPTABLE && !g_allargs.noclipy)
			}
		}
	}

	//generate_emx2_map_span_sources(spr, stacked_spans, );
}

bool generate_emh_sequence()
{
	std::cout << "code-generating EMS frames: " << std::endl;

	// individual sprites follow
	spritelist_t::iterator it = g_spriteframes.begin();

	int f = 0;
	for (; it != g_spriteframes.end(); it++, f++)
	{
		if ((DEBUG_FRAME >= 0) && (f != DEBUG_FRAME))
			continue;

		// find min source colour spans
		generate_emx_emh_active_linewidths(*it, it->ems_);

		generate_emh_frame_flexible(*it, f);
	}

	return true;
}
