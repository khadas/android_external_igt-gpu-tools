/*
 * Copyright © 2019 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors: Simon Ser <simon.ser@intel.com>
 */

#include "config.h"

#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <xf86drmMode.h>

#include "igt_core.h"
#include "igt_edid.h"

static const char edid_header[] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00
};

static const char monitor_range_padding[] = {
	0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20
};

/* vfreq is in Hz */
static void std_timing_set(struct std_timing *st, int hsize, int vfreq,
			   enum std_timing_aspect aspect)
{
	assert(hsize >= 256 && hsize <= 2288);
	st->hsize = hsize / 8 - 31;
	st->vfreq_aspect = aspect << 6 | (vfreq - 60);
}

static void std_timing_unset(struct std_timing *st)
{
	memset(st, 0x01, sizeof(struct std_timing));
}

/**
 * detailed_timing_set_mode: fill a detailed timing based on a mode
 */
void detailed_timing_set_mode(struct detailed_timing *dt, drmModeModeInfo *mode,
			      int width_mm, int height_mm)
{
	int hactive, hblank, vactive, vblank, hsync_offset, hsync_pulse_width,
	    vsync_offset, vsync_pulse_width;
	struct detailed_pixel_timing *pt = &dt->data.pixel_data;

	hactive = mode->hdisplay;
	hsync_offset = mode->hsync_start - mode->hdisplay;
	hsync_pulse_width = mode->hsync_end - mode->hsync_start;
	hblank = mode->htotal - mode->hdisplay;

	vactive = mode->vdisplay;
	vsync_offset = mode->vsync_start - mode->vdisplay;
	vsync_pulse_width = mode->vsync_end - mode->vsync_start;
	vblank = mode->vtotal - mode->vdisplay;

	dt->pixel_clock[0] = (mode->clock / 10) & 0x00FF;
	dt->pixel_clock[1] = ((mode->clock / 10) & 0xFF00) >> 8;

	assert(hactive <= 0xFFF);
	assert(hblank <= 0xFFF);
	pt->hactive_lo = hactive & 0x0FF;
	pt->hblank_lo = hblank & 0x0FF;
	pt->hactive_hblank_hi = (hactive & 0xF00) >> 4 | (hblank & 0xF00) >> 8;

	assert(vactive <= 0xFFF);
	assert(vblank <= 0xFFF);
	pt->vactive_lo = vactive & 0x0FF;
	pt->vblank_lo = vblank & 0x0FF;
	pt->vactive_vblank_hi = (vactive & 0xF00) >> 4 | (vblank & 0xF00) >> 8;

	assert(hsync_offset <= 0x3FF);
	assert(hsync_pulse_width <= 0x3FF);
	assert(vsync_offset <= 0x3F);
	assert(vsync_pulse_width <= 0x3F);
	pt->hsync_offset_lo = hsync_offset & 0x0FF;
	pt->hsync_pulse_width_lo = hsync_pulse_width & 0x0FF;
	pt->vsync_offset_pulse_width_lo = (vsync_offset & 0xF) << 4
					  | (vsync_pulse_width & 0xF);
	pt->hsync_vsync_offset_pulse_width_hi =
		((hsync_offset & 0x300) >> 2) | ((hsync_pulse_width & 0x300) >> 4)
		| ((vsync_offset & 0x30) >> 2) | ((vsync_pulse_width & 0x30) >> 4);

	assert(width_mm <= 0xFFF);
	assert(height_mm <= 0xFFF);
	pt->width_mm_lo = width_mm & 0x0FF;
	pt->height_mm_lo = height_mm & 0x0FF;
	pt->width_height_mm_hi = (width_mm & 0xF00) >> 4
				 | (height_mm & 0xF00) >> 8;

	pt->misc = EDID_PT_SYNC_DIGITAL_SEPARATE;
	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		pt->misc |= EDID_PT_HSYNC_POSITIVE;
	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		pt->misc |= EDID_PT_VSYNC_POSITIVE;
}

/**
 * detailed_timing_set_monitor_range_mode: set a detailed timing to be a
 * monitor range based on a mode
 */
void detailed_timing_set_monitor_range_mode(struct detailed_timing *dt,
					    drmModeModeInfo *mode)
{
	struct detailed_non_pixel *np = &dt->data.other_data;
	struct detailed_data_monitor_range *mr = &np->data.range;

	dt->pixel_clock[0] = dt->pixel_clock[1] = 0;

	np->type = EDID_DETAIL_MONITOR_RANGE;

	mr->min_vfreq = mode->vrefresh - 1;
	mr->max_vfreq = mode->vrefresh + 1;
	mr->min_hfreq_khz = (mode->clock / mode->htotal) - 1;
	mr->max_hfreq_khz = (mode->clock / mode->htotal) + 1;
	mr->pixel_clock_mhz = (mode->clock / 10000) + 1;
	mr->flags = 0;

	memcpy(mr->formula.pad, monitor_range_padding,
	       sizeof(monitor_range_padding));
}

/**
 * detailed_timing_set_string: set a detailed timing to be a string
 */
void detailed_timing_set_string(struct detailed_timing *dt,
				enum detailed_non_pixel_type type,
				const char *str)
{
	struct detailed_non_pixel *np = &dt->data.other_data;
	struct detailed_data_string *ds = &np->data.string;
	size_t len;

	switch (type) {
	case EDID_DETAIL_MONITOR_NAME:
	case EDID_DETAIL_MONITOR_STRING:
	case EDID_DETAIL_MONITOR_SERIAL:
		break;
	default:
		assert(0); /* not a string type */
	}

	dt->pixel_clock[0] = dt->pixel_clock[1] = 0;

	np->type = type;

	strncpy(ds->str, str, sizeof(ds->str));
	len = strlen(str);
	if (len < sizeof(ds->str))
		ds->str[len] = '\n';
}

static void edid_set_mfg(struct edid *edid, const char mfg[static 3])
{
	edid->mfg_id[0] = (mfg[0] - '@') << 2 | (mfg[1] - '@') >> 3;
	edid->mfg_id[1] = (mfg[1] - '@') << 5 | (mfg[2] - '@');
}

static void edid_set_gamma(struct edid *edid, float gamma)
{
	edid->gamma = (gamma * 100) - 100;
}

/**
 * edid_init: initialize an EDID
 *
 * The EDID will be pre-filled with established and standard timings:
 *
 *  - 1920x1080 60Hz
 *  - 1280x720 60Hz
 *  - 1024x768 60Hz
 *  - 800x600 60Hz
 *  - 640x480 60Hz
 */
void edid_init(struct edid *edid)
{
	size_t i;
	time_t t;
	struct tm *tm;

	memset(edid, 0, sizeof(struct edid));

	memcpy(edid->header, edid_header, sizeof(edid_header));
	edid_set_mfg(edid, "IGT");
	edid->version = 1;
	edid->revision = 3;
	edid->input = 0x80;
	edid->width_cm = 52;
	edid->height_cm = 30;
	edid_set_gamma(edid, 2.20);
	edid->features = 0x02;

	/* Year of manufacture */
	t = time(NULL);
	tm = localtime(&t);
	edid->mfg_year = tm->tm_year - 90;

	/* Established timings: 640x480 60Hz, 800x600 60Hz, 1024x768 60Hz */
	edid->established_timings.t1 = 0x21;
	edid->established_timings.t2 = 0x08;

	/* Standard timings */
	/* 1920x1080 60Hz */
	std_timing_set(&edid->standard_timings[0], 1920, 60, STD_TIMING_16_9);
	/* 1280x720 60Hz */
	std_timing_set(&edid->standard_timings[1], 1280, 60, STD_TIMING_16_9);
	/* 1024x768 60Hz */
	std_timing_set(&edid->standard_timings[2], 1024, 60, STD_TIMING_4_3);
	/* 800x600 60Hz */
	std_timing_set(&edid->standard_timings[3], 800, 60, STD_TIMING_4_3);
	/* 640x480 60Hz */
	std_timing_set(&edid->standard_timings[4], 640, 60, STD_TIMING_4_3);
	for (i = 5; i < STD_TIMINGS_LEN; i++)
		std_timing_unset(&edid->standard_timings[i]);
}

/**
 * edid_init_with_mode: initialize an EDID and sets its preferred mode
 */
void edid_init_with_mode(struct edid *edid, drmModeModeInfo *mode)
{
	edid_init(edid);

	/* Preferred timing */
	detailed_timing_set_mode(&edid->detailed_timings[0], mode,
				 edid->width_cm * 10, edid->height_cm * 10);
	detailed_timing_set_monitor_range_mode(&edid->detailed_timings[1],
					       mode);
	detailed_timing_set_string(&edid->detailed_timings[2],
				   EDID_DETAIL_MONITOR_NAME, "IGT");
}

static uint8_t compute_checksum(const uint8_t *buf, size_t size)
{
	size_t i;
	uint8_t sum = 0;

	assert(size > 0);
	for (i = 0; i < size - 1; i++) {
		sum += buf[i];
	}

	return 256 - sum;
}

/**
 * edid_update_checksum: compute and update the EDID checksum
 */
void edid_update_checksum(struct edid *edid)
{
	edid->checksum = compute_checksum((uint8_t *) edid,
					  sizeof(struct edid));
}

/**
 * cea_sad_init_pcm:
 * @channels: the number of supported channels (max. 8)
 * @sampling_rates: bitfield of enum cea_sad_sampling_rate
 * @sample_sizes: bitfield of enum cea_sad_pcm_sample_size
 *
 * Initialize a Short Audio Descriptor to advertise PCM support.
 */
void cea_sad_init_pcm(struct cea_sad *sad, int channels,
		      uint8_t sampling_rates, uint8_t sample_sizes)
{
	assert(channels <= 8);
	sad->format_channels = CEA_SAD_FORMAT_PCM << 3 | (channels - 1);
	sad->sampling_rates = sampling_rates;
	sad->bitrate = sample_sizes;
}

/**
 * cea_vsd_get_hdmi_default:
 *
 * Returns the default Vendor Specific Data block for HDMI.
 */
const struct cea_vsd *cea_vsd_get_hdmi_default(size_t *size)
{
	static char raw[sizeof(struct cea_vsd) + 4] = {0};
	struct cea_vsd *vsd;

	*size = sizeof(raw);

	/* Magic incantation. Works better if you orient your screen in the
	 * direction of the VESA headquarters. */
	vsd = (struct cea_vsd *) raw;
	vsd->ieee_oui[0] = 0x03;
	vsd->ieee_oui[1] = 0x0C;
	vsd->ieee_oui[2] = 0x00;
	vsd->data[0] = 0x10;
	vsd->data[1] = 0x00;
	vsd->data[2] = 0x38;
	vsd->data[3] = 0x2D;

	return vsd;
}

static void edid_cea_data_block_init(struct edid_cea_data_block *block,
				     enum edid_cea_data_type type, size_t size)
{
	assert(size <= 0xFF);
	block->type_len = type << 5 | size;
}

size_t edid_cea_data_block_set_sad(struct edid_cea_data_block *block,
				   const struct cea_sad *sads, size_t sads_len)
{
	size_t sads_size;

	sads_size = sizeof(struct cea_sad) * sads_len;
	edid_cea_data_block_init(block, EDID_CEA_DATA_AUDIO, sads_size);

	memcpy(block->data.sads, sads, sads_size);

	return sizeof(struct edid_cea_data_block) + sads_size;
}

size_t edid_cea_data_block_set_vsd(struct edid_cea_data_block *block,
				   const struct cea_vsd *vsd, size_t vsd_size)
{
	edid_cea_data_block_init(block, EDID_CEA_DATA_VENDOR_SPECIFIC,
				 vsd_size);

	memcpy(block->data.vsds, vsd, vsd_size);

	return sizeof(struct edid_cea_data_block) + vsd_size;
}

void edid_ext_set_cea(struct edid_ext *ext, size_t data_blocks_size,
		      uint8_t flags)
{
	struct edid_cea *cea = &ext->data.cea;

	ext->tag = EDID_EXT_CEA;

	cea->revision = 3;
	cea->dtd_start = 4 + data_blocks_size;
	cea->misc = flags; /* just flags, no DTD */
}

void edid_ext_update_cea_checksum(struct edid_ext *ext)
{
	ext->data.cea.checksum = compute_checksum((uint8_t *) ext,
						  sizeof(struct edid_ext));
}
