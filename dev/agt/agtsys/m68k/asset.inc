*=======================================================*
*	[A]tari [G]ame [T]ools / dml 2016
*=======================================================*
*	Asset structures
*-------------------------------------------------------*

*-------------------------------------------------------*
	.text
*-------------------------------------------------------*

*-------------------------------------------------------*
*	pair
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
lpair_first:		ds.l	1
lpair_second:		ds.l	1

*-------------------------------------------------------*
*	clipping window
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
clip_x1:		ds.w	1
clip_y1:		ds.w	1
clip_x2:		ds.w	1
clip_y2:		ds.w	1
clip_x1p:		ds.w	1
clip_x2p:		ds.w	1

*-------------------------------------------------------*
*	spritesheet .sps main header
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
sps_:
sps_crc:		ds.l	1
sps_version:		ds.w	1
sps_framecount:		ds.w	1
sps_planes:		ds.w	1
sps_flags:		ds.w	1
sps_preshift_range:	ds.b	1
sps_preshift_step:	ds.b	1
sps_srcwidth:		ds.w	1
sps_srcheight:		ds.w	1
sps_frameindex:		ds.l	0
*-------------------------------------------------------*
sps_size:
*-------------------------------------------------------*

*-------------------------------------------------------*
*	spritesheet .sps sprite frame header
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
spf_:
spf_w:			ds.w	1
spf_h:			ds.w	1
spf_xo:			ds.w	1
spf_yo:			ds.w	1
spf_omap:		ds.w	1
spf_data:		ds.l	0

*-------------------------------------------------------*
			.abs	spf_data
*-------------------------------------------------------*
*	sprite frame header: EMS/EMH
*-------------------------------------------------------*
spf_em_:
spf_em_colour:		ds.l	2
spf_em_psmask:		ds.l	16			
;...occlusion maps [index]
;...occlusion maps [data]
;...emh shared codegen
;...colour data

*-------------------------------------------------------*
			.abs	spf_data
*-------------------------------------------------------*
*	sprite frame header: EMX
*-------------------------------------------------------*
spf_emx_:
spf_emx_colour:		ds.l	2		; col0/1 (line-ordered)
spf_emx_colouropt:	ds.l	2		; col0/1 (optimized, line-reordered)
spf_emx_psmask:		ds.l	16		; preshifts[16] (shared code, unique EM data)
spf_emx_psmaskopt:	ds.l	16		; preshifts[16] (optimized, unique code)
;...occlusion maps [index]
;...occlusion maps [data]
;...colour data

*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
*	sprite frame preshifted mask header
*-------------------------------------------------------*
spf_psm_:
spf_psm_wordwidth:	ds.w	1
spf_psm_bskew:		ds.w	1
spf_psm_bsyi:		ds.w	1
spf_psm_bdyi:		ds.w	1
spf_psm_wordoffset:	ds.w	1
spf_psm_nextcmp:	ds.l	1
spf_psm_data:


*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
spct_:
spct_emdata_off:	ds.w	1
spct_srccol_off:	ds.w	1
spct_mprog_off:		ds.w	1
;spct_lrem:		ds.w	1
spct_dline_off:		ds.b	1
spct_dword_off:		ds.b	1
spct_b_syi:		ds.b	1
spct_b_dyi:		ds.b	1
spct_b_skew:		ds.b	1
spct_b_xc:		ds.b	1
spct_b_em1:		ds.w	1
spct_b_em2:		ds.w	1
spct_b_em3:		ds.w	1
spct_size:

*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
*	sprite frame preshifted mask header: EMH
*-------------------------------------------------------*
spf_emh_psm_:
spf_emh_psm_wordwidth:	ds.w	1
spf_emh_psm_wordoffset:	ds.w	1
spf_emh_psm_dstyskip:	ds.w	1
spf_emh_psm_dstwords:	ds.w	1
;
spf_emh_psm_bskew:	ds.w	1
spf_emh_psm_bsyi:	ds.w	1
spf_emh_psm_bdyi:	ds.w	1
spf_emh_psm_bxc:	ds.w	1
;
spf_emh_psm_emdata:	ds.w	1			; offset to (unique) EM data
spf_emh_psm_code:	ds.l	1			; offset to (shared) codegen
;
spf_emh_psm_nextcmp:	ds.l	1			; next component
spf_emh_psm_clip:					; clipping table follows
; shared codegen emitted standalone, like colour data
; EM data emitted after clipping table

*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
*	sprite frame preshifted mask header: EMX
*-------------------------------------------------------*
spf_emx_psm_:
spf_emx_psm_wordwidth:	ds.w	1
spf_emx_psm_wordoffset:	ds.w	1
spf_emx_psm_dstyskip:	ds.w	1
spf_emx_psm_dstwords:	ds.w	1
;
spf_emx_psm_nextcmp:	ds.l	1
spf_emx_psm_codegen:
;...emx codegen


*-------------------------------------------------------*
*	slabsheet .sls main header
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
sls_crc:		ds.l	1
sls_version:		ds.w	1
sls_framecount:		ds.w	1
sls_planes:		ds.w	1
sls_flags:		ds.w	1
sls_preshift_range:	ds.b	1
sls_preshift_step:	ds.b	1
sls_srcwidth:		ds.w	1
sls_srcheight:		ds.w	1
sls_frameindex:
*-------------------------------------------------------*
sls_size:
*-------------------------------------------------------*

*-------------------------------------------------------*
*	slabsheet .sls frame header
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
slf_w:			ds.w	1
slf_h:			ds.w	1
slf_xo:			ds.w	1
slf_yo:			ds.w	1
slf_spanblocks:		ds.w	1
slf_nextcmp:		ds.l	1
slf_indexbase:
*-------------------------------------------------------*
slf_size:
*-------------------------------------------------------*

*-------------------------------------------------------*
*	slabsheet .sls spanblock header
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
ssb_dy:			ds.w	1
ssb_xo:			ds.w	1
ssb_xs:			ds.w	1
ssb_dxw:		ds.w	1
ssb_y:			ds.w	1
ssb_ys:			ds.w	1
ssb_data:		ds.l	1
*-------------------------------------------------------*
ssb_size:
*-------------------------------------------------------*

*-------------------------------------------------------*
*	slabsheet .sls clipper header
*-------------------------------------------------------*
			.abs
*-------------------------------------------------------*
slc_tyc_offset:		ds.w	1
slc_tyc_remain:		ds.w	1
slc_tyc_data:		ds.w	1
slc_byc_remain:		ds.w	1
*-------------------------------------------------------*
slc_size:
*-------------------------------------------------------*

*-------------------------------------------------------*
	.text
*-------------------------------------------------------*
