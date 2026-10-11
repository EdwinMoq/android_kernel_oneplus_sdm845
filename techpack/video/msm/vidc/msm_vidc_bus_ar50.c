// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, The Linux Foundation. All rights reserved.
 */

#include "msm_vidc_bus.h"
#include "msm_vidc_internal.h"

static unsigned long __calculate_vpe(struct vidc_bus_vote_data *d)
{
	return 0;
}

static unsigned long __calculate_decoder(struct vidc_bus_vote_data *d)
{
	/*
	 * XXX: Don't fool around with any of the hardcoded numbers unless you
	 * know /exactly/ what you're doing.  Many of these numbers are
	 * measured heuristics and hardcoded numbers taken from the firmware.
	 */
	/* Decoder parameters */
	int width, height, lcu_size, dpb_bpp, opb_bpp, fps, opb_factor;
	bool unified_dpb_opb, dpb_compression_enabled, opb_compression_enabled,
		llc_ref_read_l2_cache_enabled = false,
		llc_vpss_ds_line_buf_enabled = false;
	fp_t dpb_opb_scaling_ratio, dpb_read_compression_factor,
		dpb_write_compression_factor, opb_compression_factor,
		qsmmu_bw_overhead_factor, height_ratio;

	/* Derived parameters */
	int lcu_per_frame, tnbr_per_lcu, colocated_bytes_per_lcu;
	unsigned long bitrate;

	fp_t bins_to_bit_factor, dpb_write_factor, ten_bpc_packing_factor,
		ten_bpc_bpp_factor, vsp_read_factor, vsp_write_factor,
		bw_for_1x_8bpc, dpb_bw_for_1x,
		motion_vector_complexity = 0, row_cache_penalty = 0, opb_bw = 0,
		dpb_total = 0;

	/* Output parameters */
	struct {
		fp_t vsp_read, vsp_write, collocated_read, collocated_write,
			line_buffer_read, line_buffer_write, recon_read,
			recon_write, opb_read, opb_write, dpb_read, dpb_write,
			total;
	} ddr = {0};

	struct {
		fp_t dpb_read, opb_read, total;
	} llc = {0};

	unsigned long ret = 0;
	unsigned int integer_part, frac_part;

	width = max(d->input_width, BASELINE_DIMENSIONS.width);
	height = max(d->input_height, BASELINE_DIMENSIONS.height);

	lcu_size = d->lcu_size;

	dpb_bpp = d->num_formats >= 1 ? __bpp(d->color_formats[0], d->sid) : INT_MAX;
	opb_bpp = d->num_formats >= 2 ?  __bpp(d->color_formats[1], d->sid) : dpb_bpp;

	fps = d->fps;

	unified_dpb_opb = d->num_formats == 1;

	dpb_opb_scaling_ratio = fp_div(FP_INT(
		(int)(d->input_width * d->input_height)),
		FP_INT((int)(d->output_width * d->output_height)));
	height_ratio = fp_div(d->input_height, d->output_height);

	dpb_compression_enabled = d->num_formats >= 1 &&
		__ubwc(d->color_formats[0]);
	opb_compression_enabled = d->num_formats >= 2 &&
		__ubwc(d->color_formats[1]);

	/*
	 * Convert Q16 number into Integer and Fractional part upto 2 places.
	 * Ex : 105752 / 65536 = 1.61; 1.61 in Q16 = 105752;
	 * Integer part =  105752 / 65536 = 1;
	 * Reminder = 105752 - 1 * 65536 = 40216;
	 * Fractional part = 40216 * 100 / 65536 = 61;
	 * Now converto to FP(1, 61, 100) for below code.
	 */

	integer_part = d->compression_ratio >> 16;
	frac_part =
		((d->compression_ratio - (integer_part << 16)) * 100) >> 16;

	dpb_read_compression_factor = FP(integer_part, frac_part, 100);

	integer_part = d->complexity_factor >> 16;
	frac_part =
		((d->complexity_factor - (integer_part << 16)) * 100) >> 16;

	motion_vector_complexity = FP(integer_part, frac_part, 100);

	dpb_write_compression_factor = dpb_read_compression_factor;

	opb_compression_factor = !opb_compression_enabled ? FP_ONE :
				dpb_write_compression_factor;

	llc_ref_read_l2_cache_enabled = llc_vpss_ds_line_buf_enabled = false;
	if (d->use_sys_cache) {
		llc_ref_read_l2_cache_enabled = true;
		llc_vpss_ds_line_buf_enabled = true;
	}

	/* Derived parameters setup */
	lcu_per_frame = DIV_ROUND_UP(width, lcu_size) *
		DIV_ROUND_UP(height, lcu_size);

	bitrate = (d->bitrate + 1000000 - 1) / 1000000;

	bins_to_bit_factor = d->work_mode == HFI_WORKMODE_1 ?
		FP_INT(0) : FP_INT(4);

	vsp_read_factor = bins_to_bit_factor + FP_INT(2);

	dpb_write_factor = FP(1, 5, 100);

	ten_bpc_packing_factor = FP(1, 67, 1000);
	ten_bpc_bpp_factor = FP(1, 1, 4);

	vsp_write_factor = bins_to_bit_factor;

	tnbr_per_lcu = lcu_size == 16 ? 128 :
		lcu_size == 32 ? 64 : 128;

	colocated_bytes_per_lcu = lcu_size == 16 ? 16 :
				lcu_size == 32 ? 64 : 256;

	/* ........................................ for DDR */
	ddr.vsp_read = fp_div(fp_mult(FP_INT(bitrate),
				vsp_read_factor), FP_INT(8));
	ddr.vsp_write = fp_div(fp_mult(FP_INT(bitrate),
				vsp_write_factor), FP_INT(8));

	ddr.collocated_read = FP_INT(lcu_per_frame *
			colocated_bytes_per_lcu * fps / bps(1));
	ddr.collocated_write = FP_INT(lcu_per_frame *
			colocated_bytes_per_lcu * fps / bps(1));

	ddr.line_buffer_read = FP_INT(tnbr_per_lcu *
			lcu_per_frame * fps / bps(1));
	ddr.line_buffer_write = ddr.line_buffer_read;

	bw_for_1x_8bpc = fp_div(FP_INT((int)(width * height)), FP_INT(32 * 8));

	bw_for_1x_8bpc = fp_mult(bw_for_1x_8bpc,
		fp_div(FP_INT(((int)(256 * fps))), FP_INT(1000 * 1000)));

	dpb_bw_for_1x = dpb_bpp == 8 ? bw_for_1x_8bpc :
		fp_mult(bw_for_1x_8bpc, fp_mult(ten_bpc_packing_factor,
				ten_bpc_bpp_factor));

	ddr.dpb_read = fp_div(fp_mult(fp_mult(dpb_bw_for_1x,
			motion_vector_complexity), dpb_write_factor),
			dpb_read_compression_factor);

	ddr.dpb_write = fp_div(fp_mult(dpb_bw_for_1x, dpb_write_factor),
		dpb_write_compression_factor);
	dpb_total = ddr.dpb_read + ddr.dpb_write;
	if (llc_ref_read_l2_cache_enabled) {
		row_cache_penalty = FP(1, 30, 100);
		ddr.dpb_read = fp_div(ddr.dpb_read, row_cache_penalty);
		llc.dpb_read = dpb_total - ddr.dpb_write - ddr.dpb_read;
	}

	opb_factor = dpb_bpp == 8 ? 8 : 4;

	ddr.opb_read = unified_dpb_opb ? 0 : opb_compression_enabled ?
		fp_div(fp_mult(fp_div(dpb_bw_for_1x, dpb_opb_scaling_ratio),
		FP_INT(opb_factor)), height_ratio) : 0;
	ddr.opb_write = unified_dpb_opb ? 0 : opb_compression_enabled ?
		ddr.dpb_read : fp_div(fp_div(fp_mult(dpb_bw_for_1x,
		FP(1, 50, 100)), dpb_opb_scaling_ratio),
			opb_compression_factor);

	if (llc_vpss_ds_line_buf_enabled) {
		llc.opb_read = ddr.opb_read;
		ddr.opb_write -= ddr.opb_read;
		ddr.opb_read = 0;
	}
	ddr.total = ddr.vsp_read + ddr.vsp_write +
		ddr.collocated_read + ddr.collocated_write +
		ddr.line_buffer_read + ddr.line_buffer_write +
		ddr.opb_read + ddr.opb_write +
		ddr.dpb_read + ddr.dpb_write;

	qsmmu_bw_overhead_factor = FP(1, 3, 100);

	ddr.total = fp_mult(ddr.total, qsmmu_bw_overhead_factor);
	llc.total = llc.dpb_read + llc.opb_read + ddr.total;

	/* Dump all the variables for easier debugging */
	if (msm_vidc_debug & VIDC_BUS) {
		struct dump dump[] = {
		{"DECODER PARAMETERS", "", DUMP_HEADER_MAGIC},
		{"LCU size", "%d", lcu_size},
		{"DPB bitdepth", "%d", dpb_bpp},
		{"frame rate", "%d", fps},
		{"DPB/OPB unified", "%d", unified_dpb_opb},
		{"DPB/OPB downscaling ratio", DUMP_FP_FMT,
			dpb_opb_scaling_ratio},
		{"DPB compression", "%d", dpb_compression_enabled},
		{"OPB compression", "%d", opb_compression_enabled},
		{"DPB Read compression factor", DUMP_FP_FMT,
			dpb_read_compression_factor},
		{"DPB Write compression factor", DUMP_FP_FMT,
			dpb_write_compression_factor},
		{"OPB compression factor", DUMP_FP_FMT,
			opb_compression_factor},
		{"frame width", "%d", width},
		{"frame height", "%d", height},

		{"DERIVED PARAMETERS (1)", "", DUMP_HEADER_MAGIC},
		{"LCUs/frame", "%d", lcu_per_frame},
		{"bitrate (Mbit/sec)", "%d", bitrate},
		{"bins to bit factor", DUMP_FP_FMT, bins_to_bit_factor},
		{"DPB write factor", DUMP_FP_FMT, dpb_write_factor},
		{"10bpc packing factor", DUMP_FP_FMT,
			ten_bpc_packing_factor},
		{"10bpc,BPP factor", DUMP_FP_FMT, ten_bpc_bpp_factor},
		{"VSP read factor", DUMP_FP_FMT, vsp_read_factor},
		{"VSP write factor", DUMP_FP_FMT, vsp_write_factor},
		{"TNBR/LCU", "%d", tnbr_per_lcu},
		{"colocated bytes/LCU", "%d", colocated_bytes_per_lcu},
		{"B/W for 1x (NV12 8bpc)", DUMP_FP_FMT, bw_for_1x_8bpc},
		{"DPB B/W For 1x (NV12)", DUMP_FP_FMT, dpb_bw_for_1x},

		{"DERIVED PARAMETERS (2)", "", DUMP_HEADER_MAGIC},
		{"MV complexity", DUMP_FP_FMT, motion_vector_complexity},
		{"row cache penalty", DUMP_FP_FMT, row_cache_penalty},
		{"qsmmu_bw_overhead_factor", DUMP_FP_FMT,
			qsmmu_bw_overhead_factor},
		{"OPB B/W (single instance)", DUMP_FP_FMT, opb_bw},

		{"INTERMEDIATE DDR B/W", "", DUMP_HEADER_MAGIC},
		{"VSP read", DUMP_FP_FMT, ddr.vsp_read},
		{"VSP write", DUMP_FP_FMT, ddr.vsp_write},
		{"collocated read", DUMP_FP_FMT, ddr.collocated_read},
		{"collocated write", DUMP_FP_FMT, ddr.collocated_write},
		{"line buffer read", DUMP_FP_FMT, ddr.line_buffer_read},
		{"line buffer write", DUMP_FP_FMT, ddr.line_buffer_write},
		{"recon read", DUMP_FP_FMT, ddr.recon_read},
		{"recon write", DUMP_FP_FMT, ddr.recon_write},
		{"OPB read", DUMP_FP_FMT, ddr.opb_read},
		{"OPB write", DUMP_FP_FMT, ddr.opb_write},
		{"DPB read", DUMP_FP_FMT, ddr.dpb_read},
		{"DPB write", DUMP_FP_FMT, ddr.dpb_write},
		{"LLC DPB read", DUMP_FP_FMT, llc.dpb_read},
		{"LLC OPB read", DUMP_FP_FMT, llc.opb_read},

		};
		__dump(dump, ARRAY_SIZE(dump), d->sid);
	}

	d->calc_bw_ddr = kbps(fp_round(ddr.total));
	d->calc_bw_llcc = kbps(fp_round(llc.total));

	return ret;
}

static unsigned long __calculate_encoder(struct vidc_bus_vote_data *d)
{
	/*
	 * XXX: Don't fool around with any of the hardcoded numbers unless you
	 * know /exactly/ what you're doing.  Many of these numbers are
	 * measured heuristics and hardcoded numbers taken from the firmware.
	 */
	/* Encoder Parameters */

	int width, height, fps, dpb_bpp, lcu_per_frame, lcu_size,
		vertical_tile_width, colocated_bytes_per_lcu, bitrate,
		ref_overlap_bw_factor;
	enum hal_uncompressed_format dpb_color_format, original_color_format;
	bool dpb_compression_enabled, original_compression_enabled,
		work_mode_1, low_power, rotation, cropping_or_scaling,
		b_frames_enabled = false,
		llc_dual_core_ref_read_buf_enabled = false,
		llc_top_line_buf_enabled = false,
		llc_ref_chroma_cache_enabled = false;
	fp_t dpb_compression_factor, original_compression_factor,
		input_compression_factor, qsmmu_bw_overhead_factor,
		ref_y_bw_factor, ref_cb_cr_bw_factor, ten_bpc_bpp_factor,
		bw_for_1x_8bpc, dpb_bw_for_1x, ref_cb_cr_read,
		bins_to_bit_factor, ref_y_read,	ten_bpc_packing_factor,
		dpb_write_factor, ref_overlap_bw, llc_ref_y_read,
		llc_ref_cb_cr_read;
	fp_t integer_part, frac_part;
	unsigned long ret = 0;

	/* Output parameters */
	struct {
		fp_t vsp_read, vsp_write, collocated_read, collocated_write,
			line_buffer_read, line_buffer_write, original_read,
			original_write, dpb_read, dpb_write, total;
	} ddr = {0};

	struct {
		fp_t dpb_read, line_buffer, total;
	} llc = {0};

	/* Encoder Parameters setup */
	ten_bpc_packing_factor = FP(1, 67, 1000);
	ten_bpc_bpp_factor = FP(1, 1, 4);
	rotation = false;
	cropping_or_scaling = false;
	vertical_tile_width = 960;
	ref_y_bw_factor = FP(1, 30, 100);
	ref_cb_cr_bw_factor = FP(1, 50, 100);
	dpb_write_factor = FP(1, 8, 100);


	/* Derived Parameters */
	lcu_size = d->lcu_size;
	fps = d->fps;
	b_frames_enabled = d->b_frames_enabled;
	width = max(d->input_width, BASELINE_DIMENSIONS.width);
	height = max(d->input_height, BASELINE_DIMENSIONS.height);
	bitrate = d->bitrate > 0 ? d->bitrate / 1000000 :
		__lut(width, height, fps)->bitrate;
	lcu_per_frame = DIV_ROUND_UP(width, lcu_size) *
		DIV_ROUND_UP(height, lcu_size);

	dpb_color_format = HAL_COLOR_FORMAT_NV12_UBWC;
	original_color_format = d->num_formats >= 1 ?
		d->color_formats[0] : HAL_UNUSED_COLOR;

	dpb_bpp = d->num_formats >= 1 ? __bpp(d->color_formats[0], d->sid) : INT_MAX;

	dpb_compression_enabled = __ubwc(dpb_color_format);
	original_compression_enabled = __ubwc(original_color_format);

	work_mode_1 = d->work_mode == HFI_WORKMODE_1;
	low_power = d->power_mode == VIDC_POWER_LOW;
	bins_to_bit_factor = work_mode_1 ?
		FP_INT(0) : FP_INT(4);

	if (d->use_sys_cache) {
		llc_dual_core_ref_read_buf_enabled = true;
		llc_ref_chroma_cache_enabled = true;
	}

	/*
	 * Convert Q16 number into Integer and Fractional part upto 2 places.
	 * Ex : 105752 / 65536 = 1.61; 1.61 in Q16 = 105752;
	 * Integer part =  105752 / 65536 = 1;
	 * Reminder = 105752 - 1 * 65536 = 40216;
	 * Fractional part = 40216 * 100 / 65536 = 61;
	 * Now converto to FP(1, 61, 100) for below code.
	 */

	integer_part = d->compression_ratio >> 16;
	frac_part =
		((d->compression_ratio - (integer_part * 65536)) * 100) >> 16;

	dpb_compression_factor = FP(integer_part, frac_part, 100);

	integer_part = d->input_cr >> 16;
	frac_part =
		((d->input_cr - (integer_part * 65536)) * 100) >> 16;

	input_compression_factor = FP(integer_part, frac_part, 100);

	/* use input cr if it is valid (not 1), otherwise use lut */
	original_compression_factor =
		!original_compression_enabled ? FP_ONE :
		input_compression_factor != FP_ONE ? input_compression_factor :
		__compression_ratio(__lut(width, height, fps), dpb_bpp);

	ddr.vsp_read = fp_mult(fp_div(FP_INT(bitrate), FP_INT(8)),
			bins_to_bit_factor);
	ddr.vsp_write = ddr.vsp_read + fp_div(FP_INT(bitrate), FP_INT(8));

	colocated_bytes_per_lcu = lcu_size == 16 ? 16 :
				lcu_size == 32 ? 64 : 256;

	ddr.collocated_read = FP_INT(lcu_per_frame *
			colocated_bytes_per_lcu * fps / bps(1));

	ddr.collocated_write = ddr.collocated_read;

	ddr.line_buffer_read = FP_INT(16 * lcu_per_frame * fps / bps(1));

	ddr.line_buffer_write = ddr.line_buffer_read;

	llc.line_buffer = ddr.line_buffer_read + ddr.line_buffer_write;
	if (llc_top_line_buf_enabled)
		ddr.line_buffer_read = ddr.line_buffer_write = FP_INT(0);

	llc.line_buffer -= (ddr.line_buffer_read + ddr.line_buffer_write);

	bw_for_1x_8bpc = fp_div(FP_INT((int)(width * height)), FP_INT(32 * 8));

	bw_for_1x_8bpc = fp_mult(bw_for_1x_8bpc,
		fp_div(FP_INT(((int)(256 * fps))), FP_INT(1000 * 1000)));

	dpb_bw_for_1x = dpb_bpp == 8 ? bw_for_1x_8bpc :
		fp_mult(bw_for_1x_8bpc, fp_mult(ten_bpc_packing_factor,
			ten_bpc_bpp_factor));

	ddr.original_read = fp_div(fp_mult(FP(1, 50, 100), dpb_bw_for_1x),
		input_compression_factor);

	ddr.original_write = FP_ZERO;

	ref_y_bw_factor =
		width == vertical_tile_width ? FP_INT(1) : ref_y_bw_factor;

	ref_y_read = fp_mult(ref_y_bw_factor, dpb_bw_for_1x);

	ref_y_read = fp_div(ref_y_read, dpb_compression_factor);

	ref_y_read =
		b_frames_enabled ? fp_mult(ref_y_read, FP_INT(2)) : ref_y_read;

	llc_ref_y_read = ref_y_read;
	if (llc_dual_core_ref_read_buf_enabled)
		ref_y_read = fp_div(ref_y_read, FP_INT(2));

	llc_ref_y_read -= ref_y_read;

	ref_cb_cr_read = fp_mult(ref_cb_cr_bw_factor, dpb_bw_for_1x) / 2;

	ref_cb_cr_read = fp_div(ref_cb_cr_read, dpb_compression_factor);

	ref_cb_cr_read =
		b_frames_enabled ? fp_mult(ref_cb_cr_read, FP_INT(2)) :
					ref_cb_cr_read;

	llc_ref_cb_cr_read = ref_cb_cr_read;

	if (llc_ref_chroma_cache_enabled)
		ref_cb_cr_read = fp_div(ref_cb_cr_read, ref_cb_cr_bw_factor);

	if (llc_dual_core_ref_read_buf_enabled)
		ref_cb_cr_read = fp_div(ref_cb_cr_read, FP_INT(2));

	llc_ref_cb_cr_read -= ref_cb_cr_read;

	ddr.dpb_write = fp_mult(dpb_write_factor, dpb_bw_for_1x);

	ddr.dpb_write = fp_mult(ddr.dpb_write, FP(1, 50, 100));

	ddr.dpb_write = fp_div(ddr.dpb_write, dpb_compression_factor);

	ref_overlap_bw_factor =
		width <= vertical_tile_width ? FP_INT(0) : FP_INT(1);

	ref_overlap_bw = fp_mult(ddr.dpb_write, ref_overlap_bw_factor);

	ref_overlap_bw = fp_div(ref_overlap_bw, dpb_write_factor);

	ref_overlap_bw = fp_mult(ref_overlap_bw,
		(dpb_write_factor - FP_INT(1)));

	ddr.dpb_read = ref_y_read + ref_cb_cr_read + ref_overlap_bw;

	llc.dpb_read = llc_ref_y_read + llc_ref_cb_cr_read;

	ddr.total = ddr.vsp_read + ddr.vsp_write +
		ddr.collocated_read + ddr.collocated_write +
		ddr.line_buffer_read + ddr.line_buffer_write +
		ddr.original_read + ddr.original_write +
		ddr.dpb_read + ddr.dpb_write;

	llc.total = llc.dpb_read + llc.line_buffer + ddr.total;

	qsmmu_bw_overhead_factor = FP(1, 3, 100);
	ddr.total = fp_mult(ddr.total, qsmmu_bw_overhead_factor);

	if (msm_vidc_debug & VIDC_BUS) {
		struct dump dump[] = {
		{"ENCODER PARAMETERS", "", DUMP_HEADER_MAGIC},
		{"width", "%d", width},
		{"height", "%d", height},
		{"DPB format", "%#x", dpb_color_format},
		{"original frame format", "%#x", original_color_format},
		{"fps", "%d", fps},
		{"DPB compression enable", "%d", dpb_compression_enabled},
		{"original compression enable", "%d",
			original_compression_enabled},
		{"low power mode", "%d", low_power},
		{"Work Mode", "%d", work_mode_1},
		{"DPB compression factor", DUMP_FP_FMT,
			dpb_compression_factor},
		{"original compression factor", DUMP_FP_FMT,
			original_compression_factor},
		{"rotation", "%d", rotation},
		{"cropping or scaling", "%d", cropping_or_scaling},

		{"DERIVED PARAMETERS", "", DUMP_HEADER_MAGIC},
		{"LCU size", "%d", lcu_size},
		{"bitrate (Mbit/sec)", "%lu", bitrate},
		{"bins to bit factor", DUMP_FP_FMT, bins_to_bit_factor},
		{"qsmmu_bw_overhead_factor",
			 DUMP_FP_FMT, qsmmu_bw_overhead_factor},

		{"INTERMEDIATE B/W DDR", "", DUMP_HEADER_MAGIC},
		{"ref_y_read", DUMP_FP_FMT, ref_y_read},
		{"ref_cb_cr_read", DUMP_FP_FMT, ref_cb_cr_read},
		{"ref_overlap_bw", DUMP_FP_FMT, ref_overlap_bw},
		{"VSP read", DUMP_FP_FMT, ddr.vsp_read},
		{"VSP write", DUMP_FP_FMT, ddr.vsp_write},
		{"collocated read", DUMP_FP_FMT, ddr.collocated_read},
		{"collocated write", DUMP_FP_FMT, ddr.collocated_write},
		{"line buffer read", DUMP_FP_FMT, ddr.line_buffer_read},
		{"line buffer write", DUMP_FP_FMT, ddr.line_buffer_write},
		{"original read", DUMP_FP_FMT, ddr.original_read},
		{"original write", DUMP_FP_FMT, ddr.original_write},
		{"DPB read", DUMP_FP_FMT, ddr.dpb_read},
		{"DPB write", DUMP_FP_FMT, ddr.dpb_write},
		{"LLC DPB read", DUMP_FP_FMT, llc.dpb_read},
		{"LLC Line buffer", DUMP_FP_FMT, llc.line_buffer},
		};
		__dump(dump, ARRAY_SIZE(dump), d->sid);
	}

	d->calc_bw_ddr = kbps(fp_round(ddr.total));
	d->calc_bw_llcc = kbps(fp_round(llc.total));

	return ret;
}

static unsigned long __calculate(struct vidc_bus_vote_data *d)
{
	unsigned long value = 0;

	switch (d->domain) {
	case HAL_VIDEO_DOMAIN_VPE:
		value = __calculate_vpe(d);
		break;
	case HAL_VIDEO_DOMAIN_ENCODER:
		value = __calculate_encoder(d);
		break;
	case HAL_VIDEO_DOMAIN_DECODER:
		value = __calculate_decoder(d);
		break;
	default:
		s_vpr_e(d->sid, "Unknown Domain %#x", d->domain);
	}

	return value;
}

int calc_bw_ar50(struct vidc_bus_vote_data *vidc_data)
{
	int ret = 0;

	if (!vidc_data)
		return ret;

	ret = __calculate(vidc_data);

	return ret;
}
