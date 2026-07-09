//---------------------------------------------------------------------------------------------------------------------
//	[A]tari [G]ame [T]ools / dml 2022
//---------------------------------------------------------------------------------------------------------------------
//	generator specifically for EMXv2 sprites
//---------------------------------------------------------------------------------------------------------------------

// sort scan pattern into blocks according to:
// [preshift0 mask width] [span width] [span offset] [span masks]

void generate_emx_preshift_analysis
(
	const spriteframe_t &spr,
	preshift_t &_shift, int _psi,
	bool _single_span_scans
)
{
	bool allow_NFSR = !g_allargs.no_nfsr;
	//int full_src_wordwidth = ((spr.w_ + 15) & -16) >> 4;
	//int src_pixwidth = full_src_wordwidth << 4;

	// extract patterns of spans according to individual span constraints

	auto &sa = _shift.sa;
	sa.span_vpattern_.resize(spr.h_);

	for (int v = 0; v < spr.h_; v++)
	{
		auto &span_hseq = sa.span_vpattern_[v];

		int accumulated_bcycles = 0;
		int accumulated_words = 0;
		int nonzero_words = 0;
		int span_pos = -1;
		u16 last_word = 0;

		u16 em[3] = { 0, 0, 0 };

		int w = 0;
		while (w < _shift.wordwidth_)
		{
			// fetch next word
			u16 *pline = &_shift.data_[(v * _shift.wordwidth_)];
			u16 word = pline[w];

			int word_pos = w++;

			// detect start of new span
			if ((span_pos < 0) && (word != 0))
				span_pos = word_pos;

			// check reference to shifted previous data
			bool FISR =
				(span_pos > 0) &&
				(u16(0xFFFF0000UL >> _shift.shift_) & pline[span_pos]);

			if (word == 0)
			{
				// absorb zero masks, but emit pending span

				if (nonzero_words > 0)
				{
					//					printf("zero-term:\n");
					//					printf("nonzero words:%d  acc words:%d\n", nonzero_words, accumulated_words);

					// check reference to current data (not shifted from previous)
					bool NFSR =
						allow_NFSR &&
						(accumulated_words > 1) &&
						(u16(0x0000FFFFUL >> _shift.shift_) & pline[span_pos + accumulated_words - 1]) == 0;

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
						/*sline_aw=*/spr.active_widths[v],
						/*pem=*/em,
						/*bccount=*/accumulated_bcycles + (FISR ? 1 : 0) - (NFSR ? 1 : 0)
						)
						);
					em[0] = em[1] = em[2] = 0;

					span_pos = -1;
					accumulated_words = 0;
					nonzero_words = 0;
					accumulated_bcycles = 0;

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

					// check reference to current data (not shifted from previous)
					bool NFSR =
						allow_NFSR &&
						(accumulated_words > 1) &&
						(u16(0x0000FFFFUL >> _shift.shift_) & pline[span_pos + accumulated_words - 1]) == 0;

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
						/*sline_aw=*/spr.active_widths[v],
						/*pem=*/em,
						/*bccount=*/accumulated_bcycles + (FISR ? 1 : 0) - (NFSR ? 1 : 0)
						)
						);
					em[0] = em[1] = em[2] = 0;

					span_pos = -1;
					accumulated_words = 0;
					nonzero_words = 0;
					accumulated_bcycles = 0;

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
					(w == _shift.wordwidth_) ||	// end of line
					(nonzero_words == 3)		// exhausted EMs
					)
				{
					//					printf("eol:%d\n", (w == _shift.wordwidth_) ? 1 : 0);
					//					printf("nonzero words:%d  acc words:%d\n", nonzero_words, accumulated_words);

					// check reference to shifted previous data
					//					bool FISR =
					//						(span_pos > 0) &&
					//						(u16(0xFFFF0000UL >> _shift.shift_) & pline[span_pos]);

					// check reference to current data (not shifted from previous)
					bool NFSR =
						allow_NFSR &&
						(accumulated_words > 1) &&
						(u16(0x0000FFFFUL >> _shift.shift_) & pline[span_pos + accumulated_words - 1]) == 0;

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
						/*sline_aw=*/spr.active_widths[v],
						/*pem=*/em,
						/*bccount=*/accumulated_bcycles + (FISR ? 1 : 0) - (NFSR ? 1 : 0)
						)
						);
					em[0] = em[1] = em[2] = 0;

					span_pos = -1;
					accumulated_words = 0;
					nonzero_words = 0;
					accumulated_bcycles = 0;

					last_word = 0;
				}
			}
		}

		if (_single_span_scans)
		{
			// strip out all but first span per scanline
			for (int v = 0; v < spr.h_; v++)
			{
				auto &span_hseq = sa.span_vpattern_[v];
				if (!span_hseq.empty())
					span_hseq.resize(1);
			}
		}
	}
}

//---------------------------------------------------------------------------------------------------------------------

void generate_emx_order_spans
(
	spriteframe_t &spr, int _frame, 
	preshift_t &shift, int pi, 
	std::vector<constrained_wordspan*> &_stacked_spans
)
{
	// stack all spans for state change optimization	

	std::map<int, int> xmap;
	std::map<uint32_t, std::vector<constrained_wordspan*>> ordered_spans;

	int max_group = -1;
	for (auto & spanlist : shift.sa.span_vpattern_)
	{
		int hgroup = 0;
		for (auto &span : spanlist)
		{
			span.line_opt = spr.emx_.linemap_src_2_opt[span.line_orig];

			// span index has priority so spans rendered vertically for edge coherence, never horizontally
			// otherwise, frame common lineorder prevails

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

			//int group = hgroup;

			// reordered scan lines, within left->right ordered components,
			// sub-grouped by width,offset
			uint32_t ordering_key =
//					(uint32_t(horizontal_index) << (32 - 5)) |	// 343
//					(uint32_t(span.ww) << (32 - 10)) |
//					(uint32_t(span.ws) << (32 - 15)) |
//					(uint32_t(span.line_opt) << 0)
//					(uint32_t(span.ws) << (32 - 5)) |
//					(uint32_t(horizontal_index) << (32 - 10)) |	// 336
//					(uint32_t(span.ww) << (32 - 15)) |
//					(uint32_t(span.line_opt) << 0)
				(uint32_t(key) << (32 - 5)) |
				(uint32_t(32-span.ww) << (32 - 10)) |
				(uint32_t(span.cxs)&1 << (32 - 15)) |
				(uint32_t(span.cxe)&1 << (32 - 16)) |
				(uint32_t(span.line_opt) << 0)
				;

			span.group = group; // use this everywhere downstream

			ordered_spans[ordering_key].push_back(&span);

			hgroup++;
		}
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
		diagnostic_name.append("_spans_emx.png");
		printf("writing span diagnostic: %s @ %dx%d\n", diagnostic_name.c_str(), spr.w_, spr.h_);
		output_span_diagnostic(spr.w_, spr.h_, _stacked_spans, diagnostic_name);
	}
}

//---------------------------------------------------------------------------------------------------------------------

void generate_emx2_frame_optimized(spriteframe_t &spr, int _frame)
{
	if (g_allargs.verbose)
		std::cout << "frame [" << _frame << "]" << std::endl << std::flush;

	// analyse all preshifts within this frame
	{
		std::vector<preshiftlist_t>::iterator pmit = spr.emx_.psmask_frames_.begin();

		int psi = 0;
		for (; pmit != spr.emx_.psmask_frames_.end(); pmit++, psi++)
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

	// find common line order
	generate_emx_emh_common_lineorder
	(
		spr, 
		spr.emx_,
		spr.emx_.linemap_opt_2_src, spr.emx_.linemap_src_2_opt, 
		/*optimize=*/true,
		/*shared_shift=*/nullptr
	);

	int pi = 0;
	for (std::vector<preshiftlist_t>::iterator pmit = spr.emx_.psmask_frames_.begin(); pmit != spr.emx_.psmask_frames_.end(); pmit++, pi++)
	{
		//for (preshiftlist_t::iterator pit = pmit->begin(); pit != pmit->end(); pit++)
		preshiftlist_t::iterator pit = pmit->begin();
		{
			auto & spshift = *pit;
			preshift_t &shift = *spshift;

			if ((DEBUG_PRESHIFT >= 0) && (pi != DEBUG_PRESHIFT))
				continue;

			std::vector<constrained_wordspan*> stacked_spans;
			generate_emx_order_spans(spr, _frame, shift, pi, stacked_spans);

			std::vector<u16> shared_mask_words; // unused
			generate_emx_emh_metaprogram(spr, _frame, shift, pi, stacked_spans, /*shared_mask=*/false, shared_mask_words);

		} // components

	} // preshifts

	//
}


/*
void generate_emx2_filter_component_spans(spriteframe_t &_spr, preshift_t &_shared_shift, int _num_components_required)
{
	bool processing = true;
	int pass_index = 0;

	while (processing)
	{
		processing = false;

		auto spcmp = std::make_shared<preshift_t>(_shift);
		auto &sa = spcmp->sa;

		// stack all spans for state change optimization
		std::map<uint32_t, std::vector<constrained_wordspan*>> ordered_spans;
		for (auto & spanlist : sa.span_vpattern_)
		{
			int horizontal_index = 0;
			for (auto &span : spanlist)
			{
				if (horizontal_index == pass_index)
				{
					span.line_opt = _spr.ems_.linemap_src_2_opt[span.line_orig];

					// span index has priority so spans rendered vertically for edge coherence, never horizontally
					// otherwise, frame common lineorder prevails
					uint32_t ordering_key;

					// left->right spans within top-bottom ordered scanlines
					ordering_key =
						(uint32_t(span.line_opt) << 5) |
						(uint32_t(horizontal_index))
						;

					ordered_spans[ordering_key].push_back(&span);
					processing = true;
					break;
				}

				horizontal_index++;
			}
		}

		if (processing)
		{
			int o = 0;
			for (auto group_it = ordered_spans.begin(); group_it != ordered_spans.end(); group_it++)
			{
				for (auto pspan : group_it->second)
				{
					pspan->draw_order = o++;
					spcmp->stacked_spans.push_back(pspan);
				}
			}

			_component_list.push_back(spcmp);
		}

		pass_index++;
	}
}
*/


bool generate_emx2_sequence()
{
	std::cout << "code-generating EMXv2 frames: " << std::endl;

	// individual sprites follow
	spritelist_t::iterator it = g_spriteframes.begin();

	int f = 0;
	for (; it != g_spriteframes.end(); it++, f++)
	{
		if ((DEBUG_FRAME >= 0) && (f != DEBUG_FRAME))
			continue;

		// find min source colour spans
		generate_emx_emh_active_linewidths(*it, it->emx_);

		generate_emx2_frame_optimized(*it, f);
	}

	return true;
}
