/***************************************************************************
Copyright (c) 2016, Xilinx, Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, 
this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, 
this list of conditions and the following disclaimer in the documentation 
and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors 
may be used to endorse or promote products derived from this software 
without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, 
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

***************************************************************************/

#ifndef _XF_HOG_DESCRIPTOR_HIST_NORM_HPP_
#define _XF_HOG_DESCRIPTOR_HIST_NORM_HPP_

#ifndef __cplusplus
#error C++ is needed to include this header
#endif

#include "imgproc/xf_hog_descriptor_compute_hist.hpp"
#include "imgproc/xf_hog_descriptor_norm.hpp"


/*******************************************************************************
 * 						 xFDHOGDescriptorKernel
 *******************************************************************************
 *
 * _phase_strm :phase computed image of depth XF_9UP.
 *
 *  _mag_strm :magnitude computed image of depth XF_9UP.
 *
 *  _block_strm : block  (O) desc data written to this stream
 *
 *******************************************************************************/

template < int ROWS, int COLS, int DEPTH_SRC, int DEPTH_DST, int NPC,
int WORD_WIDTH_SRC, int WORD_WIDTH_DST, int WIN_HEIGHT, int WIN_WIDTH,
int WIN_STRIDE, int CELL_HEIGHT, int CELL_WIDTH, int NOB, int NOHCPB,
int NOVCPB, int NOVW, int NOHW, int NOVC, int NOHC, int NOVB, int NOHB,
int BIN_STRIDE >
void xFDHOGDescriptorKernel (
		hls::stream<XF_SNAME(WORD_WIDTH_SRC)>& _phase_strm,
		hls::stream<XF_SNAME(WORD_WIDTH_SRC)>& _mag_strm,
		hls::stream<XF_SNAME(WORD_WIDTH_DST)>& _block_strm,
		uint16_t _height,uint16_t _width,uint16_t novw, uint16_t nohw, uint16_t novc, uint16_t nohc, uint16_t novb, uint16_t nohb)
{
	// array to store the histogram computed data
	ap_uint<23> HA_1[NOHC][NOB], HA_2[NOHC][NOB], HA_3[NOHC][NOB];

	// partitioning across the dim-2 to restrict the BRAM utilization
#pragma HLS ARRAY_PARTITION variable=HA_1 complete dim=2
#pragma HLS ARRAY_PARTITION variable=HA_2 complete dim=2
#pragma HLS ARRAY_PARTITION variable=HA_3 complete dim=2

	// specifying the dual-port BRAM
#pragma HLS RESOURCE variable=HA_1 core=RAM_S2P_BRAM
#pragma HLS RESOURCE variable=HA_2 core=RAM_S2P_BRAM
#pragma HLS RESOURCE variable=HA_3 core=RAM_S2P_BRAM

	// array to hold the sum of squared values of each cell
	ap_uint<45> ssv_1[NOHC], ssv_2[NOHC], ssv_3[NOHC];
#pragma HLS RESOURCE variable=ssv_1 core=RAM_S2P_BRAM
#pragma HLS RESOURCE variable=ssv_2 core=RAM_S2P_BRAM
#pragma HLS RESOURCE variable=ssv_3 core=RAM_S2P_BRAM

	// bin center computation, in the Q9.7 format
	uint16_t bin_center[NOB];
#pragma HLS ARRAY_PARTITION variable=bin_center complete dim=0

	uint16_t offset = ((BIN_STRIDE<<7)>>1), bi = 0;
	for(uchar_t i = 0; i < NOB; i++)
	{
#pragma HLS PIPELINE

		bin_center[i] = offset;
		offset += (BIN_STRIDE<<7);
	}

	ap_uint<2> idx;

	// computing two horizontal cell rows
	xFDHOGcomputeHist<ROWS,COLS,DEPTH_SRC,NPC,WORD_WIDTH_SRC,CELL_HEIGHT,CELL_WIDTH,NOHC,
	(COLS>>XF_BITSHIFT(NPC)),WIN_STRIDE,BIN_STRIDE,NOB>(_phase_strm,_mag_strm,HA_1,ssv_1,bin_center,nohc);

	xFDHOGcomputeHist<ROWS,COLS,DEPTH_SRC,NPC,WORD_WIDTH_SRC,CELL_HEIGHT,CELL_WIDTH,NOHC,
	(COLS>>XF_BITSHIFT(NPC)),WIN_STRIDE,BIN_STRIDE,NOB>(_phase_strm,_mag_strm,HA_2,ssv_2,bin_center,nohc);
	idx = 2;

	// loop running for vertical block number of times
	verticalBlockLoop:
	for(bi = 0; bi < (novb-1); bi++)
	{
#pragma HLS LOOP_TRIPCOUNT min=NOVB-1 max=NOVB-1
		if(idx == 2)
		{
			xFDHOGcomputeHist<ROWS,COLS,DEPTH_SRC,NPC,WORD_WIDTH_SRC,CELL_HEIGHT,CELL_WIDTH,NOHC,
			(COLS>>XF_BITSHIFT(NPC)),WIN_STRIDE,BIN_STRIDE,NOB>(_phase_strm,_mag_strm,HA_3,ssv_3,bin_center,nohc);

			xFDHOGNormalize<ROWS,COLS,DEPTH_DST,NPC,WORD_WIDTH_DST,NOHC,NOHCPB,
			NOVCPB,NOHW,NOVW,(NOB*NOHCPB*NOVCPB),WIN_HEIGHT,WIN_WIDTH,CELL_HEIGHT,
			CELL_WIDTH,NOHB,NOB>(HA_1,HA_2,ssv_1,ssv_2,_block_strm,bi,nohb,nohc);

			idx = 0;
		}
		else if(idx == 0)
		{
			xFDHOGcomputeHist<ROWS,COLS,DEPTH_SRC,NPC,WORD_WIDTH_SRC,CELL_HEIGHT,CELL_WIDTH,NOHC,
			(COLS>>XF_BITSHIFT(NPC)),WIN_STRIDE,BIN_STRIDE,NOB>(_phase_strm,_mag_strm,HA_1,ssv_1,bin_center,nohc);

			xFDHOGNormalize<ROWS,COLS,DEPTH_DST,NPC,WORD_WIDTH_DST,NOHC,NOHCPB,
			NOVCPB,NOHW,NOVW,(NOB*NOHCPB*NOVCPB),WIN_HEIGHT,WIN_WIDTH,CELL_HEIGHT,
			CELL_WIDTH,NOHB,NOB>(HA_2,HA_3,ssv_2,ssv_3,_block_strm,bi,nohb,nohc);

			idx = 1;
		}
		else
		{
			xFDHOGcomputeHist<ROWS,COLS,DEPTH_SRC,NPC,WORD_WIDTH_SRC,CELL_HEIGHT,CELL_WIDTH,NOHC,
			(COLS>>XF_BITSHIFT(NPC)),WIN_STRIDE,BIN_STRIDE,NOB>(_phase_strm,_mag_strm,HA_2,ssv_2,bin_center,nohc);

			xFDHOGNormalize<ROWS,COLS,DEPTH_DST,NPC,WORD_WIDTH_DST,NOHC,NOHCPB,
			NOVCPB,NOHW,NOVW,(NOB*NOHCPB*NOVCPB),WIN_HEIGHT,WIN_WIDTH,CELL_HEIGHT,
			CELL_WIDTH,NOHB,NOB>(HA_3,HA_1,ssv_3,ssv_1,_block_strm,bi,nohb,nohc);

			idx = 2;
		}
	}
	if(idx == 2)
	{
		xFDHOGNormalize < ROWS,COLS,DEPTH_DST,NPC,WORD_WIDTH_DST,NOHC,NOHCPB,
		NOVCPB,NOHW,NOVW,(NOB*NOHCPB*NOVCPB),WIN_HEIGHT,WIN_WIDTH,CELL_HEIGHT,
		CELL_WIDTH,NOHB,NOB > (HA_1,HA_2,ssv_1,ssv_2,_block_strm,bi,nohb,nohc);
	}
	else if(idx == 0)
	{
		xFDHOGNormalize < ROWS,COLS,DEPTH_DST,NPC,WORD_WIDTH_DST,NOHC,NOHCPB,
		NOVCPB,NOHW,NOVW,(NOB*NOHCPB*NOVCPB),WIN_HEIGHT,WIN_WIDTH,CELL_HEIGHT,
		CELL_WIDTH,NOHB,NOB > (HA_2,HA_3,ssv_2,ssv_3,_block_strm,bi,nohb,nohc);
	}
	else
	{
		xFDHOGNormalize < ROWS,COLS,DEPTH_DST,NPC,WORD_WIDTH_DST,NOHC,NOHCPB,
		NOVCPB,NOHW,NOVW,(NOB*NOHCPB*NOVCPB),WIN_HEIGHT,WIN_WIDTH,CELL_HEIGHT,
		CELL_WIDTH,NOHB,NOB > (HA_3,HA_1,ssv_3,ssv_1,_block_strm,bi,nohb,nohc);
	}
}


/******************************************************************************
 * xFDHOGDescriptor: This function acts as a wrapper function for the
 * 		HoG descriptor function
 *****************************************************************************/
template<int WIN_HEIGHT, int WIN_WIDTH, int WIN_STRIDE, int CELL_HEIGHT,
int CELL_WIDTH, int NOB, int NOHCPB, int NOVCPB, int ROWS, int COLS,
int DEPTH_SRC, int DEPTH_DST,int NPC,int WORDWIDTH_SRC, int WORDWIDTH_DST>
void xFDHOGDescriptor(
		hls::stream<XF_SNAME(WORDWIDTH_SRC)>& _phase_strm,
		hls::stream<XF_SNAME(WORDWIDTH_SRC)>& _mag_strm,
		hls::stream<XF_SNAME(WORDWIDTH_DST)>& _block_strm,
		uint16_t _height, uint16_t _width)
{
	assert((DEPTH_SRC == XF_16UP) &&
			"DEPTH_DST must be XF_16UP");
	assert((DEPTH_DST == XF_16UP) &&
			"DEPTH_DST must be XF_16UP");
	assert(((NPC == XF_NPPC1) || (NPC == XF_NPPC8) || (NPC == XF_NPPC16)) &&
			"NPC must be XF_NPPC1, XF_NPPC8 or XF_NPPC16");
	assert(((WORDWIDTH_SRC == XF_16UW) || (WORDWIDTH_SRC == XF_128UW) ||
			(WORDWIDTH_SRC == XF_256UW)) &&
			"WORDWIDTH_SRC must be XF_16UW, XF_128UW or XF_256UW");
	assert((WORDWIDTH_DST == XF_576UW) &&
			"WORDWIDTH_DST must be XF_576UW");
	assert(((WIN_HEIGHT%CELL_HEIGHT) == 0) &&
			"WIN_HEIGHT must be a multiple of CELL_HEIGHT");
	assert(((WIN_WIDTH%CELL_WIDTH) == 0) &&
			"WIN_WIDTH must be a multiple of CELL_WIDTH");
	assert(((WIN_HEIGHT <= ROWS) || (WIN_WIDTH <= COLS)) &&
			"WIN_HEIGHT and WIN_WIDTH must be less than or equal to the image _height and image _width respectively");
	assert((((ROWS-WIN_HEIGHT)%WIN_STRIDE == 0) || ((COLS-WIN_WIDTH)%WIN_STRIDE == 0)) &&
			"The number of windows must not extend the image boundary limit");
	assert(((CELL_HEIGHT == 8)) &&
			"CELL_HEIGHT must be 8");
	assert(((CELL_WIDTH == 8)) &&
			"CELL_WIDTH must be 8");
	assert((NOB == 9) && "NOB must be 9");
	assert(((NOHCPB == 2) && (NOVCPB == 2)) &&
			"number of horizontal and vertical cells per block must be 2");

	uint16_t novw =(((_height-WIN_HEIGHT)/WIN_STRIDE)+1);
	uint16_t nohw =(((_width-WIN_WIDTH)/WIN_STRIDE)+1);
	uint16_t novc=(_height/CELL_HEIGHT);

	uint16_t nohc =(_width/CELL_WIDTH);
	uint16_t novb=((_height/CELL_HEIGHT)-1);
	uint16_t nohb=((_width/CELL_WIDTH)-1);

	xFDHOGDescriptorKernel <
	ROWS,
	COLS,
	DEPTH_SRC,
	DEPTH_DST,
	NPC,
	WORDWIDTH_SRC,
	WORDWIDTH_DST,
	WIN_HEIGHT,
	WIN_WIDTH,
	WIN_STRIDE,
	CELL_HEIGHT,
	CELL_WIDTH,
	NOB,
	NOHCPB,
	NOVCPB,
	(((ROWS-WIN_HEIGHT)/WIN_STRIDE)+1),
	(((COLS-WIN_WIDTH)/WIN_STRIDE)+1),
	(ROWS/CELL_HEIGHT),
	(COLS/CELL_WIDTH),
	((ROWS/CELL_HEIGHT)-1),
	((COLS/CELL_WIDTH)-1),
	(180/NOB)>
	(_phase_strm,_mag_strm,_block_strm,_height,_width,novw,nohw,novc,nohc,novb,nohb);
}

#endif   // _XF_HOG_DESCRIPTOR_KERNEL_HPP_

