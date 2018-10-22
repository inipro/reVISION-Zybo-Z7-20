#include <common/xf_common.h>
#include <imgproc/xf_custom_convolution.hpp>
#include <linux/videodev2.h>
#include <stdlib.h>

#include "filter2d_sds.h"

//#define F2D_HEIGHT	2160
//#define F2D_WIDTH	3840
#define F2D_HEIGHT	1080
#define F2D_WIDTH	1920

using namespace xf;

struct filter2d_data {
	xf::Mat<XF_8UC1, F2D_HEIGHT, F2D_WIDTH, XF_NPPC1> *inLuma;
	xf::Mat<XF_8UC1, F2D_HEIGHT, F2D_WIDTH, XF_NPPC1> *outLuma;
};

#pragma SDS data mem_attribute(frm_data_in:NON_CACHEABLE|PHYSICAL_CONTIGUOUS)
#pragma SDS data copy(frm_data_in[0:pcnt*3])
#pragma SDS data access_pattern(frm_data_in:SEQUENTIAL)
#pragma SDS data mem_attribute("inLuma.data":NON_CACHEABLE|PHYSICAL_CONTIGUOUS)
#pragma SDS data copy("inLuma.data"[0:"inLuma.size"])
#pragma SDS data access_pattern("inLuma.data":SEQUENTIAL)
void read_f2d_input(unsigned short *frm_data_in,
				xf::Mat<XF_8UC1, F2D_HEIGHT, F2D_WIDTH, XF_NPPC1> &inLuma,
				int pcnt)
{

	for (int i=0; i<pcnt; i++) {
#pragma HLS pipeline II=1
		uint16_t rpix =  (uint16_t)frm_data_in[3*i];
		uint16_t gpix =  (uint16_t)frm_data_in[3*i+1];
		uint16_t bpix =  (uint16_t)frm_data_in[3*i+2];

		XF_TNAME(XF_8UC1,XF_NPPC1) graypix = (XF_TNAME(XF_8UC1,XF_NPPC1))((rpix*76 + gpix*150 + bpix*29 + 128) >> 8);

		inLuma.data[i] = graypix;
	}
}

#pragma SDS data mem_attribute("outLuma.data":NON_CACHEABLE|PHYSICAL_CONTIGUOUS)
#pragma SDS data copy("outLuma.data"[0:"outLuma.size"])
#pragma SDS data access_pattern("outLuma.data":SEQUENTIAL)
#pragma SDS data mem_attribute(frm_data_out:NON_CACHEABLE|PHYSICAL_CONTIGUOUS)
#pragma SDS data copy(frm_data_out[0:pcnt*3])
#pragma SDS data access_pattern(frm_data_out:SEQUENTIAL)
void write_f2d_output(xf::Mat<XF_8UC1, F2D_HEIGHT, F2D_WIDTH, XF_NPPC1> &outLuma,
					unsigned short *frm_data_out, int pcnt)
{
	for (int i=0; i<pcnt; i++) {
#pragma HLS pipeline II=1
		uint8_t graypix = outLuma.data[i];
		frm_data_out[i*3] = graypix;
		frm_data_out[i*3+1] = graypix;
		frm_data_out[i*3+2] = graypix;
	}
}


int filter2d_init_sds(size_t in_height, size_t in_width, size_t out_height,
		size_t out_width, uint32_t in_fourcc,
		uint32_t out_fourcc, void **priv)
{
	struct filter2d_data *f2d = (struct filter2d_data *)malloc(sizeof(struct filter2d_data));
	if (f2d == NULL) {
		return -1;
	}

	f2d->inLuma = new xf::Mat<XF_8UC1, F2D_HEIGHT, F2D_WIDTH, XF_NPPC1>(in_height, in_width);
	f2d->outLuma = new xf::Mat<XF_8UC1, F2D_HEIGHT, F2D_WIDTH, XF_NPPC1>(in_height, in_width);

	*priv = f2d;

	return 0;
}

void filter2d_sds(unsigned char *frm_data_in, unsigned char *frm_data_out,
				int height, int width, const coeff_t coeff, void *priv)
{
	struct filter2d_data *f2d = (struct filter2d_data *) priv;
	int pcnt = height*width;

	// split the 16b YUYV... input data into separate 8b YYYY... and 8b UVUV...
	read_f2d_input(frm_data_in, *f2d->inLuma, pcnt);

	// this is the xfopencv version of filter2D, found in imgproc/xf_custom_convolution.hpp
	xf::filter2D<XF_BORDER_CONSTANT, KSIZE, KSIZE, XF_8UC1, XF_8UC1, F2D_HEIGHT, F2D_WIDTH, XF_NPPC1>
		(*f2d->inLuma, *f2d->outLuma, (short int *) coeff, 0);
	//dummyFilter(*f2d->inLuma, *f2d->outLuma);

	// combine separate 8b YYYY... and 8b UVUV... data into 16b YUYV... output data
	write_f2d_output(*f2d->outLuma,  frm_data_out, pcnt);
}
